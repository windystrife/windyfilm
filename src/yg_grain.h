/* yg_grain.h - "Yedlin-principled" film grain stage for WindyFilm.
 *
 * Principles implemented (Steve Yedlin, ASC, yedlin.net "On Film Grain Emulation"):
 *  - probabilistic, not geometric: per output level the grain value is drawn from a
 *    fitted amplitude distribution (inverse-CDF / quantile table), not copied from a scan;
 *  - RNG seeded, refreshed per shot, no stored scan plates: templates are synthesised from
 *    a hash RNG at film load (optionally per shot), block placement is re-seeded per shot;
 *  - "analyse once, ship results": datasheet granularity curves -> small tables at init;
 *  - amplitude depends on the colour AND brightness of the pixel ("dark or light or red or
 *    blue"): in the FULL tier every dye layer gets its own amplitude from its own level.
 * Film physics: per-layer grain in negative (or slide) density, blue layer coarsest,
 * Graves-Saunders two-cloud spectrum, printed through a display-density chain.
 *
 * Per-pixel code is integer only (ARMv5TE, no FPU). yg_init() uses float for the 6 KB of
 * tables (soft-float) and integer code for the templates: 128 M instructions under qemu-arm
 * (FULL colour, -Os), 61 % of it the integer Gaussian blur; once per film load, not per shot.
 * All output depends only on (absolute x, absolute y, shot seed, tables): whole frames,
 * Diana half frames (y0 = 1728) and 8-row RGB strips give bit-identical grain.
 */
#ifndef YG_GRAIN_H
#define YG_GRAIN_H
#include <stdint.h>

enum { YG_NEG = 0, YG_SLIDE = 1, YG_BW = 2 };
enum { YG_FULL = 0, YG_FAST = 1 };
#define YG_SEAM_CUT 0x10  /* OR into tier: no overlap blend; block rows staggered per block column,
                            vertical seams jittered per row (cheaper; default = AV1 27/17 blend) */

#define YG_TS   256   /* template side */
#define YG_BS   32    /* block side */
#define YG_WIN  34    /* block window incl. 2 px overlap continuation */
#define YG_NR   32    /* level rows in the quantile tables */
#define YG_NQ   64    /* quantiles (ranks) per row */
#define YG_NDP  12    /* max points in a granularity curve */

typedef struct {
    const char *name;
    int   mode;            /* YG_NEG, YG_SLIDE, YG_BW */
    int   iso_grain;       /* box speed of the emulsion: sets dye-cloud size (ISO^0.25) */
    float ei_stops;        /* exposure index offset in stops, + = under-exposed (800T = 500T @ EI800: +0.68) */
    float amount;          /* scale on density grain, 1.0 = datasheet granularity */
    float size_scale[3];   /* per-layer linear grain size factor R,G,B */
    float d_fine_um;       /* single dye cloud diameter at ISO 200 (Stephenson & Saunders 2007: 6.5) */
    float d_clus_um;       /* cluster diameter at ISO 200 (15) */
    float rho;             /* inter-layer correlation of the white noise (crosstalk) */
    int   np[3];           /* points per layer curve */
    float dn[3][YG_NDP];   /* net density (NEG/BW) or total slide density (SLIDE) */
    float sig[3][YG_NDP];  /* RMS granularity sigma_D at 48 um aperture (Kodak RMS/1000) */
    float dmid[3];         /* density of an 18% grey per layer (print aim / slide density) */
    float neg_gamma;       /* negative gamma, used only for the EI offset */
    float print_gamma;     /* slope |d display density / d neg density| at mid grey */
    float rho_max;         /* display density range of print / paper / slide */
    int   nlayers;         /* 3 colour, 1 B&W */
    int   chroma_shift;    /* chroma grain x 2^-n (perceptual / JPEG chroma quantiser), 0 = datasheet */
} yg_stock;

typedef struct {
    /* templates: rank planes, row-interleaved: row t, layer c at tmpl[(t*3+c)*YG_TS] */
    uint8_t  *tmpl;               /* YG_TS*YG_TS*3 bytes (192 KB) */
    int8_t    M[3][YG_NR * YG_NQ]; /* quantile tables, Q(qs) display levels (6 KB) */
    uint8_t   rowmap[768];         /* level v (-256..511) -> row 0..31 */
    uint8_t   clampY[768];         /* v (-256..511) -> 0..255 */
    uint8_t   clampC[512];         /* c (-256..255) -> -128..127 as u8 */
    int       qs;                  /* fractional bits of M */
    int       cs;                  /* chroma shift */
    int       tier, mode, nlayers, seam;
    int       gen;                 /* 1 = the Genesis model below (no templates, no tables of yg_init) */
    int       gamp_q8, gsat_q8, gshift;   /* Genesis: amplitude, saturation, lattice shift (cell 1 << shift px) */
    uint32_t  gseed0;              /* Genesis: film_grain_random_seed, the base of the per-shot seed */
    uint32_t  gseed;               /* Genesis: the shot's seed (base mixed with the file number) */
    uint32_t  seed;                /* shot seed (block placement) */
    uint32_t  tseed;               /* template seed */
    /* diagnostics filled by yg_init */
    float     kappa[3], s_fine[3], s_clus[3];
    float     sig_mid_px[3];       /* display-level sigma at mid grey per layer (8-bit) */
    float     cov[3][3];           /* layer field covariance (diagnostic) */
} yg_ctx;

/* Builds templates and tables. tmpl_mem: YG_TS*YG_TS*3 bytes cacheable (malloc at film load).
 * scratch: >= yg_scratch_bytes() bytes, freed after init. pitch_um = sensor-equivalent film
 * pitch (35 mm frame width / image width: 36000/5184 = 6.944). Returns 0 on success. */
int  yg_init(yg_ctx *g, const yg_stock *s, int tier, uint32_t tseed, float pitch_um,
             uint8_t *tmpl_mem, void *scratch);
unsigned yg_scratch_bytes(void);
/* integer-only template (re)synthesis, e.g. per shot in wf_task; needs init done once */
int  yg_build_templates(yg_ctx *g, const yg_stock *s, uint32_t tseed, float pitch_um, void *scratch);
void yg_set_shot(yg_ctx *g, uint32_t shot);   /* per-shot seed: hash(file number) */

/* ---------------------------------------------------------------- Genesis grain ("WindyGrain")
 * A second model, not a Yedlin stock: the plugin (Genesis Pro 1.6.6) draws every pixel's amplitude from a fixed
 * 64-point quantile table per channel (`CreateFilmGrain`, kernel_source.cl:2745 - the three tables are dumped in
 * halation_performance_grain_head.cl.txt), interpolates linearly on the fraction of the rank, replaces the lowest
 * 1/64 of the rank with a straight line to the table's own tail, mixes the three channels by `saturation`, scales
 * by `amplitude` and - when the grain buffer is smaller than the frame - upsamples it bilinearly with a jitter
 * (`ApplyFilmGrain`, kernel_source.cl:2804). The amplitude does not depend on the level: Genesis adds the same
 * amplitude in the dark and in the lights. The numbers are the plugin's (data, declared in docs/DESIGN.md 1a);
 * the code below is ours, integer only, no divide.
 * The grain lattice step (cell = 1 << gshift px) is our mapping of grain_film_gauge: Genesis renders the grain on
 * a buffer of `grainWidth` px and upsamples it when the frame is bigger, and sizes that buffer from the gauge
 * (16 mm 0.5 / 35 mm 1.0 / 65 mm 1.85: the host code is not in our sources). One sample per pixel at 35 mm and
 * size 1.0, one per 2 px from 16 mm (size / gauge >= 2), capped at 4 px; finer than a pixel (65 mm) the amplitude
 * is scaled by size / gauge (the mean of the samples a pixel would cover). GUESS. */
typedef struct {
    int      amp_q8;     /* film_grain_amplitude: 256 = the tables as dumped */
    int      sat_q8;     /* film_grain_saturation: 256 = each channel its own table, 0 = the green table for all */
    int      gauge_q8;   /* grain_film_gauge: 16 mm 128, 35 mm 256, 65 mm 474 */
    int      size_q8;    /* film_grain_size: 256 = 1.0 */
    uint32_t seed;       /* film_grain_random_seed; the shot's file number is mixed in (yg_set_shot) */
} yg_gen_params;
int  yg_gen_init(yg_ctx *g, const yg_gen_params *p, uint32_t shot);
void yg_gen_uyvy_row(const yg_ctx *g, uint32_t *w, int npix, int x0, int y);
void yg_gen_rgb_strip(const yg_ctx *g, uint8_t *rgb, int w, int nrows, int y0);

/* YUV hook: one row segment, Canon UYVY words {U s8, Y0, V s8, Y1}, x0 even, npix even */
void yg_uyvy_row(const yg_ctx *g, uint32_t *words, int npix, int x0, int y);
void yg_uyvy_frame(const yg_ctx *g, uint32_t *buf, int w, int h, int y0);
/* develop fallback: interleaved RGB888 strip, rows y0..y0+nrows-1 */
void yg_rgb_strip(const yg_ctx *g, uint8_t *rgb, int w, int nrows, int y0);

extern const yg_stock yg_stocks[];
extern const int yg_nstocks;
#endif
