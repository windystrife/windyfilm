/* wf_rsrc.c - RAW develop "B": which buffer holds this shot's raw (wf_rsrc.h). WindyStrife. */
#include <stdint.h>
#include <stddef.h>
#include "wf_rsrc.h"
#include "wf_dev.h"                              /* wf_dev_unpack14 */

static const char *const wf_rs_wname[WF_RS_N] = { "ok", "few", "nomatch", "nocand" };
const char *wf_rs_why(int why) { return why >= 0 && why < WF_RS_N ? wf_rs_wname[why] : "?"; }

static float wf_rs_sqrt(float v)                 /* no libm in the module: Newton from a power-of-two guess */
{
    if (v <= 0.0f)
        return 0.0f;
    float x = 1.0f;
    while (x * x < v) x *= 2.0f;
    while (x * x > 4.0f * v) x *= 0.5f;
    for (int i = 0; i < 8; i++)
        x = 0.5f * (x + v / x);
    return x;
}

/* the block's mean Y in quarter codes (0 .. 1020): rows by .. by + 3, columns bx .. bx + 7 (bx even) */
static int wf_rs_yq(const uint32_t *yuv, int w, int bx, int by)
{
    uint32_t s = 0;
    for (int r = 0; r < 4; r++) {
        const uint32_t *p = yuv + (size_t)(by + r) * (w / 2) + bx / 2;
        for (int k = 0; k < 4; k++)
            s += ((p[k] >> 8) & 0xff) + (p[k] >> 24);
    }
    return (int)((s + 4) >> 3);                  /* 32 samples, x 4 */
}

/* the block's channel sums (black subtracted): raw x0 + bx (a pixblock), rows y0 + by .. + 3. o[0] / o[2] = the two
   non-green colours (8 photosites each: which is R and which B does not matter to the fit), o[1] = green (16) */
static void wf_rs_rgb(const uint8_t *s0, const struct wf_rs_geom *g, int bx, int by, int32_t o[3])
{
    int16_t px[8];
    int rx = g->x0 + bx, ry = g->y0 + by;
    o[0] = o[1] = o[2] = 0;
    for (int r = 0; r < 4; r++) {
        wf_dev_unpack14(s0 + (size_t)(ry + r) * g->pitch + (size_t)(rx >> 3) * 14, px, 8, g->black);
        int odd = ((ry + r) & 1) ? !g->g_odd : g->g_odd;   /* green columns of this row: odd (1) or even (0) */
        int oc = (ry + r) & 1 ? 2 : 0;                     /* the row's other colour */
        for (int k = 0; k < 8; k++)
            o[(k & 1) == odd ? 1 : oc] += px[k];
    }
}

/* fit y = a R + b G + c B + d over the blocks (least squares, centred, a small ridge for neutral scenes where the three
   channels move together): returns R^2, the share of y's variance the raw explains (scale, offset and WB free) */
static float wf_rs_r2(const float (*x)[3], const float *y, int n)
{
    float mx[3], my = 0.0f, A[3][4], syy = 0.0f;   /* no initialisers: they become memset calls */
    for (int r = 0; r < 3; r++) {
        mx[r] = 0.0f;
        for (int c = 0; c < 4; c++) A[r][c] = 0.0f;
    }
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 3; c++) mx[c] += x[i][c];
        my += y[i];
    }
    for (int c = 0; c < 3; c++) mx[c] /= (float)n;
    my /= (float)n;
    for (int i = 0; i < n; i++) {
        float d[3], e = y[i] - my;
        for (int c = 0; c < 3; c++) d[c] = x[i][c] - mx[c];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) A[r][c] += d[r] * d[c];
            A[r][3] += d[r] * e;
        }
        syy += e * e;
    }
    if (syy <= 0.0f)
        return 0.0f;
    float sxy[3] = { A[0][3], A[1][3], A[2][3] };
    float rg = 1e-4f * (A[0][0] + A[1][1] + A[2][2]) / 3.0f + 1e-20f;
    for (int c = 0; c < 3; c++) A[c][c] += rg;
    for (int c = 0; c < 3; c++) {                /* Gauss-Jordan, partial pivoting */
        int p = c;
        for (int r = c + 1; r < 3; r++)
            if ((A[r][c] < 0 ? -A[r][c] : A[r][c]) > (A[p][c] < 0 ? -A[p][c] : A[p][c])) p = r;
        if (p != c)
            for (int k = 0; k < 4; k++) { float t = A[c][k]; A[c][k] = A[p][k]; A[p][k] = t; }
        if (A[c][c] == 0.0f)
            return 0.0f;
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            float f = A[r][c] / A[c][c];
            for (int k = c; k < 4; k++) A[r][k] -= f * A[c][k];
        }
    }
    float ex = 0.0f;
    for (int c = 0; c < 3; c++) ex += A[c][3] / A[c][c] * sxy[c];
    float r2 = ex / syy;
    return r2 < 0.0f ? 0.0f : r2 > 1.0f ? 1.0f : r2;
}

int wf_rs_pick(const uint32_t *cand, int ncand, const uint32_t *yuv, const struct wf_rs_geom *g, const uint32_t *dec,
               struct wf_rs_res *r)
{
    static int16_t bxs[WF_RS_NS], bys[WF_RS_NS];
    static float yl[WF_RS_NS];
    r->pick = -1;
    r->n = 0;
    r->why = WF_RS_NOCAND;
    for (int k = 0; k < WF_RS_NCAND; k++)
        r->score[k] = -9999;
    if (ncand > WF_RS_NCAND)
        ncand = WF_RS_NCAND;
    int any = 0;
    for (int k = 0; k < ncand; k++)
        any |= cand[k] != 0;
    if (!any || !yuv || g->w < 256 || g->h < 128)
        return -1;
    /* sample blocks: a 16 x 16 grid; per cell the first of 5 places whose mean Y is a mid-tone */
    int ax = (8 - (g->x0 & 7)) & 7;              /* bx = ax (mod 8): raw x0 + bx starts a pixblock */
    int cw = g->w / 16, ch = g->h / 16, n = 0;
    static const int8_t dx[5] = { 0, -1, 1, 0, 0 }, dy[5] = { 0, 0, 0, -1, 1 };
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 16; cx++)
            for (int t = 0; t < 5; t++) {
                int bx = cx * cw + cw / 2 + dx[t] * (cw / 4), by = cy * ch + ch / 2 + dy[t] * (ch / 4);
                bx = (bx & ~7) + ax;
                by &= ~1;
                if (bx < 0 || bx + 8 > g->w || by < 0 || by + 4 > g->h)
                    continue;
                int q = wf_rs_yq(yuv, g->w, bx, by);
                if (q < 40 * 4 || q > 210 * 4)
                    continue;
                bxs[n] = (int16_t)bx;
                bys[n] = (int16_t)by;
                /* lin Q16 through the JPEG's curve; without one, code^2 (sRGB is close to gamma 2.2: enough for a
                   correlation, which only needs the same order and roughly the same spacing) */
                yl[n] = dec ? (float)(int32_t)(dec[q] >> 8) : (float)(q * q);
                n++;
                break;
            }
    r->n = n;
    if (n < WF_RS_NMIN) {
        r->why = WF_RS_FEW;
        return -1;
    }
    int best = -1, bs = -9999, second = -9999;
    for (int k = 0; k < ncand; k++) {
        if (!cand[k])
            continue;
        const uint8_t *s0 = (const uint8_t *)(uintptr_t)cand[k];
        static float xl[WF_RS_NS][3];
        for (int i = 0; i < n; i++) {
            int32_t o[3];
            wf_rs_rgb(s0, g, bxs[i], bys[i], o);
            for (int c = 0; c < 3; c++) xl[i][c] = (float)o[c] * (1.0f / 1024.0f);
        }
        /* score = the multiple correlation sqrt(R^2) x 1000 */
        int sc = (int)(1000.0f * wf_rs_sqrt(wf_rs_r2((const float (*)[3])xl, yl, n)) + 0.5f);
        r->score[k] = sc;
        if (sc > bs) {
            second = bs;
            bs = sc;
            best = k;
        } else if (sc > second) {
            second = sc;
        }
    }
    if (best < 0)
        return -1;
    if (bs < WF_RS_MIN) {
        r->why = WF_RS_NOMATCH;
        return -1;
    }
    /* a tie (the other buffer holds the same scene, e.g. a tripod shot a moment before): the earlier candidate */
    for (int k = 0; k < best; k++)
        if (cand[k] && r->score[k] >= bs - WF_RS_TIE) {
            best = k;
            break;
        }
    (void)second;
    r->pick = best;
    r->why = WF_RS_OK;
    return best;
}
