/* wf_font.c - WindyFilm browser text.
 *
 * Small text (13 / 15 px): our own 1-bit font pack ML/DATA/WFFONTS.DAT (tools/mk_wffont.py, Ubuntu Sans, format in
 * that tool's header), drawn by this file: it needs none of ML's dynamic font slots.
 * Title and film name: ML's own argnor23 / argnor28, looked up with font_by_name (ML loads both at boot) and drawn
 * here from ML's loaded RBF data (font_dynamic[slot].bitmap -> rbf_font.h `font`), straight into our canvas. */
#include "wf_ui_int.h"

#define WFF_PATH "ML/DATA/WFFONTS.DAT"

struct wff {
    const uint8_t *g;                   /* glyph table, count x 12 B */
    const uint8_t *k;                   /* kern table, nk x 4 B, sorted (left, right) */
    int nk;
    int h, asc, first, count;
    uint16_t kidx[97];                  /* first kern pair whose left char is first + i */
};

static uint8_t *wff_buf;
static uint32_t wff_len;
static struct wff wff[3];
static int wff_ok[3];
static font *rbf[3];                    /* argnor23, argnor28, term12 (fallback for the small text) */
static int fstat, fwhy;                         /* fwhy: why our pack is not used (WFF_W_*) */
enum { WFF_W_OK, WFF_W_FILE, WFF_W_HEADER, WFF_W_MEM, WFF_W_READ, WFF_W_PARSE, WFF_W_BUSY, WFF_W_N };
static const char *const fwhy_names[WFF_W_N] = { "ok", "not on the card", "bad header", "no memory", "read failed",
                                                 "bad font table", "card busy" };

/* a failed pack read (WF_RD_*) in WFF_W_* words */
static int why_of(int rc)
{
    return rc == WF_RD_MISSING ? WFF_W_FILE : rc == WF_RD_BUSY ? WFF_W_BUSY : rc == WF_RD_NOMEM ? WFF_W_MEM
         : rc == WF_RD_SHORT ? WFF_W_HEADER : WFF_W_READ;
}

static inline unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--) h = (h ^ *p++) * 0x01000193u;
    return h;
}

static font *rbf_lookup(const char *name)
{
    uint32_t spec = font_by_name((char *)name, COLOR_WHITE, COLOR_BLACK);
    int id = FONT_ID(spec);
    if (id >= MAX_DYN_FONTS) return 0;              /* FONT_CANON: not an RBF font */
    font *f = (font *)font_dynamic[id].bitmap;
    if (!f || !f->cTable || f->hdr.height <= 0 || f->hdr.height > 64 || f->hdr.charSize <= 0) return 0;
    if (f->hdr.charFirst < 0 || f->hdr.charLast > 255 || f->hdr.charLast < f->hdr.charFirst) return 0;
    return f;
}

static int wff_parse(void)
{
    const uint8_t *b = wff_buf;
    if (wff_len < 16 || memcmp(b, "WFFN", 4) || rd16(b + 4) != 1) return -1;
    int nf = rd16(b + 6);
    if (rd32(b + 8) != wff_len || nf < 1 || 16 + nf * 24 > (int)wff_len) return -1;
    if (fnv1a(b + 16, wff_len - 16) != rd32(b + 12)) return -1;
    for (int i = 0; i < nf; i++)
    {
        const uint8_t *d = b + 16 + i * 24;
        int px = d[0], wd = d[1];
        int slot = px == 15 && wd == 100 ? 0 : px == 13 && wd == 100 ? 1 : px == 13 && wd == 82 ? 2 : -1;
        if (slot < 0) continue;
        struct wff *f = &wff[slot];
        f->h = d[4];
        f->asc = d[5];
        f->first = d[6];
        f->count = d[7];
        uint32_t go = rd32(d + 8), ko = rd32(d + 12);
        f->nk = rd16(d + 16);
        if (f->count < 1 || f->count > 96 || go + f->count * 12 > wff_len || ko + f->nk * 4 > wff_len) continue;
        f->g = b + go;
        f->k = b + ko;
        int ok = 1;
        for (int c = 0; c < f->count; c++)
        {
            const uint8_t *g = f->g + c * 12;
            uint32_t bo = rd32(g + 8);
            if (bo && bo + g[5] * g[6] > wff_len) ok = 0;
            if (g[6] * 8 < g[4]) ok = 0;
        }
        if (!ok) continue;
        int j = 0;
        for (int c = 0; c <= f->count; c++)
        {
            while (j < f->nk && f->k[j * 4] < f->first + c) j++;
            f->kidx[c] = j;
        }
        wff_ok[slot] = 1;
    }
    return 0;
}

/* also again while open (the UI task tries a pack that Canon's card kept from it again): starts from nothing */
int wff_open(void)
{
    wff_close();
    fstat = 0;
    rbf[0] = rbf_lookup("argnor23");
    rbf[1] = rbf_lookup("argnor28");
    rbf[2] = 0;
    if (!rbf[0]) fstat |= 2;
    if (!rbf[1]) fstat |= 4;

    uint8_t hdr[16];
    uint32_t size = 0;
    int rc = wfui_read(WFF_PATH, 0, hdr, 16, &size);
    fwhy = why_of(rc);
    if (rc == WF_RD_OK)
    {
        uint32_t n = rd32(hdr + 8);
        fwhy = WFF_W_HEADER;
        if (!memcmp(hdr, "WFFN", 4) && n >= 16 && n < 256 * 1024 && (!size || size == n))
        {
            fwhy = WFF_W_MEM;
            wff_buf = malloc(n);
            if (wff_buf)
            {
                rc = wfui_read(WFF_PATH, 0, wff_buf, n, &size);
                fwhy = why_of(rc);
                if (rc == WF_RD_OK)
                {
                    wff_len = n;
                    fwhy = wff_parse() ? WFF_W_PARSE : WFF_W_OK;
                }
            }
        }
    }
    if (!wff_ok[0] || !wff_ok[1] || !wff_ok[2])
    {
        fstat |= 1;
        if (fwhy == WFF_W_OK)
            fwhy = WFF_W_PARSE;                     /* a size slot missing or bad */
        if (!wff_ok[0] && !wff_ok[1] && !wff_ok[2])
        {
            if (wff_buf) free(wff_buf);             /* nothing usable in it: no buffer held for nothing */
            wff_buf = 0;
            wff_len = 0;
        }
        rbf[2] = rbf_lookup("term12");              /* 0 when ML has none: that text is left out, nothing else */
    }
    return fstat;
}
const char *wff_why(void) { return fwhy >= 0 && fwhy < WFF_W_N ? fwhy_names[fwhy] : "?"; }
int wff_again(void) { return (fstat & 1) && (fwhy == WFF_W_BUSY || fwhy == WFF_W_READ || fwhy == WFF_W_MEM); }

void wff_close(void)
{
    if (wff_buf) free(wff_buf);
    wff_buf = 0;
    wff_len = 0;
    for (int i = 0; i < 3; i++) wff_ok[i] = 0;
}

int wff_status(void) { return fstat; }

/* ------------------------------------------------------------------ drawing */
static int kern(const struct wff *f, int a, int b)
{
    int i = f->kidx[a - f->first], e = f->kidx[a - f->first + 1];
    for (; i < e; i++)
    {
        int r = f->k[i * 4 + 1];
        if (r == b) return (int16_t)rd16(f->k + i * 4 + 2);
        if (r > b) break;
    }
    return 0;
}

static int wff_run(const struct wff *f, int x, int y, const char *s, int c, int xmax, int draw)
{
    int pen = 0, prev = -1;
    uint8_t col = PX(c);
    for (; *s; s++)
    {
        int o = (unsigned char)*s;
        if (o < f->first || o >= f->first + f->count) o = '?';
        if (prev >= 0) pen += kern(f, prev, o);
        const uint8_t *g = f->g + (o - f->first) * 12;
        int gx = x + ((pen + 32) >> 6) + (int8_t)g[2];
        int gy = y + (int8_t)g[3];
        int w = g[4], h = g[5], bpr = g[6];
        if (xmax && x + ((pen + (int)rd16(g) + 32) >> 6) > xmax) break;
        if (draw && w)
        {
            const uint8_t *bits = wff_buf + rd32(g + 8);
            for (int j = 0; j < h; j++)
            {
                int yy = gy + j;
                if (yy < 0 || yy >= WFUI_H) continue;
                uint8_t *row = wfg_cv + yy * wfg_pitch;
                const uint8_t *br = bits + j * bpr;
                for (int i = 0; i < w; i++)
                {
                    int xx = gx + i;
                    if ((br[i >> 3] & (0x80 >> (i & 7))) && xx >= 0 && xx < WFUI_W) row[xx] = col;
                }
            }
        }
        pen += rd16(g);
        prev = o;
    }
    return (pen + 32) >> 6;
}

static int rbf_run(const font *f, int x, int y, const char *s, int c, int xmax, int draw)
{
    int x0 = x;
    uint8_t col = PX(c);
    int h = f->hdr.height, bw = f->width, cs = f->hdr.charSize;
    for (; *s; s++)
    {
        int ch = (unsigned char)*s;
        if (ch < f->hdr.charFirst || ch > f->hdr.charLast) continue;
        int pw = (unsigned char)f->wTable[ch];
        if (xmax && x + pw > xmax) break;
        if (draw)
        {
            const uint8_t *cd = (const uint8_t *)f->cTable + (ch - f->hdr.charFirst) * cs;
            for (int yy = 0; yy < h; yy++)
            {
                int py = y + yy;
                if (py < 0 || py >= WFUI_H) continue;
                uint8_t *row = wfg_cv + py * wfg_pitch;
                const uint8_t *cr = cd + yy * bw / 8;
                for (int xx = 0; xx < pw; xx++)
                {
                    int px = x + xx;
                    if ((cr[xx >> 3] & (1 << (xx & 7))) && px >= 0 && px < WFUI_W) row[px] = col;
                }
            }
        }
        x += pw;
    }
    return x - x0;
}

/* font -> (own pack slot, ML RBF) with fallbacks: ML title fonts -> own 15 px, own pack -> ML term12 */
static int run(int font, int x, int y, const char *s, int c, int xmax, int draw)
{
    if (!s) return 0;
    if (font == F_ML23 || font == F_ML28)
    {
        const struct _font *f = rbf[font == F_ML28];
        if (f) return rbf_run(f, x, y, s, c, xmax, draw);
        font = F_15;
    }
    int slot = font == F_15 ? 0 : font == F_13 ? 1 : 2;
    if (wff_ok[slot]) return wff_run(&wff[slot], x, y, s, c, xmax, draw);
    if (rbf[2]) return rbf_run(rbf[2], x, y, s, c, xmax, draw);
    return 0;
}

int ui_text(int font, int x, int y, const char *s, int c) { return run(font, x, y, s, c, 0, 1); }
int ui_textw(int font, const char *s) { return run(font, 0, 0, s, 0, 0, 0); }
int ui_text_clip(int font, int x, int y, const char *s, int c, int xmax) { return run(font, x, y, s, c, xmax, 1); }

int ui_texth(int font)
{
    if (font == F_ML23 || font == F_ML28)
    {
        const struct _font *f = rbf[font == F_ML28];
        if (f) return f->hdr.height;
        font = F_15;
    }
    int slot = font == F_15 ? 0 : font == F_13 ? 1 : 2;
    if (wff_ok[slot]) return wff[slot].h;
    return rbf[2] ? rbf[2]->hdr.height : 12;
}
