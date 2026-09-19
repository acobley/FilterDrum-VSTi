# -*- coding: utf-8 -*-
"""Rebuild docs/signal-path.html for the current FilterDrum.

    python3 tools/make-signal-path.py

It rewrites the <body> of docs/signal-path.html and leaves the <head>
alone, so the palette and the theme handling stay where they were
written. Render the PNG from the result with the headless-Chromium
command in docs/README.md.


The two drum blocks are emitted from ONE function, because they are one
voice implementation and two instances of it - drawing them twice by
hand is the way a diagram ends up claiming a difference the code does
not have."""
import io, os, re

# The repo's own docs/, found from this script rather than from a path
# that only exists on one machine.
DOCS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs')
src = io.open(os.path.join(DOCS, 'signal-path.html'), encoding='utf-8').read()
head = src[:src.index('<body>') + len('<body>')]

W, H = 1180, 680
DY = 292                      # drum 2 is drum 1, this far down

def esc(t):
    return t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def box(x, y, w, h, kind='box'):
    return '<rect class="%s" x="%g" y="%g" width="%g" height="%g" rx="3"/>' % (kind, x, y, w, h)

def name(cx, y, t):
    return '<text class="name" x="%g" y="%g" text-anchor="middle">%s</text>' % (cx, y, t)

def sub(cx, y, t):
    return '<text class="sub" x="%g" y="%g" text-anchor="middle">%s</text>' % (cx, y, t)

def sig(pts, arrow=True):
    return '<polyline class="sig" points="%s"%s/>' % (
        ' '.join('%g,%g' % p for p in pts), ' marker-end="url(#as)"' if arrow else '')

def mod(pts, arrow=True):
    return '<polyline class="mod" points="%s"%s/>' % (
        ' '.join('%g,%g' % p for p in pts), ' marker-end="url(#am)"' if arrow else '')

def warn(pts, arrow=True):
    return '<polyline class="warnl" points="%s"%s/>' % (
        ' '.join('%g,%g' % p for p in pts), ' marker-end="url(#aw)"' if arrow else '')

def label(x, y, t, cls='modt', anchor='start'):
    return '<text class="%s" x="%g" y="%g" text-anchor="%s">%s</text>' % (cls, x, y, anchor, t)


def drum(n, dy, seed):
    """One voice. Every y is dy + drum 1's y, so the two cannot diverge."""
    o = []
    a = lambda y: y + dy                      # noqa: E731

    o.append(box(386, a(56), 630, 266, 'boxq'))
    o.append(label(402, a(78), 'DRUM %d' % n, 'warnt'))
    o.append(label(470, a(78), 'noise seed %s' % seed, 'sub'))

    # --- the audio line, left to right -----------------------------------
    o.append(box(402, a(92), 118, 48))
    o.append(name(461, a(116), 'NOISE'))
    o.append(sub(461, a(132), 'xorshift32'))

    o.append(sig([(520, a(116)), (550, a(116))]))

    o.append(box(550, a(92), 106, 48))
    o.append(name(603, a(116), '&#215; LEVEL'))
    o.append(sub(603, a(132), '0 &ndash; 100 %'))

    o.append(sig([(656, a(116)), (686, a(116))]))

    o.append(box(686, a(86), 160, 60))
    o.append(name(766, a(110), 'MS-20 LOWPASS'))
    o.append(sub(766, a(126), 'TPT SVF, K = 0 &ndash; 2.4'))
    o.append(sub(766, a(140), 'self-osc at K &ge; 2'))

    o.append(sig([(846, a(116)), (876, a(116))]))

    o.append(box(876, a(92), 124, 48))
    o.append(name(938, a(116), '&#215; VCA'))
    o.append(sub(938, a(132), '&#215; 0.4 headroom'))

    # --- the two envelopes, under the stages they drive -------------------
    o.append(box(686, a(196), 160, 64, 'boxw'))
    o.append(name(766, a(222), 'VCF &mdash; AR'))
    o.append(sub(766, a(240), 'amount &plusmn;6 oct &#215; vel'))

    o.append(box(876, a(196), 124, 64, 'boxq'))
    o.append(name(938, a(222), 'VCA &mdash; AR'))
    o.append(sub(938, a(240), 'level &#215; vel'))

    o.append(mod([(766, a(196)), (766, a(146))]))
    o.append(label(772, a(174), 'cutoff, per sample'))

    o.append(mod([(938, a(196)), (938, a(140))]))
    o.append(label(944, a(170), 'gain'))

    # --- the trigger bus along the bottom ---------------------------------
    o.append(mod([(372, a(300)), (938, a(300))], arrow=False))
    o.append(mod([(766, a(300)), (766, a(260))]))
    o.append(mod([(938, a(300)), (938, a(260))]))
    o.append(label(402, a(290), 'trigger &#215; velocity'))

    return o


parts = []
parts.append('<svg class="dg" viewBox="0 0 %d %d" role="img"' % (W, H))
parts.append(' aria-label="FilterDrum routing. A MIDI note-on and a sixteen-step '
             'sequencer driven by the host transport both feed one trigger queue, '
             'sorted by sample offset. The queue strikes two identical drum voices, '
             'each of which takes noise scaled by a Noise Level control into an MS-20 '
             'lowpass and a VCA, with an exponential AR envelope on each. '
             'The two voices are summed by a '
             'constant-power crossfader, then the output trim. The VCF envelope in each '
             'voice freezes when that voice&#39;s VCA envelope goes idle.">')
parts.append('<defs>')
for mid, cls in (('as', 'ah-sig'), ('am', 'ah-mod'), ('aw', 'ah-wrn')):
    parts.append('<marker id="%s" viewBox="0 0 10 10" refX="9" refY="5" '
                 'markerWidth="6" markerHeight="6" orient="auto-start-reverse">'
                 '<path class="%s" d="M 0 0 L 10 5 L 0 10 z"/></marker>' % (mid, cls))
parts.append('</defs>')

# ---- what starts a hit ---------------------------------------------------
parts.append(label(16, 20, 'WHAT STARTS A HIT', 'warnt'))

parts.append(box(16, 30, 150, 48, 'boxq'))
parts.append(name(91, 54, 'HOST TRANSPORT'))
parts.append(sub(91, 70, 'tempo, ppq, sig'))

parts.append(mod([(91, 78), (91, 96)]))

parts.append(box(16, 96, 150, 48, 'boxq'))
parts.append(name(91, 120, 'BAR CLOCK'))
parts.append(sub(91, 136, 'grid = 1/16 note'))

parts.append(mod([(91, 144), (91, 162)]))

parts.append(box(16, 162, 150, 64, 'boxq'))
parts.append(name(91, 184, '16 STEPS'))
parts.append(sub(91, 200, 'launch: bar &hellip; 1/16'))
parts.append(sub(91, 215, 'velocity always 1.0'))

parts.append(box(16, 250, 150, 56, 'boxq'))
parts.append(name(91, 274, 'MIDI NOTE-ON'))
parts.append(sub(91, 290, 'velocity 0 &ndash; 1'))

parts.append(mod([(166, 194), (188, 194), (188, 230), (206, 230)]))
parts.append(mod([(166, 278), (188, 278), (188, 230), (206, 230)]))

parts.append(box(206, 200, 152, 60))
parts.append(name(282, 224, 'TRIGGER QUEUE'))
parts.append(sub(282, 240, '&le; 64 per block'))
parts.append(sub(282, 255, 'sorted by offset'))

# the spine that feeds both drums
parts.append(mod([(358, 230), (372, 230), (372, 300 + DY)], arrow=False))

parts.extend(drum(1, 0, '0x9E3779B9'))
parts.extend(drum(2, DY, '0x7F4A7C15'))

# ---- the defect, marked where it happens ---------------------------------
parts.append(label(402, 224, 'VCA idle stops the loop:', 'warnt'))
parts.append(label(402, 239, 'the VCF AR freezes. Both drums.', 'warnt'))
parts.append(warn([(604, 234), (680, 234)]))

# ---- summing and output --------------------------------------------------
CY = 116 + DY // 2            # midway between the two audio rows
parts.append(sig([(1000, 116), (1028, 116), (1028, CY), (1052, CY)]))
parts.append(sig([(1000, 116 + DY), (1028, 116 + DY), (1028, CY), (1052, CY)]))

parts.append(box(1052, CY - 32, 112, 64))
parts.append(name(1108, CY - 8, 'CROSSFADER'))
parts.append(sub(1108, CY + 8, 'constant power'))
parts.append(sub(1108, CY + 23, 'g1&#178; + g2&#178; = 1'))

parts.append(sig([(1108, CY + 32), (1108, CY + 72)]))

parts.append(box(1052, CY + 72, 112, 56))
parts.append(name(1108, CY + 96, 'OUTPUT TRIM'))
parts.append(sub(1108, CY + 112, '&plusmn;12 dB, 20 ms'))

parts.append(sig([(1108, CY + 128), (1108, CY + 168)]))

parts.append(box(1052, CY + 168, 112, 48, 'boxq'))
parts.append(name(1108, CY + 192, 'OUT L = R'))
parts.append(sub(1108, CY + 208, 'mono, both buses'))

# ---- legend --------------------------------------------------------------
LY = 56 + DY + 266 + 30       # clear of drum 2's box, whatever DY is
parts.append('<line class="sig" x1="16" y1="%d" x2="52" y2="%d"/>' % (LY, LY))
parts.append(label(58, LY + 4, 'audio', 'sigt'))
parts.append('<line class="mod" x1="120" y1="%d" x2="156" y2="%d"/>' % (LY, LY))
parts.append(label(162, LY + 4, 'control and modulation', 'modt'))
parts.append('<line class="warnl" x1="320" y1="%d" x2="356" y2="%d"/>' % (LY, LY))
parts.append(label(362, LY + 4, 'known defect', 'warnt'))

parts.append('</svg>')
svg = '\n        '.join(parts)

body = u'''

<div class="wrap">

  <header>
    <div class="eyebrow">FilterDrum &middot; two monophonic MS-20 drum voices</div>
    <h1>Routing</h1>
    <p class="lede">
      Two drum voices struck by one trigger queue and blended by a crossfader, drawn as the
      code actually runs it rather than from memory. <strong>There is one voice
      implementation and two instances of it</strong> &mdash; the blocks below are emitted
      from one function in the drawing script for the same reason the panel lays out both
      rows from one function: so a difference has to be real to appear.
    </p>
  </header>

  <figure>
    <div class="scroll">
      %(svg)s
    </div>
    <figcaption>
      Every stage is transcribed from <code>DrumVoice::render</code>,
      <code>FilterDrumDsp::renderVoices</code> and the trigger-collection block of
      <code>FilterDrumProcessor::process</code>. Blue is audio, green is control and
      modulation, red marks the one known defect.
    </figcaption>
  </figure>

  <hr>

  <section>
    <h2>Four things the picture says that the prose has to work at</h2>
    <ul>
      <li>
        <strong>Two trigger sources meet in one queue, and only one of them carries
        velocity.</strong> A sequenced step always fires at 1.0, so the four Velocity
        sensitivity knobs respond to MIDI and to nothing else. Both sources carry a
        <em>sample offset</em>, and the queue is sorted by it before anything is rendered
        &mdash; firing everything at offset 0 would quantise every hit to the block size,
        which is 11&nbsp;ms at 44.1&nbsp;k and audible swing on a sixteenth.
      </li>
      <li>
        <strong>The sequencer has no clock of its own.</strong>
        <code>FilterDrumTransport</code> is lifted whole from <code>Project6-VSTi</code> and
        its grid runs at sixteenths, so one grid line <em>is</em> one step &mdash; the step
        index that comes back from <code>gridLinesInBlock</code> is the switch to look up.
        Changing that grid from eights to sixteenths is the only substantive edit to the
        lifted file.
      </li>
      <li>
        <strong>Both envelopes are exponential, and the release is defined to
        &minus;60&nbsp;dB.</strong> It runs on to &minus;100&nbsp;dB before it will call
        itself finished, which is 5/3 of the knob &mdash; inaudible, but
        <code>getTailSamples</code> has to convert or it tells the host the plug-in has
        finished while a voice is still running.
      </li>
      <li>
        <strong>The noise is each drum&rsquo;s only excitation</strong>, so the bottom of a
        Noise Level knob is an off switch rather than a pure-tone setting: a linear filter
        fed exact zero from a zero state stays at exact zero however high the resonance.
      </li>
      <li>
        <strong>The two voices have different noise seeds</strong>, and that is what makes
        them a layer. Two generators started from one seed produce the identical sequence,
        so the pair would be one drum 6&nbsp;dB louder &mdash; it would measure fine
        everywhere and sound like one drum. Measured correlation of two identically-set
        drums: &minus;0.16.
      </li>
    </ul>
  </section>

  <hr>

  <section>
    <h2>What updates when</h2>
    <p>
      Most questions about why two hits differ come down to this table. Three different
      clocks are running, and a value read on the wrong one is the usual cause.
    </p>
    <table>
      <thead>
        <tr><th>Quantity</th><th>Updated</th><th>Consequence</th></tr>
      </thead>
      <tbody>
        <tr>
          <td>Filter cutoff</td>
          <td class="when sample">every sample</td>
          <td>The envelope sweep is smooth; a per-block cutoff would zipper on a fast kick.</td>
        </tr>
        <tr>
          <td>Both AR envelopes</td>
          <td class="when sample">every sample&hellip;</td>
          <td>&hellip;but only while the VCA envelope is not idle. This is the gate that freezes the VCF envelope.</td>
        </tr>
        <tr>
          <td>Output trim<br>Crossfade gains</td>
          <td class="when sample">every sample</td>
          <td>Both smoothed over 20&nbsp;ms. A stepped <em>gain</em> is a step in the waveform, which clicks &mdash; unlike a stepped cutoff, which measured inaudible.</td>
        </tr>
        <tr>
          <td>VCF octaves<br>VCA gain</td>
          <td class="when note">latched at trigger</td>
          <td>Velocity is a property of the hit. A knob moved during a decay cannot change a note already sounding.</td>
        </tr>
        <tr>
          <td>Noise Level<br>Cutoff, Resonance</td>
          <td class="when block">once per block</td>
          <td>Marked <code>smoothed</code> in the table but nothing smooths them. Measured as inaudible: a TPT filter changes coefficients without a discontinuity in its state.</td>
        </tr>
        <tr>
          <td>Playhead lamp</td>
          <td class="when block">once per block, on change</td>
          <td>Published through <code>outputParameterChanges</code>, because a <code>sendMessage</code> from <code>process()</code> is silently discarded.</td>
        </tr>
      </tbody>
    </table>
  </section>

  <div class="flag">
    <div class="h">the one known defect</div>
    <p>
      <code>DrumVoice::render</code> returns early while <code>active()</code> is false, and
      <code>active()</code> is <code>!mVcaEnv.idle()</code> &mdash; but both envelopes are
      advanced inside the loop it returns from. A VCF release longer than the VCA&rsquo;s
      therefore never completes: it freezes mid-release, and the next hit starts its attack
      from that frozen level instead of from zero.
    </p>
    <p>
      <strong>Its size was overstated once, and this corrects it.</strong> Measured through
      the old trigger ping it read 1.27 to 6.60&nbsp;dB, because the ping landed on the one
      sample where the difference exists. With the ping gone and the noise driving the
      filter it is <span class="mono">0.4&nbsp;dB</span>, flat across every window from
      2&nbsp;ms to 250&nbsp;ms. Worth fixing as an inconsistency with no upside; not
      something anybody would hear.
    </p>
    <p>
      The same frozen-state behaviour has a second consequence now that the noise is the
      only excitation: once a drum&rsquo;s oscillation has been started, turning its Noise
      Level to 0 leaves it running for the rest of the session. It only falls silent when
      the project is reloaded with the knob already down &mdash; so the silence is
      intermittent rather than honest, and reads as a loading bug.
    </p>
  </div>

</div>
</body>
</html>
''' % {'svg': svg}

# one extra class for the box the defect sits on
head = head.replace(
    '  .dg .boxq   { fill: var(--sunk);    stroke: var(--rule);   stroke-width: 1.2; }',
    '  .dg .boxq   { fill: var(--sunk);    stroke: var(--rule);   stroke-width: 1.2; }\n'
    '  .dg .boxw   { fill: var(--warn-bg); stroke: var(--warn);   stroke-width: 1.4; }')

head = head.replace('<title>FilterDrum - signal path</title>',
                    '<title>FilterDrum - routing</title>')

io.open(os.path.join(DOCS, 'signal-path.html'), 'w', encoding='utf-8').write(head + body)
print('wrote signal-path.html')
