/* wf_ui_stub.c - WindyFilm: the browser UI's entry points when src/ui/wf_ui.c is not there (build.sh links this file
 * only then). The module still shoots and has its ML menu; "Open film browser" logs that there is no browser. */
#include "wf_api.h"

void wf_ui_init(void) { }
int  wf_ui_open(void) { wf_log("browser: not in this build"); return -1; }
void wf_ui_close(void) { }
int  wf_ui_is_open(void) { return 0; }
unsigned int wf_ui_key(unsigned int key) { (void)key; return 1; }
int  wf_ui_open_key(unsigned int key) { (void)key; return 0; }
