/* wf_cam_eosm.c - WindyFilm camera input layer, Canon EOS M 2.0.2 (built for CAM=EOSM only).
 *
 * The only code that knows the EOS M hook sites, registers, buffer addresses and frame sizes. Each hook call is gated
 * here and handed to the worker as a struct wf_frame (wf_shot_run). No post-shot develop fallback: without our hooks
 * the module only shows its status.
 *
 * EOS M ROM facts (ROM1 dump):
 *   NN    SsDevelop develops one contiguous 5184x3456 UYVY frame (stride 10368 B) and hands it to CompleteSsrawToYuv
 *         ff169414 (push {r2-r8,lr}) as r1 before both JPEG encoders run; task ShootSsDevelop (prio 0x14). Only the
 *         call with r2 == 0 carries the frame (input 9 is posted with event 0).
 *   Diana top / bottom halves (5184x1728, 0x1116000 B) in separate buffers:
 *         TOP ff17036c `str r8,[r7,#0x58]` (r8 = the buffer), DianaFront; BOT ff16b580 `str r6,[r0,#0x58]` (r6), DianaRear.
 *   Canon RscMgr shoot pool 0x40D6C000 .. 0x60000000 (ROM literals ff0c4aa0 / ff0c4aa4).
 *   JPEG aspect (PROP_IMAGE_ASPECT_RATIO): 0 3:2 5184x3456, 1 1:1 3456x3456, 2 4:3 4608x3456, 7 16:9 5184x2912; the L frame
 *   is packed at the aspect's size inside the max-size buffer. */
#include "wf_core.h"

extern char __camera_model_short[];
extern char firmware_version[];
extern int  pic_quality, shooting_mode;
extern int  streq(const char *, const char *);
extern int  patch_hook_function(uintptr_t addr, uint32_t orig, void *cbr, const char *desc);
extern void prop_add_handler(uint32_t property, void *handler);

#define SITE_NN     0xFF169414u
#define INSTR_NN    0xE92D41FCu
#define REG_NN      1
#define SITE_TOP    0xFF17036Cu
#define INSTR_TOP   0xE5878058u
#define REG_TOP     8
#define SITE_BOT    0xFF16B580u
#define INSTR_BOT   0xE5806058u
#define REG_BOT     6
#define FRAME_W     5184
#define FRAME_H     3456
#define HALF_BYTES  0x01116000u                  /* 5184 * 1728 * 2 */
#define POOL_LO     0x40D6C000u
#define POOL_HI     0x60000000u
#define PICQ_KIND_RAW 0x04000000                 /* (pic_quality & 0xFF000000): RAW / MRAW / SRAW only */
#define PICQ_SIZE_MASK 0xFF00                    /* != 0: a JPEG M / S, not the L geometry */
#define SHOOTMODE_NIGHTH 0x17
#define SHOOTMODE_HDR    0x18
#define PROP_IMAGE_ASPECT_RATIO 0x80040059

#ifdef WF_HOST
extern uint32_t wf_host_rom_read(uint32_t addr);
extern uint32_t wf_host_mem_read(uint32_t addr);
extern int wf_host_memnavi(uint32_t *out, uint32_t *tmp, int a, int layer);
#define ROM32(a) wf_host_rom_read(a)
#define PRB_RD32(a) wf_host_mem_read(a)
#define PRB_NAVCALL(o, t, a, l) wf_host_memnavi(o, t, a, l)
#else
#define ROM32(a) (*(volatile uint32_t *)(a))     /* D-side read: the original word even after patching */
#define PRB_RD32(a) (*(volatile uint32_t *)(a))
#define PRB_NAVCALL(o, t, a, l) ((int (*)(uint32_t *, uint32_t *, int, int))PRB_NAV)(o, t, a, l)
#endif

extern void *FIO_CreateFile(const char *name);
extern void *FIO_CreateFileOrAppend(const char *name);
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);

enum { K_NN = 0, K_TOP, K_BOT, K_N };
static const char *const kname[K_N] = { "NN", "TOP", "BOT" };
static const uint32_t ksite[K_N] = { SITE_NN, SITE_TOP, SITE_BOT };
static const uint32_t kinstr[K_N] = { INSTR_NN, INSTR_TOP, INSTR_BOT };
static const uint8_t kreg[K_N] = { REG_NN, REG_TOP, REG_BOT };
static uint8_t installed[K_N];
static int calls[K_N], rejects[K_N], rej_logged[K_N], rej_nojpg[K_N], rej_nojpg_seen[K_N], rej_seen[K_N];
static uint32_t last_reject[K_N];
static int bot_rej_seen;

volatile int wf_aspect = -1;

const char *wf_cam_name(void) { return "EOS M"; }

static void wf_aspect_prop(unsigned property, void *priv, uint32_t *buf, unsigned len)
{
    (void)property; (void)priv;
    if (buf && len >= 4)
        wf_aspect = (int)buf[0];
    else if (buf && len >= 1)
        wf_aspect = *(uint8_t *)buf;
}

static int geom(int aspect, int *w, int *h)
{
    switch (aspect) {
    case 0: *w = 5184; *h = 3456; return 1;
    case 1: *w = 3456; *h = 3456; return 1;
    case 2: *w = 4608; *h = 3456; return 1;
    case 7: *w = 5184; *h = 2912; return 1;
    }
    return 0;
}

/* the uncached pool alias, or its cached twin (converted); anything else (NULL, small values) is rejected */
static uint32_t check_buf(uint32_t b, uint32_t bytes)
{
    if (b < POOL_HI - 0x40000000u && b >= POOL_LO - 0x40000000u)
        b |= 0x40000000u;
    if (b < POOL_LO || b > POOL_HI - bytes || (b & 3))
        return 0;
    return b;
}

int wf_cam_init(void)
{
    if (!streq(__camera_model_short, "EOSM") || !streq(firmware_version, "2.0.2"))
        return 0;
    if (ROM32(SITE_NN) != INSTR_NN)
        return 0;
    prop_add_handler(PROP_IMAGE_ASPECT_RATIO, (void *)wf_aspect_prop);
    return 1;
}

int wf_cam_shot_ok(void)
{
    return wf_cam_ok && wf_worker_up() && installed[K_NN];
}

/* preview: a shot now would be graded at all (the hook's gates) */
int wf_cam_gate_pv(void)
{
    int w, h, ga = wf_aspect < 0 ? 0 : wf_aspect;
    return geom(ga, &w, &h) && !(pic_quality & PICQ_SIZE_MASK) && (pic_quality & 0xFF000000) != PICQ_KIND_RAW
        && shooting_mode != SHOOTMODE_NIGHTH && shooting_mode != SHOOTMODE_HDR;
}

static void reject(int k, uint32_t raw, int nojpg)
{
    rejects[k]++;
    if (nojpg)
        rej_nojpg[k]++;
    last_reject[k] = raw;
}

/* ------------------------------------------------------------------ RAW probe P0 (DESIGN 3.2)
 * At the NN entry (CompleteSsrawToYuv, before Canon releases the SS resource) the hook copies into one static record:
 * the Eeko copy of the 0xE0 parameter block (+0x50 raw input, +0x64.. SSRAW A[0..4]), five EDMAC address registers,
 * optionally MEMNAVI_GetReadMemory(.., 1, layer) for layers 0..4, and for four candidate sources (raw, EDMAC WR0,
 * A[0], A[1]) FNV-1a of 4 KB windows at +0 / +8 MB / +16 MB, at entry and again after the worker, plus 1 KB copied
 * from the last window. Only shoot-pool addresses are read, through the uncached alias. The frame is never written.
 * wf_task (wf_cam_poll) logs the record and appends it to ML/LOGS/WFPROBE.BIN; until then later calls are skipped. */
#define PRB_PB      0x41E82CECu                  /* the parameter block's Eeko copy, uncached alias */
#define PRB_PBW     56                           /* 0xE0 bytes */
#define PRB_NAV     0xFF234680u                  /* MEMNAVI_GetReadMemory(out, tmp, 1, layer) */
#define PRB_NSRC    4
#define PRB_NWIN    3
#define PRB_WIN     4096
#define PRB_CPY     1024
#define PRB_MAX     32                           /* WFPROBE.BIN records per boot */
#define PRB_PATH    "ML/LOGS/WFPROBE.BIN"
#define PRB_MAGIC   0x42504657u                  /* "WFPB" */

static const uint32_t prb_edmac[5] = { 0xC0F04008u, 0xC0F26308u, 0xC0F26408u, 0xC0F26A08u, 0xC0F26B08u };
static const uint32_t prb_woff[PRB_NWIN] = { 0, 0x800000u, 0x1000000u };
static const char *const prb_sname[PRB_NSRC] = { "raw", "wr0", "A0", "A1" };

struct wf_prb {                                  /* a WFPROBE.BIN record, 4696 bytes, little-endian */
    uint32_t magic, ver, seq, t_ms, r0, r1, r2, mode;
    uint32_t pb[PRB_PBW];
    uint32_t edmac[5];
    int32_t  nav_rc[5];
    uint32_t nav[5][8];                          /* the call gets &nav[i][2]: room on both sides */
    uint32_t nav_tmp[5];
    uint32_t src[PRB_NSRC];                      /* the uncached address read, 0 = outside the shoot pool */
    uint32_t fnv[PRB_NSRC][PRB_NWIN];            /* at entry; 0 = window not read */
    uint32_t fnv2[PRB_NSRC][PRB_NWIN];           /* after the worker (or the reject) */
    uint32_t t_probe_ms, t_shot_ms, shot_rc;     /* shot_rc 0 graded, 1 gate reject, 2 worker busy */
    uint8_t  cpy[PRB_NSRC][PRB_CPY];             /* from the +16 MB window (else +0) */
};
static struct wf_prb prb;
static volatile int prb_full, prb_skipped;
static int prb_n, prb_written, prb_skip_said;
int wf_probe_mode;

static uint32_t prb_addr(uint32_t a)
{
    if (a >= POOL_LO - 0x40000000u && a < POOL_HI - 0x40000000u)
        a |= 0x40000000u;                        /* a cached twin (SSRAW A[] have bit 30 stripped) */
    return a >= POOL_LO && a <= POOL_HI - PRB_WIN ? a : 0;
}
static int prb_win_ok(uint32_t a, int w) { return a && a + prb_woff[w] <= POOL_HI - PRB_WIN; }
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
static void prb_sums(uint32_t (*f)[PRB_NWIN])
{
    for (int k = 0; k < PRB_NSRC; k++)
        for (int w = 0; w < PRB_NWIN; w++)
            f[k][w] = prb_win_ok(prb.src[k], w) ? prb_fnv(prb.src[k] + prb_woff[w]) : 0;
}
static void prb_take(const uint32_t *regs, int mode)
{
    int t0 = get_ms_clock();
    memset(&prb, 0, sizeof prb);
    prb.magic = PRB_MAGIC;
    prb.ver = 1;
    prb.seq = ++prb_n;
    prb.t_ms = t0;
    prb.r0 = regs[0];
    prb.r1 = regs[1];
    prb.r2 = regs[2];
    prb.mode = mode;
    for (int i = 0; i < PRB_PBW; i++)
        prb.pb[i] = PRB_RD32(PRB_PB + 4 * i);
    for (int i = 0; i < 5; i++)
        prb.edmac[i] = PRB_RD32(prb_edmac[i]);
    if (mode == WF_PRB_NAV)
        for (int i = 0; i < 5; i++)
            prb.nav_rc[i] = PRB_NAVCALL(&prb.nav[i][2], &prb.nav_tmp[i], 1, i);
    const uint32_t s[PRB_NSRC] = { prb.pb[0x50 / 4], prb.edmac[0], prb.pb[0x64 / 4], prb.pb[0x68 / 4] };
    for (int k = 0; k < PRB_NSRC; k++)
        prb.src[k] = prb_addr(s[k]);
    prb_sums(prb.fnv);
    for (int k = 0; k < PRB_NSRC; k++) {
        if (!prb.src[k])
            continue;
        uint32_t c = prb.src[k] + (prb_win_ok(prb.src[k], PRB_NWIN - 1) ? prb_woff[PRB_NWIN - 1] : 0);
        const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)c;
        for (int i = 0; i < PRB_CPY; i++)
            prb.cpy[k][i] = p[i];
    }
    prb.t_probe_ms = get_ms_clock() - t0;
}
static void prb_done(int rc, int t_shot)
{
    prb.t_shot_ms = get_ms_clock() - t_shot;
    prb.shot_rc = rc;
    prb_sums(prb.fnv2);
    WF_BARRIER();
    prb_full = 1;
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

/* wf_task: the record to the log and to WFPROBE.BIN (a new file at the first record of a boot) */
static void prb_flush(void)
{
    char hx[80];
    if (prb_skipped != prb_skip_said) {
        prb_skip_said = prb_skipped;
        wf_log("probe: %d NN calls not probed (the previous record was not written yet)", prb_skip_said);
    }
    if (!prb_full)
        return;
    WF_BARRIER();
    const struct wf_prb *p = &prb;
    wf_log("probe #%u r0 %08x r1 %08x r2 %x mode %u: probe %u ms, shot %u ms rc %u", (unsigned)p->seq,
           (unsigned)p->r0, (unsigned)p->r1, (unsigned)p->r2, (unsigned)p->mode, (unsigned)p->t_probe_ms,
           (unsigned)p->t_shot_ms, (unsigned)p->shot_rc);
    for (int i = 0; i < PRB_PBW; i += 8)
        wf_log("probe pb+%02x%s", i * 4, prb_hex(hx, sizeof hx, p->pb + i, 8));
    wf_log("probe edmac wr0 %08x 308 %08x 408 %08x a08 %08x b08 %08x", (unsigned)p->edmac[0], (unsigned)p->edmac[1],
           (unsigned)p->edmac[2], (unsigned)p->edmac[3], (unsigned)p->edmac[4]);
    if (p->mode == WF_PRB_NAV)
        for (int i = 0; i < 5; i++)
            wf_log("probe nav %d: rc %d tmp %08x |%s", i, (int)p->nav_rc[i], (unsigned)p->nav_tmp[i],
                   prb_hex(hx, sizeof hx, p->nav[i], 8));
    const uint32_t s[PRB_NSRC] = { p->pb[0x50 / 4], p->edmac[0], p->pb[0x64 / 4], p->pb[0x68 / 4] };
    for (int k = 0; k < PRB_NSRC; k++) {
        if (!p->src[k]) {
            wf_log("probe %s %08x: outside the shoot pool, not read", prb_sname[k], (unsigned)s[k]);
            continue;
        }
        int same = 1;
        for (int w = 0; w < PRB_NWIN; w++)
            same &= p->fnv[k][w] == p->fnv2[k][w];
        wf_log("probe %s %08x: %08x %08x %08x -> %08x %08x %08x %s", prb_sname[k], (unsigned)p->src[k],
               (unsigned)p->fnv[k][0], (unsigned)p->fnv[k][1], (unsigned)p->fnv[k][2], (unsigned)p->fnv2[k][0],
               (unsigned)p->fnv2[k][1], (unsigned)p->fnv2[k][2], same ? "same" : "CHANGED");
    }
    if (prb_written < PRB_MAX) {
        void *f = prb_written ? FIO_CreateFileOrAppend(PRB_PATH) : FIO_CreateFile(PRB_PATH);
        if (!f || ((uint32_t)f & 1)) {
            wf_log("probe: %s not written", PRB_PATH);
        } else {
            FIO_WriteFile(f, p, sizeof *p);
            FIO_CloseFile(f);
            prb_written++;
        }
    }
    WF_BARRIER();
    prb_full = 0;
}

/* Canon task (ShootSsDevelop / DianaFront / DianaRear): gates, then the worker. No logging, no file I/O here. */
static void wf_hook_eosm(uint32_t *regs, uint32_t *stack, uint32_t pc)
{
    (void)stack;
    if (wf_unloading)
        return;
    int k;
    for (k = 0; k < K_N; k++)
        if (pc == ksite[k] && installed[k])
            break;
    if (k == K_N)
        return;
    calls[k]++;
    uint32_t raw = regs[kreg[k]];
    int pmode = *(volatile int *)&wf_probe_mode, probe = 0;
    if (k == K_NN && regs[2] == 0 && pmode > WF_PRB_OFF && pmode < WF_PRB_N) {
        if (prb_full) {
            prb_skipped++;
        } else {
            prb_take(regs, pmode);
            probe = 1;
        }
    }
    int t_shot = get_ms_clock();
    uint32_t buf = check_buf(raw, k == K_NN ? (uint32_t)FRAME_W * FRAME_H * 2 : HALF_BYTES);
    int nojpg = (k == K_NN && (pic_quality & 0xFF000000) == PICQ_KIND_RAW)      /* RAW only: no JPEG */
        || shooting_mode == SHOOTMODE_NIGHTH || shooting_mode == SHOOTMODE_HDR;   /* multishot sub-frames */
    int w = 0, h = 0, ga = wf_aspect < 0 ? 0 : wf_aspect;                       /* never reported: 3:2 */
    if (!buf || (k == K_NN && regs[2] != 0) || nojpg || (pic_quality & PICQ_SIZE_MASK) || !geom(ga, &w, &h)) {
        reject(k, raw, nojpg);
        if (probe)
            prb_done(1, t_shot);
        return;
    }
    struct wf_frame f;
    memset(&f, 0, sizeof f);
    f.fmt = WF_FMT_UYVY8;
    f.part = k == K_NN ? WF_PART_FULL : k == K_TOP ? WF_PART_TOP : WF_PART_BOT;
    f.aspect = (int8_t)wf_aspect;
    f.fw = w;
    f.fh = h;
    f.w = w;
    f.h = k == K_NN ? h : h / 2;
    f.y0 = k == K_BOT ? h / 2 : 0;
    f.pitch = w * 2;
    f.data = f.dst = (void *)(uintptr_t)buf;
    f.raw = raw;
    f.t_post = get_ms_clock();
    int rc = wf_shot_run(&f, 0);
    if (rc != 0)
        reject(k, raw, 0);                       /* worker busy / not up: the JPEG stays plain */
    if (probe)
        prb_done(rc != 0 ? 2 : 0, t_shot);
}

int wf_cam_start(void)
{
    if (wf_worker_start() != 0) {
        wf_status_set("Worker failed");
        return -1;
    }
    static const char *const names[K_N] = { "windyflm NN", "windyflm top", "windyflm bottom" };
    int err_nn = 0;
    for (int k = 0; k < K_N; k++) {
        uint32_t wd = ROM32(ksite[k]);
        int err = wd == kinstr[k] ? patch_hook_function(ksite[k], kinstr[k], (void *)wf_hook_eosm, names[k]) : -1;
        installed[k] = err == 0;
        wf_log("hook %s %x rom %08x err %x", kname[k], (unsigned)ksite[k], (unsigned)wd, err);
        if (k == K_NN && (err_nn = err) != 0)
            break;                               /* no Diana hooks without the main one */
    }
    if (err_nn) {
        /* 0x2 already patched / 0x8 old value mismatch: another module (the old port) hooks the same site */
        wf_status_set(err_nn > 0 && (err_nn & 0xA) ? "Disable filmroll.mo" : "Hook failed");
        wf_log("hook failed: films are not applied (%x)", err_nn);
        return err_nn;
    }
    wf_gen_bump();
    return 0;
}

/* wf_task: one line per kind whose reject count changed; the RAW probe record */
void wf_cam_poll(void)
{
    prb_flush();
    for (int k = 0; k < K_N; k++)
        if (rejects[k] != rej_logged[k]) {
            rej_logged[k] = rejects[k];
            wf_log("yuv %s#%d reject %x pq %x m %d a%d (%d)", kname[k], calls[k], (unsigned)last_reject[k],
                   (unsigned)pic_quality, shooting_mode, wf_aspect, rejects[k]);
        }
}

/* wf_shot.c (wf_task): rejected NN / TOP calls since the last ask that still made a JPEG (EXIF ring alignment);
 * *bot = 1 when a BOT was rejected meanwhile */
int wf_cam_rejects_jpeg(int *bot)
{
    int n = 0;
    for (int k = K_NN; k <= K_TOP; k++) {
        n += (rejects[k] - rej_seen[k]) - (rej_nojpg[k] - rej_nojpg_seen[k]);
        rej_seen[k] = rejects[k];
        rej_nojpg_seen[k] = rej_nojpg[k];
    }
    *bot = rejects[K_BOT] != bot_rej_seen;
    bot_rej_seen = rejects[K_BOT];
    return n;
}
