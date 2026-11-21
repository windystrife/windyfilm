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
#include "../../src/wf_dark.h"
#include "../../src/wf_fx.h"                    /* labfx: the Lab values (WF_GRAIN_LAB, ...) */
#include <stdlib.h>
#include <sys/stat.h>

int wfui_host_scr(void);
int wfui_host_sel_id(void);
int wfui_host_tab(void);
int wfui_host_shelf(void);
int wfui_host_focus(void);
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

/* ---------------------------------------------------------------- Prints (src/ui/wf_prints_ui.c, M5) */
int wfpr_host_roll(void);
int wfpr_host_k(void);
int wfpr_host_sheet(void);
int wfpr_host_full(void);
int wfpr_host_sel(void);
int wfpr_host_built(void);
int wfpr_host_npal(void);
int wfpr_host_pic_ok(void);
const char *wfpr_host_msg(void);
int wfui_host_play_pass(void);

/* a stand-in print: sky, a sun, fields, a row of colour patches; kind 1 warmer, 2 black and white, 3 dusk */
static void prv_rgb(int kind, int x, int y, int w, int h, int c[3])
{
    int hz = h * 56 / 100, sx = w * 70 / 100, sy = h * 24 / 100, rr = h / 9;
    if ((x - sx) * (x - sx) + (y - sy) * (y - sy) <= rr * rr)
    {
        c[0] = 255; c[1] = 226; c[2] = 168;
    }
    else if (y < hz)
    {
        int t = y * 255 / hz;
        c[0] = 64 + t / 2; c[1] = 112 + t / 3; c[2] = 196 + t / 5;
    }
    else if (y > h * 86 / 100)
    {
        static const uint8_t pt[8][3] = { { 200, 40, 40 }, { 230, 150, 40 }, { 230, 210, 60 }, { 60, 160, 70 },
                                          { 50, 150, 170 }, { 50, 70, 180 }, { 150, 60, 160 }, { 222, 180, 150 } };
        const uint8_t *q = pt[x * 8 / w];
        c[0] = q[0]; c[1] = q[1]; c[2] = q[2];
    }
    else
    {
        int band = ((x + 3 * (y - hz)) / 26) & 1, t = (y - hz) * 255 / (h - hz);
        c[0] = 70 + band * 40 + t / 6; c[1] = 110 + band * 22 - t / 8; c[2] = 40 + t / 10;
    }
    if (kind == 1) { c[0] += 24; c[2] -= 26; }
    if (kind == 3) { c[0] = c[0] * 9 / 10 + 30; c[1] = c[1] * 6 / 10; c[2] = c[2] * 7 / 10 + 20; }
    if (kind == 2) { int g = (c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8; c[0] = c[1] = c[2] = g; }
    for (int i = 0; i < 3; i++) c[i] = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
}

static uint32_t yuv_word(const int a[3], const int b[3])
{
    int y0 = (77 * a[0] + 150 * a[1] + 29 * a[2]) >> 8, y1 = (77 * b[0] + 150 * b[1] + 29 * b[2]) >> 8;
    int r = (a[0] + b[0]) / 2, g = (a[1] + b[1]) / 2, bl = (a[2] + b[2]) / 2, ym = (77 * r + 150 * g + 29 * bl) >> 8;
    int u = (bl - ym) * 144 / 256, v = (r - ym) * 183 / 256;
    u = u < -128 ? -128 : u > 127 ? 127 : u;
    v = v < -128 ? -128 : v > 127 ? 127 : v;
    return (uint32_t)(u & 0xFF) | (uint32_t)y0 << 8 | (uint32_t)(v & 0xFF) << 16 | (uint32_t)y1 << 24;
}

/* ROLLS/<roll>/Fnn_nnnn.WFP in the card dir: the layout of wf_dark.h; portrait: the print 150 x 224 in the bars;
   bad: a head that is not ours (the size right) */
static void prv_file(const char *card, const char *roll, int fr, int file, int film, int kind, int portrait, int bad,
                     int orient)
{
    static uint32_t pic[WF_PRV_W / 2 * WF_PRV_H], sh[WF_PRV_SW / 2 * WF_PRV_SH];
    char p[700];
    snprintf(p, sizeof p, "%s/ROLLS", card);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/ROLLS/%s", card, roll);
    mkdir(p, 0755);
    struct wf_prv_head h;
    memset(&h, 0, sizeof h);
    h.magic = bad ? 0x12345678u : WF_PRV_MAGIC;
    h.w = WF_PRV_W; h.h = WF_PRV_H; h.sw = WF_PRV_SW; h.sh = WF_PRV_SH;
    h.pw = portrait ? 150 : WF_PRV_W;
    h.ph = WF_PRV_H;
    h.px = (uint16_t)((WF_PRV_W - h.pw) / 2 & ~1);
    h.film = (uint16_t)film;
    h.frame = (uint16_t)(fr - 1);
    h.orient = (uint16_t)orient;
    memset(pic, 0, sizeof pic);
    for (int y = 0; y < h.ph; y++)
        for (int x = 0; x < h.pw; x += 2)
        {
            int a[3], b[3];
            if (orient == 6)                                /* stored turned left: shown h.ph wide, h.pw high */
            {
                prv_rgb(kind, h.ph - 1 - y, x, h.ph, h.pw, a);
                prv_rgb(kind, h.ph - 1 - y, x + 1, h.ph, h.pw, b);
            }
            else
            {
                prv_rgb(kind, x, y, h.pw, h.ph, a);
                prv_rgb(kind, x + 1, y, h.pw, h.ph, b);
            }
            pic[(h.py + y) * (WF_PRV_W / 2) + (h.px + x) / 2] = yuv_word(a, b);
        }
    for (int y = 0; y < WF_PRV_SH; y++)                     /* the sheet: 4 x 4 means, as wf_dk_vth_sheet */
        for (int x = 0; x < WF_PRV_SW / 2; x++)
        {
            int ya = 0, yb = 0, u = 0, v = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                {
                    uint32_t q = pic[(y * 4 + j) * (WF_PRV_W / 2) + x * 4 + i];
                    int s = (int)((q >> 8) & 0xFF) + (int)(q >> 24);
                    if (i < 2) ya += s; else yb += s;
                    u += (int8_t)(q & 0xFF);
                    v += (int8_t)((q >> 16) & 0xFF);
                }
            sh[y * (WF_PRV_SW / 2) + x] = (uint32_t)((u / 16) & 0xFF) | (uint32_t)((ya + 8) / 16) << 8
                                        | (uint32_t)((v / 16) & 0xFF) << 16 | (uint32_t)((yb + 8) / 16) << 24;
        }
    snprintf(p, sizeof p, "%s/ROLLS/%s/F%02d_%04d.WFP", card, roll, fr, file);
    FILE *f = fopen(p, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", p); exit(1); }
    fwrite(&h, sizeof h, 1, f);
    fwrite(sh, sizeof sh, 1, f);
    fwrite(pic, sizeof pic, 1, f);
    fclose(f);
}

/* the stand-in rolls: ROLL007 (F01, F02, F04 not ours, F05 + a file of the wrong size), ROLL012 (14 frames),
   ROLL012B (its develop again: 2, the second portrait) */
static void prv_card(const char *card)
{
    char p[700];
    snprintf(p, sizeof p, "rm -rf '%s/ROLLS'", card);
    if (system(p) != 0) fprintf(stderr, "rm ROLLS failed\n");
    int portra = id_of("PRS400"), hp5 = id_of("PRT160");
    prv_file(card, "ROLL007", 1, 101, hp5, 0, 0, 0, 1);
    prv_file(card, "ROLL007", 2, 102, hp5, 3, 0, 0, 1);
    prv_file(card, "ROLL007", 4, 104, hp5, 0, 0, 1, 1);
    prv_file(card, "ROLL007", 5, 105, hp5, 1, 0, 0, 6);
    snprintf(p, sizeof p, "%s/ROLLS/ROLL007/F03_0103.WFP", card);
    FILE *f = fopen(p, "wb");
    if (f) { fputs("short", f); fclose(f); }
    for (int i = 0; i < 14; i++) prv_file(card, "ROLL012", i + 1, 300 + i, portra, i % 4, 0, 0, 1);
    prv_file(card, "ROLL012B", 1, 300, portra, 2, 0, 0, 1);
    prv_file(card, "ROLL012B", 2, 301, portra, 1, 1, 0, 1);
}

static void prv_clean(const char *card)
{
    char p[700];
    snprintf(p, sizeof p, "rm -rf '%s/ROLLS'", card);
    if (system(p) != 0) fprintf(stderr, "rm ROLLS failed\n");
}

static void sheet_wait(void)
{
    for (int i = 0; i < 200 && !wfpr_host_built(); i++) tick(1);
    CHECK(wfpr_host_built(), "the contact sheet not built");
}

/* the screen's picture pixels: entries in the cans' range (the picture's own colours) inside the box */
static int prv_count(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            uint8_t c = host_px(x, y);
            n += c >= PX(C_CAN0) && c < PX(C_CAN0 + WFUI_CAN_SLOTS);
        }
    return n;
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

    /* PLAY with a blind roll (PLAY shows = Canon): refused (a toast), still open */
    key(MODULE_KEY_PLAY);
    CHECK(!closed && *wfos_host_msg(), "PLAY under a blind roll: closed %d, no toast", closed);
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
    key(MODULE_KEY_PRESS_DOWN);                             /* PLAY shows (M5) */
    CHECK(wfos_host_sfoc() == 3 && !wf_playv, "DOWN from Quick keys: row %d, PLAY shows %d", wfos_host_sfoc(), wf_playv);
    key(MODULE_KEY_PRESS_SET);
    CHECK(wf_playv == 1, "SET on PLAY shows: not Prints");
    shot("m5_eosm_settings_play");
    key(MODULE_KEY_WHEEL_LEFT);
    CHECK(wf_playv == 0, "the ring on PLAY shows: not Canon again");
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

    /* PLAY without a blind roll (PLAY shows = Canon): closes, then Canon gets PLAY; after a run Canon's catalog missed
       (labfx, wf_dark.h) the notice is said on the way out, once: the core's PLAY fallback skips that key */
    host_sent_clear();
    fake_set_dcim(1);
    int np0 = fake_nplay_notice();
    key(MODULE_KEY_PLAY);
    tick(2);
    CHECK(closed, "PLAY on Home did not close");
    end_checks();
    CHECK(host_sent_key() == MODULE_KEY_PLAY, "PLAY on Home: Canon got key %d, not PLAY", host_sent_key());
    CHECK(fake_nplay_notice() == np0 + 1, "PLAY on Home after a run: the PLAY notice %d times", fake_nplay_notice() - np0);
    CHECK(wf_ui_play_sent() && !wf_ui_play_sent(), "PLAY on Home: the sent PLAY not marked once for the core");
    fake_set_dcim(0);

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

    /* ---------------------------------------------------------------- EOS M: Prints and the contact sheet (M5) */
    {
        fake_dark_reset();
        roll_in("PRT160", 24, 12, 36);
        wf_blind = 0;
        prv_card(card);
        /* PLAY shows Prints: PLAY in shooting takes the film OS's Prints, where MENU takes Home */
        CHECK(!wf_ui_play_key(MODULE_KEY_PLAY), "PLAY took Prints with PLAY shows = Canon");
        wf_playv_set(1);
        CHECK(wf_ui_play_key(MODULE_KEY_PLAY), "PLAY did not take Prints with PLAY shows = Prints");
        CHECK(!wf_ui_play_key(MODULE_KEY_MENU), "MENU took Prints");
        wf_os_set(0);
        CHECK(!wf_ui_play_key(MODULE_KEY_PLAY), "PLAY took Prints with Camera interface = Canon + ML");
        wf_os_set(1);
        host_menu(1, -1, 0);
        CHECK(!wf_ui_play_key(MODULE_KEY_PLAY), "PLAY took Prints over the ML menu");
        host_menu(0, 0, 0);
        host_recording = 1;
        CHECK(!wf_ui_play_key(MODULE_KEY_PLAY), "PLAY took Prints while recording");
        host_recording = 0;
        lv = 0;
        CHECK(!wf_ui_play_key(MODULE_KEY_PLAY), "EOS M: PLAY took Prints outside LiveView");
        lv = 1;

        closed = 0;
        wf_ui_open_scr(WF_UI_SCR_PRINTS);
        CHECK(wf_ui_is_open(), "Prints did not open");
        wfui_task_begin();
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_PRINTS, "opened on screen %d, not Prints", wfui_host_scr());
        CHECK(wfpr_host_roll() == 0 && wfpr_host_k() == 1 && wfpr_host_pic_ok(), "Prints starts at roll %d frame %d (pic %d)",
              wfpr_host_roll(), wfpr_host_k(), wfpr_host_pic_ok());
        CHECK(fake_log_has("ui: Prints: 3 rolls, 20 prints"), "Prints: the card's prints not 3 rolls, 20");
        shot("m5_eosm_prints_portrait");
        CHECK(prv_count(72, 48, 647, 431) > 40000 && prv_count(72, 48, 200, 431) == 0,
              "portrait print: %d picture pixels, %d left of it", prv_count(72, 48, 647, 431), prv_count(72, 48, 200, 431));
        key(MODULE_KEY_PRESS_RIGHT);
        CHECK(*wfpr_host_msg() && wfpr_host_k() == 1, "RIGHT past the newest print: frame %d, no word", wfpr_host_k());
        key(MODULE_KEY_PRESS_LEFT);
        CHECK(wfpr_host_k() == 0 && wfpr_host_roll() == 0, "LEFT: roll %d frame %d", wfpr_host_roll(), wfpr_host_k());
        shot("m5_eosm_prints_bw");
        key(MODULE_KEY_WHEEL_LEFT);                         /* past the roll's first: the next older roll's last */
        CHECK(wfpr_host_roll() == 1 && wfpr_host_k() == 13, "LEFT past F01: roll %d frame %d", wfpr_host_roll(), wfpr_host_k());
        shot("m5_eosm_prints");
        CHECK(prv_count(72, 48, 647, 431) == 576 * 384 && wfpr_host_npal() > 40 && wfpr_host_npal() <= WFUI_CAN_SLOTS,
              "the 3:2 print: %d picture pixels, %d colours", prv_count(72, 48, 647, 431), wfpr_host_npal());
        key(MODULE_KEY_INFO);
        CHECK(wfpr_host_full(), "INFO: not the whole screen");
        shot("m5_eosm_prints_full");
        CHECK(prv_count(0, 0, 719, 479) == 720 * 480, "the whole screen: %d picture pixels", prv_count(0, 0, 719, 479));
        key(MODULE_KEY_MENU);
        CHECK(!wfpr_host_full() && wfui_host_scr() == WFUI_SCR_PRINTS, "MENU on the whole screen: full %d, screen %d",
              wfpr_host_full(), wfui_host_scr());
        key(MODULE_KEY_PRESS_UP);                           /* the newer roll, its first frame */
        CHECK(wfpr_host_roll() == 0 && wfpr_host_k() == 0, "UP: roll %d frame %d", wfpr_host_roll(), wfpr_host_k());
        key(MODULE_KEY_PRESS_UP);
        CHECK(wfpr_host_roll() == 0 && *wfpr_host_msg(), "UP past the newest roll: roll %d, no word", wfpr_host_roll());
        key(MODULE_KEY_PRESS_DOWN);
        key(MODULE_KEY_PRESS_DOWN);                         /* ROLL007: F01, F02, F04 (not ours), F05 */
        CHECK(wfpr_host_roll() == 2 && wfpr_host_k() == 0, "DOWN DOWN: roll %d frame %d", wfpr_host_roll(), wfpr_host_k());
        key(MODULE_KEY_PRESS_RIGHT);
        shot("m5_eosm_prints_dusk");
        key(MODULE_KEY_PRESS_RIGHT);
        CHECK(wfpr_host_k() == 2 && !wfpr_host_pic_ok(), "ROLL007 F04: frame %d, picture %d", wfpr_host_k(),
              wfpr_host_pic_ok());
        shot("m5_eosm_prints_nopic");
        key(MODULE_KEY_PRESS_RIGHT);
        CHECK(wfpr_host_k() == 3 && wfpr_host_pic_ok(), "ROLL007 F05: frame %d, picture %d", wfpr_host_k(),
              wfpr_host_pic_ok());
        shot("m5_eosm_prints_turned");                      /* stored turned left (orientation 6): shown upright */
        CHECK(prv_count(72, 48, 647, 431) == 256 * 384 && prv_count(72, 48, 231, 431) == 0
              && host_px(400, 60) != host_px(400, 420),
              "orientation 6: %d picture pixels (want 256 x 384), %d left of it", prv_count(72, 48, 647, 431),
              prv_count(72, 48, 231, 431));
        key(MODULE_KEY_PRESS_DOWN);
        CHECK(wfpr_host_roll() == 2 && *wfpr_host_msg(), "DOWN past the oldest roll: roll %d", wfpr_host_roll());

        /* the contact sheet of ROLL012 */
        key(MODULE_KEY_PRESS_UP);
        CHECK(wfpr_host_roll() == 1 && wfpr_host_k() == 0, "UP to ROLL012: roll %d frame %d", wfpr_host_roll(), wfpr_host_k());
        key(MODULE_KEY_PRESS_RIGHT);
        key(MODULE_KEY_PRESS_SET);
        CHECK(wfpr_host_sheet() && wfpr_host_sel() == 1, "SET: sheet %d on %d", wfpr_host_sheet(), wfpr_host_sel());
        sheet_wait();
        tick(1);
        shot("m5_eosm_sheet");
        CHECK(fake_log_has("ui: contact sheet: ROLL012, 14 frames,"), "the sheet's log line");
        CHECK(prv_count(36, 48, 683, 431) == 14 * WF_PRV_SW * WF_PRV_SH, "the sheet: %d picture pixels, want 14 frames'",
              prv_count(36, 48, 683, 431));
        key(MODULE_KEY_PRESS_DOWN);
        CHECK(wfpr_host_sel() == 7, "DOWN on the sheet: %d", wfpr_host_sel());
        key(MODULE_KEY_PRESS_DOWN);
        CHECK(wfpr_host_sel() == 13, "DOWN again: %d", wfpr_host_sel());
        key(MODULE_KEY_PRESS_DOWN);
        CHECK(wfpr_host_sel() == 13, "DOWN off the sheet: %d", wfpr_host_sel());
        key(MODULE_KEY_WHEEL_RIGHT);
        CHECK(wfpr_host_sel() == 13, "the ring past the last: %d", wfpr_host_sel());
        key(MODULE_KEY_PRESS_LEFT);
        key(MODULE_KEY_PRESS_UP);
        CHECK(wfpr_host_sel() == 6, "LEFT UP: %d", wfpr_host_sel());
        shot("m5_eosm_sheet_sel");
        key(MODULE_KEY_PRESS_SET);
        CHECK(!wfpr_host_sheet() && wfpr_host_roll() == 1 && wfpr_host_k() == 6 && wfpr_host_pic_ok(),
              "SET on the sheet: sheet %d, roll %d frame %d", wfpr_host_sheet(), wfpr_host_roll(), wfpr_host_k());
        key(MODULE_KEY_PRESS_SET);                          /* the sheet again: kept, no reads */
        int rd = fake_card_reads();
        tick(2);
        CHECK(wfpr_host_sheet() && wfpr_host_built() && fake_card_reads() == rd, "the sheet again: built %d, %d reads",
              wfpr_host_built(), fake_card_reads() - rd);
        key(MODULE_KEY_MENU);
        CHECK(!wfpr_host_sheet() && wfpr_host_k() == 6, "MENU on the sheet: sheet %d frame %d", wfpr_host_sheet(),
              wfpr_host_k());
        /* the sheet of ROLL007: F04 not ours shows none */
        key(MODULE_KEY_PRESS_DOWN);
        key(MODULE_KEY_PRESS_SET);
        sheet_wait();
        tick(1);
        shot("m5_eosm_sheet_short");
        CHECK(prv_count(36, 48, 683, 431) == 3 * WF_PRV_SW * WF_PRV_SH, "ROLL007's sheet: %d picture pixels",
              prv_count(36, 48, 683, 431));
        key(MODULE_KEY_MENU);
        key(MODULE_KEY_MENU);                               /* Prints -> Home */
        tick(1);
        CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "MENU on Prints: closed %d, screen %d", closed, wfui_host_scr());
        shot("m5_eosm_home");
        /* PLAY on Home with PLAY shows = Prints: Prints; there, PLAY is Canon's */
        key(MODULE_KEY_PLAY);
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_PRINTS, "PLAY on Home: screen %d, not Prints", wfui_host_scr());
        wf_blind = 1;
        fake_set_dcim(1);
        int np1 = fake_nplay_notice();
        key(MODULE_KEY_PLAY);
        tick(1);
        CHECK(!closed && *wfpr_host_msg() && fake_nplay_notice() == np1,
              "Prints PLAY under a blind roll: closed %d, no word, PLAY notice %d", closed, fake_nplay_notice() - np1);
        wf_blind = 0;
        host_sent_clear();
        key(MODULE_KEY_PLAY);
        tick(2);
        CHECK(closed, "PLAY on Prints did not close");
        end_checks();
        CHECK(host_sent_key() == MODULE_KEY_PLAY && host_nsent() == 1, "PLAY on Prints: Canon got %d (%d keys)",
              host_sent_key(), host_nsent());
        CHECK(fake_nplay_notice() == np1 + 1 && wf_ui_play_sent(), "PLAY on Prints after a run: PLAY notice %d times",
              fake_nplay_notice() - np1);
        fake_set_dcim(0);
        CHECK(!wf_ui_play_key(MODULE_KEY_PLAY), "our own PLAY for Canon's playback opened Prints again");
        CHECK(wf_ui_play_key(MODULE_KEY_PLAY), "the next PLAY does not open Prints");
        /* no prints on the card: the screen says so; Home's tile opens it (PLAY shows = Canon) */
        prv_clean(card);
        wf_playv_set(0);
        open_home();
        to_tile(2);
        key(MODULE_KEY_PRESS_SET);
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_PRINTS, "Prints tile: screen %d", wfui_host_scr());
        shot("m5_eosm_prints_none");
        key(MODULE_KEY_PRESS_RIGHT);
        CHECK(!closed && wfui_host_scr() == WFUI_SCR_PRINTS, "RIGHT with no prints: closed %d", closed);
        key(MODULE_KEY_MENU);
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_HOME, "MENU with no prints: screen %d", wfui_host_scr());
        wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
        tick(1);
        CHECK(closed, "half-press did not close");
        end_checks();
        /* the owner's own pictures (WF_PRVREAL: a card dir with ROLLS from a host Darkroom run; local shots only) */
        const char *real = getenv("WF_PRVREAL");
        if (real && *real)
        {
            char cp[1600];
            snprintf(cp, sizeof cp, "cp -r '%s/ROLLS' '%s/'", real, card);
            if (system(cp) == 0)
            {
                wf_playv_set(1);
                closed = 0;
                wf_ui_open_scr(WF_UI_SCR_PRINTS);
                wfui_task_begin();
                tick(1);
                shot("m5_real_prints");
                key(MODULE_KEY_PRESS_LEFT);
                shot("m5_real_prints_2");
                key(MODULE_KEY_INFO);
                shot("m5_real_prints_full");
                key(MODULE_KEY_INFO);
                key(MODULE_KEY_PRESS_SET);
                sheet_wait();
                tick(1);
                shot("m5_real_sheet");
                wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
                tick(1);
                end_checks();
                wf_playv_set(0);
            }
            prv_clean(card);
        }
    }

    /* ---------------------------------------------------------------- EOS M: Lab (labfx) on the film OS screens:
       Grain / Halation / Bloom Lab and Gauge 8 mm show and step on Home and in Details; a WindyFilm Halation with Bloom
       Lab is greyed with "not used" (the core's wf_row_note), WindyFilm values at 8 mm say "runs as 16 mm" */
    {
        fake_reset();
        fake_dark_reset();
        fake_set_card(card);
        host_sent_clear();
        roll_in("PRT160", 12, 12, 36);
        wf_blind = 0;
        wf_os_set(1);
        lv = 1;
        gui_state = GUISTATE_IDLE;
        fake_set_row(WF_ROW_GRAIN, WF_GRAIN_LAB);
        fake_set_row(WF_ROW_HALO, 1);                       /* Wide: Bloom Lab takes it out */
        fake_set_row(WF_ROW_BLOOM, WF_BLOOM_LAB);
        fake_set_row(WF_ROW_GAUGE, WF_GAUGE_8);
        open_home();
        for (int i = 0; i < 6 && wfos_host_hfoc() < 4; i++) key(MODULE_KEY_PRESS_DOWN);   /* the look strip */
        for (int i = 0; i < 6 && wfos_host_hfoc() != 7; i++) key(MODULE_KEY_PRESS_RIGHT);
        CHECK(wfos_host_hfoc() == 7 && !strcmp(wf_row_text(WF_ROW_GRAIN), "Lab"), "Lab: Home on Grain %d '%s'",
              wfos_host_hfoc(), wf_row_text(WF_ROW_GRAIN));
        shot("lab_eosm_home_grain");
        key(MODULE_KEY_PRESS_RIGHT);
        int k = 0, warn = 0;
        CHECK(wfos_host_hfoc() == 8 && wf_row_note(WF_ROW_HALO, &k) && k == WF_NOTE_UNUSED,
              "Lab: Home on Halation %d, Wide with Bloom Lab not 'not used' (%d)", wfos_host_hfoc(), k);
        for (int y = 330; y < 350; y++)
            for (int x = 300; x < 690; x++) warn += host_px(x, y) == PX(C_WARN);
        CHECK(warn > 40, "Lab: Home's hint does not say why Halation is not used (%d px)", warn);
        shot("lab_eosm_home_halo_unused");
        for (int i = 0; i < 6 && fake_row_value(WF_ROW_HALO) != WF_HALO_LAB; i++) key(MODULE_KEY_WHEEL_RIGHT);
        CHECK(fake_row_value(WF_ROW_HALO) == WF_HALO_LAB && !wf_row_note(WF_ROW_HALO, 0) && !strcmp(wf_row_text(WF_ROW_HALO), "Lab"),
              "Lab: the ring did not reach Halation Lab (%d)", fake_row_value(WF_ROW_HALO));
        shot("lab_eosm_home_halo_lab");
        key(MODULE_KEY_PRESS_SET);                          /* SET on the strip: Details on that row */
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_BROWSER && !wfui_host_shelf() && wfui_host_focus() == WF_ROW_HALO,
              "Lab: SET on Halation: screen %d, shelf %d, row %d", wfui_host_scr(), wfui_host_shelf(), wfui_host_focus());
        shot("lab_eosm_details");
        fake_set_row(WF_ROW_HALO, 1);
        key(MODULE_KEY_PRESS_DOWN);                         /* Bloom: Lab; Halation above it greyed, "not used" */
        CHECK(wfui_host_focus() == WF_ROW_BLOOM, "Lab: DOWN from Halation not on Bloom (%d)", wfui_host_focus());
        shot("lab_eosm_details_unused");
        key(MODULE_KEY_PRESS_DOWN);
        CHECK(wfui_host_focus() == WF_ROW_GAUGE && !strcmp(wf_row_text(WF_ROW_GAUGE), "8 mm"), "Lab: Gauge row %d '%s'",
              wfui_host_focus(), wf_row_text(WF_ROW_GAUGE));
        fake_set_row(WF_ROW_GRAIN, 2);                      /* WindyFilm values at 8 mm: "runs as 16 mm" */
        fake_set_row(WF_ROW_BLOOM, 2);
        tick(1);
        shot("lab_eosm_details_8mm");
        key(MODULE_KEY_INFO);
        tick(1);
        shot("lab_eosm_info");
        wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
        tick(1);
        CHECK(closed, "Lab: half-press did not close");
        end_checks();
        wf_blind = 1;
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
    key(MODULE_KEY_PRESS_DOWN);                             /* 5D2: the Top plate row where the EOS M has Quick keys */
    key(MODULE_KEY_PRESS_DOWN);
    CHECK(wfos_host_sfoc() == 2 && wf_plate == 1, "5D2 Settings: row %d, plate %d", wfos_host_sfoc(), wf_plate);
    key(MODULE_KEY_JOY_CENTER);
    CHECK(wf_plate == 0, "5D2 Settings: the joystick press did not turn the Top plate off");
    shot("m4_5d2_settings_plate_off");
    key(MODULE_KEY_WHEEL_RIGHT);
    CHECK(wf_plate == 1, "5D2 Settings: the dial did not turn the Top plate on again");
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

    /* ---------------------------------------------------------------- 5D2 Top plate (M4): Canon's shooting screen
       through the viewfinder opens it (wf_osvf.c wf_plate_poll, tests/host OSVF): the values, the roll's look, the
       film's card. The dials stay Canon's and a change under it redraws and keeps it up; TRASH the Film shelf, INFO
       Canon's INFO, PLAY Prints (Canon's PLAY), 20 s without a key or a change Canon's screen (nothing sent) */
    {
        struct wfos_lv pv = lv_vals(WF_VF_FULL, "1/250", "f/5.6", "400", 1, -5, 2, 1204, 0);
        snprintf(pv.mode, sizeof pv.mode, "Av");
        snprintf(pv.drive, sizeof pv.drive, "Single");
        fake_set_lv(&pv);
        for (int round = 0; round < 6; round++)
        {
            host_gui_set(0, 1, 1);
            host_sent_clear();
            closed = 0;
            wf_ui_open_scr(WF_UI_SCR_PLATE);
            CHECK(wf_ui_is_open(), "5D2 Top plate %d did not open", round);
            wfui_task_begin();
            CHECK(wfui_host_scr() == WFUI_SCR_PLATE, "5D2 Top plate %d: opened on screen %d", round, wfui_host_scr());
            tick(1);
            if (round == 0)
            {
                shot("m4_5d2_plate");
                int d0 = fake_plate_dials(0), d1 = fake_plate_dials(1);
                CHECK(key(MODULE_KEY_WHEEL_RIGHT) == 0 && fake_plate_dials(0) == d0 + 1 && fake_plate_dial_dir() == 1,
                      "5D2 Top plate: the main dial did not step (Canon would get it in its menu mode)");
                CHECK(key(MODULE_KEY_WHEEL_UP) == 0 && fake_plate_dials(1) == d1 + 1 && fake_plate_dial_dir() == -1,
                      "5D2 Top plate: the rear wheel did not step");
                tick(400);                                  /* 8 s, then the dial's change shows */
                snprintf(pv.sh, sizeof pv.sh, "1/500");
                pv.meter = -13;
                fake_set_lv(&pv);
                tick(600);                                  /* 20 s after the open, 12 s after the change: still up */
                CHECK(!closed && wfui_host_scr() == WFUI_SCR_PLATE, "5D2 Top plate: closed 12 s after a change");
                shot("m4_5d2_plate_dial");
                key(MODULE_KEY_TRASH);
                tick(1);
                CHECK(!closed && wfui_host_scr() == WFUI_SCR_BROWSER && wfui_host_shelf(),
                      "5D2 Top plate TRASH: screen %d, shelf %d", wfui_host_scr(), wfui_host_shelf());
                key(MODULE_KEY_MENU);
                tick(1);
                CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "5D2 Top plate's shelf, MENU: screen %d",
                      wfui_host_scr());
                wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
                tick(1);
                CHECK(closed && host_nsent() == 0, "5D2 Home from the plate, half-press: closed %d, keys %d", closed,
                      host_nsent());
            }
            else if (round == 1)
            {
                key(MODULE_KEY_INFO);
                tick(1);
                CHECK(closed, "5D2 Top plate INFO did not close");
            }
            else if (round == 2)
            {
                key(MODULE_KEY_MENU);
                tick(1);
                CHECK(!closed && wfui_host_scr() == WFUI_SCR_HOME, "5D2 Top plate MENU: screen %d", wfui_host_scr());
                wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
                tick(1);
            }
            else if (round == 3)
            {
                tick(1010);                                 /* 20 s, nothing: Canon's own screen */
                CHECK(closed, "5D2 Top plate: still up after 20 s without a key");
            }
            else if (round == 4)                            /* the roll at its end, blind roll on, no meter, BULB */
            {
                roll_in("PRS400", 21, 36, 36);
                wf_blind = 1;
                struct wfos_lv bv = lv_vals(WF_VF_FULL, "BULB", "f/11", "AUTO", 0, WFOS_NOMETER, 1, 96, 0);
                snprintf(bv.mode, sizeof bv.mode, "B");
                snprintf(bv.drive, sizeof bv.drive, "Timer 2s");
                fake_set_lv(&bv);
                tick(1);
                shot("m4_5d2_plate_end");
                key(MODULE_KEY_PLAY);
                tick(1);
                CHECK(!closed && wfos_host_msg()[0], "5D2 Top plate PLAY, blind roll: closed %d, no word", closed);
                wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
                tick(1);
            }
            else                                            /* no roll, no film: Canon's camera, the plate as ever */
            {
                fake_dark_reset();
                fake_set_loaded(0);
                wf_blind = 0;
                pv = lv_vals(WF_VF_FULL, "1/60", "f/4", "800", 0, 3, 2, 2210, 0);
                snprintf(pv.mode, sizeof pv.mode, "M");
                snprintf(pv.drive, sizeof pv.drive, "Cont.");
                fake_set_lv(&pv);
                tick(1);
                shot("m4_5d2_plate_canon");
                fake_set_dcim(1);
                key(MODULE_KEY_PLAY);
                tick(1);
                CHECK(closed, "5D2 Top plate PLAY did not close");
            }
            CHECK(closed, "5D2 Top plate %d: not closed", round);
            end_checks();
            if (round == 1)
                CHECK(host_sent_key() == MODULE_KEY_INFO, "5D2 Top plate INFO: Canon got %d", host_sent_key());
            if (round == 3)
                CHECK(host_nsent() == 0, "5D2 Top plate idle: %d keys sent", host_nsent());
            if (round == 5)
            {
                CHECK(host_sent_key() == MODULE_KEY_PLAY, "5D2 Top plate PLAY: Canon got %d", host_sent_key());
                CHECK(fake_nplay_notice() == 1 && wf_ui_play_sent(), "5D2 Top plate PLAY after a run: PLAY notice %d times",
                      fake_nplay_notice());
                fake_set_dcim(0);
            }
        }
    }
    lv = 1;

    /* ---------------------------------------------------------------- 5D2: Prints from the Top plate's PLAY (M5) */
    {
        fake_reset();
        fake_dark_reset();
        fake_set_card(card);
        fake_set_cam(WF_CAM_5D2);
        wf_os_set(1);
        wf_playv_set(1);
        wf_blind = 0;
        roll_in("PRS400", 21, 17, 36);
        prv_card(card);
        lv = 0;
        host_gui_set(0, 1, 1);
        CHECK(wf_ui_play_key(MODULE_KEY_PLAY), "5D2: PLAY through the viewfinder did not take Prints");
        closed = 0;
        wf_ui_open_scr(WF_UI_SCR_PLATE);
        wfui_task_begin();
        tick(1);
        key(MODULE_KEY_PLAY);
        tick(1);
        CHECK(!closed && wfui_host_scr() == WFUI_SCR_PRINTS, "5D2 Top plate PLAY: closed %d, screen %d", closed,
              wfui_host_scr());
        key(MODULE_KEY_WHEEL_LEFT);                         /* the main dial: frames */
        CHECK(wfpr_host_k() == 0, "5D2 main dial: frame %d", wfpr_host_k());
        key(MODULE_KEY_PICSTYLE);                           /* PICT: the older roll */
        CHECK(wfpr_host_roll() == 1, "5D2 PICT on Prints: roll %d", wfpr_host_roll());
        shot("m5_5d2_prints");
        key(MODULE_KEY_JOY_CENTER);
        sheet_wait();
        tick(1);
        shot("m5_5d2_sheet");
        key(MODULE_KEY_PICSTYLE);                           /* PICT on the sheet: the next roll's sheet */
        CHECK(wfpr_host_sheet() && wfpr_host_roll() == 2, "5D2 PICT on the sheet: sheet %d roll %d", wfpr_host_sheet(),
              wfpr_host_roll());
        sheet_wait();
        key(MODULE_KEY_JOY_CENTER);
        CHECK(!wfpr_host_sheet() && wfpr_host_roll() == 2, "5D2 joystick on the sheet: sheet %d", wfpr_host_sheet());
        wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
        tick(1);
        CHECK(closed, "5D2 half-press on Prints did not close");
        end_checks();
        prv_clean(card);
        wf_playv_set(0);
        lv = 1;
    }

    /* 5D2: Lab on the Top plate, Home and Details (labfx) */
    {
        fake_reset();
        fake_dark_reset();
        fake_set_card(card);
        fake_set_cam(WF_CAM_5D2);
        wf_os_set(1);
        host_sent_clear();
        roll_in("PRT160", 12, 12, 36);
        wf_blind = 0;
        lv = 0;
        fake_set_row(WF_ROW_GRAIN, WF_GRAIN_LAB);
        fake_set_row(WF_ROW_HALO, WF_HALO_LAB);
        fake_set_row(WF_ROW_BLOOM, 2);                      /* Normal: Halation Lab takes it out */
        fake_set_row(WF_ROW_GAUGE, WF_GAUGE_8);
        struct wfos_lv pv = lv_vals(WF_VF_FULL, "1/125", "f/2.8", "400", 1, 0, 2, 1204, 0);
        snprintf(pv.mode, sizeof pv.mode, "Av");
        snprintf(pv.drive, sizeof pv.drive, "Single");
        fake_set_lv(&pv);
        host_gui_set(0, 1, 1);
        closed = 0;
        wf_ui_open_scr(WF_UI_SCR_PLATE);
        CHECK(wf_ui_is_open(), "5D2 Lab: the Top plate did not open");
        wfui_task_begin();
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_PLATE && !strcmp(wf_row_text(WF_ROW_GRAIN), "Lab"), "5D2 Lab: plate %d, Grain '%s'",
              wfui_host_scr(), wf_row_text(WF_ROW_GRAIN));
        shot("lab_5d2_plate");
        CHECK(key(MODULE_KEY_MENU) == 0, "5D2 Lab: MENU on the plate not consumed");
        tick(1);
        CHECK(wfui_host_scr() == WFUI_SCR_HOME, "5D2 Lab: MENU on the plate, screen %d", wfui_host_scr());
        for (int i = 0; i < 6 && wfos_host_hfoc() < 4; i++) key(MODULE_KEY_PRESS_DOWN);   /* the look strip */
        for (int i = 0; i < 6 && wfos_host_hfoc() != 8; i++) key(MODULE_KEY_PRESS_RIGHT);
        CHECK(wfos_host_hfoc() == 8 && !strcmp(wf_row_text(WF_ROW_HALO), "Lab"), "5D2 Lab: Home on Halation %d '%s'",
              wfos_host_hfoc(), wf_row_text(WF_ROW_HALO));
        shot("lab_5d2_home");
        key(MODULE_KEY_PRESS_SET);
        tick(1);
        key(MODULE_KEY_PRESS_DOWN);
        int k = 0;
        CHECK(wfui_host_focus() == WF_ROW_BLOOM && wf_row_note(WF_ROW_BLOOM, &k) && k == WF_NOTE_UNUSED,
              "5D2 Lab: Bloom Normal with Halation Lab (row %d, note %d)", wfui_host_focus(), k);
        shot("lab_5d2_details");
        wf_ui_key(MODULE_KEY_PRESS_HALFSHUTTER);
        tick(1);
        CHECK(closed, "5D2 Lab: half-press did not close");
        end_checks();
        wf_blind = 1;
    }

    /* 5D2 LiveView: the same finder */
    lv_shots("5d2");

    printf("os_test: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
