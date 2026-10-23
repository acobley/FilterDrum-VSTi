#!/usr/bin/env python3
"""Studio Worn wear (R2Synth 1.1's shipped look, from R2Synth-VSTi/Designs/
render-weathered.py) applied to a FilterDrum mock-up, using the geometry the
mock-up recorded (name.geo.json). usage: weather.py clean.png geo.json out.png"""
import json, math, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi
S = 2
src, geo, outp = sys.argv[1:4]
G = json.load(open(geo))
BASE = np.asarray(Image.open(src).convert('RGB')).astype(np.float32) / 255.0
H, W = BASE.shape[:2]
YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)
KNOBS = [dict(cx=k['cx']*S, cy=k['cy']*S, body=k['body']*S, cap=k['cap']*S, outer=k['outer']*S, label=k['label']) for k in G['knobs']]
SELS = [dict(cx=s['cx']*S, cy=s['cy']*S, r=s['radius']*S) for s in G['selectors']]
SCREENS = [[v*S for v in r] for r in G['screens']]
KEYS = [[v*S for v in r] for r in G['keys']]
def disc(cx, cy, r, soft=1.0):
    d = np.hypot(XX - cx, YY - cy)
    return np.clip((r - d) / soft + 0.5, 0, 1)
def rectmask(r, pad=0):
    m = np.zeros((H, W), np.float32)
    m[int(r[1] - pad):int(r[3] + pad), int(r[0] - pad):int(r[2] + pad)] = 1
    return m
M_KNOB = np.zeros((H, W), np.float32)
for k in KNOBS: M_KNOB = np.maximum(M_KNOB, disc(k['cx'], k['cy'], k['body'] + 1))
M_SEL = np.zeros((H, W), np.float32)
for s in SELS: M_SEL = np.maximum(M_SEL, disc(s['cx'], s['cy'], s['r'] + 7))
M_KEY = np.zeros((H, W), np.float32)
for r in KEYS: M_KEY = np.maximum(M_KEY, rectmask(r, 0))
M_SCREEN = np.zeros((H, W), np.float32)
for r in SCREENS: M_SCREEN = np.maximum(M_SCREEN, rectmask(r, 0))
M_BEZEL = np.zeros((H, W), np.float32)
for r in SCREENS: M_BEZEL = np.maximum(M_BEZEL, rectmask(r, 7))
M_PANEL = np.clip(1 - np.maximum(np.maximum(np.maximum(M_KNOB, M_SEL), M_BEZEL), M_KEY), 0, 1)
# ---------------------------------------------------------------------------
# noise helpers
# ---------------------------------------------------------------------------
def noise(rng, sigma, octaves=1):
    acc = np.zeros((H, W), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = ndi.gaussian_filter(rng.standard_normal((H, W)).astype(np.float32), sigma / (2 ** o))
        n /= (n.std() + 1e-6)
        acc += amp * n
        tot += amp
        amp *= 0.5
    return acc / tot


def norm01(a):
    lo, hi = np.percentile(a, 1), np.percentile(a, 99)
    return np.clip((a - lo) / (hi - lo + 1e-6), 0, 1)


def over(img, colour, alpha):
    """composite a flat colour with a per-pixel alpha"""
    a = np.clip(alpha, 0, 1)[..., None]
    return img * (1 - a) + np.asarray(colour, np.float32)[None, None, :] / 255.0 * a


def luminance(img):
    return img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114


# ---------------------------------------------------------------------------
# strokes drawn on a high-res mask
# ---------------------------------------------------------------------------
def stroke_layer(paths):
    """paths: list of (points, width, alpha). Returns float mask HxW."""
    ss = 2
    im = Image.new('L', (W * ss, H * ss), 0)
    d = ImageDraw.Draw(im)
    for pts, width, alpha in paths:
        p = [(x * ss, y * ss) for x, y in pts]
        n = len(p)
        for i in range(n - 1):
            # taper the ends
            t = i / max(1, n - 2)
            taper = math.sin(math.pi * min(1, max(0, t))) ** 0.5
            a = int(255 * alpha * (0.35 + 0.65 * taper))
            w = max(1, int(round(width * ss * (0.4 + 0.6 * taper))))
            d.line([p[i], p[i + 1]], fill=a, width=w)
    im = im.resize((W, H), Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def scratch_path(rng, x, y, ang, length, bend=0.0, jitter=0.15, step=6):
    pts = [(x, y)]
    a = ang
    for _ in range(int(length / step)):
        a += bend + rng.normal(0, jitter) * 0.05
        x += math.cos(a) * step
        y += math.sin(a) * step
        pts.append((x, y))
    return pts


def scratch_cluster(rng, n, cx, cy, spread, ang, len_rng, width_rng, alpha_rng, bend=0.002):
    out = []
    for _ in range(n):
        x = cx + rng.normal(0, spread)
        y = cy + rng.normal(0, spread * 0.6)
        a = ang + rng.normal(0, 0.08)
        out.append((scratch_path(rng, x, y, a, rng.uniform(*len_rng), bend=rng.normal(0, bend)),
                    rng.uniform(*width_rng), rng.uniform(*alpha_rng)))
    return out


def turn_scuffs(rng, k, n, alpha_rng, rmax_frac=0.95):
    """concentric arcs on a knob top: the marks fingers leave turning it"""
    out = []
    for _ in range(n):
        rr = k['cap'] * rng.uniform(0.25, rmax_frac)
        a0 = rng.uniform(0, 2 * math.pi)
        span = rng.uniform(0.3, 1.6)
        pts = [(k['cx'] + rr * math.cos(a0 + span * t / 12), k['cy'] + rr * math.sin(a0 + span * t / 12))
               for t in range(13)]
        out.append((pts, rng.uniform(0.6, 1.3), rng.uniform(*alpha_rng)))
    return out


# ---------------------------------------------------------------------------
# relief: dents and grooves lit from the top left
# ---------------------------------------------------------------------------
def shade_from_height(Hm, strength):
    gy, gx = np.gradient(Hm)
    lx, ly, lz = -0.55, -0.65, 0.52
    n = np.sqrt(gx * gx + gy * gy + 1)
    lit = (-gx * lx - gy * ly + lz) / n - lz
    return lit * strength


def dent(Hm, cx, cy, r, depth, elong=1.0, ang=0.0):
    ca, sa = math.cos(ang), math.sin(ang)
    x0, x1 = int(max(0, cx - r * 3 * elong)), int(min(W, cx + r * 3 * elong))
    y0, y1 = int(max(0, cy - r * 3 * elong)), int(min(H, cy + r * 3 * elong))
    X, Y = XX[y0:y1, x0:x1] - cx, YY[y0:y1, x0:x1] - cy
    u = (X * ca + Y * sa) / elong
    v = -X * sa + Y * ca
    Hm[y0:y1, x0:x1] -= depth * np.exp(-(u * u + v * v) / (2 * r * r))


# ---------------------------------------------------------------------------
# dust
# ---------------------------------------------------------------------------
def specks(rng, n, region, rmin, rmax, alpha_rng, colour, fibres=0):
    im = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(im)
    ys, xs = np.nonzero(region > 0.5)
    idx = rng.integers(0, len(xs), n)
    for i in idx:
        x, y = xs[i], ys[i]
        r = rng.uniform(rmin, rmax) ** 1.0
        d.ellipse([x - r, y - r, x + r, y + r], fill=int(255 * rng.uniform(*alpha_rng)))
    for _ in range(fibres):
        i = rng.integers(0, len(xs))
        pts = scratch_path(rng, xs[i], ys[i], rng.uniform(0, 6.28), rng.uniform(20, 70), bend=rng.normal(0, 0.12), step=3)
        d.line(pts, fill=int(255 * rng.uniform(0.25, 0.5)), width=1)
    m = np.asarray(im.filter(ImageFilter.GaussianBlur(0.6))).astype(np.float32) / 255.0
    return m


def knob_base_ring():
    """dust gathers in a ring round each knob's foot and in the knurling"""
    m = np.zeros((H, W), np.float32)
    for k in KNOBS:
        d = np.hypot(XX - k['cx'], YY - k['cy'])
        m = np.maximum(m, np.exp(-((d - k['body'] - 3) / 5) ** 2))
    return m


def knurl_ring():
    m = np.zeros((H, W), np.float32)
    for k in KNOBS:
        d = np.hypot(XX - k['cx'], YY - k['cy'])
        m = np.maximum(m, ((d > k['cap'] + 1) & (d < k['body'])).astype(np.float32))
    for s in SELS:
        d = np.hypot(XX - s['cx'], YY - s['cy'])
        m = np.maximum(m, np.exp(-((d - s['r'] * 0.8) / 3) ** 2))
    return m


def edge_accumulation():
    """dust drifts into corners and along box edges: distance to bright strokes"""
    lum = luminance(BASE)
    lines = (lum > 0.55) & (M_PANEL > 0.5)
    dist = ndi.distance_transform_edt(~lines)
    return np.exp(-dist / 10.0) * (M_PANEL)


def fingerprint(cx, cy, rx, ry, ang, rng):
    ca, sa = math.cos(ang), math.sin(ang)
    X, Y = XX - cx, YY - cy
    u = (X * ca + Y * sa) / rx
    v = (-X * sa + Y * ca) / ry
    rr = np.sqrt(u * u + v * v)
    warp = noise(rng, 30) * 0.05
    ridges = 0.5 + 0.5 * np.sin((rr + warp) * 2 * math.pi * 14)
    env = np.exp(-(rr ** 2) * 2.2)
    return ridges * env


def lettering_mask():
    lum = luminance(BASE)
    sat = BASE.max(2) - BASE.min(2)
    return np.clip((lum - 0.45) / 0.3, 0, 1) * (sat < 0.12) * M_PANEL


def save(img, name):
    im = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))
    im.save(name, optimize=True)
    print('wrote', name)
    return im



def studio_worn(seed=11):
    rng = np.random.default_rng(seed)
    img = BASE.copy()
    haze = norm01(noise(rng, 90, 3)) * 0.55 + norm01(noise(rng, 6, 2)) * 0.45
    reach = np.clip(0.5 + (XX / W) * 0.3 - (YY / H) * 0.25, 0, 1)
    dust = (haze * 0.07 * reach + edge_accumulation() * 0.06 + knob_base_ring() * 0.10) * M_PANEL
    img = over(img, (150, 146, 138), dust)
    paths = []
    for _ in range(9):
        paths += scratch_cluster(rng, int(rng.integers(3, 9)), rng.uniform(0, W), rng.uniform(0, H),
                                 60, rng.normal(0.15, 0.4), (40, 220), (0.5, 0.9), (0.15, 0.4))
    for _ in range(40):
        paths.append((scratch_path(rng, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0, 6.28),
                                   rng.uniform(15, 90), bend=rng.normal(0, 0.01)), 0.6, rng.uniform(0.1, 0.3)))
    sc = stroke_layer(paths) * (1 - M_SCREEN)
    img = over(img, (120, 120, 116), sc * 0.9)
    kp = []
    for k in KNOBS:
        kp += turn_scuffs(rng, k, int(rng.integers(8, 18)), (0.12, 0.35))
    ks = stroke_layer(kp)
    img = over(img, (150, 150, 148), ks * M_KNOB)
    sheen = np.zeros((H, W), np.float32)
    for k in KNOBS:
        sheen = np.maximum(sheen, disc(k['cx'] - k['cap'] * 0.25, k['cy'] - k['cap'] * 0.3, k['cap'] * 0.55, 8) * 0.06)
    img = img + sheen[..., None]
    knurl = knurl_ring() * M_KNOB * (0.18 + 0.15 * norm01(noise(rng, 2)))
    img = over(img, (140, 136, 128), knurl)
    # step keys: tops polished in the middle by fingers, grime in the gap round them
    kring = np.zeros((H, W), np.float32)
    for r in KEYS:
        kring = np.maximum(kring, rectmask(r, 4) - rectmask(r, 0))
    img = over(img, (130, 126, 118), ndi.gaussian_filter(kring, 1.5) * 0.22)
    kpol = np.zeros((H, W), np.float32)
    for r in KEYS:
        kpol = np.maximum(kpol, disc((r[0]+r[2])/2, (r[1]+r[3])/2 + 6, (r[2]-r[0]) * 0.3, 10) * 0.03)
    img = img + (kpol * M_KEY)[..., None]
    sd = specks(rng, 300, M_SCREEN, 0.4, 1.3, (0.15, 0.5), None, fibres=4)
    img = over(img, (170, 170, 168), sd * M_SCREEN)
    fp = np.zeros((H, W), np.float32)
    for i, a in [(0, 0.4), (len(SCREENS) - 1, -0.3)]:
        r = SCREENS[i]
        fp += fingerprint(r[0] + (r[2]-r[0]) * 0.7, r[1] + (r[3]-r[1]) * 0.55, 26, 34, a, rng) * 0.06
    img = over(img, (200, 200, 205), fp * M_SCREEN)
    pd = specks(rng, 1400, M_PANEL + M_KNOB, 0.3, 1.2, (0.15, 0.45), None, fibres=10)
    img = over(img, (175, 170, 160), pd)
    return save(img, outp)

if __name__ == '__main__':
    studio_worn(int(sys.argv[4]) if len(sys.argv) > 4 else 11)
