/* wf_cr2.c - WindyFilm Darkroom: Canon CR2 reader on the camera (WindyStrife's own code). See wf_cr2.h.
 *
 * The lossless JPEG (ITU-T T.81 SOF3, predictor 1): the scan is jh rows of jw x nc samples, components interleaved
 * sample by sample. Sample x of a row is predicted from sample x - nc (the same component, to its left); the first nc
 * samples from the first nc samples of the row above (row 0: 2^(prec - 1)). Canon codes the sensor image in vertical
 * slices (tag 0xC640: sl_n slices of sl_w columns, then one of sl_last): the samples, in scan order, fill slice 0 row by
 * row from the top, then slice 1, and so on. That is tools/cr2_dec.c's mapping; this file does the same without a 16-bit
 * image copy: each sample goes straight into ML's packed 14-bit layout at its slice position.
 *
 * Speed (ARM946E-S): one Huffman lookup of WF_CR2_FASTB bits per sample; the table entry already holds the difference
 * when the code and its extra bits fit in those bits (most samples). The bit buffer is a left-aligned 32-bit register,
 * refilled byte by byte (0xFF 0x00 unstuffing; a marker or the strip's end feeds zero bytes, never advancing). Output:
 * 16 px = 7 words, one burst of word stores per group, when the slice widths allow (EOS M: 2 slices of 2640); 8 px = 7
 * halfwords, or one pixel at a time (read-modify-write) for any other geometry. Input: the caller's buffer keeps at least
 * one worst-case row (31 bits per sample, doubled for stuffing) ahead, so the row loops never check a bound. */
#include "wf_cr2.h"
#include <string.h>

#ifdef WF_CR2_TEST
int wf_cr2_force_mode;
#endif

/* ------------------------------------------------------------------ the TIFF part */
struct rdr { const uint8_t *b; uint32_t n, fsize, need; int le, bad, shortb; };

static uint32_t r16(struct rdr *r, uint32_t o)
{
    if (r->fsize < 2 || o > r->fsize - 2) { r->bad = 1; return 0; }
    if (o + 2 > r->need) r->need = o + 2;
    if (o + 2 > r->n) { r->shortb = 1; return 0; }
    const uint8_t *p = r->b + o;
    return r->le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 : (uint32_t)p[0] << 8 | p[1];
}

static uint32_t r32(struct rdr *r, uint32_t o)
{
    if (r->fsize < 4 || o > r->fsize - 4) { r->bad = 1; return 0; }
    if (o + 4 > r->need) r->need = o + 4;
    if (o + 4 > r->n) { r->shortb = 1; return 0; }
    const uint8_t *p = r->b + o;
    return r->le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24
                 : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* the entry of tag in the IFD at o, 0 = none */
static uint32_t ifd_tag(struct rdr *r, uint32_t o, unsigned tag)
{
    if (!o) return 0;
    uint32_t n = r16(r, o);
    if (n > 1024) { r->bad = 1; return 0; }
    for (uint32_t i = 0; i < n; i++)
        if (r16(r, o + 2 + 12 * i) == tag) return o + 2 + 12 * i;
    return 0;
}

static uint32_t ifd_next(struct rdr *r, uint32_t o)
{
    if (!o) return 0;
    uint32_t n = r16(r, o);
    if (n > 1024) { r->bad = 1; return 0; }
    return r32(r, o + 2 + 12 * n);
}

/* a SHORT / LONG entry with count 1 (or a SHORT pair), else the offset field */
static uint32_t ent_val(struct rdr *r, uint32_t e)
{
    if (!e) return 0;
    uint32_t type = r16(r, e + 2), cnt = r32(r, e + 4);
    if (type == 3 && cnt <= 2) return r16(r, e + 8);
    return r32(r, e + 8);
}

static uint32_t tag_val(struct rdr *r, uint32_t ifd, unsigned tag)
{
    return ent_val(r, ifd_tag(r, ifd, tag));
}

int wf_cr2_parse(const uint8_t *head, uint32_t nhead, uint32_t fsize, struct wf_cr2_info *inf)
{
    if (!head || !inf) return WF_CR2_E_PARAM;
    memset(inf, 0, sizeof *inf);
    inf->fsize = fsize;
    if (nhead > fsize) nhead = fsize;
    if (fsize < 16) return WF_CR2_E_NOTCR2;
    if (nhead < 16) { inf->need = 16; return WF_CR2_E_SHORT; }
    struct rdr r = { head, nhead, fsize, 16, 1, 0, 0 };
    if (head[0] == 'M' && head[1] == 'M') r.le = 0;
    else if (head[0] != 'I' || head[1] != 'I') return WF_CR2_E_NOTCR2;
    if (r16(&r, 2) != 42 || head[8] != 'C' || head[9] != 'R') return WF_CR2_E_NOTCR2;
    uint32_t ifd0 = r32(&r, 4), rawifd = r32(&r, 12);

    /* EXIF: ISO, MakerNote -> ColorData (as-shot WB at short 63 for ColorData4 and later; 25 / 34 / 71 for the 582 /
       653 / 5120 entry versions), as tools/cr2_dec.c; Auto WB at short 68 (ColorData 3 and later) */
    uint32_t e = ifd_tag(&r, ifd0, 0x8769);
    if (e) {
        uint32_t exif = r32(&r, e + 8);
        uint32_t ei = ifd_tag(&r, exif, 0x8827);
        if (ei) inf->iso = (int)r16(&r, ei + 8);
        uint32_t et = ifd_tag(&r, exif, 0x829A), fn = ifd_tag(&r, exif, 0x829D);   /* RATIONALs: the Darkroom's EV */
        if (et && r16(&r, et + 2) == 5) {
            uint32_t o = r32(&r, et + 8);
            inf->et_n = r32(&r, o);
            inf->et_d = r32(&r, o + 4);
        }
        if (fn && r16(&r, fn + 2) == 5) {
            uint32_t o = r32(&r, fn + 8);
            inf->fn_n = r32(&r, o);
            inf->fn_d = r32(&r, o + 4);
        }
        uint32_t em = ifd_tag(&r, exif, 0x927C);
        if (em) {
            uint32_t mn = r32(&r, em + 8);
            uint32_t cd = ifd_tag(&r, mn, 0x4001);
            if (cd) {
                uint32_t cnt = r32(&r, cd + 4), off = r32(&r, cd + 8);
                int idx = cnt == 582 ? 25 : cnt == 653 ? 34 : cnt == 5120 ? 71 : 63;
                inf->cd_count = (int)cnt;
                for (int c = 0; c < 4; c++) inf->wb[c] = (int)r16(&r, off + 2 * (idx + c));
                /* the Auto WB levels (owner 11-17, Light Auto = Auto WB in the Darkroom): 5 shorts after the as-shot
                   ones in ColorData 3 and later (as shot 63, its colour temperature 67, auto 68, the auto colour
                   temperature 72; checked on the owner's EOS M CR2s, 1312 entries, version 10: the auto levels and
                   temperature follow the scene while the as-shot ones stay on the set WB). Older layouts are not read;
                   levels out of range = none */
                if (idx == 63 && cnt >= 73) {
                    int ok = 1;
                    for (int c = 0; c < 4; c++) {
                        inf->wb_auto[c] = (int)r16(&r, off + 2 * (68 + c));
                        if (inf->wb_auto[c] <= 0 || inf->wb_auto[c] >= 16384)
                            ok = 0;
                    }
                    if (!ok)
                        for (int c = 0; c < 4; c++) inf->wb_auto[c] = 0;
                }
            }
        }
    }
    /* IFD0: Canon's full-size JPEG and the orientation; IFD1: thumbnail; IFD2: small RGB */
    inf->orient = (int)tag_val(&r, ifd0, 0x0112);
    inf->jpg_off = tag_val(&r, ifd0, 0x0111);
    inf->jpg_len = tag_val(&r, ifd0, 0x0117);
    inf->jpg_w = (int)tag_val(&r, ifd0, 0x0100);
    inf->jpg_h = (int)tag_val(&r, ifd0, 0x0101);
    uint32_t ifd1 = ifd_next(&r, ifd0);
    if (ifd1 && ifd1 < fsize) {
        inf->thumb_off = tag_val(&r, ifd1, 0x0201);
        inf->thumb_len = tag_val(&r, ifd1, 0x0202);
        uint32_t ifd2 = ifd_next(&r, ifd1);
        if (ifd2 && ifd2 < fsize && ifd2 != rawifd) {
            inf->rgb_w = (int)tag_val(&r, ifd2, 0x0100);
            inf->rgb_h = (int)tag_val(&r, ifd2, 0x0101);
            uint32_t be = ifd_tag(&r, ifd2, 0x0102);
            if (be) inf->rgb_bits = (int)(r32(&r, be + 4) <= 2 ? r16(&r, be + 8) : r16(&r, r32(&r, be + 8)));
            inf->rgb_off = tag_val(&r, ifd2, 0x0111);
            inf->rgb_len = tag_val(&r, ifd2, 0x0117);
        }
    }
    /* IFD3: the raw strip and its slices */
    uint32_t so = ifd_tag(&r, rawifd, 0x0111), sc = ifd_tag(&r, rawifd, 0xC640);
    if (so) inf->strip_off = ent_val(&r, so);
    uint32_t sl = ifd_tag(&r, rawifd, 0x0117);
    inf->strip_len = sl ? ent_val(&r, sl) : 0;
    if (sc) {
        uint32_t o = r32(&r, sc + 8);
        inf->sl_n = (int)r16(&r, o);
        inf->sl_w = (int)r16(&r, o + 2);
        inf->sl_last = (int)r16(&r, o + 4);
    }
    if (r.bad) return WF_CR2_E_IFD;
    if (r.shortb) { inf->need = r.need; return WF_CR2_E_SHORT; }
    inf->need = r.need;
    if (!so || !inf->strip_off || inf->strip_off >= fsize) return WF_CR2_E_NOSTRIP;
    if (!inf->strip_len || inf->strip_len > fsize - inf->strip_off) inf->strip_len = fsize - inf->strip_off;
    return WF_CR2_OK;
}

/* ------------------------------------------------------------------ the lossless JPEG header */
int wf_cr2_parse_ljpeg(const uint8_t *p, uint32_t n, struct wf_cr2_info *inf)
{
    if (!p || !inf) return WF_CR2_E_PARAM;
    if (n < 4) return WF_CR2_E_SHORT;
    if (p[0] != 0xFF || p[1] != 0xD8) return WF_CR2_E_NOSOI;
    uint32_t k = 2;
    int jw = 0, jh = 0, nc = 0, prec = 0, ns = -1;
    inf->hmask = 0;
    for (;;) {
        if (k + 4 > n) return WF_CR2_E_SHORT;
        if (p[k] != 0xFF) return WF_CR2_E_MARKER;
        int m = p[k + 1];
        uint32_t len = (uint32_t)p[k + 2] << 8 | p[k + 3];
        if (len < 2) return WF_CR2_E_MARKER;
        if (k + 2 + len > n) return WF_CR2_E_SHORT;
        const uint8_t *d = p + k + 4;
        uint32_t dn = len - 2;
        if (m == 0xC4) {                                 /* DHT: one or more tables */
            uint32_t q = 0;
            while (q < dn) {
                if (q + 17 > dn) return WF_CR2_E_MARKER;
                int th = d[q] & 15;
                if (th > 3) return WF_CR2_E_DHT;
                int tot = 0;
                for (int i = 0; i < 16; i++) tot += d[q + 1 + i];
                if (tot < 1 || tot > 17) return WF_CR2_E_DHT;  /* lossless: symbols SSSS 0..16 */
                if (q + 17 + tot > dn) return WF_CR2_E_MARKER;
                for (int i = 0; i < 16; i++) inf->hbits[th][i] = d[q + 1 + i];
                for (int i = 0; i < 17; i++) inf->hval[th][i] = i < tot ? d[q + 17 + i] : 0;
                inf->hmask |= (uint8_t)(1 << th);
                q += 17 + tot;
            }
        } else if (m == 0xC3) {                          /* SOF3 */
            if (dn < 6) return WF_CR2_E_MARKER;
            prec = d[0];
            jh = d[1] << 8 | d[2];
            jw = d[3] << 8 | d[4];
            nc = d[5];
        } else if ((m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) {
            return WF_CR2_E_SOF;                         /* another frame type */
        } else if (m == 0xDA) {                          /* SOS */
            ns = d[0];
            if (ns < 1 || ns > 4 || dn < (uint32_t)(4 + 2 * ns)) return WF_CR2_E_MARKER;
            for (int i = 0; i < ns; i++) inf->tsel[i] = (uint8_t)(d[2 + 2 * i] >> 4);
            if (d[1 + 2 * ns] != 1 || (d[3 + 2 * ns] & 15) != 0) return WF_CR2_E_SOF;   /* predictor 1, Pt 0 */
            k += 2 + len;
            break;
        } else if (m == 0xD9) {
            return WF_CR2_E_MARKER;
        }
        k += 2 + len;
    }
    if (!jw || !jh || nc < 1 || nc > 4 || prec < 2 || prec > 16 || ns != nc) return WF_CR2_E_SOF;
    for (int c = 0; c < nc; c++)
        if (inf->tsel[c] > 3 || !(inf->hmask & (1 << inf->tsel[c]))) return WF_CR2_E_DHT;
    int w = jw * nc;
    if (w > 16384 || jh > 16384 || (w & 7)) return WF_CR2_E_SIZE;
    if (inf->sl_n || inf->sl_w || inf->sl_last) {
        if (inf->sl_n < 0 || inf->sl_n > 64 || inf->sl_w <= 0 || inf->sl_last <= 0 ||
            inf->sl_n * inf->sl_w + inf->sl_last != w) return WF_CR2_E_SLICE;
    }
    inf->jw = jw; inf->jh = jh; inf->nc = nc; inf->prec = prec;
    inf->w = w; inf->h = jh; inf->pitch = w / 8 * 14;
    inf->data_off = k;
    return WF_CR2_OK;
}

const char *wf_cr2_err(int e)
{
    static const char *const s[WF_CR2_E_N] = { "ok", "short head", "not a CR2", "bad IFD", "no raw strip", "no SOI",
        "bad marker", "not lossless SOF3", "bad Huffman table", "bad size", "bad slices", "bad parameter",
        "bad code", "truncated", "read error" };
    return e <= 0 && -e < WF_CR2_E_N ? s[-e] : "?";
}

static int rowmax_of(int w) { return w * 8 + 64; }     /* 31 bits per sample, doubled by stuffing, + lookahead */

uint32_t wf_cr2_inbuf_min(const struct wf_cr2_info *inf)
{
    return 2u * (uint32_t)rowmax_of(inf->w) + 2u;
}

/* ------------------------------------------------------------------ Huffman tables */
static int build_table(struct wf_cr2_dec *d, int t, const uint8_t *bits, const uint8_t *val)
{
    int16_t *f = d->fast[t];
    for (int i = 0; i < (1 << WF_CR2_FASTB); i++) f[i] = 0;
    int code = 0, k = 0;
    for (int L = 1; L <= 16; L++) {
        int cnt = bits[L - 1];
        d->maxcode[t][L] = -1;
        d->valoff[t][L] = k - code;
        for (int i = 0; i < cnt; i++, k++, code++) {
            if (code >= (1 << L) || k > 16) return WF_CR2_E_DHT;
            int s = val[k];
            if (s > 16) return WF_CR2_E_DHT;
            d->hval[t][k] = (uint8_t)s;
            if (L <= WF_CR2_FASTB) {
                int span = WF_CR2_FASTB - L, lo = code << span;
                for (int x = 0; x < (1 << span); x++) {
                    int e;
                    if (s == 0) e = 16 | L;
                    else if (s < 16 && s <= span) {
                        int v = x >> (span - s);
                        int diff = v < (1 << (s - 1)) ? v - ((1 << s) - 1) : v;
                        e = diff * 32 + 16 + L + s;
                    } else if (s < 16) e = L | s << 5;     /* code done, s extra bits to read */
                    else e = 0;                         /* s = 16: long_code */
                    f[lo + x] = (int16_t)e;
                }
            }
        }
        if (cnt) d->maxcode[t][L] = code - 1;
        code <<= 1;
    }
    d->maxcode[t][0] = d->maxcode[t][17] = -1;
    return WF_CR2_OK;
}

/* ------------------------------------------------------------------ input */
static int refill(struct wf_cr2_dec *d)
{
    uint32_t r = (uint32_t)(d->pe - d->p);
    if (d->eof || r >= (uint32_t)d->rowmax) return 0;
    uint8_t *b = d->buf;
    if (r) {
        if ((uint32_t)(d->p - b) >= r) memcpy(b, d->p, r);
        else for (uint32_t i = 0; i < r; i++) b[i] = d->p[i];     /* overlap: forward copy, dst < src */
    }
    uint8_t *pe = b + r, *lim = b + d->bufn - 2;
    while (pe < lim && d->left) {
        uint32_t want = (uint32_t)(lim - pe);
        if (want > d->left) want = d->left;
        int got = d->rd(d->ctx, pe, want);
        if (got < 0) return WF_CR2_E_READ;
        if (got == 0) { d->left = 0; break; }
        if ((uint32_t)got > want) got = (int)want;
        pe += got;
        d->left -= (uint32_t)got;
    }
    if (!d->left) {                                      /* the strip's end: a marker the reader never passes */
        d->eof = 1;
        pe[0] = 0xFF;
        pe[1] = 0xD9;
    }
    d->p = b;
    d->pe = pe;
    return 0;
}

/* ------------------------------------------------------------------ the row loops */
/* refill the bit buffer to >= 25 bits: 0xFF 0x00 -> 0xFF; 0xFF + anything else is a marker: feed 0, do not advance */
#define REFILL                                                                                 \
    while (nb <= 24) {                                                                         \
        uint32_t b_ = *p;                                                                      \
        if (b_ == 0xFF) {                                                                      \
            if (p[1] == 0) p += 2;                                                             \
            else { b_ = 0; pad++; }                                                            \
        } else p++;                                                                            \
        acc |= b_ << (24 - nb);                                                                \
        nb += 8;                                                                               \
    }

/* one difference of component c into diff (int), or goto bad */
#define DECODE(c)                                                                              \
    do {                                                                                       \
        REFILL;                                                                                \
        int e_ = ft[c][acc >> (32 - WF_CR2_FASTB)];                                            \
        if (e_ & 16) {                                                                         \
            int n_ = e_ & 15;                                                                  \
            acc <<= n_; nb -= n_;                                                              \
            diff = e_ >> 5;                                                                    \
        } else {                                                                               \
            int L_, s_;                                                                        \
            if (e_) { L_ = e_ & 15; s_ = (e_ >> 5) & 31; }                                     \
            else {                                                                             \
                int r_ = long_code(d, d->tsel[c], acc);                                        \
                if (r_ < 0) goto bad;                                                          \
                L_ = r_ & 31; s_ = r_ >> 8;                                                    \
            }                                                                                  \
            acc <<= L_; nb -= L_;                                                              \
            if (!s_) diff = 0;                                                                 \
            else if (s_ == 16) diff = -32768;                                                  \
            else {                                                                             \
                REFILL;                                                                        \
                uint32_t v_ = acc >> (32 - s_);                                                \
                acc <<= s_; nb -= s_;                                                          \
                diff = v_ < (1u << (s_ - 1)) ? (int)v_ - ((1 << s_) - 1) : (int)v_;            \
            }                                                                                  \
        }                                                                                      \
    } while (0)

/* the output cursor: one slice row is done -> next row, or the next slice */
#define NEXT_SROW                                                                              \
    do {                                                                                       \
        ox = 0;                                                                                \
        if (++oy == d->h) { oy = 0; xs += sw; s++; sw = s < d->sl_n ? d->sl_w : d->sl_last; }   \
    } while (0)

/* mode 16 (the fast path). The bit buffer is kept at >= 16 bits: two bytes at once when neither is 0xFF (Canon's data
 * has about one in 256), else byte by byte with the unstuffing. A predictor is held as value << 16, so adding the
 * difference << 16 is the 16-bit wrap of T.81 for free. Codes longer than WF_CR2_FASTB bits: long_code (rare). */
#define REFILL_BYTES                                                                           \
    while (nb <= 24) {                                                                         \
        uint32_t b_ = *p;                                                                      \
        if (b_ == 0xFF) {                                                                      \
            if (p[1] == 0) p += 2;                                                             \
            else { b_ = 0; d->pad++; }                                                         \
        } else p++;                                                                            \
        acc |= b_ << (24 - nb);                                                                \
        nb += 8;                                                                               \
    }
#define REFILL16                                                                               \
    if (nb < 16) {                                                                             \
        uint32_t b0_ = p[0], b1_ = p[1];                                                       \
        if (b0_ != 0xFF && b1_ != 0xFF) {                                                      \
            acc |= b0_ << (24 - nb) | b1_ << (16 - nb);                                        \
            p += 2;                                                                            \
            nb += 16;                                                                          \
        } else REFILL_BYTES                                                                    \
    }

/* the slow path: a code longer than the fast table, or SSSS 16. Canonical decode (T.81 F.16) of the top bits of acc
   (>= 16 valid): returns SSSS << 8 | code length, or -1 */
static int __attribute__((noinline)) long_code(const struct wf_cr2_dec *d, int t, uint32_t acc)
{
    for (int L = 1; L <= 16; L++) {
        int cd = (int)(acc >> (32 - L));
        if (cd <= d->maxcode[t][L]) {
            unsigned k = (unsigned)(d->valoff[t][L] + cd);
            return k > 16 ? -1 : d->hval[t][k] << 8 | L;
        }
    }
    return -1;
}

/* one sample of the component whose predictor is LAST (value << 16), fast table FT, table T -> *vp++. The extra bits:
   v = the next s bits; the difference is v when its top bit is set, else v - (2^s - 1) (T.81 F.12), branch-free */
#define EXTRA(LAST, S)                                                                         \
    do {                                                                                       \
        if (nb < (S)) REFILL_BYTES                                                             \
        uint32_t t_ = 32 - (S), v_ = acc >> t_;                                                \
        uint32_t m_ = (uint32_t)((int32_t)~acc >> 31) & (0xFFFFFFFFu >> t_);                   \
        acc <<= (S); nb -= (S);                                                                \
        LAST += (v_ - m_) << 16;                                                               \
    } while (0)
#define SAMPLE(LAST, FT, T)                                                                    \
    do {                                                                                       \
        REFILL16;                                                                              \
        int e_ = (FT)[acc >> (32 - WF_CR2_FASTB)];                                             \
        if (e_ & 16) {                                 /* code + extra bits in the table */    \
            int n_ = e_ & 15;                                                                  \
            acc <<= n_; nb -= n_;                                                              \
            LAST += (uint32_t)(e_ >> 5) << 16;                                                 \
        } else if (e_) {                               /* code in the table, s 1..15 to read */ \
            int L_ = e_ & 15, s_ = e_ >> 5;                                                    \
            acc <<= L_; nb -= L_;                                                              \
            EXTRA(LAST, s_);                                                                   \
        } else {                                       /* long code or SSSS 16: out of line */  \
            int r_ = long_code(d, T, acc);                                                     \
            if (r_ < 0) goto bad;                                                              \
            int L_ = r_ & 31, s_ = r_ >> 8;                                                    \
            acc <<= L_; nb -= L_;                                                              \
            if (s_ == 16) LAST += 0x80000000u;                                                 \
            else if (s_) EXTRA(LAST, s_);                                                      \
        }                                                                                      \
        *vp++ = LAST;                                                                          \
    } while (0)

/* every slice width, the row width, raw and pitch multiples of 16 px / 4 bytes; NC 1, 2 or 4 (a constant) */
static inline __attribute__((always_inline)) int row16_nc(struct wf_cr2_dec *d, const int NC)
{
    uint32_t acc = d->acc;
    int nb = d->nb;
    const uint8_t *p = d->p;
    uint32_t l0 = (uint32_t)d->last[0] << 16, l1 = (uint32_t)d->last[1] << 16;
    uint32_t l2 = (uint32_t)d->last[2] << 16, l3 = (uint32_t)d->last[3] << 16;
    const int16_t *f0 = d->fast[d->tsel[0]], *f1 = d->fast[d->tsel[1]];
    const int16_t *f2 = d->fast[d->tsel[2]], *f3 = d->fast[d->tsel[3]];
    int s = d->s, oy = d->oy, ox = d->ox, xs = d->xs, sw = d->sw;
    uint32_t *o = (uint32_t *)(d->raw + oy * d->pitch + ((xs + ox) >> 3) * 14);
    uint32_t v[16];
    for (int x = 0; x < d->w; x += 16) {
        uint32_t *vp = v;
        for (int k = 0; k < 16; k += NC) {
            SAMPLE(l0, f0, d->tsel[0]);
            if (NC > 1) SAMPLE(l1, f1, d->tsel[1]);
            if (NC > 2) { SAMPLE(l2, f2, d->tsel[2]); SAMPLE(l3, f3, d->tsel[3]); }
        }
        if (!x)
            for (int c = 0; c < NC; c++) d->last[c] = (int)(v[c] >> 16);   /* the next row's predictors */
#define V(i) (v[i] >> 16)                    /* prec <= 14 (a mode 16 condition): no mask needed */
        uint32_t a0 = V(0), a1 = V(1), a2 = V(2), a3 = V(3);
        o[0] = (a0 << 2 | a1 >> 12) | ((a1 & 4095) << 4 | a2 >> 10) << 16;
        uint32_t a4 = V(4), a5 = V(5);
        o[1] = ((a2 & 1023) << 6 | a3 >> 8) | ((a3 & 255) << 8 | a4 >> 6) << 16;
        uint32_t a6 = V(6), a7 = V(7);
        o[2] = ((a4 & 63) << 10 | a5 >> 4) | ((a5 & 15) << 12 | a6 >> 2) << 16;
        uint32_t b0 = V(8), b1 = V(9);
        o[3] = ((a6 & 3) << 14 | a7) | (b0 << 2 | b1 >> 12) << 16;
        uint32_t b2 = V(10), b3 = V(11);
        o[4] = ((b1 & 4095) << 4 | b2 >> 10) | ((b2 & 1023) << 6 | b3 >> 8) << 16;
        uint32_t b4 = V(12), b5 = V(13);
        o[5] = ((b3 & 255) << 8 | b4 >> 6) | ((b4 & 63) << 10 | b5 >> 4) << 16;
        uint32_t b6 = V(14), b7 = V(15);
        o[6] = ((b5 & 15) << 12 | b6 >> 2) | ((b6 & 3) << 14 | b7) << 16;
#undef V
        o += 7;
        ox += 16;
        if (ox == sw) {
            NEXT_SROW;
            o = (uint32_t *)(d->raw + oy * d->pitch + (xs >> 3) * 14);
        }
    }
    d->acc = acc; d->nb = nb; d->p = p;
    d->s = s; d->oy = oy; d->ox = ox; d->xs = xs; d->sw = sw;
    return 0;
bad:
    return WF_CR2_E_DATA;
}
static int row16_1(struct wf_cr2_dec *d) { return row16_nc(d, 1); }
static int row16_2(struct wf_cr2_dec *d) { return row16_nc(d, 2); }
static int row16_4(struct wf_cr2_dec *d) { return row16_nc(d, 4); }
static int row16(struct wf_cr2_dec *d)
{
    return d->nc == 2 ? row16_2(d) : d->nc == 4 ? row16_4(d) : row16_1(d);
}

/* one pixel into the packed layout: the 8 px of a group are a 112-bit MSB-first stream over 7 halfwords */
static void put1(uint8_t *raw, int pitch, int x, int y, uint32_t v)
{
    uint16_t *h = (uint16_t *)(raw + y * pitch + (x >> 3) * 14);
    int pb = (x & 7) * 14, k = pb >> 4, o = pb & 15;
    if (o + 14 <= 16) {
        int sh = 2 - o;
        h[k] = (uint16_t)((h[k] & ~(0x3FFFu << sh)) | v << sh);
    } else {
        int sh = 18 - o;
        uint32_t u = (uint32_t)h[k] << 16 | h[k + 1];
        u = (u & ~(0x3FFFu << sh)) | v << sh;
        h[k] = (uint16_t)(u >> 16);
        h[k + 1] = (uint16_t)u;
    }
}

/* modes 8 and 1: any nc, sample by sample */
static int rowg(struct wf_cr2_dec *d)
{
    uint32_t acc = d->acc, pad = d->pad;
    int nb = d->nb;
    const uint8_t *p = d->p;
    const int16_t *ft[4];
    int last[4];
    for (int c = 0; c < 4; c++) { ft[c] = d->fast[d->tsel[c] & 3]; last[c] = d->last[c]; }
    int s = d->s, oy = d->oy, ox = d->ox, xs = d->xs, sw = d->sw;
    int nc = d->nc, c = 0, k8 = 0;
    uint32_t v8[8];
    for (int x = 0; x < d->w; x++) {
        int diff;
        DECODE(c);
        int val = (last[c] + diff) & 0xFFFF;
        last[c] = val;
        if (x < nc) d->last[x] = val;
        if (++c == nc) c = 0;
        if (d->mode == 8) {
            v8[k8++] = (uint32_t)val & 16383;
            if (k8 == 8) {
                uint16_t *h = (uint16_t *)(d->raw + oy * d->pitch + ((xs + ox - 7) >> 3) * 14);
                h[0] = (uint16_t)(v8[0] << 2 | v8[1] >> 12);
                h[1] = (uint16_t)((v8[1] & 4095) << 4 | v8[2] >> 10);
                h[2] = (uint16_t)((v8[2] & 1023) << 6 | v8[3] >> 8);
                h[3] = (uint16_t)((v8[3] & 255) << 8 | v8[4] >> 6);
                h[4] = (uint16_t)((v8[4] & 63) << 10 | v8[5] >> 4);
                h[5] = (uint16_t)((v8[5] & 15) << 12 | v8[6] >> 2);
                h[6] = (uint16_t)((v8[6] & 3) << 14 | v8[7]);
                k8 = 0;
            }
        } else put1(d->raw, d->pitch, xs + ox, oy, (uint32_t)val & 16383);
        if (++ox == sw) NEXT_SROW;
    }
    d->acc = acc; d->nb = nb; d->p = p; d->pad = pad;
    d->s = s; d->oy = oy; d->ox = ox; d->xs = xs; d->sw = sw;
    return 0;
bad:
    return WF_CR2_E_DATA;
}

/* ------------------------------------------------------------------ the decoder */
int wf_cr2_dec_begin(struct wf_cr2_dec *d, const struct wf_cr2_info *inf, uint8_t *raw, int pitch, uint8_t *inbuf,
                     uint32_t inbuf_n, int (*rd)(void *ctx, void *buf, uint32_t n), void *ctx)
{
    if (!d) return WF_CR2_E_PARAM;
    memset(d, 0, sizeof *d);
    if (!inf || !raw || !inbuf || !rd || inf->w <= 0 || inf->h <= 0 || (inf->w & 7) || inf->nc < 1 || inf->nc > 4 ||
        pitch < inf->w / 8 * 14 || (pitch & 1) || ((uintptr_t)raw & 1) || inbuf_n < wf_cr2_inbuf_min(inf) ||
        inf->data_off < 4 || inf->data_off >= (uint32_t)rowmax_of(inf->w) || inf->strip_len <= inf->data_off)
        return d->err = WF_CR2_E_PARAM;
    for (int t = 0; t < 4; t++)
        if (inf->hmask & (1 << t)) {
            int r = build_table(d, t, inf->hbits[t], inf->hval[t]);
            if (r) return d->err = r;
        }
    for (int c = 0; c < 4; c++) d->tsel[c] = c < inf->nc ? inf->tsel[c] & 3 : inf->tsel[0] & 3;
    d->raw = raw; d->pitch = pitch; d->w = inf->w; d->h = inf->h; d->nc = inf->nc; d->jh = inf->jh;
    d->rowmax = rowmax_of(inf->w);
    d->buf = inbuf; d->bufn = inbuf_n; d->p = d->pe = inbuf; d->left = inf->strip_len;
    d->rd = rd; d->ctx = ctx;
    int r = refill(d);
    if (r) return d->err = r;
    if ((uint32_t)(d->pe - d->p) < inf->data_off || d->p[0] != 0xFF || d->p[1] != 0xD8)
        return d->err = WF_CR2_E_NOSOI;
    d->p += inf->data_off;
    for (int c = 0; c < 4; c++) d->last[c] = 1 << (inf->prec - 1);
    if (inf->sl_w > 0) { d->sl_n = inf->sl_n; d->sl_w = inf->sl_w; d->sl_last = inf->sl_last; }
    else { d->sl_n = 0; d->sl_w = inf->w; d->sl_last = inf->w; }
    d->sw = d->sl_n > 0 ? d->sl_w : d->sl_last;
    int m16 = (inf->nc == 1 || inf->nc == 2 || inf->nc == 4) && inf->prec <= 14 && !(d->sl_w & 15) && !(d->sl_last & 15) &&
              !(inf->w & 15) && !(pitch & 3) && !((uintptr_t)raw & 3);
    int m8 = !(d->sl_w & 7) && !(d->sl_last & 7);
    d->mode = m16 ? 16 : m8 ? 8 : 1;
#ifdef WF_CR2_TEST
    if (wf_cr2_force_mode == 8 && m8) d->mode = 8;
    if (wf_cr2_force_mode == 1) d->mode = 1;
#endif
    return 0;
}

int wf_cr2_dec_step(struct wf_cr2_dec *d, int njrows)
{
    if (d->err) return d->err;
    while (njrows-- > 0 && d->jrow < d->jh) {
        int r = refill(d);
        if (!r) r = d->mode == 16 ? row16(d) : rowg(d);
        if (!r && d->pad * 8 > (uint32_t)d->nb) r = WF_CR2_E_TRUNC;     /* decoded from bits past the data */
        if (r) return d->err = r;
        d->jrow++;
    }
    return d->jrow < d->jh ? 1 : 0;
}
