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

static void close_ui(int k, unsigned want)
{
    unsigned r = wf_ui_key(k);
    CHECK(r == want, "key %d returned %u, want %u", k, r, want);
    tick(2);
    CHECK(closed, "key %d did not close", k);
    wfui_task_end();
    CHECK(!wf_ui_is_open(), "still open after close");
    CHECK(!host_front_disabled(), "front buffer not given back");
    CHECK(host_palette_is_canon(), "palette entries not given back");
    CHECK(host_front_is_clear(), "front buffer not transparent after close");
    CHECK(host_live_allocs() == 0, "%ld allocations left after close", host_live_allocs());
    CHECK(host_lock_depth() == 0, "BMP lock depth %d", host_lock_depth());
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
    CHECK(wfui_host_group() == g0, "4 group keys did not come back to the group");
    CHECK(wfui_host_sel_id() == id_of("SUN200"), "group selection not remembered");
    CHECK(key(MODULE_KEY_PRESS_SET) == 0, "SET not consumed");
    CHECK(wf_loaded() == id_of("SUN200"), "SET did not load SUN200");
    shot("b2s_loaded");
    close_ui(MODULE_KEY_MENU, 0);

    /* 2. Film LUT group, LV500T, focus on Film LUT (mockup_B2s_strip_lut) */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    fake_set_row(WF_ROW_LIGHT, 1);
    fake_set_row(WF_ROW_FLUT, 2);
    open_ui();
    go_to("LV500T");
    key(MODULE_KEY_PRESS_UP);                                       /* wraps from Light to Film LUT */
    CHECK(wfui_host_focus() == WF_ROW_FLUT, "UP from Light not on Film LUT");
    CHECK(wfui_host_rtop() == WF_NROWS - 7, "Film LUT focused: the list shows its last 7 rows (top %d)", wfui_host_rtop());
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
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && wf_loaded() == id_of("LLEICV"), "SET did not load Leica Vivid");
    shot("b2s_leica");
    go_to("LT25DP");
    fake_set_row(WF_ROW_FLUT, 5);
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && wf_loaded() == id_of("LT25DP"), "SET did not load Windy 250D P");
    shot("b2s_windy");
    close_ui(MODULE_KEY_MENU, 0);

    /* 2b. Highlights / Shadows: 9 rows, 7 shown, the list scrolls with the focus (the font keeps its size) */
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
    CHECK(wfui_host_focus() == WF_ROW_FLUT && wfui_host_rtop() == WF_NROWS - 7, "DOWN to Film LUT: top %d", wfui_host_rtop());
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_LIGHT && wfui_host_rtop() == 0, "DOWN from Film LUT wraps to Light (top %d)",
          wfui_host_rtop());
    for (int i = 0; i < 3; i++) key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == WF_ROW_HL && wfui_host_rtop() == WF_NROWS - 7, "UP x3 from Light: Highlights, top %d",
          wfui_host_rtop());
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
