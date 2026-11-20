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
void wf_blind_set(int on);                       /* the ML menu or the browser's Blind roll row: set it, the config saves it */
const struct wf_roll *wf_roll_now(void);         /* the current roll (never 0) */
uint32_t wf_roll_gen(void);                      /* bumped when the roll changes */
/* UI / menu requests (wf_task carries them out while Canon is idle; the result shows in wf_roll_now):
   0 = queued, -1 = a roll with frames is open, -2 = bad film / count, -3 = busy (a request is pending),
   -4 = no darkroom on this camera yet (5D2: load refused) */
int  wf_roll_req_load(int film, int exp);
int  wf_roll_req_rewind(void);
int  wf_roll_req_close(void);
/* the finished roll (FULL, or REWOUND with frames) leaves the camera and a new one loads, in one request (the
   close-and-load question, owner 11-15; the browser's Darkroom mode since 11-17). Owner 11-17: it goes on the shelf
   (below) rewound, its frames waiting for the Darkroom; devd = 1 (a Film stage roll developed at the shot: RAW develop /
   JPEG 8-bit) closes it first (frames K) and shelves it developed. Codes as wf_roll_req_load, -1 = no finished roll */
int  wf_roll_req_swap(int film, int exp, int devd);
/* owner 11-15: no shot on a finished roll. 1 (FULL) / 2 (REWOUND with frames to develop) while wf_task holds Canon's
   shutter through ML's UI lock, 0 free (no roll, still shooting, closed, developing, movie mode, recording) */
int  wf_roll_shutter_locked(void);
int  wf_roll_hud(char *line, int n);             /* the HUD text ("ROLL 021  Windy 400  5/12"), 0 = no roll */
/* one screen (owner 11-17): the browser's mode chip leaves Darkroom with a roll on the camera. Its frames are never lost:
   a roll with frames is rewound first (the UI asks), then set aside ("shelved") for the Darkroom. A shelved roll (FULL or
   REWOUND, or LOADED until wf_task has rewound it) keeps its frames and its film for PLAY -> Darkroom, but the camera
   shoots in the chosen mode meanwhile: the shutter is free, the look is built, PLAY and image review are Canon's, no end
   notice. New shots are not frames (wf_roll_seen counts only a LOADED roll). RAM only: cleared by a roll load / close /
   close-and-load, when the roll is developed or closed, by wf_roll_shelve(0) (the chip back on Darkroom) and at boot. */
int  wf_roll_shelve(int on);                     /* 0 = done, -1 = no roll on the camera (on = 1) */
int  wf_roll_shelved(void);                      /* 1 = the roll on the camera is set aside */
/* the shelf (owner 11-17: "next roll" no longer drops a roll; develop again; DESIGN 3.6 "The shelf"): the rolls on the
   card, each from its roll file in its ROLLS folder (wf_roll.h), newest first (down from the camera's roll number), the
   camera's own open roll left out (the Darkroom lists it first itself). wf_task lists the card at boot and when asked
   (Canon idle, no run); a roll leaves the camera onto it (close-and-load, a close, a developed roll). EOS M only: the
   5D2's list stays empty (no rolls there). */
#define WF_SHELF_MAX 16                          /* rolls listed; older ones are logged ("not listed") */
struct wf_shelf_ent {
    int16_t num, dev;                            /* roll number; its newest develop pass (1 = ROLLS/ROLL021, 2 = ROLL021B) */
    int16_t film, own;                           /* the pass's film; the roll's own film (own = film on a first pass) */
    int16_t exp;
    int8_t  state, nshot, left, ndev;            /* WF_RS_*; frames; still to develop (S); developed (D) */
};
struct wf_shelf {
    int      n;                                  /* entries */
    int      more;                               /* rolls on the card past the list */
    uint32_t gen;                                /* bumped when the list is made again */
    struct wf_shelf_ent e[WF_SHELF_MAX];
};
const struct wf_shelf *wf_shelf_now(void);       /* never 0 */
void wf_shelf_rescan(void);                      /* any task: list the card again (the Darkroom screen asks on entry) */

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
    int      num, dev, film;                     /* the run's roll (the last run's after it): number, pass, film */
};
const struct wf_dark_view *wf_dark_view(void);   /* never 0 */
int  wf_dark_start(void);                        /* asks wf_task for a run (it rewinds a loaded roll); 0 = started, -1 =
                                                    nothing to develop, -2 = already running, -3 = not now (Canon busy,
                                                    no worker yet, battery) */
/* a shelf roll (num, wf_shelf_now): its frames left (again = 0), or develop it again (again = 1) with film (0 = the
   roll's own) into its next pass's folder, with the PRINT settings of now (owner 11-17); codes as wf_dark_start */
int  wf_dark_start_roll(int num, int again, int film);
void wf_dark_stop(void);                         /* MENU: stop after the current band (the frame is done again next run) */
int  wf_dark_eta_s(const struct wf_dark_view *v);   /* seconds left for the roll (estimate) */
int  wf_dark_eta_left(const struct wf_dark_view *v, int left);   /* seconds for left frames (the Darkroom's pick) */
int  wf_dark_src(int output, int flut);          /* EOS M: the WF_DEV_* tables a run builds for look output (WF_OUT_*) +
                                                    Film LUT, as B (Leica Vivid WF_DEV_DISP, cubes WF_DEV_CUBE); 0 = none */

/* print finishes (docs/DESIGN.md 3.9, src/wf_print.h): a frame with the shot's details under the picture, a date stamp.
   Settings of the menu / config / the Darkroom's rows (WF_PFR_*, WF_PIN_*, WF_PDT_* of wf_print.h; defaults Off, Info
   On, Off): a run takes them at its start, so the prints of one run match. Frame Off and Date Off = today's print.
   wf_pr_jpeg (owner 11-16): WF_PJ_OVER = each print also replaces the shot's own JPEG in DCIM (after the roll copy is
   written and read back; the CR2 is never touched), so Canon's playback shows the print; default WF_PJ_KEEP.
   With a frame the picture (the look alone) and the print (framed) differ, and Canon's playback shows one JPEG a shot
   (owner RAWB28, 20-11: two JPEGs in one DCIM folder do not both preview), so one goes to DCIM, the other to ROLLS:
   WF_PJ_BOTH "Over + print" = the shot's JPEG becomes the picture alone at 5184 x 3456 (as Overwrite did up to
   RAWB23), the print stays the roll copy in ROLLS (always written first: nothing more is copied); WF_PJ_PRINT "Print +
   over" (RAWB28) = the shot's JPEG becomes the print fitted to 5184 x 3456 (as Overwrite), and the picture alone is
   added to ROLLS: ROLLS/ROLLnnn/IMG_nnnn.JPG (the camera JPEG's own name) beside the print F01_nnnn.JPG and, moved,
   the CR2 IMG_nnnn.CR2 (a RAW + JPEG pair again).
   Frame Off: one picture, both are Overwrite. (RAWB24-27 put the print into DCIM as WFP_nnnn.JPG; wf_pr_jpg2, config
   jpeg2=prefix|folder, is still read, not used.)
   wf_pr_cr2 (owner 11-17, menu "RAW after Darkroom", config cr2=keep|move; default WF_PC_KEEP): WF_PC_MOVE = with Camera
   JPEG Overwrite or Over + print, once a frame's JPEG(s) are written and read back its CR2 moves, bytes unchanged, to
   ROLLS/ROLLnnn/ (wf_roll_cr2_moved), so Canon's PLAY finds only our JPEG; the shelf and develop again read it there.
   Keep, or a frame without a camera JPEG (RAW only): the CR2 stays in DCIM. */
extern volatile int wf_pr_frame, wf_pr_info, wf_pr_date, wf_pr_jpeg, wf_pr_jpg2, wf_pr_cr2;
enum { WF_PJ_KEEP = 0, WF_PJ_OVER, WF_PJ_BOTH, WF_PJ_PRINT, WF_PJ_N };
enum { WF_J2_PREFIX = 0, WF_J2_FOLDER, WF_J2_N };
enum { WF_PC_KEEP = 0, WF_PC_MOVE, WF_PC_N };
enum { WF_PO_FRAME = 0, WF_PO_INFO, WF_PO_DATE, WF_PO_JPEG, WF_PO_RAW, WF_PO_N };   /* the Darkroom's print rows (wf_menu.c) */
const char *wf_popt_name(int k);                 /* "Frame", "Info", "Date", "Camera JPEG", "RAW files" */
const char *wf_popt_text(int k, char *b, int n); /* the value as shown: "White wide", "On + roll", "'26 11 15" (today) */
int  wf_popt_avail(int k);                       /* 0 = greyed: Info with the frame Off, RAW files with Camera JPEG Keep */
void wf_popt_step(int k, int d);                 /* LEFT / RIGHT / wheel (wraps); the config is saved later */
int  wf_clock_ymd(void);                         /* the camera clock's date as y * 10000 + m * 100 + d, -1 = unknown */

/* Screen Off while a roll develops (owner 11-16, DESIGN 3.11): one chip at the Darkroom screen's top right, the only
   place it changes (config dkscreen=on|off, kept silently). WF_DS_ON (default) = a run keeps the LCD on, as before;
   WF_DS_OFF = while a run lives the display is turned off WF_DS_WAKE_MS after the last key on the Darkroom screen; a key
   while it is dark only wakes it; the run's end (done, stopped, failed) turns it on. EOS M (the 5D2 has no Darkroom). */
extern volatile int wf_dk_scr;
/* a JPEG 8-bit roll (owner 11-17, the browser's Film stage section, DESIGN 3.14): 1 = each frame of the roll in the camera
   gets the look on Canon's JPEG at the shot (wf_look_use = WF_LU_SHOTS) and still counts on the roll; 0 = as before
   (Canon's own JPEG, the Darkroom prints it later; RAW develop on = B at the shot). The browser sets it with the roll's
   mode; config rolllook=on|off keeps it over a reboot */
extern volatile int wf_roll_look8;
void wf_roll_look8_set(int on);                  /* the browser (GUI task): set it, the config saves it */
enum { WF_DS_ON = 0, WF_DS_OFF, WF_DS_N };
#define WF_DS_WAKE_MS 10000
void wf_dk_scr_step(int d);                      /* the chip's LEFT / RIGHT / wheel: On <-> Off; the config is saved later */
int  wf_dark_wake(int now);                      /* the UI, on each key of the Darkroom screen: 1 = a Screen Off run's
                                                    display was dark (it is turned on): the key does nothing else */
int  wf_dark_keep(int now, int ui);              /* Screen On: the UI's tick (ui 1) and the run's band yields (ui 0):
                                                    a display Canon turned off mid-frame is turned on again, every 1 s */
int  wf_dark_dark(int now);                      /* the UI's tick on the Darkroom screen: turns the display off when a
                                                    Screen Off run is due dark; 1 = it reads dark (redraws can wait) */

/* the Darkroom's photos (owner RAWB28, 20-11: "preview ảnh lúc chọn darkroom", src/wf_photo.h): the picked roll's
   frames from their CR2s' small RGB, read a frame at a time while the Darkroom screen is idle (no run). The easel shows
   them as a contact sheet (focus on the chip, the ROLL or the Film row) or in the print sketch (focus on a PRINT row:
   the frame the run prints first). wf_dk_photo (config dkphoto=on|off, default on): INFO on the Darkroom screen
   switches it; off = the drawn sketch, as before RAWB28 */
extern volatile int wf_dk_photo;
void wf_dk_photo_set(int on);                    /* the UI: set it, the config saves it */
#define WF_DK_SHEET 36                           /* a contact sheet: 6 strips of 6 (the longest roll) */
enum { WF_PHS_WAIT = 0, WF_PHS_OK, WF_PHS_NONE };   /* a frame's photo: not read yet, read, none (no CR2 / not one) */
struct wf_dark_photos {
    int      num, cam;                           /* the roll asked for (cam 1 = the camera's roll) */
    int      n;                                  /* frames on the sheet (0 = nothing to show: the sketch) */
    uint8_t  st[WF_DK_SHEET];                    /* WF_PHS_* */
    int      cw, ch;                             /* a frame's size on the sheet: WF_PH_LW x WF_PH_LH up to WF_PH_LMAX
                                                    frames, else WF_PH_CW x WF_PH_CH (wf_photo.h) */
    uint8_t *cell;                               /* n x cw x ch luma, as stored (a portrait frame lies on its side, as
                                                    on a contact print) */
    int      big_frame, big_st;                  /* the frame the run prints first, WF_PHS_* */
    int      big_w, big_h;                       /* its picture the right way up: 288 x 192, or 128 x 192 */
    uint8_t *big;
    uint32_t gen;                                /* bumped as photos come in */
};
/* the UI: the roll it shows, asked each tick while shown (wf_task reads while asked: a frame's state is set once its
   cell is there, big_st once the picture is); never 0 (n = 0 until a sheet starts) */
const struct wf_dark_photos *wf_dark_photos(int num, int cam);

/* ------------------------------------------------------------------ UI */
/* wf_ui_open_scr (wf_ui.c): open the UI there, or switch an open UI to it. WF_UI_NEW_ROLL = the browser in Darkroom mode
   on its Exposures row, the way to a new roll (the ML menu's Film roll, the end-of-roll notice's INFO; owner 11-17: the
   old Roll picker screen is gone); WF_UI_SCR_DARK = the Darkroom screen; anything else = the browser */
#define WF_UI_NEW_ROLL 1
#define WF_UI_SCR_DARK 2
void wf_ui_open_scr(int scr);
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
/* the quick keys' box (owner 11-17, wf_menu.c times it): bottom middle of LiveView, the value big ("ISO 400", "f/2.8",
   "MAGIC ZOOM"), under it up to two hints, each "[KEY] text" (k given) or plain grey text (k 0). mode FULL / FADE /
   CLEAR; each call first gives the last box's pixels back (0); vram 0: forget the last box (the screen went to someone
   else). wfdr_toast_fonts(1 / 0) holds the font pack for it (the UI and the roll HUD share it). wf_dark_toast (wf_dark.c,
   wf_task) puts it into ML's bitmap: CLEAR also brings Canon's LiveView marks back (redraw) and the roll HUD over them at
   the next pass; mode -1 forgets the box */
int  wfdr_toast_fonts(int on);
void wfdr_toast(uint8_t *vram, int pitch, const char *big, const char *k1, const char *t1, const char *k2, const char *t2,
                int mode);
void wf_dark_toast(const char *big, const char *k1, const char *t1, const char *k2, const char *t2, int mode);
int  wf_clock_hm(void);                          /* the camera clock as h * 100 + m, -1 = unknown (wf_dark.c) */

#endif
