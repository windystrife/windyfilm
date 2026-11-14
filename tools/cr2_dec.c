/* cr2_dec.c - minimal Canon CR2 reader for the RAW develop's real-shot test (WindyStrife). Host tool (gcc, x86), own
 * code from the public format descriptions: TIFF / EXIF structure, the CR2 header (raw IFD offset at byte 12), the
 * slice tag 0xC640 and the lossless JPEG of ITU-T T.81 (SOF3, Huffman, predictor 1).
 *
 *   cr2_dec IN.CR2 OUTDIR    -> OUTDIR/raw.r14 (the sensor image in ML's packed 14-bit layout, 8 px in 14 bytes, pitch
 *                               = width * 14 / 8) and OUTDIR/meta.txt (width height pitch iso black_measured
 *                               wb_r wb_g1 wb_g2 wb_b white_measured; wb = Canon's as-shot RGGB levels from
 *                               ColorData, 0 if not found; white_measured = the clip pile-up, 0 if nothing clipped)
 * Only what the EOS M / 5D Mark II files need: one lossless JPEG strip, 2 or 4 interleaved components, predictor 1. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *F;
static size_t FN;
static int LE = 1;

static unsigned g16(size_t o) { return o + 2 > FN ? 0 : LE ? F[o] | F[o + 1] << 8 : F[o] << 8 | F[o + 1]; }
static uint32_t g32(size_t o)
{
    if (o + 4 > FN) return 0;
    return LE ? (uint32_t)F[o] | (uint32_t)F[o + 1] << 8 | (uint32_t)F[o + 2] << 16 | (uint32_t)F[o + 3] << 24
              : (uint32_t)F[o] << 24 | (uint32_t)F[o + 1] << 16 | (uint32_t)F[o + 2] << 8 | F[o + 3];
}

/* find tag in the IFD at o: returns the entry offset or 0 */
static size_t ifd_tag(size_t o, unsigned tag)
{
    unsigned n = g16(o);
    for (unsigned i = 0; i < n; i++)
        if (g16(o + 2 + 12 * i) == tag)
            return o + 2 + 12 * i;
    return 0;
}
static uint32_t ent_val(size_t e)               /* value of a SHORT / LONG entry with count 1, or the offset */
{
    unsigned type = g16(e + 2);
    uint32_t cnt = g32(e + 4);
    if (type == 3 && cnt <= 2) return g16(e + 8);
    return g32(e + 8);
}

/* ------------------------------------------------------------------ lossless JPEG */
struct huff { int16_t code_len[65536]; uint8_t sym[65536]; };   /* 16-bit lookahead table */
static struct huff H[4];
static size_t bp;                                /* byte position in the entropy data */
static uint32_t bbuf;
static int bcnt;

static void huff_build(struct huff *h, const uint8_t *cnt, const uint8_t *val)
{
    int code = 0, k = 0;
    for (int i = 0; i < 65536; i++) h->code_len[i] = 0;
    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < cnt[len - 1]; i++, k++) {
            int lo = code << (16 - len), hi = (code + 1) << (16 - len);
            for (int x = lo; x < hi; x++) { h->code_len[x] = (int16_t)len; h->sym[x] = val[k]; }
            code++;
        }
        code <<= 1;
    }
}

static uint32_t bits_peek16(void)
{
    while (bcnt < 16) {
        uint8_t b = 0;
        if (bp < FN) {
            b = F[bp];
            if (b == 0xFF) {
                if (bp + 1 < FN && F[bp + 1] == 0x00) bp += 2;
                else b = 0;                      /* a marker: feed zeros */
            } else
                bp++;
        }
        bbuf = bbuf << 8 | b;
        bcnt += 8;
    }
    return (bbuf >> (bcnt - 16)) & 0xFFFF;
}
static void bits_skip(int n) { bcnt -= n; }
static int bits_get(int n)
{
    if (!n) return 0;
    uint32_t v = 0;
    while (n > 16) { v = v << 16 | bits_peek16(); bits_skip(16); n -= 16; }
    v = v << n | (bits_peek16() >> (16 - n));
    bits_skip(n);
    return (int)v;
}

static int diff_decode(const struct huff *h)
{
    uint32_t p = bits_peek16();
    int len = h->code_len[p];
    if (!len) return 0;
    int s = h->sym[p];
    bits_skip(len);
    if (s == 0) return 0;
    if (s == 16) return -32768;
    int v = bits_get(s);
    if (v < (1 << (s - 1))) v -= (1 << s) - 1;
    return v;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s IN.CR2 OUTDIR\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); FN = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    F = malloc(FN);
    if (fread(F, 1, FN, f) != FN) { fprintf(stderr, "read\n"); return 1; }
    fclose(f);
    if (F[0] == 'M') LE = 0;
    if (F[8] != 'C' || F[9] != 'R') { fprintf(stderr, "not a CR2\n"); return 1; }
    size_t ifd0 = g32(4), rawifd = g32(12);
    /* EXIF: ISO, MakerNote -> ColorData (as-shot WB at short 63 for ColorData4 and later, 25 / 34 / 71 for the 582 /
       653 / 5120 entry versions) */
    int iso = 0, wb[4] = { 0, 0, 0, 0 };
    size_t e = ifd_tag(ifd0, 0x8769);
    if (e) {
        size_t exif = g32(e + 8);
        size_t ei = ifd_tag(exif, 0x8827);
        if (ei) iso = (int)g16(ei + 8);
        size_t em = ifd_tag(exif, 0x927C);
        if (em) {
            size_t mn = g32(em + 8);
            size_t cd = ifd_tag(mn, 0x4001);
            if (cd) {
                uint32_t cnt = g32(cd + 4), off = g32(cd + 8);
                int idx = cnt == 582 ? 25 : cnt == 653 ? 34 : cnt == 5120 ? 71 : 63;
                for (int c = 0; c < 4; c++) wb[c] = (int)g16(off + 2 * (idx + c));
            }
        }
    }
    size_t so = ifd_tag(rawifd, 0x111), sc = ifd_tag(rawifd, 0xC640);
    if (!so) { fprintf(stderr, "no strip in the raw IFD\n"); return 1; }
    size_t strip = ent_val(so);
    int sl_n = 0, sl_w = 0, sl_last = 0;
    if (sc) { size_t o = g32(sc + 8); sl_n = (int)g16(o); sl_w = (int)g16(o + 2); sl_last = (int)g16(o + 4); }
    /* JPEG markers */
    size_t p = strip;
    if (g16(p) != 0xD8FF && !(F[p] == 0xFF && F[p + 1] == 0xD8)) { fprintf(stderr, "no SOI\n"); return 1; }
    p += 2;
    int jw = 0, jh = 0, nc = 0, prec = 0, tbl[4] = { 0 };
    for (;;) {
        if (F[p] != 0xFF) { fprintf(stderr, "marker expected at %zu\n", p); return 1; }
        int m = F[p + 1];
        int len = F[p + 2] << 8 | F[p + 3];
        const uint8_t *d = F + p + 4;
        if (m == 0xC4) {
            int k = 0;
            while (k < len - 2) {
                int tc = d[k] & 15;
                const uint8_t *cnt = d + k + 1;
                int tot = 0;
                for (int i = 0; i < 16; i++) tot += cnt[i];
                huff_build(&H[tc & 3], cnt, d + k + 17);
                k += 17 + tot;
            }
        } else if (m == 0xC3) {
            prec = d[0]; jh = d[1] << 8 | d[2]; jw = d[3] << 8 | d[4]; nc = d[5];
        } else if (m == 0xDA) {
            int ns = d[0];
            for (int i = 0; i < ns && i < 4; i++) tbl[i] = d[2 + 2 * i] >> 4;
            p += 2 + len;
            break;
        }
        p += 2 + len;
    }
    if (!jw || !jh || nc < 1 || nc > 4) { fprintf(stderr, "no SOF3\n"); return 1; }
    int W = jw * nc, Hh = jh;
    if (!sl_w) { sl_n = 0; sl_w = W; sl_last = W; }
    uint16_t *img = calloc((size_t)W * Hh, 2);
    bp = p; bbuf = 0; bcnt = 0;
    /* decode row by row (jw x nc samples), predictor 1, and scatter into the slices */
    int *prev = calloc((size_t)W, sizeof(int)), *row = calloc((size_t)W, sizeof(int));
    long pos = 0;                                /* sample index in slice order */
    for (int y = 0; y < Hh; y++) {
        for (int x = 0; x < W; x++) {
            int c = x % nc;
            int pred;
            if (x < nc) pred = y ? prev[c] : 1 << (prec - 1);
            else pred = row[x - nc];
            row[x] = (pred + diff_decode(&H[tbl[c]])) & 0xFFFF;
        }
        memcpy(prev, row, (size_t)W * sizeof(int));
        for (int x = 0; x < W; x++, pos++) {
            /* slice order: slices 0..sl_n-1 of width sl_w, then the last of width sl_last, each top to bottom */
            long sw = sl_w, s = pos / ((long)sl_w * Hh), ox, oy;
            if (s < sl_n) { long r = pos % ((long)sl_w * Hh); oy = r / sw; ox = s * sw + r % sw; }
            else { long r = pos - (long)sl_n * sl_w * Hh; oy = r / sl_last; ox = (long)sl_n * sl_w + r % sl_last; }
            if (oy < Hh && ox < W) img[(size_t)oy * W + ox] = (uint16_t)row[x];
        }
    }
    /* optical black: columns 8..63 (left of the active area), rows 60 .. H - 60 */
    double bs = 0;
    long bn = 0;
    for (int y = 60; y < Hh - 60; y += 3)
        for (int x = 8; x < 64 && x < W; x++) { bs += img[(size_t)y * W + x]; bn++; }
    /* clip level: the most common raw value above 8192 in the active area (x >= 72, y >= 52) when it is a pile-up (the
       sensor's white for this ISO): at least 1000 photosites and 8 x the mean of the 64 values 17..80 below it (a smooth
       picture spreads ~1000-3000 photosites over every value); 0 = nothing clipped in this shot */
    static uint32_t hist[16384];
    for (int y = 52; y < Hh; y++)
        for (int x = 72; x < W; x++) hist[img[(size_t)y * W + x] & 16383]++;
    int wpk = 8193;
    for (int v = 8193; v < 16384; v++)
        if (hist[v] > hist[wpk]) wpk = v;
    double base = 0;
    for (int v = wpk - 80; v <= wpk - 17; v++) base += hist[v];
    base /= 64;
    uint32_t wn = hist[wpk];
    if (wn < 1000 || wn < 8 * base) wpk = 0;
    /* write */
    char path[1024];
    int pitch = W / 8 * 14;
    snprintf(path, sizeof path, "%s/raw.r14", argv[2]);
    FILE *o = fopen(path, "wb");
    if (!o) { perror(path); return 1; }
    uint8_t *pk = malloc((size_t)pitch + 16);
    for (int y = 0; y < Hh; y++) {
        const uint16_t *v = img + (size_t)y * W;
        uint16_t *dd = (uint16_t *)pk;
        for (int i = 0; i + 8 <= W; i += 8, v += 8, dd += 7) {
            uint32_t a = v[0] & 16383, b = v[1] & 16383, c = v[2] & 16383, d3 = v[3] & 16383, e4 = v[4] & 16383,
                     f5 = v[5] & 16383, g = v[6] & 16383, h = v[7] & 16383;
            dd[0] = (uint16_t)(a << 2 | b >> 12);
            dd[1] = (uint16_t)((b & 4095) << 4 | c >> 10);
            dd[2] = (uint16_t)((c & 1023) << 6 | d3 >> 8);
            dd[3] = (uint16_t)((d3 & 255) << 8 | e4 >> 6);
            dd[4] = (uint16_t)((e4 & 63) << 10 | f5 >> 4);
            dd[5] = (uint16_t)((f5 & 15) << 12 | g >> 2);
            dd[6] = (uint16_t)((g & 3) << 14 | h);
        }
        fwrite(pk, 1, (size_t)pitch, o);
    }
    fclose(o);
    snprintf(path, sizeof path, "%s/meta.txt", argv[2]);
    o = fopen(path, "w");
    fprintf(o, "%d %d %d %d %.1f %d %d %d %d %d\n", W, Hh, pitch, iso, bn ? bs / bn : 0, wb[0], wb[1], wb[2], wb[3],
            wpk);
    fclose(o);
    printf("%s: %dx%d (jpeg %dx%d x %d, %d bit, slices %d x %d + %d), ISO %d, black %.1f, as-shot WB %d %d %d %d, "
           "clip pile %d (%u px, %.0f per value below)\n", argv[1], W, Hh, jw, jh, nc, prec, sl_n, sl_w, sl_last, iso,
           bn ? bs / bn : 0, wb[0], wb[1], wb[2], wb[3], wpk, wn, base);
    return 0;
}
