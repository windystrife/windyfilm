/* wf_rawb.c - RAW develop "B" in the shot pipeline (WindyStrife): the worker stage, the tables, the memory, the
 * progress box and the per-shot log line. See wf_rawb.h for the flow; the develop itself is wf_dev.c, the buffer check
 * wf_rsrc.c, the raw's address and the gates the camera layer (wf_cam_eosm.c; wf_cam_5d2.c: TOP + BOT halves). */
#include "wf_core.h"
#include "wf_loglut.h"
#include "wf_rawb.h"

volatile int wf_rawb;                            /* menu, off at every boot, never saved (wf_menu.c) */
volatile int wf_rb_hue = 1;                      /* WF_DEV_HUE_I (asserted below); WINDYFLM.CFG shoulder= (wf_menu.c) */
int wf_rawb_can(void) { return WF_RAWB_CAN; }

#if WF_RAWB_CAN
#include "wf_dev.h"
_Static_assert(WF_DEV_HUE_I == 1, "wf_rb_hue default");
#include "wf_rsrc.h"

/* ML core (EOSM_202.sym) */
extern void *shoot_malloc_suite_contig(size_t size);
extern void  shoot_free_suite(void *suite);
extern void *GetFirstChunkFromSuite(void *suite);
extern void *GetMemoryAddressOfMemoryChunk(void *chunk);
extern int   GetSizeOfMemoryChunk(void *chunk);
extern int   ml_shutdown_requested;
extern int   get_halfshutter_pressed(void);
extern void  NotifyBox(int timeout, const char *fmt, ...);
/* WFSTLOG.DAT's reads (the knob tables; the EOS M core exports, as wf_dev.c declares them) */
extern void *FIO_OpenFile(const char *name, unsigned mode);   /* 0 on error */
extern int   FIO_ReadFile(void *f, void *buf, size_t n);      /* buf must be uncacheable */
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);

#ifdef WF_BUILD_5D2
#define RB_W        5616                         /* the 5D2 L frame, developed as Canon's TOP + BOT halves */
#define RB_H        3744
#define RB_CAM      WF_DEV_5D2
#define RB_DX       8                            /* the develop covers columns 8 .. 5607 (wf_dev: w a multiple of 32); */
#define RB_DW       5600                         /* the 8 px at each edge keep Canon's pixels under the 8-bit look */
#else
#define RB_W        5184                         /* the frame B develops: the EOS M L 3:2 frame (the camera layer's gate) */
#define RB_H        3456
#define RB_CAM      WF_DEV_EOSM
#define RB_DX       0
#define RB_DW       RB_W
#endif
#define RB_BAND     64                           /* rows per band between the sleeps and the stop checks */
#define RB_NAP_MS   10
#define RB_MAX_MS   90000                        /* a develop past this stops (the rest stays Canon's) */
#define RB_HALF_MS  1500                         /* half-shutter held this long stops the develop */
#define RB_RETRY_MS 10000                        /* "no memory": the next attempt */
#define RB_BOX_MS   1000                         /* progress box refresh */
#define RB_AL(n)    (((uint32_t)(n) + 31u) & ~31u)
#define RB_CACHEABLE(p) ((void *)((uint32_t)(p) & ~0x40000000u))   /* ML mem_defs.h CACHEABLE (DIGIC 5) */

/* tables + scratch: one shoot-memory block (wf_fx.c rules: no SRM fallback, given back in playback) */
static void *rb_suite;
static uint8_t *rb_mem;
static volatile int rb_have;                     /* key of the published tables, 0 = none (published last) */
static volatile int rb_live;                     /* the worker is inside wf_rawb_stage (set before it reads rb_have) */
static int rb_tried, rb_rc = -1, rb_nomem, rb_retry, rb_play, rb_build_ms;
static volatile int rb_gen;                      /* +1 per table publish: the scratch of a TOP belongs to one build */
/* 5D2: the TOP half's develop that its BOT continues (prepare's scratch, the same raw, unchanged) */
static int rb_top_key, rb_top_gen;
/* the knob key beside each key (a chain film's composed log table, wf_rawb_logpk_build; 0 = none): rb_key's 32 bits have
   no room for it, so it is published with rb_have (written first) and compared with it everywhere */
static volatile uint32_t rb_have_stk;
static uint32_t rb_top_stk, rb_tried_stk;
static uint32_t rb_top_raw, rb_top_h;
static struct wf_ll_info rb_inf;
#define rb_tabs ((struct wf_dev_tabs *)rb_mem)
#define rb_scr  ((void *)(rb_mem + RB_AL(sizeof(struct wf_dev_tabs))))

/* progress (worker writes, wf_task reads for the box) */
static volatile int rb_prog = -1;                /* -1 idle, 0..99 rows done %, 100 = preparing */
static int rb_box_t, rb_box_last = -2;

/* the shot's record (worker -> wf_task ring, like wf_shot.c's events) */
enum { RB_AB_NONE = 0, RB_AB_UNLOAD, RB_AB_POWER, RB_AB_HALF, RB_AB_TIME, RB_AB_N };
static const char *const rb_abname[RB_AB_N] = { "", "unload", "power", "half-shutter", "time" };
struct rb_ev {
    uint8_t why, skip, out, cand_n;               /* WF_RBW_*, WF_RBS_* (camera gate), look output, candidates */
    int8_t  pick, rs_why;                         /* candidate, WF_RS_* */
    uint8_t ab, wb_src, roll, tier, cont;          /* cont: a 5D2 BOT half that continued its TOP's develop */
    uint8_t hue;                                  /* WF_DEV_HUE_* the shoulder took */
    int16_t score[WF_RB_NCAND];
    int     arg, n_rs;
    uint32_t addr;
    int     black, white, lv_black, lv_white;     /* used; measured on the raw (0 = none) */
    int     t_pick, t_lev, t_prep, t_rows, rows;
    int     fit_n, clip, hl_fill;
    int     ro_c, ro_ytop, ro_ms;                 /* ro_ms: the shoulder tables (the I / D shift table) */
    uint32_t h0, h1;                              /* raw sample hash before / after the develop */
};
#define RB_EV_N 8
static struct rb_ev rb_ev[RB_EV_N];
static volatile unsigned rb_ev_head, rb_ev_tail, rb_ev_lost;
static const char *const rb_whyname[WF_RBW_N] = { "ok", "notab", "buf", "prep", "look", "filmtab", "cam",
                                                   "TOP half not developed", "raw changed since the TOP half" };
static const char *const rb_skipname[WF_RBS_N] = { "", "quality", "aspect", "drive", "noaddr" };

/* ------------------------------------------------------------------ chain films' log pack (option (c), wf_rawb.h) */
#define RB_LG_PACK  "ML/DATA/WFLOG.DAT"
#define RB_LG_ID0   89                           /* the chain rows' first id: 89 + negative (films/catalog.py) */
#define RB_LG_NNEG  64
#define RB_K_LG     5                            /* rb_key type of a log pack build (1..4: the others) */
static uint16_t rb_lg_no[RB_LG_NNEG];            /* bit (variant - WF_PRINT_STOCK0): this pair did not build from the
                                                    pack (not listed, bad table): the 8-bit chain until the next boot */
static int rb_lg_none;                           /* the pack is missing or not a pack: all chain films, until next boot */
static const char *const rb_lgname[WF_RLG_N] = { "log pack", "not a chain print", "knob rows", "strength",
                                                  "highlights / shadows", "no log pack", "not in the log pack" };

/* ------------------------------------------------------------------ the knob rows on RAW (option (c), milestone 2)
 * A chain film with film-stage knob rows away from neutral: the develop's log table composed on the camera from the
 * pair's two halves in WFSTAGE.DAT on the 33^3 log grid (wf_stagek.h), with ML/DATA/WFSTLOG.DAT's x section and log
 * placement (tools/mk_wfstlog.py), plus the shipped table's residual: T(k) = composed(k) + (.L33 - composed(default)),
 * node by node (no exposure offset among the knobs B composes so far). At default knobs B keeps the shipped table (the
 * pack path), and the residual makes the first knob step continue it: the composition's own error at default (mean
 * 0.15 codes, but a lag at the white that moved the shoulder's knee by up to 0.9 node) is taken out where the knobs
 * leave it in place (tools/mk_wfstage.c logcheck: the knee then follows the chain at every Interlayer and Printer set).
 * Memory: the knobs' printing densities (215 622 B) and the residual (int8 quarter codes, 107 811 B) in the develop
 * scratch, the default knobs' densities in the tables' block (dead before the build writes there), the halves from
 * the TMP heap one at a time. Generic interpolation and data only, as wf_stagek.c. */
#define RB_SL_PACK  "ML/DATA/WFSTLOG.DAT"
#define RB_SL_IENT  36
#define RB_SL_CH    (RB_SL_IENT * 227)           /* read size 8172: whole entries, whole nodes (6 bytes) */
#define RB_SL_SLAB  1364                         /* nodes per wf_dev_pack_samples call (WF_DV_PKCH / 6) */
#define RB_SL_PD    (WF_SG_LNODES * 3 * 2)       /* printing densities, u16 */
#define RB_SL_R     ((WF_SG_LNODES * 3 + 31) & ~31)   /* the residual, int8 */
_Static_assert(sizeof(struct wf_dev_tabs) >= RB_SL_PD, "the default knobs' densities live in the tables' block");
enum { RB_SL_E_OPEN = 60, RB_SL_E_FORMAT, RB_SL_E_NOENT, RB_SL_E_MATCH, RB_SL_E_XSUM, RB_SL_E_HALF, RB_SL_E_STAGE,
       RB_SL_E_SCR };
static int rb_sl_none;                           /* WFSTLOG.DAT missing / not ours: the knob rows keep the 8-bit chain */
static uint16_t rb_sl_no[RB_LG_NNEG];            /* bit (variant - WF_PRINT_STOCK0): this pair's knob table failed */

/* the knob sets B composes: Interlayer alone so far (milestone 2: no exposure offset, no printer points) */
static int rb_sl_knobs(uint32_t stk)
{
    struct wf_sg_knobs k;
    wf_sg_unkey(stk, &k);
    return !k.fexp && !k.prn[0] && !k.prn[1] && !k.prn[2];
}
static int rb_sl_ok(const struct wf_look *lk, int n)
{
    return rb_sl_knobs(lk->stk) && !rb_sl_none && !(rb_sl_no[n] >> (lk->variant - WF_PRINT_STOCK0) & 1)
        && wf_sg_has(lk->row, lk->variant);
}

static const char *rb_sl_errstr(int rc)
{
    switch (rc) {
    case RB_SL_E_OPEN:   return "no WFSTLOG.DAT";
    case RB_SL_E_FORMAT: return "WFSTLOG.DAT not a pack";
    case RB_SL_E_NOENT:  return "not in WFSTLOG.DAT";
    case RB_SL_E_MATCH:  return "packs of another build";
    case RB_SL_E_XSUM:   return "x section checksum";
    case RB_SL_E_HALF:   return "stage half unreadable";
    case RB_SL_E_STAGE:  return "no stage pair";
    case RB_SL_E_SCR:    return "scratch too small";
    }
    return wf_dev_pk_errstr(rc);
}

struct rb_sl {
    void *f;                                     /* WFSTLOG.DAT */
    char *dma;                                   /* RB_SL_CH + 4, uncacheable: FIO reads here */
    uint8_t *buf;                                /* its cached copy */
    struct wf_sg_pair pr;                        /* the log placement */
    uint32_t xoff, xfnv, lgsum, nfnv, pfnv;
    int curve, bw, node, clip;
    struct wf_sg_knobs k0, k;
    uint16_t *pd0, *pd, *rgb;                    /* rgb: the residual pass's slab (RB_SL_SLAB nodes) */
    int8_t *r;
    struct wf_sg_prn *P;
};

static uint32_t rb_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rb_fnv(uint32_t h, const uint8_t *p, uint32_t n)
{
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static int rb_sl_read(struct rb_sl *s, uint32_t n)
{
    if (FIO_ReadFile(s->f, s->dma, n) != (int)n)
        return WF_LL_E_TRUNC;
    wf_copyw((uint32_t *)s->buf, (const uint32_t *)s->dma, (int)((n + 3) >> 2));
    return WF_LL_OK;
}

/* WFSTLOG.DAT's header and the entry (id, variant, code) into s */
static int rb_sl_find(struct rb_sl *s, int id, int variant, const char *code)
{
    uint32_t fsz = 0;
    if (FIO_GetFileSize(RB_SL_PACK, &fsz) != 0 || !(s->f = FIO_OpenFile(RB_SL_PACK, 0)))
        return RB_SL_E_OPEN;
    if (fsz < 32 || rb_sl_read(s, 32))
        return RB_SL_E_FORMAT;
    const uint8_t *h = s->buf;
    uint32_t n = h[6] | h[7] << 8, io = rb_le32(h + 12), xo = rb_le32(h + 16), ixsum = rb_le32(h + 24);
    if (h[0] != 'W' || h[1] != 'F' || h[2] != 'S' || h[3] != 'L' || (h[4] | h[5] << 8) != 1 || !n
        || (h[8] | h[9] << 8) != WF_SG_LG || io != 32 || xo < io + n * RB_SL_IENT || (xo & 511)
        || rb_le32(h + 20) != fsz || fsz != xo + RB_SL_PD)
        return RB_SL_E_FORMAT;
    s->curve = h[10];
    s->xoff = xo;
    s->xfnv = rb_le32(h + 28);
    uint8_t want[8];
    int i = 0, found = 0;
    for (; i < 8 && code[i]; i++) want[i] = (uint8_t)code[i];
    if (code[i])
        return RB_SL_E_NOENT;                    /* longer than the index's 8 bytes: never listed */
    for (; i < 8; i++) want[i] = 0;
    uint32_t x = 2166136261u;
    for (uint32_t left = n * RB_SL_IENT; left; ) {   /* the index follows the header */
        uint32_t m = left < RB_SL_CH ? left : RB_SL_CH;
        if (rb_sl_read(s, m))
            return RB_SL_E_FORMAT;
        x = rb_fnv(x, s->buf, m);
        for (const uint8_t *e = s->buf; !found && e < s->buf + m; e += RB_SL_IENT) {
            if (e[0] != id || e[1] != variant)
                continue;
            int k = 0;
            while (k < 8 && e[4 + k] == want[k]) k++;
            if (k < 8)
                continue;
            found = 1;
            s->bw = (e[2] & 2) != 0;
            s->pr.fit = (int32_t)rb_le32(e + 12);
            s->pr.bk = (int32_t)rb_le32(e + 16);
            s->pr.inv = rb_le32(e + 20);
            s->lgsum = rb_le32(e + 24);
            s->nfnv = rb_le32(e + 28);
            s->pfnv = rb_le32(e + 32);
        }
        left -= m;
    }
    if (x != ixsum)
        return RB_SL_E_FORMAT;
    return found ? WF_LL_OK : RB_SL_E_NOENT;
}

/* the negative pass for both knob sets, the x section streamed (its checksum at the end) */
static int rb_sl_neg(struct rb_sl *s, const struct wf_sg_neg *N)
{
    if (FIO_SeekSkipFile(s->f, (int64_t)s->xoff, 0) != (int64_t)s->xoff)
        return WF_LL_E_READ;
    uint32_t x = 2166136261u;
    int node = 0;
    for (uint32_t left = RB_SL_PD; left; ) {
        uint32_t m = left < RB_SL_CH ? left : RB_SL_CH;
        if (rb_sl_read(s, m))
            return WF_LL_E_TRUNC;
        x = rb_fnv(x, s->buf, m);
        int nn = (int)(m / 6);
        wf_sg_neg_pass_x(N, &s->pr, &s->k0, (const uint16_t *)s->buf, nn, s->pd0 + node * 3);
        wf_sg_neg_pass_x(N, &s->pr, &s->k, (const uint16_t *)s->buf, nn, s->pd + node * 3);
        node += nn;
        left -= m;
    }
    return x == s->xfnv ? WF_LL_OK : RB_SL_E_XSUM;
}

/* wf_dev_pack_samples: the shipped table's nodes against the default knobs' composition -> the residual */
static int rb_sl_res(void *ctx, const uint8_t *le, int n)
{
    struct rb_sl *s = ctx;
    if (n > RB_SL_SLAB || s->node + n > WF_SG_LNODES)
        return WF_LL_E_EXTRA;
    wf_sg_prn_pass_rgb(s->P, &s->pr, s->pd0 + s->node * 3, n, s->bw, s->rgb);
    int8_t *r = s->r + s->node * 3;
    for (int i = 0; i < 3 * n; i++) {
        int d = (int)(le[2 * i] | le[2 * i + 1] << 8) - s->rgb[i];
        d = d >= 0 ? (d + 32) >> 6 : -((32 - d) >> 6);   /* quarter 8-bit codes (64 sum units), half away from 0 */
        if (d > 127 || d < -127) {
            d = d > 0 ? 127 : -127;
            s->clip++;
        }
        r[i] = (int8_t)d;
    }
    s->node += n;
    return WF_LL_OK;
}

/* wf_dev_build_gen: the knobs' composition plus the residual, one blue plane a call */
static int rb_sl_fill(void *ctx, uint16_t *rgb, int n)
{
    struct rb_sl *s = ctx;
    if (s->node + n > WF_SG_LNODES)
        return WF_LL_E_EXTRA;
    wf_sg_prn_pass_rgb(s->P, &s->pr, s->pd + s->node * 3, n, s->bw, rgb);
    const int8_t *r = s->r + s->node * 3;
    for (int i = 0; i < 3 * n; i++) {
        int v = rgb[i] + r[i] * 64;
        rgb[i] = (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
    }
    s->node += n;
    return WF_LL_OK;
}

/* the knob table of lk into t (scr: >= RB_SL_PD + RB_SL_R bytes): WF_LL_OK or a code; pi = the log pack's entry */
static int rb_sl_build(struct wf_dev_tabs *t, void *scr, size_t scr_n, const struct wf_look *lk,
                       struct wf_ll_info *li, struct wf_dev_pkinfo *pi, int *clip)
{
    const struct wf_film *f = &wf_films[lk->row];
    struct rb_sl s;
    memset(&s, 0, sizeof s);
    memset(pi, 0, sizeof *pi);
    memset(li, 0, sizeof *li);
    int nb, pb, rc = 0;
    uint32_t nfnv, pfnv;
    void *N = 0;
    if (!scr || scr_n < RB_SL_PD + RB_SL_R)
        return RB_SL_E_SCR;
    if (!wf_sg_halves(lk->row, lk->variant, &nb, &pb, &nfnv, &pfnv))
        return RB_SL_E_STAGE;
    s.pd = scr;
    s.r = (int8_t *)scr + RB_SL_PD;
    s.pd0 = (uint16_t *)t;
    wf_sg_unkey(lk->stk, &s.k);
    s.k0.iie = WF_SG_IIE_DEF;
    s.dma = __mem_malloc(RB_SL_CH + 4, WF_MEM_DMA | WF_MEM_TMP, "wf_rawb.c", __LINE__);
    s.buf = __mem_malloc(RB_SL_CH + 4, WF_MEM_TMP, "wf_rawb.c", __LINE__);
    rc = s.dma && s.buf ? rb_sl_find(&s, f->id, lk->variant, f->code) : WF_LL_E_NOMEM;
    if (!rc && (s.nfnv != nfnv || s.pfnv != pfnv))
        rc = RB_SL_E_MATCH;                      /* made for other halves than this WFSTAGE.DAT holds */
    if (!rc && !(N = __mem_malloc(sizeof(struct wf_sg_neg), WF_MEM_TMP, "wf_rawb.c", __LINE__)))
        rc = WF_LL_E_NOMEM;
    if (!rc && (rc = wf_sg_read_half(0, nb, N)))
        rc = rc == -2 ? WF_LL_E_NOMEM : RB_SL_E_HALF;
    if (!rc)
        rc = rb_sl_neg(&s, N);
    if (N) __mem_free(N);
    if (s.f) FIO_CloseFile(s.f);
    if (s.buf) __mem_free(s.buf);
    if (s.dma) __mem_free(s.dma);
    if (!rc && !(s.P = __mem_malloc(sizeof(struct wf_sg_prn), WF_MEM_TMP, "wf_rawb.c", __LINE__)))
        rc = WF_LL_E_NOMEM;
    if (!rc && (rc = wf_sg_read_half(1, pb, s.P)))
        rc = rc == -2 ? WF_LL_E_NOMEM : RB_SL_E_HALF;
    if (!rc && !(s.rgb = __mem_malloc(RB_SL_SLAB * 6, WF_MEM_TMP, "wf_rawb.c", __LINE__)))
        rc = WF_LL_E_NOMEM;
    if (!rc)
        rc = wf_dev_pack_samples(RB_LG_PACK, f->id, lk->variant, f->code, rb_sl_res, &s, pi);
    if (!rc && (s.node != WF_SG_LNODES || pi->sum != s.lgsum || pi->curve != s.curve))
        rc = RB_SL_E_MATCH;                      /* the log table it was made for, on its curve */
    if (s.rgb) __mem_free(s.rgb);
    s.node = 0;
    if (!rc)
        rc = wf_dev_build_gen(t, RB_CAM, pi->curve, 0, rb_sl_fill, &s, li);
    if (s.P) __mem_free(s.P);
    int k = 0;                                   /* the log line's title: the table code */
    for (; k < 8 && f->code[k]; k++) li->title[k] = f->code[k];
    li->title[k] = 0;
    *clip = s.clip;
    return rc;
}

int wf_rawb_logpk(const struct wf_look *lk)
{
    if (lk->output != WF_OUT_FILM || lk->row < 0 || lk->variant < WF_PRINT_STOCK0 || lk->variant >= WF_NPRINTS)
        return WF_RLG_NOTCHAIN;
    const struct wf_film *f = &wf_films[lk->row];
    int n = f->id - RB_LG_ID0;
    if (f->group != WF_GRP_STAGE || n < 0 || n >= RB_LG_NNEG)
        return WF_RLG_NOTCHAIN;
    if (lk->stk && !rb_sl_ok(lk, n))             /* knob rows B does not compose yet (or cannot): the 8-bit chain */
        return WF_RLG_KNOBS;
    if (lk->strength != 1)                       /* 30 / 125 %: wf_film_words' mix, 8-bit only */
        return WF_RLG_STRENGTH;
    if (lk->hl || lk->sh)
        return WF_RLG_HS;
    if (rb_lg_none)
        return WF_RLG_NOPACK;
    return rb_lg_no[n] >> (lk->variant - WF_PRINT_STOCK0) & 1 ? WF_RLG_NOENT : WF_RLG_OK;
}

const char *wf_rawb_logpk_why(int why) { return why >= 0 && why < WF_RLG_N ? rb_lgname[why] : "?"; }

int wf_rawb_logpk_build(struct wf_dev_tabs *t, void *scr, size_t scr_n, const struct wf_look *lk, int *ms,
                        struct wf_dev_pkinfo *pi)
{
    const struct wf_film *f = &wf_films[lk->row];
    struct wf_ll_info li;
    int t0 = get_ms_clock(), clip = 0, n = f->id - RB_LG_ID0;
    uint16_t bit = (uint16_t)(1u << (lk->variant - WF_PRINT_STOCK0));
    int rc = lk->stk ? rb_sl_build(t, scr, scr_n, lk, &li, pi, &clip)
                     : wf_dev_build_pack(t, RB_CAM, RB_LG_PACK, f->id, lk->variant, f->code, 0, &li, pi);
    if (!rc && wf_rb_hue == WF_DEV_HUE_I)        /* i's colour trim lives in the table (rb_key holds it) */
        wf_dev_trim(t, WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB);
    *ms = get_ms_clock() - t0;
    if (lk->stk) {                               /* the knob table: its failures leave the pack path alone */
        if (rc == RB_SL_E_OPEN || rc == RB_SL_E_FORMAT)
            rb_sl_none = 1;
        else if (rc == WF_LL_E_OPEN || rc == WF_DEV_PK_E_FORMAT || rc == WF_DEV_PK_E_INDEX)
            rb_lg_none = 1;
        else if (rc)
            rb_sl_no[n] |= bit;
        if (rc)
            wf_log("rawb: knob table %s v%d k%x: %s (%d), the 8-bit chain", f->code, lk->variant, (unsigned)lk->stk,
                   rb_sl_errstr(rc), rc);
        else if (clip)
            wf_log("rawb: knob table %s v%d: residual clipped at %d of %d values", f->code, lk->variant, clip,
                   WF_SG_LNODES * 3);
        return rc;
    }
    if (rc == WF_LL_E_OPEN || rc == WF_DEV_PK_E_FORMAT || rc == WF_DEV_PK_E_INDEX)
        rb_lg_none = 1;                          /* no usable pack: never tried again this boot */
    else if (rc)
        rb_lg_no[n] |= bit;
    if (rc)
        wf_log("rawb: log pack %s v%d: %s (%d), the 8-bit chain", f->code, lk->variant, wf_dev_pk_errstr(rc), rc);
    return rc;
}

/* the tables a look needs: 0 = B cannot do it (Off) */
static int rb_key(const struct wf_look *lk)
{
    switch (lk->output) {
    case WF_OUT_FILM:                                             /* a chain film's log table (the whole look) */
        if (wf_rawb_logpk(lk) == WF_RLG_OK)
            return RB_K_LG | lk->row << 4 | lk->variant << 16 | (wf_rb_hue == WF_DEV_HUE_I) << 24;   /* + i's trim */
        return 1;                                                 /* the sRGB base develop, the film tables after */
    case WF_OUT_LOG:  return 2 | lk->curve << 4;                  /* the log code itself */
    case WF_OUT_LLUT:
        if (WF_FLUT_DISP(lk->flut))                               /* Leica Vivid: raw -> sRGB -> the display cube */
            return 4 | lk->flut << 8 | ((uint8_t)lk->lei) << 16 | (lk->hl & 15) << 24 | (lk->sh & 15) << 28;
        return 3 | lk->curve << 4 | lk->flut << 8 | ((uint8_t)lk->lei) << 16;
    }
    return 0;
}
/* the knob key that goes with key (rb_have_stk): a chain film's composed log table, else 0 */
static uint32_t rb_stk(const struct wf_look *lk, int key) { return (key & 15) == RB_K_LG ? lk->stk : 0; }

static struct rb_ev *rb_ev_new(void)
{
    unsigned h = rb_ev_head;
    if (h - rb_ev_tail >= RB_EV_N) {
        rb_ev_lost++;
        return 0;
    }
    struct rb_ev *e = &rb_ev[h % RB_EV_N];
    memset(e, 0, sizeof *e);
    e->pick = -1;
    return e;
}
static void rb_ev_put(void)
{
    WF_BARRIER();
    rb_ev_head++;
}

void wf_rawb_note(const struct wf_frame *f, int why)
{
    struct rb_ev *e = rb_ev_new();
    if (!e)
        return;
    e->why = (uint8_t)why;
    e->skip = f->rb_skip;
    e->arg = f->rb_arg;
    rb_ev_put();
}

/* FNV-1a of 16 spread windows of the raw (64 bytes each): did the buffer change while we developed it */
static uint32_t rb_hash(const uint8_t *raw, int pitch, int h)
{
    uint32_t x = 2166136261u;
    for (int k = 0; k < 16; k++) {
        const volatile uint8_t *p = raw + (uint32_t)((h / 17) * (k + 1)) * pitch + (pitch / 17) * (k + 1);
        for (int i = 0; i < 64; i++)
            x = (x ^ p[i]) * 16777619u;
    }
    return x;
}

/* worker */
int wf_rawb_stage(struct wf_frame *f, const struct wf_look *lk)
{
    int key = rb_key(lk), rc = 0;
    uint32_t stk = rb_stk(lk, key);
    rb_live = 1;
    WF_BARRIER();
    struct rb_ev *e = rb_ev_new();
    struct rb_ev tmp;
    if (!e) {
        e = &tmp;                                /* ring full (wf_task far behind): develop anyway, no line */
        memset(e, 0, sizeof *e);
    }
    e->out = lk->output;
    e->cand_n = f->nrawc;
    for (int k = 0; k < WF_RB_NCAND; k++)
        e->score[k] = -9999;
    /* the rows this call develops: the whole frame (EOS M), or Canon's TOP / BOT half (5D2: f->y0, f->h) */
    int part = f->part, top = part == WF_PART_TOP, bot = part == WF_PART_BOT;
    int ya = top || bot ? f->y0 : 0, yz = top || bot ? f->y0 + f->h : f->fh;
    if (bot) {
        int tk = rb_top_key;
        rb_top_key = 0;                          /* a TOP is continued once */
        if (key && (tk != key || rb_top_stk != stk || rb_top_gen != rb_gen)) {   /* key 0: the look check says why */
            e->why = WF_RBW_TOP;
            goto out;
        }
    } else {
        rb_top_key = 0;
    }
    if (!key || f->fw != RB_W || f->fh != RB_H || f->pitch != 2 * RB_W ||
        !(part == WF_PART_FULL || (top && ya == 0 && yz == RB_H / 2) || (bot && ya == RB_H / 2 && yz == RB_H))) {
        e->why = WF_RBW_LOOK;
        goto out;
    }
    if (rb_have != key || rb_have_stk != stk || !rb_mem) {
        e->why = WF_RBW_NOTAB;                   /* tables still loading (or failed / no memory): the 8-bit look */
        goto out;
    }
    uint32_t *dst = (uint32_t *)f->dst;
    const uint32_t *dec = wf_ll_dec_of(lk->incurve);
    int t0 = get_ms_clock(), t1, t2, t3, words = f->pitch / 4;
    const uint8_t *raw;
    rb_prog = 100;
    if (bot) {                                   /* the TOP's prepare stands: same raw, unchanged, same tables */
        e->pick = 0;
        e->addr = rb_top_raw;
        raw = (const uint8_t *)(uintptr_t)rb_top_raw;
        e->h0 = rb_hash(raw, f->rpitch, f->ray1);
        if (f->nrawc < 1 || f->rawc[0] != rb_top_raw || e->h0 != rb_top_h) {
            e->why = WF_RBW_CHG;
            goto out;
        }
        e->cont = 1;
        t1 = t2 = t3 = t0;
        goto rows;
    }

    /* 1. which buffer */
    struct wf_rs_geom g = { f->rpitch, f->rx0, f->ry0, f->fw, yz - ya, f->black, f->cfa == 0 || f->cfa == 3 };
    struct wf_rs_res r;
    wf_rs_pick(f->rawc, f->nrawc, dst, &g, dec, &r);
    for (int k = 0; k < WF_RB_NCAND && k < WF_RS_NCAND; k++)
        e->score[k] = (int16_t)r.score[k];
    e->pick = (int8_t)r.pick;
    e->rs_why = (int8_t)r.why;
    e->n_rs = r.n;
    t1 = get_ms_clock();
    e->t_pick = t1 - t0;
    if (r.pick < 0) {
        e->why = WF_RBW_BUF;
        goto out;
    }
    raw = (const uint8_t *)(uintptr_t)f->rawc[r.pick];
    e->addr = f->rawc[r.pick];

    /* 2. levels */
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw;
    j.pitch = f->rpitch;
    j.ax0 = f->rax0; j.ay0 = f->ray0; j.ax1 = f->rax1; j.ay1 = f->ray1;
    int lb = 0, lw = 0;
    wf_dev_levels(rb_scr, &j, f->rbx0, f->rbx1, &lb, &lw);
    e->lv_black = lb;
    e->lv_white = lw;
    j.black = lb ? lb : f->black;
    j.white = lw ? lw : f->white;
    e->black = j.black;
    e->white = j.white;
    t2 = get_ms_clock();
    e->t_lev = t2 - t1;

    /* 3. prepare: WB fit on Canon's frame, highlight map, matrix, shoulder */
    j.x0 = f->rx0 + RB_DX; j.y0 = f->ry0;
    j.w = RB_DW; j.h = f->fh;
    j.cfa = f->cfa;
    j.canon = dst + RB_DX / 2;                   /* this call's rows (a 5D2 TOP: the top half only) */
    j.canon_pw = words;
    j.canon_y0 = 0;
    j.canon_h = yz - ya;
    j.dec = dec;
    j.roll = lk->output != WF_OUT_LOG;
    j.hue = (key & 15) == RB_K_LG ? wf_rb_hue : WF_DEV_HUE_A;   /* chain films only; cans + sRGB base develop: A */
    j.ms = get_ms_clock;
    struct wf_dev_stat st;
    memset(&st, 0, sizeof st);
    e->h0 = rb_hash(raw, f->rpitch, f->ray1);
    if (wf_dev_prepare(rb_scr, rb_tabs, &j, &st) != 0) {
        e->why = WF_RBW_PREP;
        goto out;
    }
    t3 = get_ms_clock();
    e->t_prep = t3 - t2;
    e->wb_src = (uint8_t)st.wb_src;
    e->tier = (uint8_t)st.fit_tier;
    e->fit_n = st.fit_n;
    e->clip = st.clip_quads;
    e->hl_fill = st.hl_fill;
    e->roll = (uint8_t)st.roll;
    e->ro_c = st.ro_c;
    e->ro_ytop = st.ro_ytop;
    e->hue = (uint8_t)st.hue;
    e->ro_ms = st.ro_ms;

    /* 4. rows, a band at a time; Canon's rows below the last band stay when it stops */
rows:;
    int y = ya, half_t = 0, ab = RB_AB_NONE;
    while (y < yz) {
        int yb = y + RB_BAND < yz ? y + RB_BAND : yz;
        wf_dev_rows(rb_scr, y, yb, dst + (y - ya) * words + RB_DX / 2, f->pitch);
        y = yb;
        rb_prog = (y - ya) * 100 / (yz - ya);
        if (y >= yz)
            break;
        int now = get_ms_clock();
        if (wf_unloading)
            ab = RB_AB_UNLOAD;
        else if (ml_shutdown_requested)
            ab = RB_AB_POWER;
        else if (now - t0 >= RB_MAX_MS)
            ab = RB_AB_TIME;
        else if (get_halfshutter_pressed()) {
            if (!half_t)
                half_t = now;
            else if (now - half_t >= RB_HALF_MS)
                ab = RB_AB_HALF;
        } else {
            half_t = 0;
        }
        if (ab)
            break;
        msleep(RB_NAP_MS);                       /* wf_task, Canon's GUI and the notify box run meanwhile */
    }
    e->rows = y - ya;
    e->ab = (uint8_t)ab;
    e->t_rows = get_ms_clock() - t3;
    e->h1 = rb_hash(raw, f->rpitch, f->ray1);
    e->why = WF_RBW_OK;
    rc = ab ? -1 : (key & 15) == RB_K_LG ? 2 : 1;
    if (top && !ab && e->h0 == e->h1) {          /* the BOT half may continue this develop */
        rb_top_raw = e->addr;
        rb_top_h = e->h1;
        rb_top_gen = rb_gen;
        rb_top_key = key;
        rb_top_stk = stk;
    }
out:
    if (e != &tmp)
        rb_ev_put();
    rb_prog = -1;
    WF_BARRIER();
    rb_live = 0;
    return rc;
}

/* ------------------------------------------------------------------ wf_task */
/* table cache (.RBT): the cube tables (CUBE, DISP) of one look next to its cube, ML/DATA/<name>.RBT for <name>.CUB.
 * Key: magic, the builder id (wf_dev_tabs_id: format version, table size, camera, curve -> gamut, constants), the look key
 * (rb_key: source, curve, Film LUT, LUT EI; DISP also H / S), auto exposure + camera, the table size, then the cube's
 * size, time stamp and content hash (wf_llb_cube_id, as the .FRT). B's tables have no JPEG input curve. One file per
 * cube: another LUT EI rebuilds and rewrites it. SRGB / LOG tables are computed (no parse): never cached. A file that
 * does not match (key, size, hash) is rebuilt and rewritten; a write while Canon is busy is owed and tried again
 * every RB_RBT_MS while the same tables stay published. */
#define RB_RBT_MAGIC 0x31544252u                 /* "RBT1" */
#define RB_RBT_NKEY  WF_RBT_NKEY                 /* wf_rawb.h: the darkroom keys the same file (wf_rawb_rbt_key) */
#define RB_RBT_MS    2000
static uint32_t rb_rbt_k[RB_RBT_NKEY];           /* the owed write: its key, file and tables key (0 = none) */
static char rb_rbt_path[WF_LL_NAME];
static int rb_rbt_owed, rb_rbt_t;
static const char *const rb_llbname[4] = { "hit", "none", "key", "bad" };

static int rb_rbt_name(char *d, const char *cube)          /* ML/DATA/<name>.CUB -> ML/DATA/<name>.RBT */
{
    int n = 0;
    while (cube[n] && n < WF_LL_NAME - 1) {
        d[n] = cube[n];
        n++;
    }
    d[n] = 0;
    if (cube[n] || n < 5 || d[n - 4] != '.')
        return 0;
    d[n - 3] = 'R';
    d[n - 2] = 'B';
    d[n - 1] = 'T';
    return 1;
}

/* the one key of a look's .RBT (wf_rawb.h): B's own build below and the darkroom's (wf_dark.c, same camera, same struct) */
int wf_rawb_rbt_key(const struct wf_look *lk, int src, const char *cube, char *path, uint32_t *key)
{
    if ((src != WF_DEV_CUBE && src != WF_DEV_DISP) || !cube || !rb_rbt_name(path, cube) || wf_llb_cube_id(cube, key + 5) != 0)
        return -1;
    int curve = src >= WF_DEV_SRGB ? 0 : lk->curve, autoexp = src == WF_DEV_CUBE && lk->flut != 0;
    key[0] = RB_RBT_MAGIC;
    key[1] = wf_dev_tabs_id(RB_CAM, src, curve);
    key[2] = (uint32_t)rb_key(lk);
    key[3] = (uint32_t)autoexp | (uint32_t)RB_CAM << 8;
    key[4] = (uint32_t)sizeof(struct wf_dev_tabs);
    return 0;
}

static void rb_rbt_retry(void)
{
    int now = get_ms_clock();
    if (now - rb_rbt_t < RB_RBT_MS)
        return;
    rb_rbt_t = now;
    int wr = wf_llb_write(rb_rbt_path, rb_rbt_k, RB_RBT_NKEY, rb_tabs, sizeof(struct wf_dev_tabs));
    if (wr == WF_FE_BUSY)
        return;
    rb_rbt_owed = 0;
    if (wr)
        wf_log("rawb: %s not written (%s)", rb_rbt_path + 8, wf_fe_name(wr));
    else
        wf_log("rawb: %s written %dms", rb_rbt_path + 8, get_ms_clock() - now);
}

static void rb_free(void)
{
    if (rb_suite)
        shoot_free_suite(rb_suite);
    rb_suite = 0;
    rb_mem = 0;
    rb_rbt_owed = 0;
}

static int rb_alloc(void)
{
    uint32_t nb = RB_AL(sizeof(struct wf_dev_tabs)) + RB_AL(wf_dev_scratch_bytes(RB_W, RB_H)) + 32;
    rb_suite = shoot_malloc_suite_contig(nb);
    if (!rb_suite)
        return 0;
    void *c = GetFirstChunkFromSuite(rb_suite);
    if (c && (uint32_t)GetSizeOfMemoryChunk(c) >= nb)
        rb_mem = (uint8_t *)RB_AL(RB_CACHEABLE(GetMemoryAddressOfMemoryChunk(c)));
    if (!rb_mem) {
        shoot_free_suite(rb_suite);
        rb_suite = 0;
    }
    return rb_mem != 0;
}

static void rb_report(void)
{
    while (rb_ev_tail != rb_ev_head) {
        const struct rb_ev *e = &rb_ev[rb_ev_tail % RB_EV_N];
        char sc[48];
        int n = 0;
        for (int k = 0; k < e->cand_n && k < WF_RB_NCAND; k++)
            n += snprintf(sc + n, sizeof sc - n, "%s%d", k ? "/" : "", e->score[k]);
        if (!n)
            snprintf(sc, sizeof sc, "-");
        if (e->why == WF_RBW_CAM)
            wf_log("rawb: 8-bit (%s %x)", e->skip < WF_RBS_N ? rb_skipname[e->skip] : "?", e->arg);
        else if (e->why == WF_RBW_BUF)
            wf_log("rawb: 8-bit (buffer %s, n %d, scores %s) %dms", wf_rs_why(e->rs_why), e->n_rs, sc, e->t_pick);
        else if (e->why != WF_RBW_OK)
            wf_log("rawb: 8-bit (%s)", e->why < WF_RBW_N ? rb_whyname[e->why] : "?");
        else {
            if (e->cont)
                wf_log("rawb: BOT half continues the TOP's develop (raw %x unchanged)", (unsigned)e->addr);
            else
                wf_log("rawb: buf %d %x of %d, scores %s, blocks %d", e->pick, (unsigned)e->addr, e->cand_n, sc, e->n_rs);
            if (!e->cont)
                wf_log("rawb: black %d%s white %d%s, wb %d t%d n%d, clip %d fill %d, roll %d c%d y%d, shoulder %s %dms",
                       e->black, e->lv_black ? "" : "(cam)", e->white, e->lv_white ? "" : "(iso)", e->wb_src, e->tier,
                       e->fit_n, e->clip, e->hl_fill, e->roll, e->ro_c, e->ro_ytop, wf_dev_hue_name(e->hue),
                       e->ro_ms);
            wf_log("rawb: %s rows %d, pick %d lev %d prep %d rows %d ms%s%s%s",
                   e->ab ? "STOPPED" : "done", e->rows, e->t_pick, e->t_lev, e->t_prep, e->t_rows,
                   e->ab ? " (" : "", e->ab ? rb_abname[e->ab] : "", e->ab ? ")" : "");
            if (e->h0 != e->h1)
                wf_log("rawb: the raw changed during the develop (%x -> %x)", (unsigned)e->h0, (unsigned)e->h1);
        }
        WF_BARRIER();
        rb_ev_tail++;
    }
    if (rb_ev_lost) {
        wf_log("rawb: %d shot lines lost", (int)rb_ev_lost);
        rb_ev_lost = 0;
    }
}

static void rb_box(void)
{
    int p = rb_prog, now = get_ms_clock();
    if (p < 0) {
        rb_box_last = -2;
        return;
    }
    if (rb_box_last != -2 && now - rb_box_t < RB_BOX_MS)
        return;
    rb_box_t = now;
    rb_box_last = p;
    if (p >= 100)
        NotifyBox(RB_BOX_MS * 2, "RAW develop: preparing");
    else
        NotifyBox(RB_BOX_MS * 2, "RAW develop %d%%  (hold half-shutter: stop)", p);
}

void wf_rawb_poll(void)
{
    rb_report();
    rb_box();
    struct wf_look lk;
    wf_look_now(&lk);
    int play = is_play_mode();
    /* a darkroom run (wf_dark.c) takes the shoot memory: the block goes while it lives, then is built again */
    int want = wf_rawb && wf_yuv_active() && !play && !wf_unloading && !wf_dark_busy() ? rb_key(&lk) : 0;
    uint32_t wstk = rb_stk(&lk, want);
    if (rb_have && (rb_have != want || rb_have_stk != wstk)) {
        rb_have = 0;                             /* new shots stop using the tables */
        WF_BARRIER();
    }
    if (rb_live)
        return;                                  /* a develop holds the tables and the scratch: the next poll */
    if (play != rb_play) {
        rb_play = play;
        if (play && rb_mem)
            wf_log("rawb: playback, shoot memory given back");
    }
    if (!want) {
        if (rb_mem)
            rb_free();
        rb_tried = 0;                            /* freed: the next want builds again (also after playback) */
        rb_nomem = 0;
        return;
    }
    if (want == rb_have && wstk == rb_have_stk) {
        if (rb_rbt_owed == want)
            rb_rbt_retry();                      /* the .RBT of these tables, once Canon is idle */
        return;
    }
    rb_rbt_owed = 0;                             /* rb_tabs are rebuilt below */
    int now = get_ms_clock();
    if (want == rb_tried && wstk == rb_tried_stk && (!rb_nomem || now - rb_retry < RB_RETRY_MS))
        return;                                  /* this key failed: wait for a change (or the no-memory retry) */
    rb_tried = want;
    rb_tried_stk = wstk;
    if (!rb_mem && !rb_alloc()) {
        if (!rb_nomem)
            wf_log("rawb: no shoot memory for the develop tables (%d KB), retry every %d s",
                   (int)((sizeof(struct wf_dev_tabs) + wf_dev_scratch_bytes(RB_W, RB_H)) >> 10), RB_RETRY_MS / 1000);
        rb_nomem = 1;
        rb_retry = now;
        rb_rc = WF_LL_E_NOMEM;
        wf_gen_bump();
        return;
    }
    rb_nomem = 0;
    if ((want & 15) == RB_K_LG) {                /* a chain film's log table from the pack: no parse, no .RBT */
        struct wf_dev_pkinfo pi;
        rb_rc = wf_rawb_logpk_build(rb_tabs, rb_scr, wf_dev_scratch_bytes(RB_W, RB_H), &lk, &rb_build_ms, &pi);
        if (rb_rc == WF_LL_OK) {
            /* the SD load time: open + header + index + the 215 KB table streamed through the node sampler (a knob
               table: the two halves, the x section and the shipped table read, two compositions, the build) */
            if (wstk)
                wf_log("rawb: tables knobs %s v%d k%x interlayer %d c%d %dms (sum %x, %d KB shoot memory)",
                       wf_films[lk.row].code, lk.variant, (unsigned)wstk, lk.siie * 25, pi.curve, rb_build_ms,
                       (unsigned)pi.sum, (int)((sizeof(struct wf_dev_tabs) + wf_dev_scratch_bytes(RB_W, RB_H)) >> 10));
            else
                wf_log("rawb: tables log pack %s v%d c%d %dms (offset %d, %d entries, sum %x, %d KB shoot memory)",
                       wf_films[lk.row].code, lk.variant, pi.curve, rb_build_ms, (int)pi.off, pi.count, (unsigned)pi.sum,
                       (int)((sizeof(struct wf_dev_tabs) + wf_dev_scratch_bytes(RB_W, RB_H)) >> 10));
            rb_have_stk = wstk;
            WF_BARRIER();
            rb_gen++;
            rb_have = want;
        } else {
            rb_free();                           /* rb_key says 1 now: the next poll builds the 8-bit chain's base */
        }
        wf_gen_bump();
        return;
    }
    char cn[WF_LL_NAME];
    const char *cube = 0;
    int src = lk.output == WF_OUT_FILM ? WF_DEV_SRGB : lk.output == WF_OUT_LOG ? WF_DEV_LOG
            : WF_FLUT_DISP(lk.flut) ? WF_DEV_DISP : WF_DEV_CUBE;
    if (src == WF_DEV_DISP) {                    /* the owner's display cube itself (sRGB in, sRGB out): <CODE>SR.CUB, as the JPEG path */
        static const char pre[] = "ML/DATA/", suf[] = "SR.CUB";   /* by hand: ML's printf has no precision (trap W23) */
        const char *code = wf_flut_code[lk.flut];
        int n = 0, len = 0;
        while (len < 6 && code[len]) len++;
        for (int i = 0; pre[i]; i++) cn[n++] = pre[i];
        for (int i = 0; i < 6; i++) cn[n++] = i < len ? code[i] : '_';
        for (int i = 0; suf[i]; i++) cn[n++] = suf[i];
        cn[n] = 0;
        cube = cn;
    }
    if (src == WF_DEV_CUBE && !(cube = wf_loglut_name(cn, lk.curve, lk.flut, 0))) {
        rb_rc = WF_LL_E_MODE;
        wf_log("rawb: no cube name (curve %d lut %d)", lk.curve, lk.flut);
        return;
    }
    int t0 = get_ms_clock();
    int curve = src >= WF_DEV_SRGB ? 0 : lk.curve, autoexp = src == WF_DEV_CUBE && lk.flut != 0;
    /* the .RBT first (cube tables only) */
    uint32_t rk[RB_RBT_NKEY];
    char rp[WF_LL_NAME];
    int rbt = wf_rawb_rbt_key(&lk, src, cube, rp, rk) == 0, got = -1;   /* key word 2 = rb_key(&lk) = want */
    if (rbt) {
        got = wf_llb_read(rp, rk, RB_RBT_NKEY, rb_tabs, sizeof(struct wf_dev_tabs));
        if (got == WF_LLB_HIT && (rb_tabs->src != src || rb_tabs->curve != curve || rb_tabs->cam != RB_CAM
                                  || rb_tabs->n < 2 || rb_tabs->n > 65))
            got = WF_LLB_BAD;                    /* never a key match, but cheap */
    }
    if (got == WF_LLB_HIT) {
        rb_rc = WF_LL_OK;
        char *z = (char *)&rb_inf;
        for (unsigned i = 0; i < sizeof rb_inf; i++) z[i] = 0;
    } else {
        /* plain log has no EI in the JPEG path (wf_log_words) either; the Film LUT cubes get their auto exposure */
        /* Leica Vivid: no auto exposure (LUT EI by hand), Highlights / Shadows on the cube's output */
        rb_rc = src == WF_DEV_DISP ? wf_dev_build_disp(rb_tabs, RB_CAM, cube, lk.lei, lk.hl, lk.sh, &rb_inf)
              : wf_dev_build(rb_tabs, src, curve, RB_CAM, cube, src == WF_DEV_CUBE ? lk.lei : 0, autoexp, &rb_inf);
    }
    rb_build_ms = get_ms_clock() - t0;
    if (rb_rc == WF_LL_OK) {
        char rs[64];                             /* ", <name>.RBT hit" / ", <name>.RBT none, written 12ms" */
        rs[0] = 0;
        if (rbt && got == WF_LLB_HIT)
            snprintf(rs, sizeof rs, ", %s hit", rp + 8);
        else if (rbt) {
            int t1 = get_ms_clock(), wr = wf_llb_write(rp, rk, RB_RBT_NKEY, rb_tabs, sizeof(struct wf_dev_tabs));
            if (!wr)
                snprintf(rs, sizeof rs, ", %s %s, written %dms", rp + 8, rb_llbname[got], get_ms_clock() - t1);
            else
                snprintf(rs, sizeof rs, ", %s %s, %s (%s)", rp + 8, rb_llbname[got],
                         wr == WF_FE_BUSY ? "write later" : "not written", wf_fe_name(wr));
            if (wr == WF_FE_BUSY) {
                for (int i = 0; i < RB_RBT_NKEY; i++)
                    rb_rbt_k[i] = rk[i];
                for (int i = 0; i < WF_LL_NAME; i++)
                    rb_rbt_path[i] = rp[i];
                rb_rbt_owed = want;
                rb_rbt_t = get_ms_clock();
            }
        }
        wf_log("rawb: tables %s c%d%s%s %dms (%d KB shoot memory)%s",
               src == WF_DEV_SRGB ? "srgb" : src == WF_DEV_LOG ? "log" : src == WF_DEV_DISP ? "disp" : "cube",
               curve, cube ? " " : "", cube ? cube + 8 : "", rb_build_ms,
               (int)((sizeof(struct wf_dev_tabs) + wf_dev_scratch_bytes(RB_W, RB_H)) >> 10), rs);
        int lw = src == WF_DEV_SRGB ? wf_rawb_logpk(&lk) : WF_RLG_NOTCHAIN;
        if (lw != WF_RLG_NOTCHAIN)
            wf_log("rawb: chain film %s v%d on the 8-bit chain (%s)", wf_films[lk.row].code, lk.variant,
                   wf_rawb_logpk_why(lw));
        WF_BARRIER();
        rb_gen++;                                /* a TOP half of the old tables cannot be continued */
        rb_have_stk = wstk;
        WF_BARRIER();
        rb_have = want;                          /* published last */
    } else {
        wf_log("rawb: tables err %d %s line %d (%s)", rb_rc, wf_loglut_errstr(rb_rc), rb_inf.line, cube ? cube + 8 : "-");
        rb_free();
    }
    wf_gen_bump();
}

int wf_rawb_state(void)
{
    if (!wf_rawb)
        return WF_RB_OFF;
    if (is_play_mode())
        return WF_RB_PLAY;
    struct wf_look lk;
    wf_look_now(&lk);
    int key = rb_key(&lk);
    uint32_t stk = rb_stk(&lk, key);
    if (!key)
        return WF_RB_IDLE;
    if (rb_have == key && rb_have_stk == stk)
        return WF_RB_READY;
    if (rb_nomem)
        return WF_RB_NOMEM;
    if (rb_tried == key && rb_tried_stk == stk && rb_rc != WF_LL_OK)
        return WF_RB_ERR;
    return WF_RB_LOADING;
}
int wf_rawb_err(void) { return rb_rc; }
int wf_rawb_busy(void) { return rb_live; }
int wf_rawb_holds(void) { return rb_live || rb_mem; }

void wf_rawb_end(void)
{
    for (int i = 0; i < 40 && rb_live; i++)
        msleep(50);
    wf_rawb_poll();                              /* wf_unloading: nothing wanted, the block goes (when no develop holds it) */
    if (rb_mem)
        wf_log("rawb: unload while a develop holds the tables: shoot memory kept");
}

#else /* no RAW develop on this camera */
int  wf_rawb_stage(struct wf_frame *f, const struct wf_look *lk) { (void)f; (void)lk; return 0; }
void wf_rawb_note(const struct wf_frame *f, int why) { (void)f; (void)why; }
void wf_rawb_poll(void) {}
void wf_rawb_end(void) {}
int  wf_rawb_state(void) { return wf_rawb ? WF_RB_NOCAM : WF_RB_OFF; }
int  wf_rawb_err(void) { return 0; }
int  wf_rawb_busy(void) { return 0; }
int  wf_rawb_holds(void) { return 0; }
int  wf_rawb_rbt_key(const struct wf_look *lk, int src, const char *cube, char *path, uint32_t *key)
{
    (void)lk; (void)src; (void)cube; (void)path; (void)key;
    return -1;
}
int  wf_rawb_logpk(const struct wf_look *lk) { (void)lk; return WF_RLG_NOTCHAIN; }
const char *wf_rawb_logpk_why(int why) { (void)why; return "no RAW develop"; }
int  wf_rawb_logpk_build(struct wf_dev_tabs *t, void *scr, size_t scr_n, const struct wf_look *lk, int *ms,
                         struct wf_dev_pkinfo *pi)
{
    (void)t; (void)scr; (void)scr_n; (void)lk; (void)pi;
    *ms = 0;
    return -1;
}
#endif
