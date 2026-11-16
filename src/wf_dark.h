/* wf_dark.h - WindyFilm Roll + Darkroom: what the core (src/wf_dark.c) offers the UI (src/ui/wf_dark_ui.c) and the
 * menu. docs/DESIGN.md 3.6. WindyStrife's own code.
 *
 * Threads: wf_task owns the roll (frame detection, the roll file) and runs the darkroom itself (wf_dark_start only asks
 * for a run; wf_task's next loop carries it out and comes back when the roll is developed or stopped: no task of its
 * own, DryOS's table is full while the UI is up, owner RAWB9 "dark: no task (5)"); the UI task only reads the snapshots
 * below and posts requests. Snapshots are plain structs rewritten by their owner; the UI re-reads them every tick and
 * redraws when gen changes (a torn read shows one odd frame at worst, never a crash: pointers inside stay valid while
 * the UI is open). */
#ifndef WF_DARK_H
#define WF_DARK_H
#include <stdint.h>
#include "wf_roll.h"

/* ------------------------------------------------------------------ roll (wf_task) */
extern volatile int wf_blind;                    /* menu "Blind roll": 1 (default) = image review off and PLAY blocked
                                                    while a roll is in the camera (loaded, full or rewound) and no darkroom run */
const struct wf_roll *wf_roll_now(void);         /* the current roll (never 0) */
uint32_t wf_roll_gen(void);                      /* bumped when the roll changes */
/* UI / menu requests (wf_task carries them out while Canon is idle; the result shows in wf_roll_now):
   0 = queued, -1 = a roll with frames is open, -2 = bad film / count, -3 = busy (a request is pending),
   -4 = no darkroom on this camera yet (5D2: load refused) */
int  wf_roll_req_load(int film, int exp);
int  wf_roll_req_rewind(void);
int  wf_roll_req_close(void);
/* close the finished roll (FULL, or REWOUND with frames: they stay on the card, kept undeveloped) and load a new one in
   one request (the roll picker's close-and-load question, owner 11-15); codes as wf_roll_req_load, -1 = no finished roll */
int  wf_roll_req_swap(int film, int exp);
/* owner 11-15: no shot on a finished roll. 1 (FULL) / 2 (REWOUND with frames to develop) while wf_task holds Canon's
   shutter through ML's UI lock, 0 free (no roll, still shooting, closed, developing, movie mode, recording) */
int  wf_roll_shutter_locked(void);
int  wf_roll_hud(char *line, int n);             /* the HUD text ("ROLL 021  Windy 400  5/12"), 0 = no roll */

/* ------------------------------------------------------------------ darkroom (inside wf_task: no task of its own) */
enum { WF_DR_IDLE = 0, WF_DR_RUN, WF_DR_STOPPING, WF_DR_STOPPED, WF_DR_DONE, WF_DR_FAIL, WF_DR_N };
enum { WF_DP_NONE = 0, WF_DP_LOAD, WF_DP_EXPOSE, WF_DP_FIX, WF_DP_DRY, WF_DP_MIX, WF_DP_N };   /* per frame: read +
                                                    decode the CR2, develop (B) into the frame, look stage (film,
                                                    halation, grain), encode + write the JPEG; MIX = before the first
                                                    frame (frame -1): the look's effects built for the run */
#define WF_DR_PW    288                          /* the print being developed: luma, 5184 x 3456 / 18 */
#define WF_DR_PH    192
#define WF_DR_TW    72                           /* prints on the wire: luma, 5184 x 3456 / 72 */
#define WF_DR_TH    48
#define WF_DR_NWIRE 8                            /* the last prints kept for the wire */

struct wf_dark_view {
    int      state;                              /* WF_DR_* */
    int      phase;                              /* WF_DP_* of the frame in work */
    int      frame;                              /* its index on the roll (0-based), -1 = none */
    int      done, total;                        /* frames developed in this roll / on the roll */
    int      left;                               /* frames still to develop (this one included) */
    int      prog;                               /* progress of this frame, 0..1000 (all phases) */
    int      expose;                             /* 0..1000 how far the paper is exposed (the develop rows): the print
                                                    fades in with it */
    int      t_frame;                            /* ms in this frame */
    int      avg_ms;                             /* mean ms per frame so far (0 = none yet: the UI says ~45 s) */
    int      print_ready;                        /* 0 nothing yet, 1 a quick preview from the raw, 2 the developed print */
    uint8_t *print;                              /* WF_DR_PW x WF_DR_PH luma */
    int      nwire;                              /* prints on the wire (<= WF_DR_NWIRE), newest last */
    uint8_t *wire[WF_DR_NWIRE];                  /* WF_DR_TW x WF_DR_TH luma each */
    int      wire_frame[WF_DR_NWIRE];            /* their frame indices */
    int      hung_ms;                            /* get_ms_clock() when the newest print was hung (slide animation) */
    int      battery;                            /* % or -1 unknown */
    const char *msg;                             /* why it stopped / failed, "" otherwise */
    uint32_t gen;                                /* bumped on every change worth a redraw */
};
const struct wf_dark_view *wf_dark_view(void);   /* never 0 */
int  wf_dark_start(void);                        /* asks wf_task for a run (it rewinds a loaded roll); 0 = started, -1 =
                                                    nothing to develop, -2 = already running, -3 = not now (Canon busy,
                                                    no worker yet, battery) */
void wf_dark_stop(void);                         /* MENU: stop after the current band (the frame is done again next run) */
int  wf_dark_eta_s(const struct wf_dark_view *v);   /* seconds left for the roll (estimate) */
int  wf_dark_src(int output, int flut);          /* EOS M: the WF_DEV_* tables a run builds for look output (WF_OUT_*) +
                                                    Film LUT, as B (Leica Vivid WF_DEV_DISP, cubes WF_DEV_CUBE); 0 = none */

/* print finishes (docs/DESIGN.md 3.9, src/wf_print.h): a frame with the shot's details under the picture, a date stamp.
   Settings of the menu / config / the Darkroom's rows (WF_PFR_*, WF_PIN_*, WF_PDT_* of wf_print.h; defaults Off, Info
   On, Off): a run takes them at its start, so the prints of one run match. Frame Off and Date Off = today's print.
   wf_pr_jpeg (owner 11-16): WF_PJ_OVER = each print also replaces the shot's own JPEG in DCIM (after the roll copy is
   written and read back; the CR2 is never touched), so Canon's playback shows the print; default WF_PJ_KEEP. */
extern volatile int wf_pr_frame, wf_pr_info, wf_pr_date, wf_pr_jpeg;
enum { WF_PJ_KEEP = 0, WF_PJ_OVER, WF_PJ_N };
enum { WF_PO_FRAME = 0, WF_PO_INFO, WF_PO_DATE, WF_PO_JPEG, WF_PO_N };   /* the Darkroom's print rows (wf_menu.c) */
const char *wf_popt_name(int k);                 /* "Frame", "Info", "Date", "Camera JPEG" */
const char *wf_popt_text(int k, char *b, int n); /* the value as shown: "White wide", "On + roll", "'26 11 15" (today) */
int  wf_popt_avail(int k);                       /* 0 = greyed: Info with the frame Off */
void wf_popt_step(int k, int d);                 /* LEFT / RIGHT / wheel (wraps); the config is saved later */
int  wf_clock_ymd(void);                         /* the camera clock's date as y * 10000 + m * 100 + d, -1 = unknown */

/* ------------------------------------------------------------------ UI */
#define WF_UI_SCR_ROLL 1                         /* = WFUI_SCR_ROLL / WFUI_SCR_DARK of src/ui/wf_ui_int.h */
#define WF_UI_SCR_DARK 2
void wf_ui_open_scr(int scr);                    /* open the UI on a screen (WF_UI_SCR_*), wf_ui.c */
/* the HUD over LiveView (src/ui/wf_dark_ui.c; wf_task draws it while a roll is in the camera): wfdr_hud_fonts(1) loads
   the font pack once (0 frees it, 2 reads it again; 1 back = the read may pass later: card busy), wfdr_hud draws into ML's bitmap with ML's own colour indices; blink = the "wind on"
   cue. mode: FULL = roll + film chip, big counter, ladder (2.5 s); FADE = the same, dimmed and dithered (0.4 s);
   SMALL = only a small dim counter; CLEAR = SMALL after clearing the rectangles FULL drew (to 0, LiveView shows) */
enum { WF_HUD_FULL = 0, WF_HUD_FADE, WF_HUD_SMALL, WF_HUD_CLEAR };
int  wfdr_hud_fonts(int on);
void wfdr_hud(uint8_t *vram, int pitch, int blink, int mode);
/* the end-of-roll notice (owner 11-15): a box in the middle while a roll is FULL or REWOUND; mode FULL / FADE (dimmed,
   dithered) / CLEAR (its box back to 0). wf_dark.c times it: 5 s when the roll ends, 2.5 s when LiveView comes back */
void wfdr_notice(uint8_t *vram, int pitch, int mode);
int  wf_clock_hm(void);                          /* the camera clock as h * 100 + m, -1 = unknown (wf_dark.c) */

#endif
