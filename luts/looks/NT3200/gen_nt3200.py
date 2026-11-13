#!/usr/bin/env python3
"""Night 3200 (file code NT3200, catalogue id 46, type 2 = B&W), a WindyStrife own film: a T-grain multi-speed
panchromatic black-and-white negative rated and push-developed at EI 3200 (the maker's EI 3200 starting-point time in a
fine-grain T-grain developer), printed on the same measured grade-2 paper as Press 400 and scanned.  Shadows block up
(they fall into the negative's toe), mid-tones are contrasty, highlights stay printable.  The table is luma only: every
output colour is an exact neutral (R = G = B in the .cube; U = V = 0 in the T17).

Family module "bw_spectral" (PLAN 6.2), copied from gen_prs400.py (catalogue/looks/PRS400, batch 1) so every B&W film uses
ONE pipeline; only the film data (spectral curve, characteristic curve, rating) and the kernel-fit numbers differ.
Model (input = the camera's Neutral JPEG, display sRGB; the camera itself is set to ISO 3200):
  1. sRGB -> linear (sRGB EOTF; GUESS: the Neutral picture style is treated as sRGB-encoded scene light).
  2. Spectral filter: film exposure F = w . (R, G, B)_lin.  w is a regularised least-squares fit of the data sheet's
     spectral sensitivity S(lambda) x D55 by the linear-sRGB colour-matching functions x D55 (each normalised to unit
     area, so a D55 grey of linear value g gives F = g exactly; sum w = 1 is a hard constraint; ridge alpha = 1e-3 toward
     the Rec.709 luminance weights).  The sheet gives two curves, for net densities 0.3 and 1.0 above D-min.  At EI 3200
     the metered grey prints from net density 0.48 and everything darker from less (checks: neg_net_density_by_EV), so the
     0.3 curve is the one that describes this film's working range and is used (the 1.0 curve's weights are reported).
  3. Exposure: log10 H = log10(q K / S) + log10(F / 0.18) + ev_place * log10(2), S = 3200 (the rating), K = 12.5,
     q = 0.65 (the camera / meter exposure equation; the metered mid-grey gets H = 8.1 / S lux s).  JPEG mid-grey (code 118,
     linear 0.18) is the metered grey.
  4. Negative: the data sheet's T-MAX Developer small-tank curve at 12 min, the sheet's own EI 3200 starting-point time
     (smooth fit of the digitised curve, toe -> straight line, rms 0.008 D, fitted up to log H -0.45: the camera's JPEG
     white needs the curve only up to about -1.05).
     ANCHOR (the one non-trivial reading of the source): the plotted absolute log H axis of this 2018 sheet puts the curve
     family's ISO speed at about 160, against the same sheet's statement that the nominal (ISO-method) speed in this
     developer is EI 1000, and against the October 2007 edition (F-4016 p.26), whose curves for the same developer sit
     about 1.0 log H lower.  The curves' SHAPES are used as plotted; their position is set by the stated speed: the ISO 6
     criterion (speed point m at base+fog + 0.10; point n 1.30 log H above m with D_n - D_m = 0.80) is evaluated on the
     6, 7, 10 and 12 min fits, the development meeting it is found by interpolation in time (8.7 min, next to the sheet's
     EI 400 time of 8 1/2 min and its CI 0.56), and the whole family is shifted so that this development has
     S = 0.8 / H_m = 1000.  On that axis the 12 min curve's speed point is 0.55 log H below the metered grey at EI 3200
     (1.8 stops), where a box-speed rating gives 1.0 log H: tones more than about 1.8 stops under grey print from
     net density < 0.10 (the toe) and block up.  That is the claim C1 below, taken from the push data.
  5. Print: as Press 400.  The negative modulates the paper exposure (paper log E = c - D_eff), D_eff = the density scale
     reduced by 7 % enlarger stray light (ISO 6846 annex A: 5-10 %).  Paper: the MEASURED grade-2 (filter 2) curve of a
     published variable-contrast glossy fibre paper (monotone P-spline, rms 0.005 D, LER 0.94 = R95).  Print exposure c
     (GUESS rule, as Press 400): the metered grey gets the midpoint of the paper's ISO range in log exposure.  ISO 6846
     annex A check: the negative's effective range from its speed point to the JPEG white is reported (checks); the sheet
     says its times "produce negative contrast suitable for printing with a diffusion enlarger" (p.2), i.e. a normal grade.
  6. Scan: the print's reflectance relative to paper white is scanned with levels set to the print's own darkest and
     lightest tones (input code 0 and code 255), output black / white codes 7 / 251 (GUESS, as Press 400), linear in
     reflectance, then sRGB-encoded.  Every output is a neutral: R = G = B.
  7. Kernel fit (ENGINEERING, not film data; apply_look + build_cube), the Press 400 stages unchanged in kind: the shipped
     2.5D kernel (fr_film.c) computes Y' = N[y] + E(ym, u, v) with E one signed byte per (65 luma x 17 x 17) node looked up
     at the 4:2:2 pair-mean luma; the table stores G(y601) + E', G the model's grey curve and E' the spectral shift
     E = film(rgb) - G with (a) a soft limit, (b) E taken at the mid-gamut luma of its camera chroma ("flat" E),
     (c) E scaled by smooth-min(1, G' / slope_ref) where the grey slope is low (here the toe: the blocked shadows),
     (d) the negative side limited, (e) the cube closed monotone along +R, +G, +B.  The numbers are in the preset; they
     were tuned against judge v2 (J1 / J4 / J8 / J-BW) and are listed with the rejected settings in README.md.
  The OKLab hue / chroma stages of gen_port160.py / gen_chungking.py are identity for a B&W film (output chroma is 0 by
  construction); the OKLab maths is kept for the checks (L of a neutral = cbrt(linear)).

Sources (MEASURED = digitised or copied from a public publication; GUESS = a call made here).  The digitised points are
in data/*.csv, each with its publication / date / page / figure header.
  MEASURED  Kodak Alaris F-4001 "KODAK PROFESSIONAL T-MAX P3200 Black & White Negative Film", March 2018 (Revised 03-18):
              p.7 "Spectral Sensitivity Curves" (0.3 and 1.0 greater than D-min; D-76, 1.4 s)
                  -> data/f4001_spectral_sensitivity.csv
              p.8 "Characteristic Curves", T-MAX Developer, Small Tank, 20 C, 6 / 7 / 10 / 12 min
                  -> data/f4001_tmax_dev_char.csv
              p.8 "Contrast Index Curves" (Small Tank, 20 C): T-MAX Developer line -> data/f4001_contrast_index_tmax.csv
                  (CI 0.71 at 12 min; 0.56 at the EI 400 time 8 1/2 min)
              p.3 "Small-Tank Processing": T-MAX Developer 68 F (20 C): EI 400 8 1/2, 800 9 1/2, 1600 10 1/2, 3200 12 min
              p.2 EXPOSURE: nominal speed EI 1000 in T-MAX Developer ("determined in a manner published in ISO standards");
                  at EI 3200 / 6400 "a slight increase in contrast and graininess with additional loss of shadow detail"
              p.7 IMAGE STRUCTURE: diffuse rms granularity 18 (D-76; net diffuse density 1.00, 48 um, 12x); footnote: blue
                  sensitivity "slightly less than that of other KODAK panchromatic black-and-white films"
  CROSS-CHECK Kodak F-4016 "KODAK PROFESSIONAL T-MAX Films", October 2007, T-MAX P3200 section: p.26 "Characteristic
              Curves", T-MAX Developer, Small Tank, 20 C, 8 / 9 / 11 / 13 min (13 min traced -> data/f4016_2007_p3200_tmax13.csv,
              used only to document the axis offset of the 2018 sheet); p.25 spectral sensitivity (same shape, about
              1.0 log S higher); p.20 T-MAX Developer EI 3200 small tank 12 min (same time as 2018).
  MEASURED  HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013 (copied from Press 400's data):
              p.2 filter 2 characteristic curve -> data/mgfb_classic_char_filter2.csv ; p.1 ISO range filter 2 = R95
  MEASURED  CIE 1931 2-degree colour-matching functions (CIE 018:2019 Table 6, DOI 10.25039/CIE.DS.xvudnb9b)
  MEASURED  CIE daylight components S0, S1, S2 (CIE 015:2018 model) ; D55 = CCT 5503 K
  MEASURED  ColorChecker Classic spectral reflectances, Ohta (1997), colour-science (BSD-3), only for the J-BW check
  STANDARD  ISO 6:1993 (speed point and development criterion, S = 0.8 / H_m), ISO 2720 (K = 12.5), ISO 6846:1983
              (paper ISO range; annex A stray light 5-10 %).
  GUESS     Neutral JPEG = sRGB-encoded scene light; camera WB diagonal in linear sRGB; ev_place 0 (the rating EI 3200 =
            the camera ISO); the anchor rule above (stated speed 1000, interpolation in time between the 7 and 10 min
            fits); the 0.3 curve as this film's spectral response; sensitivity 0 beyond 698 nm; the fitted toe below the
            first digitised point (log H -3.10 on the sheet's axis: the fit decays to base+fog); stray light 7 %; print
            exposure rule (metered grey at the midpoint of the paper ISO range); scan levels 7 / 251; paper Dmin 0.020
            and paper toe (Press 400's guesses); all of model step 7 (engineering).
Claims (J7, inequalities through the shipped kernel; tested by tools/review_nt3200.py -> sheets/claims_nt3200.json):
  C1 shadows block up (push data: p.8 12 min curve + p.2 stated speed): out(grey 118 - 2 EV) is at least 10 codes darker
     than Press 400's and below 40; the shadow separation out(-1 EV) - out(-3 EV) is smaller than Press 400's.
  C2 contrasty mids (p.8 CI 0.71 at the EI 3200 time vs the 0.56 that F-4017 p.2 gives Press 400's negative): mid-grey
     OKLab-L slope (grey 108..128) higher than Press 400's (1.30) by >= 0.15.
  C3 spectral (p.7 0.3 curve flat at log S 0.97..1.15 from 500 to 635 nm; blue "slightly less" than other pan films):
     red renders at least 0.03 OKLab L lighter, and blue at least 0.03 darker, relative to a luminance monochrome, than
     on Press 400.
  C4 (grain, for the effects row, not the table) rms 18 at D-76 (p.7) and more graininess at EI 3200 (p.2): coarser than
     the B&W 400 row (rms 17) -> new grain row "B&W 3200".

Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1, R = G = B on every line.
films/NT3200.T17 = NT3200.CUB through the module's own loader (/root/ml/yuvre_work/harness "lut"), then mono_t17.py (the
Press 400 post-step, byte-identical copy: raw U/V bytes forced to 0x00 only where the loader left +-1; asserts).

usage: python3 gen_nt3200.py [--data DIR] [--set JSON] <outdir>
       writes NT3200.CUB, checks_nt3200.json, provenance_nt3200.json, bw_model_nt3200.json, preview_nt3200_curves.png
"""
import sys, os, json, argparse
import numpy as np

N = 33
HERE = os.path.dirname(os.path.abspath(__file__))

# ------------------------------------------------------------------ colour maths (as gen_port160.py / gen_prs400.py)
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
        except ValueError: return np.nan
    return hdr, np.array([[num(x) for x in r] for r in rows[1:]]), com

class Sources:
    HD_COLS = {12: "D_12min", 10: "D_10min", 7: "D_7min", 6: "D_6min"}

    def __init__(self, ddir):
        self.dir = ddir
        _, self.cmf, _ = read_csv(os.path.join(ddir, "cie1931_2deg_10nm.csv"))
        _, self.s012, _ = read_csv(os.path.join(ddir, "cie_daylight_S012_10nm.csv"))
        self.cc_names, self.cc, _ = read_csv(os.path.join(ddir, "colorchecker_ohta1997_10nm.csv"))
        self.cc_names = self.cc_names[1:]
        self.sens_hdr, self.sens, _ = read_csv(os.path.join(ddir, "f4001_spectral_sensitivity.csv"))
        self.hd_hdr, self.hd, _ = read_csv(os.path.join(ddir, "f4001_tmax_dev_char.csv"))
        _, self.pp, _ = read_csv(os.path.join(ddir, "mgfb_classic_char_filter2.csv"))
        _, self.hd07, _ = read_csv(os.path.join(ddir, "f4016_2007_p3200_tmax13.csv"))
        _, self.ci, _ = read_csv(os.path.join(ddir, "f4001_contrast_index_tmax.csv"))
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

    def sensitivity(self, curve="D0.3"):
        """film spectral sensitivity on the 10 nm grid (linear, energy basis, relative); 0 beyond the last drawn point"""
        col = self.sens_hdr.index("logS_" + curve)
        x, y = self.sens[:, 0], self.sens[:, col]
        ok = ~np.isnan(y); x, y = x[ok], y[ok]
        S = 10 ** np.interp(self.wl, x, y)
        S[self.wl > x.max()] = 0.0                              # GUESS: no response beyond the last drawn point
        S[self.wl < x.min()] = 0.0                              # the curves are drawn from 400 nm (the fit starts there)
        return S

    def hd_fit(self, t, hi):
        """smooth fit of the digitised T-MAX Developer curve for t minutes, points with log H <= hi (sheet axis):
        D(x) = f + g sp(x) + c sp(x)^2, sp(x) = s ln(1 + exp((x - x0) / s)) (toe -> straight line, slight bend), as
        gen_prs400.py; f, g, c by linear least squares, s and x0 by grid search; returns (params, rms residual)"""
        key = ("fit", t, hi)
        if key in self.__dict__: return self.__dict__[key]
        col = self.hd_hdr.index(self.HD_COLS[t])
        x, y = self.hd[:, 0], self.hd[:, col]
        ok = ~np.isnan(y) & (x <= hi + 1e-9); x, y = x[ok], y[ok]
        best = None
        for s_ in np.arange(0.05, 0.805, 0.01):
            for x0 in np.arange(-3.60, -1.795, 0.01):
                sp = s_ * np.logaddexp(0.0, (x - x0) / s_)
                A = np.stack([np.ones_like(x), sp, sp * sp], -1)
                p, *_ = np.linalg.lstsq(A, y, rcond=None)
                e = float(((A @ p - y) ** 2).sum())
                if best is None or e < best[0]: best = (e, s_, x0, p)
        e, s_, x0, p = best
        q = dict(f=round(float(p[0]), 5), g=round(float(p[1]), 5), c=round(float(p[2]), 5), s=round(float(s_), 2), x0=round(float(x0), 2))
        r = (q, float(np.sqrt(e / len(x))), float(np.abs(self.hd_eval(q, x) - y).max()), int(len(x)))
        self.__dict__[key] = r
        return r

    @staticmethod
    def hd_eval(q, x):
        sp = q["s"] * np.logaddexp(0.0, (np.asarray(x, float) - q["x0"]) / q["s"])
        return q["f"] + q["g"] * sp + q["c"] * sp * sp

    def hd_curve(self, minutes, hi):
        """the T-MAX Developer curve at `minutes` (sheet axis): linear in time between the smooth fits of the bracketing
        digitised curves (6, 7, 10, 12 min; 12 min is used as it is); returns (function log H -> D, info)"""
        ts = [6, 7, 10, 12]
        t0 = max([t for t in ts if t <= minutes]); t1 = min([t for t in ts if t >= minutes])
        q0, r0, m0, _ = self.hd_fit(t0, hi); q1, r1, m1, _ = self.hd_fit(t1, hi)
        a = 0.0 if t1 == t0 else (minutes - t0) / (t1 - t0)
        f = lambda x: (1 - a) * self.hd_eval(q0, x) + a * self.hd_eval(q1, x)
        return f, dict(t0=t0, t1=t1, a=a, fit0=q0, rms0=round(r0, 4), max0=round(m0, 4), fit1=q1, rms1=round(r1, 4),
                       max1=round(m1, 4), fog=(1 - a) * q0["f"] + a * q1["f"])

    def iso_anchor(self, hi, iso_stated, dD=0.80, dlogH=1.30, dm=0.10):
        """ISO 6 speed criterion on the 6 / 7 / 10 / 12 min fits (sheet axis): speed point m at base+fog + dm, point n at
        log H_m + dlogH; the development with D_n - D_m = dD is found by linear interpolation in time and the family is
        shifted (true log H = sheet log H + shift) so that it has S = 0.8 / H_m = iso_stated."""
        xx = np.linspace(-6.0, 0.3, 6301)
        rows = []
        for t in (6, 7, 10, 12):
            q = self.hd_fit(t, hi)[0]
            d = self.hd_eval(q, xx)
            xm = float(np.interp(q["f"] + dm, d, xx)); dn = float(np.interp(xm + dlogH, xx, d)) - (q["f"] + dm)
            rows.append((t, q["f"], xm, dn))
        ts = np.array([r[0] for r in rows], float); dDs = np.array([r[3] for r in rows]); xms = np.array([r[2] for r in rows])
        assert np.all(np.diff(dDs) > 0), "development criterion must grow with time"
        t_iso = float(np.interp(dD, dDs, ts)); x_iso = float(np.interp(t_iso, ts, xms))
        shift = float(np.log10(0.8 / iso_stated) - x_iso)
        info = dict(per_curve=[dict(t=t, fog=round(f, 3), logHm_sheet=round(xm, 3), dD_1p30=round(dn, 3), S_sheet_axis=round(0.8 / 10 ** xm))
                               for t, f, xm, dn in rows],
                    t_iso_min=round(t_iso, 2), logHm_iso_sheet=round(x_iso, 3), S_sheet_axis_iso=round(0.8 / 10 ** x_iso),
                    iso_stated=iso_stated, shift_logH=round(shift, 3))
        return shift, info

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
        """smooth fit of the digitised grade-2 (filter 2) paper curve (net density vs log E), exactly as gen_prs400.py:
        a monotone penalised cubic B-spline (knots every 0.15 log E, second-difference penalty lam)."""
        if "_pfit" in self.__dict__: return self._pfit
        x, y = self.pp[:, 0], self.pp[:, 1] - self.PAPER_DMIN
        h = 0.15
        knots = np.arange(-3 * h, 2.5 + 4 * h, h)
        B = self._bspl(x, knots)
        n = B.shape[1]
        Dm2 = np.diff(np.eye(n), 2, axis=0); Dm1 = np.diff(np.eye(n), 1, axis=0)
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

def contrast_index(f, fog, lo=-6.0, hi=0.5):
    """Kodak contrast index of a curve D = f(log H): the slope of the line through a centre on the base+fog line and the
    curve points at distances 0.2 and 2.2 from that centre, with the centre chosen so the three are collinear."""
    xx = np.linspace(lo, hi, 13001); dd = f(xx)
    def pt(xc, r):
        dist = np.where(xx > xc, np.hypot(xx - xc, dd - fog), 0.0)   # the curve to the right of the centre
        i = int(np.argmax(dist >= r))
        return xx[i], dd[i]
    def mis(xc):
        a = pt(xc, 0.2); b = pt(xc, 2.2)
        return np.arctan2(a[1] - fog, a[0] - xc) - np.arctan2(b[1] - fog, b[0] - xc), a, b
    cs = np.linspace(lo + 0.5, hi - 2.3, 1500)
    ms = np.array([mis(c)[0] for c in cs])
    k = int(np.argmin(np.abs(ms)))
    _, a, b = mis(cs[k])
    return float((b[1] - a[1]) / (b[0] - a[0]))

# ------------------------------------------------------------------ the film
LOOKS = {
 "NT3200": dict(
    title="NT3200 - Night 3200 (WindyStrife original LUT)",
    note="T-grain multi-speed B&W rated and push-developed at EI 3200, printed on a medium paper: shadows block up, contrasty mids.",
    # spectral filter (MEASURED curve; the net density 0.3 curve, docstring step 2)
    curve="D0.3", fit_lo=400, fit_hi=700, alpha=1e-3, cct=5500 * 1.4388 / 1.4380,
    # exposure (ISO 6 / ISO 2720 relation); the rating = the camera ISO (GUESS ev_place 0)
    iso=3200.0, q=0.65, K=12.5, grey=0.18, ev_place=0.0,
    # negative: the sheet's EI 3200 time in T-MAX Developer (MEASURED curve), fitted up to log H -0.45 (sheet axis);
    # its position from the stated ISO-method speed (docstring step 4)
    dev_min=12.0, hd_fit_hi=-0.45, iso_stated=1000.0,
    # print (as Press 400; ISO 6846)
    stray=0.07, shadow_dD=0.10, print_anchor="grey", hi_F=1.0, grey_print_D=0.95,
    # scan levels GUESS (as Press 400)
    black_code=7.0, white_code=251.0,
    # kernel fit (model step 7; ENGINEERING, tuned against judge v2 J1/J4/J8/J-BW; see README for the rejected settings)
    e_knee=60.0, e_limit=120.0, flat_gamma=None, y_ref="mid", slope_ref=0.8, slope_h=16.0, neg_limit=40.0, mono_closure=True,
    core_c0=None, core_c1=None, e_scale=None, fade_lo=None, fade_hi=None),
}
ORDER = ["NT3200"]
DEFAULT = "NT3200"

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

class Chain:
    """the bw_spectral family module for one preset (gen_prs400.py's, plus the speed anchor of step 4)"""
    def __init__(self, P, src):
        self.P, self.src = P, src
        self.w, self.fit_resid, self.fit_s, self.fit_model = fit_weights(src, P["curve"], P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
        self.logH_grey = np.log10(P["q"] * P["K"] / P["iso"]) + P["ev_place"] * np.log10(2.0)
        self.shift, self.anchor_info = src.iso_anchor(P["hd_fit_hi"], P["iso_stated"])
        self._neg, self.hd_info = src.hd_curve(P["dev_min"], P["hd_fit_hi"])
        self.fog = float(self.hd_info["fog"])
        xx = np.linspace(-6, 0.5, 2000); dd = self.neg(xx)
        assert np.all(np.diff(dd) > 0), "negative curve must rise"
        # speed point of the used curve (density fog + 0.10), true axis, and the metered grey's distance from it
        self.logH_speed = float(np.interp(self.fog + 0.10, dd, xx))
        self.pq, self.paper_rms = src.paper_fit()
        self.E_T = float(src.paper_inv(self.pq, 0.04)); self.E_S = float(src.paper_inv(self.pq, 0.90 * self.pq["Dm"]))
        self.LER = self.E_S - self.E_T
        self.D_sh = float(self.d_eff(self.fog + P["shadow_dD"]))
        self.D_hi = float(self.d_eff(self.neg(self.logH(np.array(P["hi_F"])))))
        self.neg_range = self.D_hi - self.D_sh
        self.D_g = float(self.d_eff(self.neg(self.logH(np.array(P["grey"])))))
        if P["print_anchor"] == "mid":
            self.c = 0.5 * (self.E_T + self.E_S) + self.D_g
        elif P["print_anchor"] == "highlight":
            self.c = self.E_T + self.D_hi
        else:
            self.c = float(src.paper_inv(self.pq, P["grey_print_D"])) + self.D_g
        self.R_k = float(self.reflectance(np.array(0.0))); self.R_w = float(self.reflectance(np.array(1.0)))
        self.b0 = float(srgb_to_lin(P["black_code"] / 255.0)); self.w0 = float(srgb_to_lin(P["white_code"] / 255.0))

    def logH(self, F):
        return self.logH_grey + np.log10(np.maximum(F, 1e-12) / self.P["grey"])

    def neg(self, lh):
        """density at TRUE log H (the sheet's curve read at sheet log H = true log H - shift)"""
        return self._neg(np.asarray(lh, float) - self.shift)

    def d_eff(self, D):
        return self.fog + (1 - self.P["stray"]) * (np.asarray(D, float) - self.fog)

    def paper_x(self, Deff):
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
        _CH[k] = Chain(P, src or Sources(os.environ.get("NT3200_DATA", os.path.join(HERE, "data"))))
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
    """rgb (...,3) display sRGB 0..1 -> (...,3) display sRGB 0..1, R = G = B exactly (kernel fit of model step 7,
    the gen_prs400.py stages unchanged)."""
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
        h = P.get("slope_h", 1.0) / 255.0                      # slope window half-width (codes); 1 = local slope
        Gd = lambda yy: (Gf(np.clip(yy + h, 0, 1)) - Gf(np.clip(yy - h, 0, 1))) / (np.clip(yy + h, 0, 1) - np.clip(yy - h, 0, 1))
        Gp = Gd(y)
        if P["slope_ref"] == "mid":
            # first-order (Taylor) form: the spectral shift is an exposure offset, so its luma effect scales with the local
            # grey slope; E (taken at the mid-gamut luma yc of the chroma) is scaled by G'(y) / G'(yc), capped at 1.  This
            # removes the flat E from the blocked toe and the paper shoulder, where the film itself has no contrast.
            r = np.maximum(Gp / np.maximum(Gd(yc), 1e-3), 1e-6)
        else:
            r = np.maximum(Gp / P["slope_ref"], 1e-6)
        p = P.get("slope_p", 4.0)
        E = E * (r ** -p + 1.0) ** (-1.0 / p)                 # smooth min(1, r)
    if P.get("s_hi"):
        # luma ramp (smoothstep on the camera luma y601, codes s_lo..s_hi): the toe proxy used instead of / with the slope
        # scaling. Its luma derivative is bounded (1.5 |E| / (s_hi - s_lo) codes per code), so the kernel's nearest-of-65
        # luma levels and its 4:2:2 pair-mean luma see a slowly varying E (judge J8), while E still fades out in the
        # blocked toe, where a 1-code chroma rounding would otherwise outweigh the flat grey curve (judge J4).
        t = np.clip((y * 255.0 - P["s_lo"]) / (P["s_hi"] - P["s_lo"]), 0, 1)
        E = E * t * t * (3 - 2 * t)
    if P.get("core_c0") is not None:
        u = rgb @ np.array([-0.168736, -0.331264, 0.5]); v = rgb @ np.array([0.5, -0.418688, -0.081312])
        t = np.clip((np.hypot(u, v) * 255.0 - P["core_c0"]) / (P["core_c1"] - P["core_c0"]), 0, 1)
        E = E * t * t * (3 - 2 * t)
    if P.get("neg_limit") is not None:
        nl = P["neg_limit"]
        E = np.where(E >= 0, E, -soft_limit(-E, 0.5 * nl, nl) if nl > 0 else 0.0)
    if P.get("neg_fade_hi"):
        # negative side only, faded out toward black by the grey output G (smoothstep neg_fade_lo..neg_fade_hi codes):
        # a colour rendered darker than its luma cannot go deeper than the blocked toe (the film has no contrast there),
        # and the kernel adds E from the 4:2:2 pair-mean luma to each pixel's own N, so a negative E must not reach black
        t = np.clip((G * 255.0 - P["neg_fade_lo"]) / (P["neg_fade_hi"] - P["neg_fade_lo"]), 0, 1)
        E = np.where(E < 0, E * t * t * (3 - 2 * t), E)
    if P.get("e_scale") is not None:
        E = E * P["e_scale"]
    if P.get("fade_hi"):
        t = np.clip((G * 255.0 - P["fade_lo"]) / (P["fade_hi"] - P["fade_lo"]), 0, 1)
        E = E * t * t * (3 - 2 * t)
    if P.get("clip_levels"):
        # the scan maps the print's darkest tone (F = 0) to black_code and its lightest (F = 1, paper white) to white_code,
        # and no in-gamut colour has F outside 0..1 (w >= 0, sum w = 1), so the model's outputs never leave that range
        v = np.clip(G + E / 255.0, P["black_code"] / 255.0, P["white_code"] / 255.0)
    else:
        v = np.clip(G + E / 255.0, 0, 1)
    return np.repeat(v[..., None], 3, -1)

# ------------------------------------------------------------------ cube io + checks
def build_cube(P, n=N):
    x = np.linspace(0, 1, n)
    B, G, R = np.meshgrid(x, x, x, indexing="ij")          # [b][g][r] -> red fastest when flattened
    out = apply_look(np.stack([R, G, B], -1), P)
    if P.get("mono_closure"):
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
        f.write("# original creative LUT, parametric spectral B&W model (gen_nt3200.py); input: the camera Neutral JPEG, sRGB\n")
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
    D65 = ch.src.daylight(6500 * 1.4388 / 1.4380)
    xyz = (ch.src.cc[:, 1:].T * D65) @ ch.src.cmf[:, 1:] / (D65 @ ch.src.cmf[:, 2])
    cc_srgb = np.round(lin_to_srgb(np.clip(xyz @ XYZ_TO_SRGB.T, 0, 1)) * 255).astype(int)
    w10, r10, _, _ = fit_weights(ch.src, "D1.0", P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
    w0a, _, _, _ = fit_weights(ch.src, P["curve"], P["fit_lo"], P["fit_hi"], 0.0, P["cct"])
    w2a, _, _, _ = fit_weights(ch.src, P["curve"], P["fit_lo"], P["fit_hi"], 1e-2, P["cct"])
    # the push placement: net negative density (over base+fog) of grey +- EV, and the speed point's EV
    evs = [-4, -3, -2, -1.5, -1, 0, 1, 2, round(float(np.log2(1 / P["grey"])), 2)]
    net = {("%+g" % e): round(float(ch.neg(ch.logH_grey + e * np.log10(2)) - ch.fog), 3) for e in evs}
    ev_speed = (ch.logH_speed - ch.logH_grey) / np.log10(2)
    # contrast index of the used curve vs the sheet's CI chart; the 2007 edition's 13 min curve for the axis cross-check
    ci_fit = contrast_index(ch._neg, ch.fog)
    ci_tab = ch.src.ci
    ci_chart_12 = float(np.polyval(np.polyfit(ci_tab[:, 0], ci_tab[:, 1], 1), 12.0))
    h7 = ch.src.hd07; ok = ~np.isnan(h7[:, 1])
    fog07 = float(np.nanmin(h7[ok, 1])); x07 = float(np.interp(fog07 + 0.10, h7[ok, 1], h7[ok, 0]))
    q12 = ch.src.hd_fit(12, P["hd_fit_hi"])[0]
    xx = np.linspace(-6, 0.3, 6301); x18 = float(np.interp(q12["f"] + 0.10, ch.src.hd_eval(q12, xx), xx))
    return dict(look=name, neutral=bool(np.all(out[..., 0] == out[..., 1]) and np.all(out[..., 1] == out[..., 2])),
                luma_violations_rgb_axes=viol, grey_monotonic=bool(np.all(np.diff(luma(grey)) > 0)),
                black_out=round(float(out[0, 0, 0, 0]) * 255, 1), white_out=round(float(out[-1, -1, -1, 0]) * 255, 1),
                grey_ramp_in_out={str(int(round(a * 255))): round(float(b), 1) for a, b in zip(g, ramp)},
                max_abs_d2=round(d2, 4), p999_abs_d2=round(d2p, 4),
                spectral_curve=P["curve"], spectral_weights_rgb=np.round(ch.w, 4).tolist(), fit_rel_residual=round(ch.fit_resid, 3),
                weights_alpha0=np.round(w0a, 4).tolist(), weights_alpha_1em2=np.round(w2a, 4).tolist(),
                weights_from_D10_curve=np.round(w10, 4).tolist(), fit_rel_residual_D10=round(r10, 3),
                speed_anchor=ch.anchor_info,
                axis_crosscheck=dict(speed_point_2018_12min_sheet=round(x18, 3), speed_point_2007_13min_sheet=round(x07, 3),
                                     fog_2007_13min=round(fog07, 3), offset_2007_minus_2018=round(x07 - x18, 3),
                                     anchor_shift=round(ch.shift, 3)),
                logH_grey=round(float(ch.logH_grey), 3), logH_speed_point=round(ch.logH_speed, 3),
                grey_above_speed_point_logH=round(float(ch.logH_grey - ch.logH_speed), 3),
                speed_point_EV_below_grey=round(float(-ev_speed), 2),
                neg_net_density_by_EV=net, neg_fog=round(ch.fog, 3), neg_fit=ch.hd_info,
                contrast_index_fit_12min=round(ci_fit, 3), contrast_index_chart_12min=round(ci_chart_12, 3),
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
    X, Y = panel(70, 560, 480, (0, 255), (0, 255), "NT3200 grey ramp: display in -> out (codes)", "input code (Neutral JPEG grey)", "output code")
    g = np.linspace(0, 1, 256)
    line(X, Y, [0, 255], [0, 255], (170, 170, 170), 1)
    line(X, Y, g * 255, apply_look(np.stack([g, g, g], -1), P)[:, 0] * 255, (30, 30, 30), 3)
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    X, Y = panel(640, 560, 480, (400, 700), (0, 0.1), "film S(lambda, net D 0.3) x D55 (black) vs fit w.cbar x D55 (red), unit area", "wavelength nm", "relative")
    line(X, Y, ch.src.wl[m], ch.fit_s, (0, 0, 0), 3); line(X, Y, ch.src.wl[m], ch.fit_model, (200, 40, 40), 2)
    Kn = ch.src.cbar[m] * ch.src.daylight(P["cct"])[m][:, None]; Kn = Kn / Kn.sum(0)
    line(X, Y, ch.src.wl[m], np.array(REC709_Y) @ Kn.T, (60, 120, 200), 1)
    s10 = ch.src.sensitivity("D1.0")[m] * ch.src.daylight(P["cct"])[m]; line(X, Y, ch.src.wl[m], s10 / s10.sum(), (150, 150, 150), 1)
    d.text((660, 110), "w = R %.3f  G %.3f  B %.3f   (blue thin: Rec.709 luminance; grey thin: net D 1.0 curve)" % tuple(ch.w), fill=(0, 0, 0))
    X, Y = panel(70, 1160, 480, (-4.5, 0.5), (0, 3), "negative: F-4001 p.8 T-MAX Dev 12 min (dots, shifted %+.2f), fit (black); true log H" % ch.shift, "log H (lux s), anchored", "density")
    xx = np.linspace(-4.5, 0.4 + ch.shift, 300)
    for t, col in ((6, (190, 190, 190)), (7, (170, 170, 170)), (10, (140, 140, 140))):
        q = ch.src.hd_fit(t, P["hd_fit_hi"])[0]; line(X, Y, xx, ch.src.hd_eval(q, xx - ch.shift), col, 1)
    c12 = ch.src.hd_hdr.index("D_12min")
    for a, b in ch.src.hd[:, [0, c12]]:
        if np.isfinite(b): d.ellipse([X(a + ch.shift) - 2, Y(b) - 2, X(a + ch.shift) + 2, Y(b) + 2], fill=(90, 90, 90))
    line(X, Y, xx, ch.neg(xx), (0, 0, 0), 3)
    for x, col in ((ch.logH_speed, (40, 140, 40)), (ch.logH_grey, (200, 40, 40)), (float(ch.logH(np.array(1.0))), (40, 40, 220))):
        d.line([X(x), 1160, X(x), 680], fill=col, width=2)
    d.text((80, 1190), "green: speed point (fog + 0.10, %.2f), red: metered grey at EI 3200 (%.2f), blue: code 255; grey: 6/7/10 min" % (ch.logH_speed, ch.logH_grey), fill=(0, 0, 0))
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
    ap = argparse.ArgumentParser(description="Night 3200 B&W look: writes <outdir>/NT3200.CUB")
    ap.add_argument("--data", default=os.path.join(HERE, "data"), help="source csv directory")
    ap.add_argument("--set", default=None, help="JSON overrides of the preset parameters (experiments only)")
    ap.add_argument("--no-png", action="store_true")
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.environ["NT3200_DATA"] = a.data
    os.makedirs(a.outdir, exist_ok=True)
    P = LOOKS[DEFAULT]
    if a.set:
        P = dict(P); P.update(json.loads(a.set)); LOOKS[DEFAULT] = P
    out = build_cube(P)
    write_cube(os.path.join(a.outdir, "NT3200.CUB"), P, out)
    c = checks(DEFAULT, P, out)
    print(json.dumps(c))
    with open(os.path.join(a.outdir, "checks_nt3200.json"), "w") as f:
        json.dump(c, f, indent=1)
    ch = chain(P)
    prov = dict(code="NT3200", id=46, type="bw", generator="gen_nt3200.py", family="bw_spectral (copied from gen_prs400.py)",
                params={k: v for k, v in P.items()},
                sources=["Kodak Alaris F-4001 KODAK PROFESSIONAL T-MAX P3200 Black & White Negative Film, March 2018 (Revised 03-18): "
                         "p.7 Spectral Sensitivity Curves (0.3 and 1.0 greater than D-min); p.8 Characteristic Curves T-MAX Developer small tank "
                         "20 C 6/7/10/12 min; p.8 Contrast Index Curves small tank (T-MAX line); p.3 small-tank T-MAX Developer EI 3200 12 min; "
                         "p.2 nominal speed EI 1000 in T-MAX Developer (ISO method), EI 3200 more contrast and graininess, loss of shadow detail; "
                         "p.7 diffuse rms granularity 18",
                         "Kodak F-4016 KODAK PROFESSIONAL T-MAX Films, October 2007, T-MAX P3200 section: p.26 Characteristic Curves T-MAX Developer "
                         "small tank 8/9/11/13 min (13 min traced, axis cross-check only); p.25 spectral sensitivity; p.20 EI 3200 12 min",
                         "HARMAN technology ILFORD MULTIGRADE FB CLASSIC technical information, Oct 2013: p.2 filter 2 curve; p.1 ISO range R95 (data copied from Press 400)",
                         "CIE 018:2019 Table 6 CIE 1931 2-deg CMFs (DOI 10.25039/CIE.DS.xvudnb9b)",
                         "CIE daylight components S0 S1 S2 (CIE 015:2018 model)",
                         "ColorChecker Classic spectral reflectances, Ohta 1997 (colour-science), J-BW only",
                         "ISO 6:1993 speed point and development criterion; ISO 2720 K = 12.5; ISO 6846:1983 annexes A and B"],
                guesses=["Neutral JPEG = sRGB-encoded scene light", "camera WB diagonal in linear sRGB", "ev_place 0 (rating EI 3200 = camera ISO)",
                         "speed anchor: the plotted log H axis replaced by the stated ISO speed 1000 (ISO 6 criterion interpolated in time between the 7 and 10 min fits)",
                         "the net density 0.3 spectral curve as the film's response at EI 3200", "sensitivity 0 beyond 698 nm",
                         "fitted negative toe below the first digitised point", "stray light 7 %", "print exposure: metered grey at the midpoint of the paper ISO range",
                         "scan levels black 7 / white 251", "paper Dmin 0.020 and paper toe (Press 400's guesses)",
                         "kernel fit (engineering, model step 7): see params e_* / flat_gamma / slope_ref / neg_limit / mono_closure"],
                derived=dict(weights=np.round(ch.w, 5).tolist(), logH_grey=round(float(ch.logH_grey), 4), shift=round(ch.shift, 4),
                             logH_speed=round(ch.logH_speed, 4), LER=round(ch.LER, 4), print_c=round(float(ch.c), 4),
                             neg_effective_range=round(ch.neg_range, 4)))
    with open(os.path.join(a.outdir, "provenance_nt3200.json"), "w") as f:
        json.dump(prov, f, indent=1)
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    s = ch.src.sensitivity(P["curve"])[m]
    with open(os.path.join(a.outdir, "bw_model_nt3200.json"), "w") as f:
        json.dump(dict(note="NT3200 spectral model: F-4001 (03/2018) p.7 net D 0.3 sensitivity (energy basis, relative), fit range 400-700 nm; weights = its D55 fit on linear sRGB",
                       sensitivity=dict(nm=ch.src.wl[m].tolist(), s=np.round(s / s.max(), 6).tolist()),
                       weights=np.round(ch.w, 6).tolist()), f, indent=1)
    if not a.no_png:
        curves_png(os.path.join(a.outdir, "preview_nt3200_curves.png"))
