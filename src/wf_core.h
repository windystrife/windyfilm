/* wf_core.h - WindyFilm core: internal declarations shared by the core files (not by the UI, which sees wf_api.h only).
 *
 *   wf_main.c      module entry (MODULE_INFO / CBRs / CONFIGS / strings), init / deinit, wf_task, the log, card reads
 *   wf_cam_*.c     camera input layer (one per build): ROM checks, hooks, gates; fills a struct wf_frame
 *   wf_shot.c      worker task, look resolution (struct wf_look), dispatch, chunk loop, effects hand-over, shot log,
 *                  Log + LUT tables, the WFFILMS.DAT pack reader
 *   wf_menu.c      state (output, film, rows), ML menu, WINDYFLM.CFG, EXIF UserComment, Light / Print, wf_api.h rows
 *   wf_pv.c        LiveView preview (display filter) and its tables
 *   wf_fx.c, wf_film.c, wf_loglut.c, wf_llk*.c, wf_logout*, wf_mist.c, yh_*, yg_*, wf_expsim.c: kernels and effects */
#ifndef WF_CORE_H
#define WF_CORE_H
#include <stdint.h>
#include <stddef.h>
#include "wf_api.h"
#include "wf_frame.h"
#include "wf_cam.h"
#include "wf_stagek.h"                           /* film-stage knob tables: struct wf_sg_knobs */
#include "wf_fio.h"                              /* card writes: wf_fopen_w, wf_fcreate, wf_fremove, wf_frename */

/* ------------------------------------------------------------------ DryOS / ML imports (both cameras' .sym have them)
 * A file that includes ML's own headers first (wf_menu.c, wf_pv.c) defines WF_ML_HEADERS and takes ML's prototypes. */
#ifndef WF_ML_HEADERS
extern int  get_ms_clock(void);
extern void msleep(int ms);
extern int  snprintf(char *b, size_t n, const char *fmt, ...);
extern void *memcpy(void *d, const void *s, size_t n);
extern void *memset(void *d, int c, size_t n);
extern void *__mem_malloc(size_t len, unsigned flags, const char *file, unsigned line);
extern void  __mem_free(void *p);
extern int  is_play_mode(void);
#endif
#define DRY_OK(h)   (((uint32_t)(h) & 1) == 0)   /* DryOS task / semaphore handles are even, errors odd and never 0 */
/* a file handle (ML's FIO_OpenFile / FIO_CreateFile): anything but 0, ML's error (it maps Canon's -1 to 0). DryOS file
 * handles are small numbers, odd as often as even (RAWB8 logged 3, 5, 7 as "refused"): never DRY_OK on a file, or a good
 * handle is taken for an error and left open (RAWB8's WFROLL.TXT: created empty, then never removable) */
#define WF_FH_OK(f) ((uint32_t)(uintptr_t)(f) != 0 && (uint32_t)(uintptr_t)(f) != 0xFFFFFFFFu)
#define DRY_SEM(h)  ((uint32_t)(h) != 0 && DRY_OK(h))
#define DRY_TIMEOUT 9
#define WF_MEM_DMA 1                              /* __mem_malloc flags (ML mem.h MEM_DMA / MEM_TEMPORARY) */
#define WF_MEM_TMP 2
#define WF_BARRIER() __asm__ __volatile__("" ::: "memory")

/* ------------------------------------------------------------------ outputs and the look */
enum { WF_OUT_FILM = 0, WF_OUT_LOG, WF_OUT_LLUT, WF_OUT_OFF, WF_NOUT };
#define WF_NCURVE 5                               /* log curves 1..5 (wf_loglut.h WF_LL_SLOG3 .. WF_LL_CLOG3) */
#define WF_CURVE_VLOG 2                           /* a Film LUT can's Load sets V-Log + LUT */
enum { WF_LIGHT_AUTO = 0, WF_LIGHT_DAY, WF_LIGHT_CLOUDY, WF_LIGHT_TUNGSTEN, WF_NLIGHT };
/* Log + LUT input curve (DESIGN 3.2 A1): the JPEG as sRGB, or through a measured Canon Picture Style curve ("Film 1"
   in User Def. 1, "Neutral"). The values are the wf_loglut.h input curve slots (wf_ll_incurve, WF_LL_NINC). */
enum { WF_INC_SRGB = 0, WF_INC_FILM1, WF_INC_NEUTRAL, WF_INC_N };
#define WF_NSTRENGTH 3                            /* Strength 30 / 100 / 125 % (level 0..2) */

/* effect post codes (wf_fx.c): which effect sets a shot uses. WF_POST_FILM + catalogue index for a film (its grain /
 * halation sets), WF_POST_LLUT for Log + LUT (set 0), 0 = no effects (plain log, Off, no look) */
#ifndef WF_POST_FILM
#define WF_POST_FILM 0x100
#endif
#define WF_POST_LLUT 1

/* the look of one shot, resolved once (NN / TOP request) from the state; a BOT half reuses its TOP's.
 * Independent of the input format: the worker dispatches on (frame.fmt, look.output). */
struct wf_look {
    uint8_t  output;                              /* WF_OUT_*, after resolution (Film without a film = WF_OUT_OFF + why) */
    uint8_t  curve;                               /* 1..5 for LOG / LLUT */
    uint8_t  flut;                                /* Film LUT cube 0..WF_FLUT_N - 1 (LLUT) */
    int8_t   lei;                                 /* LUT EI, 1/3 EV (LLUT) */
    int16_t  row;                                 /* catalogue index (wf_films[]): the film (FILM) or the loaded can (LLUT), -1 */
    uint8_t  variant;                             /* print variant WF_PRINT_* (FILM) */
    uint8_t  strength;                            /* level 0..2 (30 / 100 / 125 %) */
    uint8_t  grain, halo, mist;                   /* the settings as the menu counts them */
    uint8_t  bloom, gauge;                        /* Bloom 0..WF_BLOOM_MAX, Gauge 0..WF_GAUGE_MAX (rows since 11-15) */
    uint8_t  light;                               /* WF_LIGHT_* (WB itself is set by the menu, not by the worker) */
    uint8_t  why;                                 /* WF_OUT_OFF: 0 set Off, 1 no film loaded, 2 film without a table */
    uint8_t  incurve;                             /* WF_INC_*: how Log + LUT read the JPEG (LLUT) */
    int8_t   hl, sh;                              /* Highlights / Shadows -5..5 (FILM: in ftflags; LLUT: the tables'), else 0 */
    uint32_t stk;                                 /* film-stage knob key (wf_sg_key): 0 = the pack's table, else composed */
    int8_t   sexp, spush, sprn[3];                /* the knob rows the composed table used (stk != 0), for EXIF */
    uint8_t  scomp, siie;
    uint8_t  sbl, spbl, snn, snp;                 /* Bleach bypass, Print bleach, Neutral neg / print 0..4 (x 25 %) */
    int      post;                               /* effect post code (above) */
    int      ftflags;                             /* wf_film_build flags of the film (WF_FT_*) incl. strength */
};

/* ------------------------------------------------------------------ generated catalogue (tools/mk_catalog_c.py) */
struct wf_gset { uint8_t row; uint16_t scale_q8, rho_q8; };              /* yg_stocks row, amount Q8, rho_max Q8 (0 = row's) */
struct wf_hset { uint8_t prof, sprof; uint16_t hal_q8, shal_q8; };      /* yh profile + strength, Spektra profile + strength */
extern const int8_t   wf_cat_gset[];              /* per catalogue index: grain set, -1 none */
extern const int8_t   wf_cat_hset[];              /* halation set, -1 none */
extern const uint8_t  wf_cat_ftflags[];           /* WF_FT_V21 | WF_FT_MONO */
extern const uint8_t  wf_cat_lut[];               /* a can's Film LUT 1..WF_FLUT_N - 1, 0 = a film */
extern const int8_t   wf_cat_ev8[];               /* descriptor EV x 8 (catalogue ev8) */
extern const int16_t  wf_cat_lutrow[];            /* WF_FLUT_N entries: catalogue index of each Film LUT's can, -1 */
extern const struct wf_gset wf_cat_gsets[];
extern const struct wf_hset wf_cat_hsets[];
extern const int wf_cat_ngset, wf_cat_nhset;
#define WF_CAT_LUT_GSET 0                         /* Log + LUT: set 0 of both */
#define WF_CAT_LUT_HSET 0
int wf_cat_index(int id);                         /* wf_menu.c: catalogue index of id, -1 */

/* ------------------------------------------------------------------ wf_main.c */
extern volatile int wf_cam_ok;                    /* 1 = ROM words matched (wf_cam() reports the camera) */
extern volatile int wf_unloading;                 /* module deinit started: hooks return, task ends */
void wf_status_set(const char *s);                /* wf_status() text ("" = fine) */
void wf_gen_bump(void);                           /* wf_gen() + 1 (UI redraw) */
void wf_log_flush(void);                          /* wf_task: append the log ring to ML/LOGS/WINDYFLM.LOG */
int wf_job_state(void);
/* Roll + Darkroom (wf_dark.c, docs/DESIGN.md 3.6; the UI's part is wf_dark.h) */
void wf_dark_init(void);                          /* module init: the view, no allocation */
void wf_roll_poll(void);                          /* wf_task: roll file, requests, frames, RAW+L / review, HUD */
void wf_dark_poll(void);                          /* wf_task, after wf_roll_poll: a darkroom run asked for, carried out
                                                     here (minutes; no task of its own, owner RAWB9 "dark: no task (5)") */
void wf_task_yield(int log);                      /* wf_main.c, the darkroom run inside wf_task: the light part of its
                                                     loop (ML-menu guard, browser request; log 1 = the log to the card) */
void wf_dark_end(void);                           /* wf_task at unload: stop a run, Canon's values back */
enum { WF_LU_SHOTS = 1, WF_LU_NONE, WF_LU_DARK };
int  wf_look_use(void);                           /* what the look's tables are built for (wf_dark.c, owner RAWB9): shots
                                                     (no roll, or a roll with RAW develop on), none (a roll in the camera:
                                                     its frames keep Canon's JPEG), the darkroom run (effects, film table) */
int  wf_roll_shooting(void);                      /* a roll is loaded (takes frames; RAW develop B may run on them too) */
int  wf_dark_busy(void);                          /* a darkroom run lives (RAW develop B gives its shoot memory back) */
int  wf_roll_blocks_play(void);                   /* blind roll: PLAY is eaten */
int  wf_roll_notice_key(int what);                /* GUI task, a key: drops the end-of-roll notice if it is up (wf_task
                                                     clears it); what 1 (SET / Q) also opens the Darkroom, 2 (INFO) the
                                                     roll picker for a new roll. 1 = it was up */
int  wf_roll_shutter_key(void);                   /* GUI task, a half / full shutter press: 1 = eaten (the shutter is held
                                                     on a finished roll; the end notice shows at once) */
#define WF_KEY_PLAY 24                            /* ML module.h MODULE_KEY_PLAY */                           /* wf_menu.c: ML lens_info.job_state (Canon busy with a shot when != 0) */
int wf_iso_now(void);                             /* wf_menu.c: the ISO now (lens_info.iso, or iso_auto under Auto ISO) */
void wf_ui_request(void);                         /* menu: open the browser from wf_task after the ML menu closed */

/* ------------------------------------------------------------------ camera layer (wf_cam_eosm.c / wf_cam_5d2.c) */
int  wf_cam_init(void);                           /* module init: ROM word checks only (no hook); 1 = this body is supported */
int  wf_cam_start(void);                          /* wf_task: worker + hooks; 0 = installed */
#define WF_CAM_NOWORKER (-100)                    /* wf_cam_start: no worker task (yet): wf_task tries again */
void wf_cam_poll(void);                           /* wf_task: the camera layer's own log lines (rejects) */
int  wf_cam_shot_ok(void);                        /* the in-camera path is up (worker + main hook) */
int  wf_cam_gate_pv(void);                        /* preview: a shot now would get a look at all (JPEG L, no multishot) */
const char *wf_cam_name(void);                    /* "EOS M" / "5D2" */
extern volatile int wf_aspect;                    /* JPEG aspect (EOS M property), -1 not reported; 5D2 always 0 */
/* RAW probe P0 (wf_probe.c + the camera layer, DESIGN 3.2): log only, the frame is never touched; off at every boot,
   never saved. The menu entry is built where WF_PRB_CAN (wf_cam.h). */
enum { WF_PRB_OFF = 0, WF_PRB_ON, WF_PRB_NAV, WF_PRB_N };   /* NAV: also the camera's ROM call (EOS M: MEMNAVI) */
extern int wf_probe_mode;
int wf_prb_can(void);                             /* the camera layer set the probe up (body supported) */
extern volatile int wf_req_live;                  /* a shot is between its hook post and done (preview stays off) */

/* ------------------------------------------------------------------ wf_shot.c */
int  wf_worker_start(void);                       /* camera layer: semaphores + worker task; 0 = up */
int  wf_shot_run(struct wf_frame *f, int *status);/* camera layer (hook context): hand the frame to the worker, block until
                                                     done; 0 = the worker ran it (*status = WF_ST_*), -1 = busy / no worker */
int  wf_worker_up(void);
int  wf_yuv_active(void);                         /* the in-camera path is up and the module is not unloading */
const uint32_t *wf_ws_table(int row, int variant, uint32_t stk);   /* preview (wf_task): the published film table of
                                                     (row, variant) made for stage knob key stk (0 = the pack's), else 0 */
int  wf_sg_state(void);                           /* ML/DATA/WFSTAGE.DAT: 0 not read, 1 read, < 0 missing / bad */
int  wf_sg_has(int row, int variant);             /* the stage pack has this film-stage pair (its knob rows work) */
const struct wf_sg_kd *wf_sg_kdata(void);        /* the stage pack's knob data (version 2), 0 = none: the last four
                                                    knob rows hidden and read as 0 */
void wf_sg_knobs_now(struct wf_sg_knobs *k);      /* the knob rows as they stand (wf_menu.c), in range */
/* the RAW develop's knob tables (wf_rawb.c, wf_task): the halves WFSTAGE.DAT gives pair (row, variant), their blob
 * indices and checksums (1; 0 = no usable pair), and one half (0 negative, 1 print) read into mem (sizeof struct wf_sg_neg
 * / wf_sg_prn), checksum-checked: 0, -1 cannot open, -2 no memory, -3 no pack, -4 / -5 bad (the half is then marked bad,
 * as for the 8-bit composition: its films keep their pack tables) */
int  wf_sg_halves(int row, int variant, int *nb, int *pb, uint32_t *nfnv, uint32_t *pfnv);
int  wf_sg_read_half(int half, int idx, void *mem);
void wf_shot_poll(void);                         /* wf_task: shot log, EXIF entries, pack reader, Log + LUT, effects */
void wf_ws_poll(void);                            /* pack reader step (also inside the wait loops of wf_fx.c / wf_ll_poll) */
int  wf_ws_state(void);                           /* 0 index not read, 1 read, < 0 pack missing / bad */
int  wf_ws_has(int row, int variant);             /* the pack lists a good table for this film and variant */
int  wf_ws_listed(int row, int variant);          /* the pack's index lists it, whether the table verified or not */
int  wf_look_now(struct wf_look *lk);             /* the look a shot would get now (menu state); returns lk->output */
int  wf_post_now(void);                           /* the effect post code of the current state, 0 none */
int  wf_post_want(void);                          /* the same, when it can run now (path up, Log + LUT cube loaded) */
int  wf_ll_ready(int post);                       /* 1 = not Log + LUT, or its tables match the current curve / cube / EI */
int  wf_ll_state(int curve);                      /* menu: -2 loading, -1 not tried, else WF_LL_* of the last load */
int  wf_ll_have_curve(void);                      /* curve whose tables are published, 0 none */
void *wf_ll_tables(void);                         /* the published tables (preview reads them with wf_pv_busy set) */
const char *wf_status_name(int st);
int  wf_shot_pending(void);                       /* a TOP waits for its BOT (EXIF waits) */
void wf_shot_deinit(void);
extern volatile int wf_busy, wf_parked, wf_half_valid;
extern volatile int wf_half_h, wf_half_g, wf_half_m;
extern volatile int wf_pv_busy;
/* shot status (log, the 5D2 pairing) */
enum { WF_ST_NONE = 0, WF_ST_FILM, WF_ST_NOTAB, WF_ST_LOG, WF_ST_LLUT, WF_ST_NOLUT, WF_ST_OFF, WF_ST_NOFILM,
       WF_ST_GATE, WF_ST_ASPECT, WF_ST_NOSTAGE, WF_ST_RBSTOP, WF_ST_N };   /* RBSTOP: RAW develop stopped part way */
#define WF_ST_TOUCHED(st) ((st) == WF_ST_FILM || (st) == WF_ST_LOG || (st) == WF_ST_LLUT)

/* ------------------------------------------------------------------ wf_menu.c */
extern volatile int wf_out, wf_film_id, wf_print, wf_light, wf_curve, wf_flut, wf_lut_ei, wf_strength, wf_exif_off;
extern volatile int wf_incurve;
extern volatile int wf_hl, wf_sh;                 /* Highlights / Shadows, WF_HS_MIN..WF_HS_MAX (wf_film.h) */
extern volatile int wf_sg_exp, wf_sg_push, wf_sg_comp, wf_sg_prn[3], wf_sg_iie;   /* film-stage knob rows (wf_stagek.h) */
extern volatile int wf_sg_bl, wf_sg_pbl, wf_sg_nn, wf_sg_np;   /* Bleach bypass, Print bleach, Neutral neg / print 0..4 */
extern volatile int wf_cfg_dirty;
void wf_menu_init(void);                          /* module init: config read, menu added */
void wf_menu_deinit(void);                        /* unload: WB back, menu removed */
void wf_menu_poll(void);                          /* wf_task: config save, EXIF writer, Light */
void wf_cfg_load(void);
int  wf_lut_ei_t(void);                           /* LUT EI in 1/3 EV */
int  wf_hl_t(void);                               /* Highlights / Shadows in range (-5..5) */
int  wf_sh_t(void);
const char *wf_hs_text(int v);                    /* "-5" .. "0" .. "+5" (no %+d on the camera) */
int  wf_incurve_t(void);                          /* WF_INC_* in use: a Canon curve only when its table is built in
                                                     and the camera is on its Picture Style (or not known yet) */
int  wf_style_ok(int inc);                        /* Picture Style fits input curve `inc` (sRGB: any), or unknown */
enum { WF_PSW_OK = 0, WF_PSW_STYLE, WF_PSW_BASE, WF_PSW_CONTRAST };
int  wf_style_why(int inc);                       /* WF_PSW_*: what of the Picture Style differs from the measurement */
int  wf_style_base(void);                         /* User Def. 1 base (PROP_PICSTYLE_OF_USERDEF1), -1 = not known */
int  wf_style_con(void);                          /* the active style's Contrast, -10 = not known */
int  wf_style_con_of(int inc);                    /* the Contrast input curve `inc` was measured at */
int  wf_style_raw(void);                          /* PROP_PICTURE_STYLE value, 0 = not reported yet */
int  wf_strength_pct(int level);
void wf_exif_shot(const struct wf_look *lk, int g, int h, int b, int m, int ev);   /* a shot of ours: its text (ring) */
void wf_exif_none(void);                          /* a JPEG that got no look: keeps the ring aligned */
const char *wf_out_name(int out, int curve);
const char *wf_look_title(const struct wf_look *lk, char *b, int n);      /* "Portra 400 WS Scan", "V-Log + LUT Vision3 250D" */
#define WF_EV_NONE (-128)

/* ------------------------------------------------------------------ wf_pv.c */
extern volatile int wf_pv_mode;                   /* 0 Off, 1 On, 2 Fast */
extern volatile int wf_pv_gen;
#define WF_PV_OFF  0
#define WF_PV_ON   1
#define WF_PV_FAST 2
enum { WF_PV_READY = 0, WF_PV_NOLOOK, WF_PV_GATE, WF_PV_WAIT, WF_PV_NOMEM, WF_PV_MOVIE, WF_PV_PLAY };
int  wf_pv_state(void);
void wf_pv_poll(void);                            /* wf_task */
void wf_pv_deinit(void);
unsigned int wf_dispf(unsigned int ctx);          /* CBR_DISPLAY_FILTER */

/* ------------------------------------------------------------------ chunk helpers (wf_shot.c) */
void wf_copyw(uint32_t *d, const uint32_t *s, int n);
typedef void wf_words_fn(uint32_t *p, int n, int arg);
extern void wf_log_words(uint32_t *p, int n, int curve);   /* wf_logout_arm.S */

#endif
