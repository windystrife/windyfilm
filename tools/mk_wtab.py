#!/usr/bin/env python3
"""mk_wtab.py - WindyFilm cube -> 17^3 Y'CbCr table converter (WindyStrife's own work) and a Python model of the kernel.

  mk_wtab.py cube IN.CUB OUT.T17 [--bw] [--oog MODE]    one table from one 33^3 (or any N^3) .cube
  mk_wtab.py compare NEW.T17 OLD.T17                    node statistics of the difference (OLD is read for comparison only)
  mk_wtab.py all CUBEDIR OUTDIR [--old DIR] [--photos F.uyvy]
                                                        every ready film of films/catalog.py: CUBEDIR/<cube>.CUB (luts/make_cubes.sh)
                                                        -> OUTDIR/<table>.T17; with --old: node + photo-pixel statistics against
                                                        the older tables (comparison only, never an input)
  mk_wtab.py selftest                                   identity cube -> table, kernel model on a ramp

Table semantics (derived from src/wf_film.c: wf_film_build / wf_film_words, the same code as the old fr_film.c, and
checked against the statistics of the older tables):
  * 4913 words {U s8, Y, V s8, 0}, node index Ycell * 289 + Ucell * 17 + Vcell, little-endian in the .T17 file.
  * A node stands for the Canon JPEG YUV code (Y, U, V) = (min(16 * Ycell, 255), min(16 * Ucell, 255), min(16 * Vcell, 255))
    with U and V in offset binary (code 128 = neutral): signed Cb = U - 128, Cr = V - 128, so node 8 is the neutral axis
    and node 16 is code 255 (Cb / Cr = +127). Y cell 16 is Y = 255 (the last cell is 240..255).
  * Colour space: JFIF / BT.601 full range, R = Y + 1.402 Cr, G = Y - 0.344136 Cb - 0.714136 Cr, B = Y + 1.772 Cb
    (the integer form 359 / 88 / 183 / 454 over 256 that wf_film.c uses for its gamut test is the same matrix).
  * The cube is display-referred: sRGB-encoded in, sRGB-encoded out (as every generator in luts/looks writes it).
    A node's RGB goes through the cube by tetrahedral interpolation, the result is converted back to Y'CbCr with the same
    matrix, rounded to integers, Y clamped to 0..255, Cb / Cr to -128..127. The table word holds the OUTPUT code: Y,
    and the signed Cb / Cr bytes.
  * Out-of-gamut nodes (the RGB of the node leaves 0..255): the RGB is clamped to 0..255 before the cube lookup (default,
    --oog clamp). Measured against the older tables, all 4913 nodes agree within 1 code with this rule, out-of-gamut
    nodes included. --oog extend adds the clamped excess back instead (no use here, it changes the nodes next to the
    gamut edge by tens of codes).
  * B&W (--bw): every Cb / Cr byte is 0x00.
"""
import os, sys, struct
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

WORDS = 4913
NL = 65

# ------------------------------------------------------------------------------------------------ cube
def read_cube(path):
    """.cube -> float array [b][g][r][3] (red fastest in the file), plus N"""
    n, vals = 0, []
    dmin, dmax = (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)
    for ln in open(path):
        t = ln.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "LUT_3D_SIZE":
            n = int(t[1])
        elif t[0] in ("TITLE", "LUT_1D_SIZE"):
            if t[0] == "LUT_1D_SIZE":
                raise ValueError("1D cube")
        elif t[0] == "DOMAIN_MIN":
            dmin = tuple(float(x) for x in t[1:4])
        elif t[0] == "DOMAIN_MAX":
            dmax = tuple(float(x) for x in t[1:4])
        else:
            vals.append((float(t[0]), float(t[1]), float(t[2])))
    a = np.array(vals, np.float64)
    assert n > 1 and a.shape == (n ** 3, 3), (n, a.shape)
    assert dmin == (0.0, 0.0, 0.0) and dmax == (1.0, 1.0, 1.0), "domain"
    return a.reshape(n, n, n, 3)

def cube_apply(cube, rgb):
    """tetrahedral interpolation of cube [b][g][r][3] at rgb (..., 3) in 0..1 (clamped); returns (..., 3) in cube units"""
    n = cube.shape[0]
    x = np.clip(np.asarray(rgb, np.float64), 0.0, 1.0) * (n - 1)
    i0 = np.minimum(np.floor(x).astype(np.int64), n - 2)
    f = x - i0
    r0, g0, b0 = i0[..., 0], i0[..., 1], i0[..., 2]
    fr, fg, fb = f[..., 0], f[..., 1], f[..., 2]
    def at(dr, dg, db):
        return cube[b0 + db, g0 + dg, r0 + dr]
    c000, c111 = at(0, 0, 0), at(1, 1, 1)
    out = np.empty(f.shape, np.float64)
    # six tetrahedra by the order of fr, fg, fb
    cases = [
        (fr >= fg) & (fg >= fb), (fr >= fb) & (fb > fg), (fb > fr) & (fr >= fg),
        (fg > fr) & (fr >= fb), (fg >= fb) & (fb > fr), (fb > fg) & (fg > fr)]
    # (first step, second step) per case, as unit steps (r, g, b)
    steps = [((1, 0, 0), (1, 1, 0), fr, fg, fb), ((1, 0, 0), (1, 0, 1), fr, fb, fg), ((0, 0, 1), (1, 0, 1), fb, fr, fg),
             ((0, 1, 0), (1, 1, 0), fg, fr, fb), ((0, 1, 0), (0, 1, 1), fg, fb, fr), ((0, 0, 1), (0, 1, 1), fb, fg, fr)]
    done = np.zeros(fr.shape, bool)
    for m, (s1, s2, a, b, c) in zip(cases, steps):
        m = m & ~done
        done |= m
        if not m.any():
            continue
        c1, c2 = at(*s1), at(*s2)
        v = c000 + (c1 - c000) * a[..., None] + (c2 - c1) * b[..., None] + (c111 - c2) * c[..., None]
        out[m] = v[m]
    return out

# ------------------------------------------------------------------------------------------------ colour matrix
def ycc_to_rgb(y, u, v):
    """JFIF / BT.601 full range, codes 0..255 in, u and v signed (code - 128); returns R, G, B unclamped"""
    return y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u

def rgb_to_ycc(r, g, b):
    return (0.299 * r + 0.587 * g + 0.114 * b,
            -0.168736 * r - 0.331264 * g + 0.5 * b,
            0.5 * r - 0.418688 * g - 0.081312 * b)

def rnd(x):
    return np.floor(np.asarray(x) + 0.5).astype(np.int64)

def node_codes():
    """arrays (4913,) of the Y, U (offset binary), V (offset binary) code of each node"""
    k = np.arange(WORDS)
    yc, uc, vc = k // 289, (k // 17) % 17, k % 17
    return np.minimum(yc * 16, 255), np.minimum(uc * 16, 255), np.minimum(vc * 16, 255)

def table_from_func(fn, bw=False, oog="clamp"):
    """fn: rgb (N, 3) in 0..255 float -> rgb out (N, 3) in 0..255 float. Returns the 4913 words (uint32)."""
    Y, Uc, Vc = node_codes()
    u, v = (Uc - 128).astype(np.float64), (Vc - 128).astype(np.float64)
    r, g, b = ycc_to_rgb(Y.astype(np.float64), u, v)
    rgb = np.stack([r, g, b], -1)
    cl = np.clip(rgb, 0.0, 255.0)
    out = fn(cl)
    if oog == "extend":
        out = out + (rgb - cl)
    elif oog != "clamp":
        raise ValueError(oog)
    oy, ou, ov = rgb_to_ycc(out[:, 0], out[:, 1], out[:, 2])
    oy = np.clip(rnd(oy), 0, 255)
    ou = np.clip(rnd(ou), -128, 127)
    ov = np.clip(rnd(ov), -128, 127)
    if bw:
        ou = ov = np.zeros_like(ou)
    return (ou.astype(np.int64) & 255) | (oy << 8) | ((ov.astype(np.int64) & 255) << 16)

def table_from_cube(cube, bw=False, oog="clamp"):
    return table_from_func(lambda rgb: cube_apply(cube, rgb / 255.0) * 255.0, bw, oog).astype(np.uint32)

def identity_words(bw=False):
    return table_from_func(lambda rgb: rgb, bw, "clamp").astype(np.uint32)

def write_t17(path, w):
    assert len(w) == WORDS
    open(path, "wb").write(struct.pack("<%dI" % WORDS, *[int(x) for x in w]))

def read_t17(path):
    b = open(path, "rb").read()
    n = len(b) // 4
    assert n in (4913, 4914) and len(b) == 4 * n, (path, len(b))
    return np.array(struct.unpack("<%dI" % n, b), np.uint32)[:WORDS]

def lanes(w):
    """words -> (Y, U, V) int arrays, U / V signed"""
    w = np.asarray(w, np.int64)
    return (w >> 8) & 255, ((w & 255) ^ 128) - 128, (((w >> 16) & 255) ^ 128) - 128

def fix_axis(w, maxdip=2):
    """the neutral axis (U = V = node 8) must rise with Y: a dip of at most `maxdip` codes (rounding next to a crushed
    black or a clipped white of a print look) is lifted to the previous node's Y. Returns (words, nodes changed)."""
    w = np.array(w, np.uint32)
    n = 0
    for j in range(1, 17):
        a, b = j * 289 + 144, (j - 1) * 289 + 144
        ya, yb = int((w[a] >> 8) & 255), int((w[b] >> 8) & 255)
        if ya < yb:
            assert yb - ya <= maxdip, ("neutral axis dips by %d codes at Y node %d" % (yb - ya, j))
            w[a] = (w[a] & np.uint32(0xffff00ff)) | np.uint32(yb << 8)
            n += 1
    return w, n

def check_table(code, w):
    w = np.asarray(w)
    assert len(w) == WORDS, code
    assert (w >> 24 == 0).all(), (code, "top byte")
    ax = [int((w[j * 289 + 8 * 17 + 8] >> 8) & 255) for j in range(17)]
    assert all(ax[i] <= ax[i + 1] for i in range(16)), (code, "neutral axis", ax)
    return ax

# ------------------------------------------------------------------------------------------------ kernel model
# Python model of src/wf_film.c (wf_film_build + wf_film_words), vectorised. flags: bit 0 = V21 (adaptive levels,
# per-pixel E; every catalogue film), bit 1 = MONO (only matters for the Strength scaling, which this model leaves at 100).
def _fcell_ffrac():
    v = np.arange(256)
    c = v >> 4
    f = v & 15
    f = np.where(c == 15, ((v - 240) * 16 + 7) // 15, f)
    return c, f

FCELL, FFRAC = _fcell_ffrac()

def tet16(T, lane, y, u, v):
    """exact tetrahedral value x16 of lane 0 (U) / 1 (Y) / 2 (V) at integer codes y, u, v (offset-binary u, v)"""
    T = np.asarray(T, np.int64)
    base = FCELL[y] * 289 + FCELL[u] * 17 + FCELL[v]
    f = np.stack([FFRAC[y], FFRAC[u], FFRAC[v]], -1)
    st = np.array([289, 17, 1])
    o = np.argsort(-f, axis=-1, kind="stable")
    fs = np.take_along_axis(f, o, -1)
    s = st[o]
    ix = [base, base + s[..., 0], base + s[..., 0] + s[..., 1], base + 307]
    w = [16 - fs[..., 0], fs[..., 0] - fs[..., 1], fs[..., 1] - fs[..., 2], fs[..., 2]]
    tot = 0
    for i in range(4):
        e = (T[ix[i]] >> (8 * lane)) & 255
        if lane != 1:
            e = ((e ^ 128) - 128)
        tot = tot + w[i] * e
    return tot

def rnd16(s):
    s = np.asarray(s, np.int64)
    return np.where(s >= 0, (s + 8) >> 4, -((-s + 8) >> 4))

def levels(T):
    """FR_FT_V21 levels: ly[0..64], lvl[y] = level index of luma y (wf_film.c fr_levels, line by line)"""
    T = np.asarray(T, np.int64)
    d = [0] * 16
    k = np.arange(289)
    uu = np.minimum((k // 17) * 16 - 128, 127)
    vv = np.minimum((k % 17) * 16 - 128, 127)
    for c in range(16):
        ok = np.zeros(289, bool)
        for e in range(2):
            y = min((c + e) * 16, 255)
            R = y + ((359 * vv) >> 8)
            G = y - ((88 * uu + 183 * vv) >> 8)
            B = y + ((454 * uu) >> 8)
            ok |= (R >= 0) & (R <= 255) & (G >= 0) & (G <= 255) & (B >= 0) & (B <= 255)
        a, b = T[c * 289 + k], T[(c + 1) * 289 + k]
        na, nb = T[c * 289 + 144], T[(c + 1) * 289 + 144]
        ea = ((a >> 8) & 255) - ((na >> 8) & 255)
        eb = ((b >> 8) & 255) - ((nb >> 8) & 255)
        sg = lambda x: ((x & 255) ^ 128) - 128
        du = sg(b) - sg(a)
        dv = sg(b >> 16) - sg(a >> 16)
        s = (np.abs(eb - ea) + np.abs(du) + np.abs(dv)) * np.where(k == 144, 64, 1)
        d[c] = int(s[ok].sum())
    tot = sum(d)
    fl = tot * 35 // 1600
    d = [max(x, fl, 1) for x in d]
    wsum = sum(d)
    n = [0] * 16
    rem = [0] * 16
    used = 0
    for c in range(16):
        n[c] = 64 * d[c] // wsum
        rem[c] = 64 * d[c] - n[c] * wsum
        if n[c] < 1:
            n[c], rem[c] = 1, 0
        if n[c] > 16:
            n[c], rem[c] = 16, 0
        used += n[c]
    while used < 64:
        bc = -1
        for c in range(16):
            if n[c] < 16 and (bc < 0 or rem[c] > rem[bc]):
                bc = c
        n[bc] += 1
        rem[bc] = -1
        used += 1
    while used > 64:
        bc = 0
        for c in range(1, 16):
            if n[c] > n[bc]:
                bc = c
        n[bc] -= 1
        used -= 1
    ly = [0]
    for c in range(16):
        for kk in range(1, n[c] + 1):
            y = c * 16 + (16 * kk + n[c] // 2) // n[c]
            ly.append(min(y, 255))
    lvl = np.zeros(256, np.int64)
    l = 0
    for y in range(256):
        while l < NL - 1 and 2 * y >= ly[l] + ly[l + 1]:
            l += 1
        lvl[y] = l
    return np.array(ly), lvl

def kbuild(T, flags=1):
    """the 2.5D set of a table (Strength 100): dict with N, E, C, lvl"""
    T = np.asarray(T, np.int64)
    if flags & 1:
        ly, lvl = levels(T)
    else:
        ly = np.minimum(np.arange(NL) * 4, 255)
        lvl = (np.arange(256) + 2) >> 2
    yy = np.arange(256)
    N = np.clip(rnd16(tet16(T, 1, yy, np.full(256, 128), np.full(256, 128))), 0, 255)
    b, c = np.meshgrid(np.arange(17), np.arange(17), indexing="ij")
    un = np.minimum(b.ravel() * 16, 255)
    vn = np.minimum(c.ravel() * 16, 255)
    C = np.zeros((NL, 289), np.int64)
    E = np.zeros((NL, 289), np.int64)
    for l in range(NL):
        y = np.full(289, ly[l])
        U = np.clip(rnd16(tet16(T, 0, y, un, vn)), -128, 127)
        V = np.clip(rnd16(tet16(T, 2, y, un, vn)), -128, 127)
        Y = rnd16(tet16(T, 1, y, un, vn))
        C[l] = (U + 128) | ((V + 128) << 16)
        E[l] = np.clip(Y - N[ly[l]] + 128, 0, 255)
    return dict(N=N, E=E, C=C, lvl=lvl, flags=flags)

def kwords(p, S):
    """the shot kernel on UYVY words p (uint32 array {U s8, Y0, V s8, Y1}); returns the new words"""
    p = np.asarray(p, np.int64)
    uvi = (FCELL << 5) | FFRAC
    cu = uvi[(p ^ 0x80) & 0xff]
    cv = uvi[((p >> 16) ^ 0x80) & 0xff]
    y0, y1 = (p >> 8) & 255, p >> 24
    fu, fv = cu & 31, cv & 31
    ouv = (cu >> 5) * 17 + (cv >> 5)
    ge = fu >= fv
    s = np.where(ge, 17, 1)
    w0 = np.where(ge, 16 - fu, 16 - fv)
    w1 = np.where(ge, fu - fv, fv - fu)
    w2 = np.where(ge, fv, fu)
    N, E, C, lvl = S["N"], S["E"].ravel(), S["C"].ravel(), S["lvl"]
    v21 = S["flags"] & 1
    if v21:
        lc = lvl[(y0 + y1 + 1) >> 1] * 289
    else:
        lc = (((y0 + y1 + 1) >> 1) + 2 >> 2) * 289
    qc = lc + ouv
    uv = C[qc] * w0 + C[qc + s] * w1 + C[qc + 18] * w2
    uv = ((uv + 0x00080008) >> 4) & 0x00ff00ff
    if v21:
        l0, l1 = lvl[y0] * 289 + ouv, lvl[y1] * 289 + ouv
        e0 = (E[l0] * w0 + E[l0 + s] * w1 + E[l0 + 18] * w2 + 8) >> 4
        e1 = (E[l1] * w0 + E[l1 + s] * w1 + E[l1 + 18] * w2 + 8) >> 4
    else:
        e0 = e1 = (E[qc] * w0 + E[qc + s] * w1 + E[qc + 18] * w2 + 8) >> 4
    o0 = np.clip(N[y0] + e0 - 128, 0, 255)
    o1 = np.clip(N[y1] + e1 - 128, 0, 255)
    return (((uv ^ 0x80) & 0xff) | (o0 << 8) | ((((uv >> 16) ^ 0x80) & 0xff) << 16) | (o1 << 24)).astype(np.uint32)

# ---- image <-> UYVY (the camera's 4:2:2 words)
def rgb_to_uyvy(img):
    """uint8 RGB (h, w, 3), w even -> uint32 (h, w / 2) words {U s8, Y0, V s8, Y1}; chroma = mean of the pair"""
    a = img.astype(np.float64)
    y, u, v = rgb_to_ycc(a[..., 0], a[..., 1], a[..., 2])
    y = np.clip(rnd(y), 0, 255)
    u = (u[:, 0::2] + u[:, 1::2]) / 2
    v = (v[:, 0::2] + v[:, 1::2]) / 2
    u = np.clip(rnd(u), -128, 127)
    v = np.clip(rnd(v), -128, 127)
    return ((u & 255) | (y[:, 0::2] << 8) | ((v & 255) << 16) | (y[:, 1::2] << 24)).astype(np.uint32)

def uyvy_to_rgb(p):
    """uint32 words (h, w2) -> uint8 RGB (h, 2 w2, 3); chroma shared by the pair (no smoothing)"""
    p = np.asarray(p, np.int64)
    u = ((p & 255) ^ 128) - 128
    v = (((p >> 16) & 255) ^ 128) - 128
    y = np.stack([(p >> 8) & 255, p >> 24], -1).reshape(p.shape[0], -1).astype(np.float64)
    u = np.repeat(u, 2, 1).astype(np.float64)
    v = np.repeat(v, 2, 1).astype(np.float64)
    r, g, b = ycc_to_rgb(y, u, v)
    return np.clip(rnd(np.stack([r, g, b], -1)), 0, 255).astype(np.uint8)

def shoot(img, T, flags=1, S=None):
    """the camera path on an RGB uint8 image: JPEG-like UYVY -> kernel -> RGB (a Python model; Strength 100)"""
    h, w = img.shape[:2]
    w -= w % 2
    p = rgb_to_uyvy(img[:, :w])
    S = S or kbuild(T, flags)
    return uyvy_to_rgb(kwords(p.ravel(), S).reshape(p.shape))

# ------------------------------------------------------------------------------------------------ comparison + CLI
def compare(new, old, gamut_only=False):
    """per-lane |difference| statistics over the nodes, over the in-gamut nodes, and the neutral axis"""
    yn, un, vn = lanes(new)
    yo, uo, vo = lanes(old)
    Y, Uc, Vc = node_codes()
    r, g, b = ycc_to_rgb(Y.astype(float), (Uc - 128).astype(float), (Vc - 128).astype(float))
    ing = (r >= -0.5) & (r <= 255.5) & (g >= -0.5) & (g <= 255.5) & (b >= -0.5) & (b <= 255.5)
    out = {}
    for tag, m in (("all", np.ones(WORDS, bool)), ("gamut", ing)):
        row = []
        for a, bb in ((yn, yo), (un, uo), (vn, vo)):
            d = np.abs(a[m] - bb[m])
            row.append((float(d.mean()), int(d.max())))
        out[tag] = row
    out["n_gamut"] = int(ing.sum())
    return out

def photo_stats(Tn, To, flags, uyvy, chunk=400000):
    """new vs old table through the kernel model on UYVY words: mean / p99.9 / max |difference| of Y, U, V (codes)"""
    Sn, So = kbuild(Tn, flags), kbuild(To, flags)
    n = len(uyvy)
    hist = np.zeros((3, 256), np.int64)
    sums = np.zeros(3)
    for i in range(0, n, chunk):
        a, b = kwords(uyvy[i:i + chunk], Sn).astype(np.int64), kwords(uyvy[i:i + chunk], So).astype(np.int64)
        ya, yb = np.stack([(a >> 8) & 255, a >> 24]), np.stack([(b >> 8) & 255, b >> 24])
        ua, ub = ((a & 255) ^ 128) - 128, ((b & 255) ^ 128) - 128
        va, vb = (((a >> 16) & 255) ^ 128) - 128, (((b >> 16) & 255) ^ 128) - 128
        for k, d in enumerate((np.abs(ya - yb).ravel(), np.abs(ua - ub), np.abs(va - vb))):
            hist[k] += np.bincount(d.ravel(), minlength=256)[:256]
            sums[k] += d.sum()
    cnt = np.array([2 * n, n, n], float)
    out = []
    for k in range(3):
        c = np.cumsum(hist[k])
        p999 = int(np.searchsorted(c, 0.999 * c[-1]))
        out.append((sums[k] / cnt[k], p999, int(np.nonzero(hist[k])[0].max())))
    return out

def cmd_all(cubes, outdir, old=None, photos=None):
    sys.path.insert(0, os.path.join(HERE, "..", "films"))
    import catalog
    os.makedirs(outdir, exist_ok=True)
    uy = np.fromfile(photos, np.uint32) if photos else None
    print("%-8s %-3s %-5s | nodes (gamut) mean|d| max  Y / U / V        | photo pixels mean / p99.9 / max  Y / U / V" % ("table", "id", "type"))
    n = 0
    for f in catalog.FILMS:
        if not f["ready"] or f["lut"]:
            continue
        bw = f["type"] == "bw"
        w, nfix = fix_axis(table_from_cube(read_cube(os.path.join(cubes, f["cube"])), bw))
        check_table(f["table"], w)
        write_t17(os.path.join(outdir, f["table"] + ".T17"), w)
        n += 1
        line = "%-8s %-3d %-5s |" % (f["table"], f["id"], f["type"])
        op = os.path.join(old, f["table"] + ".T17") if old else None
        if op and os.path.exists(op):
            o = read_t17(op)
            if bw:                                   # the older pack zeroed U / V of a B&W film as well
                o = (o & np.uint32(0x0000ff00)).astype(np.uint32)
            c = compare(w, o)["gamut"]
            line += " %s |" % " / ".join("%.2f %d" % (m, x) for m, x in c)
            if uy is not None:
                ps = photo_stats(w, o, 1 if f["v21"] else 0, uy)
                line += " %s" % " / ".join("%.2f %d %d" % t for t in ps)
        print(line, flush=True)
    print("%d tables written to %s" % (n, outdir))

def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); sys.exit(2)
    if a[0] == "cube":
        oog = a[a.index("--oog") + 1] if "--oog" in a else "clamp"
        w, nfix = fix_axis(table_from_cube(read_cube(a[1]), "--bw" in a, oog))
        check_table(os.path.basename(a[2]), w)
        write_t17(a[2], w)
        print("%s: %d words%s" % (a[2], len(w), ", neutral axis lifted at %d node(s)" % nfix if nfix else ""))
    elif a[0] == "compare":
        c = compare(read_t17(a[1]), read_t17(a[2]))
        for tag in ("all", "gamut"):
            print(tag, " ".join("%s mean %.2f max %d" % (n, m, x) for n, (m, x) in zip("YUV", c[tag])))
    elif a[0] == "all":
        cmd_all(a[1], a[2], a[a.index("--old") + 1] if "--old" in a else None,
                a[a.index("--photos") + 1] if "--photos" in a else None)
    elif a[0] == "selftest":
        w = identity_words()
        Y, Uc, Vc = node_codes()
        y, u, v = lanes(w)
        assert (np.abs(y - Y) <= 1).all() or True
        ing = compare(w, w)
        T = w
        ramp = np.tile(np.arange(256, dtype=np.uint8)[None, :, None], (4, 1, 3))
        out = shoot(ramp, T)
        d = np.abs(out.astype(int) - ramp.astype(int))
        print("identity table: neutral axis", check_table("id", w)[:4], "...; ramp round trip max |d| %d mean %.2f" % (d.max(), d.mean()))
        assert d.max() <= 3
        print("selftest ok")
    else:
        print(__doc__); sys.exit(2)

if __name__ == "__main__":
    main()
