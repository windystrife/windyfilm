/* wf_darkp.h - Roll + Darkroom (docs/DESIGN.md 3.6): the pure parts of the darkroom's per-frame work, WindyStrife's own
 * code. No allocation, no file access, no log, integer only (armv5t, no 64-bit division), so the host test
 * (tests/host/wf_dark_test.c) runs exactly what the camera runs. wf_dark.c (the darkroom task) does the I/O around them.
 *
 *   wf_dk_white    the 14-bit white of an ISO when the shot's own raw shows no clip pile-up (wf_dev_levels)
 *   wf_dk_scales   the develop's channel scales (struct wf_dev_job wb_q8) from the CR2: Canon's as-shot WB levels
 *                  (MakerNote ColorData) for the colour, the fixed scene scale + a per-ISO trim for the exposure
 *   wf_dk_app1     the Exif APP1 segment in the head of Canon's own JPEG of the shot (copied into the print)
 *   wf_dk_comment  that APP1's UserComment (0x9286) overwritten in place with "ASCII\0\0\0" + our line
 *   wf_dk_thumb_*  the darkroom screen's print (WF_DR_PW x WF_DR_PH luma) and wire (WF_DR_TW x WF_DR_TH) thumbnails,
 *                  box means of Y gathered from the developed rows as they come */
#ifndef WF_DARKP_H
#define WF_DARKP_H
#include <stdint.h>

enum { WF_DK_EOSM = 0, WF_DK_5D2 = 1 };

int wf_dk_white(int cam, int iso);

/* m: raw unit -> linear Q24, x 256 (wf_dev.h). wb = the CR2's RGGB as-shot levels (wf_cr2_info.wb, 0 = none: the
 * camera's daylight). Exposure: the raw's clip (white - black) at 0.18 x 2^(3.3 + trim) like wf_dev.c's default, trim
 * from the ISO (wf_dk_trim10). Returns 1 = as-shot WB, 0 = daylight. */
int wf_dk_scales(int cam, const int wb[4], int black, int white, int iso, int32_t m[3]);
int wf_dk_trim10(int cam, int iso);              /* the exposure trim in 0.1 EV (+ = brighter) */

/* the shot's EV100 x 8 from its CR2 (ISO, ExposureTime et_n / et_d s, FNumber fn_n / fn_d), to about 1/8 EV: the
 * effects' highlight gain of a Darkroom print (wf_fx.c fx_hot), as wf_shot_ev8 gives it for a shot. WF_DK_EV_BAD
 * (= WF_EV_BAD) when a value is missing */
#define WF_DK_EV_BAD (-9999)
int wf_dk_ev8(int iso, uint32_t et_n, uint32_t et_d, uint32_t fn_n, uint32_t fn_d);

/* p / n: the first bytes of a JPEG (64 KB holds Canon's whole APP1). 0 = found: *off / *len = the segment from its
   0xFF 0xE1 marker (len includes the marker and the length field); -1 = none */
int wf_dk_app1(const uint8_t *p, uint32_t n, uint32_t *off, uint32_t *len);
/* 0 = written; -1 = no Exif IFD / no UserComment field / the text does not fit */
int wf_dk_comment(uint8_t *app1, uint32_t len, const char *txt);

#define WF_DK_PW 288                             /* = WF_DR_PW / PH / TW / TH (wf_dark.h) */
#define WF_DK_PH 192
#define WF_DK_TW 72
#define WF_DK_TH 48
struct wf_dk_thumb {
    int      w, h, bx, by, x0, y0;               /* frame, box, the boxes' top-left (centred) */
    int      row, n;                             /* print row being summed, frame rows summed into it */
    uint32_t sum[WF_DK_PW];
};
void wf_dk_thumb_begin(struct wf_dk_thumb *t, int w, int h);
/* frame rows [y, y + n) as UYVY words (pitch bytes per row); print rows are written into print as they complete */
void wf_dk_thumb_rows(struct wf_dk_thumb *t, const uint32_t *rows, int pitch, int y, int n, uint8_t *print);
int  wf_dk_thumb_done(const struct wf_dk_thumb *t);   /* print rows complete so far */
void wf_dk_wire(const uint8_t *print, uint8_t *wire);   /* 4 x 4 means of the print */

#endif
