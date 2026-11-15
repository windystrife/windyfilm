/* mk_wfstage_hook.h - WindyStrife, host only (tools/mk_wfstage.c, DESIGN 1a). Force-included (-include) when the
 * generator compiles the chain's film stage with its own trace switch on: the trace lines then land in
 * mk_wfstage_trace(), which reads the traced values from the argument list at full precision. The chain's source is
 * not edited and none of this reaches the module. */
#include <stdio.h>
int mk_wfstage_trace(FILE *f, const char *fmt, ...);
#define fprintf mk_wfstage_trace
