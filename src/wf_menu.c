/* wf_menu.c - WindyFilm state (WindyStrife): output, film, the browser rows, the ML menu "WindyFilm", the settings file
 * ML/SETTINGS/WINDYFLM.CFG, Light (white balance) and Print, the EXIF UserComment of each shot, wf_shot_ev8.
 * Compiled with ML's headers (struct menu_entry, lens_info, properties).
 *
 * Threads: the rows and the ML menu change plain ints from the GUI task (CBR_KEYPRESS -> browser, ML menu). Everything
 * that sleeps or touches files runs in wf_task through wf_menu_poll: config save (~1.5 s after the last change), Light
 * (lens_set_kelvin sleeps), the EXIF writer, the Film LUT cube scan. Module init reads the config file only.
 *
 * WINDYFLM.CFG: "key=value" lines, all keys written every time (unknown keys and values are ignored on read, a missing key
 * keeps its default):
 *   input=jpeg       reserved for a RAW input path (docs/DESIGN.md 3.1); "jpeg" is the only value, no menu entry
 *   film=<id>        loaded film / Film LUT can id (0 none)          output=film|log|lut|off
 *   curve=slog3|vlog|logc3|clog2|clog3                                 flut=technical|<cube code>   lutei=<1/3 EV>
 *   incurve=srgb|film1|neutral   how Log + LUT reads the JPEG (film1 / neutral: only when the build has that curve)
 *   print=scan|cine|paper   light=auto|day|cloudy|tungsten   strength=subtle|normal|strong
 *   hl=<-5..5>  sh=<-5..5>   Highlights / Shadows (films and Log + LUT; 0 = the curve as it was)
 *   grain=off|fine|normal|strong   halation=off|halation|bloom|spk|spkbloom   mist=off|w8|w4|w2|w1|b8|b4|b2|b1
 *   preview=off|on|fast   exposure=camera|smooth   exptrim=<1/8 EV>   exif=on|off
 *   userwb=<mode>,<kelvin>,<gm>,<ba>   the user's white balance while Light holds it (restored on Auto / unload, also
 *                    after a power cycle in between) */
#include <dryos.h>
#include <menu.h>
#include <lens.h>
#include <property.h>
#include <fileprefix.h>
#define WF_ML_HEADERS
#include "wf_core.h"
#include "wf_loglut.h"
#include "wf_film.h"
#include "wf_fx.h"
#include "wf_expsim.h"
#include "wf_rawb.h"

extern int shooting_mode;                         /* Canon mode dial: 3 = M */
extern int lv;
extern int get_global_draw(void);
extern int get_global_draw_setting(void);

#define WF_CFG "ML/SETTINGS/WINDYFLM.CFG"
#define WF_CFG_MAX 600

/* ------------------------------------------------------------------ the state */
volatile int wf_out = WF_OUT_FILM;
volatile int wf_film_id;                          /* 0 = none loaded */
volatile int wf_print = WF_PRINT_SCAN;
volatile int wf_light = WF_LIGHT_AUTO;
volatile int wf_curve = WF_CURVE_VLOG;
volatile int wf_flut;                             /* 0 Technical (the user's own <LOG>.CUB) */
volatile int wf_lut_ei;                           /* 1/3 EV, WF_LL_EI_MIN..WF_LL_EI_MAX */
volatile int wf_incurve;                          /* WF_INC_*: how Log + LUT reads the JPEG (wf_incurve_t) */
volatile int wf_strength = 1;                     /* 30 / 100 / 125 % */
volatile int wf_hl, wf_sh;                        /* Highlights / Shadows, WF_HS_MIN..WF_HS_MAX, 0 = off */
volatile int wf_exif_off;
volatile int wf_cfg_dirty;
static volatile int wf_cfg_t;
static int wf_out_prev = WF_OUT_FILM;             /* Film LUT row Off goes back to this output */
static int wf_menu_added, wf_cfg_read;
static volatile int wf_light_req;                 /* wf_task applies Light (a row / film / output change) */

static void wf_dirty(void)
{
    wf_cfg_t = get_ms_clock();
    wf_cfg_dirty = 1;
    wf_gen_bump();
}

int wf_lut_ei_t(void)
{
    int v = wf_lut_ei;
    return v >= WF_LL_EI_MIN && v <= WF_LL_EI_MAX ? v : 0;
}

static int wf_hs_in(int v) { return v >= WF_HS_MIN && v <= WF_HS_MAX ? v : 0; }
int wf_hl_t(void) { return wf_hs_in(wf_hl); }
int wf_sh_t(void) { return wf_hs_in(wf_sh); }
static const char *wf_hs_name[WF_HS_MAX - WF_HS_MIN + 1] = { "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5" };
typedef char wf_hs_check[sizeof wf_hs_name / sizeof *wf_hs_name == 11 && WF_HS_MIN == -5 ? 1 : -1];
const char *wf_hs_text(int v) { return wf_hs_name[wf_hs_in(v) - WF_HS_MIN]; }

/* Picture Style (ML's PROP_PICTURE_STYLE handler keeps lens_info.raw_picstyle; 0 until the property was seen).
 * Each Canon curve is measured under one style and one Contrast setting (raw-film-engine, makernote PSInfo):
 *   Film 1: User Def. 1 holding the loaded style file (base PC 1), Contrast -2;
 *   Neutral: the built-in Neutral, Contrast 0.
 * Under anything else the JPEG is read as sRGB, and wf_ll_poll reloads the tables when it changes. The slot alone is
 * not enough: User Def. 1 can be based on a built-in style (the owner's EOS M had base Auto on 2026-11-14), so its base
 * comes from PROP_PICSTYLE_OF_USERDEF1 (a loaded file is below 0x80, as ML's picstyle.c tells them apart). */
#define WF_PS_USER1   0x21                        /* User Def. 1 (ML picstyle.c USER1_ID): holds the Film 1 style */
#define WF_PS_NEUTRAL 0x84                        /* PROP_PICTURE_STYLE value of Neutral */
#define WF_PS_STANDARD 0x81                       /* .. of Standard (Leica Vivid's base, WF_FLUT_DISP) */
#define WF_PROP_PICSTYLE 0x80000028               /* ML property.h PROP_PICTURE_STYLE (DIGIC 4 / 5) */
static const uint8_t wf_inc_ps[WF_INC_N] = { 0, WF_PS_USER1, WF_PS_NEUTRAL };
static const int8_t wf_inc_con[WF_INC_N] = { 0, -2, 0 };   /* Contrast of the measurement */
typedef char wf_inc_check[WF_INC_N == WF_LL_NINC ? 1 : -1];
#define WF_PROP_PS_OF_USER1 0x0206000a            /* ML property.h PROP_PICSTYLE_OF_USERDEF1, DIGIC 4 / 5 (both bodies);
                                                     not visible to module builds (its block needs CONFIG_DIGIC_45) */
extern void prop_add_handler(uint32_t property, void *handler);
extern int picstyle_get_current_contrast(void);   /* ML core: the active style's Contrast, -10 when not known */
static volatile int wf_ps_base = -1;              /* PROP_PICSTYLE_OF_USERDEF1, -1 = not reported yet */
static void wf_ps_base_prop(unsigned property, void *priv, uint32_t *buf, unsigned len)
{
    (void)property; (void)priv;
    if (buf && len >= 4)
        wf_ps_base = (int)(buf[0] & 0xff);
}
int wf_style_raw(void) { return lens_info.raw_picstyle; }
int wf_job_state(void) { return (int)lens_info.job_state; }
int wf_iso_now(void) { return lens_info.iso ? (int)lens_info.iso : (int)lens_info.iso_auto; }
int wf_style_base(void) { return wf_ps_base; }
int wf_style_con(void) { int c = picstyle_get_current_contrast(); return c >= -4 && c <= 4 ? c : -10; }
/* why input curve `inc` is not usable under the camera's Picture Style: WF_PSW_OK, or the first thing that differs.
 * Not reported yet (style 0, base -1, Contrast -10) counts as fitting. */
int wf_style_why(int inc)
{
    int s = lens_info.raw_picstyle, c = wf_style_con();
    if (inc <= 0 || inc >= WF_INC_N || s == 0)
        return WF_PSW_OK;
    if (s != wf_inc_ps[inc])
        return WF_PSW_STYLE;
    if (s == WF_PS_USER1 && wf_ps_base >= 0x80)
        return WF_PSW_BASE;
    if (c != -10 && c != wf_inc_con[inc])
        return WF_PSW_CONTRAST;
    return WF_PSW_OK;
}
int wf_style_ok(int inc) { return wf_style_why(inc) == WF_PSW_OK; }
int wf_style_con_of(int inc) { return wf_inc_con[inc > 0 && inc < WF_INC_N ? inc : 0]; }
extern const char *picstyle_get_current_name(void);   /* ML core (picstyle.c), exported to modules: Canon's names stay
                                                         in the core, the module holds none (provenance gate) */

/* the user's choice when its curve is built in, else sRGB (the menu value) */
static int wf_inc_chosen(void) { int k = wf_incurve; return k > 0 && k < WF_INC_N && wf_ll_curve_have(k) ? k : WF_INC_SRGB; }

int wf_incurve_t(void)
{
    if (wf_out == WF_OUT_LLUT && WF_FLUT_DISP(wf_flut))
        return WF_INC_SRGB;                       /* Leica Vivid: its cube expects the JPEG as sRGB, whatever the menu says */
    int k = wf_inc_chosen();
    return wf_style_ok(k) ? k : WF_INC_SRGB;
}

static const int wf_spct[WF_NSTRENGTH] = { 30, 100, 125 };
int wf_strength_pct(int level) { return wf_spct[level >= 0 && level < WF_NSTRENGTH ? level : 1]; }

/* ------------------------------------------------------------------ catalogue */
int wf_cat_index(int id)
{
    for (int k = 0; id > 0 && k < wf_nfilms; k++)
        if (wf_films[k].id == id)
            return k;
    return -1;
}

const struct wf_film *wf_film_by_id(int id)
{
    int k = wf_cat_index(id);
    return k >= 0 ? &wf_films[k] : 0;
}

/* ------------------------------------------------------------------ names */
static const char *const wf_out_key[WF_NOUT] = { "film", "log", "lut", "off" };
static const char *const wf_curve_key[WF_NCURVE] = { "slog3", "vlog", "logc3", "clog2", "clog3" };
static const char *wf_curve_name[WF_NCURVE] = { "S-Log3", "V-Log", "LogC3", "Canon Log 2", "Canon Log 3" };
static const char *wf_print_name[WF_NPRINTS] = { "Scan", "Cine", "Paper",
    "2302", "2383", "2393", "2395", "5380", "5381/50", "5381/72", "5382", "5384/82", "5384/92", "5385/93", "5385/62",
    "5386", "5383", "3513D", "3523XD" };
static const char *const wf_print_key[WF_NPRINTS] = { "scan", "cine", "paper",
    "p00", "p01", "p02", "p03", "p04", "p05", "p06", "p07", "p08", "p09", "p10", "p11", "p12", "p13", "p14", "p15" };
static const char *wf_light_name[WF_NLIGHT] = { "Auto", "Day", "Cloudy", "Tungsten" };
static const char *const wf_light_key[WF_NLIGHT] = { "auto", "day", "cloudy", "tungsten" };
static const char *wf_strength_name[WF_NSTRENGTH] = { "30%", "100%", "125%" };
static const char *const wf_strength_key[WF_NSTRENGTH] = { "subtle", "normal", "strong" };
static const char *wf_grain_name[WF_GRAIN_MAX + 1] = { "Off", "Fine", "Normal", "Strong" };
static const char *const wf_grain_key[WF_GRAIN_MAX + 1] = { "off", "fine", "normal", "strong" };
static const char *wf_halo_name[WF_HALO_MAX + 1] = { "Off", "Film", "Film + bloom", "Spektra", "Spektra + bloom" };
static const char *const wf_halo_key[WF_HALO_MAX + 1] = { "off", "halation", "bloom", "spk", "spkbloom" };
static const char *wf_mist_name[WF_MIST_MAX + 1] = { "Off", "1/8", "1/4", "1/2", "1", "Black 1/8", "Black 1/4", "Black 1/2",
                                                     "Black 1" };
static const char *const wf_mist_key[WF_MIST_MAX + 1] = { "off", "w8", "w4", "w2", "w1", "b8", "b4", "b2", "b1" };
static const char *wf_pv_name[3] = { "Off", "On", "Fast" };
static const char *const wf_pv_key[3] = { "off", "on", "fast" };
static const char *const wf_exp_key[2] = { "camera", "smooth" };
static const char *wf_out_menu[WF_NOUT] = { "Film", "Log", "Log + LUT", "Off" };
static const char *wf_lei_name[WF_LL_EI_MAX - WF_LL_EI_MIN + 1] = { "-1 EV", "-2/3 EV", "-1/3 EV", "0", "+1/3 EV", "+2/3 EV",
                                                                    "+1 EV", "+1 1/3 EV", "+1 2/3 EV", "+2 EV" };
typedef char wf_lei_check[sizeof wf_lei_name / sizeof *wf_lei_name == 10 && WF_LL_EI_MIN == -3 ? 1 : -1];
static const char *wf_inc_name[WF_INC_N] = { "sRGB", "Canon Film 1", "Canon Neutral" };
static const char *const wf_inc_key[WF_INC_N] = { "srgb", "film1", "neutral" };

static const char *wf_pick(const char *const *t, int n, int v) { return t[v >= 0 && v < n ? v : 0]; }
static int wf_cur(void) { int c = wf_curve; return c >= 1 && c <= WF_NCURVE ? c : WF_CURVE_VLOG; }

const char *wf_out_name(int out, int curve)
{
    static char b[24];
    if (curve < 1 || curve > WF_NCURVE)
        curve = WF_CURVE_VLOG;
    if (out == WF_OUT_LOG)
        snprintf(b, sizeof b, "%s flat", wf_curve_name[curve - 1]);
    else if (out == WF_OUT_LLUT)
        snprintf(b, sizeof b, "%s + LUT", wf_curve_name[curve - 1]);
    else
        return out == WF_OUT_FILM ? "Film" : "Off";
    return b;
}

/* "Portra 400", "Portra 400 Cine", "Tung 500T P" (a can), "V-Log + LUT Vision3 250D", "S-Log3 flat", "Off" */
const char *wf_look_title(const struct wf_look *lk, char *b, int n)
{
    int r = lk->row;
    if (lk->output == WF_OUT_FILM && r >= 0 && r < wf_nfilms)
        snprintf(b, n, lk->variant ? "%s %s" : "%s", wf_films[r].name, wf_print_name[lk->variant < WF_NPRINTS ? lk->variant : 0]);
    else if (lk->output == WF_OUT_LLUT && r >= 0 && r < wf_nfilms)
        snprintf(b, n, "%s", wf_films[r].name);
    else if (lk->output == WF_OUT_LLUT && WF_FLUT_DISP(lk->flut))
        snprintf(b, n, "%s (look on Standard)", wf_flut_name[lk->flut]);
    else if (lk->output == WF_OUT_LLUT)
        snprintf(b, n, "%s %s", wf_out_name(WF_OUT_LLUT, lk->curve), wf_flut_name[lk->flut < WF_FLUT_N ? lk->flut : 0]);
    else if (lk->output == WF_OUT_LOG)
        snprintf(b, n, "%s", wf_out_name(WF_OUT_LOG, lk->curve));
    else
        snprintf(b, n, "Off");
    return b;
}

/* ------------------------------------------------------------------ Film LUT cubes on the card (wf_task) */
static volatile uint32_t wf_cube_have;            /* bit l: the cans' cube l on its own curve (WF_FLUT_CURVE: <code>VL.CUB,
                                                     <code>C2.CUB for the CinePrint looks) is on the card */
static volatile uint32_t wf_cube_vl;              /* bit l: a one-log cube (WF_FLUT_VLONLY) whose own curve is not V-Log (the
                                                     CinePrint looks since 11-14) also has its old <code>VL.CUB on the card:
                                                     Log curve V-Log set by hand uses it, so no "set Log curve" advice */
static volatile int wf_cube_scan_req = 1;

static int wf_cube_on_card(int curve, int l)
{
    uint32_t sz = 0;
    char b[WF_LL_NAME];
    const char *p = wf_loglut_name(b, curve, l, 0);
    return p && FIO_GetFileSize(p, &sz) == 0 && sz > 0;
}

static void wf_cube_scan(void)
{
    uint32_t m = 0, v = 0;
    for (int l = 1; l < WF_FLUT_N; l++) {
        if (wf_cube_on_card(WF_FLUT_CURVE(l), l))
            m |= 1u << l;
        if (WF_FLUT_VLONLY(l) && WF_FLUT_CURVE(l) != WF_CURVE_VLOG && wf_cube_on_card(WF_CURVE_VLOG, l))
            v |= 1u << l;
    }
    wf_cube_vl = v;
    if (m != wf_cube_have) {
        wf_cube_have = m;
        wf_gen_bump();
    }
}

/* a display look (WF_FLUT_DISP, Leica Vivid) was chosen: Picture Style Standard, the base its cube was made on */
static void wf_flut_style(int l)
{
    if (!WF_FLUT_DISP(l) || lens_info.raw_picstyle == WF_PS_STANDARD)
        return;
    int v = WF_PS_STANDARD;
    prop_request_change(WF_PROP_PICSTYLE, &v, 4);
    wf_log("style: Standard for %s", wf_flut_name[l]);
}

/* ------------------------------------------------------------------ load / loaded / avail (wf_api.h) */
static int wf_print_first(int k);                 /* defined below: the first variant the film really has */

/* a film is usable when the pack holds a good table for ANY of its variants. Not "for Scan": the chain films have no
 * Scan/Cine/Paper at all (their variants are the sixteen print stocks), so asking for Scan hid all 44 of them from the
 * strip even though their tables were on the card. */
static int wf_any_table(int k)
{
    for (int v = 0; v < WF_NPRINTS; v++)
        if ((wf_films[k].prints & (1u << v)) && wf_ws_has(k, v))
            return 1;
    return 0;
}

int wf_avail(int id)
{
    int k = wf_cat_index(id);
    if (k < 0 || !wf_cam_ok || !(wf_films[k].flags & WF_F_READY))
        return 0;
    if (wf_films[k].flags & WF_F_LUTCAN)
        return (wf_cube_have >> wf_cat_lut[k]) & 1;
    return wf_any_table(k);
}

int wf_load(int id)
{
    int k = wf_cat_index(id);
    if (k < 0)
        return WF_LOAD_UNKNOWN;
    const struct wf_film *f = &wf_films[k];
    if (!wf_cam_ok || wf_unloading)
        return WF_LOAD_BUSY;
    if (f->flags & WF_F_LUTCAN) {
        int l = wf_cat_lut[k];
        if (l <= 0 || l >= WF_FLUT_N || !wf_cube_on_card(WF_FLUT_CURVE(l), l))
            return WF_LOAD_NOCUBE;
        if (wf_out != WF_OUT_LLUT)
            wf_out_prev = wf_out;
        wf_film_id = id;
        wf_out = WF_OUT_LLUT;                     /* a can: its cube's own curve + LUT (V-Log, CinePrint looks C-Log2) */
        wf_curve = WF_FLUT_CURVE(l);
        wf_flut = l;
        wf_flut_style(l);
    } else {
        if (!(f->flags & WF_F_READY) || !wf_any_table(k))
            return WF_LOAD_NOTABLE;
        wf_film_id = id;
        wf_out = WF_OUT_FILM;
    }
    wf_light_req = 1;                             /* Day / Cloudy follow the stock's kelvin */
    wf_dirty();
    wf_log("load %d %s", id, f->name);
    return WF_LOAD_OK;
}

int wf_loaded(void)
{
    int k = wf_cat_index(wf_film_id);
    if (k < 0)
        return 0;
    const struct wf_film *f = &wf_films[k];
    if (wf_out == WF_OUT_FILM && (f->flags & (WF_F_READY | WF_F_LUTCAN)) == WF_F_READY)
        return f->id;
    if (wf_out == WF_OUT_LLUT && (f->flags & WF_F_LUTCAN) && wf_cur() == WF_FLUT_CURVE(wf_cat_lut[k]) && wf_flut == wf_cat_lut[k])
        return f->id;
    return 0;
}

/* ------------------------------------------------------------------ rows (wf_api.h) */
static const char *const wf_rname[WF_NROWS] = { "Light", "Print stock", "Grain", "Halation", "Mist", "Strength",
                                                "Highlights", "Shadows", "Film LUT" };

const char *wf_row_name(int row) { return row >= 0 && row < WF_NROWS ? wf_rname[row] : ""; }

/* the print a film shoots for wf_print: its own variant when it has it, else its FIRST available one (the chain films
 * have no Scan/Cine/Paper at all: their variants start at WF_PRINT_STOCK0) */
static int wf_print_first(int k)
{
    if (k < 0)
        return WF_PRINT_SCAN;
    for (int v = 0; v < WF_NPRINTS; v++)
        if (wf_films[k].prints & (1u << v))
            return v;
    return WF_PRINT_SCAN;
}
static int wf_print_eff(int k)
{
    int v = wf_print;
    if (k < 0 || v < 0 || v >= WF_NPRINTS || !(wf_films[k].prints & (1u << v)))
        return wf_print_first(k);
    if (wf_ws_state() == 1 && !wf_ws_has(k, v))
        return wf_print_first(k);
    return v;
}
static int wf_print_ok(int k, int v)
{
    return k >= 0 && (wf_films[k].prints & (1u << v)) && (wf_ws_state() != 1 || wf_ws_has(k, v));
}

const char *wf_row_text(int row)
{
    int k = wf_cat_index(wf_film_id);
    switch (row) {
    case WF_ROW_LIGHT:    return wf_pick(wf_light_name, WF_NLIGHT, wf_light);
    case WF_ROW_PRINT:    return wf_print_name[wf_print_eff(k)];
    case WF_ROW_GRAIN:    return wf_pick(wf_grain_name, WF_GRAIN_MAX + 1, wf_grain);
    case WF_ROW_HALO:     return wf_pick(wf_halo_name, WF_HALO_MAX + 1, wf_halo);
    case WF_ROW_MIST:     return wf_pick(wf_mist_name, WF_MIST_MAX + 1, wf_mist);
    case WF_ROW_STRENGTH: return wf_pick(wf_strength_name, WF_NSTRENGTH, wf_strength);
    case WF_ROW_HL:       return wf_hs_text(wf_hl);
    case WF_ROW_SH:       return wf_hs_text(wf_sh);
    case WF_ROW_FLUT:     return wf_out == WF_OUT_LLUT ? wf_flut_name[wf_flut > 0 && wf_flut < WF_FLUT_N ? wf_flut : 0] : "Off";
    }
    return "";
}

int wf_row_avail(int row)
{
    if (!wf_cam_ok)
        return 0;
    struct wf_look lk;
    wf_look_now(&lk);
    int k = wf_cat_index(wf_film_id), post = lk.post;
    switch (row) {
    case WF_ROW_LIGHT:    return wf_out != WF_OUT_OFF;
    case WF_ROW_PRINT:
        if (lk.output != WF_OUT_FILM)
            return 0;
        for (int v = 1; v < WF_NPRINTS; v++)
            if (wf_print_ok(k, v))
                return 1;
        return 0;
    case WF_ROW_GRAIN:    return WF_GRAIN_CAN && post;
    case WF_ROW_HALO:     return WF_HALO_CAN && post && wf_aspect_has_halo(wf_aspect);
    case WF_ROW_MIST:     return WF_MIST_CAN && post;
    case WF_ROW_STRENGTH: return lk.output == WF_OUT_FILM;
    case WF_ROW_HL:
    case WF_ROW_SH:       return lk.output == WF_OUT_FILM || lk.output == WF_OUT_LLUT;   /* not flat Log */
    case WF_ROW_FLUT:     return 1;
    }
    return 0;
}

static int wf_wrap(int v, int d, int n) { v = (v >= 0 && v < n ? v : 0) + (d < 0 ? -1 : 1); return v < 0 ? n - 1 : v >= n ? 0 : v; }

/* Film LUT row: Off -> the cubes on the card -> Off. From Off: Log + LUT (the output before is kept for Off). Each cube
 * comes on its own curve (WF_FLUT_CURVE: V-Log, the CinePrint looks Canon Log 2) while the curve in use is the current
 * cube's own one; a Log curve the user set by hand that is not the cube's own (S-Log3 on Vision3 250D, V-Log on a
 * CinePrint look) stays, and only the cubes on the card for it come. Technical keeps the curve in use. Order: the film
 * cubes, the V-Log-only cubes, EKTA P, Technical. */
static const uint8_t wf_flut_cyc[WF_FLUT_N] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 11, 16, 17, 18, 19, 20, 21, 22, 23,
                                                0 };
static int wf_flut_step(int d)
{
    int set = wf_out == WF_OUT_LLUT, cur = set ? wf_cur() : WF_CURVE_VLOG, pos = 0, n = WF_FLUT_N + 1;
    int own = set && wf_flut > 0 && wf_flut < WF_FLUT_N && cur != WF_FLUT_CURVE(wf_flut);   /* a curve set by hand */
    if (set)
        for (int i = 0; i < WF_FLUT_N; i++)
            if (wf_flut_cyc[i] == wf_flut)
                pos = i + 1;
    int p0 = pos, curve = cur;
    for (int i = 0; i < n; i++) {
        pos = (pos + (d < 0 ? n - 1 : 1)) % n;
        if (pos == 0)
            break;
        int l = wf_flut_cyc[pos - 1];
        curve = own || !l ? cur : WF_FLUT_CURVE(l);
        if (wf_cube_on_card(curve, l))
            break;
    }
    if (pos == p0)
        return 0;
    if (pos == 0) {
        wf_out = wf_out_prev == WF_OUT_LLUT ? WF_OUT_FILM : wf_out_prev;
    } else {
        if (!set) {
            wf_out_prev = wf_out;
            wf_out = WF_OUT_LLUT;
        }
        wf_curve = curve;
        wf_flut = wf_flut_cyc[pos - 1];
        wf_flut_style(wf_flut);
    }
    return 1;
}

int wf_row_step(int row, int delta)
{
    int k = wf_cat_index(wf_film_id), ch = 1;
    switch (row) {
    case WF_ROW_LIGHT:
        if (!wf_cam_ok)
            return 0;
        wf_light = wf_wrap(wf_light, delta, WF_NLIGHT);
        wf_light_req = 1;
        break;
    case WF_ROW_PRINT: {
        int v = wf_print_eff(k), v0 = v;
        for (int i = 0; i < WF_NPRINTS; i++) {
            v = wf_wrap(v, delta, WF_NPRINTS);
            if (wf_print_ok(k, v))
                break;
        }
        if (v == v0 || !wf_print_ok(k, v))
            return 0;
        wf_print = v;
        break;
    }
    case WF_ROW_GRAIN:    wf_grain = wf_wrap(wf_grain, delta, WF_GRAIN_MAX + 1); break;
    case WF_ROW_HALO:     wf_halo = wf_wrap(wf_halo, delta, WF_HALO_MAX + 1); break;
    case WF_ROW_MIST:
        if (!WF_MIST_CAN)
            return 0;
        wf_mist = wf_wrap(wf_mist, delta, WF_MIST_MAX + 1);
        break;
    case WF_ROW_STRENGTH: {
        int s = wf_strength + (delta < 0 ? -1 : 1);       /* a bar: no wrap */
        if (s < 0 || s >= WF_NSTRENGTH)
            return 0;
        wf_strength = s;
        break;
    }
    case WF_ROW_HL:
    case WF_ROW_SH: {
        volatile int *p = row == WF_ROW_HL ? &wf_hl : &wf_sh;
        int v = wf_hs_in(*p) + (delta < 0 ? -1 : 1);     /* -5 .. +5, no wrap */
        if (v < WF_HS_MIN || v > WF_HS_MAX)
            return 0;
        *p = v;
        break;
    }
    case WF_ROW_FLUT:
        ch = wf_flut_step(delta);
        break;
    default:
        return 0;
    }
    if (ch)
        wf_dirty();
    return ch;
}

int wf_row_level(int row, int *max)
{
    if (row == WF_ROW_STRENGTH) {
        if (max)
            *max = WF_NSTRENGTH - 1;
        int s = wf_strength;
        return s >= 0 && s < WF_NSTRENGTH ? s : 1;
    }
    if (max)
        *max = 0;
    return -1;
}

/* ------------------------------------------------------------------ Light: white balance */
static int wf_wb_saved, wf_wb_mode, wf_wb_kelvin, wf_wb_gm, wf_wb_ba;
static int wf_light_k;                            /* kelvin we set last, 0 none */

static int wf_light_kelvin(int light)
{
    int k = wf_cat_index(wf_film_id);
    int day = k >= 0 && wf_films[k].kelvin >= 2000 && wf_films[k].kelvin <= 12000 ? wf_films[k].kelvin : 5200;
    switch (light) {
    case WF_LIGHT_DAY:      return day;
    case WF_LIGHT_CLOUDY:   return (day * 5 / 4 + 50) / 100 * 100;
    case WF_LIGHT_TUNGSTEN: return 3200;
    }
    return 0;
}

static void wf_wb_restore(const char *why)
{
    if (!wf_wb_saved)
        return;
    wf_wb_saved = 0;
    wf_light_k = 0;
    if (wf_wb_mode == WB_KELVIN) {
        lens_set_kelvin(wf_wb_kelvin);
    } else {
        int m = wf_wb_mode;
        prop_request_change(PROP_WB_MODE_LV, &m, 4);
        prop_request_change(PROP_WB_MODE_PH, &m, 4);
        msleep(20);
    }
    lens_set_wbs_gm(wf_wb_gm);
    lens_set_wbs_ba(wf_wb_ba);
    wf_log("light: user WB back (%s): mode %d %dK shift %d/%d", why, wf_wb_mode, wf_wb_kelvin, wf_wb_gm, wf_wb_ba);
}

static void wf_light_poll(void)
{
    if (!wf_light_req || !wf_cam_ok || wf_unloading)
        return;
    wf_light_req = 0;
    int K = wf_out == WF_OUT_OFF ? 0 : wf_light_kelvin(wf_light);
    if (!K) {
        if (wf_wb_saved) {
            wf_wb_restore(wf_out == WF_OUT_OFF ? "output Off" : "Auto");
            wf_dirty();                           /* userwb leaves the file */
        }
        return;
    }
    if (!wf_wb_saved) {
        wf_wb_mode = lens_info.wb_mode;
        wf_wb_kelvin = lens_info.kelvin;
        wf_wb_gm = lens_info.wbs_gm;
        wf_wb_ba = lens_info.wbs_ba;
        wf_wb_saved = 1;
        wf_dirty();                               /* userwb goes into the file */
    }
    if (K != wf_light_k || lens_info.wb_mode != WB_KELVIN || (int)lens_info.kelvin != K) {
        lens_set_kelvin(K);
        wf_light_k = K;
        wf_log("light %s: %dK (user WB mode %d %dK kept)", wf_light_name[wf_light], K, wf_wb_mode, wf_wb_kelvin);
    }
}

/* ------------------------------------------------------------------ WINDYFLM.CFG */
/* line l starts with key '=': the value, else 0 */
static const char *wf_cfg_val(const char *l, const char *key)
{
    int i = 0;
    while (*l == ' ')
        l++;
    for (; key[i]; i++)
        if (l[i] != key[i])
            return 0;
    while (l[i] == ' ')
        i++;
    if (l[i] != '=')
        return 0;
    for (i++; l[i] == ' '; i++)
        ;
    return l + i;
}

static int wf_cfg_end(char c) { return c == 0 || c == '\n' || c == '\r' || c == ' ' || c == ','; }
static int wf_lc(char c) { return c >= 'A' && c <= 'Z' ? c | 0x20 : c; }

/* the value is one of the n words (case-blind): its index, else def */
static int wf_cfg_word(const char *v, const char *const *w, int n, int def)
{
    for (int e = 0; e < n; e++) {
        int i = 0;
        while (w[e][i] && wf_lc(v[i]) == wf_lc(w[e][i]))
            i++;
        if (!w[e][i] && wf_cfg_end(v[i]))
            return e;
    }
    return def;
}

/* a signed decimal (at most 5 digits), *ok = 0 when there is none */
static int wf_cfg_int(const char **pv, int *ok)
{
    const char *v = *pv;
    int neg = *v == '-', d = 0, m = 0;
    if (*v == '-' || *v == '+')
        v++;
    for (; v[d] >= '0' && v[d] <= '9' && d < 5; d++)
        m = m * 10 + (v[d] - '0');
    *ok = d > 0 && wf_cfg_end(v[d]);
    *pv = v + d;
    return neg ? -m : m;
}

static int wf_flut_by_code(const char *v)
{
    if (wf_cfg_word(v, (const char *const[]){ "technical" }, 1, -1) == 0)
        return 0;
    return wf_cfg_word(v, wf_flut_code + 1, WF_FLUT_N - 1, -1) + 1;    /* -1 + 1 = 0: unknown = Technical */
}

void wf_cfg_load(void)
{
    char b[WF_CFG_MAX + 1];
    uint32_t sz = 0;
    wf_cfg_read = 1;
    if (FIO_GetFileSize(WF_CFG, &sz) != 0 || sz == 0 || sz > WF_CFG_MAX)
        return;
    FILE *f = FIO_OpenFile(WF_CFG, O_RDONLY | O_SYNC);
    if (!f)
        return;
    int n = FIO_ReadFile(f, b, sz);
    FIO_CloseFile(f);
    if (n <= 0 || n > WF_CFG_MAX)
        return;
    b[n] = 0;
    for (const char *l = b; *l; ) {
        const char *v;
        int ok, x;
        if ((v = wf_cfg_val(l, "film"))) {
            x = wf_cfg_int(&v, &ok);
            if (ok && (x == 0 || wf_cat_index(x) >= 0))
                wf_film_id = x;
        } else if ((v = wf_cfg_val(l, "output"))) {
            wf_out = wf_cfg_word(v, wf_out_key, WF_NOUT, wf_out);
        } else if ((v = wf_cfg_val(l, "curve"))) {
            wf_curve = wf_cfg_word(v, wf_curve_key, WF_NCURVE, wf_curve - 1) + 1;
        } else if ((v = wf_cfg_val(l, "flut"))) {
            wf_flut = wf_flut_by_code(v);
        } else if ((v = wf_cfg_val(l, "lutei"))) {
            x = wf_cfg_int(&v, &ok);
            if (ok && x >= WF_LL_EI_MIN && x <= WF_LL_EI_MAX)
                wf_lut_ei = x;
        } else if ((v = wf_cfg_val(l, "incurve"))) {
            wf_incurve = wf_cfg_word(v, wf_inc_key, WF_INC_N, WF_INC_SRGB);
        } else if ((v = wf_cfg_val(l, "print"))) {
            wf_print = wf_cfg_word(v, wf_print_key, WF_NPRINTS, WF_PRINT_SCAN);
        } else if ((v = wf_cfg_val(l, "light"))) {
            wf_light = wf_cfg_word(v, wf_light_key, WF_NLIGHT, WF_LIGHT_AUTO);
        } else if ((v = wf_cfg_val(l, "strength"))) {
            wf_strength = wf_cfg_word(v, wf_strength_key, WF_NSTRENGTH, 1);
        } else if ((v = wf_cfg_val(l, "hl"))) {
            x = wf_cfg_int(&v, &ok);
            if (ok && x >= WF_HS_MIN && x <= WF_HS_MAX)
                wf_hl = x;
        } else if ((v = wf_cfg_val(l, "sh"))) {
            x = wf_cfg_int(&v, &ok);
            if (ok && x >= WF_HS_MIN && x <= WF_HS_MAX)
                wf_sh = x;
        } else if ((v = wf_cfg_val(l, "grain"))) {
            wf_grain = wf_cfg_word(v, wf_grain_key, WF_GRAIN_MAX + 1, WF_GRAIN_DEF);
        } else if ((v = wf_cfg_val(l, "halation"))) {
            wf_halo = wf_cfg_word(v, wf_halo_key, WF_HALO_MAX + 1, WF_HALO_DEF);
        } else if ((v = wf_cfg_val(l, "mist"))) {
            wf_mist = WF_MIST_CAN ? wf_cfg_word(v, wf_mist_key, WF_MIST_MAX + 1, 0) : 0;
        } else if ((v = wf_cfg_val(l, "preview"))) {
            wf_pv_mode = wf_cfg_word(v, wf_pv_key, 3, WF_PV_ON);
        } else if ((v = wf_cfg_val(l, "exposure"))) {
            wf_exp_mode = WF_EXP_CAN ? wf_cfg_word(v, wf_exp_key, 2, WF_EXP_CANON) : WF_EXP_CANON;
        } else if ((v = wf_cfg_val(l, "exptrim"))) {
            x = wf_cfg_int(&v, &ok);
            if (ok && x >= -16 && x <= 16)
                wf_exp_trim = x;
        } else if ((v = wf_cfg_val(l, "exif"))) {
            wf_exif_off = wf_cfg_word(v, (const char *const[]){ "on", "off" }, 2, 0);
        } else if ((v = wf_cfg_val(l, "input"))) {
            ;                                     /* reserved: "jpeg" is the only input path */
        } else if ((v = wf_cfg_val(l, "userwb"))) {
            int m = wf_cfg_int(&v, &ok), ok2 = 0, ok3 = 0, ok4 = 0, kv = 0, gm = 0, ba = 0;
            if (ok && *v == ',') { v++; kv = wf_cfg_int(&v, &ok2); }
            if (ok2 && *v == ',') { v++; gm = wf_cfg_int(&v, &ok3); }
            if (ok3 && *v == ',') { v++; ba = wf_cfg_int(&v, &ok4); }
            if (ok4 && m >= 0 && m <= 20 && kv >= 1500 && kv <= 15000 && gm >= -9 && gm <= 9 && ba >= -9 && ba <= 9) {
                wf_wb_mode = m; wf_wb_kelvin = kv; wf_wb_gm = gm; wf_wb_ba = ba;
                wf_wb_saved = 1;
            }
        }
        while (*l && *l != '\n')
            l++;
        if (*l)
            l++;
    }
    if (wf_out < 0 || wf_out >= WF_NOUT)
        wf_out = WF_OUT_FILM;
}

/* 1 = written; a change during the write marks it dirty again */
static int wf_cfg_save(void)
{
    wf_cfg_dirty = 0;
    if (!wf_cfg_read)
        return 1;
    char b[WF_CFG_MAX];
    int lei = wf_lut_ei_t(), fl = wf_flut > 0 && wf_flut < WF_FLUT_N ? wf_flut : 0;
    int n = snprintf(b, sizeof b,
                     "input=jpeg\nfilm=%d\noutput=%s\ncurve=%s\nflut=%s\nlutei=%d\nincurve=%s\nprint=%s\nlight=%s\n"
                     "strength=%s\nhl=%d\nsh=%d\ngrain=%s\nhalation=%s\nmist=%s\npreview=%s\nexposure=%s\nexptrim=%d\nexif=%s\n",
                     wf_film_id, wf_pick(wf_out_key, WF_NOUT, wf_out), wf_curve_key[wf_cur() - 1],
                     fl ? wf_flut_code[fl] : "technical", lei, wf_pick(wf_inc_key, WF_INC_N, wf_incurve),
                     wf_pick(wf_print_key, WF_NPRINTS, wf_print),
                     wf_pick(wf_light_key, WF_NLIGHT, wf_light), wf_pick(wf_strength_key, WF_NSTRENGTH, wf_strength),
                     wf_hl_t(), wf_sh_t(), wf_pick(wf_grain_key, WF_GRAIN_MAX + 1, wf_grain), wf_pick(wf_halo_key, WF_HALO_MAX + 1, wf_halo),
                     wf_pick(wf_mist_key, WF_MIST_MAX + 1, wf_mist), wf_pick(wf_pv_key, 3, wf_pv_mode),
                     wf_pick(wf_exp_key, 2, wf_exp_mode), wf_exp_trim >= -16 && wf_exp_trim <= 16 ? (int)wf_exp_trim : 0,
                     wf_exif_off ? "off" : "on");
    if (wf_wb_saved && n > 0 && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, "userwb=%d,%d,%d,%d\n", wf_wb_mode, wf_wb_kelvin, wf_wb_gm, wf_wb_ba);
    if (n <= 0 || n >= (int)sizeof b)
        return 1;
    struct wf_fe e;
    FILE *f = wf_fcreate(WF_CFG, &e);            /* only while Canon is idle, never over the old file (wf_fio.h) */
    int ok = f && FIO_WriteFile(f, b, n) == n;
    if (f)
        FIO_CloseFile(f);
    if (!ok) {
        wf_cfg_t = get_ms_clock();               /* Canon / the card busy: again in 1.5 s */
        wf_cfg_dirty = 1;
    }
    return ok;
}

/* ------------------------------------------------------------------ EV of the shot (effects' highlight gain) */
/* EV100 x 8: M with a set ISO: the settings ('M'); metered: the LiveView frame's exposure minus the compensation where the
 * frame parameters exist ('F', EOS M), else lens_info's shot values ('L'); WF_EV_BAD when nothing valid is known */
static int wf_ev_ok(int av, int tv, int sv) { return av >= 8 && av <= 100 && tv >= 8 && tv <= 200 && sv >= 16 && sv <= 160; }
int wf_shot_ev8(int *src)
{
    int av = lens_info.raw_aperture, tv = lens_info.raw_shutter, iso = lens_info.raw_iso;
    int sv = iso ? iso : lens_info.raw_iso_auto, ec = lens_info.ae;
    if (shooting_mode == 3 && iso && wf_ev_ok(av, tv, sv)) {
        *src = 'M';
        return av + tv - sv + 8;
    }
    if (wf_exp_vpp) {
        const volatile uint8_t *vp = *(const volatile uint8_t *volatile *)wf_exp_vpp;
        if (vp && wf_ev_ok(vp[1], vp[2], vp[0]) && ec >= -64 && ec <= 64) {
            *src = 'F';
            return vp[1] + vp[2] - vp[0] + 8 - ec;
        }
    }
    if (wf_ev_ok(av, tv, sv)) {
        *src = 'L';
        return av + tv - sv + 8;
    }
    *src = '?';
    return WF_EV_BAD;
}

/* ------------------------------------------------------------------ EXIF UserComment
 * After a shot of ours the JPEG's empty UserComment (tag 0x9286, type UNDEFINED, 264 zero bytes on Canon bodies) gets
 * "ASCII\0\0\0" + one line of text: an in-place same-size overwrite, read back, never a rewrite. The JPEG is written
 * after our hook returns, so wf_task does it: one entry per JPEG the hooks saw (ring of 8, empty text = a JPEG without
 * a look, only kept for the numbering), written when Canon is idle (job_state 0) and the newest file is stable over two
 * polls, newest entry -> newest IMG_nnnn.JPG, a missing number (RAW only) skipped. */
#define WF_EX_N 8
#define WF_EX_T 200
static char wf_ex_q[WF_EX_N][WF_EX_T];
static unsigned wf_ex_h, wf_ex_t;
static int wf_ex_lastf = -1, wf_ex_sz = -1, wf_ex_stable, wf_ex_last, wf_ex_born, wf_ex_tries, wf_ex_cur = -1;

static void wf_ex_push(const char *t)
{
    if (wf_exif_off)
        return;
    if (wf_ex_h == wf_ex_t)
        wf_ex_born = get_ms_clock();
    if (wf_ex_h - wf_ex_t >= WF_EX_N)
        wf_ex_t++;                                /* the oldest goes: the newest stay aligned with the files */
    char *d = wf_ex_q[wf_ex_h % WF_EX_N];
    int i = 0;
    for (; t[i] && i < WF_EX_T - 1; i++)
        d[i] = t[i];
    d[i] = 0;
    wf_ex_h++;
}

void wf_exif_none(void) { wf_ex_push(""); }

/* "WindyFilm: Portra 400 | Print Cine | Light Day | Strength 100% | Grain Normal | Halation Film + bloom | Mist 1/8 | EV12"
 * g / h / m: the grain / halation / mist setting when it ran on this shot, 0 = it did not */
void wf_exif_shot(const struct wf_look *lk, int g, int h, int m, int ev)
{
    char b[WF_EX_T], t[48];
    int n = snprintf(b, sizeof b, "WindyFilm: %s", wf_look_title(lk, t, sizeof t));
    if (lk->output == WF_OUT_FILM && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | Print %s | Strength %d%%", wf_print_name[lk->variant < WF_NPRINTS ? lk->variant : 0],
                      wf_strength_pct(lk->strength));
    if (lk->output == WF_OUT_LLUT && lk->row >= 0 && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | %s", wf_out_name(WF_OUT_LLUT, lk->curve));
    if (lk->output == WF_OUT_LLUT && lk->lei && lk->lei >= WF_LL_EI_MIN && lk->lei <= WF_LL_EI_MAX && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | LUT EI %s", wf_lei_name[lk->lei - WF_LL_EI_MIN]);
    if (lk->output == WF_OUT_LLUT && lk->incurve > WF_INC_SRGB && lk->incurve < WF_INC_N && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | In %s", wf_inc_name[lk->incurve]);
    if (lk->hl && (lk->output == WF_OUT_FILM || lk->output == WF_OUT_LLUT) && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | Highlights %s", wf_hs_text(lk->hl));
    if (lk->sh && (lk->output == WF_OUT_FILM || lk->output == WF_OUT_LLUT) && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | Shadows %s", wf_hs_text(lk->sh));
    if (lk->light && lk->light < WF_NLIGHT && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | Light %s", wf_light_name[lk->light]);
    if (g > 0 && g <= WF_GRAIN_MAX && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | Grain %s", wf_grain_name[g]);
    if (h > 0 && h <= WF_HALO_MAX && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, " | Halation %s", wf_halo_name[h]);
    if (m > 0 && m <= WF_MIST_MAX && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, m > 4 ? " | Black Mist %s" : " | Mist %s", wf_mist_name[m] + (m > 4 ? 6 : 0));
    if (ev != WF_EV_NONE && n < (int)sizeof b)
        snprintf(b + n, sizeof b - n, " | EV%d", ev);
    wf_ex_push(b);
}

static int wf_ex_rd(FILE *f, uint32_t off, void *b, int n)
{
    return FIO_SeekSkipFile(f, off, SEEK_SET) >= 0 && FIO_ReadFile(f, b, n) == n;
}
static uint32_t wf_ex_u(const uint8_t *p, int n, int le)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v = (v << 8) | p[le ? n - 1 - i : i];
    return v;
}

/* tag in the IFD at TIFF offset ifd: e[0] type, e[1] count, e[2] value / offset; 0 = not there */
static int wf_ex_find(FILE *f, uint32_t tiff, int le, uint32_t ifd, unsigned tag, uint32_t e[3])
{
    uint8_t b[96];
    if (ifd < 8 || ifd > 0xF000 || !wf_ex_rd(f, tiff + ifd, b, 2))
        return 0;
    int cnt = (int)wf_ex_u(b, 2, le);
    if (cnt > 64)
        cnt = 64;
    for (int i = 0; i < cnt; i += 8) {
        int k = cnt - i < 8 ? cnt - i : 8;
        if (!wf_ex_rd(f, tiff + ifd + 2 + i * 12, b, k * 12))
            return 0;
        for (int j = 0; j < k; j++)
            if (wf_ex_u(b + j * 12, 2, le) == tag) {
                e[0] = wf_ex_u(b + j * 12 + 2, 2, le);
                e[1] = wf_ex_u(b + j * 12 + 4, 4, le);
                e[2] = wf_ex_u(b + j * 12 + 8, 4, le);
                return 1;
            }
    }
    return 0;
}

/* 0 = written and verified; "busy" = would not open (retry); else why the file was left alone */
const char *wf_ex_write(const char *path, const char *txt)
{
    FILE *f = FIO_OpenFile(path, O_RDWR | O_SYNC);
    if (!f)
        return "busy";
    const char *why = 0;
    uint8_t b[96];
    uint32_t pos = 2, tiff = 0, len = 0, e[3] = { 0, 0, 0 };
    if (!wf_ex_rd(f, 0, b, 2) || b[0] != 0xFF || b[1] != 0xD8)
        why = "not a jpeg";
    for (int i = 0; !why && !tiff; i++) {        /* markers up to the Exif APP1 */
        if (i >= 8 || pos > 0xF000 || !wf_ex_rd(f, pos, b, 10)) { why = "no exif"; break; }
        len = wf_ex_u(b + 2, 2, 0);
        if (b[0] != 0xFF || b[1] == 0xDA || b[1] == 0xD9)
            why = "no exif";
        else if (b[1] == 0xE1 && b[4] == 'E' && b[5] == 'x' && b[6] == 'i' && b[7] == 'f' && !b[8] && !b[9])
            tiff = pos + 10;
        else
            pos += 2 + len;
    }
    int le = 0;
    uint32_t lim = len > 8 ? len - 8 : 0;
    if (!why && (!wf_ex_rd(f, tiff, b, 8) || !((b[0] == 'I' && b[1] == 'I') || (b[0] == 'M' && b[1] == 'M'))))
        why = "no tiff";
    if (!why) {
        le = b[0] == 'I';
        if (wf_ex_u(b + 2, 2, le) != 42)
            why = "no tiff";
    }
    uint32_t ifd0 = why ? 0 : wf_ex_u(b + 4, 4, le);
    if (!why && (!wf_ex_find(f, tiff, le, ifd0, 0x8769, e) || e[0] != 4))
        why = "no exif ifd";
    if (!why) {
        uint32_t xo = e[2];
        if (!wf_ex_find(f, tiff, le, xo, 0x9286, e))
            why = "no comment field";
        else if (e[0] != 7 || e[1] < 8 + 16 || e[1] > 4096 || e[2] < 8 || e[2] + e[1] > lim)
            why = "comment field odd";
    }
    int tl = 0;
    while (txt[tl])
        tl++;
    if (!why && 8 + tl > (int)e[1])
        why = "text too long";
    uint32_t at = tiff + e[2], cn = e[1];
    for (uint32_t o = 0; !why && o < cn; o += 64) {
        int k = cn - o < 64 ? (int)(cn - o) : 64;
        if (!wf_ex_rd(f, at + o, b, k))
            why = "read failed";
        for (int i = 0; !why && i < k; i++)
            if (b[i])
                why = "comment not empty";
    }
    if (!why) {
        uint8_t w[8 + WF_EX_T];
        static const uint8_t hdr[8] = { 'A', 'S', 'C', 'I', 'I', 0, 0, 0 };
        for (int i = 0; i < 8; i++)
            w[i] = hdr[i];
        for (int i = 0; i < tl; i++)
            w[8 + i] = (uint8_t)txt[i];
        if (FIO_SeekSkipFile(f, at, SEEK_SET) < 0 || FIO_WriteFile(f, w, 8 + tl) != 8 + tl)
            why = "write failed";
        for (int o = 0; !why && o < 8 + tl; o += 64) {
            int k = 8 + tl - o < 64 ? 8 + tl - o : 64;
            if (!wf_ex_rd(f, at + o, b, k))
                why = "verify read failed";
            for (int i = 0; !why && i < k; i++)
                if (b[i] != w[o + i])
                    why = "verify differs";
        }
    }
    FIO_CloseFile(f);
    return why;
}

static void wf_ex_path(char *p, int n, int num)
{
    snprintf(p, n, "%s/%s%04d.JPG", get_dcim_dir(), get_file_prefix(), num);
}

static void wf_ex_drop(const char *why)
{
    for (; wf_ex_t != wf_ex_h; wf_ex_t++)
        if (wf_ex_q[wf_ex_t % WF_EX_N][0])
            wf_log("exif skip %s", why);
    wf_ex_cur = -1;
    wf_ex_tries = 0;
}

static void wf_exif_poll(void)
{
    if (wf_ex_t == wf_ex_h) {
        wf_ex_lastf = wf_ex_sz = -1;
        wf_ex_cur = -1;
        return;
    }
    if (wf_exif_off) {
        wf_ex_t = wf_ex_h;
        return;
    }
    int now = get_ms_clock();
    if (now - wf_ex_last < 300)
        return;
    wf_ex_last = now;
    if (now - wf_ex_born > 120000) {
        wf_ex_drop("timeout");
        return;
    }
    if (lens_info.job_state || wf_shot_pending()) {
        wf_ex_stable = 0;
        return;
    }
    int F = get_shooting_card()->file_number, sz = -1;
    if (F < 1) {
        static int said;
        if (!said)
            wf_log("exif skip no file number");
        said = 1;
        wf_ex_t = wf_ex_h;
        return;
    }
    uint32_t s32 = 0;
    char path[80];
    wf_ex_path(path, sizeof path, F);
    if (FIO_GetFileSize(path, &s32) == 0)
        sz = (int)s32;
    if (F != wf_ex_lastf || sz != wf_ex_sz) {
        wf_ex_lastf = F;
        wf_ex_sz = sz;
        wf_ex_stable = 0;
        wf_ex_cur = -1;
        wf_ex_tries = 0;
        return;
    }
    if (++wf_ex_stable < 2)
        return;
    int total = (int)(wf_ex_h - wf_ex_t), seen = 0;
    int num = wf_ex_cur > 0 ? wf_ex_cur : F;
    while (wf_ex_h != wf_ex_t && seen < total + 4 && num >= 1) {
        char *txt = wf_ex_q[(wf_ex_h - 1) % WF_EX_N];
        uint32_t fs = 0;
        wf_ex_path(path, sizeof path, num);
        const char *nm = path;
        for (const char *c = path; *c; c++)
            if (*c == '/')
                nm = c + 1;
        seen++;
        if (FIO_GetFileSize(path, &fs) != 0 || fs == 0) {   /* no JPG with this number: a RAW-only shot */
            num--;
            continue;
        }
        if (txt[0]) {
            const char *why = wf_ex_write(path, txt);
            if (why && why[0] == 'b') {
                wf_ex_cur = num;
                if (++wf_ex_tries >= 20) {
                    wf_log("exif %s skip busy", nm);
                    wf_ex_h--;
                    wf_ex_drop("busy");
                }
                return;
            }
            wf_log(why ? "exif %s skip %s" : "exif %s ok", nm, why);
        }
        wf_ex_h--;
        num--;
        wf_ex_tries = 0;
    }
    wf_ex_drop("no file");
}

/* ------------------------------------------------------------------ the ML menu */
static MENU_SELECT_FUNC(wf_open_select) { (void)priv; (void)delta; wf_ui_request(); }

static void wf_step_int(volatile int *v, int lo, int hi, int delta)
{
    int x = *v;
    if (x < lo || x > hi) x = lo;
    x += delta < 0 ? -1 : 1;
    *v = x < lo ? hi : x > hi ? lo : x;
    wf_dirty();
}

static MENU_SELECT_FUNC(wf_out_select)
{
    (void)priv;
    wf_step_int(&wf_out, 0, WF_NOUT - 1, delta);
    wf_light_req = 1;
}
static MENU_SELECT_FUNC(wf_curve_select) { (void)priv; wf_step_int(&wf_curve, 1, WF_NCURVE, delta); }
static MENU_SELECT_FUNC(wf_flut_select) { (void)priv; wf_step_int(&wf_flut, 0, WF_FLUT_N - 1, delta); wf_flut_style(wf_flut); }
static MENU_SELECT_FUNC(wf_lei_select) { (void)priv; wf_step_int(&wf_lut_ei, WF_LL_EI_MIN, WF_LL_EI_MAX, delta); }
static MENU_SELECT_FUNC(wf_inc_select)
{
    (void)priv;
    int k = wf_inc_chosen();
    for (int i = 0; i < WF_INC_N; i++) {          /* the next curve built into this build (sRGB always is), wrapping */
        k = (k + (delta < 0 ? WF_INC_N - 1 : 1)) % WF_INC_N;
        if (wf_ll_curve_have(k))
            break;
    }
    if (k != wf_incurve) {
        wf_incurve = k;
        wf_dirty();
    }
}
static MENU_SELECT_FUNC(wf_light_select) { (void)priv; wf_row_step(WF_ROW_LIGHT, delta); }
static MENU_SELECT_FUNC(wf_print_select) { (void)priv; wf_row_step(WF_ROW_PRINT, delta); }
static MENU_SELECT_FUNC(wf_grain_select) { (void)priv; wf_row_step(WF_ROW_GRAIN, delta); }
static MENU_SELECT_FUNC(wf_halo_select) { (void)priv; wf_row_step(WF_ROW_HALO, delta); }
static MENU_SELECT_FUNC(wf_mist_select) { (void)priv; wf_row_step(WF_ROW_MIST, delta); }
static MENU_SELECT_FUNC(wf_strength_select)
{
    (void)priv;
    wf_step_int(&wf_strength, 0, WF_NSTRENGTH - 1, delta);
}
static MENU_SELECT_FUNC(wf_hl_select) { (void)priv; wf_row_step(WF_ROW_HL, delta); }
static MENU_SELECT_FUNC(wf_sh_select) { (void)priv; wf_row_step(WF_ROW_SH, delta); }
static MENU_SELECT_FUNC(wf_pv_select)
{
    (void)priv;
    wf_step_int(&wf_pv_mode, WF_PV_OFF, WF_PV_FAST, delta);
    wf_pv_gen++;
}
static MENU_SELECT_FUNC(wf_exp_select)
{
    (void)priv; (void)delta;
    if (WF_EXP_CAN) {
        wf_exp_mode = wf_exp_mode == WF_EXP_SMOOTH ? WF_EXP_CANON : WF_EXP_SMOOTH;
        wf_dirty();
    }
}
static MENU_SELECT_FUNC(wf_exif_select) { (void)priv; (void)delta; wf_exif_off = !wf_exif_off; wf_dirty(); }

#define WF_NOT_ACTIVE() MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "%s", *wf_status() ? wf_status() : "Hooks not active (ML/LOGS/WINDYFLM.LOG).")

static MENU_UPDATE_FUNC(wf_top_update)
{
    int id = wf_loaded();
    const struct wf_film *f = wf_film_by_id(id);
    if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (f)
        MENU_SET_VALUE("%s", f->name);
    else
        MENU_SET_VALUE("%s", wf_out == WF_OUT_LLUT && WF_FLUT_DISP(wf_flut) ? "Look on Standard" : wf_out_name(wf_out, wf_curve));
}

static MENU_UPDATE_FUNC(wf_out_update)
{
    struct wf_look lk;
    wf_look_now(&lk);
    if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (wf_out == WF_OUT_FILM && lk.output != WF_OUT_FILM)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "No film loaded: open the browser and Load one. Shots stay plain.");
    else if (wf_out == WF_OUT_FILM && wf_ws_state() < 0)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "ML/DATA/WFFILMS.DAT missing or bad: shots stay plain.");
    else if (wf_out == WF_OUT_FILM)
        MENU_SET_WARNING(MENU_WARN_INFO, "%s at the shot. Picture Style Neutral or Standard, sRGB, JPEG L.",
                         wf_films[lk.row].name);
    else if (wf_out == WF_OUT_LLUT) {
        int c = wf_cur(), s = wf_ll_state(c);
        char nb[WF_LL_NAME];
        const char *nm = wf_loglut_name(nb, c, wf_flut, 0) + 8;
        if (s == -2 || s == -1)
            MENU_SET_WARNING(MENU_WARN_INFO, "Loading ML/DATA/%s (a few seconds for a new cube).", nm);
        else if (s == WF_LL_E_OPEN)
            MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Put the LUT at ML/DATA/%s. Until then photos stay plain.", nm);
        else if (s > 0)
            MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "%s: %s. Photos stay plain.", nm, wf_loglut_errstr(s));
        else
            MENU_SET_WARNING(MENU_WARN_INFO, "%s in camera. Picture Style Neutral, sRGB, JPEG L.", nm);
    } else if (wf_out == WF_OUT_LOG)
        MENU_SET_WARNING(MENU_WARN_INFO, "Flat log JPEG, no LUT (grade on a PC). Picture Style Neutral.");
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "Off: JPEGs as the camera makes them.");
}

static MENU_UPDATE_FUNC(wf_lut_update)
{
    if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (wf_out != WF_OUT_LLUT)
        MENU_SET_WARNING(MENU_WARN_INFO, "Used by the Log + LUT output (and the Film LUT cans).");
    else if (WF_FLUT_VLONLY(wf_flut) && wf_cur() != WF_FLUT_CURVE(wf_flut)
             && !(wf_cur() == WF_CURVE_VLOG && ((wf_cube_vl >> wf_flut) & 1)))
        MENU_SET_WARNING(MENU_WARN_ADVICE, "%s is a %s cube: set Log curve to %s.", wf_flut_name[wf_flut],
                         wf_curve_name[WF_FLUT_CURVE(wf_flut) - 1], wf_curve_name[WF_FLUT_CURVE(wf_flut) - 1]);
    else if (WF_FLUT_DISP(wf_flut))
        MENU_SET_WARNING(lens_info.raw_picstyle == WF_PS_STANDARD || lens_info.raw_picstyle == WF_PS_NEUTRAL ? MENU_WARN_INFO
                         : MENU_WARN_ADVICE, "%s: Picture Style Standard (Neutral is paler), sRGB in, no auto exposure.",
                         wf_flut_name[wf_flut]);
}

static MENU_UPDATE_FUNC(wf_inc_update)
{
    int k = wf_inc_chosen();
    MENU_SET_VALUE("%s", wf_inc_name[k]);
    if (!wf_ll_curve_have(WF_INC_FILM1) && !wf_ll_curve_have(WF_INC_NEUTRAL))
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "This build has no Canon curve table: sRGB only.");
    else if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (wf_out != WF_OUT_LLUT)
        MENU_SET_WARNING(MENU_WARN_INFO, "Used by the Log + LUT output (and the Film LUT cans).");
    else if (WF_FLUT_DISP(wf_flut))
        MENU_SET_WARNING(MENU_WARN_INFO, "%s always reads the JPEG as sRGB (its cube is made for that).", wf_flut_name[wf_flut]);
    else if (k && wf_style_why(k) == WF_PSW_STYLE)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Picture Style is %s, not %s: read as sRGB.", picstyle_get_current_name(),
                         k == WF_INC_FILM1 ? "User Def. 1" : "Neutral");
    else if (k && wf_style_why(k) == WF_PSW_BASE)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "User Def. 1 is a built-in style, not the Film 1 file: read as sRGB.");
    else if (k && wf_style_why(k) == WF_PSW_CONTRAST)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Style contrast is %d, measured at %d: read as sRGB.", wf_style_con(),
                         wf_style_con_of(k));
    else if (k == WF_INC_FILM1)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Needs Film 1 loaded in User Def. 1, contrast -2 (now: %s).",
                         picstyle_get_current_name());
    else if (k == WF_INC_NEUTRAL)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Needs Picture Style Neutral, contrast 0 (now: %s).", picstyle_get_current_name());
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "The JPEG is read as sRGB (any Picture Style).");
}

static MENU_UPDATE_FUNC(wf_row_update)
{
    int row = (int)(intptr_t)entry->priv - 1;     /* priv = row + 1 for the row entries */
    MENU_SET_VALUE("%s", wf_row_text(row));
    if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (!wf_row_avail(row))
        MENU_SET_WARNING(MENU_WARN_INFO, "No effect with the current output (or camera).");
    else if (row == WF_ROW_GRAIN || row == WF_ROW_HALO || row == WF_ROW_MIST) {
        int s = wf_fx_state(row == WF_ROW_GRAIN ? WF_FX_GRN : row == WF_ROW_HALO ? WF_FX_HAL : WF_FX_MST);
        if (s == WF_FXS_LOADING)
            MENU_SET_WARNING(MENU_WARN_INFO, "Being prepared (a few s); shots meanwhile go without it.");
        else if (s == WF_FXS_NOMEM)
            MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Not enough memory: off, retried every 10 s.");
        else if (s == WF_FXS_ASPECT)
            MENU_SET_WARNING(MENU_WARN_ADVICE, "Halation needs the 3:2 aspect.");
        else if (s == WF_FXS_NOLUT)
            MENU_SET_WARNING(MENU_WARN_ADVICE, "No cube loaded for Log + LUT.");
    }
}

static MENU_UPDATE_FUNC(wf_pv_update)
{
    int s = wf_pv_state();
    MENU_SET_VALUE("%s", wf_pick(wf_pv_name, 3, wf_pv_mode));
    if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (wf_pv_mode == WF_PV_OFF)
        MENU_SET_WARNING(MENU_WARN_INFO, "Off: LiveView shows plain colour; the look is in the JPEG only.");
    else if (!(get_global_draw_setting() & 1))
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Turn on Overlay > Global Draw: ML runs the preview only with it.");
    else if (lv && !get_global_draw())
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Global Draw is on, overlays hidden now: press INFO (outside ML menu).");
    else if (s == WF_PV_PLAY)
        MENU_SET_WARNING(MENU_WARN_INFO, "Playback: preview tables given back, set up again in LiveView.");
    else if (s == WF_PV_MOVIE)
        MENU_SET_WARNING(MENU_WARN_INFO, "Movie mode: videos get no look, LiveView stays plain.");
    else if (s == WF_PV_NOLOOK)
        MENU_SET_WARNING(MENU_WARN_INFO, "No look now: LiveView stays plain.");
    else if (s == WF_PV_GATE)
        MENU_SET_WARNING(MENU_WARN_INFO, "Shots get no look now (JPEG M/S, RAW only, HDR...): no preview.");
    else if (s == WF_PV_NOMEM)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "No memory for the preview tables: set this Off, then On to retry.");
    else if (s == WF_PV_WAIT)
        MENU_SET_WARNING(MENU_WARN_INFO, "Preparing the preview tables...");
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "LiveView shows the look (colour only, no grain / halation).");
}

static MENU_UPDATE_FUNC(wf_exp_update)
{
    MENU_SET_VALUE("%s", wf_exp_mode == WF_EXP_SMOOTH ? "Smooth" : "Camera");
    if (!WF_EXP_CAN)
        MENU_SET_WARNING(MENU_WARN_INFO, "EOS M only.");
    else if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (wf_exp_mode != WF_EXP_SMOOTH)
        MENU_SET_WARNING(MENU_WARN_INFO, "Camera: LiveView follows the camera's Exposure simulation.");
    else if (wf_exp_ctl == WF_EXPC_FAIL)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "ExpSim stays on: set Exposure simulation to Disable (camera menu).");
    else if (wf_exp_flash_last)
        MENU_SET_WARNING(MENU_WARN_INFO, "Shot far darker than LiveView (flash?): shown without correction.");
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "ExpSim off; LiveView gets the shot's exposure by gain.");
}

static MENU_UPDATE_FUNC(wf_exif_update)
{
    MENU_SET_VALUE("%s", wf_exif_off ? "Off" : "On");
    if (wf_exif_off)
        MENU_SET_WARNING(MENU_WARN_INFO, "Off: the JPEG keeps Canon's empty comment field.");
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "Film, print, light and effects go into the JPEG's EXIF comment.");
}

static MENU_UPDATE_FUNC(wf_status_update)
{
    int ws = wf_ws_state();
    MENU_SET_VALUE("%s", *wf_status() ? wf_status() : !wf_yuv_active() ? "Not active" : ws < 0 ? "No film pack"
                         : ws == 0 ? "Reading pack" : "OK");
    MENU_SET_WARNING(MENU_WARN_INFO, "WindyFilm by WindyStrife, %s. Log: ML/LOGS/WINDYFLM.LOG.", wf_cam_name());
}

#if WF_PRB_CAN
static const char *wf_prb_name[WF_PRB_N] = { "Off", "On", "On + ROM call" };
static MENU_UPDATE_FUNC(wf_prb_update)
{
    if (!wf_prb_can())
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Not on this camera / firmware.");
    else if (wf_probe_mode)
        MENU_SET_WARNING(MENU_WARN_INFO, "Shoot RAW+L (single and burst). Log + ML/LOGS/WFPROBE.BIN.");
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "Test only, off at every boot. The photo is never changed.");
}
#define WF_PRB_ENTRY { .name = "RAW probe", .priv = &wf_probe_mode, .max = WF_PRB_N - 1, .choices = wf_prb_name, \
                       .update = wf_prb_update, .help = "Test: logs where Canon keeps the RAW at the shot." },
#else
#define WF_PRB_ENTRY
#endif

#if WF_RAWB_CAN
/* RAW develop "B" (wf_rawb.c): off at every boot, never saved */
static const char *wf_rawb_name[2] = { "Off", "On (slow)" };
static MENU_UPDATE_FUNC(wf_rawb_update)
{
    int st = wf_rawb_state(), dm = drive_mode;
    if (!wf_rawb)
        MENU_SET_WARNING(MENU_WARN_INFO, "Develops the RAW of a RAW+L shot: about 30 s each. Off at boot.");
    else if (!wf_yuv_active())
        WF_NOT_ACTIVE();
    else if (pic_quality != PICQ_RAW_JPG_LARGE_FINE)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Set image quality to RAW+L: other shots get the normal look.");
    else if (dm != DRIVE_SINGLE && dm != 0x10 && dm != 0x11)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Single shot / self-timer only: bursts get the normal look.");
    else if (st == WF_RB_LOADING)
        MENU_SET_WARNING(MENU_WARN_INFO, "Loading the develop tables...");
    else if (st == WF_RB_NOMEM)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "No memory for the develop tables: normal look.");
    else if (st == WF_RB_ERR)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Tables: %s. Normal look.", wf_loglut_errstr(wf_rawb_err()));
    else if (st == WF_RB_IDLE)
        MENU_SET_WARNING(MENU_WARN_INFO, "Output Off: nothing to develop.");
    else
        MENU_SET_WARNING(MENU_WARN_INFO, "About 30 s per shot. Hold half-shutter to stop.");
}
#define WF_RAWB_ENTRY { .name = "RAW develop", .priv = (void *)&wf_rawb, .max = 1, .choices = wf_rawb_name, \
                        .update = wf_rawb_update, .help = "Slow: the look from Canon's RAW (RAW+L single shots)." },
#else
#define WF_RAWB_ENTRY
#endif

#define WF_ROWE(nm, row, sel, hlp) { .name = nm, .priv = (void *)(intptr_t)((row) + 1), .select = sel, \
                                     .update = wf_row_update, .help = hlp, .icon_type = IT_DICE }
static struct menu_entry wf_menu[] = {
    {
        .name = "WindyFilm",
        .select = menu_open_submenu,
        .update = wf_top_update,
        .help = "Film looks at the shot (WindyStrife). SET: settings; browser from inside.",
        .children = (struct menu_entry[]) {
            { .name = "Open film browser", .select = wf_open_select, .icon_type = IT_ACTION,
              .help = "The film strip: pick a film or Film LUT can, Light, Print, effects." },
            { .name = "Output", .priv = (void *)&wf_out, .max = WF_NOUT - 1, .choices = wf_out_menu,
              .select = wf_out_select, .update = wf_out_update,
              .help = "Film: the loaded film. Log: flat log. Log + LUT: log through a cube." },
            { .name = "Log curve", .priv = (void *)&wf_curve, .min = 1, .max = WF_NCURVE, .choices = wf_curve_name,
              .select = wf_curve_select, .help = "Curve of the Log and Log + LUT outputs." },
            { .name = "Film LUT", .priv = (void *)&wf_flut, .max = WF_FLUT_N - 1, .choices = wf_flut_name,
              .select = wf_flut_select, .update = wf_lut_update,
              .help = "Cube of Log + LUT: Technical = your own <LOG>.CUB in ML/DATA." },
            { .name = "LUT EI", .priv = (void *)&wf_lut_ei, .min = WF_LL_EI_MIN, .max = WF_LL_EI_MAX,
              .choices = wf_lei_name, .select = wf_lei_select,
              .help = "Extra exposure into Log + LUT, like a log EI." },
            { .name = "Input curve", .priv = (void *)&wf_incurve, .max = WF_INC_N - 1, .choices = wf_inc_name,
              .select = wf_inc_select, .update = wf_inc_update,
              .help = "How Log + LUT reads the JPEG: sRGB, or a Canon style undone (Film 1, Neutral)." },
            WF_ROWE("Light", WF_ROW_LIGHT, wf_light_select, "White balance: Auto (yours), the stock's Day, Cloudy, Tungsten."),
            WF_ROWE("Print", WF_ROW_PRINT, wf_print_select, "Scan, cine print or paper print of the loaded film."),
            WF_ROWE("Grain", WF_ROW_GRAIN, wf_grain_select, "Film grain at the shot (films and Log + LUT)."),
            WF_ROWE("Halation", WF_ROW_HALO, wf_halo_select, "Red glow around highlights (3:2)."),
            WF_ROWE("Mist", WF_ROW_MIST, wf_mist_select, "Diffusion filter: glow on lights, veil on shadows."),
            { .name = "Strength", .priv = (void *)&wf_strength, .max = WF_NSTRENGTH - 1, .choices = wf_strength_name,
              .select = wf_strength_select, .help = "How strongly the film look is applied." },
            WF_ROWE("Highlights", WF_ROW_HL, wf_hl_select, "-5..+5: darker / brighter upper tones (films, Log + LUT)."),
            WF_ROWE("Shadows", WF_ROW_SH, wf_sh_select, "-5..+5: deeper / lifted lower tones (films, Log + LUT)."),
            { .name = "LiveView preview", .priv = (void *)&wf_pv_mode, .max = WF_PV_FAST, .choices = wf_pv_name,
              .select = wf_pv_select, .update = wf_pv_update, .help = "The look in LiveView. Needs Overlay > Global Draw." },
            { .name = "Preview exposure", .select = wf_exp_select, .update = wf_exp_update,
              .help = "Smooth: steady LiveView; the shot's exposure is added digitally." },
            { .name = "EXIF info", .select = wf_exif_select, .update = wf_exif_update,
              .help = "Writes the shot's film and effects into the JPEG's EXIF comment." },
            WF_RAWB_ENTRY
            WF_PRB_ENTRY
            { .name = "Status", .update = wf_status_update, .icon_type = IT_ACTION,
              .help = "Camera, hooks and film pack state." },
            MENU_EOL
        },
    },
};

/* ------------------------------------------------------------------ init / poll / deinit */
void wf_menu_init(void)
{
    wf_cfg_load();
    if (!wf_cam_ok)
        wf_wb_saved = 0;                          /* unsupported body: never touch its white balance */
    wf_light_req = 1;
    prop_add_handler(WF_PROP_PS_OF_USER1, (void *)wf_ps_base_prop);   /* Canon reports the value at once */
    menu_add("Shoot", wf_menu, COUNT(wf_menu));
    wf_menu_added = 1;
}

void wf_menu_poll(void)
{
    static int ui_was;
    int ui = wf_ui_is_open();
    if ((ui && !ui_was) || wf_cube_scan_req) {    /* Film LUT cubes: at start and whenever the browser opens */
        wf_cube_scan_req = 0;
        wf_cube_scan();
    }
    ui_was = ui;
    wf_light_poll();
    wf_exif_poll();
    if (wf_cfg_dirty && get_ms_clock() - wf_cfg_t >= 1500)
        wf_cfg_save();
}

void wf_menu_deinit(void)
{
    if (wf_cfg_dirty)
        wf_cfg_save();                            /* with userwb still in it: a lost restore is redone at next boot */
    if (wf_cam_ok)
        wf_wb_restore("unload");
    if (wf_menu_added) {
        menu_remove("Shoot", wf_menu, COUNT(wf_menu));
        wf_menu_added = 0;
    }
}
