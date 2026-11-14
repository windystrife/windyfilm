/* wf_gfx.c - WindyFilm browser: canvas, drawing primitives, background (gradient, glow, floor shadow) and the
 * hardware palette (DIGIC 4 / 5 bitmap palette, ML's LCD_Palette table).
 *
 * Everything is drawn as palette offsets (PX(c) = WFUI_PAL_BASE + c) into Canon's idle bitmap buffer, then copied to
 * the front buffer (wfg_present). Soft things (gradient, glow, shadow, strip fades) use small quantised palette ramps
 * plus a 4 x 4 ordered dither between neighbouring ramp entries, so the glow of a film is recoloured by rewriting its
 * ramp entries only (wfp_set_glow), never by redrawing pixels. */
#include "wf_ui_int.h"

uint8_t *wfg_cv;
int wfg_pitch;

const uint8_t wfg_bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* ------------------------------------------------------------------ canvas */
int wfg_begin(void)
{
    uint8_t *p = bmp_vram_idle();
    if (!p) return -1;
    wfg_cv = p;
    wfg_pitch = BMPPITCH;
    return 0;
}

static int pal_dirty_any;
static void pal_write_dirty(void);

void wfg_present(void)
{
    BMP_LOCK(
        pal_write_dirty();
        bmp_idle_copy(1, 0);
    )
}

void wfg_clear_front(void)
{
    if (wfg_begin()) return;
    for (int y = 0; y < WFUI_H; y++) memset(wfg_cv + y * wfg_pitch, 0, WFUI_W);
    BMP_LOCK( bmp_idle_copy(1, 0); )
}

static inline void hspan(int x0, int x1, int y, int c)
{
    if (y < 0 || y >= WFUI_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > WFUI_W - 1) x1 = WFUI_W - 1;
    if (x1 < x0) return;
    memset(wfg_cv + y * wfg_pitch + x0, PX(c), x1 - x0 + 1);
}

void wfg_rect(int x0, int y0, int x1, int y1, int c)
{
    for (int y = y0; y <= y1; y++) hspan(x0, x1, y, c);
}

static int isqrt(int v)
{
    if (v <= 0) return 0;
    int r = 0, b = 1 << 14;
    while (b > v) b >>= 2;
    while (b)
    {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* span of a rounded rectangle on row y (PIL rounded_rectangle look: quarter circles of radius r + 0.5) */
static int rr_span(int x0, int y0, int x1, int y1, int r, int y, int *l, int *rt)
{
    if (y < y0 || y > y1) return 0;
    int dy = 0;
    if (y < y0 + r) dy = y0 + r - y;
    else if (y > y1 - r) dy = y - (y1 - r);
    int in = 0;
    if (dy)
    {
        int s = isqrt((2 * r + 1) * (2 * r + 1) - 4 * dy * dy) >> 1;
        in = r - s;
    }
    *l = x0 + in;
    *rt = x1 - in;
    return *l <= *rt;
}

void wfg_rrect(int x0, int y0, int x1, int y1, int r, int fill, int outline, int width)
{
    int mr = (x1 - x0 + 1) / 2, mh = (y1 - y0 + 1) / 2;
    if (r > mr) r = mr;
    if (r > mh) r = mh;
    if (r < 0) r = 0;
    if (outline < 0 || width <= 0)
    {
        for (int y = y0; y <= y1; y++)
        {
            int l, rt;
            if (rr_span(x0, y0, x1, y1, r, y, &l, &rt)) hspan(l, rt, y, fill);
        }
        return;
    }
    int ir = r - width;
    if (ir < 0) ir = 0;
    for (int y = y0; y <= y1; y++)
    {
        int l, rt, il, irt;
        if (!rr_span(x0, y0, x1, y1, r, y, &l, &rt)) continue;
        if (rr_span(x0 + width, y0 + width, x1 - width, y1 - width, ir, y, &il, &irt))
        {
            hspan(l, il - 1, y, outline);
            hspan(irt + 1, rt, y, outline);
            if (fill >= 0) hspan(il, irt, y, fill);
        }
        else
        {
            hspan(l, rt, y, outline);
        }
    }
}

void wfg_ellipse(int x0, int y0, int x1, int y1, int c)
{
    int a = x1 - x0 + 1, b = y1 - y0 + 1;           /* doubled radii */
    int cx = x0 + x1, cy = y0 + y1;                   /* doubled centre */
    int ab = a * a * b * b;
    for (int y = y0; y <= y1; y++)
    {
        int dy = 2 * y - cy;
        int l = 99999, rt = -99999;
        for (int x = x0; x <= x1; x++)
        {
            int dx = 2 * x - cx;
            if (dx * dx * b * b + dy * dy * a * a <= ab)
            {
                if (x < l) l = x;
                rt = x;
            }
        }
        if (rt >= l) hspan(l, rt, y, c);
    }
}

void wfg_poly(const int16_t *xy, int n, int c)
{
    int ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++)
    {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    if (ymax - ymin >= 64) return;
    int16_t lo[64], hi[64];
    for (int i = 0; i <= ymax - ymin; i++) { lo[i] = 32767; hi[i] = -32768; }
    for (int i = 0; i < n; i++)
    {
        int xa = xy[2 * i], ya = xy[2 * i + 1];
        int xb = xy[2 * ((i + 1) % n)], yb = xy[2 * ((i + 1) % n) + 1];
        if (ya > yb) { int t = xa; xa = xb; xb = t; t = ya; ya = yb; yb = t; }
        for (int y = ya; y <= yb; y++)
        {
            int x = (yb == ya) ? xa : xa + ((xb - xa) * (y - ya) * 2 + (yb - ya)) / (2 * (yb - ya));
            int k = y - ymin;
            if (x < lo[k]) lo[k] = x;
            if (x > hi[k]) hi[k] = x;
            if (yb == ya)
            {
                if (xb < lo[k]) lo[k] = xb;
                if (xb > hi[k]) hi[k] = xb;
            }
        }
    }
    for (int y = ymin; y <= ymax; y++)
    {
        int k = y - ymin;
        if (hi[k] >= lo[k]) hspan(lo[k], hi[k], y, c);
    }
}

void wfg_tri(int x0, int y0, int x1, int y1, int x2, int y2, int c)
{
    int16_t p[6] = { x0, y0, x1, y1, x2, y2 };
    wfg_poly(p, 3, c);
}

void wfg_bits16(const uint8_t *bits, int x, int y, int c)
{
    for (int j = 0; j < 16; j++)
    {
        int yy = y + j;
        if (yy < 0 || yy >= WFUI_H) continue;
        unsigned v = (bits[2 * j] << 8) | bits[2 * j + 1];
        uint8_t *row = wfg_cv + yy * wfg_pitch;
        for (int i = 0; i < 16; i++)
        {
            int xx = x + i;
            if ((v & (0x8000 >> i)) && xx >= 0 && xx < WFUI_W) row[xx] = PX(c);
        }
    }
}

/* ------------------------------------------------------------------ background: gradient, glow, floor shadow */
/* the glow and floor shadow sit under the big can: the browser's at (45, 40) (mockup glow(img, 125, 150, 150, 150,
   colour), shadow(img, 125, 272, 78, 8)); the roll picker moves them under its carousel centre (wfg_glow_centre) */
static int GLOW_CX = 125, GLOW_CY = 150, SH_CX = 125, SH_CY = 272;
#define GLOW_R  150                      /* circle */
void wfg_glow_centre(int cx, int cy, int shx, int shy)
{
    GLOW_CX = cx;
    GLOW_CY = cy;
    SH_CX = shx;
    SH_CY = shy;
}
#define SH_RX   78
#define SH_RY   8                        /* mockup shadow(img, 125, 272, 78, 8) */

/* (1 - r^2)^1.6 * 11 rings * 16, sampled at r^2 = (i + 0.5) / 256 */
static const uint8_t glow_lut[256] = {
    175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 165, 164, 162, 161, 160, 159,
    158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 144, 143,
    142, 141, 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127,
    126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 112,
    111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 102, 101, 100,  99,  98,  97,
     96,  95,  94,  94,  93,  92,  91,  90,  89,  88,  88,  87,  86,  85,  84,  83,
     83,  82,  81,  80,  79,  78,  78,  77,  76,  75,  74,  74,  73,  72,  71,  70,
     70,  69,  68,  67,  67,  66,  65,  64,  64,  63,  62,  61,  61,  60,  59,  58,
     58,  57,  56,  56,  55,  54,  53,  53,  52,  51,  51,  50,  49,  49,  48,  47,
     47,  46,  45,  45,  44,  43,  43,  42,  41,  41,  40,  39,  39,  38,  38,  37,
     36,  36,  35,  35,  34,  33,  33,  32,  32,  31,  30,  30,  29,  29,  28,  28,
     27,  27,  26,  25,  25,  24,  24,  23,  23,  22,  22,  21,  21,  20,  20,  19,
     19,  18,  18,  18,  17,  17,  16,  16,  15,  15,  14,  14,  14,  13,  13,  12,
     12,  11,  11,  11,  10,  10,  10,   9,   9,   8,   8,   8,   7,   7,   7,   6,
      6,   6,   6,   5,   5,   5,   4,   4,   4,   4,   3,   3,   3,   3,   2,   2,
      2,   2,   2,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,
};

/* gradient position of row y in 1/16 of a ref step: the mockup's 14 bands, band b = lerp(b / 13) */
static int bg_u16(int y)
{
    int u = (14 * y - 240) * (WFUI_NBG - 1) * 16 / (13 * 480);
    if (u < 0) u = 0;
    if (u > (WFUI_NBG - 1) * 16) u = (WFUI_NBG - 1) * 16;
    return u;
}

/* glow ring at (x, y), dithered: 0 = none, 1 .. WFUI_NRING - 1 */
static inline int ring_at(int x, int y)
{
    int dx = x - GLOW_CX, dy = y - GLOW_CY;
    int d2 = dx * dx + dy * dy;
    if (d2 >= GLOW_R * GLOW_R) return 0;
    int v = glow_lut[(d2 * 745) >> 16];               /* d2 * 256 / 22500, < 256 */
    int k = (v + wfg_bayer[x & 3][y & 3]) >> 4;
    return k > WFUI_NRING - 1 ? WFUI_NRING - 1 : k;
}

void wfg_background(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > WFUI_H) y1 = WFUI_H;
    for (int y = y0; y < y1; y++)
    {
        uint8_t *row = wfg_cv + y * wfg_pitch;
        int u = bg_u16(y);
        int lo = u >> 4, fr = u & 15;
        uint8_t clo = PX(C_BG0 + lo), chi = PX(C_BG0 + (lo + 1 < WFUI_NBG ? lo + 1 : lo));
        const uint8_t *bay = wfg_bayer[y & 3];
        for (int x = 0; x < WFUI_W; x++) row[x] = fr > bay[x & 3] ? chi : clo;

        /* glow */
        int dy = y - GLOW_CY;
        if (dy * dy < GLOW_R * GLOW_R)
        {
            int hw = isqrt(GLOW_R * GLOW_R - dy * dy);
            int xa = GLOW_CX - hw, xb = GLOW_CX + hw;
            if (xa < 0) xa = 0;
            if (xb > WFUI_W - 1) xb = WFUI_W - 1;
            int jv = y * 16 / (GLOW_R);               /* refs at y = 0, 150, 300 */
            for (int x = xa; x <= xb; x++)
            {
                int k = ring_at(x, y);
                if (!k) continue;
                int j = (jv + wfg_bayer[(y + 1) & 3][(x + 2) & 3]) >> 4;
                if (j > WFUI_NGREF - 1) j = WFUI_NGREF - 1;
                row[x] = PX(C_GLOW0 + j * (WFUI_NRING - 1) + k - 1);
            }
        }

        /* floor shadow, over the glow rings it covers */
        int sdy = y - SH_CY;
        if (sdy > -SH_RY && sdy < SH_RY)
        {
            for (int x = SH_CX - SH_RX; x <= SH_CX + SH_RX; x++)
            {
                int dx = x - SH_CX;
                int r2 = ((dx * dx * 2758) >> 16) + sdy * sdy * 4;      /* r^2 * 256 */
                if (r2 >= 256) continue;
                int s = ((13 * (256 - r2)) >> 6) + wfg_bayer[(y + 2) & 3][(x + 1) & 3];
                s >>= 4;                                                /* 0 .. 3.25 -> levels 0 .. 3 */
                if (!s) continue;
                if (s > WFUI_NSHL) s = WFUI_NSHL;
                int k = ring_at(x, y);
                if (k > WFUI_NSHR - 1) k = WFUI_NSHR - 1;
                row[x] = PX(C_SHAD0 + k * WFUI_NSHL + s - 1);
            }
        }
    }
}

/* strip edge fades: (1 - k / 40) ^ 1.4 toward the strip colour, k = distance from the screen edge */
static const uint8_t fade16[40] = { 16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 6,
                                    5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0 };

void wfg_fade_edges(int y0, int y1)
{
    const uint8_t strip = PX(C_STRIP), frame = PX(C_FRAME);
    const uint8_t ramp[4] = { PX(C_FRAME), PX(C_FADE1), PX(C_FADE2), PX(C_STRIP) };
    for (int y = y0; y < y1; y++)
    {
        uint8_t *row = wfg_cv + y * wfg_pitch;
        for (int k = 0; k < 40; k++)
        {
            int a = fade16[k];
            for (int side = 0; side < 2; side++)
            {
                int x = side ? WFUI_W - 1 - k : k;
                uint8_t p = row[x];
                if (p == strip) continue;
                int t = wfg_bayer[y & 3][x & 3];
                if (p == frame)
                {
                    int l = (a * 3 + t) >> 4;
                    row[x] = ramp[l > 3 ? 3 : l];
                }
                else if (a > t)
                {
                    row[x] = strip;
                }
            }
        }
    }
}

void wfg_scrim(int x0, int y0, int x1, int y1)
{
    const uint8_t d = PX(C_STRIP);
    for (int y = 0; y < WFUI_H; y++)
    {
        uint8_t *row = wfg_cv + y * wfg_pitch;
        int inside_y = y >= y0 && y <= y1;
        for (int x = (y & 1); x < WFUI_W; x += 2)
        {
            if (inside_y && x >= x0 && x <= x1) continue;
            row[x] = d;
        }
    }
}

/* ------------------------------------------------------------------ palette */
static uint8_t pal_rgb[WFUI_NPAL][3];
static uint32_t pal_hw[WFUI_NPAL];                     /* the value we want in the register */
static uint8_t pal_dirty[WFUI_NPAL];
static int pal_taken;
static int pal_op = 3, pal_yb = 0, pal_yw = 255;
static uint32_t pal_canon_white, pal_canon_black;

static const uint8_t fixed_rgb[C_NFIXED][3] = {
    { 236, 238, 240 },   /* C_TXT */
    { 146, 155, 168 },   /* C_TXT2 */
    {  98, 106, 118 },   /* C_TXT3 */
    { 244, 166,  52 },   /* C_AMBER: accent */
    {  84,  58,  22 },   /* C_AMBERD */
    {  38, 168, 158 },   /* C_TEAL: Film LUT group */
    {  16,  62,  58 },   /* C_TEALD */
    {  96, 204, 116 },   /* C_GRN: loaded */
    {  20,  34,  26 },   /* C_CHIPF */
    {  44,  92,  58 },   /* C_CHIPO */
    {  44,  50,  60 },   /* C_LINE */
    {  30,  34,  41 },   /* C_SEP */
    {  52,  58,  68 },   /* C_BAROFF */
    {   9,  10,  12 },   /* C_STRIP */
    {  40,  44,  52 },   /* C_HOLE */
    {  21,  23,  28 },   /* C_FRAME */
    {  17,  19,  23 },   /* C_FADE1: frame 2/3 + strip 1/3 */
    {  13,  14,  17 },   /* C_FADE2 */
    {  34,  38,  46 },   /* C_TRACK */
    {  60,  66,  78 },   /* C_KEYO */
    { 232,  92,  76 },   /* C_WARN */
};

static const uint8_t BG_TOP[3] = { 24, 27, 33 }, BG_BOT[3] = { 10, 11, 14 };

/* background colour at gradient position t (1/1024), channel c */
static int bg_at(int t1024, int c)
{
    return BG_TOP[c] + ((BG_BOT[c] - BG_TOP[c]) * t1024 + 512) / 1024;
}

static int bg_t_of_y(int y)
{
    int t = (14 * y - 240) * 1024 / (13 * 480);
    return t < 0 ? 0 : t > 1024 ? 1024 : t;
}

void wfp_set_rgb(int e, int r, int g, int b)
{
    if (e < 0 || e >= WFUI_NPAL) return;
    if (pal_rgb[e][0] == r && pal_rgb[e][1] == g && pal_rgb[e][2] == b && pal_hw[e]) return;
    pal_rgb[e][0] = r; pal_rgb[e][1] = g; pal_rgb[e][2] = b;
    pal_dirty[e] = 1;
    pal_dirty_any = 1;
}

void wfp_init_fixed(void)
{
    for (int i = 0; i < C_NFIXED; i++) wfp_set_rgb(i, fixed_rgb[i][0], fixed_rgb[i][1], fixed_rgb[i][2]);
    for (int i = 0; i < WFUI_NBG; i++)
    {
        int t = i * 1024 / (WFUI_NBG - 1);
        wfp_set_rgb(C_BG0 + i, bg_at(t, 0), bg_at(t, 1), bg_at(t, 2));
    }
}

void wfp_set_glow(const uint8_t gc[3])
{
    /* rings: bg * (1 - a) + glow * a, a = 0.30 * k / 11 (the mockup's amax 0.30, 16 levels -> 12 + dither) */
    for (int j = 0; j < WFUI_NGREF; j++)
    {
        int t = bg_t_of_y(j * GLOW_R);
        for (int k = 1; k < WFUI_NRING; k++)
        {
            int a = 300 * k / (WFUI_NRING - 1);       /* 1/1000 */
            int rgb[3];
            for (int c = 0; c < 3; c++) rgb[c] = (bg_at(t, c) * (1000 - a) + gc[c] * a + 500) / 1000;
            wfp_set_rgb(C_GLOW0 + j * (WFUI_NRING - 1) + k - 1, rgb[0], rgb[1], rgb[2]);
        }
    }
    /* floor shadow: the ring under it darkened by 0.2 / 0.4 / 0.6 */
    int t = bg_t_of_y(SH_CY);
    for (int k = 0; k < WFUI_NSHR; k++)
    {
        int a = 300 * k / (WFUI_NRING - 1);
        int base[3];
        for (int c = 0; c < 3; c++) base[c] = (bg_at(t, c) * (1000 - a) + gc[c] * a + 500) / 1000;
        for (int s = 1; s <= WFUI_NSHL; s++)
        {
            int m = 10 - 2 * s;                         /* tenths kept */
            wfp_set_rgb(C_SHAD0 + k * WFUI_NSHL + s - 1, base[0] * m / 10, base[1] * m / 10, base[2] * m / 10);
        }
    }
}

/* BT.601 full range (ML's yuv2rgb), scaled into the luma range of Canon's own black and white entries */
static uint32_t rgb_to_entry(const uint8_t *p)
{
    int r = p[0], g = p[1], b = p[2];
    int y = (77 * r + 150 * g + 29 * b + 128) >> 8;
    int u = (-43 * r - 85 * g + 128 * b + 128) >> 8;
    int v = (128 * r - 107 * g - 21 * b + 128) >> 8;
    int span = pal_yw - pal_yb;
    if (span != 255)
    {
        y = pal_yb + y * span / 255;
        u = u * span / 255;
        v = v * span / 255;
    }
    if (y < 0) y = 0;
    if (y > 255) y = 255;
    if (u < -127) u = -127;
    if (u > 127) u = 127;
    if (v < -127) v = -127;
    if (v > 127) v = 127;
    return ((uint32_t)pal_op << 24) | ((uint32_t)y << 16) | ((uint32_t)(u & 0xFF) << 8) | (uint32_t)(v & 0xFF);
}

static void hw_write(int idx, uint32_t v)
{
    EngDrvOut(LCD_Palette[3 * idx], v);
    EngDrvOut(LCD_Palette[3 * idx + 0x300], v);
}

static void pal_write_dirty(void)
{
    if (!pal_taken || !pal_dirty_any) return;
    if (!display_is_on()) return;
    for (int e = 0; e < WFUI_NPAL; e++)
    {
        if (!pal_dirty[e]) continue;
        pal_hw[e] = rgb_to_entry(pal_rgb[e]);
        hw_write(WFUI_PAL_BASE + e, pal_hw[e]);
        pal_dirty[e] = 0;
    }
    pal_dirty_any = 0;
}

int wfp_take(void)
{
    pal_canon_white = LCD_Palette[3 * COLOR_WHITE + 2];
    pal_canon_black = LCD_Palette[3 * COLOR_BLACK + 2];
    int op = (pal_canon_white >> 24) & 0xFF;
    int yw = (pal_canon_white >> 16) & 0xFF, yb = (pal_canon_black >> 16) & 0xFF;
    pal_op = op ? op : 3;
    if (yw >= yb + 128) { pal_yw = yw; pal_yb = yb; }
    else { pal_yw = 255; pal_yb = 0; }
    pal_taken = 1;
    for (int e = 0; e < WFUI_NPAL; e++) pal_dirty[e] = 1;
    pal_dirty_any = 1;
    return 0;
}

void wfp_keepalive(int force)
{
    if (!pal_taken || !display_is_on()) return;
    static const uint8_t probe[] = { C_TXT, C_BG0, C_GLOW0, C_CAN0, WFUI_NPAL - 1 };
    int lost = force;
    for (unsigned i = 0; i < sizeof(probe) && !lost; i++)
    {
        int e = probe[i];
        if (pal_hw[e] && shamem_read(LCD_Palette[3 * (WFUI_PAL_BASE + e)]) != pal_hw[e]) lost = 1;
    }
    if (!lost) return;
    for (int e = 0; e < WFUI_NPAL; e++) pal_dirty[e] = 1;
    pal_dirty_any = 1;
    BMP_LOCK( pal_write_dirty(); )
}

void wfp_restore(void)
{
    if (!pal_taken) return;
    pal_taken = 0;
    if (!display_is_on()) return;
    for (int e = 0; e < WFUI_NPAL; e++)
    {
        int i = WFUI_PAL_BASE + e;
        hw_write(i, LCD_Palette[3 * i + 2]);
        pal_hw[e] = 0;
    }
}

void wfp_log(void)
{
    wf_log("ui: palette 0x%02x..0x%02x (%d UI + %d can slots), Canon white %08x black %08x -> op %d Y %d..%d",
           WFUI_PAL_BASE, WFUI_PAL_END - 1, C_CAN0, WFUI_CAN_SLOTS, (unsigned)pal_canon_white,
           (unsigned)pal_canon_black, pal_op, pal_yb, pal_yw);
}
