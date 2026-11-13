/* wf_mist.h - "Mist": a clean-room simulation of a diffusion filter in front of the lens (a white / black
 * "mist" filter), for the Canon UYVY frame of WindyFilm (WindyStrife).
 *
 * Model (optics, nothing copied from a product or its code):
 *  - a diffusion filter scatters a fraction k of ALL the light that reaches it into a wide point spread function
 *    P, the rest goes straight through. Light adds in LINEAR light, so the frame is taken to linear light with
 *    the display model of the halation drive (yh_tables.h: sRGB below a tanh shoulder), diffused there, and
 *    encoded again with the same curve. Per pixel (S = the part of the light that scatters, S = L for the white
 *    filter):                 out = L - k S + k (P * S)             (colour and luma alike, one P: colour neutral)
 *    white: S = Lb (Lb = L with the clipped highlights boosted); out = (1 - k) Lb + k (P * Lb): energy conserving
 *    (P sums to 1), the glow takes the colour of its source, veiling glare lifts the shadows and lowers contrast.
 *    black: S = Lb w(Lb) with w(L) = L^2 / (L^2 + Lh^2): dark light scatters little (the glow is made by the
 *    highlights), the same form keeps it energy conserving; P has less of the wide components: shadows stay
 *    dark, contrast is mostly kept, a more local halo around highlights.
 *  - highlight recovery: a clipped highlight holds more light than the clip value. The brightest channel code m of
 *    a pixel from MIST_R0 up ramps (smoothstep) to MIST_GMAX times the linear light, all channels of the pixel by
 *    the same gain (hue kept), before the blur AND in the pixel's own term, so a lamp stays clipped and glows.
 *    MIST_GMAX is the night value: the shot's exposure scales it (mist_set_hot), x1 in daylight (a clipped sky or
 *    sky through leaves is barely over the clip; x8 there made every gap between leaves a glowing dot, 11-12).
 *  - source chroma: U, V of a source pixel go to linear light with the slope of the display curve at its luma, but
 *    never more than a power law of the mid-tones would give (gamma at code 128): the tanh shoulder's slope at codes
 *    250..255 is 13x that, and a UYVY pair of a clipped sky pixel and a leaf pixel shares the leaf's chroma, so the
 *    uncapped slope (x8) made a saturated cyan / green glow around the sky (11-12).
 *  - P = wA A + wB B + wC C, three Gaussians of 31, 124, 467 px (sigma at 5184 px width, a 50 mm Super-35-equivalent
 *    framing; the PSF is physical on the sensor, so the same in pixels for every aspect crop; mist_init takes a
 *    scale for the lens focal length, 256 = 50 mm equivalent, the module passes 256: fixed, documented).
 *  - low resolution: the frame is read into 16 x 16 pixel cells (every 2nd row), one luma + two chroma maps of
 *    LINEAR light (chroma = U, V codes x the slope of the display curve at the pixel's luma); A is blurred at
 *    1/16, B at 1/64, C at 1/256 of the frame (box pyramid), upsampled bilinear and summed into one glow map G =
 *    k P * S (3 channels). At the pixel: row-interpolated G, per pair incremental horizontal interpolation, own
 *    term and encode through per-luma tables (no per-pixel divide, no per-pixel RGB conversion).
 *
 * Order in the module (wf_shot.c wf_colour_fx): mist (camera words, display model) -> halation gather (it sees the
 * misted light, as the film behind the filter would) -> colour look -> halation / bloom -> grain.
 *
 * Whole frame:  mist_begin_frame; mist_gather_chunk for the rows (chunks of 288 words, x0 % 16 == 0);
 *               mist_build(g, MIST_WHOLE); then mist_apply_chunk per chunk (any row range, y = row of the FULL frame).
 * Halves (Diana, 5D2 Path A later): TOP: begin, gather rows 0 .. H/2 - 1, build(MIST_TOP) (the rows below the seam are
 *               mirrored, eroded, so a small source above the seam leaves no phantom glow), apply those rows.
 *               BOT: gather rows H/2 .. H - 1 (the top half's low-res rows are kept), build(MIST_BOTTOM) (the glow is made
 *               on the whole frame, then pulled onto the top half's seam row over MIST_SEAMD cell rows so there is no
 *               step), apply those rows.
 * The glow map is the only state the apply needs: mist_apply_rows / mist_apply_chunk can run on any rows of a frame
 * whose map was built (a range of rows with a y offset equals the whole frame). */
#ifndef WF_MIST_H
#define WF_MIST_H
#include <stdint.h>

#define MIST_MAX 8                     /* menu values 1..4 white 1/8 .. 1, 5..8 black 1/8 .. 1 (0 = Off) */
#define MIST_CELL 16
#ifdef WF_BUILD_5D2
#define MIST_MAXW 5616                 /* 5D2: the frame moved down 48 rows (wf_cam.h WF_HALO_Y0): 3792 rows, the seam at cell row 120 */
#define MIST_MAXH 3792
#define MIST_SEAM_CY(gh) 120           /* the TOP / BOT seam (frame row 1872 + 48 = 1920 = 120 cells = 30 x 64 px) */
#define MIST_SEAM_BAD(H) 0
#else
#define MIST_MAXW 5184
#define MIST_MAXH 3456
#define MIST_SEAM_CY(gh) ((gh) >> 1)
#define MIST_SEAM_BAD(H) (((H) >> 1) & (MIST_CELL - 1))
#endif
#define MIST_NLV 5                     /* levels: L0 = 1/16 ... L4 = 1/256 of the frame */
#define MIST_R0 250                    /* first brightest-channel code of the highlight ramp */
#define MIST_GMAX 8                    /* linear gain of a clipped code 255 at hot 100 % (guess: x8; x3 until 11-12, lamps barely
                                        * glowed); the shot's exposure scales it in EV (mist_set_hot): x1 in daylight */
#define MIST_NHOT 9                    /* hot steps 0, 1/8 .. 1 of the GMAX EV (mist_set_hot) */
#define MIST_SEAMD 12                  /* halves: cell rows over which the bottom half's glow is pulled to the top's seam row */
enum { MIST_WHOLE = 0, MIST_TOP = 1, MIST_BOTTOM = 2 };

typedef struct mist_ctx mist_ctx;
uint32_t mist_ctx_bytes(void);
uint32_t mist_mem_bytes(void);         /* maps and rows for a MIST_MAXW x MIST_MAXH frame (the block after the context) */
/* level 1..MIST_MAX (white 1..4, black 5..8), bw = a B&W film follows (nothing here depends on it; kept for the
 * caller's key), scale_q8 = focal length scale of the PSF (256 = 50 mm equivalent); mem = mist_mem_bytes() bytes, 4-aligned.
 * Re-callable on a live context with new parameters (tables only; the maps are begun by mist_begin_frame). */
int  mist_init(mist_ctx *g, int level, int scale_q8, void *mem);
/* per shot, before the gather of a whole frame / TOP (a BOT keeps its TOP's): the clipped top's gain is MIST_GMAX^(hb_q8 / 256)
 * (hb_q8 0..256, rounded to eighths; mist_init leaves 256). In daylight a clipped pixel is the sky, barely over the clip;
 * at night a lamp, many stops over (wf_fx.c fx_hot). */
void mist_set_hot(mist_ctx *g, int hb_q8);
/* 0 = this frame cannot be done (not a whole number of cells, or halves not on a cell row): leave it plain */
int  mist_begin_frame(mist_ctx *g, int W, int H);
void mist_gather_chunk(mist_ctx *g, const uint32_t *words, int npix, int x0, int y);
void mist_build(mist_ctx *g, int mode);
void mist_apply_chunk(mist_ctx *g, uint32_t *words, int npix, int x0, int y);
void mist_apply_rows(mist_ctx *g, uint32_t *words, int W, int y0, int nrows);   /* whole rows of W px, first row y0 */
/* test / tuning hooks */
int  mist_k_q16(int level);
int  mist_black(int level);
#endif
