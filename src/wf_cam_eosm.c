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
#include "wf_probe.h"
#include "wf_rawb.h"

static const struct wf_prb_cam prb_eosm;          /* RAW probe P0, defined below */

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
    wf_prb_setup(&prb_eosm);
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

/* ------------------------------------------------------------------ RAW probe P0 (DESIGN 3.2), the EOS M part
 * At the NN entry (CompleteSsrawToYuv, before Canon releases the SS resource) the hook takes one record (wf_probe.c):
 * the Eeko copy of the 0xE0 parameter block (+0x50 raw input, +0x64.. SSRAW A[0..4]), five EDMAC address registers,
 * optionally MEMNAVI_GetReadMemory(.., 1, layer) for layers 0..4, and for four candidate sources (raw, raw2 = the same
 * place in the other raw buffer, A[0], A[1]) FNV-1a of 4 KB windows at +0 / +8 MB / +16 MB, at entry and again after
 * the worker, plus 1 KB copied (raw, raw2 from +16 MB; A[0], A[1] from +0: their +16 MB fell in the YUV frame). Only
 * shoot-pool addresses are read, through the uncached alias. The frame is never written. Record version 2.
 * wf_task (wf_cam_poll -> wf_prb_flush) logs the record and appends it to ML/LOGS/WFPROBE.BIN.
 * The two raw buffers (raw-film-engine, the 11-14 run): starts S 32 MB apart, pb+0x50 = S + PRB_RAW_OFF, EDMAC WR0 at
 * the NN entry = S + PRB_RAW_WR0 (3531 rows x 9240). The last frame of a burst finds pb+0x50 still on the previous shot
 * (its raw is in the other buffer); the first shot after power-on finds pb+0x50 = 0 (the raw is then taken from WR0). */
#define PRB_PB      0x41E82CECu                  /* the parameter block's Eeko copy, uncached alias */
#define PRB_NAV     0xFF234680u                  /* MEMNAVI_GetReadMemory(out, tmp, 1, layer) */
#define PRB_RAW_S0   0x46004894u                 /* raw buffer starts S */
#define PRB_RAW_S1   0x48004894u
#define PRB_RAW_STEP 0x02000000u
#define PRB_RAW_OFF  480242u                     /* pb+0x50 = S + this */
#define PRB_RAW_WR0  32626440u                   /* EDMAC WR0 at the NN entry = S + this */

static const uint32_t prb_edmac[PRB_NREG] = { 0xC0F04008u, 0xC0F26308u, 0xC0F26408u, 0xC0F26A08u, 0xC0F26B08u };
static const char *const prb_rname[PRB_NREG] = { "wr0", "308", "408", "a08", "b08" };
static const char *const prb_sname[PRB_NSRC] = { "raw", "raw2", "A0", "A1" };
static const uint8_t prb_cwin[PRB_NSRC] = { 2, 2, 0, 0 };

/* the same place in the other raw buffer, 0 = in neither */
static uint32_t prb_other(uint32_t a)
{
    a = a ? a | 0x40000000u : 0;
    if (a >= PRB_RAW_S0 && a < PRB_RAW_S1)
        return a + PRB_RAW_STEP;
    if (a >= PRB_RAW_S1 && a < PRB_RAW_S1 + PRB_RAW_STEP)
        return a - PRB_RAW_STEP;
    return 0;
}

/* hook context (wf_prb_enter): the block, the registers, the ROM call; sources raw, raw2, SSRAW A[0], A[1] */
static void prb_read_eosm(struct wf_prb *p, int mode, uint32_t *s)
{
    for (int i = 0; i < PRB_PBW; i++)
        p->pb[i] = PRB_RD32(PRB_PB + 4 * i);
    for (int i = 0; i < PRB_NREG; i++)
        p->edmac[i] = PRB_RD32(prb_edmac[i]);
    if (mode == WF_PRB_NAV)
        for (int i = 0; i < PRB_NNAV; i++)
            p->nav_rc[i] = PRB_NAVCALL(&p->nav[i][2], &p->nav_tmp[i], 1, i);
    uint32_t raw = p->pb[0x50 / 4], wr0 = p->edmac[0] ? p->edmac[0] | 0x40000000u : 0;
    if (!raw && wr0 >= PRB_RAW_S0 + PRB_RAW_WR0 && wr0 < PRB_RAW_S1 + PRB_RAW_STEP + PRB_RAW_WR0)
        raw = wr0 - PRB_RAW_WR0 + PRB_RAW_OFF;   /* the block not filled yet (first shot after power-on) */
    s[0] = raw;
    s[1] = prb_other(raw);
    s[2] = p->pb[0x64 / 4];
    s[3] = p->pb[0x68 / 4];
}

static const struct wf_prb_cam prb_eosm = {
    .ver = 2, .what = "NN", .pool_lo = POOL_LO, .pool_hi = POOL_HI, .npb = PRB_PBW,
    .rname = prb_rname, .sname = prb_sname, .cwin = prb_cwin, .read = prb_read_eosm,
};

/* ------------------------------------------------------------------ RAW develop "B" (wf_rawb.c): the raw of the NN shot
 * P0 (raw-path BAOCAO section 7, CORE-NOTES 10): the 14-bit raw (pitch 9240, 5280 x 3528, RGGB, black 2048) sits in one of
 * two buffers 32 MB apart (0x48004894 / 0x46004894 in the probe runs). Its origin S comes from the parameter block's +0x50
 * (S = (+0x50 | uncached) - 480242) or, when that is 0 (the first shot after power on), from EDMAC WR0 (S = (WR0 | uncached)
 * - 32626440). Neither is trusted: the pointer names the previous shot at the end of a burst. So the worker gets S, S - 32 MB
 * and S + 32 MB (those inside the pool) and checks each against Canon's frame (wf_rsrc.c). JPEG pixel (x, y) = raw pixel
 * (84 + x, 64 + y) in the L 3:2 frame (Day09 CR2s). Only RAW + L fine single shots (drive single / self-timer): a burst
 * would hold every frame 30 s, MRAW / SRAW are other buffers. */
#define RB_PICQ      0x3070000                   /* PICQ_RAW_JPG_LARGE_FINE (MRAW + L 0x3070001, SRAW + L 0x3070002) */
#define RB_PITCH     9240
#define RB_RW        5280
#define RB_RH        3528
#define RB_PB_OFF    480242u
#define RB_WR0_OFF   32626440u
#define RB_STEP      0x2000000u
#define RB_WR0       0xC0F04008u
extern int drive_mode;

/* the white of the shot's ISO when the raw has no clip pile-up to measure (Day09 / Day10 CR2s: the 1/3-stop ISOs clip
   lower) */
static int rb_white(int iso)
{
    switch (iso) {
    case 100: return 13582;
    case 160: case 320: return 12636;
    case 200: return 15303;
    }
    return 15200;
}

static void rawb_fill(struct wf_frame *f, int ga)
{
    int dm = drive_mode;
    if (pic_quality != RB_PICQ) {
        f->rb_skip = WF_RBS_QUALITY;
        f->rb_arg = pic_quality;
        return;
    }
    if (ga != 0) {
        f->rb_skip = WF_RBS_ASPECT;
        f->rb_arg = ga;
        return;
    }
    if (dm != 0 && dm != 0x10 && dm != 0x11) {  /* DRIVE_SINGLE, DRIVE_SELFTIMER_REMOTE, DRIVE_SELFTIMER_2SEC */
        f->rb_skip = WF_RBS_DRIVE;
        f->rb_arg = dm;
        return;
    }
    uint32_t pbv = PRB_RD32(PRB_PB + 0x50), wr0 = PRB_RD32(RB_WR0), s = 0;
    if (pbv)
        s = (pbv | 0x40000000u) - RB_PB_OFF;
    else if (wr0)
        s = (wr0 | 0x40000000u) - RB_WR0_OFF;
    uint32_t c[3] = { s, s - RB_STEP, s + RB_STEP };
    int n = 0;
    for (int i = 0; i < 3 && s; i++) {
        uint32_t b = check_buf(c[i], (uint32_t)RB_PITCH * RB_RH);
        int dup = 0;
        for (int j = 0; j < n; j++)
            dup |= f->rawc[j] == b;
        if (b && !dup)
            f->rawc[n++] = b;
    }
    if (!n) {
        f->rb_skip = WF_RBS_NOADDR;
        f->rb_arg = (int)(pbv ? pbv : wr0);
        return;
    }
    f->nrawc = (uint8_t)n;
    f->fmt = WF_FMT_RAW14;
    f->bpp = 14;
    f->cfa = 0;
    f->black = 2048;
    f->white = (uint16_t)rb_white(wf_iso_now());
    f->rpitch = RB_PITCH;
    f->rax0 = 72; f->ray0 = 52; f->rax1 = RB_RW; f->ray1 = RB_RH;
    f->rx0 = 84; f->ry0 = 64;
    f->rbx0 = 16; f->rbx1 = 64;
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
    int probe = k == K_NN && regs[2] == 0 && wf_prb_enter(regs, 0); /* RAW probe: the frame call only */
    int t_shot = get_ms_clock();
    uint32_t buf = check_buf(raw, k == K_NN ? (uint32_t)FRAME_W * FRAME_H * 2 : HALF_BYTES);
    int nojpg = (k == K_NN && (pic_quality & 0xFF000000) == PICQ_KIND_RAW)      /* RAW only: no JPEG */
        || shooting_mode == SHOOTMODE_NIGHTH || shooting_mode == SHOOTMODE_HDR;   /* multishot sub-frames */
    int w = 0, h = 0, ga = wf_aspect < 0 ? 0 : wf_aspect;                       /* never reported: 3:2 */
    if (!buf || (k == K_NN && regs[2] != 0) || nojpg || (pic_quality & PICQ_SIZE_MASK) || !geom(ga, &w, &h)) {
        reject(k, raw, nojpg);
        if (probe)
            wf_prb_leave(0, 1, t_shot);
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
    if (k == K_NN && wf_rawb && !wf_roll_shooting())   /* a roll's frames: the darkroom develops them later */
        rawb_fill(&f, ga);
    f.t_post = get_ms_clock();
    int rc = wf_shot_run(&f, 0);
    if (rc != 0)
        reject(k, raw, 0);                       /* worker busy / not up: the JPEG stays plain */
    if (probe)
        wf_prb_leave(0, rc != 0 ? 2 : 0, t_shot);
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
    wf_prb_flush();
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
