/* yg_grain.c - see yg_grain.h. C99. Per-pixel paths are integer only.
 * Module copy of proto/grain_yedlin/yg_grain.c (md5 b1ac6242) for the EOS M build (wf_fx.c).
 * Changes: the EOS M ML core exports no lrintf / fmaxf (EOSM_202.sym), so lrintf rounds half away
 * from zero here (lrintf rounds half to even: only exact .5 values can differ) and fmaxf is a macro;
 * cov_fields divides in float instead of double (no double soft-float calls). */
#include "yg_grain.h"
#include <string.h>
#include <math.h>

static inline long yg_lrintf(float x) { return (long)(x < 0.0f ? x - 0.5f : x + 0.5f); }
#define lrintf yg_lrintf
#define fmaxf(a, b) ((a) > (b) ? (a) : (b))

/* ------------------------------------------------------------------ hashing */
static inline uint32_t hmix(uint32_t h)
{
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}
static inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t s)
{
    return hmix(a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ s);
}

void yg_set_shot(yg_ctx *g, uint32_t shot) { g->seed = hmix(shot * 0x9E3779B1u + 0x632BE5ABu); }

/* ------------------------------------------------------------------ windows */
typedef struct { const uint8_t *p; int d; } yg_win;   /* p = template addr of col i=0 at row j */

static inline void win(const yg_ctx *g, int bx, int by, int j, yg_win *w)
{
    uint32_t h = hash3((uint32_t)bx, (uint32_t)by, g->seed);
    int ox = (int)(((h & 255u) * (YG_TS - YG_WIN + 1)) >> 8);
    int oy = (int)((((h >> 8) & 255u) * (YG_TS - YG_WIN + 1)) >> 8);
    int fx = (g->tier == YG_FULL || g->mode == YG_BW) ? (int)((h >> 16) & 1u) : 0;
    int fy = (int)((h >> 17) & 1u);
    int trow = fy ? oy + (YG_WIN - 1) - j : oy + j;
    int col0 = fx ? ox + (YG_WIN - 1) : ox;
    w->p = g->tmpl + trow * (3 * YG_TS) + col0;
    w->d = fx ? -1 : 1;
}

/* ------------------------------------------------------------------ fast loops (interior) */
#define RM  (g->rowmap + 256)
#define CY  (g->clampY + 256)
#define CC  (g->clampC + 256)

#define FULL_LOOP(NAME, D)                                                             \
static void NAME(const yg_ctx *g, uint32_t *w, int nw, const uint8_t *p)               \
{                                                                                      \
    const int8_t *MR = g->M[0], *MG = g->M[1], *MB = g->M[2];                          \
    const uint8_t *rm = RM, *cy = CY, *cc = CC;                                        \
    const int sY = 8 + g->qs, rY = 1 << (sY - 1), sC = 9 + g->qs + g->cs, rC = 1 << (sC - 1); \
    while (nw-- > 0) {                                                                 \
        uint32_t v = *w;                                                               \
        int U = (int8_t)v, Y0 = (v >> 8) & 255, V = (int8_t)(v >> 16), Y1 = (int)(v >> 24); \
        int rv = (359 * V + 128) >> 8, gv = (88 * U + 183 * V + 128) >> 8, bu = (454 * U + 128) >> 8; \
        int dR0 = MR[(rm[Y0 + rv] << 6) | p[0]];                                       \
        int dG0 = MG[(rm[Y0 - gv] << 6) | p[YG_TS]];                                   \
        int dB0 = MB[(rm[Y0 + bu] << 6) | p[2 * YG_TS]];                               \
        int dR1 = MR[(rm[Y1 + rv] << 6) | p[(D)]];                                     \
        int dG1 = MG[(rm[Y1 - gv] << 6) | p[YG_TS + (D)]];                             \
        int dB1 = MB[(rm[Y1 + bu] << 6) | p[2 * YG_TS + (D)]];                         \
        p += 2 * (D);                                                                  \
        Y0 = cy[Y0 + ((77 * dR0 + 150 * dG0 + 29 * dB0 + rY) >> sY)];                  \
        Y1 = cy[Y1 + ((77 * dR1 + 150 * dG1 + 29 * dB1 + rY) >> sY)];                  \
        { int sR = dR0 + dR1, sG = dG0 + dG1, sB = dB0 + dB1;                          \
          uint32_t Uo = cc[U + ((-43 * sR - 85 * sG + 128 * sB + rC) >> sC)];          \
          uint32_t Vo = cc[V + ((128 * sR - 107 * sG - 21 * sB + rC) >> sC)];          \
          *w++ = Uo | ((uint32_t)Y0 << 8) | (Vo << 16) | ((uint32_t)Y1 << 24); }       \
    }                                                                                  \
}
FULL_LOOP(full_loop_p, 1)
FULL_LOOP(full_loop_m, -1)

#define BW_LOOP(NAME, D)                                                               \
static void NAME(const yg_ctx *g, uint32_t *w, int nw, const uint8_t *p)               \
{                                                                                      \
    const int8_t *M0 = g->M[0];                                                        \
    const uint8_t *cy = CY;                                                            \
    const int qs = g->qs, r1 = qs ? 1 << (qs - 1) : 0, r2 = qs ? r1 - 1 : 0;          \
    while (nw-- > 0) {                                                                 \
        uint32_t v = *w;                                                               \
        int Y0 = (v >> 8) & 255, Y1 = (int)(v >> 24);                                  \
        Y0 = cy[Y0 + ((M0[((Y0 >> 3) << 6) | p[0]] + r1) >> qs)];                      \
        Y1 = cy[Y1 + ((M0[((Y1 >> 3) << 6) | p[(D)]] + r2) >> qs)];                    \
        p += 2 * (D);                                                                  \
        *w++ = (v & 0x00FF00FFu) | ((uint32_t)Y0 << 8) | ((uint32_t)Y1 << 24);         \
    }                                                                                  \
}
BW_LOOP(bw_loop_p, 1)
BW_LOOP(bw_loop_m, -1)

static void fast_loop(const yg_ctx *g, uint32_t *w, int nw, const uint8_t *p)
{
    const int8_t *MY = g->M[0], *MU = g->M[1], *MV = g->M[2];
    const uint8_t *cy = CY, *cc = CC;
    const int qs = g->qs, r1 = qs ? 1 << (qs - 1) : 0, r2 = qs ? r1 - 1 : 0, rm1 = qs ? 1 : 0;
    while (nw-- > 0) {
        uint32_t v = *w;
        int U = (int8_t)v, Y0 = (v >> 8) & 255, V = (int8_t)(v >> 16), Y1 = (int)(v >> 24);
        int rc = ((Y0 + Y1) >> 4) << 6, ru = r2 + (p[0] & rm1);   /* rank parity: unbiased chroma rounding */
        int dY0 = MY[((Y0 >> 3) << 6) | p[0]];
        int dY1 = MY[((Y1 >> 3) << 6) | p[1]];
        int dU = MU[rc | p[YG_TS]];
        int dV = MV[rc | p[2 * YG_TS]];
        p += 2;
        Y0 = cy[Y0 + ((dY0 + r1) >> qs)];
        Y1 = cy[Y1 + ((dY1 + r2) >> qs)];
        {
            uint32_t Uo = cc[U + ((dU + ru) >> qs)], Vo = cc[V + ((dV + (r1 + r2 - ru)) >> qs)];
            *w++ = Uo | ((uint32_t)Y0 << 8) | (Vo << 16) | ((uint32_t)Y1 << 24);
        }
    }
}

static void interior(const yg_ctx *g, uint32_t *w, int nw, const uint8_t *p, int d)
{
    if (g->mode == YG_BW)        (d > 0 ? bw_loop_p : bw_loop_m)(g, w, nw, p);
    else if (g->tier == YG_FULL) (d > 0 ? full_loop_p : full_loop_m)(g, w, nw, p);
    else                         fast_loop(g, w, nw, p);
}

/* ------------------------------------------------------------------ blended (seam) words */
typedef struct {
    int n, shift;
    const uint8_t *p[4];  /* rank address for pixel 0 of the word, layer 0 */
    int d[4];             /* step to pixel 1 */
    int wt[2][4];         /* weight per pixel q, window k */
} yg_mix;

static inline int mixd(const int8_t *M, int row6, const yg_mix *m, int q, int loff)
{
    int k, acc = 0;
    for (k = 0; k < m->n; k++)
        acc += m->wt[q][k] * M[row6 | m->p[k][q * m->d[k] + loff]];
    return (acc + (1 << (m->shift - 1))) >> m->shift;   /* stays in Q(qs) */
}

static void word_mix(const yg_ctx *g, uint32_t *w, const yg_mix *m)
{
    const uint8_t *rm = RM, *cy = CY, *cc = CC;
    uint32_t v = *w;
    int U = (int8_t)v, Y0 = (v >> 8) & 255, V = (int8_t)(v >> 16), Y1 = (int)(v >> 24);
    int qs = g->qs, r1 = qs ? 1 << (qs - 1) : 0, r2 = qs ? r1 - 1 : 0;
    if (g->mode == YG_BW) {
        int d0 = mixd(g->M[0], (Y0 >> 3) << 6, m, 0, 0);
        int d1 = mixd(g->M[0], (Y1 >> 3) << 6, m, 1, 0);
        Y0 = cy[Y0 + ((d0 + r1) >> qs)];
        Y1 = cy[Y1 + ((d1 + r2) >> qs)];
        *w = (v & 0x00FF00FFu) | ((uint32_t)Y0 << 8) | ((uint32_t)Y1 << 24);
        return;
    }
    if (g->tier == YG_FULL) {
        int sY = 8 + qs, rY = 1 << (sY - 1), sC = 9 + qs + g->cs, rC = 1 << (sC - 1);
        int rv = (359 * V + 128) >> 8, gv = (88 * U + 183 * V + 128) >> 8, bu = (454 * U + 128) >> 8;
        int dR0 = mixd(g->M[0], rm[Y0 + rv] << 6, m, 0, 0);
        int dG0 = mixd(g->M[1], rm[Y0 - gv] << 6, m, 0, YG_TS);
        int dB0 = mixd(g->M[2], rm[Y0 + bu] << 6, m, 0, 2 * YG_TS);
        int dR1 = mixd(g->M[0], rm[Y1 + rv] << 6, m, 1, 0);
        int dG1 = mixd(g->M[1], rm[Y1 - gv] << 6, m, 1, YG_TS);
        int dB1 = mixd(g->M[2], rm[Y1 + bu] << 6, m, 1, 2 * YG_TS);
        int sR = dR0 + dR1, sG = dG0 + dG1, sB = dB0 + dB1;
        uint32_t Uo, Vo;
        Y0 = cy[Y0 + ((77 * dR0 + 150 * dG0 + 29 * dB0 + rY) >> sY)];
        Y1 = cy[Y1 + ((77 * dR1 + 150 * dG1 + 29 * dB1 + rY) >> sY)];
        Uo = cc[U + ((-43 * sR - 85 * sG + 128 * sB + rC) >> sC)];
        Vo = cc[V + ((128 * sR - 107 * sG - 21 * sB + rC) >> sC)];
        *w = Uo | ((uint32_t)Y0 << 8) | (Vo << 16) | ((uint32_t)Y1 << 24);
    } else {
        int rc = ((Y0 + Y1) >> 4) << 6;
        int dY0 = mixd(g->M[0], (Y0 >> 3) << 6, m, 0, 0);
        int dY1 = mixd(g->M[0], (Y1 >> 3) << 6, m, 1, 0);
        int dU = mixd(g->M[1], rc, m, 0, YG_TS);
        int dV = mixd(g->M[2], rc, m, 0, 2 * YG_TS);
        uint32_t Uo, Vo;
        Y0 = cy[Y0 + ((dY0 + r1) >> qs)];
        Y1 = cy[Y1 + ((dY1 + r2) >> qs)];
        {   int ru = qs ? r2 + (dY0 & 1) : 0;   /* unbiased chroma rounding, as in fast_loop */
            Uo = cc[U + ((dU + ru) >> qs)];
            Vo = cc[V + ((dV + (r1 + r2 - ru)) >> qs)]; }
        *w = Uo | ((uint32_t)Y0 << 8) | (Vo << 16) | ((uint32_t)Y1 << 24);
    }
}

/* AV1-style 2-sample overlap weights (Norkin & Birkbeck, DCC 2018): 27/17, 17/27 */
static const int OW[2][2] = { { 27, 17 }, { 17, 27 } };

static void set_hseam(yg_mix *m, const yg_win *l, const yg_win *c)
{
    m->n = 2; m->shift = 5;
    m->p[0] = l->p + 32 * l->d; m->d[0] = l->d;   /* left block continuation cols 32,33 */
    m->p[1] = c->p;             m->d[1] = c->d;
    m->wt[0][0] = OW[0][0]; m->wt[0][1] = OW[0][1];
    m->wt[1][0] = OW[1][0]; m->wt[1][1] = OW[1][1];
}
static void set_vmix(yg_mix *m, const yg_win *u, const yg_win *c, int i, int j)
{
    m->n = 2; m->shift = 5;
    m->p[0] = u->p + i * u->d; m->d[0] = u->d;    /* block above, rows 32,33 */
    m->p[1] = c->p + i * c->d; m->d[1] = c->d;
    m->wt[0][0] = m->wt[1][0] = OW[j][0];
    m->wt[0][1] = m->wt[1][1] = OW[j][1];
}
static void set_corner(yg_mix *m, const yg_win *lu, const yg_win *u, const yg_win *l,
                       const yg_win *c, int j)
{
    int q;
    m->n = 4; m->shift = 10;
    m->p[0] = lu->p + 32 * lu->d; m->d[0] = lu->d;
    m->p[1] = u->p;               m->d[1] = u->d;
    m->p[2] = l->p + 32 * l->d;   m->d[2] = l->d;
    m->p[3] = c->p;               m->d[3] = c->d;
    for (q = 0; q < 2; q++) {
        m->wt[q][0] = OW[j][0] * OW[q][0];
        m->wt[q][1] = OW[j][0] * OW[q][1];
        m->wt[q][2] = OW[j][1] * OW[q][0];
        m->wt[q][3] = OW[j][1] * OW[q][1];
    }
}

/* ------------------------------------------------------------------ cut seams (YG_SEAM_CUT)
 * Segment k of row y covers x in [32k - jx(k,y), 32(k+1) - jx(k+1,y)), jx in {0,2} (even: whole
 * UYVY words), and reads block (k, (y + sy(k)) >> 5): block rows are staggered per column (brick
 * layout) and vertical seams are ragged, so no straight seam line exists. Window column = i + 2. */
static inline int jx_of(const yg_ctx *g, int k, int y)
{
    return (int)(((((uint32_t)k * 0x9E3779B1u) ^ ((uint32_t)y * 0x85EBCA6Bu) ^ g->seed) * 0xC2B2AE35u) >> 31) << 1;
}
static inline int sy_of(const yg_ctx *g, int k) { return (int)(hmix((uint32_t)k ^ g->seed ^ 0x5A17u) & 31u); }

static void uyvy_row_cut(const yg_ctx *g, uint32_t *w, int npix, int x0, int y)
{
    int x = x0, xe = x0 + npix, k = x0 >> 5;
    if (x0 >= ((k + 1) << 5) - jx_of(g, k + 1, y)) k++;
    while (x < xe) {
        int se = ((k + 1) << 5) - jx_of(g, k + 1, y), yy = y + sy_of(g, k), nw;
        yg_win c;
        if (se > xe) se = xe;
        win(g, k, yy >> 5, yy & 31, &c);
        nw = (se - x) >> 1;
        interior(g, w, nw, c.p + (x - (k << 5) + 2) * c.d, c.d);
        w += nw; x = se; k++;
    }
}

/* ------------------------------------------------------------------ UYVY driver */
void yg_uyvy_row(const yg_ctx *g, uint32_t *w, int npix, int x0, int y)
{
    int by = y >> 5, j = y & 31, x = x0, xe = x0 + npix;
    yg_mix m;
    if (g->seam) { uyvy_row_cut(g, w, npix, x0, y); return; }
    while (x < xe) {
        int bx = x >> 5, i = x & 31, se = (bx + 1) << 5;
        yg_win c;
        if (se > xe) se = xe;
        win(g, bx, by, j, &c);
        if (j >= 2) {
            if (i == 0) {
                yg_win l; win(g, bx - 1, by, j, &l);
                set_hseam(&m, &l, &c); word_mix(g, w, &m);
                w++; x += 2; i = 2;
            }
            if (x < se) {
                int nw = (se - x) >> 1;
                interior(g, w, nw, c.p + i * c.d, c.d);
                w += nw; x = se;
            }
        } else {
            yg_win u; win(g, bx, by - 1, j + 32, &u);
            if (i == 0) {
                yg_win l, lu;
                win(g, bx - 1, by, j, &l); win(g, bx - 1, by - 1, j + 32, &lu);
                set_corner(&m, &lu, &u, &l, &c, j); word_mix(g, w, &m);
                w++; x += 2; i = 2;
            }
            for (; x < se; x += 2, i += 2) { set_vmix(&m, &u, &c, i, j); word_mix(g, w++, &m); }
        }
    }
}

void yg_uyvy_frame(const yg_ctx *g, uint32_t *buf, int w, int h, int y0)
{
    int y;
    for (y = 0; y < h; y++) yg_uyvy_row(g, buf + (size_t)y * (w >> 1), w, 0, y0 + y);
}

/* ------------------------------------------------------------------ RGB strip driver */
static inline void rgb_apply(const yg_ctx *g, uint8_t *px, int dR, int dG, int dB, int rnd)
{
    const uint8_t *cy = CY;
    int qs = g->qs;
    if (g->cs) {
        int dY = (77 * dR + 150 * dG + 29 * dB + 128) >> 8;
        dR = dY + ((dR - dY) >> g->cs); dG = dY + ((dG - dY) >> g->cs); dB = dY + ((dB - dY) >> g->cs);
    }
    px[0] = cy[px[0] + ((dR + rnd) >> qs)];
    px[1] = cy[px[1] + ((dG + rnd) >> qs)];
    px[2] = cy[px[2] + ((dB + rnd) >> qs)];
}

static inline void rgb_px(const yg_ctx *g, uint8_t *px, const yg_mix *m, int q, int x)
{
    const uint8_t *rm = RM, *cy = CY;
    int qs = g->qs, rnd = qs ? (1 << (qs - 1)) - (x & 1) : 0;
    if (g->mode == YG_BW) {
        int lum = (77 * px[0] + 150 * px[1] + 29 * px[2] + 128) >> 8, d;
        d = (mixd(g->M[0], (lum >> 3) << 6, m, q, 0) + rnd) >> qs;
        px[0] = cy[px[0] + d]; px[1] = cy[px[1] + d]; px[2] = cy[px[2] + d];
    } else {
        rgb_apply(g, px, mixd(g->M[0], rm[px[0]] << 6, m, q, 0), mixd(g->M[1], rm[px[1]] << 6, m, q, YG_TS),
                  mixd(g->M[2], rm[px[2]] << 6, m, q, 2 * YG_TS), rnd);
    }
}

static void rgb_interior(const yg_ctx *g, uint8_t *px, int n, const uint8_t *p, int d, int x)
{
    const uint8_t *rm = RM, *cy = CY;
    const int8_t *MR = g->M[0], *MG = g->M[1], *MB = g->M[2];
    int qs = g->qs, r1 = qs ? 1 << (qs - 1) : 0, flip = qs ? 1 : 0, rnd = r1 - (x & 1) * flip, rs = qs ? 2 * r1 - 1 : 0;
    if (g->mode == YG_BW) {
        while (n-- > 0) {
            int lum = (77 * px[0] + 150 * px[1] + 29 * px[2] + 128) >> 8;
            int dd = (MR[((lum >> 3) << 6) | p[0]] + rnd) >> qs;
            px[0] = cy[px[0] + dd]; px[1] = cy[px[1] + dd]; px[2] = cy[px[2] + dd];
            px += 3; p += d; rnd = rs - rnd;
        }
        return;
    }
    while (n-- > 0) {
        rgb_apply(g, px, MR[(rm[px[0]] << 6) | p[0]], MG[(rm[px[1]] << 6) | p[YG_TS]],
                  MB[(rm[px[2]] << 6) | p[2 * YG_TS]], rnd);
        px += 3; p += d; rnd = rs - rnd;
    }
}

void yg_rgb_strip(const yg_ctx *g, uint8_t *rgb, int w, int nrows, int y0)
{
    int r;
    yg_mix m;
    for (r = 0; r < nrows; r++) {
        int y = y0 + r, by = y >> 5, j = y & 31, x = 0;
        uint8_t *px = rgb + (size_t)r * w * 3;
        if (g->seam) {
            int k = 0;
            while (x < w) {
                int se = ((k + 1) << 5) - jx_of(g, k + 1, y), yy = y + sy_of(g, k);
                yg_win c;
                if (se > w) se = w;
                win(g, k, yy >> 5, yy & 31, &c);
                rgb_interior(g, px, se - x, c.p + (x - (k << 5) + 2) * c.d, c.d, x);
                px += 3 * (se - x); x = se; k++;
            }
            continue;
        }
        while (x < w) {
            int bx = x >> 5, i = x & 31, se = (bx + 1) << 5;
            yg_win c;
            if (se > w) se = w;
            win(g, bx, by, j, &c);
            if (j >= 2) {
                if (i == 0) {
                    yg_win l; win(g, bx - 1, by, j, &l); set_hseam(&m, &l, &c);
                    rgb_px(g, px, &m, 0, x); px += 3; x++;
                    if (x < se) { rgb_px(g, px, &m, 1, x); px += 3; x++; }
                    i = 2;
                }
                if (x < se) { rgb_interior(g, px, se - x, c.p + i * c.d, c.d, x); px += 3 * (se - x); x = se; }
            } else {
                yg_win u; win(g, bx, by - 1, j + 32, &u);
                if (i == 0) {
                    yg_win l, lu; win(g, bx - 1, by, j, &l); win(g, bx - 1, by - 1, j + 32, &lu);
                    set_corner(&m, &lu, &u, &l, &c, j);
                    rgb_px(g, px, &m, 0, x); px += 3; x++;
                    if (x < se) { rgb_px(g, px, &m, 1, x); px += 3; x++; }
                    i = 2;
                }
                for (; x < se; x++, i++) { set_vmix(&m, &u, &c, i, j); rgb_px(g, px, &m, 0, x); px += 3; }
            }
        }
    }
}

/* ================================================================== init (film load) */
/* Template synthesis is integer (the scalars a_i, scale factors come from yg_init's float). */
#define TN (YG_TS * YG_TS)

unsigned yg_scratch_bytes(void)
{
    /* 3 layer fields int16 + 2 temp int16 + histogram */
    return (unsigned)(3 * TN * 2 + 3 * TN * 2 + 16384 * 4      /* 3 fields, 3 temps, histogram */
                      + 2 * 3 * YG_NR * YG_NQ * sizeof(float)); /* float tables of yg_init (no stack/bss) */
}

typedef struct { int r; int k[24]; long s2; } ykern;   /* Q12 kernel, s2 = sum k^2 */

static void make_kernel(ykern *K, float sig)
{
    int n, sum = 0;
    float f[24];
    float tot = 0;
    K->r = (int)ceilf(3.0f * sig);
    if (K->r < 1) K->r = 1;
    if (K->r > 11) K->r = 11;
    for (n = -K->r; n <= K->r; n++) { f[n + K->r] = expf(-(float)(n * n) / (2.0f * sig * sig)); tot += f[n + K->r]; }
    for (n = 0; n <= 2 * K->r; n++) { K->k[n] = (int)lrintf(4096.0f * f[n] / tot); sum += K->k[n]; }
    K->k[K->r] += 4096 - sum;
    K->s2 = 0;
    for (n = 0; n <= 2 * K->r; n++) K->s2 += (long)K->k[n] * K->k[n];
}

/* separable circular blur of in (int16) -> out (int16); out = in*K2 / 2^20 * 2^8 ... */
static void blur(const int16_t *in, int16_t *tmp, int16_t *out, const ykern *K)
{
    int x, y, n, r = K->r;
    for (y = 0; y < YG_TS; y++) {
        const int16_t *row = in + y * YG_TS;
        for (x = 0; x < YG_TS; x++) {
            int32_t acc = 0;
            for (n = -r; n <= r; n++) acc += K->k[n + r] * row[(x + n) & (YG_TS - 1)];
            tmp[y * YG_TS + x] = (int16_t)(acc >> 8);
        }
    }
    for (y = 0; y < YG_TS; y++)
        for (x = 0; x < YG_TS; x++) {
            int32_t acc = 0;
            for (n = -r; n <= r; n++) acc += K->k[n + r] * tmp[((y + n) & (YG_TS - 1)) * YG_TS + x];
            out[y * YG_TS + x] = (int16_t)(acc >> 12);
        }
}

/* white noise ~ Irwin-Hall(4 bytes) - 510, var 21845; layer c, crosstalk Q12 beta */
static inline int wn(uint32_t seed, int c, int i)
{
    uint32_t h = hmix((uint32_t)i * 0x9E3779B1u ^ (uint32_t)(c + 1) * 0xC2B2AE35u ^ seed);
    return (int)(h & 255) + (int)((h >> 8) & 255) + (int)((h >> 16) & 255) + (int)(h >> 24) - 510;
}

/* empirical ranks 0..63 of a Q10 field (exact quantiles) */
static void to_ranks(const int16_t *G, uint8_t *dst, int dstride, uint32_t *hist)
{
    int i;
    uint32_t cum = 0;
    memset(hist, 0, 16384 * 4);
    for (i = 0; i < TN; i++) { int v = G[i] + 8192; v = v < 0 ? 0 : v > 16383 ? 16383 : v; hist[v]++; }
    for (i = 0; i < 16384; i++) {
        uint32_t c = hist[i];
        hist[i] = (uint32_t)(((uint64_t)(2 * cum + c) * YG_NQ) / (2u * TN));   /* mid-rank */
        if (hist[i] > YG_NQ - 1) hist[i] = YG_NQ - 1;
        cum += c;
    }
    for (i = 0; i < TN; i++) {
        int v = G[i] + 8192; v = v < 0 ? 0 : v > 16383 ? 16383 : v;
        dst[(i / YG_TS) * dstride + (i % YG_TS)] = (uint8_t)hist[v];
    }
}

typedef struct {
    ykern kf[3], kc[3];
    int a1q[3], a2q[3];      /* Q12 component weights incl. unit-variance scaling */
    int betaq;               /* Q12 crosstalk */
    float fastw[3][3];       /* FAST: Y,U,V weights on layer fields */
    float cov[3][3], covp[3][3];  /* layer field covariance (full res / 2-px pair average) */
} ytp;

static float s_px(const yg_stock *s, float d_um, int c, float pitch)
{
    float iso = (float)s->iso_grain;
    return 0.3215f * d_um * powf(iso / 200.0f, 0.25f) * s->size_scale[c] / pitch;
}

static void plan_templates(yg_ctx *g, const yg_stock *s, float pitch, ytp *T)
{
    int c, L = s->nlayers;
    float rho = s->rho, beta;
    float varm, A48 = 3.14159265f * 24.0f * 24.0f / (pitch * pitch);
    if (L == 1 || rho <= 0.0f) beta = 0.0f;
    else if (fabsf(1.0f - 2.0f * rho) < 1e-4f) beta = 0.5f;
    else beta = (-2.0f + sqrtf(4.0f + 4.0f * rho * (1.0f - 2.0f * rho))) / (2.0f * (1.0f - 2.0f * rho));
    T->betaq = (int)lrintf(beta * 4096.0f);
    varm = 21845.0f * (1.0f + (L == 1 ? 0.0f : 2.0f * beta * beta));
    for (c = 0; c < L; c++) {
        float sf = s_px(s, s->d_fine_um, c, pitch), sc = s_px(s, s->d_clus_um, c, pitch);
        float S1, S2, a1, a2, sd1, sd2;
        long sum1 = 4096;
        g->s_fine[c] = sf; g->s_clus[c] = sc;
        make_kernel(&T->kf[c], sqrtf(sf * sf + 1.0f / 6.0f));
        make_kernel(&T->kc[c], sqrtf(sc * sc + 1.0f / 6.0f));
        S1 = (float)sum1 * sum1 / (float)T->kf[c].s2; S1 *= S1;   /* 2-D aperture sum of autocorr */
        S2 = (float)sum1 * sum1 / (float)T->kc[c].s2; S2 *= S2;
        a1 = sqrtf(S2 / (S1 + S2)); a2 = sqrtf(S1 / (S1 + S2));   /* equal W(0) per Stephenson */
        g->kappa[c] = sqrtf(A48 / (2.0f * S1 * S2 / (S1 + S2)));
        /* std of blurred field in int16 units: sqrt(varm) * s2 / (2^8 * 2^12) */
        sd1 = sqrtf(varm) * (float)T->kf[c].s2 / (256.0f * 4096.0f);
        sd2 = sqrtf(varm) * (float)T->kc[c].s2 / (256.0f * 4096.0f);
        T->a1q[c] = (int)lrintf(a1 / sd1 * 1024.0f * 65536.0f);  /* Q16 -> Q10 field */
        T->a2q[c] = (int)lrintf(a2 / sd2 * 1024.0f * 65536.0f);
    }
}

/* builds fields G_c (Q10, unit variance) into f[c]; integer per sample */
static void synth_fields(const ytp *T, int L, uint32_t tseed, int16_t *f[3], int16_t *t1, int16_t *t2, int16_t *t3)
{
    int c, i;
    for (c = 0; c < L; c++) {
        int16_t *m = f[c];     /* reuse output as noise input buffer */
        for (i = 0; i < TN; i++) {
            int n = wn(tseed, c, i);
            if (L == 3) {
                int o = wn(tseed, (c + 1) % 3, i) + wn(tseed, (c + 2) % 3, i);
                n += (T->betaq * o + 2048) >> 12;
            }
            m[i] = (int16_t)n;
        }
        /* fine component into t1, cluster into t2 (t1/t2 also used as blur temp) */
        blur(m, t3, t1, &T->kf[c]);           /* t1 = fine component */
        blur(m, t3, t2, &T->kc[c]);           /* t2 = cluster component */
        for (i = 0; i < TN; i++) {
            int32_t v = (int32_t)(((int64_t)T->a1q[c] * t1[i] + (int64_t)T->a2q[c] * t2[i] + 32768) >> 16);   /* Q10 */
            v = v < -8191 ? -8191 : v > 8191 ? 8191 : v;
            m[i] = (int16_t)v;
        }
    }
}

static float erfinv_n(float p)   /* inverse standard normal CDF (Acklam) */
{
    static const float a[6] = { -3.969683028665376e+01f, 2.209460984245205e+02f, -2.759285104469687e+02f,
                                1.383577518672690e+02f, -3.066479806614716e+01f, 2.506628277459239e+00f };
    static const float b[5] = { -5.447609879822406e+01f, 1.615858368580409e+02f, -1.556989798598866e+02f,
                                6.680131188771972e+01f, -1.328068155288572e+01f };
    static const float cc[6] = { -7.784894002430293e-03f, -3.223964580411365e-01f, -2.400758277161838e+00f,
                                 -2.549732539343734e+00f, 4.374664141464968e+00f, 2.938163982698783e+00f };
    static const float d[4] = { 7.784695709041462e-03f, 3.224671290700398e-01f, 2.445134137142996e+00f,
                                3.754408661907416e+00f };
    float q, r;
    if (p < 0.02425f) { q = sqrtf(-2 * logf(p));
        return (((((cc[0]*q+cc[1])*q+cc[2])*q+cc[3])*q+cc[4])*q+cc[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1); }
    if (p > 1 - 0.02425f) { q = sqrtf(-2 * logf(1 - p));
        return -(((((cc[0]*q+cc[1])*q+cc[2])*q+cc[3])*q+cc[4])*q+cc[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1); }
    q = p - 0.5f; r = q * q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1);
}

static float interp(const float *x, const float *y, int n, float v)
{
    int i;
    if (v <= x[0]) return y[0];
    if (v >= x[n - 1]) return y[n - 1];
    for (i = 1; i < n; i++)
        if (v <= x[i]) return y[i - 1] + (y[i] - y[i - 1]) * (v - x[i - 1]) / (x[i] - x[i - 1]);
    return y[n - 1];
}

/* display-referred density chain: per layer c, level v (0..255) and z -> v' */
typedef struct { const yg_stock *s; float a, D0[3], kap[3]; } ychain;

static float vref_of(const ychain *C, int c, float v, float *Dk)
{
    const yg_stock *s = C->s;
    float L = powf(fmaxf(v, 0.25f) / 255.0f, 2.2f), r = -log10f(L);
    float lo = s->rho_max * 0.002f, hi = s->rho_max * 0.998f;
    (void)c;
    if (s->mode == YG_SLIDE) { if (r > s->rho_max) r = s->rho_max; *Dk = r; }
    else { if (r < lo) r = lo; if (r > hi) r = hi; *Dk = C->D0[c] + logf(s->rho_max / r - 1.0f) / C->a; }
    return 255.0f * powf(10.0f, -r / 2.2f);
}
static float sigma_px(const ychain *C, int c, float Dk)
{
    const yg_stock *s = C->s;
    float D = Dk;
    if (s->mode == YG_SLIDE) D = Dk + s->dn[c][0];            /* slide density = display density + Dmin */
    else D = Dk - s->ei_stops * 0.30103f * s->neg_gamma;      /* under-exposure: thinner negative */
    return s->amount * C->kap[c] * interp(s->dn[c], s->sig[c], s->np[c], D);
}
static float v_of(const ychain *C, int c, float Dk)
{
    const yg_stock *s = C->s;
    float r;
    if (s->mode == YG_SLIDE) r = Dk;
    else r = s->rho_max / (1.0f + expf(C->a * (Dk - C->D0[c])));
    if (r < 0) r = 0;
    return 255.0f * powf(10.0f, -r / 2.2f);
}

static void quantiles(float z[YG_NQ])
{
    int r; float m2 = 0;
    for (r = 0; r < YG_NQ; r++) { z[r] = erfinv_n((r + 0.5f) / YG_NQ); m2 += z[r] * z[r]; }
    m2 = sqrtf(m2 / YG_NQ);
    for (r = 0; r < YG_NQ; r++) z[r] /= m2;
}

/* float tables -> int8 with common qs */
static void quantize_tables(yg_ctx *g, float (*F)[YG_NR][YG_NQ], int L)
{
    int c, k, r;
    float mx = 0;
    for (c = 0; c < L; c++) for (k = 0; k < YG_NR; k++) for (r = 0; r < YG_NQ; r++)
        if (fabsf(F[c][k][r]) > mx) mx = fabsf(F[c][k][r]);
    g->qs = mx * 4.0f <= 127.0f ? 2 : mx * 2.0f <= 127.0f ? 1 : 0;
    for (c = 0; c < L; c++) for (k = 0; k < YG_NR; k++) for (r = 0; r < YG_NQ; r++) {
        int q = (int)lrintf(F[c][k][r] * (float)(1 << g->qs));
        g->M[c][k * YG_NQ + r] = (int8_t)(q < -128 ? -128 : q > 127 ? 127 : q);
    }
}

static void cov_fields(int16_t *f[3], int pair, float cov[3][3])
{
    int a, b, i;
    for (a = 0; a < 3; a++) for (b = a; b < 3; b++) {
        int64_t acc = 0;
        for (i = 0; i < TN; i++) {
            int va = f[a][i], vb = f[b][i];
            if (pair) { int i1 = (i & ~(YG_TS - 1)) | ((i + 1) & (YG_TS - 1));
                        va = (va + f[a][i1]) >> 1; vb = (vb + f[b][i1]) >> 1; }
            acc += (int64_t)va * vb;
        }
        cov[a][b] = cov[b][a] = (float)acc * (1.0f / ((float)TN * 1048576.0f));
    }
}


static ytp g_plan;   /* kept for per-shot template rebuilds (one film loaded at a time) */
static const float wY[3] = { 0.299f, 0.587f, 0.114f };
static const float wU[3] = { -0.168736f, -0.331264f, 0.5f };
static const float wV[3] = { 0.5f, -0.418688f, -0.081312f };

static inline int16_t sat13(int32_t v) { return (int16_t)(v < -8191 ? -8191 : v > 8191 ? 8191 : v); }

int yg_build_templates(yg_ctx *g, const yg_stock *s, uint32_t tseed, float pitch, void *scratch)
{
    int16_t *f[3], *t1, *t2, *t3;
    uint32_t *hist;
    int c, i, L = s->nlayers;
    char *sp = (char *)scratch;
    (void)pitch;
    f[0] = (int16_t *)sp; f[1] = f[0] + TN; f[2] = f[1] + TN;
    t1 = f[2] + TN; t2 = t1 + TN; t3 = t2 + TN; hist = (uint32_t *)(t3 + TN);
    g->tseed = tseed;
    synth_fields(&g_plan, L, tseed, f, t1, t2, t3);
    if (L == 3) { cov_fields(f, 0, g_plan.cov); cov_fields(f, 1, g_plan.covp); memcpy(g->cov, g_plan.cov, sizeof g->cov); }
    if (g->tier == YG_FAST && L == 3) {
        int wq[3][3], k, a, b;
        for (k = 0; k < 3; k++) {          /* unit variance of each combined field */
            float var = 0, (*cv)[3] = k == 0 ? g_plan.cov : g_plan.covp;
            for (a = 0; a < 3; a++) for (b = 0; b < 3; b++) var += g_plan.fastw[k][a] * g_plan.fastw[k][b] * cv[a][b];
            for (c = 0; c < 3; c++) wq[k][c] = (int)lrintf(g_plan.fastw[k][c] / sqrtf(var) * 4096.0f);
        }
        for (k = 0; k < 3; k++) {          /* Y full res; U, V = 2-px box (4:2:2 siting) */
            for (i = 0; i < TN; i++) {
                int i1 = (i & ~(YG_TS - 1)) | ((i + 1) & (YG_TS - 1));
                int32_t v;
                if (k == 0) v = (wq[0][0] * f[0][i] + wq[0][1] * f[1][i] + wq[0][2] * f[2][i] + 2048) >> 12;
                else v = (wq[k][0] * (f[0][i] + f[0][i1]) + wq[k][1] * (f[1][i] + f[1][i1]) +
                          wq[k][2] * (f[2][i] + f[2][i1]) + 4096) >> 13;
                t1[i] = sat13(v);
            }
            to_ranks(t1, g->tmpl + k * YG_TS, 3 * YG_TS, hist);
        }
    } else {
        for (c = 0; c < L; c++) to_ranks(f[c], g->tmpl + c * YG_TS, 3 * YG_TS, hist);
        for (c = L; c < 3; c++) for (i = 0; i < YG_TS; i++) memset(g->tmpl + (i * 3 + c) * YG_TS, 32, YG_TS);
    }
    return 0;
}

int yg_init(yg_ctx *g, const yg_stock *s, int tier, uint32_t tseed, float pitch,
            uint8_t *tmpl_mem, void *scratch)
{
    /* float tables live after the template scratch: the camera task stack is small */
    float (*F)[YG_NR][YG_NQ] = (float (*)[YG_NR][YG_NQ])((char *)scratch + 6 * TN * 2 + 16384 * 4);
    float z[YG_NQ], umid, sd[3][YG_NR];
    ychain C;
    int c, k, r, v, L = s->nlayers;
    memset(g, 0, sizeof *g);
    g->tmpl = tmpl_mem;
    g->mode = s->mode; g->nlayers = L;
    g->seam = (tier & YG_SEAM_CUT) ? 1 : 0;
    tier &= 15;
    g->tier = (L == 1) ? YG_FULL : tier;
    g->cs = (L == 3) ? s->chroma_shift : 0;
    for (v = -256; v < 512; v++) {
        int cv = v < 0 ? 0 : v > 255 ? 255 : v;
        g->rowmap[v + 256] = (uint8_t)(cv >> 3);
        g->clampY[v + 256] = (uint8_t)cv;
    }
    for (v = -256; v < 256; v++) { int cv = v < -128 ? -128 : v > 127 ? 127 : v; g->clampC[v + 256] = (uint8_t)(int8_t)cv; }
    yg_set_shot(g, 0);

    plan_templates(g, s, pitch, &g_plan);
    C.s = s;
    umid = 0.745f / s->rho_max;                    /* display density of 18% grey = 0.745 */
    C.a = s->print_gamma / (s->rho_max * umid * (1.0f - umid));
    for (c = 0; c < L; c++) { C.D0[c] = s->dmid[c] - logf(1.0f / umid - 1.0f) / C.a; C.kap[c] = g->kappa[c]; }
    quantiles(z);

    /* per-layer quantile tables ("probability map"): v' = chain(D + sigma(D) z) - v, mean removed */
    for (c = 0; c < L; c++)
        for (k = 0; k < YG_NR; k++) {
            float vk = 8.0f * k + 3.5f, Dk, vr = vref_of(&C, c, vk, &Dk), sg = sigma_px(&C, c, Dk);
            float mean = 0, m2 = 0;
            for (r = 0; r < YG_NQ; r++) { F[c][k][r] = v_of(&C, c, Dk + sg * z[r]) - vr; mean += F[c][k][r]; }
            mean /= YG_NQ;
            for (r = 0; r < YG_NQ; r++) { F[c][k][r] -= mean; m2 += F[c][k][r] * F[c][k][r]; }
            sd[c][k] = sqrtf(m2 / YG_NQ);
        }
    for (c = 0; c < L; c++) g->sig_mid_px[c] = sd[c][14];   /* row of v = 112..119 */

    if (g->tier == YG_FAST) {
        float (*Fs)[YG_NR][YG_NQ] = F + 3;
        for (c = 0; c < 3; c++) {
            g_plan.fastw[0][c] = wY[c] * sd[c][14];
            g_plan.fastw[1][c] = wU[c] * sd[c][14];
            g_plan.fastw[2][c] = wV[c] * sd[c][14];
        }
        yg_build_templates(g, s, tseed, pitch, scratch);   /* also measures layer covariances */
        for (k = 0; k < YG_NR; k++) {
            float vy = 0, vu = 0, vv = 0, como = 0; int a, b;
            for (a = 0; a < 3; a++) for (b = 0; b < 3; b++) {
                /* fields are not unit variance (about 1.66): use correlations, the pair
                   average normalised by the full-resolution variances */
                float nrm = 1.0f / sqrtf(g_plan.cov[a][a] * g_plan.cov[b][b]);
                vy += wY[a] * wY[b] * sd[a][k] * sd[b][k] * g_plan.cov[a][b] * nrm;
                vu += wU[a] * wU[b] * sd[a][k] * sd[b][k] * g_plan.covp[a][b] * nrm;
                vv += wV[a] * wV[b] * sd[a][k] * sd[b][k] * g_plan.covp[a][b] * nrm;
            }
            /* luma shape from the comonotone sum (keeps skew), rescaled to the true sigma */
            for (r = 0; r < YG_NQ; r++) {
                Fs[0][k][r] = wY[0] * F[0][k][r] + wY[1] * F[1][k][r] + wY[2] * F[2][k][r];
                como += Fs[0][k][r] * Fs[0][k][r];
            }
            como = sqrtf(como / YG_NQ);
            for (r = 0; r < YG_NQ; r++) {
                Fs[0][k][r] *= como > 1e-6f ? sqrtf(vy) / como : 0.0f;
                Fs[1][k][r] = sqrtf(vu) * z[r] / (float)(1 << g->cs);
                Fs[2][k][r] = sqrtf(vv) * z[r] / (float)(1 << g->cs);
            }
        }
        memcpy(F, Fs, 3 * sizeof *F);
        quantize_tables(g, F, 3);
        return 0;
    }
    quantize_tables(g, F, L);
    return yg_build_templates(g, s, tseed, pitch, scratch);
}
