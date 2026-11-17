/* ui_test.c - host harness for the WindyFilm browser (src/ui): scripted keys against the fake core, checks of the
 * state the keys must reach and of what close must give back, and screenshots (PPM, through the fake hardware palette).
 *
 *   ui_test OUTDIR CARD_DIR CARD_NOCANS_DIR RBF_DIR [EMPTY_CARD_DIR]
 * CARD_DIR holds ML/DATA/WFFONTS.DAT and ML/DATA/WFCANS.DAT; CARD_NOCANS_DIR only the font pack; EMPTY neither. */
#include "host_ml.h"
#include "wf_ui_int.h"
#include "fake_core.h"
#include "fake_dark.h"

int wfui_host_sel_id(void);
int wfui_host_group(void);
int wfui_host_tab(void);
int wfui_host_tab_of(int id);
int wfui_host_sec(void);
int wfui_host_tsub(void);
int wfui_host_secq(void);
int wfui_host_rows(void);
int wfui_host_focus(void);
int wfui_host_rtop(void);
int wfui_host_info(void);
int wfui_host_info_y(void);
int wfui_host_anim(void);
int wfui_host_edit(void);
int wfui_host_phase(void);
int wfui_host_mode(void);
int wfui_host_leave(void);
int wfui_host_swap(void);
int wfui_host_exp(void);
int wfui_host_chip(void);
int wfui_host_nr(void);
int wfui_host_scr(void);
const char *wfui_host_msg(void);
extern volatile int wf_rawb;
extern volatile int wf_roll_look8;
#define FOC_TOP  (-2)                           /* wf_ui.c's focus on the top bar: the section pill or the mode chip */
#define FOC_EXP  (-1)

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

/* the top bar: UP until the focus is there (the section pill or the mode chip) */
static void to_top(void)
{
    for (int i = 0; i < 40 && wfui_host_focus() != FOC_TOP; i++) key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == FOC_TOP, "UP did not reach the top bar (focus %d)", wfui_host_focus());
}

/* the mode chip one step (d > 0 the ring right): the top bar, RIGHT onto the chip, the ring */
static void chip_step(int d)
{
    to_top();
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_tsub() == 1, "RIGHT on the top bar: not the chip");
    key(d > 0 ? MODULE_KEY_WHEEL_RIGHT : MODULE_KEY_WHEEL_LEFT);
}

/* walk there with the keys a user has: the section pill (UP to the top bar, LEFT, SET, DOWN back to the row), TAP for the
 * tab, LEFT / RIGHT for the film */
static void go_to(const char *code)
{
    const struct wf_film *t = by_code(code);
    CHECK(t, "no film %s", code);
    if (!t) return;
    int tt = wfui_host_tab_of(t->id), ts = tt >= WF_GRP_STAGE;
    if (wfui_host_sec() != ts)
    {
        int f0 = wfui_host_focus();
        to_top();
        key(MODULE_KEY_PRESS_LEFT);
        key(MODULE_KEY_PRESS_SET);
        CHECK(wfui_host_sec() == ts && wfui_host_secq() < 0, "the pill did not switch to section %d", ts);
        if (f0 != FOC_TOP)
            for (int i = 0; i < 40 && wfui_host_focus() != f0; i++)
            {
                key(MODULE_KEY_PRESS_DOWN);
                if (f0 == FOC_EXP && !wfui_host_rows()) break;  /* no Exposures row in this section: Light */
            }
    }
    for (int i = 0; i < 5 && wfui_host_tab() != tt; i++) key(MODULE_KEY_Q);
    CHECK(wfui_host_tab() == tt, "TAP did not reach tab %d", tt);
    for (int i = 0; i < 80 && wfui_host_sel_id() != t->id; i++)
    {
        const struct wf_film *c = wf_film_by_id(wfui_host_sel_id());
        int ct = wfui_host_tab_of(c->id), right = ct < tt || (ct == tt && c->order < t->order);
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

/* the core's guard task: wf_ui_menu_guard every 50 ms for ms; returns the asks it made, *first = ms of the first one */
static int mg_run(int ms, int *first)
{
    int n = 0, t0 = get_ms_clock();
    if (first) *first = -1;
    while (get_ms_clock() - t0 < ms)
    {
        if (wf_ui_menu_guard())
        {
            if (first && *first < 0) *first = get_ms_clock() - t0;
            n++;
        }
        msleep(50);
    }
    return n;
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
    /* Paper, right under Print stock (11-16): Glossy first, the ring steps it */
    CHECK(wfui_host_focus() == WF_ROW_PAPER && WF_ROW_PAPER == WF_ROW_PRINT + 1 && !strcmp(wf_row_name(WF_ROW_PAPER), "Paper")
          && !strcmp(wf_row_text(WF_ROW_PAPER), "Glossy"), "DOWN x2 not on Paper / Glossy (focus %d, '%s')",
          wfui_host_focus(), wf_row_text(WF_ROW_PAPER));
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(!strcmp(wf_row_text(WF_ROW_PAPER), "Matte"), "WHEEL_RIGHT on Paper: Matte ('%s')", wf_row_text(WF_ROW_PAPER));
    shot("b2s_paper");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(!strcmp(wf_row_text(WF_ROW_PAPER), "Glossy"), "WHEEL_LEFT on Paper: Glossy again");
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_GRAIN, "DOWN x3 not on Grain");
    key(MODULE_KEY_WHEEL_DOWN);                                     /* 5D2 rear wheel */
    CHECK(fake_row_value(WF_ROW_GRAIN) == 3, "WHEEL_DOWN did not step Grain");
    key(MODULE_KEY_WHEEL_UP);
    /* Bloom, its own row right after Halation (RAWB11): the same row style, the ring (WHEEL_RIGHT / LEFT) steps it */
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_BLOOM && WF_ROW_BLOOM == WF_ROW_HALO + 1 && !strcmp(wf_row_name(WF_ROW_BLOOM), "Bloom"),
          "DOWN x2 from Grain not on Bloom (focus %d, '%s')", wfui_host_focus(), wf_row_name(wfui_host_focus()));
    CHECK(!strcmp(wf_row_text(WF_ROW_BLOOM), "Normal"), "Bloom starts on Normal ('%s')", wf_row_text(WF_ROW_BLOOM));
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(!strcmp(wf_row_text(WF_ROW_BLOOM), "Strong") && !strcmp(wf_row_text(WF_ROW_HALO), "Wide"),
          "WHEEL_RIGHT on Bloom: Strong, Halation untouched ('%s', '%s')", wf_row_text(WF_ROW_BLOOM), wf_row_text(WF_ROW_HALO));
    shot("b2s_bloom");
    key(MODULE_KEY_WHEEL_LEFT);                                     /* (the core's wrap at the ends: host FX:bloom) */
    CHECK(!strcmp(wf_row_text(WF_ROW_BLOOM), "Normal"), "WHEEL_LEFT: Normal again ('%s')", wf_row_text(WF_ROW_BLOOM));
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    int g0 = wfui_host_tab();
    key(MODULE_KEY_TOUCH_1_FINGER);
    CHECK(wfui_host_tab() != g0 && wfui_host_sec() == 0, "1-finger tap did not change the tab, or left Film");
    key(MODULE_KEY_PICSTYLE);
    key(MODULE_KEY_Q);
    CHECK(wfui_host_sec() == 0, "TAP left the Film section (tab %d)", wfui_host_tab());
    key(MODULE_KEY_Q);                                              /* the Film section's 4 tabs (11-17: Film stage */
    CHECK(wfui_host_tab() == g0, "4 tab keys did not come back to the tab");   /* has its own) */
    CHECK(wfui_host_sel_id() == id_of("SUN200"), "tab selection not remembered");
    host_gui_set(0, 1, 1);
    load_and_close(id_of("SUN200"));                                /* EOS M: back to LiveView, nothing else */
    CHECK(host_lv_starts() == 0 && host_gui_requests() == 0, "EOS M load: %d LV starts, %d gui requests",
          host_lv_starts(), host_gui_requests());
    open_ui();
    CHECK(wfui_host_sel_id() == id_of("SUN200"), "reopen not on the loaded film");
    shot("b2s_loaded");
    {                                                               /* SET on the loaded film (owner 11-14): the same */
        uint32_t g = wf_gen();                                      /* animation, then LiveView, without a load */
        load_and_close(id_of("SUN200"));
        CHECK(wf_gen() == g, "SET on the loaded film loaded it again");
    }
    /* .. ahead of the roll refusal: a roll with frames in the camera, its own film picked again animates, another
     * film is refused with the message */
    fake_dark_reset();
    {
        struct wf_roll *r = fake_roll();
        wf_roll_load(r, id_of("SUN200"), 12, 100, 205);
        r->nshot = 3;
        uint32_t g = wf_gen();
        open_ui();
        load_and_close(id_of("SUN200"));
        CHECK(wf_gen() == g && wf_roll_now()->nshot == 3, "the roll's own film: loaded again or the roll changed");
        open_ui();
        go_to("PRT160");
        CHECK(key(MODULE_KEY_PRESS_SET) == 0 && !wfui_host_anim() && !closed && wf_loaded() == id_of("SUN200"),
              "another film with a roll in the camera: refused, the browser stays open");
        close_ui(MODULE_KEY_MENU, 0);
    }
    fake_dark_reset();

    /* 2. Film LUT group, LV500T, focus on Film LUT (mockup_B2s_strip_lut) */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    fake_set_row(WF_ROW_LIGHT, 1);
    fake_set_row(WF_ROW_FLUT, 2);
    open_ui();
    go_to("LV500T");
    key(MODULE_KEY_PRESS_UP);                                       /* UP from Light: the top bar (11-17) .. */
    CHECK(wfui_host_focus() == FOC_TOP && wfui_host_tsub() == 0, "UP from Light not on the section pill (focus %d)",
          wfui_host_focus());
    key(MODULE_KEY_PRESS_UP);                                       /* .. then it wraps to the last row */
    CHECK(wfui_host_focus() == WF_NROWS - 1, "UP from the top bar not on the last row (Interlayer)");
    CHECK(wfui_host_rtop() == WF_NROWS - 7, "last row focused: the list shows its last 7 rows (top %d)", wfui_host_rtop());
    for (int i = WF_NROWS - 1; i > WF_ROW_FLUT; i--)
        key(MODULE_KEY_PRESS_UP);                                   /* up the seven film-stage knob rows */
    CHECK(wfui_host_focus() == WF_ROW_FLUT, "UP x %d from Light not on Film LUT", WF_NROWS - WF_ROW_FLUT + 1);
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
    CHECK(wfui_host_focus() == WF_ROW_HALO && !strcmp(wf_row_name(WF_ROW_HALO), "Halation"), "DOWN x4 not on Halation");
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
    CHECK(wfui_host_focus() == FOC_TOP && wfui_host_rtop() == WF_NROWS - 7, "DOWN from the last row: the top bar "
          "(focus %d top %d)", wfui_host_focus(), wfui_host_rtop());
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_LIGHT && wfui_host_rtop() == 0, "DOWN from the top bar wraps to Light (top %d)",
          wfui_host_rtop());
    for (int i = 0; i < 4; i++) key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == WF_NROWS - 3 && wfui_host_rtop() == WF_NROWS - 7, "UP x4 from Light: focus %d top %d",
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

    /* 4b. the film tables not read because the worker is not up yet (owner RAWB8: "worker 5"): no "Not on card" */
    fake_reset();
    fake_set_card(card);
    fake_set_missing(id_of("PRT160"));
    fake_set_films_state(0);
    open_ui();
    go_to("PRT160");
    shot("b2s_starting");
    fake_log_clear();
    key(MODULE_KEY_PRESS_SET);
    CHECK(wf_loaded() == 0 && fake_log_has("ui: load"), "a film whose table is not read yet was loaded");
    shot("b2s_starting_set");
    tick(150);
    fake_set_films_state(-1);
    fake_set_status("Worker failed");
    shot("b2s_noworker");
    close_ui(MODULE_KEY_MENU, 0);

    /* 4c. the rows follow the film under the cursor, before SET (owner 11-16): a row greyed for that film does not step
       there, and steps again on the loaded film */
    fake_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    fake_set_film_grey(id_of("SUN200"), 1 << WF_ROW_SIIE);
    open_ui();
    go_to("SUN200");
    for (int i = 0; i < WF_ROW_SIIE; i++) key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_SIIE, "focus on Interlayer (%d)", wfui_host_focus());
    int iv = fake_row_value(WF_ROW_SIIE);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(fake_row_value(WF_ROW_SIIE) == iv, "Interlayer stepped on a film it is greyed for (%d -> %d)", iv,
          fake_row_value(WF_ROW_SIIE));
    shot("b2s_rows_by_film");
    go_to("PRT160");
    CHECK(wfui_host_focus() == WF_ROW_SIIE, "focus kept on Interlayer (%d)", wfui_host_focus());
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(fake_row_value(WF_ROW_SIIE) == iv + 1, "Interlayer did not step on the loaded film (%d -> %d)", iv,
          fake_row_value(WF_ROW_SIIE));
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
    key(MODULE_KEY_PRESS_DOWN);                                     /* past Paper (11-16) */
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_focus() == WF_ROW_GRAIN && fake_row_value(WF_ROW_GRAIN) == 3, "5D2 edit: DOWN x3 + RIGHT: focus %d grain %d",
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

    /* 5h. the ML menu takes the screen while the browser is up (owner RAWB7: "the LiveView screen paints over" the menu
     * a two-finger tap opened; the core's 'F' opens it over a browser that did not beat, or it comes another way): the
     * close never gives Canon its front buffer under the menu (Canon's LiveView GUI would paint over it) and leaves the
     * menu's pixels alone; keys pass on meanwhile; once the menu has gone, everything is given back and Canon redraws */
    fake_reset();
    fake_set_card(card);
    host_gui_set(0, 1, 1);
    open_ui();
    {
        int e0 = host_under_enable(), r0 = host_redraws();
        host_menu(1, 3000, 0);                                      /* open now, closed by the user 3 s later */
        tick(1);
        CHECK(closed, "the browser did not end under the ML menu");
        wfui_task_end();
        CHECK(host_under_enable() == e0, "the front buffer was given back under the ML menu (%d times)", host_under_enable() - e0);
        CHECK(host_menu_px_kept() == 1, "the ML menu's pixels were cleared under it (%d)", host_menu_px_kept());
        CHECK(host_redraws() > r0, "no redraw once the ML menu went");
        given_back_checks();
    }
    /* .. and the unload does not wait for the ML menu (wf_ui_close: the close gives the screen back at once) */
    fake_reset();
    fake_set_card(card);
    open_ui();
    host_menu(1, -1, 0);
    tick(1);
    CHECK(closed, "the browser did not end under the ML menu (2)");
    wf_ui_close();
    wfui_task_end();
    CHECK(!wf_ui_is_open() && !host_front_disabled(), "an unload under the ML menu did not give the screen back");
    host_menu(0, -1, 0);
    CHECK(host_live_allocs() == 0, "%ld allocations left (unload under the ML menu)", host_live_allocs());

    /* 5i. an open while the ML menu stays up (gui_stop_menu does not close it): the browser never takes the screen */
    fake_reset();
    fake_set_card(card);
    host_menu(1, -1, 1);
    closed = 0;
    {
        int d0 = host_under_disable(), e0 = host_under_enable();
        CHECK(wf_ui_open() == 0, "open under the ML menu");
        wfui_task_begin();
        CHECK(!host_front_disabled() && host_under_disable() == d0, "the browser took the screen over the ML menu");
        tick(1);
        CHECK(closed, "an open under the ML menu did not end");
        wfui_task_end();
        CHECK(host_under_enable() == e0, "the front buffer was given back under the ML menu (open refused)");
        CHECK(!wf_ui_is_open() && host_live_allocs() == 0, "the refused open left %ld allocations", host_live_allocs());
    }
    host_menu(0, -1, 0);

    /* 5j. EOS M: the ML menu on Canon's plain LiveView (owner's RAWB7 KEYTRACE: after Canon's own MENU the first ask for
     * the LV Set dialog did not take, the menu ran on mode 0 until ML's 2 s ask): the guard asks for 93 150 ms after
     * the menu opened, then every 300 ms, up to 8 times; nothing when the ask took, outside LV, recording, with the
     * browser up or on the 5D2; an ask that lands after the menu closed is asked back to 0 */
    {
        int first, n, r0;
        fake_reset();
        host_gui_set(0, 1, 1);
        mg_run(100, 0);                                             /* no menu: nothing */
        CHECK(host_gui_requests() == 0, "the guard asked without the ML menu (%d)", host_gui_requests());
        /* the ask takes (the usual case): ML's own request only */
        fake_log_clear();
        host_menu(1, -1, 0);
        SetGUIRequestMode(93);                                      /* piggyback_canon_menu */
        n = mg_run(2500, 0);
        CHECK(n == 0 && host_gui_requests() == 1 && host_gui_mode() == 93 && !fake_log_has("menu: "),
              "the guard asked although Canon's dialog came (%d asks, %d requests, mode %d)", n, host_gui_requests(),
              host_gui_mode());
        host_menu(0, -1, 0);
        SetGUIRequestMode(0);                                       /* menu_close */
        n = mg_run(1200, 0);
        CHECK(n == 0 && host_gui_mode() == 0, "the guard acted after a plain close (%d asks)", n);
        /* the owner's case: ML's ask and our first two do not take, the third does */
        host_gui_set(0, 1, 1);
        host_gui_drop(3);
        fake_log_clear();
        host_menu(1, -1, 0);
        SetGUIRequestMode(93);
        n = mg_run(2500, &first);
        CHECK(n == 3 && first >= 150 && first < 200 && host_gui_mode() == 93,
              "guard: %d asks (want 3), the first at %d ms (want 150), mode %d", n, first, host_gui_mode());
        CHECK(fake_log_has("menu: canon dialog 0 -> asked 93 (try 1)") && fake_log_has("menu: canon dialog 0 -> asked 93 (try 3)")
              && !fake_log_has("(try 4)") && fake_log_has("menu: canon dialog 93 after try 3, "), "guard log lines");
        r0 = host_gui_requests();
        host_menu(0, -1, 0);
        SetGUIRequestMode(0);
        n = mg_run(1200, 0);
        CHECK(n == 0 && host_gui_requests() == r0 + 1 && host_gui_mode() == 0, "the guard acted after its dialog came and"
              " the menu closed (%d asks)", n);
        /* Canon never takes it: 8 tries, then ML's own 2 s ask is left to it */
        host_gui_set(0, 1, 1);
        host_gui_drop(100);
        fake_log_clear();
        host_menu(1, -1, 0);
        n = mg_run(4000, 0);
        CHECK(n == 8 && fake_log_has("menu: canon dialog still 0 after 8 tries"), "guard: %d tries (want 8)", n);
        host_menu(0, -1, 0);
        n = mg_run(1200, 0);
        CHECK(n == 0, "the guard asked 0 with no 93 in sight (%d)", n);
        /* the menu closes before our ask lands (ML closes it on mode 0): Canon's dialog is asked back to 0 */
        host_gui_set(0, 1, 1);
        host_gui_drop(2);
        fake_log_clear();
        host_menu(1, 470, 0);                                       /* try 2 at 450 ms lands at 510, the menu went at 470 */
        SetGUIRequestMode(93);
        n = mg_run(1500, 0);
        CHECK(n == 3 && host_gui_last_req() == 0 && host_gui_mode() == 0
              && fake_log_has("menu: closed before canon dialog 93 came: asked 0"),
              "a late 93 after the close: %d asks, last request %d, mode %d", n, host_gui_last_req(), host_gui_mode());
        /* not ours: outside LV, recording, the browser up, the 5D2 */
        for (int c = 0; c < 4; c++)
        {
            host_gui_set(0, 1, 1);
            host_gui_drop(100);
            if (c == 0) lv = 0;
            if (c == 1) host_recording = 1;
            if (c == 2) open_ui();
            if (c == 3) fake_set_cam(WF_CAM_5D2);
            host_menu(1, -1, 0);
            n = mg_run(1000, 0);
            CHECK(n == 0, "the guard asked in case %d (%d)", c, n);
            host_menu(0, -1, 0);
            mg_run(100, 0);
            lv = 1;
            host_recording = 0;
            if (c == 2) close_ui(MODULE_KEY_PRESS_HALFSHUTTER, 1);
            fake_set_cam(WF_CAM_EOSM);
        }
        host_gui_set(0, 1, 1);
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

    /* 7. one screen (owner 11-17): the mode chip at the top right (JPEG / RAW develop / Darkroom), the Exposures row and
     * a roll loaded from the browser, leaving Darkroom with a roll never loses a frame (asks, or sets the roll aside),
     * a finished roll asks before close-and-load; the 5D2 keeps the film chip */
    fake_reset();
    fake_dark_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    CHECK(wfui_host_chip() && wfui_host_mode() == 0 && wf_rawb == 0, "EOS M: the chip on JPEG (mode %d)", wfui_host_mode());
    /* the Light row (round 2): Auto = Auto WB inside our develop (the camera's WB is never changed) */
    CHECK(wfui_host_focus() == WF_ROW_LIGHT && !strcmp(wf_row_text(WF_ROW_LIGHT), "Auto"), "EOS M Light: '%s' (want "
          "Auto)", wf_row_text(WF_ROW_LIGHT));
    shot("one_light_auto");
    key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == FOC_TOP && wfui_host_tsub() == 0, "UP from Light: the top bar, the pill (focus %d)",
          wfui_host_focus());
    key(MODULE_KEY_PRESS_RIGHT);                                    /* round 2: LEFT / RIGHT pick the pill or the chip */
    CHECK(wfui_host_tsub() == 1 && wfui_host_mode() == 0, "RIGHT on the top bar: the chip, still JPEG (mode %d)",
          wfui_host_mode());
    shot("one_jpeg_chip");
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_tsub() == 1 && wfui_host_mode() == 0, "RIGHT on the chip: nothing more");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_mode() == 1 && wf_rawb == 1 && wfui_host_sel_id() == id_of("PRT160"),
          "SET on the chip: RAW develop (wf_rawb %d), the film stays", wf_rawb);
    shot("one_rawb");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_mode() == 2 && wf_rawb == 0 && !wfui_host_anim(), "SET on the chip: Darkroom, RAW develop off");
    key(MODULE_KEY_WHEEL_LEFT);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_mode() == 0, "the ring left x2: JPEG again (mode %d)", wfui_host_mode());
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_mode() == 2, "the ring left: Darkroom (wraps, mode %d)", wfui_host_mode());
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(wfui_host_tsub() == 0 && wfui_host_mode() == 2 && wfui_host_sec() == 0, "LEFT: the pill, nothing else changed");
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == FOC_EXP, "DOWN: the Exposures row (focus %d)", wfui_host_focus());
    for (int i = 0; i < 6 && wfui_host_exp() != 24; i++) key(MODULE_KEY_WHEEL_RIGHT);   /* (the last count is kept) */
    CHECK(wfui_host_exp() == 24, "the ring: 24 (%d)", wfui_host_exp());
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_exp() == 12, "the ring left: 12 (%d)", wfui_host_exp());
    key(MODULE_KEY_WHEEL_RIGHT);
    key(MODULE_KEY_WHEEL_RIGHT);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfui_host_exp() == 3, "the ring right past 36: 3 (%d)", wfui_host_exp());
    key(MODULE_KEY_WHEEL_LEFT);
    key(MODULE_KEY_WHEEL_LEFT);
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == WF_ROW_LIGHT, "DOWN from Exposures: Light (%d)", wfui_host_focus());
    key(MODULE_KEY_PRESS_UP);
    shot("one_dark_noroll");
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && fake_req_film() == id_of("PRT160") && fake_req_exp() == 24
          && wf_roll_now()->state == WF_RS_LOADED && wfui_host_anim(), "SET: a roll of 24 PRT160 (asked %d x%d)",
          fake_req_film(), fake_req_exp());
    tick(80);
    CHECK(closed, "not closed after the roll's load animation");
    end_checks();
    {
        struct wf_roll *r = fake_roll();
        r->nshot = 5;
        for (int i = 0; i < 5; i++) { r->folder[i] = 100; r->file[i] = 201 + i; r->fst[i] = WF_RF_SHOT; }
    }
    open_ui();
    CHECK(wfui_host_mode() == 2 && wfui_host_exp() == 24 && wfui_host_focus() == WF_ROW_LIGHT,
          "a roll with frames: Darkroom, its 24 (mode %d)", wfui_host_mode());
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_exp() == 24 && strstr(wfui_host_msg(), "keeps its count"), "the roll's count stays ('%s')",
          wfui_host_msg());
    key(MODULE_KEY_PRESS_DOWN);
    tick(150);                                                      /* the message times out */
    shot("one_dark_roll");
    go_to("SUN200");
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && !wfui_host_anim() && !closed && strstr(wfui_host_msg(), "PLAY develops")
          && wf_roll_now()->film == id_of("PRT160"), "another film while the roll shoots: refused ('%s')", wfui_host_msg());
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == FOC_TOP, "UP x2 from Light: the top bar (%d)", wfui_host_focus());
    key(MODULE_KEY_PRESS_RIGHT);
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_leave() == 0 && wfui_host_mode() == 2 && wf_roll_now()->state == WF_RS_LOADED,
          "leaving Darkroom with 5 frames: the question (for %d), nothing changed yet", wfui_host_leave());
    tick(150);
    shot("one_leave");
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(wfui_host_leave() == 0, "LEFT under the question: nothing");
    key(MODULE_KEY_MENU);
    CHECK(wfui_host_leave() < 0 && wfui_host_mode() == 2 && !closed && wf_roll_now()->nshot == 5
          && wf_roll_now()->state == WF_RS_LOADED, "MENU: the roll kept, still Darkroom, the browser open");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_leave() == 1, "the ring left: the question for RAW develop (%d)", wfui_host_leave());
    key(MODULE_KEY_PRESS_SET);
    CHECK(wf_roll_now()->state == WF_RS_REWOUND && wf_roll_shelved() && wf_roll_left(wf_roll_now()) == 5
          && wfui_host_mode() == 1 && wf_rawb == 1 && strstr(wfui_host_msg(), "5 frames wait"),
          "SET: rewound, set aside with its 5 frames, RAW develop ('%s')", wfui_host_msg());
    shot("one_left_rawb");
    key(MODULE_KEY_PRESS_DOWN);                                     /* off the chip, onto Light */
    key(MODULE_KEY_PRESS_SET);
    CHECK(!wfui_host_anim() && wf_loaded() == id_of("PRT160") && strstr(wfui_host_msg(), "film stays"),
          "set aside: another film refused, its film stays ('%s')", wfui_host_msg());
    chip_step(1);
    CHECK(wfui_host_mode() == 2 && !wf_roll_shelved() && wf_rawb == 0, "back to Darkroom: off the shelf (mode %d)",
          wfui_host_mode());
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_swap() && !wfui_host_anim(), "a rewound roll, SET on another film: close and load? (%d)", wfui_host_swap());
    tick(150);                                                      /* the last message times out */
    shot("one_swap");
    key(MODULE_KEY_MENU);
    CHECK(!wfui_host_swap() && !closed && fake_nswap() == 0, "MENU: no close-and-load");
    chip_step(1);
    CHECK(wfui_host_leave() < 0 && wf_roll_shelved() && wfui_host_mode() == 0 && wf_rawb == 0,
          "a finished roll: set aside at once, no question, JPEG (mode %d)", wfui_host_mode());
    close_ui(MODULE_KEY_MENU, 0);
    fake_dark_reset();
    fake_roll()->num = 7;
    {
        struct wf_roll *r = fake_roll();
        wf_roll_load(r, id_of("PRT160"), 12, 100, 300);
    }
    open_ui();
    chip_step(-1);
    CHECK(wfui_host_leave() < 0 && !wf_roll_open(wf_roll_now()) && wfui_host_mode() == 1 && strstr(wfui_host_msg(), "no frame"),
          "an empty roll: taken out, RAW develop ('%s')", wfui_host_msg());
    close_ui(MODULE_KEY_MENU, 0);
    /* .. the way to a new roll (owner 11-17: the Roll picker screen is gone): the end-of-roll notice's INFO (and the ML
     * menu's Film roll) open the browser in Darkroom mode on the Exposures row, the roll's film under the cursor; a full
     * roll leaves the row to the next roll, SET on a film asks to close it and load that count */
    fake_dark_reset();
    {
        struct wf_roll *r = fake_roll();
        wf_roll_load(r, id_of("PRT160"), 24, 100, 300);
        r->num = 8;
        for (int i = 0; i < 24; i++) { r->folder[i] = 100; r->file[i] = 301 + i; r->fst[i] = WF_RF_SHOT; }
        r->nshot = 24;
        r->state = WF_RS_FULL;
    }
    fake_set_loaded(id_of("PRT160"));                               /* the roll's film is the loaded one */
    closed = 0;
    wf_ui_open_scr(WF_UI_NEW_ROLL);
    wfui_task_begin();
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && wfui_host_mode() == 2 && wfui_host_focus() == FOC_EXP && !wfui_host_nr()
          && wfui_host_sel_id() == id_of("PRT160") && wfui_host_exp() == 24, "from the notice: screen %d mode %d focus %d "
          "film %d x%d (want the browser, Darkroom, Exposures, PRT160 x24)", wfui_host_scr(), wfui_host_mode(),
          wfui_host_focus(), wfui_host_sel_id(), wfui_host_exp());
    shot("one_from_notice");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_exp() == 12, "a full roll: the row is the next roll's (12: %d)", wfui_host_exp());
    go_to("SUN200");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_swap() && !wfui_host_anim() && wf_roll_now()->state == WF_RS_FULL, "the full roll, SET on SUN200: "
          "close and load? (%d)", wfui_host_swap());
    shot("shelf_swap");                                             /* owner 11-17: it goes on the shelf */
    int sw_n = wf_roll_now()->nshot, sw_num = wf_roll_now()->num;
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_nswap() == 1 && fake_req_film() == id_of("SUN200") && fake_req_exp() == 12 && wfui_host_anim(),
          "SET: closed, a roll of 12 SUN200 (swaps %d, %d x%d)", fake_nswap(), fake_req_film(), fake_req_exp());
    CHECK(wf_shelf_now()->n == 1 && wf_shelf_now()->e[0].num == sw_num && wf_shelf_now()->e[0].left == sw_n
          && wf_shelf_now()->e[0].state == WF_RS_REWOUND && !fake_nswap_devd(), "the old roll on the shelf: %d rolls, "
          "%d with %d to develop (want %d, %d)", wf_shelf_now()->n, wf_shelf_now()->e[0].num, wf_shelf_now()->e[0].left,
          sw_num, sw_n);
    tick(80);
    CHECK(closed, "not closed after the new roll's animation");
    end_checks();
    /* .. while open (the notice cannot show then, the ML menu can): the request switches the open browser */
    fake_dark_reset();
    open_ui();
    to_top();
    key(MODULE_KEY_PRESS_RIGHT);
    for (int i = 0; i < 3 && wfui_host_mode() != 0; i++) key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfui_host_mode() == 0, "JPEG before the request (mode %d)", wfui_host_mode());
    wf_ui_open_scr(WF_UI_NEW_ROLL);
    tick(1);
    CHECK(wfui_host_mode() == 2 && wfui_host_focus() == FOC_EXP && !wfui_host_nr(), "the request while open: mode %d "
          "focus %d", wfui_host_mode(), wfui_host_focus());
    close_ui(MODULE_KEY_MENU, 0);
    fake_dark_reset();
    fake_reset();
    fake_set_cam(WF_CAM_5D2);
    fake_set_card(card);
    open_ui();
    CHECK(!strcmp(wf_row_text(WF_ROW_LIGHT), "Auto"), "5D2 Light: '%s' (no Darkroom: Auto, the camera's WB)",
          wf_row_text(WF_ROW_LIGHT));
    key(MODULE_KEY_PRESS_UP);
    CHECK(!wfui_host_chip() && wfui_host_focus() == FOC_TOP, "5D2: no chip, UP from Light: the section pill (%d)",
          wfui_host_focus());
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_tsub() == 0, "5D2: RIGHT on the top bar: no chip to go to");
    key(MODULE_KEY_JOY_CENTER);
    CHECK(wfui_host_sec() == 1 && !wfui_host_edit() && wfui_host_focus() == FOC_TOP, "5D2: the joystick press on the "
          "pill: Film stage (section %d, edit %d)", wfui_host_sec(), wfui_host_edit());
    CHECK(!wfui_host_rows(), "5D2 Film stage: no Exposures row (no rolls)");
    shot("fs_5d2_pill");
    key(MODULE_KEY_INFO);                                           /* the 5D2's Info page: its own key names */
    CHECK(wfui_host_info() && wfui_host_info_y() + 17 <= 361, "5D2 Film stage: the Info page (last row y %d)",
          wfui_host_info_y());
    shot("fs_5d2_info");
    key(MODULE_KEY_INFO);
    CHECK(!wfui_host_info(), "5D2: INFO again closes it");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_sec() == 0, "5D2: SET on the pill: Film again");
    key(MODULE_KEY_PRESS_UP);
    CHECK(wfui_host_focus() == WF_NROWS - 1, "5D2: UP from the top bar wraps to the last row (%d)", wfui_host_focus());
    close_ui(MODULE_KEY_MENU, 0);
    closed = 0;                                                     /* 5D2, the ML menu's Film roll: no rolls there */
    wf_ui_open_scr(WF_UI_NEW_ROLL);
    wfui_task_begin();
    tick(1);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && !wfui_host_chip() && !wfui_host_nr()
          && strstr(wfui_host_msg(), "EOS M first"), "5D2 Film roll: the browser says why ('%s')", wfui_host_msg());
    close_ui(MODULE_KEY_MENU, 0);
    fake_set_cam(WF_CAM_EOSM);
    fake_reset();

    /* 8. Film | Film stage (owner 11-17, round 2): the section pill at the top left (UP to the top bar, LEFT / RIGHT pill
     * or chip, SET toggles, DOWN back to the rows); each section's tabs (TAP cycles inside it); Film stage: JPEG 8-bit /
     * RAW develop / Darkroom, the Exposures row and a roll in every mode, the chip changes how the next frames develop;
     * leaving Film stage with a roll asks (SET closes: rewound, set aside; MENU keeps); the EXIF lines; INFO in both */
    {
        static const int want_n[9] = { 15, 4, 11, 23, 4, 15, 15, 9, 1 };
        int n[9] = { 0 };
        for (int i = 0; i < wf_nfilms; i++)
            if (wf_films[i].flags & WF_F_READY)
            {
                int t = wfui_host_tab_of(wf_films[i].id);
                if (t >= 0 && t < 9) n[t]++;
            }
        for (int t = 0; t < 9; t++)
            CHECK(n[t] == want_n[t], "tab %d: %d films (want %d: Colour 15, Cine 4, B&W 11, Film LUT 23 | Vision3 4, "
                  "Vision 15, ECN/EXR 15, Fuji 9, B&W 1)", t, n[t], want_n[t]);
    }
    fake_reset();
    fake_dark_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    to_top();                                                       /* the chip keeps its mode between opens: JPEG */
    key(MODULE_KEY_PRESS_RIGHT);
    for (int i = 0; i < 3 && wfui_host_mode() != 0; i++) key(MODULE_KEY_WHEEL_RIGHT);
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_sec() == 0 && !wfui_host_rows() && wfui_host_mode() == 0 && wfui_host_focus() == WF_ROW_LIGHT,
          "Film, JPEG: no Exposures row, DOWN from the top bar on Light (focus %d)", wfui_host_focus());
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(wfui_host_focus() == FOC_TOP && wfui_host_tsub() == 0, "UP from Light, LEFT: the pill");
    shot("fs_pill_focus");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_sec() == 1 && wfui_host_tab() == WF_GRP_STAGE && wfui_host_focus() == FOC_TOP && wfui_host_rows()
          && wfui_host_mode() == 0, "SET on the pill: Film stage, its first tab (Vision3), the Exposures row, JPEG 8-bit "
          "(section %d tab %d rows %d mode %d)", wfui_host_sec(), wfui_host_tab(), wfui_host_rows(), wfui_host_mode());
    shot("fs_pill_stage");
    {
        int t0 = wfui_host_tab(), seen = 0;
        for (int i = 0; i < 5; i++)
        {
            key(MODULE_KEY_Q);
            CHECK(wfui_host_sec() == 1, "TAP left Film stage (tab %d)", wfui_host_tab());
            seen |= 1 << (wfui_host_tab() - WF_GRP_STAGE);
        }
        CHECK(wfui_host_tab() == t0 && seen == 31, "5 TAPs: the 5 Film stage tabs and back (seen 0x%x)", seen);
    }
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_tsub() == 1 && wfui_host_mode() == 0, "RIGHT: the chip, JPEG 8-bit");
    shot("fs_jpeg8_chip");
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfui_host_focus() == FOC_EXP, "Film stage, JPEG 8-bit: DOWN from the top bar on Exposures (%d)", wfui_host_focus());
    for (int i = 0; i < 6 && wfui_host_exp() != 24; i++) key(MODULE_KEY_WHEEL_RIGHT);   /* (the last count is kept) */
    key(MODULE_KEY_PRESS_DOWN);
    go_to("G08");
    fake_set_row(WF_ROW_PRINT, 4);                                  /* 2383 */
    fake_set_row(WF_ROW_SPUSH, 4);
    CHECK(key(MODULE_KEY_PRESS_SET) == 0 && fake_req_film() == id_of("G08") && fake_req_exp() == 24 && wf_roll_look8 == 1
          && wf_rawb == 0 && wf_roll_now()->state == WF_RS_LOADED && wfui_host_anim(), "Film stage JPEG 8-bit, SET: a roll "
          "of 24 G08 with the look at the shot (asked %d x%d, look8 %d)", fake_req_film(), fake_req_exp(), wf_roll_look8);
    tick(80);
    CHECK(closed, "not closed after the Film stage roll's animation");
    end_checks();
    {
        struct wf_roll *r = fake_roll();
        r->nshot = 7;
        for (int i = 0; i < 7; i++) { r->folder[i] = 100; r->file[i] = 401 + i; r->fst[i] = WF_RF_SHOT; }
    }
    open_ui();
    CHECK(wfui_host_sec() == 1 && wfui_host_sel_id() == id_of("G08") && wfui_host_mode() == 0 && wfui_host_exp() == 24,
          "reopen with the roll: Film stage, G08, JPEG 8-bit, 24 (section %d mode %d)", wfui_host_sec(), wfui_host_mode());
    chip_step(1);
    CHECK(wfui_host_mode() == 1 && wf_rawb == 1 && wf_roll_look8 == 0 && wfui_host_leave() < 0
          && wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->nshot == 7 && strstr(wfui_host_msg(), "goes on"),
          "the chip with a Film stage roll: RAW develop at once, the roll goes on ('%s')", wfui_host_msg());
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    tick(150);                                                      /* the message times out */
    shot("fs_rawdev_roll");
    chip_step(1);
    CHECK(wfui_host_mode() == 2 && wf_rawb == 0 && wf_roll_look8 == 0 && wf_roll_now()->state == WF_RS_LOADED,
          "the chip: Darkroom, the roll goes on (mode %d)", wfui_host_mode());
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    tick(150);
    shot("fs_darkroom");
    chip_step(1);
    CHECK(wfui_host_mode() == 0 && wf_roll_look8 == 1 && wf_rawb == 0, "the chip: JPEG 8-bit again, the look at the shot");
    fake_set_dev(31240, WF_PATH_RAW);
    key(MODULE_KEY_PRESS_DOWN);
    tick(150);                                                      /* the message times out */
    key(MODULE_KEY_INFO);
    CHECK(wfui_host_info(), "INFO: the Info page (Film stage)");
    shot("fs_info_stage");
    CHECK(wfui_host_info_y() >= 136 && wfui_host_info_y() + 17 <= 361,
          "Info page (Film stage): 13 grid rows below the description, the last (y %d) above the key help's rule",
          wfui_host_info_y());
    key(MODULE_KEY_INFO);
    CHECK(!wfui_host_info(), "INFO again closes the Info page");
    /* leaving Film stage with 7 frames of a JPEG 8-bit roll: the question says they are developed; MENU keeps the roll;
     * SET ends it (developed at the shot: closed, not set aside for the Darkroom) */
    to_top();
    key(MODULE_KEY_PRESS_LEFT);
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() == 0 && wfui_host_sec() == 1 && wf_roll_now()->state == WF_RS_LOADED,
          "the pill with 7 frames: the question (for %d), nothing changed yet", wfui_host_secq());
    tick(150);
    shot("fs_leave_stage");
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfui_host_secq() == 0 && wfui_host_tsub() == 0, "RIGHT under the question: nothing");
    key(MODULE_KEY_MENU);
    CHECK(wfui_host_secq() < 0 && wfui_host_sec() == 1 && !closed && wf_roll_now()->state == WF_RS_LOADED
          && wf_roll_now()->nshot == 7, "MENU: the roll kept, still Film stage, the browser open");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() == 0, "SET on the pill again: the question");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() < 0 && wfui_host_sec() == 0 && !wf_roll_open(wf_roll_now()) && !wf_roll_shelved()
          && strstr(wfui_host_msg(), "Roll 001 ended: 7 frames developed"),
          "SET on a JPEG 8-bit roll: it ends (developed at the shot), Film ('%s')", wfui_host_msg());
    fake_set_row(WF_ROW_PRINT, 0);                                  /* (the fake's rows are one set: Scan for Portra) */
    shot("fs_ended_jpeg8");
    fake_set_dev(1949, WF_PATH_JPEG);
    key(MODULE_KEY_PRESS_DOWN);
    tick(150);
    key(MODULE_KEY_INFO);
    CHECK(wfui_host_info(), "INFO: the Info page (Film)");
    shot("fs_info_film");
    CHECK(wfui_host_info_y() >= 136 && wfui_host_info_y() + 17 <= 361, "Info page (Film): the grid's last row y %d",
          wfui_host_info_y());
    key(MODULE_KEY_INFO);
    close_ui(MODULE_KEY_MENU, 0);
    /* a Film stage tab with the new cans (the Kodak / Eastman yellow, Fuji green, Double-X white cassettes) */
    fake_reset();
    fake_dark_reset();
    fake_set_card(card);
    fake_set_loaded(id_of("PRT160"));
    open_ui();
    go_to("G08");
    fake_set_row(WF_ROW_PRINT, 4);
    shot("fs_stage_tab_cans");
    for (int i = 0; i < 5 && wfui_host_tab() != WF_GRP_STAGE + 3; i++) key(MODULE_KEY_Q);
    CHECK(wfui_host_tab() == WF_GRP_STAGE + 3, "TAP to the Fuji tab (%d)", wfui_host_tab());
    shot("fs_stage_tab_fuji");
    key(MODULE_KEY_INFO);                                           /* a Fuji film's Info page: its facts line */
    CHECK(wfui_host_info(), "INFO on a Fuji film");
    shot("fs_info_stage_fuji");
    key(MODULE_KEY_INFO);
    CHECK(!wfui_host_info(), "INFO again closes it");
    /* an empty Film stage roll comes out at the pill; a finished one is set aside at once */
    key(MODULE_KEY_PRESS_SET);
    CHECK(wf_roll_now()->state == WF_RS_LOADED && wf_roll_look8 == 1 && wfui_host_anim(), "SET on a Fuji can: a roll");
    tick(80);
    end_checks();
    open_ui();
    to_top();
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() < 0 && wfui_host_sec() == 0 && !wf_roll_open(wf_roll_now()) && strstr(wfui_host_msg(), "no frame"),
          "an empty Film stage roll: taken out at the pill ('%s')", wfui_host_msg());
    close_ui(MODULE_KEY_MENU, 0);
    fake_dark_reset();
    {
        struct wf_roll *r = fake_roll();
        wf_roll_load(r, id_of("G08"), 12, 100, 500);
        r->num = 9;
        for (int i = 0; i < 12; i++) { r->folder[i] = 100; r->file[i] = 501 + i; r->fst[i] = WF_RF_SHOT; }
        r->nshot = 12;
        r->state = WF_RS_FULL;
    }
    open_ui();
    CHECK(wfui_host_sec() == 1 && wfui_host_sel_id() == id_of("G08"), "open with a Film stage roll: Film stage, its film");
    to_top();
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() < 0 && wfui_host_sec() == 0 && wf_roll_shelved() && strstr(wfui_host_msg(), "PLAY develops"),
          "a finished Film stage roll: set aside at once, Film ('%s')", wfui_host_msg());
    close_ui(MODULE_KEY_MENU, 0);
    /* .. the same finished roll shot in JPEG 8-bit: the close-and-load question says its frames are developed, the
     * pill ends it */
    fake_dark_reset();
    {
        struct wf_roll *r = fake_roll();
        wf_roll_load(r, id_of("G08"), 12, 100, 500);
        r->num = 9;
        for (int i = 0; i < 12; i++) { r->folder[i] = 100; r->file[i] = 501 + i; r->fst[i] = WF_RF_SHOT; }
        r->nshot = 12;
        r->state = WF_RS_FULL;
    }
    wf_roll_look8 = 1;
    open_ui();
    CHECK(wfui_host_sec() == 1 && wfui_host_mode() == 0, "a finished JPEG 8-bit roll: Film stage, JPEG 8-bit (mode %d)",
          wfui_host_mode());
    go_to("G09");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_swap() && !wfui_host_anim(), "a finished JPEG 8-bit roll, SET on G09: close and load? (%d)",
          wfui_host_swap());
    shot("fs_swap_jpeg8");
    key(MODULE_KEY_MENU);
    CHECK(!wfui_host_swap() && !closed, "MENU: no close-and-load");
    to_top();
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() < 0 && wfui_host_sec() == 0 && !wf_roll_open(wf_roll_now()) && !wf_roll_shelved()
          && strstr(wfui_host_msg(), "Roll 009 ended: 12 frames developed"),
          "a finished JPEG 8-bit roll at the pill: it ends ('%s')", wfui_host_msg());
    close_ui(MODULE_KEY_MENU, 0);
    /* .. a Film stage roll in Darkroom mode with frames: the question keeps the Darkroom words, SET sets it aside */
    fake_dark_reset();
    {
        struct wf_roll *r = fake_roll();
        wf_roll_load(r, id_of("G08"), 12, 100, 600);
        r->num = 10;
        for (int i = 0; i < 3; i++) { r->folder[i] = 100; r->file[i] = 601 + i; r->fst[i] = WF_RF_SHOT; }
        r->nshot = 3;
    }
    open_ui();
    CHECK(wfui_host_sec() == 1 && wfui_host_mode() == 2, "a Darkroom Film stage roll: Darkroom (mode %d)", wfui_host_mode());
    to_top();
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_secq() == 0, "the pill with 3 Darkroom frames: the question (%d)", wfui_host_secq());
    shot("fs_leave_stage_dark");
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_sec() == 0 && wf_roll_now()->state == WF_RS_REWOUND && wf_roll_shelved()
          && wf_roll_left(wf_roll_now()) == 3 && strstr(wfui_host_msg(), "3 frames wait for the Darkroom"),
          "SET on a Darkroom roll: rewound, set aside with its 3 frames ('%s')", wfui_host_msg());
    close_ui(MODULE_KEY_MENU, 0);
    fake_dark_reset();
    fake_reset();

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
