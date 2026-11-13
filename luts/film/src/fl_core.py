#!/usr/bin/env python3
"""fl_core.py - film-emulation model for the Film Roll "Log + LUT" cubes (own implementation).

Chain per cube node (camera log code -> display code):
  1. camera log code -> scene linear in the camera gamut (published Sony / Panasonic / ARRI / Canon formulas)
     -> CIE XYZ (D65) through the gamut's normalized primary matrix
  2. negative exposure: 3x3 from Rec.709 linear (D65-adapted camera RGB) to the film's three layer
     exposures. Default (matrix="spectral"): each Rec.709 primary is given a smooth, positive
     sigmoid-quadratic reflectance spectrum whose XYZ under the film's reference illuminant
     (Bradford-adapted to D65) equals that primary; the film's spectral sensitivities integrate those
     spectra -> one column per primary, rows normalized so neutral is kept exactly. Option
     matrix="lsq": weighted least squares on a set of smooth reflectance spectra. Colours outside
     Rec.709 (wide-gamut log nodes the camera never produces) are first pulled in smoothly.
     Camera 18 % grey = log exposure 0 on the profile's density curves (box speed).
  3. negative densities: per-layer H&D curve of the profile (dye amounts over base), optional
     interlayer (DIR-like) term that leaves neutrals unchanged.
  4a. PRINT path: spectral negative density = base + sum(dye amount x dye spectral density); printer
     light (3400 K tungsten + Schott KG3) through it onto the print stock's spectral sensitivities
     -> printer-light offsets (timing) solved so camera grey prints neutral at the target luminance
     -> print H&D curves -> spectral print density -> transmittance under a projector illuminant
     -> XYZ -> Bradford from the print's clear-base white to D65 -> Rec.709 linear.
  4b. SCAN path (still negatives): dye amounts read by an ideal (mask-removing) scanner, inverted
     per channel around the grey patch with each layer's own mid-scale gamma and D-min black point
     (per-channel levels, so the grey scale is near neutral; toe / shoulder differences between the
     layers stay), mapped back through the inverse of the step-2 matrix (colorimetric scanner
     profile), then a soft neutral scanner tone curve (softplus in log2).
  5. gain, smooth gamut compression (no negative RGB), smooth max-RGB shoulder, sRGB encoding.

Film data: spektrafilm profiles (CC BY-SA 4.0, A. Volpato), read from their JSON files. CIE 1931 CMFs
and CIE daylight components: CIE datasets. Schott KG3 transmittance: as shipped in spektrafilm's data
folder (digitized from the Newport/Schott sheet). No spektrafilm code is used.
"""
import json
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")
# spektrafilm checkout (profile JSON files only; its code is GPLv3 and is never used): WF_SPEKTRAFILM = its root, default data/spektrafilm
SPK = os.path.join(os.environ.get("WF_SPEKTRAFILM", os.path.join(DATA, "spektrafilm")), "src", "spektrafilm", "data")
PROF = os.path.join(SPK, "profiles")

WL = np.arange(380.0, 781.0, 5.0)
CMF = np.loadtxt(os.path.join(DATA, "cie_xyz1931_5nm.csv"), delimiter=",", comments="#")[:, 1:]
_COMP = np.loadtxt(os.path.join(DATA, "cie_daylight_comp_5nm.csv"), delimiter=",", comments="#")
assert CMF.shape == (81, 3) and _COMP.shape == (81, 4) and np.allclose(_COMP[:, 0], WL)


# ---------------------------------------------------------------- illuminants
def daylight(cct_nominal):
    """CIE daylight illuminant (CIE 15 formula, S0/S1/S2 components); 5500 -> D55 etc."""
    T = cct_nominal * 1.4388 / 1.4380
    if T <= 7000:
        xd = -4.6070e9 / T ** 3 + 2.9678e6 / T ** 2 + 0.09911e3 / T + 0.244063
    else:
        xd = -2.0064e9 / T ** 3 + 1.9018e6 / T ** 2 + 0.24748e3 / T + 0.237040
    yd = -3.0 * xd * xd + 2.870 * xd - 0.275
    M = 0.0241 + 0.2562 * xd - 0.7341 * yd
    M1 = (-1.3515 - 1.7703 * xd + 5.9114 * yd) / M
    M2 = (0.0300 - 31.4424 * xd + 30.0717 * yd) / M
    s = _COMP[:, 1] + M1 * _COMP[:, 2] + M2 * _COMP[:, 3]
    return s / s.mean()


def blackbody(T):
    lam = WL * 1e-9
    s = 1.0 / (lam ** 5 * (np.exp(1.4388e-2 / (lam * T)) - 1.0))
    return s / s.mean()


def kg3():
    a = np.loadtxt(os.path.join(SPK, "filters", "heat_absorbing", "schott", "KG3.csv"), delimiter=",")
    a = a[np.argsort(a[:, 0])]
    return np.interp(WL, a[:, 0], a[:, 1])


def illuminant(tag):
    if tag.startswith("D"):
        return daylight(float(tag[1:]) * 100.0)
    if tag.startswith("BB"):
        return blackbody(float(tag[2:]))
    if tag == "TH-KG3":                         # printer lamp: tungsten halogen 3400 K + KG3 heat filter
        s = blackbody(3400.0) * kg3()
        return s / s.mean()
    raise ValueError(tag)


# ---------------------------------------------------------------- colour math
def xy2XYZ(x, y):
    return np.array([x / y, 1.0, (1.0 - x - y) / y])


def npm(prim, white=(0.3127, 0.3290)):
    P = np.array([xy2XYZ(*p) for p in prim]).T
    S = np.linalg.solve(P, xy2XYZ(*white))
    return P * S


REC709 = [(0.640, 0.330), (0.300, 0.600), (0.150, 0.060)]
GAMUT = {"slog3": [(0.766, 0.275), (0.225, 0.800), (0.089, -0.087)],          # S-Gamut3.Cine
         "vlog": [(0.730, 0.280), (0.165, 0.840), (0.100, -0.030)],           # V-Gamut
         "logc3": [(0.6840, 0.3130), (0.2210, 0.8480), (0.0861, -0.1020)],    # ARRI Wide Gamut 3
         "clog2": [(0.7400, 0.2700), (0.1700, 1.1400), (0.0800, -0.1000)],    # Canon Cinema Gamut
         "clog3": [(0.7400, 0.2700), (0.1700, 1.1400), (0.0800, -0.1000)],
         "srgb": REC709}                                                      # WindyFilm: the camera JPEG (display sRGB) as input
NPM709 = npm(REC709)
XYZ2709 = np.linalg.inv(NPM709)
D65 = xy2XYZ(0.3127, 0.3290)
BRAD = np.array([[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]])


def cat(src, dst):
    """Bradford chromatic adaptation matrix, XYZ white src -> XYZ white dst"""
    return np.linalg.inv(BRAD) @ np.diag((BRAD @ dst) / (BRAD @ src)) @ BRAD


def slog3_inv(c):
    c = c * 1023.0
    return np.where(c >= 171.2102946929, 10.0 ** ((c - 420.0) / 261.5) * 0.19 - 0.01,
                    (c - 95.0) * 0.01125 / (171.2102946929 - 95.0))


def vlog_inv(c):
    return np.where(c < 0.181, (c - 0.125) / 5.6, 10.0 ** ((c - 0.598206) / 0.241514) - 0.00873)


def logc3_inv(t):
    cut, a, b, c, d, e, f = 0.010591, 5.555556, 0.052272, 0.247190, 0.385537, 5.367655, 0.092809
    return np.where(t > e * cut + f, (10.0 ** ((t - d) / c) - b) / a, (t - f) / e)


def slog3_enc(x):
    x = np.asarray(x, float)
    return np.where(x >= 0.01125, (420.0 + np.log10((np.maximum(x, 0.01125) + 0.01) / 0.19) * 261.5) / 1023.0,
                    (x * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0)


def vlog_enc(x):
    x = np.asarray(x, float)
    return np.where(x < 0.01, 5.6 * x + 0.125, 0.241514 * np.log10(np.maximum(x, 0.01) + 0.00873) + 0.598206)


def logc3_enc(x):
    x = np.asarray(x, float)
    cut, a, b, c, d, e, f = 0.010591, 5.555556, 0.052272, 0.247190, 0.385537, 5.367655, 0.092809
    return np.where(x > cut, c * np.log10(a * np.maximum(x, cut) + b) + d, e * x + f)


# Canon Log 2 / Canon Log 3, v1.2 (Canon input transform 202007; = colour-science log_encoding_CanonLog2/3 with its
# defaults: reflectance in, x / 0.9, normalised code value out): 18 % grey -> 0.3983 / 0.3434 (mk_logtab.py, same maths)
def clog2_inv(c):
    c = np.asarray(c, float)
    return 0.9 * np.where(c < 0.092864125, -(10.0 ** ((0.092864125 - c) / 0.24136077) - 1.0) / 87.09937546,
                          (10.0 ** ((c - 0.092864125) / 0.24136077) - 1.0) / 87.09937546)


def clog3_inv(c):
    c = np.asarray(c, float)
    return 0.9 * np.where(c < 0.097465473, -(10.0 ** ((0.12783901 - c) / 0.36726845) - 1.0) / 14.98325,
                          np.where(c <= 0.15277891, (c - 0.12512219) / 1.9754798,
                                   (10.0 ** ((c - 0.12240537) / 0.36726845) - 1.0) / 14.98325))


def clog2_enc(x):
    x = np.asarray(x, float) / 0.9
    return np.where(x < 0, -(0.24136077 * np.log10(np.maximum(-x, 0.0) * 87.09937546 + 1.0) - 0.092864125),
                    0.24136077 * np.log10(np.maximum(x, 0.0) * 87.09937546 + 1.0) + 0.092864125)


def clog3_enc(x):
    x = np.asarray(x, float) / 0.9
    return np.where(x < -0.014, -0.36726845 * np.log10(np.maximum(-x, 0.014) * 14.98325 + 1.0) + 0.12783901,
                    np.where(x <= 0.014, 1.9754798 * x + 0.12512219,
                             0.36726845 * np.log10(np.maximum(x, 0.014) * 14.98325 + 1.0) + 0.12240537))


def srgb_dec(v):
    v = np.asarray(v, float)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def srgb_enc(x):
    x = np.clip(np.asarray(x, float), 0.0, 1.0)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


LOG_INV = {"slog3": slog3_inv, "vlog": vlog_inv, "logc3": logc3_inv, "clog2": clog2_inv, "clog3": clog3_inv, "srgb": srgb_dec}
LOG_ENC = {"slog3": slog3_enc, "vlog": vlog_enc, "logc3": logc3_enc, "clog2": clog2_enc, "clog3": clog3_enc, "srgb": srgb_enc}
LOG_NAME = {"slog3": "S-Log3/S-Gamut3.Cine", "vlog": "V-Log/V-Gamut", "logc3": "LogC3 EI800/AWG3",
            "clog2": "Canon Log 2/Cinema Gamut", "clog3": "Canon Log 3/Cinema Gamut", "srgb": "sRGB display (camera JPEG)"}
LOG_MODE = {"slog3": "S-Log3", "vlog": "V-Log", "logc3": "LogC3", "clog2": "C-Log2", "clog3": "C-Log3", "srgb": "JPEG"}   # module output names


def srgb_oetf(L):
    L = np.clip(L, 0.0, 1.0)
    return np.where(L <= 0.0031308, 12.92 * L, 1.055 * np.power(L, 1 / 2.4) - 0.055)


def srgb_eotf(v):
    v = np.asarray(v, float)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def XYZ2Lab(XYZ, white=D65):
    t = XYZ / white
    f = np.where(t > (6 / 29) ** 3, np.cbrt(t), t / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


# ---------------------------------------------------------------- profiles
def _fillnan(a):
    a = np.array(a, float)
    if a.ndim == 1:
        a = a[:, None]
        sq = True
    else:
        sq = False
    for c in range(a.shape[1]):
        v = ~np.isnan(a[:, c])
        a[:, c] = np.interp(WL, WL[v], a[v, c])      # interior: linear, edges: nearest valid value
    return a[:, 0] if sq else a


class Stock:
    def __init__(self, name):
        d = json.load(open(os.path.join(PROF, name + ".json")))
        self.name, self.info, dd = name, d["info"], d["data"]
        self.meta = d.get("metadata", {})
        assert np.allclose(dd["wavelengths"], WL)
        ls = np.array(dd["log_sensitivity"], float)
        self.sens = np.where(np.isnan(ls), 0.0, 10.0 ** np.nan_to_num(ls, nan=-30.0))   # (81, 3) R, G, B layers
        self.dye = _fillnan(dd["channel_density"])          # (81, 3) cyan, magenta, yellow dye spectral density
        self.base = _fillnan(dd["base_density"])            # (81,)
        self.le = np.array(dd["log_exposure"], float)
        self.dc = np.array(dd["density_curves"], float)     # (256, 3) dye amount vs log exposure

    def curve(self, le):
        """le (..., 3) -> density (..., 3), per layer"""
        out = np.empty_like(le)
        for i in range(3):
            out[..., i] = np.interp(le[..., i], self.le, self.dc[:, i])
        return out

    def slope(self, le0=0.0):
        g = np.gradient(self.dc, self.le, axis=0)
        return np.array([np.interp(le0, self.le, g[:, i]) for i in range(3)])


# ---------------------------------------------------------------- reflectance training set
def training_spectra(n=3000, seed=20261112):
    """smooth reflectances sigmoid(c0 + c1 x + c2 x^2) (x = wavelength scaled to -1..1), plus greys"""
    rng = np.random.default_rng(seed)
    x = (WL - 580.0) / 200.0
    c0 = rng.uniform(-4.0, 3.0, n)
    c1 = rng.normal(0.0, 5.0, n)
    c2 = rng.normal(0.0, 8.0, n)
    z = c0[:, None] + c1[:, None] * x + c2[:, None] * x * x
    R = 0.01 + 0.94 / (1.0 + np.exp(-z))
    greys = np.repeat(np.array([0.03, 0.09, 0.18, 0.35, 0.6, 0.9])[:, None], WL.size, 1)
    return np.concatenate([R, greys], 0)


def fit_camera_to_film(stock, ref_illum, scene_illum=None):
    """3x3 M: XYZ (D65 adapted, white Y = 1) -> film layer exposure (white = 1 per layer under the
    reference illuminant). scene_illum != ref_illum: the scene is lit by scene_illum (the camera white
    balances it away), but the film still sees it (e.g. tungsten film in daylight without an 85)."""
    E_ref = illuminant(ref_illum)
    E_sc = E_ref if scene_illum is None else illuminant(scene_illum)
    R = training_spectra()
    film_norm = E_ref @ stock.sens                       # white under the reference illuminant -> 1
    film = (R * E_sc) @ stock.sens / film_norm
    W = E_sc @ CMF
    XYZ = (R * E_sc) @ CMF / W[1]
    XYZa = XYZ @ cat(W / W[1], D65).T
    w = 1.0 / (film.mean(1) + 0.02) ** 2
    A = XYZa * np.sqrt(w)[:, None]
    B = film * np.sqrt(w)[:, None]
    big = 1e4                                            # neutral kept exactly (white row, heavy weight)
    white_film = (E_sc @ stock.sens) / film_norm
    A = np.vstack([A, big * D65[None, :]])
    B = np.vstack([B, big * white_film[None, :]])
    M = np.linalg.lstsq(A, B, rcond=None)[0].T
    pred = XYZa @ M.T
    err = np.abs(np.log10(np.maximum(pred, 1e-4)) - np.log10(film))
    return M, {"fit_err_log_mean": float(err.mean()), "fit_err_log_p95": float(np.percentile(err, 95)),
               "white_film": white_film}


def _sigq(c, x):
    return 1.0 / (1.0 + np.exp(-(c[0] + c[1] * x + c[2] * x * x)))


def primary_spectra(illum_tag, scale=0.5):
    """smooth spectra for the three Rec.709 primaries: P_k = sigmoid(c0 + c1 x + c2 x^2) / scale,
    solved (Gauss-Newton) so that, lit by the illuminant and Bradford-adapted to D65, each has exactly
    the XYZ of its primary (NPM709 column). Any RGB709 >= 0 then stands for a smooth non-negative
    spectrum sum_k RGB_k P_k (a spectral reconstruction of the camera's colours)."""
    E = illuminant(illum_tag)
    W = E @ CMF
    to_ill = cat(D65, W / W[1])
    x = (WL - 580.0) / 200.0
    starts = [np.array([0.0, 10.0, 0.0]), np.array([2.0, 0.0, -20.0]), np.array([-1.0, -10.0, 0.0])]
    P, res = [], []
    for k in range(3):
        tgt = to_ill @ (NPM709[:, k] * scale)
        c = starts[k].copy()

        def f(c):
            xyz = (_sigq(c, x) * E) @ CMF / W[1]
            return np.log(np.maximum(xyz, 1e-9) / tgt)
        for _ in range(200):
            r = f(c)
            if np.abs(r).max() < 1e-10:
                break
            J = np.empty((3, 3))
            for j in range(3):
                d = np.zeros(3)
                d[j] = 1e-6
                J[:, j] = (f(c + d) - r) / 1e-6
            st = np.linalg.lstsq(J, r, rcond=None)[0]
            c = c - np.clip(st, -3, 3)
        P.append(_sigq(c, x) / scale)
        res.append(float(np.abs(f(c)).max()))
    return np.array(P).T, res                        # (81, 3), residuals


def camera_to_film_spectral(stock, ref_illum, scene_illum=None):
    """3x3 M709: linear Rec.709 (D65-adapted camera RGB) -> film layer exposure, from the primary
    spectra lit by the scene illuminant; each row normalized so RGB (1,1,1) -> 1 (neutral exact)."""
    E_ref = illuminant(ref_illum)
    sc = scene_illum or ref_illum
    E_sc = illuminant(sc)
    P, res = primary_spectra(sc)
    M = ((P * E_sc[:, None]).T @ stock.sens).T / (E_ref @ stock.sens)[:, None]
    wf = M.sum(1)
    if scene_illum is None:
        M = M / wf[:, None]
    else:                                            # keep the cast: normalize by the reference white
        Pw = P.sum(1)
        M = M / ((Pw * E_ref) @ stock.sens / (E_ref @ stock.sens))[:, None]
        M = M / np.exp(np.mean(np.log(M.sum(1))))     # meter: white geomean exposure = 1
    # error vs the true spectral exposure on the training reflectances (for the report only)
    R = training_spectra()
    film = (R * E_sc) @ stock.sens / (E_ref @ stock.sens)
    Wsc = E_sc @ CMF
    XYZa = ((R * E_sc) @ CMF / Wsc[1]) @ cat(Wsc / Wsc[1], D65).T
    pred = (XYZa @ XYZ2709.T) @ M.T
    ok = pred.min(1) > 1e-3
    err = np.abs(np.log10(np.maximum(pred[ok], 1e-4)) - np.log10(film[ok]))
    return M, {"fit_err_log_mean": float(err.mean()), "fit_err_log_p95": float(np.percentile(err, 95)),
               "primary_resid": res, "neg_pred_frac": float(1 - ok.mean())}


# ---------------------------------------------------------------- smooth helpers (C2)
def soft_floor(x, eps):
    return 0.5 * (x + np.sqrt(x * x + eps * eps))


def gamut_compress(rgb, thr=0.82):
    """pull channels below the achromatic value (max RGB) so none goes negative; identity for
    distances < thr, tanh roll-off above (C2), limit 1 (= channel 0)"""
    a = np.max(rgb, -1, keepdims=True)
    a_s = np.maximum(a, 1e-9)
    d = (a - rgb) / a_s
    k = 1.0 - thr
    dc = np.where(d > thr, thr + k * np.tanh((d - thr) / k), d)
    out = a - dc * a_s
    return np.where(a > 0, out, 0.0)


def shoulder(rgb, s0=0.80, top=1.0):
    """hue-keeping max-RGB shoulder: identity below s0, tanh roll-off to `top` above (C2)"""
    m = np.max(rgb, -1, keepdims=True)
    k = top - s0
    mc = np.where(m > s0, s0 + k * np.tanh((m - s0) / k), m)
    return rgb * np.where(m > 1e-9, mc / np.maximum(m, 1e-9), 1.0)


# ---------------------------------------------------------------- the model
class FilmModel:
    """stock / print profile names as spektrafilm file names; path 'print' or 'scan'."""

    def __init__(self, log, neg, prt=None, path="print", ref_illum=None, scene_illum=None,
                 exposure_ev=0.0, interlayer=0.0, grey_target=0.20, gain=None, proj_illum="D59",
                 flare=0.0015, surround=1.0, matrix="spectral", in_gamut_thr=0.70, neutral_axis=0.0,
                 shoulder_s0=None, scan_curve=(0.20, 1.0, 2.5, 1.0), scan_sat=1.0):
        if gain is None:
            gain = 1.2 if path == "print" else 1.0       # print: display white = 1.2 x clear base, see README
        if shoulder_s0 is None:
            shoulder_s0 = 0.80 if path == "print" else 0.92
        self.log, self.path = log, path
        self.neg = Stock(neg)
        self.prt = Stock(prt) if prt else None
        ri = ref_illum or self.neg.info["reference_illuminant"]
        if ri == "T":                                 # Kodak rates the tungsten stocks at 3200 K
            ri = "BB3200"
        self.ref_illum, self.scene_illum = ri, scene_illum
        self.matrix = matrix
        if matrix == "spectral":                       # RGB709 -> film (default, see camera_to_film_spectral)
            self.M709, self.fit = camera_to_film_spectral(self.neg, ri, scene_illum)
        else:                                          # least-squares 3x3 on the reflectance set
            M, self.fit = fit_camera_to_film(self.neg, ri, scene_illum)
            self.M709 = M @ NPM709
        self.M = self.M709
        self.cam2709 = XYZ2709 @ npm(GAMUT[log])
        self.in_thr, self.flare, self.surround = in_gamut_thr, flare, surround
        self.exposure_ev, self.interlayer = exposure_ev, interlayer
        self.grey_target, self.gain, self.s0 = grey_target, gain, shoulder_s0
        if path == "print":
            self.lamp = illuminant("TH-KG3")
            self.view = illuminant(proj_illum)
            dmin = self.prt.base + np.min(self.prt.dc, 0) @ self.prt.dye.T
            self.white = (self.view * 10.0 ** (-dmin)) @ CMF
            self.cat = XYZ2709 @ cat(self.white / self.white[1], D65)
            self.timing = np.zeros(3)
            self.neutral_axis = neutral_axis
            self._solve_timing()
            self.na_l, self.na_d = None, None
            if neutral_axis:
                self._solve_neutral_axis()
        else:
            # neutral scan: per-channel levels = per-channel gamma (regression slope of each layer's
            # curve over log exposure -1 .. +1.5 around grey) and per-channel black point at the
            # film's own D-min, so the grey scale comes out (nearly) neutral; the toe / shoulder
            # shape differences between the layers stay
            sel = (self.neg.le >= -1.0) & (self.neg.le <= 1.5)
            self.gamma = np.array([np.polyfit(self.neg.le[sel], self.neg.dc[sel, i], 1)[0] for i in range(3)])
            self.Dg = self.neg.curve(np.zeros((1, 3)))[0]
            self.floor = 0.18 * 10.0 ** ((self.neg.dc[0] - self.Dg) / self.gamma)
            self.film2709 = np.linalg.inv(self.M709)
            if scan_sat != 1.0:
                Yw = np.array([0.2126, 0.7152, 0.0722])
                S = (1 - scan_sat) * np.outer(np.ones(3), Yw) + scan_sat * np.eye(3)
                self.film2709 = S @ self.film2709
            self.scan_curve = scan_curve

    # --- stage functions
    def scene_linear(self, code):
        return LOG_INV[self.log](code)

    def layer_logexp(self, lin):
        rgb = lin @ self.cam2709.T
        if self.in_thr is not None:                    # colours outside Rec.709 (wide-gamut log footage):
            rgb = gamut_compress(rgb, self.in_thr)     # pulled in smoothly; the camera JPEG never needs it
        e = rgb @ self.M709.T
        e = soft_floor(e, 2e-5)
        return np.log10(e / 0.18) + self.exposure_ev * np.log10(2.0)

    def neg_density(self, le):
        D = self.neg.curve(le)
        if self.interlayer:
            n = self.neg.curve(np.repeat(le.mean(-1, keepdims=True), 3, -1))
            dD = D - n
            D = D + self.interlayer * (dD - dD.mean(-1, keepdims=True))
        return D

    def print_logexp(self, D, le=None):
        Ds = self.neg.base[None, :] + D @ self.neg.dye.T
        p = (10.0 ** (-Ds) * self.lamp[None, :]) @ self.prt.sens
        lp = np.log10(np.maximum(p, 1e-30)) + self.timing
        if le is not None and self.na_l is not None:  # optional neutral-axis balance (printer-light
            l = le.mean(-1)                            # offsets per neutral level, see _solve_neutral_axis)
            for j in (0, 2):
                lp[..., j] += self.neutral_axis * np.interp(l, self.na_l, self.na_d[:, j])
        return lp

    def _solve_neutral_axis(self):
        """per neutral exposure level, the red / blue printer-light offsets that make the grey scale print
        neutral (green fixed); smoothed, faded out beyond -5 / +5 EV. Off by default (physical crossovers)."""
        evs = np.arange(-7.0, 7.01, 0.25)
        out = np.zeros((evs.size, 3))
        for k, e in enumerate(evs):
            le = np.full((1, 3), e * np.log10(2.0))
            D = self.neg_density(le)
            lp0 = self.print_logexp(D)
            d = np.zeros(3)
            for _ in range(30):
                def g(dd):
                    rgb = self.print_display(self.prt.curve(lp0 + dd))[0]
                    return np.log10(np.maximum(rgb[[0, 2]], 1e-9) / max(rgb[1], 1e-9))
                f = g(d)
                if np.abs(f).max() < 1e-6:
                    break
                J = np.empty((2, 2))
                for a, j in enumerate((0, 2)):
                    dd = d.copy()
                    dd[j] += 1e-4
                    J[:, a] = (g(dd) - f) / 1e-4
                try:
                    st = np.linalg.solve(J, f)
                except np.linalg.LinAlgError:
                    break
                d[[0, 2]] -= np.clip(st, -0.1, 0.1)
            out[k] = np.clip(d, -0.3, 0.3)
        w = np.clip((7.0 - np.abs(evs)) / 2.0, 0.0, 1.0)          # fade 5..7 EV
        out = out * w[:, None]
        k3 = np.array([0.25, 0.5, 0.25])
        for j in (0, 2):
            out[:, j] = np.convolve(np.pad(out[:, j], 1, mode="edge"), k3, mode="valid")
        self.na_l, self.na_d = evs * np.log10(2.0), out

    def print_display(self, P):
        """print -> display linear before the shoulder: projected, plus a neutral viewing flare
        (fraction of the clear-base white; ~1:250 intra-frame contrast of a projected print), x gain"""
        return (self.print_rgb(P) + self.flare) * self.gain

    def print_rgb(self, P):
        Ds = self.prt.base[None, :] + P @ self.prt.dye.T
        XYZ = (10.0 ** (-Ds) * self.view[None, :]) @ CMF / self.white[1]
        return XYZ @ self.cat.T

    def _grey_D(self):
        lin = np.full((1, 3), 0.18)
        return self.neg_density(self.layer_logexp(lin))

    def _solve_timing(self):
        Dg = self._grey_D()
        lp0 = self.print_logexp(Dg)[0]
        self.timing = 0.30 - lp0                      # first guess: grey on the print's lower straight part
        tgt = np.log10(self.grey_target)
        for _ in range(40):
            f = np.log10(np.maximum(self.print_display(self.prt.curve(self.print_logexp(Dg)))[0], 1e-9)) - tgt
            if np.abs(f).max() < 1e-7:
                break
            J = np.empty((3, 3))
            for j in range(3):
                t0 = self.timing.copy()
                self.timing[j] += 1e-4
                fj = np.log10(np.maximum(self.print_display(self.prt.curve(self.print_logexp(Dg)))[0], 1e-9)) - tgt
                self.timing = t0
                J[:, j] = (fj - f) / 1e-4
            self.timing = self.timing - np.linalg.solve(J, f)
        self.timing_resid = float(np.abs(f).max())

    # soft scanner tone curve, per channel, in log2 terms around grey: x = log2(L / 0.18),
    # u = umax - softplus_beta(umax - c x) (+ offset so u(0) = 0), y = yg * 2^u. Below the knee a
    # straight line of log-log slope c, above it a smooth approach to ymax (never reached).
    @staticmethod
    def scan_tone(L, yg=0.20, c=1.0, beta=2.5, ymax=1.0):
        umax = np.log2(ymax / yg)
        off = np.log2(1.0 + 2.0 ** (-beta * umax)) / beta
        x = np.log2(np.maximum(L, 1e-12) / 0.18)
        t = beta * (umax - c * x)
        sp = np.where(t > 30, t, np.log2(1.0 + 2.0 ** np.minimum(t, 30))) / beta
        u = umax - sp + off
        return np.where(L > 0, yg * 2.0 ** u, 0.0)

    # --- full chain
    def display_linear(self, code, stages=False):
        lin = self.scene_linear(code)
        le = self.layer_logexp(lin)
        D = self.neg_density(le)
        if self.path == "print":
            lp = self.print_logexp(D, le)
            P = self.prt.curve(lp)
            rgb = self.print_display(P)
            if self.surround != 1.0:                   # dark-surround print -> dim-surround display:
                Y = rgb @ NPM709[1]                    # luminance power around grey (grey unchanged)
                Ys = soft_floor(Y, 1e-6)
                rgb = rgb * (self.grey_target * (Ys / self.grey_target) ** self.surround / Ys)[..., None]
        else:
            q = (D - self.Dg) / self.gamma
            L = 0.18 * 10.0 ** q
            L = soft_floor((L - self.floor) * 0.18 / (0.18 - self.floor), 1e-5)   # per-channel black point
            rgb = gamut_compress(L @ self.film2709.T)
            rgb = self.scan_tone(rgb, *self.scan_curve) * self.gain
        rgb = gamut_compress(rgb)
        rgb = shoulder(rgb, self.s0)
        if stages:
            return rgb, {"lin": lin, "le": le, "D": D}
        return rgb

    def __call__(self, code):
        return srgb_oetf(self.display_linear(code))


# ---------------------------------------------------------------- cube I/O (same layout as mk_testluts)
def grid(N):
    ax = np.linspace(0.0, 1.0, N)
    b, g, r = np.meshgrid(ax, ax, ax, indexing="ij")          # R fastest in file order
    return np.stack([r, g, b], -1).reshape(-1, 3)


def write_cube(path, title, comments, N, vals):
    assert len(title) <= 23, title
    hdr = ['TITLE "%s"' % title] + ["# " + c for c in comments] + ["LUT_3D_SIZE %d" % N]
    body = ["%.6f %.6f %.6f" % tuple(v) for v in vals]
    with open(path, "wb") as f:
        f.write(("\n".join(hdr + body) + "\n").encode("latin-1"))


def read_cube(path):
    n, vals = 0, []
    for ln in open(path, "rb").read().decode("latin-1").split("\n"):
        s = ln.split("#")[0].strip()
        if not s:
            continue
        t = s.split()
        if t[0] == "LUT_3D_SIZE":
            n = int(t[1])
        elif t[0][0].isalpha():
            continue
        else:
            vals.append([float(x) for x in t])
    v = np.array(vals)
    assert v.shape == (n ** 3, 3)
    return n, v


def cube_apply(cube, x):
    """tetrahedral, as the camera (and llref.py)"""
    n, L = cube
    g = np.clip(x * (n - 1), 0, n - 1)
    i = np.minimum(np.floor(g), n - 2).astype(np.int64)
    f = g - i
    st = np.array([1, n, n * n])
    base = i @ st
    o = np.argsort(-f, axis=1, kind="stable")
    fs = np.take_along_axis(f, o, 1)
    so = st[o]
    c1 = base + so[:, 0]
    c2 = c1 + so[:, 1]
    c3 = c2 + so[:, 2]
    return ((1 - fs[:, 0])[:, None] * L[base] + (fs[:, 0] - fs[:, 1])[:, None] * L[c1]
            + (fs[:, 1] - fs[:, 2])[:, None] * L[c2] + fs[:, 2][:, None] * L[c3])
