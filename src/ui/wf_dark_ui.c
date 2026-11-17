/* wf_dark_ui.c - WindyFilm Darkroom screen, roll HUD and end-of-roll notice (docs/DESIGN.md 3.6), WindyStrife's own
 * code and art. A roll is picked in the browser's Darkroom mode (src/ui/wf_ui.c, DESIGN 3.12; owner 11-17: the Roll
 * picker screen that lived here is gone).
 *
 * The pieces, all drawn with the browser's own toolkit (wf_gfx.c primitives, wf_font.c fonts, wf_cans.c can pack):
 *   Darkroom     a red safelight theme (the same palette entries recoloured): the print window on the easel at the left,
 *                the picture fading in on the paper as the develop exposes it (palette ramp only, no pixel redraw), the
 *                roll / film / "Frame n of N", a timer dial, the phase ("Exposing the paper"), "n to go - about m min";
 *                developed prints slide up to the wire along the top and hang there to dry. MENU stops. Before a run
 *                (and after a stop) the print finishes' rows Frame / Info / Date instead of the dial (UP / DOWN, LEFT /
 *                RIGHT / wheel; DESIGN 3.9) and, while no print is on the easel, a small picture of how they look.
 *                The ROLL row on top of the info block (owner 11-17, the shelf) picks the roll: the camera's own with
 *                frames first, then the shelf's (wf_shelf_now), LEFT / RIGHT / the wheel; a developed roll offers a
 *                develop again, with a Film row (SET: the browser picks a film, wfui_pick_film, and comes back).
 *   HUD         the frame counter drawn over LiveView while a roll shoots, in ML's own colour indices (the UI's palette
 *                entries are not ours outside the UI): wfdr_hud (wf_dark.c calls it from wf_task).
 * Threads: UI task only (wf_ui.c calls wfdr_*); the darkroom state comes from wf_dark_view() (wf_dark.h). */
#include "wf_ui_int.h"
#include "../wf_dark.h"
#include "../wf_print.h"                                   /* the finishes' setting values (WF_PFR_*, ...) */
#include "wf_icons.h"                                      /* wf_picons: the print rows' icons */

#define MSG_MS 2600
static const char *msg;
static int msg_until;
static void say(const char *s, int now) { msg = s; msg_until = now + MSG_MS; }

static inline int is5d2(void) { return wf_cam() == WF_CAM_5D2; }

const char *wfdr_dev_why(void)
{
    const struct wf_roll *r = wf_roll_now();
    if (wf_shelf_now()->n) return 0;                       /* the shelf: a roll waits, or one to develop again */
    if (!wf_roll_open(r)) return "Load a roll first";
    return wf_roll_left(r) > 0 ? 0 : "No frames to develop";
}
int wfdr_dev_hint_w(void) { return ui_textw(F_13, "PLAY") + 15 + ui_textw(F_13, "Darkroom"); }
int wfdr_dev_hint(int x, int y)
{
    int ok = !wfdr_dev_why(), kw = ui_textw(F_13, "PLAY") + 10;
    wfg_rrect(x, y - 1, x + kw, y + 16, 4, -1, ok ? C_AMBER : C_LINE, 1);
    ui_text(F_13, x + 5, y, "PLAY", ok ? C_AMBER : C_BAROFF);
    return x + kw + 5 + ui_text(F_13, x + kw + 5, y, "Darkroom", ok ? C_TXT : C_BAROFF);
}

static int text_r(int font, int xr, int y, const char *s, int c)
{
    int w = ui_textw(font, s);
    ui_text(font, xr - w, y, s, c);
    return w;
}

static void text_c(int font, int cx, int y, const char *s, int c)
{
    ui_text(font, cx - ui_textw(font, s) / 2, y, s, c);
}

/* letter-spaced caps (13 px): "ROLL 021", "DARKROOM", the kind line */
static int spaced_w(int font, const char *s, int sp)
{
    char b[2] = { 0, 0 };
    int w = 0;
    for (; *s; s++) { b[0] = *s; w += ui_textw(font, b) + sp; }
    return w ? w - sp : 0;
}
static void spaced(int font, int x, int y, const char *s, int c, int sp)
{
    char b[2] = { 0, 0 };
    for (; *s; s++) { b[0] = *s; x += ui_text(font, x, y, b, c) + sp; }
}
static void spaced_c(int font, int cx, int y, const char *s, int c, int sp)
{
    spaced(font, cx - spaced_w(font, s, sp) / 2, y, s, c, sp);
}

/* key hint "[KEY] word" pairs from x; returns the end */
static int hints(const char *const *k, int x, int y, int kc, int tc)
{
    for (; *k; k += 2)
    {
        int kw = ui_textw(F_13, k[0]) + 10;
        wfg_rrect(x, y - 1, x + kw, y + 16, 4, -1, C_KEYO, 1);
        ui_text(F_13, x + 5, y, k[0], kc);
        x += kw + 5;
        x += ui_text(F_13, x, y, k[1], tc) + 12;
    }
    return x;
}

static void clock_text(char *b, int n)
{
    int hm = wf_clock_hm();
    if (hm < 0) { b[0] = 0; return; }
    snprintf(b, n, "%02d:%02d", hm / 100, hm % 100);
}

/* =================================================================== darkroom */
/* palette: the fixed entries recoloured to the safelight theme; the can range holds the print ramps */
#define DR_NDEV   48                                       /* the print in the window: fades with the exposure */
#define DR_NDRY   32                                       /* prints on the wire */
#define C_DEV0    (C_CAN0)
#define C_DRY0    (C_CAN0 + DR_NDEV)
typedef char dr_pal_fits[C_DRY0 + DR_NDRY <= WFUI_NPAL ? 1 : -1];

static const uint8_t red_fixed[C_NFIXED][3] = {
    { 255, 120, 112 },   /* C_TXT */
    { 214,  76,  74 },   /* C_TXT2 */
    { 150,  44,  46 },   /* C_TXT3 */
    { 255,  58,  58 },   /* C_AMBER: the accent is the safelight */
    {  92,  14,  16 },   /* C_AMBERD */
    { 255,  58,  58 },   /* C_TEAL */
    {  92,  14,  16 },   /* C_TEALD */
    { 255, 150,  96 },   /* C_GRN: "done" in a warm tone */
    {  40,   8,  10 },   /* C_CHIPF */
    { 120,  30,  30 },   /* C_CHIPO */
    {  74,  16,  18 },   /* C_LINE */
    {  44,   9,  11 },   /* C_SEP */
    {  66,  15,  17 },   /* C_BAROFF */
    {   9,   1,   2 },   /* C_STRIP: the easel */
    {  60,  14,  16 },   /* C_HOLE: pegs */
    {  24,   5,   6 },   /* C_FRAME */
    {  18,   3,   4 },   /* C_FADE1 */
    {  13,   2,   3 },   /* C_FADE2 */
    {  36,   8,   9 },   /* C_TRACK */
    {  98,  24,  26 },   /* C_KEYO */
    { 255, 200,  60 },   /* C_WARN */
};
static const uint8_t RED_TOP[3] = { 38, 6, 8 }, RED_BOT[3] = { 11, 1, 2 };
static const uint8_t PAPER[3] = { 236, 66, 74 };           /* unexposed paper under the safelight */

static int dr_expose_shown = -1, dr_pready = -1;
static uint32_t dr_gen = 0xFFFFFFFFu;
static int dr_confirm;                                     /* MENU once while running: "MENU again to stop" */
static int dr_pfoc;                                        /* the focused row: WF_PO_* or DR_FOC_* below */
#define DR_FOC_SCR  WF_PO_N                                /* the Screen chip at the top right */
#define DR_FOC_ROLL (WF_PO_N + 1)                          /* the ROLL row */
#define DR_FOC_FILM (WF_PO_N + 2)                          /* the Film row (a develop again) */

/* the rolls the Darkroom can develop (owner 11-17, the shelf): the camera's open roll with frames first, then the shelf
 * (newest first). The pick is kept by roll number (the list is made again on every draw and key) */
struct dr_item { int num, dev, film, own, exp, state, nshot, left, ndev, cam; };
#define DR_NIT (WF_SHELF_MAX + 1)
static struct dr_item dr_it[DR_NIT];
static int dr_nit, dr_isel, dr_snum = -1, dr_scam;
static int dr_afilm;                                       /* develop again: the film picked (0 = the roll's own) */
static int dr_ret;                                         /* back from the film pick: pick, film and focus kept */
static int dr_old;                                         /* a run's "developed" from before this visit: shown as idle */

static void dr_list(void)
{
    const struct wf_roll *r = wf_roll_now();
    const struct wf_shelf *s = wf_shelf_now();
    int n = 0, cam = 0;
    if (wf_roll_open(r) && r->nshot > 0)
    {
        struct dr_item *it = &dr_it[n++];
        it->num = cam = r->num; it->dev = r->dev < 2 ? 1 : r->dev; it->film = r->film; it->own = r->own ? r->own : r->film;
        it->exp = r->exp; it->state = r->state; it->nshot = r->nshot; it->left = wf_roll_left(r);
        it->ndev = wf_roll_ndev(r); it->cam = 1;
    }
    for (int k = 0; k < s->n && n < DR_NIT; k++)
    {
        const struct wf_shelf_ent *e = &s->e[k];
        if (e->num == cam) continue;
        struct dr_item *it = &dr_it[n++];
        it->num = e->num; it->dev = e->dev; it->film = e->film; it->own = e->own; it->exp = e->exp; it->state = e->state;
        it->nshot = e->nshot; it->left = e->left; it->ndev = e->ndev; it->cam = 0;
    }
    dr_nit = n;
    dr_isel = 0;
    for (int k = 0; k < n; k++)
        if (dr_it[k].num == dr_snum && dr_it[k].cam == dr_scam) dr_isel = k;
    if (n) { dr_snum = dr_it[dr_isel].num; dr_scam = dr_it[dr_isel].cam; }
    if (dr_pfoc == DR_FOC_FILM && !(n && !dr_it[dr_isel].left)) dr_pfoc = DR_FOC_ROLL;
}
static inline const struct dr_item *dr_cur(void) { return dr_nit ? &dr_it[dr_isel] : 0; }
static inline int dr_again(void) { return dr_nit && !dr_it[dr_isel].left; }   /* developed: a develop again */
static int dr_afilm_id(void) { const struct dr_item *it = dr_cur(); return !it ? 0 : dr_afilm ? dr_afilm : it->own; }

/* developed tone of level v (0..1000): shadows to a near black red, highlights to the paper */
static void tone(int v, int out[3])
{
    static const uint8_t DARK[3] = { 16, 1, 4 };
    int v2 = v * v / 1000;
    int w = (v * 3 + v2 * 7) / 10;                         /* a gentle toe */
    for (int c = 0; c < 3; c++) out[c] = DARK[c] + (PAPER[c] - DARK[c]) * w / 1000;
}

static void dr_pal_dev(int expose)
{
    /* e = how far the picture has come up: 0 blank paper .. 1000 full contrast (ease out) */
    int e = expose < 0 ? 0 : expose > 1000 ? 1000 : expose;
    int s = 1000 - (1000 - e) * (1000 - e) / 1000;
    for (int i = 0; i < DR_NDEV; i++)
    {
        int t[3];
        tone(i * 1000 / (DR_NDEV - 1), t);
        wfp_set_rgb(C_DEV0 + i, PAPER[0] + (t[0] - PAPER[0]) * s / 1000, PAPER[1] + (t[1] - PAPER[1]) * s / 1000,
                    PAPER[2] + (t[2] - PAPER[2]) * s / 1000);
    }
    dr_expose_shown = expose;
}

static void dark_enter(void)
{
    for (int i = 0; i < C_NFIXED; i++) wfp_set_rgb(i, red_fixed[i][0], red_fixed[i][1], red_fixed[i][2]);
    for (int i = 0; i < WFUI_NBG; i++)
        wfp_set_rgb(C_BG0 + i, RED_TOP[0] + (RED_BOT[0] - RED_TOP[0]) * i / (WFUI_NBG - 1),
                    RED_TOP[1] + (RED_BOT[1] - RED_TOP[1]) * i / (WFUI_NBG - 1),
                    RED_TOP[2] + (RED_BOT[2] - RED_TOP[2]) * i / (WFUI_NBG - 1));
    for (int i = 0; i < DR_NDRY; i++)
    {
        int t[3];
        tone(i * 1000 / (DR_NDRY - 1), t);
        wfp_set_rgb(C_DRY0 + i, t[0], t[1], t[2]);
    }
    wfg_glow_centre(184, 248, -1000, -1000);               /* the safelight's glow round the easel, no floor shadow */
    static const uint8_t lamp[3] = { 200, 24, 24 };
    wfp_set_glow(lamp);
    dr_pal_dev(0);
    dr_pready = -1;
    dr_gen = 0xFFFFFFFFu;
    dr_confirm = 0;
    msg = 0;
    dr_old = wf_dark_view()->state == WF_DR_DONE;          /* the last run's end: this visit picks a roll again */
    if (dr_ret) { dr_ret = 0; dr_pfoc = DR_FOC_FILM; return; }   /* back from the film pick */
    dr_pfoc = 0;
    dr_snum = -1;                                          /* the camera's roll with frames, else the newest shelf roll */
    dr_afilm = 0;
    wf_shelf_rescan();                                     /* the card may be another one */
}

/* a luma image (lw x lh) drawn into (x, y, w, h) with ramp base / n levels, nearest sampling */
static void luma_blit(const uint8_t *l, int lw, int lh, int x, int y, int w, int h, int base, int n)
{
    if (!l || w <= 0 || h <= 0) return;
    for (int oy = 0; oy < h; oy++)
    {
        int yy = y + oy;
        if (yy < 0 || yy >= WFUI_H) continue;
        const uint8_t *src = l + (oy * lh / h) * lw;
        uint8_t *row = wfg_cv + yy * wfg_pitch;
        const uint8_t *bay = wfg_bayer[yy & 3];
        for (int ox = 0; ox < w; ox++)
        {
            int xx = x + ox;
            if (xx < 0 || xx >= WFUI_W) continue;
            int v = src[ox * lw / w] * (n - 1) * 16 / 255 + bay[xx & 3];   /* ordered dither between levels */
            int k = v >> 4;
            row[xx] = PX(base + (k > n - 1 ? n - 1 : k));
        }
    }
}

static void dr_ppreview(void);

/* the print window: easel frame, paper, the print */
#define PR_X 40
#define PR_Y 146
static void dr_easel(const struct wf_dark_view *v, int now)
{
    wfg_rrect(18, 120, 350, 374, 8, C_STRIP, C_LINE, 1);                 /* easel */
    wfg_rect(30, 134, 338, 362, C_DEV0 + DR_NDEV - 1);                  /* paper (the brightest ramp step = paper) */
    wfg_rect(24, 126, 344, 128, C_FRAME);                                /* easel blades */
    wfg_rect(24, 368, 344, 370, C_FRAME);
    if (v->print_ready && v->print)
        luma_blit(v->print, WF_DR_PW, WF_DR_PH, PR_X, PR_Y, WF_DR_PW, WF_DR_PH, C_DEV0, DR_NDEV);
    else if (v->state != WF_DR_RUN && v->state != WF_DR_STOPPING && v->state != WF_DR_DONE)
        dr_ppreview();                                     /* no print yet: how the finishes will look */
}

/* the wire with the hung prints; the newest one slides up from the easel for 700 ms */
static void dr_wire(const struct wf_dark_view *v, int now)
{
    wfg_rect(14, 46, WFUI_W - 15, 46, C_LINE);
    for (int px = 20; px < WFUI_W - 16; px += 4) wfg_rect(px, 47, px + 1, 47, C_SEP);   /* the wire's shadow */
    int n = v->nwire > WF_DR_NWIRE ? WF_DR_NWIRE : v->nwire;
    int t = now - v->hung_ms;
    int sliding = n && t >= 0 && t < 700;
    for (int i = 0; i < n; i++)
    {
        int sx = 18 + i * 86, sy = 54;
        int last = i == n - 1;
        if (last && sliding) continue;
        luma_blit(v->wire[i], WF_DR_TW, WF_DR_TH, sx, sy, WF_DR_TW, WF_DR_TH, C_DRY0, DR_NDRY);
        wfg_rrect(sx + WF_DR_TW / 2 - 4, 42, sx + WF_DR_TW / 2 + 4, 57, 2, C_HOLE, C_LINE, 1);   /* the peg */
        char b[8];
        snprintf(b, sizeof b, "%d", v->wire_frame[i] + 1);
        text_c(F_13, sx + WF_DR_TW / 2, sy + WF_DR_TH + 2, b, C_TXT3);
    }
}

static void dr_slide(const struct wf_dark_view *v, int now)
{
    int n = v->nwire > WF_DR_NWIRE ? WF_DR_NWIRE : v->nwire;
    int t = now - v->hung_ms;
    if (n && t >= 0 && t < 700)
    {
        int k = t * 1000 / 700;
        k = 1000 - (1000 - k) * (1000 - k) / 1000;         /* ease out */
        int i = n - 1;
        int x0 = PR_X, y0 = PR_Y, w0 = WF_DR_PW, h0 = WF_DR_PH;
        int x1 = 18 + i * 86, y1 = 54, w1 = WF_DR_TW, h1 = WF_DR_TH;
        int x = x0 + (x1 - x0) * k / 1000, y = y0 + (y1 - y0) * k / 1000;
        int w = w0 + (w1 - w0) * k / 1000, h = h0 + (h1 - h0) * k / 1000;
        luma_blit(v->wire[i], WF_DR_TW, WF_DR_TH, x, y, w, h, C_DRY0, DR_NDRY);
    }
}

/* 60-step sine, Q12 */
static const int16_t sin60[60] = {
       0,  428,  852, 1266, 1666, 2048, 2408, 2741, 3044, 3314, 3547, 3741, 3896, 4006, 4074, 4096, 4074, 4006,
    3896, 3741, 3547, 3314, 3044, 2741, 2408, 2048, 1666, 1266,  852,  428,    0, -428, -852,-1266,-1666,-2048,
   -2408,-2741,-3044,-3314,-3547,-3741,-3896,-4006,-4074,-4096,-4074,-4006,-3896,-3741,-3547,-3314,-3044,-2741,
   -2408,-2048,-1666,-1266, -852, -428 };
static inline int s60(int i) { return sin60[((i % 60) + 60) % 60]; }
static inline int c60(int i) { return s60(i + 15); }

/* the darkroom timer: 60 s face, a sweep hand for the seconds in this frame, a filled ring for the frame's progress */
static void dr_dial(int cx, int cy, int r, int ms, int prog, int show_t)
{
    wfg_ellipse(cx - r, cy - r, cx + r, cy + r, C_LINE);
    wfg_ellipse(cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3, C_STRIP);
    int lit = prog * 60 / 1000;
    for (int i = 0; i < 60; i++)
    {
        int big = i % 5 == 0, r0 = r - (big ? 13 : 9), r1 = r - 6;
        int xa = cx + s60(i) * r0 / 4096, ya = cy - c60(i) * r0 / 4096;
        int xb = cx + s60(i) * r1 / 4096, yb = cy - c60(i) * r1 / 4096;
        int c = i < lit ? C_AMBER : big ? C_TXT3 : C_LINE;
        if (big) wfg_tri(xa, ya, xb - 1, yb, xb + 1, yb, c);
        int16_t q[8] = { (int16_t)xa, (int16_t)ya, (int16_t)xb, (int16_t)yb, (int16_t)(xb + 1), (int16_t)yb,
                         (int16_t)(xa + 1), (int16_t)ya };
        wfg_poly(q, 4, c);
    }
    int sec = ms / 1000, sub = (ms % 1000) * 60 / 1000;     /* the hand moves in 1/60 s steps of its own 60 marks */
    (void)sub;
    int a = sec % 60, rh = r - 16;
    int tx = cx + s60(a) * rh / 4096, ty = cy - c60(a) * rh / 4096;
    int bx = cx - s60(a) * 10 / 4096, by = cy + c60(a) * 10 / 4096;
    int nx = c60(a) * 2 / 4096, ny = s60(a) * 2 / 4096;      /* a 2 px half-width across the hand */
    int16_t hand[8] = { (int16_t)(bx - nx), (int16_t)(by - ny), (int16_t)(tx - nx / 2), (int16_t)(ty - ny / 2),
                        (int16_t)(tx + nx / 2), (int16_t)(ty + ny / 2), (int16_t)(bx + nx), (int16_t)(by + ny) };
    wfg_poly(hand, 4, C_TXT);
    wfg_ellipse(cx - 4, cy - 4, cx + 4, cy + 4, C_AMBER);
    if (show_t)
    {
        char b[12];
        snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60);
        text_c(F_13, cx, cy + r / 2 - 4, b, C_TXT2);
    }
}

static const char *phase_text(int ph)
{
    switch (ph)
    {
    case WF_DP_LOAD:   return "Loading the negative";
    case WF_DP_EXPOSE: return "Exposing the paper";
    case WF_DP_FIX:    return "In the fixer";
    case WF_DP_DRY:    return "Washing and drying";
    case WF_DP_MIX:    return "Mixing the chemistry";
    }
    return "";
}

/* the print finishes (owner 11-15, DESIGN 3.9; Camera JPEG 11-16; RAW files 11-18): five rows in the browser's style
 * while no run lives; UP / DOWN pick one, LEFT / RIGHT / the wheel change it (wf_menu.c keeps them and saves the
 * config). RAW files is the menu's RAW after Darkroom (the same setting): with Camera JPEG Keep it is greyed with
 * "needs Overwrite" in place of its value, and its keys only say why (the CR2 stays in DCIM then) */
#define DR_RX0 384
#define DR_RX1 696
#define DR_RY  230                                 /* (owner 11-17: under the ROLL row, the film / Film row and the */
                                                   /* status line: the PRINT label clear of the status at 172) */
#define DR_RH  25

static void dr_prows(void)
{
    spaced(F_13, DR_RX0 + 4, DR_RY - 22, "PRINT", C_TXT3, 3);
    for (int k = 0; k < WF_PO_N; k++)
    {
        char b[24];
        int y = DR_RY + k * DR_RH, foc = k == dr_pfoc, av = wf_popt_avail(k);
        if (foc)
        {
            wfg_rrect(DR_RX0, y - 1, DR_RX1, y + 19, 6, C_AMBERD, -1, 0);
            wfg_rrect(DR_RX0, y - 1, DR_RX0 + 3, y + 19, 2, C_AMBER, -1, 0);
        }
        wfg_bits16(wf_picons[k], DR_RX0 + 12, y + 2, !av ? (foc ? C_TXT3 : C_BAROFF) : foc ? C_AMBER : C_TXT3);
        ui_text(F_15, DR_RX0 + 36, y + 1, wf_popt_name(k), !av ? C_TXT3 : foc ? C_TXT : C_TXT2);
        int note = k == WF_PO_RAW && !av, arw = foc && !note;   /* (a greyed RAW files row: no value to change) */
        const char *val = note ? "needs Overwrite" : wf_popt_text(k, b, sizeof b);
        int vx = arw ? DR_RX1 - 16 : DR_RX1 - 4;
        int xl = vx - text_r(F_15, vx, y + 1, val, !av ? C_TXT3 : foc ? C_TXT : C_TXT2);
        if (arw)
        {
            wfg_tri(DR_RX1 - 10, y + 5, DR_RX1 - 5, y + 10, DR_RX1 - 10, y + 15, C_AMBER);
            wfg_tri(xl - 7, y + 5, xl - 12, y + 10, xl - 7, y + 15, C_AMBER);
        }
        else if (k != WF_PO_N - 1 && k + 1 != dr_pfoc)
            wfg_rect(DR_RX0 + 36, y + 22, DR_RX1 - 4, y + 22, C_SEP);
    }
}

/* the easel before a print: what the finishes look like, drawn small on the paper (the picture a plain scene) */
static void dr_ppreview(void)
{
    char b[24];
    int pf = wf_pr_frame, pi = wf_pr_info;
    int fr = pf == WF_PFR_WHITE || pf == WF_PFR_WHITE_W ? 1 : pf == WF_PFR_BLACK || pf == WF_PFR_BLACK_W ? 2 : 0;
    int wide = pf == WF_PFR_WHITE_W || pf == WF_PFR_BLACK_W;
    int info = fr && (pi == WF_PIN_ON || pi == WF_PIN_ROLL), roll = info && pi == WF_PIN_ROLL;
    int date = wf_pr_date > WF_PDT_OFF && wf_pr_date < WF_PDT_N;
    const char *dt = wf_popt_text(WF_PO_DATE, b, sizeof b);
    /* proportions of the print (wf_print.h, at 3456 px): border 112 / 256, caption 464 */
    int bd = fr ? (wide ? 256 : 112) : 0, bb = bd + (info ? 464 : 0);
    int dw = 5184 + 2 * bd, dh = 3456 + bd + bb;
    int k = 288 * 1000 / dw < 212 * 1000 / dh ? 288 * 1000 / dw : 212 * 1000 / dh;   /* scale, 1/1000 */
    int w = dw * k / 1000, h = dh * k / 1000, x0 = 184 - w / 2, y0 = 248 - h / 2;
    int px0 = x0 + bd * k / 1000, py0 = y0 + bd * k / 1000, pw = 5184 * k / 1000, ph = 3456 * k / 1000;
    int paper = C_DRY0 + DR_NDRY - 1, ink = C_DRY0 + 2;
    if (fr) wfg_rect(x0, y0, x0 + w - 1, y0 + h - 1, fr == 1 ? paper : C_DRY0);
    for (int y = 0; y < ph; y++)                           /* sky to the horizon, then the ground */
    {
        int t = y < ph * 3 / 5 ? DR_NDRY - 6 - y * 10 / (ph * 3 / 5) : 9 - (y - ph * 3 / 5) * 5 / (ph - ph * 3 / 5);
        wfg_rect(px0, py0 + y, px0 + pw - 1, py0 + y, C_DRY0 + t);
    }
    wfg_ellipse(px0 + pw * 2 / 3, py0 + ph / 6, px0 + pw * 2 / 3 + ph / 6, py0 + ph / 6 + ph / 6, paper);   /* the sun */
    if (info)
    {
        int fg = fr == 1 ? ink : paper - 4, fg2 = fr == 1 ? C_DRY0 + 9 : C_DRY0 + 22, ty = py0 + ph + (bb - bd) * k / 2000;
        wfg_rect(px0, ty - 4, px0 + pw * 45 / 100, ty - 2, fg);           /* line 1 */
        if (roll) wfg_rect(px0 + pw * 80 / 100, ty - 4, px0 + pw - 1, ty - 2, fg2);
        wfg_rect(px0, ty + 2, px0 + pw * 38 / 100, ty + 3, fg2);          /* lines 2 and 3 */
        wfg_rect(px0, ty + 7, px0 + pw * 55 / 100, ty + 8, fg2);
    }
    if (date)
        text_r(F_13, px0 + pw - pw * 7 / 100, py0 + ph - ph * 7 / 100 - ui_texth(F_13), dt, C_GRN);
}

static void dots_c(int font, int cx, int y, const char *const *s, int n, int gap, int c, int dc);
/* the ROLL row (owner 11-17, the shelf): "< ROLL 021 . on the shelf >" and "2/4" at the right; LEFT / RIGHT / the
 * wheel pick another roll while it has the focus */
#define DR_SY 110
#define DR_FY 138
#define DR_IY (DR_RY + (WF_PO_N - 1) * DR_RH + 35)   /* the idle block's two lines under the PRINT rows (365, 387) */
#define DR_MY 410                                    /* a short message (say): under them, over the footer line */
static void dr_arrows(int y, int x0, int x1, int on)
{
    wfg_tri(x1 - 10, y + 5, x1 - 5, y + 10, x1 - 10, y + 15, on ? C_AMBER : C_LINE);
    wfg_tri(x0 + 10, y + 5, x0 + 5, y + 10, x0 + 10, y + 15, on ? C_AMBER : C_LINE);
}
static void dr_rollrow(int foc)
{
    const struct dr_item *it = dr_cur();
    char b[48], c[24];
    int cx = (DR_RX0 + DR_RX1) / 2;
    if (foc)
    {
        wfg_rrect(DR_RX0, DR_SY - 1, DR_RX1, DR_SY + 19, 6, C_AMBERD, -1, 0);
        wfg_rrect(DR_RX0, DR_SY - 1, DR_RX0 + 3, DR_SY + 19, 2, C_AMBER, -1, 0);
    }
    dr_arrows(DR_SY, DR_RX0 + 4, DR_RX1, foc && dr_nit > 1);
    if (!it) { text_c(F_15, cx, DR_SY + 1, "No rolls to develop", C_TXT3); return; }
    const char *s[2] = { b, it->cam ? "in the camera" : it->left ? "on the shelf" : "developed" };
    snprintf(b, sizeof b, "ROLL %03d", it->num);
    dots_c(F_15, cx - 8, DR_SY + 1, s, 2, 18, foc ? C_TXT : C_TXT2, foc ? C_AMBER : C_TXT3);
    if (dr_nit > 1)
    {
        snprintf(c, sizeof c, "%d/%d", dr_isel + 1, dr_nit);
        text_r(F_13, DR_RX1 - 18, DR_SY + 3, c, C_TXT3);
    }
}
/* the Film row of a develop again: the roll's own film, or the one picked in the browser (SET) */
static void dr_filmrow(int foc)
{
    const struct wf_film *f = wf_film_by_id(dr_afilm_id());
    int y = DR_FY;
    if (foc)
    {
        wfg_rrect(DR_RX0, y - 1, DR_RX1, y + 19, 6, C_AMBERD, -1, 0);
        wfg_rrect(DR_RX0, y - 1, DR_RX0 + 3, y + 19, 2, C_AMBER, -1, 0);
    }
    ui_text(F_15, DR_RX0 + 12, y + 1, "Film", foc ? C_TXT : C_TXT2);
    int xr = DR_RX1 - 4;
    if (foc)
    {
        static const char *const k[] = { "SET", "Pick", 0 };
        int w = hints(k, 2000, y + 2, C_TXT2, C_TXT3) - 2000 - 12;
        hints(k, DR_RX1 - 6 - w, y + 2, C_TXT2, C_TXT3);
        xr = DR_RX1 - 18 - w;
    }
    const char *nm = f ? f->name : "?";
    int maxw = xr - (DR_RX0 + 64);
    if (ui_textw(F_15, nm) > maxw && f && f->shortname) nm = f->shortname;
    text_r(F_15, xr, y + 1, nm, dr_afilm && dr_afilm != dr_cur()->own ? C_AMBER : foc ? C_TXT : C_TXT2);
}
/* the idle info block (no run, or a run's end from before): the ROLL row, the film (or the Film row), what it does */
static void dr_idle(const struct wf_dark_view *v)
{
    const struct dr_item *it = dr_cur();
    int cx = 540;
    char b[64], nm[12];
    dr_rollrow(dr_pfoc == DR_FOC_ROLL);
    if (!it)
    {
        text_c(F_15, cx, 172, "Nothing to develop", C_TXT3);
        dr_prows();
        text_c(F_15, cx, DR_IY, "Shoot a roll first", C_TXT);
        text_c(F_13, cx, DR_IY + 22, "WindyFilm in Darkroom mode: SET on a film loads one", C_TXT2);
        return;
    }
    int again = dr_again(), left = again ? it->nshot : it->left;
    if (again) dr_filmrow(dr_pfoc == DR_FOC_FILM);
    else
    {
        const struct wf_film *f = wf_film_by_id(it->film);
        text_c(F_ML28, cx, DR_FY, f ? f->name : "?", C_TXT);
    }
    if (again)                                             /* "Developed 12/12 . develop again" */
    {
        const char *s[2] = { b, "develop again" };
        if (it->ndev) snprintf(b, sizeof b, "Developed %d/%d", it->ndev, it->nshot);
        else snprintf(b, sizeof b, "Closed, %d frame%s", it->nshot, it->nshot == 1 ? "" : "s");
        dots_c(F_15, cx, 172, s, 2, 18, C_AMBER, C_AMBER);
    }
    else
    {
        if (it->dev >= 2) snprintf(b, sizeof b, "Develop %d: %d frame%s to develop", it->dev, left, left == 1 ? "" : "s");
        else snprintf(b, sizeof b, "%d frame%s to develop", left, left == 1 ? "" : "s");
        text_c(F_15, cx, 172, b, C_AMBER);
    }
    dr_prows();                                            /* the print finishes (no run: they can change) */
    if (!dr_old && (v->state == WF_DR_STOPPED || v->state == WF_DR_FAIL))
        text_c(F_15, cx, DR_IY, v->msg && *v->msg ? v->msg : "Stopped", v->state == WF_DR_FAIL ? C_WARN : C_TXT);
    else if (it->cam)
        text_c(F_15, cx, DR_IY, "Paper and chemistry ready", C_TXT);
    else
    {
        wf_roll_name(it->num, again ? it->dev + 1 : it->dev, nm);
        snprintf(b, sizeof b, "Prints go to ROLLS/%s", nm);
        text_c(F_15, cx, DR_IY, b, C_TXT);
    }
    int m = (wf_dark_eta_left(v, left) + 59) / 60;
    snprintf(b, sizeof b, "About %d min for the roll", m < 1 ? 1 : m);
    text_c(F_13, cx, DR_IY + 22, b, C_TXT2);
}

static void dr_right(const struct wf_dark_view *v, int now)
{
    if (v->state == WF_DR_IDLE || v->state == WF_DR_STOPPED || v->state == WF_DR_FAIL || (dr_old && v->state == WF_DR_DONE))
    {
        dr_idle(v);
        return;
    }
    const struct wf_roll *r = wf_roll_now();
    int vn = v->num ? v->num : r->num, vf = v->num ? v->film : r->film, vt = v->num ? v->total : r->nshot;
    const struct wf_film *f = wf_film_by_id(vf);
    int cx = 540;
    char b[64], nm[12];
    snprintf(b, sizeof b, "ROLL %03d", vn);
    spaced_c(F_13, cx, 122, b, C_TXT3, 3);
    text_c(F_ML28, cx, 138, f ? f->name : "?", C_TXT);
    int left = v->left;
    int eta = wf_dark_eta_s(v);
    if (v->state == WF_DR_RUN || v->state == WF_DR_STOPPING)
    {
        if (v->frame < 0) snprintf(b, sizeof b, "Getting ready");   /* the look's effects first (WF_DP_MIX) */
        else snprintf(b, sizeof b, "Frame %d of %d", v->frame + 1, vt);
        text_c(F_15, cx, 172, b, C_AMBER);
        dr_dial(cx, 250, 58, v->t_frame, v->prog, 1);
        text_c(F_15, cx, 318, v->state == WF_DR_STOPPING ? "Stopping after this step" : phase_text(v->phase), C_TXT);
        int m = (eta + 59) / 60, togo = v->frame < 0 ? left : left - 1;   /* getting ready: no frame in work yet */
        if (v->frame < 0) snprintf(b, sizeof b, "%d to develop - about %d min", togo, m < 1 ? 1 : m);
        else if (togo > 0) snprintf(b, sizeof b, "%d to go - about %d min", togo, m < 1 ? 1 : m);
        else snprintf(b, sizeof b, "Last frame - about %d min", m < 1 ? 1 : m);
        text_c(F_13, cx, 340, b, C_TXT2);
        wfg_rrect(cx - 100, 362, cx + 100, 365, 2, C_TRACK, -1, 0);
        wfg_rrect(cx - 100, 362, cx - 100 + 200 * v->prog / 1000, 365, 2, C_AMBER, -1, 0);
    }
    else                                                   /* WF_DR_DONE */
    {
        text_c(F_15, cx, 172, v->num && v->dev >= 2 ? "Roll developed again" : "Roll developed", C_GRN);
        dr_dial(cx, 250, 58, 0, 1000, 0);
        wf_roll_name(vn, v->num ? v->dev : 1, nm);
        snprintf(b, sizeof b, "%d prints in ROLLS/%s", v->num ? v->done : wf_roll_ndev(r), nm);
        text_c(F_15, cx, 318, b, C_TXT);
        text_c(F_13, cx, 340, "Copy them from the card's ROLLS folder", C_TXT2);
    }
}

/* Screen Off (owner 11-16, DESIGN 3.11): the screen's one control for it, a chip at the top right. Idle: UP from the
 * first print row (or DOWN from the last) focuses it; LEFT / RIGHT / the wheel switch it. While a run lives it is the
 * only control: always focused, the same keys. The icon is an LCD on its foot: lit for On, an empty outline for Off. */
static int dr_chip(int xr, int foc)                        /* right-aligned at xr; returns its width */
{
    int off = wf_dk_scr == WF_DS_OFF, pad = foc ? 20 : 12;
    const char *t = off ? "Screen Off" : "Screen On";
    int w = pad + 18 + 8 + ui_textw(F_13, t) + pad, x0 = xr - w, ix = x0 + pad;
    int ic = foc ? C_AMBER : off ? C_TXT3 : C_TXT2;
    wfg_rrect(x0, 5, xr, 27, 11, foc ? C_AMBERD : C_CHIPF, foc ? C_AMBER : C_CHIPO, 1);
    wfg_rrect(ix, 9, ix + 17, 20, 2, off ? -1 : ic, ic, 1);  /* the screen */
    wfg_rect(ix + 7, 21, ix + 10, 22, ic);                   /* its foot */
    wfg_rect(ix + 4, 23, ix + 13, 23, ic);
    ui_text(F_13, ix + 26, 8, t, foc ? C_TXT : off ? C_TXT3 : C_TXT2);
    if (foc)
    {
        wfg_tri(xr - 13, 11, xr - 8, 16, xr - 13, 21, C_AMBER);
        wfg_tri(x0 + 13, 11, x0 + 8, 16, x0 + 13, 21, C_AMBER);
    }
    return w;
}

static void dr_top(const struct wf_dark_view *v)
{
    wfg_ellipse(16, 9, 30, 23, C_AMBER);                              /* the safelight lamp */
    wfg_ellipse(19, 12, 27, 20, C_TXT);
    spaced(F_13, 40, 10, "DARKROOM", C_AMBER, 3);
    char ck[8], b[24];
    clock_text(ck, sizeof ck);
    int run = v->state == WF_DR_RUN || v->state == WF_DR_STOPPING;
    int xr = WFUI_W - 12;
    xr -= dr_chip(xr, run || ((v->state != WF_DR_DONE || dr_old) && dr_pfoc == DR_FOC_SCR)) + 16;
    if (ck[0]) xr -= text_r(F_15, xr, 8, ck, C_TXT2) + 14;
    if (v->battery >= 0)
    {
        snprintf(b, sizeof b, "%d%%", v->battery);
        int bc = v->battery <= 15 ? C_WARN : C_TXT2;
        int w = text_r(F_13, xr, 10, b, bc);
        int bx = xr - w - 30;
        wfg_rrect(bx, 11, bx + 22, 22, 2, -1, bc, 1);
        wfg_rect(bx + 23, 14, bx + 24, 19, bc);
        int fw = 18 * (v->battery > 100 ? 100 : v->battery) / 100;
        if (fw > 0) wfg_rect(bx + 2, 13, bx + 2 + fw - 1, 20, bc);
    }
}

static void dr_footer(const struct wf_dark_view *v)
{
    static const char *const k_run[] = { "MENU", "Stop", 0 };
    static const char *const k_conf[] = { "MENU", "Stop now", "SET", "Go on", 0 };
    static const char *const k_idle[] = { "SET", "Start", "MENU", "Back", 0 };
    static const char *const k_done[] = { "SET", "Another roll", "MENU", "Close", 0 };
    static const char *const k_film[] = { "SET", "Pick the film", "MENU", "Back", 0 };
    static const char *const k_again[] = { "SET", "Develop again", "MENU", "Back", 0 };
    static const char *const k_none[] = { "MENU", "Back", 0 };   /* an empty shelf: SET has nothing to start */
    const char *const *k = v->state == WF_DR_RUN ? (dr_confirm ? k_conf : k_run)
                         : v->state == WF_DR_DONE && !dr_old ? k_done : dr_pfoc == DR_FOC_FILM ? k_film
                         : !dr_nit ? k_none : dr_again() ? k_again : k_idle;
    hints(k, 16, 455, C_TXT2, C_TXT3);
    int run = v->state == WF_DR_RUN || v->state == WF_DR_STOPPING;
    const char *h = run && wf_dk_scr == WF_DS_OFF ? "Screen Off: any key wakes it for 10 s"
                  : run ? "Keep the camera on"
                  : v->state == WF_DR_DONE && !dr_old ? "" : "The shutter is locked while the darkroom works";
    text_r(F_13, WFUI_W - 16, 455, h, C_TXT3);
}

static void dark_render(int now)
{
    const struct wf_dark_view *v = wf_dark_view();
    if (v->expose != dr_expose_shown || v->print_ready != dr_pready)
    {
        dr_pal_dev(v->print_ready == 2 || v->state == WF_DR_DONE ? 1000 : v->print_ready ? v->expose : 0);
        dr_pready = v->print_ready;
    }
    if (dr_old && v->state != WF_DR_DONE) dr_old = 0;      /* a new run: its own end shows */
    dr_list();
    wfg_background(0, WFUI_H);
    dr_top(v);
    dr_wire(v, now);
    dr_easel(v, now);
    dr_slide(v, now);
    dr_right(v, now);
    if (msg && now - msg_until < 0) text_c(F_13, 540, DR_MY, msg, C_WARN);
    wfg_rect(12, 440, WFUI_W - 13, 440, C_SEP);
    dr_footer(v);
}

static int dr_dark;                                        /* the display reads dark (a Screen Off run): no redraws */
static int dark_key(int key, int now)
{
    const struct wf_dark_view *v = wf_dark_view();
    int run = v->state == WF_DR_RUN || v->state == WF_DR_STOPPING;
    if (wf_dark_wake(now))                                 /* Screen Off, dark: this key only wakes it */
    {
        dr_dark = 0;
        return 0;
    }
    int lr = key == MODULE_KEY_PRESS_LEFT || key == MODULE_KEY_WHEEL_LEFT || key == MODULE_KEY_WHEEL_UP
          || key == MODULE_KEY_PRESS_RIGHT || key == MODULE_KEY_WHEEL_RIGHT || key == MODULE_KEY_WHEEL_DOWN;
    int neg = key == MODULE_KEY_PRESS_LEFT || key == MODULE_KEY_WHEEL_LEFT || key == MODULE_KEY_WHEEL_UP;
    int idle = !run && (v->state != WF_DR_DONE || dr_old);  /* the rows work (a run's end: SET goes back to them) */
    dr_list();
    if (lr && (run || (idle && dr_pfoc == DR_FOC_SCR)))
    {
        wf_dk_scr_step(1);                                 /* two values: either way switches */
        return 0;
    }
    switch (key)
    {
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER:
        if (run) { dr_confirm = 0; break; }
        if (!idle) { dr_old = 1; dr_pfoc = DR_FOC_ROLL; break; }   /* "Another roll": the ROLL row */
        if (dr_pfoc == DR_FOC_FILM)                        /* develop again: the browser picks the film */
        {
            const struct dr_item *it = dr_cur();
            dr_ret = 1;
            wfui_pick_film(dr_afilm_id(), it->num, it->dev + 1);
            break;
        }
        {
            const struct dr_item *it = dr_cur();
            int rc = !it ? -1 : it->cam ? wf_dark_start() : wf_dark_start_roll(it->num, dr_again(), dr_again() ? dr_afilm : 0);
            if (rc == -1) say("Nothing to develop", now);
            else if (rc == -3) say("Not now: the camera is busy", now);
            else if (rc == 0) dr_old = 0;
        }
        break;
    case MODULE_KEY_PRESS_UP: case MODULE_KEY_PRESS_DOWN:  /* chip, ROLL, [Film], the print finishes (no run) */
        if (idle)
        {
            int st[WF_PO_N + 3], n = 0, i = 0;
            msg = 0;                                       /* (a message answers the last key: gone with the next) */
            st[n++] = DR_FOC_SCR;
            st[n++] = DR_FOC_ROLL;
            if (dr_again()) st[n++] = DR_FOC_FILM;
            for (int k = 0; k < WF_PO_N; k++) st[n++] = k;
            while (i < n - 1 && st[i] != dr_pfoc) i++;
            dr_pfoc = st[(i + (key == MODULE_KEY_PRESS_UP ? n - 1 : 1)) % n];
        }
        break;
    case MODULE_KEY_PRESS_LEFT: case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_UP:
    case MODULE_KEY_PRESS_RIGHT: case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_DOWN:
        if (!idle) break;
        msg = 0;
        if (dr_pfoc == DR_FOC_ROLL)                        /* another roll */
        {
            if (dr_nit > 1)
            {
                dr_isel = (dr_isel + (neg ? dr_nit - 1 : 1)) % dr_nit;
                dr_snum = dr_it[dr_isel].num;
                dr_scam = dr_it[dr_isel].cam;
                dr_afilm = 0;
            }
        }
        else if (dr_pfoc == WF_PO_RAW && !wf_popt_avail(WF_PO_RAW))
            say("Camera JPEG is Keep: the CR2 files stay in DCIM", now);
        else if (dr_pfoc >= 0 && dr_pfoc < WF_PO_N)
            wf_popt_step(dr_pfoc, neg ? -1 : 1);
        break;
    case MODULE_KEY_MENU: case MODULE_KEY_TRASH: case MODULE_KEY_PLAY: case MODULE_KEY_LV:
        if (!run && dr_pfoc == DR_FOC_FILM && key == MODULE_KEY_MENU) { dr_pfoc = DR_FOC_ROLL; break; }
        if (!run) return 1;
        if (v->state == WF_DR_STOPPING) break;
        if (!dr_confirm) { dr_confirm = 1; say("MENU again to stop (this frame is done again next time)", now); break; }
        dr_confirm = 0;
        wf_dark_stop();
        break;
    }
    return 0;
}

static int dark_tick(int now)
{
    const struct wf_dark_view *v = wf_dark_view();
    wf_dark_keep(now, 1);                                  /* Screen On: on again if Canon turned it off (no redraw) */
    int moved = 0, d = wf_dark_dark(now);                  /* Screen Off: turns the display off when due */
    if (d != dr_dark)
    {
        dr_dark = d;
        if (!d) moved = 1;                                 /* woken: draw what changed while dark */
    }
    if (d) return 0;
    static int last_state = -1;
    if (v->state != last_state) { last_state = v->state; msg = 0; dr_confirm = 0; moved = 1; }
    if (v->gen != dr_gen) { dr_gen = v->gen; moved = 1; }
    if (v->nwire && now - v->hung_ms >= 0 && now - v->hung_ms < 760) moved = 1;          /* sliding print */
    static int last_sec = -1;
    if ((v->state == WF_DR_RUN || v->state == WF_DR_STOPPING) && v->t_frame / 1000 != last_sec)
    {
        last_sec = v->t_frame / 1000;                      /* the dial's hand */
        moved = 1;
    }
    return moved;
}

/* =================================================================== HUD (over LiveView, ML colour indices) */
/* ui_text draws PX(c) = WFUI_PAL_BASE + c: an ML colour index k is c = k - WFUI_PAL_BASE */
#define MLC(k) ((int)(k) - WFUI_PAL_BASE)

static int hud_fonts;
int wfdr_hud_fonts(int on)
{
    if (on == 1 && !hud_fonts) { hud_fonts = 1; wff_open(); }
    else if (on == 0 && hud_fonts) { hud_fonts = 0; wff_close(); }
    else if (on == 2 && hud_fonts && wff_again()) wff_reload();   /* Canon held the card: read it again */
    return hud_fonts && wff_again();
}

/* the HUD's rectangles go back to 0 (LiveView shows through): every pixel (CLEAR), or every other one (FADE: a dither,
 * the palette has no alpha) */
static void hud_punch(int x0, int y0, int x1, int y1, int all)
{
    if (x0 < 0) x0 = 0;
    if (x1 > WFUI_W - 1) x1 = WFUI_W - 1;
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < WFUI_H; y++)
        for (int x = all ? x0 : x0 + ((x0 + y) & 1); x <= x1; x += all ? 1 : 2)
            wfg_cv[y * wfg_pitch + x] = 0;
}

/* owner 11-14: the film info shows for 2.5 s and fades (wf_dark.c times it), a small dim counter stays */
void wfdr_hud(uint8_t *vram, int pitch, int blink, int mode)
{
    const struct wf_roll *r = wf_roll_now();
    if (!wf_roll_open(r) || !vram) return;
    uint8_t *cv = wfg_cv;
    int cp = wfg_pitch;
    wfg_cv = vram;
    wfg_pitch = pitch;
    const struct wf_film *f = wf_film_by_id(r->film);
    char b[48];
    int fade = mode == WF_HUD_FADE, small = mode == WF_HUD_SMALL || mode == WF_HUD_CLEAR;
    int K = MLC(COLOR_BLACK), G = MLC(COLOR_GRAY(60)), R = MLC(COLOR_RED);
    int W = fade ? MLC(COLOR_GRAY(55)) : MLC(COLOR_WHITE), Y = fade ? MLC(COLOR_GRAY(55)) : MLC(COLOR_YELLOW);
    int left = r->exp - r->nshot;
    int full = r->state != WF_RS_LOADED || left <= 0;
    /* the geometry of the full HUD (CLEAR needs it too) */
    snprintf(b, sizeof b, "ROLL %03d  %s", r->num, f ? (f->shortname ? f->shortname : f->name) : "?");
    int w = ui_textw(F_15, b), chx1 = 14 + w + 36;
    char n1[8], n2[8];
    snprintf(n1, sizeof n1, "%d", r->nshot);
    snprintf(n2, sizeof n2, "/%d", r->exp);
    int cw = ui_textw(F_ML28, n1), sw = ui_textw(F_15, n2);
    int x1 = 690, x0 = x1 - cw - sw - 20, y0 = 384, y1 = 420;
    const char *lab = full ? (r->state == WF_RS_REWOUND ? "REWOUND" : "END OF ROLL") : blink ? "WIND ON" : 0;
    int lx0 = x1 - 6 - ui_textw(F_13, "END OF ROLL") - 2;
    int top = 90, bottom = 370;
    if (mode == WF_HUD_CLEAR)
    {
        hud_punch(12, 39, chx1, 63, 1);
        hud_punch(x0 < lx0 ? x0 : lx0, y0, x1, y1 + 20, 1);
        hud_punch(694, top, 704, bottom + 2, 1);
    }
    if (small)
    {
        /* frames shot / roll length, inside the big counter's box (FULL covers it again) */
        snprintf(b, sizeof b, "%d/%d", r->nshot, r->exp);
        int tw = ui_textw(F_13, b);
        wfg_rrect(x1 - tw - 12, 398, x1, 416, 5, K, -1, 0);
        ui_text(F_13, x1 - tw - 6, 400, b, full ? R : G);
        wfg_cv = cv;
        wfg_pitch = cp;
        return;
    }
    /* top-left chip: roll number and film */
    wfg_rrect(12, 39, chx1, 63, 6, K, -1, 0);
    wfg_rrect(18, 45, 30, 57, 3, Y, -1, 0);
    wfg_rect(21, 47, 23, 48, K);
    wfg_rect(25, 47, 27, 48, K);
    wfg_rect(21, 54, 23, 55, K);
    wfg_rect(25, 54, 27, 55, K);
    ui_text(F_15, 38, 42, b, W);
    /* bottom-right counter: frames left like a film counter window, plus the exposure ladder up the right edge */
    wfg_rrect(x0, y0, x1, y1 + (lab ? 18 : 0), 6, K, -1, 0);
    ui_text(F_ML28, x0 + 8, y0 + 2, n1, full ? R : W);
    ui_text(F_15, x0 + 10 + cw, y0 + 14, n2, G);
    if (lab) text_r(F_13, x1 - 6, y1, lab, full ? R : Y);
    int n = r->exp;
    for (int i = 0; i < n; i++)
    {
        int y = bottom - (bottom - top) * i / (n - 1 > 0 ? n - 1 : 1);
        int c = i < r->nshot ? G : i == r->nshot ? Y : W;
        int len = i % 6 == 5 ? 10 : 6;
        wfg_rect(704 - len, y, 704, y + (i == r->nshot ? 2 : 1), c);
    }
    if (fade)
    {
        hud_punch(12, 39, chx1, 63, 0);
        hud_punch(x0 < lx0 ? x0 : lx0, y0, x1, y1 + 20, 0);
        hud_punch(694, top, 704, bottom + 2, 0);
    }
    wfg_cv = cv;
    wfg_pitch = cp;
}

/* owner 11-15: the end-of-roll notice, a box in the middle of LiveView (clear of the HUD's chip, counter and ladder).
 * wf_dark.c says when (5 s when the roll ends, 2.5 s each time LiveView comes back, a key drops it). Text pieces are
 * joined by small square dots (the font is ASCII only). */
#define NT_X0 96
#define NT_X1 624
#define NT_Y0 118
#define NT_Y1 336
static int dots_w(int font, const char *const *s, int n, int gap)
{
    int w = 0;
    for (int i = 0; i < n; i++) w += ui_textw(font, s[i]) + (i ? gap : 0);
    return w;
}
static int nt_key_w(const char *k, const char *t) { return ui_textw(F_13, k) + 20 + ui_textw(F_13, t); }
static int nt_key(int x, int y, const char *k, const char *t, int W, int K)   /* "[KEY] text"; returns the end */
{
    int kw = ui_textw(F_13, k) + 12;
    wfg_rrect(x, y, x + kw, y + 18, 4, W, -1, 0);
    wfg_rrect(x + 1, y + 1, x + kw - 1, y + 17, 3, K, -1, 0);
    ui_text(F_13, x + 6, y + 2, k, W);
    return x + kw + 8 + ui_text(F_13, x + kw + 8, y + 2, t, W);
}
static void dots_c(int font, int cx, int y, const char *const *s, int n, int gap, int c, int dc)
{
    int x = cx - dots_w(font, s, n, gap) / 2;
    for (int i = 0; i < n; i++)
    {
        if (i) { wfg_rect(x + gap / 2 - 2, y + 8, x + gap / 2 + 1, y + 11, dc); x += gap; }
        x += ui_text(font, x, y, s[i], c);
    }
}

void wfdr_notice(uint8_t *vram, int pitch, int mode)
{
    const struct wf_roll *r = wf_roll_now();
    if (!vram) return;
    uint8_t *cv = wfg_cv;
    int cp = wfg_pitch;
    wfg_cv = vram;
    wfg_pitch = pitch;
    if (mode == WF_HUD_CLEAR || (r->state != WF_RS_FULL && r->state != WF_RS_REWOUND))
    {
        hud_punch(NT_X0, NT_Y0, NT_X1, NT_Y1, 1);
        wfg_cv = cv;
        wfg_pitch = cp;
        return;
    }
    int fade = mode == WF_HUD_FADE, cx = (NT_X0 + NT_X1) / 2;
    int K = MLC(COLOR_BLACK), R = fade ? MLC(COLOR_GRAY(55)) : MLC(COLOR_RED);
    int W = fade ? MLC(COLOR_GRAY(55)) : MLC(COLOR_WHITE), Y = fade ? MLC(COLOR_GRAY(55)) : MLC(COLOR_YELLOW);
    int G = MLC(COLOR_GRAY(60)), D = MLC(COLOR_GRAY(30));
    const struct wf_film *f = wf_film_by_id(r->film);
    char a[16], c[16];
    const char *s[3];
    /* (an ML index is a negative colour here, which wfg_rrect reads as "no outline": the frames are two fills) */
    wfg_rrect(NT_X0, NT_Y0, NT_X1, NT_Y1, 12, R, -1, 0);
    wfg_rrect(NT_X0 + 3, NT_Y0 + 3, NT_X1 - 3, NT_Y1 - 3, 9, K, -1, 0);
    spaced_c(F_ML28, cx, NT_Y0 + 16, r->state == WF_RS_FULL ? "END OF ROLL" : "ROLL REWOUND", R, 4);
    wfg_rect(NT_X0 + 40, NT_Y0 + 58, NT_X1 - 40, NT_Y0 + 58, D);
    /* 12/12 . Portra 400 . ROLL 021 (rewound: 7 frames . ..., the frames still to develop after a stopped run) */
    int left = wf_roll_left(r);
    if (r->state == WF_RS_FULL) snprintf(a, sizeof a, "%d/%d", r->nshot, r->exp);
    else snprintf(a, sizeof a, "%d frame%s", left, left == 1 ? "" : "s");
    snprintf(c, sizeof c, "ROLL %03d", r->num);
    s[0] = a;
    s[1] = f ? f->name : "?";
    s[2] = c;
    if (f && f->shortname && dots_w(F_15, s, 3, 22) > NT_X1 - NT_X0 - 40) s[1] = f->shortname;
    dots_c(F_15, cx, NT_Y0 + 72, s, 3, 22, W, R);
    /* owner 11-15: the shutter is held on a finished roll (wf_dark.c); the two ways out */
    text_c(F_15, cx, NT_Y0 + 98, "Roll finished: develop it in the Darkroom, or load a new roll", W);
    const char *dv = "Develop:", *path = "open WindyFilm > Darkroom";
    int dw = ui_textw(F_15, dv) + 10, pw = ui_textw(F_15, path), x = cx - (dw + pw) / 2;
    ui_text(F_15, x, NT_Y0 + 122, dv, G);
    ui_text(F_15, x + dw, NT_Y0 + 122, path, Y);
    /* [SET] the Darkroom now, [INFO] the browser in Darkroom mode for a new roll (wf_main.c: while the notice is up) */
    x = cx - (nt_key_w("SET", "Darkroom") + 28 + nt_key_w("INFO", "New roll")) / 2;
    x = nt_key(x, NT_Y0 + 150, "SET", "Darkroom", W, K) + 28;
    nt_key(x, NT_Y0 + 150, "INFO", "New roll", W, K);
    wfg_rect(NT_X0 + 40, NT_Y0 + 181, NT_X1 - 40, NT_Y0 + 181, D);
    text_c(F_13, cx, NT_Y0 + 191, wf_roll_shutter_locked() ? "The shutter stays locked until then"
                                                           : "Shots from now on are not counted, WindyFilm keeps working", G);
    if (fade) hud_punch(NT_X0, NT_Y0, NT_X1, NT_Y1, 0);
    wfg_cv = cv;
    wfg_pitch = cp;
}

/* owner 11-17: the quick keys' box (wf_menu.c, DESIGN 3.17), at the bottom middle of LiveView: clear of the roll HUD
 * (chip top left, counter bottom right from x 570, ladder on the right edge) and of the end-of-roll notice (y < 340) */
#define TO_CX 360
#define TO_Y0 356
#define TO_Y1 426
static int to_x0 = -1, to_x1, toast_fonts;
int wfdr_toast_fonts(int on)
{
    if (on && !toast_fonts) { toast_fonts = 1; wff_open(); }
    else if (!on && toast_fonts) { toast_fonts = 0; wff_close(); }
    return toast_fonts;
}
static int to_hint_w(const char *k, const char *t) { return k ? nt_key_w(k, t ? t : "") : t ? ui_textw(F_13, t) : 0; }
static int to_hint(int x, int y, const char *k, const char *t, int W, int G, int K)
{
    if (k) return nt_key(x, y, k, t ? t : "", W, K);
    return t ? x + ui_text(F_13, x, y + 2, t, G) : x;
}
void wfdr_toast(uint8_t *vram, int pitch, const char *big, const char *k1, const char *t1, const char *k2, const char *t2,
                int mode)
{
    if (!vram) { to_x0 = -1; return; }
    uint8_t *cv = wfg_cv;
    int cp = wfg_pitch;
    wfg_cv = vram;
    wfg_pitch = pitch;
    if (to_x0 >= 0) { hud_punch(to_x0, TO_Y0, to_x1, TO_Y1, 1); to_x0 = -1; }
    if (mode != WF_HUD_CLEAR && big)
    {
        int fade = mode == WF_HUD_FADE;
        int K = MLC(COLOR_BLACK), D = MLC(COLOR_GRAY(30)), G = MLC(COLOR_GRAY(60));
        int W = fade ? MLC(COLOR_GRAY(55)) : MLC(COLOR_WHITE);
        int h1 = to_hint_w(k1, t1), h2 = to_hint_w(k2, t2), hw = h1 + h2 + (h1 && h2 ? 24 : 0);
        int bw = ui_textw(F_ML28, big), w = (bw > hw ? bw : hw) + 40;
        if (w < 180) w = 180;
        if (w > 400) w = 400;
        int x0 = TO_CX - w / 2, x1 = TO_CX + w / 2, y1 = hw ? TO_Y1 : TO_Y0 + 46;
        wfg_rrect(x0, TO_Y0, x1, y1, 10, D, -1, 0);
        wfg_rrect(x0 + 2, TO_Y0 + 2, x1 - 2, y1 - 2, 8, K, -1, 0);
        text_c(F_ML28, TO_CX, TO_Y0 + 6, big, W);
        if (hw)
        {
            int x = TO_CX - hw / 2;
            if (h1) x = to_hint(x, TO_Y0 + 46, k1, t1, W, G, K) + 24;
            if (h2) to_hint(x, TO_Y0 + 46, k2, t2, W, G, K);
        }
        if (fade) hud_punch(x0, TO_Y0, x1, y1, 0);
        to_x0 = x0;
        to_x1 = x1;
    }
    wfg_cv = cv;
    wfg_pitch = cp;
}

/* the browser's film pick for a develop again ends (wf_ui.c): film, or -1 = none (MENU); the Darkroom comes back */
void wfdr_pick_done(int film)
{
    if (film > 0) dr_afilm = film;
}

/* =================================================================== dispatch (wf_ui.c) */
void wfdr_enter(int scr)
{
    if (scr == WFUI_SCR_DARK) dark_enter();
}

void wfdr_leave(int scr)
{
    if (scr == WFUI_SCR_DARK) wfp_init_fixed();          /* the browser's colours back */
    wfg_glow_centre(125, 150, 125, 272);                    /* the browser's glow under its big can */
}

void wfdr_render(int scr, int now)
{
    if (scr == WFUI_SCR_DARK) dark_render(now);
}

int wfdr_key(int scr, int key)
{
    int now = get_ms_clock();
    if (scr == WFUI_SCR_DARK) return dark_key(key, now);
    return 0;
}

int wfdr_tick(int scr, int now)
{
    int moved = msg && now - msg_until >= 0;
    if (moved) msg = 0;
    if (scr == WFUI_SCR_DARK) moved |= dark_tick(now);
    return moved;
}

int wfdr_dark(int scr) { return scr == WFUI_SCR_DARK && dr_dark; }

#ifdef WF_HOST
const char *wfdr_host_msg(void) { return msg ? msg : ""; }
int wfdr_host_pfoc(void) { return dr_pfoc; }
int wfdr_host_sel(void) { dr_list(); return dr_nit ? dr_it[dr_isel].num : 0; }
int wfdr_host_afilm(void) { return dr_afilm_id(); }
int wfdr_host_stop_ask(void) { return dr_confirm; }
#endif
