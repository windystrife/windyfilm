/* host_ml.c - WindyFilm host test (WindyStrife): the ML / DryOS functions and globals the module objects import,
 * for a qemu-arm Linux program (test.sh). Tasks are pthreads, semaphores POSIX ones, FIO the current directory
 * (the card root: ML/DATA, ML/SETTINGS, ML/LOGS, DCIM/100CANON). Layouts of ML structs come from wf_expoff.c. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "host_ml.h"

extern const int wf_t_li_size, wf_t_li_shutter, wf_t_li_aperture, wf_t_li_iso, wf_t_li_iso_auto, wf_t_li_ae,
    wf_t_li_job, wf_t_li_wb_mode, wf_t_li_kelvin, wf_t_li_wbs_gm, wf_t_li_wbs_ba, wf_t_li_picstyle, wf_t_card_fn, wf_t_card_size;

char __camera_model_short[16] = "EOSM", firmware_version[16] = "2.0.2";
int pic_quality, shooting_mode = 3, lv = 1;
uint8_t lens_info[1024] __attribute__((aligned(8)));
volatile int h_play, h_movie, h_menu_shown, h_gdraw = 1, h_expsim = 1, h_expsim_stuck;
volatile uint32_t h_rom_bad;
struct h_hook h_hooks[8];
volatile int h_nhooks, h_patch_err;
volatile int h_nchanges;
uint32_t h_change_prop[256];
int h_change_val[256];
volatile int h_kelvin_n, h_kelvin_last, h_gm_last, h_ba_last;
volatile int h_shoot_fail, h_heap_free = 8 << 20, h_arena_live;
volatile int h_clock_add_ms;
char h_fio_busy[128], h_fio_wpath[128];
static void *h_wf;                                  /* the open file of h_fio_wpath */
volatile int h_fio_opens, h_fio_wmax, h_fio_creates, h_fio_dup, h_fio_dir_busy;
void *h_menu;
volatile int h_menu_count;

/* ------------------------------------------------------------------ ROM */
static const uint32_t h_rom[][2] = {
    { 0xFF169414u, 0xE92D41FCu }, { 0xFF17036Cu, 0xE5878058u }, { 0xFF16B580u, 0xE5806058u },     /* EOS M 2.0.2 */
    { 0xFF888884u, 0xE92D4FF8u }, { 0xFF888888u, 0xE51F61D0u }, { 0xFF88888Cu, 0xE1A07000u },     /* 5D2 2.1.2 */
    { 0xFF888ACCu, 0xE5857054u }, { 0xFF888AD4u, 0xE585806Cu }, { 0xFF88AB40u, 0xEBFFF74Fu },
    { 0xFF8867A0u, 0xE92D47F0u }, { 0xFF8867A8u, 0xE1A05000u }, { 0xFF886824u, 0xE5805054u },
    { 0xFF886828u, 0xE580606Cu }, { 0xFF887A94u, 0xEBFFFB41u }, { 0xFF9BE83Cu, 0x0140D600u },
};
uint32_t wf_host_rom_read(uint32_t a)
{
    for (unsigned i = 0; i < sizeof h_rom / sizeof *h_rom; i++)
        if (h_rom[i][0] == a)
            return a == h_rom_bad ? h_rom[i][1] ^ 1 : h_rom[i][1];
    return 0;
}

/* ------------------------------------------------------------------ RAW probe (wf_cam_eosm.c): Canon memory, ROM lookup */
uint32_t h_pb[56], h_edmac[5], h_nav_a[5];
volatile int h_nav_calls;
uint32_t wf_host_mem_read(uint32_t a)
{
    static const uint32_t ed[5] = { 0xC0F04008u, 0xC0F26308u, 0xC0F26408u, 0xC0F26A08u, 0xC0F26B08u };
    if (a >= 0x41E82CECu && a < 0x41E82CECu + sizeof h_pb && !(a & 3))
        return h_pb[(a - 0x41E82CECu) / 4];
    for (int i = 0; i < 5; i++)
        if (a == ed[i])
            return h_edmac[i];
    return 0xDEADBEEFu;
}
int wf_host_memnavi(uint32_t *out, uint32_t *tmp, int a, int layer)
{
    h_nav_calls++;
    if (a != 1 || layer < 0 || layer > 4)
        return -1;
    out[0] = h_nav_a[layer];
    out[1] = 0;
    *tmp = 0x1234;
    return 0;
}

/* ------------------------------------------------------------------ clocks, tasks, semaphores */
static uint64_t h_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000 + (uint64_t)h_clock_add_ms * 1000u;
}
int get_ms_clock(void) { return (int)(h_us() / 1000); }
uint64_t get_us_clock(void) { return h_us(); }
void msleep(int ms) { usleep(ms > 0 ? ms * 1000 : 1000); }

struct h_task { void (*entry)(void *); void *arg; };
static void *h_task_main(void *p)
{
    struct h_task t = *(struct h_task *)p;
    free(p);
    t.entry(t.arg);
    return 0;
}
int task_create(const char *name, uint32_t prio, uint32_t stack, void *entry, void *arg)
{
    static int n;
    (void)name; (void)prio; (void)stack;
    struct h_task *t = malloc(sizeof *t);
    t->entry = (void (*)(void *))entry;
    t->arg = arg;
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 256 << 10);
    if (pthread_create(&th, &at, h_task_main, t) != 0)
        return 1;                                /* odd: an error */
    return 2 * ++n;
}

struct h_sem { sem_t s; } __attribute__((aligned(8)));
void *create_named_semaphore(const char *name, int initial)
{
    (void)name;
    struct h_sem *s = malloc(sizeof *s);
    sem_init(&s->s, 0, initial);
    return s;
}
int take_semaphore(void *p, int timeout_ms)
{
    struct h_sem *s = p;
    if (timeout_ms <= 0) {                       /* DryOS: 0 = wait for ever */
        while (sem_wait(&s->s) != 0 && errno == EINTR)
            ;
        return 0;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    for (;;) {
        if (sem_timedwait(&s->s, &ts) == 0)
            return 0;
        if (errno != EINTR)
            return 9;                            /* DryOS timeout */
    }
}
int give_semaphore(void *p) { sem_post(&((struct h_sem *)p)->s); return 0; }

/* ------------------------------------------------------------------ memory */
void *__mem_malloc(size_t n, unsigned flags, const char *file, unsigned line)
{
    (void)flags; (void)file; (void)line;
    return malloc(n ? n : 1);
}
void __mem_free(void *p) { free(p); }

#define H_ARENA (40u << 20)
static uint8_t h_arena[H_ARENA] __attribute__((aligned(64)));
static struct { uint32_t off, size; } h_blk[64];
static int h_nblk;
static pthread_mutex_t h_arena_mx = PTHREAD_MUTEX_INITIALIZER;
static void *h_arena_alloc(size_t n)
{
    uint32_t sz = ((uint32_t)n + 63) & ~63u, at = 0;
    pthread_mutex_lock(&h_arena_mx);
    int i = 0;
    for (; i < h_nblk; i++) {                    /* blocks sorted by offset: first gap that fits */
        if (h_blk[i].off - at >= sz)
            break;
        at = h_blk[i].off + h_blk[i].size;
    }
    void *p = 0;
    if (h_nblk < 64 && H_ARENA - at >= sz && (i < h_nblk || H_ARENA - at >= sz)) {
        memmove(&h_blk[i + 1], &h_blk[i], (h_nblk - i) * sizeof *h_blk);
        h_blk[i].off = at;
        h_blk[i].size = sz;
        h_nblk++;
        h_arena_live++;
        p = h_arena + at;
        memset(p, 0xA5, sz);                     /* never zero: the module must initialise what it uses */
    }
    pthread_mutex_unlock(&h_arena_mx);
    return p;
}
static void h_arena_free(void *p)
{
    pthread_mutex_lock(&h_arena_mx);
    for (int i = 0; i < h_nblk; i++)
        if (h_arena + h_blk[i].off == p) {
            memmove(&h_blk[i], &h_blk[i + 1], (h_nblk - i - 1) * sizeof *h_blk);
            h_nblk--;
            h_arena_live--;
            break;
        }
    pthread_mutex_unlock(&h_arena_mx);
}
struct h_suite { void *p; int size; };
void *shoot_malloc_suite_contig(size_t n)
{
    if (h_shoot_fail)
        return 0;
    void *p = h_arena_alloc(n);
    if (!p)
        return 0;
    struct h_suite *s = malloc(sizeof *s);
    s->p = p;
    s->size = (int)n;
    return s;
}
void shoot_free_suite(void *s) { if (s) { h_arena_free(((struct h_suite *)s)->p); free(s); } }
void *GetFirstChunkFromSuite(void *s) { return s; }
void *GetMemoryAddressOfMemoryChunk(void *c) { return ((struct h_suite *)c)->p; }
int GetSizeOfMemoryChunk(void *c) { return ((struct h_suite *)c)->size; }
void *__priv_AllocateMemory(size_t n) { return h_shoot_fail ? 0 : h_arena_alloc(n); }
void __priv_FreeMemory(void *p) { h_arena_free(p); }
int GetFreeMemForAllocateMemory(void) { return h_heap_free; }

/* ------------------------------------------------------------------ FIO: the current directory is the card */
static const char *h_path(const char *p)
{
    if ((p[0] == 'A' || p[0] == 'B') && p[1] == ':' && p[2] == '/')
        p += 3;
    return p;
}
static int h_busy(const char *path)              /* h_fio_busy: that path, or "*" = every path (card busy) */
{
    return h_fio_busy[0] && (!strcmp(h_fio_busy, "*") || !strcmp(h_path(path), h_fio_busy));
}
void *FIO_OpenFile(const char *path, unsigned mode)
{
    h_fio_opens++;
    if (h_busy(path))
        return 0;
    void *f = fopen(h_path(path), (mode & 3) == 2 ? "r+b" : "rb");
    if (f && h_fio_wpath[0] && !strcmp(h_path(path), h_fio_wpath))
        h_wf = f;
    return f;
}
static int h_exists(const char *path) { struct stat st; return stat(h_path(path), &st) == 0; }
/* a directory change (create / remove / rename) while Canon saves a shot (lens_info.job_state != 0): counted in
   h_fio_dir_busy, main() fails the run on any */
static void h_dir_change(void) { if (h_li_get("job")) h_fio_dir_busy++; }
/* ML's FIO_CreateFile = _FIO_RemoveFile + _FIO_CreateFile. On the camera a create over a name the card did not let go
   of leaves a second entry of that name (two WINDYFLM.LOG on the 11-14 card), so the module must have removed the file
   and seen it gone first: a create on an existing name is counted in h_fio_dup (main() fails the run on any). A busy
   path refuses the create. */
void *FIO_CreateFile(const char *path)
{
    h_fio_creates++;
    h_dir_change();
    if (h_exists(path))
        h_fio_dup++;
    if (h_busy(path))
        return 0;
    void *f = fopen(h_path(path), "w+b");
    if (f && h_fio_wpath[0] && !strcmp(h_path(path), h_fio_wpath))
        h_wf = f;
    return f;
}
int FIO_ReadFile(void *f, void *p, size_t n) { return (int)fread(p, 1, n, f); }
/* h_fio_wmax > 0: the next write to a file opened by the path h_fio_wpath stops after that many bytes (a short write),
   then writes are whole again */
int FIO_WriteFile(void *f, const void *p, size_t n)
{
    int m = h_fio_wmax;
    if (m > 0 && (size_t)m < n && f == h_wf) {
        h_fio_wmax = 0;
        n = (size_t)m;
    }
    int k = (int)fwrite(p, 1, n, f);
    fflush(f);
    return k;
}
void FIO_CloseFile(void *f) { if (f == h_wf) h_wf = 0; if (f) fclose(f); }
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
int FIO_RemoveFile(const char *path)             /* a busy path: the card refuses, the file stays */
{
    h_dir_change();
    return !h_busy(path) && unlink(h_path(path)) == 0 ? 0 : -1;
}
int FIO_RenameFile(const char *src, const char *dst)
{
    h_dir_change();
    if (h_busy(src) || h_busy(dst))
        return -1;
    char s[256];
    snprintf(s, sizeof s, "%s", h_path(src));
    return rename(s, h_path(dst)) == 0 ? 0 : -1;
}

/* directory listing: alloc_fio_file + FIO_FindFirstEx / NextEx + convert_fio_file_info (layout of wf_loglut.c) */
struct wf_file_info { uint16_t mode; uint32_t size; uint64_t timestamp; char name[0x80]; };
struct h_fio_file { char dir[160]; struct wf_file_info fi; };
void *alloc_fio_file(void) { return calloc(1, sizeof(struct h_fio_file)); }
struct wf_file_info convert_fio_file_info(void *f) { return ((struct h_fio_file *)f)->fi; }
static int h_find_next(DIR *d, struct h_fio_file *f)
{
    struct dirent *e;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char p[400];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", f->dir, e->d_name);
        if (stat(p, &st) != 0)
            continue;
        memset(&f->fi, 0, sizeof f->fi);
        f->fi.mode = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
        f->fi.size = (uint32_t)st.st_size;
        f->fi.timestamp = (uint64_t)st.st_mtime;
        snprintf(f->fi.name, sizeof f->fi.name, "%s", e->d_name);
        return 0;
    }
    return 1;
}
void *FIO_FindFirstEx(const char *dir, void *ff)
{
    struct h_fio_file *f = ff;
    snprintf(f->dir, sizeof f->dir, "%s", h_path(dir));
    size_t l = strlen(f->dir);
    if (l > 1 && f->dir[l - 1] == '/')
        f->dir[l - 1] = 0;
    DIR *d = opendir(f->dir[0] ? f->dir : ".");
    if (!d)
        return 0;
    if (h_find_next(d, f)) {
        closedir(d);
        return 0;
    }
    return d;
}
int FIO_FindNextEx(void *d, void *ff) { return h_find_next(d, ff); }
void FIO_FindClose(void *d) { closedir(d); }

static uint8_t h_card[64] __attribute__((aligned(8)));
void *get_shooting_card(void) { return h_card; }
void h_set_file_number(int n) { memcpy(h_card + wf_t_card_fn, &n, 4); }
const char *get_dcim_dir(void) { return "B:/DCIM/100CANON"; }
const char *picstyle_get_current_name(void) { return "(host style)"; }
int h_ps_contrast = -10;                                     /* -10 = not known, as ML without the style's settings */
int picstyle_get_current_contrast(void) { return h_ps_contrast; }
char *get_file_prefix(void) { return "IMG_"; }

/* ------------------------------------------------------------------ lens_info, properties, white balance */
static const struct { const char *n; const int *off; int bytes; } h_li_fields[] = {
    { "shutter", &wf_t_li_shutter, 1 }, { "aperture", &wf_t_li_aperture, 1 }, { "iso", &wf_t_li_iso, 1 },
    { "iso_auto", &wf_t_li_iso_auto, 1 }, { "ae", &wf_t_li_ae, 4 }, { "job", &wf_t_li_job, 4 },
    { "wb_mode", &wf_t_li_wb_mode, 4 }, { "kelvin", &wf_t_li_kelvin, 4 }, { "gm", &wf_t_li_wbs_gm, -1 },
    { "ba", &wf_t_li_wbs_ba, -1 }, { "picstyle", &wf_t_li_picstyle, 1 },
};
int h_li_get(const char *field)
{
    for (unsigned i = 0; i < sizeof h_li_fields / sizeof *h_li_fields; i++)
        if (!strcmp(h_li_fields[i].n, field)) {
            uint8_t *p = lens_info + *h_li_fields[i].off;
            int v;
            if (h_li_fields[i].bytes == 4) memcpy(&v, p, 4);
            else if (h_li_fields[i].bytes == 1) v = *p;
            else v = (int8_t)*p;
            return v;
        }
    fprintf(stderr, "h_li_get: %s?\n", field);
    exit(2);
}
void h_li_set(const char *field, int v)
{
    for (unsigned i = 0; i < sizeof h_li_fields / sizeof *h_li_fields; i++)
        if (!strcmp(h_li_fields[i].n, field)) {
            uint8_t *p = lens_info + *h_li_fields[i].off;
            if (h_li_fields[i].bytes == 4) memcpy(p, &v, 4);
            else *p = (uint8_t)v;
            return;
        }
    fprintf(stderr, "h_li_set: %s?\n", field);
    exit(2);
}

static h_prop_fn *h_ph[8];
static uint32_t h_ph_id[8];
static int h_nph;
void prop_add_handler(uint32_t property, void *handler)
{
    if (h_nph < 8) { h_ph_id[h_nph] = property; h_ph[h_nph++] = (h_prop_fn *)handler; }
}
void h_prop_send(uint32_t property, uint32_t value)
{
    for (int i = 0; i < h_nph; i++)
        if (h_ph_id[i] == property)
            h_ph[i](property, 0, &value, 4);
}
static void h_change(uint32_t prop, const void *v, int len)
{
    int x = 0;
    memcpy(&x, v, len < 4 ? len : 4);
    int n = h_nchanges;
    if (n < 256) { h_change_prop[n] = prop; h_change_val[n] = x; }
    h_nchanges = n + 1;
    if (prop == 0x80050018u || prop == 0x8000000Du)            /* PROP_WB_MODE_LV / _PH */
        h_li_set("wb_mode", x);
    else if (prop == 0x80050019u || prop == 0x8000000Eu)       /* PROP_WB_KELVIN_LV / _PH */
        h_li_set("kelvin", x);
    else if (prop == 0x80000034u && !h_expsim_stuck)           /* ExpSim: 3 = Disable */
        h_expsim = x == 3 ? 0 : 1;
}
int prop_request_change(uint32_t prop, const void *v, size_t len) { h_change(prop, v, (int)len); return 0; }
int prop_request_change_wait(uint32_t prop, const void *v, size_t len, int timeout)
{
    (void)timeout;
    h_change(prop, v, (int)len);
    return 0;
}
void lens_set_kelvin(int k)
{
    h_kelvin_n++;
    h_kelvin_last = k;
    h_li_set("wb_mode", 9);
    h_li_set("kelvin", k);
}
void lens_set_wbs_gm(int v) { h_gm_last = v; h_li_set("gm", v); }
void lens_set_wbs_ba(int v) { h_ba_last = v; h_li_set("ba", v); }
int get_expsim(void) { return h_expsim; }

/* ------------------------------------------------------------------ hooks, GUI, misc */
int patch_hook_function(uintptr_t addr, uint32_t orig, void *cbr, const char *desc)
{
    (void)orig; (void)desc;
    if (h_patch_err)
        return h_patch_err;
    if (h_nhooks < 8) {
        h_hooks[h_nhooks].addr = (uint32_t)addr;
        h_hooks[h_nhooks].cbr = (h_hook_fn *)cbr;
        h_nhooks++;
    }
    return 0;
}
h_hook_fn *h_hook_at(uint32_t addr)
{
    for (int i = 0; i < h_nhooks; i++)
        if (h_hooks[i].addr == addr)
            return h_hooks[i].cbr;
    return 0;
}
int streq(const char *a, const char *b) { return !strcmp(a, b); }
int is_play_mode(void) { return h_play; }
int is_movie_mode(void) { return h_movie; }
int gui_menu_shown(void) { return h_menu_shown; }
void gui_stop_menu(void) { h_menu_shown = 0; }
int get_global_draw(void) { return h_gdraw; }
int get_global_draw_setting(void) { return h_gdraw; }
void menu_add(const char *name, void *e, int n) { (void)name; h_menu = e; h_menu_count = n; }
void menu_remove(const char *name, void *e, int n) { (void)name; (void)e; (void)n; h_menu = 0; }
void menu_open_submenu(void) { }
void bmp_printf(uint32_t font, int x, int y, const char *fmt, ...) { (void)font; (void)x; (void)y; (void)fmt; }
void bmp_fill(uint8_t c, int x, int y, int w, int h) { (void)c; (void)x; (void)y; (void)w; (void)h; }
