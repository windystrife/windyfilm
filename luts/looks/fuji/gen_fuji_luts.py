#!/usr/bin/env python3
"""Five original film-style 3D LUTs for Film Roll (EOS M), inspired by the publicly
described character of some Fujifilm film simulations / stocks.  Nothing here is
Fujifilm data: every number below is a hand-tuned parameter of the model in this file.

Model (input = display-referred sRGB/Rec.709 JPEG, Picture Style Standard/Neutral):
  1. sRGB -> linear -> OKLab (L, C, h).
  2. Per-hue edits: hue shift dH(h), chroma gain S(h), lightness shift dL(h)*C
     (smooth periodic von-Mises interpolation between 8 hue anchors; dL scales with
     chroma, so neutrals are untouched and nothing jumps near grey).  Chroma is also
     scaled by the ratio of the sRGB cusp chroma between old and new hue.
  3. Global chroma gain + soft-knee chroma limit (absolute OKLab chroma).
  4. Tone: lift-free monotone curve (Fritsch-Carlson cubic) on display-encoded
     lightness; chroma follows the curve as C *= (L'/L)^kappa.
  5. Chroma vs output lightness (shadow / highlight saturation).
     Colours at the sRGB cube surface (a channel at 0/255 = clipped in the JPEG) get
     only 25 % of the hue turn, no net chroma boost and little split tone; saturated
     pure blues get no hue turn (OKLab hue is degenerate along the cyan-blue edge).
     This keeps them off the gamut ridges: no kinks / pale patches there.
  6. Split toning: OKLab (a,b) offsets weighted by output lightness.
  7. Gamut: soft compression at constant hue toward an anchor on the grey axis
     (same L for weak colours, sliding to the cusp L for strong ones); highlights
     roll off to white without hue clipping.
  8. Optional neutral/tinted veil in linear light (lifted, milky blacks).
  Skin guard (review 11-09, optional per look): skin_sat gives part of the shadow
  desaturation back to skin hues, skin_ts fades the cool shadow tone and the veil tint
  out on skin, so deep skin stays brown instead of ashen grey (ETERNA, CLASNEG).
Tried and dropped (OPT): cusp-relative lightness shift on hue turns (tri_l), a cusp
guard on the tone curve (guard) and no-lift for clipped colours (lfade): all three
broke monotonic luma along the cube axes.
Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1.

usage: python3 gen_fuji_luts.py <outdir>        (writes the .CUB files + curves.png + checks)
"""
import sys, os, json
import numpy as np

N = 33
OPT = dict(tri_c=1.0, tri_l=0.0, guard=0.0, knee=0.88, hfade=0.75, lfade=0.0)   # see docstring

# ------------------------------------------------------------------ colour maths
def srgb_to_lin(v):
    v = np.asarray(v, np.float64)
    return np.where(v <= 0.04045, v / 12.92, ((np.maximum(v, 0) + 0.055) / 1.055) ** 2.4)

def lin_to_srgb(l):
    l = np.clip(np.asarray(l, np.float64), 0.0, None)
    return np.where(l <= 0.0031308, 12.92 * l, 1.055 * np.power(l, 1 / 2.4) - 0.055)

M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
               [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
               [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])
M1i, M2i = np.linalg.inv(M1), np.linalg.inv(M2)

def lin_to_oklab(rgb):
    return np.cbrt(rgb @ M1.T) @ M2.T

def oklab_to_lin(lab):
    return ((lab @ M2i.T) ** 3) @ M1i.T

def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)

def pchip(xk, yk):
    """Monotone cubic (Fritsch-Carlson, scipy PCHIP end conditions)."""
    xk, yk = np.asarray(xk, float), np.asarray(yk, float)
    h = np.diff(xk); d = np.diff(yk) / h
    m = np.zeros_like(yk)
    for i in range(1, len(xk) - 1):
        if d[i - 1] * d[i] > 0:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    def end(h0, h1, d0, d1):
        e = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1)
        if np.sign(e) != np.sign(d0): return 0.0
        if np.sign(d0) != np.sign(d1) and abs(e) > 3 * abs(d0): return 3 * d0
        return e
    m[0] = end(h[0], h[1], d[0], d[1]); m[-1] = end(h[-1], h[-2], d[-1], d[-2])
    def f(x):
        x = np.clip(np.asarray(x, float), xk[0], xk[-1])
        i = np.clip(np.searchsorted(xk, x) - 1, 0, len(h) - 1)
        t = (x - xk[i]) / h[i]
        t2, t3 = t * t, t * t * t
        return ((2 * t3 - 3 * t2 + 1) * yk[i] + (t3 - 2 * t2 + t) * h[i] * m[i]
                + (-2 * t3 + 3 * t2) * yk[i + 1] + (t3 - t2) * h[i] * m[i + 1])
    return f

# hue anchors (OKLCh degrees): red, orange/skin, yellow, green, cyan, blue, purple, magenta
HUES = np.radians([27, 58, 100, 140, 195, 255, 300, 345])
KAPPA_HUE = 7.0                       # von Mises concentration (~ +-22 deg)

def hue_fn(h, vals):
    w = np.exp(KAPPA_HUE * (np.cos(h[..., None] - HUES) - 1.0))
    return (w * np.asarray(vals, float)).sum(-1) / w.sum(-1)

def softknee(x, knee, top):
    """identity below knee, smooth approach to top above it."""
    if top <= knee: return np.minimum(x, top)
    s = top - knee
    return np.where(x <= knee, x, knee + s * np.tanh((x - knee) / s))

def cmax_srgb(L, h, iters=22):
    """max in-gamut OKLab chroma at (L, h) for linear sRGB [0,1] (bisection)."""
    lo = np.zeros_like(L); hi = np.full_like(L, 0.45)
    ca, sa = np.cos(h), np.sin(h)
    for _ in range(iters):
        mid = 0.5 * (lo + hi)
        rgb = oklab_to_lin(np.stack([L, mid * ca, mid * sa], -1))
        ok = np.all((rgb >= -1e-7) & (rgb <= 1 + 1e-7), -1)
        lo = np.where(ok, mid, lo); hi = np.where(ok, hi, mid)
    return lo

_CUSP = None
def cusp(h):
    """(L, C) of the sRGB gamut cusp at hue h (table, 0.5 deg)"""
    global _CUSP
    if _CUSP is None:
        # the cusps are the cube edges R-Y-G-C-B-M-R (one channel 1, one 0)
        t = np.linspace(0, 1, 6000, endpoint=False)[:, None]
        corners = np.array([[1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 1, 1], [0, 0, 1], [1, 0, 1], [1, 0, 0]], float)
        loop = np.concatenate([(1 - t) * corners[k] + t * corners[k + 1] for k in range(6)])
        lab = lin_to_oklab(loop)
        hh = np.mod(np.arctan2(lab[:, 2], lab[:, 1]), 2 * np.pi)
        o = np.argsort(hh)
        hh, Lc, Cc = hh[o], lab[o, 0], np.hypot(lab[o, 1], lab[o, 2])
        hh, Lc, Cc = np.r_[hh[-1] - 2 * np.pi, hh, hh[0] + 2 * np.pi], np.r_[Lc[-1], Lc, Lc[0]], np.r_[Cc[-1], Cc, Cc[0]]
        # near the blue vertex OKLab hue barely moves along the C-B edge, so the exact cusp
        # curve is near-vertical there; these heuristics want a smooth cusp: circular
        # Gaussian blur, sigma 8 deg (the exact boundary is still used by gamut_soft)
        g = np.radians(np.arange(0, 360, 0.25))
        Lg, Cg = np.interp(g, hh, Lc), np.interp(g, hh, Cc)
        k = np.exp(-0.5 * (np.arange(-128, 129) / 32.0) ** 2); k /= k.sum()
        blur = lambda v: np.convolve(np.r_[v[-128:], v, v[:128]], k, mode="valid")
        Lg, Cg = blur(Lg), blur(Cg)
        _CUSP = (np.r_[g, 2 * np.pi], np.r_[Lg, Lg[0]], np.r_[Cg, Cg[0]])
    hs, Lc, Cc = _CUSP
    hd = np.mod(h, 2 * np.pi)
    return np.interp(hd, hs, Lc), np.interp(hd, hs, Cc)

def gamut_soft(L, C, h, knee=0.88):
    """Soft compression into sRGB at constant hue.  Points are moved along a line toward an
    anchor on the grey axis: same L for weak colours / anything below the cusp, sliding to the
    cusp lightness for strong colours above it (bright saturated yellows/greens get a little
    darker instead of washing out).  Below knee * boundary nothing changes."""
    Lc, Cc = cusp(h)
    w = smoothstep(0.0, 1.0, C / np.maximum(Cc, 1e-6))
    L0 = L - np.maximum(L - Lc, 0.0) * w
    ca, sa = np.cos(h), np.sin(h)
    lo = np.zeros_like(L); hi = np.full_like(L, 1.25)
    for _ in range(24):
        mid = 0.5 * (lo + hi)
        Lm = L0 + mid * (L - L0); Cm = mid * C
        rgb = oklab_to_lin(np.stack([Lm, Cm * ca, Cm * sa], -1))
        ok = np.all((rgb >= -1e-7) & (rgb <= 1 + 1e-7), -1)
        lo = np.where(ok, mid, lo); hi = np.where(ok, hi, mid)
    q = 1.0 / np.maximum(lo, 1e-6)
    s = np.where(q > knee, softknee(q, knee, 0.999) / q, 1.0)
    return L0 + s * (L - L0), s * C

# ------------------------------------------------------------------ the looks
# tone knots: (input, output) on display-encoded lightness (sRGB code / 255 of a grey).
# mid grey 18% = 0.461 (code 118).  f(0) = 0 always; lift comes from the veil.
LOOKS = {
 "CCHROME": dict(
    title="CCHROME - muted chrome-style documentary look (original LUT, not Fujifilm data)",
    note="Mau tram, bong toi chac hon, xanh troi nhat va hoi nga cyan, do/da tram am. Hop pho, doi thuong, tai lieu.",
    tone=[(0, 0), (0.05, 0.028), (0.15, 0.112), (0.30, 0.268), (0.461, 0.448), (0.65, 0.662), (0.85, 0.862), (1.0, 0.985)],
    kappa=0.85,
    #       red  skin  yel   grn   cyan  blue  purp  mag
    dH  =  [ 3,   0,   -4,   -3,   -4,  -12,   -8,   -2],
    S   =  [0.84, 0.88, 0.90, 0.76, 0.78, 0.68, 0.74, 0.80],
    dL  =  [-0.06, 0.0, -0.04, -0.12, -0.08, -0.22, -0.10, -0.05],
    sat=0.92, cknee=0.085, cmax=0.180,
    satL=[(0.0, 0.80), (0.30, 0.86), (0.60, 1.0), (0.85, 0.95), (1.0, 0.78)],
    ts=(0.0, 0.0), ts_range=(0.10, 0.50), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=0.0, veil_rgb=(1, 1, 1)),
 "PRONEG": dict(
    title="PRONEG - soft low-contrast portrait negative look (original LUT, not Fujifilm data)",
    note="Tuong phan mem, mau nhe, da tu nhien, vung sang chuyen mem. Hop chan dung, anh cuoi, anh trong nha.",
    # review 11-09: white 242 -> ~247 and a little more highlight chroma; at 242 + satL 0.70
    # lit skin went khaki-grey and clipped sky/sun turned flat grey
    tone=[(0, 0), (0.06, 0.060), (0.18, 0.178), (0.30, 0.294), (0.461, 0.453), (0.65, 0.632), (0.85, 0.825), (0.95, 0.915), (1.0, 0.968)],
    kappa=0.80,
    dH  =  [ 2,   1,    0,    5,    0,   -4,   -3,   -2],
    S   =  [0.90, 0.93, 0.88, 0.84, 0.88, 0.88, 0.88, 0.88],
    dL  =  [ 0.0, 0.02, 0.0, -0.04, 0.0, -0.05, 0.0, 0.0],
    sat=0.92, cknee=0.10, cmax=0.21,
    satL=[(0.0, 0.85), (0.30, 0.92), (0.60, 1.0), (0.85, 0.93), (1.0, 0.76)],
    ts=(0.0, 0.0), ts_range=(0.10, 0.50), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=0.0040, veil_rgb=(1, 1, 1)),
 "ETERNA": dict(
    title="ETERNA - flat cinema look, teal shadows (original LUT, not Fujifilm data)",
    note="Tuong phan thap, mau rat nhat, den nang len (milky), bong toi hoi xanh teal. Kieu phim dien anh.",
    tone=[(0, 0), (0.06, 0.060), (0.18, 0.180), (0.30, 0.292), (0.461, 0.440), (0.65, 0.615), (0.85, 0.795), (0.95, 0.880), (1.0, 0.925)],
    kappa=0.75,
    dH  =  [ 4,   2,   -2,    8,    0,   -7,   -6,   -2],
    S   =  [0.86, 0.93, 0.70, 0.64, 0.78, 0.74, 0.70, 0.74],
    dL  =  [-0.03, 0.0, -0.02, -0.06, -0.03, -0.10, -0.04, -0.03],
    sat=0.80, cknee=0.065, cmax=0.150,
    satL=[(0.0, 0.70), (0.30, 0.80), (0.60, 1.0), (0.85, 0.88), (1.0, 0.70)],
    ts=(-0.012, -0.010), ts_range=(0.15, 0.62), th=(0.002, 0.006), th_range=(0.62, 0.95),
    # review 11-09: skin guard - deep skin was ashen grey (85,55,40 -> 74,64,58), now 82,61,50
    veil=0.0070, veil_rgb=(0.80, 1.04, 1.12), skin_ts=1.0, skin_sat=0.40),
 "SUPERIA": dict(
    title="SUPERIA - punchy consumer colour-print look (original LUT, not Fujifilm data)",
    note="Xanh la tuoi (nga xanh-cyan), bong toi hoi xanh luc-cyan, vung sang am, tuong phan vua. Anh du lich, ngoai troi.",
    tone=[(0, 0), (0.05, 0.038), (0.15, 0.124), (0.30, 0.282), (0.461, 0.461), (0.65, 0.662), (0.85, 0.858), (1.0, 0.985)],
    kappa=0.80,
    dH  =  [-2,   0,    4,    7,    2,   -5,   -2,   -1],
    S   =  [1.08, 0.98, 1.02, 1.28, 1.08, 0.94, 0.96, 1.02],
    dL  =  [-0.04, 0.0, 0.0, -0.06, -0.04, 0.0, -0.02, -0.02],
    sat=1.00, cknee=0.16, cmax=0.30,
    satL=[(0.0, 0.85), (0.30, 0.95), (0.60, 1.0), (0.85, 0.95), (1.0, 0.80)],
    ts=(-0.011, -0.002), ts_range=(0.12, 0.55), th=(0.003, 0.013), th_range=(0.58, 0.95),
    veil=0.0012, veil_rgb=(0.92, 1.05, 1.00)),
 "CLASNEG": dict(
    title="CLASNEG - hard-shadow negative look, cyan shadows / warm highlights (original LUT, not Fujifilm data)",
    note="Bong toi cung va nga cyan-xanh luc, vung sang am hoi hong, tach mau ro (am ra am, lanh ra lanh). Kieu anh film may du lich cu.",
    tone=[(0, 0), (0.06, 0.030), (0.18, 0.130), (0.30, 0.262), (0.461, 0.440), (0.65, 0.665), (0.85, 0.875), (1.0, 0.985)],
    kappa=0.85,
    dH  =  [-4,  -3,   -9,   14,    2,  -10,   -8,   -4],
    S   =  [1.00, 0.94, 0.84, 0.86, 1.00, 0.90, 0.84, 0.90],
    dL  =  [-0.04, 0.0, -0.02, -0.10, -0.06, -0.16, -0.06, -0.04],
    sat=0.96, cknee=0.11, cmax=0.22,
    satL=[(0.0, 0.85), (0.30, 0.92), (0.60, 1.0), (0.85, 0.95), (1.0, 0.78)],
    ts=(-0.016, -0.008), ts_range=(0.12, 0.58), th=(0.010, 0.012), th_range=(0.58, 0.95),
    # review 11-09: partial skin guard so very deep skin keeps some warmth under the cyan shadows
    veil=0.0, veil_rgb=(1, 1, 1), skin_ts=0.6),
}
ORDER = ["CCHROME", "PRONEG", "ETERNA", "SUPERIA", "CLASNEG"]

def tone_fn(P):
    xs, ys = zip(*P["tone"])
    f = pchip(xs, ys)
    def g(L):                                     # OKLab L -> L' through display domain
        Yd = lin_to_srgb(np.clip(L, 0, 1) ** 3)
        return np.cbrt(srgb_to_lin(f(Yd)))
    return g

def apply_look(rgb, P):
    """rgb: (...,3) display sRGB 0..1  ->  (...,3) display sRGB 0..1"""
    lab = lin_to_oklab(srgb_to_lin(rgb))
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    C = np.hypot(a, b); h = np.arctan2(b, a)
    # how close the input is to the sRGB cube surface (1 = a channel at 0 or 255), measured in
    # RGB because OKLab's own boundary distance is ill-conditioned near the blue vertex.
    # Saturated colours at the surface (clipped channels in the JPEG) get smaller hue moves and
    # no chroma boost, so they are not pushed across gamut ridges -> no kinks / pale patches.
    mx, mn = rgb.max(-1), rgb.min(-1)
    q_rgb = (mx - mn) / (mx - mn + 2 * np.minimum(mn, 1 - mx) + 1e-6)
    edge = smoothstep(0.50, 1.0, q_rgb) * smoothstep(0.03, 0.08, C)
    # 2. per-hue
    #    chroma follows the cusp chroma of the new hue (tri_c); the matching lightness
    #    shift (tri_l) is off, it broke monotonic luma.
    wC = smoothstep(0.0, 0.02, C)
    #    blue-vertex guard: along the sRGB C-B edge OKLab hue hardly moves, so even a 1-2 deg
    #    turn of a saturated pure blue means a big jump in G -> no hue turn there
    bv = np.exp(-0.5 * (np.angle(np.exp(1j * (h - np.radians(264.05)))) / np.radians(8.0)) ** 2) * smoothstep(0.35, 0.85, q_rgb)
    wC = wC * (1 - bv)
    h1 = h + np.radians(hue_fn(h, P["dH"])) * wC * (1 - OPT["hfade"] * edge)
    #    skin weight (review fix 11-09): skin-hued colours with some chroma, used by skin_sat,
    #    skin_ts and the veil tint below so deep skin keeps its warmth in the cool-shadow looks
    w_sk = np.exp(6.0 * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, 0.05, C)
    Lc0, Cc0 = cusp(h); Lc1, Cc1 = cusp(h1)
    gam = np.clip(C / Cc0, 0, 1)
    C1 = C * hue_fn(h, P["S"]) * (1 + (Cc1 / Cc0 - 1) * gam * OPT["tri_c"])
    L1 = np.clip(L + hue_fn(h, P["dL"]) * C + gam * (Lc1 - Lc0) * OPT["tri_l"], 0, 1)
    # 3. global chroma
    C2 = softknee(C1 * P["sat"], P["cknee"], P["cmax"])
    # 4. tone
    T = tone_fn(P)(L1)
    L3 = L1 + np.minimum(T - L1, 0) + np.maximum(T - L1, 0) * (1 - OPT["lfade"] * edge)   # no lift for clipped colours
    r = np.clip(L3 / np.maximum(L1, 1e-4), 0.4, 2.0)
    C3 = C2 * r ** P["kappa"]
    #    cusp guard (OPT guard, off): would stop the curve lifting a strong colour above
    #    its hue's cusp; it broke monotonic luma, the looks keep their upper curve gentle instead.
    wg = smoothstep(0.55, 1.0, C3 / Cc1)
    L3 = L3 - np.maximum(0.0, L3 - np.maximum(L1, Lc1)) * wg * OPT["guard"]
    # 5. chroma vs lightness
    xs, ys = zip(*P["satL"])
    C3 = C3 * pchip(xs, ys)(L3)
    #    skin_sat (review fix 11-09): part of the shadow desaturation is given back to skin hues,
    #    so deep skin does not go grey in the flat / desaturated looks.
    if P.get("skin_sat", 0.0):
        C3 = C3 * (1 + P["skin_sat"] * w_sk * (1 - smoothstep(0.35, 0.65, L3)))
    # 5b. headroom: any net chroma gain fades out as the input nears the sRGB boundary
    #     (vibrance-style), so colours already at the edge are not pushed against it.
    g = C3 / np.maximum(C, 1e-9)
    g = np.where(g > 1, 1 + (g - 1) * (1 - smoothstep(0.35, 1.0, q_rgb) * smoothstep(0.03, 0.08, C)), g)
    C3 = np.where(C > 1e-9, C * g, C3)
    # 6. split tone
    s0, s1 = P["ts_range"]; t0, t1 = P["th_range"]
    tint_k = 1 - 0.8 * edge                      # toning is for neutrals / soft colours
    ws = (1 - smoothstep(s0, s1, L3)) * tint_k
    #    skin keeps its warmth in the shadows (review fix 11-09): the cool shadow tone is faded
    #    out on skin-hued colours (skin_ts = 0..1), otherwise deep skin turns ashen grey.
    if P.get("skin_ts", 0.0):
        ws = ws * (1 - P["skin_ts"] * w_sk)
    wh = smoothstep(t0, t1, L3) * (1 - 0.6 * smoothstep(0.90, 1.0, L3)) * tint_k
    a3 = C3 * np.cos(h1) + P["ts"][0] * ws + P["th"][0] * wh
    b3 = C3 * np.sin(h1) + P["ts"][1] * ws + P["th"][1] * wh
    # 7. gamut: soft compression at constant hue (see gamut_soft)
    C4 = np.hypot(a3, b3); h4 = np.arctan2(b3, a3)
    L5, C5 = gamut_soft(L3, C4, h4, OPT["knee"])
    lin = oklab_to_lin(np.stack([L5, C5 * np.cos(h4), C5 * np.sin(h4)], -1))
    lin = np.clip(lin, 0, 1)
    # 8. veil (lifted blacks, optionally tinted)
    v = P["veil"]
    if v > 0:
        vr = np.asarray(P["veil_rgb"], float); vr = vr / (0.2126 * vr[0] + 0.7152 * vr[1] + 0.0722 * vr[2])
        #    a tinted veil goes neutral on skin (same skin_ts weight), the lift itself stays
        k = (P.get("skin_ts", 0.0) * w_sk)[..., None]
        lin = lin * (1 - v) + v * (vr * (1 - k) + k)
    return np.clip(lin_to_srgb(lin), 0, 1)

# ------------------------------------------------------------------ cube io + checks
def build_cube(P, n=N):
    x = np.linspace(0, 1, n)
    B, G, R = np.meshgrid(x, x, x, indexing="ij")          # [b][g][r] -> red fastest when flattened
    rgb = np.stack([R, G, B], -1)
    return apply_look(rgb, P)                               # (n,n,n,3) indexed [b,g,r]

def write_cube(path, P, out):
    n = out.shape[0]
    with open(path, "w", newline="\n") as f:
        f.write('TITLE "%s"\n' % P["title"])
        f.write("# original creative LUT, parametric OKLab model (gen_fuji_luts.py); input: Canon JPEG sRGB\n")
        f.write("LUT_3D_SIZE %d\n" % n)
        for r, g, b in out.reshape(-1, 3):
            f.write("%.6f %.6f %.6f\n" % (r, g, b))

def luma(rgb):
    lin = srgb_to_lin(rgb)
    return lin_to_srgb(0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2])

def checks(name, P, out):
    n = out.shape[0]
    x = np.linspace(0, 1, n)
    B, G, R = np.meshgrid(x, x, x, indexing="ij")
    inp = np.stack([R, G, B], -1)
    Yo = luma(out)
    # monotonic luma along every r, g, b line of the cube and the grey axis
    viol = {ax: int((np.diff(Yo, axis=ax) < -1e-3).sum()) for ax in (0, 1, 2)}
    grey = out[np.arange(n), np.arange(n), np.arange(n)]
    gl = lin_to_oklab(srgb_to_lin(grey))
    grey_mono = bool(np.all(np.diff(luma(grey)) > 0))
    grey_tint = float(np.hypot(gl[:, 1], gl[:, 2]).max())
    # smoothness: max |2nd difference| along each axis (output units, 0..1)
    d2 = max(float(np.abs(np.diff(out, 2, axis=ax)).max()) for ax in (0, 1, 2))
    d2p = max(float(np.percentile(np.abs(np.diff(out, 2, axis=ax)), 99.9)) for ax in (0, 1, 2))
    # hue flips for chromatic inputs (C > 0.05, output C > 0.03)
    li = lin_to_oklab(srgb_to_lin(inp)); lo = lin_to_oklab(srgb_to_lin(out))
    Ci = np.hypot(li[..., 1], li[..., 2]); Co = np.hypot(lo[..., 1], lo[..., 2])
    m = (Ci > 0.05) & (Co > 0.03)
    dh = np.degrees(np.angle(np.exp(1j * (np.arctan2(lo[..., 2], lo[..., 1]) - np.arctan2(li[..., 2], li[..., 1])))))[m]
    # skin patches (sRGB) -> OKLCh before/after
    skins = np.array([(255, 219, 190), (240, 190, 160), (214, 160, 125), (194, 150, 130), (180, 125, 90), (130, 85, 60), (115, 82, 68)]) / 255.0
    so = apply_look(skins, P)
    def lch(c):
        l = lin_to_oklab(srgb_to_lin(c)); return np.c_[l[:, 0], np.hypot(l[:, 1], l[:, 2]), np.degrees(np.arctan2(l[:, 2], l[:, 1]))]
    return dict(look=name, luma_violations_rgb_axes=viol, grey_monotonic=grey_mono, grey_max_tint_oklab=round(grey_tint, 4),
                black_out=[round(float(v) * 255, 1) for v in out[0, 0, 0]], white_out=[round(float(v) * 255, 1) for v in out[-1, -1, -1]],
                mid_grey_118_out=[round(float(v) * 255, 1) for v in apply_look(np.array([[118 / 255.0] * 3]), P)[0]],
                max_abs_d2=round(d2, 4), p999_abs_d2=round(d2p, 4),
                hue_shift_deg_p1_p99_max=[round(float(np.percentile(dh, 1)), 1), round(float(np.percentile(dh, 99)), 1), round(float(np.abs(dh).max()), 1)],
                skin_in_LCh=np.round(lch(skins), 3).tolist(), skin_out_LCh=np.round(lch(so), 3).tolist(),
                skin_out_rgb=np.round(so * 255).astype(int).tolist())

# ------------------------------------------------------------------ curves diagram
def curves_png(path):
    from PIL import Image, ImageDraw
    cols = {"CCHROME": (90, 110, 120), "PRONEG": (200, 120, 90), "ETERNA": (40, 140, 140), "SUPERIA": (60, 160, 60), "CLASNEG": (180, 60, 120)}
    Wd, Hd, pad = 1600, 820, 50
    im = Image.new("RGB", (Wd, Hd), (250, 250, 248)); d = ImageDraw.Draw(im)
    # left: grey tone curves (display in/out)
    s = 700; ox, oy = pad, pad + s
    d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0))
    for k in range(1, 4):
        d.line([ox + k * s / 4, oy, ox + k * s / 4, oy - s], fill=(220, 220, 220))
        d.line([ox, oy - k * s / 4, ox + s, oy - k * s / 4], fill=(220, 220, 220))
    d.line([ox, oy, ox + s, oy - s], fill=(170, 170, 170))
    g = np.linspace(0, 1, 256)
    for nm in ORDER:
        y = luma(apply_look(np.stack([g, g, g], -1), LOOKS[nm]))
        d.line([(ox + g[i] * s, oy - y[i] * s) for i in range(256)], fill=cols[nm], width=3)
    d.text((ox, oy + 8), "grey in (display) ->  out luma; diagonal = no change", fill=(0, 0, 0))
    # right: hue ring at L=0.68, C=0.11 and its mapping per look (arrows on OKLab a,b)
    cx, cy, sc = 800 + 380, pad + 350, 2200
    d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(0, 0, 0))
    hs = np.radians(np.arange(0, 360, 15))
    ring = np.stack([np.full_like(hs, 0.68), 0.11 * np.cos(hs), 0.11 * np.sin(hs)], -1)
    rgb_in = np.clip(lin_to_srgb(oklab_to_lin(ring)), 0, 1)
    li = lin_to_oklab(srgb_to_lin(rgb_in))
    for i in range(len(hs)):
        x0, y0 = cx + li[i, 1] * sc, cy - li[i, 2] * sc
        d.ellipse([x0 - 7, y0 - 7, x0 + 7, y0 + 7], fill=tuple((rgb_in[i] * 255).astype(int)), outline=(0, 0, 0))
    for nm in ORDER:
        lo = lin_to_oklab(srgb_to_lin(apply_look(rgb_in, LOOKS[nm])))
        for i in range(len(hs)):
            d.line([cx + li[i, 1] * sc, cy - li[i, 2] * sc, cx + lo[i, 1] * sc, cy - lo[i, 2] * sc], fill=cols[nm], width=2)
            d.ellipse([cx + lo[i, 1] * sc - 2, cy - lo[i, 2] * sc - 2, cx + lo[i, 1] * sc + 2, cy - lo[i, 2] * sc + 2], fill=cols[nm])
    d.text((800, pad + 720), "OKLab a/b: ring of colours (L 0.68, C 0.11) -> where each look moves them", fill=(0, 0, 0))
    for k, nm in enumerate(ORDER):
        d.rectangle([800 + k * 150, 780, 820 + k * 150, 800], fill=cols[nm]); d.text((825 + k * 150, 784), nm, fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    rep = []
    for nm in ORDER:
        P = LOOKS[nm]
        out = build_cube(P)
        write_cube(os.path.join(outdir, nm + ".CUB"), P, out)
        c = checks(nm, P, out); rep.append(c)
        print(json.dumps(c))
    with open(os.path.join(outdir, "checks.json"), "w") as f:
        json.dump(rep, f, indent=1)
    curves_png(os.path.join(outdir, "preview_curves.png"))
