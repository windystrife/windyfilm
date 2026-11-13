#!/usr/bin/env python3
"""fl_cropsheet.py CAMPATH OUT.jpg - V250D_S3 33^3 vs 65^3 through the camera path, two photos at
full width plus 3x crops where the 33^3 error is largest (the most saturated areas).

Inputs from fl_camerr.py: CAMPATH/V250D_S3_cam33.uyvy, _cam65.uyvy (lltest_x86 build + apply on
photos.uyvy), _ref422.npy (exact film model, 4:2:2 chroma = the best this output format can hold),
_refpix.npy (exact model per pixel, for the 'exact' tile).
Error = max over R, G, B of |camera - exact 4:2:2|, decoded to 8-bit R'G'B' (JFIF).
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

PW, PH = 1296, 864
NAMES = ["lake", "scene", "sunset", "card", "chart"]
CW, CH, Z = 170, 113, 3          # crop size and zoom
EGAIN = 16


def ycc2rgb(Y, Cb, Cr):
    return np.stack([Y + 1.402 * Cr, Y - 0.344136 * Cb - 0.714136 * Cr, Y + 1.772 * Cb], -1)


def read_uyvy(fn, w, h):
    a = np.fromfile(fn, np.uint8).reshape(h, w // 2, 4)
    Y = np.empty((h, w))
    Y[:, 0::2] = a[..., 1]
    Y[:, 1::2] = a[..., 3]
    cb = np.repeat(a[..., 0].view(np.int8).astype(float), 2, 1)
    cr = np.repeat(a[..., 2].view(np.int8).astype(float), 2, 1)
    return Y, cb, cr


def to8(rgb):
    return np.clip(np.floor(rgb + 0.5), 0, 255)


def box_mean(e, h, w):
    c = np.pad(e, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    return (c[h:, w:] - c[:-h, w:] - c[h:, :-w] + c[:-h, :-w]) / (h * w)


def font(sz):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if os.path.exists(f):
            return ImageFont.truetype(f, sz)
    try:
        return ImageFont.load_default(size=sz)
    except TypeError:
        return ImageFont.load_default()


def main():
    cp, out = sys.argv[1], sys.argv[2]
    H = PH * 5
    cam = {n: to8(ycc2rgb(*read_uyvy(os.path.join(cp, "V250D_S3_cam%d.uyvy" % n), PW, H))) for n in (33, 65)}
    r4 = np.load(os.path.join(cp, "V250D_S3_ref422.npy")).astype(np.float64)
    ref = to8(ycc2rgb(np.floor(r4[0] + 0.5), np.floor(r4[1] + 0.5), np.floor(r4[2] + 0.5)))
    rp = np.load(os.path.join(cp, "V250D_S3_refpix.npy")).astype(np.float64)
    exact = to8(ycc2rgb(rp[..., 0], rp[..., 1], rp[..., 2]))
    err = {n: np.abs(cam[n] - ref).max(-1) for n in (33, 65)}
    # pick the two photos with the largest 33^3 crop-mean error, and the crop in each
    cand = []
    for i in range(5):
        sl = slice(i * PH, (i + 1) * PH)
        bm = box_mean(err[33][sl], CH, CW)
        y, x = np.unravel_index(np.argmax(bm), bm.shape)
        cand.append((bm[y, x], i, y, x))
    picks = sorted(sorted(cand, reverse=True)[:2], key=lambda t: t[1])
    f1, f2, f3 = font(26), font(20), font(17)
    G = 8
    TW = Z * CW
    W = 2 * PW + 3 * G
    blockh = 40 + PH + G + 28 + Z * CH + G
    S = Image.new("RGB", (W, 50 + 2 * blockh + 40), (36, 36, 36))
    d = ImageDraw.Draw(S)
    d.text((G, 12), "V250D_S3.CUB through the camera path (lltest_x86 build + apply): 33^3 vs 65^3", font=f1, fill=(235, 235, 235))
    y0 = 50
    report = []
    for score, i, cy, cx in picks:
        sl = slice(i * PH, (i + 1) * PH)
        st = {}
        for n in (33, 65):
            e = err[n][sl]
            st[n] = (e.mean(), np.percentile(e, 99.9), e.max())
        # chroma of the exact image in the crop (CIELAB-free proxy: Cb/Cr radius, codes)
        cro = np.hypot(rp[sl][cy:cy + CH, cx:cx + CW, 1], rp[sl][cy:cy + CH, cx:cx + CW, 2]).mean()
        hdr = ("photo %d '%s'  |  RGB error vs exact (whole photo, mean / p99.9 / max): 33^3 %.2f / %.0f / %.0f   65^3 %.2f / %.0f / %.0f"
               "  |  crop x %d y %d %dx%d, mean chroma %.0f codes" % (i + 1, NAMES[i], *st[33], *st[65], cx, cy, CW, CH, cro))
        report.append(hdr)
        d.text((G, y0 + 8), hdr, font=f2, fill=(255, 220, 120))
        y = y0 + 40
        for j, n in enumerate((33, 65)):
            im = Image.fromarray(cam[n][sl].astype(np.uint8))
            dd = ImageDraw.Draw(im)
            dd.rectangle([cx - 2, cy - 2, cx + CW + 1, cy + CH + 1], outline=(255, 255, 0), width=2)
            dd.text((10, 8), "%d^3" % n, font=f1, fill=(255, 255, 0))
            S.paste(im, (G + j * (PW + G), y))
        y += PH + G
        tiles = [("camera 33^3", cam[33]), ("camera 65^3", cam[65]), ("exact model (per pixel)", exact),
                 ("|33^3 - exact| x%d" % EGAIN, None), ("|65^3 - exact| x%d" % EGAIN, None)]
        gap = (W - 5 * TW) // 6
        for j, (lab, img) in enumerate(tiles):
            if img is None:
                n = 33 if j == 3 else 65
                e = err[n][sl][cy:cy + CH, cx:cx + CW]
                lab += "  (crop mean %.2f, max %.0f)" % (e.mean(), e.max())
                t = np.repeat(np.clip(e * EGAIN, 0, 255)[..., None], 3, -1)
            else:
                t = img[sl][cy:cy + CH, cx:cx + CW]
            t = np.repeat(np.repeat(t, Z, 0), Z, 1).astype(np.uint8)
            xx = gap + j * (TW + gap)
            d.text((xx, y + 4), lab, font=f3, fill=(230, 230, 230))
            S.paste(Image.fromarray(t), (xx, y + 28))
        y0 += blockh
    d.text((G, S.size[1] - 30), "error = max over R,G,B of |camera - exact film model with 4:2:2 chroma|, 8-bit codes; crops: nearest-neighbour x3 of the window "
           "with the largest 33^3 error.  film data: spektrafilm profiles, CC BY-SA 4.0, A. Volpato", font=f3, fill=(170, 170, 170))
    S.save(out, quality=92)
    print("\n".join(report))
    print("wrote", out, S.size)


if __name__ == "__main__":
    main()
