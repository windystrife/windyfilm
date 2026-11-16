"""mk_wflogo.py SVG OUT.DAT [--maker NAME] [--model MODEL]... [--render DIR] - the Darkroom's logo pack ML/DATA/WFLOGO.DAT
(src/wf_print.c wf_pf_logo, docs/DESIGN.md 3.9), WindyStrife's own tool.

The owner asked (16 Nov 2026) for the camera maker's wordmark in a framed print's band, beside our own lettering of the
model ("EOS M", "EOS 5D MARK II"). A maker's wordmark is that maker's trademark: its SVG stays in the owner's private
folder (F:/MagicLatern/_windyfilm_docs/private/logo/), and this pack is made from it at card time only
(tools/mk_card.sh LOGO=<svg>). Never commit the SVG, a raster of it or the pack. Without the pack on the card the caption
stays text only.

  image 0: the wordmark, rendered from the SVG's filled paths (nonzero rule, the group transforms applied; M L H V C S Q
           T Z in both cases) at WM_H px from its top to its bottom, 8 x 8 supersampled
  image 1..: the lettering of each --model (default "Canon EOS M", "Canon EOS 5D Mark II"): the model without the maker's
           first word, upper case, drawn here from our own geometry (below: a wide monoline with squared round corners,
           not a copy of any maker's logotype), LT_H px cap height, " Mark ..." at SUFFIX of that size

Format (little-endian):
  header 16 B   "WFLG", u16 version 1, u16 nimg, u32 file bytes, u32 FNV-1a of every byte after the header
  dir           nimg x 24 B: u8 kind (0 wordmark, 1 lettering), u8 bits (8), u16 w, u16 h, u16 top (the cap line's row),
                u16 base (the baseline's row), u16 0, u32 offset of the w x h coverage bytes (row by row, 0..255),
                u32 offset of the key, NUL-terminated (wordmark: the maker, --maker, default "Canon": the module draws
                it only on a shot whose Exif Model starts with it; lettering: the Exif Model it is for), u32 0
  data          the images, then the keys
--render DIR    PNGs of every image and of the two frame colours with the wordmark + lettering as a print draws them.
"""
import math
import re
import struct
import sys
import xml.etree.ElementTree as ET

import numpy as np

WM_H = 192           # wordmark: px from its top to its bottom (the print draws it at 40..140 px)
LT_H = 72            # lettering: cap height px (the print draws it at 18..48 px)
SUFFIX = 0.64        # " Mark II": that part of the lettering at this size
SS = 8               # supersampling of the wordmark
PAD = 2              # px round each image
MODELS = ['Canon EOS M', 'Canon EOS 5D Mark II']


def fnv(b):
    h = 0x811C9DC5
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


# ------------------------------------------------------------------ SVG paths -> polygons
def mat_mul(a, b):
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3],
            a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


def parse_transform(s):
    m = (1, 0, 0, 1, 0, 0)
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', s or ''):
        v = [float(x) for x in re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', args)]
        if name == 'matrix' and len(v) == 6:
            t = tuple(v)
        elif name == 'translate':
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == 'scale':
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == 'rotate' and len(v) == 1:
            a = math.radians(v[0])
            t = (math.cos(a), math.sin(a), -math.sin(a), math.cos(a), 0, 0)
        else:
            raise SystemExit('transform %s(%s) not handled' % (name, args))
        m = mat_mul(m, t)
    return m


def bez(p, n):
    """points of a quadratic / cubic Bezier p (control points), n steps, without the first"""
    out = []
    for i in range(1, n + 1):
        t = i / n
        if len(p) == 3:
            a, b, c = p
            out.append(((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * b[0] + t * t * c[0],
                        (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * b[1] + t * t * c[1]))
        else:
            a, b, c, d = p
            u = 1 - t
            out.append((u ** 3 * a[0] + 3 * u * u * t * b[0] + 3 * u * t * t * c[0] + t ** 3 * d[0],
                        u ** 3 * a[1] + 3 * u * u * t * b[1] + 3 * u * t * t * c[1] + t ** 3 * d[1]))
    return out


def parse_d(d):
    """subpaths (closed point lists) of an SVG path's d"""
    tok = re.findall(r'[MmLlHhVvCcSsQqTtZzAa]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d)
    subs, cur, i = [], [], 0
    x = y = sx = sy = 0.0
    cmd, last_c = None, None
    while i < len(tok):
        if re.match(r'[A-Za-z]', tok[i]):
            cmd = tok[i]
            i += 1
            if cmd in 'Zz':
                if cur:
                    subs.append(cur)
                cur, x, y, last_c = [], sx, sy, None
                continue
            if cmd in 'Aa':
                raise SystemExit('arcs (A) not handled')
        def num():
            nonlocal i
            v = float(tok[i])
            i += 1
            return v
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            if cur:
                subs.append(cur)
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            sx, sy, cur, last_c = x, y, [(x, y)], None
            cmd = 'l' if rel else 'L'            # later pairs are line-tos
        elif c in 'LHV':
            if c == 'L':
                nx, ny = num(), num()
                x, y = (x + nx, y + ny) if rel else (nx, ny)
            elif c == 'H':
                nx = num()
                x = x + nx if rel else nx
            else:
                ny = num()
                y = y + ny if rel else ny
            cur.append((x, y))
            last_c = None
        elif c in 'CS':
            if c == 'C':
                p1 = (num(), num())
            else:
                p1 = (2 * x - last_c[0], 2 * y - last_c[1]) if last_c and last_c[2] == 'C' else (x, y)
                p1 = (p1[0] - x, p1[1] - y) if rel else p1
            p2, p3 = (num(), num()), (num(), num())
            if rel:
                if c == 'C':
                    p1 = (x + p1[0], y + p1[1])
                else:
                    p1 = (x + p1[0], y + p1[1])
                p2, p3 = (x + p2[0], y + p2[1]), (x + p3[0], y + p3[1])
            cur.extend(bez([(x, y), p1, p2, p3], 24))
            last_c = (p2[0], p2[1], 'C')
            x, y = p3
        elif c in 'QT':
            if c == 'Q':
                p1 = (num(), num())
                if rel:
                    p1 = (x + p1[0], y + p1[1])
            else:
                p1 = (2 * x - last_c[0], 2 * y - last_c[1]) if last_c and last_c[2] == 'Q' else (x, y)
            p2 = (num(), num())
            if rel:
                p2 = (x + p2[0], y + p2[1])
            cur.extend(bez([(x, y), p1, p2], 16))
            last_c = (p1[0], p1[1], 'Q')
            x, y = p2
        else:
            raise SystemExit('path command %s not handled' % cmd)
    if cur:
        subs.append(cur)
    return subs


def svg_polys(path):
    root = ET.parse(path).getroot()
    polys = []

    def walk(el, m):
        m = mat_mul(m, parse_transform(el.get('transform')))
        tag = el.tag.split('}')[-1]
        if tag in ('defs', 'metadata', 'namedview', 'clipPath', 'mask'):
            return
        if tag == 'path' and el.get('d'):
            for sp in parse_d(el.get('d')):
                polys.append([(m[0] * px + m[2] * py + m[4], m[1] * px + m[3] * py + m[5]) for px, py in sp])
        for ch in el:
            walk(ch, m)
    walk(root, (1, 0, 0, 1, 0, 0))
    if not polys:
        raise SystemExit('%s: no paths' % path)
    return polys


def fill_nonzero(polys, w, h):
    """coverage (h x w, 0..1) of the polygons (px), nonzero rule, SS x SS samples per px"""
    W, H = w * SS, h * SS
    acc = np.zeros((H, W + 1), np.int32)
    for p in polys:
        q = np.array(p + [p[0]], np.float64) * SS
        for (x0, y0), (x1, y1) in zip(q[:-1], q[1:]):
            if y0 == y1:
                continue
            d = 1 if y1 > y0 else -1
            ya, yb = (y0, y1) if y0 < y1 else (y1, y0)
            k0, k1 = int(math.ceil(ya - 0.5)), int(math.ceil(yb - 0.5))
            k0, k1 = max(k0, 0), min(k1, H)
            if k0 >= k1:
                continue
            ks = np.arange(k0, k1)
            xs = x0 + (ks + 0.5 - y0) * (x1 - x0) / (y1 - y0)
            xi = np.clip(np.ceil(xs - 0.5).astype(np.int64), 0, W)
            np.add.at(acc, (ks, xi), d)
    inside = np.cumsum(acc, axis=1)[:, :W] != 0
    return inside.reshape(h, SS, w, SS).mean(axis=(1, 3))


def wordmark(svg):
    polys = svg_polys(svg)
    pts = np.array([q for p in polys for q in p])
    x0, y0 = pts.min(axis=0)
    x1, y1 = pts.max(axis=0)
    s = WM_H / (y1 - y0)
    w = int(math.ceil((x1 - x0) * s)) + 2 * PAD
    h = WM_H + 2 * PAD
    polys = [[((px - x0) * s + PAD, (py - y0) * s + PAD) for px, py in p] for p in polys]
    cov = fill_nonzero(polys, w, h)
    return np.round(cov * 255).astype(np.uint8), PAD, PAD + WM_H


# ------------------------------------------------------------------ our lettering
T = 0.14             # stroke, of the cap height
HS = T / 2
TRACK = 0.20         # between letters
SPACE = 0.50


def rpath(pts, closed=False):
    """strokes (p0, p1, cap0, cap1) of a centre line through pts = [(x, y, r)]: r > 0 rounds that vertex with an arc of
    radius r; sharp right-angle vertices get square caps (a sharp corner), other sharp vertices round joins, the open
    ends butt caps"""
    n = len(pts)
    P = [np.array(p[:2], float) for p in pts]
    R = [p[2] for p in pts]
    if not closed:
        R[0] = R[-1] = 0
    segs = []                                      # straight parts between the vertices' arcs
    idx = range(n) if closed else range(n - 1)
    starts, ends = {}, {}
    for i in idx:
        a, b = P[i], P[(i + 1) % n]
        d = (b - a) / np.linalg.norm(b - a)
        starts[i] = a + d * R[i]
        ends[i] = b - d * R[(i + 1) % n]
    strokes = []
    for i in idx:
        strokes.append([starts[i], ends[i], 'b', 'b'])
    for v in (range(n) if closed else range(1, n - 1)):
        si = (v - 1) % n                           # the stroke into v, the stroke out of v
        so = v
        if R[v] > 0:                               # the arc between them, round joins inside it
            a = P[v] - (P[v] - P[si]) / np.linalg.norm(P[v] - P[si]) * R[v]
            b = P[v] + (P[(v + 1) % n] - P[v]) / np.linalg.norm(P[(v + 1) % n] - P[v]) * R[v]
            din = (P[v] - P[si]) / np.linalg.norm(P[v] - P[si])
            dout = (P[(v + 1) % n] - P[v]) / np.linalg.norm(P[(v + 1) % n] - P[v])
            c = a + np.array([-din[1], din[0]]) * R[v] * np.sign(din[0] * dout[1] - din[1] * dout[0])
            a0 = math.atan2(a[1] - c[1], a[0] - c[0])
            a1 = math.atan2(b[1] - c[1], b[0] - c[0])
            da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
            k = 12
            arc = [c + R[v] * np.array([math.cos(a0 + da * j / k), math.sin(a0 + da * j / k)]) for j in range(k + 1)]
            for j in range(k):
                strokes.append([arc[j], arc[j + 1], 'r', 'r'])
        else:
            din = (P[v] - P[si]) / np.linalg.norm(P[v] - P[si])
            dout = (P[(v + 1) % n] - P[v]) / np.linalg.norm(P[(v + 1) % n] - P[v])
            cap = 's' if abs(float(np.dot(din, dout))) < 0.05 else 'r'
            strokes[si if not closed or si in idx else 0][3] = cap
            strokes[so][2] = cap
    return strokes


def line(a, b, ext0=0.0, ext1=0.0):
    a, b = np.array(a, float), np.array(b, float)
    d = (b - a) / np.linalg.norm(b - a)
    return [[a - d * ext0, b + d * ext1, 'b', 'b']]


def rrect(x0, y0, x1, y1, r):
    return rpath([(x0, y0, r), (x1, y0, r), (x1, y1, r), (x0, y1, r)], closed=True)


def glyphs():
    h = HS
    G = {}

    def put(c, w, strokes):
        G[c] = (w, strokes)
    w = 1.34
    put('O', w, rrect(h, h, w - h, 1 - h, 0.30))
    w = 1.04
    put('0', w, rrect(h, h, w - h, 1 - h, 0.28))
    w = 0.92
    put('E', w, rpath([(w, h, 0), (h, h, 0), (h, 1 - h, 0), (w, 1 - h, 0)]) + line((h, 0.5), (w - 0.05, 0.5)))
    w = 1.04
    put('S', w, rpath([(w, h, 0), (h, h, 0.24), (h, 0.5, 0.17), (w - h, 0.5, 0.17), (w - h, 1 - h, 0.24), (0, 1 - h, 0)]))
    w = 1.30
    put('M', w, rpath([(h, 1, 0), (h, h, 0), (w / 2, 0.66, 0), (w - h, h, 0), (w - h, 1, 0)]))
    w = 1.12
    put('D', w, rpath([(h, h, 0), (w - h, h, 0.30), (w - h, 1 - h, 0.30), (h, 1 - h, 0)], closed=True))
    w = 0.98
    put('5', w, rpath([(w, h, 0), (h, h, 0), (h, 0.47, 0), (w - h, 0.47, 0.22), (w - h, 1 - h, 0.22), (0, 1 - h, 0)]))
    w = 1.16
    put('A', w, rpath([(h, 1, 0), (h, h, 0.30), (w - h, h, 0.30), (w - h, 1, 0)]) + line((h, 0.58), (w - h, 0.58)))
    w = 1.08
    put('R', w, rpath([(h, 1, 0), (h, h, 0), (w - h, h, 0.21), (w - h, 0.56, 0.21), (h, 0.56, 0)])
        + line((0.46 * w, 0.56), (w - h, 1.0), 0, 0.3))
    w = 1.00
    put('P', w, rpath([(h, 1, 0), (h, h, 0), (w - h, h, 0.21), (w - h, 0.56, 0.21), (h, 0.56, 0)]))
    w = 1.04
    bx = w - 0.07
    arm = line((h + 0.02, 0.62), (bx, 0.0), 0, 0.3)
    cx, cy = h + 0.45 * (bx - h), 0.62 - 0.45 * 0.62
    put('K', w, line((h, 0), (h, 1)) + arm + line((cx, cy), (bx, 1.0), 0, 0.3))
    put('I', T, line((h, 0), (h, 1)))
    w = 1.10
    put('X', w, line((0.06, 0), (w - 0.06, 1), 0.3, 0.3) + line((w - 0.06, 0), (0.06, 1), 0.3, 0.3))
    w = 0.62
    put('1', w, line((w - h, 0), (w - h, 1)) + line((w - h, h), (0.06, h)))
    w = 0.98
    put('2', w, rpath([(0, h, 0), (w - h, h, 0.22), (w - h, 0.5, 0.17), (h, 0.5, 0.17), (h, 1 - h, 0), (w, 1 - h, 0)]))
    put('3', w, rpath([(0, h, 0), (w - h, h, 0.24), (w - h, 1 - h, 0.24), (0, 1 - h, 0)]) + line((0.30, 0.5), (w - h, 0.5)))
    w = 1.02
    put('4', w, rpath([(h, 0, 0), (h, 0.64, 0), (w, 0.64, 0)]) + line((0.72 * w, 0.30), (0.72 * w, 1)))
    w = 1.00
    six = rpath([(w, h, 0), (h, h, 0.26), (h, 1 - h, 0.26), (w - h, 1 - h, 0.20), (w - h, 0.47, 0.20), (h, 0.47, 0)])
    put('6', w, six)
    put('9', w, [[np.array([w, 1]) - s[0], np.array([w, 1]) - s[1], s[2], s[3]] for s in six])
    w = 0.96
    put('7', w, rpath([(0, h, 0), (w - h, h, 0), (0.36 - 0.212, 1.372, 0)]))   # cut at the baseline
    w = 1.04
    put('8', w, rrect(h, h, w - h, 1 - h, 0.28) + line((h, 0.5), (w - h, 0.5)))
    return G


def sd_stroke(X, Y, s):
    """signed distance (glyph units) of the points to one stroke"""
    a, b, c0, c1 = s
    d = b - a
    L = float(np.hypot(d[0], d[1]))
    if L < 1e-9:
        return np.hypot(X - a[0], Y - a[1]) - HS
    u = d / L
    sx = (X - a[0]) * u[0] + (Y - a[1]) * u[1]       # along
    ty = -(X - a[0]) * u[1] + (Y - a[1]) * u[0]      # across
    lo = -HS if c0 == 's' else 0.0
    hi = L + HS if c1 == 's' else L
    qx = np.maximum(lo - sx, sx - hi)
    qy = np.abs(ty) - HS
    box = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0)
    res = box
    if c0 == 'r':
        res = np.minimum(res, np.hypot(X - a[0], Y - a[1]) - HS)
    if c1 == 'r':
        res = np.minimum(res, np.hypot(X - b[0], Y - b[1]) - HS)
    return res


def lettering(model):
    """our lettering of an Exif Model: (coverage, top row, base row, text) or None (a character we have no glyph for)"""
    words = model.split()
    if len(words) < 2:
        return None
    text = ' '.join(words[1:])
    main, suf = text, ''
    m = re.search(r' (Mark .*)$', text)
    if m:
        main, suf = text[:m.start()], m.group(1)
    G = glyphs()
    items, pen = [], 0.0                            # (char, x, scale)
    for part, sc in ((main.upper(), 1.0), (suf.upper(), SUFFIX)):
        if not part:
            continue
        if items:
            pen += SPACE
        for ch in part:
            if ch == ' ':
                pen += SPACE * sc
                continue
            if ch not in G:
                return None
            items.append((ch, pen, sc))
            pen += (G[ch][0] + TRACK) * sc
    width = pen - TRACK * items[-1][2]
    px = LT_H                                      # px per unit
    w = int(math.ceil(width * px)) + 2 * PAD
    h = LT_H + 2 * PAD
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    out = np.zeros((h, w))
    for ch, x0, sc in items:
        gw, strokes = G[ch]
        lx0, lx1 = int(PAD + x0 * px) - 2, int(math.ceil(PAD + (x0 + gw * sc) * px)) + 3
        lx0, lx1 = max(lx0, 0), min(lx1, w)
        X = ((xx[:, lx0:lx1] + 0.5 - PAD) / px - x0) / sc
        Y = ((yy[:, lx0:lx1] + 0.5 - PAD) / px - (1 - sc)) / sc   # a small part sits on the baseline
        sd = np.full(X.shape, 1e9)
        for s in strokes:
            sd = np.minimum(sd, sd_stroke(X, Y, s))
        sd = np.maximum(sd, np.maximum(-Y, Y - 1))  # cut at the cap line and the baseline
        cov = np.clip(0.5 - sd * px * sc, 0, 1)
        out[:, lx0:lx1] = np.maximum(out[:, lx0:lx1], cov)
    return np.round(out * 255).astype(np.uint8), PAD, PAD + LT_H, text.upper()


# ------------------------------------------------------------------ pack
def pack(imgs):
    """imgs: [(kind, cov, top, base, key or None)]"""
    n = len(imgs)
    off = 16 + 24 * n
    dirs, data = b'', b''
    keys = b''
    koff0 = off + sum(c.size for _, c, _, _, _ in imgs)
    for kind, cov, top, base, key in imgs:
        h, w = cov.shape
        ko = 0
        if key:
            ko = koff0 + len(keys)
            keys += key.encode('ascii') + b'\0'
        dirs += struct.pack('<BBHHHHHIII', kind, 8, w, h, top, base, 0, off + len(data), ko, 0)
        data += cov.tobytes()
    body = dirs + data + keys
    total = 16 + len(body)
    return b'WFLG' + struct.pack('<HHII', 1, n, total, fnv(body)) + body


def render(d, imgs):
    from PIL import Image
    import os
    os.makedirs(d, exist_ok=True)
    for i, (kind, cov, top, base, key) in enumerate(imgs):
        Image.fromarray(255 - cov).save('%s/img%d_%s.png' % (d, i, 'wordmark' if kind == 0 else 'lettering'))
    wm = imgs[0]
    for bg, wc, lc, nm in (((255, 255, 255), (191, 25, 32), (30, 30, 30), 'white'),
                           ((12, 12, 12), (228, 228, 228), (236, 236, 236), 'black')):
        for li in range(1, len(imgs)):
            lt = imgs[li]
            sw = 112 / (wm[3] - wm[2])
            sl = 44 / (lt[3] - lt[2])
            a = np.asarray(Image.fromarray(wm[1]).resize((int(wm[1].shape[1] * sw), int(wm[1].shape[0] * sw)),
                                                         Image.BOX), float) / 255
            b = np.asarray(Image.fromarray(lt[1]).resize((int(lt[1].shape[1] * sl), int(lt[1].shape[0] * sl)),
                                                         Image.BOX), float) / 255
            W = max(a.shape[1], b.shape[1]) + 80
            H = a.shape[0] + b.shape[0] + 80
            im = np.zeros((H, W, 3)) + bg
            ox = W - 40 - a.shape[1]
            for c in range(3):
                im[40:40 + a.shape[0], ox:ox + a.shape[1], c] += (wc[c] - bg[c]) * a
            oy, ox = 40 + a.shape[0] + 14, W - 40 - b.shape[1]
            for c in range(3):
                sl_ = im[oy:oy + b.shape[0], ox:ox + b.shape[1], c]
                sl_ += (lc[c] - sl_) * b
            Image.fromarray(np.clip(im, 0, 255).astype(np.uint8)).save('%s/mock_%s_%d.png' % (d, nm, li))


def main(a):
    if len(a) < 2:
        print(__doc__)
        return 2
    svg, out = a[0], a[1]
    models, rdir, maker, i = [], None, 'Canon', 2
    while i < len(a):
        if a[i] == '--maker':
            maker = a[i + 1]
            i += 2
        elif a[i] == '--model':
            models.append(a[i + 1])
            i += 2
        elif a[i] == '--render':
            rdir = a[i + 1]
            i += 2
        else:
            raise SystemExit('unknown argument %s' % a[i])
    cov, top, base = wordmark(svg)
    imgs = [(0, cov, top, base, maker)]
    for m in models or MODELS:
        r = lettering(m)
        if r is None:
            print('lettering: "%s": a character without a glyph, left out' % m)
            continue
        imgs.append((1, r[0], r[1], r[2], m))
        print('lettering: "%s" -> %s, %d x %d' % (m, r[3], r[0].shape[1], r[0].shape[0]))
    b = pack(imgs)
    with open(out, 'wb') as f:
        f.write(b)
    print('wordmark: %d x %d, %s: %d B, %d images' % (cov.shape[1], cov.shape[0], out, len(b), len(imgs)))
    if rdir:
        render(rdir, imgs)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
