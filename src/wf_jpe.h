/* wf_jpe.h - WindyFilm baseline JPEG encoder (WindyStrife's own code, written from ITU-T T.81: baseline sequential
 * DCT, Huffman coding; the Annex K example quantisation and Huffman tables).
 *
 * The Darkroom (docs/DESIGN.md 3.6) develops a CR2 outside Canon's pipeline, so Canon's JPEG encoder is not in the path:
 * this encoder writes the print. Input = WindyFilm UYVY words exactly as wf_dev_rows() writes them (and as Canon's YUV
 * frame holds them): little-endian word = byte 0 U (int8, Cb - 128), byte 1 Y0 (0..255), byte 2 V (int8, Cr - 128),
 * byte 3 Y1; full-range BT.601 Y'CbCr (JFIF). Output = one baseline JFIF / Exif JPEG, 4:2:2 like Canon's JPEG L
 * (SOF0, Y 2x1, Cb 1x1, Cr 1x1, MCU 16 x 8), 8-bit, no restart markers.
 *
 * Streaming: the caller feeds bands of rows (wf_jpe_rows, 8 rows or a multiple, the frame's last band may be shorter)
 * and gets the bytes through out(): before each 8 x 8 block the encoder makes sure obuf has 512 bytes free (a block
 * takes at most 416), so out() gets chunks of obuf_n - 512 .. obuf_n bytes, and the rest at wf_jpe_end. A caller that
 * wants fixed-size card writes buffers in out(). No allocation, no file access, no log: the state (wf_jpe_bytes(),
 * 3748 B) and obuf are the caller's. Edges: a width that is not a multiple of 16 and a height that is not a multiple of
 * 8 are padded by repeating the last pixel / row. The bytes do not depend on the band heights or obuf_n (host test).
 *
 * DCT: a separable 8-point scaled DCT (even part 1 multiply, odd part 4), 12-bit constants, 2 fraction bits kept
 * between the passes; the per-coefficient scale of the scaled outputs is folded into the quantiser reciprocals and the
 * quantiser into the column pass, so each coefficient costs one multiply-add and a shift. Camera rules: integer only,
 * armv5t, no LDRD / STRD, no 64-bit division (imports: __aeabi_idiv / __aeabi_uidiv, at begin only).
 * Speed (memsim, ARM946E-S 200 MHz, EOS M 5184 x 3456 at quality 95): see tools/dr_jpe_check.sh. */
#ifndef WF_JPE_H
#define WF_JPE_H
#include <stdint.h>
#include <stddef.h>

enum { WF_JPE_OK = 0, WF_JPE_E_ARG = -1, WF_JPE_E_OUT = -2, WF_JPE_E_ROWS = -3, WF_JPE_E_STATE = -4 };

typedef int (*wf_jpe_out_fn)(void *ctx, const uint8_t *p, uint32_t n);   /* 0 = taken, < 0 = error (encoder stops) */

struct wf_jpe;                                   /* opaque; wf_jpe_bytes() bytes, 4-byte aligned, the caller's */

size_t wf_jpe_bytes(void);

/* Start a w x h image (w even, 2..65534; h 1..65535), quality 1..100 (the Annex K tables scaled: 50 = as printed,
 * 100 = all ones). app1 = a whole APP1 segment from its marker (0xFF 0xE1, length field = app1_n - 2; e.g. Canon's Exif
 * copied from the shot's own JPEG), or 0 for a JFIF APP0 instead. obuf / obuf_n (>= 1024): the output buffer, handed to
 * out() whenever it is full and at wf_jpe_end. Writes SOI, APP1 / APP0, DQT, SOF0, DHT, SOS into obuf.
 * Returns WF_JPE_OK or a WF_JPE_E_* code (then the encoder is unusable). */
int wf_jpe_begin(struct wf_jpe *e, int w, int h, int quality, const uint8_t *app1, int app1_n,
                 uint8_t *obuf, uint32_t obuf_n, wf_jpe_out_fn out, void *ctx);

/* The next nrows rows, top to bottom: row r at (const uint8_t *)uyvy + r * pitch_bytes, w / 2 words. nrows must be a
 * multiple of 8 unless the band ends the frame. Returns WF_JPE_OK or an error (also every later call). */
int wf_jpe_rows(struct wf_jpe *e, const uint32_t *uyvy, int pitch_bytes, int nrows);

/* After the last row: pad the bits with 1s, EOI, hand the rest of obuf to out(). Returns WF_JPE_OK or an error. */
int wf_jpe_end(struct wf_jpe *e);

uint32_t wf_jpe_written(const struct wf_jpe *e); /* bytes produced so far (all of them after wf_jpe_end) */
int      wf_jpe_rows_done(const struct wf_jpe *e);

#endif
