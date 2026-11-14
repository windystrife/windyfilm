/* wf_jpe.c - WindyFilm baseline JPEG encoder (WindyStrife's own code from ITU-T T.81; see wf_jpe.h).
 *
 * Per MCU (16 x 8 px of UYVY words): gather Y left / Y right / Cb / Cr into four 8 x 8 int32 blocks (level shift for
 * Y; the chroma bytes are already signed), forward DCT, quantise in zig-zag order, Huffman-code (DC difference, AC
 * run / size, ZRL, EOB) into a 32-bit bit buffer with 0xFF stuffing, bytes into the caller's obuf.
 *
 * DCT (own derivation, checked against the defining sum in tests/host/wf_jpe_test.c): with a_k = x_k + x_(7-k),
 * b_k = x_k - x_(7-k), c0 = a0 + a3, c1 = a1 + a2, e = a0 - a3, f = a1 - a2 and C_k = cos(k pi / 16), the scaled outputs
 *   o0 = c0 + c1, o4 = c0 - c1, o2 = e + C4 (e + f), o6 = e - C4 (e + f),
 *   z3 = C4 (b1 + b2), z5 = C6 ((b3 + b2) - (b1 + b0)), z2 = (C2 - C6)(b3 + b2) + z5, z4 = (C2 + C6)(b1 + b0) + z5,
 *   o1 = b0 + z3 + z4, o7 = b0 + z3 - z4, o5 = b0 - z3 + z2, o3 = b0 - z3 - z2
 * equal Y_u = sum_k x_k cos((2k + 1) u pi / 16) times 1 / S_u, S = {1, .5098, .5412, .6013, .7071, .9000, 1.3066,
 * 2.5629}. JPEG's coefficient F(u, v) = C(u) C(v) / 4 Y(u, v) (C(0) = 1 / sqrt 2), so F = o(u, v) a_u a_v with
 * a_u = S_u C(u) / 2 (WF_A below, Q15). The quantiser reciprocal of coefficient (u, v) is a_u a_v 2^18 / (4 Q):
 * q = (|o| r + 2^17) >> 18, |o| r < 2^29 for every 8-bit block, so the product never leaves 32 bits. */
#include "wf_jpe.h"

#define FC4    2896                              /* Q12: C4 */
#define FC6    1567                              /* C6 */
#define FC2M6  2217                              /* C2 - C6 */
#define FC2P6  5352                              /* C2 + C6 */

struct wf_jpe {
    int       w, h, mcux, rows, err, ended;
    uint32_t  total;                             /* bytes handed to out() */
    uint8_t  *ob;
    uint32_t  on, op;
    wf_jpe_out_fn out;
    void     *ctx;
    uint32_t  bb;                                /* bit buffer: the low bc bits are pending */
    int       bc;
    int       dc[3];                             /* DC predictors Y, Cb, Cr */
    int32_t   rq[2][64];                         /* quantiser reciprocals (natural order), Q18, incl. the DCT scale */
    uint32_t  hdc[2][12];                        /* code << 8 | len */
    uint32_t  hac[2][256];
    int32_t   blk[4][64];
};

/* ------------------------------------------------------------------ T.81 Annex K tables */
static const uint8_t zz[64] = {                  /* zig-zag index -> natural index */
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47,
    55, 62, 63 };
static const uint8_t qbase[2][64] = {            /* K.1 luminance, K.2 chrominance (natural order) */
    { 16, 11, 10, 16, 24, 40, 51, 61,   12, 12, 14, 19, 26, 58, 60, 55,   14, 13, 16, 24, 40, 57, 69, 56,
      14, 17, 22, 29, 51, 87, 80, 62,   18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
      49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 },
    { 17, 18, 24, 47, 99, 99, 99, 99,   18, 21, 26, 66, 99, 99, 99, 99,   24, 26, 56, 99, 99, 99, 99, 99,
      47, 66, 99, 99, 99, 99, 99, 99,   99, 99, 99, 99, 99, 99, 99, 99,   99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99,   99, 99, 99, 99, 99, 99, 99, 99 } };
static const uint8_t dc_bits[2][16] = { { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },     /* K.3 */
                                        { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 } };   /* K.4 */
static const uint8_t dc_val[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t ac_bits[2][16] = { { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d },  /* K.5 */
                                        { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 } }; /* K.6 */
static const uint8_t ac_val[2][162] = {
    { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
      0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
      0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
      0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
      0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
      0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
      0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
      0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa },
    { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa } };
static const uint32_t WF_A[8] = { 11585, 8352, 8867, 9852, 11585, 14745, 21407, 41991 };   /* a_u, Q15 */

size_t wf_jpe_bytes(void) { return sizeof(struct wf_jpe); }
uint32_t wf_jpe_written(const struct wf_jpe *e) { return e->total + e->op; }
int wf_jpe_rows_done(const struct wf_jpe *e) { return e->rows; }

/* ------------------------------------------------------------------ bytes out */
/* obuf is never left full: a byte is stored, then a full buffer goes to out(). After an error out() is not called
   again and the bytes are dropped (op wraps to 0), so a later byte never runs past obuf. */
static int flush(struct wf_jpe *e)
{
    if (e->err) { e->op = 0; return e->err; }
    if (e->op)
    {
        if (e->out(e->ctx, e->ob, e->op) < 0) { e->err = WF_JPE_E_OUT; e->op = 0; return e->err; }
        e->total += e->op;
        e->op = 0;
    }
    return 0;
}

static void __attribute__((noinline)) obuf_full(struct wf_jpe *e, uint32_t op)
{
    e->op = op;
    flush(e);
}

static void put8(struct wf_jpe *e, int b)
{
    e->ob[e->op++] = (uint8_t)b;
    if (e->op >= e->on) flush(e);
}
static void put16(struct wf_jpe *e, int v) { put8(e, v >> 8); put8(e, v & 0xFF); }

/* ------------------------------------------------------------------ set-up */
static void huff_codes(uint32_t *tab, const uint8_t *bits, const uint8_t *val)
{
    uint32_t code = 0;
    int k = 0;
    for (int len = 1; len <= 16; len++)
    {
        for (int i = 0; i < bits[len - 1]; i++, k++, code++) tab[val[k]] = code << 8 | (uint32_t)len;
        code <<= 1;
    }
}

int wf_jpe_begin(struct wf_jpe *e, int w, int h, int quality, const uint8_t *app1, int app1_n,
                 uint8_t *obuf, uint32_t obuf_n, wf_jpe_out_fn out, void *ctx)
{
    if (!e) return WF_JPE_E_ARG;
    for (uint32_t *z = (uint32_t *)e, *ze = (uint32_t *)(e + 1); z < ze; z++) *z = 0;   /* no libc in the module */
    e->err = WF_JPE_E_ARG;
    if (w < 2 || w > 65534 || (w & 1) || h < 1 || h > 65535 || !obuf || obuf_n < 1024 || !out) return WF_JPE_E_ARG;
    if (app1 && (app1_n < 4 || app1_n > 65537 || app1[0] != 0xFF || app1[1] != 0xE1 ||
                 (app1[2] << 8 | app1[3]) != app1_n - 2)) return WF_JPE_E_ARG;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    e->err = 0;
    e->w = w;
    e->h = h;
    e->mcux = (w + 15) >> 4;
    e->ob = obuf;
    e->on = obuf_n;
    e->out = out;
    e->ctx = ctx;

    int s = quality < 50 ? 5000 / quality : 200 - 2 * quality;
    uint8_t qt[2][64];
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < 64; k++)
        {
            int n = zz[k];
            int q = (qbase[t][n] * s + 50) / 100;
            if (q < 1) q = 1;
            if (q > 255) q = 255;
            qt[t][n] = (uint8_t)q;
            uint32_t kq = (WF_A[n >> 3] * WF_A[n & 7] + (1u << 13)) >> 14;     /* a_u a_v, Q16 */
            e->rq[t][n] = (int32_t)((kq + (uint32_t)q / 2) / (uint32_t)q);    /* a_u a_v 2^18 / (4 q) */
        }
    for (int t = 0; t < 2; t++)
    {
        huff_codes(e->hdc[t], dc_bits[t], dc_val);
        huff_codes(e->hac[t], ac_bits[t], ac_val[t]);
    }

    put16(e, 0xFFD8);                                         /* SOI */
    if (app1)
        for (int i = 0; i < app1_n; i++) put8(e, app1[i]);
    else
    {
        static const uint8_t jfif[18] = { 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 };
        for (int i = 0; i < 18; i++) put8(e, jfif[i]);
    }
    put16(e, 0xFFDB);                                         /* DQT: both tables, 8-bit, zig-zag order */
    put16(e, 2 + 2 * 65);
    for (int t = 0; t < 2; t++)
    {
        put8(e, t);
        for (int k = 0; k < 64; k++) put8(e, qt[t][zz[k]]);
    }
    put16(e, 0xFFC0);                                         /* SOF0: 8-bit, h x w, Y 2x1 / Cb 1x1 / Cr 1x1 */
    put16(e, 17);
    put8(e, 8);
    put16(e, h);
    put16(e, w);
    put8(e, 3);
    put8(e, 1); put8(e, 0x21); put8(e, 0);
    put8(e, 2); put8(e, 0x11); put8(e, 1);
    put8(e, 3); put8(e, 0x11); put8(e, 1);
    put16(e, 0xFFC4);                                         /* DHT: DC 0, AC 0, DC 1, AC 1 */
    put16(e, 2 + 4 * 17 + 2 * 12 + 2 * 162);
    for (int t = 0; t < 2; t++)
    {
        put8(e, t);
        for (int i = 0; i < 16; i++) put8(e, dc_bits[t][i]);
        for (int i = 0; i < 12; i++) put8(e, dc_val[i]);
        put8(e, 0x10 | t);
        for (int i = 0; i < 16; i++) put8(e, ac_bits[t][i]);
        for (int i = 0; i < 162; i++) put8(e, ac_val[t][i]);
    }
    put16(e, 0xFFDA);                                         /* SOS: 3 components, 0..63, Ah Al 0 */
    put16(e, 12);
    put8(e, 3);
    put8(e, 1); put8(e, 0x00);
    put8(e, 2); put8(e, 0x11);
    put8(e, 3); put8(e, 0x11);
    put8(e, 0); put8(e, 63); put8(e, 0);
    return e->err;
}

/* ------------------------------------------------------------------ DCT (scaled; natural order out) */
/* one row of 8 samples into o[0..7] with 2 fraction bits; dco = 8 x the level shift (Y: 1024, the DCT of a constant
   only touches o0, so the shift costs one subtract per row instead of one per sample) */
static inline void row8(int32_t *o, int32_t x0, int32_t x1, int32_t x2, int32_t x3, int32_t x4, int32_t x5,
                        int32_t x6, int32_t x7, int32_t dco)
{
    int32_t a0 = x0 + x7, b0 = x0 - x7;
    int32_t a1 = x1 + x6, b1 = x1 - x6;
    int32_t a2 = x2 + x5, b2 = x2 - x5;
    int32_t a3 = x3 + x4, b3 = x3 - x4;
    int32_t c0 = a0 + a3, c1 = a1 + a2, ee = a0 - a3, f = a1 - a2;
    o[0] = (c0 + c1 - dco) << 2;
    o[4] = (c0 - c1) << 2;
    int32_t z1 = ((ee + f) * FC4 + 512) >> 10;
    o[2] = (ee << 2) + z1;
    o[6] = (ee << 2) - z1;
    int32_t t10 = b3 + b2, t12 = b1 + b0;
    int32_t z3 = ((b2 + b1) * FC4 + 512) >> 10;
    int32_t z11 = (b0 << 2) + z3, z13 = (b0 << 2) - z3;
    int32_t z5 = (t10 - t12) * FC6 + 512;
    int32_t z2 = (t10 * FC2M6 + z5) >> 10;
    int32_t z4 = (t12 * FC2P6 + z5) >> 10;
    o[1] = z11 + z4;
    o[7] = z11 - z4;
    o[5] = z13 + z2;
    o[3] = z13 - z2;
}

static const uint8_t izz[64] = {                 /* natural index -> zig-zag index */
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42, 3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60, 21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57,
    58, 62, 63 };

/* column DCTs fused with the quantiser: d = the block after the row pass, q = the quantised block in zig-zag order.
   q = (o r + 2^17) >> 18, signed (|o r| < 2^29). */
#define QZ(u, o) q[iz[(u) * 8]] = ((o) * r[(u) * 8] + 0x20000) >> 18
static void colq(const int32_t *d, const int32_t *rq, int32_t *q)
{
    const int32_t *p = d;
    for (int j = 0; j < 8; j++, p++)
    {
        const int32_t *r = rq + j;
        const uint8_t *iz = izz + j;
        int32_t a0 = p[0] + p[56], b0 = p[0] - p[56];
        int32_t a1 = p[8] + p[48], b1 = p[8] - p[48];
        int32_t a2 = p[16] + p[40], b2 = p[16] - p[40];
        int32_t a3 = p[24] + p[32], b3 = p[24] - p[32];
        int32_t c0 = a0 + a3, c1 = a1 + a2, ee = a0 - a3, f = a1 - a2;
        QZ(0, c0 + c1);
        QZ(4, c0 - c1);
        int32_t z1 = ((ee + f) * FC4 + 2048) >> 12;
        QZ(2, ee + z1);
        QZ(6, ee - z1);
        int32_t t10 = b3 + b2, t12 = b1 + b0;
        int32_t z3 = ((b2 + b1) * FC4 + 2048) >> 12;
        int32_t z11 = b0 + z3, z13 = b0 - z3;
        int32_t z5 = (t10 - t12) * FC6 + 2048;
        int32_t z2 = (t10 * FC2M6 + z5) >> 12;
        int32_t z4 = (t12 * FC2P6 + z5) >> 12;
        QZ(1, z11 + z4);
        QZ(7, z11 - z4);
        QZ(5, z13 + z2);
        QZ(3, z13 - z2);
    }
}

/* ------------------------------------------------------------------ one block: Huffman */
/* Bytes go into obuf without a bound check: enc_block makes room for a whole block first (a block is at most
   22 + 63 x 26 bits = 208 bytes, 416 with 0xFF stuffing; obuf_n >= 1024 by wf_jpe_begin). */
#define WF_JPE_ROOM 512
#define EMIT(b) (ob[op++] = (uint8_t)(b))
#define PUT(code, len) do { bb = bb << (len) | (code); bc += (len); \
    while (bc >= 8) { bc -= 8; uint32_t by_ = (bb >> bc) & 0xFF; EMIT(by_); if (by_ == 0xFF) EMIT(0); } } while (0)

/* q = the quantised block in zig-zag order; t = table set 0 luma / 1 chroma; dcp = this component's predictor.
   A value past the baseline range (only near quality 100, by the DCT's rounding) is clamped: DC -1024..1023, AC
   +-1023. Magnitude bits: v for v > 0, v - 1 for v < 0, the low s bits (s = bit length of |v|). */
static void enc_block(struct wf_jpe *e, int32_t *q, int t, int *dcp)
{
    uint8_t *ob = e->ob;
    uint32_t op = e->op, bb = e->bb;
    int bc = e->bc;
    if (e->on - op < WF_JPE_ROOM) { obuf_full(e, op); op = 0; }
    const uint32_t *hac = e->hac[t];
    int last = 63;
    while (last && !q[last]) last--;
    {
        int32_t v0 = q[0];
        if (v0 > 1023) v0 = 1023;
        if (v0 < -1024) v0 = -1024;
        int32_t d = v0 - *dcp;
        *dcp = v0;
        if (d)
        {
            int32_t sg = d >> 31;
            int c = __builtin_clz((uint32_t)((d ^ sg) - sg));
            int s = 32 - c;
            uint32_t hc = e->hdc[t][s];                      /* <= 11 + 11 bits: one put */
            PUT((hc >> 8) << s | ((uint32_t)(d + sg) << c) >> c, (int)(hc & 0xFF) + s);
        }
        else
        {
            uint32_t hc = e->hdc[t][0];
            PUT(hc >> 8, (int)(hc & 0xFF));
        }
    }
    int run = 0;                                              /* zeros before the next value, x 16 */
    const int32_t *qp = q + 1, *qe = q + last;
    for (; qp <= qe; qp++)
    {
        int32_t v = *qp;
        if (!v) { run += 16; continue; }
        if (run >= 256)
        {
            uint32_t hz = hac[0xF0];
            do { PUT(hz >> 8, (int)(hz & 0xFF)); run -= 256; } while (run >= 256);
        }
        int32_t sg = v >> 31;
        int c = __builtin_clz((uint32_t)((v ^ sg) - sg));
        int s = 32 - c;
        if (s > 10) { s = 10; c = 22; v = sg ? -1023 : 1023; }
        uint32_t mag = ((uint32_t)(v + sg) << c) >> c;
        uint32_t hc = hac[run + s];
        int len = (int)(hc & 0xFF) + s;
        if (len <= 24) PUT((hc >> 8) << s | mag, len);
        else { PUT(hc >> 8, len - s); PUT(mag, s); }
        run = 0;
    }
    if (last < 63)
    {
        uint32_t he = hac[0x00];
        PUT(he >> 8, (int)(he & 0xFF));
    }
    e->op = op;
    e->bb = bb;
    e->bc = bc;
}

/* ------------------------------------------------------------------ rows */
/* one MCU row: rp[r] = the 8 source rows (the last ones repeated at the frame's bottom). The words are read byte by
   byte straight into the row DCTs (U s8, Y0, V s8, Y1): no gather pass. */
static int mcu_row(struct wf_jpe *e, const uint32_t *const rp[8])
{
    int nw = e->w >> 1;
    int32_t q[64];
    for (int mx = 0; mx < e->mcux; mx++)
    {
        int x0 = mx << 3, cnt = nw - x0;
        int32_t *yl = e->blk[0], *yr = e->blk[1], *cb = e->blk[2], *cr = e->blk[3];
        for (int r = 0; r < 8; r++, yl += 8, yr += 8, cb += 8, cr += 8)
        {
            const uint32_t *p = rp[r] + x0;
            uint32_t tmp[8];
            if (cnt < 8)                                       /* right edge: repeat the last pixel */
            {
                uint32_t L = p[cnt - 1], P = (L & 0xFF00FFFFu) | ((L >> 24) << 8);
                for (int j = 0; j < 8; j++) tmp[j] = j < cnt ? p[j] : P;
                p = tmp;
            }
            const uint8_t *u8 = (const uint8_t *)p;
            const int8_t *s8 = (const int8_t *)p;
            row8(yl, u8[1], u8[3], u8[5], u8[7], u8[9], u8[11], u8[13], u8[15], 1024);
            row8(yr, u8[17], u8[19], u8[21], u8[23], u8[25], u8[27], u8[29], u8[31], 1024);
            row8(cb, s8[0], s8[4], s8[8], s8[12], s8[16], s8[20], s8[24], s8[28], 0);
            row8(cr, s8[2], s8[6], s8[10], s8[14], s8[18], s8[22], s8[26], s8[30], 0);
        }
        colq(e->blk[0], e->rq[0], q);
        enc_block(e, q, 0, &e->dc[0]);
        colq(e->blk[1], e->rq[0], q);
        enc_block(e, q, 0, &e->dc[0]);
        colq(e->blk[2], e->rq[1], q);
        enc_block(e, q, 1, &e->dc[1]);
        colq(e->blk[3], e->rq[1], q);
        enc_block(e, q, 1, &e->dc[2]);
        if (e->err) return e->err;
    }
    return 0;
}

int wf_jpe_rows(struct wf_jpe *e, const uint32_t *uyvy, int pitch_bytes, int nrows)
{
    if (e->err) return e->err;
    if (e->ended) return e->err = WF_JPE_E_STATE;
    if (!uyvy || nrows <= 0 || e->rows + nrows > e->h || ((nrows & 7) && e->rows + nrows != e->h))
        return e->err = WF_JPE_E_ROWS;
    const uint8_t *base = (const uint8_t *)uyvy;
    for (int y = 0; y < nrows; y += 8)
    {
        const uint32_t *rp[8];
        for (int r = 0; r < 8; r++)
        {
            int yy = y + r < nrows ? y + r : nrows - 1;
            rp[r] = (const uint32_t *)(base + (size_t)yy * pitch_bytes);
        }
        if (mcu_row(e, rp)) return e->err;
    }
    e->rows += nrows;
    return 0;
}

int wf_jpe_end(struct wf_jpe *e)
{
    if (e->err) return e->err;
    if (e->ended || e->rows != e->h) return e->err = WF_JPE_E_STATE;
    if (e->bc)                                                /* pad with 1-bits to the byte */
    {
        uint8_t *ob = e->ob;
        uint32_t op = e->op, bb = e->bb;
        int bc = e->bc, n = 8 - bc;
        PUT((1u << n) - 1, n);
        e->op = op;
        e->bb = bb;
        e->bc = bc;
        if (e->err) return e->err;
    }
    put16(e, 0xFFD9);                                         /* EOI */
    e->ended = 1;
    return flush(e);
}
