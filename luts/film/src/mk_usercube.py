#!/usr/bin/env python3
"""mk_usercube.py IN.cube OUTDIR [--stem EKTAP] [--in-log vlog] [--n 65] [--title "EKTA P"]

The owner's own grade as a Film LUT of the "+ LUT" outputs (menu Film LUT "EKTA P", catalogue can 66 LEKTAP): he
grades in DaVinci Resolve on V-Log / V-Gamut footage and exports the node tree as a 3D LUT (Resolve: right-click the
still or the clip > Generate LUT > 65 Point Cube; input V-Log/V-Gamut, output display sRGB / Rec.709 primaries, with
any colour-space transform the grade needs inside the node tree). This tool makes the five card cubes of that grade,
one per log curve of the camera, from that one export:

  <stem>_S3.CUB  S-Log3 / S-Gamut3.Cine      <stem>_VL.CUB  V-Log / V-Gamut (the export resampled to 65^3; a 65^3
  <stem>_LC.CUB  LogC3 EI800 / AWG3                         export comes out with the same values)
  <stem>_C2.CUB  Canon Log 2 / Cinema Gamut  <stem>_C3.CUB  Canon Log 3 / Cinema Gamut

Each node of the 65^3 output grid is a code of the target log: decoded to scene linear in its gamut (fl_core.py
LOG_INV, published formulas), to CIE XYZ (D65) and to V-Gamut (normalized primary matrices, all D65: no adaptation),
encoded as V-Log, clamped to the cube's 0..1 domain, and looked up in the export with tetrahedral interpolation (as
Resolve and the camera's loader do). Colours outside V-Gamut / below V-Log code 0 (the corners of a wider gamut,
which the camera never produces: its JPEG is inside Rec.709) clamp to the export's edge. The camera's own
input stays inside Rec.709, so the cubes agree on everything it can shoot (checked below: 20,000 random sRGB colours
through every cube vs the export, codes).

Spatial parts of a grade (Resolve halation, glow, grain, blur, sharpening, noise reduction) are not in a 3D LUT: the
module's own Halation / Grain / Mist rows stand in for them. Pure numpy; the export is read with its DOMAIN_MIN /
DOMAIN_MAX (or LUT_3D_INPUT_RANGE); a 1D shaper is refused (the camera's loader refuses one too).
Own code (WindyStrife); the grade and its export are the owner's and are not in the repo.
"""
import argparse
import hashlib
import os
import sys

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fl_core as F  # noqa: E402

SUFFIX = {"slog3": "S3", "vlog": "VL", "logc3": "LC", "clog2": "C2", "clog3": "C3"}   # = fr_loglut.c paths, curve order


def read_cube(path):
    """(N, values (N^3, 3) R fastest, domain min (3,), domain max (3,), title)"""
    n, vals, title = 0, [], ""
    lo, hi = np.zeros(3), np.ones(3)
    for ln in open(path, "rb").read().decode("latin-1").splitlines():
        s = ln.split("#")[0].strip()
        if not s:
            continue
        t = s.split()
        k = t[0].upper()
        if k == "TITLE":
            title = s[5:].strip().strip('"')
        elif k == "LUT_3D_SIZE":
            n = int(t[1])
        elif k in ("LUT_1D_SIZE", "LUT_1D_INPUT_RANGE"):
            sys.exit("%s: a 1D LUT / 1D shaper (%s): export a plain 3D cube (65 point)" % (path, k))
        elif k == "DOMAIN_MIN":
            lo = np.array([float(v) for v in t[1:4]])
        elif k == "DOMAIN_MAX":
            hi = np.array([float(v) for v in t[1:4]])
        elif k == "LUT_3D_INPUT_RANGE":
            lo, hi = np.full(3, float(t[1])), np.full(3, float(t[2]))
        elif k[0].isalpha():
            continue
        else:
            vals.append([float(v) for v in t[:3]])
    v = np.array(vals, float)
    if not 2 <= n <= 256 or v.shape != (n ** 3, 3):
        sys.exit("%s: LUT_3D_SIZE %d, %d samples" % (path, n, len(v)))
    if not np.isfinite(v).all() or np.any(hi <= lo):
        sys.exit("%s: non-finite samples or an empty domain" % path)
    return n, v, lo, hi, title


def tetra(n, v, x):
    """tetrahedral lookup of x (M, 3) in 0..1 (clamped) in a cube of size n, values v (n^3, 3), R fastest"""
    L = v.reshape(n, n, n, 3)                         # [b, g, r]
    p = np.clip(x, 0.0, 1.0) * (n - 1)
    i = np.minimum(np.floor(p).astype(int), n - 2)
    f = p - i
    r0, g0, b0 = i[:, 0], i[:, 1], i[:, 2]
    fr, fg, fb = f[:, 0:1], f[:, 1:2], f[:, 2:3]

    def c(dr, dg, db):
        return L[b0 + db, g0 + dg, r0 + dr]
    c000, c111 = c(0, 0, 0), c(1, 1, 1)
    out = np.empty_like(x, dtype=float)
    # the 6 tetrahedra by the order of fr, fg, fb
    for cond, (a, b), (wa, wb, wc) in (
            ((fr >= fg) & (fg >= fb), ((1, 0, 0), (1, 1, 0)), (fr, fg, fb)),
            ((fr >= fb) & (fb > fg), ((1, 0, 0), (1, 0, 1)), (fr, fb, fg)),
            ((fb > fr) & (fr >= fg), ((0, 0, 1), (1, 0, 1)), (fb, fr, fg)),
            ((fg > fr) & (fr >= fb), ((0, 1, 0), (1, 1, 0)), (fg, fr, fb)),
            ((fg >= fb) & (fb > fr), ((0, 1, 0), (0, 1, 1)), (fg, fb, fr)),
            ((fb > fg) & (fg > fr), ((0, 0, 1), (0, 1, 1)), (fb, fg, fr))):
        m = cond[:, 0]
        if not m.any():
            continue
        ca, cb = c(*a)[m], c(*b)[m]
        A, B, C = wa[m], wb[m], wc[m]
        out[m] = c000[m] + A * (ca - c000[m]) + B * (cb - ca) + C * (c111[m] - cb)
    left = ~(((fr >= fg) & (fg >= fb)) | ((fr >= fb) & (fb > fg)) | ((fb > fr) & (fr >= fg)) | ((fg > fr) & (fr >= fb))
             | ((fg >= fb) & (fb > fr)) | ((fb > fg) & (fg > fr)))[:, 0]
    assert not left.any()
    return out


def convert(n, v, lo, hi, in_log, out_log, N):
    """the export (input in_log) as a cube of size N whose input is out_log"""
    code = F.grid(N)                                   # out_log codes, R fastest
    lin = F.LOG_INV[out_log](code)                     # scene linear, out gamut
    M = np.linalg.inv(F.npm(F.GAMUT[in_log])) @ F.npm(F.GAMUT[out_log])
    lin_in = lin @ M.T
    x = F.LOG_ENC[in_log](lin_in)                      # in_log codes
    x = (x - lo) / (hi - lo)                           # the export's domain -> 0..1
    return tetra(n, v, x)


def check(n, v, lo, hi, in_log, cubes):
    """codes: the export vs each new cube on 20,000 random in-camera colours (sRGB JPEG values, Rec.709 primaries)"""
    rng = np.random.default_rng(7)
    s = rng.uniform(0, 1, (20000, 3))
    lin709 = F.srgb_eotf(s)
    res = {}
    for log, (N, vals) in cubes.items():
        def through(lg, nn, vv, l0, h0):
            M = np.linalg.inv(F.npm(F.GAMUT[lg])) @ F.NPM709
            x = (F.LOG_ENC[lg](lin709 @ M.T) - l0) / (h0 - l0)
            return tetra(nn, vv, x)
        ref = through(in_log, n, v, lo, hi)
        got = through(log, N, vals, np.zeros(3), np.ones(3))
        d = np.abs(ref - got) * 255.0
        res[log] = (float(d.mean()), float(np.percentile(d, 99.9)), float(d.max()))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cube")
    ap.add_argument("out")
    ap.add_argument("--stem", default="EKTAP")
    ap.add_argument("--in-log", default="vlog", choices=sorted(SUFFIX))
    ap.add_argument("--n", type=int, default=65)
    ap.add_argument("--title", default="EKTA P")
    a = ap.parse_args()
    n, v, lo, hi, t0 = read_cube(a.cube)
    md5 = hashlib.md5(open(a.cube, "rb").read()).hexdigest()
    print("input %s: %d^3, domain %s..%s, title %r, output range %.4f..%.4f, md5 %s" % (
        a.cube, n, lo.tolist(), hi.tolist(), t0, v.min(), v.max(), md5))
    if v.min() < -1e-6 or v.max() > 1 + 1e-6:
        print("  note: samples outside 0..1 (the camera clamps them, its menu says 'clipped')")
    os.makedirs(a.out, exist_ok=True)
    cubes = {}
    for log, suf in SUFFIX.items():
        vals = np.clip(convert(n, v, lo, hi, a.in_log, log, a.n), 0.0, 1.0)
        name = (a.stem + "_" + suf) if len(a.stem) + 1 + len(suf) <= 8 else (a.stem + suf)
        assert len(name) <= 8, name
        title = ("%s %s" % (a.title, F.LOG_MODE[log]))[:23]
        com = ["%s: the owner's own Resolve grade as a Film Roll Film LUT (menu Film LUT, the '%s + LUT' output)." % (
                   a.title, F.LOG_MODE[log]),
               "Input: %s (cube 0..1 = normalised log code). Output: display sRGB, Rec.709 primaries." % F.LOG_NAME[log],
               "Made by luts_film/src/mk_usercube.py (WindyStrife) from his %d^3 %s export (md5 %s): target log code ->" % (
                   n, F.LOG_NAME[a.in_log], md5),
               "scene linear -> XYZ (D65) -> %s -> tetrahedral lookup in the export." % F.LOG_NAME[a.in_log]]
        fn = os.path.join(a.out, name + ".CUB")
        F.write_cube(fn, title, com, a.n, vals)
        cubes[log] = (a.n, vals)
        print("  %-13s %-23s %9d B  md5 %s" % (name + ".CUB", title, os.path.getsize(fn),
                                               hashlib.md5(open(fn, "rb").read()).hexdigest()))
    for log, (mean, p999, mx) in check(n, v, lo, hi, a.in_log, cubes).items():
        print("  check %-6s vs the export on 20,000 in-camera colours: mean %.3f  p99.9 %.2f  max %.2f codes" % (
            log, mean, p999, mx))


if __name__ == "__main__":
    main()
