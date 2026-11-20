/* os_mock.c - modernUI stage 1 (docs/MODERNUI.md): the WindyFilm "film OS" screens as mockups, drawn on the PC with the
 * browser's own code: src/ui/wf_gfx.c (canvas, primitives, background, palette), wf_font.c (Ubuntu Sans small text,
 * ML's argnor fonts), wf_cans.c (the WFCANS.DAT cans), plus the display fonts of mk_osfont.py (WFOSFONT.DAT, read
 * here by a small renderer of the same format). Not part of the module build: these screens move into src/ui once the
 * owner approves the pictures.
 *
 *   os_mock OUTDIR CARD RBFDIR OSFONT CAM          CAM = EOSM | 5D2
 * Per screen: OUTDIR/<cam>_<name>.ppm (the front buffer through the hardware palette), .idx (720 x 480 raw palette
 * indices, 0 = transparent: LiveView or the playback layer shows there) and .lay (the photos of that layer and the two
 * scrim entries) for os_comp.py. */
#include <stdio.h>
#include "wf_ui_int.h"
#include "fake_core.h"

static int CAM;                                 /* WF_CAM_EOSM / WF_CAM_5D2 */
static const char *outdir;
static FILE *lay;                               /* the open screen's .lay */

/* ------------------------------------------------------------------ display fonts (WFOSFONT.DAT, WFFONTS.DAT format) */
enum { D20 = 0, D30, D46, D80, L12, D26, OSF_N };
struct osf { const uint8_t *g, *k; int nk, h, asc, first, count; uint16_t kidx[97]; };
static uint8_t *osf_buf;
static struct osf osf[OSF_N];

static unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int osf_load(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    osf_buf = (uint8_t *)host_malloc(n);
    if (fread(osf_buf, 1, n, fp) != (size_t)n) { fclose(fp); return -1; }
    fclose(fp);
    if (memcmp(osf_buf, "WFFN", 4) || rd16(osf_buf + 6) < OSF_N) return -1;
    for (int i = 0; i < OSF_N; i++)
    {
        const uint8_t *d = osf_buf + 16 + i * 24;
        struct osf *f = &osf[i];
        f->h = d[4]; f->asc = d[5]; f->first = d[6]; f->count = d[7];
        f->g = osf_buf + rd32(d + 8);
        f->k = osf_buf + rd32(d + 12);
        f->nk = rd16(d + 16);
        int j = 0;
        for (int c = 0; c <= f->count; c++)
        {
            while (j < f->nk && f->k[j * 4] < f->first + c) j++;
            f->kidx[c] = j;
        }
    }
    return 0;
}

static int osf_kern(const struct osf *f, int a, int b)
{
    for (int i = f->kidx[a - f->first], e = f->kidx[a - f->first + 1]; i < e; i++)
        if (f->k[i * 4 + 1] == b) return (int16_t)rd16(f->k + i * 4 + 2);
    return 0;
}

/* sp: extra px between letters (the small caps labels); y = the cell top; returns the advance */
static int os_run(int fi, int x, int y, const char *s, int c, int sp, int draw)
{
    const struct osf *f = &osf[fi];
    int pen = 0, prev = -1;
    for (; *s; s++)
    {
        int o = (unsigned char)*s;
        if (o < f->first || o >= f->first + f->count) o = '?';
        if (prev >= 0) pen += osf_kern(f, prev, o) + sp * 64;
        const uint8_t *g = f->g + (o - f->first) * 12;
        if (draw && rd32(g + 8))
        {
            int gx = ((pen + 32) >> 6) + x + (int8_t)g[2], gy = y + (int8_t)g[3], w = g[4], h = g[5], bpr = g[6];
            const uint8_t *bm = osf_buf + rd32(g + 8);
            for (int j = 0; j < h; j++)
            {
                int yy = gy + j;
                if (yy < 0 || yy >= WFUI_H) continue;
                for (int i = 0; i < w; i++)
                    if (bm[j * bpr + (i >> 3)] & (0x80 >> (i & 7)))
                    {
                        int xx = gx + i;
                        if (xx >= 0 && xx < WFUI_W) wfg_cv[yy * wfg_pitch + xx] = PX(c);
                    }
            }
        }
        pen += rd16(g);
        prev = o;
    }
    return (pen + 32) >> 6;
}
static int os_text(int fi, int x, int y, const char *s, int c) { return os_run(fi, x, y, s, c, 0, 1); }
static int os_textw(int fi, const char *s) { return os_run(fi, 0, 0, s, 0, 0, 0); }
static int os_textr(int fi, int xr, int y, const char *s, int c) __attribute__((unused));
static int os_textr(int fi, int xr, int y, const char *s, int c) { return os_text(fi, xr - os_textw(fi, s), y, s, c); }
static int os_textc(int fi, int xc, int y, const char *s, int c) { return os_text(fi, xc - os_textw(fi, s) / 2, y, s, c); }
static int os_caps(int x, int y, const char *s, int c) { return os_run(L12, x, y, s, c, 1, 1); }
static int os_capsw(const char *s) { return os_run(L12, 0, 0, s, 0, 1, 0); }
/* baseline helpers: text whose baseline sits at b */
static int os_textb(int fi, int x, int b, const char *s, int c) { return os_text(fi, x, b - osf[fi].asc, s, c); }

/* ------------------------------------------------------------------ pieces of the identity */
static void clear0(void)                        /* transparent: LiveView / the playback layer shows */
{
    for (int y = 0; y < WFUI_H; y++) memset(wfg_cv + y * wfg_pitch, 0, WFUI_W);
}
static void hole(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++) memset(wfg_cv + y * wfg_pitch + x0, 0, x1 - x0 + 1);
}

/* the WindyFilm mark: an amber tile with three wind strokes, the top one ending in a curl */
static void os_mark(int x, int y, int s)
{
    wfg_rrect(x, y, x + s - 1, y + s - 1, s / 4, C_AMBER, -1, 0);
    /* a spool at the left with its leader running out to the right as a gust, and two shorter gusts under it */
    int u = s / 22 > 0 ? s / 22 : 1;
    wfg_ellipse(x + 3 * u, y + 5 * u, x + 13 * u, y + 15 * u, C_STRIP);
    wfg_ellipse(x + 6 * u, y + 8 * u, x + 10 * u, y + 12 * u, C_AMBER);
    wfg_rect(x + 8 * u, y + 5 * u, x + 18 * u, y + 7 * u, C_STRIP);
    wfg_rrect(x + 13 * u, y + 11 * u, x + 19 * u, y + 12 * u, u, C_STRIP, -1, 0);
    wfg_rrect(x + 11 * u, y + 15 * u, x + 17 * u, y + 16 * u, u, C_STRIP, -1, 0);
}

static void battery(int x, int y, int pct, int c)
{
    wfg_rrect(x, y, x + 21, y + 10, 2, -1, c, 1);
    wfg_rect(x + 22, y + 3, x + 23, y + 7, c);
    wfg_rect(x + 2, y + 2, x + 2 + 17 * pct / 100, y + 8, c);
}

/* the roll gauge: one tick per frame; exposed ticks light, the next frame amber, the rest dim */
static void ticks(int x, int y, int w, int h, int shot, int total)
{
    for (int i = 0; i < total; i++)
    {
        int tx = x + i * (w - 2) / (total - 1);
        int c = i < shot ? C_TXT2 : i == shot ? C_AMBER : C_BAROFF;
        int hh = (i % 6 == 5 || i == 0) ? h : h - 4;
        if (i == shot) hh = h + 4;
        wfg_rect(tx, y + h - hh, tx + 1, y + h - 1 + (i == shot ? 0 : 0), c);
    }
}

static int chip(int x, int y, const char *s, int fill, int line, int tc)
{
    int w = ui_textw(F_13, s) + 16;
    wfg_rrect(x, y, x + w - 1, y + 19, 10, fill, line, line >= 0 ? 1 : 0);
    ui_text(F_13, x + 8, y + 2, s, tc);
    return w;
}

/* footer: key caps and what they do (pairs), from x; returns the end */
static int key(int x, int y, const char *k, const char *what)
{
    int w = ui_textw(F_13, k) + 10;
    wfg_rrect(x, y, x + w - 1, y + 18, 4, C_FRAME, C_KEYO, 1);
    ui_text(F_13, x + 5, y + 2, k, C_TXT);
    x += w + 5;
    x += ui_text(F_13, x, y + 2, what, C_TXT2);
    return x + 14;
}
static void footer(const char *const *kv)
{
    wfg_rect(16, 440, 703, 440, C_SEP);
    int x = 16;
    for (; kv[0]; kv += 2) x = key(x, 450, kv[0], kv[1]);
}

static void top_bar(const char *title, const char *chip_txt)
{
    os_mark(16, 9, 22);
    os_text(D20, 48, 8, title, C_TXT);
    int x = 704 - ui_textw(F_15, "14:32");
    ui_text(F_15, x, 11, "14:32", C_TXT2);
    x -= 34;
    battery(x, 15, 70, C_TXT2);
    x -= 8;
    if (chip_txt)
    {
        int w = ui_textw(F_13, chip_txt) + 16;
        chip(x - w, 10, chip_txt, -1, C_LINE, C_TXT2);
    }
    wfg_rect(16, 40, 703, 40, C_SEP);
}

/* 20 x 20 line icons */
static void ic_can(int x, int y, int c, int bg)
{
    wfg_rect(x + 8, y, x + 11, y + 2, c);
    wfg_rrect(x + 3, y + 2, x + 16, y + 5, 1, c, -1, 0);
    wfg_rrect(x + 4, y + 5, x + 15, y + 16, 2, -1, c, 2);
    wfg_rrect(x + 3, y + 16, x + 16, y + 19, 1, c, -1, 0);
    (void)bg;
}
static void ic_bulb(int x, int y, int c, int bg)
{
    wfg_ellipse(x + 3, y, x + 16, y + 13, c);
    wfg_ellipse(x + 6, y + 3, x + 10, y + 7, bg);
    wfg_rect(x + 6, y + 13, x + 13, y + 15, c);
    wfg_rect(x + 7, y + 17, x + 12, y + 19, c);
}
static void ic_prints(int x, int y, int c, int bg)
{
    wfg_rrect(x + 5, y + 1, x + 19, y + 14, 2, -1, c, 2);
    wfg_rrect(x, y + 5, x + 14, y + 19, 2, bg, c, 2);
    wfg_rect(x + 3, y + 8, x + 11, y + 14, c);
}
static void ic_gear(int x, int y, int c, int bg)
{
    static const int8_t d[8][2] = { { 0, -8 }, { 6, -6 }, { 8, 0 }, { 6, 6 }, { 0, 8 }, { -6, 6 }, { -8, 0 }, { -6, -6 } };
    int cx = x + 10, cy = y + 10;
    for (int i = 0; i < 8; i++) wfg_rect(cx + d[i][0] - 2, cy + d[i][1] - 2, cx + d[i][0] + 1, cy + d[i][1] + 1, c);
    wfg_ellipse(cx - 7, cy - 7, cx + 6, cy + 6, c);
    wfg_ellipse(cx - 3, cy - 3, cx + 2, cy + 2, bg);
}

static void bars5(int x, int y, int n, int on, int off)
{
    for (int i = 0; i < 5; i++) wfg_rect(x + i * 7, y + 12 - 2 * i, x + i * 7 + 4, y + 14, i < n ? on : off);
}

/* the exposure scale: -2 .. +2 in thirds, the value as a lit tick with an amber marker under it */
static void meter(int x, int y, int ev3, int lc)
{
    for (int i = -6; i <= 6; i++)
    {
        int tx = x + (i + 6) * 10, hh = i % 3 ? 6 : (i ? 10 : 14);
        wfg_rect(tx, y + 14 - hh, tx + 1, y + 13, i == ev3 ? C_AMBER : lc);
    }
    int mx = x + (ev3 + 6) * 10 + 1;
    wfg_tri(mx - 5, y + 23, mx + 5, y + 23, mx, y + 17, C_AMBER);
    ui_text(F_13, x - 3, y + 24, "-2", lc);
    ui_text(F_13, x + 116, y + 24, "+2", lc);
}

static const struct wf_film *film(const char *code)
{
    for (int i = 0; i < wf_nfilms; i++)
        if (!strcmp(wf_films[i].code, code)) return &wf_films[i];
    fprintf(stderr, "no film %s\n", code);
    exit(1);
}

static void glow_for(const struct wf_film *f, int cx, int cy, int shx, int shy)
{
    uint8_t rgb[3];
    wfc_glow(f, rgb);
    wfg_glow_centre(cx, cy, shx, shy);
    wfp_set_glow(rgb);
}

/* ------------------------------------------------------------------ output */
static char cur[64];
static void begin(const char *name)
{
    snprintf(cur, sizeof cur, "%s_%s", CAM == WF_CAM_5D2 ? "5d2" : "eosm", name);
    char p[700];
    snprintf(p, sizeof p, "%s/%s.lay", outdir, cur);
    lay = fopen(p, "w");
    fprintf(lay, "scrim %d 0.50\nscrim %d 0.68\n", PX(C_FADE1), PX(C_FADE2));
    wfg_begin();
}
static void layer(int x0, int y0, int x1, int y1, const char *photo, const char *fit)
{
    fprintf(lay, "photo %d %d %d %d %s %s\n", x0, y0, x1, y1, photo, fit);
}
static void end(void)
{
    fclose(lay);
    wfg_present();
    char p[700];
    snprintf(p, sizeof p, "%s/%s.ppm", outdir, cur);
    host_dump(p);
    snprintf(p, sizeof p, "%s/%s.idx", outdir, cur);
    FILE *fp = fopen(p, "wb");
    uint8_t *r = bmp_vram_real();
    for (int y = 0; y < WFUI_H; y++) fwrite(r + y * BMPPITCH, 1, WFUI_W, fp);
    fclose(fp);
    printf("shot %s\n", cur);
}

/* the roll in the camera, per camera (the owner's rolls of the RAWB27 / RAWB28 checks) */
static const char *R_FILM, *R_NUM, *R_LIGHT, *R_PRINT;
static int R_SHOT;

/* ------------------------------------------------------------------ 1. Home (in place of the ML menu) */
static void scr_home(int focus)
{
    const struct wf_film *f = film(R_FILM);
    begin(focus ? "02_home_darkroom" : "01_home");
    glow_for(f, 118, 168, 118, 292);
    wfg_background(0, WFUI_H);
    top_bar("WindyFilm", CAM == WF_CAM_5D2 ? "5D Mark II" : "EOS M");

    wfc_draw(f, CAN_BIG, 38, 54, 0);
    char s[64];
    snprintf(s, sizeof s, "ROLL %s", R_NUM);
    int cw = os_caps(222, 62, s, C_AMBER);
    wfg_rect(222 + cw + 7, 67, 222 + cw + 9, 69, C_AMBER);
    os_caps(222 + cw + 17, 62, "IN THE CAMERA", C_AMBER);
    os_text(D30, 220, 78, f->name, C_TXT);
    int x = 222;
    x += chip(x, 120, f->kind, -1, C_LINE, C_TXT2) + 6;
    snprintf(s, sizeof s, "ISO %d", f->iso);
    x += chip(x, 120, s, -1, C_LINE, C_TXT2) + 6;
    chip(x, 120, f->format, -1, C_LINE, C_TXT2);

    snprintf(s, sizeof s, "%d", R_SHOT);
    int w = os_textb(D80, 216, 236, s, C_TXT);
    os_textb(D20, 216 + w + 4, 236, "/ 36", C_TXT3);
    snprintf(s, sizeof s, "%d frames left", 36 - R_SHOT);
    ui_text(F_15, 362, 190, s, C_TXT);
    snprintf(s, sizeof s, "Next: frame %d", R_SHOT + 1);
    ui_text(F_13, 362, 212, s, C_TXT2);
    ticks(222, 254, 250, 14, R_SHOT, 36);

    /* the paths: tiles */
    static const char *const tn[4] = { "Film shelf", "Darkroom", "Prints", "Settings" };
    char sub[4][48];
    snprintf(sub[0], 48, "%d films  -  load a new roll", wf_nfilms);
    snprintf(sub[1], 48, "%d frames to develop", R_SHOT);
    snprintf(sub[2], 48, "ROLL 021  -  24 prints");
    snprintf(sub[3], 48, "Keys, PLAY, HUD, roll");
    for (int i = 0; i < 4; i++)
    {
        int y0 = 54 + i * 66, foc = i == focus;
        wfg_rrect(492, y0, 703, y0 + 57, 10, foc ? C_AMBERD : C_SEP, foc ? C_AMBER : -1, foc ? 2 : 0);
        int ic = foc ? C_AMBER : C_TXT2, bg = foc ? C_AMBERD : C_SEP;
        if (i == 0) ic_can(506, y0 + 19, ic, bg);
        if (i == 1) ic_bulb(506, y0 + 19, foc ? C_AMBER : C_WARN, bg);
        if (i == 2) ic_prints(506, y0 + 19, ic, bg);
        if (i == 3) ic_gear(506, y0 + 19, ic, bg);
        os_text(D20, 538, y0 + 7, tn[i], C_TXT);
        ui_text(F_13, 539, y0 + 34, sub[i], foc ? C_TXT : C_TXT2);
    }

    /* the look of the loaded roll */
    wfg_rrect(16, 322, 703, 422, 12, C_FRAME, C_SEP, 1);
    os_caps(32, 334, "LOOK OF THIS ROLL", C_TXT3);
    os_caps(560, 334, "INFO  CHANGE", C_TXT3);
    static const char *const ln[6] = { "LIGHT", "PRINT", "PAPER", "GRAIN", "HALATION", "STRENGTH" };
    const char *lv[6] = { R_LIGHT, R_PRINT, "Glossy", "Normal", "Wide", "" };
    for (int i = 0; i < 6; i++)
    {
        int cx = 32 + i * 112;
        if (i) wfg_rect(cx - 12, 362, cx - 12, 406, C_SEP);
        os_caps(cx, 362, ln[i], C_TXT3);
        if (i == 5) bars5(cx, 384, 4, C_AMBER, C_BAROFF);
        else os_text(D20, cx, 380, lv[i], C_TXT);
    }
    if (CAM == WF_CAM_EOSM)
    {
        static const char *const k[] = { "SET", "Open", "UP/DN", "Choose", "MENU", "Canon menu",
                                         "HALF", "Shoot", "2 fingers", "ML menu", 0 };
        footer(k);
    }
    else
    {
        static const char *const k[] = { "SET", "Open", "JOY", "Choose", "MENU", "Canon menu",
                                         "HALF", "Shoot", "TRASH", "ML menu", 0 };
        footer(k);
    }
    end();
}

/* ------------------------------------------------------------------ 2. Film shelf (loading film) */
static void scr_shelf(void)
{
    static const char *const shelf[12] = { "PRT160", "PRT400", "PRT800", "PAS400", "PRONEG", "SUN200", "SNP200", "EVD400",
                                           "TRV400", "CLASNEG", "BLD100", "POP800" };
    const int sel = 1;
    const struct wf_film *f = film(shelf[sel]);
    begin("03_shelf");
    glow_for(f, 150, 190, 150, 302);
    wfg_background(0, WFUI_H);
    top_bar("Film shelf", 0);

    /* section pill and tabs */
    wfg_rrect(16, 50, 177, 73, 12, C_FRAME, C_LINE, 1);
    wfg_rrect(18, 52, 74, 71, 10, C_AMBER, -1, 0);
    ui_text(F_13, 31, 54, "Film", C_STRIP);
    ui_text(F_13, 88, 54, "Film stage", C_TXT2);
    static const char *const tabs[4] = { "Colour 15", "Cine 4", "B&W 11", "Film LUT 23" };
    int x = 320;
    for (int i = 0; i < 4; i++)
    {
        int w = ui_textw(F_15, tabs[i]);
        ui_text(F_15, x, 53, tabs[i], i ? C_TXT2 : C_TXT);
        if (!i) wfg_rect(x, 74, x + w - 1, 75, C_AMBER);
        x += w + 24;
    }

    /* the picked can, big, and what it is */
    wfc_draw(f, CAN_BIG, 70, 84, 0);
    os_text(D30, 24, 318, f->name, C_TXT);
    x = 26;
    x += chip(x, 358, f->kind, -1, C_LINE, C_TXT2) + 6;
    char s[48];
    snprintf(s, sizeof s, "ISO %d", f->iso);
    x += chip(x, 358, s, -1, C_LINE, C_TXT2) + 6;
    chip(x, 358, "3 prints", -1, C_LINE, C_TXT2);
    ui_text(F_13, 26, 386, f->line1, C_TXT2);
    ui_text(F_13, 26, 402, f->line2, C_TXT2);

    /* three shelves of four cans */
    for (int r = 0; r < 3; r++)
    {
        int by = 84 + r * 116 + 84;                 /* the board under the cans */
        wfg_rrect(312, by, 703, by + 3, 1, C_LINE, -1, 0);
        wfg_rect(316, by + 4, 699, by + 7, C_FRAME);
        for (int c = 0; c < 4; c++)
        {
            int i = r * 4 + c, cx = 312 + c * 98 + 49;
            const struct wf_film *g = film(shelf[i]);
            if (i == sel)
            {
                wfg_rrect(cx - 33, by - 92, cx + 32, by - 1, 8, C_AMBERD, C_AMBER, 2);
                wfc_draw(g, CAN_SEL, cx - 29, by - 88, 0);
            }
            else wfc_draw(g, CAN_STRIP, cx - 26, by - 75, 0);
            int w = ui_textw(F_13C, g->shortname);
            ui_text(F_13C, cx - w / 2, by + 9, g->shortname, i == sel ? C_AMBER : C_TXT2);
            if (i == 0) wfg_ellipse(cx + 22, by - 76, cx + 28, by - 70, C_GRN);   /* in the camera now */
        }
    }
    wfg_rrect(708, 84, 709, 420, 1, C_TRACK, -1, 0);
    wfg_rrect(708, 84, 709, 260, 1, C_TXT3, -1, 0);
    if (CAM == WF_CAM_EOSM)
    {
        static const char *const k[] = { "SET", "Load", "RING", "Film", "TAP", "Group", "INFO", "About", "MENU", "Home", 0 };
        footer(k);
    }
    else
    {
        static const char *const k[] = { "SET", "Load", "DIAL", "Film", "PICT", "Group", "INFO", "About", "MENU", "Home", 0 };
        footer(k);
    }
    end();
}

/* ------------------------------------------------------------------ 3. Shooting: the LiveView HUD (both cameras) */
static void corners(int x0, int y0, int x1, int y1, int l, int c)
{
    wfg_rect(x0, y0, x0 + l, y0 + 1, c); wfg_rect(x0, y0, x0 + 1, y0 + l, c);
    wfg_rect(x1 - l, y0, x1, y0 + 1, c); wfg_rect(x1 - 1, y0, x1, y0 + l, c);
    wfg_rect(x0, y1 - 1, x0 + l, y1, c); wfg_rect(x0, y1 - l, x0 + 1, y1, c);
    wfg_rect(x1 - l, y1 - 1, x1, y1, c); wfg_rect(x1 - 1, y1 - l, x1, y1, c);
}

static void scr_hud(int minimal)
{
    const struct wf_film *f = film(R_FILM);
    begin(minimal ? "05_hud_minimal" : "04_hud_full");
    clear0();
    layer(0, 0, 719, 479, CAM == WF_CAM_5D2 ? "lv5d2" : "lv", "cover");
    uint8_t rgb[3];
    wfc_glow(f, rgb);
    wfp_set_rgb(C_TEALD, rgb[0], rgb[1], rgb[2]);       /* the film's dot: one entry recoloured */
    char s[64];

    /* film pill, top left */
    snprintf(s, sizeof s, "ROLL %s", R_NUM);
    int w = ui_textw(F_15, f->shortname) + ui_textw(F_13, s) + 50;
    wfg_rrect(14, 12, 14 + w, 39, 14, C_FADE2, -1, 0);
    wfg_ellipse(26, 20, 36, 30, C_TEALD);
    int x = 44 + ui_text(F_15, 44, 16, f->shortname, C_TXT);
    ui_text(F_13, x + 8, 18, s, C_TXT2);

    /* frames, top right (minimal: the whole HUD is this pill and the film pill) */
    snprintf(s, sizeof s, "%d", R_SHOT + 1);
    if (minimal)
    {
        int tw = os_textw(D26, s) + ui_textw(F_13, "/36") + 30;
        wfg_rrect(705 - tw, 12, 705, 46, 16, C_FADE2, -1, 0);
        int xx = os_text(D26, 720 - tw, 13, s, C_TXT);
        ui_text(F_13, 720 - tw + xx + 3, 25, "/36", C_TXT2);
        corners(28, 56, 691, 451, 16, C_TXT);
        end();
        return;
    }
    wfg_rrect(612, 12, 705, 39, 14, C_FADE2, -1, 0);
    battery(626, 21, 70, C_TXT);
    ui_text(F_13, 658, 18, CAM == WF_CAM_5D2 ? "RAW" : "RAW+L", C_TXT2);
    corners(28, 56, 691, 386, 16, C_TXT);

    /* the exposure band */
    wfg_rrect(10, 398, 709, 471, 16, C_FADE1, -1, 0);
    static const char *const lab[3] = { "SHUTTER", "APERTURE", "ISO" };
    const char *val[3] = { CAM == WF_CAM_5D2 ? "1/250" : "1/125", CAM == WF_CAM_5D2 ? "f/5.6" : "f/2.8", 0 };
    snprintf(s, sizeof s, "%d", f->iso);
    val[2] = s;
    int cx = 28;
    for (int i = 0; i < 3; i++)
    {
        os_caps(cx, 408, lab[i], C_TXT3);
        int vw = os_text(D26, cx - 1, 424, val[i], C_TXT);
        if (i == 2) chip(cx + vw + 6, 432, "FILM", -1, C_AMBER, C_AMBER);
        cx += i == 2 ? 150 : 104;
    }
    os_caps(386, 408, "METER", C_TXT3);
    meter(386, 422, -1, C_TXT2);
    /* the counter and the roll */
    char n[8];
    snprintf(n, sizeof n, "%d", R_SHOT + 1);
    int nw = os_textw(D46, n);
    os_caps(552, 408, "FRAME", C_TXT3);
    os_text(D46, 664 - nw, 404, n, C_TXT);
    ui_text(F_13, 668, 436, "/ 36", C_TXT2);
    ticks(552, 452, 70, 8, R_SHOT, 36);
    end();
}

/* ------------------------------------------------------------------ 3b. 5D2 through the viewfinder: the top plate */
static void scr_topplate(void)
{
    const struct wf_film *f = film(R_FILM);
    begin("06_topplate");
    glow_for(f, 594, 160, 594, 290);
    wfg_background(0, WFUI_H);
    top_bar("Top plate", "Viewfinder");

    os_caps(24, 56, "SHUTTER", C_TXT3);
    os_textb(D80, 18, 146, "1/250", C_TXT);
    os_caps(258, 56, "APERTURE", C_TXT3);
    os_textb(D80, 252, 146, "f/5.6", C_TXT);

    wfg_rect(24, 170, 459, 170, C_SEP);
    os_caps(24, 184, "ISO", C_TXT3);
    char s[48];
    snprintf(s, sizeof s, "%d", f->iso);
    int w = os_text(D46, 22, 198, s, C_TXT);
    chip(30 + w, 216, "FROM FILM", -1, C_AMBER, C_AMBER);
    os_caps(258, 184, "EXPOSURE", C_TXT3);
    meter(262, 208, 1, C_TXT2);
    os_caps(406, 184, "MODE", C_TXT3);
    os_text(D46, 404, 198, "Av", C_AMBER);

    wfg_rrect(16, 268, 463, 336, 12, C_FRAME, C_SEP, 1);
    static const char *const ln[4] = { "LIGHT", "PRINT", "GRAIN", "PAPER" };
    const char *lv[4] = { R_LIGHT, R_PRINT, "Normal", "Glossy" };
    for (int i = 0; i < 4; i++)
    {
        int cx = 32 + i * 110;
        if (i) wfg_rect(cx - 12, 280, cx - 12, 324, C_SEP);
        os_caps(cx, 282, ln[i], C_TXT3);
        os_text(D20, cx, 300, lv[i], C_TXT);
    }
    static const char *const ln2[4] = { "NEXT FRAME", "CARD", "DRIVE", "QUICK" };
    snprintf(s, sizeof s, "%d", R_SHOT + 1);
    const char *lv2[4] = { s, "412 RAW", "Single", "Off" };
    for (int i = 0; i < 4; i++)
    {
        int cx = 32 + i * 110;
        if (i) wfg_rect(cx - 12, 360, cx - 12, 404, C_SEP);
        os_caps(cx, 362, ln2[i], C_TXT3);
        os_text(D20, cx, 380, lv2[i], i ? C_TXT2 : C_TXT);
    }

    /* the roll, right */
    wfg_rrect(484, 50, 703, 432, 14, C_FRAME, C_SEP, 1);
    wfc_draw(f, CAN_BIG, 514, 56, 0);
    os_textc(D20, 594, 290, f->name, C_TXT);
    snprintf(s, sizeof s, "ROLL %s", R_NUM);
    os_caps(594 - os_capsw(s) / 2, 318, s, C_AMBER);
    snprintf(s, sizeof s, "%d", R_SHOT);
    int nw = os_textw(D46, s) + 4 + ui_textw(F_15, "/ 36");
    int nx = os_text(D46, 594 - nw / 2, 336, s, C_TXT);
    ui_text(F_15, 594 - nw / 2 + nx + 4, 366, "/ 36", C_TXT2);
    ticks(500, 404, 188, 12, R_SHOT, 36);
    static const char *const k[] = { "MENU", "Home", "TRASH", "Film shelf", "INFO", "Screen off", "PLAY", "Prints", 0 };
    footer(k);
    end();
}

/* ------------------------------------------------------------------ 4. PLAY: prints and the contact sheet */
static void scr_print(void)
{
    begin("07_play_print");
    wfg_glow_centre(-400, -400, -400, -400);
    wfg_background(0, WFUI_H);
    top_bar("Prints", "PLAY: prints");
    /* the print (1800 x 1379) at the right, a window onto the playback layer; the roll and the count at the left */
    int pw = 504, ph = 386, px0 = 200, py0 = 47;
    wfg_rrect(px0 - 2, py0 - 2, px0 + pw + 1, py0 + ph + 1, 2, C_STRIP, -1, 0);
    hole(px0, py0, px0 + pw - 1, py0 + ph - 1);
    layer(px0, py0, px0 + pw - 1, py0 + ph - 1, "print", "fit");
    os_caps(24, 58, "ROLL 021", C_AMBER);
    os_text(D20, 23, 74, "Portra 400", C_TXT);
    ui_text(F_13, 24, 100, "Print Cine", C_TXT2);
    ui_text(F_13, 24, 116, "White frame, Info on", C_TXT2);
    os_textb(D80, 18, 230, "7", C_TXT);
    ui_text(F_15, 66, 208, "/ 24", C_TXT2);
    ticks(24, 246, 150, 12, 7, 24);
    os_caps(24, 278, "FILE", C_TXT3);
    ui_text(F_13, 24, 294, "ROLLS/ROLL021", C_TXT2);
    ui_text(F_13, 24, 310, "F07_6851.JPG", C_TXT2);
    os_caps(24, 338, "SHOT", C_TXT3);
    ui_text(F_13, 24, 354, "6 Mar 2024, 16:42", C_TXT2);
    ui_text(F_13, 24, 370, "1/320  f/6.3  ISO 320", C_TXT2);
    chip(24, 398, "Developed", -1, C_GRN, C_GRN);
    static const char *const k[] = { "L/R", "Print", "SET", "Contact sheet", "INFO", "Details", "PLAY", "Camera JPEG",
                                     "MENU", "Home", 0 };
    footer(k);
    end();
}

static void scr_sheet(void)
{
    begin("08_play_sheet");
    wfg_glow_centre(-400, -400, -400, -400);
    wfg_background(0, WFUI_H);
    top_bar("Contact sheet", "ROLL 021  -  24 prints");
    /* a 24-exposure roll: three strips of 8 frames, rebate band, frame windows, numbers in the rebate */
    static const int pics[24] = { 5, 9, 13, 15, 18, 19, 8, 12, 37, 40, 46, 34, 35, 33, 30, 31, 41, 42, 43, 44, 45, 47,
                                  48, 49 };
    int fw = 78, fh = 52, gap = 4, x0 = (720 - (8 * fw + 7 * gap)) / 2, sel = 6;
    for (int r = 0; r < 3; r++)
    {
        int y0 = 56 + r * 76;
        wfg_rect(x0 - 10, y0 - 6, x0 + 8 * fw + 7 * gap + 9, y0 + fh + 16, C_STRIP);
        for (int c = 0; c < 8; c++)
        {
            int i = r * 8 + c, fx = x0 + c * (fw + gap);
            hole(fx, y0, fx + fw - 1, y0 + fh - 1);
            char nm[8];
            snprintf(nm, sizeof nm, "t_%02d", pics[i]);
            layer(fx, y0, fx + fw - 1, y0 + fh - 1, nm, "cover");
            char s[8];
            snprintf(s, sizeof s, "%d", i + 1);
            ui_text(F_13C, fx + 2, y0 + fh + 1, s, i == sel ? C_AMBER : C_TXT3);
            if (i == sel)
            {
                wfg_rrect(fx - 3, y0 - 3, fx + fw + 2, y0 + fh + 2, 3, -1, C_AMBER, 2);
                hole(fx, y0, fx + fw - 1, y0 + fh - 1);
            }
        }
    }
    /* the picked frame: bigger, with what it is */
    wfg_rrect(16, 290, 703, 432, 12, C_FRAME, C_SEP, 1);
    hole(28, 300, 28 + 179, 300 + 119);
    layer(28, 300, 28 + 179, 300 + 119, "t_08", "cover");
    os_caps(226, 304, "FRAME 7", C_AMBER);
    ui_text(F_13, 226 + os_capsw("FRAME 7") + 12, 302, "IMG_6851  >  F07_6851.JPG", C_TXT2);
    os_text(D20, 225, 322, "1/320   f/6.3   ISO 320   45 mm", C_TXT);
    ui_text(F_13, 226, 352, "Portra 400   Print Cine   Paper Glossy", C_TXT2);
    ui_text(F_13, 226, 368, "Grain Normal   Halation Film   Bloom Low   Mist 1/8", C_TXT2);
    chip(226, 396, "Developed 21 Nov", -1, C_GRN, C_GRN);
    chip(354, 396, "CR2 in ROLLS", -1, C_LINE, C_TXT2);
    static const char *const k[] = { "UP/DN/L/R", "Frame", "SET", "Print", "INFO", "Roll", "PLAY", "Camera JPEG",
                                     "MENU", "Home", 0 };
    footer(k);
    end();
}

/* ------------------------------------------------------------------ 5. Settings */
static void scr_settings(void)
{
    begin("09_settings");
    wfg_glow_centre(-400, -400, -400, -400);
    wfg_background(0, WFUI_H);
    top_bar("Settings", 0);
    static const char *const cat[6] = { "Interface", "Shooting", "Roll", "Darkroom", "Paper", "System" };
    for (int i = 0; i < 6; i++)
    {
        int y0 = 54 + i * 40;
        if (!i)
        {
            wfg_rrect(16, y0, 195, y0 + 33, 8, C_SEP, -1, 0);
            wfg_rect(16, y0 + 6, 18, y0 + 27, C_AMBER);
        }
        ui_text(F_15, 32, y0 + 8, cat[i], i ? C_TXT2 : C_TXT);
    }
    struct { const char *n, *v, *sub; int grey; } rows[8] = {
        { "Camera interface", "WindyFilm", "Canon + ML menu: the camera as before, WindyFilm in the ML menu", 0 },
        { "MENU key", "Home", "MENU again on Home opens Canon's menu", 0 },
        { "PLAY shows", "Prints", "Prints: the roll's prints from ROLLS.  Camera JPEG: Canon's playback", 0 },
        { "LiveView HUD", "Full", "Full / Minimal / Canon's own", 0 },
        { 0, 0, 0, 0 }, { "Blind roll", "Off", "No looks at the shot: you see the roll only after the Darkroom", 0 },
        { "ML menu", 0, 0, 1 }, { 0, 0, 0, 0 } };
    if (CAM == WF_CAM_5D2)
    {
        rows[4].n = "Top plate"; rows[4].v = "On"; rows[4].sub = "On the back screen while you shoot through the viewfinder";
        rows[6].v = "TRASH";
    }
    else
    {
        rows[4].n = "Quick keys"; rows[4].v = "DOWN ring"; rows[4].sub = "DOWN: the ring sets ISO, then aperture";
        rows[6].v = "2-finger tap";
    }
    rows[6].sub = "Always there: the way back to every ML setting";
    for (int i = 0; i < 7; i++)
    {
        int y0 = 52 + i * 54, foc = i == 2;
        if (foc) wfg_rrect(212, y0, 703, y0 + 49, 8, C_AMBERD, C_AMBER, 1);
        ui_text(F_15, 228, y0 + 7, rows[i].n, rows[i].grey ? C_TXT2 : C_TXT);
        int vw = ui_textw(F_15, rows[i].v);
        int vx = 680 - vw;
        ui_text(F_15, vx, y0 + 7, rows[i].v, foc ? C_AMBER : rows[i].grey ? C_TXT3 : C_TXT);
        if (foc)
        {
            wfg_tri(vx - 14, y0 + 15, vx - 8, y0 + 10, vx - 8, y0 + 20, C_AMBER);
            wfg_tri(694, y0 + 15, 688, y0 + 10, 688, y0 + 20, C_AMBER);
        }
        ui_text(F_13, 228, y0 + 28, rows[i].sub, foc ? C_TXT2 : C_TXT3);
        if (!foc && i < 6) wfg_rect(228, y0 + 51, 687, y0 + 51, C_SEP);
    }
    if (CAM == WF_CAM_EOSM)
    {
        static const char *const k[] = { "UP/DN", "Row", "RING", "Value", "L/R", "Section", "MENU", "Home", 0 };
        footer(k);
    }
    else
    {
        static const char *const k[] = { "JOY", "Row", "DIAL", "Value", "PICT", "Section", "MENU", "Home", 0 };
        footer(k);
    }
    end();
}

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: os_mock OUTDIR CARD RBFDIR OSFONT EOSM|5D2\n"); return 2; }
    outdir = argv[1];
    CAM = !strcmp(argv[5], "5D2") ? WF_CAM_5D2 : WF_CAM_EOSM;
    host_init(argv[3], 0x03FF0000u, 0x03000000u);
    fake_reset();
    fake_set_cam(CAM);
    fake_set_card(argv[2]);
    if (osf_load(argv[4])) { fprintf(stderr, "no %s\n", argv[4]); return 1; }
    if (wfg_begin()) return 1;
    wfp_init_fixed();
    wfp_take();
    printf("fonts %d (%s), cans %d (%s)\n", wff_open(), wff_why(), wfc_open(), wfc_why(wfc_status()));
    if (CAM == WF_CAM_5D2) { R_FILM = "PRS400"; R_NUM = "021"; R_SHOT = 17; R_LIGHT = "Day"; R_PRINT = "Scan"; }
    else { R_FILM = "PRT160"; R_NUM = "024"; R_SHOT = 24; R_LIGHT = "Auto"; R_PRINT = "Scan"; }
    scr_home(0);
    scr_home(1);
    scr_shelf();
    scr_hud(0);
    scr_hud(1);
    if (CAM == WF_CAM_5D2) scr_topplate();
    scr_print();
    scr_sheet();
    scr_settings();
    return 0;
}
