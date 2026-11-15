/* wf_shot.c - WindyFilm shot pipeline (WindyStrife): the worker task, the look of a shot (struct wf_look), the dispatch on
 * (frame format, output), the chunk loop with the effects, the shot log and EXIF entries, the Log + LUT tables, and the
 * film table pack ML/DATA/WFFILMS.DAT.
 *
 * Request flow: a Canon develop task enters our hook (camera layer, wf_cam_*.c), which gates the call, fills a struct
 * wf_frame and calls wf_shot_run(): one request at a time (wf_lock), handed to the worker film_yuv (prio 0x1C) through
 * wf_go, the hook task blocking on wf_done until the worker has finished writing the buffer. The worker never logs and
 * never does file I/O: results go through a single-producer event ring that wf_task turns into log lines, the per-shot
 * summary and the EXIF entry (wf_shot_poll).
 *
 * Look: resolved once per NN / TOP request from the state (wf_look_now); a BOT half uses its TOP's look, film table,
 * Log + LUT decision, effect decision and seed, so a frame is never half graded.
 *
 * Shared state between wf_task and the worker (one in-order core, compiler barriers):
 *   the film table: wf_task loads it from the pack into one of two buffers and publishes it by one pointer store; the
 *     worker holds the buffer it builds from (wf_ws_hold), wf_task never writes a published or held buffer.
 *   Log + LUT tables: wf_task clears wf_ll_have, waits until the worker is idle (or parked) and no BOT half is owed the
 *     tables, rewrites / frees them, publishes wf_ll_have last.
 *   effect state: wf_fx.c, the same protocol (fx_wait).
 *   A worker waiting for wf_task (film table, effect keys) sits "parked" (wf_parked): wf_task's wait loops count it idle.
 *
 * RAW input (docs/DESIGN.md 3.1): RAW develop "B" (wf_rawb.c) takes a WF_FMT_RAW14 frame at wf_dispatch() and writes
 * UYVY8 into frame.dst; the look's own stage then runs on dst (films: the film table + effects on the sRGB base
 * develop; Log + LUT: the effects only, the cube is in the develop; plain log: nothing). A WF_FMT_LOG16 buffer for a
 * 16-bit look path stays reserved. */
#include "wf_core.h"
#include "wf_film.h"
#include "wf_loglut.h"
#include "wf_fx.h"
#include "wf_rawb.h"

extern int  task_create(const char *name, uint32_t prio, uint32_t stack, void *entry, void *arg);
extern void *create_named_semaphore(const char *name, int initial);
extern int  take_semaphore(void *sem, int timeout_ms);
extern int  give_semaphore(void *sem);
extern uint64_t get_us_clock(void);
extern void *FIO_OpenFile(const char *name, unsigned mode);
extern int   FIO_ReadFile(void *f, void *buf, size_t n);
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
#define WF_WORKER_PRIO 0x1C                      /* below Canon GUI (0x19), above TaskMain (0x1D) */
#define WF_SHOT_LINE 110                         /* a log line holds 120 chars with the time prefix */

volatile int wf_busy, wf_parked, wf_half_valid, wf_half_h, wf_half_g, wf_half_m;
volatile int wf_pv_busy, wf_req_live;
static void *wf_lock, *wf_go, *wf_done;
static volatile int wf_worker_ok;
static struct wf_frame wf_req;                   /* the request (copied by wf_shot_run) */
static volatile int wf_req_status;

int wf_worker_up(void) { return wf_worker_ok; }
int wf_yuv_active(void) { return wf_cam_shot_ok() && !wf_unloading; }
/* halation (+ bloom) is built for the 3:2 frame: aspect 0, or -1 = never reported (taken as 3:2) */
int wf_aspect_has_halo(int aspect) { return aspect <= 0; }

/* ------------------------------------------------------------------ chunks through the cached scratch */
/* 8-word LDM/STM bursts: r0 dst, r1 src, r2 words, a multiple of 8 and > 0 (no LDRD / STRD) */
extern void wf_copy8(uint32_t *dst, const uint32_t *src, int words);
__asm__(".text\n .align 2\n .global wf_copy8\n .type wf_copy8, %function\n"
        "wf_copy8:\n"
        " stmfd sp!, {r4-r10}\n"
        "1: ldmia r1!, {r3-r10}\n"
        " stmia r0!, {r3-r10}\n"
        " subs r2, r2, #8\n"
        " bgt 1b\n"
        " ldmfd sp!, {r4-r10}\n"
        " bx lr\n"
        " .size wf_copy8, .-wf_copy8\n");

void wf_copyw(uint32_t *d, const uint32_t *s, int n)
{
    int m8 = n & ~7;
    if (m8)
        wf_copy8(d, s, m8);
    for (int i = m8; i < n; i++)
        d[i] = s[i];
}

/* the frame is the uncached alias: 288-word bursts (TCC places sections 16-aligned; nothing needs more) */
static uint32_t wf_scr[WF_CHUNK] __attribute__((aligned(32)));

static void wf_chunks(uint32_t *buf, int n, wf_words_fn *fn, int arg)
{
    for (int o = 0; o < n; o += WF_CHUNK) {
        int m = n - o < WF_CHUNK ? n - o : WF_CHUNK;
        wf_copyw(wf_scr, buf + o, m);
        fn(wf_scr, m, arg);
        wf_copyw(buf + o, wf_scr, m);
    }
}

/* words of the next segment: at most a chunk, never past the end of a row (x0 = pixel position in the row), so every
 * effect call is one row segment (EOS M rows are whole chunks; a 5D2 half row is 2808 words) */
static int wf_seg(int left, int x0, int W)
{
    int r = (W - x0) / 2, m = left < WF_CHUNK ? left : WF_CHUNK;
    return m < r ? m : r;
}

static uint32_t wf_us(void) { return (uint32_t)get_us_clock(); }
static uint32_t wf_dt(uint32_t a, uint32_t b) { return b - a; }

/* ------------------------------------------------------------------ the look of a shot */
int wf_cat_index(int id);
/* the variant a shot really uses: the wanted one if the film HAS it, else the film's first variant; and if the pack
 * does not CARRY that variant at all, the first variant it does carry (the chain films have no Scan - their variants
 * are the sixteen print stocks - so the first-variant fallback is what makes them usable). A variant the pack carries
 * but whose table fails its checksum is kept, not swapped: the shot then ends with ": notab" as it always did. */
static int wf_variant_eff(int k, int v)
{
    const struct wf_film *f = &wf_films[k];
    int w;
    if (v < 0 || v >= WF_NPRINTS || !(f->prints & (1u << v))) {
        for (v = 0; v < WF_NPRINTS && !(f->prints & (1u << v)); v++)
            ;
        if (v >= WF_NPRINTS)
            v = WF_PRINT_SCAN;
    }
    if (wf_ws_state() == 1 && !wf_ws_listed(k, v)) {
        for (w = 0; w < WF_NPRINTS; w++)
            if ((f->prints & (1u << w)) && wf_ws_listed(k, w)) {
                v = w;
                break;
            }
    }
    return v;
}

int wf_look_now(struct wf_look *lk)
{
    int out = wf_out, k = wf_cat_index(wf_film_id), c = wf_curve, l = wf_flut, s = wf_strength;
    memset(lk, 0, sizeof *lk);
    lk->row = -1;
    lk->curve = c >= 1 && c <= WF_NCURVE ? c : WF_CURVE_VLOG;
    lk->flut = l > 0 && l < WF_FLUT_N ? l : 0;
    lk->lei = (int8_t)wf_lut_ei_t();
    lk->incurve = (uint8_t)wf_incurve_t();
    lk->strength = s >= 0 && s < WF_NSTRENGTH ? s : 1;
    lk->grain = (uint8_t)wf_grain; lk->halo = (uint8_t)wf_halo; lk->mist = (uint8_t)wf_mist;
    lk->bloom = (uint8_t)wf_bloom; lk->gauge = (uint8_t)wf_gauge;
    lk->light = (uint8_t)wf_light;
    switch (out) {
    case WF_OUT_FILM:
        if (k < 0 || (wf_films[k].flags & (WF_F_LUTCAN | WF_F_READY)) != WF_F_READY) {
            lk->output = WF_OUT_OFF;
            lk->why = 1;                         /* no film loaded (or a can: Log + LUT is its output) */
            break;
        }
        lk->output = WF_OUT_FILM;
        lk->row = (int16_t)k;
        lk->variant = (uint8_t)wf_variant_eff(k, wf_print);
        lk->hl = (int8_t)wf_hl_t();
        lk->sh = (int8_t)wf_sh_t();
        lk->ftflags = wf_cat_ftflags[k] | WF_FT_S(wf_strength_pct(lk->strength)) | WF_FT_HS(lk->hl, lk->sh);
        lk->post = WF_POST_FILM + k;
        break;
    case WF_OUT_LOG:
        lk->output = WF_OUT_LOG;
        break;
    case WF_OUT_LLUT:
        lk->output = WF_OUT_LLUT;
        lk->post = WF_POST_LLUT;
        lk->hl = (int8_t)wf_hl_t();               /* the menu's; a shot gets the tables' (wf_dispatch) */
        lk->sh = (int8_t)wf_sh_t();
        if (k >= 0 && wf_cat_lut[k] && wf_cat_lut[k] == lk->flut)
            lk->row = (int16_t)k;                /* its Film LUT can is loaded: the shot is titled by the can */
        break;
    default:
        lk->output = WF_OUT_OFF;
        break;
    }
    return lk->output;
}

int wf_post_now(void)
{
    struct wf_look lk;
    wf_look_now(&lk);
    return lk.post;
}

/* ------------------------------------------------------------------ Log + LUT tables (wf_task writes, worker reads) */
static struct wf_lltabs *wf_ll_tabs;             /* 181,356 B, malloc'd only while Log + LUT is the output */
static volatile int wf_ll_have, wf_ll_loading;
static volatile int wf_ll_have_lut, wf_ll_have_ei, wf_ll_have_inc, wf_ll_have_hs;
static int wf_ll_tried, wf_ll_tried_lut, wf_ll_tried_ei, wf_ll_tried_inc, wf_ll_tried_hs;
/* Highlights / Shadows on Log + LUT: the loaded tables' N before the bend (the .FRT cache holds the tables unbent), so a
   change of only Highlights / Shadows bends N again without a card read; WF_FT_HS packing (0 = 0 / 0) */
static uint8_t wf_ll_n0[256];
static int wf_ll_hs_now(void) { return WF_FT_HS(wf_hl_t(), wf_sh_t()); }
static volatile int wf_half_ll;                  /* curve the TOP half was graded with, 0 = plain */
static int wf_ll_rc[WF_NCURVE] = { -1, -1, -1, -1, -1 };
static struct wf_ll_info wf_ll_inf[WF_NCURVE];

int wf_ll_ready(int post)
{
    if (post != WF_POST_LLUT)
        return 1;
    int c = wf_curve;
    return wf_ll_have && wf_ll_have == c;
}
int wf_ll_have_curve(void) { return wf_ll_have; }
void *wf_ll_tables(void) { return wf_ll_tabs; }

int wf_post_want(void)
{
    int p = wf_post_now();
    return p && wf_yuv_active() && wf_ll_ready(p) ? p : 0;
}

/* menu: -2 loading or about to load, -1 not tried, else the last result */
int wf_ll_state(int curve)
{
    if (curve < 1 || curve > WF_NCURVE)
        return -1;
    if (wf_ll_loading == curve || (wf_out == WF_OUT_LLUT && wf_curve == curve
                                   && (wf_ll_tried != curve || wf_ll_tried_lut != wf_flut || wf_ll_tried_ei != wf_lut_ei_t()
                                       || wf_ll_tried_inc != wf_incurve_t() || wf_ll_tried_hs != wf_ll_hs_now())))
        return -2;
    return wf_ll_rc[curve - 1];
}

static void wf_ll_poll(void)
{
    int c = wf_curve, l = wf_flut;
    int want = wf_yuv_active() && wf_out == WF_OUT_LLUT && c >= 1 && c <= WF_NCURVE ? c : 0;
    int lut = want && l > 0 && l < WF_FLUT_N ? l : 0;
    int ei = want ? wf_lut_ei_t() : 0, inc = want ? wf_incurve_t() : 0, hs = want ? wf_ll_hs_now() : 0;
    int same = want == wf_ll_tried && lut == wf_ll_tried_lut && ei == wf_ll_tried_ei && inc == wf_ll_tried_inc;
    if (same && hs == wf_ll_tried_hs)
        return;
    int had = wf_ll_have;
    wf_ll_have = 0;                              /* new shots stop using the tables */
    WF_BARRIER();
    /* wait until nothing reads them: a shot in the worker (not parked: a parked worker holds a film, never these),
     * a preview frame, then the BOT half of a TOP graded with them (5 s) */
    for (int t = 0, tb = 0;;) {
        int busy = wf_busy && !wf_parked;
        int owed = wf_half_valid && wf_half_ll;
        busy |= wf_busy && !wf_parked;
        busy |= wf_pv_busy;
        if (busy) {
            if (++t > 1000) {                    /* stuck for 10 s: keep the tables, retry later */
                wf_ll_have = had;
                return;
            }
        } else if (owed) {
            if (++tb > 500) {
                wf_half_ll = 0;
                WF_BARRIER();
                wf_log("lut: no BOT half in 5 s, that shot is half graded");
                continue;
            }
        } else {
            break;
        }
        wf_ws_poll();
        msleep(10);
    }
    wf_ll_tried_hs = hs;
    if (same && want && had == want && wf_ll_tabs) {   /* only Highlights / Shadows changed: bend N again, no load */
        memcpy(wf_ll_tabs->N, wf_ll_n0, sizeof wf_ll_n0);
        wf_hs_bend(wf_ll_tabs->N, WF_FT_HOF(hs), WF_FT_SHOF(hs));
        wf_log("lut: highlights %s shadows %s", wf_hs_text(WF_FT_HOF(hs)), wf_hs_text(WF_FT_SHOF(hs)));
        wf_ll_have_hs = hs;
        WF_BARRIER();
        wf_ll_have = want;
        wf_gen_bump();
        return;
    }
    wf_ll_tried = want;
    wf_ll_tried_lut = lut;
    wf_ll_tried_ei = ei;
    wf_ll_tried_inc = inc;
    if (wf_ll_tabs && !want) {
        __mem_free(wf_ll_tabs);
        wf_ll_tabs = 0;
    }
    if (!want)
        return;
    struct wf_ll_info *in = &wf_ll_inf[want - 1];
    char cn[WF_LL_NAME], fn[WF_LL_NAME];
    const char *cp = wf_loglut_name(cn, want, lut, 0), *nm = cp ? cp + 8 : "?";
    int rc = WF_LL_E_NOMEM, t0 = get_ms_clock();
    if (!wf_ll_tabs)
        wf_ll_tabs = __mem_malloc(sizeof *wf_ll_tabs, 0, "wf_shot.c", __LINE__);
    wf_ll_loading = want;
    wf_ll_ei = ei;
    wf_ll_auto = lut != 0 && !WF_FLUT_DISP(lut); /* Film LUT cubes: exposed for 18 % grey -> 118 (Technical, Leica Vivid: as is) */
    wf_ll_disp = WF_FLUT_DISP(lut);              /* Leica Vivid: the cube reads the JPEG's sRGB codes, no log */
    wf_ll_incurve = inc;                         /* a Canon curve slot instead of sRGB (cache key bits 17-18) */
    if (wf_ll_tabs && cp)
        rc = wf_loglut_load_cached(cp, wf_loglut_name(fn, want, lut, 1), want, wf_ll_tabs, in);
    else
        in->line = 0;
    int ms = get_ms_clock() - t0;
    wf_ll_rc[want - 1] = rc;
    wf_ll_loading = 0;
    if (rc == WF_LL_OK) {
        memcpy(wf_ll_n0, wf_ll_tabs->N, sizeof wf_ll_n0);
        wf_hs_bend(wf_ll_tabs->N, WF_FT_HOF(hs), WF_FT_SHOF(hs));   /* after the cache write: the .FRT stays unbent */
        char eis[64] = "";
        int n = 0, av = (int8_t)wf_ll_tabs->pad[0];
        int ch = wf_incurve > WF_INC_SRGB && wf_incurve < WF_INC_N && wf_ll_curve_have(wf_incurve) ? wf_incurve : 0;
        int why = wf_style_why(ch);
        if (inc)                                 /* the curve, with what the camera reported (base ff / c-10: not yet) */
            n = snprintf(eis, sizeof eis, " in-%s b%02x c%d", inc == WF_INC_FILM1 ? "film1" : "neutral",
                         wf_style_base() & 0xff, wf_style_con());
        else if (ch && why == WF_PSW_BASE)       /* a curve chosen, the camera's style does not fit it */
            n = snprintf(eis, sizeof eis, " srgb(base %02x)", wf_style_base());
        else if (ch && why == WF_PSW_CONTRAST)
            n = snprintf(eis, sizeof eis, " srgb(contrast %d)", wf_style_con());
        else if (ch)
            n = snprintf(eis, sizeof eis, " srgb(style %02x)", wf_style_raw());
        if (ei)
            n += snprintf(eis + n, sizeof eis - n, " ei%s%d/3", ei < 0 ? "-" : "+", ei < 0 ? -ei : ei);   /* no %+d */
        if (wf_ll_tabs->pad[1] == WF_LL_AUTO_MARK) {
            int a = av < 0 ? -av : av, u = 0;
            while (a >= 10) { a -= 10; u++; }
            n += snprintf(eis + n, sizeof eis - n, " auto%s%d.%dEV", av < 0 ? "-" : "+", u, a);
        }
        if (hs && n < (int)sizeof eis)
            snprintf(eis + n, sizeof eis - n, " hl%s sh%s", wf_hs_text(WF_FT_HOF(hs)), wf_hs_text(WF_FT_SHOF(hs)));
        wf_log("lut %s %d^3 %s %dms c%d s%d%s%s '%s'", nm, in->n, in->cached ? "cache" : "ok", ms,
               in->clamped, in->skipped, in->domain ? " dom" : "", eis, in->title);
        wf_ll_have_lut = lut;
        wf_ll_have_ei = ei;
        wf_ll_have_inc = inc;
        wf_ll_have_hs = hs;
        WF_BARRIER();
        wf_ll_have = want;                       /* published last */
    } else {
        wf_log("lut %s err %d %s line %d (%dms)", nm, rc, wf_loglut_errstr(rc), in->line, ms);
        if (wf_ll_tabs) {
            __mem_free(wf_ll_tabs);
            wf_ll_tabs = 0;
        }
    }
    wf_gen_bump();
}

/* ------------------------------------------------------------------ ML/DATA/WFFILMS.DAT (docs/DESIGN.md 4.3) */
#define WF_WS_PACK   "ML/DATA/WFFILMS.DAT"
#define WF_WS_WORDS  4913
#define WF_WS_BYTES  (WF_WS_WORDS * 4)
#define WF_WS_HDR    32
#define WF_WS_IENT   24
#define WF_WS_CHUNK  8192
#define WF_WS_WAIT_MS 1000                       /* worker: a table being loaded */
#define WF_WS_MAXROW 128                         /* catalogue rows (ids 26..127: at most 102) */
#define WF_WS_KEYS   (WF_WS_MAXROW * WF_NPRINTS)
#define WF_WS_KEY(row, v) ((row) * WF_NPRINTS + (v))
struct wf_wsbuf { uint32_t T[WF_WS_WORDS]; int key; uint32_t ser; };
static struct wf_wsbuf wf_wsb[2];
static struct wf_wsbuf *volatile wf_ws_pub;
static const struct wf_wsbuf *volatile wf_ws_hold;
static uint32_t wf_ws_off[WF_WS_KEYS], wf_ws_fnv[WF_WS_KEYS];
static volatile uint8_t wf_ws_ok[WF_WS_KEYS];
static uint8_t wf_ws_checked[WF_WS_KEYS];
static volatile int wf_ws_st;                    /* 0 index not read, 1 read, < 0 missing (-1) / bad (-2..) */
static uint32_t wf_ws_serial;
static int wf_ws_sweep_i, wf_ws_shot_key = -1, wf_ws_waited;
static uint32_t wf_ws_shot_ser;
static int wf_half_ws;

int wf_ws_state(void) { return wf_ws_st; }
int wf_ws_has(int row, int v)
{
    return row >= 0 && row < wf_nfilms && row < WF_WS_MAXROW && v >= 0 && v < WF_NPRINTS && wf_ws_st == 1
        && wf_ws_ok[WF_WS_KEY(row, v)];
}
/* the pack's index LISTS this film and variant, whatever became of its table afterwards: a table that fails its
 * checksum is listed but not good, and that difference is what keeps a corrupt table from being swapped for another
 * look (the shot must end with ": notab") while a variant the pack simply does not carry falls back to one it does. */
int wf_ws_listed(int row, int v)
{
    return row >= 0 && row < wf_nfilms && row < WF_WS_MAXROW && v >= 0 && v < WF_NPRINTS && wf_ws_st == 1
        && wf_ws_off[WF_WS_KEY(row, v)] != 0;
}

static uint32_t wf_fnv1(uint32_t h, const uint8_t *p, uint32_t n)
{
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static uint32_t wf_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* n bytes at pos into p (0 = checksum only) through the DMA buffer; words: table words checked on the way (top byte 0) */
static int wf_ws_rd(void *f, char *dma, uint32_t pos, uint8_t *p, uint32_t n, uint32_t *h, int words)
{
    if (FIO_SeekSkipFile(f, pos, 0) != (int64_t)pos)
        return -1;
    while (n) {
        int k = n > WF_WS_CHUNK ? WF_WS_CHUNK : (int)n;
        if (FIO_ReadFile(f, dma, k) != k)
            return -1;
        *h = wf_fnv1(*h, (const uint8_t *)dma, k);
        if (words)
            for (int i = 0; i + 3 < k; i += 4)
                if (((const uint8_t *)dma)[i + 3])
                    return -6;
        if (p) {
            memcpy(p, dma, k);
            p += k;
        }
        n -= k;
    }
    return 0;
}

static void wf_ws_index(void)
{
    uint32_t sz = 0, h = 2166136261u;
    int rc = -1, nok = 0, t0 = get_ms_clock();
    void *f = 0;
    uint8_t *ix = 0;
    char *dma = __mem_malloc(WF_WS_CHUNK, WF_MEM_DMA | WF_MEM_TMP, "wf_shot.c", __LINE__);
    if (!dma)
        return;
    if (FIO_GetFileSize(WF_WS_PACK, &sz) == 0 && (f = FIO_OpenFile(WF_WS_PACK, 0)) != 0) {
        uint8_t hd[WF_WS_HDR];
        rc = wf_ws_rd(f, dma, 0, hd, WF_WS_HDR, &h, 0) ? -2 : 0;
        uint32_t n = hd[6] | hd[7] << 8, ioff = wf_u32(hd + 16), doff = wf_u32(hd + 20);
        if (!rc && (hd[0] != 'W' || hd[1] != 'F' || hd[2] != 'F' || hd[3] != 'T' || (hd[4] | hd[5] << 8) != 1
                    || wf_u32(hd + 8) != WF_WS_WORDS || wf_u32(hd + 12) != WF_WS_BYTES || ioff != WF_WS_HDR
                    || n == 0 || n > 1024 || doff < ioff + n * WF_WS_IENT || doff > sz))
            rc = -3;                             /* not our pack (format / version) */
        if (!rc && !(ix = __mem_malloc(n * WF_WS_IENT, WF_MEM_TMP, "wf_shot.c", __LINE__)))
            rc = -4;
        h = 2166136261u;
        if (!rc && (wf_ws_rd(f, dma, ioff, ix, n * WF_WS_IENT, &h, 0) || h != wf_u32(hd + 24)))
            rc = -5;                             /* index unreadable / its checksum */
        for (uint32_t i = 0; !rc && i < n; i++) {
            const uint8_t *e = ix + i * WF_WS_IENT;
            int k = wf_cat_index(e[0]), v = e[1];
            uint32_t off = wf_u32(e + 12);
            if (k < 0 || k >= WF_WS_MAXROW || v >= WF_NPRINTS || (wf_films[k].flags & WF_F_LUTCAN)
                || (e[2] & 3) != (wf_cat_ftflags[k] & 3) || wf_u32(e + 16) != WF_WS_BYTES
                || off < doff || off > sz || sz - off < WF_WS_BYTES)
                continue;                        /* not in this build's catalogue / bad place */
            const char *code = wf_films[k].code;
            int same = 1, end = 0;
            for (int c = 0; c < 8; c++) {
                char cc = end ? 0 : code[c];
                if (!cc)
                    end = 1;
                same &= e[4 + c] == (uint8_t)cc;
            }
            int key = WF_WS_KEY(k, v);
            if (!same || wf_ws_ok[key])
                continue;                        /* id and code must both match (and once) */
            wf_ws_off[key] = off;
            wf_ws_fnv[key] = wf_u32(e + 20);
            wf_ws_ok[key] = 1;
            nok++;
        }
    }
    if (ix) __mem_free(ix);
    if (dma) __mem_free(dma);
    if (f) FIO_CloseFile(f);
    if (rc == -4)
        return;                                  /* no memory for the index: next poll */
    wf_ws_st = rc ? rc : 1;
    if (rc)
        wf_log("pack %s %s (%d): films stay plain", WF_WS_PACK, rc == -1 ? "missing" : "bad", rc);
    else
        wf_log("pack ok: %d tables, index %dms, catalog %d films", nok, get_ms_clock() - t0, wf_nfilms);
    wf_gen_bump();
}

static int wf_ws_read(int key, uint32_t *T)
{
    uint32_t h = 2166136261u;
    void *f = FIO_OpenFile(WF_WS_PACK, 0);
    char *dma = f ? __mem_malloc(WF_WS_CHUNK, WF_MEM_DMA | WF_MEM_TMP, "wf_shot.c", __LINE__) : 0;
    int rc = !f ? -1 : !dma ? -2 : wf_ws_rd(f, dma, wf_ws_off[key], (uint8_t *)T, WF_WS_BYTES, &h, 1);
    if (!rc && h != wf_ws_fnv[key])
        rc = -4;
    if (dma) __mem_free(dma);
    if (f) FIO_CloseFile(f);
    return rc;
}

static void wf_ws_bad(int key, int rc)
{
    wf_ws_ok[key] = 0;
    wf_log("pack %s v%d: table bad (%d), shots stay plain", wf_films[key / WF_NPRINTS].code, key % WF_NPRINTS, rc);
    wf_gen_bump();
}

/* the table the next shot (and the preview) uses: key + 1, 0 = none */
static int wf_ws_want(void)
{
    struct wf_look lk;
    if (wf_look_now(&lk) != WF_OUT_FILM || lk.row >= WF_WS_MAXROW)
        return 0;
    return WF_WS_KEY(lk.row, lk.variant) + 1;
}

/* wf_task (its loop and the 10 ms wait loops of wf_ll_poll / fx_wait): the index once, then the wanted table, else one
 * sweep step (a bad entry is found before anyone picks it) */
void wf_ws_poll(void)
{
    if (!wf_yuv_active())
        return;
    if (!wf_ws_st)
        wf_ws_index();
    if (wf_ws_st <= 0)
        return;
    int w = wf_ws_want() - 1;
    struct wf_wsbuf *p = wf_ws_pub;
    if (w >= 0 && wf_ws_ok[w] && !(p && p->key == w)) {
        struct wf_wsbuf *x = p == &wf_wsb[0] ? &wf_wsb[1] : &wf_wsb[0];
        if (x == wf_ws_hold)
            return;                              /* the worker still builds from it */
        int t0 = get_ms_clock();
        x->key = -1;
        int rc = wf_ws_read(w, x->T);
        if (rc == -2)
            return;                              /* no DMA buffer now: next poll */
        if (rc) {
            wf_ws_bad(w, rc);
            return;
        }
        x->ser = ++wf_ws_serial;
        x->key = w;
        WF_BARRIER();
        wf_ws_pub = x;
        wf_ws_checked[w] = 1;
        wf_log("pack %s v%d loaded %dms", wf_films[w / WF_NPRINTS].code, w % WF_NPRINTS, get_ms_clock() - t0);
        return;
    }
    int nk = wf_nfilms * WF_NPRINTS;
    if (nk > WF_WS_KEYS)
        nk = WF_WS_KEYS;
    for (int n = 0; n < nk; n++) {
        int i = wf_ws_sweep_i;
        wf_ws_sweep_i = i + 1 < nk ? i + 1 : 0;
        if (!wf_ws_ok[i] || wf_ws_checked[i])
            continue;
        int rc = wf_ws_read(i, 0);
        if (rc == -2)
            return;
        if (rc)
            wf_ws_bad(i, rc);
        else
            wf_ws_checked[i] = 1;
        return;
    }
}

/* worker: 10 ms asleep, parked (wf_task's waits go ahead meanwhile; nothing shared is read here) */
static void wf_park_sleep(void)
{
    wf_parked = 1;
    WF_BARRIER();
    msleep(10);
    WF_BARRIER();
    wf_parked = 0;
    WF_BARRIER();
}

/* worker: the published table of key, held until the caller clears wf_ws_hold; 0 = not within ~1 s / not in the pack */
static const struct wf_wsbuf *wf_ws_take(int key)
{
    int t0 = get_ms_clock();
    for (;;) {
        const struct wf_wsbuf *b = wf_ws_pub;
        wf_ws_hold = b;
        WF_BARRIER();
        if (b && b == wf_ws_pub && b->key == key) {
            wf_ws_waited = get_ms_clock() - t0;
            return b;
        }
        wf_ws_hold = 0;
        if (get_ms_clock() - t0 >= WF_WS_WAIT_MS || wf_ws_st < 0 || (wf_ws_st && !wf_ws_ok[key])) {
            wf_ws_waited = get_ms_clock() - t0;
            return 0;
        }
        wf_park_sleep();
    }
}

/* worker: 1 = the film kernel's shot set holds (row, variant) with these build flags (built from the pack when needed);
 * a BOT uses exactly what its TOP got */
static int wf_ws_shot(int kind, int bot, int row, int v, int flags)
{
    int key = WF_WS_KEY(row, v);
    if (bot)
        return wf_half_ws && wf_ws_shot_key == key;
    int use = 0;
    if (wf_film_shot_ok() && row < WF_WS_MAXROW) {
        use = wf_ws_shot_key == key && wf_ws_shot_ser && wf_film_shot_key() == wf_ws_shot_ser
              && wf_film_shot_flags() == flags;
        if (!use) {
            const struct wf_wsbuf *b = wf_ws_take(key);
            if (b) {
                wf_ws_shot_key = -1;
                wf_film_prepare(b->T, b->ser, flags);
                wf_ws_shot_key = key;
                wf_ws_shot_ser = b->ser;
                WF_BARRIER();
                wf_ws_hold = 0;
                use = 1;
            }
        }
    }
    if (kind == WF_PART_TOP)
        wf_half_ws = use;
    return use;
}

/* preview (wf_pv.c, wf_task): the published table when it is the one of key (row, variant), else 0 */
const uint32_t *wf_ws_table(int row, int v)
{
    const struct wf_wsbuf *p = wf_ws_pub;
    return p && row >= 0 && row < WF_WS_MAXROW && p->key == WF_WS_KEY(row, v) ? p->T : 0;
}

/* ------------------------------------------------------------------ the event ring (worker -> wf_task) */
#define WF_EV_N 32
struct wf_ev {
    uint8_t kind, status, hst, gst, mst, fxp;
    int8_t aspect;
    char evs;
    int16_t ev8;
    uint8_t llut; int8_t lei;
    uint8_t rb;                                  /* RAW develop: 0 not asked / 8-bit, 1 developed, 2 stopped part way */
    uint32_t buf, raw;
    int ms, q, h_ms, g_ms, m_ms, wait;
    struct wf_look lk;
};
static struct wf_ev wf_ev[WF_EV_N];
static volatile unsigned wf_ev_head, wf_ev_tail, wf_ev_lost, wf_ev_lost_said;
static const char *const wf_stname[WF_ST_N] = { "none", "film", "notab", "log", "lut", "nolut", "off", "nofilm",
                                               "gate", "aspect", "nostage", "rbstop" };
const char *wf_status_name(int st) { return st >= 0 && st < WF_ST_N ? wf_stname[st] : "?"; }

/* ------------------------------------------------------------------ effects around the colour pass (worker) */
static int wf_fx_st[3];                          /* halation / grain / mist: 0 not wanted, 1 ran, 2 not ready, 3 aspect */
static uint32_t wf_fx_us[3];
static int wf_fx_waited, wf_fx_pend;
static uint32_t wf_fx_shots, wf_half_seed;
#define WF_FX_WAIT_MS 2000

/* our colour pass over the request's frame, then the effects when they are ready. fx 0: the plain chunk pass.
 * Otherwise mist map, then pass 1 = mist + halation gather + colour (or colour + grain without halation), halation build,
 * pass 2 = halation apply + grain. The halation drive is gathered from the camera's words before the colour pass (clipped
 * highlights are still 255 there). NN / TOP decide (keys read once, waiting parked up to 2 s while wf_task is still to
 * publish a wanted key, films only); a BOT repeats its TOP with the same seed. */
static void wf_nop_words(uint32_t *p, int n, int arg) { (void)p; (void)n; (void)arg; }   /* RAW develop: colour done */

static void wf_colour_fx(const struct wf_frame *f, int bot, const struct wf_look *lk, wf_words_fn *fn, int arg)
{
    uint32_t *buf = wf_frame_words(f);
    int k = f->part, W = f->fw, H = f->fh, nw = wf_frame_nwords(f), ga = f->aspect < 0 ? 0 : f->aspect;
    int hk, gk, mk, fx = 0, post = lk->post;
    uint32_t seed;
    wf_fx_keys(post, &hk, &gk);
    mk = wf_fx_mkey(post);
    if (bot) {
        fx = (wf_half_h ? WF_FX_HAL : 0) | (wf_half_g ? WF_FX_GRN : 0) | (wf_half_m ? WF_FX_MST : 0);
        seed = wf_half_seed;
    } else {
        int hw = hk && wf_aspect_has_halo(ga) && k != WF_PART_BOT, t0 = get_ms_clock();
        for (; post >= WF_POST_FILM;) {          /* films only: a Log + LUT shot holds tables wf_ll_poll may free for a
                                                    parked worker; keys read unparked only */
            int wh = hw && wf_fx_hk != hk && wf_fx_coming(WF_FX_HAL, hk);
            int wg = gk && wf_fx_gk != gk && wf_fx_coming(WF_FX_GRN, gk);
            int wm = mk && wf_fx_mk != mk && wf_fx_coming(WF_FX_MST, mk);
            if (!wh && !wg && !wm)
                break;
            if (get_ms_clock() - t0 >= WF_FX_WAIT_MS) {
                wf_fx_pend = 1;
                break;
            }
            wf_park_sleep();
        }
        wf_fx_waited = get_ms_clock() - t0;
        if (hw && wf_fx_hk == hk)
            fx |= WF_FX_HAL;
        if (gk && wf_fx_gk == gk)
            fx |= WF_FX_GRN;
        if (mk && wf_fx_mk == mk) {
            wf_fx_mist_geom(W, H);
            if (wf_fx_mist_begin(k))             /* 0: a frame size mist cannot do: plain */
                fx |= WF_FX_MST;
        }
        seed = ++wf_fx_shots * 0x9E3779B1u ^ (uint32_t)f->t_post;
        if (k == WF_PART_TOP) {
            wf_half_h = (fx & WF_FX_HAL) != 0;
            wf_half_g = (fx & WF_FX_GRN) != 0;
            wf_half_m = (fx & WF_FX_MST) != 0;
            wf_half_seed = seed;
        }
    }
    wf_fx_st[0] = (fx & WF_FX_HAL) ? 1 : hk ? (!wf_aspect_has_halo(ga) ? 3 : 2) : 0;
    wf_fx_st[1] = (fx & WF_FX_GRN) ? 1 : gk ? 2 : 0;
    wf_fx_st[2] = (fx & WF_FX_MST) ? 1 : mk ? 2 : 0;
    if (!fx) {
        if (fn != wf_nop_words)
            wf_chunks(buf, nw, fn, arg);
        return;
    }
    wf_fx_begin(k, fx, seed);
    uint32_t th = 0, tg = 0, tm = 0, t, t2;
    int y0 = f->y0, wpr = f->w / 2;
    if (fx & WF_FX_MST) {                        /* the mist's low-res light map: every 2nd row of this request */
        t = wf_us();
        for (int r = 0, o = 0; o < nw; r++, o += wpr)
            if (!((y0 + r) & 1))
                for (int c = 0; c < wpr; c += WF_CHUNK) {
                    int m = wpr - c < WF_CHUNK ? wpr - c : WF_CHUNK;
                    wf_copyw(wf_scr, buf + o + c, m);
                    wf_fx_mist_gather(wf_scr, m, 2 * c, y0 + r);
                }
        wf_fx_mist_build(k);
        tm += wf_dt(t, wf_us());
    }
    for (int o = 0, x0 = 0, y = y0, m = 0; o < nw; o += m) {
        m = wf_seg(nw - o, x0, W);
        wf_copyw(wf_scr, buf + o, m);
        if (fx & WF_FX_MST) {                    /* the diffusion filter first: halation then sees the misted light */
            t = wf_us();
            wf_fx_mist_apply(wf_scr, m, x0, y);
            tm += wf_dt(t, wf_us());
        }
        if (fx & WF_FX_HAL) {
            t = wf_us();
            wf_fx_gather(wf_scr, m, x0, y);
            th += wf_dt(t, wf_us());
            fn(wf_scr, m, arg);
        } else {
            fn(wf_scr, m, arg);
            if (fx & WF_FX_GRN) {
                t = wf_us();
                wf_fx_grain(wf_scr, m, x0, y);
                tg += wf_dt(t, wf_us());
            }
        }
        wf_copyw(buf + o, wf_scr, m);
        if ((x0 += 2 * m) >= W) { x0 = 0; y++; }
    }
    if (fx & WF_FX_HAL) {
        t = wf_us();
        wf_fx_build(k);
        th += wf_dt(t, wf_us());
        for (int o = 0, x0 = 0, y = y0, m = 0; o < nw; o += m) {
            m = wf_seg(nw - o, x0, W);
            wf_copyw(wf_scr, buf + o, m);
            t = wf_us();
            wf_fx_apply(wf_scr, m, x0, y);
            t2 = wf_us();
            th += wf_dt(t, t2);
            if (fx & WF_FX_GRN) {
                wf_fx_grain(wf_scr, m, x0, y);
                tg += wf_dt(t2, wf_us());
            }
            wf_copyw(buf + o, wf_scr, m);
            if ((x0 += 2 * m) >= W) { x0 = 0; y++; }
        }
    }
    wf_fx_us[0] = th;
    wf_fx_us[1] = tg;
    wf_fx_us[2] = tm;
}

/* ------------------------------------------------------------------ the worker */
static struct wf_look wf_half_look;              /* the TOP's look, for its BOT */

static int wf_rb_res;                            /* this request's RAW develop: 0 none / 8-bit, 1 developed, 2 stopped */

/* RAW14: the develop, then the look's stage on its result (Log + LUT: effects only, plain log: nothing). 1 = handled
 * (*st set), 0 = not developed: the 8-bit path runs on Canon's frame. Films come here after their table is in. */
static int wf_rawb_run(struct wf_frame *f, int bot, struct wf_look *lk, int *llut, int *lei, int *st)
{
    int r = wf_rawb_stage(f, lk);
    if (r == 0)
        return 0;
    if (r < 0) {
        wf_rb_res = 2;
        *st = WF_ST_RBSTOP;                      /* stopped part way: no look stage on a half developed frame */
        return 1;
    }
    wf_rb_res = 1;
    switch (lk->output) {
    case WF_OUT_FILM:
        wf_colour_fx(f, bot, lk, wf_film_words, 0);
        *st = WF_ST_FILM;
        break;
    case WF_OUT_LLUT:
        *llut = lk->flut;                        /* the develop's own cube (wf_rawb.c tables key) */
        *lei = lk->lei;
        wf_colour_fx(f, bot, lk, wf_nop_words, 0);
        *st = WF_ST_LLUT;
        break;
    default:
        *st = WF_ST_LOG;                         /* flat: no effects */
        break;
    }
    return 1;
}

/* one (format, output) -> stage. UYVY8: the 8-bit look; RAW14 (RAW develop on, EOS M NN): the develop first, Canon's
 * frame back to the 8-bit look when the develop cannot run (wf_rawb.c logs why). */
static int wf_dispatch(struct wf_frame *f, int bot, struct wf_look *lk, int *llut, int *lei)
{
    int raw = f->fmt == WF_FMT_RAW14, st;
    wf_rb_res = 0;
    if (raw)
        f->fmt = WF_FMT_UYVY8;                   /* dst holds Canon's UYVY8 frame until the develop writes it */
    else if (f->fmt != WF_FMT_UYVY8)
        return WF_ST_NOSTAGE;                    /* no stage for this input format: untouched */
    else if (f->rb_skip)
        wf_rawb_note(f, WF_RBW_CAM);             /* RAW develop on, the camera layer's gate said no */
    if (raw && lk->output != WF_OUT_FILM) {
        if (lk->output != WF_OUT_LOG && lk->output != WF_OUT_LLUT)
            wf_rawb_note(f, WF_RBW_LOOK);        /* Off: nothing to develop to */
        else if (wf_rawb_run(f, bot, lk, llut, lei, &st))
            return st;
    }
    switch (lk->output) {
    case WF_OUT_FILM:
        if (wf_ws_shot(f->part, bot, lk->row, lk->variant, lk->ftflags)) {
            if (raw && wf_rawb_run(f, bot, lk, llut, lei, &st))
                return st;
            wf_colour_fx(f, bot, lk, wf_film_words, 0);
            return WF_ST_FILM;
        }
        if (raw)
            wf_rawb_note(f, WF_RBW_FILMTAB);
        return WF_ST_NOTAB;                      /* no table within ~1 s, or none in the pack: untouched */
    case WF_OUT_LLUT: {
        int use = bot ? wf_half_ll == lk->curve : wf_ll_have == lk->curve;
        if (use && f->part == WF_PART_TOP)
            wf_half_ll = lk->curve;
        if (!use)
            return WF_ST_NOLUT;                  /* loading / missing / bad cube: untouched */
        *llut = wf_ll_have_lut;
        *lei = wf_ll_have_ei;
        lk->flut = (uint8_t)*llut;               /* the tables' cube is what the shot got */
        lk->lei = (int8_t)*lei;
        lk->incurve = (uint8_t)wf_ll_have_inc;
        lk->hl = (int8_t)WF_FT_HOF(wf_ll_have_hs);
        lk->sh = (int8_t)WF_FT_SHOF(wf_ll_have_hs);
        wf_colour_fx(f, bot, lk, wf_ll_words, (int)(uintptr_t)wf_ll_tabs);
        return WF_ST_LLUT;
    }
    case WF_OUT_LOG:
        wf_chunks(wf_frame_words(f), wf_frame_nwords(f), wf_log_words, lk->curve);
        return WF_ST_LOG;                        /* flat: no effects */
    default:
        return lk->why == 1 ? WF_ST_NOFILM : WF_ST_OFF;
    }
}

static void wf_run(void)
{
    struct wf_frame *f = &wf_req;
    int k = f->part, t0 = get_ms_clock(), llut = 0, lei = 0;
    int bot = k == WF_PART_BOT && wf_half_valid;
    struct wf_look lk;
    if (bot) {
        lk = wf_half_look;
    } else {
        wf_look_now(&lk);
        if (k == WF_PART_TOP) {
            wf_half_look = lk;
            wf_half_ll = 0;
            wf_half_h = wf_half_g = wf_half_m = 0;
            wf_half_ws = 0;
        }
    }
    wf_half_valid = k == WF_PART_TOP;
    wf_fx_st[0] = wf_fx_st[1] = wf_fx_st[2] = 0;
    wf_fx_us[0] = wf_fx_us[1] = wf_fx_us[2] = 0;
    wf_fx_waited = wf_fx_pend = wf_ws_waited = 0;
    int st = wf_dispatch(f, bot, &lk, &llut, &lei);
    if (k == WF_PART_TOP)
        wf_half_look = lk;                       /* with the cube the TOP actually got */
    wf_req_status = st;
    int t1 = get_ms_clock();
    unsigned h = wf_ev_head;
    if (h - wf_ev_tail >= WF_EV_N) { wf_ev_lost++; return; }
    struct wf_ev *e = &wf_ev[h % WF_EV_N];
    e->kind = (uint8_t)k;
    e->status = (uint8_t)st;
    e->aspect = f->aspect;
    e->buf = (uint32_t)(uintptr_t)f->data;
    e->raw = f->raw;
    e->ms = t1 - t0;
    e->q = t0 - f->t_post;
    e->hst = (uint8_t)wf_fx_st[0]; e->gst = (uint8_t)wf_fx_st[1]; e->mst = (uint8_t)wf_fx_st[2];
    e->h_ms = wf_fx_st[0] == 1 ? (int)((wf_fx_us[0] + 500) / 1000) : 0;
    e->g_ms = wf_fx_st[1] == 1 ? (int)((wf_fx_us[1] + 500) / 1000) : 0;
    e->m_ms = wf_fx_st[2] == 1 ? (int)((wf_fx_us[2] + 500) / 1000) : 0;
    e->evs = wf_fx_st[0] == 1 || wf_fx_st[2] == 1 ? (char)wf_fx_evsrc : 0;
    e->ev8 = (int16_t)(wf_fx_ev8 == WF_EV_BAD ? 0 : wf_fx_ev8);
    e->llut = (uint8_t)llut;
    e->lei = (int8_t)lei;
    e->fxp = (uint8_t)wf_fx_pend;
    e->rb = (uint8_t)wf_rb_res;
    e->wait = wf_ws_waited + wf_fx_waited;
    e->lk = lk;
    WF_BARRIER();
    wf_ev_head = h + 1;
}

static void wf_worker(void *arg)
{
    (void)arg;
    for (;;) {
        int e = take_semaphore(wf_go, 1000);
        if (e == 0) {
            wf_busy = 1;                         /* stored before the shared flags are read */
            WF_BARRIER();
            if (!wf_unloading)
                wf_run();
            else
                wf_req_status = WF_ST_NONE;
            WF_BARRIER();
            wf_busy = 0;
            give_semaphore(wf_done);
        } else if (e != DRY_TIMEOUT) {
            msleep(1000);                        /* never spin at prio 0x1C */
        }
    }
}

int wf_worker_start(void)
{
    if (wf_worker_ok)
        return 0;
    wf_lock = create_named_semaphore("wf_lock", 1);
    wf_go = create_named_semaphore("wf_go", 0);
    wf_done = create_named_semaphore("wf_done", 0);
    uint32_t tid = 1;
    if (DRY_SEM(wf_lock) && DRY_SEM(wf_go) && DRY_SEM(wf_done)) {
        tid = task_create("film_yuv", WF_WORKER_PRIO, 0x2000, (void *)wf_worker, 0);
        wf_worker_ok = DRY_OK(tid);
    }
    wf_log("worker %x prio %x sem %x %x %x", (unsigned)tid, WF_WORKER_PRIO, (unsigned)wf_lock, (unsigned)wf_go,
           (unsigned)wf_done);
    return wf_worker_ok ? 0 : -1;
}

/* hook context (camera layer): never runs the look here (seconds of work on Canon's stack at Canon's priority) */
int wf_shot_run(struct wf_frame *f, int *status)
{
    if (!wf_worker_ok || wf_unloading || take_semaphore(wf_lock, 0) != 0)
        return -1;
    wf_req_live = 1;
    wf_req = *f;
    wf_req_status = WF_ST_NONE;
    if (give_semaphore(wf_go) != 0) {
        wf_req_live = 0;
        give_semaphore(wf_lock);
        return -1;
    }
    while (take_semaphore(wf_done, 0) != 0)     /* never hand the buffer back while the worker may write it */
        msleep(10);
    if (status)
        *status = wf_req_status;
    wf_req_live = 0;
    give_semaphore(wf_lock);
    return 0;
}

/* ------------------------------------------------------------------ the shot log (wf_task) */
struct wf_sp { int ms, lk, h, g, m; uint8_t hst, gst, mst; };
static struct { int on, t; struct wf_sp p; struct wf_look lk; int st, ev; } wf_top;
static int wf_shot_n;
int wf_shot_pending(void) { return wf_top.on; }

static void wf_ap(char *o, int n, int *l, const char *s)
{
    for (; *s && *l < n - 1; s++)
        o[(*l)++] = *s;
    o[*l] = 0;
}
static void wf_apt(char *o, int n, int *l, int ms)          /* tenths of a second: "8.1" */
{
    unsigned t = ((unsigned)(ms < 0 ? 0 : ms) + 50) / 100;
    if (t > 16000) t = 16000;
    unsigned q = (t * 6554u) >> 16;              /* t / 10, exact for t <= 16000 */
    char b[16];
    snprintf(b, sizeof b, "%d.%d", (int)q, (int)(t - q * 10));
    wf_ap(o, n, l, b);
}

/* "shot #3 Portra 400 WS: 16.4 s = look 4.6 + mist 9.2 + halation+bloom normal 1.8 + grain 0.6 (TOP 8.1, BOT 8.3) EV12";
 * the halation stage is named by what ran in it: "halation", "bloom low", "halation+bloom strong" (Wrap: no bloom) */
static void wf_shot_flush(const struct wf_sp *a, const struct wf_sp *b, const struct wf_look *lk, int st, int ev)
{
    if (!WF_ST_TOUCHED(st)) {
        wf_exif_none();
        return;
    }
    static const char *const bn[WF_BLOOM_MAX + 1] = { "", "bloom low", "bloom normal", "bloom strong" };
    char hn[32];
    int hb = lk->bloom <= WF_BLOOM_MAX && !WF_HALO_GEN(lk->halo) ? lk->bloom : 0;
    snprintf(hn, sizeof hn, " + %s%s%s ", lk->halo || !hb ? "halation" : "", lk->halo && hb ? "+" : "", bn[hb]);
    const char *const en[3] = { " + mist ", hn, " + grain " };
    char line[WF_SHOT_LINE], title[48];
    int l = 0;
    char nb[16];
    snprintf(nb, sizeof nb, "shot #%d ", ++wf_shot_n);
    line[0] = 0;
    wf_ap(line, sizeof line, &l, nb);
    wf_ap(line, sizeof line, &l, wf_look_title(lk, title, sizeof title));
    if (lk->hl || lk->sh) {
        snprintf(nb, sizeof nb, " hl%s sh%s", wf_hs_text(lk->hl), wf_hs_text(lk->sh));
        wf_ap(line, sizeof line, &l, nb);
    }
    wf_ap(line, sizeof line, &l, ": ");
    wf_apt(line, sizeof line, &l, a->ms + (b ? b->ms : 0));
    wf_ap(line, sizeof line, &l, " s = look ");
    wf_apt(line, sizeof line, &l, a->lk + (b ? b->lk : 0));
    for (int e = 0; e < 3; e++) {
        int sa = e == 0 ? a->mst : e == 1 ? a->hst : a->gst, sb = !b ? 0 : e == 0 ? b->mst : e == 1 ? b->hst : b->gst;
        int ms = e == 0 ? a->m + (b ? b->m : 0) : e == 1 ? a->h + (b ? b->h : 0) : a->g + (b ? b->g : 0);
        if (!sa && !sb)
            continue;
        wf_ap(line, sizeof line, &l, en[e]);
        if (sa == 1 && (!b || sb == 1))
            wf_apt(line, sizeof line, &l, ms);
        else
            wf_ap(line, sizeof line, &l, "-");
    }
    if (b) {
        wf_ap(line, sizeof line, &l, " (TOP ");
        wf_apt(line, sizeof line, &l, a->ms);
        wf_ap(line, sizeof line, &l, ", BOT ");
        wf_apt(line, sizeof line, &l, b->ms);
        wf_ap(line, sizeof line, &l, ")");
    }
    if (ev != WF_EV_NONE) {
        snprintf(nb, sizeof nb, " EV%d", ev);
        wf_ap(line, sizeof line, &l, nb);
    }
    wf_log("%s", line);
    int hr = a->hst == 1 && (!b || b->hst == 1), gr = a->gst == 1 && (!b || b->gst == 1), mr = a->mst == 1 && (!b || b->mst == 1);
    wf_exif_shot(lk, gr ? lk->grain : 0, hr ? lk->halo : 0, hr && !WF_HALO_GEN(lk->halo) ? lk->bloom : 0, mr ? lk->mist : 0, ev);
}

static void wf_shot_note(const struct wf_ev *e)
{
    struct wf_sp p;
    p.ms = e->ms; p.h = e->h_ms; p.g = e->g_ms; p.m = e->m_ms;
    p.lk = p.ms - p.h - p.g - p.m;
    if (p.lk < 0)
        p.lk = 0;
    p.hst = e->hst; p.gst = e->gst; p.mst = e->mst;
    int ev = e->evs && e->evs != '?' ? (e->ev8 + 4) >> 3 : WF_EV_NONE;
    if (wf_top.on && e->kind != WF_PART_BOT) {   /* the TOP before this one never got its BOT */
        wf_top.on = 0;
        wf_shot_flush(&wf_top.p, 0, &wf_top.lk, wf_top.st, wf_top.ev);
    }
    if (e->kind == WF_PART_TOP) {
        wf_top.on = 1; wf_top.t = get_ms_clock(); wf_top.p = p; wf_top.lk = e->lk; wf_top.st = e->status; wf_top.ev = ev;
    } else if (e->kind == WF_PART_BOT && wf_top.on) {
        wf_top.on = 0;
        wf_shot_flush(&wf_top.p, &p, &wf_top.lk, wf_top.st, wf_top.ev);
    } else {
        wf_shot_flush(&p, 0, &e->lk, e->status, ev);
    }
}

static const char *const wf_kname[3] = { "NN", "TOP", "BOT" };
extern int wf_cam_rejects_jpeg(int *bot_rejected);   /* camera layer: new rejected calls that made a JPEG */

static void wf_report(void)
{
    while (wf_ev_tail != wf_ev_head) {
        const struct wf_ev *e = &wf_ev[wf_ev_tail % WF_EV_N];
        char fx[40], ex[40], code[40];
        int n = 0;
        fx[0] = ex[0] = 0;
        if (e->hst) n += snprintf(fx + n, sizeof fx - n, e->hst == 1 ? " h%d" : e->hst == 3 ? " har" : " h-", e->h_ms);
        if (e->gst && n < (int)sizeof fx) n += snprintf(fx + n, sizeof fx - n, e->gst == 1 ? " g%d" : " g-", e->g_ms);
        if (e->mst && n < (int)sizeof fx) n += snprintf(fx + n, sizeof fx - n, e->mst == 1 ? " m%d" : " m-", e->m_ms);
        if (e->evs == '?' && n < (int)sizeof fx) snprintf(fx + n, sizeof fx - n, " ev?");
        else if (e->evs && n < (int)sizeof fx) {
            char es[2] = { e->evs, 0 };          /* no %c: Canon's vsnprintf prints it wrong */
            snprintf(fx + n, sizeof fx - n, " ev%d%s", (e->ev8 + 4) >> 3, es);
        }
        int cl = 0;
        if (e->lk.output == WF_OUT_FILM && e->lk.row >= 0)
            cl = snprintf(code, sizeof code, " %s v%d s%d", wf_films[e->lk.row].code, e->lk.variant,
                     wf_strength_pct(e->lk.strength));
        else if (e->lk.output == WF_OUT_LLUT || e->lk.output == WF_OUT_LOG)
            cl = snprintf(code, sizeof code, " c%d%s%s", e->lk.curve, e->lk.output == WF_OUT_LLUT ? " " : "",
                     e->lk.output == WF_OUT_LLUT ? wf_flut_code[e->llut < WF_FLUT_N ? e->llut : 0] : "");
        else
            code[0] = 0;
        if ((e->lk.hl || e->lk.sh) && cl >= 0 && cl < (int)sizeof code)
            snprintf(code + cl, sizeof code - cl, " hl%s sh%s", wf_hs_text(e->lk.hl), wf_hs_text(e->lk.sh));
        n = 0;
        if (e->rb) n += snprintf(ex + n, sizeof ex - n, e->rb == 1 ? " raw" : " raw-stop");
        if (e->fxp && n < (int)sizeof ex) n += snprintf(ex + n, sizeof ex - n, " fx pending");
        if (e->wait > 0 && n < (int)sizeof ex) snprintf(ex + n, sizeof ex - n, " w%d", e->wait);
        wf_log("yuv %s %x/%x: %s%s %dms%s q%d a%d%s", wf_kname[e->kind < 3 ? e->kind : 0], (unsigned)e->buf,
               (unsigned)e->raw, wf_status_name(e->status), code, e->ms, fx, e->q, e->aspect, ex);
        wf_shot_note(e);
        WF_BARRIER();
        wf_ev_tail++;
    }
    if (wf_top.on && get_ms_clock() - wf_top.t > 4000) {   /* a TOP whose BOT never came */
        wf_top.on = 0;
        wf_shot_flush(&wf_top.p, 0, &wf_top.lk, wf_top.st, wf_top.ev);
    }
    if (wf_ev_lost != wf_ev_lost_said) {
        wf_ev_lost_said = wf_ev_lost;
        wf_log("yuv: %d results not logged (ring full)", wf_ev_lost);
    }
    int botrej = 0, nj = wf_cam_rejects_jpeg(&botrej);
    if (botrej && wf_top.on) {                   /* its BOT was rejected: the TOP goes out alone now (EXIF order) */
        wf_top.on = 0;
        wf_shot_flush(&wf_top.p, 0, &wf_top.lk, wf_top.st, wf_top.ev);
    }
    for (; nj > 0 && nj <= 8; nj--)
        wf_exif_none();                          /* a JPEG without a look: its file number counts */
}

void wf_shot_poll(void)
{
    wf_ws_poll();
    wf_report();
    wf_ll_poll();
}

void wf_shot_deinit(void)
{
    /* the worker and the hooks stay (a Canon task may be inside a hook); wf_unloading makes them return at once */
}
