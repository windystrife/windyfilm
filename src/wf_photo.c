/* wf_photo.c - the Darkroom's photos from the CR2's small RGB (wf_photo.h). WindyStrife's own code: TIFF tags read
 * from the TIFF 6.0 layout, a box average, a histogram. No division wider than 32 bits (DryOS). */
#include <string.h>
#include "wf_photo.h"

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

uint32_t wf_ph_head(const uint8_t *b, uint32_t n)
{
    if (!b || n < 8 || b[0] != 'I' || b[1] != 'I' || le16(b + 2) != 42)
        return 0;
    return le32(b + 4);
}

int wf_ph_ifd(const uint8_t *b, uint32_t n, uint32_t base, struct wf_ph_ifd *o)
{
    memset(o, 0, sizeof *o);
    if (!b || n < 2)
        return -1;
    uint32_t k = le16(b);
    if (k == 0 || k > 200 || 2 + 12 * k + 4 > n)
        return -1;
    for (uint32_t i = 0; i < k; i++) {
        const uint8_t *e = b + 2 + 12 * i;
        uint32_t tag = le16(e), ty = le16(e + 2), cnt = le32(e + 4);
        uint32_t v = ty == 3 ? le16(e + 8) : le32(e + 8);         /* SHORT or LONG, one value in the entry */
        switch (tag) {
        case 256: o->w = (int)v; break;
        case 257: o->h = (int)v; break;
        case 259: o->comp = (int)v; break;
        case 274: o->orient = (int)v; break;
        case 277: o->spp = (int)v; break;
        case 273: if (cnt == 1) o->off = v; break;
        case 279: if (cnt == 1) o->bytes = v; break;
        case 258:                                                   /* one value, or an array (one per sample) */
            if (ty == 3 && cnt == 1)
                o->bps = (int)v;
            else if (ty == 3 && cnt >= 3 && le32(e + 8) >= base && le32(e + 8) - base + 2 <= n)
                o->bps = (int)le16(b + (le32(e + 8) - base));
            break;
        }
    }
    o->next = le32(b + 2 + 12 * k);
    return 0;
}

int wf_ph_rgb_ok(const struct wf_ph_ifd *d)
{
    return d->comp == 1 && d->spp == 3 && (d->bps == 16 || d->bps == 0) && d->w >= 64 && d->h >= 48 && d->w <= 2048
        && d->h <= 2048 && d->off && d->bytes == (uint32_t)d->w * (uint32_t)d->h * 6u;
}

/* the masked band of the bodies this module knows (measured on their CR2s: the left columns, the EOS M's top rows) */
static void ph_band(int sw, int sh, int *x0, int *y0)
{
    *x0 = *y0 = 0;
    if (sw == 660 && sh == 441) { *x0 = 9; *y0 = 6; }               /* EOS M */
    else if (sw == 362 && sh == 234) { *x0 = 10; }                   /* 5D2 */
}

int wf_ph_band(const struct wf_ph *t, int r)
{
    return t->y0 + (int)((uint32_t)(t->sh - t->y0) * (uint32_t)r / (uint32_t)t->dh);
}

int wf_ph_begin(struct wf_ph *t, int sw, int sh, int dw, int dh, uint16_t *out)
{
    memset(t, 0, sizeof *t);
    ph_band(sw, sh, &t->x0, &t->y0);
    if (dw < 1 || dh < 1 || dw > WF_PH_BW || dw > sw - t->x0 || dh > sh - t->y0 || !out)
        return -1;
    t->sw = sw; t->sh = sh; t->dw = dw; t->dh = dh; t->out = out;
    for (int i = 0; i <= dw; i++)
        t->cx[i] = (uint16_t)(t->x0 + (int)((uint32_t)(sw - t->x0) * (uint32_t)i / (uint32_t)dw));
    return 0;
}

static void ph_flush(struct wf_ph *t)
{
    uint16_t *o = t->out + t->row * t->dw;
    if (t->n)
        for (int i = 0; i < t->dw; i++) {
            uint32_t c = (uint32_t)(t->cx[i + 1] - t->cx[i]) * (uint32_t)t->n;
            o[i] = (uint16_t)(t->sum[i] / (c ? c : 1));
        }
    else if (t->row)
        memcpy(o, o - t->dw, (size_t)t->dw * 2);
    else
        memset(o, 0, (size_t)t->dw * 2);
    memset(t->sum, 0, sizeof t->sum);
    t->n = 0;
    t->row++;
}

void wf_ph_rows(struct wf_ph *t, const uint16_t *rgb, int y, int n)
{
    for (int k = 0; k < n; k++, rgb += t->sw * 3) {
        int sy = y + k;
        if (sy < 0 || sy >= t->sh || t->row >= t->dh)
            continue;
        for (int x = 0; x < t->x0; x++) {                           /* the black, from every row's masked columns */
            const uint16_t *p = rgb + x * 3;
            t->bsum += ((uint32_t)p[0] * 77 + (uint32_t)p[1] * 150 + (uint32_t)p[2] * 29) >> 8;
            t->bn++;
        }
        if (sy < t->y0)
            continue;
        while (t->row < t->dh && sy >= wf_ph_band(t, t->row + 1))
            ph_flush(t);
        if (t->row >= t->dh)
            continue;
        for (int i = 0; i < t->dw; i++) {
            uint32_t s = 0;
            for (int x = t->cx[i]; x < t->cx[i + 1]; x++) {
                const uint16_t *p = rgb + x * 3;
                s += ((uint32_t)p[0] * 77 + (uint32_t)p[1] * 150 + (uint32_t)p[2] * 29) >> 8;
            }
            t->sum[i] += s;
        }
        t->n++;
    }
}

void wf_ph_end(struct wf_ph *t)
{
    while (t->row < t->dh)
        ph_flush(t);
}

int wf_ph_black(const struct wf_ph *t)
{
    return t->bn ? (int)(t->bsum / t->bn) : -1;
}

void wf_ph_levels(const uint16_t *v, int n, int black, int *lo, int *hi)
{
    uint32_t h[256];
    int mx = 0, sh = 0;
    for (int i = 0; i < n; i++)
        if (v[i] > mx)
            mx = v[i];
    while ((mx >> sh) > 255)
        sh++;
    memset(h, 0, sizeof h);
    for (int i = 0; i < n; i++)
        h[v[i] >> sh]++;
    uint32_t a = (uint32_t)n / 200, c = 0;                          /* 0.5 % at each end */
    int b0 = 0, b1 = 255;
    for (c = 0; b0 < 255 && c + h[b0] <= a; b0++)
        c += h[b0];
    for (c = 0; b1 > 0 && c + h[b1] <= a; b1--)
        c += h[b1];
    *lo = black >= 0 ? black : b0 << sh;
    *hi = ((b1 + 1) << sh) - 1;
    if (*hi < *lo + WF_PH_SPAN)
        *hi = *lo + WF_PH_SPAN;
}

/* the sRGB curve (IEC 61966-2-1) at x / 256 of linear light, 0..255: Canon's small RGB is linear (measured: its median
 * over the black matches the camera JPEG's linearised median), the screen's ramps are gamma-encoded like a JPEG */
static const uint8_t ph_srgb[257] = {
      0,  13,  22,  28,  34,  38,  42,  46,  49,  53,  56,  58,  61,  64,  66,  68,  71,  73,  75,
     77,  79,  81,  83,  85,  86,  88,  90,  91,  93,  95,  96,  98,  99, 101, 102, 103, 105, 106,
    107, 109, 110, 111, 113, 114, 115, 116, 118, 119, 120, 121, 122, 123, 124, 126, 127, 128, 129,
    130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 145, 146, 147,
    148, 149, 150, 151, 152, 153, 153, 154, 155, 156, 157, 158, 158, 159, 160, 161, 162, 162, 163,
    164, 165, 166, 166, 167, 168, 169, 169, 170, 171, 172, 172, 173, 174, 174, 175, 176, 177, 177,
    178, 179, 179, 180, 181, 181, 182, 183, 184, 184, 185, 186, 186, 187, 188, 188, 189, 189, 190,
    191, 191, 192, 193, 193, 194, 195, 195, 196, 196, 197, 198, 198, 199, 199, 200, 201, 201, 202,
    202, 203, 204, 204, 205, 205, 206, 207, 207, 208, 208, 209, 209, 210, 211, 211, 212, 212, 213,
    213, 214, 214, 215, 216, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 223, 223,
    224, 224, 225, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233,
    233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 239, 240, 240, 241, 241, 242,
    242, 243, 243, 244, 244, 245, 245, 246, 246, 246, 247, 247, 248, 248, 249, 249, 250, 250, 251,
    251, 251, 252, 252, 253, 253, 254, 254, 255, 255
};

void wf_ph_to8(const uint16_t *v, uint8_t *o, int n, int lo, int hi)
{
    uint32_t d = (uint32_t)(hi > lo ? hi - lo : 1);
    for (int i = 0; i < n; i++) {
        int x = v[i] - lo;
        uint32_t t = x <= 0 ? 0 : (uint32_t)x >= d ? 65536u : (uint32_t)x * 65536u / d;   /* 16.16 of the range */
        uint32_t k = t >> 8, f = t & 255;
        o[i] = k >= 256 ? 255 : (uint8_t)(ph_srgb[k] + (((uint32_t)(ph_srgb[k + 1] - ph_srgb[k]) * f) >> 8));
    }
}

int wf_ph_find(wf_ph_rd rd, void *ctx, uint8_t *b, struct wf_ph_src *s)
{
    struct wf_ph_ifd d;
    memset(s, 0, sizeof *s);
    if (rd(ctx, 0, b, 8) != 0)
        return -1;
    uint32_t o = wf_ph_head(b, 8);
    for (int k = 0; k < 3; k++) {                                   /* IFD0 (the orientation), IFD1, IFD2 */
        if (!o || rd(ctx, o, b, WF_PH_IFDB) != 0 || wf_ph_ifd(b, WF_PH_IFDB, o, &d) != 0)
            return -1;
        if (!k)
            s->orient = d.orient;
        o = d.next;
    }
    if (!wf_ph_rgb_ok(&d) || d.w > WF_PH_SWMAX)
        return -1;
    s->w = d.w;
    s->h = d.h;
    s->off = d.off;
    return 0;
}

int wf_ph_cell(wf_ph_rd rd, void *ctx, const struct wf_ph_src *s, struct wf_ph *t, int cw, int ch, uint16_t *rows,
               uint16_t *t16, uint8_t *cell)
{
    uint32_t rb = (uint32_t)s->w * 6;
    int lo, hi;
    if (wf_ph_begin(t, s->w, s->h, cw, ch, t16) != 0)
        return -1;
    for (int r = 0; r < ch; r++) {
        int a = wf_ph_band(t, r), b = wf_ph_band(t, r + 1), m = (a + b) / 2 - 1;
        if (m < a)
            m = a;
        if (m + 3 > b)
            m = b - 3 < a ? a : b - 3;
        int n = m + 3 <= b ? 3 : b - m;
        if (n <= 0)
            continue;
        if (rd(ctx, s->off + (uint32_t)m * rb, rows, (uint32_t)n * rb) != 0)
            return -1;
        wf_ph_rows(t, rows, m, n);
    }
    wf_ph_end(t);
    wf_ph_levels(t16, cw * ch, wf_ph_black(t), &lo, &hi);
    wf_ph_to8(t16, cell, cw * ch, lo, hi);
    return 0;
}

int wf_ph_big(wf_ph_rd rd, void *ctx, const struct wf_ph_src *s, struct wf_ph *t, uint16_t *rows, uint16_t *t16,
              uint8_t *big, int *bw, int *bh)
{
    int port = s->orient == 6 || s->orient == 8, dw = port ? WF_PH_BH : WF_PH_BW, dh = port ? WF_PH_BH * 2 / 3 : WF_PH_BH;
    uint32_t rb = (uint32_t)s->w * 6;
    int lo, hi;
    if (wf_ph_begin(t, s->w, s->h, dw, dh, t16) != 0)
        return -1;
    for (int y = 0; y < s->h; y += WF_PH_NROW) {
        int n = s->h - y < WF_PH_NROW ? s->h - y : WF_PH_NROW;
        if (rd(ctx, s->off + (uint32_t)y * rb, rows, (uint32_t)n * rb) != 0)
            return -1;
        wf_ph_rows(t, rows, y, n);
    }
    wf_ph_end(t);
    wf_ph_levels(t16, dw * dh, wf_ph_black(t), &lo, &hi);
    wf_ph_to8(t16, (uint8_t *)t16, dw * dh, lo, hi);                 /* in place: 8 bits behind the 16 read */
    wf_ph_turn((const uint8_t *)t16, dw, dh, s->orient, big);
    *bw = port ? dh : dw;
    *bh = port ? dw : dh;
    return 0;
}

void wf_ph_turn(const uint8_t *in, int w, int h, int orient, uint8_t *out)
{
    if (orient == 6)                                                /* turn right: the top row becomes the right column */
        for (int y = 0; y < w; y++)
            for (int x = 0; x < h; x++)
                out[y * h + x] = in[(h - 1 - x) * w + y];
    else if (orient == 8)                                           /* turn left */
        for (int y = 0; y < w; y++)
            for (int x = 0; x < h; x++)
                out[y * h + x] = in[x * w + (w - 1 - y)];
    else if (orient == 3)
        for (int i = 0; i < w * h; i++)
            out[i] = in[w * h - 1 - i];
    else if (out != in)
        memcpy(out, in, (size_t)w * h);
}
