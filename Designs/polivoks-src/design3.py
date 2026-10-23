"""Design 3 - MIRROR COLUMNS. The drums stand side by side as two columns,
the signal flowing DOWN each (screens, NOISE > VCF, VCF envelope, VCA),
with the shared parts in a spine between them.
  * every control sits at the same height in both columns, so drum 1 and
    drum 2 are compared - and matched - by eye across the panel;
  * the D1/D2 mix knob sits between the drums it mixes: turn it left
    toward drum 1, right toward drum 2;
  * the envelope knobs get twice the spacing, the screens a full row;
  * COPY 1 > 2 / 2 > 1 (an editor action, one undo step) for starting a
    second drum from the first;
  * STRIKE lamps in each drum's title, transport in the spine.
"""
import sys
from kit import *
from drums import *

p = Panel()
p.backdrop()


def column(n, x0):
    d = D[n]
    r = [x0, 68, x0 + 432, 504]
    p.section(r, 'DRUM %d' % n, badge=str(n))
    p.lamp(x0 + 360, 90, n == 1, 'STRIKE')
    filter_screen(p, [x0 + 14, 108, x0 + 214, 180], n)
    env_screen(p, [x0 + 224, 108, x0 + 418, 180], n)
    # NOISE > VCF
    p.subbox([x0 + 12, 198, x0 + 420, 294], 'NOISE → VCF - MS-20 LOWPASS')
    p.knob(x0 + 56, 240, 48, kv(d, 'noise'), d['noise'][1], 'NOISE')
    p.chevron(x0 + 101, 240)
    p.knob(x0 + 158, 240, 52, kv(d, 'cutoff'), d['cutoff'][1], 'CUTOFF')
    p.knob(x0 + 262, 240, 52, d['res'][0], d['res'][1], 'RESONANCE')
    p.lamp(x0 + 336, 240, False, 'SELF-OSC')
    # VCF envelope
    p.subbox([x0 + 12, 308, x0 + 420, 388], 'VCF ENVELOPE')
    for i, (k, lab) in enumerate([('fa', 'ATTACK'), ('fr', 'RELEASE'), ('famt', 'AMOUNT'), ('fvel', 'VELOCITY')]):
        p.knob(x0 + 66 + i * 100, 334, 40, kv(d, k), d[k][1], lab, bipolar=(k == 'famt'))
    # VCA
    p.subbox([x0 + 12, 402, x0 + 420, 482], 'VCA')
    for i, (k, lab) in enumerate([('aa', 'ATTACK'), ('ar', 'RELEASE'), ('aamt', 'AMOUNT'), ('avel', 'VELOCITY')]):
        p.knob(x0 + 66 + i * 100, 428, 40, kv(d, k), d[k][1], lab)
    p.text(x0 + 216, 494, d['vel'], 8.5, '#b9b6ae', anchor='middle', bold=False)


column(1, 16)
column(2, 752)
p.chevron(454, 442)
p.chevron(746, 442, left=True)

# spine
r = [460, 68, 740, 504]
p.section(r, 'MIX', centred=True)
cx = 600
p.text(512, 120, '‹ DRUM 1', 8.5, DIM, anchor='middle', bold=False)
p.text(688, 120, 'DRUM 2 ›', 8.5, DIM, anchor='middle', bold=False)
p.knob(cx, 170, 92, 0.5, '50 %', 'D1 / D2', ends=('D1', 'D2'))
p.text(cx, 254, '.71  /  .71   CONSTANT POWER', 8.5, DIM, anchor='middle', bold=False)
p.knob(cx, 306, 56, 0.75, '+0.0 DB', 'OUTPUT', bipolar=True)
p.line(476, 370, 724, 370, '#3a3a3a', 1)
p.push([484, 392, 570, 426], 'RUN', lamp_on=True)
p.selector(658, 412, 16, [(-100, 'BAR'), (-50, '1/2'), (0, '1/4'), (50, '1/8'), (100, '1/16')], 0,
           'LAUNCH ON', label_y=448, lr=27, size=8.5)
p.line(476, 462, 724, 462, '#3a3a3a', 1)
p.push([484, 470, 594, 494], 'COPY 1 → 2', lamp_on=None)
p.push([606, 470, 716, 494], 'COPY 2 → 1', lamp_on=None)

# sequencer
r = [16, 516, 1184, 644]
p.section(r, 'SEQUENCER', '16 STEPS = ONE BAR OF 1/16THS  /  STRIKES BOTH DRUMS  /  MIDI STILL TRIGGERS')
p.text(1170, 538, 'RIGHT-CLICK A STEP TO SKIP IT  /  14 STEPS = 7/8', 8.5, DIM, anchor='end', bold=False)
kw, kh, gap, ggap = 58, 42, 8, 26
x = 49
for i in range(1, 17):
    p.step_key(x, 570, kw, kh, i, hit=i in STEPS_ON, play=i == PLAY, disabled=i in DISABLED, led_y=559)
    x += kw + gap
    if i % 4 == 0:
        x += ggap - gap

p.write(sys.argv[2] if len(sys.argv) > 2 else 'FilterDrum-Polivoks-3-MirrorColumns', sys.argv[1])
