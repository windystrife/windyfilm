#!/usr/bin/env python3
"""mk_osfont.py OUT.DAT - the film OS display fonts for the modernUI mockups (docs/MODERNUI.md), our own pack.

Same format and glyph code as WFFONTS.DAT (tools/mk_wffont.py, DESIGN 4.5): Ubuntu Sans variable (UFL 1.0), ASCII
32..126, 1 bit, 26.6 advances + kerning. Only the fence that places glyphs grows with the size (mk_wffont's fixed
24 px fence clips glyphs taller than that). The fonts, in this order:
  0: 20 px wght 600            titles, buttons, tile names
  1: 30 px wght 500            film names on Home and the shelf
  2: 46 px wght 400            exposure values (shutter, aperture, ISO) on the top plate and the HUD
  3: 80 px wght 300            the frame counter
  4: 12 px wght 700            small caps labels (letter-spaced by the renderer)
  5: 26 px wght 400            HUD values
Stage 2 (after approval) appends these to WFFONTS.DAT: wf_font.c already skips sizes it does not know."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
import mk_wffont as M

OSFONTS = ((20, 100, 600), (30, 100, 500), (46, 100, 400), (80, 100, 300), (12, 100, 700), (26, 100, 400))

_fenced = M.fenced
def fenced(f, s):
    """mk_wffont.fenced with the canvas and the offset grown to the font size"""
    from PIL import Image, ImageDraw
    O = max(24, int(f.size * 1.25))
    p64 = int(round(f.getlength(M.FENCE) * 64))
    if s:
        p64 += int(round((f.getlength(M.FENCE + s[0]) - f.getlength(M.FENCE) - f.getlength(s[0])) * 64))
    im = Image.new('L', (int(f.getlength(M.FENCE + s + M.FENCE[::-1])) + 2 * O, 3 * O), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = '1'
    d.text((O - (p64 & 63) / 64.0, O), M.FENCE + s + M.FENCE[::-1], font=f, fill=255)
    return im, O, O + (p64 >> 6)
M.fenced = fenced

def main():
    out = sys.argv[1]
    fonts = [M.build_font(px, wd, wg) for px, wd, wg in OSFONTS]
    for F in fonts:
        for adv, x, y, w, h, rows in F['gl']:
            assert -128 <= x < 128 and -128 <= y < 128 and w < 256 and h < 256 and adv < 65536
    blob = M.pack(fonts)
    open(out, "wb").write(blob)
    print("%s: %d fonts, %d B" % (os.path.basename(out), len(fonts), len(blob)))

if __name__ == "__main__":
    main()
