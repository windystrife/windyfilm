/* fake_dark.c - the core side of wf_dark.h for the src/ui host harness: a roll, a darkroom view with prints read from
 * luma files, requests that just record what they were asked. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../src/wf_dark.h"
#include "fake_dark.h"

static struct wf_roll roll;
static struct wf_dark_view view;
static uint32_t rgen = 1;
static int clock_hm = 1432, req_film, req_exp, nstart, nstop;
static uint8_t print_buf[WF_DR_PW * WF_DR_PH];
static uint8_t wire_buf[WF_DR_NWIRE][WF_DR_TW * WF_DR_TH];
volatile int wf_blind = 1;

static int locked, nswap;
void fake_set_locked(int k) { locked = k; }
int fake_nswap(void) { return nswap; }
int wf_roll_shutter_locked(void) { return locked; }
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
    rgen++;
}
struct wf_roll *fake_roll(void) { rgen++; return &roll; }
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
int wf_roll_req_swap(int film, int exp)
{
    if (!(roll.state == WF_RS_FULL || (roll.state == WF_RS_REWOUND && wf_roll_left(&roll) > 0))) return -1;
    if (!wf_roll_ok_exp(exp)) return -2;
    wf_roll_close(&roll);
    nswap++;
    return wf_roll_req_load(film, exp);
}
int wf_roll_hud(char *line, int n) { (void)line; (void)n; return 0; }
const struct wf_dark_view *wf_dark_view(void) { return &view; }
int wf_dark_start(void) { nstart++; return wf_roll_left(&roll) ? 0 : -1; }
void wf_dark_stop(void) { nstop++; view.state = WF_DR_STOPPING; view.gen++; }
int wf_dark_eta_s(const struct wf_dark_view *v)
{
    int per = v->avg_ms ? v->avg_ms : 45000;
    int left = v->state == WF_DR_IDLE ? wf_roll_left(&roll) : v->left;
    int s = (left * per - (v->state == WF_DR_RUN ? v->t_frame : 0)) / 1000;
    return s < 0 ? 0 : s;
}
int wf_clock_hm(void) { return clock_hm; }
