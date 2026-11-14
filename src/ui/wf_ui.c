/* wf_ui.c - WindyFilm browser (B2 small text, docs/DESIGN.md 5; mockup.py variant_b2s).
 *
 * Threads:
 *   GUI task: wf_ui_key() (from the core's CBR_KEYPRESS) only queues keys and returns at once; wf_ui_open_key().
 *   UI task "wf_ui" (own, started by wf_ui_open, ends on close): takes the screen with canon_gui_disable_front_buffer
 *     (EOS M patch 0003: that sleeps 200..480 ms while Canon's LV Set dialog comes up, so never the GUI task), takes
 *     palette entries WFUI_PAL_BASE..254, then every 20 ms: keys, wf_gen() check, redraw when something changed, a
 *     full redraw every HEAL_MS (Canon / ML may scribble over the bitmap), palette keep-alive. On close: transparent
 *     front buffer, Canon's palette entries back, front buffer back to Canon, everything freed.
 * Memory: nothing is allocated before wf_ui_open (wf_ui_init only clears state); the film list, the font pack and the
 * can pack index / cache are malloc'ed by the UI task and freed when it ends.
 *
 * Never a dead camera (owner 11-14, EOS M: "sometimes neither the film menu nor the ML menu opens"). While the browser is
 * up it takes every key, so a UI task that does not run would leave both menus dead: the film worker (film_yuv, prio
 * 0x1C) develops a shot for seconds (RAW develop 35-45 s) and starved the old prio-0x1E UI task, which a TRASH had just
 * started. Now the UI task runs above the worker, and it beats (hb, phase) at every step; a key that finds no beat for
 * STALE_MS goes to Canon / ML and asks the task to close when it runs again (the EOS M two-finger tap then opens the ML
 * menu: no module took MENU). The task is never declared dead from outside: it may still run and give the screen back.
 * Phases in the log: 1 created, 2 ML menu away, 3 film list, 4 fonts, 5 cans, 6 Canon dialog, 7 screen, 8 palette,
 * 9 tick, 10 drawing, 11 copy, 12 palette check, 13 load animation, 14 closing, 15 starting LiveView. */
#include "wf_ui_int.h"
#include "wf_icons.h"

#define UI_PRIO         0x1B            /* above the film worker (0x1C), below ML's menu_task (0x1A) and Canon's GUI */
#define UI_STACK        0x2000
#define STALE_MS        2000
#define TICK_MS         20
#define PAL_PROBE_MS    100
#define PAL_FORCE_MS    2000
#define HEAL_MS         1000
#define MSG_MS          2500
#define KQ_N            16
#define STRIP_SLOTS     11
#define STRIP_VIS       10              /* slots the scrollbar thumb stands for */

/* CLOSING: the screen is Canon's again (keys pass on), the task still starts LiveView after a load (5D2) */
enum { ST_CLOSED = 0, ST_OPENING, ST_OPEN, ST_CLOSING };
enum { PH_CREATED = 1, PH_MENU, PH_LIST, PH_FONTS, PH_CANS, PH_DIALOG, PH_SCREEN, PH_PAL, PH_TICK, PH_DRAW, PH_COPY,
       PH_PALCHK, PH_ANIM, PH_END, PH_LV };

static volatile int st;
static volatile int close_req;
static volatile uint8_t kq[KQ_N];
static volatile int kq_w, kq_r;
static int took;                        /* we hold the screen (front buffer disabled) and the palette */
static volatile int hb, phase;          /* the UI task's last sign of life (ms clock) and where it was */
static volatile int stale_said;
static inline void beat(int p) { phase = p; hb = get_ms_clock(); }

/* 5D2: a Canon dialog under the browser, as ML's own menu does (menu.c piggyback_canon_menu, GUIMODE_ML_MENU =
 * lv ? 38 : 2 on the 5D2; mode 0 while recording).
 *  - Without LiveView (owner 11-14: the browser came up only with LV on) Canon shows nothing of its own on the LCD (often
 *    the display is off); its MENU mode 2 turns the display on and keeps it on.
 *  - In LiveView (owner 11-14: "the settings cannot be changed from the UI on the 5D2") Canon's plain LV (mode 0) turns
 *    the dials into exposure changes itself and sends the module no WHEEL codes; ML's menu asks for mode 38 there,
 *    "a gui mode which lets you use the wheel and all other keys" (consts.h), with the bitmap muted while Canon's dialog
 *    comes up.
 * The disabled front buffer hides Canon's dialog. ML's order: mode, then the front buffer; on close mode 0 first
 * (close_canon_menu), then the front buffer. A joystick press (edit) also changes the rows, in case a dial still
 * sends nothing. */
#define DLG_NOLV        2
#define DLG_LV          38
static int dlg_want;                    /* the mode for this open (0 = none) */
static int dlg_mode;                    /* we asked Canon for dlg_want */
static int dlg_mask;                    /* bmp_off() is ours to undo */
static int dlg_seen;                    /* the screen came up: losing the mode or the display then closes us */
static void dlg_take(void);
static int dlg_lost(void);
static int dlg_give(void);

/* SET loads a film: a short load animation of our own (the can's film leader winds out to a gate), then the browser
 * closes and LiveView shows (owner 11-14: on the EOS M the browser is open only in LV; the 5D2 starts LV when the
 * browser was opened without it). A key skips the rest of the animation. */
#define ANIM_WIND_MS    800
#define ANIM_MS         1150
static int anim_on, anim_t0, anim_drawn;
static int lv_after;                    /* a film was loaded: LiveView after the close */
static int edit;                        /* 5D2: joystick press, LEFT / RIGHT change the focused row */

static uint8_t *list;                   /* wf_films index of each ready film: by group, then order */
static int nlist;
static int gstart[WF_NGROUPS + 1];
static int sel, focus, info;
static int gsel[WF_NGROUPS], gfirst[WF_NGROUPS];
#define FIRST_UNSET (-100)
static int last_id;                     /* selection kept between opens */
static int dirty;
static uint32_t drawn_gen;
/* the rows: WF_NROWS (9) do not fit above the tabs, so ROWS_VIS of them show at 19 px and the list scrolls with the
   focus (the font keeps its size); rtop = the first row shown */
#define ROWS_VIS 7
static int rtop;
typedef char rows_vis_check[ROWS_VIS <= WF_NROWS && sizeof wf_icons / sizeof *wf_icons == WF_NROWS ? 1 : -1];
static void focus_to(int f)
{
    focus = (f + WF_NROWS) % WF_NROWS;
    if (focus < rtop) rtop = focus;
    if (focus >= rtop + ROWS_VIS) rtop = focus - ROWS_VIS + 1;
    dirty = 1;
}
static int glow_id;
static const char *msg;
static int msg_until;
static int t_probe, t_force, t_heal;

static const char *const gname[WF_NGROUPS] = { "Colour", "Cine", "B&W", "Film LUT" };

static inline const struct wf_film *film_at(int i) { return i >= 0 && i < nlist ? &wf_films[list[i]] : 0; }
static inline int gcount(int g) { return gstart[g + 1] - gstart[g]; }
static inline int group_of_sel(void) { return nlist ? wf_films[list[sel]].group % WF_NGROUPS : 0; }
static inline int acc(int g) { return g == WF_GRP_LUT ? C_TEAL : C_AMBER; }
static inline int accd(int g) { return g == WF_GRP_LUT ? C_TEALD : C_AMBERD; }
static inline int is5d2(void) { return wf_cam() == WF_CAM_5D2; }

/* ------------------------------------------------------------------ film list and selection */
static int build_list(void)
{
    int n = wf_nfilms > 255 ? 255 : wf_nfilms;
    nlist = 0;
    list = malloc(n > 0 ? n : 1);
    if (!list) return -1;
    for (int g = 0; g < WF_NGROUPS; g++)
    {
        int start = gstart[g] = nlist;
        for (int i = 0; i < n; i++)
        {
            const struct wf_film *f = &wf_films[i];
            if (f->group != g || !(f->flags & WF_F_READY)) continue;
            int k = nlist++;
            while (k > start && wf_films[list[k - 1]].order > f->order) { list[k] = list[k - 1]; k--; }
            list[k] = i;
        }
    }
    gstart[WF_NGROUPS] = nlist;
    return 0;
}

static int index_of_id(int id)
{
    for (int i = 0; i < nlist; i++)
        if (wf_films[list[i]].id == id) return i;
    return -1;
}

/* strip scroll of group g so that local index l sits in slots 1..8 (slot 0 and 9..10 are under the edge fades) */
static void place(int g, int l)
{
    int n = gcount(g), first = gfirst[g];
    if (first == FIRST_UNSET)
    {
        first = l - 4;
        if (first > n - 9) first = n - 9;
        int lo = l > 0 ? 0 : -1;
        if (first < lo) first = lo;
    }
    if (l - first < 1) first = l - 1;
    if (l - first > 8) first = l - 8;
    gfirst[g] = first;
}

static void select_film(int i)
{
    if (i < 0 || i >= nlist) return;
    sel = i;
    int g = group_of_sel();
    gsel[g] = i;
    place(g, i - gstart[g]);
    dirty = 1;
}

static void next_group(void)
{
    int g = group_of_sel();
    for (int k = 1; k <= WF_NGROUPS; k++)
    {
        int h = (g + k) % WF_NGROUPS;
        if (!gcount(h)) continue;
        if (h == g) return;
        int i = gsel[h];
        if (i < gstart[h] || i >= gstart[h + 1]) i = gstart[h];
        select_film(i);
        return;
    }
}

static void say(const char *s)
{
    msg = s;
    msg_until = get_ms_clock() + MSG_MS;
    dirty = 1;
}

static void load_sel(void)
{
    const struct wf_film *f = film_at(sel);
    if (!f) return;
    if (wf_loaded() == f->id) { say("Already loaded"); return; }
    int r = wf_load(f->id);
    if (r == WF_LOAD_OK)
    {
        anim_on = 1;                                        /* the animation, then close and LiveView */
        anim_t0 = get_ms_clock();
        anim_drawn = 0;
        lv_after = 1;
        edit = info = 0;
        msg = 0;
        dirty = 1;
        return;
    }
    say(r == WF_LOAD_NOTABLE ? "Film table not on the card" : r == WF_LOAD_NOCUBE ? "LUT cube not on the card"
        : r == WF_LOAD_BUSY ? "Busy, press SET again" : "Unknown film");
    wf_log("ui: load %d -> %d", f->id, r);
}

static void step_row(int d)
{
    if (!wf_row_avail(focus)) return;
    if (wf_row_step(focus, d)) dirty = 1;
}

static void act(int k)
{
    if (anim_on)
    {
        close_req = 1;                                      /* any key: skip the rest of the load animation */
        return;
    }
    switch (k)
    {
    case MODULE_KEY_PRESS_LEFT:   if (edit) step_row(-1); else select_film(sel - 1); break;
    case MODULE_KEY_PRESS_RIGHT:  if (edit) step_row(1); else select_film(sel + 1); break;
    case MODULE_KEY_PRESS_UP:     focus_to(focus - 1); break;
    case MODULE_KEY_PRESS_DOWN:   focus_to(focus + 1); break;
    case MODULE_KEY_WHEEL_LEFT:
    case MODULE_KEY_WHEEL_UP:     step_row(-1); break;
    case MODULE_KEY_WHEEL_RIGHT:
    case MODULE_KEY_WHEEL_DOWN:   step_row(1); break;
    case MODULE_KEY_JOY_CENTER:
        if (is5d2()) { edit = !edit; dirty = 1; break; }   /* 5D2: the joystick press edits the focused row */
        load_sel();
        break;
    case MODULE_KEY_PRESS_SET:    edit = 0; load_sel(); break;
    case MODULE_KEY_Q:
    case MODULE_KEY_PICSTYLE:
    case MODULE_KEY_TOUCH_1_FINGER: edit = 0; next_group(); break;
    case MODULE_KEY_INFO:         info = !info; dirty = 1; break;
    case MODULE_KEY_MENU:
    case MODULE_KEY_TRASH:
    case MODULE_KEY_PLAY:
    case MODULE_KEY_LV:           close_req = 1; break;
    }
}

/* ------------------------------------------------------------------ drawing helpers */
static int text_r(int font, int xr, int y, const char *s, int c)
{
    int w = ui_textw(font, s);
    ui_text(font, xr - w, y, s, c);
    return w;
}

static void text_c(int font, int cx, int y, const char *s, int c, int maxw)
{
    int w = ui_textw(font, s);
    if (w <= maxw) ui_text(font, cx - w / 2, y, s, c);
    else ui_text_clip(font, cx - maxw / 2, y, s, c, cx + maxw / 2);
}

/* outlined 13 px chip (mockup chip_s); skipped when it would pass xmax */
static int chip_s(int x, int y, const char *s, int col, int xmax)
{
    if (!s || !*s) return x;
    int w = ui_textw(F_13, s) + 12;
    if (x + w > xmax) return x;
    wfg_rrect(x, y, x + w, y + 17, 8, -1, col, 1);
    ui_text(F_13, x + 6, y + 1, s, col);
    return x + w + 6;
}

static const char *label_of(const struct wf_film *f)
{
    return f->shortname && *f->shortname ? f->shortname : f->code ? f->code : "";
}

/* ------------------------------------------------------------------ areas */
static void draw_header(int now)
{
    wfg_rrect(14, 6, 36, 28, 6, C_AMBER, -1, 0);                       /* own mark: a frame with sprockets + hill */
    for (int i = 0; i < 3; i++)
    {
        wfg_rect(17 + i * 6, 9, 20 + i * 6, 11, C_BGLAST);
        wfg_rect(17 + i * 6, 23, 20 + i * 6, 25, C_BGLAST);
    }
    wfg_tri(18, 21, 25, 14, 32, 21, C_BGLAST);
    int x = 44 + ui_text(F_ML23, 44, 4, "WindyFilm", C_TXT);
    ui_text(F_13, x + 8, 11, "by WindyStrife", C_TXT3);

    const char *s, *st_s = wf_status();
    const struct wf_film *lf = wf_film_by_id(wf_loaded());
    int fill = C_SEP, out = C_WARN, dot = C_WARN, tc = C_TXT;
    if (msg && now - msg_until < 0) s = msg;
    else if (st_s && *st_s) s = st_s;
    else if (lf) { s = lf->name; fill = C_CHIPF; out = C_CHIPO; dot = C_GRN; }
    else { s = "No film loaded"; out = C_LINE; dot = C_BAROFF; tc = C_TXT2; }
    int lw = ui_textw(F_15, s);
    if (lw > 400) lw = 400;
    wfg_rrect(WFUI_W - 30 - lw - 18, 7, WFUI_W - 12, 27, 10, fill, out, 1);
    wfg_ellipse(WFUI_W - 26 - lw - 14, 13, WFUI_W - 26 - lw - 6, 21, dot);
    ui_text_clip(F_15, WFUI_W - 20 - lw, 8, s, tc, WFUI_W - 20);
    wfg_rect(12, 33, WFUI_W - 13, 33, C_LINE);
}

/* SET Load button (or its Loaded / not-on-card state); returns its left edge */
static int draw_load_button(const struct wf_film *f, int g)
{
    int A = acc(g);
    if (wf_loaded() == f->id)
    {
        int bw = ui_textw(F_15, "Loaded") + 30;
        wfg_rrect(706 - bw, 70, 706, 90, 10, C_CHIPF, C_CHIPO, 1);
        wfg_ellipse(706 - bw + 9, 76, 706 - bw + 16, 83, C_GRN);
        ui_text(F_15, 706 - bw + 21, 72, "Loaded", C_GRN);
        return 706 - bw;
    }
    if (!wf_avail(f->id))
    {
        const char *s = "Not on card";
        int bw = ui_textw(F_15, s) + 20;
        wfg_rrect(706 - bw, 70, 706, 90, 10, -1, C_TXT3, 1);
        ui_text(F_15, 706 - bw + 10, 72, s, C_TXT3);
        return 706 - bw;
    }
    int wset = ui_textw(F_13, "SET");
    int bw = wset + ui_textw(F_15, "Load") + 26;
    wfg_rrect(706 - bw, 70, 706, 90, 10, A, -1, 0);
    ui_text(F_13, 706 - bw + 9, 73, "SET", C_BGLAST);
    ui_text(F_15, 706 - bw + 17 + wset, 72, "Load", C_BGLAST);
    return 706 - bw;
}

static void draw_info_column(const struct wf_film *f, int g)
{
    int A = acc(g), AD = accd(g);
    char b[24];
    ui_text_clip(F_ML28, 262, 38, f->name, C_TXT, 712);
    int bx = draw_load_button(f, g);
    int x = chip_s(264, 72, f->kind, A, bx - 6);
    snprintf(b, sizeof(b), "ISO %d", f->iso);
    x = chip_s(x, 72, b, C_TXT2, bx - 6);
    chip_s(x, 72, f->format, C_TXT2, bx - 6);
    if (f->line1) ui_text_clip(F_15, 264, 96, f->line1, C_TXT2, 712);
    if (f->line2) ui_text_clip(F_15, 264, 113, f->line2, C_TXT2, 712);

    for (int r = rtop; r < rtop + ROWS_VIS && r < WF_NROWS; r++)
    {
        int y = 136 + (r - rtop) * 19;
        int foc = r == focus, av = wf_row_avail(r);
        if (foc)
        {
            wfg_rrect(258, y - 1, 708, y + 18, 6, AD, edit ? A : -1, 1);  /* 5D2 edit: outlined, LEFT / RIGHT step */
            wfg_rrect(258, y - 1, 261, y + 18, 2, A, -1, 0);
        }
        wfg_bits16(wf_icons[r], 270, y + 1, !av ? (foc ? C_TXT3 : C_BAROFF) : foc ? A : C_TXT3);
        const char *nm = wf_row_name(r);
        ui_text(F_15, 294, y, nm ? nm : "", !av ? C_TXT3 : foc ? C_TXT : C_TXT2);
        const char *val = wf_row_text(r);
        if (!val) val = "";
        int live = foc && av;
        int vx = live ? 684 : 700;
        int xl = vx - text_r(F_15, vx, y, val, !av ? C_TXT3 : foc ? C_TXT : C_TXT2);   /* left edge of the value */
        if (r == WF_ROW_STRENGTH)
        {
            int max = 0, lv = wf_row_level(r, &max);
            int lit = max > 0 ? (lv * 5 + max / 2) / max : 0;
            if (lit < 0) lit = 0;
            if (lit > 5) lit = 5;
            int bx0 = 600 - (700 - vx);                     /* 5 bars before the value, moved with it */
            for (int i = 0; i < 5; i++)
                wfg_rrect(bx0 + i * 10, y + 5, bx0 + 6 + i * 10, y + 13, 2, i < lit && av ? A : C_BAROFF, -1, 0);
            xl = bx0;
        }
        if (live)
        {
            wfg_tri(694, y + 4, 699, y + 9, 694, y + 14, A);
            wfg_tri(xl - 7, y + 4, xl - 12, y + 9, xl - 7, y + 14, A);
        }
        if (!foc && r != rtop + ROWS_VIS - 1) wfg_rect(294, y + 18, 700, y + 18, C_SEP);
    }
    if (WF_NROWS > ROWS_VIS)                                /* scroll bar: where the 7 shown rows sit among all */
    {
        int y0 = 136, h = ROWS_VIS * 19 - 1;
        int t0 = y0 + h * rtop / WF_NROWS, t1 = y0 + h * (rtop + ROWS_VIS) / WF_NROWS - 1;
        wfg_rect(712, y0, 713, y0 + h - 1, C_SEP);
        wfg_rect(712, t0, 713, t1, C_TXT3);
    }
}

static void draw_tabs(int g)
{
    char b[32];
    int tx = 22;
    for (int h = 0; h < WF_NGROUPS; h++)
    {
        snprintf(b, sizeof(b), "%s %d", gname[h], gcount(h));
        int w = ui_textw(F_15, b);
        ui_text(F_15, tx, 276, b, h == g ? C_TXT : C_TXT3);
        if (h == g) wfg_rrect(tx, 294, tx + w, 296, 1, acc(h), -1, 0);
        tx += w + 24;
    }
    snprintf(b, sizeof(b), "%d / %d", sel + 1, nlist);
    text_r(F_13, 700, 278, b, C_TXT3);
}

static void draw_strip(int g)
{
    int A = acc(g), AD = accd(g);
    int loaded = wf_loaded();
    wfg_rect(0, 300, WFUI_W - 1, 436, C_STRIP);
    for (int hx = -6; hx < WFUI_W; hx += 22)
    {
        wfg_rrect(hx, 303, hx + 11, 308, 2, C_HOLE, -1, 0);
        wfg_rrect(hx, 428, hx + 11, 433, 2, C_HOLE, -1, 0);
    }
    int n = gcount(g), first = gfirst[g], l = sel - gstart[g];
    for (int i = 0; i < STRIP_SLOTS; i++)
    {
        int x = -20 + i * 72;
        wfg_rrect(x - 6, 314, x + 58, 422, 4, C_FRAME, -1, 0);
    }
    for (int i = 0; i < STRIP_SLOTS; i++)
    {
        int li = first + i;
        if (li < 0 || li >= n || li == l) continue;
        const struct wf_film *f = film_at(gstart[g] + li);
        int x = -20 + i * 72;
        wfc_draw(f, CAN_STRIP, x, 320, C_FRAME);
        text_c(F_13C, x + 26, 399, label_of(f), wf_avail(f->id) ? C_TXT3 : C_LINE, 66);
        if (f->id == loaded) wfg_ellipse(x + 23, 384, x + 30, 391, C_GRN);
    }
    const struct wf_film *f = film_at(sel);
    int i = l - first;
    if (f && i >= 0 && i < STRIP_SLOTS)
    {
        int x = -20 + i * 72;
        wfg_rrect(x - 10, 308, x + 62, 428, 6, AD, A, 2);
        wfc_draw(f, CAN_SEL, x - 3, 312, AD);
        text_c(F_13C, x + 26, 397, label_of(f), C_TXT, 70);
        if (f->id == loaded) wfg_ellipse(x + 23, 384, x + 30, 391, C_GRN);
    }
    wfg_fade_edges(307, 430);
    wfg_tri(8, 368, 16, 360, 16, 376, sel > 0 ? C_TXT2 : C_BAROFF);
    wfg_tri(711, 368, 703, 360, 703, 376, sel < nlist - 1 ? C_TXT2 : C_BAROFF);

    /* scrollbar: the strip's window in the whole list */
    wfg_rrect(22, 442, 698, 445, 2, C_TRACK, -1, 0);
    if (nlist)
    {
        int gf = gstart[g] + (first < 0 ? 0 : first);
        int tw = 676 * STRIP_VIS / nlist;
        if (tw > 676) tw = 676;
        if (tw < 8) tw = 8;
        int t0 = 22 + 676 * gf / nlist;
        if (t0 + tw > 698) t0 = 698 - tw;
        wfg_rrect(t0, 441, t0 + tw, 446, 3, A, -1, 0);
    }
}

static void draw_footer(void)
{
    static const char *const k_eosm[] = { "SET", "Load", "L/R", "Film", "UP/DN", "Row", "RING", "Value",
                                          "TAP", "Group", "INFO", "Info", "MENU", "Exit", 0 };
    static const char *const k_5d2[] = { "SET", "Load", "L/R", "Film", "UP/DN", "Row", "DIAL", "Value",
                                         "JOY", "Edit", "PICT", "Group", "INFO", "Info", "MENU", "Exit", 0 };
    static const char *const k_5d2e[] = { "L/R", "Value", "DIAL", "Value", "UP/DN", "Row", "JOY", "Done",
                                          "SET", "Load", "MENU", "Exit", 0 };
    const char *const *k = !is5d2() ? k_eosm : edit ? k_5d2e : k_5d2;
    int x = 14;
    for (; *k; k += 2)
    {
        int kw = ui_textw(F_13, k[0]) + 10;
        wfg_rrect(x, 454, x + kw, 471, 4, -1, C_KEYO, 1);
        ui_text(F_13, x + 5, 455, k[0], C_TXT2);
        x += kw + 5;
        x += ui_text(F_13, x, 455, k[1], C_TXT3) + 12;
    }
}

/* INFO: the film's details and the key map of this camera, over a dimmed browser */
static void draw_info(const struct wf_film *f, int g)
{
    static const char *const h_eosm[] = { "Left / Right", "previous / next film", "Up / Down", "choose a row",
        "Rear ring", "change the row", "SET", "load the film", "1-finger tap", "next group",
        "INFO", "this page", "MENU, 2-finger tap", "close", "Half-shutter", "close, shoot", 0 };
    static const char *const h_5d2[] = { "Joystick left / right", "previous / next film", "Joystick up / down",
        "choose a row", "Main or rear dial", "change the row", "Joystick press", "edit row: L / R",
        "SET", "load the film", "Pict.Style", "next group", "INFO", "this page", "MENU, TRASH", "close",
        "Half-shutter", "close, shoot", 0 };
    int A = acc(g);
    int x0 = 104, y0 = 44, x1 = 616, y1 = 440;
    char b[48];
    wfg_scrim(x0, y0, x1, y1);
    wfg_rrect(x0, y0, x1, y1, 10, C_SEP, C_LINE, 1);
    int x = x0 + 20, y = y0 + 10;
    ui_text_clip(F_ML28, x, y, f->name, C_TXT, x1 - 20);
    y += 36;
    int cx = chip_s(x, y, f->kind, A, x1 - 20);
    snprintf(b, sizeof(b), "ISO %d", f->iso);
    cx = chip_s(cx, y, b, C_TXT2, x1 - 20);
    chip_s(cx, y, f->format, C_TXT2, x1 - 20);
    y += 26;
    if (f->line1) ui_text_clip(F_15, x, y, f->line1, C_TXT2, x1 - 20);
    if (f->line2) ui_text_clip(F_15, x, y + 17, f->line2, C_TXT2, x1 - 20);
    y += 42;
    wfg_rect(x, y, x1 - 20, y, C_LINE);
    y += 8;

    int lut = f->flags & WF_F_LUTCAN, av = wf_avail(f->id);
    static const char *const pn[WF_NPRINTS] = { "Scan", "Cine", "Paper" };
    char pr[32];
    pr[0] = 0;
    for (int v = 0; v < WF_NPRINTS; v++)
        if (f->prints & (1 << v))
        {
            int n = strlen(pr);
            snprintf(pr + n, sizeof(pr) - n, "%s%s", n ? ", " : "", pn[v]);
        }
    char kel[16];
    snprintf(kel, sizeof(kel), "%d K", f->kelvin);
    const char *kv[] = {
        "Table", f->code ? f->code : "",
        "Daylight", kel,
        "Prints", lut ? "-" : pr,
        "Output", lut ? (f->format && !strcmp(f->format, "Standard") ? "Look on the Standard picture" : "V-Log + the can's LUT")
                      : "Film look",
        "On the card", av ? (lut ? "LUT cube found" : "Film table found") : (lut ? "LUT cube missing" : "Film table missing"),
        "State", wf_loaded() == f->id ? "Loaded" : "Not loaded",
        0 };
    for (int i = 0; kv[i]; i += 2, y += 19)
    {
        ui_text(F_15, x, y, kv[i], C_TXT3);
        ui_text_clip(F_15, x + 112, y, kv[i + 1], i == 8 && !av ? C_WARN : C_TXT, x1 - 20);
    }
    y += 4;
    wfg_rect(x, y, x1 - 20, y, C_LINE);
    y += 8;
    const char *const *h = is5d2() ? h_5d2 : h_eosm;
    int col = 0, yy = y;
    for (; *h; h += 2)
    {
        int hx = x + col * 250;
        ui_text_clip(F_13, hx, yy, h[0], C_TXT2, hx + 126);
        ui_text_clip(F_13, hx + 130, yy, h[1], C_TXT3, col ? x1 - 20 : hx + 246);
        if (++col == 2) { col = 0; yy += 17; }
    }
    text_r(F_13, x1 - 20, y1 - 22, "INFO: close this page", C_TXT3);
}

void wfui_render(void)
{
    int now = get_ms_clock();
    const struct wf_film *f = film_at(sel);
    int g = group_of_sel();
    if (f && f->id != glow_id)
    {
        uint8_t rgb[3];
        wfc_glow(f, rgb);
        wfp_set_glow(rgb);
        glow_id = f->id;
    }
    wfg_background(0, 300);
    wfg_background(437, WFUI_H);
    draw_header(now);
    if (!f)
    {
        ui_text(F_15, 262, 60, "No films in this build.", C_TXT2);
        draw_footer();
        return;
    }
    wfc_draw(f, CAN_BIG, 45, 40, C_BG0);
    draw_info_column(f, g);
    draw_tabs(g);
    draw_strip(g);
    draw_footer();
    if (info) draw_info(f, g);
}

/* the load animation at t ms (own design, own can art): a panel over the dimmed browser; the film's leader winds out of
 * its can (WFCANS.DAT, CAN_SEL) to a gate on the right, sprocket holes riding with the film, then the gate lights and
 * "Loaded" shows. first = the browser under it was just drawn: dim it once. Only the panel is drawn again per frame. */
#define AX0 150
#define AY0 112
#define AX1 570
#define AY1 368
void wfui_render_anim(int t, int first)
{
    const struct wf_film *f = film_at(sel);
    if (!f) return;
    int g = group_of_sel(), A = acc(g), AD = accd(g);
    if (first) wfg_scrim(AX0, AY0, AX1, AY1);
    int done = t >= ANIM_WIND_MS;
    int p = t >= ANIM_WIND_MS ? 1024 : t < 0 ? 0 : t * 1024 / ANIM_WIND_MS;
    int e = 1024 - (1024 - p) * (1024 - p) / 1024;                  /* ease out: fast pull, slows into the gate */
    char b[24];

    wfg_rrect(AX0, AY0, AX1, AY1, 10, C_SEP, C_LINE, 1);
    ui_text(F_15, AX0 + 20, AY0 + 12, done ? "Loaded" : "Loading", done ? C_GRN : C_TXT3);
    ui_text_clip(F_ML23, AX0 + 20, AY0 + 30, f->name, C_TXT, AX1 - 20);
    int cx = chip_s(AX0 + 22, AY0 + 62, f->kind, A, AX1 - 20);
    snprintf(b, sizeof(b), "ISO %d", f->iso);
    chip_s(cx, AY0 + 62, b, C_TXT2, AX1 - 20);

    /* can, leader, gate */
    int canx = AX0 + 24, cany = AY0 + 96;
    int ys0 = cany + 23, ys1 = cany + 59;                           /* the film band across the can's middle */
    int xs = canx + wfc_size[CAN_SEL][0] - 6;                       /* leaves the can's right side */
    int gx0 = AX1 - 78, gx1 = AX1 - 24;                             /* the gate */
    int lead = xs + (gx1 - 6 - xs) * e / 1024;
    wfg_rrect(gx0, ys0 - 10, gx1, ys1 + 10, 6, done ? AD : -1, done ? A : C_LINE, done ? 2 : 1);
    if (lead > xs)
    {
        wfg_rect(xs, ys0, lead, ys1, C_STRIP);
        for (int hx = lead - 10; hx >= xs; hx -= 16)                /* holes ride with the film */
        {
            wfg_rrect(hx - 7, ys0 + 3, hx, ys0 + 7, 1, C_HOLE, -1, 0);
            wfg_rrect(hx - 7, ys1 - 7, hx, ys1 - 3, 1, C_HOLE, -1, 0);
        }
        for (int fx = lead - 64; fx - 40 >= xs; fx -= 52)            /* frames behind the leader */
            wfg_rect(fx - 40, ys0 + 11, fx, ys1 - 11, C_FRAME);
        wfg_tri(lead - 13, ys0, lead + 1, ys0, lead + 1, ys0 + 14, C_SEP);   /* the leader's cut tip */
    }
    wfc_draw(f, CAN_SEL, canx, cany, C_SEP);
    if (done)
    {
        snprintf(b, sizeof(b), "%d", 1);
        text_c(F_15, (gx0 + gx1) / 2, ys0 + 9, b, A, gx1 - gx0 - 8);   /* frame 1 in the gate */
    }

    /* progress */
    int bx0 = AX0 + 20, bx1 = AX1 - 20, by = AY1 - 34;
    wfg_rrect(bx0, by, bx1, by + 4, 2, C_TRACK, -1, 0);
    wfg_rrect(bx0, by - 1, bx0 + (bx1 - bx0) * e / 1024, by + 5, 3, done ? C_GRN : A, -1, 0);
}

/* ------------------------------------------------------------------ the UI task */
void wfui_task_begin(void)
{
    beat(PH_MENU);
    anim_on = lv_after = edit = 0;
    if (close_req) return;                                  /* a key found no beat before this task first ran */
    for (int i = 0; i < 50 && gui_menu_shown(); i++)
    {
        if (!i) gui_stop_menu();
        msleep(20);
        beat(PH_MENU);
    }
    beat(PH_LIST);
    if (build_list()) { close_req = 1; return; }
    for (int g = 0; g < WF_NGROUPS; g++) { gsel[g] = gstart[g]; gfirst[g] = FIRST_UNSET; }
    int i = index_of_id(wf_loaded());
    if (i < 0) i = index_of_id(last_id);
    if (i < 0) i = 0;
    sel = 0;
    if (nlist) select_film(i);
    focus = WF_ROW_LIGHT;
    rtop = 0;
    info = 0;
    msg = 0;
    glow_id = -1;

    beat(PH_FONTS);
    int fs = wff_open();
    wfp_init_fixed();
    beat(PH_CANS);
    int cs = wfc_open();
    if (close_req) return;
    beat(PH_DIALOG);
    dlg_take();
    if (close_req) return;
    beat(PH_SCREEN);
    canon_gui_disable_front_buffer();
    took = 1;
    if (dlg_mask)                                           /* Canon's dialog is behind the disabled front buffer now */
    {
        bmp_on();
        dlg_mask = 0;
    }
    beat(PH_PAL);
    wfp_take();
    char fw[96];                                /* wff_open / wfc_open return codes in words (0 = loaded) */
    snprintf(fw, sizeof fw, "%s%s%s", fs & 1 ? wff_why() : "ok", fs & 2 ? ", no argnor23" : "",
             fs & 4 ? ", no argnor28" : "");
    wf_log("ui: open, %d films, fonts WFFONTS.DAT %s, cans WFCANS.DAT %s (%d colours)", nlist, fw, wfc_why(cs),
           wfc_pal_used());
    wfp_log();
    int now = get_ms_clock();
    t_probe = t_force = t_heal = now;
    dirty = 1;
    beat(PH_TICK);
    st = ST_OPEN;
}

/* the load animation, one frame; 1 = it has run its time */
static int anim_tick(int now)
{
    int t = now - anim_t0;
    if (t >= ANIM_MS) return 1;
    beat(PH_ANIM);
    if (!wfg_begin())
    {
        if (!anim_drawn) wfui_render();                     /* the browser once, dimmed around the panel */
        wfui_render_anim(t, !anim_drawn);
        anim_drawn = 1;
        beat(PH_COPY);
        wfg_present();
    }
    if (now - t_probe >= PAL_PROBE_MS)
    {
        t_probe = now;
        beat(PH_PALCHK);
        wfp_keepalive(0);
    }
    return 0;
}

int wfui_task_tick(void)
{
    beat(PH_TICK);
    if (close_req) return 1;
    while (kq_r != kq_w)
    {
        int k = kq[kq_r];
        kq_r = (kq_r + 1) % KQ_N;
        act(k);
        if (close_req) return 1;
    }
    if (gui_menu_shown()) return 1;                         /* the ML menu took the screen */
    if (dlg_lost()) return 1;
    int now = get_ms_clock();
    if (anim_on) return anim_tick(now);
    uint32_t gen = wf_gen();
    if (gen != drawn_gen) dirty = 1;
    if (msg && now - msg_until >= 0) { msg = 0; dirty = 1; }
    if (dirty || now - t_heal >= HEAL_MS)
    {
        dirty = 0;
        drawn_gen = gen;
        if (!wfg_begin())
        {
            beat(PH_DRAW);
            wfui_render();
            beat(PH_COPY);
            wfg_present();
        }
        t_heal = t_probe = now;
    }
    else if (now - t_probe >= PAL_PROBE_MS)
    {
        int force = now - t_force >= PAL_FORCE_MS;
        if (force) t_force = now;
        t_probe = now;
        beat(PH_PALCHK);
        wfp_keepalive(force);
    }
    return 0;
}

void wfui_task_end(void)
{
    beat(PH_END);
    if (took)
    {
        wfg_clear_front();
        wfp_restore();
        int masked = dlg_give();                            /* mode 0 while the front buffer is still ours */
        canon_gui_enable_front_buffer(1);
        if (masked) bmp_on();
        took = 0;
    }
    else if (dlg_give())
        bmp_on();
    const struct wf_film *f = film_at(sel);
    if (f) last_id = f->id;
    wfc_close();
    wff_close();
    if (list) free(list);
    list = 0;
    nlist = 0;
    anim_on = edit = 0;
    st = ST_CLOSING;                                        /* the screen is Canon's: keys pass on */
    wf_log("ui: closed");
    int go_lv = lv_after && is5d2() && !lv && !RECORDING;
    lv_after = 0;
    if (go_lv)                                              /* loaded from the viewfinder: show the film in LiveView */
    {
        beat(PH_LV);
        force_liveview();
        wf_log("ui: LiveView after the load: %s", lv ? "on" : "still off");
    }
    st = ST_CLOSED;
}

static void ui_task(void *arg)
{
    (void)arg;
    wfui_task_begin();
    while (!wfui_task_tick()) msleep(TICK_MS);
    wfui_task_end();
}

#ifdef WF_HOST
int wfui_host_sel_id(void) { const struct wf_film *f = film_at(sel); return f ? f->id : 0; }
int wfui_host_group(void) { return group_of_sel(); }
int wfui_host_focus(void) { return focus; }
int wfui_host_rtop(void) { return rtop; }
int wfui_host_info(void) { return info; }
int wfui_host_anim(void) { return anim_on; }
int wfui_host_edit(void) { return edit; }
int wfui_host_phase(void) { return phase; }
#endif

/* ------------------------------------------------------------------ 5D2: Canon's dialog under the browser */
static int dlg_wait(int ms, int want_mode)
{
    for (int t = 0; t < ms; t += 20)
    {
        if (get_gui_mode() == want_mode && (!want_mode || display_is_on())) return 1;
        msleep(20);
        beat(PH_DIALOG);
    }
    return get_gui_mode() == want_mode && (!want_mode || display_is_on());
}

static void dlg_take(void)
{
    dlg_mode = dlg_seen = dlg_mask = dlg_want = 0;
    if (wf_cam() != WF_CAM_5D2 || RECORDING) return;
    dlg_want = lv ? DLG_LV : DLG_NOLV;
    int t0 = get_ms_clock(), gm0 = get_gui_mode(), d0 = display_is_on();
    dlg_wait(1000, 0);                      /* opened from the ML menu: its own mode 0 comes first .. */
    msleep(150);                            /* .. then its front buffer and redraw (menu_close) */
    beat(PH_DIALOG);
    if (close_req) return;
    if (get_gui_mode() == 0)
    {
        if (lv)                             /* piggyback_canon_menu: Canon's dialog never shows over LiveView */
        {
            bmp_off();
            dlg_mask = 1;
        }
        SetGUIRequestMode(dlg_want);
        dlg_mode = 1;
        msleep(200);
        dlg_wait(800, dlg_want);
    }
    if (!display_is_on())
    {
        display_on();
        for (int t = 0; t < 1000 && !display_is_on(); t += 20)
        {
            msleep(20);
            beat(PH_DIALOG);
        }
    }
    dlg_seen = display_is_on() && (!dlg_mode || get_gui_mode() == dlg_want);
    wf_log("ui: 5D2 %s: gui mode %d -> %d%s, display %d -> %d, %d ms", lv ? "LV" : "without LV", gm0, get_gui_mode(),
           dlg_mode ? (dlg_want == DLG_LV ? " (asked 38)" : " (asked 2)") : "", d0, display_is_on(), get_ms_clock() - t0);
}

/* 1 = Canon left the mode we asked for, or the display went off: close (else the next TRASH would open nothing) */
static int dlg_lost(void)
{
    if (!dlg_seen) return 0;
    if (display_is_on() && (!dlg_mode || get_gui_mode() == dlg_want)) return 0;
    wf_log("ui: 5D2: gui mode %d, display %d: closing", get_gui_mode(), display_is_on());
    return 1;
}

/* mode 0 again (close_canon_menu); 1 = the bitmap is muted, the caller unmutes it once the front buffer is Canon's */
static int dlg_give(void)
{
    int gm = get_gui_mode(), masked = dlg_mask;
    if (dlg_mode && (gm == dlg_want || gm == 0))    /* 0: our request may still be queued; 0 replaces it */
    {
        if (lv && !masked)
        {
            bmp_off();
            masked = 1;
        }
        SetGUIRequestMode(0);
        msleep(100);
    }
    dlg_mode = dlg_seen = dlg_mask = dlg_want = 0;
    return masked;
}

/* ------------------------------------------------------------------ wf_api.h */
void wf_ui_init(void)
{
    st = ST_CLOSED;
    close_req = 0;
    kq_r = kq_w = 0;
    list = 0;
    nlist = 0;
    took = 0;
    anim_on = lv_after = edit = 0;
}

int wf_ui_open(void)
{
    int now = get_ms_clock(), was;
    uint32_t old = cli();                   /* TRASH (GUI task) and the ML menu entry (wf_task) may race here */
    was = st;
    if (was == ST_CLOSED)
    {
        hb = now;
        phase = PH_CREATED;
        close_req = 0;
        kq_r = kq_w = 0;
        st = ST_OPENING;
    }
    sei(old);
    if (was != ST_CLOSED) return close_req || was == ST_CLOSING ? -1 : 0;
    stale_said = 0;
    uint32_t tid = task_create("wf_ui", UI_PRIO, UI_STACK, (void *)ui_task, 0);
    if (tid & 1)                            /* DryOS: handles even, errors odd */
    {
        st = ST_CLOSED;
        wf_log("ui: no UI task (%x)", (unsigned)tid);
        return -1;
    }
    return 0;                               /* host: task_create runs nothing, the harness calls begin / tick / end */
}

/* from a task that may sleep (module deinit): waits until the UI task has given the screen back */
void wf_ui_close(void)
{
    if (st == ST_CLOSED) return;
    close_req = 1;
#ifndef WF_HOST
    for (int i = 0; i < 100 && st != ST_CLOSED; i++) msleep(20);
#endif
}

int wf_ui_is_open(void) { return st != ST_CLOSED; }

unsigned int wf_ui_key(unsigned int key)
{
    if (st == ST_CLOSED || st == ST_CLOSING) return 1;
    int idle = get_ms_clock() - hb;
    if (idle > STALE_MS)                    /* the UI task has not run: Canon / ML get the keys, it closes later */
    {
        close_req = 1;
        if (!stale_said)
        {
            stale_said = 1;
            wf_log("ui: no beat for %d ms (state %d, phase %d): keys go to Canon / ML, closing", idle, st, phase);
        }
        return 1;
    }
    switch (key)
    {
    case MODULE_KEY_PRESS_HALFSHUTTER:
    case MODULE_KEY_PRESS_FULLSHUTTER:
    case MODULE_KEY_REC:
        close_req = 1;                                      /* shooting wins: close, and Canon gets the key */
        lv_after = 0;                                       /* (through the viewfinder, too: no LiveView then) */
        return 1;
    case MODULE_KEY_UNPRESS_HALFSHUTTER:
    case MODULE_KEY_UNPRESS_FULLSHUTTER:
        return 1;
    case MODULE_KEY_PRESS_LEFT: case MODULE_KEY_PRESS_RIGHT: case MODULE_KEY_PRESS_UP: case MODULE_KEY_PRESS_DOWN:
    case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_UP: case MODULE_KEY_WHEEL_DOWN:
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER: case MODULE_KEY_Q: case MODULE_KEY_PICSTYLE:
    case MODULE_KEY_TOUCH_1_FINGER: case MODULE_KEY_INFO: case MODULE_KEY_MENU: case MODULE_KEY_TRASH:
    case MODULE_KEY_PLAY: case MODULE_KEY_LV:
    {
        int w = kq_w, nw = (w + 1) % KQ_N;
        if (nw != kq_r) { kq[w] = key; kq_w = nw; }
        if (key == MODULE_KEY_MENU) close_req = 1;          /* EOS M 2-finger tap: the ML menu opens in 3 s */
        return 0;
    }
    }
    return 0;                                               /* everything else is ours while the browser is up */
}

void wf_ui_open_refused(unsigned int key, int play)
{
    if (key != MODULE_KEY_TRASH) return;
    wf_log("ui: TRASH not taken: open %d, ML menu %d, play %d, gui_state %d, lv %d, display %d", st != ST_CLOSED,
           gui_menu_shown(), play, gui_state, lv, display_is_on());
}

int wf_ui_open_key(unsigned int key)
{
    if (st != ST_CLOSED || key != MODULE_KEY_TRASH) return 0;  /* EOS M: long DOWN arrives as TRASH (patch 0002) */
    if (gui_menu_shown() || gui_state != GUISTATE_IDLE) return 0;
    if (wf_cam() != WF_CAM_5D2 && (!lv || is_movie_mode())) return 0;   /* EOS M: Canon's LV Set dialog = stills LV */
    return 1;
}
