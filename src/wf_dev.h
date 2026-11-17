/* wf_dev.h - WindyFilm full RAW develop (WindyStrife), "B" in docs/DESIGN.md 3.2: an optional slow mode that develops
 * Canon's 14-bit raw of the shot itself and writes the graded UYVY 4:2:2 frame into Canon's YUV buffer, so Canon's JPEG
 * encoder runs as usual. EOS M only: src/wf_rawb.c drives it (menu "RAW develop", RAW+L single shots; CORE-NOTES 10.7).
 *
 * Pipeline (per output pixel):
 *   14-bit packed raw (ML raw_pixblock, 8 px in 14 bytes) -> black subtract -> demosaic (MHC, 5x5, on int16 rows)
 *   -> clipped highlights rebuilt in camera space (colour ratios from a block map, see below)
 *   -> one 3x3 = scene gain x (Rec.709 -> log gamut) x (camera -> Rec.709) x per-shot channel scales (WB + exposure)
 *   -> linear Q14 (1.0 = JPEG white of the sRGB decode = 16384)
 *   -> per channel: CLZ-indexed table (256 steps per octave, 1/16384 steps below 1/64) -> grid coordinate Q10 of a
 *      33^3 node table in the cube's input space (the log curve, the cube's DOMAIN and the clamp folded in)
 *   -> tetrahedral interpolation of the node table, nodes packed as Y | Cb << 16 and Cr in 9-bit lanes (half codes),
 *      Q7 weights: one MUL + 3 MLA per word -> Y per pixel, Cb / Cr as the pair mean -> UYVY {U s8, Y0, V s8, Y1}.
 *
 * Node sources (wf_dev_build):
 *   WF_DEV_CUBE  Log + LUT / Film LUT cans: the card cube resampled to 33^3 at the nodes (exact for 2, 3, 5, 9, 17,
 *                33 and 65 point cubes), node = cube output R'G'B' -> Y'CbCr (BT.601 full range, as wf_loglut.c).
 *                Same log curves, gamut matrices, LUT EI and Film LUT auto exposure as the JPEG path (wf_loglut.c), so a
 *                raw whose linear light equals the JPEG's sRGB decode gives the Log + LUT picture (host test DEV).
 *   WF_DEV_LOG   plain log output: identity nodes (code value = R'G'B').
 *   WF_DEV_SRGB  base develop: Rec.709 primaries, sRGB OETF, clipped at 1.0 = a Canon-like 8-bit frame for the
 *                display-referred film tables (wf_film_words then runs unchanged on the developed frame). The grid is
 *                the sRGB code itself with identity nodes (exact between nodes).
 *   WF_DEV_DISP  a display-referred look cube on the rendered picture (Leica Vivid, WF_FLUT_DISP): the SRGB base
 *                develop's grid and enc (sRGB code, shoulder), nodes = the card cube sampled at those sRGB codes
 *                (exact for 2, 3, 5, 9, 17, 33 and 65 point cubes): no log curve, no V-Log step. LUT EI scales the
 *                linear light; Highlights / Shadows bend each output channel with wf_hs_bend's curve (build time).
 *
 * Per shot (wf_dev_prepare, worker, before the first row; once per shot, a Diana BOT half reuses its TOP's):
 *   - WB + exposure: per-channel scales fitted against Canon's own UYVY frame of the same shot (dst before it is
 *     overwritten): near-neutral, unclipped mid-tone samples on a 16 px grid, decoded with j.dec (the JPEG's input
 *     curve: A1's Canon curve slot, or sRGB) -> camera space -> sum ratio. So the develop follows Canon's AWB / WB shift / ISO / exposure without a
 *     property. A caller may pass its own scales instead (j.wb_q8, e.g. from Canon's colour block once P0 reads it).
 *   - highlight map: one pass over the raw at 2x2 quad resolution: a quad is a ratio sample when its brightest
 *     photosite is 55..92 % of clip and no clipped photosite lies within 2 quads (>= 3 px); sums per 32x32 px block
 *     -> R/G and B/G, holes filled from neighbour blocks (8 rounds), then neutral (trap W19).
 *   - film shoulder (j.roll): the look's tone curve to a knee half an octave below the first clip, then a shoulder that
 *     reaches the look's white with zero slope at the sensor's white, folded into a per-shot copy of the enc tables
 *     (no per-pixel cost; j.hue I / D: a per-shot shift table on the luma enc index instead, see WF_DEV_HUE_*).
 *     Without it a V-Log film cube, made for about 8 stops above grey, leaves the EOS M's clip
 *     (3.2 - 4.1 stops) mid-shoulder: clipped sky and sun stop flat at about 195 - 213 instead of rolling into white.
 * Rows (wf_dev_rows): any band of output rows, so the NN full frame and the Diana halves use the same code.
 *
 * Threads and memory: wf_dev_build runs in wf_task (file I/O through wf_ll_parse, allocates its slabs with
 * __mem_malloc like wf_loglut.c). The worker calls wf_dev_prepare / wf_dev_rows only: no allocation, no log, no file
 * access, no wait. The node table (struct wf_dev_tabs, 356 KB) and the per-shot scratch (wf_dev_scratch_bytes(), about
 * 0.4 MB for the EOS M frame) are the caller's (wf_rawb.c: one 790 KB shoot memory block, no SRM fallback), given back in
 * playback. Integer only. armv5t, no LDRD / STRD, no 64-bit division (own shift-subtract divide). */
#ifndef WF_DEV_H
#define WF_DEV_H
#include <stdint.h>
#include <stddef.h>
#include "wf_loglut.h"

#define WF_DEV_G      33                         /* node grid per axis */
#define WF_DEV_NODES  (WF_DEV_G * WF_DEV_G * WF_DEV_G)
#define WF_DEV_GF     10                         /* grid coordinate fraction bits: 0 .. 32 << 10 */
#define WF_DEV_XONE   16384                      /* kernel linear Q14: 1.0 = JPEG white (sRGB decode) */
#define WF_DEV_XMAX   ((1 << 24) - 1)            /* 1024.0 (the tables saturate at 128.0, above every log curve's 1.0) */
#define WF_DEV_NENC   4352                       /* CLZ index of x in 0 .. WF_DEV_XMAX */
#define WF_DEV_BSTR   1152                       /* nodes per blue slab: 33 x 33 + padding, so that the word stride of
                                                    blue (2304 = 9 << 8) is an ARM immediate */
#define WF_DEV_BLK    32                         /* highlight map block, px */

enum { WF_DEV_CUBE = 1, WF_DEV_LOG = 2, WF_DEV_SRGB = 3, WF_DEV_DISP = 4 };
enum { WF_DEV_EOSM = 0, WF_DEV_5D2 = 1, WF_DEV_NCAM };

/* load-time tables (wf_task builds, the worker reads). Layout fixed for a later asm kernel: node first. */
struct wf_dev_tabs {
    uint32_t node[2 * WF_DEV_BSTR * WF_DEV_G];   /* node i = r + 33 g + 1152 b: [2i] = Y9 + 1 | (Cb9 + 1) << 16,
                                                    [2i+1] = Cr9 + 1; Y9 = 2 Y', Cb9 = 2 (Cb + 128), Cr9 = 2 (Cr + 128),
                                                    each 0..510 (the + 1 is the kernel's rounding: weights sum to 128) */
    uint32_t enc[3][WF_DEV_NENC];                /* per channel c: CLZ index of x (Q14) -> (cell x word stride of c) << 9
                                                    | fraction Q7: the sum of the three is the node word offset << 9
                                                    plus three fractions (< 512) */
    int32_t  gm[9];                              /* camera (white balanced) -> working gamut, Q20 (rows sum to 2^20) */
    int32_t  gain;                               /* scene-light gain Q16: LUT EI x Film LUT auto exposure */
    uint8_t  src, curve, cam, n;                 /* WF_DEV_*, log curve 1..5, camera, cube size (0 for LOG / SRGB) */
    int8_t   auto10;                             /* the auto exposure part in 0.1 EV (0 = off / none) */
    int8_t   ei;                                 /* LUT EI in 1/3 EV */
    uint8_t  domain, pad;                        /* 1 = a cube DOMAIN other than 0..1 */
};

/* one shot's raw and output (the camera layer fills it; only it knows the buffers) */
struct wf_dev_job {
    const uint8_t *raw;                          /* packed 14-bit raw, row 0 of the sensor buffer (uncached alias is fine) */
    int      pitch;                              /* bytes per raw row (EOS M 9240) */
    int      ax0, ay0, ax1, ay1;                 /* active area [ax0, ax1) x [ay0, ay1) in raw pixels: neighbours outside
                                                    are mirrored (EOS M CR2: 72, 52, 5280, 3528) */
    int      x0, y0;                             /* raw pixel of output pixel (0, 0), both even: Canon's JPEG crop (EOS M
                                                    L: 84, 64 in the CR2s of Day09, see CORE-NOTES 10) */
    int      w, h;                               /* the whole output frame (EOS M L 3:2: 5184 x 3456) */
    int      black, white;                       /* 14-bit levels of this shot (EOS M black ~2048; white per ISO: the
                                                    1/3-stop ISOs clip lower, ISO 320 at ~12636: take the camera's) */
    int      cfa;                                /* Bayer phase at (x0, y0): 0 RGGB, 1 GRBG, 2 GBRG, 3 BGGR */
    uint32_t *canon;                             /* Canon's finished UYVY frame of this shot (w x h, pitch 2 w) for the
                                                    WB fit, 0 = no fit; read before any row is written */
    int      canon_y0, canon_h;                  /* the rows of the frame that canon holds (a Diana TOP: 0, h / 2) */
    int32_t  wb_q8[3];                           /* caller's scales raw unit -> linear Q24 (x 256), all 0 = fit / default */
    const uint32_t *dec;                         /* how the fit reads canon: code i/4 -> linear Q24, 1022 entries (the
                                                    input curve of Log + LUT: wf_ll_dec_c<k> on main's A1), 0 = sRGB */
    int      roll;                               /* 1 = film shoulder: the sensor's white lands on the look's white (CUBE,
                                                    SRGB; see wf_dev.c wf_dv_roll), 0 = the look's curve as is */
    int      canon_pw;                           /* words per row of canon, 0 = w / 2 (a 5D2 half: the 5616 px row while
                                                    the develop covers 5600 px of it, w must be a multiple of 32) */
    int      hue;                                /* the shoulder's colour, WF_DEV_HUE_* (only with roll on a log curve's
                                                    CUBE tables; A elsewhere): the chain films' RAW develop takes I */
    int    (*ms)(void);                          /* a ms clock for st->ro_ms, 0 = none */
};

/* how the film shoulder treats colour (j.hue). The shoulder is one map of the enc index i -> i' (wf_dv_roll):
 *   A  each channel through the map at its own index: a coloured light whitens as soon as its brightest channel rolls
 *      (blue sky above the knee turns magenta; the Log + LUT cans, the sRGB base develop, RAWB14 / RAWB15 and before)
 *   D  one shift per pixel, the map's shift at the pixel's luma index: R:G:B in linear stay, the hue holds into the
 *      highlights (comparisons and the cost bench only)
 *   I  D below the look's white, then over 1.5 stops (WF_DV_HUE_SPAN) a smooth step toward A: lights keep their colour
 *      up to the look's white and roll into cream / paper white above it (the owner's pick 2026-11-15, the gallery's
 *      shoulder-e round). Per shot a shift table on the luma enc index; per pixel a luma, one lookup, and per channel
 *      one more lookup and multiply-add (src/wf_dev_arm.S wf_dv_span_hs; CORE-NOTES 10.9). */
enum { WF_DEV_HUE_A = 0, WF_DEV_HUE_I = 1, WF_DEV_HUE_D = 2, WF_DEV_HUE_N };
const char *wf_dev_hue_name(int hue);           /* "a" / "i" / "d" (logs, WINDYFLM.CFG shoulder=), "?" out of range */
/* the colour trim the chain films take with I (wf_rawb_logpk_build, on the built table): saturation and vibrance in %,
   wf_dev_trim. The owner 2026-11-15: i a little less saturated, -10 / -10; 2026-11-16, still a bit strong: that again
   x 0.9, so -19 / -19 (a muted colour x 0.81, the strongest x 0.81 x 0.81 = 0.656: the -10 trim applied twice, to 0.3 %) */
#define WF_DEV_HUE_I_SAT (-19)
#define WF_DEV_HUE_I_VIB (-19)
/* on built tables (any src): Y' of every node kept (the tone and every detail stay, a neutral is untouched), Cb / Cr x
   (1 + sat / 100) (1 + vib / 100 x s), s = max - min of the node's R'G'B' (0 .. 1): sat on every colour, vib weighted by
   the colour's own saturation (negative: the strong colours cut most, the muted ones left to sat). Once per build, no
   per-pixel cost; sat = vib = 0 leaves the table as built. */
void wf_dev_trim(struct wf_dev_tabs *t, int sat, int vib);
/* the Paper row on built tables (wf_loglut.h WF_PAPER_*; DESIGN 3.10): every node's Y'CbCr through wf_paper_ycc16, once
   per build, no per-pixel cost. Callers: the tables that hold the whole look (a can's CUBE / DISP, a chain film's log
   table: wf_rawb.c, wf_dark.c), never the SRGB base develop (its film table carries the paper) nor plain LOG. The .RBT
   holds the tables without it (applied after the cache read / write). p = Glossy: untouched. */
void wf_dev_paper(struct wf_dev_tabs *t, int p);

/* per-shot results (log line, tests) */
struct wf_dev_stat {
    int32_t  m_q8[3];                            /* the scales used: raw unit -> linear Q24, x 256 */
    int      wb_src;                             /* 0 default (daylight, +3.3 EV clip), 1 fit, 2 caller */
    int      fit_n, fit_tier;                    /* samples used, tier (0 neutral |C| <= 20, 1 <= 64, 2 any) */
    int      clip_quads, hl_samples, hl_blocks;  /* clipped quads, ratio samples, blocks with clipped quads */
    int      hl_fill;                            /* blocks filled from neighbours / by the neutral fallback */
    uint32_t hl_px;                              /* pixels through the highlight path (wf_dev_rows) */
    int      sh, fast;                           /* matrix shift; 1 = the asm span kernel runs (blocks without clipping) */
    int      roll;                               /* 1 = the film shoulder is in this shot's enc tables */
    int      ro_k, ro_c, ro_w, ro_grey;          /* enc indices (256 per octave): knee, sensor white, look's white, 18 % grey */
    int      ro_ytop;                            /* the look's white the shoulder ends on, 256 Y' */
    int      hue;                                /* WF_DEV_HUE_* this shot's shoulder took (A when roll is 0) */
    int      ro_ms;                              /* the shoulder's per-shot tables, ms (j.ms; 0 without a clock) */
};

/* ------------------------------------------------------------------ load time (wf_task) */
/* Build the tables for node source src (WF_DEV_*), log curve 1..5, camera WF_DEV_EOSM / WF_DEV_5D2. CUBE: cube = card path
 * of the .cube (wf_ll_parse rules and results, WF_LL_*), ei = LUT EI 1/3 EV, autoexp = 1 for the Film LUT cubes (18 %
 * grey -> 118 as wf_loglut.c). LOG / SRGB: cube ignored (0). Returns WF_LL_OK or a WF_LL_E_* code (t is then unusable). */
/* WF_DEV_DISP with Highlights / Shadows h, s (-5 .. 5, wf_hs_bend) on the cube's output channels; wf_dev_build with
 * src WF_DEV_DISP is the same with h = s = 0 (curve, autoexp ignored) */
int wf_dev_build_disp(struct wf_dev_tabs *t, int cam, const char *cube, int ei, int h, int s, struct wf_ll_info *info);
int wf_dev_build(struct wf_dev_tabs *t, int src, int curve, int cam, const char *cube, int ei, int autoexp,
                 struct wf_ll_info *info);
/* The chain films' log pack, ML/DATA/WFLOG.DAT (option (c), docs/DESIGN.md 1a; tools/mk_wflog.py writes it):
 *   header 32 B  "WFLG", u16 version 1, u16 count, u32 grid 33, u32 table bytes 215622, u32 index offset 32,
 *                u32 data offset, u32 FNV-1a of the index, u8 curve (WF_LL_*, the grid's log curve), u8 feed (0 = the
 *                chain was fed the curve's own gamut, 1 = linear Rec.709; information only), u16 0
 *   index  24 B  u8 catalogue id, u8 print variant (WF_PRINT_*), u8 flags (bit 1 B&W), u8 0, char code[8] (the table
 *                code, NUL padded), u32 offset, u32 bytes, u32 Adler-32 of the table
 *   table        33^3 nodes, red fastest (the .cube order), R' G' B' u16 little-endian each, value x 65535: the
 *                develop's own grid for the header's curve (DOMAIN 0..1). Entries may share one table (the chain's
 *                identical print tables are stored once).
 * wf_dev_build_pack: the entry (id, variant, code) as a WF_DEV_CUBE build on the header's curve, LUT EI ei, no auto
 * exposure: the very tables wf_dev_build makes of a 33-point .cube with those samples, without the text parse. The table
 * is read in 8190-byte slabs into wf_dev_build's own node sampler (no 215 KB buffer); its Adler-32 is checked before
 * the tables count. Returns WF_LL_OK, WF_LL_E_OPEN / _READ / _NOMEM / _TRUNC, or a WF_DEV_PK_E_* code (t unusable then);
 * pi (may be 0) gets the pack's curve and feed, the entry and the index checksum for the log line and a table cache. */
#define WF_DEV_PK_BYTES (WF_DEV_NODES * 6)
enum { WF_DEV_PK_E_FORMAT = 40,                  /* not a WFLG pack of this version / grid / table size / curve */
       WF_DEV_PK_E_INDEX,                        /* index unreadable, its checksum, or the entry's place in the file */
       WF_DEV_PK_E_NOENT,                        /* no entry with this id, variant and code */
       WF_DEV_PK_E_SUM };                        /* the table's checksum */
struct wf_dev_pkinfo { int curve, feed, count, flags; uint32_t off, sum, ixsum; };
int wf_dev_build_pack(struct wf_dev_tabs *t, int cam, const char *pack, int id, int variant, const char *code, int ei,
                      struct wf_ll_info *info, struct wf_dev_pkinfo *pi);
const char *wf_dev_pk_errstr(int rc);            /* short text of any result above (wf_loglut_errstr for WF_LL_*) */
/* a WF_DEV_CUBE build on curve from samples made on the camera (a chain film's composed log table, the film-stage knob
 * rows on RAW: wf_stagek.h's log grid): fill(ctx, rgb, n) writes the next n nodes (n = 33^2, one blue plane, the .cube
 * order, R' G' B' u16 value x 65535) and returns WF_LL_OK or a WF_LL_* code that stops the build; LUT EI ei, no auto
 * exposure. The same samples make the very tables wf_dev_build_pack makes of a pack entry holding them. */
int wf_dev_build_gen(struct wf_dev_tabs *t, int cam, int curve, int ei, int (*fill)(void *ctx, uint16_t *rgb, int n),
                     void *ctx, struct wf_ll_info *info);
/* a pack entry's samples as they are (the knob tables' residual, wf_rawb.c): fn(ctx, le, n) gets the next n nodes (the
 * .cube order, R' G' B' u16 little-endian, n <= 1364 a call, n x 6 bytes at le) and returns WF_LL_OK or a code that
 * stops the read; the table's Adler-32 is checked against the index after the last call (WF_DEV_PK_E_SUM). pi as
 * wf_dev_build_pack. Returns WF_LL_OK, WF_LL_E_OPEN / _READ / _NOMEM / _TRUNC, a WF_DEV_PK_E_* code or fn's. */
int wf_dev_pack_samples(const char *pack, int id, int variant, const char *code,
                        int (*fn)(void *ctx, const uint8_t *le, int n), void *ctx, struct wf_dev_pkinfo *pi);
/* identity of the builder for a table cache (wf_rawb.c's .RBT): FNV-1a of WF_DEV_TABS_VER, the table size, src, curve,
 * cam and every constant table wf_dev_build reads for them (camera matrix, LUT EI gains, the sRGB decode, the curve's gamut
 * matrix, log encode and EI constants). Cached tables of another build id are rebuilt. Bump WF_DEV_TABS_VER whenever
 * wf_dev_build's arithmetic changes the tables of a cube (a change of the constants alone changes the id by itself). */
#define WF_DEV_TABS_VER 1
uint32_t wf_dev_tabs_id(int cam, int src, int curve);

/* ------------------------------------------------------------------ the shot (worker) */
size_t wf_dev_scratch_bytes(int w, int h);       /* cacheable scratch for a frame of w x h (aligned to 32 inside) */
/* per shot: WB / exposure, highlight map, the matrix. scratch = wf_dev_scratch_bytes(j->w, j->h) bytes, cacheable, kept
 * until the shot's last wf_dev_rows (a Diana BOT keeps its TOP's scratch). Returns 0, or -1 for a job it cannot do. */
int  wf_dev_prepare(void *scratch, const struct wf_dev_tabs *t, const struct wf_dev_job *j, struct wf_dev_stat *st);
/* output rows [ya, yb) of the frame into dst (UYVY words, dst_pitch bytes per row; dst row 0 = frame row ya), through
 * the cached scratch (rows are written whole, 8-word bursts). */
void wf_dev_rows(void *scratch, int ya, int yb, uint32_t *dst, int dst_pitch);
/* the same rows, each one's pixels [x, x + n) only (x, n even; clamped to the job's w): the 5D2 Darkroom develops 5632
   columns (w a multiple of 32) for Canon's 5616 (wf_darkp.h wf_dk_geom) */
void wf_dev_rows_win(void *scratch, int ya, int yb, uint32_t *dst, int dst_pitch, int x, int n);

/* this shot's levels from its own raw (worker, before wf_dev_prepare; j: raw, pitch, ax0..ay1 only).
 *   black: mean of the masked columns [bx0, bx1) (EOS M: 16, 64; the image data starts at 72) on every 64th active row;
 *          0 when those columns do not look like optical black (outside 1024 .. 4096)
 *   white: the clip pile-up: the most frequent value >= 8192 on every 4th active row, when it holds >= 200
 *          photosites and >= 8 x the mean count of the 64 values 17 .. 80 below it (tools/cr2_dec.c's rule at a quarter of
 *          the rows); 0 = no pile-up (nothing clipped in the sample: the caller's per-ISO value).
 * scratch: at least 32 KB (the histogram; wf_dev_scratch_bytes is far more). About 8 MB of raw read. */
void wf_dev_levels(void *scratch, const struct wf_dev_job *j, int bx0, int bx1, int *black, int *white);

/* ------------------------------------------------------------------ parts (host tests, tools) */
/* one pixel of linear light (x Q14 per channel, working gamut; t->gain is applied here) -> 256 Y', 256 (Cb + 128),
   256 (Cr + 128): the kernel's lane sums before rounding */
void wf_dev_px(const struct wf_dev_tabs *t, const int32_t x[3], int o[3]);
/* camera -> Rec.709 (Q20, rows sum to 2^20) of a camera */
const int32_t *wf_dev_cam_rgb(int cam);
void wf_dev_unpack14(const uint8_t *src, int16_t *dst, int npx, int black);   /* npx multiple of 8, src at a pixblock */
/* 1: the C reference instead of src/wf_dev_arm.S for the blocks without clipping (host tests; same output bit for bit) */
void wf_dev_kernel(int ref);

#endif
