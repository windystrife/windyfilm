/* wf_probe.c - RAW probe P0, the camera-independent part (wf_probe.h). Moved out of wf_cam_eosm.c (a675c8a) without a
 * change of the EOS M record or log lines, so the 5D2 layer can use the same record, hashes, log and file. One record
 * slot per probed call kind (the 5D2 BOT comes a few hundred ms after its TOP, often before wf_task flushed the TOP). */
#include "wf_core.h"
#include "wf_probe.h"

extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);

static const uint32_t prb_woff[PRB_NWIN] = { 0, 0x800000u, 0x1000000u };

/* a slot: the hook fills it while `full` is 0 and sets `full` last (wf_prb_leave); wf_task logs it, copies it to
   prb_out and clears `full`. A call of a kind whose slot is still full is skipped and counted. */
struct prb_slot {
    struct wf_prb r;
    uint32_t s[PRB_NSRC];                        /* the raw source addresses the camera named (for the log) */
    volatile int full;
    int logged;
};
static const struct wf_prb_cam *prb_cam;
static struct prb_slot prb_sl[PRB_NK];
static volatile int prb_skipped;
static int prb_n, prb_written, prb_skip_said;
int wf_probe_mode;
/* wf_task: the record on its way to WFPROBE.BIN. The flush copies a taken record here (the hook may take the next one
   at once) and writes it at its own offset, prb_written records in, so a retry after a short write overwrites the
   torn record; while the card refuses (busy with a shot) it retries every PRB_RETRY ms, for PRB_GIVEUP ms at most. */
#define PRB_RETRY  200
#define PRB_GIVEUP 30000
static struct wf_prb prb_out;
static int prb_out_busy, prb_out_tries, prb_out_t0, prb_out_last;

void wf_prb_setup(const struct wf_prb_cam *c) { prb_cam = c; }
int wf_prb_can(void) { return prb_cam != 0; }

static uint32_t prb_addr(uint32_t a)
{
    uint32_t lo = prb_cam->pool_lo, hi = prb_cam->pool_hi;
    if (a >= lo - 0x40000000u && a < hi - 0x40000000u)
        a |= 0x40000000u;                        /* a cached twin (EOS M SSRAW A[] have bit 30 stripped) */
    return a >= lo && a <= hi - PRB_WIN ? a : 0;
}
static int prb_win_ok(uint32_t a, int w) { return a && a + prb_woff[w] <= prb_cam->pool_hi - PRB_WIN; }
static uint32_t prb_fnv(uint32_t a)
{
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)a;
    uint32_t h = 2166136261u;
    for (int i = 0; i < PRB_WIN; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
static void prb_sums(const struct wf_prb *p, uint32_t (*f)[PRB_NWIN])
{
    for (int k = 0; k < PRB_NSRC; k++)
        for (int w = 0; w < PRB_NWIN; w++)
            f[k][w] = prb_win_ok(p->src[k], w) ? prb_fnv(p->src[k] + prb_woff[w]) : 0;
}
static int prb_slot_of(int kind) { return prb_cam && prb_cam->kname && kind >= 0 && kind < PRB_NK ? kind : 0; }

/* hook context: the camera layer calls this at the entry of the call it probes */
int wf_prb_enter(const uint32_t *regs, int kind)
{
    int mode = *(volatile int *)&wf_probe_mode;
    if (!prb_cam || mode <= WF_PRB_OFF || mode >= WF_PRB_N)
        return 0;
    struct prb_slot *sl = &prb_sl[prb_slot_of(kind)];
    if (sl->full) {
        prb_skipped++;
        return 0;
    }
    struct wf_prb *p = &sl->r;
    int t0 = get_ms_clock();
    memset(p, 0, sizeof *p);
    p->magic = PRB_MAGIC;
    p->ver = prb_cam->ver;
    p->seq = ++prb_n;
    p->t_ms = t0;
    p->r0 = regs[0];
    p->r1 = regs[1];
    p->r2 = regs[2];
    p->mode = (uint32_t)mode | (uint32_t)(kind & 0xff) << 8;
    memset(sl->s, 0, sizeof sl->s);
    prb_cam->read(p, mode, sl->s);
    for (int k = 0; k < PRB_NSRC; k++)
        p->src[k] = prb_addr(sl->s[k]);
    prb_sums(p, p->fnv);
    for (int k = 0; k < PRB_NSRC; k++) {
        if (!p->src[k])
            continue;
        int w = prb_cam->cwin ? prb_cam->cwin[k] : PRB_NWIN - 1;
        uint32_t c = p->src[k] + (w > 0 && w < PRB_NWIN && prb_win_ok(p->src[k], w) ? prb_woff[w] : 0);
        const volatile uint8_t *q = (const volatile uint8_t *)(uintptr_t)c;
        for (int i = 0; i < PRB_CPY; i++)
            p->cpy[k][i] = q[i];
    }
    p->t_probe_ms = get_ms_clock() - t0;
    return 1;
}

void wf_prb_leave(int kind, int rc, int t_shot)
{
    struct prb_slot *sl = &prb_sl[prb_slot_of(kind)];
    sl->r.t_shot_ms = get_ms_clock() - t_shot;
    sl->r.shot_rc = rc;
    prb_sums(&sl->r, sl->r.fnv2);
    WF_BARRIER();
    sl->full = 1;
}

/* n words as " xxxxxxxx" each, built word by word (one short format, no long repeated format strings in .rodata) */
static const char *prb_hex(char *b, int size, const uint32_t *w, int n)
{
    int o = 0;
    b[0] = 0;
    for (int i = 0; i < n && o < size - 10; i++)
        o += snprintf(b + o, size - o, " %08x", (unsigned)w[i]);
    return b;
}

/* wf_task: one try at writing prb_out (a new file at the first record of a boot), logged with the failing step */
static void prb_out_try(void)
{
    int now = get_ms_clock(), seq = (int)prb_out.seq;
    if (prb_out_tries && now - prb_out_last < PRB_RETRY)
        return;
    prb_out_last = now;
    struct wf_fe e;
    void *f = wf_fopen_w(PRB_PATH, prb_written ? WF_FO_AT : WF_FO_NEW, (uint32_t)prb_written * sizeof prb_out, &e);
    if (f) {
        int w = FIO_WriteFile(f, &prb_out, sizeof prb_out);
        FIO_CloseFile(f);
        if (w != (int)sizeof prb_out) {
            e.step = WF_FE_WRITE;
            e.rc = w;
        }
    }
    if (e.step == WF_FE_OK) {
        prb_written++;
        prb_out_busy = 0;
        if (prb_out_tries)
            wf_log("probe: record #%d written to %s on try %d, %d ms after the shot", seq, PRB_PATH,
                   prb_out_tries + 1, now - (int)prb_out.t_ms);
        return;
    }
    if (!prb_out_tries++)
        wf_log("probe: record #%d not written to %s yet: %s rc %d (shot job %d), retrying", seq, PRB_PATH,
               wf_fe_name(e.step), e.rc, wf_job_state());
    if (now - prb_out_t0 >= PRB_GIVEUP) {
        wf_log("probe: record #%d not written to %s: %d tries in %d ms, last %s rc %d (shot job %d)", seq, PRB_PATH,
               prb_out_tries, now - prb_out_t0, wf_fe_name(e.step), e.rc, wf_job_state());
        prb_out_busy = 0;
    }
}

/* wf_task: a taken record to the log at once */
static void prb_log(const struct prb_slot *sl)
{
    char hx[80];
    const struct wf_prb_cam *c = prb_cam;
    const struct wf_prb *p = &sl->r;
    int mode = p->mode & 0xff, kind = (p->mode >> 8) & 0xff;
    wf_log("probe #%d%s%s r0 %08x r1 %08x r2 %x mode %d: probe %d ms, shot %d ms rc %d", (int)p->seq,
           c->kname ? " " : "", c->kname ? c->kname[kind < PRB_NK ? kind : 0] : "", (unsigned)p->r0,
           (unsigned)p->r1, (unsigned)p->r2, mode, (int)p->t_probe_ms, (int)p->t_shot_ms, (int)p->shot_rc);
    for (int i = 0; i < c->npb && i < PRB_PBW; i += 8)
        wf_log("probe pb+%02x%s", i * 4, prb_hex(hx, sizeof hx, p->pb + i, c->npb - i < 8 ? c->npb - i : 8));
    int o = 0;
    for (int i = 0; i < PRB_NREG && o < (int)sizeof hx - 16; i++)
        o += snprintf(hx + o, sizeof hx - o, " %s %08x", c->rname[i], (unsigned)p->edmac[i]);
    wf_log("probe edmac%s", hx);
    if (mode == WF_PRB_NAV)
        for (int i = 0; i < PRB_NNAV; i++)
            wf_log("probe nav %d: rc %x tmp %08x |%s", i, (unsigned)p->nav_rc[i], (unsigned)p->nav_tmp[i],
                   prb_hex(hx, sizeof hx, p->nav[i], 8));
    for (int k = 0; k < PRB_NSRC; k++) {
        if (!p->src[k]) {
            wf_log("probe %s %08x: outside the shoot pool, not read", c->sname[k], (unsigned)sl->s[k]);
            continue;
        }
        int same = 1;
        for (int w = 0; w < PRB_NWIN; w++)
            same &= p->fnv[k][w] == p->fnv2[k][w];
        wf_log("probe %s %08x: %08x %08x %08x -> %08x %08x %08x %s", c->sname[k], (unsigned)p->src[k],
               (unsigned)p->fnv[k][0], (unsigned)p->fnv[k][1], (unsigned)p->fnv[k][2], (unsigned)p->fnv2[k][0],
               (unsigned)p->fnv2[k][1], (unsigned)p->fnv2[k][2], same ? "same" : "CHANGED");
    }
}

/* wf_task (wf_cam_poll): the full slots to the log (in seq order), then one by one through prb_out to WFPROBE.BIN */
void wf_prb_flush(void)
{
    if (!prb_cam)
        return;
    if (prb_skipped != prb_skip_said) {
        prb_skip_said = prb_skipped;
        wf_log("probe: %d %s calls not probed so far (a record was still waiting for the card)", prb_skip_said,
               prb_cam->what);
    }
    if (prb_out_busy)
        prb_out_try();
    int ord[PRB_NK];
    for (int i = 0; i < PRB_NK; i++)
        ord[i] = i;
    if (prb_sl[0].full && prb_sl[1].full && prb_sl[1].r.seq < prb_sl[0].r.seq) {
        ord[0] = 1;
        ord[1] = 0;
    }
    WF_BARRIER();
    for (int i = 0; i < PRB_NK; i++) {
        struct prb_slot *sl = &prb_sl[ord[i]];
        if (sl->full && !sl->logged) {
            prb_log(sl);
            sl->logged = 1;
        }
    }
    for (int i = 0; i < PRB_NK; i++) {
        struct prb_slot *sl = &prb_sl[ord[i]];
        if (!sl->full || !sl->logged)
            continue;
        if (prb_out_busy)
            return;                              /* the previous record still waits for the card */
        if (prb_written < PRB_MAX) {
            memcpy(&prb_out, &sl->r, sizeof prb_out);
            prb_out_busy = 1;
            prb_out_tries = 0;
            prb_out_t0 = get_ms_clock();
        } else if (prb_written == PRB_MAX) {
            wf_log("probe: %s holds %d records, later ones are only logged", PRB_PATH, PRB_MAX);
            prb_written++;
        }
        sl->logged = 0;
        WF_BARRIER();
        sl->full = 0;                            /* the hook may take the next record of this kind */
        if (prb_out_busy)
            prb_out_try();
    }
}
