#!/usr/bin/env python3
"""Snapshot 200 (code SNP200, catalogue id 48, type neg, family "consumer neg", batch 4): WindyStrife's own look of a
budget ISO 200 consumer colour negative, as a soft minilab print scan.  Rosy-warm (red-leaning, not golden) greys,
strong reds and yellows, slightly muted greens and blues, soft contrast and a lifted, warm "faded print" black: the
nostalgic snapshot.  Grain ("Neg 200" x1.15) and halation (negative profile) are set by the module's effects, not by
this LUT.  Same OKLab colour model as gen_chungking.py / gen_port160.py and the consumer-neg family module `neg_scan`
of gen_sun200.py (batch 1); that code is COPIED verbatim from looks/SUN200/gen_sun200.py (family consistency), only
the provenance, the presets and the file names differ.  Two presets (PLAN 7.2, confidence L-M):
  SNP200   "rosy faded"  : red-leaning warm greys, faded warm black, softest contrast   (candidate A)
  SNP200B  "amber soft"  : amber-orange warmth, slightly lifted black, gentle contrast    (candidate B)
  The one farther from SUN200 (judge v2 mean dE) ships as SNP200; the other stays here as a preset (README).

PROVENANCE (MEASURED = read from a maker data sheet; DESCRIBED = published written accounts; INFERRED = reasoning
from a measured or described fact; GUESS = a look or model call made here)
  NO DATA SHEET.  The modelled stock has no published technical data (searched 2026-11-11: maker sites, 125px techdocs,
               web; only retail listings).  Everything specific to this film is DESCRIBED or GUESS.
  S1 MEASURED  Kodak Publication No. E-7022 "KODAK GOLD 200 Film", Revised 06-23 (June 2023), Kodak Alaris, p.4 fig.
  (CLASS PROXY) "Characteristic Curves" (E7022B), Log H Ref -1.14, Status M; digitised in batch 1 (data/
               E7022_characteristic_curves.csv, copied unchanged from looks/SUN200/data) -> E7022_LOGH / E7022_D below.
               G1 INFERRED: used as the tone-curve class proxy = the only published characteristic curve of the same
               maker's current ISO 200 consumer C-41 film; D5 reports grain identical and colour only slightly different
               side by side, D1 the same estimated PGI.  The softer, faded rendering is made by the scan parameters
               (GUESS), not by a different film curve.
  S2 RETAIL    data/SNP200_retail_facts.csv: ISO 200, C-41, latitude -2/+3 stops (glazerscamera.com,
               lookingglassphoto.com, pictureline.com, metergeist; wording matches S1 p.1, probably copied).  Only the
               latitude note in the README; not used in the LUT.
  S3 MEASURED  Kodak Publication No. E-4021 (October 2003) p.9, Supra Endura paper characteristic curves (data/
               E4021_paper_characteristic.csv, copied from looks/SUN200/data): display-medium Dmax for the grain row's
               rho_max cap only (README), not this LUT.
  D1 DESCRIBED analog.cafe, Dmitri, "Kodak ColorPlus 200 Film Review" (2023-06-13, upd. 2025-10-08): evident warmth
               from a stronger red response, reds emphasised, greens vivid, blues slightly subdued; fairly strong
               contrast in scans; muddy in grey / low-contrast light; fine grain, est. PGI ~44.
  D2 DESCRIBED lenslurker.com, "Kodak ColorPlus 200 - Cheap 35mm Film for Color Shooters" (upd. 2023-08-29): warm
               vintage / sun-kissed palette, warm but more balanced (cooler) than the golden ISO 200 sibling, moderate
               saturation (less than that sibling), moderate contrast (lower than that sibling), flat in cloudy light.
  D3 DESCRIBED bayphoto.com, TeamBay, "Film Comparison: Kodak Gold v.s. Kodak Color plus" (2023-11-14): reds cooler,
               more magenta than the golden sibling's orange-red; yellows / greens / blues almost identical.
  D4 DESCRIBED dehancer.com, "Films at different exposures: Kodak Color Plus 200 vs Fujicolor C200" (undated): tends
               toward red at normal exposure, skin included; fine to -2 EV, shadows lost at -3..-4 EV; highlights hold.
  D5 DESCRIBED analog.cafe, Dmitri, "Kodak Gold vs. ColorPlus: Side-by-Side Comparison" (2023-11-23, upd. 2025-07-15):
               only slight colour differences, identical grain; maker marketing: a "more vintage feel".
  D6 DESCRIBED uniquephoto.com, "Kodak ColorPlus 200 Profile" (2026-09-26): warm and vintage, inviting skin.
  D7 DESCRIBED filmsupply.club, "Kodak Gold vs Kodak ColorPlus" (2026-05-03): cooler and more neutral than the golden
               sibling; coarser grain.
  D8 DESCRIBED 35mmc.com, Tony Warren, "Kodak Colorplus 200 ... Mini-Review" (2023-07-26): nicely but not overly
               saturated; moderate grain.
  (D1-D8 are paraphrased in data/SNP200_descriptions.csv.)
  CATALOGUE LOOK CALL (PLAN 5.1 / batch-4 task row): "warm, nostalgic, saturated reds / yellows, slightly muted greens,
               softer".  The muted greens are a look call: D1 says greens are vivid and D3 that they match the sibling,
               so the muting is kept slight (GUESS) and noted as unsupported by the sources.
  Scanner rendering named: a soft consumer minilab print scan (Frontier / Noritsu-type auto scan: per-record base
               removal, per-record contrast normalisation, grey balance near mid grey, auto black point), printed
               a little soft with a lifted ("faded print") warm black: GUESS (D2 "vintage", D5 maker "vintage feel").
How each look element is sourced:
  tone          neg_scan from S1 (class proxy, G1) + GUESS scan parameters: ns_contrast < 0 makes the print SOFTER
                than the camera JPEG (mid slope below 1; D2 "contrast lower than the golden sibling"); the faded
                black is the veil (GUESS).  D1's "fairly strong contrast" is the scan of a well-lit scene; the
                catalogue call is "softer", so D2 wins (noted).
  warmth        DESCRIBED D1 (warmth from red response), D4 (tends toward red, skin too), D3 (reds more magenta), D2 / D7
                (warm, but less golden than the sibling): declared grey-axis tint tintL_lab leaning RED (OKLab hue
                ~45-55 deg in the mids for SNP200, ~62 deg for SNP200B) and weaker than SUN200's golden ~72 deg tint;
                warm (reddish-brown) veil in the black; neutral paper white.  Amounts GUESS.
  saturation    DESCRIBED D2 / D8 (moderate overall), the look call (reds / yellows saturated): global sat ~1.0,
                reds and yellows up, greens and blues down; all gains GUESS.
  hue edits     reds turn slightly toward crimson (D3), skin a few degrees toward red (D4), greens slightly muted
                (look call), blues subdued (D1); amounts GUESS.
  skin          skin guards as SUN200 (tint kept mostly off skin, skin_sat gives back shadow chroma); skin stays in
                OKLab hue 40..70 (judge bar, default window: no skin push is declared).
  sky           sky guard sky_tl (from gen_sun200.py): the warm tint is mostly kept off pale blue / cyan.
Claims (README, judge J7), all against the sibling SUN200 (looks/SUN200, judged PASS 11-10):
  C1 softer: mid-grey log-log slope (grey 118 +-0.5 EV) lower than SUN200's 1.12            (D2; look call)
  C2 less golden but warm: 0 < OKLab b(grey 118) < SUN200's +0.0245                         (D2, D7; D1, D6)
  C3 red-leaning warmth: grey-118 tint hue (atan2(b, a)) at least 10 deg redder (lower) than SUN200's   (D4, D3, D1)
  C4 less saturated overall than SUN200: synthetic chroma factor (card, chart, scene) below SUN200's    (D2, D8)

Model (input = display-referred sRGB/Rec.709 JPEG, Picture Style Neutral, WB 5200 K):
  neg_scan (family module from gen_sun200.py, grey axis only; see that file for the derivation):
    1. code v -> relative scene exposure E = lin(v) + f0 (1 - lin(v)), f0 = 0.18 * 2^ns_floor  (GUESS ns_floor)
    2. log H = -1.14 (S1 Log H Ref) + ns_ev * log10(2) + log10(E / 0.18)
    3. per record c: net density n_c = D_c(log H) - D_c,min (S1 class proxy, monotone fit)
    4. scanner: L_c = 10^(n_c/g_c) - 1; per-record auto levels (black at the JPEG black, white at the JPEG white);
       per-record grey balance (record mid -> ns_mid); paper white ns_white
    5. print contrast ns_contrast (here negative = softer), faded out over display ns_ctop
    6. tone knots = display luma; grey-axis tint = ns_tint x neutral-output OKLab (a, b) + declared tintL_lab, faded
       out over output L tint_top so paper white stays neutral
  then the OKLab look of gen_chungking.py (steps 1-8), unchanged code (sky guard 6b and gamut knee 0.84 as SUN200).
  8. Veil (lifted, warm black, GUESS): lin = lin (1 - v) + v * veil_rgb (luminance-normalised).
Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1.  films/SNP200.T17 = SNP200.CUB through the module's
own loader (/root/ml/yuvre_work/harness "lut SNP200.CUB SNP200.T17").

usage: python3 gen_snp200.py [--preset SNP200|SNP200B] <outdir>
       writes <PRESET>.CUB, checks_<preset>.json and preview_<preset>_curves.png
"""
import sys, os, json, argparse
import numpy as np

N = 33
OPT = dict(tri_c=1.0, tri_l=0.0, guard=0.0, knee=0.84, hfade=0.75, lfade=0.0)   # as gen_chungking.py / gen_port160.py,
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


# ------------------------------------------------------------------ S1 data: E-7022 rev. 06-23, p.4, characteristic curves
# (= data/E7022_characteristic_curves.csv; log H in log lux-s, Status M densities R, G, B; -1.2..-1.0 linear between
# -1.3 and -0.9 where the curve letters hide the lines)
LOGH_REF = -1.14                                  # S1 p.4 "Log H Ref: -1.14" (normal exposure of a mid grey)
E7022_LOGH = np.round(np.arange(-2.9, 0.85, 0.1), 2)
E7022_D = np.array([
    [0.253, 0.659, 0.986], [0.261, 0.667, 0.974], [0.267, 0.674, 0.963], [0.277, 0.687, 0.966], [0.292, 0.708, 0.985],
    [0.311, 0.734, 1.007], [0.339, 0.768, 1.041], [0.374, 0.809, 1.082], [0.416, 0.854, 1.131], [0.465, 0.903, 1.189],
    [0.511, 0.951, 1.245], [0.565, 1.007, 1.306], [0.620, 1.064, 1.367], [0.674, 1.123, 1.429], [0.729, 1.181, 1.492],
    [0.780, 1.237, 1.557], [0.832, 1.292, 1.626], [0.887, 1.349, 1.686], [0.941, 1.407, 1.746], [0.996, 1.464, 1.806],
    [1.051, 1.521, 1.865], [1.104, 1.577, 1.922], [1.160, 1.634, 1.983], [1.216, 1.691, 2.048], [1.273, 1.748, 2.112],
    [1.329, 1.805, 2.176], [1.382, 1.857, 2.235], [1.439, 1.914, 2.298], [1.496, 1.969, 2.360], [1.546, 2.020, 2.418],
    [1.591, 2.066, 2.469], [1.627, 2.103, 2.513], [1.661, 2.136, 2.550], [1.693, 2.167, 2.585], [1.722, 2.195, 2.619],
    [1.753, 2.224, 2.652], [1.781, 2.253, 2.686], [1.811, 2.284, 2.720]])

def _check_csv():
    """the embedded table must equal data/E7022_characteristic_curves.csv when that file is next to the generator"""
    fn = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "E7022_characteristic_curves.csv")
    if not os.path.exists(fn): return None
    rows = [l.split(",") for l in open(fn) if l.strip() and not l.startswith("#")][1:]
    t = np.array([[float(x) for x in r[:4]] for r in rows])
    assert np.allclose(t[:, 0], E7022_LOGH) and np.allclose(t[:, 1:], E7022_D, atol=5e-4), "embedded E-7022 table != csv"
    return fn

def _mono(col):
    """monotone fit of a drawn record: flat at its minimum below the argmin (the blue toe's 0.02 dip at log H -2.7 as
    drawn is flattened), running maximum above"""
    c = col.copy(); i = int(np.argmin(c)); c[:i] = c[i]
    return np.maximum.accumulate(c)

E7022_DM = np.stack([_mono(E7022_D[:, k]) for k in range(3)], -1)
E7022_DMIN = E7022_DM[0]
_sl = (E7022_LOGH >= -2.0 - 1e-9) & (E7022_LOGH <= 1e-9)
E7022_GAMMA = np.array([np.polyfit(E7022_LOGH[_sl], E7022_D[_sl, k], 1)[0] for k in range(3)])   # ~0.54 / 0.56 / 0.61

# ------------------------------------------------------------------ neg_scan: the consumer-neg family module
_DF = None
def _dfun():
    global _DF
    if _DF is None: _DF = [pchip(E7022_LOGH, E7022_DM[:, k]) for k in range(3)]
    return _DF

def neg_scan_grey(v, P):
    """grey input codes v (0..1) -> (..., 3) display R, G, B of the scanned negative (before the OKLab look)"""
    v = np.asarray(v, float)
    f0 = 0.18 * 2.0 ** P["ns_floor"]
    Df = _dfun()
    def rec(vv):
        l = srgb_to_lin(vv); E = l + f0 * (1 - l)
        lh = LOGH_REF + P["ns_ev"] * np.log10(2.0) + np.log10(E / 0.18)
        n = np.stack([Df[k](lh) - E7022_DMIN[k] for k in range(3)], -1)
        return 10.0 ** (n / E7022_GAMMA) - 1.0            # exposure above the record's toe, record contrast normalised
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
    """fills P["tone"] (display luma knots) and P["tintL"] (OKLab a, b vs output L) from neg_scan + tintL_lab"""
    P = dict(P)
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

# ------------------------------------------------------------------ the look
# ns_* = neg_scan parameters (GUESS, see the docstring); every number in LOOKS is a GUESS unless its comment cites S1/D*.
# The SUN200 values (looks/SUN200/gen_sun200.py) are quoted in the comments only to show where each preset departs.
_LOOKS_SRC = {
 # candidate A "rosy faded"
 "SNP200": dict(
    title="SNP200 - Snapshot 200 (WindyStrife original LUT)",
    note="Budget consumer negative 200, soft minilab print: rosy-warm greys, strong reds and yellows, muted greens, soft contrast, faded warm black.",
    # softer print than the JPEG (D2: contrast lower than the golden sibling; look call "softer"): ns_contrast < 0
    # (SUN200 +0.12); mid a hair lighter than SUN200 (0.176 vs 0.172); ns_tint 0.35: this lab removes more of the toe tint
    ns_floor=-4.6, ns_ev=0.0, ns_mid=0.1875, ns_white=0.970, ns_contrast=-0.04, ns_ctop=(0.60, 0.95), ns_tint=0.35, tint_top=(0.84, 0.95),
    # declared lab tint (D1 red warmth, D4 tends red, D2/D7 less golden): rosy-peach, OKLab hue ~47 deg in the mids,
    # chroma about 3/4 of SUN200's golden ~0.026 (GUESS amounts)
    tintL_lab=[(0.0, 0.0, 0.0), (0.15, 0.0110, 0.0090), (0.30, 0.0170, 0.0145), (0.55, 0.0200, 0.0180), (0.75, 0.0200, 0.0190), (0.90, 0.0120, 0.0120), (1.0, 0.0, 0.0)],
    kappa=0.8,
    #       red   skin  yel   grn   cyan  blue  purp  mag
    dH  =  [-3,   4,    -2,    0,    0,    0,    0,    0],     # reds toward crimson (D3); skin +3 offsets the red turn's spill onto skin
    S   =  [1.26, 1.04, 1.20, 0.80, 0.82, 0.84, 0.96, 1.04],
    dL  =  [-0.01, 0.0, 0.0,  0.0,  0.0,  0.02, 0.0,  0.0],
    sat=1.07, cknee=0.14, cmax=0.25,                         # moderate overall saturation (D2, D8); SUN200 1.10
    satL=[(0.0, 0.72), (0.30, 0.88), (0.60, 1.0), (0.85, 0.96), (1.0, 0.85)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=0.0045, veil_rgb=(1.08, 1.0, 0.84),                  # faded print: lifted warm-brown black
    skin_ts=0.0, skin_sat=0.12, skin_cmin=0.02,
    tl_cfade=(0.03, 0.12, 0.5), skin_tl=1.0, skin_tl_kappa=2.0, tl_toe=0.1, sky_tl=(0.85, 220.0, 1.2, 0.05)),
 # candidate B "amber soft"
 "SNP200B": dict(
    title="SNP200B - Snapshot 200 amber (WindyStrife original LUT)",
    note="Budget consumer negative 200, gentle minilab print: amber-orange warmth, strong reds and yellows, olive-muted greens, slightly lifted black.",
    ns_floor=-5.2, ns_ev=0.0, ns_mid=0.175, ns_white=0.972, ns_contrast=0.0, ns_ctop=(0.60, 0.95), ns_tint=0.5, tint_top=(0.88, 0.97),
    # amber-orange tint, OKLab hue ~63 deg in the mids, chroma ~0.6 of SUN200's (GUESS amounts)
    tintL_lab=[(0.0, 0.0, 0.0), (0.15, 0.0030, 0.0055), (0.30, 0.0050, 0.0090), (0.55, 0.0065, 0.0130), (0.75, 0.0060, 0.0125), (0.90, 0.0035, 0.0070), (1.0, 0.0, 0.0)],
    kappa=0.8,
    #       red   skin  yel   grn   cyan  blue  purp  mag
    dH  =  [4,    -1,   -3,   -5,    0,   -2,    0,    0],     # orange-red, olive greens (GUESS)
    S   =  [1.12, 1.03, 1.12, 0.90, 0.92, 0.92, 1.0,  1.0],
    dL  =  [-0.01, 0.0, 0.0,  0.0,  0.0,  0.02, 0.0,  0.0],
    sat=1.02, cknee=0.14, cmax=0.25,
    satL=[(0.0, 0.82), (0.30, 0.94), (0.60, 1.0), (0.85, 0.97), (1.0, 0.85)],
    ts=(0.0, 0.0), ts_range=(0.12, 0.55), th=(0.0, 0.0), th_range=(0.60, 0.95),
    veil=0.0020, veil_rgb=(1.06, 1.0, 0.90),
    skin_ts=0.0, skin_sat=0.12, skin_cmin=0.04,
    tl_cfade=(0.03, 0.12, 0.5), skin_tl=1.0, skin_tl_kappa=2.0, tl_toe=0.1, sky_tl=(0.85, 220.0, 1.2, 0.05)),
}
ORDER = ["SNP200", "SNP200B"]
DEFAULT = "SNP200"
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

RETUNE = {'SNP200': {'smooth': (0.03, 0.06, 12), 'mono': 1.0, 'gk': 4.0, 'gc': 0.09, 'gather': 1.1, 'skl_lo': 0.4, 'ds': (1.15, 1.0, 1.15, 0.8, 1, 1, 1, 1), 'sat': 0.9342, 'k': 0.75708, 'black': -0.09208, 'shoulder': 1.49377, 'wa': 0.00658, 'wb': 0.00323, 'sa': -0.00126, 'sb': 0.00402}}  # RETUNE-LINE
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
    #       sky_tl = (k, hue_deg, kappa, C1)    (gen_sun200.py only) fade on sky blues: a warm grey-axis tint added
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
        f.write("# original creative LUT, parametric OKLab model (gen_snp200.py); input: Canon JPEG sRGB\n")
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
    """left: neg_scan grey response per record (thin) and the look's grey luma (bold); right: hue ring moves"""
    from PIL import Image, ImageDraw
    P = LOOKS[sel]
    Wd, Hd, pad = 1600, 820, 50
    im = Image.new("RGB", (Wd, Hd), (250, 250, 248)); d = ImageDraw.Draw(im)
    s = 700; ox, oy = pad, pad + s
    d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0))
    for k in range(1, 4):
        d.line([ox + k * s / 4, oy, ox + k * s / 4, oy - s], fill=(220, 220, 220))
        d.line([ox, oy - k * s / 4, ox + s, oy - k * s / 4], fill=(220, 220, 220))
    d.line([ox, oy, ox + s, oy - s], fill=(170, 170, 170))
    g = np.linspace(0, 1, 256)
    ns = neg_scan_grey(g, P)
    for c, col in enumerate([(210, 50, 50), (50, 150, 50), (50, 80, 210)]):
        d.line([(ox + g[i] * s, oy - ns[i, c] * s) for i in range(256)], fill=col, width=1)
    y = luma(apply_look(np.stack([g, g, g], -1), P))
    d.line([(ox + g[i] * s, oy - y[i] * s) for i in range(256)], fill=(200, 120, 0), width=4)
    d.text((ox, oy + 8), "grey in -> out: neg_scan R/G/B (thin, from E-7022 p.4 curves, class proxy), %s luma (bold)" % sel, fill=(0, 0, 0))
    cx, cy, sc = 800 + 380, pad + 350, 2200
    d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(0, 0, 0))
    hs = np.radians(np.arange(0, 360, 15))
    ring = np.stack([np.full_like(hs, 0.68), 0.11 * np.cos(hs), 0.11 * np.sin(hs)], -1)
    rgb_in = np.clip(lin_to_srgb(oklab_to_lin(ring)), 0, 1)
    li = lin_to_oklab(srgb_to_lin(rgb_in))
    lo = lin_to_oklab(srgb_to_lin(apply_look(rgb_in, P)))
    for i in range(len(hs)):
        x0, y0 = cx + li[i, 1] * sc, cy - li[i, 2] * sc
        d.ellipse([x0 - 7, y0 - 7, x0 + 7, y0 + 7], fill=tuple((rgb_in[i] * 255).astype(int)), outline=(0, 0, 0))
        d.line([x0, y0, cx + lo[i, 1] * sc, cy - lo[i, 2] * sc], fill=(200, 120, 0), width=2)
        d.ellipse([cx + lo[i, 1] * sc - 3, cy - lo[i, 2] * sc - 3, cx + lo[i, 1] * sc + 3, cy - lo[i, 2] * sc + 3], fill=(200, 120, 0))
    d.text((800, pad + 720), "OKLab a/b: ring of colours (L 0.68, C 0.11) -> where %s moves them" % sel, fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Snapshot 200 look: writes <outdir>/<PRESET>.CUB")
    ap.add_argument("--preset", default=DEFAULT, choices=ORDER)
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    csv = _check_csv()
    P = LOOKS[a.preset]
    out = build_cube(P)
    write_cube(os.path.join(a.outdir, a.preset + ".CUB"), P, out)
    c = dict(checks(a.preset, P, out), cub=True, csv_checked=bool(csv), gamma_RGB=np.round(E7022_GAMMA, 3).tolist(),
             tone_knots=[[round(x, 4), round(y, 4)] for x, y in P["tone"]], tintL=[[round(v, 4) for v in t] for t in P["tintL"]])
    print(json.dumps(c))
    with open(os.path.join(a.outdir, "checks_%s.json" % a.preset.lower()), "w") as f:
        json.dump([c], f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_%s_curves.png" % a.preset.lower()), a.preset)
