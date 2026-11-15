/* wf_mist.c - diffusion filter ("Mist") for Canon UYVY frames. Model, API and order of work: wf_mist.h.
 * Clean-room (optics + a public product description); the display-model tables are yh_tables.h's (baked, -DYH_BAKED:
 * the camera core exports no pow / tanh, see yh_halo.c). Integer per pixel (ARMv5T, no divide, no float); mist_init
 * uses float once (soft float, a few hundred operations, wf_task). Memory: the context + one block of
 * mist_mem_bytes() from the caller; nothing is allocated here.
 *
 * GUESSES (no film or filter data behind them, tuned by eye on synthetic scenes and a photo): k per strength, the
 * three component sizes and their weights, the black filter's source curve Lh, MIST_GMAX and MIST_R0 (docs/README_EOSM.txt). */
#include "wf_mist.h"
#include <string.h>
#include <math.h>
#include "yh_tables.h"

#define RMAX 7                          /* taps per side of a blur; 16 ring slots (no divide: slot = row & 15) */
#define NSLOT 16
#define LV_B 2                          /* B is blurred at 1/64, C at 1/256 of the frame */
#define LV_C 4
#define GW_MAX (MIST_MAXW / MIST_CELL)
#define GH_MAX (MIST_MAXH / MIST_CELL)
#define XROW (GW_MAX + 2)

static const float mist_k[4] = { 0.08f, 0.15f, 0.28f, 0.45f };           /* GUESS: scattered fraction at 1/8 1/4 1/2 1 (v2 11-12: x2..2.7 after the owner's camera test) */
static const float mist_sig[3] = { 31.f, 124.f, 467.f };                /* GUESS: sigma in px at 5184 px width, 50 mm equivalent */
static const float mist_mixw[3] = { 0.25f, 0.35f, 0.40f };              /* GUESS: white: A local halo, B mid glow, C veil */
static const float mist_mixb[3] = { 0.55f, 0.35f, 0.10f };              /* GUESS: black: more local, little veil */
#define MIST_LH 0.12f                                                    /* GUESS: black filter source knee, of the clip level */

struct mist_ctx {
    int W, H, gw, gh, level, scale_q8, ok, black;
    int lw[MIST_NLV], lh[MIST_NLV];
    int rA, rB, rC;
    int16_t kA[RMAX + 1], kB[RMAX + 1], kC[RMAX + 1];
    int wA, wB, wC;                     /* Q16 component weights incl. k */
    uint32_t tb[256];                   /* own light at luma code v: lin20 (1 - k w), Q20 of the clip level */
    uint32_t oc[256];                   /* own chroma factor: dlin/dcode (1 - k w) */
    uint32_t sl[256], sc[256];          /* source light lin20 w, source chroma factor (dlin/dcode w) >> 2 */
    uint32_t rd[256];                   /* 2^24 / (dlin/dcode) */
    struct { uint32_t tb, oc, rd, pad; } T[256];   /* the same three, interleaved: one base register in the pair loop */
    uint8_t enc[3072];                  /* encB | encC of yh_tables.h in one array (one base register) */
    uint16_t gt[256];                   /* ramp gain of the brightest channel code, Q8 (codes >= MIST_R0: gtop[hot]) */
    uint16_t gtop[MIST_NHOT][256 - MIST_R0];   /* that ramp for GMAX^(h / 8), h = 0..8 (mist_set_hot) */
    int hot;
    uint8_t moff[1024];                 /* conservative max(R'-Y, G'-Y, B'-Y, 0) per (U>>3, V>>3) */
    int32_t *L0, *G, *P[MIST_NLV], *B2, *B4, *ring, *rowL, *rowU, *rowV, *seam;
    uint32_t *acc;                      /* 3 per cell: light, U, V sums of the current cell row */
    int xi0[GW_MAX], xi1[GW_MAX], xf[GW_MAX];
    int ring_have[NSLOT];
    int cur_cy, row_y, have_top, seam_cy;
};

uint32_t mist_ctx_bytes(void) { return (sizeof(struct mist_ctx) + 31u) & ~31u; }
int mist_k_q16(int level) { return (int)(mist_k[(level - 1) & 3] * 65536.0f + 0.5f); }
int mist_black(int level) { return level > 4; }

static void sizes(int gw, int gh, int *lw, int *lh)
{
    lw[0] = gw; lh[0] = gh;
    for (int k = 1; k < MIST_NLV; k++) { lw[k] = (lw[k - 1] + 1) >> 1; lh[k] = (lh[k - 1] + 1) >> 1; }
}
#define AL4(n) (((n) + 3u) & ~3u)
uint32_t mist_mem_bytes(void)
{
    int lw[MIST_NLV], lh[MIST_NLV];
    uint32_t n = 0;
    sizes(GW_MAX, GH_MAX, lw, lh);
    n += 2 * AL4(12u * lw[0] * lh[0]);                              /* L0, G */
    for (int k = 1; k < MIST_NLV; k++) n += AL4(12u * lw[k] * lh[k]);
    n += AL4(12u * lw[LV_B] * lh[LV_B]) + AL4(12u * lw[LV_C] * lh[LV_C]);   /* B2, B4 */
    n += AL4(12u * NSLOT * GW_MAX);                                 /* blur ring */
    n += 3 * AL4(4u * XROW) + AL4(12u * GW_MAX) * 2;                /* rows, seam, acc */
    return n;
}

/* ---------------------------------------------------------------- init */
static int gauss(int16_t *k, float sigma_cells)
{
    if (sigma_cells < 0.15f) { k[0] = 4096; return 0; }
    int r = (int)(3.0f * sigma_cells + 0.999f);
    if (r > RMAX) r = RMAX;
    float w[RMAX + 1], s = 0;
    for (int i = 0; i <= r; i++) { w[i] = expf(-0.5f * i * i / (sigma_cells * sigma_cells)); s += i ? 2 * w[i] : w[i]; }
    int sum = 0;
    for (int i = 0; i <= r; i++) { k[i] = (int16_t)(w[i] / s * 4096.0f + 0.5f); sum += i ? 2 * k[i] : k[i]; }
    k[0] += (int16_t)(4096 - sum);                                   /* taps sum to exactly 4096 */
    return r;
}

int mist_init(mist_ctx *g, int level, int scale_q8, void *mem)
{
    if (level < 1 || level > MIST_MAX) return 0;
    int black = level > 4;
    float k = mist_k[(level - 1) & 3];
    const float *mix = black ? mist_mixb : mist_mixw;
    g->level = level; g->black = black; g->ok = 0;
    g->scale_q8 = scale_q8 < 64 ? 64 : scale_q8 > 1024 ? 1024 : scale_q8;
    float sc = g->scale_q8 / 256.0f;
    g->rA = gauss(g->kA, mist_sig[0] * sc / (float)MIST_CELL);
    g->rB = gauss(g->kB, mist_sig[1] * sc / (float)(MIST_CELL << LV_B));
    g->rC = gauss(g->kC, mist_sig[2] * sc / (float)(MIST_CELL << LV_C));
    g->wA = (int)(k * mix[0] * 65536.0f + 0.5f);
    g->wB = (int)(k * mix[1] * 65536.0f + 0.5f);
    g->wC = (int)(k * mix[2] * 65536.0f + 0.5f);
    /* per luma code: linear light (Q20 of the clip level), slope of the display curve (lin per code) */
    float lh2 = MIST_LH * MIST_LH;
    /* source chroma slope cap: the log slope of the display curve at mid grey (code 128), d = gam * lin / v above it */
    float gam;
    {
        uint32_t l0 = (yh_t_lin_q30[127] + 512u) >> 10, l1 = (yh_t_lin_q30[129] + 512u) >> 10, lm = (yh_t_lin_q30[128] + 512u) >> 10;
        gam = (float)(int32_t)(l1 - l0) * 0.5f * 128.0f / (float)(int32_t)lm;
    }
    for (int v = 0; v < 256; v++) {
        uint32_t lin = (yh_t_lin_q30[v] + 512u) >> 10;
        uint32_t lo = (yh_t_lin_q30[v ? v - 1 : 0] + 512u) >> 10, hi = (yh_t_lin_q30[v < 255 ? v + 1 : 255] + 512u) >> 10;
        float d = (float)(int32_t)(hi - lo) / (v == 0 || v == 255 ? 1.0f : 2.0f);   /* int casts: the camera core has no __aeabi_ui2f / f2uiz */
        float L = (float)(int32_t)lin / 1048576.0f;
        float w = black ? L * L / (L * L + lh2) : 1.0f;
        float f = 1.0f - k * w;
        g->tb[v] = (uint32_t)(int32_t)((float)(int32_t)lin * f + 0.5f);
        g->oc[v] = (uint32_t)(int32_t)(d * f + 0.5f);
        g->sl[v] = (uint32_t)(int32_t)((float)(int32_t)lin * w + 0.5f);
        float dc = v ? gam * (float)(int32_t)lin / (float)v : d;
        if (dc > d) dc = d;
        g->sc[v] = (uint32_t)(int32_t)(dc * w * 0.25f + 0.5f);
        g->rd[v] = (uint32_t)(int32_t)(16777216.0f / d + 0.5f);
        g->gt[v] = 256;
        if (v >= MIST_R0) {
            float t = (float)(v - MIST_R0) / (float)(255 - MIST_R0), s = t * t * (3.0f - 2.0f * t);
            for (int h = 0; h < MIST_NHOT; h++) {
                float gm = expf((float)h / (float)(MIST_NHOT - 1) * logf((float)MIST_GMAX));
                g->gtop[h][v - MIST_R0] = (uint16_t)(int)(256.0f + 256.0f * (gm - 1.0f) * s + 0.5f);
            }
            g->gt[v] = g->gtop[MIST_NHOT - 1][v - MIST_R0];
        }
    }
    g->hot = MIST_NHOT - 1;
    for (int v = 0; v < 256; v++) { g->T[v].tb = g->tb[v]; g->T[v].oc = g->oc[v]; g->T[v].rd = g->rd[v]; g->T[v].pad = 0; }
    memcpy(g->enc, yh_t_encB, 1024); memcpy(g->enc + 1024, yh_t_encC, 2048);   /* L < 2^16 at step 64, L < 2^20 at step 512 */
    /* the halation's exact test: brightest channel offset bound per (U >> 3, V >> 3) bin */
    for (int ub = 0; ub < 32; ub++)
        for (int vb = 0; vb < 32; vb++) {
            int mx = 0;
            for (int a = 0; a < 8; a++)
                for (int b = 0; b < 8; b++) {
                    int U = (int8_t)(ub * 8 + a), V = (int8_t)(vb * 8 + b);
                    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
                    if (rv > mx) mx = rv;
                    if (-gu > mx) mx = -gu;
                    if (bu > mx) mx = bu;
                }
            g->moff[ub | (vb << 5)] = (uint8_t)(mx > 255 ? 255 : mx);
        }
    /* memory */
    uint8_t *m = (uint8_t *)mem;
    int lw[MIST_NLV], lh[MIST_NLV];
    sizes(GW_MAX, GH_MAX, lw, lh);
    g->L0 = (int32_t *)m; m += AL4(12u * lw[0] * lh[0]);
    g->G = (int32_t *)m; m += AL4(12u * lw[0] * lh[0]);
    g->P[0] = g->L0;
    for (int i = 1; i < MIST_NLV; i++) { g->P[i] = (int32_t *)m; m += AL4(12u * lw[i] * lh[i]); }
    g->B2 = (int32_t *)m; m += AL4(12u * lw[LV_B] * lh[LV_B]);
    g->B4 = (int32_t *)m; m += AL4(12u * lw[LV_C] * lh[LV_C]);
    g->ring = (int32_t *)m; m += AL4(12u * NSLOT * GW_MAX);
    g->rowL = (int32_t *)m; m += AL4(4u * XROW);
    g->rowU = (int32_t *)m; m += AL4(4u * XROW);
    g->rowV = (int32_t *)m; m += AL4(4u * XROW);
    g->seam = (int32_t *)m; m += AL4(12u * GW_MAX);
    g->acc = (uint32_t *)m;
    g->W = g->H = g->gw = g->gh = 0;
    g->cur_cy = g->row_y = -1; g->have_top = 0;
    return 1;
}

void mist_set_hot(mist_ctx *g, int hb_q8)
{
    int h = (hb_q8 * (MIST_NHOT - 1) + 128) >> 8;
    h = h < 0 ? 0 : h > MIST_NHOT - 1 ? MIST_NHOT - 1 : h;
    if (h != g->hot) {
        memcpy(g->gt + MIST_R0, g->gtop[h], sizeof g->gtop[0]);
        g->hot = h;
    }
}

int mist_begin_frame(mist_ctx *g, int W, int H)
{
    g->ok = 0;
    if (W < MIST_CELL || H < MIST_CELL || W > MIST_MAXW || H > MIST_MAXH || (W & (MIST_CELL - 1)) || (H & (MIST_CELL - 1))
        || MIST_SEAM_BAD(H) || !g->level)
        return 0;
    g->W = W; g->H = H; g->gw = W / MIST_CELL; g->gh = H / MIST_CELL;
    sizes(g->gw, g->gh, g->lw, g->lh);
    g->seam_cy = MIST_SEAM_CY(g->gh);
    memset(g->L0, 0, 12u * g->gw * g->gh);
    memset(g->acc, 0, 12u * g->gw);
    g->cur_cy = g->row_y = -1; g->have_top = 0;
    g->ok = 1;
    return 1;
}

/* ---------------------------------------------------------------- gather */
static void flush(mist_ctx *g)
{
    int gw = g->gw;
    int32_t *d = g->L0 + 3 * gw * g->cur_cy;
    for (int i = 0; i < gw; i++) {
        d[3 * i] = (int32_t)(g->acc[3 * i] >> 11);                  /* 128 samples, Q20 -> Q16 */
        d[3 * i + 1] = (int32_t)g->acc[3 * i + 1] >> 9;             /* 128 samples, Q18 -> Q16 */
        d[3 * i + 2] = (int32_t)g->acc[3 * i + 2] >> 9;
        g->acc[3 * i] = g->acc[3 * i + 1] = g->acc[3 * i + 2] = 0;
    }
}

/* a word with a pixel in the ramp: the brightest channel code of each pixel gives its gain (all channels alike) */
static __attribute__((noinline)) void hot_src(const mist_ctx *g, uint32_t w, int Y0, int Y1, uint32_t *sL, int32_t *sU, int32_t *sV)
{
    int U = (int8_t)w, V = (int8_t)(w >> 16);
    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
    int mc = rv > -gu ? rv : -gu;
    if (bu > mc) mc = bu;
    if (mc < 0) mc = 0;
    uint32_t L = 0;
    int32_t c = 0;
    for (int k = 0; k < 2; k++) {
        int Y = k ? Y1 : Y0, m = Y + mc;
        uint32_t gq = g->gt[m > 255 ? 255 : m];
        L += (uint32_t)(((uint64_t)g->sl[Y] * gq) >> 8);
        c += (int32_t)(((uint64_t)g->sc[Y] * gq) >> 8);
    }
    *sL += L;
    *sU += U * c;
    *sV += V * c;
}

void mist_gather_chunk(mist_ctx *g, const uint32_t *p, int npix, int x0, int y)
{
    if (!g->ok || (y & 1)) return;                                  /* every 2nd row */
    int cy = y >> 4;
    if (cy != g->cur_cy) { if (g->cur_cy >= 0) flush(g); g->cur_cy = cy; }
    const uint8_t *mo = g->moff;
    const uint32_t *sl = g->sl, *sc = g->sc;
    uint32_t *acc = g->acc + 3 * (x0 >> 4);                         /* x0 % 16 == 0 */
    int nw = npix >> 1;
    for (int k = 0; k < nw; k += 8) {
        uint32_t sL = 0;
        int32_t sU = 0, sV = 0;
        int e = nw - k < 8 ? nw - k : 8;
        for (int i = 0; i < e; i++) {
            uint32_t w = p[k + i];
            int Y0 = (w >> 8) & 255, Y1 = (int)(w >> 24);
            int ym = Y0 > Y1 ? Y0 : Y1;
            if (__builtin_expect(ym + mo[((w >> 3) & 31) | ((w >> 14) & 0x3e0)] >= MIST_R0, 0)) {
                hot_src(g, w, Y0, Y1, &sL, &sU, &sV);
            } else {
                int32_t c = (int32_t)(sc[Y0] + sc[Y1]);
                sL += sl[Y0] + sl[Y1];
                sU += (int8_t)w * c;
                sV += (int8_t)(w >> 16) * c;
            }
        }
        acc[0] += sL; acc[1] += (uint32_t)sU; acc[2] += (uint32_t)sV;
        acc += 3;
    }
}

/* ---------------------------------------------------------------- build */
static void down2(const int32_t *s, int w, int h, int32_t *d, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int y0 = 2 * y, y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        for (int x = 0; x < dw; x++) {
            int x0 = 2 * x, x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            const int32_t *a = s + 3 * (y0 * w + x0), *b = s + 3 * (y0 * w + x1), *c = s + 3 * (y1 * w + x0), *e = s + 3 * (y1 * w + x1);
            for (int ch = 0; ch < 3; ch++) d[3 * (y * dw + x) + ch] = (a[ch] + b[ch] + c[ch] + e[ch] + 2) >> 2;
        }
    }
}

/* horizontal pass of one row (3 channels interleaved), edges replicated, Q12 taps */
static void hpass(const int32_t *s, int32_t *d, int w, const int16_t *k, int r)
{
    for (int x = 0; x < w; x++)
        for (int c = 0; c < 3; c++) {
            int32_t sum = k[0] * s[3 * x + c];
            for (int t = 1; t <= r; t++) {
                int a = x - t < 0 ? 0 : x - t, b = x + t >= w ? w - 1 : x + t;
                sum += k[t] * (s[3 * a + c] + s[3 * b + c]);
            }
            d[3 * x + c] = sum >> 12;
        }
}

/* separable Gaussian: dst = wgt (Q16) * blur(src), rows through a ring of horizontally blurred rows */
static void blur(mist_ctx *g, const int32_t *src, int32_t *dst, int w, int h, const int16_t *k, int r, int wgt)
{
    for (int i = 0; i < NSLOT; i++) g->ring_have[i] = -1;
    for (int y = 0; y < h; y++) {
        for (int d = -r; d <= r; d++) {
            int yy = y + d < 0 ? 0 : y + d >= h ? h - 1 : y + d, sl = yy & (NSLOT - 1);
            if (g->ring_have[sl] != yy) { hpass(src + 3 * w * yy, g->ring + 3 * w * sl, w, k, r); g->ring_have[sl] = yy; }
        }
        for (int x = 0; x < 3 * w; x++) {
            int32_t sum = k[0] * g->ring[3 * w * (y & (NSLOT - 1)) + x];
            for (int t = 1; t <= r; t++) {
                int a = y - t < 0 ? 0 : y - t, b = y + t >= h ? h - 1 : y + t;
                sum += k[t] * (g->ring[3 * w * (a & (NSLOT - 1)) + x] + g->ring[3 * w * (b & (NSLOT - 1)) + x]);
            }
            int32_t v = sum >> 12;
            dst[3 * w * y + x] = wgt == 65536 ? v : (int32_t)(((int64_t)v * wgt) >> 16);
        }
    }
}

/* dst (L0 grid) += wgt * bilinear upsample of src (level n: cells of 2^n L0 cells) */
static void up_add(mist_ctx *g, int32_t *dst, const int32_t *src, int sw, int sh, int n, int wgt)
{
    int gw = g->gw, gh = g->gh;
    for (int x = 0; x < gw; x++) {
        int u = (((2 * x + 1) << 8) >> (n + 1)) - 128, i0 = u >> 8;
        g->xf[x] = u & 255;
        g->xi0[x] = i0 < 0 ? 0 : i0 >= sw ? sw - 1 : i0;
        g->xi1[x] = i0 + 1 < 0 ? 0 : i0 + 1 >= sw ? sw - 1 : i0 + 1;
    }
    for (int y = 0; y < gh; y++) {
        int v = (((2 * y + 1) << 8) >> (n + 1)) - 128, j0 = v >> 8, fy = v & 255;
        int j1 = j0 + 1 < 0 ? 0 : j0 + 1 >= sh ? sh - 1 : j0 + 1;
        j0 = j0 < 0 ? 0 : j0 >= sh ? sh - 1 : j0;
        const int32_t *ra = src + 3 * sw * j0, *rb = src + 3 * sw * j1;
        int32_t *d = dst + 3 * gw * y;
        for (int x = 0; x < gw; x++) {
            int fx = g->xf[x], a = 3 * g->xi0[x], b = 3 * g->xi1[x];
            for (int c = 0; c < 3; c++) {
                int32_t t0 = (ra[a + c] * (256 - fx) + ra[b + c] * fx) >> 8, t1 = (rb[a + c] * (256 - fx) + rb[b + c] * fx) >> 8;
                int32_t t = (t0 * (256 - fy) + t1 * fy) >> 8;
                d[3 * x + c] += (int32_t)(((int64_t)t * wgt) >> 16);
            }
        }
    }
}

/* TOP half: the rows below the seam = the rows above it, mirrored, a LIGHT only where it reaches the seam. Going away
 * from the seam row in each column, a cell brighter than a cell all at code MIST_R0 (a light: clipped, or a lamp's hot
 * gain) is taken only while it is no brighter than the cell taken before it, else that cell again (all three channels
 * of one cell). A round light ON the seam comes out whole (its own lower half); a light above it, not touching it, leaves
 * nothing below, so no phantom glow; the rest (sky, walls) mirrors as it is. Until 11-15 the mirror was eroded instead
 * (min over +-2 cells and the rows next to it): a light on the seam lost 2 cells a side and the bottom half's pull
 * (mist_build) drew two dark lobes under it, -32 / -64 codes at white / black 1 against the whole frame, while a light
 * 24 px above the seam still left +33 / +41 under it (owner 11-15 mist strips; the 5D2's seam crosses every frame). */
static void mirror_rows(mist_ctx *g, int s)
{
    int gw = g->gw, gh = g->gh;
    int32_t tb = (int32_t)(g->sl[MIST_R0] >> 4), *pr = g->rowL;   /* Q20 a sample -> the cells' Q16 mean (flush); rowL:
                                                                    * per column the row last taken (free until the apply) */
    for (int x = 0; x < gw; x++)
        pr[x] = s - 1;
    for (int j = 0; s + j < gh; j++) {
        int sr = s - 1 - j < 0 ? 0 : s - 1 - j;
        const int32_t *src = g->L0 + 3 * gw * sr;
        int32_t *d = g->L0 + 3 * gw * (s + j);
        for (int x = 0; x < gw; x++) {
            const int32_t *c = src + 3 * x, *p = g->L0 + 3 * (gw * pr[x] + x);
            if (c[0] > tb && c[0] > p[0])
                c = p;
            else
                pr[x] = sr;
            d[3 * x] = c[0]; d[3 * x + 1] = c[1]; d[3 * x + 2] = c[2];
        }
    }
}

void mist_build(mist_ctx *g, int mode)
{
    if (!g->ok) return;
    if (g->cur_cy >= 0) { flush(g); g->cur_cy = -1; }
    int gw = g->gw, gh = g->gh, s = g->seam_cy;
    if (mode == MIST_TOP) mirror_rows(g, s);
    for (int k = 1; k < MIST_NLV; k++) down2(g->P[k - 1], g->lw[k - 1], g->lh[k - 1], g->P[k], g->lw[k], g->lh[k]);
    blur(g, g->L0, g->G, gw, gh, g->kA, g->rA, g->wA);
    blur(g, g->P[LV_B], g->B2, g->lw[LV_B], g->lh[LV_B], g->kB, g->rB, 65536);
    blur(g, g->P[LV_C], g->B4, g->lw[LV_C], g->lh[LV_C], g->kC, g->rC, 65536);
    up_add(g, g->G, g->B2, g->lw[LV_B], g->lh[LV_B], LV_B, g->wB);
    up_add(g, g->G, g->B4, g->lw[LV_C], g->lh[LV_C], LV_C, g->wC);
    if (mode == MIST_TOP) {
        memcpy(g->seam, g->G + 3 * gw * (s - 1), 12u * gw);
        g->have_top = 1;
    } else if (mode == MIST_BOTTOM && g->have_top) {
        /* pull the whole-frame glow onto the top half's seam row, the pull fading over MIST_SEAMD rows */
        int32_t *r = g->G + 3 * gw * (s - 1);
        for (int x = 0; x < 3 * gw; x++) {
            int32_t c = g->seam[x] - r[x];
            r[x] += c;
            for (int j = 0; j < MIST_SEAMD && s + j < gh; j++)
                g->G[3 * gw * (s + j) + x] += (int32_t)(((int64_t)c * ((MIST_SEAMD - j) * 256 / MIST_SEAMD)) >> 8);
        }
    }
    g->row_y = -1;
}

/* ---------------------------------------------------------------- apply */
static void row_prep(mist_ctx *g, int y)
{
    int gw = g->gw, gh = g->gh, num = 2 * y - 15, j0 = num >> 5, fy = (num & 31) << 3;
    int j1 = j0 + 1 < 0 ? 0 : j0 + 1 >= gh ? gh - 1 : j0 + 1;
    j0 = j0 < 0 ? 0 : j0 >= gh ? gh - 1 : j0;
    const int32_t *a = g->G + 3 * gw * j0, *b = g->G + 3 * gw * j1;
    for (int x = 0; x < gw; x++) {
        g->rowL[x + 1] = (a[3 * x] * (256 - fy) + b[3 * x] * fy) >> 4;               /* Q16 -> Q20 */
        g->rowU[x + 1] = (a[3 * x + 1] * (256 - fy) + b[3 * x + 1] * fy) >> 4;
        g->rowV[x + 1] = (a[3 * x + 2] * (256 - fy) + b[3 * x + 2] * fy) >> 4;
    }
    g->rowL[0] = g->rowL[1]; g->rowU[0] = g->rowU[1]; g->rowV[0] = g->rowV[1];
    g->rowL[gw + 1] = g->rowL[gw]; g->rowU[gw + 1] = g->rowU[gw]; g->rowV[gw + 1] = g->rowV[gw];
    g->row_y = y;
}

static inline int enc(uint32_t L)
{
    if (L < 4096) return yh_t_encA[L >> 2];
    if (L < 65536) return yh_t_encB[L >> 6];
    if (L < 1048576) return yh_t_encC[L >> 9];
    return 255;
}

static inline uint32_t pack(const mist_ctx *g, int U, int V, int o0, int o1, int32_t uo, int32_t vo)
{
    int32_t rdm = (int32_t)((g->rd[o0] + g->rd[o1]) >> 1);
    int u = (int)(((int64_t)uo * rdm) >> 24), v = (int)(((int64_t)vo * rdm) >> 24);
    (void)U; (void)V;
    u = u < -128 ? -128 : u > 127 ? 127 : u;
    v = v < -128 ? -128 : v > 127 ? 127 : v;
    return (uint32_t)(u & 255) | (uint32_t)o0 << 8 | (uint32_t)(v & 255) << 16 | (uint32_t)o1 << 24;
}

/* a pair with a pixel in the ramp: own light of each pixel x the gain of its darkest channel, own chroma as recorded */
static __attribute__((noinline)) uint32_t hot_pair(const mist_ctx *g, uint32_t w, int Y0, int Y1, int32_t gl, int32_t gu, int32_t gv)
{
    int U = (int8_t)w, V = (int8_t)(w >> 16);
    int rv = (359 * V) >> 8, gg = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
    int mc = rv > -gg ? rv : -gg;
    if (bu > mc) mc = bu;
    if (mc < 0) mc = 0;
    int nc = rv < -gg ? rv : -gg;                   /* darkest channel offset: the pixel's own light is boosted only when ALL its */
    if (bu < nc) nc = bu;                           /* channels are clipped (a white lamp); a coloured clipped pixel shows real G / B */
    if (nc > 0) nc = 0;
    uint32_t L[2];
    for (int k = 0; k < 2; k++) {
        int Y = k ? Y1 : Y0, m = Y + nc;
        uint32_t gq = g->gt[m < 0 ? 0 : m > 255 ? 255 : m];
        L[k] = (uint32_t)(((uint64_t)g->tb[Y] * gq) >> 8);
    }
    int o0 = enc(L[0] + (uint32_t)gl), o1 = enc(L[1] + (uint32_t)gl);
    /* own chroma: no gain (a clipped coloured core keeps its colour), back through the INPUT code of the SAME pixel it was
       read with (the brighter one), so it comes out as U (1 - k w). The old mean slope x mean inverse slope multiplied a
       dark / bright edge pair's chroma by ~ slope ratio / 4 (a green line round every sky gap). Glow chroma: output codes. */
    int yr = Y0 > Y1 ? Y0 : Y1;
    int32_t c = (int32_t)g->oc[yr], rdi = (int32_t)g->rd[yr], rdo = (int32_t)((g->rd[o0] + g->rd[o1]) >> 1);
    int u = (int)(((int64_t)(U * c) * rdi + (int64_t)gu * rdo + (1 << 23)) >> 24);   /* rounded, as smulsh */
    int v = (int)(((int64_t)(V * c) * rdi + (int64_t)gv * rdo + (1 << 23)) >> 24);
    u = u < -128 ? -128 : u > 127 ? 127 : u;
    v = v < -128 ? -128 : v > 127 ? 127 : v;
    return (uint32_t)(u & 255) | (uint32_t)o0 << 8 | (uint32_t)(v & 255) << 16 | (uint32_t)o1 << 24;
}

/* encode of a linear light (Q20 of the clip level) through the combined table */
static inline int enc2(const uint8_t *e, uint32_t L)
{
    if (L < 65536) return e[L >> 6];
    if (L < 1048576) return e[1024 + (L >> 9)];
    return 255;
}
/* (a * b + 2^23) >> 24: the signed 64-bit product, rounded (SMLAL onto 2^23). A plain >> 24 rounds down: U and V of every
 * pixel lost up to a code (the dark of a frame -0.5 with white mist, -1.0 with black: a green cast in the shadows, found by
 * MIST:levels 11-15) */
static inline int32_t smulsh(int32_t a, int32_t b)
{
    int32_t lo = 1 << 23, hi = 0;
    __asm__("smlal %0, %1, %2, %3" : "+&r"(lo), "+&r"(hi) : "r"(a), "r"(b));
    return (int32_t)(((uint32_t)lo >> 24) | ((uint32_t)hi << 8));
}

void mist_apply_chunk(mist_ctx *g, uint32_t *p, int npix, int x0, int y)
{
    if (!g->ok) return;
    if (y != g->row_y) row_prep(g, y);
    const typeof(g->T[0]) *T = g->T;
    const uint8_t *mo = g->moff, *en = g->enc;
    int nw = npix >> 1;
    for (int i = 0; i < nw;) {
        int t = x0 + 2 * i - 7, j0 = t >> 4, fx = t & 15, n = (17 - fx) >> 1;
        if (n > nw - i) n = nw - i;
        int32_t a0 = g->rowL[j0 + 1], dl = g->rowL[j0 + 2] - a0;
        int32_t gl = a0 + ((dl * fx) >> 4);
        int32_t gu = (g->rowU[j0 + 1] + g->rowU[j0 + 2]) >> 1, gv = (g->rowV[j0 + 1] + g->rowV[j0 + 2]) >> 1;   /* chroma glow: one value per half cell */
        dl >>= 3;
        for (int e = i + n; i < e; i++) {
            uint32_t w = p[i];
            int Y0 = (w >> 8) & 255, Y1 = (int)(w >> 24);
            int ym = Y0 > Y1 ? Y0 : Y1;
            if (__builtin_expect(ym + mo[((w >> 3) & 31) | ((w >> 14) & 0x3e0)] >= MIST_R0, 0)) {
                p[i] = hot_pair(g, w, Y0, Y1, gl, gu, gv);
            } else {
                int32_t U = (int8_t)w, V = (int8_t)(w >> 16);
                const typeof(g->T[0]) *a = T + Y0, *b = T + Y1;
                int o0 = enc2(en, a->tb + (uint32_t)gl), o1 = enc2(en, b->tb + (uint32_t)gl);
                int32_t c = (int32_t)a->oc, r = (int32_t)T[o0].rd;      /* chroma in AND out through the first pixel: the pair's mean
                                                                           slope (set by its brighter pixel) times the first pixel's
                                                                           inverse slope gave a dark / bright edge pair up to
                                                                           slope ratio / 2 its own chroma (green leaf / sky edges) */
                int32_t u = smulsh(U * c + gu, r), v = smulsh(V * c + gv, r);
                if (__builtin_expect(((uint32_t)(u + 128) | (uint32_t)(v + 128)) > 255, 0)) {
                    u = u < -128 ? -128 : u > 127 ? 127 : u;
                    v = v < -128 ? -128 : v > 127 ? 127 : v;
                }
                p[i] = (uint32_t)(u & 255) | (uint32_t)o0 << 8 | (uint32_t)(v & 255) << 16 | (uint32_t)o1 << 24;
            }
            gl += dl;
        }
    }
}

void mist_apply_rows(mist_ctx *g, uint32_t *words, int W, int y0, int nrows)
{
    int wpr = W >> 1;
    for (int r = 0; r < nrows; r++)
        for (int x = 0; x < wpr; x += 288)
            mist_apply_chunk(g, words + (size_t)r * wpr + x, 2 * (wpr - x < 288 ? wpr - x : 288), 2 * x, y0 + r);
}
