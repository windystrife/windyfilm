/* wf_llk.c - "Log + LUT" shot kernel for WindyFilm (WindyStrife). Build -O2.
 *
 * Applies the composite tables of wf_loglut.c (struct wf_lltabs, wf_loglut.h) to a UYVY frame:
 *   chroma  u, v (0..255) -> cell u>>3, v>>3 of the 33 x 33 node grid (nodes 0, 8, .., 256), triangle
 *           weights 0..8 (the diagonal splits each cell, as wf_film.c)
 *   luma    level l = y>>3 of 33 (nodes 0, 8, .., 256), linear between level l and l + 1
 *   Y'      = N[y] + E, E = (interpolated node Y') - (the same interpolation of the neutral Y'):
 *           0 for grey, so grey is N[y] exactly, per pixel
 * A smooth pair (|y0 - y1| < 8) uses the pair mean ym for the level and the chroma (4:2:2: one Cb/Cr
 * for the pair) and evaluates E per pixel on the line through ym's two levels (each pixel's own
 * luma, extrapolated by at most half a level). Without that, E stayed constant inside a level and
 * strong LUTs showed a period-8 luma sawtooth on smooth gradients (review CS2).
 * An edge pair (|y0 - y1| >= 8, ~4 % of the pairs of photos) is evaluated per pixel at its own
 * level; the chroma is the rounded mean of the two pixels' chroma (as the camera's 4:2:2 mean).
 *
 * Packed lanes: C node = (2 U + 1) | (2 V + 1) << 16. Triangle sums <= 511 * 8, times the luma
 * weights (sum 8) <= 32704 < 2^15, so U and V are interpolated together in one 32-bit word and the
 * "+ 1" of every node is the rounding (sum = 2 x (weighted sum) + 64, >> 7 = rounded / 64).
 * The bias in L (wf_loglut.h) makes N[y] + E' the byte offset of clamp(N[y] + E) in the struct's
 * clamp table K. Every table access is a load at a small immediate offset from the struct, the
 * pair's node index or the level.
 *
 * wf_ll_words_c is the reference. On the camera wf_ll_words is wf_llk_arm.S (build with
 * -DWF_LL_ASM), bit-exact with it (test.sh SHOT: wf_ll_words vs wf_ll_words_c); it calls wf_ll_edge_w for the edge
 * pairs. Elsewhere (host tests) wf_ll_words = wf_ll_words_c. */
#include <stdint.h>
#include "wf_loglut.h"

/* one pixel at its own level: chroma sum (lanes 2 x 64 x value + 64) and E' (= E + WF_LL_KOFF) */
static inline __attribute__((always_inline)) uint32_t
wf_ll_px(const struct wf_lltabs *t, int cell, const int s, int w0, int w1, int w2, int y, int *e)
{
    int l = y >> 3, f = y & 7, o = l * WF_LL_LSTEP + cell;
    const uint32_t *qc = t->C + o;
    const uint8_t *qy = t->Y + o;
    uint32_t a = qc[0] * w0 + qc[s] * w1 + qc[WF_LL_NC + 1] * w2;
    uint32_t b = qc[WF_LL_LSTEP] * w0 + qc[WF_LL_LSTEP + s] * w1 + qc[WF_LL_LSTEP + WF_LL_NC + 1] * w2;
    int da = qy[0] * w0 + qy[s] * w1 + qy[WF_LL_NC + 1] * w2 - t->L[l];
    int db = qy[WF_LL_LSTEP] * w0 + qy[WF_LL_LSTEP + s] * w1 + qy[WF_LL_LSTEP + WF_LL_NC + 1] * w2 - t->L[l + 1];
    *e = (da * (8 - f) + db * f) >> 6;
    return a * (8 - f) + b * f;
}

/* an edge pair (|y0 - y1| >= 8), triangle side s: each pixel at its own level */
static inline __attribute__((always_inline)) uint32_t
wf_ll_edge(const struct wf_lltabs *t, int cell, const int s, int w0, int w1, int w2, int y0, int y1)
{
    const uint8_t *S = (const uint8_t *)t;
    int e0, e1;
    uint32_t ua = wf_ll_px(t, cell, s, w0, w1, w2, y0, &e0);
    uint32_t ub = wf_ll_px(t, cell, s, w0, w1, w2, y1, &e1);
    uint32_t uv = ((ua + ub) >> 8) & ~0xff00u;        /* lanes <= 65408: rounded mean */
    return (uv ^ 0x00800080u) | (uint32_t)S[S[y0] + e0] << 8 | (uint32_t)S[S[y1] + e1] << 24;
}

/* one edge word, out of line (few pairs): also called by wf_llk_arm.S */
__attribute__((noinline)) uint32_t wf_ll_edge_w(const struct wf_lltabs *t, uint32_t wd)
{
    uint32_t x = wd ^ 0x00800080u;
    int u = x & 0xff, v = (x >> 16) & 0xff;
    int y0 = (wd >> 8) & 0xff, y1 = wd >> 24;
    int fu = u & 7, fv = v & 7;
    int cell = (u >> 3) * WF_LL_NC + (v >> 3);
    if (fu >= fv)
        return wf_ll_edge(t, cell, WF_LL_NC, 8 - fu, fu - fv, fv, y0, y1);
    return wf_ll_edge(t, cell, 1, 8 - fv, fv - fu, fu, y0, y1);
}

/* a smooth pair (|y0 - y1| < 8), triangle side s (33: fu >= fv, 1: fu < fv) */
static inline __attribute__((always_inline)) uint32_t
wf_ll_pair(const struct wf_lltabs *t, int cell, const int s, int w0, int w1, int w2, int y0, int y1)
{
    const uint8_t *S = (const uint8_t *)t;
    int m2 = y0 + y1 + 1, l = m2 >> 4, fl = (m2 >> 1) & 7, o = l * WF_LL_LSTEP + cell;
    const uint32_t *qc = t->C + o;
    const uint8_t *qy = t->Y + o;
    uint32_t a = qc[0] * w0 + qc[s] * w1 + qc[WF_LL_NC + 1] * w2;
    uint32_t b = qc[WF_LL_LSTEP] * w0 + qc[WF_LL_LSTEP + s] * w1 + qc[WF_LL_LSTEP + WF_LL_NC + 1] * w2;
    uint32_t uv = ((a * 8 + (b - a) * fl) >> 7) & ~0xfe00u;   /* = a (8 - fl) + b fl, mod 2^32 */
    int da = qy[0] * w0 + qy[s] * w1 + qy[WF_LL_NC + 1] * w2 - t->L[l];
    int db = qy[WF_LL_LSTEP] * w0 + qy[WF_LL_LSTEP + s] * w1 + qy[WF_LL_LSTEP + WF_LL_NC + 1] * w2 - t->L[l + 1];
    int d = db - da, base = da * 8, l8 = l * 8;
    int e0 = (base + d * (y0 - l8)) >> 6;             /* E' on the line through ym's two levels */
    int e1 = (base + d * (y1 - l8)) >> 6;
    uint32_t o0 = S[S[y0] + e0], o1 = S[S[y1] + e1];  /* N at offset 0, K: clamp(N + E) */
    return (uv ^ 0x00800080u) | o0 << 8 | o1 << 24;
}

void wf_ll_words_c(uint32_t *p, int n, int tabs)
{
    const struct wf_lltabs *t = (const struct wf_lltabs *)(uintptr_t)tabs;
    for (uint32_t *pe = p + n; p < pe; p++) {
        uint32_t wd = *p, x = wd ^ 0x00800080u;
        int u = x & 0xff, v = (x >> 16) & 0xff;
        int y0 = (wd >> 8) & 0xff, y1 = wd >> 24;
        int fu = u & 7, fv = v & 7;
        int cell = (u >> 3) * WF_LL_NC + (v >> 3);
        if ((unsigned)(y0 - y1 + 7) > 14)
            *p = wf_ll_edge_w(t, wd);
        else if (fu >= fv)                            /* triangle: weights >= 0, sum 8 */
            *p = wf_ll_pair(t, cell, WF_LL_NC, 8 - fu, fu - fv, fv, y0, y1);
        else
            *p = wf_ll_pair(t, cell, 1, 8 - fv, fv - fu, fu, y0, y1);
    }
}

#ifndef WF_LL_ASM
void wf_ll_words(uint32_t *p, int n, int tabs) __attribute__((alias("wf_ll_words_c")));
#endif
