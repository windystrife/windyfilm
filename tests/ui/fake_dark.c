/* fake_dark.c - the core side of wf_dark.h for the src/ui host harness: a roll, a darkroom view with prints read from
 * luma files, requests that just record what they were asked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include "../../src/wf_api.h"
#include "../../src/wf_dark.h"
#include "../../src/wf_photo.h"
#include "fake_dark.h"

static struct wf_roll roll;
static struct wf_dark_view view;
static uint32_t rgen = 1;
static int clock_hm = 1432, req_film, req_exp, nstart, nstop;
static uint8_t print_buf[WF_DR_PW * WF_DR_PH];
static uint8_t wire_buf[WF_DR_NWIRE][WF_DR_TW * WF_DR_TH];
volatile int wf_blind = 1;
void wf_blind_set(int on) { wf_blind = on != 0; }
int wf_roll_blocks_play(void);

static int locked, nswap;
static int fshelf, rawb_can = 1;
static struct wf_shelf fshv;                     /* the shelf (owner 11-17): rolls the requests below put on it */
static int nrescan, st_num, st_again, st_film, nswap_devd;
volatile int wf_rawb;
volatile int wf_roll_look8;                      /* a JPEG 8-bit roll (wf_dark.h): the browser sets it */
void wf_roll_look8_set(int on) { wf_roll_look8 = on != 0; }
void fake_set_locked(int k) { locked = k; }
int fake_nswap(void) { return nswap; }
int wf_roll_shutter_locked(void) { return locked; }
int wf_roll_blocks_play(void) { return wf_blind && wf_roll_open(&roll) && !fshelf; }   /* (no darkroom run here) */
void fake_dark_reset(void)
{
    wf_roll_reset(&roll);
    memset(&view, 0, sizeof view);
    view.frame = -1;
    view.battery = -1;
    view.msg = "";
    view.print = print_buf;
    for (int i = 0; i < WF_DR_NWIRE; i++) view.wire[i] = wire_buf[i];
    req_film = req_exp = nstart = nstop = 0;
    locked = nswap = 0;
    fshelf = 0;
    memset(&fshv, 0, sizeof fshv);
    nrescan = st_num = st_again = st_film = nswap_devd = 0;
    wf_rawb = 0;
    wf_roll_look8 = 0;
    rawb_can = 1;
    rgen++;
    fake_photo_reset();
}
struct wf_roll *fake_roll(void) { rgen++; fake_photo_again(); return &roll; }
struct wf_dark_view *fake_view(void) { view.gen++; return &view; }
void fake_set_clock(int hm) { clock_hm = hm; }
int fake_req_film(void) { return req_film; }
int fake_req_exp(void) { return req_exp; }
int fake_nstart(void) { return nstart; }
int fake_nstop(void) { return nstop; }

static int load(const char *path, uint8_t *dst, int n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int k = (int)fread(dst, 1, n, f);
    fclose(f);
    return k == n ? 0 : -1;
}
int fake_load_print(const char *path) { return load(path, print_buf, sizeof print_buf); }
int fake_load_wire(int i, const char *path) { return i >= 0 && i < WF_DR_NWIRE ? load(path, wire_buf[i], WF_DR_TW * WF_DR_TH) : -1; }

/* ------------------------------------------------------------------ wf_dark.h */
const struct wf_roll *wf_roll_now(void) { return &roll; }
uint32_t wf_roll_gen(void) { return rgen; }
int wf_roll_req_load(int film, int exp)
{
    if (wf_roll_open(&roll) && roll.nshot > 0) return -1;
    if (!wf_roll_ok_exp(exp)) return -2;
    req_film = film;
    req_exp = exp;
    wf_roll_load(&roll, film, exp, 100, 1);
    rgen++;
    return 0;
}
int wf_roll_req_rewind(void) { return wf_roll_rewind(&roll); }
int wf_roll_req_close(void) { return wf_roll_close(&roll); }
int wf_roll_req_swap(int film, int exp, int devd)
{
    if (!(roll.state == WF_RS_FULL || (roll.state == WF_RS_REWOUND && wf_roll_left(&roll) > 0))) return -1;
    if (!wf_roll_ok_exp(exp)) return -2;
    if (devd) { wf_roll_close(&roll); nswap_devd++; }
    else wf_roll_rewind(&roll);
    fake_shelf_add(&roll);                       /* the roll goes on the shelf (newest first) */
    if (roll.state != WF_RS_DONE) roll.state = WF_RS_EMPTY;   /* and leaves the camera */
    nswap++;
    return wf_roll_req_load(film, exp);
}
int wf_roll_hud(char *line, int n) { (void)line; (void)n; return 0; }
const struct wf_dark_view *wf_dark_view(void) { return &view; }
int wf_dark_start(void) { nstart++; st_num = 0; return wf_roll_left(&roll) ? 0 : -1; }
int wf_dark_start_roll(int num, int again, int film)
{
    nstart++;
    st_num = num;
    st_again = again;
    st_film = film;
    for (int k = 0; k < fshv.n; k++)
        if (fshv.e[k].num == num) return (again ? fshv.e[k].nshot : fshv.e[k].left) ? 0 : -1;
    return -1;
}
int wf_dark_eta_left(const struct wf_dark_view *v, int left) { return left * (v->avg_ms ? v->avg_ms : 45000) / 1000; }
const struct wf_shelf *wf_shelf_now(void) { return &fshv; }
void wf_shelf_rescan(void) { nrescan++; }
struct wf_shelf *fake_shelf(void) { fshv.gen++; fake_photo_again(); return &fshv; }
void fake_shelf_add(const struct wf_roll *r)
{
    if (fshv.n >= WF_SHELF_MAX) fshv.n = WF_SHELF_MAX - 1;
    memmove(&fshv.e[1], &fshv.e[0], fshv.n * sizeof fshv.e[0]);
    struct wf_shelf_ent *e = &fshv.e[0];
    e->num = r->num; e->dev = r->dev < 2 ? 1 : r->dev; e->film = r->film; e->own = r->own ? r->own : r->film;
    e->exp = r->exp; e->state = r->state; e->nshot = r->nshot; e->left = wf_roll_left(r); e->ndev = wf_roll_ndev(r);
    fshv.n++;
    fshv.gen++;
}
int fake_nrescan(void) { return nrescan; }
int fake_start_num(void) { return st_num; }
int fake_start_again(void) { return st_again; }
int fake_start_film(void) { return st_film; }
int fake_nswap_devd(void) { return nswap_devd; }
void wf_dark_stop(void) { nstop++; view.state = WF_DR_STOPPING; view.gen++; }
int wf_dark_eta_s(const struct wf_dark_view *v)
{
    int per = v->avg_ms ? v->avg_ms : 45000;
    int left = v->state == WF_DR_IDLE ? wf_roll_left(&roll) : v->left;
    int s = (left * per - (v->state == WF_DR_RUN ? v->t_frame : 0)) / 1000;
    return s < 0 ? 0 : s;
}
int wf_clock_hm(void) { return clock_hm; }

/* ------------------------------------------------------------------ print finishes (wf_menu.c's rows, DESIGN 3.9) */
volatile int wf_pr_frame, wf_pr_info = 1, wf_pr_date, wf_pr_jpeg, wf_pr_cr2;
static int clock_ymd = 20261115;
void fake_set_date(int ymd) { clock_ymd = ymd; }
int wf_clock_ymd(void) { return clock_ymd; }
static const char *const pfr_name[5] = { "Off", "White", "Black", "White wide", "Black wide" };
static const char *const pin_name[3] = { "Off", "On", "On + roll" };
const char *wf_popt_name(int k) { static const char *const n[WF_PO_N] = { "Frame", "Info", "Date", "Camera JPEG", "RAW files" }; return k >= 0 && k < WF_PO_N ? n[k] : ""; }
int wf_popt_avail(int k) { return k == WF_PO_INFO ? wf_pr_frame > 0 && wf_pr_frame < 5 : k == WF_PO_RAW ? wf_pr_jpeg > 0 && wf_pr_jpeg < 4 : 1; }
void wf_popt_step(int k, int d)
{
    static const int n[WF_PO_N] = { 5, 3, 4, 4, 2 };
    volatile int *v = k == WF_PO_FRAME ? &wf_pr_frame : k == WF_PO_INFO ? &wf_pr_info : k == WF_PO_JPEG ? &wf_pr_jpeg
                    : k == WF_PO_RAW ? &wf_pr_cr2 : &wf_pr_date;
    if (k < 0 || k >= WF_PO_N || !d) return;
    *v = (*v + (d > 0 ? 1 : n[k] - 1)) % n[k];
}
const char *wf_popt_text(int k, char *b, int n)
{
    if (k == WF_PO_FRAME) return pfr_name[wf_pr_frame % 5];
    if (k == WF_PO_INFO) return pin_name[wf_pr_info % 3];
    if (k == WF_PO_JPEG) return wf_pr_jpeg == 3 ? "Print + over" : wf_pr_jpeg == 2 ? "Over + print" : wf_pr_jpeg ? "Overwrite" : "Keep";
    if (k == WF_PO_RAW) return wf_pr_cr2 ? "Move to ROLLS" : "Keep in DCIM";
    int y = clock_ymd / 10000 % 100, m = clock_ymd / 100 % 100, d = clock_ymd % 100;
    switch (wf_pr_date) {
    case 1: snprintf(b, n, "'%02d %02d %02d", y, m, d); return b;
    case 2: snprintf(b, n, "%02d %02d '%02d", d, m, y); return b;
    case 3: snprintf(b, n, "%02d %02d '%02d", m, d, y); return b;
    }
    return "Off";
}

/* ------------------------------------------------------------------ Screen Off (wf_dark.h, DESIGN 3.11) */
volatile int wf_dk_scr;

/* ------------------------------------------------------------------ the Darkroom's photos (RAWB28): real CR2s through wf_photo.c */
volatile int wf_dk_photo = 1;
void wf_dk_photo_set(int on) { wf_dk_photo = on != 0; }
#define PH_MAX 200
static char ph_dir[400], ph_name[PH_MAX][16];
static int ph_cnt, ph_part = -1, ph_none = -1;
static uint32_t ph_gen;
static struct wf_dark_photos ph;
static uint8_t ph_cell[WF_DK_SHEET * WF_PH_CW * WF_PH_CH], ph_big[WF_PH_BW * WF_PH_BH];
static struct { int st, bw, bh; uint8_t cell[WF_PH_CW * WF_PH_CH], lcell[WF_PH_LW * WF_PH_LH], big[WF_PH_BW * WF_PH_BH]; }
    *ph_cache;                                   /* per file */
static int ph_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
void fake_photo_again(void) { ph.num = -2; }
void fake_photo_reset(void) { wf_dk_photo = 1; ph_part = ph_none = -1; fake_photo_again(); }
void fake_photo_partial(int nread, int none) { ph_part = nread; ph_none = none; fake_photo_again(); }
const struct wf_dark_photos *fake_photos(void) { return &ph; }
void fake_set_photo_dir(const char *d)
{
    snprintf(ph_dir, sizeof ph_dir, "%s", d);
    ph_cnt = 0;
    DIR *dd = opendir(d);
    struct dirent *e;
    while (dd && (e = readdir(dd)) && ph_cnt < PH_MAX)
    {
        size_t n = strlen(e->d_name);
        if (n > 4 && n < 16 && !strcmp(e->d_name + n - 4, ".CR2")) strcpy(ph_name[ph_cnt++], e->d_name);
    }
    if (dd) closedir(dd);
    qsort(ph_name, ph_cnt, sizeof ph_name[0], ph_cmp);
    free(ph_cache);
    ph_cache = calloc(PH_MAX, sizeof *ph_cache);
    fake_photo_again();
}
static int ph_rd(void *ctx, uint32_t pos, void *dst, uint32_t n)   /* wf_photo.h's reader through stdio */
{
    FILE *f = ctx;
    return fseek(f, (long)pos, SEEK_SET) == 0 && fread(dst, 1, n, f) == n ? 0 : -1;
}
/* one CR2 as the camera reads it (wf_photo.c, the same calls as wf_dark.c's loader) */
static int ph_read(const char *path, uint8_t *cell, uint8_t *lcell, uint8_t *big, int *bw, int *bh)
{
    static uint8_t hd[WF_PH_IFDB];
    static uint16_t t16[WF_PH_BW * WF_PH_BH], rows[WF_PH_ROWS];
    static struct wf_ph t;
    struct wf_ph_src s;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int rc = wf_ph_find(ph_rd, f, hd, &s) == 0 && wf_ph_cell(ph_rd, f, &s, &t, WF_PH_CW, WF_PH_CH, rows, t16, cell) == 0
             && wf_ph_cell(ph_rd, f, &s, &t, WF_PH_LW, WF_PH_LH, rows, t16, lcell) == 0
             && wf_ph_big(ph_rd, f, &s, &t, rows, t16, big, bw, bh) == 0 ? 0 : -1;
    fclose(f);
    return rc;
}
/* frame i of a roll = the dir's (i + num x 5)th CR2 (the camera's roll: from the first); read once a file */
const struct wf_dark_photos *wf_dark_photos(int num, int cam)
{
    if (ph.num == num && ph.cam == cam) return &ph;
    memset(&ph, 0, sizeof ph);
    ph.num = num;
    ph.cam = cam;
    ph.cell = ph_cell;
    ph.big = ph_big;
    ph.gen = ++ph_gen;
    int n = 0, first = 0, off = 0;
    if (cam) { n = roll.nshot; first = wf_roll_next_dev(&roll); }
    else
        for (int k = 0; k < fshv.n; k++)
            if (fshv.e[k].num == num) { n = fshv.e[k].nshot; first = n - fshv.e[k].left; off = num * 5; }
    if (!ph_cnt || n <= 0) return &ph;
    ph.n = n > WF_DK_SHEET ? WF_DK_SHEET : n;
    ph.cw = ph.n <= WF_PH_LMAX ? WF_PH_LW : WF_PH_CW;
    ph.ch = ph.n <= WF_PH_LMAX ? WF_PH_LH : WF_PH_CH;
    ph.big_frame = first >= 0 && first < ph.n ? first : 0;
    for (int i = 0; i < ph.n; i++)
    {
        int j = (i + off) % ph_cnt;
        if (ph_part >= 0 && i >= ph_part) continue;      /* WF_PHS_WAIT */
        if (i == ph_none) { ph.st[i] = WF_PHS_NONE; continue; }
        if (!ph_cache[j].st)
        {
            char p[600];
            snprintf(p, sizeof p, "%s/%s", ph_dir, ph_name[j]);
            ph_cache[j].st = ph_read(p, ph_cache[j].cell, ph_cache[j].lcell, ph_cache[j].big, &ph_cache[j].bw,
                                     &ph_cache[j].bh) == 0
                           ? WF_PHS_OK : WF_PHS_NONE;
        }
        ph.st[i] = (uint8_t)ph_cache[j].st;
        memcpy(ph_cell + i * ph.cw * ph.ch, ph.cw == WF_PH_LW ? ph_cache[j].lcell : ph_cache[j].cell, (size_t)ph.cw * ph.ch);
        if (i == ph.big_frame && ph.st[i] == WF_PHS_OK)
        {
            ph.big_st = WF_PHS_OK;
            ph.big_w = ph_cache[j].bw;
            ph.big_h = ph_cache[j].bh;
            memcpy(ph_big, ph_cache[j].big, (size_t)ph.big_w * ph.big_h);
        }
        else if (i == ph.big_frame)
            ph.big_st = ph.st[i];
    }
    return &ph;
}
static int fdark, nwake;
void fake_set_dark(int d) { fdark = d; }
int fake_nwake(void) { return nwake; }
void wf_dk_scr_step(int d) { if (d) wf_dk_scr = !wf_dk_scr; }
static int frun(void) { return view.state == WF_DR_RUN || view.state == WF_DR_STOPPING; }
int wf_dark_wake(int now)
{
    (void)now;
    if (!frun() || wf_dk_scr != WF_DS_OFF || !fdark) return 0;
    fdark = 0;
    nwake++;
    return 1;
}
int wf_dark_dark(int now) { (void)now; return frun() && wf_dk_scr == WF_DS_OFF && fdark; }
static int fkeep;
int wf_dark_keep(int now, int ui) { (void)now; fkeep += ui; return 0; }
int fake_nkeep(void) { return fkeep; }

/* ------------------------------------------------------------------ one screen (wf_dark.h: the roll set aside) */
int wf_roll_shelve(int on) { if (on && !wf_roll_open(&roll)) return -1; fshelf = on ? 1 : 0; rgen++; return 0; }
int wf_roll_shelved(void) { return fshelf && wf_roll_open(&roll); }
void fake_set_shelf(int on) { fshelf = on; rgen++; }

/* RAW develop B's menu toggle (wf_rawb.h; the core's wf_rawb.c is not in the harness) */
int wf_rawb_can(void) { return rawb_can; }     /* as the real builds: B on both cameras (wf_cam.h WF_RAWB_CAN) */
void fake_set_rawb_can(int c) { rawb_can = c; }
