"""Black-panel drawing kit for the FilterDrum mock-ups: R2Synth 1.1's
Polivoks look (matte black, white printing, boxed sections, knurled knobs
with printed scales, chicken-head selectors, small colour OLEDs behind
glass, every word in the 70s sci-fi lettering). Panel units; rendered 2x.

Every knob, selector, screen and screw is also recorded in GEO so the wear
pass (weather.py) knows where the parts are."""
import math, json
from html import escape

W, H = 1200, 660
INK = '#f4f1e8'        # white printing
DIM = '#8f8c85'        # unselected / secondary printing
FAINT = '#5a5852'
BOX = '#d9d6cc'
SUBBOX = '#55534e'
GROUND = '#121212'
SECTION = '#151515'
RED = '#e2342b'
LAMP_ON, LAMP_OFF = '#ff3b2f', '#3a1512'
TRACE = {'noise': '#ffb23a', 'filter': '#2fb8ff', 'amp': '#38f0a0', 'seq': '#c27bff'}
SCREEN_TEXT = '#8d96a3'
MONO = "'DejaVu Sans Mono', monospace"
SANS = "'DejaVu Sans Condensed', sans-serif"


def pol(cx, cy, r, deg):
    t = math.radians(deg)
    return cx + r * math.sin(t), cy - r * math.cos(t)


class Panel:
    def __init__(s, w=W, h=H):
        s.w, s.h = w, h
        s.parts = []
        s.geo = dict(width=w, height=h, knobs=[], selectors=[], screens=[], screws=[], keys=[])
        s.n = 0

    def add(s, x):
        s.parts.append(x)

    def uid(s):
        s.n += 1
        return 'g%d' % s.n

    # ---------------------------------------------------------------- basics
    def text(s, x, y, msg, size=9.5, fill=INK, anchor='start', bold=True, family=SANS):
        msg = msg.replace('->', '→')
        y = y + size * 0.32
        s.add(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" font-size="{size}" '
              f'font-weight="{700 if bold else 400}" fill="{fill}" text-anchor="{anchor}">{escape(msg)}</text>')

    def rect(s, r, fill='none', stroke='none', sw=1, rx=0, extra=''):
        s.add(f'<rect x="{r[0]:.1f}" y="{r[1]:.1f}" width="{r[2]-r[0]:.1f}" height="{r[3]-r[1]:.1f}" rx="{rx}" '
              f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>')

    def line(s, x1, y1, x2, y2, stroke=INK, sw=1, extra=''):
        s.add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{stroke}" '
              f'stroke-width="{sw}" stroke-linecap="round" {extra}/>')

    def circle(s, cx, cy, r, fill='none', stroke='none', sw=1, extra=''):
        s.add(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{fill}" stroke="{stroke}" '
              f'stroke-width="{sw}" {extra}/>')

    # ---------------------------------------------------------------- chrome
    def backdrop(s, engine='ENGINE: 48000 HZ   /   CUTOFF CEILING 21600 HZ', header=56, licence=True, div=266):
        s.rect([0, 0, s.w, s.h], fill=GROUND)
        s.rect([0, 0, s.w, header], fill='#0c0c0c')
        s.line(0, header - 1, s.w, header - 1, '#e9e6dc', 2)
        for x, y in [(8, 8), (s.w - 8, 8), (8, s.h - 8), (s.w - 8, s.h - 8)]:
            s.circle(x, y, 3.2, fill='#3a3a3a', stroke='#000', sw=0.6)
            s.line(x - 2.2, y - 2.2, x + 2.2, y + 2.2, '#222', 0.8)
            s.geo['screws'].append([x, y])
        s.add(f'<g id="wm" transform="translate(22,{header / 2 - 12:.1f})"></g>')
        s.line(div, 16, div, 40, FAINT, 1)
        s.text(div + 14, header / 2, engine, 10.5, INK, bold=False, family=MONO)
        if licence:
            r = [s.w - 16 - 300, 14, s.w - 16, 42]
            s.rect(r, fill='#0c0c0c', stroke=RED, sw=1.2, rx=3)
            s.circle(r[0] + 16, 28, 4.5, fill=RED)
            s.text(r[0] + 30, 28.5, 'EVALUATION - 7 DAYS LEFT - ACTIVATE...', 11, INK)

    def section(s, r, title, sub='', centred=False, badge=None):
        s.rect(r, fill=SECTION, stroke=BOX, sw=1.2, rx=2)
        tw = len(title) * 8.9 + 14
        tx = r[0] + 14 if not centred else (r[0] + r[2]) / 2 - tw / 2 + 2
        ty = r[1] + 22
        if badge:
            s.circle(tx + 9, ty, 10.5, stroke=BOX, sw=1.3, fill=SECTION)
            s.text(tx + 9, ty + 0.5, badge, 12, INK, anchor='middle')
            tx += 28
        s.rect([tx - 2, ty - 11, tx - 2 + tw, ty + 11], fill=SECTION, stroke=BOX, sw=1.2, rx=2)
        s.text(tx + 6, ty, title, 13, INK)
        if sub:
            s.text(tx + tw + 12, ty, sub, 9, '#b9b6ae', bold=False)
        return tx + tw

    def subbox(s, r, caption=None, cap_x=None):
        """a thin inner box round one block of the voice, caption cut into
        its top edge (Polivoks prints its blocks this way)"""
        s.rect(r, stroke=SUBBOX, sw=1, rx=2)
        if caption:
            cw = sum(2.9 if c == ' ' else 7.0 for c in caption) + 6
            x = cap_x if cap_x is not None else r[0] + 10
            s.rect([x - 4, r[1] - 6, x + cw - 4, r[1] + 6], fill=SECTION)
            s.text(x, r[1], caption, 10.5, INK)

    def chevron(s, x, y, left=False):
        d = -1 if left else 1
        s.add(f'<path d="M{x-5*d},{y-10} L{x+4*d},{y} L{x-5*d},{y+10}" fill="none" stroke="{BOX}" '
              f'stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>')

    def bracket(s, x0, x1, y, label):
        """a printed bracket over a group of knobs, label in the gap"""
        lw = len(label) * 6.0 + 14
        m = (x0 + x1) / 2
        s.line(x0, y + 4, x0, y, FAINT, 1)
        s.line(x1, y + 4, x1, y, FAINT, 1)
        s.line(x0, y, m - lw / 2, y, FAINT, 1)
        s.line(m + lw / 2, y, x1, y, FAINT, 1)
        s.text(m, y, label, 9, DIM, anchor='middle')

    def lamp(s, x, y, on=False, label=None, label_side='right', r=4):
        s.circle(x, y, r + 1.5, fill='#000', stroke=BOX, sw=1)
        if on:
            s.circle(x, y, r + 6, fill=LAMP_ON, extra='opacity="0.22" filter="url(#soft)"')
        s.circle(x, y, r, fill=LAMP_ON if on else LAMP_OFF)
        s.circle(x - 1.2, y - 1.2, 1.3, fill='#fff', extra=f'opacity="{0.55 if on else 0.25}"')
        if label:
            if label_side == 'right':
                s.text(x + r + 8, y + 0.5, label, 9.5, INK)
            elif label_side == 'left':
                s.text(x - r - 8, y + 0.5, label, 9.5, INK, anchor='end')
            else:
                s.text(x, y + r + 12, label, 9.5, INK, anchor='middle')

    # ---------------------------------------------------------------- knob
    def knob(s, cx, cy, dia, v, reading, label, bipolar=False, ends=None, label_gap=2, reading_col=INK):
        outer = dia / 2
        body = outer - (6 if dia >= 44 else 5)
        ang = -135 + 270 * max(0.0, min(1.0, v))
        for i in range(11):
            t = -135 + 27 * i
            lg = i in (0, 5, 10)
            x0, y0 = pol(cx, cy, body + 1.5, t)
            x1, y1 = pol(cx, cy, outer - (0 if lg else 1.8), t)
            s.line(x0, y0, x1, y1, '#e9e6dc', 1.3 if lg else 0.9)
        if dia >= 52 or ends:
            lo, hi = ends or (('-', '+') if bipolar else ('0', '10'))
            x, y = pol(cx, cy, outer + 2, -138)
            s.text(x - 2, y + 4, lo, 7.5, '#e9e6dc', anchor='end', bold=False)
            x, y = pol(cx, cy, outer + 2, 138)
            s.text(x + 2, y + 4, hi, 7.5, '#e9e6dc', anchor='start', bold=False)
        teeth = 28 if dia < 64 else 36
        pts = []
        for i in range(teeth * 2):
            t = 360 * i / (teeth * 2)
            rr = body if i % 2 == 0 else body - 1.4
            pts.append('%.2f,%.2f' % pol(cx, cy, rr, t))
        s.add(f'<polygon points="{" ".join(pts)}" fill="#0a0a0a" stroke="#2f2f2f" stroke-width="0.6"/>')
        s.circle(cx, cy, body * 0.78, fill='url(#capgrad)', stroke='#000', sw=0.8)
        x0, y0 = pol(cx, cy, body * 0.15, ang)
        x1, y1 = pol(cx, cy, body - 0.5, ang)
        s.line(x0, y0, x1, y1, INK, 2.4 if dia >= 60 else (2.2 if dia >= 44 else 1.8))
        top = cy + outer + label_gap
        if reading:
            s.text(cx, top + 7, reading, 10, reading_col, anchor='middle', bold=False, family=MONO)
        if label:
            s.text(cx, top + 19.5, label, 9.5, INK, anchor='middle')
        s.geo['knobs'].append(dict(cx=cx, cy=cy, body=body, cap=body * 0.78, outer=outer, label=label))

    # ---------------------------------------------------------------- selector
    def selector(s, cx, cy, r, positions, cur, label=None, label_y=None, lr=None, size=9.5):
        """positions: [(deg, name)], deg from 12 o'clock clockwise"""
        lr = lr or r + 11
        for i, (t, name) in enumerate(positions):
            act = i == cur
            x0, y0 = pol(cx, cy, r + 2.5, t)
            x1, y1 = pol(cx, cy, r + 8, t)
            s.line(x0, y0, x1, y1, RED if act else '#e9e6dc', 2 if act else 1.6)
            lx, ly = pol(cx, cy, lr, t)
            anchor = 'middle' if abs(t) < 10 or abs(abs(t) - 180) < 10 else ('end' if t < 0 else 'start')
            dx = 0 if anchor == 'middle' else (-2 if t < 0 else 2)
            dy = (-8 if abs(t) < 10 else 8) if anchor == 'middle' else (2 if abs(t) < 80 else (0 if abs(t) < 100 else -2))
            s.text(lx + dx, ly + dy, name, size, INK if act else DIM, anchor=anchor)
        ang = positions[cur][0]
        s.circle(cx, cy, r * 0.8, fill='#0b0b0b', stroke='#444', sw=0.8)
        tip = pol(cx, cy, r + 3, ang)
        l = pol(cx, cy, r * 0.5, ang - 90)
        rg = pol(cx, cy, r * 0.5, ang + 90)
        tail = pol(cx, cy, r * 1.0, ang + 180)
        s.add(f'<path d="M{tip[0]:.1f},{tip[1]:.1f} L{rg[0]:.1f},{rg[1]:.1f} Q{tail[0]:.1f},{tail[1]:.1f} '
              f'{l[0]:.1f},{l[1]:.1f} Z" fill="url(#capgrad)" stroke="#000" stroke-width="0.8"/>')
        x0, y0 = pol(cx, cy, 2, ang)
        s.line(x0, y0, tip[0], tip[1], INK, 1.8)
        if label:
            s.text(cx, label_y if label_y is not None else cy + r + 22, label, 9.5, INK, anchor='middle')
        s.geo['selectors'].append(dict(cx=cx, cy=cy, radius=r, label=label))

    # ---------------------------------------------------------------- OLED
    def screen(s, r, trace, main, fill=True, companion=None, companion_col=None, dashed_comp=True,
               caption='', annotation='', legend=('', ''), gate=-1, marker=None, extra=None, ann_col=None,
               inset=6):
        gid = s.uid()
        s.rect([r[0] - 3, r[1] - 3, r[2] + 3, r[3] + 3], fill='#050505', stroke='#3c3c3c', sw=1, rx=4)
        s.rect(r, fill='#000', rx=2)
        dots = []
        for x in range(int(r[0]) + 24, int(r[2]) - 1, 24):
            for y in range(int(r[1]) + 12, int(r[3]) - 1, 12):
                dots.append(f'M{x},{y}h0.01')
        for y in range(int(r[1]) + 24, int(r[3]) - 1, 24):
            for x in range(int(r[0]) + 12, int(r[2]) - 1, 12):
                dots.append(f'M{x},{y}h0.01')
        s.add(f'<path d="{" ".join(dots)}" stroke="#2a2d33" stroke-width="1.2" stroke-linecap="round"/>')
        th = 13
        if caption:
            s.text(r[0] + 8, r[1] + 10, caption, 9.5, SCREEN_TEXT, bold=False, family=MONO)
        if annotation:
            s.text(r[2] - 8, r[1] + 10, annotation, 9.5, ann_col or trace, anchor='end', bold=False, family=MONO)
        if legend[0]:
            s.text(r[0] + 8, r[3] - 9, legend[0], 9.5, trace, bold=False, family=MONO)
        if legend[1]:
            s.text(r[2] - 8, r[3] - 9, legend[1], 9.5, SCREEN_TEXT, anchor='end', bold=False, family=MONO)
        pl, pt, pr, pb = r[0] + inset, r[1] + inset, r[2] - inset, r[3] - inset
        if caption or annotation:
            pt += th
        if legend[0] or legend[1]:
            pb -= th
        box = (pl, pt, pr, pb)

        def pts(data):
            n = len(data)
            return ['%.1f,%.1f' % (pl + (pr - pl) * i / (n - 1), pb - max(0.0, min(1.0, v)) * (pb - pt))
                    for i, v in enumerate(data)]
        if 0 <= gate <= 1:
            x = pl + gate * (pr - pl)
            s.line(x, pt, x, pb, SCREEN_TEXT, 1, extra='stroke-dasharray="3 3"')
        if fill:
            s.add(f'<linearGradient id="{gid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{trace}" '
                  f'stop-opacity="0.38"/><stop offset="1" stop-color="{trace}" stop-opacity="0.02"/></linearGradient>')
        if companion:
            cc = companion_col or trace
            dash = 'stroke-dasharray="4 3"' if dashed_comp else ''
            s.add(f'<polyline points="{" ".join(pts(companion))}" fill="none" stroke="{cc}" stroke-width="1.3" '
                  f'opacity="0.85" {dash}/>')
        if extra:
            extra(s, box, pts)
        if main is not None:
            m = pts(main)
            if fill:
                s.add(f'<polygon points="{pl:.1f},{pb:.1f} {" ".join(m)} {pr:.1f},{pb:.1f}" fill="url(#{gid})"/>')
            s.add(f'<polyline points="{" ".join(m)}" fill="none" stroke="{trace}" stroke-width="1.6" '
                  f'stroke-linejoin="round" filter="url(#bloom)"/>')
        if marker:
            mx = pl + marker[0] * (pr - pl)
            my = pb - max(0.0, min(1.0, marker[1])) * (pb - pt)
            s.circle(mx, my, 4, fill='#000', stroke='#fff', sw=1.6)
        s.add(f'<linearGradient id="{gid}g" x1="0" y1="0" x2="0.35" y2="1"><stop offset="0" stop-color="#fff" '
              f'stop-opacity="0.07"/><stop offset="0.45" stop-color="#fff" stop-opacity="0.015"/>'
              f'<stop offset="0.46" stop-color="#fff" stop-opacity="0"/></linearGradient>')
        s.rect(r, fill=f'url(#{gid}g)', rx=2)
        s.line(r[0] + 3, r[1] + 0.8, r[2] - 3, r[1] + 0.8, '#ffffff', 0.6, extra='opacity="0.12"')
        s.geo['screens'].append(list(r))
        return box, pts

    # ---------------------------------------------------------------- keys
    def step_key(s, x, y, w, h, n, hit=False, play=False, disabled=False, led_y=None, num=True):
        """a black square push-button. HIT: its red lens is lit. PLAYHEAD:
        the white chaser LED above it is lit. DISABLED: no button, a dashed
        hollow (the step has left the cycle)."""
        cx = x + w / 2
        ly = led_y if led_y is not None else y - 9
        if disabled:
            s.rect([x + 0.5, y + 0.5, x + w - 0.5, y + h - 0.5], stroke='#6a6862', sw=1.1, rx=3,
                   extra='stroke-dasharray="3 3"')
            if hit:
                s.rect([x + w * 0.2, y + 6, x + w * 0.8, y + 11], stroke='#7a2a24', sw=1, rx=1.5)
            s.line(cx - 3, ly, cx + 3, ly, FAINT, 1.2)
            if num:
                s.text(cx, y + h + 11, str(n), 9.5, FAINT, anchor='middle')
            return
        s.circle(cx, ly, 2.8, fill='#000', stroke='#3a3a3a', sw=0.8)
        if play:
            s.circle(cx, ly, 7, fill='#fff', extra='opacity="0.25" filter="url(#soft)"')
            s.circle(cx, ly, 2.2, fill='#fffdf4')
        else:
            s.circle(cx, ly, 2.0, fill='#22201c')
        s.rect([x, y, x + w, y + h], fill='#000', rx=3.5)
        s.rect([x + 1.5, y + 1, x + w - 1.5, y + h - 2.5], fill='url(#keygrad)', stroke='#3a3a3a', sw=0.7, rx=3)
        lens = [x + w * 0.2, y + 6, x + w * 0.8, y + 11]
        if hit:
            s.rect([lens[0] - 3, lens[1] - 3, lens[2] + 3, lens[3] + 3], fill=LAMP_ON,
                   extra='opacity="0.35" filter="url(#soft)"', rx=3)
            s.rect(lens, fill=LAMP_ON, rx=1.5)
            s.rect([lens[0] + 1, lens[1] + 0.8, lens[2] - 1, lens[1] + 2], fill='#fff', rx=1, extra='opacity="0.35"')
        else:
            s.rect(lens, fill='#2a0e0c', rx=1.5, stroke='#000', sw=0.5)
        if num:
            s.text(cx, y + h + 11, str(n), 9.5, INK if (hit or play) else DIM, anchor='middle')
        s.geo['keys'].append([x, y, x + w, y + h])

    def push(s, r, label, lamp_on=False, sub=None):
        s.rect(r, fill='#000', rx=4)
        s.rect([r[0] + 1.5, r[1] + 1, r[2] - 1.5, r[3] - 2.5], fill='url(#keygrad)', stroke='#3a3a3a', sw=0.7, rx=3.5)
        cy = (r[1] + r[3]) / 2 - 1
        if lamp_on is None:
            s.text((r[0] + r[2]) / 2, cy + 0.5, label, 10, INK, anchor='middle')
        else:
            s.circle(r[0] + 16, cy, 4.2, fill=LAMP_ON if lamp_on else LAMP_OFF, stroke='#000', sw=0.6)
            s.text(r[0] + 28, cy + 0.5, label, 12, INK)
        if sub:
            s.text((r[0] + r[2]) / 2, r[3] + 12, sub, 8.5, DIM, anchor='middle', bold=False)
        s.geo['keys'].append(list(r))

    # ---------------------------------------------------------------- output
    def svg(s):
        defs = ('<defs>'
                '<radialGradient id="capgrad" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#3a3a3a"/>'
                '<stop offset="0.6" stop-color="#161616"/><stop offset="1" stop-color="#050505"/></radialGradient>'
                '<linearGradient id="keygrad" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2c2c2c"/>'
                '<stop offset="1" stop-color="#121212"/></linearGradient>'
                '<filter id="bloom" x="-5%" y="-20%" width="110%" height="140%"><feGaussianBlur stdDeviation="0.9" result="b"/>'
                '<feComponentTransfer in="b" result="c"><feFuncA type="linear" slope="0.7"/></feComponentTransfer>'
                '<feMerge><feMergeNode in="c"/><feMergeNode in="SourceGraphic"/></feMerge></filter>'
                '<filter id="soft" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="2.2"/></filter>'
                '</defs>')
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{s.w}" height="{s.h}" viewBox="0 0 {s.w} {s.h}">'
                + defs + ''.join(s.parts) + '</svg>')

    def write(s, name, outdir):
        rb = open(__file__.replace('kit.py', 'rbfont.js')).read()
        html = f'''<!doctype html><html><head><meta charset="utf-8"><style>
html,body{{margin:0;padding:0;background:{GROUND}}} svg{{display:block}}
.wm{{position:absolute;left:22px;top:17px}}</style></head><body>
<div style="position:relative;width:{s.w}px;height:{s.h}px">{s.svg()}<div class="wm"></div></div>
<script>{rb}
const wm = document.querySelector('.wm');
const big = (t, col, x) => {{ const a = rbPaths(t, 21, 1.7, 1.2); return {{ a, svg: `<g transform="translate(${{x}},0)"><g fill="none" stroke="${{col}}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">${{a.paths}}</g><g fill="none" stroke="#0c0c0c" stroke-width=".32" stroke-linecap="round" stroke-linejoin="round">${{a.paths}}</g></g>` }}; }};
const p1 = big('FILTER', '#f4f1e8', 0), p2 = big('DRUM', '#f4f1e8', p1.a.width + 10);
wm.innerHTML = `<svg width="${{p1.a.width + 10 + p2.a.width + 4}}" height="34" style="overflow:visible;display:block">${{p1.svg}}${{p2.svg}}</svg>`;
rbConvert();
</script></body></html>'''
        open(f'{outdir}/{name}.html', 'w').write(html)
        json.dump(s.geo, open(f'{outdir}/{name}.geo.json', 'w'))


# ---------------------------------------------------------------- curves
def ms20_db(f, fc, K):
    w = f / fc
    return -10 * math.log10((1 - w * w) ** 2 + ((2 - K) * w) ** 2)


def filter_curve(fc, K, n=160, lo=-30, hi=14):
    out = []
    for i in range(n):
        f = 20 * (1000 ** (i / (n - 1)))
        out.append((max(lo, min(hi, ms20_db(f, min(fc, 21600), K))) - lo) / (hi - lo))
    return out


def freq_x(f):
    return math.log(f / 20) / math.log(1000)


def env_curve(att_ms, rel_ms, span_ms, n=160, scale=1.0):
    out = []
    for i in range(n):
        t = span_ms * i / (n - 1)
        if t < att_ms:
            v = t / att_ms
        else:
            v = math.exp(-(t - att_ms) / (rel_ms / 4.6))
        out.append(v * scale)
    return out


def norm_log(f):
    return math.log(f / 20) / math.log(21600 / 20)
