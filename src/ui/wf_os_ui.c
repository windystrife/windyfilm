/* wf_os_ui.c - WindyFilm's film OS screens (docs/MODERNUI.md, modernUI M1): Home and Settings, in the browser's UI task.
 *
 * Home comes up on the MENU key in shooting while Camera interface = WindyFilm (wf_os; wf_ui.c wf_ui_home_key) in place
 * of Canon's menu: the roll in the camera (can, ROLL nnn, film, frames shot / left on the roll gauge), four ways on
 * (Film shelf = the browser, Darkroom, Prints = wf_prints_ui.c, Settings) and the roll's look (Light, Print, Paper, Grain,
 * Halation, Strength: the ring / dial steps the focused one, SET / INFO opens the browser on it).
 * MENU on Home closes the UI at once and hands Canon its MENU (wf_ui.c after_close; the EOS M two-finger tap arrives as
 * MENU too and opens the ML menu instead). 5D2: TRASH on Home opens the ML menu (its only way in while WindyFilm takes
 * TRASH in shooting). Settings: the Interface section (Camera interface, Quick keys, Blind roll, the fixed keys) and a
 * System section (what the packs say). MENU there goes back to Home. The 5D2's Top plate (M4): Canon's shooting screen
 * through the viewfinder as a film camera's top plate (below Settings).
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
void wfos_footer(const char *const *kv) { footer(kv); }   /* (the Film shelf, wf_ui.c) */

/* the top bar of every film OS screen (and the Film shelf, wf_ui.c): the mark and the title; right: the battery (the
   5D2's in % too: the EOS M has only ML's three bars), the camera's clock, the camera. Returns the left edge of that
   group (the shelf puts its mode chip before it). The UI task draws it again every second (HEAL_MS): the clock moves. */
int wfos_top_bar(const char *title)
{
    os_mark(16, 9, 22);
    ui_text(F_D20, 48, 8, title, C_TXT);
    const char *cam = is5d2() ? "5D Mark II" : "EOS M";
    int w = ui_textw(F_13, cam) + 16, xr = 704 - w;
    chip(xr, 10, cam, C_LINE, C_TXT2);
    xr -= 14;
    int hm = wf_clock_hm();
    if (hm >= 0)
    {
        char ck[8];
        snprintf(ck, sizeof ck, "%02d:%02d", hm / 100 % 100, hm % 100);
        xr -= ui_textw(F_15, ck);
        ui_text(F_15, xr, 10, ck, C_TXT2);
        xr -= 14;
    }
    int b = wf_battery_pct();
    if (b >= 0)
    {
        int c = b <= 15 ? C_WARN : C_TXT2, fw = 17 * (b > 100 ? 100 : b) / 100;
        if (is5d2())
        {
            char p[8];
            snprintf(p, sizeof p, "%d%%", b > 100 ? 100 : b);
            xr -= ui_textw(F_13, p);
            ui_text(F_13, xr, 12, p, c);
            xr -= 6;
        }
        xr -= 24;
        wfg_rrect(xr, 13, xr + 21, 24, 2, -1, c, 1);
        wfg_rect(xr + 22, 16, xr + 23, 21, c);
        if (fw > 0) wfg_rect(xr + 2, 15, xr + 2 + fw - 1, 22, c);
        xr -= 14;
    }
    wfg_rect(16, 40, 703, 40, C_SEP);
    return xr;
}
static void top_bar(const char *title) { wfos_top_bar(title); }

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
void wfos_roll_info(struct wfos_roll *h)
{
    const struct wf_roll *r = wf_roll_now();
    h->open = wf_roll_open(r) && !wf_roll_shelved();
    h->f = h->open ? wf_film_by_id(r->film) : 0;
    if (!h->f) h->f = wf_film_by_id(wf_loaded());
    h->num = r->num;
    h->shot = h->open ? r->nshot : 0;
    h->exp = h->open ? r->exp : 0;
    h->done = h->open && !wf_roll_can_shoot(r);
    h->rewound = h->open && r->state == WF_RS_REWOUND;
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
    if (t == HF_SHELF) { wfui_shelf(); return; }
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
        wf_log("ui: Home -> Prints");
        wfui_goto(WFUI_SCR_PRINTS);
        return;
    }
    wfui_goto(WFUI_SCR_SET);
}

void wfos_play(void)
{
    if (wf_playv) { open_tile(HF_PRINTS); return; }
    if (wf_roll_blocks_play()) { say("Blind roll: Canon's playback after the Darkroom"); return; }
    wfui_close_after(WFUI_AFTER_PLAY);
}

/* ------------------------------------------------------------------ Home */
static void home_render(void)
{
    struct wfos_roll h;
    wfos_roll_info(&h);
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
    sub[HF_PRINTS] = "The Darkroom's prints";
    sub[HF_SET] = "Interface, finder, keys";
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
    int hc = C_TXT3, hk = 0;
    char hb[48];
    const char *ht = lf && f && wf_row_avail(look_row[hfoc - HF_LOOK]) ? wf_row_note(look_row[hfoc - HF_LOOK], &hk) : 0;
    if (ht)                                                 /* labfx: why the focused value does not run as shown */
    {
        int i = 0;
        for (; ht[i] && i < (int)sizeof hb - 1; i++) hb[i] = ht[i] >= 'a' && ht[i] <= 'z' ? ht[i] - 32 : ht[i];
        hb[i] = 0;
        hint = hb;
        hc = hk == WF_NOTE_UNUSED ? C_WARN : C_AMBER;
    }
    ui_caps(688 - ui_capsw(hint), 334, hint, hc);
    for (int i = 0; i < NLOOK; i++)
    {
        int cx = 32 + i * 112, row = look_row[i], foc = hfoc == HF_LOOK + i, av = f && wf_row_avail(row);
        if (i) wfg_rect(cx - 12, 362, cx - 12, 406, C_SEP);
        if (foc) wfg_rrect(cx - 8, 356, cx + 96, 412, 8, C_AMBERD, C_AMBER, 1);
        ui_caps(cx, 362, look_cap[i], foc ? C_AMBER : C_TXT3);
        int nk = 0;
        if (av) wf_row_note(row, &nk);                      /* labfx: a glow value the other row's Lab takes out: grey */
        int tc = !av || nk == WF_NOTE_UNUSED ? C_TXT3 : foc ? C_AMBER : C_TXT;
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
        wfos_play();
        break;
    case MODULE_KEY_TRASH:                                  /* 5D2: the ML menu; EOS M (long DOWN): the shelf */
        if (is5d2()) wfui_close_after(WFUI_AFTER_MLMENU);
        else wfui_shelf();
        break;
    case MODULE_KEY_LV:
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ Settings */
enum { SR_OS = 0, SR_VF, SR_QK, SR_BLIND, SR_MLMENU, SR_MENUKEY, SR_PLATE, SR_PLAY, SR_N };
enum { SY_FILMS = 0, SY_FONTS, SY_CANS, SY_N };
static const char *const sec_name[2] = { "Interface", "System" };

static int set_rows(int sec, uint8_t *out)
{
    int n = 0;
    if (sec == 0)
    {
        out[n++] = SR_OS;
        out[n++] = SR_VF;
        out[n++] = is5d2() ? SR_PLATE : SR_QK;              /* the quick keys are the EOS M's, the Top plate the 5D2's */
        out[n++] = SR_PLAY;
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
    case SR_VF:
    {
        static const char *const vf_v[WF_VF_N] = { "Full", "Minimal", "Bright lines", "Canon + ML" };
        static const char *const vf_s[WF_VF_N] = { "Film, roll, shutter, aperture, ISO, meter and frames over LiveView",
                                                   "Only the film and the frame counter over LiveView",
                                                   "A rangefinder's frame, the values in a strip under it",
                                                   "ML's own info bars over LiveView, as before" };
        int v = wf_vf >= 0 && wf_vf < WF_VF_N ? wf_vf : 0;
        *name = "LiveView finder";
        *val = vf_v[v];
        *sub = !wf_os ? "With Camera interface WindyFilm" : vf_s[v];
        return 1;
    }
    case SR_QK:
        *name = "Quick keys";
        *val = qk_v[wf_qk >= 0 && wf_qk < WF_QK_N ? wf_qk : 0];
        *sub = qk_s[wf_qk >= 0 && wf_qk < WF_QK_N ? wf_qk : 0];
        return 1;
    case SR_PLATE:
        *name = "Top plate";
        *val = wf_plate ? "On" : "Off";
        *sub = !wf_os ? "With Camera interface WindyFilm"
             : wf_plate ? "Through the viewfinder: Canon's screen as the camera's top plate"
                        : "Through the viewfinder: Canon's own shooting screen";
        return 1;
    case SR_PLAY:
        *name = "PLAY shows";
        *val = wf_playv ? "Prints" : "Canon";
        *sub = !wf_os ? "With Camera interface WindyFilm"
             : wf_playv ? "The Darkroom's prints; PLAY there: Canon's playback" : "Canon's playback, as before";
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
    else if (r == SR_VF) wf_vf_set((wf_vf + WF_VF_N + d) % WF_VF_N);
    else if (r == SR_QK) wf_qk_set((wf_qk + WF_QK_N + d) % WF_QK_N);
    else if (r == SR_BLIND) wf_blind_set(!wf_blind);
    else if (r == SR_PLATE) wf_plate_set(!wf_plate);
    else if (r == SR_PLAY) wf_playv_set(!wf_playv);
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
        if (rows[sfoc] == SR_VF) wf_log("ui: LiveView finder %d", wf_vf);
        if (rows[sfoc] == SR_PLATE) wf_log("ui: Top plate %s", wf_plate ? "on" : "off");
        if (rows[sfoc] == SR_PLAY) wf_log("ui: PLAY shows %s", wf_playv ? "Prints" : "Canon");
        break;
    case MODULE_KEY_MENU: case MODULE_KEY_TRASH:
        wfui_goto(WFUI_SCR_HOME);
        break;
    case MODULE_KEY_LV:
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ the 5D2's Top plate (M4, mock 06)
 * Shooting through the viewfinder, the back LCD as a film camera's top plate: shutter and aperture large, ISO (FROM FILM
 * when the film set it), the exposure scale (M: Canon's meter at the last half-press; P / Tv / Av: the compensation),
 * the mode, the roll's look, the next frame, the card's shots left, the drive, the blind roll; on the right the film's
 * card: can, name, ROLL nnn, frames shot and the gauge. It comes up by itself on Canon's shooting screen (wf_osvf.c
 * wf_plate_poll: power on, the display back on, back from a Canon screen; Settings > Top plate) and opens nothing
 * else. Half-press closes it (wf_ui_key: Canon meters, as ever); the dials set the exposure (wf_plate_dial: the plate
 * holds Canon's MENU mode, so they come to us); without a key or a change it gives the screen back after
 * PLATE_IDLE_MS (Canon's own screen and its LCD timer again). Keys: MENU Home, TRASH the Film shelf, INFO Canon's
 * screen (closed, Canon gets INFO), PLAY Prints. */
#define PLATE_IDLE_MS 20000
static int plate_t, plate_sig;                              /* the last key / change (or the open); the values' sum */

/* digits, "/" and "." in the large display size up to xmax (F_D80 has nothing else: "f" in F_D30; a long time, BULB or
   anything wider than xmax all F_D30) */
static int big_val(int x, int by, const char *s, int c, int xmax)
{
    if (s[0] == 'f')
    {
        int w = ui_text(F_D30, x, by - ui_textasc(F_D30), "f", c) + 2;
        return w + big_val(x + w, by, s + 1, c, xmax);
    }
    int big = ui_textw(F_D80, s) <= xmax - x;
    for (const char *p = s; *p && big; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '/' || *p == '.')) big = 0;
    int f = big ? F_D80 : F_D30;
    return ui_text(f, x, by - ui_textasc(f), s, c);
}

/* the exposure scale: -2 .. +2 in thirds (13 ticks), the marker under it, red past either end */
static void plate_scale(int x, int y, int m8)
{
    for (int i = 0; i < 13; i++)
    {
        int tx = x + i * 10, whole = i % 3 == 0;
        wfg_rect(tx, y + (whole ? 0 : 3), tx + 1, y + 10, i == 6 ? C_TXT : whole ? C_TXT2 : C_TXT3);
    }
    ui_text(F_13, x - 4, y + 24, "-2", C_TXT3);
    ui_text(F_13, x + 112, y + 24, "+2", C_TXT3);
    if (m8 == WFOS_NOMETER) return;
    int t = m8 >= 0 ? (m8 * 3 + 4) / 8 : -((-m8 * 3 + 4) / 8), over = t > 6 || t < -6;
    if (t > 6) t = 6;
    if (t < -6) t = -6;
    int mx = x + (t + 6) * 10;
    wfg_tri(mx - 5, y + 21, mx + 6, y + 21, mx, y + 14, over ? C_WARN : C_AMBER);
}

static void plate_cell(int x, int y, const char *cap, const char *val, int vc)
{
    ui_caps(x, y, cap, C_TXT3);
    ui_text_clip(F_15, x, y + 18, val, vc, x + 100);
}

static int plate_hash(const struct wfos_lv *v)
{
    unsigned h = 2166136261u;
    const char *s[5] = { v->sh, v->ap, v->iso, v->mode, v->drive };
    for (int k = 0; k < 5; k++)
        for (const char *p = s[k]; *p; p++)
            h = (h ^ (unsigned char)*p) * 16777619u;
    int n[3] = { v->iso_film, v->meter, v->shots };
    for (int k = 0; k < 3; k++)
        h = (h ^ (unsigned)n[k]) * 16777619u;
    return (int)h;
}

static void plate_render(void)
{
    struct wfos_roll h;
    struct wfos_lv v;
    wfos_roll_info(&h);
    wf_osvf_values(&v);
    plate_sig = plate_hash(&v);
    const struct wf_film *f = h.f;
    int gid = f ? f->id : 0;
    if (gid != glow_film)
    {
        uint8_t rgb[3] = { 60, 64, 72 };
        if (f) wfc_glow(f, rgb);
        wfg_glow_centre(594, 174, 594, 298);
        wfp_set_glow(rgb);
        glow_film = gid;
    }
    wfg_background(0, WFUI_H);
    top_bar("Top plate");
    char s[48];
    /* shutter, aperture */
    ui_caps(24, 58, "SHUTTER", C_TXT3);
    ui_caps(258, 58, "APERTURE", C_TXT3);
    big_val(22, 150, v.sh, C_TXT, 246);
    big_val(256, 150, v.ap, C_TXT, 468);
    /* ISO, the exposure scale, the mode */
    ui_caps(24, 186, "ISO", C_TXT3);
    int iw = ui_text(F_D30, 22, 202, v.iso, C_TXT);
    if (v.iso_film)
    {
        int tw = ui_capsw("FROM FILM");
        wfg_rrect(22 + iw + 10, 214, 22 + iw + 10 + tw + 17, 232, 9, -1, C_AMBER, 1);
        ui_caps(22 + iw + 19, 218, "FROM FILM", C_AMBER);
    }
    ui_caps(258, 186, "EXPOSURE", C_TXT3);
    plate_scale(262, 210, v.meter);
    ui_caps(406, 186, "MODE", C_TXT3);
    ui_text(F_D30, 404, 202, v.mode, C_AMBER);
    /* the roll's look */
    wfg_rrect(16, 268, 463, 336, 10, C_FRAME, C_SEP, 1);
    static const uint8_t pr[4] = { WF_ROW_LIGHT, WF_ROW_PRINT, WF_ROW_GRAIN, WF_ROW_PAPER };
    static const char *const pc[4] = { "LIGHT", "PRINT", "GRAIN", "PAPER" };
    int id = f ? f->id : wf_loaded();
    for (int i = 0; i < 4; i++)
    {
        int x = 32 + i * 110;
        const char *t = id && wf_row_avail_for(pr[i], id) ? wf_row_text_for(pr[i], id) : 0;
        plate_cell(x, 282, pc[i], t ? t : "-", t ? C_TXT : C_TXT3);
        if (i) wfg_rect(x - 12, 282, x - 12, 322, C_SEP);
    }
    /* next frame, card, drive, blind roll */
    if (h.open && !h.done) snprintf(s, sizeof s, "%d", h.shot + 1);
    else snprintf(s, sizeof s, "%s", h.open ? "End" : "-");
    plate_cell(32, 362, "NEXT FRAME", s, h.done ? C_WARN : C_TXT);
    if (v.shots >= 0) snprintf(s, sizeof s, "%d", v.shots);
    else snprintf(s, sizeof s, "-");
    plate_cell(142, 362, "CARD", s, C_TXT);
    plate_cell(252, 362, "DRIVE", v.drive, C_TXT);
    plate_cell(362, 362, "BLIND ROLL", wf_blind ? "On" : "Off", wf_blind ? C_AMBER : C_TXT);
    /* the film's card */
    wfg_rrect(484, 50, 703, 432, 12, -1, C_SEP, 1);
    if (f) wfc_draw(f, CAN_BIG, 514, 60, C_BG0);
    else
    {
        wfg_rrect(514, 60, 673, 285, 14, -1, C_SEP, 1);
        ui_caps(594 - ui_capsw("EMPTY") / 2, 166, "EMPTY", C_TXT3);
    }
    const char *nm = f ? f->name : "No film loaded";
    int nw = ui_textw(F_D20, nm);
    if (nw > 200) nw = 200;
    ui_text_clip(F_D20, 594 - nw / 2, 292, nm, f ? C_TXT : C_TXT2, 698);
    if (h.open) snprintf(s, sizeof s, "ROLL %03d", h.num);
    else snprintf(s, sizeof s, "%s", f ? "NO ROLL" : "CANON");
    ui_caps(594 - ui_capsw(s) / 2, 322, s, h.done ? C_WARN : h.open ? C_AMBER : C_TXT3);
    if (h.open)
    {
        char d[12];
        snprintf(s, sizeof s, "%d", h.shot);
        snprintf(d, sizeof d, "/%d", h.exp);
        int w = ui_textw(F_D30, s) + 4 + ui_textw(F_15, d), x = 594 - w / 2;
        x += ui_text(F_D30, x, 344, s, h.done ? C_WARN : C_TXT) + 4;
        ui_text(F_15, x, 360, d, C_TXT3);
        ticks(500, 400, 188, 12, h.shot, h.exp);
    }
    else
    {
        const char *t = f ? "Every shot gets the look" : "TRASH: a film from the shelf";
        ui_text(F_13, 594 - ui_textw(F_13, t) / 2, 352, t, C_TXT2);
    }
    static const char *const kp[] = { "MENU", "Home", "TRASH", "Film shelf", "INFO", "Canon screen", "PLAY", "Prints",
                                      0 };
    footer(kp);
    toast();
}

static int plate_key(int k)
{
    plate_t = get_ms_clock();
    msg = 0;
    switch (k)
    {
    case MODULE_KEY_MENU:  wfui_goto(WFUI_SCR_HOME); break;
    case MODULE_KEY_TRASH: wfui_shelf(); break;
    case MODULE_KEY_INFO:  wf_log("ui: Top plate: INFO, Canon's screen"); wfui_close_after(WFUI_AFTER_INFO); break;
    case MODULE_KEY_PLAY:  wfos_play(); break;
    case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_RIGHT:   /* the main dial */
        wf_plate_dial(0, k == MODULE_KEY_WHEEL_RIGHT ? 1 : -1);
        break;
    case MODULE_KEY_WHEEL_UP: case MODULE_KEY_WHEEL_DOWN:      /* the rear wheel */
        wf_plate_dial(1, k == MODULE_KEY_WHEEL_DOWN ? 1 : -1);
        break;
    case MODULE_KEY_LV:    return 1;
    }
    return 0;
}

/* a change (the dials, a frame, the card) redraws and keeps it up; nothing for PLATE_IDLE_MS: Canon's screen */
static int plate_tick(int now)
{
    struct wfos_lv v;
    wf_osvf_values(&v);
    if (plate_hash(&v) != plate_sig)
    {
        plate_t = now;
        return 1;
    }
    if (now - plate_t >= PLATE_IDLE_MS)
    {
        plate_t = now;
        wf_log("ui: Top plate: %d s without a key, Canon's screen", PLATE_IDLE_MS / 1000);
        wfui_close_after(WFUI_AFTER_NONE);
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
    else if (scr == WFUI_SCR_PLATE)
    {
        plate_t = get_ms_clock();
        plate_sig = 0;
        wf_log("ui: Top plate");
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
    else if (scr == WFUI_SCR_PLATE) plate_render();
    else set_render();
}

int wfos_key(int scr, int key)
{
    return scr == WFUI_SCR_HOME ? home_key(key) : scr == WFUI_SCR_PLATE ? plate_key(key) : set_key(key);
}

int wfos_tick(int scr, int now)
{
    int moved = 0;
    if (msg && now - msg_until >= 0) { msg = 0; moved = 1; }
    if (wf_gen() != seen_gen) { seen_gen = wf_gen(); moved = 1; }
    if (scr == WFUI_SCR_PLATE) moved |= plate_tick(now);
    return moved;
}

#ifdef WF_HOST
int wfos_host_hfoc(void) { return hfoc; }
int wfos_host_sfoc(void) { return sfoc; }
int wfos_host_ssec(void) { return ssec; }
const char *wfos_host_msg(void) { return msg ? msg : ""; }
#endif
