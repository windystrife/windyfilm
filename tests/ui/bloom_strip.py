#!/usr/bin/env python3
"""bloom_strip.py - helpers of tests/ui/bloom_strip.sh (WindyStrife, RAWB11 Bloom row).

  bloom_strip.py uyvy IN.JPG OUT.uyvy W H
      the JPEG's own Y'CbCr (no colour conversion: PIL draft) as the camera's UYVY words {U s8, Y0, V s8, Y1}, chroma =
      the mean of each pixel pair; the picture scaled to cover W x H and centre-cropped
  bloom_strip.py strip RUNDIR OUT.png TITLE [X,Y]
      RUNDIR/bloom_0..3.ppm (1/2 size, halation Off, Bloom Off / Low / Normal / Strong) -> one PNG: the four frames
      small on top, below them the same crop at 1:2 round X,Y (frame pixels) or the brightest area, labelled
Needs numpy and PIL (WSL python3 -I -B).
"""
import sys

import numpy as np
from PIL import Image, ImageDraw

LEVELS = ('Off', 'Low', 'Normal', 'Strong')


def uyvy(src, dst, w, h):
    im = Image.open(src)
    im.draft('YCbCr', im.size)
    im = im.convert('YCbCr') if im.mode != 'YCbCr' else im
    s = max(w / im.width, h / im.height)
    if s != 1:
        im = im.resize((round(im.width * s), round(im.height * s)), Image.BILINEAR)
    x0, y0 = (im.width - w) // 2, (im.height - h) // 2
    a = np.asarray(im.crop((x0, y0, x0 + w, y0 + h)), dtype=np.int32)
    y, cb, cr = a[..., 0], a[..., 1], a[..., 2]
    u = ((cb[:, 0::2] + cb[:, 1::2] + 1) // 2 - 128).astype(np.int8).view(np.uint8)
    v = ((cr[:, 0::2] + cr[:, 1::2] + 1) // 2 - 128).astype(np.int8).view(np.uint8)
    out = np.stack([u, y[:, 0::2].astype(np.uint8), v, y[:, 1::2].astype(np.uint8)], axis=-1)
    out.tofile(dst)


def strip(rundir, dst, title, at=None):
    ims = [Image.open('%s/bloom_%d.ppm' % (rundir, i)).convert('RGB') for i in range(4)]
    w, h = ims[0].size
    # the crop: the brightest 1/8 cell of the Off frame (its luma, a box blur) -> 480 x 320 at 1:2
    g = np.asarray(ims[0].convert('L'), dtype=np.float32)
    cs = 32
    gh, gw = h // cs, w // cs
    cell = g[: gh * cs, : gw * cs].reshape(gh, cs, gw, cs).mean(axis=(1, 3))
    cy, cx = np.unravel_index(np.argmax(cell), cell.shape)
    px, py = (cx * cs + cs // 2, cy * cs + cs // 2) if not at else (int(at.split(',')[0]) // 2, int(at.split(',')[1]) // 2)
    cw, ch = 480, 320
    x0 = int(min(max(px - cw // 2, 0), w - cw))
    y0 = int(min(max(py - ch // 2, 0), h - ch))
    tw = 480
    th = round(h * tw / w)
    pad, lab = 8, 22
    W = 4 * tw + 5 * pad
    H = lab + th + pad + lab + ch + pad + 26
    sheet = Image.new('RGB', (W, H), (16, 16, 18))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 4), '%s: Halation Off, Bloom Off / Low / Normal / Strong (top: whole frame; below: crop at 1:2, x%d y%d)'
           % (title, 2 * x0, 2 * y0), fill=(230, 230, 230))
    for i, im in enumerate(ims):
        x = pad + i * (tw + pad)
        sheet.paste(im.resize((tw, th), Image.BILINEAR), (x, lab))
        d.rectangle([x + x0 * tw // w, lab + y0 * th // h, x + (x0 + cw) * tw // w, lab + (y0 + ch) * th // h],
                    outline=(255, 200, 0))
        d.text((x + 2, lab + th + pad), 'Bloom ' + LEVELS[i], fill=(255, 220, 120))
        sheet.paste(im.crop((x0, y0, x0 + cw, y0 + ch)), (x, lab + th + pad + lab))
    sheet.save(dst)


if __name__ == '__main__':
    if len(sys.argv) == 6 and sys.argv[1] == 'uyvy':
        uyvy(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]))
    elif len(sys.argv) in (5, 6) and sys.argv[1] == 'strip':
        strip(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5] if len(sys.argv) == 6 else None)
    else:
        sys.exit(__doc__)
