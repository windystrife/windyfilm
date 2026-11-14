/* host_ml.h - the slice of Magic Lantern's API that src/ui uses, for the x86 host harness (-DWF_HOST).
 * Same names and types as the ML tree (bmp.h, rbf_font.h, font.h, gui-common.h, module.h, timer.h); the
 * implementation (host_ml.c) keeps a 960 x 540 idle / front bitmap pair and a fake LCD palette, so the harness can
 * dump exactly what the camera would show. */
#ifndef HOST_ML_H
#define HOST_ML_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* allocation counted, so the harness can check that close frees everything */
void *host_malloc(size_t n);
void  host_free(void *p);
#define malloc(n) host_malloc(n)
#define free(p)   host_free(p)

/* bitmap */
#define BMPPITCH 960
uint8_t *bmp_vram_idle(void);
uint8_t *bmp_vram_real(void);
void bmp_idle_copy(int direction, int fullsize);
#define BMP_LOCK(x) { host_lock(1); x; host_lock(-1); }
void host_lock(int d);

/* palette */
extern uint32_t LCD_Palette[];
void EngDrvOut(uint32_t reg, uint32_t value);
uint32_t shamem_read(uint32_t addr);
#define COLOR_WHITE 1
#define COLOR_BLACK 2
#define COLOR_RED 8                             /* ML bmp.h default palette indices (the HUD) */
#define COLOR_YELLOW 0x0F
#define COLOR_GRAY(p) (38 + ((p) * 41) / 100)

/* RBF fonts (rbf_font.h / font.h / bmp.h) */
#define RBF_MAX_NAME 64
typedef struct {
    int magic1, magic2;
    char name[RBF_MAX_NAME];
    int charSize, points, height, maxWidth, charFirst, charLast, _unknown4, _wmapAddr, _cmapAddr, descent, intline;
} font_hdr;
typedef struct _font {
    font_hdr hdr;
    int charCount;
    int width;
    char wTable[256];
    char *cTable;
    int cTableSize;
    int cTableSizeMax;
} font;
struct font { unsigned height, width; unsigned *bitmap; };
#define MAX_DYN_FONTS 7
extern struct font font_dynamic[MAX_DYN_FONTS + 1];
#define FONT_ID(f) (((f) >> 16) & 0x7)
uint32_t font_by_name(char *file, uint32_t fg_color, uint32_t bg_color);

/* GUI, tasks, time */
void canon_gui_disable_front_buffer(void);
void canon_gui_enable_front_buffer(int also_redraw);
uint32_t task_create(const char *name, uint32_t priority, uint32_t stack_size, void *entry, void *arg);
void msleep(int ms);
int get_ms_clock(void);
int gui_menu_shown(void);
void gui_stop_menu(void);
extern int gui_state;
#define GUISTATE_IDLE 0
extern int lv;
int is_movie_mode(void);
int display_is_on(void);
void display_on(void);
int get_gui_mode(void);
void SetGUIRequestMode(int mode);
void bmp_off(void);
void bmp_on(void);
void force_liveview(void);
extern int host_recording;
#define RECORDING (host_recording)
static inline uint32_t cli(void) { return 0x80; }
static inline void sei(uint32_t old) { (void)old; }

/* module.h portable key codes */
#define MODULE_KEY_PRESS_HALFSHUTTER       ( 1)
#define MODULE_KEY_UNPRESS_HALFSHUTTER     ( 2)
#define MODULE_KEY_PRESS_FULLSHUTTER       ( 3)
#define MODULE_KEY_UNPRESS_FULLSHUTTER     ( 4)
#define MODULE_KEY_WHEEL_UP                ( 5)
#define MODULE_KEY_WHEEL_DOWN              ( 6)
#define MODULE_KEY_WHEEL_LEFT              ( 7)
#define MODULE_KEY_WHEEL_RIGHT             ( 8)
#define MODULE_KEY_PRESS_SET               ( 9)
#define MODULE_KEY_UNPRESS_SET             (10)
#define MODULE_KEY_JOY_CENTER              (11)
#define MODULE_KEY_PRESS_UP                (12)
#define MODULE_KEY_PRESS_RIGHT             (15)
#define MODULE_KEY_PRESS_LEFT              (16)
#define MODULE_KEY_PRESS_DOWN              (19)
#define MODULE_KEY_UNPRESS_UDLR            (20)
#define MODULE_KEY_MENU                    (22)
#define MODULE_KEY_INFO                    (23)
#define MODULE_KEY_PLAY                    (24)
#define MODULE_KEY_TRASH                   (25)
#define MODULE_KEY_REC                     (27)
#define MODULE_KEY_LV                      (28)
#define MODULE_KEY_Q                       (29)
#define MODULE_KEY_PICSTYLE                (30)
#define MODULE_KEY_TOUCH_1_FINGER          (35)
#define MODULE_KEY_UNTOUCH_1_FINGER        (36)

/* harness side */
void host_init(const char *rbf_dir, uint32_t canon_white, uint32_t canon_black);
int  host_dump(const char *ppm_path);           /* front buffer through the hardware palette -> PPM; returns stray px */
int  host_front_disabled(void);
/* Canon's GUI mode and display (5D2 without LiveView): a request is applied 60 ms later; a mode other than 0 turns the
 * display on when `lights` (Canon's menu does), display_on() always does */
void host_gui_set(int mode, int display, int lights);
void host_gui_later(int mode, int after_ms);
int  host_gui_mode(void);
int  host_gui_requests(void);
int  host_gui_req0_front(void);         /* front buffer state at the last SetGUIRequestMode(0): 1 = still disabled */
int  host_display_ons(void);
int  host_bmp_muted(void);              /* bmp_off() in force (Canon's bitmap hidden) */
int  host_bmp_offs(void);               /* bmp_off() calls since host_gui_set */
int  host_bmp_muted_at_req(void);       /* muted at the last SetGUIRequestMode(non-zero): 1 = Canon's dialog hidden */
int  host_lv_starts(void);              /* force_liveview() calls since host_gui_set */
int  host_tasks(void);                  /* task_create() calls */
void host_task_fail(int fail);          /* task_create returns an odd (error) handle */
int  host_palette_is_canon(void);               /* every entry 0x50..0xFE back to Canon's value */
int  host_front_is_clear(void);
long host_live_allocs(void);
long host_peak_bytes(void);
int  host_lock_depth(void);
int  host_ui_pixels_ok(void);                   /* every front pixel is one of the browser's entries */

#endif
