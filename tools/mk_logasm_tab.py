#!/usr/bin/env python3
"""mk_logasm_tab.py WF_LOGTAB_H OUT.h - tables for the ARMv5TE log kernel wf_logout_arm.S (WindyFilm).

Reads the wf_dec / wf_lmat values of wf_logtab.h (mk_logtab.py output, so the two can never disagree)
and writes wf_logasm_tab.h with:

  wf_lasm      one .rodata block (struct, 32-B aligned); the kernel's base register is B = wf_lasm + 1024:
               B - 1024  U table, 256 x {offB, GA4} by cb = -128..127 (int32 pairs; address B + 8 cb)
               B + 1024  V table, 256 x {offR, GB4} by the Cr byte V (cr = (int8)V; address B + 1024 + 8 V)
               B + 3072  padded wf_dec: PD[j] = wf_dec[CLAMP(j + IMIN, 0, 1020)], j = 0 .. IMAX - IMIN
               (the hot entries, cb / cr near 0, sit at B and at B + 1024 / B + 3072: half a cache way apart)
  wf_lasm_k    per mode 5 words of packed Q14 matrix halves {m0|m1<<16, m2|m3<<16, m4|m5<<16, m6|m7<<16,
               m8|2<<16} (the top half 2 of the last word is the kernel's x2 for every SMULTT offset)

Maths (wf_logout_ref.c): ir = (y << 16) + 8192 + tr >> 14 = 4 y + dr with dr = (8192 + 91881 cr) >> 14
(65536 y is a multiple of 16384), likewise dg = (8192 - 22554 cb - 46802 cr) >> 14, db = (8192 + 116130 cb) >> 14.
The padded table makes the CLAMP(.., 0, 1020) free: lr = PD[4 y + dr - IMIN]. With pp = B + 8 y the kernel
loads LDRH [pp, off] where off = 3072 + 2 (d - IMIN) = 2 d + D, D = 3072 - 2 IMIN:
  offR, offB come straight from the tables; offG = 2 * ((GA4 + GB4) >> 16) (one SMULTT with the "2" half of
  wf_lasm_k[.][4]), GA4 = 4 (8192 - 22554 cb) + (D / 2) << 16, GB4 = -4 * 46802 cr.
Every identity is checked here over the whole (cb, cr) range, and max(wf_lenc) <= 65279 (the kernel's dead
Cb/Cr clamp and its d << 14 / Y' sum ranges depend on it). Pure stdlib, deterministic.
"""
import re
import sys


def cs8(b):
    return b - 256 if b >= 128 else b


def parse(path):
    txt = open(path).read()
    m = re.search(r"wf_dec\[1024\]\s*=\s*\{(.*?)\};", txt, re.S)
    dec = [int(v) for v in re.findall(r"-?\d+", m.group(1))]
    m = re.search(r"wf_lmat\[5\]\[9\]\s*=\s*\{(.*?)\n\};", txt, re.S)
    lmat = [[int(v) for v in row.split(",")] for row in re.findall(r"\{([^{}]*)\}", m.group(1))]
    assert len(dec) == 1024 and len(lmat) == 5 and all(len(r) == 9 for r in lmat), (len(dec), lmat)
    i = txt.index("wf_lenc[5][4097]")
    body = re.sub(r"/\*.*?\*/", "", txt[txt.index("{", i):], flags=re.S)
    lenc = [[int(v) for v in re.findall(r"-?\d+", row)] for row in re.findall(r"\{([^{}]*)\}", body[:body.index("};")])]
    assert len(lenc) == 5 and all(len(r) == 4097 for r in lenc), [len(r) for r in lenc]
    return dec, lmat, lenc


def main():
    dec, lmat, lenc = parse(sys.argv[1])
    # wf_lenc codes (Q8): the kernel drops CLAMP(u / v, -128, 127) and forms d << 14 = (pair-sum diff) << 14.
    # |X| <= 8192 * 2 cmax, so -128 <= (X + 2^22) >> 23 <= 127 needs cmax <= 65279 (today about 39110);
    # 2 cmax << 14 < 2^31 and 65536 cmax + 2^23 < 2^32 (Y' sum) follow.
    cmax = max(max(r) for r in lenc)
    assert min(min(r) for r in lenc) >= 0 and cmax <= 65279, cmax
    assert (16384 * cmax + (1 << 22)) >> 23 <= 127 and (-16384 * cmax + (1 << 22)) >> 23 >= -128
    assert (2 * cmax) << 14 < 2**31 and 65536 * cmax + (1 << 23) < 2**32
    dr = {cr: (8192 + 91881 * cr) >> 14 for cr in range(-128, 128)}
    db = {cb: (8192 + 116130 * cb) >> 14 for cb in range(-128, 128)}
    dg = {(cb, cr): (8192 - 22554 * cb - 46802 * cr) >> 14 for cb in range(-128, 128) for cr in range(-128, 128)}
    dall = list(dr.values()) + list(db.values()) + list(dg.values())
    IMIN, IMAX = min(dall), 1020 + max(dall)
    PDOFF = 3072                                   # PD start - B
    D = PDOFF - 2 * IMIN
    K = D // 2
    assert D % 2 == 0 and -32768 <= min(dg.values()) + K and max(dg.values()) + K <= 32767
    # padded wf_dec
    PD = [dec[min(max(j + IMIN, 0), 1020)] for j in range(IMAX - IMIN + 1)]
    assert max(PD) == 16384 and min(PD) == 0       # lr, lg, lb fit SMULxy halves; S <= 2^28
    # U table by cb (index cb + 128), V table by the byte V
    U = []
    V = []
    for i in range(256):
        cb, cr = i - 128, cs8(i)
        U += [PDOFF + 2 * (db[cb] - IMIN), 4 * (8192 - 22554 * cb) + (K << 16)]
        V += [PDOFF + 2 * (dr[cr] - IMIN), -4 * 46802 * cr]
    for cb in range(-128, 128):
        for cr in range(-128, 128):
            x4 = U[2 * (cb + 128) + 1] + V[2 * (cr & 255) + 1]
            assert -2**31 <= x4 < 2**31 and -32768 <= (x4 >> 16) <= 32767
            assert 2 * (x4 >> 16) == PDOFF + 2 * (dg[(cb, cr)] - IMIN), (cb, cr)
    # every LDRH lands inside PD: B + 8 y + off, off = PDOFF + 2 (d - IMIN)
    for d in dall:
        for y in (0, 255):
            j = 4 * y + d - IMIN
            assert 0 <= j < len(PD)
            assert PD[j] == dec[min(max(4 * y + d, 0), 1020)]
    # matrix: halves fit s16, rows sum to 16384, coefficients >= 0 (the kernel drops the dead clamps)
    KM = []
    for m in lmat:
        assert all(0 <= v <= 32767 for v in m) and sum(m[0:3]) == sum(m[3:6]) == sum(m[6:9]) == 16384, m
        KM.append([m[0] | m[1] << 16, m[2] | m[3] << 16, m[4] | m[5] << 16, m[6] | m[7] << 16, m[8] | 2 << 16])

    o = ["/* generated by mk_logasm_tab.py from wf_logtab.h - do not edit */",
         "/* ARMv5TE log kernel tables (wf_logout_arm.S): IMIN %d IMAX %d D %d, %d bytes */" % (
             IMIN, IMAX, D, PDOFF + 1024 + 2 * len(PD)),
         "#include <stdint.h>",
         "#define WF_LASM_IMIN %d" % IMIN,
         "#define WF_LASM_NPD %d" % len(PD),
         "const struct { int32_t uv[1024]; uint16_t pd[%d]; } wf_lasm __attribute__((aligned(32))) = {" % len(PD),
         "  { /* U table: {offB, GA4} for cb = -128..127, then V table: {offR, GB4} for V = 0..255 */"]
    for i in range(0, 512, 8):
        o.append("    " + ",".join(str(v) for v in U[i:i + 8]) + ",")
    for i in range(0, 512, 8):
        o.append("    " + ",".join(str(v) for v in V[i:i + 8]) + ",")
    o.append("  },")
    o.append("  { /* padded wf_dec */")
    for i in range(0, len(PD), 16):
        o.append("    " + ",".join(str(v) for v in PD[i:i + 16]) + ",")
    o.append("  }")
    o.append("};")
    o.append("const uint32_t wf_lasm_k[5][5] = {   /* m0|m1<<16, m2|m3<<16, m4|m5<<16, m6|m7<<16, m8|2<<16 */")
    for k in KM:
        o.append("  { %s }," % ", ".join("0x%08x" % v for v in k))
    o.append("};")
    open(sys.argv[2], "w", newline="\n").write("\n".join(o) + "\n")
    print("mk_logasm_tab: IMIN %d IMAX %d D %d, wf_lasm %d bytes + wf_lasm_k 100 bytes, max wf_lenc %d" % (
        IMIN, IMAX, D, PDOFF + 1024 + 2 * len(PD), cmax))


main()
