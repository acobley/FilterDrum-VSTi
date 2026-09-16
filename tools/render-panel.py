#!/usr/bin/env python3
"""Draw the plug-in's panel as a host shows it, into docs/panel.png.

WHY IT READS THE SOURCE. Every position and size below comes out of
source/FilterDrumEditor.cpp and source/FilterDrumEditor.h, and the control
titles out of source/FilterDrumParams.cpp, so this cannot quietly drift from
the thing it is a picture of. A constant that gets renamed makes this FAIL
rather than silently draw last year's panel - the same bargain the other
projects' tools/render-panel.py makes.

What it cannot show is the real VSTGUI rendering: this is a faithful
reconstruction from the same numbers, not a screenshot. It exists so that a
control in the wrong place HERE is a control in the wrong place in the
plug-in, which is the only way to judge a layout without a build.

    python3 tools/render-panel.py [out.png]
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

SCALE = 2          # drawn at 2x so the text is legible


# ---------------------------------------------------------------------------
# The numbers, read out of the source rather than copied
# ---------------------------------------------------------------------------
def read_constants(path, wanted):
    """Pull `constexpr int NAME = VALUE;` / `static const int NAME = VALUE;`
    out of a file, resolving references to constants already found."""
    text = open(os.path.join(ROOT, path), encoding='utf-8').read()
    found = {}
    pattern = re.compile(
        r'(?:constexpr|static\s+const)\s+int\s+(\w+)\s*=\s*([^;]+);')
    for name, expr in pattern.findall(text):
        if name not in wanted:
            continue
        try:
            found[name] = int(eval(expr, {'__builtins__': {}}, dict(found)))
        except Exception:
            pass

    missing = [w for w in wanted if w not in found]
    if missing:
        raise SystemExit(
            'render-panel.py: these constants are gone from %s: %s\n'
            'The panel changed shape; fix this script rather than the picture.'
            % (path, ', '.join(missing)))
    return found


L = read_constants('source/FilterDrumEditor.cpp', [
    'kMargin', 'kColumnWidth', 'kColumnGap', 'kColumnPitch', 'kSliderHeight',
    'kTitleY', 'kLabelHeight',
    'kDrum1LabelY', 'kDrum1VcfY', 'kDrum1VcaY',
    'kDrum2LabelY', 'kDrum2VcfY', 'kDrum2VcaY',
    'kVelocityY', 'kRateY',
    'kMixWidth', 'kMixX', 'kMixTop', 'kMixBottom', 'kTrimColumn',
])
L.update(read_constants('source/FilterDrumEditor.h',
                        ['kEditorWidth', 'kEditorHeight']))

# The control titles, from the parameter table.
TABLE = open(os.path.join(ROOT, 'source/FilterDrumParams.cpp'),
             encoding='utf-8').read()
TITLES = dict(re.findall(r'\{(k\w+),\s*"([^"]+)"', TABLE))
if len(TITLES) < 24:
    raise SystemExit('render-panel.py: found %d parameters, expected 24'
                     % len(TITLES))


def short(tag):
    """The label the editor draws: drum 1's title, section prefix stripped."""
    base = tag[:-1] if tag.endswith('2') and tag != 'kResonance' else tag
    title = TITLES.get(base, TITLES.get(tag, tag))
    title = re.sub(r'^VC[FA] 2 ', '', title)
    title = re.sub(r'^VC[FA] ', '', title)
    title = re.sub(r' 2$', '', title)
    return title


# ---------------------------------------------------------------------------
# The palette, from FilterDrumControls.h and the editor's own background
# ---------------------------------------------------------------------------
BACK   = (44, 48, 44)
LABEL  = (50, 255, 50)
VALUE  = (232, 232, 232)
TRACE  = (127, 200, 255)
BAR_HI = (255, 255, 255)
BAR_LO = (200, 200, 200)
BAR_FL = (100, 100, 100)
GRID   = (200, 200, 200)
LAMP   = (255, 0, 0)


def font(size):
    for path in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                 '/System/Library/Fonts/Supplemental/Arial.ttf',
                 '/Library/Fonts/Arial.ttf'):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


F_MAIN = font(8 * SCALE)
F_SMALL = font(7 * SCALE)

img = Image.new('RGB', (L['kEditorWidth'] * SCALE, L['kEditorHeight'] * SCALE), BACK)
d = ImageDraw.Draw(img)


def s(v):
    return v * SCALE


def text(x, y, msg, fill=VALUE, fnt=F_MAIN, anchor='la'):
    d.text((s(x), s(y)), msg, fill=fill, font=fnt, anchor=anchor)


def bevel(x0, y0, x1, y1, light, dark):
    """The 3d rect the controls draw: light top-left, dark bottom-right."""
    d.line([(s(x0), s(y0)), (s(x1), s(y0))], fill=light, width=SCALE)
    d.line([(s(x0), s(y0)), (s(x0), s(y1))], fill=light, width=SCALE)
    d.line([(s(x0), s(y1)), (s(x1), s(y1))], fill=dark, width=SCALE)
    d.line([(s(x1), s(y0)), (s(x1), s(y1))], fill=dark, width=SCALE)


def slider(col, y, tag, value=0.55, lamp=None):
    """One SpySlider: value text at the top, bar, label at the bottom."""
    x = L['kMargin'] + col * L['kColumnPitch']
    w, h = L['kColumnWidth'], L['kSliderHeight']

    text(x + w / 2, y + 1, '--', fill=VALUE, fnt=F_MAIN, anchor='ma')

    # the bar: rect.bottom-15 .. rect.bottom-3, per kBarHeight/kBarBottomInset
    by1, by0 = y + h - 3, y + h - 15
    bevel(x, by0, x + w * value, by1, BAR_LO, BAR_HI)
    d.rectangle([s(x) + SCALE, s(by0) + SCALE,
                 s(x + w * value) - SCALE, s(by1) - SCALE], fill=BAR_FL)

    text(x + w / 2, y + h - L['kLabelHeight'] - 1, short(tag),
         fill=LABEL, fnt=F_SMALL, anchor='ma')

    if lamp is not None:
        d.rectangle([s(x), s(y), s(x + 10), s(y + 10)],
                    fill=LAMP if lamp else (0, 0, 0), outline=BAR_FL)


def drum_block(drum, label_y, vcf_y, vca_y):
    suffix = '2' if drum == 2 else ''
    text(L['kMargin'], label_y,
         'DRUM 1   noise -> MS-20 lowpass -> VCA   (lamp = self-oscillating)'
         if drum == 1 else 'DRUM 2   same voice, its own settings',
         fill=LABEL, fnt=F_MAIN)

    for col, tag in enumerate(['kNoiseLevel', 'kCutoff', 'kResonance',
                               'kVcfAttack', 'kVcfRelease', 'kVcfAmount',
                               'kVcfVelocity']):
        slider(col, vcf_y, tag + suffix,
               lamp=False if tag == 'kResonance' else None)

    for col, tag in enumerate(['kVcaAttack', 'kVcaRelease',
                               'kVcaAmount', 'kVcaVelocity']):
        slider(col, vca_y, tag + suffix)


# ---------------------------------------------------------------------------
text(L['kMargin'], L['kTitleY'],
     'FilterDrum   -   two monophonic MS-20 drum voices, struck together',
     fill=VALUE, fnt=F_MAIN)

drum_block(1, L['kDrum1LabelY'], L['kDrum1VcfY'], L['kDrum1VcaY'])
drum_block(2, L['kDrum2LabelY'], L['kDrum2VcfY'], L['kDrum2VcaY'])

# the crossfader
x0, x1 = L['kMixX'], L['kMixX'] + L['kMixWidth']
y0, y1 = L['kMixTop'], L['kMixBottom']
mid = (x0 + x1) / 2
text(mid, y0, 'D1', fill=LABEL, fnt=F_SMALL, anchor='ma')
text(mid, y1 - 15, 'D2', fill=LABEL, fnt=F_SMALL, anchor='ma')
text(mid, y1 - 30, '.71/.71', fill=VALUE, fnt=F_SMALL, anchor='ma')

gy0, gy1 = y0 + 15 + 2, y1 - 15 - 2 - 15
bevel(mid - 8, gy0, mid + 8, gy1, BAR_LO, BAR_HI)
d.rectangle([s(mid - 8) + SCALE, s(gy0) + SCALE,
             s(mid + 8) - SCALE, s(gy1) - SCALE], fill=BAR_FL)

knob_y = gy0 + 4 + 0.5 * ((gy1 - gy0) - 8)
bevel(mid - 8 - 7, knob_y - 4, mid + 8 + 7, knob_y + 4, BAR_HI, BAR_LO)
d.rectangle([s(mid - 15) + SCALE, s(knob_y - 4) + SCALE,
             s(mid + 15) - SCALE, s(knob_y + 4) - SCALE], fill=GRID)

# the output trim, in drum 2's VCA row
slider(L['kTrimColumn'], L['kDrum2VcaY'], 'kOutputTrim')

text(L['kMargin'], L['kVelocityY'],
     'D1  v127: +3.60oct 100%   v64: +1.81oct 50%   v0: +0.00oct 0%'
     '      D2  v127: +2.10oct 100%   v64: +1.06oct 50%   v0: +0.00oct 0%',
     fill=TRACE, fnt=F_SMALL)
text(L['kMargin'], L['kRateY'],
     'Engine: 48000 Hz    cutoff ceiling 21600 Hz', fill=VALUE, fnt=F_SMALL)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs/panel.png')
img.save(out)
print('%s  %d x %d (drawn at %dx)'
      % (out, L['kEditorWidth'], L['kEditorHeight'], SCALE))
