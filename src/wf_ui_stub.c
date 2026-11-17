/* wf_ui_stub.c - WindyFilm: the browser UI's entry points when src/ui/wf_ui.c is not there (build.sh links this file
 * only then). The module still shoots and has its ML menu; "Open film browser" logs that there is no browser. */
#include "wf_api.h"

void wf_ui_init(void) { }
int  wf_ui_open(void) { wf_log("browser: not in this build"); return -1; }
void wf_ui_close(void) { }
int  wf_ui_is_open(void) { return 0; }
unsigned int wf_ui_key(unsigned int key) { (void)key; return 1; }
int  wf_ui_open_key(unsigned int key) { (void)key; return 0; }
void wf_ui_open_refused(unsigned int key, int play) { (void)key; (void)play; }
int  wf_ui_menu_guard(void) { return 0; }

/* Roll + Darkroom screens and the LiveView HUD (src/ui/wf_dark_ui.c, wf_dark.h): none without the browser */
void wf_ui_open_scr(int scr) { wf_log("browser: not in this build (screen %d)", scr); }
int  wfdr_hud_fonts(int on) { (void)on; return -1; }
void wfdr_hud(uint8_t *vram, int pitch, int blink, int mode) { (void)vram; (void)pitch; (void)blink; (void)mode; }
void wfdr_notice(uint8_t *vram, int pitch, int mode) { (void)vram; (void)pitch; (void)mode; }
int  wfdr_toast_fonts(int on) { (void)on; return 0; }
void wfdr_toast(uint8_t *vram, int pitch, const char *big, const char *k1, const char *t1, const char *k2, const char *t2,
                int mode) { (void)vram; (void)pitch; (void)big; (void)k1; (void)t1; (void)k2; (void)t2; (void)mode; }
