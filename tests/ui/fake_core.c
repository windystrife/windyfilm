/* fake_core.c - the core side of wf_api.h for the src/ui host harness: row values, load, gen, status, the card.
 * The catalogue itself (wf_films, wf_nfilms) is generated from films/catalog.py by mk_fakecat.py. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../src/wf_api.h"
#include "fake_core.h"

static int cam = WF_CAM_EOSM, loaded, gen = 1, grey_mask, missing[8], nmissing;
static const char *status = "";
static char card[512] = ".";
static int val[WF_NROWS];

static const char *const names[WF_NROWS] = { "Light", "Print", "Grain", "Halation", "Mist", "Strength", "Highlights",
                                             "Shadows", "Film LUT" };
static const char *const v_light[] = { "Day", "Tungsten", "Cloudy", "Shade", "Flash", 0 };
static const char *const v_print[] = { "Scan", "Cine", "Paper", 0 };
static const char *const v_grain[] = { "Off", "Fine", "Normal", "Strong", 0 };
static const char *const v_halo[] = { "Off", "Neg", "Strong", 0 };
static const char *const v_mist[] = { "Off", "1/8", "1/4", "1/2", 0 };
static const char *const v_flut[] = { "Off", "Vision3 250D", "Vision3 500T", "Portra 400", 0 };
static const char *const v_hs[] = { "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5", 0 };
static const char *const *vals[WF_NROWS] = { v_light, v_print, v_grain, v_halo, v_mist, 0, v_hs, v_hs, v_flut };
#define STRENGTH_MAX 6                      /* 0 .. 150 %, 25 % steps */

static int nvals(int r)
{
    if (r == WF_ROW_STRENGTH) return STRENGTH_MAX + 1;
    int n = 0;
    while (vals[r][n]) n++;
    return n;
}

void fake_reset(void)
{
    cam = WF_CAM_EOSM; loaded = 0; gen++; grey_mask = 0; nmissing = 0; status = "";
    for (int r = 0; r < WF_NROWS; r++) val[r] = 0;
    val[WF_ROW_GRAIN] = 2;                  /* Normal */
    val[WF_ROW_HALO] = 1;                   /* Neg */
    val[WF_ROW_STRENGTH] = 4;               /* 100 % */
    val[WF_ROW_HL] = val[WF_ROW_SH] = 5;    /* 0 */
}
void fake_set_cam(int c) { cam = c; gen++; }
void fake_set_loaded(int id) { loaded = id; gen++; }
void fake_set_row(int r, int v) { val[r] = v; gen++; }
void fake_set_grey(int mask) { grey_mask = mask; gen++; }
void fake_set_missing(int id) { if (nmissing < 8) missing[nmissing++] = id; gen++; }
void fake_set_status(const char *s) { status = s; gen++; }
void fake_set_card(const char *dir) { snprintf(card, sizeof(card), "%s", dir); }
int  fake_row_value(int r) { return val[r]; }

/* ------------------------------------------------------------------ wf_api.h */
int wf_cam(void) { return cam; }

const struct wf_film *wf_film_by_id(int id)
{
    for (int i = 0; i < wf_nfilms; i++)
        if (wf_films[i].id == id) return &wf_films[i];
    return 0;
}

const char *wf_row_name(int r) { return r >= 0 && r < WF_NROWS ? names[r] : ""; }

const char *wf_row_text(int r)
{
    static char b[8];
    if (r == WF_ROW_STRENGTH) { snprintf(b, sizeof(b), "%d%%", val[r] * 25); return b; }
    return r >= 0 && r < WF_NROWS ? vals[r][val[r]] : "";
}

int wf_row_avail(int r) { return !(grey_mask & (1 << r)); }

int wf_row_step(int r, int d)
{
    int n = nvals(r), v = val[r] + d;
    if (v < 0 || v >= n) return 0;
    val[r] = v;
    gen++;
    return 1;
}

int wf_row_level(int r, int *max)
{
    if (max) *max = STRENGTH_MAX;
    return r == WF_ROW_STRENGTH ? val[r] : 0;
}

int wf_avail(int id)
{
    for (int i = 0; i < nmissing; i++)
        if (missing[i] == id) return 0;
    return wf_film_by_id(id) != 0;
}

int wf_load(int id)
{
    const struct wf_film *f = wf_film_by_id(id);
    if (!f) return WF_LOAD_UNKNOWN;
    if (!wf_avail(id)) return f->flags & WF_F_LUTCAN ? WF_LOAD_NOCUBE : WF_LOAD_NOTABLE;
    loaded = id;
    gen++;
    return WF_LOAD_OK;
}

int wf_loaded(void) { return loaded; }
const char *wf_status(void) { return status; }
uint32_t wf_gen(void) { return gen; }

void wf_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[log] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int wf_card_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size)
{
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", card, path);
    FILE *fp = fopen(full, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (size) *size = (uint32_t)sz;
    if ((long)pos + (long)n > sz) { fclose(fp); return -2; }
    fseek(fp, pos, SEEK_SET);
    size_t got = fread(p, 1, n, fp);
    fclose(fp);
    return got == n ? 0 : -3;
}
