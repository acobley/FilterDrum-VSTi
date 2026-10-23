"""Design 1 - TWIN LANES, black panel. The Design 2.0 layout kept, redressed
as R2Synth 1.1. Usability changes are small and local:
  * each lane's blocks (NOISE / VCF / VCA) get their own printed box, and
    the VCF envelope a bracket, so the eight look-alike small knobs read
    as two groups of four;
  * LAUNCH ON becomes a five-position rotary (one turn, no tiny segments);
  * step keys light a red lens for a hit and a white chaser LED for the
    playhead, so the two never look alike; a disabled step is an empty
    dashed outline; the right-click gesture is printed on the panel.
"""
import sys
from kit import *
from drums import *

p = Panel()
p.backdrop()


def lane(n, top):
    d = D[n]
    r = [16, top, 986, top + 206]
    p.section(r, 'DRUM %d' % n, badge=str(n))
    p.text(972, top + 22, d['vel'], 9, '#b9b6ae', anchor='end', bold=False)
    bt, bb = top + 46, top + 196
    # NOISE
    p.subbox([28, bt, 116, bb], 'NOISE')
    p.knob(72, top + 104, 56, kv(d, 'noise'), d['noise'][1], 'LEVEL')
    p.chevron(123, top + 104)
    # VCF
    p.subbox([130, bt, 578, bb], 'VCF - MS-20 LOWPASS')
    p.lamp(492, bt + 18, False, 'SELF-OSC')
    p.knob(182, top + 104, 60, kv(d, 'cutoff'), d['cutoff'][1], 'CUTOFF')
    p.knob(270, top + 104, 60, d['res'][0], d['res'][1], 'RESONANCE')
    p.bracket(330, 566, top + 80, 'ENVELOPE')
    for i, (k, lab) in enumerate([('fa', 'ATTACK'), ('fr', 'RELEASE'), ('famt', 'AMOUNT'), ('fvel', 'VELOCITY')]):
        p.knob(360 + i * 58, top + 116, 40, kv(d, k), d[k][1], lab, bipolar=(k == 'famt'))
    p.chevron(585, top + 104)
    # VCA
    p.subbox([592, bt, 818, bb], 'VCA')
    for i, (k, lab) in enumerate([('aa', 'ATTACK'), ('ar', 'RELEASE'), ('aamt', 'AMOUNT'), ('avel', 'VELOCITY')]):
        p.knob(628 + i * 54, top + 116, 40, kv(d, k), d[k][1], lab)
    # screens
    filter_screen(p, [832, top + 42, 972, top + 114], n)
    env_screen(p, [832, top + 124, 972, top + 194], n)


lane(1, 68)
lane(2, 284)

# MIX column
r = [998, 68, 1184, 490]
p.section(r, 'MIX', centred=True)
cx = 1091
p.knob(cx, 156, 76, 0.5, '50 %', 'D1 / D2', ends=('D1', 'D2'))
p.text(cx, 230, '.71  /  .71', 9, DIM, anchor='middle', bold=False, family=MONO)
p.text(cx, 244, 'CONSTANT POWER', 8.5, DIM, anchor='middle', bold=False)
p.line(1012, 262, 1170, 262, '#3a3a3a', 1)
p.knob(cx, 316, 56, 0.75, '+0.0 DB', 'OUTPUT', bipolar=True)
p.line(1012, 384, 1170, 384, '#3a3a3a', 1)
p.text(cx, 404, 'STRIKE', 10.5, INK, anchor='middle')
p.lamp(1056, 432, True, 'D1')
p.lamp(1112, 432, False, 'D2')
p.text(cx, 462, 'VEL 127', 9, DIM, anchor='middle', bold=False, family=MONO)

# SEQUENCER
r = [16, 502, 1184, 644]
e = p.section(r, 'SEQUENCER', '16 STEPS = ONE BAR OF 1/16THS  /  STRIKES BOTH DRUMS  /  MIDI STILL TRIGGERS')
kw, kh, gap, ggap = 42, 40, 7, 20
x = 34
for i in range(1, 17):
    p.step_key(x, 558, kw, kh, i, hit=i in STEPS_ON, play=i == PLAY, disabled=i in DISABLED, led_y=548)
    x += kw + gap
    if i % 4 == 0:
        x += ggap - gap
p.text(34, 630, 'RIGHT-CLICK A STEP TO TAKE IT OUT OF THE CYCLE - 14 STEPS = 7/8', 8.5, DIM, bold=False)
p.line(882, 520, 882, 630, '#3a3a3a', 1)
p.push([900, 556, 980, 596], 'RUN', lamp_on=True, sub='HOST TRANSPORT')
p.selector(1090, 582, 19, [(-100, 'BAR'), (-50, '1/2'), (0, '1/4'), (50, '1/8'), (100, '1/16')], 0,
           'LAUNCH ON', label_y=630, lr=31)

p.write(sys.argv[2] if len(sys.argv) > 2 else 'FilterDrum-Polivoks-1-TwinLanes', sys.argv[1])
