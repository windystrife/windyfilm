/* wf_roll.h - WindyFilm "Roll + Darkroom" (docs/DESIGN.md 3.6), WindyStrife's own code.
 *
 * A roll: one film, 3 / 6 / 12 / 24 / 36 exposures, shot RAW + L with no image review; afterwards the in-camera Darkroom
 * develops each frame's CR2 from the card with the roll's film (src/wf_dark.c: own CR2 decoder wf_cr2.c -> RAW develop
 * B wf_dev.c -> the look stage of wf_shot.c -> own JPEG encoder wf_jpe.c) into ROLLS/ROLLnnn/Fnn_xxxx.JPG.
 *
 * This file is the pure part: the roll's state, its text file, the state machine and the path names. No ML call, no
 * file access, no allocation: the host test (tests/host/wf_roll_test.c) links it alone. The card glue (the roll file
 * through src/wf_fio.h, frame detection from Canon's file number, the darkroom task) is src/wf_dark.c.
 *
 * Roll file ML/SETTINGS/WFROLL.TXT: plain text, always WF_ROLL_FILE bytes (padded with spaces before the last '\n'),
 * created once while Canon is idle and afterwards rewritten in place at offset 0 (an open of an existing file never
 * changes the card directory, trap W24):
 *   WFROLL 1
 *   roll=21                 roll number shown as ROLL 021 (1..999, +1 for every new roll)
 *   film=57                 catalogue film id
 *   exp=12                  3 / 6 / 12 / 24 / 36 (3 and 6: owner 11-15, a short test roll)
 *   state=loaded            empty | loaded | full | rewound | done
 *   dev=2                   only on a develop again (owner 11-17): the develop pass (absent = 1, the first)
 *   own=57                  only on a develop again: the roll's own film (film= is the one this pass prints with)
 *   seen=100,205            DCIM folder and file number Canon had when the roll was loaded / the last frame
 *   f=100,206,S             one line per frame: folder, IMG number, state S shot / D developed / E failed / K kept
 *   ...
 *   end
 *
 * The shelf (owner 11-17, DESIGN 3.6 "The shelf"): every roll that leaves the camera with frames, and every developed
 * roll, has a copy of its roll file beside its prints, <root>ROLLS/ROLL021/WFROLL.TXT; a develop again prints into a
 * folder of its own with its own roll file, ROLLS/ROLL021B/ (pass 2), ROLL021C/ (pass 3) .. ROLL021Z: the card's
 * names are 8.3, so "ROLL021_2" (9 letters) is not one. The list is built from these files (wf_dark.c).
 */
#ifndef WF_ROLL_H
#define WF_ROLL_H
#include <stdint.h>

#define WF_ROLL_MAXEXP  36
#define WF_ROLL_FILE    1536                     /* bytes of WFROLL.TXT: 36 frame lines of <= 15 chars + header */
#define WF_ROLL_PATH    "ML/SETTINGS/WFROLL.TXT"
#define WF_ROLL_SHELF   "WFROLL.TXT"             /* the shelf copy, in the roll's ROLLS folder */
#define WF_ROLL_MAXDEV  26                       /* develop passes: 1 (ROLL021) .. 26 (ROLL021Z) */

enum { WF_RS_EMPTY = 0, WF_RS_LOADED, WF_RS_FULL, WF_RS_REWOUND, WF_RS_DONE, WF_RS_N };
#define WF_RF_SHOT  'S'                          /* frame states */
#define WF_RF_DEV   'D'
#define WF_RF_ERR   'E'
#define WF_RF_KEPT  'K'                          /* the roll was closed without developing this frame (CR2 untouched) */

struct wf_roll {
    int      num;                                /* roll number 1..999; 0 = never loaded a roll */
    int      film;                               /* catalogue id */
    int      exp;                                /* 3 / 6 / 12 / 24 / 36 */
    int      state;                              /* WF_RS_* */
    int      nshot;                              /* frames on the roll */
    int      seen_folder, seen_file;             /* Canon's DCIM folder / file number last looked at */
    int      dev;                                /* develop pass: 0 / 1 the first (ROLLS/ROLL021), 2.. a develop again */
    int      own;                                /* a develop again: the roll's own film (0 = film is it) */
    uint16_t folder[WF_ROLL_MAXEXP];             /* per frame: DCIM folder number (100..999) */
    uint16_t file[WF_ROLL_MAXEXP];               /* per frame: IMG number (1..9999) */
    char     fst[WF_ROLL_MAXEXP];                /* per frame: WF_RF_* */
};

static inline int wf_roll_ok_exp(int e) { return e == 3 || e == 6 || e == 12 || e == 24 || e == 36; }
const char *wf_roll_state_name(int st);          /* "empty", "loaded", ... */

/* ------------------------------------------------------------------ state machine (pure) */
void wf_roll_reset(struct wf_roll *r);           /* never loaded */
/* load a new roll (film, exp) with Canon at folder / file now. Allowed when the roll is empty / done, or loaded with no
 * frame yet (then the same roll number is reloaded with the new film / count). 0 = ok, -1 = a roll with frames is open,
 * -2 = bad exp */
int  wf_roll_load(struct wf_roll *r, int film, int exp, int folder, int file);
/* Canon's file number moved to folder / file: add each new frame that has a CR2 (has_cr2(folder, n) = 1). Called by the
 * card glue only while Canon is idle. Returns the frames added. Numbers wrap 9999 -> 1 with the next folder. */
int  wf_roll_seen(struct wf_roll *r, int folder, int file, int (*has_cr2)(void *ctx, int folder, int file), void *ctx);
int  wf_roll_rewind(struct wf_roll *r);          /* loaded with >= 1 frame, or full -> rewound; 0 = ok, -1 = nothing to rewind */
int  wf_roll_next_dev(const struct wf_roll *r);  /* index of the next frame the darkroom develops, -1 = none */
int  wf_roll_left(const struct wf_roll *r);      /* frames still to develop */
int  wf_roll_ndev(const struct wf_roll *r);      /* frames developed (D) */
void wf_roll_mark(struct wf_roll *r, int i, char st);   /* D / E; the roll becomes done when nothing is left */
int  wf_roll_close(struct wf_roll *r);           /* give up: frames left -> K, state done; 0 = ok, -1 = no open roll */
int  wf_roll_can_shoot(const struct wf_roll *r); /* loaded and not full: the HUD counts, PLAY is blocked (blind roll) */
int  wf_roll_open(const struct wf_roll *r);      /* loaded / full / rewound: a roll is on the camera */
/* a shelf roll the Darkroom develops (it left the camera, or it was stopped): a loaded / full roll is rewound, a frame
   still to develop whose CR2 is gone (has_cr2 = 0) becomes K. Returns the frames to develop */
int  wf_roll_shelf_prep(struct wf_roll *r, int (*has_cr2)(void *ctx, int folder, int file), void *ctx);
/* develop again (owner 11-17): r (any pass of the roll) becomes its next pass printed with film: dev + 1, own = the
   roll's own film, every frame S again when its CR2 is there (else K), rewound (done when none is). Returns the frames
   to develop; -1 = the last pass (WF_ROLL_MAXDEV) was made already, or not a roll with frames */
int  wf_roll_again(struct wf_roll *r, int film, int (*has_cr2)(void *ctx, int folder, int file), void *ctx);

/* ------------------------------------------------------------------ file text (pure) */
int  wf_roll_text(const struct wf_roll *r, char *buf, int n);   /* WF_ROLL_FILE bytes into buf (n >= WF_ROLL_FILE); returns
                                                                    WF_ROLL_FILE, or -1 */
int  wf_roll_parse(struct wf_roll *r, const char *buf, int n);  /* 0 = ok (r filled), -1 = not a roll file (r reset) */

/* ------------------------------------------------------------------ names (pure) */
/* the CR2 of frame i: "<dcim>/<folder>CANON/<prefix><nnnn>.CR2" made from Canon's current DCIM dir (its last component
 * names the folder: the number is replaced by the frame's) */
int  wf_roll_cr2(const struct wf_roll *r, int i, const char *dcim_dir, const char *prefix, const char *ext, char *out, int n);
/* the second camera JPEG of Camera JPEG "Over + print" (owner 11-17, DESIGN 3.9 "Camera JPEG"): from the shot's own
 * JPEG ".../DCIM/100CANON/IMG_0208.JPG", ext in place of its extension. folder 0 (config jpeg2=prefix): the shot's
 * folder and file number with our letters, ".../DCIM/100CANON/WFP_0208.<ext>", dir ".../DCIM/100CANON"; folder 1
 * (jpeg2=folder): a folder of the same number with our letters, ".../DCIM/100WFILM/WFP_0208.<ext>", dir
 * ".../DCIM/100WFILM". The length; -1 = not a DCF name (4 letters + 4 digits + "." + 3 in an NNN + 5 letters folder),
 * Canon's letters are ours (the two files would be one name), or no room */
#define WF_J2_LETTERS "WFP_"
#define WF_J2_SUFFIX  "WFILM"
int  wf_roll_jpg2(const char *jpg, int folder, const char *ext, char *dir, int dn, char *out, int n);
/* card root of a DCIM dir ("B:/DCIM/100CANON" -> "B:/") */
int  wf_roll_root(const char *dcim_dir, char *out, int n);
/* the darkroom's output: "<root>ROLLS", "<root>ROLLS/ROLL021", "<root>ROLLS/ROLL021/F01_0206.JPG"; a develop again
   ROLLS/ROLL021B/...; level 2 = the shelf copy "<root>ROLLS/ROLL021/WFROLL.TXT" */
int  wf_roll_dir(const struct wf_roll *r, const char *root, int level, char *out, int n);
int  wf_roll_out(const struct wf_roll *r, int i, const char *root, char *out, int n);
/* RAW after Darkroom "Move to ROLLS" (owner 11-17, DESIGN 3.9): where a frame's CR2 goes once its camera JPEG is
   written, "<root>ROLLS/ROLL021/IMG_0206.CR2": Canon's name, the roll's first folder for every develop pass. The
   length; -1 = not a roll number (1..999) / file number (0..9999), or no room */
int  wf_roll_cr2_moved(int num, int file, const char *root, const char *prefix, char *out, int n);
/* a roll's folder name: "ROLL021" (pass 1), "ROLL021B" (pass 2) .. into out (>= 12 bytes) */
void wf_roll_name(int num, int dev, char *out);
/* a ROLLS entry's name back: "ROLL021" -> 21, pass 1; "roll021b" -> 21, pass 2; 0 = ok, -1 = not a roll folder */
int  wf_roll_name_parse(const char *s, int *num, int *dev);

/* ------------------------------------------------------------------ the film's speed (pure, owner 11-17) */
/* Canon's raw ISO code for a film's box speed (ISO 100 = 72, 8 codes a stop): the step nearest by ratio among those
 * from lo to hi (raw codes); third 1: the 1/3 stops 100, 125, 160 .. 12800 .. 25600, third 0: whole stops only (100,
 * 200, 400 ..). The ISO of that step in *iso (may be 0). Returns 0 for no speed (<= 0) or no step in lo..hi */
int  wf_roll_iso(int speed, int lo, int hi, int third, int *iso);
int  wf_roll_iso_of(int code);                  /* the ISO of a 1/3-stop code, 0 = not one (Auto is code 0) */
/* the quick keys (owner 11-17, wf_menu.c): one ISO step from code in direction dir (> 0 up) among the 1/3 stops
 * (third 1) or whole stops (third 0) inside lo..hi; Auto (0): up gives the lowest step, down stays Auto; at an end of
 * the range: code */
int  wf_roll_iso_step(int code, int dir, int lo, int hi, int third);
/* one aperture step from code (> 0) in direction dir (> 0: a bigger f-number) on the 1/3-stop grid (code % 8 = 0, 3, 5)
 * or the 1/2-stop grid (half 1: 0, 4) inside lo..hi (the lens' widest and smallest; an end off the grid counts too);
 * at an end: code */
int  wf_roll_av_step(int code, int dir, int lo, int hi, int half);
int  wf_roll_av_f10(int code);                  /* the f-number x 10 of an aperture code (8 = f/1.0), 0 = none */

#endif
