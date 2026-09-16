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
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

SCALE = 2          # drawn at 2x so the text is legible


# ---------------------------------------------------------------------------
# The numbers, read out of the source rather than copied
# ---------------------------------------------------------------------------
def read_constants(path, wanted, seed=None):
    """Pull `constexpr int NAME = VALUE;` / `static const int NAME = VALUE;`
    out of a file, resolving references to constants already found.

    `seed` is constants from a file read earlier - the panel's width lives
    in the header and the sequencer row is positioned off it, so the
    header has to be read first and handed in here."""
    text = open(os.path.join(ROOT, path), encoding='utf-8').read()
    # The class qualifier is noise to eval; the name is what matters.
    text = text.replace('FilterDrumEditor::', '')
    found = dict(seed or {})
    pattern = re.compile(
        r'(?:constexpr|static\s+const)\s+int\s+(\w+)\s*=\s*([^;]+);')
    # EVERY constant is evaluated, not just the wanted ones, because the
    # wanted ones are now DERIVED from intermediates - kDrum1VcaY is
    # kDrum1VcfY + kRowPitch, and kRowPitch is not something this script
    # draws with. Skipping unwanted names left those references
    # unresolvable and reported a dozen constants as "gone" when they
    # were merely arithmetic.
    for name, expr in pattern.findall(text):
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


H = read_constants('source/FilterDrumEditor.h',
                   ['kEditorWidth', 'kEditorHeight'])

L = read_constants('source/FilterDrumEditor.cpp', [
    'kMargin', 'kColumnWidth', 'kColumnGap', 'kColumnPitch', 'kSliderHeight',
    'kTitleY', 'kLabelHeight',
    'kDrum1LabelY', 'kDrum1VcfY', 'kDrum1VcaY', 'kDrum1ShapeY',
    'kDrum2LabelY', 'kDrum2VcfY', 'kDrum2VcaY', 'kDrum2ShapeY',
    'kRowPitch', 'kBlockPitch', 'kTrimRowY',
    'kVelocityY', 'kRateY',
    'kMixWidth', 'kMixX', 'kMixTop', 'kMixBottom', 'kTrimColumn',
    'kSeqLabelY', 'kSeqRowY', 'kStepWidth', 'kStepGap', 'kStepPitch',
    'kStepHeight', 'kSeqCtrlX', 'kSeqCtrlW', 'kSeqCtrlGap', 'kSeqCtrlSpan',
    'kSeqCtrlClear', 'kSeqBoxRight',
    'kGroupPadX', 'kGroupPadTop', 'kGroupPadBottom', 'kGroupGap', 'kDrumPad',
    'kGroupX', 'kContentX', 'kGroupHeight', 'kGroupW',
    'kDrum1BoxY', 'kDrum2BoxY', 'kDrumBoxX', 'kDrumBoxW', 'kDrumBoxH',
    'kMixBoxX', 'kMixBoxW', 'kMixBoxTop', 'kMixBoxBottom',
    'kOutputBoxY', 'kOutputBoxW', 'kReadoutX',
    'kEnvX', 'kEnvWidth', 'kEnvGap', 'kEnvPoints',
    'kEnv1Top', 'kEnv1Bottom', 'kEnv2Top', 'kEnv2Bottom',
], seed=H)
L.update(H)

# The control titles, from the parameter table.
TABLE = open(os.path.join(ROOT, 'source/FilterDrumParams.cpp'),
             encoding='utf-8').read()
TITLES = dict(re.findall(r'\{(k\w+),\s*"([^"]+)"', TABLE))
if len(TITLES) < 42:
    raise SystemExit('render-panel.py: found %d parameters, expected 42'
                     % len(TITLES))


SHAPE_TAGS = ('kVcfAttackShape', 'kVcfReleaseShape',
              'kVcaAttackShape', 'kVcaReleaseShape')


def short(tag):
    """The label the editor draws: drum 1's title, section prefix stripped.

    This mirrors FilterDrumEditor::shortLabelFor, INCLUDING its exception
    for the four shape controls - they are the one place the VCF/VCA
    prefix goes back on, because stripping it leaves four labels reading
    Atk / Rel / Atk / Rel. Getting that wrong here does not break the
    plug-in, it draws a picture of a panel that does not exist, which is
    the failure this whole script is built to avoid."""
    base = tag[:-1] if tag.endswith('2') and tag != 'kResonance' else tag

    if base in SHAPE_TAGS:
        vcf = base.startswith('kVcf')
        atk = 'Attack' in base
        return ('VCF ' if vcf else 'VCA ') + ('Atk' if atk else 'Rel')

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
OUTER  = (100, 100, 100)
GRID_B = (100, 255, 100)
G_FRAME = (120, 128, 120)   # Colours::kGroupFrame
D_FRAME = (90, 150, 90)     # Colours::kDrumFrame
T_VCF  = (60, 255, 90)      # Colours::kTraceVcf
T_VCA  = (255, 70, 70)      # Colours::kTraceVca
PLATE  = (12, 13, 12)       # Colours::kPlate over the panel ground


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


def group_box(x, y, w, h, title, drum=False):
    """One SpyGroupBox: a rectangle with its name breaking the top edge."""
    colour = D_FRAME if drum else G_FRAME
    fnt = F_SMALL

    if not title:
        d.rectangle([s(x), s(y), s(x + w) - 1, s(y + h) - 1],
                    outline=colour, width=1)
        return

    tw = d.textlength(title, font=fnt) / SCALE
    gap_start = x + 8 - 4
    gap_end = gap_start + tw + 8

    # the top edge in two pieces, with the title in the gap
    d.line([s(x), s(y), s(gap_start), s(y)], fill=colour, width=1)
    if gap_end < x + w:
        d.line([s(gap_end), s(y), s(x + w) - 1, s(y)], fill=colour, width=1)
    d.line([s(x), s(y + h) - 1, s(x + w) - 1, s(y + h) - 1], fill=colour, width=1)
    d.line([s(x), s(y), s(x), s(y + h) - 1], fill=colour, width=1)
    d.line([s(x + w) - 1, s(y), s(x + w) - 1, s(y + h) - 1], fill=colour, width=1)

    text(x + 8, y - 5, title, fill=colour, fnt=fnt)


def slider(col, y, tag, value=0.55, lamp=None):
    """One SpySlider: value text at the top, bar, label at the bottom."""
    x = L['kContentX'] + col * L['kColumnPitch']
    w, h = L['kColumnWidth'], L['kSliderHeight']

    text(x + w / 2, y + 1, '--', fill=VALUE, fnt=F_MAIN, anchor='ma')

    # the bar: rect.bottom-15 .. rect.bottom-3, per kBarHeight/kBarBottomInset
    by1, by0 = y + h - 3, y + h - 15

    # A CONTROL AT THE BOTTOM OF ITS TRAVEL HAS NO BAR. The four shape
    # knobs default to -100 - the Exponential end - which normalises to
    # 0, and drawing a zero-width bevel asked PIL for a rectangle whose
    # right edge was left of its left edge. Nothing to draw is the right
    # picture as well as the safe one.
    fill_w = w * value
    if fill_w >= 2:
        bevel(x, by0, x + fill_w, by1, BAR_LO, BAR_HI)
        d.rectangle([s(x) + SCALE, s(by0) + SCALE,
                     s(x + fill_w) - SCALE, s(by1) - SCALE], fill=BAR_FL)

    text(x + w / 2, y + h - L['kLabelHeight'] - 1, short(tag),
         fill=LABEL, fnt=F_SMALL, anchor='ma')

    if lamp is not None:
        d.rectangle([s(x), s(y), s(x + 10), s(y + 10)],
                    fill=LAMP if lamp else (0, 0, 0), outline=BAR_FL)


def drum_block(drum, label_y, vcf_y, vca_y, shape_y):
    suffix = '2' if drum == 2 else ''

    # The outer box, then the three group boxes, then the controls -
    # back to front, exactly as addDrumBlock adds them.
    box_y = L['kDrum1BoxY'] if drum == 1 else L['kDrum2BoxY']
    group_box(L['kDrumBoxX'], box_y, L['kDrumBoxW'], L['kDrumBoxH'],
              'DRUM %d' % drum, drum=True)
    for row_y, title in ((vcf_y, 'VCF'), (vca_y, 'VCA'),
                         (shape_y, 'ENVELOPE SHAPE')):
        group_box(L['kGroupX'], row_y - L['kGroupPadTop'],
                  L['kGroupW'], L['kGroupHeight'], title)

    text(L['kContentX'], label_y,
         'noise -> MS-20 lowpass -> VCA      (lamp = self-oscillating)'
         if drum == 1 else 'the same voice again, with its own settings',
         fill=LABEL, fnt=F_MAIN)

    for col, tag in enumerate(['kNoiseLevel', 'kCutoff', 'kResonance',
                               'kVcfAttack', 'kVcfRelease', 'kVcfAmount',
                               'kVcfVelocity']):
        slider(col, vcf_y, tag + suffix,
               lamp=False if tag == 'kResonance' else None)

    for col, tag in enumerate(['kVcaAttack', 'kVcaRelease',
                               'kVcaAmount', 'kVcaVelocity']):
        slider(col, vca_y, tag + suffix)

    # The SHAPE row. Drum 2's shape ids carry the suffix in a different
    # place - kVcfAttackShape2, not kVcfAttack2Shape - so the suffix is
    # appended here exactly as it is for the rows above and the table
    # lookup finds the right title either way.
    for col, tag in enumerate(['kVcfAttackShape', 'kVcfReleaseShape',
                               'kVcaAttackShape', 'kVcaReleaseShape']):
        slider(col, shape_y, tag + suffix, value=0.0)

    # The legend, in the columns the shape row does not use. The box is
    # titled, so this only has to say what the travel is.
    text(L['kContentX'] + 4 * L['kColumnPitch'],
         shape_y + L['kSliderHeight'] - L['kLabelHeight'] - 1,
         'Exp  ->  Lin  ->  Log',
         fill=LABEL, fnt=F_SMALL)


# ---------------------------------------------------------------------------
text(L['kContentX'], L['kTitleY'],
     'FilterDrum   -   two monophonic MS-20 drum voices, struck together',
     fill=VALUE, fnt=F_MAIN)

drum_block(1, L['kDrum1LabelY'], L['kDrum1VcfY'], L['kDrum1VcaY'], L['kDrum1ShapeY'])
drum_block(2, L['kDrum2LabelY'], L['kDrum2VcfY'], L['kDrum2VcaY'], L['kDrum2ShapeY'])

# ---------------------------------------------------------------------------
# The two envelope displays
#
# THE CURVES COME OUT OF THE PLUG-IN. tools/dump-envelope.cpp is compiled
# against source/FilterDrumDsp.cpp and run, so the shapes drawn here are the
# ones traceDrumEnvelopes() produces and not a Python re-implementation of
# them. A re-implementation would go on looking right for exactly as long as
# nobody changed the envelope, which is the failure this whole script exists
# to avoid.
#
# The SETTINGS come from the defaults in source/FilterDrumParams.cpp, parsed
# the same way the titles are. So nothing here is a copy of anything.
# ---------------------------------------------------------------------------
def envelope_defaults(drum):
    """The six defaults that shape one drum's curves, out of the table."""
    text = open(os.path.join(ROOT, 'source/FilterDrumParams.cpp'),
                encoding='utf-8').read()
    suffix = '2' if drum == 2 else ''
    out = {}
    for base in ('kVcfAttack', 'kVcfRelease', 'kVcfAmount',
                 'kVcaAttack', 'kVcaRelease', 'kVcaAmount',
                 'kVcfAttackShape', 'kVcfReleaseShape',
                 'kVcaAttackShape', 'kVcaReleaseShape'):
        name = base + suffix
        # {kName, "Title", "unit", ParamType::X, min, max, DEFAULT, lo, hi,
        m = re.search(r'\{\s*' + name + r'\s*,' + r'[^}]*?}', text)
        if not m:
            raise SystemExit('render-panel.py: no table row for ' + name)
        fields = [f.strip() for f in m.group(0).strip('{}').split(',')]
        # 0 id, 1 title, 2 unit, 3 type, 4 min, 5 max, 6 default,
        # 7 internal-lo, 8 internal-hi
        plain_min, plain_max = float(fields[4]), float(fields[5])
        default = float(fields[6])
        lo, hi = float(fields[7]), float(fields[8])
        if 'Log' in fields[3]:
            import math
            norm = (math.log(default) - math.log(plain_min)) / \
                   (math.log(plain_max) - math.log(plain_min))
            internal = math.exp(math.log(lo) + norm * (math.log(hi) - math.log(lo)))
        else:
            norm = (default - plain_min) / (plain_max - plain_min)
            internal = lo + norm * (hi - lo)
        out[base] = internal
    return out


def envelope_curves(drum, points):
    """Compile and run tools/dump-envelope.cpp for this drum's defaults."""
    cxx = shutil.which('g++') or shutil.which('clang++')
    if not cxx:
        raise SystemExit('render-panel.py: no C++ compiler, so the envelope '
                         'curves cannot be taken from the plug-in. Refusing '
                         'to draw them from a second copy of the maths.')

    d = envelope_defaults(drum)
    # kMaxEnvOctaves, read out of the DSP header rather than typed.
    dsp = open(os.path.join(ROOT, 'source/FilterDrumDsp.h'), encoding='utf-8').read()
    m = re.search(r'constexpr\s+double\s+kMaxEnvOctaves\s*=\s*([0-9.]+)', dsp)
    if not m:
        raise SystemExit('render-panel.py: kMaxEnvOctaves is gone from '
                         'FilterDrumDsp.h')
    max_oct = float(m.group(1))

    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'dumpenv')
        subprocess.run(
            [cxx, '-std=c++17', '-O2', '-I' + os.path.join(ROOT, 'source'),
             '-o', exe,
             os.path.join(ROOT, 'tools/dump-envelope.cpp'),
             os.path.join(ROOT, 'source/FilterDrumDsp.cpp')],
            check=True)
        args = [exe,
                '%.9f' % d['kVcfAttack'], '%.9f' % d['kVcfRelease'],
                '%.9f' % d['kVcfAttackShape'], '%.9f' % d['kVcfReleaseShape'],
                '%.9f' % d['kVcaAttack'], '%.9f' % d['kVcaRelease'],
                '%.9f' % d['kVcaAttackShape'], '%.9f' % d['kVcaReleaseShape'],
                str(points)]
        lines = subprocess.run(args, check=True,
                               capture_output=True, text=True).stdout.split()

    span = float(lines[0])
    vals = [float(v) for v in lines[1:]]

    # The two Amount marker heights, 0..1, and the signed figures for the
    # legend. The VCF's magnitude sets the marker and its sign goes in
    # the text, which is the panel's own rule - see
    # FilterDrumEditor::refreshEnvelopeDisplay.
    return (span, vals[0::2], vals[1::2],
            abs(d['kVcfAmount']) / max_oct, d['kVcaAmount'],
            d['kVcfAmount'])


def envelope_display(drum, top, bottom):
    x0, x1 = L['kEnvX'], L['kEnvX'] + L['kEnvWidth']
    d.rectangle([s(x0), s(top), s(x1) - 1, s(bottom) - 1],
                fill=PLATE, outline=OUTER, width=SCALE)

    span, vcf, vca, vcf_amt, vca_amt, octaves = envelope_curves(
        drum, L['kEnvPoints'])

    text(x0 + 4, top + 3, 'DRUM %d ENV' % drum, fill=VALUE, fnt=F_SMALL)
    label = ('%.2f s' % span) if span >= 1.0 else ('%.0f ms' % (span * 1000))
    text(x1 - 4, top + 3, label, fill=BAR_LO, fnt=F_SMALL, anchor='ra')

    text(x0 + 4, bottom - 14, 'VCF %+.1foct' % octaves, fill=T_VCF, fnt=F_SMALL)
    text(x1 - 4, bottom - 14, 'VCA %.0f%%' % (vca_amt * 100),
         fill=T_VCA, fnt=F_SMALL, anchor='ra')

    px0, pw = x0 + 2, L['kEnvWidth'] - 4
    py0 = top + 2 + 12
    ph = (bottom - top) - 4 - 12 - 12

    d.line([s(px0), s(py0 + ph), s(px0 + pw), s(py0 + ph)], fill=OUTER,
           width=SCALE)

    # The two Amount markers, a quarter of the plot wide, BEFORE the
    # curves so a curve crossing one is drawn over it. Same order and
    # same fraction as SpyEnvelopeView::draw.
    for amount, colour in ((vcf_amt, T_VCF), (vca_amt, T_VCA)):
        y = py0 + ph - max(0.0, min(1.0, amount)) * ph
        d.line([s(px0), s(y), s(px0 + pw * 0.25), s(y)], fill=colour,
               width=SCALE)

    for series, colour in ((vcf, T_VCF), (vca, T_VCA)):
        pts = []
        for i, v in enumerate(series):
            x = px0 + pw * i / float(len(series) - 1)
            pts += [s(x), s(py0 + ph - max(0.0, min(1.0, v)) * ph)]
        d.line(pts, fill=colour, width=SCALE)


envelope_display(1, L['kEnv1Top'], L['kEnv1Bottom'])
envelope_display(2, L['kEnv2Top'], L['kEnv2Bottom'])

# the crossfader, in its own box
group_box(L['kMixBoxX'], L['kMixBoxTop'], L['kMixBoxW'],
          L['kMixBoxBottom'] - L['kMixBoxTop'], 'MIX')

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

# the output trim, in a box of its own below both drums
group_box(L['kMargin'], L['kOutputBoxY'], L['kOutputBoxW'],
          L['kGroupHeight'], 'OUTPUT')
slider(L['kTrimColumn'], L['kTrimRowY'], 'kOutputTrim')

# the sequencer row: sixteen small switches, then Run and Launch On
text(L['kContentX'], L['kSeqLabelY'],
     '16 steps = one bar of 1/16ths   '
     '(lamp = playhead; MIDI still triggers)', fill=LABEL, fnt=F_MAIN)

group_box(L['kMargin'], L['kSeqRowY'] - L['kGroupPadTop'],
          L['kSeqBoxRight'] - L['kMargin'],
          L['kStepHeight'] + L['kGroupPadTop'] + L['kGroupPadBottom'],
          'SEQUENCER')

# the default pattern, four on the floor, and a playhead part way through
PATTERN = [(i % 4) == 0 for i in range(16)]
PLAYHEAD = 6

for i in range(16):
    x = L['kContentX'] + i * L['kStepPitch']
    y = L['kSeqRowY']
    w, h = L['kStepWidth'], L['kStepHeight']
    mid = x + w / 2

    # SpyStepSwitch: a box round the whole cell, the lamp CENTRED above
    # the number, and the bar across the bottom when the step is on.
    d.rectangle([s(x), s(y), s(x + w - 1), s(y + h - 1)],
                outline=OUTER, width=SCALE)

    d.rectangle([s(mid - 4), s(y + 4), s(mid + 4), s(y + 12)],
                fill=LAMP if i == PLAYHEAD else (0, 0, 0), outline=(100, 100, 100))

    text(mid, y + 15, str(i + 1),
         fill=VALUE if PATTERN[i] else LABEL, fnt=F_SMALL, anchor='ma')

    by1, by0 = y + h - 4, y + h - 13
    bevel(x + 4, by0, x + w - 4, by1, BAR_LO, BAR_HI)
    if PATTERN[i]:
        d.rectangle([s(x + 4) + SCALE, s(by0) + SCALE,
                     s(x + w - 4) - SCALE, s(by1) - SCALE], fill=BAR_FL)

# Run is the same boxed cell as a step, with a reading where a step has
# a bar. Launch On is a SpySelector - an outlined box with its value
# across it - and keeps its own shape.
y, w, h = L['kSeqRowY'], L['kSeqCtrlW'], L['kStepHeight']

cx = L['kSeqCtrlX']
mid = cx + w / 2
d.rectangle([s(cx), s(y), s(cx + w - 1), s(y + h - 1)], outline=OUTER, width=SCALE)
d.rectangle([s(mid - 4), s(y + 4), s(mid + 4), s(y + 12)],
            fill=LAMP, outline=(100, 100, 100))
text(mid, y + 15, 'armed', fill=VALUE, fnt=F_SMALL, anchor='ma')
text(mid, y + h - 13, 'Run', fill=LABEL, fnt=F_SMALL, anchor='ma')

cx = L['kSeqCtrlX'] + L['kSeqCtrlW'] + L['kSeqCtrlGap']
mid = cx + w / 2
d.rectangle([s(cx), s(y), s(cx + w - 1), s(y + h - 1)], outline=GRID_B, width=SCALE)
text(mid, y + 11, '1/1', fill=VALUE, fnt=F_MAIN, anchor='ma')
text(mid, y + h - 13, 'Launch On', fill=LABEL, fnt=F_SMALL, anchor='ma')

text(L['kReadoutX'], L['kVelocityY'],
     'D1  v127: +3.60oct 100%   v64: +1.81oct 50%   v0: +0.00oct 0%'
     '      D2  v127: +2.10oct 100%   v64: +1.06oct 50%   v0: +0.00oct 0%',
     fill=TRACE, fnt=F_SMALL)
text(L['kReadoutX'], L['kRateY'],
     'Engine: 48000 Hz    cutoff ceiling 21600 Hz', fill=VALUE, fnt=F_SMALL)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs/panel.png')
img.save(out)
print('%s  %d x %d (drawn at %dx)'
      % (out, L['kEditorWidth'], L['kEditorHeight'], SCALE))
