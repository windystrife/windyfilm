/* wf_rsrc.h - RAW develop "B": which buffer holds this shot's raw (WindyStrife). Camera-independent.
 *
 * The EOS M keeps the 14-bit raw in two alternating buffers (P0, raw-path BAOCAO section 7). The camera layer names
 * the candidate frame origins (sensor row 0, column 0) from Canon's parameter block or the EDMAC register; the pointer
 * is right for single shots but points at the previous shot for the last shot of a burst, and it is 0 for the first
 * shot after power on. So the buffer is never trusted: wf_rs_pick compares it with Canon's finished YUV frame of the
 * same shot before anything is developed.
 *
 * Check: up to WF_RS_NS mid-tone sample blocks of 8 x 4 pixels on a 16 x 16 grid over the frame (Canon's Y 40..210).
 * Per block, the raw side is the sums of its three colours less black, the YUV side is the block's mean Y read through
 * the JPEG's input curve (dec: code i/4 -> linear Q24, 0 = code^2). Score = the multiple correlation of a least-squares
 * fit Y = a R + b G + c B + d over the blocks, x 1000 (scale, offset and WB free: exposure, WB and ISO do not matter;
 * green alone scored 0.88 - 0.93 on the synthetic shot's saturated patches, Canon's Y is not green). The best
 * candidate wins when its score is >= WF_RS_MIN and no other one is within WF_RS_TIE of it; a tie goes to the earlier
 * candidate (the camera layer lists the pointer's buffer first). Reads 4 rows x 14 bytes per block and candidate:
 * about 60 KB through the uncached alias, a few ms.
 *
 * Threads: the worker (wf_rawb.c), before wf_dev_prepare. No allocation, log or file access. */
#ifndef WF_RSRC_H
#define WF_RSRC_H
#include <stdint.h>

#define WF_RS_NCAND 4
#define WF_RS_NS    256                          /* sample blocks at most */
#define WF_RS_NMIN  64                           /* fewer usable blocks: no decision (WF_RS_FEW) */
#define WF_RS_MIN   800                          /* score x 1000 a buffer needs (real pairs: own 911-1000) */
#define WF_RS_TIE   3                            /* closer than this (x 1000): the earlier one wins */

enum { WF_RS_OK = 0, WF_RS_FEW, WF_RS_NOMATCH, WF_RS_NOCAND, WF_RS_N };

struct wf_rs_geom {
    int pitch;                                   /* bytes per raw row (EOS M 9240) */
    int x0, y0;                                  /* raw pixel of YUV pixel (0, 0): EOS M L 84, 64; x0 % 8 == 4 or 0 */
    int w, h;                                    /* the YUV frame (pitch 2 w) */
    int black;
    int g_odd;                                   /* 1 = green at even raw rows on odd columns (RGGB / BGGR); 0 = GRBG / GBRG */
};

struct wf_rs_res {
    int pick;                                    /* candidate index, -1 = none */
    int why;                                     /* WF_RS_* */
    int n;                                       /* blocks used */
    int score[WF_RS_NCAND];                      /* x 1000; -9999 = no address */
};

/* cand[k] = frame origin of candidate k (readable for pitch x (y0 + h + 4) bytes), 0 = none. yuv = Canon's UYVY frame. */
int wf_rs_pick(const uint32_t *cand, int ncand, const uint32_t *yuv, const struct wf_rs_geom *g, const uint32_t *dec,
               struct wf_rs_res *r);
const char *wf_rs_why(int why);                  /* "ok", "few", "nomatch", "nocand" */

#endif
