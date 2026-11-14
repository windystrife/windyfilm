/* dr_ui_test.c - host harness for the Roll + Darkroom screens (src/ui/wf_dark_ui.c, docs/DESIGN.md 3.5): scripted
 * keys against the fake core + fake roll / darkroom (fake_dark.c), checks of what the keys must reach, screenshots.
 *
 *   dr_ui_test OUTDIR CARD_DIR RBF_DIR THUMB_DIR
 * CARD_DIR holds ML/DATA/WFFONTS.DAT + WFCANS.DAT; THUMB_DIR print_<k>.l8 (288 x 192 luma) and wire_<k>.l8 (72 x 48)
 * for k = 0..7 (tests/ui/dr_mkthumbs.py). Screens: OUTDIR/<name>.ppm; the HUD: OUTDIR/hud_<name>.idx (720 x 480 ML
 * colour indices, 0 = LiveView shows through) for dr_mkthumbs.py --hud to put over a LiveView frame. */
#include "host_ml.h"
#include "wf_ui_int.h"
#include "fake_core.h"
#include "fake_dark.h"

int wfdr_host_tab(void);
int wfdr_host_exp(void);
int wfdr_host_sel_id(void);
int wfui_host_anim(void);

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
static void key(int k) { wf_ui_key(k); tick(1); }

static void shot(const char *name)
{
    char p[700];
    snprintf(p, sizeof(p), "%s/%s.ppm", outdir, name);
    tick(1);
    int stray = host_dump(p);
    CHECK(stray == 0, "%s: %d pixels outside the UI's palette entries", name, stray);
    printf("shot %s\n", p);
}

static void open_scr(int s)
{
    closed = 0;
    wf_ui_open_scr(s);
    wfui_task_begin();
    tick(1);
}

static void close_ui(void)
{
    key(MODULE_KEY_MENU);
    tick(2);
    CHECK(closed, "MENU did not close");
    wfui_task_end();
    CHECK(!wf_ui_is_open(), "still open");
    CHECK(host_palette_is_canon(), "palette not given back");
    CHECK(host_live_allocs() == 0, "%ld allocations left", host_live_allocs());
}

static int first_of(int group, int slide)
{
    for (int i = 0; i < wf_nfilms; i++)
        if (wf_films[i].group == group && (wf_films[i].flags & WF_F_READY) && !!(wf_films[i].flags & WF_F_SLIDE) == slide)
            return wf_films[i].id;
    return 0;
}

static const char *thumbs;
static void prints(int print_k, int nwire)
{
    char p[600];
    snprintf(p, sizeof p, "%s/print_%d.l8", thumbs, print_k);
    CHECK(fake_load_print(p) == 0, "no %s", p);
    for (int i = 0; i < nwire; i++)
    {
        snprintf(p, sizeof p, "%s/wire_%d.l8", thumbs, i);
        CHECK(fake_load_wire(i, p) == 0, "no %s", p);
    }
}

/* a roll of n frames shot from IMG_0206 */
static void roll_of(int film, int exp, int n, int state)
{
    struct wf_roll *r = fake_roll();
    wf_roll_reset(r);
    wf_roll_load(r, film, exp, 100, 205);
    r->num = 21;
    for (int i = 0; i < n; i++) { r->folder[i] = 100; r->file[i] = 206 + i; r->fst[i] = WF_RF_SHOT; }
    r->nshot = n;
    r->state = state;
}

static uint8_t hv[480 * 720];
#define MLC_W COLOR_WHITE                                   /* ML's white index in the HUD dump */
static void hud_dump(const char *name, int blink, int mode, int keep)   /* keep: draw over the last dump (CLEAR) */
{
    uint8_t *v = hv;
    if (!keep) memset(v, 0, sizeof hv);
    wfdr_hud_fonts(1);
    wfdr_hud(v, 720, blink, mode);
    char p[700];
    snprintf(p, sizeof p, "%s/hud_%s.idx", outdir, name);
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(v, 1, sizeof hv, f); fclose(f); }
    printf("hud %s\n", p);
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: dr_ui_test OUTDIR CARD RBFDIR THUMBDIR\n"); return 2; }
    outdir = argv[1];
    thumbs = argv[4];
    host_init(argv[3], 0x03FF0000u, 0x03000000u);
    wf_ui_init();
    fake_reset();
    fake_set_card(argv[2]);
    fake_dark_reset();
    int colour = first_of(WF_GRP_COLOUR, 0), slide = first_of(WF_GRP_COLOUR, 1), bw = first_of(WF_GRP_BW, 0);
    CHECK(colour && bw, "catalogue without colour / B&W films");

    /* 1. roll picker, no roll: Colour shelf, the loaded film in the centre */
    fake_set_loaded(colour);
    open_scr(WFUI_SCR_ROLL);
    CHECK(wfdr_host_sel_id() == colour, "picker does not start on the loaded film");
    key(MODULE_KEY_PRESS_RIGHT);
    key(MODULE_KEY_PRESS_RIGHT);
    key(MODULE_KEY_PRESS_RIGHT);
    shot("roll_colour");
    int e0 = wfdr_host_exp();
    key(MODULE_KEY_PRESS_UP);
    CHECK(wfdr_host_exp() != e0, "UP did not change the exposures");
    key(MODULE_KEY_Q);
    if (slide) CHECK(wfdr_host_tab() == 1, "Q did not go to SLIDE (%d)", wfdr_host_tab());
    key(MODULE_KEY_PRESS_RIGHT);
    shot("roll_slide");
    key(MODULE_KEY_TOUCH_1_FINGER);
    key(MODULE_KEY_PRESS_RIGHT);
    key(MODULE_KEY_PRESS_RIGHT);
    shot("roll_bw");
    int want = wfdr_host_sel_id(), we = wfdr_host_exp();
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_req_film() == want && fake_req_exp() == we, "SET did not load the roll (%d %d)", fake_req_film(), fake_req_exp());
    CHECK(wf_roll_now()->state == WF_RS_LOADED, "roll not loaded");
    /* as a film load (owner 11-14): the load animation, then the UI closes (LiveView) */
    CHECK(wfui_host_anim() && !closed, "SET on a roll: no load animation");
    tick(10);
    shot("roll_anim_wind");
    tick(30);
    shot("roll_anim_loaded");
    tick(40);
    CHECK(closed, "the roll's load animation did not close the UI");
    wfui_task_end();
    CHECK(!wf_ui_is_open() && host_palette_is_canon() && host_live_allocs() == 0, "after the roll's animation: open %d, "
          "palette %d, %ld allocations", wf_ui_is_open(), host_palette_is_canon(), host_live_allocs());

    /* 2. picker with a roll on the camera, 5 of 12 shot: SET says develop first */
    roll_of(bw, 12, 5, WF_RS_LOADED);
    open_scr(WFUI_SCR_ROLL);
    CHECK(wfdr_host_sel_id() == bw, "picker does not start on the roll's film");
    key(MODULE_KEY_PRESS_RIGHT);
    int before = fake_req_film();
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_req_film() == before, "a roll with frames was replaced");
    shot("roll_loaded_msg");
    close_ui();

    /* 3. darkroom: ready */
    roll_of(bw, 12, 12, WF_RS_FULL);
    fake_view()->state = WF_DR_IDLE;
    fake_view()->battery = 76;
    open_scr(WFUI_SCR_DARK);
    shot("dark_ready");
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_nstart() == 1, "SET did not start the darkroom");

    /* 4. frame 1 loading: blank paper */
    struct wf_dark_view *v = fake_view();
    struct wf_roll *r = fake_roll();
    r->state = WF_RS_REWOUND;
    v->state = WF_DR_RUN; v->phase = WF_DP_LOAD; v->frame = 0; v->done = 0; v->total = 12; v->left = 12;
    v->prog = 60; v->expose = 0; v->t_frame = 4000; v->print_ready = 0; v->nwire = 0;
    shot("dark_load");

    /* 5. frame 2 exposing: preview fading in, frame 1 on the wire */
    prints(1, 1);
    v = fake_view();
    r = fake_roll();
    r->fst[0] = WF_RF_DEV;
    v->frame = 1; v->done = 1; v->left = 11; v->avg_ms = 41000; v->phase = WF_DP_EXPOSE; v->print_ready = 1;
    v->nwire = 1; v->wire_frame[0] = 0; v->hung_ms = get_ms_clock() - 100000;
    v->expose = 180; v->prog = 230; v->t_frame = 11000;
    shot("dark_expose_18");
    v = fake_view(); v->expose = 520; v->prog = 480; v->t_frame = 21000;
    shot("dark_expose_52");
    v = fake_view(); v->expose = 900; v->prog = 760; v->t_frame = 32000;
    shot("dark_expose_90");

    /* 6. frame 4: fixer, three prints dry */
    prints(3, 3);
    v = fake_view();
    r = fake_roll();
    r->fst[1] = r->fst[2] = WF_RF_DEV;
    v->frame = 3; v->done = 3; v->left = 9; v->phase = WF_DP_FIX; v->print_ready = 2; v->expose = 1000;
    v->prog = 880; v->t_frame = 38000; v->nwire = 3; v->wire_frame[1] = 1; v->wire_frame[2] = 2;
    shot("dark_fix");

    /* 7. frame 4 hung: the print slides up to the wire */
    prints(3, 4);
    v = fake_view();
    r = fake_roll();
    r->fst[3] = WF_RF_DEV;
    v->frame = 4; v->done = 4; v->left = 8; v->phase = WF_DP_LOAD; v->print_ready = 0; v->expose = 0; v->prog = 10;
    v->t_frame = 300; v->nwire = 4; v->wire_frame[3] = 3; v->hung_ms = get_ms_clock() - 220;
    shot("dark_slide_a");
    tick(10);
    shot("dark_slide_b");

    /* 8. MENU once: confirm; MENU again: stop */
    key(MODULE_KEY_MENU);
    CHECK(!closed && fake_nstop() == 0, "first MENU stopped / closed");
    shot("dark_stop_confirm");
    key(MODULE_KEY_MENU);
    CHECK(fake_nstop() == 1, "second MENU did not stop");

    /* 9. done: eight prints on the wire */
    prints(7, 8);
    v = fake_view();
    r = fake_roll();
    for (int i = 0; i < 12; i++) r->fst[i] = WF_RF_DEV;
    r->state = WF_RS_DONE;
    v->state = WF_DR_DONE; v->done = 12; v->left = 0; v->frame = -1; v->nwire = 8; v->print_ready = 2;
    for (int i = 0; i < 8; i++) v->wire_frame[i] = 4 + i;
    v->hung_ms = get_ms_clock() - 100000;
    tick(40);
    shot("dark_done");
    close_ui();

    /* 10. HUD over LiveView */
    roll_of(bw, 12, 5, WF_RS_LOADED);
    hud_dump("5of12_wind", 1, WF_HUD_FULL, 0);
    hud_dump("5of12_fade", 0, WF_HUD_FADE, 0);
    hud_dump("5of12", 0, WF_HUD_FULL, 0);
    hud_dump("5of12_small", 0, WF_HUD_CLEAR, 1);   /* the info's 2.5 s are over: only the small counter is left */
    int left = 0, inside = 0;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 720; x++)
            if (hv[y * 720 + x]) { if (y >= 398 && y <= 416 && x >= 620 && x <= 690) inside++; else left++; }
    CHECK(left == 0 && inside > 200, "CLEAR after FULL: %d pixels left outside the small counter (%d in it)", left, inside);
    roll_of(colour, 36, 36, WF_RS_FULL);
    hud_dump("full", 0, WF_HUD_FULL, 0);
    hud_dump("full_small", 0, WF_HUD_CLEAR, 1);
    roll_of(colour, 36, 7, WF_RS_REWOUND);
    hud_dump("rewound", 0, WF_HUD_FULL, 0);
    wfdr_hud_fonts(0);
    CHECK(host_live_allocs() == 0, "HUD fonts freed: %ld allocations left", host_live_allocs());

    /* 11. the font pack shared by the HUD and the UI (wff_open counts its users): the UI's open finds it, its close
     * leaves it to the HUD, the HUD's close frees it */
    wfdr_hud_fonts(1);
    long a0 = host_live_allocs();
    CHECK(a0 > 0, "HUD fonts: no pack held");
    roll_of(bw, 12, 5, WF_RS_LOADED);
    open_scr(WFUI_SCR_ROLL);
    key(MODULE_KEY_MENU);
    tick(2);
    CHECK(closed, "MENU did not close (shared pack)");
    wfui_task_end();
    CHECK(host_live_allocs() == a0, "the UI's close took the HUD's pack (%ld -> %ld allocations)", a0, host_live_allocs());
    hud_dump("after_ui", 0, WF_HUD_FULL, 0);
    int px = 0;
    for (int i = 0; i < 480 * 720; i++) px += hv[i] == MLC_W;
    CHECK(px > 100, "HUD text after the UI closed: %d white pixels", px);
    wfdr_hud_fonts(0);
    CHECK(host_live_allocs() == 0, "the HUD's close: %ld allocations left", host_live_allocs());

    printf("peak UI heap %ld bytes\n", host_peak_bytes());
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
