/* wf_film.c - WindyStrife own film looks for WindyFilm (windyflm.mo): the per-pixel
 * colour pass. Compiled -O2 (hot loop), merged with `ld -r` like the other objects. The shot set is built
 * and read by the worker task only; the LiveView preview has its own set (wf_film.h, wf_shot.c),
 * built by the same wf_film_build and read by the same wf_film_words.
 *
 * Source: the film's 17^3 YUV table made by the module's own .cube loader (the card pack WFFILMS.DAT,
 * tools/mk_wfpack.py), words {U^0x80, Y, V^0x80, 0}, index Ycell*289 + Ucell*17 + Vcell, cells of 16 code
 * values (the last 240..255, node 16 = 255). Exact value at any point = tetrahedral interpolation (wf_tet16).
 *
 * Per shot the 3D lookup is too slow for 9 M pixel pairs (trilinear 182, tetrahedral 150
 * instructions per pair under qemu), so wf_film_prepare() resamples the table once per film into
 * a "2.5D" form and wf_film_words() costs ~80 instructions per pair:
 *   Y'(y, u, v)  = N[y] + E(ym, u, v)       N = the film's neutral-axis curve (u = v = 0), per pixel
 *   U'V'(u, v)   = C(ym, u, v)              once per pixel pair (UYVY shares chroma)
 * with ym = the pair's mean luma, E and C sampled on a 65-level luma grid (step 4, nearest) x the
 * 17x17 chroma nodes, triangle (3-corner) interpolation in u, v. On real photos vs the exact
 * tetrahedral lookup: luma mean 0.24, p99.9 2 code values; chroma max 2 (phase 1 measurement).
 *
 * WF_FT_V21 (the catalogue films from id 32; judge v2.1, lookfix batch-0 spec): adaptive luma levels and
 * per-pixel E. The 64 luma intervals of E/C are shared among the 16 Y cells of the T17 in proportion to how
 * much E, U, V change along luma in that cell (wf_levels), and E is taken at each pixel's own luma level
 * (per-pixel E: Y'0 = N[y0] + E(y0, u, v), Y'1 = N[y1] + E(y1, u, v)); C stays at the pair mean luma.
 * Without the flag the set and the kernel are the shipped ones, bit-exact (ids 26..31). The loop is picked
 * once per call (per chunk of rows), never per pixel.
 *
 * Strength (the browser row, 30 / 100 / 125 %): 30 % / 125 % scale every builder value's deviation from the identity
 * mapping once at build time (wf_d12); 100 % takes the unscaled path. */
#include <stdint.h>
#include "wf_film.h"

/* the shot set: worker task only. Allocated once by wf_film_shot_alloc (wf_task, module start: 96.5 KB kept out of
 * the module image); while it is missing the shot path leaves film frames untouched (wf_film_shot_ok). */
static struct wf_ftabs *wf_ft_sp;
#define wf_ft_shot (*wf_ft_sp)
static uint32_t wf_ft_key;                         /* its key (load serial) and build flags (wf_film_prepare) */
static int wf_ft_flags;

static int wf_clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static int wf_rnd16(int s) { return s >= 0 ? (s + 8) >> 4 : -((-s + 8) >> 4); }
/* D12: v' = i + sgn(d) * ((|d| * s + 50) / 100), d = v - i (rounded half away from zero; constant divisor) */
static int wf_d12(int v, int i, int s)
{
    int d = v - i;
    return d >= 0 ? i + (int)(((unsigned)d * (unsigned)s + 50u) / 100u)
                  : i - (int)(((unsigned)-d * (unsigned)s + 50u) / 100u);
}

/* exact tetrahedral value x16 of byte lane `sh` (0 U, 8 Y, 16 V; U/V signed) at (y, u, v) */
static int wf_tet16(const struct wf_ftabs *t, const uint32_t *T, int sh, int y, int u, int v)
{
    int base = t->fcell[y] * 289 + t->fcell[u] * 17 + t->fcell[v];
    int f[3] = { t->ffrac[y], t->ffrac[u], t->ffrac[v] }, st[3] = { 289, 17, 1 }, o[3] = { 0, 1, 2 };
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (f[o[j]] > f[o[i]]) { int x = o[i]; o[i] = o[j]; o[j] = x; }
    int ix[4] = { base, base + st[o[0]], base + st[o[0]] + st[o[1]], base + 307 };
    int w[4] = { 16 - f[o[0]], f[o[0]] - f[o[1]], f[o[1]] - f[o[2]], f[o[2]] }, s = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t e = T[ix[i]] >> sh;
        s += w[i] * (sh == 8 ? (int)(e & 255) : (int)(int8_t)e);
    }
    return s;
}

/* WF_FT_V21 levels of table T: ly[0..64] the level lumas, lvl[y] = nearest level x 289 (ties to the upper one).
 * Weight of Y cell c = sum over the in-gamut u, v nodes (a real colour at either end of the cell) of
 * |dE| + |dU| + |dV| across the cell, the neutral node x64; floor 35 % of the mean, 1..16 intervals per cell,
 * largest remainder; levels evenly spaced integer y inside a cell. */
static void wf_levels(const uint32_t *T, int *ly, uint16_t *lvl)
{
    int n[16], d[16], rem[16], tot = 0, wsum = 0, used = 0;
    for (int c = 0; c < 16; c++) {
        int s = 0;
        for (int k = 0; k < 289; k++) {
            int u = (k / 17) * 16 - 128, v = (k % 17) * 16 - 128, ok = 0;
            if (u > 127) u = 127;
            if (v > 127) v = 127;
            for (int e = 0; e < 2; e++) {
                int y = (c + e) * 16 > 255 ? 255 : (c + e) * 16;
                int R = y + ((359 * v) >> 8), G = y - ((88 * u + 183 * v) >> 8), B = y + ((454 * u) >> 8);
                if (R >= 0 && R <= 255 && G >= 0 && G <= 255 && B >= 0 && B <= 255) ok = 1;
            }
            if (!ok) continue;
            uint32_t a = T[c * 289 + k], b = T[(c + 1) * 289 + k], na = T[c * 289 + 144], nb = T[(c + 1) * 289 + 144];
            int ea = (int)((a >> 8) & 255) - (int)((na >> 8) & 255), eb = (int)((b >> 8) & 255) - (int)((nb >> 8) & 255);
            int du = (int)(int8_t)b - (int)(int8_t)a, dv = (int)(int8_t)(b >> 16) - (int)(int8_t)(a >> 16);
            s += ((eb > ea ? eb - ea : ea - eb) + (du < 0 ? -du : du) + (dv < 0 ? -dv : dv)) * (k == 144 ? 64 : 1);
        }
        d[c] = s; tot += s;
    }
    int fl = tot * 35 / 1600;
    for (int c = 0; c < 16; c++) { if (d[c] < fl) d[c] = fl; if (d[c] < 1) d[c] = 1; wsum += d[c]; }
    for (int c = 0; c < 16; c++) {
        n[c] = 64 * d[c] / wsum; rem[c] = 64 * d[c] - n[c] * wsum;
        if (n[c] < 1) { n[c] = 1; rem[c] = 0; }
        if (n[c] > 16) { n[c] = 16; rem[c] = 0; }
        used += n[c];
    }
    while (used < 64) {
        int bc = -1;
        for (int c = 0; c < 16; c++) if (n[c] < 16 && (bc < 0 || rem[c] > rem[bc])) bc = c;
        n[bc]++; rem[bc] = -1; used++;
    }
    while (used > 64) {
        int bc = 0;
        for (int c = 1; c < 16; c++) if (n[c] > n[bc]) bc = c;
        n[bc]--; used--;
    }
    int l = 0;
    ly[l++] = 0;
    for (int c = 0; c < 16; c++)
        for (int k = 1; k <= n[c]; k++) { int y = c * 16 + (16 * k + n[c] / 2) / n[c]; ly[l++] = y > 255 ? 255 : y; }
    l = 0;
    for (int y = 0; y < 256; y++) {
        while (l < WF_NL - 1 && 2 * y >= ly[l] + ly[l + 1]) l++;
        lvl[y] = l * 289;
    }
}

#ifndef MODULE
/* host builds (without -DMODULE): the level lumas of the set built last (one set at a time there) */
static int wf_host_ly[WF_NL];
#define WF_LEVEL_Y(l) (wf_host_ly[l])
#endif

/* builds the 2.5D tables of set t for table T (4913 words) with the WF_FT_* flags; ~8.7 M instructions
 * (qemu count), once per film change. t->src is cleared first and set last: a set whose src is T is complete. */
void wf_film_build(struct wf_ftabs *t, const uint32_t *T, int flags)
{
    int s = WF_FT_SOF(flags), mono = flags & WF_FT_MONO, ly[WF_NL];
    t->src = 0;
    t->flags = (uint8_t)flags;
    for (int v = 0; v < 256; v++) {
        int c = v >> 4, f = v & 15;
        if (c == 15) f = ((v - 240) * 16 + 7) / 15;   /* grid 240 -> 255 */
        t->fcell[v] = c;
        t->ffrac[v] = f;
        t->uvi[v] = c << 5 | f;
    }
    for (int x = -256; x < 512; x++)
        t->clampY[256 + x] = wf_clampi(x, 0, 255);
    if (flags & WF_FT_V21) {
        wf_levels(T, ly, t->lvl);
    } else {
        for (int l = 0; l < WF_NL; l++)
            ly[l] = l * 4 > 255 ? 255 : l * 4;
        for (int y = 0; y < 256; y++)
            t->lvl[y] = ((y + 2) >> 2) * 289;
    }
#ifndef MODULE
    for (int l = 0; l < WF_NL; l++)
        wf_host_ly[l] = ly[l];
#endif
    for (int y = 0; y < 256; y++) {
        int n = wf_rnd16(wf_tet16(t, T, 8, y, 128, 128));
        if (s != 100)
            n = wf_d12(n, y, s);
        t->N[y] = wf_clampi(n, 0, 255);
    }
    for (int l = 0; l < WF_NL; l++) {
        int y = ly[l];
        for (int b = 0; b < 17; b++)
            for (int c = 0; c < 17; c++) {
                int un = b * 16 > 255 ? 255 : b * 16, vn = c * 16 > 255 ? 255 : c * 16;
                int U = wf_rnd16(wf_tet16(t, T, 0, y, un, vn));
                int V = wf_rnd16(wf_tet16(t, T, 16, y, un, vn));
                int Y = wf_rnd16(wf_tet16(t, T, 8, y, un, vn));
                if (s != 100) {                       /* D12: deviation from identity scaled (B&W: luma only) */
                    Y = wf_clampi(wf_d12(Y, y, s), 0, 255);
                    if (!mono) {
                        U = wf_d12(U, un - 128, s);
                        V = wf_d12(V, vn - 128, s);
                    }
                }
                U = wf_clampi(U, -128, 127);
                V = wf_clampi(V, -128, 127);
                t->C[l * 289 + b * 17 + c] = (uint32_t)(U + 128) | (uint32_t)(V + 128) << 16;
                t->E[l * 289 + b * 17 + c] = wf_clampi(Y - t->N[y] + 128, 0, 255);
            }
    }
    if (s > 100)                                      /* Strong: N non-decreasing (after E, which used the scaled N) */
        for (int y = 1; y < 256; y++)
            if (t->N[y] < t->N[y - 1])
                t->N[y] = t->N[y - 1];
    __asm__ __volatile__("" ::: "memory");          /* tables complete before src says so */
    t->src = T;
}

/* the shot set (worker task): built for (T, key, flags) unless it already holds them */
void wf_film_prepare(const uint32_t *T, uint32_t key, int flags)
{
    if (!wf_ft_sp)
        return;
    if (T == wf_ft_shot.src && key == wf_ft_key && flags == wf_ft_flags)
        return;
    wf_ft_key = 0;
    wf_film_build(&wf_ft_shot, T, flags);
    wf_ft_key = key;
    wf_ft_flags = flags;
}
uint32_t wf_film_shot_key(void) { return wf_ft_sp && wf_ft_shot.src ? wf_ft_key : 0; }
/* the shot set's memory (mem: sizeof(struct wf_ftabs) bytes, cacheable, kept for the module's life) */
void wf_film_shot_set_mem(void *mem)
{
    struct wf_ftabs *t = (struct wf_ftabs *)mem;
    if (t)
        t->src = 0;
    wf_ft_key = 0;
    wf_ft_sp = t;
}
int wf_film_shot_ok(void) { return wf_ft_sp != 0; }
int wf_film_shot_flags(void) { return wf_ft_flags; }

/* wf_chunks callback: n UYVY words {U s8, Y0, V s8, Y1} in place (cached scratch); tabs = the
 * set, 0 = the shot set. The loop is chosen once per call from the set's build flags. */
void wf_film_words(uint32_t *p, int n, int tabs)
{
    const struct wf_ftabs *t = tabs ? (const struct wf_ftabs *)(uintptr_t)tabs : wf_ft_sp;
    if (!t)
        return;
    const uint16_t *uvi = t->uvi;
    const uint8_t *N = t->N, *clampY = t->clampY, *E = t->E;
    const uint32_t *C = t->C;
    if (t->flags & WF_FT_V21) {                       /* adaptive levels, per-pixel E */
        const uint16_t *lv = t->lvl;
        for (int k = 0; k < n; k++) {
            uint32_t wd = p[k];
            int cu = uvi[(wd ^ 0x80) & 0xff], cv = uvi[((wd >> 16) ^ 0x80) & 0xff];
            int y0 = (wd >> 8) & 0xff, y1 = wd >> 24;
            int fu = cu & 31, fv = cv & 31;
            int ouv = (cu >> 5) * 17 + (cv >> 5);
            const uint32_t *qc = C + lv[(y0 + y1 + 1) >> 1] + ouv;
            int s, w0, w1, w2;                        /* triangle: weights >= 0, sum 16 */
            if (fu >= fv) { s = 17; w0 = 16 - fu; w1 = fu - fv; w2 = fv; }
            else          { s = 1;  w0 = 16 - fv; w1 = fv - fu; w2 = fu; }
            uint32_t uv = qc[0] * w0 + qc[s] * w1 + qc[18] * w2;      /* lanes <= 4080 */
            uv = ((uv + 0x00080008u) >> 4) & 0x00ff00ffu;
            const uint8_t *q0 = E + lv[y0] + ouv, *q1 = E + lv[y1] + ouv;
            int e0 = (q0[0] * w0 + q0[s] * w1 + q0[18] * w2 + 8) >> 4;   /* E + 128 at y0 */
            int e1 = (q1[0] * w0 + q1[s] * w1 + q1[18] * w2 + 8) >> 4;   /* E + 128 at y1 */
            uint32_t o0 = clampY[128 + N[y0] + e0], o1 = clampY[128 + N[y1] + e1];
            p[k] = ((uv ^ 0x80) & 0xff) | o0 << 8 | (((uv >> 16) ^ 0x80) & 0xff) << 16 | o1 << 24;
        }
        return;
    }
    for (int k = 0; k < n; k++) {                     /* shipped kernel: uniform levels, E at the pair mean */
        uint32_t wd = p[k];
        int cu = uvi[(wd ^ 0x80) & 0xff], cv = uvi[((wd >> 16) ^ 0x80) & 0xff];
        int y0 = (wd >> 8) & 0xff, y1 = wd >> 24;
        int fu = cu & 31, fv = cv & 31;
        int off = ((((y0 + y1 + 1) >> 1) + 2) >> 2) * 289 + (cu >> 5) * 17 + (cv >> 5);
        const uint32_t *qc = C + off;
        const uint8_t *qe = E + off;
        int s, w0, w1, w2;                            /* triangle: weights >= 0, sum 16 */
        if (fu >= fv) { s = 17; w0 = 16 - fu; w1 = fu - fv; w2 = fv; }
        else          { s = 1;  w0 = 16 - fv; w1 = fv - fu; w2 = fu; }
        uint32_t uv = qc[0] * w0 + qc[s] * w1 + qc[18] * w2;          /* lanes <= 4080 */
        uv = ((uv + 0x00080008u) >> 4) & 0x00ff00ffu;
        int e = (qe[0] * w0 + qe[s] * w1 + qe[18] * w2 + 8) >> 4;     /* E + 128 */
        uint32_t o0 = clampY[128 + N[y0] + e], o1 = clampY[128 + N[y1] + e];
        p[k] = ((uv ^ 0x80) & 0xff) | o0 << 8 | (((uv >> 16) ^ 0x80) & 0xff) << 16 | o1 << 24;
    }
}
