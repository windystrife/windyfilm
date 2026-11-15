/* wf_darkp.c - Roll + Darkroom: the pure per-frame parts (wf_darkp.h). WindyStrife's own code, written from scratch for
 * the darkroom: the TIFF / Exif walk follows the public TIFF 6.0 / Exif 2.3 layouts (as wf_menu.c's UserComment writer). */
#include "wf_darkp.h"

/* ------------------------------------------------------------------ levels and scales */
int wf_dk_white(int cam, int iso)
{
    if (cam == WF_DK_5D2)
        return 15600;                            /* 5D2: the pile-up of the shot itself normally wins (camera run owed) */
    /* EOS M: the clip pile-ups measured in the Day09 / 1114 CR2s; the 1/3-stop ISOs clip lower */
    return iso == 100 ? 13582 : iso == 160 || iso == 320 ? 12636 : iso == 200 ? 15303 : 15200;
}

/* 2^(k / 10) in Q12, k = 0..9 */
static const uint16_t wf_dk_p10[10] = { 4096, 4390, 4705, 5043, 5405, 5793, 6208, 6654, 7132, 7643 };

/* exposure trims, 0.1 EV per ISO: B's WB / exposure fit against Canon's own L JPEG minus the fixed scene scale (0.18 at
 * 3.3 EV below the clip), the median per ISO of tools/dr_dark_check.sh over the 34 Day09 EOS M pairs (11-14): ISO 100
 * +0.21 (n 4), 160 +0.33 (n 3), 200 +0.30 (n 6, spread -0.08..+0.82), 320 +0.14 (n 21, spread -0.40..+0.23). Other ISOs
 * take the nearest row (no pairs yet). The 5D2 has no pairs: 0 until its camera run. */
struct wf_dk_trim { uint16_t iso; int8_t t10; };
static const struct wf_dk_trim wf_dk_trims[2][8] = {
    { { 100, 2 }, { 160, 3 }, { 200, 3 }, { 320, 1 }, { 0, 0 } },
    { { 100, 0 }, { 0, 0 } },
};

int wf_dk_trim10(int cam, int iso)
{
    const struct wf_dk_trim *t = wf_dk_trims[cam == WF_DK_5D2 ? 1 : 0];
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 8 && t[i].iso; i++) {
        int d = iso - t[i].iso;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = t[i].t10; }
    }
    return best;
}

/* 256 x log2(v), v >= 1: the top bit's place, then 256 x log2(1 + i / 32) of the next five bits, linear between */
static int dk_log2q8(uint32_t v)
{
    static const uint16_t t[33] = { 0, 11, 22, 33, 44, 54, 63, 73, 82, 92, 100, 109, 118, 126, 134, 142, 150, 157, 165, 172,
                                    179, 186, 193, 200, 207, 213, 220, 226, 232, 238, 244, 250, 256 };
    int e = 31 - __builtin_clz(v);
    uint32_t m = e >= 13 ? v >> (e - 13) : v << (13 - e);   /* 1.13: 8192 .. 16383 */
    uint32_t i = (m >> 8) & 31, f = m & 255;
    return 256 * e + t[i] + (int)(((t[i + 1] - t[i]) * f + 128) >> 8);
}

/* EV100 = log2(N^2 / t) - log2(ISO / 100), N = fn_n / fn_d, t = et_n / et_d: sums of log2 of 32-bit values only */
int wf_dk_ev8(int iso, uint32_t et_n, uint32_t et_d, uint32_t fn_n, uint32_t fn_d)
{
    if (iso <= 0 || !et_n || !et_d || !fn_n || !fn_d)
        return WF_DK_EV_BAD;
    int q = 2 * dk_log2q8(fn_n) - 2 * dk_log2q8(fn_d) + dk_log2q8(et_d) - dk_log2q8(et_n) + dk_log2q8(100)
          - dk_log2q8((uint32_t)iso);
    return (q + 16) >> 5;                        /* Q8 -> x 8, rounded (an arithmetic shift for EV < 0) */
}

int wf_dk_scales(int cam, const int wb[4], int black, int white, int iso, int32_t m[3])
{
    static const int32_t day[2][3] = { { 9892, 4096, 5590 }, { 10542, 4096, 5683 } };   /* = wf_dev.c wf_dev_day */
    const int32_t *d = day[cam == WF_DK_5D2 ? 1 : 0];
    int cl = white - black;
    if (cl < 1024)
        cl = 1024;
    uint32_t mg = ((29746152u * 16u) / (uint32_t)cl) * 16u;   /* clip = 0.18 x 2^3.3 = 1.773 (wf_dev.c) */
    int t = wf_dk_trim10(cam, iso);
    if (t < -40) t = -40;
    if (t > 40) t = 40;
    int e = t >= 0 ? t / 10 : -((-t + 9) / 10), f = t - e * 10;   /* t = 10 e + f, f 0..9 */
    uint64_t g = (uint64_t)mg * wf_dk_p10[f];
    g = e >= 0 ? g << e : g >> -e;
    mg = (uint32_t)(g >> 12);
    int32_t r[3];
    int as = 0;
    int gs = wb ? wb[1] + wb[2] : 0;
    if (wb && wb[0] > 0 && wb[3] > 0 && gs > 0 && wb[0] < 16384 && wb[3] < 16384) {
        r[0] = (int32_t)(((uint32_t)wb[0] << 13) / (uint32_t)gs);   /* 2 wb[0] / (G1 + G2), Q12 */
        r[1] = 4096;
        r[2] = (int32_t)(((uint32_t)wb[3] << 13) / (uint32_t)gs);
        as = r[0] >= 1024 && r[0] <= 16384 && r[2] >= 1024 && r[2] <= 16384;
    }
    for (int k = 0; k < 3; k++)
        m[k] = (int32_t)(((uint64_t)mg * (uint32_t)(as ? r[k] : d[k])) >> 12);
    return as;
}

/* ------------------------------------------------------------------ Exif */
int wf_dk_app1(const uint8_t *p, uint32_t n, uint32_t *off, uint32_t *len)
{
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8)
        return -1;
    uint32_t o = 2;
    for (int i = 0; i < 16 && o + 4 <= n; i++) {
        if (p[o] != 0xFF || p[o + 1] == 0xDA || p[o + 1] == 0xD9)
            return -1;
        uint32_t l = (uint32_t)p[o + 2] << 8 | p[o + 3];
        if (l < 2)
            return -1;
        if (p[o + 1] == 0xE1 && o + 10 <= n && p[o + 4] == 'E' && p[o + 5] == 'x' && p[o + 6] == 'i' && p[o + 7] == 'f'
            && !p[o + 8] && !p[o + 9]) {
            if (o + 2 + l > n)
                return -1;
            *off = o;
            *len = l + 2;
            return 0;
        }
        o += 2 + l;
    }
    return -1;
}

static uint32_t wf_dk_u(const uint8_t *p, int n, int le)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v = (v << 8) | p[le ? n - 1 - i : i];
    return v;
}

/* tag in the IFD at TIFF offset ifd of t[0 .. tn): e = type, count, value; 0 = not there */
static int wf_dk_find(const uint8_t *t, uint32_t tn, int le, uint32_t ifd, unsigned tag, uint32_t e[3])
{
    if (ifd < 8 || ifd + 2 > tn)
        return 0;
    uint32_t cnt = wf_dk_u(t + ifd, 2, le);
    for (uint32_t i = 0; i < cnt && i < 256 && ifd + 2 + i * 12 + 12 <= tn; i++) {
        const uint8_t *q = t + ifd + 2 + i * 12;
        if (wf_dk_u(q, 2, le) == tag) {
            e[0] = wf_dk_u(q + 2, 2, le);
            e[1] = wf_dk_u(q + 4, 4, le);
            e[2] = wf_dk_u(q + 8, 4, le);
            return 1;
        }
    }
    return 0;
}

int wf_dk_comment(uint8_t *app1, uint32_t len, const char *txt)
{
    if (len < 18)
        return -1;
    uint8_t *t = app1 + 10;
    uint32_t tn = len - 10, e[3];
    int le = t[0] == 'I' && t[1] == 'I';
    if (!le && !(t[0] == 'M' && t[1] == 'M'))
        return -1;
    if (wf_dk_u(t + 2, 2, le) != 42)
        return -1;
    if (!wf_dk_find(t, tn, le, wf_dk_u(t + 4, 4, le), 0x8769, e) || e[0] != 4)
        return -1;
    if (!wf_dk_find(t, tn, le, e[2], 0x9286, e) || e[0] != 7 || e[1] < 8 || e[1] > 4096 || e[1] <= 4)
        return -1;
    uint32_t at = e[2], cn = e[1];
    if (at < 8 || at + cn > tn)
        return -1;
    uint32_t tl = 0;
    while (txt[tl])
        tl++;
    if (8 + tl > cn)
        return -1;
    static const uint8_t hdr[8] = { 'A', 'S', 'C', 'I', 'I', 0, 0, 0 };
    for (uint32_t i = 0; i < cn; i++)
        t[at + i] = i < 8 ? hdr[i] : i - 8 < tl ? (uint8_t)txt[i - 8] : 0;
    return 0;
}

/* ------------------------------------------------------------------ thumbnails */
void wf_dk_thumb_begin(struct wf_dk_thumb *t, int w, int h)
{
    t->w = w;
    t->h = h;
    t->bx = w / WF_DK_PW;
    t->by = h / WF_DK_PH;
    t->x0 = ((w - t->bx * WF_DK_PW) / 2) & ~1;
    t->y0 = (h - t->by * WF_DK_PH) / 2;
    t->row = 0;
    t->n = 0;
    for (int c = 0; c < WF_DK_PW; c++)
        t->sum[c] = 0;
}

/* every 2nd row of a box, the Y0 of every word (even pixels): a quarter of the frame read */
void wf_dk_thumb_rows(struct wf_dk_thumb *t, const uint32_t *rows, int pitch, int y, int n, uint8_t *print)
{
    if (t->bx < 2 || t->by < 2)
        return;
    for (int r = 0; r < n; r++, y++) {
        int ry = y - t->y0 - t->row * t->by;
        if (t->row >= WF_DK_PH || ry < 0)
            continue;
        if (!(ry & 1)) {
            const uint32_t *wd = (const uint32_t *)((const uint8_t *)rows + r * pitch);
            int xa = t->x0 >> 1;
            for (int c = 0; c < WF_DK_PW; c++) {
                int xb = (t->x0 + (c + 1) * t->bx) >> 1;
                uint32_t s = 0;
                for (int x = xa; x < xb; x++)
                    s += (wd[x] >> 8) & 0xFF;
                t->sum[c] += s;
                xa = xb;
            }
            t->n++;
        }
        if (ry == t->by - 1) {                   /* the box row is complete */
            int xa = t->x0 >> 1;
            for (int c = 0; c < WF_DK_PW; c++) {
                int xb = (t->x0 + (c + 1) * t->bx) >> 1;
                uint32_t cnt = (uint32_t)(xb - xa) * (uint32_t)t->n;
                print[t->row * WF_DK_PW + c] = (uint8_t)(cnt ? (t->sum[c] + cnt / 2) / cnt : 0);
                t->sum[c] = 0;
                xa = xb;
            }
            t->n = 0;
            t->row++;
        }
    }
}

int wf_dk_thumb_done(const struct wf_dk_thumb *t) { return t->row; }

void wf_dk_wire(const uint8_t *print, uint8_t *wire)
{
    for (int y = 0; y < WF_DK_TH; y++)
        for (int x = 0; x < WF_DK_TW; x++) {
            uint32_t s = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    s += print[(y * 4 + j) * WF_DK_PW + x * 4 + i];
            wire[y * WF_DK_TW + x] = (uint8_t)((s + 8) >> 4);
        }
}
