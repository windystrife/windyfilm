#!/usr/bin/env python3
"""mk_loglut_tab.py OUT.h [FILM1.csv|-] [NEUTRAL.csv|-] - constant tables for wf_loglut.c (WindyFilm, "Log + LUT").
The optional CSVs are the measured Canon Picture Style curves of the input curve slots 1 and 2 (DESIGN 3.2 A1).

Same camera model as wf_logout_ref.c / mk_logtab.py (BT.601 full-range YCbCr of a display-referred sRGB
JPEG -> sRGB EOTF -> Rec.709-primaries linear -> 3x3 to the log gamut -> log curve), in higher
precision, for the LOAD-TIME composite builder only (nothing here runs per pixel at the shot):

  wf_ll_dec[1022]    sRGB code value i/4 (0..255) -> linear light, Q24 (entry 1021 = entry 1020, so
                     the builder can always interpolate between i and i+1)
  wf_ll_mat[5][9]    linear Rec.709 -> linear target gamut, Q20, rows sum exactly to 2^20 (white kept)
  wf_ll_enc[5][4097] linear Q12 index -> log code * 256 (Q8 of the 8-bit code, 0..65280): the SAME
                     values as wf_lenc in mk_logtab.py (asserted below); the builder interpolates it.
                     With -DWF_LL_EXTERN_ENC the table is not emitted and wf_loglut.c uses wf_lenc
                     from wf_logout_tab.c instead (saves 24.6 KB in the module, see INTEGRATION.md).
  wf_ll_ycc[3][3]    R'G'B' (tetrahedral sums, see wf_loglut.c) -> Y, Cb, Cr x 2^48, BT.601 full range
  wf_ll_r10[27]      round(2^56 / 10^d): decimal fraction -> Q24 in the .cube number parser
  wf_ll_p10[10]      10^d
  wf_ll_cls[256]     character classes of the parser (blank / end of content / digit)

Also checks the gamut matrices against the published RGB->XYZ matrices (Sony S-Gamut3.Cine,
Panasonic V-Gamut, ARRI Wide Gamut 3; Canon publishes Cinema Gamut as primaries only, so it has no check)
and prints the log curves at black / 18 % / white.
Pure stdlib, deterministic.
"""
import math
import sys

D65 = (0.3127, 0.3290)
REC709 = [(0.640, 0.330), (0.300, 0.600), (0.150, 0.060)]
GAMUTS = [
    ("S-Gamut3.Cine", [(0.766, 0.275), (0.225, 0.800), (0.089, -0.087)]),
    ("V-Gamut", [(0.730, 0.280), (0.165, 0.840), (0.100, -0.030)]),
    ("ARRI Wide Gamut 3", [(0.6840, 0.3130), (0.2210, 0.8480), (0.0861, -0.1020)]),
    ("Cinema Gamut", [(0.7400, 0.2700), (0.1700, 1.1400), (0.0800, -0.1000)]),     # Canon Log 2 (mode 4)
    ("Cinema Gamut", [(0.7400, 0.2700), (0.1700, 1.1400), (0.0800, -0.1000)]),     # Canon Log 3 (mode 5)
]
# published normalized primary matrices (RGB -> CIE XYZ), D65
PUBLISHED = [
    # Sony, "Technical Summary for S-Gamut3.Cine/S-Log3 and S-Gamut3/S-Log3"
    [[0.5990839208, 0.2489255161, 0.1024464902],
     [0.2150758201, 0.8850685017, -0.1001443219],
     [-0.0320658495, -0.0276583907, 1.1487819910]],
    # Panasonic, "V-Log/V-Gamut Reference manual" (Nov. 2014)
    [[0.679644, 0.152211, 0.118600],
     [0.260686, 0.774894, -0.035580],
     [-0.009310, -0.004612, 1.102980]],
    # ARRI, "ALEXA Log C Curve - Usage in VFX" (2017-03), Wide Gamut -> XYZ
    [[0.638008, 0.214704, 0.097744],
     [0.291954, 0.823841, -0.115795],
     [0.002798, -0.067034, 1.153294]],
    None,                                       # Canon Cinema Gamut: primaries only (EOS C500 firmware notes)
    None,
]


def srgb_eotf(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def slog3(x):
    if x >= 0.01125000:
        return (420.0 + math.log10((x + 0.01) / (0.18 + 0.01)) * 261.5) / 1023.0
    return (x * (171.2102946929 - 95.0) / 0.01125000 + 95.0) / 1023.0


def vlog(x):
    if x < 0.01:
        return 5.6 * x + 0.125
    return 0.241514 * math.log10(x + 0.00873) + 0.598206


def logc3_ei800(x):
    cut, a, b, c, d, e, f = 0.010591, 5.555556, 0.052272, 0.247190, 0.385537, 5.367655, 0.092809
    return c * math.log10(a * x + b) + d if x > cut else e * x + f


def clog2(x):
    """Canon Log 2 v1.2 (Canon, input transform 202007, as colour-science log_encoding_CanonLog2): reflectance x
    (18 % grey = 0.18) / 0.9, normalised code value (legal range already in the constants): 0.18 -> 0.3983"""
    x = x / 0.9
    if x < 0:
        return -(0.24136077 * math.log10(-x * 87.09937546 + 1) - 0.092864125)
    return 0.24136077 * math.log10(x * 87.09937546 + 1) + 0.092864125


def clog3(x):
    """Canon Log 3 v1.2 (same source): linear segment |x / 0.9| <= 0.014, 0.18 -> 0.3434"""
    x = x / 0.9
    if x < -0.014:
        return -0.36726845 * math.log10(-x * 14.98325 + 1) + 0.12783901
    if x <= 0.014:
        return 1.9754798 * x + 0.12512219
    return 0.36726845 * math.log10(x * 14.98325 + 1) + 0.12240537


CURVES = [("S-Log3", slog3), ("V-Log", vlog), ("LogC3 EI800", logc3_ei800), ("Canon Log 2", clog2), ("Canon Log 3", clog3)]
assert len(GAMUTS) == len(PUBLISHED) == len(CURVES) == 5


def inverse(m):
    a, b, c = m[0]; d, e, f = m[1]; g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
            [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
            [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]


def mul(A, B):
    return [[sum(A[r][k] * B[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def npm(prim, white):
    def xyz(xy):
        x, y = xy
        return [x / y, 1.0, (1 - x - y) / y]
    P = [xyz(p) for p in prim]
    M = [[P[c][r] for c in range(3)] for r in range(3)]
    W = xyz(white)
    inv = inverse(M)
    S = [sum(inv[r][c] * W[c] for c in range(3)) for r in range(3)]
    return [[M[r][c] * S[c] for c in range(3)] for r in range(3)]


def rec709_to(prim):
    return mul(inverse(npm(prim, D65)), npm(REC709, D65))


def qmat(M, one):
    out = []
    for r in range(3):
        row = [int(round(M[r][c] * one)) for c in range(3)]
        row[r] += one - sum(row)
        out += row
    return out


def lenc_table(f):
    """identical to wf_lenc in mk_logtab.py"""
    return [int(round(min(max(f(i / 4096.0), 0.0), 1.0) * 255.0 * 256.0)) for i in range(4097)]


def read_curve(path):
    """luts/canon_curve/<STYLE>.csv (DESIGN 3.2 A1): '#' lines, a 'code,linear' header, 256 rows code 0..255,
    linear strictly increasing (code 0 may be 0), scene-linear with 18 % grey = 0.18, code 255 <= 4.0.
    Returns (256 values, the first '#' line)."""
    vals, title = {}, ""
    with open(path, encoding="ascii") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            if s.startswith("#"):
                title = title or s.lstrip("# ")
                continue
            if s.replace(" ", "").lower() == "code,linear":
                continue
            c, v = s.split(",")
            vals[int(c)] = float(v)
    assert sorted(vals) == list(range(256)), "%s: need codes 0..255" % path
    v = [vals[c] for c in range(256)]
    assert all(v[c + 1] > v[c] for c in range(255)), "%s: not strictly increasing" % path
    assert v[0] >= 0.0 and v[255] <= 4.0, "%s: linear range" % path
    return v, title


def emit_dec(L, name, dec, comment):
    L.append("static const uint32_t %s[1022] = {   /* %s */" % (name, comment))
    for i in range(0, 1022, 12):
        L.append("  " + ",".join(str(v) for v in dec[i:i + 12]) + ",")
    L.append("};")


CANON_SLOTS = 2        # input curve slots 1..2 (wf_loglut.h WF_LL_NINC - 1): 1 Canon Film 1, 2 Canon Neutral


def main():
    out = sys.argv[1]
    slots = sys.argv[2:]
    assert len(slots) <= CANON_SLOTS, "at most %d curve files ('-' = none)" % CANON_SLOTS
    slots += ["-"] * (CANON_SLOTS - len(slots))
    L = ["/* generated by mk_loglut_tab.py - do not edit */", "#include <stdint.h>", ""]
    dec = [int(round(srgb_eotf(min(i / 4.0, 255.0) / 255.0) * (1 << 24))) for i in range(1021)]
    dec.append(dec[1020])
    emit_dec(L, "wf_ll_dec", dec, "sRGB code i/4 -> linear Q24")
    # A1: the measured Canon Picture Style curves, code i/4 -> scene-linear Q24 (above 1.0 allowed: the builder takes
    # values over white through the log segment of each curve, see wf_ll_coords). Slot k: WF_LL_HAVE_C<k>,
    # WF_LL_C<k>_SRC (the CSV's first '#' line) and wf_ll_dec_c<k>, or WF_LL_HAVE_C<k> 0.
    for k, curve in enumerate(slots, 1):
        if curve == "-" or not curve:
            L.append("#define WF_LL_HAVE_C%d 0" % k)
            continue
        cv, title = read_curve(curve)
        decc = []
        for i in range(1021):
            c0 = min(i // 4, 254)
            t = i / 4.0 - c0
            decc.append(int(round((cv[c0] + (cv[c0 + 1] - cv[c0]) * t) * (1 << 24))))
        decc.append(decc[1020])
        assert all(decc[i + 1] >= decc[i] for i in range(1021)) and decc[1020] < (1 << 31)
        L.append("#define WF_LL_HAVE_C%d 1" % k)
        L.append('#define WF_LL_C%d_SRC "%s"' % (k, title.replace('"', "'").replace("\\", "/")[:100]))
        emit_dec(L, "wf_ll_dec_c%d" % k, decc, "input curve %d: code i/4 -> scene-linear Q24" % k)
        g = next(c for c in range(256) if cv[c] >= 0.18)
        print("  input curve %d %s: 0.18 at code %d, code 255 -> %.3f" % (k, curve, g, cv[255]))
    L.append("static const int32_t wf_ll_mat[%d][9] = {     /* linear Rec.709 -> target gamut, Q20 */" % len(GAMUTS))
    worst = 0.0
    for (name, prim), pub in zip(GAMUTS, PUBLISHED):
        N = npm(prim, D65)
        if pub is not None:
            dev = max(abs(N[r][c] - pub[r][c]) for r in range(3) for c in range(3))
            worst = max(worst, dev)
            print("  %-18s npm vs published RGB->XYZ: max |diff| %.2e" % (name, dev))
            assert dev < 5e-6, name
        M = rec709_to(prim)
        q = qmat(M, 1 << 20)
        assert all(v >= 0 for v in q), (name, q)    # no negative terms: 0..1 in -> 0..1 out
        L.append("  { %s },  /* -> %s */" % (", ".join(str(v) for v in q), name))
    L.append("};")
    L.append("#ifndef WF_LL_EXTERN_ENC")
    L.append("static const uint16_t wf_ll_enc[%d][4097] = {  /* linear Q12 -> log code Q8 (= wf_lenc) */" % len(CURVES))
    for name, f in CURVES:
        enc = lenc_table(f)
        L.append("  { /* %s */" % name)
        for i in range(0, 4097, 16):
            L.append("    " + ",".join(str(v) for v in enc[i:i + 16]) + ",")
        L.append("  },")
    L.append("};")
    L.append("#else")
    L.append("extern const uint16_t wf_lenc[%d][4097];       /* wf_logout_tab.c (wf_logtab.h made non-static) */" % len(CURVES))
    L.append("#define wf_ll_enc wf_lenc")
    L.append("#endif")
    # Y, Cb, Cr from h_c = (tetrahedral sum >> 1); sum = value * 65535 * 65536 (value 0..1)
    # code = 255 * k . v = sum_c k_c * 255 * 2 * h_c / (65535 * 65536); x 2^48
    K = [[0.299, 0.587, 0.114],
         [-0.168735891647856, -0.331264108352144, 0.5],
         [0.5, -0.418687589158345, -0.081312410841655]]
    A = [[int(round(k * 255.0 * 2.0 * 2.0 ** 48 / (65535.0 * 65536.0))) for k in row] for row in K]
    L.append("static const int32_t wf_ll_ycc[3][3] = {      /* Y, Cb, Cr x 2^48 from tetrahedral sums / 2 */")
    for row in A:
        L.append("  { %d, %d, %d }," % tuple(row))
    L.append("};")
    r10 = [0] + [((1 << 56) + 10 ** d // 2) // 10 ** d for d in range(1, 27)]
    L.append("static const uint64_t wf_ll_r10[27] = {      /* round(2^56 / 10^d) */")
    for i in range(0, 27, 3):
        L.append("  " + ", ".join("%dull" % v for v in r10[i:i + 3]) + ",")
    L.append("};")
    L.append("static const uint32_t wf_ll_p10[10] = { %s };" % ", ".join("%du" % 10 ** d for d in range(10)))
    # character classes of the .cube parser: 1 blank (space, tab, CR, VT, FF), 2 end of the
    # content ('\n', '#'), 4 digit
    cls = [0] * 256
    for ch in " \t\r\v\f":
        cls[ord(ch)] = 1
    for ch in "\n#":
        cls[ord(ch)] = 2
    for ch in "0123456789":
        cls[ord(ch)] = 4
    L.append("static const uint8_t wf_ll_cls[256] = {       /* 1 blank, 2 '\\n' or '#', 4 digit */")
    for i in range(0, 256, 32):
        L.append("  " + ",".join(str(v) for v in cls[i:i + 32]) + ",")
    L.append("};")
    open(out, "w", newline="\n").write("\n".join(L) + "\n")
    for name, f in CURVES:
        print("  %-12s black %.4f  18%% grey %.4f  white(1.0) %.4f" % (name, f(0), f(0.18), f(1.0)))
    print("  wrote", out)


if __name__ == "__main__":
    main()
