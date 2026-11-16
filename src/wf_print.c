/* wf_print.c - Darkroom print finishes (wf_print.h, docs/DESIGN.md 3.9), WindyStrife's own code, written from scratch
 * for WindyFilm. Pure: no libc, no allocation, no file access, integer only (the host test runs these objects). */
#include "wf_print.h"

static inline unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline int rd16s(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* a size given at a 3456 px short side, for short side s: rounded to 16 (pf_sc16) or to 1 px (pf_sc) */
static int pf_sc(int v, int s) { return (v * s + 1728) / 3456; }
static int pf_sc16(int v, int s)
{
    int r = (v * s + 3456 * 8) / (3456 * 16) * 16;
    return r < 16 ? 16 : r;
}

/* ================================================================== layout */
int wf_pf_layout(struct wf_pf_lay *L, int frame, int info, int iw, int ih, int orient)
{
    uint8_t *z = (uint8_t *)L;
    for (unsigned i = 0; i < sizeof *L; i++)
        z[i] = 0;
    if (orient != 3 && orient != 6 && orient != 8)
        orient = 1;
    int rot = orient == 6 || orient == 8, s = iw < ih ? iw : ih;
    L->orient = orient;
    L->iw = L->cw = L->dw = iw;
    L->ih = L->ch = L->dh = ih;
    if (rot) {
        L->dw = ih;
        L->dh = iw;
    }
    L->info = info > 0 && info < WF_PIN_N ? info : 0;
    if (frame <= WF_PFR_OFF || frame >= WF_PFR_N || iw < 64 || ih < 64)
        return 0;
    L->frame = frame;
    int black = frame == WF_PFR_BLACK || frame == WF_PFR_BLACK_W, wide = frame >= WF_PFR_WHITE_W;
    L->bg = black ? 12 : 255;
    L->fg1 = black ? 236 : 30;
    L->fg2 = black ? 150 : 118;
    int bd = pf_sc16(wide ? WF_PF_BD_WIDE : WF_PF_BD_THIN, s);
    int bb = L->info ? bd + pf_sc16(WF_PF_CAP, s) : bd;
    int dw0 = L->dw, dh0 = L->dh;
    L->bd = bd;
    L->bb = bb;
    L->dw = dw0 + 2 * bd;
    L->dh = dh0 + bd + bb;
    L->cw = rot ? L->dh : L->dw;
    L->ch = rot ? L->dw : L->dh;
    /* the shown picture at (bd, bd), the caption under it (bd, bd + dh0, dw0 x bb), mapped into the stored canvas */
    switch (orient) {
    case 1: L->ix = bd; L->iy = bd; L->tx = bd;      L->ty = bd + ih; L->tw = iw; L->th = bb; break;
    case 3: L->ix = bd; L->iy = bb; L->tx = bd;      L->ty = 0;       L->tw = iw; L->th = bb; break;
    case 6: L->ix = bd; L->iy = bd; L->tx = bd + iw; L->ty = bd;      L->tw = bb; L->th = ih; break;
    default: L->ix = bb; L->iy = bd; L->tx = 0;      L->ty = bd;      L->tw = bb; L->th = ih; break;   /* 8 */
    }
    if (!L->info)
        L->tx = L->ty = L->tw = L->th = 0;
    return 1;
}

/* display (u, v) of the canvas -> index in a buffer covering the stored rect (rx, ry, rw x rh): idx = base + u du + v dv */
struct pf_map { int base, du, dv; };
static void pf_map(const struct wf_pf_lay *L, int cw, int ch, int rx, int ry, int rw, struct pf_map *m)
{
    switch (L->orient) {
    case 3: m->du = -1;  m->dv = -rw; m->base = (ch - 1 - ry) * rw + (cw - 1 - rx); break;   /* x = cw-1-u, y = ch-1-v */
    case 6: m->du = -rw; m->dv = 1;   m->base = (ch - 1 - ry) * rw - rx; break;              /* x = v, y = ch-1-u */
    case 8: m->du = rw;  m->dv = -1;  m->base = -ry * rw + (cw - 1 - rx); break;             /* x = cw-1-v, y = u */
    default: m->du = 1;  m->dv = rw;  m->base = -ry * rw - rx; break;                        /* x = u, y = v */
    }
}

/* ================================================================== font pack */
static uint32_t pf_fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--)
        h = (h ^ *p++) * 0x01000193u;
    return h;
}

int wf_pf_fonts(struct wf_pf_font f[WF_PF_NFONT], const uint8_t *b, uint32_t n)
{
    if (!b || n < 16 + WF_PF_NFONT * 24 || b[0] != 'W' || b[1] != 'F' || b[2] != 'P' || b[3] != 'F' || rd16(b + 4) != 1
        || rd16(b + 6) < WF_PF_NFONT || rd32(b + 8) != n || pf_fnv(b + 16, n - 16) != rd32(b + 12))
        return -1;
    for (int i = 0; i < WF_PF_NFONT; i++) {
        const uint8_t *d = b + 16 + i * 24;
        struct wf_pf_font *F = &f[i];
        uint32_t go = rd32(d + 16), ko = rd32(d + 20);
        F->b = b;
        F->px = rd16(d);
        F->asc = rd16(d + 8);
        F->desc = rd16(d + 10);
        F->nk = rd16(d + 12);
        if (d[5] != WF_PF_FIRST || d[6] != WF_PF_COUNT || go < 16 || go + WF_PF_COUNT * 16 > n || ko + F->nk * 4u > n
            || F->asc < 1 || F->asc > 400)
            return -1;
        F->g = b + go;
        F->k = b + ko;
        for (int c = 0; c < WF_PF_COUNT; c++) {
            const uint8_t *g = F->g + c * 16;
            uint32_t w = rd16(g + 6), h = rd16(g + 8), bpr = rd16(g + 10), bo = rd32(g + 12);
            if (w > 1000 || h > 1000 || bpr * 2 < w || (w && h && (bo < 16 || bo + bpr * h > n)))
                return -1;
        }
        int j = 0;
        for (int c = 0; c <= WF_PF_COUNT; c++) {
            while (j < F->nk && F->k[j * 4] < WF_PF_FIRST + c)
                j++;
            F->kidx[c] = (uint16_t)j;
        }
    }
    return 0;
}

static int pf_kern(const struct wf_pf_font *f, int a, int b)
{
    for (int i = f->kidx[a - WF_PF_FIRST], e = f->kidx[a - WF_PF_FIRST + 1]; i < e; i++) {
        int r = f->k[i * 4 + 1];
        if (r == b)
            return rd16s(f->k + i * 4 + 2);
        if (r > b)
            break;
    }
    return 0;
}

/* the glyph's 16-byte record of byte o (outside the set: '?') */
static int pf_ch(int o) { return o >= WF_PF_FIRST && o < WF_PF_FIRST + WF_PF_COUNT ? o : '?'; }

struct pf_dst {                                  /* where a string is drawn: the band buffer through the orientation map */
    uint8_t *buf;
    struct pf_map m;
    int u0, u1, v0, v1;                          /* the shown rect the text may touch */
};

/* 16 coverage steps -> 0..256 */
static const uint16_t pf_a16[16] = { 0, 17, 34, 51, 68, 85, 102, 119, 137, 154, 171, 188, 205, 222, 239, 256 };

/* width of s in px; drawn at pen u (left), baseline v when d */
static int pf_run(const struct wf_pf_font *f, const char *s, int u, int v, const struct pf_dst *d, int fg)
{
    int pen = 0, prev = -1;
    for (; *s; s++) {
        int o = pf_ch((unsigned char)*s);
        if (prev >= 0)
            pen += pf_kern(f, prev, o);
        const uint8_t *g = f->g + (o - WF_PF_FIRST) * 16;
        int w = rd16(g + 6), h = rd16(g + 8), bpr = rd16(g + 10);
        if (d && w && h) {
            int gu = u + ((pen + 32) >> 6) + rd16s(g + 2), gv = v - f->asc + rd16s(g + 4);
            const uint8_t *bits = f->b + rd32(g + 12);
            for (int j = 0; j < h; j++) {
                int vv = gv + j;
                if (vv < d->v0 || vv >= d->v1)
                    continue;
                const uint8_t *br = bits + j * bpr;
                for (int i = 0; i < w; i++) {
                    int c = (br[i >> 1] >> (i & 1 ? 0 : 4)) & 15, uu = gu + i;
                    if (!c || uu < d->u0 || uu >= d->u1)
                        continue;
                    uint8_t *p = d->buf + d->m.base + uu * d->m.du + vv * d->m.dv;
                    int a = pf_a16[c];
                    *p = (uint8_t)(*p + (((fg - *p) * a + 128) >> 8));
                }
            }
        }
        pen += rd16(g);
        prev = o;
    }
    return (pen + 32) >> 6;
}

int wf_pf_textw(const struct wf_pf_font *f, const char *s) { return pf_run(f, s, 0, 0, 0, 0); }

/* ================================================================== caption */
/* append s to b (n bytes, *k used) */
static void pf_cat(char *b, int n, int *k, const char *s)
{
    while (*s && *k < n - 1)
        b[(*k)++] = *s++;
    b[*k] = 0;
}

/* items joined with sep while the whole fits maxw: the number of items used; the first one is cut to fit when alone */
static int pf_fit(const struct wf_pf_font *f, const char *const *it, int ni, const char *sep, int maxw, char *b, int n)
{
    int k = 0, used = 0;
    b[0] = 0;
    for (int i = 0; i < ni; i++) {
        if (!it[i] || !it[i][0])
            continue;
        int k0 = k;
        if (used)
            pf_cat(b, n, &k, sep);
        pf_cat(b, n, &k, it[i]);
        if (wf_pf_textw(f, b) <= maxw) {
            used++;
            continue;
        }
        k = k0;
        b[k] = 0;
        if (!used) {                             /* the first item alone is too long: cut it */
            const char *s = it[i];
            while (*s && k < n - 1) {
                b[k++] = *s++;
                b[k] = 0;
                if (wf_pf_textw(f, b) > maxw) {
                    b[--k] = 0;
                    break;
                }
            }
            used = k > 0;
        }
        break;
    }
    return used;
}

uint32_t wf_pf_band_bytes(const struct wf_pf_lay *L) { return (uint32_t)L->tw * (uint32_t)L->th; }

/* the height of a glyph above the baseline (cap: 'H') and below it (desc: 'g') */
static int pf_above(const struct wf_pf_font *f, int o) { return f->asc - rd16s(f->g + (o - WF_PF_FIRST) * 16 + 4); }
static int pf_below(const struct wf_pf_font *f, int o)
{
    const uint8_t *g = f->g + (o - WF_PF_FIRST) * 16;
    return rd16s(g + 4) + (int)rd16(g + 8) - f->asc;
}

/* the caption's three baselines in the shown canvas, and line 1's / lines 2-3's cap heights */
struct pf_ln { int b1, b2, b3, cap1, cap2; };
static void pf_lines(const struct wf_pf_lay *L, const struct wf_pf_font *F, struct pf_ln *n)
{
    int s = L->iw < L->ih ? L->iw : L->ih, dh0 = L->dh - L->bd - L->bb;
    int g12 = pf_sc(110, s), g23 = pf_sc(100, s);
    int cap = pf_above(&F[0], 'H'), desc = pf_below(&F[1], 'g');
    int blk = cap + g12 + g23 + desc, pad = (L->bb - blk) / 2;
    n->b1 = L->bd + dh0 + (pad > 0 ? pad : 0) + cap;
    n->b2 = n->b1 + g12;
    n->b3 = n->b2 + g23;
    n->cap1 = cap;
    n->cap2 = pf_above(&F[1], 'H');
}

void wf_pf_band(const struct wf_pf_lay *L, const struct wf_pf_font *F, const struct wf_pf_cap *c, uint8_t *band,
                char *drawn, int dn)
{
    uint32_t nb = wf_pf_band_bytes(L);
    uint32_t fill = 0x01010101u * L->bg;
    if (drawn && dn > 0)
        drawn[0] = 0;
    if (!nb)
        return;
    uint32_t i = 0;
    for (; i < nb && ((uintptr_t)(band + i) & 3); i++)
        band[i] = L->bg;
    for (; i + 4 <= nb; i += 4)
        *(uint32_t *)(band + i) = fill;
    for (; i < nb; i++)
        band[i] = L->bg;
    if (!F || !c)
        return;
    int s = L->iw < L->ih ? L->iw : L->ih;
    int dw0 = L->dw - 2 * L->bd, dh0 = L->dh - L->bd - L->bb;
    struct pf_dst d;
    d.buf = band;
    pf_map(L, L->cw, L->ch, L->tx, L->ty, L->tw, &d.m);
    d.u0 = L->bd;
    d.u1 = L->bd + dw0;
    d.v0 = L->bd + dh0;
    d.v1 = L->dh;
    struct pf_ln ln;
    pf_lines(L, F, &ln);
    int b1 = ln.b1, b2 = ln.b2, b3 = ln.b3, rsv = c->rsv > 0 && c->rsv < dw0 ? c->rsv : 0;
    char t1[160], t2[96], t3[256], tr[48];
    tr[0] = 0;
    int rw = 0;
    if (c->roll && c->roll[0]) {
        const char *one[1] = { c->roll };
        pf_fit(&F[1], one, 1, "", dw0 / 2, tr, sizeof tr);
        rw = wf_pf_textw(&F[1], tr);
    }
    int gap = pf_sc(64, s), r1 = rw && !c->roll3 ? rw + gap : 0, r3 = rw && c->roll3 ? rw + gap : 0;
    pf_fit(&F[0], c->l1, c->n1, "  /  ", dw0 - r1 - rsv, t1, sizeof t1);
    const char *two[1] = { c->l2 };
    pf_fit(&F[1], two, 1, "", dw0 - rsv, t2, sizeof t2);
    pf_fit(&F[1], c->l3, c->n3, "  " WF_PF_DOT "  ", dw0 - r3, t3, sizeof t3);
    pf_run(&F[0], t1, d.u0, b1, &d, L->fg1);
    if (rw)
        pf_run(&F[1], tr, d.u1 - rw, c->roll3 ? b3 : b1, &d, L->fg2);
    pf_run(&F[1], t2, d.u0, b2, &d, L->fg2);
    pf_run(&F[1], t3, d.u0, b3, &d, L->fg2);
    if (drawn && dn > 0) {
        int k = 0;
        pf_cat(drawn, dn, &k, t1);
        if (tr[0]) { pf_cat(drawn, dn, &k, " + "); pf_cat(drawn, dn, &k, tr); }
        pf_cat(drawn, dn, &k, " | ");
        pf_cat(drawn, dn, &k, t2);
        pf_cat(drawn, dn, &k, " | ");
        pf_cat(drawn, dn, &k, t3);
        for (int j = 0; j < k; j++)
            if (drawn[j] == 0x7f)
                drawn[j] = '.';                  /* the log has no middle dot */
    }
}

/* ================================================================== rows */
#define DIV255(v) (((v) + 1 + ((v) >> 8)) >> 8)  /* v / 255, exact for 0..65535 */

/* ink k over canvas row y (o: its UYVY words, cw px): Y per pixel, U / V per pair by the pair's mean coverage */
static void pf_ink_row(const struct wf_pf_ink *k, int y, uint32_t *o, int cw)
{
    if (!k->map || y < k->y || y >= k->y + k->h)
        return;
    const uint8_t *m = k->map + (y - k->y) * k->w;
    int x0 = k->x & ~1, x1 = k->x + k->w;
    if (x0 < 0) x0 = 0;
    if (x1 > cw) x1 = cw;
    for (int x = x0; x < x1; x += 2) {
        int a0 = x >= k->x ? m[x - k->x] : 0, a1 = x + 1 < k->x + k->w && x + 1 >= k->x ? m[x + 1 - k->x] : 0;
        if (!(a0 | a1))
            continue;
        uint32_t w = o[x >> 1];
        int U = (int8_t)(w & 0xFF), Y0 = (int)(w >> 8) & 0xFF, V = (int8_t)((w >> 16) & 0xFF), Y1 = (int)(w >> 24);
        int ac = (a0 + a1 + 1) >> 1;
        Y0 += Y0 < k->Y ? (int)DIV255((k->Y - Y0) * a0) : -(int)DIV255((Y0 - k->Y) * a0);
        Y1 += Y1 < k->Y ? (int)DIV255((k->Y - Y1) * a1) : -(int)DIV255((Y1 - k->Y) * a1);
        U += U < k->U ? (int)DIV255((k->U - U) * ac) : -(int)DIV255((U - k->U) * ac);
        V += V < k->V ? (int)DIV255((k->V - V) * ac) : -(int)DIV255((V - k->V) * ac);
        o[x >> 1] = (uint32_t)(U & 0xFF) | (uint32_t)Y0 << 8 | (uint32_t)(V & 0xFF) << 16 | (uint32_t)Y1 << 24;
    }
}

/* a picture in one block: row y = img + y * ipitch */
struct pf_flat { uint8_t *img; int pitch; };
static uint32_t *pf_flat_row(void *ctx, int y)
{
    const struct pf_flat *f = ctx;
    return (uint32_t *)(f->img + y * f->pitch);
}

void wf_pf_rows(const struct wf_pf_lay *L, int cy, int n, const uint32_t *img, int ipitch, const uint8_t *band,
                uint32_t *out)
{
    struct pf_flat fl = { (uint8_t *)img, ipitch };
    wf_pf_rows_fn(L, cy, n, pf_flat_row, &fl, band, out);
}

void wf_pf_rows_fn(const struct wf_pf_lay *L, int cy, int n, wf_pf_rowfn *row, void *ctx, const uint8_t *band,
                   uint32_t *out)
{
    int nw = L->cw >> 1, ix = L->ix >> 1, iw = L->iw >> 1, tx = L->tx >> 1, tw = L->tw >> 1;
    uint32_t bgw = (uint32_t)L->bg << 8 | (uint32_t)L->bg << 24;   /* U = V = 0: neutral */
    for (int r = 0; r < n; r++) {
        int y = cy + r;
        uint32_t *o = out + r * nw;
        int pic = y >= L->iy && y < L->iy + L->ih;
        if (pic) {
            const uint32_t *s = row(ctx, y - L->iy);
            for (int x = 0; x < ix; x++)
                o[x] = bgw;
            for (int x = 0; x < iw; x++)
                o[ix + x] = s[x];
            for (int x = ix + iw; x < nw; x++)
                o[x] = bgw;
        } else {
            for (int x = 0; x < nw; x++)
                o[x] = bgw;
        }
        if (band && tw && y >= L->ty && y < L->ty + L->th) {
            const uint8_t *b = band + (y - L->ty) * L->tw;
            for (int x = 0; x < tw; x++)
                o[tx + x] = (uint32_t)b[2 * x] << 8 | (uint32_t)b[2 * x + 1] << 24;
        }
        for (int k = 0; k < L->nink; k++)
            pf_ink_row(&L->ink[k], y, o, L->cw);
    }
}

int wf_pf_band_rows(const struct wf_pf_lay *L, int cy, const uint32_t *img, int ipitch, const uint8_t *band,
                    uint32_t *rowbuf, const uint32_t **rows, int *pitch)
{
    struct pf_flat fl = { (uint8_t *)img, ipitch };
    return wf_pf_band_rows_fn(L, cy, pf_flat_row, &fl, ipitch, band, rowbuf, rows, pitch);
}

int wf_pf_band_rows_fn(const struct wf_pf_lay *L, int cy, wf_pf_rowfn *row, void *ctx, int ipitch, const uint8_t *band,
                       uint32_t *rowbuf, const uint32_t **rows, int *pitch)
{
    int n = L->ch - cy < 16 ? L->ch - cy : 16;
    if (!L->frame) {
        const uint32_t *r0 = row(ctx, cy);
        if ((const uint8_t *)row(ctx, cy + n - 1) == (const uint8_t *)r0 + (n - 1) * ipitch) {
            *rows = r0;                          /* one run: the picture's own rows, no copy (today's bytes) */
            *pitch = ipitch;
            return n;
        }
        if (!rowbuf)
            return -1;
        int nw = L->cw >> 1;                     /* across two pieces: the rows copied */
        for (int r = 0; r < n; r++) {
            const uint32_t *s = row(ctx, cy + r);
            uint32_t *o = rowbuf + r * nw;
            for (int x = 0; x < nw; x++)
                o[x] = s[x];
        }
        *rows = rowbuf;
        *pitch = L->cw * 2;
        return n;
    }
    wf_pf_rows_fn(L, cy, n, row, ctx, band, rowbuf);
    *rows = rowbuf;
    *pitch = L->cw * 2;
    return n;
}

/* ================================================================== text from the shot */
static void pf_num(char *b, int n, int *k, uint32_t v)
{
    char t[12];
    int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 11);
    while (i && *k < n - 1)
        b[(*k)++] = t[--i];
    b[*k] = 0;
}
/* v10 = tenths: "5.6", "8", "11" */
static void pf_dec(char *b, int n, int *k, uint32_t v10)
{
    pf_num(b, n, k, v10 / 10);
    if (v10 % 10) {
        pf_cat(b, n, k, ".");
        pf_num(b, n, k, v10 % 10);
    }
}

int wf_pf_line1(const struct wf_dk_shot *s, char *model, int mn, char *lens, int ln)
{
    int k = 0, m = 0;
    model[0] = lens[0] = 0;
    if (s->model[0]) { pf_cat(model, mn, &k, s->model); m++; }
    k = 0;
    if (s->lens[0]) { pf_cat(lens, ln, &k, s->lens); m++; }
    return m;
}

int wf_pf_line2(const struct wf_dk_shot *s, char *b, int n)
{
    int k = 0, m = 0;
    static const char sp[] = "   ";
    b[0] = 0;
    if (s->fl[1] && s->fl[0] < 100000000u) {
        uint32_t v10 = (s->fl[0] * 10 + s->fl[1] / 2) / s->fl[1];
        pf_dec(b, n, &k, v10 >= 100 ? (v10 + 5) / 10 * 10 : v10);   /* 45mm; 4.5mm under 10 */
        pf_cat(b, n, &k, "mm");
        m++;
    }
    if (s->fn[1] && s->fn[0] < 100000000u) {
        uint32_t v10 = (s->fn[0] * 10 + s->fn[1] / 2) / s->fn[1];
        if (m) pf_cat(b, n, &k, sp);
        pf_cat(b, n, &k, "f/");
        pf_dec(b, n, &k, v10 >= 100 ? (v10 + 5) / 10 * 10 : v10);   /* f/5.6, f/11 */
        m++;
    }
    if (s->et[1] && s->et[0] && s->et[0] < 100000000u && s->et[1] < 100000000u) {
        uint32_t a = s->et[0], d = s->et[1];
        if (m) pf_cat(b, n, &k, sp);
        if (a * 10 >= d * 3) {                   /* 0.3 s and longer: "0.3s", "1s", "2.5s", "30s" */
            uint32_t v10 = (a * 10 + d / 2) / d;
            pf_dec(b, n, &k, v10 >= 100 ? (v10 + 5) / 10 * 10 : v10);
        } else {
            pf_cat(b, n, &k, "1/");
            pf_num(b, n, &k, a == 1 ? d : (d + a / 2) / a);
        }
        pf_cat(b, n, &k, "s");
        m++;
    }
    if (s->iso > 0) {
        if (m) pf_cat(b, n, &k, sp);
        pf_cat(b, n, &k, "ISO ");
        pf_num(b, n, &k, (uint32_t)s->iso);
        m++;
    }
    return m;
}

static int pf_pre(const char *s, const char *p)
{
    while (*p && *s == *p) { s++; p++; }
    return !*p;
}

int wf_pf_look_items(char *txt, const char **it, int max)
{
    static const char *const keep[] = { "Print ", "Grain ", "Halation ", "Bloom ", "Mist ", "Black Mist " };
    char *p = txt;
    int n = 0;
    if (pf_pre(p, "WindyFilm: "))
        p += 11;
    while (*p && n < max) {
        char *e = p;
        while (*e && !(e[0] == ' ' && e[1] == '|' && e[2] == ' '))
            e++;
        char *nx = *e ? e + 3 : e;
        *e = 0;
        int ok = n == 0;
        for (int k = 0; !ok && k < (int)(sizeof keep / sizeof keep[0]); k++)
            ok = pf_pre(p, keep[k]);
        if (ok && n && pf_pre(p, "Print ")) {   /* "Portra 400 Cine": the title names the print already */
            int lt = 0, lp = 0;
            while (it[0][lt]) lt++;
            while (p[6 + lp]) lp++;
            ok = !(lt > lp && it[0][lt - lp - 1] == ' ' && pf_pre(it[0] + lt - lp, p + 6));
        }
        if (ok && *p)
            it[n++] = p;
        p = nx;
    }
    return n;
}

void wf_pf_caption(const struct wf_dk_shot *s, const char *look, const char *roll, struct wf_pf_txt *t,
                   struct wf_pf_cap *c)
{
    uint8_t *z = (uint8_t *)c;
    for (unsigned i = 0; i < sizeof *c; i++)
        z[i] = 0;
    wf_pf_line1(s, t->model, sizeof t->model, t->lens, sizeof t->lens);
    if (t->model[0]) c->l1[c->n1++] = t->model;
    if (t->lens[0]) c->l1[c->n1++] = t->lens;
    wf_pf_line2(s, t->l2, sizeof t->l2);
    c->l2 = t->l2;
    int k = 0;
    t->l3[0] = 0;
    if (look)
        pf_cat(t->l3, sizeof t->l3, &k, look);
    c->n3 = wf_pf_look_items(t->l3, c->l3, (int)(sizeof c->l3 / sizeof c->l3[0]));
    k = 0;
    t->roll[0] = 0;
    if (roll && roll[0]) {
        pf_cat(t->roll, sizeof t->roll, &k, roll);
        c->roll = t->roll;
    }
}

static int pf_d2(const char *p) { return p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9'; }

int wf_pf_date(const struct wf_dk_shot *s, int fmt, char *b, int n)
{
    const char *t = s->dt;
    b[0] = 0;
    if (fmt <= WF_PDT_OFF || fmt >= WF_PDT_N || n < 10 || !pf_d2(t) || !pf_d2(t + 2) || t[4] != ':' || !pf_d2(t + 5)
        || t[7] != ':' || !pf_d2(t + 8))
        return 0;
    int mo = (t[5] - '0') * 10 + t[6] - '0', dd = (t[8] - '0') * 10 + t[9] - '0';
    if (mo < 1 || mo > 12 || dd < 1 || dd > 31)
        return 0;
    char y[4] = { '\'', t[2], t[3], 0 }, m[3] = { t[5], t[6], 0 }, d[3] = { t[8], t[9], 0 };
    const char *a = fmt == WF_PDT_YMD ? y : fmt == WF_PDT_DMY ? d : m;
    const char *bb = fmt == WF_PDT_YMD ? m : fmt == WF_PDT_DMY ? m : d;
    const char *c = fmt == WF_PDT_YMD ? d : y;
    int k = 0;
    pf_cat(b, n, &k, a);
    pf_cat(b, n, &k, " ");
    pf_cat(b, n, &k, bb);
    pf_cat(b, n, &k, " ");
    pf_cat(b, n, &k, c);
    return k;
}

/* ================================================================== date stamp
 * Seven-segment digits as on a film camera's date back, drawn from their geometry: per pixel the distance to the lit
 * segments (hexagons with 45-degree tips, slanted 8 degrees), a soft core and a glow round it, screened over the picture
 * in an orange that turns red at the rim. Sizes in hundredths of the digit height H (3.6 % of the short side). */
#define ST_H      36                             /* digit height, per mille of the short side */
#define ST_M      70                             /* margins right / bottom, per mille of the short side */
#define ST_CELL   56                             /* digit cell width, % of H */
#define ST_ADV    72                             /* digit advance */
#define ST_QADV   30                             /* the apostrophe's advance */
#define ST_SADV   36                             /* a space */
#define ST_R      65                             /* segment half thickness, per mille of H */
#define ST_GAP    18                             /* gap at the tips, per mille of H */
#define ST_SLANT  14                             /* x shift per unit height, % */
#define ST_GLOW   30                             /* glow reach, % of H */
#define ST_GMAX   112                            /* glow strength at the edge (of 255) */

static const uint8_t st_seg[10] = {              /* bits a b c d e f g = 1 2 4 8 16 32 64 */
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static int st_adv(int ch, int H)
{
    return ch == ' ' ? H * ST_SADV / 100 : ch == '\'' ? H * ST_QADV / 100 : H * ST_ADV / 100;
}
/* the text's width (no trailing space), px; -1 = a character it cannot draw */
static int st_width(const char *t, int H)
{
    int w = 0, last = 0;
    for (; *t; t++) {
        if (*t != ' ' && *t != '\'' && (*t < '0' || *t > '9'))
            return -1;
        last = *t == '\'' ? H * 18 / 100 : *t == ' ' ? 0 : H * ST_CELL / 100;
        w += st_adv(*t, H);
        if (!t[1])
            w += last - st_adv(*t, H);
    }
    return w;
}

static void st_geom(int iw, int ih, int *H, int *pad)
{
    int s = iw < ih ? iw : ih;
    *H = s * ST_H / 1000;
    *pad = *H * ST_GLOW / 100 + 2;
}

uint32_t wf_pf_stamp_bytes(int iw, int ih, const char *txt)
{
    int H, pad, w;
    st_geom(iw, ih, &H, &pad);
    if (H < 8 || (w = st_width(txt, H)) <= 0)
        return 0;
    return (uint32_t)(w + H * ST_SLANT / 100 + 2 * pad) * (uint32_t)(H + 2 * pad);
}

/* one segment: centre (cx, cy), half length hl, half thickness r, vertical or not; distance in 1/16 px (inside < 0) */
static inline int st_dist(int x, int y, int cx, int cy, int hl, int r, int vert)
{
    int s = vert ? y - cy : x - cx, t = vert ? x - cx : y - cy;
    if (s < 0) s = -s;
    if (t < 0) t = -t;
    int a = t - r, b = ((s + t - hl) * 181) >> 8;     /* the sides; the 45-degree tips (1 / sqrt 2) */
    return a > b ? a : b;
}

/* the segments of one digit cell, 1/16 px from the cell's top left (worked out once per stamp: no division per pixel,
   the ARM946 has no divide instruction) */
struct st_geo {
    int sx[7], sy[7], hl[7];                     /* centre and half length of a b c d e f g */
    int r, tx, ty, thl, tr;                      /* half thickness; the apostrophe's tick */
};
static void st_geo_init(struct st_geo *G, int H16)
{
    int r = H16 * ST_R / 1000, g = H16 * ST_GAP / 1000, cw = H16 * ST_CELL / 100;
    int xl = r, xr = cw - r, yt = r, ym = H16 / 2, yb = H16 - r;
    int hh = (xr - xl) / 2 - g, hv = (ym - yt) / 2 - g, cx = (xl + xr) / 2;
    static const int8_t px[7] = { 0, 1, 1, 0, -1, -1, 0 };   /* x: centre / right / left */
    for (int k = 0; k < 7; k++) {
        G->sx[k] = px[k] > 0 ? xr : px[k] < 0 ? xl : cx;
        G->hl[k] = px[k] ? hv : hh;
    }
    G->sy[0] = yt; G->sy[6] = ym; G->sy[3] = yb;
    G->sy[1] = G->sy[5] = (yt + ym) / 2;
    G->sy[2] = G->sy[4] = (ym + yb) / 2;
    G->r = r;
    G->tx = H16 * 9 / 100;
    G->ty = H16 * 16 / 100;
    G->thl = H16 * 13 / 100;
    G->tr = r * 85 / 100;
}

/* the distance (1/16 px) from glyph point (x, y) (unslanted, cell origin, 1/16 px) to the lit segments of ch */
static int st_glyph(const struct st_geo *G, int ch, int x, int y)
{
    if (ch == '\'')                              /* a short tick at the top */
        return st_dist(x, y, G->tx, G->ty, G->thl, G->tr, 1);
    int m = st_seg[ch - '0'], best = 1 << 30;
    for (int k = 0; k < 7; k++, m >>= 1) {
        if (!(m & 1))
            continue;
        int d = st_dist(x, y, G->sx[k], G->sy[k], G->hl[k], G->r, k != 0 && k != 3 && k != 6);
        if (d < best)
            best = d;
    }
    return best;
}

/* the map: per pixel 0..255 = how much of the stamp's light lands there */
static void st_map(const char *txt, int H, int pad, uint8_t *map, int mw, int mh)
{
    int H16 = H * 16, R16 = H16 * ST_GLOW / 100, cw16 = H16 * ST_CELL / 100;
    int x0[16], n = 0, pen = 0;
    char ch[16];
    struct st_geo G;
    st_geo_init(&G, H16);
    for (const char *t = txt; *t && n < 16; t++) {
        if (*t != ' ') {
            x0[n] = pen;
            ch[n++] = *t;
        }
        pen += st_adv(*t, H) * 16;
    }
    uint8_t gl[160];                             /* the glow by whole px of distance (R <= 159 px) */
    int R = R16 >> 4;
    if (R > 159) R = 159;
    for (int i = 0; i <= R; i++) {
        int q = R ? ((R - i) << 8) / R : 0;
        gl[i] = (uint8_t)((ST_GMAX * q * q) >> 16);
    }
    for (int my = 0; my < mh; my++) {
        int y = (my - pad) * 16 + 8;             /* glyph y (pixel centre), 1/16 px from the cells' top */
        int sh = ((H16 - y) * ST_SLANT) / 100;   /* the slant: the top leans right */
        uint8_t *row = map + my * mw;
        for (int mx = 0; mx < mw; mx++) {
            int x = (mx - pad) * 16 + 8 - sh, d = 1 << 30;
            for (int i = 0; i < n; i++) {
                int lx = x - x0[i];
                if (lx < -R16 || lx > cw16 + R16)
                    continue;
                int di = st_glyph(&G, ch[i], lx, y);
                if (di < d)
                    d = di;
            }
            int core = d >= 16 ? 0 : d <= -16 ? 255 : ((16 - d) * 255) >> 5;   /* a 2 px soft edge */
            int glow = d <= 0 ? ST_GMAX : (d >> 4) < R ? gl[d >> 4] : 0;
            int a = core + DIV255((255 - core) * glow);
            row[mx] = (uint8_t)(a > 255 ? 255 : a);
        }
    }
}

static inline int st_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* screen the stamp's light (a 0..255) over one pixel's RGB */
static void st_screen(int *r, int *g, int *b, int a)
{
    /* the colour: red-orange at the rim, orange in the core (a^2 mix) */
    int t = DIV255(a * a);
    int cg = 64 + DIV255((150 - 64) * t), cb = 16 + DIV255((60 - 16) * t);
    *r += DIV255((255 - *r) * a);                /* red: 255 */
    *g += DIV255(DIV255((255 - *g) * cg) * a);
    *b += DIV255(DIV255((255 - *b) * cb) * a);
}

int wf_pf_stamp(uint32_t *img, int ipitch, int iw, int ih, int orient, const char *txt, uint8_t *map, uint32_t mapn)
{
    struct pf_flat fl = { (uint8_t *)img, ipitch };
    return wf_pf_stamp_fn(pf_flat_row, &fl, iw, ih, orient, txt, map, mapn);
}

int wf_pf_stamp_fn(wf_pf_rowfn *rowf, void *ctx, int iw, int ih, int orient, const char *txt, uint8_t *map,
                   uint32_t mapn)
{
    int H, pad, w;
    st_geom(iw, ih, &H, &pad);
    if (H < 8 || !txt || (w = st_width(txt, H)) <= 0)
        return -1;
    if (orient != 3 && orient != 6 && orient != 8)
        orient = 1;
    int rot = orient == 6 || orient == 8, dw = rot ? ih : iw, dh = rot ? iw : ih, s = iw < ih ? iw : ih;
    int mw = w + H * ST_SLANT / 100 + 2 * pad, mh = H + 2 * pad, mg = s * ST_M / 1000;
    int mu0 = dw - mg - w - pad, mv0 = dh - mg - H - pad;
    if ((uint32_t)mw * (uint32_t)mh > mapn || mu0 < 0 || mv0 < 0 || mu0 + mw > dw || mv0 + mh > dh)
        return -1;
    st_map(txt, H, pad, map, mw, mh);
    /* the map's shown rect -> the stored rect (forward map of the picture: o3 x = iw-1-u, y = ih-1-v; o6 x = v,
       y = ih-1-u; o8 x = iw-1-v, y = u) */
    int x0, y0, x1, y1;
    switch (orient) {
    case 3: x0 = iw - mu0 - mw; x1 = iw - mu0; y0 = ih - mv0 - mh; y1 = ih - mv0; break;
    case 6: x0 = mv0; x1 = mv0 + mh; y0 = ih - mu0 - mw; y1 = ih - mu0; break;
    case 8: x0 = iw - mv0 - mh; x1 = iw - mv0; y0 = mu0; y1 = mu0 + mw; break;
    default: x0 = mu0; x1 = mu0 + mw; y0 = mv0; y1 = mv0 + mh; break;
    }
    x0 &= ~1;
    if (x1 & 1) x1++;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = rowf(ctx, y);
        for (int x = x0; x < x1; x += 2) {
            int a2[2];
            for (int k = 0; k < 2; k++) {
                int xx = x + k, u, v;
                switch (orient) {
                case 3: u = iw - 1 - xx; v = ih - 1 - y; break;
                case 6: u = ih - 1 - y; v = xx; break;
                case 8: u = y; v = iw - 1 - xx; break;
                default: u = xx; v = y; break;
                }
                u -= mu0;
                v -= mv0;
                a2[k] = u >= 0 && u < mw && v >= 0 && v < mh ? map[v * mw + u] : 0;
            }
            if (!a2[0] && !a2[1])
                continue;
            uint32_t wd = row[x >> 1];
            int U = (int8_t)(wd & 0xFF), V = (int8_t)((wd >> 16) & 0xFF), Y[2] = { (int)(wd >> 8) & 0xFF, (int)(wd >> 24) };
            int rs = 0, gs = 0, bs = 0, yo[2];
            for (int k = 0; k < 2; k++) {
                int r = st_clamp(Y[k] + ((359 * V) >> 8)), g = st_clamp(Y[k] - ((88 * U + 183 * V) >> 8));
                int b = st_clamp(Y[k] + ((454 * U) >> 8));
                st_screen(&r, &g, &b, a2[k]);
                yo[k] = st_clamp((77 * r + 150 * g + 29 * b + 128) >> 8);
                rs += r; gs += g; bs += b;
            }
            int Uo = (-43 * rs - 85 * gs + 128 * bs + 256) >> 9, Vo = (128 * rs - 107 * gs - 21 * bs + 256) >> 9;
            Uo = Uo < -128 ? -128 : Uo > 127 ? 127 : Uo;
            Vo = Vo < -128 ? -128 : Vo > 127 ? 127 : Vo;
            row[x >> 1] = (uint32_t)(Uo & 0xFF) | (uint32_t)yo[0] << 8 | (uint32_t)(Vo & 0xFF) << 16 | (uint32_t)yo[1] << 24;
        }
    }
    return 0;
}

/* ================================================================== logo (wf_print.h, tools/mk_wflogo.py) */
#define PF_RED_Y  76                             /* the maker's red #BF1920 in the print's Y'CbCr (the stamp's matrix) */
#define PF_RED_U  (-24)
#define PF_RED_V  82
#define PF_LIGHT  228                            /* the wordmark on a black frame: light, as on the camera body */
#define PF_LMAX   10                             /* a map is scaled down at most this much (pf_rs taps) */
#define PF_RST    (PF_LMAX + 2)

/* a NUL-terminated key at o inside the pack */
static int pf_key(const uint8_t *b, uint32_t n, uint32_t o, const char **s)
{
    if (o < 16 || o >= n)
        return -1;
    for (uint32_t i = o; i < n; i++)
        if (!b[i]) {
            *s = (const char *)b + o;
            return 0;
        }
    return -1;
}

static int pf_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* 1 = model is "<maker> ..." */
static int pf_maker_of(const char *maker, const char *model)
{
    if (!maker || !model)
        return 0;
    while (*maker && *model == *maker) { maker++; model++; }
    return !*maker && *model == ' ';
}

int wf_pf_logo(struct wf_pf_logo *g, const uint8_t *b, uint32_t n, const char *model)
{
    uint8_t *z = (uint8_t *)g;
    for (unsigned i = 0; i < sizeof *g; i++)
        z[i] = 0;
    if (!b || n < 16 + 24 || b[0] != 'W' || b[1] != 'F' || b[2] != 'L' || b[3] != 'G' || rd16(b + 4) != 1)
        return -1;
    uint32_t ni = rd16(b + 6);
    if (ni < 1 || ni > 64 || rd32(b + 8) != n || 16 + ni * 24 > n || pf_fnv(b + 16, n - 16) != rd32(b + 12))
        return -1;
    int haswm = 0;
    for (uint32_t i = 0; i < ni; i++) {
        const uint8_t *d = b + 16 + i * 24;
        int w = (int)rd16(d + 2), h = (int)rd16(d + 4), top = (int)rd16(d + 6), base = (int)rd16(d + 8);
        uint32_t off = rd32(d + 12), ko = rd32(d + 16);
        const char *key = 0;
        if (d[1] != 8 || w < 8 || h < 8 || w > 2048 || h > 1024 || top >= base || base > h || off < 16 + ni * 24
            || off + (uint32_t)w * (uint32_t)h > n || (ko && pf_key(b, n, ko, &key)))
            return -1;
        struct wf_pf_img im = { b + off, w, h, top, base };
        if (d[0] == 0 && !haswm) {
            haswm = 1;
            g->wm = im;
            g->maker = key;
        } else if (d[0] == 1 && key && model && pf_streq(key, model))
            g->lt = im;
    }
    if (!haswm || !g->maker)
        return -1;
    if (!pf_maker_of(g->maker, model)) {         /* a shot of another maker (or no model known): no logo */
        g->wm.a = 0;
        g->lt.a = 0;
    }
    return 0;
}

void wf_pf_cap_logo(struct wf_pf_txt *t, struct wf_pf_cap *c, const struct wf_pf_logo *g)
{
    if (!g || !g->wm.a || !t->model[0] || !c->n1 || c->l1[0] != t->model)
        return;
    if (g->lt.a) {                               /* the lettering names the model */
        for (int i = 1; i < c->n1; i++)
            c->l1[i - 1] = c->l1[i];
        c->n1--;
        return;
    }
    if (!pf_maker_of(g->maker, t->model))
        return;
    int i = 0, j = 0;                            /* "Canon EOS 6D" -> "EOS 6D": the wordmark says the maker */
    while (g->maker[i])
        i++;
    i++;
    while ((t->model[j++] = t->model[i++]) != 0)
        ;
}

/* a shown rect inside the canvas, at a scale the resampler can do (down only, at most PF_LMAX) */
static int pf_lfit(const struct wf_pf_lay *L, const struct wf_pf_img *S, int u, int v, int w, int h)
{
    return w >= 4 && h >= 4 && u >= 0 && v >= 0 && u + w <= L->dw && v + h <= L->dh && S->w <= PF_LMAX * w
        && S->h <= PF_LMAX * h && w <= S->w && h <= S->h;
}

struct pf_rs { int16_t s; uint8_t n, pad; int16_t w[PF_RST]; };   /* one axis of an area resample: taps from s, Q12 */

uint32_t wf_pf_logo_place(const struct wf_pf_lay *L, const struct wf_pf_font *F, const struct wf_pf_logo *g,
                          struct wf_pf_cap *c, struct wf_pf_lpos *P)
{
    uint8_t *z = (uint8_t *)P;
    for (unsigned i = 0; i < sizeof *P; i++)
        z[i] = 0;
    if (!L->frame || !g || !g->wm.a)
        return 0;
    const struct wf_pf_img *W = &g->wm, *T = g->lt.a ? &g->lt : 0;
    int ws = W->base - W->top, ls = T ? T->base - T->top : 1, s = L->iw < L->ih ? L->iw : L->ih;
    int dw0 = L->dw - 2 * L->bd, dh0 = L->dh - L->bd - L->bb, hw, hl, wdw, wdh, ldw = 0, ldh = 0;
    int cap = L->tw && F && c, rsv = 0;
    if (cap) {                                   /* right of the caption: the wordmark over the lettering */
        struct pf_ln ln;
        pf_lines(L, F, &ln);
        int vtop = ln.b1 - ln.cap1, ub = L->bd + dw0;
        hw = ln.b2 - ln.cap2 - ln.cap2 * 2 / 5 - vtop;
        hl = ln.cap2;
        if (hw > ws) hw = ws;
        if (hl > ls) hl = ls;
        wdw = W->w * hw / ws;
        wdh = W->h * hw / ws;
        if (T) {
            ldw = T->w * hl / ls;
            if (ldw > wdw) {                     /* no wider than the wordmark */
                hl = hl * wdw / ldw;
                ldw = T->w * hl / ls;
            }
            ldh = T->h * hl / ls;
            P->u[1] = ub - ldw;
            P->v[1] = ln.b2 - T->base * hl / ls;
        }
        P->u[0] = ub - wdw;
        P->v[0] = vtop - W->top * hw / ws;
        rsv = (wdw > ldw ? wdw : ldw) + pf_sc(96, s);
    } else {                                     /* side by side, centred in the bottom border */
        hw = L->bb * 2 / 5;
        if (hw > ws) hw = ws;
        hl = T ? hw * 11 / 20 : 0;
        if (hl > ls) hl = ls;
        wdw = W->w * hw / ws;
        wdh = W->h * hw / ws;
        if (T) {
            ldw = T->w * hl / ls;
            ldh = T->h * hl / ls;
        }
        int gap = T ? hw * 3 / 5 : 0, u0 = L->bd + (dw0 - wdw - gap - ldw) / 2;
        int vtop = L->bd + dh0 + (L->bb - hw) / 2;
        P->u[0] = u0;
        P->v[0] = vtop - W->top * hw / ws;
        if (T) {
            P->u[1] = u0 + wdw + gap;
            P->v[1] = vtop + hw - T->base * hl / ls;
        }
    }
    P->w[0] = wdw;
    P->h[0] = wdh;
    P->src[0] = W;
    if (!pf_lfit(L, W, P->u[0], P->v[0], wdw, wdh))
        return 0;
    P->n = 1;
    if (T && pf_lfit(L, T, P->u[1], P->v[1], ldw, ldh)) {
        P->w[1] = ldw;
        P->h[1] = ldh;
        P->src[1] = T;
        P->n = 2;
    }
    if (cap) {
        c->rsv = rsv;
        c->roll3 = 1;
    }
    uint32_t nb = 0, tb = 0;
    for (int k = 0; k < P->n; k++) {
        uint32_t t = (uint32_t)(P->w[k] + P->h[k]) * sizeof(struct pf_rs);
        nb += ((uint32_t)P->w[k] * (uint32_t)P->h[k] + 3) & ~3u;
        if (t > tb) tb = t;
    }
    return nb + tb;
}

/* t[i] for dst i of m covering src [i n / m, (i + 1) n / m) (1/256 px), weights summing to 4096 */
static void pf_rs_axis(struct pf_rs *t, int n, int m)
{
    for (int i = 0; i < m; i++) {
        int a = i * n * 256 / m, b = (i + 1) * n * 256 / m, s = a >> 8, e = (b + 255) >> 8, tot = b - a, sum = 0, big = 0;
        if (e - s > PF_RST)
            e = s + PF_RST;
        t[i].s = (int16_t)s;
        t[i].n = (uint8_t)(e - s);
        for (int k = s; k < e; k++) {
            int lo = a > k * 256 ? a : k * 256, hi = b < (k + 1) * 256 ? b : (k + 1) * 256;
            int w = ((hi - lo) * 4096 + tot / 2) / tot;
            t[i].w[k - s] = (int16_t)w;
            sum += w;
            if (w > t[i].w[big])
                big = k - s;
        }
        t[i].w[big] = (int16_t)(t[i].w[big] + 4096 - sum);
    }
}

int wf_pf_logo_maps(struct wf_pf_lay *L, const struct wf_pf_lpos *P, uint8_t *mem, uint32_t n)
{
    L->nink = 0;
    uint32_t used = 0;
    for (int k = 0; k < P->n; k++)
        used += ((uint32_t)P->w[k] * (uint32_t)P->h[k] + 3) & ~3u;
    struct pf_rs *tx = (struct pf_rs *)(mem + used);
    for (int k = 0; k < P->n; k++)
        if (used + (uint32_t)(P->w[k] + P->h[k]) * sizeof(struct pf_rs) > n || ((uintptr_t)mem & 3))
            return -1;
    int black = L->frame == WF_PFR_BLACK || L->frame == WF_PFR_BLACK_W;
    uint8_t *map = mem;
    for (int k = 0; k < P->n; k++) {
        const struct wf_pf_img *S = P->src[k];
        int w = P->w[k], h = P->h[k], u0 = P->u[k], v0 = P->v[k];
        struct pf_rs *ty = tx + w;
        pf_rs_axis(tx, S->w, w);
        pf_rs_axis(ty, S->h, h);
        struct wf_pf_ink *K = &L->ink[k];
        int rot = L->orient == 6 || L->orient == 8, sw = rot ? h : w;   /* the map's stored width */
        switch (L->orient) {                     /* the shown rect -> the stored rect (pf_map's forward map) */
        case 3: K->x = L->cw - u0 - w; K->y = L->ch - v0 - h; break;
        case 6: K->x = v0; K->y = L->ch - u0 - w; break;
        case 8: K->x = L->cw - v0 - h; K->y = u0; break;
        default: K->x = u0; K->y = v0; break;
        }
        K->w = sw;
        K->h = rot ? w : h;
        K->map = map;
        if (k == 0) {
            K->Y = black ? PF_LIGHT : PF_RED_Y;
            K->U = black ? 0 : PF_RED_U;
            K->V = black ? 0 : PF_RED_V;
        } else {
            K->Y = L->fg1;
            K->U = K->V = 0;
        }
        for (int b = 0; b < h; b++) {
            const struct pf_rs *ry = &ty[b];
            for (int a = 0; a < w; a++) {
                const struct pf_rs *rx = &tx[a];
                int acc = 0;
                for (int r = 0; r < ry->n; r++) {
                    const uint8_t *sr = S->a + (ry->s + r) * S->w + rx->s;
                    int rs = 0;
                    for (int q = 0; q < rx->n; q++)
                        rs += sr[q] * rx->w[q];
                    acc += (rs >> 8) * ry->w[r];
                }
                int v = (acc + 32768) >> 16, xs, ys;
                switch (L->orient) {
                case 3: xs = w - 1 - a; ys = h - 1 - b; break;
                case 6: xs = b; ys = w - 1 - a; break;
                case 8: xs = h - 1 - b; ys = a; break;
                default: xs = a; ys = b; break;
                }
                map[ys * sw + xs] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v);
            }
        }
        map += ((uint32_t)w * (uint32_t)h + 3) & ~3u;
    }
    L->nink = P->n;
    return 0;
}
