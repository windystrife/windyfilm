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
 * 0x1C) develops a shot for seconds (RAW develop 35-45 s), and wf_task (0x1E) builds the grain tables for 2-19 s
 * without sleeping (the owner's RAWB2 log: a TRASH during that build, "ui: closed" right after "fx grain ... ready",
 * no "ui: open"; DryOS does not time-slice equal priorities, so the old prio-0x1E UI task waited the whole build).
 * Now the UI task runs above both, and it beats (hb, phase) at every step; a key that finds no beat for
 * STALE_MS (STALE_PACK_MS while it opens inside a pack read: a card busy with a shot) goes to Canon / ML and asks the
 * task to close when it runs again (the EOS M two-finger tap then opens the ML
 * menu: no module took MENU). The task is never declared dead from outside: it may still run and give the screen back.
 * Phases in the log: 1 created, 2 ML menu away, 3 film list, 4 fonts, 5 cans, 6 Canon dialog, 7 screen, 8 palette,
 * 9 tick, 10 drawing, 11 copy, 12 palette check, 13 load animation, 14 closing, 15 starting LiveView.
 *
 * The film OS (docs/MODERNUI.md, modernUI M1): with Camera interface = WindyFilm (wf_os) the MENU key in shooting opens
 * the Home screen (wf_os_ui.c) instead of Canon's menu (wf_ui_home_key), and MENU in the browser / the Darkroom goes back
 * to Home. MENU on Home closes the UI at once (the EOS M two-finger tap arrives as MENU too: ML opens its menu when we let
 * go) and, when no ML menu came, hands Canon its MENU (after_close): Canon's menu. Off: everything as before M1. */
#include "wf_ui_int.h"
#include "wf_icons.h"
#include "../wf_dark.h"
#include "../wf_rawb.h"

#define UI_PRIO         0x1B            /* above the film worker (0x1C), below ML's menu_task (0x1A) and Canon's GUI */
#define UI_STACK        0x2000
#define STALE_MS        2000
#define STALE_PACK_MS   10000           /* opening, inside a pack read: Canon may hold the card for seconds after a shot
                                           (owner RAWB9: "no beat for 2212 / 7256 ms (state 1, phase 5)", the cans read
                                           while the end-of-roll frame was saved); nothing of ours is on screen yet */
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
static int fonts_held;                  /* this open counted itself a font pack user (wff_open): its close gives that back */
static volatile int ui_quit;            /* wf_ui_close (unload): stop waiting for the ML menu to go */
static volatile int scr;                /* WFUI_SCR_*: the browser, or a Roll + Darkroom screen (wf_dark_ui.c) */
static volatile int scr_req = -1;       /* a switch asked for while open (the UI task makes it) */
static volatile int hb, phase;          /* the UI task's last sign of life (ms clock) and where it was */
static volatile int stale_said;
static volatile int close_after;        /* WFUI_AFTER_*: what the close hands Canon / ML once the screen is theirs */
static volatile int menu_pass, menu_pass_t;   /* our own MENU for Canon's menu (after Home): not Home again */
static inline void beat(int p) { phase = p; hb = get_ms_clock(); }

/* Pack reads (fonts, cans). Canon may hold the card for a while (owner's log 11-14, EOS M 10 s after boot: "fonts
 * WFFONTS.DAT not on the card, cans WFCANS.DAT read failed" with both files there). At open a busy read is tried again
 * until PACK_WAIT_MS (beating; a close request stops it). A pack still not read is tried again every PACK_RETRY_MS
 * while the browser is up, PACK_TRIES times, and drawn as soon as it loads; until then the small text is ML's term12
 * and the cans are the plain ones. A file the card says is not there is not tried again. */
#define PACK_WAIT_MS    1500
#define PACK_RETRY_MS   2000
#define PACK_TRIES      5
static int rd_until, rd_tries;          /* wfui_read: no more waiting after rd_until (ms clock); reads tried again */
static int pack_again, pack_n, t_pack;  /* bit 0 fonts, bit 1 cans still to read; retries made; the last one */

int wfui_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size)
{
    int ph = phase;
    for (;;)
    {
        beat(ph);
        int rc = wf_card_read(path, pos, p, n, size);
        if (rc == WF_RD_OK || rc == WF_RD_MISSING || rc == WF_RD_SHORT || close_req || get_ms_clock() - rd_until >= 0)
            return rc;
        rd_tries++;
        msleep(100);
    }
}

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
static const struct wf_film *anim_film;  /* the film being loaded (the browser's, or a roll's from wf_dark_ui.c) */
static int anim_exp;                    /* a roll: its exposures (0 = a film load) */
static int lv_after;                    /* a film was loaded: LiveView after the close */
static int edit;                        /* 5D2: joystick press, LEFT / RIGHT change the focused row */

static uint8_t *list;                   /* wf_films index of each ready film: by group, then order */
static int nlist;
static int sel, focus, info;
#define FIRST_UNSET (-100)
static int last_id;                     /* selection kept between opens */
static int dirty;
static uint32_t drawn_gen;
/* the rows: WF_NROWS (23: Bloom and Gauge, then the seven film-stage knobs since 11-15, four more and Paper 11-16) do not fit above the tabs, so ROWS_VIS of them show at 19 px and the list scrolls with the
   focus (the font keeps its size); rtop = the first row shown */
#define ROWS_VIS 7
static int rtop;
typedef char rows_vis_check[ROWS_VIS <= WF_NROWS && sizeof wf_icons / sizeof *wf_icons == WF_NROWS ? 1 : -1];
static int glow_id;
static const char *msg;
static int msg_until;
static int t_probe, t_force, t_heal;
static int disp_seen = 1;                                   /* the display read on at the last tick */

/* ------------------------------------------------------------------ the mode chip (owner 11-17: one screen)
 * Top right of the browser, in place of the loaded film's chip (the film's own "Loaded" badge says that): how the camera
 * shoots. JPEG = the film look on Canon's JPEG at once (RAW develop off, no roll); RAW develop = the slow look from
 * Canon's RAW at the shot (wf_rawb on, no roll); Darkroom = a roll now, prints later (the Exposures row tops the rows and
 * SET loads a roll of the film under the cursor: wf_roll_req_load, by the old Roll picker's rules). The mode is worked out
 * from the camera each time, so the ML menu's RAW develop / Film roll stay in step with it: a roll on the
 * camera (not set aside) = Darkroom; Darkroom picked here with no roll yet (mode_dk, kept between opens, never saved);
 * else RAW develop as its menu says. RAW develop with a roll at once stays the ML menu's: the chip shows Darkroom then.
 * Leaving Darkroom with a roll never loses a frame: one with frames asks first (End the roll now: rewound, the frames
 * wait for the Darkroom / Keep the roll), a finished one is set aside at once (wf_roll_shelve, wf_dark.h), an empty
 * one comes out. Focus (11-17, round 2): the top bar holds the section pill and this chip (below). EOS M (RAW develop and rolls are EOS M only): the 5D2 keeps the film chip.
 * The Roll picker screen is gone (owner 11-17: "the WindyFilm UI is nicer"): the ML menu's Film roll and the end-of-roll
 * notice's INFO open this browser in Darkroom mode on the Exposures row (wf_ui_open_scr(WF_UI_NEW_ROLL), new_roll_view).
 * The Blind roll row under it (owner RAWB28, 20-11: "trước đây bấm play ko thấy được ảnh lúc liveview h thì có tùy chọn
 * cho nhìn thấy"): the ML menu's Blind roll, the same setting (wf_blind, cfg blind). On (default) = as film: no image
 * review and PLAY blocked while the roll is in the camera; Off = Canon's review after each shot and PLAY shows the
 * frames (Canon's own JPEGs: the film look comes at the Darkroom). */
/* The section pill (owner 11-17, DESIGN 3.14) at the top left: Film | Film stage. Each section has its own tabs (TAP
 * cycles them, the counter counts inside it): Film = Colour / Cine / B&W / Film LUT, Film stage = the chain films by
 * their negative's family (Vision3 / Vision / ECN-EXR / Fuji / B&W). The section is the film under the cursor's; the
 * pill switches to the other section's last tab and film. Film stage: the chip reads JPEG 8-bit for JPEG (the look on
 * Canon's 8-bit JPEG, to compare with RAW develop's time), the Exposures row is there in every mode and SET always loads
 * a roll: RAW develop = B develops each frame at the shot, JPEG 8-bit = the look at the shot (wf_roll_look8), Darkroom
 * = Canon's own JPEG, the Darkroom prints later; every frame counts on the roll, and with a roll in the chip changes how
 * the next frames develop (no question: nothing is lost). Leaving a section with a roll in never loses a frame: one with
 * frames asks first (Close it and switch: rewound, set aside for the Darkroom / Keep the roll), a finished one is set
 * aside at once, an empty one comes out. Focus: the top bar is one stop above the rows (UP from the first row, DOWN
 * from the last); LEFT / RIGHT pick the pill or the chip there, SET / the ring / the joystick press toggle or step it.
 * The 5D2 has the same pill, chip and rolls (DESIGN 3.18, 11-17): its keys in the footers and the help (DIAL for the
 * ring, PICT for TAP, the joystick press edits a row). The film stage's two EXIF lines under the film name and the INFO
 * page show what the develop writes (wf_exif_text + wf_exif_tail). */
enum { M_JPEG = 0, M_RAW, M_DARK, M_N };
static const char *const mode_name[M_N] = { "JPEG", "RAW develop", "Darkroom" };
#define FOC_TOP  (-2)                           /* focus on the top bar: the section pill or the mode chip (tsub) */
#define FOC_EXP  (-1)                           /* focus on the Exposures row (a roll can be loaded) */
#define FOC_BLIND (-3)                          /* focus on the Blind roll row (under Exposures) */
#define NEXP 5
static const int exps[NEXP] = { 3, 6, 12, 24, 36 };
static int mode_dk;                             /* Darkroom picked on the chip, no roll on the camera yet */
static int exp_i = 3;                           /* the Exposures row: 24 */
static int leave_to = -1;                       /* the leave-Darkroom question is up: the mode asked for */
static int sec_to = -1;                         /* the leave-section question is up: the section asked for */
static int tsub;                                /* the top bar's focus: 0 the section pill, 1 the mode chip */
static const struct wf_film *sw_film;           /* the close-and-load question (a finished roll, SET on another film) */
static int pick, pick_num, pick_dev;            /* the Darkroom's develop again picks its film here (wfui_pick_film) */
static int sw_exp;
static int shelf, shelf_back, sh_top;          /* the film OS's Film shelf is up; its Details came from it; the
                                                  shelf's focus on the pill / chip (M2, below wfui_render) */
static void shelf_act(int k);
static void shelf_on(void);
static char msgb[96];                           /* a say() text built here */

/* the tabs: 0 .. 3 the Film section's groups, TAB_STAGE .. NTAB - 1 the Film stage families */
#define TAB_STAGE WF_GRP_STAGE
#define NTAB      (TAB_STAGE + 5)
static const char *const tname[NTAB] = { "Colour", "Cine", "B&W", "Film LUT", "Vision3", "Vision", "ECN/EXR", "Fuji", "B&W" };
static int tstart[NTAB + 1];
static int tsel[NTAB], tfirst[NTAB];
static int stab[2] = { 0, TAB_STAGE };          /* the last tab of each section */

static int pre(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
/* a film's tab: its group, or for a chain film the family of its negative by name (catalog.py "Vision3 5219 500T",
 * "Vision2 ..", "Vision ..", "Primetime ..", "EXR ..", "ECN-I ..", "Double-X 5222", the Fuji stocks) */
static int tab_of(const struct wf_film *f)
{
    int g = f->group % WF_NGROUPS;
    if (g != WF_GRP_STAGE) return g;
    const char *n = f->name ? f->name : "";
    return TAB_STAGE + (pre(n, "Vision3") ? 0 : pre(n, "Vision") || pre(n, "Primetime") ? 1
                        : pre(n, "EXR") || pre(n, "ECN") ? 2 : pre(n, "Double-X") ? 4 : 3);
}
static inline const struct wf_film *film_at(int i) { return i >= 0 && i < nlist ? &wf_films[list[i]] : 0; }
static inline int tcount(int t) { return tstart[t + 1] - tstart[t]; }
static inline int tab_of_sel(void) { return nlist ? tab_of(&wf_films[list[sel]]) : 0; }
static inline int sec_of_tab(int t) { return t >= TAB_STAGE; }
static inline int sec_now(void) { return sec_of_tab(tab_of_sel()); }   /* 0 Film, 1 Film stage */
static inline int sec_lo(int s) { return s ? TAB_STAGE : 0; }
static inline int sec_hi(int s) { return s ? NTAB : TAB_STAGE; }
static inline int acc(int t) { return t == WF_GRP_LUT ? C_TEAL : C_AMBER; }
static inline int accd(int t) { return t == WF_GRP_LUT ? C_TEALD : C_AMBERD; }
static inline int is5d2(void) { return wf_cam() == WF_CAM_5D2; }

static int mode_ok(int m)
{
    return m == M_JPEG || (m == M_RAW && wf_rawb_can()) || m == M_DARK;
}
static int chip_on(void) { return !pick && (mode_ok(M_RAW) || mode_ok(M_DARK)); }
static int roll_in(void) { const struct wf_roll *r = wf_roll_now(); return wf_roll_open(r) && !wf_roll_shelved(); }
static int roll_sec(void)                       /* the section of the roll on the camera (set aside too), -1 none */
{
    const struct wf_roll *r = wf_roll_now();
    const struct wf_film *f = wf_roll_open(r) ? wf_film_by_id(r->film) : 0;
    return f ? sec_of_tab(tab_of(f)) : -1;
}
/* a Film stage roll whose frames develop at the shot (RAW develop or JPEG 8-bit: wf_look_use builds the look for it):
 * nothing waits for the Darkroom, so leaving the section or a close-and-load just ends it, and the questions say so
 * (owner 11-17, round 2: the dialogs say what happens in the current mode) */
static int roll_devd(void)
{
    return roll_sec() == 1 && ((wf_rawb_can() && wf_rawb) || wf_roll_look8);
}
static int mode_now(void)
{
    if (mode_ok(M_DARK) && sec_now() && roll_in())          /* a Film stage roll: how its frames develop */
        return mode_ok(M_RAW) && wf_rawb ? M_RAW : wf_roll_look8 ? M_JPEG : M_DARK;
    if (mode_ok(M_DARK) && (roll_in() || mode_dk)) return M_DARK;
    return mode_ok(M_RAW) && wf_rawb ? M_RAW : M_JPEG;
}
static const char *mname(int m) { return m == M_JPEG && sec_now() ? "JPEG 8-bit" : mode_name[m]; }
static inline int dkm(void) { return mode_now() == M_DARK; }
/* the Exposures and Blind roll rows (2, else 0) and SET = a roll: Film in Darkroom mode, Film stage in every mode */
static int rollrows(void) { return !pick && mode_ok(M_DARK) && (sec_now() || dkm()) ? 2 : 0; }
/* a roll still shooting with frames keeps its count (the Exposures row shows it, greyed); a finished one (full, rewound)
 * leaves the row to the next roll: SET on a can then asks to close it and load that count (the close-and-load question) */
static int exp_locked(void)
{
    const struct wf_roll *r = wf_roll_now();
    return roll_in() && r->state == WF_RS_LOADED && r->nshot > 0;
}
static int exp_now(void) { return exp_locked() ? wf_roll_now()->exp : exps[exp_i]; }

/* positions UP / DOWN walk: the top bar, [Exposures, Blind roll], the WF_NROWS rows; lines (the list that scrolls):
 * [Exposures, Blind roll] rows */
static int nlines(void) { return WF_NROWS + rollrows(); }
static int line_of(int f) { return f == FOC_EXP ? 0 : f == FOC_BLIND ? 1 : f >= 0 ? f + rollrows() : -1; }
static int npos(void) { return nlines() + 1; }
static int pos_of(int f) { return f == FOC_TOP ? 0 : line_of(f) + 1; }
static int foc_at(int p)
{
    if (!p--) return FOC_TOP;
    if (rollrows())
    {
        if (!p--) return FOC_EXP;
        if (!p--) return FOC_BLIND;
    }
    return p;
}
/* the focus still valid (the mode may change under it: a roll loaded / closed by wf_task) and its line on screen */
static void focus_fix(void)
{
    if (((focus == FOC_EXP || focus == FOC_BLIND) && !rollrows()) || focus >= WF_NROWS || focus < FOC_BLIND)
        focus = WF_ROW_LIGHT;
    if (!chip_on()) tsub = 0;
    int l = line_of(focus), top_max = nlines() - ROWS_VIS;
    if (l >= 0 && l < rtop) rtop = l;
    if (l >= 0 && l >= rtop + ROWS_VIS) rtop = l - ROWS_VIS + 1;
    if (rtop > top_max) rtop = top_max;
    if (rtop < 0) rtop = 0;
}
static void focus_to(int f)
{
    focus = f;
    focus_fix();
    dirty = 1;
}
static void focus_move(int d)
{
    int n = npos();
    focus_to(foc_at((pos_of(focus) + d + n * 2) % n));
}

/* ------------------------------------------------------------------ film list and selection */
static int build_list(void)
{
    int n = wf_nfilms > 255 ? 255 : wf_nfilms;
    nlist = 0;
    list = malloc(n > 0 ? n : 1);
    if (!list) return -1;
    for (int t = 0; t < NTAB; t++)
    {
        int start = tstart[t] = nlist;
        for (int i = 0; i < n; i++)
        {
            const struct wf_film *f = &wf_films[i];
            if (!(f->flags & WF_F_READY) || tab_of(f) != t) continue;
            int k = nlist++;
            while (k > start && wf_films[list[k - 1]].order > f->order) { list[k] = list[k - 1]; k--; }
            list[k] = i;
        }
    }
    tstart[NTAB] = nlist;
    return 0;
}

static int index_of_id(int id)
{
    for (int i = 0; i < nlist; i++)
        if (wf_films[list[i]].id == id) return i;
    return -1;
}

/* strip scroll of tab t so that local index l sits in slots 1..8 (slot 0 and 9..10 are under the edge fades) */
static void place(int t, int l)
{
    int n = tcount(t), first = tfirst[t];
    if (first == FIRST_UNSET)
    {
        first = l - 4;
        if (first > n - 9) first = n - 9;
        int lo = l > 0 ? 0 : -1;
        if (first < lo) first = lo;
    }
    if (n <= 8) first = -1;                                 /* a short tab (Vision3 4, Fuji 9 no): all its cans in view */
    if (l - first < 1) first = l - 1;
    if (l - first > 8) first = l - 8;
    tfirst[t] = first;
}

static void select_film(int i)
{
    if (i < 0 || i >= nlist) return;
    sel = i;
    int t = tab_of_sel();
    tsel[t] = i;
    stab[sec_of_tab(t)] = t;
    place(t, i - tstart[t]);
    dirty = 1;
}

/* TAP: the next tab with films, inside the section */
static void next_tab(void)
{
    int t = tab_of_sel(), lo = sec_lo(sec_of_tab(t)), n = sec_hi(sec_of_tab(t)) - lo;
    for (int k = 1; k <= n; k++)
    {
        int h = lo + (t - lo + k) % n;
        if (!tcount(h)) continue;
        if (h == t) return;
        int i = tsel[h];
        if (i < tstart[h] || i >= tstart[h + 1]) i = tstart[h];
        select_film(i);
        return;
    }
}

/* LEFT / RIGHT: the previous / next film, across the tabs of the section only */
static void film_step(int d)
{
    int s = sec_now(), i = sel + d;
    if (i >= tstart[sec_lo(s)] && i < tstart[sec_hi(s)]) select_film(i);
}

static void say(const char *s)
{
    msg = s;
    msg_until = get_ms_clock() + MSG_MS;
    dirty = 1;
}

static void anim_start(const struct wf_film *f, int nexp)
{
    anim_film = f;
    anim_exp = nexp;
    anim_on = 1;                                            /* the animation, then close and LiveView */
    anim_t0 = get_ms_clock();
    anim_drawn = 0;
    lv_after = 1;
    edit = info = 0;
    msg = 0;
    dirty = 1;
}

/* why a film cannot load as a roll (the old Roll picker's words): a table not read yet is not "not on the card" (RAWB8) */
static const char *roll_why(const struct wf_film *f)
{
    if (f->flags & WF_F_LUTCAN) return "LUT cube not on the card";
    int fs = wf_films_state();
    return fs == 0 ? "Starting: film tables not read yet" : fs == -1 ? "Worker not running: film tables cannot load"
                   : "Film table not on the card";
}

/* Darkroom mode: SET on a can loads a roll of it with the Exposures row's count, by the old Roll picker's rules (moved
 * here 11-17): the roll in the camera picked again = the animation, no load; a roll still shooting with frames keeps its
 * film; a finished one asks to close it first (sw_film) */
static void roll_load(const struct wf_film *f)
{
    const struct wf_roll *r = wf_roll_now();
    int ne = exp_now();
    if (r->state == WF_RS_LOADED && r->film == f->id && r->exp == ne)
    {
        wf_log("ui: roll %d picked again (%d/%d)", r->num, r->nshot, r->exp);
        anim_start(f, ne);
        return;
    }
    if (wf_roll_open(r) && r->nshot > 0)
    {
        if (r->state == WF_RS_LOADED) { say("This roll has frames: PLAY develops it (Darkroom)"); return; }
        if (!wf_avail(f->id)) { say(roll_why(f)); return; }
        sw_film = f;
        sw_exp = exps[exp_i];
        dirty = 1;
        wf_log("ui: roll %d finished, close it for %d x%d?", r->num, f->id, sw_exp);
        return;
    }
    if (!wf_avail(f->id)) { say(roll_why(f)); return; }
    wf_roll_look8_set(sec_of_tab(tab_of(f)) && mode_now() == M_JPEG);   /* Film stage JPEG 8-bit: the look at the shot */
    int rc = wf_roll_req_load(f->id, ne);
    if (rc >= 0)
    {
        wf_log("ui: roll load %d x%d", f->id, ne);
        anim_start(f, ne);
        return;
    }
    say(rc == -3 ? "Busy, press SET again" : rc == -4 ? "Rolls come to this camera later (EOS M first)"
        : "Cannot load this roll");
}

static void swap_answer(int yes)
{
    const struct wf_film *nf = sw_film;
    int ne = sw_exp;
    sw_film = 0;
    dirty = 1;
    if (!yes) { wf_log("ui: close-and-load dropped (MENU)"); return; }
    int devd = roll_devd();                         /* (before the new roll's JPEG 8-bit is set below) */
    wf_roll_look8_set(sec_of_tab(tab_of(nf)) && mode_now() == M_JPEG);
    int rc = wf_roll_req_swap(nf->id, ne, devd);
    if (rc >= 0)
    {
        wf_log("ui: roll on the shelf for a new one, load %d x%d", nf->id, ne);
        anim_start(nf, ne);
        return;
    }
    say(rc == -3 ? "Busy, press SET again" : "Cannot load this roll");
}

static void mode_apply(int was, int m)
{
    mode_dk = m == M_DARK;
    if (mode_ok(M_RAW)) wf_rawb = m == M_RAW;               /* the ML menu's RAW develop follows (off at boot) */
    if (sec_now() && roll_in()) wf_roll_look8_set(m == M_JPEG);   /* a Film stage roll: how its next frames develop */
    if (m == M_DARK && wf_roll_shelved() && roll_sec() == sec_now())
        wf_roll_shelve(0);                                  /* this section's roll set aside is the camera's roll again */
    wf_log("ui: mode %s -> %s", mname(was), mname(m));
    focus_fix();
    dirty = 1;
}

/* the chip asks for mode m */
static void mode_set(int m)
{
    int cur = mode_now();
    if (m == cur || !mode_ok(m)) return;
    const struct wf_roll *r = wf_roll_now();
    if (sec_now() && roll_in())                             /* a Film stage roll stays in every mode: nothing is lost */
    {
        mode_apply(cur, m);
        snprintf(msgb, sizeof msgb, "Roll %03d goes on: the next frames %s", r->num,
                 m == M_JPEG ? "get the look at the shot" : m == M_RAW ? "develop at the shot" : "for the Darkroom");
        say(msgb);
        return;
    }
    if (cur != M_DARK || !roll_in()) { mode_apply(cur, m); return; }
    if (r->state == WF_RS_LOADED && r->nshot == 0)          /* nothing shot: the roll comes out */
    {
        int rc = wf_roll_req_close();
        if (rc < 0) { say(rc == -3 ? "Busy, press again" : "Cannot take the roll out"); return; }
        snprintf(msgb, sizeof msgb, "Roll %03d taken out: no frame on it", r->num);
        mode_apply(cur, m);
        say(msgb);
        return;
    }
    if (r->state == WF_RS_LOADED)                           /* frames on it: ask (End the roll now / Keep the roll) */
    {
        leave_to = m;
        dirty = 1;
        wf_log("ui: roll %03d has %d frame(s): end it for %s?", r->num, r->nshot, mname(m));
        return;
    }
    wf_roll_shelve(1);                                      /* finished: its frames wait for the Darkroom */
    snprintf(msgb, sizeof msgb, "Roll %03d waits for the Darkroom: PLAY develops it", r->num);
    mode_apply(cur, m);
    say(msgb);
}

static int mode_next(int d)
{
    int m = mode_now();
    for (int k = 0; k < M_N; k++)
    {
        m = (m + (d < 0 ? M_N - 1 : 1)) % M_N;
        if (mode_ok(m)) break;
    }
    return m;
}
static void mode_step(int d) { mode_set(mode_next(d)); }

static void leave_answer(int yes)
{
    int m = leave_to;
    leave_to = -1;
    dirty = 1;
    if (!yes) { wf_log("ui: roll kept, still Darkroom"); return; }
    const struct wf_roll *r = wf_roll_now();
    int n = r->nshot, rc = wf_roll_req_rewind();
    if (rc < 0) { say(rc == -3 ? "Busy, press SET again" : "Cannot end the roll"); return; }
    wf_roll_shelve(1);
    snprintf(msgb, sizeof msgb, "Roll %03d ended: %d frame%s wait for the Darkroom (PLAY)", r->num, n, n == 1 ? "" : "s");
    mode_apply(M_DARK, m);
    say(msgb);
}

/* the section pill: the other section's last tab and film; a roll of this section in the camera: see above */
static void sec_apply(int ns)
{
    int t = stab[ns];
    if (t < sec_lo(ns) || t >= sec_hi(ns) || !tcount(t))
        for (t = sec_lo(ns); t < sec_hi(ns) && !tcount(t); t++) ;
    if (t >= sec_hi(ns)) { say(ns ? "No Film stage films in this build" : "No Film section films in this build"); return; }
    int i = tsel[t];
    if (i < tstart[t] || i >= tstart[t + 1]) i = tstart[t];
    select_film(i);
    wf_log("ui: section %s", ns ? "Film stage" : "Film");
    focus_fix();
}

static void sec_set(int ns)
{
    int cs = sec_now();
    if (ns == cs) return;
    const struct wf_roll *r = wf_roll_now();
    if (!roll_in() || roll_sec() != cs) { sec_apply(ns); return; }
    if (r->state == WF_RS_LOADED && r->nshot == 0)          /* nothing shot: the roll comes out */
    {
        int rc = wf_roll_req_close();
        if (rc < 0) { say(rc == -3 ? "Busy, press again" : "Cannot take the roll out"); return; }
        snprintf(msgb, sizeof msgb, "Roll %03d taken out: no frame on it", r->num);
        sec_apply(ns);
        say(msgb);
        return;
    }
    if (r->state == WF_RS_LOADED)                           /* frames on it: ask (Close it and switch / Keep the roll) */
    {
        sec_to = ns;
        dirty = 1;
        wf_log("ui: roll %03d has %d frame(s): close it for %s?", r->num, r->nshot, ns ? "Film stage" : "Film");
        return;
    }
    if (roll_devd())                                        /* finished, developed at the shot: it ends */
    {
        int n = r->nshot, rc = wf_roll_req_close();
        if (rc < 0) { say(rc == -3 ? "Busy, press again" : "Cannot end the roll"); return; }
        wf_log("ui: roll %03d ended: %d frame(s) developed at the shot", r->num, n);
        snprintf(msgb, sizeof msgb, "Roll %03d ended: %d frame%s developed", r->num, n, n == 1 ? "" : "s");
        sec_apply(ns);
        say(msgb);
        return;
    }
    wf_roll_shelve(1);                                      /* finished: its frames wait for the Darkroom */
    snprintf(msgb, sizeof msgb, "Roll %03d waits for the Darkroom: PLAY develops it", r->num);
    sec_apply(ns);
    say(msgb);
}

/* the leave-section question's answer: SET = a Darkroom roll rewound and set aside (never a frame lost), a roll
 * developed at the shot ends (its files stay on the card); MENU = the roll stays */
static void sec_answer(int yes)
{
    int ns = sec_to;
    sec_to = -1;
    dirty = 1;
    if (!yes) { wf_log("ui: roll kept, still %s", ns ? "Film" : "Film stage"); return; }
    const struct wf_roll *r = wf_roll_now();
    int n = r->nshot, devd = roll_devd(), rc = devd ? wf_roll_req_close() : wf_roll_req_rewind();
    if (rc < 0) { say(rc == -3 ? "Busy, press SET again" : "Cannot end the roll"); return; }
    if (devd)
    {
        wf_log("ui: roll %03d ended: %d frame(s) developed at the shot", r->num, n);
        snprintf(msgb, sizeof msgb, "Roll %03d ended: %d frame%s developed", r->num, n, n == 1 ? "" : "s");
    }
    else
    {
        wf_roll_shelve(1);
        snprintf(msgb, sizeof msgb, "Roll %03d ended: %d frame%s wait for the Darkroom (PLAY)", r->num, n,
                 n == 1 ? "" : "s");
    }
    sec_apply(ns);
    say(msgb);
}

/* the top bar: SET / the ring / the joystick press on the pill toggles the section, on the chip steps the mode */
static void top_step(int d)
{
    if (tsub && chip_on()) mode_step(d);
    else sec_set(!sec_now());
}
static void top_lr(int t)                                   /* LEFT: the pill, RIGHT: the chip */
{
    if (t && !chip_on()) return;
    if (tsub != t) { tsub = t; dirty = 1; }
}

static void exp_step(int d)
{
    if (exp_locked()) { say("The roll in the camera keeps its count"); return; }
    exp_i = (exp_i + (d < 0 ? NEXP - 1 : 1)) % NEXP;
    dirty = 1;
}

/* the Blind roll row: the ring / LEFT / RIGHT (5D2 edit) switch it, either way */
static void blind_step(void)
{
    wf_blind_set(!wf_blind);
    wf_log("ui: blind roll %s", wf_blind ? "on" : "off");
    dirty = 1;
}

static void pick_back(const struct wf_film *f);
static void load_sel(void)
{
    const struct wf_film *f = film_at(sel);
    if (!f) return;
    if (pick)                                               /* the Darkroom's develop again: this film, back there */
    {
        if (!wf_avail(f->id)) { say(roll_why(f)); return; }
        pick_back(f);
        return;
    }
    if (rollrows()) { roll_load(f); return; }
    if (wf_loaded() == f->id) { anim_start(f, 0); return; } /* picked again (a roll's own film too): the same animation,
                                                               then LiveView, without a load (owner 11-14) */
    const struct wf_roll *ro = wf_roll_now();
    if (wf_roll_shelved() && ro->nshot > 0)                 /* set aside: the Darkroom prints it with its own film */
    {
        snprintf(msgb, sizeof msgb, "Roll %03d waits for the Darkroom: its film stays until then", ro->num);
        say(msgb);
        return;
    }
    if (wf_roll_open(ro) && ro->nshot > 0) { say("A roll is in the camera: its film stays until the darkroom"); return; }
    int r = wf_load(f->id);
    if (r == WF_LOAD_OK)
    {
        wf_iso_film(f->id, 0);                              /* owner 11-17: the camera's ISO to the film's speed */
        anim_start(f, 0);
        return;
    }
    int fs = wf_films_state();                              /* a table not read is not "not on the card" (RAWB8) */
    say(r == WF_LOAD_NOTABLE ? (fs == 0 ? "Starting: film tables not read yet, SET again soon"
                                : fs == -1 ? "Worker not running: film tables cannot load" : "Film table not on the card")
        : r == WF_LOAD_NOCUBE ? "LUT cube not on the card" : r == WF_LOAD_BUSY ? "Busy, press SET again" : "Unknown film");
    wf_log("ui: load %d -> %d", f->id, r);
}

/* the rows follow the film under the cursor, before SET (owner 11-16: a film-stage film lights its knob rows) */
static int row_film(void)
{
    const struct wf_film *f = film_at(sel);
    return f ? f->id : wf_loaded();
}

static void step_row(int d)
{
    if (focus == FOC_TOP) { top_step(d); return; }
    if (focus == FOC_EXP) { exp_step(d); return; }
    if (focus == FOC_BLIND) { blind_step(); return; }
    int id = row_film();
    if (!wf_row_avail_for(focus, id)) return;
    if (wf_row_step_for(focus, d, id)) dirty = 1;
}

/* PLAY with a roll on the camera: the Darkroom (owner 11-15) while it has frames to develop, else why not */
static void dev_key(void)
{
    const char *why = wfdr_dev_why();
    if (why) { say(why); return; }
    wf_log("ui: browser -> Darkroom");
    scr_req = WFUI_SCR_DARK;
}

static void act(int k)
{
    if (anim_on)
    {
        close_req = 1;                                      /* any key: skip the rest of the load animation */
        return;
    }
    if (scr != WFUI_SCR_BROWSER)
    {
        if (wfdr_key(scr, k))
        {
            if (scr == WFUI_SCR_DARK && k == MODULE_KEY_MENU && wf_os) wfui_goto(WFUI_SCR_HOME);   /* film OS: back */
            else close_req = 1;
        }
        dirty = 1;
        return;
    }
    if (pick && (k == MODULE_KEY_MENU || k == MODULE_KEY_TRASH || k == MODULE_KEY_PLAY || k == MODULE_KEY_LV))
    {
        pick_back(0);                                       /* the film pick: back to the Darkroom, no other film */
        return;
    }
    if (leave_to >= 0 || sw_film || sec_to >= 0)           /* a question is up: SET yes, MENU no, nothing else */
    {
        int yes = k == MODULE_KEY_PRESS_SET || k == MODULE_KEY_JOY_CENTER;
        if (yes || k == MODULE_KEY_MENU)
        {
            if (leave_to >= 0) leave_answer(yes);
            else if (sec_to >= 0) sec_answer(yes);
            else swap_answer(yes);
        }
        return;
    }
    if (shelf && !pick) { shelf_act(k); return; }          /* the film OS's Film shelf (M2) */
    focus_fix();
    int top = focus == FOC_TOP;
    switch (k)
    {
    case MODULE_KEY_PRESS_LEFT:   if (top) top_lr(0); else if (edit) step_row(-1); else film_step(-1); break;
    case MODULE_KEY_PRESS_RIGHT:  if (top) top_lr(1); else if (edit) step_row(1); else film_step(1); break;
    case MODULE_KEY_PRESS_UP:     focus_move(-1); break;
    case MODULE_KEY_PRESS_DOWN:   focus_move(1); break;
    case MODULE_KEY_WHEEL_LEFT:
    case MODULE_KEY_WHEEL_UP:     step_row(-1); break;
    case MODULE_KEY_WHEEL_RIGHT:
    case MODULE_KEY_WHEEL_DOWN:   step_row(1); break;
    case MODULE_KEY_JOY_CENTER:
        if (top) { top_step(1); break; }
        if (is5d2()) { edit = !edit; dirty = 1; break; }   /* 5D2: the joystick press edits the focused row */
        load_sel();
        break;
    case MODULE_KEY_PRESS_SET:    edit = 0; if (top) top_step(1); else load_sel(); break;
    case MODULE_KEY_Q:
    case MODULE_KEY_PICSTYLE:
    case MODULE_KEY_TOUCH_1_FINGER: edit = 0; next_tab(); break;
    case MODULE_KEY_INFO:         info = !info; dirty = 1; break;
    case MODULE_KEY_PLAY:                                   /* a roll on the camera or Darkroom mode: the Darkroom */
        if (wf_roll_open(wf_roll_now()) || rollrows() || wf_shelf_now()->n) { dev_key(); break; }
        close_req = 1;                                      /* it closes, as before */
        break;
    case MODULE_KEY_MENU:
        if (wf_os && shelf_back) { shelf_on(); break; }    /* the shelf's Details: back to the shelf */
        if (wf_os) { edit = info = 0; wfui_goto(WFUI_SCR_HOME); break; }   /* film OS: back to Home */
        close_req = 1;
        break;
    case MODULE_KEY_TRASH:
    case MODULE_KEY_LV:           close_req = 1; break;
    }
}

/* ------------------------------------------------------------------ for the film OS screens (wf_os_ui.c, UI task) */
void wfui_goto(int s)
{
    if (s >= 0 && s < WFUI_SCR_N) scr_req = s;
}

void wfui_close_after(int what)
{
    close_after = what;
    close_req = 1;
}

/* the browser on the film in the camera, the focus on a row (Home's look strip: SET / INFO there) */
void wfui_browse(int row)
{
    int i = index_of_id(wf_loaded());
    if (i >= 0) select_film(i);
    edit = info = 0;
    tsub = 0;
    shelf = shelf_back = 0;
    focus_to(row >= 0 && row < WF_NROWS ? row : WF_ROW_LIGHT);
    scr_req = WFUI_SCR_BROWSER;
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

/* "[KEY] word" pairs from x (y = the text's top); returns the end */
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

/* the mode's icon, 18 x 13 at (x, y), our own drawing: JPEG a picture (hills, sun), RAW develop the sensor's colour
 * filter squares, Darkroom a strip of film with its sprocket holes */
static void mode_icon(int m, int x, int y, int c, int bg)
{
    if (m == M_DARK)
    {
        wfg_rrect(x, y, x + 17, y + 12, 2, c, -1, 0);
        for (int i = 0; i < 4; i++)
        {
            wfg_rect(x + 2 + i * 4, y + 1, x + 3 + i * 4, y + 2, bg);
            wfg_rect(x + 2 + i * 4, y + 10, x + 3 + i * 4, y + 11, bg);
        }
        wfg_rect(x + 3, y + 4, x + 7, y + 8, bg);
        wfg_rect(x + 10, y + 4, x + 14, y + 8, bg);
        return;
    }
    wfg_rrect(x, y, x + 17, y + 12, 2, -1, c, 1);
    if (m == M_RAW)
    {
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 3; i++)
                wfg_rect(x + 3 + i * 4 + (j ? 2 : 0), y + 3 + j * 4, x + 5 + i * 4 + (j ? 2 : 0), y + 5 + j * 4, c);
        return;
    }
    wfg_tri(x + 2, y + 10, x + 7, y + 4, x + 11, y + 10, c);
    wfg_tri(x + 8, y + 10, x + 12, y + 6, x + 16, y + 10, c);
    wfg_ellipse(x + 12, y + 2, x + 15, y + 5, c);
}

/* the mode chip, right-aligned at xr (the Darkroom screen's Screen chip, the same look and keys); returns its width */
static int mode_chip(int xr, int foc)
{
    int m = mode_now();
    const struct wf_roll *r = wf_roll_now();
    char b[32];
    const char *t = mname(m);
    if (roll_in() && (m == M_DARK || sec_now()))            /* the name, then the count (two calls: the gate's string list) */
    {
        int n = snprintf(b, sizeof b, "%s ", t);
        if (n > 0 && n < (int)sizeof b) snprintf(b + n, sizeof b - n, "%d/%d", r->nshot, r->exp);
        t = b;
    }
    int pad = foc ? 20 : 12, w = pad + 18 + 8 + ui_textw(F_13, t) + pad, x0 = xr - w, ix = x0 + pad;
    int ic = foc ? C_AMBER : C_TXT2;
    wfg_rrect(x0, 5, xr, 27, 11, foc ? C_AMBERD : C_CHIPF, foc ? C_AMBER : C_CHIPO, 1);
    mode_icon(m, ix, 9, ic, foc ? C_AMBERD : C_CHIPF);
    ui_text(F_13, ix + 26, 8, t, foc ? C_TXT : C_TXT2);
    if (foc)
    {
        wfg_tri(xr - 13, 11, xr - 8, 16, xr - 13, 21, C_AMBER);
        wfg_tri(x0 + 13, 11, x0 + 8, 16, x0 + 13, 21, C_AMBER);
    }
    return w;
}

/* the section pill at x (the chip's look and keys): "Film | Film stage", the section in white; returns its right edge */
static int sec_pill(int x, int foc)
{
    int s = sec_now(), wa = ui_textw(F_13, "Film"), wb = ui_textw(F_13, "Film stage"), ws = ui_textw(F_13, "|");
    int pad = foc ? 20 : 12, w = pad + wa + 7 + ws + 7 + wb + pad, xr = x + w, tx = x + pad;
    wfg_rrect(x, 5, xr, 27, 11, foc ? C_AMBERD : C_CHIPF, foc ? C_AMBER : C_CHIPO, 1);
    ui_text(F_13, tx, 8, "Film", !s ? C_TXT : C_TXT3);
    ui_text(F_13, tx + wa + 7, 8, "|", C_LINE);
    ui_text(F_13, tx + wa + 7 + ws + 7, 8, "Film stage", s ? C_TXT : C_TXT3);
    if (foc)
    {
        wfg_tri(xr - 13, 11, xr - 8, 16, xr - 13, 21, C_AMBER);
        wfg_tri(x + 13, 11, x + 8, 16, x + 13, 21, C_AMBER);
    }
    return xr;
}

/* ------------------------------------------------------------------ areas */
static void draw_header(int now)
{
    (void)now;
    wfg_rrect(14, 6, 36, 28, 6, C_AMBER, -1, 0);                       /* own mark: a frame with sprockets + hill */
    for (int i = 0; i < 3; i++)
    {
        wfg_rect(17 + i * 6, 9, 20 + i * 6, 11, C_BGLAST);
        wfg_rect(17 + i * 6, 23, 20 + i * 6, 25, C_BGLAST);
    }
    wfg_tri(18, 21, 25, 14, 32, 21, C_BGLAST);
    int x = 44 + ui_text(F_ML23, 44, 4, "WindyFilm", C_TXT);
    int pe = sec_pill(x + 10, focus == FOC_TOP && !tsub && !info) + 10;   /* owner 11-17: in place of "by WindyStrife" */
    const char *s, *st_s = wf_status();
    if (pick)                                               /* the Darkroom's film pick: what SET is for */
    {
        char b[48];
        snprintf(b, sizeof b, "Film for ROLL %03d, develop %d", pick_num, pick_dev);
        int w = 24 + ui_textw(F_13, b), xr = WFUI_W - 12;
        wfg_rrect(xr - w, 5, xr, 27, 11, C_AMBERD, C_AMBER, 1);
        ui_text(F_13, xr - w + 12, 8, b, C_TXT);
        wfg_rect(12, 33, WFUI_W - 13, 33, C_LINE);
        return;
    }
    if (chip_on())                                          /* EOS M: the mode chip; a core status left of it */
    {
        int xr = WFUI_W - 12 - mode_chip(WFUI_W - 12, focus == FOC_TOP && tsub && !info) - 10;
        if (st_s && *st_s && xr - pe > 60)
        {
            int lw = ui_textw(F_13, st_s), lmax = xr - pe - 30;
            if (lw > lmax) lw = lmax;
            int x0 = xr - lw - 30;
            wfg_rrect(x0, 7, xr, 27, 10, C_SEP, C_WARN, 1);
            wfg_ellipse(x0 + 9, 13, x0 + 16, 20, C_WARN);
            ui_text_clip(F_13, x0 + 22, 9, st_s, C_TXT, xr - 8);
        }
        wfg_rect(12, 33, WFUI_W - 13, 33, C_LINE);
        return;
    }
    const struct wf_film *lf = wf_film_by_id(wf_loaded());  /* 5D2: the loaded film's chip, right of the pill */
    int fill = C_SEP, out = C_WARN, dot = C_WARN, tc = C_TXT;
    if (st_s && *st_s) s = st_s;
    else if (lf) { s = lf->name; fill = C_CHIPF; out = C_CHIPO; dot = C_GRN; }
    else { s = "No film loaded"; out = C_LINE; dot = C_BAROFF; tc = C_TXT2; }
    int lw = ui_textw(F_15, s), lmax = WFUI_W - 12 - 48 - pe;
    if (lw > lmax) lw = lmax;
    wfg_rrect(WFUI_W - 30 - lw - 18, 7, WFUI_W - 12, 27, 10, fill, out, 1);
    wfg_ellipse(WFUI_W - 26 - lw - 14, 13, WFUI_W - 26 - lw - 6, 21, dot);
    ui_text_clip(F_15, WFUI_W - 20 - lw, 8, s, tc, WFUI_W - 20);
    wfg_rect(12, 33, WFUI_W - 13, 33, C_LINE);
}

/* a message (say): a toast over the browser for MSG_MS (owner 11-17 mock: the header has the pill and the chip now) */
static void draw_toast(int now)
{
    if (!msg || now - msg_until >= 0) return;
    int w = ui_textw(F_15, msg), mw = WFUI_W - 80;
    if (w > mw) w = mw;
    int x0 = (WFUI_W - w) / 2 - 18, x1 = x0 + w + 36;
    wfg_rrect(x0, 188, x1, 224, 10, C_AMBERD, C_AMBER, 2);
    ui_text_clip(F_15, x0 + 18, 197, msg, C_TXT, x1 - 18);
}

/* SET Load button (or its Loaded / not-on-card state); returns its left edge */
static int draw_load_button(const struct wf_film *f, int g)
{
    int A = acc(g), dk = rollrows();
    const struct wf_roll *r = wf_roll_now();
    if (dk ? roll_in() && r->film == f->id : wf_loaded() == f->id)   /* Darkroom: the roll's film; else the loaded one */
    {
        const char *s = dk ? "On the roll" : "Loaded";
        int bw = ui_textw(F_15, s) + 30;
        wfg_rrect(706 - bw, 70, 706, 90, 10, C_CHIPF, C_CHIPO, 1);
        wfg_ellipse(706 - bw + 9, 76, 706 - bw + 16, 83, C_GRN);
        ui_text(F_15, 706 - bw + 21, 72, s, C_GRN);
        return 706 - bw;
    }
    if (!wf_avail(f->id))
    {
        int fs = (f->flags & WF_F_LUTCAN) ? 1 : wf_films_state();   /* the tables need the worker + pack first */
        const char *s = fs == 0 ? "Starting..." : fs == -1 ? "Worker not running" : "Not on card";
        int bw = ui_textw(F_15, s) + 20;
        wfg_rrect(706 - bw, 70, 706, 90, 10, -1, C_TXT3, 1);
        ui_text(F_15, 706 - bw + 10, 72, s, C_TXT3);
        return 706 - bw;
    }
    const char *ls = pick ? "Use" : dk ? "Load roll" : "Load";
    int wset = ui_textw(F_13, "SET");
    int bw = wset + ui_textw(F_15, ls) + 26;
    wfg_rrect(706 - bw, 70, 706, 90, 10, A, -1, 0);
    ui_text(F_13, 706 - bw + 9, 73, "SET", C_BGLAST);
    ui_text(F_15, 706 - bw + 17 + wset, 72, ls, C_BGLAST);
    return 706 - bw;
}

/* the Exposures row (Darkroom mode, on top of the rows): 3 / 6 / 12 / 24 / 36, the count picked in a pill; greyed with
 * the count of a roll in the camera still shooting with frames ("on the roll"); with a finished roll it is the next
 * roll's ("next roll"). The icon: our own film cassette with its leader */
static void draw_exp_line(int y, int A, int AD, int last)
{
    int foc = focus == FOC_EXP, lock = exp_locked(), cur = exp_now(), live = foc && !lock;
    if (foc)
    {
        wfg_rrect(258, y - 1, 708, y + 18, 6, AD, -1, 1);
        wfg_rrect(258, y - 1, 261, y + 18, 2, A, -1, 0);
    }
    int ic = lock ? C_TXT3 : foc ? A : C_TXT3;
    wfg_rrect(271, y + 3, 280, y + 15, 2, -1, ic, 1);                 /* the cassette */
    wfg_rect(274, y + 1, 277, y + 2, ic);                             /* its spool */
    wfg_rrect(281, y + 5, 286, y + 13, 1, -1, ic, 1);                 /* the leader */
    int nx = 294 + ui_text(F_15, 294, y, "Exposures", lock ? C_TXT3 : foc ? C_TXT : C_TXT2);
    if (lock) ui_text(F_13, nx + 10, y + 2, "on the roll", C_TXT3);
    else if (roll_in() && wf_roll_now()->nshot > 0) ui_text(F_13, nx + 10, y + 2, "next roll", foc ? C_TXT2 : C_TXT3);
    int xr = live ? 684 : 700, xl = xr;
    char b[8];
    for (int k = NEXP - 1; k >= 0; k--)
    {
        snprintf(b, sizeof b, "%d", exps[k]);
        int w = ui_textw(F_15, b), x = xr - w, on = exps[k] == cur;
        if (on)
            wfg_rrect(x - 6, y, x + w + 5, y + 17, 8, live ? A : -1, lock ? C_TXT3 : A, 1);
        ui_text(F_15, x, y, b, on ? (live ? C_BGLAST : lock ? C_TXT2 : C_TXT) : lock ? C_BAROFF : C_TXT3);
        xl = x - 6;
        xr = x - 16;
    }
    if (live)
    {
        wfg_tri(694, y + 4, 699, y + 9, 694, y + 14, A);
        wfg_tri(xl - 7, y + 4, xl - 12, y + 9, xl - 7, y + 14, A);
    }
    if (!foc && !last) wfg_rect(294, y + 18, 700, y + 18, C_SEP);
}

/* the Blind roll row (under Exposures): Off / On in pills as the counts are; the note beside the name says what PLAY
 * does with the roll in. The icon: an eye, struck through while the roll is blind */
static void draw_blind_line(int y, int A, int AD, int last)
{
    int foc = focus == FOC_BLIND, on = wf_blind != 0;
    if (foc)
    {
        wfg_rrect(258, y - 1, 708, y + 18, 6, AD, -1, 1);
        wfg_rrect(258, y - 1, 261, y + 18, 2, A, -1, 0);
    }
    int ic = foc ? A : C_TXT3;
    wfg_rrect(270, y + 4, 286, y + 14, 5, -1, ic, 1);                /* the eye */
    wfg_ellipse(275, y + 6, 281, y + 12, ic);                        /* its pupil */
    if (on)                                                          /* struck through */
        for (int k = 0; k < 15; k++) wfg_rect(271 + k, y + 16 - k, 272 + k, y + 16 - k, ic);
    int nx = 294 + ui_text(F_15, 294, y, "Blind roll", foc ? C_TXT : C_TXT2);
    ui_text(F_13, nx + 10, y + 2, on ? "no review, no PLAY" : "review + PLAY show the frames", foc ? C_TXT2 : C_TXT3);
    static const char *const v[2] = { "Off", "On" };
    int xr = foc ? 684 : 700, xl = xr;
    for (int k = 1; k >= 0; k--)
    {
        int w = ui_textw(F_15, v[k]), x = xr - w, cur = k == on;
        if (cur) wfg_rrect(x - 6, y, x + w + 5, y + 17, 8, foc ? A : -1, A, 1);
        ui_text(F_15, x, y, v[k], cur ? (foc ? C_BGLAST : C_TXT) : C_TXT3);
        xl = x - 6;
        xr = x - 16;
    }
    if (foc)
    {
        wfg_tri(694, y + 4, 699, y + 9, 694, y + 14, A);
        wfg_tri(xl - 7, y + 4, xl - 12, y + 9, xl - 7, y + 14, A);
    }
    if (!foc && !last) wfg_rect(294, y + 18, 700, y + 18, C_SEP);
}

/* Film stage (owner 11-17): two EXIF lines under the film's chips, what the develop writes (wf_exif_text, DESIGN 3.14):
 *   EXIF  <negative> -> <print stock> . Exp <film exp> . Push <n> . Comp <exp comp>
 *         Printer R/G/B . Interlayer <n> . <Light>
 * the separators drawn (our fonts are ASCII): a centre dot, an arrow after the negative */
static void seg_line(int x, int y, const char *const *t, int arrow, int c, int xmax)
{
    for (int i = 0; t[i]; i++)
    {
        if (i && i == 1 && arrow)
        {
            if (x + 20 > xmax) return;
            wfg_rect(x + 5, y + 8, x + 12, y + 8, c);
            wfg_tri(x + 11, y + 5, x + 15, y + 8, x + 11, y + 11, c);
            x += 20;
        }
        else if (i)
        {
            if (x + 12 > xmax) return;
            wfg_rect(x + 5, y + 7, x + 6, y + 8, c);
            x += 12;
        }
        if (x + ui_textw(F_13, t[i]) > xmax) { ui_text_clip(F_13, x, y, t[i], c, xmax); return; }
        x += ui_text(F_13, x, y, t[i], c);
    }
}
static const char *rv(int r, int id)                        /* a row's value for the film (a table's: no copy needed) */
{
    const char *v = wf_row_avail_for(r, id) ? wf_row_text_for(r, id) : 0;
    return v && *v ? v : "-";
}
static void exif_lines(const struct wf_film *f)
{
    char a[24], b[24], c[24], d[48], e[32];
    int id = f->id;
    const char *pu = rv(WF_ROW_SPUSH, id);
    snprintf(a, sizeof a, "Exp %s", rv(WF_ROW_SEXP, id));
    if (pre(pu, "Push") || pre(pu, "Pull")) snprintf(b, sizeof b, "%s", pu);
    else snprintf(b, sizeof b, "Push %s", pu);
    snprintf(c, sizeof c, "Comp %s", rv(WF_ROW_SCOMP, id));
    snprintf(d, sizeof d, "Printer %s/%s/%s", rv(WF_ROW_SPR, id), rv(WF_ROW_SPG, id), rv(WF_ROW_SPB, id));
    snprintf(e, sizeof e, "Interlayer %s", rv(WF_ROW_SIIE, id));
    const char *l1[] = { f->name ? f->name : "", rv(WF_ROW_PRINT, id), a, b, c, 0 };
    const char *l2[] = { d, e, rv(WF_ROW_LIGHT, id), 0 };
    int x = 264 + ui_text(F_13, 264, 97, "EXIF", C_AMBER) + 8;
    seg_line(x, 97, l1, 1, C_TXT2, 712);
    seg_line(x, 114, l2, 0, C_TXT2, 712);
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
    if (sec_now()) exif_lines(f);                           /* Film stage: what the develop writes */
    else
    {
        if (f->line1) ui_text_clip(F_15, 264, 96, f->line1, C_TXT2, 712);
        if (f->line2) ui_text_clip(F_15, 264, 113, f->line2, C_TXT2, 712);
    }

    int dk = rollrows(), nl = WF_NROWS + dk;
    for (int li = rtop; li < rtop + ROWS_VIS && li < nl; li++)
    {
        int y = 136 + (li - rtop) * 19, r = li - dk;
        if (r < 0)
        {
            if (r == -dk) draw_exp_line(y, A, AD, li == rtop + ROWS_VIS - 1);
            else draw_blind_line(y, A, AD, li == rtop + ROWS_VIS - 1);
            continue;
        }
        int foc = r == focus, av = wf_row_avail_for(r, f->id);
        if (foc)
        {
            wfg_rrect(258, y - 1, 708, y + 18, 6, AD, edit ? A : -1, 1);  /* 5D2 edit: outlined, LEFT / RIGHT step */
            wfg_rrect(258, y - 1, 261, y + 18, 2, A, -1, 0);
        }
        wfg_bits16(wf_icons[r], 270, y + 1, !av ? (foc ? C_TXT3 : C_BAROFF) : foc ? A : C_TXT3);
        const char *nm = wf_row_name(r);
        ui_text(F_15, 294, y, nm ? nm : "", !av ? C_TXT3 : foc ? C_TXT : C_TXT2);
        int live = foc && av;
        int vx = live ? 684 : 700;
        /* the long text (a print stock's full name) where it fits between the name and the value's arrows */
        const char *val = wf_row_text_long_for(r, f->id);
        if (!val || ui_textw(F_15, val) > vx - 18 - (294 + ui_textw(F_15, nm ? nm : "") + 16)) val = wf_row_text_for(r, f->id);
        if (!val) val = "";
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
        if (!foc && li != rtop + ROWS_VIS - 1) wfg_rect(294, y + 18, 700, y + 18, C_SEP);
    }
    if (nl > ROWS_VIS)                                      /* scroll bar: where the 7 shown rows sit among all */
    {
        int y0 = 136, h = ROWS_VIS * 19 - 1;
        int t0 = y0 + h * rtop / nl, t1 = y0 + h * (rtop + ROWS_VIS) / nl - 1;
        wfg_rect(712, y0, 713, y0 + h - 1, C_SEP);
        wfg_rect(712, t0, 713, t1, C_TXT3);
    }
}

static void draw_tabs(int t)
{
    char b[32];
    int s = sec_of_tab(t), tx = 22;
    for (int h = sec_lo(s); h < sec_hi(s); h++)
    {
        snprintf(b, sizeof(b), "%s %d", tname[h], tcount(h));
        int w = ui_textw(F_15, b);
        ui_text(F_15, tx, 276, b, h == t ? C_TXT : C_TXT3);
        if (h == t) wfg_rrect(tx, 294, tx + w, 296, 1, acc(h), -1, 0);
        tx += w + 24;
    }
    int a = tstart[sec_lo(s)];                              /* the counter counts inside the section */
    snprintf(b, sizeof(b), "%d / %d", sel - a + 1, tstart[sec_hi(s)] - a);
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
    int n = tcount(g), first = tfirst[g], l = sel - tstart[g], s = sec_of_tab(g);
    int sa = tstart[sec_lo(s)], sn = tstart[sec_hi(s)] - sa;   /* the section's films */
    for (int i = 0; i < STRIP_SLOTS; i++)
    {
        int x = -20 + i * 72;
        wfg_rrect(x - 6, 314, x + 58, 422, 4, C_FRAME, -1, 0);
    }
    for (int i = 0; i < STRIP_SLOTS; i++)
    {
        int li = first + i;
        if (li < 0 || li >= n || li == l) continue;
        const struct wf_film *f = film_at(tstart[g] + li);
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
    wfg_tri(8, 368, 16, 360, 16, 376, sel > sa ? C_TXT2 : C_BAROFF);
    wfg_tri(711, 368, 703, 360, 703, 376, sel < sa + sn - 1 ? C_TXT2 : C_BAROFF);

    /* scrollbar: the strip's window in the section's list */
    wfg_rrect(22, 442, 698, 445, 2, C_TRACK, -1, 0);
    if (sn)
    {
        int gf = tstart[g] - sa + (first < 0 ? 0 : first);
        int tw = 676 * STRIP_VIS / sn;
        if (tw > 676) tw = 676;
        if (tw < 8) tw = 8;
        int t0 = 22 + 676 * gf / sn;
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
    static const char *const k_dark[] = { "SET", "Load roll", "L/R", "Film", "UP/DN", "Row", "RING", "Value",   /* the */
                                          "TAP", "Group", "INFO", "Info", "MENU", "Exit", 0 };                /* mode */
    static const char *const k_pill[] = { "SET", "Film / Film stage", "L/R", "Section / mode", "DOWN", "Rows",
                                          "TAP", "Group", "INFO", "Info", "MENU", "Exit", 0 };   /* the top bar */
    static const char *const k_pill5[] = { "SET", "Film / Film stage", "DOWN", "Rows", "PICT", "Group", "INFO", "Info",
                                           "MENU", "Exit", 0 };
    const char *k_chip[] = { "SET", mname(mode_next(1)), "L/R", "Section / mode", "DOWN", "Rows",
                             is5d2() ? "PICT" : "TAP", "Group", "INFO", "Info", "MENU", "Exit", 0 };   /* SET names the
                                                                         mode it goes to (11-17) */
    static const char *const k_pill5c[] = { "SET", "Film / Film stage", "L/R", "Section / mode", "DOWN", "Rows",
                                            "PICT", "Group", "INFO", "Info", "MENU", "Exit", 0 };   /* 5D2, with the chip */
    static const char *const k_5d2d[] = { "SET", "Load roll", "L/R", "Film", "UP/DN", "Row", "DIAL", "Value",
                                          "JOY", "Edit", "PICT", "Group", "MENU", "Exit", 0 };   /* 5D2: the Darkroom mode */
    static const char *const k_5d2dp[] = { "SET", "Load roll", "UP/DN", "Row", "DIAL", "Value", "JOY", "Edit",
                                           "PICT", "Group", "MENU", "Exit", 0 };   /* room for "[PLAY] Darkroom" */
    static const char *const k_info[] = { "INFO", "Close", "MENU", "Exit", 0 };
    static const char *const k_darkp[] = { "SET", "Load roll", "L/R", "Film", "UP/DN", "Row", "RING", "Value",
                                           "TAP", "Group", "MENU", "Exit", 0 };   /* room for "[PLAY] Darkroom" */
    static const char *const k_ask[] = { "SET", "Yes", "MENU", "No", 0 };
    static const char *const k_pick[] = { "SET", "Use this film", "L/R", "Film", "UP/DN", "Row", "TAP", "Group",
                                          "MENU", "Back", 0 };   /* the Darkroom's develop again picks its film */
    static const char *const k_pick5[] = { "SET", "Use this film", "L/R", "Film", "UP/DN", "Row", "PICT", "Group",
                                           "MENU", "Back", 0 };
    int ask = leave_to >= 0 || sw_film || sec_to >= 0;
    int play = !ask && !info && !pick && (wf_roll_open(wf_roll_now()) || wf_shelf_now()->n);
    const char *const *k = ask ? k_ask : info ? k_info : pick && focus != FOC_TOP ? (is5d2() ? k_pick5 : k_pick)
                         : focus == FOC_TOP ? (tsub && chip_on() ? k_chip : is5d2() ? (chip_on() ? k_pill5c : k_pill5)
                                               : k_pill)
                         : is5d2() ? (edit ? k_5d2e : rollrows() ? (play ? k_5d2dp : k_5d2d) : k_5d2)
                         : rollrows() ? (play ? k_darkp : k_dark) : k_eosm;
    if (is5d2() && edit) play = 0;                          /* the row's editing keys only */
    hints(k, 14, 455, C_TXT2, C_TXT3);
    if (play)                                               /* owner 11-15: "[PLAY] Darkroom" while a roll is on the */
        wfdr_dev_hint(706 - wfdr_dev_hint_w(), 455);         /* camera, dim without frames (no roll: as before) */
}

/* the leave-Darkroom question (owner 11-17: never a frame lost) over the dimmed browser */
static void draw_leave(void)
{
    const struct wf_roll *r = wf_roll_now();
    const struct wf_film *rf = wf_film_by_id(r->film);
    int x0 = 96, x1 = 624, y0 = 120, y1 = 344, cx = 360, mw = x1 - x0 - 40, n = r->nshot;
    char a[64], b[96];
    wfg_scrim(x0, y0, x1, y1);
    wfg_rrect(x0, y0, x1, y1, 12, C_BGLAST, C_WARN, 2);
    snprintf(a, sizeof a, "Roll %03d: %d of %d frames shot", r->num, n, r->exp);
    text_c(F_ML23, cx, y0 + 20, a, C_TXT, mw);
    snprintf(b, sizeof b, "End the roll now and shoot %s?", mname(leave_to >= 0 ? leave_to : M_JPEG));
    text_c(F_15, cx, y0 + 64, b, C_TXT, mw);
    snprintf(b, sizeof b, "The %d frame%s shot go%s to the Darkroom: PLAY develops %s", n, n == 1 ? "" : "s",
             n == 1 ? "es" : "", n == 1 ? "it" : "them");
    text_c(F_15, cx, y0 + 92, b, C_AMBER, mw);
    snprintf(b, sizeof b, "with %s, which stays loaded until then.", rf ? rf->name : "the roll's film");
    text_c(F_13, cx, y0 + 124, b, C_TXT2, mw);
    text_c(F_13, cx, y0 + 142, "New shots are not on the roll.", C_TXT2, mw);
    static const char *const k[] = { "SET", "End the roll now", "MENU", "Keep the roll", 0 };
    int w = hints(k, 2000, y1 - 40, C_TXT, C_TXT2) - 2000 - 12;
    hints(k, cx - w / 2, y1 - 40, C_TXT, C_TXT2);
}

/* the leave-section question (owner 11-17: the pill with a roll of this section shooting; never a frame lost) */
static void draw_sec_leave(void)
{
    const struct wf_roll *r = wf_roll_now();
    const struct wf_film *rf = wf_film_by_id(r->film);
    int x0 = 96, x1 = 624, y0 = 120, y1 = 344, cx = 360, mw = x1 - x0 - 40, n = r->nshot;
    char a[64], b[96];
    wfg_scrim(x0, y0, x1, y1);
    wfg_rrect(x0, y0, x1, y1, 12, C_BGLAST, C_WARN, 2);
    snprintf(a, sizeof a, "Roll %03d: %d of %d frames shot", r->num, n, r->exp);
    text_c(F_ML23, cx, y0 + 20, a, C_TXT, mw);
    snprintf(b, sizeof b, "Close it and switch to %s?", sec_to ? "Film stage" : "Film");
    text_c(F_15, cx, y0 + 64, b, C_TXT, mw);
    if (roll_devd())                                        /* RAW develop / JPEG 8-bit: developed at the shot */
    {
        snprintf(b, sizeof b, "The %d frame%s %s already developed. The roll just ends here.", n, n == 1 ? "" : "s",
                 n == 1 ? "is" : "are");
        text_c(F_15, cx, y0 + 92, b, C_AMBER, mw);
        text_c(F_13, cx, y0 + 124, "The files stay on the card.", C_TXT2, mw);
    }
    else
    {
        snprintf(b, sizeof b, "The %d frame%s shot wait%s for the Darkroom: PLAY develops %s", n, n == 1 ? "" : "s",
                 n == 1 ? "s" : "", n == 1 ? "it" : "them");
        text_c(F_15, cx, y0 + 92, b, C_AMBER, mw);
        snprintf(b, sizeof b, "with %s, which stays loaded until then.", rf ? rf->name : "the roll's film");
        text_c(F_13, cx, y0 + 124, b, C_TXT2, mw);
        text_c(F_13, cx, y0 + 142, "New shots are not on the roll.", C_TXT2, mw);
    }
    static const char *const k[] = { "SET", "Close and switch", "MENU", "Keep the roll", 0 };
    int w = hints(k, 2000, y1 - 40, C_TXT, C_TXT2) - 2000 - 12;
    hints(k, cx - w / 2, y1 - 40, C_TXT, C_TXT2);
}

/* the close-and-load question (a finished roll, SET on another film in Darkroom mode; moved here from the old Roll
 * picker). Owner 11-17: the roll is not dropped any more, it goes on the shelf (wf_roll_req_swap) and the Darkroom
 * develops it later; one developed at the shot (RAW develop / JPEG 8-bit) goes on it developed */
static void draw_swap(void)
{
    const struct wf_roll *r = wf_roll_now();
    int x0 = 100, x1 = 620, y0 = 128, y1 = 352, cx = 360, mw = x1 - x0 - 40, left = wf_roll_left(r);
    char a[64], b[80];
    wfg_scrim(x0, y0, x1, y1);
    wfg_rrect(x0, y0, x1, y1, 12, C_BGLAST, C_WARN, 2);
    text_c(F_ML23, cx, y0 + 22, "Load the new roll?", C_TXT, mw);
    snprintf(a, sizeof a, "%s  x%d", sw_film->name, sw_exp);
    text_c(F_15, cx, y0 + 62, a, C_AMBER, mw);
    if (roll_devd())                                        /* developed at the shot: nothing waits */
    {
        snprintf(b, sizeof b, "Roll %03d (%d frame%s, developed at the shot) goes on the shelf:", r->num, r->nshot,
                 r->nshot == 1 ? "" : "s");
        text_c(F_15, cx, y0 + 96, b, C_TXT, mw);
        text_c(F_15, cx, y0 + 118, "the Darkroom can print it again later.", C_TXT, mw);
    }
    else
    {
        snprintf(b, sizeof b, "Roll %03d (%d frame%s) goes on the shelf:", r->num, left, left == 1 ? "" : "s");
        text_c(F_15, cx, y0 + 96, b, C_TXT, mw);
        text_c(F_15, cx, y0 + 118, "the Darkroom develops it later.", C_TXT, mw);
    }
    text_c(F_13, cx, y0 + 150, "PLAY > Darkroom picks it. Its RAW/JPG files stay on the card.", C_TXT2, mw);
    static const char *const k[] = { "SET", "Shelve and load", "MENU", "Cancel", 0 };
    int w = hints(k, 2000, y1 - 40, C_TXT, C_TXT2) - 2000 - 12;
    hints(k, cx - w / 2, y1 - 40, C_TXT, C_TXT2);
}

/* INFO (owner 11-17, both sections): under the title the film's chips (kind / ISO / format) and its catalogue
 * description (line1 / line2: the Film films' browser text; the chain films' facts line and official name); the grid,
 * two columns: Film, Table, Daylight (the old overlay's), what the develop writes into the EXIF and the print comment
 * (the Film stage's own keys in amber), the mode, the last develop's time and path (wf_dev_last), the film table on the
 * card; the key help (the old overlay's block, today's keys); one note line. 720 x 480 with the footer: a chain film's
 * 26 pairs (13 rows of 17 px, 15 px text as the browser's description) end at y 357, the keys 366..414, the note 424.
 * INFO again closes it. */
#define INFO_Y0   136                                       /* the grid's first row (below the description) */
#define INFO_DY   17                                        /* its pitch */
#define INFO_KEYS 366                                       /* the key help's first row (3 rows of 16 px) */
static int ipair, ilast;                                    /* the pairs drawn so far (row-major, two columns), last y */
static void info_pair(const char *k, const char *v, int kc)
{
    int col = ipair & 1, x = col ? 372 : 22, y = INFO_Y0 + (ipair >> 1) * INFO_DY, xm = col ? 698 : 356;
    ilast = y;
    ui_text_clip(F_15, x, y, k, kc, x + 124);
    ui_text_clip(F_15, x + 128, y, v ? v : "", C_TXT, xm);
    ipair++;
}
/* the key help (owner 11-17, "all three back": the old overlay's block with today's keys; the 5D2's key names as its
 * footer's): three columns, column-major, the key in C_TXT2, its action after the column's widest key, 13 px */
static void info_keys(int y)
{
    static const char *const h_eosm[] = { "L/R", "film, or section / mode", "UP/DN", "row, or the top bar",
        "RING", "value", "SET", "load the film, or a roll", "TAP", "next group (1 finger)", "INFO", "this page",
        "PLAY", "Darkroom (a roll on)", "MENU", "close, or 2-finger tap", "Half-shutter", "close, shoot" };
    static const char *const h_5d2[] = { "L/R", "film, or section / mode", "UP/DN", "row, or the top bar",
        "DIAL", "value", "SET", "load the film, or a roll", "JOY", "edit a row (L/R)", "PICT", "next group",
        "PLAY", "Darkroom (a roll on)", "MENU", "close, or TRASH", "Half-shutter", "close, shoot" };
    const char *const *h = is5d2() ? h_5d2 : h_eosm;
    for (int c = 0; c < 3; c++)
    {
        int x = 22 + c * 226, kw = 0;
        for (int r = 0; r < 3; r++)
        {
            int t = ui_textw(F_13, h[(c * 3 + r) * 2]);
            if (t > kw) kw = t;
        }
        for (int r = 0; r < 3; r++)
        {
            const char *const *e = h + (c * 3 + r) * 2;
            ui_text(F_13, x, y + r * 16, e[0], C_TXT2);
            ui_text_clip(F_13, x + kw + 8, y + r * 16, e[1], C_TXT3, c == 2 ? 698 : x + 220);
        }
    }
}

static void info_row(int r, int id, int st)
{
    if (!wf_row_avail_for(r, id)) return;
    const char *nm = wf_row_name(r), *v = wf_row_text_long_for(r, id);
    if (!v || ui_textw(F_15, v) > 220) v = wf_row_text_for(r, id);
    info_pair(nm ? nm : "", v, st ? C_AMBER : C_TXT3);
}

static void draw_info(const struct wf_film *f, int g)
{
    int s = sec_of_tab(g), id = f->id, m = mode_now();
    char b[64];
    wfg_rect(0, 34, WFUI_W - 1, 447, C_BG0);
    int w = ui_text(F_ML23, 22, 40, "Info", C_TXT);
    wfg_rect(22 + w + 8, 53, 22 + w + 10, 55, C_TXT3);
    ui_text_clip(F_ML23, 22 + w + 18, 40, f->name, C_TXT, 600);
    text_r(F_13, 698, 48, s ? "FILM STAGE" : "FILM", C_AMBER);
    int x = chip_s(22, 70, f->kind, acc(g), 697);           /* the chips and the description, as the browser's */
    snprintf(b, sizeof b, "ISO %d", f->iso);
    x = chip_s(x, 70, b, C_TXT2, 697);
    chip_s(x, 70, f->format, C_TXT2, 697);
    if (f->line1) ui_text_clip(F_15, 22, 91, f->line1, C_TXT2, 697);
    if (f->line2) ui_text_clip(F_15, 22, 108, f->line2, C_TXT2, 697);
    wfg_rect(22, INFO_Y0 - 6, 697, INFO_Y0 - 6, C_LINE);
    ipair = 0;
    ilast = 0;
    info_pair("Film", f->name, C_TXT3);
    info_pair("Table", f->code && *f->code ? f->code : "-", C_TXT3);   /* a chain film: its negative's (G08) */
    snprintf(b, sizeof b, "%d K%s", f->kelvin, f->kelvin < 4000 ? " (tungsten)" : "");  /* Light Day's balance */
    info_pair("Daylight", b, C_TXT3);
    static const uint8_t rows_film[] = { WF_ROW_PRINT, WF_ROW_STRENGTH, WF_ROW_PAPER, WF_ROW_HL, WF_ROW_SH, WF_ROW_FLUT,
                                         WF_ROW_LIGHT, WF_ROW_GRAIN, WF_ROW_HALO, WF_ROW_BLOOM, WF_ROW_GAUGE, WF_ROW_MIST };
    static const uint8_t rows_stage[] = { WF_ROW_SEXP, WF_ROW_SPUSH, WF_ROW_SCOMP, 0xFF, WF_ROW_SIIE, WF_ROW_SBL,
                                          WF_ROW_SPBL, WF_ROW_SNN, WF_ROW_SNP };
    for (int i = 0; i < (int)sizeof rows_film; i++)
    {
        int r = rows_film[i];
        if (s && r == WF_ROW_FLUT) continue;               /* a chain film has no Film LUT */
        info_row(r, id, 0);
        if (s && r == WF_ROW_STRENGTH)                      /* the film stage's knobs after the print and its strength */
            for (int j = 0; j < (int)sizeof rows_stage; j++)
            {
                if (rows_stage[j] != 0xFF) { info_row(rows_stage[j], id, 1); continue; }
                snprintf(b, sizeof b, "%s / %s / %s", rv(WF_ROW_SPR, id), rv(WF_ROW_SPG, id), rv(WF_ROW_SPB, id));
                info_pair("Printer R G B", b, C_AMBER);
            }
    }
    if (rollrows() && roll_in()) snprintf(b, sizeof b, "%s, roll %d exp", mname(m), wf_roll_now()->exp);
    else snprintf(b, sizeof b, "%s", mname(m));
    info_pair("Mode", b, C_TXT3);
    int lp = 0, ms = wf_dev_last(&lp);
    if (ms > 0)
    {
        int t = (ms + 50) / 100;
        snprintf(b, sizeof b, "%d.%d s (%s)", t / 10, t % 10, wf_path_name(lp));
    }
    else snprintf(b, sizeof b, "none yet");
    info_pair("Last develop", b, C_TXT3);
    int lut = f->flags & WF_F_LUTCAN, av = wf_avail(f->id), fs = wf_films_state();
    info_pair("On the card", av ? (lut ? "LUT cube found" : "Film table found") : lut ? "LUT cube missing"
              : fs == 0 ? "Starting (tables not read yet)" : fs == -1 ? "Worker not running" : "Film table missing",
              C_TXT3);
    wfg_rect(22, INFO_KEYS - 5, 697, INFO_KEYS - 5, C_LINE);
    info_keys(INFO_KEYS);
    char nb[128];                                           /* one note line (the key help says INFO closes) */
    snprintf(nb, sizeof nb, "%sWritten to EXIF and the print comment %s", s ? "Amber = Film stage only. " : "",
             m == M_DARK ? "when the Darkroom develops the roll." : m == M_RAW ? "right after the shot (RAW develop)."
             : s ? "with the JPEG at the shot (8-bit path)." : "with the JPEG at the shot.");
    ui_text_clip(F_13, 22, 424, nb, C_TXT2, 698);
}

/* ------------------------------------------------------------------ the film OS's Film shelf (modernUI M2, mock 03)
 * With Camera interface = WindyFilm the browser opens on the shelf (Home's Film shelf tile, the EOS M's long DOWN): the
 * film OS's top bar with the section pill (and the EOS M's mode chip); on the left the film under the cursor (big can,
 * name, chips, its two lines, why it cannot load), on the right the section's tabs and the tab's films in a 4-column
 * grid (the cursor's can larger in an outline, the loaded film's green dot, a scroll bar past 3 rows). The browser's own
 * list, tabs, selection, loads, questions and toasts. INFO opens the browser's own screen (Details: the look rows,
 * Exposures / Blind roll, its INFO page) and MENU there comes back to the shelf; MENU on the shelf goes to Home.
 * Keys: LEFT / RIGHT and the ring / dial the film (across the section's tabs), UP / DOWN a grid row (UP from the first
 * row: the pill / chip, as the browser's top bar), TAP / PICT / Q the next tab, SET / the joystick press load (or step
 * the pill / chip). Off (Camera interface Canon + ML), the Darkroom's film pick and a new roll: the browser as before. */
#define SH_COLS 4
#define SH_ROWS 3
#define SH_X0   304                                         /* the grid's left edge, 4 columns of SH_CW */
#define SH_CW   98
#define SH_Y0   84                                          /* the first visible row's top, rows SH_RH apart */
#define SH_RH   116
static int sh_row[NTAB];                                    /* each tab's first visible grid row */

static void sh_place(void)
{
    int t = tab_of_sel(), r = (sel - tstart[t]) / SH_COLS, nr = (tcount(t) + SH_COLS - 1) / SH_COLS;
    if (r < sh_row[t]) sh_row[t] = r;
    if (r >= sh_row[t] + SH_ROWS) sh_row[t] = r - SH_ROWS + 1;
    if (sh_row[t] > nr - SH_ROWS) sh_row[t] = nr - SH_ROWS;
    if (sh_row[t] < 0) sh_row[t] = 0;
}

/* the shelf (again): the cursor's row in view, the focus on the films */
static void shelf_on(void)
{
    shelf = 1;
    shelf_back = 0;
    sh_top = 0;
    edit = info = 0;
    if (nlist) sh_place();
    dirty = 1;
}

/* the Film shelf on the film in the camera (Home's Film shelf tile, the EOS M's long DOWN on Home) */
void wfui_shelf(void)
{
    int i = index_of_id(wf_loaded());
    if (i >= 0) select_film(i);
    tsub = 0;
    focus_to(WF_ROW_LIGHT);
    shelf_on();
    scr_req = WFUI_SCR_BROWSER;
}

static void sh_film_step(int d)
{
    film_step(d);
    sh_place();
}

/* UP / DOWN: a row of the tab; UP from the first row: the pill / chip; DOWN from there: the films again */
static void sh_row_step(int d)
{
    if (sh_top)
    {
        if (d > 0) { sh_top = 0; dirty = 1; }
        return;
    }
    int t = tab_of_sel(), l = sel - tstart[t], n = tcount(t);
    if (d < 0 && l < SH_COLS) { sh_top = 1; tsub = 0; dirty = 1; return; }
    int nl = l + d * SH_COLS;
    if (nl >= n) nl = (n - 1) / SH_COLS > l / SH_COLS ? n - 1 : l;   /* into a short last row: its last film */
    if (nl < 0 || nl == l) return;
    select_film(tstart[t] + nl);
    sh_place();
}

static void shelf_act(int k)
{
    switch (k)
    {
    case MODULE_KEY_PRESS_LEFT:   if (sh_top) top_lr(0); else sh_film_step(-1); break;
    case MODULE_KEY_PRESS_RIGHT:  if (sh_top) top_lr(1); else sh_film_step(1); break;
    case MODULE_KEY_WHEEL_LEFT:
    case MODULE_KEY_WHEEL_UP:     if (sh_top) top_step(-1); else sh_film_step(-1); break;
    case MODULE_KEY_WHEEL_RIGHT:
    case MODULE_KEY_WHEEL_DOWN:   if (sh_top) top_step(1); else sh_film_step(1); break;
    case MODULE_KEY_PRESS_UP:     sh_row_step(-1); break;
    case MODULE_KEY_PRESS_DOWN:   sh_row_step(1); break;
    case MODULE_KEY_JOY_CENTER:
    case MODULE_KEY_PRESS_SET:
        if (sh_top) top_step(1);
        else load_sel();
        if (nlist) sh_place();
        break;
    case MODULE_KEY_Q:
    case MODULE_KEY_PICSTYLE:
    case MODULE_KEY_TOUCH_1_FINGER: next_tab(); sh_place(); break;
    case MODULE_KEY_INFO:                                   /* the browser's own screen on this film */
        shelf = 0;
        shelf_back = 1;
        sh_top = 0;
        focus_to(WF_ROW_LIGHT);
        wf_log("ui: shelf -> details");
        dirty = 1;
        break;
    case MODULE_KEY_PLAY:                                   /* as the browser: a roll or Darkroom mode, the Darkroom */
        if (wf_roll_open(wf_roll_now()) || rollrows() || wf_shelf_now()->n) { dev_key(); break; }
        close_req = 1;
        break;
    case MODULE_KEY_MENU:         sh_top = 0; wfui_goto(WFUI_SCR_HOME); break;
    case MODULE_KEY_TRASH:
    case MODULE_KEY_LV:           close_req = 1; break;
    }
}

static void sh_tabs(int t)
{
    char b[32];
    int s = sec_of_tab(t), tx = SH_X0 + 10;
    for (int h = sec_lo(s); h < sec_hi(s); h++)
    {
        snprintf(b, sizeof(b), "%s %d", tname[h], tcount(h));
        int w = ui_textw(F_15, b);
        if (tx + w > 704) break;
        ui_text(F_15, tx, 50, b, h == t ? C_TXT : C_TXT3);
        if (h == t) wfg_rrect(tx, 70, tx + w, 72, 1, acc(h), -1, 0);
        tx += w + 18;
    }
}

static void sh_grid(int t)
{
    int A = acc(t), AD = accd(t), loaded = wf_loaded();
    int n = tcount(t), r0 = sh_row[t], l = sel - tstart[t], nr = (n + SH_COLS - 1) / SH_COLS;
    for (int rr = 0; rr < SH_ROWS; rr++)
    {
        int y = SH_Y0 + rr * SH_RH;
        if ((r0 + rr) * SH_COLS >= n) break;
        wfg_rect(SH_X0 + 6, y + 86, 694, y + 87, C_SEP);    /* the shelf board under the row */
        for (int c = 0; c < SH_COLS; c++)
        {
            int li = (r0 + rr) * SH_COLS + c;
            if (li >= n) break;
            const struct wf_film *f = film_at(tstart[t] + li);
            int cx = SH_X0 + c * SH_CW + SH_CW / 2, on = li == l;
            if (on)
            {
                wfg_rrect(cx - 38, y - 3, cx + 37, y + 88, 8, AD, sh_top ? C_LINE : A, 2);
                wfc_draw(f, CAN_SEL, cx - 29, y + 2, AD);
            }
            else wfc_draw(f, CAN_STRIP, cx - 26, y + 12, C_BG0);
            if (f->id == loaded) wfg_ellipse(cx + 22, y + 4, cx + 29, y + 11, C_GRN);
            text_c(F_13C, cx, y + 94, label_of(f), on && !sh_top ? A : wf_avail(f->id) ? C_TXT2 : C_LINE, SH_CW - 4);
        }
    }
    if (nr > SH_ROWS)                                       /* the tab's rows: where the 3 shown sit */
    {
        int y0 = SH_Y0, h = SH_ROWS * SH_RH - 12;
        int t0 = y0 + h * r0 / nr, t1 = y0 + h * (r0 + SH_ROWS) / nr - 1;
        wfg_rect(705, y0, 706, y0 + h - 1, C_SEP);
        wfg_rect(705, t0, 706, t1, C_TXT3);
    }
}

static void sh_left(const struct wf_film *f, int t)
{
    char b[24];
    wfc_draw(f, CAN_BIG, 66, 52, C_BG0);
    ui_text_clip(F_ML28, 18, 288, f->name, C_TXT, 292);
    int x = chip_s(18, 326, f->kind, acc(t), 292);
    snprintf(b, sizeof(b), "ISO %d", f->iso);
    x = chip_s(x, 326, b, C_TXT2, 292);
    chip_s(x, 326, f->format, C_TXT2, 292);
    if (sec_now())                                          /* Film stage: the negative it develops as */
    {
        if (f->line1) ui_text_clip(F_13, 18, 354, f->line1, C_TXT2, 292);
    }
    else
    {
        if (f->line1) ui_text_clip(F_13, 18, 354, f->line1, C_TXT2, 292);
        if (f->line2) ui_text_clip(F_13, 18, 371, f->line2, C_TXT2, 292);
    }
    if (!wf_avail(f->id)) ui_text_clip(F_13, 18, 396, roll_why(f), C_WARN, 292);
}

static void sh_footer(void)
{
    const char *ld = rollrows() ? "Load roll" : "Load";
    const char *const kt[] = { "SET", ld, "RING", "Film", "UP/DN", "Row", "TAP", "Group", "INFO", "Details",
                               "MENU", "Home", 0 };
    const char *const k5[] = { "SET", ld, "DIAL", "Film", "PICT", "Group", "INFO", "Details", "MENU", "Home", 0 };
    const char *const kp[] = { "SET", tsub && chip_on() ? mname(mode_next(1)) : "Film / Film stage",
                               chip_on() ? "L/R" : "DOWN", chip_on() ? "Section / mode" : "Films", "MENU", "Home", 0 };
    wfos_footer(sh_top ? kp : is5d2() ? k5 : kt);
}

static void shelf_render(int now)
{
    const struct wf_film *f = film_at(sel);
    int t = tab_of_sel();
    wfg_glow_centre(146, 165, 146, 284);                    /* the glow under the big can */
    wfg_background(0, WFUI_H);
    int xr = wfos_top_bar("Film shelf");
    int pe = sec_pill(48 + ui_textw(F_D20, "Film shelf") + 14, sh_top && !tsub);
    if (chip_on() && xr - 10 > pe + 150) mode_chip(xr - 10, sh_top && tsub);
    if (!f)
    {
        ui_text(F_15, 24, 60, "No films in this build.", C_TXT2);
        sh_footer();
        return;
    }
    sh_left(f, t);
    sh_tabs(t);
    sh_grid(t);
    sh_footer();
    draw_toast(now);
    if (leave_to >= 0) draw_leave();
    else if (sec_to >= 0) draw_sec_leave();
    else if (sw_film) draw_swap();
}

void wfui_render(void)
{
    int now = get_ms_clock();
    if (scr != WFUI_SCR_BROWSER)
    {
        wfdr_render(scr, now);
        return;
    }
    const struct wf_film *f = film_at(sel);
    int g = tab_of_sel();
    if (f && f->id != glow_id)
    {
        uint8_t rgb[3];
        wfc_glow(f, rgb);
        wfp_set_glow(rgb);
        glow_id = f->id;
    }
    if (shelf && !pick) { shelf_render(now); return; }
    wfg_glow_centre(125, 150, 125, 272);                    /* the browser's glow under its big can */
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
    draw_toast(now);
    if (leave_to >= 0) draw_leave();
    else if (sec_to >= 0) draw_sec_leave();
    else if (sw_film) draw_swap();
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
    const struct wf_film *f = anim_film ? anim_film : film_at(sel);
    if (!f) return;
    int g = f->group % WF_NGROUPS, A = acc(g), AD = accd(g);
    if (first) wfg_scrim(AX0, AY0, AX1, AY1);
    int done = t >= ANIM_WIND_MS;
    int p = t >= ANIM_WIND_MS ? 1024 : t < 0 ? 0 : t * 1024 / ANIM_WIND_MS;
    int e = 1024 - (1024 - p) * (1024 - p) / 1024;                  /* ease out: fast pull, slows into the gate */
    char b[24];

    wfg_rrect(AX0, AY0, AX1, AY1, 10, C_SEP, C_LINE, 1);
    ui_text(F_15, AX0 + 20, AY0 + 12, anim_exp ? (done ? "Roll loaded" : "Loading the roll") : done ? "Loaded" : "Loading",
            done ? C_GRN : C_TXT3);
    ui_text_clip(F_ML23, AX0 + 20, AY0 + 30, f->name, C_TXT, AX1 - 20);
    int cx = chip_s(AX0 + 22, AY0 + 62, f->kind, A, AX1 - 20);
    snprintf(b, sizeof(b), "ISO %d", f->iso);
    cx = chip_s(cx, AY0 + 62, b, C_TXT2, AX1 - 20);
    if (anim_exp)
    {
        snprintf(b, sizeof(b), "%d EXP", anim_exp);
        chip_s(cx, AY0 + 62, b, C_TXT2, AX1 - 20);
    }

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

/* the way to a new roll (wf_ui_open_scr WF_UI_NEW_ROLL: the ML menu's Film roll, the end-of-roll notice's INFO; owner
 * 11-17, the old Roll picker is gone): Darkroom mode (a roll set aside is the camera's roll again), the focus on the
 * Exposures row, the roll's film under the cursor. The 5D2 (no rolls) gets the plain browser and says why */
static volatile int nr_req;
static void new_roll_view(void)
{
    nr_req = 0;
    shelf = shelf_back = 0;
    if (!mode_ok(M_DARK)) { say("Rolls come to this camera later (EOS M first)"); return; }
    const struct wf_roll *r = wf_roll_now();
    int i = wf_roll_open(r) ? index_of_id(r->film) : -1;
    if (i >= 0) select_film(i);                             /* the roll's film: its section too */
    int cur = mode_now();
    if (!sec_now() && cur != M_DARK) mode_apply(cur, M_DARK);   /* Film: rolls are Darkroom's; Film stage: any mode */
    focus_to(FOC_EXP);
    wf_log("ui: a new roll: Darkroom mode, Exposures %d", exp_now());
}

/* develop again's film (owner 11-17, wf_ui_int.h): the Darkroom's Film row opens the browser on the film it has now; SET
 * takes the film under the cursor back (no load: the Darkroom loads it for its run), MENU / PLAY go back without one.
 * The Exposures row and the mode chip are off meanwhile (no roll is loaded here); the header says what SET is for */
void wfui_pick_film(int film, int num, int dev)
{
    pick = 1;
    pick_num = num;
    pick_dev = dev;
    int i = index_of_id(film);
    if (i >= 0) select_film(i);
    tsub = 0;
    focus_to(WF_ROW_LIGHT);
    scr_req = WFUI_SCR_BROWSER;
    wf_log("ui: develop %d of roll %03d: pick its film (now %d)", dev, num, film);
}
static void pick_back(const struct wf_film *f)
{
    pick = 0;
    wfdr_pick_done(f ? f->id : -1);
    if (f) wf_log("ui: develop %d of roll %03d with film %d %s", pick_dev, pick_num, f->id, f->name);
    else wf_log("ui: develop %d of roll %03d: film pick dropped", pick_dev, pick_num);
    scr_req = WFUI_SCR_DARK;
    focus_fix();
    dirty = 1;
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
    if (gui_menu_shown())                                   /* the ML menu keeps the screen: never draw over it */
    {
        wf_log("ui: the ML menu is still up after 1 s: not opened");
        close_req = 1;
        return;
    }
    beat(PH_LIST);
    if (build_list()) { close_req = 1; return; }
    for (int t = 0; t < NTAB; t++) { tsel[t] = tstart[t]; tfirst[t] = FIRST_UNSET; }
    stab[0] = 0;
    stab[1] = TAB_STAGE;
    int i = index_of_id(wf_loaded());
    if (i < 0) i = index_of_id(last_id);
    if (i < 0) i = 0;
    sel = 0;
    if (nlist) select_film(i);
    if (nlist && roll_in() && roll_sec() >= 0 && roll_sec() != sec_now())   /* a roll in the camera: its section */
    {
        int k = index_of_id(wf_roll_now()->film);
        if (k >= 0) select_film(k);
    }
    focus = WF_ROW_LIGHT;
    rtop = 0;
    info = 0;
    msg = 0;
    glow_id = -1;
    leave_to = -1;
    sec_to = -1;
    tsub = 0;
    sw_film = 0;
    pick = 0;
    memset(sh_row, 0, sizeof sh_row);
    shelf = 0;
    if (wf_os) shelf_on();                                  /* film OS: the Film shelf (M2) */
    {
        const struct wf_roll *r = wf_roll_now();
        for (int k = 0; k < NEXP; k++)                      /* the roll on the camera's count, else the last one */
            if (wf_roll_open(r) && exps[k] == r->exp) exp_i = k;
    }
    if (nr_req) new_roll_view();                            /* opened for a new roll */

    rd_until = get_ms_clock() + PACK_WAIT_MS;
    rd_tries = 0;
    beat(PH_FONTS);
    /* the LiveView finder (wf_dark.h): a band or the middle being painted right now finishes before the canvas and the
       font pack are ours (wf_ui_is_open() already holds them off from here on) */
    if (wf_hud_lock(1000)) wf_hud_lock(-1);
    int fs = wff_open();
    fonts_held = 1;
    wfp_init_fixed();
    beat(PH_CANS);
    int cs = wfc_open();
    rd_until = get_ms_clock();                              /* while open: one try per read, no waiting */
    pack_again = (fs & 1 && wff_again() ? 1 : 0) | (cs && wfc_again() ? 2 : 0);
    pack_n = 0;
    if (scr != WFUI_SCR_BROWSER) wfdr_enter(scr);           /* after the cans: the darkroom's print tones use their entries */
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
    char fw[96], rt[48];                        /* wff_open / wfc_open return codes in words (0 = loaded) */
    snprintf(fw, sizeof fw, "%s%s%s", fs & 1 ? wff_why() : "ok", fs & 2 ? ", no argnor23" : "",
             fs & 4 ? ", no argnor28" : "");
    rt[0] = 0;
    if (rd_tries) snprintf(rt, sizeof rt, ", %d reads tried again", rd_tries);
    wf_log("ui: open, %d films, fonts WFFONTS.DAT %s, cans WFCANS.DAT %s (%s%d colours)%s%s%s", nlist, fw, wfc_why(cs),
           cs ? "plain cans, " : "", wfc_pal_used(), rt, pack_again ? ", packs tried again while open" : "",
           !(fs & 1) && wff_disp() != 0xF ? ", film OS sizes missing from the font pack (older pack)" : "");
    wfp_log(wfc_pal_used());
    int now = get_ms_clock();
    t_probe = t_force = t_heal = t_pack = now;
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

/* a pack Canon's card kept from the open, read again (one try per read); drawn at once when it loads */
static void pack_retry(int now)
{
    t_pack = now;
    pack_n++;
    if (pack_again & 1)
    {
        beat(PH_FONTS);
        int fs = wff_reload();                              /* the UI's own use stays counted (the roll HUD shares it) */
        if (!(fs & 1)) wf_log("ui: fonts WFFONTS.DAT ok, read again %d", pack_n);
        else if (!wff_again()) wf_log("ui: fonts WFFONTS.DAT %s, read again %d", wff_why(), pack_n);
        if (!(fs & 1) || !wff_again()) pack_again &= ~1;
        dirty = 1;
    }
    if ((pack_again & 2) && scr != WFUI_SCR_DARK)          /* the darkroom draws no cans: its print tones hold those */
    {                                                       /* palette entries */
        beat(PH_CANS);
        int cs = wfc_open();
        if (!cs) wf_log("ui: cans WFCANS.DAT ok (%d colours), read again %d", wfc_pal_used(), pack_n);
        else if (!wfc_again()) wf_log("ui: cans WFCANS.DAT %s, read again %d", wfc_why(cs), pack_n);
        if (!cs || !wfc_again()) pack_again &= ~2;
        glow_id = -1;                                       /* the glow of the selected film from the pack now */
        dirty = 1;
    }
    if (pack_again && pack_n >= PACK_TRIES)
    {
        wf_log("ui: packs read again %d times: fonts %s, cans %s; plain until the next open", pack_n,
               pack_again & 1 ? wff_why() : "ok", pack_again & 2 ? wfc_why(wfc_status()) : "ok");
        pack_again = 0;
    }
    beat(PH_TICK);
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
    int don = display_is_on();                              /* on again (Canon blanked it; the Darkroom's keep-alive): */
    if (don && !disp_seen) wfp_keepalive(1);                /* our palette at once, not at the next probe (no redraw) */
    disp_seen = don;
    if (anim_on) return anim_tick(now);
    if (pack_again && now - t_pack >= PACK_RETRY_MS) pack_retry(now);
    uint32_t gen = wf_gen();
    if (gen != drawn_gen) dirty = 1;
    if (msg && now - msg_until >= 0) { msg = 0; dirty = 1; }
    if (scr_req >= 0)
    {
        int s = scr_req;
        scr_req = -1;
        if (s != scr)
        {
            if (scr != WFUI_SCR_BROWSER) wfdr_leave(scr);
            scr = s;
            if (s == WFUI_SCR_BROWSER) { wfp_init_fixed(); wfc_pal_again(); glow_id = -1; }   /* (the darkroom's */
                                                                                             /* tones held the cans') */
            else wfdr_enter(s);
            dirty = 1;
        }
    }
    if (nr_req && scr == WFUI_SCR_BROWSER) { new_roll_view(); dirty = 1; }   /* asked for while open */
    if (scr != WFUI_SCR_BROWSER && wfdr_tick(scr, now)) dirty = 1;
    if ((dirty || now - t_heal >= HEAL_MS) && !wfdr_dark(scr))   /* Screen Off: nothing drawn while dark */
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

/* the close hands Canon / ML a key once the screen is theirs (Home: MENU, PLAY; 5D2 TRASH: the ML menu) */
static void after_close(int what)
{
    if (ml_shutdown_requested || RECORDING) return;
    if (what == WFUI_AFTER_MLMENU)
    {
        wf_log("ui: closed for the ML menu");
        gui_open_menu();
        return;
    }
    if (what == WFUI_AFTER_PLAY)
    {
        wf_log("ui: closed for Canon's PLAY");
        module_send_keypress(MODULE_KEY_PLAY);
        return;
    }
    if (what != WFUI_AFTER_MENU) return;
    if (wf_cam() == WF_CAM_EOSM)            /* a two-finger tap came as MENU (patch 0003): ML opens its menu at the end of */
    {                                       /* canon_gui_enable_front_buffer (module_ui_after_release); a real MENU does not */
        for (int t = 0; t < 400 && !gui_menu_shown(); t += 20)
        {
            msleep(20);
            beat(PH_END);
        }
        if (gui_menu_shown())
        {
            wf_log("ui: MENU on Home was the two-finger tap: the ML menu");
            return;
        }
    }
    menu_pass_t = get_ms_clock();
    menu_pass = 1;
    module_send_keypress(MODULE_KEY_MENU);
    wf_log("ui: MENU on Home: Canon's menu");
}

void wfui_task_end(void)
{
    beat(PH_END);
    int after = close_after, under_menu = 0;
    close_after = 0;
    if (took)
    {
        /* The ML menu has the screen: the core opened it over us (a two-finger tap while this task did not beat, the
         * core's 'F'), or it came another way. Canon's front buffer must stay off under it (menu_open turned it off) or
         * Canon's LiveView GUI paints over the menu, and its pixels are not ours to clear. So: the palette back now,
         * keys on (ST_CLOSING), and the screen back only once the menu has gone (menu_close gave Canon its GUI). */
        int under = gui_menu_shown();
        under_menu = under;
        if (under)
        {
            st = ST_CLOSING;
            wfp_restore();
            wf_log("ui: the ML menu took the screen: given back after it closes");
            int t0 = get_ms_clock();
            while (gui_menu_shown() && !ui_quit && !ml_shutdown_requested)
            {
                beat(PH_END);
                msleep(50);
            }
            wf_log("ui: the ML menu went after %d ms%s", get_ms_clock() - t0, ui_quit ? " (unload)" : "");
        }
        wfg_clear_front();
        if (!under) wfp_restore();
        int masked = dlg_give();                            /* mode 0 while the front buffer is still ours */
        canon_gui_enable_front_buffer(1);
        if (masked) bmp_on();
        if (under) redraw();                                /* menu_close may have turned it on already: no redraw then */
        took = 0;
    }
    else if (dlg_give())
        bmp_on();
    const struct wf_film *f = film_at(sel);
    if (f) last_id = f->id;
    if (scr != WFUI_SCR_BROWSER) wfdr_leave(scr);
    scr = WFUI_SCR_BROWSER;
    wfc_close();
    if (fonts_held) wff_close();                            /* an open that ended before wff_open must not take the */
    fonts_held = 0;                                         /* roll HUD's use of the shared pack */
    if (list) free(list);
    list = 0;
    nlist = 0;
    anim_on = edit = 0;
    st = ST_CLOSING;                                        /* the screen is Canon's: keys pass on */
    wf_log("ui: closed");
    if (after && !under_menu && !lv_after) after_close(after);
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
int wfui_host_group(void) { return nlist ? wf_films[list[sel]].group % WF_NGROUPS : 0; }
int wfui_host_tab(void) { return tab_of_sel(); }
int wfui_host_tab_of(int id) { const struct wf_film *f = wf_film_by_id(id); return f ? tab_of(f) : -1; }
int wfui_host_sec(void) { return sec_now(); }
int wfui_host_tsub(void) { return tsub; }
int wfui_host_secq(void) { return sec_to; }             /* the leave-section question: the section asked for, -1 none */
int wfui_host_rows(void) { return rollrows(); }
int wfui_host_focus(void) { return focus; }
int wfui_host_rtop(void) { return rtop; }
int wfui_host_info(void) { return info; }
int wfui_host_shelf(void) { return shelf; }                /* the film OS's Film shelf is up */
int wfui_host_sh_top(void) { return sh_top; }
int wfui_host_sh_row(void) { return nlist ? sh_row[tab_of_sel()] : 0; }
int wfui_host_sh_cur(void) { return nlist ? (sel - tstart[tab_of_sel()]) / SH_COLS : 0; }   /* the cursor's row */
int wfui_host_sh_n(void) { return nlist ? tcount(tab_of_sel()) : 0; }
int wfui_host_info_y(void) { return ilast; }                /* the Info page's last grid row (drawn) */
int wfui_host_anim(void) { return anim_on; }
int wfui_host_scr(void) { return scr; }
int wfui_host_after(void) { return close_after; }
int wfui_host_menu_pass(void) { return menu_pass; }
const char *wfui_host_msg(void) { return msg && get_ms_clock() - msg_until < 0 ? msg : ""; }
int wfui_host_edit(void) { return edit; }
int wfui_host_phase(void) { return phase; }
int wfui_host_mode(void) { return mode_now(); }
int wfui_host_pick(void) { return pick; }         /* 0 JPEG, 1 RAW develop, 2 Darkroom */
int wfui_host_leave(void) { return leave_to; }
int wfui_host_swap(void) { return sw_film != 0; }
int wfui_host_exp(void) { return exp_now(); }
int wfui_host_chip(void) { return chip_on(); }
int wfui_host_nr(void) { return nr_req; }
void wfui_host_beat(int ph, int ago_ms) { phase = ph; hb = get_ms_clock() - ago_ms; }   /* a UI task held up for ago_ms */
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
static struct { int shown, open_at, tries, last_ask, came, done, undo_until; } mg;   /* wf_ui_menu_guard (below) */

void wf_ui_init(void)
{
    scr = WFUI_SCR_BROWSER;
    st = ST_CLOSED;
    close_req = 0;
    kq_r = kq_w = 0;
    list = 0;
    nlist = 0;
    took = 0;
    fonts_held = ui_quit = 0;
    anim_on = lv_after = edit = 0;
    close_after = menu_pass = 0;
    mg.shown = mg.tries = mg.came = mg.done = mg.undo_until = 0;
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
        close_after = 0;
        ui_quit = 0;
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
    ui_quit = 1;                                            /* a close waiting for the ML menu to go stops waiting */
#ifndef WF_HOST
    for (int i = 0; i < 100 && st != ST_CLOSED; i++) msleep(20);
#endif
}

int wf_ui_is_open(void) { return st != ST_CLOSED; }

/* wf_dark.h: WF_UI_SCR_DARK opens the UI on the Darkroom screen (or switches an open UI to it), WF_UI_SCR_HOME on the film
 * OS's Home, WF_UI_NEW_ROLL the browser in Darkroom mode for a new roll (new_roll_view), anything else the browser */
void wf_ui_open_scr(int s)
{
    if (s == WF_UI_NEW_ROLL) nr_req = 1;
    s = s == WF_UI_SCR_DARK ? WFUI_SCR_DARK : s == WF_UI_SCR_HOME ? WFUI_SCR_HOME : WFUI_SCR_BROWSER;
    if (st == ST_OPEN)
    {
        scr_req = s;                                        /* the UI task makes the switch on its next tick */
        return;
    }
    if (st == ST_CLOSED) scr = s;
    if (wf_ui_open() < 0) nr_req = 0;                       /* not opened: no stale request for the next open */
}

unsigned int wf_ui_key(unsigned int key)
{
    if (st == ST_CLOSED || st == ST_CLOSING) return 1;
    int idle = get_ms_clock() - hb, ph = phase;
    int stale = st == ST_OPENING && (ph == PH_FONTS || ph == PH_CANS) ? STALE_PACK_MS : STALE_MS;
    if (idle > stale)                       /* the UI task has not run: Canon / ML get the keys, it closes later */
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
        if (key == MODULE_KEY_MENU && scr == WFUI_SCR_BROWSER && !wf_os && leave_to < 0 && !sw_film && sec_to < 0
            && !pick)
            close_req = 1;                                  /* EOS M 2-finger tap: the ML menu opens in 3 s (the Roll /
                                                               Darkroom screens and the browser's questions decide on
                                                               MENU themselves: there it is "no"; with the film OS the
                                                               browser's MENU goes back to Home) */
        if (key == MODULE_KEY_MENU && scr == WFUI_SCR_HOME && scr_req < 0)
        {
            close_after = WFUI_AFTER_MENU;                  /* Home: close at once (a tap: the ML menu; else Canon's) */
            close_req = 1;
        }
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

void wf_ui_open_home(void) { wf_ui_open_scr(WF_UI_SCR_HOME); }

/* the film OS (docs/MODERNUI.md): MENU in shooting opens Home with Camera interface = WindyFilm. Not over the ML menu or
 * a Canon screen, not while recording; EOS M: stills LiveView (as the open key); not our own MENU for Canon's menu */
int wf_ui_home_key(unsigned int key)
{
    if (key != MODULE_KEY_MENU || !wf_os || st != ST_CLOSED) return 0;
    if (menu_pass)
    {
        menu_pass = 0;
        if (get_ms_clock() - menu_pass_t < 1500) return 0;
    }
    if (gui_menu_shown() || gui_state != GUISTATE_IDLE || RECORDING) return 0;
    if (wf_cam() != WF_CAM_5D2 && (!lv || is_movie_mode())) return 0;
    return 1;
}

int wf_ui_open_key(unsigned int key)
{
    if (st != ST_CLOSED || key != MODULE_KEY_TRASH) return 0;  /* EOS M: long DOWN arrives as TRASH (patch 0002) */
    if (gui_menu_shown() || gui_state != GUISTATE_IDLE) return 0;
    if (wf_cam() != WF_CAM_5D2 && (!lv || is_movie_mode())) return 0;   /* EOS M: Canon's LV Set dialog = stills LV */
    return 1;
}

/* ------------------------------------------------------------------ EOS M: the ML menu's Canon dialog
 * The ML menu lives on Canon's "LV Set" dialog (GUIMODE_ML_MENU = 93 in stills LV, platform/EOSM.202/consts.h):
 * menu_open's piggyback_canon_menu asks for it once (SetGUIRequestMode, 200 ms), and menu_ensure_canon_dialog asks
 * again every 2 s. The owner's RAWB7 KEYTRACE: right after Canon's own MENU (GUI mode 2) was left, the first ask did
 * not take; the menu ran on mode 0 (plain LiveView, which paints over it) until the 2 s ask, 2.0 / 2.2 s after the
 * tap. A tap 1.9 s after leaving the ML menu the same way (half-shutter) got 93 at once. So while the ML menu is up in
 * LV on mode 0, ask for 93 ourselves: 150 ms after it opened, then every 300 ms, MG_TRIES times (past ML's own 2 s
 * ask). Our asks may land after the menu has closed (ML closes it when it sees mode 0 after its redraw flood): Canon's
 * dialog would then stay up with no menu on it, so a 93 seen within 1 s of the close is asked back to 0.
 * Called every ~10 ms from wf_task's waits (wf_main.c; RAWB8 had a task of its own, dropped: DryOS's task table), never
 * while the browser is open (the core's module UI
 * holds the dialog then). 5D2: nothing (its menu dialog is 38, no report of this there). State: mg (wf_api.h part). */
#define MG_MODE      93                     /* GUIMODE_ML_MENU in stills LV (RECORDING_H264 -> 99: never asked) */
#define MG_FIRST_MS  150
#define MG_EVERY_MS  300
#define MG_TRIES     8
#define MG_UNDO_MS   1000

int wf_ui_menu_guard(void)
{
    if (wf_cam() != WF_CAM_EOSM) return 0;
    int now = get_ms_clock(), shown = gui_menu_shown(), gm = get_gui_mode();
    int ours = lv && !RECORDING && st == ST_CLOSED;         /* plain stills LV, no browser */
    if (!shown)
    {
        if (mg.shown && mg.tries && !mg.came) mg.undo_until = now + MG_UNDO_MS;  /* closed, our asks may still land */
        mg.shown = mg.tries = mg.came = mg.done = 0;
        if (!mg.undo_until) return 0;
        if (now - mg.undo_until >= 0) { mg.undo_until = 0; return 0; }
        if (gm != MG_MODE || !ours) return 0;
        mg.undo_until = 0;
        SetGUIRequestMode(0);
        wf_log("menu: closed before canon dialog %d came: asked 0", gm);
        return 1;
    }
    if (!mg.shown)
    {
        mg.shown = 1;
        mg.open_at = now;
        mg.tries = mg.came = mg.done = mg.undo_until = 0;
    }
    if (gm)
    {
        if (mg.tries && !mg.came)
        {
            wf_log("menu: canon dialog %d after try %d, %d ms after the menu opened", gm, mg.tries, now - mg.open_at);
            mg.came = mg.done = 1;                          /* this menu's round is over: ML's 2 s ask keeps it */
        }
        return 0;
    }
    if (!ours || mg.done) return 0;
    if (mg.tries >= MG_TRIES)
    {
        wf_log("menu: canon dialog still 0 after %d tries (%d ms): left to ML", mg.tries, now - mg.open_at);
        mg.done = 1;
        return 0;
    }
    if (now - (mg.tries ? mg.last_ask + MG_EVERY_MS : mg.open_at + MG_FIRST_MS) < 0) return 0;
    mg.tries++;
    mg.last_ask = now;
    SetGUIRequestMode(MG_MODE);
    wf_log("menu: canon dialog 0 -> asked %d (try %d)", MG_MODE, mg.tries);
    return 1;
}
