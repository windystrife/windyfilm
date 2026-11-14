/* wf_cans.c - WindyFilm browser: the can pack ML/DATA/WFCANS.DAT (DESIGN 4.4, written by tools/mk_wfcans.py).
 *
 * Kept in memory while the browser is open: the header, the palette (npal <= 128 colours) and the index. Images
 * are read on demand through wf_card_read() into a small LRU cache of RLE blobs and decoded straight into the
 * canvas. The pack palette index i goes to palette entry C_CAN0 + i; when the pack has more colours than the UI has
 * hardware entries left (WFUI_CAN_SLOTS), the extra ones are mapped to their nearest placed colour.
 * Image offsets: absolute file offsets, or relative to the data section (detected: the smallest offset is below the
 * data offset only when they are relative).
 * No pack, a bad pack or a film without a can: a plain cassette in the film's group colour. */
#include "wf_ui_int.h"

#define CANS_PATH       "ML/DATA/WFCANS.DAT"
#define CACHE_N         24
#define CACHE_BYTES     (96 * 1024)         /* working set: big + selected + 10 strip cans, about 50 KB */
#define MAX_BLOB        (48 * 1024)

const uint8_t wfc_size[CAN_NSIZES][2] = { { 160, 226 }, { 52, 73 }, { 58, 82 } };

static struct {
    int st;
    int count, npal, used;
    uint32_t size, data_off;
    int rel;
    uint8_t *idx;                       /* count x 32 B */
    uint8_t remap[128];                 /* pack colour -> palette offset */
} P;

struct blob { int16_t e; int8_t s; uint8_t ok; uint32_t n, age; uint8_t *p; };
static struct blob cache[CACHE_N];
static uint32_t cache_bytes, cache_clock;

/* plain can colours, placed at C_CAN0 when there is no pack */
enum { G_CAP = 0, G_CAPHI, G_CAPLO, G_LABEL, G_BODY0 };
static const uint8_t group_rgb[WF_NGROUPS][3] = {
    { 214, 150, 60 },                   /* Colour: amber */
    { 182, 104, 66 },                   /* Cine: rust */
    { 150, 153, 160 },                  /* B&W: grey */
    { 38, 156, 148 },                   /* Film LUT: teal */
};

static inline unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void place_plain_colours(void)
{
    wfp_set_rgb(C_CAN0 + G_CAP, 54, 56, 62);
    wfp_set_rgb(C_CAN0 + G_CAPHI, 112, 116, 124);
    wfp_set_rgb(C_CAN0 + G_CAPLO, 26, 27, 31);
    wfp_set_rgb(C_CAN0 + G_LABEL, 240, 241, 243);
    for (int g = 0; g < WF_NGROUPS; g++)
    {
        const uint8_t *c = group_rgb[g];
        int e = C_CAN0 + G_BODY0 + g * 3;
        wfp_set_rgb(e, c[0], c[1], c[2]);
        wfp_set_rgb(e + 1, c[0] * 55 / 100, c[1] * 55 / 100, c[2] * 55 / 100);
        int hi[3];
        for (int k = 0; k < 3; k++) hi[k] = c[k] + (255 - c[k]) * 35 / 100;
        wfp_set_rgb(e + 2, hi[0], hi[1], hi[2]);
    }
}

static int load(void)
{
    uint8_t h[64];
    uint32_t size = 0;
    if (wf_card_read(CANS_PATH, 0, h, 64, &size)) return WFC_E_NOFILE;
    if (memcmp(h, "WFCN", 4) || rd16(h + 4) != 1) return WFC_E_HEADER;
    int count = rd16(h + 6), npal = rd16(h + 8), nsizes = rd16(h + 10);
    if (count < 1 || count > 255 || npal < 1 || npal > 128 || nsizes != CAN_NSIZES) return WFC_E_HEADER;
    for (int s = 0; s < CAN_NSIZES; s++)
        if (rd16(h + 12 + 4 * s) != wfc_size[s][0] || rd16(h + 14 + 4 * s) != wfc_size[s][1]) return WFC_E_SIZES;
    uint32_t pal_off = rd32(h + 24), idx_off = rd32(h + 28), data_off = rd32(h + 32);
    if (pal_off < 64 || idx_off < 64 || data_off < 64) return WFC_E_HEADER;
    if (size && (pal_off + npal * 4 > size || idx_off + count * 32 > size || data_off > size)) return WFC_E_HEADER;

    uint8_t pal[128 * 4];
    P.idx = malloc(count * 32);
    if (!P.idx) return WFC_E_MEM;
    if (wf_card_read(CANS_PATH, pal_off, pal, npal * 4, 0) || wf_card_read(CANS_PATH, idx_off, P.idx, count * 32, 0))
        return WFC_E_READ;

    uint32_t mn = 0xFFFFFFFFu;
    for (int e = 0; e < count; e++)
        for (int s = 0; s < CAN_NSIZES; s++)
        {
            uint32_t o = rd32(P.idx + e * 32 + 8 + 4 * s);
            if (rd32(P.idx + e * 32 + 20 + 4 * s) && o < mn) mn = o;
        }
    P.rel = mn < data_off;
    P.count = count;
    P.npal = npal;
    P.size = size;
    P.data_off = data_off;

    P.used = npal < WFUI_CAN_SLOTS ? npal : WFUI_CAN_SLOTS;
    for (int i = 0; i < P.used; i++)
    {
        wfp_set_rgb(C_CAN0 + i, pal[4 * i], pal[4 * i + 1], pal[4 * i + 2]);
        P.remap[i] = C_CAN0 + i;
    }
    for (int i = P.used; i < npal; i++)
    {
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < P.used; j++)
        {
            int dr = pal[4 * i] - pal[4 * j], dg = pal[4 * i + 1] - pal[4 * j + 1], db = pal[4 * i + 2] - pal[4 * j + 2];
            int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
            if (d < bd) { bd = d; best = j; }
        }
        P.remap[i] = C_CAN0 + best;
    }
    return WFC_OK;
}

int wfc_open(void)
{
    memset(&P, 0, sizeof(P));
    P.st = load();
    if (P.st)
    {
        if (P.idx) free(P.idx);
        P.idx = 0;
        P.count = 0;
        place_plain_colours();
    }
    return P.st;
}

void wfc_close(void)
{
    for (int i = 0; i < CACHE_N; i++)
    {
        if (cache[i].p) free(cache[i].p);
        cache[i].p = 0;
    }
    cache_bytes = 0;
    if (P.idx) free(P.idx);
    P.idx = 0;
    P.count = 0;
}

int wfc_status(void) { return P.st; }
const char *wfc_why(int st)
{
    static const char *const n[] = { "ok", "not on the card", "bad header", "other can sizes", "no memory",
                                     "read failed" };
    return st >= 0 && st < (int)(sizeof n / sizeof n[0]) ? n[st] : "?";
}
int wfc_pal_used(void) { return P.st ? 0 : P.used; }

static int entry_of(const struct wf_film *f)
{
    if (P.st || !f) return -1;
    if (f->can < P.count && P.idx[f->can * 32] == f->id) return f->can;
    if (f->can == 255) return -1;
    for (int e = 0; e < P.count; e++)
        if (P.idx[e * 32] == f->id) return e;
    return -1;
}

int wfc_glow(const struct wf_film *f, uint8_t rgb[3])
{
    int e = entry_of(f);
    if (e >= 0)
    {
        memcpy(rgb, P.idx + e * 32 + 4, 3);
        return 1;
    }
    memcpy(rgb, group_rgb[f ? f->group & 3 : 0], 3);
    return 0;
}

/* walk one RLE image; draw = 0 only validates */
static int rle(const uint8_t *p, uint32_t n, int w, int h, int x, int y, int draw)
{
    int total = w * h, i = 0, col = 0, row = 0;
    uint32_t k = 0;
    while (i < total)
    {
        if (k >= n) return -1;
        int c = p[k++], run = 1, v;
        if (c < 0x80) v = c;
        else if (c == 0xFF) v = 0xFF;
        else
        {
            if (k >= n) return -1;
            run = c - 0x7E;
            v = p[k++];
        }
        if (v != 0xFF && v >= P.npal) return -1;
        if (i + run > total) return -1;
        i += run;
        if (!draw || v == 0xFF)
        {
            col += run;
            while (col >= w) { col -= w; row++; }
            continue;
        }
        uint8_t px = PX(P.remap[v]);
        while (run)
        {
            int seg = w - col < run ? w - col : run;
            int yy = y + row;
            if (yy >= 0 && yy < WFUI_H)
            {
                int x0 = x + col, x1 = x0 + seg;
                if (x0 < 0) x0 = 0;
                if (x1 > WFUI_W) x1 = WFUI_W;
                if (x1 > x0) memset(wfg_cv + yy * wfg_pitch + x0, px, x1 - x0);
            }
            run -= seg;
            col += seg;
            if (col >= w) { col = 0; row++; }
        }
    }
    return 0;
}

static struct blob *fetch(int e, int s)
{
    cache_clock++;
    for (int i = 0; i < CACHE_N; i++)
        if (cache[i].p && cache[i].e == e && cache[i].s == s)
        {
            cache[i].age = cache_clock;
            return cache[i].ok ? &cache[i] : 0;
        }
    const uint8_t *ie = P.idx + e * 32;
    uint32_t off = rd32(ie + 8 + 4 * s), n = rd32(ie + 20 + 4 * s);
    if (!n || n > MAX_BLOB) return 0;
    if (P.rel) off += P.data_off;
    if (P.size && off + n > P.size) return 0;
    /* make room: a free slot and the byte budget */
    for (;;)
    {
        int fr = -1, old = -1;
        for (int i = 0; i < CACHE_N; i++)
        {
            if (!cache[i].p) { if (fr < 0) fr = i; }
            else if (old < 0 || cache[i].age < cache[old].age) old = i;
        }
        if (fr >= 0 && cache_bytes + n <= CACHE_BYTES) break;
        if (old < 0) break;
        cache_bytes -= cache[old].n;
        free(cache[old].p);
        cache[old].p = 0;
    }
    int slot = -1;
    for (int i = 0; i < CACHE_N; i++)
        if (!cache[i].p) { slot = i; break; }
    if (slot < 0) return 0;
    uint8_t *p = malloc(n);
    if (!p) return 0;
    struct blob *b = &cache[slot];
    b->p = p;
    b->n = n;
    b->e = e;
    b->s = s;
    b->age = cache_clock;
    cache_bytes += n;
    b->ok = wf_card_read(CANS_PATH, off, p, n, 0) == 0 && rle(p, n, wfc_size[s][0], wfc_size[s][1], 0, 0, 0) == 0;
    return b->ok ? b : 0;
}

static void plain_can(const struct wf_film *f, int s, int x, int y)
{
    int w = wfc_size[s][0], h = wfc_size[s][1];
    int g = f ? (f->group & 3) : 0;
    int body = C_CAN0 + G_BODY0 + g * 3;
    int cap = h * 9 / 100, in = w * 8 / 100, knob = cap / 2;
    if (cap < 4) cap = 4;
    if (in < 2) in = 2;
    if (knob < 2) knob = 2;
    int top = y + knob, bot = y + h - 1 - knob;
    wfg_rect(x + w * 3 / 8, y, x + w - 1 - w * 3 / 8, top, C_CAN0 + G_CAPHI);              /* spool knob */
    wfg_rect(x + w * 2 / 5, bot, x + w - 1 - w * 2 / 5, y + h - 1, C_CAN0 + G_CAPLO);        /* stub */
    wfg_rect(x + in, top + cap, x + w - 1 - in, bot - cap, body);                            /* body */
    int bw = w - 2 * in;
    wfg_rect(x + in, top + cap, x + in + bw / 6, bot - cap, body + 1);                       /* lip band */
    wfg_rect(x + in + bw * 35 / 100, top + cap, x + in + bw * 35 / 100 + bw / 12, bot - cap, body + 2);
    wfg_rrect(x, top, x + w - 1, top + cap, cap / 3, C_CAN0 + G_CAP, -1, 0);                 /* caps */
    wfg_rrect(x, bot - cap, x + w - 1, bot, cap / 3, C_CAN0 + G_CAP, -1, 0);
    wfg_rect(x + 2, top + 1, x + w - 3, top + 1, C_CAN0 + G_CAPHI);
    wfg_rect(x + 2, bot - cap + 1, x + w - 3, bot - cap + 1, C_CAN0 + G_CAPHI);
    if (s == CAN_BIG && f)
    {
        const char *nm = f->shortname ? f->shortname : f->code;
        int lx = x + in + bw / 6 + 4, lw = x + w - 1 - in - 4 - lx;
        int tw = ui_textw(F_15, nm);
        ui_text_clip(F_15, lx + (tw < lw ? (lw - tw) / 2 : 0), y + h / 2 - 16, nm, C_CAN0 + G_LABEL, lx + lw);
        char iso[12];
        snprintf(iso, sizeof(iso), "ISO %d", f->iso);
        tw = ui_textw(F_15, iso);
        ui_text(F_15, lx + (lw - tw) / 2, y + h / 2 + 4, iso, C_CAN0 + G_LABEL);
    }
}

void wfc_draw(const struct wf_film *f, int s, int x, int y, int bg)
{
    (void)bg;
    int e = entry_of(f);
    struct blob *b = e >= 0 ? fetch(e, s) : 0;
    if (b) rle(b->p, b->n, wfc_size[s][0], wfc_size[s][1], x, y, 1);
    else plain_can(f, s, x, y);
}
