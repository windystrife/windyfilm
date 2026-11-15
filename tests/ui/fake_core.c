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
static int busy_n, busy_from, nreads;      /* Canon holds the card: the next busy_n reads at pos >= busy_from (-1: all) */
static char logbuf[32768];
static int loglen;
static int val[WF_NROWS];

/* the names and choices of src/wf_menu.c (film-stage: print stocks, Grain / Halation with the Even / Wrap choices,
 * Bloom and Gauge; the print stocks' full names of wf_row_text_long) */
static const char *const names[WF_NROWS] = { "Light", "Print stock", "Grain", "Halation", "Bloom", "Gauge", "Mist",
                                             "Strength", "Highlights", "Shadows", "Film LUT" };
static const char *const v_light[] = { "Auto", "Day", "Cloudy", "Tungsten", 0 };
static const char *const v_print[] = { "Scan", "Cine", "Paper", "2302", "2383", "2393", "2395", "5380", "5381/50", "5381/72",
                                       "5382", "5384/82", "5384/92", "5385/93", "5385/62", "5386", "5383", "3513D", "3523XD", 0 };
static const char *const v_print_full[16] = {
    "Kodak B&W Print Film 2302", "Kodak Vision Color 2383", "Kodak Vision Premier Color 2393",
    "Kodak Vision Color Teleprint 2395 500T", "Eastman Color Print LC 5380", "Eastman Color Print 5381 (1950)",
    "Eastman Color Print 5381 (1972)", "Eastman Color Print 5382", "Eastman Color Print 5384 (1982)",
    "Eastman Color Print 5384 (1992)", "Eastman Color Print LC 5385 (1993)", "Eastman Color Print 5385 (1962)",
    "Eastman EXR Color Print 5386", "Eastman Color Print 5383", "Fuji Eterna-CP Type 3513D", "Fuji Eterna-CP 3523XD" };
static const char *const v_grain[] = { "Off", "Fine", "Normal", "Strong", "Even Fine", "Even Normal", "Even Strong", 0 };
static const char *const v_halo[] = { "Off", "Wide", "Tight", "Wrap", 0 };
static const char *const v_bloom[] = { "Off", "Low", "Normal", "Strong", 0 };
static const char *const v_gauge[] = { "16 mm", "35 mm", "65 mm", 0 };
static const char *const v_mist[] = { "Off", "1/8", "1/4", "1/2", "1", "Black 1/8", "Black 1/4", "Black 1/2", "Black 1", 0 };
static const char *const v_flut[] = { "Off", "Vision3 250D", "Vision3 500T", "Portra 400", "Leica Vivid", "Windy 250D P", 0 };
static const char *const v_hs[] = { "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5", 0 };
static const char *const *vals[WF_NROWS] = { v_light, v_print, v_grain, v_halo, v_bloom, v_gauge, v_mist, 0, v_hs, v_hs,
                                             v_flut };
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
    busy_n = 0;
    for (int r = 0; r < WF_NROWS; r++) val[r] = 0;
    val[WF_ROW_GRAIN] = 2;                  /* Normal */
    val[WF_ROW_HALO] = 1;                   /* Wide */
    val[WF_ROW_BLOOM] = 2;                  /* Normal */
    val[WF_ROW_GAUGE] = 1;                  /* 35 mm */
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

const char *wf_row_text_long(int r)
{
    return r == WF_ROW_PRINT && val[r] >= 3 && val[r] < 19 ? v_print_full[val[r] - 3] : wf_row_text(r);
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
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[log] %s\n", line);
    int k = (int)strlen(line);
    if (loglen + k + 2 < (int)sizeof logbuf)
    {
        memcpy(logbuf + loglen, line, k);
        loglen += k;
        logbuf[loglen++] = '\n';
        logbuf[loglen] = 0;
    }
}
void fake_log_clear(void) { loglen = 0; logbuf[0] = 0; }
int fake_log_has(const char *s) { return strstr(logbuf, s) != 0; }

void fake_card_busy(int n, int from_pos) { busy_n = n; busy_from = from_pos; }
int fake_card_reads(void) { return nreads; }

/* as the core's (wf_main.c): WF_RD_* results */
int wf_card_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size)
{
    nreads++;
    if (size) *size = 0;
    if (busy_n && (int)pos >= busy_from)
    {
        if (busy_n > 0) busy_n--;
        return WF_RD_BUSY;
    }
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", card, path);
    FILE *fp = fopen(full, "rb");
    if (!fp) return WF_RD_MISSING;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (size) *size = (uint32_t)sz;
    if ((long)pos + (long)n > sz) { fclose(fp); return WF_RD_SHORT; }
    fseek(fp, pos, SEEK_SET);
    size_t got = fread(p, 1, n, fp);
    fclose(fp);
    return got == n ? WF_RD_OK : WF_RD_IO;
}
