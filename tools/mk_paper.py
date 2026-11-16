#!/usr/bin/env python3
"""mk_paper.py - WindyFilm paper finishes (WindyStrife): the per-channel curves of the Paper row -> src/wf_paper_tab.h.

    python3 -I -B tools/mk_paper.py src/wf_paper_tab.h        # writes the header, prints the model's numbers
    python3 -I -B tools/mk_paper.py --check src/wf_paper_tab.h # 0 = the header is what this model makes

Our own model of a print on paper, from physical paper behaviour and published orders of magnitude only (glossy vs
matte surface Dmax, first-surface reflection, paper base white, image tone); no code or table of any other film module.

Linear light L per channel = the sRGB EOTF of the output code / 255, display-relative: L = 0 is the darkest the look
prints (a glossy print's Dmax), L = 1 its paper white. A finish is one curve per channel on that light:

  Glossy  identity. The builders skip the pass, so a Glossy look is the look of before, bit for bit.
  Matte   a matte (or lustre-matte) surface scatters its first-surface reflection, about 1 % of the light, into every
          direction, so it reaches the eye over the whole picture; a glossy surface sends it away as a specular glint
          under directional light. With s the part of the light reflected at the surface (white, not through the
          image) and 1 - s the part that goes through the image and back:
              L' = s + (1 - s) L
          s from the two surfaces' Dmax: 10^-Dm = 10^-Dg (1 - s) + s, Dg = 2.20 (glossy), Dm = 1.75 (matte; data
          sheets put glossy RC / fibre papers at about 2.1 - 2.3 and matte surfaces at about 1.6 - 1.8): s = 0.0116.
          Blacks lift, the shadows lose a little contrast and colour (the flare is white), white stays white (L = 1).
  Warm    a cream paper base and a warm (brown-black) image tone on a glossy surface:
  Cool    a blue-white base (optical brighteners) and a cold (blue-black) image tone:
              L'_c = w_c L_c^k_c
          w = the base's white relative to a neutral base, its largest channel 1 (paper white never clips); k = the
          image tone as a density scale per channel (D_c = k_c D, so the tone grows with density and is nil at white).
          w and k (k_G = 1) are solved for CIELAB targets (D65 white, sRGB primaries): the base white's a*, b*, then the
          colour of L = 0.05 (about D 1.3, the deep shadows) with the base included.

The header holds each curve at codes 0, 4, .., 256 (65 knots; code 256 continues the model past white) as output
code x 16. src/wf_loglut.c interpolates them linearly on code x 16 and continues the end segments outside 0..256
(R'G'B' of a table node outside the RGB cube). Max error of that interpolation against the model is printed (0.133
code, at the steep foot of the sRGB curve; under half a code everywhere). Matte is one curve for the three channels (a neutral stays neutral, exactly); Warm and Cool have three each.
"""
import math, sys

DG, DM = 2.20, 1.75                               # glossy / matte surface Dmax
WARM = dict(white=(0.6, 3.6), deep=(1.2, 3.0))    # CIELAB a*, b*: the base white, and L = 0.05 (base included)
COOL = dict(white=(0.6, -3.2), deep=(0.2, -2.8))
DEEP_L = 0.05
NK = 65                                           # knots: codes 0, 4, .., 256


def dec(x):                                       # sRGB EOTF, x = code / 255 (continued past 1 by the same formula)
    return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4


def enc(l):
    return 12.92 * l if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055


M = ((0.4124564, 0.3575761, 0.1804375), (0.2126729, 0.7151522, 0.0721750), (0.0193339, 0.1191920, 0.9503041))
WP = (0.95047, 1.0, 1.08883)


def lab(rgb):
    xyz = [sum(M[i][j] * rgb[j] for j in range(3)) / WP[i] for i in range(3)]
    f = [t ** (1 / 3) if t > 216 / 24389 else (24389 / 27 * t + 16) / 116 for t in xyz]
    return 116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])


def solve2(fn, target, x0):
    """2-D Newton on fn(x) -> (a, b), numeric Jacobian"""
    x = list(x0)
    for _ in range(50):
        a, b = fn(x)
        ea, eb = a - target[0], b - target[1]
        if abs(ea) < 1e-9 and abs(eb) < 1e-9:
            break
        h = 1e-6
        J = []
        for i in range(2):
            xp = list(x)
            xp[i] += h
            ap, bp = fn(xp)
            J.append(((ap - a) / h, (bp - b) / h))
        det = J[0][0] * J[1][1] - J[1][0] * J[0][1]
        dx0 = (ea * J[1][1] - eb * J[1][0]) / det
        dx1 = (eb * J[0][0] - ea * J[0][1]) / det
        x[0] -= dx0
        x[1] -= dx1
    return x


def toned(t, warm):
    """w (largest channel 1) and k (k_G = 1) for the targets t"""
    # base: warm = (1, g, b), cool = (r, g, 1)
    if warm:
        wf = lambda x: (1.0, x[0], x[1])
    else:
        wf = lambda x: (x[0], x[1], 1.0)
    xw = solve2(lambda x: lab(wf(x))[1:], t['white'], (0.98, 0.95) if warm else (0.97, 0.98))
    w = wf(xw)
    kf = lambda x: (x[0], 1.0, x[1])
    xk = solve2(lambda x: lab([w[c] * DEEP_L ** kf(x)[c] for c in range(3)])[1:], t['deep'], (1.0, 1.0))
    return w, kf(xk)


def curves():
    s = (10 ** -DM - 10 ** -DG) / (1 - 10 ** -DG)
    w_w, k_w = toned(WARM, True)
    w_c, k_c = toned(COOL, False)
    fns = [lambda l: s + (1 - s) * l]
    for w, k in ((w_w, k_w), (w_c, k_c)):
        for c in range(3):
            fns.append(lambda l, w=w[c], k=k[c]: w * l ** k)
    model = [lambda x, f=f: 255 * enc(f(dec(x / 255))) for f in fns]   # code -> code
    return s, (w_w, k_w), (w_c, k_c), model


def knots(model):
    return [[int(round(16 * m(4 * i))) for i in range(NK)] for m in model]


def interp(K, x16):                               # = src/wf_loglut.c wf_paper_ch
    i = x16 >> 6
    i = 0 if i < 0 else 63 if i > 63 else i
    f = x16 - (i << 6)
    return K[i] + (((K[i + 1] - K[i]) * f + 32) >> 6)


def header(K):
    names = ('matte', 'warm R', 'warm G', 'warm B', 'cool R', 'cool G', 'cool B')
    out = ['/* wf_paper_tab.h - generated by tools/mk_paper.py (the model and its numbers are in its docstring): the Paper',
           ' * row\'s channel curves, output code x 16 at codes 0, 4, .., 256. Do not edit. */',
           '#ifndef WF_PAPER_TAB_H', '#define WF_PAPER_TAB_H', '#include <stdint.h>',
           '#define WF_PP_NK %d' % NK,
           'static const int16_t wf_pp_knot[%d][WF_PP_NK] = {' % len(K)]
    for n, row in zip(names, K):
        out.append('    /* %-7s */ {' % n)
        for j in range(0, NK, 13):
            out.append('        ' + ', '.join('%4d' % v for v in row[j:j + 13]) + ',')
        out.append('    },')
    out += ['};',
            '/* the curve of each channel R, G, B per paper (Glossy has none) */',
            'static const uint8_t wf_pp_curve[4][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 1, 2, 3 }, { 4, 5, 6 } };',
            '#endif', '']
    return '\n'.join(out)


def main():
    args = sys.argv[1:]
    check = args and args[0] == '--check'
    if check:
        args = args[1:]
    if len(args) != 1:
        print(__doc__.split('\n\n')[0])
        return 2
    s, (w_w, k_w), (w_c, k_c), model = curves()
    K = knots(model)
    txt = header(K)
    err = 0.0
    for m, k in zip(model, K):
        for x16 in range(0, 4081):
            err = max(err, abs(interp(k, x16) / 16 - m(x16 / 16)))
    print('matte: s %.5f (Dg %.2f, Dm %.2f), black -> code %.1f, grey 118 -> %.1f, 200 -> %.1f, 255 -> %.1f'
          % (s, DG, DM, model[0](0), model[0](118), model[0](200), model[0](255)))
    for nm, w, k, base in (('warm', w_w, k_w, 1), ('cool', w_c, k_c, 4)):
        wc = [model[base + c](255) for c in range(3)]
        dc = [model[base + c](64) for c in range(3)]
        gc = [model[base + c](118) for c in range(3)]
        print('%s: w %.4f %.4f %.4f  k %.4f %.4f %.4f | white -> %.1f %.1f %.1f, 118 -> %.1f %.1f %.1f, 64 -> %.1f %.1f %.1f'
              % ((nm,) + tuple(w) + tuple(k) + tuple(wc) + tuple(gc) + tuple(dc)))
        print('      Lab white %s, deep (L %.2f) %s' % (' '.join('%.2f' % v for v in lab(w)), DEEP_L,
              ' '.join('%.2f' % v for v in lab([w[c] * DEEP_L ** k[c] for c in range(3)]))))
    print('knots: max interpolation error %.3f code' % err)
    if check:
        old = open(args[0], encoding='ascii').read()
        if old != txt:
            print('mk_paper: %s differs from the model' % args[0])
            return 1
        print('mk_paper: %s ok' % args[0])
        return 0
    with open(args[0], 'w', newline='\n', encoding='ascii') as fp:
        fp.write(txt)
    print('wrote', args[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
