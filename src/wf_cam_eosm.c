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
#define ROM32(a) wf_host_rom_read(a)
#else
#define ROM32(a) (*(volatile uint32_t *)(a))     /* D-side read: the original word even after patching */
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
    uint32_t buf = check_buf(raw, k == K_NN ? (uint32_t)FRAME_W * FRAME_H * 2 : HALF_BYTES);
    int nojpg = (k == K_NN && (pic_quality & 0xFF000000) == PICQ_KIND_RAW)      /* RAW only: no JPEG */
        || shooting_mode == SHOOTMODE_NIGHTH || shooting_mode == SHOOTMODE_HDR;   /* multishot sub-frames */
    int w = 0, h = 0, ga = wf_aspect < 0 ? 0 : wf_aspect;                       /* never reported: 3:2 */
    if (!buf || (k == K_NN && regs[2] != 0) || nojpg || (pic_quality & PICQ_SIZE_MASK) || !geom(ga, &w, &h)) {
        reject(k, raw, nojpg);
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
    if (wf_shot_run(&f, 0) != 0)
        reject(k, raw, 0);                       /* worker busy / not up: the JPEG stays plain */
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

/* wf_task: one line per kind whose reject count changed */
void wf_cam_poll(void)
{
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
