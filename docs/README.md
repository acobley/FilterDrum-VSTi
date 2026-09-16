# docs

**signal-path.html / signal-path.png** — the two voices, transcribed from
`DrumVoice::render` and `Ms20Filter::process` rather than drawn from
memory. Blue is audio, green is modulation, red marks the one known
defect. The HTML is the source; the PNG is rendered from it with headless
Chromium, so the picture stays regenerable rather than replaceable.

It is worth having because three things about this voice are easy to
picture wrongly, and all three are easier to see than to read:

* **there is one voice implementation and two instances of it.** The
  second drum is the same code with its own settings, not a copy — the
  processor routes all twenty-two per-drum parameters through eleven case
  labels and the panel lays out both rows from one function, so there is
  nothing to drift;
* **the two voices have different noise seeds**, and that is what makes
  them a layer. Two generators started from one seed produce the
  identical sequence, so the pair would be one drum 6 dB louder — it
  would measure fine everywhere and sound like one drum. Measured
  correlation of two identically-set drums: −0.16;
* **the noise is each drum's only excitation**, so the bottom of a Noise
  Level knob is an off switch rather than a pure-tone setting: a linear
  filter fed exact zero from a zero state stays at exact zero however
  high the resonance. A per-note trigger ping used to cover that and was
  removed — `git log` has it, and `../PORTING-NOTES.md` §5a has what it
  was worth (0.2 dB at any useful noise level);
* **only half the damping term passes the diode.** The damping is
  `2·bp − K·diode(bp)`; the constant 2 is the integrators' own loss and
  stays linear. That asymmetry is what bounds the self-oscillation —
  saturating the whole term would remove the loss along with the feedback
  and the oscillation would grow without limit;
* **three different clocks are running.** Some values update every
  sample, some are latched once at note-on, some only once per block. A
  value read on the wrong one is the usual reason two hits differ, so the
  page ends with a table of which is which.

The flag at the bottom marks the defect recorded in
`../PORTING-NOTES.md`: `DrumVoice::render` returns early while that
voice's VCA envelope is idle and both its envelopes are advanced inside
that loop, so a VCF release longer than the VCA's never completes. Its
size was overstated once and the flag now carries the correction — 0.4 dB,
not the 1.3–6.6 dB that measuring through the trigger ping suggested.

## Regenerating the PNG

No fonts are fetched — the HTML deliberately has no external
dependencies, so it opens correctly from a checkout with no network. It
asks for Helvetica Neue and SF Mono, which resolve on macOS and fall back
to TeX Gyre Heros and DejaVu Sans Mono under headless Linux.

    node -e '
      const { chromium } = require("playwright");
      (async () => {
        const b = await chromium.launch();
        const p = await b.newPage({ viewport: { width: 1060, height: 900 },
                                    deviceScaleFactor: 2, colorScheme: "light" });
        await p.goto("file://" + process.cwd() + "/signal-path.html");
        await p.evaluate(() => document.fonts.ready);
        await p.screenshot({ path: "signal-path.png", fullPage: true });
        await b.close();
      })();'

The page is theme-aware; `colorScheme: "light"` is what keeps the PNG on
a light ground for printing. Drop it for the dark version, which uses the
plug-in panel's own palette.
