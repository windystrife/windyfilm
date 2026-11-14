/* wf_test.c - WindyFilm host test (WindyStrife): the module's own camera objects (build.sh) driven the way the camera
 * drives them, under qemu-arm (test.sh). The ML / DryOS side is tests/host/host_ml.c; the camera layer is built with
 * -DWF_HOST (ROM words from the shim). Frames sit in the camera's shoot pool range (mmap at 0x48000000 / 0x4C000000,
 * a PROT_NONE page after each). Film tables are SYNTHETIC (identity and shifted 17^3 tables written into a test
 * WFFILMS.DAT): no table, cube or binary of anybody else is read.
 *
 * usage: wf_test MODE [CASE]      (in a fresh card directory: ML/DATA, ML/SETTINGS, ML/LOGS, DCIM/100CANON are made here)
 *   SHOT [nocam|busy]  film / strength / print-less shots bit-exact vs wf_film_build + wf_film_words, B&W, identity, aspects
 *                      and gates (EOS M), Diana halves, Log 1..5, Log + LUT (+ .FRT cache, can), Off, preview, keys
 *   FX                 grain + halation through the shot: ready keys, glow round a light, grain texture, no memory, free
 *   MIST               white / black mist: glow, veil, halves vs whole frame, seam
 *   EXIF               wf_ex_write on LE / BE files and odd files; the ring vs IMG_nnnn.JPG numbers (RAW-only gap, JPEG M)
 *   EXPSIM             dEV, smoothing, gain tables, ExpSim off / back / stuck, the display filter (EOS M; 5D2: stays off)
 *   CFG                WINDYFLM.CFG load (every key), save, garbage, oversize, userwb restore at unload
 *   PACK CASE          ok | missing | magic | index | top | fnv | code | dup | flags | real FILE
 *   LIGHT              Day / Cloudy / Tungsten / Auto, film kelvin, user WB back
 *   PRINT              Scan / Cine / Paper rows and shots, missing variants
 *   5D2                (5D2 build) TOP / BOT pairing: BOT alone, owed BOT, pairing timeout, r1 / RAW gates, Log halves
 *   PROBE              (EOS M build) RAW probe P0: off at boot, record + log + WFPROBE.BIN, uncached alias, out-of-pool,
 *                      CHANGED after the worker, ROM-call mode, gate reject probed, r2 != 0 not, frame never touched
 * Output: PASS / FAIL lines, then "RESULT MODE CASE: P pass, F fail"; exit status 1 on any failure. */
#define _GNU_SOURCE
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define WF_ML_HEADERS
#include "wf_core.h"
#include "wf_film.h"
#include "wf_loglut.h"
#include "wf_fx.h"
#include "wf_expsim.h"
#include "host_ml.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* ------------------------------------------------------------------ the camera under test */
#ifdef WF_BUILD_5D2
#define CAM_ID   WF_CAM_5D2
#define CAM_NAME "5D2"
#define CAM_FW   "2.1.2"
#define FW 5616
#define FH 3744
#define S_TOP 0xFF888884u
#define S_BOT 0xFF8867A0u
#define R_TOP 0
#define R_BOT 0
#define ROM_BAD S_TOP
#define NHOOKS 2
#define A_USED 0x140E000u                        /* half 0x140D600 B, page rounded */
#define B_USED 0x140E000u
#else
#define CAM_ID   WF_CAM_EOSM
#define CAM_NAME "EOSM"
#define CAM_FW   "2.0.2"
#define FW 5184
#define FH 3456
#define S_NN  0xFF169414u
#define S_TOP 0xFF17036Cu
#define S_BOT 0xFF16B580u
#define R_TOP 8
#define R_BOT 6
#define ROM_BAD S_NN
#define NHOOKS 3
#define A_USED 0x222C000u                        /* the NN frame 5184 x 3456 x 2 */
#define B_USED 0x1116000u                        /* a Diana half */
#endif
#define FRAME_A 0x48000000u
#define FRAME_B 0x4C000000u
#define SENT 0xD15EA5EDu
#define PROP_ASPECT 0x80040059u
#define NW (FW * FH / 2)

struct t_modinfo { uint8_t magic, major, minor, patch; const char *name, *long_name; unsigned (*init)(void); unsigned (*deinit)(void); };
struct t_cbr { const char *name, *symbol; unsigned type; unsigned (*handler)(unsigned); unsigned ctx; };
extern struct t_modinfo __module_info_windyflm;
extern struct t_cbr __module_cbr_windyflm[];
extern const char *wf_ex_write(const char *path, const char *txt);
extern const int wf_t_li_size, wf_t_card_fn, wf_t_card_size;

/* ------------------------------------------------------------------ results */
static int npass, nfail;
static void chk(int ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf(ok ? "PASS " : "FAIL ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    if (ok) npass++; else nfail++;
}
#define WAIT(cond, ms) ({ int _ok = 0; for (int _t = 0; _t <= (ms); _t += 10) { if (cond) { _ok = 1; break; } usleep(10000); } _ok; })

/* ------------------------------------------------------------------ files */
static void mkdirs(void)
{
    mkdir("ML", 0777); mkdir("ML/DATA", 0777); mkdir("ML/SETTINGS", 0777); mkdir("ML/LOGS", 0777);
    mkdir("DCIM", 0777); mkdir("DCIM/100CANON", 0777);
}
static void put_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n) { fprintf(stderr, "cannot write %s\n", path); exit(3); }
    fclose(f);
}
static char *get_file(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { *n = 0; return 0; }
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(s + 1);
    *n = (long)fread(b, 1, s, f);
    b[*n] = 0;
    fclose(f);
    return b;
}
static void put_cfg(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    put_file("ML/SETTINGS/WINDYFLM.CFG", b, strlen(b));
}

/* the module's log: lines after the mark */
#define LOG_PATH "ML/LOGS/WINDYFLM.LOG"
static long log_at;
static void log_mark(void) { long n; free(get_file(LOG_PATH, &n)); log_at = n; }
static int log_has(const char *s)
{
    long n;
    char *b = get_file(LOG_PATH, &n);
    int r = b && n > log_at && strstr(b + log_at, s) != 0;
    free(b);
    return r;
}
#define LOGW(s, ms) WAIT(log_has(s), ms)
/* the last line after the mark that contains s, "" none (static copy) */
static const char *log_line(const char *s)
{
    static char lb[4][256];
    static int li;
    char *l = lb[li++ & 3];                    /* four buffers: several calls in one printf */
    long n;
    char *b = get_file(LOG_PATH, &n), *p = b ? b + (n > log_at ? log_at : n) : 0, *last = 0;
    l[0] = 0;
    for (char *q; p && (q = strstr(p, s)); p = q + 1)
        last = q;
    if (last) {
        while (last > b && last[-1] != '\n') last--;
        int i = 0;
        while (last[i] && last[i] != '\n' && i < 255) { l[i] = last[i]; i++; }
        l[i] = 0;
    }
    free(b);
    return l;
}
static void log_tail(int lines)
{
    long n;
    char *b = get_file(LOG_PATH, &n);
    if (!b) return;
    char *p = b + n;
    for (int k = 0; p > b && k <= lines; p--)
        if (p[-1] == '\n') k++;
    printf("---- %s (last lines)\n%s----\n", LOG_PATH, p);
    free(b);
}

/* ------------------------------------------------------------------ synthetic 17^3 tables and WFFILMS.DAT */
#define TW 4913
static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
/* words {U s8, Y, V s8, 0}, node i at code min(16 i, 255): identity + (dy, du, dv), mono = U' = V' = 0 */
static uint32_t *tab_make(int dy, int du, int dv, int mono)
{
    uint32_t *T = malloc(TW * 4);
    for (int yi = 0; yi < 17; yi++)
        for (int ui = 0; ui < 17; ui++)
            for (int vi = 0; vi < 17; vi++) {
                int y = clampi(16 * yi, 0, 255), u = clampi(16 * ui, 0, 255) - 128, v = clampi(16 * vi, 0, 255) - 128;
                int y2 = clampi(y + dy, 0, 255), u2 = mono ? 0 : clampi(u + du, -128, 127), v2 = mono ? 0 : clampi(v + dv, -128, 127);
                T[yi * 289 + ui * 17 + vi] = (uint32_t)(uint8_t)u2 | (uint32_t)y2 << 8 | (uint32_t)(uint8_t)v2 << 16;
            }
    return T;
}
static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) { while (n--) h = (h ^ *p++) * 16777619u; return h; }
static void le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

struct pk { int k, v; const uint32_t *T; };
static const uint32_t *tabs[128][3];             /* the table of (catalogue row, variant) written into the pack */
/* bad: 0 good, 'M' magic, 'I' index checksum, 'T' a word with its top byte set (entry 0), 'F' entry 0's checksum,
 *      'C' entry 0's code, 'D' entry 0 twice (the second with another table), 'G' entry 0's flags */
static void pack_write(const struct pk *e, int n, int bad)
{
    int ne = n + (bad == 'D');
    uint32_t doff = 32 + 24 * ne, sz = doff + ne * TW * 4;
    uint8_t *b = calloc(1, sz);
    memcpy(b, "WFFT", 4);
    b[4] = 1; b[6] = ne; b[7] = ne >> 8;
    le32(b + 8, TW); le32(b + 12, TW * 4); le32(b + 16, 32); le32(b + 20, doff);
    uint32_t *other = tab_make(-50, 0, 0, 0);
    for (int i = 0; i < ne; i++) {
        const struct pk *x = &e[i < n ? i : 0];
        const uint32_t *T = i < n ? x->T : other;
        uint8_t *ix = b + 32 + 24 * i, *d = b + doff + i * TW * 4;
        const struct wf_film *f = &wf_films[x->k];
        for (int w = 0; w < TW; w++) le32(d + 4 * w, T[w]);
        if (i == 0 && bad == 'T') d[4 * 100 + 3] = 1;
        ix[0] = f->id; ix[1] = x->v; ix[2] = wf_cat_ftflags[x->k] ^ (i == 0 && bad == 'G' ? 1 : 0);
        strncpy((char *)ix + 4, f->code, 8);
        if (i == 0 && bad == 'C') ix[4] ^= 0x20;
        le32(ix + 12, doff + i * TW * 4); le32(ix + 16, TW * 4);
        le32(ix + 20, fnv(2166136261u, d, TW * 4) ^ (i == 0 && bad == 'F'));
        if (i < n) tabs[x->k][x->v] = T;
    }
    le32(b + 24, fnv(2166136261u, b + 32, 24 * ne) ^ (bad == 'I'));
    if (bad == 'M') b[3] = 'X';
    put_file("ML/DATA/WFFILMS.DAT", b, sz);
    free(b);
    free(other);
}

/* a .cube with the identity at size n (R fastest) */
static void cube_write(const char *path, int n)
{
    FILE *f = fopen(path, "w");
    fprintf(f, "TITLE \"identity %d\"\nLUT_3D_SIZE %d\n", n, n);
    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++)
                fprintf(f, "%.6f %.6f %.6f\n", r / (double)(n - 1), g / (double)(n - 1), b / (double)(n - 1));
    fclose(f);
}

/* ------------------------------------------------------------------ catalogue picks */
static int fA = -1, fB = -1, fC = -1, fX = -1, fCan = -1;    /* rows: colour + 3 prints, B&W, colour, not in pack, a can */
static int colour_film(int k) { return (wf_films[k].flags & (WF_F_READY | WF_F_LUTCAN | WF_F_BW)) == WF_F_READY; }
static void pick_films(void)
{
    for (int k = 0; k < wf_nfilms; k++) {
        const struct wf_film *f = &wf_films[k];
        if (fA < 0 && colour_film(k) && (f->prints & 7) == 7 && (wf_cat_ftflags[k] & WF_FT_V21)) fA = k;
        else if (fB < 0 && (f->flags & (WF_F_READY | WF_F_BW | WF_F_LUTCAN)) == (WF_F_READY | WF_F_BW)) fB = k;
        else if (fCan < 0 && (f->flags & WF_F_LUTCAN) && wf_cat_lut[k] > 0) fCan = k;
    }
    for (int k = 0; k < wf_nfilms; k++)
        if (k != fA && colour_film(k) && wf_films[k].prints == 1) { if (fC < 0) fC = k; else if (fX < 0) { fX = k; break; } }
    if (fA < 0 || fB < 0 || fC < 0 || fX < 0 || fCan < 0) {
        printf("FAIL catalogue picks: A %d B %d C %d X %d can %d\n", fA, fB, fC, fX, fCan);
        exit(1);
    }
    printf("films: A %s (id %d, prints %d), B %s (B&W), C %s, X %s (not in the pack), can %s (Film LUT %d)\n",
           wf_films[fA].code, wf_films[fA].id, wf_films[fA].prints, wf_films[fB].code, wf_films[fC].code,
           wf_films[fX].code, wf_films[fCan].code, wf_cat_lut[fCan]);
}
static void std_pack(void)
{
    struct pk e[] = {
        { fA, 0, tab_make(12, 6, -6, 0) }, { fA, 1, tab_make(-10, 0, 0, 0) }, { fA, 2, tab_make(24, 0, 0, 0) },
        { fB, 0, tab_make(0, 0, 0, 1) }, { fC, 0, tab_make(0, 0, 0, 0) },
    };
    pack_write(e, 5, 0);
}

/* ------------------------------------------------------------------ frames */
static uint32_t *fa, *fb, *in, *out, *ref, *out2;
static uint32_t *map_at(uint32_t a, uint32_t used)
{
    void *p = mmap((void *)(uintptr_t)a, used + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)(uintptr_t)a) {
        if (p != MAP_FAILED) munmap(p, used + 4096);
        p = mmap((void *)(uintptr_t)a, used + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    if (p != (void *)(uintptr_t)a) { fprintf(stderr, "cannot map the frame at %x (%p)\n", (unsigned)a, p); exit(3); }
    mprotect((char *)p + used, 4096, PROT_NONE);  /* a write past the buffer faults */
    return p;
}
static uint32_t rs = 0x12345678u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint32_t word(int u, int y0, int v, int y1)
{
    return (uint32_t)(uint8_t)clampi(u, -128, 127) | (uint32_t)clampi(y0, 0, 255) << 8
         | (uint32_t)(uint8_t)clampi(v, -128, 127) << 16 | (uint32_t)clampi(y1, 0, 255) << 24;
}
#define WY0(w) ((int)((w) >> 8 & 255))
#define WY1(w) ((int)((w) >> 24))
#define WU(w)  ((int)(int8_t)(w))
#define WV(w)  ((int)(int8_t)((w) >> 16))
/* kind 0: gradients + noise, every code range; kind 1: dark grey + noise with a light (Y 255) disc of radius r at (cx, cy) */
static int disc_r = 60;
static void gen_frame(uint32_t *p, int w, int h, int kind)
{
    int wpr = w / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < wpr; x++) {
            uint32_t r = rnd();
            if (kind == 0) {
                int yb = 8 + (2 * x * 230) / w + (int)(r & 15) - 8, uv = (y * 200) / h - 100;
                p[y * wpr + x] = word(uv + (int)(r >> 8 & 15) - 8, yb + (int)(r >> 12 & 7), (x * 180) / wpr - 90 + (int)(r >> 16 & 15) - 8,
                                      yb + (int)(r >> 20 & 7));
            } else {
                int dx = 2 * x - w / 2, dy = y - h / 2, in_d = dx * dx + dy * dy <= disc_r * disc_r;
                p[y * wpr + x] = in_d ? word(0, 255, 0, 255)
                                      : word((int)(r & 3) - 2, 40 + (int)(r >> 4 & 7) - 4, (int)(r >> 8 & 3) - 2, 40 + (int)(r >> 12 & 7) - 4);
            }
        }
}
static int ndiff(const uint32_t *a, const uint32_t *b, int n)
{
    int d = 0;
    for (int i = 0; i < n; i++) d += a[i] != b[i];
    return d;
}
static double mean_dy(const uint32_t *a, const uint32_t *b, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++) s += WY0(b[i]) - WY0(a[i]) + WY1(b[i]) - WY1(a[i]);
    return s / (2.0 * n);
}
static int max_d(const uint32_t *a, const uint32_t *b, int n)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        int d[4] = { WY0(b[i]) - WY0(a[i]), WY1(b[i]) - WY1(a[i]), WU(b[i]) - WU(a[i]), WV(b[i]) - WV(a[i]) };
        for (int j = 0; j < 4; j++) { if (d[j] < 0) d[j] = -d[j]; if (d[j] > m) m = d[j]; }
    }
    return m;
}
/* mean of (b - a) over the pixels of an annulus r0 <= r < r1 round the frame centre (w x h frame), lane 'y' / 'v' */
static double ring(const uint32_t *a, const uint32_t *b, int w, int h, int r0, int r1, int lane, double *sd)
{
    double s = 0, s2 = 0;
    long n = 0;
    int wpr = w / 2;
    for (int y = h / 2 - r1; y <= h / 2 + r1; y++)
        for (int x = (w / 2 - r1) / 2; x <= (w / 2 + r1) / 2; x++) {
            int dx = 2 * x - w / 2, dy = y - h / 2, d2 = dx * dx + dy * dy;
            if (y < 0 || y >= h || x < 0 || x >= wpr || d2 < r0 * r0 || d2 >= r1 * r1) continue;
            uint32_t A = a[y * wpr + x], B = b[y * wpr + x];
            double d = lane == 'y' ? (WY0(B) - WY0(A) + WY1(B) - WY1(A)) * 0.5 : WV(B) - WV(A);
            s += d; s2 += d * d; n++;
        }
    if (!n) return 0;
    if (sd) *sd = sqrt(s2 / n - (s / n) * (s / n));
    return s / n;
}
/* the mean (b - a) luma of a box of rows y0..y1, all words */
static double rows_dy(const uint32_t *a, const uint32_t *b, int w, int y0, int y1)
{
    int wpr = w / 2;
    return mean_dy(a + y0 * wpr, b + y0 * wpr, (y1 - y0 + 1) * wpr);
}
/* a corner box (dark background, far from the disc): mean and sd of (b - a) luma */
static double corner(const uint32_t *a, const uint32_t *b, int w, double *sd)
{
    double s = 0, s2 = 0;
    long n = 0;
    int wpr = w / 2;
    for (int y = 100; y < 400; y++)
        for (int x = 50; x < 200; x++) {
            uint32_t A = a[y * wpr + x], B = b[y * wpr + x];
            double d = (WY0(B) - WY0(A) + WY1(B) - WY1(A)) * 0.5;
            s += d; s2 += d * d; n++;
        }
    if (sd) *sd = sqrt(s2 / n - (s / n) * (s / n));
    return s / n;
}

/* ------------------------------------------------------------------ references: the module's kernels called directly */
static struct wf_ftabs ft_ref;
static void ref_film(uint32_t *p, int n, const uint32_t *T, int flags)
{
    wf_film_build(&ft_ref, T, flags);
    for (int o = 0; o < n; o += WF_CHUNK)
        wf_film_words(p + o, n - o < WF_CHUNK ? n - o : WF_CHUNK, (int)(uintptr_t)&ft_ref);
}
static void ref_fn(uint32_t *p, int n, wf_words_fn *fn, int arg)
{
    for (int o = 0; o < n; o += WF_CHUNK)
        fn(p + o, n - o < WF_CHUNK ? n - o : WF_CHUNK, arg);
}
static int flags_of(int k, int level) { return wf_cat_ftflags[k] | WF_FT_S(wf_strength_pct(level)); }
/* Log + LUT tables equal in what the kernel reads (pad holds the auto-exposure note only) */
static int ll_same(const struct wf_lltabs *a, const void *pb)
{
    const struct wf_lltabs *b = pb;
    return b && !memcmp(a, b, offsetof(struct wf_lltabs, pad)) && !memcmp(a->C, b->C, sizeof a->C);
}

/* ------------------------------------------------------------------ the module */
static unsigned (*cbr_key)(unsigned), (*cbr_disp)(unsigned);
static int halves;                               /* EOS M: Diana TOP + BOT instead of NN */
static uint32_t regs[16], stk[64];

static void call_hook(uint32_t site)
{
    h_hook_fn *f = h_hook_at(site);
    if (!f) { chk(0, "hook at %x registered", (unsigned)site); return; }
    f(regs, stk, site);
}
static int sentinel_bad(const uint32_t *p, uint32_t from_words, uint32_t used)
{
    int bad = 0;
    for (uint32_t i = from_words; i < used / 4; i++) bad += p[i] != SENT;
    return bad;
}
/* one shot of a w x h frame through the hooks (in -> out); returns the number of words written outside the frame */
static int shot(const uint32_t *src, uint32_t *dst, int w, int h)
{
    int n = w * h / 2, nh = n / 2, bad;
    for (uint32_t i = 0; i < A_USED / 4; i++) fa[i] = SENT;
    for (uint32_t i = 0; i < B_USED / 4; i++) fb[i] = SENT;
#ifdef WF_BUILD_5D2
    memcpy(fa, src, nh * 4);
    memcpy(fb, src + nh, (n - nh) * 4);
    memset(regs, 0, sizeof regs); regs[R_TOP] = FRAME_A; call_hook(S_TOP);
    memset(regs, 0, sizeof regs); regs[R_BOT] = FRAME_B; call_hook(S_BOT);
    memcpy(dst, fa, nh * 4);
    memcpy(dst + nh, fb, (n - nh) * 4);
    bad = sentinel_bad(fa, nh, A_USED) + sentinel_bad(fb, n - nh, B_USED);
#else
    if (!halves) {
        memcpy(fa, src, n * 4);
        memset(regs, 0, sizeof regs); regs[1] = FRAME_A; regs[2] = 0; call_hook(S_NN);
        memcpy(dst, fa, n * 4);
        bad = sentinel_bad(fa, n, A_USED) + sentinel_bad(fb, 0, B_USED);
    } else {
        memcpy(fa, src, nh * 4);
        memcpy(fb, src + nh, (n - nh) * 4);
        memset(regs, 0, sizeof regs); regs[R_TOP] = FRAME_A; call_hook(S_TOP);
        memset(regs, 0, sizeof regs); regs[R_BOT] = FRAME_B; call_hook(S_BOT);
        memcpy(dst, fa, nh * 4);
        memcpy(dst + nh, fb, (n - nh) * 4);
        bad = sentinel_bad(fa, nh, A_USED) + sentinel_bad(fb, n - nh, B_USED);
    }
#endif
    return bad;
}

static void mod_init(int want_path)
{
    struct t_modinfo *mi = &__module_info_windyflm;
    chk(mi->magic == 0x5A && mi->major == 8 && !strcmp(mi->name, "windyflm") && mi->init && mi->deinit,
        "module_info: magic %#x api %d name %s", mi->magic, mi->major, mi->name);
    for (int i = 0; i < 8 && __module_cbr_windyflm[i].name; i++) {
        if (__module_cbr_windyflm[i].type == 7) cbr_key = __module_cbr_windyflm[i].handler;
        if (__module_cbr_windyflm[i].type == 4) cbr_disp = __module_cbr_windyflm[i].handler;
    }
    chk(cbr_key && cbr_disp, "CBR_KEYPRESS and CBR_DISPLAY_FILTER registered");
    log_at = 0;
    chk(mi->init() == 0, "init returns 0");
    if (!want_path)
        return;
    chk(WAIT(wf_cam_shot_ok(), 20000), "worker + hooks up (%d hooks)", h_nhooks);
    chk(WAIT(wf_ws_state() != 0, 20000), "pack index read (state %d)", wf_ws_state());
    usleep(200000);                              /* the log lines of the start reach the card */
}
static void mod_deinit(void)
{
    log_mark();
    chk(__module_info_windyflm.deinit() == 0, "deinit returns 0");
    chk(LOGW("unload", 5000), "log: unload");
}
static int look_table_ready(void)
{
    struct wf_look lk;
    return wf_look_now(&lk) != WF_OUT_FILM || wf_ws_table(lk.row, lk.variant) != 0;
}

/* a film shot of the current look; checks bit-exact vs the direct kernel, the mean luma shift, the log status */
static void film_shot(const char *what, int k, int v, int level, double dy, double tol)
{
    chk(WAIT(look_table_ready(), 10000), "%s: table published", what);
    log_mark();
    int sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[k][v], flags_of(k, level));
    int d = ndiff(out, ref, NW);
    double m = mean_dy(in, out, NW);
    chk(d == 0 && sb == 0, "%s: bit-exact vs wf_film_build + wf_film_words (%d words differ, %d written outside)", what, d, sb);
    chk(fabs(m - dy) <= tol, "%s: mean dY %+.2f (want %+.1f +- %.1f)", what, m, dy, tol);
    chk(LOGW(": film ", 3000), "%s: log '%s'", what, log_line(": film "));
}
/* a shot that must leave the frame as it was */
static void plain_shot(const char *what, const char *logs)
{
    log_mark();
    int sb = shot(in, out, FW, FH);
    chk(ndiff(in, out, NW) == 0 && sb == 0, "%s: frame untouched", what);
    if (logs)
        chk(LOGW(logs, 3000), "%s: log has '%s'", what, logs);
}

/* ------------------------------------------------------------------ SHOT */
static void t_shot(const char *cs)
{
    if (!strcmp(cs, "nocam")) {                  /* a body whose ROM word differs: status only, nothing touched */
        h_rom_bad = ROM_BAD;
        put_cfg("light=day\nuserwb=1,5600,2,-3\n");
        std_pack();
        mod_init(0);
        usleep(500000);
        chk(wf_cam() == WF_CAM_NONE && !strcmp(wf_status(), "Unsupported firmware"), "unsupported ROM: wf_cam %d, status '%s'",
            wf_cam(), wf_status());
        chk(h_nhooks == 0 && !wf_cam_shot_ok(), "no hook installed");
        chk(wf_row_step(WF_ROW_LIGHT, 1) == 0 && wf_load(wf_films[fA].id) == WF_LOAD_BUSY && !wf_avail(wf_films[fA].id),
            "Light / load / avail refuse");
        chk(wf_row_avail(WF_ROW_GRAIN) == 0, "rows greyed");
        mod_deinit();
        chk(h_kelvin_n == 0 && h_nchanges == 0 && h_gm_last == 0 && h_ba_last == 0, "white balance never touched");
        return;
    }
    if (!strcmp(cs, "busy")) {                   /* the old module holds the hook site */
        h_patch_err = 2;
        std_pack();
        mod_init(0);
        chk(LOGW("hook failed", 10000), "log: hook failed");
        chk(!strcmp(wf_status(), "Disable filmroll.mo") && !wf_cam_shot_ok(), "status '%s'", wf_status());
        mod_deinit();
        return;
    }
    std_pack();
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\npreview=on\n", wf_films[fA].id);
    mod_init(1);
    chk(h_nhooks == NHOOKS, "%d hooks installed", h_nhooks);
    chk(wf_cam() == CAM_ID && !*wf_status(), "wf_cam %d, status '%s'", wf_cam(), wf_status());
    chk(LOGW("pack ok: 5 tables", 5000), "log: %s", log_line("pack ok"));
    chk(wf_loaded() == wf_films[fA].id && wf_avail(wf_films[fA].id) && wf_avail(wf_films[fB].id) && !wf_avail(wf_films[fX].id),
        "loaded %d, avail A B, not X", wf_loaded());
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    gen_frame(in, FW, FH, 0);
    film_shot("film A scan", fA, 0, 1, 12, 1.5);

    /* preview on the same look */
    chk(WAIT(wf_pv_state() == WF_PV_READY, 10000), "preview ready (state %d)", wf_pv_state());
    chk(cbr_disp(0) == 1, "display filter: run");
    {
        enum { LW = 360, LH = 480 };
        uint32_t *src = malloc(LW * LH * 4), *dst = malloc(LW * LH * 4), *pr = malloc(LW * LH * 4);
        gen_frame(src, 720, LH, 0);
        uint32_t *bufs[2] = { src, dst };
        chk(cbr_disp((unsigned)(uintptr_t)bufs) == 1, "display filter: frame");
        memcpy(pr, src, LW * LH * 4);
        ref_film(pr, LW * LH, tabs[fA][0], flags_of(fA, 1));
        chk(ndiff(dst, pr, LW * LH) == 0, "preview: bit-exact vs the shot's kernel (%d words differ)", ndiff(dst, pr, LW * LH));
        wf_pv_mode = WF_PV_FAST;
        memset(dst, 0, LW * LH * 4);
        cbr_disp((unsigned)(uintptr_t)bufs);
        int fe = 0;
        for (int y = 0; y < LH; y += 2) {
            fe += ndiff(dst + y * LW, pr + y * LW, LW);
            fe += ndiff(dst + (y + 1) * LW, dst + y * LW, LW);
        }
        chk(fe == 0, "preview Fast: even rows graded, odd rows copies (%d words differ)", fe);
        wf_pv_mode = WF_PV_ON;
        pic_quality = 0x04000000;
        chk(wf_pv_state() == WF_PV_GATE && cbr_disp(0) == 0, "preview off for RAW only (state %d)", wf_pv_state());
        pic_quality = 0;
        h_play = 1;
        chk(wf_pv_state() == WF_PV_PLAY && cbr_disp(0) == 0, "preview off in playback");
        h_play = 0;
        free(src); free(dst); free(pr);
    }

    /* Strength */
    chk(wf_row_step(WF_ROW_STRENGTH, -1) == 1 && !strcmp(wf_row_text(WF_ROW_STRENGTH), "30%"), "Strength 30%%");
    film_shot("strength 30%", fA, 0, 0, 12 * 0.3, 1.2);
    chk(wf_row_step(WF_ROW_STRENGTH, -1) == 0, "Strength bar: no wrap below 30%%");
    wf_row_step(WF_ROW_STRENGTH, 1);
    wf_row_step(WF_ROW_STRENGTH, 1);
    chk(!strcmp(wf_row_text(WF_ROW_STRENGTH), "125%"), "Strength 125%%");
    film_shot("strength 125%", fA, 0, 2, 12 * 1.25, 1.5);
    wf_row_step(WF_ROW_STRENGTH, -1);

    /* B&W and identity */
    chk(wf_load(wf_films[fB].id) == WF_LOAD_OK && wf_loaded() == wf_films[fB].id, "load B&W film");
    film_shot("B&W (mono table)", fB, 0, 1, 0, 1.0);
    {
        double su = 0;
        for (int i = 0; i < NW; i += 7) su += abs(WU(out[i])) + abs(WV(out[i]));
        chk(su / (NW / 7) < 1.0, "B&W: chroma 0 (mean |U|+|V| %.2f)", su / (NW / 7));
    }
    chk(wf_load(wf_films[fC].id) == WF_LOAD_OK, "load film C");
    film_shot("identity table", fC, 0, 1, 0, 0.5);
    chk(max_d(in, out, NW) <= 2, "identity table: every code within 2 (max %d)", max_d(in, out, NW));
    chk(wf_load(wf_films[fX].id) == WF_LOAD_NOTABLE && wf_load(1) == WF_LOAD_UNKNOWN, "load: no table / unknown id");

#ifndef WF_BUILD_5D2
    /* JPEG aspects: the frame packed at the aspect's size, nothing written past it */
    static const int as[3][3] = { { 1, 3456, 3456 }, { 2, 4608, 3456 }, { 7, 5184, 2912 } };
    for (int i = 0; i < 3; i++) {
        int w = as[i][1], h = as[i][2], n = w * h / 2;
        h_prop_send(PROP_ASPECT, as[i][0]);
        gen_frame(in, w, h, 0);
        int sb = shot(in, out, w, h);
        memcpy(ref, in, n * 4);
        ref_film(ref, n, tabs[fC][0], flags_of(fC, 1));
        chk(ndiff(out, ref, n) == 0 && sb == 0, "aspect %d (%dx%d): bit-exact, %d words written outside", as[i][0], w, h, sb);
    }
    h_prop_send(PROP_ASPECT, 0);
    gen_frame(in, FW, FH, 0);

    /* gates */
    pic_quality = 0x0100;  plain_shot("JPEG M", "reject"); pic_quality = 0;
    pic_quality = 0x04000000; plain_shot("RAW only", "reject"); pic_quality = 0;
    shooting_mode = 0x18; plain_shot("HDR sub-frames", "reject"); shooting_mode = 3;
    {
        log_mark();
        memcpy(fa, in, NW * 4);
        memset(regs, 0, sizeof regs); regs[1] = FRAME_A; regs[2] = 1; call_hook(S_NN);
        chk(ndiff(fa, in, NW) == 0, "NN call with r2 != 0: untouched");
        memset(regs, 0, sizeof regs); regs[1] = 0x30000000u; call_hook(S_NN);
        chk(LOGW("reject 30000000", 3000), "buffer outside the pool rejected: %s", log_line("reject 3"));
        memset(regs, 0, sizeof regs); regs[1] = 0; call_hook(S_NN);
        chk(LOGW("reject 0 ", 3000), "NULL buffer rejected: %s", log_line("reject 0 "));
    }
    /* Diana halves */
    halves = 1;
    film_shot("Diana TOP + BOT", fC, 0, 1, 0, 0.5);
    chk(LOGW("(TOP ", 3000), "shot line of a halves pair: %s", log_line("(TOP "));
    halves = 0;
#endif

    /* plain log */
    for (int c = 1; c <= WF_NCURVE; c++) {
        char what[32];
        snprintf(what, sizeof what, "log curve %d", c);
        wf_curve = c;
        wf_out = WF_OUT_LOG;
        log_mark();
        int sb = shot(in, out, FW, FH);
        memcpy(ref, in, NW * 4);
        ref_fn(ref, NW, wf_log_words, c);
        chk(ndiff(out, ref, NW) == 0 && sb == 0 && ndiff(out, in, NW) > NW / 2, "%s: bit-exact vs wf_log_words", what);
        chk(LOGW(": log ", 3000), "%s: log '%s'", what, log_line(": log "));
    }

    /* Log + LUT: the user's V-Log cube (identity) */
    cube_write("ML/DATA/VLOG.CUB", 9);
    wf_curve = WF_CURVE_VLOG;
    wf_flut = 0;
    log_mark();
    wf_out = WF_OUT_LLUT;
    chk(WAIT(wf_ll_have_curve() == WF_CURVE_VLOG, 60000), "Log + LUT tables loaded (%s)", log_line("lut "));
    {
        static struct wf_lltabs ll;
        struct wf_ll_info inf;
        int rc = wf_loglut_load("ML/DATA/VLOG.CUB", WF_CURVE_VLOG, &ll, &inf);
        chk(rc == WF_LL_OK && inf.n == 9 && ll_same(&ll, wf_ll_tables()), "published tables = wf_loglut_load (rc %d)", rc);
        int sb = shot(in, out, FW, FH);
        memcpy(ref, in, NW * 4);
        ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&ll);
        chk(ndiff(out, ref, NW) == 0 && sb == 0, "Log + LUT shot: bit-exact vs wf_ll_words");
        uint32_t *a = malloc(65536 * 4), *b = malloc(65536 * 4);
        for (int i = 0; i < 65536; i++) a[i] = b[i] = rnd();
        wf_ll_words(a, 65536, (int)(uintptr_t)&ll);
        wf_ll_words_c(b, 65536, (int)(uintptr_t)&ll);
        chk(ndiff(a, b, 65536) == 0, "wf_ll_words (asm) = wf_ll_words_c on random words");
        free(a); free(b);
        struct stat st;
        chk(stat("ML/DATA/VLOG.FRT", &st) == 0 && st.st_size > 181356, "table cache ML/DATA/VLOG.FRT written");
        log_mark();
        wf_curve = 1;                            /* no SLOG3.CUB on the card */
        chk(LOGW("lut SLOG3.CUB err 1", 20000) && wf_ll_have_curve() == 0, "missing cube: %s", log_line("lut "));
        log_mark();
        wf_curve = WF_CURVE_VLOG;
        chk(LOGW(" cache ", 20000) && WAIT(wf_ll_have_curve() == WF_CURVE_VLOG, 5000) && ll_same(&ll, wf_ll_tables()),
            "reload from the cache, same tables: %s", log_line("lut "));
        /* a Film LUT can: its cube under its own name */
        int id = wf_films[fCan].id;
        chk(wf_load(id) == WF_LOAD_NOCUBE, "can without its cube: no cube");
        char nb[WF_LL_NAME];
        cube_write(wf_loglut_name(nb, WF_CURVE_VLOG, wf_cat_lut[fCan], 0), 5);
        log_mark();
        chk(wf_load(id) == WF_LOAD_OK && wf_loaded() == id && wf_out == WF_OUT_LLUT && wf_flut == wf_cat_lut[fCan], "load the can");
        chk(LOGW("lut ", 60000) && WAIT(wf_ll_have_curve() == WF_CURVE_VLOG && wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000),
            "can's cube loaded: %s", log_line("lut "));
        sb = shot(in, out, FW, FH);
        memcpy(ref, in, NW * 4);
        ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)wf_ll_tables());
        chk(ndiff(out, ref, NW) == 0 && sb == 0, "can shot: bit-exact vs wf_ll_words on the published tables");

        /* Input curve Canon Film 1 (DESIGN 3.2 A1), on the technical V-Log cube (identity): only the decode differs, so
         * the curve's 18 % grey (code 139 -> 0.18) must come out where sRGB's (118) does, and its light above 1.0
         * (white 2.07) above sRGB white through the log segment */
        log_mark();
        wf_flut = 0;
        chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&ll, wf_ll_tables()),
            "back to the technical cube, sRGB: %s", log_line("lut "));
        chk(wf_incurve_t() == WF_INC_SRGB && !strstr(log_line("lut "), "in-film1"), "Input curve sRGB by default");
        enum { GN = 64 };
        static const uint8_t gy[4] = { 118, 255, 139, 250 };
        uint32_t gs[GN], gc[GN];
        for (int i = 0; i < GN; i++) gs[i] = gc[i] = word(0, gy[i & 3], 0, gy[i & 3]);
        wf_ll_words_c(gs, GN, (int)(uintptr_t)&ll);
        if (wf_ll_canon_have()) {
            static struct wf_lltabs lc;
            log_mark();
            wf_incurve = WF_INC_FILM1;
            chk(LOGW(" in-film1", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000),
                "Input curve Film 1 loaded: %s", log_line("lut "));
            wf_ll_incurve = 1;
            rc = wf_loglut_load("ML/DATA/VLOG.CUB", WF_CURVE_VLOG, &lc, &inf);
            chk(rc == WF_LL_OK && ll_same(&lc, wf_ll_tables()) && !ll_same(&ll, &lc),
                "Film 1 tables = wf_loglut_load with the curve, not the sRGB ones (rc %d)", rc);
            sb = shot(in, out, FW, FH);
            memcpy(ref, in, NW * 4);
            ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&lc);
            chk(ndiff(out, ref, NW) == 0 && sb == 0, "Film 1 shot: bit-exact vs wf_ll_words on its tables");
            wf_ll_words_c(gc, GN, (int)(uintptr_t)&lc);
            int y18s = WY0(gs[0]), yws = WY0(gs[1]), y18c = WY0(gc[2]), ywc = WY0(gc[3]);
            chk(abs(y18c - y18s) <= 2 && WY1(gc[2]) == y18c, "18 %% grey: Film 1 code 139 -> %d, sRGB code 118 -> %d", y18c,
                y18s);
            chk(ywc >= yws + 8, "headroom: Film 1 code 250 -> %d, sRGB white -> %d", ywc, yws);
            struct wf_look lk;
            wf_look_now(&lk);
            chk(lk.output == WF_OUT_LLUT && lk.incurve == WF_INC_FILM1, "the look carries the input curve");
            /* the curve is for Picture Style User Def. 1: another style reads sRGB (tables reloaded), back on UD1 */
            log_mark();
            h_li_set("picstyle", 0x81);
            chk(LOGW("srgb(style 81)", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
                && ll_same(&ll, wf_ll_tables()) && wf_incurve == WF_INC_FILM1 && wf_incurve_t() == WF_INC_SRGB,
                "Picture Style Standard: Film 1 chosen, sRGB tables: %s", log_line("lut "));
            wf_look_now(&lk);
            chk(lk.incurve == WF_INC_SRGB, "the look says sRGB under Standard");
            log_mark();
            h_li_set("picstyle", 0x21);
            chk(LOGW(" in-film1", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&lc, wf_ll_tables()),
                "Picture Style User Def. 1 again: Film 1 tables: %s", log_line("lut "));
            h_li_set("picstyle", 0);
            log_mark();
            wf_incurve = WF_INC_SRGB;
            chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&ll, wf_ll_tables())
                && !strstr(log_line("lut "), "in-film1"), "back to sRGB: the same tables as before: %s", log_line("lut "));
        } else {
            wf_incurve = WF_INC_FILM1;
            chk(wf_incurve_t() == WF_INC_SRGB, "no Canon curve table in this build: Film 1 reads as sRGB");
            wf_incurve = WF_INC_SRGB;
        }
    }

    /* Off, no film */
    wf_out = WF_OUT_OFF;
    plain_shot("output Off", ": off");
    wf_out = WF_OUT_FILM;
    wf_film_id = 0;
    plain_shot("Film without a film", ": nofilm");
    chk(cbr_key(0x1234) == 1, "keys pass while the browser is closed");
    mod_deinit();
}

/* ------------------------------------------------------------------ FX */
static int fx_line_ok(const char *l, const char *a, const char *b)
{
    return l[0] && strstr(l, a) && (!b || strstr(l, b)) && !strstr(l, " h-") && !strstr(l, " g-");
}
static void t_fx(void)
{
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=normal\nhalation=bloom\nmist=off\n", wf_films[fC].id);
    wf_fx_retry_ms = 500;
    mod_init(1);
    int rdy = WAIT(wf_fx_hk && wf_fx_gk, 120000);
    usleep(200000);
    chk(rdy, "halation + grain state ready (%s | %s)", log_line("fx halation"), log_line("fx grain"));
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    disc_r = 60;
    gen_frame(in, FW, FH, 1);
    chk(WAIT(look_table_ready(), 10000), "table published");
    log_mark();
    int sb = shot(in, out, FW, FH);
    chk(LOGW(": film ", 5000), "log: %s", log_line(": film "));
    const char *l = log_line(": film ");
    chk(fx_line_ok(l, " h", " g"), "halation and grain ran on the shot");
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    double sd, cm = corner(ref, out, FW, &sd), rg = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'y', 0);
    double rv = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'v', 0);
    chk(sb == 0, "nothing written outside the frame");
    chk(fabs(cm) < 3 && sd > 0.3 && sd < 25, "grain far from the light: mean %+.2f, sd %.2f", cm, sd);
    chk(rg > 1.0 && rv > 0.2, "glow round the light: dY %+.2f, dV (red) %+.2f", rg, rv);
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 1);
    gen_frame(in, 3456, 3456, 1);
    log_mark();
    shot(in, out, 3456, 3456);
    chk(LOGW(": film ", 5000) && strstr(log_line(": film "), " har") && strstr(log_line(": film "), " g"),
        "1:1 aspect: no halation (' har'), grain on: %s", log_line(": film "));
    h_prop_send(PROP_ASPECT, 0);
    gen_frame(in, FW, FH, 1);
#endif
    /* plain log gets no effects */
    wf_out = WF_OUT_LOG;
    log_mark();
    shot(in, out, FW, FH);
    chk(LOGW(": log ", 5000) && !strstr(log_line(": log "), " h") && !strstr(log_line(": log "), " g"), "plain log: no effects: %s",
        log_line(": log "));
    wf_out = WF_OUT_FILM;
    /* no shoot memory for a new halation key: the shot goes without, then the retry */
    h_shoot_fail = 1;
    log_mark();
    wf_halo = 1;
    chk(LOGW("fx halation: no memory", 20000), "no memory: %s", log_line("fx halation"));
    log_mark();
    shot(in, out, FW, FH);
    chk(LOGW(": film ", 5000) && strstr(log_line(": film "), " h-"), "shot without halation: %s", log_line(": film "));
    h_shoot_fail = 0;
    chk(WAIT(wf_fx_hk != 0, 60000), "retry after the memory came back (%s)", log_line("fx halation"));
    log_mark();
    shot(in, out, FW, FH);
    chk(LOGW(": film ", 5000) && fx_line_ok(log_line(": film "), " h", " g"), "halation back: %s", log_line(": film "));
    /* effects off: every block given back */
    wf_halo = 0;
    wf_grain = 0;
    chk(WAIT(!wf_fx_hk && !wf_fx_gk && h_arena_live == 0, 20000), "effects off: shoot memory and heap blocks freed (%d live)", h_arena_live);
    mod_deinit();
}

/* ------------------------------------------------------------------ MIST */
static void t_mist(void)
{
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=w4\n", wf_films[fC].id);
    mod_init(1);
    int rdy = WAIT(wf_fx_mk == 2, 120000);
    usleep(200000);
    chk(rdy, "mist 1/4 ready (%s)", log_line("fx mist"));
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    disc_r = 80;
    gen_frame(in, FW, FH, 1);
    chk(WAIT(look_table_ready(), 10000), "table published");
    log_mark();
    int sb = shot(in, out, FW, FH);
    chk(LOGW(": film ", 5000) && strstr(log_line(": film "), " m") && !strstr(log_line(": film "), " m-"), "mist ran: %s",
        log_line(": film "));
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    double glow = ring(ref, out, FW, FH, disc_r + 8, disc_r + 60, 'y', 0), far = corner(ref, out, FW, 0);
    double wide = ring(ref, out, FW, FH, disc_r + 300, disc_r + 700, 'y', 0), core = ring(ref, out, FW, FH, 0, disc_r - 8, 'y', 0);
    chk(sb == 0, "nothing written outside the frame");
    chk(glow > 2.0 && wide > 0.0, "white mist: glow round the light dY %+.2f, wide glow %+.3f", glow, wide);
    chk(fabs(far) < 1.0, "white mist: the dark far from any light stays (dY %+.3f)", far);
    chk(core > -6, "the light itself stays bright (dY %+.2f)", core);
#ifndef WF_BUILD_5D2
    halves = 1;                                  /* Diana halves: the same picture as the whole frame, no step at the seam */
    log_mark();
    shot(in, out2, FW, FH);
    halves = 0;
    chk(LOGW("(TOP ", 5000), "halves shot: %s", log_line("(TOP "));
    double hm = mean_dy(out, out2, NW);
    int wpr = FW / 2;
    double sa = rows_dy(ref, out2, FW, FH / 2 - 3, FH / 2 - 1), sbm = rows_dy(ref, out2, FW, FH / 2, FH / 2 + 2);
    chk(fabs(hm) < 0.5, "halves vs whole frame: mean dY %+.3f", hm);
    chk(fabs(sa - sbm) < 1.5, "no step at the seam: rows above %+.2f, below %+.2f", sa, sbm);
    (void)wpr;
#else
    double sa = rows_dy(ref, out, FW, FH / 2 - 3, FH / 2 - 1), sbm = rows_dy(ref, out, FW, FH / 2, FH / 2 + 2);
    chk(fabs(sa - sbm) < 1.5, "no step at the TOP / BOT seam: rows above %+.2f, below %+.2f", sa, sbm);
#endif
    wf_mist = 6;                                 /* black 1/4 */
    chk(WAIT(wf_fx_mk == 6, 60000), "black mist 1/4 ready");
    log_mark();
    shot(in, out2, FW, FH);
    LOGW(": film ", 5000);
    double bwide = ring(ref, out2, FW, FH, disc_r + 300, disc_r + 700, 'y', 0), bglow = ring(ref, out2, FW, FH, disc_r + 8, disc_r + 60, 'y', 0);
    chk(bwide < wide && bglow > 0.5, "black mist: less wide glow (%+.3f < %+.3f), a glow near the light (%+.2f)", bwide, wide, bglow);
    wf_mist = 0;
    chk(WAIT(!wf_fx_mk && h_arena_live == 0, 20000), "mist off: memory freed (%d live)", h_arena_live);
    mod_deinit();
}

/* ------------------------------------------------------------------ EXIF */
static uint8_t jbe;
static void j16(uint8_t *p, unsigned v) { if (jbe) { p[0] = v >> 8; p[1] = v; } else { p[0] = v; p[1] = v >> 8; } }
static void j32(uint8_t *p, uint32_t v) { if (jbe) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; } else le32(p, v); }
/* SOI, APP1 Exif (IFD0 -> Exif IFD -> UserComment, type 7, cnt bytes of `fill`), EOI */
static void jpeg_write(const char *path, int be, int cnt, int fill)
{
    static uint8_t b[8192];
    jbe = (uint8_t)be;
    memset(b, 0, sizeof b);
    b[0] = 0xFF; b[1] = 0xD8; b[2] = 0xFF; b[3] = 0xE1;
    memcpy(b + 6, "Exif\0\0", 6);
    uint8_t *T = b + 12;
    T[0] = T[1] = be ? 'M' : 'I';
    j16(T + 2, 42); j32(T + 4, 8);
    j16(T + 8, 1); j16(T + 10, 0x8769); j16(T + 12, 4); j32(T + 14, 1); j32(T + 18, 26); j32(T + 22, 0);
    j16(T + 26, 2);
    j16(T + 28, 0x9000); j16(T + 30, 7); j32(T + 32, 4); memcpy(T + 36, "0230", 4);
    j16(T + 40, 0x9286); j16(T + 42, 7); j32(T + 44, cnt); j32(T + 48, 56);
    j32(T + 52, 0);
    memset(T + 56, fill, cnt);
    int len = 2 + 6 + 56 + cnt, e = 12 + 56 + cnt;
    b[4] = len >> 8; b[5] = len;
    b[e] = 0xFF; b[e + 1] = 0xD9;
    put_file(path, b, e + 2);
}
/* the comment text of a file written by jpeg_write: "" when still zero, "?" when not ours */
static const char *comment(const char *path)
{
    static char t[300];
    long n;
    char *b = get_file(path, &n);
    t[0] = 0;
    if (b && n > 12 + 56 + 8) {
        const char *c = b + 12 + 56;
        if (!memcmp(c, "ASCII\0\0\0", 8)) snprintf(t, sizeof t, "%s", c + 8);
        else if (c[0]) snprintf(t, sizeof t, "?");
    }
    free(b);
    return t;
}
static void jpg_name(char *p, int num) { sprintf(p, "DCIM/100CANON/IMG_%04d.JPG", num); }

static void t_exif(void)
{
    /* wf_ex_write alone */
    jpeg_write("ML/le.jpg", 0, 264, 0);
    jpeg_write("ML/be.jpg", 1, 264, 0);
    const char *r1 = wf_ex_write("B:/ML/le.jpg", "WindyFilm: test LE"), *r2 = wf_ex_write("B:/ML/be.jpg", "WindyFilm: test BE");
    chk(!r1 && !strcmp(comment("ML/le.jpg"), "WindyFilm: test LE"), "little-endian Exif: written + verified (%s)", r1 ? r1 : "ok");
    chk(!r2 && !strcmp(comment("ML/be.jpg"), "WindyFilm: test BE"), "big-endian Exif: written + verified (%s)", r2 ? r2 : "ok");
    r1 = wf_ex_write("B:/ML/le.jpg", "again");
    chk(r1 && !strcmp(r1, "comment not empty"), "a filled comment is never overwritten (%s)", r1 ? r1 : "written");
    put_file("ML/x.jpg", "GIF89a....", 10);
    r1 = wf_ex_write("B:/ML/x.jpg", "x");
    chk(r1 && !strcmp(r1, "not a jpeg"), "not a JPEG: %s", r1 ? r1 : "written");
    put_file("ML/y.jpg", "\xFF\xD8\xFF\xDB\x00\x04\x00\x00\xFF\xD9", 10);
    r1 = wf_ex_write("B:/ML/y.jpg", "x");
    chk(r1 && !strcmp(r1, "no exif"), "JPEG without Exif: %s", r1 ? r1 : "written");
    jpeg_write("ML/s.jpg", 0, 30, 0);
    r1 = wf_ex_write("B:/ML/s.jpg", "WindyFilm: a text longer than the field is");
    chk(r1 && !strcmp(r1, "text too long") && !strcmp(comment("ML/s.jpg"), ""), "short field: %s, file unchanged", r1 ? r1 : "written");
    r1 = wf_ex_write("B:/ML/none.jpg", "x");
    chk(r1 && !strcmp(r1, "busy"), "missing file: %s", r1 ? r1 : "written");

    /* through shots */
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fC].id);
    mod_init(1);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(look_table_ready(), 10000), "table published");
    char p[64], want[200], want30[200], want125[200];
    snprintf(want, sizeof want, "WindyFilm: %s | Print Scan | Strength 100%%", wf_films[fC].name);
    snprintf(want30, sizeof want30, "WindyFilm: %s | Print Scan | Strength 30%%", wf_films[fC].name);
    snprintf(want125, sizeof want125, "WindyFilm: %s | Print Scan | Strength 125%%", wf_films[fC].name);
    log_mark();
    h_li_set("job", 1);                          /* Canon busy from the shot until its JPEG is on the card */
    shot(in, out, FW, FH);
    jpg_name(p, 5); jpeg_write(p, 0, 264, 0);
    h_set_file_number(5);
    h_li_set("job", 0);
    chk(LOGW("exif IMG_0005.JPG ok", 8000), "one shot: %s", log_line("exif"));
    chk(!strcmp(comment(p), want), "IMG_0005: '%s'", comment(p));

    /* two shots while Canon is busy: newest entry -> newest file */
    h_li_set("job", 1);
    log_mark();
    shot(in, out, FW, FH);
    wf_strength = 0;
    shot(in, out, FW, FH);
    wf_strength = 1;
    jpg_name(p, 6); jpeg_write(p, 0, 264, 0);
    jpg_name(p, 7); jpeg_write(p, 1, 264, 0);
    h_set_file_number(7);
    usleep(400000);
    h_li_set("job", 0);
    chk(LOGW("exif IMG_0006.JPG ok", 8000), "two shots: %s", log_line("exif"));
    jpg_name(p, 6);
    chk(!strcmp(comment(p), want), "IMG_0006 (older): '%s'", comment(p));
    jpg_name(p, 7);
    chk(!strcmp(comment(p), want30), "IMG_0007 (newer): '%s'", comment(p));

    /* RAW-only shot between: no JPEG, no entry, its number skipped */
    h_li_set("job", 1);
    log_mark();
    shot(in, out, FW, FH);
    pic_quality = 0x04000000;
    shot(in, out, FW, FH);
    pic_quality = 0;
    wf_strength = 2;
    shot(in, out, FW, FH);
    wf_strength = 1;
    jpg_name(p, 8); jpeg_write(p, 0, 264, 0);
    jpg_name(p, 10); jpeg_write(p, 0, 264, 0);
    h_set_file_number(10);
    usleep(400000);
    h_li_set("job", 0);
    chk(LOGW("exif IMG_0008.JPG ok", 8000), "RAW-only gap: %s", log_line("exif"));
    jpg_name(p, 8);
    chk(!strcmp(comment(p), want), "IMG_0008: '%s'", comment(p));
    jpg_name(p, 10);
    chk(!strcmp(comment(p), want125), "IMG_0010: '%s'", comment(p));

    /* a JPEG the hooks refused (EOS M: JPEG M; 5D2: not size L): an empty entry keeps the numbers aligned */
    h_li_set("job", 1);
    log_mark();
    shot(in, out, FW, FH);
#ifdef WF_BUILD_5D2
    for (uint32_t i = 0; i < A_USED / 4; i++) fa[i] = SENT;
    memset(regs, 0, sizeof regs); regs[0] = FRAME_A; regs[3] = 1; call_hook(S_TOP);
    memset(regs, 0, sizeof regs); regs[0] = FRAME_B; regs[3] = 1; call_hook(S_BOT);
#else
    pic_quality = 0x0100;
    shot(in, out, FW, FH);
    pic_quality = 0;
#endif
    jpg_name(p, 11); jpeg_write(p, 0, 264, 0);
    jpg_name(p, 12); jpeg_write(p, 0, 264, 0);
    h_set_file_number(12);
    usleep(400000);
    h_li_set("job", 0);
    chk(LOGW("exif IMG_0011.JPG ok", 8000), "refused JPEG: %s", log_line("exif"));
    jpg_name(p, 11);
    chk(!strcmp(comment(p), want), "IMG_0011 (graded): '%s'", comment(p));
    jpg_name(p, 12);
    chk(!strcmp(comment(p), ""), "IMG_0012 (refused): comment left empty");

    /* EXIF info off */
    wf_exif_off = 1;
    log_mark();
    shot(in, out, FW, FH);
    jpg_name(p, 13); jpeg_write(p, 0, 264, 0);
    h_set_file_number(13);
    usleep(1500000);
    chk(!strcmp(comment(p), "") && !log_has("exif IMG_0013"), "EXIF info Off: nothing written");
    mod_deinit();
}

/* ------------------------------------------------------------------ EXPSIM */
static uint8_t vp_bytes[4] = { 72, 48, 80, 0 };  /* LiveView frame: ISO raw, aperture raw, shutter raw */
static uint8_t *vp_ptr = vp_bytes;
static void t_expsim(void)
{
#ifdef WF_BUILD_5D2
    put_cfg("exposure=smooth\noutput=off\n");
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    mod_init(1);
    usleep(500000);
    int n = 0;
    for (int i = 0; i < h_nchanges && i < 256; i++) n += h_change_prop[i] == 0x80000034u;
    chk(wf_exp_mode == WF_EXP_CANON && cbr_disp(0) != 2 && n == 0, "5D2: Preview exposure stays Camera, ExpSim never written");
    mod_deinit();
#else
    struct wf_exp_in x;
    memset(&x, 0, sizeof x);
    x.metered = 1; x.ec = 8;
    chk(wf_exp_dev8(&x) == 8, "metered: dEV = compensation");
    x.trim = 2; x.ec = -8;
    chk(wf_exp_dev8(&x) == -6, "metered: + trim");
    x.ec = 100;
    chk(wf_exp_dev8(&x) == WF_EXP_BAD, "metered: compensation out of range");
    memset(&x, 0, sizeof x);
    x.stv = 88; x.sav = 48; x.siso = 72; x.ftv = 80; x.fav = 48; x.fiso = 72;
    chk(wf_exp_dev8(&x) == -8, "manual: dEV = frame - shot (%d)", wf_exp_dev8(&x));
    x.fiso = 0;
    chk(wf_exp_dev8(&x) == WF_EXP_BAD, "manual: frame without ISO");
    struct wf_exp_sm s;
    wf_exp_sm_reset(&s);
    int q1 = wf_exp_sm_step(&s, 16), q2 = wf_exp_sm_step(&s, 17), q3 = 0;
    for (int i = 0; i < 12; i++) q3 = wf_exp_sm_step(&s, 40);
    chk(q1 == 16 && q2 == 16 && q3 == 32, "smoothing: first %d, hysteresis %d, clamp %d", q1, q2, q3);
    wf_exp_sm_reset(&s);
    int f1 = wf_exp_sm_step(&s, -48), fl = s.flash, f2 = -1;
    for (int i = 0; i < 40 && s.flash; i++) f2 = wf_exp_sm_step(&s, 0);
    chk(f1 == 0 && fl == 1 && !s.flash && f2 != 0, "flash state below -5 EV, left again (%d)", f2);
    static struct wf_exp_tab t0, t8;
    wf_exp_build(&t0, 0);
    wf_exp_build(&t8, 8);
    int idn = 1, mono = 1;
    for (int y = 0; y < 256; y++) {
        idn &= t0.gy[y] == y;
        if (y) mono &= t8.gy[y] >= t8.gy[y - 1];
    }
    chk(idn && mono && t8.gy[128] > 160 && t8.gy[255] == 255, "gain tables: q 0 identity, +1 EV monotonic (128 -> %d)", t8.gy[128]);
    {
        uint32_t a[512], b[512];
        for (int i = 0; i < 512; i++) a[i] = b[i] = rnd();
        wf_exp_words(a, 512, &t0);
        chk(ndiff(a, b, 512) == 0, "q 0 words: unchanged");
    }
    /* the module: ExpSim off by us, the filter corrects the picture, back on in Camera mode, a write that does not take */
    put_cfg("exposure=smooth\noutput=off\n");
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    h_expsim = 1;
    mod_init(1);
    chk(WAIT(wf_exp_ctl == WF_EXPC_SET && h_expsim == 0, 5000) && LOGW("expsim: off by us", 3000), "ExpSim switched off (%s)",
        log_line("expsim"));
    int n = 0;
    for (int i = 0; i < h_nchanges && i < 256; i++) n += h_change_prop[i] == 0x80000034u && h_change_val[i] == 3;
    chk(n >= 1, "property 0x80000034 = 3 requested");
    chk(cbr_disp(0) == 2, "display filter: 2 (the picture has the shot's luma)");
    {
        enum { LW = 360, LH = 480 };
        uint32_t *src = malloc(LW * LH * 4), *dst = malloc(LW * LH * 4), *pr = malloc(LW * LH * 4);
        gen_frame(src, 720, LH, 0);
        uint32_t *bufs[2] = { src, dst };
        cbr_disp((unsigned)(uintptr_t)bufs);
        static struct wf_exp_tab tq;
        wf_exp_build(&tq, -8);
        memcpy(pr, src, LW * LH * 4);
        for (int y = 0; y < LH; y++) wf_exp_words(pr + y * LW, LW, &tq);
        chk(wf_exp_q_last == -8 && ndiff(dst, pr, LW * LH) == 0 && mean_dy(src, dst, LW * LH) < -5,
            "preview 1 EV darker: q %d, bit-exact vs wf_exp_words, mean dY %+.1f", wf_exp_q_last, mean_dy(src, dst, LW * LH));
        free(src); free(dst); free(pr);
    }
    log_mark();
    wf_exp_mode = WF_EXP_CANON;
    chk(WAIT(h_expsim == 1 && wf_exp_ctl == WF_EXPC_IDLE, 5000) && LOGW("expsim: back on", 3000), "Camera mode: ExpSim back on");
    h_expsim_stuck = 1;
    wf_exp_mode = WF_EXP_SMOOTH;
    chk(WAIT(wf_exp_ctl == WF_EXPC_FAIL, 10000) && cbr_disp(0) != 2, "a write that does not take: FAIL after 3 tries, no correction");
    h_expsim_stuck = 0;
    wf_exp_mode = WF_EXP_CANON;
    WAIT(wf_exp_ctl == WF_EXPC_IDLE, 3000);
    wf_exp_mode = WF_EXP_SMOOTH;
    chk(WAIT(wf_exp_ctl == WF_EXPC_SET && h_expsim == 0, 5000), "set again");
    mod_deinit();
    chk(h_expsim == 1, "unload: ExpSim given back");
#endif
}

/* ------------------------------------------------------------------ CFG */
static void t_cfg(void)
{
    std_pack();
    const char *code1 = wf_flut_code[1];
    put_cfg("# comment\nfilm=%d\n output = LUT\ncurve=CLOG2\nflut=%s\nlutei=-2\nincurve=FILM1\nprint=CINE\nlight=tungsten\nstrength=strong\n"
            "grain=fine\nhalation=spkbloom\nmist=b2\npreview=fast\nexposure=smooth\nexptrim=-5\nexif=off\ninput=jpeg\nbogus=1\n"
            "userwb=1,5600,2,-3\n", wf_films[fA].id, code1);
    mod_init(1);
    chk(wf_film_id == wf_films[fA].id && wf_out == WF_OUT_LLUT && wf_curve == 4 && wf_flut == 1 && wf_lut_ei == -2,
        "film %d output %d curve %d flut %d lutei %d", wf_film_id, wf_out, wf_curve, wf_flut, wf_lut_ei);
    chk(wf_incurve == WF_INC_FILM1 && wf_incurve_t() == (wf_ll_canon_have() ? WF_INC_FILM1 : WF_INC_SRGB),
        "incurve %d (in use %d)", wf_incurve, wf_incurve_t());
    chk(wf_print == 1 && wf_light == 3 && wf_strength == 2 && wf_grain == 1 && wf_halo == 4 && wf_mist == 7 && wf_pv_mode == 2,
        "print %d light %d strength %d grain %d halation %d mist %d preview %d", wf_print, wf_light, wf_strength, wf_grain, wf_halo,
        wf_mist, wf_pv_mode);
    chk(wf_exp_mode == (WF_EXP_CAN ? WF_EXP_SMOOTH : WF_EXP_CANON) && wf_exp_trim == -5 && wf_exif_off == 1,
        "exposure %d exptrim %d exif off %d", wf_exp_mode, wf_exp_trim, wf_exif_off);
    chk(WAIT(h_kelvin_last == 3200, 5000), "Light Tungsten applied at start (%dK)", h_kelvin_last);
    wf_row_step(WF_ROW_GRAIN, 1);
    h_clock_add_ms += 2000;
    usleep(300000);
    long n;
    char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    char want[700];
    snprintf(want, sizeof want, "input=jpeg\nfilm=%d\noutput=lut\ncurve=clog2\nflut=%s\nlutei=-2\nincurve=film1\nprint=cine\nlight=tungsten\n"
             "strength=strong\ngrain=normal\nhalation=spkbloom\nmist=b2\npreview=fast\nexposure=%s\nexptrim=-5\nexif=off\n"
             "userwb=1,5600,2,-3\n", wf_films[fA].id, code1, WF_EXP_CAN ? "smooth" : "camera");
    chk(b && !strcmp(b, want), "saved 1.5 s after a change, every key:\n%s", b ? b : "(none)");
    free(b);
    put_cfg("film=99999\noutput=nope\nlutei=77\nexptrim=99\ncurve=x\nprint=\n");
    wf_cfg_load();
    chk(wf_film_id == wf_films[fA].id && wf_out == WF_OUT_LLUT && wf_lut_ei == -2 && wf_exp_trim == -5 && wf_curve == 4,
        "garbage values ignored");
    char big[700];
    memset(big, '#', sizeof big);
    memcpy(big, "film=0\n", 7);
    put_file("ML/SETTINGS/WINDYFLM.CFG", big, sizeof big);
    wf_cfg_load();
    chk(wf_film_id == wf_films[fA].id, "a file over 600 bytes is not read");
    put_cfg("film=0\n");
    wf_cfg_load();
    chk(wf_film_id == 0, "film=0 (none) read");
    wf_row_step(WF_ROW_GRAIN, -1);
    h_nchanges = 0;
    mod_deinit();
    int m1 = 0, m2 = 0;
    for (int i = 0; i < h_nchanges && i < 256; i++) {
        m1 |= h_change_prop[i] == 0x80050018u && h_change_val[i] == 1;
        m2 |= h_change_prop[i] == 0x8000000Du && h_change_val[i] == 1;
    }
    chk(m1 && m2 && h_gm_last == 2 && h_ba_last == -3, "unload: the user's WB from the file back (mode 1, shift 2 / -3)");
    b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && strstr(b, "film=0\n") && strstr(b, "grain=fine\n") && strstr(b, "userwb=1,5600,2,-3\n"), "saved at unload (with userwb)");
    free(b);
}

/* ------------------------------------------------------------------ PACK */
static void t_pack(const char *cs, const char *file)
{
    struct pk e[] = {
        { fA, 0, tab_make(12, 6, -6, 0) }, { fA, 1, tab_make(-10, 0, 0, 0) }, { fB, 0, tab_make(0, 0, 0, 1) }, { fC, 0, tab_make(0, 0, 0, 0) },
    };
    int bad = !strcmp(cs, "magic") ? 'M' : !strcmp(cs, "index") ? 'I' : !strcmp(cs, "top") ? 'T' : !strcmp(cs, "fnv") ? 'F'
            : !strcmp(cs, "code") ? 'C' : !strcmp(cs, "dup") ? 'D' : !strcmp(cs, "flags") ? 'G' : 0;
    if (!strcmp(cs, "real")) {
        long n;
        char *b = file ? get_file(file, &n) : 0;
        if (!b) { chk(0, "real pack %s readable", file ? file : "(none)"); return; }
        put_file("ML/DATA/WFFILMS.DAT", b, n);
        free(b);
    } else if (strcmp(cs, "missing")) {
        pack_write(e, 4, bad);
    }
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    int st = wf_ws_state();
    if (!strcmp(cs, "real")) {
        int nf = 0, nh = 0, nv = 0;
        for (int k = 0; k < wf_nfilms; k++) {
            if (!colour_film(k) && (wf_films[k].flags & (WF_F_READY | WF_F_LUTCAN)) != WF_F_READY) continue;
            nf++;
            nh += wf_ws_has(k, 0);
            nv += wf_ws_has(k, 1) + wf_ws_has(k, 2);
        }
        chk(st == 1 && nh == nf, "real pack: %s; scan tables for %d of %d ready films, %d print variants", log_line("pack ok"), nh, nf, nv);
        usleep(3000000);                         /* the sweep checks every table */
        chk(!log_has("table bad"), "real pack: every table passes its checksum and word check");
        mod_deinit();
        return;
    }
    if (!strcmp(cs, "missing") || bad == 'M' || bad == 'I') {
        int want = !strcmp(cs, "missing") ? -1 : bad == 'M' ? -3 : -5;
        chk(st == want && LOGW(want == -1 ? "missing" : "bad", 3000), "%s pack: state %d (want %d): %s", cs, st, want, log_line("pack"));
        chk(wf_load(wf_films[fC].id) == WF_LOAD_NOTABLE && !wf_avail(wf_films[fA].id), "no film loadable");
        gen_frame(in, FW, FH, 0);
        plain_shot("film shot without a pack", ": notab");
        mod_deinit();
        return;
    }
    chk(st == 1, "%s: index read (state %d): %s", cs[0] ? cs : "ok", st, log_line("pack ok"));
    if (!cs[0] || !strcmp(cs, "ok")) {
        chk(wf_ws_has(fA, 0) && wf_ws_has(fA, 1) && !wf_ws_has(fA, 2) && wf_ws_has(fB, 0) && wf_ws_has(fC, 0) && !wf_ws_has(fX, 0),
            "has: A scan + cine, B, C; not A paper, not X");
        uint32_t sz = 0;
        uint8_t h[32];
        int r = wf_card_read("ML/DATA/WFFILMS.DAT", 0, h, 32, &sz);
        chk(r == 0 && sz == 32 + 4 * 24 + 4 * TW * 4 && !memcmp(h, "WFFT", 4), "wf_card_read: header, size %u", sz);
        r = wf_card_read("ML/DATA/WFFILMS.DAT", sz - 8, h, 16, &sz);
        chk(r == -1 && sz == 32 + 4 * 24 + 4 * TW * 4, "wf_card_read: short read = error, size still set");
        r = wf_card_read("ML/DATA/NONE.DAT", 0, h, 4, &sz);
        chk(r == -1 && sz == 0, "wf_card_read: missing file, size 0");
        r = wf_card_read("ML/DATA/WFFILMS.DAT", 32, 0, 24, &sz);
        chk(r == 0, "wf_card_read: skip without a buffer");
        usleep(1500000);
        chk(!log_has("table bad"), "sweep: no bad table");
    } else if (bad == 'T' || bad == 'F') {
        gen_frame(in, FW, FH, 0);
        chk(LOGW("table bad", 10000) && !wf_ws_has(fA, 0), "%s: entry A scan dropped: %s", cs, log_line("table bad"));
        plain_shot("shot of the bad table", ": notab");
        chk(wf_ws_has(fC, 0), "other entries stay");
    } else if (bad == 'C' || bad == 'G') {
        chk(!wf_ws_has(fA, 0) && wf_ws_has(fA, 1) && wf_ws_has(fC, 0) && LOGW("pack ok: 3 tables", 3000), "%s: entry A scan skipped: %s",
            cs, log_line("pack ok"));
    } else if (bad == 'D') {
        gen_frame(in, FW, FH, 0);
        chk(LOGW("pack ok: 4 tables", 3000), "dup: one of the two kept: %s", log_line("pack ok"));
        film_shot("dup: the first entry wins", fA, 0, 1, 12, 1.5);
    }
    mod_deinit();
}

/* ------------------------------------------------------------------ LIGHT */
static void t_light(void)
{
    int kd = -1, kt = -1;                        /* films with Day kelvin 5500 and 3200 in the catalogue */
    for (int k = 0; k < wf_nfilms; k++) {
        if (!colour_film(k)) continue;
        if (kd < 0 && wf_films[k].kelvin != 5200 && wf_films[k].kelvin != 3200) kd = k;
        if (kt < 0 && wf_films[k].kelvin == 3200) kt = k;
    }
    if (kd < 0) kd = fC;
    if (kt < 0) kt = fA;
    struct pk e[] = { { kd, 0, tab_make(0, 0, 0, 0) }, { kt, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 2, 0);
    put_cfg("film=%d\noutput=film\nlight=auto\ngrain=off\nhalation=off\nmist=off\n", wf_films[kd].id);
    h_li_set("wb_mode", 0); h_li_set("kelvin", 5000); h_li_set("gm", 1); h_li_set("ba", -2);
    mod_init(1);
    usleep(300000);
    chk(h_kelvin_n == 0 && h_nchanges == 0, "Light Auto: white balance untouched");
    int day = wf_films[kd].kelvin, cloudy = (day * 5 / 4 + 50) / 100 * 100;
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == day, 3000) && h_li_get("wb_mode") == 9 && !strcmp(wf_row_text(WF_ROW_LIGHT), "Day"),
        "Day: %dK from the film (%s)", h_kelvin_last, wf_films[kd].code);
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == cloudy, 3000), "Cloudy: %dK (want %d)", h_kelvin_last, cloudy);
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == 3200, 3000), "Tungsten: %dK", h_kelvin_last);
    h_clock_add_ms += 2000;
    usleep(300000);
    long n;
    char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && strstr(b, "light=tungsten\n") && strstr(b, "userwb=0,5000,1,-2\n"), "config keeps the user's WB while Light is set");
    free(b);
    h_nchanges = 0;
    log_mark();
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(LOGW("user WB back (Auto)", 3000), "Auto: %s", log_line("light"));
    int m1 = 0, m2 = 0;
    for (int i = 0; i < h_nchanges && i < 256; i++) {
        m1 |= h_change_prop[i] == 0x80050018u && h_change_val[i] == 0;
        m2 |= h_change_prop[i] == 0x8000000Du && h_change_val[i] == 0;
    }
    chk(m1 && m2 && h_gm_last == 1 && h_ba_last == -2, "Auto: WB mode 0 and shift 1 / -2 back");
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == day, 3000), "Day again: %dK", h_kelvin_last);
    chk(wf_load(wf_films[kt].id) == WF_LOAD_OK && WAIT(h_kelvin_last == (wf_films[kt].kelvin >= 2000 ? wf_films[kt].kelvin : 5200), 3000),
        "another film: Day follows its kelvin (%s %dK)", wf_films[kt].code, h_kelvin_last);
    h_li_set("kelvin", 4000);                    /* the user turns the dial: the next Light request sets its kelvin again */
    int n0 = h_kelvin_n;
    wf_load(wf_films[kt].id);
    chk(WAIT(h_kelvin_n > n0 && h_li_get("kelvin") == h_kelvin_last && h_kelvin_last != 4000, 3000),
        "kelvin changed behind Light's back is set again (%dK)", h_kelvin_last);
    h_nchanges = 0;
    mod_deinit();
    int r1 = 0;
    for (int i = 0; i < h_nchanges && i < 256; i++) r1 |= h_change_prop[i] == 0x80050018u && h_change_val[i] == 0;
    chk(r1 && h_gm_last == 1 && h_ba_last == -2, "unload: the user's WB back");
}

/* ------------------------------------------------------------------ PRINT */
static void t_print(void)
{
    struct pk e[] = {
        { fA, 0, tab_make(12, 0, 0, 0) }, { fA, 1, tab_make(-10, 0, 0, 0) }, { fA, 2, tab_make(24, 0, 0, 0) },
        { fC, 0, tab_make(0, 0, 0, 0) }, { fB, 0, tab_make(0, 0, 0, 1) },
    };
    int k2 = -1;                                 /* a second 3-print film whose pack has the scan only */
    for (int k = 0; k < wf_nfilms; k++)
        if (k != fA && colour_film(k) && (wf_films[k].prints & 7) == 7) { k2 = k; break; }
    struct pk e2[6];
    memcpy(e2, e, sizeof e);
    e2[5].k = k2; e2[5].v = 0; e2[5].T = tab_make(5, 0, 0, 0);
    pack_write(e2, k2 >= 0 ? 6 : 5, 0);
    put_cfg("film=%d\noutput=film\nprint=scan\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    gen_frame(in, FW, FH, 0);
    chk(wf_row_avail(WF_ROW_PRINT) && !strcmp(wf_row_text(WF_ROW_PRINT), "Scan"), "Print row: Scan, available");
    film_shot("print Scan", fA, 0, 1, 12, 1.5);
    chk(wf_row_step(WF_ROW_PRINT, 1) && !strcmp(wf_row_text(WF_ROW_PRINT), "Cine"), "Print -> Cine");
    film_shot("print Cine", fA, 1, 1, -10, 1.5);
    chk(wf_row_step(WF_ROW_PRINT, 1) && !strcmp(wf_row_text(WF_ROW_PRINT), "Paper"), "Print -> Paper");
    film_shot("print Paper", fA, 2, 1, 24, 1.5);
    chk(wf_row_step(WF_ROW_PRINT, 1) && !strcmp(wf_row_text(WF_ROW_PRINT), "Scan"), "Print wraps to Scan");
    chk(wf_row_step(WF_ROW_PRINT, -1) && !strcmp(wf_row_text(WF_ROW_PRINT), "Paper"), "Print back to Paper");
    chk(wf_load(wf_films[fC].id) == WF_LOAD_OK && !wf_row_avail(WF_ROW_PRINT) && !strcmp(wf_row_text(WF_ROW_PRINT), "Scan"),
        "a scan-only film: row greyed, shows Scan");
    film_shot("scan-only film with Print Paper set", fC, 0, 1, 0, 0.5);
    chk(wf_row_step(WF_ROW_PRINT, 1) == 0, "row does not step");
    if (k2 >= 0) {
        tabs[k2][0] = e2[5].T;
        chk(wf_load(wf_films[k2].id) == WF_LOAD_OK && !wf_row_avail(WF_ROW_PRINT), "3-print film, pack with its scan only: row greyed");
        film_shot("pack without the print: the scan", k2, 0, 1, 5, 1.5);
    }
    chk(wf_load(wf_films[fA].id) == WF_LOAD_OK && !strcmp(wf_row_text(WF_ROW_PRINT), "Paper"), "back to A: Paper kept");
    wf_out = WF_OUT_LOG;
    chk(!wf_row_avail(WF_ROW_PRINT), "plain log: Print greyed");
    wf_out = WF_OUT_FILM;
    mod_deinit();
}

/* ------------------------------------------------------------------ 5D2 pairing */
#ifdef WF_BUILD_5D2
static void half_call(uint32_t site, uint32_t buf, uint32_t r1, uint32_t r3)
{
    memset(regs, 0, sizeof regs);
    regs[0] = buf; regs[1] = r1; regs[3] = r3;
    call_hook(site);
}
static void t_5d2(void)
{
    struct pk e[] = { { fA, 0, tab_make(12, 6, -6, 0) }, { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 2, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    chk(h_nhooks == 2 && h_hook_at(S_TOP) && h_hook_at(S_BOT) && LOGW("5D2 Path A on", 3000), "TOP + BOT hooks: %s", log_line("Path A"));
    gen_frame(in, FW, FH, 0);
    film_shot("pair TOP + BOT", fA, 0, 1, 12, 1.5);
    chk(LOGW("(TOP ", 3000), "shot line: %s", log_line("(TOP "));
    int nh = NW / 2;
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));
    /* BOT without its TOP */
    log_mark();
    memcpy(fb, in + nh, nh * 4);
    half_call(S_BOT, FRAME_B, 0, 0);
    chk(ndiff(fb, in + nh, nh) == 0 && LOGW("its TOP not graded", 3000), "BOT alone: plain");
    /* TOP, TOP (the first one's BOT owed), BOT */
    log_mark();
    memcpy(fa, in, nh * 4);
    half_call(S_TOP, FRAME_A, 0, 0);
    chk(ndiff(fa, ref, nh) == 0, "TOP graded");
    memcpy(fa, in, nh * 4);
    half_call(S_TOP, FRAME_A, 0, 0);
    chk(ndiff(fa, in, nh) == 0 && LOGW("BOT of the last shot owed", 3000), "second TOP while a BOT is owed: plain");
    memcpy(fb, in + nh, nh * 4);
    half_call(S_BOT, FRAME_B, 0, 0);
    chk(ndiff(fb, ref + nh, nh) == 0, "the owed BOT graded");
    /* a BOT that never comes holds new TOPs off for 20 s only */
    memcpy(fa, in, nh * 4);
    half_call(S_TOP, FRAME_A, 0, 0);
    h_clock_add_ms += 21000;
    memcpy(fa, in, nh * 4);
    half_call(S_TOP, FRAME_A, 0, 0);
    chk(ndiff(fa, ref, nh) == 0, "after 20 s a new TOP is graded");
    memcpy(fb, in + nh, nh * 4);
    half_call(S_BOT, FRAME_B, 0, 0);
    chk(ndiff(fb, ref + nh, nh) == 0, "and its BOT");
    /* gates */
    log_mark();
    memcpy(fa, in, nh * 4);
    half_call(S_TOP, FRAME_A, 0x44100000u, 0);
    chk(ndiff(fa, in, nh) == 0 && LOGW("r1/r3 not 0", 3000), "second YUV buffer (r1): plain");
    pic_quality = 0x04000000;
    memcpy(fa, in, nh * 4);
    half_call(S_TOP, FRAME_A, 0, 0);
    pic_quality = 0;
    chk(ndiff(fa, in, nh) == 0 && LOGW("RAW only", 3000), "RAW only: plain");
    log_mark();
    half_call(S_TOP, 0x20000000u, 0, 0);
    chk(LOGW("plain: buffer", 3000), "buffer outside the pool: %s", log_line("plain: buffer"));
    /* Log halves */
    wf_out = WF_OUT_LOG;
    wf_curve = 3;
    int sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_log_words, 3);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "LogC3 halves: bit-exact");
    wf_out = WF_OUT_FILM;
    mod_deinit();
}
#endif

#ifndef WF_BUILD_5D2
/* ------------------------------------------------------------------ PROBE (RAW probe P0, EOS M) */
#define PRB_REC 4696                             /* sizeof(struct wf_prb) in wf_cam_eosm.c */
static uint32_t fnv4k(const uint8_t *p)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4096; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static void probe_nn(uint32_t r2)
{
    memcpy(fa, in, NW * 4);
    memset(regs, 0, sizeof regs); regs[0] = 0x00ABC000u; regs[1] = FRAME_A; regs[2] = r2; call_hook(S_NN);
    memcpy(out, fa, NW * 4);
}
static void t_probe(void)
{
    const uint32_t raw_u = FRAME_B + 0x100002u, a0_u = FRAME_B + 0x10000u, a1_u = FRAME_A;
    uint8_t *b = (uint8_t *)fb;
    for (uint32_t i = 0; i < B_USED; i++) b[i] = (uint8_t)((i * 0x9E3779B1u) >> 24);
    memset(h_pb, 0, sizeof h_pb);
    h_pb[0x00 / 4] = 2;
    h_pb[0x50 / 4] = raw_u - 0x40000000u;        /* cached twins: the probe reads the uncached alias */
    h_pb[0x60 / 4] = 1;
    h_pb[0x64 / 4] = a0_u - 0x40000000u;
    h_pb[0x68 / 4] = a1_u - 0x40000000u;         /* "A1" = the frame itself: graded in place -> CHANGED */
    h_pb[0x94 / 4] = 5184; h_pb[0x98 / 4] = 5184; h_pb[0x9c / 4] = 3456;
    h_edmac[0] = 0x30000000u;                    /* outside the shoot pool: never read */
    h_edmac[1] = 0x4D000000u; h_edmac[2] = 0x4D100000u; h_edmac[3] = 0x4D200000u; h_edmac[4] = 0x4D300000u;
    for (int i = 0; i < 5; i++) h_nav_a[i] = 0x0C010000u + 0x100000u * i;

    std_pack();
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    h_prop_send(PROP_ASPECT, 0);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(look_table_ready(), 10000), "film table published");
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));

    /* off (the boot state): no record, no file, no ROM call */
    chk(wf_probe_mode == WF_PRB_OFF, "probe off at boot");
    log_mark();
    probe_nn(0);
    chk(ndiff(out, ref, NW) == 0, "off: the film shot as always");
    chk(LOGW(": film ", 3000), "off: shot logged");
    usleep(300000);
    long n = -1;
    char *f = get_file("ML/LOGS/WFPROBE.BIN", &n);
    chk(!log_has("probe #") && !f && h_nav_calls == 0, "off: no probe line, no WFPROBE.BIN, no ROM call");
    free(f);

    /* on */
    wf_probe_mode = WF_PRB_ON;
    log_mark();
    probe_nn(0);
    chk(ndiff(out, ref, NW) == 0, "on: the frame is graded exactly as without the probe");
    chk(LOGW("probe #1 r0 00abc000 r1 48000000 r2 0 mode 1", 5000), "on: header '%s'", log_line("probe #1"));
    chk(LOGW("probe pb+60 00000001 ", 3000), "on: parameter block dumped ('%s')", log_line("probe pb+60"));
    chk(log_has("probe edmac wr0 30000000 308 4d000000"), "on: EDMAC words ('%s')", log_line("probe edmac"));
    char e[200];
    snprintf(e, sizeof e, "probe raw %08x: %08x %08x %08x -> %08x %08x %08x same", raw_u, fnv4k(b + 0x100002),
             fnv4k(b + 0x900002), fnv4k(b + 0x1100002), fnv4k(b + 0x100002), fnv4k(b + 0x900002), fnv4k(b + 0x1100002));
    chk(LOGW(e, 3000), "on: raw windows (uncached alias, 3 FNV, unchanged): '%s'", log_line("probe raw"));
    snprintf(e, sizeof e, "probe A0 %08x: %08x", a0_u, fnv4k(b + 0x10000));
    chk(log_has(e) && strstr(log_line("probe A0"), " same"), "on: SSRAW A[0] from the block: '%s'", log_line("probe A0"));
    snprintf(e, sizeof e, "probe A1 %08x: %08x", a1_u, fnv4k((const uint8_t *)in));
    chk(log_has(e) && strstr(log_line("probe A1"), " CHANGED"), "on: a window the worker writes reads CHANGED: '%s'",
        log_line("probe A1"));
    chk(log_has("probe wr0 30000000: outside the shoot pool, not read"), "on: out-of-pool address not read");
    chk(!log_has("probe nav") && h_nav_calls == 0, "on: no ROM call");
    chk(WAIT((free(get_file("ML/LOGS/WFPROBE.BIN", &n)), n == PRB_REC), 3000), "on: one record in WFPROBE.BIN (%ld B)", n);
    f = get_file("ML/LOGS/WFPROBE.BIN", &n);
    if (f && n >= PRB_REC) {
        const uint32_t *w = (const uint32_t *)f;
        chk(w[0] == 0x42504657u && w[1] == 1 && w[2] == 1 && w[5] == FRAME_A && w[6] == 0 && w[7] == 1,
            "record: magic, version 1, seq 1, r1, r2, mode");
        chk(w[8 + 0x50 / 4] == raw_u - 0x40000000u && w[8 + 0x68 / 4] == a1_u - 0x40000000u, "record: parameter block");
        chk(memcmp(f + 600, b + 0x1100002, 1024) == 0, "record: 1 KB of the raw from the +16 MB window");
        chk(memcmp(f + 600 + 3 * 1024, (const uint8_t *)in + 0x1000000, 1024) == 0,
            "record: A1 copy taken at entry (the frame before grading)");
    }
    free(f);

    /* on + ROM call */
    wf_probe_mode = WF_PRB_NAV;
    log_mark();
    probe_nn(0);
    chk(LOGW("probe #2 ", 5000) && h_nav_calls == 5, "nav: record 2, MEMNAVI called for layers 0..4 (%d calls)", h_nav_calls);
    snprintf(e, sizeof e, "probe nav 1: rc 0 tmp 00001234 | 00000000 00000000 %08x 00000000", h_nav_a[1]);
    chk(LOGW(e, 3000), "nav: '%s'", log_line("probe nav 1"));
    chk(ndiff(out, ref, NW) == 0, "nav: frame graded as always");
    chk(WAIT((free(get_file("ML/LOGS/WFPROBE.BIN", &n)), n == 2 * PRB_REC), 3000), "nav: two records (%ld B)", n);

    /* a gate reject (RAW only) is probed too; r2 != 0 calls are not */
    wf_probe_mode = WF_PRB_ON;
    pic_quality = 0x04000000;
    log_mark();
    probe_nn(0);
    pic_quality = 0;
    chk(ndiff(out, in, NW) == 0, "RAW only: frame untouched");
    chk(LOGW("probe #3 ", 5000) && strstr(log_line("probe #3 "), " rc 1"), "RAW only: probed, rc 1 ('%s')",
        log_line("probe #3 "));
    log_mark();
    probe_nn(1);
    usleep(600000);
    chk(!log_has("probe #4"), "an NN call with r2 != 0 is not probed");
    mod_deinit();
}
#endif

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    mallopt(M_ARENA_MAX, 1);
    fa = map_at(FRAME_A, A_USED);                /* first, before any thread or large allocation */
    fb = map_at(FRAME_B, B_USED);
    if (argc < 2) { fprintf(stderr, "usage: wf_test MODE [CASE [FILE]]\n"); return 2; }
    const char *mode = argv[1], *cs = argc > 2 ? argv[2] : "";
    mkdirs();
    if (wf_t_li_size > 1024 || wf_t_card_size > 64) { fprintf(stderr, "lens_info %d / card_info %d too big for the shim\n", wf_t_li_size, wf_t_card_size); return 3; }
    snprintf(__camera_model_short, sizeof __camera_model_short, "%s", CAM_NAME);
    snprintf(firmware_version, sizeof firmware_version, "%s", CAM_FW);
    h_li_set("shutter", 88); h_li_set("aperture", 48); h_li_set("iso", 72); h_li_set("ae", 0);
    h_li_set("wb_mode", 0); h_li_set("kelvin", 5000); h_li_set("gm", 0); h_li_set("ba", 0);
    wf_exp_vpp = (uintptr_t)&vp_ptr;             /* the camera reads 0x404C8: here a fake frame-parameter block */
    in = malloc(NW * 4); out = malloc(NW * 4); ref = malloc(NW * 4); out2 = malloc(NW * 4);
    if (!in || !out || !ref || !out2) { fprintf(stderr, "no memory for the frames\n"); return 3; }
    pick_films();
    if (!strcmp(mode, "SHOT")) t_shot(cs);
    else if (!strcmp(mode, "FX")) t_fx();
    else if (!strcmp(mode, "MIST")) t_mist();
    else if (!strcmp(mode, "EXIF")) t_exif();
    else if (!strcmp(mode, "EXPSIM")) t_expsim();
    else if (!strcmp(mode, "CFG")) t_cfg();
    else if (!strcmp(mode, "PACK")) t_pack(cs, argc > 3 ? argv[3] : 0);
    else if (!strcmp(mode, "LIGHT")) t_light();
    else if (!strcmp(mode, "PRINT")) t_print();
#ifdef WF_BUILD_5D2
    else if (!strcmp(mode, "5D2")) t_5d2();
#else
    else if (!strcmp(mode, "PROBE")) t_probe();
#endif
    else { fprintf(stderr, "unknown mode %s for %s\n", mode, CAM_NAME); return 2; }
    if (nfail)
        log_tail(25);
    printf("RESULT %s %s %s: %d pass, %d fail\n", CAM_NAME, mode, cs, npass, nfail);
    fflush(stdout);
    _exit(nfail ? 1 : 0);                        /* the module's worker stays blocked on its semaphore: no join */
}
