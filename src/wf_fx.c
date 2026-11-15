/* wf_fx.c - WindyStrife film effects at the shot for WindyFilm (WindyStrife
 * port): halation + bloom (yh_halo.c, prototype proto/halation_bloom, camera version cam/src_fix)
 * and Yedlin-principled grain (yg_grain.c, prototype proto/grain_yedlin). Plain C, correctness and safety first.
 *
 * Where: only on our own output paths, after our colour pass (wf_film_words / wf_ll_words): a loaded film (output
 * Film) and the Log + LUT output (only while its cube is loaded). Never on plain log or Off, never on a "nolut" or
 * "notab" frame (left plain). That list lives in ONE place, the effect post code of the look (struct wf_look .post,
 * wf_look_now / wf_post_now in wf_shot.c; also the greyed state of the browser's Grain / Halation / Mist rows).
 * Order: colour -> halation / bloom -> grain (the halation drive is read from the camera's words before the
 * colour pass, where clipped highlights are still 255: wf_colour_fx in wf_shot.c).
 * Parameters: each film's grain row x amount scale (and rho_max) and halation profile x strength come from the
 * catalogue (films/catalog.py, one provenance place) as SETS: wf_catalog.h wf_cat_gsets / wf_cat_hsets, one per
 * distinct parameter set, row -> set in wf_cat_gset / wf_cat_hset; set 0 = the + LUT modes (negative halation,
 * Neg 400 grain, both x1). Two films with the same set share the state (no rebuild on a switch between them);
 * the Strength row scales the colour table only, never grain or halation. Chungking 320's
 * Neg 800T x1.25 and negative x1.25 are its sets {1, 320} / {0, 320}: the same arithmetic as before (amount *=
 * 320 / 256.0f, hal_q8 = 320), so its shots are unchanged.
 * Grain tier YG_FULL | YG_SEAM_CUT, amount 0.6 / 1.0 / 1.5 (menu Fine / Normal / Strong).
 * Halation only on the 3:2 frame (its buffers are made for 5184x3456); other aspects get grain only. The
 * aspect test is ONE function too, wf_aspect_has_halo in wf_shot.c (worker, poll, menu, browser row).
 *
 * Threads (same hand-over as Log + LUT, wf_ll_poll in wf_shot.c):
 *   wf_task, wf_fx_poll: allocation, yh_init (baked tables, ~3 M instructions),
 *     yg_init (soft float + template synthesis, ~100 M instructions: about a second), free. It clears
 *     the ready key (wf_fx_hk / wf_fx_gk), waits until the worker is idle (wf_busy) and no Diana BOT half
 *     is owed the state (wf_half_valid && wf_half_h / wf_half_g), rewrites or frees, and publishes the
 *     key LAST. A failed attempt (no memory) is retried every wf_fx_retry_ms while the worker is idle,
 *     and at once on the next change of what is wanted.
 *   worker (wf_run in wf_shot.c): reads the keys once per NN / Diana TOP request; a BOT uses
 *     its TOP's decision and seed. Per chunk: wf_fx_gather / wf_fx_apply / wf_fx_grain on the cached
 *     scratch. Nothing here allocates, logs or waits in the worker.
 * Memory (camera review): never ML's __mem_malloc. When shoot_malloc fails there, ML falls through to
 * srm_malloc, and while an SRM buffer is held the camera cannot take pictures (ML locks the shutter
 * against ERR70). Instead:
 *   shoot memory, shoot_malloc_suite_contig (ML exmem.c, no SRM fallback: NULL = no memory), one
 *     contiguous chunk made cacheable as ML's CACHEABLE(): the halation block (yh_ctx + its buffers,
 *     ~5.7 MB), the 880 KB yg_init scratch (freed before publish), and the grain block when Canon's heap
 *     is short. Canon wants this pool empty for some playback operations (ML mem.c: image editing ->
 *     ERR70), so these blocks are given back while Canon playback is shown and set up again after it.
 *   Canon's heap (AllocateMemory, ML's __priv_AllocateMemory, no fallback): the grain block (yg_ctx +
 *     192 KB templates, 200 KB) when at least 1.5 MB stays free there (ML's own preferred free space for
 *     that heap); kept through playback.
 * Blocks are held only while the current output mode uses the effect. No memory: the effect stays off,
 * one WINDYFLM.LOG line per failure spell (it ends with a success or when the effect is not wanted),
 * a menu warning, a timed retry, never a crash. */
#include <stdint.h>
#include <stddef.h>
#include "yh_halo.h"
#include "yg_grain.h"
#include "wf_mist.h"
#include "wf_cam.h"
#include "wf_fx.h"

/* ML core (EOSM_202.sym) */
extern void *shoot_malloc_suite_contig(size_t size);   /* struct memSuite *, NULL = no memory (no SRM) */
extern void  shoot_free_suite(void *suite);
extern void *GetFirstChunkFromSuite(void *suite);
extern void *GetMemoryAddressOfMemoryChunk(void *chunk);
extern int   GetSizeOfMemoryChunk(void *chunk);
extern void *__priv_AllocateMemory(size_t size);       /* Canon's AllocateMemory, NULL = no memory */
extern void  __priv_FreeMemory(void *p);
extern int   GetFreeMemForAllocateMemory(void);
extern int   get_ms_clock(void);
extern void  msleep(int ms);
extern void  wf_log(const char *fmt, ...);
extern int   is_play_mode(void);
extern int   wf_yuv_active(void);                /* wf_shot.c: worker up, main hook installed */
/* wf_core.h (below): wf_ll_ready, wf_post_now / wf_post_want, wf_aspect, wf_busy, wf_half_valid, wf_half_h / g / m */

#define FX_W  WF_HALO_W                          /* the 3:2 L frame (halation tables / buffers): wf_cam.h, EOS M 5184x3456, 5D2 5616x3744 */
#define FX_HH WF_HALO_H
#define FX_AL(n) (((uint32_t)(n) + 31u) & ~31u)
#define FX_GBYTES (FX_AL(sizeof(yg_ctx)) + YG_TS * YG_TS * 3)
#define FX_HEAP_KEEP (3 << 19)                   /* Canon heap: leave 1.5 MB free (ML mem.c preferred_free_space) */
#define FX_CACHEABLE(p) ((void *)((uint32_t)(p) & ~0x40000000u))   /* ML mem_defs.h CACHEABLE (DIGIC 5) */
#include "wf_core.h"                             /* post codes, the generated catalogue's effect sets (wf_catalog.c; at most
                                                    62 sets each, asserted by tools/mk_catalog_c.py) */

volatile int wf_grain = WF_GRAIN_CAN ? WF_GRAIN_DEF : 0;   /* wf_cam.h: the 5D2 build has grain (default Off), no halation */
volatile int wf_halo = WF_HALO_CAN ? WF_HALO_DEF : 0;
volatile int wf_bloom = WF_HALO_CAN ? WF_BLOOM_DEF : 0;   /* runs in the halation stage (WF_HALO_CAN) */
volatile int wf_gauge = WF_GAUGE_DEF;            /* grain and halation stage (WF_GAUGE_Q8) */
volatile int wf_mist = WF_MIST_CAN ? WF_MIST_DEF : 0;
volatile int wf_fx_hk, wf_fx_gk, wf_fx_mk;
volatile int wf_fx_retry_ms = 10000;             /* "no memory": next attempt after this (the host test shortens it) */

/* Each film's effect parameters, their reasons and GUESS marks are its catalogue row (films/catalog.py, docs/FILMS.md):
 * e.g. Chungking 320 = Neg 800T grain x1.25 + negative halation x1.25 (the designer's "pushed, coarse" call, not
 * fitted), Cine 250D = Neg 250 grain + negative halation, both x1 (box-speed rule, not fitted). */
#define FX_KEY_SET 63                            /* key bits 0..5: set + 1; grain bits 6..7 the amount level, bit 8 the Genesis
                                                    grain (Even), bits 9..10 the gauge; halation stage bits 6..7 the halation
                                                    choice (2 = spektrafilm: the set's spk profile and strength, 3 = the
                                                    Genesis light wrap), bits 8..9 the bloom level, bits 10..11 the gauge
                                                    (wf_fx.h WF_FXK_*) */
#define FX_KEY_GGEN 256
static const char *const fx_gname[WF_GAUGE_MAX + 1] = { "16mm", "35mm", "65mm" };
static const char *const fx_hname[YH_NSTOCK] = { "neg", "800T", "slide", "bw", "spk-s", "spk-w", "spk-c", "spk-n", "spk-bw", "spk-bww",
                                                 "wrap" };
static const float fx_amount[WF_GRAIN_MAX + 1] = { 0.0f, 0.6f, 1.0f, 1.5f, 0.6f, 1.0f, 1.5f };
static const char *const fx_aname[WF_GRAIN_MAX + 1] = { "off", "fine", "normal", "strong", "even fine", "even normal", "even strong" };

/* one block: p = cacheable address; from a shoot memory suite (suite != 0) or Canon's heap (raw) */
typedef struct { void *p, *raw, *suite; } fx_blk;
static fx_blk fx_hb, fx_gb, fx_mb;   /* mist: mist_ctx, then its maps */
#define fx_mc ((mist_ctx *)fx_mb.p)
static int fx_m_tried, fx_m_nomem, fx_m_retry, fx_m_logged, fx_m_errlog, fx_m_ok, fx_mw, fx_mh;
                     /* halation block: yh_ctx, then its buffers; grain: yg_ctx, templates */
#define fx_yh ((yh_ctx *)fx_hb.p)
#define fx_yg ((yg_ctx *)fx_gb.p)
static yg_stock fx_gst;                          /* stock with the menu amount, read by yg_init */
static int fx_h_tried, fx_g_tried;               /* key last loaded or attempted, 0 = none */
static int fx_h_nomem, fx_g_nomem;               /* that attempt found no memory */
static int fx_h_retry, fx_g_retry;               /* get_ms_clock() from which a "no memory" key is tried again */
static int fx_h_logged, fx_g_logged;             /* key of the last "ready" line */
static int fx_h_errlog, fx_g_errlog;             /* "no memory" logged in this failure spell */
static int fx_h_bw;                              /* the halation set is a B&W one: it writes Y only (wf_fx_apply) */
static int fx_h_spk;                             /* the halation set is a Spektra one: physical strength, no daylight half-strength ramp (wf_fx_begin) */
static volatile int fx_inplay;                   /* Canon playback shown at the last poll (shoot memory given back) */

/* shoot memory: one contiguous chunk of at least nb bytes, or nothing (no SRM fallback) */
static int fx_shoot_alloc(fx_blk *b, uint32_t nb)
{
    b->p = b->raw = 0;
    b->suite = shoot_malloc_suite_contig(nb);
    if (!b->suite)
        return 0;
    void *c = GetFirstChunkFromSuite(b->suite);
    if (c && (uint32_t)GetSizeOfMemoryChunk(c) >= nb)
        b->p = FX_CACHEABLE(GetMemoryAddressOfMemoryChunk(c));
    if (!b->p) {
        shoot_free_suite(b->suite);              /* not one chunk: never happens with _contig */
        b->suite = 0;
    }
    return b->p != 0;
}

/* grain block: Canon's heap when it keeps FX_HEAP_KEEP free, else shoot memory */
static int fx_heap_alloc(fx_blk *b, uint32_t nb)
{
    b->p = b->raw = b->suite = 0;
    if (GetFreeMemForAllocateMemory() - (int)nb - 1024 > FX_HEAP_KEEP && (b->raw = __priv_AllocateMemory(nb)))
        b->p = FX_CACHEABLE(b->raw);
    return b->p ? 1 : fx_shoot_alloc(b, nb);
}

static void fx_free(fx_blk *b)
{
    if (b->suite)
        shoot_free_suite(b->suite);
    else if (b->raw)
        __priv_FreeMemory(b->raw);
    b->p = b->raw = b->suite = 0;
}

static uint32_t fx_hbytes(void)
{
    return FX_AL(sizeof(yh_ctx)) + yh_mem_bytes(FX_W, FX_HH);
}

/* post code of the current output (wf_post_mode with the loaded film; 0 = none): the menu's view of the shot */
int wf_fx_mode(void)
{
    return wf_post_now();
}

/* the grain / halation set of a post code; 0 = none (no post) */
static int fx_sets(int post, int *gs, int *hs)
{
    int r = post - WF_POST_FILM;
    if (r >= 0 && r < wf_nfilms && wf_cat_gset[r] >= 0 && wf_cat_hset[r] >= 0
        && wf_cat_gset[r] < wf_cat_ngset && wf_cat_hset[r] < wf_cat_nhset) {
        *gs = wf_cat_gset[r];
        *hs = wf_cat_hset[r];
        return 1;
    }
    if (post == WF_POST_LLUT) {
        *gs = WF_CAT_LUT_GSET;
        *hs = WF_CAT_LUT_HSET;
        return 1;
    }
    return 0;
}

/* post: as a shot runs it (wf_post_mode); only post codes get keys, the catalogue gives the sets.
 * keys: halation stage (set + 1) | halation << 6 | bloom << 8 | gauge << 10, when halation or bloom runs (Wrap has no
 * bloom); grain (set + 1) | amount level << 6 | 256 Genesis | gauge << 9 */
int wf_fx_keys(int post, int *hk, int *gk)
{
    int h = wf_halo, b = wf_bloom, g = wf_grain, ga = wf_gauge, gs, hs;   /* each read once */
    *hk = *gk = 0;
    if (!fx_sets(post, &gs, &hs))
        return 0;
    if (h < 0 || h > WF_HALO_MAX)
        h = 0;
    if (b < 0 || b > WF_BLOOM_MAX || WF_HALO_GEN(h))
        b = 0;
    if (ga < 0 || ga > WF_GAUGE_MAX)
        ga = WF_GAUGE_DEF;
    if (WF_HALO_CAN && (h || b))
        *hk = (hs + 1) | h << 6 | b << 8 | ga << 10;
    if (WF_GRAIN_CAN && g >= 1 && g <= WF_GRAIN_MAX)
        *gk = (gs + 1) | ((WF_GRAIN_GEN(g) ? g - WF_GRAIN_GEN0 + 1 : g) << 6) | (WF_GRAIN_GEN(g) ? FX_KEY_GGEN : 0) | ga << 9;
    return *hk | *gk;
}

/* wanted mist key: the level 1..WF_MIST_MAX when the output gets our post, else 0 (no sets are needed: one PSF for every
 * film; a B&W film ends with U / V 0 from its own colour pass, which runs after the mist) */
int wf_fx_mkey(int post)
{
    int gs, hs, m = wf_mist;
#if WF_MIST_CAN
    if (m >= 1 && m <= WF_MIST_MAX && fx_sets(post, &gs, &hs))
        return m;
#else
    (void)gs; (void)hs; (void)post; (void)m;
#endif
    return 0;
}

/* the browser's Mist row: whether mist can run for this post code at all, i.e. the part of wf_fx_mkey that does
 * not depend on the setting (greyed row: the same test the shot path applies, only the level is left out) */
int wf_fx_has_mist(int post)
{
    int gs, hs;
    return WF_MIST_CAN && fx_sets(post, &gs, &hs);
}

/* " x1.25" for a Q8 scale other than 256 (two decimals, rounded), "" for x1 */
static const char *fx_xname(int q8, char *x)
{
    /* x: 8 bytes */
    if (q8 == 256)
        return "";
    int c = (q8 * 100 + 128) >> 8;
    if (c > 999)
        c = 999;
    int h = 0, t = 0;                             /* digits without a division (no __aeabi_uidivmod at -Os, D10) */
    while (c >= 100) { c -= 100; h++; }
    while (c >= 10) { c -= 10; t++; }
    x[0] = ' '; x[1] = 'x'; x[2] = '0' + h; x[3] = '.'; x[4] = '0' + t; x[5] = '0' + c; x[6] = 0;
    return x;
}

/* menu (GUI task only): grain stock of the current output, its scale appended (" x1.25") */
const char *wf_fx_gstock_name(void)
{
    static char nm[32], xb[8];
    int gs, hs;
    if (!fx_sets(wf_fx_mode(), &gs, &hs))
        return "-";
    if (wf_grain >= WF_GRAIN_GEN0)
        return "Even";                           /* WindyGrain Even ...: the film-stage grain (DESIGN 1a), not a stock of ours */
    const char *s = yg_stocks[wf_cat_gsets[gs].row].name, *x = fx_xname(wf_cat_gsets[gs].scale_q8, xb);
    int i = 0;
    while (s[i] && i < 20) { nm[i] = s[i]; i++; }
    while (*x) nm[i++] = *x++;
    nm[i] = 0;
    return nm;
}

/* ------------------------------------------------------------ wf_task */
/* 0 = the worker no longer uses effect `bit`: idle (or parked: waiting for wf_task in wf_shot.c; it reads the
 * effect keys of its request only while unparked, and the key was cleared before this wait, so it sees 0 or the
 * state published after the rewrite), and no Diana BOT half owed it (5 s at most, then that BOT goes without it:
 * a plain store of 0 can only make a BOT skip, never use, an effect); -1 = worker busy for 10 s (keep the state,
 * retry on a later poll). Same logic as wf_ll_poll. A parked worker may wait for a film table: wf_ws_poll runs in
 * the loop (wf_task is the pack's only reader). */
static int fx_wait(int bit)
{
    for (int t = 0, tb = 0;;) {
        int busy = wf_busy && !wf_parked;
        int owed = wf_half_valid && (bit == WF_FX_HAL ? wf_half_h : bit == WF_FX_MST ? wf_half_m : wf_half_g);   /* between two wf_busy reads */
        busy |= wf_busy && !wf_parked;
        if (busy) {
            if (++t > 1000)
                return -1;
        } else if (owed) {
            if (++tb > 500) {
                if (bit == WF_FX_HAL)
                    wf_half_h = 0;
                else if (bit == WF_FX_MST)
                    wf_half_m = 0;
                else
                    wf_half_g = 0;
                __asm__ __volatile__("" ::: "memory");
                wf_log("fx: no Diana BOT half in 5 s, that shot keeps %s on top only", bit == WF_FX_HAL ? "halation" : bit == WF_FX_MST ? "mist" : "grain");
                continue;
            }
        } else {
            return 0;
        }
        wf_ws_poll();
        msleep(10);
    }
}

/* worker (wf_shot.c, the bounded effect wait of a shot): 1 = wf_task will still publish key `want` of effect
 * `bit`: wanted, its last attempt did not fail for memory, and Canon playback is not holding it back */
int wf_fx_coming(int bit, int want)
{
    if (!want || fx_inplay)
        return 0;
    if (bit == WF_FX_MST)
        return !(fx_m_tried == want && fx_m_nomem);
    if (bit == WF_FX_HAL)
        return !(fx_h_tried == want && fx_h_nomem);
    return !(fx_g_tried == want && fx_g_nomem);
}

/* the same key again after "no memory": once its retry time has come and the worker is idle (nothing
 * is held or in use then, so this never waits) */
static int fx_retry_due(int want, int nomem, int at)
{
    return want && nomem && !wf_busy && (int)(get_ms_clock() - at) >= 0;
}

/* Strength v2 (11-12, after the owner's camera shots: halation was hardly visible, slide not at all). QA on the
 * owner's night street frame (phase 1 QA): x3 negative gives a visible red-orange
 * fringe on shop lights and head lights, x1.5 800T the strong red glow, x4 slide (anti-halation underlayer: still
 * faint, as it should be), x2.5 B&W, bloom x2 (bloom_frac 0.06 -> 0.12 effective). GUESSES like the profiles.
 * yh_init limits hal_q8 / bloom_q8 to 1024 (x4); clamped here too so the log names what is used. */
/* Bloom levels (its own row since 11-15, as RAWB11 on the darkroom branch): Normal = the old "+ Glow" (bloom_q8 512,
 * x2 since Strength v2), Low half of it, Strong 896 (x3.5 of bloom_frac). GUESSES like the rest. */
static const uint16_t fx_bloom_q8[WF_BLOOM_MAX + 1] = { 0, 256, 512, 896 };
static const char *const fx_bname[WF_BLOOM_MAX + 1] = { "off", "low", "normal", "strong" };
typedef char fx_gain_rows[YH_NSTOCK == 11 ? 1 : -1];                 /* a new profile needs its gain below (0 = no halation) */
static int fx_hal_gain(int prof, int hq)
{
    /* YH_NEG, YH_C800T, YH_SLIDE, YH_BW; YH_SPK_* x1: spektrafilm's own strengths by anti-halation class (Q8);
     * YH_GEN x1: Genesis's halation_strength 1.0, the plugin's own default */
    static const uint16_t g[YH_NSTOCK] = { 768, 384, 1024, 640, 256, 256, 256, 256, 256, 256, 256 };
    int v = prof >= 0 && prof < YH_NSTOCK ? (hq * g[prof]) >> 8 : hq;
    return v > 1024 ? 1024 : v;
}

static void fx_h_sync(int want)
{
    if (want == fx_h_tried && !fx_retry_due(want, fx_h_nomem, fx_h_retry))
        return;
    int had = wf_fx_hk;
    wf_fx_hk = 0;                                /* new shots stop using the state */
    __asm__ __volatile__("" ::: "memory");
    if (fx_wait(WF_FX_HAL)) {
        wf_fx_hk = had;                          /* untouched: in use again */
        return;
    }
    fx_h_tried = want;
    fx_h_nomem = 0;
    if (!want) {
        fx_free(&fx_hb);                         /* no current mode uses it (or playback): ~5.7 MB back */
        fx_h_errlog = 0;                         /* a failure spell ends here: the next one logs again */
        return;
    }
    uint32_t nb = fx_hbytes();
    int t0 = get_ms_clock();
    if (!fx_hb.p && !fx_shoot_alloc(&fx_hb, nb)) {
        fx_h_nomem = 1;
        fx_h_retry = get_ms_clock() + wf_fx_retry_ms;
        if (!fx_h_errlog)
            wf_log("fx halation: no memory for %d KB, halation off", (int)(nb >> 10));
        fx_h_errlog = 1;
        fx_h_logged = 0;                         /* a later success logs its ready line */
        return;
    }
    yh_params p;
    int s = (want & FX_KEY_SET) - 1, hm = WF_FXK_HALO(want), bl = WF_FXK_BLOOM(want), ga = WF_FXK_HGAUGE(want);
    int gen = WF_HALO_GEN(hm), spk = WF_HALO_SPK(hm);
    int prof = gen ? YH_GEN : spk ? wf_cat_hsets[s].sprof : wf_cat_hsets[s].prof;
    int hq = spk ? wf_cat_hsets[s].shal_q8 : wf_cat_hsets[s].hal_q8;
    char xb[8];
    if (bl > WF_BLOOM_MAX || gen)
        bl = 0;
    if (ga > WF_GAUGE_MAX)
        ga = WF_GAUGE_DEF;
    yh_default_params(&p, prof);                 /* hal_q8 256 = the profile's strength; knee / boosts: the profile's,
                                                    also for bloom alone (halation Off: the film's own profile) */
    p.bloom_q8 = fx_bloom_q8[bl];
    p.hal_q8 = hm ? fx_hal_gain(prof, hq) : 0;   /* the set's strength (Chungking 320: 320 = x1.25) x the profile's gain */
    p.gauge_q8 = WF_GAUGE_Q8(ga);                /* halo and bloom sized for the gauge (yh_halo.h) */
    p.gen_gauge_q8 = WF_GAUGE_Q8(ga);            /* the light wrap's own halation_film_gauge */
    fx_h_bw = YH_STOCK_BW(prof);                 /* read by the worker only under a published key */
    fx_h_spk = spk;                              /* likewise */
    yh_init(fx_yh, &p, FX_W, FX_HH, (uint8_t *)fx_yh + FX_AL(sizeof(yh_ctx)));
    if (want != fx_h_logged)
        wf_log("fx halation %s%s%s%s, bloom %s, %s ready %dms %dKB", hm ? "" : "off (",
               prof >= 0 && prof < YH_NSTOCK ? fx_hname[prof] : "?", hm ? fx_xname(hq, xb) : "", hm ? "" : ")",
               fx_bname[bl], fx_gname[ga], get_ms_clock() - t0, (int)(nb >> 10));
    fx_h_logged = want;
    fx_h_errlog = 0;
    __asm__ __volatile__("" ::: "memory");
    wf_fx_hk = want;                             /* published last */
}

static void fx_g_sync(int want)
{
    if (want == fx_g_tried && !fx_retry_due(want, fx_g_nomem, fx_g_retry))
        return;
    int had = wf_fx_gk;
    wf_fx_gk = 0;
    __asm__ __volatile__("" ::: "memory");
    if (fx_wait(WF_FX_GRN)) {
        wf_fx_gk = had;
        return;
    }
    fx_g_tried = want;
    fx_g_nomem = 0;
    if (!want) {
        fx_free(&fx_gb);
        fx_g_errlog = 0;
        return;
    }
    int t0 = get_ms_clock();
    fx_blk scr = { 0, 0, 0 };
    int s = (want & FX_KEY_SET) - 1, a = (want >> 6) & 3, row = wf_cat_gsets[s].row, sc = wf_cat_gsets[s].scale_q8;
    int gen = (want & FX_KEY_GGEN) != 0;
    int ga = WF_FXK_GGAUGE(want) <= WF_GAUGE_MAX ? WF_FXK_GGAUGE(want) : WF_GAUGE_DEF, gq = WF_GAUGE_Q8(ga);
    char xb[8];
    if (!fx_gb.p)
        fx_heap_alloc(&fx_gb, FX_GBYTES);
    if (fx_gb.p && !gen)
        fx_shoot_alloc(&scr, yg_scratch_bytes());   /* yg_init's scratch: shoot memory, ~1 s (the Genesis model has none) */
    if (!scr.p && !gen) {
        fx_free(&fx_gb);                         /* nothing usable: no RAM held */
        fx_g_nomem = 1;
        fx_g_retry = get_ms_clock() + wf_fx_retry_ms;
        if (!fx_g_errlog)
            wf_log("fx grain: no memory for %d KB + %d KB, grain off", (int)(FX_GBYTES >> 10), (int)(yg_scratch_bytes() >> 10));
        fx_g_errlog = 1;
        fx_g_logged = 0;
        return;
    }
    if (!fx_gb.p) {
        fx_g_nomem = 1;
        fx_g_retry = get_ms_clock() + wf_fx_retry_ms;
        if (!fx_g_errlog)
            wf_log("fx grain: no memory for %d KB, grain off", (int)(FX_GBYTES >> 10));
        fx_g_errlog = 1;
        fx_g_logged = 0;
        return;
    }
    if (gen) {
        /* WindyGrain: Genesis's own model - its amplitude tables, its saturation, its gauge; no templates, no
         * density chain, no init cost (yg_gen_init only fills five numbers) */
        yg_gen_params gp;
        gp.amp_q8 = (int)(fx_amount[a] * sc / 256.0f * 256.0f + 0.5f);
        gp.sat_q8 = 256;                         /* film_grain_saturation 1.0: each channel its own table */
        gp.gauge_q8 = gq;                        /* grain_film_gauge: the Gauge row (16 mm 128, 35 mm 256, 65 mm 474) */
        gp.size_q8 = 256;                        /* film_grain_size 1.0 */
        gp.seed = 0x67656E31u;                   /* film_grain_random_seed ("gen1"); yg_set_shot mixes the file number in */
        yg_gen_init(fx_yg, &gp, 0);
        if (want != fx_g_logged)
            wf_log("fx grain %s %s %s ready %dms %dKB%s", yg_stocks[row].name, fx_aname[a + WF_GRAIN_GEN0 - 1], fx_gname[ga],
                   get_ms_clock() - t0,
                   (int)(sizeof(yg_ctx) >> 10), fx_gb.suite ? " shoot" : "");
        fx_g_logged = want;
        fx_g_errlog = 0;
        __asm__ __volatile__("" ::: "memory");
        wf_fx_gk = want;                         /* published last */
        return;
    }
    fx_gst = yg_stocks[row];
    fx_gst.amount *= fx_amount[a];
    if (sc != 256)
        fx_gst.amount *= sc / 256.0f;            /* the set's scale (Chungking 320: 320 / 256 = 1.25, exact in float) */
    if (wf_cat_gsets[s].rho_q8)
        fx_gst.rho_max = wf_cat_gsets[s].rho_q8 / 256.0f;   /* the film's own density range (catalog.py rho_for) */
    /* film pitch (um per pixel): a 36 mm frame at 35 mm, x gauge (16 mm: half the film per pixel, so the grain is twice
     * the size and twice the RMS in pixels; 65 mm: x1.85, finer and weaker) */
    yg_init(fx_yg, &fx_gst, YG_FULL | YG_SEAM_CUT, 0xC0FFEEu, 36000.0f * gq / 256.0f / WF_GRAIN_W,
            (uint8_t *)fx_yg + FX_AL(sizeof(yg_ctx)), scr.p);
    fx_free(&scr);
    if (want != fx_g_logged)
        wf_log("fx grain %s%s %s %s ready %dms %dKB%s", yg_stocks[row].name, fx_xname(sc, xb), fx_aname[a], fx_gname[ga],
               get_ms_clock() - t0,
               (int)(FX_GBYTES >> 10), fx_gb.suite ? " shoot" : "");
    fx_g_logged = want;
    fx_g_errlog = 0;
    __asm__ __volatile__("" ::: "memory");
    wf_fx_gk = want;                             /* published last */
}

static const char *const fx_mname[MIST_MAX + 1] = { "off", "white 1/8", "white 1/4", "white 1/2", "white 1",
                                                    "black 1/8", "black 1/4", "black 1/2", "black 1" };
static uint32_t fx_mbytes(void)
{
    return mist_ctx_bytes() + mist_mem_bytes();
}

static void fx_m_sync(int want)
{
    if (want == fx_m_tried && !fx_retry_due(want, fx_m_nomem, fx_m_retry))
        return;
    int had = wf_fx_mk;
    wf_fx_mk = 0;
    __asm__ __volatile__("" ::: "memory");
    if (fx_wait(WF_FX_MST)) {
        wf_fx_mk = had;
        return;
    }
    fx_m_tried = want;
    fx_m_nomem = 0;
    if (!want) {
        fx_free(&fx_mb);                         /* ~2.2 MB of shoot memory back */
        fx_m_errlog = 0;
        return;
    }
    uint32_t nb = fx_mbytes();
    int t0 = get_ms_clock();
    if (!fx_mb.p && !fx_shoot_alloc(&fx_mb, nb)) {
        fx_m_nomem = 1;
        fx_m_retry = get_ms_clock() + wf_fx_retry_ms;
        if (!fx_m_errlog)
            wf_log("fx mist: no memory for %d KB, mist off", (int)(nb >> 10));
        fx_m_errlog = 1;
        fx_m_logged = 0;
        return;
    }
    mist_init(fx_mc, want, WF_MIST_SCALE, (uint8_t *)fx_mc + mist_ctx_bytes());   /* focal length scale: fixed 50 mm equivalent */
    if (want != fx_m_logged)
        wf_log("fx mist %s ready %dms %dKB", fx_mname[want], get_ms_clock() - t0, (int)(nb >> 10));
    fx_m_logged = want;
    fx_m_errlog = 0;
    __asm__ __volatile__("" ::: "memory");
    wf_fx_mk = want;                             /* published last */
}

/* wf_task (wf_task): what the current output mode and the menu want, loaded / switched / freed.
 * Whether the effects are wanted at all is wf_mode_has_post (wf_shot.c, also the shelf rows' greyed state):
 * a + LUT mode wants them only while its cube is loaded (wf_ll_poll runs just before, in the same task): a
 * "nolut" frame stays plain, so nothing is held for it. While Canon playback is shown, nothing
 * stays in (or is taken from) the shoot memory: the halation block is freed, the grain block too unless
 * it lives in Canon's heap; both are set up again when playback is left (halation ~25 ms, grain ~1 s). */
void wf_fx_poll(void)
{
    /* what for (wf_look_use, owner RAWB9): shots as before; a roll in the camera nothing (all given back); a darkroom run
     * the look's post code without the shot LUT (its develop holds the cube) and for its 3:2 print whatever the aspect */
    int use = wf_look_use(), hk = 0, gk = 0;
    int m = use == WF_LU_SHOTS ? wf_post_want() : use == WF_LU_DARK && wf_yuv_active() ? wf_post_now() : 0;
    if (m)
        wf_fx_keys(m, &hk, &gk);
    int mk = m ? wf_fx_mkey(m) : 0;
    int play = (hk || gk || mk) && is_play_mode();
    fx_inplay = play;
    if (hk && ((use != WF_LU_DARK && !wf_aspect_has_halo(wf_aspect)) || play))   /* 3:2 only (wf_shot.c; -1 not
                                                                                    reported = 3:2 as v1) */
        hk = 0;
    if (play && !(gk == fx_g_tried && fx_gb.p && !fx_gb.suite))
        gk = 0;                                  /* only a grain block in Canon's heap stays */
    if (play)
        mk = 0;                                  /* mist block: shoot memory, given back in playback like halation's */
    fx_h_sync(hk);
    fx_g_sync(gk);
    fx_m_sync(mk);
}

/* menu (wf_menu.c) */
int wf_fx_state(int which)
{
    int hk, gk, hal = which == WF_FX_HAL || which == WF_FX_BLM, m;
    if (which == WF_FX_MST) {
        int mk;
        if (!wf_mist)
            return WF_FXS_OFF;
        if (!wf_yuv_active())
            return WF_FXS_INACTIVE;
        mk = wf_fx_mkey(m = wf_fx_mode());
        if (!mk)
            return WF_FXS_UNUSED;
        if (!wf_ll_ready(m))
            return WF_FXS_NOLUT;
        if (wf_fx_mk == mk)
            return WF_FXS_READY;
        if (fx_inplay)
            return WF_FXS_PLAY;
        return fx_m_tried == mk && fx_m_nomem ? WF_FXS_NOMEM : WF_FXS_LOADING;
    }
    if (!(which == WF_FX_HAL ? wf_halo : which == WF_FX_BLM ? wf_bloom : wf_grain))
        return WF_FXS_OFF;
    if (!wf_yuv_active())
        return WF_FXS_INACTIVE;
    if (which == WF_FX_BLM && WF_HALO_GEN(wf_halo))
        return WF_FXS_UNUSED;                    /* the light wrap has no bloom */
    wf_fx_keys(m = wf_fx_mode(), &hk, &gk);
    if (!(hal ? hk : gk))
        return WF_FXS_UNUSED;
    if (!wf_ll_ready(m))
        return WF_FXS_NOLUT;
    if (hal && !wf_aspect_has_halo(wf_aspect))
        return WF_FXS_ASPECT;
    if (hal ? wf_fx_hk == hk : wf_fx_gk == gk)
        return WF_FXS_READY;
    if (fx_inplay)
        return WF_FXS_PLAY;
    if (hal)
        return fx_h_tried == hk && fx_h_nomem ? WF_FXS_NOMEM : WF_FXS_LOADING;
    return fx_g_tried == gk && fx_g_nomem ? WF_FXS_NOMEM : WF_FXS_LOADING;
}

/* ------------------------------------------------------------ worker (wf_run) */
/* How much brighter than the top code a clipped pixel is depends on the light of the scene, which the exposure tells
 * (11-12, the owner's tree canopy against the sky: every gap between the leaves glowed as a round dot). Code 255 sits
 * ~3 EV over the metered grey: in daylight (EV100 >= 13) a clipped pixel is sky or a white wall, barely over the clip,
 * and the small clipped blobs are gaps between leaves; at night or indoors (EV100 <= 7) clipped pixels are lamps,
 * many stops over the clip, and the small ones are point lights. So the clipped boost (halation boost_ev, mist
 * MIST_GMAX) goes from 0 at EV100 13 to 100 % at EV100 7, linear in EV, and the point boost (halation pboost_ev, small
 * isolated clipped blobs) from 0 at EV100 9 to 100 % at EV100 6. Exposure unknown: half the clipped boost, no point
 * boost. The halation strength itself (fx_hal_gain: x3 on the negative stocks, tuned on the owner's night shots) follows
 * the clipped boost from 100 % at night to 50 % in daylight (FX_HS_DAY): spektrafilm's still-film halation is a few
 * per cent of the light (R 0.015 strong anti-halation .. 0.08 weak), ours x3 is ~7x that, right round lamps, too much on
 * a sunlit scene. Bloom is unchanged. GUESSES (the end points, the 50 %), like the boosts themselves. A BOT keeps its
 * TOP's tables. */
#define FX_EVB0 (7 * 8)                          /* EV100 x 8: clipped boost 100 % at or below */
#define FX_EVB1 (13 * 8)                         /* 0 at or above */
#define FX_EVP0 (6 * 8)                          /* point boost 100 % at or below */
#define FX_EVP1 (9 * 8)                          /* 0 at or above */
#define FX_RAMP(e, lo, hi) ((e) <= (lo) ? 256 : (e) >= (hi) ? 0 : (((hi) - (e)) * ((256 << 8) / ((hi) - (lo)))) >> 8)
#define FX_HS_DAY 128                            /* halation strength at clipped boost 0 (daylight), Q8 of fx_hal_gain */
volatile int wf_fx_ev8 = WF_EV_BAD, wf_fx_evsrc;
static int fx_hb_q8 = 256, fx_hp_q8 = 256;

static void fx_hot(int ev8)
{
    int src = 'R', e = ev8;                      /* a Darkroom frame: the CR2's exposure, not the camera's settings now */
    if (ev8 == WF_EV_LIVE)
        e = wf_shot_ev8(&src);
    else if (ev8 == WF_EV_BAD)
        src = '?';
    wf_fx_ev8 = e;
    wf_fx_evsrc = src;
    if (e == WF_EV_BAD) {
        fx_hb_q8 = 128;
        fx_hp_q8 = 0;
    } else {
        fx_hb_q8 = FX_RAMP(e, FX_EVB0, FX_EVB1);
        fx_hp_q8 = FX_RAMP(e, FX_EVP0, FX_EVP1);
    }
}

void wf_fx_begin(int kind, int fx, uint32_t seed, int ev8)
{
    if (kind != 2 && (fx & (WF_FX_HAL | WF_FX_MST))) {
        fx_hot(ev8);
        if (fx & WF_FX_HAL)
            yh_set_hot(fx_yh, fx_hb_q8, fx_hp_q8, fx_h_spk ? 256 : FX_HS_DAY + (((256 - FX_HS_DAY) * fx_hb_q8 + 128) >> 8));   /* Spektra: its own physical strengths, the clipped / point boosts still follow the exposure */
        if (fx & WF_FX_MST)
            mist_set_hot(fx_mc, fx_hb_q8);
    }
    if ((fx & WF_FX_HAL) && kind != 2)
        yh_begin_frame(fx_yh);                   /* NN / Diana TOP: a new frame; BOT keeps the TOP's rows */
    if (fx & WF_FX_GRN)
        yg_set_shot(fx_yg, seed);
}

void wf_fx_gather(const uint32_t *w, int nw, int x0, int y)
{
    yh_gather_chunk(fx_yh, w, 2 * nw, x0, y + WF_HALO_Y0);
}

void wf_fx_build(int kind)
{
    if (kind == 0)
        yh_build(fx_yh, 0, FX_HH, YH_WHOLE);
    else if (kind == 1)
        yh_build(fx_yh, 0, WF_HALO_SEAM, YH_TOP);
    else
        yh_build(fx_yh, WF_HALO_SEAM, FX_HH, YH_BOTTOM);
}

/* A B&W set writes Y only (PLAN 5.2): the halo and the bloom gather Canon's colour frame and the bloom keeps its
 * colour, so on a B&W film they would tint it (bw_after_colour). Its colour pass left every U and V at 0 (a B&W
 * film's table has U/V bytes 0x00: tools/mk_wtab.py; only B&W films take a B&W set: catalog.py check), so keeping
 * them is clearing them, on the cached scratch words (no read of the frame) */
void wf_fx_apply(uint32_t *w, int nw, int x0, int y)
{
    yh_apply_chunk(fx_yh, w, 2 * nw, x0, y + WF_HALO_Y0);
    if (fx_h_bw)
        for (int i = 0; i < nw; i++)
            w[i] &= 0xff00ff00u;
}

void wf_fx_grain(uint32_t *w, int nw, int x0, int y)
{
    yg_uyvy_row(fx_yg, w, 2 * nw, x0, y);
}

/* ---- mist (worker; wf_shot.c wf_colour_fx). kind 0 NN, 1 TOP, 2 BOT. The frame size is announced first; a BOT uses its
 * TOP's map (wf_half_m says the TOP had mist). */
void wf_fx_mist_geom(int W, int H)
{
    fx_mw = W;
    fx_mh = H + WF_HALO_Y0;                      /* wf_cam.h: the 5D2 grid is the frame moved down WF_HALO_Y0 rows (seam alignment) */
}

int wf_fx_mist_begin(int kind)
{
    if (kind != 2)
        fx_m_ok = mist_begin_frame(fx_mc, fx_mw, fx_mh);   /* 0: this frame size cannot be done: plain */
    return fx_m_ok;
}

void wf_fx_mist_gather(const uint32_t *w, int nw, int x0, int y)
{
    mist_gather_chunk(fx_mc, w, 2 * nw, x0, y + WF_HALO_Y0);
}

void wf_fx_mist_build(int kind)
{
    mist_build(fx_mc, kind == 0 ? MIST_WHOLE : kind == 1 ? MIST_TOP : MIST_BOTTOM);
}

void wf_fx_mist_apply(uint32_t *w, int nw, int x0, int y)
{
    mist_apply_chunk(fx_mc, w, 2 * nw, x0, y + WF_HALO_Y0);
}
