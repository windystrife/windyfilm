/* host_ml.h - WindyFilm host test (WindyStrife): the knobs of the ML / DryOS shim (host_ml.c) the test (wf_test.c)
 * turns. The module objects are the camera's own (build.sh), linked into a qemu-arm Linux program. */
#ifndef HOST_ML_H
#define HOST_ML_H
#include <stdint.h>

/* camera identity and Canon state the module reads */
extern char __camera_model_short[16], firmware_version[16];
extern int pic_quality, shooting_mode, lv;
extern uint8_t lens_info[];                      /* ML struct lens_info bytes (layout from wf_expoff.c) */
extern volatile int h_play, h_movie, h_menu_shown, h_gdraw, h_expsim, h_expsim_stuck;

/* ROM words (wf_cam_*: wf_host_rom_read); h_rom_bad = address whose word reads wrong (0 none) */
extern volatile uint32_t h_rom_bad;

/* hooks: patch_hook_function records them; h_patch_err = what it returns (0 ok, 0x2 already patched) */
typedef void h_hook_fn(uint32_t *regs, uint32_t *stack, uint32_t pc);
struct h_hook { uint32_t addr; h_hook_fn *cbr; };
extern struct h_hook h_hooks[8];
extern volatile int h_nhooks, h_patch_err;
h_hook_fn *h_hook_at(uint32_t addr);

/* property handlers / changes */
typedef void h_prop_fn(unsigned property, void *priv, uint32_t *buf, unsigned len);
void h_prop_send(uint32_t property, uint32_t value);     /* call the module's handler of `property` */
extern int h_ps_contrast;                                /* picstyle_get_current_contrast(), -10 = not known */
extern volatile int h_nchanges;                  /* prop_request_change calls */
extern uint32_t h_change_prop[256];
extern int h_change_val[256];
extern volatile int h_kelvin_n, h_kelvin_last, h_gm_last, h_ba_last;

/* lens_info fields by name */
int  h_li_get(const char *field);
void h_li_set(const char *field, int v);

/* card: get_shooting_card()->file_number */
void h_set_file_number(int n);
void h_set_folder_number(int n);

/* memory: shoot memory / Canon heap from a low arena (bit 30 clear: CACHEABLE() is the identity) */
extern volatile int h_shoot_fail;                /* shoot_malloc_suite_contig returns 0 */
extern volatile int h_heap_free;                 /* GetFreeMemForAllocateMemory */
extern volatile int h_arena_live;                /* blocks handed out and not freed */

/* clocks: h_clock_add_ms shifts get_ms_clock / get_us_clock forward (time jumps without waiting) */
extern volatile int h_clock_add_ms;

/* FIO: a path that refuses to open, to be created, removed or renamed (EXIF busy test, the log / WFPROBE.BIN on a busy
   card), "*" every path, "" none; counts; h_fio_wmax > 0: the next write to a file of the path h_fio_wpath stops after
   that many bytes. h_fio_dup: creates on an existing name (two entries of one name on the camera), h_fio_dir_busy:
   creates / removes / renames while lens_info.job_state != 0; main() fails every run on either. */
extern char h_fio_busy[128], h_fio_wpath[128];
extern volatile int h_fio_opens, h_fio_wmax, h_fio_creates, h_fio_dup, h_fio_dir_busy;

/* the menu entries menu_add got */
extern void *h_menu;
extern volatile int h_menu_count;

uint32_t wf_host_rom_read(uint32_t addr);

/* RAW probe (EOS M): the Eeko parameter block copy at 0x41E82CEC, EDMAC WR0 / 308 / 408 / A08 / B08, and the
 * MEMNAVI_GetReadMemory lookup (out[0] = h_nav_a[layer]); other addresses read 0xDEADBEEF */
extern uint32_t h_pb[56], h_edmac[5], h_nav_a[5];
extern volatile int h_nav_calls;
/* 5D2 probe: Canon words by address (read by wf_host_mem_read before the EOS M ones; unset words read 0xDEADBEEF),
   the getters' return values (GetImageBuffer, GetJobID, GetShootParam, GetFileContainer, shamem_read) and the calls */
void h_mem_set(uint32_t a, uint32_t v);
extern uint32_t h_romcall_ret[5], h_romcall_fn[16], h_romcall_arg[16];
extern volatile int h_romcalls;
uint32_t wf_host_romcall(uint32_t fn, uint32_t arg);
uint32_t wf_host_mem_read(uint32_t addr);
int wf_host_memnavi(uint32_t *out, uint32_t *tmp, int a, int layer);

/* RAW develop "B": ML's drive_mode and ml_shutdown_requested; get_halfshutter_pressed() returns h_half and moves the clock
 * by h_half_step_ms per read; NotifyBox keeps its last text (h_notify) and counts calls */
extern int drive_mode, ml_shutdown_requested;
extern volatile int h_half, h_half_step_ms, h_notify_n;
extern char h_notify[160];
#endif
