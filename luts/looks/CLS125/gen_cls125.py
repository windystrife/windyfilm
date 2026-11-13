#!/usr/bin/env python3
"""Classic 125 (file code CLS125, catalogue id 61, type bw), a WindyStrife own film: a classic medium-speed, fine-grain
panchromatic black-and-white negative of the ISO 125 class, developed to the data sheet's curve and printed on a
measured grade-2 paper, as a scanned print.  Long straight tonal scale, clean highlights, open shadows.  The table is
luma only: R = G = B in the .cube, U = V = 0 in the T17.

The model is the family module bw_spectral of gen_cls125.py (catalogue/looks/CLS125), COPIED; the data and the look
calls are this film's.  Steps: sRGB -> linear; spectral filter F = w.(R,G,B) with w fitted to the film's own spectral
sensitivity x D55; exposure H = q K / S (S 125) with the JPEG mid grey as the metered grey; the film's published
characteristic curve placed by its ISO 6 speed point; 7 % enlarger stray light; a measured grade-2 paper curve; scan
levels 7 / 251; kernel fit (engineering, see apply_look).

Sources (MEASURED = digitised from a maker publication; GUESS = look call):
  [1] HARMAN technology, "FP4 PLUS" technical information, Nov 2018, p.1 (ISO 125/22, 6 stops over / 2 under), p.5
      CHARACTERISTIC CURVE (ILFOTEC HC 1+31, 8 min, 20 C, intermittent) -> data/char_cls125.csv.  Speed rated with ID-11.
  [2] ILFORD "FP4 PLUS" fact sheet, April 2004, p.1 SPECTRAL SENSITIVITY, wedge spectrogram to tungsten light (2850 K)
      -> data/spec_cls125.csv (2850 K source divided out; relative linear reading of the 0.5 / 1.0 axis is a GUESS).
  [3] HARMAN ILFORD MULTIGRADE FB CLASSIC technical information, Oct 2013, p.2: grade-2 paper curve (as CLS125).
  [4] CIE 1931 CMFs, CIE daylight S0-S2, ColorChecker (Ohta 1997): data/*.csv as CLS125.
  [5] ISO 6 speed point H_m = 0.8/S; ISO 6846 / ISO 2720 K = 12.5, q = 0.65 as CLS125.
GUESS: sensitivity 0 beyond 666 nm; the curve is read as log10 exposure (ISO 6 gradient check in the CSV header).

"""
import sys, os, json, argparse
import numpy as np

N = 33
HERE = os.path.dirname(os.path.abspath(__file__))

# ------------------------------------------------------------------ colour maths (as gen_port160.py)
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

XYZ_TO_SRGB = np.array([[3.2404542, -1.5371385, -0.4985314],
                        [-0.9692660, 1.8760108, 0.0415560],
                        [0.0556434, -0.2040259, 1.0572252]])
REC709_Y = np.array([0.2126, 0.7152, 0.0722])

# ------------------------------------------------------------------ sources
def read_csv(path):
    """-> (header list, float array, comment lines); blank cells -> nan"""
    com, rows = [], []
    for ln in open(path, encoding="utf-8"):
        s = ln.strip()
        if not s: continue
        if s.startswith("#"): com.append(s[1:].strip()); continue
        rows.append(s.split(","))
    hdr = rows[0]
    def num(x):
        try: return float(x)
        except ValueError: return np.nan                     # blank or a text note column
    return hdr, np.array([[num(x) for x in r] for r in rows[1:]]), com

class Sources:
    def __init__(self, ddir):
        self.dir = ddir
        _, self.cmf, _ = read_csv(os.path.join(ddir, "cie1931_2deg_10nm.csv"))
        _, self.s012, _ = read_csv(os.path.join(ddir, "cie_daylight_S012_10nm.csv"))
        self.cc_names, self.cc, _ = read_csv(os.path.join(ddir, "colorchecker_ohta1997_10nm.csv"))
        self.cc_names = self.cc_names[1:]
        _, self.sens, _ = read_csv(os.path.join(ddir, "spec_cls125.csv"))
        self.hd_hdr, self.hd, _ = read_csv(os.path.join(ddir, "char_cls125.csv"))
        _, self.pp, _ = read_csv(os.path.join(ddir, "mgfb_classic_char_filter2.csv"))
        self.wl = self.cmf[:, 0]
        assert np.all(self.s012[:, 0] == self.wl) and np.all(self.cc[:, 0] == self.wl)
        self.cbar = self.cmf[:, 1:] @ XYZ_TO_SRGB.T          # linear-sRGB colour-matching functions

    def daylight(self, cct):
        """CIE 015 daylight SPD at correlated colour temperature cct (4000..7000 K branch)"""
        T = cct
        xd = -4.6070e9 / T ** 3 + 2.9678e6 / T ** 2 + 0.09911e3 / T + 0.244063
        yd = -3.0 * xd ** 2 + 2.870 * xd - 0.275
        m = 0.0241 + 0.2562 * xd - 0.7341 * yd
        m1 = (-1.3515 - 1.7703 * xd + 5.9114 * yd) / m
        m2 = (0.0300 - 31.4424 * xd + 30.0717 * yd) / m
        return self.s012[:, 1] + m1 * self.s012[:, 2] + m2 * self.s012[:, 3]

    def sensitivity(self, curve="D1.0"):
        """film spectral sensitivity on the 10 nm grid (linear, energy basis); 0 beyond the last drawn point"""
        col = 1 if curve == "D1.0" else 2
        x, y = self.sens[:, 0], self.sens[:, col]
        ok = ~np.isnan(y); x, y = x[ok], y[ok]
        ls = np.interp(self.wl, x, y)
        S = 10 ** ls
        S[self.wl > x.max()] = 0.0                              # GUESS: no response beyond the last drawn point
        return S

    def hd_fit(self, t):
        """smooth fit of the digitised D-76 curve for t minutes (the raw points carry +-0.01..0.02 reading
        noise, which a point-to-point interpolation turns into slope wiggles):
        D(x) = f + g sp(x) + c sp(x)^2, sp(x) = s ln(1 + exp((x - x0) / s)) (toe -> straight line, slight bend);
        f, g, c by linear least squares, s and x0 by grid search; returns (params, rms residual)"""
        key = ("fit", t)
        if key in self.__dict__: return self.__dict__[key]
        cols = {6: 1, 8: 2, 10: 3, 12: 4}
        x, y = self.hd[:, 0], self.hd[:, cols[t]]
        ok = ~np.isnan(y); x, y = x[ok], y[ok]
        best = None
        for s_ in np.arange(0.10, 0.805, 0.01):
            for x0 in np.arange(-3.60, -1.795, 0.01):
                sp = s_ * np.logaddexp(0.0, (x - x0) / s_)
                A = np.stack([np.ones_like(x), sp, sp * sp], -1)
                p, *_ = np.linalg.lstsq(A, y, rcond=None)
                e = float(((A @ p - y) ** 2).sum())
                if best is None or e < best[0]: best = (e, s_, x0, p)
        e, s_, x0, p = best
        r = (dict(f=round(float(p[0]), 5), g=round(float(p[1]), 5), c=round(float(p[2]), 5), s=round(float(s_), 2), x0=round(float(x0), 2)),
             float(np.sqrt(e / len(x))))
        self.__dict__[key] = r
        return r

    @staticmethod
    def hd_eval(q, x):
        sp = q["s"] * np.logaddexp(0.0, (np.asarray(x, float) - q["x0"]) / q["s"])
        return q["f"] + q["g"] * sp + q["c"] * sp * sp

    def hd_curve(self, minutes):
        """the D-76 curve at `minutes`: linear in time between the smooth fits of the bracketing digitised
        curves (6, 8, 10, 12 min); returns (function log H -> D, info)"""
        ts = [6, 8, 10, 12]
        t0 = max([t for t in ts if t <= minutes]); t1 = min([t for t in ts if t >= minutes])
        q0, r0 = self.hd_fit(t0); q1, r1 = self.hd_fit(t1)
        a = 0.0 if t1 == t0 else (minutes - t0) / (t1 - t0)
        f = lambda x: (1 - a) * self.hd_eval(q0, x) + a * self.hd_eval(q1, x)
        return f, dict(t0=t0, t1=t1, a=a, fit0=q0, rms0=round(r0, 4), fit1=q1, rms1=round(r1, 4),
                       fog=(1 - a) * q0["f"] + a * q1["f"])

    PAPER_DMIN = 0.020                                          # the plotted curves start at D 0.02 (csv header)

    @staticmethod
    def _bspl(x, knots):
        """cubic B-spline basis (Cox-de Boor) on a uniform knot vector, (len(x), len(knots) - 4)"""
        x = np.asarray(x, float)
        B = ((x[:, None] >= knots[None, :-1]) & (x[:, None] < knots[None, 1:])).astype(float)
        for k in range(1, 4):
            a = (x[:, None] - knots[None, :-k - 1]) / (knots[k:-1] - knots[:-k - 1])[None, :]
            b = (knots[None, k + 1:] - x[:, None]) / (knots[k + 1:] - knots[1:-k])[None, :]
            B = a * B[:, :-1] + b * B[:, 1:]
        return B

    def paper_fit(self, lam=0.02):
        """smooth fit of the digitised grade-2 (filter 2) paper curve (net density vs log E): a penalised cubic
        B-spline (P-spline, knots every 0.15 log E, second-difference penalty lam), linear least squares; the
        digitised points carry +-0.01..0.02 reading noise.  Below log E 0 the curve is held at its value at 0,
        above 2.5 at its value at 2.5.  Returns (params, rms residual); params = the knots and coefficients."""
        if "_pfit" in self.__dict__: return self._pfit
        x, y = self.pp[:, 0], self.pp[:, 1] - self.PAPER_DMIN
        h = 0.15
        knots = np.arange(-3 * h, 2.5 + 4 * h, h)
        B = self._bspl(x, knots)
        n = B.shape[1]
        Dm2 = np.diff(np.eye(n), 2, axis=0); Dm1 = np.diff(np.eye(n), 1, axis=0)
        # monotone P-spline (Eilers 2005): an asymmetric penalty on falling coefficient steps, iterated;
        # rising coefficients guarantee a rising curve (the plateau gap 1.45..1.80 would otherwise overshoot)
        v = np.zeros(n - 1)
        for _ in range(50):
            c = np.linalg.solve(B.T @ B + lam * Dm2.T @ Dm2 + 1e6 * Dm1.T @ np.diag(v) @ Dm1, B.T @ y)
            v_new = (np.diff(c) < 0).astype(float)
            if np.array_equal(v_new, v): break
            v = v_new
        q = dict(knots=knots, coef=c)
        grid = np.linspace(0, 2.5, 2501); dg = (self._bspl(grid, knots) @ c)
        assert np.all(np.diff(dg) > -1e-9), "paper fit must not decrease"
        q["Dm"] = float(dg[-1]); q["grid"] = grid; q["dg"] = np.maximum.accumulate(dg)
        self._pfit = (q, float(np.sqrt(((B @ c - y) ** 2).mean())))
        return self._pfit

    @classmethod
    def paper_eval(cls, q, x):
        x = np.asarray(x, float)
        return np.interp(np.clip(x, 0.0, 2.5), q["grid"], q["dg"])

    @staticmethod
    def paper_inv(q, Dn):
        """log E giving net density Dn (0 < Dn < Dm)"""
        return np.interp(Dn, q["dg"], q["grid"])

# ------------------------------------------------------------------ the film
LOOKS = {
 "CLS125": dict(
    title="CLS125 - Classic 125 (WindyStrife original LUT)",
    note="Classic fine-grain panchromatic B&W 125: long straight tonal scale, clean highlights, open shadows, grade-2 print.",
    curve="D1.0", fit_lo=400, fit_hi=700, alpha=1e-3, cct=5500 * 1.4388 / 1.4380,
    iso=125.0, q=0.65, K=12.5, grey=0.18, ev_place=0.0,
    dev_min=8.0, g_scale=0.95,
    stray=0.07, shadow_dD=0.10, print_anchor="mid", hi_F=1.0, grey_print_D=0.70,
    black_code=7.0, white_code=251.0,
    e_knee=40.0, e_limit=72.0, flat_gamma=0.0, y_ref="mid", slope_ref=0.8, neg_limit=6.0, mono_closure=True,
    core_c0=None, core_c1=None, e_scale=None, fade_lo=None, fade_hi=None),
}

ORDER = ["CLS125"]
DEFAULT = "CLS125"

def fit_weights(src, curve, lo, hi, alpha, cct):
    """min |s - K w|^2 + alpha |w - w709|^2 s.t. sum w = 1; s, K = S x D55 and cbar x D55, unit area"""
    m = (src.wl >= lo) & (src.wl <= hi)
    E = src.daylight(cct)
    s = src.sensitivity(curve)[m] * E[m]; s = s / s.sum()
    Km = src.cbar[m] * E[m][:, None]; Km = Km / Km.sum(0)
    A = np.zeros((4, 4)); A[:3, :3] = Km.T @ Km + alpha * np.eye(3); A[:3, 3] = 1; A[3, :3] = 1
    w = np.linalg.solve(A, np.r_[Km.T @ s + alpha * REC709_Y, 1.0])[:3]
    resid = float(np.sqrt(((s - Km @ w) ** 2).sum() / (s ** 2).sum()))
    return w, resid, s, Km @ w

def neg_curve(P, src):
    """the film's published characteristic curve (relative log exposure x, density D), smoothed (gaussian 0.06) and placed
    on absolute log H by its ISO 6 speed point: H_m = 0.8 / S at density base+fog+0.10.  P['g_scale'] scales the density
    above base+fog (look call), P['fog'] overrides base+fog (look call)."""
    x, D = src.hd[:, 0], src.hd[:, 1]
    g = np.arange(-1.0, 6.0, 0.005)
    Dg = np.interp(g, x, D)
    k = np.exp(-0.5 * (np.arange(-60, 61) * 0.005 / 0.06) ** 2); k /= k.sum()
    Ds = np.convolve(np.r_[np.full(60, Dg[0]), Dg, np.full(60, Dg[-1])], k, mode="valid")
    Ds = np.maximum.accumulate(Ds)
    fog0 = float(D.min()); fog = P.get("fog", fog0)
    Ds = fog + P.get("g_scale", 1.0) * (Ds - fog0)
    xs = float(np.interp(fog + 0.10, Ds, g))
    shift = np.log10(0.8 / P["iso"]) - xs
    lh = g + shift
    f = lambda v: np.interp(np.asarray(v, float), lh, Ds) + 1e-4 * np.asarray(v, float)
    return f, dict(fog=fog, x_speed_rel=round(xs, 3), shift=round(float(shift), 3), g_scale=P.get("g_scale", 1.0), source="single published curve, ISO 6 speed point placement")

class Chain:
    """the bw_spectral family module for one preset"""
    def __init__(self, P, src):
        self.P, self.src = P, src
        self.w, self.fit_resid, self.fit_s, self.fit_model = fit_weights(src, P["curve"], P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
        self.logH_grey = np.log10(P["q"] * P["K"] / P["iso"]) + P["ev_place"] * np.log10(2.0)
        self._neg, self.hd_info = neg_curve(P, src)
        self.fog = float(self.hd_info["fog"])
        xx = np.linspace(-6, 0.5, 2000); dd = self._neg(xx)
        assert np.all(np.diff(dd) > 0), "negative curve must rise"
        # speed point of this curve (density fog + 0.10) and the metered grey's distance from it
        self.logH_speed = float(np.interp(self.fog + 0.10, dd, xx))
        # paper: the measured grade-2 (filter 2) curve, smooth fit; ISO 6846 points T (net 0.04) and S (0.90 net Dmax)
        self.pq, self.paper_rms = src.paper_fit()
        self.E_T = float(src.paper_inv(self.pq, 0.04)); self.E_S = float(src.paper_inv(self.pq, 0.90 * self.pq["Dm"]))
        self.LER = self.E_S - self.E_T                          # the paper's log exposure range (ISO range / 100)
        # the negative's effective density range between the deepest textured shadow (speed point) and the
        # brightest textured highlight (a 90 % diffuse white), for the ISO 6846 annex A match report
        self.D_sh = float(self.d_eff(self.fog + P["shadow_dD"]))
        self.D_hi = float(self.d_eff(self.neg(self.logH(np.array(P["hi_F"])))))
        self.neg_range = self.D_hi - self.D_sh
        # print exposure: paper log E = c - D_eff.  "mid" (used): the metered grey gets the midpoint of the paper's
        # ISO range in log exposure, (log E_T + log E_S) / 2; "highlight": hi_F prints at T (annex B);
        # "grey": the metered grey prints at net reflection density grey_print_D (variants kept for the record)
        self.D_g = float(self.d_eff(self.neg(self.logH(np.array(P["grey"])))))
        if P["print_anchor"] == "mid":
            self.c = 0.5 * (self.E_T + self.E_S) + self.D_g
        elif P["print_anchor"] == "highlight":
            self.c = self.E_T + self.D_hi
        else:
            self.c = float(src.paper_inv(self.pq, P["grey_print_D"])) + self.D_g
        # levels: the print's darkest (F = 0) and lightest (F = 1) tones
        self.R_k = float(self.reflectance(np.array(0.0))); self.R_w = float(self.reflectance(np.array(1.0)))
        self.b0 = float(srgb_to_lin(P["black_code"] / 255.0)); self.w0 = float(srgb_to_lin(P["white_code"] / 255.0))

    def logH(self, F):
        return self.logH_grey + np.log10(np.maximum(F, 1e-12) / self.P["grey"])

    def neg(self, lh):
        return self._neg(lh)

    def d_eff(self, D):
        return self.fog + (1 - self.P["stray"]) * (np.asarray(D, float) - self.fog)

    def paper_x(self, Deff):
        """normalised paper log exposure: 0 at T (highlight), 1 at S (shadow)"""
        return (self.c - Deff - self.E_T) / self.LER

    def print_net_density(self, F):
        return self.src.paper_eval(self.pq, self.c - self.d_eff(self.neg(self.logH(F))))

    def reflectance(self, F):
        return 10 ** (-self.print_net_density(F))

    def tone_F(self, F):
        """film exposure F (0..1) -> output linear (display)"""
        R = self.reflectance(F)
        return self.b0 + (self.w0 - self.b0) * (R - self.R_k) / (self.R_w - self.R_k)

    def spectral_F(self, refl):
        """the film's own spectral model: F of reflectance spectra (n, wl) under D55, grey-normalised"""
        m = (self.src.wl >= self.P["fit_lo"]) & (self.src.wl <= self.P["fit_hi"])
        E = self.src.daylight(self.P["cct"])
        s = self.src.sensitivity(self.P["curve"])[m] * E[m]
        return np.asarray(refl)[..., m] @ s / s.sum()

    def camera_rgb(self, refl):
        """white-balanced (D55 grey -> R = G = B) linear sRGB of reflectance spectra under D55"""
        m = (self.src.wl >= self.P["fit_lo"]) & (self.src.wl <= self.P["fit_hi"])
        E = self.src.daylight(self.P["cct"])
        K = self.src.cbar[m] * E[m][:, None]
        return np.asarray(refl)[..., m] @ K / K.sum(0)

_CH = {}
def chain(P, src=None):
    k = id(P)
    if k not in _CH:
        _CH[k] = Chain(P, src or Sources(os.environ.get("CLS125_DATA", os.path.join(HERE, "data"))))
    return _CH[k]

YUV_Y = np.array([0.299, 0.587, 0.114])                    # the camera's luma (BT.601 on the JPEG codes)

def soft_limit(e, knee, lim):
    """identity for |e| <= knee, then a tanh shoulder that approaches +-lim (C1-continuous)"""
    a = np.abs(e); s = lim - knee
    return np.sign(e) * np.where(a <= knee, a, knee + s * np.tanh((a - knee) / s))

def apply_look_raw(rgb, P):
    """the film model alone: display sRGB -> output code (0..1), no kernel fit"""
    ch = chain(P)
    return np.clip(lin_to_srgb(ch.tone_F(srgb_to_lin(rgb) @ ch.w)), 0, 1)

def apply_look(rgb, P):
    """rgb (...,3) display sRGB 0..1 -> (...,3) display sRGB 0..1, R = G = B exactly.
    Kernel fit (model step 7): G = the grey output at the camera luma y601 of the input (what the kernel's
    neutral curve N gives), E = film output - G in codes; E -> soft limit -> flat E at the mid-gamut luma of
    the same camera chroma -> slope scaling -> negative-side limit (optional coring / scale / fade stages are
    off in the preset); output = G + E (R = G = B)."""
    ch = chain(P)
    rgb = np.asarray(rgb, np.float64)
    O = apply_look_raw(rgb, P)
    y = np.clip(rgb @ YUV_Y, 0, 1)
    Gf = lambda yy: np.clip(lin_to_srgb(ch.tone_F(srgb_to_lin(yy))), 0, 1)
    G = Gf(y)
    E = (O - G) * 255.0
    if P.get("e_limit"):
        E = soft_limit(E, P["e_knee"], P["e_limit"])
    if P.get("flat_gamma") is not None:
        # E at the same camera chroma (u, v) but at the reference luma y_ref (clipped into the in-gamut luma range
        # of that chroma); the table's E is gamma x E(y) + (1 - gamma) x E(y_ref): less luma dependence at fixed u, v
        u = rgb @ np.array([-0.168736, -0.331264, 0.5]); v = rgb @ np.array([0.5, -0.418688, -0.081312])
        off = np.stack([1.402 * v, -0.344136 * u - 0.714136 * v, 1.772 * u], -1)
        lo, hi = np.maximum(0.0, -off.min(-1)), np.minimum(1.0, 1.0 - off.max(-1))
        yc = 0.5 * (lo + hi) if P["y_ref"] == "mid" else np.clip(P["y_ref"] / 255.0, lo, hi)
        rc = np.clip(yc[..., None] + off, 0, 1)
        Ec = (apply_look_raw(rc, P) - Gf(yc)) * 255.0
        if P.get("e_limit"):
            Ec = soft_limit(Ec, P["e_knee"], P["e_limit"])
        E = P["flat_gamma"] * E + (1 - P["flat_gamma"]) * Ec
    if P.get("slope_ref") is not None:
        # toe / shoulder: E scaled by the grey curve's local slope where it is below slope_ref (monotone in R, G, B)
        h = 1.0 / 255.0
        Gp = (Gf(np.clip(y + h, 0, 1)) - Gf(np.clip(y - h, 0, 1))) / (np.clip(y + h, 0, 1) - np.clip(y - h, 0, 1))
        r = np.maximum(Gp / P["slope_ref"], 1e-6)
        E = E * (r ** -4 + 1.0) ** -0.25                      # smooth min(1, r)
    if P.get("core_c0") is not None:
        # chroma coring: no spectral luma shift for camera chroma |(U, V)| <= core_c0, full from core_c1 (smoothstep)
        u = rgb @ np.array([-0.168736, -0.331264, 0.5]); v = rgb @ np.array([0.5, -0.418688, -0.081312])
        t = np.clip((np.hypot(u, v) * 255.0 - P["core_c0"]) / (P["core_c1"] - P["core_c0"]), 0, 1)
        E = E * t * t * (3 - 2 * t)
    if P.get("neg_limit") is not None:
        nl = P["neg_limit"]
        E = np.where(E >= 0, E, -soft_limit(-E, 0.5 * nl, nl) if nl > 0 else 0.0)
    if P.get("e_scale") is not None:
        E = E * P["e_scale"]
    if P.get("fade_hi"):
        t = np.clip((G * 255.0 - P["fade_lo"]) / (P["fade_hi"] - P["fade_lo"]), 0, 1)
        E = E * t * t * (3 - 2 * t)
    v = np.clip(G + E / 255.0, 0, 1)
    return np.repeat(v[..., None], 3, -1)

# ------------------------------------------------------------------ cube io + checks
def build_cube(P, n=N):
    x = np.linspace(0, 1, n)
    B, G, R = np.meshgrid(x, x, x, indexing="ij")          # [b][g][r] -> red fastest when flattened
    out = apply_look(np.stack([R, G, B], -1), P)
    if P.get("mono_closure"):
        # monotone closure: running maximum along +R, +G, +B (in that order; each pass keeps the earlier axes
        # non-decreasing), so adding any primary never darkens the output
        v = out[..., 0]
        for ax in (2, 1, 0):
            v = np.maximum.accumulate(v, axis=ax)
        out = np.repeat(v[..., None], 3, -1)
    return out

def write_cube(path, P, out):
    n = out.shape[0]
    assert np.all(out[..., 0] == out[..., 1]) and np.all(out[..., 1] == out[..., 2]), "B&W cube must be neutral"
    with open(path, "w", newline="\n") as f:
        f.write('TITLE "%s"\n' % P["title"])
        f.write("# original creative LUT, parametric spectral B&W model (gen_cls125.py); input: the camera Neutral JPEG, sRGB\n")
        f.write("LUT_3D_SIZE %d\n" % n)
        for r, g, b in out.reshape(-1, 3):
            f.write("%.6f %.6f %.6f\n" % (r, g, b))

def luma(rgb):
    lin = srgb_to_lin(rgb)
    return lin_to_srgb(0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2])

def jbw_generator(P):
    """J-BW in the generator's own float domain (no camera): spectral F vs w.RGB through the same tone"""
    ch = chain(P); cc = ch.src.cc[:, 1:].T
    Fsp = ch.spectral_F(cc); rgb = ch.camera_rgb(cc)
    Lsp = np.cbrt(ch.tone_F(Fsp))
    Lmd = np.cbrt(ch.tone_F(np.clip(np.clip(rgb, 0, 1) @ ch.w, 0, None)))
    d = Lmd - Lsp
    return dict(mean_abs_dL=round(float(np.abs(d).mean()), 4), max_abs_dL=round(float(np.abs(d).max()), 4),
                per_patch={n: round(float(v), 4) for n, v in zip(ch.src.cc_names, d)})

def checks(name, P, out):
    ch = chain(P)
    n = out.shape[0]
    Yo = luma(out)
    viol = {ax: int((np.diff(Yo, axis=ax) < -1e-3).sum()) for ax in (0, 1, 2)}
    grey = out[np.arange(n), np.arange(n), np.arange(n)]
    d2 = max(float(np.abs(np.diff(out, 2, axis=ax)).max()) for ax in (0, 1, 2))
    d2p = max(float(np.percentile(np.abs(np.diff(out, 2, axis=ax)), 99.9)) for ax in (0, 1, 2))
    g = np.array([0, 4, 8, 16, 32, 48, 64, 96, 118, 140, 160, 200, 230, 245, 255]) / 255.0
    ramp = apply_look(np.stack([g, g, g], -1), P)[:, 0] * 255
    # the transcription check of the ColorChecker spectra: sRGB under D65 vs the chart's published sRGB
    D65 = ch.src.daylight(6500 * 1.4388 / 1.4380)
    xyz = (ch.src.cc[:, 1:].T * D65) @ ch.src.cmf[:, 1:] / (D65 @ ch.src.cmf[:, 2])
    cc_srgb = np.round(lin_to_srgb(np.clip(xyz @ XYZ_TO_SRGB.T, 0, 1)) * 255).astype(int)
    w03, r03, _, _ = fit_weights(ch.src, "D0.3", P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
    w0a, _, _, _ = fit_weights(ch.src, P["curve"], P["fit_lo"], P["fit_hi"], 0.0, P["cct"])
    w2a, _, _, _ = fit_weights(ch.src, P["curve"], P["fit_lo"], P["fit_hi"], 1e-2, P["cct"])
    return dict(look=name, neutral=bool(np.all(out[..., 0] == out[..., 1]) and np.all(out[..., 1] == out[..., 2])),
                luma_violations_rgb_axes=viol, grey_monotonic=bool(np.all(np.diff(luma(grey)) > 0)),
                black_out=round(float(out[0, 0, 0, 0]) * 255, 1), white_out=round(float(out[-1, -1, -1, 0]) * 255, 1),
                grey_ramp_in_out={str(int(round(a * 255))): round(float(b), 1) for a, b in zip(g, ramp)},
                max_abs_d2=round(d2, 4), p999_abs_d2=round(d2p, 4),
                spectral_weights_rgb=np.round(ch.w, 4).tolist(), fit_rel_residual=round(ch.fit_resid, 3),
                weights_alpha0=np.round(w0a, 4).tolist(), weights_alpha_1em2=np.round(w2a, 4).tolist(),
                weights_from_D03_curve=np.round(w03, 4).tolist(),
                logH_grey=round(float(ch.logH_grey), 3), logH_speed_point=round(ch.logH_speed, 3),
                grey_above_speed_point=round(float(ch.logH_grey - ch.logH_speed), 3),
                neg_density_grey=round(float(ch.neg(ch.logH_grey)), 3), neg_density_white_F1=round(float(ch.neg(ch.logH(np.array(1.0)))), 3),
                neg_fog=round(ch.fog, 3), neg_fit=ch.hd_info,
                paper_fit=dict(net_Dmax=round(ch.pq["Dm"], 3), logE_T=round(ch.E_T, 3), logE_S=round(ch.E_S, 3)), paper_fit_rms=round(ch.paper_rms, 4),
                paper_LER=round(ch.LER, 3), paper_ISO_range="R%d (sheet: R95)" % int(round(100 * ch.LER)),
                neg_effective_range_shadow_to_highlight=round(ch.neg_range, 3),
                paper_x_speed_point=round(float(ch.paper_x(ch.D_sh)), 3),
                paper_x_F1=round(float(ch.paper_x(ch.d_eff(ch.neg(ch.logH(np.array(1.0)))))), 3),
                paper_x_grey=round(float(ch.paper_x(ch.d_eff(ch.neg(ch.logH(np.array(P["grey"])))))), 3),
                print_net_density_F0=round(float(ch.print_net_density(np.array(0.0))), 3),
                print_net_density_grey=round(float(ch.print_net_density(np.array(P["grey"]))), 3),
                print_net_density_F1=round(float(ch.print_net_density(np.array(1.0))), 3),
                jbw_generator=jbw_generator(P), colorchecker_srgb_D65=dict(zip(ch.src.cc_names, cc_srgb.tolist())))

# ------------------------------------------------------------------ curves diagram
def curves_png(path, sel=DEFAULT):
    from PIL import Image, ImageDraw
    P = LOOKS[sel]; ch = chain(P)
    Wd, Hd = 1600, 1240
    im = Image.new("RGB", (Wd, Hd), (250, 250, 248)); d = ImageDraw.Draw(im)
    def panel(ox, oy, s, xr, yr, title, xl, yl):
        d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0))
        for k in range(1, 4):
            d.line([ox + k * s / 4, oy, ox + k * s / 4, oy - s], fill=(222, 222, 222))
            d.line([ox, oy - k * s / 4, ox + s, oy - k * s / 4], fill=(222, 222, 222))
        d.text((ox, oy - s - 18), title, fill=(0, 0, 0))
        d.text((ox + 30, oy + 6), "x: " + xl, fill=(0, 0, 0)); d.text((ox + 6, oy - s + 6), "y: " + yl, fill=(80, 80, 80))
        d.text((ox - 4, oy + 18), "%g" % xr[0], fill=(90, 90, 90)); d.text((ox + s - 20, oy + 18), "%g" % xr[1], fill=(90, 90, 90))
        d.text((ox - 40, oy - 8), "%g" % yr[0], fill=(90, 90, 90)); d.text((ox - 40, oy - s - 4), "%g" % yr[1], fill=(90, 90, 90))
        X = lambda x: ox + (np.asarray(x) - xr[0]) / (xr[1] - xr[0]) * s
        Y = lambda y: oy - (np.asarray(y) - yr[0]) / (yr[1] - yr[0]) * s
        return X, Y
    def line(X, Y, xs, ys, col, w=2):
        pts = [(float(a), float(b)) for a, b in zip(X(xs), Y(ys)) if np.isfinite(a) and np.isfinite(b)]
        d.line(pts, fill=col, width=w)
    # 1. grey tone curve display in -> out, and the luminance-mono reference (identity)
    X, Y = panel(70, 560, 480, (0, 255), (0, 255), "CLS125 grey ramp: display in -> out (codes)", "input code (Neutral JPEG grey)", "output code")
    g = np.linspace(0, 1, 256)
    line(X, Y, [0, 255], [0, 255], (170, 170, 170), 1)
    line(X, Y, g * 255, apply_look(np.stack([g, g, g], -1), P)[:, 0] * 255, (30, 30, 30), 3)
    # 2. spectral fit
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    X, Y = panel(640, 560, 480, (400, 700), (0, 0.1), "film S(lambda) x D55 (black) vs fit w.cbar x D55 (red), unit area", "wavelength nm", "relative")
    line(X, Y, ch.src.wl[m], ch.fit_s, (0, 0, 0), 3); line(X, Y, ch.src.wl[m], ch.fit_model, (200, 40, 40), 2)
    Kn = ch.src.cbar[m] * ch.src.daylight(P["cct"])[m][:, None]; Kn = Kn / Kn.sum(0)
    yv = np.array(REC709_Y) @ Kn.T
    line(X, Y, ch.src.wl[m], yv, (60, 120, 200), 1)
    d.text((660, 110), "w = R %.3f  G %.3f  B %.3f   (blue thin: Rec.709 luminance)" % tuple(ch.w), fill=(0, 0, 0))
    # 3. negative curve
    X, Y = panel(70, 1160, 480, (-4, 1), (0, 3), "negative: F-4017 p.8 D-76 points 6/8 min (dots), fits (grey), 7.75 min used (black)", "log H (lux s)", "density")
    xx = np.linspace(-4, 0.3, 300)
    for t, k in ():
        q, _ = ch.src.hd_fit(t); line(X, Y, xx, ch.src.hd_eval(q, xx), (150, 150, 150), 1)
        for a, b in ch.src.hd[:, [0, k]]:
            if np.isfinite(b): d.ellipse([X(a) - 2, Y(b) - 2, X(a) + 2, Y(b) + 2], fill=(90, 90, 90))
    line(X, Y, xx, ch.neg(xx), (0, 0, 0), 3)
    for x, col in ((ch.logH_speed, (40, 140, 40)), (ch.logH_grey, (200, 40, 40)), (float(ch.logH(np.array(1.0))), (40, 40, 220))):
        d.line([X(x), 1160, X(x), 680], fill=col, width=2)
    d.text((80, 1190), "green: speed point (base+fog + 0.10, log H %.2f), red: mid-grey 118 (metered, log H %.2f), blue: code 255" % (ch.logH_speed, ch.logH_grey), fill=(0, 0, 0))
    # 4. paper curve
    X, Y = panel(640, 1160, 480, (0, 2.5), (0, 2.2), "grade-2 paper: MG FB Classic filter 2 points (dots), fit (black); LER %.2f" % ch.LER, "paper log E (relative)", "net density")
    xs = np.linspace(0, 2.5, 300); line(X, Y, xs, ch.src.paper_eval(ch.pq, xs), (0, 0, 0), 3)
    for a, b in ch.src.pp[:, :2]:
        d.ellipse([X(a) - 2, Y(b - ch.src.PAPER_DMIN) - 2, X(a) + 2, Y(b - ch.src.PAPER_DMIN) + 2], fill=(90, 90, 90))
    for F, col in ((0.0, (40, 120, 40)), (P["grey"], (200, 40, 40)), (1.0, (40, 40, 200))):
        x = float(ch.c - ch.d_eff(ch.neg(ch.logH(np.array(F))))); d.line([X(x), 1160, X(x), 680], fill=col, width=2)
    for x in (ch.E_T, ch.E_S): d.line([X(x), 1160, X(x), 1140], fill=(0, 0, 0), width=3)
    d.text((660, 1190), "green: F 0 (base+fog), red: grey, blue: F 1 (code 255); ticks: ISO 6846 T and S", fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Classic 125 B&W look: writes <outdir>/CLS125.CUB")
    ap.add_argument("--data", default=os.path.join(HERE, "data"), help="source csv directory")
    ap.add_argument("--set", default=None, help="JSON overrides of the preset parameters (experiments only)")
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.environ["CLS125_DATA"] = a.data
    os.makedirs(a.outdir, exist_ok=True)
    P = LOOKS[DEFAULT]
    if a.set:
        P = dict(P); P.update(json.loads(a.set)); LOOKS[DEFAULT] = P
    out = build_cube(P)
    write_cube(os.path.join(a.outdir, "CLS125.CUB"), P, out)
    c = checks(DEFAULT, P, out)
    print(json.dumps(c))
    with open(os.path.join(a.outdir, "checks_cls125.json"), "w") as f:
        json.dump(c, f, indent=1)
    ch = chain(P)
    prov = dict(code="CLS125", id=61, type="bw", generator="gen_cls125.py", params={k: v for k, v in P.items()},
                sources=["Kodak Professional F-4017 TRI-X 320 and 400 Films, Dec 2016: p.7 Spectral Sensitivity Curves (D=1.0); p.8 Characteristic Curves 400TX 35 mm D-76 large tank 20 C; p.3 D-76 large tank 68 F 7 3/4 min; p.2 contrast index 0.56; p.6 rms granularity 17",
                         "HARMAN technology ILFORD MULTIGRADE FB CLASSIC technical information, Oct 2013: p.2 characteristic curves, filter 2 (grade 2); p.1 ISO range table, filter 2 = R95",
                         "CIE 018:2019 Table 6 CIE 1931 2-deg CMFs (DOI 10.25039/CIE.DS.xvudnb9b)",
                         "CIE daylight components S0 S1 S2 (CIE_illum_Dxx_comp.csv, CIE 015:2018 model)",
                         "ColorChecker Classic spectral reflectances, Ohta 1997 (colour-science DATA_COLORCHECKER_N_OHTA), J-BW only",
                         "ISO 6846:1983 clause 6.2.1, annexes A and B; ISO 6:1993 speed point; ISO 2720 K = 12.5"],
                guesses=["Neutral JPEG = sRGB-encoded scene light", "camera WB diagonal in linear sRGB", "ev_place 0 (box speed)",
                         "stray light 7 % (ISO 6846 annex A range 5-10 %)", "print exposure: metered grey at the midpoint of the paper ISO range",
                         "scan levels black 7 / white 251", "linear-in-time interpolation 6 -> 8 min", "fitted negative toe below log H -3.4",
                         "paper Dmin 0.020 and paper toe below log E 0.30 (mean of filter 1 and 3 traces)", "sensitivity 0 beyond 665 nm",
                         "kernel fit (engineering, model step 7): E soft limit 32/64 codes, flat E at the mid-gamut luma, "
                         "slope scaling below G' 0.8, negative E limited to 6 codes, monotone closure"],
                derived=dict(weights=np.round(ch.w, 5).tolist(), logH_grey=round(float(ch.logH_grey), 4), LER=round(ch.LER, 4),
                             print_c=round(float(ch.c), 4), neg_effective_range=round(ch.neg_range, 4)))
    with open(os.path.join(a.outdir, "provenance_cls125.json"), "w") as f:
        json.dump(prov, f, indent=1)
    # the film's own spectral model for judge v2 --bw-model (J-BW self-consistency): the data sheet sensitivity
    # on the fit range (energy basis, 0 outside 400..700 nm as in the fit) and the fitted linear-sRGB weights
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    s = ch.src.sensitivity(P["curve"])[m]
    with open(os.path.join(a.outdir, "bw_model_cls125.json"), "w") as f:
        json.dump(dict(note="CLS125 spectral model: F-4017 p.7 D=1.0 sensitivity (energy basis, relative), fit range 400-700 nm; weights = its D55 fit on linear sRGB",
                       sensitivity=dict(nm=ch.src.wl[m].tolist(), s=np.round(s / s.max(), 6).tolist()),
                       weights=np.round(ch.w, 6).tolist()), f, indent=1)
    curves_png(os.path.join(a.outdir, "preview_cls125_curves.png"))
