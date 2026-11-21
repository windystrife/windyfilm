#!/usr/bin/env python3
"""lab_check.py - frames and numbers for tools/lab_check.sh (WindyStrife's own tool; WSL python3 -I -B, numpy + PIL).

  lab_check.py uyvy IN.JPG W H OUT.uyvy      the JPEG's own Y'CbCr (no colour conversion: PIL draft) as the camera's
                                             UYVY frame ({U s8, Y0, V s8, Y1}, chroma = the pair's mean), centred in W x H
  lab_check.py hot DIR N                     the N JPEGs of DIR with the most pixels at or above luma 0.8 (glow sources)
  lab_check.py cmp IN.ppm CAM.ppm REF.ppm    the camera against the reference: per channel mean / max |cam - ref|, PSNR,
                                             the same inside the glow (where the reference moved a pixel by >= 2 levels),
                                             and the glow itself (mean ref - in)
  lab_check.py grain IN.ppm CAM.ppm REF.ppm  grain statistics of cam - in and ref - in: mean, std of luma by zone, the
                                             share of pixels moved, the luma autocorrelation at 1 / 3 / 6 / 12 px; and
                                             of the reference through the camera's UYVY round trip (the camera's grain
                                             is rounded to whole Y / U / V codes, then read back as RGB: that adds
                                             near-white noise, so this is the line the camera's compares with)
"""
import sys

import numpy as np
from PIL import Image


def read_ppm(p):
    with open(p, "rb") as f:
        data = f.read()
    parts, i = [], 0
    while len(parts) < 4:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(data[i + 1:i + 1 + w * h * 3], dtype=np.uint8).reshape(h, w, 3)


def to_uyvy(src, W, H, out):
    im = Image.open(src)
    im.draft("YCbCr", im.size)
    if im.mode != "YCbCr":
        im = im.convert("YCbCr")
    a = np.asarray(im).astype(np.int32)
    h, w = a.shape[:2]
    fr = np.zeros((H, W, 3), np.int32)
    fr[..., 1:] = 128
    y0, x0 = max(0, (H - h) // 2), max(0, (W - w) // 2)
    sy, sx = max(0, (h - H) // 2), max(0, (w - W) // 2)
    hh, ww = min(h, H), min(w, W)
    fr[y0:y0 + hh, x0:x0 + ww] = a[sy:sy + hh, sx:sx + ww]
    Y = fr[..., 0]
    U = (fr[:, 0::2, 1] + fr[:, 1::2, 1] + 1) // 2 - 128
    V = (fr[:, 0::2, 2] + fr[:, 1::2, 2] + 1) // 2 - 128
    q = (U & 255) | (Y[:, 0::2] << 8) | ((V & 255) << 16) | (Y[:, 1::2].astype(np.uint32) << 24)
    q.astype("<u4").tofile(out)


def hot(d, n):
    import os
    r = []
    for f in sorted(os.listdir(d)):
        if not f.upper().endswith(".JPG"):
            continue
        im = Image.open(os.path.join(d, f))
        im.draft("YCbCr", (im.size[0] // 8, im.size[1] // 8))
        y = np.asarray(im.convert("YCbCr"))[..., 0]
        r.append(((y >= 204).mean(), f))
    for s, f in sorted(r, reverse=True)[:n]:
        print(f"{f} {s * 100:.2f}")


def psnr(a, b):
    m = ((a.astype(np.float64) - b) ** 2).mean()
    return 99.0 if m == 0 else 10 * np.log10(255.0 ** 2 / m)


def cmp(pin, pcam, pref):
    i, c, r = (read_ppm(p).astype(np.int32) for p in (pin, pcam, pref))
    d = np.abs(c - r)
    glow = (r - i).max(axis=2) >= 2
    out = [f"all: mean |cam-ref| R {d[..., 0].mean():.3f} G {d[..., 1].mean():.3f} B {d[..., 2].mean():.3f}"
           f" max {d.max()} PSNR {psnr(c, r):.2f} dB"]
    if glow.any():
        dg = d[glow]
        eg = (r - i)[glow]
        out.append(f"glow ({glow.mean() * 100:.1f} % of px): mean |cam-ref| {dg.mean():.3f} p99 {np.percentile(dg, 99):.0f}"
                   f" max {dg.max()} | glow ref-in mean {eg.mean():.2f} cam-in {(c - i)[glow].mean():.2f}"
                   f" PSNR(ref,in) {psnr(r, i):.2f} dB")
    print(" | ".join(out))


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def ac(d, lag):
    a = d[:, :-lag].ravel()
    b = d[:, lag:].ravel()
    a = a - a.mean()
    b = b - b.mean()
    s = np.sqrt((a * a).mean() * (b * b).mean())
    return 0.0 if s == 0 else (a * b).mean() / s


def via_uyvy(a):
    """RGB -> full-range BT.601 4:2:2 (chroma = the pair's mean), whole codes -> RGB as tools/lab_check.c reads it"""
    R, G, B = a[..., 0], a[..., 1], a[..., 2]
    Y = np.clip(np.floor(0.299 * R + 0.587 * G + 0.114 * B + 0.5), 0, 255).astype(np.int64)
    cb = -0.168736 * R - 0.331264 * G + 0.5 * B
    cr = 0.5 * R - 0.418688 * G - 0.081312 * B
    U = np.repeat(np.clip(np.floor((cb[:, 0::2] + cb[:, 1::2]) / 2 + 0.5), -128, 127).astype(np.int64), 2, axis=1)
    V = np.repeat(np.clip(np.floor((cr[:, 0::2] + cr[:, 1::2]) / 2 + 0.5), -128, 127).astype(np.int64), 2, axis=1)
    rv, gu, bu = (359 * V) >> 8, (88 * U + 183 * V) >> 8, (454 * U) >> 8
    return np.stack([np.clip(Y + rv, 0, 255), np.clip(Y - gu, 0, 255), np.clip(Y + bu, 0, 255)], -1).astype(np.float64)


def grain(pin, pcam, pref):
    i, c, r = (read_ppm(p).astype(np.float64) for p in (pin, pcam, pref))
    L = luma(i) / 255
    for nm, o in (("cam", c), ("ref", r), ("ref via UYVY", via_uyvy(r))):
        d = luma(o) - luma(i)
        z = [d[(L < 0.25)].std(), d[(L > 0.35) & (L < 0.6)].std(), d[(L > 0.75) & (L < 0.98)].std()]
        moved = (np.abs(o - i).max(axis=2) > 0).mean()
        print(f"{nm}: mean {d.mean():+.3f} std shadows {z[0]:.3f} mid {z[1]:.3f} high {z[2]:.3f} moved {moved * 100:.1f} %"
              f" ac1 {ac(d, 1):.2f} ac3 {ac(d, 3):.2f} ac6 {ac(d, 6):.2f} ac12 {ac(d, 12):.2f}")


if __name__ == "__main__":
    a = sys.argv
    if len(a) >= 6 and a[1] == "uyvy":
        to_uyvy(a[2], int(a[3]), int(a[4]), a[5])
    elif len(a) >= 4 and a[1] == "hot":
        hot(a[2], int(a[3]))
    elif len(a) >= 5 and a[1] == "cmp":
        cmp(a[2], a[3], a[4])
    elif len(a) >= 5 and a[1] == "grain":
        grain(a[2], a[3], a[4])
    else:
        sys.exit(__doc__)
