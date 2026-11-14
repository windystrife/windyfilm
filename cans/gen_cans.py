#!/usr/bin/env python3
"""cans/gen_cans.py - WindyFilm can drawing code (WindyStrife's own work): the film canisters as RGB + coverage.

Each film's canister is drawn in the colours and layout feel of a 35 mm film cassette with the film name printed on it
in plain bold sans (mixed case, anti-aliased; no logo glyphs, no bitmaps, everything is drawn here). Deterministic: no
clock, no randomness. This file only draws: it has NO palette and reads no pack of any other module. The palette
(our own median cut over our own cans) and the WFCANS.DAT pack are made by tools/mk_wfcans.py from render() below.

Cassette (our own geometry): a 35 mm cassette seen from the front. Glossy black plastic end caps slightly wider than
the body (rounded outer corners, soft rim highlight, dark underside), a spool knob on top and a short stub below, no
film leader. The body is a cylinder: dark at both edges, a broad soft highlight left of centre, a thin specular
streak; the printed label is shaded with it so it reads as wrapped round the can. A dark lip band on the left of the
body carries small rotated text in the film's accent colour (film type, "35mm"). The label art is drawn per film from
the STYLE table below (shapes in label units 122 x 142; text is mixed-case Ubuntu Sans, fitted to the label width).
Everything is rendered at 4x on a 170 x 240 design grid; render() area-averages it to any output size, blends the
edges with the colour the can sits on and marks pixels below 50% coverage as transparent. The small cans
(width < 60 px) leave the fine print out.

Films: DEFAULT_CODES must be exactly the ready rows of films/catalog.py (films() asserts it). Rows come from
catalog.py (id, ISO, type); a code without a row gets a synthetic row (STYLE experiments only, not in a release).
The catalogue directory: --catalog DIR, env WF_CATALOG_DIR, or this repo's films/. `--names` prints the printed texts.

Film LUT cans (catalog rows with `lut`): the same cassette, drawn in the colours of the film the cube emulates, with
the film name, a small rounded "LUT" tag at the foot of the label and "LOG + LUT" instead of "36 EXP." (STYLE keys
badge / foot_text).
"""
import os, sys, struct, hashlib
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CATALOG = os.path.join(HERE, "..", "films")        # this repo: catalog.py
BW, BH = 170, 240                       # the design grid the cassette is drawn on (render() resamples it)
SS = 4
FONT_UB = "/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf"
FONT_FALLBACK = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

_fonts = {}
def ub_font(size, wd, wg):
    k = (size, wd, wg)
    if k not in _fonts:
        try:
            f = ImageFont.truetype(FONT_UB, size)
            f.set_variation_by_axes([wd, wg])
        except Exception:
            f = ImageFont.truetype(FONT_FALLBACK, size)
        _fonts[k] = f
    return _fonts[k]

def fit_font(s, maxw, maxh, maxsize, wd, wg):
    """largest font (size in SS px, steps of 2) whose ink box for s fits maxw x maxh (label units)"""
    for sz in range(int(maxsize * SS), 7, -2):
        f = ub_font(sz, wd, wg)
        l, t, r, b = f.getbbox(s, anchor="ls")
        if r - l <= maxw * SS and b - t <= maxh * SS:
            break
    return f, (l, t, r, b)

# ------------------------------------------------------------------------------------------------ colour maths
def lin(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def oklab(rgb_lin):
    m1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929], [0.2119034982, 0.6806995451, 0.1073969566],
                   [0.0883024619, 0.2817188376, 0.6299787005]])
    m2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468], [1.9779984951, -2.4285922050, 0.4505937099],
                   [0.0259040371, 0.7827717662, -0.8086757660]])
    return np.cbrt(np.clip(rgb_lin, 0, None) @ m1.T) @ m2.T

def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h

# ------------------------------------------------------------------------------------------------ cassette geometry
# big-can units (170 x 240). Body 146 wide (x 12..158: lip 12..36, label 36..158), visible y 50..192.
CX, R = 85.0, 78.0                                # cylinder axis and radius of the body
BX0, BX1, BY0, BY1 = 7.0, 163.0, 40.0, 200.0
LIPW = 24.0
LWU, LHU = BX1 - BX0 - LIPW, BY1 - BY0            # label 132 x 160 (art is drawn on the 122 x 142 design grid and scaled)
DW, DH = 122, 142
CAPT, CAPB = (16.0, 40.0), (200.0, 224.0)         # end caps (y0, y1), x 3..167
KNOB, STUBY = (4.0, 20.0), (222.0, 232.0)
BLACK = (22, 22, 25)

def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)

_static = {}
def static_layers():
    """film independent layers at SS x: body shading (mult, add), under (knob, stub) and over (caps) grey level + mask"""
    if _static:
        return _static
    W, H = BW * SS, BH * SS
    x = (np.arange(W) + 0.5) / SS
    y = (np.arange(H) + 0.5) / SS
    X, Y = x[None, :], y[:, None]
    # body: a cylinder lit from the front left
    n = np.clip((x - CX) / R, -1, 1)
    diff = np.clip(-0.40 * n + 0.917 * np.sqrt(1 - n * n), 0, 1)
    kx = (0.38 + 0.68 * diff) * (1 - 0.34 * np.abs(n) ** 4) + 0.08 * np.exp(-((n - 0.80) / 0.13) ** 2)
    v = Y - BY0
    ao = (0.55 + 0.45 * sstep(0, 7, v)) * (0.55 + 0.45 * sstep(0, 7, LHU - v))
    _static["bk"] = kx[None, :] * ao
    sv = sstep(0, 16, v) * sstep(0, 16, LHU - v)
    spec = 0.30 * np.exp(-((n + 0.47) / 0.032) ** 2) + 0.07 * np.exp(-((n + 0.28) / 0.20) ** 2)
    _static["ba"] = spec[None, :] * (0.35 + 0.65 * sv)
    _static["bm"] = (X >= BX0) & (X < BX1) & (Y >= BY0 - 4) & (Y < BY1 + 4)

    def part(x0, x1, y0, y1, r_out, r_in, top, base, rimk):
        cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
        px, py = X - cx, Y - cy
        outer = (py < 0) if top else (py > 0)
        r = np.where(outer, r_out, r_in)
        qx, qy = np.abs(px) - hx + r, np.abs(py) - hy + r
        sdf = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
        m = sdf < 0
        depth = -sdf
        nn = np.clip(px / hx, -1, 1)
        dc = np.clip(-0.40 * nn + 0.917 * np.sqrt(1 - nn * nn), 0, 1)
        fade = sstep(y0, y0 + 8, Y) * sstep(y1, y1 - 8, Y)
        c = base * (0.55 + 0.9 * dc) + 0.075 * np.exp(-((nn + 0.30) / 0.24) ** 2) \
            + 0.34 * np.exp(-((nn + 0.50) / 0.040) ** 2) * (0.5 + 0.5 * sstep(0, 5, depth)) * (0.6 + 0.4 * fade)
        c = c + 0.012 * (1 - sstep(y0, y1, Y))
        w = np.where(outer, 1.0, 0.22) * rimk
        c = c + 0.55 * np.exp(-(depth / 1.05) ** 2) * w * (0.55 + 0.45 * (1 - nn) / 2)
        c = c * (1 - 0.5 * sstep(y1 - 5.0, y1 - 0.8, Y))
        return c, m

    ucol = np.zeros((H, W)); um = np.zeros((H, W), bool)
    over = np.zeros((H, W)); om = np.zeros((H, W), bool)
    kb = 0.0115
    c, m = part(CX - 22, CX + 22, KNOB[0], KNOB[1] + 6, 5.5, 1.0, True, kb * 1.5, 1.0)
    ucol[m] = c[m]; um |= m
    c, m = part(CX - 16, CX + 16, STUBY[0], STUBY[1], 4.0, 1.0, False, kb * 1.5, 0.7)
    ucol[m & ~um] = c[m & ~um]; um |= m
    c, m = part(CX - 82, CX + 82, CAPT[0], CAPT[1], 9.0, 2.5, True, kb, 1.0)
    over[m] = c[m]; om |= m
    c, m = part(CX - 82, CX + 82, CAPB[0], CAPB[1], 9.0, 2.5, False, kb, 0.55)
    over[m] = c[m]; om |= m
    _static["u"], _static["um"], _static["o"], _static["om"] = ucol, um, over, om
    return _static

# ------------------------------------------------------------------------------------------------ label drawing
def Rc(x0, y0, x1, y1, cs, a=1.0):
    return ("r", (x0, y0, x1, y1), cs, a)
def Pg(pts, cs, a=1.0):
    return ("p", pts, cs, a)

def grad_arr(cs, w, h):
    """colour spec -> (h, w, 3) float: flat colour, ("v", c0, c1) top to bottom, ("h", c0, c1), ("d", c0, c1) diagonal"""
    if len(cs) == 3 and isinstance(cs[0], str):
        k, c0, c1 = cs
        yy, xx = np.mgrid[0:h, 0:w]
        t = {"v": yy / (h - 1.0), "h": xx / (w - 1.0), "d": xx / (w - 1.0) * 0.45 + yy / (h - 1.0) * 0.55}[k]
        return np.array(c0, np.float64) * (1 - t[..., None]) + np.array(c1, np.float64) * t[..., None]
    return np.broadcast_to(np.array(cs, np.float64), (h, w, 3))

def paint(art, shape):
    """composite one label shape (label units) onto the PIL image art, alpha supported"""
    kind, g, cs, a = shape
    w, h = art.size
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(m)
    if kind == "r":
        d.rectangle([g[0] * SS, g[1] * SS, g[2] * SS - 1, g[3] * SS - 1], fill=255)
    else:
        d.polygon([(x * SS, y * SS) for x, y in g], fill=255)
    if a < 1.0:
        m = m.point(lambda v: int(v * a))
    art.paste(Image.fromarray(grad_arr(cs, w, h).round().astype(np.uint8)), (0, 0), m)

def put_text(art, s, box, col, maxsize, wd=100, wg=800, sh=None, glow=None, align="c"):
    """ink of s centred in box (x0, y0, x1, y1) label units, fitted to the box (never larger than it)"""
    if not s:
        return
    x0, y0, x1, y1 = box
    f, (l, t, r, b) = fit_font(s, x1 - x0, y1 - y0, maxsize, wd, wg)
    cx = (x0 + x1) / 2.0 * SS - (l + r) / 2.0 if align == "c" else x0 * SS - l
    cy = (y0 + y1) / 2.0 * SS - (t + b) / 2.0
    if glow is not None:
        gm = Image.new("L", art.size, 0)
        ImageDraw.Draw(gm).text((cx, cy), s, font=f, fill=255, anchor="ls")
        gm = gm.filter(ImageFilter.GaussianBlur(2.6 * SS)).point(lambda v: min(255, int(v * 2.2)))
        art.paste(Image.new("RGB", art.size, glow), (0, 0), gm)
    d = ImageDraw.Draw(art)
    if sh is not None:
        d.text((cx + 0.9 * SS, cy + 1.1 * SS), s, font=f, fill=sh, anchor="ls")
    d.text((cx, cy), s, font=f, fill=col, anchor="ls")

LAY = dict(maker=(8, 31), name=(33, 76), iso=(76, 122), maxw=100)
TYPE_TEXT = {"neg": "colour negative", "slide": "slide", "bw": "black & white"}

def body_art(st, ftype, small):
    """lip + label as one sRGB image (146 x 142 units at SS): uint8 array"""
    lay = dict(LAY, **st.get("lay", {}))
    W, H = DW * SS, DH * SS
    art = Image.new("RGB", (W, H), (0, 0, 0))
    art.paste(Image.fromarray(grad_arr(st["bg"], W, H).round().astype(np.uint8)), (0, 0))
    for s_ in st["deco"]:
        paint(art, s_)
    edge = st["lip"]
    paint(art, Rc(0, 0, 5.5, DH, tuple(int(c * 0.92) for c in edge)))
    paint(art, Rc(5.5, 0, 6.6, DH, tuple(int(c * 0.55) for c in edge)))
    mw = lay["maxw"]
    cxl = DW / 2.0 + 0.5
    def box(y0, y1, w=mw):
        g = 2.5 if y1 - y0 > 6 else 0.0
        return (cxl - w / 2.0, y0 + g, cxl + w / 2.0, y1 - g)
    mk = st["maker"]
    put_text(art, mk[0], box(*lay["maker"]), mk[1], 30, wg=760)
    lines = st["lines"]
    iso0 = st["iso"][0]
    if lines and not (small and iso0):
        y0, y1 = lay["name"]
        tot = sum(l[2] if len(l) > 2 else 1.0 for l in lines)
        y = y0
        for l in lines:
            hh = (y1 - y0) * (l[2] if len(l) > 2 else 1.0) / tot
            put_text(art, l[0], box(y, y + hh), l[1], 40, sh=st.get("nsh"), glow=st.get("glow"))
            y += hh
    iso = st["iso"]
    put_text(art, iso[0], box(*lay["iso"], w=st.get("isow", mw)), iso[1], 66, sh=st.get("isosh"))
    if not small:
        for e in st.get("extra", []):
            put_text(art, e[0], box(e[2], e[3], e[4] if len(e) > 4 else mw), e[1], 14, wg=700)
        put_text(art, st.get("foot_text", "36 EXP."), (9, DH - 15, 62, DH - 5), st["foot"], 11, wg=760, align="l")
    bd = st.get("badge")
    if bd:                                             # the Film LUT tag (V10): rounded box, its text fitted inside
        x0, y0, x1, y1 = bd["box"]
        ImageDraw.Draw(art).rounded_rectangle([x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1], radius=int(3.2 * SS),
                                              fill=bd["bg"], outline=bd["edge"], width=int(0.9 * SS))
        put_text(art, bd["text"], (x0 + 3, y0 + 2, x1 - 3, y1 - 2), bd["fg"], 14, wg=820)
    # the lip: a dark band with the film type and 35mm in the accent colour, reading bottom to top
    art = art.resize((int(LWU * SS), int(LHU * SS)), Image.LANCZOS)
    H = int(LHU * SS)
    lip = Image.new("RGB", (int(LIPW * SS), H), BLACK)
    if not small:
        tt = st.get("side", TYPE_TEXT[ftype])
        f, (l, t, r, b) = fit_font(tt, 92, 12, 14, 100, 700)
        f2, (l2, t2, r2, b2) = fit_font(st.get("side35", "35mm") or " ", 40, 12, 14, 100, 700)
        strip = Image.new("L", (H, int(LIPW * SS)), 0)
        d = ImageDraw.Draw(strip)
        cy = 13.5 * SS - (t + b) / 2.0
        if st.get("side35", "35mm"):
            d.text((7 * SS - l2, cy), st.get("side35", "35mm"), font=f2, fill=255, anchor="ls")
        end = 7 * SS + (r2 - l2) + 12 * SS if st.get("side35", "35mm") else 7 * SS
        d.text((end - l, cy), tt, font=f, fill=255, anchor="ls")
        strip = strip.rotate(90, expand=True)          # counter-clockwise: the text starts at the bottom
        lip.paste(Image.new("RGB", lip.size, st["lip"]), (0, 0), strip)
    full = Image.new("RGB", (int((LIPW + LWU) * SS), H))
    full.paste(lip, (0, 0))
    full.paste(art, (int(LIPW * SS), 0))
    return np.asarray(full)

def compose(st, ftype, small=False):
    """the cassette at SS x the output grid: linear RGB (H, W, 3) and alpha (H, W)"""
    L = static_layers()
    W, H = BW * SS, BH * SS
    col = np.zeros((H, W, 3))
    a = np.zeros((H, W))
    col[L["um"]] = L["u"][L["um"]][:, None]
    a[L["um"]] = 1.0
    art = lin(body_art(st, ftype, small).astype(np.float64))
    full = np.zeros((H, W, 3))
    y0, x0 = int(BY0 * SS), int(BX0 * SS)
    full[y0:y0 + art.shape[0], x0:x0 + art.shape[1]] = art
    xs = (np.arange(W) + 0.5) / SS
    seam = 1 - 0.45 * np.exp(-((xs - (BX0 + LIPW + 0.8)) / 0.8) ** 2)       # the label's edge casts a thin shadow
    kk = L["bk"] * seam[None, :]
    body = np.clip(full * kk[..., None] + L["ba"][..., None], 0, 1)
    rows = (np.arange(H) + 0.5) / SS
    outside = (rows < BY0) | (rows >= BY1)                                  # body hidden under the caps
    body[outside] = lin(BLACK) * 0.5
    m = L["bm"]
    col[m] = body[m]
    a[m] = 1.0
    col[L["om"]] = L["o"][L["om"]][:, None]
    a[L["om"]] = 1.0
    return col, a

def downsample(col, a, w, h, bg=(24, 27, 33)):
    """area average, premultiplied, blended with the colour the can sits on (bg) by coverage: linear RGB (h, w, 3), coverage (h, w)"""
    pm = col * a[..., None]
    chans = [np.asarray(Image.fromarray(pm[..., i].astype(np.float32), "F").resize((w, h), Image.BOX)) for i in range(3)]
    al = np.asarray(Image.fromarray(a.astype(np.float32), "F").resize((w, h), Image.BOX))
    rgb = np.stack(chans, -1) + (1 - al)[..., None] * lin(bg)[None, None, :]
    return rgb, al

def delin(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)

_composed = {}
def render(f, w, h, bg=(24, 27, 33)):
    """film row f (from films()) drawn at w x h: (rgb8 (h, w, 3) uint8, opaque (h, w) bool). Widths under 60 px are
    the small can (fine print left out). Edge pixels are pre-blended with bg; coverage < 50 % is transparent."""
    small = w < 60
    k = (f["code"], small)
    if k not in _composed:
        _composed[k] = compose(f["style"], f["type"], small=small)
    col, a = _composed[k]
    rgb, al = downsample(col, a, w, h, bg)
    return (delin(rgb) * 255 + 0.5).astype(np.uint8), al >= 0.5

# ------------------------------------------------------------------------------------------------ inks
Y1, Y2, Y3 = (252, 214, 96), (250, 199, 60), (247, 186, 25)
OR, OR2, RD, RD2, RD3 = (234, 153, 24), (219, 93, 45), (216, 30, 30), (150, 14, 14), (110, 18, 38)
BK, BK2, WH, CR = (22, 22, 25), (44, 44, 50), (246, 246, 244), (246, 233, 215)
BL, BL2, GR, GR2, TL = (22, 94, 190), (12, 54, 150), (8, 158, 70), (20, 96, 58), (14, 118, 94)
SL, SL2, GY, SV, BRN = (84, 87, 99), (110, 112, 122), (160, 160, 162), (206, 202, 198), (82, 52, 34)
VI, MG = (112, 54, 148), (151, 85, 173)
PG, PG2 = (138, 194, 148), (104, 170, 124)
KB0, KB1 = (36, 36, 40), (14, 14, 16)                  # glossy black label tops
KG0, KG1 = (26, 26, 30), (8, 8, 10)                    # black label body

def V(c0, c1):
    return ("v", c0, c1)

def ribbon(c0, c1, y=128, rise=12, hi=None):
    """a straight diagonal band across the foot of the label (own geometry), optional light top edge"""
    s = [Pg([(0, y), (DW, y - rise), (DW, DH), (0, DH)], ("h", c0, c1))]
    if hi is not None:
        s.append(Pg([(0, y), (DW, y - rise), (DW, y - rise + 2.2), (0, y + 2.2)], hi))
    return s

def band(y0, y1, cs, line=None, lh=2.4):
    s = [Rc(0, y0, DW, y1, cs)]
    if line is not None:
        s.append(Rc(0, y1, DW, y1 + lh, line))
    return s

STYLE = {}
def S(code, name, mk, lines, iso, foot, lip, bg, deco=(), **kw):
    STYLE[code] = dict(name=name, maker=mk, lines=lines, iso=iso, foot=foot, lip=lip, bg=bg,
                       deco=[x for a in deco for x in (a if isinstance(a, list) else [a])], **kw)

TOP = dict(maker=(7, 30), name=(38, 78), iso=(78, 122))        # label with a top band
# ---- colour negative
S("PRT160", "Kodak Portra 160", ("Kodak", CR), [("Portra", BRN)], ("160", (217, 125, 55)), CR, (230, 150, 70),
  V((248, 236, 218), (238, 214, 184)),
  [band(0, 33, V((226, 140, 66), (208, 112, 44)), BRN), ribbon((217, 125, 55), (200, 100, 40), hi=(240, 170, 100))],
  lay=TOP)
S("PRT400", "Kodak Portra 400", ("Kodak", Y2), [("Portra", BK)], ("400", BK), Y2, Y3,
  V((252, 198, 52), (232, 152, 10)),
  [band(0, 33, V(KB0, KB1), RD), ribbon(BK, BK2, hi=(90, 90, 96))], lay=TOP)
S("PRT800", "Kodak Portra 800", ("Kodak", Y2), [("Portra", WH)], ("800", Y2), WH, Y2,
  V((226, 30, 30), (150, 12, 14)),
  [band(0, 33, V(RD3, (80, 12, 28)), Y2), ribbon(RD3, (86, 12, 30), hi=(235, 90, 70))], lay=TOP, isosh=(110, 18, 38))
S("PAS400", "Fuji Pro 400H", ("Fujifilm", WH), [("Pro", TL)], ("400H", TL), WH, (160, 226, 190),
  V((20, 130, 100), (10, 90, 70)),
  [band(0, 30, V((20, 110, 70), GR2)), Rc(7, 36, DW, 112, V((250, 250, 248), (226, 232, 228))),
   Rc(7, 112, DW, 114.4, (170, 214, 190)), ribbon((8, 84, 62), (4, 62, 48))],
  lay=dict(maker=(6, 28), name=(40, 72), iso=(70, 108), maxw=100), extra=[("ISO 400", WH, 114, 126, 50)], isow=96)
S("BLD100", "Kodak Ektar 100", ("Kodak", RD), [("Ektar", WH)], ("100", Y2), Y2, Y2,
  V((232, 100, 44), (176, 62, 30)),
  [band(0, 33, V((252, 214, 80), (246, 190, 30)), BK), ribbon((120, 30, 20), (90, 20, 18), hi=(240, 130, 70))],
  lay=TOP, nsh=(90, 28, 20), isosh=(110, 30, 20))
S("SUN200", "Kodak Gold 200", ("Kodak", Y2), [("Gold", RD)], ("200", RD2), Y2, RD,
  V((254, 220, 90), (246, 184, 24)),
  [band(0, 33, V((226, 34, 30), (176, 14, 18)), RD2), ribbon(RD, RD2, hi=(240, 100, 80))], lay=TOP)
S("SNP200", "Kodak ColorPlus 200", ("Kodak", RD), [("ColorPlus", WH)], ("200", Y2), Y2, RD,
  V((254, 214, 70), (246, 190, 30)),
  [Pg([(0, 78), (DW, 44), (DW, DH), (0, DH)], V((226, 36, 32), (160, 14, 18))),
   Pg([(0, 78), (DW, 44), (DW, 47), (0, 81)], (250, 120, 70))],
  lay=dict(maker=(8, 32), name=(84, 108), iso=(108, 130)), nsh=(120, 12, 14), isosh=(120, 12, 14), isow=84)
S("EVD400", "Kodak UltraMax 400", ("Kodak", RD), [("UltraMax", WH)], ("400", RD), RD2, Y2,
  V((254, 216, 76), (246, 188, 28)),
  [Rc(0, 46, DW, 98, V((24, 84, 196), (12, 50, 150))), Rc(0, 43, DW, 46, RD), Rc(0, 98, DW, 101, RD),
   ribbon((246, 180, 20), (240, 150, 16), hi=(255, 230, 150))],
  lay=dict(maker=(8, 38), name=(50, 94), iso=(102, 126)), isow=84)
S("TRV400", "Fuji Superia X-TRA 400", ("Fujifilm", WH), [("Superia", WH), ("X-TRA", Y2)], ("400", WH), WH, Y2,
  V((12, 168, 76), (14, 100, 60)),
  [Pg([(0, 124), (DW, 112), (DW, 116.5), (0, 128.5)], WH), Pg([(0, 130), (DW, 118), (DW, 121), (0, 133)], RD)],
  lay=dict(maker=(7, 26), name=(30, 74), iso=(76, 114)), nsh=(8, 70, 40), isow=84)
S("POP800", "Lomo Color 800", ("Lomo", WH), [("Color", BK)], ("800", WH), BK, WH,
  V((250, 164, 26), (222, 104, 36)),
  [band(0, 33, V(KB0, KB1), WH), ribbon(BK, BK2, hi=(120, 120, 126))], lay=TOP, isosh=(60, 30, 10))
S("CIN50D", "Kodak Vision3 50D", ("Kodak", BK), [("Vision3", WH)], ("50D", Y3), WH, Y3,
  V((30, 30, 34), (8, 8, 10)),
  [Rc(0, 7, DW, 36, V((252, 208, 60), (244, 176, 20))), Rc(0, 39, DW, 41.5, RD)],
  lay=dict(maker=(8, 35), name=(50, 82), iso=(82, 124)))
S("TUN800", "CineStill 800T", ("CineStill", WH), [], ("800T", RD), WH, (255, 210, 200),
  V((226, 30, 30), (140, 10, 14)),
  [Rc(7, 34, DW, 100, V((250, 250, 248), (226, 226, 224))), Rc(7, 100, DW, 102.4, (250, 140, 120))],
  lay=dict(maker=(6, 28), iso=(40, 94), maxw=100), isow=94,
  extra=[("Tungsten", WH, 105, 114, 76), ("balanced", WH, 114, 123, 76)])
# ---- B&W
S("PRS400", "Kodak Tri-X 400", ("Kodak", Y2), [("Tri-X", BK)], ("400", BK), Y2, Y3,
  V((254, 220, 90), (244, 182, 22)),
  [band(0, 33, V(KB0, KB1), RD), ribbon(BK, BK2, hi=(90, 90, 96))], lay=TOP)
S("STR400", "Ilford HP5 Plus", ("Ilford", WH), [("HP5", BK, 2.2), ("Plus", BK, 1.0)], ("400", WH), WH, WH,
  V(KG0, KG1),
  [Rc(7, 34, DW, 100, V((250, 250, 248), (224, 224, 222))), Rc(7, 100, DW, 102.4, GY)],
  lay=dict(maker=(6, 30), name=(38, 96), iso=(103, 124)), isow=76)
S("CHM400", "Ilford XP2 Super", ("Ilford", WH), [("XP2", WH, 2.2), ("Super", WH, 1.0)], ("400", WH), WH, (120, 220, 190),
  V(KG0, KG1),
  [Rc(7, 34, DW, 104, V((18, 138, 110), (10, 96, 78))), Rc(7, 104, DW, 106.4, WH)],
  lay=dict(maker=(6, 30), name=(38, 98), iso=(108, 126)), isow=76)
S("FIN100", "Kodak T-Max 100", ("Kodak", BK), [("T-Max", WH)], ("100", Y3), WH, Y3,
  V((30, 30, 34), (8, 8, 10)),
  [band(0, 33, V((252, 200, 50), (240, 170, 12)), RD), ribbon((24, 24, 28), (12, 12, 14), hi=(250, 190, 40))], lay=TOP)
S("NT3200", "Kodak T-Max P3200", ("Kodak", Y2), [("T-Max", WH)], ("P3200", Y3), WH, Y3,
  V((30, 30, 34), (8, 8, 10)),
  [band(0, 33, V((226, 34, 30), (170, 14, 18)), Y3), ribbon((24, 24, 28), (12, 12, 14), hi=(226, 34, 30))], lay=TOP)
S("CRS100", "Ilford Delta 100", ("Ilford", WH), [("Delta", WH)], ("100", WH), WH, (120, 190, 255),
  V(KG0, KG1),
  [Rc(7, 34, DW, 108, V((30, 108, 210), (16, 62, 160))), Rc(7, 108, DW, 110.4, WH)],
  lay=dict(maker=(6, 30), name=(38, 72), iso=(72, 106)), isow=84)
S("SHP50", "Ilford Pan F Plus 50", ("Ilford", BK), [("Pan F", WH, 1.6), ("Plus", OR, 1.0)], ("50", OR), WH, OR,
  V(KG0, KG1),
  [band(0, 33, V((250, 170, 30), (226, 124, 12)), WH), ribbon((24, 24, 28), (12, 12, 14), hi=OR)],
  lay=dict(maker=(7, 30), name=(38, 82), iso=(84, 124)))
S("SLK100", "Fuji Acros 100 II", ("Fujifilm", WH), [("Acros", BK, 2.0), ("II", BK, 1.0)], ("100", WH), WH, SV,
  V(KG0, KG1),
  [Rc(7, 34, DW, 100, V((232, 230, 228), (178, 176, 174))), Rc(7, 100, DW, 103.4, RD)],
  lay=dict(maker=(6, 30), name=(38, 94), iso=(102, 124)), isow=76)
S("VTG100", "Fomapan 100", ("Fomapan", Y2), [], ("100", BK), BK, OR,
  V((250, 160, 20), (226, 118, 6)),
  [band(0, 33, V(KB0, KB1), WH), ribbon(BK, BK2, hi=(120, 120, 126))],
  lay=dict(maker=(7, 30), iso=(38, 112)), isow=100, extra=[("B&W film", BK, 114, 124, 60)])
S("CLS125", "Ilford FP4 Plus", ("Ilford", WH), [("FP4", WH, 2.2), ("Plus", WH, 1.0)], ("125", WH), WH, (110, 230, 160),
  V(KG0, KG1),
  [Rc(7, 34, DW, 104, V((14, 166, 76), (10, 110, 56))), Rc(7, 104, DW, 106.4, WH)],
  lay=dict(maker=(6, 30), name=(38, 98), iso=(108, 126)), isow=76)
S("BAS200", "Kentmere Pan 200", ("Kentmere", WH), [("Pan", BK)], ("200", RD), WH, RD,
  V((226, 224, 222), (186, 184, 184)),
  [band(0, 33, V((226, 34, 30), (176, 14, 18)), BK), ribbon((60, 60, 66), (30, 30, 34), hi=GY)],
  lay=dict(maker=(8, 30), name=(38, 78), iso=(78, 122)))
# ---- effect film
S("CRX125", "Harman Switch Azure", ("Harman", WH), [("Switch", WH, 1.2), ("Azure", Y2, 1.0)], ("125", WH), WH, (120, 220, 230),
  V((24, 80, 190), (10, 40, 130)),
  [Pg([(0, 100), (DW, 80), (DW, 94), (0, 114)], TL), Pg([(0, 116), (DW, 96), (DW, 99), (0, 119)], WH)],
  lay=dict(maker=(6, 28), name=(30, 72), iso=(72, 104)), isow=84)
# ---- Film LUT cans (V10): the film's colours, the name, the LUT tag (LUTB) at the foot, "LOG + LUT" as the foot
# text (not "S-Log3": the Load keeps a V-Log / LogC3 + LUT output that is set)
LUTB = dict(text="LUT", bg=(0, 150, 160), fg=WH, edge=(214, 246, 246), box=(70, 125, 117, 140))
S("LV250D", "Vision3 250D LUT", ("Kodak", BK), [("Vision3", WH)], ("250D", Y3), WH, Y3,
  V((30, 30, 34), (8, 8, 10)),
  [Rc(0, 7, DW, 36, V((252, 208, 60), (244, 176, 20))), Rc(0, 39, DW, 41.5, RD)],
  lay=dict(maker=(8, 35), name=(48, 80), iso=(80, 122)), badge=LUTB, foot_text="LOG + LUT")
S("LV500T", "Vision3 500T LUT", ("Kodak", WH), [("Vision3", WH)], ("500T", (150, 196, 255)), WH, (150, 196, 255),
  V((30, 30, 34), (8, 8, 10)),
  [Rc(0, 7, DW, 36, V((64, 120, 214), (34, 78, 176))), Rc(0, 39, DW, 41.5, RD)],
  lay=dict(maker=(8, 35), name=(48, 80), iso=(80, 122)), badge=LUTB, foot_text="LOG + LUT")
S("LPR400", "Portra 400 LUT", ("Kodak", Y2), [("Portra", BK)], ("400", BK), BK, Y3,
  V((252, 198, 52), (232, 152, 10)),
  [band(0, 33, V(KB0, KB1), RD), Rc(0, 120, DW, 122.4, BK)], lay=TOP, badge=LUTB, foot_text="LOG + LUT")
S("LFP400", "Pro 400H LUT", ("Fujifilm", WH), [("Pro", TL)], ("400H", TL), WH, (160, 226, 190),
  V((20, 130, 100), (10, 90, 70)),
  [band(0, 30, V((20, 110, 70), GR2)), Rc(7, 36, DW, 112, V((250, 250, 248), (226, 232, 228))),
   Rc(7, 112, DW, 114.4, (170, 214, 190))],
  lay=dict(maker=(6, 28), name=(40, 72), iso=(70, 108), maxw=100), isow=96, badge=LUTB, foot_text="LOG + LUT")
# V12: the owner's six V-Log cubes (catalog.py 67..72): print stocks as print cans (maker + "Print" + stock number),
# Ektar in its film's colours, the two graders' looks in neutral house colours with "Film LUT" as the maker line
S("LF3513", "Fuji 3513 LUT", ("Fujifilm", WH), [("Print", (150, 220, 200))], ("3513", WH), WH, (120, 220, 190),
  V((24, 70, 72), (8, 26, 30)),
  [Rc(0, 7, DW, 33, V((20, 130, 100), (10, 90, 70))), Rc(0, 36, DW, 38.4, TL)],
  lay=dict(maker=(8, 28), name=(42, 68), iso=(72, 116)), badge=LUTB, foot_text="LOG + LUT")
S("LK83V3", "2383 v3 LUT", ("Kodak", BK), [("Print v3", WH)], ("2383", Y3), WH, Y3,
  V((30, 30, 34), (8, 8, 10)),
  [Rc(0, 7, DW, 36, V((252, 208, 60), (244, 176, 20))), Rc(0, 39, DW, 41.5, RD)],
  lay=dict(maker=(8, 35), name=(48, 74), iso=(78, 118)), badge=LUTB, foot_text="LOG + LUT")
S("LK83V5", "2383 v5 LUT", ("Kodak", WH), [("Print v5", WH)], ("2383", OR), WH, OR,
  V((30, 30, 34), (8, 8, 10)),
  [Rc(0, 7, DW, 36, V((236, 120, 40), (212, 86, 22))), Rc(0, 39, DW, 41.5, Y3)],
  lay=dict(maker=(8, 35), name=(48, 74), iso=(78, 118)), badge=LUTB, foot_text="LOG + LUT")
S("LEKTR5", "Kodak Ektar LUT", ("Kodak", RD), [("Ektar", WH)], ("100", Y2), Y2, Y2,
  V((232, 100, 44), (176, 62, 30)),
  [band(0, 33, V((252, 214, 80), (246, 190, 30)), BK), Rc(0, 120, DW, 122.4, (120, 30, 20))],
  lay=TOP, nsh=(90, 28, 20), isosh=(110, 30, 20), badge=LUTB, foot_text="LOG + LUT")
S("LTUNGN", "Windy Neutral", ("Film LUT", SV), [("Windy", WH, 1.0), ("Neutral", SV, 1.0)], ("", WH), SV, (200, 206, 220),
  V((92, 95, 108), (58, 60, 70)),
  [band(0, 28, V((64, 66, 74), (44, 44, 50)), (200, 206, 220)), Rc(0, 116, DW, 118.4, SV)],
  lay=dict(maker=(5, 24), name=(36, 104), iso=(0, 1), maxw=100), badge=LUTB, foot_text="LOG + LUT")
S("LLBONO", "Luca Bono LUT", ("Film LUT", (236, 206, 166)), [("Luca", WH, 1.0), ("Bono", (240, 176, 110), 1.0)], ("", WH),
  (236, 206, 166), (240, 176, 110),
  V((104, 66, 44), (54, 32, 22)),
  [band(0, 28, V((70, 42, 28), (44, 26, 18)), (240, 176, 110)), Rc(0, 116, DW, 118.4, (240, 176, 110))],
  lay=dict(maker=(5, 24), name=(36, 104), iso=(0, 1), maxw=100), badge=LUTB, foot_text="LOG + LUT")
# V14T (owner 2026-11-13 "names starting with Tung"; "Windy" since 11-14, as Film Roll V14U): the twelve looks of the owner's film pack (catalog.py 66, 73..83)
# in the Tung Neutral house layout, our own colours (no film's packaging): one colour per stock family, P (2383 print)
# with a light rule under the top band, M (DI curve) without; the same LUT tag and foot
TUNG_FAM = {"250D": ((250, 170, 80), V((98, 62, 32), (50, 30, 16))), "500T": ((130, 176, 244), V((32, 46, 88), (12, 18, 40))),
            "EKTA": ((240, 130, 170), V((74, 28, 62), (30, 10, 26))), "EKTAa": ((196, 160, 244), V((54, 34, 88), (20, 12, 40))),
            "EXR": (Y2, V((44, 44, 48), (14, 14, 16))), "EXRa": ((120, 222, 204), V((22, 62, 62), (8, 24, 26)))}
for _code, _fam, _pm in (("LEKTAP", "EKTA", "P"), ("LT25DP", "250D", "P"), ("LT25DM", "250D", "M"), ("LT50TP", "500T", "P"),
                         ("LT50TM", "500T", "M"), ("LTEKTM", "EKTA", "M"), ("LTEKAP", "EKTAa", "P"), ("LTEKAM", "EKTAa", "M"),
                         ("LTEXRP", "EXR", "P"), ("LTEXRM", "EXR", "M"), ("LTEXAP", "EXRa", "P"), ("LTEXAM", "EXRa", "M")):
    _acc, _bg = TUNG_FAM[_fam]
    S(_code, "Windy %s %s" % (_fam, _pm), ("Film LUT", SV), [("Windy", WH, 1.0), ("%s %s" % (_fam, _pm), _acc, 1.0)], ("", WH),
      SV, _acc, _bg,
      [band(0, 28, V((64, 66, 74), (44, 44, 50)), _acc if _pm == "P" else None), Rc(0, 116, DW, 118.4, _acc)],
      lay=dict(maker=(5, 24), name=(36, 104), iso=(0, 1), maxw=100), badge=LUTB, foot_text="LOG + LUT")
# 11-14 "Leica Vivid" (catalog.py 84): the same house layout in our own colours, a graphite body with a warm silver /
# champagne accent; the name in our font only: no logo, no red dot, no script lettering, nothing of the maker's identity
LEICV_ACC = (226, 204, 160)
S("LLEICV", "Leica Vivid", ("Film LUT", SV), [("Leica", WH, 1.0), ("Vivid", LEICV_ACC, 1.0)], ("", WH), SV, LEICV_ACC,
  V((70, 72, 78), (26, 27, 30)),
  [band(0, 28, V((44, 45, 50), (30, 30, 34)), LEICV_ACC), Rc(0, 112, DW, 113.4, SV), Rc(0, 116, DW, 118.4, LEICV_ACC)],
  lay=dict(maker=(5, 24), name=(36, 104), iso=(0, 1), maxw=100), badge=LUTB, foot_text="STD + LUT",
  side="digital look", side35="")             # a look on the Standard picture, not a log / film look (owner 11-14)
# 14/11/2026, branch film-stage: the four films baked out of the declared exception's chain (catalog.py 85..88). Our own
# house layout: charcoal body, one accent per stock family, the name in our font, foot "35mm" - nothing of Kodak's or
# Fuji's packaging, the stock name only as the film's name.
for _c, _nm, _words, _iso, _acc, _bg in (
        ("GX500T", "Vision3 500T 2393", ("Vision3", "500T"), "500", (132, 176, 244), V((30, 44, 86), (12, 18, 40))),
        ("GX250D", "Vision3 250D 2383", ("Vision3", "250D"), "250", (250, 170, 80), V((96, 62, 32), (50, 30, 16))),
        ("GXP400", "Portra 400 2383", ("Portra", "2383"), "400", (226, 120, 150), V((72, 28, 52), (30, 10, 22))),
        ("GXE100", "Ektar 100 3513", ("Ektar", "3513"), "100", (196, 160, 244), V((54, 34, 88), (20, 12, 40)))):
    S(_c, _nm, ("WindyStrife", SV), [(_words[0], WH), (_words[1], _acc)], (_iso, WH), SV, _acc, _bg,
      [band(0, 30, V((64, 66, 74), (44, 44, 50)), _acc),
       ribbon((58, 58, 64), (40, 40, 46), hi=_acc)],
      lay=dict(maker=(6, 25), name=(34, 82), iso=(82, 124), maxw=100), foot_text="35mm")
# ---- shipped films
S("CHROME", "Classic Chrome", ("WindyStrife", (214, 208, 204)), [("Classic", WH), ("Chrome", SV)], ("200", WH), SV,
  (226, 120, 110), V((92, 95, 108), (62, 64, 74)),
  [band(0, 30, V((70, 70, 74), (50, 50, 54)), (190, 88, 80)), ribbon((60, 60, 64), (42, 42, 46), hi=(190, 88, 80))],
  lay=dict(maker=(6, 25), name=(34, 82), iso=(82, 124)))
S("CLSNEG", "Classic Neg", ("WindyStrife", WH), [("Classic", WH), ("Neg", Y2)], ("400", WH), WH, Y2,
  V((12, 168, 76), (14, 100, 60)),
  [Pg([(0, 124), (DW, 112), (DW, 116.5), (0, 128.5)], WH), Pg([(0, 130), (DW, 118), (DW, 121), (0, 133)], RD)],
  lay=dict(maker=(7, 26), name=(30, 74), iso=(76, 114)), nsh=(8, 70, 40), isow=84)
S("PRONEG", "Pro Neg Std", ("WindyStrife", WH), [("Pro Neg", GR2), ("Std", TL)], ("160", GR2), WH, (170, 230, 200),
  V(PG, PG2),
  [band(0, 28, V((20, 130, 100), (10, 94, 74)), WH), ribbon((14, 110, 90), (8, 80, 66), hi=(200, 240, 220))],
  lay=dict(maker=(6, 25), name=(34, 80), iso=(80, 122)))
S("ETERNA", "Eterna", ("WindyStrife", SV), [("Eterna", WH)], ("250", SV), SV, (80, 210, 190),
  V((44, 54, 58), (16, 16, 18)),
  [Rc(0, 31, DW, 33.4, TL), Rc(0, 124, DW, 126.4, OR)],
  lay=dict(maker=(8, 28), name=(42, 82), iso=(82, 122)))
S("CHUNGK", "Chungking 320", ("WindyStrife", MG), [("Chungking", (240, 60, 70))], ("320", PG), WH, (230, 130, 190),
  V((22, 44, 118), (8, 12, 14)),
  [Rc(0, 28, DW, 30.6, RD), Rc(0, 120, DW, 122.6, MG),
   Pg([(100, 36), (104, 36), (102, 118), (98, 118)], (200, 60, 90)),
   Pg([(10, 98), (16, 98), (24, 126), (18, 126)], TL)],
  lay=dict(maker=(8, 26), name=(34, 66), iso=(66, 118), maxw=96), nsh=(70, 10, 30), isosh=(20, 80, 60),
  glow=(160, 30, 50), isow=88)
S("C250D", "Cine 250D", ("WindyStrife", BK), [("Cine", BK, 1.0), ("250D", OR2, 1.0)], ("", BK), WH, OR,
  V((50, 50, 56), (24, 24, 28)),
  [Rc(0, 0, DW, 28, V((250, 164, 26), (226, 118, 12))), Rc(7, 36, DW, 104, V((250, 250, 248), (228, 228, 226))),
   Rc(7, 104, DW, 107, OR), ribbon((30, 30, 34), (18, 18, 20), hi=OR)],
  lay=dict(maker=(5, 24), name=(40, 100), iso=(0, 1), maxw=96), extra=[("Daylight", WH, 110, 122, 70)])

DEFAULT_CODES = ["PRT160", "PRT400", "PRT800", "PAS400", "BLD100", "SUN200", "SNP200", "EVD400", "TRV400", "POP800",
                 "CIN50D", "TUN800", "PRS400", "STR400", "CHM400", "FIN100", "NT3200", "CRS100", "SHP50", "SLK100",
                 "VTG100", "CLS125", "BAS200", "CRX125", "CHROME", "CLSNEG", "PRONEG", "ETERNA", "CHUNGK", "C250D",
                 "LV250D", "LV500T", "LPR400", "LFP400", "LF3513", "LK83V3", "LK83V5", "LEKTR5", "LTUNGN", "LLBONO", "LEKTAP",
                 "LT25DP", "LT25DM", "LT50TP", "LT50TM", "LTEKTM", "LTEKAP", "LTEKAM", "LTEXRP", "LTEXRM", "LTEXAP", "LTEXAM", "LLEICV",
                 "GX500T", "GX250D", "GXP400", "GXE100"]
# rows for codes the catalogue does not (yet) have: id, ISO, type; the shipped films' names as in catalog.py
SYNTH = {"CLS125": (125, "bw"), "BAS200": (200, "bw"), "CRX125": (125, "neg")}
FALLBACK_ROW = {"CHROME": (26, 200, "slide", "Classic Chrome"), "CLSNEG": (27, 400, "neg", "Classic Neg"),
                "PRONEG": (28, 160, "neg", "Pro Neg Std"), "ETERNA": (29, 250, "neg", "Eterna"),
                "CHUNGK": (30, 320, "neg", "Chungking 320"), "C250D": (31, 250, "neg", "Cine 250D")}
ISO_OF = {"PRT160": 160, "PRT400": 400, "PRT800": 800, "PAS400": 400, "BLD100": 100, "SUN200": 200, "SNP200": 200,
          "EVD400": 400, "TRV400": 400, "POP800": 800, "CIN50D": 50, "TUN800": 800, "PRS400": 400, "STR400": 400,
          "CHM400": 400, "FIN100": 100, "NT3200": 3200, "CRS100": 100, "SHP50": 50, "SLK100": 100, "VTG100": 100}

def load_catalog(a):
    d = a[a.index("--catalog") + 1] if "--catalog" in a else os.environ.get("WF_CATALOG_DIR", DEFAULT_CATALOG)
    sys.path.insert(0, d)
    import catalog
    return catalog

def films(a):
    cat = load_catalog(a)
    codes = a[a.index("--codes") + 1].split(",") if "--codes" in a else DEFAULT_CODES
    rows = {f["code"]: f for f in cat.FILMS}
    if "--codes" not in a:                         # the release: exactly the ready rows of the catalogue (build.sh)
        ready = sorted(f["code"] for f in cat.FILMS if f["ready"])
        assert sorted(codes) == ready, ("cans: DEFAULT_CODES differ from the ready rows of catalog.py",
                                        sorted(set(codes) ^ set(ready)))
    nxt, out, used = 58, [], {f["id"] for f in cat.FILMS}
    for c in codes:
        assert c in STYLE, ("no style for", c)
        if c in rows:
            r = rows[c]
            f = dict(id=r["id"], code=c, iso=r["iso"], type=r["type"], name=r["name"])
        elif c in FALLBACK_ROW:
            i, iso, t, n = FALLBACK_ROW[c]
            f = dict(id=i, code=c, iso=iso, type=t, name=n)
        else:
            while nxt in used:
                nxt += 1
            iso, t = SYNTH[c] if c in SYNTH else (ISO_OF[c], "neg")
            f = dict(id=nxt, code=c, iso=iso, type=t, name=STYLE[c]["name"])
            used.add(nxt)
        f["style"] = STYLE[c]
        out.append(f)
    return sorted(out, key=lambda f: f["id"])

def texts(f):
    st = f["style"]
    t = [st["maker"][0]] + [l[0] for l in st["lines"]] + [st["iso"][0]] + [e[0] for e in st.get("extra", [])]
    t += [st["badge"]["text"]] if st.get("badge") else []
    return [x for x in t if x] + [st.get("foot_text", "36 EXP."), TYPE_TEXT[f["type"]], "35mm"]


def main():
    a = sys.argv[1:]
    if "--names" in a:                             # every text printed on a can
        seen = []
        for f in films(a):
            for s in [f["style"]["name"]] + texts(f):
                if s not in seen:
                    seen.append(s)
        print("\n".join(seen))
        return
    print(__doc__)

if __name__ == "__main__":
    main()
