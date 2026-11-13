#!/usr/bin/env python3
"""fl_camerr.py CUBES33 CUBES65 OUTDIR [--ll LLTEST_X86] - film cube error THROUGH THE CAMERA PATH.

The camera never evaluates the .cube per pixel: fr_loglut.c (lltest_x86 build) resamples the cube
(tetrahedral) onto its own Y'CbCr table, 33 luma levels x 33 x 33 chroma nodes (8 codes apart),
and fr_ll_words (lltest_x86 apply) interpolates that table per pixel pair in integers (4:2:2).
So the error of a shot against the exact film model has three parts:
  cube   the .cube itself: cube (float, tetrahedral) vs exact model, per pixel (size-dependent)
  grid   the camera's node grid + 8-bit integer kernel: camera vs the float cube (llref's 'cam')
  total  camera vs exact model (what the JPEG shows)
plus the FLOOR: the camera kernel (simk.kernel, bit-exact emulation of fr_ll_words, checked against
the C output here) with tables built from the EXACT model at the camera's nodes = what an infinitely
fine cube would give through this camera path.
Reference (as proto/loglut/llref.py): per pixel Y'CbCr -> R'G'B' (clamped) -> sRGB EOTF -> 3x3 ->
log formula -> exact film model (fl_core.FilmModel, float) -> Y'CbCr; chroma = mean of the pair's two
pixels (4:2:2), rounded. Errors in 8-bit Y'CbCr codes: mean / p99.9 / max, Y over luma samples, C
over Cb + Cr samples.
Frames: photos.uyvy (the five judge photos, 1296 x 864 each) and rand20k.uyvy (the 20,000 random
sRGB colours of fl_check.py, seed 5, each as a pixel pair of one colour, 1000 x 40).
Writes OUTDIR/camerr.json, camerr.txt and, for V250D_S3, the camera frames + reference used by
fl_cropsheet.py.
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fl_core as F  # noqa: E402
import mk_filmluts as MF  # noqa: E402

REPO_LL = os.path.join(os.path.dirname(os.path.dirname(HERE)), "proto", "loglut")   # this repo's proto/loglut
sys.path.insert(0, REPO_LL)
import llref as LR  # noqa: E402
import simk as SK  # noqa: E402

PHOTOS = os.environ.get("FL_PHOTOS", "/root/ml/loglut/photos.uyvy")   # proto/loglut/mkframes.py makes it
CA, LA = SK.chroma_axis("c33"), SK.luma_axis("l33")       # the camera's table axes (fr_llk.c)


def exact_fn(model, mode):
    def fn(Y, Cb, Cr):
        rgb = np.clip(LR.ycc2rgb(Y, Cb, Cr), 0, 255) / 255.0
        lt = LR.srgb_eotf(rgb) @ LR.M709[mode].T
        code = LR.LOGF[mode](np.clip(lt, 0, 1))
        return model(code) @ LR.KY.T
    return fn


def per_frame_u(fn, Y, cb, cr):
    """fn per pixel (chroma duplicated), evaluated once per distinct (Y, Cb, Cr) ->
    (Y, Cb pair mean, Cr pair mean), per-pixel output (h, w, 3)"""
    h, w = Y.shape
    Yp = Y.reshape(-1).astype(np.int64)
    Cbp = np.repeat(cb, 2, axis=1).reshape(-1).astype(np.int64) + 128
    Crp = np.repeat(cr, 2, axis=1).reshape(-1).astype(np.int64) + 128
    u, inv = np.unique((Yp << 16) | (Cbp << 8) | Crp, return_inverse=True)
    o = fn((u >> 16).astype(float), ((u >> 8) & 255).astype(float) - 128, (u & 255).astype(float) - 128)
    o = o[inv.reshape(-1)].reshape(h, w, 3)
    return (o[..., 0], (o[:, 0::2, 1] + o[:, 1::2, 1]) / 2, (o[:, 0::2, 2] + o[:, 1::2, 2]) / 2), o


def tables_exact(fn):
    """camera tables (N, Nl, Yn, U, V) from the exact model, rounded like fr_loglut.c (simk.tables)"""
    oN = fn(np.arange(256.0), np.zeros(256), np.zeros(256))
    N = np.clip(np.floor(oN[:, 0] + 0.5), 0, 255).astype(np.int64)
    l, b, c = np.meshgrid(np.arange(LA.n), np.arange(CA.n), np.arange(CA.n), indexing="ij")
    o = fn(LA.nodes[l].ravel().astype(float), (CA.nodes[b] - 128).ravel().astype(float),
           (CA.nodes[c] - 128).ravel().astype(float))
    sh = (LA.n, CA.n, CA.n)
    Yn = np.clip(np.floor(o[:, 0] + 0.5), 0, 255).astype(np.int64).reshape(sh)
    U = (np.clip(np.floor(o[:, 1] + 0.5), -128, 127).astype(np.int64) + 128).reshape(sh)
    V = (np.clip(np.floor(o[:, 2] + 0.5), -128, 127).astype(np.int64) + 128).reshape(sh)
    return N, N[np.minimum(LA.nodes, 255)], Yn, U, V


def rand_frame(fn):
    rng = np.random.default_rng(5)                       # the same colours as fl_check.py's interp check
    s = rng.uniform(0, 1, (20000, 3))
    ycc = s @ LR.KY.T
    Y = np.clip(np.floor(ycc[:, 0] + 0.5), 0, 255).astype(np.uint8)
    cb = np.clip(np.floor(ycc[:, 1] + 0.5), -128, 127).astype(np.int8).view(np.uint8)
    cr = np.clip(np.floor(ycc[:, 2] + 0.5), -128, 127).astype(np.int8).view(np.uint8)
    a = np.stack([cb, Y, cr, Y], -1).reshape(40, 500, 4)
    a.tofile(fn)
    return 1000, 40


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("c33")
    ap.add_argument("c65")
    ap.add_argument("out")
    ap.add_argument("--ll", default=os.path.join(os.path.dirname(HERE), "lltest", "lltest_x86"))
    ap.add_argument("--stocks", default="V250D,V500T,PRT400,FP400H")
    ap.add_argument("--logs", default="slog3,vlog,logc3")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tmp = os.path.join(a.out, "tmp")
    os.makedirs(tmp, exist_ok=True)
    rnd_fn = os.path.join(a.out, "rand20k.uyvy")
    frames = {"photos": (PHOTOS, 1296, 4320), "rand": (rnd_fn, *rand_frame(rnd_fn))}
    inputs = {fr: LR.read_uyvy(p, w, h) for fr, (p, w, h) in frames.items()}
    prm65 = json.load(open(os.path.join(a.c65, "params.json")))
    res, lines = {}, []
    for log in a.logs.split(","):
        mode = LR.MODES.index(log)
        for k in a.stocks.split(","):
            name = MF.card_name(k, log)
            pre, stem, neg, prt, path, extra, desc = MF.STOCKS[k]
            kw = dict(extra)
            p = prm65[name]
            kw.update({kk: p[kk] for kk in ("exposure_ev", "interlayer", "neutral_axis", "grey_target", "gain", "flare") if p.get(kk) is not None})
            fn = exact_fn(F.FilmModel(log, neg, prt, path, **kw), mode)
            Tex = tables_exact(fn)
            R = {}
            for fr, (fpath, w, h) in frames.items():
                Y, cb, cr = inputs[fr]
                ref, refpix = per_frame_u(fn, Y, cb, cr)
                floor, _ = SK.kernel(Tex, CA, LA, "linx", 8, Y, cb, cr)
                G = {"floor": LR.errs(floor, ref)}
                keep = (k == "V250D" and log == "slog3" and fr == "photos")
                if keep:
                    np.save(os.path.join(a.out, "V250D_S3_refpix.npy"), refpix.astype(np.float32))
                    np.save(os.path.join(a.out, "V250D_S3_ref422.npy"), np.stack([ref[0], np.repeat(ref[1], 2, 1), np.repeat(ref[2], 2, 1)]).astype(np.float32))
                for n, d in ((33, a.c33), (65, a.c65)):
                    cfn = os.path.join(d, name)
                    cube = LR.parse_cube(cfn)
                    assert cube[0] == n, (cfn, cube[0])
                    tabs = os.path.join(tmp, "t.tabs")
                    r = subprocess.run([a.ll, "build", cfn, str(mode + 1), tabs], capture_output=True, text=True)
                    if r.returncode != 0:
                        raise RuntimeError("build failed %s: %s" % (cfn, r.stdout))
                    camf = os.path.join(tmp, "o.uyvy")
                    subprocess.run([a.ll, "apply", tabs, fpath, camf], check=True)
                    cam = LR.read_uyvy(camf, w, h)
                    tN, _, tY, tU, tV = LR.read_tabs(tabs)      # as llref.main: level N values for the kernel
                    sim, _ = SK.kernel((tN, tN[np.minimum(LA.nodes, 255)], tY, tU, tV), CA, LA, "linx", 8, Y, cb, cr)
                    simok = all(np.array_equal(sim[i], cam[i]) for i in range(3))
                    cf, _ = per_frame_u(lambda y, b, c: LR.composite(mode, cube, y, b, c), Y, cb, cr)
                    G[str(n)] = {"total": LR.errs(cam, ref), "cube": LR.errs(LR.rnd(cf), ref), "grid": LR.errs(cam, cf),
                                 "sim_equals_c": int(simok), "build": r.stdout.strip()}
                    if keep:
                        os.replace(camf, os.path.join(a.out, "V250D_S3_cam%d.uyvy" % n))
                    else:
                        os.remove(camf)
                    os.remove(tabs)
                R[fr] = G
            res[name] = R
            for fr in frames:
                G = R[fr]
                ln = "%-13s %-6s | 33 total Y %s C %s (cube Y %s C %s) | 65 total Y %s C %s (cube Y %s C %s) | floor Y %s C %s | sim==C %d%d" % (
                    name, fr, G["33"]["total"]["Y"], G["33"]["total"]["C"], G["33"]["cube"]["Y"], G["33"]["cube"]["C"],
                    G["65"]["total"]["Y"], G["65"]["total"]["C"], G["65"]["cube"]["Y"], G["65"]["cube"]["C"],
                    G["floor"]["Y"], G["floor"]["C"], G["33"]["sim_equals_c"], G["65"]["sim_equals_c"])
                print(ln, flush=True)
                lines.append(ln)
            json.dump(res, open(os.path.join(a.out, "camerr.json"), "w"), indent=1)
    open(os.path.join(a.out, "camerr.txt"), "w").write("\n".join(lines) + "\n")
    os.rmdir(tmp)


if __name__ == "__main__":
    main()
