"""dr_fin_check.py - one Darkroom print with the print finishes (docs/DESIGN.md 3.9), WindyStrife's own check.

  python3 -I -B tools/dr_fin_check.py FIN.JPG PLAIN.JPG CANON.JPG CW CH IX IY ORIENT FRAME DATE OUT.PNG

FIN.JPG: the print with the finishes (tests/host/wf_dark_test.c finish), PLAIN.JPG: the same frame without them, CANON.JPG:
Canon's own JPEG of the shot; CW CH IX IY ORIENT: the canvas the test printed ("canvas" log line); FRAME / DATE: the
settings (WF_PFR_* / WF_PDT_*). Checks:
  - the print decodes at CW x CH
  - the picture's luma at (IX, IY) is the plain print's, value for value (the 16-px MCU alignment), except, with a date,
    the stamp: then the changed pixels sit at the bottom right of the picture as the viewer shows it
  - a frame: its corners are the frame colour (white >= 248, black <= 20)
  - the Exif PixelX / YDimension are CW / CH and the Orientation is Canon's
OUT.PNG: the print as the viewer shows it, a quarter of its size. Exit 0 = all good."""
import sys
import numpy as np
from PIL import Image, ImageOps


def luma(p):
    im = Image.open(p)
    im.draft('YCbCr', im.size)
    return im, np.asarray(im.convert('YCbCr'))[:, :, 0].astype(np.int32)


def main():
    fin, plain, canon = sys.argv[1:4]
    cw, ch, ix, iy, orient, frame, date = (int(v) for v in sys.argv[4:11])
    png = sys.argv[11]
    bad = []
    im, yf = luma(fin)
    _, yp = luma(plain)
    if im.size != (cw, ch):
        bad.append('size %dx%d, want %dx%d' % (im.size + (cw, ch)))
    ph, pw = yp.shape
    pic = yf[iy:iy + ph, ix:ix + pw]
    d = pic != yp if pic.shape == yp.shape else None
    note = ''
    if d is None:
        bad.append('picture region off the canvas')
    elif not date:
        if d.any():
            bad.append('picture luma changed at %d px (max %d)' % (d.sum(), np.abs(pic - yp).max()))
        else:
            note = 'picture luma = plain print'
    else:
        if not d.any():
            bad.append('no date stamp')
        else:
            m = d                                            # the changes as the viewer shows them
            if orient == 3:
                m = m[::-1, ::-1]
            elif orient == 6:
                m = np.rot90(m, -1)
            elif orient == 8:
                m = np.rot90(m, 1)
            ys, xs = np.nonzero(m)
            h, w = m.shape
            x0, y0 = xs.min() / w, ys.min() / h
            ok = x0 > 0.55 and y0 > 0.8
            note = 'date stamp at %.0f%%..%.0f%% x %.0f%%..%.0f%% of the shown picture, %d px' % (
                100 * x0, 100 * xs.max() / w, 100 * y0, 100 * ys.max() / h, d.sum())
            if not ok:
                bad.append(note + ' (want the bottom right)')
    if frame:
        white = frame in (1, 3)
        k = 8
        cs = [yf[:k, :k], yf[:k, -k:], yf[-k:, :k], yf[-k:, -k:]]
        mv = [int(c.mean()) for c in cs]
        if any((v < 248) if white else (v > 20) for v in mv):
            bad.append('frame corners Y %s' % mv)
        note += ', corners Y %s' % mv
    ex = im.getexif()
    ce = Image.open(canon).getexif()
    sub = ex.get_ifd(0x8769)
    if sub.get(0xA002) != cw or sub.get(0xA003) != ch:
        bad.append('Exif size %s x %s' % (sub.get(0xA002), sub.get(0xA003)))
    if ex.get(0x0112) != ce.get(0x0112):
        bad.append('Orientation %s, Canon %s' % (ex.get(0x0112), ce.get(0x0112)))
    sh = ImageOps.exif_transpose(im.convert('RGB'))
    sh.resize((sh.size[0] // 4, sh.size[1] // 4), Image.BOX).save(png)
    print('fin check %s: %dx%d o%s, %s%s' % (fin.split('/')[-1], im.size[0], im.size[1], ex.get(0x0112), note,
                                            ': ' + '; '.join(bad) if bad else ': ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
