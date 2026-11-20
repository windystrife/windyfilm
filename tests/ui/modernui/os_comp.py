#!/usr/bin/env python3
"""os_comp.py SHOTDIR PHOTODIR LV_EOSM LV_5D2 OUTDIR - the modernUI mock screens as PNGs (tests/ui/modernui/os_run.sh).

Each os_mock screen comes as <name>.ppm (the front buffer through the palette), <name>.idx (raw indices, 0 =
transparent) and <name>.lay. The .lay names the photos of the layer under the bitmap (LiveView, or Canon's / our
playback layer) and the two scrim entries; on the camera a scrim entry is a palette entry with partial opacity, here it
is blended with the layer. Photos stay outside the repository: PHOTODIR holds print.jpg and photos/t_NN.jpg
(mk_app_assets.py), LV_* are camera JPEGs used as stand-in LiveView frames."""
import os, sys
import numpy as np
from PIL import Image, ImageOps

def fit_into(im, w, h, mode):
    im = ImageOps.exif_transpose(im).convert("RGB")
    if mode == "cover":
        return ImageOps.fit(im, (w, h), Image.LANCZOS)
    im.thumbnail((w, h), Image.LANCZOS)
    out = Image.new("RGB", (w, h), (0, 0, 0))
    out.paste(im, ((w - im.width) // 2, (h - im.height) // 2))
    return out

def main():
    sd, pd, lv, lv5, od = sys.argv[1:6]
    os.makedirs(od, exist_ok=True)
    src = {"lv": lv, "lv5d2": lv5, "print": os.path.join(pd, "print.jpg")}
    for n in sorted(os.listdir(sd)):
        if not n.endswith(".lay"):
            continue
        b = n[:-4]
        ppm = np.asarray(Image.open(os.path.join(sd, b + ".ppm")).convert("RGB")).copy()
        idx = np.fromfile(os.path.join(sd, b + ".idx"), np.uint8).reshape(480, 720)
        base = np.zeros_like(ppm)
        scrims = []
        for line in open(os.path.join(sd, n)):
            t = line.split()
            if t[0] == "scrim":
                scrims.append((int(t[1]), float(t[2])))
            elif t[0] == "photo":
                x0, y0, x1, y1 = map(int, t[1:5])
                name, mode = t[5], t[6]
                p = src.get(name) or os.path.join(pd, "photos", name + ".jpg")
                base[y0:y1 + 1, x0:x1 + 1] = np.asarray(fit_into(Image.open(p), x1 - x0 + 1, y1 - y0 + 1, mode))
        out = ppm.copy()
        m = idx == 0
        out[m] = base[m]
        for v, a in scrims:
            m = idx == v
            out[m] = (base[m] * (1 - a) + ppm[m] * a + 0.5).astype(np.uint8)
        Image.fromarray(out).save(os.path.join(od, b + ".png"))
        print(b + ".png")

main()
