#!/usr/bin/env python3
"""Sharp 50 (file code SHP50, catalogue id 54, type 2 = B&W), a WindyStrife own film: a slow, high-contrast,
very fine grain panchromatic black-and-white negative of the ISO 50 class, developed to a high contrast index and
printed on a hard (grade 3) paper, as a scanned print.  Deep blacks, bright clean whites, steep mid-tones.
The table is luma only: every output colour is an exact neutral (R = G = B in the .cube; U = V = 0 in the T17).
Shown ISO 50, camera ISO 100 (the catalogue row): the camera meters at 100, the model meters at box speed.

FAMILY: the bw_spectral pipeline of gen_prs400.py (Press 400), copied (PLAN 6.2, "all B&W films use ONE
bw_spectral pipeline"): colour maths, the regularised spectral fit (fit_weights), the exposure equation, the
stray-light print model on the published grade-2 paper curve, the ISO 6846 paper points, the scan levels, the
kernel fit, the cube writer, the checks and the J-BW model output.  Two parts are PARAMETRIC here because the
film's own data sheet was not digitised in this clean room (no downloads in this batch):
  * the spectral sensitivity: a smooth curve (blue hump + green-red plateau + red cut-off) whose three features
    are set from the data sheet's published description (LOOK CALL values, listed in LOOKS and the README);
  * the negative: a toe + straight-line curve (softplus) with the published class contrast (LOOK CALL), placed
    on the absolute log H axis by the ISO 6 speed point at box speed.
  The paper grade is the published grade-2 curve stretched along log E by paper_k = LER(target grade) / LER(grade
  2) from the same paper sheet's ISO range table (as Street 400 and Crisp 100 do).

Model (input = the camera's Neutral JPEG, display sRGB):
  1. sRGB -> linear (GUESS: the Neutral picture style is treated as sRGB-encoded scene light).
  2. Spectral filter: film exposure F = w . (R, G, B)_lin; w = the family fit of S(lambda) x D55 by the linear-sRGB
     colour-matching functions x D55 (unit area, sum w = 1, ridge alpha 1e-3 toward Rec.709, 400..700 nm).
  3. Exposure: log10 H = log10(q K / S) + log10(F / 0.18) + ev_place log10(2), K 12.5, q 0.65, S = box speed.
  4. Negative: D(x) = fog + G s ln(1 + exp((x - x0) / s)) - shoulder, x0 set so that D(log10(0.8 / S)) = fog + 0.10.
  5. Print: stray light 7 %, paper log E = c - D_eff, the grade-2 paper curve stretched by paper_k, print exposure c
     by the anchor rule in LOOKS ("mid" = the metered grey at the midpoint of the paper's ISO range, the family rule;
     "grey" = the metered grey printed at net density grey_print_D; "highlight" = the JPEG white at T).
  6. Scan: levels from the print's darkest to lightest tone -> codes black_code / white_code, then sRGB-encoded.
  7. Kernel fit (ENGINEERING): the family table G(y601) + E' (E = spectral luma shift), with the v2.1 settings of the
     family (per-pixel-E kernel model of judge v2.1): soft limit 60/120 codes, no flat E, slope scaling below G' 0.8,
     negative side soft-limited to 40 codes, monotone closure along +R, +G, +B.

usage: python3 gen_shp50.py [--data DIR] [--set JSON] [--no-png] <outdir>
       writes SHP50.CUB, checks_shp50.json, provenance_shp50.json, bw_model_shp50.json, preview_shp50_curves.png
"""
import sys, os, json, argparse
import numpy as np

N = 33
HERE = os.path.dirname(os.path.abspath(__file__))
CODE = "SHP50"

# ------------------------------------------------------------------ colour maths (as gen_prs400.py)
def srgb_to_lin(v):
    v = np.asarray(v, np.float64)
    return np.where(v <= 0.04045, v / 12.92, ((np.maximum(v, 0) + 0.055) / 1.055) ** 2.4)

def lin_to_srgb(l):
    l = np.clip(np.asarray(l, np.float64), 0.0, None)
    return np.where(l <= 0.0031308, 12.92 * l, 1.055 * np.power(l, 1 / 2.4) - 0.055)

XYZ_TO_SRGB = np.array([[3.2404542, -1.5371385, -0.4985314],
                        [-0.9692660, 1.8760108, 0.0415560],
                        [0.0556434, -0.2040259, 1.0572252]])
REC709_Y = np.array([0.2126, 0.7152, 0.0722])
YUV_Y = np.array([0.299, 0.587, 0.114])                    # the camera's luma (BT.601 on the JPEG codes)

def read_csv(path):
    com, rows = [], []
    for ln in open(path, encoding="utf-8"):
        s = ln.strip()
        if not s: continue
        if s.startswith("#"): com.append(s[1:].strip()); continue
        rows.append(s.split(","))
    def num(x):
        try: return float(x)
        except ValueError: return np.nan
    return rows[0], np.array([[num(x) for x in r] for r in rows[1:]]), com

# ------------------------------------------------------------------ sources (public CIE data + the published paper curve)
class Sources:
    PAPER_DMIN = 0.020

    def __init__(self, ddir, paper_k=1.0):
        _, self.cmf, _ = read_csv(os.path.join(ddir, "cie1931_2deg_10nm.csv"))
        _, self.s012, _ = read_csv(os.path.join(ddir, "cie_daylight_S012_10nm.csv"))
        self.cc_names, self.cc, _ = read_csv(os.path.join(ddir, "colorchecker_ohta1997_10nm.csv"))
        self.cc_names = self.cc_names[1:]
        _, self.pp, _ = read_csv(os.path.join(ddir, "mgfb_classic_char_filter2.csv"))
        # LOOK CALL (grade): grade-2 curve stretched along log E by paper_k = LER(target)/LER(grade 2) (sheet p.1 ISO ranges)
        self.pp = self.pp.copy(); self.pp[:, 0] = self.pp[:, 0] * paper_k
        self.wl = self.cmf[:, 0]
        assert np.all(self.s012[:, 0] == self.wl) and np.all(self.cc[:, 0] == self.wl)
        self.cbar = self.cmf[:, 1:] @ XYZ_TO_SRGB.T

    def daylight(self, cct):
        T = cct
        xd = -4.6070e9 / T ** 3 + 2.9678e6 / T ** 2 + 0.09911e3 / T + 0.244063
        yd = -3.0 * xd ** 2 + 2.870 * xd - 0.275
        m = 0.0241 + 0.2562 * xd - 0.7341 * yd
        m1 = (-1.3515 - 1.7703 * xd + 5.9114 * yd) / m
        m2 = (0.0300 - 31.4424 * xd + 30.0717 * yd) / m
        return self.s012[:, 1] + m1 * self.s012[:, 2] + m2 * self.s012[:, 3]

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
        """monotone P-spline of the (stretched) grade-2 curve, as gen_prs400.py (knots scale with paper_k)"""
        if "_pfit" in self.__dict__: return self._pfit
        x, y = self.pp[:, 0], self.pp[:, 1] - self.PAPER_DMIN
        top = float(x.max()); h = 0.15 * top / 2.5
        knots = np.arange(-3 * h, top + 4 * h, h)
        B = self._bspl(x, knots); n = B.shape[1]
        Dm2 = np.diff(np.eye(n), 2, axis=0); Dm1 = np.diff(np.eye(n), 1, axis=0)
        v = np.zeros(n - 1)
        for _ in range(50):
            c = np.linalg.solve(B.T @ B + lam * Dm2.T @ Dm2 + 1e6 * Dm1.T @ np.diag(v) @ Dm1, B.T @ y)
            v_new = (np.diff(c) < 0).astype(float)
            if np.array_equal(v_new, v): break
            v = v_new
        grid = np.linspace(0, top, 2501); dg = self._bspl(grid, knots) @ c
        assert np.all(np.diff(dg) > -1e-9), "paper fit must not decrease"
        q = dict(Dm=float(dg[-1]), grid=grid, dg=np.maximum.accumulate(dg), top=top)
        self._pfit = (q, float(np.sqrt(((B @ c - y) ** 2).mean())))
        return self._pfit

    @staticmethod
    def paper_eval(q, x):
        return np.interp(np.clip(np.asarray(x, float), 0.0, q["top"]), q["grid"], q["dg"])

    @staticmethod
    def paper_inv(q, Dn):
        return np.interp(Dn, q["dg"], q["grid"])

# ------------------------------------------------------------------ the film (LOOK CALL values marked)
LOOKS = {
 "SHP50": dict(
    title="SHP50 - Sharp 50 (WindyStrife original LUT)",
    note="Slow high-contrast panchromatic B&W 50 on a hard paper: deep blacks, bright clean whites, steep mids.",
    # spectral sensitivity (energy basis), LOOK CALL shape from the sheet's description: blue hump (centre, width,
    # height), green-red plateau rising at sp_rise and cut off at sp_cut (logistic widths in nm)
    sp_blue=440.0, sp_bw=35.0, sp_bh=1.0, sp_rise=495.0, sp_rw=12.0, sp_ph=0.85, sp_cut=655.0, sp_cw=8.0,
    fit_lo=400, fit_hi=700, alpha=1e-3, cct=5500 * 1.4388 / 1.4380,
    iso=50.0, q=0.65, K=12.5, grey=0.18, ev_place=0.0,
    # negative (LOOK CALL class values): base+fog, straight-line gradient, toe softness s (log H), shoulder off
    fog=0.20, gamma=0.72, toe_s=0.14, sh_D=None, sh_s=0.3,
    # print: stray light, paper grade by paper_k (grade 3: R80 / R95), print exposure anchor
    stray=0.07, paper_k=80.0 / 95.0, shadow_dD=0.10, print_anchor="grey", hi_F=1.0, grey_print_D=0.70,
    black_code=7.0, white_code=251.0,
    # kernel fit, family v2.1 settings (see docstring step 7)
    e_knee=60.0, e_limit=120.0, slope_ref=0.8, slope_h=16.0, neg_limit=40.0, mono_closure=True),
}
DEFAULT = CODE

def sensitivity(P, wl):
    lg = lambda x, c, w: 1.0 / (1.0 + np.exp(-(x - c) / w))
    S = P["sp_bh"] * np.exp(-0.5 * ((wl - P["sp_blue"]) / P["sp_bw"]) ** 2) \
        + P["sp_ph"] * lg(wl, P["sp_rise"], P["sp_rw"]) * (1.0 - lg(wl, P["sp_cut"], P["sp_cw"]))
    return S / S.max()

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
    def __init__(self, P, src):
        self.P, self.src = P, src
        self.S = sensitivity(P, src.wl)
        self.w, self.fit_resid, self.fit_s, self.fit_model = fit_weights(src, self.S, P["fit_lo"], P["fit_hi"], P["alpha"], P["cct"])
        self.logH_grey = np.log10(P["q"] * P["K"] / P["iso"]) + P["ev_place"] * np.log10(2.0)
        self.fog = P["fog"]
        # speed point at box speed (ISO 6): D = fog + 0.10 at log10(0.8 / S)
        self.logH_speed = float(np.log10(0.8 / P["iso"]))
        sp = lambda x0: P["gamma"] * P["toe_s"] * np.log1p(np.exp((self.logH_speed - x0) / P["toe_s"]))
        lo, hi = -6.0, 2.0
        for _ in range(80):
            mid = 0.5 * (lo + hi)
            if sp(mid) > 0.10: lo = mid
            else: hi = mid
        self.x0 = 0.5 * (lo + hi)
        xx = np.linspace(-6, 1.0, 2000); dd = self.neg(xx)
        assert np.all(np.diff(dd) >= 0) and dd[-1] > dd[0], "negative curve must not fall"
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
        P = self.P; x = np.asarray(lh, float)
        D = self.fog + P["gamma"] * P["toe_s"] * np.logaddexp(0.0, (x - self.x0) / P["toe_s"])
        if P.get("sh_D"):
            # optional shoulder: the net density approaches sh_D softly (smooth min, softness sh_s in D)
            n = D - self.fog; s = P["sh_s"]
            D = self.fog + P["sh_D"] - s * np.logaddexp(0.0, (P["sh_D"] - n) / s)
        return D

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
        E = self.src.daylight(self.P["cct"]); s = self.S[m] * E[m]
        return np.asarray(refl)[..., m] @ s / s.sum()

    def camera_rgb(self, refl):
        m = (self.src.wl >= self.P["fit_lo"]) & (self.src.wl <= self.P["fit_hi"])
        E = self.src.daylight(self.P["cct"]); K = self.src.cbar[m] * E[m][:, None]
        return np.asarray(refl)[..., m] @ K / K.sum(0)

_CH = {}
def chain(P):
    k = id(P)
    if k not in _CH:
        _CH[k] = Chain(P, Sources(os.environ.get(CODE + "_DATA", os.path.join(HERE, "data")), P["paper_k"]))
    return _CH[k]

def soft_limit(e, knee, lim):
    a = np.abs(e); s = lim - knee
    return np.sign(e) * np.where(a <= knee, a, knee + s * np.tanh((a - knee) / s))

def apply_look_raw(rgb, P):
    ch = chain(P)
    return np.clip(lin_to_srgb(ch.tone_F(srgb_to_lin(rgb) @ ch.w)), 0, 1)

def apply_look(rgb, P):
    """family kernel fit (gen_prs400.py apply_look, v2.1 settings: flat E off): output = G(y601) + E', R = G = B"""
    ch = chain(P)
    rgb = np.asarray(rgb, np.float64)
    O = apply_look_raw(rgb, P)
    y = np.clip(rgb @ YUV_Y, 0, 1)
    Gf = lambda yy: np.clip(lin_to_srgb(ch.tone_F(srgb_to_lin(yy))), 0, 1)
    G = Gf(y)
    E = soft_limit((O - G) * 255.0, P["e_knee"], P["e_limit"])
    h = P.get("slope_h", 1.0) / 255.0                      # slope window half-width (codes); 1 = local slope
    Gp = (Gf(np.clip(y + h, 0, 1)) - Gf(np.clip(y - h, 0, 1))) / (np.clip(y + h, 0, 1) - np.clip(y - h, 0, 1))
    r = np.maximum(Gp / P["slope_ref"], 1e-6)
    E = E * (r ** -4 + 1.0) ** -0.25                          # smooth min(1, G' / slope_ref)
    nl = P["neg_limit"]
    E = np.where(E >= 0, E, -soft_limit(-E, 0.5 * nl, nl))
    v = np.clip(G + E / 255.0, P["black_code"] / 255.0, P["white_code"] / 255.0)
    return np.repeat(v[..., None], 3, -1)

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
        f.write("# original creative LUT, parametric spectral B&W model (gen_%s.py); input: the camera Neutral JPEG, sRGB\n" % CODE.lower())
        f.write("LUT_3D_SIZE %d\n" % n)
        for r, g, b in out.reshape(-1, 3):
            f.write("%.6f %.6f %.6f\n" % (r, g, b))

def luma(rgb):
    lin = srgb_to_lin(rgb)
    return lin_to_srgb(0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2])

def jbw_generator(P):
    ch = chain(P); cc = ch.src.cc[:, 1:].T
    Lsp = np.cbrt(ch.tone_F(ch.spectral_F(cc)))
    Lmd = np.cbrt(ch.tone_F(np.clip(np.clip(ch.camera_rgb(cc), 0, 1) @ ch.w, 0, None)))
    d = Lmd - Lsp
    return dict(mean_abs_dL=round(float(np.abs(d).mean()), 4), max_abs_dL=round(float(np.abs(d).max()), 4))

def checks(P, out):
    ch = chain(P); n = out.shape[0]
    Yo = luma(out)
    viol = {ax: int((np.diff(Yo, axis=ax) < -1e-3).sum()) for ax in (0, 1, 2)}
    grey = out[np.arange(n), np.arange(n), np.arange(n)]
    d2 = max(float(np.abs(np.diff(out, 2, axis=ax)).max()) for ax in (0, 1, 2))
    d2p = max(float(np.percentile(np.abs(np.diff(out, 2, axis=ax)), 99.9)) for ax in (0, 1, 2))
    g = np.array([0, 4, 8, 16, 32, 48, 64, 96, 118, 140, 160, 200, 230, 245, 255]) / 255.0
    ramp = apply_look(np.stack([g, g, g], -1), P)[:, 0] * 255
    return dict(look=CODE, neutral=bool(np.all(out[..., 0] == out[..., 1]) and np.all(out[..., 1] == out[..., 2])),
                luma_violations_rgb_axes=viol, grey_monotonic=bool(np.all(np.diff(luma(grey)) >= 0)),
                black_out=round(float(out[0, 0, 0, 0]) * 255, 1), white_out=round(float(out[-1, -1, -1, 0]) * 255, 1),
                grey_ramp_in_out={str(int(round(a * 255))): round(float(b), 1) for a, b in zip(g, ramp)},
                max_abs_d2=round(d2, 4), p999_abs_d2=round(d2p, 4),
                spectral_weights_rgb=np.round(ch.w, 4).tolist(), fit_rel_residual=round(ch.fit_resid, 3),
                logH_grey=round(float(ch.logH_grey), 3), logH_speed_point=round(ch.logH_speed, 3), neg_x0=round(ch.x0, 3),
                grey_above_speed_point=round(float(ch.logH_grey - ch.logH_speed), 3),
                neg_density_grey=round(float(ch.neg(ch.logH_grey)), 3), neg_density_white_F1=round(float(ch.neg(ch.logH(np.array(1.0)))), 3),
                paper_k=round(P["paper_k"], 4), paper_LER=round(ch.LER, 3), paper_net_Dmax=round(ch.pq["Dm"], 3),
                neg_effective_range_shadow_to_highlight=round(ch.neg_range, 3),
                paper_x_speed_point=round(float(ch.paper_x(ch.D_sh)), 3),
                paper_x_F1=round(float(ch.paper_x(ch.d_eff(ch.neg(ch.logH(np.array(1.0)))))), 3),
                print_net_density_grey=round(float(ch.print_net_density(np.array(P["grey"]))), 3),
                jbw_generator=jbw_generator(P))

def curves_png(path, P):
    from PIL import Image, ImageDraw
    ch = chain(P)
    im = Image.new("RGB", (1500, 560), (250, 250, 248)); d = ImageDraw.Draw(im)
    def panel(ox, xr, yr, title):
        oy, s = 520, 440
        d.rectangle([ox, oy - s, ox + s, oy], outline=(0, 0, 0)); d.text((ox, oy - s - 18), title, fill=(0, 0, 0))
        return (lambda x: ox + (np.asarray(x) - xr[0]) / (xr[1] - xr[0]) * s), (lambda y: oy - (np.asarray(y) - yr[0]) / (yr[1] - yr[0]) * s)
    def line(X, Y, xs, ys, col, w=2):
        d.line([(float(a), float(b)) for a, b in zip(X(xs), Y(ys))], fill=col, width=w)
    X, Y = panel(40, (0, 255), (0, 255), "%s grey ramp in -> out (codes)" % CODE)
    g = np.linspace(0, 1, 256); line(X, Y, [0, 255], [0, 255], (170, 170, 170), 1)
    line(X, Y, g * 255, apply_look(np.stack([g, g, g], -1), P)[:, 0] * 255, (30, 30, 30), 3)
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    X, Y = panel(530, (400, 700), (0, 0.1), "S x D55 (black, LOOK CALL shape) vs fit (red); w = %.3f %.3f %.3f" % tuple(ch.w))
    line(X, Y, ch.src.wl[m], ch.fit_s, (0, 0, 0), 3); line(X, Y, ch.src.wl[m], ch.fit_model, (200, 40, 40), 2)
    X, Y = panel(1020, (-4, 1), (0, 3), "negative D(log H) (parametric), grey (red), speed point (green)")
    xx = np.linspace(-4, 1, 300); line(X, Y, xx, ch.neg(xx), (0, 0, 0), 3)
    for x, col in ((ch.logH_speed, (40, 140, 40)), (ch.logH_grey, (200, 40, 40))):
        d.line([X(x), 520, X(x), 80], fill=col, width=2)
    im.save(path)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="%s B&W look: writes <outdir>/%s.CUB" % (CODE, CODE))
    ap.add_argument("--data", default=os.path.join(HERE, "data"))
    ap.add_argument("--set", default=None, help="JSON overrides of the preset parameters (experiments only)")
    ap.add_argument("--no-png", action="store_true")
    ap.add_argument("outdir")
    a = ap.parse_args()
    os.environ[CODE + "_DATA"] = a.data
    os.makedirs(a.outdir, exist_ok=True)
    P = LOOKS[DEFAULT]
    if a.set:
        P = dict(P); P.update(json.loads(a.set)); LOOKS[DEFAULT] = P
    out = build_cube(P)
    write_cube(os.path.join(a.outdir, CODE + ".CUB"), P, out)
    c = checks(P, out)
    print(json.dumps(c))
    lc = CODE.lower()
    json.dump(c, open(os.path.join(a.outdir, "checks_%s.json" % lc), "w"), indent=1)
    ch = chain(P)
    json.dump(dict(code=CODE, type="bw", generator="gen_%s.py" % lc, params=P,
                   derived=dict(weights=np.round(ch.w, 5).tolist(), x0=round(ch.x0, 4), LER=round(ch.LER, 4), print_c=round(float(ch.c), 4))),
              open(os.path.join(a.outdir, "provenance_%s.json" % lc), "w"), indent=1)
    m = (ch.src.wl >= P["fit_lo"]) & (ch.src.wl <= P["fit_hi"])
    json.dump(dict(note="%s spectral model: parametric LOOK CALL sensitivity (energy basis, relative), fit range 400-700 nm; weights = its D55 fit on linear sRGB" % CODE,
                   sensitivity=dict(nm=ch.src.wl[m].tolist(), s=np.round(ch.S[m], 6).tolist()), weights=np.round(ch.w, 6).tolist()),
              open(os.path.join(a.outdir, "bw_model_%s.json" % lc), "w"), indent=1)
    if not a.no_png:
        curves_png(os.path.join(a.outdir, "preview_%s_curves.png" % lc), P)
