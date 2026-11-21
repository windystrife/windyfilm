/* wf_ui_stub.c - WindyFilm: the browser UI's entry points when src/ui/wf_ui.c is not there (build.sh links this file
 * only then). The module still shoots and has its ML menu; "Open film browser" logs that there is no browser. */
#include "wf_api.h"
#include "wf_dark.h"                              /* struct wfos_lv (the LiveView finder's stubs) */

void wf_ui_init(void) { }
int  wf_ui_open(void) { wf_log("browser: not in this build"); return -1; }
void wf_ui_close(void) { }
int  wf_ui_is_open(void) { return 0; }
unsigned int wf_ui_key(unsigned int key) { (void)key; return 1; }
int  wf_ui_open_key(unsigned int key) { (void)key; return 0; }
int  wf_ui_home_key(unsigned int key) { (void)key; return 0; }
void wf_ui_open_home(void) { }
void wf_ui_open_refused(unsigned int key, int play) { (void)key; (void)play; }
int  wf_ui_menu_guard(void) { return 0; }

/* Roll + Darkroom screens and the LiveView HUD (src/ui/wf_dark_ui.c, wf_dark.h): none without the browser */
void wf_ui_open_scr(int scr) { wf_log("browser: not in this build (screen %d)", scr); }
int  wfdr_hud_fonts(int on) { (void)on; return -1; }
#ifndef WF_HOST
int  wfdr_hud_fonts_on(void) { return 0; }       /* (so the film OS's LiveView finder never comes up) */
void wfos_hud_top(uint8_t *vram, int pitch, int y, const struct wfos_lv *v) { (void)vram; (void)pitch; (void)y; (void)v; }
void wfos_hud_bottom(uint8_t *vram, int pitch, int y, const struct wfos_lv *v) { (void)vram; (void)pitch; (void)y; (void)v; }
void wfos_hud_mid(uint8_t *vram, int pitch, int y0, int y1, int vf) { (void)vram; (void)pitch; (void)y0; (void)y1; (void)vf; }
#else
/* the host test (test.sh, OSVF): the finder may come up (wf_stub_hud_fonts), each painter counts its calls and keeps its
   last rows and mode: [0] top band, [1] bottom band, [2] the middle */
int wf_stub_hud_fonts, wf_stub_hud_n[3], wf_stub_hud_y[3], wf_stub_hud_vf[3];
int  wfdr_hud_fonts_on(void) { return wf_stub_hud_fonts; }
static void stub_hud(int k, uint8_t *vram, int y, int vf) { if (vram) { wf_stub_hud_n[k]++; wf_stub_hud_y[k] = y; wf_stub_hud_vf[k] = vf; } }
void wfos_hud_top(uint8_t *vram, int pitch, int y, const struct wfos_lv *v) { (void)pitch; stub_hud(0, vram, y, v->vf); }
void wfos_hud_bottom(uint8_t *vram, int pitch, int y, const struct wfos_lv *v) { (void)pitch; stub_hud(1, vram, y, v->vf); }
void wfos_hud_mid(uint8_t *vram, int pitch, int y0, int y1, int vf) { (void)pitch; (void)y1; stub_hud(2, vram, y0, vf); }
#endif
void wfdr_hud(uint8_t *vram, int pitch, int blink, int mode) { (void)vram; (void)pitch; (void)blink; (void)mode; }
void wfdr_notice(uint8_t *vram, int pitch, int mode) { (void)vram; (void)pitch; (void)mode; }
int  wfdr_toast_fonts(int on) { (void)on; return 0; }
void wfdr_toast(uint8_t *vram, int pitch, const char *big, const char *k1, const char *t1, const char *k2, const char *t2,
                int mode) { (void)vram; (void)pitch; (void)big; (void)k1; (void)t1; (void)k2; (void)t2; (void)mode; }
