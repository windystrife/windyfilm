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
extern volatile int h_nchanges;                  /* prop_request_change calls */
extern uint32_t h_change_prop[256];
extern int h_change_val[256];
extern volatile int h_kelvin_n, h_kelvin_last, h_gm_last, h_ba_last;

/* lens_info fields by name */
int  h_li_get(const char *field);
void h_li_set(const char *field, int v);

/* card: get_shooting_card()->file_number */
void h_set_file_number(int n);

/* memory: shoot memory / Canon heap from a low arena (bit 30 clear: CACHEABLE() is the identity) */
extern volatile int h_shoot_fail;                /* shoot_malloc_suite_contig returns 0 */
extern volatile int h_heap_free;                 /* GetFreeMemForAllocateMemory */
extern volatile int h_arena_live;                /* blocks handed out and not freed */

/* clocks: h_clock_add_ms shifts get_ms_clock / get_us_clock forward (time jumps without waiting) */
extern volatile int h_clock_add_ms;

/* FIO: a path that refuses to open (EXIF busy test), "" none; counts */
extern char h_fio_busy[128];
extern volatile int h_fio_opens;

/* the menu entries menu_add got */
extern void *h_menu;
extern volatile int h_menu_count;

uint32_t wf_host_rom_read(uint32_t addr);
#endif
