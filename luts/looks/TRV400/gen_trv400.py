#!/usr/bin/env python3
"""@@DOC@@"""
import sys, os, json, argparse
import numpy as np

N = 33
OPT = dict(tri_c=1.0, tri_l=0.0, guard=0.0, knee=0.60, hfade=0.75, lfade=0.0)   # as gen_chungking.py / gen_port160.py,
# except knee 0.84 (0.88 there): the gamut roll-off starts a little earlier, so dark saturated greens and bright
# oranges at the gamut edge change smoothly enough for the shipped 2.5D kernel (judge J8 per image <= 2)

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



# ------------------------------------------------------------------ S1 data: AF3-0217E (KAMIQ-06.10-FP), p.6, fig. 17
# (= data/AF30217E_p6_fig17_characteristic.csv; log H in log lux-s, Status M densities R, G, B, process CN-16,
# daylight 1/125 s; 0.1 log H steps from -3.6 to +0.1; "bridged" rows (grid line / curve label) come from a local
# linear fit of the traced line, see the csv header)
AF_LOGH = np.round(np.arange(-3.6, 0.15, 0.1), 2)
AF_D = np.array([
    [0.137, 0.454, 0.728], [0.143, 0.457, 0.730], [0.150, 0.460, 0.733], [0.161, 0.465, 0.737], [0.177, 0.477, 0.743],
    [0.195, 0.485, 0.751], [0.219, 0.503, 0.764], [0.243, 0.522, 0.777], [0.273, 0.552, 0.800], [0.306, 0.589, 0.833],
    [0.341, 0.635, 0.877], [0.380, 0.691, 0.936], [0.421, 0.749, 0.995], [0.474, 0.812, 1.062], [0.528, 0.876, 1.131],
    [0.585, 0.940, 1.201], [0.645, 1.007, 1.273], [0.705, 1.073, 1.346], [0.765, 1.138, 1.416], [0.822, 1.202, 1.487],
    [0.888, 1.270, 1.558], [0.948, 1.336, 1.629], [1.009, 1.400, 1.705], [1.070, 1.467, 1.773], [1.132, 1.534, 1.843],
    [1.194, 1.602, 1.912], [1.254, 1.667, 1.980], [1.315, 1.735, 2.049], [1.376, 1.802, 2.117], [1.437, 1.868, 2.185],
    [1.498, 1.936, 2.254], [1.560, 2.004, 2.323], [1.621, 2.071, 2.391], [1.684, 2.141, 2.462], [1.746, 2.209, 2.530],
    [1.808, 2.277, 2.599], [1.870, 2.346, 2.669], [1.932, 2.414, 2.739]])

def _check_csv():
    """the embedded table must equal data/AF30217E_p6_fig17_characteristic.csv when that file is next to the generator"""
    fn = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "AF30217E_p6_fig17_characteristic.csv")
    if not os.path.exists(fn): return None
    rows = [l.split(",") for l in open(fn) if l.strip() and not l.startswith("#")][1:]
    t = np.array([[float(x) for x in r[:4]] for r in rows])
    assert np.allclose(t[:, 0], AF_LOGH) and np.allclose(t[:, 1:], AF_D, atol=5e-4), "embedded AF3-0217E table != csv"
    return fn

def _mono(col):
    """monotone fit of a drawn record: flat at its minimum below the argmin, running maximum above (the traced
    records are already monotone; this only guards the fit)"""
    c = col.copy(); i = int(np.argmin(c)); c[:i] = c[i]
    return np.maximum.accumulate(c)

AF_DM = np.stack([_mono(AF_D[:, k]) for k in range(3)], -1)
AF_DMIN = AF_DM[0]                                # D at log H -3.6, the left end of the drawn curves (R 0.14, G 0.45, B 0.73)
_sl = (AF_LOGH >= -2.0 - 1e-9) & (AF_LOGH <= 1e-9)
AF_GAMMA = np.array([np.polyfit(AF_LOGH[_sl], AF_D[_sl, k], 1)[0] for k in range(3)])   # ~0.61 / 0.67 / 0.70
# Normal exposure of an 18 % grey (INFERRED from S1): the sheet prints no Log H reference; p.4 section 12 says an 18 %
# grey card "when read through the RED filter should render density readings between 0.75 and 0.95".  The grey
# card's log H is therefore where the red record reads the middle of that band, 0.85: log H -1.66 (band -1.83..-1.50).
# (Cross-check only: the ISO-arithmetic reference 14.5 / ISO lux-s used on other makers' sheets gives -1.44 for ISO 400,
# just above the band; the sheet's own statement is used.)
LOGH_REF = float(np.interp(0.85, AF_D[:, 0], AF_LOGH))
assert abs(LOGH_REF + 1.66) < 0.01
# ------------------------------------------------------------------ neg_scan: the consumer-neg family module
_DF = None
def _dfun():
    global _DF
    if _DF is None: _DF = [pchip(AF_LOGH, AF_DM[:, k]) for k in range(3)]
    return _DF

def neg_scan_grey(v, P):
    """grey input codes v (0..1) -> (..., 3) display R, G, B of the scanned negative (before the OKLab look)"""
    v = np.asarray(v, float)
    f0 = 0.18 * 2.0 ** P["ns_floor"]
    Df = _dfun()
    def rec(vv):
        l = srgb_to_lin(vv); E = l + f0 * (1 - l)
        lh = LOGH_REF + P["ns_ev"] * np.log10(2.0) + np.log10(E / 0.18)
        n = np.stack([Df[k](lh) - AF_DMIN[k] for k in range(3)], -1)
        return 10.0 ** (n / AF_GAMMA) - 1.0            # exposure above the record's toe, record contrast normalised
    L, L0, L1, Lm = rec(v), rec(np.array(0.0)), rec(np.array(1.0)), rec(np.array(118 / 255.0))
    # minilab auto levels: per-record black point at the JPEG black and white point at the JPEG white ...
    rel = np.clip((L - L0) / (L1 - L0), 0.0, 1.0)
    # ... and per-record grey balance at mid grey by a gamma (the record's mid lands on ns_mid)
    gm = np.log(P["ns_mid"] / P["ns_white"]) / np.log((Lm - L0) / (L1 - L0))
    rel = rel ** gm
    d = lin_to_srgb(P["ns_white"] * rel)                 # paper white ns_white
    # print contrast: display S-curve pinned at 0, at the mid-grey output dm and at white; slope at dm is about
    # 1 + ns_contrast; faded out over display ns_ctop so the light end (and the cube's bright gamut surface) is unchanged
    dm = float(lin_to_srgb(np.array(P["ns_mid"])))
    return d + P["ns_contrast"] * (d - dm) * d * (1.0 - d) * 4.0 * (1.0 - smoothstep(P["ns_ctop"][0], P["ns_ctop"][1], d))

NS_KNOTS = np.array([0, 2, 4, 6, 9, 13, 18, 25, 33, 43, 55, 70, 88, 108, 130, 152, 175, 198, 220, 238, 250, 255]) / 255.0

def resolve(P):
    """fills P["tone"] (display luma knots) and P["tintL"] (OKLab a, b vs output L) from neg_scan + tintL_lab
    (a preset that brings its own hand-set "tone" knots, TRV400_V1, is returned unchanged: no neg_scan)"""
    P = dict(P)
    if "tone" in P: return P
    out = neg_scan_grey(NS_KNOTS, P)
    lab = lin_to_oklab(srgb_to_lin(out))
    Lo = lab[:, 0]
    assert np.all(np.diff(Lo) > 0), "neg_scan grey output not rising"
    P["tone"] = [(float(x), float(y)) for x, y in zip(NS_KNOTS, lin_to_srgb(Lo ** 3))]
    kx, ka, kb = zip(*P["tintL_lab"])
    la, lb = pchip(kx, ka)(Lo), pchip(kx, kb)(Lo)
    # neutral white: the whole grey-axis tint fades out over output L tint_top (paper white stays white)
    ft = 1.0 - smoothstep(P["tint_top"][0], P["tint_top"][1], Lo)
    P["tintL"] = [(0.0, 0.0, 0.0)] + [(float(Lo[i]), float((P["ns_tint"] * lab[i, 1] + la[i]) * ft[i]), float((P["ns_tint"] * lab[i, 2] + lb[i]) * ft[i]))
                                      for i in range(1, len(Lo))]
    if P["tintL"][-1][0] < 1.0: P["tintL"].append((1.0, 0.0, 0.0))
    return P

# ------------------------------------------------------------------ the looks
# TRV400_V1 = the starting point: our own preset SUPERIA of luts_fuji/gen_fuji_luts.py (repo 4178f62, OKLab model from
# public descriptions, reviewed 11-09), values copied unchanged (hand-set tone knots, split tones).  Kept only as the
# "before" of the review sheets; it is not shipped and its CUB is never written as TRV400.CUB (rendered here with this
# file's OPT knee 0.84, so it is close to, not byte-equal with, the old SUPERIA.CUB).
# TRV400 = the shipped film: V1's colour model refined with the S1 data sheet (tone from fig. 17 through neg_scan,
# grey balance, see the docstring).  Every number is a GUESS unless its comment cites S1/S2/D*.
_LOOKS_SRC = {
 "TRV400_V1": dict(
    title="TRV400_V1 - starting preset (not shipped)",
    note="the SUPERIA preset values of gen_fuji_luts.py, unchanged",
    tone=[(0, 0), (0.05, 0.038), (0.15, 0.124), (0.30, 0.282), (0.461, 0.461), (0.65, 0.662), (0.85, 0.858), (1.0, 0.985)],
    kappa=0.80,
    dH  =  [-2,   0,    4,    7,    2,   -5,   -2,   -1],
    S   =  [1.08, 0.98, 1.02, 1.28, 1.08, 0.94, 0.96, 1.02],
    dL  =  [-0.04, 0.0, 0.0, -0.06, -0.04, 0.0, -0.02, -0.02],
    sat=1.00, cknee=0.16, cmax=0.30,
    satL=[(0.0, 0.85), (0.30, 0.95), (0.60, 1.0), (0.85, 0.95), (1.0, 0.80)],
    ts=(-0.011, -0.002), ts_range=(0.12, 0.55), th=(0.003, 0.013), th_range=(0.58, 0.95),
    veil=0.0012, veil_rgb=(0.92, 1.05, 1.00)),
 "TRV400": dict(
    title="TRV400 - Travel 400 (WindyStrife original LUT)",
    note="4-layer consumer negative 400: green lean, cyan-green shadows, warm highlights, punchy mids.",
    ns_floor=-4.6, ns_ev=0.0, ns_mid=0.18, ns_white=0.975, ns_contrast=0.20, ns_ctop=(0.5, 1.0), ns_tint=0.0, tint_top=(0.90, 0.97),
    # declared grey-axis tint (S1 Fig. 17 per-record toes, D1/D2): cyan-green shadows, neutral mids, warm light greys (GUESS amounts)
    tintL_lab=[(0.0, 0.0, 0.0), (0.10, -0.004, -0.001), (0.22, -0.008, -0.002), (0.35, -0.006, -0.0015), (0.48, -0.003, -0.001), (0.566, -0.002, 0.0),
               (0.68, -0.001, 0.004), (0.80, 0.002, 0.010), (0.90, 0.002, 0.008), (1.0, 0.0, 0.0)],
    kappa=0.80,
    #       red   skin  yel   grn   cyan  blue  purp  mag
    dH  =  [-3,   -4,   4,    7,    2,   -5,   -2,   -1],
    S   =  [1.12, 0.98, 1.06, 1.24, 1.08, 0.96, 1.04, 1.04],
    dL  =  [-0.04, 0.0, 0.0, -0.05, -0.04, 0.0, -0.02, -0.02],
    sat=1.04, cknee=0.16, cmax=0.30,
    satL=[(0.0, 0.85), (0.30, 0.95), (0.60, 1.0), (0.85, 0.95), (1.0, 0.80)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.58, 0.95),
    veil=0.004, veil_rgb=(0.92, 1.05, 1.00),
    skin_ts=0.8, skin_sat=0.0, skin_tl=1.0, skin_tl_kappa=2.0, tl_cfade=(0.03, 0.12, 0.5), tl_toe=0.15, sky_tl=(0.85, 220.0, 1.2, 0.05), skin_cmin=0.04),
}
_LOOKS_SRC["TRV400_L"] = dict(_LOOKS_SRC["TRV400"], title="TRV400_L - Travel 400, lighter preset (WindyStrife original LUT)",
    note="Lighter, cleaner variant of Travel 400: lower contrast and saturation, softer green-cyan lean (the Travel 200 look as a preset)")
ORDER = ["TRV400", "TRV400_V1", "TRV400_L"]
DEFAULT = "TRV400"
LOOKS = {k: resolve(v) for k, v in _LOOKS_SRC.items()}

# ------------------------------------------------------------------ family retune (colour-negative separation round, 2026-11-11)
# The measured axes of the colour-negative family (warmth, saturation, contrast, black level, shadow tint, highlight
# roll-off; see catalogue/lookfix/neg_separation.md) are moved to the film's own targets by a thin layer on the resolved
# preset: sat = global chroma gain, k = tone power about OKLab mid L (contrast), black = lift of the dark end,
# shoulder = highlight roll-off (pivot sh_pivot), wa/wb = grey-axis tint offset in the mids, sa/sb = in the shadows
# (OKLab a, b added to tintL).  Numbers are look calls (GUESS), fitted by measuring the table through the camera kernel.
def smooth_cube(out, P):
    """cube smoothing (colour-negative separation round): where the second difference along a cube axis exceeds sm_t0
    (output units) a 1-2-1 pass is blended in, fully at sm_t1 and above, sm_n passes; the nodes on the cube faces and the
    smooth bulk are untouched.  P["smooth"] = (sm_t0, sm_t1, sm_n); absent = off."""
    sm = P.get("smooth")
    if not sm: return mono_cube(out) if P.get("mono") else out
    t0, t1, nit = sm
    o = np.array(out, float)
    for _ in range(int(nit)):
        for ax in (0, 1, 2):
            v = np.moveaxis(o, ax, 0)
            m = v[:-2] + v[2:] - 2.0 * v[1:-1]
            w = np.clip((np.abs(m).max(-1, keepdims=True) - t0) / max(t1 - t0, 1e-9), 0.0, 1.0)
            v[1:-1] += 0.25 * w * m
    return mono_cube(o) if P.get("mono") else o

def mono_cube(out, tol=0.6 / 255.0):
    """luma repair (colour-negative separation round): along every cube axis a node whose luma (display of linear Rec.709
    luminance) falls more than tol under its predecessor is raised by the missing amount (all channels, clipped at 1)."""
    o = np.array(out, float)
    def Y(v):
        lin = srgb_to_lin(v); return lin_to_srgb(0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2])
    for _ in range(16):
        changed = False
        for ax in (0, 1, 2):
            v = np.moveaxis(o, ax, 0)
            for i in range(1, v.shape[0]):
                need = np.maximum(Y(v[i - 1]) - tol - Y(v[i]), 0.0)
                if need.max() > 1e-6:
                    changed = True; v[i] = np.clip(v[i] + need[..., None], 0.0, 1.0)
        if not changed: break
    return o

RETUNE = {'TRV400': {'smooth': (0.03, 0.06, 12), 'mono': 1.0, 'gk': 4.0, 'gc': 0.09, 'gather': 2.0, 'skl_lo': 0.6, 'dh': (6, 7, -14, -3, 0, 0, 0, 0), 'ssat': 0.5, 'spl': 8.0, 'sat': 1.05269, 'k': 1.0387, 'black': 0.00347, 'shoulder': -0.08554, 'wa': -0.01016, 'wb': 0.00069, 'sa': -0.00268, 'sb': -0.00188}, 'TRV400_L': {'smooth': (0.03, 0.06, 12), 'mono': 1.0, 'gk': 4.0, 'gc': 0.09, 'gather': 2.0, 'skl_lo': 0.6, 'dh': (6, 7, -14, -3, 0, 0, 0, 0), 'ssat': 0.5, 'spl': 8.0, 'sat': 0.9082, 'k': 0.85477, 'black': 0.00707, 'shoulder': 0.38558, 'wa': -0.00645, 'wb': 0.00072, 'sa': -0.00344, 'sb': -0.00237}}  # RETUNE-LINE
def retune(P, R):
    P = dict(P)
    if R.get("sat"): P["sat"] = P["sat"] * R["sat"]
    k, b, s = R.get("k", 1.0), R.get("black", 0.0), R.get("shoulder", 0.0)
    if k != 1.0 or b or s:
        xs, ys = zip(*P["tone"])
        L = np.cbrt(srgb_to_lin(np.array(ys, float)))
        Lm = R.get("mid", 0.566)
        L1 = L + (1.0 - smoothstep(0.78, 0.97, L)) * (Lm * (np.maximum(L, 1e-9) / Lm) ** k - L)   # contrast power fades out toward white (white stays put)
        L1 = L1 + b * np.clip(1.0 - L1 / Lm, 0, 1) ** 2
        Lp = R.get("sh_pivot", 0.62)
        t = np.clip((L1 - Lp) / (1.0 - Lp), 0, None)
        L2 = np.where(L1 > Lp, Lp + (1.0 - Lp) * (1.0 + s) * t / (1.0 + s * t), L1)
        L2 = np.clip(np.maximum.accumulate(L2), 0, 1)
        P["tone"] = [(float(x), float(y)) for x, y in zip(xs, lin_to_srgb(L2 ** 3))]
    if R.get("smooth"): P["smooth"] = tuple(R["smooth"])
    if R.get("mono"): P["mono"] = True
    if R.get("gather"): P["skin_gather"] = R["gather"]
    if R.get("gk"): P["skin_gather_k"] = R["gk"]
    if R.get("glo"): P["skin_gather_lo"] = R["glo"]
    if R.get("gc"): P["skin_gather_c0"] = R["gc"]
    if R.get("sdk"): P["skin_dark"] = R["sdk"]
    if R.get("spl"): P["skin_pale"] = R["spl"]
    if R.get("cmin"): P["skin_cmin"] = R["cmin"]
    if R.get("ssat") is not None: P["skin_sat"] = R["ssat"]
    if R.get("skl"): P["skin_tl"] = R["skl"]
    if R.get("skl_lo"): P["skin_tl_lo"] = R["skl_lo"]
    if R.get("tlk"): P["skin_tl_kappa"] = R["tlk"]
    if R.get("dh"): P["dH"] = [float(a) + float(b) for a, b in zip(P["dH"], R["dh"])]
    if R.get("ds"): P["S"] = [float(a) * float(b) for a, b in zip(P["S"], R["ds"])]
    wa, wb, sa, sb = (R.get(q, 0.0) for q in ("wa", "wb", "sa", "sb"))
    if wa or wb or sa or sb:
        kx, ka, kb = zip(*(P.get("tintL") or [(0.0, 0.0, 0.0), (0.5, 0.0, 0.0), (1.0, 0.0, 0.0)]))
        Lg = np.linspace(0, 1, 41)
        a, bb = pchip(kx, ka)(Lg), pchip(kx, kb)(Lg)
        wm = np.exp(-0.5 * ((Lg - 0.57) / 0.20) ** 2) * (1 - smoothstep(0.85, 0.98, Lg))
        ws = np.exp(-0.5 * ((Lg - 0.30) / 0.13) ** 2) * smoothstep(0.0, 0.08, Lg)
        a = a + wa * wm + sa * ws; bb = bb + wb * wm + sb * ws
        a[0] = bb[0] = 0.0; a[-1] = bb[-1] = 0.0
        P["tintL"] = [(float(x), float(p), float(q)) for x, p, q in zip(Lg, a, bb)]
    return P
LOOKS_BASE = {k: dict(v) for k, v in LOOKS.items()}
for _k in list(LOOKS):
    if _k in RETUNE: LOOKS[_k] = retune(LOOKS_BASE[_k], RETUNE[_k])

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
    #    skin_gather (from gen_tun800.py): skin hues are pulled toward 55 deg by a fraction of their distance (broad weight,
    #    kappa 4, full from C 0.04); it also cancels the spread the black lift, the shadow tint and the 17^3 table's chroma
    #    interpolation give deep / yellowish skin (retune: R["gather"])
    if P.get("skin_gather", 0.0):
        dsk = np.angle(np.exp(1j * (h - np.radians(55.0))))
        h1 = h1 - P["skin_gather"] * np.exp(P.get("skin_gather_k", 4.0) * (np.cos(dsk) - 1.0)) * smoothstep(0.01, 0.04, C) * smoothstep(np.radians(P.get("skin_gather_lo", -90.0)), np.radians(P.get("skin_gather_lo", -90.0) + 12.0), h) * (1.0 - smoothstep(P.get("skin_gather_c0", 9.0), P.get("skin_gather_c0", 9.0) + 0.06, C)) * dsk * (1 - OPT["hfade"] * edge)
    #    skin_dark (retune R["sdk"], degrees): pre-bias of dark skin hue; the 17^3 table renders the darkest low-chroma skins redder than the cube
    if P.get("skin_dark", 0.0):
        h1 = h1 + np.radians(P["skin_dark"]) * np.exp(8.0 * (np.cos(h - np.radians(50.0)) - 1.0)) * (1.0 - smoothstep(0.28, 0.48, L)) * smoothstep(0.01, 0.04, C)
    if P.get("skin_pale", 0.0):   # retune R["spl"] (degrees): pre-bias of pale skin hue (clipped-channel pale skin is rendered redder by the table)
        h1 = h1 + np.radians(P["skin_pale"]) * np.exp(8.0 * (np.cos(h - np.radians(50.0)) - 1.0)) * smoothstep(0.80, 0.90, L) * smoothstep(0.01, 0.04, C)
    #    skin weight (review fix 11-09): skin-hued colours with some chroma, used by skin_sat,
    #    skin_ts and the veil tint below so deep skin keeps its warmth in the cool-shadow looks
    #    skin_cmin (CHUNGK): chroma where the weight is full; deep skin in shadow has C ~0.03,
    #    at the default 0.05 it got only half the guard and went olive under the green cast
    w_sk = np.exp(6.0 * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
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
    #    neon guard (CHUNGK): split tone fades on strong colours, ts_cfade = (C0, C1, amount)
    if P.get("ts_cfade"):
        c0, c1, ka = P["ts_cfade"]
        ws = ws * (1 - ka * smoothstep(c0, c1, C3))
    #    toe fade (CHUNGK): the shadow tone grows in from black over L 0..ts_toe; at full strength
    #    right down to L=0 the offset clips negative RGB and the tint jumps in the first codes
    if P.get("ts_toe"):
        ws = ws * smoothstep(0.0, P["ts_toe"], L3)
    wh = smoothstep(t0, t1, L3) * (1 - 0.6 * smoothstep(0.90, 1.0, L3)) * tint_k
    a3 = C3 * np.cos(h1) + P["ts"][0] * ws + P["th"][0] * wh
    b3 = C3 * np.sin(h1) + P["ts"][1] * ws + P["th"][1] * wh
    # 6b. grey-axis tint curve vs output lightness (CHUNGK, CHUNGK_RT): tintL = [(L, da, db), ...], OKLab offsets
    #     as a smooth, shape-preserving function of L (pchip per component, no overshoot between knots), with the
    #     split tones' neutral weighting (tint_k) and three optional guards:
    #       skin_tl = k or (k_shadow, k_high)  fade on skin hues (the pair: shadow amount below L 0.30, high amount
    #                                         above 0.55); skin_tl_kappa = hue width of that fade (default: the
    #                                         skin weight w_sk above, kappa 6; lower = wider, smoother)
    #       tl_cfade = (C0, C1, amount)        fade on strong colours (neon, oranges, skin keep their hue)
    #       tl_toe = L                         the tint grows in from black over L 0..tl_toe
    #       sky_tl = (k, hue_deg, kappa, C1)    (gen_sun200.py, copied here) fade on sky blues: a warm grey-axis tint added
    #                                         to pale blue turns it green; amount k on hues around hue_deg (von
    #                                         Mises width kappa, wide), growing in over chroma 0..C1 (gentle, so
    #                                         the 17^3 table and the 2.5D kernel can follow it)
    #     Absent -> no change.  (CHUNGK_RT was fitted with the colour fade applied before the skin fade; the order
    #     only changes float rounding, its CHUNGK.CUB text is the same either way.)
    if P.get("tintL"):
        kx, kta, ktb = zip(*P["tintL"])
        wt = tint_k * np.ones_like(L3)
        if P.get("skin_tl", 0.0):
            k_sk = P["skin_tl"]
            if np.ndim(k_sk):
                k_sk = k_sk[0] + (k_sk[1] - k_sk[0]) * smoothstep(0.30, 0.55, L3)
            w_skt = w_sk
            if P.get("skin_tl_kappa"):
                w_skt = np.exp(P["skin_tl_kappa"] * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
            if P.get("skin_tl_lo"):   # low-chroma skin: the 17^3 table blends the tinted grey node into pale / deep skin, so the cube over-corrects it (retune R["skl_lo"])
                k_sk = k_sk + P["skin_tl_lo"] * (1.0 - smoothstep(0.015, 0.07, C))
            wt = wt * (1 - k_sk * w_skt)
        if P.get("tl_cfade"):
            c0, c1, ka = P["tl_cfade"]
            wt = wt * (1 - ka * smoothstep(c0, c1, C3))
        if P.get("tl_toe"):
            wt = wt * smoothstep(0.0, P["tl_toe"], L3)
        if P.get("sky_tl"):
            ks, hs, kps, cs1 = P["sky_tl"]
            wt = wt * (1 - ks * np.exp(kps * (np.cos(h - np.radians(hs)) - 1.0)) * smoothstep(0.0, cs1, C))
        a3 = a3 + pchip(kx, kta)(L3) * wt
        b3 = b3 + pchip(kx, ktb)(L3) * wt
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
    return smooth_cube(apply_look(rgb, P), P)                               # (n,n,n,3) indexed [b,g,r]

def write_cube(path, P, out):
    n = out.shape[0]
    with open(path, "w", newline="\n") as f:
        f.write('TITLE "%s"\n' % P["title"])
        # (same comment line as the other looks, with this generator named)
        f.write("# original creative LUT, parametric OKLab model (gen_trv400.py); input: Canon JPEG sRGB\n")
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
def curves_png(path, sel=DEFAULT, ref="TRV400_V1"):
    """left: neg_scan grey response per record (thin), the look's grey luma (bold) and the V1 preset (grey);
    right: hue ring moves of the look (orange) and of V1 (grey)"""
    from PIL import Image, ImageDraw
    P, R = LOOKS[sel], LOOKS[ref]
    Wd, Hd, pad = 1600, 840, 50
    im = Image.new("RGB", (Wd, Hd), (250, 250, 248)); d = ImageDraw.Draw(im)
    s = 700; ox, oy = pad, pad + s
    d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0))
    for k in range(1, 4):
        d.line([ox + k * s / 4, oy, ox + k * s / 4, oy - s], fill=(220, 220, 220))
        d.line([ox, oy - k * s / 4, ox + s, oy - k * s / 4], fill=(220, 220, 220))
    d.line([ox, oy, ox + s, oy - s], fill=(170, 170, 170))
    g = np.linspace(0, 1, 256)
    if "ns_floor" in P:
        ns = neg_scan_grey(g, P)
        for c, col in enumerate([(210, 50, 50), (50, 150, 50), (50, 80, 210)]):
            d.line([(ox + g[i] * s, oy - ns[i, c] * s) for i in range(256)], fill=col, width=1)
    yr = luma(apply_look(np.stack([g, g, g], -1), R))
    d.line([(ox + g[i] * s, oy - yr[i] * s) for i in range(256)], fill=(120, 120, 120), width=2)
    y = luma(apply_look(np.stack([g, g, g], -1), P))
    d.line([(ox + g[i] * s, oy - y[i] * s) for i in range(256)], fill=(200, 120, 0), width=4)
    d.text((ox, oy + 8), "grey in -> out: neg_scan R/G/B (thin, from AF3-0217E p.6 fig. 17), %s luma (bold), %s (grey)" % (sel, ref), fill=(0, 0, 0))
    cx, cy, sc = 800 + 380, pad + 350, 2200
    d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(0, 0, 0))
    hs = np.radians(np.arange(0, 360, 15))
    ring = np.stack([np.full_like(hs, 0.68), 0.11 * np.cos(hs), 0.11 * np.sin(hs)], -1)
    rgb_in = np.clip(lin_to_srgb(oklab_to_lin(ring)), 0, 1)
    li = lin_to_oklab(srgb_to_lin(rgb_in))
    for Q, col, w in ((R, (120, 120, 120), 1), (P, (200, 120, 0), 2)):
        lo = lin_to_oklab(srgb_to_lin(apply_look(rgb_in, Q)))
        for i in range(len(hs)):
            x0, y0 = cx + li[i, 1] * sc, cy - li[i, 2] * sc
            d.line([x0, y0, cx + lo[i, 1] * sc, cy - lo[i, 2] * sc], fill=col, width=w)
            d.ellipse([cx + lo[i, 1] * sc - 3, cy - lo[i, 2] * sc - 3, cx + lo[i, 1] * sc + 3, cy - lo[i, 2] * sc + 3], fill=col)
    for i in range(len(hs)):
        x0, y0 = cx + li[i, 1] * sc, cy - li[i, 2] * sc
        d.ellipse([x0 - 7, y0 - 7, x0 + 7, y0 + 7], fill=tuple((rgb_in[i] * 255).astype(int)), outline=(0, 0, 0))
    d.text((800, pad + 720), "OKLab a/b: ring of colours (L 0.68, C 0.11) -> where %s (orange) and %s (grey) move them" % (sel, ref), fill=(0, 0, 0))
    # grey-axis tint strip: output OKLab a (green-) and b (blue-) of the grey ramp
    gl = lin_to_oklab(srgb_to_lin(apply_look(np.stack([g, g, g], -1), P)))
    bx, by, bw, bh = 800, pad + 745, 760, 40
    d.rectangle([bx, by - bh, bx + bw, by + bh], outline=(0, 0, 0)); d.line([bx, by, bx + bw, by], fill=(200, 200, 200))
    d.line([(bx + g[i] * bw, by - gl[i, 1] * 1000) for i in range(256)], fill=(200, 0, 120), width=2)
    d.line([(bx + g[i] * bw, by - gl[i, 2] * 1000) for i in range(256)], fill=(200, 160, 0), width=2)
    d.text((bx, by + bh + 4), "grey ramp output OKLab a (magenta, + = magenta) and b (amber, + = yellow), x1000 px", fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Travel 400 look: writes <outdir>/TRV400.CUB (preset TRV400) or <outdir>/<PRESET>.CUB")
    ap.add_argument("--preset", default=DEFAULT, choices=ORDER)
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    csv = _check_csv()
    P = LOOKS[a.preset]
    out = build_cube(P)
    write_cube(os.path.join(a.outdir, a.preset + ".CUB"), P, out)
    c = dict(checks(a.preset, P, out), cub=(a.preset == DEFAULT), csv_checked=bool(csv), log_h_ref=round(LOGH_REF, 3),
             gamma_RGB=np.round(AF_GAMMA, 3).tolist(),
             tone_knots=[[round(x, 4), round(y, 4)] for x, y in P["tone"]], tintL=[[round(v, 4) for v in t] for t in P.get("tintL", [])])
    print(json.dumps(c))
    if a.preset == DEFAULT:
        with open(os.path.join(a.outdir, "checks_trv400.json"), "w") as f:
            json.dump([c], f, indent=1)
        curves_png(os.path.join(a.outdir, "preview_trv400_curves.png"), a.preset)
