"""The two drums' settings, as in Designs/FilterDrum-MainDesign.png."""
from kit import *

D = {
    1: dict(noise=(1.0, '100 %'), cutoff=(800, '800 HZ'), res=(0.40, '40% K0.96', 0.96),
            fa=(1.0, '1.0 MS'), fr=(120, '120 MS'), famt=(3.6, '+3.6 OCT'), fvel=(1.0, '100 %'),
            aa=(1.0, '1.0 MS'), ar=(150, '150 MS'), aamt=(1.0, '100 %'), avel=(1.0, '100 %'),
            strike='9.7K', vel='VEL 127: +3.6 OCT / 100%    64: +1.8 OCT / 50%    0: OFF'),
    2: dict(noise=(1.0, '100 %'), cutoff=(2400, '2.40 KHZ'), res=(0.62, '62% K1.49', 1.49),
            fa=(0.5, '0.5 MS'), fr=(45, '45 MS'), famt=(2.1, '+2.1 OCT'), fvel=(1.0, '100 %'),
            aa=(0.5, '0.5 MS'), ar=(60, '60 MS'), aamt=(1.0, '100 %'), avel=(1.0, '100 %'),
            strike='10.3K', vel='VEL 127: +2.1 OCT / 100%    64: +1.0 OCT / 50%    0: OFF'),
}


def tv(ms, lo=0.1, hi=4000):
    import math
    return math.log(ms / lo) / math.log(hi / lo)


def kv(d, key):
    """knob position 0..1 for a setting"""
    v = d[key][0]
    if key == 'cutoff':
        return norm_log(v)
    if key in ('fa', 'aa', 'fr', 'ar'):
        return tv(v)
    if key == 'famt':
        return 0.5 + v / 16
    return v


def filter_screen(p, r, n, caption='', show_legend=False):
    d = D[n]
    fc, K = d['cutoff'][0], d['res'][2]
    strike = min(21600, fc * 2 ** d['famt'][0])
    main = filter_curve(fc, K)
    comp = filter_curve(strike, K)
    import math
    mk = (freq_x(fc), main[int(round(freq_x(fc) * 159))])
    return p.screen(r, TRACE['filter'], main, companion=comp, caption=caption,
                    annotation='PEAK ' + d['strike'], marker=mk,
                    legend=('20 HZ', '20 KHZ') if show_legend else ('', ''))


def env_screen(p, r, n, span=None, caption=None):
    d = D[n]
    span = span or max(d['fr'][0], d['ar'][0]) * 1.25
    vca = env_curve(d['aa'][0], d['ar'][0], span)
    vcf = env_curve(d['fa'][0], d['fr'][0], span, scale=0.92)
    return p.screen(r, TRACE['amp'], vca, companion=vcf, companion_col=TRACE['filter'],
                    caption=caption or ('VCF ' + d['famt'][1]), ann_col=TRACE['amp'],
                    annotation='%d MS' % round(span * 0.8 / 10) * 10 if False else ('%d MS' % (round(d['ar'][0] * 1.07 / 5) * 5)))


STEPS_ON = {1, 5, 9, 13}
PLAY = 7
DISABLED = {15, 16}
