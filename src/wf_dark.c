/* wf_dark.c - Roll + Darkroom core (docs/DESIGN.md 3.5), WindyStrife's own code, written from scratch for WindyFilm.
 *
 * The roll (wf_task, wf_roll_poll): the roll file through wf_fio.h, the requests of the UI / menu, frame detection from
 * Canon's file number while Canon is idle, RAW+L and image review while a roll is in the camera, the HUD over LiveView.
 * The darkroom (task "wf_dark", one per run): memory, then per frame the CR2 read + decode (wf_cr2.c), the develop
 * (wf_dev.c), the look stage through the worker (wf_shot_run with frame.devd), the print encode (wf_jpe.c) and write,
 * the roll file, the view the UI draws (struct wf_dark_view). While a run lives it owns the roll: wf_task leaves it alone.
 * The pure per-frame parts (scales, Exif, thumbnails) are wf_darkp.c, shared with the host test. */
#include "wf_core.h"
#include "wf_dark.h"
#include "wf_loglut.h"
#include "wf_rawb.h"                             /* B gives its shoot memory back before a darkroom run */
#ifdef WF_BUILD_5D2
#define WF_DARK_CAN 0                            /* the 5D2 follows once the EOS M works (raw 5792 x 3804, GBRG) */
#else
#define WF_DARK_CAN 1
#include "wf_darkp.h"
#include "wf_cr2.h"
#include "wf_dev.h"
#include "wf_jpe.h"
#endif

/* ML core (EOSM_202.sym / 5D2_212.sym both export these) */
extern int  task_create(const char *name, uint32_t prio, uint32_t stack, void *entry, void *arg);
extern void *FIO_OpenFile(const char *name, unsigned mode);
extern int   FIO_ReadFile(void *f, void *buf, size_t n);
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
extern int  prop_request_change(unsigned property, const void *addr, size_t len);
extern void PauseLiveView(void);
extern int  ResumeLiveView(void);
extern void powersave_prohibit(void);
extern void powersave_permit(void);
extern int  is_movie_mode(void);
extern int  gui_menu_shown(void);
extern uint8_t *bmp_vram(void);
extern void redraw(void);
extern void NotifyBox(int timeout, const char *fmt, ...);
extern void LoadCalendarFromRTC(void *tm);
extern void *srm_malloc_suite(int n);
extern void  srm_free_suite(void *suite);
extern void *shoot_malloc_suite_contig(size_t size);
extern void  shoot_free_suite(void *suite);
extern void *GetFirstChunkFromSuite(void *suite);
extern void *GetNextMemoryChunk(void *suite, void *chunk);
extern void *GetMemoryAddressOfMemoryChunk(void *chunk);
extern int   GetSizeOfMemoryChunk(void *chunk);
extern int  ml_shutdown_requested;
extern int  lv, pic_quality, image_review_time, battery_level_bars;
#ifdef WF_BUILD_5D2
extern int  GetBatteryLevel(void);
#endif

/* ML property.h / module.h values (both bodies) */
#define WF_PROP_PIC_QUALITY 0x8000002f
#define WF_PICQ_RAW_L       0x3070000            /* PICQ_RAW_JPG_LARGE_FINE */
#define WF_PROP_REVIEW      0x02020006           /* PROP_IMAGE_REVIEW_TIME: 0, 2, 4, 8, 0xff */
#define WF_BMPPITCH         960
#define WF_CACHEABLE(p)     ((void *)((uint32_t)(p) & ~0x40000000u))   /* ML mem_defs.h CACHEABLE (DIGIC 4 / 5) */

/* wf_menu.c (it has ML's card headers) */
int  wf_card_nums(int *folder, int *file);       /* Canon's DCIM folder / last file number, 0 = ok */
const char *wf_dcim_dir(void);                   /* get_dcim_dir() */
const char *wf_file_prefix(void);                /* get_file_prefix() */
const char *wf_exif_text(const struct wf_look *lk, int g, int h, int m, int ev, char *b, int n);

/* ================================================================== the roll (wf_task) */
static struct wf_roll roll;
static volatile uint32_t roll_gen_n = 1;
static int roll_read_done, roll_have_file, roll_dirty, roll_save_fail_t;
static char roll_txt[WF_ROLL_FILE] __attribute__((aligned(32)));
volatile int wf_blind = 1;
static volatile int req_kind, req_film, req_exp;  /* 1 load, 2 rewind, 3 close */
static int poll_F = -1, poll_D = -1, poll_stable, wind_t = -100000;
static int pq_old = -1, rv_old = -1, prop_t;
static int hud_on, hud_fonts_on, hud_t, hud_mode = -1, info_t = -100000, hud_sig, hud_job;
static int hud_font_again, hud_font_t, hud_font_n;  /* the font pack read again while Canon held the card */
static volatile int dk_running;                   /* the darkroom task owns the roll */

const struct wf_roll *wf_roll_now(void) { return &roll; }
uint32_t wf_roll_gen(void) { return roll_gen_n; }
static void roll_changed(void) { roll_gen_n++; wf_gen_bump(); }
int wf_roll_shooting(void) { return roll.state == WF_RS_LOADED; }
int wf_roll_blocks_play(void) { return wf_blind && wf_roll_open(&roll) && !dk_running; }
int wf_dark_busy(void) { return dk_running; }

int wf_roll_req_load(int film, int exp)
{
    if (!WF_DARK_CAN)
        return -4;
    if (req_kind || dk_running)
        return -3;
    if (wf_roll_open(&roll) && roll.nshot > 0)
        return -1;
    if (!wf_roll_ok_exp(exp) || !wf_film_by_id(film))
        return -2;
    req_film = film;
    req_exp = exp;
    WF_BARRIER();
    req_kind = 1;
    return 0;
}
int wf_roll_req_rewind(void)
{
    if (req_kind || dk_running)
        return -3;
    if (!((roll.state == WF_RS_LOADED && roll.nshot > 0) || roll.state == WF_RS_FULL))
        return -1;
    req_kind = 2;
    return 0;
}
int wf_roll_req_close(void)
{
    if (req_kind || dk_running)
        return -3;
    if (!wf_roll_open(&roll))
        return -1;
    req_kind = 3;
    return 0;
}

int wf_roll_hud(char *line, int n)
{
    if (!wf_roll_open(&roll))
        return 0;
    const struct wf_film *f = wf_film_by_id(roll.film);
    snprintf(line, n, "ROLL %03d  %s  %d/%d", roll.num, f ? f->shortname : "?", roll.nshot, roll.exp);
    return 1;
}

/* the roll file: read once at boot; saved whole, created once, then rewritten in place (trap W24) */
static void roll_read(void)
{
    uint32_t size = 0;
    wf_roll_reset(&roll);
    roll_have_file = FIO_GetFileSize(WF_ROLL_PATH, &size) == 0;
    if (!roll_have_file)
        return;
    uint32_t n = size < WF_ROLL_FILE ? size : WF_ROLL_FILE;
    if (wf_card_read(WF_ROLL_PATH, 0, roll_txt, n, 0) != 0 || wf_roll_parse(&roll, roll_txt, (int)n) != 0) {
        wf_log("roll: %s unreadable (%d bytes): no roll", WF_ROLL_PATH, (int)size);
        wf_roll_reset(&roll);
        return;
    }
    if (size != WF_ROLL_FILE)
        roll_dirty = 1;                          /* rewritten at its full size */
    wf_log("roll: %03d %s film %d %d/%d", roll.num, wf_roll_state_name(roll.state), roll.film, roll.nshot, roll.exp);
}

static int roll_save(void)
{
    if (wf_roll_text(&roll, roll_txt, sizeof roll_txt) != WF_ROLL_FILE)
        return -1;
    struct wf_fe e = { 0, 0 };
    void *f = wf_fopen_w(WF_ROLL_PATH, roll_have_file ? WF_FO_AT : WF_FO_NEW, 0, &e);
    if (!f) {
        if (e.step == WF_FE_OPEN && roll_have_file)
            roll_have_file = 0;                  /* gone (card swapped): created again next time */
        return -1;
    }
    int w = FIO_WriteFile(f, roll_txt, WF_ROLL_FILE);
    FIO_CloseFile(f);
    if (w != WF_ROLL_FILE)
        return -1;
    roll_have_file = 1;
    roll_dirty = 0;
    return 0;
}

static int roll_cr2_at(int folder, int file, const char *ext, char *out, int n)
{
    struct wf_roll t;
    t.nshot = 1;
    t.folder[0] = (uint16_t)folder;
    t.file[0] = (uint16_t)file;
    return wf_roll_cr2(&t, 0, wf_dcim_dir(), wf_file_prefix(), ext, out, n);
}
static int roll_has_cr2(void *ctx, int folder, int file)
{
    (void)ctx;
    char p[96];
    uint32_t sz = 0;
    return roll_cr2_at(folder, file, "CR2", p, sizeof p) > 0 && FIO_GetFileSize(p, &sz) == 0 && sz > 0;
}

/* RAW+L and image review while a roll is in the camera; Canon's values back afterwards */
static void roll_props(int idle)
{
    int now = get_ms_clock();
    if (now - prop_t < 2000 || !idle || is_movie_mode())
        return;
    int want = wf_roll_can_shoot(&roll) && !dk_running, blind = want && wf_blind;
    if (want && pic_quality != WF_PICQ_RAW_L) {
        if (pq_old < 0)
            pq_old = pic_quality;
        int v = WF_PICQ_RAW_L;
        prop_request_change(WF_PROP_PIC_QUALITY, &v, 4);
        wf_log("roll: image quality %x -> RAW+L", (unsigned)pq_old);
        prop_t = now;
    } else if (!want && pq_old >= 0) {
        if (pic_quality != pq_old)
            prop_request_change(WF_PROP_PIC_QUALITY, &pq_old, 4);
        wf_log("roll: image quality back to %x", (unsigned)pq_old);
        pq_old = -1;
        prop_t = now;
    }
    if (blind && image_review_time != 0) {
        if (rv_old < 0)
            rv_old = image_review_time;
        int v = 0;
        prop_request_change(WF_PROP_REVIEW, &v, 4);
        wf_log("roll: image review %d -> off (blind roll)", rv_old);
        prop_t = now;
    } else if (!blind && rv_old >= 0) {
        if (image_review_time == 0)
            prop_request_change(WF_PROP_REVIEW, &rv_old, 4);
        wf_log("roll: image review back to %d", rv_old);
        rv_old = -1;
        prop_t = now;
    }
}

/* the HUD over LiveView (DESIGN 3.5, owner 11-14): the roll + film chip, the big counter and the ladder show for
 * HUD_INFO_MS, fade for HUD_FADE_MS, then only a small dim counter stays. They show again when LiveView comes back
 * (or the menu / UI / PLAY goes), after a shot, after a frame counts, and when the roll, its film or its state changes. */
#define HUD_INFO_MS 2500
#define HUD_FADE_MS 400
static void roll_hud(void)
{
    int want = wf_roll_open(&roll) && !dk_running && lv && !gui_menu_shown() && !wf_ui_is_open() && !is_play_mode();
    int now = get_ms_clock();
    if (wf_roll_open(&roll) && !hud_fonts_on && !wf_ui_is_open()) {
        hud_font_again = wfdr_hud_fonts(1) == 1;
        hud_fonts_on = 1;
        hud_font_t = now;
        hud_font_n = 0;
    } else if (!wf_roll_open(&roll) && hud_fonts_on && !wf_ui_is_open()) {
        wfdr_hud_fonts(0);
        hud_fonts_on = 0;
    } else if (hud_fonts_on && hud_font_again && !wf_ui_is_open() && now - hud_font_t >= 2000) {
        hud_font_t = now;                        /* (the UI reads it again itself while it is open) */
        hud_font_again = wfdr_hud_fonts(2) == 1;
        if (++hud_font_n >= 5 || !hud_font_again) {
            wf_log("roll: HUD fonts %s after %d reads again", hud_font_again ? "still not read" : "read", hud_font_n);
            hud_font_again = 0;
        }
    }
    int sig = (int)(roll.num * 4099u + roll.film * 37u + roll.exp * 5u + roll.state) ^ (wf_loaded() << 20);
    if (sig != hud_sig) {                        /* a new roll, film or state */
        hud_sig = sig;
        info_t = now;
    }
    if (wf_job_state())
        hud_job = 1;                             /* a shot: the info again once Canon is idle */
    else if (hud_job) {
        hud_job = 0;
        info_t = now;
    }
    if (want && !hud_on)
        info_t = now;                            /* LiveView is back */
    if (want) {
        int blink = now - wind_t < 1000, t = now - info_t;
        int mode = t < HUD_INFO_MS ? WF_HUD_FULL : t < HUD_INFO_MS + HUD_FADE_MS ? WF_HUD_FADE : WF_HUD_SMALL;
        if (!hud_on || mode != hud_mode || blink || now - hud_t >= 500) {
            /* the first small pass clears what the full HUD drew (only our own rectangles) */
            wfdr_hud(bmp_vram(), WF_BMPPITCH, blink, mode == WF_HUD_SMALL && hud_mode != WF_HUD_SMALL ? WF_HUD_CLEAR : mode);
            hud_t = now;
            hud_mode = mode;
        }
    } else {
        if (hud_on && !wf_ui_is_open())
            redraw();
        hud_mode = -1;
    }
    hud_on = want;
}

static void roll_req(void)
{
    int k = req_kind, F = 0, D = 0;
    if (!k)
        return;
    const struct wf_film *f = wf_film_by_id(req_film);
    if (k == 1) {
        if (wf_card_nums(&D, &F) != 0) {
            wf_log("roll: no file number, load refused");
        } else if (wf_roll_load(&roll, req_film, req_exp, D, F) == 0) {
            int rc = wf_loaded() == req_film ? WF_LOAD_OK : wf_load(req_film);
            wf_log("roll: %03d loaded, %s x%d at %d/%04d (film load %d)", roll.num, f ? f->name : "?", roll.exp, D, F, rc);
            if (WF_RAWB_CAN && wf_rawb)          /* both modes at once (DESIGN 3.5): B at shot time, the print later */
                wf_log("roll: RAW develop stays on (frames get B now, a print later)");
            poll_F = F;
            poll_D = D;
            roll_dirty = 1;
        }
    } else if (k == 2) {
        if (wf_roll_rewind(&roll) == 0) {
            wf_log("roll: %03d rewound at %d/%d", roll.num, roll.nshot, roll.exp);
            roll_dirty = 1;
        }
    } else if (k == 3) {
        int left = wf_roll_left(&roll);
        if (wf_roll_close(&roll) == 0) {
            wf_log("roll: %03d closed, %d frame(s) kept undeveloped", roll.num, left);
            roll_dirty = 1;
        }
    }
    WF_BARRIER();
    req_kind = 0;
    roll_changed();
}

void wf_roll_poll(void)
{
    if (!roll_read_done) {
        roll_read_done = 1;
        roll_read();
        roll_changed();
    }
    if (dk_running) {
        hud_on = 0;
        return;
    }
    int idle = wf_job_state() == 0 && !wf_shot_pending();
    if (idle)
        roll_req();
    if (idle && roll.state == WF_RS_LOADED) {    /* new frames: the file number stable over two polls */
        int D = 0, F = 0;
        if (wf_card_nums(&D, &F) == 0) {
            if (F != poll_F || D != poll_D) {
                poll_F = F;
                poll_D = D;
                poll_stable = 0;
            } else if (++poll_stable == 2 && (F != roll.seen_file || D != roll.seen_folder)) {
                int n0 = roll.nshot, n = wf_roll_seen(&roll, D, F, roll_has_cr2, 0);
                roll_dirty = 1;
                if (n > 0) {
                    wind_t = info_t = get_ms_clock();
                    wf_log("roll: %03d frame %d/%d IMG_%04d%s", roll.num, roll.nshot, roll.exp, roll.file[roll.nshot - 1],
                           roll.state == WF_RS_FULL ? ", end of roll" : "");
                    roll_changed();
                } else if (n0 == roll.nshot) {
                    wf_log("roll: file %d/%04d is not a frame (no CR2)", D, F);
                }
            }
        }
    }
    roll_props(idle);
    if (roll_dirty && idle && get_ms_clock() - roll_save_fail_t > 3000 && roll_save() != 0) {
        roll_save_fail_t = get_ms_clock();
        wf_log("roll: %s not saved, retry", WF_ROLL_PATH);
    }
    roll_hud();
}

/* ================================================================== the darkroom */
#define DK_PRIO     0x1F                         /* below wf_task (0x1E) and the worker (0x1C) */
#define DK_W        5184                         /* the EOS M L frame in its CR2 (DESIGN 3.2: x0 84, y0 64) */
#define DK_H        3456
#define DK_X0       84
#define DK_Y0       64
#define DK_RD       (256 * 1024)                 /* card reads / writes through the DMA buffer */
#define DK_IN       (256 * 1024)                 /* the decoder's cached input */
#define DK_OB       (64 * 1024)                  /* encoder output buffer */
#define DK_APP1     (64 * 1024)
#define DK_BAND     64
#define DK_JROWS    64
#define DK_BAT_GO   30
#define DK_BAT_STOP 15
#define DK_RAW_N    (9240u * 3528u)              /* the EOS M CR2's sensor image, ML packed 14-bit */

static struct wf_dark_view dv;
static uint8_t dv_print[WF_DR_PW * WF_DR_PH];
static uint8_t dv_wire[WF_DR_NWIRE][WF_DR_TW * WF_DR_TH];
static volatile int dk_stop;
static int dk_t0, dk_sec;

static void dv_bump(void) { dv.gen++; }

const struct wf_dark_view *wf_dark_view(void)
{
    if (dv.state == WF_DR_RUN || dv.state == WF_DR_STOPPING) {
        int now = get_ms_clock();
        dv.t_frame = now - dk_t0;                /* live, so the dial moves while the worker holds the frame */
        if ((now >> 10) != dk_sec) {
            dk_sec = now >> 10;
            dv.gen++;
        }
    }
    return &dv;
}

int wf_dark_eta_s(const struct wf_dark_view *v)
{
    int per = v->avg_ms ? v->avg_ms : 55000;
    int left = v->state == WF_DR_IDLE || v->state == WF_DR_STOPPED || v->state == WF_DR_FAIL ? wf_roll_left(&roll) : v->left;
    if (v->state == WF_DR_IDLE && roll.state == WF_RS_LOADED)
        left = roll.nshot;
    int s = (left * (per / 100) - (v->state == WF_DR_RUN ? v->t_frame / 100 : 0)) / 10;
    return s < 0 ? 0 : s;
}

int wf_clock_hm(void)
{
    int tm[16];
    memset(tm, 0, sizeof tm);
    LoadCalendarFromRTC(tm);                     /* struct tm: sec, min, hour, ... */
    if (tm[2] < 0 || tm[2] > 23 || tm[1] < 0 || tm[1] > 59)
        return -1;
    return tm[2] * 100 + tm[1];
}

#if WF_DARK_CAN
static struct wf_cr2_dec dk_dec;
static struct wf_cr2_info dk_inf;
static uint32_t dk_jpe[1024] __attribute__((aligned(8)));   /* struct wf_jpe (wf_jpe_bytes() <= 4096) */
static struct wf_dk_thumb dk_th;

static int dk_battery(void)
{
#ifdef WF_BUILD_5D2
    int b = GetBatteryLevel();
    return b >= 0 && b <= 100 ? b : -1;
#else
    int b = battery_level_bars;                  /* EOS M: bars only (ML lens.c: 0 low, 1 half, else full) */
    return b <= 0 ? 5 : b == 1 ? 30 : b == 2 ? 70 : 100;
#endif
}

/* ------------------------------------------------------------------ memory */
struct dk_mem {
    void *srm, *shoot;                           /* suites */
    uint8_t *raw, *frame;                        /* cacheable aliases */
    uint8_t *dma, *in, *ob, *app1;
    struct wf_dev_tabs *tabs;
    void *scr;
};
static struct dk_mem M;

/* the first contiguous run of suite chunks of >= need bytes, skipping [skip, skip + skipn) */
static uint8_t *dk_run(void *suite, uint32_t need, uint8_t *skip, uint32_t skipn)
{
    uint32_t a0 = 0, len = 0;
    for (void *c = GetFirstChunkFromSuite(suite); c; c = GetNextMemoryChunk(suite, c)) {
        uint32_t a = (uint32_t)GetMemoryAddressOfMemoryChunk(c), n = (uint32_t)GetSizeOfMemoryChunk(c);
        uint32_t ac = a & ~0x40000000u;
        if (skip && ac < (uint32_t)skip + skipn && ac + n > (uint32_t)skip) {
            len = 0;
            continue;
        }
        if (len && (a0 & ~0x40000000u) + len == ac)
            len += n;
        else {
            a0 = a;
            len = n;
        }
        if (len >= need)
            return WF_CACHEABLE(a0);
    }
    return 0;
}

static void dk_log_suite(const char *what, void *suite)
{
    char b[100];
    int l = 0, k = 0;
    b[0] = 0;
    for (void *c = GetFirstChunkFromSuite(suite); c && k < 6; c = GetNextMemoryChunk(suite, c), k++)
        l += snprintf(b + l, sizeof b - l, " %x+%dK", (unsigned)GetMemoryAddressOfMemoryChunk(c), GetSizeOfMemoryChunk(c) >> 10);
    wf_log("dark: %s chunks%s", what, b);
}

static void dk_free(void)
{
    if (M.srm) srm_free_suite(M.srm);
    if (M.shoot) shoot_free_suite(M.shoot);
    if (M.dma) __mem_free(M.dma);
    if (M.in) __mem_free(M.in);
    if (M.ob) __mem_free(M.ob);
    if (M.app1) __mem_free(M.app1);
    if (M.tabs) __mem_free(M.tabs);
    if (M.scr) __mem_free(M.scr);
    memset(&M, 0, sizeof M);
}

static int dk_alloc(int raw_n, int frame_n)
{
    memset(&M, 0, sizeof M);
    M.dma = __mem_malloc(DK_RD, WF_MEM_DMA, "wf_dark.c", __LINE__);
    M.in = __mem_malloc(DK_IN, 0, "wf_dark.c", __LINE__);
    M.ob = __mem_malloc(DK_OB, 0, "wf_dark.c", __LINE__);
    M.app1 = __mem_malloc(DK_APP1, 0, "wf_dark.c", __LINE__);
    M.tabs = __mem_malloc(sizeof(struct wf_dev_tabs), 0, "wf_dark.c", __LINE__);
    M.scr = __mem_malloc(wf_dev_scratch_bytes(DK_W, DK_H), 0, "wf_dark.c", __LINE__);
    if (!M.dma || !M.in || !M.ob || !M.app1 || !M.tabs || !M.scr) {
        wf_log("dark: no memory for the buffers / tables");
        return -1;
    }
    M.shoot = shoot_malloc_suite_contig((size_t)frame_n);
    if (M.shoot) {
        M.frame = dk_run(M.shoot, (uint32_t)frame_n, 0, 0);
        if (!M.frame) {
            shoot_free_suite(M.shoot);
            M.shoot = 0;
        }
    }
    M.srm = srm_malloc_suite(0);
    if (M.srm) {
        dk_log_suite("srm", M.srm);
        M.raw = dk_run(M.srm, (uint32_t)raw_n, 0, 0);
        if (M.raw && !M.frame)
            M.frame = dk_run(M.srm, (uint32_t)frame_n, M.raw, (uint32_t)raw_n);
    }
    wf_log("dark: memory raw %x frame %x (%s)", (unsigned)M.raw, (unsigned)M.frame, M.shoot ? "shoot" : "srm");
    return M.raw && M.frame ? 0 : -1;
}

/* ------------------------------------------------------------------ card I/O through the DMA buffer */
struct dk_rd { void *f; };
static int dk_read_cb(void *ctx, void *buf, uint32_t n)
{
    struct dk_rd *r = ctx;
    if (n > DK_RD) n = DK_RD;
    int k = FIO_ReadFile(r->f, M.dma, n);
    if (k < 0)
        return -1;
    memcpy(buf, M.dma, (size_t)k);
    return k;
}
static int dk_read_at(void *f, uint32_t pos, uint8_t *dst, uint32_t n)
{
    if (FIO_SeekSkipFile(f, pos, 0) != (int64_t)pos)
        return -1;
    while (n) {
        uint32_t k = n > DK_RD ? DK_RD : n;
        if (FIO_ReadFile(f, M.dma, k) != (int)k)
            return -1;
        memcpy(dst, M.dma, k);
        dst += k;
        n -= k;
    }
    return 0;
}
struct dk_wr { void *f; uint32_t n; int err; };
static int dk_write_cb(void *ctx, const uint8_t *p, uint32_t n)
{
    struct dk_wr *w = ctx;
    while (n && !w->err) {
        uint32_t k = n > DK_RD ? DK_RD : n;
        memcpy(M.dma, p, k);
        if (FIO_WriteFile(w->f, M.dma, k) != (int)k)
            w->err = 1;
        p += k;
        n -= k;
        w->n += k;
    }
    return w->err ? -1 : 0;
}

/* ------------------------------------------------------------------ one frame */
enum { DK_OK = 0, DK_STOP, DK_ERR, DK_FATAL };
static const char *dk_why;
static int dk_stopk;                             /* why a run stopped: 1 MENU, 2 unload / power off */
static int dk_t[4];

static int dk_should_stop(void)
{
    if (dk_stop) { dk_why = "stopped"; dk_stopk = 1; return 1; }
    if (wf_unloading) { dk_why = "unload"; dk_stopk = 2; return 1; }
    if (ml_shutdown_requested) { dk_why = "power off"; dk_stopk = 2; return 1; }
    return 0;
}

static void dk_phase(int ph, int prog)
{
    dv.phase = ph;
    dv.prog = prog;
    dv_bump();
}

static int dk_frame(int i, const struct wf_look *lk, int src, int *bytes)
{
    char cr2[96], jpg[96], out[96], root[16], txt[200], t2[200];
    int t = get_ms_clock(), rc;
    dk_why = "";
    if (wf_roll_cr2(&roll, i, wf_dcim_dir(), wf_file_prefix(), "CR2", cr2, sizeof cr2) < 0
        || wf_roll_cr2(&roll, i, wf_dcim_dir(), wf_file_prefix(), "JPG", jpg, sizeof jpg) < 0
        || wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_out(&roll, i, root, out, sizeof out) < 0) {
        dk_why = "path";
        return DK_ERR;
    }
    /* 1. load: head, the lossless JPEG header, the decode */
    dk_phase(WF_DP_LOAD, 0);
    dv.print_ready = 0;
    memset(dv_print, 0, sizeof dv_print);
    uint32_t fsize = 0;
    if (FIO_GetFileSize(cr2, &fsize) != 0 || fsize < 4096) { dk_why = "no CR2"; return DK_ERR; }
    void *f = FIO_OpenFile(cr2, 0);
    if (!f || !DRY_OK(f)) { dk_why = "open CR2"; return DK_ERR; }
    uint32_t nh = fsize < WF_CR2_HEAD ? fsize : WF_CR2_HEAD;
    rc = dk_read_at(f, 0, M.in, nh) ? WF_CR2_E_READ : wf_cr2_parse(M.in, nh, fsize, &dk_inf);
    if (rc == 0) {
        uint32_t nl = fsize - dk_inf.strip_off < WF_CR2_LJHEAD ? fsize - dk_inf.strip_off : WF_CR2_LJHEAD;
        rc = dk_read_at(f, dk_inf.strip_off, M.in, nl) ? WF_CR2_E_READ : wf_cr2_parse_ljpeg(M.in, nl, &dk_inf);
    }
    if (rc == 0 && (dk_inf.w < DK_X0 + DK_W || dk_inf.h < DK_Y0 + DK_H || DK_IN < wf_cr2_inbuf_min(&dk_inf)
                    || (uint32_t)dk_inf.pitch * (uint32_t)dk_inf.h > DK_RAW_N))
        rc = WF_CR2_E_SIZE;
    struct dk_rd rdc = { f };
    if (rc == 0 && FIO_SeekSkipFile(f, dk_inf.strip_off, 0) != (int64_t)dk_inf.strip_off)
        rc = WF_CR2_E_READ;
    if (rc == 0)
        rc = wf_cr2_dec_begin(&dk_dec, &dk_inf, M.raw, dk_inf.pitch, M.in, DK_IN, dk_read_cb, &rdc);
    while (rc >= 0 && (rc = wf_cr2_dec_step(&dk_dec, DK_JROWS)) > 0) {
        dv.prog = dk_dec.jrow * 250 / (dk_dec.jh ? dk_dec.jh : 1);
        dv_bump();
        if (dk_should_stop()) { FIO_CloseFile(f); return DK_STOP; }
        msleep(2);
    }
    FIO_CloseFile(f);
    if (rc < 0) { dk_why = wf_cr2_err(rc); return DK_ERR; }
    uint32_t al = 0, ao = 0, jn = 0;
    if (FIO_GetFileSize(jpg, &jn) == 0 && jn > 1024) {   /* Canon's APP1 from the shot's own JPEG */
        void *g = FIO_OpenFile(jpg, 0);
        uint32_t n = jn < DK_APP1 ? jn : DK_APP1;
        if (g && DRY_OK(g)) {
            if (dk_read_at(g, 0, M.app1, n) != 0 || wf_dk_app1(M.app1, n, &ao, &al) != 0)
                al = 0;
            FIO_CloseFile(g);
        }
    }
    dk_t[0] = get_ms_clock() - t;
    t += dk_t[0];

    /* 2. expose: levels, scales, the develop in bands with the thumbnail */
    dk_phase(WF_DP_EXPOSE, 250);
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = M.raw; j.pitch = dk_inf.pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = dk_inf.w; j.ay1 = dk_inf.h;   /* EOS M CR2: image data from 72, 52 */
    j.x0 = DK_X0; j.y0 = DK_Y0; j.w = DK_W; j.h = DK_H; j.cfa = 0;
    int lb = 0, lw = 0;
    wf_dev_levels(M.scr, &j, 16, 64, &lb, &lw);
    j.black = lb ? lb : 2048;
    j.white = lw ? lw : wf_dk_white(WF_DK_EOSM, dk_inf.iso);
    int as = wf_dk_scales(WF_DK_EOSM, dk_inf.wb, j.black, j.white, dk_inf.iso, j.wb_q8);
    j.roll = src != WF_DEV_LOG;
    struct wf_dev_stat st;
    memset(&st, 0, sizeof st);
    if (wf_dev_prepare(M.scr, M.tabs, &j, &st) != 0) { dk_why = "prepare"; return DK_ERR; }
    uint32_t *frm = (uint32_t *)M.frame;
    wf_dk_thumb_begin(&dk_th, DK_W, DK_H);
    dv.print_ready = 1;
    for (int y = 0; y < DK_H; y += DK_BAND) {
        int yb = y + DK_BAND < DK_H ? y + DK_BAND : DK_H;
        wf_dev_rows(M.scr, y, yb, frm + y * (DK_W / 2), DK_W * 2);
        wf_dk_thumb_rows(&dk_th, frm + y * (DK_W / 2), DK_W * 2, y, yb - y, dv_print);
        dv.expose = yb * 1000 / DK_H;
        dv.prog = 250 + yb * 450 / DK_H;
        dv_bump();
        if (dk_should_stop())
            return DK_STOP;
        msleep(2);
    }
    dk_t[1] = get_ms_clock() - t;
    t += dk_t[1];

    /* 3. fix: the look's stage through the worker (films: table + effects; Log + LUT: effects; plain log: none) */
    dk_phase(WF_DP_FIX, 700);
    struct wf_frame fr;
    memset(&fr, 0, sizeof fr);
    fr.fmt = WF_FMT_UYVY8;
    fr.part = WF_PART_FULL;
    fr.aspect = 0;
    fr.devd = 1;
    fr.w = fr.fw = DK_W;
    fr.h = fr.fh = DK_H;
    fr.pitch = DK_W * 2;
    fr.data = fr.dst = frm;
    int status = -1, tries = 0;
    for (;;) {
        fr.t_post = get_ms_clock();
        if (wf_shot_run(&fr, &status) == 0)
            break;
        if (++tries >= 100 || dk_should_stop()) {
            if (!dk_why[0]) dk_why = "worker busy";
            return tries >= 100 ? DK_ERR : DK_STOP;
        }
        msleep(100);
    }
    dk_t[2] = get_ms_clock() - t;
    t += dk_t[2];

    /* 4. dry: the print (Canon's APP1 + our comment), written in chunks; the final thumbnail from the same bands */
    dk_phase(WF_DP_DRY, 800);
    snprintf(t2, sizeof t2, "%s", wf_exif_text(lk, lk->grain, lk->halo, lk->mist, WF_EV_NONE, txt, sizeof txt));
    const char *body = t2;
    static const char pre[] = "WindyFilm: ";
    int pk = 0;
    while (pre[pk] && body[pk] == pre[pk])
        pk++;
    if (!pre[pk])
        body += pk;
    snprintf(txt, sizeof txt, "WindyFilm Darkroom: %s | ROLL %03d F%02d", body, roll.num, i + 1);
    if (al && wf_dk_comment(M.app1 + ao, al, txt) != 0)
        wf_log("dark: F%02d no UserComment field in Canon's APP1", i + 1);
    struct wf_fe e = { 0, 0 };
    uint32_t osz = 0;
    if (FIO_GetFileSize(out, &osz) == 0 && wf_fremove(out, &e) != 0) {
        dk_why = "remove old print";
        return DK_FATAL;
    }
    void *o = wf_fopen_w(out, WF_FO_NEW, 0, &e);
    if (!o) { dk_why = "create print"; return DK_FATAL; }
    struct dk_wr wr = { o, 0, 0 };
    struct wf_jpe *je = (struct wf_jpe *)dk_jpe;
    rc = wf_jpe_bytes() <= sizeof dk_jpe ? wf_jpe_begin(je, DK_W, DK_H, 95, al ? M.app1 + ao : 0, (int)al, M.ob, DK_OB,
                                                        dk_write_cb, &wr) : WF_JPE_E_ARG;
    wf_dk_thumb_begin(&dk_th, DK_W, DK_H);
    int stopped = 0;
    for (int y = 0; rc == WF_JPE_OK && y < DK_H; y += 16) {
        int n = DK_H - y < 16 ? DK_H - y : 16;
        rc = wf_jpe_rows(je, frm + y * (DK_W / 2), DK_W * 2, n);
        wf_dk_thumb_rows(&dk_th, frm + y * (DK_W / 2), DK_W * 2, y, n, dv_print);
        if (!(y & 255)) {
            dv.prog = 800 + y * 200 / DK_H;
            dv_bump();
            if (dk_should_stop()) { stopped = 1; break; }
        }
    }
    if (rc == WF_JPE_OK && !stopped)
        rc = wf_jpe_end(je);
    FIO_CloseFile(o);
    if (stopped || rc != WF_JPE_OK || wr.err) {
        wf_fremove(out, &e);                     /* no partial print stays on the card */
        if (stopped)
            return DK_STOP;
        dk_why = wr.err ? "write (card full?)" : "encode";
        return DK_FATAL;
    }
    dk_t[3] = get_ms_clock() - t;
    *bytes = (int)wr.n;
    dv.print_ready = 2;
    wf_log("dark: F%02d IMG_%04d iso %d: load %d expose %d fix %d dry %d ms, %d KB, wb %s, trim %d, black %d white %d%s, "
           "look %s", i + 1, roll.file[i], dk_inf.iso, dk_t[0], dk_t[1], dk_t[2], dk_t[3], (int)(wr.n >> 10),
           as ? "as-shot" : "daylight", wf_dk_trim10(WF_DK_EOSM, dk_inf.iso), j.black, j.white, lw ? "" : " (table)",
           wf_status_name(status));
    return DK_OK;
}

static void dk_hang(int i)
{
    if (dv.nwire >= WF_DR_NWIRE) {                /* the oldest print leaves the wire */
        uint8_t *w0 = dv.wire[0];
        for (int k = 1; k < WF_DR_NWIRE; k++) {
            dv.wire[k - 1] = dv.wire[k];
            dv.wire_frame[k - 1] = dv.wire_frame[k];
        }
        dv.wire[WF_DR_NWIRE - 1] = w0;
        dv.nwire = WF_DR_NWIRE - 1;
    }
    wf_dk_wire(dv_print, dv.wire[dv.nwire]);
    dv.wire_frame[dv.nwire] = i;
    dv.nwire++;
    dv.hung_ms = get_ms_clock();
}

static void dk_task(void *arg)
{
    (void)arg;
    int t_run = get_ms_clock(), ndone = 0, nerr = 0, paused = 0, ms_sum = 0;
    dk_stopk = 0;
    const char *end_why = "";
    if (roll.state == WF_RS_LOADED && roll.nshot > 0) {
        wf_roll_rewind(&roll);
        roll_save();
    }
    /* RAW develop B gives its tables + scratch (shoot memory) back while the run lives (wf_rawb_poll sees
     * wf_dark_busy) and builds them again after it: wait for that here, so the frame's contiguous block has room */
    int t_b = get_ms_clock();
    while (wf_rawb_holds() && get_ms_clock() - t_b < 10000 && !dk_stop)   /* a cube build may take seconds */
        msleep(20);
    if (wf_rawb_holds())
        wf_log("dark: RAW develop still holds its memory after %d ms", get_ms_clock() - t_b);
    else if (WF_RAWB_CAN && wf_rawb)
        wf_log("dark: RAW develop tables given back (%d ms), built again after the run", get_ms_clock() - t_b);
    struct wf_look lk;
    wf_look_now(&lk);
    dv.total = roll.nshot;
    dv.done = wf_roll_ndev(&roll);
    dv.left = wf_roll_left(&roll);
    wf_log("dark: roll %03d start, %d of %d to develop, look output %d", roll.num, dv.left, dv.total, lk.output);
    if (lv) {
        PauseLiveView();
        paused = 1;
    }
    powersave_prohibit();
    int src = lk.output == WF_OUT_FILM ? WF_DEV_SRGB : lk.output == WF_OUT_LOG ? WF_DEV_LOG
            : lk.output == WF_OUT_LLUT ? WF_DEV_CUBE : 0;
    if (!src) {
        end_why = "no look (output Off)";
    } else if (dk_alloc((int)DK_RAW_N + 4096, DK_W * DK_H * 2) != 0) {
        end_why = "no memory";
    } else {
        char cn[WF_LL_NAME];
        const char *cube = 0;
        struct wf_ll_info li;
        int rc = WF_LL_OK;
        if (src == WF_DEV_CUBE && !(cube = wf_loglut_name(cn, lk.curve, lk.flut, 0)))
            rc = WF_LL_E_MODE;
        if (rc == WF_LL_OK)
            rc = wf_dev_build(M.tabs, src, src == WF_DEV_SRGB ? 0 : lk.curve, WF_DEV_EOSM, cube,
                              src == WF_DEV_CUBE ? lk.lei : 0, src == WF_DEV_CUBE && lk.flut != 0, &li);
        if (rc != WF_LL_OK) {
            wf_log("dark: tables err %d %s", rc, wf_loglut_errstr(rc));
            end_why = "develop tables";
        }
    }
    if (!end_why[0]) {                           /* <root>ROLLS and <root>ROLLS/ROLLnnn */
        char root[16], d[48];
        struct wf_fe e = { 0, 0 };
        d[0] = 0;
        if (wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_dir(&roll, root, 0, d, sizeof d) < 0
            || wf_fmkdir(d, &e) != 0 || wf_roll_dir(&roll, root, 1, d, sizeof d) < 0 || wf_fmkdir(d, &e) != 0) {
            wf_log("dark: folder %s: %s %d", d, wf_fe_name(e.step), e.rc);
            end_why = "cannot make the ROLLS folder";
        }
    }
    while (!end_why[0]) {
        int i = wf_roll_next_dev(&roll);
        if (i < 0)
            break;
        int bat = dk_battery();
        dv.battery = bat;
        if (bat >= 0 && bat < DK_BAT_STOP) {
            end_why = "Battery low: charge, then develop the rest";
            break;
        }
        dv.frame = i;
        dv.left = wf_roll_left(&roll);
        dk_t0 = get_ms_clock();
        int bytes = 0, r = dk_frame(i, &lk, src, &bytes);
        if (r == DK_STOP) {
            end_why = dk_why;
            break;
        }
        if (r == DK_OK) {
            wf_roll_mark(&roll, i, WF_RF_DEV);
            ndone++;
            ms_sum += get_ms_clock() - dk_t0;
            dv.avg_ms = ms_sum / ndone;
            dk_hang(i);
        } else {
            wf_log("dark: F%02d IMG_%04d failed: %s", i + 1, roll.file[i], dk_why);
            if (r == DK_FATAL) {
                end_why = dk_why;
                break;
            }
            wf_roll_mark(&roll, i, WF_RF_ERR);
            nerr++;
        }
        if (roll_save() != 0)
            wf_log("dark: roll file not saved");
        dv.done = wf_roll_ndev(&roll);
        dv.left = wf_roll_left(&roll);
        roll_changed();
        dv_bump();
    }
    dk_free();
    powersave_permit();
    if (paused)
        ResumeLiveView();
    int left = wf_roll_left(&roll);
    dv.frame = -1;
    dv.phase = WF_DP_NONE;
    dv.left = left;
    dv.msg = end_why;
    dv.state = !left ? WF_DR_DONE : dk_stopk || !end_why[0] ? WF_DR_STOPPED : WF_DR_FAIL;
    wf_log("dark: roll %03d end: %d developed, %d failed, %d left, %d s%s%s", roll.num, ndone, nerr, left,
           (get_ms_clock() - t_run) / 1000, end_why[0] ? ": " : "", end_why);
    roll_changed();
    dv_bump();
    WF_BARRIER();
    dk_running = 0;
}

#else
int wf_dark_start(void) { dv.msg = "The Darkroom runs on the EOS M first"; dv_bump(); return -3; }
void wf_dark_stop(void) {}
#endif

#if WF_DARK_CAN
int wf_dark_start(void)
{
    if (dk_running)
        return -2;
    if (!wf_cam_ok || wf_unloading || wf_job_state() || is_movie_mode() || req_kind || wf_rawb_busy())
        return -3;                               /* (a shot's B develop still runs: 30 s) */
    if (wf_cam() != WF_CAM_EOSM) {
        dv.msg = "The Darkroom runs on the EOS M first";
        dv_bump();
        return -3;
    }
    int n = roll.state == WF_RS_LOADED ? roll.nshot : wf_roll_left(&roll);
    if (n <= 0)
        return -1;
    int bat = dk_battery();
    dv.battery = bat;
    if (bat >= 0 && bat < DK_BAT_GO) {
        dv.msg = "Battery low: charge it first";
        dv_bump();
        return -3;
    }
    dk_stop = 0;
    dv.state = WF_DR_RUN;
    dv.phase = WF_DP_LOAD;
    dv.frame = -1;
    dv.msg = "";
    dv.prog = dv.expose = 0;
    dv.print_ready = 0;
    dv.nwire = 0;
    dv.avg_ms = 0;
    dk_t0 = get_ms_clock();
    WF_BARRIER();
    dk_running = 1;                              /* wf_task leaves the roll alone from here */
    int tid = task_create("wf_dark", DK_PRIO, 0x2000, (void *)dk_task, 0);
    if (!DRY_OK(tid)) {
        dk_running = 0;
        dv.state = WF_DR_FAIL;
        dv.msg = "no task";
        dv_bump();
        return -3;
    }
    dv_bump();
    return 0;
}

void wf_dark_stop(void)
{
    if (!dk_running)
        return;
    dk_stop = 1;
    dv.state = WF_DR_STOPPING;
    dv_bump();
}

#endif

void wf_dark_init(void)
{
    memset(&dv, 0, sizeof dv);
    dv.frame = -1;
    dv.battery = -1;
    dv.msg = "";
    dv.print = dv_print;
    for (int i = 0; i < WF_DR_NWIRE; i++)
        dv.wire[i] = dv_wire[i];
}

/* wf_task at unload: a run stops at its next band; Canon's quality / review back */
void wf_dark_end(void)
{
    dk_stop = 1;
    for (int i = 0; i < 300 && dk_running; i++)
        msleep(20);
    if (pq_old >= 0 && pic_quality != pq_old)
        prop_request_change(WF_PROP_PIC_QUALITY, &pq_old, 4);
    if (rv_old >= 0 && image_review_time == 0)
        prop_request_change(WF_PROP_REVIEW, &rv_old, 4);
    if (roll_dirty)
        roll_save();
    if (hud_fonts_on)
        wfdr_hud_fonts(0);
}
