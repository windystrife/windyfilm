/* wf_darkp.h - Roll + Darkroom (docs/DESIGN.md 3.6): the pure parts of the darkroom's per-frame work, WindyStrife's own
 * code. No allocation, no file access, no log, integer only (armv5t, no 64-bit division), so the host test
 * (tests/host/wf_dark_test.c) runs exactly what the camera runs. wf_dark.c (the darkroom task) does the I/O around them.
 *
 *   wf_dk_white    the 14-bit white of an ISO when the shot's own raw shows no clip pile-up (wf_dev_levels)
 *   wf_dk_wb_pick  which of the CR2's WB levels the develop takes: Light Auto = Canon's Auto WB levels (owner 11-17)
 *   wf_dk_scales   the develop's channel scales (struct wf_dev_job wb_q8) from the CR2: Canon's WB levels
 *                  (MakerNote ColorData) for the colour, the fixed scene scale + a per-ISO trim for the exposure
 *   wf_dk_app1     the Exif APP1 segment in the head of Canon's own JPEG of the shot (copied into the print)
 *   wf_dk_comment  that APP1's UserComment (0x9286) overwritten in place with "ASCII\0\0\0" + our line
 *   wf_dk_thumb_*  the darkroom screen's print (WF_DR_PW x WF_DR_PH luma) and wire (WF_DR_TW x WF_DR_TH) thumbnails,
 *                  box means of Y gathered from the developed rows as they come */
#ifndef WF_DARKP_H
#define WF_DARKP_H
#include <stdint.h>

enum { WF_DK_EOSM = 0, WF_DK_5D2 = 1 };

/* the Darkroom's frame in a camera's CR2 (DESIGN 3.6, 3.18). EOS M: the Day09 pairs (CORE-NOTES 10). 5D2: the owner's
 * Day 10 + 100EOS5D RAW+L pairs (11-17): sensor image 5792 x 3804 (pitch 10136, 4 components, slices 2 x 1936 + 1920),
 * Canon's L JPEG 5616 x 3744 at (168, 56) by correlation on every pair, GBRG there, black 1024 in the masked columns, the
 * image data from column 158 / row 52 (row 0 is a bright line). wf_dev develops widths that are a multiple of 32, so the
 * 5D2 develop covers 5632 columns from 152 and the print takes columns 16 .. 5631 of each developed row (wf_dev_rows_win):
 * its reads reach 8 columns past the window (WF_DV_M), so the window ends at 5784 and the row's bytes (pitch 10136) hold
 * them; columns 152 .. 157 are masked (dark) and only feed develop columns the print never takes. */
struct wf_dk_geom {
    int w, h;                                    /* the print = Canon's L JPEG */
    int x0, y0;                                  /* its corner in the CR2's sensor image */
    int dw, dx;                                  /* the develop's width (a multiple of 32), the print's first column in it */
    int ax0, ay0;                                /* image data from here (wf_dev_job ax0 / ay0; ax1 / ay1 = the CR2's size) */
    int cfa;                                     /* Bayer phase at (x0 - dx, y0) (wf_dev_job cfa) */
    int bx0, bx1;                                /* masked columns for the black (wf_dev_levels) */
    int black;                                   /* the black when those columns say nothing */
    int sw, sh, pitch;                           /* the CR2's sensor image (ML packed 14-bit): the raw buffer */
};
#define WF_DK_GEOM_EOSM { 5184, 3456, 84, 64, 5184, 0, 72, 52, 0, 16, 64, 2048, 5280, 3528, 9240 }
#define WF_DK_GEOM_5D2  { 5616, 3744, 168, 56, 5632, 16, 152, 52, 2, 16, 144, 1024, 5792, 3804, 10136 }
const struct wf_dk_geom *wf_dk_geom(int cam);    /* WF_DK_EOSM / WF_DK_5D2 (anything else: the EOS M's) */

int wf_dk_white(int cam, int iso);

/* the WB levels of a print (owner 11-17: "light = auto -> auto WB, Day / Cloudy / Shade / Tungsten as before"):
 * want_auto (the run's Light is Auto) with the CR2's Auto levels (wf_cr2_info.wb_auto) there = Canon's Auto WB for the
 * shot; else the as-shot levels (wf_cr2_info.wb), which hold Day / Cloudy / Tungsten (the Light row set the camera's WB
 * to them at the shot) or, without a Light, the camera's own WB. *use = the levels for wf_dk_scales. Returns WF_DKWB_*:
 * AUTO, ASSHOT, or NOAUTO (Auto asked, the CR2 has no Auto levels: as shot) */
enum { WF_DKWB_ASSHOT = 0, WF_DKWB_AUTO, WF_DKWB_NOAUTO };
int wf_dk_wb_pick(const int wb[4], const int wb_auto[4], int want_auto, const int **use);

/* m: raw unit -> linear Q24, x 256 (wf_dev.h). wb = the CR2's RGGB levels (wf_dk_wb_pick: as shot or Auto; 0 = none:
 * the camera's daylight). Exposure: the raw's clip (white - black) at 0.18 x 2^(3.3 + trim) like wf_dev.c's default, trim
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

/* the shot's own details for the print finishes (wf_print.h): IFD0 Model / Orientation, Exif ExposureTime, FNumber, ISO,
   DateTimeOriginal, FocalLength, LensModel (none in the 5D2's Exif: then Canon's MakerNote 0x0095, unless it only says
   the focal length) */
struct wf_dk_shot {
    char     model[32], lens[64];                /* "" = none */
    uint32_t fl[2], fn[2], et[2];                /* focal length, f-number, exposure time as num / den (den 0 = none) */
    int      iso;                                /* 0 = none */
    int      orient;                             /* Exif Orientation 1..8, 0 = none */
    char     dt[20];                             /* DateTimeOriginal "YYYY:MM:DD HH:MM:SS", "" = none */
};
enum { WF_DKS_MODEL = 1, WF_DKS_LENS = 2, WF_DKS_FL = 4, WF_DKS_FN = 8, WF_DKS_ET = 16, WF_DKS_ISO = 32,
       WF_DKS_ORIENT = 64, WF_DKS_DATE = 128 };
/* t / tn = a TIFF from its "II" / "MM" header: Canon's APP1 + 10 (after "Exif\0\0"), or a CR2's head. Only the fields
   still empty in s are filled (the APP1 first, then the CR2 for the gaps). Returns the WF_DKS_* bits filled now. */
int wf_dk_shot(const uint8_t *t, uint32_t tn, struct wf_dk_shot *s);
/* the fields still empty in s from c (the CR2's own, read at load while its head is in memory). Not the orientation:
   the print turns by its APP1's (Canon's JPEG), so only that one says how the viewer shows it. */
void wf_dk_shot_gaps(struct wf_dk_shot *s, const struct wf_dk_shot *c);
/* the APP1's Exif PixelXDimension / PixelYDimension (0xA002 / 0xA003) set to w x h in place (SHORT or LONG; a SHORT
   that cannot hold the value is left). 0 = both set, -1 = not (no Exif IFD, a tag missing or too small) */
int wf_dk_dims(uint8_t *app1, uint32_t len, int w, int h);

/* the Exif thumbnail (IFD1 JPEGInterchangeFormat 0x0201 / Length 0x0202) of the APP1 (from its marker, len bytes):
   *off = where its bytes start in app1, *room = what a new one may take there: to the APP1's end when only zeros (or at
   most 3 bytes) follow it (Canon's sits at the APP1's end, 16..20 KB), else its own length. 0 = found */
int wf_dk_xthumb_slot(const uint8_t *app1, uint32_t len, uint32_t *off, uint32_t *room);
/* jpg (n bytes, a JPEG; an APP0 after its SOI is dropped) into that slot, IFD1's length set, the rest of the slot zeroed.
   0 = done, -1 = no slot / does not fit / not a JPEG (the APP1 is left as it was) */
int wf_dk_xthumb_put(uint8_t *app1, uint32_t len, const uint8_t *jpg, uint32_t n);

/* the Exif thumbnail's picture (owner 11-16: Canon's playback and index show the print, not the camera's JPEG): the
   canvas (cw x ch as stored) fitted into WF_DK_XW x WF_DK_XH with black bars like Canon's thumbnail, box means gathered
   from every 2nd row of the encoder's bands as they come (Y per pixel, U / V per word). out: WF_DK_XW / 2 x WF_DK_XH
   UYVY words, filled black by _begin and row by row by _rows. */
#define WF_DK_XW 160
#define WF_DK_XH 120
#define WF_DK_XBYTES (WF_DK_XW * WF_DK_XH * 2)
struct wf_dk_xth {
    int cw, ch, tw, th, ox, oy;                  /* canvas; the picture's size and place in the thumbnail */
    int row, y1, n;                              /* thumbnail row being summed, the canvas row it ends at, rows summed */
    uint16_t xb[WF_DK_XW + 1];                   /* the canvas word each thumbnail column starts at */
    int32_t sy[WF_DK_XW], su[WF_DK_XW], sv[WF_DK_XW];
};
void wf_dk_xth_begin(struct wf_dk_xth *t, int cw, int ch, uint32_t *out);
void wf_dk_xth_rows(struct wf_dk_xth *t, const uint32_t *rows, int pitch, int y, int n, uint32_t *out);
int  wf_dk_xth_done(const struct wf_dk_xth *t);   /* 1 = every row of the picture written */

#define WF_DK_VW  336                            /* = WF_PRV_W / H / SW / SH (wf_dark.h) */
#define WF_DK_VH  224
#define WF_DK_VSW 84
#define WF_DK_VSH 56
/* the Prints screens' picture (modernUI M5, wf_dark.h WF_PRV_*): the canvas fitted into WF_DK_VW x WF_DK_VH as the Exif
   thumbnail above (box means of every 2nd row, black bars), and its contact sheet picture (4 x 4 means of it) */
struct wf_dk_vth {
    int cw, ch, tw, th, ox, oy;
    int row, y1, n;
    uint16_t xb[WF_DK_VW + 1];
    int32_t sy[WF_DK_VW], su[WF_DK_VW], sv[WF_DK_VW];
};
void wf_dk_vth_begin(struct wf_dk_vth *t, int cw, int ch, uint32_t *out);
void wf_dk_vth_rows(struct wf_dk_vth *t, const uint32_t *rows, int pitch, int y, int n, uint32_t *out);
int  wf_dk_vth_done(const struct wf_dk_vth *t);
void wf_dk_vth_sheet(const uint32_t *pic, uint32_t *sheet);   /* WF_DK_VW x WF_DK_VH -> WF_DK_VSW x WF_DK_VSH words */

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
