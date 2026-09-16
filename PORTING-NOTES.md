# FilterDrum — engineering notes

**FilterDrum is not a port.** It was scaffolded blank from
`~/DXi-DEv/vst3-port-template` (with the editor control set lifted from
`Project6-VSTi`) and then built out as an original monophonic drum voice
around a model of the Korg MS-20 lowpass filter. The porting guide's
rules about audio-thread allocation, denormals, 64-bit hosts, bounded
recursive state and the release tail apply to new DSP exactly as they do
to a port, and are followed here.

This file is the one that outlives the session: the decisions that are
permanent, the traps handled, the traps deliberately left undone, and
exactly what was verified and how. Add to it as you go, not at the end.

Both commits were made on 2026-09-15.

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

## 4. Measured output levels

    48 kHz, 1-second render, velocity 127

    the pair, as a user first hears it
      both drums, mix 50 %               -6.80 dBFS
      both self-oscillating, unity trim  -2.20 dBFS
      both self-oscillating, +12 dB trim +9.80 dBFS

    drum 1 alone, comparable with the pre-pair numbers
      default patch                      -7.87 dBFS
      full resonance, self-oscillating   -3.09 dBFS
      worst case, +12 dB output trim     +8.91 dBFS
      fastest VCA attack                 -8.23 dBFS
      noise level 0, from cold              silence

**The first two are held in place by the test suite**, in deliberately
narrow windows — they are what pin `kVoiceGain`. The fourth is measured
rather than bounded; it used to be the worst case, when a per-note
trigger ping peaked against an envelope that was still opening, and it is
kept because a future excitation would show up there first. They are not decoration: the first version of the voice
measured **+0.09 dBFS on the default patch** — clipping before the user
has touched anything — and `measureDefaultLevel()` is what caught it. A
default patch that clips masks every other fault in the signal path.

The fix was `kVoiceGain = 0.4` in `FilterDrumDsp.h`, a fixed attenuation
**after the filter**. It has to be after: attenuating the noise going
*in* would not touch the loudest thing the plug-in does, because a
self-oscillating filter's amplitude is set by where its diodes limit and
not by how hard it is driven. That constant is the one number in the DSP
chosen by measurement rather than derivation, and the comment on it
records the unattenuated figures so a future change can be judged.

The third figure is a user dialling +12 dB of trim on the loudest patch,
and it is *allowed* to clip — trim above unity is an explicit request.
The test only asserts it stays finite.

Three other assertions guard this area:

* unity output trim is **bit-identical**, not approximately so —
  `dbToGain(0)` returns exactly `1.0` and `applyTrim` multiplies by
  exactly `1.0f`;
* **the negative control**: the same comparison at half gain must *fail*
  to be identical. A guard that has never failed is a guess;
* the default patch stays **an order of magnitude below `kStateCeiling`**,
  which is what PORTING-GUIDE.md §5 asks to be proved rather than
  assumed about a state clamp.

---

## 5. Parameters

Eleven. `kOutputTrim` keeps id 0 — the ten new ones were **appended**,
even though grouping the trim with the VCA would have read better.

| id | title | units | plain range | default | internal | smoothed |
|---|---|---|---|---|---|---|
| 0 | Output Trim | dB | −24 … +12 | 0 | same | yes |
| 1 | Cutoff | Hz | 20 … 20 k, **log** | 800 | same | yes |
| 2 | Resonance | % | 0 … 100 | 40 | **K, 0 … 2.4** | yes |
| 3 | VCF Attack | ms | 0.1 … 1000, **log** | 1 | **seconds** | no |
| 4 | VCF Release | ms | 1 … 4000, **log** | 120 | **seconds** | no |
| 5 | VCF Amount | % | −100 … +100 | +60 | **octaves, ±6** | no |
| 6 | VCF Velocity | % | 0 … 100 | 100 | **0 … 1** | no |
| 7 | VCA Attack | ms | 0.1 … 1000, **log** | 1 | **seconds** | no |
| 8 | VCA Release | ms | 1 … 4000, **log** | 150 | **seconds** | no |
| 9 | VCA Amount | % | 0 … 100 | 100 | **linear gain** | no |
| 10 | VCA Velocity | % | 0 … 100 | 100 | **0 … 1** | no |
| 11 | Noise Level | % | 0 … 100 | 100 | **linear gain** | yes |
| 12–22 | *drum 2's eleven* | | *as above* | *see below* | *as above* | |
| 23 | Mix D1/D2 | % | 0 … 100 | 50 | **0 … 1, 1 = all drum 1** | yes |
| 24–39 | Step 1–16 | | on / off | 1, 5, 9, 13 on | | |
| 40 | Sequencer Run | | on / off | off | | |
| 41 | Launch On | | Bar … 1/16 | Bar | **LaunchDivision** | |
| 1001 | Playhead | | read-only | | **step, or −1** | |

At 0 a drum's Noise Level makes it silent — see §5a. The panel reads
`silent` there.

**Drum 2's eleven are appended in the same order as drum 1's**, which is
load bearing rather than tidy: `kDrum2Offset` and `splitDrumParam()`
depend on it, and a block of `static_assert`s in the header fails the
**build** if a row ever stops matching its twin. (It cannot be checked
from `tests/DspTests.cpp` — that file is SDK-free and the table is not.)

**Drum 2's defaults are deliberately not drum 1's**: cutoff 2400 Hz,
resonance 62 %, VCF amount 35 %, releases 45 and 60 ms. It is voiced as
the snap over drum 1's body. Two drums with identical settings are one
drum 6 dB louder, so an out-of-the-box patch where the pair does nothing
would look broken.

**A version-2 project opens as a one-drum patch and then changes**, which
is recorded at `kStateVersion` in the processor: the old values land on
the parameters they were written for, drum 2 arrives at its defaults and
the mix at 50 %, so the old kick is now blended with a snap that was not
there before. The alternative — defaulting the mix to 100 % drum 1 —
would hide the second drum from everyone who never opens an old
project.

**Noise Level is id 11 and sits first on the panel**, which is the one
place panel order and id order deliberately disagree. It belongs at the
head of the VCF row because it is what feeds the filter, but it was
**appended** — inserting it at the front would have renumbered all ten
parameters after it and every project saved by the previous build would
have restored its values into the wrong ones, quietly. Its default is
100 %, which is what the plug-in did before the knob existed: a new
parameter whose default changes the sound silently rewrites every preset
made before it.

`kBypass` = 1000. IDs 1001+ reserved for read-only output parameters;
none used.

**The internal range is now doing real work**, which it was not when
this was a blank scaffold. Every bold entry above is a conversion that
happens in `toInternal()` and nowhere else, so there is exactly one place
a unit can be got wrong. `ParamType::Log` was added for this: a cutoff
swept linearly from 20 Hz to 20 kHz spends its first 2 % of travel
covering the bottom five octaves, and an attack from 0.1 ms to 1 s
linearly has every drum-length value in the first thousandth of the knob.

**The velocity law**, which is the part of the spec that read two ways:

    effective amount = amount × (1 − sensitivity + sensitivity × velocity)

At sensitivity 100 % that is `amount × velocity`, so MIDI 127 gives the
full amount and MIDI 0 gives nothing — the behaviour asked for. At
sensitivity 0 % velocity is ignored entirely, which is what makes the new
control a *sensitivity* rather than a second amount. It lives in
`velocityScaled()` in `FilterDrumDsp.h`, the panel's bottom line calls
it, and `testVelocityLaw()` asserts both end cases explicitly plus
monotonicity at five sensitivities.

VST3 delivers `noteOn.velocity` **already normalised to 0…1**, so there
is no division by 127 anywhere. A plug-in that does one anyway ends up
127 times too quiet.

---

## 5a. The voice, and why it is built this way

    DRUM 1  noise x level -> [ MS-20 LP ] -> [ VCA ] --+
                                  ^             ^       |
                              AR envelope   AR envelope +--> mix -> trim
                            (cutoff, bipolar)  (level)  |
    DRUM 2  noise x level -> [ MS-20 LP ] -> [ VCA ] --+

**Two voices, struck together by one note.** Still monophonic — a layer
is not polyphony, so there is no voice allocation anywhere. **The note
number is ignored**; every key makes the same pair, which is what keeps
the Cutoff knobs meaning absolute frequencies.

`DrumVoice` is the whole voice and `FilterDrumDsp` owns two of them,
doing nothing but triggering both, crossfading and applying the trim.
"The second drum is exactly the same as the first" is therefore a fact
about the code rather than a promise about it — there is no second copy
to drift. The processor routes all twenty-two per-drum parameters through
eleven case labels using `splitDrumParam()`, and the editor lays out both
rows from one `addDrumBlock()` call, for the same reason.

**The two voices get different noise seeds**, and that line is what makes
the pair a layer rather than one drum 6 dB louder: two generators started
from one seed produce the identical sequence. Measured correlation of two
*identically set* drums: **−0.16**.

**The crossfader is constant power**, `sin`/`cos`, so `g1² + g2² = 1` at
every position — asserted at 101 of them. The two drums are uncorrelated,
so their powers add; a linear fade would dip 3 dB in the middle. It is
**smoothed**, unlike the cutoff: a stepped cutoff turned out to be
inaudible because a TPT filter changes coefficients without a
discontinuity in its state, but a stepped *gain* is a step in the
waveform, which is a click.

### The noise is the only excitation

The Noise Level knob scales the noise from full down to **nothing**, and
nothing means nothing. A linear filter fed exact zero from a zero state
outputs exact zero forever, however far past its self-oscillation
threshold it is set — zero times any amount of resonance is still zero.

**So Noise Level 0 is silence from a cold start, at every resonance.** It
is an off switch, not a pure-tone setting, and the panel reads `silent`
there rather than `0 %`.

There is a wrinkle that makes that silence *intermittent* rather than
honest, and it is the more confusing half. `renderVoices` does not
advance the filter while the voice is idle, so its state **freezes**
between hits rather than decaying. Once an oscillation has been started
by noise, turning the knob to 0 leaves it running:

    one hit at 100 % noise, then the knob to 0 (peak per hit)
      K=0.96:  -7 dB   -31 dB   silent   silent
      K=2.40:  -3 dB    -9 dB    -9 dB    -9 dB   <- keeps going

So at high resonance the plug-in works perfectly well for the rest of the
session and is silent only when the project is reloaded with the knob
already down. "It worked until I reloaded" is a bug report nobody can act
on, which is why `testNoiseLevel()` asserts both halves — the sustain
after the knob moves, and the silence from cold with the identical patch.

### What the trigger ping was, and why it went

A per-note excitation used to close that gap: `kTriggerCharge = 2.0`,
dumped into the filter's output integrator at note-on, the way an
analogue drum voice pings its resonator with a trigger pulse. It was
removed on request, and `git log` has the whole implementation.

**What the measurements said before it went** — the same patch with and
without it:

| | with ping | without |
|---|---|---|
| noise 100 %, K=0.96 | −7.87 dBFS | −7.87 dBFS |
| noise 100 %, K=1.90 | −4.75 dBFS | −4.75 dBFS |
| noise 100 %, K=2.40 | −2.92 dBFS | −3.09 dBFS |
| first 5 ms RMS | −17.86 dBFS | −18.08 dBFS |

It was worth **0.2 dB** at any useful noise level. The comment in the
source claiming it "gives every hit a consistent attack transient" was
measurably wrong — the noise dominates the attack completely. It bought
the 0 % setting and essentially nothing else.

Two things about how it was built are worth keeping, in case one is ever
put back:

* **a one-sample impulse on the input does not work.** Tried first and
  measured: −46.63 dBFS at the default resonance, −40.70 at K=1.8, and
  only −9.64 (usable) at full resonance. A single sample carries almost
  no energy and the filter then attenuates it by roughly `g`, so the ping
  got *quieter as the cutoff came down* — backwards for a drum. Setting
  the integrator state instead gives a level independent of cutoff.
* **what bounded its amplitude was the fastest VCA attack**, not the
  loudest resonance: the peak is a fast-decaying ring times an envelope
  still opening, so a shorter attack made it louder. Charge 3.0 clipped
  at the 0.1 ms minimum.

**The cheaper alternative, if the silence at 0 turns out to matter**, is a
floor on the Noise Level knob rather than a second excitation. Measured
from cold at full resonance, 0.2 % noise already gives the full
−9.8 dBFS self-oscillation — the same level as 1 %, 5 % and 25 %, because
the diodes set it and not the drive. A knob that bottomed out at 0.2 – 0.5 %
would give the pure tone with nothing audible from the noise, and is also
what a real circuit's thermal noise does.

### The sequencer, and the clock under it

Sixteen steps, one bar of sixteenths in 4/4, triggering both drums. MIDI
notes still trigger independently and carry their own velocity; **sequenced
hits are always full velocity**, so the four Velocity sensitivity knobs do
nothing for them.

**`FilterDrumTransport.{h,cpp}` is lifted whole from `Project6-VSTi`**,
with two changes and nothing else: the namespace, and the grid runs at
**sixteenths rather than eighths**. `tests/TransportTests.cpp` came with
it — lifting arithmetic whose own banner says it has "four ways to be
subtly wrong" and leaving its tests behind would have been exactly the
wrong half to take. `LaunchDivision` gains a `Sixteenth`.

Changing the grid made ten of the lifted tests fail on numbers that
assumed eight steps, which is what they were for. They were updated, not
deleted: the 7/8 case now expects step 14 where it expected 7, and a
400-block run finds 35 lines where it found 18.

**One grid line is one step**, which is the whole reason the grid was
changed rather than a second clock written. `StepSequencer` has no clock:
the processor hands it the step index that came back from
`gridLinesInBlock` and it says whether to strike.

**Arm, then launch.** Switching Run on does not start the pattern — it
*arms* it, and the pattern begins at the next grid line the launch
division fires on. Switching Run off stops it **at once**, and that
asymmetry is deliberate: waiting for a bar line before starting is the
feature, but waiting for one before stopping reads as a stuck plug-in.

**The playhead is the grid step, not a counter.** A counter would drift
through a loop or a locate and carry the error forward for ever; reading
it from the bar means a locate into the middle of a bar puts the playhead
in the middle of the pattern, which is what every hardware sequencer does.

**The block is now rendered in segments.** A trigger that always landed at
offset 0 quantised every hit to the block size — 11 ms at 512 samples and
44.1 k, audible swing on a sixteenth. `process()` collects every trigger
from both sources, sorts by offset, and renders the pieces between them.
**MIDI notes go through the same path and gained that accuracy too**; they
had been firing at offset 0 since the scaffold.

**The playhead reaches the panel as a read-only output parameter**,
`kPlayheadOut` = 1001 — the first use of the space reserved for exactly
this since the scaffold, and the case it was reserved for. A `sendMessage`
from `process()` would be silently discarded by the host's connection
proxy. One parameter and not sixteen: sixteen continuously-changing
parameters would put thousands of points a second into a host's queue to
light lamps that redraw at thirty frames. `playheadToNormalized()` is
shared by both sides so they cannot disagree about the encoding, and -1
(not playing) is 0.0 so that "stopped" and "on step 1" are distinguishable.

The default pattern is **four on the floor** rather than empty, because an
empty pattern plus a Run switch that is off is two things a new user has
to find before anything happens. Run still defaults to off.

**`SpyStepSwitch` is the second control in the family that is not
lifted**, after `SpyFader`, and for a related reason. The DXi's SlideSpin
put its lamp hard in the control's **top-left corner** — fine on a
69-pixel property-page control, but on a 30-pixel step switch it sits ten
pixels left of the centred number, so a row of sixteen reads as a column
of lamps that does not line up with the column of switches. It centres
the lamp over its own switch and draws a box round the pair.

One rule lets the same class be both a 30-pixel step and the 100-pixel
Run switch: **a cell with a reading shows the reading; a cell without one
shows its bar.** A step's number and bar are all it has to say; Run has
three states worth naming — off, armed, running — and a bar under them
would be repeating the middle one badly.

The alignment that *was* always right is the functional one:
`lineFires()` sets `mPlayhead = gridStep` and then returns
`mSteps[gridStep]`, so the lamp that lights and the step that strikes are
the same index. `testPlayhead()` now asserts that over a full pass with a
pattern that has both on and off steps in it.

### Which MS-20 filter

There are two and they are not the same filter. This models the **later,
OTA (LM13600) revision**, whose small-signal response Stinchcombe gives
as

    Vo/Vin = −k1 / ( s²/ωc² + (2 − k1·k2)·s/ωc + 1 )

— a two-pole lowpass whose *damping* the resonance feedback reduces while
its cutoff stays put, self-oscillating once `k1·k2 ≥ 2`. The earlier
Korg35 version is a true Sallen-Key with a threshold of 2⅓ and, more
importantly, **its diodes in the forward path**, so they distort
everything rather than only the resonance. The OTA revision's three
back-to-back diodes sit in the feedback loop and colour the resonance
alone, which is the sound people mean by "the MS-20 filter".

Consequences worth knowing, both asserted in the suite:

* **DC gain is 1 and stays 1 as resonance rises.** The resonance moves
  the damping term and leaves the constant term alone, so the low end
  does not change level. A filter whose bass drops away as you turn the
  peak up has its feedback in the wrong place.
* **At zero resonance the gain at the nominal cutoff is −6 dB, not −3.**
  The OTA topology is two cascaded first-order sections; K = 0 leaves
  them critically damped, and 1/√2 squared is 0.5. −3 dB is what one
  reaches for and it would be wrong here.

### How it is realised

A **topology-preserving-transform state variable filter** (Zavalishin).
It gives exactly that denominator with `2R = 2 − K`, is stable at every
cutoff including past Nyquist, and — the point — exposes the **bandpass**
signal, which *is* the resonance feedback path. So the diode saturator
goes on that signal and nowhere else, which is topologically where the
hardware's are.

Two things about it that are easy to get wrong and are commented at the
code:

* **Only the feedback is saturated, not the whole damping term.** The
  damping is `2 − K·diode(bp)/bp`. The constant 2 is the two integrator
  stages' own loss and stays linear; as the oscillation grows,
  `diode(bp)/bp` falls, the net damping comes back positive and the
  amplitude settles. Saturating the whole term — the obvious-looking
  simplification — removes the loss along with the feedback, and then it
  grows without bound.
* **The diodes make it implicit, so it is solved with Newton**, three
  iterations, rather than with one-sample-delayed feedback. Delayed
  feedback detunes the resonance at high cutoffs and is exactly what
  makes cheap emulations sound wrong at the top of the knob. The
  derivative is `(1+g)² − K·g·sat'`, which for `sat' ≤ 1` is strictly
  positive for every `g` as long as `K ≤ 4` — so Newton converges
  monotonically and cannot divide by zero. `kMaxResonanceK` is 2.4.
  **Raise it past 4 and that guarantee is gone.** The suite asserts the
  residual stays below 1e-9 at the worst settings the knobs can reach.

`kMaxResonanceK = 2.4` puts the self-oscillation onset at 83 % of knob
travel, leaving useful room past it. The panel's lamp lights from
`selfOscillating()`, the same predicate, so it cannot disagree with what
you hear.

### The envelopes are triggered, not gated

Note-on starts the attack; the release begins the moment the attack
completes, and **note-off is ignored**. This is a deliberate departure
from what "AR" usually means, and the reason is that a drum has to sound
the same whether the key was tapped or held — a MIDI drum note is often
only a couple of milliseconds long, and a gated envelope would cut every
hit off at the note length and make both Release knobs appear broken.

`AREnvelope::release` is the hook if a gated AR is ever wanted; only the
call from `kNoteOffEvent` is missing.

The times **mean** something, and the suite checks them at all six
sample rates rather than trusting the arithmetic:

* **attack** = time to reach 1.0, via a one-pole aimed at 1.2 (it
  therefore arrives, where an exponential aimed exactly at 1.0 never
  does — which is why a naive one-pole attack measures far longer than
  its knob says);
* **release** = time to fall from 1.0 to zero (it was −60 dBFS before the
  shape controls; see below).

Both curves are **exponential, not linear**, and the shapes were measured
rather than assumed:

| t/T through the attack | level | a linear ramp |
|---|---|---|
| 0.25 | 0.4333 | 0.2500 |
| 0.50 | 0.7101 | 0.5000 |
| 0.75 | 0.8870 | 0.7500 |
| 1.00 | 1.0000 | 1.0000 |

The attack is **concave** — fast out of the gate, easing into the peak —
and about 0.21 above a ramp at its midpoint. The shape is identical at
1 ms, 10 ms and 100 ms: the time scales, the curve does not. The release
is a pure exponential, so it is **straight in decibels**: −25, −50 and
−75 dB at the quarter points of its real length.

#### The shape controls, and what they replaced

There are now **four shape knobs per drum** — VCF attack, VCF release,
VCA attack, VCA release — each sweeping

    Exponential  ->  Linear  ->  Logarithmic

through one family of curves, the charge and discharge of a capacitor
through a resistor:

    rise (x, b) = (1 - e^-bx) / (1 - e^-b)
    fall (x, b) = 1 - rise (x, b)

**Why this family and not `x^k`.** It contains the curves this plug-in
already had, exactly. The old release was a true exponential decay
calibrated to −60 dB, which is `fall(x, ln 1000)` up to an offset of
0.001; the old attack — a one-pole aimed at 1.2 and stopped at 1.0 — is
`rise(x, ln 6)` with nothing left over. A power law gets within a couple
of per cent on the attack and is wrong by a factor of five in the release
tail, so adopting it would have quietly restyled every existing patch.
This way **the Exponential end is the old behaviour**, all eight knobs
default to it, and `testShapedEnvelope` measures the new release against
a copy of the old one-pole kept in the test file for the purpose. The
worst deviation is 0.001 — the endpoint offset — at every release time
and every sample rate.

`b > 0` is fast-then-slow at **both** ends of the envelope: a quick rise
easing into the peak, and a quick drop with a long tail. That is the
punchy analogue shape, and it is what "Exponential" means on all four
knobs. `b < 0` is the mirror image. `b = 0` is a straight line, guarded
by a threshold because the normalising denominator `1 - e^-b` goes to
zero with `b`.

**It is a phase ramp now, not a recursion on the level.** That is what
makes every shape arrive: `rise(1,b) = 1` and `fall(1,b) = 0` for every
`b`, by construction. Three consequences:

* The attack no longer has to aim past its target. The 1.2 is gone.
* The release **lands on zero at exactly the knob time**, so
  `getTailSamples` is back to `longest + 0.5`. For about an hour it
  converted by 5/3, because the old one-pole ran on to −100 dB before
  it would call itself finished; that conversion was right for that
  envelope and is wrong for this one. `testShapedEnvelope` asserts the
  release does not overrun, which is the thing the tail line depends on.
* The Release knob's **definition moved** from "time to −60 dB" to "time
  to zero". The curve either side is the same curve, so nothing about
  the sound moved — but the shaped release now passes −60 dB at 0.9 of
  its knob rather than 1.0. That is arithmetic, and `testEnvelopeTimes`
  asserts both numbers so it stays on the record.

A retrigger needs one thing a recursion got for free: the old envelope
carried the level as its state, so it continued from wherever it was.
This one carries a phase, so `trigger()` asks `shapedRiseInverse` **which
phase of the attack curve holds the current level** and starts there.
Asserted at every shape.

Per sample the cost is one multiply, as before. `e = exp(-b·phase)` is
advanced by `e *= exp(-b·step)`, so `exp()` is called only when a stage
starts — and because a running product is exactly the kind of thing that
drifts quietly, the suite checks it against the closed form over a four
second release (worst error 3e-8).

#### The eight ids had to be appended, and that means two offsets

The four shapes belong next to `kVcfAttack` and the rest. Putting them
there would have pushed every drum-2 id up by four, so drum 2's Cutoff
lane would have started driving its VCF Attack in every project already
saved. They are appended past the sequencer instead: ids 42–45 for drum
1, 46–49 for drum 2.

The cost is that **drum 2's shapes are four away from drum 1's, not
eleven**. `drumParam(base, drum)` in `FilterDrumParams.h` is the only
thing that knows which offset applies; `splitDrumParam` gained a second
range test so the processor's switch did not have to learn about any of
it. The one remaining site that added `kDrum2Offset` by hand was correct
— all four of its parameters are in the original block — and was changed
anyway, because leaving one correct example of that pattern in the file
is how an incorrect copy of it gets made.

A version-4 project loads **sounding the same**: `setState` applies
defaults before reading the stream, and the defaults are the Exponential
end.

A retrigger **does not zero the level**; it continues from where the
envelope is, which is what stops a fast roll clicking on every note.
The envelope ends at exactly zero and goes idle rather than decaying
into denormals — see the silence-flag trap below.

### The two envelope displays

One per drum, in a strip between the seven columns and the crossfader,
each level with the drum block it belongs to. `SpyEnvelopeView` is
ForTran's `FtCurveView` — dark plate, caption left, figure right, fixed
0..1 scale — with **one change that is the whole reason it exists: it
holds two series and draws them against a shared horizontal axis.**

The shared axis is the point. Two independently scaled traces would draw
a 45 ms amp envelope and a 4 s filter envelope as the same picture, and
*which of these two outlasts the other* is the question the display is
for. A filter release running past the amp's shows as a green line still
descending after the red one has reached the floor.

* **Green is the VCF, red is the VCA.** VCA is drawn last, so where the
  two run together the red is the one on top — the amp envelope is what
  decides whether anything is heard at all.
* **The height is the Amount control**, on a fixed scale that is never
  normalised: a normalising display would draw Amount 10 % and Amount
  100 % identically. The VCA's amount is already a linear gain; the
  VCF's is signed octaves, so its **magnitude** is the height.
* **The sign goes in the legend**, not the curve. A negative VCF amount
  closes the filter on the attack instead of opening it, and the
  envelope is the *same shape* either way — the curve cannot show the
  difference, so the legend reads `VCF inv`. Drawing it inverted would
  need a centred zero line, which would halve the height available to
  the VCA curve for the sake of a minority of patches.
* **Drawn at full velocity**, because a display cannot know how hard the
  next hit will be played. The velocity line under the panel covers the
  rest, and is computed from `velocityScaled()`.
* **Built from parameters, not from the DSP** — the editor cannot see a
  `DrumVoice`; in a host like Logic it is not even in the same process.
  What keeps it honest is that `traceDrumEnvelopes()` **drives real
  `AREnvelope` objects**, so the only thing that could drift is the
  settings handed to them, and those come through `toInternal()`, the
  same conversion the processor applies to the same normalised values.

The work is bounded: the trace runs `kEnvPoints * 4` steps whatever the
release times are, because the trace rate is derived from the span rather
than fixed at 48 kHz. Only the six parameters that shape a curve trigger
a redraw, because this runs on every pixel of every drag.

`tools/dump-envelope.cpp` exists so that `tools/render-panel.py` can draw
the docs picture from **this same code** rather than from a Python
re-implementation of the maths. A re-implementation would go on looking
right for exactly as long as nobody changed the envelope.

The panel grew from 798 to **986** wide to hold the strip. The step
switches grew 30 → 41 with it: `kStepWidth` is now *derived* from the
panel width rather than typed, because a typed 30 left a two-hundred
pixel hole in the middle of the sequencer row, and a `static_assert`
fails the build if the arithmetic ever stops coming out whole.

### Deliberate non-determinism

The filter state is **not** reset on note-on and the noise is not
reseeded, so the phase of a self-oscillating tone at the moment of a hit
is arbitrary and two identical MIDI notes are not bit-identical.
Resetting would make every kick start on the same part of the cycle,
which sounds noticeably more like a sample and less like an analogue
drum.

**A correction to what this file said before.** It claimed the noise and
the filter "run continuously", as they do in the hardware. They do not:
`renderVoices` returns early while the VCA envelope is idle, so between
hits nothing is advanced at all and the filter's state simply freezes
where the last note left it. The audible result is the same — a hit
inherits an arbitrary phase from the previous one — but the mechanism is
different, and the difference is what made the Noise Level knob's bottom
end silent rather than quiet: on the *first* note after a reset there is
no inherited state to ring either.

**Stated plainly: this plug-in does not render deterministically from a
given MIDI sequence.** If a bit-exact bounce ever matters, the line to
change is in `FilterDrumDsp::trigger` and the noise seed is the other
half of it.

### Known limitation: no oversampling

The diode saturator generates harmonics that alias. It is tolerable here
because they are generated *inside* a lowpass loop and then filtered by
it — the standard argument for undersampled ZDF filters — but it is the
first thing to change if the self-oscillation sounds gritty at high
cutoffs. The
place to do it is around the per-sample loop in
`FilterDrumDsp::renderVoices`.

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
* **The release tail is declared, not computed.** `getTailSamples()`
  returns the longer of the two releases plus half a second, which is
  what PORTING-GUIDE.md §5 asks for. Without it a host may stop calling
  `process()` the moment the notes stop, and every hit gets truncated at
  its note length — the same symptom a gated envelope would give, from a
  different cause.
* **The silence flag asks the DSP rather than guessing.** A bus flagged
  silent that is not is far worse than one that is not flagged: the host
  may skip it and the hit never arrives. `mDsp.active()` is false only
  once the VCA envelope has reached *exactly* zero and gone idle, which
  is why `AREnvelope::next` snaps to zero instead of decaying into
  denormals — an envelope that never quite arrives keeps the voice alive
  and every downstream plug-in awake for the life of the session.
* **The VCA alone decides whether the voice is active.** The VCF
  envelope can still be running while the VCA has closed, and nothing
  that happens to the cutoff of a muted signal is audible.
* **The velocity-scaled amounts are latched at note-on**, not read per
  sample. A drum's velocity is a property of the hit; recomputing them
  would mean a knob moved during a decay changed a note already
  sounding, and automation on an Amount knob would make every hit drift
  while it decayed.
* **`guard()` tests `isfinite` *and* clamps, and the two are different
  checks.** A NaN compares false against everything, so a plain clamp
  passes it straight through and it then poisons every subsequent sample
  for the life of the instance, silently. The clamp catches the merely
  enormous before it becomes an inf.
* **The cutoff is recomputed every sample**, through the shared
  `cutoffWithEnv()`. A per-block cutoff steps the filter once per
  buffer, and on a fast sweep — which is every kick — that is audible as
  a zipper.
* **The processor keeps a normalised copy of what the host sent**, purely
  for `getState`. The DSP stores internal units, and turning seconds
  back into a normalised position would mean maintaining an inverse of
  every mapping in the table — an inverse that has to be kept in step
  with the forward one. Eleven doubles is cheaper and exact.
* **Bus layout says the same thing in all four places** —
  `PlugType::kInstrumentDrum` in the entry, the buses added in
  `initialize`, what `setBusArrangements` accepts, and the single
  `0 in / 2 out` entry in the plist. The template's extra `1/1` entry was
  **cut**; a layout listed there that `setBusArrangements` refuses is a
  mismatch `auval` finds and nothing else does.

## 7. Deliberate omissions, and the trap that bites when each is undone

* ~~No `getProcessContextRequirements` override.~~ **Added with the
  sequencer** — it is the thing that finally reads the tempo. The
  override asks for transport state, musical position, tempo and time
  signature. Without it `data.processContext` arrives with nothing
  valid, the tempo reads 120 in every host, the bar lines land nowhere,
  the sequencer never launches, and the validator prints `- None` rather
  than complaining. It sat in the banner as pasteable code from the
  scaffold until now.
* **No oversampling** — see §5a.
* **Note-off is ignored**, and the `case` is written out rather than
  falling into the default, because "we looked at note-off and chose to
  ignore it" and "we never handled note-off" are different things to
  read six months from now.
* **No processor → controller messages beyond the sample rate**, and no
  controller → processor message at all. The reserved block in
  `FilterDrumIDs.h` carries the rule for adding one: UI thread only, and
  a host that never connects the two components delivers none of them,
  which is why anything expressible as a number goes through a parameter
  instead.
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

    FilterDrum DSP tests           257 checks, 0 failures
    FilterDrum transport tests     ALL TESTS PASSED (0 failures)
    FilterDrum sequencer tests      56 checks, 0 failures

The DSP suite's own tail, which carries the measured levels:

      default patch, velocity 127:      -7.87 dBFS
      full resonance, self-oscillating: -3.09 dBFS
      worst case, +12 dB trim:          +8.91 dBFS
      fastest VCA attack:               -8.23 dBFS
      noise level 0, from cold:         silence
      correlation of two identically-set drums: -0.1575
      both drums, mix 50 %, velocity 127:  -6.80 dBFS
      both self-oscillating, unity trim:   -2.20 dBFS

Exit status 0. Every rate-dependent assertion is made at 44.1, 48, 88.2,
96, 176.4 and 192 kHz.

**The suite has found three real defects so far**, which is the reason to
write it before believing the code:

1. the default patch clipped at **+0.09 dBFS** — fixed by `kVoiceGain`,
   §4;
2. the 12 dB/octave assertion failed at −13.73 dB/octave. *That one was
   the test being wrong and the filter being right*: it measured at 4 k
   and 8 k with fs = 48 k, and a bilinear-transformed filter necessarily
   steepens towards Nyquist because it must reach zero there. Moved to
   1 k / 2 k with a 100 Hz cutoff, where the asymptote has taken hold and
   the warping has not. The reasoning is recorded at the assertion so
   nobody "fixes" the filter to satisfy it.
3. when Noise Level was added, the assertion that the trigger ping scales
   *linearly* with velocity read 0.577 instead of 0.5. **That one was the
   test being wrong too**: velocity scales the cutoff sweep as well as
   the level, so a softer hit was ringing a different filter — one that
   had swept 1.8 octaves instead of 3.6 — and the two effects were being
   measured together. Pinning the sweep to zero isolates the VCA and the
   ratio is exact. (That assertion went with the ping; the lesson did
   not.)

A fourth defect came out of `tests/SequenceDiagnostics.cpp`, which
measures across a *sequence* of hits rather than one: **the VCF envelope
freezes when the VCA closes first**. `renderVoices` returns early while
the voice is idle and both envelopes are advanced inside that loop, so a
VCF release longer than the VCA's never completes. Hit 1 starts its sweep
from 0; every hit after it starts from ~0.87.

**The size of that was overstated once and is corrected here.** Measured
through the trigger ping it read 1.27 to 6.60 dB, and the notes said "the
first note of a session is a different sound from every note after it".
That was the ping landing on the one sample where the difference exists.
With the ping gone and the noise driving the filter, it is **0.4 dB, flat
across every window from 2 ms to 250 ms** — because the attack reaches
1.0 either way, so the two cases only differ for the length of the
attack. Still worth fixing, as an inconsistency with no upside; not a
thing anybody would hear. Every assertion in the main suite looks at a
single hit, which is the gap that file exists to cover.

**The harness is proved capable of failing** by mutation, each time
something is added. Three mutations so far, each reporting failures
across several test groups rather than only the one that was broken:

| mutation | failures |
|---|---|
| `velocityScaled()` ignores its arguments | 10, across three groups |
| Noise Level knob ignored | 4 |
| trigger ping removed | 7 |

The third is why the removal was cheap to do: the suite already said
exactly what would break. Those seven assertions have been **inverted**
rather than deleted — they now assert the silence at 0 instead of the
sound, so if a per-note excitation is ever put back they are the ones
that fail first and say so.

### Every source file compiles, and nothing is left undefined

    SDK=~/DXi-DEv/Project6-VSTi/external/vst3sdk
    for f in source/*.cpp; do
      g++ -c -std=c++17 -DLINUX=1 -DRELEASE=1 \
          -I$SDK -I$SDK/vstgui4 -Isource \
          -o /tmp/objs/$(basename $f .cpp).o $f || echo "FAILED $f"
    done
    nm -C /tmp/objs/*.o | grep " U " | grep "FilterDrum::"

All nine translation units compiled with no errors. The undefined-symbol
list was cross-checked against the defined one: **48 undefined
`FilterDrum` symbols, all 48 defined in another object, 0 unresolved**
(713 defined in total, across nine translation units).

Two details that matter about this check:

* `-DRELEASE=1` is **required** — `fdebug.h` refuses to compile without
  it.
* Compile to **object files** and `nm -C` them, not `-fsyntax-only`: a
  header that declares a function nobody defined passes a syntax check
  and fails at link.

The only warnings are from inside the SDK's own headers (`-Wmultichar`
on VSTGUI's four-character attribute ids). None are from this project's
code.

### On the Mac — `auval` passes

**2026-09-15: `auval -v aumu FDrm AECo` passes**, against the Release
build in `build/VST3/Release/`.

That closes the part of this project the Linux VM could not reach, and it
is worth being precise about what it does and does not settle.

**What it proves**, all of it structural and all of it stuff that was
argued for on paper up to now:

* the **four-place identity agreement** actually agrees — the `aumu` type
  code, `PlugType::kInstrumentDrum`, the buses added in `initialize` and
  what `setBusArrangements` accepts. `auval` checks the last two against
  each other and is stricter about it than most hosts;
* `SupportedNumChannels` matches the processor. Cutting the template's
  second `1/1` entry was right; leaving it in would have failed here;
* **no null title or units reached `RangeParameter`.** That trap's
  symptom is the validator itself segfaulting, so a clean run is the
  evidence;
* parameter round-tripping, which `auval` exercises hard — every one of
  the twelve, including the `Log` ones added for the cutoff and the four
  times;
* the state stream survives save and restore;
* the AU wrapper loads the VST3 and finds `GetPluginFactory`.

**What it does not prove: anything at all about the sound.** `auval` does
not listen. Every number in §4 is still a measurement made by the test
suite and not a judgement made by an ear.

### The panel can be looked at without a build

`tools/render-panel.py` draws the editor from the constants in
`FilterDrumEditor.cpp` and the titles in `FilterDrumParams.cpp`, into
`docs/panel.png`. It fails rather than drawing a stale picture if a
constant is renamed.

It earned its place immediately: the crossfader had been given a full
94-pixel column, and at that width it read as a slider that had grown
rather than as a different kind of control. The fix was to take the fader
off the column grid entirely — `kMixWidth` is 52, and the groove is a
fixed width measured from the control's centre rather than an inset from
its sides, so a fader stays fader-shaped whatever box it is given.

### The new assertions were mutation-tested, and one mutation got through

The shape controls took the DSP suite to **535** checks, 0 failures. Six
mutations were run against the new assertions:

| mutation | failures |
|---|---|
| `shapeToCurve` loses its sign flip, so every knob works backwards | 45 |
| the release is not normalised, so it stops at −60 dB instead of 0 | 21 |
| the attack aims at 1.0 rather than arriving | **0, then 10** |
| the retrigger jumps to zero instead of resuming | 3 |
| the linear threshold is never taken, so Linear divides by zero | 1 |
| a stage is not re-primed when a knob moves mid-hit | 1 |

**The third one survived the first time, and that is the useful result.**
Dropping the normalising divide from the attack leaves the level at
`1 - e^-b` at the top instead of 1 — and because `next()` assigns an
exact 1.0 when the phase runs out, every endpoint assertion still passed.
At the default curve the defect is 0.1 % and inaudible. At a mid-range
shape the denominator is 0.63, so the attack would have climbed to 0.63
and then **jumped** to 1.0: a click on every hit, invisible to a suite
that only looked at the ends of the curve.

The fix was to assert the **whole trajectory** — the running level
against `shapedRise`/`shapedFall` at seven shapes, plus a
no-discontinuity check on the largest single-sample step — after which
the mutation fails ten ways. The sequencer suite (57 checks) and the
transport suite were re-run unchanged and still pass.

### Still to do

* **Run the sequencer against a host transport.** Everything about the
  bar lines is asserted arithmetically and nothing has heard it. The
  things to check are the ones a test cannot: that the launch lands on
  the downbeat rather than a sixteenth either side of it, that a loop
  does not double-fire the bar line, and that stopping the transport
  mid-pattern and starting again launches cleanly.
* **Listen to the shape knobs.** The curves are asserted and the
  Exponential end is proved identical to the old release, but nothing
  has heard Linear or Logarithmic. The two worth trying first are a
  Logarithmic VCA release, which should hang and then drop rather than
  decaying away, and a Logarithmic VCF attack on a long attack time,
  where the old envelope had no equivalent at all.
* **Look at the two envelope displays in a real host.** The layout is
  rendered and the curve maths is asserted, but `SpyEnvelopeView::draw`
  has never been run by VSTGUI — `panel.png` is the panel renderer's
  reading of the same constants, not a screenshot. The specific things a
  drawing bug would show up as: the traces clipped by the caption band,
  the legend overlapping the floor line, or a curve leaving the plate on
  a very short envelope.
* **Listen to the pair.** The second drum is in and the build is waiting.
  Drum 2 is voiced as a snap over drum 1's body and the fader defaults to
  an even blend, so the first thing to check is whether that default
  reads as one layered hit or as two drums that happen to fire together.
* **Listen to it without the trigger ping.** Removed on request. The open question is what the voice
  loses: the measurements say 0.2 dB at any useful noise level, and the
  only real cost is that Noise Level 0 is now silence. If that setting is
  wanted back, §5a has the cheaper fix — a floor on the knob rather than
  a second excitation.
* **Test in a real host**, which exercises things `auval` does not:
  automation, project save and reload, and whether the panel survives
  being opened and closed repeatedly (the `editorDestroyed` trap).
* **The `.component` currently holds a symlink into the build tree**, and
  this was confirmed rather than assumed:

        FilterDrum.component/Contents/Resources/plugin.vst3
          -> /Users/andy/DXi-DEv/FilterDrum-VSTi/build/VST3/Release/FilterDrum.vst3

  That is normal for a development build and is *why* `auval` passes on
  this machine. It also means **the Audio Unit is dead on any other
  machine** — it would install, register, and then fail to load with
  nothing useful said about why. Replacing the symlink with the real
  `.vst3` bundle is the last step before this goes anywhere, and the
  `vst3-macos-installer` skill covers it along with signing and
  notarisation.

Adding a source file later regenerates the Xcode project mid-build and
compiles the old file list; the symptom is *"Bundle does not export the
required 'GetPluginFactory' function"*. Re-run `./setup-xcode.sh
--no-open` and build again.

Adding a source file later regenerates the Xcode project mid-build and
compiles the old file list; the symptom is *"Bundle does not export the
required 'GetPluginFactory' function"*. Re-run `./setup-xcode.sh
--no-open` and build again.

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


---

## 10. Sources

The MS-20 filter topology, its transfer function and the
self-oscillation threshold are from Timothy E. Stinchcombe, *A Study of
the Korg MS10 & MS20 Filters*,
<https://www.timstinchcombe.co.uk/synth/MS20_study.pdf> — in particular
the distinction between the Korg35 and OTA revisions and the placement
of the diodes in each.
