/* wf_print.h - Darkroom print finishes (docs/DESIGN.md 3.9, owner 2026-11-15), WindyStrife's own code, written from
 * scratch for WindyFilm: a white or black frame with the shot's details under the picture, and a date stamp like a film
 * camera's date back. Pure code (no allocation, no file access, no log, integer only, armv5t, no 64-bit division), so
 * the host test (tests/host/wf_dark_test.c) runs exactly what the camera runs; wf_dark.c does the I/O around it.
 *
 * The print is encoded row-wise (wf_jpe.c). With a frame the canvas is bigger than the picture: wf_pf_rows makes each
 * 16-row band of the canvas on the fly from the frame colour, the picture's own rows and the caption band (rendered
 * once per print), so no downscale and no second full-frame buffer. Every offset and size is a multiple of 16 in the
 * stored image, so the picture's 16 x 8 MCUs are the same blocks as without a frame (its pixels decode the same).
 *
 * Orientation: Canon stores a portrait shot landscape with Exif Orientation 6 / 8; the viewer turns it. The canvas is
 * laid out as the viewer shows it (caption under the picture, the date at its bottom right) and mapped back into the
 * stored image, so the text and the digits are turned with it.
 *
 *   wf_pf_layout      the canvas for a frame / info setting, picture size and orientation
 *   wf_pf_fonts       the print font pack ML/DATA/WFPRINT.DAT (tools/mk_wffont.py --print: Ubuntu Sans, 4-bit
 *                     anti-aliased glyphs at print size; format in that tool's header)
 *   wf_pf_band        the caption band (luma) from the shot's details, the look and the roll, fitted to the width
 *   wf_pf_band_rows   the next encoder band: the picture's own rows when there is no frame (today's bytes), else
 *                     composed into a row buffer (wf_pf_rows)
 *   wf_pf_stamp       the date in orange seven-segment digits with a glow, drawn into the picture (procedural)
 *   wf_pf_line1 / 2, wf_pf_date   caption and date text from struct wf_dk_shot (wf_darkp.h: Exif / CR2 tags) */
#ifndef WF_PRINT_H
#define WF_PRINT_H
#include <stdint.h>
#include "wf_darkp.h"

/* settings (menu / config / Darkroom rows) */
enum { WF_PFR_OFF = 0, WF_PFR_WHITE, WF_PFR_BLACK, WF_PFR_WHITE_W, WF_PFR_BLACK_W, WF_PFR_N };   /* Frame */
enum { WF_PIN_OFF = 0, WF_PIN_ON, WF_PIN_ROLL, WF_PIN_N };                                      /* Info */
enum { WF_PDT_OFF = 0, WF_PDT_YMD, WF_PDT_DMY, WF_PDT_MDY, WF_PDT_N };                           /* Date */

/* sizes at a 3456 px short side (the EOS M L frame), scaled with the short side otherwise; all multiples of 16 */
#define WF_PF_BD_THIN 112                        /* border, about 3 % of the short side */
#define WF_PF_BD_WIDE 256                        /* "wide": about 7 % */
#define WF_PF_CAP     464                        /* the caption under the picture adds this to the bottom border */

struct wf_pf_lay {
    int frame, info, orient;                     /* the settings; Exif orientation 1 / 3 / 6 / 8 (else 1) */
    int iw, ih;                                  /* the picture as stored */
    int cw, ch;                                  /* the canvas as stored (= iw x ih: no frame) */
    int ix, iy;                                  /* the picture in the canvas */
    int dw, dh;                                  /* the canvas as shown (orientation applied) */
    int bd, bb;                                  /* border (sides, top) and bottom band, as shown */
    int tx, ty, tw, th;                          /* the caption band in the stored canvas (tw = 0: none) */
    uint8_t bg, fg1, fg2;                        /* Y of the frame, caption line 1, lines 2 / 3 and the roll */
};
/* 0 = the canvas is the picture (frame Off); 1 = a frame */
int wf_pf_layout(struct wf_pf_lay *L, int frame, int info, int iw, int ih, int orient);

/* ------------------------------------------------------------------ print font pack */
#define WF_PF_NFONT  2                           /* 0: caption line 1 (84 px, medium); 1: lines 2 / 3, roll (62 px) */
#define WF_PF_FIRST  32
#define WF_PF_COUNT  96                          /* ASCII 32..126 + 127 = the middle dot (U+00B7) */
#define WF_PF_DOT    "\x7f"
#define WF_PF_MAXB   (512 * 1024)                /* the biggest pack the darkroom reads */
struct wf_pf_font {
    const uint8_t *b, *g, *k;                    /* pack, glyph table (16 B each), kern table (4 B each) */
    int px, asc, desc, nk;
    uint16_t kidx[WF_PF_COUNT + 1];              /* first kern pair whose left char is WF_PF_FIRST + i */
};
int wf_pf_fonts(struct wf_pf_font f[WF_PF_NFONT], const uint8_t *b, uint32_t n);   /* 0 = ok, -1 = bad pack */
int wf_pf_textw(const struct wf_pf_font *f, const char *s);                         /* px */

/* ------------------------------------------------------------------ caption */
struct wf_pf_cap {
    const char *l1[2]; int n1;                   /* line 1: camera, lens (joined with " / ") */
    const char *l2;                              /* line 2: focal length, f-number, shutter, ISO */
    const char *l3[8]; int n3;                   /* line 3: the film, then print / grain / halation / bloom / mist */
    const char *roll;                            /* right of line 1: "ROLL 021 . 07/24", 0 = none */
};
uint32_t wf_pf_band_bytes(const struct wf_pf_lay *L);   /* tw x th (0: no caption) */
/* the band (luma, tw x th, stored orientation). Items that do not fit the picture's width are left out from the end
   (line 1's lens, then line 3's effects; the film name is cut last). drawn (optional, dn bytes) = what was drawn, for
   the log: "line 1 | line 2 | line 3". F == 0: the band in the frame colour only. */
void wf_pf_band(const struct wf_pf_lay *L, const struct wf_pf_font *F, const struct wf_pf_cap *c, uint8_t *band,
                char *drawn, int dn);

/* canvas rows [cy, cy + n) as UYVY words into out (cw words / 2 per row, pitch cw * 2): the frame colour, the picture's
   rows (img = its row 0, ipitch bytes), the caption band */
void wf_pf_rows(const struct wf_pf_lay *L, int cy, int n, const uint32_t *img, int ipitch, const uint8_t *band,
                uint32_t *out);
/* the encoder's band at canvas row cy (16 rows, or the rest): returns n; *rows / *pitch = the picture's own rows when the
   canvas is the picture (no copy), else rowbuf (cw * 2 * 16 bytes) composed by wf_pf_rows */
int wf_pf_band_rows(const struct wf_pf_lay *L, int cy, const uint32_t *img, int ipitch, const uint8_t *band,
                    uint32_t *rowbuf, const uint32_t **rows, int *pitch);

/* ------------------------------------------------------------------ text from the shot */
int wf_pf_line1(const struct wf_dk_shot *s, char *model, int mn, char *lens, int ln);   /* items found (0..2) */
int wf_pf_line2(const struct wf_dk_shot *s, char *b, int n);   /* "45mm   f/6.3   1/320s   ISO 320"; 0 = nothing known */
int wf_pf_date(const struct wf_dk_shot *s, int fmt, char *b, int n);   /* "'26 11 15" (WF_PDT_*); 0 = no date */
/* caption line 3 from the print's comment text (wf_exif_text: "WindyFilm: Portra 400 | Print Cine | Strength 100% |
   Grain Normal | ..."), split in place: the film first, then its Print (unless the title names it) / Grain / Halation /
   Bloom / Mist items, the rest left out. Returns the items (<= max). */
int wf_pf_look_items(char *txt, const char **it, int max);
/* the whole caption of a print: line 1 / 2 from the shot, line 3 from the look text (copied into t), the roll text
   ("ROLL 021 . 07/24", 0 = none) right of line 1. c points into t. */
struct wf_pf_txt { char model[32], lens[64], l2[64], l3[200], roll[40]; };
void wf_pf_caption(const struct wf_dk_shot *s, const char *look, const char *roll, struct wf_pf_txt *t,
                   struct wf_pf_cap *c);

/* ------------------------------------------------------------------ date stamp */
uint32_t wf_pf_stamp_bytes(int iw, int ih, const char *txt);   /* the glow map it needs */
/* txt ("'26 11 15": digits, ' and spaces) at the shown picture's bottom right, screened in orange over the picture
   (UYVY in place). map = scratch of wf_pf_stamp_bytes(). 0 = drawn, -1 = no room / bad text */
int wf_pf_stamp(uint32_t *img, int ipitch, int iw, int ih, int orient, const char *txt, uint8_t *map, uint32_t mapn);

#endif
