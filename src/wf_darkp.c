/* wf_darkp.c - Roll + Darkroom: the pure per-frame parts (wf_darkp.h). WindyStrife's own code, written from scratch for
 * the darkroom: the TIFF / Exif walk follows the public TIFF 6.0 / Exif 2.3 layouts (as wf_menu.c's UserComment writer). */
#include "wf_darkp.h"

/* ------------------------------------------------------------------ levels and scales */
static const struct wf_dk_geom wf_dk_geoms[2] = { WF_DK_GEOM_EOSM, WF_DK_GEOM_5D2 };
const struct wf_dk_geom *wf_dk_geom(int cam) { return &wf_dk_geoms[cam == WF_DK_5D2 ? 1 : 0]; }

int wf_dk_white(int cam, int iso)
{
    /* 5D2: the clip pile-ups in the owner's Day 10 + 100EOS5D CR2s (11-17): ISO 1250 13264..13318, every other ISO
     * 15761..16383 (2500 and 5000 too: 16268..16332). 160 / 320 / 640 have no pairs: taken as 1250 (1/3 stop down) */
    if (cam == WF_DK_5D2)
        return iso == 160 || iso == 320 || iso == 640 || iso == 1250 ? 13300 : 16000;
    /* EOS M: the clip pile-ups measured in the Day09 / 1114 CR2s; the 1/3-stop ISOs clip lower */
    return iso == 100 ? 13582 : iso == 160 || iso == 320 ? 12636 : iso == 200 ? 15303 : 15200;
}

/* 2^(k / 10) in Q12, k = 0..9 */
static const uint16_t wf_dk_p10[10] = { 4096, 4390, 4705, 5043, 5405, 5793, 6208, 6654, 7132, 7643 };

/* exposure trims, 0.1 EV per ISO: B's WB / exposure fit against Canon's own L JPEG minus the fixed scene scale (0.18 at
 * 3.3 EV below the clip), the median per ISO of tools/dr_dark_check.sh over the 34 Day09 EOS M pairs (11-14): ISO 100
 * +0.21 (n 4), 160 +0.33 (n 3), 200 +0.30 (n 6, spread -0.08..+0.82), 320 +0.14 (n 21, spread -0.40..+0.23). Other ISOs
 * take the nearest row (no pairs yet). 5D2: one trim for every ISO, the median of the same fit over the owner's 55 RAW+L
 * pairs (Day 10 + 100EOS5D, 11-17; ISO 100..6400): +0.74. The per-ISO medians (200 +0.71 n 16, 1600 +0.90 n 10, 2000 +1.06
 * n 6, 3200 +0.46 n 5, ...) follow the scenes more than the ISO: left out one pair at a time, the single median misses by
 * 0.27 EV on average and a per-ISO table (n >= 3) by 0.35. */
struct wf_dk_trim { uint16_t iso; int8_t t10; };
static const struct wf_dk_trim wf_dk_trims[2][8] = {
    { { 100, 2 }, { 160, 3 }, { 200, 3 }, { 320, 1 }, { 0, 0 } },
    { { 100, 7 }, { 0, 0 } },
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

int wf_dk_wb_pick(const int wb[4], const int wb_auto[4], int want_auto, const int **use)
{
    int ok = want_auto && wb_auto && wb_auto[0] > 0 && wb_auto[1] > 0 && wb_auto[2] > 0 && wb_auto[3] > 0;
    *use = ok ? wb_auto : wb;
    return ok ? WF_DKWB_AUTO : want_auto ? WF_DKWB_NOAUTO : WF_DKWB_ASSHOT;
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

/* the 12-byte entry of tag in the IFD at TIFF offset ifd of t[0 .. tn): its offset, 0 = not there */
static uint32_t wf_dk_entry(const uint8_t *t, uint32_t tn, int le, uint32_t ifd, unsigned tag)
{
    if (ifd < 8 || ifd + 2 > tn)
        return 0;
    uint32_t cnt = wf_dk_u(t + ifd, 2, le);
    for (uint32_t i = 0; i < cnt && i < 256 && ifd + 2 + i * 12 + 12 <= tn; i++)
        if (wf_dk_u(t + ifd + 2 + i * 12, 2, le) == tag)
            return ifd + 2 + i * 12;
    return 0;
}

/* tag in the IFD at TIFF offset ifd of t[0 .. tn): e = type, count, value; 0 = not there */
static int wf_dk_find(const uint8_t *t, uint32_t tn, int le, uint32_t ifd, unsigned tag, uint32_t e[3])
{
    uint32_t o = wf_dk_entry(t, tn, le, ifd, tag);
    if (!o)
        return 0;
    e[0] = wf_dk_u(t + o + 2, 2, le);
    e[1] = wf_dk_u(t + o + 4, 4, le);
    e[2] = wf_dk_u(t + o + 8, 4, le);
    return 1;
}

/* the TIFF header: 1 = little endian, 0 = big, -1 = not a TIFF; *ifd0 = IFD0's offset */
static int wf_dk_tiff(const uint8_t *t, uint32_t tn, uint32_t *ifd0)
{
    if (tn < 16)
        return -1;
    int le = t[0] == 'I' && t[1] == 'I';
    if (!le && !(t[0] == 'M' && t[1] == 'M'))
        return -1;
    if (wf_dk_u(t + 2, 2, le) != 42)
        return -1;
    *ifd0 = wf_dk_u(t + 4, 4, le);
    return le;
}

/* an ASCII tag into d (n bytes, trailing spaces dropped): 1 = something copied */
static int wf_dk_str(const uint8_t *t, uint32_t tn, int le, uint32_t ifd, unsigned tag, char *d, int n)
{
    uint32_t e[3], o = wf_dk_entry(t, tn, le, ifd, tag);
    if (!o || !wf_dk_find(t, tn, le, ifd, tag, e) || e[0] != 2 || e[1] < 2 || e[1] > 4096)
        return 0;
    const uint8_t *p = e[1] <= 4 ? t + o + 8 : e[2] < tn && e[2] + e[1] <= tn ? t + e[2] : 0;
    if (!p)
        return 0;
    int k = 0;
    for (uint32_t i = 0; i < e[1] && k < n - 1 && p[i]; i++)
        d[k++] = p[i] >= 32 && p[i] < 127 ? (char)p[i] : '?';
    while (k && d[k - 1] == ' ')
        k--;
    d[k] = 0;
    return k > 0;
}

/* a RATIONAL tag (num, den): 1 = read (den != 0) */
static int wf_dk_rat(const uint8_t *t, uint32_t tn, int le, uint32_t ifd, unsigned tag, uint32_t r[2])
{
    uint32_t e[3];
    if (!wf_dk_find(t, tn, le, ifd, tag, e) || e[0] != 5 || e[1] < 1 || e[2] < 8 || e[2] + 8 > tn)
        return 0;
    uint32_t a = wf_dk_u(t + e[2], 4, le), b = wf_dk_u(t + e[2] + 4, 4, le);
    if (!b)
        return 0;
    r[0] = a;
    r[1] = b;
    return 1;
}

/* a SHORT / LONG tag (count 1): its value, -1 = none */
static int wf_dk_int(const uint8_t *t, uint32_t tn, int le, uint32_t ifd, unsigned tag)
{
    uint32_t e[3];
    if (!wf_dk_find(t, tn, le, ifd, tag, e) || e[1] < 1)
        return -1;
    if (e[0] == 3)
        return (int)(le ? e[2] & 0xFFFF : e[2] >> 16);
    return e[0] == 4 && e[2] < 0x7FFFFFFFu ? (int)e[2] : -1;
}

int wf_dk_shot(const uint8_t *t, uint32_t tn, struct wf_dk_shot *s)
{
    uint32_t i0, e[3], r[2];
    int le = wf_dk_tiff(t, tn, &i0), got = 0, v;
    if (le < 0)
        return 0;
    if (!s->model[0] && wf_dk_str(t, tn, le, i0, 0x0110, s->model, sizeof s->model))
        got |= WF_DKS_MODEL;
    if (!s->orient && (v = wf_dk_int(t, tn, le, i0, 0x0112)) >= 1 && v <= 8) {
        s->orient = v;
        got |= WF_DKS_ORIENT;
    }
    if (!wf_dk_find(t, tn, le, i0, 0x8769, e) || (e[0] != 4 && e[0] != 13))
        return got;
    uint32_t ex = e[2];
    if (!s->lens[0] && wf_dk_str(t, tn, le, ex, 0xA434, s->lens, sizeof s->lens))
        got |= WF_DKS_LENS;
    if (!s->fl[1] && wf_dk_rat(t, tn, le, ex, 0x920A, r) && r[0]) {
        s->fl[0] = r[0]; s->fl[1] = r[1];
        got |= WF_DKS_FL;
    }
    if (!s->fn[1] && wf_dk_rat(t, tn, le, ex, 0x829D, r) && r[0]) {
        s->fn[0] = r[0]; s->fn[1] = r[1];
        got |= WF_DKS_FN;
    }
    if (!s->et[1] && wf_dk_rat(t, tn, le, ex, 0x829A, r) && r[0]) {
        s->et[0] = r[0]; s->et[1] = r[1];
        got |= WF_DKS_ET;
    }
    if (!s->iso && (v = wf_dk_int(t, tn, le, ex, 0x8827)) > 0) {
        s->iso = v;
        got |= WF_DKS_ISO;
    }
    char d[20];
    if (!s->dt[0] && wf_dk_str(t, tn, le, ex, 0x9003, d, sizeof d) && d[4] == ':' && d[7] == ':') {
        for (int k = 0; k < 20; k++)
            s->dt[k] = d[k];
        got |= WF_DKS_DATE;
    }
    return got;
}

void wf_dk_shot_gaps(struct wf_dk_shot *s, const struct wf_dk_shot *c)
{
    int k;
    if (!s->model[0])
        for (k = 0; k < (int)sizeof s->model; k++) s->model[k] = c->model[k];
    if (!s->lens[0])
        for (k = 0; k < (int)sizeof s->lens; k++) s->lens[k] = c->lens[k];
    if (!s->fl[1]) { s->fl[0] = c->fl[0]; s->fl[1] = c->fl[1]; }
    if (!s->fn[1]) { s->fn[0] = c->fn[0]; s->fn[1] = c->fn[1]; }
    if (!s->et[1]) { s->et[0] = c->et[0]; s->et[1] = c->et[1]; }
    if (!s->iso) s->iso = c->iso;
    if (!s->dt[0])
        for (k = 0; k < (int)sizeof s->dt; k++) s->dt[k] = c->dt[k];
}

int wf_dk_dims(uint8_t *app1, uint32_t len, int w, int h)
{
    if (len < 18 || w < 1 || h < 1)
        return -1;
    uint8_t *t = app1 + 10;
    uint32_t tn = len - 10, i0, e[3];
    int le = wf_dk_tiff(t, tn, &i0), ok = 0;
    if (le < 0 || !wf_dk_find(t, tn, le, i0, 0x8769, e))
        return -1;
    for (int k = 0; k < 2; k++) {
        uint32_t o = wf_dk_entry(t, tn, le, e[2], k ? 0xA003 : 0xA002), v = (uint32_t)(k ? h : w);
        if (!o)
            continue;
        uint32_t ty = wf_dk_u(t + o + 2, 2, le);
        uint8_t *p = t + o + 8;
        if (ty == 3 && v <= 0xFFFF) {
            p[0] = (uint8_t)(le ? v : v >> 8);
            p[1] = (uint8_t)(le ? v >> 8 : v);
            ok++;
        } else if (ty == 4) {
            for (int i = 0; i < 4; i++)
                p[i] = (uint8_t)(v >> (le ? 8 * i : 24 - 8 * i));
            ok++;
        }
    }
    return ok == 2 ? 0 : -1;
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

/* ------------------------------------------------------------------ the Exif thumbnail */
static void wf_dk_put32(uint8_t *p, uint32_t v, int le)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (le ? 8 * i : 24 - 8 * i));
}

/* IFD1's 0x0201 / 0x0202 entries (offsets in t) and values; 0 = not there */
static int wf_dk_ifd1(const uint8_t *t, uint32_t tn, int *le, uint32_t *eo, uint32_t *el, uint32_t *off, uint32_t *n)
{
    uint32_t i0;
    if ((*le = wf_dk_tiff(t, tn, &i0)) < 0 || i0 < 8 || i0 + 2 > tn)
        return 0;
    uint32_t c0 = wf_dk_u(t + i0, 2, *le), nx = i0 + 2 + c0 * 12;
    if (c0 > 256 || nx + 4 > tn)
        return 0;
    uint32_t i1 = wf_dk_u(t + nx, 4, *le);
    *eo = wf_dk_entry(t, tn, *le, i1, 0x0201);
    *el = wf_dk_entry(t, tn, *le, i1, 0x0202);
    if (!*eo || !*el)
        return 0;
    *off = wf_dk_u(t + *eo + 8, 4, *le);
    *n = wf_dk_u(t + *el + 8, 4, *le);
    return *off >= 8 && *off < tn && *n <= tn - *off;
}

int wf_dk_xthumb_slot(const uint8_t *app1, uint32_t len, uint32_t *off, uint32_t *room)
{
    if (len < 18)
        return -1;
    const uint8_t *t = app1 + 10;
    uint32_t tn = len - 10, eo, el, o, n;
    int le;
    if (!wf_dk_ifd1(t, tn, &le, &eo, &el, &o, &n))
        return -1;
    uint32_t e = o + n, nz = 0;
    for (uint32_t i = e; i < tn; i++)
        nz += t[i] != 0;
    *off = 10 + o;
    *room = tn - e <= 3 || !nz ? tn - o : n;
    return 0;
}

int wf_dk_xthumb_put(uint8_t *app1, uint32_t len, const uint8_t *jpg, uint32_t n)
{
    uint32_t off, room, eo, el, o, on;
    int le;
    if (n < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8 || wf_dk_xthumb_slot(app1, len, &off, &room) != 0)
        return -1;
    uint32_t skip = 0;                           /* our encoder's JFIF APP0: not in an Exif thumbnail */
    if (n >= 6 && jpg[2] == 0xFF && jpg[3] == 0xE0)
        skip = 2 + ((uint32_t)jpg[4] << 8 | jpg[5]);
    if (skip >= n - 2)
        return -1;
    uint32_t m = n - skip;
    if (m > room)
        return -1;
    uint8_t *t = app1 + 10;
    wf_dk_ifd1(t, len - 10, &le, &eo, &el, &o, &on);
    uint8_t *d = app1 + off;
    d[0] = 0xFF;
    d[1] = 0xD8;
    for (uint32_t i = 2; i < m; i++)
        d[i] = jpg[skip + i];
    for (uint32_t i = m; i < room; i++)
        d[i] = 0;
    wf_dk_put32(t + el + 8, m, le);
    return 0;
}

void wf_dk_xth_begin(struct wf_dk_xth *t, int cw, int ch, uint32_t *out)
{
    t->cw = cw;
    t->ch = ch;
    if ((uint32_t)cw * WF_DK_XH >= (uint32_t)ch * WF_DK_XW) {   /* wide: bars top and bottom */
        t->tw = WF_DK_XW;
        t->th = (ch * WF_DK_XW + cw / 2) / cw;
    } else {
        t->th = WF_DK_XH;
        t->tw = ((cw * WF_DK_XH + ch / 2) / ch) & ~1;
    }
    if (t->th < 1) t->th = 1;
    if (t->tw < 2) t->tw = 2;
    t->ox = ((WF_DK_XW - t->tw) / 2) & ~1;
    t->oy = (WF_DK_XH - t->th) / 2;
    for (int j = 0; j <= t->tw; j++)             /* whole words per column */
        t->xb[j] = (uint16_t)((j * (cw >> 1)) / t->tw);
    t->row = 0;
    t->y1 = ch / t->th;
    t->n = 0;
    for (int j = 0; j < WF_DK_XW; j++)
        t->sy[j] = t->su[j] = t->sv[j] = 0;
    for (int i = 0; i < WF_DK_XW / 2 * WF_DK_XH; i++)
        out[i] = 0;                              /* black: Y 0, U = V = 0 */
}

void wf_dk_xth_rows(struct wf_dk_xth *t, const uint32_t *rows, int pitch, int y, int n, uint32_t *out)
{
    for (int r = 0; r < n; r++, y++) {
        if (t->row >= t->th)
            return;
        if (!(y & 1)) {
            const uint32_t *wd = (const uint32_t *)((const uint8_t *)rows + r * pitch);
            for (int j = 0; j < t->tw; j++) {
                int32_t a = 0, u = 0, v = 0;
                for (int x = t->xb[j]; x < t->xb[j + 1]; x++) {
                    uint32_t q = wd[x];
                    a += (int32_t)((q >> 8) & 0xFF) + (int32_t)(q >> 24);
                    u += (int8_t)(q & 0xFF);
                    v += (int8_t)((q >> 16) & 0xFF);
                }
                t->sy[j] += a;
                t->su[j] += u;
                t->sv[j] += v;
            }
            t->n++;
        }
        if (y + 1 >= t->y1) {                    /* the thumbnail row is complete */
            uint32_t *o = out + (t->oy + t->row) * (WF_DK_XW / 2) + t->ox / 2;
            for (int j = 0; j + 1 < t->tw; j += 2) {
                int c0 = (t->xb[j + 1] - t->xb[j]) * t->n, c1 = (t->xb[j + 2] - t->xb[j + 1]) * t->n, cc = c0 + c1;
                int y0 = c0 ? (t->sy[j] + c0) / (2 * c0) : 0, y1 = c1 ? (t->sy[j + 1] + c1) / (2 * c1) : 0;
                int u = cc ? (t->su[j] + t->su[j + 1]) / cc : 0, v = cc ? (t->sv[j] + t->sv[j + 1]) / cc : 0;
                o[j >> 1] = (uint32_t)(u & 0xFF) | (uint32_t)(y0 > 255 ? 255 : y0) << 8 | (uint32_t)(v & 0xFF) << 16
                          | (uint32_t)(y1 > 255 ? 255 : y1) << 24;
            }
            for (int j = 0; j < t->tw; j++)
                t->sy[j] = t->su[j] = t->sv[j] = 0;
            t->n = 0;
            t->row++;
            t->y1 = (int)(((t->row + 1) * t->ch) / t->th);
        }
    }
}

int wf_dk_xth_done(const struct wf_dk_xth *t) { return t->row >= t->th; }

/* ------------------------------------------------------------------ the Prints screens' picture (M5) */
void wf_dk_vth_begin(struct wf_dk_vth *t, int cw, int ch, uint32_t *out)
{
    t->cw = cw;
    t->ch = ch;
    if ((uint32_t)cw * WF_DK_VH >= (uint32_t)ch * WF_DK_VW) {   /* wide: bars top and bottom */
        t->tw = WF_DK_VW;
        t->th = (ch * WF_DK_VW + cw / 2) / cw;
    } else {
        t->th = WF_DK_VH;
        t->tw = ((cw * WF_DK_VH + ch / 2) / ch) & ~1;
    }
    if (t->th < 1) t->th = 1;
    if (t->th > WF_DK_VH) t->th = WF_DK_VH;
    if (t->tw < 2) t->tw = 2;
    t->ox = ((WF_DK_VW - t->tw) / 2) & ~1;
    t->oy = (WF_DK_VH - t->th) / 2;
    for (int j = 0; j <= t->tw; j++)
        t->xb[j] = (uint16_t)((j * (cw >> 1)) / t->tw);
    t->row = 0;
    t->y1 = ch / t->th;
    t->n = 0;
    for (int j = 0; j < WF_DK_VW; j++)
        t->sy[j] = t->su[j] = t->sv[j] = 0;
    for (int i = 0; i < WF_DK_VW / 2 * WF_DK_VH; i++)
        out[i] = 0;
}

void wf_dk_vth_rows(struct wf_dk_vth *t, const uint32_t *rows, int pitch, int y, int n, uint32_t *out)
{
    for (int r = 0; r < n; r++, y++) {
        if (t->row >= t->th)
            return;
        if (!(y & 1)) {
            const uint32_t *wd = (const uint32_t *)((const uint8_t *)rows + r * pitch);
            for (int j = 0; j < t->tw; j++) {
                int32_t a = 0, u = 0, v = 0;
                for (int x = t->xb[j]; x < t->xb[j + 1]; x++) {
                    uint32_t q = wd[x];
                    a += (int32_t)((q >> 8) & 0xFF) + (int32_t)(q >> 24);
                    u += (int8_t)(q & 0xFF);
                    v += (int8_t)((q >> 16) & 0xFF);
                }
                t->sy[j] += a;
                t->su[j] += u;
                t->sv[j] += v;
            }
            t->n++;
        }
        if (y + 1 >= t->y1) {
            uint32_t *o = out + (t->oy + t->row) * (WF_DK_VW / 2) + t->ox / 2;
            for (int j = 0; j + 1 < t->tw; j += 2) {
                int c0 = (t->xb[j + 1] - t->xb[j]) * t->n, c1 = (t->xb[j + 2] - t->xb[j + 1]) * t->n, cc = c0 + c1;
                int y0 = c0 ? (t->sy[j] + c0) / (2 * c0) : 0, y1 = c1 ? (t->sy[j + 1] + c1) / (2 * c1) : 0;
                int u = cc ? (t->su[j] + t->su[j + 1]) / cc : 0, v = cc ? (t->sv[j] + t->sv[j + 1]) / cc : 0;
                o[j >> 1] = (uint32_t)(u & 0xFF) | (uint32_t)(y0 > 255 ? 255 : y0) << 8 | (uint32_t)(v & 0xFF) << 16
                          | (uint32_t)(y1 > 255 ? 255 : y1) << 24;
            }
            for (int j = 0; j < t->tw; j++)
                t->sy[j] = t->su[j] = t->sv[j] = 0;
            t->n = 0;
            t->row++;
            t->y1 = (int)(((t->row + 1) * t->ch) / t->th);
        }
    }
}

int wf_dk_vth_done(const struct wf_dk_vth *t) { return t->row >= t->th; }

void wf_dk_vth_sheet(const uint32_t *pic, uint32_t *sheet)
{
    for (int y = 0; y < WF_DK_VSH; y++)
        for (int x = 0; x < WF_DK_VSW / 2; x++) {   /* one sheet word = 2 x 4 picture words (8 x 4 pixels) */
            int32_t ya = 0, yb = 0, u = 0, v = 0;
            for (int j = 0; j < 4; j++) {
                const uint32_t *w = pic + (y * 4 + j) * (WF_DK_VW / 2) + x * 4;
                for (int i = 0; i < 4; i++) {
                    uint32_t q = w[i];
                    int s = ((q >> 8) & 0xFF) + (q >> 24);
                    if (i < 2) ya += s; else yb += s;
                    u += (int8_t)(q & 0xFF);
                    v += (int8_t)((q >> 16) & 0xFF);
                }
            }
            u /= 16;
            v /= 16;
            sheet[y * (WF_DK_VSW / 2) + x] = (uint32_t)(u & 0xFF) | (uint32_t)((ya + 8) / 16) << 8
                                           | (uint32_t)(v & 0xFF) << 16 | (uint32_t)((yb + 8) / 16) << 24;
        }
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
