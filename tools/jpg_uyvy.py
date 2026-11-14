#!/usr/bin/env python3
"""jpg_uyvy.py - a camera JPEG as the camera's UYVY 4:2:2 frame, aligned to the raw window (WindyStrife).

  jpg_uyvy.py IN.JPG OUTDIR [X0 Y0]
    reads OUTDIR/raw.r14 + OUTDIR/meta.txt (tools/cr2_dec.c), decodes the JPEG to Y'CbCr without a colour conversion
    (PIL draft), finds the JPEG's offset to the raw window at (X0, Y0) (default 84, 64: where the EOS M JPEG L sits in
    its CR2) by phase correlation of the raw's green (quad resolution, log) against the JPEG's Y (2 x 2 means, its sRGB
    decode, log), shifts the JPEG by it, and writes OUTDIR/jpeg.uyvy ({U s8, Y0, V s8, Y1}, chroma = the mean of each
    pixel pair) and OUTDIR/align.txt (dx dy peak w h). Also uyvy2png: jpg_uyvy.py --png IN.uyvy W H OUT.png [scale].
Needs numpy and PIL (WSL python3 -I -B).
"""
import sys

import numpy as np
from PIL import Image


def unpack14(buf, w, h, pitch):
    a = np.frombuffer(buf, dtype="<u2", count=h * pitch // 2).reshape(h, pitch // 2)[:, : w // 8 * 7]
    g = a.reshape(h, w // 8, 7).astype(np.uint32)
    h0, h1, h2, h3, h4, h5, h6 = (g[..., i] for i in range(7))
    out = np.empty((h, w // 8, 8), dtype=np.uint16)
    out[..., 0] = h0 >> 2
    out[..., 1] = ((h0 & 3) << 12) | (h1 >> 4)
    out[..., 2] = ((h1 & 15) << 10) | (h2 >> 6)
    out[..., 3] = ((h2 & 63) << 8) | (h3 >> 8)
    out[..., 4] = ((h3 & 255) << 6) | (h4 >> 10)
    out[..., 5] = ((h4 & 1023) << 4) | (h5 >> 12)
    out[..., 6] = ((h5 & 4095) << 2) | (h6 >> 14)
    out[..., 7] = h6 & 16383
    return out.reshape(h, w)


def srgb_eotf(v):
    v = np.clip(v, 0, 1)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def to_uyvy(Y, Cb, Cr):
    h, w = Y.shape
    U = np.round((Cb[:, 0::2] + Cb[:, 1::2]) / 2 - 128).clip(-128, 127).astype(np.int16)
    V = np.round((Cr[:, 0::2] + Cr[:, 1::2]) / 2 - 128).clip(-128, 127).astype(np.int16)
    out = np.empty((h, w // 2, 4), dtype=np.uint8)
    out[..., 0] = (U & 255).astype(np.uint8)
    out[..., 1] = Y[:, 0::2].round().clip(0, 255).astype(np.uint8)
    out[..., 2] = (V & 255).astype(np.uint8)
    out[..., 3] = Y[:, 1::2].round().clip(0, 255).astype(np.uint8)
    return out


def uyvy2png(path, w, h, out, scale=1):
    a = np.fromfile(path, dtype=np.uint8, count=w * h * 2).reshape(h, w // 2, 4).astype(np.float64)
    U = a[..., 0].copy(); U[U > 127] -= 256
    V = a[..., 2].copy(); V[V > 127] -= 256
    Y = np.empty((h, w)); Y[:, 0::2] = a[..., 1]; Y[:, 1::2] = a[..., 3]
    Uf = np.repeat(U, 2, axis=1); Vf = np.repeat(V, 2, axis=1)
    rgb = np.stack([Y + 1.402 * Vf, Y - 0.344136 * Uf - 0.714136 * Vf, Y + 1.772 * Uf], axis=-1)
    im = Image.fromarray(rgb.clip(0, 255).astype(np.uint8))
    if scale != 1:
        im = im.resize((w // scale, h // scale), Image.LANCZOS)
    im.save(out)


def main():
    if sys.argv[1] == "--png":
        uyvy2png(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], int(sys.argv[6]) if len(sys.argv) > 6 else 1)
        return
    jpg, d = sys.argv[1], sys.argv[2]
    x0, y0 = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (84, 64)
    W, H, pitch, iso, black = open(d + "/meta.txt").read().split()[:5]
    W, H, pitch, black = int(W), int(H), int(pitch), float(black)
    raw = unpack14(open(d + "/raw.r14", "rb").read(), W, H, pitch).astype(np.float64) - black
    im = Image.open(jpg)
    im.draft("YCbCr", im.size)
    if im.mode != "YCbCr":
        im = im.convert("YCbCr")
    ycc = np.asarray(im).astype(np.float64)
    jh, jw = ycc.shape[:2]
    # quad-resolution green of the raw window (RGGB at the window corner) and the JPEG's linear luma, 2 x 2 means
    qh, qw = jh // 2, jw // 2
    win = raw[y0 : y0 + 2 * qh, x0 : x0 + 2 * qw]
    G = (win[0::2, 1::2] + win[1::2, 0::2]) / 2
    Yl = srgb_eotf(ycc[: 2 * qh, : 2 * qw, 0] / 255)
    Yq = (Yl[0::2, 0::2] + Yl[0::2, 1::2] + Yl[1::2, 0::2] + Yl[1::2, 1::2]) / 4
    cy, cx, n = qh // 2, qw // 2, 512
    A = np.log(np.maximum(G[cy - n : cy + n, cx - n : cx + n], 1.0))
    B = np.log(np.maximum(Yq[cy - n : cy + n, cx - n : cx + n], 1e-4))
    A -= A.mean(); B -= B.mean()
    win2 = np.outer(np.hanning(2 * n), np.hanning(2 * n))
    R = np.fft.fft2(A * win2) * np.conj(np.fft.fft2(B * win2))
    r = np.fft.ifft2(R / np.maximum(np.abs(R), 1e-12)).real
    py, px = np.unravel_index(np.argmax(r), r.shape)
    peak = r[py, px]
    dy = py if py < n else py - 2 * n
    dx = px if px < n else px - 2 * n
    # raw quad (i, j) ~ JPEG quad (i - dx, j - dy): the JPEG pixel X shows raw window pixel X + 2 dx
    sx, sy = 2 * int(dx), 2 * int(dy)
    shifted = np.empty_like(ycc)
    ys = np.clip(np.arange(jh) - sy, 0, jh - 1)
    xs = np.clip(np.arange(jw) - sx, 0, jw - 1)
    shifted[:] = ycc[ys][:, xs]
    to_uyvy(shifted[..., 0], shifted[..., 1], shifted[..., 2]).tofile(d + "/jpeg.uyvy")
    open(d + "/align.txt", "w").write("%d %d %.3f %d %d\n" % (sx, sy, peak, jw, jh))
    print("%s: %dx%d, offset to the raw window (%d, %d): dx %+d dy %+d px (peak %.3f)" % (jpg, jw, jh, x0, y0, sx, sy, peak))


if __name__ == "__main__":
    main()
