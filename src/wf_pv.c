/* wf_pv.c - WindyFilm LiveView preview (WindyStrife): the look on the LCD through ML's module display filter
 * (CBR_DISPLAY_FILTER, wf_main.c), and the preview exposure gain of wf_expsim.c.
 *
 * ML calls wf_dispf(0) ("should the filter run?", any task, flag reads only) and wf_dispf(&bufs) with struct
 * display_filter_buffers {src, dst}: 720x480 UYVY {U s8, Y0, V s8, Y1}, both cacheable, from its LiveView task
 * (priority 0x18, above the worker and wf_task). ML runs display filters only with Global Draw on.
 *
 * What is previewed: the colour pass the shot gets (a film on the preview's own table set, plain log, Log + LUT on the
 * worker's published tables). Grain, halation and mist are in the JPEG only. Not previewed: shots that get no look
 * (wf_cam_gate_pv: JPEG M / S, RAW only, multishot modes), movie mode, Canon playback.
 *
 * Concurrency (one core):
 *   - film set (wf_pv_ft, 96 KB, malloc'd only while a film is previewed): built and freed by wf_task only. The key it
 *     holds (wf_pv_film) is cleared first, then wf_task waits for !wf_pv_busy, rebuilds, and stores the key last.
 *   - Log + LUT tables: wf_pv_busy is stored before wf_ll_have_curve() is read; wf_shot.c waits for !wf_pv_busy before
 *     it frees or rewrites them.
 *   - the display task never waits: a look that is not ready, or a shot coming in (wf_req_live, checked every 16 rows),
 *     leaves the rest of the frame copied plain. No preview while a shot runs (wf_dispf(0) = 0).
 *   - lifetime: the film set lives only while a film is previewed; wf_task frees it once the preview is Off, Global Draw
 *     is off, movie mode or playback is on, another look is chosen, or the module unloads. */
#include "wf_core.h"
#include "wf_film.h"
#include "wf_loglut.h"
#include "wf_expsim.h"

extern int lv;
extern int shooting_mode;
extern int is_movie_mode(void);                  /* ML (bool) */
extern int get_global_draw_setting(void);        /* Overlay > Global Draw (bit 0 = in LiveView) */

#define WF_SHOOTMODE_MOVIE 0x14
#define WF_LV_W   720
#define WF_LV_H   480
#define WF_LV_ROW (WF_LV_W / 2)                  /* 360 words */

enum { WF_PVK_NONE = 0, WF_PVK_FILM, WF_PVK_LOG, WF_PVK_LLUT };

volatile int wf_pv_mode = WF_PV_ON;              /* menu "LiveView preview", WINDYFLM.CFG "preview" */
volatile int wf_pv_gen;                          /* bumped by the menu entry: a failed allocation is retried */
static struct wf_ftabs *wf_pv_ft;                /* the preview's own film set (wf_task) */
static volatile int wf_pv_film;                  /* the want key whose set wf_pv_ft holds complete, 0 = none */
static int wf_pv_want = -1;                      /* wf_task: the last key handled */
static volatile int wf_pv_nomem;
static uint32_t wf_pv_scr[WF_LV_ROW] __attribute__((aligned(32)));   /* display task only */

/* the key of a film look: catalogue row, print variant and strength (the table and the build flags) */
static int wf_pv_fkey(const struct wf_look *lk) { return (lk->row + 1) | lk->variant << 8 | lk->strength << 10; }

/* the look the shot would get now: WF_PVK_* and *arg (FILM: its key; LOG / LLUT: the curve) */
static int wf_pv_kind(struct wf_look *lk, int *arg)
{
    switch (wf_look_now(lk)) {
    case WF_OUT_FILM: *arg = wf_pv_fkey(lk); return WF_PVK_FILM;
    case WF_OUT_LOG:  *arg = lk->curve;      return WF_PVK_LOG;
    case WF_OUT_LLUT: *arg = lk->curve;      return WF_PVK_LLUT;
    }
    return WF_PVK_NONE;
}

static int wf_pv_movie(void) { return is_movie_mode() || shooting_mode == WF_SHOOTMODE_MOVIE; }

static int wf_pv_state_k(int *kind, int *arg, struct wf_look *lk)
{
    int k = wf_pv_kind(lk, arg);
    *kind = k;
    if (is_play_mode())
        return WF_PV_PLAY;
    if (wf_pv_movie())
        return WF_PV_MOVIE;
    if (!k)
        return WF_PV_NOLOOK;
    if (!wf_cam_gate_pv())
        return WF_PV_GATE;
    if (k == WF_PVK_FILM && wf_pv_film != *arg)
        return wf_pv_nomem ? WF_PV_NOMEM : WF_PV_WAIT;
    if (k == WF_PVK_LLUT && wf_ll_have_curve() != *arg)
        return WF_PV_WAIT;
    return WF_PV_READY;
}

int wf_pv_state(void)
{
    int k, a = 0;
    struct wf_look lk;
    return wf_pv_state_k(&k, &a, &lk);
}

/* wf_task: no preview frame in flight; 0 = still busy after 1 s (the caller retries on its next poll) */
static int wf_pv_quiet(void)
{
    for (int t = 0; wf_pv_busy; ) {
        if (++t > 100)
            return 0;
        msleep(10);
    }
    return 1;
}

static void wf_pv_free(void)
{
    if (wf_pv_ft) {
        __mem_free(wf_pv_ft);
        wf_pv_ft = 0;
    }
}

/* wf_task: keep wf_pv_ft built for the film look `want` (0 = none: freed). The table is the pack's, published by
 * wf_ws_poll in this same task (it cannot change during the build); until it is there the preview stays plain. */
static void wf_pv_film_poll(int want, const struct wf_look *lk)
{
    if (want == wf_pv_want)
        return;
    const uint32_t *T = want ? wf_ws_table(lk->row, lk->variant) : 0;
    if (want && !T) {
        if (wf_pv_film) {
            wf_pv_film = 0;                      /* another film is wanted: the old set is no longer shown */
            WF_BARRIER();
        }
        return;
    }
    int had = wf_pv_film;
    wf_pv_film = 0;                              /* new frames stop using the set */
    WF_BARRIER();
    if (!wf_pv_quiet()) {
        wf_pv_film = had;
        return;
    }
    wf_pv_want = want;
    wf_pv_nomem = 0;
    if (!want) {
        wf_pv_free();                            /* no film previewed: 96 KB back */
        return;
    }
    if (!wf_pv_ft)
        wf_pv_ft = __mem_malloc(sizeof *wf_pv_ft, 0, "wf_pv.c", __LINE__);
    if (!wf_pv_ft) {
        wf_pv_nomem = 1;                         /* retried on the next look change or menu change */
        wf_log("preview: no memory for the film set");
        return;
    }
    wf_film_build(wf_pv_ft, T, lk->ftflags);     /* the shot's builder, table and flags */
    WF_BARRIER();
    wf_pv_film = want;                           /* published last */
}

void wf_pv_poll(void)
{
    static int gen;
    int a = 0, k = WF_PVK_NONE, g = wf_pv_gen;
    struct wf_look lk;
    lk.row = -1;
    if (g != gen) {
        gen = g;
        if (wf_pv_nomem)
            wf_pv_want = -1;                     /* the menu entry changed: try the allocation again */
    }
    if (WF_PV_CAN && wf_pv_mode && wf_yuv_active() && (get_global_draw_setting() & 1) && !wf_pv_movie() && !is_play_mode())
        k = wf_pv_kind(&lk, &a);
    wf_pv_film_poll(k == WF_PVK_FILM ? a : 0, &lk);
}

void wf_pv_deinit(void)
{
    wf_pv_film = 0;
    WF_BARRIER();
    if (wf_pv_quiet()) {
        wf_pv_want = -1;
        wf_pv_free();
    }
}

/* ML display filter (CBR_DISPLAY_FILTER) */
unsigned int wf_dispf(unsigned int ctx)
{
    if (!WF_PV_CAN)
        return 0;
    if (!ctx) {                                  /* "should it run?": flag reads only */
        if (!wf_yuv_active() || !lv || wf_busy || wf_req_live)
            return 0;
        /* Preview exposure Smooth: the filter runs whatever the look; 2 = its picture has the shot's luma */
        if (wf_exp_mode == WF_EXP_SMOOTH && wf_exp_active())
            return 2;
        return wf_pv_mode && wf_pv_state() == WF_PV_READY;
    }
    if (!wf_yuv_active())
        return 0;
    uint32_t *src = ((uint32_t **)ctx)[0], *dst = ((uint32_t **)ctx)[1];
    if (!src || !dst)
        return 0;
    wf_pv_busy = 1;                              /* stored before the table flags are read */
    WF_BARRIER();
    wf_words_fn *fn = 0;
    int arg = 0, k = 0, a = 0, step = wf_pv_mode == WF_PV_FAST ? 2 : 1;
    struct wf_look lk;
    const struct wf_exp_tab *xg = wf_exp_frame();   /* this frame's exposure gain, 0 = none */
    if (wf_pv_mode && !wf_busy && !wf_req_live && wf_pv_state_k(&k, &a, &lk) == WF_PV_READY) {
        WF_BARRIER();                            /* table pointers read after their flags */
        if (k == WF_PVK_FILM)      { fn = wf_film_words; arg = (int)(uintptr_t)wf_pv_ft; }
        else if (k == WF_PVK_LOG)  { fn = wf_log_words;  arg = a; }
        else if (k == WF_PVK_LLUT) { fn = wf_ll_words;   arg = (int)(uintptr_t)wf_ll_tables(); }
        if (!arg)
            fn = 0;
    }
    int y = 0;
    if (fn || xg)
        for (; y < WF_LV_H; y += step) {
            if (!(y & 15) && wf_req_live)        /* a shot came in: the rest plain, the CPU to the worker */
                break;
            wf_copyw(wf_pv_scr, src + y * WF_LV_ROW, WF_LV_ROW);
            if (xg)
                wf_exp_words(wf_pv_scr, WF_LV_ROW, xg);
            if (fn)
                fn(wf_pv_scr, WF_LV_ROW, arg);
            wf_copyw(dst + y * WF_LV_ROW, wf_pv_scr, WF_LV_ROW);
            if (step == 2)
                wf_copyw(dst + (y + 1) * WF_LV_ROW, wf_pv_scr, WF_LV_ROW);
        }
    if (y < WF_LV_H)                             /* not ready / interrupted: plain picture, never stale */
        wf_copyw(dst + y * WF_LV_ROW, src + y * WF_LV_ROW, (WF_LV_H - y) * WF_LV_ROW);
    WF_BARRIER();
    wf_pv_busy = 0;
    return 1;
}
