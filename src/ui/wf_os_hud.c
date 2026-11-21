/* wf_os_hud.c - the film OS's LiveView finder (docs/MODERNUI.md M3): what it paints over LiveView.
 *
 * Three pieces, each painted by one task only (wf_dark.h): the top and the bottom band (32 px each, where ML's info bars
 * are) from ML's own bar redraw, through our lvinfo items (wf_osvf.c), and the middle between them from wf_task. The
 * modes (wf_api.h WF_VF_*):
 *   Full          top: the film pill (dot, film, ROLL nnn) left; battery + the card's shots left right.
 *                 bottom: SHUTTER, APERTURE, ISO (+ FILM when the film set it), METER (a -2 .. +2 tick scale with an
 *                 amber marker), FRAME (the big counter, frames shot / roll length; WIND ON in amber for 1 s after a
 *                 frame, END / REWOUND in red). Middle: four frame corners.
 *   Minimal       top: the film pill left, battery + counter right; bottom empty (ML's bar stays hidden).
 *   Bright lines  top: battery + counter right only; middle: a bright-line frame with a focus patch in its centre;
 *                 bottom: one strip under it (shutter, aperture, three meter lamps, frames).
 * Colours: ML's own palette indices (MLC: the browser's PX(c) = WFUI_PAL_BASE + c), COLOR_TRANSPARENT_BLACK for the
 * scrims; nothing writes the palette. Our own layout and marks (DESIGN 1: nothing of M-Film: no needle, no aperture
 * scale, no segment digits). The caller holds wf_hud_lock and the font pack (wfdr_hud_fonts). No allocation. */
#include "wf_ui_int.h"
#include "../wf_dark.h"

#define MLC(k)  ((int)(k) - WFUI_PAL_BASE)
#define H_SCR   MLC(COLOR_TRANSPARENT_BLACK)
#define H_W     MLC(COLOR_WHITE)
#define H_G     MLC(COLOR_GRAY(75))
#define H_D     MLC(COLOR_GRAY(45))
#define H_A     MLC(COLOR_ORANGE)
#define H_R     MLC(COLOR_RED)
#define H_0     MLC(0)                          /* transparent: LiveView shows */

static uint8_t *sv_cv;
static int sv_pitch;
static void hb(uint8_t *vram, int pitch) { sv_cv = wfg_cv; sv_pitch = wfg_pitch; wfg_cv = vram; wfg_pitch = pitch; }
static void he(void) { wfg_cv = sv_cv; wfg_pitch = sv_pitch; }

/* text on a baseline */
static int tb(int font, int x, int by, const char *s, int c) { return ui_text(font, x, by - ui_textasc(font), s, c); }
static int capsb(int x, int by, const char *s, int c) { return ui_caps(x, by - ui_textasc(F_L12), s, c); }

static void pill(int x0, int y0, int x1, int y1) { wfg_rrect(x0, y0, x1, y1, (y1 - y0) / 2, H_SCR, -1, 0); }

/* battery: a 22 x 11 body with three cells (ML's battery_level_bars: 0 low = one red cell, 1 = two, 2+ = three) */
#define BATT_W 26
static void batt(int x, int cy, int lev)
{
    int c = lev == 0 ? H_R : H_W;
    wfg_rrect(x, cy - 5, x + 21, cy + 5, 2, -1, lev < 0 ? H_D : c, 1);
    wfg_rect(x + 22, cy - 2, x + 23, cy + 2, lev < 0 ? H_D : c);
    int n = lev < 0 ? 0 : lev == 0 ? 1 : lev == 1 ? 2 : 3;
    for (int i = 0; i < n; i++) wfg_rect(x + 3 + i * 6, cy - 2, x + 7 + i * 6, cy + 2, c);
}

static const char *film_name(const struct wf_film *f)
{
    return !f ? "No film" : f->shortname ? f->shortname : f->name;
}

/* the frame counter's colour and its label */
static int count_col(const struct wfos_roll *h, const struct wfos_lv *v)
{
    return h->done ? H_R : v->wind ? H_A : H_W;
}

/* "25" + "/36" (roll) or the card's shots left; returns the width it takes, draws when draw */
static int counter(int x, int by, const struct wfos_roll *h, const struct wfos_lv *v, int big, int draw)
{
    char n[12], d[12];
    if (h->open)
    {
        snprintf(n, sizeof n, "%d", h->shot);
        snprintf(d, sizeof d, "/%d", h->exp);
    }
    else
    {
        if (v->shots < 0) return 0;
        snprintf(n, sizeof n, "%d", v->shots);
        d[0] = 0;
    }
    int w = ui_textw(big, n), dw = d[0] ? ui_textw(F_13, d) + 3 : 0;
    if (draw)
    {
        tb(big, x, by, n, h->open ? count_col(h, v) : H_W);
        if (d[0]) tb(F_13, x + w + 3, by, d, H_G);
    }
    return w + dw;
}

/* ------------------------------------------------------------------ top band */
void wfos_hud_top(uint8_t *vram, int pitch, int y, const struct wfos_lv *v)
{
    if (!vram) return;
    struct wfos_roll h;
    wfos_roll_info(&h);
    hb(vram, pitch);
    int cy = y + 16, by = cy + 5;
    char rl[16];
    if (v->vf != WF_VF_LINES)                    /* the film pill */
    {
        const char *nm = film_name(h.f);
        if (h.open) snprintf(rl, sizeof rl, "ROLL %03d", h.num);
        else snprintf(rl, sizeof rl, "%s", h.f ? "LOOK" : "CANON");
        int w = 30 + ui_textw(F_15, nm) + 10 + ui_capsw(rl) + 12;
        pill(8, y + 3, 8 + w, y + 28);
        wfg_ellipse(18, cy - 4, 26, cy + 4, h.f ? H_A : H_D);
        int x = 36 + tb(F_15, 36, by, nm, h.f ? H_W : H_G) + 10;
        capsb(x, by - 1, rl, h.done ? H_R : H_G);
    }
    /* right: battery, then the counter (Minimal, Bright lines) or the card's shots left (Full) */
    char s[16];
    int cw = 0;
    if (v->vf == WF_VF_FULL)
    {
        if (v->shots >= 0) snprintf(s, sizeof s, "%d", v->shots);
        else s[0] = 0;
        cw = s[0] ? ui_capsw("CARD") + 6 + ui_textw(F_15, s) : 0;
    }
    else cw = counter(0, 0, &h, v, F_D20, 0);
    int w = 12 + BATT_W + (cw ? 12 + cw : 0) + 12, x0 = 712 - w;
    pill(x0, y + 3, 712, y + 28);
    batt(x0 + 12, cy, v->batt);
    int x = x0 + 12 + BATT_W + 12;
    if (cw && v->vf == WF_VF_FULL)
    {
        x += capsb(x, by - 1, "CARD", H_G) + 6;
        tb(F_15, x, by, s, H_W);
    }
    else if (cw) counter(x, by + 2, &h, v, F_D20, 1);
    he();
}

/* ------------------------------------------------------------------ bottom band */
/* the meter as thirds of a stop from -2 to +2: 13 ticks 11 px apart, the whole stops taller, the marker under them;
   past either end the end tick's marker turns red */
#define MT_X 376
#define MT_N 13
#define MT_D 11
static int meter_pos(int m8, int *over)
{
    int t = m8 >= 0 ? (m8 * 3 + 4) / 8 : -((-m8 * 3 + 4) / 8);   /* 1/8 EV -> thirds, rounded */
    *over = t > 6 || t < -6;
    return t > 6 ? 6 : t < -6 ? -6 : t;
}
static void meter_scale(int y, int m8)
{
    for (int i = 0; i < MT_N; i++)
    {
        int x = MT_X + i * MT_D, whole = i % 3 == 0;
        wfg_rect(x, y + (whole ? 0 : 2), x + 1, y + 7, i == 6 ? H_W : whole ? H_G : H_D);
    }
    if (m8 == WFOS_NOMETER) return;
    int over, t = meter_pos(m8, &over), mx = MT_X + (t + 6) * MT_D;
    wfg_tri(mx - 4, y + 15, mx + 5, y + 15, mx, y + 10, over ? H_R : H_A);
}

static void seg(int x, int lb, int vb, const char *cap, const char *val)
{
    capsb(x, lb, cap, H_G);
    tb(F_D20, x, vb, val, H_W);
}

/* three lamps: under (a left triangle), right (a dot), over (a right triangle) */
static int lamps(int x, int cy, int m8, int draw)
{
    if (!draw) return 46;
    int over = 0, t = m8 == WFOS_NOMETER ? 99 : meter_pos(m8, &over);
    int lu = t != 99 && t < 0, lo = t != 99 && t > 0, lc = t == 0;
    int cu = lu ? (over ? H_R : H_A) : H_D, co = lo ? (over ? H_R : H_A) : H_D;
    wfg_tri(x + 8, cy - 6, x + 8, cy + 6, x, cy, cu);
    wfg_ellipse(x + 18, cy - 4, x + 26, cy + 4, lc ? H_A : H_D);
    wfg_tri(x + 36, cy - 6, x + 36, cy + 6, x + 44, cy, co);
    return 46;
}

void wfos_hud_bottom(uint8_t *vram, int pitch, int y, const struct wfos_lv *v)
{
    if (!vram || v->vf == WF_VF_MIN) return;     /* Minimal: the band stays empty (ML's bar hidden) */
    struct wfos_roll h;
    wfos_roll_info(&h);
    hb(vram, pitch);
    if (v->vf == WF_VF_LINES)
    {
        /* one strip, centred under the frame: shutter, aperture, the lamps, frames */
        int cy = y + 16, by = cy + 7;
        int wsh = ui_textw(F_D20, v->sh), wap = ui_textw(F_D20, v->ap), wc = counter(0, 0, &h, v, F_D20, 0);
        int w = 16 + wsh + 24 + wap + 24 + 46 + (wc ? 24 + wc : 0) + 16, x0 = 360 - w / 2;
        pill(x0, y + 3, x0 + w, y + 28);
        int x = x0 + 16;
        x += tb(F_D20, x, by, v->sh, H_W) + 24;
        x += tb(F_D20, x, by, v->ap, H_W) + 24;
        x += lamps(x, cy, v->meter, 1);
        if (wc) counter(x + 24, by, &h, v, F_D20, 1);
        he();
        return;
    }
    wfg_rrect(6, y + 1, 713, y + 30, 8, H_SCR, -1, 0);
    int lb = y + 10, vb = y + 27;
    seg(20, lb, vb, "SHUTTER", v->sh);
    seg(126, lb, vb, "APERTURE", v->ap);
    seg(240, lb, vb, "ISO", v->iso);
    if (v->iso_film)                             /* the film set the ISO: a small amber tag after it */
    {
        int x = 240 + ui_textw(F_D20, v->iso) + 6, tw = ui_capsw("FILM");
        wfg_rrect(x, vb - 13, x + tw + 9, vb + 1, 7, -1, H_A, 1);
        capsb(x + 5, vb - 2, "FILM", H_A);
    }
    capsb(MT_X, lb, "METER", H_G);
    meter_scale(y + 13, v->meter);
    const char *lab = !h.open ? "SHOTS" : h.done ? (h.rewound ? "REWOUND" : "END") : v->wind ? "WIND ON" : "FRAME";
    capsb(548, lb, lab, h.done ? H_R : v->wind && h.open ? H_A : H_G);
    int cw = counter(0, 0, &h, v, F_D30, 0);
    if (cw) counter(704 - cw, y + 28, &h, v, F_D30, 1);
    he();
}

/* ------------------------------------------------------------------ the middle */
static void corners(int x0, int y0, int x1, int y1, int c)
{
    const int a = 22, t = 2;
    wfg_rect(x0, y0, x0 + a, y0 + t - 1, c);  wfg_rect(x0, y0, x0 + t - 1, y0 + a, c);
    wfg_rect(x1 - a, y0, x1, y0 + t - 1, c);  wfg_rect(x1 - t + 1, y0, x1, y0 + a, c);
    wfg_rect(x0, y1 - t + 1, x0 + a, y1, c);  wfg_rect(x0, y1 - a, x0 + t - 1, y1, c);
    wfg_rect(x1 - a, y1 - t + 1, x1, y1, c);  wfg_rect(x1 - t + 1, y1 - a, x1, y1, c);
}

/* a rangefinder-style frame: 3:2, inset, two-pixel lines with short ticks into the corners, a focus patch centred */
static void bright_lines(int y0, int y1, int c, int cp)
{
    int h = y1 - y0 - 40;
    if (h > 352) h = 352;
    if (h < 60) return;
    int w = h * 3 / 2, fx0 = 360 - w / 2, fx1 = fx0 + w - 1, cy = (y0 + y1) / 2, fy0 = cy - h / 2, fy1 = fy0 + h - 1;
    wfg_rect(fx0, fy0, fx1, fy0 + 1, c);
    wfg_rect(fx0, fy1 - 1, fx1, fy1, c);
    wfg_rect(fx0, fy0, fx0 + 1, fy1, c);
    wfg_rect(fx1 - 1, fy0, fx1, fy1, c);
    wfg_rect(fx0 + 2, fy0 + 2, fx0 + 9, fy0 + 3, c);  wfg_rect(fx0 + 2, fy0 + 2, fx0 + 3, fy0 + 9, c);
    wfg_rect(fx1 - 9, fy0 + 2, fx1 - 2, fy0 + 3, c);  wfg_rect(fx1 - 3, fy0 + 2, fx1 - 2, fy0 + 9, c);
    wfg_rect(fx0 + 2, fy1 - 3, fx0 + 9, fy1 - 2, c);  wfg_rect(fx0 + 2, fy1 - 9, fx0 + 3, fy1 - 2, c);
    wfg_rect(fx1 - 9, fy1 - 3, fx1 - 2, fy1 - 2, c);  wfg_rect(fx1 - 3, fy1 - 9, fx1 - 2, fy1 - 2, c);
    wfg_rrect(360 - 28, cy - 18, 360 + 27, cy + 17, 4, -1, cp, 2);
}

void wfos_hud_mid(uint8_t *vram, int pitch, int y0, int y1, int vf)
{
    if (!vram || y1 - y0 < 40) return;
    hb(vram, pitch);
    int cx0 = 24, cx1 = 695, cy0 = y0 + 8, cy1 = y1 - 9;
    if (vf < 0)                                  /* everything a finder draws here back to 0 */
    {
        corners(cx0, cy0, cx1, cy1, H_0);
        bright_lines(y0, y1, H_0, H_0);
    }
    else if (vf == WF_VF_LINES) bright_lines(y0, y1, H_W, H_W);
    else corners(cx0, cy0, cx1, cy1, H_W);
    he();
}
