/* wf_logout_ref.c - log output modes of WindyFilm (S-Log3 / V-Log / LogC3 / Canon Log 2 / 3): the C REFERENCE.
 * The camera build (build.sh) does not compile this file: it uses wf_logout_arm.S (wf_log_words in
 * hand-scheduled ARMv5TE asm) + wf_logout_tab.c (wf_lenc and the asm tables of mk_logasm_tab.py), which are
 * bit-exact with this code. This file is the reference of that bit-exact gate (logchk.c, test.sh mode LOGCHK:
 * this file at the module flags, symbols renamed ref_log_words / ref_lenc, vs the shipped wf_logout.o, all
 * 2^24 (U, V, Y) twice per mode + random words and chunks; the logspd harness logchk.sh adds test frames) and
 * of the host prototypes (proto/loglut). Change the maths here first, then the asm, and rerun the gate.
 * Re-encodes the camera's JPEG-bound UYVY frame (BT.601 full range, sRGB) as camera log, so a
 * Sony / Panasonic / ARRI / Canon (or creative) LUT can be applied in post. Per pixel:
 *   YCbCr -> R'G'B' (Q2 index) -> sRGB EOTF (wf_dec, Q14 linear) -> 3x3 to the target gamut (Q14)
 *   -> log curve (wf_lenc, Q8 code) -> Y per pixel, Cb/Cr from the pair's mean (UYVY 4:2:2).
 * Integer only, no LDRD/STRD; plain C at the module flags (-march=armv5t), compiled only as the reference
 * (logchk.c, proto/loglut), never into the module. Tables: mk_logtab.py.
 * Chroma bytes are signed (s8) like the rest of the module (wf_lut_words). */
#include <stdint.h>
#include "wf_logtab.h"

#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

/* YCbCr -> R'G'B' full range BT.601, Q16 */
#define K_RV   91881    /* 1.402    */
#define K_GU   22554    /* 0.344136 */
#define K_GV   46802    /* 0.714136 */
#define K_BU  116130    /* 1.772    */

/* mode 1..5; p = n UYVY words (cached memory) */
void wf_log_words(uint32_t *p, int n, int mode)
{
    const int32_t *m = wf_lmat[mode - 1];
    const uint16_t *enc = wf_lenc[mode - 1];
    for (int k = 0; k < n; k++) {
        uint32_t wd = p[k];
        int cb = (int8_t)wd, cr = (int8_t)(wd >> 16);
        int tr = K_RV * cr, tg = -K_GU * cb - K_GV * cr, tb = K_BU * cb;
        uint32_t ys[2], er = 0, eg = 0, eb = 0;
        for (int px = 0; px < 2; px++) {
            int y = px ? (int)(wd >> 24) : (int)((wd >> 8) & 0xff);
            int base = (y << 16) + (1 << 13);
            int ir = (base + tr) >> 14, ig = (base + tg) >> 14, ib = (base + tb) >> 14;
            int lr = wf_dec[CLAMP(ir, 0, 1020)], lg = wf_dec[CLAMP(ig, 0, 1020)], lb = wf_dec[CLAMP(ib, 0, 1020)];
            int xr = (m[0] * lr + m[1] * lg + m[2] * lb + 8192) >> 14;
            int xg = (m[3] * lr + m[4] * lg + m[5] * lb + 8192) >> 14;
            int xb = (m[6] * lr + m[7] * lg + m[8] * lb + 8192) >> 14;
            uint32_t r = enc[(CLAMP(xr, 0, 16384) + 2) >> 2];
            uint32_t g = enc[(CLAMP(xg, 0, 16384) + 2) >> 2];
            uint32_t b = enc[(CLAMP(xb, 0, 16384) + 2) >> 2];
            /* Q8 codes <= 65280: the sum stays below 2^32 */
            ys[px] = (19595u * r + 38470u * g + 7471u * b + (1u << 23)) >> 24;
            er += r; eg += g; eb += b;
        }
        /* pair sums (Q8, <= 130560) x Q14 coefficients: |sum| < 2^31 */
        int u = (-2765 * (int)er - 5427 * (int)eg + 8192 * (int)eb + (1 << 22)) >> 23;
        int v = (8192 * (int)er - 6860 * (int)eg - 1332 * (int)eb + (1 << 22)) >> 23;
        u = CLAMP(u, -128, 127);
        v = CLAMP(v, -128, 127);
        p[k] = (uint8_t)u | ys[0] << 8 | (uint32_t)(uint8_t)v << 16 | ys[1] << 24;
    }
}
