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
 *     It also runs the EOS M ML-menu guard (wf_ui_menu_guard, src/ui/wf_ui.c) in its 10 ms waits. RAWB8 had that
 *     guard in a task of its own (wf_mguard): one task more filled DryOS's task table at boot and the worker was
 *     refused on 3 of 3 boots, so it lives here now (while wf_task is busy reading a LUT, ML's own 2 s ask stays).
 *   worker film_yuv (prio 0x1C, wf_shot.c) and the Canon hook tasks never log and never touch files. */
#include <module.h>
#include "wf_core.h"
#include "wf_expsim.h"
#include "wf_film.h"
#include "wf_fx.h"
#include "wf_rawb.h"

extern int  task_create(const char *name, uint32_t prio, uint32_t stack, void *entry, void *arg);
extern void *create_named_semaphore(const char *name, int initial);
extern int  take_semaphore(void *sem, int timeout_ms);
extern int  give_semaphore(void *sem);
extern int  vsnprintf(char *b, size_t n, const char *fmt, __builtin_va_list ap);
extern void *FIO_OpenFile(const char *name, unsigned mode);
extern int   FIO_ReadFile(void *f, void *buf, size_t n);
extern void *FIO_CreateFile(const char *name);
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
extern int   FIO_RemoveFile(const char *name);
extern int   FIO_RenameFile(const char *src, const char *dst);
extern int   FIO_CreateDirectory(const char *dirname);
extern int   is_dir(const char *path);
extern int  gui_menu_shown(void);
extern void NotifyBox(int timeout, const char *fmt, ...);
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

/* the worker + hooks start (wf_task): a refused worker task (DryOS's task table full at boot) is tried again every
 * WF_START_MS, WF_START_TRIES times (~15 s); "Worker failed" only after the last */
#define WF_START_MS    500
#define WF_START_TRIES 30
static volatile int wf_cam_rc = WF_CAM_NOWORKER, wf_cam_tries;
static int wf_cam_t;
static void wf_cam_try(void)
{
    wf_cam_t = get_ms_clock();
    int rc = wf_cam_start();
    wf_cam_tries++;
    if (rc == WF_CAM_NOWORKER && wf_cam_tries < WF_START_TRIES)
        return;                                  /* (wf_worker_start logged the try) */
    if (rc == WF_CAM_NOWORKER)
        wf_status_set("Worker failed");
    wf_cam_rc = rc;
    wf_log("hooks: %s (%d), try %d", rc ? "not installed" : "installed", rc, wf_cam_tries);
    wf_gen_bump();
}
int wf_films_state(void)
{
    int ws = wf_ws_state();
    if (!wf_cam_ok)
        return -1;
    if (ws == 1 || ws < 0)
        return ws == 1 ? 1 : -2;
    if (wf_cam_rc == WF_CAM_NOWORKER)
        return wf_cam_tries < WF_START_TRIES ? 0 : -1;
    return wf_cam_rc ? -1 : 0;
}
uint32_t wf_gen(void) { return wf_gen_n; }
void wf_gen_bump(void) { wf_gen_n++; }

/* ------------------------------------------------------------------ card writes (wf_fio.h)
 * The only code that creates, removes or renames a card file. Canon's FIO makes a second directory entry of the same
 * name when a file is created over an existing one; ML's FIO_CreateFile removes the file first, but a remove the card
 * refuses while it saves a shot goes unnoticed (the 11-14 EOS M run: two WINDYFLM.LOG entries, trap W24). So a
 * directory change waits until Canon is idle (lens_info.job_state 0), and a create follows only a verified absence. */
#define WF_O_RDWR_SYNC 0x1002                    /* ML fio-ml.h O_RDWR | O_SYNC (what ML's own append uses) */
#define WF_RM_TRIES    4                         /* removes while the name is still there (an old duplicate entry) */
static const char *const wf_fe_names[WF_FE_N] = { "ok", "busy", "remove", "create", "open", "seek", "write", "rename" };
const char *wf_fe_name(int step) { return step >= 0 && step < WF_FE_N ? wf_fe_names[step] : "?"; }
static int wf_fexists(const char *path) { uint32_t size; return FIO_GetFileSize(path, &size) == 0; }
static int wf_fset(struct wf_fe *e, int step, int rc) { e->step = step; e->rc = rc; return step == WF_FE_OK ? 0 : -1; }

int wf_fremove(const char *path, struct wf_fe *e)
{
    int job = wf_job_state(), rc = 0;
    if (job)
        return wf_fset(e, WF_FE_BUSY, job);
    for (int i = 0; i < WF_RM_TRIES && wf_fexists(path); i++)
        rc = FIO_RemoveFile(path);
    return wf_fset(e, wf_fexists(path) ? WF_FE_REMOVE : WF_FE_OK, rc);
}

void *wf_fcreate(const char *path, struct wf_fe *e)
{
    if (wf_fremove(path, e))
        return 0;
    void *f = FIO_CreateFile(path);              /* ML: _FIO_RemoveFile (nothing there now) + _FIO_CreateFile */
    if (!WF_FH_OK(f)) {
        wf_fset(e, WF_FE_CREATE, (int)(uintptr_t)f);
        return 0;
    }
    return f;
}

int wf_frename(const char *src, const char *dst, struct wf_fe *e)
{
    if (wf_fremove(dst, e))
        return -1;
    int rc = FIO_RenameFile(src, dst);
    return wf_fset(e, wf_fexists(src) || !wf_fexists(dst) ? WF_FE_RENAME : WF_FE_OK, rc);
}

int wf_fmkdir(const char *path, struct wf_fe *e)
{
    if (is_dir(path))
        return wf_fset(e, WF_FE_OK, 0);
    int job = wf_job_state();
    if (job)
        return wf_fset(e, WF_FE_BUSY, job);
    int rc = FIO_CreateDirectory(path);
    return wf_fset(e, is_dir(path) ? WF_FE_OK : WF_FE_CREATE, rc);
}

void *wf_fopen_w(const char *path, int how, uint32_t pos, struct wf_fe *e)
{
    wf_fset(e, WF_FE_OK, 0);
    if (how == WF_FO_NEW || (how == WF_FO_END_OR_NEW && !wf_fexists(path)))
        return wf_fcreate(path, e);
    void *f = FIO_OpenFile(path, WF_O_RDWR_SYNC);  /* an open never changes the directory: fine while Canon is busy */
    if (!WF_FH_OK(f)) {
        wf_fset(e, WF_FE_OPEN, (int)(uintptr_t)f);
        return 0;
    }
    int64_t at = how == WF_FO_AT ? FIO_SeekSkipFile(f, pos, 0) : FIO_SeekSkipFile(f, 0, 2);
    if (at < 0 || (how == WF_FO_AT && at != (int64_t)pos)) {
        FIO_CloseFile(f);
        wf_fset(e, WF_FE_SEEK, (int)at);
        return 0;
    }
    return f;
}

/* ------------------------------------------------------------------ the log
 * Text lines from any task but the worker / hooks (wf_task, the GUI task: menu, browser) go to a byte ring that only
 * wf_task writes to the card, appended to ML/LOGS/WINDYFLM.LOG. A line is "<seconds>.<tenths> text\n", at most
 * WF_LOG_W - 1 chars. Lines leave the ring only once the card took them: while the card refuses (busy with a shot)
 * they wait and the flush retries every WF_LOG_RETRY ms ("log: card refused N writes .. lines kept" once the card took
 * them); a line that finds the ring full, or the semaphore taken for
 * 50 ms, is dropped and counted ("log: N lines lost"). The log is appended across boots (the first line of a boot is
 * "WindyFilm <date>: camera ..."); at the first write of a boot a log over WF_LOG_MAX becomes WINDYFLM.OLD. */
#define WF_LOG_PATH  "ML/LOGS/WINDYFLM.LOG"
#define WF_LOG_OLD   "ML/LOGS/WINDYFLM.OLD"
#define WF_LOG_MAX   (512 * 1024)
#define WF_LOG_RING  16384                       /* a power of two */
#define WF_LOG_W     120
#define WF_LOG_RETRY 200
static char wf_log_ring[WF_LOG_RING];
static volatile unsigned wf_log_h, wf_log_t, wf_log_lost;    /* byte counters, h - t <= WF_LOG_RING */
static void *wf_log_sem;
static int wf_log_opened, wf_log_lost_said;
static int wf_log_fails, wf_log_fail_t0, wf_log_fail_last, wf_log_fail_job;
static struct wf_fe wf_log_fe;

void wf_log(const char *fmt, ...)
{
    char d[WF_LOG_W + 1];
    int ms = get_ms_clock(), n;
    n = snprintf(d, WF_LOG_W, "%5d.%d ", ms / 1000, ms % 1000 / 100);
    if (n < 0 || n >= WF_LOG_W)
        n = 0;
    d[n] = 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(d + n, WF_LOG_W - 1 - n, fmt, ap);  /* Canon's vsnprintf writes max + 1 bytes (ML stdio.c) */
    __builtin_va_end(ap);
    d[WF_LOG_W - 1] = 0;
    while (n < WF_LOG_W - 1 && d[n])
        n++;
    d[n++] = '\n';
    if (!wf_log_sem || take_semaphore(wf_log_sem, 50) != 0) {
        wf_log_lost++;
        return;
    }
    unsigned h = wf_log_h;
    if (WF_LOG_RING - (h - wf_log_t) < (unsigned)n) {
        wf_log_lost++;
    } else {
        for (int i = 0; i < n; i++)
            wf_log_ring[(h + i) & (WF_LOG_RING - 1)] = d[i];
        WF_BARRIER();
        wf_log_h = h + n;
    }
    give_semaphore(wf_log_sem);
}

/* wf_task only: the first write of a boot appends to the log (renamed to WINDYFLM.OLD first when it is over WF_LOG_MAX,
 * a new file when there is none: both only while Canon is idle, wf_fio.h); later writes only ever append */
static void *wf_log_open(struct wf_fe *e)
{
    if (wf_log_opened)
        return wf_fopen_w(WF_LOG_PATH, WF_FO_END, 0, e);
    uint32_t size = 0;
    if (FIO_GetFileSize(WF_LOG_PATH, &size) == 0 && size > WF_LOG_MAX && wf_frename(WF_LOG_PATH, WF_LOG_OLD, e) &&
        e->step == WF_FE_BUSY)
        return 0;                                /* Canon busy: rotated at a later try (any other failure: appended) */
    void *f = wf_fopen_w(WF_LOG_PATH, WF_FO_END_OR_NEW, 0, e);
    if (f)
        wf_log_opened = 1;
    return f;
}

/* wf_task only: the ring to the card (two writes when the text wraps round the ring's end) */
void wf_log_flush(void)
{
    if (wf_log_lost != (unsigned)wf_log_lost_said) {
        wf_log_lost_said = wf_log_lost;
        wf_log("log: %d lines lost so far (the log ring was full or busy)", wf_log_lost_said);
    }
    unsigned t = wf_log_t, h = wf_log_h;
    if (t == h)
        return;
    int now = get_ms_clock();
    if (wf_log_fails && now - wf_log_fail_last < WF_LOG_RETRY && !wf_unloading)
        return;
    struct wf_fe e;
    void *f = wf_log_open(&e);
    while (f && t != h) {
        unsigned a = t & (WF_LOG_RING - 1), n = h - t;
        if (n > WF_LOG_RING - a)
            n = WF_LOG_RING - a;
        int w = FIO_WriteFile(f, wf_log_ring + a, n);
        if (w > 0)
            t += (unsigned)w > n ? n : (unsigned)w;
        if (w != (int)n) {
            e.step = WF_FE_WRITE;
            e.rc = w;
            break;
        }
    }
    if (f)
        FIO_CloseFile(f);
    WF_BARRIER();
    wf_log_t = t;                                /* what the card took leaves the ring */
    if (t == h) {
        if (wf_log_fails)
            wf_log("log: card refused %d writes in %d ms (first: %s rc %d, job %d), lines kept", wf_log_fails,
                   now - wf_log_fail_t0, wf_fe_name(wf_log_fe.step), wf_log_fe.rc, wf_log_fail_job);
        wf_log_fails = 0;
        return;
    }
    if (!wf_log_fails++) {
        wf_log_fail_t0 = now;
        wf_log_fe = e;
        wf_log_fail_job = wf_job_state();
    }
    wf_log_fail_last = now;
}

/* ------------------------------------------------------------------ card reads (UI: can / font packs) */
#define WF_RD_CHUNK 8192                         /* through an 8 KB DMA buffer (ML: cacheable reads up to 8 KB) */
int wf_card_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size)
{
    uint32_t sz = 0;
    if (size)
        *size = 0;
    if (!path)
        return WF_RD_MISSING;
    if (FIO_GetFileSize(path, &sz) != 0) {       /* 11-14 log: "not on the card" while it was there, Canon busy */
        uint32_t a = 0;
        return FIO_GetFileSize("AUTOEXEC.BIN", &a) == 0 ? WF_RD_MISSING : WF_RD_BUSY;
    }
    if (size)
        *size = sz;
    if (pos > sz || sz - pos < n)
        return WF_RD_SHORT;
    char *dma = __mem_malloc(WF_RD_CHUNK, WF_MEM_DMA | WF_MEM_TMP, "wf_main.c", __LINE__);
    if (!dma)
        return WF_RD_NOMEM;
    void *f = FIO_OpenFile(path, 0);
    int rc = 0;
    if (!WF_FH_OK(f)) {
        rc = WF_RD_OPEN;
        f = 0;
    } else if (FIO_SeekSkipFile(f, pos, 0) != (int64_t)pos) {
        rc = WF_RD_IO;
    } else {
        uint8_t *d = (uint8_t *)p;
        while (n && !rc) {
            int k = n > WF_RD_CHUNK ? WF_RD_CHUNK : (int)n;
            if (FIO_ReadFile(f, dma, k) != k)
                rc = WF_RD_IO;
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
    if (wf_ui_is_open() && wf_ui_req < 80) {     /* a browser still closing (it gives the screen back only once the ML */
        wf_ui_req++;                             /* menu has gone, wf_ui.c): open again after it, up to ~2 s more */
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
    switch (key) {                               /* the end-of-roll notice (wf_dark.c): a key press drops it */
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER: case MODULE_KEY_Q:   /* (EOS M LiveView: SET comes as Q) */
        if (wf_roll_notice_key(1))
            return 0;                            /* SET: the Darkroom opens instead */
        break;
    case MODULE_KEY_PRESS_HALFSHUTTER: case MODULE_KEY_PRESS_FULLSHUTTER: case MODULE_KEY_REC:
    case MODULE_KEY_PRESS_LEFT: case MODULE_KEY_PRESS_RIGHT: case MODULE_KEY_PRESS_UP: case MODULE_KEY_PRESS_DOWN:
    case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_UP: case MODULE_KEY_WHEEL_DOWN:
    case MODULE_KEY_INFO: case MODULE_KEY_MENU: case MODULE_KEY_TRASH: case MODULE_KEY_PLAY: case MODULE_KEY_LV:
    case MODULE_KEY_PICSTYLE: case MODULE_KEY_TOUCH_1_FINGER:
        wf_roll_notice_key(0);                   /* and the key goes on as before */
        break;
    }
    if (key == WF_KEY_PLAY && !gui_menu_shown() && wf_roll_blocks_play()) {   /* blind roll (docs/DESIGN.md 3.6) */
        NotifyBox(2000, "Roll in camera: develop it in the Darkroom");
        return 0;
    }
    if (!gui_menu_shown() && !is_play_mode() && wf_ui_open_key(key)) {
        wf_ui_open();
        return 0;
    }
    if (key == MODULE_KEY_TRASH)                 /* the next log says why the browser did not come up */
        wf_ui_open_refused(key, is_play_mode());
    return 1;
}

/* ------------------------------------------------------------------ the task */
static void wf_task(void *arg)
{
    (void)arg;
    wf_task_up = 1;
    if (wf_cam_ok) {
        void *m = __mem_malloc(sizeof(struct wf_ftabs), 0, "wf_main.c", __LINE__);
        wf_film_shot_set_mem(m);                 /* the shot set of the film kernel (wf_film.c), kept */
        if (!m)
            wf_log("film: no memory for the shot set, film looks stay off");
        wf_cam_try();
    }
    while (!wf_unloading) {
        if (wf_cam_ok && wf_cam_rc == WF_CAM_NOWORKER && wf_cam_tries < WF_START_TRIES
            && get_ms_clock() - wf_cam_t >= WF_START_MS)
            wf_cam_try();
        if (wf_cam_ok) {
            wf_cam_poll();
            wf_shot_poll();                      /* shot log, EXIF entries, pack reader, Log + LUT, effects */
            wf_pv_poll();                        /* before the effects: a film switch shows its look first */
            wf_fx_poll();                        /* halation / grain / mist state: load / switch / free */
            wf_exp_poll();
            wf_rawb_poll();                      /* RAW develop: tables, memory, progress box, the shot's lines */
            wf_roll_poll();                      /* Roll + Darkroom: roll file, frames, RAW+L / review, HUD */
        }
        wf_menu_poll();                          /* config save, EXIF writer, Light */
        wf_ui_poll();
        wf_log_flush();
        for (int i = 0; i < 5 && !wf_unloading; i++) {
            msleep(10);
            if (wf_cam_ok) {
                wf_ws_poll();                    /* a parked shot may wait for its film table */
                wf_ui_menu_guard();              /* EOS M: the ML menu's Canon dialog (no task of its own) */
            }
        }
    }
    if (wf_cam_ok) {
        wf_rawb_end();                           /* a develop stops at its next band (wf_unloading); then the memory goes */
        wf_dark_end();                           /* the darkroom too; Canon's image quality / review back */
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
    wf_log("WindyFilm %s: camera %s, ROM check %s", __DATE__, wf_cam_name(), wf_cam_ok ? "ok" : "failed");   /* a boot's 1st line */
    if (!wf_cam_ok)
        wf_status_set("Unsupported firmware");
    wf_ui_init();
    wf_dark_init();
    wf_menu_init();                              /* config, ML menu */
    uint32_t tt = task_create("wf_task", WF_TASK_PRIO, 0x2000, (void *)wf_task, 0);
    if (!DRY_OK(tt))                             /* DryOS's task table full: nothing of WindyFilm runs */
        wf_log("no wf_task (%x): WindyFilm stays off", (unsigned)tt);
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
