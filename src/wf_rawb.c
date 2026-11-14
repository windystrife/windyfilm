/* wf_rawb.c - RAW develop "B" in the shot pipeline (WindyStrife): the worker stage, the tables, the memory, the
 * progress box and the per-shot log line. See wf_rawb.h for the flow; the develop itself is wf_dev.c, the buffer check
 * wf_rsrc.c, the raw's address and the gates the camera layer (wf_cam_eosm.c; wf_cam_5d2.c: TOP + BOT halves). */
#include "wf_core.h"
#include "wf_loglut.h"
#include "wf_rawb.h"

volatile int wf_rawb;                            /* menu, off at every boot, never saved (wf_menu.c) */
int wf_rawb_can(void) { return WF_RAWB_CAN; }

#if WF_RAWB_CAN
#include "wf_dev.h"
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
    int16_t score[WF_RB_NCAND];
    int     arg, n_rs;
    uint32_t addr;
    int     black, white, lv_black, lv_white;     /* used; measured on the raw (0 = none) */
    int     t_pick, t_lev, t_prep, t_rows, rows;
    int     fit_n, clip, hl_fill;
    int     ro_c, ro_ytop;
    uint32_t h0, h1;                              /* raw sample hash before / after the develop */
};
#define RB_EV_N 8
static struct rb_ev rb_ev[RB_EV_N];
static volatile unsigned rb_ev_head, rb_ev_tail, rb_ev_lost;
static const char *const rb_whyname[WF_RBW_N] = { "ok", "notab", "buf", "prep", "look", "filmtab", "cam",
                                                   "TOP half not developed", "raw changed since the TOP half" };
static const char *const rb_skipname[WF_RBS_N] = { "", "quality", "aspect", "drive", "noaddr" };

/* the tables a look needs: 0 = B cannot do it (Off) */
static int rb_key(const struct wf_look *lk)
{
    switch (lk->output) {
    case WF_OUT_FILM: return 1;                                   /* the sRGB base develop, the film tables after */
    case WF_OUT_LOG:  return 2 | lk->curve << 4;                  /* the log code itself */
    case WF_OUT_LLUT: return 3 | lk->curve << 4 | lk->flut << 8 | ((uint8_t)lk->lei) << 16;
    }
    return 0;
}

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
        if (tk != key || rb_top_gen != rb_gen) {
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
    if (rb_have != key || !rb_mem) {
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
    rc = ab ? -1 : 1;
    if (top && !ab && e->h0 == e->h1) {          /* the BOT half may continue this develop */
        rb_top_raw = e->addr;
        rb_top_h = e->h1;
        rb_top_gen = rb_gen;
        rb_top_key = key;
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
static void rb_free(void)
{
    if (rb_suite)
        shoot_free_suite(rb_suite);
    rb_suite = 0;
    rb_mem = 0;
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
                wf_log("rawb: black %d%s white %d%s, wb %d t%d n%d, clip %d fill %d, roll %d c%d y%d", e->black,
                       e->lv_black ? "" : "(cam)", e->white, e->lv_white ? "" : "(iso)", e->wb_src, e->tier, e->fit_n,
                       e->clip, e->hl_fill, e->roll, e->ro_c, e->ro_ytop);
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
    int want = wf_rawb && wf_yuv_active() && !play && !wf_unloading ? rb_key(&lk) : 0;
    if (rb_have && rb_have != want) {
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
    if (want == rb_have)
        return;
    int now = get_ms_clock();
    if (want == rb_tried && (!rb_nomem || now - rb_retry < RB_RETRY_MS))
        return;                                  /* this key failed: wait for a change (or the no-memory retry) */
    rb_tried = want;
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
    char cn[WF_LL_NAME];
    const char *cube = 0;
    int src = lk.output == WF_OUT_FILM ? WF_DEV_SRGB : lk.output == WF_OUT_LOG ? WF_DEV_LOG : WF_DEV_CUBE;
    if (src == WF_DEV_CUBE && !(cube = wf_loglut_name(cn, lk.curve, lk.flut, 0))) {
        rb_rc = WF_LL_E_MODE;
        wf_log("rawb: no cube name (curve %d lut %d)", lk.curve, lk.flut);
        return;
    }
    int t0 = get_ms_clock();
    /* plain log has no EI in the JPEG path (wf_log_words) either; the Film LUT cubes get their auto exposure */
    rb_rc = wf_dev_build(rb_tabs, src, src == WF_DEV_SRGB ? 0 : lk.curve, RB_CAM, cube,
                         src == WF_DEV_CUBE ? lk.lei : 0, src == WF_DEV_CUBE && lk.flut != 0, &rb_inf);
    rb_build_ms = get_ms_clock() - t0;
    if (rb_rc == WF_LL_OK) {
        wf_log("rawb: tables %s c%d%s%s %dms (%d KB shoot memory)",
               src == WF_DEV_SRGB ? "srgb" : src == WF_DEV_LOG ? "log" : "cube", src == WF_DEV_SRGB ? 0 : lk.curve,
               cube ? " " : "", cube ? cube + 8 : "", rb_build_ms,
               (int)((sizeof(struct wf_dev_tabs) + wf_dev_scratch_bytes(RB_W, RB_H)) >> 10));
        WF_BARRIER();
        rb_gen++;                                /* a TOP half of the old tables cannot be continued */
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
    if (!key)
        return WF_RB_IDLE;
    if (rb_have == key)
        return WF_RB_READY;
    if (rb_nomem)
        return WF_RB_NOMEM;
    if (rb_tried == key && rb_rc != WF_LL_OK)
        return WF_RB_ERR;
    return WF_RB_LOADING;
}
int wf_rawb_err(void) { return rb_rc; }

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
#endif
