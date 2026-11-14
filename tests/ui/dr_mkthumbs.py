#!/usr/bin/env python3
"""dr_mkthumbs.py - inputs and outputs of the Roll + Darkroom screen harness (tests/ui/dr_ui_test.c). WSL, python3 -I -B.

  dr_mkthumbs.py THUMBDIR IMG1.JPG [IMG2.JPG ...]
      print_<k>.l8 (288 x 192 luma) and wire_<k>.l8 (72 x 48 luma) for k = 0..7 (the images repeat when fewer are
      given), lv.png (720 x 480: the first image as a stand-in LiveView frame). The photos stay outside the repository.
  dr_mkthumbs.py --hud THUMBDIR SHOTDIR
      every SHOTDIR/hud_<name>.idx (720 x 480 ML colour indices, 0 = transparent) over lv.png -> SHOTDIR/hud_<name>.png
  dr_mkthumbs.py --png SHOTDIR
      every SHOTDIR/*.ppm -> .png (the .ppm removed)"""
import os, sys
from PIL import Image

PW, PH, TW, TH = 288, 192, 72, 48

# ML's default bitmap palette for the few indices the HUD uses (bmp.h names)
ML = {1: (255, 255, 255), 2: (0, 0, 0), 8: (235, 40, 40), 0x0F: (255, 220, 60)}
for i in range(38, 80):                                   # COLOR_GRAY(p) = 38 + p * 41 / 100
    g = round((i - 38) * 255 / 41)
    ML.setdefault(i, (g, g, g))


def luma(im, w, h):
    im = im.convert("RGB")
    sw, sh = im.size
    tw = sw if sw * h <= sh * w else sh * w // h           # centre crop to 3:2
    th = sh if sw * h <= sh * w else sh
    th = tw * h // w
    box = ((sw - tw) // 2, (sh - th) // 2, (sw + tw) // 2, (sh + th) // 2)
    return im.crop(box).resize((w, h), Image.LANCZOS).convert("L")


def main():
    a = sys.argv[1:]
    if a and a[0] == "--png":
        d = a[1]
        for n in sorted(os.listdir(d)):
            if n.endswith(".ppm"):
                Image.open(os.path.join(d, n)).convert("RGB").save(os.path.join(d, n[:-4] + ".png"))
                os.remove(os.path.join(d, n))
        return
    if a and a[0] == "--hud":
        td, sd = a[1], a[2]
        lv = Image.open(os.path.join(td, "lv.png")).convert("RGB")
        for n in sorted(os.listdir(sd)):
            if not n.endswith(".idx"):
                continue
            raw = open(os.path.join(sd, n), "rb").read()
            out = lv.copy()
            px = out.load()
            for y in range(480):
                row = raw[y * 720:(y + 1) * 720]
                for x in range(720):
                    c = row[x]
                    if c:
                        px[x, y] = ML.get(c, (255, 0, 255))
            out.save(os.path.join(sd, n[:-4] + ".png"))
            os.remove(os.path.join(sd, n))
        return
    td, imgs = a[0], a[1:]
    os.makedirs(td, exist_ok=True)
    ims = [Image.open(p) for p in imgs]
    for k in range(8):
        im = ims[k % len(ims)]
        open(os.path.join(td, "print_%d.l8" % k), "wb").write(luma(im, PW, PH).tobytes())
        open(os.path.join(td, "wire_%d.l8" % k), "wb").write(luma(im, TW, TH).tobytes())
    lv = ims[0].convert("RGB").resize((720, 480), Image.LANCZOS)
    lv.save(os.path.join(td, "lv.png"))
    print("thumbs: %d images -> %s" % (len(ims), td))


main()
