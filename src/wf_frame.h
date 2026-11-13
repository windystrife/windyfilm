/* wf_frame.h - WindyFilm input stage (docs/DESIGN.md 3.1): what the camera input layer hands to the worker.
 *
 * Only the camera layer (wf_cam_eosm.c / wf_cam_5d2.c) knows hook registers, Canon buffer addresses and frame sizes.
 * It fills a struct wf_frame per hook call and passes it to wf_shot_run() (wf_shot.c), which resolves the look once per
 * shot (struct wf_look, wf_core.h) and dispatches on (frame.fmt, look.output). Kernels and effects only ever see the
 * frame's words through the worker's chunk loop, never a register or a fixed address.
 *
 * Formats:
 *   WF_FMT_UYVY8  Canon's developed 4:2:2 frame, words {U s8, Y0, V s8, Y1}, processed in place (data == dst).
 *                 The only format implemented.
 *   WF_FMT_RAW14  reserved: ML's raw buffer (raw_info), packed 14-bit Bayer. A develop stage (black, white, WB, demosaic,
 *                 matrix) would turn it into WF_FMT_LOG16 or straight into UYVY8 in `dst`.
 *   WF_FMT_LOG16  reserved: a 16-bit log-encoded working buffer that a develop stage writes and a 16-bit look path reads.
 * A (fmt, output) pair the worker has no stage for is logged ("nostage") and the frame is left untouched. */
#ifndef WF_FRAME_H
#define WF_FRAME_H
#include <stdint.h>

enum { WF_FMT_NONE = 0, WF_FMT_UYVY8 = 1, WF_FMT_RAW14 = 2, WF_FMT_LOG16 = 3 };
enum { WF_PART_FULL = 0, WF_PART_TOP = 1, WF_PART_BOT = 2 };

struct wf_frame {
    uint8_t  fmt;                 /* WF_FMT_* */
    uint8_t  part;                /* WF_PART_*: a whole frame, or the top / bottom half of a frame developed in two halves */
    int8_t   aspect;              /* JPEG aspect as the camera reported it (EOS M PROP_IMAGE_ASPECT_RATIO, -1 = unknown) */
    uint8_t  pad;
    int      w, h;                /* this request's pixels (a half: h = full height / 2) */
    int      fw, fh;              /* the whole frame (fh = 2 * h for a half) */
    int      y0;                  /* first row of this request inside the whole frame (BOT: fh / 2) */
    int      pitch;               /* bytes per row of data */
    void    *data;                /* input words (UYVY8: the frame itself, uncached pool alias) */
    void    *dst;                 /* output buffer; == data for the in-place UYVY8 path */
    /* RAW fields (reserved, 0 for UYVY8) */
    uint16_t black, white;        /* sensor levels */
    uint8_t  cfa;                 /* Bayer phase 0..3 (RGGB, GRBG, GBRG, BGGR) */
    uint8_t  bpp;                 /* bits per sample in data (14) */
    uint16_t raw_pad;
    /* bookkeeping for the log (camera layer) */
    uint32_t raw;                 /* the register value the buffer came from */
    int      t_post;              /* get_ms_clock() when the hook posted the request */
};

/* the frame's words for the UYVY8 path (pitch == 2 * w for every Canon L frame we hook) */
static inline uint32_t *wf_frame_words(const struct wf_frame *f) { return (uint32_t *)f->data; }
static inline int wf_frame_nwords(const struct wf_frame *f) { return f->h * (f->w / 2); }

#endif
