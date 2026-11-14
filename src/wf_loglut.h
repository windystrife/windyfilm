/* wf_loglut.h - "Log + LUT" output modes for WindyFilm (WindyStrife).
 *
 * The user puts a LUT made for camera log footage on the card (ML/DATA/SLOG3.CUB, VLOG.CUB,
 * LOGC3.CUB, CLOG2.CUB, CLOG3.CUB: input S-Log3/S-Gamut3.Cine, V-Log/V-Gamut, LogC3 EI800/AWG3,
 * Canon Log 2 / Canon Log 3 with Cinema Gamut; output Rec.709 display).
 * At load time (wf_task, never the pixel worker) wf_loglut_load() builds ONE composite
 *     camera JPEG YCbCr -> sRGB EOTF -> 3x3 to the log gamut -> log curve -> the user's cube
 *     (tetrahedral, at its native grid) -> R'G'B' -> YCbCr
 * evaluated directly at the node set of the shot kernel wf_ll_words() (wf_llk_arm.S; C reference
 * wf_ll_words_c in wf_llk.c):
 *     N[256]               the neutral axis (u = v = 128), per luma code
 *     C, Y at 33 x 33 x 33 nodes y = 0, 8, .., 256 x u, v = 0, 8, .., 256 (Y'CbCr codes)
 * wf_ll_words() interpolates them (triangle in u, v; linear in luma, per pixel) and adds N[y]: grey
 * is exact per pixel, colour detail is held at 8-code steps of Y', Cb and Cr. The tables and the
 * per-shot cost are the same for every cube size (2..65); only the load time grows with the cube.
 *
 * The .cube file is streamed (8 KB DMA reads + a 16 KB cached line buffer) and the cube is never
 * held in full: only two B-slabs (2 x N x N x RGB u16, 50.7 KB at 65^3) are kept, and every node
 * is evaluated when the slab pair around its blue coordinate is complete.
 *
 * wf_loglut_load_cached() keeps the built tables next to the cube (ML/DATA/<LOG>.FRT, 181 KB), so
 * only the first load of a new cube pays the parse. */
#ifndef WF_LOGLUT_H
#define WF_LOGLUT_H
#include <stdint.h>

#define WF_LL_NL      33                 /* luma levels: y = 0, 8, .., 256 (8 l) */
#define WF_LL_NC      33                 /* chroma nodes per axis: u, v = 0, 8, .., 256 */
#define WF_LL_LSTEP   (WF_LL_NC * WF_LL_NC)            /* 1089: index step of one luma level */
#define WF_LL_NODES   (WF_LL_NL * WF_LL_LSTEP)         /* 35937 */

/* the tables wf_ll_words() reads. Order and offsets matter to the kernel: N at offset 0, K, L and
 * Y (+ 1123) within 4095 bytes of the start (immediate load offsets); checked below. */
#define WF_LL_KOFF    768                /* E bias: byte N[y] + E + 768 of the struct = clamp(N[y] + E) */
struct wf_lltabs {
    uint8_t  N[256];                     /*     0  Y' of the neutral axis, per luma code */
    uint8_t  K[1280];                    /*   256  clamp: K[j] = clamp(j - 512, 0, 255) (N + E: -510..765) */
    int32_t  L[WF_LL_NL];                /*  1536  8 N[min(8 l, 255)] - 4 - 8 WF_LL_KOFF: neutral Y' x 8
                                                   at each level, minus the rounding and the K bias */
    uint8_t  Y[WF_LL_NODES];             /*  1668  Y' at the node, index l*1089 + (u/8)*33 + v/8 */
    uint8_t  pad[3];
    uint32_t C[WF_LL_NODES];             /* 37608  (2 (Cb + 128) + 1) | (2 (Cr + 128) + 1) << 16 */
};                                       /* 181356 bytes */
typedef char wf_lltabs_layout[(__builtin_offsetof(struct wf_lltabs, K) == 256
                               && __builtin_offsetof(struct wf_lltabs, L) == 1536
                               && __builtin_offsetof(struct wf_lltabs, Y) == 1668
                               && __builtin_offsetof(struct wf_lltabs, C) == 37608
                               && sizeof(struct wf_lltabs) == 181356) ? 1 : -1];

/* modes = the wf_log_words curve numbers */
#define WF_LL_SLOG3   1                  /* S-Log3 / S-Gamut3.Cine      ML/DATA/SLOG3.CUB */
#define WF_LL_VLOG    2                  /* V-Log / V-Gamut             ML/DATA/VLOG.CUB  */
#define WF_LL_LOGC3   3                  /* ARRI LogC3 EI800 / AWG3     ML/DATA/LOGC3.CUB */
#define WF_LL_CLOG2   4                  /* Canon Log 2 / Cinema Gamut  ML/DATA/CLOG2.CUB (owner 2026-11-13) */
#define WF_LL_CLOG3   5                  /* Canon Log 3 / Cinema Gamut  ML/DATA/CLOG3.CUB */
#define WF_LL_NCURVE  5                  /* curves 1..WF_LL_NCURVE */

/* LUT EI (menu "LUT EI", wf_menu.c): exposure of the + LUT outputs in 1/3 EV, WF_LL_EI_MIN..WF_LL_EI_MAX, 0 = none.
 * The builder scales scene-linear light by 2^(EI/3) before the log curve, like a log camera's exposure index: the
 * picture is metered as usual (no +EV on the camera) and JPEG white lands at log(2^(EI/3)) instead of log(1.0), so the
 * cube keeps the highlights a camera +EV would clip. wf_task sets it before wf_loglut_load(_cached); the .FRT key
 * holds it (EI 0: the key and the tables of a build without the setting). */
#define WF_LL_EI_MIN  (-3)
#define WF_LL_EI_MAX  6
extern int wf_ll_ei;
/* Auto exposure of a cube (V14, owner 2026-11-13): wf_task sets 1 for the Film LUT cubes (not Technical). The
 * builder measures the cube's neutral axis and scales scene light so 18 % grey (input 118) comes out at 118, then
 * the LUT EI on top. The result's pad[0] = that correction in 0.1 EV (int8), pad[1] = 0xa5; the .FRT key holds it. */
extern int wf_ll_auto;
#define WF_LL_AUTO_MARK 0xa5
/* Input curve (DESIGN 3.2 A1, menu "Input curve"): 0 = the JPEG is read as sRGB (as before), 1 = through the measured
 * Canon Picture Style curve (luts/canon_curve/, built in when build.sh found the CSV: wf_ll_canon_have()). Scene light
 * above 1.0 then goes through each curve's log segment, as the LUT EI does. 18 % grey is 0.18 of the curve for the
 * cube auto exposure. wf_task sets it before a load; the .FRT key holds it (0: the key as before). */
extern int wf_ll_incurve;
int wf_ll_canon_have(void);
const char *wf_ll_canon_src(void);               /* the CSV's first comment line, "" without */

/* results (distinct, for the log line and the menu warning) */
enum {
    WF_LL_OK = 0,
    WF_LL_E_OPEN,        /*  1 file missing or cannot be opened */
    WF_LL_E_READ,        /*  2 read error */
    WF_LL_E_NOMEM,       /*  3 no memory for the buffers */
    WF_LL_E_FSIZE,       /*  4 file larger than 32 MB */
    WF_LL_E_1D,          /*  5 LUT_1D_SIZE / LUT_1D_INPUT_RANGE: 1D LUT or 1D shaper + 3D */
    WF_LL_E_NOSIZE,      /*  6 no LUT_3D_SIZE before the data (or empty file) */
    WF_LL_E_SIZE,        /*  7 LUT_3D_SIZE not an integer 2..65, or given twice */
    WF_LL_E_DOMAIN,      /*  8 DOMAIN_MIN/MAX or LUT_3D_INPUT_RANGE unusable */
    WF_LL_E_SYNTAX,      /*  9 bad number / not 3 numbers on a data line / keyword after the data */
    WF_LL_E_TRUNC,       /* 10 fewer than N^3 samples, or the last line cut (truncated file) */
    WF_LL_E_EXTRA,       /* 11 more than N^3 samples */
    WF_LL_E_LINE,        /* 12 a data or keyword line longer than the 8 KB buffer (or CR-only file) */
    WF_LL_E_LINES,       /* 13 more than 65536 comment / blank / keyword lines */
    WF_LL_E_MODE,        /* 14 mode not 1..5 */
    WF_LL_E_N
};

struct wf_ll_info {
    int      n;                          /* LUT_3D_SIZE (0 = not seen) */
    int      line;                       /* error: line of the error (1-based); OK: lines read */
    int      samples;                    /* RGB samples read */
    int      clamped;                    /* sample values outside 0..1 (clamped) */
    int      skipped;                    /* unknown keyword lines ignored (before the data) */
    int      domain;                     /* 1 = a DOMAIN / LUT_3D_INPUT_RANGE other than 0..1 is applied */
    uint32_t bytes;                      /* bytes read */
    uint32_t peak;                       /* bytes allocated by the loader at its peak (not counting out) */
    int      cached;                     /* 1 = tables read from the table cache (wf_loglut_load_cached) */
    char     title[24];                  /* TITLE, truncated, for the log */
};

/* Loads `path` for log `mode` and fills *out (sizeof 181356 bytes, caller-owned, cacheable). On any
 * error *out is left half written: the caller must not use it (see INTEGRATION.md). Blocking:
 * file I/O (7.4 MB for a 65^3 file) plus the parse and the node build (see INTEGRATION.md for the
 * instruction counts). wf_task only. */
int wf_loglut_load(const char *path, int mode, struct wf_lltabs *out, struct wf_ll_info *info);

/* The same with a table cache next to the cube (cache = wf_loglut_cache_path(mode)): when the
 * cache was made by this builder from a cube with the same size, time stamp and content samples
 * (the first 8 KB and 16 spaced 512-byte blocks up to the end), its tables are read instead
 * (~1 M instructions + ~200 KB of card reads); otherwise the cube is loaded and, on success, the
 * cache rewritten (a failed write costs nothing but the next load's parse). The cache never
 * changes a result: a cube that fails to load fails, whatever the cache holds. info->cached = 1
 * on a cache hit. wf_task only. */
int wf_loglut_load_cached(const char *cube, const char *cache, int mode, struct wf_lltabs *out,
                          struct wf_ll_info *info);

/* card path for a mode ("ML/DATA/SLOG3.CUB", .. "ML/DATA/CLOG3.CUB"), 0 for a bad mode */
const char *wf_loglut_path(int mode);
/* card path of its table cache ("ML/DATA/SLOG3.FRT", ..), 0 for a bad mode */
const char *wf_loglut_cache_path(int mode);

/* Film LUT (menu "Film LUT", wf_menu.c wf_film_lut): which cube a Log + LUT mode loads. 0 Technical = the user's own
 * SLOG3.CUB / VLOG.CUB / LOGC3.CUB / CLOG2.CUB / CLOG3.CUB (the paths above); 1..4 the WindyFilm film cubes (negative +
 * print / scan, made from spektrafilm's CC BY-SA profiles): ML/DATA/<stem><S3|VL|LC|C2|C3>.CUB, each with its own .FRT
 * table cache next to it. V11 (owner 2026-11-13): 5..10 = six V-Log cubes of the owner's (Resolve 33^3, his own files,
 * never in git) under 8.3 names <stem>VL.CUB; their other names exist in the table but are not on the card (a missing
 * cube for those logs). V13: 11 EKTA P, the owner's own Resolve grade (EKTAP_*.CUB, not shipped, made by
 * luts_film/src/mk_usercube.py from his VL export, so all five logs can exist). V14T (owner 2026-11-13 "make films of
 * these, names starting with Tung"): 11 is "Tung EKTA P" and 12..22 the other eleven looks of the owner's purchased film
 * pack (V-Log 33^3 cubes, on the cards only, never in git), V-Log only like 5..10. */
#define WF_FLUT_VL0   5                       /* the first V-Log-only cube (V11: the owner's six, 5..10); every cube 1..22 has its
                                               * Film LUT can (WF_FLUT_CAT_N == WF_FLUT_N) */
#define WF_FLUT_VL_END 11                     /* first cube after the owner's six (11 EKTA P has every log) */
#define WF_FLUT_EKTAP 11
#define WF_FLUT_TUNG0 12                      /* V14T: the eleven Tung cubes 12..22, V-Log only */
#define WF_FLUT_N     23
#define WF_FLUT_VLONLY(l) (((l) >= WF_FLUT_VL0 && (l) < WF_FLUT_VL_END) || ((l) >= WF_FLUT_TUNG0 && (l) < WF_FLUT_N))
extern const char *wf_flut_name[WF_FLUT_N];           /* "Technical", "Vision3 250D", .. (menu choices, EXIF text) */
extern const char *const wf_flut_code[WF_FLUT_N];   /* "", "V250D", .. (the short form in the "shot #" log line) */
/* the cube (frt 0) or its .FRT table cache (frt 1) of output `mode` for Film LUT `lut`: "ML/DATA/<wf_flut_code padded with _ to 6
 * characters><S3|VL|LC|C2|C3>.CUB" built in b (WF_LL_NAME bytes, the caller's: wf_task holds its names for a 30 s load while the
 * menu builds others); lut 0 or out of range = the Technical paths above (fixed strings, b unused); 0 for a bad mode */
#define WF_LL_NAME 24
const char *wf_loglut_name(char *b, int mode, int lut, int frt);
/* short text for a result (<= 15 chars, for "LUT %s line %d" log lines and menu warnings) */
const char *wf_loglut_errstr(int err);

/* the shot kernel (wf_llk_arm.S on the camera, wf_llk.c elsewhere): wf_chunks callback, n UYVY words {U s8, Y0, V s8, Y1} in place,
 * tabs = (int)(struct wf_lltabs *), filled by a successful wf_loglut_load(_cached). No state. */
void wf_ll_words(uint32_t *p, int n, int tabs);
/* wf_llk.c: the C reference (= wf_ll_words without -DWF_LL_ASM) and the edge-pair word, which
 * wf_llk_arm.S calls */
void wf_ll_words_c(uint32_t *p, int n, int tabs);
uint32_t wf_ll_edge_w(const struct wf_lltabs *t, uint32_t wd);

/* parser alone (host tests): hdr is called once when the first data line is reached (n and the
 * domain known; a nonzero return aborts with that code), smp once per sample in file order
 * (R fastest), values u16 0..65535 = 0..1 after clamping. dmin/dmax are Q24. */
struct wf_ll_hdr { int n; int32_t dmin[3], dmax[3]; };
typedef int  wf_ll_hdr_fn(void *ctx, const struct wf_ll_hdr *h);
typedef void wf_ll_smp_fn(void *ctx, const uint16_t rgb[3]);
int wf_ll_parse(const char *path, wf_ll_hdr_fn *hdr, wf_ll_smp_fn *smp, void *ctx, struct wf_ll_info *info);

#endif
