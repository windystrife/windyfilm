/* yl_lab.h - the Lab effects: halation, bloom and grain with the film-lab numbers (WindyStrife's own port).
 *
 * film-lab is the owner's film-look reference (formulas + presets for 8 / 16 / 35 / 65 mm, its own C++ and JS code
 * and a PPM CLI). This is a separate, fixed-point implementation of its Bloom, Halation and Grain stages for the
 * camera (ARMv5TE, no FPU, no divide per pixel); no file of film-lab is copied here. The numbers it uses are the
 * film-lab's preset tables: yl_lab.c holds them as tables. What the camera does differently from the
 * reference, and why, is docs/DESIGN.md 3.20 (each point is measured against the reference CLI by tools/lab_check.sh):
 *   - halation and bloom are blurred on a 1/8 (halation) and a 1/16 (bloom) grid of cell means, then interpolated
 *     per pixel pair; the blur variance is the reference's three-box Gaussian minus what the grid adds;
 *   - the halation core (0.75 x layer) and the colour, the clamps and the bloom's ease-off in the highlights are per pixel, on the
 *     camera's own UYVY words (full-range BT.601, as Canon writes them), as deltas: a word without any glow is kept
 *     bit-exact;
 *   - the bloom's edge mask uses the largest luma step per 16-px cell instead of a per-pixel Sobel (the
 *     reference dilates and blurs that map over 16 + 46 px anyway);
 *   - both glows are gathered from the frame before the bloom (the reference blurs the halation layer of the bloomed
 *     frame; the per-pixel core uses the bloomed pixel, as the reference does);
 *   - the grain is the reference's lattice noise (its hash at whole cells of the cell size, bilinear in between over the
 *     weights' root sum of squares, R G B on their shifted lattices, the clump blur, the strength gain of the 800-row
 *     preview) on a periodic lattice of P x P cells: its 256 x 256 samples of that period are computed once per gauge
 *     with the exact kernel per axis, a pixel mixes the four round it (within ~3 % rms of the exact field, the lost
 *     variance put back), at a random offset per shot; the period is 512 .. 2560 px; the lattice's turn is left out.
 * film-lab's tone stages before the bloom are not on the camera (the film's own table does that part). */
#ifndef YL_LAB_H
#define YL_LAB_H

#include <stdint.h>

#define YL_NG     4              /* film-lab presets: 0 8 mm, 1 16 mm, 2 35 mm, 3 65 mm */
#define YL_TS     256            /* grain tile side (fine samples of one period) */
#define YL_GT     16             /* grain: lattice taps per sample and axis */
#define YL_GR     32             /* grain: the largest clump blur radius (px) */
#define YL_LPMAX  4096           /* grain: the longest period (px) */
#define YL_XF     256            /* halves: the BOT fades from the TOP's glow to the whole frame's over this many rows */
#define YL_WHOLE  0
#define YL_TOP    1
#define YL_BOTTOM 2
#define YL_BLK    64             /* apply: words per block of yl_ctx.blk */

typedef struct {
    int hal, bloom;              /* 1 = on */
    int gauge;                   /* 0..3 (YL_NG order) */
} yl_params;

typedef struct {                 /* one separable blur on a grid: up to 3 boxes, then a small kernel */
    int nbox, r[3];
    int nk;                      /* taps each side of the kernel (0 = none) */
    uint16_t k[24];              /* kernel, Q15, k[0] the centre */
} yl_plan;

typedef struct {
    int W, H, gw, gh, bw, bh;    /* frame; 1/8 grid (halation), 1/16 grid (bloom) */
    int hal, bloom, gauge;
    int two;                     /* both on: the halation layer is gathered again from the bloomed frame (gather2) */
    int seam;                    /* halves: the seam row (frame row) of the last yl_build, 0 = whole */
    /* halation tables */
    uint32_t mtab[2048];         /* mask x 65536 x 4095 / 4080 by luma in eighth levels */
    uint16_t pw[4096];           /* layer = x^curve, Q15, by x * 4095 */
    int lw_q15, gw_q15;          /* near / far weight x mix, Q15 */
    int kr, kg, kb;              /* halo colour x 4080 (Q4 levels per unit halo) */
    int hmin;                    /* lowest luma (eighth levels) with a non-zero mask */
    yl_plan pl_l, pl_g;          /* halation: local blur of the layer, then the step from local to global */
    /* bloom tables */
    int bthr;                    /* start level, luma in quarter levels: pixels at or above it are the bloom layer */
    int bgain_q12;               /* 2^gain (EV), Q12 */
    uint16_t kl[1024];           /* amount x 1.6 x (1 - ease x 0.35 x L^0.25), Q15, by luma in quarter levels */
    int nl;                      /* bloom levels */
    uint16_t wl[6];              /* layer weights, Q15 */
    yl_plan pl_b[6];             /* layer i: the step from layer i-1 */
    yl_plan pl_e;                /* edge map: its box blur */
    int edil;                    /* edge map: dilation in 1/16 cells */
    int emask_q15;               /* the edge mask's strength, Q15 */
    uint8_t moff[1024];          /* max(R - Y, G - Y, B - Y, 0) per (U >> 3, V >> 3) bin */
    uint8_t mneg[1024];          /* max(Y - R, Y - G, Y - B, 0) per bin */
    uint8_t lofs[1024];          /* how far the clamped RGB's luma can rise over Y per (U >> 3, V >> 3) bin */
    uint32_t bint[1024];         /* apply: moff | mneg << 8 | lofs << 16 per bin */
    uint16_t ky[256];            /* apply: kl by Y (kl[Y << 2]) */
    int klmax;                   /* the largest kl (the bloom's k at black), Q15 */
    int hy, hu, hv;              /* the halo colour's Y / U / V, Q4 levels per unit halo (Q15) */
    int lmin;                    /* gather: a word whose Y + lofs stays under this has no layer and no bloom */
    /* buffers (one block from yl_mem_bytes) */
    uint16_t *P, *BL, *BG, *T;   /* 1/8: layer mean, local and global glow (Q15), blur temp */
    uint16_t *HI, *TB, *AC;      /* 1/16, 3 channels interleaved: bloom layer mean, blur temp, glow (Q4 levels) */
    uint8_t  *E;                 /* 1/16: largest luma step */
    uint32_t *pacc, *bacc;       /* gather sums of the current cell row */
    uint8_t  *eacc, *prow;       /* its luma steps; the last gathered pixel row (edges) */
    uint8_t  *YM, *yacc;         /* both on, 1/16: the largest Y + lofs per cell (gather2 skips cells under its band) */
    int32_t  *ln0, *ln1;         /* blur lines */
    uint16_t *sv, *svb;          /* halves: the TOP's halation / bloom rows round the seam (each its own block) */
    int16_t  *hlo, *hhi, *blo, *bhi;   /* non-zero cells per glow row (lo > hi: none) */
    uint16_t *vh, *vb;           /* apply: the current pixel row's glow, between its two grid rows */
    uint16_t *vq;                /* apply: that row's halo of a pixel without a core, per 1/8 cell (Q15) */
    int16_t  *vb4;               /* apply: that row's bloom glow per 1/16 cell as Y, U, V and its largest channel */
    int32_t  blk[3 * YL_BLK];    /* apply, per word of a block: the halo (Q15); the bloom's Y; its U << 16 | V */
    int pj, bj, prow_y;          /* gather: current cell rows, last pixel row */
    /* apply: current pixel row */
    int cur_y, hl0, hh0, bl0, bh0;     /* its non-zero cells (lo > hi: none) */
} yl_ctx;

uint32_t yl_mem_bytes(int W, int H, int hal, int bloom);
int  yl_init(yl_ctx *g, const yl_params *p, int W, int H, void *mem);
void yl_begin_frame(yl_ctx *g);
/* frame rows (not the halation grid's): chunks of a row in order, x0 % 8 == 0; the whole frame or a half.
 * Order per frame / half: yl_gather_chunk (after the film's colour pass), yl_build; when g->two, a read-only pass of
 * yl_gather2_chunk over the same rows and yl_build2; then yl_apply_chunk. */
void yl_gather_chunk(yl_ctx *g, const uint32_t *words, int npix, int x0, int y);
void yl_build(yl_ctx *g, int y_begin, int y_end, int mode);
void yl_gather2_chunk(yl_ctx *g, const uint32_t *words, int npix, int x0, int y);
void yl_build2(yl_ctx *g, int y_begin, int y_end, int mode);
void yl_apply_chunk(yl_ctx *g, uint32_t *words, int npix, int x0, int y);

/* grain: a tile of YL_TS x YL_TS x 3 int8 (per tile row: Y, U, V planes: the field's samples over one period of lp px)
 * and a row of that period (int16 Y per pixel, U and V per word) in the caller's memory (YL_GBYTES), built once per
 * gauge (soft-float hash) with YL_GSCRATCH bytes of scratch */
typedef struct {
    int8_t  *tile;
    int16_t *ry, *ru, *rv;       /* the current pixel row over one period, x256 tile units */
    int16_t kz[256];             /* amount x 0.28 x gain x zone(L) x 255 per tile unit, Q16 levels, by Y */
    uint8_t cly[384], clu[384];  /* Y clamped to 0..255 by Y + 64; U / V to -128..127 (as bytes) by U + 192 */
    int d, lp, st, cy;           /* px per sample (even), period (YL_TS d px); 1 / d, Q12; the row in ry / ru / rv
                                    (-1: none) */
    int ox, oy, bw;              /* per-shot offset (px, ox even); 1 = B&W film (Y only) */
    int gauge, clump, lat, maxabs, gain_q12;   /* diagnostics: the clump blur (px), the lattice period (cells),
                                                    the largest |noise| (Q13), film-lab's strength gain */
} yl_grain;

#define YL_GBYTES   (YL_TS * YL_TS * 3 + YL_LPMAX * 2 + YL_LPMAX * 2)
#define YL_GSCRATCH (5 * YL_TS * YL_TS * 2 + YL_TS * YL_GT * 4 + YL_TS * 2 + 4 * YL_TS * 8)
int  yl_grain_init(yl_grain *g, int gauge, int H, int bw, void *tile_mem, void *scratch);
void yl_grain_shot(yl_grain *g, uint32_t seed);
void yl_grain_row(yl_grain *g, uint32_t *words, int npix, int x0, int y);

#endif
