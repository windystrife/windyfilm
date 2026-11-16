/* wf_cr2.h - WindyFilm Darkroom: Canon CR2 reader on the camera (WindyStrife's own code).
 *
 * Own code from the public format descriptions (TIFF / EXIF structure, the CR2 header with the raw IFD offset at byte 12,
 * the slice tag 0xC640, the lossless JPEG of ITU-T T.81: SOF3, Huffman, predictor 1), the camera port of the logic of
 * tools/cr2_dec.c (WindyStrife's host tool). Its output is bit-exact with cr2_dec's raw.r14 (tools/dr_cr2_check.sh).
 *
 * Use (the Darkroom task; the caller does all file I/O):
 *   1. read the file head (WF_CR2_HEAD bytes, or the whole file when smaller) -> wf_cr2_parse(). WF_CR2_E_SHORT: read
 *      inf.need bytes and call it again.
 *   2. read a few KB at inf.strip_off -> wf_cr2_parse_ljpeg(): the sensor size (w x h), pitch, the Huffman tables.
 *   3. a raw buffer of pitch x h bytes (EOS M 9240 x 3528 = 32,598,720 B), an input buffer (>= wf_cr2_inbuf_min()),
 *      seek the file to inf.strip_off, wf_cr2_dec_begin(), then wf_cr2_dec_step() until it returns 0 (done) or < 0.
 *      Between steps the caller may sleep, redraw, or stop (just stop calling; nothing to free).
 * Output: the whole sensor image in ML's packed 14-bit layout (raw_pixblock: 8 px in 14 bytes, 7 little-endian 16-bit
 * words), pitch = w / 8 * 14 or more, black not subtracted. Every pixel of the image is written exactly once; nothing
 * outside raw[0 .. pitch x h) is ever written, whatever the file holds.
 *
 * Camera rules: no allocation (the caller passes every buffer), no file access, no log; integer only, armv5t, no LDRD /
 * STRD, no 64-bit division. struct wf_cr2_dec is about 9 KB (the Huffman fast tables). */
#ifndef WF_CR2_H
#define WF_CR2_H
#include <stdint.h>

#define WF_CR2_HEAD     (128 * 1024)    /* head bytes to read first (the EOS M files need 71,888: IFD0..IFD3, EXIF,
                                           MakerNote / ColorData) */
#define WF_CR2_LJHEAD   4096            /* bytes at the strip start for wf_cr2_parse_ljpeg (the EOS M header is 110 B) */
#ifndef WF_CR2_FASTB
#define WF_CR2_FASTB    10              /* Huffman fast table bits (2 tables x 2 KB: the hot set stays in the 8 KB D-cache) */
#endif

enum {
    WF_CR2_OK = 0,
    WF_CR2_E_SHORT = -1,                /* the head is too short: read inf.need bytes and call again */
    WF_CR2_E_NOTCR2 = -2,               /* no TIFF / CR2 header */
    WF_CR2_E_IFD = -3,                  /* an IFD or an offset outside the file */
    WF_CR2_E_NOSTRIP = -4,              /* no raw strip in IFD3 */
    WF_CR2_E_NOSOI = -5,                /* the strip does not start with a JPEG SOI */
    WF_CR2_E_MARKER = -6,               /* a broken marker segment before the scan */
    WF_CR2_E_SOF = -7,                  /* not a lossless (SOF3) frame this reader handles: components 1..4, precision
                                           2..16, predictor 1, no point transform */
    WF_CR2_E_DHT = -8,                  /* a Huffman table that is broken or missing */
    WF_CR2_E_SIZE = -9,                 /* sensor size: width not a multiple of 8, or out of range */
    WF_CR2_E_SLICE = -10,               /* slice widths do not add up to the sensor width */
    WF_CR2_E_PARAM = -11,               /* a caller buffer too small / null */
    WF_CR2_E_DATA = -12,                /* a code not in the Huffman table */
    WF_CR2_E_TRUNC = -13,               /* the strip ended (or hit a marker) before the last row */
    WF_CR2_E_READ = -14,                /* rd() returned an error */
    WF_CR2_E_N = 15
};

struct wf_cr2_info {
    /* wf_cr2_parse: the TIFF part */
    uint32_t fsize;                     /* file size (from the caller) */
    uint32_t need;                      /* WF_CR2_E_SHORT: head bytes needed (else the bytes the IFDs reached) */
    int      iso;                       /* EXIF ISOSpeedRatings, 0 = not given */
    uint32_t et_n, et_d;                /* EXIF ExposureTime (0x829A) et_n / et_d s, 0 0 = not given */
    uint32_t fn_n, fn_d;                /* EXIF FNumber (0x829D) fn_n / fn_d, 0 0 = not given */
    int      wb[4];                     /* Canon's as-shot RGGB levels from ColorData (MakerNote 0x4001), 0 = none */
    int      wb_auto[4];                /* Canon's Auto WB RGGB levels (WB_RGGBLevelsAuto) of the shot, whatever WB it
                                           was taken with: ColorData 3 and later only (EOS M: 1312 entries), 0 = none */
    int      cd_count;                  /* ColorData entries (its version), 0 = none */
    int      orient;                    /* EXIF orientation 1..8 of IFD0, 0 = not given */
    uint32_t strip_off, strip_len;      /* the raw: lossless JPEG in IFD3 */
    int      sl_n, sl_w, sl_last;       /* slices (tag 0xC640): sl_n of width sl_w, then one of sl_last; 0 0 0 = none */
    uint32_t jpg_off, jpg_len;          /* IFD0: Canon's full-size JPEG (jpg_w x jpg_h), 0 = none */
    int      jpg_w, jpg_h;
    uint32_t thumb_off, thumb_len;      /* IFD1: the small JPEG thumbnail, 0 = none */
    uint32_t rgb_off, rgb_len;          /* IFD2: the small uncompressed RGB image (EOS M 660 x 441, 16 bit), 0 = none */
    int      rgb_w, rgb_h, rgb_bits;
    /* wf_cr2_parse_ljpeg: the lossless JPEG header */
    int      w, h, pitch;               /* the sensor image: w = jw x nc, h = jh, packed pitch w / 8 * 14 */
    int      jw, jh, nc, prec;
    uint32_t data_off;                  /* the entropy-coded data, bytes from the strip start */
    uint8_t  tsel[4];                   /* Huffman table of each component */
    uint8_t  hmask;                     /* bit t: table t defined */
    uint8_t  pad[3];
    uint8_t  hbits[4][16];              /* DHT: codes of each length 1..16 */
    uint8_t  hval[4][17];               /* DHT: the symbols (SSSS 0..16) */
};

/* streaming decoder state: caller memory, no pointers into it survive wf_cr2_dec_step */
struct wf_cr2_dec {
    int16_t  fast[4][1 << WF_CR2_FASTB];  /* peek WF_CR2_FASTB bits -> code + extra bits done (bit 4 set: diff << 5 |
                                             16 | bits), or the code length | SSSS << 5, or 0 = a longer code */
    int32_t  maxcode[4][18];            /* canonical slow path: the last code of each length, -1 = none */
    int32_t  valoff[4][18];             /* symbol index of code c of length L = valoff[L] + c */
    uint8_t  hval[4][17];
    uint8_t  tsel[4];
    /* the bit reader: acc holds nb bits, left-aligned */
    uint32_t acc;
    int      nb;
    uint32_t pad;                       /* zero bytes fed for a marker / the strip's end */
    const uint8_t *p, *pe;              /* unread bytes of buf */
    uint8_t *buf;
    uint32_t bufn;
    uint32_t left;                      /* strip bytes not yet read */
    int      eof;
    int (*rd)(void *ctx, void *buf, uint32_t n);
    void    *ctx;
    /* the image */
    uint8_t *raw;
    int      pitch, w, h, nc, mode;     /* mode 16 / 8 / 1: output groups of 16 px (word stores), 8 px, single px */
    int      jrow, jh;                  /* jpeg rows done of jh: the progress */
    int      rowmax;                    /* input bytes kept ahead of a row (worst case of one row + slack) */
    int      last[4];                   /* the previous jpeg row's first sample of each component (row predictor) */
    /* the output cursor (slice order): slice s, row oy, column ox in the slice of width sw starting at x xs */
    int      s, oy, ox, xs, sw, sl_n, sl_w, sl_last;
    int      err;
};

/* the TIFF part from the file head (head = the first nhead bytes of a file of fsize bytes) */
int  wf_cr2_parse(const uint8_t *head, uint32_t nhead, uint32_t fsize, struct wf_cr2_info *inf);
/* the lossless JPEG header from the first n bytes at inf->strip_off; checks the slices against the sensor width */
int  wf_cr2_parse_ljpeg(const uint8_t *p, uint32_t n, struct wf_cr2_info *inf);
const char *wf_cr2_err(int e);          /* "ok", "short head", ... for the log */
uint32_t wf_cr2_inbuf_min(const struct wf_cr2_info *inf);   /* smallest input buffer (2 x the worst row + 2) */

/* rd(ctx, buf, n): the next up to n bytes of the file from the strip start on (the caller seeked to strip_off);
   returns the bytes read, 0 at the end, < 0 for an error. begin reads the first buffer and skips the header. */
int  wf_cr2_dec_begin(struct wf_cr2_dec *d, const struct wf_cr2_info *inf, uint8_t *raw, int pitch, uint8_t *inbuf,
                      uint32_t inbuf_n, int (*rd)(void *ctx, void *buf, uint32_t n), void *ctx);
/* decode up to njrows jpeg rows: > 0 = more to do, 0 = the image is complete, < 0 = WF_CR2_E_* (stays failed) */
int  wf_cr2_dec_step(struct wf_cr2_dec *d, int njrows);

#ifdef WF_CR2_TEST
extern int wf_cr2_force_mode;           /* host test: 8 or 1 forces the slower output paths (same output) */
#endif

#endif
