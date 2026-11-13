#!/usr/bin/env python3
"""wf_judge.py PHOTODIR OUTDIR [--old DIR] - judge v2 contact sheets of the WindyFilm tables (own code).

Every table goes through the Python model of the camera kernel (tools/mk_wtab.py shoot: JPEG -> 4:2:2 words -> the wf_film
kernel -> RGB). Inputs in PHOTODIR: scene.png, sunset.png, card.png (test photos / charts, sRGB).
  judge_new_old_<k>.png   6 films per sheet: original | new table | older table, for three pictures (scene, card detail,
                          sunset). The older tables (--old, films/*.T17 of the previous port) are read for comparison only.
  judge_prints.png        the films that have print variants: original | Scan | Cine print | Paper, two pictures
  judge_skin.png          the card's skin swatches and grey ramp through every film (Scan), one row per film
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "films"))
import mk_wtab as W
import catalog

CW, CH = 200, 133
FONT = "/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf"

def font(sz):
    f = ImageFont.truetype(FONT, sz)
    f.set_variation_by_axes([100, 500])
    return f

def load(photos):
    d = {}
    for n in ("scene", "sunset", "card"):
        im = Image.open(os.path.join(photos, n + ".png")).convert("RGB")
        if n == "card":                                    # colour checker + skin swatches (the top block)
            w, h = im.size
            im = im.crop((0, 0, w, int(h * 0.50)))
        im = im.resize((CW * 2, CH * 2), Image.LANCZOS)
        d[n] = np.asarray(im)
    return d

def shot(img, T, flags, S=None):
    return W.shoot(img, T, flags, S)

def thumb(a):
    return Image.fromarray(a).resize((CW, CH), Image.LANCZOS)

def sheet(rows, heads, labels, path, lw=170):
    n = len(rows[0])
    H = 26 + len(rows) * (CH + 6)
    S = Image.new("RGB", (lw + n * (CW + 4) + 6, H), (24, 27, 33))
    d = ImageDraw.Draw(S)
    f, fs = font(14), font(12)
    for j, h in enumerate(heads):
        d.text((lw + j * (CW + 4), 5), h, fill=(190, 195, 205), font=fs)
    for i, (r, lab) in enumerate(zip(rows, labels)):
        y = 26 + i * (CH + 6)
        d.text((8, y + 4), lab[0], fill=(244, 166, 52), font=f)
        d.text((8, y + 24), lab[1], fill=(150, 156, 168), font=fs)
        for j, im in enumerate(r):
            S.paste(thumb(im), (lw + j * (CW + 4), y))
    S.save(path)

def main():
    a = sys.argv[1:]
    photos, out = a[0], a[1]
    old = a[a.index("--old") + 1] if "--old" in a else None
    os.makedirs(out, exist_ok=True)
    img = load(photos)
    names = ("scene", "card", "sunset")
    films = [f for f in catalog.FILMS if f["ready"] and not f["lut"]]
    films.sort(key=lambda f: f["order"])
    cache = {}
    def table(f, v):
        k = (f["table"], v)
        if k not in cache:
            T = W.read_t17(catalog.t17_path(f, v))
            cache[k] = (T, W.kbuild(T, 1 if f["v21"] else 0))
        return cache[k]
    # ---- new vs old
    rows, labels = [], []
    for f in films:
        T, S = table(f, "scan")
        row = [img[n] for n in names] + [W.shoot(img[n], T, 1 if f["v21"] else 0, S) for n in names]
        if old:
            To = W.read_t17(os.path.join(old, f["table"] + ".T17"))
            if f["type"] == "bw":
                To = (To & np.uint32(0x0000ff00)).astype(np.uint32)
            So = W.kbuild(To, 1 if f["v21"] else 0)
            row += [W.shoot(img[n], To, 1 if f["v21"] else 0, So) for n in names]
        rows.append(row)
        labels.append((f["name"], "%s  id %d  %s" % (f["table"], f["id"], f["kind"])))
    heads = ["original: " + n for n in names] + ["NEW table: " + n for n in names]
    if old:
        heads += ["OLDER table: " + n for n in names]
    for k in range(0, len(rows), 6):
        sheet(rows[k:k + 6], heads, labels[k:k + 6], os.path.join(out, "judge_new_old_%d.png" % (k // 6 + 1)))
    # ---- print variants
    rows, labels = [], []
    for f in films:
        if len(f["prints"]) < 2:
            continue
        flags = 1 if f["v21"] else 0
        row = [img["scene"], img["sunset"]]
        for v in ("scan", "cine", "paper"):
            if v in f["prints"]:
                T, S = table(f, v)
                row += [W.shoot(img[n], T, flags, S) for n in ("scene", "sunset")]
            else:
                row += [np.full_like(img["scene"], 40)] * 2
        rows.append(row)
        labels.append((f["name"], "%s  %s" % (f["table"], "/".join(f["prints"]))))
    heads = ["original: scene", "original: sunset", "Scan: scene", "Scan: sunset", "Cine print: scene", "Cine print: sunset",
             "Paper: scene", "Paper: sunset"]
    for k in range(0, len(rows), 6):
        sheet(rows[k:k + 6], heads, labels[k:k + 6], os.path.join(out, "judge_prints_%d.png" % (k // 6 + 1)))
    # ---- skin and grey ramp strip of the card through every Scan table
    card = np.asarray(Image.open(os.path.join(photos, "card.png")).convert("RGB"))
    ch, cw = card.shape[:2]
    skin = card[int(ch * 0.02):int(ch * 0.50), int(cw * 0.50):]                     # skin swatches
    ramp = card[int(ch * 0.50):int(ch * 0.62), :]
    sk = np.asarray(Image.fromarray(skin).resize((CW * 2, CH * 2), Image.LANCZOS))
    rp = np.asarray(Image.fromarray(ramp).resize((CW * 2, 24), Image.LANCZOS))
    labels = [(f["name"], f["table"]) for f in films]
    # simple version: skin original | skin new | ramp original | ramp new
    rows2 = []
    for f in films:
        T, S = table(f, "scan")
        flags = 1 if f["v21"] else 0
        o = W.shoot(sk, T, flags, S)
        r = W.shoot(rp, T, flags, S)
        rr = np.concatenate([np.repeat(rp, 3, 0), np.repeat(r, 3, 0)], 0)
        rr = np.asarray(Image.fromarray(rr).resize((CW * 2, CH * 2)))
        rows2.append([sk, o, rr])
    for k in range(0, len(rows2), 8):
        sheet(rows2[k:k + 8], ["skin: original", "skin: through the table", "grey ramp: original over table"],
              labels[k:k + 8], os.path.join(out, "judge_skin_%d.png" % (k // 8 + 1)))
    print("sheets in", out, sorted(os.listdir(out)))

if __name__ == "__main__":
    main()
