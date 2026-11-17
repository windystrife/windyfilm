/* wf_dev.c - WindyFilm full RAW develop "B" (WindyStrife). See wf_dev.h for the pipeline, threads and memory.
 *
 * Build: module flags (-march=armv5t -marm), -O2 for the row kernel. Integer only; no LDRD / STRD; the only 64-bit
 * division is wf_dv_udiv64 (shift and subtract, load time and once per shot); 32-bit divisions only per block / per
 * shot (__aeabi_uidiv, in both card .sym). The worker part (wf_dev_prepare, wf_dev_rows) allocates nothing, never logs
 * and never touches a file.
 *
 * Numbers:
 *   raw     int16 after black subtraction (not clamped: noise below black averages out in the demosaic)
 *   x       linear Q14, 1.0 = 16384 = the JPEG path's linear 1.0 (sRGB decode of code 255), clamped 0 .. 2^20 - 1
 *   matrix  M = gain x gm x diag(m) in x per raw unit, scaled per shot by 2^sh so that sum |M_ij| x WF_DV_RAWMAX < 2^31:
 *           32-bit MUL / MLA (sh about 9 at EI 0: coefficients ~1300, under 1e-3 relative = 0.0015 EV)
 *   enc     CLZ index i(x) = s * 256 + (x >> s), s = max(0, 23 - clz(x)): exact below 256 (1/64 of white), then 256
 *           steps per octave; the value is the grid coordinate of the bin centre (Q10), the log curve evaluated as in
 *           wf_loglut.c wf_ll_coords (wf_lenc interpolation; above 1.0 the curve's log segment, exact)
 *   nodes   Y9 | Cb9 << 16 and Cr9 (half codes 0..510); weights Q7 (sum 128): lane sums <= 65280, so the rounding
 *           (+ 128) never carries into the next lane.
 *
 * Highlights (trap W19): colour ratios come only from quads 55..92 % of clip with no clipped photosite within 2 quads
 * (>= 3 px); a pixel's channel counts as clipped when a photosite of that colour in its 3x3 window is clipped; the
 * channel is then raised to the estimate from its unclipped channels and the block map's ratios (never lowered); all
 * three clipped: neutral at the brightest white-balanced channel. */
#include <stdint.h>
#include <stddef.h>
#include "wf_dev.h"
#define WF_LL_EXTERN_ENC                         /* wf_ll_enc = wf_lenc (wf_logout_tab.c) */
#include "wf_loglut_tab.h"                       /* wf_ll_dec (sRGB decode), wf_ll_mat (Rec.709 -> log gamut) */

extern void *__mem_malloc(size_t len, unsigned flags, const char *file, unsigned line);
extern void  __mem_free(void *p);
extern void  wf_copyw(uint32_t *d, const uint32_t *s, int n);   /* wf_shot.c: 8-word LDM / STM bursts */
/* the log pack's reads (wf_dev_build_pack; the EOS M / 5D2 core exports, as wf_loglut.c declares them) */
extern void *FIO_OpenFile(const char *name, unsigned mode);   /* 0 on error */
extern int   FIO_ReadFile(void *f, void *buf, size_t n);      /* buf must be uncacheable */
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
#define WF_DV_MEM_DMA 1
#define WF_DV_MEM_TMP 2

/* ------------------------------------------------------------------ constants (tools/mk_devmat.py --check) */
static const int32_t wf_dev_rgbcam[WF_DEV_NCAM][9] = {   /* camera (WB) -> linear Rec.709, Q20 */
    { 1977740, -1092465, 163301, -227580, 1751304, -475148, 20972, -526778, 1554382 },   /* EOS M */
    { 2168654, -1384477, 264399, -40008, 1431792, -343208, 37421, -397175, 1408330 },   /* 5D Mark II */
};
static const int32_t wf_dev_camrgb[WF_DEV_NCAM][9] = {   /* linear Rec.709 -> camera (WB), Q20 */
    { 600400, 391560, 56616, 83501, 745853, 219222, 20198, 247486, 780892 },   /* EOS M */
    { 515882, 506188, 26506, 11936, 835316, 201324, -10341, 222125, 836792 },   /* 5D Mark II */
};
static const int32_t wf_dev_day[WF_DEV_NCAM][3] = {      /* D65 multipliers, G = 4096 */
    { 9892, 4096, 5590 },   /* EOS M */
    { 10542, 4096, 5683 },   /* 5D Mark II */
};
/* the same numbers as wf_loglut.c (wf_ll_eik, wf_ll_eib, wf_ll_eid): LUT EI gains round(2^(t/3) 65536), t = -3..6; per
   curve the log segment's b (Q24) and a log10(2) in log code Q20 */
static const int32_t wf_dv_eik[WF_LL_EI_MAX - WF_LL_EI_MIN + 1] = { 32768, 41285, 52016, 65536, 82570, 104032, 131072,
                                                                    165140, 208064, 262144 };
static const int32_t wf_dv_eib[WF_LL_NCURVE] = { 167772, 146465, 157856, 173359, 1007758 };
static const int32_t wf_dv_eid[WF_LL_NCURVE] = { 20575288, 19439817, 19896687, 19427483, 29561978 };
/* Y, Cb + 128, Cr + 128 in half codes from R'G'B' sums s = value x 65535 x 65536: x 2^48 (BT.601 full range) */
#define WF_DV_K(k) ((int64_t)((k) * 510.0 * 281474976710656.0 / (65535.0 * 65536.0) + 0.5))
static const int64_t wf_dv_ycc[3][3] = {
    { WF_DV_K(0.299), WF_DV_K(0.587), WF_DV_K(0.114) },
    { -WF_DV_K(0.168735891647856), -WF_DV_K(0.331264108352144), WF_DV_K(0.5) },
    { WF_DV_K(0.5), -WF_DV_K(0.418687589158345), -WF_DV_K(0.081312410841655) },
};
#define WF_DV_ONE    (1 << 24)
#define WF_DV_RAWMAX 57344                       /* |raw| bound of the matrix shift (4 x 14336): MHC output stays below */
#define WF_DV_HLMAX  (1 << 19)                   /* rebuilt channels (36 x 14336): through the matrix with fewer bits */

/* ------------------------------------------------------------------ small helpers */
static uint32_t wf_dv_udiv64(uint64_t n, uint64_t d)   /* n / d, saturating; no __aeabi_uldivmod */
{
    uint64_t q = 0, r = 0;
    if (!d)
        return 0xffffffffu;
    for (int b = 63; b >= 0; b--) {
        r = r << 1 | ((n >> b) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << b; }
    }
    return q > 0xffffffffu ? 0xffffffffu : (uint32_t)q;
}

static inline int wf_dv_clz(uint32_t x) { return __builtin_clz(x | 1); }

/* x (Q14, 0 .. WF_DEV_XMAX) -> enc index */
static inline int wf_dv_idx(uint32_t x)
{
    int s = 23 - wf_dv_clz(x);
    if (s < 0)
        s = 0;
    return (s << 8) + (int)(x >> s);
}

const int32_t *wf_dev_cam_rgb(int cam) { return wf_dev_rgbcam[cam >= 0 && cam < WF_DEV_NCAM ? cam : 0]; }

/* 3x3 Q20 product A . B, rows re-summed to 2^20 on the diagonal (white kept) */
static void wf_dv_mat(const int32_t *A, const int32_t *B, int32_t *o)
{
    for (int r = 0; r < 3; r++) {
        int32_t s = 0;
        for (int c = 0; c < 3; c++) {
            int64_t v = (int64_t)A[3 * r] * B[c] + (int64_t)A[3 * r + 1] * B[3 + c] + (int64_t)A[3 * r + 2] * B[6 + c];
            o[3 * r + c] = (int32_t)((v + (1 << 19)) >> 20);
            s += o[3 * r + c];
        }
        o[4 * r] += (1 << 20) - s;
    }
}

/* ------------------------------------------------------------------ load time: tables */
/* log code (Q20 of the 8-bit code) of linear light x24 (Q24) for curve c (1..5): wf_loglut.c wf_ll_coords' curve
   step, with the log segment above 1.0 always on */
static int32_t wf_dv_logcode(int curve, int32_t x24)
{
    const uint16_t *enc = wf_ll_enc[curve - 1];
    int up = 0;
    if (x24 < 0)
        x24 = 0;
    if (x24 > WF_DV_ONE) {
        int32_t h = x24, b = wf_dv_eib[curve - 1];
        while (h > WF_DV_ONE) { h >>= 1; up++; }
        x24 = (int32_t)(((int64_t)x24 + b + (1 << (up - 1))) >> up) - b;
        if (x24 > WF_DV_ONE)
            x24 = WF_DV_ONE;
    }
    int idx = x24 >> 12;
    int32_t u;
    if (idx >= 4096) {
        u = (int32_t)enc[4096] << 12;
    } else {
        int32_t e0 = enc[idx];
        u = (e0 << 12) + ((int32_t)enc[idx + 1] - e0) * (x24 & 4095);
    }
    return u + up * wf_dv_eid[curve - 1];
}

/* word stride of each channel in the node table */
static const int32_t wf_dv_wst[3] = { 2, 2 * WF_DEV_G, 2 * WF_DEV_BSTR };

/* bin centre (Q24, saturated at 128.0) of enc index i */
static int32_t wf_dv_bin24(int i)
{
    if (i < 256)
        return (i << 10) + 512;
    int s = (i >> 8) - 1, mant = i & 255;
    int64_t c = ((int64_t)(256 + mant) << (s + 10)) + ((int64_t)1 << (s + 9));
    return c > 0x7fffffff ? 0x7fffffff : (int32_t)c;
}

/* grid coordinate Q7 (0 .. 32 x 128 - 1) -> enc entry of channel ch */
static uint32_t wf_dv_entry(int ch, int64_t g7)
{
    if (g7 < 0) g7 = 0;
    if (g7 > (WF_DEV_G - 1) * 128 - 1) g7 = (WF_DEV_G - 1) * 128 - 1;   /* cell 31 at most */
    return (uint32_t)(((g7 >> 7) * wf_dv_wst[ch]) << 9 | (g7 & 127));
}

/* enc table of channel ch: domain [dmin, dmax] (Q24) of the cube's input */
static void wf_dv_enc(uint32_t *e, int ch, int curve, int32_t dmin, int32_t dmax)
{
    uint32_t r24 = (uint32_t)(dmax - dmin);
    int32_t off = (int32_t)(((int64_t)dmin * 255) >> 4);
    int32_t K = (int32_t)wf_dv_udiv64((uint64_t)(WF_DEV_G - 1) << 52, (uint64_t)r24 * 255u);
    for (int i = 0; i < WF_DEV_NENC; i++) {
        int32_t u = wf_dv_logcode(curve, wf_dv_bin24(i));
        int64_t g = ((int64_t)(u - off) * K) >> 32;              /* Q16 grid coordinate, 0 .. 32 << 16 */
        e[i] = wf_dv_entry(ch, (g + 256) >> 9);                  /* rounded to the kernel's Q7 weights */
    }
}

/* R'G'B' sums (value x 65535 x 65536) -> node words */
static void wf_dv_node(uint32_t *nd, const uint32_t s[3])
{
    int v[3];
    for (int k = 0; k < 3; k++) {
        int64_t a = wf_dv_ycc[k][0] * (int64_t)s[0] + wf_dv_ycc[k][1] * (int64_t)s[1] + wf_dv_ycc[k][2] * (int64_t)s[2];
        a += (int64_t)1 << 47;
        if (k)
            a += (int64_t)256 << 48;
        int q = (int)(a >> 48);
        v[k] = q < 0 ? 0 : q > 510 ? 510 : q;
    }
    nd[0] = (uint32_t)(v[0] + 1) | (uint32_t)(v[1] + 1) << 16;   /* + 1: the rounding of the lane sums (weights sum
                                                                    to 128: + 128 per pixel) */
    nd[1] = (uint32_t)(v[2] + 1);
}

/* cube resampler: the 33^3 nodes at n-grid position i (n - 1) / 32, tetrahedral in two B-slabs (streamed) */
struct wf_dv_b {
    struct wf_dev_tabs *t;
    int n, nn3, ir, ig, ib, curve, disp, hs;
    uint16_t *slab, *wp;
    uint8_t N[256];                              /* WF_DEV_DISP: wf_hs_bend curve on the output codes */
    int32_t pos[WF_DEV_G];                       /* Q16 position on the n-grid */
    uint8_t cell[WF_DEV_G];
};

static void wf_dv_eval(struct wf_dv_b *B, int i, int j, int k)
{
    int n = B->n, cr = B->cell[i], cg = B->cell[j], cb = B->cell[k];
    uint32_t f[3] = { (uint32_t)(B->pos[i] - (cr << 16)), (uint32_t)(B->pos[j] - (cg << 16)),
                      (uint32_t)(B->pos[k] - (cb << 16)) };
    const uint16_t *p0 = B->slab + (cb & 1) * B->nn3 + (cg * n + cr) * 3;
    int st[3] = { 3, 3 * n, (cb & 1) ? -B->nn3 : B->nn3 };
    int a = 0, b = 1, c = 2, tt;
    if (f[a] < f[b]) { tt = a; a = b; b = tt; }
    if (f[b] < f[c]) { tt = b; b = c; c = tt; }
    if (f[a] < f[b]) { tt = a; a = b; b = tt; }
    const uint16_t *p1 = p0 + st[a], *p2 = p1 + st[b], *p3 = p2 + st[c];
    uint32_t w0 = 65536 - f[a], w1 = f[a] - f[b], w2 = f[b] - f[c], w3 = f[c];
    uint32_t s[3];
    for (int ch = 0; ch < 3; ch++)
        s[ch] = w0 * p0[ch] + w1 * p1[ch] + w2 * p2[ch] + w3 * p3[ch];
    wf_dv_node(B->t->node + 2 * (i + WF_DEV_G * j + WF_DEV_BSTR * k), s);
}

static void wf_dv_enc_srgb(uint32_t *e, int ch);

static int wf_dv_hdr(void *ctx, const struct wf_ll_hdr *h)
{
    struct wf_dv_b *B = ctx;
    int n = h->n;
    B->n = n;
    B->nn3 = n * n * 3;
    for (int i = 0; i < WF_DEV_G; i++) {
        B->pos[i] = (i * (n - 1)) << 11;         /* i (n - 1) / 32 in Q16, exact */
        int c = B->pos[i] >> 16;
        B->cell[i] = (uint8_t)(c > n - 2 ? n - 2 : c);
    }
    int same = 1;
    for (int c = 0; c < 3; c++) {
        if (B->disp) {                           /* the cube's input is the sRGB code 0..1: the SRGB develop's enc */
            if (h->dmin[c] != 0 || h->dmax[c] != WF_DV_ONE)
                return WF_LL_E_DOMAIN;
            wf_dv_enc_srgb(B->t->enc[c], c);
            continue;
        }
        wf_dv_enc(B->t->enc[c], c, B->curve, h->dmin[c], h->dmax[c]);
        if (h->dmin[c] != 0 || h->dmax[c] != WF_DV_ONE)
            B->t->domain = 1;
        if (c && (h->dmin[c] != h->dmin[0] || h->dmax[c] != h->dmax[0]))
            same = 0;
    }
    (void)same;
    B->slab = __mem_malloc((size_t)2 * B->nn3 * sizeof(uint16_t), WF_DV_MEM_TMP, "wf_dev.c", __LINE__);
    if (!B->slab)
        return WF_LL_E_NOMEM;
    B->wp = B->slab;
    return WF_LL_OK;
}

static void wf_dv_smp(void *ctx, const uint16_t *rgb)
{
    struct wf_dv_b *B = ctx;
    uint16_t *w = B->wp;
    w[0] = rgb[0]; w[1] = rgb[1]; w[2] = rgb[2];
    if (B->hs)                                   /* Highlights / Shadows on the output code, linear between codes */
        for (int c = 0; c < 3; c++) {
            uint32_t p = (uint32_t)w[c] * 255u, i = p / 65535u, f = p - i * 65535u;
            uint32_t a = B->N[i], b = B->N[i < 255 ? i + 1 : 255];   /* N non-decreasing: b >= a */
            w[c] = (uint16_t)(a * 257u + ((b - a) * 257u * f + 32767u) / 65535u);   /* < 2^32 */
        }
    B->wp = w + 3;
    if (++B->ir < B->n)
        return;
    B->ir = 0;
    if (++B->ig < B->n)
        return;
    B->ig = 0;
    if (B->ib > 0)                               /* slabs ib - 1 and ib complete: the nodes in blue cell ib - 1 */
        for (int k = 0; k < WF_DEV_G; k++)
            if (B->cell[k] == B->ib - 1)
                for (int j = 0; j < WF_DEV_G; j++)
                    for (int i = 0; i < WF_DEV_G; i++)
                        wf_dv_eval(B, i, j, k);
    B->ib++;
    B->wp = B->slab + (B->ib & 1) * B->nn3;
}

/* the log pack (wf_dev.h): the open pack and its entry for the build in progress (wf_task, as wf_dv_hs_h) */
#define WF_DV_PKCH  8184                         /* read size: a multiple of 6 (one node) and 24 (one index entry) */
struct wf_dv_pk {
    void *f;
    char *dma;                                   /* WF_DV_PKCH + 4, uncacheable: FIO reads here */
    uint8_t *buf;                                /* its cached copy (wf_copyw bursts), the nodes are read from it */
    uint32_t off, sum;
};
static const struct wf_dv_pk *wf_dv_pkp;
/* a generated table (wf_dev_build_gen): fill gives the samples, WF_DV_GENN nodes a call, in the .cube order */
#define WF_DV_GENN (WF_DEV_G * WF_DEV_G)         /* one blue plane */
struct wf_dv_gen {
    int (*fill)(void *ctx, uint16_t *rgb, int n);
    void *ctx;
    uint16_t *buf;
};
static const struct wf_dv_gen *wf_dv_genp;

static uint32_t wf_dv_le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t wf_dv_le32(const uint8_t *p) { return wf_dv_le16(p) | wf_dv_le16(p + 2) << 16; }

/* n bytes (<= WF_DV_PKCH) from the pack's position into pk->buf */
static int wf_dv_pkread(const struct wf_dv_pk *pk, uint32_t n)
{
    if (FIO_ReadFile(pk->f, pk->dma, n) != (int)n)
        return WF_LL_E_TRUNC;
    wf_copyw((uint32_t *)pk->buf, (const uint32_t *)pk->dma, (int)((n + 3) >> 2));
    return WF_LL_OK;
}

/* wf_ll_parse's part for a generated table: the pack's grid (33 points on DOMAIN 0..1), the nodes from g->fill into the
   same sampler, so the same samples make the same tables as a pack entry */
static int wf_dv_genparse(const struct wf_dv_gen *g, struct wf_dv_b *B, struct wf_ll_info *info)
{
    struct wf_ll_hdr h;
    h.n = WF_DEV_G;
    for (int c = 0; c < 3; c++) {
        h.dmin[c] = 0;
        h.dmax[c] = WF_DV_ONE;
    }
    info->n = WF_DEV_G;
    int rc = wf_dv_hdr(B, &h);
    if (rc)
        return rc;
    for (int done = 0; done < WF_DEV_NODES; done += WF_DV_GENN) {
        if ((rc = g->fill(g->ctx, g->buf, WF_DV_GENN)))
            return rc;
        for (int i = 0; i < WF_DV_GENN; i++)
            wf_dv_smp(B, g->buf + 3 * i);
        info->bytes += WF_DV_GENN * 6;
    }
    info->samples = WF_DEV_NODES;
    return WF_LL_OK;
}

/* wf_ll_parse's part for a pack entry: a 33-point grid on DOMAIN 0..1, then the table's nodes in the .cube order into
   the same sampler; Adler-32 (RFC 1950) of the table bytes against the index */
static int wf_dv_pkparse(const struct wf_dv_pk *pk, struct wf_dv_b *B, struct wf_ll_info *info)
{
    struct wf_ll_hdr h;
    h.n = WF_DEV_G;
    for (int c = 0; c < 3; c++) {
        h.dmin[c] = 0;
        h.dmax[c] = WF_DV_ONE;
    }
    info->n = WF_DEV_G;
    int rc = wf_dv_hdr(B, &h);
    if (rc)
        return rc;
    if (FIO_SeekSkipFile(pk->f, (int64_t)pk->off, 0) != (int64_t)pk->off)
        return WF_LL_E_READ;
    uint32_t a = 1, b = 0;
    for (uint32_t left = WF_DEV_PK_BYTES; left; ) {
        uint32_t n = left < WF_DV_PKCH ? left : WF_DV_PKCH;
        if ((rc = wf_dv_pkread(pk, n)))
            return rc;
        const uint8_t *d = pk->buf;
        for (uint32_t i = 0; i < n; ) {
            uint32_t e = n - i > 5552 ? i + 5552 : n;   /* 5552: b stays below 2^32 before the reduction */
            for (; i < e; i++) {
                a += d[i];
                b += a;
            }
            a %= 65521u;
            b %= 65521u;
        }
        for (uint32_t i = 0; i < n; i += 6) {
            uint16_t rgb[3] = { (uint16_t)wf_dv_le16(d + i), (uint16_t)wf_dv_le16(d + i + 2),
                                (uint16_t)wf_dv_le16(d + i + 4) };
            wf_dv_smp(B, rgb);
        }
        left -= n;
        info->bytes += n;
    }
    info->samples = WF_DEV_NODES;
    return (b << 16 | a) == pk->sum ? WF_LL_OK : WF_DEV_PK_E_SUM;
}

/* sRGB OETF of linear Q24 (clamped 0..1) as a u16 sum unit (value x 65535): inverse of wf_ll_dec, interpolated */
static uint32_t wf_dv_oetf(int32_t x24)
{
    if (x24 <= 0)
        return 0;
    if (x24 >= WF_DV_ONE)
        return 65535;
    int lo = 0, hi = 1020;                       /* wf_ll_dec[lo] <= x24 < wf_ll_dec[hi] */
    while (hi - lo > 1) {
        int m = (lo + hi) >> 1;
        if ((int32_t)wf_ll_dec[m] <= x24) lo = m; else hi = m;
    }
    uint32_t d0 = wf_ll_dec[lo], d1 = wf_ll_dec[hi];
    uint32_t num = (uint32_t)(x24 - (int32_t)d0), den = d1 - d0, f16 = 0;
    for (int b = 15; b >= 0 && den; b--) {       /* 16 fraction bits by shift-subtract (num < den < 2^24) */
        num <<= 1;
        if (num >= den) { num -= den; f16 |= 1u << b; }
    }
    /* code = (lo + f16 / 65536) / 4 of 255 -> x 65535 / 255 = 257 */
    return (uint32_t)((((uint64_t)lo << 16) + f16) * 257 >> 18);
}

/* enc table of the SRGB base develop: the grid is the sRGB code itself (identity nodes, so the tetrahedral is exact
   between the nodes); above 1.0 the top node (the JPEG's clip) */
static void wf_dv_enc_srgb(uint32_t *e, int ch)
{
    for (int i = 0; i < WF_DEV_NENC; i++)
        e[i] = wf_dv_entry(ch, (wf_dv_oetf(wf_dv_bin24(i)) * (uint32_t)((WF_DEV_G - 1) * 128) + 32767) / 65535);
}

/* wf_film.c wf_hs_bend, the same curve (wf_dev.o links without wf_film.o in tools/dev_check.sh) */
static void wf_dv_hs_bend(uint8_t *N, int h, int s)
{
    int p = 0;
    for (int y = 0; y < 256; y++) {
        int x = N[y], w = 255 - x;
        int d = h * (x * x * w) + s * (x * w * w);
        unsigned m = (unsigned)(d < 0 ? -d : d);
        int q = (int)((m + 192670u) / 385341u);
        int v = x + (d < 0 ? -q : q);
        if (v > 255) v = 255;
        if (v < p) v = p;
        N[y] = (uint8_t)v;
        p = v;
    }
}

static int wf_dv_hs_h, wf_dv_hs_s;                /* wf_dev_build_disp's H / S for the build in progress (wf_task) */

int wf_dev_build_disp(struct wf_dev_tabs *t, int cam, const char *cube, int ei, int h, int s, struct wf_ll_info *info)
{
    wf_dv_hs_h = h < -5 ? -5 : h > 5 ? 5 : h;
    wf_dv_hs_s = s < -5 ? -5 : s > 5 ? 5 : s;
    int rc = wf_dev_build(t, WF_DEV_DISP, 0, cam, cube, ei, 0, info);
    wf_dv_hs_h = wf_dv_hs_s = 0;
    return rc;
}

static uint32_t wf_dv_fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

uint32_t wf_dev_tabs_id(int cam, int src, int curve)
{
    uint32_t w[5] = { WF_DEV_TABS_VER, (uint32_t)sizeof(struct wf_dev_tabs), (uint32_t)src, (uint32_t)curve, (uint32_t)cam };
    uint32_t h = wf_dv_fnv(2166136261u, w, sizeof w);
    if (cam >= 0 && cam < WF_DEV_NCAM)
        h = wf_dv_fnv(h, wf_dev_rgbcam[cam], sizeof wf_dev_rgbcam[cam]);
    h = wf_dv_fnv(h, wf_dv_eik, sizeof wf_dv_eik);
    h = wf_dv_fnv(h, wf_ll_dec, sizeof wf_ll_dec);           /* the auto exposure's neutral axis */
    if (curve >= 1 && curve <= WF_LL_NCURVE) {
        h = wf_dv_fnv(h, wf_ll_mat[curve - 1], sizeof wf_ll_mat[curve - 1]);
        h = wf_dv_fnv(h, wf_ll_enc[curve - 1], sizeof wf_ll_enc[curve - 1]);
        h = wf_dv_fnv(h, &wf_dv_eib[curve - 1], 4);
        h = wf_dv_fnv(h, &wf_dv_eid[curve - 1], 4);
    }
    return h ? h : 1;
}

int wf_dev_build(struct wf_dev_tabs *t, int src, int curve, int cam, const char *cube, int ei, int autoexp,
                 struct wf_ll_info *info)
{
    struct wf_ll_info tmp;
    if (!info)
        info = &tmp;
    {
        char *z = (char *)info;
        for (unsigned i = 0; i < sizeof *info; i++) z[i] = 0;
    }
    if (!t || src < WF_DEV_CUBE || src > WF_DEV_DISP || cam < 0 || cam >= WF_DEV_NCAM
        || (src < WF_DEV_SRGB && (curve < 1 || curve > WF_LL_NCURVE)))
        return WF_LL_E_MODE;
    if (src >= WF_DEV_SRGB) {
        curve = 0;                               /* no log curve: an sRGB-coded grid */
        autoexp = 0;
    }
    if (ei < WF_LL_EI_MIN || ei > WF_LL_EI_MAX)
        ei = 0;
    t->src = (uint8_t)src;
    t->curve = (uint8_t)curve;
    t->cam = (uint8_t)cam;
    t->n = 0;
    t->ei = (int8_t)ei;
    t->auto10 = 0;
    t->domain = 0;
    t->pad = 0;
    t->gain = wf_dv_eik[ei - WF_LL_EI_MIN];
    if (src >= WF_DEV_SRGB) {                    /* Rec.709 primaries; DISP keeps the LUT EI gain */
        for (int k = 0; k < 9; k++) t->gm[k] = wf_dev_rgbcam[cam][k];
        if (src == WF_DEV_SRGB)
            t->gain = 65536;
    } else {
        wf_dv_mat(wf_ll_mat[curve - 1], wf_dev_rgbcam[cam], t->gm);
    }
    if (src == WF_DEV_CUBE || src == WF_DEV_DISP) {
        struct wf_dv_b B;
        char *z = (char *)&B;
        for (unsigned i = 0; i < sizeof B; i++) z[i] = 0;
        B.t = t;
        B.curve = curve;
        B.disp = src == WF_DEV_DISP;
        if (B.disp && (wf_dv_hs_h || wf_dv_hs_s)) {
            for (int y = 0; y < 256; y++) B.N[y] = (uint8_t)y;
            wf_dv_hs_bend(B.N, wf_dv_hs_h, wf_dv_hs_s);
            B.hs = 1;
        }
        int rc = wf_dv_pkp && !B.disp ? wf_dv_pkparse(wf_dv_pkp, &B, info)
               : wf_dv_genp && !B.disp ? wf_dv_genparse(wf_dv_genp, &B, info)
                                       : wf_ll_parse(cube, wf_dv_hdr, wf_dv_smp, &B, info);
        if (B.slab)
            __mem_free(B.slab);
        if (rc)
            return rc;
        t->n = (uint8_t)B.n;
    } else {
        for (int c = 0; c < 3; c++) {
            if (src == WF_DEV_LOG)
                wf_dv_enc(t->enc[c], c, curve, 0, WF_DV_ONE);
            else
                wf_dv_enc_srgb(t->enc[c], c);
        }
        uint32_t ax[WF_DEV_G];                   /* identity nodes: R'G'B' of node i = i / 32 (sum unit) */
        for (int i = 0; i < WF_DEV_G; i++)
            ax[i] = (uint32_t)i * 65535u * 2048u;
        for (int k = 0; k < WF_DEV_G; k++)
            for (int j = 0; j < WF_DEV_G; j++)
                for (int i = 0; i < WF_DEV_G; i++) {
                    uint32_t s[3] = { ax[i], ax[j], ax[k] };
                    wf_dv_node(t->node + 2 * (i + WF_DEV_G * j + WF_DEV_BSTR * k), s);
                }
    }
    if (src == WF_DEV_CUBE && autoexp) {
        /* wf_loglut.c's Film LUT exposure on our own pixel path: the input luma whose neutral output is 118 (the middle of
           the 117.5 / 118.5 crossings), gain = lin(y*) / lin(118), 0.25 .. 4, times the LUT EI, 0.125 .. 8; within 1/40 EV
           of 1 the JPEG path keeps gain 1, and so do we */
        int N[256], lo = -2, hi = -2;            /* the neutral axis in whole codes, as wf_loglut.c's N[] */
        int32_t g0 = t->gain;
        t->gain = 65536;
        for (int y = 0; y < 256; y++) {
            int32_t x = (int32_t)((wf_ll_dec[4 * y] + 512) >> 10), xx[3] = { x, x, x };
            int o[3];
            wf_dev_px(t, xx, o);
            N[y] = (o[0] + 128) >> 8;
        }
        t->gain = g0;
        for (int pass = 0; pass < 2; pass++) {   /* wf_ll_cross(N, 235 / 237) */
            int t2 = pass ? 237 : 235, r;
            if (2 * N[0] > t2) r = -1;
            else {
                r = 256 << 8;
                for (int i = 0; i < 255; i++)
                    if (2 * N[i + 1] > t2) { r = (i << 8) + ((t2 - 2 * N[i]) << 8) / (2 * (N[i + 1] - N[i])); break; }
            }
            if (pass) hi = r; else lo = r;
        }
        int32_t ga;
        if (lo < 0 || hi < 0)
            ga = 16384;
        else if (lo > 255 << 8 || hi > 255 << 8)
            ga = 262144;
        else {
            int y8 = (lo + hi + 1) >> 1, idx = y8 >> 6;
            uint32_t fr = (uint32_t)(y8 << 8) & 0x3fff, d0 = wf_ll_dec[idx];
            uint32_t l1 = d0 + (((wf_ll_dec[idx + 1] - d0) * fr) >> 14), l0 = wf_ll_dec[118 * 4];
            uint32_t g = wf_dv_udiv64((uint64_t)l1 << 16, l0);
            ga = g < 16384 ? 16384 : g > 262144 ? 262144 : (int32_t)g;
        }
        uint64_t tt = ((uint64_t)(uint32_t)ga * (uint32_t)t->gain + 32768) >> 16;
        int32_t tot = tt < 8192 ? 8192 : tt > 524288 ? 524288 : (int32_t)tt;
        t->gain = (tot < 64410 || tot > 66682) ? tot : 65536;
        {   /* round(10 log2(ga / 65536)) as wf_ll_ev10 */
            static const uint32_t mid[10] = { 67847, 72717, 77936, 83530, 89525, 95950, 102837, 110218, 118129, 126607 };
            uint32_t g = (uint32_t)ga;
            int e = 0, k = 0;
            while (g >= 131072) { g >>= 1; e += 10; }
            while (g < 65536) { g <<= 1; e -= 10; }
            while (k < 10 && g >= mid[k]) k++;
            t->auto10 = (int8_t)(e + k);
        }
    }
    return WF_LL_OK;
}

/* the pack's header and index (wf_dev.h): the entry (id, variant, code) into pk->off / pk->sum and pi */
static int wf_dv_pkfind(struct wf_dv_pk *pk, uint32_t fsz, int id, int variant, const char *code,
                        struct wf_dev_pkinfo *pi)
{
    uint8_t want[8];
    int i = 0;
    for (; i < 8 && code && code[i]; i++) want[i] = (uint8_t)code[i];
    if (code && code[i])
        return WF_DEV_PK_E_NOENT;                /* longer than the index's 8 bytes: never listed */
    for (; i < 8; i++) want[i] = 0;
    if (fsz < 32 || wf_dv_pkread(pk, 32))
        return WF_DEV_PK_E_FORMAT;
    const uint8_t *hp = pk->buf;
    uint32_t count = wf_dv_le16(hp + 6), doff = wf_dv_le32(hp + 20), ixsum = wf_dv_le32(hp + 24);
    int curve = hp[28];
    if (hp[0] != 'W' || hp[1] != 'F' || hp[2] != 'L' || hp[3] != 'G' || wf_dv_le16(hp + 4) != 1 || !count
        || wf_dv_le32(hp + 8) != WF_DEV_G || wf_dv_le32(hp + 12) != WF_DEV_PK_BYTES || wf_dv_le32(hp + 16) != 32
        || doff < 32 + count * 24 || doff > fsz || curve < 1 || curve > WF_LL_NCURVE)
        return WF_DEV_PK_E_FORMAT;
    pi->curve = curve;
    pi->feed = hp[29];
    pi->count = (int)count;
    uint32_t h = 2166136261u, off = 0, bytes = 0, sum = 0;
    int found = 0;
    for (uint32_t left = count * 24; left; ) {   /* the index follows the header */
        uint32_t n = left < WF_DV_PKCH ? left : WF_DV_PKCH;
        if (wf_dv_pkread(pk, n))
            return WF_DEV_PK_E_INDEX;
        h = wf_dv_fnv(h, pk->buf, n);
        for (const uint8_t *e = pk->buf; !found && e < pk->buf + n; e += 24) {
            if (e[0] != id || e[1] != variant)
                continue;
            int k = 0;
            while (k < 8 && e[4 + k] == want[k]) k++;
            if (k < 8)
                continue;
            found = 1;
            pi->flags = e[2];
            off = wf_dv_le32(e + 12);
            bytes = wf_dv_le32(e + 16);
            sum = wf_dv_le32(e + 20);
        }
        left -= n;
    }
    pi->ixsum = h;
    if (h != ixsum)
        return WF_DEV_PK_E_INDEX;
    if (!found)
        return WF_DEV_PK_E_NOENT;
    if (bytes != WF_DEV_PK_BYTES || off < doff || off > fsz || bytes > fsz - off)
        return WF_DEV_PK_E_INDEX;
    pk->off = pi->off = off;
    pk->sum = pi->sum = sum;
    return WF_LL_OK;
}

int wf_dev_build_pack(struct wf_dev_tabs *t, int cam, const char *pack, int id, int variant, const char *code, int ei,
                      struct wf_ll_info *info, struct wf_dev_pkinfo *pi)
{
    struct wf_ll_info tmp;
    struct wf_dev_pkinfo ptmp;
    if (!info)
        info = &tmp;
    if (!pi)
        pi = &ptmp;
    {
        char *z = (char *)info;
        for (unsigned i = 0; i < sizeof *info; i++) z[i] = 0;
        z = (char *)pi;
        for (unsigned i = 0; i < sizeof *pi; i++) z[i] = 0;
    }
    if (!t || cam < 0 || cam >= WF_DEV_NCAM)
        return WF_LL_E_MODE;
    uint32_t fsz = 0;
    if (!pack || FIO_GetFileSize(pack, &fsz) != 0)
        return WF_LL_E_OPEN;
    struct wf_dv_pk pk = { 0, 0, 0, 0, 0 };
    if (!(pk.f = FIO_OpenFile(pack, 0)))
        return WF_LL_E_OPEN;
    pk.dma = __mem_malloc(WF_DV_PKCH + 4, WF_DV_MEM_DMA | WF_DV_MEM_TMP, "wf_dev.c", __LINE__);
    pk.buf = __mem_malloc(WF_DV_PKCH + 4, WF_DV_MEM_TMP, "wf_dev.c", __LINE__);
    int rc = pk.dma && pk.buf ? wf_dv_pkfind(&pk, fsz, id, variant, code, pi) : WF_LL_E_NOMEM;
    if (!rc) {
        wf_dv_pkp = &pk;
        rc = wf_dev_build(t, WF_DEV_CUBE, pi->curve, cam, pack, ei, 0, info);
        wf_dv_pkp = 0;
        int k = 0;                               /* the log line's title: the table code */
        for (; code && k < 8 && code[k]; k++) info->title[k] = code[k];
        info->title[k] = 0;
    }
    if (pk.buf)
        __mem_free(pk.buf);
    if (pk.dma)
        __mem_free(pk.dma);
    FIO_CloseFile(pk.f);
    return rc;
}

int wf_dev_build_gen(struct wf_dev_tabs *t, int cam, int curve, int ei, int (*fill)(void *ctx, uint16_t *rgb, int n),
                     void *ctx, struct wf_ll_info *info)
{
    struct wf_ll_info tmp;
    if (!info)
        info = &tmp;
    if (!t || !fill || cam < 0 || cam >= WF_DEV_NCAM || curve < 1 || curve > WF_LL_NCURVE)
        return WF_LL_E_MODE;
    struct wf_dv_gen g = { fill, ctx, __mem_malloc(WF_DV_GENN * 6, WF_DV_MEM_TMP, "wf_dev.c", __LINE__) };
    if (!g.buf)
        return WF_LL_E_NOMEM;
    wf_dv_genp = &g;
    int rc = wf_dev_build(t, WF_DEV_CUBE, curve, cam, "gen", ei, 0, info);
    wf_dv_genp = 0;
    __mem_free(g.buf);
    return rc;
}

int wf_dev_pack_samples(const char *pack, int id, int variant, const char *code,
                        int (*fn)(void *ctx, const uint8_t *le, int n), void *ctx, struct wf_dev_pkinfo *pi)
{
    {
        char *z = (char *)pi;
        for (unsigned i = 0; i < sizeof *pi; i++) z[i] = 0;
    }
    uint32_t fsz = 0;
    if (!pack || !fn || FIO_GetFileSize(pack, &fsz) != 0)
        return WF_LL_E_OPEN;
    struct wf_dv_pk pk = { 0, 0, 0, 0, 0 };
    if (!(pk.f = FIO_OpenFile(pack, 0)))
        return WF_LL_E_OPEN;
    pk.dma = __mem_malloc(WF_DV_PKCH + 4, WF_DV_MEM_DMA | WF_DV_MEM_TMP, "wf_dev.c", __LINE__);
    pk.buf = __mem_malloc(WF_DV_PKCH + 4, WF_DV_MEM_TMP, "wf_dev.c", __LINE__);
    int rc = pk.dma && pk.buf ? wf_dv_pkfind(&pk, fsz, id, variant, code, pi) : WF_LL_E_NOMEM;
    if (!rc && FIO_SeekSkipFile(pk.f, (int64_t)pk.off, 0) != (int64_t)pk.off)
        rc = WF_LL_E_READ;
    uint32_t a = 1, b = 0;
    for (uint32_t left = WF_DEV_PK_BYTES; !rc && left; ) {
        uint32_t n = left < WF_DV_PKCH ? left : WF_DV_PKCH;
        if ((rc = wf_dv_pkread(&pk, n)))
            break;
        const uint8_t *d = pk.buf;
        for (uint32_t i = 0; i < n; ) {
            uint32_t e = n - i > 5552 ? i + 5552 : n;   /* as wf_dv_pkparse */
            for (; i < e; i++) {
                a += d[i];
                b += a;
            }
            a %= 65521u;
            b %= 65521u;
        }
        rc = fn(ctx, d, (int)(n / 6));
        left -= n;
    }
    if (!rc && (b << 16 | a) != pk.sum)
        rc = WF_DEV_PK_E_SUM;
    if (pk.buf)
        __mem_free(pk.buf);
    if (pk.dma)
        __mem_free(pk.dma);
    FIO_CloseFile(pk.f);
    return rc;
}

/* the colour trim (wf_dev.h): each node's R'G'B' back from its Y'CbCr (BT.601 full range, 9-bit half codes), s = its
   max - min / 510, chroma x ks kv (Q15) rounded half away from 0 */
void wf_dev_trim(struct wf_dev_tabs *t, int sat, int vib)
{
    if (!t || (!sat && !vib))
        return;
    sat = sat < -100 ? -100 : sat > 100 ? 100 : sat;
    vib = vib < -100 ? -100 : vib > 100 ? 100 : vib;
    const int32_t ks = 32768 + sat * 32768 / 100;
    for (int b = 0; b < WF_DEV_G; b++)
        for (int g = 0; g < WF_DEV_G; g++)
            for (int r = 0; r < WF_DEV_G; r++) {
                uint32_t *nd = t->node + 2 * (r + WF_DEV_G * g + WF_DEV_BSTR * b);
                int y = (int)(nd[0] & 0xffff) - 1, c[2] = { (int)(nd[0] >> 16) - 257, (int)nd[1] - 257 };
                if (!c[0] && !c[1])
                    continue;                    /* a neutral */
                int R = y + ((91881 * c[1] + 32768) >> 16), G = y - ((22554 * c[0] + 46802 * c[1] + 32768) >> 16),
                    B = y + ((116130 * c[0] + 32768) >> 16);
                int mx = R > G ? R : G, mn = R < G ? R : G;
                if (B > mx) mx = B;
                if (B < mn) mn = B;
                int s = mx - mn > 510 ? 510 : mx - mn;
                int32_t kv = 32768 + vib * s * 32768 / 51000;      /* |vib s 32768| <= 100 x 510 x 32768 < 2^31 */
                int32_t k = (int32_t)(((int64_t)ks * kv + 16384) >> 15);
                for (int q = 0; q < 2; q++) {
                    int32_t v = c[q] * k;
                    v = (v >= 0 ? (v + 16384) >> 15 : -((-v + 16384) >> 15)) + 256;
                    c[q] = v < 0 ? 0 : v > 510 ? 510 : v;
                }
                nd[0] = (uint32_t)(y + 1) | (uint32_t)(c[0] + 1) << 16;
                nd[1] = (uint32_t)(c[1] + 1);
            }
}

void wf_dev_paper(struct wf_dev_tabs *t, int p)
{
    if (!t || p <= WF_PAPER_GLOSSY || p >= WF_NPAPER)
        return;
    for (int b = 0; b < WF_DEV_G; b++)
        for (int g = 0; g < WF_DEV_G; g++)
            for (int r = 0; r < WF_DEV_G; r++) {
                uint32_t *nd = t->node + 2 * (r + WF_DEV_G * g + WF_DEV_BSTR * b);
                /* half codes Y9 = 2 Y', Cb9 = 2 (Cb + 128), each stored + 1: x 8 = code x 16 */
                int y = ((int)(nd[0] & 0xffff) - 1) * 8, u = ((int)(nd[0] >> 16) - 257) * 8, v = ((int)nd[1] - 257) * 8;
                wf_paper_ycc16(p, &y, &u, &v);
                y = (y + 4) >> 3;
                u = ((u + 4) >> 3) + 256;
                v = ((v + 4) >> 3) + 256;
                y = y < 0 ? 0 : y > 510 ? 510 : y;
                u = u < 0 ? 0 : u > 510 ? 510 : u;
                v = v < 0 ? 0 : v > 510 ? 510 : v;
                nd[0] = (uint32_t)(y + 1) | (uint32_t)(u + 1) << 16;
                nd[1] = (uint32_t)(v + 1);
            }
}

const char *wf_dev_pk_errstr(int rc)
{
    switch (rc) {
    case WF_DEV_PK_E_FORMAT: return "not a log pack";
    case WF_DEV_PK_E_INDEX:  return "pack index bad";
    case WF_DEV_PK_E_NOENT:  return "not in the pack";
    case WF_DEV_PK_E_SUM:    return "table checksum";
    }
    return wf_loglut_errstr(rc);
}

/* ------------------------------------------------------------------ the pixel look (shared by the kernel and tests) */
/* tetrahedral on the packed nodes at the enc entries er, eg, eb: lane sums a0 = 256 Y | 256 (Cb + 128) << 16,
   a1 = 256 (Cr + 128) (each lane = node half codes x Q7 weights). src/wf_dev_arm.S does the same, bit for bit. */
static inline __attribute__((always_inline)) void
wf_dv_tet(const uint32_t *node, uint32_t er, uint32_t eg, uint32_t eb, uint32_t *a0, uint32_t *a1)
{
    uint32_t fr = er & 127, fg = eg & 127, fb = eb & 127;
    const uint32_t *p = node + ((er + eg + eb) >> 9);
    int o1, o2;
    uint32_t w0, w1, w2, w3;
    if (fr > fg) {
        if (fg > fb)      { o1 = 2;    o2 = 2 + 66;    w0 = 128 - fr; w1 = fr - fg; w2 = fg - fb; w3 = fb; }
        else if (fr > fb) { o1 = 2;    o2 = 2 + 2304;  w0 = 128 - fr; w1 = fr - fb; w2 = fb - fg; w3 = fg; }
        else              { o1 = 2304; o2 = 2 + 2304;  w0 = 128 - fb; w1 = fb - fr; w2 = fr - fg; w3 = fg; }
    } else {
        if (fb > fg)      { o1 = 2304; o2 = 66 + 2304; w0 = 128 - fb; w1 = fb - fg; w2 = fg - fr; w3 = fr; }
        else if (fb > fr) { o1 = 66;   o2 = 66 + 2304; w0 = 128 - fg; w1 = fg - fb; w2 = fb - fr; w3 = fr; }
        else              { o1 = 66;   o2 = 2 + 66;    w0 = 128 - fg; w1 = fg - fr; w2 = fr - fb; w3 = fb; }
    }
    const int o3 = 2 + 66 + 2304;
    *a0 = w0 * p[0] + w1 * p[o1] + w2 * p[o2] + w3 * p[o3];
    *a1 = w0 * p[1] + w1 * p[o1 + 1] + w2 * p[o2 + 1] + w3 * p[o3 + 1];
}

void wf_dev_px(const struct wf_dev_tabs *t, const int32_t x[3], int o[3])
{
    uint32_t g[3], a0, a1;
    for (int c = 0; c < 3; c++) {
        int64_t v = ((int64_t)x[c] * t->gain + 32768) >> 16;
        uint32_t u = v < 0 ? 0 : v > WF_DEV_XMAX ? WF_DEV_XMAX : (uint32_t)v;
        g[c] = t->enc[c][wf_dv_idx(u)];
    }
    wf_dv_tet(t->node, g[0], g[1], g[2], &a0, &a1);
    o[0] = (int)(a0 & 0xffff) - 128;            /* without the nodes' rounding bias */
    o[1] = (int)(a0 >> 16) - 128;
    o[2] = (int)(a1 & 0xffff) - 128;
}

/* ------------------------------------------------------------------ raw unpack */
void wf_dev_unpack14(const uint8_t *src, int16_t *dst, int npx, int black)
{
    const uint16_t *s = (const uint16_t *)src;
    for (int i = 0; i < npx; i += 8, s += 7, dst += 8) {
        uint32_t h0 = s[0], h1 = s[1], h2 = s[2], h3 = s[3], h4 = s[4], h5 = s[5], h6 = s[6];
        dst[0] = (int16_t)((int)(h0 >> 2) - black);
        dst[1] = (int16_t)((int)(((h0 & 3) << 12) | (h1 >> 4)) - black);
        dst[2] = (int16_t)((int)(((h1 & 15) << 10) | (h2 >> 6)) - black);
        dst[3] = (int16_t)((int)(((h2 & 63) << 8) | (h3 >> 8)) - black);
        dst[4] = (int16_t)((int)(((h3 & 255) << 6) | (h4 >> 10)) - black);
        dst[5] = (int16_t)((int)(((h4 & 1023) << 4) | (h5 >> 12)) - black);
        dst[6] = (int16_t)((int)(((h5 & 4095) << 2) | (h6 >> 14)) - black);
        dst[7] = (int16_t)((int)(h6 & 16383) - black);
    }
}

/* ------------------------------------------------------------------ per-shot state in the scratch */
#define WF_DV_M  8                               /* ring row margin, px (>= 2) */
#define WF_DV_SPAN 64                            /* pixel pairs per call of the span kernel */

/* the span kernel (pixel pairs without a clipped pixel in their 3x3): the MHC of each pair (pixel x then x + 1, each in
   the row's colour order: row colour, G, column colour), the matrix (int16 coefficients, x = max(acc, 0) >> sh, no
   rounding term), the enc lookups, the tetrahedral and the UYVY word. wf_dv_span_s (src/wf_dev_arm.S, ARMv5TE) does
   it pair by pair (the values stay in a hot stack slot); the C reference wf_dv_mhc_c + wf_dv_lut_c goes through tmp
   in chunks; the words are the same (tests/host/wf_dev_test.c span). */
struct wf_dv_span {                              /* src/wf_dev_arm.S reads it by these offsets */
    const int16_t  *row[5];                      /*  0: rows y-2 .. y+2 at window column 0 */
    int32_t        *tmp;                         /* 20: C reference only: WF_DV_SPAN x 6 */
    const uint32_t *node;                        /* 24 */
    const uint32_t *encb;                        /* 28: enc[0] + (23 - sh) x 256 entries (the asm's CLZ index) */
    uint32_t        k;                           /* 32: 256 << sh */
    uint32_t        w[5];                        /* 36: the matrix in the row's colour order: int16 pairs m00 m01,
                                                    m02 m10, m11 m12, m20 m21, m22 (low half first) */
    int             sh;                          /* 56: C reference only */
    const uint32_t *enc;                         /* 60: C reference only: enc[0] */
    int             p;                           /* 64: the row's phase: 1 = x even is green */
    /* film shoulder I / D (j.hue): wf_dv_span_hs only, read once per call (wf_dv_hue_idx is the C reference) */
    const uint32_t *tb;                          /* 68: the shift table + off entries (off = (23 - sh) x 256: the asm's
                                                    CLZ index is enc index - off, as for encb) */
    int32_t         noff;                        /* 72: -off, the CLZ index of enc index 0 */
    uint32_t        lc0;                         /* 76: luma weights c0 | c1 << 16 (Q16, half scale: SMULW) */
    int32_t         lc2;                         /* 80: c2 */
    uint32_t        hk;                          /* 84: accumulators R and G both below hk: the luma index is below the
                                                    knee, the shift 0 (the plain lookups; 0 = always the full path) */
    int32_t         vmax;                        /* 88: the CLZ index of enc index WF_DEV_NENC - 1 */
    const uint32_t *ht;                          /* 92: the shift table (0 = shoulder A / none) */
};
typedef char wf_dv_span_chk[__builtin_offsetof(struct wf_dv_span, w) == 36 && __builtin_offsetof(struct wf_dv_span, p) == 64
                            && __builtin_offsetof(struct wf_dv_span, tb) == 68
                            && __builtin_offsetof(struct wf_dv_span, ht) == 92 && sizeof(void *) == 4 ? 1 : -1];
void wf_dv_span_s(const struct wf_dv_span *sp, int x, int n, uint32_t *out);   /* n pairs from x into out[0 .. n) */
void wf_dv_span_hs(const struct wf_dv_span *sp, int x, int n, uint32_t *out);  /* the same with the shoulder I / D */
struct wf_dv_map { uint16_t kr, kb, ikr, ikb; }; /* block colour: R/G, B/G, G/R, G/B in raw units, Q12 */
struct wf_dv_sum { uint32_t r, g, b, n; };       /* block sums of the ratio samples (g = G1 + G2) */

struct wf_dv_ctx {
    struct wf_dev_job j;
    const struct wf_dev_tabs *t;
    struct wf_dev_stat *st;
    int32_t  M[9];                               /* raw -> x Q14, scaled by 2^sh */
    int      sh;
    int      cl, thr;                            /* clip (white - black) and the clipped test (97 %) */
    int32_t  qr, qb, iqr, iqb;                   /* m_R / m_G, m_B / m_G and inverses, Q12 (neutral fill) */
    int      rw, rs;                             /* ring row length and stride (int16) */
    int      xs8, ox;                            /* ring column 0 = raw column xs8 (pixblock), window column 0 = ring ox */
    int      fast;                               /* 1: the span kernel runs (sh >= 7, |M| < 2^15) */
    struct wf_dv_span sp;                        /* rows set per row, w per row colour */
    uint32_t wcol[2][5];                         /* sp.w for a red row (0) and a blue row (1) */
    int      mw;                                 /* mask words per row (block columns + 2) */
    int      bw, bh;                             /* map blocks */
    int16_t *ring[5];
    int      ring_y[5];
    uint32_t *mask[5];                           /* clip bits per ring row (word bx + 1 = block column bx) */
    uint32_t *stage;                             /* raw bytes of one row, word aligned */
    uint32_t *out;                               /* one UYVY row */
    struct wf_dv_sum *sum;                       /* bw x bh; after the fill each entry holds its struct wf_dv_map */
    uint8_t  *need;                              /* bw x bh: highlight path (a clipped quad in this or a neighbour block) */
    uint32_t hl_px;
    const uint32_t *enc;                         /* the shot's enc[3][WF_DEV_NENC]: t->enc, or enc2 with the film shoulder A */
    uint32_t *enc2;                              /* scratch: enc with the shoulder A, or the shift table of I / D
                                                    (wf_dv_roll) */
    int      hue;                                /* WF_DEV_HUE_I / _D: sp.ht holds the shift table; A: 0 */
    uint16_t *ytab;                              /* scratch: the look's neutral luma per enc index (wf_dv_roll only) */
};

static uint8_t *wf_dv_al(uint8_t *p) { return (uint8_t *)(((uintptr_t)p + 31) & ~(uintptr_t)31); }

static int wf_dv_geom(int w, int h, int *rw, int *rs, int *mw, int *bw, int *bh)
{
    *rw = w + 2 * WF_DV_M + 8;                  /* the most a window at any even x0 needs (wf_dev_prepare) */
    int b = ((*rw * 2 + 31) & ~31) ;             /* bytes, then a stride that is not a multiple of 2 KB: 5 row streams */
    b += (384 - (b & 2047) + 2048) & 2047;       /* sit 12 cache lines apart in the 4-way 8 KB D-cache */
    *rs = b / 2;
    *bw = (w + WF_DEV_BLK - 1) / WF_DEV_BLK;
    *bh = (h + WF_DEV_BLK - 1) / WF_DEV_BLK;
    *mw = *bw + 2;
    return b;
}

/* pass A's memory (it runs before the ring is used, in the same place): 2 raw rows, 3 quad rows of {R, G1 + G2, B},
   5 rows of clip bits, 3 rows of band bits */
static size_t wf_dv_passA_bytes(int w)
{
    int qw = w >> 1, nw = (qw + 31) >> 5;
    return 2 * (size_t)((w + 23) & ~15) * 2 + 9 * (size_t)((qw + 1) & ~1) * 2 + 8 * (size_t)nw * 4 + 32;
}

static size_t wf_dv_regA(int w, int rb)
{
    size_t a = 5 * (size_t)rb, b = wf_dv_passA_bytes(w);
    return ((a > b ? a : b) + 31) & ~(size_t)31;
}

size_t wf_dev_scratch_bytes(int w, int h)
{
    int rw, rs, mw, bw, bh;
    int rb = wf_dv_geom(w, h, &rw, &rs, &mw, &bw, &bh);
    size_t n = 32 + ((sizeof(struct wf_dv_ctx) + 31) & ~31u);
    n += wf_dv_regA(w, rb) + 32;                 /* ring / pass A */
    n += 5 * (((size_t)mw * 4 + 31) & ~31u);     /* masks */
    n += ((((size_t)rw / 8 + 2) * 14 + 8 + 31) & ~31u);   /* staging (one raw row of the window) */
    n += (((size_t)w * 2 + 31) & ~31u);          /* UYVY row */
    n += WF_DV_SPAN * 24 + 32;                   /* span kernel values */
    n += (size_t)bw * bh * sizeof(struct wf_dv_sum) + 32;
    n += (size_t)bw * bh + 32;
    n += 3 * WF_DEV_NENC * 4 + 32;               /* enc2 (film shoulder) */
    n += WF_DEV_NENC * 2 + 32;                   /* ytab */
    return n;
}

/* raw row sy (sensor), columns xs8 .. xs8 + rw (window columns -ox ..), black subtracted, into r */
static void wf_dv_rawrow(struct wf_dv_ctx *c, int sy, int16_t *r)
{
    const struct wf_dev_job *j = &c->j;
    int xs = c->xs8;                             /* multiple of 8 */
    uint32_t b0 = (uint32_t)(xs >> 3) * 14, nb = (uint32_t)(c->rw >> 3) * 14;
    const uint8_t *s = j->raw + (size_t)sy * j->pitch + b0;
    uint32_t mis = (uint32_t)(uintptr_t)s & 3;
    int nw = (int)((nb + mis + 3) >> 2);
    wf_copyw(c->stage, (const uint32_t *)(s - mis), nw);
    wf_dev_unpack14((const uint8_t *)c->stage + mis, r, c->rw, j->black);
    /* the two columns outside the active area next to it (all the 5x5 window can reach from the output window):
       mirrored by an even distance (same Bayer colour) */
    int a = j->ax0 - xs, b = j->ax1 - 1 - xs;   /* first / last active column in ring coordinates */
    for (int k = 1; k <= 2; k++) {
        if (a - k >= 0 && a + k < c->rw)
            r[a - k] = r[a + k];
        if (b + k < c->rw && b - k >= 0)
            r[b + k] = r[b - k];
    }
}

static int wf_dv_srcrow(const struct wf_dev_job *j, int y)   /* output row (may be -2 .. h + 1) -> sensor row */
{
    int sy = j->y0 + y;
    if (sy < j->ay0) sy = 2 * j->ay0 - sy;
    if (sy >= j->ay1) sy = 2 * (j->ay1 - 1) - sy;
    return sy;
}

/* Bayer colour of output pixel (x, y): 0 R, 1 G, 2 B */
static inline int wf_dv_col(int cfa, int x, int y)
{
    static const uint8_t pat[4][4] = { { 0, 1, 1, 2 }, { 1, 0, 2, 1 }, { 1, 2, 0, 1 }, { 2, 1, 1, 0 } };
    return pat[cfa & 3][((y & 1) << 1) | (x & 1)];
}

/* ------------------------------------------------------------------ per shot: pass A (highlight map + WB fit) */
struct wf_dv_fit { uint64_t t[3][3]; uint32_t r[3][3]; uint32_t n[3]; };   /* per tier: camera-space target, raw, count */

static void wf_dv_fitsample(struct wf_dv_ctx *c, struct wf_dv_fit *F, int qi, int qj, int R, int G2, int B)
{
    const struct wf_dev_job *j = &c->j;
    int py = 2 * qj - j->canon_y0;               /* frame rows 2 qj, 2 qj + 1 inside canon */
    if (py < 0 || py + 1 >= j->canon_h)
        return;
    int cpw = j->canon_pw ? j->canon_pw : j->w >> 1;                  /* words per canon row */
    const uint32_t *p = j->canon + (size_t)py * cpw + qi;            /* word qi = pixels 2 qi, 2 qi + 1 */
    uint32_t w0 = p[0], w1 = p[cpw];
    int u0 = (int8_t)(w0 & 255), u1 = (int8_t)(w1 & 255), v0 = (int8_t)((w0 >> 16) & 255), v1 = (int8_t)((w1 >> 16) & 255);
    int y[4] = { (int)((w0 >> 8) & 255), (int)(w0 >> 24), (int)((w1 >> 8) & 255), (int)(w1 >> 24) };
    int ymin = y[0], ymax = y[0];
    for (int k = 1; k < 4; k++) {
        if (y[k] < ymin) ymin = y[k];
        if (y[k] > ymax) ymax = y[k];
    }
    /* flat 2 x 2 only: the raw quad and the JPEG block then see the same light (texture, edges, Canon's sharpening and
       its chroma siting would bias the sums: the code average of a textured block is not its light average) */
    if (ymax - ymin > 6 || u0 - u1 > 6 || u1 - u0 > 6 || v0 - v1 > 6 || v1 - v0 > 6)
        return;
    int32_t lin[3] = { 0, 0, 0 };
    const uint32_t *dec = c->j.dec ? c->j.dec : wf_ll_dec;   /* the JPEG's input curve (sRGB or a Canon curve) */
    for (int k = 0; k < 4; k++) {                /* each pixel decoded, the light summed (x 4) */
        int u = k < 2 ? u0 : u1, v = k < 2 ? v0 : v1, y4 = 4 * y[k];
        /* R'G'B' x 4: Y + 1.402 V, Y - 0.344136 U - 0.714136 V, Y + 1.772 U */
        int r4 = y4 + ((5743 * v) >> 10), g4 = y4 - ((1410 * u + 2925 * v) >> 10), b4 = y4 + ((7258 * u) >> 10);
        if (r4 < 96 || g4 < 96 || b4 < 96 || r4 > 896 || g4 > 896 || b4 > 896)
            return;                              /* 24 .. 224: not crushed, not near clip */
        lin[0] += (int32_t)dec[r4]; lin[1] += (int32_t)dec[g4]; lin[2] += (int32_t)dec[b4];
    }
    for (int k = 0; k < 3; k++)
        lin[k] = (lin[k] + 2) >> 2;
    int u = u0 + u1, v = v0 + v1;
    int au = u < 0 ? -u : u, av = v < 0 ? -v : v, m = au > av ? au : av;   /* 2 x |mean chroma| */
    int tier = m <= 40 ? 0 : m <= 128 ? 1 : 2;
    const int32_t *A = wf_dev_camrgb[c->t->cam];
    int32_t tc[3];
    for (int k = 0; k < 3; k++) {
        int64_t s = (int64_t)A[3 * k] * lin[0] + (int64_t)A[3 * k + 1] * lin[1] + (int64_t)A[3 * k + 2] * lin[2];
        tc[k] = (int32_t)(s >> 20);
        if (tc[k] < 0) tc[k] = 0;
    }
    for (int k = tier; k < 3; k++) {             /* a tier-0 sample counts in tiers 0, 1, 2 */
        F->t[k][0] += (uint32_t)tc[0]; F->t[k][1] += (uint32_t)tc[1]; F->t[k][2] += (uint32_t)tc[2];
        F->r[k][0] += (uint32_t)R; F->r[k][1] += (uint32_t)G2; F->r[k][2] += (uint32_t)B;
        F->n[k]++;
    }
}

/* h-dilate a bit row by 2 (bits = quads), words w */
static inline uint32_t wf_dv_hd2(const uint32_t *m, int k, int nw)
{
    uint32_t a = m[k], p = k > 0 ? m[k - 1] : 0, n = k + 1 < nw ? m[k + 1] : 0;
    return a | a << 1 | a << 2 | a >> 1 | a >> 2 | p >> 31 | p >> 30 | n << 31 | n << 30;
}

static void wf_dv_passA(struct wf_dv_ctx *c, uint8_t *mem, struct wf_dv_fit *F)
{
    const struct wf_dev_job *j = &c->j;
    const int qw = j->w >> 1, qh = j->h >> 1, nw = (qw + 31) >> 5;
    const int lo = (c->cl * 55) / 100, hi = (c->cl * 92) / 100, thr = c->thr;
    const int xa8 = j->x0 & ~7, xo = j->x0 - xa8, npx = (xo + j->w + 7) & ~7;   /* pixblocks holding the window */
    int16_t *r0 = (int16_t *)mem, *r1 = r0 + ((j->w + 23) & ~15);
    int16_t *qv = r1 + ((j->w + 23) & ~15);      /* 3 rows of qw x {R, G1 + G2, B} */
    uint32_t *qc = (uint32_t *)(qv + 3 * 3 * ((qw + 1) & ~1));   /* 5 rows of clip bits */
    uint32_t *qb = qc + 5 * nw;                  /* 3 rows of band bits */
    const int cR = j->cfa, cB = 3 - j->cfa;      /* quad {r0[2i], r0[2i+1], r1[2i], r1[2i+1]}: R at index cfa */
    struct wf_dv_sum *S = c->sum;
    for (int k = 0; k < c->bw * c->bh; k++) S[k].r = S[k].g = S[k].b = S[k].n = 0;
    for (int k = 0; k < c->bw * c->bh; k++) c->need[k] = 0;
    for (int qj = 0; qj < qh + 2; qj++) {
        if (qj < qh) {                           /* read quad row qj: output rows 2 qj, 2 qj + 1 (window only) */
            for (int e = 0; e < 2; e++) {
                int16_t *dst = e ? r1 : r0;
                int sy = j->y0 + 2 * qj + e;
                uint32_t b0 = (uint32_t)(xa8 >> 3) * 14, nb = (uint32_t)(npx >> 3) * 14;
                const uint8_t *s = j->raw + (size_t)sy * j->pitch + b0;
                uint32_t mis = (uint32_t)(uintptr_t)s & 3;
                wf_copyw(c->stage, (const uint32_t *)(s - mis), (int)((nb + mis + 3) >> 2));
                wf_dev_unpack14((const uint8_t *)c->stage + mis, dst, npx, j->black);
            }
            const int16_t *ra = r0 + xo, *rb = r1 + xo;
            int16_t *v = qv + (qj % 3) * 3 * ((qw + 1) & ~1);
            uint32_t *cw = qc + (qj % 5) * nw, *bw = qb + (qj % 3) * nw;
            for (int k = 0; k < nw; k++) cw[k] = bw[k] = 0;
            int fitrow = (qj & 7) == 4 && j->canon;
            for (int i = 0; i < qw; i++) {
                int q[4] = { ra[2 * i], ra[2 * i + 1], rb[2 * i], rb[2 * i + 1] };
                int mx = q[0] > q[1] ? q[0] : q[1];
                if (q[2] > mx) mx = q[2];
                if (q[3] > mx) mx = q[3];
                int R = q[cR], B = q[cB], G2 = q[0] + q[1] + q[2] + q[3] - R - B;
                v[3 * i] = (int16_t)R; v[3 * i + 1] = (int16_t)G2; v[3 * i + 2] = (int16_t)B;
                if (mx >= thr)
                    cw[i >> 5] |= 1u << (i & 31);
                else if (mx >= lo && mx <= hi)
                    bw[i >> 5] |= 1u << (i & 31);
                if (fitrow && (i & 7) == 4 && mx < hi && R > 16 && G2 > 32 && B > 16)
                    wf_dv_fitsample(c, F, i, qj, R, G2, B);
            }
        }
        int pj = qj - 2;                         /* quad row whose 5-row window is complete */
        if (pj < 0)
            continue;
        const uint32_t *cr[5];
        for (int d = 0; d < 5; d++) {
            int rr = pj - 2 + d;
            cr[d] = rr >= 0 && rr < qh ? qc + (rr % 5) * nw : 0;
        }
        const uint32_t *bwp = qb + (pj % 3) * nw, *cc = qc + (pj % 5) * nw;
        const int16_t *v = qv + (pj % 3) * 3 * ((qw + 1) & ~1);
        int by = (2 * pj) / WF_DEV_BLK;
        struct wf_dv_sum *Srow = S + by * c->bw;
        uint8_t *Nrow = c->need + by * c->bw;
        for (int k = 0; k < nw; k++) {
            if (cc[k]) {                         /* clipped quads: their blocks need the highlight path */
                uint32_t m = cc[k];
                while (m) {
                    int b = 31 - __builtin_clz(m);
                    m &= ~(1u << b);
                    int i = (k << 5) + b;
                    Nrow[(2 * i) / WF_DEV_BLK] = 1;
                    c->st->clip_quads++;
                }
            }
            uint32_t m = bwp[k];
            if (!m)
                continue;
            uint32_t dil = 0;
            for (int d = 0; d < 5; d++)
                if (cr[d])
                    dil |= wf_dv_hd2(cr[d], k, nw);
            m &= ~dil;
            while (m) {
                int b = 31 - __builtin_clz(m);
                m &= ~(1u << b);
                int i = (k << 5) + b;
                struct wf_dv_sum *s = Srow + (2 * i) / WF_DEV_BLK;
                s->r += (uint32_t)v[3 * i]; s->g += (uint32_t)v[3 * i + 1]; s->b += (uint32_t)v[3 * i + 2]; s->n++;
                c->st->hl_samples++;
            }
        }
    }
}

/* holes of the block map: two sweeps (forward, backward) taking the count-weighted mean of the already known
   neighbours, twice; blocks still empty: neutral (white balanced) */
static void wf_dv_fill(struct wf_dv_ctx *c)
{
    const int bw = c->bw, bh = c->bh, N = bw * bh;
    struct wf_dv_sum *S = c->sum;
    int filled = 0;
    /* state in n: 0 empty, 16 filled, 64 measured (>= 4 samples); r, b = R / G, B / G in raw units, Q16 (the G sums
       hold G1 + G2: x 2) */
    for (int k = 0; k < N; k++)
        if (S[k].n < 4) {
            S[k].r = S[k].g = S[k].b = 0;
            S[k].n = 0;
        } else {
            uint32_t g = S[k].g;
            S[k].r = wf_dv_udiv64((uint64_t)S[k].r << 17, g);
            S[k].b = wf_dv_udiv64((uint64_t)S[k].b << 17, g);
            if (S[k].r > 1u << 20) S[k].r = 1u << 20;
            if (S[k].b > 1u << 20) S[k].b = 1u << 20;
            S[k].g = 65536;
            S[k].n = 64;
        }
    for (int round = 0; round < 4; round++)     /* forward, backward, forward, backward */
        for (int t = 0; t < N; t++) {
            int k = (round & 1) ? N - 1 - t : t, bx = k % bw, by = k / bw;
            if (S[k].n >= 64)
                continue;
            uint32_t sr = 0, sb = 0, sn = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int x = bx + dx, y = by + dy;
                    if ((!dx && !dy) || x < 0 || y < 0 || x >= bw || y >= bh)
                        continue;
                    const struct wf_dv_sum *q = S + y * bw + x;
                    if (!q->n)
                        continue;
                    uint32_t wgt = q->n >> 4;    /* measured 4, filled 1 */
                    sr += q->r * wgt; sb += q->b * wgt; sn += wgt;
                }
            if (sn) {
                if (!S[k].n)
                    filled++;
                S[k].r = sr / sn;
                S[k].b = sb / sn;
                S[k].g = 65536;
                S[k].n = 16;
            }
        }
    c->st->hl_fill = filled;
    /* to ratios in place (struct wf_dv_map in the first 8 bytes); neutral where nothing reached */
    for (int k = 0; k < N; k++) {
        uint32_t kr, kb;
        if (S[k].n) {
            kr = S[k].r >> 4;                    /* Q16 -> Q12 */
            kb = S[k].b >> 4;
        } else {
            kr = (uint32_t)c->iqr;               /* neutral: R / G in raw = m_G / m_R */
            kb = (uint32_t)c->iqb;
            c->st->hl_fill++;
        }
        if (kr < 256) kr = 256;                  /* 1/16 .. 16 */
        if (kr > 65535) kr = 65535;
        if (kb < 256) kb = 256;
        if (kb > 65535) kb = 65535;
        struct wf_dv_map *m = (struct wf_dv_map *)&S[k];
        uint32_t ikr = (1u << 24) / kr, ikb = (1u << 24) / kb;
        m->kr = (uint16_t)kr;
        m->kb = (uint16_t)kb;
        m->ikr = (uint16_t)(ikr > 65535 ? 65535 : ikr);
        m->ikb = (uint16_t)(ikb > 65535 ? 65535 : ikb);
    }
}

/* the bytes of a raw row the ring reads, from the row start: the pixblocks of the window and its margins (wf_dv_rawrow),
   + 3 when rows are not word aligned (wf_copyw reads whole words) */
static int wf_dv_rowbytes(const struct wf_dev_job *j)
{
    int xs8 = (j->x0 - WF_DV_M) & ~7, rw = (j->x0 - xs8 + j->w + WF_DV_M + 7) & ~7;
    int n = ((xs8 + rw) >> 3) * 14;
    if (((uintptr_t)j->raw | (uintptr_t)j->pitch) & 3)
        n += 3;
    return n;
}

/* ------------------------------------------------------------------ the shot's levels (wf_dev.h) */
void wf_dev_levels(void *scratch, const struct wf_dev_job *j, int bx0, int bx1, int *black, int *white)
{
    uint32_t *hist = (uint32_t *)wf_dv_al((uint8_t *)scratch);   /* values 8192 .. 16383 */
    int16_t px[8];
    *black = *white = 0;
    uint32_t sb = 0, nb = 0;
    bx0 &= ~7;
    for (int y = j->ay0; y < j->ay1 && bx1 - bx0 >= 8; y += 64)
        for (int x = bx0; x + 8 <= bx1; x += 8) {
            wf_dev_unpack14(j->raw + (size_t)y * j->pitch + (size_t)(x >> 3) * 14, px, 8, 0);
            for (int k = 0; k < 8; k++) { sb += (uint32_t)px[k]; nb++; }
        }
    if (nb) {
        uint32_t b = (sb + nb / 2) / nb;
        if (b >= 1024 && b <= 4096)
            *black = (int)b;
    }
    for (int i = 0; i < 8192; i++) hist[i] = 0;
    int xa = j->ax0 & ~7;
    for (int y = j->ay0; y < j->ay1; y += 4)
        for (int x = xa; x + 8 <= j->ax1; x += 8) {
            wf_dev_unpack14(j->raw + (size_t)y * j->pitch + (size_t)(x >> 3) * 14, px, 8, 0);
            for (int k = 0; k < 8; k++)
                if (px[k] >= 8192)
                    hist[px[k] - 8192]++;
        }
    int best = -1;
    uint32_t bc = 0;
    for (int v = 81; v < 8192; v++)
        if (hist[v] > bc) { bc = hist[v]; best = v; }
    if (best < 0 || bc < 200)
        return;
    uint32_t s = 0;
    for (int v = best - 80; v <= best - 17; v++) s += hist[v];
    if (bc * 64 >= 8 * s)                        /* >= 8 x the mean of those 64 values */
        *white = best + 8192;
}

/* ------------------------------------------------------------------ film shoulder (job.roll, owner 2026-11-14)
 * A print rolls the brightest light into paper white. The look's own curve does that only where its camera had the
 * light: a V-Log film cube reaches its white about 8 stops above grey, the EOS M clips 3.2 (green) to 4.1 (the
 * white-balanced plateau) stops above grey, so clipped sky and sun stop flat mid-shoulder (luma 195 - 213 with
 * T250DMVL) next to a still-rising gradient: the "digital" edge.
 * Here, per shot: the look's curve unchanged up to a knee, then a shoulder that reaches a target luma with zero slope
 * exactly at the sensor's white xc = clip x max(m) (the all-clipped plateau of wf_dv_hl; anything brighter lands on
 * the target too). On [knee, xc] the target luma is a cubic Hermite from the look's own value and slope to the
 * target, slope 0; the enc index i of the shot then reads the look at i', where the look's neutral luma equals the
 * target. It is a map of x alone, so every channel gets the same i -> i' (each channel reaches the top on its own:
 * highlights lose saturation into white as on film), written into a per-shot copy of the enc tables: the kernels only
 * see another table, no per-pixel cost. Knee: WF_DV_ROLL_KNEE below the first clip (97 % clip x min(m)), at most an
 * octave below the look's white (sRGB base: Canon-like shoulder above 1.0).
 * Target (WF_DV_ROLL_LIFT): the look's own luma 1.5 stops above the sensor's white (T250DMVL: about 232), capped at the
 * look's white less one code. Reaching the look's white itself (about 242) needs the shoulder about twice as steep as
 * the look at the knee; per channel that steepness spreads the channels apart before they meet at white: an orange
 * ring round the sun, flare streaks turned red, blue sky pushed to cyan (Day09 IMG_0227 / 0236 / 0238). Ending on the
 * look's value at the clip (about 214) hides the sun in a grey sky. The sRGB base reaches its white either way. */
#define WF_DV_ROLL_KNEE 128                       /* knee below the first clip, enc steps (256 per octave) */
int wf_dev_roll_knee = WF_DV_ROLL_KNEE;          /* host experiments (tools); the camera keeps the default */
#define WF_DV_ROLL_LIFT 384                       /* the target: the look's own luma 1.5 stops above the sensor's white */
int wf_dev_roll_lift = WF_DV_ROLL_LIFT;          /* -1 = the look's white (host experiments) */

static uint32_t wf_dv_g7(int ch, uint32_t e) { return ((e >> 9) / (uint32_t)wf_dv_wst[ch]) << 7 | (e & 127); }

/* ---- shoulder I / D (j.hue, wf_dev.h WF_DEV_HUE_*). The map above as a shift of the enc index, s(i) = i' - i in quarter
 * steps (below 0 where the shoulder holds the look back, above 0 near the sensor's white where it lifts the look toward
 * its target), per shot in a table T of WF_DEV_NENC + 256 words (enc2): T[i] = (uint16)(s(i) + 2) | w(i) << 16, the +2
 * the final rounding, w Q14 the weight toward each channel's own shift: 0 up to the look's white iw, then the smooth
 * step t^2 (3 - 2 t) over WF_DV_HUE_SPAN steps, 1 beyond (I; D keeps w 0). Per pixel: the luma of the three
 * non-negative accumulators (half scale, the gamut's Y row, SMULW: each product floored), im = its enc index + 256 (the
 * octave of the half scale), d = T[im] low, w = T[im] high; per channel with enc index i: i += (d (16384 - w)
 * + h w) >> 16, h = T[i] low (whole steps: at most 2 codes from the per-channel A's interpolated entry at w = 1),
 * kept within 0 .. WF_DEV_NENC - 1 (a channel far above its luma may take a lift past the table), then the plain enc
 * table. A neutral has im = i: the knee and the target stay A's. */
#define WF_DV_HUE_SPAN 384                       /* I: 1.5 stops from the look's white to the per-channel shift */
#define WF_DV_HUE_NT   (WF_DEV_NENC + 256)       /* the table's words: im reaches idx(1.1 x 2^23) + 256 < 4608 */
/* luma of each log gamut: the Y row of its RGB -> XYZ (tools/mk_loglut_tab.py GAMUTS, D65), x 32768 (SMULW: half scale),
   rows sum to 32768; c0, c1 > 0 >= c2 in all five (wf_dv_hue_hk relies on it) */
static const int16_t wf_dv_hue_lw[WF_LL_NCURVE][3] = {
    { 7048, 29002, -3282 },                      /* S-Gamut3.Cine (S-Log3: the chain films' log pack) */
    { 8542, 25392, -1166 },                      /* V-Gamut */
    { 9567, 26995, -3794 },                      /* ARRI Wide Gamut 3 */
    { 8561, 28496, -4289 },                      /* Cinema Gamut (Canon Log 2) */
    { 8561, 28496, -4289 },                      /* Cinema Gamut (Canon Log 3) */
};
const char *wf_dev_hue_name(int hue)
{
    static const char *const n[WF_DEV_HUE_N] = { "a", "i", "d" };
    return hue >= 0 && hue < WF_DEV_HUE_N ? n[hue] : "?";
}
const int16_t *wf_dev_hue_luma(int curve)        /* host tests: the weights against wf_ll_mat */
{
    return curve >= 1 && curve <= WF_LL_NCURVE ? wf_dv_hue_lw[curve - 1] : 0;
}

static uint32_t wf_dv_hue_w(int hue, int i, int iw)
{
    if (hue != WF_DEV_HUE_I || i <= iw)
        return 0;
    if (i >= iw + WF_DV_HUE_SPAN)
        return 16384;
    uint32_t s = ((uint32_t)(i - iw) << 14) / WF_DV_HUE_SPAN;       /* Q14 */
    return ((s * s) >> 14) * (49152 - 2 * s) >> 14;
}

/* the asm's pre-test: R and G accumulators both below hk -> the luma index is below the knee ik (T = 2: no shift, w 0).
   la <= (a0 c0 + a1 c1) / 2^16 (c2 <= 0, a2 >= 0) < hk (c0 + c1) / 2^16 <= xk << sh, xk the least x with
   idx(x) >= ik - 256: so idx(la >> sh) + 256 < ik. Only a shortcut: the full path gives the same words. */
static uint32_t wf_dv_hue_hk(int ik, int sh, const int16_t *lw)
{
    int n = ik - 256;
    if (n <= 0 || lw[0] <= 0 || lw[1] <= 0 || lw[2] > 0)
        return 0;
    uint32_t xk;
    if (n < 512)
        xk = (uint32_t)n;                        /* idx(x) = x below 512 */
    else {
        int s = (n >> 8) - 1;                    /* idx = (s << 8) + (x >> s), x >> s in 256 .. 511 */
        xk = (uint32_t)(n - (s << 8)) << s;
    }
    uint64_t q = wf_dv_udiv64((uint64_t)xk << (sh + 16), (uint32_t)(lw[0] + lw[1]));
    return q > 0x7fffffffu ? 0x7fffffffu : (uint32_t)q;
}

static inline int32_t wf_dv_smulw(int32_t a, int32_t c) { return (int32_t)(((int64_t)a * c) >> 16); }

/* the C reference of the shoulder I / D lookups: the enc indices of one pixel from its non-negative accumulators a[k]
   (x = a >> sh, capped at WF_DEV_XMAX as the paths do), sp->ht / lc0 / lc2 */
static inline void wf_dv_hue_idx(const struct wf_dv_span *sp, int sh, const int32_t *a, int *ix)
{
    const uint32_t *T = sp->ht;
    int32_t la = wf_dv_smulw(a[0], (int16_t)(sp->lc0 & 0xffff)) + wf_dv_smulw(a[1], (int16_t)(sp->lc0 >> 16))
               + wf_dv_smulw(a[2], sp->lc2);
    uint32_t xl = (uint32_t)(la < 0 ? 0 : la) >> sh;
    uint32_t tm = T[wf_dv_idx(xl > WF_DEV_XMAX ? WF_DEV_XMAX : xl) + 256];
    int32_t w = (int32_t)(tm >> 16), dq = (int32_t)(int16_t)(tm & 0xffff) * (16384 - w);
    for (int k = 0; k < 3; k++) {
        uint32_t x = (uint32_t)a[k] >> sh;
        int i = wf_dv_idx(x > WF_DEV_XMAX ? WF_DEV_XMAX : x);
        i += (dq + (int32_t)(int16_t)(T[i] & 0xffff) * w) >> 16;
        ix[k] = i < 0 ? 0 : i > WF_DEV_NENC - 1 ? WF_DEV_NENC - 1 : i;
    }
}

static void wf_dv_roll(struct wf_dv_ctx *c, const int32_t m[3], struct wf_dev_stat *st)
{
    const struct wf_dev_tabs *t = c->t;
    const uint32_t *E = t->enc[0];
    uint16_t *Y = c->ytab;
    int32_t mx = m[0], mn = m[0];
    for (int k = 1; k < 3; k++) {
        if (m[k] > mx) mx = m[k];
        if (m[k] < mn) mn = m[k];
    }
    /* x Q14 = raw x m / 2^8 (lin Q24) / 2^10 x gain / 2^16 */
    int64_t xc = ((int64_t)c->cl * mx >> 18) * t->gain >> 16, x1 = ((int64_t)c->thr * mn >> 18) * t->gain >> 16;
    if (xc > WF_DEV_XMAX) xc = WF_DEV_XMAX;
    if (x1 > WF_DEV_XMAX) x1 = WF_DEV_XMAX;
    int ic = wf_dv_idx((uint32_t)xc), i1 = wf_dv_idx((uint32_t)x1);
    int i0 = i1 - 1024 > 0 ? i1 - 1024 : 0;      /* the look's luma from 4 octaves below the first clip */
    int ym = 0;
    for (int i = i0; i < WF_DEV_NENC; i++) {
        uint32_t a0, a1;
        wf_dv_tet(t->node, E[i], E[WF_DEV_NENC + i], E[2 * WF_DEV_NENC + i], &a0, &a1);
        int y = (int)(a0 & 0xffff) - 128;
        if (y > ym) ym = y;                      /* monotone (a cube's neutral axis may wiggle) */
        Y[i] = (uint16_t)ym;
    }
    int ytop = ym - 256, iw = i0;
    if (wf_dev_roll_lift >= 0) {
        int it = ic + wf_dev_roll_lift < WF_DEV_NENC - 1 ? ic + wf_dev_roll_lift : WF_DEV_NENC - 1;
        if (Y[it] < ytop)
            ytop = Y[it];
    }
    while (iw < WF_DEV_NENC - 1 && Y[iw] < ytop) iw++;
    int ik = i1 - wf_dev_roll_knee;
    if (ik > iw - 256) ik = iw - 256;
    if (ik < i0 + 32) ik = i0 + 32;
    if (ytop <= 0 || ic <= ik + 32 || ytop <= Y[ik] + 256)
        return;                                  /* no room for a shoulder: the look as is */
    int yk = Y[ik], dy = ytop - yk, span = ic - ik;
    int64_t m0 = ((Y[ik + 16] - Y[ik - 16]) * span) >> 5;   /* the look's slope at the knee, over the span (< 2^29) */
    if (m0 < 0) m0 = 0;
    if (m0 > 3 * (int64_t)dy) m0 = 3 * (int64_t)dy;                 /* the Hermite stays monotone */
    int hue = c->j.hue == WF_DEV_HUE_I || c->j.hue == WF_DEV_HUE_D ? c->j.hue : WF_DEV_HUE_A;
    if (t->src != WF_DEV_CUBE || t->curve < 1 || t->curve > WF_LL_NCURVE)
        hue = WF_DEV_HUE_A;                      /* I / D need the log gamut's luma */
    uint32_t *T = c->enc2;
    if (hue)
        for (int i = 0; i < ik; i++)
            T[i] = 2;                            /* no shift (+ the rounding), w 0 */
    else
        for (int ch = 0; ch < 3; ch++)
            wf_copyw(c->enc2 + ch * WF_DEV_NENC, E + ch * WF_DEV_NENC, ik);
    int j = i0;
    for (int i = ik; i < WF_DEV_NENC; i++) {
        int yt;
        if (i >= ic) {
            yt = ytop;
        } else {                                 /* Hermite, s Q16: m0 (s^3 - 2 s^2 + s) + dy (3 s^2 - 2 s^3) */
            int64_t s = ((i - ik) << 16) / span, s2 = s * s >> 16, s3 = s2 * s >> 16;   /* 32-bit division */
            int64_t h = m0 * (s3 - 2 * s2 + s) + (int64_t)dy * (3 * s2 - 2 * s3);
            yt = yk + (int)(h >> 16);
        }
        while (j < WF_DEV_NENC - 1 && Y[j + 1] < yt) j++;
        int j1 = j + 1 < WF_DEV_NENC ? j + 1 : j, f = 0;   /* Y[j] < yt <= Y[j1]: fraction Q7 between them */
        if (Y[j1] > Y[j] && yt > Y[j])
            f = ((yt - Y[j]) << 7) / (Y[j1] - Y[j]);
        if (f > 128) f = 128;
        if (hue) {                               /* Q7 shift -> quarter steps */
            int32_t s = (((j << 7) + f - (i << 7)) + 16) >> 5;
            T[i] = (uint32_t)(uint16_t)(s + 2) | wf_dv_hue_w(hue, i, iw) << 16;
            continue;
        }
        for (int ch = 0; ch < 3; ch++) {
            const uint32_t *e = E + ch * WF_DEV_NENC;
            int32_t g0 = (int32_t)wf_dv_g7(ch, e[j]), g1 = (int32_t)wf_dv_g7(ch, e[j1]);
            c->enc2[ch * WF_DEV_NENC + i] = wf_dv_entry(ch, g0 + (((g1 - g0) * f + 64) >> 7));
        }
    }
    if (hue) {
        for (int i = WF_DEV_NENC; i < WF_DV_HUE_NT; i++)
            T[i] = T[WF_DEV_NENC - 1];
        c->hue = hue;
        c->sp.ht = T;                            /* the enc tables stay t->enc */
    } else {
        c->enc = c->enc2;
    }
    st->roll = 1;
    st->ro_k = ik;
    st->ro_c = ic;
    st->ro_w = iw;
    st->ro_grey = wf_dv_idx((uint32_t)(((int64_t)2949 * t->gain) >> 16));   /* 0.18 in Q14, after the LUT EI */
    st->ro_ytop = ytop;
}

int wf_dev_prepare(void *scratch, const struct wf_dev_tabs *t, const struct wf_dev_job *jin, struct wf_dev_stat *st)
{
    static struct wf_dev_stat st0;
    if (!st)
        st = &st0;
    {
        char *z = (char *)st;
        for (unsigned i = 0; i < sizeof *st; i++) z[i] = 0;
    }
    if (!scratch || !t || !jin || jin->w < 64 || jin->h < 64 || (jin->w & 31) || (jin->h & 1) || (jin->x0 & 1)
        || (jin->y0 & 1) || jin->cfa < 0 || jin->cfa > 3 || jin->x0 - WF_DV_M < 0 || jin->x0 < jin->ax0
        || jin->x0 + jin->w > jin->ax1 || jin->y0 < jin->ay0 || jin->y0 + jin->h > jin->ay1
        || wf_dv_rowbytes(jin) > jin->pitch
        || jin->white - jin->black < 1024)
        return -1;
    uint8_t *p = wf_dv_al((uint8_t *)scratch);
    struct wf_dv_ctx *c = (struct wf_dv_ctx *)p;
    p += (sizeof *c + 31) & ~31u;
    c->j = *jin;
    c->t = t;
    c->st = st;
    int rb = wf_dv_geom(jin->w, jin->h, &c->rw, &c->rs, &c->mw, &c->bw, &c->bh);
    c->xs8 = (jin->x0 - WF_DV_M) & ~7;
    c->ox = jin->x0 - c->xs8;                    /* WF_DV_M .. WF_DV_M + 7, even */
    c->rw = (c->ox + jin->w + WF_DV_M + 7) & ~7;
    uint8_t *ring0 = p;
    for (int k = 0; k < 5; k++) {
        c->ring[k] = (int16_t *)(ring0 + (size_t)k * rb);
        c->ring_y[k] = -99;
    }
    p = wf_dv_al(ring0 + wf_dv_regA(jin->w, rb));
    for (int k = 0; k < 5; k++) {
        c->mask[k] = (uint32_t *)p;
        p += ((size_t)c->mw * 4 + 31) & ~31u;
    }
    c->stage = (uint32_t *)p;
    p += ((((size_t)c->rw / 8 + 2) * 14 + 8 + 31) & ~31u);
    c->out = (uint32_t *)p;
    p += ((size_t)jin->w * 2 + 31) & ~31u;
    c->sp.tmp = (int32_t *)p;
    p += WF_DV_SPAN * 24;
    c->sum = (struct wf_dv_sum *)wf_dv_al(p);
    p = (uint8_t *)(c->sum + c->bw * c->bh);
    c->need = wf_dv_al(p);
    p = c->need + c->bw * c->bh;
    c->enc2 = (uint32_t *)wf_dv_al(p);
    p = (uint8_t *)(c->enc2 + 3 * WF_DEV_NENC);
    c->ytab = (uint16_t *)wf_dv_al(p);
    c->enc = t->enc[0];
    c->cl = jin->white - jin->black;
    c->thr = c->cl - c->cl / 32;                 /* 97 % */
    c->hl_px = 0;

    /* channel scales m (raw unit -> linear Q24, x 256) */
    int32_t m[3];
    const int32_t *day = wf_dev_day[t->cam];
    uint32_t mg = ((29746152u * 16u) / (uint32_t)c->cl) * 16u;    /* default: clip = 0.18 x 2^3.3 = 1.773 */
    for (int k = 0; k < 3; k++) m[k] = (int32_t)(((uint64_t)mg * (uint32_t)day[k]) >> 12);
    st->wb_src = 0;
    struct wf_dv_fit F;
    {
        char *z = (char *)&F;
        for (unsigned i = 0; i < sizeof F; i++) z[i] = 0;
    }
    c->iqr = (int32_t)wf_dv_udiv64((uint64_t)day[1] << 12, (uint32_t)day[0]);   /* provisional neutral (the fill runs after the scales) */
    c->iqb = (int32_t)wf_dv_udiv64((uint64_t)day[1] << 12, (uint32_t)day[2]);
    wf_dv_passA(c, ring0, &F);
    if (jin->wb_q8[0] > 0 && jin->wb_q8[1] > 0 && jin->wb_q8[2] > 0) {
        for (int k = 0; k < 3; k++) m[k] = jin->wb_q8[k];
        st->wb_src = 2;
    } else if (jin->canon) {
        static const uint32_t need[3] = { 64, 64, 16 };
        for (int tier = 0; tier < 3; tier++) {
            if (F.n[tier] < need[tier])
                continue;
            int32_t f[3];
            for (int k = 0; k < 3; k++) {
                uint64_t r = k == 1 ? (uint64_t)F.r[tier][1] : (uint64_t)F.r[tier][k] * 2;   /* G sums hold G1 + G2 */
                f[k] = (int32_t)wf_dv_udiv64(F.t[tier][k] * 512u, r);
            }
            if (f[1] > (int32_t)(mg >> 3) && f[1] < (int32_t)(mg << 3) && f[0] > 0 && f[2] > 0) {
                for (int k = 0; k < 3; k++) m[k] = f[k];
                st->wb_src = 1;
                st->fit_n = (int)F.n[tier];
                st->fit_tier = tier;
            }
            break;
        }
    }
    for (int k = 0; k < 3; k++) st->m_q8[k] = m[k];
    c->qr = (int32_t)wf_dv_udiv64((uint64_t)m[0] << 12, (uint32_t)m[1]);
    c->qb = (int32_t)wf_dv_udiv64((uint64_t)m[2] << 12, (uint32_t)m[1]);
    c->iqr = (int32_t)wf_dv_udiv64((uint64_t)m[1] << 12, (uint32_t)m[0]);
    c->iqb = (int32_t)wf_dv_udiv64((uint64_t)m[1] << 12, (uint32_t)m[2]);
    wf_dv_fill(c);
    /* blocks next to a block with a clipped quad also need the path (3x3 windows cross block edges) */
    for (int by = 0; by < c->bh; by++)
        for (int bx = 0; bx < c->bw; bx++)
            if (c->need[by * c->bw + bx] & 1) {
                st->hl_blocks++;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int x = bx + dx, y = by + dy;
                        if (x >= 0 && y >= 0 && x < c->bw && y < c->bh)
                            c->need[y * c->bw + x] |= 2;
                    }
            }

    /* the matrix: gain x gm x diag(m), raw unit -> x Q14 (= lin Q24 >> 10), scaled by 2^sh */
    int64_t Mf[9];                               /* x Q14 per raw unit, Q24 */
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) {
            int64_t a = ((int64_t)t->gm[3 * r + k] * m[k]) >> 20;   /* lin Q24 per raw unit, x 256 */
            Mf[3 * r + k] = (a * t->gain) >> 10;                    /* x 2^16 gain, / 2^10 (Q24 -> Q14), x 256: Q24 */
        }
    int sh = 24, ok;
    for (;;) {
        ok = 1;
        for (int r = 0; r < 3; r++) {
            int64_t s = 0;
            for (int k = 0; k < 3; k++) {
                int64_t v = Mf[3 * r + k] >> (24 - sh);
                s += v < 0 ? -v : v;
            }
            if (s * WF_DV_RAWMAX >= ((int64_t)1 << 31) - (1 << sh))
                ok = 0;
            for (int k = 0; k < 3; k++)          /* 16-bit coefficients for the asm kernel's SMLAxy */
                if ((Mf[3 * r + k] + (sh < 24 ? (int64_t)1 << (23 - sh) : 0)) >> (24 - sh) > 32767
                    || (Mf[3 * r + k] + (sh < 24 ? (int64_t)1 << (23 - sh) : 0)) >> (24 - sh) < -32768)
                    ok = 0;
        }
        if (ok || sh == 0)
            break;
        sh--;
    }
    for (int k = 0; k < 9; k++) {
        int64_t v = Mf[k] + (sh < 24 ? (int64_t)1 << (23 - sh) : 0);
        c->M[k] = (int32_t)(v >> (24 - sh));
    }
    c->sh = sh;
    /* the span kernel: x = acc >> sh < 2^31 >> 7 = 2^24 (no upper clamp); MHC values of 14-bit raw stay within
       (28 x 16383) / 16 < 2^15 (int16 operands of SMLAxy) whatever the black level */
    c->fast = ok && sh >= 7;
    for (int v = 0; v < 2; v++) {
        int a = v ? 2 : 0, b = 2 - a;            /* row colour, column colour */
        uint32_t q[9];
        for (int r = 0; r < 3; r++) {
            q[3 * r] = (uint32_t)c->M[3 * r + a] & 0xffff;
            q[3 * r + 1] = (uint32_t)c->M[3 * r + 1] & 0xffff;
            q[3 * r + 2] = (uint32_t)c->M[3 * r + b] & 0xffff;
        }
        c->wcol[v][0] = q[0] | q[1] << 16;
        c->wcol[v][1] = q[2] | q[3] << 16;
        c->wcol[v][2] = q[4] | q[5] << 16;
        c->wcol[v][3] = q[6] | q[7] << 16;
        c->wcol[v][4] = q[8];
    }
    c->hue = 0;
    c->sp.ht = 0;
    if (jin->roll && t->src != WF_DEV_LOG) {
        int t0 = jin->ms ? jin->ms() : 0;
        wf_dv_roll(c, m, st);
        if (jin->ms)
            st->ro_ms = jin->ms() - t0;
    }
    c->sp.node = t->node;
    c->sp.enc = c->enc;
    c->sp.encb = (const uint32_t *)((uintptr_t)c->enc + (uintptr_t)(intptr_t)((23 - sh) * 256 * 4));
    c->sp.k = 256u << sh;
    c->sp.sh = sh;
    if (c->hue) {                                /* the shoulder I / D: the shift table and the luma for the kernels */
        const int16_t *lw = wf_dv_hue_lw[t->curve - 1];
        c->sp.tb = c->sp.ht + (23 - sh) * 256;
        c->sp.noff = -(23 - sh) * 256;
        c->sp.vmax = WF_DEV_NENC - 1 - (23 - sh) * 256;
        c->sp.lc0 = (uint32_t)(uint16_t)lw[0] | (uint32_t)(uint16_t)lw[1] << 16;
        c->sp.lc2 = lw[2];
        c->sp.hk = wf_dv_hue_hk(st->ro_k, sh, lw);
    }
    st->hue = c->hue;
    st->sh = sh;
    st->fast = c->fast;
    return 0;
}

/* ------------------------------------------------------------------ rows */
/* the ring row of output row y (y - 2 .. y + 2 are made present by wf_dv_need) */
static int16_t *wf_dv_row(struct wf_dv_ctx *c, int y, uint32_t **mk)
{
    int s = (y + 10) % 5;
    if (c->ring_y[s] != y) {
        int16_t *r = c->ring[s];
        wf_dv_rawrow(c, wf_dv_srcrow(&c->j, y), r);
        uint32_t *m = c->mask[s];
        for (int k = 0; k < c->mw; k++) m[k] = 0;
        int yy = y < 0 ? -y : y >= c->j.h ? 2 * (c->j.h - 1) - y : y;   /* the mirrored row's own blocks */
        const uint8_t *nd = c->need + (yy / WF_DEV_BLK) * c->bw;
        const int16_t *a = r + c->ox;
        for (int bx = 0; bx < c->bw; bx++)
            if (nd[bx]) {
                uint32_t w = 0;
                for (int i = 0; i < 32; i++)
                    if (a[32 * bx + i] >= c->thr) w |= 1u << i;
                m[bx + 1] = w;
            }
        c->ring_y[s] = y;
    }
    *mk = c->mask[s];
    return c->ring[s];
}

/* x (raw -> Q14) of one pixel, the enc lookups and the tetrahedral: lane sums */
#define WF_DV_LOOK(R, G, B, A0, A1) do {                                                          \
        int32_t x0_ = (M0 * (R) + M1 * (G) + M2 * (B));                                            \
        int32_t x1_ = (M3 * (R) + M4 * (G) + M5 * (B));                                            \
        int32_t x2_ = (M6 * (R) + M7 * (G) + M8 * (B));                                            \
        if (c->hue) {                                                                              \
            int32_t a_[3] = { x0_ < 0 ? 0 : x0_, x1_ < 0 ? 0 : x1_, x2_ < 0 ? 0 : x2_ };            \
            int i_[3];                                                                             \
            wf_dv_hue_idx(&c->sp, sh, a_, i_);                                                     \
            wf_dv_tet(node, e0[i_[0]], e1[i_[1]], e2[i_[2]], &(A0), &(A1));                        \
            break;                                                                                 \
        }                                                                                          \
        x0_ >>= sh;                                                                                \
        x1_ >>= sh;                                                                                \
        x2_ >>= sh;                                                                                \
        x0_ = x0_ < 0 ? 0 : x0_ > WF_DEV_XMAX ? WF_DEV_XMAX : x0_;                                 \
        x1_ = x1_ < 0 ? 0 : x1_ > WF_DEV_XMAX ? WF_DEV_XMAX : x1_;                                 \
        x2_ = x2_ < 0 ? 0 : x2_ > WF_DEV_XMAX ? WF_DEV_XMAX : x2_;                                 \
        wf_dv_tet(node, e0[wf_dv_idx((uint32_t)x0_)], e1[wf_dv_idx((uint32_t)x1_)],                \
                  e2[wf_dv_idx((uint32_t)x2_)], &(A0), &(A1));                                     \
    } while (0)

/* the pair's UYVY word from the two pixels' lane sums (the rounding is in the nodes) */
static inline uint32_t wf_dv_word(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1)
{
    uint32_t y0 = (a0 & 0xffff) >> 8, y1 = (b0 & 0xffff) >> 8;
    uint32_t cb = ((a0 >> 16) + (b0 >> 16)) >> 9, cr = ((a1 & 0xffff) + (b1 & 0xffff)) >> 9;
    return (cb ^ 0x80) | y0 << 8 | (cr ^ 0x80) << 16 | y1 << 24;
}

/* clipped colours of the pixel at bit position b (0..31 inside word k of the row masks): window bits x-1, x, x+1 */
static inline int wf_dv_win(const uint32_t *m, int x)
{
    /* m indexed by word bx + 1; bit x - 1 .. x + 1 of output x */
    int k = (x >> 5) + 1, b = x & 31;
    uint64_t w = (uint64_t)m[k - 1] >> 31 | (uint64_t)m[k] << 1 | (uint64_t)m[k + 1] << 33;   /* bit 0 = x - 32 ... */
    return (int)((w >> b) & 7);                  /* bit 0: x - 1, bit 1: x, bit 2: x + 1 */
}

/* rebuild the clipped channels of one pixel (raw units); col = Bayer colour of the site, up / mid / dn = window bits */
static void wf_dv_hl(struct wf_dv_ctx *c, int x, int y, int col, int up, int mid, int dn, int32_t *rgb)
{
    int cl[3];
    int ud = up | dn;
    if (col == 1) {                              /* G site: G centre + diagonals; row colour on the sides; the other above / below */
        int rowc = wf_dv_col(c->j.cfa, x + 1, y); /* the non-green colour of this row */
        cl[1] = (mid & 2) || (ud & 5);
        cl[rowc] = (mid & 5) != 0;
        cl[2 - rowc] = (ud & 2) != 0;
    } else {                                     /* R or B site: itself centre, G on the cross, the other on the diagonals */
        cl[col] = (mid & 2) != 0;
        cl[1] = (mid & 5) || (ud & 2);
        cl[2 - col] = (ud & 5) != 0;
    }
    int n = cl[0] + cl[1] + cl[2];
    if (!n)
        return;
    c->hl_px++;
    /* the block map at the pixel: bilinear between block centres */
    int fx = (x << 4) - (WF_DEV_BLK << 3), fy = (y << 4) - (WF_DEV_BLK << 3);   /* x 16, centre at 16 px */
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    int bx = fx >> 9, by = fy >> 9, ax = (fx & 511) >> 1, ay = (fy & 511) >> 1;   /* 32 px x 16 = 512; weights Q8 */
    if (bx >= c->bw - 1) { bx = c->bw - 1; ax = 0; }
    if (by >= c->bh - 1) { by = c->bh - 1; ay = 0; }
    const struct wf_dv_sum *S = c->sum;
    const struct wf_dv_map *m00 = (const struct wf_dv_map *)&S[by * c->bw + bx];
    const struct wf_dv_map *m01 = ax ? m00 + 0 : m00, *m10 = m00, *m11 = m00;
    if (ax) m01 = (const struct wf_dv_map *)&S[by * c->bw + bx + 1];
    if (ay) m10 = (const struct wf_dv_map *)&S[(by + 1) * c->bw + bx];
    if (ax && ay) m11 = (const struct wf_dv_map *)&S[(by + 1) * c->bw + bx + 1];
    else if (ay) m11 = m10;
    else if (ax) m11 = m01;
#define WF_DV_BIL(f) ((((m00->f * (256 - ax) + m01->f * ax) >> 4) * (256 - ay) + ((m10->f * (256 - ax) + m11->f * ax) >> 4) * ay) >> 12)
    int32_t kr = WF_DV_BIL(kr), kb = WF_DV_BIL(kb), ikr = WF_DV_BIL(ikr), ikb = WF_DV_BIL(ikb);
#undef WF_DV_BIL
    int32_t r = rgb[0], g = rgb[1], b = rgb[2];
    if (n == 3) {
        /* all clipped: neutral, at least as bright in every white balanced channel as the block colour would be with
           each clipped channel at its value (each one is a lower bound): a coloured lamp's core then never comes out
           darker than its two-channel ring (whose G is rebuilt from B) */
        int32_t gm = g, e = (int32_t)(((int64_t)r * ikr) >> 12);
        if (e > gm) gm = e;
        e = (int32_t)(((int64_t)b * ikb) >> 12);
        if (e > gm) gm = e;
        if (gm > WF_DV_HLMAX) gm = WF_DV_HLMAX;
        int32_t rm = (int32_t)(((int64_t)gm * kr) >> 12), bm = (int32_t)(((int64_t)gm * kb) >> 12);
        if (rm < r) rm = r;
        if (bm < b) bm = b;
        int32_t v = (int32_t)(((int64_t)rm * c->qr) >> 12), vb = (int32_t)(((int64_t)bm * c->qb) >> 12);
        if (gm > v) v = gm;
        if (vb > v) v = vb;
        r = (int32_t)(((int64_t)v * c->iqr) >> 12);
        b = (int32_t)(((int64_t)v * c->iqb) >> 12);
        g = v;
    } else if (cl[1]) {
        int32_t ge;
        if (!cl[0] && !cl[2])
            ge = (int32_t)(((int64_t)r * ikr + (int64_t)b * ikb) >> 13);
        else if (!cl[2])
            ge = (int32_t)(((int64_t)b * ikb) >> 12);
        else
            ge = (int32_t)(((int64_t)r * ikr) >> 12);
        if (ge > g) g = ge;
        if (cl[0]) { int32_t e = (int32_t)(((int64_t)g * kr) >> 12); if (e > r) r = e; }
        if (cl[2]) { int32_t e = (int32_t)(((int64_t)g * kb) >> 12); if (e > b) b = e; }
    } else {
        if (cl[0]) { int32_t e = (int32_t)(((int64_t)g * kr) >> 12); if (e > r) r = e; }
        if (cl[2]) { int32_t e = (int32_t)(((int64_t)g * kb) >> 12); if (e > b) b = e; }
    }
    if (r > WF_DV_HLMAX) r = WF_DV_HLMAX;
    if (g > WF_DV_HLMAX) g = WF_DV_HLMAX;
    if (b > WF_DV_HLMAX) b = WF_DV_HLMAX;
    rgb[0] = r; rgb[1] = g; rgb[2] = b;
}

/* MHC demosaic of the pair (x, x + 1) in a row: v = rows y-2..y+2 at x. p = 0: x is the row's non-green site (colour
   nc), x + 1 green; p = 1: x green, x + 1 the non-green. Out: camera RGB of both pixels. */
static inline __attribute__((always_inline)) void
wf_dv_mhc(const int16_t *nn, const int16_t *n, const int16_t *o, const int16_t *s, const int16_t *ss, int x,
          int p, int nc, int32_t *A, int32_t *Bq)
{
    const int oc = 2 - nc;
    int xc = x + p, xg = x + 1 - p;              /* non-green site, green site */
    /* non-green site xc: G = (4C + 2 cross - axis2) / 8, other = (12C + 4 diag - 3 axis2) / 16 */
    int32_t C = o[xc], ax2 = nn[xc] + ss[xc] + o[xc - 2] + o[xc + 2];
    int32_t cr = n[xc] + s[xc] + o[xc - 1] + o[xc + 1], dg = n[xc - 1] + n[xc + 1] + s[xc - 1] + s[xc + 1];
    int32_t vg = (4 * C + 2 * cr - ax2) >> 3, vo = (12 * C + 4 * dg - 3 * ax2) >> 4;
    /* green site xg: row colour from W / E, other from N / S */
    int32_t G = o[xg], dgg = n[xg - 1] + n[xg + 1] + s[xg - 1] + s[xg + 1];
    int32_t hv = o[xg - 2] + o[xg + 2], vv = nn[xg] + ss[xg];
    int32_t vr = (10 * G + 8 * (o[xg - 1] + o[xg + 1]) - 2 * dgg - 2 * hv + vv) >> 4;
    int32_t vb = (10 * G + 8 * (n[xg] + s[xg]) - 2 * dgg - 2 * vv + hv) >> 4;
    int32_t *P = p ? Bq : A, *Q = p ? A : Bq;    /* P = the non-green pixel, Q = the green one */
    P[nc] = C; P[1] = vg; P[oc] = vo;
    Q[nc] = vr; Q[1] = G; Q[oc] = vb;
}

/* C reference of wf_dv_mhc_s: same values, same order */
static void wf_dv_mhc_c(const struct wf_dv_span *sp, int x, int n, int p)
{
    int32_t *v = sp->tmp;
    for (int i = 0; i < n; i++, x += 2, v += 6)
        wf_dv_mhc(sp->row[0], sp->row[1], sp->row[2], sp->row[3], sp->row[4], x, p, 0, v, v + 3);
}

static inline int32_t wf_dv_lo16(uint32_t w) { return (int16_t)(w & 0xffff); }
static inline int32_t wf_dv_hi16(uint32_t w) { return (int16_t)(w >> 16); }

/* C reference of wf_dv_lut_s; with the shoulder I / D (sp->ht) of wf_dv_span_hs, counting its paths (host tests: the
   pre-test's plain lookups, w 0, w > 0; the C reference never runs on the camera) */
uint32_t wf_dev_hue_paths[3];
static void wf_dv_lut_c(const struct wf_dv_span *sp, int n, uint32_t *out)
{
    const uint32_t *w = sp->w, *e0 = sp->enc, *e1 = e0 + WF_DEV_NENC, *e2 = e1 + WF_DEV_NENC;
    const int32_t *v = sp->tmp;
    for (int i = 0; i < n; i++, v += 6) {
        uint32_t a[2][2];
        for (int e = 0; e < 2; e++) {
            int32_t r = wf_dv_lo16((uint32_t)v[3 * e]), g = wf_dv_lo16((uint32_t)v[3 * e + 1]),
                    b = wf_dv_lo16((uint32_t)v[3 * e + 2]);
            int32_t x0 = r * wf_dv_lo16(w[0]) + g * wf_dv_hi16(w[0]) + b * wf_dv_lo16(w[1]);
            int32_t x1 = r * wf_dv_hi16(w[1]) + g * wf_dv_lo16(w[2]) + b * wf_dv_hi16(w[2]);
            int32_t x2 = r * wf_dv_lo16(w[3]) + g * wf_dv_hi16(w[3]) + b * wf_dv_lo16(w[4]);
            if (sp->ht) {                        /* the shoulder I / D (wf_dv_span_hs) */
                int32_t ac[3] = { x0 < 0 ? 0 : x0, x1 < 0 ? 0 : x1, x2 < 0 ? 0 : x2 };
                int ix[3];
                wf_dv_hue_idx(sp, sp->sh, ac, ix);
                if ((uint32_t)ac[0] < sp->hk && (uint32_t)ac[1] < sp->hk)
                    wf_dev_hue_paths[0]++;       /* the asm's paths, for the test: the pre-test's plain lookups */
                else {
                    int32_t la = wf_dv_smulw(ac[0], (int16_t)(sp->lc0 & 0xffff))
                               + wf_dv_smulw(ac[1], (int16_t)(sp->lc0 >> 16)) + wf_dv_smulw(ac[2], sp->lc2);
                    uint32_t xl = (uint32_t)(la < 0 ? 0 : la) >> sp->sh;
                    wf_dev_hue_paths[sp->ht[wf_dv_idx(xl > WF_DEV_XMAX ? WF_DEV_XMAX : xl) + 256] >> 16 ? 2 : 1]++;
                }
                wf_dv_tet(sp->node, e0[ix[0]], e1[ix[1]], e2[ix[2]], &a[e][0], &a[e][1]);
                continue;
            }
            x0 = (x0 < 0 ? 0 : x0) >> sp->sh;
            x1 = (x1 < 0 ? 0 : x1) >> sp->sh;
            x2 = (x2 < 0 ? 0 : x2) >> sp->sh;
            wf_dv_tet(sp->node, e0[wf_dv_idx((uint32_t)x0)], e1[wf_dv_idx((uint32_t)x1)], e2[wf_dv_idx((uint32_t)x2)],
                      &a[e][0], &a[e][1]);
        }
        out[i] = wf_dv_word(a[0][0], a[0][1], a[1][0], a[1][1]);
    }
}

#ifdef WF_DEV_NO_ASM
static int wf_dv_ref = 1;
void wf_dev_kernel(int ref) { (void)ref; }
#define wf_dv_span_s(sp, x, n, o) ((void)(sp), (void)(x), (void)(n), (void)(o))
#define wf_dv_span_hs(sp, x, n, o) ((void)(sp), (void)(x), (void)(n), (void)(o))
#else
static int wf_dv_ref;
void wf_dev_kernel(int ref) { wf_dv_ref = ref; }
#endif

/* output pixels [xa, xb) of the row through the span kernel (c->sp set for the row) */
static void wf_dv_fastspan(struct wf_dv_ctx *c, int xa, int xb, int p)
{
    if (!wf_dv_ref) {
        if (c->hue)
            wf_dv_span_hs(&c->sp, xa, (xb - xa) >> 1, c->out + (xa >> 1));
        else
            wf_dv_span_s(&c->sp, xa, (xb - xa) >> 1, c->out + (xa >> 1));
        return;
    }
    for (int x = xa; x < xb; x += 2 * WF_DV_SPAN) {
        int n = (xb - x) >> 1;
        if (n > WF_DV_SPAN)
            n = WF_DV_SPAN;
        wf_dv_mhc_c(&c->sp, x, n, p);
        wf_dv_lut_c(&c->sp, n, c->out + (x >> 1));
    }
}

/* one pixel pair through the highlight path: MHC, the rebuild of each pixel with a clipped pixel in its 3x3, the
   matrix with k bits fewer when rebuilt channels exceed WF_DV_RAWMAX, the clamps, enc, tetrahedral, word */
static inline __attribute__((always_inline)) void
wf_dv_hlpair(struct wf_dv_ctx *c, int x, int y, int p, int nc, const int16_t *const *R5, uint32_t *const *mk)
{
    const int32_t *M = c->M;
    const int sh = c->sh;
    const uint32_t *node = c->t->node, *e0 = c->enc, *e1 = e0 + WF_DEV_NENC, *e2 = e1 + WF_DEV_NENC;
    int32_t A[3], B[3];
    uint32_t a[2][2];
    wf_dv_mhc(R5[0], R5[1], R5[2], R5[3], R5[4], x, p, nc, A, B);
    for (int e = 0; e < 2; e++) {
        int xx = x + e;
        int32_t *P = e ? B : A;
        int up = wf_dv_win(mk[1], xx), mid = wf_dv_win(mk[2], xx), dn = wf_dv_win(mk[3], xx);
        if (up | mid | dn)
            wf_dv_hl(c, xx, y, wf_dv_col(c->j.cfa, xx, y), up, mid, dn, P);
        /* rebuilt channels may exceed WF_DV_RAWMAX: drop k bits of the raw and k of the shift */
        int32_t mx = P[0] > P[1] ? P[0] : P[1];
        if (P[2] > mx) mx = P[2];
        int k = 0;
        while ((mx >> k) > WF_DV_RAWMAX && k < sh) k++;
        const int shk = sh - k;
        const int32_t R = P[0] >> k, G = P[1] >> k, Bq = P[2] >> k;
        int32_t x0 = (M[0] * R + M[1] * G + M[2] * Bq);
        int32_t x1 = (M[3] * R + M[4] * G + M[5] * Bq);
        int32_t x2 = (M[6] * R + M[7] * G + M[8] * Bq);
        if (c->hue) {                            /* the shoulder I / D */
            int32_t ac[3] = { x0 < 0 ? 0 : x0, x1 < 0 ? 0 : x1, x2 < 0 ? 0 : x2 };
            int ix[3];
            wf_dv_hue_idx(&c->sp, shk, ac, ix);
            wf_dv_tet(node, e0[ix[0]], e1[ix[1]], e2[ix[2]], &a[e][0], &a[e][1]);
            continue;
        }
        x0 >>= shk;
        x1 >>= shk;
        x2 >>= shk;
        x0 = x0 < 0 ? 0 : x0 > WF_DEV_XMAX ? WF_DEV_XMAX : x0;
        x1 = x1 < 0 ? 0 : x1 > WF_DEV_XMAX ? WF_DEV_XMAX : x1;
        x2 = x2 < 0 ? 0 : x2 > WF_DEV_XMAX ? WF_DEV_XMAX : x2;
        wf_dv_tet(node, e0[wf_dv_idx((uint32_t)x0)], e1[wf_dv_idx((uint32_t)x1)], e2[wf_dv_idx((uint32_t)x2)],
                  &a[e][0], &a[e][1]);
    }
    c->out[x >> 1] = wf_dv_word(a[0][0], a[0][1], a[1][0], a[1][1]);
}

/* one output row y into c->out */
static void wf_dv_rowk(struct wf_dv_ctx *c, int y)
{
    const struct wf_dev_job *j = &c->j;
    uint32_t *mk[5];
    const int16_t *R5[5];
    for (int k = 0; k < 5; k++)
        R5[k] = wf_dv_row(c, y - 2 + k, &mk[k]) + c->ox;
    const int16_t *nn = R5[0], *n = R5[1], *o = R5[2], *s = R5[3], *ss = R5[4];
    const uint32_t *node = c->t->node;
    const uint32_t *e0 = c->enc, *e1 = e0 + WF_DEV_NENC, *e2 = e1 + WF_DEV_NENC;
    const int32_t M0 = c->M[0], M1 = c->M[1], M2 = c->M[2], M3 = c->M[3], M4 = c->M[4], M5 = c->M[5],
                  M6 = c->M[6], M7 = c->M[7], M8 = c->M[8];
    const int sh = c->sh;
    const int c0 = wf_dv_col(j->cfa, 0, y), c1 = wf_dv_col(j->cfa, 1, y);
    const int p = c0 == 1, nc = p ? c1 : c0;     /* x even: green (p = 1) or the row's non-green colour */
    const uint8_t *nd = c->need + (y / WF_DEV_BLK) * c->bw;
    uint32_t *out = c->out;
    const int W = j->w;
    if (c->fast) {
        for (int k = 0; k < 5; k++)
            c->sp.row[k] = R5[k];
        for (int k = 0; k < 5; k++)
            c->sp.w[k] = c->wcol[nc == 2][k];
        c->sp.p = p;
        int xs = -1;                             /* start of the pending run of clean pairs */
        for (int bx = 0; bx < c->bw; bx++) {
            int xa = bx * WF_DEV_BLK, xb = xa + WF_DEV_BLK;
            if (xb > W) xb = W;
            if (!nd[bx]) {
                if (xs < 0) xs = xa;
                continue;
            }
            /* bit i of D: a clipped pixel in rows y-1 .. y+1, columns i-1 .. i+1 (pixel i needs the rebuild) */
            uint32_t Op = mk[1][bx] | mk[2][bx] | mk[3][bx], O = mk[1][bx + 1] | mk[2][bx + 1] | mk[3][bx + 1],
                     On = mk[1][bx + 2] | mk[2][bx + 2] | mk[3][bx + 2];
            uint32_t D = O | O << 1 | O >> 1 | Op >> 31 | On << 31;
            for (int x = xa; x < xb; x += 2) {
                if (!((D >> (x & 31)) & 3)) {
                    if (xs < 0) xs = x;
                    continue;
                }
                if (xs >= 0) {
                    wf_dv_fastspan(c, xs, x, p);
                    xs = -1;
                }
                wf_dv_hlpair(c, x, y, p, nc, R5, mk);
            }
        }
        if (xs >= 0)
            wf_dv_fastspan(c, xs, W, p);
        return;
    }
    for (int bx = 0; bx < c->bw; bx++) {
        int xa = bx * WF_DEV_BLK, xb = xa + WF_DEV_BLK;
        if (xb > W) xb = W;
        if (!nd[bx]) {
            if (p == 0) {
                for (int x = xa; x < xb; x += 2) {
                    int32_t A[3], B[3];
                    uint32_t a0, a1, b0, b1;
                    wf_dv_mhc(nn, n, o, s, ss, x, 0, nc, A, B);
                    WF_DV_LOOK(A[0], A[1], A[2], a0, a1);
                    WF_DV_LOOK(B[0], B[1], B[2], b0, b1);
                    out[x >> 1] = wf_dv_word(a0, a1, b0, b1);
                }
            } else {
                for (int x = xa; x < xb; x += 2) {
                    int32_t A[3], B[3];
                    uint32_t a0, a1, b0, b1;
                    wf_dv_mhc(nn, n, o, s, ss, x, 1, nc, A, B);
                    WF_DV_LOOK(A[0], A[1], A[2], a0, a1);
                    WF_DV_LOOK(B[0], B[1], B[2], b0, b1);
                    out[x >> 1] = wf_dv_word(a0, a1, b0, b1);
                }
            }
        } else {
            for (int x = xa; x < xb; x += 2)
                wf_dv_hlpair(c, x, y, p, nc, R5, mk);
        }
    }
}

void wf_dev_rows(void *scratch, int ya, int yb, uint32_t *dst, int dst_pitch)
{
    struct wf_dv_ctx *c = (struct wf_dv_ctx *)wf_dv_al((uint8_t *)scratch);
    if (ya < 0) ya = 0;
    if (yb > c->j.h) yb = c->j.h;
    for (int y = ya; y < yb; y++) {
        wf_dv_rowk(c, y);
        wf_copyw((uint32_t *)((uint8_t *)dst + (size_t)(y - ya) * dst_pitch), c->out, c->j.w >> 1);
    }
    c->st->hl_px = c->hl_px;
}

void wf_dev_rows_win(void *scratch, int ya, int yb, uint32_t *dst, int dst_pitch, int x, int n)
{
    struct wf_dv_ctx *c = (struct wf_dv_ctx *)wf_dv_al((uint8_t *)scratch);
    x &= ~1;
    if (x < 0) x = 0;
    if (n > c->j.w - x) n = c->j.w - x;
    n &= ~1;
    if (ya < 0) ya = 0;
    if (yb > c->j.h) yb = c->j.h;
    for (int y = ya; y < yb; y++) {
        wf_dv_rowk(c, y);
        if (n > 0)
            wf_copyw((uint32_t *)((uint8_t *)dst + (size_t)(y - ya) * dst_pitch), c->out + (x >> 1), n >> 1);
    }
    c->st->hl_px = c->hl_px;
}
