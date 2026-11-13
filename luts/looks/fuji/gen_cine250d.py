#!/usr/bin/env python3
"""Cine 250D (file code CINE250D), WindyStrife's 6th own film: a modern daylight-balanced (5500 K)
ISO 250 motion-picture colour negative, developed ECN-2 and lab-scanned the way stills
photographers use it.  Shelf id 31, output mode 12 (the old port).
Copy of gen_chungking.py (same OKLab model and knobs: ts_cfade, ts_toe, skin_cmin, tintL with
tl_cfade / skin_tl / skin_tl_kappa / tl_toe), plus three new knobs: veil_skin_rgb, skin_sq and
skin_dhL (see apply_look).  Every number is a hand-set parameter; no film scans, frames or
third-party LUTs are in this file.

Presets (--preset, default CINE250D; the output is always CINE250D.CUB, so the module build takes
whichever preset was written last):
  CINE250D        shipped default: the lab-scan look of a low-gamma negative.
                    - tone: flat low mids and mids (slope ~0.73 of the Canon JPEG over codes 64..160);
                      open low mids over a clean black (~5/255); a soft highlight shoulder: the slope
                      peaks at ~1.1 in the upper mids and falls below 1 from ~232 up (0.90 / 0.86 per
                      code over 240..255); white ~251
                    - colour: natural (~0.91x Canon chroma); neutral mid grey; golden upper mids and
                      highlights; a hair cool in the deep shadows
                    - hues: skin kept warm and even (squeezed toward 55 deg); olive greens; cyan skies;
                      reds a touch warm
  CINE250D_PRINT  alternate: the same negative printed to a cine print film.
                    - tone: higher contrast (mid slope ~1.10); dense toe (black ~4/255); no shoulder
                      (camera grey slope 1.0-1.2 per code over 224..255)
                    - colour: lightly teal shadows; neutral highlights; fuller mid chroma
                    - hues: deeper reds
                    - NOT a passing look: judge_x.py fails it on the exposure ramps (a lightness
                      reversal of 4 on the denim ramp, bar 3), the fixer's top-band bar too (no
                      shoulder); unjudged for shipping, an experiment only
Where the numbers come from:
  RECALLED (public datasheet facts as remembered; the datasheet PDFs were NOT downloaded or re-read
  from their plots for this file, so every number here is approximate and unverified):
    - 250D-class camera negative (Kodak H-1-5207, VISION3 250D 5207/7207 technical data): EI 250
      at 5500 K and ECN-2 (well known); a long straight-line characteristic curve with the toe a
      few stops under normal exposure and no shoulder over the plotted range (the stock is sold
      on extra highlight latitude); a low straight-line gamma, ~0.6.  Not used here: granularity,
      spectral sensitivity and MTF (the granularity chart was not read, see fr_fx.c: the Neg 250
      grain strength is a box-speed GUESS).
    - cine print film (Kodak H-1-2383, VISION color print 2383/3383): a steep S-curve, D-max ~4
      (status A), sold on rich blacks and neutral highlights; print gamma is commonly quoted at
      ~2.6-3, i.e. a neg x print system gamma of ~1.5-1.7 for dark-surround projection.
  GUESSES (taste, from how stills photographers describe ECN-2 lab scans of the stock; reports
    disagree and every lab scans differently):
    - what is reported: flatter and more muted than consumer C-41 stock, tolerant of
      overexposure, grainy when underexposed, a mild red or green cast that labs or users
      neutralise
    - what we take: the neutral end (no cast); flat mids and a soft highlight shoulder (a LUT on
      an 8-bit JPEG cannot add latitude or bring back clipped highlights: the shoulder only
      compresses the top of the JPEG a little instead of stretching it); golden highlights;
      clean shadows
    - the print variant's lightly teal shadows follow common print-emulation practice, not a
      measurement
    How a gamma-0.6 negative plus a scanner's auto levels maps to a Canon JPEG (already
    tone-mapped by its picture style) is a look call: the curves below are designed, not derived.
  CHECKED on the camera's own-film pass (fr_film.c fr_film_build + fr_film_words on 4:2:2 YUV) with
    the judge's script judge_x.py, run unmodified (ALL true), and with the fixer's copy of it that adds
    the top-band bar below (ALL true).  Independent judge v2 (11-11; it measures through the same kernel
    and gates the kernel's error against the exact 17^3 lookup, J8 p99.9 <= 2 per image): the shoulder
    table of the 11-10 review fix FAILED it on J8 (lake frame p99.9 3), the straight toe below (32 knot
    0.156 -> 0.146) PASSES every check, with the J5 note: 2.46 dE x100 from its nearest film, Portrait 160
    (a catalogue candidate, not shipped), inside the 2.0..3.0 band the owner is told about.  judge_x.py
    and the top-band copy were rerun on this table by the judge v2 step.  The card-LUT pass
    (fr_lut_words) is only a cross-check.  The bar:
    - skin hue stays in 40..70 deg and keeps chroma, on the 340-sample skin grid and the probes
    - white >= 248; blacks not plugged; 0 grey reversals; cube luma monotonic; d2 <= 0.0588
    - top band (review fix 11-10): camera grey luma slope <= 1 per code over 232..255 and no 8-bit
      channel step over 3 in 224..255 (a shoulder, not a run to white)
    - at least ~2.5 OKLab dE x100 from every shipped film and from the plain JPEG (now PRONEG 2.64,
      plain JPEG 3.00)

films/CINE250D.T17 = CINE250D.CUB through the module's own loader (/root/ml/yuvre_work/harness
"lut CINE250D.CUB CINE250D.T17"), then mk_filmtab.py (build.sh) -> fr_filmtab.h.  To ship
CINE250D_PRINT instead (it fails the bar, see above): python3 gen_cine250d.py --preset
CINE250D_PRINT <luts_fuji dir>, rerun the harness line, copy the .T17 to the old port/films/,
rebuild (build.sh).  luts_fuji/run.sh always writes the default preset: after it, redo these steps.

Model (input = display-referred sRGB/Rec.709 JPEG, Picture Style Standard/Neutral): see
gen_chungking.py; steps 1-8 and the skin guards are unchanged (same code).
Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1.

usage: python3 gen_cine250d.py [--preset CINE250D|CINE250D_PRINT] <outdir>
       writes CINE250D.CUB (the chosen preset), checks_cine250d.json and
       preview_cine250d_curves.png (both presets, the chosen one bold); the files of the other
       looks in the same dir are not touched
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
# Knot inputs: codes 0, 8, 16, 32, 64, 118, 160, 200, 230, 245, 255.
LOOKS = {
 # ---- shipped default: ECN-2 negative, lab scan (stills use)
 "CINE250D": dict(
    title="CINE250D - Cine 250D (WindyStrife original LUT)",
    note="Lab scan: flat mid contrast, open low mids over a clean black, soft highlight shoulder, golden upper mids, natural colour, olive greens, cyan skies.",
    # gamma-0.6 negative + scanner levels, checked on the camera's own-film pass (fr_film.c; camera grey luma below):
    #   - flat low mids and mids, slope ~0.73 of the Canon JPEG (64 -> ~82, 118 -> ~120, 160 -> ~152): the low-gamma
    #     negative.  The first version (slope 0.96) kept the JPEG's own contrast and landed 1.3 dE x100 from Neg Std
    #     (PRONEG).
    #   - clean black ~5/255 under an open toe (16 -> ~19, 32 -> ~39, 64 -> ~82): the scan sets its black point at
    #     base + fog, the toe starts ~4 stops under.  PRONEG lifts black to 13 instead.  The toe is one straight run
    #     from code 16 to 64 (display slope ~1.35 over 16..32 and ~1.37 over 32..64).  Judge v2 fix 11-11: the old
    #     32 knot (0.156, camera 32 -> ~41) bulged the toe to a slope of 1.51 over 16..32.  The steep toe, then the
    #     drop to ~0.7 in the mids, made the table's luma offset of dark saturated colours (their 17^3 nodes get
    #     clipped to sRGB by the loader) fall fast with lightness.  fr_film.c applies that offset at the pixel
    #     pair's mean luma, so dark pixels of high-contrast pairs (dark foliage against sky) strayed 3 codes from
    #     the exact lookup.  Lake frame: 0.137 % of pixels, so p99.9 3, against judge v2's J8 bar of 2.  With the
    #     straight toe it is 0.013 % (p99.9 2).  The distances to the other films and to the plain JPEG move by
    #     <= 0.05 dE x100 (PRONEG 2.67 -> 2.64, plain JPEG 3.05 -> 3.00, nearest PORT160 2.51 -> 2.46).
    #   - a soft highlight shoulder to white ~251 (200 -> ~194, 230 -> ~227, 245 -> ~242): the slope rises to ~1.1
    #     over 160..224, then falls, 1.10 / 1.03 / 0.98 / 0.90 / 0.86 per code over 208-224 / 224-232 / 232-240 /
    #     240-248 / 248-255, so the top of the Canon JPEG is compressed a little instead of stretched.  That is all
    #     a LUT can show of the stock's highlight latitude: clipped JPEG highlights stay clipped.
    #   Review fix 11-10: the first shipped curve (64 -> 0.298, 200 -> 0.722, 230 -> 0.838, 245 -> 0.918) held the
    #   upper mids down and then ran up to white at up to 1.7 per code, steeper than any other film and the opposite
    #   of a shoulder.  With white >= 248 over flat mids the slope has to pass 1 somewhere above mid grey; it now
    #   peaks in the upper mids instead of at white.  Brighter upper mids alone moved the look to 2.2 dE x100 from
    #   PRONEG; the flatter low mids (64: 0.298 -> 0.318) bring it back to 2.67 (plain JPEG 3.05, ETERNA 3.74).
    #   An old end of 245 -> 0.94 / 255 -> 0.988 made a 5-code grey step at 247 on the camera; now <= 3 codes.
    tone=[(0.0, 0.0), (0.03137, 0.024), (0.06275, 0.061), (0.12549, 0.146), (0.25098, 0.318), (0.46275, 0.468),
          (0.62745, 0.596), (0.78431, 0.764), (0.90196, 0.895), (0.96078, 0.953), (1.0, 0.985)],
    # kappa 0.6 (was 0.8): the lifted low mids gain less chroma.  At 0.8 with sat 1.05, a low-chroma tan skin
    # (101,83,66) went to hue 69.7 on the camera, against the 70 limit (the identity LUT already gives 68.3 there).
    kappa=0.6,
    #       red  skin  yel   grn   cyan  blue  purp  mag
    # reds a touch warmer, greens toward olive, skies toward cyan, skin hue kept.  The camera skin window is
    # sensitive to red: at +5 tan skin reached hue 69.9; at 0, or with magenta -3, deep skin fell to 40.0.
    # Yellow is kept at 0: yellow -3 with green -5 turned sunlit conifers orange-brown on the lake image.
    dH  =  [ 2,   0,    0,   -5,   -4,   -7,   -3,    0],
    # restrained yellows and greens (0.90 / 0.82): they also pull low-chroma skin toward yellow through the
    # camera's 16-code UV cells
    S   =  [0.92, 0.98, 0.90, 0.82, 0.90, 0.92, 0.88, 0.92],
    dL  =  [-0.02, 0.0, 0.0, -0.03, -0.02, -0.03, -0.02, -0.02],
    # sat 1.05: natural colour, ~0.91x Canon chroma on the camera; the review fix was 1.02 on the old curve
    sat=1.05, cknee=0.12, cmax=0.24,
    # less chroma in the deep shadows, the lifted low mids and the top
    satL=[(0.0, 0.85), (0.25, 0.90), (0.45, 0.95), (0.62, 1.0), (0.80, 0.97), (1.0, 0.84)],
    ts=(0.0, 0.0), ts_range=(0.18, 0.70), th=(0.0, 0.0), th_range=(0.70, 0.97),
    # black ~5/255, a hair cool (scanner black point on the orange-mask-removed negative: GUESS).  On skin hues the
    # lift is faintly skin-coloured (veil_skin_rgb), so deep skin stays brown through the camera's 17^3 YUV pass.
    veil=0.0016, veil_rgb=(0.98, 1.0, 1.04), veil_skin_rgb=(1.2, 0.94, 0.84),
    skin_ts=0.9, skin_sat=0.12, skin_cmin=0.04, ts_cfade=(0.045, 0.13, 0.9), ts_toe=0.16,
    # skin hue squeeze toward 55 deg (k 0.25 at all L) plus an offset: +2 deg for deep skin, -4 deg at the bright
    # end.  This pre-compensates the camera's skin drift (see apply_look).  Without it, pale yellowish skin reached
    # hue 70.2 and deep pinkish skin 40.0.  At -2.5 on the bright end, light skin (255,219,190) sat at 69.1.
    skin_sq=[(0.0, 0.25), (0.5, 0.25), (1.0, 0.25)],
    skin_dhL=[(0.0, 2.0), (0.30, 2.0), (0.45, 0.5), (0.60, 0.0), (0.70, 0.0), (0.85, -2.5), (1.0, -4.0)],
    # neutral axis per output L (OKLab a, b):
    #   - slightly cool shadows (h ~250, C ~.003).  Twice that turned deep skin pink through the camera's UV cells,
    #     whose non-skin neighbours take the full tint.
    #   - neutral mid grey (118 -> 119,119,119)
    #   - golden upper mids and highlights (h ~66, C ~.012 over L .78-.86), fading to a near-neutral white
    #   The tint starts above mid grey (L .58): a warm grey node at L ~.5 pushed low-chroma tan skin toward yellow.
    tintL=[(0.0, -0.0006, -0.002), (0.15, -0.001, -0.0028), (0.30, -0.0007, -0.0014), (0.45, 0.0, 0.0), (0.58, 0.0006, 0.0008),
           (0.68, 0.0030, 0.0062), (0.78, 0.0048, 0.0106), (0.86, 0.0050, 0.0115), (0.93, 0.0030, 0.0068), (1.0, 0.0004, 0.0008)],
    # tint off on skin (95 % at all L: at 85 % in the top a pale low-chroma skin went h 64 -> 70), wide hue kernel
    # (kappa 3, as CHUNGK: the camera's 16-code UV cells blend skin with its neighbours)
    tl_cfade=(0.03, 0.10, 0.6), skin_tl=(0.95, 0.95), skin_tl_kappa=3.0, tl_toe=0.06),
 # ---- alternate: the negative printed to a cine print film
 "CINE250D_PRINT": dict(
    title="CINE250D - Cine 250D, print-film variant (WindyStrife original LUT)",
    note="Print: higher contrast, dense toe, no shoulder, lightly teal shadows, neutral highlights, fuller mids, deeper reds (fails the judge bar).",
    # neg x print system gamma ~1.6 shown as a mid slope ~1.10 of the Canon JPEG (64..160).  The print curve has a
    # denser toe (review fix P25: 32 -> 0.082, 64 -> 0.184, 200 -> 0.755, 245 -> 0.934); its top is not a shoulder
    # (camera grey slope 1.0-1.2 per code over 224..255).  judge_x.py fails this preset: exposure-ramp lightness
    # reversal 4 on denim (bar 3), and the top-band bar.  Kept as an unjudged experiment, not a shipping option.
    # Mid grey prints a little darker (118 -> ~110), so the curve stays under the diagonal above mid grey: lifting
    # bright saturated oranges over their cusp kinked gamut_soft at the B = 0 cube face (d2 0.087 with 0.627 -> 0.648).
    tone=[(0.0, 0.0), (0.03137, 0.014), (0.06275, 0.034), (0.12549, 0.082), (0.25098, 0.184), (0.46275, 0.428),
          (0.62745, 0.604), (0.78431, 0.755), (0.90196, 0.872), (0.96078, 0.934), (1.0, 0.982)],
    kappa=0.85,
    #       red  skin  yel   grn   cyan  blue  purp  mag
    # review fix P25.  Reds +2 toward orange: a deep rust skin sample dropped to hue 38.5 on the camera at -1.
    # Green 0 (was +5) and cyan/blue -2/-4 (were -3/-6) bring the look back from the generic teal print look.
    dH  =  [ 2,   2,   -4,    0,   -2,   -4,   -4,   -1],
    S   =  [1.04, 1.00, 0.98, 0.86, 0.96, 0.92, 0.88, 0.94],
    dL  =  [-0.05, 0.0, 0.0, -0.06, -0.04, -0.06, -0.04, -0.03],
    sat=1.0, cknee=0.13, cmax=0.27,
    satL=[(0.0, 0.75), (0.25, 0.88), (0.50, 1.04), (0.75, 1.0), (1.0, 0.78)],
    ts=(0.0, 0.0), ts_range=(0.18, 0.70), th=(0.0, 0.0), th_range=(0.70, 0.97),
    # black ~4/255, teal-leaning (skin-coloured on skin hues, see CINE250D)
    veil=0.0011, veil_rgb=(0.80, 1.02, 1.08), veil_skin_rgb=(1.2, 0.94, 0.84),
    # skin_sat 0.45 (review fix P25; it was 0.35): the dense toe greyed deep skin (code ~30 skin -> C .007); 0.5 made
    # very deep skin orange
    skin_ts=0.95, skin_sat=0.45, skin_cmin=0.03, ts_cfade=(0.045, 0.13, 0.9), ts_toe=0.16,
    # neutral axis (review fix P25):
    #   - teal shadows at about 0.6x the first version (h ~214, C ~.0065 at L .12, faded out by L .45).  At C ~.012
    #     the teal turned low-chroma deep skin yellow on the camera's own-film pass.
    #   - near-neutral highlights (C ~.004), following the print datasheet's "neutral highlights" instead of golden
    #     ones.  The plateau over L .75-.86 is kept: a falling red part there once made one 8-bit grey reversal at
    #     code 200 through the camera pass, from V rounding.
    tintL=[(0.0, -0.0024, -0.0018), (0.12, -0.0054, -0.0036), (0.25, -0.0045, -0.0027), (0.38, -0.0015, -0.0006), (0.50, 0.0, 0.001),
           (0.62, 0.001, 0.003), (0.75, 0.0012, 0.004), (0.86, 0.0012, 0.004), (0.94, 0.0008, 0.0025), (1.0, 0.0003, 0.001)],
    tl_cfade=(0.03, 0.10, 0.65), skin_tl=(0.9, 0.5), skin_tl_kappa=3.0, tl_toe=0.08),
}
ORDER = ["CINE250D", "CINE250D_PRINT"]
DEFAULT = "CINE250D"

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
    #    skin_sq / skin_dhL (CINE250D, new): skin hue squeeze and offset vs the INPUT lightness, weighted by w_sk and not
    #    faded at the cube surface.  skin_sq = [(L, k), ...]: skin-family hues move toward 55 deg by k x their distance
    #    from it (k <= 0.35 keeps the hue map monotonic); skin_dhL = [(L, deg), ...]: a plain offset.  Both pchip.
    #    Why: the camera's own-film pass widens the skin hue spread.  The loader's 17^3 YUV nodes around bright skin
    #    lie outside sRGB and are clipped before the look, so yellowish pale and tan skin drift ~+4..5 deg toward
    #    yellow and pinkish or deep skin drift 1..4 deg toward pink.  Strong reds / oranges are left alone: the weight
    #    fades out over C 0.10..0.16 (skin is C <= ~0.10).  Absent -> no change (gen_chungking.py behaviour).
    w_sq = w_sk * (1 - smoothstep(0.10, 0.16, C))
    if P.get("skin_sq"):
        kx, kk = zip(*P["skin_sq"])
        h1 = h1 - pchip(kx, kk)(L) * np.angle(np.exp(1j * (h - np.radians(55.0)))) * w_sq
    if P.get("skin_dhL"):
        kx, kd = zip(*P["skin_dhL"])
        h1 = h1 + np.radians(pchip(kx, kd)(L)) * w_sq
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
    #     Absent -> no change.  (Code identical to gen_chungking.py, where these knobs were introduced.)
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
        #    veil_skin_rgb (CINE250D, new): the colour the veil takes on skin instead of neutral.  A neutral lift of
        #    about the size of a deep-shadow skin colour itself turns it pink-grey (OKLab hue 44 -> 32 at code ~30);
        #    a faint skin-hued lift keeps deep skin brown.  Absent -> neutral (the gen_chungking.py behaviour).
        vs = np.asarray(P.get("veil_skin_rgb", (1.0, 1.0, 1.0)), float)
        vs = vs / (0.2126 * vs[0] + 0.7152 * vs[1] + 0.0722 * vs[2])
        lin = lin * (1 - v) + v * (vr * (1 - k) + k * vs)
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
        f.write("# original creative LUT, parametric OKLab model (gen_cine250d.py); input: Canon JPEG sRGB\n")
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
    """both presets; sel (the one written to CINE250D.CUB) is drawn last and bold"""
    from PIL import Image, ImageDraw
    cols = {"CINE250D": (200, 120, 40), "CINE250D_PRINT": (40, 110, 160)}
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
        d.rectangle([800 + k * 330, 780, 820 + k * 330, 800], fill=cols[nm])
        d.text((825 + k * 330, 784), nm + (" (CINE250D.CUB)" if nm == sel else ""), fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Cine 250D look: writes <outdir>/CINE250D.CUB from one preset")
    ap.add_argument("--preset", default=DEFAULT, choices=ORDER, help="look written to CINE250D.CUB (default %s)" % DEFAULT)
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rep = []
    # the chosen preset goes to CINE250D.CUB; checks for both presets (the chosen one first, "cub": true)
    for nm in [a.preset] + [k for k in ORDER if k != a.preset]:
        P = LOOKS[nm]
        out = build_cube(P)
        if nm == a.preset:
            write_cube(os.path.join(a.outdir, "CINE250D.CUB"), P, out)
        c = dict(checks(nm, P, out), cub=nm == a.preset); rep.append(c)
        print(json.dumps(c))
    # own names: gen_fuji_luts.py / gen_chungking.py write their checks / previews into the same dir
    with open(os.path.join(a.outdir, "checks_cine250d.json"), "w") as f:
        json.dump(rep, f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_cine250d_curves.png"), a.preset)
