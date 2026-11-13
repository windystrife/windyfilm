#!/usr/bin/env python3
"""mk_wfprints.py OUTDIR [--only CODE,...] [--n 33] [--set key=value ...] - the Cine / Paper print cubes of WindyFilm.

For every film whose negative exists in the spektrafilm profile data, two display-referred cubes (input = the camera
JPEG as sRGB display values, output = sRGB display values, the same convention as luts/looks):
  <CODE>_C.CUB  Cine print: negative -> Kodak 2383 print film, projected   (spektrafilm profile kodak_2383)
  <CODE>_P.CUB  Paper print: negative -> the colour paper its profile names as target_print (Portra Endura for the
                Kodak stills, Crystal Archive Type II for the Fuji stills); not made for the cine negatives (their
                target is the 2383 print)
tools/mk_wtab.py turns them into films/tables/<code>_C.T17 / _P.T17. Scan (the existing look of the catalogue, luts/looks)
is not touched. Model: fl_core.py (own code, the negative -> print chain of the Log + LUT cubes with a JPEG input).

Film data: spektrafilm profiles, CC BY-SA 4.0, A. Volpato (github.com/andreavolpato/spektrafilm), read from its JSON
files (WF_SPEKTRAFILM = a checkout; nothing is downloaded). Its code is GPLv3 and is not used. The cubes and tables
made from it are CC BY-SA 4.0 too (changes: an own negative / print / viewing model, see fl_core.py).

--set key=value overrides a FilmModel parameter for the selected films (exposure_ev, interlayer, gain, grey_target, ...).
"""
import argparse, json, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fl_core as F

PRINT_DEF = {"interlayer": 0.25, "neutral_axis": 0.5}
# film code -> negative profile; paper profile = the negative's target_print (read from its info); cine films: no paper
NEG = {"PRT160": "kodak_portra_160", "PRT400": "kodak_portra_400", "PRT800": "kodak_portra_800",
       "SUN200": "kodak_gold_200", "EVD400": "kodak_ultramax_400", "BLD100": "kodak_ektar_100",
       "PAS400": "fujifilm_pro_400h", "TRV400": "fujifilm_xtra_400",
       "CIN50D": "kodak_vision3_50d", "CINE250D": "kodak_vision3_250d", "TUN800": "kodak_vision3_500t"}
CINE_NEG = ("CIN50D", "CINE250D", "TUN800")
# paper print: viewed by reflection under D50 (print viewing standard); the 2383 print keeps the projector default
PAPER_DEF = dict(PRINT_DEF, proj_illum="D50", gain=1.12, flare=0.004, grey_target=0.20)

def build(code, variant, n, over):
    neg = NEG[code]
    info = F.Stock(neg).info
    if variant == "cine":
        prt, kw = "kodak_2383", dict(PRINT_DEF)
    else:
        prt, kw = info["target_print"], dict(PAPER_DEF)
    kw.update(over)
    m = F.FilmModel("srgb", neg, prt, "print", **kw)
    vals = m(F.grid(n))
    assert np.isfinite(vals).all() and vals.min() >= 0.0 and vals.max() <= 1.0
    return m, prt, vals

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--only", default="")
    ap.add_argument("--n", type=int, default=33)
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
    rep = {}
    for code in NEG:
        if a.only and code not in a.only.split(","):
            continue
        for variant, suf in (("cine", "_C"), ("paper", "_P")):
            if variant == "paper" and code in CINE_NEG:
                continue
            m, prt, vals = build(code, variant, a.n, over)
            fn = os.path.join(a.out, code + suf + ".CUB")
            F.write_cube(fn, "%s %s" % (code, variant), [
                "WindyFilm %s print of %s: negative %s -> %s." % (variant, code, NEG[code], prt),
                "Input: the camera JPEG (sRGB display values). Output: sRGB display values.",
                "film data: spektrafilm profiles, CC BY-SA 4.0, A. Volpato (github.com/andreavolpato/spektrafilm);",
                "this cube is CC BY-SA 4.0 too. Generator: mk_wfprints.py / fl_core.py (WindyStrife)."], a.n, vals)
            grey = F.srgb_oetf(m.display_linear(F.srgb_enc(np.full((1, 3), 0.18))))[0]
            rep[code + suf] = {"negative": NEG[code], "print": prt, "timing": np.round(m.timing, 4).tolist(),
                               "timing_resid": m.timing_resid, "grey18_out_code": np.round(grey * 255, 1).tolist(),
                               "fit_err": round(m.fit["fit_err_log_mean"], 4)}
            print("%-10s %-30s -> %-34s timing %s grey18 -> %s" % (code + suf, NEG[code], prt, np.round(m.timing, 3), np.round(grey * 255, 1)), flush=True)
    json.dump(rep, open(os.path.join(a.out, "prints.json"), "w"), indent=1)

if __name__ == "__main__":
    main()
