/* wf_fio.h - WindyFilm card writes (wf_main.c), WindyStrife's own code. Included by wf_core.h and by wf_loglut.c (which
 * builds without the core headers).
 *
 * Canon's FIO makes a second directory entry of the same name when a file is created over an existing one. ML's
 * FIO_CreateFile removes the file first, but a remove the card refuses while it saves a shot goes unnoticed: the 11-14
 * EOS M run left two WINDYFLM.LOG entries in ML/LOGS (trap W24). So only these functions change a card directory:
 *   wf_fcreate   a new file: only while Canon is idle (lens_info.job_state 0) and only once the name is verified gone
 *                (removed, then FIO_GetFileSize fails)
 *   wf_fremove   remove (same rule), up to WF_RM_TRIES times while the name is still there (an old duplicate entry)
 *   wf_frename   rename: the target removed first; ok when the source is gone and the target is there
 *   wf_fopen_w   a handle at the end (WF_FO_END) / at an offset (WF_FO_AT) of an existing file: an open never changes
 *                the directory, so it may run while Canon is busy; WF_FO_NEW, and WF_FO_END_OR_NEW when the file is
 *                not there, go through wf_fcreate
 * They return 0 / a handle for FIO_WriteFile + FIO_CloseFile, or -1 / 0 with e = the step that failed and its return
 * value (ML's FIO_* give no error code; for WF_FE_BUSY the job state). tools/provenance_gate.py check 5 fails the build
 * when any other code calls FIO_CreateFile, FIO_CreateFileOrAppend, FIO_RemoveFile or FIO_RenameFile. */
#ifndef WF_FIO_H
#define WF_FIO_H
#include <stdint.h>

enum { WF_FO_NEW, WF_FO_END, WF_FO_END_OR_NEW, WF_FO_AT };
enum { WF_FE_OK, WF_FE_BUSY, WF_FE_REMOVE, WF_FE_CREATE, WF_FE_OPEN, WF_FE_SEEK, WF_FE_WRITE, WF_FE_RENAME, WF_FE_N };
struct wf_fe { int step, rc; };

void *wf_fopen_w(const char *path, int how, uint32_t pos, struct wf_fe *e);
void *wf_fcreate(const char *path, struct wf_fe *e);
int   wf_fremove(const char *path, struct wf_fe *e);
int   wf_frename(const char *src, const char *dst, struct wf_fe *e);
const char *wf_fe_name(int step);                /* "busy", "remove", "create", "open", "seek", "write", "rename" */

#endif
