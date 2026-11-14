/* wf_cam_5d2.c - WindyFilm camera input layer, Canon EOS 5D Mark II 2.1.2 (built for CAM=5D2 only), "Path A".
 *
 * The 5D2 develops a JPEG L in two half frames, each handed to sdfExecuteYuvToJpegPath(r0 = YUV half, r1 = second buffer,
 * r2, r3 = size class) in its own task: FrontDevelop (TOP, 0xFF888884) and RearDevelop (BOT, 0xFF8867A0), priority 0x14.
 * Both hooks or none (a TOP whose BOT cannot be hooked would leave every shot half graded). No post-shot develop fallback.
 *
 * Pairing: a shot is TOP(n) then BOT(n) (the rear encodes only after the front's JPEG). A BOT is graded only right after
 * a graded TOP (wf_owed). A TOP that comes while the last graded TOP's BOT is still owed (within WF_PAIR_MS) is left
 * plain, so the owed BOT finds its own TOP's half state. A BOT that never comes holds new TOPs off for WF_PAIR_MS at most.
 * Both hooks run at task priority 0x14; between the load and the store of wf_owed there is no call that can block. */
#include "wf_core.h"
#include "wf_probe.h"

extern char __camera_model_short[];
extern char firmware_version[];
extern int  pic_quality;
extern int  streq(const char *, const char *);
extern int  patch_hook_function(uintptr_t addr, uint32_t orig, void *cbr, const char *desc);

#define SITE_TOP    0xFF888884u                  /* FrontDevelop sdfExecuteYuvToJpegPath, push {r3-fp,lr}; r0 = top half */
#define INSTR_TOP   0xE92D4FF8u
#define SITE_BOT    0xFF8867A0u                  /* RearDevelop sdfExecuteYuvToJpegPath, push {r4-sl,lr}; r0 = bottom half */
#define INSTR_BOT   0xE92D47F0u
#define FRAME_W     5616
#define FRAME_H     3744
#define HALF_BYTES  0x0140D600u                  /* 5616 * 1872 * 2 (ROM literal 0xFF9BE83C) */
#define POOL_LO     0x44000000u                  /* MEMORY_MGR, uncached (RscMgr map) */
#define POOL_HI     0x60000000u
#define PICQ_KIND_RAW 0x04000000
#define WF_PAIR_MS  20000

#ifdef WF_HOST
extern uint32_t wf_host_rom_read(uint32_t addr);
#define ROM32(a) wf_host_rom_read(a)
#else
#define ROM32(a) (*(volatile uint32_t *)(a))
#endif

/* the prologues and the next instructions, the +0x54 / +0x6c stores of the buffer and r1, the only callers' bl, the
 * half-frame size literal: all 12 must read as listed or the body is not supported */
static const uint32_t rom_words[12][2] = {
    { SITE_TOP,    INSTR_TOP },                  /* push {r3-fp,lr} */
    { 0xFF888888u, 0xE51F61D0u },                /* ldr r6, =0x369C            front state */
    { 0xFF88888Cu, 0xE1A07000u },                /* mov r7, r0                 the buffer */
    { 0xFF888ACCu, 0xE5857054u },                /* str r7, [r5, #0x54]        buffer into the JPEG path */
    { 0xFF888AD4u, 0xE585806Cu },                /* str r8, [r5, #0x6c]        r1 */
    { 0xFF88AB40u, 0xEBFFF74Fu },                /* bl 0xFF888884              the only caller */
    { SITE_BOT,    INSTR_BOT },                  /* push {r4-sl,lr} */
    { 0xFF8867A8u, 0xE1A05000u },                /* mov r5, r0 */
    { 0xFF886824u, 0xE5805054u },                /* str r5, [r0, #0x54] */
    { 0xFF886828u, 0xE580606Cu },                /* str r6, [r0, #0x6c] */
    { 0xFF887A94u, 0xEBFFFB41u },                /* bl 0xFF8867A0              the only caller */
    { 0xFF9BE83Cu, HALF_BYTES },                 /* half-frame YUV size */
};
static uint32_t bad_a, bad_w, bad_x;

enum { K_TOP = 0, K_BOT, K_N };
static const char *const kname[K_N] = { "TOP", "BOT" };
static uint8_t installed[K_N];
enum { R_BUF, R_ARGS, R_RAW, R_NOTOP, R_OVERLAP, R_WORKER, R_N };
static const char *const rname[R_N] = { "buffer", "r1/r3 not 0", "RAW only", "its TOP not graded",
                                        "BOT of the last shot owed", "worker" };
static int calls[K_N], rej[R_N], rej_logged[R_N], rej_k[K_N], rej_k_nojpg[K_N], rej_seen[K_N], rej_nojpg_seen[K_N];
static uint32_t rej_v[R_N][4];
static volatile int wf_owed, wf_owed_t;
static int bot_rej_seen;

volatile int wf_aspect = 0;                      /* no aspect setting on the 5D2: always 3:2 */

const char *wf_cam_name(void) { return "5D2"; }

static uint32_t check_buf(uint32_t b, uint32_t bytes)
{
    if (b < POOL_HI - 0x40000000u && b >= POOL_LO - 0x40000000u)
        b |= 0x40000000u;
    if (b < POOL_LO || b > POOL_HI - bytes || (b & 3))
        return 0;
    return b;
}

/* ------------------------------------------------------------------ RAW probe P0 (DESIGN 3.2), the 5D2 part
 * Same record, log and file as the EOS M (wf_probe.c), taken at the entry of the TOP and the BOT hook (kind K_TOP /
 * K_BOT in the record's mode bits 8-15). What is read (block, registers, ROM call, sources, the safe address range)
 * comes from raw-path/P0_5D2_SPEC.md; until then WF_PRB_CAN is 0 and nothing of it is built. Hook rules as on the
 * EOS M: no logging, no file I/O, only reads inside the spec's range, the frame is never written. */
#if WF_PRB_CAN
static const char *const prb_kname[K_N] = { "TOP", "BOT" };
static void prb_read_5d2(struct wf_prb *p, int mode, uint32_t *s);     /* from the spec */
static const struct wf_prb_cam prb_5d2 = {
    .ver = 0x0102, .what = "TOP/BOT", .kname = prb_kname, .pool_lo = POOL_LO, .pool_hi = POOL_HI,
    /* .npb, .rname, .sname, .read: from the spec */
};
#define PRB_ENTER(k, regs) wf_prb_enter(regs, k)
#define PRB_LEAVE(on, rc, t) do { if (on) wf_prb_leave(rc, t); } while (0)
#else
#define PRB_ENTER(k, regs) 0
#define PRB_LEAVE(on, rc, t) do { (void)(on); (void)(t); } while (0)
#endif

int wf_cam_init(void)
{
    if (!streq(__camera_model_short, "5D2") || !streq(firmware_version, "2.1.2"))
        return 0;
    for (unsigned i = 0; i < sizeof rom_words / sizeof *rom_words; i++) {
        uint32_t w = ROM32(rom_words[i][0]);
        if (w != rom_words[i][1]) {
            bad_a = rom_words[i][0]; bad_w = w; bad_x = rom_words[i][1];
            return 0;
        }
    }
#if WF_PRB_CAN
    wf_prb_setup(&prb_5d2);
#endif
    return 1;
}

int wf_cam_shot_ok(void)
{
    return wf_cam_ok && wf_worker_up() && installed[K_TOP] && installed[K_BOT];
}

int wf_cam_gate_pv(void)
{
    return (pic_quality & 0xFF000000) != PICQ_KIND_RAW;
}

static void reject(int k, int why, const uint32_t *regs)
{
    rej_k[k]++;
    if (why == R_RAW)
        rej_k_nojpg[k]++;                        /* a RAW-only shot has no JPEG: no EXIF entry */
    rej_v[why][0] = (uint32_t)k; rej_v[why][1] = regs[0]; rej_v[why][2] = regs[1]; rej_v[why][3] = regs[3];
    rej[why]++;
}

static void wf_hook_5d2(uint32_t *regs, uint32_t *stack, uint32_t pc)
{
    (void)stack;
    if (wf_unloading)
        return;
    int k = pc == SITE_TOP ? K_TOP : pc == SITE_BOT ? K_BOT : K_N;
    if (k == K_N || !installed[k])
        return;
    calls[k]++;
    int probe = PRB_ENTER(k, regs), t_shot = get_ms_clock();    /* RAW probe P0: off = 0, nothing read */
    uint32_t raw = regs[0];
    uint32_t buf = check_buf(raw, HALF_BYTES);
    int why = -1, owed = wf_owed;
    if (k == K_TOP && owed && get_ms_clock() - wf_owed_t < WF_PAIR_MS) {
        why = R_OVERLAP;                         /* keep the owed BOT's state: this shot stays plain */
    } else {
        wf_owed = 0;                             /* taken by this BOT, or dropped by a new TOP */
        if (!buf)
            why = R_BUF;
        else if (regs[1] != 0 || regs[3] != 0)   /* second YUV buffer (two-buffer develop) / not size L */
            why = R_ARGS;
        else if ((pic_quality & 0xFF000000) == PICQ_KIND_RAW)
            why = R_RAW;
        else if (k == K_BOT && !owed)
            why = R_NOTOP;                       /* never grade a bottom half under a plain top half */
        else if (!wf_worker_up())
            why = R_WORKER;
    }
    if (why >= 0) {
        reject(k, why, regs);
        PRB_LEAVE(probe, why == R_WORKER ? 2 : 1, t_shot);
        return;
    }
    struct wf_frame f;
    memset(&f, 0, sizeof f);
    f.fmt = WF_FMT_UYVY8;
    f.part = k == K_TOP ? WF_PART_TOP : WF_PART_BOT;
    f.aspect = 0;
    f.fw = FRAME_W;
    f.fh = FRAME_H;
    f.w = FRAME_W;
    f.h = FRAME_H / 2;
    f.y0 = k == K_BOT ? FRAME_H / 2 : 0;
    f.pitch = FRAME_W * 2;
    f.data = f.dst = (void *)(uintptr_t)buf;
    f.raw = raw;
    f.t_post = get_ms_clock();
    int st = -1;
    if (wf_shot_run(&f, &st) != 0) {
        reject(k, R_WORKER, regs);
        PRB_LEAVE(probe, 2, t_shot);
        return;
    }
    if (k == K_TOP) {
        wf_owed_t = get_ms_clock();
        wf_owed = st < 0 || WF_ST_TOUCHED(st);   /* the worker changed the half (or the result is unknown) */
    }
    PRB_LEAVE(probe, 0, t_shot);
}

int wf_cam_start(void)
{
    if (wf_worker_start() != 0) {
        wf_status_set("Worker failed");
        return -1;
    }
    static const char *const names[K_N] = { "windyflm top", "windyflm bottom" };
    static const uint32_t site[K_N] = { SITE_TOP, SITE_BOT }, instr[K_N] = { INSTR_TOP, INSTR_BOT };
    int err = -1;
    for (int k = K_TOP; k <= K_BOT; k++) {
        uint32_t w = ROM32(site[k]);
        err = w == instr[k] ? patch_hook_function(site[k], instr[k], (void *)wf_hook_5d2, names[k]) : -1;
        installed[k] = err == 0;
        wf_log("hook %s %x rom %08x err %x", kname[k], (unsigned)site[k], (unsigned)w, err);
        if (err)
            break;
    }
    if (err) {
        /* both or none: a TOP hook that went in stays (unpatching while a Canon task may be inside is unsafe) but
         * returns at once (installed 0) */
        installed[K_TOP] = installed[K_BOT] = 0;
        wf_status_set(err > 0 && (err & 0xA) ? "Disable filmroll.mo" : "Hook failed");
        wf_log("hook failed: films are not applied (%x)", err);
        return err;
    }
    wf_log("5D2 Path A on: halves %dx%d", FRAME_W, FRAME_H / 2);
    wf_gen_bump();
    return 0;
}

void wf_cam_poll(void)
{
    static int said;
    wf_prb_flush();                              /* RAW probe record (nothing while the 5D2 part is not built) */
    if (!said && bad_a) {
        said = 1;
        wf_log("5D2: ROM %x = %08x, not %08x: unsupported", (unsigned)bad_a, (unsigned)bad_w, (unsigned)bad_x);
    }
    for (int r = 0; r < R_N; r++)
        if (rej[r] != rej_logged[r]) {
            rej_logged[r] = rej[r];
            wf_log("yuv %s plain: %s (%d) r0 %x r1 %x r3 %x", kname[rej_v[r][0] < K_N ? rej_v[r][0] : 0], rname[r], rej[r],
                   (unsigned)rej_v[r][1], (unsigned)rej_v[r][2], (unsigned)rej_v[r][3]);
        }
}

int wf_cam_rejects_jpeg(int *bot)
{
    int n = (rej_k[K_TOP] - rej_seen[K_TOP]) - (rej_k_nojpg[K_TOP] - rej_nojpg_seen[K_TOP]);
    rej_seen[K_TOP] = rej_k[K_TOP];
    rej_nojpg_seen[K_TOP] = rej_k_nojpg[K_TOP];
    *bot = rej_k[K_BOT] != bot_rej_seen;
    bot_rej_seen = rej_k[K_BOT];
    return n;
}
