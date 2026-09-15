# Port checklist — `FilterDrum`

Working copy for this plug-in. **FilterDrum is not a port** — it was
scaffolded blank, so Phase 1 and Phase 4's "ported line for line" items
do not apply and are struck through. Everything else does. See
`PORTING-NOTES.md` for what has already been decided and verified.

`PORTING-GUIDE.md` is the reference behind every step; the section numbers
here point into it. Phases end where a **build** is needed — those are the
handoffs, because the assistant session reaches this Mac through a Linux VM
and cannot run cmake, Xcode or the validator.

**Legend** — 🧑 you · 🤖 assistant

---

## Decisions to record

Fill these in during Phase 1 and do not change them afterwards.

| | Value |
|---|---|
| Plug-in name | `FilterDrum` |
| Effect or instrument | `aumu` (instrument) |
| VST3 category | `PlugType::kInstrumentDrum` |
| AU subtype (4 chars, unique per plug-in) | `FDrm` |
| AU manufacturer (4 chars, same across your plug-ins) | `AECo` |
| Bundle id, VST3 | `audio.filterdrum.vst3` |
| Bundle id, AU | `audio.filterdrum.audiounit` |
| Processor class UID | `0x155EA437, 0xC565701B, 0xCA9B562F, 0x151DE2EE` |
| Controller class UID | `0x813A44E4, 0x5D8B2936, 0xA835509C, 0x6BCEF690` |

> The four-character codes and both class UIDs are permanent once you ship a
> build. Change one and every existing session loses the plug-in.

---

## Phase 1 — Reconnaissance  (no code written)

- [ ] 🤖 Read the whole DXi tree
- [ ] 🤖 **Is it really an effect?** Does the loop read its input buffer? Does
      anything read the MIDI queue? Yes/no ⇒ audio effect, and the whole MFX
      layer is deleted rather than ported *(guide §1)*
- [ ] 🤖 **Which processing loop is real?** Diff the near-identical copies;
      port the latest, identified by the features only it has *(§1)*
- [ ] 🤖 Parameter inventory — every parameter, **both** ranges (external from
      `Parameters.h`, internal from `MapToInternal`), defaults, and any label
      that lies about what it does *(§4)*
- [ ] 🤖 Dialog geometry — solve the dialog-unit scale from a control whose
      bitmap size is known, **verify against a second control**, extract every
      position as data *(§6)*
- [ ] 🤖 Read `resource.h` — controls listed but absent from the shipped
      dialog are cut features, sometimes worth reinstating
- [ ] 🧑 Confirm effect vs instrument, and choose the four-character codes
- [ ] 🤖 Note anything the property page did *itself* — link switches, timers,
      readouts — these have no parameter behind them and are easy to miss

**Exit:** the table above is filled in and the parameter list is agreed.
This phase is cheap to redo and expensive to get wrong. Don't rush it.

---

## Phase 2 — Scaffold, and the first build

- [x] 🧑 `cp -R ~/DXi-DEv/vst3-port-template ~/DXi-DEv/FilterDrum-VSTi`
- [x] 🤖 Edit the `PLUG-IN IDENTITY` block in `CMakeLists.txt` — and nothing
      else in that file
- [x] 🤖 Fill in `resource/au-info.plist` (every `CHANGE ME`), and make
      `AudioUnit SupportedNumChannels` match what `setBusArrangements` will
      accept *(§9)* — cut to one `0 in / 2 out` entry; the template's `1/1`
      is gone
- [x] 🤖 Generate two fresh class UIDs
- [x] ~~🤖 Convert artwork `res/*.bmp` → `resource/*.png`~~ — no artwork:
      every control draws with rectangles and text
- [x] 🤖 Write the seven stubs — **plus** an SDK-free `FilterDrumDsp.*` and
      an editor, both deliberate; see `PORTING-NOTES.md` §1
- [x] 🤖 `processContextRequirements` — **deliberately absent**, nothing reads
      the tempo yet. The replacement code is written out in
      `FilterDrumProcessor.h`'s banner *(§7 — silent failure otherwise)*
- [x] 🤖 DSP suite written and run: **62 checks, 0 failures**; every `.cpp`
      compiles against the SDK and `nm -C` shows 0 unresolved symbols
- [ ] 🧑 `./setup-xcode.sh` — first configure fetches ~250 MB, takes minutes
- [ ] 🧑 Build. **Milestone: a silent plug-in that passes the validator.**

**Exit:** validator clean. Almost everything structural surfaces here, while
there are hundreds of lines to search rather than thousands.

---

## Phase 3 — Parameters

- [ ] 🤖 Parameter table with all three ranges and a `toInternal()`
      reproducing `MapToInternal` exactly *(§4)*
- [ ] 🤖 `kIsBypass` added; any DXi "enabled" switch kept **separate**, with
      its original default
- [ ] 🤖 Enum step counts preserved even where values repeat
- [ ] 🤖 Never index the table with an unchecked `ParamID` — `kBypass` is 1000
- [ ] 🤖 `USTRING(...)` for every title and units — a null segfaults the
      validator, not your plug-in *(§7)*
- [ ] 🧑 Build; the validator exercises parameter round-tripping hard

---

## Phase 4 — DSP

- [ ] 🤖 Ported line for line, **free of SDK types** so it compiles standalone
- [ ] 🤖 Resist improving it — odd gain staging and apparent bugs are the
      sound *(§5)*
- [ ] 🤖 Fix only the genuinely broken: `sizeof(pointer)` in a `memset`,
      allocation on the audio thread, unbounded feedback/resonance, no
      denormal handling, 32-bit-float only, stereo only *(§5)*
- [ ] 🤖 Per-sample smoothing on the same parameters the DXi interpolated
- [ ] 🤖 `getTailSamples()` rather than porting the tail readers
- [ ] 🤖 Standalone tests that actually run — tap positions, quantisation
      grids, mapping functions at every realistic sample rate *(§8)*
- [ ] 🤖 Every deliberate behaviour change **proved a no-op** across normal
      settings before it is claimed
- [ ] 🧑 Build and listen. First noise.

---

## Phase 5 — Editor

- [ ] 🤖 Positions from the Phase 1 geometry — never placed by eye *(§6)*
- [ ] 🤖 Check `OnVScroll`: vertical sliders usually ran **top = maximum**
- [ ] 🤖 Port what the property page did itself — link switches issue a
      `performEdit` for the partner so the host records both sides
- [ ] 🤖 Value readouts always visible; **tooltips may never appear on
      macOS** *(§6)*
- [ ] 🤖 Anything the editor displays that the DSP computes comes from **one
      shared static function** both call *(§7)*
- [ ] 🤖 `editorDestroyed` compares upcast pointers, not `dynamic_cast` *(§7)*
- [ ] 🤖 Processor → controller values go via `outputParameterChanges`, never
      `sendMessage` from the audio thread *(§7)*
- [ ] 🧑 Build and eyeball against a screenshot of the original

---

## Phase 6 — Audio Unit and ship checks

- [ ] 🧑 Build the `<PlugIn>-au` scheme (Xcode generator only)
- [ ] 🧑 `auval -v aufx <subtype> <manufacturer>`
- [ ] 🧑 Validator against a **Release** build
- [ ] 🧑 Test in a real host
- [ ] 🧑 Before handing it to anyone: the installed `.component` holds a
      **symlink into your build tree** — replace it with the real `.vst3`
      bundle *(§9)*
- [ ] 🧑 Real signing identity, if it is going further than this machine

---

## Throughout

- [ ] 🤖 `PORTING-NOTES.md` written **as you go**, not at the end: the
      parameter mapping, every deliberate deviation and why, the UIDs and
      codes marked *never change*, and every trap hit with the symptom
      actually seen *(§10)*
- [ ] 🧑 Save build logs into the project folder — the assistant can read them
      there without you pasting
