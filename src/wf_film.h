/* wf_film.h - WindyStrife own film looks (wf_film.c): the 2.5D table set and its kernel.
 * Two sets exist on the camera: the shot set inside wf_film.c (built and read by the worker task
 * only, wf_film_prepare) and the LiveView preview set (malloc'd by wf_shot.c, built by wf_task
 * with wf_film_build, read by the display filter task). Same builder, same kernel. */
#ifndef WF_FILM_H
#define WF_FILM_H
#include <stdint.h>

#define WF_NL 65                                   /* luma levels: y = 0, 4, .., 252, 255 (legacy) or adaptive (V21) */
struct wf_ftabs {
    uint16_t uvi[256];                             /*     0  cell << 5 | weight, for u and v */
    uint8_t  N[256];                               /*   512  neutral-axis luma curve */
    uint8_t  clampY[768];                          /*   768  clampY[256 + x] = clamp(x, 0, 255) */
    uint8_t  fcell[256], ffrac[256];               /*  1536  grid cell 0..15, weight 0..16 (build only) */
    uint8_t  E[WF_NL * 289];                       /*  2048  Y' - N[y_level] + 128 */
    uint8_t  flags, pad[2];                        /* 20833  the build's WF_FT_* flags (wf_film_words picks its loop) */
    uint32_t C[WF_NL * 289];                       /* 20836  (U' + 128) | (V' + 128) << 16 */
    uint16_t lvl[256];                             /* 95976  luma y -> its level's offset (level * 289) */
    const uint32_t *src;                           /* 96488  table the set was built from, 0 = none */
};
typedef char wf_ftabs_layout[(__builtin_offsetof(struct wf_ftabs, E) == 2048
                              && __builtin_offsetof(struct wf_ftabs, C) == 20836
                              && __builtin_offsetof(struct wf_ftabs, lvl) == 95976
                              && sizeof(struct wf_ftabs) == 96492) ? 1 : -1];

/* build flags. WF_FT_V21: adaptive luma levels + per-pixel E (the catalogue films, ids 32..); without it the set is
 * the shipped kernel's, bit-exact (ids 26..31). WF_FT_MONO: a B&W film (catalogue type 2), the Effect strength
 * scales luma only. WF_FT_S(s): the Effect strength s (30 Subtle, 100 Normal = no scaling step, 125 Strong). */
#define WF_FT_V21   0x01
#define WF_FT_MONO  0x02
#define WF_FT_S(s)  ((s) == 100 ? 0 : (s) << 8)
#define WF_FT_SOF(f) (((f) >> 8 & 255) ? ((f) >> 8 & 255) : 100)
/* WF_FT_HS(h, s): Highlights h / Shadows s, WF_HS_MIN..WF_HS_MAX each, as two signed 4-bit fields (bits 16-19, 20-23);
 * 0 / 0 adds no bit, so the flags (and the set) of a build without the setting stay as they were */
#define WF_HS_MIN   (-5)
#define WF_HS_MAX   5
#define WF_FT_HS(h, s) ((int)(((unsigned)(h) & 15u) << 16 | ((unsigned)(s) & 15u) << 20))
#define WF_FT_HOF(f)   ((int)((unsigned)(f) << 12) >> 28)
#define WF_FT_SHOF(f)  ((int)((unsigned)(f) << 8) >> 28)

/* Highlights h / Shadows s on a neutral-axis curve N (the film set's N, the Log + LUT tables' N), in place:
 *   N'[y] = N[y] + round((h x^2 (255 - x) + s x (255 - x)^2) / 385341), x = N[y]
 * (two cubic humps, peaks at x = 170 (h) and 85 (s), 0 at black and white, about 6.4 codes a step), rounded half away
 * from zero, kept non-decreasing and inside 0..255 (p = clamp(x + d, p, 255)). h = s = 0 leaves N as it is. The divisor
 * is a constant: a multiply on the camera, no __aeabi_uidiv. Build time only, never per pixel. */
void wf_hs_bend(uint8_t *N, int h, int s);

/* builds set t from the film's raw 17^3 table T (4913 words); ~8.7 M qemu instructions, ~45 ms at 200 MHz
 * (estimate, not measured on the camera) */
void wf_film_build(struct wf_ftabs *t, const uint32_t *T, int flags);
/* the shot set (worker task only): builds it for (T, key, flags) unless it already holds them. key: the card
 * table's load serial (wf_shot.c), never 0 for a table that was loaded; the host tests pass 0 after clearing src */
void wf_film_prepare(const uint32_t *T, uint32_t key, int flags);
/* the key and flags the shot set holds; 0 = none (src cleared) */
uint32_t wf_film_shot_key(void);
int wf_film_shot_flags(void);
/* the shot set's memory (sizeof(struct wf_ftabs), given once at start; 0 = none: film shots stay untouched) */
void wf_film_shot_set_mem(void *mem);
int wf_film_shot_ok(void);
/* wf_chunks callback: n UYVY words {U s8, Y0, V s8, Y1} in place; tabs = (int)(struct wf_ftabs *),
 * 0 = the shot set */
void wf_film_words(uint32_t *p, int n, int tabs);

#endif
