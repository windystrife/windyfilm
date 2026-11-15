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

--icons OUT.h  also writes the 18 row icons (16 x 16, 1 bit, MSB first, 2 bytes per row) drawn with the same PIL calls
               as the mockup's icon() (Light, Print, Grain, Halation, Bloom, Gauge, Mist, Strength, Highlights, Shadows,
               Film LUT, and the film-stage knobs Film exp, Push, Exp comp, Printer R / G / B, Interlayer).
--check        renders sample strings with the pack's own algorithm and compares them with PIL (pixel mismatches).
--render DIR   writes a PNG sample sheet of the three fonts.
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
ICONS = ('Light', 'Print', 'Grain', 'Halation', 'Bloom', 'Gauge', 'Mist', 'Strength', 'Highlights', 'Shadows', 'Film LUT',
         'Film exp', 'Push', 'Exp comp', 'Printer R', 'Printer G', 'Printer B', 'Interlayer')


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
    elif k == 'Gauge':                             # a strip of film: the frame between two rows of perforations
        d.rectangle([x + 1, y + 0, x + 14, y + 15], outline=col)
        d.rectangle([x + 5, y + 4, x + 10, y + 11], outline=col)
        for yy in (2, 6, 10, 14):
            d.point((x + 3, y + yy), fill=col)
            d.point((x + 12, y + yy), fill=col)
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
    elif k == 'Film exp':                          # a square cut on its diagonal: plus above, minus below
        d.rectangle([x + 1, y + 1, x + 14, y + 14], outline=col)
        d.line([x + 14, y + 1, x + 1, y + 14], fill=col)
        d.line([x + 3, y + 5, x + 7, y + 5], fill=col)
        d.line([x + 5, y + 3, x + 5, y + 7], fill=col)
        d.line([x + 8, y + 10, x + 12, y + 10], fill=col)
    elif k == 'Push':                              # a double arrow, up and down, on a frame line
        d.line([x + 8, y + 1, x + 8, y + 14], fill=col)
        d.polygon([(x + 8, y + 0), (x + 4, y + 4), (x + 12, y + 4)], fill=col)
        d.polygon([(x + 8, y + 15), (x + 4, y + 11), (x + 12, y + 11)], fill=col)
        d.line([x + 2, y + 7, x + 5, y + 7], fill=col)
        d.line([x + 11, y + 7, x + 14, y + 7], fill=col)
    elif k == 'Exp comp':                          # a balance: a beam level on its pivot
        d.line([x + 1, y + 5, x + 14, y + 5], fill=col)
        d.polygon([(x + 8, y + 6), (x + 4, y + 13), (x + 12, y + 13)], outline=col)
        d.line([x + 1, y + 5, x + 1, y + 8], fill=col)
        d.line([x + 14, y + 5, x + 14, y + 8], fill=col)
        d.line([x + 2, y + 14, x + 13, y + 14], fill=col)
    elif k in ('Printer R', 'Printer G', 'Printer B'):   # three printer lights, the row's own one lit
        lit = ('Printer R', 'Printer G', 'Printer B').index(k)
        for i in range(3):
            box = [x + 1 + i * 5, y + 3, x + 4 + i * 5, y + 12]
            if i == lit:
                d.rectangle(box, fill=col)
            else:
                d.rectangle(box, outline=col)
        d.line([x + 1, y + 14, x + 14, y + 14], fill=col)
    elif k == 'Interlayer':                        # three emulsion layers, the effect crossing between them
        for yy in (2, 7, 12):
            d.rectangle([x + 1, y + yy, x + 14, y + yy + 2], outline=col)
        for xx in (5, 10):
            d.line([x + xx, y + 5, x + xx, y + 6], fill=col)
            d.line([x + xx, y + 10, x + xx, y + 11], fill=col)
    return im


def write_icons(path):
    out = ['/* wf_icons.h - generated by tools/mk_wffont.py --icons: the %d row icons of the browser, 16 x 16, 1 bit,'
           % len(ICONS),
           ' * MSB first, 2 bytes per row (our own drawing, the same PIL calls as the mockup\'s icon()). Do not edit. */',
           '#ifndef WF_ICONS_H', '#define WF_ICONS_H', '#include <stdint.h>',
           'static const uint8_t wf_icons[%d][32] = {' % len(ICONS)]
    for k in ICONS:
        im = icon_img(k)
        b = []
        for y in range(16):
            v = 0
            for x in range(16):
                if im.getpixel((x, y)):
                    v |= 0x8000 >> x
            b += [v >> 8, v & 0xFF]
        out.append('    /* %-10s */ {%s},' % (k, ', '.join('0x%02x' % v for v in b)))
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--icons')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--render')
    a = ap.parse_args()
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
    if a.check:
        sys.exit(1 if check(fonts) else 0)


if __name__ == '__main__':
    main()
