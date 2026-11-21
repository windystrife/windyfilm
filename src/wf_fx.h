/* wf_fx.h - WindyStrife film effects at the shot (wf_fx.c): halation and bloom (one stage, yh_halo.c) and
 * Yedlin-principled grain (yg_grain.c). Shared by wf_shot.c (worker passes, wf_task poll),
 * wf_menu.c (rows, ML menu, WINDYFLM.CFG) and the host test. */
#ifndef WF_FX_H
#define WF_FX_H
#include <stdint.h>

#define WF_FX_HAL 1                  /* effect bits of one request / Diana shot */
#define WF_FX_GRN 2
#define WF_FX_MST 4                  /* Mist (wf_mist.c): diffusion filter on the camera's words, before our colour pass */

/* menu choices (wf_menu.c, WINDYFLM.CFG "grain" / "halation" / "bloom" / "gauge"); the browser rows show and change the
 * same values. The rows are Grain, Halation, Bloom and Gauge since 11-15 (Grain / Halation were "WindyGrain" /
 * "WindyHalation"). The last choice of Grain and of Halation is the Genesis Pro 1.6.6 model (DESIGN 1a): Genesis's own
 * grain (its amplitude tables, yg_gen_* in yg_grain.c, "Even") and the plugin's light wrap (yh_gen_* in yh_halo.c,
 * "Wrap"). The Yedlin grain and the per-film / 3-bounce halation before them stay as they were. */
/* Lab (11-21): the last choice of Grain, Halation and Bloom is the Lab model (yl_lab.c, docs/DESIGN.md 3.20): the
 * film-lab formulas and presets, one per gauge, the same for every film. A Lab value in the Halation or the Bloom row
 * makes the stage the Lab one: a WindyFilm value in the other row then does not run (WF_FXS_UNUSED, as Bloom with
 * Wrap). */
#define WF_GRAIN_MAX 7               /* 0 off, 1 fine (0.6), 2 normal (1.0, default), 3 strong (1.5) - Yedlin;
                                        4..6 the same three amounts of the Genesis grain (Even); 7 Lab */
#define WF_GRAIN_GEN0 4              /* first Genesis choice */
#define WF_GRAIN_LAB 7
#define WF_GRAIN_GEN(g) ((g) >= WF_GRAIN_GEN0 && (g) < WF_GRAIN_LAB)
#define WF_HALO_MAX  4               /* 0 off, 1 Wide (the film's catalogue profile, default), 2 Tight (the film's
                                        spektrafilm 3-bounce profile), 3 Wrap (the Genesis light wrap), 4 Lab */
#define WF_HALO_LAB 4
#define WF_HALO_SPK(h) ((h) == 2)
#define WF_HALO_GEN(h) ((h) == 3)
/* Bloom: its own row since 11-15, as on the darkroom branch (RAWB11); it was the "+ Glow" halation choices. A soft
 * glow in the light's own colour, run in the halation stage: either one on runs the stage, both off = no stage. The
 * light wrap has no bloom: with Halation Wrap the Bloom row keeps its value but does not run (WF_FXS_UNUSED). */
#define WF_BLOOM_MAX 4               /* 0 off, 1 low, 2 normal (the old "+ Glow"), 3 strong, 4 Lab */
#define WF_BLOOM_LAB 4
/* what runs of a Halation / Bloom pair (values in range): the stage is the Lab one when either is Lab */
#define WF_FX_LABSTAGE(h, b) ((h) == WF_HALO_LAB || (b) == WF_BLOOM_LAB)
#define WF_HALO_RUN(h, b)  (WF_FX_LABSTAGE(h, b) && (h) != WF_HALO_LAB ? 0 : (h))
#define WF_BLOOM_RUN(h, b) (WF_FX_LABSTAGE(h, b) ? ((b) == WF_BLOOM_LAB ? (b) : 0) : WF_HALO_GEN(h) ? 0 : (b))
/* Gauge (11-15): the film format the grain and the halation stage are sized for. 35 mm = the stages as they were (the
 * grain pitch of a 36 mm frame, the halation profiles as fitted); 16 mm = the same film on a frame half as wide (grain
 * twice the size and twice the RMS in pixels, halo and bloom twice as wide), 65 mm = 1.85x as wide (finer, tighter).
 * The ratios 0.5 / 1 / 1.85 are Genesis's film_gauge (DESIGN 1a); the light wrap takes them as its own gauge. */
/* 8 mm (11-21) is the Lab presets' fourth gauge: the WindyFilm models take it as 16 mm (WF_GAUGE_CLASSIC). The menu shows
 * the gauges in the order 8, 16, 35, 65 mm (wf_menu.c). */
#define WF_GAUGE_MAX 3               /* 0 16 mm, 1 35 mm (default), 2 65 mm, 3 8 mm */
#define WF_GAUGE_DEF 1
#define WF_GAUGE_8   3
#define WF_GAUGE_CLASSIC(v) ((v) == WF_GAUGE_8 ? 0 : (v))
#define WF_GAUGE_Q8(v) ((v) == 0 || (v) == WF_GAUGE_8 ? 128 : (v) == 2 ? 474 : 256)
#define WF_GAUGE_LAB(v) ((v) == WF_GAUGE_8 ? 0 : (v) + 1)   /* the Lab preset (yl_lab.h order 8 / 16 / 35 / 65 mm) */
#define WF_GRAIN_DEF WF_GRAIN_DEF_CAM   /* wf_cam.h (included by wf_core.h before any use): EOS M normal, 5D2 off */
#define WF_HALO_DEF  WF_HALO_DEF_CAM    /* wf_cam.h: EOS M Wide, 5D2 off */
#define WF_BLOOM_DEF WF_BLOOM_DEF_CAM   /* wf_cam.h: EOS M normal, 5D2 off */
#define WF_MIST_MAX 8                /* Mist: 0 Off, 1..4 white 1/8 1/4 1/2 1, 5..8 black 1/8 .. 1 (WINDYFLM.CFG "mist") */
#define WF_MIST_DEF 0
extern volatile int wf_grain, wf_halo, wf_bloom, wf_gauge, wf_mist;
#define WF_FX_BLM 8                  /* wf_fx_state only: the Bloom row (its stage is WF_FX_HAL's) */

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
 * halation stage: (halation set + 1) | halation 0..3 << 6 | bloom 0..3 << 8 | gauge << 10, when halation or bloom
 * runs; the Lab stage (halation set + 1) | 4096 Lab halation | 8192 Lab bloom | gauge 0..3 << 10. Grain: (grain set + 1) |
 * amount level 1..3 << 6 | 256 for the Genesis grain | gauge << 9; the Lab grain (grain set + 1) | 2048 | gauge 0..3 << 9.
 * The WindyFilm models take 8 mm as 16 mm in their keys (WF_GAUGE_CLASSIC). The sets are the
 * catalogue's (wf_catalog.h wf_cat_hsets / wf_cat_gsets: profile or yg_stocks row, strength / amount scale,
 * rho_max), one per distinct film parameter set; set 0 is the + LUT modes' */
extern volatile int wf_fx_hk, wf_fx_gk, wf_fx_mk;   /* wf_fx_mk: mist level | 16 for a B&W film, 0 = none */
#define WF_FXK_HALO(k)   (((k) >> 6) & 3)            /* a halation-stage key's halation choice (WF_HALO_*) */
#define WF_FXK_BLOOM(k)  (((k) >> 8) & 3)            /* its bloom level */
#define WF_FXK_HGAUGE(k) (((k) >> 10) & 3)           /* its gauge */
#define WF_FXK_GGAUGE(k) (((k) >> 9) & 3)            /* a grain key's gauge */
#define WF_FXK_GLAB(k)   (((k) >> 11) & 1)           /* a grain key of the Lab grain */
#define WF_FXK_HLAB(k)   (((k) >> 12) & 1)           /* a halation-stage key with the Lab halation */
#define WF_FXK_BLAB(k)   (((k) >> 13) & 1)           /* with the Lab bloom (either one: the Lab stage) */
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
int  wf_fx_state(int which);                             /* menu: WF_FXS_* of WF_FX_HAL, WF_FX_BLM, WF_FX_GRN or WF_FX_MST */
const char *wf_fx_gstock_name(void);                     /* menu: grain stock of the current mode */

/* worker task only, between a ready key check and the end of the request (kind 0 NN, 1 TOP, 2 BOT) */
/* NN / TOP: also the shot's exposure -> clipped gain (fx_hot): ev8 = WF_EV_LIVE takes the camera's (wf_shot_ev8), a
 * Darkroom frame passes its CR2's own (wf_dk_ev8, log source 'R'; WF_EV_BAD when the CR2 does not tell) */
void wf_fx_begin(int kind, int fx, uint32_t seed, int ev8);
#define WF_EV_BAD (-9999)
#define WF_EV_LIVE (-9998)
int  wf_shot_ev8(int *src);                              /* wf_menu.c: EV100 x 8 of the shot, WF_EV_BAD; *src 'M' / 'L' / 'F' */
extern volatile int wf_fx_ev8, wf_fx_evsrc;              /* the last NN / TOP request's (log line " ev<EV><src>") */
void wf_fx_gather(const uint32_t *w, int nw, int x0, int y);
void wf_fx_build(int kind);
int  wf_fx_lab(void);                                    /* worker, under a published key: the halation stage is the Lab one
                                                            (it gathers after the colour pass, not before) */
int  wf_fx_two(void);                                    /* the Lab stage with both glows: a read-only gather2 pass + build2 */
void wf_fx_gather2(const uint32_t *w, int nw, int x0, int y);
void wf_fx_build2(int kind);
void wf_fx_apply(uint32_t *w, int nw, int x0, int y);
void wf_fx_grain(uint32_t *w, int nw, int x0, int y);
int  wf_fx_mist_begin(int kind);                         /* 1 = this frame gets mist (a BOT: the TOP's map is kept) */
void wf_fx_mist_gather(const uint32_t *w, int nw, int x0, int y);
void wf_fx_mist_build(int kind);
void wf_fx_mist_apply(uint32_t *w, int nw, int x0, int y);
void wf_fx_mist_geom(int W, int H);                      /* worker: the frame of this request, before wf_fx_mist_begin */
#endif
