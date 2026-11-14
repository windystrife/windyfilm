/* wf_dev_shim.c - the few ML / DryOS / module calls the RAW develop test and bench need (WindyStrife): memory, the
 * FIO calls of wf_loglut.c (current directory = card), wf_copyw (C: same result as wf_shot.c's LDM / STM bursts).
 * Smaller than host_ml.c on purpose: wf_dev_test links only wf_dev.o + wf_loglut.o + the Log + LUT kernel. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void *__mem_malloc(size_t n, unsigned flags, const char *file, unsigned line)
{
    (void)flags; (void)file; (void)line;
    return malloc(n);
}
void __mem_free(void *p) { free(p); }

static const char *h_path(const char *p)
{
    if ((p[0] == 'A' || p[0] == 'B') && p[1] == ':' && p[2] == '/')
        p += 3;
    return p;
}
void *FIO_OpenFile(const char *path, unsigned mode) { return fopen(h_path(path), (mode & 3) == 2 ? "r+b" : "rb"); }
void *FIO_CreateFile(const char *path) { return fopen(h_path(path), "w+b"); }
int FIO_ReadFile(void *f, void *p, size_t n) { return (int)fread(p, 1, n, f); }
int FIO_WriteFile(void *f, const void *p, size_t n) { return (int)fwrite(p, 1, n, f); }
void FIO_CloseFile(void *f) { if (f) fclose(f); }
int FIO_GetFileSize(const char *path, uint32_t *size)
{
    struct stat st;
    if (stat(h_path(path), &st) != 0 || S_ISDIR(st.st_mode))
        return -1;
    *size = (uint32_t)st.st_size;
    return 0;
}
int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence)
{
    if (fseeko(f, (off_t)pos, whence) != 0)
        return -1;
    return (int64_t)ftello(f);
}
int FIO_RemoveFile(const char *path) { return unlink(h_path(path)) == 0 ? 0 : -1; }
/* wf_fio.h (wf_main.c on the camera): the table cache's create / remove; the host card is never busy */
struct wf_fe;
void *wf_fcreate(const char *path, struct wf_fe *e) { (void)e; unlink(h_path(path)); return FIO_CreateFile(path); }
int wf_fremove(const char *path, struct wf_fe *e) { (void)e; unlink(h_path(path)); return 0; }
/* directory listing (the table cache's stale-file sweep): nothing found */
struct wf_file_info { uint16_t mode; uint32_t size; uint64_t timestamp; char name[0x80]; };
void *alloc_fio_file(void) { return calloc(1, 256); }
struct wf_file_info convert_fio_file_info(void *f) { struct wf_file_info fi; (void)f; memset(&fi, 0, sizeof fi); return fi; }
void *FIO_FindFirstEx(const char *dir, void *ff) { (void)dir; (void)ff; return 0; }
int FIO_FindNextEx(void *d, void *ff) { (void)d; (void)ff; return -1; }
void FIO_FindClose(void *d) { (void)d; }

#ifndef WF_DEV_SHIM_NO_COPYW                    /* tools/dev_bench.c has the LDM / STM one */
void wf_copyw(uint32_t *d, const uint32_t *s, int n)
{
    memcpy(d, s, (size_t)n * 4);
}
#endif
