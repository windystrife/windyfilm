/* wf_os_ui.c - WindyFilm's film OS screens (docs/MODERNUI.md, modernUI M1): Home and Settings, in the browser's UI task.
 *
 * Home comes up on the MENU key in shooting while Camera interface = WindyFilm (wf_os; wf_ui.c wf_ui_home_key) in place
 * of Canon's menu: the roll in the camera (can, ROLL nnn, film, frames shot / left on the roll gauge), four ways on
 * (Film shelf = the browser, Darkroom, Prints = Canon's PLAY, Settings) and the roll's look (Light, Print, Paper, Grain,
 * Halation, Strength: the ring / dial steps the focused one, SET / INFO opens the browser on it).
 * MENU on Home closes the UI at once and hands Canon its MENU (wf_ui.c after_close; the EOS M two-finger tap arrives as
 * MENU too and opens the ML menu instead). 5D2: TRASH on Home opens the ML menu (its only way in while WindyFilm takes
 * TRASH in shooting). Settings: the Interface section (Camera interface, Quick keys, Blind roll, the fixed keys) and a
 * System section (what the packs say). MENU there goes back to Home.
 *
 * Drawing: the browser's canvas, palette and fonts (wf_gfx.c, wf_font.c with the display sizes F_D20 / F_D30 / F_D80 /
 * F_L12, wf_cans.c). Our own layout and marks (DESIGN 1: nothing of M-Film). Threads: UI task only (wf_dark_ui.c's
 * wfdr_* pass the two screens on). No allocation. */
#include "wf_ui_int.h"
#include "../wf_dark.h"

int wf_roll_blocks_play(void);                              /* wf_core.h: blind roll, PLAY is eaten */

#define OS_MSG_MS 2500
enum { HF_SHELF = 0, HF_DARK, HF_PRINTS, HF_SET, HF_NTILE };
#define HF_LOOK  HF_NTILE                                   /* focus 4..9: the look strip */
#define NLOOK    6
static const uint8_t look_row[NLOOK] = { WF_ROW_LIGHT, WF_ROW_PRINT, WF_ROW_PAPER, WF_ROW_GRAIN, WF_ROW_HALO,
                                         WF_ROW_STRENGTH };
static const char *const look_cap[NLOOK] = { "LIGHT", "PRINT", "PAPER", "GRAIN", "HALATION", "STRENGTH" };

static int hfoc, hlook = HF_LOOK;                          /* Home's focus; the look item it last had */
static int sfoc, ssec;                                      /* Settings: row, section */
static const char *msg;
static int msg_until;
static int glow_film = -1;
static uint32_t seen_gen;

static int is5d2(void) { return wf_cam() == WF_CAM_5D2; }
static void say(const char *s) { msg = s; msg_until = get_ms_clock() + OS_MSG_MS; }

/* ------------------------------------------------------------------ pieces of the identity */
/* the WindyFilm mark: an amber tile, a spool whose leader runs out as a gust, two shorter gusts under it */
static void os_mark(int x, int y, int s)
{
    int u = s / 22 > 0 ? s / 22 : 1;
    wfg_rrect(x, y, x + s - 1, y + s - 1, s / 4, C_AMBER, -1, 0);
    wfg_ellipse(x + 3 * u, y + 5 * u, x + 13 * u, y + 15 * u, C_STRIP);
    wfg_ellipse(x + 6 * u, y + 8 * u, x + 10 * u, y + 12 * u, C_AMBER);
    wfg_rect(x + 8 * u, y + 5 * u, x + 18 * u, y + 7 * u, C_STRIP);
    wfg_rrect(x + 13 * u, y + 11 * u, x + 19 * u, y + 12 * u, u, C_STRIP, -1, 0);
    wfg_rrect(x + 11 * u, y + 15 * u, x + 17 * u, y + 16 * u, u, C_STRIP, -1, 0);
}

/* the roll gauge: one tick per frame; exposed ticks light, the next frame amber and taller, the rest dim */
static void ticks(int x, int y, int w, int h, int shot, int total)
{
    if (total < 2) return;
    for (int i = 0; i < total; i++)
    {
        int tx = x + i * (w - 2) / (total - 1);
        int c = i < shot ? C_TXT2 : i == shot ? C_AMBER : C_BAROFF;
        int hh = i == shot ? h + 4 : (i % 6 == 5 || i == 0) ? h : h - 4;
        wfg_rect(tx, y + h - hh, tx + 1, y + h - 1, c);
    }
}

static int chip(int x, int y, const char *s, int line, int tc)
{
    int w = ui_textw(F_13, s) + 16;
    wfg_rrect(x, y, x + w - 1, y + 19, 10, -1, line, 1);
    ui_text(F_13, x + 8, y + 2, s, tc);
    return w;
}

/* footer: key caps and what they do (pairs) */
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

static void top_bar(const char *title)
{
    os_mark(16, 9, 22);
    ui_text(F_D20, 48, 8, title, C_TXT);
    const char *cam = is5d2() ? "5D Mark II" : "EOS M";
    int w = ui_textw(F_13, cam) + 16;
    chip(704 - w, 10, cam, C_LINE, C_TXT2);
    wfg_rect(16, 40, 703, 40, C_SEP);
}

static void toast(void)
{
    if (!msg) return;
    int w = ui_textw(F_15, msg) + 32, x0 = (WFUI_W - w) / 2;
    wfg_rrect(x0, 288, x0 + w - 1, 315, 13, C_FRAME, C_AMBER, 1);
    ui_text(F_15, x0 + 16, 293, msg, C_TXT);
}

/* 20 x 20 line icons */
static void ic_can(int x, int y, int c)
{
    wfg_rect(x + 8, y, x + 11, y + 2, c);
    wfg_rrect(x + 3, y + 2, x + 16, y + 5, 1, c, -1, 0);
    wfg_rrect(x + 4, y + 5, x + 15, y + 16, 2, -1, c, 2);
    wfg_rrect(x + 3, y + 16, x + 16, y + 19, 1, c, -1, 0);
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

/* Strength as bars (the browser's bar row): level of max */
static void bars(int x, int y, int lev, int max, int on, int off)
{
    if (max < 1) max = 1;
    for (int i = 0; i < 5; i++)
        wfg_rect(x + i * 7, y + 12 - 2 * i, x + i * 7 + 4, y + 14, i < (lev * 5 + max - 1) / max ? on : off);
}

/* ------------------------------------------------------------------ the roll in the camera */
struct home_roll {
    const struct wf_film *f;                                /* the roll's film, else the loaded one, else 0 */
    int open, num, shot, exp, done;                         /* done: full / rewound (no more frames) */
};
static void home_roll(struct home_roll *h)
{
    const struct wf_roll *r = wf_roll_now();
    h->open = wf_roll_open(r) && !wf_roll_shelved();
    h->f = h->open ? wf_film_by_id(r->film) : 0;
    if (!h->f) h->f = wf_film_by_id(wf_loaded());
    h->num = r->num;
    h->shot = h->open ? r->nshot : 0;
    h->exp = h->open ? r->exp : 0;
    h->done = h->open && !wf_roll_can_shoot(r);
}

/* the Darkroom tile's line: frames to develop, or why not */
static const char *dark_line(char *b, int n)
{
    const char *why = wfdr_dev_why();
    if (why) return why;
    const struct wf_roll *r = wf_roll_now();
    if (wf_roll_open(r) && wf_roll_left(r) > 0) snprintf(b, n, "%d frames to develop", wf_roll_left(r));
    else snprintf(b, n, "Rolls on the shelf");
    return b;
}

static void open_tile(int t)
{
    if (t == HF_SHELF) { wfui_browse(WF_ROW_LIGHT); return; }
    if (t == HF_DARK)
    {
        const char *why = wfdr_dev_why();
        if (why) { say(why); return; }
        wf_log("ui: Home -> Darkroom");
        wfui_goto(WFUI_SCR_DARK);
        return;
    }
    if (t == HF_PRINTS)
    {
        if (wf_roll_blocks_play()) { say("Blind roll: the prints come after the Darkroom"); return; }
        wfui_close_after(WFUI_AFTER_PLAY);
        return;
    }
    wfui_goto(WFUI_SCR_SET);
}

/* ------------------------------------------------------------------ Home */
static void home_render(void)
{
    struct home_roll h;
    home_roll(&h);
    const struct wf_film *f = h.f;
    int gid = f ? f->id : 0;
    if (gid != glow_film)
    {
        uint8_t rgb[3] = { 60, 64, 72 };
        if (f) wfc_glow(f, rgb);
        wfg_glow_centre(118, 168, 118, 292);
        wfp_set_glow(rgb);
        glow_film = gid;
    }
    wfg_background(0, WFUI_H);
    top_bar("WindyFilm");

    char s[64];
    if (f) wfc_draw(f, CAN_BIG, 38, 54, C_BG0);
    else                                                    /* no film: the can's place, empty */
    {
        wfg_rrect(38, 54, 197, 279, 14, -1, C_SEP, 1);
        ui_caps(118 - ui_capsw("EMPTY") / 2, 160, "EMPTY", C_TXT3);
    }
    if (h.open)
    {
        snprintf(s, sizeof s, "ROLL %03d", h.num);
        int cw = ui_caps(222, 62, s, C_AMBER);
        wfg_rect(222 + cw + 7, 67, 222 + cw + 9, 69, C_AMBER);
        ui_caps(222 + cw + 17, 62, h.done ? "FINISHED" : "IN THE CAMERA", C_AMBER);
    }
    else ui_caps(222, 62, f ? "NO ROLL  -  THE LOOK IS ON" : "NOTHING LOADED", C_TXT3);
    ui_text_clip(F_D30, 220, 78, f ? f->name : "Choose a film", f ? C_TXT : C_TXT2, 478);
    if (f)
    {
        int x = 222;
        x += chip(x, 120, f->kind, C_LINE, C_TXT2) + 6;
        snprintf(s, sizeof s, "ISO %d", f->iso);
        x += chip(x, 120, s, C_LINE, C_TXT2) + 6;
        chip(x, 120, f->format, C_LINE, C_TXT2);
    }
    if (h.open)
    {
        snprintf(s, sizeof s, "%d", h.shot);
        int w = ui_text(F_D80, 216, 236 - ui_textasc(F_D80), s, C_TXT);
        snprintf(s, sizeof s, "/ %d", h.exp);
        ui_text(F_D20, 216 + w + 4, 236 - ui_textasc(F_D20), s, C_TXT3);
        if (h.done)
        {
            ui_text(F_15, 362, 190, "Roll finished", C_TXT);
            ui_text(F_13, 362, 212, "Develop it in the Darkroom", C_TXT2);
        }
        else
        {
            snprintf(s, sizeof s, "%d frames left", h.exp - h.shot);
            ui_text(F_15, 362, 190, s, C_TXT);
            snprintf(s, sizeof s, "Next: frame %d", h.shot + 1);
            ui_text(F_13, 362, 212, s, C_TXT2);
        }
        ticks(222, 254, 250, 14, h.shot, h.exp);
    }
    else
    {
        ui_text(F_15, 222, 170, f ? "Every shot gets this film's look." : "Shots are Canon's own.", C_TXT);
        ui_text(F_13, 222, 194, "A roll counts frames for the Darkroom:", C_TXT2);
        ui_text(F_13, 222, 212, "Film shelf, Darkroom mode, SET on a film.", C_TXT2);
    }

    /* the ways on */
    static const char *const tn[HF_NTILE] = { "Film shelf", "Darkroom", "Prints", "Settings" };
    char sb[48], db[48];
    const char *sub[HF_NTILE];
    snprintf(sb, sizeof sb, "%d films  -  load a roll", wf_nfilms);
    sub[HF_SHELF] = sb;
    sub[HF_DARK] = dark_line(db, sizeof db);
    sub[HF_PRINTS] = wf_roll_blocks_play() ? "After the Darkroom" : "Canon's playback";
    sub[HF_SET] = "Camera interface, keys";
    for (int i = 0; i < HF_NTILE; i++)
    {
        int y0 = 54 + i * 66, foc = i == hfoc;
        int bg = foc ? C_AMBERD : C_SEP, ic = foc ? C_AMBER : C_TXT2;
        wfg_rrect(492, y0, 703, y0 + 57, 10, bg, foc ? C_AMBER : -1, foc ? 2 : 0);
        if (i == HF_SHELF) ic_can(506, y0 + 19, ic);
        if (i == HF_DARK) ic_bulb(506, y0 + 19, foc ? C_AMBER : C_WARN, bg);
        if (i == HF_PRINTS) ic_prints(506, y0 + 19, ic, bg);
        if (i == HF_SET) ic_gear(506, y0 + 19, ic, bg);
        ui_text(F_D20, 538, y0 + 7, tn[i], C_TXT);
        ui_text_clip(F_13, 539, y0 + 34, sub[i], foc ? C_TXT : C_TXT2, 698);
    }

    /* the look of the roll (the loaded film's rows) */
    int lf = hfoc >= HF_LOOK;
    wfg_rrect(16, 322, 703, 422, 12, C_FRAME, lf ? C_AMBER : C_SEP, 1);
    ui_caps(32, 334, h.open ? "LOOK OF THIS ROLL" : "LOOK", C_TXT3);
    const char *hint = lf ? (is5d2() ? "DIAL  CHANGE   SET  MORE" : "RING  CHANGE   SET  MORE") : "DOWN  TO CHANGE";
    ui_caps(688 - ui_capsw(hint), 334, hint, C_TXT3);
    for (int i = 0; i < NLOOK; i++)
    {
        int cx = 32 + i * 112, row = look_row[i], foc = hfoc == HF_LOOK + i, av = f && wf_row_avail(row);
        if (i) wfg_rect(cx - 12, 362, cx - 12, 406, C_SEP);
        if (foc) wfg_rrect(cx - 8, 356, cx + 96, 412, 8, C_AMBERD, C_AMBER, 1);
        ui_caps(cx, 362, look_cap[i], foc ? C_AMBER : C_TXT3);
        int tc = !av ? C_TXT3 : foc ? C_AMBER : C_TXT;
        if (row == WF_ROW_STRENGTH)
        {
            int max = 1, lev = wf_row_level(row, &max);
            bars(cx, 384, lev, max, av ? (foc ? C_AMBER : C_TXT) : C_TXT3, C_BAROFF);
        }
        else ui_text_clip(F_D20, cx, 380, f ? wf_row_text(row) : "-", tc, cx + 98);
    }

    if (is5d2())
    {
        static const char *const k[] = { "SET", "Open", "JOY", "Choose", "MENU", "Canon menu", "HALF", "Shoot",
                                         "TRASH", "ML menu", 0 };
        footer(k);
    }
    else
    {
        static const char *const k[] = { "SET", "Open", "UP/DN", "Choose", "MENU", "Canon menu", "HALF", "Shoot",
                                         "2 fingers", "ML menu", 0 };
        footer(k);
    }
    toast();
}

static void look_step(int d)
{
    int row = look_row[hfoc - HF_LOOK];
    if (!wf_loaded()) { say("Load a film first"); return; }
    if (!wf_row_avail(row)) { say("Not used with this film"); return; }
    wf_row_step(row, d);
}

static int home_key(int k)
{
    int look = hfoc >= HF_LOOK;
    msg = 0;
    switch (k)
    {
    case MODULE_KEY_PRESS_UP:
        if (look) { hlook = hfoc; hfoc = HF_SET; }
        else hfoc = hfoc ? hfoc - 1 : hlook;
        break;
    case MODULE_KEY_PRESS_DOWN:
        if (look) hfoc = HF_SHELF;
        else if (hfoc == HF_SET) hfoc = hlook;
        else hfoc++;
        break;
    case MODULE_KEY_PRESS_LEFT:
        if (look) hfoc = HF_LOOK + (hfoc - HF_LOOK + NLOOK - 1) % NLOOK;
        break;
    case MODULE_KEY_PRESS_RIGHT:
        if (look) hfoc = HF_LOOK + (hfoc - HF_LOOK + 1) % NLOOK;
        break;
    case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_UP:
    case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_DOWN:
    {
        int d = k == MODULE_KEY_WHEEL_LEFT || k == MODULE_KEY_WHEEL_UP ? -1 : 1;
        if (look) look_step(d);
        else hfoc = (hfoc + HF_NTILE + d) % HF_NTILE;
        break;
    }
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER: case MODULE_KEY_Q:
        if (look) wfui_browse(look_row[hfoc - HF_LOOK]);
        else open_tile(hfoc);
        break;
    case MODULE_KEY_INFO:
        wfui_browse(look ? look_row[hfoc - HF_LOOK] : WF_ROW_LIGHT);
        break;
    case MODULE_KEY_MENU:                                   /* (wf_ui_key closes at once; this when a switch was pending) */
        wfui_close_after(WFUI_AFTER_MENU);
        break;
    case MODULE_KEY_PLAY:
        open_tile(HF_PRINTS);
        break;
    case MODULE_KEY_TRASH:                                  /* 5D2: the ML menu; EOS M (long DOWN): the shelf */
        if (is5d2()) wfui_close_after(WFUI_AFTER_MLMENU);
        else wfui_browse(WF_ROW_LIGHT);
        break;
    case MODULE_KEY_LV:
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ Settings */
enum { SR_OS = 0, SR_QK, SR_BLIND, SR_MLMENU, SR_MENUKEY, SR_N };
enum { SY_FILMS = 0, SY_FONTS, SY_CANS, SY_N };
static const char *const sec_name[2] = { "Interface", "System" };

static int set_rows(int sec, uint8_t *out)
{
    int n = 0;
    if (sec == 0)
    {
        out[n++] = SR_OS;
        if (!is5d2()) out[n++] = SR_QK;                     /* the quick keys are the EOS M's */
        out[n++] = SR_BLIND;
        out[n++] = SR_MLMENU;
        out[n++] = SR_MENUKEY;
    }
    else
        for (int i = 0; i < SY_N; i++) out[n++] = i;
    return n;
}

/* a row's name, value, line under it; 1 = the value can change */
static int set_row(int sec, int r, const char **name, const char **val, const char **sub, char *b, int bn)
{
    static const char *const qk_v[WF_QK_N] = { "Off", "DOWN ring", "L/R ISO" };
    static const char *const qk_s[WF_QK_N] = { "DOWN, LEFT, RIGHT and SET do what they did before",
                                               "DOWN: the ring sets ISO, DOWN again: aperture (M)",
                                               "LEFT / RIGHT: ISO, DOWN: aperture" };
    if (sec == 1)
    {
        if (r == SY_FILMS)
        {
            *name = "Films";
            snprintf(b, bn, "%d", wf_nfilms);
            *val = b;
            *sub = "In this build, Film and Film stage";
        }
        else if (r == SY_FONTS)
        {
            int d = wff_disp(), own = !(wff_status() & 1);
            *name = "Font pack";
            *val = !own ? wff_why() : d == 0xF ? "ok" : "older pack";
            *sub = own && d != 0xF ? "Copy this version's ML/DATA/WFFONTS.DAT for the big numbers" : "ML/DATA/WFFONTS.DAT";
        }
        else
        {
            *name = "Can pack";
            *val = wfc_why(wfc_status());
            *sub = "ML/DATA/WFCANS.DAT: the cans on the shelf and on Home";
        }
        return 0;
    }
    switch (r)
    {
    case SR_OS:
        *name = "Camera interface";
        *val = wf_os ? "WindyFilm" : "Canon + ML";
        *sub = wf_os ? "MENU opens this Home; MENU on Home: Canon's menu"
                     : "MENU opens Canon's menu; WindyFilm stays in the ML menu";
        return 1;
    case SR_QK:
        *name = "Quick keys";
        *val = qk_v[wf_qk >= 0 && wf_qk < WF_QK_N ? wf_qk : 0];
        *sub = qk_s[wf_qk >= 0 && wf_qk < WF_QK_N ? wf_qk : 0];
        return 1;
    case SR_BLIND:
        *name = "Blind roll";
        *val = wf_blind ? "On" : "Off";
        *sub = "On: no image review and no PLAY while a roll is in the camera";
        return 1;
    case SR_MLMENU:
        *name = "ML menu";
        *val = is5d2() ? "TRASH on Home" : "2-finger tap";
        *sub = "Always there: every ML setting, WindyFilm's too";
        return 0;
    default:
        *name = "MENU key";
        *val = wf_os ? "Home" : "Canon menu";
        *sub = "MENU on Home opens Canon's menu";
        return 0;
    }
}

static void set_step(int r, int d)
{
    if (r == SR_OS) wf_os_set(!wf_os);
    else if (r == SR_QK) wf_qk_set((wf_qk + WF_QK_N + d) % WF_QK_N);
    else if (r == SR_BLIND) wf_blind_set(!wf_blind);
}

static void set_render(void)
{
    uint8_t rows[8];
    int n = set_rows(ssec, rows);
    if (glow_film != -2)
    {
        uint8_t rgb[3] = { 40, 44, 52 };
        wfg_glow_centre(-400, -400, -400, -400);            /* no glow: a plain gradient */
        wfp_set_glow(rgb);
        glow_film = -2;
    }
    wfg_background(0, WFUI_H);
    top_bar("Settings");
    for (int i = 0; i < 2; i++)
    {
        int y0 = 54 + i * 40;
        if (i == ssec)
        {
            wfg_rrect(16, y0, 195, y0 + 33, 8, C_SEP, -1, 0);
            wfg_rect(16, y0 + 6, 18, y0 + 27, C_AMBER);
        }
        ui_text(F_15, 32, y0 + 8, sec_name[i], i == ssec ? C_TXT : C_TXT2);
    }
    char b[24];
    for (int i = 0; i < n; i++)
    {
        const char *name, *val, *sub;
        int y0 = 52 + i * 54, foc = i == sfoc, ed = set_row(ssec, rows[i], &name, &val, &sub, b, sizeof b);
        if (foc) wfg_rrect(212, y0, 703, y0 + 49, 8, C_AMBERD, C_AMBER, 1);
        ui_text(F_15, 228, y0 + 7, name, ed || ssec ? C_TXT : C_TXT2);
        int vx = 680 - ui_textw(F_15, val);
        ui_text(F_15, vx, y0 + 7, val, foc && ed ? C_AMBER : ed || ssec ? C_TXT : C_TXT3);
        if (foc && ed)
        {
            wfg_tri(vx - 14, y0 + 15, vx - 8, y0 + 10, vx - 8, y0 + 20, C_AMBER);
            wfg_tri(694, y0 + 15, 688, y0 + 10, 688, y0 + 20, C_AMBER);
        }
        ui_text_clip(F_13, 228, y0 + 28, sub, foc ? C_TXT2 : C_TXT3, 696);
        if (!foc && i + 1 != sfoc && i < n - 1) wfg_rect(228, y0 + 51, 687, y0 + 51, C_SEP);
    }
    if (is5d2())
    {
        static const char *const k[] = { "JOY", "Row", "DIAL", "Value", "PICT", "Section", "MENU", "Home", 0 };
        footer(k);
    }
    else
    {
        static const char *const k[] = { "UP/DN", "Row", "RING", "Value", "L/R", "Section", "MENU", "Home", 0 };
        footer(k);
    }
    toast();
}

static int set_key(int k)
{
    uint8_t rows[8];
    int n = set_rows(ssec, rows);
    const char *name, *val, *sub;
    char b[24];
    msg = 0;
    if (sfoc >= n) sfoc = n - 1;
    int ed = set_row(ssec, rows[sfoc], &name, &val, &sub, b, sizeof b);
    switch (k)
    {
    case MODULE_KEY_PRESS_UP:   sfoc = (sfoc + n - 1) % n; break;
    case MODULE_KEY_PRESS_DOWN: sfoc = (sfoc + 1) % n; break;
    case MODULE_KEY_PRESS_LEFT: case MODULE_KEY_PRESS_RIGHT:
    case MODULE_KEY_PICSTYLE: case MODULE_KEY_Q: case MODULE_KEY_TOUCH_1_FINGER:
        ssec = !ssec;
        sfoc = 0;
        break;
    case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_UP:
    case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_DOWN:
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER:
        if (!ed) { say(ssec ? "This section only shows" : "This one is fixed"); break; }
        set_step(rows[sfoc], k == MODULE_KEY_WHEEL_LEFT || k == MODULE_KEY_WHEEL_UP ? -1 : 1);
        if (rows[sfoc] == SR_OS) wf_log("ui: Camera interface %s", wf_os ? "WindyFilm" : "Canon + ML");
        break;
    case MODULE_KEY_MENU: case MODULE_KEY_TRASH:
        wfui_goto(WFUI_SCR_HOME);
        break;
    case MODULE_KEY_LV:
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ dispatch (wf_dark_ui.c) */
void wfos_enter(int scr)
{
    wfp_init_fixed();                                       /* the fixed colours and the cans' (the Darkroom held them) */
    wfc_pal_again();
    glow_film = -1;
    msg = 0;
    seen_gen = wf_gen();
    if (scr == WFUI_SCR_HOME)
    {
        wf_log("ui: Home");
        if (hfoc >= HF_LOOK + NLOOK) hfoc = HF_SHELF;
    }
    else
    {
        sfoc = ssec = 0;
        wf_log("ui: Settings");
    }
}

void wfos_leave(int scr)
{
    (void)scr;
    msg = 0;
    glow_film = -1;
}

void wfos_render(int scr, int now)
{
    (void)now;
    if (scr == WFUI_SCR_HOME) home_render();
    else set_render();
}

int wfos_key(int scr, int key)
{
    return scr == WFUI_SCR_HOME ? home_key(key) : set_key(key);
}

int wfos_tick(int scr, int now)
{
    (void)scr;
    int moved = 0;
    if (msg && now - msg_until >= 0) { msg = 0; moved = 1; }
    if (wf_gen() != seen_gen) { seen_gen = wf_gen(); moved = 1; }
    return moved;
}

#ifdef WF_HOST
int wfos_host_hfoc(void) { return hfoc; }
int wfos_host_sfoc(void) { return sfoc; }
int wfos_host_ssec(void) { return ssec; }
const char *wfos_host_msg(void) { return msg ? msg : ""; }
#endif
