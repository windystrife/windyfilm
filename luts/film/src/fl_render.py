#!/usr/bin/env python3
"""fl_render.py CUBEDIR OUTDIR [--work DIR] - renders + contact sheets for the film cubes.

Photos: the five judge frames of the Log + LUT accuracy test (WORK/photos.uyvy, 1296 x 864 each, the
camera's Y'CbCr L-JPEG layout) run through the CAMERA's own path: lltest_x86 build (fr_loglut.c,
the module's loader / composite builder) + apply (fr_ll_words, the shot kernel). So the pictures are
what the camera writes, cube interpolation and 8-bit tables included.
Chart: synthetic patches (grey steps, ColorChecker-like, skin tones, hue sweeps, ramps) encoded in
the mode's log curve and graded in float with the cube (tetrahedral, as the camera). The bottom
ramp row runs past the camera's range (linear 1.0 = +2.47 EV) to show the cube's roll-off to +5 EV.
Columns: Canon JPEG (input) | module technical log->709 cube | the film cubes.
"""
import argparse
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.dont_write_bytecode = True        # never leave __pycache__ in the (read-only) repo worktree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fl_core as F  # noqa: E402

REPO_LL = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "proto", "loglut")
# lltest_x86 built from the module's current sources by ../mk_lltest.sh (fl_camerr.py docstring); the older
# WSL copy gives bit-identical tables and frames (checked on V250D_S3)
X86 = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "lltest", "lltest_x86")
if not os.path.exists(X86):
    X86 = "/root/ml/loglut/lltest_x86"
PHOTOS = os.environ.get("FL_PHOTOS", "/root/ml/loglut/photos.uyvy")   # proto/loglut/mkframes.py makes it
PW, PH, NPH = 1296, 864, 5
MODES = {"slog3": 1, "vlog": 2, "logc3": 3}
TH_W = 432

CC24 = [(115, 82, 68), (194, 150, 130), (98, 122, 157), (87, 108, 67), (133, 128, 177), (103, 189, 170),
        (214, 126, 44), (80, 91, 166), (193, 90, 99), (94, 60, 108), (157, 188, 64), (224, 163, 46),
        (56, 61, 150), (70, 148, 73), (175, 54, 60), (231, 199, 31), (187, 86, 149), (8, 133, 161),
        (243, 243, 242), (200, 200, 200), (160, 160, 160), (122, 122, 121), (85, 85, 85), (52, 52, 52)]
SKIN = [(255, 224, 196), (241, 194, 167), (224, 172, 138), (198, 134, 103), (161, 102, 75), (120, 76, 56), (87, 55, 40)]


def read_uyvy_rgb(fn, w, h):
    a = np.fromfile(fn, np.uint8).reshape(h, w // 2, 4)
    Y = np.empty((h, w))
    Y[:, 0::2] = a[..., 1]
    Y[:, 1::2] = a[..., 3]
    cb = np.repeat(a[..., 0].view(np.int8).astype(float), 2, 1)
    cr = np.repeat(a[..., 2].view(np.int8).astype(float), 2, 1)
    rgb = np.stack([Y + 1.402 * cr, Y - 0.344136 * cb - 0.714136 * cr, Y + 1.772 * cb], -1)
    return np.clip(np.round(rgb), 0, 255).astype(np.uint8)


def chart_linear():
    """camera-side linear Rec.709 image (float, rows of patches), plus patch boxes for checks"""
    W, ph = 1296, 96
    rows = []
    boxes = {}
    # row 0: grey steps -6..+2 EV (camera range) and the camera white
    evs = [-6, -5, -4, -3, -2, -1, -0.5, 0, 0.5, 1, 1.5, 2, 2.47]
    r = np.zeros((ph, W, 3))
    pw = W // len(evs)
    for i, e in enumerate(evs):
        r[:, i * pw:(i + 1) * pw] = min(0.18 * 2.0 ** e, 1.0)
    rows.append(r)
    # rows 1-2: ColorChecker-like (sRGB codes -> linear)
    for k in range(2):
        r = np.zeros((ph, W, 3))
        pw = W // 12
        for i in range(12):
            r[:, i * pw:(i + 1) * pw] = F.srgb_eotf(np.array(CC24[k * 12 + i]) / 255.0)
        rows.append(r)
    # row 3: skin tones + a smooth skin ramp
    r = np.zeros((ph, W, 3))
    pw = 96
    for i, c in enumerate(SKIN):
        r[:, i * pw:(i + 1) * pw] = F.srgb_eotf(np.array(c) / 255.0)
    base = F.srgb_eotf(np.array(SKIN[2]) / 255.0)
    t = np.linspace(-3.0, 2.0, W - 7 * pw)
    r[:, 7 * pw:] = np.minimum(base[None, :] * 2.0 ** t[:, None], 1.0)[None]
    rows.append(r)
    # rows 4-5: hue sweeps, S = 0.9 / 0.5, V = 0.85
    for s in (0.9, 0.5):
        h = np.linspace(0, 6, W, endpoint=False)
        c = np.clip(np.abs(((h[:, None] + np.array([0.0, 4.0, 2.0])) % 6.0) - 3.0) - 1.0, 0, 1)
        col = 0.85 * (1 - s + s * c)
        rows.append(np.repeat(F.srgb_eotf(col)[None], ph, 0))
    # row 6: smooth grey ramp, camera range (linear 0 .. 1)
    t = np.linspace(0.0, 1.0, W)
    rows.append(np.repeat(np.repeat(F.srgb_eotf(t)[None, :, None], 3, 2), ph // 2, 0))
    # row 7: EXTENDED ramp -8 .. +5 EV (beyond the camera's 1.0 at +2.47 EV)
    t = 0.18 * 2.0 ** np.linspace(-8.0, 5.0, W)
    rows.append(np.repeat(np.repeat(t[None, :, None], 3, 2), ph // 2, 0))
    return np.concatenate(rows, 0)


def to_log(lin709, log, clip=True):
    m = np.linalg.inv(F.npm(F.GAMUT[log])) @ F.NPM709
    x = lin709 @ m.T
    if clip:
        x = np.clip(x, 0.0, 1.0)
    return F.LOG_ENC[log](np.maximum(x, 0.0))


def chart_log(log):
    lin = chart_linear()
    H = lin.shape[0]
    code = to_log(lin, log)
    ext = to_log(lin[H - 48:], log, clip=False)          # last row: no camera clip
    code[H - 48:] = ext
    return lin, code


def grade_chart(cube, log):
    lin, code = chart_log(log)
    out = F.cube_apply(cube, code.reshape(-1, 3)).reshape(code.shape)
    return np.clip(np.round(out * 255), 0, 255).astype(np.uint8)


def canon_chart():
    lin = chart_linear()
    return np.clip(np.round(F.srgb_oetf(np.minimum(lin, 1.0)) * 255), 0, 255).astype(np.uint8)


def camera_photos(cube_path, mode, tmp):
    tabs = os.path.join(tmp, "t.tabs")
    r = subprocess.run([X86, "build", cube_path, str(mode), tabs], capture_output=True, text=True)
    info = r.stdout.strip()
    if r.returncode != 0:
        raise RuntimeError("lltest build failed: %s %s" % (cube_path, info))
    out = os.path.join(tmp, "o.uyvy")
    subprocess.run([X86, "apply", tabs, PHOTOS, out], check=True)
    rgb = read_uyvy_rgb(out, PW, PH * NPH)
    os.remove(out)
    os.remove(tabs)
    return rgb, info


def thumb(a, w=TH_W):
    im = Image.fromarray(a)
    return im.resize((w, int(round(a.shape[0] * w / a.shape[1]))), Image.LANCZOS)


def sheet(cols, labels, title, fn):
    """cols: list of lists of PIL images (one per row)"""
    nr = len(cols[0])
    rh = [max(c[i].size[1] for c in cols) for i in range(nr)]
    W = TH_W * len(cols) + 4 * (len(cols) + 1)
    H = 40 + sum(rh) + 4 * (nr + 1) + 22
    S = Image.new("RGB", (W, H), (40, 40, 40))
    d = ImageDraw.Draw(S)
    d.text((6, 4), title, fill=(230, 230, 230))
    for j, l in enumerate(labels):
        d.text((4 + j * (TH_W + 4) + 2, 22), l, fill=(255, 220, 120))
    y = 40
    for i in range(nr):
        for j, c in enumerate(cols):
            S.paste(c[i], (4 + j * (TH_W + 4), y))
        y += rh[i] + 4
    d.text((6, H - 18), "film data: spektrafilm profiles, CC BY-SA 4.0, A. Volpato  |  photos: camera path (lltest_x86 build+apply)  |  chart bottom row: -8..+5 EV, beyond the camera's +2.47 EV", fill=(180, 180, 180))
    S.save(fn, quality=90)
    return fn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cubes")
    ap.add_argument("out")
    ap.add_argument("--stocks", default="V250D,V500T,PRT400,FP400H")
    ap.add_argument("--logs", default="slog3,vlog")
    ap.add_argument("--tag", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tmp = os.path.join(a.out, "tmp")
    os.makedirs(tmp, exist_ok=True)
    sys.path.insert(0, REPO_LL)
    import mk_testluts as MT                                  # the module's own technical cube
    import mk_filmluts as MF
    canon = read_uyvy_rgb(PHOTOS, PW, PH * NPH)
    for log in a.logs.split(","):
        mode = MODES[log]
        tech = os.path.join(tmp, "TECH_%s.cube" % log)
        MT.write(tech, ['TITLE "tech %s"' % log, "LUT_3D_SIZE 33"], MT.lines_of(MT.tech(mode - 1, MT.grid(33))))
        cols, labels = [], []
        # Canon column
        c = [thumb(canon[i * PH:(i + 1) * PH]) for i in range(NPH)] + [thumb(canon_chart())]
        cols.append(c)
        labels.append("Canon JPEG (input)")
        entries = [("tech", tech, "module tech %s->709" % log)] + \
                  [(k, os.path.join(a.cubes, MF.card_name(k, log)), MF.card_name(k, log)) for k in a.stocks.split(",")]
        for key, path, lab in entries:
            if not os.path.exists(path):
                continue
            rgb, info = camera_photos(path, mode, tmp)
            print(log, os.path.basename(path), "|", info, flush=True)
            cube = F.read_cube(path)
            ch = grade_chart(cube, log)
            Image.fromarray(ch).save(os.path.join(a.out, "chart_%s_%s.png" % (log, os.path.basename(path).split(".")[0])))
            Image.fromarray(rgb[2 * PH:3 * PH]).save(os.path.join(a.out, "sunset_%s_%s.jpg" % (log, os.path.basename(path).split(".")[0])), quality=92)
            Image.fromarray(rgb[3 * PH:4 * PH]).save(os.path.join(a.out, "card_%s_%s.png" % (log, os.path.basename(path).split(".")[0])))
            cols.append([thumb(rgb[i * PH:(i + 1) * PH]) for i in range(NPH)] + [thumb(ch)])
            labels.append(lab)
        fn = sheet(cols, labels, "Film Roll film LUTs %s - %s mode (camera path)" % (a.tag, log),
                   os.path.join(a.out, "contact_%s%s.jpg" % (log, ("_" + a.tag) if a.tag else "")))
        print("wrote", fn, flush=True)
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)


if __name__ == "__main__":
    main()
