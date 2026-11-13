#!/usr/bin/env python3
"""Cine 50D (file code CIN50D, catalogue id 50, type neg, batch 4), a WindyStrife own film: a 50D-class
daylight-balanced (5500 K) ECN-2 motion-picture colour negative without rem-jet, lab-scanned for stills.
Shown ISO 50, camera ISO 100 (code 72; the camera's lowest), WB 5200 K, Picture Style Neutral 0x84 with 0
parameters (catalogue row, not this file).

Built on gen_tun800.py (the gen_port160.py OKLab colour model with TUN800's optional keys and GAP_TOL) with two
family modules copied unchanged in their maths:
  - neg_scan (from gen_tun800.py, the ECN-2 cine member of the neg family): the published characteristic curves
    -> the grey tone curve and the measured per-layer neutral tint;
  - spectral hint (from gen_prt400.py): the published spectral sensitivities -> per-hue hue / chroma / lightness
    edits (minimum-norm spectra of linear sRGB colours under a 5500 K Planck radiator, CIE CMF fit [W1]).
The data are read from data/*.csv (digitised from [K1]; each csv names publication, revision, page and figure).
Every look number is MEASURED (digitised), DERIVED (by the stated model) or a GUESS (named as such).  No film
scans, frames, photos, third-party LUTs, other film emulations or anything of the original module author's
(descriptors, curve / lw / cmap tables, LUTs, any pack of another module, texts, samples or dumps of them) are in this file.

Presets (--preset, default CIN50D; the output is always CIN50D.CUB):
  CIN50D       shipped default (see LOOKS).
  CIN50D_FLAT  reference only: the neg_scan tone alone (no colour edits, no hint).  Never shipped.

SOURCES
  [K1] Kodak Publication H-1-5203, "KODAK VISION3 50D Color Negative Film 5203 / 7203, Technical Data / Color
       Negative Film", March 2026 ("Revised 3-26"), kodak.com (read through WebFetch):
         p.1  "Kodak's finest grain motion picture film ... pristine, clean images that are full of color and
              detail"; "unrivaled highlight latitude"; "An Anti-halation undercoat replaces the traditional remjet
              backing layer"; Exposure Indexes: Daylight (5500K) 50, Tungsten (3200K) 12 with 80A.
         p.2  Scanning: "up to two stops more image information can be extracted from scene highlights".
         p.3-4 the same figures as [K1b] (raster images; checked by eye against the digitised vector curves).
  [K1b] Kodak Publication H-1-5203t, same film, Technical Data, July 2015 ("Revised 7-15") (mirror of the Kodak
       PDF, read through WebFetch; vector figures, digitised from their paths, the PDF is not kept):
         p.4  Sensitometric Curves (5500K Daylight 1/50 s, ECN-2, Status M) -> data/h1_5203_p4_sensitometric.csv
         p.4  Spectral Sensitivity Curves (1/10 s, ECN-2, Status M, D 0.2 > D-min) -> data/h1_5203_p4_spectral_sensitivity.csv
         p.4  Diffuse rms Granularity Curves (48 um) -> data/h1_5203_p4_rms_granularity.csv (grain row proposal, README)
         p.5  Spectral Dye Density Curves -> data/h1_5203_p5_spectral_dye_density.csv (provenance only)
         p.1  (2015 text: "acetate safety base with rem-jet backing"; the 2026 revision replaces rem-jet by an
              anti-halation undercoat, [K1] p.1)
  [K2] Kodak Publication H-1-5207, "KODAK VISION3 250D Color Negative Film 5207 / 7207", March 2026 ("Revised 3-26"),
       kodak.com: p.3 Diffuse rms Granularity Curves (raster, read with the same raster method as the 50D figure ->
       data/h1_5203_5207_granularity_raster_read.csv): claim C1 only.
  [D1] Dmitri, "Kodak Vision 3 50D 5203/7203 Film Review", analog.cafe, 2024-11-12 (updated 2025-07-23): natural
       colour in most light, high colour accuracy, excellent dynamic range, very fine grain; saturation rises in warm
       light.  Direction only (no amounts).
  [W1] C. Wyman, P.-P. Sloan, P. Shirley, "Simple Analytic Approximations to the CIE XYZ Color Matching Functions",
       JCGT 2(2), 2013 (multi-lobe fit; via the copied spectral-hint module).
MEASURED (from [K1b], digitised)
  - straight line from about -3.5 to beyond +6 camera stops; slopes over -2..+3 stops (D per plotted stop):
    R 0.147, G 0.176, B 0.170 (0.468 / 0.558 / 0.539 per log10 H on the top axis); D-min R 0.17, G 0.58, B 0.86;
    density at normal exposure R 0.94, G 1.45, B 1.70; a gentle shoulder only in the last stop plotted (+7..+8).
  - per-channel toes, each normalised by its own straight-line slope, differ by < 0.1 stop down to -6 stops: a
    neutral scan stays neutral (the measured per-layer tint below is tiny, |a|,|b| < 0.004).
  - spectral sensitivity peaks: yellow-forming 469 nm, magenta-forming 549 nm, cyan-forming 649 nm.
DERIVED
  - dEI = log2(50 / 100) = -1 stop: the camera at ISO 100 gives the EI 50 film one stop less exposure than its box
    speed ([K1] p.1); the lab scan corrects the density (comp 1.0, GUESS), so the scene's deep shadows sit one stop
    further down the toe: soft, compressed deep shadows over a clean black; no film shoulder anywhere in the JPEG's
    range (+3 scene stops = +2 film stops), so the highlight roll-off is the scanner's.
  - the spectral hint (the copied module, from the [K1b] sensitivities): hue turn / chroma ratio / lightness change
    per anchor hue, printed in checks_cin50d.json (provenance.spectral_hint_at_anchors).
CLAIMS (relative; checked on the shipped kernel, values in README / claims_cin50d.json)
  C1 finer grain than the 250D-class stock ([K1] p.3 vs [K2] p.3 granularity, same raster read): grain row Neg 50
     below Neg 250 (effect, not this table).
  C2 mid contrast in the VISION3 family's place ([K1b] p.4 G 0.558 per log H, the same as the 500T-class sheet's
     0.56 used by TUN800): judge-v2 mid slope within +-0.08 of TUN800's and above the Cine 250D's flat mids.
  C3 highlight latitude ([K1] p.1-2, [K1b] p.4 no shoulder before +7): highlights keep separating up to white
     (slope over grey codes 200..245 >= 0.8) and white 248..252 (not clipped).
  C4 clean neutral daylight colour ([K1] p.1 daylight balance, [D1]): grey mids neutral (|a|,|b| <= 0.01 for grey
     codes 40..220) and natural, slightly fuller colour than the camera (J5 chroma factor 1.00..1.15).
GUESS (look calls; final numbers in NEG_CIN50D / LOOKS)
  - the JPEG inverse (shared with TUN800: Canon Neutral ~ sRGB of scene light, mid grey 18.4 % at code 118, a
    shoulder putting code 255 at +3.0 stops).
  - the scanner: per-layer normalisation to the straight-line slope, contrast k, black point beta / qb, white, comp.
  - the colour: global chroma, satL, per-hue base edits, the hint strengths spec_h / spec_s / spec_l, skin guards.
  - grain row Neg 50 (new, from [K1b] p.4 granularity), halation C800T x0.6 (catalogue row; README).
CODE CHANGES vs gen_tun800.py: data read from csv (1/4-stop curves instead of the embedded 1/2-stop table);
  hue_params (spectral hint) added to apply_look; everything else (colour model, neg_scan maths, guards,
  GAP_TOL) unchanged.

Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1.
films/CIN50D.T17 = CIN50D.CUB through the module's own loader (/root/ml/yuvre_work/harness
"lut CIN50D.CUB CIN50D.T17"), then the pack builder (mk_filmpack.py, batch 0).

usage: python3 gen_cin50d.py [--preset CIN50D|CIN50D_FLAT] <outdir>   (reads data/ next to this file)
       writes CIN50D.CUB (the chosen preset), checks_cin50d.json and preview_cin50d_curves.png
"""
import sys, os, json, argparse, csv
import numpy as np

N = 33
OPT = dict(tri_c=1.0, tri_l=0.0, guard=0.0, knee=0.88, hfade=0.75, lfade=0.0)   # as gen_port160.py / gen_tun800.py
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

# ------------------------------------------------------------------ colour maths (as gen_tun800.py)
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

_CUSP = None
def cusp(h):
    """(L, C) of the sRGB gamut cusp at hue h (table, 0.5 deg; smoothed, as gen_tun800.py)"""
    global _CUSP
    if _CUSP is None:
        t = np.linspace(0, 1, 6000, endpoint=False)[:, None]
        corners = np.array([[1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 1, 1], [0, 0, 1], [1, 0, 1], [1, 0, 0]], float)
        loop = np.concatenate([(1 - t) * corners[k] + t * corners[k + 1] for k in range(6)])
        lab = lin_to_oklab(loop)
        hh = np.mod(np.arctan2(lab[:, 2], lab[:, 1]), 2 * np.pi)
        o = np.argsort(hh)
        hh, Lc, Cc = hh[o], lab[o, 0], np.hypot(lab[o, 1], lab[o, 2])
        hh, Lc, Cc = np.r_[hh[-1] - 2 * np.pi, hh, hh[0] + 2 * np.pi], np.r_[Lc[-1], Lc, Lc[0]], np.r_[Cc[-1], Cc, Cc[0]]
        g = np.radians(np.arange(0, 360, 0.25))
        Lg, Cg = np.interp(g, hh, Lc), np.interp(g, hh, Cc)
        k = np.exp(-0.5 * (np.arange(-128, 129) / 32.0) ** 2); k /= k.sum()
        blur = lambda v: np.convolve(np.r_[v[-128:], v, v[:128]], k, mode="valid")
        Lg, Cg = blur(Lg), blur(Cg)
        _CUSP = (np.r_[g, 2 * np.pi], np.r_[Lg, Lg[0]], np.r_[Cg, Cg[0]])
    hs, Lc, Cc = _CUSP
    hd = np.mod(h, 2 * np.pi)
    return np.interp(hd, hs, Lc), np.interp(hd, hs, Cc)

GAP_TOL = 1e-3                        # gen_tun800.py fix: the OKLab blue-vertex gap (see there)

def gamut_soft(L, C, h, knee=0.88):
    """soft compression into sRGB at constant hue (unchanged from gen_tun800.py, GAP_TOL included)"""
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

# ------------------------------------------------------------------ data ([K1b], digitised; see data/*.csv headers)
def read_csv(fn):
    rows = [r for r in csv.reader(l for l in open(fn) if not l.startswith("#")) if r]
    return rows[0], np.array([[float(v) if v not in ("", "nan") else np.nan for v in r] for r in rows[1:]])

_DAT = {}
def neg_data(data=None):
    """camera stops (1/4 stop, -8..+8; 0 = normal exposure at EI 50, 5500 K) and status-M densities R, G, B"""
    data = data or DATA
    if data not in _DAT:
        _, a = read_csv(os.path.join(data, "h1_5203_p4_sensitometric.csv"))
        _DAT[data] = (a[:, 0], {"R": a[:, 2], "G": a[:, 3], "B": a[:, 4]})
    return _DAT[data]

# ------------------------------------------------------------------ neg_scan family module (copied from gen_tun800.py)
MID_LIN = 0.184                      # mid grey 18.4 % linear = sRGB code 118 (the judges' mid grey)

def neg_density(c, f):
    """status-M density of layer c at film exposure f (camera stops, 0 = EI 50 normal); flat D-min below -8 stops,
    last slope beyond +8 (not reached: the JPEG ends near +3 scene stops = +2 film stops)"""
    st, D = neg_data()
    d = D[c]; f = np.asarray(f, float)
    lo = np.interp(f, st, d)
    s_hi = (d[-1] - d[-2]) / (st[-1] - st[-2])
    return np.where(f > st[-1], d[-1] + (f - st[-1]) * s_hi, lo)

def neg_slope(c):
    """straight-line slope (D per stop) of layer c, least squares over -2..+3 stops (the straight part)"""
    st, D = neg_data()
    m = (st >= -2) & (st <= 3)
    return float(np.polyfit(st[m], D[c][m], 1)[0])

def jpeg_to_stops(v, top):
    """GUESS (shared with TUN800): Canon Neutral JPEG ~ sRGB-encoded scene light, mid grey at code 118, with a shoulder
    that puts code 255 at +top stops (expansion above +1 stop, C1-continuous). v display 0..1 -> scene stops"""
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
    """display grey v (0..1) -> scanned display RGB (..., 3) through the negative and the scanner model (gen_tun800.py).
    Q: top (JPEG inverse, GUESS), dEI (exposure offset in stops, DERIVED), k (scanner contrast, GUESS), beta / qb (black
    point, GUESS), white (GUESS), chan (1 = per-layer densities, 0 = green layer for all), comp (fraction of the rating
    offset the lab's scan corrects, GUESS)"""
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
    s_w = np.log2(np.maximum(lin, 1e-9) / MID_LIN)
    return stops_to_jpeg(s_w, Q["top"])

def neg_scan_grey_lin(Q, v):
    """scanned grey (green layer for all three), linear light, white point by a linear-light gain (v = 1 -> Q["white"])"""
    v = np.asarray(v, float)
    gl = srgb_to_lin(neg_scan_rgb(np.r_[v, 1.0], dict(Q, chan=0))[:, 1])
    return gl[:-1] * srgb_to_lin(Q["white"]) / gl[-1]

def neg_scan_veil(Q):
    """the scan's black (film base + fog left by the black point), linear light: carried by the model's veil step"""
    return float(neg_scan_grey_lin(Q, [0.0])[0])

def neg_scan_tone(Q, n=40):
    """tone knots (display in, display out) for the grey axis: the scanned grey minus the veil, renormalised, f(0) = 0"""
    v = np.r_[0.0, np.geomspace(0.004, 1.0, n - 1)]
    vl = neg_scan_veil(Q)
    core = (neg_scan_grey_lin(Q, v) - vl) / (1.0 - vl)
    return list(zip(v.tolist(), lin_to_srgb(np.maximum(core, 0.0)).tolist()))

def neg_chan_tint(Q, Ls=(0.0, 0.08, 0.15, 0.25, 0.35, 0.5, 0.65, 0.8, 0.9, 1.0)):
    """OKLab (a, b) of the scanned grey with per-layer curves minus the green-only grey, at output L: the MEASURED part of
    the neutral-axis tint"""
    v = np.linspace(0.0, 1.0, 256)
    rgb = neg_scan_rgb(v, dict(Q, chan=1)); grey = neg_scan_rgb(v, dict(Q, chan=0))
    lab = lin_to_oklab(srgb_to_lin(rgb)); labg = lin_to_oklab(srgb_to_lin(grey))
    L = labg[:, 0]; da = lab[:, 1] - labg[:, 1]; db = lab[:, 2] - labg[:, 2]
    o = np.argsort(L)
    return [(float(x), float(np.interp(x, L[o], da[o])), float(np.interp(x, L[o], db[o]))) for x in Ls]

# ------------------------------------------------------------------ spectral hint (copied from gen_prt400.py; [K1b] p.4 + [W1])
def _g(l, m, s1, s2):
    return np.exp(-0.5 * ((l - m) / np.where(l < m, s1, s2)) ** 2)

_SPEC = {}
def spectral_film_matrix(data=None, T=5500.0):
    """3x3 matrix linear sRGB -> film layer exposures (R = cyan-forming, G = magenta-, B = yellow-forming), each row
    normalised so grey -> 1, for minimum-norm spectra under a Planck radiator of T K ([K1] p.1: daylight 5500 K)"""
    data = data or DATA
    key = (data, T)
    if key not in _SPEC:
        _, a = read_csv(os.path.join(data, "h1_5203_p4_spectral_sensitivity.csv"))
        wl = np.arange(380.0, 705.0, 5.0)
        S = np.zeros((3, wl.size))
        for j in range(3):                                         # csv columns: yellow, magenta, cyan
            m = ~np.isnan(a[:, 1 + j])
            S[j] = 10 ** np.interp(wl, a[m, 0], a[m, 1 + j], left=-9, right=-9)   # outside the plotted curve: 0
        S = S[::-1]                                                # -> R (cyan layer), G, B
        xb = 1.056 * _g(wl, 599.8, 37.9, 31.0) + 0.362 * _g(wl, 442.0, 16.0, 26.7) - 0.065 * _g(wl, 501.1, 20.4, 26.2)
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
    """per anchor hue: (hue turn deg, chroma ratio relative to the ring mean, lightness change per unit chroma, mean)"""
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
    """effective per-hue edits: the preset's base values plus the spectral hint at strengths spec_h/s/l (gen_prt400.py)"""
    dH, S, dL = np.array(P["dH"], float), np.array(P["S"], float), np.array(P["dL"], float)
    if P.get("spec_h", 0) or P.get("spec_s", 0) or P.get("spec_l", 0):
        h, r, l, _ = spectral_hint(data)
        dH = dH + P.get("spec_h", 0.0) * h
        S = S * r ** P.get("spec_s", 0.0)
        dL = dL + P.get("spec_l", 0.0) * l
    return dH, S, dL

# ------------------------------------------------------------------ the looks
# neg: dEI = log2(50 / 100) = -1 (DERIVED: [K1] p.1 EI 50 at 5500 K, camera ISO 100); the rest is a GUESS (scanner):
NEG_CIN50D = dict(top=3.0, dEI=float(np.log2(50.0 / 100.0)), k=0.90, beta=0.80, qb=0.25, white=0.985, chan=1, comp=1.0)

def _tint(Q, guess):
    """tintL knots = the MEASURED per-layer part (neg_chan_tint, at the same L) + the GUESS look tint"""
    Ls = [g[0] for g in guess]
    m = neg_chan_tint(Q, Ls)
    return [(L, ga + ma, gb + mb) for (L, ga, gb), (_, ma, mb) in zip(guess, m)]

# no look tint (C4 clean neutral): only the measured per-layer part
TINT_CIN50D = [(L, 0.0, 0.0) for L in (0.0, 0.08, 0.15, 0.25, 0.35, 0.5, 0.65, 0.8, 0.9, 0.97, 1.0)]

LOOKS = {
 "CIN50D": dict(
    title="CIN50D - Cine 50D (WindyStrife original LUT)",
    note="Daylight cine negative 50, lab scan: soft datasheet toe over a clean black, long open highlights, clean neutral greys, natural full colour.",
    neg=NEG_CIN50D,
    tone=neg_scan_tone(NEG_CIN50D),
    kappa=0.6,
    #       red   skin  yel   grn   cyan  blue  purp  mag
    dH  =  [ 0,    0,    0,    0,    0,    0,    0,    0],
    S   =  [1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00],
    dL  =  [0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0],
    spec_h=1.0, spec_s=1.0, spec_l=0.25,
    sat=1.10, cknee=0.14, cmax=0.30,
    satL=[(0.0, 0.90), (0.30, 0.97), (0.50, 1.0), (0.85, 1.0), (1.0, 0.85)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=neg_scan_veil(NEG_CIN50D), veil_rgb=(1.0, 1.0, 1.0),
    skin_ts=0.0, skin_sat=0.20, skin_cmin=0.03,
    tintL=_tint(NEG_CIN50D, TINT_CIN50D),
    tl_cfade=(0.04, 0.16, 1.0), skin_tl=(1.0, 0.85), skin_tl_kappa=2.0, tl_toe=0.06,
    veil_keep_hue=1.0, skin_gather=0.35),
 "CIN50D_FLAT": dict(
    title="CIN50D_FLAT - neg_scan tone only (reference, not shipped)",
    note="neg_scan tone of the datasheet curve, no colour edits, no hint, no tints.",
    neg=NEG_CIN50D,
    tone=neg_scan_tone(NEG_CIN50D),
    kappa=0.6,
    dH=[0] * 8, S=[1.0] * 8, dL=[0.0] * 8,
    sat=1.0, cknee=0.30, cmax=0.40,
    satL=[(0.0, 1.0), (0.5, 1.0), (1.0, 1.0)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=neg_scan_veil(NEG_CIN50D), veil_rgb=(1.0, 1.0, 1.0)),
}
ORDER = ["CIN50D", "CIN50D_FLAT"]
DEFAULT = "CIN50D"

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

RETUNE = {'CIN50D': {'smooth': (0.03, 0.06, 12), 'mono': 1.0, 'gk': 4.0, 'gc': 0.09, 'gather': 1.1, 'skl_lo': 0.4, 'dh': (3, 3, 0, 0, 0, 0, 0, 0), 'sat': 0.91648, 'k': 0.87128, 'black': -0.02056, 'shoulder': 0.31517, 'wa': -0.0, 'wb': -0.0, 'sa': -0.0003, 'sb': 0.00357}}  # RETUNE-LINE
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
    """rgb: (...,3) display sRGB 0..1  ->  (...,3) display sRGB 0..1  (gen_tun800.py apply_look + hue_params)"""
    dH_e, S_e, dL_e = hue_params(P)
    lab = lin_to_oklab(srgb_to_lin(rgb))
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    C = np.hypot(a, b); h = np.arctan2(b, a)
    mx, mn = rgb.max(-1), rgb.min(-1)
    q_rgb = (mx - mn) / (mx - mn + 2 * np.minimum(mn, 1 - mx) + 1e-6)
    edge = smoothstep(0.50, 1.0, q_rgb) * smoothstep(0.03, 0.08, C)
    # 2. per-hue (blue-vertex guard, skin_gather, skin weight: as gen_tun800.py)
    wC = smoothstep(0.0, 0.02, C)
    bv = np.exp(-0.5 * (np.angle(np.exp(1j * (h - np.radians(264.05)))) / np.radians(8.0)) ** 2) * smoothstep(0.35, 0.85, q_rgb)
    wC = wC * (1 - bv)
    h1 = h + np.radians(hue_fn(h, dH_e)) * wC * (1 - OPT["hfade"] * edge)
    if P.get("skin_gather", 0.0):
        dsk = np.angle(np.exp(1j * (h - np.radians(55.0))))
        h1 = h1 - P["skin_gather"] * np.exp(P.get("skin_gather_k", 4.0) * (np.cos(dsk) - 1.0)) * smoothstep(0.01, 0.04, C) * smoothstep(np.radians(P.get("skin_gather_lo", -90.0)), np.radians(P.get("skin_gather_lo", -90.0) + 12.0), h) * (1.0 - smoothstep(P.get("skin_gather_c0", 9.0), P.get("skin_gather_c0", 9.0) + 0.06, C)) * dsk * (1 - OPT["hfade"] * edge)
    #    skin_dark (retune R["sdk"], degrees): pre-bias of dark skin hue; the 17^3 table renders the darkest low-chroma skins redder than the cube
    if P.get("skin_dark", 0.0):
        h1 = h1 + np.radians(P["skin_dark"]) * np.exp(8.0 * (np.cos(h - np.radians(50.0)) - 1.0)) * (1.0 - smoothstep(0.28, 0.48, L)) * smoothstep(0.01, 0.04, C)
    if P.get("skin_pale", 0.0):   # retune R["spl"] (degrees): pre-bias of pale skin hue (clipped-channel pale skin is rendered redder by the table)
        h1 = h1 + np.radians(P["skin_pale"]) * np.exp(8.0 * (np.cos(h - np.radians(50.0)) - 1.0)) * smoothstep(0.80, 0.90, L) * smoothstep(0.01, 0.04, C)
    w_sk = np.exp(6.0 * (np.cos(h - np.radians(55.0)) - 1.0)) * smoothstep(0.0, P.get("skin_cmin", 0.05), C)
    Lc0, Cc0 = cusp(h); Lc1, Cc1 = cusp(h1)
    gam = np.clip(C / Cc0, 0, 1)
    C1 = C * hue_fn(h, S_e) * (1 + (Cc1 / Cc0 - 1) * gam * OPT["tri_c"])
    L1 = np.clip(L + hue_fn(h, dL_e) * C + gam * (Lc1 - Lc0) * OPT["tri_l"], 0, 1)
    # 3. global chroma
    C2 = softknee(C1 * P["sat"], P["cknee"], P["cmax"])
    # 4. tone
    T = tone_fn(P)(L1)
    L3 = L1 + np.minimum(T - L1, 0) + np.maximum(T - L1, 0) * (1 - OPT["lfade"] * edge)
    r = np.clip(L3 / np.maximum(L1, 1e-4), 0.4, 2.0)
    C3 = C2 * r ** P["kappa"]
    wg = smoothstep(0.55, 1.0, C3 / Cc1)
    L3 = L3 - np.maximum(0.0, L3 - np.maximum(L1, Lc1)) * wg * OPT["guard"]
    # 5. chroma vs lightness, skin_sat, headroom
    xs, ys = zip(*P["satL"])
    C3 = C3 * pchip(xs, ys)(L3)
    if P.get("skin_sat", 0.0):
        C3 = C3 * (1 + P["skin_sat"] * w_sk * (1 - smoothstep(0.35, 0.65, L3)))
    hq0, hq1 = P.get("headroom", (0.35, 1.0))
    g = C3 / np.maximum(C, 1e-9)
    g = np.where(g > 1, 1 + (g - 1) * (1 - smoothstep(hq0, hq1, q_rgb) * smoothstep(0.03, 0.08, C)), g)
    C3 = np.where(C > 1e-9, C * g, C3)
    # 6. split tone (unused here) + 6b. grey-axis tint with guards (as gen_tun800.py)
    s0, s1 = P["ts_range"]; t0, t1 = P["th_range"]
    tint_k = 1 - 0.8 * edge
    ws = (1 - smoothstep(s0, s1, L3)) * tint_k
    if P.get("skin_ts", 0.0):
        ws = ws * (1 - P["skin_ts"] * w_sk)
    wh = smoothstep(t0, t1, L3) * (1 - 0.6 * smoothstep(0.90, 1.0, L3)) * tint_k
    a3 = C3 * np.cos(h1) + P["ts"][0] * ws + P["th"][0] * wh
    b3 = C3 * np.sin(h1) + P["ts"][1] * ws + P["th"][1] * wh
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
        if P.get("tl_cool"):
            kc, kk = P["tl_cool"][:2]; hc = P["tl_cool"][2] if len(P["tl_cool"]) > 2 else 230.0
            w_cool = np.exp(kk * (np.cos(h1 - np.radians(hc)) - 1.0)) * smoothstep(0.01, 0.04, C3)
            wt = wt * (1 - kc * w_cool * smoothstep(0.50, 0.70, L3))
        a3 = a3 + pchip(kx, kta)(L3) * wt
        b3 = b3 + pchip(kx, ktb)(L3) * wt
    # 7. gamut
    C4 = np.hypot(a3, b3); h4 = np.arctan2(b3, a3)
    L5, C5 = gamut_soft(L3, C4, h4, P.get("knee", OPT["knee"]))
    lin = oklab_to_lin(np.stack([L5, C5 * np.cos(h4), C5 * np.sin(h4)], -1))
    lin = np.clip(lin, 0, 1)
    # 8. veil (the scan's black) + 8b. veil_keep_hue (as gen_tun800.py)
    v = P["veil"]
    if v > 0:
        vr = np.asarray(P["veil_rgb"], float); vr = vr / (0.2126 * vr[0] + 0.7152 * vr[1] + 0.0722 * vr[2])
        k = (P.get("skin_ts", 0.0) * w_sk)[..., None]
        lin = lin * (1 - v) + v * (vr * (1 - k) + k)
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
    return smooth_cube(apply_look(np.stack([R, G, B], -1), P), P)

def write_cube(path, P, out):
    n = out.shape[0]
    with open(path, "w", newline="\n") as f:
        f.write('TITLE "%s"\n' % P["title"])
        f.write("# original creative LUT, parametric OKLab model (gen_cin50d.py); input: Canon JPEG sRGB\n")
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
    viol = {ax: int((np.diff(Yo, axis=ax) < -1e-3).sum()) for ax in (0, 1, 2)}
    worst = min(float(np.diff(Yo, axis=ax).min()) for ax in (0, 1, 2)) * 255
    grey = out[np.arange(n), np.arange(n), np.arange(n)]
    gl = lin_to_oklab(srgb_to_lin(grey))
    d2 = max(float(np.abs(np.diff(out, 2, axis=ax)).max()) for ax in (0, 1, 2))
    d2p = max(float(np.percentile(np.abs(np.diff(out, 2, axis=ax)), 99.9)) for ax in (0, 1, 2))
    li = lin_to_oklab(srgb_to_lin(inp)); lo = lin_to_oklab(srgb_to_lin(out))
    Ci = np.hypot(li[..., 1], li[..., 2]); Co = np.hypot(lo[..., 1], lo[..., 2])
    m = (Ci > 0.05) & (Co > 0.03)
    dh = np.degrees(np.angle(np.exp(1j * (np.arctan2(lo[..., 2], lo[..., 1]) - np.arctan2(li[..., 2], li[..., 1])))))[m]
    skins = np.array([(255, 219, 190), (240, 190, 160), (214, 160, 125), (194, 150, 130), (180, 125, 90), (130, 85, 60), (115, 82, 68)]) / 255.0
    so = apply_look(skins, P)
    def lch(c):
        l = lin_to_oklab(srgb_to_lin(c)); return np.c_[l[:, 0], np.hypot(l[:, 1], l[:, 2]), np.degrees(np.arctan2(l[:, 2], l[:, 1]))]
    gv = np.array([0, 8, 16, 32, 64, 96, 118, 160, 200, 230, 250, 255]) / 255.0
    go = apply_look(np.stack([gv] * 3, -1), P)
    return dict(look=name, luma_violations_rgb_axes=viol, luma_worst_step_8bit=round(worst, 2),
                grey_monotonic=bool(np.all(np.diff(luma(grey)) > 0)), grey_max_tint_oklab=round(float(np.hypot(gl[:, 1], gl[:, 2]).max()), 4),
                grey_luma_0_8_16_32_64_96_118_160_200_230_250_255=[round(float(v) * 255, 1) for v in luma(go)],
                black_out=[round(float(v) * 255, 1) for v in out[0, 0, 0]], white_out=[round(float(v) * 255, 1) for v in out[-1, -1, -1]],
                max_abs_d2=round(d2, 4), p999_abs_d2=round(d2p, 4),
                hue_shift_deg_p1_p99_max=[round(float(np.percentile(dh, 1)), 1), round(float(np.percentile(dh, 99)), 1), round(float(np.abs(dh).max()), 1)],
                skin_in_LCh=np.round(lch(skins), 3).tolist(), skin_out_LCh=np.round(lch(so), 3).tolist())

def provenance(P):
    h, r, l, mean = spectral_hint()
    dH, S, dL = hue_params(P)
    st, D = neg_data()
    return dict(neg_slopes_D_per_stop={c: round(neg_slope(c), 4) for c in "RGB"},
                neg_Dmin={c: round(float(D[c][0]), 3) for c in "RGB"},
                neg_D_at_0={c: round(float(np.interp(0.0, st, D[c])), 3) for c in "RGB"},
                neg_scan=P["neg"], veil_lin=round(P["veil"], 6),
                measured_neutral_tint=[tuple(round(v, 5) for v in t) for t in neg_chan_tint(P["neg"])],
                spectral_matrix=np.round(spectral_film_matrix(), 4).tolist(), spectral_ring_mean_chroma=round(mean, 3),
                spectral_hint_at_anchors=dict(dH=np.round(h, 2).tolist(), S_rel=np.round(r, 3).tolist(), dL_per_C=np.round(l, 3).tolist()),
                effective_per_hue=dict(dH=np.round(dH, 2).tolist(), S=np.round(S, 3).tolist(), dL=np.round(dL, 3).tolist()))

# ------------------------------------------------------------------ curves diagram
def curves_png(path, sel=DEFAULT):
    """left: grey tone of every preset + identity; middle: the datasheet curves [K1b] p.4 (digitised) with the mid grey at
    camera ISO 100 and the JPEG's scene range; right: hue ring moves of sel"""
    from PIL import Image, ImageDraw
    cols = {"CIN50D": (40, 110, 190), "CIN50D_FLAT": (90, 90, 90)}
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
    ox2 = 800; s2w, s2h = 700, 700
    d.rectangle([ox2, oy - s2h, ox2 + s2w, oy], outline=(0, 0, 0))
    X = lambda st: ox2 + (st + 8) / 16 * s2w
    Y = lambda D: oy - D / 3.0 * s2h
    for st in range(-8, 9, 2): d.line([X(st), oy, X(st), oy - s2h], fill=(225, 225, 225))
    Q = LOOKS[sel]["neg"]
    lo, hi = jpeg_to_stops(np.array([1 / 255.0]), Q["top"])[0] + Q["dEI"], Q["top"] + Q["dEI"]
    d.rectangle([max(X(lo), ox2 + 2), oy - s2h + 2, min(X(hi), ox2 + s2w - 2), oy - 2], outline=(240, 200, 120))
    d.line([X(Q["dEI"]), oy, X(Q["dEI"]), oy - s2h], fill=(240, 140, 60), width=2)
    st, D = neg_data()
    for c, cc in zip("RGB", [(220, 60, 60), (40, 160, 60), (60, 90, 220)]):
        d.line([(X(st[i]), Y(D[c][i])) for i in range(len(st))], fill=cc, width=2)
    d.text((ox2, oy + 8), "[K1b] p.4 sensitometric curves, density vs camera stops (-8..+8); orange line = mid grey at camera ISO 100 (-1 stop),", fill=(0, 0, 0))
    d.text((ox2, oy + 22), "box = scene range of the JPEG (code 1 .. 255) on the film", fill=(0, 0, 0))
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
    ap = argparse.ArgumentParser(description="Cine 50D look: writes <outdir>/CIN50D.CUB from one preset")
    ap.add_argument("--preset", default=DEFAULT, choices=ORDER, help="look written to CIN50D.CUB (default %s)" % DEFAULT)
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rep = []
    for nm in [a.preset] + [k for k in ORDER if k != a.preset]:
        P = LOOKS[nm]
        out = build_cube(P)
        if nm == a.preset:
            write_cube(os.path.join(a.outdir, "CIN50D.CUB"), P, out)
        c = dict(checks(nm, P, out), cub=nm == a.preset, provenance=provenance(P),
                 tone_knots=[(round(x, 4), round(y, 4)) for x, y in P["tone"]])
        rep.append(c)
        print(json.dumps({k: v for k, v in c.items() if k not in ("tone_knots", "provenance", "skin_in_LCh")}))
    with open(os.path.join(a.outdir, "checks_cin50d.json"), "w") as f:
        json.dump(rep, f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_cin50d_curves.png"), a.preset)
