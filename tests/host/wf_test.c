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
 *   FXAB               Even grain / Wrap (the Genesis Pro 1.6.6 model, DESIGN 1a) against the stages before; Bloom alone,
 *                      the Gauge (16 / 35 / 65 mm: halo reach, grain size)
 *                      them on the same frame: the halo round a light, the core of a light (difference of blur keeps
 *                      it), no pixel darker than the plain look, the three grain amounts monotonic, and the frames
 *                      not equal to the old ones
 *   MIST               white / black mist: glow, veil, halves vs whole frame, seam
 *   EXIF               wf_ex_write on LE / BE files and odd files; the ring vs IMG_nnnn.JPG numbers (RAW-only gap, JPEG M)
 *   EXPSIM             dEV, smoothing, gain tables, ExpSim off / back / stuck, the display filter (EOS M; 5D2: stays off)
 *   CFG                WINDYFLM.CFG load (every key), save, garbage, oversize, userwb restore at unload
 *   PACK CASE          ok | missing | magic | index | top | fnv | code | dup | flags | real FILE
 *   LIGHT              Day / Cloudy / Tungsten / Auto, film kelvin, user WB back
 *   LEICA              Leica Vivid can (its cube on the JPEG's sRGB codes, no V-Log; no auto exposure, its load asks
 *                      for Picture Style Standard),
 *                      hidden without its cube, Film LUT row, flut=leicav; the Windy names (no Tung left)
 *   PRINT              Scan / Cine / Paper rows and shots, missing variants
 *   5D2                (5D2 build) TOP / BOT pairing: BOT alone, owed BOT, pairing timeout, r1 / RAW gates, Log halves
 *   PROBE [boot]       RAW probe P0. EOS M: off at boot, record + log + WFPROBE.BIN, uncached alias, out-of-pool,
 *                      CHANGED after the worker, ROM-call mode, gate reject probed, r2 != 0 not, frame never touched,
 *                      WFPROBE.BIN refused (busy card): the record waits + retries, the next one is still logged, a torn
 *                      record (short write) is overwritten at its offset, an empty block (raw from WR0, raw2 = the
 *                      other buffer, copies at +16 MB / +0); boot: the first record waits for an idle camera and a
 *                      removable old file. 5D2: TOP + BOT back to back (one slot each), the spec's words, sources and
 *                      copies, ROM getters with each half's job handle, not a live job, MEM1 past the pool, SH unaligned
 *   RAWB               RAW develop "B" (EOS M): off at boot; tables in shoot memory; a RAW+L single shot developed from
 *                      the raw = wf_dev (levels, prepare, rows) + the film table, bit-exact; the buffer check (pointer
 *                      right, at the previous shot, 0 -> EDMAC WR0, no match -> the 8-bit look); the gates (JPEG only,
 *                      MRAW, burst, 1:1, no address); stops (power off, half-shutter, time limit: the rest stays
 *                      Canon's); Log and Log + LUT tables; playback / no memory / off give the memory back.
 *                      5D2: TOP + BOT halves from MEM1 + 0x76C8 (job handles), bit-exact; no match, raw changed between
 *                      the hooks, no live job, MEM1 past the pool, gates, half-shutter stop in the TOP.
 *   HS                 Highlights / Shadows: wf_hs_bend vs the formula (121 pairs), never down, 0 / 0 bit-identical to the
 *                      film builder before the feature (FNV), only N changes; rows, film shot + log + EXIF + preview; Log +
 *                      LUT bent after the load (.FRT unbent), an h / s change bends again without a load; flat Log / Off
 *                      ignore it; hl / sh in the file
 *   LOG [big]          the module's log: appended across boots, time stamps, a busy card (lines wait, nothing removed,
 *                      the refusal logged), ring full (lines counted), a short write (no line twice); big: a log over
 *                      512 KB becomes WINDYFLM.OLD at boot
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
#include <time.h>
#include <unistd.h>
#define WF_ML_HEADERS
#include "wf_core.h"
#include "wf_film.h"
#include "wf_loglut.h"
#include "wf_fx.h"
#include "wf_expsim.h"
#include "host_ml.h"
#include "wf_probe.h"
#include "wf_rawb.h"
#include "wf_dev.h"
/* this build's input curve decode tables (generated header, static copies for the checks) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "wf_loglut_tab.h"
#pragma GCC diagnostic pop
#if !WF_LL_HAVE_C1
#define wf_ll_dec_c1 wf_ll_dec
#endif
#if !WF_LL_HAVE_C2
#define wf_ll_dec_c2 wf_ll_dec
#endif
static const uint32_t *const t_dec[WF_LL_NINC] = { wf_ll_dec, wf_ll_dec_c1, wf_ll_dec_c2 };

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
#ifdef WF_BUILD_5D2
#define FRAME_C 0x50000000u                      /* the 5D2 probe's raw buffer (MEM1) */
#define C_USED  0x2300000u
#else
#define FRAME_C 0x46000000u                      /* the EOS M probe's other raw buffer */
#define C_USED  0x1100000u
#endif
#define SENT 0xD15EA5EDu
#define PROP_ASPECT 0x80040059u
#define PROP_PSU1 0x0206000au                        /* PROP_PICSTYLE_OF_USERDEF1 (DIGIC 4 / 5) */
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
    FILE *a = fopen("AUTOEXEC.BIN", "ab");                     /* ML booted from it (wf_card_read: missing vs busy) */
    if (a) fclose(a);
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
static const uint32_t *tabs[128][WF_NPRINTS];    /* the table of (catalogue row, variant) written into the pack */
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

/* a display-referred look cube at size n (sRGB in, sRGB out), not the identity: R a little G, G^1.15, B down and lifted */
static void dcube_write(const char *path, int n)
{
    FILE *f = fopen(path, "w");
    fprintf(f, "TITLE \"display look %d\"\nLUT_3D_SIZE %d\n", n, n);
    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++) {
                double R = r / (double)(n - 1), G = g / (double)(n - 1), B = b / (double)(n - 1);
                fprintf(f, "%.6f %.6f %.6f\n", 0.92 * R + 0.08 * G, pow(G, 1.15), 0.85 * B + 0.1);
            }
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
static uint32_t *fa, *fb, *fc, *in, *out, *ref, *out2;
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
        for (int i = 0; i < GN; i++) gs[i] = word(0, gy[i & 3], 0, gy[i & 3]);
        wf_ll_words_c(gs, GN, (int)(uintptr_t)&ll);
        int y18s = WY0(gs[0]), yws = WY0(gs[1]);
        /* every curve slot of this build (1 Film 1 in User Def. 1, 2 Neutral): the same checks; a slot without a CSV
         * reads as sRGB. The cross style (Film 1 under Neutral, Neutral under User Def. 1) must read sRGB. */
        static const char *const inkey[WF_INC_N] = { "srgb", "film1", "neutral" };
        static const int inps[WF_INC_N] = { 0, 0x21, 0x84 };   /* each curve's Picture Style (wf_menu.c wf_inc_ps) */
        static struct wf_lltabs lc[WF_INC_N];
        int ncurve = 0;
        for (int k = 1; k < WF_INC_N; k++) {
            if (!wf_ll_curve_have(k)) {
                wf_incurve = k;
                chk(wf_incurve_t() == WF_INC_SRGB, "no %s curve in this build: it reads as sRGB", inkey[k]);
                wf_incurve = WF_INC_SRGB;
                continue;
            }
            ncurve++;
            char inl[16], sl[24];
            snprintf(inl, sizeof inl, " in-%s", inkey[k]);
            log_mark();
            wf_incurve = k;
            chk(LOGW(inl, 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000),
                "Input curve %s loaded: %s", inkey[k], log_line("lut "));
            wf_ll_incurve = k;
            rc = wf_loglut_load("ML/DATA/VLOG.CUB", WF_CURVE_VLOG, &lc[k], &inf);
            chk(rc == WF_LL_OK && ll_same(&lc[k], wf_ll_tables()) && !ll_same(&ll, &lc[k]),
                "%s tables = wf_loglut_load with the curve, not the sRGB ones (rc %d)", inkey[k], rc);
            for (int j = 1; j < k; j++)
                if (wf_ll_curve_have(j))
                    chk(!ll_same(&lc[j], &lc[k]), "%s tables differ from the %s ones (own .FRT key)", inkey[k], inkey[j]);
            sb = shot(in, out, FW, FH);
            memcpy(ref, in, NW * 4);
            ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&lc[k]);
            chk(ndiff(out, ref, NW) == 0 && sb == 0, "%s shot: bit-exact vs wf_ll_words on its tables", inkey[k]);
            /* the curve's 18 % grey (its first code at 0.18) comes out where sRGB's (118) does; light above 1.0 at code
             * 250 comes out above sRGB white through the log segment */
            const uint32_t *dk = t_dec[k];
            int g = 0;
            while (g < 255 && dk[4 * g] < 3019899) g++;
            for (int i = 0; i < GN; i++) gc[i] = i & 1 ? word(0, 250, 0, 250) : word(0, g, 0, g);
            wf_ll_words_c(gc, GN, (int)(uintptr_t)&lc[k]);
            int y18c = WY0(gc[0]), ywc = WY0(gc[1]);
            chk(abs(y18c - y18s) <= 2 && WY1(gc[0]) == y18c, "18 %% grey: %s code %d -> %d, sRGB code 118 -> %d", inkey[k], g,
                y18c, y18s);
            if (dk[1000] > 20971520)
                chk(ywc >= yws + 8, "headroom: %s code 250 (%.2f) -> %d, sRGB white -> %d", inkey[k], dk[1000] / 16777216.0, ywc,
                    yws);
            struct wf_look lk;
            wf_look_now(&lk);
            chk(lk.output == WF_OUT_LLUT && lk.incurve == k, "the look carries the input curve %s", inkey[k]);
            int bad = k == WF_INC_FILM1 ? 0x84 : 0x21;
            snprintf(sl, sizeof sl, "srgb(style %02x)", bad);
            log_mark();
            h_li_set("picstyle", bad);
            chk(LOGW(sl, 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
                && ll_same(&ll, wf_ll_tables()) && wf_incurve == k && wf_incurve_t() == WF_INC_SRGB,
                "Picture Style %02x: %s chosen, sRGB tables: %s", bad, inkey[k], log_line("lut "));
            wf_look_now(&lk);
            chk(lk.incurve == WF_INC_SRGB, "the look says sRGB under style %02x", bad);
            log_mark();
            h_li_set("picstyle", inps[k]);
            chk(LOGW(inl, 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&lc[k], wf_ll_tables()),
                "Picture Style %02x again: %s tables (.FRT): %s", inps[k], inkey[k], log_line("lut "));
            /* the Contrast of the measurement (Film 1 -2, Neutral 0): another one reads sRGB; back, and the log says it */
            int con = k == WF_INC_FILM1 ? -2 : 0;
            log_mark();
            h_ps_contrast = con + 1;
            snprintf(sl, sizeof sl, "srgb(contrast %d)", con + 1);
            chk(LOGW(sl, 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&ll, wf_ll_tables())
                && wf_style_why(k) == WF_PSW_CONTRAST && wf_incurve_t() == WF_INC_SRGB,
                "Contrast %d: %s chosen, sRGB tables: %s", con + 1, inkey[k], log_line("lut "));
            log_mark();
            h_ps_contrast = con;
            snprintf(sl, sizeof sl, "%s b%02x c%d", inl, wf_style_base() & 0xff, con);
            chk(LOGW(sl, 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&lc[k], wf_ll_tables()),
                "Contrast %d again: %s tables, log '%s': %s", con, inkey[k], sl, log_line("lut "));
            if (k == WF_INC_FILM1) {             /* User Def. 1 based on a built-in style (Auto) is not the Film 1 file */
                log_mark();
                h_prop_send(PROP_PSU1, 0x87);
                chk(LOGW("srgb(base 87)", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
                    && ll_same(&ll, wf_ll_tables()) && wf_style_why(k) == WF_PSW_BASE,
                    "User Def. 1 base Auto: sRGB tables: %s", log_line("lut "));
                log_mark();
                h_prop_send(PROP_PSU1, 0x41);
                chk(LOGW(" in-film1 b41 c-2", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
                    && ll_same(&lc[k], wf_ll_tables()), "User Def. 1 base PC 1 (a style file): Film 1 tables: %s",
                    log_line("lut "));
            }
            h_ps_contrast = -10;
            h_li_set("picstyle", 0);
        }
        if (ncurve) {
            log_mark();
            wf_incurve = WF_INC_SRGB;
            chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&ll, wf_ll_tables())
                && !strstr(log_line("lut "), " in-"), "back to sRGB: the same tables as before: %s", log_line("lut "));
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
    wf_bloom = 0;                                /* Wide alone: a new halation-stage key */
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

/* ------------------------------------------------------------------ FXAB: WindyGrain / WindyHalation against the old stages */
/* the most negative (b - a) luma of an annulus round the frame centre, and how many pixels it darkened */
static double ring_neg(const uint32_t *a, const uint32_t *b, int w, int h, int r0, int r1, int *nneg)
{
    int wpr = w / 2, mn = 0;
    *nneg = 0;
    for (int y = h / 2 - r1; y <= h / 2 + r1; y++)
        for (int x = (w / 2 - r1) / 2; x <= (w / 2 + r1) / 2; x++) {
            int dx = 2 * x - w / 2, dy = y - h / 2, d2 = dx * dx + dy * dy;
            if (y < 0 || y >= h || x < 0 || x >= wpr || d2 < r0 * r0 || d2 >= r1 * r1) continue;
            int d = (WY0(b[y * wpr + x]) - WY0(a[y * wpr + x]) + WY1(b[y * wpr + x]) - WY1(a[y * wpr + x])) / 2;
            if (d < 0) { (*nneg)++; if (d < mn) mn = d; }
        }
    return mn;
}

/* mean (b - a) luma over the pixels x0 <= x < x1, y0 <= y < y1 of a w-wide frame (x0, x1 even) */
static double box_dy(const uint32_t *a, const uint32_t *b, int w, int x0, int x1, int y0, int y1)
{
    int wpr = w / 2;
    int64_t s = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0 / 2; x < x1 / 2; x++)
            s += WY0(b[y * wpr + x]) - WY0(a[y * wpr + x]) + WY1(b[y * wpr + x]) - WY1(a[y * wpr + x]);
    return (double)s / ((double)(x1 - x0) * (y1 - y0));
}

/* over n words: the mean and sd of the (b - a) luma, the mean (b - a) of U and V */
static void flat_stats(const uint32_t *a, const uint32_t *b, int n, double *my, double *sy, double *mu, double *mv)
{
    int64_t s = 0, s2 = 0, su = 0, sv = 0;
    for (int i = 0; i < n; i++) {
        int d0 = WY0(b[i]) - WY0(a[i]), d1 = WY1(b[i]) - WY1(a[i]);
        s += d0 + d1;
        s2 += d0 * d0 + d1 * d1;
        su += WU(b[i]) - WU(a[i]);
        sv += WV(b[i]) - WV(a[i]);
    }
    *my = (double)s / (2.0 * n);
    *sy = sqrt((double)s2 / (2.0 * n) - *my * *my);
    *mu = (double)su / n;
    *mv = (double)sv / n;
}

static void t_fxab(void)
{
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    int hk = 0, gk = 0, nneg, post;
    double sd, sd_f, sd_n, sd_s, cm, ho, hi, hl;
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fC].id);
    wf_fx_retry_ms = 500;
    mod_init(1);
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    disc_r = 60;
    gen_frame(in, FW, FH, 1);
    chk(WAIT(look_table_ready(), 10000), "table published");
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    shot(in, out2, FW, FH);
    chk(ndiff(ref, out2, NW) == 0, "both effects off: the frame is the film look alone");

    /* ---- the stages as they were: Yedlin grain Normal, halation Wide + Glow (cfg bloom) */
    wf_grain = 2;
    wf_halo = 1;
    wf_bloom = 2;
    post = wf_fx_mode();
    chk(wf_fx_keys(post, &hk, &gk) && WAIT(wf_fx_hk == hk && wf_fx_gk == gk, 120000), "old keys ready (%s | %s)",
        log_line("fx halation"), log_line("fx grain"));
    usleep(200000);
    shot(in, out, FW, FH);
    cm = corner(ref, out, FW, &sd);
    double sd_old = sd, cm_old = cm;
    double ho_old = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'y', 0);
    double hi_old = ring(ref, out, FW, FH, 6, disc_r - 8, 'y', 0);
    memcpy(out2, out, NW * 4);                       /* the old stages' frame, for the difference below */

    /* ---- WindyHalation: the Genesis light wrap (no grain, so the ring measures the halo alone) */
    wf_grain = 0;
    wf_halo = 3;                                     /* Bloom stays Normal: the light wrap has none (no bloom in the key) */
    chk(wf_fx_keys(post, &hk, &gk) && WF_FXK_HALO(hk) == 3 && WF_FXK_BLOOM(hk) == 0 && WAIT(wf_fx_hk == hk, 120000),
        "Wrap ready, no bloom with it (%s)", log_line("fx halation"));
    usleep(200000);
    shot(in, out, FW, FH);
    chk(ndiff(ref, out, NW) > 2000, "WindyHalation changes the frame (%d of %d words)", ndiff(ref, out, NW), NW);
    ho = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'y', 0);
    hi = ring(ref, out, FW, FH, 6, disc_r - 8, 'y', 0);
    hl = ring_neg(ref, out, FW, FH, disc_r + 6, disc_r + 40, &nneg);
    chk(ho > 1.0, "halo round the light: dY %+.2f (old stages %+.2f)", ho, ho_old);
    chk(nneg == 0 && hl == 0, "the halo only adds light (%d darker pixels, worst %+.0f)", nneg, hl);
    chk(fabs(hi) < 3.0, "difference of blur keeps the core of the light (dY %+.2f, old stages %+.2f)", hi, hi_old);
    chk(fabs(ho - ho_old) > 0.3, "not the old halation: %+.2f vs %+.2f", ho, ho_old);
#ifndef WF_BUILD_5D2
    /* ---- 11-15: WindyHalation in Diana halves (TOP + BOT builds) against the whole frame. The light crosses the seam
     * (the centre row): its glow in the 6 rows above and below the seam, beside and over the disc. */
    {
        int x0 = FW / 2 - disc_r - 40, x1 = FW / 2 + disc_r + 40;
        double wa = box_dy(ref, out, FW, x0, x1, FH / 2 - 6, FH / 2), wb = box_dy(ref, out, FW, x0, x1, FH / 2, FH / 2 + 6);
        double wa2 = box_dy(ref, out, FW, x0, x1, FH / 2 - 60, FH / 2 - 6), wb2 = box_dy(ref, out, FW, x0, x1, FH / 2 + 6, FH / 2 + 60);
        halves = 1;
        log_mark();
        shot(in, out2, FW, FH);
        halves = 0;
        chk(LOGW("(TOP ", 5000), "WindyHalation halves shot: %s", log_line("(TOP "));
        double ha = box_dy(ref, out2, FW, x0, x1, FH / 2 - 6, FH / 2), hb = box_dy(ref, out2, FW, x0, x1, FH / 2, FH / 2 + 6);
        double ha2 = box_dy(ref, out2, FW, x0, x1, FH / 2 - 60, FH / 2 - 6), hb2 = box_dy(ref, out2, FW, x0, x1, FH / 2 + 6, FH / 2 + 60);
        printf("INFO halves seam: whole %+.2f | %+.2f (wide %+.2f | %+.2f), halves %+.2f | %+.2f (wide %+.2f | %+.2f), max diff %d,"
               " mean %+.3f\n", wa, wb, wa2, wb2, ha, hb, ha2, hb2, max_d(out, out2, NW), mean_dy(out, out2, NW));
        chk(fabs(ha - wa) < 1.0 && fabs(hb - wb) < 1.0, "WindyHalation halves = whole frame at the seam: above %+.2f (whole %+.2f),"
            " below %+.2f (whole %+.2f)", ha, wa, hb, wb);
        chk(fabs(ha2 - wa2) < 1.0 && fabs(hb2 - wb2) < 1.0, "WindyHalation halves = whole frame near the seam: above %+.2f"
            " (whole %+.2f), below %+.2f (whole %+.2f)", ha2, wa2, hb2, wb2);
    }
#else
    /* ---- 11-15: every 5D2 shot is TOP + BOT (seam at the centre row): the light's glow is the same above and below */
    {
        int x0 = FW / 2 - disc_r - 40, x1 = FW / 2 + disc_r + 40;
        double a = box_dy(ref, out, FW, x0, x1, FH / 2 - 6, FH / 2), b = box_dy(ref, out, FW, x0, x1, FH / 2, FH / 2 + 6);
        double a2 = box_dy(ref, out, FW, x0, x1, FH / 2 - 60, FH / 2 - 6), b2 = box_dy(ref, out, FW, x0, x1, FH / 2 + 6, FH / 2 + 60);
        printf("INFO TOP / BOT seam: %+.2f | %+.2f (wide %+.2f | %+.2f)\n", a, b, a2, b2);
        chk(fabs(a - b) < 1.0 && fabs(a2 - b2) < 1.0, "WindyHalation over the TOP / BOT seam: above %+.2f, below %+.2f (wide"
            " %+.2f, %+.2f)", a, b, a2, b2);
    }
#endif

    /* ---- WindyGrain: the Genesis grain, its three amounts */
    wf_halo = 0;
    wf_bloom = 0;
    wf_grain = 4;
    chk(wf_fx_keys(post, &hk, &gk) && (gk & 256) && WAIT(wf_fx_gk == gk, 120000), "WindyGrain fine ready (%s)",
        log_line("fx grain"));
    usleep(200000);
    shot(in, out, FW, FH);
    corner(ref, out, FW, &sd_f);
    wf_grain = 5;
    chk(wf_fx_keys(post, &hk, &gk) && WAIT(wf_fx_gk == gk, 120000), "WindyGrain normal ready (%s)", log_line("fx grain"));
    usleep(200000);
    shot(in, out, FW, FH);
    cm = corner(ref, out, FW, &sd_n);
    chk(fabs(cm) < 3 && sd_n > 0.8 && sd_n < 25, "grain in the dark: mean %+.2f, sd %.2f", cm, sd_n);
    wf_grain = 6;
    chk(wf_fx_keys(post, &hk, &gk) && WAIT(wf_fx_gk == gk, 120000), "WindyGrain strong ready (%s)", log_line("fx grain"));
    usleep(200000);
    shot(in, out, FW, FH);
    corner(ref, out, FW, &sd_s);
    chk(sd_f < sd_n && sd_n < sd_s, "grain amount is monotonic: fine %.2f < normal %.2f < strong %.2f", sd_f, sd_n, sd_s);
    chk(sd_n < sd_old * 0.9 + 6 && sd_n > 0.3, "not the Yedlin grain: sd %.2f vs %.2f", sd_n, sd_old);
    chk(fabs(cm_old) < 3, "the old grain was centred too (mean %+.2f, sd %.2f)", cm_old, sd_old);

    /* ---- 11-15: WindyGrain (Strong) is centred and the same at every level, it adds in display space. Flat grey
     * frames, the whole frame: the old rank mapping of yg_gtab gave about dY +0.15, dU +0.12 here. */
    {
        static const int lv[2] = { 70, 150 };
        double fm[2], fs[2], fu[2], fv[2];
        for (int k = 0; k < 2; k++) {
            for (int i = 0; i < NW; i++) in[i] = word(0, lv[k], 0, lv[k]);
            memcpy(ref, in, NW * 4);
            ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
            shot(in, out, FW, FH);
            flat_stats(ref, out, NW, &fm[k], &fs[k], &fu[k], &fv[k]);
        }
        chk(fabs(fm[0]) < 0.05 && fabs(fm[1]) < 0.05, "WindyGrain is centred: dY %+.3f at Y %d, %+.3f at Y %d", fm[0], lv[0],
            fm[1], lv[1]);
        chk(fabs(fu[0]) < 0.05 && fabs(fv[0]) < 0.05 && fabs(fu[1]) < 0.05 && fabs(fv[1]) < 0.05,
            "WindyGrain chroma is centred: dU %+.3f / %+.3f, dV %+.3f / %+.3f", fu[0], fu[1], fv[0], fv[1]);
        chk(fs[0] > 0.5 && fabs(fs[1] / fs[0] - 1) < 0.05, "WindyGrain is the same at both levels: sd %.3f / %.3f", fs[0],
            fs[1]);
    }

    /* ---- 11-15: Bloom on its own row, the Gauge. Bloom alone (Halation Off) glows; the Wide halo reaches further on a
     * narrower gauge (16 mm > 35 mm > 65 mm); the Yedlin grain is stronger at 16 mm and weaker at 65 mm, the Even grain
     * weaker at 65 mm (finer than a pixel) */
    {
        double bo, far[3], near[3], gsd[3], esd[3];
        gen_frame(in, FW, FH, 1);
        memcpy(ref, in, NW * 4);
        ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
        wf_grain = 0;
        wf_halo = 0;
        wf_bloom = 2;
        wf_gauge = 1;
        chk(wf_fx_keys(post, &hk, &gk) && WF_FXK_HALO(hk) == 0 && WF_FXK_BLOOM(hk) == 2 && WAIT(wf_fx_hk == hk && !wf_fx_gk, 120000),
            "bloom alone ready (%s)", log_line("fx halation"));
        usleep(200000);
        shot(in, out, FW, FH);
        bo = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'y', 0);
        chk(bo > 0.5, "Bloom alone glows round the light: dY %+.2f", bo);
        wf_bloom = 0;
        wf_halo = 1;
        for (int ga = 0; ga <= WF_GAUGE_MAX; ga++) {
            wf_gauge = ga;
            chk(wf_fx_keys(post, &hk, &gk) && WF_FXK_HGAUGE(hk) == ga && WAIT(wf_fx_hk == hk, 120000), "Wide at gauge %d ready (%s)",
                ga, log_line("fx halation"));
            usleep(200000);
            shot(in, out, FW, FH);
            near[ga] = ring(ref, out, FW, FH, disc_r + 6, disc_r + 30, 'y', 0);
            far[ga] = ring(ref, out, FW, FH, disc_r + 60, disc_r + 160, 'y', 0);
        }
        /* the weights move to the coarser bases on a narrower gauge: more of the halo leaves the disc's edge for
         * r+6..30 (11-15: EOS M 7.18 / 3.10 / 1.39, 5D2 6.33 / 2.62 / 1.31); r+60..160 stays near 0 for Wide */
        chk(near[0] > near[1] * 1.5 && near[1] > near[2] * 1.4 && near[1] > 1.0 && far[0] >= far[1] && far[1] >= far[2] - 0.02,
            "the halo spreads wider on a narrower gauge: dY at r+6..30 16 mm %+.2f, 35 mm %+.2f, 65 mm %+.2f (r+60..160:"
            " %+.2f %+.2f %+.2f)", near[0], near[1], near[2], far[0], far[1], far[2]);
        wf_halo = 0;
        for (int ga = 0; ga <= WF_GAUGE_MAX; ga++) {
            wf_gauge = ga;
            wf_grain = 2;
            chk(wf_fx_keys(post, &hk, &gk) && WF_FXK_GGAUGE(gk) == ga && WAIT(wf_fx_gk == gk && !wf_fx_hk, 120000),
                "Normal grain at gauge %d ready (%s)", ga, log_line("fx grain"));
            usleep(200000);
            shot(in, out, FW, FH);
            corner(ref, out, FW, &gsd[ga]);
            wf_grain = 5;
            chk(wf_fx_keys(post, &hk, &gk) && WAIT(wf_fx_gk == gk, 120000), "Even Normal at gauge %d ready (%s)", ga,
                log_line("fx grain"));
            usleep(200000);
            shot(in, out, FW, FH);
            corner(ref, out, FW, &esd[ga]);
        }
        chk(gsd[0] > gsd[1] * 1.3 && gsd[1] > gsd[2] * 1.2, "Yedlin grain sd by gauge: 16 mm %.2f > 35 mm %.2f > 65 mm %.2f",
            gsd[0], gsd[1], gsd[2]);
        chk(esd[2] < esd[1] * 0.8 && esd[0] > esd[1] * 0.7, "Even grain sd by gauge: 16 mm %.2f, 35 mm %.2f, 65 mm %.2f (finer"
            " than a pixel: weaker)", esd[0], esd[1], esd[2]);
        wf_gauge = WF_GAUGE_DEF;
    }

    wf_grain = 0;
    wf_halo = 0;
    wf_bloom = 0;
    chk(WAIT(!wf_fx_hk && !wf_fx_gk, 20000), "effects off: both keys cleared");
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
            "hl=3\nsh= -2\ngrain=fine\nhalation=spkbloom\nmist=b2\npreview=fast\nexposure=smooth\nexptrim=-5\nexif=off\ninput=jpeg\nbogus=1\n"
            "userwb=1,5600,2,-3\n", wf_films[fA].id, code1);
    mod_init(1);
    chk(wf_film_id == wf_films[fA].id && wf_out == WF_OUT_LLUT && wf_curve == 4 && wf_flut == 1 && wf_lut_ei == -2,
        "film %d output %d curve %d flut %d lutei %d", wf_film_id, wf_out, wf_curve, wf_flut, wf_lut_ei);
    chk(wf_incurve == WF_INC_FILM1 && wf_incurve_t() == (wf_ll_curve_have(WF_INC_FILM1) ? WF_INC_FILM1 : WF_INC_SRGB),
        "incurve %d (in use %d)", wf_incurve, wf_incurve_t());
    chk(wf_print == 1 && wf_light == 3 && wf_strength == 2 && wf_grain == 1 && wf_halo == 2 && wf_bloom == 2 && wf_mist == 7
        && wf_pv_mode == 2 && wf_gauge == WF_GAUGE_DEF,
        "print %d light %d strength %d grain %d halation %d mist %d preview %d", wf_print, wf_light, wf_strength, wf_grain, wf_halo,
        wf_mist, wf_pv_mode);
    chk(wf_hl == 3 && wf_sh == -2, "hl %d sh %d", wf_hl, wf_sh);
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
             "strength=strong\nhl=3\nsh=-2\ngrain=normal\nhalation=spk\nbloom=normal\ngauge=35\nmist=b2\npreview=fast\nexposure=%s\n"
             "exptrim=-5\nexif=off\n"
             "userwb=1,5600,2,-3\n", wf_films[fA].id, code1, WF_EXP_CAN ? "smooth" : "camera");
    chk(b && !strcmp(b, want), "saved 1.5 s after a change, every key:\n%s", b ? b : "(none)");
    free(b);
    /* Canon busy with a shot: the save waits (a create then could leave two WINDYFLM.CFG entries), then follows */
    h_li_set("job", 1);
    wf_row_step(WF_ROW_GRAIN, 1);
    h_clock_add_ms += 2000;
    usleep(400000);
    b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && strstr(b, "grain=normal\n"), "Canon busy: the change not saved yet");
    free(b);
    h_li_set("job", 0);
    h_clock_add_ms += 2000;
    usleep(400000);
    b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && !strstr(b, "grain=normal\n") && strstr(b, "grain="), "Canon idle: saved");
    free(b);
    wf_row_step(WF_ROW_GRAIN, -1);
    h_clock_add_ms += 2000;
    usleep(400000);
    b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && !strcmp(b, want), "back to the first file");
    free(b);
    put_cfg("film=99999\noutput=nope\nlutei=77\nexptrim=99\ncurve=x\nprint=\nhl=7\nsh=\n");
    wf_cfg_load();
    chk(wf_film_id == wf_films[fA].id && wf_out == WF_OUT_LLUT && wf_lut_ei == -2 && wf_exp_trim == -5 && wf_curve == 4
        && wf_hl == 3 && wf_sh == -2,
        "garbage values ignored");
    int inc0 = wf_incurve;
    put_cfg("incurve=Neutral\n");
    wf_cfg_load();
    chk(wf_incurve == WF_INC_NEUTRAL && wf_incurve_t() == (wf_ll_curve_have(WF_INC_NEUTRAL) ? WF_INC_NEUTRAL : WF_INC_SRGB),
        "incurve=Neutral read: %d (in use %d)", wf_incurve, wf_incurve_t());
    wf_incurve = inc0;
    /* the film-stage words: since 11-15 (evennormal, wrap, bloom=, gauge=) and the earlier ones (genstrong, gen, and the
     * halation words bloom / spkbloom that held the bloom) */
    int gr0 = wf_grain, ha0 = wf_halo, bl0 = wf_bloom, ga0 = wf_gauge;
    put_cfg("grain=EvenNormal\nhalation=wrap\n");
    wf_cfg_load();
    chk(wf_grain == 5 && wf_halo == 3 && wf_bloom == 0, "grain=EvenNormal / halation=wrap read: %d %d (bloom %d)", wf_grain,
        wf_halo, wf_bloom);
    put_cfg("grain=genstrong\nhalation=gen\n");
    wf_cfg_load();
    chk(wf_grain == 6 && wf_halo == 3, "grain=genstrong / halation=gen (before 11-15) read: %d %d", wf_grain, wf_halo);
    put_cfg("halation=spkbloom\n");
    wf_cfg_load();
    chk(wf_halo == 2 && wf_bloom == 2, "halation=spkbloom (before 11-15): Tight + Bloom Normal (%d %d)", wf_halo, wf_bloom);
    put_cfg("halation=bloom\nbloom=strong\ngauge=16\n");
    wf_cfg_load();
    chk(wf_halo == 1 && wf_bloom == 3 && wf_gauge == 0, "halation=bloom with a bloom= line: Wide + Strong, gauge 16 mm (%d %d %d)",
        wf_halo, wf_bloom, wf_gauge);
    put_cfg("bloom=low\nhalation=halation\ngauge=65\n");
    wf_cfg_load();
    chk(wf_halo == 1 && wf_bloom == 1 && wf_gauge == 2, "bloom= before halation= still counts, gauge 65 mm (%d %d %d)", wf_halo,
        wf_bloom, wf_gauge);
    put_cfg("grain=even\nhalation=wrapx\nbloom=x\ngauge=70\n");
    wf_cfg_load();
    chk(wf_grain == WF_GRAIN_DEF && wf_halo == WF_HALO_DEF && wf_bloom == WF_BLOOM_DEF && wf_gauge == WF_GAUGE_DEF,
        "unknown grain / halation / bloom / gauge words: the defaults (%d %d %d %d)", wf_grain, wf_halo, wf_bloom, wf_gauge);
    /* the rows and labels name no product: Grain / Halation / Bloom / Gauge; the halo by its look (per-film profiles Wide,
     * 3-bounce Tight, light wrap Wrap) */
    wf_grain = 5;
    wf_halo = 1;
    wf_bloom = 3;
    wf_gauge = 0;
    int lw = !strcmp(wf_row_text(WF_ROW_HALO), "Wide") && !strcmp(wf_row_text(WF_ROW_BLOOM), "Strong")
             && !strcmp(wf_row_text(WF_ROW_GAUGE), "16 mm");
    wf_halo = 2;
    wf_gauge = 2;
    lw = lw && !strcmp(wf_row_text(WF_ROW_HALO), "Tight") && !strcmp(wf_row_text(WF_ROW_GAUGE), "65 mm");
    wf_halo = 3;
    chk(lw && !strcmp(wf_row_text(WF_ROW_HALO), "Wrap") && !strcmp(wf_row_text(WF_ROW_GRAIN), "Even Normal")
        && !strcmp(wf_row_name(WF_ROW_GRAIN), "Grain") && !strcmp(wf_row_name(WF_ROW_HALO), "Halation")
        && !strcmp(wf_row_name(WF_ROW_BLOOM), "Bloom") && !strcmp(wf_row_name(WF_ROW_GAUGE), "Gauge"),
        "rows Grain / Halation / Bloom / Gauge; Wide / Tight / Wrap, Bloom Strong, 16 / 65 mm, Even Normal (%s, %s, %s)",
        wf_row_name(WF_ROW_HALO), wf_row_text(WF_ROW_HALO), wf_row_text(WF_ROW_GRAIN));
    wf_grain = gr0;
    wf_halo = ha0;
    wf_bloom = bl0;
    wf_gauge = ga0;
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
            if (!(wf_films[k].prints & 1)) continue;   /* a chain film: no Scan variant (print stocks only) */
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
        chk(r == WF_RD_SHORT && sz == 32 + 4 * 24 + 4 * TW * 4, "wf_card_read: short read = error, size still set");
        r = wf_card_read("ML/DATA/NONE.DAT", 0, h, 4, &sz);
        chk(r == WF_RD_MISSING && sz == 0, "wf_card_read: missing file, size 0 (%d)", r);
        rename("AUTOEXEC.BIN", "AUTOEXEC.OFF");                  /* the card answers for nothing: Canon holds it */
        r = wf_card_read("ML/DATA/NONE.DAT", 0, h, 4, &sz);
        chk(r == WF_RD_BUSY && sz == 0, "wf_card_read: no answer for AUTOEXEC.BIN either = card busy (%d)", r);
        rename("AUTOEXEC.OFF", "AUTOEXEC.BIN");
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

/* ------------------------------------------------------------------ PRINT:chain (own process: the pack index of an
 * earlier mod_init stays in the statically linked module) */
/* a chain film: no Scan / Cine / Paper, only print stocks (bits WF_PRINT_STOCK0..). Its catalogue row must be READY
 * (else the browser leaves it off the strip and wf_load says "Film table not on the card"), it loads, the Print stock
 * row starts on its first stock and steps to the next, and each shot uses that stock's table. */
static void t_print_chain(void)
{
    int kc = -1, v1 = -1, v2 = -1;
    for (int k = 0; k < wf_nfilms && kc < 0; k++)
        if (!(wf_films[k].flags & WF_F_LUTCAN) && (wf_films[k].prints >> WF_PRINT_STOCK0))
            kc = k;
    if (kc < 0) {
        printf("  (no chain film in this catalogue: chain part skipped)\n");
        return;
    }
    for (int v = WF_PRINT_STOCK0; v < WF_NPRINTS; v++)
        if (wf_films[kc].prints & (1u << v)) {
            if (v1 < 0) v1 = v;
            else if (v2 < 0) v2 = v;
        }
    chk(wf_films[kc].flags & WF_F_READY, "chain film %d %s: READY (its print tables were there at build time)",
        wf_films[kc].id, wf_films[kc].code);
    chk(v1 >= 0 && v2 >= 0 && !(wf_films[kc].prints & 7), "chain film: two print stocks, no Scan / Cine / Paper");
    if (v2 < 0)
        return;
    struct pk ec[] = { { kc, v1, tab_make(12, 0, 0, 0) }, { kc, v2, tab_make(-10, 0, 0, 0) } };
    pack_write(ec, 2, 0);
    put_cfg("film=%d\noutput=film\nprint=scan\ngrain=off\nhalation=off\nmist=off\n", wf_films[kc].id);
    mod_init(1);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(wf_avail(wf_films[kc].id), 5000) && wf_load(wf_films[kc].id) == WF_LOAD_OK && wf_loaded() == wf_films[kc].id,
        "chain film: on the card and loads");
    char t1[16] = "", t2[16] = "";
    snprintf(t1, sizeof t1, "%s", wf_row_text(WF_ROW_PRINT));
    chk(wf_row_avail(WF_ROW_PRINT) && strcmp(t1, "Scan") && strcmp(t1, "Cine") && strcmp(t1, "Paper"),
        "chain film: Print stock row on its first stock (%s), not Scan", t1);
    film_shot("chain film, first print stock", kc, v1, 1, 12, 1.5);
    chk(wf_row_step(WF_ROW_PRINT, 1), "chain film: Print stock steps");
    snprintf(t2, sizeof t2, "%s", wf_row_text(WF_ROW_PRINT));
    chk(strcmp(t1, t2) != 0, "chain film: the next stock (%s -> %s)", t1, t2);
    film_shot("chain film, second print stock", kc, v2, 1, -10, 1.5);
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

/* ------------------------------------------------------------------ PROBE (RAW probe P0) */
#define PRB_REC 4696                             /* sizeof(struct wf_prb), wf_probe.h: both cameras */
typedef char prb_rec_check[sizeof(struct wf_prb) == PRB_REC ? 1 : -1];
static uint32_t fnv4k(const uint8_t *p)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4096; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
#ifdef WF_BUILD_5D2
/* 5D2: the Canon words of one develop job (P0_5D2_SPEC.md) as the shim serves them; the raw = MEM1 + 0x76C8 in fc */
#define P5_H_TOP 0x00200000u
#define P5_H_BOT 0x00200100u
#define P5_P     0x00300000u
#define P5_SH    0x00400000u
#define P5_RAW   (FRAME_C + 0x76C8u)
#define P5_PITCH 10136u
static void p5_job(uint32_t magic, uint32_t mem1, uint32_t sh)
{
    h_mem_set(0x369Cu + 0x3C, P5_H_TOP); h_mem_set(0x369Cu + 0x40, 0x17);
    h_mem_set(0x3630u + 0x2C, P5_H_BOT); h_mem_set(0x3630u + 0x30, 0x17);
    for (int k = 0; k < 2; k++) {
        uint32_t h = k ? P5_H_BOT : P5_H_TOP;
        h_mem_set(h, magic); h_mem_set(h + 4, P5_P); h_mem_set(h + 8, 2 + k); h_mem_set(h + 12, 1); h_mem_set(h + 16, 0);
    }
    for (int i = 0; i < 15; i++)
        h_mem_set(P5_P + 4 * i, 0x1000 + i);
    for (int i = 0; i < 8; i++)                  /* GetShootParam's 8 words at P + 0x64 (+0x6C = the flags) */
        h_mem_set(P5_P + 0x64 + 4 * i, 0x3000 + i);
    h_mem_set(P5_P + 0x4C, 0x17); h_mem_set(P5_P + 0x48, 5); h_mem_set(P5_P + 0x6C, 0x00040100);
    h_mem_set(P5_P + 0x538, 0xABCD0003u); h_mem_set(P5_P + 0xBE8, 1); h_mem_set(P5_P + 0xBE0, 0x00310000);
    h_mem_set(P5_P + 0xC68, mem1);
    h_mem_set(0x2AC34u + 0x54, mem1 + 0x76C8u);
    h_mem_set(0x8FBC, sh);
    h_mem_set(P5_SH | 0x4208, mem1 + 0x76C8u); h_mem_set(P5_SH | 0x420C, 0x144D8); h_mem_set(P5_SH | 0x4210, 0x60000000u);
    h_mem_set(P5_SH | 0x4308, FRAME_A); h_mem_set(P5_SH | 0x4D08, FRAME_A); h_mem_set(P5_SH | 0x4408, 0x5A000000u);
    for (int k = 0; k < 2; k++) {
        uint32_t b = k ? 0x2A9F4u : 0x2AB14u;
        for (int i = 0; i < 16; i++)
            h_mem_set(b + 0x38 + 4 * i, 0x2000 + 0x100 * k + i);
        h_mem_set(b + 0x44, mem1);
        h_mem_set(b + 0x5C, mem1 + 0x24D1C00u);
        h_mem_set(b + 0x74, k ? FRAME_B : FRAME_A);
    }
    static const uint32_t ed[5] = { 0xC0F04208u, 0xC0F04A08u, 0xC0F04308u, 0xC0F04D08u, 0xC0F04408u };
    for (int i = 0; i < 5; i++)
        h_mem_set(ed[i], 0x51000000u + 0x100 * i);
    h_romcall_ret[0] = mem1; h_romcall_ret[1] = 0x17; h_romcall_ret[2] = P5_P + 0x64; h_romcall_ret[3] = 0x00310000;
    h_romcall_ret[4] = mem1 + 0x76C8u;
}
/* one half of a shot: the frame from `in`, this half's CRAW read shadow (a08: raw row 56 / 1928) */
static void p5_half(int bot)
{
    int nh = NW / 2;
    h_mem_set(P5_SH | 0x4A08, P5_RAW + (bot ? 1928u : 56u) * P5_PITCH);
    if (bot)
        memcpy(fb, in + nh, nh * 4);
    else
        memcpy(fa, in, nh * 4);
    half_call(bot ? S_BOT : S_TOP, bot ? FRAME_B : FRAME_A, 0, 0);
}
static const char *p5_src(char *e, int n, const char *name, uint32_t a)    /* "probe <name> <a>: fnv x3 -> same" */
{
    const uint8_t *c = (const uint8_t *)fc + (a - FRAME_C);
    uint32_t h0 = fnv4k(c), h1 = fnv4k(c + 0x800000), h2 = fnv4k(c + 0x1000000);
    snprintf(e, n, "probe %s %08x: %08x %08x %08x -> %08x %08x %08x same", name, a, h0, h1, h2, h0, h1, h2);
    return e;
}
static void t_probe_5d2(void)
{
    uint8_t *c = (uint8_t *)fc;
    for (uint32_t i = 0; i < C_USED; i++) c[i] = (uint8_t)((i * 0x9E3779B1u) >> 24);
    p5_job(0xFFCD0761u, FRAME_C, P5_SH);
    struct pk pe[] = { { fA, 0, tab_make(12, 6, -6, 0) }, { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(pe, 2, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(look_table_ready(), 10000), "film table published");
    int nh = NW / 2;
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));
    const char *bin = "ML/LOGS/WFPROBE.BIN";
    long n = -1;
    char *f, e[200];
    const uint32_t a08t = P5_RAW + 56u * P5_PITCH, a08b = P5_RAW + 1928u * P5_PITCH;

    /* off (the boot state): no record, no file, no ROM call */
    chk(wf_prb_can() && wf_probe_mode == WF_PRB_OFF, "the 5D2 can probe; off at boot");
    log_mark();
    p5_half(0); p5_half(1);
    chk(ndiff(fa, ref, nh) == 0 && ndiff(fb, ref + nh, nh) == 0, "off: both halves graded as always");
    usleep(300000);
    f = get_file(bin, &n);
    chk(!log_has("probe #") && !f && h_romcalls == 0, "off: no probe line, no WFPROBE.BIN, no ROM call");
    free(f);

    /* on: the TOP and the BOT of one shot back to back (no flush between): one record each */
    wf_probe_mode = WF_PRB_ON;
    log_mark();
    p5_half(0); p5_half(1);
    chk(ndiff(fa, ref, nh) == 0 && ndiff(fb, ref + nh, nh) == 0, "on: both halves graded exactly as without the probe");
    chk(LOGW("probe #1 TOP r0 48000000 r1 00000000 r2 0 mode 1", 5000), "on: TOP header '%s'", log_line("probe #1"));
    chk(LOGW("probe #2 BOT r0 4c000000 r1 00000000 r2 0 mode 1", 5000), "on: BOT header '%s'", log_line("probe #2"));
    chk(!log_has("not probed"), "on: the BOT is taken while the TOP's record waits (one slot per kind)");
    snprintf(e, sizeof e, "probe pb+00 %08x 00000017 ffcd0761 %08x 00000002 00000001 00000000 %08x", P5_H_TOP, P5_P, P5_RAW);
    chk(log_has(e), "on: TOP H, job id, H[0..4], Mem1ToRaw ('%s')", e);
    snprintf(e, sizeof e, "probe pb+20 00000017 00000005 00040100 00000003 00000001 00310000 %08x 00001000", FRAME_C);
    chk(log_has(e), "on: P fields (done mask a halfword), MEM1, P[0] ('%s')", e);
    snprintf(e, sizeof e, "probe pb+60 00001009 0000100a 0000100b 0000100c 0000100d 0000100e %08x %08x", P5_SH, P5_RAW);
    chk(log_has(e), "on: P[9..14], SH, the capture shadow ('%s')", e);
    snprintf(e, sizeof e, "probe pb+80 000144d8 60000000 %08x deadbeef deadbeef 48000000 48000000 5a000000", a08t);
    chk(log_has(e), "on: shadows, TOP a08 at raw row 56 ('%s')", e);
    snprintf(e, sizeof e, "probe pb+c0 00002108 %08x 0000210a 0000210b 0000210c 0000210d 0000210e 4c000000",
             FRAME_C + 0x24D1C00u);
    chk(log_has(e), "on: the BOT's own Mem1ToYuv block ('%s')", e);
    chk(log_has("probe edmac 208 51000000 a08 51000100 308 51000200 d08 51000300 408 51000400"), "on: registers '%s'",
        log_line("probe edmac"));
    chk(log_has(p5_src(e, sizeof e, "raw0", P5_RAW)), "on: raw0 = MEM1 + 0x76C8, unchanged by the worker ('%s')",
        log_line("probe raw0"));
    chk(log_has(p5_src(e, sizeof e, "qr", P5_RAW)) && log_has(p5_src(e, sizeof e, "m2r", P5_RAW)), "on: qr, m2r");
    chk(log_has(p5_src(e, sizeof e, "a08", a08t)) && log_has(p5_src(e, sizeof e, "a08", a08b)),
        "on: a08 of each half (rows 56 / 1928)");
    chk(!log_has("probe nav") && h_romcalls == 0, "on: no ROM call");
    chk(WAIT((free(get_file(bin, &n)), n == 2 * PRB_REC), 3000), "on: two records in WFPROBE.BIN (%ld B)", n);
    f = get_file(bin, &n);
    if (f && n == 2 * PRB_REC) {
        const uint32_t *w = (const uint32_t *)f, *w2 = (const uint32_t *)(f + PRB_REC);
        const int so = (int)(offsetof(struct wf_prb, src) / 4), co = (int)offsetof(struct wf_prb, cpy);
        chk(w[0] == 0x42504657u && w[1] == 0x0102 && w[2] == 1 && w[7] == 1 && w2[2] == 2 && w2[7] == (1 | 1 << 8),
            "records: version 0x0102, seq 1 TOP, seq 2 BOT (mode bits 8-15)");
        chk(w[so] == P5_RAW && w[so + 2] == a08t && w2[so + 2] == a08b, "records: sources");
        chk(memcmp(f + co, c + 0x76C8 + 0x1000000, 1024) == 0 &&
            memcmp(f + PRB_REC + co + 2 * 1024, c + (a08b - FRAME_C) + 0x1000000, 1024) == 0,
            "records: the 1 KB copies from +16 MB (inside the raw)");
    }
    free(f);

    /* on + ROM call: the five getters per half, with that half's job handle */
    wf_probe_mode = WF_PRB_NAV;
    log_mark();
    p5_half(0); p5_half(1);
    chk(LOGW("probe #4 BOT", 5000) && h_romcalls == 10, "nav: 5 getters per half (%d calls)", h_romcalls);
    chk(h_romcall_fn[0] == 0xFF8AC4B8u && h_romcall_arg[0] == P5_H_TOP && h_romcall_fn[3] == 0xFF8AEB84u &&
        h_romcall_fn[4] == 0xFF9A550Cu && h_romcall_arg[4] == 0xC0F04208u && h_romcall_arg[5] == P5_H_BOT,
        "nav: GetImageBuffer(H) .. GetFileContainer(H), shamem_read(0xC0F04208); the BOT with its own H");
    snprintf(e, sizeof e, "probe nav 0: rc %x tmp 00000000 |", FRAME_C);
    chk(log_has(e), "nav: GetImageBuffer = MEM1 ('%s')", log_line("probe nav 0"));
    chk(log_has("probe nav 2: rc 300064 tmp 00000000 | 00003000 00003001 00040100 00003003 00003004 00003005 00003006 "
                "00003007"), "nav: GetShootParam and its 8 words ('%s')", log_line("probe nav 2"));
    chk(ndiff(fa, ref, nh) == 0 && ndiff(fb, ref + nh, nh) == 0, "nav: both halves graded as always");

    /* not a live job (H[0] is not the magic): P not followed, no ROM call, no raw0 */
    p5_job(0, FRAME_C, P5_SH);
    int rc0 = h_romcalls;
    log_mark();
    p5_half(0); p5_half(1);
    chk(LOGW("probe #6 BOT", 5000) && h_romcalls == rc0, "not a live job: no ROM call");
    chk(log_has("probe pb+20 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000"),
        "not a live job: P not followed ('%s')", log_line("probe pb+20"));
    chk(log_has("probe raw0 00000000: outside the shoot pool, not read"), "not a live job: no raw0");

    /* MEM1 past the probe's pool (0x5C000000), SH not word aligned: raw0 / qr / a08 / m2r not read */
    p5_job(0xFFCD0761u, 0x5C000000u, 0x00400002u);
    wf_probe_mode = WF_PRB_ON;
    log_mark();
    p5_half(0); p5_half(1);
    chk(LOGW("probe #8 BOT", 5000) && log_has("probe raw0 5c0076c8: outside the shoot pool, not read") &&
        log_has("probe m2r 5c0076c8: outside the shoot pool, not read"), "MEM1 past 0x5C000000: raw0, m2r not read");
    chk(log_has("probe qr 00000000: outside the shoot pool, not read") && log_has("probe pb+60 00001009 0000100a "
        "0000100b 0000100c 0000100d 0000100e 00400002 00000000"), "SH not aligned: no shadow read");
    chk(ndiff(fa, ref, nh) == 0 && ndiff(fb, ref + nh, nh) == 0, "both halves graded as always");
    mod_deinit();
}
#else
static void probe_nn(uint32_t r2)
{
    memcpy(fa, in, NW * 4);
    memset(regs, 0, sizeof regs); regs[0] = 0x00ABC000u; regs[1] = FRAME_A; regs[2] = r2; call_hook(S_NN);
    memcpy(out, fa, NW * 4);
}
static void t_probe(const char *cs)
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

    /* PROBE:boot - the first record of a boot starts WFPROBE.BIN over: only once Canon is idle (a create while it saves
       a shot could leave two entries of the name) and once the old file is verified gone */
    if (!strcmp(cs, "boot")) {
        const char *bin = "ML/LOGS/WFPROBE.BIN";
        long n = -1, n0 = 3 * PRB_REC;
        char *old = calloc(1, n0);
        put_file(bin, old, n0);
        free(old);
        wf_probe_mode = WF_PRB_ON;
        h_li_set("job", 1);
        log_mark();
        probe_nn(0);
        chk(LOGW("probe #1 ", 5000), "boot: the shot probed and logged");
        chk(LOGW("probe: record #1 not written to ML/LOGS/WFPROBE.BIN yet: busy rc 1 (shot job 1)", 3000),
            "boot, Canon busy: the new file waits ('%s')", log_line("probe: record #1"));
        usleep(500000);
        free(get_file(bin, &n));
        chk(n == n0, "boot, Canon busy: the old file untouched (%ld B, was %ld)", n, n0);
        snprintf(h_fio_busy, sizeof h_fio_busy, "%s", bin);  /* idle, but the card does not let go of the old file */
        h_li_set("job", 0);
        usleep(700000);
        free(get_file(bin, &n));
        chk(n == n0 && !log_has("probe: record #1 written"), "old file not removable: no new file over it (%ld B)", n);
        h_fio_busy[0] = 0;
        chk(WAIT((free(get_file(bin, &n)), n == PRB_REC), 3000) &&
            LOGW("probe: record #1 written to ML/LOGS/WFPROBE.BIN on try ", 3000),
            "card lets go: the old file removed, a new one with record #1 (%ld B): '%s'", n,
            log_line("probe: record #1 written"));
        char *f = get_file(bin, &n);
        chk(f && n == PRB_REC && ((const uint32_t *)f)[0] == 0x42504657u && ((const uint32_t *)f)[2] == 1,
            "boot: record #1 at the start of the new file");
        free(f);
        mod_deinit();
        return;
    }

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
    chk(log_has("probe raw2 00000000: outside the shoot pool, not read"), "on: a raw in neither raw buffer: no raw2");
    chk(!log_has("probe nav") && h_nav_calls == 0, "on: no ROM call");
    chk(WAIT((free(get_file("ML/LOGS/WFPROBE.BIN", &n)), n == PRB_REC), 3000), "on: one record in WFPROBE.BIN (%ld B)", n);
    f = get_file("ML/LOGS/WFPROBE.BIN", &n);
    if (f && n >= PRB_REC) {
        const uint32_t *w = (const uint32_t *)f;
        chk(w[0] == 0x42504657u && w[1] == 2 && w[2] == 1 && w[5] == FRAME_A && w[6] == 0 && w[7] == 1,
            "record: magic, version 2, seq 1, r1, r2, mode");
        chk(w[8 + 0x50 / 4] == raw_u - 0x40000000u && w[8 + 0x68 / 4] == a1_u - 0x40000000u, "record: parameter block");
        chk(memcmp(f + 600, b + 0x1100002, 1024) == 0, "record: 1 KB of the raw from the +16 MB window");
        chk(memcmp(f + 600 + 3 * 1024, (const uint8_t *)in, 1024) == 0,
            "record: A1 copy from +0, taken at entry (the frame before grading)");
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

    /* the card refuses WFPROBE.BIN (busy with the shot): the record waits and retries, the next one is still logged */
    const char *bin = "ML/LOGS/WFPROBE.BIN";
    long n0;
    free(get_file(bin, &n0));
    snprintf(h_fio_busy, sizeof h_fio_busy, "%s", bin);
    log_mark();
    probe_nn(0);
    chk(LOGW("probe: record #4 not written to ML/LOGS/WFPROBE.BIN yet: open rc 0 (shot job ", 5000),
        "busy card: '%s'", log_line("probe: record #4"));
    probe_nn(0);
    chk(LOGW("probe #5 ", 5000), "busy card: the next shot is probed and logged while #4 waits");
    probe_nn(0);
    chk(LOGW("probe: 1 NN calls not probed so far", 5000), "busy card: a third call while two records wait is skipped ('%s')",
        log_line("not probed so far"));
    usleep(300000);
    free(get_file(bin, &n));
    chk(n == n0, "busy card: WFPROBE.BIN untouched (%ld B, was %ld)", n, n0);
    h_fio_busy[0] = 0;
    chk(WAIT((free(get_file(bin, &n)), n == n0 + 2 * PRB_REC), 5000), "card back: records #4 and #5 written (%ld B)", n);
    chk(LOGW("probe: record #4 written to ML/LOGS/WFPROBE.BIN on try ", 3000), "'%s'", log_line("probe: record #4 written"));
    f = get_file(bin, &n);
    if (f && n == n0 + 2 * PRB_REC) {
        const uint32_t *w3 = (const uint32_t *)(f + 3 * PRB_REC), *w4 = (const uint32_t *)(f + 4 * PRB_REC);
        chk(w3[0] == 0x42504657u && w3[2] == 4 && w4[0] == 0x42504657u && w4[2] == 5, "records 4 and 5 at their offsets");
    }
    free(f);

    /* a short write tears a record: the retry writes it again at its own offset */
    snprintf(h_fio_wpath, sizeof h_fio_wpath, "%s", bin);
    h_fio_wmax = 1000;
    log_mark();
    probe_nn(0);
    chk(LOGW("probe: record #6 not written to ML/LOGS/WFPROBE.BIN yet: write rc 1000", 5000), "short write: '%s'",
        log_line("probe: record #6"));
    chk(WAIT((free(get_file(bin, &n)), n == n0 + 3 * PRB_REC) && log_has("probe: record #6 written"), 5000),
        "short write: record #6 whole after the retry (%ld B)", n);
    f = get_file(bin, &n);
    if (f && n == n0 + 3 * PRB_REC) {
        const uint32_t *w5 = (const uint32_t *)(f + 5 * PRB_REC);
        chk(w5[0] == 0x42504657u && w5[2] == 6 && w5[1] == 2, "record 6 at its offset, not torn");
    }
    free(f);
    h_fio_wpath[0] = 0;

    /* the first shot after power-on (the block empty, pb+0x50 = 0): the raw from EDMAC WR0, raw2 = the other buffer
       (raw-film-engine, the 11-14 run: S = WR0 - 32626440, raw = S + 480242, buffers 32 MB apart); copies: raw and
       raw2 from +16 MB, A0 / A1 from +0 */
    uint8_t *c = (uint8_t *)fc;
    for (uint32_t i = 0; i < C_USED; i++) c[i] = (uint8_t)((i * 0x2545F491u) >> 24);
    h_pb[0x50 / 4] = 0;
    h_edmac[0] = 0x48004894u + 32626440u;
    const uint32_t r1a = 0x48004894u + 480242u, r2a = r1a - 0x2000000u, o2 = r2a - FRAME_C;
    log_mark();
    probe_nn(0);
    uint32_t q0 = fnv4k(c + o2), q1 = fnv4k(c + o2 + 0x800000), q2 = fnv4k(c + o2 + 0x1000000);
    snprintf(e, sizeof e, "probe raw2 %08x: %08x %08x %08x -> %08x %08x %08x same", r2a, q0, q1, q2, q0, q1, q2);
    chk(LOGW(e, 5000), "empty block: raw2 = the other buffer ('%s')", log_line("probe raw2"));
    snprintf(e, sizeof e, "probe raw %08x: ", r1a);
    chk(log_has(e), "empty block: the raw from EDMAC WR0 ('%s')", log_line("probe raw "));
    chk(WAIT((free(get_file(bin, &n)), n == n0 + 4 * PRB_REC), 5000), "empty block: record #7 written (%ld B)", n);
    f = get_file(bin, &n);
    if (f && n == n0 + 4 * PRB_REC) {
        const uint8_t *r = (const uint8_t *)f + 6 * PRB_REC;
        const uint32_t *w = (const uint32_t *)r;
        const int so = (int)(offsetof(struct wf_prb, src) / 4), co = (int)offsetof(struct wf_prb, cpy);
        chk(w[2] == 7 && w[so] == r1a && w[so + 1] == r2a, "record 7: raw %08x, raw2 %08x", w[so], w[so + 1]);
        chk(memcmp(r + co + 1024, c + o2 + 0x1000000, 1024) == 0, "record 7: raw2 copy from +16 MB");
        chk(memcmp(r + co + 2 * 1024, b + 0x10000, 1024) == 0, "record 7: A0 copy from +0");
    }
    free(f);
    h_pb[0x50 / 4] = raw_u - 0x40000000u;
    h_edmac[0] = 0x30000000u;
    mod_deinit();
}
#endif

/* ------------------------------------------------------------------ LOG: the module's log on the card */
static int count_str(const char *b, const char *s)
{
    int n = 0;
    for (const char *p = b; p && (p = strstr(p, s)); p++)
        n++;
    return n;
}
/* the number after "log: " in the last line holding s, -1 none */
static int log_num(const char *s)
{
    const char *l = log_line(s), *p = strstr(l, "log: ");
    int v = -1;
    if (p)
        sscanf(p, "log: %d", &v);
    return v;
}
static void t_log(const char *cs)
{
    long n;
    char *b;
    std_pack();
    if (!strcmp(cs, "big")) {                    /* a log over 512 KB becomes WINDYFLM.OLD at the first write of a boot */
        size_t big = 600 * 1024;
        char *z = malloc(big);
        memset(z, 'x', big);
        z[big - 1] = '\n';
        put_file(LOG_PATH, z, big);
        free(z);
        h_li_set("job", 1);                      /* Canon busy: the rename waits */
        mod_init(0);
        usleep(700000);
        b = get_file("ML/LOGS/WINDYFLM.OLD", &n);
        chk(!b && (free(get_file(LOG_PATH, &n)), n == (long)big), "Canon busy: the big log not renamed yet");
        free(b);
        h_li_set("job", 0);
        chk(LOGW("WindyFilm ", 5000), "boot line written");
        b = get_file(LOG_PATH, &n);
        chk(b && n < 64 * 1024 && b[0] != 'x' && strstr(b, "WindyFilm "), "new log after the rotation (%ld B)", n);
        free(b);
        b = get_file("ML/LOGS/WINDYFLM.OLD", &n);
        chk(b && n == (long)big && b[0] == 'x', "the big log kept as WINDYFLM.OLD (%ld B)", n);
        free(b);
        mod_deinit();
        return;
    }
    if (!strcmp(cs, "new")) {                    /* no log on the card, Canon busy at boot: the log is made once idle */
        unlink(LOG_PATH);
        h_li_set("job", 1);
        mod_init(0);
        wf_log("a line while Canon is busy");
        usleep(700000);
        b = get_file(LOG_PATH, &n);
        chk(!b, "Canon busy: no log made yet");
        free(b);
        h_li_set("job", 0);
        chk(LOGW("a line while Canon is busy\n", 3000), "Canon idle: the log made, the waiting lines in it");
        b = get_file(LOG_PATH, &n);
        chk(b && strstr(b, "WindyFilm ") && count_str(b, "a line while Canon is busy\n") == 1,
            "the boot line, the waiting line once");
        free(b);
        chk(LOGW("log: card refused ", 3000) && strstr(log_line("log: card refused"), "(first: busy rc 1, job 1)"),
            "refusal logged: '%s'", log_line("log: card refused"));
        mod_deinit();
        return;
    }
    /* appended across boots: the old lines stay, the boot line follows them */
    put_file(LOG_PATH, "old boot line\n", 14);
    put_file("ML/LOGS/WINDYFLM.OLD", "older\n", 6);
    mod_init(0);
    chk(LOGW("WindyFilm ", 5000), "boot line written");
    b = get_file(LOG_PATH, &n);
    chk(b && !strncmp(b, "old boot line\n", 14) && strstr(b, "WindyFilm "), "appended to the old log (%ld B)", n);
    free(b);
    b = get_file("ML/LOGS/WINDYFLM.OLD", &n);
    chk(b && !strcmp(b, "older\n"), "a small log is not rotated (WINDYFLM.OLD untouched)");
    free(b);
    const char *l = log_line("WindyFilm ");
    int s = -1, t = -1;
    char c = 0;
    chk(sscanf(l, "%d.%d %c", &s, &t, &c) == 3 && s >= 0 && t >= 0 && t <= 9 && c == 'W', "time stamp '%.20s'", l);
    usleep(500000);

    /* card busy: the lines wait in the ring, the file is not removed or touched; then the refusal is logged */
    long before;
    free(get_file(LOG_PATH, &before));
    log_at = before;
    snprintf(h_fio_busy, sizeof h_fio_busy, "%s", LOG_PATH);
    int creates = h_fio_creates;
    for (int i = 0; i < 30; i++)
        wf_log("busy test line %d", i);
    usleep(700000);
    free(get_file(LOG_PATH, &n));
    chk(n == before && h_fio_creates == creates, "busy card: log untouched, never re-created (%ld B, was %ld)", n, before);
    h_fio_busy[0] = 0;
    chk(LOGW("busy test line 29\n", 3000), "card back: the waiting lines written");
    b = get_file(LOG_PATH, &n);
    int ok = b != 0;
    const char *p = b ? b + log_at : 0;
    for (int i = 0; i < 30 && ok; i++) {
        char k[32];
        snprintf(k, sizeof k, "busy test line %d\n", i);
        const char *q = strstr(p, k);
        ok = q != 0;
        if (q) p = q;
    }
    chk(ok && count_str(b + log_at, "busy test line 7\n") == 1, "all 30 lines once, in order");
    chk(b && !strncmp(b, "old boot line\n", 14), "the old lines still there");
    free(b);
    chk(LOGW("log: card refused ", 3000) && strstr(log_line("log: card refused"), "(first: open rc 0, job "),
        "refusal logged: '%s'", log_line("log: card refused"));

    /* ring full: lines dropped and counted, the first ones kept */
    usleep(300000);
    free(get_file(LOG_PATH, &before));
    log_at = before;
    snprintf(h_fio_busy, sizeof h_fio_busy, "%s", LOG_PATH);
    for (int i = 0; i < 300; i++)
        wf_log("ring line %03d .................................................................................", i);
    usleep(300000);
    h_fio_busy[0] = 0;
    chk(LOGW("lines lost so far", 3000), "ring full: '%s'", log_line("lines lost so far"));
    int lost = log_num("lines lost so far");
    b = get_file(LOG_PATH, &n);
    int kept = b ? count_str(b + log_at, "ring line ") : 0;
    chk(b && strstr(b + log_at, "ring line 000 ") && !strstr(b + log_at, "ring line 299 ") && kept > 100 && lost > 0 &&
        kept + lost >= 300, "ring full: %d kept (the first ones), %d counted lost", kept, lost);
    free(b);

    /* a short write: the rest follows, no line twice */
    usleep(300000);
    free(get_file(LOG_PATH, &before));
    log_at = before;
    snprintf(h_fio_wpath, sizeof h_fio_wpath, "%s", LOG_PATH);
    h_fio_wmax = 20;
    wf_log("short write line A, long enough for the short write to cut it");
    wf_log("short write line B");
    chk(LOGW("short write line B\n", 3000), "short write: the rest followed");
    b = get_file(LOG_PATH, &n);
    chk(b && count_str(b + log_at, "short write line A, long enough for the short write to cut it\n") == 1 &&
        count_str(b + log_at, "short write line B\n") == 1, "short write: each line once, whole");
    free(b);
    chk(LOGW("log: card refused 1 writes", 3000) && strstr(log_line("log: card refused"), "first: write rc 20"),
        "short write logged: '%s'", log_line("log: card refused"));
    h_fio_wpath[0] = 0;
    mod_deinit();
}

/* ------------------------------------------------------------------ RAWB (RAW develop "B") */
#ifndef WF_BUILD_5D2
#define RB_Z      0x50000000u                    /* noise */
#define RB_A      0x52000000u                    /* this shot's raw */
#define RB_P      0x54000000u                    /* another picture (the frame upside down): "the previous shot" */
#define RB_0      0x56000000u                    /* zeros */
static uint32_t *rb_map;                         /* RB_Z .. +128 MB, mapped in main() first */
#define RB_PITCH  9240
#define RB_RW     5280
#define RB_RH     3528
#define RB_PBOFF  480242u                        /* wf_cam_eosm.c: S = (+0x50 | uncached) - 480242 */
#define RB_WR0OFF 32626440u                      /* S = (WR0 | uncached) - 32626440 */
#define RB_ROWW   (FW / 2)

static void rb_pack14(const uint16_t *v, uint8_t *dst, int npx)   /* ML raw_pixblock: 8 px in 14 bytes */
{
    uint16_t *d = (uint16_t *)dst;
    for (int i = 0; i < npx; i += 8, v += 8, d += 7) {
        uint32_t a = v[0], b = v[1], c = v[2], e3 = v[3], e = v[4], f = v[5], g = v[6], h = v[7];
        d[0] = (uint16_t)(a << 2 | b >> 12);
        d[1] = (uint16_t)((b & 4095) << 4 | c >> 10);
        d[2] = (uint16_t)((c & 1023) << 6 | e3 >> 8);
        d[3] = (uint16_t)((e3 & 255) << 8 | e >> 6);
        d[4] = (uint16_t)((e & 63) << 10 | f >> 4);
        d[5] = (uint16_t)((f & 15) << 12 | g >> 2);
        d[6] = (uint16_t)((g & 3) << 14 | h);
    }
}
/* a neutral 14-bit raw of Canon's frame src (RGGB, black 2048): photosite (84 + x, 64 + y) = the sRGB decode of Y(x, y)
 * x 16000 DN x the channel's gain (R 0.55, G 1, B 0.7) + noise of +-8 DN (a dense histogram like a sensor's), clipped at
 * 14048 (green above Y ~232 piles up there: the measured white); masked columns (x < 72) and the rows above 52 at black
 * + noise. */
static void rb_raw(uint8_t *raw, const uint32_t *src)
{
    static uint16_t lin[256], row[RB_RW];
    static const int gq8[3] = { 141, 256, 179 };
    for (int i = 0; i < 256; i++) {
        double c = i / 255.0;
        lin[i] = (uint16_t)(16000.0 * (c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)) + 0.5);
    }
    for (int y = 0; y < RB_RH; y++) {
        int jy = clampi(y - 64, 0, FH - 1);
        const uint32_t *r = src + (size_t)jy * RB_ROWW;
        for (int x = 0; x < RB_RW; x++) {
            int v = 2048 + (int)(rnd() % 17) - 8;
            if (x >= 72 && y >= 52) {
                int jx = clampi(x - 84, 0, FW - 1), ch = (x & 1) + (y & 1);
                uint32_t w = r[jx / 2];
                v += lin[jx & 1 ? WY1(w) : WY0(w)] * gq8[ch] >> 8;
            }
            row[x] = (uint16_t)(v > 14048 ? 14048 : v);
        }
        rb_pack14(row, raw + (size_t)y * RB_PITCH, RB_RW);
    }
}
/* the develop of RB_A called directly, the way wf_rawb_stage does it (levels, then prepare on Canon's frame, rows) */
static int rb_ref_hl, rb_ref_sh;                 /* WF_DEV_DISP: Highlights / Shadows of the reference */
static int rb_ref_curve;                         /* WF_DEV_CUBE: the curve and cube of the reference, auto exposure on */
static const char *rb_ref_cube;
static void rb_ref(uint32_t *dst, const uint32_t *canon, int src, int roll)
{
    static struct wf_dev_tabs T;
    struct wf_ll_info inf;
    int cu = src == WF_DEV_CUBE;
    int rc = src == WF_DEV_DISP ? wf_dev_build_disp(&T, WF_DEV_EOSM, "ML/DATA/LEICAVSR.CUB", 0, rb_ref_hl, rb_ref_sh, &inf)
                                : wf_dev_build(&T, src, cu ? rb_ref_curve : 0, WF_DEV_EOSM, cu ? rb_ref_cube : 0, 0, cu, &inf);
    void *scr = memalign(32, wf_dev_scratch_bytes(FW, FH));
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = (const uint8_t *)(uintptr_t)RB_A; j.pitch = RB_PITCH;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = RB_RW; j.ay1 = RB_RH;
    int lb = 0, lw = 0;
    wf_dev_levels(scr, &j, 16, 64, &lb, &lw);
    j.black = lb ? lb : 2048;
    j.white = lw ? lw : 15200;
    j.x0 = 84; j.y0 = 64; j.w = FW; j.h = FH; j.cfa = 0;
    memcpy(dst, canon, NW * 4);
    j.canon = dst; j.canon_y0 = 0; j.canon_h = FH;
    j.roll = roll;
    struct wf_dev_stat st;
    int rp = wf_dev_prepare(scr, &T, &j, &st);
    wf_dev_rows(scr, 0, FH, dst, FW * 2);
    printf("reference develop: build %d, levels %d / %d, prepare %d, wb %d, clip %d, roll %d\n", rc, lb, lw, rp, st.wb_src,
           st.clip_quads, st.roll);
    free(scr);
}
/* B table cache (.RBT) helpers: switch the Film LUT and wait for B's tables line holding `expect` (ms = its time) */
static int rb_lut_to(int fl, const char *expect, int *ms)
{
    log_mark();
    wf_flut = fl;
    int ok = LOGW(expect, 120000);
    if (ms) {
        const char *l = log_line("rawb: tables cube");
        const char *p = l ? strstr(l, ".CUB ") : 0;
        *ms = p ? atoi(p + 5) : -1;
    }
    return ok;
}
static long rb_fsize(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_size; }
static void rb_fflip(const char *p)              /* one byte in the middle of the file */
{
    FILE *f = fopen(p, "r+b");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long at = ftell(f) / 2;
    fseek(f, at, SEEK_SET);
    int c = fgetc(f);
    fseek(f, at, SEEK_SET);
    fputc(c ^ 0x5a, f);
    fclose(f);
}
static void rb_point(uint32_t pb50, uint32_t wr0)  /* Canon's pointers as the hook reads them (cached twins) */
{
    h_pb[0x50 / 4] = pb50 ? pb50 + RB_PBOFF - 0x40000000u : 0;
    h_edmac[0] = wr0 ? wr0 + RB_WR0OFF - 0x40000000u : 0;
}
/* a shot that must stop after band 1 (power off): rows 64.. stay Canon's; rb_dev = the develop's first band */
static uint32_t *rb_dev;
static void rb_stopped(const char *what, const char *want_buf)
{
    ml_shutdown_requested = 1;
    log_mark();
    int sb = shot(in, out, FW, FH);
    ml_shutdown_requested = 0;
    chk(LOGW("rawb: STOPPED rows 64,", 5000) && strstr(log_line("rawb: STOPPED"), "(power)"), "%s: stopped at power off "
        "after the first band ('%s')", what, log_line("rawb: STOPPED"));
    chk(strstr(log_line("rawb: buf"), want_buf) != 0, "%s: '%s' (want '%s')", what, log_line("rawb: buf"), want_buf);
    chk(sb == 0 && ndiff(out + 64 * RB_ROWW, in + 64 * RB_ROWW, NW - 64 * RB_ROWW) == 0,
        "%s: the rows after the first band stay Canon's", what);
    chk(LOGW(": rbstop", 3000) && strstr(log_line(": rbstop"), "raw-stop"), "%s: status '%s'", what, log_line(": rbstop"));
}

extern int get_ms_clock(void);
static void t_rawb(void)
{
    uint32_t *R = rb_map ? rb_map : map_at(RB_Z, 0x8000000u - 4096), *alt = malloc(NW * 4);
    rb_dev = malloc(NW * 4);
    for (uint32_t i = 0; i < 0x2000000u / 4; i++) R[i] = rnd();
    gen_frame(in, FW, FH, 0);
    /* two other pictures, waves in the mid-tones: the previous shot's (in RB_P), and Canon's frame of "no match" (alt) */
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < RB_ROWW; x++) {
            int Y = 125 + (int)(80 * sin(x / 37.0) * cos(y / 53.0));
            alt[y * RB_ROWW + x] = word(0, Y, 0, Y);
        }
    rb_raw((uint8_t *)(uintptr_t)RB_P, alt);
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < RB_ROWW; x++) {
            int Y = 125 + (int)(80 * sin(x / 23.0 + y / 41.0) * cos(y / 71.0));
            alt[y * RB_ROWW + x] = word(0, Y, 0, Y);
        }
    rb_raw((uint8_t *)(uintptr_t)RB_A, in);
    std_pack();
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    h_prop_send(PROP_ASPECT, 0);
    chk(WAIT(look_table_ready(), 10000), "film table published");
    chk(wf_rawb_can() == 1 && wf_rawb == 0 && wf_rawb_state() == WF_RB_OFF, "RAW develop built in, off at boot");
    pic_quality = 0x3070000;                     /* RAW + L fine */
    drive_mode = 0;
    memset(h_pb, 0, sizeof h_pb);
    memset(h_edmac, 0, sizeof h_edmac);
    rb_point(RB_A, 0);
    memcpy(out2, in, NW * 4);
    ref_film(out2, NW, tabs[fA][0], flags_of(fA, 1));   /* the 8-bit film look */
    usleep(300000);
    int live0 = h_arena_live;

    log_mark();
    int sb = shot(in, out, FW, FH);
    usleep(300000);
    chk(sb == 0 && ndiff(out, out2, NW) == 0 && !log_has("rawb"), "off: the 8-bit film look, no rawb line");

    /* on: the sRGB base tables, then a developed film shot */
    log_mark();
    wf_rawb = 1;
    chk(LOGW("rawb: tables srgb", 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000) && h_arena_live == live0 + 1,
        "on: base tables in one shoot-memory block ('%s', blocks %d -> %d)", log_line("rawb: tables"), live0, h_arena_live);
    log_mark();
    int n0 = h_notify_n, t0 = get_ms_clock();
    sb = shot(in, out, FW, FH);
    int ms = get_ms_clock() - t0;
    rb_ref(rb_dev, in, WF_DEV_SRGB, 1);
    memcpy(ref, rb_dev, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "developed film shot (%d ms under qemu): bit-exact vs wf_dev levels + prepare + "
        "rows + wf_film_words (%d words differ)", ms, ndiff(out, ref, NW));
    chk(ndiff(out, out2, NW) > NW / 4, "the developed frame is not the 8-bit look (%d of %d words differ)", ndiff(out, out2, NW), NW);
    chk(LOGW("rawb: done rows 3456,", 5000), "log: '%s'", log_line("rawb: done"));
    chk(strstr(log_line("rawb: buf"), "buf 0 52000000 of 3,") != 0, "the pointer's buffer, candidate 0 of 3: '%s'",
        log_line("rawb: buf"));
    chk(strstr(log_line("rawb: black"), "black 2048 white 14048,") != 0 && strstr(log_line("rawb: black"), "roll 1"),
        "levels measured on the raw, shoulder on: '%s'", log_line("rawb: black"));
    chk(LOGW(": film ", 3000) && strstr(log_line(": film "), " raw"), "shot line marks the develop: '%s'", log_line(": film "));
    chk(h_notify_n > n0 && strstr(h_notify, "RAW develop"), "progress box shown %d times (last '%s')", h_notify_n - n0, h_notify);

    /* the pointer at the previous shot (another picture): candidate 1 is this shot's raw */
    rb_point(RB_P, 0);
    rb_stopped("pointer at the previous shot", "buf 1 52000000 of 3,");
    chk(ndiff(out, rb_dev, 64 * RB_ROWW) == 0, "pointer at the previous shot: the first band is the develop of this raw");
    /* the first shot after power on: +0x50 is 0, the origin from EDMAC WR0 */
    rb_point(0, RB_A);
    rb_stopped("+0x50 = 0, EDMAC WR0", "buf 0 52000000 of 3,");
    /* no candidate holds Canon's frame: the 8-bit look, bit-exact */
    rb_point(RB_A, 0);
    memcpy(ref, alt, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));
    log_mark();
    sb = shot(alt, out, FW, FH);
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "no match: the 8-bit film look, bit-exact");
    chk(LOGW("rawb: 8-bit (buffer nomatch", 3000), "no match: '%s'", log_line("rawb: 8-bit"));

    /* the camera layer's gates: the 8-bit look and one line each */
    static const struct { int q, dm, asp; uint32_t pb; const char *line; } G[] = {
        { 0x3010000, 0, 0, RB_A, "rawb: 8-bit (quality 3010000)" },
        { 0x3070001, 0, 0, RB_A, "rawb: 8-bit (quality 3070001)" },
        { 0x3070000, 1, 0, RB_A, "rawb: 8-bit (drive 1)" },
        { 0x3070000, 0, 1, RB_A, "rawb: 8-bit (aspect 1)" },
        { 0x3070000, 0, 0, 0, "rawb: 8-bit (noaddr 0)" },
        { 0x3070000, 0, 0, 0x30000000u + RB_PBOFF, "rawb: 8-bit (noaddr" },
    };
    for (unsigned g = 0; g < sizeof G / sizeof *G; g++) {
        pic_quality = G[g].q;
        drive_mode = G[g].dm;
        rb_point(G[g].pb, 0);
        if (G[g].asp) h_prop_send(PROP_ASPECT, G[g].asp);
        log_mark();
        sb = G[g].asp ? shot(in, out, 3456, 3456) : shot(in, out, FW, FH);
        chk(LOGW(G[g].line, 3000) && (G[g].asp || (sb == 0 && ndiff(out, out2, NW) == 0)), "gate: '%s' (want '%s')",
            log_line("rawb: "), G[g].line);
        if (G[g].asp) h_prop_send(PROP_ASPECT, 0);
    }
    pic_quality = 0x3070000; drive_mode = 0;
    rb_point(RB_A, 0);

    /* stops: half-shutter held 1.5 s (each read moves the clock 1 s), the 90 s limit (each read 100 s) */
    h_half = 1; h_half_step_ms = 1000;
    log_mark();
    sb = shot(in, out, FW, FH);
    h_half = 0; h_half_step_ms = 0;
    chk(LOGW("rawb: STOPPED", 5000) && strstr(log_line("rawb: STOPPED"), "(half-shutter)"), "half-shutter: '%s'",
        log_line("rawb: STOPPED"));
    h_half_step_ms = 100000;
    log_mark();
    sb = shot(in, out, FW, FH);
    h_half_step_ms = 0;
    chk(LOGW("rawb: STOPPED rows 128,", 5000) && strstr(log_line("rawb: STOPPED"), "(time)"), "time limit: '%s'",
        log_line("rawb: STOPPED"));

    /* plain log: its own tables, no shoulder */
    wf_curve = WF_CURVE_VLOG;
    log_mark();
    wf_out = WF_OUT_LOG;
    chk(LOGW("rawb: tables log c2", 10000), "log output: '%s'", log_line("rawb: tables"));
    rb_stopped("log output", "buf 0 52000000 of 3,");
    chk(strstr(log_line("rawb: black"), "roll 0") != 0, "log output: no shoulder ('%s')", log_line("rawb: black"));
    /* Log + LUT: the cube develop (the user's V-Log cube, identity) */
    cube_write("ML/DATA/VLOG.CUB", 9);
    wf_flut = 0;
    log_mark();
    wf_out = WF_OUT_LLUT;
    chk(LOGW("rawb: tables cube c2 VLOG.CUB", 60000), "Log + LUT: '%s'", log_line("rawb: tables"));
    rb_stopped("Log + LUT", "buf 0 52000000 of 3,");
    chk(strstr(log_line("rawb: black"), "roll 1") != 0, "Log + LUT: shoulder on ('%s')", log_line("rawb: black"));
    /* Leica Vivid (WF_FLUT_DISP): raw -> the sRGB develop -> the owner's display cube on the sRGB codes, no log step */
    {
        char nb[WF_LL_NAME];
        cube_write(wf_loglut_name(nb, WF_CURVE_VLOG, WF_FLUT_LEICAV, 0), 5);   /* the JPEG path's cube */
        dcube_write("ML/DATA/LEICAVSR.CUB", 33);
        uint32_t *l0 = malloc(NW * 4);
        log_mark();
        wf_flut = WF_FLUT_LEICAV;
        chk(LOGW("rawb: tables disp c0 LEICAVSR.CUB", 30000), "Leica Vivid: '%s'", log_line("rawb: tables"));
        rb_ref_hl = rb_ref_sh = 0;
        rb_ref(ref, in, WF_DEV_DISP, 1);
        log_mark();
        sb = shot(in, out, FW, FH);
        chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0,
            "Leica Vivid shot: bit-exact vs the sRGB develop + display cube (%d words differ)", ndiff(out, ref, NW));
        memcpy(l0, out, NW * 4);
        log_mark();
        wf_hl = 2; wf_sh = -2;
        chk(LOGW("rawb: tables disp c0 LEICAVSR.CUB", 30000), "Leica Vivid +2 / -2: tables again");
        rb_ref_hl = 2; rb_ref_sh = -2;
        rb_ref(ref, in, WF_DEV_DISP, 1);
        log_mark();
        sb = shot(in, out, FW, FH);
        chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0 && ndiff(out, l0, NW) > NW / 4,
            "Leica Vivid +2 / -2: bit-exact (%d words differ), bent (%d words moved)", ndiff(out, ref, NW), ndiff(out, l0, NW));
        wf_hl = wf_sh = 0; rb_ref_hl = rb_ref_sh = 0;
        wf_flut = 0;
        free(l0);
    }
    /* a CinePrint can (main 856178c): Canon Log 2 + Cinema Gamut, auto exposure, its <code>C2.CUB; a 33^3 and a 65^3 cube
     * (the load time grows with the cube), then B's table cache: ML/DATA/<name>.RBT next to the cube */
    {
        uint32_t *v0 = malloc(NW * 4);
        int ms[2] = { -1, -1 }, hm[2] = { -1, -1 };
        static const int fl[2] = { WF_FLUT_TUNG0, WF_FLUT_TUNG0 + 4 }, nn[2] = { 33, 65 };
        static const char *const cn[2] = { "ML/DATA/T250DPC2.CUB", "ML/DATA/TEKTAMC2.CUB" };
        static const char *const rn[2] = { "ML/DATA/T250DPC2.RBT", "ML/DATA/TEKTAMC2.RBT" };
        const long rsz = 4 * 9 + (long)sizeof(struct wf_dev_tabs) + 4;
        remove(rn[0]);                               /* a run directory of before may hold them */
        remove(rn[1]);
        for (int t = 0; t < 2; t++) {
            char want[64], nb[WF_LL_NAME];
            dcube_write(cn[t], nn[t]);
            chk(!strcmp(wf_loglut_name(nb, WF_LL_CLOG2, fl[t], 0), cn[t]) && WF_FLUT_CURVE(fl[t]) == WF_LL_CLOG2,
                "CinePrint %d: Canon Log 2 can, cube %s", fl[t], cn[t]);
            snprintf(want, sizeof want, "rawb: tables cube c4 %s", cn[t] + 8);
            log_mark();
            wf_curve = WF_LL_CLOG2;
            wf_flut = fl[t];
            wf_out = WF_OUT_LLUT;
            chk(LOGW(want, 120000), "CinePrint %d^3: '%s'", nn[t], log_line("rawb: tables"));
            const char *l = log_line("rawb: tables");
            const char *p = l ? strstr(l, "C2.CUB ") : 0;
            if (p)
                ms[t] = atoi(p + 7);
            chk(l && strstr(l, ".RBT none, written") && rb_fsize(rn[t]) == rsz,
                "CinePrint %d^3: no .RBT yet: built, %s written (%ld B, want %ld)", nn[t], rn[t] + 8, rb_fsize(rn[t]), rsz);
            rb_ref_curve = WF_LL_CLOG2;
            rb_ref_cube = cn[t];
            rb_ref(ref, in, WF_DEV_CUBE, 1);
            log_mark();
            sb = shot(in, out, FW, FH);
            chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0,
                "CinePrint %d^3 shot: bit-exact vs the Canon Log 2 cube develop, auto exposure (%d words differ)", nn[t],
                ndiff(out, ref, NW));
        }
        rb_ref_curve = WF_CURVE_VLOG;                /* the same 65^3 cube read as a V-Log one: a different picture */
        rb_ref(v0, in, WF_DEV_CUBE, 1);
        chk(ndiff(out, v0, NW) > NW / 4, "CinePrint: Canon Log 2 / Cinema Gamut, not V-Log (%d words differ)",
            ndiff(out, v0, NW));
        /* the cache: a hit gives the tables of a build (bit-exact shots) */
        rb_ref_curve = WF_LL_CLOG2;
        for (int t = 0; t < 2; t++) {
            char want[48];
            snprintf(want, sizeof want, "%s hit", rn[t] + 8);
            chk(rb_lut_to(fl[t], want, &hm[t]), "%d^3 again: %s ('%s')", nn[t], want, log_line("rawb: tables"));
            rb_ref_cube = cn[t];
            rb_ref(ref, in, WF_DEV_CUBE, 1);
            log_mark();
            sb = shot(in, out, FW, FH);
            chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0,
                "%d^3 from the .RBT: bit-exact vs a build (%d words differ)", nn[t], ndiff(out, ref, NW));
        }
        printf("CinePrint B tables (host): 33^3 build %d ms, .RBT %d ms; 65^3 build %d ms, .RBT %d ms\n", ms[0], hm[0],
               ms[1], hm[1]);
        /* another key: LUT EI +2/3 EV (one file per cube: EI 0 again rebuilds and rewrites it) */
        log_mark();
        wf_lut_ei = 2;
        chk(LOGW("TEKTAMC2.RBT key, written", 120000), "LUT EI +2: another key, rebuilt + rewritten ('%s')",
            log_line("rawb: tables"));
        log_mark();
        wf_lut_ei = 0;
        chk(LOGW("TEKTAMC2.RBT key, written", 120000), "LUT EI 0 again: rebuilt + rewritten ('%s')",
            log_line("rawb: tables"));
        /* damaged files: one byte flipped in the tables (hash), a cut file (size) */
        rb_fflip(rn[1]);
        chk(rb_lut_to(fl[0], "T250DPC2.RBT hit", 0) && rb_lut_to(fl[1], "TEKTAMC2.RBT bad, written", 0),
            "a flipped byte: rebuilt + rewritten ('%s')", log_line("rawb: tables"));
        log_mark();
        sb = shot(in, out, FW, FH);
        chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0,
            "after the flipped byte: bit-exact (%d words differ)", ndiff(out, ref, NW));
        chk(truncate(rn[0], 1000) == 0 && rb_lut_to(fl[0], "T250DPC2.RBT bad, written", 0) && rb_fsize(rn[0]) == rsz,
            "a cut file: rebuilt + rewritten ('%s')", log_line("rawb: tables"));
        /* the cube changed on the card under the same name: another content hash */
        cube_write(cn[0], 33);
        chk(rb_lut_to(fl[1], "TEKTAMC2.RBT hit", 0) && rb_lut_to(fl[0], "T250DPC2.RBT key, written", 0),
            "a new T250DPC2.CUB: rebuilt + rewritten ('%s')", log_line("rawb: tables"));
        rb_ref_cube = cn[0];
        rb_ref(ref, in, WF_DEV_CUBE, 1);
        log_mark();
        sb = shot(in, out, FW, FH);
        chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0,
            "the new cube: bit-exact (%d words differ)", ndiff(out, ref, NW));
        /* Canon busy at the build: no file then, written once Canon is idle (wf_fio rules) */
        remove(rn[1]);
        h_li_set("job", 1);
        log_mark();
        wf_flut = fl[1];
        int rdy = WAIT(wf_rawb_state() == WF_RB_READY, 120000);
        usleep(300000);
        long busy_sz = rb_fsize(rn[1]);
        h_li_set("job", 0);
        chk(rdy && busy_sz < 0 && LOGW("TEKTAMC2.RBT none, write later (busy)", 5000),
            "Canon busy: tables built, no file (%ld), write later ('%s')", busy_sz, log_line("rawb: tables"));
        chk(LOGW("rawb: TEKTAMC2.RBT written", 10000) && rb_fsize(rn[1]) == rsz, "Canon idle: the owed .RBT written ('%s')",
            log_line("rawb: TEKTAMC2.RBT"));
        chk(rb_lut_to(fl[0], "T250DPC2.RBT hit", 0) && rb_lut_to(fl[1], "TEKTAMC2.RBT hit", 0),
            "the owed file is a hit ('%s')", log_line("rawb: tables"));
        rb_ref_cube = 0;
        wf_flut = 0;
        wf_curve = WF_CURVE_VLOG;
        free(v0);
    }
    log_mark();
    wf_out = WF_OUT_FILM;
    chk(LOGW("rawb: tables srgb", 10000), "back to the film: base tables again");

    /* playback gives the shoot memory back; shooting builds the tables again */
    log_mark();
    h_play = 1;
    chk(LOGW("rawb: playback, shoot memory given back", 5000) && WAIT(h_arena_live == live0, 3000)
        && wf_rawb_state() == WF_RB_PLAY, "playback: memory given back (blocks %d)", h_arena_live);
    log_mark();
    h_play = 0;
    chk(LOGW("rawb: tables srgb", 10000) && WAIT(h_arena_live == live0 + 1, 2000), "after playback: tables again");
    /* no shoot memory: one line, the 8-bit look, a retry after 10 s */
    wf_rawb = 0;
    chk(WAIT(h_arena_live == live0, 3000) && wf_rawb_state() == WF_RB_OFF, "off: memory given back");
    h_shoot_fail = 1;
    log_mark();
    wf_rawb = 1;
    chk(LOGW("rawb: no shoot memory", 5000) && WAIT(wf_rawb_state() == WF_RB_NOMEM, 2000), "no memory: '%s'",
        log_line("rawb: no shoot"));
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, out2, NW) == 0 && LOGW("rawb: 8-bit (notab)", 3000), "no memory: the 8-bit look ('%s')",
        log_line("rawb: 8-bit"));
    h_shoot_fail = 0;
    log_mark();
    h_clock_add_ms += 11000;
    chk(LOGW("rawb: tables srgb", 10000), "no memory: the retry builds the tables");
    wf_rawb = 0;
    chk(WAIT(h_arena_live == live0, 3000), "off again: memory given back");
    mod_deinit();
    free(alt); free(rb_dev);
}
#else
/* 5D2: RAW develop "B" over Canon's TOP + BOT halves. CR2 row 0 sits at MEM1 + 0x76C8 - pitch (MEM1 = R5_M1, named by the job
 * handles of p5_job as wf_cam_5d2.c reads them), 5792 x 3804, pitch 10136, GBRG, black 1024. */
#define R5_M1     0x54000000u
#define R5_RAW    (R5_M1 + 0x76C8u - R5_PITCH)   /* CR2 row 0: MEM1 + 0x76C8 is row 1 (probe 11-14) */
#define R5_PITCH  10136
#define R5_RW     5792
#define R5_RH     3804
#define R5_MAPN   0x2500000u
#define R5_ROWW   (FW / 2)
static uint32_t *r5_map;                         /* mapped in main() first (before the frame mallocs) */
static void r5_pack14(const uint16_t *v, uint8_t *dst, int npx)   /* ML raw_pixblock: 8 px in 14 bytes */
{
    uint16_t *d = (uint16_t *)dst;
    for (int i = 0; i < npx; i += 8, v += 8, d += 7) {
        uint32_t a = v[0], b = v[1], c = v[2], e3 = v[3], e = v[4], f = v[5], g = v[6], h = v[7];
        d[0] = (uint16_t)(a << 2 | b >> 12);
        d[1] = (uint16_t)((b & 4095) << 4 | c >> 10);
        d[2] = (uint16_t)((c & 1023) << 6 | e3 >> 8);
        d[3] = (uint16_t)((e3 & 255) << 8 | e >> 6);
        d[4] = (uint16_t)((e & 63) << 10 | f >> 4);
        d[5] = (uint16_t)((f & 15) << 12 | g >> 2);
        d[6] = (uint16_t)((g & 3) << 14 | h);
    }
}
/* a neutral raw of Canon's frame src: photosite (168 + x, 56 + y) = sRGB decode of Y x 15000 DN x the channel gain
 * (GBRG: G at even x of even rows), noise +-8, clipped at 14600; masked columns (x < 160) / rows < 52 at black */
static void r5_raw(uint8_t *raw, const uint32_t *src)   /* the same noise every call (a raw can be made again) */
{
    rs = 0x2468ACE1u;
    static uint16_t lin[256], row[R5_RW];
    static const int gq8[3] = { 141, 256, 179 };
    for (int i = 0; i < 256; i++) {
        double c = i / 255.0;
        lin[i] = (uint16_t)(15000.0 * (c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)) + 0.5);
    }
    for (int y = 0; y < R5_RH; y++) {
        int jy = clampi(y - 56, 0, FH - 1);
        const uint32_t *r = src + (size_t)jy * R5_ROWW;
        for (int x = 0; x < R5_RW; x++) {
            int v = 1024 + (int)(rnd() % 17) - 8;
            if (x >= 160 && y >= 52) {
                int jx = clampi(x - 168, 0, FW - 1), ch = (x & 1) == (y & 1) ? 1 : (x & 1) ? 2 : 0;
                uint32_t w = r[jx / 2];
                v += lin[jx & 1 ? WY1(w) : WY0(w)] * gq8[ch] >> 8;
            }
            row[x] = (uint16_t)(v > 14600 ? 14600 : v);
        }
        r5_pack14(row, raw + (size_t)y * R5_PITCH, R5_RW);
    }
}
/* the develop called directly as wf_rawb_stage does over the two halves: levels, prepare on the TOP half, all rows */
static int r5_disp;                              /* the reference with the Leica display cube (WF_DEV_DISP) */
static void r5_ref(uint32_t *dst, const uint32_t *canon)
{
    static struct wf_dev_tabs T;
    struct wf_ll_info inf;
    int rc = r5_disp ? wf_dev_build_disp(&T, WF_DEV_5D2, "ML/DATA/LEICAVSR.CUB", 0, 0, 0, &inf)
                     : wf_dev_build(&T, WF_DEV_SRGB, 0, WF_DEV_5D2, 0, 0, 0, &inf);
    void *scr = memalign(32, wf_dev_scratch_bytes(FW, FH));
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = (const uint8_t *)(uintptr_t)R5_RAW; j.pitch = R5_PITCH;
    j.ax0 = 160; j.ay0 = 52; j.ax1 = R5_RW; j.ay1 = R5_RH;
    int lb = 0, lw = 0;
    wf_dev_levels(scr, &j, 16, 144, &lb, &lw);
    j.black = lb ? lb : 1024;
    j.white = lw ? lw : 15600;
    j.x0 = 176; j.y0 = 56; j.w = 5600; j.h = FH; j.cfa = 2;     /* columns 8 .. 5607 (w a multiple of 32) */
    memcpy(dst, canon, NW * 4);
    j.canon = dst + 4; j.canon_pw = FW / 2; j.canon_y0 = 0; j.canon_h = FH / 2;
    j.roll = 1;
    struct wf_dev_stat st;
    int rp = wf_dev_prepare(scr, &T, &j, &st);
    wf_dev_rows(scr, 0, FH, dst + 4, FW * 2);
    printf("reference develop: build %d, levels %d / %d, prepare %d, wb %d, clip %d, roll %d\n", rc, lb, lw, rp, st.wb_src,
           st.clip_quads, st.roll);
    free(scr);
}
static int r5_count(const char *s)               /* lines after the mark that contain s */
{
    long n;
    char *b = get_file(LOG_PATH, &n), *p = b ? b + (n > log_at ? log_at : n) : 0;
    int c = 0;
    while (p && (p = strstr(p, s)) != 0) {
        c++;
        p += strlen(s);
    }
    free(b);
    return c;
}
/* the TOP hook, then `between` (may change the raw), then the BOT hook */
static void r5_shot_mid(const uint32_t *src, uint32_t *dst, const uint32_t *between)
{
    int nh = NW / 2;
    memcpy(fa, src, nh * 4);
    memcpy(fb, src + nh, (NW - nh) * 4);
    memset(regs, 0, sizeof regs); regs[R_TOP] = FRAME_A; call_hook(S_TOP);
    if (between)
        r5_raw((uint8_t *)(uintptr_t)R5_RAW, between);
    memset(regs, 0, sizeof regs); regs[R_BOT] = FRAME_B; call_hook(S_BOT);
    memcpy(dst, fa, nh * 4);
    memcpy(dst + nh, fb, (NW - nh) * 4);
}

static void t_rawb(void)
{
    if (!r5_map)
        r5_map = map_at(R5_M1, R5_MAPN);
    uint32_t *alt = malloc(NW * 4), *dev = malloc(NW * 4);
    int nh = NW / 2;
    gen_frame(in, FW, FH, 0);
    for (int y = 0; y < FH; y++)                 /* another picture: waves in the mid-tones */
        for (int x = 0; x < R5_ROWW; x++) {
            int Y = 125 + (int)(80 * sin(x / 23.0 + y / 41.0) * cos(y / 71.0));
            alt[y * R5_ROWW + x] = word(0, Y, 0, Y);
        }
    r5_raw((uint8_t *)(uintptr_t)R5_RAW, in);
    std_pack();
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fA].id);
    mod_init(1);
    chk(WAIT(look_table_ready(), 10000), "film table published");
    chk(wf_rawb_can() == 1 && wf_rawb == 0 && wf_rawb_state() == WF_RB_OFF, "RAW develop built in, off at boot");
    pic_quality = 0x3070000;
    drive_mode = 0;
    p5_job(0xFFCD0761u, R5_M1, P5_SH);
    memcpy(out2, in, NW * 4);
    ref_film(out2, NW, tabs[fA][0], flags_of(fA, 1));   /* the 8-bit film look */
    usleep(300000);
    int live0 = h_arena_live;
    log_mark();
    int sb = shot(in, out, FW, FH);
    usleep(300000);
    chk(sb == 0 && ndiff(out, out2, NW) == 0 && !log_has("rawb"), "off: the 8-bit film look, no rawb line");

    /* on: tables, then a developed TOP + BOT */
    log_mark();
    wf_rawb = 1;
    chk(LOGW("rawb: tables srgb", 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000) && h_arena_live == live0 + 1,
        "on: base tables in one shoot-memory block ('%s')", log_line("rawb: tables"));
    log_mark();
    int n0 = h_notify_n;
    sb = shot(in, out, FW, FH);
    r5_ref(dev, in);
    memcpy(ref, dev, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "TOP + BOT developed: bit-exact vs wf_dev levels + prepare (TOP half) + rows + "
        "wf_film_words (%d words differ)", ndiff(out, ref, NW));
    chk(ndiff(out, out2, NW) > NW / 4, "and not the 8-bit look");
    usleep(500000);
    chk(LOGW("rawb: buf 0 54004f30 of 1,", 3000) && r5_count("rawb: done rows 1872,") == 2 && !log_has("rawb: 8-bit"),
        "log: buffer MEM1 + 0x76C8 - pitch (CR2 row 0), both halves done ('%s')", log_line("rawb: buf"));
    chk(log_has("rawb: black 1024 white 14600,") && log_has("rawb: BOT half continues the TOP"), "TOP levels measured (black 1024, white 14600), BOT continues ('%s')", log_line("rawb: black"));
    chk(LOGW(": film ", 3000) && strstr(log_line(": film "), " raw"), "shot line marks the develop: '%s'",
        log_line(": film "));
    chk(h_notify_n > n0, "progress box shown (%d)", h_notify_n - n0);

    /* the raw is another picture: no develop, both halves the 8-bit look */
    r5_raw((uint8_t *)(uintptr_t)R5_RAW, alt);
    log_mark();
    sb = shot(in, out, FW, FH);
    usleep(500000);
    chk(sb == 0 && ndiff(out, out2, NW) == 0, "no match: the 8-bit film look, bit-exact");
    chk(LOGW("rawb: 8-bit (buffer nomatch", 3000) && log_has("rawb: 8-bit (TOP half not developed)"),
        "no match logged: '%s' / BOT '%s'", log_line("rawb: 8-bit (buffer"), log_line("rawb: 8-bit (TOP"));
    r5_raw((uint8_t *)(uintptr_t)R5_RAW, in);

    /* the raw changes between the hooks: the BOT is not continued */
    log_mark();
    r5_shot_mid(in, out, alt);
    usleep(500000);
    chk(ndiff(out, ref, nh) == 0 && ndiff(out + nh, out2 + nh, NW - nh) == 0,
        "raw changed after the TOP: TOP developed, BOT the 8-bit look");
    chk(LOGW("rawb: 8-bit (raw changed since the TOP half)", 3000), "logged: '%s'", log_line("rawb: 8-bit (raw"));
    r5_raw((uint8_t *)(uintptr_t)R5_RAW, in);

    /* no live job / MEM1 past the pool end / gates: nothing read, the 8-bit look */
    static const struct { uint32_t magic, m1; int picq, drive; const char *want, *what; } G[] = {
        { 0,           R5_M1,       0x3070000, 0, "rawb: 8-bit (noaddr ",          "no live job (H[0] != magic)" },
        { 0xFFCD0761u, 0x5B000000u, 0x3070000, 0, "rawb: 8-bit (noaddr 5b000000)", "MEM1 raw past 0x5C000000" },
        { 0xFFCD0761u, R5_M1,       0x3070001, 0, "rawb: 8-bit (quality 3070001)", "MRAW + L" },
        { 0xFFCD0761u, R5_M1,       0x3010000, 0, "rawb: 8-bit (quality 3010000)", "JPEG only" },
        { 0xFFCD0761u, R5_M1,       0x3070000, 1, "rawb: 8-bit (drive 1)",         "burst" },
    };
    for (int i = 0; i < (int)(sizeof G / sizeof G[0]); i++) {
        p5_job(G[i].magic, G[i].m1, P5_SH);
        pic_quality = G[i].picq;
        drive_mode = G[i].drive;
        log_mark();
        sb = shot(in, out, FW, FH);
        usleep(400000);
        chk(sb == 0 && ndiff(out, out2, NW) == 0 && LOGW(G[i].want, 3000) && !log_has("rawb: buf"),
            "%s: the 8-bit look, '%s'", G[i].what, log_line("rawb: 8-bit"));
    }
    p5_job(0xFFCD0761u, R5_M1, P5_SH);
    pic_quality = 0x3070000;
    drive_mode = 0;

    /* half-shutter held during the TOP: it stops, the BOT gets the 8-bit look */
    h_half = 1;
    h_half_step_ms = 1000;
    log_mark();
    sb = shot(in, out, FW, FH);
    h_half = 0;
    h_half_step_ms = 0;
    usleep(500000);
    chk(LOGW("rawb: STOPPED", 3000) && strstr(log_line("rawb: STOPPED"), "(half-shutter)") &&
        ndiff(out + nh, in + nh, NW - nh) == 0,
        "half-shutter: TOP stopped ('%s'), BOT stays Canon's (no look under a stopped TOP)", log_line("rawb: STOPPED"));

    /* again a full developed shot (the stop leaves nothing behind) */
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "after the stop: developed again, bit-exact");

    /* Leica Vivid (WF_FLUT_DISP): both halves developed through the display cube on the sRGB codes */
    int out0 = wf_out, fl0 = wf_flut;
    {
        char nb[WF_LL_NAME];
        cube_write(wf_loglut_name(nb, WF_CURVE_VLOG, WF_FLUT_LEICAV, 0), 5);
        dcube_write("ML/DATA/LEICAVSR.CUB", 33);
    }
    log_mark();
    wf_out = WF_OUT_LLUT;
    wf_flut = WF_FLUT_LEICAV;
    chk(LOGW("rawb: tables disp c0 LEICAVSR.CUB", 30000), "Leica Vivid: '%s'", log_line("rawb: tables"));
    r5_disp = 1;
    r5_ref(ref, in);
    r5_disp = 0;
    log_mark();
    sb = shot(in, out, FW, FH);
    usleep(500000);
    chk(sb == 0 && LOGW("BOT half continues", 3000) && ndiff(out, ref, NW) == 0,
        "Leica Vivid shot: TOP + BOT bit-exact vs the sRGB develop + display cube (%d words differ)", ndiff(out, ref, NW));
    wf_out = out0;
    wf_flut = fl0;

    /* off: the memory goes back */
    wf_rawb = 0;
    chk(WAIT(h_arena_live == live0, 3000), "off: shoot memory given back");
    mod_deinit();
    free(alt); free(dev);
}
#endif
/* ------------------------------------------------------------------ HS: Highlights / Shadows */
/* the formula as specified, in 64 bits: round((h x^2 (255 - x) + s x (255 - x)^2) / 385341), half away from zero */
static int hs_d(int x, int h, int s)
{
    long long d = (long long)h * x * x * (255 - x) + (long long)s * x * (255 - x) * (255 - x);
    return (int)(d < 0 ? -((192670 - d) / 385341) : (d + 192670) / 385341);
}
/* ... applied to a curve with the non-decreasing clamp p = clamp(x + d, p, 255); 0 / 0 leaves it */
static void hs_ref(uint8_t *N, int h, int s)
{
    if (!h && !s) return;
    for (int y = 0, p = 0; y < 256; y++) {
        p = clampi(N[y] + hs_d(N[y], h, s), p, 255);
        N[y] = (uint8_t)p;
    }
}
/* FNV of film sets built by the builder of 7f9e294 (before Highlights / Shadows), memset 0 first, bytes up to src */
static const struct { int dy, du, dv, mono, flags; uint32_t fnv; } hs_gold[] = {
    { 12, 6, -6, 0, 0x0000, 0xedfa640fu },
    { 12, 6, -6, 0, 0x0001, 0x5f01b244u },
    { 12, 6, -6, 0, 0x1e01, 0x5130d2a0u },
    { 12, 6, -6, 0, 0x7d01, 0x9857eddau },
    { -10, 0, 0, 0, 0x0001, 0xc69f90e8u },
    { 24, 0, 0, 0, 0x7d01, 0x26f24324u },
    { 0, 0, 0, 1, 0x0003, 0x332e189cu },
    { 0, 0, 0, 1, 0x7d03, 0x332e189cu },
};
/* the display filter on one LiveView frame, against the film kernel built with `flags` */
static int hs_pv_diff(int k, int flags)
{
    enum { LW = 360, LH = 480 };
    uint32_t *src = malloc(LW * LH * 4), *dst = malloc(LW * LH * 4), *pr = malloc(LW * LH * 4);
    gen_frame(src, 720, LH, 0);
    uint32_t *bufs[2] = { src, dst };
    int d = -1;
    if (cbr_disp((unsigned)(uintptr_t)bufs) == 1) {
        memcpy(pr, src, LW * LH * 4);
        ref_film(pr, LW * LH, tabs[k][0], flags);
        d = ndiff(dst, pr, LW * LH);
    }
    free(src); free(dst); free(pr);
    return d;
}
static void t_hs(void)
{
    /* 1. the bend: all 121 pairs against the formula, on the identity and on a film's N; never down; the packing */
    static struct wf_ftabs t1, t2;
    uint8_t id[256], a[256], b[256];
    for (int y = 0; y < 256; y++) id[y] = (uint8_t)y;
    uint32_t *TA = tab_make(12, 6, -6, 0);
    wf_film_build(&t1, TA, WF_FT_V21 | WF_FT_S(125));
    int bad = 0, raw = 0, dec = 0, ends = 0, pk = 0, mx = 0;
    for (int h = WF_HS_MIN; h <= WF_HS_MAX; h++)
        for (int s = WF_HS_MIN; s <= WF_HS_MAX; s++) {
            for (int c = 0; c < 2; c++) {
                const uint8_t *src = c ? t1.N : id;
                memcpy(a, src, 256);
                memcpy(b, src, 256);
                wf_hs_bend(a, h, s);
                hs_ref(b, h, s);
                bad += memcmp(a, b, 256) != 0;
                for (int y = 1; y < 256; y++) dec += a[y] < a[y - 1];
            }
            memcpy(a, id, 256);
            wf_hs_bend(a, h, s);
            for (int y = 0; y < 256; y++) {
                raw += a[y] != y + hs_d(y, h, s);
                if (abs(a[y] - y) > mx) mx = abs(a[y] - y);
            }
            ends += a[0] != 0 || a[255] != 255;
            int f = WF_FT_HS(h, s);
            pk += WF_FT_HOF(f) != h || WF_FT_SHOF(f) != s || (f & ~0x00ff0000) || WF_FT_HOF(f | 0x7d03) != h
                  || WF_FT_SOF(f | WF_FT_S(30)) != 30;
        }
    chk(bad == 0, "wf_hs_bend = the formula + clamp for all 121 pairs, on the identity and on a film's N (%d curves differ)",
        bad);
    chk(raw == 0 && ends == 0, "identity: the plain formula, the clamp never needed (%d codes differ), 0 and 255 kept (%d)",
        raw, ends);
    chk(dec == 0, "every bent curve non-decreasing (%d steps down)", dec);
    chk(pk == 0 && WF_FT_HS(0, 0) == 0, "WF_FT_HS: -5..5 in bits 16-23, 0 / 0 adds no bit, strength untouched (%d bad)", pk);
    memcpy(a, id, 256); wf_hs_bend(a, 5, 0);
    memcpy(b, id, 256); wf_hs_bend(b, 0, 5);
    int a2 = a[170], b2 = b[85];
    memcpy(a, id, 256); wf_hs_bend(a, -5, 0);
    memcpy(b, id, 256); wf_hs_bend(b, 0, -5);
    chk(a2 == 202 && b2 == 117 && a[170] == 138 && b[85] == 53,
        "+5 Highlights lifts 170 by 32 (%d), +5 Shadows 85 by 32 (%d), -5: %d / %d; largest shift (+5 / +5) %d", a2, b2,
        a[170], b[85], mx);
    free(TA);

    /* 2. film sets: 0 / 0 is the builder before the feature bit for bit; h / s change N alone, by wf_hs_bend */
    int ng = (int)(sizeof hs_gold / sizeof *hs_gold), gb = 0, nb = 0;
    static const int8_t pr[5][2] = { { 5, 0 }, { 0, -5 }, { 3, -2 }, { -5, 5 }, { 1, 1 } };
    size_t no = offsetof(struct wf_ftabs, N), ne = no + 256, end = offsetof(struct wf_ftabs, src);
    for (int i = 0; i < ng; i++) {
        uint32_t *T = tab_make(hs_gold[i].dy, hs_gold[i].du, hs_gold[i].dv, hs_gold[i].mono);
        memset(&t1, 0, sizeof t1);
        wf_film_build(&t1, T, hs_gold[i].flags | WF_FT_HS(0, 0));
        uint32_t hh = fnv(2166136261u, (const uint8_t *)&t1, end);
        if (hh != hs_gold[i].fnv) {
            gb++;
            printf("  set %d: fnv %08x, before the feature %08x\n", i, (unsigned)hh, (unsigned)hs_gold[i].fnv);
        }
        for (int j = 0; j < 5; j++) {
            memset(&t2, 0, sizeof t2);
            wf_film_build(&t2, T, hs_gold[i].flags | WF_FT_HS(pr[j][0], pr[j][1]));
            memcpy(a, t1.N, 256);
            wf_hs_bend(a, pr[j][0], pr[j][1]);
            const uint8_t *p1 = (const uint8_t *)&t1, *p2 = (const uint8_t *)&t2;
            nb += memcmp(a, t2.N, 256) || memcmp(p1, p2, no) || memcmp(p1 + ne, p2 + ne, end - ne) || t1.src != t2.src;
        }
        free(T);
    }
    chk(gb == 0, "0 / 0: %d film sets bit-identical to the builder before Highlights / Shadows (%d differ)", ng, gb);
    chk(nb == 0, "h / s: only N changes, and it is wf_hs_bend of the 0 / 0 N (%d of %d sets wrong)", nb, ng * 5);

    /* 3. through the module: rows, a film shot (+ log, EXIF), the preview */
    std_pack();
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\npreview=on\n", wf_films[fA].id);
    mod_init(1);
    chk(!strcmp(wf_row_name(WF_ROW_HL), "Highlights") && !strcmp(wf_row_name(WF_ROW_SH), "Shadows")
        && !strcmp(wf_row_text(WF_ROW_HL), "0") && !strcmp(wf_row_text(WF_ROW_SH), "0") && wf_hl == 0 && wf_sh == 0,
        "rows Highlights / Shadows, 0 / 0 without the keys in the file");
    chk(wf_row_avail(WF_ROW_HL) && wf_row_avail(WF_ROW_SH), "the rows are live with a film");
    int st = 0;
    for (int i = 0; i < 7; i++) st += wf_row_step(WF_ROW_HL, 1);
    chk(st == 5 && wf_hl == 5 && !strcmp(wf_row_text(WF_ROW_HL), "+5"), "Highlights up to +5, then no step (%d steps)", st);
    st = 0;
    for (int i = 0; i < 9; i++) st += wf_row_step(WF_ROW_SH, -1);
    chk(st == 5 && wf_sh == -5 && !strcmp(wf_row_text(WF_ROW_SH), "-5"), "Shadows down to -5, then no step (%d steps)", st);
    wf_row_step(WF_ROW_HL, -1); wf_row_step(WF_ROW_HL, -1);
    wf_row_step(WF_ROW_SH, 1); wf_row_step(WF_ROW_SH, 1); wf_row_step(WF_ROW_SH, 1);
    chk(wf_hl == 3 && wf_sh == -2 && !strcmp(wf_row_text(WF_ROW_SH), "-2"), "Highlights +3, Shadows -2");
    struct wf_look lk;
    wf_look_now(&lk);
    chk(lk.hl == 3 && lk.sh == -2 && lk.ftflags == (flags_of(fA, 1) | WF_FT_HS(3, -2)), "the look: hl %d sh %d, flags %x",
        lk.hl, lk.sh, (unsigned)lk.ftflags);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(look_table_ready(), 10000), "table published");
    char p[64];
    log_mark();
    h_li_set("job", 1);                          /* Canon busy until its JPEG is on the card (EXIF) */
    int sb = shot(in, out, FW, FH);
    jpg_name(p, 9); jpeg_write(p, 0, 264, 0);
    h_set_file_number(9);
    h_li_set("job", 0);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1) | WF_FT_HS(3, -2));
    memcpy(out2, in, NW * 4);
    ref_film(out2, NW, tabs[fA][0], flags_of(fA, 1));
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "film shot +3 / -2: bit-exact vs wf_film_build with WF_FT_HS(3, -2) (%d differ)",
        ndiff(out, ref, NW));
    chk(ndiff(out, out2, NW) > NW / 4, "... and not the 0 / 0 picture (%d of %d words differ)", ndiff(out, out2, NW), NW);
    chk(LOGW(" hl+3 sh-2:", 5000) && strstr(log_line("shot #"), " hl+3 sh-2:"), "shot line: '%s'", log_line("shot #"));
    chk(strstr(log_line("yuv "), " hl+3 sh-2") != 0, "yuv line: '%s'", log_line("yuv "));
    chk(LOGW("exif IMG_0009.JPG ok", 8000) && strstr(comment(p), " | Highlights +3 | Shadows -2"), "EXIF: '%s'", comment(p));
    chk(WAIT(wf_pv_state() == WF_PV_READY, 10000), "preview ready (state %d)", wf_pv_state());
    int d = hs_pv_diff(fA, flags_of(fA, 1) | WF_FT_HS(3, -2));
    chk(d == 0, "preview +3 / -2: bit-exact vs the shot's kernel (%d words differ)", d);
    wf_row_step(WF_ROW_HL, -1);
    chk(WAIT(wf_pv_state() == WF_PV_READY, 10000), "preview rebuilt after Highlights +2 (state %d)", wf_pv_state());
    d = hs_pv_diff(fA, flags_of(fA, 1) | WF_FT_HS(2, -2));
    chk(d == 0, "preview +2 / -2: the new set (%d words differ)", d);
    wf_row_step(WF_ROW_HL, -1); wf_row_step(WF_ROW_HL, -1);
    wf_row_step(WF_ROW_SH, 1); wf_row_step(WF_ROW_SH, 1);
    chk(wf_hl == 0 && wf_sh == 0, "back to 0 / 0");
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(ndiff(out, out2, NW) == 0 && sb == 0, "film shot 0 / 0: the picture without the feature (%d differ)",
        ndiff(out, out2, NW));
    chk(LOGW("shot #", 5000) && !strstr(log_line("shot #"), " hl") && !strstr(log_line("yuv "), " hl"),
        "0 / 0: nothing in the log lines: '%s'", log_line("shot #"));

    /* 4. Log + LUT: N bent after the load (the .FRT holds it unbent); only h / s changed: bent again, no card read */
    static struct wf_lltabs ll, l2, lb;
    struct wf_ll_info inf;
    wf_row_step(WF_ROW_HL, 1); wf_row_step(WF_ROW_HL, 1);
    wf_row_step(WF_ROW_SH, -1); wf_row_step(WF_ROW_SH, -1);
    cube_write("ML/DATA/VLOG.CUB", 9);
    wf_curve = WF_CURVE_VLOG;
    wf_flut = 0;
    log_mark();
    wf_out = WF_OUT_LLUT;
    chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
        && strstr(log_line("lut VLOG.CUB"), " hl+2 sh-2"), "Log + LUT at +2 / -2: '%s'", log_line("lut VLOG.CUB"));
    chk(wf_row_avail(WF_ROW_HL) && wf_row_avail(WF_ROW_SH), "the rows are live with Log + LUT");
    int rc = wf_loglut_load("ML/DATA/VLOG.CUB", WF_CURVE_VLOG, &ll, &inf);
    memcpy(&lb, &ll, sizeof ll);
    wf_hs_bend(lb.N, 2, -2);
    chk(rc == WF_LL_OK && ll_same(&lb, wf_ll_tables()) && !ll_same(&ll, wf_ll_tables()),
        "published tables = wf_loglut_load with N bent +2 / -2, nothing else (rc %d)", rc);
    rc = wf_loglut_load_cached("ML/DATA/VLOG.CUB", "ML/DATA/VLOG.FRT", WF_CURVE_VLOG, &l2, &inf);
    chk(rc == WF_LL_OK && inf.cached && ll_same(&ll, &l2), "ML/DATA/VLOG.FRT holds the tables unbent (rc %d, cache %d)", rc,
        inf.cached);
    uint32_t g[128];
    for (int i = 0; i < 128; i++) g[i] = word(0, 2 * i, 0, 2 * i + 1);
    wf_ll_words_c(g, 128, (int)(uintptr_t)wf_ll_tables());
    int ge = 0;
    for (int i = 0; i < 128; i++) ge += WY0(g[i]) != lb.N[2 * i] || WY1(g[i]) != lb.N[2 * i + 1];
    chk(ge == 0, "grey comes out at the bent N, every code (%d off)", ge);
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&lb);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "Log + LUT shot +2 / -2: bit-exact vs wf_ll_words on the bent tables");
    chk(LOGW(" hl+2 sh-2", 5000) && strstr(log_line("yuv "), " hl+2 sh-2"), "yuv line: '%s'", log_line("yuv "));
    log_mark();
    wf_row_step(WF_ROW_HL, 1); wf_row_step(WF_ROW_HL, 1);
    chk(LOGW("lut: highlights +4 shadows -2", 10000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
        && !log_has("lut VLOG.CUB"), "Highlights +4: N bent again, no load: '%s'", log_line("lut"));
    memcpy(&lb, &ll, sizeof ll);
    wf_hs_bend(lb.N, 4, -2);
    chk(ll_same(&lb, wf_ll_tables()), "tables = the loaded ones with N bent +4 / -2");
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&lb);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "Log + LUT shot +4 / -2: bit-exact");
    log_mark();
    wf_lut_ei = 3;                               /* another load key: loaded again (cache), then bent */
    chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
        && strstr(log_line("lut VLOG.CUB"), " ei+3/3 hl+4 sh-2"), "LUT EI +1 EV: loaded again and bent: '%s'",
        log_line("lut VLOG.CUB"));
    wf_ll_ei = 3;
    rc = wf_loglut_load("ML/DATA/VLOG.CUB", WF_CURVE_VLOG, &l2, &inf);
    wf_ll_ei = 0;
    memcpy(&lb, &l2, sizeof l2);
    wf_hs_bend(lb.N, 4, -2);
    chk(rc == WF_LL_OK && ll_same(&lb, wf_ll_tables()), "tables = the EI +1 load with N bent +4 / -2 (rc %d)", rc);
    log_mark();
    for (int i = 0; i < 4; i++) wf_row_step(WF_ROW_HL, -1);
    wf_row_step(WF_ROW_SH, 1); wf_row_step(WF_ROW_SH, 1);
    chk(LOGW("lut: highlights 0 shadows 0", 10000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
        && ll_same(&l2, wf_ll_tables()), "0 / 0: the loaded tables as they are: '%s'", log_line("lut"));
    wf_lut_ei = 0;

    /* 5. flat Log and Off: no Highlights / Shadows */
    wf_row_step(WF_ROW_HL, 1);
    wf_out = WF_OUT_LOG;
    chk(!wf_row_avail(WF_ROW_HL) && !wf_row_avail(WF_ROW_SH), "rows greyed for flat Log");
    wf_look_now(&lk);
    chk(lk.hl == 0 && lk.sh == 0, "flat Log: the look carries no Highlights / Shadows");
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_log_words, WF_CURVE_VLOG);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "flat Log with Highlights +1: plain wf_log_words");
    wf_out = WF_OUT_OFF;
    chk(!wf_row_avail(WF_ROW_HL) && !wf_row_avail(WF_ROW_SH), "rows greyed for Off");

    /* 6. the file: hl / sh saved, read back, out of range ignored */
    h_clock_add_ms += 2000;
    usleep(400000);
    long n;
    char *f = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(f && strstr(f, "\nhl=1\nsh=0\n"), "saved: hl=1, sh=0");
    free(f);
    put_cfg("hl=-4\nsh=5\n");
    wf_cfg_load();
    chk(wf_hl == -4 && wf_sh == 5, "read back: hl %d sh %d", wf_hl, wf_sh);
    put_cfg("hl=6\nsh=-9\nhl=x\n");
    wf_cfg_load();
    chk(wf_hl == -4 && wf_sh == 5, "out of range / not a number ignored: hl %d sh %d", wf_hl, wf_sh);
    wf_hl = wf_sh = 0;
    mod_deinit();
}

/* ------------------------------------------------------------------ LEICA: the display-look can and the Windy names */
static int ps_changes(int from, int val)
{
    int n = 0;
    for (int i = from; i < h_nchanges && i < 256; i++)
        n += h_change_prop[i] == 0x80000028u && (val < 0 || h_change_val[i] == val);
    return n;
}
static void t_leica(void)
{
    int kL = wf_cat_index(84), kW = wf_cat_index(71);
    chk(kL >= 0 && kW >= 0 && !strcmp(wf_films[kL].name, "Leica Vivid") && (wf_films[kL].flags & WF_F_LUTCAN)
        && wf_cat_lut[kL] == WF_FLUT_LEICAV && !strcmp(wf_films[kW].name, "Windy Neutral"),
        "catalogue: Leica Vivid can (Film LUT %d), Windy Neutral", kL >= 0 ? wf_cat_lut[kL] : -1);
    int nt = 0;
    for (int k = 0; k < wf_nfilms; k++)
        nt += !strncmp(wf_films[k].name, "Tung", 4);
    for (int l = 0; l < WF_FLUT_N; l++)
        nt += !strncmp(wf_flut_name[l], "Tung", 4);
    chk(nt == 0 && !strcmp(wf_flut_name[9], "Windy Neutral") && !strcmp(wf_flut_name[12], "Windy 250D P")
        && !strcmp(wf_flut_name[22], "Windy EXRa M") && !strcmp(wf_flut_name[WF_FLUT_LEICAV], "Leica Vivid"),
        "no Tung name left (%d), Windy names, Leica Vivid last", nt);
    char nb[WF_LL_NAME];
    chk(!strcmp(wf_loglut_name(nb, WF_CURVE_VLOG, WF_FLUT_LEICAV, 0), "ML/DATA/LEICAVSR.CUB")
        && !strcmp(wf_loglut_name(nb, 1, WF_FLUT_LEICAV, 0), "ML/DATA/LEICAVSR.CUB")
        && !strcmp(wf_loglut_name(nb, WF_CURVE_VLOG, WF_FLUT_LEICAV, 1), "ML/DATA/LEICAVSR.FRT")
        && !strcmp(wf_loglut_name(nb, WF_CURVE_VLOG, 9, 0), "ML/DATA/TUNGNTVL.CUB"),
        "cube names: LEICAVSR.CUB / .FRT for every Log curve, the Windy cubes keep their file names");
    chk(WF_FLUT_DISP(WF_FLUT_LEICAV) && !WF_FLUT_DISP(9) && !WF_FLUT_DISP(0) && !WF_FLUT_VLONLY(WF_FLUT_LEICAV)
        && WF_FLUT_VLONLY(9),
        "Leica Vivid: a display look on sRGB codes, not a V-Log cube");

    /* hidden without its cube (the module scans the cubes at start and when the browser opens) */
    std_pack();
    put_cfg("output=off\nincurve=film1\n");
    cube_write("ML/DATA/TUNGNTVL.CUB", 9);
    cube_write("ML/DATA/LEICAVVL.CUB", 9);      /* the old V-Log file name: not Leica Vivid's cube any more */
    h_li_set("picstyle", 0x84);                  /* Neutral */
    mod_init(1);
    chk(WAIT(wf_avail(71), 5000) && !wf_avail(84) && wf_load(84) == WF_LOAD_NOCUBE,
        "TUNGNTVL.CUB and the old LEICAVVL.CUB on the card: Windy Neutral shown, Leica Vivid hidden and does not load");
    cube_write("ML/DATA/LEICAVSR.CUB", 9);

    /* load: Picture Style Standard asked for, sRGB in whatever the Input curve, no auto exposure */
    int inc = wf_ll_curve_have(WF_INC_FILM1) ? WF_INC_FILM1 : WF_INC_SRGB;
    wf_incurve = inc;
    h_li_set("picstyle", 0);                     /* not reported: Film 1 counts as usable */
    chk(wf_incurve_t() == inc, "before: the Input curve in use is %d", inc);
    int n0 = h_nchanges;
    log_mark();
    chk(wf_load(84) == WF_LOAD_OK && wf_out == WF_OUT_LLUT && wf_flut == WF_FLUT_LEICAV && wf_curve == WF_CURVE_VLOG,
        "load Leica Vivid: Log + LUT, V-Log, its cube");
    chk(ps_changes(n0, 0x81) == 1 && LOGW("style: Standard for Leica Vivid", 3000),
        "loading it asks for Picture Style Standard (0x81): %d request(s), log '%s'", ps_changes(n0, -1),
        log_line("style:"));
    chk(wf_incurve == inc && wf_incurve_t() == WF_INC_SRGB, "Input curve %d chosen, Leica Vivid reads sRGB", inc);
    chk(LOGW("lut LEICAVSR.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && wf_ll_auto == 0
        && wf_ll_disp == 1 && wf_ll_incurve == WF_INC_SRGB && !strstr(log_line("lut LEICAVSR.CUB"), " in-"),
        "loaded on the sRGB codes, no auto exposure: '%s'", log_line("lut LEICAVSR.CUB"));

    /* the colour path: the identity cube on the sRGB codes gives the JPEG back (no log on the way) */
    static struct wf_lltabs la, lb, lc, ld, le;
    struct wf_ll_info inf;
    wf_ll_incurve = WF_INC_SRGB; wf_ll_ei = 0; wf_ll_auto = 0; wf_ll_disp = 1;
    int rc = wf_loglut_load("ML/DATA/LEICAVSR.CUB", WF_CURVE_VLOG, &la, &inf);
    wf_ll_auto = 1;                              /* ignored for a display look */
    int rc3 = wf_loglut_load("ML/DATA/LEICAVSR.CUB", 1, &lc, &inf);
    wf_ll_auto = 0;
    wf_ll_ei = 3;                                /* +1 EV by hand */
    int rc4 = wf_loglut_load("ML/DATA/LEICAVSR.CUB", WF_CURVE_VLOG, &ld, &inf);
    wf_ll_ei = 0; wf_ll_disp = 0;
    int rc2 = wf_loglut_load("ML/DATA/LEICAVSR.CUB", WF_CURVE_VLOG, &lb, &inf);   /* the old path: sRGB -> V-Log -> cube */
    int dn = 0, dv = 0;
    for (int y = 0; y < 256; y++) {
        int d = abs(la.N[y] - y), e = abs(lb.N[y] - y);
        dn = d > dn ? d : dn;
        dv = e > dv ? e : dv;
    }
    chk(rc == WF_LL_OK && rc2 == WF_LL_OK && rc3 == WF_LL_OK && rc4 == WF_LL_OK && ll_same(&la, wf_ll_tables())
        && ll_same(&la, &lc) && !ll_same(&lb, wf_ll_tables()),
        "published tables = the cube on the sRGB codes (S-Log3 + auto the same), not the V-Log path");
    chk(dn <= 1 && dv > 20, "identity cube: neutral Y' out = in (max %d codes off); through V-Log it moved %d", dn, dv);
    chk(ld.N[100] >= 135 && ld.N[100] <= 141 && ld.N[0] <= 1 && ld.N[255] == 255 && ld.N[200] == 255,
        "LUT EI +1 EV: light x2 then sRGB again: 100 -> %d (138), 0 -> %d, 200 -> %d", ld.N[100], ld.N[0], ld.N[200]);
    wf_ll_disp = 1;
    log_mark();
    chk(wf_loglut_load_cached("ML/DATA/LEICAVSR.CUB", "ML/DATA/LEICAVSR.FRT", WF_CURVE_VLOG, &le, &inf) == WF_LL_OK
        && inf.cached && ll_same(&la, &le), "the .FRT the module wrote is read back for the display look");
    wf_ll_disp = 0;
    chk(wf_loglut_load_cached("ML/DATA/LEICAVSR.CUB", "ML/DATA/LEICAVSR.FRT", WF_CURVE_VLOG, &le, &inf) == WF_LL_OK
        && !inf.cached && ll_same(&lb, &le), "the same .FRT is not taken for the V-Log path (key bit 19)");
    wf_ll_disp = 1;
    chk(wf_loglut_load_cached("ML/DATA/LEICAVSR.CUB", "ML/DATA/LEICAVSR.FRT", WF_CURVE_VLOG, &le, &inf) == WF_LL_OK
        && !inf.cached && ll_same(&la, &le), "and back: rebuilt for the display look");
    wf_ll_disp = 0;
    struct wf_look lk;
    wf_look_now(&lk);
    chk(lk.output == WF_OUT_LLUT && lk.flut == WF_FLUT_LEICAV && lk.incurve == WF_INC_SRGB, "the look: Leica Vivid, sRGB");
    chk(!strcmp(wf_row_text(WF_ROW_FLUT), "Leica Vivid"), "Film LUT row: '%s'", wf_row_text(WF_ROW_FLUT));

    /* Picture Style already Standard: nothing asked */
    h_li_set("picstyle", 0x81);
    n0 = h_nchanges;
    chk(wf_load(84) == WF_LOAD_OK && ps_changes(n0, -1) == 0, "style already Standard: no request");

    /* another can: the Input curve and auto exposure are back, no style request */
    h_li_set("picstyle", 0);
    n0 = h_nchanges;
    log_mark();
    chk(wf_load(71) == WF_LOAD_OK && wf_flut == 9 && wf_incurve_t() == inc && ps_changes(n0, -1) == 0,
        "Windy Neutral: Input curve %d again, no style request", inc);
    chk(LOGW("lut TUNGNTVL.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && wf_ll_auto == 1
        && wf_ll_disp == 0, "Windy Neutral loaded with auto exposure, through V-Log: '%s'", log_line("lut TUNGNTVL.CUB"));
    chk(!strcmp(wf_row_text(WF_ROW_FLUT), "Windy Neutral"), "Film LUT row: '%s'", wf_row_text(WF_ROW_FLUT));

    /* the Film LUT row reaches it (last cube before Off) and asks for the style too */
    n0 = h_nchanges;
    int steps = 0;
    while (wf_flut != WF_FLUT_LEICAV && steps++ < WF_FLUT_N + 2)
        wf_row_step(WF_ROW_FLUT, 1);
    chk(wf_flut == WF_FLUT_LEICAV && ps_changes(n0, 0x81) == 1, "Film LUT row: Leica Vivid after %d steps, style asked", steps);
    wf_row_step(WF_ROW_FLUT, 1);
    chk(wf_out != WF_OUT_LLUT, "one more step: Off");

    /* config: flut=leicav */
    put_cfg("output=lut\ncurve=vlog\nflut=leicav\n");
    wf_cfg_load();
    chk(wf_flut == WF_FLUT_LEICAV && wf_out == WF_OUT_LLUT, "flut=leicav read (Film LUT %d)", wf_flut);
    wf_incurve = WF_INC_SRGB;
    wf_out = WF_OUT_OFF;
    mod_deinit();
}

/* the CinePrint looks on Canon Log 2 (owner 11-14 "thay het qua clog2 thoi ban may cai cineprint ay"): Windy EKTA P (11)
 * and 12..22 load Canon Log 2 and their <code>C2.CUB; their V-Log cubes of before do not make a can show; the Film LUT row
 * brings each cube on its own curve, and a Log curve set by hand that is not the cube's own stays and finds that curve's
 * cube (mk_card.sh CINEPRINT_C2 writes the C2 names) */
static void t_clog2(void)
{
    char nb[WF_LL_NAME];
    int ok = 1;
    for (int l = -1; l <= WF_FLUT_N; l++)
        ok &= WF_FLUT_CURVE(l) == (l >= WF_FLUT_EKTAP && l < WF_FLUT_LEICAV ? WF_LL_CLOG2 : WF_CURVE_VLOG);
    chk(ok, "cube curves: Canon Log 2 for Film LUT 11..22, V-Log for the others and out of range");
    chk(!strcmp(wf_loglut_name(nb, WF_LL_CLOG2, 12, 0), "ML/DATA/T250DPC2.CUB")
        && !strcmp(wf_loglut_name(nb, WF_LL_CLOG2, 12, 1), "ML/DATA/T250DPC2.FRT")
        && !strcmp(wf_loglut_name(nb, WF_LL_CLOG2, WF_FLUT_EKTAP, 0), "ML/DATA/EKTAP_C2.CUB")
        && !strcmp(wf_loglut_name(nb, WF_LL_CLOG2, 19, 0), "ML/DATA/TEXRP_C2.CUB")
        && !strcmp(wf_loglut_name(nb, WF_LL_CLOG2, 22, 0), "ML/DATA/TEXRAMC2.CUB")
        && !strcmp(wf_loglut_name(nb, WF_CURVE_VLOG, 12, 0), "ML/DATA/T250DPVL.CUB"),
        "C2 cube names T250DPC2 / EKTAP_C2 / TEXRP_C2 / TEXRAMC2, the V-Log names kept");
    int k11 = -1, k12 = -1, k13 = -1, k9 = -1;
    for (int k = 0; k < wf_nfilms; k++)
        if (wf_films[k].flags & WF_F_LUTCAN) {
            if (wf_cat_lut[k] == WF_FLUT_EKTAP) k11 = k;
            if (wf_cat_lut[k] == 12) k12 = k;
            if (wf_cat_lut[k] == 13) k13 = k;
            if (wf_cat_lut[k] == 9) k9 = k;
        }
    if (k11 < 0 || k12 < 0 || k13 < 0 || k9 < 0) {
        chk(0, "catalogue: cans of Film LUT 9 / 11 / 12 / 13 (%d %d %d %d)", k9, k11, k12, k13);
        return;
    }
    const char *f11 = wf_films[k11].format, *f12 = wf_films[k12].format, *f9 = wf_films[k9].format;
    chk(f11 && f12 && f9 && !strcmp(f11, "C-Log2") && !strcmp(f12, "C-Log2") && !strcmp(f9, "V-Log"),
        "catalogue chip: Windy EKTA P / 250D P 'C-Log2', Windy Neutral 'V-Log' (%s %s %s)", f11 ? f11 : "-",
        f12 ? f12 : "-", f9 ? f9 : "-");
    int id11 = wf_films[k11].id, id12 = wf_films[k12].id, id9 = wf_films[k9].id;

    /* only the V-Log cubes of before on the card: the CinePrint cans are hidden and do not load */
    std_pack();
    put_cfg("output=off\n");
    cube_write("ML/DATA/TUNGNTVL.CUB", 9);
    cube_write("ML/DATA/T250DPVL.CUB", 9);
    cube_write("ML/DATA/T250DMVL.CUB", 9);
    cube_write("ML/DATA/EKTAP_VL.CUB", 9);
    mod_init(1);
    chk(WAIT(wf_avail(id9), 5000) && !wf_avail(id12) && !wf_avail(id11) && wf_load(id12) == WF_LOAD_NOCUBE
        && wf_load(id11) == WF_LOAD_NOCUBE && wf_out != WF_OUT_LLUT,
        "V-Log cubes only (T250DPVL, EKTAP_VL): Windy 250D P and EKTA P hidden, no load; Windy Neutral shown");

    /* its C2 cube: Load sets Canon Log 2 + LUT */
    cube_write("ML/DATA/T250DPC2.CUB", 9);
    cube_write("ML/DATA/TEKTAMC2.CUB", 9);       /* Windy EKTA M (16): the next C2 cube on the row */
    log_mark();
    chk(wf_load(id12) == WF_LOAD_OK && wf_loaded() == id12 && wf_out == WF_OUT_LLUT && wf_flut == 12
        && wf_curve == WF_LL_CLOG2, "load Windy 250D P: Canon Log 2 + LUT, Film LUT 12");
    chk(LOGW("lut T250DPC2.CUB", 60000) && WAIT(wf_ll_state(WF_LL_CLOG2) == WF_LL_OK, 5000)
        && wf_ll_have_curve() == WF_LL_CLOG2 && wf_ll_auto == 1 && wf_ll_disp == 0,
        "its C2 cube loaded, auto exposure on: '%s'", log_line("lut T250DPC2.CUB"));
    chk(!strcmp(wf_row_text(WF_ROW_FLUT), "Windy 250D P"), "Film LUT row: '%s'", wf_row_text(WF_ROW_FLUT));

    /* the Film LUT row: each cube on its own curve */
    wf_row_step(WF_ROW_FLUT, 1);
    chk(wf_flut == 16 && wf_curve == WF_LL_CLOG2 && wf_out == WF_OUT_LLUT,
        "row +1: 250D M (only its V-Log cube) skipped, Windy EKTA M on Canon Log 2 (Film LUT %d, curve %d)", wf_flut, wf_curve);
    wf_row_step(WF_ROW_FLUT, -1);
    wf_row_step(WF_ROW_FLUT, -1);
    chk(wf_flut == 9 && wf_curve == WF_CURVE_VLOG && wf_out == WF_OUT_LLUT,
        "row -1 twice: back over 250D P to Windy Neutral, on its V-Log (Film LUT %d, curve %d)", wf_flut, wf_curve);
    wf_row_step(WF_ROW_FLUT, 1);
    chk(wf_flut == 12 && wf_curve == WF_LL_CLOG2 && wf_loaded() == id12,
        "row +1: Windy 250D P on Canon Log 2 again, the can counts as loaded");

    /* V-Log set by hand on a CinePrint look: kept, its V-Log cube used, the row offers the cubes V-Log has */
    log_mark();
    wf_curve = WF_CURVE_VLOG;
    chk(wf_loaded() != id12 && LOGW("lut T250DPVL.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000),
        "V-Log by hand: T250DPVL.CUB, the can no longer the loaded look: '%s'", log_line("lut T250DPVL.CUB"));
    wf_row_step(WF_ROW_FLUT, 1);
    chk(wf_flut == 13 && wf_curve == WF_CURVE_VLOG, "row +1 on V-Log by hand: Windy 250D M through T250DMVL.CUB (%d, %d)",
        wf_flut, wf_curve);
    wf_row_step(WF_ROW_FLUT, 1);
    chk(wf_flut == WF_FLUT_EKTAP && wf_curve == WF_CURVE_VLOG, "row +1: Windy EKTA P through EKTAP_VL.CUB (%d, %d)",
        wf_flut, wf_curve);

    /* the can's Load: Canon Log 2 and EKTAP_C2.CUB whatever the curve before, EKTAP_VL.CUB not used */
    chk(wf_load(id11) == WF_LOAD_NOCUBE, "Windy EKTA P without EKTAP_C2.CUB: no load (EKTAP_VL.CUB does not count)");
    cube_write("ML/DATA/EKTAP_C2.CUB", 9);
    log_mark();
    chk(wf_load(id11) == WF_LOAD_OK && wf_flut == WF_FLUT_EKTAP && wf_curve == WF_LL_CLOG2 && wf_loaded() == id11,
        "load Windy EKTA P: Canon Log 2 + EKTAP_C2.CUB");
    chk(LOGW("lut EKTAP_C2.CUB", 60000) && WAIT(wf_ll_state(WF_LL_CLOG2) == WF_LL_OK, 5000),
        "EKTAP_C2.CUB loaded: '%s'", log_line("lut EKTAP_C2.CUB"));
    wf_out = WF_OUT_OFF;
    mod_deinit();
}

/* a 65^3 cube dropped in under a can's name (owner 11-14: the final CinePrint cubes are 65^3 exports of the C-Log2
 * PowerGrades, the 33^3 OSM cubes until then): no code change. It parses over a 33^3 file of the same name (not the old
 * cache), its .FRT is as big as a 33^3 one's and is used on the next load, and an identity cube gives the same tables at
 * both sizes (+-1). WF_CUBE_REAL=<file>[:<file>..] also loads real cubes under that name and prints the first-load and
 * cache times (qemu wall time: only their ratio means something for the camera) */
static long fsize(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_size; }
static long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000; }
static int ll_maxd(const struct wf_lltabs *a, const struct wf_lltabs *b)
{
    int m = 0;
    for (int i = 0; i < 256; i++) { int d = abs(a->N[i] - b->N[i]); if (d > m) m = d; }
    for (int i = 0; i < WF_LL_NODES; i++) {
        int d = abs(a->Y[i] - b->Y[i]);
        int e = abs((int)(a->C[i] & 0xffff) - (int)(b->C[i] & 0xffff)) / 2, f = abs((int)(a->C[i] >> 16) - (int)(b->C[i] >> 16)) / 2;
        if (d > m) m = d;
        if (e > m) m = e;
        if (f > m) m = f;
    }
    return m;
}
static int fcopy(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = a ? fopen(to, "wb") : 0;
    char buf[65536];
    size_t n;
    int ok = a && b;
    while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0)
        ok = fwrite(buf, 1, n, b) == n;
    if (a) fclose(a);
    if (b) fclose(b);
    return ok;
}
static void t_cube65(void)
{
    static struct wf_lltabs l33, l65, lc;
    struct wf_ll_info inf;
    const char *cub = "ML/DATA/T250DPC2.CUB", *frt = "ML/DATA/T250DPC2.FRT";
    char nb[WF_LL_NAME];
    chk(!strcmp(wf_loglut_name(nb, WF_LL_CLOG2, 12, 1), frt), "the cache of Windy 250D P's cube: %s", nb);
    wf_ll_ei = 0;
    wf_ll_auto = 1;
    wf_ll_disp = 0;
    cube_write(cub, 33);
    unlink(frt);
    long t0 = ms_now();
    int rc = wf_loglut_load_cached(cub, frt, WF_LL_CLOG2, &l33, &inf);
    long t33 = ms_now() - t0, s33 = fsize(frt);
    chk(rc == WF_LL_OK && !inf.cached && inf.n == 33 && s33 > (long)sizeof l33,
        "33^3 identity: parsed in %ld ms, .FRT %ld B", t33, s33);
    cube_write(cub, 65);                         /* the 65^3 file over it, same name */
    long c65 = fsize(cub);
    t0 = ms_now();
    rc = wf_loglut_load_cached(cub, frt, WF_LL_CLOG2, &l65, &inf);
    long t65 = ms_now() - t0;
    chk(rc == WF_LL_OK && !inf.cached && inf.n == 65 && inf.samples == 65 * 65 * 65 && inf.clamped == 0,
        "65^3 over the 33^3 file (%ld B): parsed, not the old cache, %ld ms (%ld.%ld x the 33^3 time), peak %u B",
        c65, t65, t33 ? t65 / t33 : 0, t33 ? t65 * 10 / t33 % 10 : 0, (unsigned)inf.peak);
    chk(fsize(frt) == s33, ".FRT %ld B, as for 33^3 (%ld)", fsize(frt), s33);
    t0 = ms_now();
    rc = wf_loglut_load_cached(cub, frt, WF_LL_CLOG2, &lc, &inf);
    chk(rc == WF_LL_OK && inf.cached && ll_same(&l65, &lc), "next load from the cache, same tables, %ld ms", ms_now() - t0);
    int md = ll_maxd(&l33, &l65);
    chk(md <= 1, "identity at 33^3 and 65^3: the same tables (max %d code)", md);

    /* real cubes (manual: WF_CUBE_REAL=a.cube:b.cube ./test.sh CUBE65) */
    const char *rl = getenv("WF_CUBE_REAL");
    static char list[2048];
    snprintf(list, sizeof list, "%s", rl ? rl : "");
    for (char *f = strtok(list, ":"); f; f = strtok(0, ":")) {
        unlink(frt);
        if (!fcopy(f, cub)) { printf("real cube %s: not copied\n", f); continue; }
        t0 = ms_now();
        rc = wf_loglut_load_cached(cub, frt, WF_LL_CLOG2, &l65, &inf);
        long a = ms_now() - t0;
        t0 = ms_now();
        int rc2 = wf_loglut_load_cached(cub, frt, WF_LL_CLOG2, &lc, &inf);
        long b = ms_now() - t0;
        printf("real cube %s: %ld B, %d^3, rc %d, first load %ld ms, then cache rc %d cached %d %ld ms, .FRT %ld B, '%s'\n",
               f, fsize(cub), inf.n, rc, a, rc2, inf.cached, b, fsize(frt), inf.title);
    }
    unlink(cub);
    unlink(frt);
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    mallopt(M_ARENA_MAX, 1);
    fa = map_at(FRAME_A, A_USED);                /* first, before any thread or large allocation */
    fb = map_at(FRAME_B, B_USED);
    fc = map_at(FRAME_C, C_USED);
#ifndef WF_BUILD_5D2
    if (argc > 1 && !strcmp(argv[1], "RAWB"))
        rb_map = map_at(RB_Z, 0x8000000u - 4096);  /* RAWB: before the frame mallocs can land in 0x50000000.. */
#else
    if (argc > 1 && !strcmp(argv[1], "RAWB"))
        r5_map = map_at(R5_M1, R5_MAPN);         /* RAWB: MEM1 before the frame mallocs */
#endif
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
    else if (!strcmp(mode, "FXAB")) t_fxab();
    else if (!strcmp(mode, "MIST")) t_mist();
    else if (!strcmp(mode, "EXIF")) t_exif();
    else if (!strcmp(mode, "EXPSIM")) t_expsim();
    else if (!strcmp(mode, "CFG")) t_cfg();
    else if (!strcmp(mode, "PACK")) t_pack(cs, argc > 3 ? argv[3] : 0);
    else if (!strcmp(mode, "LIGHT")) t_light();
    else if (!strcmp(mode, "PRINT")) t_print();
    else if (!strcmp(mode, "PRINT:chain")) t_print_chain();
    else if (!strcmp(mode, "LOG")) t_log(cs);
    else if (!strcmp(mode, "RAWB")) t_rawb();
    else if (!strcmp(mode, "HS")) t_hs();
    else if (!strcmp(mode, "LEICA")) t_leica();
    else if (!strcmp(mode, "CLOG2")) t_clog2();
    else if (!strcmp(mode, "CUBE65")) t_cube65();
#ifdef WF_BUILD_5D2
    else if (!strcmp(mode, "5D2")) t_5d2();
    else if (!strcmp(mode, "PROBE")) t_probe_5d2();
#else
    else if (!strcmp(mode, "PROBE")) t_probe(cs);
#endif
    else { fprintf(stderr, "unknown mode %s for %s\n", mode, CAM_NAME); return 2; }
    chk(h_fio_dup == 0, "no card file created over an existing name (%d times: two entries of one name on the camera)",
        h_fio_dup);
    chk(h_fio_dir_busy == 0, "no card file created / removed / renamed while Canon saved a shot (%d times)",
        h_fio_dir_busy);
    if (nfail)
        log_tail(25);
    printf("RESULT %s %s %s: %d pass, %d fail\n", CAM_NAME, mode, cs, npass, nfail);
    fflush(stdout);
    _exit(nfail ? 1 : 0);                        /* the module's worker stays blocked on its semaphore: no join */
}
