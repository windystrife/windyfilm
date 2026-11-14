/* wf_fx.h - WindyStrife film effects at the shot (wf_fx.c): halation + bloom (yh_halo.c) and
 * Yedlin-principled grain (yg_grain.c). Shared by wf_shot.c (worker passes, wf_task poll),
 * wf_menu.c (rows, ML menu, WINDYFLM.CFG) and the host test. */
#ifndef WF_FX_H
#define WF_FX_H
#include <stdint.h>

#define WF_FX_HAL 1                  /* effect bits of one request / Diana shot */
#define WF_FX_GRN 2
#define WF_FX_MST 4                  /* Mist (wf_mist.c): diffusion filter on the camera's words, before our colour pass */

/* menu choices (wf_menu.c, WINDYFLM.CFG "grain" / "halation"); the browser rows show and change the same values.
 * The last choices of each row are the Genesis Pro 1.6.6 model (DESIGN 1a), shown as "WindyGrain" / "WindyHalation":
 * Genesis's own grain (its amplitude tables, yg_gen_* in yg_grain.c) and the plugin's light wrap (yh_gen_* in
 * yh_halo.c). The Yedlin grain and the 3-bounce halation before them stay exactly as they were. */
#define WF_GRAIN_MAX 6               /* 0 off, 1 fine (0.6), 2 normal (1.0, default), 3 strong (1.5) - Yedlin;
                                        4..6 the same three amounts of the Genesis grain (WindyGrain) */
#define WF_GRAIN_GEN0 4              /* first Genesis choice */
#define WF_GRAIN_GEN(g) ((g) >= WF_GRAIN_GEN0)
#define WF_HALO_MAX  5               /* 0 off, 1 halation, 2 halation + bloom (default), 3 spektrafilm halation (the
                                        film's catalogue spk profile), 4 spektrafilm + bloom, 5 WindyHalation
                                        (the Genesis light wrap) */
#define WF_HALO_SPK(h) ((h) >= 3 && (h) <= 4)
#define WF_HALO_BLOOM(h) ((h) == 2 || (h) == 4)
#define WF_HALO_GEN(h) ((h) == 5)
#define WF_GRAIN_DEF WF_GRAIN_DEF_CAM   /* wf_cam.h (included by wf_core.h before any use): EOS M normal, 5D2 off */
#define WF_HALO_DEF  WF_HALO_DEF_CAM    /* wf_cam.h: EOS M halation + bloom, 5D2 off */
#define WF_MIST_MAX 8                /* Mist: 0 Off, 1..4 white 1/8 1/4 1/2 1, 5..8 black 1/8 .. 1 (WINDYFLM.CFG "mist") */
#define WF_MIST_DEF 0
extern volatile int wf_grain, wf_halo, wf_mist;

/* wf_shot.c is the ONE place that says which shots get our post (our colour pass, then these effects): the post code
 * (wf_core.h) is WF_POST_FILM + catalogue index for a film, WF_POST_LLUT for Log + LUT, 0 = none (wf_post_now; wf_post_want
 * = the same while it can run now: path up, Log + LUT tables loaded). */
#define WF_POST_FILM 0x100
/* wf_shot.c: the worker waits for wf_task (a film table, then effect keys) without holding anything wf_task waits for:
 * wf_ll_poll / fx_wait count a busy AND parked worker as idle and run wf_ws_poll in their 10 ms loops */
extern volatile int wf_parked;
void wf_ws_poll(void);
int  wf_aspect_has_halo(int aspect);                     /* wf_shot.c: halation (+ bloom) runs for this JPEG aspect
                                                            (3:2, or -1 = not reported): worker, wf_task, menu */

/* key of the effect state that is ready (published last by wf_task), 0 = none.
 * halation: (halation set + 1) | 64 with bloom; grain: (grain set + 1) | amount level << 6. The sets are the
 * catalogue's (wf_catalog.h wf_cat_hsets / wf_cat_gsets: profile or yg_stocks row, strength / amount scale,
 * rho_max), one per distinct film parameter set; set 0 is the + LUT modes' */
extern volatile int wf_fx_hk, wf_fx_gk, wf_fx_mk;   /* wf_fx_mk: mist level | 16 for a B&W film, 0 = none */
extern volatile int wf_fx_retry_ms;          /* "no memory": the same setting is tried again after this (10 s) */

/* wf_fx_state(): what the menu shows for one effect (NOLUT: a + LUT mode whose cube is not loaded) */
enum { WF_FXS_OFF = 0, WF_FXS_READY, WF_FXS_LOADING, WF_FXS_NOMEM, WF_FXS_UNUSED, WF_FXS_ASPECT, WF_FXS_PLAY,
       WF_FXS_INACTIVE, WF_FXS_NOLUT };

int  wf_fx_mode(void);                                   /* post code of the current output (wf_post_mode with the
                                                            loaded film), 0 = none */
int  wf_fx_keys(int post, int *hk, int *gk);
int  wf_fx_has_mist(int post);                           /* a post code that gets mist (any level; the Mist row is greyed otherwise) */
int  wf_fx_mkey(int post);                               /* wanted mist key (mist level), 0 = none */             /* wanted keys for a post code and the menu */
int  wf_fx_coming(int bit, int want);                    /* worker: wf_task is still to publish key want of effect
                                                            bit (not failed for memory, not in playback) */
void wf_fx_poll(void);                                   /* wf_task: load / switch / free */
int  wf_fx_state(int which);                             /* menu: WF_FXS_* of WF_FX_HAL or WF_FX_GRN */
const char *wf_fx_gstock_name(void);                     /* menu: grain stock of the current mode */

/* worker task only, between a ready key check and the end of the request (kind 0 NN, 1 TOP, 2 BOT) */
void wf_fx_begin(int kind, int fx, uint32_t seed);    /* NN / TOP: also the shot's exposure -> clipped gain (fx_hot) */
#define WF_EV_BAD (-9999)
int  wf_shot_ev8(int *src);                              /* wf_menu.c: EV100 x 8 of the shot, WF_EV_BAD; *src 'M' / 'L' / 'F' */
extern volatile int wf_fx_ev8, wf_fx_evsrc;              /* the last NN / TOP request's (log line " ev<EV><src>") */
void wf_fx_gather(const uint32_t *w, int nw, int x0, int y);
void wf_fx_build(int kind);
void wf_fx_apply(uint32_t *w, int nw, int x0, int y);
void wf_fx_grain(uint32_t *w, int nw, int x0, int y);
int  wf_fx_mist_begin(int kind);                         /* 1 = this frame gets mist (a BOT: the TOP's map is kept) */
void wf_fx_mist_gather(const uint32_t *w, int nw, int x0, int y);
void wf_fx_mist_build(int kind);
void wf_fx_mist_apply(uint32_t *w, int nw, int x0, int y);
void wf_fx_mist_geom(int W, int H);                      /* worker: the frame of this request, before wf_fx_mist_begin */
#endif
