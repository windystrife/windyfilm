/* wf_logout_tab.c - tables of the log output modes (S-Log3 / V-Log / LogC3 / Canon Log 2 / 3) for WindyFilm.
 * The kernel wf_log_words is ARMv5TE assembly (wf_logout_arm.S); this file only defines its tables:
 *   wf_lenc   (wf_logtab.h, mk_logtab.py)      unchanged symbol and format: wf_loglut.c uses it (extern)
 *   wf_lasm, wf_lasm_k (wf_logasm_tab.h, mk_logasm_tab.py, generated from wf_logtab.h)
 * wf_dec and wf_lmat (static in wf_logtab.h) are folded into wf_lasm / wf_lasm_k and not emitted here. */
#include <stdint.h>
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "wf_logtab.h"
#include "wf_logasm_tab.h"
