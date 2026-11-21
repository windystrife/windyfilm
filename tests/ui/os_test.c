/* os_test.c - host harness for the film OS screens (src/ui/wf_os_ui.c, docs/MODERNUI.md, modernUI M1): Home and Settings
 * against the fake core. The MENU key's way in (wf_ui_home_key) and out (Canon's menu, or the ML menu after the EOS M
 * two-finger tap; 5D2 TRASH on Home), the keys of both screens, what close gives back, screenshots of both cameras and
 * of Home on a font pack from before M1 (no display sizes: the fallbacks).
 *
 *   os_test OUTDIR CARD_DIR CARD_OLDFONTS_DIR RBF_DIR
 * CARD_DIR: ML/DATA/WFFONTS.DAT (with the display sizes) + WFCANS.DAT; CARD_OLDFONTS_DIR: the same with the old font pack. */
#include "host_ml.h"
#include "wf_ui_int.h"
#include "fake_core.h"
#include "fake_dark.h"
#include "wf_api.h"

int wfui_host_scr(void);
int wfui_host_sel_id(void);
int wfui_host_tab(void);
int wfui_host_shelf(void);
int wfui_host_sh_top(void);
int wfui_host_sh_row(void);
int wfui_host_sh_cur(void);
int wfui_host_sh_n(void);
int wfui_host_after(void);
int wfos_host_hfoc(void);
int wfos_host_sfoc(void);
int wfos_host_ssec(void);
const char *wfos_host_msg(void);

static const char *outdir;
static int fails, closed;

#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
                           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static void tick(int n)
{
    for (int i = 0; i < n && !closed; i++)
    {
        if (wfui_task_tick()) closed = 1;
        msleep(20);
    }
}

static unsigned key(int k)
{
    unsigned r = wf_ui_key(k);
    tick(1);
    return r;
}

static int id_of(const char *c)
{
    for (int i = 0; i < wf_nfilms; i++)
        if (!strcmp(wf_films[i].code, c)) return wf_films[i].id;
    fprintf(stderr, "no film %s\n", c);
    exit(1);
}

static void shot(const char *name)
{
    char p[700];
    snprintf(p, sizeof(p), "%s/%s.ppm", outdir, name);
    tick(1);
    int stray = host_dump(p);
    CHECK(stray == 0, "%s: %d pixels outside the UI's palette entries", name, stray);
    printf("shot %s\n", p);
}

static void open_home(void)
{
    closed = 0;
    CHECK(!wf_ui_is_open(), "open twice");
    wf_ui_open_home();
    CHECK(wf_ui_is_open(), "Home did not open");
    wfui_task_begin();
    CHECK(host_front_disabled(), "front buffer not taken");
    CHECK(wfui_host_scr() == WFUI_SCR_HOME, "opened on screen %d, not Home", wfui_host_scr());
    tick(1);
}

static void given_back_checks(void)
{
    CHECK(!wf_ui_is_open(), "still open after close");
    CHECK(!host_front_disabled(), "front buffer not given back");
    CHECK(host_palette_is_canon(), "palette entries not given back");
    CHECK(host_front_is_clear(), "front buffer not transparent after close");
    CHECK(host_live_allocs() == 0, "%ld allocations left after close", host_live_allocs());
    CHECK(host_lock_depth() == 0, "BMP lock depth %d", host_lock_depth());
    CHECK(!host_bmp_muted(), "Canon's bitmap left muted after close");
}

static void end_checks(void)
{
    wfui_task_end();
    given_back_checks();
}

/* a roll in the camera: film, frames shot of exp, all still to develop */
static void roll_in(const char *film, int num, int shot_n, int exp)
{
    struct wf_roll *r = fake_roll();
    wf_roll_reset(r);
    r->num = num;
    r->film = id_of(film);
    r->exp = exp;
    r->nshot = shot_n;
    r->state = shot_n >= exp ? WF_RS_FULL : WF_RS_LOADED;
    for (int i = 0; i < shot_n; i++) { r->folder[i] = 100; r->file[i] = 6800 + i; r->fst[i] = WF_RF_SHOT; }
    fake_set_loaded(r->film);
}

static void to_tile(int t)
{
    for (int i = 0; i < 12 && wfos_host_hfoc() != t; i++) key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfos_host_hfoc() == t, "DOWN did not reach Home focus %d (%d)", t, wfos_host_hfoc());
}

/* ---------------------------------------------------------------- the LiveView finder (src/ui/wf_os_hud.c, M3) */
#define LV_P    960                                 /* ML's bitmap pitch */
#define LV_TOP  0                                   /* ML's bars on a 3:2 LCD (get_ml_topbar_pos / bottombar_pos) */
#define LV_BOT  448
static uint8_t lvb[480 * LV_P];

static struct wfos_lv lv_vals(int vf, const char *sh, const char *ap, const char *iso, int iso_film, int meter, int batt,
                              int shots, int wind)
{
    struct wfos_lv v;
    memset(&v, 0, sizeof v);
    v.vf = vf;
    snprintf(v.sh, sizeof v.sh, "%s", sh);
    snprintf(v.ap, sizeof v.ap, "%s", ap);
    snprintf(v.iso, sizeof v.iso, "%s", iso);
    v.iso_film = iso_film;
    v.meter = meter;
    v.batt = batt;
    v.shots = shots;
    v.wind = wind;
    return v;
}

/* pixels set outside rows y0 .. y1 or columns 0 .. 719 */
static int lv_outside(int y0, int y1)
{
    int n = 0;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < LV_P; x++)
            if (lvb[y * LV_P + x] && (y < y0 || y > y1 || x > 719)) n++;
    return n;
}

static int lv_count(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < 720; x++) n += lvb[y * LV_P + x] != 0;
    return n;
}

/* the three pieces as the camera paints them (ML's task the bands, wf_task the middle), each checked to stay in its own
   rows, then the screen over a stand-in LiveView frame (WF_LVBG: a 720 x 480 PPM) */
static void lv_shot(const char *name, const struct wfos_lv *v)
{
    CHECK(wfdr_hud_fonts(1) == 0 && wfdr_hud_fonts_on(), "%s: the font pack did not open", name);
    memset(lvb, 0, sizeof lvb);
    wfos_hud_top(lvb, LV_P, LV_TOP, v);
    CHECK(lv_outside(LV_TOP, LV_TOP + 31) == 0, "%s: the top band paints outside its 32 rows", name);
    CHECK(lv_count(LV_TOP, LV_TOP + 31) > 0, "%s: the top band is empty", name);
    wfos_hud_bottom(lvb, LV_P, LV_BOT, v);
    CHECK(lv_outside(LV_TOP, LV_TOP + 31) == lv_count(LV_BOT, LV_BOT + 31), "%s: the bottom band paints outside its rows",
          name);
    CHECK((lv_count(LV_BOT, LV_BOT + 31) == 0) == (v->vf == WF_VF_MIN), "%s: bottom band %d px in mode %d", name,
          lv_count(LV_BOT, LV_BOT + 31), v->vf);
    int bands = lv_count(0, 479);
    wfos_hud_mid(lvb, LV_P, LV_TOP + 32, LV_BOT, v->vf);
    int mid = lv_count(LV_TOP + 32, LV_BOT - 1);
    CHECK(mid > 0 && lv_count(0, 479) == bands + mid, "%s: the middle paints %d px, or outside its rows", name, mid);
    char p[700];
    snprintf(p, sizeof(p), "%s/%s.ppm", outdir, name);
    int stray = host_dump_lv(p, lvb, LV_P, getenv("WF_LVBG"));
    CHECK(stray == 0, "%s: %d pixels in none of ML's colours", name, stray);
    printf("shot %s\n", p);
    /* the finder going down: the middle back to 0 */
    wfos_hud_mid(lvb, LV_P, LV_TOP + 32, LV_BOT, -1);
    CHECK(lv_count(LV_TOP + 32, LV_BOT - 1) == 0, "%s: the middle not cleared (%d px left)", name,
          lv_count(LV_TOP + 32, LV_BOT - 1));
    wfdr_hud_fonts(0);
}

static void lv_shots(const char *cam)
{
    char n[64];
    struct wfos_lv v = lv_vals(WF_VF_FULL, "1/125", "f/2.8", "160", 1, -3, 2, 812, 0);
    snprintf(n, sizeof n, "m3_%s_lv_full", cam);
    lv_shot(n, &v);
    v.wind = 1;
    v.meter = 20;                                           /* 2.5 stops over: the marker red at the end */
    snprintf(v.sh, sizeof v.sh, "1/4000");
    snprintf(v.iso, sizeof v.iso, "3200");
    v.iso_film = 0;
    snprintf(n, sizeof n, "m3_%s_lv_full_wind", cam);
    lv_shot(n, &v);
    v = lv_vals(WF_VF_MIN, "1/60", "f/5.6", "400", 0, 0, 1, 812, 0);
    snprintf(n, sizeof n, "m3_%s_lv_min", cam);
    lv_shot(n, &v);
    v = lv_vals(WF_VF_LINES, "1/250", "f/8", "160", 1, 8, 2, 812, 0);
    snprintf(n, sizeof n, "m3_%s_lv_lines", cam);
    lv_shot(n, &v);
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: os_test OUTDIR CARD OLDFONTS RBFDIR\n"); return 2; }
    outdir = argv[1];
    const char *card = argv[2], *oldf = argv[3];
    host_init(argv[4], 0x03FF0000u, 0x03000000u);
    wf_ui_init();

    /* ---------------------------------------------------------------- EOS M: the way in */
    fake_reset();
    fake_dark_reset();
    fake_set_card(card);
    host_sent_clear();
    roll_in("PRT160", 24, 24, 36);
    wf_blind = 1;
    lv = 1;
    gui_state = GUISTATE_IDLE;
    CHECK(!wf_ui_home_key(MODULE_KEY_MENU), "MENU took Home with Camera interface = Canon + ML");
    wf_os_set(1);
    CHECK(wf_ui_home_key(MODULE_KEY_MENU), "MENU did not take Home with Camera interface = WindyFilm");
    CHECK(!wf_ui_home_key(MODULE_KEY_TRASH), "TRASH took Home");
    host_menu(1, -1, 0);
    CHECK(!wf_ui_home_key(MODULE_KEY_MENU), "MENU took Home over the ML menu");
    host_menu(0, 0, 0);
    host_recording = 1;
    CHECK(!wf_ui_home_key(MODULE_KEY_MENU), "MENU took Home while recording");
    host_recording = 0;
    lv = 0;
    CHECK(!wf_ui_home_key(MODULE_KEY_MENU), "EOS M: MENU took Home outside LiveView");
    lv = 1;

    /* ---------------------------------------------------------------- Home */
    open_home();
    CHECK(wfos_host_hfoc() == 0, "Home does not start on Film shelf");
    shot("m1_eosm_home");
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfos_host_hfoc() == 1, "DOWN: focus %d, not Darkroom", wfos_host_hfoc());
    shot("m1_eosm_home_darkroom");
    to_tile(4);                                             /* the look strip's first item (Light) */
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfos_host_hfoc() == 5, "RIGHT in the look strip: %d, not Print", wfos_host_hfoc());
    key(MODULE_KEY_PRESS_RIGHT);
    key(MODULE_KEY_PRESS_RIGHT);                            /* Grain */
    int g0 = fake_row_value(WF_ROW_GRAIN);
    CHECK(key(MODULE_KEY_WHEEL_RIGHT) == 0, "ring not consumed on Home");
    CHECK(fake_row_value(WF_ROW_GRAIN) != g0, "the ring did not step Grain (%d)", fake_row_value(WF_ROW_GRAIN));
    shot("m1_eosm_home_look");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(fake_row_value(WF_ROW_GRAIN) == g0, "the ring back did not give Grain back");
    key(MODULE_KEY_PRESS_UP);                               /* look -> Settings tile */
    CHECK(wfos_host_hfoc() == 3, "UP from the look strip: %d, not Settings", wfos_host_hfoc());

    /* Darkroom tile: frames to develop -> the Darkroom; MENU there (no run) -> Home */
    to_tile(1);
    key(MODULE_KEY_PRESS_SET);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_DARK, "Darkroom tile: screen %d", wfui_host_scr());
    key(MODULE_KEY_MENU);
    tick(1);
    CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "MENU in the Darkroom: closed %d, screen %d", closed,
          wfui_host_scr());

    /* Film shelf tile -> the Film shelf (M2): the ring and DOWN through the grid (the cursor's row always in view),
       TAP the next tab, UP from the first row the pill, INFO the browser's own screen (Details) and MENU there back to
       the shelf, MENU on the shelf -> Home (not closed) */
    to_tile(0);
    key(MODULE_KEY_PRESS_SET);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && wfui_host_shelf(), "Film shelf tile: screen %d, shelf %d",
          wfui_host_scr(), wfui_host_shelf());
    CHECK(wfui_host_sel_id() == id_of("PRT160"), "the shelf opens on the film in the camera (%d)", wfui_host_sel_id());
    shot("m2_eosm_shelf");
    int s0 = wfui_host_sel_id();
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfui_host_sel_id() != s0, "the ring on the shelf: the film did not move");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_sel_id() == s0, "the ring back: not the film it was on");
    for (int i = 0; i < 6; i++)
    {
        int c0 = wfui_host_sh_cur();
        key(MODULE_KEY_PRESS_DOWN);
        int c = wfui_host_sh_cur(), r = wfui_host_sh_row();
        CHECK(c == c0 + 1 || c == (wfui_host_sh_n() - 1) / 4, "DOWN %d: cursor row %d after %d", i, c, c0);
        CHECK(c >= r && c <= r + 2, "DOWN %d: the cursor's row %d not in view (rows %d..%d)", i, c, r, r + 2);
        if (i == 2) shot("m2_eosm_shelf_down");
    }
    CHECK(wfui_host_sh_n() <= 12 || wfui_host_sh_row() > 0, "a tab of %d films never scrolled", wfui_host_sh_n());
    key(MODULE_KEY_TOUCH_1_FINGER);
    CHECK(wfui_host_tab() == 1 && wfui_host_shelf(), "TAP on the shelf: tab %d, not Cine", wfui_host_tab());
    shot("m2_eosm_shelf_cine");
    key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_sh_top(), "UP from the first row: not on the pill");
    shot("m2_eosm_shelf_top");
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(!wfui_host_sh_top(), "DOWN from the pill: still there");
    key(MODULE_KEY_INFO);
    CHECK(!wfui_host_shelf() && wfui_host_scr() == WFUI_SCR_BROWSER, "INFO on the shelf: shelf %d, screen %d",
          wfui_host_shelf(), wfui_host_scr());
    shot("m2_eosm_shelf_details");
    key(MODULE_KEY_MENU);
    tick(1);
    CHECK(!closed && wfui_host_shelf() && wfui_host_scr() == WFUI_SCR_BROWSER, "MENU in Details: shelf %d, closed %d",
          wfui_host_shelf(), closed);
    CHECK(key(MODULE_KEY_MENU) == 0, "MENU on the shelf not consumed");
    tick(1);
    CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "MENU on the shelf: closed %d, screen %d", closed,
          wfui_host_scr());
    /* the look strip (Grain, the look item Home kept) opens the browser's own screen on its row, not the shelf */
    to_tile(7);
    key(MODULE_KEY_PRESS_SET);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && !wfui_host_shelf(), "the look strip: screen %d, shelf %d",
          wfui_host_scr(), wfui_host_shelf());
    key(MODULE_KEY_MENU);
    tick(1);
    CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "MENU in the look's browser: closed %d, screen %d", closed,
          wfui_host_scr());

    /* Prints with a blind roll: refused (a toast), still open */
    to_tile(2);
    key(MODULE_KEY_PRESS_SET);
    CHECK(!closed && *wfos_host_msg(), "Prints under a blind roll: closed %d, no toast", closed);
    shot("m1_eosm_home_blind");

    /* Settings */
    to_tile(3);
    key(MODULE_KEY_PRESS_SET);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_SET, "Settings tile: screen %d", wfui_host_scr());
    shot("m1_eosm_settings");
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(!wf_os, "the ring on Camera interface did not turn it to Canon + ML");
    shot("m1_eosm_settings_off");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wf_os, "SET on Camera interface did not turn it back");
    key(MODULE_KEY_PRESS_DOWN);                             /* LiveView finder */
    CHECK(wfos_host_sfoc() == 1, "DOWN from Camera interface: row %d, not LiveView finder", wfos_host_sfoc());
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wf_vf == WF_VF_MIN, "the ring on LiveView finder: %d, not Minimal", wf_vf);
    shot("m3_eosm_settings_finder");
    key(MODULE_KEY_WHEEL_LEFT);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wf_vf == WF_VF_OFF, "the ring back past Full: %d, not Canon + ML", wf_vf);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wf_vf == WF_VF_FULL, "LiveView finder not back to Full (%d)", wf_vf);
    key(MODULE_KEY_PRESS_DOWN);                             /* Quick keys */
    int q0 = wf_qk;
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wf_qk == (q0 + 1) % WF_QK_N, "the ring on Quick keys: %d (was %d)", wf_qk, q0);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wf_qk == q0, "Quick keys not back");
    key(MODULE_KEY_PRESS_DOWN);                             /* Blind roll */
    key(MODULE_KEY_PRESS_SET);
    CHECK(!wf_blind, "SET on Blind roll did not turn it off");
    key(MODULE_KEY_PRESS_DOWN);                             /* ML menu: fixed */
    key(MODULE_KEY_PRESS_SET);
    CHECK(*wfos_host_msg(), "SET on the fixed ML menu row: no toast");
    key(MODULE_KEY_PRESS_RIGHT);                            /* the System section */
    CHECK(wfos_host_ssec() == 1, "RIGHT: section %d", wfos_host_ssec());
    shot("m1_eosm_system");
    key(MODULE_KEY_MENU);
    tick(1);
    CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "MENU in Settings: closed %d, screen %d", closed, wfui_host_scr());

    /* Prints without a blind roll: closes, then Canon gets PLAY */
    to_tile(2);
    host_sent_clear();
    key(MODULE_KEY_PRESS_SET);
    tick(2);
    CHECK(closed, "Prints did not close");
    end_checks();
    CHECK(host_sent_key() == MODULE_KEY_PLAY, "Prints: Canon got key %d, not PLAY", host_sent_key());

    /* MENU on Home: closes at once, then Canon's menu (a real MENU key: no ML menu came) */
    open_home();
    host_sent_clear();
    CHECK(wf_ui_key(MODULE_KEY_MENU) == 0, "MENU on Home not consumed");
    CHECK(wfui_host_after() == WFUI_AFTER_MENU, "MENU on Home: after %d", wfui_host_after());
    tick(1);
    CHECK(closed, "MENU on Home did not close");
    end_checks();
    CHECK(host_sent_key() == MODULE_KEY_MENU && host_nsent() == 1, "MENU on Home: Canon got %d (%d keys)", host_sent_key(),
          host_nsent());
    CHECK(!wf_ui_home_key(MODULE_KEY_MENU), "our own MENU for Canon's menu opened Home again");
    CHECK(wf_ui_home_key(MODULE_KEY_MENU), "the next MENU does not open Home");

    /* the two-finger tap on Home (MENU from ML's module_offer_key): the ML menu opens as we let go, no MENU to Canon */
    open_home();
    host_sent_clear();
    host_tap_menu(1);
    fake_log_clear();
    wf_ui_key(MODULE_KEY_MENU);
    tick(1);
    CHECK(closed, "the tap on Home did not close");
    wfui_task_end();
    CHECK(gui_menu_shown(), "the tap's ML menu did not open");
    CHECK(host_nsent() == 0, "the tap: Canon got key %d too", host_sent_key());
    CHECK(fake_log_has("two-finger tap"), "the tap: not logged");
    host_menu(0, 0, 0);
    CHECK(host_live_allocs() == 0, "%ld allocations left after the tap's close", host_live_allocs());

    /* half-press on Home: closes, nothing handed on */
    open_home();
    host_sent_clear();
    CHECK(wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER) == 1, "half-press on Home not passed on");
    tick(1);
    CHECK(closed, "half-press did not close Home");
    end_checks();
    CHECK(host_nsent() == 0 && host_menu_opens() == 0, "half-press handed on %d keys", host_nsent());

    /* no roll, no film: Home still draws */
    fake_dark_reset();
    fake_set_loaded(0);
    open_home();
    shot("m1_eosm_home_empty");
    wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
    tick(1);
    end_checks();

    /* the font pack from before M1: the display sizes fall back (argnor / 13 px), nothing breaks */
    fake_set_card(oldf);
    roll_in("PRT160", 24, 24, 36);
    open_home();
    shot("m1_eosm_home_oldfonts");
    wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
    tick(1);
    end_checks();
    fake_set_card(card);

    /* Camera interface off: the browser's MENU closes as before (the browser suites cover the rest) */
    wf_os_set(0);
    closed = 0;
    CHECK(wf_ui_open() == 0, "browser open");
    wfui_task_begin();
    tick(1);
    wf_ui_key(MODULE_KEY_MENU);
    tick(1);
    CHECK(closed, "off: MENU did not close the browser");
    end_checks();
    wf_os_set(1);

    /* ---------------------------------------------------------------- EOS M: the LiveView finder */
    fake_dark_reset();
    roll_in("PRT160", 24, 12, 36);
    lv_shots("eosm");
    {
        /* the roll at its end, a scene mode (no meter), the battery low, a long exposure */
        roll_in("PRT160", 24, 36, 36);
        struct wfos_lv v = lv_vals(WF_VF_FULL, "2.5\"", "f/16", "160", 1, WFOS_NOMETER, 0, 812, 0);
        lv_shot("m3_eosm_lv_end", &v);
        v.vf = WF_VF_LINES;
        lv_shot("m3_eosm_lv_lines_end", &v);
        /* no roll: the film's look only, the card's shots left */
        fake_dark_reset();
        v = lv_vals(WF_VF_FULL, "1/30", "f/1.4", "AUTO", 0, -9, -1, 1543, 0);
        lv_shot("m3_eosm_lv_look", &v);
        fake_set_loaded(0);
        v.vf = WF_VF_MIN;
        lv_shot("m3_eosm_lv_canon", &v);
        v.shots = -1;                                       /* no card count either: the counter goes */
        lv_shot("m3_eosm_lv_canon_nocard", &v);
    }

    /* ---------------------------------------------------------------- 5D2, through the viewfinder */
    fake_reset();
    fake_dark_reset();
    fake_set_card(card);
    fake_set_cam(WF_CAM_5D2);
    wf_os_set(1);
    roll_in("PRS400", 21, 17, 36);
    wf_blind = 0;
    lv = 0;
    host_gui_set(0, 1, 1);
    CHECK(wf_ui_home_key(MODULE_KEY_MENU), "5D2: MENU through the viewfinder did not take Home");
    open_home();
    shot("m1_5d2_home");
    to_tile(0);                                             /* 5D2: the Film shelf with the dial and PICT */
    key(MODULE_KEY_JOY_CENTER);
    tick(1);
    CHECK(wfui_host_shelf() && wfui_host_sel_id() == id_of("PRS400"), "5D2 Film shelf: shelf %d, film %d",
          wfui_host_shelf(), wfui_host_sel_id());
    shot("m2_5d2_shelf");
    int t5 = wfui_host_tab();
    key(MODULE_KEY_PICSTYLE);
    CHECK(wfui_host_tab() != t5 && wfui_host_shelf(), "5D2 PICT on the shelf: still tab %d", wfui_host_tab());
    key(MODULE_KEY_MENU);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_HOME, "5D2 MENU on the shelf: screen %d", wfui_host_scr());
    to_tile(3);
    key(MODULE_KEY_JOY_CENTER);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_SET, "5D2: joystick press on Settings: screen %d", wfui_host_scr());
    shot("m1_5d2_settings");
    key(MODULE_KEY_MENU);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_HOME, "5D2: MENU in Settings: screen %d", wfui_host_scr());
    host_sent_clear();
    key(MODULE_KEY_TRASH);                                  /* 5D2: TRASH on Home = the ML menu */
    tick(1);
    CHECK(closed, "5D2: TRASH on Home did not close");
    end_checks();
    CHECK(host_menu_opens() == 1 && host_nsent() == 0, "5D2 TRASH on Home: ML menu %d, keys %d", host_menu_opens(),
          host_nsent());

    /* 5D2 MENU on Home: Canon's menu at once (no tap to wait for) */
    host_gui_set(0, 1, 1);
    open_home();
    host_sent_clear();
    wf_ui_key(MODULE_KEY_MENU);
    tick(1);
    end_checks();
    CHECK(host_sent_key() == MODULE_KEY_MENU, "5D2 MENU on Home: Canon got %d", host_sent_key());
    lv = 1;

    /* 5D2 LiveView: the same finder */
    lv_shots("5d2");

    printf("os_test: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
