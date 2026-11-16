/* wf_api.h - WindyFilm: the frozen interface between the core (src/*.c), the browser UI (src/ui/*.c) and the
 * generated catalogue (films/catalog.py -> tools/mk_catalog_c.py -> build/wf_catalog.c).
 * Change it only together with docs/DESIGN.md (section 4), and tell every side. */
#ifndef WF_API_H
#define WF_API_H
#include <stdint.h>

/* ------------------------------------------------------------------ cameras */
enum { WF_CAM_NONE = 0, WF_CAM_EOSM = 1, WF_CAM_5D2 = 2 };
int wf_cam(void);                         /* core: the camera this build is for, WF_CAM_NONE when the init checks failed */

/* ------------------------------------------------------------------ catalogue (generated, read only) */
enum { WF_GRP_COLOUR = 0, WF_GRP_CINE, WF_GRP_BW, WF_GRP_LUT, WF_GRP_STAGE, WF_NGROUPS };   /* STAGE: the film-stage chain films */

#define WF_F_READY    0x01                /* a film with a table in films/tables, or a Film LUT can with a cube name */
#define WF_F_LUTCAN   0x02                /* Film LUT can: output V-Log + LUT with the can's cube, no 17^3 table */
#define WF_F_BW       0x04                /* black and white */
#define WF_F_SLIDE    0x08                /* reversal stock */

#define WF_PRINT_SCAN   0                 /* print variants = table variants in WFFILMS.DAT */
#define WF_PRINT_CINE   1
#define WF_PRINT_PAPER  2
/* 3..18: the chain films' sixteen print stocks in the plugin's UI order (the catalogue's "p00".."p15", 2302 first).
 * Kept AFTER the three catalogue variants so a shipped film keeps bits 0..2 and its pack entries do not change. A chain
 * film sets 3..18 (3513D, 17, never: no table) and wf_print_eff falls back to WF_PRINT_CHAIN_DEF when the film has it,
 * else to its first available variant. */
#define WF_PRINT_STOCK0 3
#define WF_NPRINTS      19
/* a chain film's default print (Print = Scan, or a print it lacks): 2383, the plain cinema print. The owner's own
 * session used 2393, whose table is the same; 2302 (the first slot) is the B&W print. */
#define WF_PRINT_CHAIN_DEF (WF_PRINT_STOCK0 + 1)

struct wf_film {
    uint8_t  id;                          /* stable id (the old catalogue numbering 26..127 is kept; the config stores it) */
    uint8_t  group;                       /* WF_GRP_* */
    uint8_t  flags;                       /* WF_F_* */
    uint8_t  can;                         /* entry in WFCANS.DAT, 255 = none (the UI draws a plain can) */
    uint16_t iso;                         /* box speed shown on the chip */
    uint16_t kelvin;                      /* Light "Day" white balance for this stock */
    uint16_t order;                       /* strip order inside its group, ascending */
    uint32_t prints;                      /* bit v set = print variant v exists (see WF_NPRINTS, 19 variants) */
    const char *code;                     /* <= 8 chars: table code (films/tables/<code>.T17) */
    const char *name;                     /* <= 24 chars: full name (title line) */
    const char *shortname;                /* <= 11 chars: strip label */
    const char *line1, *line2;            /* short description, <= 44 chars each */
    const char *kind;                     /* chip 1: "COLOUR NEG", "SLIDE", "B&W NEG", "CINE NEG", "FILM LUT" */
    const char *format;                   /* chip 3: "35mm", "120", "V-Log" */
};
extern const struct wf_film wf_films[];
extern const int wf_nfilms;
const struct wf_film *wf_film_by_id(int id);   /* 0 when unknown */

/* ------------------------------------------------------------------ state the browser shows and changes (core) */
/* WF_ROW_PAPER (owner 11-16): the print's paper, Glossy / Matte / Warm / Cool, right under Print stock */
enum { WF_ROW_LIGHT = 0, WF_ROW_PRINT, WF_ROW_PAPER, WF_ROW_GRAIN, WF_ROW_HALO, WF_ROW_BLOOM, WF_ROW_GAUGE, WF_ROW_MIST,
       WF_ROW_STRENGTH, WF_ROW_HL, WF_ROW_SH, WF_ROW_FLUT,
       WF_ROW_SEXP, WF_ROW_SPUSH, WF_ROW_SCOMP, WF_ROW_SPR, WF_ROW_SPG, WF_ROW_SPB, WF_ROW_SIIE,   /* film-stage knobs */
       WF_ROW_SBL, WF_ROW_SPBL, WF_ROW_SNN, WF_ROW_SNP,   /* Bleach bypass, Print bleach, Neutral neg / print */
       WF_NROWS };

const char *wf_row_name(int row);         /* "Light", "Print stock", "Paper", "Grain", "Halation", "Bloom", "Gauge", "Mist",
                                             "Strength", "Highlights", "Shadows", "Film LUT", "Film exp", "Push / pull",
                                             "Exp comp", "Printer R", "Printer G", "Printer B", "Interlayer"; the last
                                             choice of Grain and of Halation is the Genesis Pro 1.6.6 model, the seven
                                             film-stage knob rows move that chain's own settings on the film-stage films
                                             (docs/DESIGN.md 1a; available only with ML/DATA/WFSTAGE.DAT) */
const char *wf_row_text(int row);         /* the current value as text: "Day", "Scan", "Normal", "Wrap", "Off", "100%", "+2",
                                             ... (Highlights / Shadows: "-5" .. "0" .. "+5", no wrap at the ends; Film exp
                                             "-2" .. "+0.3" .. "+2", Push / pull "Pull 1" .. "Push 1", Printer "-12" ..
                                             "+12", Interlayer "0%" .. "200%", none of them wraps) */
const char *wf_row_text_long(int row);    /* the same, with a print stock by its full name ("Kodak Vision Color 2383"); the
                                             ML menu's value field is too narrow for it, the WindyFilm UI row is not */
int  wf_row_avail(int row);               /* 0 = has no effect with the current output or camera: the UI greys it */
int  wf_row_step(int row, int delta);     /* delta -1 / +1; 1 = the value changed (and was saved) */
int  wf_row_level(int row, int *max);     /* bar rows (Strength): the level 0..*max */
/* the same four for film id as SET would load it (Film for a film, its cube's Log + LUT for a can; the loaded film or an
   unknown id: as above). The WindyFilm UI's rows follow the film under the cursor with these, before SET (owner 11-16):
   a film-stage film lights its knob rows, a can greys Print stock, and Print stock shows and steps that film's prints.
   The values stay the module's own (one set for all films), so a row set before SET is the film's look once loaded. */
const char *wf_row_text_for(int row, int id);
const char *wf_row_text_long_for(int row, int id);
int  wf_row_avail_for(int row, int id);
int  wf_row_step_for(int row, int delta, int id);

enum { WF_LOAD_OK = 0, WF_LOAD_NOTABLE, WF_LOAD_NOCUBE, WF_LOAD_BUSY, WF_LOAD_UNKNOWN };
int  wf_load(int id);                     /* make film id the active look: output Film, or V-Log + LUT for a can */
int  wf_loaded(void);                     /* the active film id, 0 = none (output is plain log, Log + LUT or Off) */
int  wf_avail(int id);                    /* 1 = loadable now (its table is in the pack / its cube is on the card) */
int  wf_films_state(void);                /* the film tables: 1 read, 0 starting (worker / pack not up yet), -1 the
                                             worker or the hooks failed, -2 the pack is missing or bad */
const char *wf_status(void);              /* one line, "" when all is well: "Unsupported firmware", "Disable filmroll.mo", ... */
uint32_t wf_gen(void);                    /* bumped on every state change: the UI redraws when it differs */
/* the develop paths (owner 11-17: the Info page's "Last develop", the EXIF "Mode"): JPEG = the look on Canon's JPEG,
 * JPEG 8-bit = the same for a Film stage film, RAW develop = B at the shot, Darkroom = a roll's print */
enum { WF_PATH_NONE = 0, WF_PATH_JPEG, WF_PATH_JPEG8, WF_PATH_RAW, WF_PATH_DARK, WF_PATH_N };
int wf_dev_last(int *path);               /* ms of the last develop (0 = none yet), *path its WF_PATH_* */
const char *wf_path_name(int path);       /* "JPEG", "JPEG 8-bit", "RAW develop", "Darkroom" ("" for none) */

/* ------------------------------------------------------------------ browser UI (src/ui) */
void wf_ui_init(void);                    /* module init: no allocation, no file access */
int  wf_ui_open(void);                    /* menu entry / open key; 0 = opened */
void wf_ui_close(void);
int  wf_ui_is_open(void);
unsigned int wf_ui_key(unsigned int key); /* from the core's CBR_KEYPRESS while open: 0 = consumed, 1 = pass on */
int  wf_ui_open_key(unsigned int key);    /* 1 = this key opens the browser (EOS M: long DOWN; 5D2: TRASH) */
void wf_ui_open_refused(unsigned int key, int play);   /* the open key did not open it: log why */
int  wf_ui_menu_guard(void);              /* EOS M, every ~10 ms (wf_task's waits): the ML menu's Canon dialog; 1 = asked */

/* ------------------------------------------------------------------ core services */
void wf_log(const char *fmt, ...);        /* ML/LOGS/WINDYFLM.LOG (buffered, written by the core task) */
int  wf_card_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size);   /* 0 = ok, else WF_RD_* */
/* wf_card_read results. MISSING only when the card answers for AUTOEXEC.BIN (the file ML booted from) but not for
 * this file; when it answers for neither, Canon holds the card (boot, a shot being written): BUSY. */
enum { WF_RD_OK = 0, WF_RD_MISSING = -1, WF_RD_NOMEM = -2, WF_RD_OPEN = -3, WF_RD_IO = -4, WF_RD_SHORT = -5,
       WF_RD_BUSY = -6 };

#endif
