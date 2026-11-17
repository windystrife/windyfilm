/* dr_ui_test.c - host harness for the Darkroom screen, the roll's HUD and end notice (src/ui/wf_dark_ui.c, docs/DESIGN.md
 * 3.6) and the way to a roll in the browser's Darkroom mode (src/ui/wf_ui.c, DESIGN 3.12; owner 11-17: the Roll picker
 * screen is gone): scripted keys against the fake core + fake roll / darkroom (fake_dark.c), checks of what the keys must
 * reach, screenshots.
 *
 *   dr_ui_test OUTDIR CARD_DIR RBF_DIR THUMB_DIR
 * CARD_DIR holds ML/DATA/WFFONTS.DAT + WFCANS.DAT; THUMB_DIR print_<k>.l8 (288 x 192 luma) and wire_<k>.l8 (72 x 48)
 * for k = 0..7 (tests/ui/dr_mkthumbs.py). Screens: OUTDIR/<name>.ppm; the HUD: OUTDIR/hud_<name>.idx (720 x 480 ML
 * colour indices, 0 = LiveView shows through) for dr_mkthumbs.py --hud to put over a LiveView frame. */
#include "host_ml.h"
#include "wf_ui_int.h"
#include "fake_core.h"
#include "fake_dark.h"

int wfui_host_anim(void);
int wfui_host_scr(void);
const char *wfui_host_msg(void);
void wfui_host_beat(int ph, int ago_ms);
int wfui_host_sel_id(void);
int wfui_host_focus(void);
int wfui_host_mode(void);
int wfui_host_exp(void);
int wfui_host_swap(void);
int wfui_host_nr(void);
const char *wfdr_host_msg(void);
int wfdr_host_pfoc(void);
int wfdr_host_stop_ask(void);
int wfdr_host_sel(void);
int wfdr_host_afilm(void);
int wfui_host_pick(void);
#define FOC_EXP (-1)                                        /* wf_ui.c: the focus on the Exposures row */
#define M_DARK  2                                           /* wfui_host_mode: Darkroom */

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

static void open_scr(int s)                                 /* s: WF_UI_* of wf_dark.h (0 = the browser) */
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

/* a shelf roll (owner 11-17): wf_shelf_now's entries, newest first as the core lists them */
static void shelf_ent(int num, int dev, int film, int own, int exp, int state, int nshot, int left, int ndev)
{
    struct wf_shelf *s = fake_shelf();
    struct wf_shelf_ent *e = &s->e[s->n++];
    e->num = num; e->dev = dev; e->film = film; e->own = own; e->exp = exp; e->state = state;
    e->nshot = nshot; e->left = left; e->ndev = ndev;
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

/* the end-of-roll notice over LiveView: hud < 0 = the notice alone, else the HUD in that mode first (as wf_dark.c) */
#define NT_BOX(x, y) ((x) >= 96 && (x) <= 624 && (y) >= 118 && (y) <= 336)
static void nt_dump(const char *name, int mode, int hud, int keep)
{
    uint8_t *v = hv;
    if (!keep) memset(v, 0, sizeof hv);
    wfdr_hud_fonts(1);
    if (hud >= 0) wfdr_hud(v, 720, 0, hud);
    wfdr_notice(v, 720, mode);
    char p[700];
    snprintf(p, sizeof p, "%s/hud_%s.idx", outdir, name);
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(v, 1, sizeof hv, f); fclose(f); }
    printf("hud %s\n", p);
}
/* the quick keys' box (owner 11-17, DESIGN 3.17): hud < 0 = the box alone, else the roll HUD in that mode first */
static void to_dump(const char *name, const char *big, const char *k1, const char *t1, const char *k2, const char *t2,
                    int mode, int hud)
{
    memset(hv, 0, sizeof hv);
    wfdr_toast(0, 0, 0, 0, 0, 0, 0, WF_HUD_CLEAR);          /* (a fresh screen: no last box to give back) */
    if (hud >= 0) { wfdr_hud_fonts(1); wfdr_hud(hv, 720, 0, hud); }
    wfdr_toast(hv, 720, big, k1, t1, k2, t2, mode);
    char p[700];
    snprintf(p, sizeof p, "%s/hud_%s.idx", outdir, name);
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(hv, 1, sizeof hv, f); fclose(f); }
    printf("hud %s\n", p);
}
static int nt_count(int in, int c)                          /* pixels of colour c (-1 any) inside / outside the box */
{
    int n = 0;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 720; x++)
            if (!NT_BOX(x, y) == !in && (c < 0 ? hv[y * 720 + x] != 0 : hv[y * 720 + x] == c)) n++;
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: dr_ui_test OUTDIR CARD RBFDIR THUMBDIR\n"); return 2; }
    outdir = argv[1];
    thumbs = argv[4];
    host_init(argv[3], 0x03FF0000u, 0x03000000u);
    wf_ui_init();
    fake_reset();
    const char *cam = getenv("WF_UI_CAM");      /* dr_run.sh CAM=5D2: the same screens with the 5D2's keys (DESIGN 3.18) */
    if (cam && !strcmp(cam, "5D2"))
        fake_set_cam(WF_CAM_5D2);
    fake_set_card(argv[2]);
    fake_dark_reset();
    int colour = first_of(WF_GRP_COLOUR, 0), bw = first_of(WF_GRP_BW, 0);
    CHECK(colour && bw, "catalogue without colour / B&W films");

    /* 1. a new roll (owner 11-17: the Roll picker screen is gone): the ML menu's Film roll opens the browser in Darkroom
     * mode on the Exposures row, the loaded film under the cursor; the wheel steps the count, SET loads the roll */
    fake_set_loaded(colour);
    open_scr(WF_UI_NEW_ROLL);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && wfui_host_mode() == M_DARK && wfui_host_focus() == FOC_EXP
          && !wfui_host_nr(), "Film roll: screen %d mode %d focus %d (want the browser, Darkroom, Exposures)",
          wfui_host_scr(), wfui_host_mode(), wfui_host_focus());
    CHECK(wfui_host_sel_id() == colour, "the browser does not start on the loaded film (%d)", wfui_host_sel_id());
    shot("roll_new");
    int e0 = wfui_host_exp();
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfui_host_exp() != e0, "the wheel did not change the exposures (%d)", e0);
    key(MODULE_KEY_PRESS_RIGHT);
    key(MODULE_KEY_PRESS_RIGHT);
    int want = wfui_host_sel_id(), we = wfui_host_exp();
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

    /* 2. a roll on the camera, 5 of 12 shot: SET on another film says develop first */
    roll_of(bw, 12, 5, WF_RS_LOADED);
    open_scr(WF_UI_NEW_ROLL);
    CHECK(wfui_host_sel_id() == bw && wfui_host_exp() == 12, "the browser not on the roll's film / count (%d x%d)",
          wfui_host_sel_id(), wfui_host_exp());
    key(MODULE_KEY_PRESS_RIGHT);
    int before = fake_req_film();
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_req_film() == before && strstr(wfui_host_msg(), "PLAY"), "a roll with frames was replaced ('%s')",
          wfui_host_msg());
    shot("roll_loaded_msg");
    /* .. the roll's own film and count picked again (owner 11-14): the load animation, then LiveView, no new load */
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(wfui_host_sel_id() == bw && wfui_host_exp() == 12, "LEFT not back on the roll's film (%d x%d)",
          wfui_host_sel_id(), wfui_host_exp());
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_anim() && !closed && fake_req_film() == before && wf_roll_now()->nshot == 5
          && wf_roll_now()->state == WF_RS_LOADED, "the roll picked again: anim %d, request %d, %d frames",
          wfui_host_anim(), fake_req_film(), wf_roll_now()->nshot);
    tick(80);
    CHECK(closed, "the roll picked again did not close the UI");
    wfui_task_end();
    CHECK(!wf_ui_is_open() && host_live_allocs() == 0, "after the roll picked again: open %d, %ld allocations",
          wf_ui_is_open(), host_live_allocs());
    /* .. a full roll's own film: the close-and-load question (it takes no frame, owner 11-15) */
    roll_of(bw, 12, 12, WF_RS_FULL);
    open_scr(WF_UI_NEW_ROLL);
    key(MODULE_KEY_PRESS_SET);
    CHECK(!wfui_host_anim() && !closed && wfui_host_swap(), "a full roll picked again: anim %d, question %d (want "
          "the close-and-load question, owner 11-15)", wfui_host_anim(), wfui_host_swap());
    key(MODULE_KEY_MENU);                                    /* no: the roll stays */
    CHECK(!wfui_host_swap() && !closed, "MENU on the question: question %d, closed %d", wfui_host_swap(), closed);
    close_ui();

    /* 3. darkroom: ready */
    roll_of(bw, 12, 12, WF_RS_FULL);
    fake_view()->state = WF_DR_IDLE;
    fake_view()->battery = 76;
    open_scr(WF_UI_SCR_DARK);
    shot("dark_ready");
    /* 3b. the print finishes (owner 11-15, DESIGN 3.9): Frame / Info / Date rows; UP / DOWN pick, LEFT / RIGHT / wheel */
    CHECK(wfdr_host_pfoc() == 0 && wf_pr_frame == 0 && wf_pr_info == 1 && wf_pr_date == 0, "finishes not at their "
          "defaults: focus %d frame %d info %d date %d", wfdr_host_pfoc(), wf_pr_frame, wf_pr_info, wf_pr_date);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wf_pr_frame == 1, "RIGHT on Frame: %d (want White)", wf_pr_frame);
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfdr_host_pfoc() == 1 && wf_pr_info == 2, "Info: focus %d value %d (want On + roll)", wfdr_host_pfoc(), wf_pr_info);
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfdr_host_pfoc() == 2 && wf_pr_date == 1, "Date: focus %d value %d (want '26 11 15)", wfdr_host_pfoc(), wf_pr_date);
    CHECK(fake_nstart() == 0 && !closed, "the finish rows started or closed something");
    shot("dark_print_white_info_date");
    key(MODULE_KEY_PRESS_DOWN);                              /* Camera JPEG (owner 11-16): Keep / Overwrite / Over + print */
    CHECK(wfdr_host_pfoc() == 3 && wf_pr_jpeg == 0, "DOWN from Date: focus %d jpeg %d (want Camera JPEG, Keep)",
          wfdr_host_pfoc(), wf_pr_jpeg);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wf_pr_jpeg == 1, "RIGHT on Camera JPEG: %d (want Overwrite)", wf_pr_jpeg);
    shot("dark_print_jpeg_overwrite");
    key(MODULE_KEY_PRESS_RIGHT);                             /* (owner 11-17: the picture and the framed print in DCIM) */
    CHECK(wf_pr_jpeg == 2, "RIGHT again on Camera JPEG: %d (want Over + print)", wf_pr_jpeg);
    shot("dark_print_jpeg_both");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wf_pr_jpeg == 1, "wheel left on Camera JPEG: %d (want Overwrite)", wf_pr_jpeg);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wf_pr_jpeg == 0, "wheel left on Camera JPEG: %d (want Keep)", wf_pr_jpeg);
    key(MODULE_KEY_PRESS_DOWN);                              /* the Screen chip at the top right (owner 11-16) */
    CHECK(wfdr_host_pfoc() == WF_PO_N && wf_dk_scr == WF_DS_ON, "DOWN from Camera JPEG: focus %d screen %d (want the "
          "Screen chip, On)", wfdr_host_pfoc(), wf_dk_scr);
    shot("dark_screen_chip_on");
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wf_dk_scr == WF_DS_OFF && wf_pr_jpeg == 0 && fake_nstart() == 0, "RIGHT on the chip: screen %d jpeg %d "
          "started %d (want Off, nothing else)", wf_dk_scr, wf_pr_jpeg, fake_nstart());
    shot("dark_screen_off");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wf_dk_scr == WF_DS_ON, "wheel left on the chip: %d (want On)", wf_dk_scr);
    key(MODULE_KEY_PRESS_DOWN);                              /* the ROLL row (owner 11-17, the shelf) under the chip */
    CHECK(wfdr_host_pfoc() == WF_PO_N + 1, "DOWN from the chip: focus %d (want the ROLL row, wraps)", wfdr_host_pfoc());
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wfdr_host_sel() == 21 && fake_nstart() == 0 && wf_pr_frame == 1, "RIGHT on the ROLL row with one roll: roll %d "
          "started %d frame %d (want 21, nothing else: frame White)", wfdr_host_sel(), fake_nstart(), wf_pr_frame);
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfdr_host_pfoc() == 0, "DOWN from the ROLL row: focus %d (want Frame)", wfdr_host_pfoc());
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    CHECK(wfdr_host_pfoc() == WF_PO_N, "UP UP from Frame: focus %d (want the chip)", wfdr_host_pfoc());
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_DOWN);
    key(MODULE_KEY_PRESS_LEFT);
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(wf_pr_frame == 4, "LEFT LEFT from White: %d (want Black wide, wraps)", wf_pr_frame);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfdr_host_pfoc() == 2 && wf_pr_date == 0, "UP to Date, wheel left: focus %d date %d", wfdr_host_pfoc(), wf_pr_date);
    shot("dark_print_black_wide");
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_UP);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(wf_pr_frame == 0, "Frame back to Off: %d", wf_pr_frame);
    key(MODULE_KEY_PRESS_DOWN);
    shot("dark_print_info_greyed");                          /* frame Off: Info greyed */
    key(MODULE_KEY_PRESS_LEFT);                              /* On + roll -> On: the defaults again */
    CHECK(wf_pr_frame == 0 && wf_pr_info == 1 && wf_pr_date == 0, "finishes back to defaults: %d %d %d", wf_pr_frame,
          wf_pr_info, wf_pr_date);
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_nstart() == 1, "SET did not start the darkroom");
    CHECK(wf_pr_frame == 0 && wf_pr_date == 0, "SET changed the finishes");

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
    /* 5b. Screen Off while it develops (owner 11-16): dark = no redraws; a key only wakes it (MENU too); awake, LEFT /
     * RIGHT switch the chip (the run's only control) */
    wf_dk_scr = WF_DS_OFF;
    fake_set_dark(1);
    tick(2);
    CHECK(wfdr_dark(WFUI_SCR_DARK), "Screen Off run, display dark: the UI still draws");
    key(MODULE_KEY_MENU);
    CHECK(fake_nwake() == 1 && !wfdr_dark(WFUI_SCR_DARK) && !wfdr_host_stop_ask() && fake_nstop() == 0 && !closed,
          "MENU on the dark screen: woken %d, dark %d, confirm %d, stop %d (want a wake only)", fake_nwake(),
          wfdr_dark(WFUI_SCR_DARK), wfdr_host_stop_ask(), fake_nstop());
    shot("dark_run_screen_off_wake");
    fake_set_dark(1);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(fake_nwake() == 2 && wf_dk_scr == WF_DS_OFF, "RIGHT on the dark screen: woken %d, screen %d (want a wake, "
          "still Off)", fake_nwake(), wf_dk_scr);
    key(MODULE_KEY_PRESS_RIGHT);
    CHECK(fake_nwake() == 2 && wf_dk_scr == WF_DS_ON, "RIGHT awake during the run: screen %d (want On)", wf_dk_scr);
    shot("dark_run_screen_on");
    {
        int k0 = fake_nkeep();
        tick(2);
        CHECK(fake_nkeep() >= k0 + 2, "Screen On run: the tick keeps the display on (%d keep-alive calls in 2 ticks)",
              fake_nkeep() - k0);
    }
    fake_set_dark(0);
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

    /* 10b. the quick keys' box (owner 11-17, DESIGN 3.17): the approval shots; clear of the roll HUD (its widest: a full
     * 36 roll) and of the end-of-roll notice; CLEAR gives every pixel back */
    CHECK(wfdr_toast_fonts(1) == 1, "the quick keys' box holds the font pack");
    to_dump("toast_iso", "ISO 400", "RING", "ISO", "DOWN", "Aperture", WF_HUD_FULL, -1);
    to_dump("toast_iso_fade", "ISO 400", "RING", "ISO", "DOWN", "Aperture", WF_HUD_FADE, -1);
    to_dump("toast_av", "f/2.8", "RING", "Aperture", "DOWN", "Done", WF_HUD_FULL, -1);
    to_dump("toast_lr", "ISO 800", "LEFT", "Lower", "RIGHT", "Higher", WF_HUD_FULL, -1);
    to_dump("toast_mz", "MAGIC ZOOM", "SET", "Off", 0, 0, WF_HUD_FULL, -1);
    to_dump("toast_mz_ml", "MAGIC ZOOM", 0, "Turn on ML Overlay > Magic Zoom first", 0, 0, WF_HUD_FULL, -1);
    to_dump("toast_iso_auto", "ISO AUTO", 0, "Turn up: ISO 100", 0, 0, WF_HUD_FULL, -1);
    roll_of(bw, 12, 5, WF_RS_LOADED);
    to_dump("toast_iso_roll", "ISO 400", "RING", "ISO", "DOWN", "Aperture", WF_HUD_FULL, WF_HUD_FULL);
    to_dump("toast_mz_roll", "MAGIC ZOOM", "SET", "Off", 0, 0, WF_HUD_FULL, WF_HUD_SMALL);
    {
        static uint8_t hud_only[480 * 720];
        roll_of(colour, 36, 36, WF_RS_FULL);
        memset(hv, 0, sizeof hv);
        wfdr_hud(hv, 720, 0, WF_HUD_FULL);
        memcpy(hud_only, hv, sizeof hv);
        const char *wide[][5] = { { "ISO 12800", "RING", "ISO", "DOWN", "Aperture" },
                                  { "MAGIC ZOOM", 0, "Turn on ML Overlay > Global Draw first", 0, 0 },
                                  { "ISO 400", 0, "The highest the camera takes", 0, 0 } };
        for (int k = 0; k < 3; k++)
        {
            memset(hv, 0, sizeof hv);
            wfdr_toast(0, 0, 0, 0, 0, 0, 0, WF_HUD_CLEAR);
            wfdr_toast(hv, 720, wide[k][0], wide[k][1], wide[k][2], wide[k][3], wide[k][4], WF_HUD_FULL);
            int tn = 0, both = 0, innt = 0;
            for (int i = 0; i < 480 * 720; i++)
                if (hv[i]) { tn++; if (hud_only[i]) both++; if (NT_BOX(i % 720, i / 720)) innt++; }
            CHECK(tn > 1500 && both == 0 && innt == 0, "the box '%s': %d pixels, %d on the roll HUD's, %d in the notice's box",
                  wide[k][0], tn, both, innt);
            wfdr_toast(hv, 720, 0, 0, 0, 0, 0, WF_HUD_CLEAR);
            tn = 0;
            for (int i = 0; i < 480 * 720; i++) if (hv[i]) tn++;
            CHECK(tn == 0, "CLEAR gives every pixel of the box back (%d left)", tn);
        }
        memset(hv, 0, sizeof hv);                           /* a new text first gives the last box back */
        wfdr_toast(0, 0, 0, 0, 0, 0, 0, WF_HUD_CLEAR);
        wfdr_toast(hv, 720, "ZOOM OFF", 0, 0, 0, 0, WF_HUD_FULL);
        memcpy(hud_only, hv, sizeof hv);
        memset(hv, 0, sizeof hv);
        wfdr_toast(0, 0, 0, 0, 0, 0, 0, WF_HUD_CLEAR);
        wfdr_toast(hv, 720, "MAGIC ZOOM", 0, "Turn on ML Overlay > Magic Zoom first", 0, 0, WF_HUD_FULL);
        wfdr_toast(hv, 720, "ZOOM OFF", 0, 0, 0, 0, WF_HUD_FULL);
        CHECK(!memcmp(hv, hud_only, sizeof hv), "a narrower box over a wide one: the same as alone (nothing of the wide left)");
    }
    CHECK(wfdr_toast_fonts(0) == 0, "the box lets the font pack go");
    wfdr_hud_fonts(0);
    CHECK(host_live_allocs() == 0, "HUD fonts freed: %ld allocations left", host_live_allocs());

    /* 11. the font pack shared by the HUD and the UI (wff_open counts its users): the UI's open finds it, its close
     * leaves it to the HUD, the HUD's close frees it */
    wfdr_hud_fonts(1);
    long a0 = host_live_allocs();
    CHECK(a0 > 0, "HUD fonts: no pack held");
    roll_of(bw, 12, 5, WF_RS_LOADED);
    open_scr(WF_UI_NEW_ROLL);
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

    /* 12. an open that ends before its own wff_open (the ML menu stays up) must not give back the HUD's use of the pack */
    wfdr_hud_fonts(1);
    a0 = host_live_allocs();
    host_menu(1, -1, 1);
    open_scr(0);
    CHECK(closed, "an open under the ML menu did not end");
    wfui_task_end();
    host_menu(0, -1, 0);
    CHECK(host_live_allocs() == a0, "a refused open took the HUD's pack (%ld -> %ld allocations)", a0, host_live_allocs());
    hud_dump("after_refused", 0, WF_HUD_FULL, 0);
    px = 0;
    for (int i = 0; i < 480 * 720; i++) px += hv[i] == MLC_W;
    CHECK(px > 100, "HUD text after a refused open: %d white pixels", px);
    wfdr_hud_fonts(0);
    CHECK(host_live_allocs() == 0, "the HUD's close (12): %ld allocations left", host_live_allocs());

    /* 13. the end-of-roll notice (owner 11-15): a box in the middle, clear of the HUD's chip, counter and ladder */
    roll_of(bw, 12, 12, WF_RS_FULL);
    nt_dump("end_notice_alone", WF_HUD_FULL, -1, 0);
    int nr = nt_count(1, COLOR_RED), nw = nt_count(1, MLC_W), out = nt_count(0, -1), nall = nt_count(1, -1);
    CHECK(nr > 400 && nw > 400 && out == 0, "notice FULL: %d red, %d white in the box, %d pixels outside it", nr, nw, out);
    static uint8_t hud_only[480 * 720];
    hud_dump("end_hud_only", 0, WF_HUD_FULL, 0);
    memcpy(hud_only, hv, sizeof hv);
    nt_dump("end_notice_full", WF_HUD_FULL, WF_HUD_FULL, 0);
    int both = 0;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 720; x++)
            if (hud_only[y * 720 + x] && NT_BOX(x, y)) both++;
    CHECK(both == 0, "notice and HUD overlap on %d pixels", both);
    nt_dump("end_notice_full_fade", WF_HUD_FADE, WF_HUD_FADE, 0);
    int nf = nt_count(1, -1);
    CHECK(nf > nall / 3 && nf < nall * 6 / 10 && nt_count(1, COLOR_RED) == 0, "notice FADE: dimmed, dithered (%d of %d, red %d)",
          nf, nall, nt_count(1, COLOR_RED));
    nt_dump("end_notice_full", WF_HUD_FULL, WF_HUD_FULL, 0);
    nt_dump("end_notice_clear", WF_HUD_CLEAR, WF_HUD_CLEAR, 1);   /* its time is over: the box back to 0, HUD small */
    CHECK(nt_count(1, -1) == 0 && nt_count(0, -1) > 200, "notice CLEAR: %d pixels left in the box (%d outside: the HUD)",
          nt_count(1, -1), nt_count(0, -1));
    roll_of(colour, 36, 7, WF_RS_REWOUND);
    nt_dump("end_notice_rewound", WF_HUD_FULL, WF_HUD_FULL, 0);
    CHECK(nt_count(1, COLOR_RED) > 400, "notice REWOUND: %d red pixels", nt_count(1, COLOR_RED));
    roll_of(colour, 36, 36, WF_RS_FULL);
    nt_dump("end_notice_full36", WF_HUD_FULL, WF_HUD_FULL, 0);
    roll_of(bw, 12, 5, WF_RS_LOADED);                          /* a roll still shooting: nothing (only a CLEAR) */
    nt_dump("end_notice_loaded", WF_HUD_FULL, -1, 0);
    CHECK(nt_count(1, -1) == 0 && nt_count(0, -1) == 0, "notice on a loaded roll: %d pixels", nt_count(1, -1));
    wfdr_hud_fonts(0);
    CHECK(host_live_allocs() == 0, "the HUD's close (13): %ld allocations left", host_live_allocs());

    /* 14. owner 11-15: the Darkroom from inside the UI (PLAY, EOS M), the close-and-load question, the notice while the
     * shutter is held, rolls of 3 */
    /* .. browser in JPEG mode, no roll: as before (no entry, PLAY closes it). Darkroom mode is kept from the opens above
     * (the mode chip, owner 11-17): UP past Exposures to the top bar, RIGHT to the chip (round 2: LEFT / RIGHT pick the
     * section pill or the chip), SET = JPEG (no Exposures row), DOWN back to Light */
    fake_dark_reset();
    open_scr(0);
    if (wfui_host_mode() == M_DARK)
    {
        key(MODULE_KEY_PRESS_UP);
        key(MODULE_KEY_PRESS_UP);
        key(MODULE_KEY_PRESS_RIGHT);
        key(MODULE_KEY_PRESS_SET);
        key(MODULE_KEY_PRESS_DOWN);
    }
    CHECK(wfui_host_mode() == 0 && wfui_host_focus() == WF_ROW_LIGHT, "JPEG mode on Light (mode %d, focus %d)",
          wfui_host_mode(), wfui_host_focus());
    shot("dev_browser_noroll");
    key(MODULE_KEY_PLAY);
    tick(2);
    CHECK(closed, "browser PLAY with no roll: not closed (as before it closes)");
    wfui_task_end();
    CHECK(!wf_ui_is_open() && host_palette_is_canon() && host_live_allocs() == 0, "browser PLAY close: open %d, %ld allocations",
          wf_ui_is_open(), host_live_allocs());
    /* .. a loaded roll without a frame: the entry dim, PLAY says "No frames to develop" and stays */
    roll_of(bw, 12, 0, WF_RS_LOADED);
    open_scr(0);
    shot("dev_browser_dim");
    key(MODULE_KEY_PLAY);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && !closed && !strcmp(wfui_host_msg(), "No frames to develop"),
          "browser PLAY, no frame yet: '%s' (screen %d, closed %d)", wfui_host_msg(), wfui_host_scr(), closed);
    shot("dev_browser_dim_play");
    close_ui();
    /* .. 5 frames: bright, PLAY opens the Darkroom */
    roll_of(bw, 12, 5, WF_RS_LOADED);
    open_scr(0);
    shot("dev_browser_on");
    key(MODULE_KEY_PLAY);
    tick(2);
    CHECK(wfui_host_scr() == WFUI_SCR_DARK && !closed, "browser PLAY, 5 frames: screen %d (want the Darkroom)",
          wfui_host_scr());
    shot("dev_browser_to_dark");
    close_ui();
    /* .. Darkroom mode (the way to a new roll): PLAY into the Darkroom with frames; no roll: PLAY says why, it stays */
    roll_of(bw, 12, 5, WF_RS_LOADED);
    open_scr(WF_UI_NEW_ROLL);
    key(MODULE_KEY_PLAY);
    tick(2);
    CHECK(wfui_host_scr() == WFUI_SCR_DARK && !closed, "Darkroom mode PLAY, 5 frames: screen %d", wfui_host_scr());
    close_ui();
    fake_dark_reset();
    open_scr(WF_UI_NEW_ROLL);
    key(MODULE_KEY_PLAY);
    CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && !closed && !strcmp(wfui_host_msg(), "Load a roll first"),
          "Darkroom mode PLAY, no roll: '%s' (screen %d, closed %d)", wfui_host_msg(), wfui_host_scr(), closed);
    shot("dev_newroll_play");
    /* .. rolls of 3 and 6 (owner 11-15): the wheel reaches them on the Exposures row */
    for (int i = 0; i < 5 && wfui_host_exp() != 3; i++) key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_exp() == 3, "the wheel does not reach 3 exposures (%d)", wfui_host_exp());
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wfui_host_exp() == 6, "the wheel up from 3: %d (want 6)", wfui_host_exp());
    key(MODULE_KEY_WHEEL_LEFT);
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wfui_host_exp() == 36, "the wheel down from 3 wraps to 36: %d", wfui_host_exp());
    close_ui();
    /* .. a full roll: SET on a film asks; MENU keeps the roll, SET closes it and loads the new one */
    roll_of(bw, 12, 12, WF_RS_FULL);
    open_scr(WF_UI_NEW_ROLL);
    key(MODULE_KEY_PRESS_RIGHT);
    int nfilm = wfui_host_sel_id();
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_swap() && !closed && wf_roll_now()->state == WF_RS_FULL, "SET on a full roll: no question (%d)",
          wfui_host_swap());
    key(MODULE_KEY_PRESS_LEFT);
    CHECK(wfui_host_swap() && wfui_host_sel_id() == nfilm, "a key other than SET / MENU moved the browser under the question");
    key(MODULE_KEY_MENU);
    CHECK(!wfui_host_swap() && !closed && wf_roll_now()->state == WF_RS_FULL && fake_nswap() == 0,
          "MENU: the question gone, the roll kept, the browser open (closed %d, swaps %d)", closed, fake_nswap());
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_swap(), "SET again: the question again");
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_nswap() == 1 && wf_roll_now()->state == WF_RS_LOADED && fake_req_film() == nfilm && wfui_host_anim(),
          "SET: closed and loaded (%d swaps, film %d want %d, anim %d)", fake_nswap(), fake_req_film(), nfilm,
          wfui_host_anim());
    tick(80);
    CHECK(closed, "the new roll's load animation did not close the UI");
    wfui_task_end();
    CHECK(!wf_ui_is_open() && host_live_allocs() == 0, "after close-and-load: open %d, %ld allocations", wf_ui_is_open(),
          host_live_allocs());
    /* .. rewound with frames to develop: the same question */
    roll_of(colour, 36, 7, WF_RS_REWOUND);
    open_scr(WF_UI_NEW_ROLL);
    key(MODULE_KEY_PRESS_SET);
    CHECK(wfui_host_swap(), "SET on a rewound roll: no question");
    key(MODULE_KEY_MENU);
    CHECK(!wfui_host_swap() && !closed, "MENU on the question closed the UI");
    close_ui();
    /* .. the notice while the shutter is held (wf_dark.c locks it on a finished roll): inside its box, clear of the HUD */
    roll_of(bw, 12, 12, WF_RS_FULL);
    fake_set_locked(1);
    nt_dump("end_notice_locked_alone", WF_HUD_FULL, -1, 0);
    CHECK(nt_count(1, COLOR_RED) > 400 && nt_count(1, MLC_W) > 400 && nt_count(0, -1) == 0,
          "locked notice: %d red, %d white in the box, %d outside", nt_count(1, COLOR_RED), nt_count(1, MLC_W),
          nt_count(0, -1));
    nt_dump("end_notice_locked", WF_HUD_FULL, WF_HUD_FULL, 0);
    fake_set_locked(0);
    /* .. the HUD of a roll of 3 */
    roll_of(colour, 3, 2, WF_RS_LOADED);
    hud_dump("3_2of3", 0, WF_HUD_FULL, 0);
    roll_of(colour, 3, 3, WF_RS_FULL);
    hud_dump("3_full", 0, WF_HUD_FULL, 0);
    wfdr_hud_fonts(0);
    CHECK(host_live_allocs() == 0, "section 14: %ld allocations left", host_live_allocs());

    /* 15. owner RAWB9 (11-15): the Darkroom asked for from the UI with the UI task up (wf_task carries the run out, no
     * task of its own): SET starts it, the screen says it gets ready while the look's effects are built (frame -1,
     * WF_DP_MIX), then frame 1 loads. And the UI that opens while Canon holds the card after the roll's last frame (its
     * cans read: "no beat for 2212 / 7256 ms (state 1, phase 5)") keeps its keys up to 10 s there, 2 s elsewhere. */
    roll_of(colour, 3, 3, WF_RS_FULL);
    fake_view()->state = WF_DR_IDLE;
    fake_view()->battery = 70;
    fake_view()->nwire = 0;
    fake_view()->print_ready = 0;
    int ns0 = fake_nstart();
    open_scr(WF_UI_SCR_DARK);
    key(MODULE_KEY_PRESS_SET);
    CHECK(fake_nstart() == ns0 + 1 && !closed, "RAWB10: SET in the Darkroom asks for the run (%d)", fake_nstart() - ns0);
    v = fake_view();
    r = fake_roll();
    v->state = WF_DR_RUN; v->phase = WF_DP_MIX; v->frame = -1; v->done = 0; v->total = 3; v->left = 3;
    v->prog = 0; v->expose = 0; v->t_frame = 6000; v->print_ready = 0; v->nwire = 0; v->avg_ms = 0; v->gen++;
    shot("dark_ui_mix");
    CHECK(!closed, "RAWB10: the UI stays up while the run gets ready");
    v->phase = WF_DP_LOAD; v->frame = 0; v->prog = 80; v->t_frame = 3000; v->gen++;
    r->state = WF_RS_FULL;
    shot("dark_ui_frame1");
    v->state = WF_DR_IDLE; v->phase = WF_DP_NONE; v->frame = -1; v->gen++;
    close_ui();
    closed = 0;
    wf_ui_open_scr(WF_UI_SCR_DARK);                /* the UI task created, still in its open */
    wfui_host_beat(5, 7256);                       /* PH_CANS, held up 7.3 s (RAWB9 192.8 s) */
    CHECK(wf_ui_key(MODULE_KEY_PRESS_SET) == 0, "RAWB10: opening, 7.3 s in the cans read: SET still the UI's");
    wfui_host_beat(5, 10500);
    CHECK(wf_ui_key(MODULE_KEY_PRESS_SET) == 1, "RAWB10: opening, 10.5 s in the cans read: the key goes to Canon / ML");
    wfui_task_begin();                             /* the close asked for: it ends without taking the screen */
    wfui_task_end();
    closed = 0;
    wf_ui_open_scr(WF_UI_SCR_DARK);
    wfui_host_beat(3, 2300);                       /* PH_LIST: not a card read, the old 2 s limit */
    CHECK(wf_ui_key(MODULE_KEY_PRESS_SET) == 1, "RAWB10: opening, 2.3 s outside a pack read: the key goes on (2 s)");
    wfui_task_begin();
    wfui_task_end();
    CHECK(host_live_allocs() == 0, "section 15: %ld allocations left", host_live_allocs());

    /* 16. the shelf (owner 11-17): "next roll" no longer drops a roll; the Darkroom's ROLL row picks the camera's roll
     * or a shelf roll; a developed roll develops again with a film the browser picks (and comes back) */
    {
        int film3 = 0;
        for (int i = 0; i < wf_nfilms && !film3; i++)
            if (wf_films[i].group == WF_GRP_COLOUR && (wf_films[i].flags & WF_F_READY) && wf_films[i].id != colour
                && !(wf_films[i].flags & WF_F_SLIDE))
                film3 = wf_films[i].id;
        fake_dark_reset();
        roll_of(colour, 24, 5, WF_RS_LOADED);                       /* the camera: roll 024 shooting, 5 of 24 */
        fake_roll()->num = 24;
        shelf_ent(23, 1, bw, bw, 12, WF_RS_REWOUND, 12, 12, 0);     /* shelved with its 12 frames */
        shelf_ent(21, 1, colour, colour, 12, WF_RS_DONE, 12, 0, 12);   /* developed */
        shelf_ent(19, 2, film3, colour, 6, WF_RS_REWOUND, 6, 4, 2);   /* a develop 2, stopped with 4 left */
        fake_view()->state = WF_DR_IDLE;
        fake_view()->battery = 64;
        fake_view()->nwire = 0;
        fake_view()->print_ready = 0;
        int rs0 = fake_nrescan(), st0 = fake_nstart();
        open_scr(WF_UI_SCR_DARK);
        CHECK(fake_nrescan() == rs0 + 1 && wfdr_host_sel() == 24 && wfdr_host_pfoc() == 0, "the shelf: the Darkroom opens "
              "on the camera's roll (%d), asks for the card's list (%d)", wfdr_host_sel(), fake_nrescan() - rs0);
        key(MODULE_KEY_PRESS_UP);
        key(MODULE_KEY_PRESS_RIGHT);
        CHECK(wfdr_host_pfoc() == WF_PO_N + 1 && wfdr_host_sel() == 23, "UP, RIGHT: the ROLL row on roll 023 (focus %d, "
              "roll %d)", wfdr_host_pfoc(), wfdr_host_sel());
        shot("shelf_roll_left");
        key(MODULE_KEY_PRESS_SET);
        CHECK(fake_nstart() == st0 + 1 && fake_start_num() == 23 && !fake_start_again(), "SET on roll 023: its frames "
              "(start %d, again %d)", fake_start_num(), fake_start_again());
        key(MODULE_KEY_WHEEL_RIGHT);
        CHECK(wfdr_host_sel() == 21 && wfdr_host_afilm() == colour, "the wheel: roll 021, developed (%d, film %d)",
              wfdr_host_sel(), wfdr_host_afilm());
        key(MODULE_KEY_PRESS_DOWN);
        CHECK(wfdr_host_pfoc() == WF_PO_N + 2, "DOWN on a developed roll: the Film row (%d)", wfdr_host_pfoc());
        shot("shelf_done_again");
        key(MODULE_KEY_PRESS_SET);                               /* the browser picks the film */
        tick(2);
        CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && wfui_host_pick() && wfui_host_sel_id() == colour && !closed,
              "SET on the Film row: the browser (%d), picking (%d), on the roll's film (%d)", wfui_host_scr(),
              wfui_host_pick(), wfui_host_sel_id());
        key(MODULE_KEY_PRESS_RIGHT);
        int pf = wfui_host_sel_id();
        CHECK(pf && pf != colour && fake_req_film() == 0, "RIGHT: another film (%d), nothing loaded", pf);
        shot("shelf_film_pick");
        key(MODULE_KEY_PRESS_SET);
        tick(2);
        CHECK(wfui_host_scr() == WFUI_SCR_DARK && !wfui_host_pick() && wfdr_host_afilm() == pf && wfdr_host_sel() == 21
              && wfdr_host_pfoc() == WF_PO_N + 2 && fake_req_film() == 0 && !closed, "SET in the browser: back in the "
              "Darkroom (%d) on roll %d, film %d (want %d), focus %d", wfui_host_scr(), wfdr_host_sel(),
              wfdr_host_afilm(), pf, wfdr_host_pfoc());
        shot("shelf_film_back");
        key(MODULE_KEY_PRESS_SET);                               /* again, MENU there: back, the film kept */
        tick(2);
        key(MODULE_KEY_MENU);
        tick(2);
        CHECK(wfui_host_scr() == WFUI_SCR_DARK && wfdr_host_afilm() == pf && !closed, "MENU in the film pick: back (%d), "
              "film %d kept", wfui_host_scr(), wfdr_host_afilm());
        key(MODULE_KEY_PRESS_DOWN);
        key(MODULE_KEY_PRESS_SET);
        CHECK(fake_start_num() == 21 && fake_start_again() && fake_start_film() == pf, "SET on Frame: develop again of "
              "roll %d (again %d) with film %d", fake_start_num(), fake_start_again(), fake_start_film());
        key(MODULE_KEY_PRESS_UP);
        key(MODULE_KEY_PRESS_UP);
        key(MODULE_KEY_PRESS_RIGHT);
        CHECK(wfdr_host_sel() == 19 && wfdr_host_pfoc() == WF_PO_N + 1 && wfdr_host_afilm() == colour, "roll 019: its "
              "develop 2 goes on (%d, focus %d)", wfdr_host_sel(), wfdr_host_pfoc());
        shot("shelf_dev2_left");
        struct wf_dark_view *dv2 = fake_view();
        dv2->state = WF_DR_DONE; dv2->num = 21; dv2->dev = 2; dv2->film = pf; dv2->done = 12; dv2->left = 0;
        tick(2);
        shot("shelf_again_done");
        key(MODULE_KEY_PRESS_SET);
        CHECK(wfdr_host_pfoc() == WF_PO_N + 1 && fake_start_num() == 21, "SET on a run's end: the ROLL row (%d)",
              wfdr_host_pfoc());
        fake_view()->state = WF_DR_IDLE;
        close_ui();
        /* the browser: PLAY with only the shelf (no roll on the camera) opens the Darkroom on its newest roll */
        fake_dark_reset();
        shelf_ent(23, 1, bw, bw, 12, WF_RS_REWOUND, 12, 12, 0);
        shelf_ent(21, 1, colour, colour, 12, WF_RS_DONE, 12, 0, 12);
        open_scr(0);
        key(MODULE_KEY_PLAY);
        tick(2);
        CHECK(wfui_host_scr() == WFUI_SCR_DARK && wfdr_host_sel() == 23 && !closed, "PLAY with the shelf only: the "
              "Darkroom (%d) on roll %d", wfui_host_scr(), wfdr_host_sel());
        close_ui();
        /* an empty shelf: nothing to develop */
        fake_dark_reset();
        fake_view()->battery = 64;
        open_scr(WF_UI_SCR_DARK);
        shot("shelf_empty");
        int st1 = fake_nstart();
        key(MODULE_KEY_PRESS_SET);
        CHECK(fake_nstart() == st1 && !strcmp(wfdr_host_msg(), "Nothing to develop"), "an empty shelf, SET: started %d, "
              "'%s'", fake_nstart() - st1, wfdr_host_msg());
        close_ui();
        CHECK(host_live_allocs() == 0, "section 16: %ld allocations left", host_live_allocs());
    }

    printf("peak UI heap %ld bytes\n", host_peak_bytes());
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
