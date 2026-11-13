#!/usr/bin/env python3
"""mk_wfcans.py OUT.DAT [--colours N] [--sheet DIR] [--catalog DIR] - the WindyFilm can pack ML/DATA/WFCANS.DAT.

Our own work: the cans are drawn by cans/gen_cans.py (RGB), the palette is a median cut over our own cans, nothing of
any other module is read. Layout (docs/DESIGN.md 4.4):

  header 64 B  "WFCN", u16 version 1, u16 count, u16 npal (<= 128), u16 nsizes 3,
               sizes[3] {u16 w, u16 h} = big 160x226, strip 52x73, selected 58x82,
               u32 palette offset, u32 index offset, u32 data offset, u32 FNV-1a of all bytes after the header, zero pad
  palette      npal x {u8 r, g, b, 0}
  index  32 B  u8 id, u8 pad[3], u8 glow r, g, b, u8 pad, u32 offset[3], u32 bytes[3]   (offsets from the file start)
  data         RLE per image, row-major, a run never crosses a row end:
               0x00..0x7F one pixel of that palette index; 0xFF one transparent pixel;
               0x80..0xFE a run of (c - 0x7E) = 2..128 pixels of the next byte (an index, or 0xFF = transparent)

--colours N (default 112): the palette size. The edge pixels of each size are pre-blended with the colour it sits on
(big: the page background, strip: the film frame, selected: the amber frame). glow = the mean of the can's saturated
pixels x 1.25 (weight = max - min + 8, as the mockup's can_mean). Self-test: every image is decoded again and compared
with the quantised source (exact) and with the RGB source (mean / max colour error); --sheet DIR writes the decoded
contact sheets (PNG).
"""
import os, sys, struct, hashlib
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "cans"))
import gen_cans as G

SIZES = ((160, 226), (52, 73), (58, 82))
BGS = ((24, 27, 33), (21, 23, 28), (84, 58, 22))            # big: page, strip: film frame, selected: amber frame
GLOW_SIZE = (40, 56)

def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h

def glow_of(f):
    """mean of the saturated pixels x 1.25 (mockup.py can_mean)"""
    rgb8, m = G.render(f, GLOW_SIZE[0], GLOW_SIZE[1], (0, 0, 0))
    px = rgb8[m].astype(float)
    w = (px.max(1) - px.min(1)) + 8
    mean = (px * w[:, None]).sum(0) / w.sum()
    return tuple(min(255, int(int(v) * 1.25)) for v in mean)

def median_cut(pixels, n):
    """our own median cut: boxes of RGB points split at the median of the widest channel (weighted by population)
    until n boxes; each box's palette entry = its mean. pixels (N, 3) uint8."""
    cols, cnt = np.unique(pixels, axis=0, return_counts=True)
    boxes = [(cols.astype(np.int64), cnt.astype(np.int64))]
    while len(boxes) < n:
        best, bs = -1, -1.0
        for i, (c, w) in enumerate(boxes):
            if len(c) < 2:
                continue
            rng = (c.max(0) - c.min(0)).max()
            score = float(rng) * float(w.sum()) ** 0.5            # large and populated boxes first
            if score > bs:
                best, bs = i, score
        if best < 0:
            break
        c, w = boxes.pop(best)
        ax = int((c.max(0) - c.min(0)).argmax())
        o = np.argsort(c[:, ax], kind="stable")
        c, w = c[o], w[o]
        cum = np.cumsum(w)
        k = int(np.searchsorted(cum, cum[-1] / 2.0)) + 1
        k = min(max(k, 1), len(c) - 1)
        boxes.append((c[:k], w[:k]))
        boxes.append((c[k:], w[k:]))
    pal = np.array([np.round((c * w[:, None]).sum(0) / w.sum()) for c, w in boxes], np.uint8)
    return pal

B8 = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38],
               [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
               [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]])
DITHER = (0.020, 0.008, 0.008)          # ordered-dither amplitude in OKLab (L, a, b), flat areas only (no speckle on text / edges)

def nearest(pal, rgb8, mask, dither=True):
    """palette index of every opaque pixel (nearest in OKLab, ordered dither on flat areas), 255 for transparent pixels"""
    lab_p = G.oklab(G.lin(pal))
    h, w = mask.shape
    lab_all = G.oklab(G.lin(rgb8))
    if dither:
        m = (np.tile(B8, (h // 8 + 1, w // 8 + 1))[:h, :w] + 0.5) / 64.0 - 0.5
        g = np.zeros((h, w))
        for ax, sh in ((0, 1), (0, -1), (1, 1), (1, -1)):
            g = np.maximum(g, np.abs(np.roll(lab_all[..., 0], sh, ax) - lab_all[..., 0]))
        flat = (g < 0.045)[..., None]
        lab_all = lab_all + m[..., None] * np.array(DITHER)[None, None, :] * flat
    out = np.full(mask.shape, 255, np.uint8)
    lab = lab_all[mask]
    idx = np.empty(len(lab), np.uint8)
    for i in range(0, len(lab), 4096):
        d = ((lab[i:i + 4096, None, :] - lab_p[None, :, :]) ** 2).sum(-1)
        idx[i:i + 4096] = d.argmin(1)
    out[mask] = idx
    return out

def rle(img):
    """row-major RLE of a (h, w) uint8 image of palette indices (255 = transparent), runs stay inside a row"""
    out = bytearray()
    for row in img:
        x, w = 0, len(row)
        while x < w:
            v = int(row[x])
            n = 1
            while x + n < w and int(row[x + n]) == v and n < 128:
                n += 1
            if n == 1:
                out.append(0xFF if v == 255 else v)
            else:
                out.append(0x7E + n)
                out.append(v)
            x += n
    return bytes(out)

def unrle(b, w, h):
    """the decoder the UI implements (self-test): bytes -> (h, w) uint8 image, 255 = transparent"""
    out = np.empty(w * h, np.uint8)
    i = o = 0
    while o < w * h:
        c = b[i]; i += 1
        if c <= 0x7F:
            out[o] = c; o += 1
        elif c == 0xFF:
            out[o] = 255; o += 1
        else:
            n = c - 0x7E
            v = b[i]; i += 1
            out[o:o + n] = v; o += n
    assert o == w * h and i == len(b), ("RLE size", o, w * h, i, len(b))
    return out.reshape(h, w)

def build(fl, ncol):
    imgs = {}                                                       # (code, size k) -> (rgb8, mask)
    for f in fl:
        for k, (w, h) in enumerate(SIZES):
            imgs[(f["code"], k)] = G.render(f, w, h, BGS[k])
    pix = np.concatenate([rgb[m] for rgb, m in imgs.values()], 0)
    pal = median_cut(pix, ncol)
    assert len(pal) <= 128
    npal = len(pal)
    q = {key: nearest(pal, *v) for key, v in imgs.items()}
    ents = []
    for f in fl:
        enc = [rle(q[(f["code"], k)]) for k in range(3)]
        ents.append((f, enc, glow_of(f)))
    return pal, ents, imgs, q

def pack(pal, ents):
    n, npal = len(ents), len(pal)
    pal_off = 64
    idx_off = pal_off + 4 * npal
    data_off = idx_off + 32 * n
    idx, data = b"", b""
    for f, enc, g in ents:
        offs, lens = [], []
        for e in enc:
            offs.append(data_off + len(data)); lens.append(len(e)); data += e
        idx += struct.pack("<B3x3sx3I3I", f["id"], bytes(g), *offs, *lens)
    assert len(idx) == 32 * n
    body = b"".join(struct.pack("<4B", int(r), int(g), int(b), 0) for r, g, b in pal) + idx + data
    hdr = b"WFCN" + struct.pack("<HHHH", 1, n, npal, 3)
    for w, h in SIZES:
        hdr += struct.pack("<HH", w, h)
    hdr += struct.pack("<IIII", pal_off, idx_off, data_off, fnv(body))
    hdr = hdr.ljust(64, b"\0")
    assert len(hdr) == 64
    return hdr + body

def selftest(blob, fl, pal, imgs, q, sheet):
    assert blob[:4] == b"WFCN"
    ver, n, npal, nsz = struct.unpack_from("<HHHH", blob, 4)
    assert (ver, n, nsz) == (1, len(fl), 3)
    sizes = [struct.unpack_from("<HH", blob, 12 + 4 * k) for k in range(3)]
    assert sizes == list(SIZES), sizes
    pal_off, idx_off, data_off, chk = struct.unpack_from("<IIII", blob, 24)
    assert chk == fnv(blob[64:]), "checksum"
    P = np.frombuffer(blob[pal_off:pal_off + 4 * npal], np.uint8).reshape(npal, 4)[:, :3]
    assert (P == pal).all()
    lut = np.zeros((256, 4), np.uint8)
    lut[:npal, :3] = P
    lut[:npal, 3] = 255
    stats = []
    dec = {}
    for i, f in enumerate(fl):
        e = struct.unpack_from("<B3x3sx3I3I", blob, idx_off + 32 * i)
        assert e[0] == f["id"]
        for k, (w, h) in enumerate(SIZES):
            off, ln = e[2 + k], e[5 + k]
            img = unrle(blob[off:off + ln], w, h)
            assert (img == q[(f["code"], k)]).all(), ("decode mismatch", f["code"], k)
            dec[(f["code"], k)] = lut[img]
            rgb, m = imgs[(f["code"], k)]
            d = np.abs(lut[img][..., :3][m].astype(int) - rgb[m].astype(int))
            stats.append((d.mean(), d.max(), (img == 255).sum() == (~m).sum()))
    assert all(s[2] for s in stats), "transparent pixel count"
    mean = float(np.mean([s[0] for s in stats]))
    print("self-test: %d cans x 3 sizes decode exactly; colour error vs RGB source: mean %.2f, worst can mean %.2f, max %d (codes)" % (
        len(fl), mean, max(s[0] for s in stats), max(s[1] for s in stats)))
    if sheet:
        os.makedirs(sheet, exist_ok=True)
        cols = 13
        rows = (len(fl) + cols - 1) // cols
        for tag, k, bgc in (("big", 0, BGS[0]), ("strip", 1, BGS[1]), ("sel", 2, BGS[2])):
            w, h = SIZES[k]
            sc = 1 if k == 0 else 2
            cw, ch = (w + 8) * sc, (h + 8) * sc
            cc = 6 if k == 0 else cols
            rr = (len(fl) + cc - 1) // cc
            S = np.zeros((rr * ch + 8, cc * cw + 8, 3), np.uint8)
            S[...] = bgc
            for i, f in enumerate(fl):
                im = dec[(f["code"], k)]
                im = np.repeat(np.repeat(im, sc, 0), sc, 1)
                x, y = 8 + (i % cc) * cw, 8 + (i // cc) * ch
                m = im[..., 3] > 0
                S[y:y + im.shape[0], x:x + im.shape[1]][m] = im[..., :3][m]
            Image.fromarray(S).save(os.path.join(sheet, "wfcans_%s.png" % tag))
        # side by side: RGB source vs decoded, a few cans, 3x
        pick = fl[::max(1, len(fl) // 6)][:6]
        w, h = SIZES[0]
        S = np.zeros((2 * (h + 8) + 8, len(pick) * (w + 8) + 8, 3), np.uint8)
        S[...] = BGS[0]
        for i, f in enumerate(pick):
            rgb, m = imgs[(f["code"], 0)]
            x = 8 + i * (w + 8)
            S[8:8 + h, x:x + w][m] = rgb[m]
            im = dec[(f["code"], 0)]
            mm = im[..., 3] > 0
            S[16 + h:16 + 2 * h, x:x + w][mm] = im[..., :3][mm]
        Image.fromarray(S).save(os.path.join(sheet, "wfcans_cmp.png"))
        print("sheets in", sheet)

def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("--"):
        print(__doc__); sys.exit(2)
    out = a[0]
    ncol = int(a[a.index("--colours") + 1]) if "--colours" in a else 112
    sheet = a[a.index("--sheet") + 1] if "--sheet" in a else None
    fl = G.films(a)
    fl = sorted(fl, key=lambda f: f["id"])
    pal, ents, imgs, q = build(fl, ncol)
    blob = pack(pal, ents)
    open(out, "wb").write(blob)
    print("%s: %d cans, npal %d, %d B, md5 %s" % (os.path.basename(out), len(fl), len(pal), len(blob),
                                                  hashlib.md5(blob).hexdigest()))
    selftest(blob, fl, pal, imgs, q, sheet)

if __name__ == "__main__":
    main()
