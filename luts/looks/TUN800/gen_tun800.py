#!/usr/bin/env python3
"""Tungsten 800 (file code TUN800, catalogue id 35, type neg), a WindyStrife own film: a 500T-class
tungsten-balanced (3200 K) motion-picture colour negative without rem-jet, rated EI 800 and
lab-scanned.  Built on gen_port160.py (same OKLab colour model and knobs) plus the neg_scan family
module (PLAN 6 step 2), which turns the published characteristic curve into the tone curve, and a few
small additions to the colour path, all optional parameters that are absent (= no change) in the
other films' looks: headroom, skin_gather, tl_cool, veil_keep_hue, neg comp; plus GAP_TOL in
gamut_soft (see CODE CHANGES).  Every look number is either digitised from the datasheet named below,
derived from it by the stated model, or a GUESS (named as such); no film scans, frames, photos,
third-party LUTs or other film emulations are in this file.

Camera side (catalogue row, not this file): descriptor WB 3200 K, ISO 800 (code 96), picture style
Neutral 0x84 with 0 parameters.  The JPEG the table sees is therefore already balanced for 3200 K:
tungsten-lit scenes arrive neutral, daylight scenes arrive blue (physically right, as with the stock
shot in daylight without its 85 filter).  The table must not undo either: it carries the stock tone,
teal-leaning shadows and warm highlights, and leaves neutral mid greys neutral (PLAN 6.3, J-TUNG).
The red halos around lights are the module's halation effect (C800T profile), not this table.

Presets (--preset, default TUN800; the output is always TUN800.CUB):
  TUN800      shipped default (see LOOKS for the numbers and their origin).
  TUN800_FLAT reference only: neg_scan tone alone (no colour edits, no tints), to show what the
              datasheet curve does by itself.  Never shipped.

SOURCES
  [K1] Kodak Publication H-1-5219t, "KODAK VISION3 500T Color Negative Film 5219 / 7219 / SO-219,
       Technical Data", July 2015 ("Revised 7-15"), 5 pages (kodak.com, read through WebFetch).
         p.1  Exposure Indexes: Tungsten (3200K) 500; Daylight 320 with an 85 filter.
              "2 stops of extended highlight latitude".  Base with rem-jet backing (the class
              modelled here is the same emulsion with the rem-jet removed: no anti-halation layer).
         p.4  fig. TI2647F Sensitometric Curves (3200K tungsten 1/50 s, ECN-2, status M):
              digitised from the PDF vector paths -> h1_5219t_p4_TI2647F_characteristic.csv.
              fig. TI2647C Diffuse rms Granularity (-> ..._TI2647C_granularity_paths.csv),
              fig. TI2647D Spectral Sensitivity (-> ..._TI2647D_spectral_sensitivity.csv).
         p.3  fig. TI2647B MTF: red layer lowest at high frequency (read off the page; the red
              layer sits nearest the base: supports red halation = effect, not table).
         p.5  fig. TI2647E Spectral Dye Density (-> ..._TI2647E_dye_density_paths.csv).
  [D1] Dmitri, "Kodak Vision 3 500T 5219/7219 Film Review", analog.cafe, 2025-05-05 (upd.
       2026-01-22): daylight without filter gives a blue/teal cast; accurate colour under ~3200 K;
       shadows are the weak point in cool light; a push gives a little more blue noise in shadows;
       C-41 cross-processing gives more contrast than ECN-2.
  [D2] BastianK, "Analogue Adventures - Part 7" (rem-jet-free 800T stock), phillipreeve.net,
       2022-05-30: red halos around point lights (also in contrasty daylight); more contrast and a
       narrower range than the same 500T emulsion processed ECN-2.
MEASURED (from [K1], digitised; the csv files carry publication / revision / page / figure)
  - straight line from about -3.5 to +4 camera stops, slopes (status M, D per stop over -2..+3):
    R 0.143, G 0.167, B 0.162 (gamma 0.47 / 0.56 / 0.54); toe from about -3.5 stops, D-min reached
    by about -6.5 stops; gentle shoulder only above about +4..5 stops (8 stops plotted).
  - per-channel toe shapes, each normalised by its own straight-line slope, differ by <= 0.07 stop
    down to -6 stops: a neutral scan stays neutral in the shadows (the data do NOT predict a teal
    shadow; see GUESS below).
DERIVED (neg_scan, model stated in neg_scan_tone): EI 800 = 2/3 stop under the box speed, so the
  scene's shadows land on the toe about 3 stops below mid grey: compressed, soft shadow separation;
  no film shoulder inside the JPEG's range, so the highlight roll-off is the scanner's.
CLAIMS (relative, checked by judge_tun800.py as J7; final values)
  C1 mid-grey contrast below CHUNGK (1.391) and CINE250D_PRINT (1.131), above ETERNA (0.888), within
     0.92..1.08 of the Canon JPEG: 0.932 ([K1] p.4: one long straight line, scanned normal).
  C2 grey mids neutral, |a|,|b| <= 0.015 for codes 80..200: 0.0077 ([K1] p.1 EI 500 at 3200 K, WB 3200 K).
  C3 shadow / mid contrast below the JPEG's: 0.762 vs 1.0 (EI 800 = 2/3 stop under, shadows on the toe).
  C4 highlights keep separating up to white: slope over codes 200..245 1.109 > 0.6, white
     [252, 251, 249] within 248..252 ([K1] p.1 extended highlight latitude, p.4 no shoulder before +4).
GUESS (look calls; [D1]/[D2] give only the direction; final numbers in LOOKS / NEG_TUN800 / TINT_TUN800)
  - the JPEG inverse (Canon Neutral ~ sRGB of scene light with mid grey 18.4 % at code 118 and a
    shoulder putting code 255 at +3.0 stops, top 3.0).
  - the scanner: each channel normalised to its straight-line slope (chan 1 = green layer for the
    grey), contrast k 0.92 (soft lab scan), comp 0.15 (the lab corrects only 15 % of the 2/3-stop
    under-rating: mid grey 118 -> 113), black point beta 0.55 / qb 0.25 (black 18 codes), white 0.9855
    (code 255 -> 249..252; 0.985 left 4 quantisation-level L reversals on the judge-v2 denim ramp,
    0.9875 put R at 253).
  - the neutral-axis tint TINT_TUN800: teal shadows (hue ~200, C up to 0.026 at L .18, gone by L .36),
    neutral L .36..70, amber highlights (hue ~68, C up to 0.021 at L .94), neutral again at white.
  - richer colour: sat 1.22 (chroma x1.08 on the test images) with a soft knee C 0.12..0.28; satL
    (shadows and the top lose some chroma); per-hue edits dH / S / dL (richer orange-red reds, greens
    and blues turned toward cyan, blues held back a little, purples / magentas a touch richer).
  - guards: tint off on skin and strong colours (skin_tl, tl_cfade), grows in from black (tl_toe);
    skin keeps chroma in the shadows (skin_sat 0.30); skin hues gathered toward 55 deg (skin_gather
    0.15); deep skin keeps its pre-veil hue (veil_keep_hue 1.0); the warm highlight tint off on cyan /
    blue near-whites (tl_cool 1.0, 1.0, 215: a daylight sun at WB 3200 K stays cyan, not mint).
  - grain row Neg 800T x1.0, halation C800T x1.0 (catalogue row; the effects, not this file).
CODE CHANGES vs gen_port160.py
  - GAP_TOL 1e-3 in gamut_soft: OKLab constant-hue rays toward the blue primary leave sRGB by up to
    -7e-4 linear in one channel; the strict in-gamut test made pure blues look 15-20 % out of gamut and
    their compression broke cube luma monotonicity.  Only colours at the sRGB surface move.
  - new optional keys (absent = old behaviour): headroom (q0, q1) (default 0.35, 1.0 = the old fixed
    values), skin_gather, tl_cool, veil_keep_hue (step 8b), neg comp (default 1.0).
DISTINCTNESS (judge v2, mean dE OKLab x100 over its sample set; >= 2.0 required, 2..3 = owner note)
  PRONEG 2.46, C250X6B 2.58, CINE250D_PRINT 2.61, CINE250D 2.63, PRT400 2.85, C250PF 2.93, SUN200 2.96,
  C250F 3.11, ETERNA 3.52, CCHROME 4.00, CLASNEG 4.03, CHUNGK 4.68, PORT160 4.68, PRS400 10.64
  (the uncorrected camera JPEG: 2.36, information only).

Model (input = display-referred sRGB JPEG, Picture Style Neutral, WB from the descriptor):
  0. neg_scan (this file): the tone knots come from the characteristic curve (see neg_scan_tone).
  1-8. as gen_port160.py / gen_chungking.py: OKLab per-hue edits, global chroma + soft knee, tone on
     display lightness with chroma following (kappa), chroma vs lightness, split tone (unused),
     grey-axis tint curve tintL with skin / strong-colour / cool / toe guards, soft gamut compression,
     veil (the lifted black) with the warm-hue keep.
Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1.

films/TUN800.T17 = TUN800.CUB through the module's own loader (/root/ml/yuvre_work/harness
"lut TUN800.CUB TUN800.T17"), then the pack builder (mk_filmpack.py, batch 0).

usage: python3 gen_tun800.py [--preset TUN800|TUN800_FLAT] <outdir>
       writes TUN800.CUB (the chosen preset), checks_tun800.json and preview_tun800_curves.png
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

GAP_TOL = 1e-3

def gamut_soft(L, C, h, knee=0.88):
    """Soft compression into sRGB at constant hue.  Points are moved along a line toward an
    anchor on the grey axis: same L for weak colours / anything below the cusp, sliding to the
    cusp lightness for strong colours above it (bright saturated yellows/greens get a little
    darker instead of washing out).  Below knee * boundary nothing changes.
    TUN800 fix (11-10): OKLab's constant-hue line from grey to the blue vertex leaves sRGB on the way (one
    channel dips to -7e-4 linear at most, then comes back to 0 at pure blue), so the bisection found that first
    exit and treated pure blues as 15-20 % out of gamut: (0,0,112) came out (0,15,100) even with no look, and its
    neighbours did not, which broke luma monotonicity along R and G.  The in-gamut test tolerates -1e-3 below 0
    (GAP_TOL); the final clip removes that dip (<= 2 display codes, only in the gap)."""
    Lc, Cc = cusp(h)
    w = smoothstep(0.0, 1.0, C / np.maximum(Cc, 1e-6))
    L0 = L - np.maximum(L - Lc, 0.0) * w
    ca, sa = np.cos(h), np.sin(h)
    lo = np.zeros_like(L); hi = np.full_like(L, 1.25)
    for _ in range(24):
        mid = 0.5 * (lo + hi)
        Lm = L0 + mid * (L - L0); Cm = mid * C
        rgb = oklab_to_lin(np.stack([Lm, Cm * ca, Cm * sa], -1))
        ok = np.all((rgb >= -GAP_TOL) & (rgb <= 1 + 1e-7), -1)
        lo = np.where(ok, mid, lo); hi = np.where(ok, hi, mid)
    q = 1.0 / np.maximum(lo, 1e-6)
    s = np.where(q > knee, softknee(q, knee, 0.999) / q, 1.0)
    return L0 + s * (L - L0), s * C

# ------------------------------------------------------------------ neg_scan family module (PLAN 6 step 2)
# [K1] p.4 fig. TI2647F, digitised from the PDF vector paths (h1_5219t_p4_TI2647F_characteristic.csv),
# density (status M, ECN-2) every 1/2 camera stop from -8 to +8; 0 stops = normal exposure at EI 500, 3200 K.
NEG_STOPS = np.arange(-16, 17) / 2.0
NEG_D = {
 "R": [0.189, 0.193, 0.198, 0.204, 0.220, 0.244, 0.274, 0.309, 0.354, 0.409, 0.477, 0.547, 0.617, 0.688, 0.758, 0.829, 0.900,
       0.972, 1.043, 1.115, 1.187, 1.259, 1.330, 1.401, 1.469, 1.534, 1.595, 1.654, 1.709, 1.759, 1.798, 1.832, 1.862],
 "G": [0.585, 0.586, 0.590, 0.597, 0.616, 0.643, 0.676, 0.715, 0.767, 0.832, 0.912, 0.995, 1.078, 1.162, 1.247, 1.331, 1.415,
       1.498, 1.581, 1.664, 1.747, 1.831, 1.915, 1.998, 2.078, 2.154, 2.228, 2.299, 2.366, 2.427, 2.478, 2.522, 2.562],
 "B": [0.840, 0.843, 0.847, 0.849, 0.865, 0.890, 0.918, 0.954, 1.001, 1.068, 1.150, 1.235, 1.320, 1.401, 1.481, 1.562, 1.642,
       1.722, 1.802, 1.883, 1.964, 2.046, 2.129, 2.210, 2.289, 2.364, 2.435, 2.503, 2.569, 2.625, 2.667, 2.707, 2.745]}
MID_LIN = 0.184                      # mid grey 18.4 % linear = sRGB code 118 (the judges' mid grey)

def neg_density(c, f):
    """status-M density of layer c at film exposure f (camera stops, 0 = EI 500 normal); flat D-min below
    -8 stops, last slope beyond +8 (not reached: the JPEG ends near +3 scene stops)"""
    d = np.asarray(NEG_D[c]); f = np.asarray(f, float)
    lo = np.interp(f, NEG_STOPS, d)
    s_hi = (d[-1] - d[-2]) / 0.5
    return np.where(f > NEG_STOPS[-1], d[-1] + (f - NEG_STOPS[-1]) * s_hi, lo)

def neg_slope(c):
    """straight-line slope (D per stop) of layer c, least squares over -2..+3 stops (the straight part)"""
    m = (NEG_STOPS >= -2) & (NEG_STOPS <= 3)
    return float(np.polyfit(NEG_STOPS[m], np.asarray(NEG_D[c])[m], 1)[0])

def jpeg_to_stops(v, top):
    """GUESS: Canon Neutral JPEG ~ sRGB-encoded scene light, mid grey at code 118, with a shoulder that
    puts code 255 at +top stops (expansion above +1 stop, C1-continuous). v display 0..1 -> scene stops"""
    s = np.log2(np.maximum(srgb_to_lin(np.asarray(v, float)), 1e-6) / MID_LIN)
    s0, s1 = 1.0, np.log2(1.0 / MID_LIN)
    a = (top - s0) / (s1 - s0) - 1.0
    t = np.clip((s - s0) / (s1 - s0), 0, None)
    return np.where(s > s0, s0 + (s - s0) * (1 + a * t), s)

def stops_to_jpeg(s, top):
    """inverse of jpeg_to_stops (bisection on the display value), used as the scanner's display rendering"""
    s = np.asarray(s, float)
    lo = np.zeros_like(s); hi = np.ones_like(s)
    for _ in range(40):
        mid = 0.5 * (lo + hi); up = jpeg_to_stops(mid, top) < s
        lo = np.where(up, mid, lo); hi = np.where(up, hi, mid)
    return 0.5 * (lo + hi)

def neg_scan_rgb(v, Q):
    """display grey v (0..1) -> scanned display RGB (..., 3) through the negative and the scanner model.
    Q = LOOKS[...]["neg"]:
      top    JPEG inverse shoulder (GUESS)          dEI   exposure offset in stops (log2(500/800), MEASURED+rating)
      k      scanner contrast on scan stops (GUESS) beta  how much of film base + fog the black point removes (GUESS)
      qb     black point, stops below the base+fog level (GUESS)
      white  display value the scanner puts the JPEG white at (GUESS)
      chan   1 = per-layer densities (each normalised by its own straight-line slope), 0 = green layer for all
      comp   fraction of the rating offset the lab's scan corrects (GUESS; 1 = the scan puts the EI 800 mid grey
             back at mid grey, < 1 = the frame keeps part of the 2/3-stop under-rating: denser, lower-key scan)"""
    s = jpeg_to_stops(v, Q["top"])
    f = s + Q["dEI"]; fmid = Q["dEI"] * Q.get("comp", 1.0)
    out = []
    for c in "RGB":
        cc = c if Q.get("chan", 1) else "G"
        sl = neg_slope(cc)
        q = (neg_density(cc, f) - neg_density(cc, fmid)) / sl                   # scan stops, 0 = mid grey
        qmin = (neg_density(cc, -9.0) - neg_density(cc, fmid)) / sl               # film base + fog
        qb = qmin - Q["qb"]
        lin = 2.0 ** (Q["k"] * q); lb = Q["beta"] * 2.0 ** (Q["k"] * qb)
        lin = (lin - lb) / (1.0 - lb)                                             # black point, mid grey kept at 1
        out.append(lin)
    lin = np.stack(out, -1) * MID_LIN
    # the scanner's display rendering = the JPEG's own (stops_to_jpeg), so with no film (q = s) this is identity
    s_w = np.log2(np.maximum(lin, 1e-9) / MID_LIN)
    disp = stops_to_jpeg(s_w, Q["top"])
    return disp

def neg_scan_grey_lin(Q, v):
    """scanned grey (green layer for all three), linear light, with the white point applied by a linear-light gain so
    that v = 1 -> display Q["white"] (the mid moves by the same factor)"""
    v = np.asarray(v, float)
    gl = srgb_to_lin(neg_scan_rgb(np.r_[v, 1.0], dict(Q, chan=0))[:, 1])
    return gl[:-1] * srgb_to_lin(Q["white"]) / gl[-1]

def neg_scan_veil(Q):
    """the scan's black (film base + fog left by the black point), linear light: carried by the model's veil step
    (lin * (1 - v) + v), which adds a constant in linear light and so keeps luma monotonic at the black corner"""
    return float(neg_scan_grey_lin(Q, [0.0])[0])

def neg_scan_tone(Q, n=40):
    """tone knots (display in, display out) for the grey axis: the scanned grey minus the veil, renormalised,
    f(0) = 0 (so tone + veil = the scanned grey exactly).  The per-layer difference goes into tintL (neg_chan_tint)."""
    v = np.r_[0.0, np.geomspace(0.004, 1.0, n - 1)]
    vl = neg_scan_veil(Q)
    core = (neg_scan_grey_lin(Q, v) - vl) / (1.0 - vl)
    return list(zip(v.tolist(), lin_to_srgb(np.maximum(core, 0.0)).tolist()))

def neg_chan_tint(Q, Ls=(0.0, 0.08, 0.15, 0.25, 0.35, 0.5, 0.65, 0.8, 0.9, 1.0)):
    """OKLab (a, b) of the scanned grey with per-layer curves minus the green-only grey, at output L:
    the MEASURED part of the neutral-axis tint (tiny: the toes match within 0.07 stop)"""
    v = np.linspace(0.0, 1.0, 256)
    rgb = neg_scan_rgb(v, dict(Q, chan=1)); grey = neg_scan_rgb(v, dict(Q, chan=0))
    lab = lin_to_oklab(srgb_to_lin(rgb)); labg = lin_to_oklab(srgb_to_lin(grey))
    L = labg[:, 0]; da = lab[:, 1] - labg[:, 1]; db = lab[:, 2] - labg[:, 2]
    o = np.argsort(L)
    return [(float(x), float(np.interp(x, L[o], da[o])), float(np.interp(x, L[o], db[o]))) for x in Ls]

# ------------------------------------------------------------------ the looks
# neg: the neg_scan parameters.  dEI is the rating offset (EI 500 box speed at 3200 K, [K1] p.1, rated EI 800:
# log2(500/800) = -0.678 stop); everything else in neg is a GUESS (the scanner and the JPEG inverse):
#   k 0.92      scanner contrast a little under 1 on scan stops (soft lab scan: lifted toe, open highlights)
#   comp 0.15   the lab corrects only 15 % of the 2/3-stop under-rating (a denser, lower-key frame: mid grey
#               118 -> 114); with k this keeps the look away from both the brighter flat and the darker
#               contrasty cine candidates (distinctness, J5)
#   beta 0.55   the black point removes 55 % of film base + fog: a lifted, soft black (18 codes, < J2's 25)
#   white 0.9855 the JPEG white lands at 249..252 per channel (not clipped: claim C4; J2 white floor 248 after the
#               warm tint).  0.985 left 4 quantisation-level L reversals on the judge-v2 denim ramp, 0.9875 put R at 253
NEG_TUN800 = dict(top=3.0, dEI=float(np.log2(500.0 / 800.0)), k=0.92, beta=0.55, qb=0.25, white=0.9855, chan=1, comp=0.15)

def _tint(Q, guess):
    """tintL knots = the MEASURED per-layer part (neg_chan_tint, at the same L) + the GUESS look tint"""
    Ls = [g[0] for g in guess]
    m = neg_chan_tint(Q, Ls)
    return [(L, ga + ma, gb + mb) for (L, ga, gb), (_, ma, mb) in zip(guess, m)]

# GUESS look tint of the neutral axis (OKLab a, b vs output L): teal shadows (hue ~205, C up to 0.026 at L .18),
# kept below L .36 so the camera table does not carry it into deep skin (its U/V nodes are ~0.04 C apart, the skin
# fade cannot be resolved closer to grey than that); neutral mids L .36-.70 (J-TUNG: grey 80..200 within 0.015, here
# 0.007 through the kernel); warm amber highlights (hue ~68, C up to 0.021 at L .94), back to neutral at white.
# Direction from [D1] (cool / blue-teal shadows) and the class's lab-scan convention; amounts are look calls.
TINT_TUN800 = [(0.0, 0.0, 0.0), (0.06, -0.012, -0.006), (0.12, -0.022, -0.010), (0.18, -0.024, -0.011), (0.24, -0.014, -0.0065),
               (0.30, -0.005, -0.0025), (0.36, 0.0, 0.0), (0.70, 0.0, 0.0), (0.78, 0.002, 0.005), (0.84, 0.004, 0.010),
               (0.89, 0.007, 0.018), (0.94, 0.008, 0.020), (0.975, 0.002, 0.005), (1.0, 0.0, 0.0)]

LOOKS = {
 # ---- shipped default
 "TUN800": dict(
    title="TUN800 - Tungsten 800 (WindyStrife original LUT)",
    note="Tungsten cine negative at EI 800, lab scan: datasheet toe, lifted soft black, denser mids, rich colour, teal shadows, warm highlights.",
    neg=NEG_TUN800,
    tone=neg_scan_tone(NEG_TUN800),
    kappa=0.8,
    # GUESS per-hue edits: reds a little richer and toward orange-red (lamps, tail lights), skin kept (a touch less
    # chroma against the global gain), greens turned toward cyan and deeper, cyan / blue toward cyan, blues held
    # back a little (the surface blues), purples / magentas a touch richer
    #       red   skin  yel   grn   cyan  blue  purp  mag
    dH  =  [ 4,    0,   -4,    8,   -4,   -6,   -4,    0],
    S   =  [1.10, 0.96, 1.00, 0.94, 0.98, 0.94, 1.02, 1.04],
    dL  =  [-0.01, 0.0, 0.0, -0.04, -0.02, -0.03, -0.02, 0.0],
    # GUESS: richer colour than the camera JPEG (chroma x1.08 on the test images), soft knee from C 0.12 to 0.28
    sat=1.22, cknee=0.12, cmax=0.28,
    # shadows lose some colour (on the toe, 2/3 stop under), highlights roll off
    satL=[(0.0, 0.85), (0.25, 0.92), (0.50, 1.0), (0.80, 1.0), (1.0, 0.82)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=neg_scan_veil(NEG_TUN800), veil_rgb=(1.0, 1.0, 1.0),
    skin_ts=0.0, skin_sat=0.30, skin_cmin=0.03,
    tintL=_tint(NEG_TUN800, TINT_TUN800),
    # the shadow tint is for neutrals: off on skin and on strong colours, grows in from black
    tl_cfade=(0.04, 0.16, 1.0), skin_tl=(1.0, 0.85), skin_tl_kappa=2.0, tl_toe=0.06,
    # the warm highlight tint is for neutrals and warm colours: off on cyan / blue near-whites (review fix: a daylight
    # sun at WB 3200 K, OKLab hue 200, went mint green, 173 deg; now 189 through the camera table)
    tl_cool=(1.0, 1.0, 215.0),
    # deep skin keeps its hue through the black lift; skin hues gathered a little toward 55 deg (see apply_look)
    veil_keep_hue=1.0, skin_gather=0.15),
 # ---- reference only: the datasheet tone alone
 "TUN800_FLAT": dict(
    title="TUN800_FLAT - neg_scan tone only (reference, not shipped)",
    note="neg_scan tone of the datasheet curve, no colour edits, no tints.",
    neg=NEG_TUN800,
    tone=neg_scan_tone(NEG_TUN800),
    kappa=0.8,
    dH=[0] * 8, S=[1.0] * 8, dL=[0.0] * 8,
    sat=1.0, cknee=0.30, cmax=0.40,
    satL=[(0.0, 1.0), (0.5, 1.0), (1.0, 1.0)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=neg_scan_veil(NEG_TUN800), veil_rgb=(1.0, 1.0, 1.0)),
}
ORDER = ["TUN800", "TUN800_FLAT"]
DEFAULT = "TUN800"

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
    #    skin_gather (TUN800): skin hues are pulled toward 55 deg by a fraction of their distance (broad weight,
    #    kappa 4, full from C 0.04), the "skin tone line" of the datasheet's skin claim ([K1] p.1, a GUESS as to
    #    how); it also cancels the spread the black lift and the shadow tint give deep / yellowish skin
    if P.get("skin_gather", 0.0):
        dsk = np.angle(np.exp(1j * (h - np.radians(55.0))))
        h1 = h1 - P["skin_gather"] * np.exp(4.0 * (np.cos(dsk) - 1.0)) * smoothstep(0.01, 0.04, C) * dsk * (1 - OPT["hfade"] * edge)
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
    #     headroom = (q0, q1) where the fade starts / is complete (default 0.35, 1.0); TUN800 ends it a little
    #     earlier so the gain is gone before the surface (bright saturated blues next to the surface kept their
    #     last few % of gain and lost luma against the surface colour).
    hq0, hq1 = P.get("headroom", (0.35, 1.0))
    g = C3 / np.maximum(C, 1e-9)
    g = np.where(g > 1, 1 + (g - 1) * (1 - smoothstep(hq0, hq1, q_rgb) * smoothstep(0.03, 0.08, C)), g)
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
        #       tl_cool = (amount, kappa[, centre]) (TUN800 review fix): the upper (warm) part of the tint, L > .50..70, fades
        #                                 on cool colours (broad hue weight centred on 230 deg or centre, from C .01..04), so a
        #                                 cyan near-white (a daylight sun at WB 3200 K) is not turned mint green by
        #                                 the amber offset; greys and warm colours keep it.
        if P.get("tl_cool"):
            kc, kk = P["tl_cool"][:2]; hc = P["tl_cool"][2] if len(P["tl_cool"]) > 2 else 230.0
            w_cool = np.exp(kk * (np.cos(h1 - np.radians(hc)) - 1.0)) * smoothstep(0.01, 0.04, C3)
            wt = wt * (1 - kc * w_cool * smoothstep(0.50, 0.70, L3))
        a3 = a3 + pchip(kx, kta)(L3) * wt
        b3 = b3 + pchip(kx, ktb)(L3) * wt
    # 7. gamut: soft compression at constant hue (see gamut_soft)
    C4 = np.hypot(a3, b3); h4 = np.arctan2(b3, a3)
    L5, C5 = gamut_soft(L3, C4, h4, P.get("knee", OPT["knee"]))
    lin = oklab_to_lin(np.stack([L5, C5 * np.cos(h4), C5 * np.sin(h4)], -1))
    lin = np.clip(lin, 0, 1)
    # 8. veil (lifted blacks, optionally tinted)
    v = P["veil"]
    if v > 0:
        vr = np.asarray(P["veil_rgb"], float); vr = vr / (0.2126 * vr[0] + 0.7152 * vr[1] + 0.0722 * vr[2])
        #    a tinted veil goes neutral on skin (same skin_ts weight), the lift itself stays
        k = (P.get("skin_ts", 0.0) * w_sk)[..., None]
        lin = lin * (1 - v) + v * (vr * (1 - k) + k)
        # 8b. veil_keep_hue (TUN800): the lift is added in linear light, which turns the OKLab hue of dark warm
        #     colours toward red (deep skin at L .30: 46 -> 41 deg at a black of 18 codes).  On warm hues (broad
        #     weight centred on skin, 55 deg) the pre-veil hue is given back; lightness and chroma of the veiled
        #     colour are kept, so the lift itself (and the neutral axis) is unchanged.
        vk = P.get("veil_keep_hue", 0.0)
        if vk:
            lab_v = lin_to_oklab(lin)
            Cv = np.hypot(lab_v[..., 1], lab_v[..., 2]); hv = np.arctan2(lab_v[..., 2], lab_v[..., 1])
            wk = vk * np.exp(3.0 * (np.cos(h4 - np.radians(55.0)) - 1.0)) * smoothstep(0.005, 0.03, C5)
            hn = hv + wk * np.angle(np.exp(1j * (h4 - hv)))
            lin = np.clip(oklab_to_lin(np.stack([lab_v[..., 0], Cv * np.cos(hn), Cv * np.sin(hn)], -1)), 0, 1)
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
        # (same comment line as the other looks, with this generator named)
        f.write("# original creative LUT, parametric OKLab model (gen_tun800.py); input: Canon JPEG sRGB\n")
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
    """left: grey tone (display in -> out luma) of every preset + identity; middle: the datasheet curves [K1] p.4
    TI2647F (digitised) with the EI 800 mid grey and the JPEG's scene range; right: hue ring moves of sel"""
    from PIL import Image, ImageDraw
    cols = {"TUN800": (200, 90, 40), "TUN800_FLAT": (90, 90, 90)}
    order = [nm for nm in ORDER if nm != sel] + [sel]
    Wd, Hd, pad = 2300, 820, 50
    im = Image.new("RGB", (Wd, Hd), (250, 250, 248)); d = ImageDraw.Draw(im)
    s = 700; ox, oy = pad, pad + s
    d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0))
    for k in range(1, 4):
        d.line([ox + k * s / 4, oy, ox + k * s / 4, oy - s], fill=(220, 220, 220))
        d.line([ox, oy - k * s / 4, ox + s, oy - k * s / 4], fill=(220, 220, 220))
    d.line([ox, oy, ox + s, oy - s], fill=(170, 170, 170))
    g = np.linspace(0, 1, 256)
    for nm in order:
        out = apply_look(np.stack([g, g, g], -1), LOOKS[nm])
        if nm == sel:
            for ch, cc in enumerate([(220, 60, 60), (40, 160, 60), (60, 90, 220)]):
                d.line([(ox + g[i] * s, oy - out[i, ch] * s) for i in range(256)], fill=cc, width=1)
        y = luma(out)
        d.line([(ox + g[i] * s, oy - y[i] * s) for i in range(256)], fill=cols[nm], width=3 if nm == sel else 2)
    d.text((ox, oy + 8), "grey in (display) -> out luma (thick) and R/G/B of %s (thin); grey line = no change" % sel, fill=(0, 0, 0))
    # middle: characteristic curves
    ox2 = 800; s2w, s2h = 700, 700
    d.rectangle([ox2, oy - s2h, ox2 + s2w, oy], outline=(0, 0, 0))
    X = lambda st: ox2 + (st + 8) / 16 * s2w
    Y = lambda D: oy - D / 3.0 * s2h
    for st in range(-8, 9, 2): d.line([X(st), oy, X(st), oy - s2h], fill=(225, 225, 225))
    Q = LOOKS[sel]["neg"]
    lo, hi = jpeg_to_stops(np.array([1 / 255.0]), Q["top"])[0] + Q["dEI"], Q["top"] + Q["dEI"]
    d.rectangle([max(X(lo), ox2 + 2), oy - s2h + 2, min(X(hi), ox2 + s2w - 2), oy - 2], outline=(240, 200, 120))   # clipped to the panel (code 1 sits below -8 stops)
    d.line([X(Q["dEI"]), oy, X(Q["dEI"]), oy - s2h], fill=(240, 140, 60), width=2)
    for c, cc in zip("RGB", [(220, 60, 60), (40, 160, 60), (60, 90, 220)]):
        dd = NEG_D[c]
        d.line([(X(NEG_STOPS[i]), Y(dd[i])) for i in range(len(dd))], fill=cc, width=2)
    d.text((ox2, oy + 8), "[K1] p.4 TI2647F density vs camera stops (-8..+8); orange line = mid grey at EI 800,", fill=(0, 0, 0))
    d.text((ox2, oy + 22), "box = scene range of the JPEG (code 1 .. 255) on the film", fill=(0, 0, 0))
    # right: hue ring
    cx, cy, sc = 1950, pad + 350, 2000
    d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(0, 0, 0))
    hs = np.radians(np.arange(0, 360, 15))
    ring = np.stack([np.full_like(hs, 0.68), 0.11 * np.cos(hs), 0.11 * np.sin(hs)], -1)
    rgb_in = np.clip(lin_to_srgb(oklab_to_lin(ring)), 0, 1)
    li = lin_to_oklab(srgb_to_lin(rgb_in))
    lo_ = lin_to_oklab(srgb_to_lin(apply_look(rgb_in, LOOKS[sel])))
    for i in range(len(hs)):
        x0, y0 = cx + li[i, 1] * sc, cy - li[i, 2] * sc
        d.ellipse([x0 - 7, y0 - 7, x0 + 7, y0 + 7], fill=tuple((rgb_in[i] * 255).astype(int)), outline=(0, 0, 0))
        d.line([x0, y0, cx + lo_[i, 1] * sc, cy - lo_[i, 2] * sc], fill=cols[sel], width=2)
        d.ellipse([cx + lo_[i, 1] * sc - 3, cy - lo_[i, 2] * sc - 3, cx + lo_[i, 1] * sc + 3, cy - lo_[i, 2] * sc + 3], fill=cols[sel])
    d.text((1560, pad + 720), "OKLab a/b: ring (L 0.68, C 0.11) -> where %s moves it" % sel, fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Tungsten 800 look: writes <outdir>/TUN800.CUB from one preset")
    ap.add_argument("--preset", default=DEFAULT, choices=ORDER, help="look written to TUN800.CUB (default %s)" % DEFAULT)
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rep = []
    for nm in [a.preset] + [k for k in ORDER if k != a.preset]:
        P = LOOKS[nm]
        out = build_cube(P)
        if nm == a.preset:
            write_cube(os.path.join(a.outdir, "TUN800.CUB"), P, out)
        c = dict(checks(nm, P, out), cub=nm == a.preset, neg=P["neg"], tone_knots=[(round(x, 4), round(y, 4)) for x, y in P["tone"]],
                 tintL=[tuple(round(v, 5) for v in t) for t in P.get("tintL", [])])
        rep.append(c)
        print(json.dumps({k: v for k, v in c.items() if k not in ("tone_knots",)}))
    with open(os.path.join(a.outdir, "checks_tun800.json"), "w") as f:
        json.dump(rep, f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_tun800_curves.png"), a.preset)
