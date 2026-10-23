#!/usr/bin/env python3
"""Draw the plug-in's panel (Design 2.0) into docs/panel.png.

WHY IT DOES NOT READ THE SOURCE WITH REGULAR EXPRESSIONS ANY MORE. The
Design 2.0 layout is DATA, in source/FilterDrumLayout.h, so this compiles
tools/dump-layout.cpp - which includes that header, the parameter table and
the DSP - and draws what it prints. Every position, every default and every
curve on the screens comes from the plug-in's own code; nothing here is a
second copy of it except the drawing.

What it cannot show is the real VSTGUI rendering: this is a faithful
reconstruction from the same numbers (Panel::Knob's geometry, the palette in
FilterDrumPanelViews.h), not a screenshot.

    python3 tools/render-panel.py [out.png] [sampleRate]

Needs a C++ compiler, Pillow, and the VST3 SDK's pluginterfaces/ (one
header, via FilterDrumParams.h) at external/vst3sdk or $VST3_SDK_ROOT.
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
OUT_SCALE = 2      # docs/panel.png is drawn at 2x so the text is legible
SS = 2             # and supersampled 2x again for clean arcs
K = OUT_SCALE * SS

# FilterDrumPanelViews.h's palette
ACCENT = [(242, 165, 65), (86, 168, 245), (95, 208, 160), (176, 140, 255)]
INK = [(26, 18, 5), (6, 20, 36), (6, 30, 20), (22, 15, 38)]
GROUND, HEADER, BORDER, SECTION = (20, 21, 24), (26, 28, 32), (42, 45, 51), (28, 30, 34)
RULE, TRACK, CAP, CAP_EDGE = (36, 38, 43), (46, 49, 55), (42, 44, 49), (60, 64, 71)
TEXT, TITLE, MUTED, SUBTLE = (236, 234, 228), (201, 204, 210), (156, 161, 170), (126, 131, 140)
DIM, SCREEN, SCREEN_EDGE, GRID = (74, 78, 86), (15, 16, 18), (38, 41, 46), (26, 29, 33)
SEG_BACK, PAD, PAD_EDGE = (18, 19, 22), (36, 37, 44), (48, 50, 58)
LAMP_OFF, LAMP_EDGE = (58, 42, 32), (90, 58, 40)


def dump(rate):
    cxx = os.environ.get('CXX') or shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
    if not cxx:
        raise SystemExit('render-panel.py: no C++ compiler - the panel is drawn from the '
                         "plug-in's own tables, and they need compiling.")
    sdk = os.environ.get('VST3_SDK_ROOT') or os.path.join(ROOT, 'external/vst3sdk')
    if not os.path.isfile(os.path.join(sdk, 'pluginterfaces/vst/vsttypes.h')):
        raise SystemExit('render-panel.py: no VST3 SDK at %s (run ./setup-xcode.sh once, '
                         'or set VST3_SDK_ROOT)' % sdk)
    src = lambda p: os.path.join(ROOT, p)
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'dump-layout')
        subprocess.run([cxx, '-std=c++17', '-O1', '-I' + src('source'), '-I' + sdk, '-o', exe,
                        src('tools/dump-layout.cpp'), src('source/FilterDrumParams.cpp'),
                        src('source/FilterDrumDsp.cpp'), src('source/FilterDrumTransport.cpp')],
                       check=True)
        return json.loads(subprocess.run([exe, str(rate)], check=True, capture_output=True,
                                         text=True).stdout)


def font(kind, size):
    """The editor's faces where the Mac has them, close stand-ins where not."""
    candidates = {
        'sans':  ['/System/Library/Fonts/HelveticaNeue.ttc', '/usr/share/fonts/truetype/lato/Lato-Regular.ttf',
                  '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'],
        'bold':  ['/System/Library/Fonts/HelveticaNeue.ttc', '/usr/share/fonts/truetype/lato/Lato-Bold.ttf',
                  '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'],
        'mono':  ['/System/Library/Fonts/Menlo.ttc', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'],
        'monob': ['/System/Library/Fonts/Menlo.ttc', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'],
        'word':  ['/System/Library/Fonts/Avenir Next Condensed.ttc', '/usr/share/fonts/truetype/lato/Lato-Black.ttf',
                  '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'],
    }[kind]
    index = {'bold': 1, 'monob': 1, 'word': 2}.get(kind, 0)
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size * K, index=index if path.endswith('.ttc') else 0)
            except OSError:
                return ImageFont.truetype(path, size * K)
    return ImageFont.load_default()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs/panel.png')
    rate = float(sys.argv[2]) if len(sys.argv) > 2 else 48000.0
    L = dump(rate)
    W, H = L['size']
    img = Image.new('RGB', (W * K, H * K), GROUND)
    d = ImageDraw.Draw(img, 'RGBA')
    s = lambda v: v * K

    def box(r, radius, fill, outline=None, width=1):
        d.rounded_rectangle([s(r[0]), s(r[1]), s(r[2]) - 1, s(r[3]) - 1], radius=s(radius),
                            fill=fill, outline=outline, width=int(s(width)) if outline else 0)

    def text(r, msg, fnt, fill, align='left'):
        l, t, rr, b = r
        bb = d.textbbox((0, 0), msg, font=fnt)
        w = bb[2] - bb[0]
        x = s(l) if align == 'left' else (s(rr) - w if align == 'right' else (s(l) + s(rr) - w) / 2)
        asc, desc = fnt.getmetrics()
        y = (s(t) + s(b)) / 2 - (asc + desc) / 2
        d.text((x - bb[0], y), msg, font=fnt, fill=fill)

    def arc(cx, cy, rad, a0, a1, col, width):
        if a1 - a0 < 0.5:
            return
        bb = [s(cx - rad - width / 2), s(cy - rad - width / 2), s(cx + rad + width / 2), s(cy + rad + width / 2)]
        d.arc(bb, a0, a1, fill=col, width=int(s(width)))
        for a in (a0, a1):   # round caps
            px, py = cx + rad * math.cos(math.radians(a)), cy + rad * math.sin(math.radians(a))
            d.ellipse([s(px - width / 2), s(py - width / 2), s(px + width / 2), s(py + width / 2)], fill=col)

    def polyline(pts, col, width, dashed=False):
        pts = [(s(x), s(y)) for x, y in pts]
        if not dashed:
            d.line(pts, fill=col, width=int(s(width)), joint='curve')
            return
        on, dash, left = True, s(3), s(3)
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            seg = math.hypot(x1 - x0, y1 - y0)
            pos = 0.0
            while pos < seg:
                step = min(left, seg - pos)
                if on:
                    f0, f1 = pos / seg, (pos + step) / seg
                    d.line([(x0 + (x1 - x0) * f0, y0 + (y1 - y0) * f0),
                            (x0 + (x1 - x0) * f1, y0 + (y1 - y0) * f1)], fill=col, width=int(s(width)))
                pos += step
                left -= step
                if left <= 0:
                    on = not on
                    left = dash if on else s(2.5)

    F = {'title': font('bold', 13), 'sub': font('sans', 12), 'label': font('bold', 10), 'value': font('mono', 10),
         'note': font('sans', 11), 'badge': font('monob', 12), 'seg': font('bold', 12), 'button': font('bold', 13),
         'word': font('word', 23)}

    # ---- the header -------------------------------------------------------
    hh = L['header']
    d.rectangle([0, 0, s(W), s(hh)], fill=HEADER)
    d.rectangle([0, s(hh - 1), s(W), s(hh)], fill=BORDER)
    wm = L['wordmark']
    fw = d.textbbox((0, 0), 'FILTER', font=F['word'])[2] / K
    text(wm, 'FILTER', F['word'], TEXT)
    text([wm[0] + fw + 1, wm[1], wm[2] + 60, wm[3]], 'DRUM', F['word'], ACCENT[0])
    d.rectangle([s(wm[2] + 4), s(16), s(wm[2] + 5), s(40)], fill=BORDER)
    text(L['engineRect'], L['engine'], F['sub'], SUBTLE)

    # ---- sections ------------------------------------------------------------
    for sec in L['sections']:
        r = sec['r']
        box(r, 10, SECTION, BORDER)
        top = r[1] + 12
        mid = top + 9
        tw = d.textbbox((0, 0), sec['title'], font=F['title'])[2] / K
        mw = 30 if sec['badge'] else 16
        left = r[0] + 14 if not sec['centred'] else (r[0] + r[2]) / 2 - (mw + tw) / 2
        if sec['badge']:
            d.ellipse([s(left), s(mid - 11), s(left + 22), s(mid + 11)], fill=CAP)
            text([left, mid - 11, left + 22, mid + 11], sec['badge'], F['badge'], ACCENT[sec['accent']], 'center')
        else:
            d.ellipse([s(left), s(mid - 4), s(left + 8), s(mid + 4)], fill=ACCENT[sec['accent']])
        text([left + mw, top, r[2], top + 18], sec['title'], F['title'], TITLE)
        if sec['sub']:
            text([left + mw + tw + 14, top, r[2], top + 18], sec['sub'], F['sub'], SUBTLE)

    for x, y in L['chevrons']:
        polyline([(x - 3, y - 8), (x + 3, y), (x - 3, y + 8)], DIM, 2)
    for r in L['rules']:
        d.rectangle([s(r[0]), s(r[1]), s(r[2]) - 1, s(r[3]) - 1], fill=RULE)
    for c in L['captions']:
        align = 'center' if c['centred'] else 'left'
        if c['style'] == 2:
            text(c['r'], c['text'], F['note'], SUBTLE, align)
        else:
            text(c['r'], c['text'].upper(), F['label'], ACCENT[c['accent']] if c['style'] == 0 else MUTED, align)

    # ---- screens and lamps ----------------------------------------------------
    def scope(r, caption, cap_col, ann, main, comp, main_col, comp_col, dot=None):
        box(r, 8, SCREEN)
        for x in range(r[0] + 24, r[2] - 1, 24):
            d.rectangle([s(x), s(r[1] + 1), s(x + 1) - 1, s(r[3] - 1)], fill=GRID)
        for y in range(r[1] + 24, r[3] - 1, 24):
            d.rectangle([s(r[0] + 1), s(y), s(r[2] - 1), s(y + 1) - 1], fill=GRID)
        text([r[0] + 8, r[1] + 3, r[2] - 8, r[1] + 16], caption, F['value'], cap_col)
        text([r[0] + 8, r[1] + 3, r[2] - 8, r[1] + 16], ann, F['value'], SUBTLE, 'right')
        pl, pr, pt, pb = r[0] + 6, r[2] - 6, r[1] + 19, r[3] - 6
        pts = lambda data: [(pl + (pr - pl) * i / (len(data) - 1), pb - max(0, min(1, v)) * (pb - pt))
                            for i, v in enumerate(data)]
        polyline(pts(comp), comp_col, 1.5, dashed=True)
        poly = [(s(x), s(y)) for x, y in pts(main)] + [(s(pr), s(pb)), (s(pl), s(pb))]
        d.polygon(poly, fill=main_col + (40,))
        polyline(pts(main), main_col, 2)
        if dot:
            x, y = pl + dot[0] * (pr - pl), pb - max(0, min(1, dot[1])) * (pb - pt)
            d.ellipse([s(x - 4), s(y - 4), s(x + 4), s(y + 4)], fill=SCREEN, outline=main_col, width=int(s(2)))
        box(r, 8, None, SCREEN_EDGE)

    def lamp(r, name, on, col):
        tw = d.textbbox((0, 0), name, font=F['label'])[2] / K
        cx, cy = r[2] - tw - 10, (r[1] + r[3]) / 2
        if on:
            d.ellipse([s(cx - 7), s(cy - 7), s(cx + 7), s(cy + 7)], fill=col + (70,))
            d.ellipse([s(cx - 4), s(cy - 4), s(cx + 4), s(cy + 4)], fill=col)
        else:
            d.ellipse([s(cx - 4), s(cy - 4), s(cx + 4), s(cy + 4)], fill=LAMP_OFF, outline=LAMP_EDGE, width=K)
        text([r[2] - tw, r[1], r[2], r[3]], name, F['label'], col if on else SUBTLE)

    for ln in L['lanes']:
        scope(ln['filter'], 'at the knobs', SUBTLE, ln['peak'], ln['knobs'], ln['strike'],
              ACCENT[1], (86, 168, 245, 150), ln['dot'])
        scope(ln['env'], ln['caption'], ACCENT[1], ln['span'], ln['vca'], ln['vcf'], ACCENT[2], ACCENT[1])
        lamp(ln['lamp'], 'SELF-OSC', ln['selfOsc'], (255, 138, 61))
        text(ln['velRect'], ln['velocity'], F['value'], SUBTLE, 'right')

    text(L['mixRect'], L['mix'], F['value'], MUTED, 'center')
    lamp(L['strike1'], 'D1', True, ACCENT[0])     # drawn as just struck
    lamp(L['strike2'], 'D2', True, ACCENT[0])
    text(L['strikeVelRect'], 'vel 127', F['value'], SUBTLE, 'center')

    # ---- controls ---------------------------------------------------------------
    for c in L['controls']:
        r, col, kind = c['r'], ACCENT[c['accent']], c['kind']
        if kind == 0:     # Panel::Knob
            v = 1 - c['value'] if c['reversed'] else c['value']
            dia = c['diameter']
            st = 5 if dia >= 60 else 4
            cx, cy = (r[0] + r[2]) / 2, r[1] + dia / 2
            rad = dia / 2 - st / 2 - 1
            arc(cx, cy, rad, 135, 405, TRACK, st)
            a = 135 + 270 * v
            if c['bipolar']:
                arc(cx, cy, rad, min(a, 270), max(a, 270), col, st)
                if abs(a - 270) < 0.5:
                    d.ellipse([s(cx - st / 2), s(cy - rad - st / 2), s(cx + st / 2), s(cy - rad + st / 2)], fill=col)
            else:
                arc(cx, cy, rad, 135, a, col, st)
            cr = rad - st / 2 - 4
            d.ellipse([s(cx - cr), s(cy - cr), s(cx + cr), s(cy + cr)], fill=CAP, outline=CAP_EDGE, width=K)
            ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
            d.line([(s(cx + cr * 0.2 * ca), s(cy + cr * 0.2 * sa)), (s(cx + (cr - 2) * ca), s(cy + (cr - 2) * sa))],
                   fill=TEXT, width=int(s(2)))
            tt = r[1] + dia + 2
            text([r[0] - 4, tt, r[2] + 4, tt + 13], c['reading'], F['value'], TEXT, 'center')
            text([r[0] - 4, tt + 13, r[2] + 4, tt + 25], c['label'].upper(), F['label'], MUTED, 'center')
        elif kind == 2:   # Panel::StepPad
            on = c['value'] >= 0.5
            inner = [r[0] + 1, r[1] + 1, r[2] - 1, r[3] - 1]
            if on:
                box(inner, 7, col)
                box([inner[0] + 3, inner[1] + 3, inner[2] - 3, inner[1] + 3 + (inner[3] - inner[1]) * 0.36], 5,
                    (255, 255, 255, 34))
            else:
                box(inner, 7, PAD, PAD_EDGE)
            text([r[0], r[3] - 18, r[2], r[3] - 4], str(c['number']), F['badge'] if on else F['value'],
                 INK[c['accent']] if on else MUTED, 'center')
        elif kind == 3:   # Panel::ToggleButton
            on = c['value'] >= 0.5
            inner = [r[0] + 1, r[1] + 1, r[2] - 1, r[3] - 1]
            box(inner, 8, (col + (46,)) if on else PAD, col if on else PAD_EDGE)
            cy = (r[1] + r[3]) / 2
            d.ellipse([s(r[0] + 15), s(cy - 5), s(r[0] + 25), s(cy + 5)], fill=col if on else SEG_BACK,
                      outline=None if on else PAD_EDGE, width=K)
            text([r[0] + 36, r[1] + 7, r[2] - 6, r[1] + 25], c['label'].upper(), F['button'], col if on else MUTED)
            text([r[0] + 36, r[1] + 25, r[2] - 6, r[1] + 39], c['reading'], F['value'], TEXT if on else SUBTLE)
        elif kind == 1:   # Panel::SegmentSwitch
            box(r, 8, SEG_BACK, BORDER)
            names = L['divisions']
            n = len(names)
            cur = int(round(c['value'] * (n - 1)))
            sw = (r[2] - r[0] - 6) / n
            for i, name in enumerate(names):
                seg = [r[0] + 3 + i * sw, r[1] + 3, r[0] + 3 + (i + 1) * sw, r[3] - 3]
                if i == cur:
                    box(seg, 6, col)
                text(seg, name, F['seg'], INK[c['accent']] if i == cur else MUTED, 'center')

    img = img.resize((W * OUT_SCALE, H * OUT_SCALE), Image.LANCZOS)
    img.save(out)
    print('wrote', out)


if __name__ == '__main__':
    main()
