#!/usr/bin/env python3
"""mist_strip.py - sheets of tests/ui/mist_strip.sh (WindyStrife, owner 11-15: every Mist level on real photos).

  mist_strip.py levels RUNDIR OUTSTEM TITLE
      RUNDIR/mist_0..8.ppm (1/2 size: Off, 1/8, 1/4, 1/2, 1, Black 1/8 .. Black 1) and mist_<lv>_c<k>.ppm (1:1 crops,
      480 x 320) -> OUTSTEM_frames.png (white row, black row; each starting with Off) and OUTSTEM_crop<k>.png (the same rows
      as 1:1 crops); with combo_0..3 (Off, Mist 1/4, Bloom Normal + Halation Film, all three) OUTSTEM_combo.png (the frame
      on top, each crop below); with halves_4 / sky_0..1 (the disc scene) OUTSTEM_extra.png
Needs numpy and PIL (WSL python3 -I -B). The camera's UYVY of a JPEG: bloom_strip.py uyvy.
"""
import os
import sys

from PIL import Image, ImageDraw

NAMES = ('Off', '1/8', '1/4', '1/2', '1', 'Black 1/8', 'Black 1/4', 'Black 1/2', 'Black 1')
COMBO = ('Off', 'Mist 1/4', 'Halation Film + Bloom Normal', 'all three')
TW = 480           # a whole frame's width on the sheet (the crops are 480 x 320 at 1:1)
PAD, LAB = 8, 20


def load(path):
    return Image.open(path).convert('RGB') if os.path.exists(path) else None


def grid(rows, title, dst):
    """rows: [(row label, [(cell label, image), ...]), ...] -> one PNG, cells TW wide (images scaled to TW)"""
    cells = [[(lab, im if im.width == TW else im.resize((TW, round(im.height * TW / im.width)), Image.BILINEAR))
              for lab, im in r] for _, r in rows]
    nc = max(len(r) for r in cells)
    rh = [max(im.height for _, im in r) for r in cells]
    W = PAD + nc * (TW + PAD)
    H = LAB + sum(LAB + h + PAD for h in rh) + PAD
    sheet = Image.new('RGB', (W, H), (16, 16, 18))
    d = ImageDraw.Draw(sheet)
    d.text((PAD, 4), title, fill=(230, 230, 230))
    y = LAB
    for (rl, _), r, h in zip(rows, cells, rh):
        for i, (cl, im) in enumerate(r):
            x = PAD + i * (TW + PAD)
            d.text((x + 2, y + 3), (rl + ': ' if rl and i == 0 else '') + cl, fill=(255, 220, 120))
            sheet.paste(im, (x, y + LAB))
        y += LAB + h + PAD
    sheet.save(dst)
    print('  ' + dst)


def levels(rundir, stem, title):
    fr = [load('%s/mist_%d.ppm' % (rundir, lv)) for lv in range(9)]
    if any(im is None for im in fr):
        sys.exit('%s: mist_0..8.ppm missing' % rundir)
    w2 = [(NAMES[lv], fr[lv]) for lv in (0, 1, 2, 3, 4)]
    b2 = [(NAMES[lv], fr[lv]) for lv in (0, 5, 6, 7, 8)]
    grid([('White Mist', w2), ('Black Mist', b2)], '%s: Mist levels, Halation Off, Bloom Off (whole frame, scaled)' % title,
         stem + '_frames.png')
    k = 0
    while os.path.exists('%s/mist_0_c%d.ppm' % (rundir, k)):
        c = [load('%s/mist_%d_c%d.ppm' % (rundir, lv, k)) for lv in range(9)]
        grid([('White Mist', [(NAMES[lv], c[lv]) for lv in (0, 1, 2, 3, 4)]),
              ('Black Mist', [(NAMES[lv], c[lv]) for lv in (0, 5, 6, 7, 8)])],
             '%s: crop %d at 1:1 (480 x 320 frame pixels), Halation Off, Bloom Off' % (title, k), '%s_crop%d.png' % (stem, k))
        k += 1
    if os.path.exists('%s/combo_0.ppm' % rundir):
        rows = [('', [(COMBO[i], load('%s/combo_%d.ppm' % (rundir, i))) for i in range(4)])]
        for j in range(k):
            rows.append(('crop %d' % j, [(COMBO[i], load('%s/combo_%d_c%d.ppm' % (rundir, i, j))) for i in range(4)]))
        grid(rows, '%s: do the glows add? Off / Mist 1/4 / Halation Film + Bloom Normal / all three (crops at 1:1)' % title,
             stem + '_combo.png')
    ex = [(n, load('%s/%s.ppm' % (rundir, f))) for n, f in (('white 1, Diana halves (TOP/BOT seam)', 'halves_4'),
                                                             ('sky over shadow, white 1', 'sky_0'),
                                                             ('sky over shadow, black 1', 'sky_1'))]
    ex = [(n, im) for n, im in ex if im is not None]
    if ex:
        rows = [('', ex)]
        cr = [(n, load('%s/%s_c%d.ppm' % (rundir, f, j))) for n, f in (('halves crop %d', 'halves_4'),) for j in range(k)]
        cr = [(n % j, im) for j, (n, im) in enumerate(cr) if im is not None]
        if cr:
            rows.append(('', cr))
        grid(rows, '%s: extra frames' % title, stem + '_extra.png')


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == 'levels':
        levels(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
