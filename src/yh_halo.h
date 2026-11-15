/* yh_halo.h - halation + bloom stage for the EOS M in-camera film engine (Canon UYVY before JPEG).
 *
 * Model (see README.md for sources and confidence labels):
 *  - estimate scene-linear highlight energy from display YUV: per-channel R'G'B' -> inverse display curve,
 *    clipped code 255 assigned +boost EV above the top code, small isolated clipped blobs (light sources,
 *    glints) a further +pboost EV, minus a knee (light below the knee does not drive);
 *  - one RGB "drive" pyramid: L0 = 1/8 res (648x432 for 5184x3456), tent-decimated to L5 = 1/256;
 *  - each level is blurred [1 2 1]^2 and mixed by a 3x3 matrix M_k = bloom_k * I + hal_k (x) spec:
 *    bloom keeps the colour of the source (neutral optics), halation is a red/orange (grey for B&W) response
 *    to the light that reaches the base (spec = spectral weights of that light); per-level weights are NNLS
 *    fits of target PSFs (fit_levels.py -> yh_weights.h);
 *  - progressive bilinear upsampling to one 1/8 halo grid H0 = (hY, hU, hV, hMax) in linear light;
 *  - apply: the halo is added in LINEAR light and re-encoded with the same display curve. Small halos
 *    (the wide tails) use the tangent of the curve at the pixel's luma (one table read + multiply per
 *    pixel); near sources an exact per-channel lin -> add -> encode path. The YUV delta is added to the
 *    original word, so pixels with zero halo stay bit-identical.
 * Per-pixel code is integer only (ARMv5TE, no FPU, no divide). yh_init uses float (soft-float) once;
 * yh_build divides once per isolated clipped cell (a few hundred per frame).
 *
 * Whole frame:  yh_begin_frame; yh_gather_chunk for the rows (chunks of 288 px, x0 % 8 == 0);
 *               yh_build(g, 0, H, YH_WHOLE); then in the main look pass yh_apply_chunk per chunk.
 * Diana halves: top: yh_begin_frame; gather rows 0..1727; yh_build(g, 0, 1728, YH_TOP); apply 0..1727.
 *               bottom: gather 1728..3455; yh_build(g, 1728, 3456, YH_BOTTOM); apply 1728..3455.
 *   The top half sees no light below row 1728. The bottom half sees the whole frame (the top half's L0 rows
 *   are kept) and its coarse (>= 1/32) part is crossfaded from the top half's version over `xfade` rows of
 *   32 px, so the wide glow has no step at the seam.
 * Module changes (WindyStrife camera port, camera review 2026-11-10): a pixel whose brightest channel is in the
 * clipped-energy ramp boosts all three channels by that channel's gain (hue kept, no R-only glow); the top
 * build mirrors below the seam only light that reaches the seam as a wide area (no phantom glow from small
 * sources above the seam); the bottom build keeps its own H0 cells below the seam where they are larger
 * than the top build's (a source just below the seam keeps its glow). */
#ifndef YH_HALO_H
#define YH_HALO_H
#include <stdint.h>

#define YH_NLEV 6            /* L0 = 1/8 ... L5 = 1/256 */
#define YH_NBAS 7            /* bases: L0 sharp, L0 blurred, L1..L5 blurred */

/* YH_SPK_*: the spektrafilm halation model (fit_levels.py): 3 bounces from the base, the same sigma in every channel
 * (65 um still, 50 um cine), strengths by the film's anti-halation class; S still strong, W still weak, C cine strong,
 * N cine none (no rem-jet), BW / BWW B&W strong / weak (GUESS, spektrafilm has no B&W) */
enum { YH_NEG = 0, YH_C800T, YH_SLIDE, YH_BW, YH_SPK_S, YH_SPK_W, YH_SPK_C, YH_SPK_N, YH_SPK_BW, YH_SPK_BWW,
       YH_GEN,                   /* the light wrap of Genesis Pro 1.6.6 (yh_halo.c + DESIGN 1a): its own mask, blur,
                                  * difference-of-blur and gate; strength / radius / gauge from yh_params.gen_* */
       YH_NSTOCK };
#define YH_STOCK_BW(s) ((s) == YH_BW || (s) == YH_SPK_BW || (s) == YH_SPK_BWW)   /* one grey layer: the halo writes Y only */
enum { YH_WHOLE = 0, YH_TOP = 1, YH_BOTTOM = 2 };

typedef struct {
    const char *name;
    const float (*hw)[YH_NBAS];  /* [3][YH_NBAS] halation weights per output channel (fraction of drive) */
    float xt[3][3];              /* drive of the halation in output layer c by light j (rows sum to 1):
                                  * colour film: each layer is re-exposed by the light it is sensitive to
                                  * (near-diagonal); B&W: one layer, every row = spectral weights */
    float knee_ev;               /* default knee, EV over 18 % grey */
    float boost_ev;              /* default energy of a clipped code 255, EV above the top code */
    float pboost_ev;             /* default extra EV for small isolated clipped blobs */
} yh_stock;

typedef struct {
    int   stock;                 /* YH_NEG ... */
    int   hal_q8;                /* halation strength, 256 = 100 % (stock default), 0 = off */
    int   bloom_q8;              /* bloom strength, 256 = 100 % of bloom_frac, 0 = off */
    float bloom_frac;            /* fraction of the drive energy spread as bloom at 100 % */
    float knee_ev;               /* < -50: stock default */
    float boost_ev;              /* < -50: stock default */
    float pboost_ev;             /* < -50: stock default; 0 = off */
    int   tint_q8;               /* 0 = film tint, 256 = neutral (white) halation ("why not white?") */
    int   ystep;                 /* gather every n-th row: 1, 2 (default) or 4 */
    int   xfade;                 /* Diana: crossfade below the seam in 32-px rows (0 = off, default 8) */
    int   exact_only;            /* 1 = exact per-channel path everywhere (reference for tests) */
    int   pair_t;                /* pair path threshold abs(Y0 - Y1), default 40; < 0 = no pair path */
    int   gauge_q8;              /* film gauge: 256 = 35 mm (the profiles as fitted, default), 128 = 16 mm, 474 = 65 mm;
                                  * halo and bloom keep their size on the film (yh_init: yh_gauge_w) */
    /* YH_GEN (Genesis light wrap): the mask, its blur and the gate of the plugin; hal_q8 is halation_strength */
    int   gen;                   /* 1 = the Genesis model (stock must be YH_GEN; knee / boost / bloom / tint unused) */
    int   gen_radius;            /* light_wrap_radius: the halo radius in px at 2048 px and 35 mm, default 10 */
    int   gen_gauge_q8;          /* halation_film_gauge: 16 mm 128, 35 mm 256, 65 mm 474 */
    int   gen_thr;               /* first display code of the mask ramp, default 240 (Genesis's luma threshold 0.94) */
    int   gen_rgb_q8[3];         /* light_wrap_color * 256, default 102 128 115 (0.40 / 0.50 / 0.45, GUESS on B) */
} yh_params;

typedef struct {
    int W, H, gw, gh;            /* frame, L0 grid */
    int lw[YH_NLEV], lh[YH_NLEV];
    int ystep, yshift, xfade, xf_rcp;
    int kneev;                   /* smallest display code with a non-zero drive */
    int hfast;                   /* < 0: exact path only (tests) */
    int hmin;                    /* min over v of hmt[v] */
    int pair_t;                  /* pair path when abs(Y0 - Y1) <= pair_t, else exact */
    int pb_q8;                   /* point boost, EV * 256 (the current one: pb_base x hp_q8) */
    int pb_base, boost_q16, knee_q16;   /* at hot 100 %: point boost (EV * 256), clipped boost (EV, Q16); the knee (EV, Q16) */
    int hb_q8, hp_q8;            /* current scales of the clipped boost / point boost (yh_set_hot, 256 = the stock's) */
    int hs_q8;                   /* current scale of the halation part of M (yh_set_hot, 256 = params hal_q8) */
    int32_t M0[YH_NBAS][3][3];   /* M at hs 256 (yh_init) and its halation part: M = M0 - Mh0 + Mh0 * hs (bloom kept) */
    int32_t Mh0[YH_NBAS][3][3];
    uint32_t qk;                 /* knee, linear Q20 of the top code (drive = (linear - qk) / 1024) */
    uint16_t rgain[8];           /* clipped-energy ramp gain of the brightest channel code YH_RAMP0..255, Q8 */
    int32_t M[YH_NBAS][3][3];    /* Q16 */
    /* tables */
    uint16_t dl[768];            /* drive, index v+256 (v = -256..511, clamped), Q10 of the top code */
    uint32_t lt[768];            /* (linear Q20 << 8) | enc(linear), index v+256 */
    uint16_t sl[256];            /* tangent: display levels per Q15 halo unit at luma v, Q16 */
    uint16_t hmt[256];           /* largest halo (Q15) for which the tangent at v is within 0.75 level */
    uint8_t  vthrB[256];         /* exact skip: smallest luma v at which a halo bound B in [8k, 8k+7] rounds to 0 */
    uint8_t  vthrM[256];         /* exact skip: smallest luma v from which hMax in [8k, 8k+7] takes the fast path */
    uint32_t rsl[256];           /* 2^24 / sl[v] (pair path luma gain) */
    uint8_t  moff[1024];         /* conservative max(R'-Y, G'-Y, B'-Y, 0) per (U>>3, V>>3) bin */
    uint8_t  encA[1024], encB[1024], encC[2048];
    /* YH_GEN (Genesis light wrap): the two tables of its two arctans, the wrap colour, the blur radius and a
     * reciprocal of the box length (init only; the model needs no display model) */
    int      gen, g_rad, g_str_q8, g_thr;
    int      g_keep;             /* halves: the seam row (L0 grid) whose mask rows the TOP build kept in L[1], -1 none */
    int16_t  gcol[3];            /* light_wrap_color, Q15 */
    uint16_t gta[256];           /* the highlight mask (lightWrapI1Val's arctan), Q15 of 0..1 */
    uint16_t gti[256];           /* the per-channel gate (LightWrapStepTwo's i2), Q8 of 0..1 (0..256) */
    uint32_t grq[18];            /* 65536 / k, k = 1..17: the box blur's normaliser (its length at the edges) */
    /* buffers (one block from yh_mem_bytes) */
    uint16_t *L[YH_NLEV];        /* drive levels, 3 ch interleaved, Q10 */
    uint16_t *A[YH_NLEV];        /* A[k] (k >= 1) accumulated halo at level k, RGB Q16 */
    int16_t  *H0;                /* final 1/8 halo grid: hY, hU, hV, hMax (Q15) */
    uint16_t *C[3];              /* clipped drive at 1/8, 1/16, 1/32 (point boost) */
    int16_t  *l0lo, *l0hi;       /* non-zero cell span per L0 row */
    int16_t  *h0lo, *h0hi;       /* non-zero cell span per H0 row */
    int16_t  *a1lo, *a1hi;       /* non-zero span per A1 row */
    uint32_t *gacc;              /* gather accumulators, one L0 row: R, G, B, clipped */
    int32_t  *trow;              /* temp rows */
    uint16_t *xsave;             /* Diana: top-half A2 rows */
    int16_t  *h0save;            /* Diana: top-half H0 rows s0-1 .. s0+YH_H0X-1 and their spans */
    int gacc_gy, cur_y, row_lo, row_hi, seam2, nsave, glo, ghi;
    int row_fy, apply_mode;      /* current pixel row: vertical weight of H0 row j1; test: 4 = unpack/pack only */
    const int16_t *row_a, *row_b;
    /* statistics for the cost model (YH_STATS builds) */
    uint32_t st_gather_words, st_gather_hot, st_apply_fast, st_apply_pair, st_apply_exact, st_build_cells, st_pboost_cells;
    uint32_t st_cells_vis, st_cells_skip;
} yh_ctx;

extern const yh_stock yh_stocks[YH_NSTOCK];
void     yh_default_params(yh_params *p, int stock);
uint32_t yh_mem_bytes(int W, int H);
int      yh_init(yh_ctx *g, const yh_params *p, int W, int H, void *mem);
/* per shot, before yh_gather_chunk of a whole frame / Diana TOP (a BOT keeps its TOP's): how much brighter than the
 * top code a clipped pixel is taken to be. hb_q8 scales boost_ev (all clipped codes), hp_q8 pboost_ev (small isolated
 * clipped blobs), 0..256 (256 = the stock / params values, yh_init's state). hs_q8 scales the halation strength
 * (hal_q8; bloom unchanged), 0..256. Integer, ~260 table entries + 63 matrix entries: cheap. */
void     yh_set_hot(yh_ctx *g, int hb_q8, int hp_q8, int hs_q8);
void     yh_begin_frame(yh_ctx *g);
/* chunk of Canon UYVY words {U s8, Y0, V s8, Y1} of row y starting at pixel x0 (x0 % 8 == 0), npix even */
void     yh_gather_chunk(yh_ctx *g, const uint32_t *words, int npix, int x0, int y);
void     yh_build(yh_ctx *g, int y_begin, int y_end, int mode);
void     yh_apply_chunk(yh_ctx *g, uint32_t *words, int npix, int x0, int y);
#endif
