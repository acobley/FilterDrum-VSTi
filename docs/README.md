# docs

**signal-path.html / signal-path.png** — the voice, transcribed from
`FilterDrumDsp::renderVoices` and `Ms20Filter::process` rather than drawn
from memory. Blue is audio, green is modulation, red marks the one known
defect. The HTML is the source; the PNG is rendered from it with headless
Chromium, so the picture stays regenerable rather than replaceable.

It is worth having because three things about this voice are easy to
picture wrongly, and all three are easier to see than to read:

* **the two excitations never meet at a summing node.** The noise goes
  into the filter's input; the trigger ping is written straight into
  `s2`, the *second* integrator's state. So the filter sees a step rather
  than an impulse, the ping's energy sits at the low end instead of at
  the resonant peak, and the resonance knob reaches the two sources very
  differently;
* **only half the damping term passes the diode.** The damping is
  `2·bp − K·diode(bp)`; the constant 2 is the integrators' own loss and
  stays linear. That asymmetry is what bounds the self-oscillation —
  saturating the whole term would remove the loss along with the feedback
  and the oscillation would grow without limit;
* **three different clocks are running.** Some values update every
  sample, some are latched once at note-on, some only once per block. A
  value read on the wrong one is the usual reason two hits differ, so the
  page ends with a table of which is which.

The red box marks the defect recorded in `../PORTING-NOTES.md`:
`renderVoices` returns early while the VCA envelope is idle and both
envelopes are advanced inside that loop, so a VCF release longer than the
VCA's never completes. Redraw the box away when that is fixed.

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
