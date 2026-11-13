#!/usr/bin/env python3
"""mk_filmluts.py OUTDIR [--only NAME,...] [--n 65] [--set key=value ...]

Film-emulation 3D LUTs for the Film Roll "Log + LUT" modes (input = the camera log curve + gamut of
the mode, output = sRGB-encoded display values for the JPEG). Model: fl_core.py (own code).

Stock choices are parameters: edit STOCKS below or pass --set (applies to every selected stock), e.g.
  --set exposure_ev=0.33 --set interlayer=0.3 --set gain=1.1 --set grey_target=0.18
Writes OUTDIR/<NAME>.CUB, OUTDIR/params.json (every parameter, timing, fit error per cube),
OUTDIR/CHANGELOG.txt and a copy of spektrafilm's licence text (CC BY-SA 4.0 requires both).
"""
import argparse
import hashlib
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fl_core as F  # noqa: E402

ATTR = "film data: spektrafilm profiles, CC BY-SA 4.0, A. Volpato (github.com/andreavolpato/spektrafilm)"

# defaults per path (estimates, see README_FILMLUT.txt): interlayer = DIR-like colour separation in
# density (neutral unchanged); neutral_axis = share of the print grey-scale crossover removed (0 = raw model)
PRINT_DEF = {"interlayer": 0.25, "neutral_axis": 0.5}
SCAN_DEF = {"interlayer": 0.10}
# key: (card prefix, title stem, negative profile, print profile or None, path, extra params, description)
STOCKS = {
    "V250D": ("V250D", "Vision3 250D>2383", "kodak_vision3_250d", "kodak_2383", "print", PRINT_DEF,
              "Kodak Vision3 250D (5207) negative printed on Kodak Vision 2383 print film, projected"),
    "V500T": ("V500T", "Vision3 500T>2383", "kodak_vision3_500t", "kodak_2383", "print", PRINT_DEF,
              "Kodak Vision3 500T (5219) negative printed on Kodak Vision 2383, projected; camera white "
              "balance trusted = film exposed under its 3200 K rating (as if lit by tungsten or shot with an 85)"),
    "PRT400": ("PRT400", "Portra 400 scan", "kodak_portra_400", None, "scan", SCAN_DEF,
               "Kodak Portra 400 still negative, neutral scan (per-channel levels, soft scanner curve)"),
    "FP400H": ("FP400H", "Pro 400H scan", "fujifilm_pro_400h", None, "scan", SCAN_DEF,
               "Fujifilm Pro 400H still negative, neutral scan (extra: Fuji still stock, no Eterna data)"),
}
LOGS = {"slog3": ("S3", "SLog3"), "vlog": ("VL", "VLog"), "logc3": ("LC", "LogC3"), "clog2": ("C2", "CLog2"),
        "clog3": ("C3", "CLog3")}


def card_name(key, log):
    pre = STOCKS[key][0]
    suf = LOGS[log][0]
    name = (pre + "_" + suf) if len(pre) + 1 + len(suf) <= 8 else (pre + suf)
    assert len(name) <= 8, name
    return name + ".CUB"


def build(key, log, N, over):
    pre, stem, neg, prt, path, extra, desc = STOCKS[key]
    kw = dict(extra)
    kw.update(over)
    m = F.FilmModel(log, neg, prt, path, **kw)
    vals = m(F.grid(N))
    assert np.isfinite(vals).all() and vals.min() >= 0.0 and vals.max() <= 1.0
    title = "%s %s" % (stem, LOGS[log][1])
    if len(title) > 23:
        title = title[:23]
    prm = {"negative": neg, "print": prt, "path": path, "log": log, "ref_illum": m.ref_illum,
           "scene_illum": m.scene_illum, "exposure_ev": m.exposure_ev, "interlayer": m.interlayer, "neutral_axis": getattr(m, "neutral_axis", None),
           "grey_target": m.grey_target, "gain": m.gain, "shoulder_s0": m.s0, "flare": m.flare,
           "surround": m.surround, "matrix": m.matrix,
           "rec709_to_film_3x3": np.round(m.M, 5).tolist(),
           "fit_err_log_mean": round(m.fit["fit_err_log_mean"], 4), "fit_err_log_p95": round(m.fit["fit_err_log_p95"], 4)}
    if path == "print":
        prm.update({"printer_lights_log": np.round(m.timing, 5).tolist(), "timing_resid": m.timing_resid,
                    "printer_lamp": "3400 K tungsten + Schott KG3", "projector": "CIE D59 (xenon proxy)"})
    else:
        prm.update({"scan_gamma": np.round(m.gamma, 4).tolist(), "scan_curve": list(m.scan_curve),
                    "scan_black_floor": np.round(m.floor, 5).tolist()})
    com = [desc + ".",
           "Input: %s (cube 0..1 = log code 0..1023/1023), the Film Roll '%s + LUT' mode." % (F.LOG_NAME[log], F.LOG_MODE[log]),
           "Output: display-referred Rec.709 primaries, sRGB transfer (for the camera JPEG). Grey 18%% -> %.3f display linear." % m.grey_target,
           ATTR + ".",
           "This LUT encodes those profiles and is itself CC BY-SA 4.0 (changes: rendered through an own",
           "negative/print/scan model, fl_core.py; see README_FILMLUT.txt and CHANGELOG.txt).",
           "CIE 1931 CMFs and daylight components: CIE datasets. Generator: mk_filmluts.py (WindyStrife).",
           "params: " + json.dumps({k: prm[k] for k in ("ref_illum", "exposure_ev", "interlayer", "neutral_axis", "grey_target", "gain", "flare")})]
    return m, title, com, vals, prm


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--only", default="")
    ap.add_argument("--logs", default="slog3,vlog,logc3,clog2,clog3")
    ap.add_argument("--n", type=int, default=65)
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    over = {}
    for s in a.set:
        k, v = s.split("=", 1)
        try:
            v = float(v)
        except ValueError:
            pass
        over[k] = v
    os.makedirs(a.out, exist_ok=True)
    keys = [k for k in STOCKS if not a.only or k in a.only.split(",")]
    allp = {}
    pf = os.path.join(a.out, "params.json")
    if os.path.exists(pf):
        allp = json.load(open(pf))
    for key in keys:
        for log in a.logs.split(","):
            m, title, com, vals, prm = build(key, log, a.n, over)
            fn = os.path.join(a.out, card_name(key, log))
            F.write_cube(fn, title, com, a.n, vals)
            md5 = hashlib.md5(open(fn, "rb").read()).hexdigest()
            prm.update({"file": os.path.basename(fn), "title": title, "size": a.n, "bytes": os.path.getsize(fn), "md5": md5})
            allp[os.path.basename(fn)] = prm
            print("%-13s %-24s %8d B  md5 %s  fit %.3f  %s" % (os.path.basename(fn), title, prm["bytes"], md5,
                  prm["fit_err_log_mean"], ("timing " + str(prm.get("printer_lights_log"))) if prm["path"] == "print" else ("gamma " + str(prm["scan_gamma"]))), flush=True)
    json.dump(allp, open(pf, "w"), indent=1)
    shutil.copy(os.path.join(F.SPK, "license", "SPEKTRAFILM_LICENSE.txt"), os.path.join(a.out, "SPEKTRAFILM_LICENSE.txt"))
    with open(os.path.join(a.out, "CHANGELOG.txt"), "w", newline="\r\n") as f:
        f.write("Film Roll film LUTs - changes relative to the spektrafilm profiles they encode\n"
                "(" + ATTR + "; licence: SPEKTRAFILM_LICENSE.txt, CC BY-SA 4.0)\n\n"
                "2026-11-12  first version (mk_filmluts.py / fl_core.py, own code, no spektrafilm code):\n"
                "  - profiles used as published: spectral sensitivities, dye spectral densities, base density,\n"
                "    density curves of kodak_vision3_250d, kodak_vision3_500t, kodak_portra_400,\n"
                "    fujifilm_pro_400h (negatives) and kodak_2383 (print); NaN samples at the ends of the\n"
                "    wavelength range filled with the nearest valid value\n"
                "  - camera -> film layer exposure reduced to a 3x3 built from smooth positive spectra of the\n"
                "    Rec.709 primaries (sigmoid-quadratic), rows normalized so neutral is exact\n"
                "  - negative: added interlayer (DIR-like) colour term, 0.25 for print, 0.10 for scans\n"
                "    (neutral unchanged)\n"
                "  - printing: 3400 K + KG3 lamp, printer lights solved for neutral grey; half of the\n"
                "    print grey-scale colour crossover removed (neutral_axis 0.5); projection under\n"
                "    CIE D59 as a xenon proxy; display white = 1.2 x clear print base with a soft shoulder,\n"
                "    viewing flare 0.0015\n"
                "  - scans: ideal dye-amount scanner, per-channel levels, own soft tone curve\n"
                "  - output encoded for sRGB displays, gamut-compressed into Rec.709\n"
                "  - sampled on a %d^3 grid (first version 33^3; 65^3 since the second build of\n"
                "    2026-11-12, same parameters)\n" % a.n)


if __name__ == "__main__":
    main()
