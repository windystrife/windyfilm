#!/usr/bin/env python3
"""Chungking 320 (file code CHUNGK), WindyStrife's 5th own film: a Hong Kong 90s night colour
negative look with an ISO 320 pushed feel. Shelf id 30, output mode 11 (the old port).
Copy of gen_fuji_luts.py (same OKLab model) with extra parameters: a neon guard (ts_cfade:
split tone fades on strong colours), a toe fade for the shadow tone (ts_toe), a wider
skin-guard chroma ramp (skin_cmin) and a grey-axis tint curve vs output lightness (tintL,
step 6b, with its guards tl_cfade / skin_tl / skin_tl_kappa / tl_toe).  Every number is a
hand-set or solved parameter; no film scans, frames or third-party LUTs are in this file.

Presets (--preset, default CHUNGK; the output is always CHUNGK.CUB, so the module build takes
whichever preset was written last):
  CHUNGK         shipped default (11-10): colour modelled on the old Artificial Eye R2 DVD of
                 the film, a cool night grade: crushed blue-black, blue shadows, a teal
                 crossover, lime upper mids, green-white highlights, firmer contrast.  = the
                 judged variant COOL with the judge's white-point fix w5 (only the top 4 tone
                 knots raised: out 0.77485 / 0.83111 / 0.89763 / 0.95344 -> 0.778 / 0.84 /
                 0.915 / 0.975).
  CHUNGK_RT      alternate: the old Miramax R1 DVD grade (1998): near-neutral clean blacks,
                 faint pink lower mids, pink-warm upper mids, very slightly cool whites, low
                 contrast, muted reds.
  CHUNGK_NIGHTC  reference only: the first look (NIGHT-C, 11-10), deep green-cyan shadows,
                 crimson reds, amber sodium, neutral mid grey; written byte-identical to the
                 CHUNGK.CUB of commit 0cc1bcf.
Where the DVD numbers come from (MEASURED): area means (OKLab L, C, hue; 8-bit RGB) on screen
captures of the two DVDs, kept outside this repo; the comments in LOOKS quote them as "DVD"
or "AE R2" / "R1" values.  GUESSES: a DVD is a video transfer of a print, not the negative,
so turning its grade into a LUT for a Canon JPEG is a look call; the toe crush, the skin
guards (skin_sat / skin_tl) and the w5 white point are design choices, not measurements.

films/CHUNGK.T17 = CHUNGK.CUB through the module's own loader (/root/ml/yuvre_work/harness
"lut CHUNGK.CUB CHUNGK.T17"), then mk_filmtab.py (build.sh) -> fr_filmtab.h.  To ship
CHUNGK_RT instead: python3 gen_chungking.py --preset CHUNGK_RT <luts_fuji dir>, rerun the
harness line, copy the .T17 to the old port/films/, rebuild (build.sh); the camera only
sees the rebuilt the old module (the own films are compiled in, nothing is read from the card).

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
  6b. Optional grey-axis tint curve tintL (CHUNGK, CHUNGK_RT): OKLab (a,b) offsets as a
     smooth function (pchip) of output lightness, faded on strong colours, on skin and
     right above black.  The DVD grades change the hue of the neutral axis with lightness.
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

usage: python3 gen_chungking.py [--preset CHUNGK|CHUNGK_RT|CHUNGK_NIGHTC] <outdir>
       writes CHUNGK.CUB (the chosen preset), checks_chungk.json and preview_chungk_curves.png
       (all three presets, the chosen one bold); the files of the other looks in the same
       dir are not touched
"""
import sys, os, json, argparse
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
# "DVD" / "AE R2" / "R1" numbers in the comments are MEASURED area means on screen captures of the
# two DVDs (OKLab hue h in degrees, chroma C); everything else is a set or solved parameter.
LOOKS = {
 # ---- shipped default: old Artificial Eye R2 DVD grade (variant COOL + judge white-point fix w5)
 "CHUNGK": dict(
    title="CHUNGK - Chungking 320 (WindyStrife original LUT, old Artificial Eye R2 DVD grade reference)",
    note="Old-DVD cool: crushed blue-black, blue/cyan shadows, teal crossover, lime upper mids, green-white highlights, firmer contrast.",
    # solved (fit_tone): knots at input codes 0,8,16,32,48,64,96,118,140,160,180,200,220,240,255 hit an 8-bit Y601
    # target = the NIGHT-C grey response (stand-in for the R1 tone) through the measured R1 -> AE R2 luma map of
    # matched areas, plus part of the night-street crush in the toe (a choice): deep toe, mid slope x1.08-1.10,
    # log-log slope 40-100 1.64 (NIGHT-C 1.27).  w5 (judge fix): the top 4 knots raised from 0.77485 / 0.83111 /
    # 0.89763 / 0.95344 to 0.778 / 0.84 / 0.915 / 0.975 (white closer to NIGHT-C's 0.972; GUESS, a look call)
    tone=[(0.0, 0.0), (0.03137, 0.0042), (0.06275, 0.01413), (0.12549, 0.05032), (0.18824, 0.10144), (0.25098, 0.16786),
          (0.37647, 0.32827), (0.46275, 0.45822), (0.54902, 0.56717), (0.62745, 0.64764), (0.70588, 0.71222),
          (0.78431, 0.778), (0.86275, 0.84), (0.94118, 0.915), (1.0, 0.975)],
    kappa=0.8,
    #       red  skin  yel   grn   cyan  blue  purp  mag
    # skin anchor -2 (NIGHT-C +2): skin lands at h 51-53 under the lime upper-mid tint (TF1 DVD face p50 h53)
    dH  =  [-2,  -2,   -8,    9,   -5,   -6,   -6,    0],
    S   =  [1.12, 0.98, 1.06, 1.08, 1.02, 0.92, 0.92, 1.05],
    dL  =  [-0.06, 0.0, 0.0, -0.08, -0.05, -0.07, -0.06, -0.03],
    sat=1.04, cknee=0.14, cmax=0.3,
    # shadows a bit fuller than NIGHT-C (0.92 at L .25): the deeper toe already lowers chroma via kappa
    # (AE R2 shadow chroma ~R1 x0.94)
    satL=[(0.0, 0.8), (0.25, 0.92), (0.5, 1.06), (0.75, 1.0), (1.0, 0.75)],
    # split tones off: the neutral axis is set by tintL below
    ts=(0.0, 0.0), ts_range=(0.18, 0.7), th=(0.0, 0.0), th_range=(0.7, 0.97),
    # blue veil = the crushed blue black: grey 0 -> about (2, 1, 7), h ~290 (DVD darkest 1 %: (1.3, 1.0, 7.1) h 280);
    # the max channel stays >= 3 on the test images while luma sits at ~2
    veil=0.0006, veil_rgb=(0.45, 0.4, 1.8),
    # skin_sat 0.3 (NIGHT-C 0.18): deep skin stays brown at L .2-.3 (TF1 DVD deep skin h49 C.038)
    skin_ts=0.95, skin_sat=0.3, skin_cmin=0.025, ts_cfade=(0.045, 0.13, 0.9), ts_toe=0.16,
    # neutral axis per output L: blue shadows h254 C.017 (DVD h253-258 C.013-.018), teal-green crossover ~L .36
    # (two measured areas disagree, h136 / h254: set between), lime upper mids h104-116 C.028 (DVD h94-117 C.031),
    # green-white top h158 C.014 at L .75-.9 (DVD whites h167 C.016), near neutral at L 1
    tintL=[(0.0, 0.0, -0.006), (0.1, -0.001, -0.013), (0.2, -0.006, -0.013), (0.28, -0.009, -0.008), (0.36, -0.01, 0.004),
           (0.45, -0.009, 0.023), (0.55, -0.006, 0.031), (0.65, -0.012, 0.031), (0.74, -0.014, 0.019), (0.83, -0.013, 0.002),
           (0.91, -0.006, 0.0), (1.0, -0.002, 0.0)],
    # tint fades from C .02 to .10 (75 %): oranges / neon / skin keep their hue (AE R2 orange chroma = R1 +0-6 %)
    tl_cfade=(0.02, 0.1, 0.75),
    # tint off on skin (95 % at all L) with a wide hue kernel (kappa 3): the camera's 16-code UV cells blend skin
    # with its unprotected neighbours; a narrow guard (kappa 6) left pale skin at h 75-80
    skin_tl=(0.95, 0.95), skin_tl_kappa=3.0,
    tl_toe=0.1),
 # ---- alternate: old Miramax R1 DVD grade (1998), variant RT (iteration r9 of the fit)
 "CHUNGK_RT": dict(
    title="CHUNGK - Chungking 320: old Miramax R1 DVD grade (variant rt) (WindyStrife original LUT)",
    note="Old DVD (Miramax R1 1998): neutral clean blacks, pink-warm mids, low contrast, orange h40.",
    # mid slope ~1.14 (the least contrasty of the releases), deeper toe for a clean neutral floor, no lift; top 0.98
    tone=[(0, 0), (0.04, 0.010), (0.10, 0.050), (0.20, 0.144), (0.32, 0.278), (0.461, 0.452), (0.62, 0.626), (0.78, 0.780),
          (0.90, 0.889), (1.0, 0.980)],
    kappa=0.8,
    #       red  skin  yel   grn   cyan  blue  purp  mag
    dH  =  [1,    1,   -8,    4,   -3,    2,    0,    0],     # orange stays h37-40 (DVD); green +4 keeps grass from olive-yellow
    S   =  [0.80, 0.97, 0.96, 0.94, 1.00, 0.95, 0.95, 1.00],   # orange ~0.86x NIGHT-C, reds muted (R1 LED/logo reds ~0.7x AE R2 / TF1)
    dL  =  [-0.05, 0.0, 0.0, -0.05, -0.04, -0.06, -0.05, -0.03],
    sat=0.98, cknee=0.14, cmax=0.3,                            # ~0.87x of the Canon JPEG chroma on the test images
    satL=[(0.0, 0.85), (0.25, 0.92), (0.50, 1.00), (0.75, 1.00), (1.0, 0.80)],
    # no green-cyan shadow tone, no highlight tone: the grey axis is set by tintL below
    ts=(0.0, 0.0), ts_range=(0.18, 0.70), th=(0.0, 0.0), th_range=(0.70, 0.97),
    # neutral black floor at 3 codes
    veil=0.0008, veil_rgb=(1.0, 1.0, 1.0),
    skin_ts=0.95, skin_sat=0.18, skin_cmin=0.025, ts_cfade=(0.045, 0.13, 0.9), ts_toe=0.16,
    # neutral axis per output lightness (OKLab a, b): neutral-warm shadows, faint pink L .3-.45, warm pink-orange
    # upper mids (C ~.018 h32), very slightly cool whites (h~265 C~.007)
    tintL=[(0.00, 0.000, 0.000), (0.10, 0.001, 0.002), (0.22, 0.0035, 0.006), (0.37, 0.008, -0.002), (0.52, 0.016, 0.011),
           (0.66, 0.016, 0.012), (0.82, -0.001, -0.008), (0.92, -0.001, -0.008), (1.00, -0.0005, -0.003)],
    tl_cfade=(0.03, 0.10, 0.55),                               # the tint is for neutrals: fades on grass, sky, neon
    skin_tl=0.6),                                              # skin keeps most of its hue (DVD deep skin h42-47)
 # ---- reference: the first Chungking look, NIGHT-C (11-10, commit 0cc1bcf)
 "CHUNGK_NIGHTC": dict(
    title="CHUNGK - Chungking 320: Hong Kong 90s neon negative, night (WindyStrife original LUT)",
    note="Night neon: green-cyan fluorescent cast in mids/shadows, clean neon reds + sodium yellows, deep toe, punchy contrast.",
    # S-curve: crushed toe, steep mids (slope ~1.2), NO lift above 0.75: lifting bright saturated
    # colours above their hue's cusp made gamut_soft kink at the yellow/cyan cube edges.
    # NIGHT-C (11-10) against the night candidate: toe a little less crushed (0.04 -> 0.018,
    # 0.10 -> 0.058 ...), mid grey 118 -> 0.454, skin hue turn 4 -> 2, red chroma 1.20 -> 1.12,
    # blue chroma 0.92 / lightness -0.07 kept, purple 0.92 / -0.06, weaker shadow tone over a
    # narrower range, stronger neon guard and skin guard.
    tone=[(0, 0), (0.04, 0.018), (0.10, 0.058), (0.20, 0.142), (0.32, 0.266), (0.461, 0.454), (0.62, 0.638), (0.78, 0.786), (0.90, 0.889), (1.0, 0.972)],
    kappa=0.80,
    #       red  skin  yel   grn   cyan  blue  purp  mag
    dH  =  [-2,   2,   -8,    9,   -5,   -6,   -6,    0],
    S   =  [1.12, 0.98, 1.06, 1.08, 1.02, 0.92, 0.92, 1.05],
    dL  =  [-0.06, 0.0, 0.0, -0.08, -0.05, -0.07, -0.06, -0.03],
    sat=1.04, cknee=0.14, cmax=0.30,
    satL=[(0.0, 0.80), (0.25, 0.90), (0.50, 1.05), (0.75, 1.0), (1.0, 0.75)],
    ts=(-0.013, 0.0), ts_range=(0.18, 0.70), th=(0.001, 0.005), th_range=(0.70, 0.97),
    veil=0.0016, veil_rgb=(0.85, 1.06, 1.00), skin_ts=0.95, skin_sat=0.18, skin_cmin=0.025, ts_cfade=(0.045, 0.13, 0.9),
    ts_toe=0.16),
}
ORDER = ["CHUNGK", "CHUNGK_RT", "CHUNGK_NIGHTC"]
DEFAULT = "CHUNGK"

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
            wt = wt * (1 - k_sk * w_skt)
        if P.get("tl_cfade"):
            c0, c1, ka = P["tl_cfade"]
            wt = wt * (1 - ka * smoothstep(c0, c1, C3))
        if P.get("tl_toe"):
            wt = wt * smoothstep(0.0, P["tl_toe"], L3)
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
    return apply_look(rgb, P)                               # (n,n,n,3) indexed [b,g,r]

def write_cube(path, P, out):
    n = out.shape[0]
    with open(path, "w", newline="\n") as f:
        f.write('TITLE "%s"\n' % P["title"])
        # (same comment line as the other looks; CHUNGK.CUB / films/CHUNGK.T17 were made with it)
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
def curves_png(path, sel=DEFAULT):
    """all presets; sel (the one written to CHUNGK.CUB) is drawn last and bold"""
    from PIL import Image, ImageDraw
    cols = {"CHUNGK": (40, 110, 200), "CHUNGK_RT": (200, 70, 110), "CHUNGK_NIGHTC": (40, 160, 120)}
    order = [nm for nm in ORDER if nm != sel] + [sel]
    wid = lambda nm, w: w + 2 if nm == sel else w
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
    for nm in order:
        y = luma(apply_look(np.stack([g, g, g], -1), LOOKS[nm]))
        d.line([(ox + g[i] * s, oy - y[i] * s) for i in range(256)], fill=cols[nm], width=wid(nm, 2))
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
    for nm in order:
        lo = lin_to_oklab(srgb_to_lin(apply_look(rgb_in, LOOKS[nm])))
        for i in range(len(hs)):
            d.line([cx + li[i, 1] * sc, cy - li[i, 2] * sc, cx + lo[i, 1] * sc, cy - lo[i, 2] * sc], fill=cols[nm], width=wid(nm, 1))
            d.ellipse([cx + lo[i, 1] * sc - 2, cy - lo[i, 2] * sc - 2, cx + lo[i, 1] * sc + 2, cy - lo[i, 2] * sc + 2], fill=cols[nm])
    d.text((800, pad + 720), "OKLab a/b: ring of colours (L 0.68, C 0.11) -> where each look moves them", fill=(0, 0, 0))
    for k, nm in enumerate(ORDER):
        d.rectangle([800 + k * 250, 780, 820 + k * 250, 800], fill=cols[nm])
        d.text((825 + k * 250, 784), nm + (" (CHUNGK.CUB)" if nm == sel else ""), fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Chungking 320 look: writes <outdir>/CHUNGK.CUB from one preset")
    ap.add_argument("--preset", default=DEFAULT, choices=ORDER, help="look written to CHUNGK.CUB (default %s)" % DEFAULT)
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rep = []
    # the chosen preset goes to CHUNGK.CUB; checks for all presets (the chosen one first, "cub": true)
    for nm in [a.preset] + [k for k in ORDER if k != a.preset]:
        P = LOOKS[nm]
        out = build_cube(P)
        if nm == a.preset:
            write_cube(os.path.join(a.outdir, "CHUNGK.CUB"), P, out)
        c = dict(checks(nm, P, out), cub=nm == a.preset); rep.append(c)
        print(json.dumps(c))
    # own names: gen_fuji_luts.py writes checks.json / preview_curves.png into the same dir
    with open(os.path.join(a.outdir, "checks_chungk.json"), "w") as f:
        json.dump(rep, f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_chungk_curves.png"), a.preset)
