#!/usr/bin/env python3
"""Pastel 400 (file code PAS400, catalogue id 44, type neg), a WindyStrife own film: an ISO 400 professional colour
negative (C-41) with a fourth colour-sensitive layer, exposed one stop over its box speed and rendered by a bright,
soft lab scan: pastel colour, airy highlights, cool-green foliage, soft skin, neutral greys, clean open shadows.
Grain and halation are the module's effects, not this LUT.

The colour path is the OKLab model of gen_chungking.py / gen_port160.py, taken from the batch-1 portrait-negative
generator gen_prt400.py (catalogue/looks/PRT400) by COPYING it: the colour maths, the gamut handling, the OKLab model
(apply_look) and the family module neg_scan (toe_model, fit_toe, neg_scan_channel, neg_scan, neg_grey) are unchanged
copies, so the portrait-negative family shares one scan model.  What is new here: the data (this film's curves), the
grey-card placement and the +1 EV exposure, the fourth layer in the spectral hint, and the PAS400 preset.  No film
scans, frames, photos, third-party LUTs or other film tables are used; every number is either digitised from the
publications below or a GUESS made here.

Sources (MEASURED = digitised or quoted from a maker publication; DERIVED = computed from MEASURED numbers by a stated
rule; GUESS = a look call made here):
  [1] Fujifilm, FUJICOLOR PRO 400H PROFESSIONAL, Fujifilm Product Information Bulletin, Ref. No. AF3-176E
      (EIGI-05.1-FG(HB) 4-1), PDF created 2005-01-19, 8 pages.  Read with WebFetch (copy on asset.fujifilm.com,
      .../2019-09/d68b00b13f1b6234629342317be9cf2b/films_pro-400h_datasheet_01.pdf, md5 61b77af8...); not stored.
      The curves were digitised from the PDF's own vector paths (src/digitise_af3176e.py; no raster tracing).
      MEASURED p.8 fig. 18 "Characteristic Curves" (Daylight 1/125 s, C-41, Status M; no Log H Ref printed)
               -> data/af3176e_p8_fig18_characteristic.csv.  Used by neg_scan.
      MEASURED p.8 fig. 19 "Spectral Sensitivity Curves" (C-41, Status M, density 1.0 above D-min), blue-, green- and
               red-sensitive layers and the dashed "Cyan Sensitive Layer" -> data/af3176e_p8_fig19_spectral_sensitivity.csv.
               Used by the spectral hint (with the fourth layer, see below).
      MEASURED p.8 fig. 20 "MTF Curve", fig. 21 "Spectral Dye Density Curves" -> data/af3176e_p8_fig20_mtf.csv,
               data/af3176e_p8_fig21_dye_density.csv.  Not used by the LUT (the scan model works on Status M densities,
               which already include the dye set); kept for the record.
      MEASURED p.7 sec. 16 "Diffuse RMS Granularity Value": 4 (48 um aperture, density 1.0 above D-min, Fujifilm
               measurement); sec. 17 resolving power 50 / 125 lines/mm -> data/af3176e_p7_granularity_resolution.csv.
               Grain proposal only (README), not the LUT.
      MEASURED p.1 sec. 2: ISO 400/27 deg in daylight.  p.1 sec. 1 features table: "Wide exposure latitude ... Faithful
               reproduction of neutral grays over a wide exposure range from underexposure to overexposure"; "Superb
               skin tone reproduction ... with continuously smooth gradation from the highlights to the shadows without
               any washout"; "Excellent three-dimensional appearance ... Clearer colors in the highlights and
               appropriately controlled color saturation in the shadows".
      MEASURED p.7 sec. 14-1: a fourth colour-sensitive layer "in order to reproduce colors as they are perceived by
               the human eye"; natural colours "even for photographs taken under fluorescent lights or mixed light
               sources".  NOT REPRODUCIBLE HERE: that benefit acts on the light falling on the film; a camera JPEG has
               already been white-balanced and mixed by the camera, so the LUT can only carry the fourth layer's effect
               as one fixed daylight colour transform (the spectral hint below).  Mixed-light behaviour is not modelled.
  [2] Fujifilm, FUJICOLOR SUPERIA X-TRA400, data sheet Ref. No. AF3-0217E (KAMIQ-06.10-FP), PDF 2006-09-04, sec. 17:
      diffuse RMS granularity 4 (same maker, same measurement).  Grain note only (README).
  [3] Colour matching functions: C. Wyman, P.-P. Sloan, P. Shirley, "Simple Analytic Approximations to the CIE XYZ
      Color Matching Functions", JCGT 2(2), 2013 (multi-lobe fit).  Illuminant for the hint: 5500 K Planck (GUESS
      for "Daylight").
  [4] CIE 1931 RGB system (primaries 700 / 546.1 / 435.8 nm; the standard CIE RGB -> XYZ matrix with 1/0.17697):
      r-bar is computed from [3] to compare the size of its negative lobe with the fourth layer's plotted sensitivity
      (cie_rbar_lobe(); printed in checks_pas400.json).  A plausibility check of the coupling GUESS, not a fit.
  [5] Kodak E-4050 (Portra 400) technical data, January 2025, p.4 characteristic curves, "Log H Ref -1.44", as
      digitised by batch-1 PRT400 (copy in data/e4050_characteristic.csv).  Used only for the offset between a
      printed grey-card reference and the same sheet's ISO 5800 speed point (1.006 log H, src/speedpoint.py).

Grey-card placement and the +1 EV (DERIVED, used by neg_scan):
  AF3-176E prints no Log H Ref.  Box speed 400 gives the ISO 5800 speed point log H_m = log10(sqrt(2) / 400) = -2.452;
  the grey card sits 1.006 log H above the speed point, the offset of [5]'s printed reference over its own speed point
  -> LOGH_REF = -1.446.  The look's exposure is +1 EV (ei = +0.301 log H).  Cross-check from [1]'s own curves: the
  layers reach D-min + 0.15 at log H -2.24 (B), -2.21 (G), -2.17 (R), so H_m = -2.186 and sqrt(2)/H_m = ISO 217:
  on the plotted axis the film behaves about one stop slower than its box speed, and +1 EV puts the grey card at
  -1.145, within 0.04 log H of where that curve-derived speed would put it (-1.180).  The absolute calibration of
  [1]'s axis is not stated (GUESS that it is absolute; only the cross-check depends on it).

Fourth layer in the spectral hint (GUESS on the coupling, data from [1] p.8 fig. 19):
  the cyan-sensitive layer (peak about 520 nm) acts on the red-sensitive layer by an interlayer (inhibiting) effect,
  so the red record's effective sensitivity is S_R(l) - k4 * S_C4(l) with both curves taken from the same plotted
  log axis (k4 = 1.0: the published relative sensitivities taken at face value).  Its negative lobe then reaches
  k4 * S_C4(520) / max S_R = 10^(0.42 - 1.06) = 0.23 of the red peak; the CIE r-bar's negative lobe [4] is 0.27 of
  its peak at about 520 nm (cie_rbar_lobe()), so the size is plausible.  Greens and cyans give the red record less
  exposure: the film renders them purer and cooler (claim C4).

Claims (relative, cited; metrics = judge v2's functions on the shipped kernel, numbers in README.md):
  C1 neutral greys at every exposure: "faithful reproduction of neutral grays over a wide exposure range" ([1] p.1)
     -> judge v2 J2 grey tint (max OKLab chroma on the grey ramp, L 0.15..0.95) <= 0.010, half the gate; no tint declared.
  C2 long straight line, no shoulder in the plotted range: the three slopes stay 0.51..0.53 from Ref +1 to the plot end
     (+1.8 log H over the +1 EV grey card), against 0.53..0.58 over Ref -1..+1 ([1] p.8 fig. 18) -> airy highlights:
     metric: output luma slope over input grey 160..230 (display units) >= 0.85 and white >= 248 (no hard clip).
  C3 controlled saturation in the shadows, clearer colour in the highlights ([1] p.1, "three-dimensional appearance")
     -> metric: chroma ratio (film / camera) of the judge images in the shadow band (camera OKLab L < 0.45) at least
     0.08 below the highlight band (L >= 0.75); and lower overall colour than PRT400 (whose sheet claims "exceptional
     color saturation"; [1] makes no saturation claim): J5 synthetic chroma factor below PRT400's (pastel).
  C4 cool-green foliage from the fourth layer ([1] p.7 sec. 14-1, p.8 fig. 19 with the coupling above): the judge's
     foliage ramp turns toward cyan (hue up) against the camera and against PRT400 (output hue at the ramp's mid
     exposures, judge v2 J4 ramp "foliage").

Model (input = display-referred sRGB JPEG, Picture Style Neutral):
  0. neg_scan (family module, unchanged copy) on the grey axis, from [1] p.8 fig. 18: display sRGB -> scene light
     relative to the grey card (cam_toe f = 0.004, GUESS) -> log H = log10(lin' / 0.18) + LOGH_REF + ei (+1 EV) ->
     each layer's density from the softplus toe model fitted to the digitised curve -> the scanner divides each layer
     by its own mid slope (Ref -1..+1), renders 10^(k * e) (scan contrast k, GUESS), subtracts beta * the density of
     code 0 (black point, GUESS), sets white at the JPEG white and grey-balances at the grey card; the layers'
     different toes survive as a small shadow tint scaled by toe_tint (GUESS; kept small for C1).
  1-8. the OKLab model (unchanged copy): per-hue edits (base values + the spectral hint at strengths spec_h / spec_s /
     spec_l), global chroma, tone (neg_scan grey curve, then knots that place the bright lab print), chroma vs
     lightness, skin guards, split tone (off), grey-axis tint (off), neg_scan toe tint, gamut, veil.
  GUESS: everything numeric in LOOKS and the hint strengths.
Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1.

PAS400.T17 = PAS400.CUB through the module's own loader (/root/ml/yuvre_work/harness "lut PAS400.CUB PAS400.T17"),
as Portrait 160 did.

usage: python3 gen_pas400.py [--data DIR] <outdir>
       writes PAS400.CUB, checks_pas400.json (self-checks + provenance) and preview_pas400_curves.png;
       --data defaults to data/ next to this file
"""
import sys, os, json, argparse, csv
import numpy as np

N = 33
OPT = dict(tri_c=1.0, tri_l=0.0, guard=0.0, knee=0.88, hfade=0.75, lfade=0.0)   # as gen_port160.py
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

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


# ------------------------------------------------------------------ family module neg_scan (step 0; used on the grey axis)
# DERIVED (header "Grey-card placement"): box speed 400 -> ISO 5800 speed point log10(sqrt(2) / 400) = -2.452, grey card
# 1.006 log H above it (the offset of [5]'s printed Log H Ref over its own speed point) -> -1.446.  [1] prints no Log H Ref.
ISO_BOX = 400.0
REF_OFFSET = 1.006
LOGH_REF = float(np.log10(np.sqrt(2.0) / ISO_BOX) + REF_OFFSET)
CHAR_CSV = "af3176e_p8_fig18_characteristic.csv"
SPEC_CSV = "af3176e_p8_fig19_spectral_sensitivity.csv"

def read_csv(fn):
    rows = [r for r in csv.reader(l for l in open(fn) if not l.startswith("#")) if r]
    return rows[0], np.array([[float(v) if v != "" else np.nan for v in r] for r in rows[1:]])

def toe_model(x, Dmin, g, x0, s):
    """smooth toe: D-min far below x0, a straight line of slope g above it, softplus of width s between"""
    return Dmin + g * s * np.log10(1 + 10 ** ((np.asarray(x, float) - x0) / s))

def fit_toe(x, D):
    """grid search over (x0, s), linear least squares for (Dmin, g); returns (Dmin, g, x0, s, rms)"""
    best = None
    ss = np.arange(0.05, 0.801, 0.0025)
    for x0 in np.arange(-3.40, -1.80, 0.0025):
        f = ss[:, None] * np.log10(1 + 10 ** ((x[None, :] - x0) / ss[:, None]))      # (ns, n)
        fm, Dm = f.mean(1), D.mean()
        g = ((f - fm[:, None]) * (D - Dm)).sum(1) / ((f - fm[:, None]) ** 2).sum(1)
        Dmin = Dm - g * fm
        r = Dmin[:, None] + g[:, None] * f - D[None, :]
        e = (r ** 2).mean(1); i = int(e.argmin())
        if best is None or e[i] < best[4]:
            best = (float(Dmin[i]), float(g[i]), float(x0), float(ss[i]), float(e[i]))
    return best[0], best[1], best[2], best[3], float(np.sqrt(best[4]))

_FILM = {}
def film_curves(data=None):
    """fitted toe models of the three layers, keyed by the scanner channel they drive (R = red density = cyan layer)"""
    data = data or DATA
    if data not in _FILM:
        _, a = read_csv(os.path.join(data, CHAR_CSV))              # columns logH, D_B, D_G, D_R ([1] p.8 fig. 18)
        a = a[~np.isnan(a[:, 1:]).any(1)]
        x = a[:, 0]
        _FILM[data] = {c: fit_toe(x, a[:, 1 + j]) for j, c in enumerate("BGR")}
    return _FILM[data]

def neg_scan_channel(x, c, ns, data=None):
    """scanner output (linear, white = 1) of channel c for log H x"""
    Dmin, g, x0, s, _ = film_curves(data)[c]
    D = lambda v: toe_model(v, Dmin, g, x0, s)
    ref = LOGH_REF
    gam = (D(ref + 1.0) - D(ref - 1.0)) / 2.0                     # the layer's mid slope (Ref -1..+1)
    e = lambda v: (D(v) - D(ref)) / gam
    k, beta = ns["k"], ns["beta"]
    xw = ref + ns.get("ei", 0.0) + np.log10(1.0 / 0.18)            # log H of the JPEG white (code 255)
    f = ns.get("cam_toe", 0.0)
    if f > 0:   # black point at the darkest density the camera image can put on the film (code 0, see cam_toe)
        Yb = 10 ** (k * e(ref + ns.get("ei", 0.0) + np.log10(f / (1.0 + f) / 0.18)))
    else:       # black point at the film base (D-min) as the scanner sees it
        Yb = 10 ** (k * (Dmin - D(ref)) / gam)
    return (10 ** (k * e(x)) - beta * Yb) / (10 ** (k * e(xw)) - beta * Yb)

def neg_scan(rgb, P, data=None):
    """display sRGB -> film (per-layer density, fitted toe) -> scanner inversion -> display sRGB, per channel.
    Each channel keeps the shape all three layers share; the differences between the layers' curves
    (the film's own shadow tint) are scaled by toe_tint.  The scanner then grey-balances at the grey card
    (a per-channel power that keeps white at 1; lab scanners balance on neutrals), so the layer differences
    only survive in the shadows (toe) and, slightly, in the highlights.  apply_look uses it on the grey axis
    only (neg_grey); per channel on colours it would treat the JPEG's R, G, B as the film's layer exposures,
    which they are not (the colour path is the OKLab model + the spectral hint instead)."""
    ns = P.get("neg")
    if not ns: return rgb
    ei = ns.get("ei", 0.0); tt = ns.get("toe_tint", 1.0)
    # cam_toe (GUESS): the camera JPEG's black is not scene black; a picture-style toe darkens the deepest codes
    # below their scene value, so the scene exposure behind a dark code is taken as (lin + f) / (1 + f): code 0
    # sits about log10(f / 0.18) below the grey card instead of at minus infinity; white and grey barely move
    f = ns.get("cam_toe", 0.0)
    camlin = lambda v: (v + f) / (1.0 + f)
    lin = camlin(srgb_to_lin(rgb))
    x = np.log10(np.maximum(lin, 1e-6) / 0.18) + LOGH_REF + ei
    def chan(v, c):
        own = neg_scan_channel(v, c, ns, data)
        avg = sum(neg_scan_channel(v, cc, ns, data) for cc in "RGB") / 3.0
        return avg + tt * (own - avg)
    xg = np.log10(camlin(0.18) / 0.18) + LOGH_REF + ei
    m = {c: float(chan(np.array(xg), c)) for c in "RGB"}                  # grey card through each channel
    mg = float(np.exp(np.mean([np.log(v) for v in m.values()])))
    out = np.empty_like(lin)
    for j, c in enumerate("RGB"):
        out[..., j] = np.clip(chan(x[..., j], c), 1e-9, 1) ** (np.log(mg) / np.log(m[c]))
    return np.clip(lin_to_srgb(np.clip(out, 0, 1)), 0, 1)

_NEGG = {}
def neg_grey(P, data=None):
    """the film + scan on the grey axis as functions of OKLab L: L -> (L', da, db).  L' is the grey's new
    lightness (the tone, applied hue-preserving in stage 4), (da, db) its colour (the film's toe tint, added in
    stage 6c with the grey-axis tint guards).  None without a neg block."""
    ns = P.get("neg")
    if not ns: return None
    key = (json.dumps(ns, sort_keys=True), data)
    if key not in _NEGG:
        Lg = np.linspace(0.0, 1.0, 2049)
        v = lin_to_srgb(Lg ** 3)
        o = lin_to_oklab(srgb_to_lin(neg_scan(np.stack([v, v, v], -1), P, data)))
        _NEGG[key] = (Lg, o[:, 0], o[:, 1], o[:, 2])
    Lg, Ln, an, bn = _NEGG[key]
    return (lambda L: np.interp(L, Lg, Ln)), (lambda L: np.interp(L, Lg, an)), (lambda L: np.interp(L, Lg, bn))

# ------------------------------------------------------------------ spectral hint (from [1] p.8 fig. 19 and [3])
def _g(l, m, s1, s2):
    return np.exp(-0.5 * ((l - m) / np.where(l < m, s1, s2)) ** 2)

K4 = 1.0     # GUESS: coupling of the fourth (cyan-sensitive) layer's inhibition onto the red record (header)

def layer_sensitivities(data=None, k4=None):
    """(wl, S) with S rows R, G, B (linear relative sensitivity) from [1] p.8 fig. 19; the red row is the effective
    red record S_R - k4 * S_C4 (the fourth layer's interlayer inhibition), also returned separately as (S_R, S_C4)"""
    data = data or DATA
    k4 = K4 if k4 is None else k4
    _, a = read_csv(os.path.join(data, SPEC_CSV))                 # columns nm, blue, green, red, cyan4th (log, relative)
    wl = np.arange(380.0, 705.0, 5.0)
    lin = []
    for j in range(4):
        m = ~np.isnan(a[:, 1 + j])
        lin.append(10 ** np.interp(wl, a[m, 0], a[m, 1 + j], left=-9, right=-9))   # outside the plotted curve: 0
    Sb, Sg, Sr, Sc = lin
    return wl, np.stack([Sr - k4 * Sc, Sg, Sb]), (Sr, Sc)

def cie_rbar_lobe():
    """[4]: CIE 1931 r-bar from the analytic x/y/z-bar of [3] through the CIE RGB -> XYZ matrix; returns (negative lobe
    min / positive max, wavelength of the min), to compare with the fourth layer's plotted size (header)"""
    wl = np.arange(380.0, 781.0, 1.0)
    xb = 1.056 * _g(wl, 599.8, 37.9, 31.0) + 0.362 * _g(wl, 442.0, 16.0, 26.7) - 0.065 * _g(wl, 501.1, 20.4, 26.2)
    yb = 0.821 * _g(wl, 568.8, 46.9, 40.5) + 0.286 * _g(wl, 530.9, 16.3, 31.1)
    zb = 1.217 * _g(wl, 437.0, 11.8, 36.0) + 0.681 * _g(wl, 459.0, 26.0, 13.8)
    M = np.array([[0.49, 0.31, 0.20], [0.17697, 0.81240, 0.01063], [0.0, 0.01, 0.99]]) / 0.17697
    r = (np.linalg.inv(M) @ np.stack([xb, yb, zb]))[0]
    return float(r.min() / r.max()), float(wl[r.argmin()])

def fourth_layer_lobe(data=None, k4=None):
    """size of the fourth layer's negative lobe on the red record: k4 * max S_C4 / max S_R, and its peak wavelength"""
    k4 = K4 if k4 is None else k4
    wl, _, (Sr, Sc) = layer_sensitivities(data, k4)
    return float(k4 * Sc.max() / Sr.max()), float(wl[Sc.argmax()])

_SPEC = {}
def spectral_film_matrix(data=None, T=5500.0):
    """3x3 matrix linear sRGB -> film layer exposures (R = cyan-forming record incl. the fourth layer's inhibition,
    G = magenta-, B = yellow-forming), each row normalised so grey -> 1, for minimum-norm spectra under a Planck
    radiator of T K"""
    data = data or DATA
    key = (data, T, K4)
    if key not in _SPEC:
        wl, S, _ = layer_sensitivities(data)
        xb =1.056 * _g(wl, 599.8, 37.9, 31.0) + 0.362 * _g(wl, 442.0, 16.0, 26.7) - 0.065 * _g(wl, 501.1, 20.4, 26.2)
        yb = 0.821 * _g(wl, 568.8, 46.9, 40.5) + 0.286 * _g(wl, 530.9, 16.3, 31.1)
        zb = 1.217 * _g(wl, 437.0, 11.8, 36.0) + 0.681 * _g(wl, 459.0, 26.0, 13.8)
        lm = wl * 1e-9
        I = 1.0 / (lm ** 5 * (np.exp(1.4388e-2 / (lm * T)) - 1.0)); I /= I.max()
        C = np.stack([xb, yb, zb]) * I
        X2S = np.array([[3.2404542, -1.5371385, -0.4985314], [-0.9692660, 1.8760108, 0.0415560], [0.0556434, -0.2040259, 1.0572252]])
        S2X = np.linalg.inv(X2S)
        wI = C.sum(1)                                              # XYZ of a flat reflector under I
        def H(rgb):
            xyz = (S2X @ rgb.T).T / (S2X @ np.ones(3)) * wI        # sRGB white -> the flat reflector
            rho = (C.T @ np.linalg.solve(C @ C.T, xyz.T)).T        # minimum-norm spectrum
            return rho @ (S * I).T
        Hw = H(np.ones((1, 3)))[0]
        _SPEC[key] = np.stack([H(np.eye(3)[i:i + 1])[0] for i in range(3)], 1) / Hw[:, None]
    return _SPEC[key]

def spectral_hint(data=None, L=0.70, C=0.09):
    """per anchor hue: (hue turn deg, chroma ratio relative to the ring mean, lightness change per unit chroma)"""
    M = spectral_film_matrix(data)
    hs = np.r_[HUES, np.radians(np.arange(0, 360, 5))]
    lab = np.stack([np.full_like(hs, L), C * np.cos(hs), C * np.sin(hs)], -1)
    rgb = np.clip(oklab_to_lin(lab), 0, 1)
    li = lin_to_oklab(rgb); lo = lin_to_oklab(np.clip(rgb @ M.T, 0, None))
    Ci, Co = np.hypot(li[:, 1], li[:, 2]), np.hypot(lo[:, 1], lo[:, 2])
    dh = np.degrees(np.angle(np.exp(1j * (np.arctan2(lo[:, 2], lo[:, 1]) - np.arctan2(li[:, 2], li[:, 1])))))
    ratio = Co / Ci; mean = ratio[len(HUES):].mean()
    n = len(HUES)
    return dh[:n], ratio[:n] / mean, (lo[:n, 0] - li[:n, 0]) / Ci[:n], float(mean)

def hue_params(P, data=None):
    """effective per-hue edits: the preset's base values plus the spectral hint at strengths spec_h/s/l"""
    dH, S, dL = np.array(P["dH"], float), np.array(P["S"], float), np.array(P["dL"], float)
    if P.get("spec_h", 0) or P.get("spec_s", 0) or P.get("spec_l", 0):
        h, r, l, _ = spectral_hint(data)
        dH = dH + P.get("spec_h", 0.0) * h
        S = S * r ** P.get("spec_s", 0.0)
        dL = dL + P.get("spec_l", 0.0) * l
    return dH, S, dL

# ------------------------------------------------------------------ the looks
# tone knots: (input, output) on display-encoded lightness, applied after the neg_scan grey curve (whose toe, black
# point and contrast already shape the tone); here they only place the final brightness and the scan shoulder.
# All numbers are GUESSES (look calls) unless a comment says otherwise.
LOOKS = {
 # ---- the shipped look: +1 EV exposure, bright soft lab scan
 "PAS400": dict(
    title="PAS400 - Pastel 400 (WindyStrife original LUT)",
    note="Four-layer pro negative 400 exposed +1 EV, bright soft scan: pastel colour, airy highlights, cool-green foliage, soft skin, neutral greys.",
    # neg_scan: scan contrast k, camera toe, black point beta, toe tint, EI offset, skin fade of the toe tint (see
    # gen_prt400.py for the meaning of each).  ei +0.301 = the +1 EV exposure (header).  k (GUESS) a little under the
    # camera's own contrast: the bright print stays soft.  toe_tint (GUESS) small: C1 neutral greys.
    neg=dict(k=0.90, cam_toe=0.004, beta=0.97, toe_tint=0.2, ei=0.301, skin=0.8),
    # tone knots after the neg_scan grey curve (GUESS): an open, slightly lifted black (the overexposed negative has
    # density in the shadows, the scanner keeps them clean but not crushed), mids printed bright, a long straight upper
    # mid (C2) and a soft roll-off over the last stop
    tone=[(0.0, 0.030), (0.037, 0.080), (0.112, 0.175), (0.186, 0.265), (0.255, 0.345), (0.399, 0.485), (0.475, 0.560),
          (0.557, 0.635), (0.639, 0.700), (0.792, 0.834), (0.906, 0.929), (0.962, 0.966), (1.0, 0.986)],
    kappa=0.5,
    # base per-hue edits (GUESS; the spectral hint with the fourth layer is added on top, see hue_params)
    #       red  skin  yel   grn   cyan  blue  purp  mag
    #   pastel: less colour everywhere; reds and skin a touch toward pink; yellow toward lemon; greens cooler and lighter
    #   (C4, on top of the hint); sky toward cyan and lighter
    dH  =  [ 2,   8.0,  4,    10,   4,   0,    -2,   -1],
    S   =  [0.78, 0.84, 0.76, 0.84, 0.80, 0.70, 0.80, 0.80],
    dL  =  [ 0.0, 0.01, 0.0,  0.03, 0.02, -0.02, 0.0, 0.0],
    spec_h=0.5, spec_s=1.0, spec_l=0.25,
    sat=1.0, cknee=0.10, cmax=0.21,
    # chroma vs lightness (C3): controlled saturation in the shadows, clearer colour in the highlights, a pastel top
    satL=[(0.0, 0.70), (0.30, 0.78), (0.55, 0.92), (0.75, 0.98), (0.90, 0.88), (1.0, 0.70)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=0.0, veil_rgb=(1.0, 1.0, 1.0),
    skin_ts=0.0, skin_sat=0.25, skin_cmin=0.04,
    skin_hi_dh=(0.72, 0.92, -5.0), skin_pastel_keep=0.6,
    opt=dict(knee=0.8)),
}
ORDER = ["PAS400"]
DEFAULT = "PAS400"

def tone_fn(P):
    """stage 4 tone on OKLab L: first the film + scan grey curve of neg_scan (if the preset has one), then the
    tone knots on display-encoded lightness (here: the final brightness and the scan shoulder)"""
    xs, ys = zip(*P["tone"])
    f = pchip(xs, ys)
    ng = neg_grey(P)
    def g(L):                                     # OKLab L -> L' through display domain
        L = np.clip(L, 0, 1)
        if ng is not None: L = ng[0](L)
        Yd = lin_to_srgb(L ** 3)
        return np.cbrt(srgb_to_lin(f(Yd)))
    return g

def apply_look(rgb, P):
    """rgb: (...,3) display sRGB 0..1  ->  (...,3) display sRGB 0..1"""
    OPT = dict(globals()["OPT"], **P.get("opt", {}))      # per-preset overrides of the model options
    dH_e, S_e, dL_e = hue_params(P)
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
    #    hue_cmin (PAS400, optional; default 0.02 = the family's value): chroma at which the hue moves reach full
    #    strength.  The shipped kernel interpolates near-neutral colours between the neutral node and the first ring of
    #    UV nodes (16 codes out, OKLab C about 0.05); a large hue turn on that ring (the cool-green turn) leaks into
    #    low-chroma skin as a yellow drift of up to +9 deg, so the turns grow in more slowly with chroma.
    wC = smoothstep(0.0, P.get("hue_cmin", 0.02), C)
    #    blue-vertex guard: along the sRGB C-B edge OKLab hue hardly moves, so even a 1-2 deg
    #    turn of a saturated pure blue means a big jump in G -> no hue turn there
    bv = np.exp(-0.5 * (np.angle(np.exp(1j * (h - np.radians(264.05)))) / np.radians(8.0)) ** 2) * smoothstep(0.35, 0.85, q_rgb)
    wC = wC * (1 - bv)
    h1 = h + np.radians(hue_fn(h, dH_e)) * wC * (1 - OPT["hfade"] * edge)
    #    skin weight (review fix 11-09): skin-hued colours with some chroma, used by skin_sat,
    #    skin_ts and the veil tint below so deep skin keeps its warmth in the cool-shadow looks
    #    skin_cmin (CHUNGK): chroma where the weight is full; deep skin in shadow has C ~0.03,
    #    at the default 0.05 it got only half the guard and went olive under the green cast
    w_sk = np.exp(6.0 * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
    #    skin_hi_dh = (L0, L1, deg): pale skin turned by deg (negative = toward red) as the input lightness goes from L0 to
    #    L1.  Lifting a pale warm colour toward white runs its red channel into 255 first, so it drifts toward yellow
    #    ("lemon" highlights on skin); this keeps the palest skin peach.  Wide skin weight (kappa 3), faded on colours at
    #    the cube surface like the other hue moves.
    if P.get("skin_hi_dh"):
        l0, l1, deg = P["skin_hi_dh"]
        w_hi = (np.exp(3.0 * (np.cos(h - np.radians(58.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
                * (1 - smoothstep(0.09, 0.15, C)))           # skin chroma only: saturated oranges / yellows keep their hue
        h1 = h1 + np.radians(deg) * smoothstep(l0, l1, L) * w_hi * (1 - OPT["hfade"] * edge)
    Lc0, Cc0 = cusp(h); Lc1, Cc1 = cusp(h1)
    gam = np.clip(C / Cc0, 0, 1)
    C1 = C * hue_fn(h, S_e) * (1 + (Cc1 / Cc0 - 1) * gam * OPT["tri_c"])
    L1 = np.clip(L + hue_fn(h, dL_e) * C + gam * (Lc1 - Lc0) * OPT["tri_l"], 0, 1)
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
    fL = pchip(xs, ys)(L3)
    #    skin_pastel_keep = k: skin-hued colours keep the fraction k of what the pastel highlights (satL < 1) would take
    #    from them, so pale skin stays skin-coloured while white-ish highlights go pastel
    if P.get("skin_pastel_keep"):
        fL = fL + P["skin_pastel_keep"] * w_sk * np.maximum(1.0 - fL, 0.0) * smoothstep(0.55, 0.75, L3)
    C3 = C3 * fL
    #    skin_sat (review fix 11-09): part of the shadow desaturation is given back to skin hues,
    #    so deep skin does not go grey in the flat / desaturated looks.
    if P.get("skin_sat", 0.0):
        C3 = C3 * (1 + P["skin_sat"] * w_sk * (1 - smoothstep(0.35, 0.65, L3)))
    #    vib = (k, (L0, L1, L2, L3), (c0, c1), k_skin, kappa_skin) (fix round 2, 11-11): extra colour for the soft
    #    colours of the mid tones only (C3, "more colour than PRT160"): gain 1 + k, grown in over output L0..L1 and faded
    #    out over L2..L3 (deep tones and the pastel shoulder keep their colour), faded out over input OKLab chroma
    #    c0..c1 (saturated colours get none, so the cube's gamut top is not bent), and faded by k_skin on skin hues
    #    (skin already gets its own gain; wide hue weight exp(kappa_skin (cos(h - 55) - 1))).  Every weight is a smooth
    #    function of OKLab L, C, h: a weight on the RGB saturation q_rgb changed too fast between the 17^3 nodes in dark
    #    and skin colours (judge v2 J8 kernel p99.9 3), a global or per-hue gain bent the cube top (d2 > 0.06).
    if P.get("vib"):
        kv, (v0, v1, v2, v3), (c0, c1), kvs, kap = P["vib"]
        w_skv = np.exp(kap * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
        wv = smoothstep(v0, v1, L3) * (1 - smoothstep(v2, v3, L3)) * (1 - smoothstep(c0, c1, C)) * (1 - kvs * w_skv)
        C3 = C3 * (1 + kv * wv)
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
    # 6c. neg_scan grey-axis tint: the a/b the film + scanner model leaves on a neutral (the layers' different toes,
    #     scaled by neg.toe_tint), looked up at the input lightness the neg tone used; same neutral weighting, faded
    #     on skin hues (neg.skin, wide kappa 2 weight) and on strong colours (C 0.03..0.10, 80 %)
    ng = neg_grey(P)
    if ng is not None:
        w_skn = np.exp(2.0 * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
        wn = tint_k * (1 - P["neg"].get("skin", 0.8) * w_skn) * (1 - 0.8 * smoothstep(0.03, 0.10, C3))
        a3 = a3 + ng[1](L1) * wn
        b3 = b3 + ng[2](L1) * wn
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
        # (same comment line as the other looks, with this generator named)
        f.write("# original creative LUT, neg_scan + parametric OKLab model (gen_pas400.py); input: camera JPEG sRGB, Picture Style Neutral\n")
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
    """left: the digitised characteristic curves [1] with the fitted toe models; middle: grey tone of both presets
    (luma, and the three channels of the selected one); right: hue ring moves (OKLab a/b)"""
    from PIL import Image, ImageDraw
    cols = {"PAS400": (60, 150, 140)}
    order = [nm for nm in ORDER if nm != sel] + [sel]
    Wd, Hd, pad = 2100, 820, 50
    im = Image.new("RGB", (Wd, Hd), (250, 250, 248)); d = ImageDraw.Draw(im)
    # characteristic curves
    s = 640; ox, oy = pad, pad + s
    d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0))
    X = lambda v: ox + (v + 4.0) / 5.0 * s; Yd = lambda v: oy - v / 4.0 * s
    for k in range(-3, 1):
        d.line([X(k), oy, X(k), oy - s], fill=(225, 225, 225))
    for k in range(1, 4):
        d.line([ox, Yd(k), ox + s, Yd(k)], fill=(225, 225, 225))
    _, a = read_csv(os.path.join(DATA, CHAR_CSV))
    F = film_curves()
    cc = {"B": (40, 60, 200), "G": (30, 150, 60), "R": (210, 40, 40)}
    xx = np.linspace(-3.6, 0.8, 300)
    for j, c in enumerate("BGR"):
        for i in range(a.shape[0]):
            d.ellipse([X(a[i, 0]) - 2, Yd(a[i, 1 + j]) - 2, X(a[i, 0]) + 2, Yd(a[i, 1 + j]) + 2], outline=cc[c])
        Dmin, g, x0, sw, rms = F[c]
        d.line([(X(v), Yd(toe_model(v, Dmin, g, x0, sw))) for v in xx], fill=cc[c], width=2)
        d.text((ox + 8, oy - s + 8 + 14 * j), "%s: Dmin %.3f  slope %.3f  toe x0 %.2f  s %.3f  rms %.4f" % (c, Dmin, g, x0, sw, rms), fill=cc[c])
    ei = LOOKS[sel]["neg"].get("ei", 0.0)
    d.line([X(LOGH_REF), oy, X(LOGH_REF), oy - s], fill=(190, 190, 190))
    d.line([X(LOGH_REF + ei), oy, X(LOGH_REF + ei), oy - s], fill=(90, 90, 90))
    d.line([X(LOGH_REF + ei + np.log10(1 / 0.18)), oy, X(LOGH_REF + ei + np.log10(1 / 0.18)), oy - s], fill=(160, 160, 160))
    d.text((ox, oy + 8), "AF3-176E p.8 fig. 18 (dots) + fitted toe model; lines: box-speed grey, +1 EV grey (dark), JPEG white", fill=(0, 0, 0))
    # grey tone
    ox2 = ox + s + 60
    d.rectangle([ox2, oy - s, ox2 + s, oy], outline=(0, 0, 0))
    for k in range(1, 4):
        d.line([ox2 + k * s / 4, oy, ox2 + k * s / 4, oy - s], fill=(225, 225, 225))
        d.line([ox2, oy - k * s / 4, ox2 + s, oy - k * s / 4], fill=(225, 225, 225))
    d.line([ox2, oy, ox2 + s, oy - s], fill=(170, 170, 170))
    gg = np.linspace(0, 1, 256)
    for nm in order:
        o = apply_look(np.stack([gg, gg, gg], -1), LOOKS[nm])
        if nm == sel:
            for j, c in enumerate("RGB"):
                d.line([(ox2 + gg[i] * s, oy - o[i, j] * s) for i in range(256)], fill=tuple(int(0.5 * v + 120) for v in cc[c]), width=1)
        y = luma(o)
        d.line([(ox2 + gg[i] * s, oy - y[i] * s) for i in range(256)], fill=cols[nm], width=3 if nm == sel else 2)
    d.text((ox2, oy + 8), "grey in (display) -> out (luma; thin: R G B of %s)" % sel, fill=(0, 0, 0))
    # hue ring
    cx, cy, sc = ox2 + s + 60 + 330, pad + 330, 2000
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
            d.line([cx + li[i, 1] * sc, cy - li[i, 2] * sc, cx + lo[i, 1] * sc, cy - lo[i, 2] * sc], fill=cols[nm], width=2 if nm == sel else 1)
            d.ellipse([cx + lo[i, 1] * sc - 2, cy - lo[i, 2] * sc - 2, cx + lo[i, 1] * sc + 2, cy - lo[i, 2] * sc + 2], fill=cols[nm])
    d.text((cx - 330, pad + 690), "OKLab a/b: ring (L 0.68, C 0.11) -> where each preset moves it", fill=(0, 0, 0))
    for k, nm in enumerate(ORDER):
        d.rectangle([cx - 330 + k * 300, 760, cx - 310 + k * 300, 780], fill=cols[nm])
        d.text((cx - 305 + k * 300, 764), nm + ".CUB", fill=(0, 0, 0))
    im.save(path)

def speed_points(data=None):
    """ISO 5800-style speed point per layer of [1] fig. 18 (log H where D reaches D-min + 0.15) and log H_m"""
    _, a = read_csv(os.path.join(data or DATA, CHAR_CSV))
    a = a[~np.isnan(a[:, 1:]).any(1)]
    out = {}
    for j, c in enumerate("BGR"):
        x, d = a[:, 0], a[:, 1 + j]; dmin = d[:6].min(); k = int(np.argmax(d >= dmin + 0.15))
        out[c] = float(x[k - 1] + (dmin + 0.15 - d[k - 1]) / (d[k] - d[k - 1]) * (x[k] - x[k - 1]))
    hm = 0.5 * (out["G"] + max(out.values()))
    return dict(per_layer={c: round(v, 3) for c, v in out.items()}, log_Hm=round(hm, 3), iso_from_curves=round(float(np.sqrt(2) / 10 ** hm), 0))

def provenance(P):
    F = film_curves()
    h, r, l, mean = spectral_hint()
    dH, S, dL = hue_params(P)
    rb, rbw = cie_rbar_lobe(); fl, flw = fourth_layer_lobe()
    ei = P["neg"].get("ei", 0.0)
    return dict(logH_ref_box=round(LOGH_REF, 3), ei=ei, logH_grey_exposed=round(LOGH_REF + ei, 3), speed_points=speed_points(),
                fourth_layer=dict(k4=K4, lobe_vs_red_peak=round(fl, 3), peak_nm=flw, cie_rbar_lobe_vs_peak=round(-rb, 3), cie_rbar_min_nm=rbw),
                film_fit={c: dict(zip(("Dmin", "slope", "toe_x0", "toe_width", "rms"), [round(v, 4) for v in F[c]])) for c in "RGB"},
                film_mid_gamma={c: round(float((toe_model(LOGH_REF + 1, *F[c][:4]) - toe_model(LOGH_REF - 1, *F[c][:4])) / 2), 4) for c in "RGB"},
                spectral_matrix=np.round(spectral_film_matrix(), 4).tolist(), spectral_ring_mean_chroma=round(mean, 3),
                spectral_hint_at_anchors=dict(dH=np.round(h, 2).tolist(), S_rel=np.round(r, 3).tolist(), dL_per_C=np.round(l, 3).tolist()),
                effective_per_hue=dict(dH=np.round(dH, 2).tolist(), S=np.round(S, 3).tolist(), dL=np.round(dL, 3).tolist()))

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Pastel 400 look: writes <outdir>/PAS400.CUB")
    ap.add_argument("--preset", default="all", choices=["all"] + ORDER, help="cube(s) to write (default: all = %s)" % " + ".join(ORDER))
    ap.add_argument("--data", default=None, help="digitised source csv dir (default: data/ next to this file)")
    ap.add_argument("outdir")
    a = ap.parse_args()
    if a.data: DATA = a.data
    os.makedirs(a.outdir, exist_ok=True)
    rep = []
    for nm in ORDER:
        P = LOOKS[nm]
        out = build_cube(P)
        written = a.preset in ("all", nm)
        if written:
            write_cube(os.path.join(a.outdir, nm + ".CUB"), P, out)
        c = dict(checks(nm, P, out), cub=(nm + ".CUB") if written else None, default=nm == DEFAULT, provenance=provenance(P)); rep.append(c)
        print(json.dumps({k: v for k, v in c.items() if k != "provenance"}))
    with open(os.path.join(a.outdir, "checks_pas400.json"), "w") as f:
        json.dump(rep, f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_pas400_curves.png"), DEFAULT)
