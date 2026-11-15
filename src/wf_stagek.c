/* wf_stagek.c - WindyStrife: compose a film-stage film's 17^3 table from its negative and print halves (wf_stagek.h).
 * Pure computation, no I/O: the module calls it from wf_task (wf_shot.c), tools/mk_wfstage.c calls the same file on the
 * PC to check it against the chain. Integer only: armv5t, no float, 64-bit products (SMULL) and constant 64-bit shifts,
 * no division (no __aeabi_* helper). */
#include "wf_stagek.h"

typedef char wf_sg_neg_size[sizeof(struct wf_sg_neg) == 24 + 2 * (WF_SG_NX + 1) + 2 * 3 * WF_SG_N1 + 2 + 12 * WF_SG_A3 ? 1 : -1];
typedef char wf_sg_prn_size[sizeof(struct wf_sg_prn) == 24 + 2 * 3 * WF_SG_N1 + 2 + 6 * WF_SG_B3 + 2 ? 1 : -1];

static int wf_sg_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Exp comp takes Film exp and Push / pull out (the chain's exposure_compensation: exposure = -push); Film exp and
 * Push / pull are both an offset on the same exposure axis at the same scale (a third of a stop = 0.1 logE), so what
 * the table depends on is their sum, in thirds of a stop */
static int wf_sg_off(const struct wf_sg_knobs *k)
{
    return k->comp ? 0 : wf_sg_clamp(k->fexp, -WF_SG_FEXP_MAX, WF_SG_FEXP_MAX)
                         + wf_sg_clamp(k->push, -WF_SG_PUSH_MAX, WF_SG_PUSH_MAX);
}

int wf_sg_default(const struct wf_sg_knobs *k)
{
    return wf_sg_off(k) == 0 && !k->prn[0] && !k->prn[1] && !k->prn[2] && k->iie == WF_SG_IIE_DEF;
}

uint32_t wf_sg_key(const struct wf_sg_knobs *k)
{
    if (wf_sg_default(k))
        return 0;
    uint32_t key = 1u | (uint32_t)(wf_sg_off(k) + 16) << 1 | (uint32_t)wf_sg_clamp(k->iie, 0, WF_SG_IIE_MAX) << 21;
    for (int c = 0; c < 3; c++)
        key |= (uint32_t)(wf_sg_clamp(k->prn[c], -WF_SG_PRN_MAX, WF_SG_PRN_MAX) + 16) << (6 + 5 * c);
    return key;
}

void wf_sg_unkey(uint32_t key, struct wf_sg_knobs *k)
{
    k->comp = 0;
    k->push = 0;
    k->fexp = key ? (int)(key >> 1 & 31) - 16 : 0;          /* the offset's sum; wf_sg_neg_pass reads it whole */
    for (int c = 0; c < 3; c++)
        k->prn[c] = key ? (int)(key >> (6 + 5 * c) & 31) - 16 : 0;
    k->iie = key ? (int)(key >> 21 & 15) : WF_SG_IIE_DEF;
}

/* 1D table of WF_SG_N1 entries over 0..65536 (Q8 fraction: a step times 255 stays inside 32 bits) */
static int wf_sg_1d(const uint16_t *t, int x)
{
    if (x <= 0) return t[0];
    if (x >= 65536) return t[WF_SG_N1 - 1];
    int i = x >> 8;
    return t[i] + (((t[i + 1] - t[i]) * (x & 255)) >> 8);
}

/* grid position Q16 (already scaled) for n nodes: cell and Q14 fraction */
static void wf_sg_cell(int64_t g, int n, int *cell, int *fr)
{
    int top = (n - 1) << 16;
    int gi = g < 0 ? 0 : g > top ? top : (int)g;
    int c = gi >> 16;
    if (c > n - 2) c = n - 2;
    *cell = c;
    *fr = (gi - (c << 16)) >> 2;
}

/* tetrahedral interpolation: corner walk from the cell origin along the axes in falling fraction order; the four
 * weights are Q14 and sum to 16384, so a u16 or s16 value times a weight stays inside 32 bits */
struct wf_sg_tet { int i0, i1, i2, i3, w0, w1, w2, w3; };
static void wf_sg_tetw(const int *cell, const int *fr, int n, struct wf_sg_tet *t)
{
    int st[3] = { n * n, n, 1 }, o0 = 0, o1 = 1, o2 = 2, x;
    if (fr[o1] > fr[o0]) { x = o0; o0 = o1; o1 = x; }
    if (fr[o2] > fr[o1]) { x = o1; o1 = o2; o2 = x; }
    if (fr[o1] > fr[o0]) { x = o0; o0 = o1; o1 = x; }
    t->i0 = cell[0] * st[0] + cell[1] * st[1] + cell[2];
    t->i1 = t->i0 + st[o0];
    t->i2 = t->i1 + st[o1];
    t->i3 = t->i2 + st[o2];
    t->w0 = 16384 - fr[o0];
    t->w1 = fr[o0] - fr[o1];
    t->w2 = fr[o1] - fr[o2];
    t->w3 = fr[o2];
}

/* node (yc, uc, vc) of the 17^3 grid -> its R'G'B' code, Q16, clamped (BT.601 full range, as tools/mk_wtab.py) */
static void wf_sg_node(int yc, int uc, int vc, int *rgb)
{
    int Y = yc * 16 > 255 ? 255 : yc * 16, U = (uc * 16 > 255 ? 255 : uc * 16) - 128;
    int V = (vc * 16 > 255 ? 255 : vc * 16) - 128;
    rgb[0] = wf_sg_clamp(Y * 65536 + 91881 * V, 0, 255 << 16);
    rgb[1] = wf_sg_clamp(Y * 65536 - 22554 * U - 46802 * V, 0, 255 << 16);
    rgb[2] = wf_sg_clamp(Y * 65536 + 116130 * U, 0, 255 << 16);
}

/* the negative pass's per-table part */
struct wf_sg_np { int sx, iie, tim[3]; };
static void wf_sg_np_set(const struct wf_sg_pair *pr, const struct wf_sg_knobs *k, struct wf_sg_np *q)
{
    /* exposure axis offset, as the chain adds it: (push + exposure x 0.3) / 6 in Q16 logE, the fitted exposure inside;
     * the knobs' offset (wf_sg_off, thirds of a stop) on top. Exp comp: only the fitted placement is left. A key's
     * knobs (wf_sg_unkey: the whole offset in fexp) give the same table as the knobs it was made from */
    int off = k->comp ? 0 : wf_sg_clamp(k->fexp + k->push, -WF_SG_OFF_MAX, WF_SG_OFF_MAX);
    int32_t loge = (int32_t)(((int64_t)pr->fit * 19661) >> 16) + off * 6554;   /* stops x 0.3; a third = 0.1 logE */
    q->sx = loge >= 0 ? (int)(((int64_t)loge * 10923) >> 16) : -(int)(((int64_t)-loge * 10923) >> 16);   /* / 6 */
    /* colour timing in Q16 printing density: the chain's auto-balance is inside A2's base (measured with the printer at
     * 0), so only the printer points move it, 1 point = 0.025 logE, subtracted as the chain does */
    q->iie = wf_sg_clamp(k->iie, 0, WF_SG_IIE_MAX);
    for (int c = 0; c < 3; c++)
        q->tim[c] = -((wf_sg_clamp(k->prn[c], -WF_SG_PRN_MAX, WF_SG_PRN_MAX) * 104858) >> 6);
}

/* one node of the negative pass: the exposure axis x of each channel (Q16) -> A1 -> A2 -> printing density / 4 */
static inline void wf_sg_nnode(const struct wf_sg_neg *n, const struct wf_sg_np *q, const int *x, uint16_t *pd)
{
    int cell[3], fr[3];
    struct wf_sg_tet t;
    for (int c = 0; c < 3; c++) {
        int d = wf_sg_1d(n->A1[c], x[c] + q->sx);
        wf_sg_cell(((int64_t)(d - n->lo[c]) * n->sc[c]) >> 14, WF_SG_NA, &cell[c], &fr[c]);
    }
    wf_sg_tetw(cell, fr, WF_SG_NA, &t);
    const int16_t *a0 = n->A2[t.i0], *a1 = n->A2[t.i1], *a2 = n->A2[t.i2], *a3 = n->A2[t.i3];
    for (int c = 0; c < 3; c++) {
        int base = (a0[c] * t.w0 + a1[c] * t.w1 + a2[c] * t.w2 + a3[c] * t.w3 + 8192) >> 14;
        int quad = (a0[3 + c] * t.w0 + a1[3 + c] * t.w1 + a2[3 + c] * t.w2 + a3[3 + c] * t.w3 + 8192) >> 14;
        int v = (base * 16 + ((quad * 16 * q->iie) >> 2) + q->tim[c]) >> 2;   /* Q16 printing density / 4: B1's axis */
        pd[c] = (uint16_t)wf_sg_clamp(v, 0, 65535);                         /* 65535 stands for the top end */
    }
}

void wf_sg_neg_pass(const struct wf_sg_neg *n, const struct wf_sg_pair *pr, const struct wf_sg_knobs *k, uint16_t *pd)
{
    struct wf_sg_np q;
    wf_sg_np_set(pr, k, &q);
    for (int node = 0, yc = 0, uc = 0, vc = 0; node < WF_SG_NODES; node++) {   /* node = (yc * 17 + uc) * 17 + vc */
        int rgb[3], x[3];
        wf_sg_node(yc, uc, vc, rgb);
        if (++vc == 17 && (vc = 0, ++uc == 17))
            uc = 0, yc++;
        for (int c = 0; c < 3; c++) {
            int r = rgb[c], i = r >> 14;
            x[c] = i >= WF_SG_NX - 1 ? n->X[WF_SG_NX - 1] : n->X[i] + (((n->X[i + 1] - n->X[i]) * (r & 0x3fff)) >> 14);
        }
        wf_sg_nnode(n, &q, x, pd + node * 3);
    }
}

void wf_sg_neg_pass_x(const struct wf_sg_neg *n, const struct wf_sg_pair *pr, const struct wf_sg_knobs *k,
                      const uint16_t *xn, int nodes, uint16_t *pd)
{
    struct wf_sg_np q;
    wf_sg_np_set(pr, k, &q);
    for (int i = 0; i < nodes; i++, xn += 3, pd += 3) {
        int x[3] = { xn[0], xn[1], xn[2] };
        wf_sg_nnode(n, &q, x, pd);
    }
}

/* one node of the print pass: printing density / 4 -> B1 -> B2 -> the pinned ends, Q16 (black 0, white 65536) */
static inline void wf_sg_pnode(const struct wf_sg_prn *p, const struct wf_sg_pair *pr, const uint16_t *pd, int64_t *lv)
{
    int cell[3], fr[3];
    struct wf_sg_tet t;
    for (int c = 0; c < 3; c++) {
        int x = pd[c];
        int q = wf_sg_1d(p->B1[c], x == 65535 ? 65536 : x);
        wf_sg_cell(((int64_t)(q - p->lo[c]) * p->sc[c]) >> 12, WF_SG_NB, &cell[c], &fr[c]);
    }
    wf_sg_tetw(cell, fr, WF_SG_NB, &t);
    for (int c = 0; c < 3; c++) {
        int raw = (p->B2[t.i0][c] * t.w0 + p->B2[t.i1][c] * t.w1 + p->B2[t.i2][c] * t.w2
                   + p->B2[t.i3][c] * t.w3 + 8192) >> 14;
        int64_t v = ((int64_t)(raw - pr->bk) * pr->inv) >> 16;        /* pinned ends: black 0, white 65536 */
        lv[c] = v < 0 ? 0 : v > 65536 ? 65536 : v;
    }
}

void wf_sg_prn_pass_rgb(const struct wf_sg_prn *p, const struct wf_sg_pair *pr, const uint16_t *pd, int nodes, int bw,
                        uint16_t *rgb)
{
    for (int i = 0; i < nodes; i++, pd += 3, rgb += 3) {
        int64_t lv[3];
        wf_sg_pnode(p, pr, pd, lv);
        if (bw)                                                       /* BT.601 luma in all three, as a B&W .L33 */
            lv[0] = lv[1] = lv[2] = (lv[0] * 19595 + lv[1] * 38470 + lv[2] * 7471 + 32768) >> 16;
        for (int c = 0; c < 3; c++)
            rgb[c] = (uint16_t)((lv[c] * 65535 + 32768) >> 16);       /* value x 65535 */
    }
}

void wf_sg_prn_pass(const struct wf_sg_prn *p, const struct wf_sg_pair *pr, const uint16_t *pd, int bw, uint32_t *T)
{
    for (int node = 0; node < WF_SG_NODES; node++) {
        int64_t o[3];
        wf_sg_pnode(p, pr, pd + node * 3, o);
        for (int c = 0; c < 3; c++)
            o[c] *= 255;                                              /* Q16 code */
        int oy = (int)((o[0] * 19595 + o[1] * 38470 + o[2] * 7471 + ((int64_t)1 << 31)) >> 32);
        int ou = (int)((o[0] * -11058 + o[1] * -21710 + o[2] * 32768 + ((int64_t)1 << 31)) >> 32);
        int ov = (int)((o[0] * 32768 + o[1] * -27439 + o[2] * -5329 + ((int64_t)1 << 31)) >> 32);
        oy = wf_sg_clamp(oy, 0, 255);
        ou = bw ? 0 : wf_sg_clamp(ou, -128, 127);
        ov = bw ? 0 : wf_sg_clamp(ov, -128, 127);
        T[node] = (uint32_t)(ou & 255) | (uint32_t)oy << 8 | (uint32_t)(ov & 255) << 16;
    }
}

void wf_sg_compose(const struct wf_sg_neg *n, const struct wf_sg_prn *p, const struct wf_sg_pair *pr,
                   const struct wf_sg_knobs *k, int bw, uint16_t *pd, uint32_t *T)
{
    wf_sg_neg_pass(n, pr, k, pd);
    wf_sg_prn_pass(p, pr, pd, bw, T);
}

int wf_sg_res_quant(const uint8_t *le, const uint16_t *rgb, int n, int8_t *r)
{
    int clip = 0;
    for (int i = 0; i < 3 * n; i++) {
        int d = (int)(le[2 * i] | le[2 * i + 1] << 8) - rgb[i];
        d = d >= 0 ? (d + 32) >> 6 : -((32 - d) >> 6);   /* quarter 8-bit codes, half away from 0 */
        if (d > 127 || d < -127) {
            d = d > 0 ? 127 : -127;
            clip++;
        }
        r[i] = (int8_t)d;
    }
    return clip;
}

static inline int wf_sg_lerp(int a, int b, int f) { return a + (((b - a) * f + 128) >> WF_SG_SHIFT_Q); }

void wf_sg_res_add(const int8_t *r, const uint16_t *pos, int node0, int n, uint16_t *rgb)
{
    const int G = WF_SG_LG, dr = 3, dg = 3 * WF_SG_LG, db = 3 * WF_SG_LG * WF_SG_LG;
    unsigned u = (unsigned)node0;
    int kr = (int)(u % WF_SG_LG), kg = (int)(u / WF_SG_LG % WF_SG_LG), kb = (int)(u / (WF_SG_LG * WF_SG_LG));
    for (int i = 0; i < n; i++, rgb += 3) {
        int v[3];
        if (!pos) {
            const int8_t *q = r + 3 * (node0 + i);
            for (int c = 0; c < 3; c++)
                v[c] = q[c] * 64;
        } else {
            int k[3] = { kr, kg, kb }, i0[3], f[3];
            for (int a = 0; a < 3; a++) {
                int p = pos[k[a]];
                i0[a] = p >> WF_SG_SHIFT_Q > G - 2 ? G - 2 : p >> WF_SG_SHIFT_Q;
                f[a] = p - (i0[a] << WF_SG_SHIFT_Q);
            }
            const int8_t *q = r + 3 * (i0[0] + G * (i0[1] + G * i0[2]));
            for (int c = 0; c < 3; c++, q++) {
                int x00 = wf_sg_lerp(q[0] * 64, q[dr] * 64, f[0]);
                int x10 = wf_sg_lerp(q[dg] * 64, q[dg + dr] * 64, f[0]);
                int x01 = wf_sg_lerp(q[db] * 64, q[db + dr] * 64, f[0]);
                int x11 = wf_sg_lerp(q[db + dg] * 64, q[db + dg + dr] * 64, f[0]);
                v[c] = wf_sg_lerp(wf_sg_lerp(x00, x10, f[1]), wf_sg_lerp(x01, x11, f[1]), f[2]);
            }
        }
        for (int c = 0; c < 3; c++)
            rgb[c] = (uint16_t)wf_sg_clamp(rgb[c] + v[c], 0, 65535);
        if (++kr == G) {
            kr = 0;
            if (++kg == G) {
                kg = 0;
                kb++;
            }
        }
    }
}
