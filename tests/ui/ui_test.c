/* ui_test.c - host harness for the WindyFilm browser (src/ui): scripted keys against the fake core, checks of the
 * state the keys must reach and of what close must give back, and screenshots (PPM, through the fake hardware palette).
 *
 *   ui_test OUTDIR CARD_DIR CARD_NOCANS_DIR RBF_DIR [EMPTY_CARD_DIR]
 * CARD_DIR holds ML/DATA/WFFONTS.DAT and ML/DATA/WFCANS.DAT; CARD_NOCANS_DIR only the font pack; EMPTY neither. */
#include "host_ml.h"
#include "wf_ui_int.h"
#include "fake_core.h"

int wfui_host_sel_id(void);
int wfui_host_group(void);
int wfui_host_focus(void);
int wfui_host_rtop(void);
int wfui_host_info(void);
int wfui_host_anim(void);
int wfui_host_edit(void);
int wfui_host_phase(void);

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

static const struct wf_film *by_code(const char *c)
{
    for (int i = 0; i < wf_nfilms; i++)
        if (wf_films[i].code && !strcmp(wf_films[i].code, c)) return &wf_films[i];
    return 0;
}

static int id_of(const char *c)
{
    const struct wf_film *f = by_code(c);
    CHECK(f, "no film %s in the catalogue", c);
    return f ? f->id : 0;
}

/* walk there with the keys a user has: Q for the group, LEFT / RIGHT for the film */
static void go_to(const char *code)
{
    const struct wf_film *t = by_code(code);
    CHECK(t, "no film %s", code);
    if (!t) return;
    for (int i = 0; i < WF_NGROUPS && wfui_host_group() != t->group; i++) key(MODULE_KEY_Q);
    CHECK(wfui_host_group() == t->group, "Q did not reach group %d", t->group);
    for (int i = 0; i < 80 && wfui_host_sel_id() != t->id; i++)
    {
        const struct wf_film *c = wf_film_by_id(wfui_host_sel_id());
        int right = c->group < t->group || (c->group == t->group && c->order < t->order);
        key(right ? MODULE_KEY_PRESS_RIGHT : MODULE_KEY_PRESS_LEFT);
    }
    CHECK(wfui_host_sel_id() == t->id, "LEFT / RIGHT did not reach %s", code);
}

static void open_ui(void)
{
    closed = 0;
    CHECK(!wf_ui_is_open(), "open twice");
    CHECK(wf_ui_open() == 0, "wf_ui_open failed");
    wfui_task_begin();
    CHECK(host_front_disabled(), "front buffer not taken");
    tick(1);
}

/* the task closed (tick returned 1): end it and check that everything is given back */
static void end_checks(void)
{
    wfui_task_end();
    CHECK(!wf_ui_is_open(), "still open after close");
    CHECK(!host_front_disabled(), "front buffer not given back");
    CHECK(host_palette_is_canon(), "palette entries not given back");
    CHECK(host_front_is_clear(), "front buffer not transparent after close");
    CHECK(host_live_allocs() == 0, "%ld allocations left after close", host_live_allocs());
    CHECK(host_lock_depth() == 0, "BMP lock depth %d", host_lock_depth());
    CHECK(!host_bmp_muted(), "Canon's bitmap left muted after close");
}

static void close_ui(int k, unsigned want)
{
    unsigned r = wf_ui_key(k);
    CHECK(r == want, "key %d returned %u, want %u", k, r, want);
    tick(2);
    CHECK(closed, "key %d did not close", k);
    end_checks();
}

/* SET on the selected film: the load animation runs, then the browser closes by itself */
static void load_and_close(int want_id)
{
    CHECK(key(MODULE_KEY_PRESS_SET) == 0, "SET not consumed");
    CHECK(wf_loaded() == want_id, "SET did not load %d (loaded %d)", want_id, wf_loaded());
    CHECK(wfui_host_anim() && !closed, "no load animation after SET");
    tick(40);
    CHECK(!closed, "closed before the animation ended");
    tick(40);
    CHECK(closed, "not closed after the load animation");
    end_checks();
}

static void shot(const char *name)
{
    char p[700];
    snprintf(p, sizeof(p), "%s/%s.ppm", outdir, name);
    tick(1);
    int stray = host_dump(p);
    CHECK(stray == 0, "%s: %d pixels outside the browser's palette entries", name, stray);
    printf("shot %s\n", p);
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: ui_test OUTDIR CARD NOCANS RBFDIR [EMPTY]\n"); return 2; }
    outdir = argv[1];
    const char *card = argv[2], *nocans = argv[3], *empty = argc > 5 ? argv[5] : 0;
    host_init(argv[4], 0x03FF0000u, 0x03000000u);
    wf_ui_init();
    CHECK(host_live_allocs() == 0, "wf_ui_init allocated");

    /* 1. Colour group, SUN200, Portra 160 loaded (mockup_B2s_strip) */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    CHECK(wfui_host_sel_id() == id_of("PRT160"), "open does not start on the loaded film");
    go_to("SUN200");
    CHECK(wfui_host_focus() == WF_ROW_LIGHT, "focus not on Light");
    shot("b2s_colour");
    int v0 = fake_row_value(WF_ROW_LIGHT);
    CHECK(key(MODULE_KEY_WHEEL_RIGHT) == 0, "wheel not consumed");
    CHECK(fake_row_value(WF_ROW_LIGHT) == v0 + 1, "WHEEL_RIGHT did not step Light");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(fake_row_value(WF_ROW_LIGHT) == v0, "WHEEL_LEFT did not step back");
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_GRAIN, "DOWN x2 not on Grain");
    key(MODULE_KEY_WHEEL_DOWN);                                     /* 5D2 rear wheel */
    CHECK(fake_row_value(WF_ROW_GRAIN) == 3, "WHEEL_DOWN did not step Grain");
    key(MODULE_KEY_WHEEL_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    int g0 = wfui_host_group();
    key(MODULE_KEY_TOUCH_1_FINGER);
    CHECK(wfui_host_group() != g0, "1-finger tap did not change the group");
    key(MODULE_KEY_PICSTYLE);
    key(MODULE_KEY_Q);
    key(MODULE_KEY_Q);
    key(MODULE_KEY_Q);                                              /* 5 groups since 11-15 (Film stage) */
    CHECK(wfui_host_group() == g0, "5 group keys did not come back to the group");
    CHECK(wfui_host_sel_id() == id_of("SUN200"), "group selection not remembered");
    host_gui_set(0, 1, 1);
    load_and_close(id_of("SUN200"));                                /* EOS M: back to LiveView, nothing else */
    CHECK(host_lv_starts() == 0 && host_gui_requests() == 0, "EOS M load: %d LV starts, %d gui requests",
          host_lv_starts(), host_gui_requests());
    open_ui();
    CHECK(wfui_host_sel_id() == id_of("SUN200"), "reopen not on the loaded film");
    shot("b2s_loaded");
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && !wfui_host_anim() && !closed, "SET on the loaded film: no animation, stays open");
    close_ui(MODULE_KEY_MENU, 0);

    /* 2. Film LUT group, LV500T, focus on Film LUT (mockup_B2s_strip_lut) */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    fake_set_row(WF_ROW_LIGHT, 1);
    fake_set_row(WF_ROW_FLUT, 2);
    open_ui();
    go_to("LV500T");
    key(MODULE_KEY_PRESS_UP);                                       /* wraps from Light to the last row */
    CHECK(wfui_host_focus() == WF_NROWS - 1, "UP from Light not on the last row (Interlayer)");
    CHECK(wfui_host_rtop() == WF_NROWS - 7, "last row focused: the list shows its last 7 rows (top %d)", wfui_host_rtop());
    for (int i = WF_NROWS - 1; i > WF_ROW_FLUT; i--)
        key(MODULE_KEY_PRESS_UP);                                   /* up the seven film-stage knob rows */
    CHECK(wfui_host_focus() == WF_ROW_FLUT, "UP x %d from Light not on Film LUT", WF_NROWS - WF_ROW_FLUT);
    CHECK(wfui_host_rtop() == WF_ROW_FLUT, "Film LUT focused from below: it tops the list (top %d)", wfui_host_rtop());
    shot("b2s_lut");
    close_ui(MODULE_KEY_TRASH, 0);

    /* 2c. Leica Vivid (11-14): its can selected and loaded (SET), the Film LUT row on it; a Windy can for the new names */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    fake_set_row(WF_ROW_FLUT, 4);
    open_ui();
    go_to("LLEICV");
    CHECK(wfui_host_sel_id() == id_of("LLEICV"), "Leica Vivid not selected");
    load_and_close(id_of("LLEICV"));
    open_ui();
    shot("b2s_leica");
    go_to("LT25DP");
    fake_set_row(WF_ROW_FLUT, 5);
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && wf_loaded() == id_of("LT25DP"), "SET did not load Windy 250D P");
    tick(18);
    shot("b2s_anim_wind");                                          /* the leader on its way (~400 ms) */
    tick(26);
    shot("b2s_anim_done");                                          /* in the gate, "Loaded" (~950 ms) */
    CHECK(!closed, "closed during the animation");
    tick(20);
    CHECK(closed, "not closed after the animation");
    end_checks();
    open_ui();
    shot("b2s_windy");
    close_ui(MODULE_KEY_MENU, 0);

    /* 2d. a key during the load animation closes at once (the film stays loaded) */
    fake_reset();
    fake_set_card(card);
    open_ui();
    go_to("SUN200");
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && wfui_host_anim(), "SET: no animation");
    tick(5);
    CHECK(key(MODULE_KEY_INFO) == 0, "INFO in the animation not consumed");
    tick(1);
    CHECK(closed && wf_loaded() == id_of("SUN200"), "a key did not skip the animation");
    end_checks();

    /* 2e. film-stage (11-15): a chain can (G08 = 5219 500T, the Film stage tab) loaded, Print stock 2302 (its full name
     * on the row), Grain Even Normal, Halation Wrap; the focus walks down to Halation, then Bloom and Gauge; INFO */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("G08"));
    fake_set_row(WF_ROW_PRINT, 3);
    fake_set_row(WF_ROW_GRAIN, 5);
    fake_set_row(WF_ROW_HALO, 3);
    open_ui();
    CHECK(wfui_host_sel_id() == id_of("G08"), "open not on the loaded chain can");
    CHECK(wfui_host_group() == WF_GRP_STAGE, "chain can in group %d, not Film stage", wfui_host_group());
    shot("fs_chain");
    for (int i = 0; i < WF_ROW_HALO; i++) key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_HALO && !strcmp(wf_row_name(WF_ROW_HALO), "Halation"), "DOWN x3 not on Halation");
    shot("fs_rows");
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfui_host_focus() == WF_ROW_BLOOM && !strcmp(wf_row_text(WF_ROW_BLOOM), "Strong")
          && !strcmp(wf_row_text(WF_ROW_HALO), "Wrap"), "Bloom row: RIGHT to Strong, Halation untouched ('%s', '%s')",
          wf_row_text(WF_ROW_BLOOM), wf_row_text(WF_ROW_HALO));
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_focus() == WF_ROW_GAUGE && !strcmp(wf_row_text(WF_ROW_GAUGE), "16 mm"), "Gauge row: LEFT to 16 mm ('%s')",
          wf_row_text(WF_ROW_GAUGE));
    shot("fs_bloom_gauge");
    key(MODULE_KEY_INFO);
    CHECK(wfui_host_info(), "INFO did not open the overlay");
    shot("fs_info");
    key(MODULE_KEY_INFO);
    close_ui(MODULE_KEY_MENU, 0);
    /* the longest print stock name (2395 Teleprint) on the focused Print stock row, between its arrows */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("G08"));
    fake_set_row(WF_ROW_PRINT, 6);
    open_ui();
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_PRINT, "DOWN not on Print stock");
    shot("fs_print_long");
    close_ui(MODULE_KEY_MENU, 0);

    /* 2b. Highlights / Shadows: WF_NROWS rows (18 since 11-15), 7 shown, the list scrolls with the focus (the font
     * keeps its size) */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    go_to("SUN200");
    CHECK(wfui_host_rtop() == 0, "open: the list starts at Light");
    for (int i = 0; i < WF_ROW_HL; i++) key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_HL && wfui_host_rtop() == WF_ROW_HL - 6, "DOWN to Highlights: focus %d top %d",
          wfui_host_focus(), wfui_host_rtop());
    key(MODULE_KEY_WHEEL_RIGHT);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(fake_row_value(WF_ROW_HL) == 7, "Highlights +2 (value %d)", fake_row_value(WF_ROW_HL));
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_focus() == WF_ROW_SH && fake_row_value(WF_ROW_SH) == 4, "Shadows -1 (value %d)", fake_row_value(WF_ROW_SH));
    shot("b2s_hs");
    for (int i = 0; i < 6; i++) key(MODULE_KEY_WHEEL_LEFT);
    CHECK(fake_row_value(WF_ROW_SH) == 0, "Shadows stops at -5 (value %d)", fake_row_value(WF_ROW_SH));
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_FLUT && wfui_host_rtop() == WF_ROW_FLUT - 6, "DOWN to Film LUT: top %d",
          wfui_host_rtop());
    /* the film-stage knob rows follow Film LUT (since 11-15); the last row wraps to Light */
    for (int i = WF_ROW_FLUT + 1; i < WF_NROWS; i++) key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_NROWS - 1 && wfui_host_rtop() == WF_NROWS - 7, "DOWN to the last row: focus %d top %d",
          wfui_host_focus(), wfui_host_rtop());
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_LIGHT && wfui_host_rtop() == 0, "DOWN from the last row wraps to Light (top %d)",
          wfui_host_rtop());
    for (int i = 0; i < 3; i++) key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == WF_NROWS - 3 && wfui_host_rtop() == WF_NROWS - 7, "UP x3 from Light: focus %d top %d",
          wfui_host_focus(), wfui_host_rtop());
    for (int i = WF_NROWS - 3; i > WF_ROW_HL; i--) key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == WF_ROW_HL && wfui_host_rtop() == WF_ROW_HL, "UP to Highlights: focus %d top %d",
          wfui_host_focus(), wfui_host_rtop());
    shot("b2s_hs_top");
    close_ui(MODULE_KEY_MENU, 0);

    /* 3. INFO overlay, EOS M and 5D2 */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    go_to("SUN200");
    key(MODULE_KEY_INFO);
    CHECK(wfui_host_info(), "INFO did not open the overlay");
    shot("b2s_info");
    key(MODULE_KEY_INFO);
    CHECK(!wfui_host_info(), "INFO did not close the overlay");
    close_ui(MODULE_KEY_PRESS_HALFSHUTTER, 1);                      /* half-shutter: close and pass on */

    fake_reset();
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    open_ui();
    go_to("LV250D");
    key(MODULE_KEY_INFO);
    shot("b2s_info_5d2");
    close_ui(MODULE_KEY_PLAY, 0);

    /* 4. greyed rows, a film whose cube is not on the card, SET on it, a core status line */
    fake_reset();
    fake_set_card(card);
    fake_set_grey((1 << WF_ROW_PRINT) | (1 << WF_ROW_GRAIN));
    fake_set_missing(id_of("LK83V3"));
    open_ui();
    go_to("LK83V3");
    key(MODULE_KEY_PRESS_DOWN);
    int pv = fake_row_value(WF_ROW_PRINT);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(fake_row_value(WF_ROW_PRINT) == pv, "a greyed row changed");
    key(MODULE_KEY_JOY_CENTER);
    CHECK(wf_loaded() == 0, "a film not on the card was loaded");
    shot("b2s_greyed");
    tick(150);                                                      /* message times out (2.5 s) */
    fake_set_status("Disable filmroll.mo");
    shot("b2s_status");
    close_ui(MODULE_KEY_MENU, 0);

    /* 5. no can pack: plain cans in the group colour */
    fake_reset();
    fake_set_card(nocans);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    CHECK(wfc_status() == WFC_E_NOFILE, "can pack status %d", wfc_status());
    go_to("SUN200");
    shot("b2s_nocans");
    go_to("LV500T");
    shot("b2s_nocans_lut");
    close_ui(MODULE_KEY_MENU, 0);

    /* 5b. empty card: no can pack and no font pack (small text falls back to ML's term12) */
    if (empty)
    {
        fake_reset();
        fake_set_card(empty);
        open_ui();
        CHECK(wff_status() & 1, "font pack reported present");
        go_to("SUN200");
        shot("b2s_nopacks");
        close_ui(MODULE_KEY_MENU, 0);
    }

    /* 5c. 5D2 without LiveView (owner 11-14: the browser came up only with LV on): Canon's menu mode 2 brings the display
     * up before the browser draws, mode 0 again on close while the front buffer is still ours */
    fake_reset();
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    lv = 0;
    host_gui_set(0, 0, 1);                                          /* viewfinder, display off */
    open_ui();
    CHECK(host_gui_mode() == 2 && display_is_on() && host_gui_requests() == 1, "5D2 no LV: gui mode %d, display %d, %d requests",
          host_gui_mode(), display_is_on(), host_gui_requests());
    go_to("LV250D");
    shot("b2s_5d2_vf");
    close_ui(MODULE_KEY_TRASH, 0);
    CHECK(host_gui_mode() == 0 && host_gui_requests() == 2 && host_gui_req0_front() == 1,
          "5D2 no LV close: gui mode %d, %d requests, front buffer at mode 0: %d", host_gui_mode(), host_gui_requests(),
          host_gui_req0_front());

    fake_reset();                                                   /* Canon leaves the menu mode itself: we close */
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    host_gui_set(0, 1, 1);
    open_ui();
    host_gui_later(0, 0);
    tick(2);
    CHECK(closed, "5D2 no LV: still open after Canon left its menu mode");
    wfui_task_end();
    CHECK(!wf_ui_is_open() && !host_front_disabled(), "5D2 no LV: not closed cleanly");

    fake_reset();                                                   /* the mode does not light the display: display_on */
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    host_gui_set(0, 0, 0);
    open_ui();
    CHECK(host_display_ons() == 1 && display_is_on(), "5D2 no LV: display_on not used (%d)", host_display_ons());
    close_ui(MODULE_KEY_MENU, 0);

    fake_reset();                                                   /* from the ML menu entry: its mode 0 comes first */
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    host_gui_set(2, 1, 1);
    host_gui_later(0, 300);
    open_ui();
    CHECK(host_gui_mode() == 2 && host_gui_requests() == 1, "5D2 no LV after the ML menu: gui mode %d, %d requests",
          host_gui_mode(), host_gui_requests());
    close_ui(MODULE_KEY_MENU, 0);

    fake_reset();                                                   /* the EOS M asks for no mode here */
    fake_set_card(card);
    host_gui_set(0, 1, 1);
    lv = 0;
    open_ui();
    close_ui(MODULE_KEY_MENU, 0);
    CHECK(host_gui_requests() == 0, "EOS M: %d gui mode requests", host_gui_requests());
    lv = 1;

    /* 5d. 5D2 in LiveView (owner 11-14: the rows could not be changed): ML's LV menu mode 38 under the browser, Canon's
     * bitmap muted while that dialog comes up and unmuted once the front buffer is ours; on close mode 0, then Canon's
     * front buffer and bitmap. The dials step the rows; a joystick press edits the focused row with LEFT / RIGHT. */
    fake_reset();
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    lv = 1;
    host_gui_set(0, 1, 1);
    open_ui();
    CHECK(host_gui_mode() == 38 && host_gui_requests() == 1 && host_bmp_muted_at_req() == 1 && !host_bmp_muted(),
          "5D2 LV: gui mode %d, %d requests, muted at the request %d, muted now %d", host_gui_mode(), host_gui_requests(),
          host_bmp_muted_at_req(), host_bmp_muted());
    go_to("SUN200");
    int l0 = fake_row_value(WF_ROW_LIGHT);
    key(MODULE_KEY_WHEEL_RIGHT);                                    /* main dial */
    key(MODULE_KEY_WHEEL_DOWN);                                     /* rear dial */
    CHECK(fake_row_value(WF_ROW_LIGHT) == l0 + 2, "5D2 LV: the dials did not step Light (%d -> %d)", l0,
          fake_row_value(WF_ROW_LIGHT));
    key(MODULE_KEY_JOY_CENTER);
    CHECK(wfui_host_edit() && wf_loaded() == 0, "5D2: joystick press did not start editing (edit %d, loaded %d)",
          wfui_host_edit(), wf_loaded());
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(fake_row_value(WF_ROW_LIGHT) == l0 + 1 && wfui_host_sel_id() == id_of("SUN200"),
          "5D2 edit: LEFT did not step Light back, or changed the film");
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_focus() == WF_ROW_GRAIN && fake_row_value(WF_ROW_GRAIN) == 3, "5D2 edit: DOWN x2 + RIGHT: focus %d grain %d",
          wfui_host_focus(), fake_row_value(WF_ROW_GRAIN));
    shot("b2s_5d2_edit");
    key(MODULE_KEY_JOY_CENTER);
    CHECK(!wfui_host_edit(), "5D2: second joystick press did not end editing");
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_sel_id() != id_of("SUN200"), "5D2: RIGHT after editing did not change the film");
    shot("b2s_5d2_lv");
    load_and_close(wfui_host_sel_id());
    CHECK(host_gui_mode() == 0 && host_gui_requests() == 2 && host_gui_req0_front() == 1 && host_bmp_offs() == 2 &&
          host_lv_starts() == 0 && lv, "5D2 LV load: mode %d, %d requests, front at 0: %d, %d bmp_off, %d LV starts",
          host_gui_mode(), host_gui_requests(), host_gui_req0_front(), host_bmp_offs(), host_lv_starts());

    fake_reset();                                                   /* recording: no Canon dialog (ML: mode 0) */
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    host_gui_set(0, 1, 1);
    host_recording = 1;
    open_ui();
    close_ui(MODULE_KEY_MENU, 0);
    CHECK(host_gui_requests() == 0 && host_bmp_offs() == 0, "5D2 recording: %d gui requests, %d bmp_off",
          host_gui_requests(), host_bmp_offs());
    host_recording = 0;

    /* 5e. 5D2 without LiveView: SET loads, the animation runs, then LiveView starts (owner 11-14: "LiveView on the 5D2
     * too"); a half-press during the animation shoots through the viewfinder instead: no LiveView */
    fake_reset();
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    lv = 0;
    host_gui_set(0, 0, 1);
    open_ui();
    go_to("LV250D");
    load_and_close(id_of("LV250D"));
    CHECK(host_lv_starts() == 1 && lv == 1 && host_gui_mode() == 0, "5D2 no LV load: %d LV starts, lv %d, gui mode %d",
          host_lv_starts(), lv, host_gui_mode());

    fake_reset();
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    lv = 0;
    host_gui_set(0, 0, 1);
    open_ui();
    go_to("SUN200");
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && wfui_host_anim(), "5D2 no LV: SET, no animation");
    CHECK(wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER) == 1, "half-shutter in the animation not passed on");
    tick(2);
    CHECK(closed, "half-shutter did not close the animation");
    end_checks();
    CHECK(host_lv_starts() == 0 && lv == 0, "half-shutter in the animation: LiveView started (%d)", host_lv_starts());
    lv = 1;

    /* 5f. never a dead camera (owner 11-14, EOS M: neither the film browser nor the ML menu opened): a UI task that has
     * not run for STALE_MS lets keys through and closes when it runs again */
    fake_reset();
    fake_set_card(card);
    host_gui_set(0, 1, 1);
    open_ui();
    CHECK(wf_ui_key(MODULE_KEY_PRESS_LEFT) == 0, "a key was not consumed while the task beats");
    tick(1);
    msleep(2100);                                                   /* the task is starved: no tick */
    CHECK(wf_ui_key(MODULE_KEY_MENU) == 1 && wf_ui_key(MODULE_KEY_PRESS_DOWN) == 1,
          "keys still eaten with no beat for 2.1 s");
    CHECK(wf_ui_is_open(), "the browser declared closed from outside");
    tick(1);
    CHECK(closed, "the task did not close when it ran again");
    end_checks();

    fake_reset();                                                   /* the task never ran after TRASH */
    fake_set_card(card);
    closed = 0;
    CHECK(wf_ui_open() == 0, "open");
    msleep(2500);
    CHECK(wf_ui_key(MODULE_KEY_TRASH) == 1 && wf_ui_key(MODULE_KEY_MENU) == 1, "keys eaten before the task ran");
    wfui_task_begin();
    CHECK(!host_front_disabled(), "a late task took the screen after the keys went on");
    tick(1);
    CHECK(closed, "a late task did not close");
    end_checks();

    {                                                               /* a second open while one is under way: one task */
        fake_reset();
        fake_set_card(card);
        int t0 = host_tasks();
        closed = 0;
        CHECK(wf_ui_open() == 0 && wf_ui_open() == 0 && host_tasks() == t0 + 1, "two opens made %d tasks", host_tasks() - t0);
        wfui_task_begin();
        tick(1);
        close_ui(MODULE_KEY_MENU, 0);
        host_task_fail(1);                                          /* no task: not open, keys pass on */
        CHECK(wf_ui_open() == -1 && !wf_ui_is_open() && wf_ui_key(MODULE_KEY_PRESS_LEFT) == 1, "task_create failure");
        host_task_fail(0);
    }

    /* 5g. Canon holds the card (owner's RAWB2 log, EOS M 10 s after boot: "fonts WFFONTS.DAT not on the card, cans
     * WFCANS.DAT read failed (0 colours)" with both files on the card) */
    {
        char want[96];
        fake_reset();                                               /* a short busy spell: the open waits it out */
        fake_set_card(card);
        fake_log_clear();
        fake_card_busy(4, 0);
        open_ui();
        CHECK(!(wff_status() & 1) && wfc_status() == WFC_OK, "short busy spell: fonts %s, cans %s", wff_why(),
              wfc_why(wfc_status()));
        CHECK(fake_log_has(", 4 reads tried again"), "the open log does not count the reads tried again");
        close_ui(MODULE_KEY_MENU, 0);

        fake_reset();                                               /* busy through the whole open */
        fake_set_card(card);
        fake_log_clear();
        fake_card_busy(-1, 0);
        int t0 = get_ms_clock();
        open_ui();
        CHECK(get_ms_clock() - t0 <= 1700, "the open waited %d ms for the card", get_ms_clock() - t0);
        CHECK((wff_status() & 1) && !strcmp(wff_why(), "card busy"), "fonts: %s", wff_why());
        CHECK(wfc_status() == WFC_E_BUSY && wfc_pal_used() == WFC_PLAIN_N, "cans: %s, %d colours",
              wfc_why(wfc_status()), wfc_pal_used());
        snprintf(want, sizeof want, "fonts WFFONTS.DAT card busy, cans WFCANS.DAT card busy (plain cans, %d colours)",
                 WFC_PLAIN_N);
        CHECK(fake_log_has(want) && fake_log_has("packs tried again while open"), "open log: want \"%s\"", want);
        snprintf(want, sizeof want, "can slots, %d in use)", WFC_PLAIN_N);
        CHECK(fake_log_has(want), "the palette log counts can slots, not the colours in use");
        CHECK(!fake_log_has("not on the card"), "a busy card reported as a missing file");
        go_to("SUN200");
        shot("b2s_cardbusy");
        fake_card_busy(0, 0);                                       /* the card answers again: both packs come in */
        tick(110);
        CHECK(!closed && !(wff_status() & 1) && wfc_status() == WFC_OK, "not read again: fonts %s, cans %s",
              wff_why(), wfc_why(wfc_status()));
        CHECK(fake_log_has("ui: fonts WFFONTS.DAT ok, read again") && fake_log_has("ui: cans WFCANS.DAT ok ("),
              "no log line for the packs read again");
        shot("b2s_cardbusy_later");
        close_ui(MODULE_KEY_MENU, 0);

        fake_reset();                                               /* the header, then Canon takes the card */
        fake_set_card(card);
        fake_card_busy(-1, 1);                                      /* every read past offset 0 fails */
        open_ui();
        CHECK(!(wff_status() & 1), "fonts lost: %s", wff_why());
        CHECK(wfc_status() == WFC_E_BUSY && wfc_pal_used() == WFC_PLAIN_N, "cans after the header: %s, %d colours",
              wfc_why(wfc_status()), wfc_pal_used());
        go_to("LV500T");
        tick(5);
        close_ui(MODULE_KEY_MENU, 0);                               /* end_checks: no index or blob left behind */

        fake_reset();                                               /* busy for good: PACK_TRIES, then no more reads */
        fake_set_card(card);
        fake_log_clear();
        fake_card_busy(-1, 0);
        open_ui();
        tick(5 * 100 + 20);
        CHECK(fake_log_has("ui: packs read again 5 times: fonts card busy, cans card busy"), "no give-up line");
        int r0 = fake_card_reads();
        tick(200);
        CHECK(!closed && fake_card_reads() == r0, "%d reads after giving up", fake_card_reads() - r0);
        close_ui(MODULE_KEY_MENU, 0);

        fake_reset();                                               /* can images that fail to read while open */
        fake_set_card(card);
        fake_set_loaded(id_of("PRT160"));
        open_ui();
        fake_card_busy(-1, 1);
        go_to("LV500T");                                            /* another group: its cans are not cached */
        r0 = fake_card_reads();
        tick(100);
        CHECK(fake_card_reads() - r0 <= 3, "%d image reads in 2 s on a busy card", fake_card_reads() - r0);
        fake_card_busy(0, 0);
        r0 = fake_card_reads();
        tick(60);
        CHECK(fake_card_reads() > r0, "the can images were not read again");
        shot("b2s_blob_later");
        close_ui(MODULE_KEY_MENU, 0);

        fake_reset();                                               /* a pack really not on the card: no retries */
        fake_set_card(nocans);
        fake_log_clear();
        open_ui();
        CHECK(wfc_status() == WFC_E_NOFILE && fake_log_has("cans WFCANS.DAT not on the card (plain cans"),
              "missing can pack: %s", wfc_why(wfc_status()));
        tick(120);
        CHECK(!fake_log_has("read again") && !fake_log_has("tried again"), "a missing pack was tried again");
        close_ui(MODULE_KEY_MENU, 0);

        if (empty)                                                  /* no font pack and no term12: text left out */
        {
            fake_reset();
            fake_set_card(empty);
            host_hide_font("term12");
            open_ui();
            CHECK((wff_status() & 1) && ui_textw(F_13, "Light") == 0 && ui_texth(F_13) > 0, "no-font fallback");
            go_to("SUN200");
            key(MODULE_KEY_INFO);
            shot("b2s_nofont");
            key(MODULE_KEY_INFO);
            close_ui(MODULE_KEY_MENU, 0);
            host_hide_font(0);
        }
    }

    /* 6. keys while closed pass on; the open key */
    CHECK(wf_ui_key(MODULE_KEY_PRESS_LEFT) == 1, "a key was eaten while closed");
    fake_set_cam(WF_CAM_EOSM);
    CHECK(wf_ui_open_key(MODULE_KEY_TRASH) == 1, "EOS M: TRASH (long DOWN) does not open");
    CHECK(wf_ui_open_key(MODULE_KEY_PRESS_DOWN) == 0, "EOS M: DOWN opens");
    fake_set_cam(WF_CAM_5D2);
    CHECK(wf_ui_open_key(MODULE_KEY_TRASH) == 1, "5D2: TRASH does not open");
    lv = 0;
    CHECK(wf_ui_open_key(MODULE_KEY_TRASH) == 1, "5D2: TRASH outside LV does not open");
    fake_set_cam(WF_CAM_EOSM);
    CHECK(wf_ui_open_key(MODULE_KEY_TRASH) == 0, "EOS M: opens outside LV");
    lv = 1;

    printf("peak UI heap %ld bytes\n", host_peak_bytes());
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
