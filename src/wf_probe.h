/* wf_probe.h - RAW probe P0 (DESIGN 3.2), the camera-independent part: one record slot per probed call kind, the window
 * hashes, the log lines and ML/LOGS/WFPROBE.BIN. Log only: the frame is never written. Off at every boot (wf_probe_mode, never saved).
 *
 * The camera layer (wf_cam_eosm.c, wf_cam_5d2.c) says what a record holds through a struct wf_prb_cam: the
 * shoot-pool bounds (the only memory a source window may be read from), the log names, and read(), which fills the
 * block words, the register words, the optional ROM call and the raw source addresses. Contexts:
 *   hook (a Canon task):  wf_prb_enter() at the entry of a probed call, wf_prb_leave() after the worker / the reject.
 *                         No logging, no file I/O. While the slot of that kind is not flushed yet, later calls of
 *                         that kind are skipped (counted).
 *   wf_task:              wf_prb_flush() from wf_cam_poll(): log lines + one WFPROBE.BIN record.
 * The record layout is the same on both cameras (4696 bytes, little-endian); `ver` says what the words mean: EOS M 1
 * (sources raw / wr0 / A0 / A1, copies from +16 MB), EOS M 2 (raw / raw2 = the other raw buffer / A0 / A1, copies:
 * raw and raw2 from +16 MB, A0 / A1 from +0), 5D2 0x0102 (raw-path/P0_5D2_SPEC.md). */
#ifndef WF_PROBE_H
#define WF_PROBE_H

#include <stdint.h>

#define PRB_PBW     56                           /* block words (EOS M: the 0xE0 parameter block) */
#define PRB_NREG    5                            /* register words (EOS M: EDMAC address registers) */
#define PRB_NNAV    5                            /* ROM call results (EOS M: MEMNAVI_GetReadMemory layers 0..4) */
#define PRB_NSRC    4                            /* candidate sources hashed */
#define PRB_NWIN    3                            /* 4 KB windows per source at +0 / +8 MB / +16 MB */
#define PRB_WIN     4096
#define PRB_CPY     1024
#define PRB_MAX     64                           /* WFPROBE.BIN records per boot (the 5D2 takes two per shot) */
#define PRB_NK      2                            /* record slots: one per probed call kind (5D2 TOP / BOT) */
#define PRB_PATH    "ML/LOGS/WFPROBE.BIN"
#define PRB_MAGIC   0x42504657u                  /* "WFPB" */

struct wf_prb {                                  /* a WFPROBE.BIN record, 4696 bytes, little-endian */
    uint32_t magic, ver, seq, t_ms, r0, r1, r2;
    uint32_t mode;                               /* bits 0-7 WF_PRB_*, bits 8-15 the probed call (wf_prb_cam.kname index) */
    uint32_t pb[PRB_PBW];
    uint32_t edmac[PRB_NREG];
    int32_t  nav_rc[PRB_NNAV];
    uint32_t nav[PRB_NNAV][8];                   /* the call gets &nav[i][2]: room on both sides */
    uint32_t nav_tmp[PRB_NNAV];
    uint32_t src[PRB_NSRC];                      /* the uncached address read, 0 = outside the shoot pool */
    uint32_t fnv[PRB_NSRC][PRB_NWIN];            /* at entry; 0 = window not read */
    uint32_t fnv2[PRB_NSRC][PRB_NWIN];           /* after the worker (or the reject) */
    uint32_t t_probe_ms, t_shot_ms, shot_rc;     /* shot_rc 0 graded, 1 gate reject, 2 worker busy */
    uint8_t  cpy[PRB_NSRC][PRB_CPY];             /* from the window wf_prb_cam.cwin names (else +0) */
};

struct wf_prb_cam {
    uint32_t ver;                                /* record version (EOS M 1) */
    const char *what;                            /* log name of the probed call(s) ("NN") */
    const char *const *kname;                    /* 0: one probed call; else log names per kind ("TOP", "BOT"), one
                                                    record slot each (at most PRB_NK kinds) */
    uint32_t pool_lo, pool_hi;                   /* shoot pool, uncached; a source address bit 30 clear is its cached twin */
    int npb;                                     /* block words logged, <= PRB_PBW (8 per line) */
    const char *const *rname;                    /* log names of the PRB_NREG register words */
    const char *const *sname;                    /* log names of the PRB_NSRC sources */
    const uint8_t *cwin;                         /* per source: the window (0..PRB_NWIN-1) the 1 KB copy comes from;
                                                    0 = the last window */
    /* hook context: fill p->pb, p->edmac, p->nav* (mode WF_PRB_NAV only) and s[] = the raw source addresses */
    void (*read)(struct wf_prb *p, int mode, uint32_t *s);
};

void wf_prb_setup(const struct wf_prb_cam *c);   /* camera layer, wf_cam_init: this body can probe */
int  wf_prb_enter(const uint32_t *regs, int kind);   /* hook: 1 = a record was taken (then call wf_prb_leave) */
void wf_prb_leave(int kind, int rc, int t_shot); /* hook: rc 0 graded, 1 gate reject, 2 worker busy */
void wf_prb_flush(void);                         /* wf_task (wf_cam_poll) */
int  wf_prb_can(void);                           /* a camera layer called wf_prb_setup (the menu shows "RAW probe") */

#endif
