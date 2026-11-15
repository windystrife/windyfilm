#!/usr/bin/env python3
"""mk_wffont.py OUT.DAT [--icons OUT.h] [--check] [--render DIR] - WindyFilm's own UI font pack (DESIGN 4.5).

Glyphs: Ubuntu Sans variable (/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf, Ubuntu Font Licence 1.0),
ASCII 32..126, rendered 1-bit exactly as the browser mockup draws its small text (PIL, set_variation_by_axes([wdth,
wght]), ImageDraw fontmode '1'):
  font 0: 15 px, wght 500            (rows, description, tabs, loaded chip)
  font 1: 13 px, wght 500            (chips, footer, counter, "by WindyStrife")
  font 2: 13 px, wght 500, wdth 82   (strip names)
The browser's renderer (src/ui/wf_font.c) needs none of ML's dynamic font slots.

WFFONTS.DAT v1 (little endian, own format):
  header 16 B   "WFFN", u16 version 1, u16 nfonts, u32 file bytes, u32 FNV-1a of every byte after the header
  font dir      nfonts x 24 B: u8 px, u8 wdth, u16 wght, u8 height (ascent + descent), u8 ascent, u8 first, u8 count,
                u32 glyph table offset, u32 kern table offset, u16 nkern, u16 0, u32 0
  glyph table   count x 12 B: u16 advance (1/64 px), s8 x (left of the bitmap from the pen), s8 y (top of the bitmap
                from the cell top = PIL's ascender line), u8 w, u8 h, u8 bytes per row, u8 0, u32 bitmap offset
  kern table    nkern x 4 B, sorted by (left, right): u8 left, u8 right, s16 dx (1/64 px)
  bitmaps       rows top to bottom, MSB first, rows padded to whole bytes
Pen positions are 26.6 fixed point; a glyph is drawn at ((pen + 32) >> 6) + x, as PIL places mono glyphs.

--icons OUT.h  also writes the 10 row icons (16 x 16, 1 bit, MSB first, 2 bytes per row) drawn with the same PIL calls
               as the mockup's icon() (Light, Print, Grain, Halation, Bloom, Mist, Strength, Highlights, Shadows, Film LUT),
               and the 3 Darkroom print rows' icons (Frame, Info, Date) as wf_picons.
--check        renders sample strings with the pack's own algorithm and compares them with PIL (pixel mismatches).
--render DIR   writes a PNG sample sheet of the three fonts.
--print-only   OUT is that print font pack alone (build.sh's pack step: mk_wffont.py WFPRINT.DAT --print-only).
--print P.DAT  also writes the Darkroom's print font pack ML/DATA/WFPRINT.DAT (src/wf_print.c, DESIGN 3.9): the caption
               under a framed print, at print size and anti-aliased (16 levels), same font file:
  font 0: 84 px, wght 500   (line 1: camera / lens)
  font 1: 62 px, wght 400   (lines 2 / 3, the roll)
  WFPRINT.DAT v1 (little endian, own format):
  header 16 B   "WFPF", u16 version 1, u16 nfonts, u32 file bytes, u32 FNV-1a of every byte after the header
  font dir      nfonts x 24 B: u16 px, u16 wght, u8 wdth, u8 first (32), u8 count (96), u8 0, u16 ascent, u16 descent,
                u16 nkern, u16 0, u32 glyph table offset, u32 kern table offset
  glyph table   96 x 16 B (ASCII 32..126, then 127 = U+00B7 middle dot): u16 advance (1/64 px), s16 x (bitmap left from
                the pen), s16 y (bitmap top from the ascender line), u16 w, u16 h, u16 bytes per row, u32 bitmap offset
  kern table    nkern x 4 B, sorted by (left, right): u8 left, u8 right, s16 dx (1/64 px)
  bitmaps       rows top to bottom, 2 px per byte (high nibble first), coverage 0..15
  A glyph is drawn at ((pen + 32) >> 6) + x with pen in 26.6 fixed point, its top at baseline - ascent + y.
Deterministic: same inputs, same bytes."""
import sys, os, struct, argparse

FONT = '/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf'
FONTS = ((15, 100, 500), (13, 100, 500), (13, 82, 500))
FIRST, LAST = 32, 126


def fnv(b):
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def load(px, wdth, wght):
    from PIL import ImageFont
    f = ImageFont.truetype(FONT, px)
    f.set_variation_by_axes([wdth, wght])
    return f


FENCE = '| '        # PIL 10 places mono glyphs vertically by the rounded bbox of the whole string, so a glyph drawn
                    # alone can sit 1 px off the baseline; drawn between two bars (taller and deeper than any ASCII
                    # glyph) every glyph gets the same vertical origin, which is what a string renderer wants


def fenced(f, s):
    """PIL mono rendering of FENCE + s + FENCE[::-1], shifted so that s's pen lands on a whole pixel:
    (image, O, column of s's pen)"""
    from PIL import Image, ImageDraw
    O = 24
    p64 = int(round(f.getlength(FENCE) * 64))
    if s:
        p64 += int(round((f.getlength(FENCE + s[0]) - f.getlength(FENCE) - f.getlength(s[0])) * 64))
    im = Image.new('L', (int(f.getlength(FENCE + s + FENCE[::-1])) + 2 * O, 3 * O), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = '1'
    d.text((O - (p64 & 63) / 64.0, O), FENCE + s + FENCE[::-1], font=f, fill=255)
    return im, O, O + (p64 >> 6)


def columns(im):
    W, H = im.size
    d = im.load()
    return [x for x in range(W) if any(d[x, y] for y in range(H))]


def glyph(f, c):
    """bitmap of one glyph (PIL fontmode '1') drawn at a whole-pixel pen, relative to that pen and to the cell top
    (PIL's ascender line)"""
    im, O, gx = fenced(f, c)
    ref, _, _ = fenced(f, '')
    rc = columns(ref)                               # bare fence: left bar columns, then right bar columns
    half = (rc[0] + rc[-1]) / 2.0
    lbar = [x for x in rc if x < half]
    wbar = len([x for x in rc if x > half])
    cols = columns(im)
    xl = max(lbar) + 1
    xr = cols[-1] - wbar + 1                        # first column of the right bar
    W, H = im.size
    data = im.load()
    px = [(x, y) for y in range(H) for x in range(xl, xr) if data[x, y]]
    if not px:
        return 0, 0, 0, 0, []
    x0 = min(x for x, y in px); x1 = max(x for x, y in px) + 1
    y0 = min(y for x, y in px); y1 = max(y for x, y in px) + 1
    s = set(px)
    rows = [[1 if (x, y) in s else 0 for x in range(x0, x1)] for y in range(y0, y1)]
    return x0 - gx, y0 - O, x1 - x0, y1 - y0, rows


def build_font(px, wdth, wght):
    f = load(px, wdth, wght)
    asc, desc = f.getmetrics()
    gl = []
    for o in range(FIRST, LAST + 1):
        c = chr(o)
        adv = int(round(f.getlength(c) * 64))       # unhinted advance, 1/64 px (PIL positions glyphs with these)
        x, y, w, h, rows = glyph(f, c)
        gl.append((adv, x, y, w, h, rows))
    kern = []
    chars = [chr(o) for o in range(FIRST, LAST + 1)]
    single = {c: f.getlength(c) for c in chars}
    for a in chars:
        for b in chars:
            k = int(round((f.getlength(a + b) - single[a] - single[b]) * 64))
            if k:
                kern.append((ord(a), ord(b), k))
    return dict(px=px, wdth=wdth, wght=wght, asc=asc, desc=desc, gl=gl, kern=kern, f=f)


def pack(fonts):
    nf = len(fonts)
    dir_off = 16
    pos = dir_off + nf * 24
    blobs = []
    dirs = []
    for F in fonts:
        n = LAST - FIRST + 1
        g_off = pos
        pos += n * 12
        k_off = pos
        pos += len(F['kern']) * 4
        bm_parts = []
        recs = []
        bpos = pos
        for adv, x, y, w, h, rows in F['gl']:
            bpr = (w + 7) // 8
            data = bytearray()
            for r in rows:
                for bx in range(bpr):
                    v = 0
                    for i in range(8):
                        xx = bx * 8 + i
                        if xx < w and r[xx]:
                            v |= 0x80 >> i
                    data.append(v)
            recs.append(struct.pack('<HbbBBBBI', adv, x, y, w, h, bpr, 0, bpos if data else 0))
            bm_parts.append(bytes(data))
            bpos += len(data)
        kb = b''.join(struct.pack('<BBh', a, b, k) for a, b, k in sorted(F['kern']))
        blob = b''.join(recs) + kb + b''.join(bm_parts)
        assert len(blob) == bpos - g_off
        blobs.append(blob)
        pos = bpos
        dirs.append(struct.pack('<BBHBBBBIIHHI', F['px'], F['wdth'], F['wght'], F['asc'] + F['desc'], F['asc'], FIRST,
                                LAST - FIRST + 1, g_off, k_off, len(F['kern']), 0, 0))
    body = b''.join(dirs) + b''.join(blobs)
    total = 16 + len(body)
    hdr = b'WFFN' + struct.pack('<HHII', 1, nf, total, fnv(body))
    return hdr + body


def render_own(F, s):
    """the C renderer's algorithm (wf_font.c) in Python: returns a set of (x, y) pixels"""
    kern = {(a, b): k for a, b, k in F['kern']}
    pen, prev, px = 0, None, set()
    for c in s:
        o = ord(c)
        if not FIRST <= o <= LAST:
            continue
        if prev is not None:
            pen += kern.get((prev, o), 0)
        adv, x, y, w, h, rows = F['gl'][o - FIRST]
        gx = ((pen + 32) >> 6) + x
        for j, r in enumerate(rows):
            for i, v in enumerate(r):
                if v:
                    px.add((gx + i, y + j))
        pen += adv
        prev = o
    return px, (pen + 32) >> 6


def render_pil(F, s):
    """PIL's own rendering of s, fenced like the glyphs (see FENCE), relative to s's pen and the cell top"""
    f = F['f']
    im, O, pen = fenced(f, s)
    ref, _, _ = fenced(f, '')
    rc = columns(ref)
    half = (rc[0] + rc[-1]) / 2.0
    xl = max(x for x in rc if x < half) + 1
    wbar = len([x for x in rc if x > half])
    xr = columns(im)[-1] - wbar + 1
    gx = pen
    data = im.load()
    W, H = im.size
    return {(x - gx, y - O) for y in range(H) for x in range(xl, xr) if data[x, y]}


SAMPLES = ["Light", "Print", "Grain", "Halation", "Strength", "Film LUT", "Normal", "Tungsten", "Vision3 500T",
           "Consumer colour negative: golden cast,", "saturated colour, peach skin tones.", "Colour 15", "Cine 4",
           "B&W 11", "Film LUT 22", "Kodak Portra 160", "by WindyStrife", "COLOUR NEG", "ISO 200", "35mm", "V-Log",
           "SET", "Load", "L/R", "UP/DN", "MENU", "INFO", "5 / 52", "Portra 800", "Pro 400H", "Gold 200", "UltraMax",
           "Superia 400", "Ektar 100", "Color 800", "Tung Neutral", "Luca Bono", "2383 v3", "AVATAR To WAVY"]


def check(fonts):
    bad = 0
    for F in fonts:
        for s in SAMPLES:
            a, _ = render_own(F, s)
            b = render_pil(F, s)
            d = len(a ^ b)
            if d:
                bad += 1
                print('font %d/%d %r: %d pixels differ' % (F['px'], F['wdth'], s, d))
    print('check: %d of %d strings differ from PIL' % (bad, len(SAMPLES) * len(fonts)))
    return bad


# ------------------------------------------------------------------ row icons (mockup.py icon(), our own drawing)
ICONS = ('Light', 'Print', 'Grain', 'Halation', 'Bloom', 'Mist', 'Strength', 'Highlights', 'Shadows', 'Film LUT')


def icon_img(k):
    from PIL import Image, ImageDraw
    im = Image.new('1', (16, 16), 0)
    d = ImageDraw.Draw(im)
    x = y = 0
    col = 1
    if k == 'Light':
        d.ellipse([x + 4, y + 4, x + 11, y + 11], outline=col, width=2)
        for dx, dy in ((7, 0), (7, 14), (0, 7), (14, 7), (2, 2), (12, 2), (2, 12), (12, 12)):
            d.rectangle([x + dx, y + dy, x + dx + 1, y + dy + 1], fill=col)
    elif k == 'Print':
        d.rectangle([x + 2, y + 1, x + 13, y + 14], outline=col)
        d.polygon([(x + 9, y + 1), (x + 13, y + 5), (x + 9, y + 5)], fill=col)
        d.line([x + 4, y + 8, x + 11, y + 8], fill=col)
        d.line([x + 4, y + 11, x + 11, y + 11], fill=col)
    elif k == 'Grain':
        for dx, dy in ((2, 3), (8, 2), (12, 6), (5, 8), (10, 11), (3, 12), (13, 13), (7, 14)):
            d.rectangle([x + dx, y + dy, x + dx + 1, y + dy + 1], fill=col)
    elif k == 'Halation':
        d.ellipse([x + 1, y + 1, x + 14, y + 14], outline=col)
        d.ellipse([x + 5, y + 5, x + 10, y + 10], fill=col)
    elif k == 'Bloom':                             # a light in a soft glow: a solid core, then a dither thinning outwards
        for yy in range(16):
            for xx in range(16):
                r2 = (xx - 7.5) ** 2 + (yy - 7.5) ** 2
                if r2 <= 2.6 ** 2 or (r2 <= 4.9 ** 2 and (xx + yy) % 2 == 0) or (r2 <= 7.4 ** 2 and xx % 2 == 0 and yy % 2 == 0):
                    d.point((x + xx, y + yy), fill=col)
    elif k == 'Mist':
        for yy in (4, 8, 12):
            d.arc([x + 1, y + yy - 3, x + 8, y + yy + 3], 200, 340, fill=col)
            d.arc([x + 7, y + yy - 3, x + 14, y + yy + 3], 20, 160, fill=col)
    elif k == 'Strength':
        for i, hgt in enumerate((5, 9, 13)):
            d.rectangle([x + 2 + i * 5, y + 15 - hgt, x + 4 + i * 5, y + 15], fill=col)
    elif k in ('Highlights', 'Shadows'):           # a tone disc, the upper (Highlights) or lower (Shadows) half lit
        d.ellipse([x + 1, y + 1, x + 14, y + 14], outline=col)
        if k == 'Highlights':
            d.pieslice([x + 1, y + 1, x + 14, y + 14], 180, 360, fill=col)
        else:
            d.pieslice([x + 1, y + 1, x + 14, y + 14], 0, 180, fill=col)
        d.line([x + 1, y + 7, x + 14, y + 7] if k == 'Highlights' else [x + 1, y + 8, x + 14, y + 8], fill=col)
    elif k == 'Film LUT':
        d.polygon([(x + 8, y + 1), (x + 14, y + 4), (x + 14, y + 11), (x + 8, y + 14), (x + 2, y + 11), (x + 2, y + 4)],
                  outline=col)
        d.line([x + 8, y + 7, x + 8, y + 14], fill=col)
        d.line([x + 2, y + 4, x + 8, y + 7], fill=col)
        d.line([x + 14, y + 4, x + 8, y + 7], fill=col)
    return im


PICONS = ('Frame', 'Info', 'Date')               # the Darkroom's print rows (src/ui/wf_dark_ui.c)


def picon_img(k):
    from PIL import Image, ImageDraw
    im = Image.new('1', (16, 16), 0)
    d = ImageDraw.Draw(im)
    if k == 'Frame':                               # a print: border, the picture, a caption line under it
        d.rectangle([1, 1, 14, 14], outline=1)
        d.rectangle([3, 3, 12, 9], fill=1)
        d.line([3, 12, 9, 12], fill=1)
    elif k == 'Info':                              # three lines of text, the first one bold
        d.rectangle([2, 3, 13, 4], fill=1)
        d.line([2, 8, 10, 8], fill=1)
        d.line([2, 12, 12, 12], fill=1)
    elif k == 'Date':                              # two slanted seven-segment digits, "2" and "6"
        for ox, segs in ((1, 'abged'), (8, 'afgedc')):
            for s in segs:
                x0, y0, x1, y1 = {'a': (2, 2, 5, 2), 'b': (6, 3, 6, 6), 'c': (5, 9, 5, 12), 'd': (1, 13, 4, 13),
                                  'e': (0, 9, 0, 12), 'f': (1, 3, 1, 6), 'g': (1, 7, 5, 7)}[s]
                d.line([ox + x0, y0, ox + x1, y1], fill=1)
    return im


def icon_rows(im):
    b = []
    for y in range(16):
        v = 0
        for x in range(16):
            if im.getpixel((x, y)):
                v |= 0x8000 >> x
        b += [v >> 8, v & 0xFF]
    return b


def write_icons(path):
    out = ['/* wf_icons.h - generated by tools/mk_wffont.py --icons: the %d row icons of the browser, 16 x 16, 1 bit,'
           % len(ICONS),
           ' * MSB first, 2 bytes per row (our own drawing, the same PIL calls as the mockup\'s icon()). Do not edit. */',
           '#ifndef WF_ICONS_H', '#define WF_ICONS_H', '#include <stdint.h>',
           'static const uint8_t wf_icons[%d][32] = {' % len(ICONS)]
    for k in ICONS:
        out.append('    /* %-10s */ {%s},' % (k, ', '.join('0x%02x' % v for v in icon_rows(icon_img(k)))))
    out += ['};', '/* the Darkroom\'s print rows (Frame, Info, Date), same format */',
            'static const uint8_t wf_picons[%d][32] = {' % len(PICONS)]
    for k in PICONS:
        out.append('    /* %-10s */ {%s},' % (k, ', '.join('0x%02x' % v for v in icon_rows(picon_img(k)))))
    out += ['};', '#endif', '']
    with open(path, 'w', newline='\n') as fp:
        fp.write('\n'.join(out))


def sheet(fonts, d):
    from PIL import Image, ImageDraw
    os.makedirs(d, exist_ok=True)
    im = Image.new('RGB', (720, 60 + 30 * len(fonts) * 3), (12, 13, 16))
    y = 10
    for F in fonts:
        for s in (''.join(chr(o) for o in range(32, 80)), ''.join(chr(o) for o in range(80, 127)), SAMPLES[9]):
            px, w = render_own(F, s)
            for (x, yy) in px:
                if 0 <= 10 + x < 720:
                    im.putpixel((10 + x, y + yy), (230, 232, 236))
            y += 26
    im.save(os.path.join(d, 'wffonts_sheet.png'))


# ------------------------------------------------------------------ print font pack (WFPRINT.DAT, src/wf_print.c)
PRINT_FONTS = ((84, 100, 500), (62, 100, 400))
PRINT_CHARS = [chr(o) for o in range(32, 127)] + ['·']   # code 127 = the middle dot


def print_glyph(f, c):
    """anti-aliased glyph (16 levels) drawn at a whole-pixel pen, relative to the pen and to the ascender line"""
    from PIL import Image, ImageDraw
    O = f.size
    im = Image.new('L', (4 * O, 3 * O), 0)
    d = ImageDraw.Draw(im)
    d.text((O, O), c, font=f, fill=255, anchor='la')
    bb = im.getbbox()
    if not bb:
        return 0, 0, 0, 0, []
    x0, y0, x1, y1 = bb
    px = im.load()
    rows = [[(px[x, y] * 15 + 127) // 255 for x in range(x0, x1)] for y in range(y0, y1)]
    return x0 - O, y0 - O, x1 - x0, y1 - y0, rows


def build_print_font(px, wdth, wght):
    f = load(px, wdth, wght)
    asc, desc = f.getmetrics()
    gl = []
    for c in PRINT_CHARS:
        adv = int(round(f.getlength(c) * 64))
        gl.append((adv,) + print_glyph(f, c))
    single = {c: f.getlength(c) for c in PRINT_CHARS}
    kern = []
    for i, a in enumerate(PRINT_CHARS):
        for j, b in enumerate(PRINT_CHARS):
            k = int(round((f.getlength(a + b) - single[a] - single[b]) * 64))
            if k:
                kern.append((32 + i, 32 + j, k))
    return dict(px=px, wdth=wdth, wght=wght, asc=asc, desc=desc, gl=gl, kern=sorted(kern), f=f)


def pack_print(fonts):
    nf = len(fonts)
    pos = 16 + nf * 24
    dirs, blobs = [], []
    for F in fonts:
        g_off = pos
        pos += len(F['gl']) * 16
        k_off = pos
        pos += len(F['kern']) * 4
        bpos = pos
        recs, bms = [], []
        for adv, x, y, w, h, rows in F['gl']:
            bpr = (w + 1) // 2
            data = bytearray()
            for r in rows:
                for bx in range(bpr):
                    hi = r[2 * bx]
                    lo = r[2 * bx + 1] if 2 * bx + 1 < w else 0
                    data.append(hi << 4 | lo)
            recs.append(struct.pack('<HhhHHHI', adv, x, y, w, h, bpr, bpos if data else 0))
            bms.append(bytes(data))
            bpos += len(data)
        kb = b''.join(struct.pack('<BBh', a, b, k) for a, b, k in F['kern'])
        blobs.append(b''.join(recs) + kb + b''.join(bms))
        pos = bpos
        dirs.append(struct.pack('<HHBBBBHHHHII', F['px'], F['wght'], F['wdth'], 32, len(F['gl']), 0, F['asc'], F['desc'],
                                len(F['kern']), 0, g_off, k_off))
    body = b''.join(dirs) + b''.join(blobs)
    return b'WFPF' + struct.pack('<HHII', 1, nf, 16 + len(body), fnv(body)) + body


def print_render_own(F, s):
    """wf_print.c's algorithm in Python: {(x, y): coverage 0..15}, relative to the pen and the baseline"""
    kern = {(a, b): k for a, b, k in F['kern']}
    pen, prev, px = 0, None, {}
    for c in s:
        o = 127 if c == '·' else ord(c)
        if not 32 <= o <= 127:
            o = ord('?')
        if prev is not None:
            pen += kern.get((prev, o), 0)
        adv, x, y, w, h, rows = F['gl'][o - 32]
        gx = ((pen + 32) >> 6) + x
        for j, r in enumerate(rows):
            for i, v in enumerate(r):
                if v:
                    px[(gx + i, y - F['asc'] + j)] = max(v, px.get((gx + i, y - F['asc'] + j), 0))
        pen += adv
        prev = o
    return px, (pen + 32) >> 6


def print_check(fonts):
    """the pack's rendering against PIL's own (whole string, anchor at the baseline): mean / max coverage error"""
    from PIL import Image, ImageDraw
    worst = 0.0
    for F in fonts:
        for s in ('Canon EOS M  /  EF-M15-45mm f/3.5-6.3 IS STM', '45mm   f/6.3   1/320s   ISO 320',
                  'Portra 400  ·  Print Cine  ·  Grain Normal', 'ROLL 021 · 07/24', 'AVATAR To WAVY'):
            own, w = print_render_own(F, s)
            O = F['px']
            im = Image.new('L', (w + 4 * O, 3 * O), 0)
            ImageDraw.Draw(im).text((2 * O, 2 * O), s, font=F['f'], fill=255, anchor='ls')
            ref = im.load()
            err, n = 0, 0
            for y in range(im.size[1]):
                for x in range(im.size[0]):
                    a = own.get((x - 2 * O, y - 2 * O), 0) * 17
                    b = ref[x, y]
                    if a or b:
                        err += abs(a - b)
                        n += 1
            m = err / max(n, 1)
            worst = max(worst, m)
            print('print font %d px %r: mean |own - PIL| %.1f of 255 over %d px' % (F['px'], s, m, n))
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--icons')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--render')
    ap.add_argument('--print', dest='prt')
    ap.add_argument('--print-only', action='store_true', help='OUT is the print font pack (WFPRINT.DAT) alone')
    a = ap.parse_args()
    if a.print_only:
        pf =[build_print_font(*p) for p in PRINT_FONTS]
        pd = pack_print(pf)
        with open(a.out, 'wb') as fp:
            fp.write(pd)
        print('%s: %d bytes, %d print fonts, kern pairs %s' % (a.out, len(pd), len(pf), [len(F['kern']) for F in pf]))
        sys.exit(1 if a.check and print_check(pf) > 12 else 0)
    fonts = [build_font(*p) for p in FONTS]
    data = pack(fonts)
    with open(a.out, 'wb') as fp:
        fp.write(data)
    print('%s: %d bytes, %d fonts, kern pairs %s' % (a.out, len(data), len(fonts), [len(F['kern']) for F in fonts]))
    if a.icons:
        write_icons(a.icons)
        print('icons ->', a.icons)
    if a.render:
        sheet(fonts, a.render)
    bad = check(fonts) if a.check else 0
    if a.prt:
        pf = [build_print_font(*p) for p in PRINT_FONTS]
        pd = pack_print(pf)
        with open(a.prt, 'wb') as fp:
            fp.write(pd)
        print('%s: %d bytes, %d print fonts, kern pairs %s' % (a.prt, len(pd), len(pf), [len(F['kern']) for F in pf]))
        if a.check and print_check(pf) > 12:
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
