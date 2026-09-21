# FilterDrum 1.0.1.0

## What changed since 1.0.0.1

- **A factory preset, "Pew"**, for both formats. The VST3 and the Audio Unit
  copies are made from the same bytes, so they sound the same.
- **The Audio Unit is now listed as "AE Cobley: FilterDrum"** (it was
  "A. E. Cobley: FilterDrum"). REAPER files its AU presets under the plug-in's
  name cut off at the first full stop, so every A. E. Cobley Audio Unit shared
  one preset list — FilterDrum's menu showed another plug-in's presets, and
  loading them changed nothing. The AU's type, subtype and manufacturer codes
  are unchanged, so **projects saved with 1.0.0.1 still open** with the
  plug-in in place.
- The AU version is now 1.0.1, so hosts that cache Audio Units (Logic,
  GarageBand) rescan it and pick up the new name.

Nothing in the sound engine changed.


Two monophonic drum voices, struck together. Each is noise through a Korg
MS-20 lowpass into a VCA, with an AR envelope on each; a constant-power
crossfader blends the two. A sixteen-step sequencer rides the host's bar
lines, and MIDI still triggers it.

The filter is the later, OTA (LM13600) MS-20 revision — a two-pole lowpass
whose damping the resonance feedback reduces, self-oscillating once K ≥ 2 —
realised as a topology-preserving-transform state-variable filter so that the
diode saturator sits on the bandpass signal, which is where the hardware's
diodes are. The resonance is coloured; nothing else is.

## Installing

Download `FilterDrum-1.0.1.0.pkg` and open it. **Quit your DAW first** — a
host that is already running holds the old plug-in open and will not see the
new one until it restarts.

The installer offers the two formats separately:

- **VST3** → `/Library/Audio/Plug-Ins/VST3/FilterDrum.vst3`
- **Audio Unit** → `/Library/Audio/Plug-Ins/Components/FilterDrum.component`
- **Factory presets** → `/Library/Audio/Presets/A. E. Cobley/FilterDrum` (VST3)
  and `/Library/Audio/Presets/AE Cobley/FilterDrum` (Audio Unit)

It appears as an **instrument** by A. E. Cobley (VST3) or AE Cobley (AU). Requires **macOS 10.13** or
later.

## Four things that are deliberate

These are the ones most likely to be reported as faults.

- **Noise Level at 0 is silence, not a quiet tone.** The noise is each
  voice's only excitation — there is no oscillator — and a filter fed exact
  zero from a zero state stays at exact zero however high the resonance.
- **Note-off is ignored.** The envelopes are triggered, not gated, so a hit
  sounds the same whether the key was tapped or held. The Release controls
  decide how long it rings. This is deliberate: a MIDI drum note is often only
  a couple of milliseconds long, and a gated envelope would cut every hit off
  at the note length.
- **Run *arms* the sequencer.** It starts on the next line the Launch On
  division fires — bar, ½, ¼, ⅛ or 1/16 — so it can wait most of a bar before
  the first step. With the transport stopped it waits indefinitely, which is
  correct and looks exactly like nothing happening.
- **Sequenced steps play at full velocity.** There is no per-step level, so
  the four Velocity controls affect incoming MIDI only.

## Known gaps

- **A VCF release longer than its VCA release does not finish.** The voice
  stops advancing once the amplitude envelope is idle, so the filter envelope
  freezes mid-release and the next hit starts from that frozen level. Measured
  at 0.4 dB, flat across every window from 2 ms to 250 ms — an inconsistency
  with no upside rather than something anybody would hear.
- **No oversampling.** The diode saturator generates harmonics that alias.
  Tolerable here because the source is noise and the filter is a lowpass, but
  it is a real limitation at high resonance and high cutoff.
- **Monophonic**, one voice per drum. A retrigger continues from the level the
  envelope is at rather than restarting from zero, which is what stops a fast
  roll clicking.

## Presets in REAPER

The VST3 shows "Pew" in REAPER's preset menu. **With the Audio Unit, REAPER's
menu is its own list** and does not read the installed `.aupreset` files, so
"Pew" may not be there; hosts that do read them (Logic, GarageBand) list it as
a factory preset. If you saved AU presets in REAPER under 1.0.0.1 they were
filed in a list shared with other plug-ins, and REAPER will not show them
under the new name.

## Uninstalling

A `.pkg` never will, so:

```sh
sudo rm -rf /Library/Audio/Plug-Ins/VST3/FilterDrum.vst3
sudo rm -rf /Library/Audio/Plug-Ins/Components/FilterDrum.component
sudo rm -rf "/Library/Audio/Presets/A. E. Cobley/FilterDrum"
sudo rm -rf "/Library/Audio/Presets/AE Cobley/FilterDrum"
```

## Verifying the download

```
SHA-256: <paste from: shasum -a 256 installer/FilterDrum-1.0.1.0.pkg>
```

Take that from the **finished, stapled** package — stapling changes the bytes,
so a checksum taken before it is wrong.

The package is signed with a Developer ID and notarised by Apple. Notarisation
is a malware scan, not an endorsement.

## Source

<https://github.com/acobley/FilterDrum-VSTi> — MIT licence.
`PORTING-NOTES.md` there has the engineering detail: what was measured, what
was not, and why the filter is built the way it is.
