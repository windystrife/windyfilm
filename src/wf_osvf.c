/* wf_osvf.c - the film OS's LiveView finder, the core side (docs/MODERNUI.md M3; wf_dark.h, wf_api.h wf_vf).
 *
 * When it is up: Camera interface = WindyFilm, the finder is not "Canon + ML", photo LiveView with ML's overlays on
 * (get_global_draw: Global Draw on and Canon's clean view), the display on, no ML menu, no WindyFilm UI, no PLAY, no
 * darkroom run, the HUD's font pack held (the roll HUD's, wf_dark.c).
 *
 * ML's info bars: two lvinfo items of ours, one per bar, as wide as the screen and of the highest priority. ML's layout
 * then hides every other item of that bar (lvinfo_squeeze_space hides the lowest priorities until the widest fits) and,
 * when it redraws the bar, clears our box to COLOR_EMPTY and calls our update with can_draw = 1: we paint the band
 * (src/ui/wf_os_hud.c). So the bands change exactly when ML's bars would (ML redraws them on its own property changes)
 * and never flicker against them; a value of ours that changes (the roll's count, the meter) asks for a redraw with
 * lens_display_set_dirty. When the finder is down the items measure 0 px wide: ML's own items come back at its next
 * layout. ML never removes an item (module unload only calls deinit): after deinit they stay 0 px wide.
 *
 * The middle between the bars (frame corners, bright lines) is painted by wf_task (wf_osvf_poll, from the roll HUD)
 * every 500 ms and at once on a change, and given back (0) when the finder goes down in LiveView.
 *
 * One lock (wf_hud_lock) for every painter that points the browser's canvas at ML's bitmap: ML's task (the bands),
 * wf_task (the middle, the roll HUD, its notice and the quick keys' box, the font pack's open / close) and the UI's
 * open (a barrier). Compiled with ML's headers (struct lens_info, struct lvinfo_item). */
#include <dryos.h>
#include <lens.h>
#include <bmp.h>
#include <lvinfo.h>
#include <property.h>
#include "wf_core.h"
#include "wf_dark.h"

extern int lv, shooting_mode, battery_level_bars, avail_shot, drive_mode, gui_state;
extern int get_gui_mode(void);
extern int get_global_draw(void);
extern int gui_menu_shown(void);
extern int is_play_mode(void);
extern int display_is_on(void);                  /* (is_movie_mode: ML's propvalues.h) */
extern int get_ae_value(void);
extern void lens_display_set_dirty(void);
extern int get_ml_topbar_pos(void);
extern int get_ml_bottombar_pos(void);

#define VF_PITCH    960                          /* ML's bitmap pitch (wf_dark.c WF_BMPPITCH) */
#define VF_BAR_H    32                           /* ML's bar height (lvinfo.c) */
#define VF_MID_MS   500
#define SM_P        0                            /* Canon's mode dial (ML propvalues.h SHOOTMODE_*) */
#define SM_AV       2
#define SM_M        3
#define PLATE_MS    300                          /* Canon's shooting screen this long: the Top plate */
#define PLATE_HOLD  1500                         /* after the UI closes: Canon's screen settles (no re-arm) */

static struct semaphore *hud_sem;
static volatile int vf_up, vf_dead;
static int vf_shown = -1, vf_mid_t, vf_sig, vf_y0, vf_y1, vf_logged = -1;

int wf_hud_lock(int ms)
{
    if (!hud_sem)
        return 1;
    if (ms < 0) {
        give_semaphore(hud_sem);
        return 1;
    }
    return take_semaphore(hud_sem, ms > 0 ? ms : 1) == 0;   /* (0 would wait for ever) */
}

/* ------------------------------------------------------------------ the values */
static const struct wf_film *vf_film(void)
{
    const struct wf_roll *r = wf_roll_now();
    const struct wf_film *f = wf_roll_open(r) && !wf_roll_shelved() ? wf_film_by_id(r->film) : 0;
    return f ? f : wf_film_by_id(wf_loaded());
}

static void vf_values(struct wfos_lv *v)
{
    int r = lens_info.raw_shutter, s = (int)lens_info.shutter, a = (int)lens_info.aperture, iso = (int)lens_info.iso;
    v->vf = wf_vf;
    if (r >= 70 && s > 0)
        snprintf(v->sh, sizeof v->sh, "1/%d", s);
    else if (r >= 15 && s > 0 && s % 10)
        snprintf(v->sh, sizeof v->sh, "%d.%d\"", s / 10, s % 10);
    else if (r >= 15 && s > 0)
        snprintf(v->sh, sizeof v->sh, "%d\"", s / 10);
    else
        snprintf(v->sh, sizeof v->sh, "%s", r ? "BULB" : "--");
    if (a <= 0)
        snprintf(v->ap, sizeof v->ap, "f/--");
    else if (a < 100)
        snprintf(v->ap, sizeof v->ap, "f/%d.%d", a / 10, a % 10);
    else
        snprintf(v->ap, sizeof v->ap, "f/%d", a / 10);
    if (iso > 0)
        snprintf(v->iso, sizeof v->iso, "%d", iso);
    else
        snprintf(v->iso, sizeof v->iso, "AUTO");
    const struct wf_film *f = vf_film();
    v->iso_film = f && iso > 0 && f->iso == iso;
    /* M: Canon's exposure level (AE_VALUE, 1/8 EV, + = more light than the meter asks); P / Tv / Av: the
       compensation; the scene modes: none */
    v->meter = shooting_mode == SM_M ? get_ae_value()
             : shooting_mode >= SM_P && shooting_mode <= SM_AV ? lens_info.ae : WFOS_NOMETER;
    if (v->meter != WFOS_NOMETER && (v->meter < -127 || v->meter > 127))
        v->meter = WFOS_NOMETER;
    v->batt = battery_level_bars >= 0 && battery_level_bars <= 9 ? battery_level_bars : -1;
    v->shots = avail_shot >= 0 && avail_shot < 100000 ? avail_shot : -1;
    v->wind = wf_roll_wind();
    static const struct { int m; const char *s; } md[] = {
        { 0, "P" }, { 1, "Tv" }, { 2, "Av" }, { 3, "M" }, { 4, "B" }, { 5, "A-DEP" }, { 7, "C1" }, { 0x10, "C2" },
        { 0x11, "C3" }, { 0x13, "CA" }, { 9, "Auto" }, { 0xF, "Auto" },
    };                                           /* ML propvalues.h SHOOTMODE_* */
    snprintf(v->mode, sizeof v->mode, "--");
    for (unsigned k = 0; k < sizeof md / sizeof md[0]; k++)
        if (md[k].m == shooting_mode)
            snprintf(v->mode, sizeof v->mode, "%s", md[k].s);
    snprintf(v->drive, sizeof v->drive, "%s", drive_mode == 0 ? "Single" : drive_mode == 1 ? "Cont."
                                            : drive_mode == 0x10 ? "Timer 10s" : drive_mode == 0x11 ? "Timer 2s"
                                            : drive_mode == 7 ? "Timer cont." : "--");   /* ML DRIVE_* */
}

void wf_osvf_values(struct wfos_lv *v)
{
    vf_values(v);
}

static int vf_hash(const struct wfos_lv *v)
{
    unsigned h = 2166136261u;
    const char *s[3] = { v->sh, v->ap, v->iso };
    for (int k = 0; k < 3; k++)
        for (const char *p = s[k]; *p; p++)
            h = (h ^ (unsigned char)*p) * 16777619u;
    const struct wf_roll *r = wf_roll_now();
    int n[10] = { v->vf, v->iso_film, v->meter, v->batt, v->shots, v->wind, r->num, r->nshot, r->state, wf_loaded() };
    for (int k = 0; k < 10; k++)
        h = (h ^ (unsigned)n[k]) * 16777619u;
    return (int)h;
}

/* ------------------------------------------------------------------ ML's bars: our two items */
static void vf_band(struct lvinfo_item *item, int can_draw, int bottom)
{
    if (!vf_up || vf_dead || gui_menu_shown() || wf_ui_is_open()) {
        item->width = 0;                         /* not shown: ML's own items keep the bar */
        item->custom_drawing = 0;
        return;
    }
    item->width = 720;
    item->height = VF_BAR_H;
    item->custom_drawing = 1;
    item->color_fg = COLOR_WHITE;
    item->color_bg = COLOR_EMPTY;                /* ML clears our box to transparent before it calls us to paint */
    if (!can_draw)
        return;
    if (!wf_hud_lock(30))
        return;
    if (vf_up && !wf_ui_is_open()) {
        struct wfos_lv v;
        vf_values(&v);
        int y = item->y - 2;                     /* lvinfo_valign_items: bar_y + (32 - height) / 2 + 2 */
        if (bottom)
            wfos_hud_bottom(bmp_vram(), VF_PITCH, y, &v);
        else
            wfos_hud_top(bmp_vram(), VF_PITCH, y, &v);
    }
    wf_hud_lock(-1);
}
static LVINFO_UPDATE_FUNC(vf_top_update) { vf_band(item, can_draw, 0); }
static LVINFO_UPDATE_FUNC(vf_bot_update) { vf_band(item, can_draw, 1); }

static struct lvinfo_item vf_items[2] = {
    { .name = "WindyFilm finder", .which_bar = LV_TOP_BAR_ONLY, .priority = 127, .update = vf_top_update },
    { .name = "WindyFilm finder.", .which_bar = LV_BOTTOM_BAR_ONLY, .priority = 127, .update = vf_bot_update },
};

void wf_osvf_init(void)
{
    hud_sem = create_named_semaphore("wf_hud", 1);
    if (!DRY_SEM(hud_sem))
        hud_sem = 0;
    lvinfo_add_items(vf_items, 2);
}

void wf_osvf_deinit(void)
{
    vf_dead = 1;
    vf_up = 0;
}

/* ------------------------------------------------------------------ wf_task */
int wf_osvf_wants(void)
{
    return wf_os && wf_vf != WF_VF_OFF && !vf_dead;
}

/* ------------------------------------------------------------------ the 5D2's Top plate (M4) */
/* the Top plate's dials (UI task): the plate holds Canon's MENU mode (2, as the browser without LiveView), so the dials
   come to us and would only move Canon's hidden menu. dial 0 = the main dial: Tv / M the shutter, Av the aperture; dial
   1 = the rear wheel: M the aperture, P / Tv / Av the compensation. One 1/3 stop (a half-stop body: 1/2) each way.
   1 back = Canon took a new value; 0 = nothing to change here (that mode, an end, not taken: logged) */
int wf_plate_dial(int dial, int dir)
{
    int m = shooting_mode, d = dir > 0 ? 1 : -1;
    const char *what = 0;
    int c0 = 0, c = 0, ok = 0;
    if (dial == 0 && (m == 1 || m == SM_M)) {
        what = "shutter";
        c0 = lens_info.raw_shutter;
        c = c0 > 0 ? wf_roll_av_step(c0, d, 16, 160, (c0 & 7) == 4) : c0;   /* 30" .. 1/8000 */
        ok = c != c0 && lens_set_rawshutter(c);
    } else if ((dial == 0 && m == SM_AV) || (dial == 1 && m == SM_M)) {
        what = "aperture";
        c0 = lens_info.raw_aperture;
        int lo = lens_info.raw_aperture_min ? lens_info.raw_aperture_min : 8;
        int hi = lens_info.raw_aperture_max ? lens_info.raw_aperture_max : 96;
        c = lens_info.name[0] && c0 > 0 ? wf_roll_av_step(c0, d, lo, hi, (c0 & 7) == 4) : c0;
        ok = c != c0 && lens_set_rawaperture(c);
    } else if (dial == 1 && m >= SM_P && m <= SM_AV) {
        what = "compensation";
        c0 = lens_info.ae;
        c = wf_roll_av_step(c0 + 64, d, 64 - 16, 64 + 16, (c0 & 7) == 4) - 64;   /* -2 .. +2 EV (1/8 EV codes) */
        ok = c != c0 && lens_set_ae(c);
    }
    if (what)
        wf_log("plate: dial %d %s %d -> %d%s", dial, what, c0, c, ok ? "" : c == c0 ? " (an end)" : " (not taken)");
    return ok;
}

static int pl_armed = 1, pl_t0, pl_ui_t = -100000;

int wf_plate_poll(int now)
{
    if (wf_cam() != WF_CAM_5D2 || !wf_os || !wf_plate || vf_dead) {
        pl_armed = 0;                            /* turned on again: the next screen, not this one */
        return 0;
    }
    if (wf_ui_is_open()) {                       /* ours: Canon's screen comes back after it, give it time */
        pl_ui_t = now;
        pl_t0 = 0;
        return 0;
    }
    if (now - pl_ui_t < PLATE_HOLD)
        return 0;
    /* Canon's shooting screen through the viewfinder: no LiveView, the display on, Canon's GUI idle (gui_state 0 =
       GUISTATE_IDLE, gui mode 0: no Canon menu / dialog / review), no ML menu, no PLAY, no run, no shot being saved */
    int canon = !lv && display_is_on() && gui_state == 0 && get_gui_mode() == 0 && !gui_menu_shown()
                && !is_play_mode() && !wf_dark_busy() && !lens_info.job_state;
    if (!canon) {
        pl_armed = 1;
        pl_t0 = 0;
        return 0;
    }
    if (!pl_armed)
        return 0;
    if (!pl_t0) {
        pl_t0 = now ? now : 1;
        return 0;
    }
    if (now - pl_t0 < PLATE_MS)
        return 0;
    pl_armed = 0;
    pl_t0 = 0;
    wf_log("plate: Canon's shooting screen: the Top plate");
    wf_ui_open_scr(WF_UI_SCR_PLATE);
    return 1;
}

int wf_osvf_poll(int now)
{
    int want = wf_osvf_wants() && lv && !is_movie_mode() && get_global_draw() && !gui_menu_shown() && !wf_ui_is_open()
               && !is_play_mode() && display_is_on() && !wf_dark_busy() && wfdr_hud_fonts_on();
    if (want != vf_up) {
        /* gone in plain LiveView (Canon's own view, the finder set off): our middle back to 0; under a menu, the UI or
           PLAY the screen is someone else's (they clear it) */
        if (!want && vf_shown >= 0 && lv && !gui_menu_shown() && !wf_ui_is_open() && !is_play_mode()
            && wf_hud_lock(50)) {
            wfos_hud_mid(bmp_vram(), VF_PITCH, vf_y0, vf_y1, -1);
            wf_hud_lock(-1);
        }
        vf_up = want;
        vf_shown = -1;
        lens_display_set_dirty();                /* ML's bars: ours now, or its own items back */
        if (want != vf_logged) {
            vf_logged = want;
            wf_log("osvf: LiveView finder %s (mode %d, bars at %d / %d)", want ? "up" : "down", wf_vf,
                   get_ml_topbar_pos(), get_ml_bottombar_pos());
        }
    }
    if (!want)
        return 0;
    struct wfos_lv v;
    vf_values(&v);
    int sig = vf_hash(&v);
    if (sig != vf_sig) {                         /* a value changed: ML redraws its bars (ours) soon */
        vf_sig = sig;
        lens_display_set_dirty();
    }
    int y0 = get_ml_topbar_pos() + VF_BAR_H, y1 = get_ml_bottombar_pos();
    if (vf_shown != wf_vf || y0 != vf_y0 || y1 != vf_y1 || now - vf_mid_t >= VF_MID_MS) {
        vf_mid_t = now;
        if (wf_hud_lock(50)) {
            if (!wf_ui_is_open() && !gui_menu_shown()) {
                if (vf_shown >= 0 && (vf_shown != wf_vf || y0 != vf_y0 || y1 != vf_y1))
                    wfos_hud_mid(bmp_vram(), VF_PITCH, vf_y0, vf_y1, -1);   /* the last mode's marks go */
                wfos_hud_mid(bmp_vram(), VF_PITCH, y0, y1, wf_vf);
                vf_shown = wf_vf;
                vf_y0 = y0;
                vf_y1 = y1;
            }
            wf_hud_lock(-1);
        }
    }
    return 1;
}
