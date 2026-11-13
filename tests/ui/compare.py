#!/usr/bin/env python3
"""compare.py OUTDIR MOCKDIR - PPM screenshots of ui_test -> PNG, plus side-by-side sheets (ours | mockup) and 2x crops
of the areas that matter, for the by-eye check. Run with python3 -I -B (WSL)."""
import os, sys
from PIL import Image

out, mock = sys.argv[1], sys.argv[2]
PAIRS = {"b2s_colour": "mock_B2s_strip_720.png", "b2s_lut": "mock_B2s_strip_lut_720.png"}
CROPS = {"header": (0, 0, 720, 40), "info": (250, 30, 720, 280), "strip": (0, 270, 720, 480), "can": (20, 30, 260, 290)}

for n in sorted(os.listdir(out)):
    if not n.endswith(".ppm"):
        continue
    b = n[:-4]
    im = Image.open(os.path.join(out, n)).convert("RGB")
    im.save(os.path.join(out, b + ".png"))
    os.remove(os.path.join(out, n))
    if b in PAIRS:
        m = Image.open(os.path.join(mock, PAIRS[b])).convert("RGB")
        sheet = Image.new("RGB", (1440, 480))
        sheet.paste(im, (0, 0))
        sheet.paste(m, (720, 0))
        sheet.save(os.path.join(out, "cmp_" + b + ".png"))
        for k, box in CROPS.items():
            w, h = box[2] - box[0], box[3] - box[1]
            c = Image.new("RGB", (w * 2, h * 4 + 8), (255, 0, 255))
            c.paste(im.crop(box).resize((w * 2, h * 2), Image.NEAREST), (0, 0))
            c.paste(m.crop(box).resize((w * 2, h * 2), Image.NEAREST), (0, h * 2 + 8))
            c.save(os.path.join(out, "cmp_%s_%s.png" % (b, k)))
    print("png", b)
