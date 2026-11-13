/* wf_expsim.h - "Preview exposure: Smooth (corrected)" (docs/EXPSIM.md): Canon's ExpSim stays off so LiveView is
 * auto-exposed (bright, smooth, strobe-friendly); the module measures the exposure difference between the shot
 * settings and the LiveView frame (dEV) and applies it digitally to the LiveView picture in the display filter
 * (wf_dispf), before the film look. ML's overlays read that corrected picture (ML core patch 0004). */
#ifndef WF_EXPSIM_H
#define WF_EXPSIM_H
#include <stdint.h>

#define WF_EXP_CANON    0                /* wf_exp_mode (menu values): Canon's own ExpSim, nothing of ours */
#define WF_EXP_SMOOTH   1

/* dEV = EV the shot gets minus EV of the LiveView frame, in 1/8 EV (+ = the shot is brighter than the LV
 * picture: brighten the preview). Raw units of the camera: shutter 56 = 1 s, aperture 8 = f/1, ISO 72 = 100,
 * one step = 1/8 EV (lens.h APEX_*). */
#define WF_EXP_BAD      0x7fff           /* no usable inputs */
struct wf_exp_in {
    int metered;                         /* 1: the camera meters the shot (P, Tv, Av, scene modes, M + Auto ISO) */
    int ec;                              /* exposure compensation, 1/8 EV (lens_info.ae), used when metered */
    int trim;                            /* WINDYFLM.CFG "exptrim", 1/8 EV, added to both cases */
    int stv, sav, siso;                  /* the shot: raw shutter, aperture, ISO (manual exposure) */
    int ftv, fav, fiso;                  /* the LiveView frame: raw shutter, aperture, ISO */
};
int wf_exp_dev8(const struct wf_exp_in *in);

/* smoothing: IIR (1/4 per frame) + hysteresis (+-2/8 EV) + clamp; a flash state below -5 EV */
#define WF_EXP_QMIN     (-40)            /* darkest gain applied: -5 EV */
#define WF_EXP_QMAX     32               /* brightest gain applied: +4 EV */
#define WF_EXP_FLASH_ON (-40)            /* smoothed dEV below this: flash? (no gain) */
#define WF_EXP_FLASH_OFF (-34)           /* ... and back above this */
#define WF_EXP_HYST     2
struct wf_exp_sm { int s16, q, flash, init; };
void wf_exp_sm_reset(struct wf_exp_sm *s);
int wf_exp_sm_step(struct wf_exp_sm *s, int dev8);   /* returns the gain to apply, 1/8 EV (0 = none) */

/* the gain as tables: Y' = enc(2^(q/8) dec(Y)) on the sRGB curve; chroma scaled by Y'/Y and, once the gain
 * clips, by a ramp from 1 (the gained luma at white) to 0 (4x white): highlights go towards white */
struct wf_exp_tab {
    uint8_t  gy[256];
    uint16_t cs[256];                    /* chroma scale Q8, indexed by the pair's mean luma */
    int      q;
};
void wf_exp_build(struct wf_exp_tab *t, int q);
/* n UYVY words {U s8, Y0, V s8, Y1} in place (wf_expk.c, -O2) */
void wf_exp_words(uint32_t *p, int n, const struct wf_exp_tab *t);

/* control / state, wf_expsim.c */
extern volatile int wf_exp_mode;         /* WF_EXP_*, WINDYFLM.CFG "exposure" */
extern volatile int wf_exp_trim;         /* WINDYFLM.CFG "exptrim", 1/8 EV, -16..16 */
extern volatile uintptr_t wf_exp_vpp;    /* camera: 0x404C8, the pointer to the frame parameters (host: a fake) */
int wf_exp_active(void);                 /* smooth mode and ExpSim off: the filter must run, with luma accurate */
const struct wf_exp_tab *wf_exp_frame(void);   /* display task, once per frame: steps the state; table or 0 (no gain) */
void wf_exp_poll(void);                  /* wf_task: ExpSim off / back, "flash?" text, log */
void wf_exp_deinit(void);                /* wf_deinit: ExpSim back */
/* wf_exp_ctl: what the menu says */
#define WF_EXPC_IDLE    0                /* Canon mode, or no LiveView */
#define WF_EXPC_SET     1                /* we switched ExpSim off (and give it back) */
#define WF_EXPC_USER    2                /* ExpSim was already off (Canon menu) */
#define WF_EXPC_FAIL    3                /* ExpSim stays on: no correction, set it to Disable in Canon's menu */
extern volatile int wf_exp_ctl;
extern volatile int wf_exp_dev_last;     /* last dEV, 1/8 EV (WF_EXP_BAD = none) */
extern volatile int wf_exp_q_last;       /* gain applied, 1/8 EV */
extern volatile int wf_exp_flash_last;   /* 1: flash? state */
#endif
