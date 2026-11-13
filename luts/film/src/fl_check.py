#!/usr/bin/env python3
"""fl_check.py CUBEDIR [--json OUT] - self-check numbers for the film cubes (and the module's
technical cube of each log mode for comparison).

Everything goes through the cube file as written (tetrahedral, like the camera), with the camera's
input model (Rec.709 linear 0..1 -> log gamut -> log curve), except the extended grey ramp, which
runs to +5 EV (beyond the camera's 1.0 = +2.47 EV) to test the roll-off itself.
  grey     a*, b* of the grey ramp (CIELAB, D65, from the output sRGB), max chroma over -4..+5 EV
  hi       codes at +2.47 (camera white), +3, +4, +5 EV; monotonic to +5 EV; first EV at code >= 254.5
  skin     hue angle / chroma of 9 skin patches, Canon (input) vs output
  smooth   2nd differences of the cube along R, G, B: max and p99.9 (vs the tech cube)
  black    output code for camera black (linear 0) and at -4 EV; cube min
  interp   cube (as written, tetrahedral) vs the exact model on 20,000 random in-camera colours (codes)
  s3vl     S-Log3 cube vs V-Log cube of the same stock on the same colours (codes)
"""
import argparse
import json
import os
import sys

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fl_core as F  # noqa: E402
import fl_render as R  # noqa: E402
import mk_filmluts as MF  # noqa: E402

sys.path.insert(0, R.REPO_LL)
import mk_testluts as MT  # noqa: E402


def lab_of_code(code):
    return F.XYZ2Lab(F.srgb_eotf(code) @ F.NPM709.T)


def enc(lin709, log, clip=True):
    return R.to_log(lin709, log, clip)


def check(cube, log, model=None):
    out = {}
    # grey ramp
    ev = np.arange(-6.0, 5.01, 0.25)
    lin = np.repeat((0.18 * 2.0 ** ev)[:, None], 3, 1)
    code = F.cube_apply(cube, enc(lin, log, clip=False))
    lab = lab_of_code(code)
    sel = (ev >= -4) & (ev <= 5)
    C = np.hypot(lab[:, 1], lab[:, 2])
    i = np.argmax(np.where(sel, C, -1))
    out["grey"] = {"max_C": round(float(C[sel].max()), 2), "at_ev": float(ev[i]),
                   "a_b_at_max": [round(float(lab[i, 1]), 2), round(float(lab[i, 2]), 2)],
                   "C_-2..+2": round(float(C[(ev >= -2) & (ev <= 2)].max()), 2),
                   "code_grey": round(float(code[ev == 0].mean() * 255), 1)}
    Y = F.srgb_eotf(code) @ F.NPM709[1]
    hi = {}
    for e in (2.47, 3.0, 4.0, 5.0):
        c1 = F.cube_apply(cube, enc(np.full((1, 3), 0.18 * 2 ** e), log, clip=False))
        hi["%+.2f" % e] = round(float(c1.mean() * 255), 1)
    mono = bool(np.all(np.diff(Y[ev >= -4]) > 0))
    clipat = ev[(code.max(1) * 255 >= 254.5)]
    hi["monotonic_-4..+5"] = mono
    hi["min_step_+3..+5_code"] = round(float(np.diff(code.mean(1)[ev >= 3]).min() * 255), 3)
    hi["first_ev_code_254.5"] = float(clipat[0]) if clipat.size else None
    out["hi"] = hi
    # skin
    skins = [R.CC24[0], R.CC24[1]] + R.SKIN
    s_in = np.array(skins) / 255.0
    lab_in = lab_of_code(s_in)
    code_s = F.cube_apply(cube, enc(F.srgb_eotf(s_in), log))
    lab_out = lab_of_code(code_s)
    h_in = np.degrees(np.arctan2(lab_in[:, 2], lab_in[:, 1]))
    h_out = np.degrees(np.arctan2(lab_out[:, 2], lab_out[:, 1]))
    c_in = np.hypot(lab_in[:, 1], lab_in[:, 2])
    c_out = np.hypot(lab_out[:, 1], lab_out[:, 2])
    out["skin"] = {"hue_in_mean": round(float(h_in.mean()), 1), "hue_out_mean": round(float(h_out.mean()), 1),
                   "dhue_mean": round(float((h_out - h_in).mean()), 1), "dhue_range": [round(float((h_out - h_in).min()), 1), round(float((h_out - h_in).max()), 1)],
                   "chroma_ratio_mean": round(float((c_out / c_in).mean()), 3),
                   "L_in_out_lightskin": [round(float(lab_in[1, 0]), 1), round(float(lab_out[1, 0]), 1)]}
    # smoothness
    n, L = cube
    G = L.reshape(n, n, n, 3)                  # [b, g, r, ch]
    d2 = [np.abs(np.diff(G, 2, axis=a)) for a in (2, 1, 0)]
    allv = np.concatenate([d.ravel() for d in d2])
    # the part of the cube the camera can reach: nodes whose Rec.709 linear is within -0.02 .. 1.02
    lin = F.LOG_INV[log](F.grid(n)) @ (np.linalg.inv(F.NPM709) @ F.npm(F.GAMUT[log])).T
    ok = np.all((lin >= -0.02) & (lin <= 1.02), -1).reshape(n, n, n)
    cam = []
    for a, d in zip((2, 1, 0), d2):
        sl = [slice(None)] * 3
        m3 = np.ones(d.shape[:3], bool)
        for k in range(3):
            sl[a] = slice(k, k + d.shape[a])
            m3 &= ok[tuple(sl)]
        cam.append(d[m3].ravel())
    cam = np.concatenate(cam)
    out["smooth"] = {"d2_max": round(float(allv.max()), 4), "d2_p999": round(float(np.percentile(allv, 99.9)), 4),
                     "d2_max_RGB_axis": [round(float(d.max()), 4) for d in d2],
                     "d2_max_camera_region": round(float(cam.max()), 4) if cam.size else None,
                     "d2_p999_camera_region": round(float(np.percentile(cam, 99.9)), 4) if cam.size else None,
                     "camera_region_nodes": int(ok.sum())}
    # black
    blk = F.cube_apply(cube, enc(np.zeros((1, 3)), log))
    m4 = F.cube_apply(cube, enc(np.full((1, 3), 0.18 / 16), log))
    out["black"] = {"code_black": np.round(blk[0] * 255, 1).tolist(), "code_-4ev": round(float(m4.mean() * 255), 1),
                    "cube_min": round(float(L.min()), 5), "cube_max": round(float(L.max()), 5),
                    "finite": bool(np.isfinite(L).all())}
    if model is not None:
        rng = np.random.default_rng(5)
        s = rng.uniform(0, 1, (20000, 3))
        lin = F.srgb_eotf(s)
        c = enc(lin, log)
        ex = model(c)
        cb = F.cube_apply(cube, c)
        d = np.abs(ex - cb) * 255
        out["interp"] = {"mean": round(float(d.mean()), 3), "p99.9": round(float(np.percentile(d, 99.9)), 2), "max": round(float(d.max()), 2)}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cubes")
    ap.add_argument("--json", default="")
    ap.add_argument("--stocks", default="V250D,V500T,PRT400,FP400H")
    ap.add_argument("--logs", default="slog3,vlog,logc3")
    a = ap.parse_args()
    res = {}
    for log in a.logs.split(","):
        mode = R.MODES[log] - 1
        tech = (33, np.clip(MT.tech(mode, MT.grid(33)), 0, 1))
        res["TECH_" + log] = check(tech, log)
        for k in a.stocks.split(","):
            fn = os.path.join(a.cubes, MF.card_name(k, log))
            if not os.path.exists(fn):
                continue
            pre, stem, neg, prt, path, extra, desc = MF.STOCKS[k]
            prm = json.load(open(os.path.join(a.cubes, "params.json"))).get(os.path.basename(fn), {})
            kw = dict(extra)
            kw.update({kk: prm[kk] for kk in ("exposure_ev", "interlayer", "neutral_axis", "grey_target", "gain", "flare") if prm.get(kk) is not None})

            m = F.FilmModel(log, neg, prt, path, **kw)
            res[os.path.basename(fn)] = check(F.read_cube(fn), log, m)
    # S-Log3 vs V-Log vs LogC3 of the same stock on the same in-camera colours
    rng = np.random.default_rng(9)
    lin = F.srgb_eotf(rng.uniform(0, 1, (20000, 3)))
    for k in a.stocks.split(","):
        outs = {}
        for log in a.logs.split(","):
            fn = os.path.join(a.cubes, MF.card_name(k, log))
            if os.path.exists(fn):
                outs[log] = F.cube_apply(F.read_cube(fn), enc(lin, log))
        if "slog3" in outs:
            for log in outs:
                if log != "slog3":
                    d = np.abs(outs[log] - outs["slog3"]) * 255
                    res.setdefault("consistency", {})["%s %s-vs-slog3" % (k, log)] = {"mean": round(float(d.mean()), 3), "max": round(float(d.max()), 2)}
    for k, v in res.items():
        print(k, json.dumps(v))
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
