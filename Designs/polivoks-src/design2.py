"""Design 2 - FOCUS. One drum is edited at a time on a bigger editor; both
drums stay visible as cards on the left.
  * EDIT selector (1 / 2) - or click a card - picks the drum the editor
    shows. The cards keep a live mini envelope, the key readings and the
    STRIKE lamp of both drums, so nothing goes out of sight;
  * the editor's two OLEDs are three times the size, with the axes and a
    key (VCA solid / VCF dashed) printed on the glass;
  * the velocity table becomes its own OLED readout line;
  * transport (RUN, LAUNCH ON) moves up beside MIX, so the step keys get
    the full width and grow to 58 x 48.
UI state only: no new parameters."""
import sys
from kit import *
from drums import *

p = Panel()
p.backdrop()

# --- DRUMS: edit selector + two cards -----------------------------------
r = [16, 68, 226, 490]
p.section(r, 'DRUMS')
p.selector(180, 104, 17, [(-55, '1'), (55, '2')], 0, None, lr=28, size=11)
p.text(140, 104, 'EDIT', 10.5, INK, anchor='end')


def card(n, top, sel):
    d = D[n]
    rr = [28, top, 214, top + 140]
    p.rect(rr, fill='#101010' if sel else SECTION, stroke=RED if sel else SUBBOX, sw=1.6 if sel else 1, rx=3)
    p.circle(46, top + 18, 9.5, stroke=BOX, sw=1.2, fill=SECTION)
    p.text(46, top + 18.5, str(n), 11, INK, anchor='middle')
    p.text(64, top + 18, 'DRUM %d' % n, 11, INK)
    if sel:
        p.text(204, top + 18, 'EDITING', 8.5, RED, anchor='end')
    env_mini = env_curve(d['aa'][0], d['ar'][0], 200)
    vcf_mini = env_curve(d['fa'][0], d['fr'][0], 200, scale=0.92)
    p.screen([40, top + 34, 202, top + 88], TRACE['amp'], env_mini, companion=vcf_mini,
             companion_col=TRACE['filter'], inset=5)
    p.text(40, top + 104, d['cutoff'][1] + '  ' + d['res'][1].split(' ')[1], 9, INK, bold=False, family=MONO)
    p.text(40, top + 120, '%s / %s' % (d['fr'][1].split()[0], d['ar'][1]), 9, DIM, bold=False, family=MONO)
    p.lamp(196, top + 112, n == 1, None)
    p.text(196, top + 128, 'STRIKE', 7.5, DIM, anchor='middle', bold=False)


card(1, 142, True)
card(2, 318, False)

# --- EDITOR -------------------------------------------------------------
n = 1
d = D[n]
r = [238, 68, 986, 490]
p.section(r, 'DRUM 1', 'TURN EDIT OR CLICK A CARD TO SWITCH DRUMS', badge='1')
box, pts = filter_screen(p, [254, 104, 610, 236], 1, caption='AT THE KNOBS', show_legend=True)
span = 200
vca = env_curve(d['aa'][0], d['ar'][0], span)
vcf = env_curve(d['fa'][0], d['fr'][0], span, scale=0.92)
p.screen([622, 104, 970, 236], TRACE['amp'], vca, companion=vcf, companion_col=TRACE['filter'],
         caption='ENVELOPES', annotation='VCA 160 MS', legend=('VCA ——', '- - - VCF +3.6 OCT'), gate=-1)

bt, bb = 260, 420
p.subbox([254, bt, 326, bb], 'NOISE')
p.knob(290, 326, 52, kv(d, 'noise'), d['noise'][1], 'LEVEL')
p.chevron(332, 326)
p.subbox([338, bt, 752, bb], 'VCF - MS-20 LOWPASS')
p.lamp(668, bt + 18, False, 'SELF-OSC')
p.knob(388, 330, 68, kv(d, 'cutoff'), d['cutoff'][1], 'CUTOFF')
p.knob(474, 330, 68, d['res'][0], d['res'][1], 'RESONANCE')
p.bracket(522, 740, 300, 'ENVELOPE')
for i, (k, lab) in enumerate([('fa', 'ATTACK'), ('fr', 'RELEASE'), ('famt', 'AMOUNT'), ('fvel', 'VELOCITY')]):
    p.knob(553 + i * 56, 342, 44, kv(d, k), d[k][1], lab, bipolar=(k == 'famt'))
p.chevron(758, 326)
p.subbox([764, bt, 970, bb], 'VCA')
for i, (k, lab) in enumerate([('aa', 'ATTACK'), ('ar', 'RELEASE'), ('aamt', 'AMOUNT'), ('avel', 'VELOCITY')]):
    p.knob(792 + i * 49, 342, 40, kv(d, k), d[k][1], lab)

# velocity readout: a one-line OLED


def velline(s, b, pts):
    pass


rr = [254, 436, 970, 474]
p.screen(rr, TRACE['filter'], None, fill=False, inset=4)
p.text(266, 455, 'VELOCITY', 9.5, SCREEN_TEXT, bold=False, family=MONO)
cols = [('127', '+3.6 OCT', '100 %'), ('64', '+1.8 OCT', '50 %'), ('0', 'OFF', 'OFF')]
for i, (v, f, a) in enumerate(cols):
    x = 380 + i * 196
    p.text(x, 455, v, 9.5, INK, bold=False, family=MONO)
    p.text(x + 36, 455, 'VCF ' + f, 9.5, TRACE['filter'], bold=False, family=MONO)
    p.text(x + 122, 455, 'VCA ' + a, 9.5, TRACE['amp'], bold=False, family=MONO)

# --- MIX + TRANSPORT ------------------------------------------------------
r = [998, 68, 1184, 490]
p.section(r, 'MIX', centred=True)
cx = 1091
p.knob(cx, 144, 70, 0.5, '50 %', 'D1 / D2', ends=('D1', 'D2'))
p.text(cx, 214, '.71  /  .71  CONSTANT POWER', 8.5, DIM, anchor='middle', bold=False)
p.knob(cx, 268, 52, 0.75, '+0.0 DB', 'OUTPUT', bipolar=True)
p.line(1012, 334, 1170, 334, '#3a3a3a', 1)
p.text(cx, 350, 'TRANSPORT', 10.5, INK, anchor='middle')
p.push([1040, 364, 1142, 398], 'RUN', lamp_on=True)
p.selector(cx, 442, 16, [(-100, 'BAR'), (-50, '1/2'), (0, '1/4'), (50, '1/8'), (100, '1/16')], 0,
           'LAUNCH ON', label_y=478, lr=27, size=8.5)

# --- SEQUENCER --------------------------------------------------------------
r = [16, 502, 1184, 644]
p.section(r, 'SEQUENCER', '16 STEPS = ONE BAR OF 1/16THS  /  STRIKES BOTH DRUMS  /  MIDI STILL TRIGGERS')
kw, kh, gap, ggap = 58, 46, 8, 26
x = 49
for i in range(1, 17):
    p.step_key(x, 558, kw, kh, i, hit=i in STEPS_ON, play=i == PLAY, disabled=i in DISABLED, led_y=547)
    x += kw + gap
    if i % 4 == 0:
        x += ggap - gap
p.text(1170, 524, 'RIGHT-CLICK A STEP TO SKIP IT  /  14 STEPS = 7/8', 8.5, DIM, anchor='end', bold=False)

p.write(sys.argv[2] if len(sys.argv) > 2 else 'FilterDrum-Polivoks-2-Focus', sys.argv[1])
