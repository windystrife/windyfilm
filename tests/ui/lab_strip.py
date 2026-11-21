#!/usr/bin/env python3
"""lab_strip.py - helpers of tests/ui/lab_strip.sh (WindyStrife, labfx: the film-lab Halation / Bloom / Grain).

  lab_strip.py sheets RUNDIR OUTDIR NAME [GX,GY [NX,NY]]
      RUNDIR/lab_{off,classic,lab35,lab8,lab16,lab65}.ppm (whole frames from host case FX lab with WF_FX_DUMP=1) ->
      OUTDIR/lab_compare_NAME.png: Off | WindyFilm (Grain Normal, Halation Wide, Bloom Normal, 35 mm) | Lab 35 mm, each
          as the whole frame, the glow crop at 1:1 round GX,GY (frame pixels; else a bright edge) and the grain
          crop at 2:1 round NX,NY (else the flattest mid-grey area);
      OUTDIR/lab_gauges_NAME.png: Lab at 8 / 16 / 35 / 65 mm, the same two crops.
Needs numpy and PIL (WSL python3 -I -B). The frames are the module's own output (its UYVY words read back as RGB).
"""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SETS = ('off', 'classic', 'lab35', 'lab8', 'lab16', 'lab65')
NAMES = {'off': 'Effects off (the film look only)', 'classic': 'WindyFilm: Grain Normal, Halation Wide, Bloom Normal, 35 mm',
         'lab35': 'Lab, 35 mm', 'lab8': 'Lab, 8 mm', 'lab16': 'Lab, 16 mm', 'lab65': 'Lab, 65 mm'}
TW, TH = 720, 480                                # one panel
GW, GH = 360, 240                                # the grain crop (shown x2)


def font(n):
    try:
        return ImageFont.load_default(size=n)
    except TypeError:                            # Pillow < 10.1
        return ImageFont.load_default()


def pick(L, what):
    """the centre of the glow (bright next to dark: where halation and bloom spill) or grain (flattest cell with mean
    luma 0.35..0.6) area"""
    cs = 48
    h, w = L.shape
    gh, gw = h // cs, w // cs
    c = L[: gh * cs, : gw * cs].reshape(gh, cs, gw, cs)
    if what == 'glow':                           # bright (>= 230) and dark (< 110) pixels side by side
        score = np.minimum((c >= 230).mean(axis=(1, 3)), (c < 110).mean(axis=(1, 3)))
        k = np.ones((5, 5)) / 25.0               # 240 px round it
        from numpy.lib.stride_tricks import sliding_window_view as sw
        p = np.pad(score, 2)
        score = (sw(p, (5, 5)) * k).sum(axis=(2, 3))
        cy, cx = np.unravel_index(np.argmax(score), score.shape)
    else:
        m = c.mean(axis=(1, 3)) / 255.0
        s = c.std(axis=(1, 3))
        s[(m < 0.35) | (m > 0.6)] = 1e9
        cy, cx = np.unravel_index(np.argmin(s), s.shape)
    return cx * cs + cs // 2, cy * cs + cs // 2


def box(cx, cy, cw, ch, w, h):
    x0 = int(min(max(cx - cw // 2, 0), w - cw)) & ~1
    y0 = int(min(max(cy - ch // 2, 0), h - ch))
    return x0, y0


def sheets(rundir, outdir, name, g_at=None, n_at=None):
    ims = {s: Image.open('%s/lab_%s.ppm' % (rundir, s)).convert('RGB') for s in SETS}
    w, h = ims['off'].size
    L = np.asarray(ims['off'].convert('L'), dtype=np.float32)
    gx, gy = tuple(int(v) for v in g_at.split(',')) if g_at else pick(L, 'glow')
    nx, ny = tuple(int(v) for v in n_at.split(',')) if n_at else pick(L, 'grain')
    gb = box(gx, gy, TW, TH, w, h)
    nb = box(nx, ny, GW, GH, w, h)
    th = round(h * TW / w)
    pad, lab = 10, 26
    f1, f2 = font(20), font(16)

    def panel(sheet, d, x, y, im, rows):
        for r in rows:
            if r == 'frame':
                sheet.paste(im.resize((TW, th), Image.LANCZOS), (x, y))
                for (bx, by), (cw, ch), col in ((gb, (TW, TH), (255, 200, 0)), (nb, (GW, GH), (120, 200, 255))):
                    d.rectangle([x + bx * TW // w, y + by * th // h, x + (bx + cw) * TW // w, y + (by + ch) * th // h],
                                outline=col, width=2)
                y += th + pad
            elif r == 'glow':
                sheet.paste(im.crop((gb[0], gb[1], gb[0] + TW, gb[1] + TH)), (x, y))
                y += TH + pad
            else:
                sheet.paste(im.crop((nb[0], nb[1], nb[0] + GW, nb[1] + GH)).resize((TW, TH), Image.NEAREST), (x, y))
                y += TH + pad
        return y

    for kind, sets, rows in (('compare', ('off', 'classic', 'lab35'), ('frame', 'glow', 'grain')),
                             ('gauges', ('lab8', 'lab16', 'lab35', 'lab65'), ('glow', 'grain'))):
        n = len(sets)
        H = lab + pad + sum(th if r == 'frame' else TH for r in rows) + len(rows) * pad + lab + 30
        sheet = Image.new('RGB', (n * TW + (n + 1) * pad, H), (16, 16, 18))
        d = ImageDraw.Draw(sheet)
        d.text((pad, 4), '%s - %s (module output). Yellow: glow crop 1:1 at x%d y%d; blue: grain crop 2:1 at x%d y%d'
               % (name, 'Off / WindyFilm / Lab' if kind == 'compare' else 'Lab gauges', gb[0], gb[1], nb[0], nb[1]),
               fill=(230, 230, 230), font=f1)
        for i, s in enumerate(sets):
            x = pad + i * (TW + pad)
            d.text((x, lab + 4), NAMES[s], fill=(255, 220, 120), font=f2)
            panel(sheet, d, x, lab + pad + 22, ims[s], rows)
        sheet.save('%s/lab_%s_%s.png' % (outdir, kind, name))


if __name__ == '__main__':
    a = sys.argv
    if len(a) >= 5 and a[1] == 'sheets':
        sheets(a[2], a[3], a[4], a[5] if len(a) > 5 else None, a[6] if len(a) > 6 else None)
    else:
        sys.exit(__doc__)
