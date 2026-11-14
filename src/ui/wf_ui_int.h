/* wf_ui_int.h - WindyFilm browser internals shared by src/ui/*.c (not part of wf_api.h).
 *
 * Screen: the ML bitmap, 720 x 480, 8-bit palette indices, drawn into Canon's idle (back) buffer and copied to the
 * front buffer in one go (wf_gfx.c). Palette: the browser owns hardware entries WFUI_PAL_BASE .. 254 while it is
 * open (ML itself uses 0 .. 0x4F, 255 is Canon's ClearScreen) and gives them back on close. */
#ifndef WF_UI_INT_H
#define WF_UI_INT_H
#include <stdint.h>
#include "wf_ui_ml.h"
#include "../wf_api.h"

#define WFUI_W          720
#define WFUI_H          480

/* ------------------------------------------------------------------ palette layout (offsets from WFUI_PAL_BASE) */
#define WFUI_PAL_BASE   0x50
#define WFUI_PAL_END    0xFF            /* exclusive: 255 stays Canon's */

enum {                                  /* fixed UI colours (our scheme, DESIGN 5) */
    C_TXT = 0, C_TXT2, C_TXT3,
    C_AMBER, C_AMBERD, C_TEAL, C_TEALD,
    C_GRN, C_CHIPF, C_CHIPO, C_LINE, C_SEP, C_BAROFF,
    C_STRIP, C_HOLE, C_FRAME, C_FADE1, C_FADE2, C_TRACK, C_KEYO, C_WARN,
    C_NFIXED
};
#define WFUI_NBG        6               /* background gradient refs (y 0 .. 479), ordered-dithered between */
#define WFUI_NRING      12              /* glow levels 0 .. 11 (0 = background), dithered between */
#define WFUI_NGREF      3               /* glow background refs at y = 0, 150, 300 */
#define WFUI_NSHL       3               /* shadow levels 1 .. 3 (dithered), over glow rings 0 .. 3 */
#define WFUI_NSHR       4
#define C_BG0           (C_NFIXED)
#define C_GLOW0         (C_BG0 + WFUI_NBG)                              /* [ref][ring - 1] */
#define C_SHAD0         (C_GLOW0 + WFUI_NGREF * (WFUI_NRING - 1))      /* [ring][level - 1] */
#define C_CAN0          (C_SHAD0 + WFUI_NSHR * WFUI_NSHL)
#define WFUI_NPAL       (WFUI_PAL_END - WFUI_PAL_BASE)
#define WFUI_CAN_SLOTS  (WFUI_NPAL - C_CAN0)                            /* hardware entries left for can colours */
#define C_BGLAST        (C_BG0 + WFUI_NBG - 1)

#define PX(c)           ((uint8_t)(WFUI_PAL_BASE + (c)))

/* ------------------------------------------------------------------ wf_gfx.c: canvas, primitives, palette */
extern uint8_t *wfg_cv;                 /* top-left of the 720 x 480 drawing area (idle buffer), pitch wfg_pitch */
extern int wfg_pitch;
extern const uint8_t wfg_bayer[4][4];

int  wfg_begin(void);                   /* fetch the idle buffer; 0 = ok */
void wfg_present(void);                 /* write a dirty palette, copy idle -> front (under ML's BMP lock) */
void wfg_clear_front(void);             /* make the front buffer transparent (before Canon gets it back) */

void wfg_rect(int x0, int y0, int x1, int y1, int c);                         /* inclusive, palette offset */
void wfg_rrect(int x0, int y0, int x1, int y1, int r, int fill, int outline, int width);   /* -1 = none */
void wfg_ellipse(int x0, int y0, int x1, int y1, int c);
void wfg_poly(const int16_t *xy, int n, int c);                               /* convex, edges included */
void wfg_tri(int x0, int y0, int x1, int y1, int x2, int y2, int c);
void wfg_bits16(const uint8_t *bits, int x, int y, int c);                    /* 16 x 16 1-bit icon */
void wfg_background(int y0, int y1);    /* gradient + glow + floor shadow rows y0 .. y1 - 1 */
void wfg_fade_edges(int y0, int y1);    /* strip edge fades (both sides, 40 px) */
void wfg_scrim(int x0, int y0, int x1, int y1);   /* checkerboard dim outside a box (INFO overlay) */
void wfg_glow_centre(int cx, int cy, int shx, int shy);   /* where wfg_background puts the glow and the floor shadow
                                                             (call before wfp_set_glow) */

/* palette */
void wfp_init_fixed(void);              /* fixed colours + background refs into the RGB table */
void wfp_set_glow(const uint8_t rgb[3]);/* glow rings + shadow for one film (only these entries change) */
void wfp_set_rgb(int entry, int r, int g, int b);
int  wfp_take(void);                    /* read Canon's black / white / opacity, write ours; 0 = ok */
void wfp_keepalive(int force);          /* rewrite ours if Canon / ML replaced them */
void wfp_restore(void);                 /* give Canon its entries back */
void wfp_log(int can_used);             /* can_used: can colours in use (pack or plain cans) */

/* ------------------------------------------------------------------ wf_font.c: text */
enum { F_ML23 = 0, F_ML28, F_15, F_13, F_13C, F_NFONTS };
int  wff_open(void);                    /* load WFFONTS.DAT and look up ML's argnor23 / argnor28; 0 = all there. One more
                                           user (the UI, the roll HUD): read only when not there yet or wff_again() */
int  wff_reload(void);                  /* read it again from nothing, the users stay (a retry while open) */
void wff_close(void);                   /* one user less: freed with the last */
int  wff_status(void);                  /* bit 0: own pack missing, bit 1: argnor23 missing, bit 2: argnor28 */
const char *wff_why(void);              /* why the own pack is not used ("ok", "not on the card", "card busy", ...) */
int  wff_again(void);                   /* 1 = the own pack failed for a reason that may pass (card busy, memory) */
int  ui_text(int font, int x, int y, const char *s, int c);                   /* returns the advance */
int  ui_textw(int font, const char *s);
int  ui_text_clip(int font, int x, int y, const char *s, int c, int xmax);    /* stops before xmax */
int  ui_texth(int font);

/* ------------------------------------------------------------------ wf_cans.c: can pack */
enum { CAN_BIG = 0, CAN_STRIP, CAN_SEL, CAN_NSIZES };
extern const uint8_t wfc_size[CAN_NSIZES][2];
int  wfc_open(void);                    /* read the header, palette, index of WFCANS.DAT; 0 = ok */
void wfc_close(void);
int  wfc_status(void);                  /* 0 ok, else the reason (WFC_E_*) */
const char *wfc_why(int st);            /* a WFC_E_* code in words ("ok", "not on the card", ...) */
int  wfc_glow(const struct wf_film *f, uint8_t rgb[3]);   /* 1 = from the pack, 0 = group colour */
void wfc_draw(const struct wf_film *f, int size, int x, int y, int bg);   /* pack image or the plain can */
/* the pack image scaled by num / den (nearest; den <= 8), dim = 0..2: every 2nd / 3 of 4 pixels left out (a can further
   back in the roll picker's carousel); the plain can when the pack has none */
void wfc_draw_scaled(const struct wf_film *f, int size, int x, int y, int num, int den, int dim);
int  wfc_pal_used(void);                /* can colours in use: the pack's, or the plain cans' (WFC_PLAIN_N) */
int  wfc_again(void);                   /* 1 = the pack failed for a reason that may pass (card busy, memory) */
enum { WFC_OK = 0, WFC_E_NOFILE, WFC_E_HEADER, WFC_E_SIZES, WFC_E_MEM, WFC_E_READ, WFC_E_BUSY };
#define WFC_PLAIN_N     (4 + WF_NGROUPS * 3)                            /* plain can colours: caps, label, 3 per group */

/* ------------------------------------------------------------------ wf_ui.c: pack reads */
int  wfui_read(const char *path, uint32_t pos, void *p, uint32_t n, uint32_t *size);   /* wf_card_read, tried again
                                        while Canon holds the card (until the open's deadline, beating) */

/* ------------------------------------------------------------------ wf_ui.c (host harness entry points) */
void wfui_task_begin(void);             /* the UI task's set-up: resources, screen, palette */
int  wfui_task_tick(void);              /* one loop pass: keys, state, redraw; 0 = keep going */
void wfui_task_end(void);               /* give the screen and palette back, free everything */
void wfui_render(void);                 /* draw the whole browser into the idle buffer */
void wfui_render_anim(int t, int first);/* the load animation at t ms over it (first: dim the browser once) */

/* ------------------------------------------------------------------ wf_dark_ui.c: Roll + Darkroom screens (DESIGN 3.5) */
enum { WFUI_SCR_BROWSER = 0, WFUI_SCR_ROLL, WFUI_SCR_DARK, WFUI_SCR_N };
void wfdr_enter(int scr);               /* the screen becomes active: palette theme, its state */
void wfdr_render(int scr, int now);     /* draw it into the idle buffer */
enum { WFDR_STAY = 0, WFDR_CLOSE = 1, WFDR_LOADED = 2 };
int  wfdr_key(int scr, int key);        /* WFDR_*: LOADED = a roll was loaded (wf_ui.c: the load animation, then close) */
const struct wf_film *wfdr_loaded(int *nexp);   /* that roll's film and exposures */
int  wfdr_tick(int scr, int now);       /* 1 = something moved (redraw now) */
void wfdr_leave(int scr);

#endif
