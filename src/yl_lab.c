/* yl_lab.c - the Lab effects (yl_lab.h): film-lab's Bloom, Halation and Grain on the camera's UYVY words.
 * WindyStrife's own fixed-point port; the formulas are film-lab's, the numbers its presets. Integer per pixel (no FPU,
 * no divide); soft float only in yl_init and yl_grain_init. docs/DESIGN.md 3.20 lists what differs from the reference
 * and tools/lab_check.sh measures it. */
#include "yl_lab.h"
#include <string.h>
#include <math.h>

#define YL_INLINE static inline __attribute__((always_inline))

/* ------------------------------------------------------------------ film-lab presets (8 / 16 / 35 / 65 mm) */
/* halation (%): core level, the mask ramp's two ends, near and far spread, warmth, curve, blue cut, mix, width */
static const struct { uint8_t core, m0, m1, nr, fr, warm, curve, bcut, mix, wide; } yl_hp[YL_NG] = {
    { 80, 15, 80, 25, 45, 50, 70, 100, 80, 50 },
    { 80, 15, 80, 20, 35, 50, 70,   0, 80, 50 },
    { 80, 15, 80, 15, 25, 50, 70, 100, 70, 50 },
    { 80, 15, 80, 10, 20, 50, 70, 100, 70, 50 },
};
/* bloom: start level (%), gain (EV x 10), spread, decay per level, levels, edge mask, edge reach, amount, chroma, span
 * (x 100), highlight ease; for every gauge the knee at its widest (nothing under the start level) and an edge dilation
 * of 5 (the reference's defaults) */
static const struct { uint8_t start; int8_t ev10; uint8_t spread, decay, nl, emask, ereach, amt, chroma, span, ease; } yl_bp[YL_NG] = {
    { 80,  8, 100, 40, 4, 15, 50, 45, 100, 100, 20 },
    { 80,  0,  80, 20, 4, 20, 55, 55, 100, 100, 20 },
    { 80,  0,  50, 30, 4, 20, 60, 50, 100, 100, 20 },
    { 80, -2,  35, 20, 4, 25, 70, 40, 100, 100, 20 },
};
#define YL_BEDIL 5
/* grain: cell, amount, zone weights (dark, mid, bright), chroma, clump x 10, turn (degrees); seed 1 */
static const struct { uint8_t cell, amt, zd, zm, zb, chroma, clump10, turn; } yl_gp[YL_NG] = {
    { 48, 50, 25, 45, 65, 65, 16, 1 },
    { 25, 45, 30, 55, 65, 65, 16, 1 },
    { 12, 35, 30, 55, 65, 65, 16, 1 },
    {  2, 15, 30, 55, 65, 65, 16, 1 },
};

/* the variance a grid adds to a field it averages over cells of c px and interpolates bilinearly between their centres
 * (box (c^2 - 1) / 12 + tent c^2 / 6), px^2 */
#define YL_GV8  (63.0f / 12.0f + 64.0f / 6.0f)
#define YL_GV16 (255.0f / 12.0f + 256.0f / 6.0f)

static float yl_ss(float a, float b, float x)        /* film-lab's smoothstep */
{
    if (a == b) return x < a ? 0.0f : 1.0f;
    float t = (x - a) / (b - a);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

/* film-lab's three boxes for a Gaussian of sigma px (boxSizesForGauss, n = 3): widths */
static void yl_boxes(float s, int w[3])
{
    if (!(s > 0.01f)) { w[0] = w[1] = w[2] = 1; return; }
    float wi = sqrtf(12.0f * s * s / 3.0f + 1.0f);
    int wl = (int)floorf(wi);
    if (!(wl & 1)) wl--;
    if (wl < 1) wl = 1;
    int wu = wl + 2;
    float mi = (12.0f * s * s - 3.0f * wl * wl - 12.0f * wl - 9.0f) / (-4.0f * wl - 4.0f);
    int m = (int)floorf(mi + 0.5f);
    for (int i = 0; i < 3; i++) w[i] = i < m ? wl : wu;
}

/* the variance of those three boxes, px^2 (a box of width w: (w^2 - 1) / 12) */
static float yl_gvar(float s)
{
    int w[3];
    yl_boxes(s, w);
    return (w[0] * w[0] - 1 + w[1] * w[1] - 1 + w[2] * w[2] - 1) / 12.0f;
}

/* a blur of variance v cells^2: three boxes for most of it when it is large, then a sampled Gaussian for the rest
 * (its sigma corrected so that the sampled kernel has variance v) */
static void yl_mkplan(yl_plan *pl, float v)
{
    memset(pl, 0, sizeof *pl);
    if (v <= 0.02f) return;
    if (v > 9.0f) {
        int wl = (int)floorf(sqrtf(4.0f * v + 1.0f));
        if (!(wl & 1)) wl--;
        if (wl < 1) wl = 1;
        float mi = (12.0f * v - 3.0f * wl * wl - 12.0f * wl - 9.0f) / (-4.0f * wl - 4.0f);
        int m = (int)ceilf(mi);
        m = m < 0 ? 0 : m > 3 ? 3 : m;
        for (int i = 0; i < 3; i++) {
            int w = i < m ? wl : wl + 2;
            pl->r[i] = (w - 1) / 2;
            v -= (w * w - 1) / 12.0f;
        }
        pl->nbox = 3;
    }
    if (v <= 0.02f) return;
    float s = sqrtf(v), f[24];
    int nk = 0;
    for (int it = 0; it < 4; it++) {
        float t = 0.0f, m2 = 0.0f;
        nk = (int)ceilf(3.0f * s);
        nk = nk < 1 ? 1 : nk > 23 ? 23 : nk;
        for (int i = 0; i <= nk; i++) {
            f[i] = expf(-0.5f * i * i / (s * s));
            t += i ? 2.0f * f[i] : f[i];
            m2 += 2.0f * f[i] * i * i;
        }
        m2 /= t;
        s *= sqrtf(v / m2);
    }
    float t = 0.0f;
    for (int i = 0; i <= nk; i++) t += i ? 2.0f * f[i] : f[i];
    int sum = 0;
    for (int i = 1; i <= nk; i++) { pl->k[i] = (uint16_t)(f[i] / t * 32768.0f + 0.5f); sum += 2 * pl->k[i]; }
    pl->k[0] = (uint16_t)(32768 - sum);
    pl->nk = nk;
}

/* ------------------------------------------------------------------ grid blurs */
YL_INLINE int yl_cl(int i, int n) { return i < 0 ? 0 : i >= n ? n - 1 : i; }

/* one box of radius r, edges replicated, as film-lab's boxBlurPlane (sum / (2r + 1)) */
static void yl_box(const int32_t *s, int32_t *d, int n, int r)
{
    uint32_t rc = ((1u << 24) + (uint32_t)r) / (uint32_t)(2 * r + 1);   /* init-free: one divide per line, not per cell */
    uint32_t sum = 0;
    int x = 0;
    for (int i = -r; i <= r; i++) sum += (uint32_t)s[yl_cl(i, n)];
    for (; x < n && (x < r || x + r + 1 >= n); x++) {      /* the edges: replicated */
        d[x] = (int32_t)(((uint64_t)sum * rc + (1u << 23)) >> 24);
        sum -= (uint32_t)s[yl_cl(x - r, n)];
        sum += (uint32_t)s[yl_cl(x + r + 1, n)];
    }
    for (; x + r + 1 < n; x++) {                 /* the inside */
        d[x] = (int32_t)(((uint64_t)sum * rc + (1u << 23)) >> 24);
        sum += (uint32_t)s[x + r + 1] - (uint32_t)s[x - r];
    }
    for (; x < n; x++) {
        d[x] = (int32_t)(((uint64_t)sum * rc + (1u << 23)) >> 24);
        sum -= (uint32_t)s[yl_cl(x - r, n)];
        sum += (uint32_t)s[yl_cl(x + r + 1, n)];
    }
}

static void yl_kern(const yl_plan *pl, const int32_t *s, int32_t *d, int n)
{
    int nk = pl->nk;
    for (int x = 0; x < n; x++) {
        uint32_t a = (uint32_t)pl->k[0] * (uint32_t)s[x];
        if (x >= nk && x + nk < n)
            for (int j = 1; j <= nk; j++) a += pl->k[j] * (uint32_t)(s[x - j] + s[x + j]);
        else
            for (int j = 1; j <= nk; j++) a += pl->k[j] * (uint32_t)(s[yl_cl(x - j, n)] + s[yl_cl(x + j, n)]);
        d[x] = (int32_t)((a + 16384u) >> 15);
    }
}

/* the plan on one line in a; returns the buffer that holds the result (a or b) */
static int32_t *yl_line(const yl_plan *pl, int32_t *a, int32_t *b, int n)
{
    for (int k = 0; k < pl->nbox; k++) {
        int32_t *t;
        if (pl->r[k] < 1) continue;
        yl_box(a, b, n, pl->r[k]);
        t = a; a = b; b = t;
    }
    if (pl->nk) {
        yl_kern(pl, a, b, n);
        a = b;
    }
    return a;
}

/* field f (w x h cells, nc channels interleaved), channel c, in place */
static void yl_blur(yl_ctx *g, uint16_t *f, int w, int h, int nc, int c, const yl_plan *pl)
{
    if (!pl->nbox && !pl->nk) return;
    for (int y = 0; y < h; y++) {
        uint16_t *p = f + (y * w) * nc + c;
        for (int x = 0; x < w; x++) g->ln0[x] = p[x * nc];
        int32_t *r = yl_line(pl, g->ln0, g->ln1, w);
        for (int x = 0; x < w; x++) p[x * nc] = (uint16_t)(r[x] > 65535 ? 65535 : r[x]);
    }
    for (int x = 0; x < w; x++) {
        uint16_t *p = f + x * nc + c;
        for (int y = 0; y < h; y++) g->ln0[y] = p[y * w * nc];
        int32_t *r = yl_line(pl, g->ln0, g->ln1, h);
        for (int y = 0; y < h; y++) p[y * w * nc] = (uint16_t)(r[y] > 65535 ? 65535 : r[y]);
    }
}

/* ------------------------------------------------------------------ memory, init */
#define YL_AL(n) (((uint32_t)(n) + 3u) & ~3u)
#define YL_SV8  (YL_XF / 8 + 2)                /* halves: 1/8 glow rows kept round the seam */
#define YL_SV16 (YL_XF / 16 + 2)

static uint32_t yl_lines(int W, int H)
{
    int n = W / 8 > H / 8 ? W / 8 : H / 8;
    return YL_AL(4u * (n + 8));
}

uint32_t yl_mem_bytes(int W, int H, int hal, int bloom)
{
    uint32_t gw = W / 8, gh = H / 8, bw = W / 16, bh = H / 16, n = 0;
    if (hal)
        n += 4 * YL_AL(2 * gw * gh) + YL_AL(4 * gw) + 2 * YL_AL(2 * gh) + YL_AL(2 * 2 * gw * YL_SV8) + YL_AL(2 * gw);
    if (bloom)
        n += 3 * YL_AL(6 * bw * bh) + YL_AL(bw * bh) + YL_AL(12 * bw) + YL_AL(bw) + YL_AL(W) + 2 * YL_AL(2 * bh)
             + YL_AL(2 * 3 * bw) + YL_AL(2 * 3 * bw * YL_SV16) + YL_AL(2 * 4 * bw);
    if (hal && bloom)
        n += YL_AL(bw * bh) + YL_AL(bw);
    return n + 2 * yl_lines(W, H);
}

int yl_init(yl_ctx *g, const yl_params *p, int W, int H, void *mem)
{
    uint8_t *m = (uint8_t *)mem;
    int ga = p->gauge >= 0 && p->gauge < YL_NG ? p->gauge : 2;
    float scale = H / 1080.0f;
    memset(g, 0, sizeof *g);
    g->W = W; g->H = H; g->gw = W / 8; g->gh = H / 8; g->bw = W / 16; g->bh = H / 16;
    g->hal = p->hal != 0; g->bloom = p->bloom != 0; g->gauge = ga;
    g->two = g->hal && g->bloom;
#define YL_TAKE(ptr, type, bytes) do { ptr = (type)(void *)m; m += YL_AL(bytes); } while (0)
    if (g->hal) {
        uint32_t n8 = (uint32_t)g->gw * g->gh;
        YL_TAKE(g->P, uint16_t *, 2 * n8);
        YL_TAKE(g->BL, uint16_t *, 2 * n8);
        YL_TAKE(g->BG, uint16_t *, 2 * n8);
        YL_TAKE(g->T, uint16_t *, 2 * n8);
        YL_TAKE(g->pacc, uint32_t *, 4 * g->gw);
        YL_TAKE(g->hlo, int16_t *, 2 * g->gh);
        YL_TAKE(g->hhi, int16_t *, 2 * g->gh);
        YL_TAKE(g->sv, uint16_t *, 2 * 2 * g->gw * YL_SV8);
        YL_TAKE(g->vq, uint16_t *, 2 * g->gw);
    }
    if (g->bloom) {
        uint32_t n16 = (uint32_t)g->bw * g->bh;
        YL_TAKE(g->HI, uint16_t *, 6 * n16);
        YL_TAKE(g->TB, uint16_t *, 6 * n16);
        YL_TAKE(g->AC, uint16_t *, 6 * n16);
        YL_TAKE(g->E, uint8_t *, n16);
        YL_TAKE(g->bacc, uint32_t *, 12 * g->bw);
        YL_TAKE(g->eacc, uint8_t *, g->bw);
        YL_TAKE(g->prow, uint8_t *, W);
        YL_TAKE(g->blo, int16_t *, 2 * g->bh);
        YL_TAKE(g->bhi, int16_t *, 2 * g->bh);
        YL_TAKE(g->vb, uint16_t *, 2 * 3 * g->bw);
        YL_TAKE(g->svb, uint16_t *, 2 * 3 * g->bw * YL_SV16);
        YL_TAKE(g->vb4, int16_t *, 2 * 4 * g->bw);
        if (g->two) {
            YL_TAKE(g->YM, uint8_t *, n16);
            YL_TAKE(g->yacc, uint8_t *, g->bw);
        }
    }
    YL_TAKE(g->ln0, int32_t *, yl_lines(W, H));
    YL_TAKE(g->ln1, int32_t *, yl_lines(W, H));
#undef YL_TAKE

    /* moff / lofs per (U >> 3, V >> 3) bin over the bin's corners: the largest positive channel offset over Y, and
     * the largest rise of luma when the negative channels clamp to 0 (Y >= 0: at most 0.299 (-R) + 0.587 (-G) + ...) */
    for (int bu = 0; bu < 32; bu++)
        for (int bv = 0; bv < 32; bv++) {
            int mo = 0, lo = 0, mg = 0;
            for (int cu = 0; cu < 2; cu++)
                for (int cv = 0; cv < 2; cv++) {
                    int U = bu * 8 - 128 + cu * 7, V = bv * 8 - 128 + cv * 7;
                    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bb = (454 * U) >> 8;
                    int o = rv > -gu ? rv : -gu, ng = -rv > gu ? -rv : gu;
                    if (bb > o) o = bb;
                    if (o > mo) mo = o;
                    if (-bb > ng) ng = -bb;
                    if (ng > mg) mg = ng;
                    int l = (299 * (rv < 0 ? -rv : 0) + 587 * (gu > 0 ? gu : 0) + 114 * (bb < 0 ? -bb : 0) + 999) / 1000;
                    if (l > lo) lo = l;
                }
            g->moff[bu * 32 + bv] = (uint8_t)(mo > 255 ? 255 : mo);
            g->mneg[bu * 32 + bv] = (uint8_t)(mg > 255 ? 255 : mg);
            g->lofs[bu * 32 + bv] = (uint8_t)(lo > 255 ? 255 : lo);
            g->bint[bu * 32 + bv] = g->moff[bu * 32 + bv] | (uint32_t)g->mneg[bu * 32 + bv] << 8
                                    | (uint32_t)g->lofs[bu * 32 + bv] << 16;
        }
    g->lmin = 256;

    if (g->hal) {
        const float lo = (yl_hp[ga].m0 < yl_hp[ga].m1 ? yl_hp[ga].m0 : yl_hp[ga].m1) / 100.0f;
        const float hi = (yl_hp[ga].m0 < yl_hp[ga].m1 ? yl_hp[ga].m1 : yl_hp[ga].m0) / 100.0f;
        const float T = yl_hp[ga].core / 100.0f, pw = yl_hp[ga].curve / 100.0f < 0.05f ? 0.05f : yl_hp[ga].curve / 100.0f;
        const float warm = yl_hp[ga].warm / 100.0f, op = yl_hp[ga].mix / 100.0f;
        float lw = yl_hp[ga].nr / 100.0f, gw = yl_hp[ga].fr / 100.0f, s;
        g->hmin = 2048;
        for (int i = 0; i < 2048; i++) {
            float L = i / 2040.0f, mk = yl_ss(lo, hi, L) * yl_ss(T - 0.06f, T + 0.06f, L);
            g->mtab[i] = (uint32_t)(mk * 65536.0f * 4095.0f / 4080.0f + 0.5f);
            if (g->mtab[i] && i < g->hmin) g->hmin = i;
        }
        for (int i = 0; i < 4096; i++)
            g->pw[i] = (uint16_t)(powf(i / 4095.0f, pw) * 32767.0f + 0.5f);
        for (int i = 0; i < 1024; i++)
            g->grcp[i] = (uint16_t)(((1u << 26) - 1u) / (uint32_t)(1024 + i));
        for (int i = 0; i <= YL_GE; i++)
            g->gexp[i] = (uint16_t)(expf(-i / 64.0f) * 32768.0f + 0.5f);
        if (lw + gw < 1e-4f) { lw = 1.0f; gw = 0.0f; }
        s = lw + gw;
        g->lw_q15 = (int)(lw / s * op * 32768.0f + 0.5f);
        g->gw_q15 = (int)(gw / s * op * 32768.0f + 0.5f);
        if (g->lw_q15 + g->gw_q15 > 32768) g->gw_q15 = 32768 - g->lw_q15;
        g->kr = 4080;
        g->kg = (int)((0.10f + (0.48f - 0.10f) * warm) * 4080.0f + 0.5f);
        g->kb = (int)((0.03f + (0.10f - 0.03f) * warm) * (1.0f - yl_hp[ga].bcut / 100.0f * 0.85f) * 4080.0f + 0.5f);
        g->hy = (19595 * g->kr + 38470 * g->kg + 7471 * g->kb + 32768) >> 16;
        g->hu = (-11059 * g->kr - 21709 * g->kg + 32768 * g->kb) >> 16;
        g->hv = (32768 * g->kr - 27439 * g->kg - 5329 * g->kb) >> 16;
        {
            float sm = 0.5f + yl_hp[ga].wide / 100.0f * 1.5f;
            float sl = (1.0f + yl_hp[ga].nr / 100.0f * 10.0f) * scale * sm, sg = (6.0f + yl_hp[ga].fr / 100.0f * 90.0f) * scale * sm;
            float vl = yl_gvar(sl < 0.3f ? 0.3f : sl), vg = yl_gvar(sg < 0.3f ? 0.3f : sg), v0 = vl > YL_GV8 ? vl : YL_GV8;
            yl_mkplan(&g->pl_l, (vl - YL_GV8) / 64.0f);
            yl_mkplan(&g->pl_g, (vg - v0) / 64.0f);
        }
        if (!g->two && g->hmin / 8 < g->lmin) g->lmin = g->hmin / 8;   /* both on: the first gather is the bloom's */
    }
    if (g->bloom) {
        float bs = scale * yl_bp[ga].span / 100.0f, base = (0.5f + yl_bp[ga].spread / 100.0f * 19.5f) * bs, vp = YL_GV16, ws = 0.0f;
        float fall = yl_bp[ga].decay / 100.0f, w[6];
        int dr = (int)floorf(yl_bp[ga].ereach / 100.0f * 24.0f * scale + 0.5f), er = (int)floorf(YL_BEDIL * scale + 0.5f);
        g->bthr = (yl_bp[ga].start * 1020 + 99) / 100;   /* luma >= thr / 100: quarter levels >= ceil(thr * 2.55 * 4) */
        g->bgain_q12 = (int)(powf(2.0f, yl_bp[ga].ev10 / 10.0f) * 4096.0f + 0.5f);
        g->nl = yl_bp[ga].nl < 1 ? 1 : yl_bp[ga].nl > 6 ? 6 : yl_bp[ga].nl;
        for (int i = 0; i < g->nl; i++) { w[i] = powf(fall, (float)i); ws += w[i]; }
        for (int i = 0; i < g->nl; i++) {
            float sg = base * powf(2.0f, (float)i) * 1.12f, v = yl_gvar(sg < 0.3f ? 0.3f : sg);
            g->wl[i] = (uint16_t)(w[i] / ws * 32768.0f + 0.5f);
            yl_mkplan(&g->pl_b[i], (v - vp) / 256.0f);
            if (v > vp) vp = v;
        }
        for (int i = 0; i < 1024; i++) {
            float L = i / 1020.0f, k = yl_bp[ga].amt / 100.0f * 1.6f * (1.0f - yl_bp[ga].ease / 100.0f * 0.35f * powf(L, 0.25f));
            g->kl[i] = (uint16_t)(k * 32768.0f + 0.5f);
            if (g->kl[i] > g->klmax) g->klmax = g->kl[i];
        }
        for (int i = 0; i < 256; i++)
            g->ky[i] = g->kl[i << 2];
        yl_mkplan(&g->pl_e, (((2 * dr + 1) * (2 * dr + 1) - 1) / 12.0f - YL_GV16) / 256.0f);
        g->edil = (er + 8) / 16;
        g->emask_q15 = yl_bp[ga].emask * 32768 / 100;
        if (g->bthr / 4 < g->lmin) g->lmin = g->bthr / 4;
    }
    yl_begin_frame(g);
    return 0;
}

void yl_begin_frame(yl_ctx *g)
{
    if (g->hal) {
        memset(g->P, 0, 2u * g->gw * g->gh);
        memset(g->pacc, 0, 4u * g->gw);
    }
    if (g->bloom) {
        memset(g->HI, 0, 6u * g->bw * g->bh);
        memset(g->E, 0, (uint32_t)g->bw * g->bh);
        memset(g->bacc, 0, 12u * g->bw);
        memset(g->eacc, 0, g->bw);
        if (g->two) {
            memset(g->YM, 0, (uint32_t)g->bw * g->bh);
            memset(g->yacc, 0, g->bw);
        }
    }
    g->pj = g->bj = -1;
    g->prow_y = -2;
    g->cur_y = -1;
    g->seam = 0;
}

/* ------------------------------------------------------------------ gather */
static void yl_flush(yl_ctx *g)
{
    if (g->pj >= 0) {
        uint16_t *P = g->P + g->pj * g->gw;
        for (int i = 0; i < g->gw; i++) { P[i] = (uint16_t)(g->pacc[i] >> 6); g->pacc[i] = 0; }
        g->pj = -1;
    }
    if (g->bj >= 0) {
        uint16_t *Hp = g->HI + 3 * g->bj * g->bw;
        uint8_t *E = g->E + g->bj * g->bw;
        for (int i = 0; i < 3 * g->bw; i++) { Hp[i] = (uint16_t)(g->bacc[i] >> 4); g->bacc[i] = 0; }
        for (int i = 0; i < g->bw; i++) { E[i] = g->eacc[i]; g->eacc[i] = 0; }
        if (g->two) {
            uint8_t *M = g->YM + g->bj * g->bw;
            for (int i = 0; i < g->bw; i++) { M[i] = g->yacc[i]; g->yacc[i] = 0; }
        }
        g->bj = -1;
    }
}

YL_INLINE int yl_c8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* one pixel of the gather: the halation layer of its clamped RGB and, at or above the bloom's start level, its RGB */
YL_INLINE void yl_gpix(yl_ctx *g, int Y, int rv, int gu, int bu, uint32_t *pa, uint32_t *ba)
{
    int R = yl_c8(Y + rv), G = yl_c8(Y - gu), B = yl_c8(Y + bu);
    int s = 19595 * R + 38470 * G + 7471 * B;
    if (pa && (s >> 13) >= g->hmin) {
        int t4 = (R << 4) + ((1434 * (G << 4) + 410 * (B << 4) + 2048) >> 12);
        uint32_t idx = ((uint32_t)t4 * g->mtab[s >> 13]) >> 16;
        *pa += g->pw[idx > 4095 ? 4095 : idx];
    }
    if (ba && (s >> 14) >= g->bthr) {
        ba[0] += R; ba[1] += G; ba[2] += B;
    }
}

void yl_gather_chunk(yl_ctx *g, const uint32_t *p, int npix, int x0, int y)
{
    int hg = g->hal && !g->two;                  /* both on: the halation layer comes from the bloomed frame (gather2) */
    if (y < 0 || y >= g->H) return;
    if (hg && (y >> 3) != g->pj) {
        if (g->pj >= 0) {
            uint16_t *P = g->P + g->pj * g->gw;
            for (int i = 0; i < g->gw; i++) { P[i] = (uint16_t)(g->pacc[i] >> 6); g->pacc[i] = 0; }
        }
        g->pj = y >> 3;
    }
    if (g->bloom && (y >> 4) != g->bj) {
        if (g->bj >= 0) {
            uint16_t *Hp = g->HI + 3 * g->bj * g->bw;
            uint8_t *E = g->E + g->bj * g->bw;
            for (int i = 0; i < 3 * g->bw; i++) { Hp[i] = (uint16_t)(g->bacc[i] >> 4); g->bacc[i] = 0; }
            for (int i = 0; i < g->bw; i++) { E[i] = g->eacc[i]; g->eacc[i] = 0; }
            if (g->two) {
                uint8_t *M = g->YM + g->bj * g->bw;
                for (int i = 0; i < g->bw; i++) { M[i] = g->yacc[i]; g->yacc[i] = 0; }
            }
        }
        g->bj = y >> 4;
    }
    const uint8_t *lofs = g->lofs;
    const int lmin = g->lmin;
    if (!g->bloom) {                             /* halation alone */
        for (int k = 0; k < npix; k += 2) {
            uint32_t w = *p++, wb = w ^ 0x00800080u;
            int Y0 = (w >> 8) & 255, Y1 = w >> 24;
            if ((Y0 > Y1 ? Y0 : Y1) + lofs[((wb & 0xf8u) << 2) | ((wb >> 19) & 31u)] < lmin)
                continue;
            int U = (int8_t)w, V = (int8_t)(w >> 16), x = x0 + k;
            int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
            yl_gpix(g, Y0, rv, gu, bu, g->pacc + (x >> 3), 0);
            yl_gpix(g, Y1, rv, gu, bu, g->pacc + (x >> 3), 0);
        }
        return;
    }
    /* the bloom's edge map: the largest luma step per 16 px cell, in the word and to
     * the row above when it was gathered; per cell, so the cell's maximum stays in a register */
    int ev = g->prow_y == y - 1;
    uint8_t *pr = g->prow + x0;
    for (int k = 0; k < npix;) {
        int x = x0 + k, n = (16 - (x & 15)) >> 1;
        if (n > (npix - k) >> 1) n = (npix - k) >> 1;
        uint8_t *ea = g->eacc + (x >> 4);
        uint32_t *ba = g->bacc + 3 * (x >> 4);
        int em = *ea, lm = 0;
        for (int i = 0; i < n; i++, p++, pr += 2) {
            uint32_t w = *p, wb = w ^ 0x00800080u;
            int Y0 = (w >> 8) & 255, Y1 = w >> 24, e = Y0 - Y1;
            if (e < 0) e = -e;
            if (e > em) em = e;
            if (ev) {
                int d0 = Y0 - pr[0], d1 = Y1 - pr[1];
                if (d0 < 0) d0 = -d0;
                if (d1 < 0) d1 = -d1;
                if (d0 > em) em = d0;
                if (d1 > em) em = d1;
            }
            pr[0] = (uint8_t)Y0; pr[1] = (uint8_t)Y1;
            int l = (Y0 > Y1 ? Y0 : Y1) + lofs[((wb & 0xf8u) << 2) | ((wb >> 19) & 31u)];
            if (l > lm) lm = l;
            if (l < lmin)
                continue;
            int U = (int8_t)w, V = (int8_t)(w >> 16);
            int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
            uint32_t *pa = hg ? g->pacc + ((x + 2 * i) >> 3) : 0;
            yl_gpix(g, Y0, rv, gu, bu, pa, ba);
            yl_gpix(g, Y1, rv, gu, bu, pa, ba);
        }
        *ea = (uint8_t)em;
        if (g->two) {
            uint8_t *ya = g->yacc + (x >> 4);
            if (lm > 255) lm = 255;
            if (lm > *ya) *ya = (uint8_t)lm;
        }
        k += 2 * n;
    }
    if (x0 + npix >= g->W)
        g->prow_y = y;
}

/* ------------------------------------------------------------------ build */
static void yl_spans(const uint16_t *f, int w, int h, int nc, int16_t *lo, int16_t *hi)
{
    for (int y = 0; y < h; y++) {
        const uint16_t *r = f + y * w * nc;
        int a = w, b = -1;
        for (int x = 0; x < w; x++, r += nc) {
            int nz = r[0];
            for (int c = 1; c < nc; c++) nz |= r[c];
            if (nz) { if (x < a) a = x; b = x; }
        }
        lo[y] = (int16_t)a; hi[y] = (int16_t)b;
    }
}

/* halves: keep the TOP's glow rows round the seam (TOP build), or fade from them to the whole frame's (BOT build):
 * cell rows above the seam stay the TOP's (the BOT's first pixel rows interpolate from them), rows below take
 * (centre - seam) / YL_XF of the whole frame's */
static void yl_seam_h(yl_ctx *g, int mode)
{
    uint16_t *sv = g->sv;
    {
        int j0 = g->seam / 8 - 1, n = YL_SV8, w = g->gw;
        for (int k = 0; k < n; k++) {
            int j = j0 + k;
            if (j < 0 || j >= g->gh) continue;
            uint16_t *a = g->BL + j * w, *b = g->BG + j * w, *s = sv + 2 * k * w;
            if (mode == YL_TOP) {
                memcpy(s, a, 2u * w);
                memcpy(s + w, b, 2u * w);
            } else {
                int c = 8 * j + 4 - g->seam, f = c <= 0 ? 0 : c >= YL_XF ? 256 : (c * 256) / YL_XF;
                for (int i = 0; i < w; i++) {
                    a[i] = (uint16_t)(s[i] + (((a[i] - s[i]) * f) >> 8));
                    b[i] = (uint16_t)(s[w + i] + (((b[i] - s[w + i]) * f) >> 8));
                }
            }
        }
    }
}

static void yl_seam_b(yl_ctx *g, int mode)
{
    uint16_t *sv = g->svb;                       /* its own block: with both on, g->sv holds the halation's rows */
    {
        int j0 = g->seam / 16 - 1, n = YL_SV16, w = 3 * g->bw;
        for (int k = 0; k < n; k++) {
            int j = j0 + k;
            if (j < 0 || j >= g->bh) continue;
            uint16_t *a = g->AC + j * w, *s = sv + k * w;
            if (mode == YL_TOP) {
                memcpy(s, a, 2u * w);
            } else {
                int c = 16 * j + 8 - g->seam, f = c <= 0 ? 0 : c >= YL_XF ? 256 : (c * 256) / YL_XF;
                for (int i = 0; i < w; i++)
                    a[i] = (uint16_t)(s[i] + (((a[i] - s[i]) * f) >> 8));
            }
        }
    }
}

/* halves, the TOP's build: the rows below the seam are not gathered yet. Light that crosses the seam as a wide area is
 * mirrored into them, each cell at most the seam row's opening over +-er cells (the row's minimum over +-er, then the
 * maximum of that over +-er: a source wider than 2 er + 1 cells keeps its whole width, a narrower one at the seam gets
 * no phantom twin; the WindyFilm stage's rule, yh_halo.c seam_mirror, erodes only); the BOT then fades from this glow to
 * the real one */
#define YL_MIR8  2                               /* +-2 cells: sources under 40 px at the seam are not mirrored (1/8 grid) */
#define YL_MIR16 1                               /* +-1 cell: under 48 px (1/16 grid) */
static void yl_mirror(yl_ctx *g, uint16_t *f, int w, int h, int nc, int s0, int er)
{
    if (s0 < 1 || s0 >= h)
        return;
    for (int c = 0; c < nc; c++) {
        const uint16_t *sr = f + (s0 - 1) * w * nc + c;
        int32_t *ero = g->ln1, *lim = g->ln0;    /* w <= W / 8 entries: the blur lines */
        for (int x = 0; x < w; x++) {
            int m = 65535;
            for (int d = -er; d <= er; d++) {
                int i = x + d < 0 ? 0 : x + d >= w ? w - 1 : x + d;
                if (sr[i * nc] < m) m = sr[i * nc];
            }
            ero[x] = m;
        }
        for (int x = 0; x < w; x++) {
            int m = 0;
            for (int d = -er; d <= er; d++) {
                int i = x + d < 0 ? 0 : x + d >= w ? w - 1 : x + d;
                if (ero[i] > m) m = ero[i];
            }
            lim[x] = m;
        }
        for (int k = 0; s0 + k < h && s0 - 1 - k >= 0; k++) {
            const uint16_t *a = f + (s0 - 1 - k) * w * nc + c;
            uint16_t *b = f + (s0 + k) * w * nc + c;
            for (int x = 0; x < w; x++)
                b[x * nc] = a[x * nc] < lim[x] ? a[x * nc] : (uint16_t)lim[x];
        }
    }
}

/* halation: the layer means P blurred to the local glow BL and on to the global BG; the halves' fade; the spans */
static void yl_build_h(yl_ctx *g, int mode)
{
    uint32_t n8 = (uint32_t)g->gw * g->gh;
    memcpy(g->T, g->P, 2 * n8);
    if (mode == YL_TOP)
        yl_mirror(g, g->T, g->gw, g->gh, 1, g->seam / 8, YL_MIR8);
    yl_blur(g, g->T, g->gw, g->gh, 1, 0, &g->pl_l);
    memcpy(g->BL, g->T, 2 * n8);
    yl_blur(g, g->T, g->gw, g->gh, 1, 0, &g->pl_g);
    memcpy(g->BG, g->T, 2 * n8);
    if (mode != YL_WHOLE)
        yl_seam_h(g, mode);
    for (int y = 0; y < g->gh; y++) {          /* cells whose halo adds under a quarter level (yl_build) are dropped */
        uint16_t *a = g->BL + y * g->gw, *b = g->BG + y * g->gw;
        int l = g->gw, h = -1;
        for (int x = 0; x < g->gw; x++) {
            uint32_t v = ((uint32_t)a[x] * (uint32_t)g->lw_q15 + (uint32_t)b[x] * (uint32_t)g->gw_q15) >> 15;
            if (v * (uint32_t)g->kr < 4u * 32768u) {
                a[x] = b[x] = 0;
                continue;
            }
            if (x < l) l = x;
            h = x;
        }
        g->hlo[y] = (int16_t)l; g->hhi[y] = (int16_t)h;
    }
}

void yl_build(yl_ctx *g, int y_begin, int y_end, int mode)
{
    yl_flush(g);
    g->seam = mode == YL_TOP ? y_end : mode == YL_BOTTOM ? y_begin : 0;
    if (g->bloom) {
        int n16 = g->bw * g->bh, emax = 0;
        uint16_t *TB = g->TB, *AC = g->AC;
        for (int i = 0; i < 3 * n16; i++) {
            uint32_t v = ((uint32_t)g->HI[i] * (uint32_t)g->bgain_q12 + 2048u) >> 12;
            TB[i] = (uint16_t)(v > 65535 ? 65535 : v);
            AC[i] = 0;
        }
        if (mode == YL_TOP)
            yl_mirror(g, TB, g->bw, g->bh, 3, g->seam / 16, YL_MIR16);
        for (int l = 0; l < g->nl; l++) {
            for (int c = 0; c < 3; c++)
                yl_blur(g, TB, g->bw, g->bh, 3, c, &g->pl_b[l]);
            for (int i = 0; i < 3 * n16; i++) {
                uint32_t v = AC[i] + (((uint32_t)TB[i] * g->wl[l] + 16384u) >> 15);
                AC[i] = (uint16_t)(v > 65535 ? 65535 : v);
            }
        }
        /* the edge mask: the largest luma step per cell over the frame's largest, dilated, blurred (in TB's first
         * plane, free now); the glow is multiplied by 1 - min(2.5 x that, 1) x the edge mask */
        for (int i = 0; i < n16; i++) if (g->E[i] > emax) emax = g->E[i];
        if (emax && g->emask_q15) {
            uint32_t rc = (32768u * 256u + (uint32_t)emax / 2) / (uint32_t)emax;
            uint16_t *M = TB;
            for (int i = 0; i < n16; i++) M[i] = (uint16_t)((g->E[i] * rc + 128) >> 8);
            if (mode == YL_TOP)
                yl_mirror(g, M, g->bw, g->bh, 1, g->seam / 16, YL_MIR16);
            for (int d = 0; d < g->edil; d++) {      /* one cell each way per step: 3 x 3 max */
                for (int y = 0; y < g->bh; y++) {
                    uint16_t *r = M + y * g->bw, pv = r[0];
                    for (int x = 0; x < g->bw; x++) {
                        uint16_t c = r[x], nx = x + 1 < g->bw ? r[x + 1] : c, m = c > pv ? c : pv;
                        if (nx > m) m = nx;
                        pv = c; r[x] = m;
                    }
                }
                for (int x = 0; x < g->bw; x++) {
                    uint16_t pv = M[x];
                    for (int y = 0; y < g->bh; y++) {
                        uint16_t c = M[y * g->bw + x], nx = y + 1 < g->bh ? M[(y + 1) * g->bw + x] : c, m = c > pv ? c : pv;
                        if (nx > m) m = nx;
                        pv = c; M[y * g->bw + x] = m;
                    }
                }
            }
            yl_blur(g, M, g->bw, g->bh, 1, 0, &g->pl_e);
            for (int i = 0; i < n16; i++) {
                uint32_t e = (5u * M[i]) >> 1, k = 32768u - (((e > 32768u ? 32768u : e) * (uint32_t)g->emask_q15) >> 15);
                AC[3 * i] = (uint16_t)((AC[3 * i] * k + 16384u) >> 15);
                AC[3 * i + 1] = (uint16_t)((AC[3 * i + 1] * k + 16384u) >> 15);
                AC[3 * i + 2] = (uint16_t)((AC[3 * i + 2] * k + 16384u) >> 15);
            }
        }
        if (mode != YL_WHOLE)
            yl_seam_b(g, mode);
        {   /* a cell whose glow adds under a quarter level to any channel of any pixel is dropped (the halation's cells
               the same, yl_build_h: both together stay under half a level, which rounds away), so the spans and the
               apply end where the glow does */
            uint32_t z = (4u * 32768u + (uint32_t)g->klmax - 1u) / (uint32_t)(g->klmax > 0 ? g->klmax : 1);
            for (int i = 0; i < n16; i++) {
                uint16_t *a = AC + 3 * i;
                if (a[0] < z && a[1] < z && a[2] < z)
                    a[0] = a[1] = a[2] = 0;
            }
        }
        yl_spans(g->AC, g->bw, g->bh, 3, g->blo, g->bhi);
    }
    if (g->hal && !g->two)
        yl_build_h(g, mode);
    g->cur_y = -1;
}

/* ------------------------------------------------------------------ apply */
static void yl_prep_b(yl_ctx *g, int y);

static void yl_prep_row(yl_ctx *g, int y)
{
    g->cur_y = y;
    g->hl0 = 1; g->hh0 = 0; g->bl0 = 1; g->bh0 = 0;
    if (g->bloom)
        yl_prep_b(g, y);
    if (g->hal) {
        int t = 2 * y - 7, j0 = t >> 4, j1 = j0 + 1, fy = t & 15;
        j0 = yl_cl(j0, g->gh); j1 = yl_cl(j1, g->gh);
        int lo = g->hlo[j0] < g->hlo[j1] ? g->hlo[j0] : g->hlo[j1], hi = g->hhi[j0] > g->hhi[j1] ? g->hhi[j0] : g->hhi[j1];
        if (lo <= hi) {
            const uint16_t *a0 = g->BL + j0 * g->gw, *a1 = g->BL + j1 * g->gw, *b0 = g->BG + j0 * g->gw, *b1 = g->BG + j1 * g->gw;
            for (int i = lo; i <= hi; i++) {
                uint32_t l = (uint32_t)((a0[i] * (16 - fy) + a1[i] * fy + 8) >> 4);
                uint32_t q = (uint32_t)((b0[i] * (16 - fy) + b1[i] * fy + 8) >> 4);
                g->vq[i] = (uint16_t)((l * (uint32_t)g->lw_q15 + q * (uint32_t)g->gw_q15) >> 15);
            }
            g->hl0 = lo; g->hh0 = hi;
        }
    }
}

/* the bloom glow of the pixel row y between its two 1/16 rows (vb), and its non-zero cells */
static void yl_prep_b(yl_ctx *g, int y)
{
    int t = 2 * y - 15, j0 = t >> 5, j1 = j0 + 1, fy = t & 31;
    g->bl0 = 1; g->bh0 = 0;
    j0 = yl_cl(j0, g->bh); j1 = yl_cl(j1, g->bh);
    int lo = g->blo[j0] < g->blo[j1] ? g->blo[j0] : g->blo[j1], hi = g->bhi[j0] > g->bhi[j1] ? g->bhi[j0] : g->bhi[j1];
    if (lo <= hi) {
        const uint16_t *a0 = g->AC + 3 * j0 * g->bw, *a1 = g->AC + 3 * j1 * g->bw;
        for (int i = 3 * lo; i < 3 * hi + 3; i++)
            g->vb[i] = (uint16_t)((a0[i] * (32 - fy) + a1[i] * fy + 16) >> 5);
        for (int i = lo; i <= hi; i++) {         /* AC <= 16 x 255 x 2^0.8: Y / U / V fit 16 bits */
            const uint16_t *v = g->vb + 3 * i;
            int R = v[0], G = v[1], B = v[2], m = R > G ? R : G;
            int16_t *o = g->vb4 + 4 * i;
            o[0] = (int16_t)((19595 * R + 38470 * G + 7471 * B) >> 16);
            o[1] = (int16_t)((-11059 * R - 21709 * G + 32768 * B) >> 16);
            o[2] = (int16_t)((32768 * R - 27439 * G - 5329 * B) >> 16);
            o[3] = (int16_t)(B > m ? B : m);
        }
        g->bl0 = lo; g->bh0 = hi;
    }
}

/* gather2: along a pixel row the bloom glow is linear between two cell centres (16 i + 7.5; the frame's edge columns
 * repeat), so inside a cell pair it is a value plus a step per word, kept x16: (acc + 8) >> 4 is the per-word formula's
 * own value, c0 (16 - f) + c1 f over 16 */
typedef struct {
    int R, Gc, B, dR, dG, dB;    /* bloom glow x 16 (Q4 levels) and steps per word */
    int n, z;                    /* words left; 1 = all zero */
    int ab;                      /* the most levels it adds to a channel of a pixel in this pair, + 1 */
} yl_brun;

YL_INLINE void yl_bseg(const yl_ctx *g, int x, yl_brun *s)
{
    int t = x - 7, i = t >> 4, f = t & 15;       /* f odd, 1..15 */
    int ia = i < 0 ? 0 : i, ib = i + 1 >= g->bw ? g->bw - 1 : i + 1;
    int r0 = 0, g0 = 0, c0 = 0, r1 = 0, g1 = 0, c1 = 0;
    if (ia >= g->bl0 && ia <= g->bh0) { const uint16_t *v = g->vb + 3 * ia; r0 = v[0]; g0 = v[1]; c0 = v[2]; }
    if (ib >= g->bl0 && ib <= g->bh0) { const uint16_t *v = g->vb + 3 * ib; r1 = v[0]; g1 = v[1]; c1 = v[2]; }
    int am = r0 > g0 ? r0 : g0;
    if (c0 > am) am = c0;
    if (r1 > am) am = r1;
    if (g1 > am) am = g1;
    if (c1 > am) am = c1;
    s->n = (17 - f) >> 1;
    s->z = !am;
    s->ab = ((am * g->klmax) >> 19) + 1;
    s->R = 16 * r0 + (r1 - r0) * f; s->dR = 2 * (r1 - r0);
    s->Gc = 16 * g0 + (g1 - g0) * f; s->dG = 2 * (g1 - g0);
    s->B = 16 * c0 + (c1 - c0) * f; s->dB = 2 * (c1 - c0);
}

YL_INLINE void yl_bnone(yl_brun *s, int n)      /* no bloom in this row */
{
    memset(s, 0, sizeof *s);
    s->n = n; s->z = 1; s->ab = 1;
}

/* the bloom on one pixel's clamped RGB (levels) into r4 g4 b4 (Q4 levels, clamped at 255) */
YL_INLINE void yl_bpix(const yl_ctx *g, int R, int G, int B, int aR, int aG, int aB, int *r4, int *g4, int *b4)
{
    int k = g->kl[(19595 * R + 38470 * G + 7471 * B) >> 14];
    int r = (R << 4) + ((aR * k) >> 15), gg = (G << 4) + ((aG * k) >> 15), b = (B << 4) + ((aB * k) >> 15);
    *r4 = r > 4080 ? 4080 : r; *g4 = gg > 4080 ? 4080 : gg; *b4 = b > 4080 ? 4080 : b;
}

/* the halation layer pow(clamp01(tint x mask), curve) of a pixel in Q4 levels, Q15 */
YL_INLINE int yl_layer(const yl_ctx *g, int r4, int g4, int b4)
{
    int lq = (19595 * r4 + 38470 * g4 + 7471 * b4) >> 17;
    if (lq < g->hmin) return 0;
    int t4 = r4 + ((1434 * g4 + 410 * b4 + 2048) >> 12);
    uint32_t idx = ((uint32_t)t4 * g->mtab[lq]) >> 16;
    return g->pw[idx > 4095 ? 4095 : idx];
}

/* both on: the halation layer of the bloomed frame, as film-lab blurs it (a second, read-only pass after yl_build) */
void yl_gather2_chunk(yl_ctx *g, const uint32_t *p, int npix, int x0, int y)
{
    if (!g->two || y < 0 || y >= g->H) return;
    if ((y >> 3) != g->pj) {
        if (g->pj >= 0) {
            uint16_t *P = g->P + g->pj * g->gw;
            for (int i = 0; i < g->gw; i++) { P[i] = (uint16_t)(g->pacc[i] >> 6); g->pacc[i] = 0; }
        }
        g->pj = y >> 3;
    }
    if (y != g->cur_y) {
        g->cur_y = y;
        yl_prep_b(g, y);
    }
    const int lmin = g->hmin >> 3;
    yl_brun bs;
    if (g->bl0 > g->bh0)
        yl_bnone(&bs, npix);
    else
        bs.n = 0;
    for (int x = x0, xe = x0 + npix; x < xe;) {
        if (!bs.n)
            yl_bseg(g, x, &bs);
        int n = (xe - x) >> 1, lim;
        if (bs.n < n) n = bs.n;
        bs.n -= n;
        lim = lmin - bs.ab;                      /* Y + lofs under this: no layer even with the most bloom of the pair */
        {                                        /* the cells of these pixels (the first gather's maxima) */
            const uint8_t *M = g->YM + (y >> 4) * g->bw;
            int c0 = M[x >> 4], c1 = M[(x + 2 * n - 1) >> 4];
            if ((c0 > c1 ? c0 : c1) < lim) {
                p += n; x += 2 * n;
                bs.R += n * bs.dR; bs.Gc += n * bs.dG; bs.B += n * bs.dB;
                continue;
            }
        }
        for (int j = 0; j < n; j++, p++, x += 2) {
            uint32_t w = *p, wb = w ^ 0x00800080u;
            int Y0 = (w >> 8) & 255, Y1 = w >> 24;
            if ((Y0 > Y1 ? Y0 : Y1) + g->lofs[((wb & 0xf8u) << 2) | ((wb >> 19) & 31u)] < lim)
                continue;
            int U = (int8_t)w, V = (int8_t)(w >> 16);
            int aR = (bs.R + j * bs.dR + 8) >> 4, aG = (bs.Gc + j * bs.dG + 8) >> 4, aB = (bs.B + j * bs.dB + 8) >> 4;
            int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8, r4, g4, b4;
            for (int q = 0; q < 2; q++) {
                int Y = q ? Y1 : Y0;
                yl_bpix(g, yl_c8(Y + rv), yl_c8(Y - gu), yl_c8(Y + bu), aR, aG, aB, &r4, &g4, &b4);
                g->pacc[x >> 3] += yl_layer(g, r4, g4, b4);
            }
        }
        bs.R += n * bs.dR; bs.Gc += n * bs.dG; bs.B += n * bs.dB;
    }
}

void yl_build2(yl_ctx *g, int y_begin, int y_end, int mode)
{
    (void)y_begin; (void)y_end;
    if (!g->two) return;
    if (g->pj >= 0) {
        uint16_t *P = g->P + g->pj * g->gw;
        for (int i = 0; i < g->gw; i++) { P[i] = (uint16_t)(g->pacc[i] >> 6); g->pacc[i] = 0; }
        g->pj = -1;
    }
    yl_build_h(g, mode);
    g->cur_y = -1;
}

/* film-lab's glow composite on one channel, Q4 levels (v <= 4080, a >= 0): v + a while the halo fills at most half the
 * headroom hd = 4080 - v (the knee, 0.5), past it 4080 - hd / 2 x exp(1 - 2a / hd): the same value and slope at the
 * knee, towards white without reaching it. No divide: 2a / hd from the reciprocal of hd's top 11 bits (CLZ; hd's
 * rounding moves the result by at most ~2 Q4 levels), Q12; exp from a table at 1/64 steps, linear in between. */
YL_INLINE int yl_glow_i(const yl_ctx *g, int v, int a)
{
    int hd = 4080 - v;
    if (2 * a <= hd) return v + a;
    if (hd <= 0) return 4080;
    int sh = 21 - __builtin_clz((unsigned)hd);         /* hd's top bit - 10 */
    int m = sh > 0 ? (hd + 1) >> 1 : hd << -sh;        /* 1024 .. 2047 (hd <= 4080: sh <= 1) */
    int u = (int)(((uint32_t)(2 * a) * g->grcp[m - 1024]) >> (14 + sh)) - 4096;
    if (u < 0) u = 0;
    int i = u >> 6;
    if (i >= YL_GE) return 4080;
    int e = g->gexp[i] - (((g->gexp[i] - g->gexp[i + 1]) * (u & 63)) >> 6);
    return 4080 - ((hd * e + 32768) >> 16);
}

int yl_glow(const yl_ctx *g, int v, int a)
{
    return yl_glow_i(g, v, a);
}

YL_INLINE uint32_t yl_pack(int U, int Y0, int V, int Y1)
{
    Y0 = yl_c8(Y0); Y1 = yl_c8(Y1);
    U = U < -128 ? -128 : U > 127 ? 127 : U;
    V = V < -128 ? -128 : V > 127 ? 127 : V;
    return (uint32_t)(U & 255) | ((uint32_t)Y0 << 8) | ((uint32_t)(V & 255) << 16) | ((uint32_t)Y1 << 24);
}

/* a word the linear case does not cover (a channel that clamps, or a halo past the knee on some channel): per pixel
 * the bloom with its clamp, then the halo through the glow composite, as film-lab orders them. The glows come from
 * the block: the bloom's Y / U / V back to RGB, the halo h times its colour */
static void __attribute__((noinline)) yl_apply_clip(const yl_ctx *g, uint32_t *p, int32_t a, int32_t c)
{
    int aY = a & 0xffff, h = a >> 16, aU = c >> 16, aV = (int16_t)c;
    int aR = aY + ((359 * aV) >> 8), aG = aY - ((88 * aU + 183 * aV) >> 8), aB = aY + ((454 * aU) >> 8);
    int hr = (h * g->kr) >> 15, hg = (h * g->kg) >> 15, hb = (h * g->kb) >> 15;
    if (aR < 0) aR = 0;
    if (aG < 0) aG = 0;
    if (aB < 0) aB = 0;
    uint32_t w = *p;
    int Y0 = (w >> 8) & 255, Y1 = w >> 24, U = (int8_t)w, V = (int8_t)(w >> 16);
    int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8, d[6];
    for (int q = 0; q < 2; q++) {
        int Y = q ? Y1 : Y0, R = yl_c8(Y + rv), G = yl_c8(Y - gu), B = yl_c8(Y + bu);
        int k = g->kl[(19595 * R + 38470 * G + 7471 * B) >> 14];
        int r4 = (R << 4) + ((aR * k) >> 15), g4 = (G << 4) + ((aG * k) >> 15), b4 = (B << 4) + ((aB * k) >> 15);
        d[3 * q] = yl_glow_i(g, r4 > 4080 ? 4080 : r4, hr) - (R << 4);
        d[3 * q + 1] = yl_glow_i(g, g4 > 4080 ? 4080 : g4, hg) - (G << 4);
        d[3 * q + 2] = yl_glow_i(g, b4 > 4080 ? 4080 : b4, hb) - (B << 4);
    }
    int nY0 = Y0 + ((19595 * d[0] + 38470 * d[1] + 7471 * d[2] + (1 << 19)) >> 20);
    int nY1 = Y1 + ((19595 * d[3] + 38470 * d[4] + 7471 * d[5] + (1 << 19)) >> 20);
    int sR = d[0] + d[3], sG = d[1] + d[4], sB = d[2] + d[5];
    int nU = U + ((-11059 * sR - 21709 * sG + 32768 * sB + (1 << 20)) >> 21);
    int nV = V + ((32768 * sR - 27439 * sG - 5329 * sB + (1 << 20)) >> 21);
    *p = yl_pack(nU, nY0, nV, nY1);
}

/* a block of m words from pixel x into q (yl_ctx.blk): per word the halo, the bloom's Y, the bloom's U << 16 | V.
 * Along a row each glow is linear between two cell centres (halation 8 i + 3.5, bloom 16 i + 7.5; the frame's edge
 * columns repeat), so inside a cell pair it is a value plus a step per word, kept x8 / x16. Returns 0 when the block
 * has no glow. */
static int yl_fill(const yl_ctx *g, int32_t *q, int x, int m)
{
    int32_t *qh = q, *qy = q + YL_BLK, *qc = q + 2 * YL_BLK;
    int any = 0;
    if (g->hl0 > g->hh0) {
        memset(qh, 0, 4u * m);
    } else {
        for (int j = 0; j < m;) {
            int t = x + 2 * j - 3, i = t >> 3, f = t & 7, n = (9 - f) >> 1;   /* x even: f odd */
            int ia = i < 0 ? 0 : i, ib = i + 1 >= g->gw ? g->gw - 1 : i + 1;
            int a0 = ia >= g->hl0 && ia <= g->hh0 ? g->vq[ia] : 0, a1 = ib >= g->hl0 && ib <= g->hh0 ? g->vq[ib] : 0;
            int acc = 8 * a0 + (a1 - a0) * f + 4, d = 2 * (a1 - a0);
            if (n > m - j) n = m - j;
            any |= a0 | a1;
            for (; n > 0; n--, j++, acc += d)
                qh[j] = acc >> 3;
        }
    }
    if (g->bl0 > g->bh0) {
        memset(qy, 0, 4u * m);
        memset(qc, 0, 4u * m);
        return any;
    }
    static const int16_t z4[4] = { 0, 0, 0, 0 };
    for (int j = 0; j < m;) {
        int t = x + 2 * j - 7, i = t >> 4, f = t & 15, n = (17 - f) >> 1;
        int ia = i < 0 ? 0 : i, ib = i + 1 >= g->bw ? g->bw - 1 : i + 1;
        const int16_t *c0 = ia >= g->bl0 && ia <= g->bh0 ? g->vb4 + 4 * ia : z4;
        const int16_t *c1 = ib >= g->bl0 && ib <= g->bh0 ? g->vb4 + 4 * ib : z4;
        int ay = 16 * c0[0] + (c1[0] - c0[0]) * f + 8, dy = 2 * (c1[0] - c0[0]);
        int au = 16 * c0[1] + (c1[1] - c0[1]) * f + 8, du = 2 * (c1[1] - c0[1]);
        int av = 16 * c0[2] + (c1[2] - c0[2]) * f + 8, dv = 2 * (c1[2] - c0[2]);
        if (n > m - j) n = m - j;
        any |= c0[3] | c1[3];
        for (; n > 0; n--, j++, ay += dy, au += du, av += dv) {
            qy[j] = ay >> 4;
            qc[j] = (int32_t)(((uint32_t)(au >> 4) << 16) | ((uint32_t)(av >> 4) & 0xffffu));
        }
    }
    return any;
}

/* the linear case's limit for the words of pixels xu .. xu + 15 (xu % 16 == 0) from the cells their glows come from
 * (halation cells xu / 8 - 1 .. xu / 8 + 2, bloom xu / 16 - 1 .. xu / 16 + 1): the most Y + moff can be with no
 * channel clamping after the bloom and the halo under the glow composite's knee (twice the halo fits) on every channel */
YL_INLINE int yl_limit(const yl_ctx *g, int xu)
{
    int hm = 0, bm = 0;
    if (g->hl0 <= g->hh0) {
        int i0 = (xu >> 3) - 1, i1 = i0 + 3;
        if (i0 < g->hl0) i0 = g->hl0;
        if (i1 > g->hh0) i1 = g->hh0;
        for (int i = i0; i <= i1; i++)
            if (g->vq[i] > hm) hm = g->vq[i];
    }
    if (g->bl0 <= g->bh0) {
        int i0 = (xu >> 4) - 1, i1 = i0 + 2;
        if (i0 < g->bl0) i0 = g->bl0;
        if (i1 > g->bh0) i1 = g->bh0;
        for (int i = i0; i <= i1; i++) {
            const int16_t *c = g->vb4 + 4 * i;
            if (c[3] > bm) bm = c[3];
        }
    }
    return 254 - (bm ? ((bm * g->klmax) >> 19) + 1 : 0) - ((2 * hm * g->kr) >> 19);
}

void yl_apply_chunk(yl_ctx *g, uint32_t *p, int npix, int x0, int y)
{
    if (y < 0 || y >= g->H) return;
    if (y != g->cur_y) yl_prep_row(g, y);
    if (g->hl0 > g->hh0 && g->bl0 > g->bh0) return;
    const int hy = g->hy, hu = g->hu, hv = g->hv;
    const uint32_t *bint = g->bint;
    const uint16_t *ky = g->ky;
    int32_t *q = g->blk;
    for (int x = x0, xe = x0 + npix; x < xe; x += 2 * YL_BLK, p += YL_BLK) {
        int m = (xe - x) >> 1;
        if (m > YL_BLK) m = YL_BLK;
        if (!yl_fill(g, q, x, m))
            continue;
        for (int j = 0; j < m;) {
            int xu = (x + 2 * j) & ~15, je = j + ((xu + 16 - (x + 2 * j)) >> 1), A = yl_limit(g, xu);
            if (je > m) je = m;
            for (; j < je; j++) {
                int h = q[j], aY = q[YL_BLK + j];
                int32_t c = q[2 * YL_BLK + j];
                if (!(h | aY | c))
                    continue;
                uint32_t w = p[j], wb = w ^ 0x00800080u;
                int Y0 = (w >> 8) & 255, Y1 = w >> 24;
                uint32_t t = bint[((wb & 0xf8u) << 2) | ((wb >> 19) & 31u)];
                int lo = Y0 < Y1 ? Y0 : Y1, hi = Y0 < Y1 ? Y1 : Y0;
                /* the linear case: no channel of either pixel clamps, before or after, and the halo stays under the
                 * glow composite's knee on every channel (where it adds as it is): the change is the bloom glow x k(Y)
                 * plus the halo, so its Y / U / V parts come from the word's glow once */
                if (hi + (int)(t & 255) > A || lo < (int)((t >> 8) & 255)) {
                    yl_apply_clip(g, p + j, aY | h << 16, c);
                    continue;
                }
                int U = (int8_t)w, V = (int8_t)(w >> 16), k0 = ky[Y0], k1 = ky[Y1], ks = k0 + k1, dh = ((h * hy) >> 15) + 8;
                int nY0 = Y0 + ((dh + ((aY * k0) >> 15)) >> 4), nY1 = Y1 + ((dh + ((aY * k1) >> 15)) >> 4);
                int nU = U + ((((h * hu) >> 15) + 8 + (((c >> 16) * ks) >> 16)) >> 4);
                int nV = V + ((((h * hv) >> 15) + 8 + (((int16_t)c * ks) >> 16)) >> 4);
                if ((unsigned)(nU + 128) <= 255u && (unsigned)(nV + 128) <= 255u)
                    p[j] = (uint32_t)(nU & 255) | (uint32_t)nY0 << 8 | (uint32_t)(nV & 255) << 16 | (uint32_t)nY1 << 24;
                else
                    p[j] = yl_pack(nU, nY0, nV, nY1);
            }
        }
    }
}

/* ------------------------------------------------------------------ grain */
/* double without double literals: the module builds with -fsingle-precision-constant, so every constant here is an
 * integer ratio (exact or correctly rounded) */
static double yl_pi(void) { return (double)7074237752028440LL / (double)(1LL << 51); }   /* the double nearest pi */

static int yl_dfloor(double x)                    /* |x| < 2^31 */
{
    int i = (int)x;
    return (double)i > x ? i - 1 : i;
}

/* sin and cos in double (no double libm in the core: sinf only): x in turns of the nearest quarter, then Taylor to
 * the 17th power on [-pi/4, pi/4] (error < 1e-16) */
static double yl_tc[18];                         /* (-1)^(n/2) / n!, n = 2..17 (yl_grain_init) */

static void yl_dsincos(double x, double *s, double *c)
{
    double pi = yl_pi(), t = x / (2.0 * pi);
    int q = yl_dfloor(4.0 * t + 0.5);
    double r = (t - q * 0.25) * (2.0 * pi), r2 = r * r, ps = 0.0, pc = 0.0;
    for (int n = 17; n >= 3; n -= 2) ps = (ps + yl_tc[n]) * r2;   /* Horner */
    for (int n = 16; n >= 2; n -= 2) pc = (pc + yl_tc[n]) * r2;
    double sv = r + r * ps, cv = 1.0 + pc;
    switch (q & 3) {
    case 0: *s = sv; *c = cv; break;
    case 1: *s = cv; *c = -sv; break;
    case 2: *s = -sv; *c = -cv; break;
    default: *s = -cv; *c = sv; break;
    }
}

/* film-lab's three boxes for a Gaussian of sigma px as one kernel: h[0 .. 2r] (sum 1); returns r. *k1 = sum h^2 (the
 * 2-D kernel's is its square), *sd = the boxes' sigma; a blur whose boxes are all under 3 px is a copy (k1 1, sd 0) */
static int yl_gkern(int sigma, float *h, float *k1, float *sd)
{
    int w[3], n = 1;
    float u[2 * YL_GR + 1], v = 0.0f;
    yl_boxes((float)sigma, w);
    h[0] = 1.0f;
    *k1 = 1.0f; *sd = 0.0f;
    if (w[0] < 3 && w[1] < 3 && w[2] < 3)
        return 0;
    for (int k = 0; k < 3; k++) {
        int m = n + w[k] - 1;
        if (m > 2 * YL_GR + 1) return 0;           /* not with the presets (sigma <= 6 px) */
        for (int i = 0; i < m; i++) {
            float s = 0.0f;
            for (int j = 0; j < n; j++) if (i - j >= 0 && i - j < w[k]) s += h[j];
            u[i] = s / w[k];
        }
        n = m;
        memcpy(h, u, sizeof(float) * n);
        v += (w[k] * (float)w[k] - 1.0f) / 12.0f;
    }
    *k1 = 0.0f;
    for (int i = 0; i < n; i++) *k1 += h[i] * h[i];
    *sd = sqrtf(v);
    return (n - 1) / 2;
}

/* one axis of the grain field at the YL_TS fine samples of a period (lp px, P lattice cells, cell s = lp / P px):
 * sample m (at m lp / YL_TS px) = sum over the blur's taps k of h[k] x the normalised bilinear weights of the two
 * lattice points round m lp / YL_TS - k (film-lab's lattice noise: the weights over their root sum of squares, which
 * splits per axis), the lattice shifted by o cells. Per sample: the first lattice point (wrapped) and YL_GT weights,
 * Q15 */
static void yl_gaxis(int lp, int P, float o, const float *h, int r, int16_t *ib, int32_t *W)
{
    float s = (float)lp / (float)P;
    memset(W, 0, sizeof(int32_t) * YL_TS * YL_GT);
    for (int m = 0; m < YL_TS; m++) {
        float x = (float)m * (float)lp / (float)YL_TS, acc[YL_GT];
        int base = (int)floorf((x - r) / s - o);
        memset(acc, 0, sizeof acc);
        for (int k = -r; k <= r; k++) {
            float q = (x - k) / s - o, f;
            int i0 = (int)floorf(q), t = i0 - base;
            f = q - (float)i0;
            float nf = 1.0f / sqrtf((1.0f - f) * (1.0f - f) + f * f);
            if (t >= 0 && t < YL_GT) acc[t] += h[k + r] * (1.0f - f) * nf;
            if (t + 1 >= 0 && t + 1 < YL_GT) acc[t + 1] += h[k + r] * f * nf;
        }
        for (int t = 0; t < YL_GT; t++) W[m * YL_GT + t] = (int32_t)floorf(acc[t] * 32768.0f + 0.5f);
        ib[m] = (int16_t)(((base % P) + P) % P);
    }
}

int yl_grain_init(yl_grain *g, int gauge, int H, int bw, void *tile_mem, void *scratch)
{
    int ga = gauge >= 0 && gauge < YL_NG ? gauge : 2, maxabs = 1, P, lp, r;
    int16_t *pl[3], *L, *X, *ib;
    int32_t *W;
    double *sx, *cx, *sy, *cy;
    float h[2 * YL_GR + 1], ck1, csd, sp, gain;
    memset(g, 0, sizeof *g);
    g->tile = (int8_t *)tile_mem;
    g->ry = (int16_t *)((int8_t *)tile_mem + YL_TS * YL_TS * 3);
    g->ru = g->ry + YL_LPMAX;
    g->rv = g->ru + YL_LPMAX / 2;
    g->gauge = ga;
    g->bw = bw;
    g->cy = -1;
    {
        double f = 1.0;
        for (int n = 2; n <= 17; n++) { f *= n; yl_tc[n] = ((n >> 1) & 1 ? -1.0 : 1.0) / f; }
    }
    for (int c = 0; c < 3; c++) pl[c] = (int16_t *)scratch + c * YL_TS * YL_TS;
    L = pl[2] + YL_TS * YL_TS;                    /* P x P lattice of one channel */
    X = L + YL_TS * YL_TS;                        /* its x pass: P rows x YL_TS */
    W = (int32_t *)(X + YL_TS * YL_TS);
    ib = (int16_t *)(W + YL_TS * YL_GT);
    sx = (double *)(ib + YL_TS);
    cx = sx + YL_TS; sy = cx + YL_TS; cy = sy + YL_TS;

    /* film-lab's grain: lattice noise of the cell size cells (the hash at whole cells, bilinear in between over the weights'
     * root sum of squares), R G B on three lattices (shifted 17.13 / -9.71 and -5.37 / 23.19 cells, seeds 1, 2.31,
     * 3.73), blurred by the clump (round(clump x H / 1080) px). Here the lattice is P x P cells, periodic, the
     * field's YL_TS x YL_TS samples of that period computed once (the exact kernel per axis), and a pixel interpolates
     * them; the lattice's turn is left out (it turns the pattern by a degree, the camera's pattern moves per shot anyway).
     * A sample spacing of 0.5 sqrt(sigma^2 + 0.22 s^2) px keeps the interpolation within ~3 % rms of the exact field
     * (measured over the presets, 3456 and 3744 rows; the even spacing below: under 4 %) */
    sp = (1.0f + (yl_gp[ga].cell - 1.0f) / 47.0f * 11.0f) * ((float)H / 1080.0f);
    if (sp < 0.5f) sp = 0.5f;
    g->clump = (int)floorf(yl_gp[ga].clump10 / 10.0f * H / 1080.0f + 0.5f);
    r = yl_gkern(g->clump, h, &ck1, &csd);
    {
        /* d px between samples, a whole even number (a word's two pixels in one span; the largest under 1.15 x the
         * above), so the period is YL_TS d px: the lattice is P cells of lp / P px (within 1 % of the cell size) */
        int d = 2 * (int)(0.575f * sqrtf(csd * csd + 0.22f * sp * sp) / 2.0f);
        if (d < 2) d = 2;
        if (d > YL_LPMAX / YL_TS) d = YL_LPMAX / YL_TS;
        lp = YL_TS * d;
        P = (int)floorf(lp / sp + 0.5f);
        if (P > YL_TS) P = YL_TS;
        if (P < 2) P = 2;
        g->d = d;
        g->st = (4096 + d / 2) / d;
    }
    g->lat = P;
    g->lp = lp;
    /* the clamps of yl_grain_row: |grain| < 64 levels (tile x256 x kz, both under 2^15, >> 24) */
    for (int i = 0; i < 384; i++) {
        int v = i - 64, u = i - 192;
        g->cly[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        g->clu[i] = (uint8_t)((u < -128 ? -128 : u > 127 ? 127 : u) & 255);
    }
    {
        static const short ox[3] = { 0, 87, 37 }, oy[3] = { 0, 71, 81 }, sd[3] = { 100, 231, 373 };   /* /100 */
        double k1 = (double)129898 / 10000, k2 = (double)78233 / 1000, k3 = (double)37719 / 1000;
        double big = (double)437585453 / 10000;
        for (int i = 0; i < P; i++) yl_dsincos(i * k1, sx + i, cx + i);
        for (int c = 0; c < 3; c++) {
            int64_t s1 = 0, s2 = 0;
            int n = P * P;
            for (int j = 0; j < P; j++) yl_dsincos(j * k2 + k3 * ((double)sd[c] / 100), sy + j, cy + j);
            for (int j = 0; j < P; j++)
                for (int i = 0; i < P; i++) {
                    double v = (sx[i] * cy[j] + cx[i] * sy[j]) * big;       /* sin(i k1 + (j k2 + seed k3)) x big */
                    int q = (int)((v - yl_dfloor(v)) * 32768.0) - 16384;     /* Q14 of fract * 2 - 1 */
                    L[j * P + i] = (int16_t)q;
                    s1 += q; s2 += (int64_t)q * q;
                }
            /* the P^2 hash values to mean 0 and the uniform's std (1 / sqrt 3), so the strength does not ride on the draw */
            {
                float mu = (float)s1 / n, var = (float)s2 / n - mu * mu;
                int32_t k = var > 0.0f ? (int32_t)(16384.0f / sqrtf(3.0f) / sqrtf(var) * 65536.0f + 0.5f) : 65536;
                int m0 = (int)floorf(mu + 0.5f);
                for (int i = 0; i < n; i++) {
                    int v = (int)(((int64_t)(L[i] - m0) * k) >> 16);
                    L[i] = (int16_t)(v < -32767 ? -32767 : v > 32767 ? 32767 : v);
                }
            }
            yl_gaxis(lp, P, ox[c] / 100.0f, h, r, ib, W);                    /* x: P rows of YL_TS */
            for (int j = 0; j < P; j++)
                for (int m = 0; m < YL_TS; m++) {
                    const int32_t *w = W + m * YL_GT;
                    int32_t acc = 1 << 14;
                    for (int t = 0, i = ib[m]; t < YL_GT; t++, i = i + 1 == P ? 0 : i + 1)
                        if (w[t]) acc += w[t] * L[j * P + i];
                    X[j * YL_TS + m] = (int16_t)(acc >> 15);
                }
            yl_gaxis(lp, P, oy[c] / 100.0f, h, r, ib, W);                    /* y: the samples, Q13 */
            for (int n2 = 0; n2 < YL_TS; n2++) {
                const int32_t *w = W + n2 * YL_GT;
                for (int m = 0; m < YL_TS; m++) {
                    int32_t acc = 1 << 15;
                    for (int t = 0, j = ib[n2]; t < YL_GT; t++, j = j + 1 == P ? 0 : j + 1)
                        if (w[t]) acc += w[t] * X[j * YL_TS + m];
                    acc >>= 16;
                    pl[c][n2 * YL_TS + m] = (int16_t)(acc < -32767 ? -32767 : acc > 32767 ? 32767 : acc);
                }
            }
        }
    }
    /* colour: n = mono + (n_c - mono) x color, mono = the mean of the three; then the Y / U / V parts (full-range
     * BT.601), Q14 coefficients, in place. A pixel between samples is their bilinear mix, which lowers the variance by
     * (2 + rho) / 3 per axis (rho: the correlation of neighbouring samples): each plane is raised by that */
    {
        float col = yl_gp[ga].chroma / 100.0f, M[3][3] = { { 0.299f, 0.587f, 0.114f }, { -0.168736f, -0.331264f, 0.5f },
                                                        { 0.5f, -0.418688f, -0.081312f } };
        int K[3][3], sc[3];
        int64_t s1[3] = { 0, 0, 0 }, s2[3] = { 0, 0, 0 }, sxx[3] = { 0, 0, 0 }, syy[3] = { 0, 0, 0 };
        for (int o = 0; o < 3; o++) {
            float rs = M[o][0] + M[o][1] + M[o][2];
            for (int c = 0; c < 3; c++) K[o][c] = (int)floorf((M[o][c] * col + rs * (1.0f - col) / 3.0f) * 16384.0f + 0.5f);
        }
        for (int i = 0; i < YL_TS * YL_TS; i++) {
            int r0 = pl[0][i], g0 = pl[1][i], b0 = pl[2][i];
            for (int o = 0; o < 3; o++) {
                int v = (K[o][0] * r0 + K[o][1] * g0 + K[o][2] * b0) >> 14;
                pl[o][i] = (int16_t)(v < -32767 ? -32767 : v > 32767 ? 32767 : v);
            }
        }
        for (int y = 0; y < YL_TS; y++)
            for (int x = 0; x < YL_TS; x++) {
                int i = y * YL_TS + x, ix = y * YL_TS + ((x + 1) & (YL_TS - 1)), iy = ((y + 1) & (YL_TS - 1)) * YL_TS + x;
                for (int o = 0; o < 3; o++) {
                    int v = pl[o][i];
                    s1[o] += v; s2[o] += v * v; sxx[o] += v * pl[o][ix]; syy[o] += v * pl[o][iy];
                }
            }
        for (int o = 0; o < 3; o++) {
            float n = (float)(YL_TS * YL_TS), mu = (float)s1[o] / n, var = (float)s2[o] / n - mu * mu;
            float rx = var > 0.0f ? ((float)sxx[o] / n - mu * mu) / var : 1.0f, ry = var > 0.0f ? ((float)syy[o] / n - mu * mu) / var : 1.0f;
            sc[o] = (int)(65536.0f / sqrtf((2.0f + rx) / 3.0f * (2.0f + ry) / 3.0f) + 0.5f);
        }
        for (int pass = 0; pass < 2; pass++) {
            int32_t rc = pass ? (int32_t)(((uint32_t)127 << 16) / (uint32_t)maxabs) : 0;
            for (int y = 0; y < YL_TS; y++)
                for (int x = 0; x < YL_TS; x++)
                    for (int o = 0; o < 3; o++) {
                        int v = (int)(((int64_t)pl[o][y * YL_TS + x] * sc[o]) >> 16);
                        if (!pass) { int a = v < 0 ? -v : v; if (a > maxabs) maxabs = a; }
                        else {
                            int q = (int)(((int64_t)v * rc + (v < 0 ? -32768 : 32768)) >> 16);
                            g->tile[(y * 3 + o) * YL_TS + x] = (int8_t)(q < -127 ? -127 : q > 127 ? 127 : q);
                        }
                    }
        }
    }
    g->maxabs = maxabs;
    /* film-lab's strength: amount x 0.28 x the gain that keeps the grain as the 800-row preview shows it at any
     * resolution (the reference's gain: the reference blur's sqrt(k2) x min(sqrt(1 + 4 pi (sigma / the cell size)^2 / 2.45), 1 /
     * sqrt(k2) of this blur)); then zone(L) x that x 255 levels per tile unit (maxabs / 127 of Q13), Q16 */
    {
        float hr[2 * YL_GR + 1], rk1, rsd, x = 1.0f;
        yl_gkern((int)floorf(yl_gp[ga].clump10 / 10.0f * 800.0f / 1080.0f + 0.5f), hr, &rk1, &rsd);
        if (csd > 0.0f) {
            float cells = csd / sp;
            x = sqrtf(1.0f + 4.0f * 3.14159265f * cells * cells / 2.45f);
        }
        gain = rk1 * (x < 1.0f / ck1 ? x : 1.0f / ck1);
        g->gain_q12 = (int)(gain * 4096.0f + 0.5f);
        float a = yl_gp[ga].amt / 100.0f * 0.28f * gain * 255.0f * (maxabs / 127.0f / 8192.0f) * 65536.0f;
        for (int v = 0; v < 256; v++) {
            float Lm = v / 255.0f;
            float ws = 1.0f - yl_ss(0.0f, 0.35f, Lm), wm = yl_ss(0.12f, 0.45f, Lm) * (1.0f - yl_ss(0.55f, 0.88f, Lm));
            float wh = yl_ss(0.62f, 1.0f, Lm), z = ws * yl_gp[ga].zd / 100.0f + wm * yl_gp[ga].zm / 100.0f + wh * yl_gp[ga].zb / 100.0f;
            g->kz[v] = (int16_t)(z * a + 0.5f);
        }
    }
    yl_grain_shot(g, 0);
    return 0;
}

void yl_grain_shot(yl_grain *g, uint32_t seed)
{
    uint32_t h = seed * 0x9E3779B1u + 0x7F4A7C15u;
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13;
    g->ox = (int)((h & 0xffffu) % (uint32_t)g->lp) & ~1;    /* even: a word's two pixels stay in one period */
    g->oy = (int)((h >> 16) % (uint32_t)g->lp);
    g->cy = -1;
}

/* the grain of pixel row y over one period: per pixel Y, per word U and V, x256 tile units; the two sample rows round
 * the row mixed by its position between them, then each sample's d pixels (d / 2 words) walked linearly to the next */
static void yl_grain_build(yl_grain *g, int y)
{
    const int d = g->d, h = d >> 1, lp = g->lp, st = g->st;
    int t = ((y + g->oy) % lp + lp) % lp, n0 = t / d, fy = ((t - n0 * d) << 8) / d, n1 = (n0 + 1) & (YL_TS - 1);
    const int8_t *a = g->tile + n0 * 3 * YL_TS, *b = g->tile + n1 * 3 * YL_TS;
    int16_t *r = g->ry;
    int c0 = a[0] * 256 + (b[0] - a[0]) * fy;
    for (int m = 0; m < YL_TS; m++) {
        int m1 = (m + 1) & (YL_TS - 1), c1 = a[m1] * 256 + (b[m1] - a[m1]) * fy;
        int32_t acc = (c0 << 12) + 2048, dd = (c1 - c0) * st;
        for (int j = 0; j < h; j++, r += 2, acc += 2 * dd) {
            r[0] = (int16_t)(acc >> 12);
            r[1] = (int16_t)((acc + dd) >> 12);
        }
        c0 = c1;
    }
    if (!g->bw) {
        const int8_t *au = a + YL_TS, *bu = b + YL_TS, *av = a + 2 * YL_TS, *bv = b + 2 * YL_TS;
        int16_t *u = g->ru, *v = g->rv;
        int u0 = au[0] * 256 + (bu[0] - au[0]) * fy, v0 = av[0] * 256 + (bv[0] - av[0]) * fy;
        for (int m = 0; m < YL_TS; m++) {
            int m1 = (m + 1) & (YL_TS - 1);
            int u1 = au[m1] * 256 + (bu[m1] - au[m1]) * fy, v1 = av[m1] * 256 + (bv[m1] - av[m1]) * fy;
            int32_t ua = (u0 << 12) + 2048, va = (v0 << 12) + 2048, ud = 2 * (u1 - u0) * st, vd = 2 * (v1 - v0) * st;
            for (int j = 0; j < h; j++, ua += ud, va += vd) {
                *u++ = (int16_t)(ua >> 12);
                *v++ = (int16_t)(va >> 12);
            }
            u0 = u1; v0 = v1;
        }
    }
    g->cy = y;
}

void yl_grain_row(yl_grain *g, uint32_t *p, int npix, int x0, int y)
{
    if (y != g->cy) yl_grain_build(g, y);
    const int16_t *kz = g->kz;
    const uint8_t *cly = g->cly + 64, *clu = g->clu + 192;
    const int lp = g->lp;
    int tx = (x0 + g->ox) % lp;                   /* x0 and ox even: tx even */
    uint32_t *e = p + (npix >> 1);
    while (p < e) {                               /* runs up to the period's end */
        int n = (lp - tx) >> 1;
        if (n > e - p) n = (int)(e - p);
        const int16_t *py = g->ry + tx, *pu = g->ru + (tx >> 1), *pv = g->rv + (tx >> 1);
        uint32_t *pe = p + n;
        if (g->bw) {
            for (; p < pe; p++, py += 2) {
                uint32_t w = *p;
                int Y0 = (w >> 8) & 255, Y1 = w >> 24;
                int nY0 = Y0 + ((py[0] * kz[Y0] + (1 << 23)) >> 24), nY1 = Y1 + ((py[1] * kz[Y1] + (1 << 23)) >> 24);
                *p = (w & 0x00ff00ffu) | ((uint32_t)cly[nY0] << 8) | ((uint32_t)cly[nY1] << 24);
            }
        } else {
            for (; p < pe; p++, py += 2, pu++, pv++) {
                uint32_t w = *p;
                int Y0 = (w >> 8) & 255, Y1 = w >> 24, k0 = kz[Y0], k1 = kz[Y1], ks = k0 + k1;
                int nY0 = Y0 + ((py[0] * k0 + (1 << 23)) >> 24), nY1 = Y1 + ((py[1] * k1 + (1 << 23)) >> 24);
                int nU = (int8_t)w + ((*pu * ks + (1 << 24)) >> 25), nV = (int8_t)(w >> 16) + ((*pv * ks + (1 << 24)) >> 25);
                *p = clu[nU] | ((uint32_t)cly[nY0] << 8) | ((uint32_t)clu[nV] << 16) | ((uint32_t)cly[nY1] << 24);
            }
        }
        tx = 0;
    }
}
