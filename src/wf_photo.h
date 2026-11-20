/* wf_photo.h - the Darkroom's photos (owner RAWB28, 20-11: "Thêm option có thể preview ảnh lúc chọn darkroom"): a
 * frame's picture straight from its CR2, without the RAW. Canon's CR2 keeps a small uncompressed RGB image in IFD2
 * (3 x 16 bits a pixel, little-endian, one strip: EOS M 660 x 441, 5D2 362 x 234; Canon's colour, black near
 * 2048 / 1024, a masked band at the left and the EOS M's top; linear light). Its rows are box-averaged into luma
 * thumbnails, levelled and gamma-encoded.
 * WindyStrife's own code. Pure: the caller gives a reader of the file (wf_dark.c on the camera through FIO,
 * tests/ui/fake_dark.c and tests/host/wf_dark_test.c on the PC through stdio). */
#ifndef WF_PHOTO_H
#define WF_PHOTO_H
#include <stdint.h>

#define WF_PH_CW 46                              /* a contact sheet frame: up to 6 strips of 6 on the easel's paper */
#define WF_PH_CH 30
#define WF_PH_LW 72                              /* a roll of up to 12 frames: 3 strips of 4, bigger */
#define WF_PH_LH 48
#define WF_PH_LMAX 12
#define WF_PH_BW 288                             /* the print sketch's picture (= WF_DR_PW x WF_DR_PH): a landscape */
#define WF_PH_BH 192                             /* frame 288 x 192, a portrait one 128 x 192 */
#define WF_PH_SWMAX 680                          /* the widest small RGB read (EOS M 660) */
#define WF_PH_NROW 8                             /* source rows a read of wf_ph_big */
#define WF_PH_ROWS (WF_PH_NROW * WF_PH_SWMAX * 3)   /* the rows buffer, uint16_t (32640 bytes) */
#define WF_PH_IFDB 512                           /* bytes read at each IFD's offset */

/* one TIFF IFD of the CR2. b holds the file's bytes from offset base (the IFD's own offset: 2 + 12 x entries + 4 bytes,
 * and what it points at right after, 512 bytes are plenty). 0 = read; -1 = not one */
struct wf_ph_ifd {
    uint32_t next;                               /* the next IFD's offset, 0 = none */
    int      orient;                             /* tag 274 (IFD0): 1 as shot, 3 upside down, 6 / 8 turned (0 = none) */
    int      w, h, comp, spp, bps;               /* 256, 257, 259, 277, 258 (bps 0 = not readable from b) */
    uint32_t off, bytes;                         /* 273 / 279: the strip */
};
uint32_t wf_ph_head(const uint8_t *b, uint32_t n);   /* the file's first 8 bytes: IFD0's offset, 0 = not a TIFF "II" */
int  wf_ph_ifd(const uint8_t *b, uint32_t n, uint32_t base, struct wf_ph_ifd *o);
int  wf_ph_rgb_ok(const struct wf_ph_ifd *d);   /* IFD2 is the small RGB: 1 = 3 x 16 bits, uncompressed, one strip */

/* the box average of the source (sw x sh, minus its masked band) into dw x dh 16-bit luma. Rows may come one by one
 * and some may be skipped (the camera reads a few rows of each band): each destination pixel is the mean of what came */
struct wf_ph {
    int      sw, sh, x0, y0, dw, dh;             /* source; its picture starts at (x0, y0); destination */
    int      row, n;                             /* destination row being summed; source rows in it */
    uint32_t bsum, bn;                           /* the masked band's luma: the black */
    uint16_t cx[WF_PH_BW + 1];                   /* source column where each destination column starts */
    uint32_t sum[WF_PH_BW];
    uint16_t *out;
};
/* 0 = ready; -1 = sizes out of range (dw <= WF_PH_BW, dh <= the source's picture) */
int  wf_ph_begin(struct wf_ph *t, int sw, int sh, int dw, int dh, uint16_t *out);
int  wf_ph_band(const struct wf_ph *t, int r);  /* the first source row of destination row r (r = dh: the end) */
/* source rows [y, y + n), sw x 3 samples each, in order (rows before y0 count for the black only) */
void wf_ph_rows(struct wf_ph *t, const uint16_t *rgb, int y, int n);
void wf_ph_end(struct wf_ph *t);                /* the last rows; a row nothing came for copies the one above (or 0) */
int  wf_ph_black(const struct wf_ph *t);        /* the masked band's mean, -1 = no band */
/* the levels of a 16-bit thumbnail (n values): lo = the black (or its 0.5 % point), hi = its 99.5 % point, at least
 * WF_PH_SPAN above lo (a dark frame stays dark: Canon's picture is 12 bits over its black) */
#define WF_PH_SPAN 2048
void wf_ph_levels(const uint16_t *v, int n, int black, int *lo, int *hi);
void wf_ph_to8(const uint16_t *v, uint8_t *o, int n, int lo, int hi);   /* lo..hi of linear light to sRGB 8 bits
                                                                          (o may be v's own bytes) */
/* the picture the right way up (EXIF orientation): 6 / 8 = out is h wide, w tall; 3 = turned over; else a copy */
void wf_ph_turn(const uint8_t *in, int w, int h, int orient, uint8_t *out);

/* ---- one frame from its CR2 through a reader: bytes [pos, pos + n) of the file into dst, 0 = read (all of them) */
typedef int (*wf_ph_rd)(void *ctx, uint32_t pos, void *dst, uint32_t n);
struct wf_ph_src { int w, h, orient; uint32_t off; };   /* the small RGB (IFD2) and the frame's orientation (IFD0) */
/* b: WF_PH_IFDB bytes of scratch. 0 = found; -1 = not a CR2, no small RGB in it, wider than WF_PH_SWMAX, a read failed */
int  wf_ph_find(wf_ph_rd rd, void *ctx, uint8_t *b, struct wf_ph_src *s);
/* a contact sheet frame, cw x ch luma as stored, from 3 rows in the middle of each band (a few reads: the camera reads
 * a whole roll's). t: scratch; rows: WF_PH_ROWS; t16: cw x ch. 0 = done; -1 = a read failed */
int  wf_ph_cell(wf_ph_rd rd, void *ctx, const struct wf_ph_src *s, struct wf_ph *t, int cw, int ch, uint16_t *rows,
                uint16_t *t16, uint8_t *cell);
/* the print sketch's picture from every row, the right way up: *bw x *bh (288 x 192 or 128 x 192). t16: WF_PH_BW x
 * WF_PH_BH; big: as many bytes. 0 = done; -1 = a read failed */
int  wf_ph_big(wf_ph_rd rd, void *ctx, const struct wf_ph_src *s, struct wf_ph *t, uint16_t *rows, uint16_t *t16,
               uint8_t *big, int *bw, int *bh);

#endif
