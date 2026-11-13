#!/usr/bin/env python3
"""Street 400 (file code STR400, catalogue id 41, type 2 = B&W), a WindyStrife own film: a softer cubic-grain
panchromatic black-and-white negative of the ISO 400 class with a long gentle toe and a long straight line,
developed to the data sheet's own EI 400 process and printed on a measured paper of the grade that matches the
negative's density range, as a scanned print.  Long smooth mid-tones, open shadows, no shoulder.  The table is
luma only: every output colour is an exact neutral (R = G = B in the .cube; U = V = 0 in the T17).

FAMILY: this is the bw_spectral family module of gen_prs400.py (Press 400, batch 1), COPIED (PLAN 6.2; the
catalogue rule "all B&W films use ONE bw_spectral pipeline").  Unchanged from gen_prs400.py: the colour maths,
the spectral fit (fit_weights), the exposure equation, the stray-light print model, the ISO 6846 paper points,
the print-exposure rule, the scan levels, the whole kernel fit (apply_look, soft_limit, flat E, slope scaling,
negative limit, monotone closure), the cube writer, the checks and the J-BW model output.  Changed (source data
only): Sources.sensitivity (a 2850 K wedge spectrogram with the source divided out, instead of a log S curve),
Sources.hd_* (one published curve on a RELATIVE log exposure axis, placed by the ISO 6 speed point, instead of
four absolute curves interpolated in time) and the paper filter (chosen by the ISO 6846 annex A match, see 5).

Model (PLAN 6.2; input = the camera's Neutral JPEG, display sRGB):
  1. sRGB -> linear (sRGB EOTF; GUESS: the Neutral picture style is treated as sRGB-encoded scene light).
  2. Spectral filter: film exposure F = w . (R, G, B)_lin.  The data sheet gives a wedge spectrogram "to tungsten
     light (2850K)", i.e. the film's response to a 2850 K source, plotted on a linear 0..1 "Sensitivity" axis.
     The source is divided out first (PLAN 5.2): S(lambda) = response(lambda) / B(lambda, 2850 K), B = Planck's
     law (c2 = 1.4388e-2 m K), S = 0 outside the drawn curve (351..659 nm).  GUESS: the plotted height is a
     linear response and only the source is to be removed (no spectrograph / wedge correction is published).
     Robustness: reading the height as a logarithmic wedge contour with a wedge range of 1..2 log units instead
     gives w_R 0.146..0.163 (linear reading 0.139; the B&W sibling Press 400 0.215), so the conclusion "less red,
     more blue than Press 400" does not depend on this guess (checks: weights_log_reading_*).
     w is the regularised least-squares fit of S(lambda) x D55 by the linear-sRGB colour-matching functions x D55
     (both unit area, sum w = 1, ridge alpha 1e-3 toward Rec.709, fit range 400..700 nm), exactly as Press 400.
     Camera model: the camera white-balances D55 grey to R = G = B (diagonal scaling in linear sRGB).
  3. Exposure: log10 H = log10(q K / S) + log10(F / 0.18) + ev_place log10(2), S = 400, K = 12.5, q = 0.65; the
     JPEG mid-grey (code 118, linear 0.18) is the metered grey, as Press 400.
  4. Negative: the data sheet's only characteristic curve, ILFOTEC HC 1+31, 6 1/2 min, 20 C (p.5), which is the
     process the sheet's own table gives for EI 400 (p.3).  Smooth fit of the digitised points (toe -> straight
     line, same functional form as Press 400).  The curve is on a RELATIVE log exposure axis; it is placed on the
     absolute axis by the ISO 6 speed point: the point at base+fog + 0.10 gets log H_m = log10(0.8 / 400).
     Check (reported): ISO 6 asks for dD 0.80 over 1.30 log H from that point; this curve gives about 0.77, so
     the published process is at ISO development contrast and the speed point rule applies (GUESS: the sheet says
     its EI "is based on a practical evaluation of film speed and is not based on foot speed"; the July 2004
     edition states ISO 400 measured in ID-11; we place the curve at box speed by the ISO rule).
  5. Print: identical model to Press 400 (stray light 7 %, paper log E = c - D_eff, metered grey at the midpoint
     of the paper's ISO range).  The paper is the same published variable-contrast fibre paper (ILFORD MULTIGRADE
     FB CLASSIC, Oct 2013, p.2); its filter is chosen by the ISO 6846 annex A rule that Press 400 reports as its
     check ("match the paper's LER to the negative's effective density range; for medium contrast papers an
     exact match generally works best"): the negative's effective range from its speed point to the JPEG white
     is compared with the LERs of the sheet's filters (ISO range table p.1: 00 R170, 0 R140, 1 R110, 2 R95,
     3 R80) and the nearest is used (checks: paper_match).  Press 400's negative matched filter 2.
  6. Scan: levels from the print's darkest / lightest tones to output codes 7 / 251 (GUESS, as Press 400).
  7. Kernel fit (ENGINEERING, not film data): as Press 400 (see gen_prs400.py docstring step 7), parameters in
     the preset.
  The OKLab hue / chroma stages of gen_port160.py / gen_chungking.py are identity for a B&W film.

Sources (MEASURED = digitised or copied from a public publication; GUESS = a call made here).  The digitised
points are in data/*.csv, each with its publication / date / page / figure header.
  MEASURED  HARMAN technology "ILFORD PHOTO TECHNICAL INFORMATION: HP5 PLUS", Nov 2018 (6 pages):
              p.1 "SPECTRAL SENSITIVITY - Wedge spectrogram to tungsten light (2850K)"
                  -> data/hp5plus_spectrogram_2850K.csv
              p.5 "CHARACTERISTIC CURVE", ILFOTEC HC (1+31) stock, 6 1/2 min, 20 C, intermittent agitation
                  -> data/hp5plus_char_hc131.csv
              p.3 development table: ILFOTEC HC 1+31, EI 400/27: 6 1/2 min; "These times will produce negatives
                  of average contrast suitable for printing in all enlargers"
              p.1 exposure rating: EI 400 "based on a practical evaluation of film speed and is not based on
                  foot speed"
  MEASURED  ILFORD "HP5 Plus" FACT SHEET, July 2004 (95042.GB.www), p.1: "ISO 400/27 ... to daylight", the ISO
              speed "measured using ILFORD ID-11 developer"; same spectrogram and curve as the 2018 edition
  MEASURED  HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013 (MGFB_CLASSIC_1013):
              p.2 "Characteristic curves" (glossy, MULTIGRADE developer 1+9, 2 min 20 C), filter curves;
              p.1 ISO range table (ISO 6846).  The filter 2 curve (data/mgfb_classic_char_filter2.csv) is the
              file digitised for Press 400 (copied unchanged, see its header).
  MEASURED  CIE 1931 2-degree CMFs (CIE 018:2019 Table 6, DOI 10.25039/CIE.DS.xvudnb9b); CIE daylight S0/S1/S2
              (CIE 015:2018 model); ColorChecker Classic reflectances (Ohta 1997, via colour-science, BSD-3), all
              copied from the Press 400 source pack.
  STANDARD  Planck's law, c2 = 1.4388e-2 m K (ITS-90); ISO 6846:1983 (paper ISO range, annex A); ISO 6:1993
              (speed point H_m = 0.8 / S, contrast criterion dD 0.80 over 1.30 log H); ISO 2720 (K = 12.5).
  GUESS     Neutral JPEG = sRGB-encoded scene light; camera WB = diagonal in linear sRGB; ev_place 0 (box speed);
            the spectrogram height is a linear response and only the 2850 K source is divided out; response 0
            outside the drawn curve; the relative log E axis is placed by the ISO 6 speed point at ISO 400;
            stray light 7 %; print exposure rule (metered grey at the midpoint of the paper's ISO range); scan
            levels 7 / 251; the negative fit's toe below relative log E 0.5 is the base+fog plateau (drawn);
            the straight line continues beyond the last drawn point (relative log E 4.11, D 2.15, no shoulder
            drawn); paper Dmin 0.020 and the filter-2 toe below log E 0.30 (as Press 400).
Claims (J7, inequalities against the sibling Press 400; tested through the shipped kernel by
  tools/review_str400.py -> sheets/claims_str400.json):
  C1 reds render darker than Press 400 and the red/blue balance is shifted to blue: after dividing out the
     2850 K source the sensitivity at 630 nm is 0.60 of that at 550 nm (Press 400's sheet: 0.85) and at 450 nm
     2.5x (Press 400: 1.5x).  Test: ColorChecker "red" L(STR400) <= L(PRS400) - 0.03 and the fitted red weight
     at most 0.75x Press 400's.
  C2 softer tone with a long toe and no shoulder (see the claims file for the tested numbers; the tone claim is
     stated against the measured curves of both films, not against a target).
  C3 (grain, for the effects row, not the table): the 2018 sheet publishes no granularity; grain row B&W 400 x 1.0
     is the catalogue assignment for the cubic-grain 400 class (PLAN 5.2), not a measured value.

Output: .cube, LUT_3D_SIZE 33, red index fastest, values 0..1, R = G = B on every line.
STR400.T17 = STR400.CUB through the module's own loader (/root/ml/yuvre_work/harness "lut"), then mono_t17.py
(the Press 400 post-step, copied: raw U/V bytes forced to 0x00 only where the loader left +-1; asserts).

usage: python3 gen_str400.py [--data DIR] [--set JSON] <outdir>
       writes STR400.CUB, checks_str400.json, provenance_str400.json, bw_model_str400.json, preview_str400_curves.png
"""
import sys, os, json, argparse
import numpy as np

N = 33
HERE = os.path.dirname(os.path.abspath(__file__))
CODE = "STR400"

# ------------------------------------------------------------------ colour maths (as gen_prs400.py / gen_port160.py)
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
C2_PLANCK = 1.4388e7                                           # nm K (ITS-90 second radiation constant)

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

PAPERS = {"filter2": "mgfb_classic_char_filter2.csv"}
# ISO range table of the paper sheet (p.1): filter -> R (LER = R / 100); used for the ISO 6846 annex A match report
PAPER_ISO_R = {"filter00": 170, "filter0": 140, "filter1": 110, "filter2": 95, "filter3": 80}

class Sources:
    SPEC_LO, SPEC_HI = 351.1, 659.1                            # where the drawn spectrogram meets zero (csv header)

    def __init__(self, ddir, paper="filter2", paper_k=1.0):
        self.dir = ddir
        _, self.cmf, _ = read_csv(os.path.join(ddir, "cie1931_2deg_10nm.csv"))
        _, self.s012, _ = read_csv(os.path.join(ddir, "cie_daylight_S012_10nm.csv"))
        self.cc_names, self.cc, _ = read_csv(os.path.join(ddir, "colorchecker_ohta1997_10nm.csv"))
        self.cc_names = self.cc_names[1:]
        _, self.spec, _ = read_csv(os.path.join(ddir, "hp5plus_spectrogram_2850K.csv"))
        _, self.hd, _ = read_csv(os.path.join(ddir, "hp5plus_char_hc131.csv"))
        self.paper = paper
        _, self.pp, _ = read_csv(os.path.join(ddir, PAPERS[paper]))
        # LOOK CALL (grade): the grade-2 curve stretched along log E by paper_k = LER(target filter)/LER(filter 2), the LER of the
        # target grade from the paper sheet p.1 ISO range table (softer grade = wider range, same Dmax); GUESS: curve shape kept
        self.pp = self.pp.copy(); self.pp[:, 0] = self.pp[:, 0] * paper_k
        self.wl = self.cmf[:, 0]
        assert np.all(self.s012[:, 0] == self.wl) and np.all(self.cc[:, 0] == self.wl)
        self.cbar = self.cmf[:, 1:] @ XYZ_TO_SRGB.T

    def daylight(self, cct):
        """CIE 015 daylight SPD at correlated colour temperature cct (4000..7000 K branch)"""
        T = cct
        xd = -4.6070e9 / T ** 3 + 2.9678e6 / T ** 2 + 0.09911e3 / T + 0.244063
        yd = -3.0 * xd ** 2 + 2.870 * xd - 0.275
        m = 0.0241 + 0.2562 * xd - 0.7341 * yd
        m1 = (-1.3515 - 1.7703 * xd + 5.9114 * yd) / m
        m2 = (0.0300 - 31.4424 * xd + 30.0717 * yd) / m
        return self.s012[:, 1] + m1 * self.s012[:, 2] + m2 * self.s012[:, 3]

    def planck(self, T):
        wl = self.wl
        return wl ** -5.0 / np.expm1(C2_PLANCK / (wl * T))

    def response(self):
        """the digitised spectrogram (response to the 2850 K source) on the 10 nm grid, 0 outside the drawn curve"""
        r = np.interp(self.wl, self.spec[:, 0], self.spec[:, 1], left=0.0, right=0.0)
        r[(self.wl < self.SPEC_LO) | (self.wl > self.SPEC_HI)] = 0.0
        return np.clip(r, 0.0, None)

    def sensitivity(self, curve="lin", src_T=2850.0, wedge=None):
        """film spectral sensitivity on the 10 nm grid (relative, energy basis, max 1): the spectrogram with the
        2850 K source divided out.  curve "lin" (used): the plotted height is a linear response; "log" (robustness
        check only): the height is a wedge contour, response = 10^(wedge (h - 1))"""
        r = self.response()
        if curve == "log":
            r = np.where(r > 0.02, 10 ** (wedge * (r - 1.0)), 0.0)
        S = r / self.planck(src_T)
        return S / S.max()

    def hd_fit(self):
        """smooth fit of the digitised curve (relative log E): D(x) = f + g sp(x) + c sp(x)^2,
        sp(x) = s ln(1 + exp((x - x0) / s)) (the Press 400 form: base+fog -> toe -> straight line, slight bend);
        f, g, c by linear least squares, s and x0 by grid search; returns (params, rms residual)"""
        if "_hdfit" in self.__dict__: return self._hdfit
        x, y = self.hd[:, 0], self.hd[:, 1]
        ok = ~np.isnan(y); x, y = x[ok], y[ok]
        best = None
        for s_ in np.arange(0.10, 1.205, 0.01):
            for x0 in np.arange(0.20, 2.205, 0.01):
                sp = s_ * np.logaddexp(0.0, (x - x0) / s_)
                A = np.stack([np.ones_like(x), sp, sp * sp], -1)
                p, *_ = np.linalg.lstsq(A, y, rcond=None)
                e = float(((A @ p - y) ** 2).sum())
                if best is None or e < best[0]: best = (e, s_, x0, p)
        e, s_, x0, p = best
        self._hdfit = (dict(f=round(float(p[0]), 5), g=round(float(p[1]), 5), c=round(float(p[2]), 5),
                            s=round(float(s_), 2), x0=round(float(x0), 2)), float(np.sqrt(e / len(x))))
        return self._hdfit

    @staticmethod
    def hd_eval(q, x):
        sp = q["s"] * np.logaddexp(0.0, (np.asarray(x, float) - q["x0"]) / q["s"])
        return q["f"] + q["g"] * sp + q["c"] * sp * sp

    PAPER_DMIN = 0.020                                          # the plotted curves start at D 0.02 (csv header)

    @staticmethod
    def _bspl(x, knots):
        x = np.asarray(x, float)
        B = ((x[:, None] >= knots[None, :-1]) & (x[:, None] < knots[None, 1:])).astype(float)
        for k in range(1, 4):
            a = (x[:, None] - knots[None, :-k - 1]) / (knots[k:-1] - knots[:-k - 1])[None, :]
            b = (knots[None, k + 1:] - x[:, None]) / (knots[k + 1:] - knots[1:-k])[None, :]
            B = a * B[:, :-1] + b * B[:, 1:]
        return B

    def paper_fit(self, lam=0.02):
        """monotone P-spline fit of the digitised paper curve (as gen_prs400.py)"""
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
        return np.interp(Dn, q["dg"], q["grid"])

# ------------------------------------------------------------------ the film
LOOKS = {
 "STR400": dict(
    title="STR400 - Street 400 (WindyStrife original LUT)",
    note="Softer cubic-grain panchromatic B&W 400: long gentle toe, long straight line, less red and more blue than Press 400.",
    # spectral filter: the 2850 K spectrogram with the source divided out (MEASURED curve, settings in the docstring)
    curve="lin", src_T=2850.0, fit_lo=400, fit_hi=700, alpha=1e-3, cct=5500 * 1.4388 / 1.4380,
    # exposure (ISO 6 / ISO 2720 relation) and placement (GUESS 0 = box speed)
    iso=400.0, q=0.65, K=12.5, grey=0.18, ev_place=0.0,
    # print (ISO 6846; stray light within annex A's 5-10 %), paper filter by the annex A match (checks: paper_match)
    paper="filter2", paper_k=140.0 / 95.0, stray=0.07, shadow_dD=0.10, print_anchor="mid", hi_F=1.0, grey_print_D=0.70,
    # scan levels GUESS
    black_code=7.0, white_code=251.0,
    # kernel fit (model step 7, ENGINEERING; the Press 400 settings)
    e_knee=60.0, e_limit=120.0, flat_gamma=None, y_ref="mid", slope_ref=1.2, slope_h=16.0, neg_limit=40.0, mono_closure=True,
    core_c0=None, core_c1=None, e_scale=None, fade_lo=None, fade_hi=None),
}
ORDER = ["STR400"]
DEFAULT = "STR400"

def fit_weights(src, S, lo, hi, alpha, cct):
    """min |s - K w|^2 + alpha |w - w709|^2 s.t. sum w = 1; s, K = S x D55 and cbar x D55, unit area (gen_prs400.py)"""
    m = (src.wl >= lo) & (src.wl <= hi)
    E = src.daylight(cct)
    s = S[m] * E[m]; s = s / s.sum()
    Km = src.cbar[m] * E[m][:, None]; Km = Km / Km.sum(0)
    A = np.zeros((4, 4)); A[:3, :3] = Km.T @ Km + alpha * np.eye(3); A[:3, 3] = 1; A[3, :3] = 1
    w = np.linalg.solve(A, np.r_[Km.T @ s + alpha * REC709_Y, 1.0])[:3]
    resid = float(np.sqrt(((s - Km @ w) ** 2).sum() / (s ** 2).sum()))
    return w, resid, s, Km @ w

class Chain:
    """the bw_spectral family module for one preset (gen_prs400.py Chain; negative placement on a relative axis)"""
    def __init__(self, P, src):
        self.P, self.src = P, src
        self.S = src.sensitivity(P["curve"], P["src_T"])
        self.w, self.fit_resid, self.fit_s, self.fit_model = fit_weights(src, self.S, P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
        self.logH_grey = np.log10(P["q"] * P["K"] / P["iso"]) + P["ev_place"] * np.log10(2.0)
        # negative: the published curve on its relative axis, placed by the ISO 6 speed point (base+fog + 0.10 at 0.8 / S)
        self.hq, self.hd_rms = src.hd_fit()
        self.fog = float(self.hq["f"])
        xr = np.linspace(-3.0, 6.0, 9001); dr = src.hd_eval(self.hq, xr)
        assert np.all(np.diff(dr) > 0), "negative curve must rise"
        self.x_speed_rel = float(np.interp(self.fog + 0.10, dr, xr))
        self.logH_speed = float(np.log10(0.8 / P["iso"]))
        self.x_off = self.logH_speed - self.x_speed_rel        # absolute log H = relative + x_off
        self.iso6_dD = float(src.hd_eval(self.hq, self.x_speed_rel + 1.30) - (self.fog + 0.10))
        self.gbar = float((src.hd_eval(self.hq, self.x_speed_rel + 1.50) - (self.fog + 0.10)) / 1.50)
        self.hd_info = dict(fit=self.hq, rms=round(self.hd_rms, 4), fog=round(self.fog, 4), x_speed_rel=round(self.x_speed_rel, 3),
                            x_off=round(self.x_off, 4), iso6_dD_over_1p30=round(self.iso6_dD, 3), gbar_1p5=round(self.gbar, 3))
        # paper (ISO 6846 points T (net 0.04) and S (0.90 net Dmax)), as gen_prs400.py
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
        return self.src.hd_eval(self.hq, np.asarray(lh, float) - self.x_off)

    def d_eff(self, D):
        return self.fog + (1 - self.P["stray"]) * (np.asarray(D, float) - self.fog)

    def paper_x(self, Deff):
        return (self.c - Deff - self.E_T) / self.LER

    def print_net_density(self, F):
        return self.src.paper_eval(self.pq, self.c - self.d_eff(self.neg(self.logH(F))))

    def reflectance(self, F):
        return 10 ** (-self.print_net_density(F))

    def tone_F(self, F):
        R = self.reflectance(F)
        return self.b0 + (self.w0 - self.b0) * (R - self.R_k) / (self.R_w - self.R_k)

    def spectral_F(self, refl):
        m = (self.src.wl >= self.P["fit_lo"]) & (self.src.wl <= self.P["fit_hi"])
        E = self.src.daylight(self.P["cct"])
        s = self.S[m] * E[m]
        return np.asarray(refl)[..., m] @ s / s.sum()

    def camera_rgb(self, refl):
        m = (self.src.wl >= self.P["fit_lo"]) & (self.src.wl <= self.P["fit_hi"])
        E = self.src.daylight(self.P["cct"])
        K = self.src.cbar[m] * E[m][:, None]
        return np.asarray(refl)[..., m] @ K / K.sum(0)

_CH = {}
def chain(P, src=None):
    k = id(P)
    if k not in _CH:
        _CH[k] = Chain(P, src or Sources(os.environ.get("STR400_DATA", os.path.join(HERE, "data")), P.get("paper", "filter2"), P.get("paper_k", 1.0)))
    return _CH[k]

YUV_Y = np.array([0.299, 0.587, 0.114])

def soft_limit(e, knee, lim):
    a = np.abs(e); s = lim - knee
    return np.sign(e) * np.where(a <= knee, a, knee + s * np.tanh((a - knee) / s))

def apply_look_raw(rgb, P):
    ch = chain(P)
    return np.clip(lin_to_srgb(ch.tone_F(srgb_to_lin(rgb) @ ch.w)), 0, 1)

def apply_look(rgb, P):
    """display sRGB -> display sRGB, R = G = B exactly (the gen_prs400.py kernel fit, unchanged)"""
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
        Gp = (Gf(np.clip(y + h, 0, 1)) - Gf(np.clip(y - h, 0, 1))) / (np.clip(y + h, 0, 1) - np.clip(y - h, 0, 1))
        r = np.maximum(Gp / P["slope_ref"], 1e-6)
        E = E * (r ** -4 + 1.0) ** -0.25
    if P.get("core_c0") is not None:
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
    if P.get("rs_lo") is not None:
        # range scale (STR400 addition, off in gen_prs400.py): E x min(1, (G - black) / rs_lo) x min(1, (white - G) / rs_hi)
        # (smooth min).  The kernel looks E up at the nearest of 65 luma levels (4-code steps), so a luma-dependent scale
        # s(y) makes E jump by 4 s' E at each level; a scale linear in the grey OUTPUT keeps that jump below the grey
        # curve's own rise when rs >= 4 |E| (gen_prs400.py's slope scale s = G'/0.8 jumps with G'', large in this toe)
        sm = lambda a: (np.maximum(a, 1e-6) ** -4 + 1.0) ** -0.25          # smooth min(1, a)
        s = sm(np.maximum(G * 255.0 - P["black_code"], 1e-6) / P["rs_lo"])
        if P.get("rs_hi"):
            s = s * sm(np.maximum(P["white_code"] - G * 255.0, 1e-6) / P["rs_hi"])
        E = E * s
    if P.get("hr_neg") is not None:
        # headroom limit (STR400 addition, off in gen_prs400.py): a colour may be rendered darker than its camera-luma
        # grey by at most hr_neg x (G - black) codes (soft min), so along an exposure ramp out = G + E keeps a slope of
        # at least (1 - hr_neg) G' where the limit is active: no luma reversal from the spectral shift in the toe
        room = np.maximum(G * 255.0 - P["black_code"], 0.0) * P["hr_neg"]
        En = np.maximum(-E, 0.0)
        En = En * room / np.power(np.power(En, 4) + np.power(room, 4) + 1e-12, 0.25)   # smooth min(En, room)
        E = np.where(E < 0, -En, E)
    if P.get("hr_pos") is not None:
        room = np.maximum(P["white_code"] - G * 255.0, 0.0) * P["hr_pos"] + P.get("hr_pos_add", 0.0)
        Ep = np.maximum(E, 0.0)
        Ep = Ep * room / np.power(np.power(Ep, 4) + np.power(room, 4) + 1e-12, 0.25)
        E = np.where(E > 0, Ep, E)
    v = np.clip(G + E / 255.0, 0, 1)
    return np.repeat(v[..., None], 3, -1)

# ------------------------------------------------------------------ cube io + checks
def build_cube(P, n=N):
    x = np.linspace(0, 1, n)
    B, G, R = np.meshgrid(x, x, x, indexing="ij")
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
        f.write("# original creative LUT, parametric spectral B&W model (gen_str400.py); input: the camera Neutral JPEG, sRGB\n")
        f.write("LUT_3D_SIZE %d\n" % n)
        for r, g, b in out.reshape(-1, 3):
            f.write("%.6f %.6f %.6f\n" % (r, g, b))

def luma(rgb):
    lin = srgb_to_lin(rgb)
    return lin_to_srgb(0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2])

def jbw_generator(P):
    ch = chain(P); cc = ch.src.cc[:, 1:].T
    Fsp = ch.spectral_F(cc); rgb = ch.camera_rgb(cc)
    Lsp = np.cbrt(ch.tone_F(Fsp))
    Lmd = np.cbrt(ch.tone_F(np.clip(np.clip(rgb, 0, 1) @ ch.w, 0, None)))
    d = Lmd - Lsp
    return dict(mean_abs_dL=round(float(np.abs(d).mean()), 4), max_abs_dL=round(float(np.abs(d).max()), 4),
                per_patch={n: round(float(v), 4) for n, v in zip(ch.src.cc_names, d)})

def paper_match(ch):
    """ISO 6846 annex A: the negative's effective range (speed point -> JPEG white) vs the LER of each filter (p.1 table)"""
    r = ch.neg_range
    d = {k: round(abs(v / 100.0 - r), 3) for k, v in PAPER_ISO_R.items()}
    return dict(neg_effective_range=round(r, 3), filter_LER={k: v / 100.0 for k, v in PAPER_ISO_R.items()},
                abs_diff=d, nearest=min(d, key=d.get), used=ch.src.paper)

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
    w0a, _, _, _ = fit_weights(ch.src, ch.S, P["fit_lo"], P["fit_hi"], 0.0, P["cct"])
    w2a, _, _, _ = fit_weights(ch.src, ch.S, P["fit_lo"], P["fit_hi"], 1e-2, P["cct"])
    wlog = {}
    for wd in (1.0, 2.0, 3.0):
        wl_, _, _, _ = fit_weights(ch.src, ch.src.sensitivity("log", P["src_T"], wd), P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
        wlog["wedge_%.0f" % wd] = np.round(wl_, 4).tolist()
    wraw, _, _, _ = fit_weights(ch.src, ch.src.response(), P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
    wl = ch.src.wl; i550 = int(np.where(wl == 550)[0][0])
    return dict(look=name, neutral=bool(np.all(out[..., 0] == out[..., 1]) and np.all(out[..., 1] == out[..., 2])),
                luma_violations_rgb_axes=viol, grey_monotonic=bool(np.all(np.diff(luma(grey)) > 0)),
                black_out=round(float(out[0, 0, 0, 0]) * 255, 1), white_out=round(float(out[-1, -1, -1, 0]) * 255, 1),
                grey_ramp_in_out={str(int(round(a * 255))): round(float(b), 1) for a, b in zip(g, ramp)},
                max_abs_d2=round(d2, 4), p999_abs_d2=round(d2p, 4),
                spectral_weights_rgb=np.round(ch.w, 4).tolist(), fit_rel_residual=round(ch.fit_resid, 3),
                weights_alpha0=np.round(w0a, 4).tolist(), weights_alpha_1em2=np.round(w2a, 4).tolist(),
                weights_log_reading=wlog, weights_without_source_division=np.round(wraw, 4).tolist(),
                sensitivity_rel_550={str(int(w)): round(float(ch.S[np.where(wl == w)[0][0]] / ch.S[i550]), 3) for w in (420, 450, 500, 550, 600, 630, 650)},
                logH_grey=round(float(ch.logH_grey), 3), logH_speed_point=round(ch.logH_speed, 3),
                grey_above_speed_point=round(float(ch.logH_grey - ch.logH_speed), 3),
                neg_density_grey=round(float(ch.neg(ch.logH_grey)), 3), neg_density_white_F1=round(float(ch.neg(ch.logH(np.array(1.0)))), 3),
                neg_fog=round(ch.fog, 3), neg_fit=ch.hd_info,
                paper=ch.src.paper, paper_fit=dict(net_Dmax=round(ch.pq["Dm"], 3), logE_T=round(ch.E_T, 3), logE_S=round(ch.E_S, 3)), paper_fit_rms=round(ch.paper_rms, 4),
                paper_LER=round(ch.LER, 3), paper_match=paper_match(ch),
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
    X, Y = panel(70, 560, 480, (0, 255), (0, 255), "STR400 grey ramp: display in -> out (codes)", "input code (Neutral JPEG grey)", "output code")
    g = np.linspace(0, 1, 256)
    line(X, Y, [0, 255], [0, 255], (170, 170, 170), 1)
    line(X, Y, g * 255, apply_look(np.stack([g, g, g], -1), P)[:, 0] * 255, (30, 30, 30), 3)
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    X, Y = panel(640, 560, 480, (400, 700), (0, 0.1), "S = response/B(2850K), x D55 (black) vs fit w.cbar x D55 (red); grey: raw 2850K response", "wavelength nm", "relative (unit area)")
    line(X, Y, ch.src.wl[m], ch.fit_s, (0, 0, 0), 3); line(X, Y, ch.src.wl[m], ch.fit_model, (200, 40, 40), 2)
    r = ch.src.response()[m]; line(X, Y, ch.src.wl[m], r / r.sum(), (150, 150, 150), 1)
    Kn = ch.src.cbar[m] * ch.src.daylight(P["cct"])[m][:, None]; Kn = Kn / Kn.sum(0)
    line(X, Y, ch.src.wl[m], np.array(REC709_Y) @ Kn.T, (60, 120, 200), 1)
    d.text((660, 110), "w = R %.3f  G %.3f  B %.3f   (blue thin: Rec.709 luminance)" % tuple(ch.w), fill=(0, 0, 0))
    X, Y = panel(70, 1160, 480, (-4, 1), (0, 3), "negative: HP5 Plus sheet p.5 HC 1+31 6.5 min points (dots), fit (black), placed by ISO 6 speed point", "log H (lux s)", "density")
    xx = np.linspace(-4, 0.5, 300)
    line(X, Y, xx, ch.neg(xx), (0, 0, 0), 3)
    for a, b in ch.src.hd[:, :2]:
        if np.isfinite(b): d.ellipse([X(a + ch.x_off) - 2, Y(b) - 2, X(a + ch.x_off) + 2, Y(b) + 2], fill=(90, 90, 90))
    for x, col in ((ch.logH_speed, (40, 140, 40)), (ch.logH_grey, (200, 40, 40)), (float(ch.logH(np.array(1.0))), (40, 40, 220))):
        d.line([X(x), 1160, X(x), 680], fill=col, width=2)
    d.text((80, 1190), "green: speed point (base+fog + 0.10, log H %.2f), red: mid-grey 118 (metered, log H %.2f), blue: code 255" % (ch.logH_speed, ch.logH_grey), fill=(0, 0, 0))
    X, Y = panel(640, 1160, 480, (0, 2.5), (0, 2.2), "paper: MG FB Classic %s points (dots), fit (black); LER %.2f" % (ch.src.paper, ch.LER), "paper log E (relative)", "net density")
    xs = np.linspace(0, 2.5, 300); line(X, Y, xs, ch.src.paper_eval(ch.pq, xs), (0, 0, 0), 3)
    for a, b in ch.src.pp[:, :2]:
        d.ellipse([X(a) - 2, Y(b - ch.src.PAPER_DMIN) - 2, X(a) + 2, Y(b - ch.src.PAPER_DMIN) + 2], fill=(90, 90, 90))
    for F, col in ((0.0, (40, 120, 40)), (P["grey"], (200, 40, 40)), (1.0, (40, 40, 200))):
        x = float(ch.c - ch.d_eff(ch.neg(ch.logH(np.array(F))))); d.line([X(x), 1160, X(x), 680], fill=col, width=2)
    for x in (ch.E_T, ch.E_S): d.line([X(x), 1160, X(x), 1140], fill=(0, 0, 0), width=3)
    d.text((660, 1190), "green: F 0 (base+fog), red: grey, blue: F 1 (code 255); ticks: ISO 6846 T and S", fill=(0, 0, 0))
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Street 400 B&W look: writes <outdir>/STR400.CUB")
    ap.add_argument("--data", default=os.path.join(HERE, "data"), help="source csv directory")
    ap.add_argument("--set", default=None, help="JSON overrides of the preset parameters (experiments only)")
    ap.add_argument("--no-png", action="store_true")
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.environ["STR400_DATA"] = a.data
    os.makedirs(a.outdir, exist_ok=True)
    P = LOOKS[DEFAULT]
    if a.set:
        P = dict(P); P.update(json.loads(a.set)); LOOKS[DEFAULT] = P
    out = build_cube(P)
    write_cube(os.path.join(a.outdir, "STR400.CUB"), P, out)
    c = checks(DEFAULT, P, out)
    print(json.dumps(c))
    with open(os.path.join(a.outdir, "checks_str400.json"), "w") as f:
        json.dump(c, f, indent=1)
    ch = chain(P)
    prov = dict(code="STR400", id=41, type="bw", generator="gen_str400.py", family="bw_spectral (copied from gen_prs400.py)",
                params={k: v for k, v in P.items()},
                sources=["HARMAN technology ILFORD PHOTO TECHNICAL INFORMATION HP5 PLUS, Nov 2018: p.1 Wedge spectrogram to tungsten light (2850K); p.5 Characteristic curve, ILFOTEC HC 1+31 6 1/2 min 20 C; p.3 development table (HC 1+31 EI 400: 6 1/2 min); p.1 exposure rating",
                         "ILFORD HP5 Plus FACT SHEET, July 2004 (95042.GB.www): p.1 ISO 400/27 to daylight, measured in ID-11",
                         "HARMAN technology ILFORD MULTIGRADE FB CLASSIC technical information, Oct 2013: p.2 characteristic curves (paper filter %s); p.1 ISO range table" % ch.src.paper,
                         "CIE 018:2019 Table 6 CIE 1931 2-deg CMFs (DOI 10.25039/CIE.DS.xvudnb9b)",
                         "CIE daylight components S0 S1 S2 (CIE 015:2018 model)",
                         "ColorChecker Classic spectral reflectances, Ohta 1997 (colour-science), J-BW only",
                         "Planck's law c2 = 1.4388e-2 m K; ISO 6846:1983 annex A; ISO 6:1993 speed point and contrast criterion; ISO 2720 K = 12.5"],
                guesses=["Neutral JPEG = sRGB-encoded scene light", "camera WB diagonal in linear sRGB", "ev_place 0 (box speed)",
                         "spectrogram height = linear response; only the 2850 K source divided out; 0 outside 351..659 nm",
                         "relative log E axis placed by the ISO 6 speed point at ISO 400",
                         "negative straight line continues past the last drawn point (log E rel 4.11)",
                         "stray light 7 % (ISO 6846 annex A range 5-10 %)", "print exposure: metered grey at the midpoint of the paper ISO range",
                         "scan levels black 7 / white 251",
                         "paper Dmin 0.020 and paper toe below log E 0.30 (as Press 400)",
                         "kernel fit (engineering, model step 7): parameters in params"],
                derived=dict(weights=np.round(ch.w, 5).tolist(), logH_grey=round(float(ch.logH_grey), 4), LER=round(ch.LER, 4),
                             print_c=round(float(ch.c), 4), neg_effective_range=round(ch.neg_range, 4), x_off=round(ch.x_off, 4)))
    with open(os.path.join(a.outdir, "provenance_str400.json"), "w") as f:
        json.dump(prov, f, indent=1)
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    s = ch.S[m]
    with open(os.path.join(a.outdir, "bw_model_str400.json"), "w") as f:
        json.dump(dict(note="STR400 spectral model: HP5 Plus sheet (Nov 2018) p.1 2850 K wedge spectrogram divided by Planck 2850 K (energy basis, relative), fit range 400-700 nm; weights = its D55 fit on linear sRGB",
                       sensitivity=dict(nm=ch.src.wl[m].tolist(), s=np.round(s / s.max(), 6).tolist()),
                       weights=np.round(ch.w, 6).tolist()), f, indent=1)
    if not a.no_png:
        curves_png(os.path.join(a.outdir, "preview_str400_curves.png"))
