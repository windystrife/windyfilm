/* wf_main.c - WindyFilm module entry (WindyStrife): module info, callbacks, init / deinit, the core task wf_task,
 * the log (ML/LOGS/WINDYFLM.LOG), card reads, wf_status / wf_gen / wf_cam.
 *
 * No MODULE_CONFIGS on purpose: ML would load and save its own ML/SETTINGS/<module>.CFG, the same name as our
 * WINDYFLM.CFG (wf_menu.c). Our settings are plain key=value lines written by wf_task.
 *
 * Threads:
 *   module init (ML's module loader): ROM checks (wf_cam_init), config, menu, the task. No hook, no file write.
 *   wf_task (prio 0x1E, own): starts the worker + hooks (wf_cam_start), then every ~50 ms: shot log, pack reader, Log + LUT
 *     tables, effects, preview tables, config save, EXIF writer, Light, the browser request, log flush.
 *   worker film_yuv (prio 0x1C, wf_shot.c) and the Canon hook tasks never log and never touch files. */
#include <module.h>
#include "wf_core.h"
#include "wf_expsim.h"
#include "wf_film.h"
#include "wf_fx.h"

extern int  task_create(const char *name, uint32_t prio, uint32_t stack, void *entry, void *arg);
extern void *create_named_semaphore(const char *name, int initial);
extern int  take_semaphore(void *sem, int timeout_ms);
extern int  give_semaphore(void *sem);
extern int  vsnprintf(char *b, size_t n, const char *fmt, __builtin_va_list ap);
extern void *FIO_OpenFile(const char *name, unsigned mode);
extern int   FIO_ReadFile(void *f, void *buf, size_t n);
extern void *FIO_CreateFile(const char *name);
extern void *FIO_CreateFileOrAppend(const char *name);
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
extern int  gui_menu_shown(void);
extern void gui_stop_menu(void);
extern void wf_film_shot_set_mem(void *mem);
#define WF_TASK_PRIO 0x1E

volatile int wf_cam_ok;
volatile int wf_unloading;
static volatile uint32_t wf_gen_n = 1;
static const char *volatile wf_status_s = "";
static volatile int wf_ui_req;
static volatile int wf_task_up;

/* ------------------------------------------------------------------ wf_api.h services */
int wf_cam(void)
{
    if (!wf_cam_ok)
        return WF_CAM_NONE;
#ifdef WF_BUILD_5D2
    return WF_CAM_5D2;
#else
    return WF_CAM_EOSM;
#endif
}
const char *wf_status(void) { return wf_status_s; }
void wf_status_set(const char *s) { wf_status_s = s ? s : ""; wf_gen_bump(); }
uint32_t wf_gen(void) { return wf_gen_n; }
void wf_gen_bump(void) { wf_gen_n++; }

/* ------------------------------------------------------------------ the log
 * A ring of lines written by any task but the worker / hooks (wf_task, the GUI task: menu, browser), flushed to the
 * card by wf_task. A line is "<seconds>.<tenths> text", at most WF_LOG_W - 1 chars. One semaphore guards the ring;
 * a writer that cannot get it within 50 ms drops its line (counted). */
#define WF_LOG_PATH "ML/LOGS/WINDYFLM.LOG"
#define WF_LOG_N 48
#define WF_LOG_W 120
static char wf_log_ring[WF_LOG_N][WF_LOG_W];
static volatile unsigned wf_log_h, wf_log_t, wf_log_lost;
static void *wf_log_sem;
static int wf_log_started, wf_log_lost_said;

void wf_log(const char *fmt, ...)
{
    if (!wf_log_sem || take_semaphore(wf_log_sem, 50) != 0) {
        wf_log_lost++;
        return;
    }
    if (wf_log_h - wf_log_t >= WF_LOG_N) {
        wf_log_lost++;
    } else {
        char *d = wf_log_ring[wf_log_h % WF_LOG_N];
        int ms = get_ms_clock(), n;
        unsigned s = (unsigned)ms / 1000u, t = ((unsigned)ms - s * 1000u) / 100u;
        n = snprintf(d, WF_LOG_W, "%5u.%u ", s, t);
        if (n < 0 || n >= WF_LOG_W)
            n = 0;
        __builtin_va_list ap;
        __builtin_va_start(ap, fmt);
        vsnprintf(d + n, WF_LOG_W - n, fmt, ap);
        __builtin_va_end(ap);
        wf_log_h++;
    }
    give_semaphore(wf_log_sem);
}

/* wf_task only: the ring to the card. The first flush of a boot starts a new file. */
void wf_log_flush(void)
{
    if (wf_log_lost != (unsigned)wf_log_lost_said) {
        wf_log_lost_said = wf_log_lost;
        wf_log("log: %d lines lost (ring full or busy)", wf_log_lost_said);
    }
    if (wf_log_t == wf_log_h)
        return;
    static char buf[WF_LOG_N * WF_LOG_W];
    int n = 0;
    while (wf_log_t != wf_log_h) {
        const char *s = wf_log_ring[wf_log_t % WF_LOG_N];
        for (int i = 0; s[i] && i < WF_LOG_W - 1 && n < (int)sizeof buf - 2; i++)
            buf[n++] = s[i];
        buf[n++] = '\n';
        WF_BARRIER();
        wf_log_t++;
    }
    void *f = wf_log_started ? FIO_CreateFileOrAppend(WF_LOG_PATH) : FIO_CreateFile(WF_LOG_PATH);
    if (!f || (uint32_t)f & 1)
        return;                                  /* card busy: these lines are gone, the next flush tries again */
    wf_log_started = 1;
    FIO_WriteFile(f, buf, n);
    FIO_CloseFile(f);
}

/* ------------------------------------------------------------------ card reads (UI: can / font packs) */
#define WF_RD_CHUNK 8192                         /* through an 8 KB DMA buffer (ML: cacheable reads up to 8 KB) */
int wf_card_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size)
{
    uint32_t sz = 0;
    if (size)
        *size = 0;
    if (!path || FIO_GetFileSize(path, &sz) != 0)
        return -1;
    if (size)
        *size = sz;
    if (pos > sz || sz - pos < n)
        return -1;
    char *dma = __mem_malloc(WF_RD_CHUNK, WF_MEM_DMA | WF_MEM_TMP, "wf_main.c", __LINE__);
    if (!dma)
        return -2;
    void *f = FIO_OpenFile(path, 0);
    int rc = 0;
    if (!f || ((uint32_t)f & 1)) {
        rc = -1;
        f = 0;
    } else if (FIO_SeekSkipFile(f, pos, 0) != (int64_t)pos) {
        rc = -1;
    } else {
        uint8_t *d = (uint8_t *)p;
        while (n && !rc) {
            int k = n > WF_RD_CHUNK ? WF_RD_CHUNK : (int)n;
            if (FIO_ReadFile(f, dma, k) != k)
                rc = -1;
            else if (d) {
                memcpy(d, dma, k);
                d += k;
            }
            n -= k;
        }
    }
    if (f)
        FIO_CloseFile(f);
    __mem_free(dma);
    return rc;
}

/* ------------------------------------------------------------------ the browser */
void wf_ui_request(void) { wf_ui_req = 1; }

static void wf_ui_poll(void)
{
    static int asked;
    if (!wf_ui_req)
        return;
    if (gui_menu_shown() && wf_ui_req < 40) {    /* the ML menu first goes away (opened from its entry), up to ~2 s */
        if (!asked)
            gui_stop_menu();
        asked = 1;
        wf_ui_req++;
        return;
    }
    wf_ui_req = 0;
    asked = 0;
    if (!wf_ui_is_open())
        wf_log("ui: open -> %d", wf_ui_open());
}

/* CBR_KEYPRESS (GUI task): the open browser gets every key; else its open key opens it (not in the ML menu, not in
 * Canon playback, where TRASH deletes) */
static unsigned int wf_keypress(unsigned int key)
{
    if (!wf_cam_ok || wf_unloading)
        return 1;
    if (wf_ui_is_open())
        return wf_ui_key(key);
    if (!gui_menu_shown() && !is_play_mode() && wf_ui_open_key(key)) {
        wf_ui_open();
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ the task */
static void wf_task(void *arg)
{
    (void)arg;
    wf_task_up = 1;
    wf_log("WindyFilm %s: camera %s, ROM check %s", __DATE__, wf_cam_name(), wf_cam_ok ? "ok" : "failed");
    if (wf_cam_ok) {
        void *m = __mem_malloc(sizeof(struct wf_ftabs), 0, "wf_main.c", __LINE__);
        wf_film_shot_set_mem(m);                 /* the shot set of the film kernel (wf_film.c), kept */
        if (!m)
            wf_log("film: no memory for the shot set, film looks stay off");
        int rc = wf_cam_start();
        wf_log("hooks: %s (%d)", rc ? "not installed" : "installed", rc);
    }
    while (!wf_unloading) {
        if (wf_cam_ok) {
            wf_cam_poll();
            wf_shot_poll();                      /* shot log, EXIF entries, pack reader, Log + LUT, effects */
            wf_pv_poll();                        /* before the effects: a film switch shows its look first */
            wf_fx_poll();                        /* halation / grain / mist state: load / switch / free */
            wf_exp_poll();
        }
        wf_menu_poll();                          /* config save, EXIF writer, Light */
        wf_ui_poll();
        wf_log_flush();
        for (int i = 0; i < 5 && !wf_unloading; i++) {
            msleep(10);
            if (wf_cam_ok)
                wf_ws_poll();                    /* a parked shot may wait for its film table */
        }
    }
    wf_log("unload");
    wf_log_flush();
    wf_task_up = 0;
}

/* ------------------------------------------------------------------ module init / deinit */
static unsigned int wf_init(void)
{
    wf_log_sem = create_named_semaphore("wf_log", 1);
    if (!DRY_SEM(wf_log_sem))
        wf_log_sem = 0;
    wf_cam_ok = wf_cam_init();                   /* ROM words only */
    if (!wf_cam_ok)
        wf_status_set("Unsupported firmware");
    wf_ui_init();
    wf_menu_init();                              /* config, ML menu */
    task_create("wf_task", WF_TASK_PRIO, 0x2000, (void *)wf_task, 0);
    return 0;
}

static unsigned int wf_deinit(void)
{
    wf_unloading = 1;                            /* hooks return at once (never unpatched: a Canon task may be inside) */
    if (wf_ui_is_open())
        wf_ui_close();
    wf_menu_deinit();                            /* the user's white balance back, menu removed */
    wf_pv_deinit();
    wf_exp_deinit();
    wf_shot_deinit();
    for (int i = 0; i < 50 && wf_task_up; i++)
        msleep(20);
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(wf_init)
    MODULE_DEINIT(wf_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS, wf_keypress, 0)
    MODULE_CBR(CBR_DISPLAY_FILTER, wf_dispf, 0)
MODULE_CBRS_END()
