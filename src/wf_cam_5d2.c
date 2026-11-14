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
#include "wf_rawb.h"

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
 * K_BOT in the record's mode bits 8-15, one record slot each), record version 0x0102. What is read comes from
 * raw-path/P0_5D2_SPEC.md (WindyStrife's ROM analysis of the owner's dump):
 *   pb[0..1]   the job handle H and the job id word of this half's develop state (TOP 0x369C, BOT 0x3630)
 *   pb[2..6]   H[0..4] when H is a readable pointer; P = H[1] only when H[0] is the live-job magic
 *   pb[7]      the Mem1ToRaw source (CR2 compression input)
 *   pb[8..14]  P+0x4C job id, +0x48 stage count, +0x6C flags, +0x538 done mask (halfword), +0xBE8 done bits,
 *              +0xBE0 file container, +0xC68 MEM1 (the raw buffer; freed when the done bits reach the mask)
 *   pb[15..29] P[0..14]
 *   pb[30..39] the engio shadow base SH and the shadows of 9 EDMAC registers (capture write 208/20C/210, CRAW read
 *              A08/A0C/A10, YUV write 308, YUV read 4D08, JPEG write 408), read inline as shamem_read does
 *   pb[40..55] this half's Mem1ToYuv block +0x38 .. +0x74 (TOP 0x2AB14, BOT 0x2A9F4)
 *   edmac      the live address registers 208 / a08 / 308 / d08 / 408
 *   nav        "On + ROM call", for a live job only (the getters' error branch logs): GetImageBuffer, GetJobID,
 *              GetShootParam (+ its 8 words), GetFileContainer (all of H), shamem_read(0xC0F04208)
 *   sources    raw0 = MEM1 + 0x76C8 (5792 x 3804, pitch 10136, 14-bit packed), qr = the capture write shadow (ML's
 *              QR source), a08 = where this half's develop started reading, m2r = the Mem1ToRaw source; copies from
 *              +16 MB (inside the raw)
 * Pointers are read only when word aligned and inside RAM (cached 0x1000.. or uncached 0x40001000..0x5FFFFFFF, 512 MB:
 * no read can fault); sources only inside 0x44000000..0x5C000000 (tighter than the hook's range on purpose). Hook rules
 * as on the EOS M: no logging, no file I/O, the frame is never written. */
#if WF_PRB_CAN
#ifdef WF_HOST
extern uint32_t wf_host_mem_read(uint32_t addr);
extern uint32_t wf_host_romcall(uint32_t fn, uint32_t arg);
#define PRB_RD32(a) wf_host_mem_read(a)
#define PRB_CALL(fn, a) wf_host_romcall(fn, a)
#else
#define PRB_RD32(a) (*(volatile uint32_t *)(a))
#define PRB_CALL(fn, a) ((uint32_t (*)(uint32_t))(fn))(a)
#endif
#define PRB_ST_TOP    0x369Cu                    /* FrontDevelop state: +0x3C H, +0x40 job id */
#define PRB_ST_BOT    0x3630u                    /* RearDevelop state: +0x2C H, +0x30 job id */
#define PRB_M2Y_TOP   0x2AB14u                   /* Mem1ToYuv block of the half */
#define PRB_M2Y_BOT   0x2A9F4u
#define PRB_M2R       0x2AC34u                   /* Mem1ToRaw block: +0x54 the source */
#define PRB_SHADOW    0x8FBCu                    /* engio shadow base SH: shamem_read(r) = *(SH | (r & 0x3FFFF)) */
#define PRB_JOB_MAGIC 0xFFCD0761u                /* H[0] of a live job (= [0xFFC77B04]) */
#define PRB_MEM1_RAW  0x76C8u                    /* the capture EDMAC writes the raw at MEM1 + this */
#define PRB_POOL_HI   0x5C000000u                /* RscMgr MEMORY_MGR end */
static const uint32_t prb_edmac[PRB_NREG] = { 0xC0F04208u, 0xC0F04A08u, 0xC0F04308u, 0xC0F04D08u, 0xC0F04408u };
static const uint16_t prb_shreg[9] = { 0x4208, 0x420C, 0x4210, 0x4A08, 0x4A0C, 0x4A10, 0x4308, 0x4D08, 0x4408 };
static const uint16_t prb_poff[7] = { 0x4C, 0x48, 0x6C, 0x538, 0xBE8, 0xBE0, 0xC68 };
static const uint32_t prb_getter[PRB_NNAV] = { 0xFF8AC4B8u, 0xFF8AA9D4u, 0xFF8AC4F8u, 0xFF8AEB84u, 0xFF9A550Cu };
static const char *const prb_kname[K_N] = { "TOP", "BOT" };
static const char *const prb_rname[PRB_NREG] = { "208", "a08", "308", "d08", "408" };
static const char *const prb_sname[PRB_NSRC] = { "raw0", "qr", "a08", "m2r" };

/* a pointer that may be read for n bytes: word aligned, inside cached RAM or its uncached alias */
static int prb_ok(uint32_t a, uint32_t n)
{
    if (a & 3)
        return 0;
    return (a >= 0x1000u && a <= 0x20000000u - n) || (a >= 0x40001000u && a <= 0x60000000u - n);
}

/* hook context (wf_prb_enter): the job, the shadows, this half's block, the registers, the ROM calls, the sources */
static void prb_read_5d2(struct wf_prb *p, int mode, uint32_t *s)
{
    int bot = (int)(p->mode >> 8 & 0xff) == K_BOT;
    uint32_t st = bot ? PRB_ST_BOT : PRB_ST_TOP, h = PRB_RD32(st + (bot ? 0x2C : 0x3C)), pp = 0;
    p->pb[0] = h;
    p->pb[1] = PRB_RD32(st + (bot ? 0x30 : 0x40));
    if (prb_ok(h, 20)) {
        for (int i = 0; i < 5; i++)
            p->pb[2 + i] = PRB_RD32(h + 4 * i);
        if (p->pb[2] == PRB_JOB_MAGIC && prb_ok(p->pb[3], 0xC6C))
            pp = p->pb[3];
    }
    p->pb[7] = PRB_RD32(PRB_M2R + 0x54);
    if (pp) {
        for (int i = 0; i < 7; i++)
            p->pb[8 + i] = PRB_RD32(pp + prb_poff[i]);
        p->pb[11] &= 0xFFFFu;                    /* the done mask is the halfword at P+0x538 */
        for (int i = 0; i < 15; i++)
            p->pb[15 + i] = PRB_RD32(pp + 4 * i);
    }
    uint32_t sh = PRB_RD32(PRB_SHADOW);
    p->pb[30] = sh;
    if (prb_ok(sh, 4) && prb_ok(sh | 0x3FFFCu, 4))
        for (int i = 0; i < 9; i++)
            p->pb[31 + i] = PRB_RD32(sh | prb_shreg[i]);
    uint32_t blk = (bot ? PRB_M2Y_BOT : PRB_M2Y_TOP) + 0x38;
    for (int i = 0; i < 16; i++)
        p->pb[40 + i] = PRB_RD32(blk + 4 * i);
    for (int i = 0; i < PRB_NREG; i++)
        p->edmac[i] = PRB_RD32(prb_edmac[i]);
    if (mode == WF_PRB_NAV && p->pb[2] == PRB_JOB_MAGIC) {
        for (int i = 0; i < PRB_NNAV - 1; i++)
            p->nav_rc[i] = (int32_t)PRB_CALL(prb_getter[i], h);
        p->nav_rc[PRB_NNAV - 1] = (int32_t)PRB_CALL(prb_getter[PRB_NNAV - 1], prb_edmac[0]);
        uint32_t sp = (uint32_t)p->nav_rc[2];
        if (prb_ok(sp, 32))
            for (int i = 0; i < 8; i++)
                p->nav[2][i] = PRB_RD32(sp + 4 * i);
    }
    s[0] = p->pb[14] ? p->pb[14] + PRB_MEM1_RAW : 0;
    s[1] = p->pb[31];
    s[2] = p->pb[34];
    s[3] = p->pb[7];
}

static const struct wf_prb_cam prb_5d2 = {
    .ver = 0x0102, .what = "TOP/BOT", .kname = prb_kname, .pool_lo = POOL_LO, .pool_hi = PRB_POOL_HI, .npb = PRB_PBW,
    .rname = prb_rname, .sname = prb_sname, .read = prb_read_5d2,
};
#define PRB_ENTER(k, regs) wf_prb_enter(regs, k)
#define PRB_LEAVE(on, k, rc, t) do { if (on) wf_prb_leave(k, rc, t); } while (0)

#if WF_RAWB_CAN
/* ------------------------------------------------------------------ RAW develop "B" (wf_rawb.c): the raw of the shot
 * raw-path P0_5D2_SPEC.md [code, not yet probed on the camera]: each half's develop state holds the job handle H
 * (TOP [0x369C + 0x3C], BOT [0x3630 + 0x2C]); a live job has H[0] = 0xFFCD0761, P = H[1], MEM1 = [P + 0xC68], and the
 * capture EDMAC wrote the raw at MEM1 + 0x76C8: 5792 x 3804, pitch 10136, 14-bit packed, GBRG at (0, 0) and at the JPEG
 * crop (168, 56), black 1024. The owner's probe run (11-14, 8 records, TOP and BOT, IMG_4467..4476) found MEM1 + 0x76C8 =
 * CR2 row 1 (byte-exact or NCC 1.0000, 0..1 LSB): CR2 row 0, where these coordinates start, is one pitch before it. MEM1 is freed only after the BOT hook returns. Every pointer is read only where prb_ok
 * says it cannot fault, the raw only inside the RscMgr pool; the worker then checks its content against Canon's TOP
 * half (wf_rsrc.c) before anything is developed, and the BOT continues only the same raw, unchanged (wf_rawb.c).
 * Only RAW + L fine single shots (drive single / self-timer). */
#define RB_PICQ      0x3070000                   /* PICQ_RAW_JPG_LARGE_FINE */
#define RB_PITCH     10136
#define RB_RW        5792
#define RB_RH        3804
extern int drive_mode;

/* the white when the raw has no clip pile-up to measure: ISO 640 clipped at 13318 (raw-path measurements); the other
   1/3-stop-down ISOs are taken as the same family, the full stops as 15600 (ISO 2000 >= 15674 measured) */
static int rb_white(int iso)
{
    switch (iso) {
    case 160: case 320: case 640: case 1250: case 2500: case 5000: return 13300;
    }
    return 15600;
}

static void rawb_fill(struct wf_frame *f, int k)
{
    int dm = drive_mode;
    if (pic_quality != RB_PICQ) {
        f->rb_skip = WF_RBS_QUALITY;
        f->rb_arg = pic_quality;
        return;
    }
    if (dm != 0 && dm != 0x10 && dm != 0x11) {  /* DRIVE_SINGLE, DRIVE_SELFTIMER_REMOTE, DRIVE_SELFTIMER_2SEC */
        f->rb_skip = WF_RBS_DRIVE;
        f->rb_arg = dm;
        return;
    }
    uint32_t h = PRB_RD32(k == K_BOT ? PRB_ST_BOT + 0x2C : PRB_ST_TOP + 0x3C), p = 0, m1 = 0, b = 0;
    if (prb_ok(h, 8) && PRB_RD32(h) == PRB_JOB_MAGIC) {
        p = PRB_RD32(h + 4);
        if (prb_ok(p, 0xC6C))
            m1 = PRB_RD32(p + 0xC68);
    }
    if (m1 && m1 + PRB_MEM1_RAW > m1)
        b = check_buf(m1 + PRB_MEM1_RAW - RB_PITCH, (uint32_t)RB_PITCH * RB_RH);   /* CR2 row 0 (probe 11-14) */
    if (b > PRB_POOL_HI - (uint32_t)RB_PITCH * RB_RH)
        b = 0;                                   /* past the RscMgr pool's end */
    if (!b) {
        f->rb_skip = WF_RBS_NOADDR;
        f->rb_arg = (int)(m1 ? m1 : p ? p : h);
        return;
    }
    f->rawc[0] = b;
    f->nrawc = 1;
    f->fmt = WF_FMT_RAW14;
    f->bpp = 14;
    f->cfa = 2;                                  /* GBRG at (168, 56) */
    f->black = 1024;
    f->white = (uint16_t)rb_white(wf_iso_now());
    f->rpitch = RB_PITCH;
    f->rax0 = 160; f->ray0 = 52; f->rax1 = RB_RW; f->ray1 = RB_RH;
    f->rx0 = 168; f->ry0 = 56;
    f->rbx0 = 16; f->rbx1 = 144;                 /* masked columns (the lit area starts at 158) */
}
#endif
#else
#define PRB_ENTER(k, regs) 0
#define PRB_LEAVE(on, k, rc, t) do { (void)(on); (void)(t); } while (0)
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
        PRB_LEAVE(probe, k, why == R_WORKER ? 2 : 1, t_shot);
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
#if WF_PRB_CAN && WF_RAWB_CAN
    if (wf_rawb)
        rawb_fill(&f, k);                     /* RAW develop on: this half's raw and the gates */
#endif
    f.t_post = get_ms_clock();
    int st = -1;
    if (wf_shot_run(&f, &st) != 0) {
        reject(k, R_WORKER, regs);
        PRB_LEAVE(probe, k, 2, t_shot);
        return;
    }
    if (k == K_TOP) {
        wf_owed_t = get_ms_clock();
        wf_owed = st < 0 || WF_ST_TOUCHED(st);   /* the worker changed the half (or the result is unknown) */
    }
    PRB_LEAVE(probe, k, 0, t_shot);
}

int wf_cam_start(void)
{
    if (wf_worker_start() != 0)
        return WF_CAM_NOWORKER;                  /* wf_task tries again (wf_main.c), "Worker failed" only at the end */
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
    wf_prb_flush();                              /* RAW probe records */
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
