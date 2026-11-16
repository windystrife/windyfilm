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
 *   WF_FMT_RAW14  RAW develop "B" (wf_rawb.c, menu "RAW develop", EOS M NN only): data == dst = Canon's UYVY8 frame of
 *                 the shot (read for the buffer check and the WB fit, then overwritten row by row by the develop); the
 *                 packed 14-bit raw is one of rawc[] (the worker checks which, wf_rsrc.c). After the stage the frame is
 *                 UYVY8 again: developed, or Canon's own when the stage could not run (the 8-bit path then runs).
 *   WF_FMT_LOG16  reserved: a 16-bit log-encoded working buffer that a develop stage writes and a 16-bit look path reads.
 * A (fmt, output) pair the worker has no stage for is logged ("nostage") and the frame is left untouched. */
#ifndef WF_FRAME_H
#define WF_FRAME_H
#include <stdint.h>

enum { WF_FMT_NONE = 0, WF_FMT_UYVY8 = 1, WF_FMT_RAW14 = 2, WF_FMT_LOG16 = 3 };
enum { WF_PART_FULL = 0, WF_PART_TOP = 1, WF_PART_BOT = 2 };
/* RAW develop on, but not for this shot (the camera layer's gate; fmt stays UYVY8, the worker logs it) */
enum { WF_RBS_NONE = 0, WF_RBS_QUALITY, WF_RBS_ASPECT, WF_RBS_DRIVE, WF_RBS_NOADDR, WF_RBS_N };
#define WF_RB_NCAND 4
#define WF_FSEG_MAX 16                   /* pieces of a devd frame (struct wf_frame nseg): a split head from up to 15 shoot chunks */
struct wf_fseg { int y; uint32_t *p; };  /* a piece: from row y of the request, at p */

struct wf_frame {
    uint8_t  fmt;                 /* WF_FMT_* */
    uint8_t  part;                /* WF_PART_*: a whole frame, or the top / bottom half of a frame developed in two halves */
    int8_t   aspect;              /* JPEG aspect as the camera reported it (EOS M PROP_IMAGE_ASPECT_RATIO, -1 = unknown) */
    uint8_t  devd;                /* 1 = the Darkroom's frame (wf_dark.c): UYVY8 already developed from a CR2 by B; the
                                     worker runs only the look's stage after B (docs/DESIGN.md 3.6) and keeps it out of
                                     the shot log's EXIF ring; 2 = the same, developed with a chain film's log table
                                     (wf_rawb.h): the film's look is in it, the stage adds the effects only */
    int      w, h;                /* this request's pixels (a half: h = full height / 2) */
    int      fw, fh;              /* the whole frame (fh = 2 * h for a half) */
    int      y0;                  /* first row of this request inside the whole frame (BOT: fh / 2) */
    int      pitch;               /* bytes per row of data */
    void    *data;                /* input words (UYVY8: the frame itself, uncached pool alias) */
    void    *dst;                 /* output buffer; == data for the in-place UYVY8 path */
    /* RAW fields (RAW14 only, else 0) */
    uint16_t black, white;        /* sensor levels when the worker cannot measure them (the white of the shot's ISO) */
    uint8_t  cfa;                 /* Bayer phase 0..3 (RGGB, GRBG, GBRG, BGGR) at (rx0, ry0) */
    uint8_t  bpp;                 /* bits per sample in data (14) */
    uint8_t  nrawc;               /* candidates in rawc */
    uint8_t  rb_skip;             /* UYVY8 with the menu's RAW develop on: why not this shot (WF_RBS_*), 0 = not asked */
    uint32_t rawc[WF_RB_NCAND];   /* candidate raw frame origins (sensor row 0, column 0; uncached alias), 0 = none */
    int      rb_arg;              /* the skip's value (drive mode, pic_quality) */
    int      rpitch;              /* bytes per raw row */
    int16_t  rax0, ray0, rax1, ray1;   /* the active area in raw pixels */
    int16_t  rx0, ry0;            /* raw pixel of output pixel (0, 0) (Canon's JPEG crop) */
    int16_t  rbx0, rbx1;          /* masked columns [rbx0, rbx1) for the black level */
    /* bookkeeping for the log (camera layer) */
    uint32_t raw;                 /* the register value the buffer came from */
    int      t_post;              /* get_ms_clock() when the hook posted the request */
    int      dev_ev8;             /* devd only: the CR2's own EV100 x 8 for the effects' highlight gain (wf_dk_ev8;
                                     WF_EV_BAD = the CR2 does not tell), never the camera's settings at develop time */
    /* devd only: the frame in pieces (wf_dark.c, when no one block holds it: the owner's RAWB17 EOS M had one 31.9 MB
       SRM chunk for a 35 MB frame). nseg > 0: rows seg[k].y .. seg[k + 1].y - 1 of this request at seg[k].p (pitch as
       above), the last piece up to h; seg[0].y = 0 and data = seg[0].p. 0 = data holds every row (all other frames) */
    uint8_t  nseg;
    const struct wf_fseg *seg;
};

/* the frame's words for the UYVY8 path (pitch == 2 * w for every Canon L frame we hook) */
static inline uint32_t *wf_frame_words(const struct wf_frame *f) { return (uint32_t *)f->data; }
static inline int wf_frame_nwords(const struct wf_frame *f) { return f->h * (f->w / 2); }
/* row r of the request (0 .. h - 1), pieces or not */
static inline uint32_t *wf_frame_row(const struct wf_frame *f, int r)
{
    if (!f->nseg)
        return (uint32_t *)((uint8_t *)f->data + (uint32_t)r * (uint32_t)f->pitch);
    int k = f->nseg - 1;
    while (k > 0 && r < f->seg[k].y)
        k--;
    return (uint32_t *)((uint8_t *)f->seg[k].p + (uint32_t)(r - f->seg[k].y) * (uint32_t)f->pitch);
}

#endif
