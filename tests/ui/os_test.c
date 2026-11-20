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

int wfui_host_scr(void);
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

    /* Film shelf tile -> the browser; MENU there -> Home (not closed) */
    to_tile(0);
    key(MODULE_KEY_PRESS_SET);
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER, "Film shelf tile: screen %d", wfui_host_scr());
    CHECK(key(MODULE_KEY_MENU) == 0, "MENU in the browser not consumed");
    tick(1);
    CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "MENU in the browser: closed %d, screen %d", closed,
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

    printf("os_test: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
