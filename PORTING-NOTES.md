# FilterDrum — porting notes

Nothing has been ported yet. This file exists from the first commit
because it is the one that outlives the session: it records the decisions
that are permanent, the traps that are already handled, the traps that
are deliberately *not* handled yet, and exactly what was verified and
how. Add to it as you go, not at the end.

Scaffolded 2026-09-15 from `~/DXi-DEv/vst3-port-template`, with the
editor control set lifted from `Project6-VSTi`.

---

## 1. The four decisions

| | Value | Why |
|---|---|---|
| Effect or instrument | **Instrument** (`aumu`, `PlugType::kInstrumentDrum`) | A drum machine generates audio from notes. Sets the AU type code, the event input, and the absence of an audio input bus. |
| Bus layout | **One stereo output, no audio input** | Simplest thing that validates. Per-pad aux outputs can be *appended* later as extra buses — `Project6-VSTi` is the worked example of nine output buses, and note there that the AU plist entry still describes the main element only. |
| Name and codes | `FilterDrum` / `FDrm` / `AECo` | See §2. `FDrm` checked against every other `au-info.plist` in `~/DXi-DEv`: `FTrn`, `Prj6`, `SDub`, `SpyB`, `VcFl`. No clash. |
| Editor now or later | **Now**, with the Project6 control set | The skill's recommendation is *later*, and it was overridden deliberately: the controls lift with a namespace rename only (no bitmaps, nothing Project6-specific), so the cost is small and the UI path is proven at the same time as the parameter path. If the first build's validator run is noisy, the editor is the first thing to bisect out — `createView` returning `nullptr` disables it without touching anything else. |

---

## 2. Permanent identity — **never change any of this**

Hosts store these in the project file. Change one after a build has
shipped and every existing session silently loses the plug-in.

| | Value | Where |
|---|---|---|
| Processor class UID | `0x155EA437, 0xC565701B, 0xCA9B562F, 0x151DE2EE` | `source/FilterDrumIDs.h` |
| Controller class UID | `0x813A44E4, 0x5D8B2936, 0xA835509C, 0x6BCEF690` | `source/FilterDrumIDs.h` |
| AU type | `aumu` | `resource/au-info.plist` |
| AU subtype | `FDrm` | `resource/au-info.plist` |
| AU manufacturer | `AECo` | `resource/au-info.plist` — shared across all these plug-ins |
| VST3 bundle id | `audio.filterdrum.vst3` | `CMakeLists.txt` |
| AU bundle id | `audio.filterdrum.audiounit` | `CMakeLists.txt` |

Both UIDs were generated fresh from `os.urandom`. Neither is reused from
another project.

Validate with: `auval -v aumu FDrm AECo`

---

## 3. Why the DSP is SDK-free

`source/FilterDrumDsp.{h,cpp}` contains no `#include` of anything under
`pluginterfaces/` or `public.sdk/`, and must not acquire one. Two
reasons, and the first is the practical one:

1. **It is the only thing that executes during development.** The
   assistant session reaches this Mac through a Linux VM and cannot run
   cmake, Xcode, the SDK validator or `auval` at all. `tests/DspTests.cpp`
   compiles straight against `FilterDrumDsp.cpp` with plain `g++`, so the
   numbers can be tested before anything is built. The moment an SDK
   header enters that file, the suite stops compiling and the only
   executable verification on the project goes with it.

2. **The processor and the controller do not share memory.** Anything the
   editor *displays* that the DSP *computes* has to come from one shared
   function both call, or the two copies of the arithmetic drift apart and
   the panel starts lying about what you are hearing. `dbToGain()` /
   `gainToDb()` in `FilterDrumDsp.h` are the first of those; the editor's
   readout calls `dbToGain` rather than doing its own `pow`.

---

## 4. Measured default output level

    48 kHz, 4096-sample block, Output Trim at its default of 0.0 dB
    peak: L -inf dBFS, R -inf dBFS  (exact silence)

Exact silence, because `FilterDrumDsp::renderVoices()` is empty. The
measurement looks pointless today and is written now on purpose: **it is
the measurement the first real DSP has to repeat.** A default patch that
clips masks every other fault in the signal path and sends you chasing
the wrong bug. When `renderVoices` does something, change that assertion
in `measureDefaultLevel()` to insist on a peak comfortably below 0 dBFS
— do not delete it.

Two other assertions guard the same area and are worth keeping:

* unity is **bit-identical**, not approximately so — `dbToGain(0)` returns
  exactly `1.0` and `applyTrim` multiplies by exactly `1.0f`;
* **the negative control**: the same comparison at half gain must *fail*
  to be identical. A guard that has never failed is a guess. The harness
  itself was also proved capable of failing, by temporarily asserting
  `dbToGain(0.0) == 2.0` and watching it report `62 checks, 1 failures`
  with exit status 1.

---

## 5. Parameters

One parameter, on purpose.

| id | title | units | plain range | default | internal range | smoothed |
|---|---|---|---|---|---|---|
| `kOutputTrim` = 0 | Output Trim | dB | −24 … +12 | 0 | −24 … +12 | yes |

`kBypass` = 1000. No output (read-only) parameters yet; ids from 1001 up
are reserved for them.

**Internal == plain for every parameter here, and the slot is empty
rather than absent.** The table carries all three ranges — normalised,
plain, internal — because a ported DXi parameter needs the internal one
(`CParamEnvelope::MapToInternal`), and the first one ported in should find
the machinery already there rather than have to invent it. `toInternal()`
is called everywhere it will eventually be needed, including where it is
currently a no-op.

The trim is not decoration: it is the only thing that proves host →
`inputParameterChanges` → DSP → panel end to end. A scaffold with no
parameters validates without ever exercising any of that.

---

## 6. Traps already handled (each with the symptom it produces)

* **`kBypass` is 1000, far past the end of a one-entry table.**
  `paramDef()` is the only place `kParams` is indexed and range-checks
  first; `isTableParam()` is the test for callers that must distinguish
  "not a table parameter" from "the first one". Unchecked, `kParams[1000]`
  reads a thousand entries off the end.
* **Never a null title or units to `RangeParameter`.** It hands both to
  `UString::assign`, which dereferences without a check. The symptom is
  *the SDK validator segfaulting in the post-build step*, which sends you
  looking at the build system. Empty string, never null.
* **`setState` resets everything the stream does not mention back to its
  default, before reading the stream.** Otherwise a host that reuses one
  instance across projects leaves the previous project's values in every
  unmentioned parameter: you open an old song and hear the new one's
  settings, with nothing saying why. The controller's `setComponentState`
  does the same thing in the same order, reading the identical layout.
* **Append parameters, never insert.** An id inserted in the middle
  renumbers everything after it and old projects restore values into the
  wrong parameters, quietly. `kFirstFreeParamSlot` names the place.
* **A message sent from `process()` is silently discarded** by the host's
  connection proxy — it returns success and does nothing. Noted at the
  top of `FilterDrumIDs.h`, with the rule that per-block values go out
  through `data.outputParameterChanges` instead. The one message that
  exists (sample rate) is sent from `setActive`, on the UI thread.
* **`data.numSamples == 0` and `numOutputs < 1` are legal** and arrive in
  practice — hosts use them to deliver automation between audible blocks.
  Parameters are applied *before* the early return, or a parameter-only
  block is thrown away along with the block. The DSP guards the same case
  itself, once, rather than in every voice.
* **A 64-bit host is accepted** (`canProcessSampleSize` takes both), and
  the DSP renders into a float scratch sized in `setupProcessing`, so the
  audio thread never allocates. `process()` also *checks* the block
  against the scratch rather than trusting `maxSamplesPerBlock`, and
  refuses an oversized block rather than growing the buffer on the audio
  thread.
* **`editorDestroyed` compares upcast pointers, not `dynamic_cast`.**
  `EditorView::~EditorView()` calls it after the derived sub-object is
  gone, so `dynamic_cast` yields null and the entry survives as a
  dangling pointer that the next `setParamNormalized` follows.
* **Sample-rate-dependent coefficients are all recomputed in one
  function**, `FilterDrumDsp::setSampleRate`. A coefficient computed once
  at 44.1 k and used at 96 k is the bug that presents as "it sounds wrong
  on his machine only". The smoother's glide is specified in
  *milliseconds*, so switching to 192 k does not make it four times
  faster and bring the click back.
* **`reset()` snaps the trim rather than gliding to it** — a reset that
  left the smoother at zero fades the instrument in over 20 ms on every
  transport start, which reads as a missing first hit.
* **Denormals are flushed on the smoother's state only, never on the
  signal.** The recursive element is the one that decays into denormals;
  flushing the signal would break `applyTrim`'s bit-identity at unity for
  no benefit.
* **Bus layout says the same thing in all four places** —
  `PlugType::kInstrumentDrum` in the entry, the buses added in
  `initialize`, what `setBusArrangements` accepts, and the single
  `0 in / 2 out` entry in the plist. The template's extra `1/1` entry was
  **cut**; a layout listed there that `setBusArrangements` refuses is a
  mismatch `auval` finds and nothing else does.

## 7. Deliberate omissions, and the trap that bites when each is undone

* **No `getProcessContextRequirements` override.** Since VST3 3.7 the
  ProcessContext is opt-in and the default is *no flags*: without it,
  `data.processContext` arrives with nothing valid, everything that reads
  the tempo silently gets 120 in every host, and the validator prints
  `- None` rather than complaining. A drum machine that launches on the
  bar would simply never launch and nothing would say why. Nothing reads
  the tempo yet, so it is absent — the exact replacement code is written
  out in the banner of `FilterDrumProcessor.h`, ready to paste the moment
  a sync division, bar launch or tempo readout appears.
* **No processor → controller messages beyond the sample rate**, and no
  controller → processor message at all. The reserved block in
  `FilterDrumIDs.h` carries the rule for adding one: UI thread only, and
  a host that never connects the two components delivers none of them,
  which is why anything expressible as a number goes through a parameter
  instead.
* **`renderVoices()` is empty.** It is the one empty function; everything
  else is the frame around it. It is called with the block already
  cleared and with `numSamples > 0` and both pointers non-null, so a
  voice loop needs no guards of its own.
* **No artwork in `resource/`.** The lifted controls draw everything with
  rectangles and text — inherited from a DXi property page that used GDI
  — so the panel is resolution-independent for free. Keep any new control
  the same way.
* **Bypass renders silence, not a dry path.** An instrument has no input
  to pass through, and a host expects a bypassed plug-in to be inaudible.

---

## 8. What was verified, and what could not be

The session that wrote this reaches the Mac through a Linux VM: **cmake,
Xcode, the SDK validator and `auval` cannot be run from here.** Nothing
below is a substitute for the first real build. What *was* run, verbatim:

### The DSP suite

    cd ~/DXi-DEv/FilterDrum-VSTi
    g++ -std=c++17 -O2 -Wall -Wextra -Isource \
        -o /tmp/dsptests tests/DspTests.cpp source/FilterDrumDsp.cpp
    /tmp/dsptests

Output:

    FilterDrum DSP tests
    --------------------
    dbToGain / gainToDb
    smoother step limit
    unity passes the signal through untouched
    trim takes effect across its range
    zero frames and null buffers
    default patch level
      measured peak: L -inf (silence) dBFS, R -inf (silence) dBFS
    --------------------
    62 checks, 0 failures

Exit status 0. Every rate-dependent assertion is made at 44.1, 48, 88.2,
96, 176.4 and 192 kHz.

### Every source file compiles, and nothing is left undefined

    SDK=~/DXi-DEv/Project6-VSTi/external/vst3sdk
    for f in source/*.cpp; do
      g++ -c -std=c++17 -DLINUX=1 -DRELEASE=1 \
          -I$SDK -I$SDK/vstgui4 -Isource \
          -o /tmp/objs/$(basename $f .cpp).o $f || echo "FAILED $f"
    done
    nm -C /tmp/objs/*.o | grep " U " | grep "FilterDrum::"

All seven translation units compiled with no errors. The undefined-symbol
list was cross-checked against the defined one: **18 undefined
`FilterDrum` symbols, all 18 defined in another object, 0 unresolved.**

Two details that matter about this check:

* `-DRELEASE=1` is **required** — `fdebug.h` refuses to compile without
  it.
* Compile to **object files** and `nm -C` them, not `-fsyntax-only`: a
  header that declares a function nobody defined passes a syntax check
  and fails at link.

The only warnings are from inside the SDK's own headers (`-Wmultichar` on
VSTGUI's four-character attribute ids). None are from this project's code.

### Still to do, on the Mac

    cd ~/DXi-DEv/FilterDrum-VSTi && ./setup-xcode.sh

then build, and run the SDK validator against a **Release** build and:

    auval -v aumu FDrm AECo

Adding a source file later regenerates the Xcode project mid-build and
compiles the old file list; the symptom is *"Bundle does not export the
required 'GetPluginFactory' function"*. Re-run `./setup-xcode.sh --no-open`
and build again.

---

## 9. The SDK

**In-tree clone**, chosen deliberately. The first `cmake` configure clones
the VST3 SDK (tag `v3.8.1_build_84`, ~250 MB) into `external/`, which is
`.gitignore`d.

Pointing at a sibling project's checkout with
`-DVST3_SDK_ROOT=~/DXi-DEv/Project6-VSTi/external/vst3sdk` would have
saved the clone, but a project whose SDK lives inside another project's
folder is not self-contained, and this one may well ship separately. That
shortcut is for throwaways.

Note that the **verification compile above did** use Project6's checkout —
headers only, read-only, nothing written into it. That is a check running
on a Linux VM, not this project's build configuration.
