/* wf_rawb.h - RAW develop "B" in the shot pipeline (WindyStrife).
 *
 * Menu "RAW develop" (Off / On (slow), off at every boot, never saved). With it on, the camera layer hands an EOS M
 * RAW+L single shot to the worker as WF_FMT_RAW14 (wf_frame.h): Canon's finished UYVY frame plus the candidate raw
 * buffers. The worker (wf_rawb_stage, from wf_dispatch):
 *   1. which buffer holds this shot: wf_rs_pick against Canon's frame (wf_rsrc.c); none = the normal 8-bit look;
 *   2. black from the masked columns, white from the clip pile-up (wf_dev_levels), else the camera layer's values;
 *   3. wf_dev_prepare (WB fit on Canon's frame, highlight map, matrix, the film shoulder), then wf_dev_rows over the
 *      frame in 64-row bands, sleeping 10 ms between bands (wf_task and Canon's GUI run meanwhile) and stopping at
 *      unload, power off (ml_shutdown_requested), half-shutter held 1.5 s, or 90 s: the rows not developed stay Canon's.
 * The look's own stage then runs on the developed frame: films (sRGB base develop) get wf_film_words + effects, Log +
 * LUT (the cube develop) only the effects, plain log nothing.
 *
 * Tables: wf_task builds the look's develop tables (wf_dev_build, file I/O there) whenever the look changes, into one
 * shoot-memory block with the per-shot scratch (about 0.8 MB, shoot_malloc_suite_contig, cacheable, wf_fx.c rules),
 * given back while Canon playback is shown and when the menu is off. Published like the Log + LUT tables: wf_task
 * clears the key, the worker marks itself inside the stage first and reads the key after. Cube tables (Log + LUT, Leica
 * Vivid) are kept in ML/DATA/<cube name>.RBT (356 KB, wf_llb_*: builder id, look key, cube size / time / content hash):
 * a matching file is read instead of the parse, any other is rebuilt and rewritten (only while Canon is idle; a busy
 * write is tried again every 2 s while the tables stay). No shoot memory beyond the 0.8 MB block.
 *
 * Log: one line per shot that asked for B (wf_task): the buffer and its score, levels, WB source, the shoulder, the
 * time per phase, or why the shot got the 8-bit look. */
#ifndef WF_RAWB_H
#define WF_RAWB_H
#include <stdint.h>
struct wf_frame;
struct wf_look;

extern volatile int wf_rawb;                     /* menu: 0 Off, 1 On (slow) */
enum { WF_RB_OFF = 0, WF_RB_NOCAM, WF_RB_IDLE, WF_RB_PLAY, WF_RB_LOADING, WF_RB_READY, WF_RB_NOMEM, WF_RB_ERR };
int  wf_rawb_can(void);                          /* this camera build can develop the raw (EOS M) */
int  wf_rawb_state(void);                        /* menu: WF_RB_* */
int  wf_rawb_err(void);                          /* WF_RB_ERR: the WF_LL_* code of the table build */
/* worker: develop f (WF_FMT_RAW14) into f->dst. 1 = developed, 0 = not run (dst is still Canon's frame: the caller runs
   the 8-bit look), -1 = stopped part way (the caller does nothing more to the frame) */
int  wf_rawb_stage(struct wf_frame *f, const struct wf_look *lk);
/* worker: a shot that asked for B but did not get it (the camera layer's gate, or a look B cannot do) */
enum { WF_RBW_OK = 0, WF_RBW_NOTAB, WF_RBW_BUF, WF_RBW_PREP, WF_RBW_LOOK, WF_RBW_FILMTAB, WF_RBW_CAM, WF_RBW_TOP,
       WF_RBW_CHG, WF_RBW_N };            /* TOP / CHG: a 5D2 BOT half that cannot continue its TOP */
void wf_rawb_note(const struct wf_frame *f, int why);
void wf_rawb_poll(void);                         /* wf_task: tables, memory, the progress box, the shot's log line */
void wf_rawb_end(void);                          /* wf_task at unload: waits up to 2 s for a develop to stop, frees */
int  wf_rawb_busy(void);                         /* a shot's develop runs (the darkroom does not start then) */
int  wf_rawb_holds(void);                        /* the tables + scratch block is held, or a develop runs */
/* the .RBT of a look's cube tables (src WF_DEV_CUBE / WF_DEV_DISP of wf_dev.h, cube = wf_loglut_name's path) as B keys
 * it: path (WF_LL_NAME bytes) and key (WF_RBT_NKEY words) for wf_llb_read / wf_llb_write (wf_loglut.h) with a struct
 * wf_dev_tabs blob, so the darkroom (wf_dark.c) reads what B wrote and B what a run wrote. 0 = filled, -1 = no cache for
 * these tables (SRGB / LOG, a bad name, no cube id, no B on this camera). Hashes the cube (file I/O, Canon idle). */
#define WF_RBT_NKEY 9
int  wf_rawb_rbt_key(const struct wf_look *lk, int src, const char *cube, char *path, uint32_t *key);

#endif
