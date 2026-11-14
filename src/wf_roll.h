/* wf_roll.h - WindyFilm "Roll + Darkroom" (docs/DESIGN.md 3.5), WindyStrife's own code.
 *
 * A roll: one film, 12 / 24 / 36 exposures, shot RAW + L with no image review; afterwards the in-camera Darkroom
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
 *   exp=12                  12 / 24 / 36
 *   state=loaded            empty | loaded | full | rewound | done
 *   seen=100,205            DCIM folder and file number Canon had when the roll was loaded / the last frame
 *   f=100,206,S             one line per frame: folder, IMG number, state S shot / D developed / E failed / K kept
 *   ...
 *   end
 */
#ifndef WF_ROLL_H
#define WF_ROLL_H
#include <stdint.h>

#define WF_ROLL_MAXEXP  36
#define WF_ROLL_FILE    1536                     /* bytes of WFROLL.TXT: 36 frame lines of <= 15 chars + header */
#define WF_ROLL_PATH    "ML/SETTINGS/WFROLL.TXT"

enum { WF_RS_EMPTY = 0, WF_RS_LOADED, WF_RS_FULL, WF_RS_REWOUND, WF_RS_DONE, WF_RS_N };
#define WF_RF_SHOT  'S'                          /* frame states */
#define WF_RF_DEV   'D'
#define WF_RF_ERR   'E'
#define WF_RF_KEPT  'K'                          /* the roll was closed without developing this frame (CR2 untouched) */

struct wf_roll {
    int      num;                                /* roll number 1..999; 0 = never loaded a roll */
    int      film;                               /* catalogue id */
    int      exp;                                /* 12 / 24 / 36 */
    int      state;                              /* WF_RS_* */
    int      nshot;                              /* frames on the roll */
    int      seen_folder, seen_file;             /* Canon's DCIM folder / file number last looked at */
    uint16_t folder[WF_ROLL_MAXEXP];             /* per frame: DCIM folder number (100..999) */
    uint16_t file[WF_ROLL_MAXEXP];               /* per frame: IMG number (1..9999) */
    char     fst[WF_ROLL_MAXEXP];                /* per frame: WF_RF_* */
};

static inline int wf_roll_ok_exp(int e) { return e == 12 || e == 24 || e == 36; }
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

/* ------------------------------------------------------------------ file text (pure) */
int  wf_roll_text(const struct wf_roll *r, char *buf, int n);   /* WF_ROLL_FILE bytes into buf (n >= WF_ROLL_FILE); returns
                                                                    WF_ROLL_FILE, or -1 */
int  wf_roll_parse(struct wf_roll *r, const char *buf, int n);  /* 0 = ok (r filled), -1 = not a roll file (r reset) */

/* ------------------------------------------------------------------ names (pure) */
/* the CR2 of frame i: "<dcim>/<folder>CANON/<prefix><nnnn>.CR2" made from Canon's current DCIM dir (its last component
 * names the folder: the number is replaced by the frame's) */
int  wf_roll_cr2(const struct wf_roll *r, int i, const char *dcim_dir, const char *prefix, const char *ext, char *out, int n);
/* card root of a DCIM dir ("B:/DCIM/100CANON" -> "B:/") */
int  wf_roll_root(const char *dcim_dir, char *out, int n);
/* the darkroom's output: "<root>ROLLS", "<root>ROLLS/ROLL021", "<root>ROLLS/ROLL021/F01_0206.JPG" */
int  wf_roll_dir(const struct wf_roll *r, const char *root, int level, char *out, int n);
int  wf_roll_out(const struct wf_roll *r, int i, const char *root, char *out, int n);

#endif
