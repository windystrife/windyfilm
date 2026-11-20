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
enum { F_ML23 = 0, F_ML28, F_15, F_13, F_13C,
       F_D20, F_D30, F_D80, F_L12,     /* the film OS's display sizes (docs/MODERNUI.md); F_D80 has digits, "/", "." only */
       F_NFONTS };
int  wff_open(void);                    /* load WFFONTS.DAT and look up ML's argnor23 / argnor28; 0 = all there. One more
                                           user (the UI, the roll HUD): read only when not there yet or wff_again() */
int  wff_reload(void);                  /* read it again from nothing, the users stay (a retry while open) */
void wff_close(void);                   /* one user less: freed with the last */
int  wff_status(void);                  /* bit 0: own pack missing, bit 1: argnor23 missing, bit 2: argnor28 */
int  wff_disp(void);                    /* the display sizes in the pack: bit 0 F_D20 .. bit 3 F_L12 (missing ones fall back) */
const char *wff_why(void);              /* why the own pack is not used ("ok", "not on the card", "card busy", ...) */
int  wff_again(void);                   /* 1 = the own pack failed for a reason that may pass (card busy, memory) */
int  ui_text(int font, int x, int y, const char *s, int c);                   /* returns the advance */
int  ui_textw(int font, const char *s);
int  ui_text_clip(int font, int x, int y, const char *s, int c, int xmax);    /* stops before xmax */
int  ui_texth(int font);
int  ui_textasc(int font);              /* cell top to baseline */
int  ui_caps(int x, int y, const char *s, int c);   /* F_L12 letter-spaced by 1 px (small caps labels: pass capitals) */
int  ui_capsw(const char *s);

/* ------------------------------------------------------------------ wf_cans.c: can pack */
enum { CAN_BIG = 0, CAN_STRIP, CAN_SEL, CAN_NSIZES };
extern const uint8_t wfc_size[CAN_NSIZES][2];
int  wfc_open(void);                    /* read the header, palette, index of WFCANS.DAT; 0 = ok */
void wfc_close(void);
int  wfc_status(void);                  /* 0 ok, else the reason (WFC_E_*) */
const char *wfc_why(int st);            /* a WFC_E_* code in words ("ok", "not on the card", ...) */
int  wfc_glow(const struct wf_film *f, uint8_t rgb[3]);   /* 1 = from the pack, 0 = group colour */
void wfc_draw(const struct wf_film *f, int size, int x, int y, int bg);   /* pack image or the plain can */
int  wfc_pal_used(void);                /* can colours in use: the pack's, or the plain cans' (WFC_PLAIN_N) */
void wfc_pal_again(void);               /* place them again (the browser back after the Darkroom) */
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

/* ------------------------------------------------------------------ wf_dark_ui.c: the Darkroom screen (DESIGN 3.6) */
/* (owner 11-17: the Roll picker screen is gone; a roll is picked in the browser's Darkroom mode, DESIGN 3.12) */
enum { WFUI_SCR_BROWSER = 0, WFUI_SCR_DARK, WFUI_SCR_HOME, WFUI_SCR_SET, WFUI_SCR_N };
void wfdr_enter(int scr);               /* the screen becomes active: palette theme, its state */
void wfdr_render(int scr, int now);     /* draw it into the idle buffer */
int  wfdr_key(int scr, int key);        /* 1 = close the UI */
int  wfdr_tick(int scr, int now);       /* 1 = something moved (redraw now) */
int  wfdr_dark(int scr);                /* 1 = the Darkroom's Screen Off has the display dark: no redraws (wf_dark.h) */
void wfdr_leave(int scr);
/* the Darkroom entry of the browser (owner 11-15, EOS M): PLAY opens the Darkroom while the roll has
   frames to develop. wfdr_dev_why: 0 = it can, else why not ("Load a roll first" / "No frames to develop");
   wfdr_dev_hint draws "[PLAY] Darkroom" at x (bright / dim by that), returns its end; wfdr_dev_hint_w its width */
const char *wfdr_dev_why(void);
int  wfdr_dev_hint(int x, int y);
int  wfdr_dev_hint_w(void);
/* develop again's film (owner 11-17): the Darkroom's Film row opens the browser to pick one (film = the one now, num /
   dev = the roll and the pass it makes, for its header); the browser's SET / MENU end it with wfdr_pick_done (film, or
   -1 = none) and switch back to the Darkroom */
void wfui_pick_film(int film, int num, int dev);
void wfdr_pick_done(int film);

/* ------------------------------------------------------------------ wf_os_ui.c: the film OS screens (docs/MODERNUI.md) */
/* Home (the MENU key with Camera interface = WindyFilm) and Settings; wf_dark_ui.c's wfdr_* pass these two screens on */
void wfos_enter(int scr);
void wfos_leave(int scr);
void wfos_render(int scr, int now);
int  wfos_key(int scr, int key);        /* 1 = close the UI (nothing after it) */
int  wfos_tick(int scr, int now);       /* 1 = redraw */
/* wf_ui.c, for the screens (UI task): switch to another screen; close and then hand Canon / ML a key (WFUI_AFTER_*);
   the browser with the focus on a row (the Home look strip) */
enum { WFUI_AFTER_NONE = 0, WFUI_AFTER_MENU, WFUI_AFTER_MLMENU, WFUI_AFTER_PLAY };
void wfui_goto(int scr);
void wfui_close_after(int what);
void wfui_browse(int row);

#endif
