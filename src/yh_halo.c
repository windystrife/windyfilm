/* yh_halo.c - halation + bloom for Canon UYVY (see yh_halo.h). C99, integer per-pixel code.
 * Module copy of proto/halation_bloom/cam/src_fix/yh_halo.c (md5 95ebce8d), built with -DYH_BAKED.
 * Changes (WindyStrife camera port):
 *  - yh_init's suffix-minimum tables (2 KB) moved from the stack into g->trow;
 *  - camera review 2026-11-10: gather_hot boosts all channels of a pixel by the ramp gain of its brightest
 *    channel (rgain, qk); the YH_TOP mirror is limited to wide light at the seam (seam_mirror); the
 *    YH_BOTTOM H0 hand-off keeps the bottom build's own cells below the seam where they are larger. */
#include "yh_halo.h"
#include <string.h>
#include <math.h>
#include "yh_weights.h"

#if YH_NB != YH_NBAS
#error "yh_weights.h basis count mismatch"
#endif

/* ML builds modules with -Os, which leaves these per-pixel helpers out of line (one call per word or channel);
 * force them inline so -Os and -O2 run the same inner loops. */
#define YH_INLINE static inline __attribute__((always_inline))
#ifndef YH_SKIP
#define YH_SKIP 0            /* 1: exact skip of cells whose halo rounds to 0 (bit-identical output); pays only on slide (-0.44 s window), costs 0.1-0.5 s on neg/800T/bloom/B&W at -Os */
#endif

/* Camera build (-DYH_BAKED): the display-model tables come from yh_tables.h (made on the host by cam/mk_tables.c
 * with the double-precision init below), because the EOS M ML core exports no pow/log/tanh/floor (only the
 * single-precision powf/logf/expf/floorf) and -ffast-math -fsingle-precision-constant changes the tables. */
#ifdef YH_BAKED
#include "yh_tables.h"
/* V14T (owner 2026-11-13 "slim it"): wf_mist.c reads four of these tables through global aliases, so the module holds one copy
 * of them, not two (-5 KB) */
extern const uint32_t yh_g_lin_q30[256] __attribute__((alias("yh_t_lin_q30")));
extern const uint8_t yh_g_encA[1024] __attribute__((alias("yh_t_encA")));
extern const uint8_t yh_g_encB[1024] __attribute__((alias("yh_t_encB")));
extern const uint8_t yh_g_encC[2048] __attribute__((alias("yh_t_encC")));
static uint64_t yh_exp2_q30(uint64_t base, int x)      /* base * 2^(x / 65536), x in Q16 EV (table + linear interp.) */
{
    int n = x >> 16;
    uint32_t f = (uint32_t)x & 0xffffu, i = f >> 8, r = f & 255u;
    uint64_t m = yh_t_exp2_q30[i] + (((uint64_t)(yh_t_exp2_q30[i + 1] - yh_t_exp2_q30[i]) * r) >> 8);
    uint64_t v = (base * m) >> 30;
    return n >= 0 ? v << n : v >> -n;
}
#endif

/* ------------------------------------------------------------------ display model (= yh_curve.py) */
#ifndef YH_BAKED
static const double SH_A = 0.55;
static double oetf(double x) { if (x <= 0) return 0; if (x >= 1) return 1;
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1 / 2.4) - 0.055; }
static double eotf(double v) { if (v <= 0) return 0; if (v >= 1) return 1;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double shoulder(double L) { if (L <= SH_A) return L < 0 ? 0 : L;
    return SH_A + (1 - SH_A) * tanh((L - SH_A) / (1 - SH_A)); }
static double ishoulder(double s) { if (s <= SH_A) return s; double x = (s - SH_A) / (1 - SH_A);
    if (x > 1 - 1e-12) x = 1 - 1e-12; return SH_A + (1 - SH_A) * 0.5 * log((1 + x) / (1 - x)); }
static double lin_to_disp(double L) { return 255.0 * oetf(shoulder(L)); }
static double disp_to_lin(double v) { return ishoulder(eotf(v / 255.0)); }
#endif

/* ------------------------------------------------------------------ stocks (GUESSES, see README) */
/* Layer crosstalk (GUESS, physics direction LIKELY): the light reflected at the base re-exposes a layer only in
 * proportion to that layer's sensitivity to it. Red layer: red light + a little of the camera's green (yellow)
 * light; green layer: green + a sliver of red; blue light hardly reaches the base (blue layer + yellow filter).
 * Rows sum to 1, so a neutral source gets exactly the same halo as with the old rank-1 'spec' coupling, but a
 * red light now gets a red halo (no green added), a green light a weak green one, a blue light none.
 * (v1 used hal (x) spec: every source got an orange halo; red neon / tail lights turned orange-yellow.) */
#define YH_XT_COLOUR { { 0.90f, 0.10f, 0.0f }, { 0.05f, 0.95f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
#define YH_XT_BW     { { 0.30f, 0.59f, 0.11f }, { 0.30f, 0.59f, 0.11f }, { 0.30f, 0.59f, 0.11f } }
#define YH_XT_DIAG   { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
const yh_stock yh_stocks[YH_NSTOCK] = {
    { "colour negative (rem-jet / AH backing)",      yh_w_neg,   YH_XT_COLOUR, 1.0f, 2.0f, 4.0f },
    { "tungsten negative, no rem-jet",               yh_w_c800t, YH_XT_COLOUR, 1.0f, 2.0f, 4.0f },
    { "slide (E-6, AH underlayer)",                  yh_w_slide, YH_XT_COLOUR, 1.0f, 2.0f, 4.0f },
    { "B&W (grey AH base)",                          yh_w_bw,    YH_XT_BW,     1.0f, 2.0f, 4.0f },
    /* spektrafilm: each layer re-exposed by its own light (diagonal, as in its per-channel convolution); knee, clipped
     * boost and point boost as the rows above (its boost_highlights is off by default and works on unclipped raw) */
    { "spektrafilm still, strong AH",                yh_w_spk_s,   YH_XT_DIAG, 1.0f, 2.0f, 4.0f },
    { "spektrafilm still, weak AH",                  yh_w_spk_w,   YH_XT_DIAG, 1.0f, 2.0f, 4.0f },
    { "spektrafilm cine, strong AH",                 yh_w_spk_c,   YH_XT_DIAG, 1.0f, 2.0f, 4.0f },
    { "spektrafilm cine, no AH (no rem-jet)",        yh_w_spk_n,   YH_XT_DIAG, 1.0f, 2.0f, 4.0f },
    { "spektrafilm-style B&W, strong AH",            yh_w_spk_bw,  YH_XT_BW,   1.0f, 2.0f, 4.0f },
    { "spektrafilm-style B&W, weak AH",              yh_w_spk_bww, YH_XT_BW,   1.0f, 2.0f, 4.0f },
    /* the light wrap of Genesis Pro 1.6.6: the pyramid's weights, crosstalk and knee are not read (yh_params.gen
     * switches the stage to the plugin's own mask / blur / gate, see yh_gen_* at the end of this file) */
    { "light wrap (arctan mask, difference of blur)", yh_w_neg, YH_XT_DIAG, 1.0f, 2.0f, 4.0f },
};

void yh_default_params(yh_params *p, int stock)
{
    p->stock = stock; p->hal_q8 = 256; p->bloom_q8 = 0; p->bloom_frac = 0.06f;
    p->knee_ev = -99.f; p->boost_ev = -99.f; p->pboost_ev = -99.f; p->tint_q8 = 0; p->ystep = 2; p->xfade = 8;
    p->exact_only = 0; p->pair_t = 40; p->gauge_q8 = 256;
    /* YH_GEN (Genesis light wrap): the plugin's own defaults - halation_strength 1.0 (hal_q8 256, above),
     * light_wrap_radius 10, halation_film_gauge 35 mm, and the mask threshold 0.94 of lightWrapI1Val */
    p->gen = stock == YH_GEN ? 1 : 0;
    p->gen_radius = 10;
    p->gen_gauge_q8 = 256;
    p->gen_thr = 240;                                   /* 0.94 * 255 = 239.7 */
    p->gen_rgb_q8[0] = 102; p->gen_rgb_q8[1] = 128; p->gen_rgb_q8[2] = 115;   /* 0.40 / 0.50 / 0.45 */
}

#ifndef YH_RAMP0
#define YH_RAMP0 253                                     /* first display code of the clipped-energy ramp */
#endif
#ifndef YH_CLIPV
#define YH_CLIPV 254                                     /* a word counts as clipped (point boost) from this code */
#endif
#if 256 - YH_RAMP0 > 8 || YH_RAMP0 > 255
#error "yh_ctx.rgain holds 8 ramp codes"
#endif
#define XSAVE_MAX 24
#ifndef YH_MIRROR
#define YH_MIRROR 4
#endif
#ifndef YH_MIRROR_ERODE
#define YH_MIRROR_ERODE 2                                /* Diana mirror: +-2 L0 cells (40 px wide) of light at the seam */
#endif
#define YH_H0X 4                                         /* Diana: H0 rows below the seam blended from the top build */
#define YH_MIRROR_NOTE                                    /* Diana: L0 rows mirrored below the seam (top build) */
/* YH_GEN (Genesis light wrap): its three passes are implemented at the end of this file */
static void yh_gen_tables(yh_ctx *g, const yh_params *p);
static void yh_gen_gather(yh_ctx *g, const uint32_t *p, int npix, int x0, int y);
static void yh_gen_build(yh_ctx *g, int y_begin, int y_end, int mode);
static void yh_gen_apply(yh_ctx *g, uint32_t *p, int npix, int x0, int y);
static void sizes(int W, int H, int *lw, int *lh)
{
    lw[0] = (W + 7) >> 3; lh[0] = (H + 7) >> 3;
    for (int k = 1; k < YH_NLEV; k++) { lw[k] = (lw[k - 1] + 1) >> 1; lh[k] = (lh[k - 1] + 1) >> 1; }
}
#define AL4(n) (((n) + 3u) & ~3u)
uint32_t yh_mem_bytes(int W, int H)
{
    int lw[YH_NLEV], lh[YH_NLEV]; uint32_t n = 0;
    sizes(W, H, lw, lh);
    for (int k = 0; k < YH_NLEV; k++) n += AL4(3u * lw[k] * lh[k] * 2) * (k ? 2 : 1);
    n += 8u * lw[0] * lh[0];                             /* H0 */
    for (int k = 0; k < 3; k++) n += AL4(2u * lw[k] * lh[k]);   /* C0..C2 */
    n += 4 * AL4(lh[0] * 2) + 2 * AL4(lh[1] * 2);
    n += 4 * lw[0] * 4;                                  /* gacc */
    n += 3 * (4 * (lw[0] + 4)) * 4;                      /* trow x3 */
    n += AL4(3 * lw[2] * XSAVE_MAX * 2);                 /* xsave */
    n += 8u * lw[0] * (YH_H0X + 1) + 4u * (YH_H0X + 1);   /* h0save + spans */
    return n;
}

/* ------------------------------------------------------------------ init */
#ifndef YH_BAKED
static int enc_val(uint32_t q, double L255)
{
    double v = lin_to_disp((q + 0.0) / 1048576.0 * L255);
    int i = (int)floor(v + 0.5);
    return i < 0 ? 0 : i > 255 ? 255 : i;
}
#endif
YH_INLINE int enc(const yh_ctx *g, uint32_t L)
{
    if (L < (1u << 12)) return g->encA[L >> 2];
    if (L < (1u << 16)) return g->encB[L >> 6];
    if (L < (1u << 20)) return g->encC[L >> 9];
    return 255;
}

/* drive table entries v0..511 (index v + 256; codes > 255 read as 255) and the ramp gains for a clipped boost of boost_q16
 * EV (Q16): the codes >= YH_RAMP0 ramp (log) from their true light to the boosted top, the others are exact. yh_init
 * (v0 = -256) and yh_set_hot (v0 = YH_RAMP0: only the ramp depends on the boost; kneev can only get smaller, a
 * conservative skip bound) */
static void hot_tables(yh_ctx *g, int boost_q16, int v0)
{
#ifdef YH_BAKED
    /* drive D = (L - Lk) / L255 * 1024 from L / L255 in Q30. Lk / L255 = (0.18 / L255) * 2^knee; codes >= YH_RAMP0
     * ramp to the boosted top: Lr0 * (Lb / Lr0)^t = Lr0 * 2^(t * (log2(L255 / Lr0) + boost)), t = (v - R0) / (255 - R0) */
    uint64_t lk = yh_exp2_q30(YH_T_GREY_Q30, g->knee_q16);
    for (int v = v0; v < 512; v++) {
        int vc = v < 0 ? 0 : v > 255 ? 255 : v;
        uint64_t L = vc < YH_RAMP0 ? yh_t_lin_q30[vc]
                   : yh_exp2_q30(yh_t_lin_q30[YH_RAMP0], (vc - YH_RAMP0) * (YH_T_LOG2R_Q16 + boost_q16) / (255 - YH_RAMP0));
        uint64_t di = L > lk ? (((L - lk) >> 19) + 1) >> 1 : 0;          /* * 1024 / 2^30, rounded */
        g->dl[v + 256] = (uint16_t)(di > 65535 ? 65535 : di);
        if (di > 0 && v >= 0 && v < g->kneev) g->kneev = v;
    }
    /* brightest channel code m in the ramp: gain = ramped / true linear of m, Q8 (gather_hot). 32-bit divide:
     * (Lr >> 8) < 2^28 for boost <= 6 EV, lin >> 16 ~ 2^14 */
    for (int m = YH_RAMP0; m <= 255; m++) {
        uint64_t Lr = yh_exp2_q30(yh_t_lin_q30[YH_RAMP0], (m - YH_RAMP0) * (YH_T_LOG2R_Q16 + boost_q16) / (255 - YH_RAMP0));
        uint32_t den = yh_t_lin_q30[m] >> 16, gq = den ? (uint32_t)(Lr >> 8) / den : 256;
        g->rgain[m - YH_RAMP0] = (uint16_t)(gq < 256 ? 256 : gq > 16384 ? 16384 : gq);
    }
#else
    /* clipped-energy ramp: codes < YH_RAMP0 are inverted exactly; YH_RAMP0..255 ramp (log) to the boosted top.
     * v1 ramped from 250, which made unclipped near-whites (code 251-254 = +2.8..+3.1 EV) drive 1.4-3.3x their
     * true energy, and (with the >= 250 clipped flag) also get the point boost: white text glowed like neon. */
    double L255 = disp_to_lin(254.75), boost = boost_q16 / 65536.0;
    double Lk = 0.18 * pow(2.0, g->knee_q16 / 65536.0), Lr0 = disp_to_lin(YH_RAMP0), Lb = L255 * pow(2.0, boost);
    for (int v = v0; v < 512; v++) {
        int vc = v < 0 ? 0 : v > 255 ? 255 : v;
        double L = vc < YH_RAMP0 ? disp_to_lin(vc) : Lr0 * pow(Lb / Lr0, (vc - YH_RAMP0) / (255.0 - YH_RAMP0));
        double d = (L - Lk) / L255 * 1024.0;
        int di = d <= 0 ? 0 : d >= 65535 ? 65535 : (int)(d + 0.5);
        g->dl[v + 256] = (uint16_t)di;
        if (di > 0 && v >= 0 && v < g->kneev) g->kneev = v;
    }
    for (int m = YH_RAMP0; m <= 255; m++) {
        double Lm = m < 255 ? disp_to_lin(m) : L255, Lr = Lr0 * pow(Lb / Lr0, (m - YH_RAMP0) / (255.0 - YH_RAMP0));
        double gq = floor(256.0 * Lr / Lm + 0.5);
        g->rgain[m - YH_RAMP0] = (uint16_t)(gq < 256 ? 256 : gq > 16384 ? 16384 : gq);
    }
#endif
}

/* per shot (worker, before the gather of an NN / Diana TOP request): the exposure decides how much brighter than the top
 * code a clipped pixel is, and how strong the halation is (wf_fx.c fx_hot). Nothing to do when the scales are those of
 * the last call. hs 256 gives M0 exactly ((x * 256 + 128) >> 8 == x, also for negative x). */
void yh_set_hot(yh_ctx *g, int hb_q8, int hp_q8, int hs_q8)
{
    if (g->gen)
        return;                                  /* the light wrap has no clipped-energy ramp and no per-shot scale */
    hb_q8 = hb_q8 < 0 ? 0 : hb_q8 > 256 ? 256 : hb_q8;
    hp_q8 = hp_q8 < 0 ? 0 : hp_q8 > 256 ? 256 : hp_q8;
    hs_q8 = hs_q8 < 0 ? 0 : hs_q8 > 256 ? 256 : hs_q8;
    if (hs_q8 != g->hs_q8) {
        g->hs_q8 = hs_q8;
        for (int b = 0; b < YH_NBAS; b++)
            for (int c = 0; c < 3; c++)
                for (int j = 0; j < 3; j++)
                    g->M[b][c][j] = g->M0[b][c][j] - g->Mh0[b][c][j] + ((g->Mh0[b][c][j] * hs_q8 + 128) >> 8);
    }
    if (hb_q8 == g->hb_q8 && hp_q8 == g->hp_q8)
        return;
    g->hb_q8 = hb_q8;
    g->hp_q8 = hp_q8;
    g->pb_q8 = (g->pb_base * hp_q8 + 128) >> 8;
    hot_tables(g, (int)(((int64_t)g->boost_q16 * hb_q8 + 128) >> 8), YH_RAMP0);
}

/* The gauge (yh_params.gauge_q8, 256 = 35 mm as fitted): halo and bloom keep their size on the film, so on a frame
 * 256 / gauge_q8 times narrower they cover that many times more pixels. The bases are about an octave apart (L0s, L0b,
 * L1 .. L5: the 1/8 grid sharp and blurred, then 1/16 .. 1/256 blurred), so a gauge moves each base's weight by
 * log2(256 / gauge_q8) bases: 16 mm (128) exactly one base coarser, 65 mm (474) 0.9 of a base finer, split between the
 * two bases it falls between. Weight that would leave the pyramid stays at its end (L5 or L0s); the total (the halo's
 * energy) is unchanged. 256: the weights as they are. Float, init only (exact IEEE ops: host = camera). */
static void yh_gauge_w(const float *w, float *o, int gq)
{
    int n = 0;
    float x = 256.0f / (float)(gq < 32 ? 32 : gq > 2048 ? 2048 : gq), f, t;
    if (gq == 256) {
        for (int b = 0; b < YH_NBAS; b++)
            o[b] = w[b];
        return;
    }
    for (int b = 0; b < YH_NBAS; b++)
        o[b] = 0.0f;
    while (x >= 2.0f) { x *= 0.5f; n++; }
    while (x < 1.0f) { x *= 2.0f; n--; }
    t = x - 1.0f;
    f = t * (1.34f - 0.34f * t);                 /* log2(x) on [1, 2), within 0.01 */
    for (int b = 0; b < YH_NBAS; b++) {
        int b0 = b + n, b1 = b + n + 1;
        b0 = b0 < 0 ? 0 : b0 >= YH_NBAS ? YH_NBAS - 1 : b0;
        b1 = b1 < 0 ? 0 : b1 >= YH_NBAS ? YH_NBAS - 1 : b1;
        o[b0] += w[b] * (1.0f - f);
        o[b1] += w[b] * f;
    }
}

int yh_init(yh_ctx *g, const yh_params *p, int W, int H, void *mem)
{
    const yh_stock *s = &yh_stocks[p->stock < 0 || p->stock >= YH_NSTOCK ? 0 : p->stock];
    memset(g, 0, sizeof *g);
    g->W = W; g->H = H;
    sizes(W, H, g->lw, g->lh);
    g->gw = g->lw[0]; g->gh = g->lh[0];
    g->ystep = p->ystep == 1 || p->ystep == 4 ? p->ystep : 2;
    g->yshift = 6 - (g->ystep == 1 ? 0 : g->ystep == 2 ? 1 : 2);
    g->xfade = p->xfade < 0 ? 0 : p->xfade > XSAVE_MAX - 2 ? XSAVE_MAX - 2 : p->xfade;
    g->xf_rcp = g->xfade ? 65536 / g->xfade : 0;
    /* memory */
    uint8_t *m = (uint8_t *)mem;
    for (int k = 0; k < YH_NLEV; k++) {
        uint32_t b = AL4(3u * g->lw[k] * g->lh[k] * 2);
        g->L[k] = (uint16_t *)m; m += b;
        if (k) { g->A[k] = (uint16_t *)m; m += b; }
    }
    g->H0 = (int16_t *)m; m += 8u * g->gw * g->gh;
    for (int k = 0; k < 3; k++) { g->C[k] = (uint16_t *)m; m += AL4(2u * g->lw[k] * g->lh[k]); }
    g->l0lo = (int16_t *)m; m += AL4(g->gh * 2); g->l0hi = (int16_t *)m; m += AL4(g->gh * 2);
    g->h0lo = (int16_t *)m; m += AL4(g->gh * 2); g->h0hi = (int16_t *)m; m += AL4(g->gh * 2);
    g->a1lo = (int16_t *)m; m += AL4(g->lh[1] * 2); g->a1hi = (int16_t *)m; m += AL4(g->lh[1] * 2);
    g->gacc = (uint32_t *)m; m += 4 * g->gw * 4;
    g->trow = (int32_t *)m; m += 3 * (4 * (g->gw + 4)) * 4;
    g->xsave = (uint16_t *)m; m += AL4(3 * g->lw[2] * XSAVE_MAX * 2);
    g->h0save = (int16_t *)m;

    /* user parameters, clamped so that no integer stage can overflow (camera review):
     *  tint_q8 outside [0, 256] makes M negative (unsigned wrap in combine_level / combine_h0);
     *  pboost_ev >= 8 overflows (gain - 256) * c0 in point_boost (the u16 L0 saturates above ~ +4 EV anyway);
     *  hal_q8 / bloom_q8 are limited to 400 % (the M * X sums stay far below 2^32). */
    float knee_f = p->knee_ev > -50 ? p->knee_ev : s->knee_ev;
    float boost_f = p->boost_ev > -50 ? p->boost_ev : s->boost_ev;
    float pboost_f = p->pboost_ev > -50 ? p->pboost_ev : s->pboost_ev;
    knee_f = knee_f < -3 ? -3 : knee_f > 6 ? 6 : knee_f;
    boost_f = boost_f < 0 ? 0 : boost_f > 6 ? 6 : boost_f;
    pboost_f = pboost_f < 0 ? 0 : pboost_f > 6 ? 6 : pboost_f;
    int hal_q8 = p->hal_q8 < 0 ? 0 : p->hal_q8 > 1024 ? 1024 : p->hal_q8;
    int bloom_q8 = p->bloom_q8 < 0 ? 0 : p->bloom_q8 > 1024 ? 1024 : p->bloom_q8;
    int tint_q8 = p->tint_q8 < 0 ? 0 : p->tint_q8 > 256 ? 256 : p->tint_q8;
    float bfrac = p->bloom_frac < 0 ? 0 : p->bloom_frac > 0.25f ? 0.25f : p->bloom_frac;
    g->pb_q8 = g->pb_base = pboost_f > 0 ? (int)(pboost_f * 256 + 0.5f) : 0;
    g->hb_q8 = g->hp_q8 = 256;
    if (p->gen) {                                    /* YH_GEN: no display-model tables, no matrices (see the end) */
        g->gen = 1;
        g->g_str_q8 = hal_q8;
        yh_gen_tables(g, p);
        yh_begin_frame(g);
        return 0;
    }
    float hwg[3][YH_NBAS], wbg[YH_NBAS];         /* the profile's and the bloom's base weights for the gauge */
    for (int c = 0; c < 3; c++)
        yh_gauge_w(s->hw[c], hwg[c], p->gauge_q8);
    yh_gauge_w(yh_w_bloom, wbg, p->gauge_q8);
#ifdef YH_BAKED
#if YH_T_RAMP0 != YH_RAMP0
#error "yh_tables.h was made for another YH_RAMP0: re-run cam/mk_tables"
#endif
    /* camera: display tables baked; drive and matrices in integer arithmetic (identical on host and camera) */
    memcpy(g->encA, yh_t_encA, sizeof g->encA); memcpy(g->encB, yh_t_encB, sizeof g->encB);
    memcpy(g->encC, yh_t_encC, sizeof g->encC); memcpy(g->lt, yh_t_lt, sizeof g->lt);
    memcpy(g->sl, yh_t_sl, sizeof g->sl); memcpy(g->hmt, yh_t_hmt, sizeof g->hmt); memcpy(g->rsl, yh_t_rsl, sizeof g->rsl);
    g->hmin = YH_T_HMIN;
    {
        g->knee_q16 = (int)(knee_f * 65536.0f + (knee_f < 0 ? -0.5f : 0.5f));
        g->boost_q16 = (int)(boost_f * 65536.0f + 0.5f);
        g->qk = (uint32_t)(yh_exp2_q30(YH_T_GREY_Q30, g->knee_q16) >> 10);
        g->kneev = 256;
        hot_tables(g, g->boost_q16, -256);
    }
#else
    /* display tables */
    double L255 = disp_to_lin(254.75);
    for (int i = 0; i < 1024; i++) g->encA[i] = (uint8_t)enc_val(((uint32_t)i << 2) + 2, L255);
    for (int i = 0; i < 1024; i++) g->encB[i] = (uint8_t)enc_val(((uint32_t)i << 6) + 32, L255);
    for (int i = 0; i < 2048; i++) g->encC[i] = (uint8_t)enc_val(((uint32_t)i << 9) + 256, L255);
    g->knee_q16 = (int)floor(knee_f * 65536.0 + 0.5);
    g->boost_q16 = (int)floor(boost_f * 65536.0 + 0.5);
    for (int v = -256; v < 512; v++) {
        int vc = v < 0 ? 0 : v > 255 ? 255 : v;
        uint32_t q = vc == 255 ? (1u << 20) : (uint32_t)floor(disp_to_lin(vc) / L255 * 1048576.0 + 0.5);
        g->lt[v + 256] = (q << 8) | (uint32_t)enc(g, q);
    }
    g->qk = (uint32_t)floor(0.18 * pow(2.0, g->knee_q16 / 65536.0) / L255 * 1048576.0 + 0.5);
    g->kneev = 256;
    hot_tables(g, g->boost_q16, -256);
    /* tangent table (levels per Q15 unit, Q16) and the largest halo it is accurate for (<= 0.75 level) */
    const double u15 = L255 / 32768.0;
    for (int v = 0; v < 256; v++) {
        double L = v < 255 ? disp_to_lin(v) : L255, e = 0.25 * u15;
        double sl = v < 255 ? (lin_to_disp(L + e) - lin_to_disp(L)) / e * u15 : 0.0;
        int q = (int)(sl * 65536.0 + 0.5);
        g->sl[v] = (uint16_t)(q > 65535 ? 65535 : q);
    }
    /* per luma: largest halo for which the tangent is within 0.75 level (binary search, 15 x 255 evaluations;
     * the display curve is fixed, so on the camera these tables can be constants) */
    g->hmin = 32767;
    for (int v = 0; v < 256; v++) {
        int lo = 0, hi = 32767;
        double L = v < 255 ? disp_to_lin(v) : L255, e0 = lin_to_disp(L);
        while (lo < hi) {
            int h = (lo + hi + 1) >> 1;
            double d = lin_to_disp(L + h * u15) - e0;
            if (fabs(g->sl[v] / 65536.0 * h - d) <= 0.75) lo = h; else hi = h - 1;
        }
        g->hmt[v] = (uint16_t)lo;
        if (lo < g->hmin) g->hmin = lo;
        g->rsl[v] = g->sl[v] ? (uint32_t)((1u << 24) / g->sl[v]) : (1u << 24);
    }
#endif
    g->hfast = g->hmin; g->pair_t = p->pair_t;
    if (p->exact_only == 1 || p->exact_only == 3) {          /* test modes: all exact, all pair */
        g->hfast = g->hmin = -1; for (int v = 0; v < 256; v++) g->hmt[v] = 0;
        g->pair_t = p->exact_only == 3 ? 255 : -1;
    }
    if (p->exact_only == 2) g->hfast = g->hmin = 32767;     /* test mode: all fast (cost only) */
    g->apply_mode = p->exact_only;

    for (int ub = 0; ub < 32; ub++) for (int vb = 0; vb < 32; vb++) {
        int mx = 0;
        for (int a = 0; a < 8; a++) for (int b = 0; b < 8; b++) {
            int U = (int8_t)(ub * 8 + a), V = (int8_t)(vb * 8 + b);
            int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
            if (rv > mx) mx = rv;
            if (-gu > mx) mx = -gu;
            if (bu > mx) mx = bu;
        }
        g->moff[ub | (vb << 5)] = (uint8_t)(mx > 255 ? 255 : mx);
    }
    /* exact skip of invisible cells (apply). A cell is left untouched when every pixel (luma >= T) takes the fast path
     * and all its deltas round to 0: with B >= max(hY, |hU|, |hV|) over the cell, dY = (s*hY + 2^15) >> 16 = 0 and
     * dU, dV = (ss*h + 2^16) >> 17 = 0 when sl[Y] * B < 32768 (ss <= 2 sl); sl does not increase with Y, so
     * hz[v] = 32767 / sl[v] (suffix min) bounds every pixel >= v. The fast path needs hMax <= hmt[min(Y0, Y1)]
     * (suffix min of hmt). Tables give the smallest T per 8-unit bucket (rounded up: conservative). */
    {
        /* module (wf_fx.c): yh_init runs in wf_task (4 KB stack), so the 2 KB of suffix minima live in
         * the build's temp rows (trow, 31 KB, unused until yh_build) instead of the stack */
        int hzs = 32767, hms = 32767, *hz = (int *)g->trow, *hm = (int *)g->trow + 256;
        for (int v = 255; v >= 0; v--) {
            int z = g->sl[v] ? 32767 / g->sl[v] : 32767;
            if (z < hzs) hzs = z;
            if (g->hmt[v] < hms) hms = g->hmt[v];
            hz[v] = hzs; hm[v] = hms;
        }
        for (int k = 0; k < 256; k++) {
            int vb = 0, vm = 0;
            while (vb < 255 && hz[vb] < 8 * k + 7) vb++;
            while (vm < 255 && hm[vm] < 8 * k + 7) vm++;
            g->vthrB[k] = (uint8_t)vb; g->vthrM[k] = (uint8_t)vm;     /* 255: never skipped (test modes clear hmt) */
        }
    }
#ifdef YH_BAKED
    {
        /* M_b[c][j] = hal * hw_b[c] * x[c][j] + (c == j) * bloom * frac * wb_b, Q16; weights converted to Q24 by an exact
         * float scaling, products in 64-bit integers */
        int t = tint_q8, bw = YH_STOCK_BW(p->stock);
        int32_t bf24 = (int32_t)(bfrac * 16777216.0f + 0.5f);
        for (int b = 0; b < YH_NBAS; b++) {
            int32_t wb24 = (int32_t)(wbg[b] * 16777216.0f + 0.5f), w0 = (int32_t)(hwg[0][b] * 16777216.0f + 0.5f);
            for (int c = 0; c < 3; c++) {
                int32_t wc = (int32_t)(hwg[c][b] * 16777216.0f + 0.5f);
                int64_t hw24 = ((int64_t)(256 - t) * wc + (int64_t)t * w0 + 128) >> 8;
                for (int j = 0; j < 3; j++) {
                    int32_t xc = (int32_t)(s->xt[c][j] * 65536.0f + 0.5f), xn = bw ? xc : (c == j ? 65536 : 0);
                    int64_t x16 = ((int64_t)(256 - t) * xc + (int64_t)t * xn + 128) >> 8;
                    int64_t v48 = (int64_t)hal_q8 * hw24 * x16;                          /* Q8 * Q24 * Q16 */
                    g->Mh0[b][c][j] = (int32_t)((v48 + (1ll << 31)) >> 32);              /* the halation part alone */
                    if (c == j) v48 += ((int64_t)bloom_q8 * bf24 * wb24) >> 8;           /* Q8 * Q24 * Q24 >> 8 */
                    g->M[b][c][j] = (int32_t)((v48 + (1ll << 31)) >> 32);
                }
            }
        }
    }
#else
    /* level matrices: M_b[c][j] = bloom_b * I + hal_b[c] * xt[c][j]  (Q16); tint 1 = neutral: every layer gets the
     * red PSF and xt -> I (the halo takes the colour of the source) */
    double hs = hal_q8 / 256.0, bs = bloom_q8 / 256.0 * bfrac, t = tint_q8 / 256.0;
    for (int b = 0; b < YH_NBAS; b++)
        for (int c = 0; c < 3; c++)
            for (int j = 0; j < 3; j++) {
                double hw = (1 - t) * hwg[c][b] + t * hwg[0][b];
                double x = (1 - t) * s->xt[c][j] + t * (YH_STOCK_BW(p->stock) ? s->xt[c][j] : (c == j));
                double v = hs * hw * x + (c == j ? bs * wbg[b] : 0.0);
                g->Mh0[b][c][j] = (int32_t)floor(hs * hw * x * 65536.0 + 0.5);
                g->M[b][c][j] = (int32_t)floor(v * 65536.0 + 0.5);
            }
#endif
    memcpy(g->M0, g->M, sizeof g->M);
    g->hs_q8 = 256;
    yh_begin_frame(g);
    return 0;
}

void yh_begin_frame(yh_ctx *g)
{
    memset(g->L[0], 0, 3u * g->gw * g->gh * 2);
    memset(g->C[0], 0, 2u * g->gw * g->gh);
    memset(g->gacc, 0, 4u * g->gw * 4);
    for (int i = 0; i < g->gh; i++) { g->l0lo[i] = (int16_t)g->gw; g->l0hi[i] = 0; g->h0lo[i] = (int16_t)g->gw; g->h0hi[i] = 0; }
    g->gacc_gy = -1; g->cur_y = -1; g->row_lo = g->row_hi = 0; g->nsave = 0; g->glo = 1 << 30; g->ghi = -1;
    g->g_keep = -1;
    g->st_gather_words = g->st_gather_hot = g->st_apply_fast = g->st_apply_exact = g->st_build_cells = 0;
    g->st_pboost_cells = g->st_apply_pair = g->st_cells_vis = 0;
}

/* ------------------------------------------------------------------ gather (down pass) */
static void flush(yh_ctx *g)
{
    int gy = g->gacc_gy, sh = g->yshift, lo = g->gw, hi = 0;
    uint32_t rnd = 1u << (sh - 1);
    uint16_t *d = g->L[0] + 3 * g->gw * gy, *dc = g->C[0] + g->gw * gy;
    memset(d, 0, 6u * g->gw); memset(dc, 0, 2u * g->gw);   /* row may hold the Diana mirror rows */
    if (g->ghi >= 0) {
        uint32_t *a = g->gacc;
        for (int i = g->glo; i <= g->ghi; i++) {
            uint32_t v[4];
            for (int k = 0; k < 4; k++) { v[k] = (a[4 * i + k] + rnd) >> sh; a[4 * i + k] = 0; if (v[k] > 65535) v[k] = 65535; }
            d[3 * i] = (uint16_t)v[0]; d[3 * i + 1] = (uint16_t)v[1]; d[3 * i + 2] = (uint16_t)v[2]; dc[i] = (uint16_t)v[3];
            if (v[0] | v[1] | v[2]) { if (i < lo) lo = i; hi = i + 1; }
        }
    }
    g->l0lo[gy] = (int16_t)lo; g->l0hi[gy] = (int16_t)hi;
    g->glo = 1 << 30; g->ghi = -1;
}

/* drive (Q10, as dl) of channel code v scaled by the ramp gain gq (Q8) of the pixel's brightest channel */
YH_INLINE uint32_t ramp_dl(const yh_ctx *g, int v, uint32_t gq)
{
    uint32_t q = (((g->lt[v + 256] >> 12) * gq) >> 4);         /* linear Q20 (1 << 20 = top code) x gain */
    uint32_t d = q > g->qk ? (q - g->qk + 512) >> 10 : 0;
    return d > 65535 ? 65535 : d;
}

/* A pixel whose brightest channel (code m = Y + mc) is in the clipped-energy ramp (>= YH_RAMP0) has its true
 * energy above the clip in all its channels: every channel gets that channel's ramp gain rgain[m] (module
 * change, camera review: the per-channel ramp boosted only the channels that reached 253..255, so a
 * near-white source such as (255,250,247) got a red-only glow ~4x too strong, a light-blue neon a blue-only
 * bloom). For the brightest channel this is exactly the old ramp; pixels below the ramp are unchanged. */
static __attribute__((noinline)) void gather_hot(const yh_ctx *g, uint32_t *a, uint32_t w, int Y0, int Y1, int ym)
{
    const uint16_t *dl = g->dl + 256;
    int U = (int8_t)w, V = (int8_t)(w >> 16);
    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
    int mc = rv > -gu ? rv : -gu; if (bu > mc) mc = bu;
    uint32_t r, gg, b;
    if (ym + mc < YH_RAMP0) {
        r = (uint32_t)dl[Y0 + rv] + dl[Y1 + rv];
        gg = (uint32_t)dl[Y0 - gu] + dl[Y1 - gu];
        b = (uint32_t)dl[Y0 + bu] + dl[Y1 + bu];
    } else {
        r = gg = b = 0;
        for (int k = 0; k < 2; k++) {
            int Y = k ? Y1 : Y0, m = Y + mc;
            if (m < YH_RAMP0) {
                r += dl[Y + rv]; gg += dl[Y - gu]; b += dl[Y + bu];
            } else {
                uint32_t gq = g->rgain[(m > 255 ? 255 : m) - YH_RAMP0];
                r += ramp_dl(g, Y + rv, gq); gg += ramp_dl(g, Y - gu, gq); b += ramp_dl(g, Y + bu, gq);
            }
        }
    }
    a[0] += r; a[1] += gg; a[2] += b;
    if (ym + mc >= YH_CLIPV) a[3] += r + gg + b;      /* word with a clipped channel */
}

void yh_gather_chunk(yh_ctx *g, const uint32_t *p, int npix, int x0, int y)
{
    if (g->gen) { yh_gen_gather(g, p, npix, x0, y); return; }
    if (y & (g->ystep - 1)) return;
    int gy = y >> 3;
    if (gy != g->gacc_gy) { if (g->gacc_gy >= 0) flush(g); g->gacc_gy = gy; }
    const uint8_t *mo = g->moff;
    const int kv = g->kneev;
    uint32_t *acc = g->gacc + 4 * (x0 >> 3);          /* x0 % 8 == 0 */
    int nw = npix >> 1, nhot = 0, clo = 1 << 30, chi = -1;
    for (int k = 0; k < nw; k++) {
        uint32_t w = p[k];
        int Y0 = (w >> 8) & 255, Y1 = (int)(w >> 24);
        int ym = Y0 > Y1 ? Y0 : Y1;
        /* cold test only (most words); the hot part is out of line so the compiler keeps this loop tight */
        if (__builtin_expect(ym + mo[((w >> 3) & 31) | ((w >> 14) & 0x3e0)] < kv, 1)) continue;
        gather_hot(g, acc + 4 * (k >> 2), w, Y0, Y1, ym);
        if (k < clo) clo = k;
        chi = k; nhot++;
    }
    if (chi >= 0) {
        int c0 = (x0 >> 3) + (clo >> 2), c1 = (x0 >> 3) + (chi >> 2);
        if (c0 < g->glo) g->glo = c0;
        if (c1 > g->ghi) g->ghi = c1;
    }
#ifdef YH_STATS
    g->st_gather_words += nw; g->st_gather_hot += nhot;
#else
    (void)nhot;
#endif
}

/* ------------------------------------------------------------------ pyramid */
/* tent [1 3 3 1]/8 decimation, nc-channel u16 -> u16, zero outside; optional input row spans */
static void tent_down(yh_ctx *g, const uint16_t *src, int fw, int fh, uint16_t *dst, int cw, int ch, int nc,
                      const int16_t *slo, const int16_t *shi)
{
    int32_t *v = g->trow;                               /* nc*(fw+4), index (x+2) */
    for (int i = 0; i < ch; i++) {
        uint16_t *d = dst + nc * cw * i;
        int r0 = 2 * i - 1, lo = fw, hi = 0;
        for (int t = 0; t < 4; t++) {
            int r = r0 + t; if (r < 0 || r >= fh) continue;
            int a = slo ? slo[r] : 0, b = shi ? shi[r] : fw;
            if (a < lo) lo = a;
            if (b > hi) hi = b;
        }
        memset(d, 0, 2u * nc * cw);
        if (lo >= hi) continue;
        memset(v, 0, 4u * nc * (fw + 4));
        for (int t = 0; t < 4; t++) {
            int r = r0 + t; if (r < 0 || r >= fh) continue;
            int wgt = (t == 0 || t == 3) ? 1 : 3;
            const uint16_t *s = src + nc * fw * r;
            for (int x = nc * lo; x < nc * hi; x++) v[x + 2 * nc] += wgt * s[x];
        }
        int ca = (lo - 2) >> 1, cb = (hi >> 1) + 1;      /* outputs touched by inputs [lo,hi) */
        if (ca < 0) ca = 0;
        if (cb > cw) cb = cw;
        for (int c = ca; c < cb; c++) {
            const int32_t *q = v + nc * (2 * c + 1);    /* inputs 2c-1 .. 2c+2 */
            for (int k = 0; k < nc; k++) {
                uint32_t o = (uint32_t)(q[k] + 3 * q[nc + k] + 3 * q[2 * nc + k] + q[3 * nc + k] + 32) >> 6;
                d[nc * c + k] = (uint16_t)(o > 65535 ? 65535 : o);
            }
        }
    }
}

/* point boost: clipped blobs much smaller than ~64 px (isolated) get +pboost EV (GUESS: such blobs are light
 * sources / glints whose true energy is far above the clip; large clipped areas get only +boost). */
static const uint16_t exp2_16[16] = { 256, 267, 279, 291, 304, 317, 331, 346, 362, 378, 395, 412, 431, 450, 470, 491 };
YH_INLINE int log2_q8(uint32_t x)                         /* x > 0; mantissa linear (error < 0.09) */
{
    int e = 31 - __builtin_clz(x);
    uint32_t mm = e >= 8 ? (x >> (e - 8)) & 255 : (x << (8 - e)) & 255;
    return (e << 8) + (int)mm;
}
static void point_boost(yh_ctx *g, int r_begin, int r_end)   /* L0 rows [r_begin, r_end) not boosted yet */
{
    if (g->pb_q8 <= 0) return;
    int gw = g->gw, gh = g->gh, w2 = g->lw[2], h2 = g->lh[2];
    tent_down(g, g->C[0], gw, gh, g->C[1], g->lw[1], g->lh[1], 1, g->l0lo, g->l0hi);
    tent_down(g, g->C[1], g->lw[1], g->lh[1], g->C[2], w2, h2, 1, 0, 0);
    const uint16_t *C2 = g->C[2];
    if (r_end > gh) r_end = gh;
    for (int r = r_begin; r < r_end; r++) {
        int uy = 2 * r - 3, j0 = uy >> 3, fy = uy & 7, j1 = j0 + 1;
        if (j0 < 0) j0 = 0;
        if (j1 >= h2) j1 = h2 - 1;
        if (j0 >= h2) j0 = h2 - 1;
        for (int c = g->l0lo[r]; c < g->l0hi[r]; c++) {
            uint32_t c0 = g->C[0][gw * r + c];
            if (!c0) continue;
            int ux = 2 * c - 3, i0 = ux >> 3, fx = ux & 7, i1 = i0 + 1;
            if (i0 < 0) i0 = 0;
            if (i1 >= w2) i1 = w2 - 1;
            if (i0 >= w2) i0 = w2 - 1;
            uint32_t c2 = (C2[w2 * j0 + i0] * (8 - fx) + C2[w2 * j0 + i1] * fx) * (8 - fy) +
                          (C2[w2 * j1 + i0] * (8 - fx) + C2[w2 * j1 + i1] * fx) * fy;          /* x64 */
            int dq8 = c2 ? log2_q8(c0 << 6) - log2_q8(c2) : 8 << 8;
            int t = (dq8 - 128) >> 1;                   /* isolation 0..1 for log2 ratio 0.5..2.5 */
            if (t <= 0) continue;
            if (t > 256) t = 256;
            int ge = (g->pb_q8 * t) >> 8;
            uint32_t gain = (uint32_t)exp2_16[(ge >> 4) & 15] << (ge >> 8);     /* Q8 */
            uint16_t *d = g->L[0] + 3 * (gw * r + c);
            uint32_t sum = (uint32_t)d[0] + d[1] + d[2];
            if (!sum) continue;
            uint32_t rq = ((gain - 256) * c0) / sum;    /* build-time divide, isolated clipped cells only */
            for (int k = 0; k < 3; k++) { uint32_t v = d[k] + ((rq * d[k]) >> 8); d[k] = (uint16_t)(v > 65535 ? 65535 : v); }
#ifdef YH_STATS
            g->st_pboost_cells++;
#endif
        }
    }
}

/* one fine row of up2(coarse) into int32 u[] (x4 scale vertically) */
static void up2_vrow(const uint16_t *A, int cw, int chh, int r, int32_t *u)
{
    int j = r >> 1, jn = (r & 1) ? j + 1 : j - 1;
    if (j >= chh) j = chh - 1;
    if (jn < 0) jn = 0;
    if (jn >= chh) jn = chh - 1;
    const uint16_t *a = A + 3 * cw * j, *b = A + 3 * cw * jn;
    for (int x = 0; x < 3 * cw; x++) u[x] = 3 * a[x] + b[x];
}
YH_INLINE uint32_t up2_h(const int32_t *u, int cw, int c, int k)
{
    int cc = c >> 1, cn = (c & 1) ? cc + 1 : cc - 1;
    if (cn < 0) cn = 0;
    if (cn >= cw) cn = cw - 1;
    return (uint32_t)(3 * u[3 * cc + k] + u[3 * cn + k] + 8) >> 4;
}

/* A_k = M_k * blur121(L_k) + up2(A_{k+1}), full rows (levels >= 1); drive Q10 -> halo Q16 */
static void combine_level(yh_ctx *g, int k)
{
    int w = g->lw[k], h = g->lh[k];
    const uint16_t *L = g->L[k];
    uint16_t *A = g->A[k];
    int32_t *v = g->trow;                               /* 3*(w+2) */
    int32_t *u = g->trow + 4 * (g->gw + 4);             /* 3*cw */
    const int32_t (*M)[3] = g->M[k + 1];                /* basis index: L0s=0, L0b=1, Lk=k+1 */
    int up = k + 1 < YH_NLEV;
    int cw = up ? g->lw[k + 1] : 0, chh = up ? g->lh[k + 1] : 0;
    for (int r = 0; r < h; r++) {
        memset(v, 0, 3u * (w + 2) * 4);
        for (int t = -1; t <= 1; t++) {
            int rr = r + t; if (rr < 0 || rr >= h) continue;
            int wt = t ? 1 : 2;
            const uint16_t *s = L + 3 * w * rr;
            for (int x = 0; x < 3 * w; x++) v[x + 3] += wt * s[x];
        }
        if (up) up2_vrow(g->A[k + 1], cw, chh, r, u);
        uint16_t *d = A + 3 * w * r;
        int lo = w, hi = 0;
        for (int c = 0; c < w; c++) {
            const int32_t *q = v + 3 * c;               /* q[0..2] = c-1, q[3..5] = c, q[6..8] = c+1 */
            uint32_t X0 = (uint32_t)(q[0] + 2 * q[3] + q[6] + 8) >> 4;
            uint32_t X1 = (uint32_t)(q[1] + 2 * q[4] + q[7] + 8) >> 4;
            uint32_t X2 = (uint32_t)(q[2] + 2 * q[5] + q[8] + 8) >> 4;
            uint32_t o[3];
            for (int ch = 0; ch < 3; ch++) {
                uint32_t a = (X0 | X1 | X2) ? (M[ch][0] * X0 + M[ch][1] * X1 + M[ch][2] * X2 + 512) >> 10 : 0;
                if (up) a += up2_h(u, cw, c, ch);
                o[ch] = a > 65535 ? 65535 : a;
            }
            d[3 * c] = (uint16_t)o[0]; d[3 * c + 1] = (uint16_t)o[1]; d[3 * c + 2] = (uint16_t)o[2];
            if (o[0] | o[1] | o[2]) { if (c < lo) lo = c; hi = c + 1; }
        }
        if (k == 1) { g->a1lo[r] = (int16_t)lo; g->a1hi[r] = (int16_t)hi; }
    }
}

YH_INLINE int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* H0 = M0s * L0 + M0b * blur121(L0) + up2(A1) -> (hY, hU, hV, hMax) Q15, only where it can be non-zero */
static void combine_h0(yh_ctx *g, int r_begin, int r_end)     /* H0 rows [r_begin, r_end) */
{
    int w = g->gw, h = g->gh, cw = g->lw[1], chh = g->lh[1];
    const uint16_t *L = g->L[0];
    int32_t *v = g->trow, *u = g->trow + 4 * (g->gw + 4);
    const int32_t (*Ms)[3] = g->M[0], (*Mb)[3] = g->M[1];
    for (int r = r_begin; r < r_end; r++) {
        int lo = w, hi = 0;
        for (int t = -1; t <= 1; t++) {
            int rr = r + t; if (rr < 0 || rr >= h) continue;
            if (g->l0lo[rr] < lo) lo = g->l0lo[rr];
            if (g->l0hi[rr] > hi) hi = g->l0hi[rr];
        }
        int llo = lo - 1 < 0 ? 0 : lo - 1, lhi = hi + 1 > w ? w : hi + 1;   /* L0 part */
        if (llo >= lhi) { llo = w; lhi = 0; }
        int j = r >> 1, jn = (r & 1) ? j + 1 : j - 1;
        if (j >= chh) j = chh - 1;
        if (jn < 0) jn = 0;
        if (jn >= chh) jn = chh - 1;
        int alo = g->a1lo[j] < g->a1lo[jn] ? g->a1lo[j] : g->a1lo[jn];
        int ahi = g->a1hi[j] > g->a1hi[jn] ? g->a1hi[j] : g->a1hi[jn];
        int ulo = 2 * alo - 1, uhi = 2 * ahi + 1;                         /* up2 part */
        if (alo >= ahi) { ulo = w; uhi = 0; }
        if (ulo < 0) ulo = 0;
        if (uhi > w) uhi = w;
        int clo = llo < ulo ? llo : ulo, chi = lhi > uhi ? lhi : uhi;
        int16_t *d = g->H0 + 4 * w * r;
        g->h0lo[r] = (int16_t)w; g->h0hi[r] = 0;
        if (clo >= chi) continue;
        if (llo < lhi) {
            memset(v + 3 * llo, 0, 3u * (lhi - llo + 2) * 4);
            for (int t = -1; t <= 1; t++) {
                int rr = r + t; if (rr < 0 || rr >= h) continue;
                int wt = t ? 1 : 2;
                const uint16_t *s = L + 3 * w * rr;
                int a = llo - 1 < 0 ? 0 : llo - 1, b = lhi + 1 > w ? w : lhi + 1;
                for (int x = 3 * a; x < 3 * b; x++) v[x + 3] += wt * s[x];
            }
        }
        if (ulo < uhi) up2_vrow(g->A[1], cw, chh, r, u);
        const uint16_t *s0 = L + 3 * w * r;
        int olo = w, ohi = 0;
        for (int c = clo; c < chi; c++) {
            uint32_t o[3] = { 0, 0, 0 };
            if (c >= llo && c < lhi) {
                const int32_t *q = v + 3 * c;
                uint32_t X0 = (uint32_t)(q[0] + 2 * q[3] + q[6] + 8) >> 4;
                uint32_t X1 = (uint32_t)(q[1] + 2 * q[4] + q[7] + 8) >> 4;
                uint32_t X2 = (uint32_t)(q[2] + 2 * q[5] + q[8] + 8) >> 4;
                uint32_t S0 = s0[3 * c], S1 = s0[3 * c + 1], S2 = s0[3 * c + 2];
                if (X0 | X1 | X2 | S0 | S1 | S2)
                    for (int ch = 0; ch < 3; ch++)
                        o[ch] = (Mb[ch][0] * X0 + Mb[ch][1] * X1 + Mb[ch][2] * X2 +
                                 Ms[ch][0] * S0 + Ms[ch][1] * S1 + Ms[ch][2] * S2 + 512) >> 10;
            }
            if (c >= ulo && c < uhi)
                for (int ch = 0; ch < 3; ch++) o[ch] += up2_h(u, cw, c, ch);
            int32_t R = o[0] > 131071 ? 131071 : (int32_t)o[0], G = o[1] > 131071 ? 131071 : (int32_t)o[1];
            int32_t B = o[2] > 131071 ? 131071 : (int32_t)o[2];
            int32_t mx = R > G ? R : G; if (B > mx) mx = B;
            int16_t *e = d + 4 * c;
            if (mx < 2) { e[0] = e[1] = e[2] = e[3] = 0; continue; }          /* < 1 Q15 unit */
            e[0] = sat16((77 * R + 150 * G + 29 * B + 256) >> 9);
            e[1] = sat16((-43 * R - 85 * G + 128 * B + 256) >> 9);
            e[2] = sat16((128 * R - 107 * G - 21 * B + 256) >> 9);
            e[3] = sat16(mx >> 1);
            if (c < olo) olo = c;
            ohi = c + 1;
#ifdef YH_STATS
            g->st_build_cells++;
#endif
        }
        g->h0lo[r] = (int16_t)olo; g->h0hi[r] = (int16_t)ohi;
    }
}

/* Diana top build. GUESS: the bottom half is not exposed yet. Mirror the last YH_MIRROR L0 rows (32 px)
 * above the seam into the rows below it with falling weight, so bright areas that cross the seam (sky,
 * windows) keep their halation core on the top side. flush() clears these rows before the bottom gather.
 * Module change (camera review): only light that reaches the seam as a wide area is mirrored. Every
 * mirrored cell is limited to E = the minimum of L0 row s0-1 (the row at the seam) over +-YH_MIRROR_ERODE
 * cells, per channel. A sky or window crossing the seam keeps its mirror (E = its own level); a small
 * source above the seam, also one touching it, gets none. Before, its mirror image put a phantom glow
 * below the seam, which the bottom build took over (H0 hand-off, A2 crossfade): +39..+135 levels in the
 * bottom half 4..28 px under a 12 px source. */
static void seam_mirror(yh_ctx *g, int s0)
{
    int gw = g->gw;
    if (s0 < 1 || s0 >= g->gh)
        return;
    uint16_t *E = (uint16_t *)g->trow;                   /* 4 * gw u16 (R, G, B, clipped); trow is free here */
    const uint16_t *el = g->L[0] + 3 * gw * (s0 - 1), *ec = g->C[0] + gw * (s0 - 1);
    for (int x = 0; x < gw; x++) {
        int a = x - YH_MIRROR_ERODE < 0 ? 0 : x - YH_MIRROR_ERODE, b = x + YH_MIRROR_ERODE >= gw ? gw - 1 : x + YH_MIRROR_ERODE;
        uint32_t m0 = 65535, m1 = 65535, m2 = 65535, mcl = 65535;
        for (int j = a; j <= b; j++) {
            if (el[3 * j] < m0) m0 = el[3 * j];
            if (el[3 * j + 1] < m1) m1 = el[3 * j + 1];
            if (el[3 * j + 2] < m2) m2 = el[3 * j + 2];
            if (ec[j] < mcl) mcl = ec[j];
        }
        E[4 * x] = (uint16_t)m0; E[4 * x + 1] = (uint16_t)m1; E[4 * x + 2] = (uint16_t)m2; E[4 * x + 3] = (uint16_t)mcl;
    }
    for (int t = 0; t < YH_MIRROR && s0 + t < g->gh && s0 - 1 - t >= 0; t++) {
        int wq = 256 * (YH_MIRROR - t) / YH_MIRROR, lo = gw, hi = 0;
        const uint16_t *sl0 = g->L[0] + 3 * gw * (s0 - 1 - t), *sc = g->C[0] + gw * (s0 - 1 - t);
        uint16_t *dl0 = g->L[0] + 3 * gw * (s0 + t), *dc = g->C[0] + gw * (s0 + t);
        for (int x = 0; x < gw; x++) {
            uint32_t v[4];
            for (int k = 0; k < 3; k++) {
                uint32_t s = sl0[3 * x + k], e = E[4 * x + k];
                v[k] = ((s < e ? s : e) * wq) >> 8;
                dl0[3 * x + k] = (uint16_t)v[k];
            }
            uint32_t s = sc[x], e = E[4 * x + 3];
            dc[x] = (uint16_t)(((s < e ? s : e) * wq) >> 8);
            if (v[0] | v[1] | v[2]) { if (x < lo) lo = x; hi = x + 1; }
        }
        g->l0lo[s0 + t] = (int16_t)lo; g->l0hi[s0 + t] = (int16_t)hi;
    }
}

void yh_build(yh_ctx *g, int y_begin, int y_end, int mode)
{
    if (g->gen) { yh_gen_build(g, y_begin, y_end, mode); return; }
    if (g->gacc_gy >= 0) { flush(g); g->gacc_gy = -1; }
    int s0 = (mode == YH_TOP ? y_end : y_begin) >> 3;    /* Diana: seam row on the L0 grid */
    if (mode == YH_TOP) {
        seam_mirror(g, s0);
        point_boost(g, 0, s0 + YH_MIRROR);
    } else
        point_boost(g, mode == YH_BOTTOM ? s0 : 0, g->gh);   /* rows above the seam were boosted in the top build */

    tent_down(g, g->L[0], g->lw[0], g->lh[0], g->L[1], g->lw[1], g->lh[1], 3, g->l0lo, g->l0hi);
    for (int k = 1; k + 1 < YH_NLEV; k++)
        tent_down(g, g->L[k], g->lw[k], g->lh[k], g->L[k + 1], g->lw[k + 1], g->lh[k + 1], 3, 0, 0);
    for (int k = YH_NLEV - 1; k >= 1; k--) {
        combine_level(g, k);
        if (k == 2 && mode != YH_WHOLE && g->xfade > 0) {
            /* Diana: rows [seam2-2, seam2+xfade) of A2; seam2 = seam row / 32 */
            int w = g->lw[2], h = g->lh[2], s2 = (mode == YH_TOP ? y_end : y_begin) >> 5;
            int r0 = s2 - 2, r1 = s2 + g->xfade;
            if (r0 < 0) r0 = 0;
            if (r1 > h) r1 = h;
            uint16_t *A2 = g->A[2];
            if (mode == YH_TOP) {
                memcpy(g->xsave, A2 + 3 * w * r0, 3u * w * (r1 - r0) * 2);
                g->seam2 = s2; g->nsave = r1 - r0;
            } else if (g->nsave == r1 - r0 && g->seam2 == s2) {
                for (int r = r0; r < r1; r++) {
                    int tq = r <= s2 ? 0 : (((r - s2) * g->xf_rcp) >> 8);
                    if (tq > 256) tq = 256;
                    uint16_t *d = A2 + 3 * w * r; const uint16_t *sv = g->xsave + 3 * w * (r - r0);
                    for (int x = 0; x < 3 * w; x++) d[x] = (uint16_t)((sv[x] * (256 - tq) + d[x] * tq + 128) >> 8);
                }
            }
        }
    }
    /* H0 rows the following apply reads: top half rows <= s0, bottom half rows >= s0 - 1 */
    int h0a = mode == YH_BOTTOM ? s0 - 1 : 0, h0b = mode == YH_TOP ? s0 + YH_H0X : g->gh;
    if (h0a < 0) h0a = 0;
    if (h0b > g->gh) h0b = g->gh;
    memset(g->H0 + 4 * g->gw * h0a, 0, 8u * g->gw * (h0b - h0a));   /* prep_row relies on zeros outside written cells */
    combine_h0(g, h0a, h0b);
    if (mode != YH_WHOLE && s0 >= 1 && s0 + YH_H0X <= g->gh) {
        /* Diana: the final halo field is continuous across the seam. The top build keeps its H0 rows s0-1 ..
         * s0+YH_H0X-1; the bottom build uses row s0-1 (centre 4.5 px above the seam, the row the top half's
         * last rows were applied with) as is and blends to its own over the YH_H0X rows below (32 px).
         * Module change (camera review): in those rows a cell keeps the bottom build's own value where it is
         * larger (hMax) than the top build's. The top build never saw the bottom half, so a source just below
         * the seam had its glow inside the bottom half cut along a straight line 8-16 px under the seam
         * (R 158 -> 4 one row in); now only the top half lacks its upward glow (a step at the seam, which the
         * top half cannot avoid). Weights 256 (s0-1), 204, 153, 102, 51 (was 256, 256, 192, 128, 64). */
        int gw = g->gw, n = YH_H0X + 1;
        int16_t *sv = g->h0save, *sp = g->h0save + 4 * gw * n;
        if (mode == YH_TOP) {
            memcpy(sv, g->H0 + 4 * gw * (s0 - 1), 8u * gw * n);
            for (int k = 0; k < n; k++) { sp[2 * k] = g->h0lo[s0 - 1 + k]; sp[2 * k + 1] = g->h0hi[s0 - 1 + k]; }
        } else {
            for (int k = 0; k < n; k++) {
                int r = s0 - 1 + k, wt = 256 * (n - k) / n;
                int lo = sp[2 * k] < g->h0lo[r] ? sp[2 * k] : g->h0lo[r];
                int hi = sp[2 * k + 1] > g->h0hi[r] ? sp[2 * k + 1] : g->h0hi[r];
                int16_t *d = g->H0 + 4 * gw * r; const int16_t *t = sv + 4 * gw * k;
                for (int c = lo; c < hi; c++) {
                    if (k && d[4 * c + 3] > t[4 * c + 3])
                        continue;                        /* below the seam, the bottom build's own halo is larger */
                    for (int q = 0; q < 3; q++) d[4 * c + q] = (int16_t)((t[4 * c + q] * wt + d[4 * c + q] * (256 - wt) + 128) >> 8);
                    if (t[4 * c + 3] > d[4 * c + 3]) d[4 * c + 3] = t[4 * c + 3];
                }
                if (lo < hi) { g->h0lo[r] = (int16_t)lo; g->h0hi[r] = (int16_t)hi; }
            }
        }
    }
    g->cur_y = -1; g->row_lo = g->row_hi = 0;
}

/* ------------------------------------------------------------------ apply */
/* per pixel row: the two H0 rows and the vertical weight; columns are interpolated inside the chunk loop.
 * H0 is zero outside the cells combine_h0 wrote (yh_build clears it), so no span checks per column. */
static void prep_row(yh_ctx *g, int y)
{
    int t = 2 * y - 7, j0 = t >> 4, j1 = j0 + 1;
    g->cur_y = y;
    if (j0 < 0) j0 = 0;
    if (j1 < 0) j1 = 0;
    if (j0 >= g->gh) j0 = g->gh - 1;
    if (j1 >= g->gh) j1 = g->gh - 1;
    int lo = g->h0lo[j0] < g->h0lo[j1] ? g->h0lo[j0] : g->h0lo[j1];
    int hi = g->h0hi[j0] > g->h0hi[j1] ? g->h0hi[j0] : g->h0hi[j1];
    if (lo >= hi) { g->row_lo = g->row_hi = 0; return; }
    g->row_a = g->H0 + 4 * g->gw * j0; g->row_b = g->H0 + 4 * g->gw * j1; g->row_fy = t & 15;
    g->row_lo = lo - 1; g->row_hi = hi;                  /* cells i (between columns i, i+1) that can be non-zero */
}

YH_INLINE uint32_t pack_word(int U, int Y0, int V, int Y1)
{
    return (uint32_t)(U & 255) | ((uint32_t)Y0 << 8) | ((uint32_t)(V & 255) << 16) | ((uint32_t)Y1 << 24);
}
#define CLAMP_S8(x) do { if ((unsigned)((x) + 128) > 255u) (x) = (x) < 0 ? -128 : 127; } while (0)
#define CLAMP_U8(x) do { if ((unsigned)(x) > 255u) (x) = (x) < 0 ? 0 : 255; } while (0)

/* fast path: the halo is small next to the pixel, so use the tangent of the display curve at each pixel's luma:
 * one table read and one multiply per value. Within 0.75 level of the exact path (hmt table). */
YH_INLINE void fast_word(const uint16_t *sl, uint32_t *pw, int hY, int hU, int hV)
{
    uint32_t w = *pw;
    int Y0 = (w >> 8) & 255, Y1 = (int)(w >> 24);
    int s0 = sl[Y0], s1 = sl[Y1], ss = s0 + s1;
    int dY0 = (s0 * hY + 32768) >> 16, dY1 = (s1 * hY + 32768) >> 16;
    int du = (ss * hU + 65536) >> 17, dv = (ss * hV + 65536) >> 17;
    if (!(dY0 | dY1 | du | dv)) return;
    int U = (int8_t)w + du, V = (int8_t)(w >> 16) + dv;
    Y0 += dY0; if (Y0 > 255) Y0 = 255;
    Y1 += dY1; if (Y1 > 255) Y1 = 255;
    CLAMP_S8(U); CLAMP_S8(V);
    *pw = pack_word(U, Y0, V, Y1);
}

/* display delta of one channel: encode(linear(code) + halo) - code; lt = (linear Q20 << 8) | code */
YH_INLINE int ch_delta(const yh_ctx *g, uint32_t e, uint32_t h)
{
    return enc(g, (e >> 8) + h) - (int)(e & 255);
}

/* pair path: per-channel exact add at the word's mean luma (3 encodes), luma of each pixel moved by the
 * local gain of the curve (slope after / slope before); for words whose two pixels are alike */
YH_INLINE void pair_word(const yh_ctx *g, uint32_t *pw, uint32_t hr, uint32_t hg, uint32_t hb)
{
    const uint32_t *lt = g->lt + 256;
    uint32_t w = *pw;
    int U = (int8_t)w, Y0 = (w >> 8) & 255, V = (int8_t)(w >> 16), Y1 = (int)(w >> 24);
    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
    int Ym = (Y0 + Y1 + 1) >> 1;
    int dR = ch_delta(g, lt[Ym + rv], hr), dG = ch_delta(g, lt[Ym - gu], hg), dB = ch_delta(g, lt[Ym + bu], hb);
    if (!(dR | dG | dB)) return;
    int dY = (77 * dR + 150 * dG + 29 * dB + 128) >> 8;
    int Yo = Ym + dY; if (Yo > 255) Yo = 255;
    int k = (int)(((uint64_t)g->sl[Yo] * g->rsl[Ym]) >> 16) - 256;      /* gain - 1, Q8, <= 0 */
    if (k > 0) k = 0;
    Y0 += dY + ((k * (Y0 - Ym)) >> 8); CLAMP_U8(Y0);
    Y1 += dY + ((k * (Y1 - Ym)) >> 8); CLAMP_U8(Y1);
    U += (-43 * dR - 85 * dG + 128 * dB + 128) >> 8; CLAMP_S8(U);
    V += (128 * dR - 107 * dG - 21 * dB + 128) >> 8; CLAMP_S8(V);
    *pw = pack_word(U, Y0, V, Y1);
}

/* exact path: per pixel, per channel: linear + halo -> encode (6 encodes); YUV delta added to the word */
YH_INLINE void exact_word(const yh_ctx *g, uint32_t *pw, uint32_t hr, uint32_t hg, uint32_t hb)
{
    const uint32_t *lt = g->lt + 256;
    uint32_t w = *pw;
    int U = (int8_t)w, Y0 = (w >> 8) & 255, V = (int8_t)(w >> 16), Y1 = (int)(w >> 24);
    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
    int dR0 = ch_delta(g, lt[Y0 + rv], hr), dG0 = ch_delta(g, lt[Y0 - gu], hg), dB0 = ch_delta(g, lt[Y0 + bu], hb);
    int dR1 = ch_delta(g, lt[Y1 + rv], hr), dG1 = ch_delta(g, lt[Y1 - gu], hg), dB1 = ch_delta(g, lt[Y1 + bu], hb);
    int sR = dR0 + dR1, sG = dG0 + dG1, sB = dB0 + dB1;
    if (!(sR | sG | sB)) return;
    Y0 += (77 * dR0 + 150 * dG0 + 29 * dB0 + 128) >> 8; if (Y0 > 255) Y0 = 255;
    Y1 += (77 * dR1 + 150 * dG1 + 29 * dB1 + 128) >> 8; if (Y1 > 255) Y1 = 255;
    U += (-43 * sR - 85 * sG + 128 * sB + 256) >> 9; CLAMP_S8(U);
    V += (128 * sR - 107 * sG - 21 * sB + 256) >> 9; CLAMP_S8(V);
    *pw = pack_word(U, Y0, V, Y1);
}

/* the cell's mixed loop: per word, tangent if the halo is small next to the darker pixel, else pair / exact */
static __attribute__((noinline)) void cell_mixed(const yh_ctx *g, uint32_t *pw, uint32_t *pe, int cM,
                                                 int qY, int qU, int qV, int sY, int sU, int sV)
{
    const uint16_t *sl = g->sl, *hmt = g->hmt;
    const int pt = g->pair_t;
    for (; pw < pe; pw++, qY += sY, qU += sU, qV += sV) {
        uint32_t w = *pw;
        int Y0 = (w >> 8) & 255, Y1 = (int)(w >> 24);
        int ym = Y0 < Y1 ? Y0 : Y1;
        int hY = qY >> 3, hU = qU >> 3, hV = qV >> 3;
        if (cM <= hmt[ym]) {
#ifdef YH_STATS
            ((yh_ctx *)g)->st_apply_fast++;
#endif
            fast_word(sl, pw, hY, hU, hV);
            continue;
        }
        int hR = hY + ((359 * hV) >> 8), hG = hY - ((88 * hU + 183 * hV) >> 8), hB = hY + ((454 * hU) >> 8);
        uint32_t r = hR > 0 ? (uint32_t)hR << 5 : 0, gg = hG > 0 ? (uint32_t)hG << 5 : 0;
        uint32_t b = hB > 0 ? (uint32_t)hB << 5 : 0;
        int dd = Y0 - Y1; if (dd < 0) dd = -dd;
        if (dd <= pt) {
#ifdef YH_STATS
            ((yh_ctx *)g)->st_apply_pair++;
#endif
            pair_word(g, pw, r, gg, b);
        } else {
#ifdef YH_STATS
            ((yh_ctx *)g)->st_apply_exact++;
#endif
            exact_word(g, pw, r, gg, b);
        }
    }
}

/* test mode 4: the part of a word update that a merged look pass already does (load, unpack, clamp, pack, store) */
static __attribute__((noinline)) void cell_null(uint32_t *pw, uint32_t *pe, int qY, int sY)
{
    for (; pw < pe; pw++, qY += sY) {
        uint32_t w = *pw;
        int Y0 = ((w >> 8) & 255) + (qY >> 12), Y1 = (int)(w >> 24) + (qY >> 13);
        int U = (int8_t)w + (qY >> 14), V = (int8_t)(w >> 16) + (qY >> 15);
        if (Y0 > 255) Y0 = 255;
        if (Y1 > 255) Y1 = 255;
        CLAMP_S8(U); CLAMP_S8(V);
        *pw = pack_word(U, Y0, V, Y1);
    }
}

void yh_apply_chunk(yh_ctx *g, uint32_t *p, int npix, int x0, int y)
{
    if (y != g->cur_y) prep_row(g, y);
    if (g->row_lo >= g->row_hi) return;
    if (g->gen) { yh_gen_apply(g, p, npix, x0, y); return; }
    int xa = x0, xb = x0 + npix;
    int ia = (xa - 3) >> 3, ib = (xb - 5) >> 3;          /* cells of the first / last word */
    if (ia < g->row_lo) ia = g->row_lo;
    if (ib > g->row_hi - 1) ib = g->row_hi - 1;
    if (ia > ib) return;
    const int16_t *ra = g->row_a, *rb = g->row_b;
    const int fy = g->row_fy, wa = 16 - fy, gw = g->gw, hmin = g->hmin;
    const uint16_t *sl = g->sl;
    /* column j of the vertically interpolated halo row (edges replicate) */
#define YH_COL(j, Y, U, V, M) do {                                                        \
        int j_ = (j); if ((unsigned)j_ >= (unsigned)gw) j_ = j_ < 0 ? 0 : gw - 1;           \
        const int16_t *pa_ = ra + 4 * j_, *pb_ = rb + 4 * j_;                               \
        int ma_ = pa_[3], mb_ = pb_[3];                                                     \
        if (ma_ | mb_) {                                                                    \
            M = ma_ > mb_ ? ma_ : mb_;                                                      \
            Y = (pa_[0] * wa + pb_[0] * fy + 8) >> 4;                                       \
            U = (pa_[1] * wa + pb_[1] * fy + 8) >> 4;                                       \
            V = (pa_[2] * wa + pb_[2] * fy + 8) >> 4;                                       \
        } else M = Y = U = V = 0;                                                           \
    } while (0)
    /* bound of |hY|, |hU|, |hV| of a column (the words of a cell interpolate between its two columns) */
#define YH_BND(Y, U, V) ({ int au_ = (U) < 0 ? -(U) : (U), av_ = (V) < 0 ? -(V) : (V), b_ = (Y) > au_ ? (Y) : au_; \
                           b_ > av_ ? b_ : av_; })
    int cY, cU, cV, cM, cB, nY, nU, nV, nM, nB;
    YH_COL(ia, cY, cU, cV, cM);
    cB = YH_SKIP ? YH_BND(cY, cU, cV) : 0; nB = 0;
    for (int i = ia; i <= ib; i++, cY = nY, cU = nU, cV = nV, cM = nM, cB = nB) {
        YH_COL(i + 1, nY, nU, nV, nM);
#if YH_SKIP
        nB = YH_BND(nY, nU, nV);
#endif
        int mx = cM > nM ? cM : nM;
        if (!mx) continue;
        int xs = 8 * i + 4, xe = xs + 8;
        /* halo at the words of this cell: bilinear in x, Q3 (words at 1/8, 3/8, 5/8, 7/8 of the cell) */
        int sY = nY - cY, sU = nU - cU, sV = nV - cV;
        int qY = 8 * cY + sY, qU = 8 * cU + sU, qV = 8 * cV + sV;
        sY *= 2; sU *= 2; sV *= 2;
        if (xs < xa) {                                   /* first cell of a chunk starts mid-cell */
            int m = (xa - xs) >> 1;
            qY += m * sY; qU += m * sU; qV += m * sV; xs = xa;
        }
        if (xe > xb) xe = xb;
        uint32_t *pw = p + ((xs - x0) >> 1), *pe = p + ((xe - x0) >> 1);
#if YH_SKIP
        int bm = cB > nB ? cB : nB;
        if ((bm | mx) < 2048) {
            /* exact skip: if every pixel of the cell has luma >= T, every delta of the fast path rounds to 0 */
            uint32_t T = g->vthrB[bm >> 3], T2 = g->vthrM[mx >> 3];
            if (T2 > T) T = T2;
            if (T < 255) {
                uint32_t T8 = T << 8, T24 = T << 24;
                const uint32_t *q = pw;
                while (q < pe && (*q & 0xff00u) >= T8 && *q >= T24) q++;
                if (q == pe) {
#ifdef YH_STATS
                    g->st_cells_skip++;
#endif
                    continue;
                }
            }
        }
#endif
#ifdef YH_STATS
        g->st_cells_vis++;
#endif
        if (g->apply_mode == 4) { cell_null(pw, pe, qY, sY); continue; }
        if (mx <= hmin) {
#ifdef YH_STATS
            g->st_apply_fast += (uint32_t)(pe - pw);
#endif
            for (; pw < pe; pw++, qY += sY, qU += sU, qV += sV)
                fast_word(sl, pw, qY >> 3, qU >> 3, qV >> 3);
        } else
            cell_mixed(g, pw, pe, mx, qY, qU, qV, sY, sU, sV);
    }
#undef YH_COL
#undef YH_BND
}

/* ================================================================== YH_GEN: the light wrap of Genesis Pro 1.6.6
 * Written from the plugin's own kernels (halation_step1_step2.cl.txt; kernel_source.cl:1723 lightWrapI1Val,
 * :2340 LightWrapStepTwo, halation_performance_grain_head.cl.txt LightWrapStepTwoNoConvolve):
 *
 *   luma  = (r + g + b) / 3 + wrapSens - 0.94                   (wrapSens = light_wrap_sensitivity, default 0)
 *   a     = clamp(-0.55 + 1 / (0.626 * atan(6.5267 - 9.2033 * luma) + 1.147), 0, 1)
 *   i1_c  = a * wrapIntens(1) * wrapColor_c                     (the highlight mask, one value per channel)
 *   i2_c  = 1 - clamp(0.491 - 0.507 * atan(-5.5 * (-0.3 + c)), 0, 1)   (c = that channel of the source, 0..1)
 *   delta = max(blur(i1) - i1, 0)                               (difference of blur: the core of a light stays put)
 *   out   = src + delta * i2 * halation_strength
 *
 * blur: the cone |dist - r| / r of the Precision kernel (halation_version 1) or the Gaussian of the Performance
 * kernel (version 0, the plugin's default); radius r = max(1, maxDim * light_wrap_radius / (2048 * gauge)).
 * Here: the mask is gathered on the 1/8 grid the pyramid already uses (Q15 per channel), blurred there with two
 * box passes (a Gaussian to within a few percent, and every radius this build makes is 1..8 cells), and the
 * difference of blur is taken per pixel against the mask at that pixel's own luma - which is exactly the plugin's
 * operator, only its blur runs on the coarse grid.
 * Domain (declared in docs/DESIGN.md 1a): the plugin runs on LogC3, where 0.94 sits far above display white, and
 * adds in that log space; our frame is display-referred, so the arctan's own window (rank 0.58..0.95 -> 0..1) is
 * laid onto codes gen_thr..255, and the delta is added in code units, the closest this stage gets after the film
 * table. The gate i2 keeps a bright core from being lifted twice, as in the plugin. */
#define YH_GEN_MAX_RAD 8
#ifndef YH_GEN_BOX2
#define YH_GEN_BOX2 2                                    /* box passes; the plugin's Performance blur is a Gaussian */
#endif

/* atan for the init tables only (the EOS M ML core exports no atanf): polynomial on |x| <= 1 folded through
 * pi/2 - atan(1/x), error < 1e-5 - far below what a 256-entry table keeps */
static float yh_at1(float x)
{
    float x2 = x * x;
    return x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
}
static float yh_atan(float x)
{
    if (x < 0.0f)
        return -yh_atan(-x);
    if (x <= 1.0f)
        return yh_at1(x);
    return 1.5707963268f - yh_at1(1.0f / x);
}

/* the two tables, the wrap colour, the radius; yh_init calls this and returns (no display model, no matrices) */
static void yh_gen_tables(yh_ctx *g, const yh_params *p)
{
    int v, thr = p->gen_thr < 1 ? 1 : p->gen_thr > 254 ? 254 : p->gen_thr;
    float span = 255.0f - thr;
    g->g_thr = thr;
    for (v = 0; v < 256; v++) {
        float t = 0.58f + 0.37f * ((float)v - thr) / span;   /* the arctan's own 0.58..0.95 window, laid on the codes */
        float a = -0.55f + 1.0f / (0.626f * yh_atan(6.5267f - 9.2033f * t) + 1.147f);
        float c = (float)v * (1.0f / 255.0f);
        float q = 0.491f - 0.507f * yh_atan(-5.5f * (-0.3f + c));
        a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
        q = q < 0.0f ? 0.0f : q > 1.0f ? 1.0f : q;
        g->gta[v] = (uint16_t)(a * 32768.0f + 0.5f);
        g->gti[v] = (uint16_t)((1.0f - q) * 256.0f + 0.5f);
    }
    for (v = 0; v < 3; v++) {
        int c = p->gen_rgb_q8[v];
        c = c < 0 ? 0 : c > 256 ? 256 : c;
        g->gcol[v] = (int16_t)(c << 7);                  /* Q8 of 0..1 -> Q15 */
    }
    {
        int rad = p->gen_radius < 1 ? 1 : p->gen_radius > 40 ? 40 : p->gen_radius;
        int gg = p->gen_gauge_q8 < 64 ? 64 : p->gen_gauge_q8;
        int big = g->W > g->H ? g->W : g->H;
        int cells = (big * rad) / (64 * gg);              /* px = big * rad / (2048 * gg / 256), / 8 for the grid */
        if (cells < 1) cells = 1;
        if (cells > YH_GEN_MAX_RAD) cells = YH_GEN_MAX_RAD;
        g->g_rad = cells;
    }
    for (v = 1; v <= 17; v++)
        g->grq[v] = (uint32_t)(65536.0f / (float)v + 0.5f);
}

/* gather: the mean mask of a 1/8 cell, Q15 per channel, into L0 (the drive slots the pyramid would use) */
static void yh_gen_gather(yh_ctx *g, const uint32_t *p, int npix, int x0, int y)
{
    const uint8_t *mo = g->moff;
    const uint16_t *ta = g->gta;
    int thr = g->g_thr, c0 = g->gcol[0], c1 = g->gcol[1], c2 = g->gcol[2];
    uint32_t *acc;
    int nw = npix >> 1, clo = 1 << 30, chi = -1, gy;
    if (y & (g->ystep - 1))
        return;
    gy = y >> 3;
    if (gy != g->gacc_gy) { if (g->gacc_gy >= 0) flush(g); g->gacc_gy = gy; }
    acc = g->gacc + 4 * (x0 >> 3);
    for (int k = 0; k < nw; k++) {
        uint32_t w = p[k];
        int Y0 = (w >> 8) & 255, Y1 = (int)(w >> 24), ym = Y0 > Y1 ? Y0 : Y1;
        int U, V, rv, gu, bu;
        uint32_t *a;
        if (ym + mo[((w >> 3) & 31) | ((w >> 14) & 0x3e0)] < thr)
            continue;                                    /* no channel of this word can reach the mask */
        U = (int8_t)w; V = (int8_t)(w >> 16);
        rv = (359 * V) >> 8; gu = (88 * U + 183 * V) >> 8; bu = (454 * U) >> 8;
        a = acc + 4 * (k >> 2);
        for (int q = 0; q < 2; q++) {
            int Y = q ? Y1 : Y0, l = (3 * Y + rv - gu + bu) / 3, m;
            if (l < thr) continue;
            if (l > 255) l = 255;
            m = ta[l];
            if (!m) continue;
            a[0] += (uint32_t)((m * c0) >> 15);
            a[1] += (uint32_t)((m * c1) >> 15);
            a[2] += (uint32_t)((m * c2) >> 15);
        }
        if (k < clo) clo = k;
        chi = k;
    }
    if (chi >= 0) {
        int c_0 = (x0 >> 3) + (clo >> 2), c_1 = (x0 >> 3) + (chi >> 2);
        if (c_0 < g->glo) g->glo = c_0;
        if (c_1 > g->ghi) g->ghi = c_1;
    }
}

/* horizontal box of `src` (3 channels per cell when stride 3 = L0's mask, 4 when stride 4 = H0 itself) into H0,
 * normalised by the length the window actually used (the plugin skips the out-of-frame taps and divides by their
 * sum), edges replicated; H0's fourth slot takes the channel maximum (the span test of the apply) */
static void yh_gen_box_h(yh_ctx *g, const int16_t *src, int stride, int h0a, int h0b)
{
    int w = g->gw, r = g->g_rad;
    for (int y = h0a; y < h0b; y++) {
        const int16_t *s = src + stride * w * y;
        int16_t *d = g->H0 + 4 * w * y;
        int lo = w, hi = 0;
        for (int c = 0; c < w; c++) {
            int x0 = c - r < 0 ? 0 : c - r, x1 = c + r >= w ? w - 1 : c + r;
            uint32_t q = g->grq[x1 - x0 + 1], a0 = 0, a1 = 0, a2 = 0;
            int v0, v1, v2, mx;
            for (int x = x0; x <= x1; x++) {
                const int16_t *p = s + stride * x;
                a0 += (uint16_t)p[0]; a1 += (uint16_t)p[1]; a2 += (uint16_t)p[2];
            }
            v0 = (int)(((uint64_t)a0 * q) >> 16);
            v1 = (int)(((uint64_t)a1 * q) >> 16);
            v2 = (int)(((uint64_t)a2 * q) >> 16);
            mx = v0 > v1 ? v0 : v1;
            if (v2 > mx) mx = v2;
            if (mx > 32767) { v0 = v0 > 32767 ? 32767 : v0; v1 = v1 > 32767 ? 32767 : v1;
                              v2 = v2 > 32767 ? 32767 : v2; mx = 32767; }
            d[4 * c] = (int16_t)v0; d[4 * c + 1] = (int16_t)v1; d[4 * c + 2] = (int16_t)v2;
            d[4 * c + 3] = (int16_t)mx;
            if (mx) { if (c < lo) lo = c; hi = c + 1; }
        }
        g->h0lo[y] = (int16_t)lo; g->h0hi[y] = (int16_t)hi;
    }
}

/* build: two separable box passes of the mask, all at the 1/8 grid; H0 is the working buffer, L0 the copy the
 * vertical pass reads (the mask in L0 is not needed after this: the apply takes i1 from the pixel's own luma).
 * Halves (11-15): the TOP build has no mask below the seam yet, so the seam is its frame edge: the vertical window
 * stops at row s0 - 1 and is normalised by the rows it used, the plugin's own rule at the frame edges. Before, the
 * empty rows counted and the glow of a light across the seam fell to about half in the rows above it (host FXAB,
 * EOS M halves: +4.3 levels against +8.1 for the whole frame, 23 levels at worst). The TOP build also keeps the
 * mask rows the BOTTOM build needs (row s0 - 1 and the YH_GEN_BOX2 * r rows above it) in L[1], which this model
 * does not use; the BOTTOM build puts them back and starts its passes YH_GEN_BOX2 * r rows above row s0 - 1, each
 * pass r rows lower, so its rows from s0 - 1 down are the whole frame's exactly. Before, it blurred the TOP build's
 * left-over blurred rows again (-1.0 level just below the seam). */
static void yh_gen_build(yh_ctx *g, int y_begin, int y_end, int mode)
{
    int w = g->gw, h = g->gh, r = g->g_rad;
    int s0 = (mode == YH_TOP ? y_end : y_begin) >> 3;
    int ylim = mode == YH_TOP && s0 >= 1 ? s0 : h;        /* the vertical window ends before this row */
    int ka = s0 - 1 - YH_GEN_BOX2 * r, h0a = 0, h0b = mode == YH_TOP ? s0 + YH_H0X : h, keep, restored = 0;
    if (g->gacc_gy >= 0) { flush(g); g->gacc_gy = -1; }
    if (ka < 0) ka = 0;
    if (h0b > h) h0b = h;
    keep = mode != YH_WHOLE && s0 >= 1 && s0 < h && w * (s0 - ka) <= g->lw[1] * g->lh[1];
    if (keep && mode == YH_TOP) {
        memcpy(g->L[1], g->L[0] + 3 * w * ka, 6u * w * (s0 - ka));
        g->g_keep = s0;
    }
    if (mode == YH_BOTTOM) {
        if (keep && g->g_keep == s0) {
            memcpy(g->L[0] + 3 * w * ka, g->L[1], 6u * w * (s0 - ka));
            h0a = ka;
            restored = 1;
        } else
            h0a = s0 - 1 < 0 ? 0 : s0 - 1;               /* no TOP build kept them: the rows as they are */
    }
    memset(g->H0 + 4 * w * h0a, 0, 8u * w * (h0b - h0a));
    for (int pass = 0; pass < YH_GEN_BOX2; pass++) {
        /* the first row this pass gets right: a BOTTOM build's window reaches r rows above its first row */
        int va = restored ? s0 - 1 - (YH_GEN_BOX2 - 1 - pass) * r : h0a;
        if (va < h0a) va = h0a;
        /* horizontal: from L0 (pass 0) or from the previous pass's H0 */
        yh_gen_box_h(g, pass ? g->H0 : (const int16_t *)g->L[0], pass ? 4 : 3, h0a, h0b);
        for (int y = h0a; y < h0b; y++) {                 /* the horizontal result, kept for the vertical pass */
            const int16_t *s = g->H0 + 4 * w * y;
            uint16_t *d = g->L[0] + 3 * w * y;
            for (int c = 0; c < w; c++) {
                d[3 * c] = (uint16_t)s[4 * c];
                d[3 * c + 1] = (uint16_t)s[4 * c + 1];
                d[3 * c + 2] = (uint16_t)s[4 * c + 2];
            }
        }
        h0a = va;
        for (int y = h0a; y < h0b; y++) {                 /* vertical, out of the copy, into H0 */
            int y0 = y - r < 0 ? 0 : y - r, y1 = y + r >= ylim ? ylim - 1 : y + r;
            if (y0 > y1) y0 = y1;                         /* TOP rows below the seam: the edge row */
            uint32_t q = g->grq[y1 - y0 + 1];
            int16_t *d = g->H0 + 4 * w * y;
            int lo = w, hi = 0;
            for (int c = 0; c < w; c++) {
                uint32_t a0 = 0, a1 = 0, a2 = 0;
                int v0, v1, v2, mx;
                for (int k = y0; k <= y1; k++) {
                    const uint16_t *s = g->L[0] + 3 * w * k;
                    a0 += s[3 * c]; a1 += s[3 * c + 1]; a2 += s[3 * c + 2];
                }
                v0 = (int)(((uint64_t)a0 * q) >> 16);
                v1 = (int)(((uint64_t)a1 * q) >> 16);
                v2 = (int)(((uint64_t)a2 * q) >> 16);
                mx = v0 > v1 ? v0 : v1;
                if (v2 > mx) mx = v2;
                if (mx > 32767) { v0 = v0 > 32767 ? 32767 : v0; v1 = v1 > 32767 ? 32767 : v1;
                                  v2 = v2 > 32767 ? 32767 : v2; mx = 32767; }
                d[4 * c] = (int16_t)v0; d[4 * c + 1] = (int16_t)v1; d[4 * c + 2] = (int16_t)v2;
                d[4 * c + 3] = (int16_t)mx;
                if (mx) { if (c < lo) lo = c; hi = c + 1; }
            }
            g->h0lo[y] = (int16_t)lo; g->h0hi[y] = (int16_t)hi;
        }
    }
    g->cur_y = -1; g->row_lo = g->row_hi = 0;
}

/* one delta: the difference of blur (Q15) through the gate (Q8) and the strength (Q8), in code levels */
static inline int yh_gen_d(int cr, int gate, int hs)
{
    int t = (cr * gate) >> 8;                             /* Q15 */
    t = (t * 255) >> 7;                                   /* Q8 of a display level */
    t = (t * hs) >> 8;
    return (t + 128) >> 8;
}

/* the cell's words: the blurred mask interpolated between cells i and i+1 (Q3, as the pyramid's halo),
 * the plugin's per-pixel gate, then the delta added in code units and packed back into the UYVY word */
static __attribute__((noinline)) void yh_gen_cell(const yh_ctx *g, uint32_t *pw, uint32_t *pe,
                                                  int qR, int qG, int qB, int sR, int sG, int sB)
{
    const uint16_t *ta = g->gta, *ti = g->gti;
    int hs = g->g_str_q8, g0 = g->gcol[0], g1 = g->gcol[1], g2 = g->gcol[2], thr = g->g_thr;
    for (; pw < pe; pw++, qR += sR, qG += sG, qB += sB) {
        uint32_t w = *pw;
        int U = (int8_t)w, Y0 = (w >> 8) & 255, V = (int8_t)(w >> 16), Y1 = (int)(w >> 24);
        int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
        int bR = qR >> 3, bG = qG >> 3, bB = qB >> 3;
        int dR0 = 0, dG0 = 0, dB0 = 0, dR1 = 0, dG1 = 0, dB1 = 0, s_r, s_g, s_b;
        for (int q = 0; q < 2; q++) {
            int Y = q ? Y1 : Y0;
            int r = Y + rv, gg = Y - gu, b = Y + bu;
            int l = (r + gg + b) / 3, m, cr, cg, cb, dR, dG, dB;
            if (l < thr && !(bR | bG | bB))
                continue;                                 /* nothing can be added here */
            if (l > 255) l = 255;
            if (l < 0) l = 0;
            m = ta[l];
            cr = bR - ((m * g0) >> 15); if (cr < 0) cr = 0;
            cg = bG - ((m * g1) >> 15); if (cg < 0) cg = 0;
            cb = bB - ((m * g2) >> 15); if (cb < 0) cb = 0;
            dR = yh_gen_d(cr, ti[r < 0 ? 0 : r > 255 ? 255 : r], hs);
            dG = yh_gen_d(cg, ti[gg < 0 ? 0 : gg > 255 ? 255 : gg], hs);
            dB = yh_gen_d(cb, ti[b < 0 ? 0 : b > 255 ? 255 : b], hs);
            if (q) { dR1 = dR; dG1 = dG; dB1 = dB; }
            else   { dR0 = dR; dG0 = dG; dB0 = dB; }
        }
        s_r = dR0 + dR1; s_g = dG0 + dG1; s_b = dB0 + dB1;
        if (!(s_r | s_g | s_b))
            continue;
        Y0 += (77 * dR0 + 150 * dG0 + 29 * dB0 + 128) >> 8;
        Y1 += (77 * dR1 + 150 * dG1 + 29 * dB1 + 128) >> 8;
        U += (-43 * s_r - 85 * s_g + 128 * s_b + 256) >> 9;
        V += (128 * s_r - 107 * s_g - 21 * s_b + 256) >> 9;
        if (Y0 > 255) Y0 = 255;
        if (Y0 < 0) Y0 = 0;
        if (Y1 > 255) Y1 = 255;
        if (Y1 < 0) Y1 = 0;
        CLAMP_S8(U); CLAMP_S8(V);
        *pw = pack_word(U, Y0, V, Y1);
    }
}

static void yh_gen_apply(yh_ctx *g, uint32_t *p, int npix, int x0, int y)
{
    int xa = x0, xb = x0 + npix;
    int ia = (xa - 3) >> 3, ib = (xb - 5) >> 3;
    const int16_t *ra, *rb;
    int fy, wa, gw = g->gw, cR, cG, cB, nR, nG, nB;
    (void)y;
    if (ia < g->row_lo) ia = g->row_lo;
    if (ib > g->row_hi - 1) ib = g->row_hi - 1;
    if (ia > ib) return;
    ra = g->row_a; rb = g->row_b; fy = g->row_fy; wa = 16 - fy;
#define YH_GEN_COL(j, R, G, B) do {                                                      \
        int j_ = (j); if ((unsigned)j_ >= (unsigned)gw) j_ = j_ < 0 ? 0 : gw - 1;          \
        const int16_t *pa_ = ra + 4 * j_, *pb_ = rb + 4 * j_;                              \
        if (!(pa_[3] | pb_[3])) { R = G = B = 0; }                                         \
        else {                                                                             \
            R = (pa_[0] * wa + pb_[0] * fy + 8) >> 4;                                      \
            G = (pa_[1] * wa + pb_[1] * fy + 8) >> 4;                                      \
            B = (pa_[2] * wa + pb_[2] * fy + 8) >> 4;                                      \
        }                                                                                  \
    } while (0)
    YH_GEN_COL(ia, cR, cG, cB);
    for (int i = ia; i <= ib; i++, cR = nR, cG = nG, cB = nB) {
        int xs, xe, sR, sG, sB, qR, qG, qB;
        YH_GEN_COL(i + 1, nR, nG, nB);
        if (!(cR | cG | cB | nR | nG | nB))
            continue;
        xs = 8 * i + 4; xe = xs + 8;
        sR = nR - cR; sG = nG - cG; sB = nB - cB;
        qR = 8 * cR + sR; qG = 8 * cG + sG; qB = 8 * cB + sB;
        sR *= 2; sG *= 2; sB *= 2;
        if (xs < xa) {
            int m = (xa - xs) >> 1;
            qR += m * sR; qG += m * sG; qB += m * sB; xs = xa;
        }
        if (xe > xb) xe = xb;
        yh_gen_cell(g, p + ((xs - x0) >> 1), p + ((xe - x0) >> 1), qR, qG, qB, sR, sG, sB);
    }
#undef YH_GEN_COL
}
