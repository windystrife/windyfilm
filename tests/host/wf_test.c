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
 *   FX bloom           Bloom on its own row (RAWB11): halation alone, bloom alone, both, both off, levels, rows, EXIF text;
 *                      WF_FX_DUMP=1 [WF_FX_UYVY=frame] also writes bloom_<level>.ppm (the harness strip)
 *   FX lab             the film-lab Halation / Bloom / Grain (yl_lab.c): the glow composite and the sky edge, config words,
 *                      rows, keys, the glows, 8 mm, grain, EXIF; WF_FX_DUMP=1 [WF_FX_UYVY=frame] also writes lab_<set>.ppm (tests/ui/lab_strip.sh)
 *   FXAB             Even grain / Wrap (the Genesis Pro 1.6.6 model, DESIGN 1a) against the stages before; Bloom alone,
 *                      the Gauge (16 / 35 / 65 mm: halo reach, grain size)
 *                      them on the same frame: the halo round a light, the core of a light (difference of blur keeps
 *                      it), no pixel darker than the plain look, the three grain amounts monotonic, and the frames
 *                      not equal to the old ones
 *   MIST               white / black mist: glow, veil, halves vs whole frame, seam
 *   EXIF               wf_ex_write on LE / BE files and odd files; the ring vs IMG_nnnn.JPG numbers (RAW-only gap, JPEG M)
 *   EXPSIM             dEV, smoothing, gain tables, ExpSim off / back / stuck, the display filter (EOS M; 5D2: stays off)
 *   CFG                WINDYFLM.CFG load (every key), save, garbage, oversize, userwb restore at unload
 *   PACK CASE          ok | missing | magic | index | top | fnv | code | dup | flags | real FILE
 *   LIGHT              Day / Cloudy / Shade / Tungsten / Auto, film kelvin, user WB back
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
 *   RAWB chain         (EOS M) option (c): a chain film's RAW shot from its log table in WFLOG.DAT = the direct develop
 *                      with that table (S-Log3, LUT EI 0, no auto exposure), bit-exact, no film table after it; strength
 *                      30 %, a print the pack lacks and a bad pack keep the 8-bit chain bit-exact, the reason logged
 *   RAWB knobs         (EOS M) the knob rows on RAW: Interlayer 0 / 50 / 200 %, Film exp +1, Film exp -2 + Push -1/3
 *                      with Printer R +12, Exp comp on, a mixed set = the direct develop with the composed log table +
 *                      the shipped table's residual (worked out here, read at the node the exposure offset moves to),
 *                      bit-exact; the residual's clip logged; Film exp + Push cancelling and default knobs give the
 *                      shipped table (the same frame); strength 30 % keeps the 8-bit chain; a WFSTLOG.DAT made for
 *                      other halves the 8-bit chain with the reason
 *   HS                 Highlights / Shadows: wf_hs_bend vs the formula (121 pairs), never down, 0 / 0 bit-identical to the
 *                      film builder before the feature (FNV), only N changes; rows, film shot + log + EXIF + preview; Log +
 *                      LUT bent after the load (.FRT unbent), an h / s change bends again without a load; flat Log / Off
 *                      ignore it; hl / sh in the file
 *   ROLL               Roll + Darkroom in the module (DESIGN 3.6): both modes at once with RAW develop (no switch-off
 *                      either way; RAWB checks a B shot that counts and a darkroom run taking B's memory), frames from
 *                      file number + CR2, PLAY blocked while a roll is in the camera, the roll file; 5D2: load refused
 *   ROLL dark / boot   RAWB9: a roll frame keeps Canon's JPEG (no look), nothing of the look is built while a roll is in,
 *                      the Darkroom starts with DryOS's task table full (the UI task on the last slot) and builds the
 *                      look for its run; boot: the roll file read first, no build
 *   ROLL mem           owner RAWB17 (11-16): the camera's memory (one 31,888 KB SRM chunk, no 35 MB shoot block): the
 *                      "memory short" line; one-block vs split frame on three real RAW+L pairs, byte for byte after
 *                      APP1; the owner's look with the head from shoot chunks (pairs: WF_DARK_CR2, skipped without)
 *   ROLL jpeg          owner 11-16: Camera JPEG Keep (our Exif thumbnail in the prints) / Overwrite (frame Off: the
 *                      print's bytes; a frame: the picture at 5184 x 3456; a missing JPEG skipped), the CR2s untouched,
 *                      no temp file left; the logo pack (WF_LOGO_DAT) drawn with the frame; owner 11-17 (playback
 *                      still Canon's): each overwritten JPEG holds only our two pictures, the CR2 Canon's own
 *   LOG [big]        the module's log: appended across boots, time stamps, a busy card (lines wait, nothing removed,
 *                      the refusal logged), ring full (lines counted), a short write (no line twice); big: a log over
 *                      512 KB becomes WINDYFLM.OLD at boot
 * Output: PASS / FAIL lines, then "RESULT MODE CASE: P pass, F fail"; exit status 1 on any failure. */
#define _GNU_SOURCE
#include <dirent.h>
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
#include "wf_dark.h"
#include "wf_dev.h"
#include "wf_darkp.h"
#include "wf_cr2.h"
#include "wf_print.h"
#include "yl_lab.h"
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
static int file_exists(const char *path) { return access(path, F_OK) == 0; }
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
    if (getenv("WF_SHOT_HASH"))                  /* tools: every shot's result as a hash, to hold a build against another one */
        printf("shot hash %08x\n", fnv(2166136261u, (const uint8_t *)dst, (size_t)n * 4));
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
    return wf_look_now(&lk) != WF_OUT_FILM || wf_ws_table(lk.row, lk.variant, lk.stk) != 0;
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
    if (!strcmp(cs, "late") || !strcmp(cs, "noworker")) {   /* DryOS's task table full at boot (owner RAWB8 "worker 5") */
        int late = !strcmp(cs, "late");
        h_task_fail = late ? 3 : 1000;
        std_pack();
        int s0 = h_nsem;
        mod_init(0);
        chk(LOGW("worker 5 prio 1c", 3000) && LOGW("try 1, tasks 70 of 72", 3000) && !wf_cam_shot_ok(),
            "a refused worker: logged with the task count, no hooks yet");
        chk(wf_films_state() == 0 && !*wf_status() && !wf_avail(wf_films[fA].id),
            "while it tries again: tables 'starting' (%d), no status yet ('%s')", wf_films_state(), wf_status());
        if (late) {
            chk(WAIT(wf_cam_shot_ok(), 5000) && LOGW("hooks: installed (0), try 4", 3000), "the 4th try starts it: %s",
                log_line("hooks:"));
            chk(WAIT(wf_ws_state() == 1, 5000) && LOGW("pack ok", 3000) && wf_films_state() == 1 && wf_avail(wf_films[fA].id)
                && !*wf_status(), "the pack is read after the late start (state %d)", wf_films_state());
        } else {
            chk(WAIT(log_has("hooks: not installed (-100), try 30"), 25000) && !strcmp(wf_status(), "Worker failed")
                && wf_films_state() == -1 && !wf_cam_shot_ok(), "30 tries then 'Worker failed' (%d, '%s')",
                wf_films_state(), wf_status());
            usleep(1200000);
            chk(!log_has("try 31"), "no 31st try");
        }
        chk(h_nsem - s0 == 3, "the worker's 3 semaphores made once over all the tries: %d", h_nsem - s0);
        mod_deinit();
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
    chk(wf_halo == 1 && wf_bloom == 2, "halation=bloom (before RAWB11) read as Wide + Bloom Normal (%d, %d)", wf_halo, wf_bloom);
    wf_bloom = 1;                                /* a new halation-stage key */
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
    wf_bloom = 0;
    wf_grain = 0;
    chk(WAIT(!wf_fx_hk && !wf_fx_gk && h_arena_live == 0, 20000), "effects off: shoot memory and heap blocks freed (%d live)", h_arena_live);
    mod_deinit();
}

/* ------------------------------------------------------------------ FX:bloom */
/* RAWB11 (owner 11-15): Bloom on its own row. Halation alone, bloom alone, both, both off: the keys, the ready line, the
 * shot line, the EXIF text, the rows. Then the bloom strip with WF_FX_DUMP set: halation off, bloom Off / Low / Normal /
 * Strong on one frame (WF_FX_UYVY = an FW x FH UYVY file, words {U s8, Y0, V s8, Y1}; else the disc), written as
 * bloom_<level>.ppm at 1/2 size for the harness strip (tests/ui/bloom_strip.sh) */
const char *wf_exif_text(const struct wf_look *lk, int g, int h, int bl, int m, int ev, char *b, int bn);
static void dump_half(const char *path, const uint32_t *p, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int ow = w / 2, oh = h / 2;
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    unsigned char *row = malloc((size_t)ow * 3);
    for (int y = 0; row && y < oh; y++) {
        for (int x = 0; x < ow; x++) {
            uint32_t q = p[(size_t)y * 2 * (w / 2) + x];
            int u = (int8_t)(q & 255), yy = (q >> 8) & 255, v = (int8_t)((q >> 16) & 255);
            row[x * 3] = (unsigned char)clampi(yy + 1402 * v / 1000, 0, 255);
            row[x * 3 + 1] = (unsigned char)clampi(yy - (344 * u + 714 * v) / 1000, 0, 255);
            row[x * 3 + 2] = (unsigned char)clampi(yy + 1772 * u / 1000, 0, 255);
        }
        fwrite(row, 1, (size_t)ow * 3, f);
    }
    free(row);
    fclose(f);
}
static void dump_full(const char *path, const uint32_t *p, int w, int h)   /* the whole frame as RGB (lab strip) */
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    unsigned char *row = malloc((size_t)w * 3);
    for (int y = 0; row && y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t q = p[(size_t)y * (w / 2) + x / 2];
            int u = (int8_t)(q & 255), yy = x & 1 ? q >> 24 : (q >> 8) & 255, v = (int8_t)((q >> 16) & 255);
            row[x * 3] = (unsigned char)clampi(yy + 1402 * v / 1000, 0, 255);
            row[x * 3 + 1] = (unsigned char)clampi(yy - (344 * u + 714 * v) / 1000, 0, 255);
            row[x * 3 + 2] = (unsigned char)clampi(yy + 1772 * u / 1000, 0, 255);
        }
        fwrite(row, 1, (size_t)w * 3, f);
    }
    free(row);
    fclose(f);
}
/* set halation / bloom, wait for the stage's key (or none), shoot the frame in `in`; the glow ring's mean dY (dV in *rv) */
static double fx_bloom_shot(const char *what, int halo, int bloom, double *rv)
{
    log_mark();
    wf_halo = halo;
    wf_bloom = bloom;
    int want = halo || bloom;
    chk(WAIT(want ? wf_fx_hk && WF_FXK_HALO(wf_fx_hk) == halo && WF_FXK_BLOOM(wf_fx_hk) == bloom : !wf_fx_hk, 60000),
        "%s: halation-stage key %x (halation %d, bloom %d)", what, wf_fx_hk, WF_FXK_HALO(wf_fx_hk), WF_FXK_BLOOM(wf_fx_hk));
    shot(in, out, FW, FH);
    chk(LOGW("shot #", 5000), "%s: %s", what, log_line("shot #"));
    double ry = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'y', 0);
    if (rv)
        *rv = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'v', 0);
    return ry;
}
static void t_fx_bloom(void)
{
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=halation\nbloom=off\nmist=off\n", wf_films[fC].id);
    wf_fx_retry_ms = 500;
    mod_init(1);
    chk(wf_halo == 1 && wf_bloom == 0, "halation=halation + bloom=off: two settings (%d, %d)", wf_halo, wf_bloom);
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    disc_r = 60;
    gen_frame(in, FW, FH, 1);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    chk(WAIT(look_table_ready(), 10000), "table published");
    chk(WAIT(wf_fx_hk != 0, 120000) && strstr(log_line("fx halation "), ", bloom off, 35mm ready") && !strstr(log_line("fx halation "), "off ("),
        "halation alone: ready line '%s'", log_line("fx halation "));
    double rvh, rvb, rvhb, rv0;
    double ryh = fx_bloom_shot("halation alone", 1, 0, &rvh);
    const char *sl = log_line("shot #");
    chk(strstr(sl, " + halation ") && !strstr(sl, "bloom") && fx_line_ok(log_line(": film "), " h", 0),
        "halation alone: shot line '%s'", sl);
    chk(rvh > 0.2 && ryh > 0.3, "halation alone: the red edge (dY %+.2f, dV %+.2f)", ryh, rvh);
    chk(wf_fx_state(WF_FX_HAL) == WF_FXS_READY && wf_fx_state(WF_FX_BLM) == WF_FXS_OFF, "menu state: Halation ready, Bloom Off");

    double ryb = fx_bloom_shot("bloom alone", 0, 2, &rvb);
    sl = log_line("shot #");
    chk(strstr(sl, " + bloom normal ") && !strstr(sl, "halation") && fx_line_ok(log_line(": film "), " h", 0),
        "bloom alone: shot line '%s'", sl);
    chk(strstr(log_line("fx halation "), "fx halation off (") && strstr(log_line("fx halation "), ", bloom normal, 35mm ready"),
        "bloom alone: ready line '%s'", log_line("fx halation "));
    chk(ryb > 0.3 && fabs(rvb) < rvh / 2, "bloom alone: a glow in the light's own colour (dY %+.2f, dV %+.2f; halation dV %+.2f)",
        ryb, rvb, rvh);
    chk(wf_fx_state(WF_FX_HAL) == WF_FXS_OFF && wf_fx_state(WF_FX_BLM) == WF_FXS_READY, "menu state: Halation Off, Bloom ready");

    double ryhb = fx_bloom_shot("halation + bloom", 1, 2, &rvhb);
    sl = log_line("shot #");
    chk(strstr(sl, " + halation+bloom normal ") != 0, "both: shot line '%s'", sl);
    chk(ryhb > ryh && ryhb > ryb && rvhb > rvb, "both: more than either alone (dY %+.2f vs %+.2f / %+.2f, dV %+.2f)", ryhb, ryh,
        ryb, rvhb);

    double ry1 = fx_bloom_shot("bloom low", 0, 1, 0), ry3 = fx_bloom_shot("bloom strong", 0, 3, 0);
    chk(ry1 > 0.1 && ry1 < ryb && ryb < ry3, "bloom levels rise: low %+.2f < normal %+.2f < strong %+.2f", ry1, ryb, ry3);
    chk(strstr(log_line("shot #"), " + bloom strong ") != 0, "strong: shot line '%s'", log_line("shot #"));

    double ry0 = fx_bloom_shot("both off", 0, 0, &rv0);
    sl = log_line("shot #");
    chk(!strstr(sl, "halation") && !strstr(sl, "bloom") && !strstr(log_line(": film "), " h"),
        "both off: no halation stage ('%s' | '%s')", sl, log_line(": film "));
    chk(fabs(ry0) < 0.3 && fabs(rv0) < 0.3, "both off: no glow (dY %+.2f, dV %+.2f)", ry0, rv0);
    chk(WAIT(!wf_fx_hk && h_arena_live == 0, 20000), "both off: the stage's memory given back (%d live)", h_arena_live);

    /* the rows: Bloom right after Halation, the same availability, steps independently, wraps */
    wf_halo = 2;
    wf_bloom = 3;
    chk(!strcmp(wf_row_name(WF_ROW_BLOOM), "Bloom") && WF_ROW_BLOOM == WF_ROW_HALO + 1 && !strcmp(wf_row_text(WF_ROW_BLOOM), "Strong")
        && !strcmp(wf_row_text(WF_ROW_HALO), "Tight"), "rows: '%s' = '%s', Halation '%s'", wf_row_name(WF_ROW_BLOOM),
        wf_row_text(WF_ROW_BLOOM), wf_row_text(WF_ROW_HALO));
    chk(wf_row_avail(WF_ROW_BLOOM) == wf_row_avail(WF_ROW_HALO) && wf_row_avail(WF_ROW_BLOOM) == 1, "Bloom available like Halation");
    wf_row_step(WF_ROW_BLOOM, 1);
    chk(wf_bloom == WF_BLOOM_LAB && wf_halo == 2, "Bloom Strong + 1 = Lab, Halation untouched (%d, %d)", wf_bloom, wf_halo);
    wf_row_step(WF_ROW_BLOOM, 1);
    chk(wf_bloom == 0 && wf_halo == 2, "Bloom Lab + 1 wraps to Off, Halation untouched (%d, %d)", wf_bloom, wf_halo);
    wf_row_step(WF_ROW_HALO, 1);
    chk(wf_halo == 3 && wf_bloom == 0, "Halation Tight + 1 = Wrap (%d, %d)", wf_halo, wf_bloom);
    wf_row_step(WF_ROW_HALO, 1);
    chk(wf_halo == WF_HALO_LAB && wf_bloom == 0, "Halation Wrap + 1 = Lab (%d, %d)", wf_halo, wf_bloom);
    wf_row_step(WF_ROW_HALO, 1);
    chk(wf_halo == 0 && wf_bloom == 0, "Halation Lab + 1 wraps to Off (%d, %d)", wf_halo, wf_bloom);
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 1);
    chk(WAIT(!wf_row_avail(WF_ROW_BLOOM), 2000) && !wf_row_avail(WF_ROW_HALO), "1:1 aspect: Bloom greyed like Halation");
    h_prop_send(PROP_ASPECT, 0);
#endif

    /* the EXIF / print text names each part */
    struct wf_look lk;
    char tb[200];
    wf_look_now(&lk);
    const char *t = wf_exif_text(&lk, 0, 1, 2, 0, WF_EV_NONE, tb, sizeof tb);
    chk(strstr(t, " | Halation Wide | Bloom Normal") != 0, "EXIF text, both: '%s'", t);
    t = wf_exif_text(&lk, 0, 0, 1, 0, WF_EV_NONE, tb, sizeof tb);
    chk(strstr(t, " | Bloom Low") && !strstr(t, "Halation"), "EXIF text, bloom alone: '%s'", t);
    t = wf_exif_text(&lk, 0, 2, 0, 0, WF_EV_NONE, tb, sizeof tb);
    chk(strstr(t, " | Halation Tight") && !strstr(t, "Bloom"), "EXIF text, halation alone: '%s'", t);
    /* owner 11-17: a Film stage film's text ends with the develop path and its time (the Info page's "Last develop"
       reads the same); the Film section's films keep their text */
    int tn = wf_exif_tail(&lk, WF_PATH_RAW, 31240, tb, sizeof tb);
    chk(tn == (int)strlen(tb) && !strstr(tb, "Mode") && !strstr(tb, "Dev "), "EXIF tail, a Film section film: "
        "nothing added ('%s')", tb);
    struct wf_look ls = lk;
    for (int k = 0; k < wf_nfilms; k++)
        if (wf_films[k].group == WF_GRP_STAGE) { ls.row = k; break; }
    wf_exif_text(&ls, 0, 2, 0, 0, WF_EV_NONE, tb, sizeof tb);
    tn = wf_exif_tail(&ls, WF_PATH_RAW, 31240, tb, sizeof tb);
    chk(tn == (int)strlen(tb) && strstr(tb, " | Halation Tight | Mode RAW develop | Dev 31.2 s"), "EXIF tail, RAW "
        "develop 31240 ms: '%s'", tb);
    wf_exif_text(&ls, 0, 0, 0, 0, WF_EV_NONE, tb, sizeof tb);
    wf_exif_tail(&ls, WF_PATH_DARK, 0, tb, sizeof tb);
    chk(strstr(tb, " | Mode Darkroom") && !strstr(tb, "Dev "), "EXIF tail, Darkroom, no time: '%s'", tb);
    wf_exif_text(&ls, 0, 0, 0, 0, WF_EV_NONE, tb, sizeof tb);
    wf_exif_tail(&ls, WF_PATH_JPEG, 1949, tb, sizeof tb);
    chk(strstr(tb, " | Mode JPEG 8-bit | Dev 1.9 s") != 0, "EXIF tail, a Film stage film's JPEG path: '%s'", tb);
    char sm[40] = "WindyFilm: X";
    wf_exif_tail(&ls, WF_PATH_DARK, 1234567, sm, sizeof sm);
    chk(strlen(sm) < sizeof sm && !strncmp(sm, "WindyFilm: X | Mode Darkroom", 28), "EXIF tail, a short buffer: '%s'", sm);
    int lp = -1;
    wf_dev_note(&ls, WF_PATH_JPEG, 1949);
    chk(wf_dev_last(&lp) == 1949 && lp == WF_PATH_JPEG8 && !strcmp(wf_path_name(lp), "JPEG 8-bit")
        && LOGW("dev time: JPEG 8-bit 1949 ms", 2000), "Last develop: %d ms, path %d", wf_dev_last(0), lp);

    /* the bloom strip (harness only): halation off, each bloom level on one frame */
    if (getenv("WF_FX_DUMP")) {
        const char *src = getenv("WF_FX_UYVY");
        long n = 0;
        char *b = src ? get_file(src, &n) : 0;
        if (b && n == (long)NW * 4)
            memcpy(in, b, NW * 4);
        free(b);
        printf("bloom strip on %s\n", b && n == (long)NW * 4 ? src : "the disc frame");
        for (int lv = 0; lv < WF_BLOOM_LAB; lv++) {   /* the WindyFilm levels (Lab: FX:lab) */
            char pth[32];
            fx_bloom_shot("strip", 0, lv, 0);
            printf("  %s\n", log_line("shot #"));
            snprintf(pth, sizeof pth, "bloom_%d.ppm", lv);
            dump_half(pth, out, FW, FH);
        }
    }
    mod_deinit();
}

/* ------------------------------------------------------------------ FXAB: WindyGrain / WindyHalation against the old stages */
/* the most negative (b - a) luma of an annulus round the frame centre, and how many pixels it darkened */
/* ------------------------------------------------------------------ FX:lab */
/* Lab (11-21, DESIGN 3.20): the film-lab Halation / Bloom / Grain (yl_lab.c) in the module - the config words, the rows
 * and keys, a Lab value in one glow row taking the other's WindyFilm value out, the stage on the disc frame, 8 mm. How
 * close the camera's arithmetic is to the reference CLI is tools/lab_check.sh's (frames, not this test). */
static double box_dy(const uint32_t *a, const uint32_t *b, int w, int x0, int x1, int y0, int y1);
static double fx_lab_shot(const char *what, int halo, int bloom, double *rv)
{
    log_mark();
    wf_halo = halo;
    wf_bloom = bloom;
    int want = WF_HALO_RUN(halo, bloom) || WF_BLOOM_RUN(halo, bloom);
    chk(WAIT(want ? wf_fx_hk && WF_FXK_HLAB(wf_fx_hk) == (halo == WF_HALO_LAB) && WF_FXK_BLAB(wf_fx_hk) == (bloom == WF_BLOOM_LAB)
             : !wf_fx_hk, 60000), "%s: halation-stage key %x", what, wf_fx_hk);
    usleep(200000);
    shot(in, out, FW, FH);
    chk(LOGW("shot #", 5000), "%s: %s", what, log_line("shot #"));
    double ry = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'y', 0);
    if (rv)
        *rv = ring(ref, out, FW, FH, disc_r + 6, disc_r + 40, 'v', 0);
    return ry;
}
/* the halo's glow composite (film-lab 11-21: the halo is the whole scattered light, added as it is up to half the
 * headroom, then rolled off towards white) on the camera's arithmetic: against the formula in double, and the sky
 * edge of film-lab's own check (until then the halo left out the sharp core, 0.75 x the layer: just inside every
 * highlight edge a band got no halo, a pale rim between the orange outside and the halo inside) */
static double fx_glow_ref(double v, double a)
{
    double hd = 1.0 - v;
    if (a <= 0.5 * hd) return v + a > 1.0 ? 1.0 : v + a;
    if (hd <= 0.0) return 1.0;
    return v + hd * (0.5 + 0.5 * (1.0 - exp(-(a / hd - 0.5) / 0.5)));
}
static int fx_r_of(uint32_t w) { return clampi(WY0(w) + ((359 * WV(w)) >> 8), 0, 255); }
static void t_fx_lab_glow(void)
{
    const int W = 320, H = 544, edge = 160;      /* film-lab's frame at 1080 / 2 rows: sky left of the edge, leaves right */
    yl_params lp = { 1, 0, 2 };                  /* Halation alone, 35 mm */
    yl_ctx *g = malloc(sizeof *g);
    void *mem = malloc(yl_mem_bytes(W, H, 1, 0));
    uint32_t *f = malloc((size_t)W * H * 2), *o = malloc((size_t)W * H * 2);
    yl_init(g, &lp, W, H, mem);
    double me = 0.0;
    int exact = 1, mono = 1;
    for (int v = 0; v <= 4080; v += 5)
        for (int a = 0, pv = v; a <= 4080; a += 7) {
            int c = yl_glow(g, v, a);
            double e = fabs(c - 4080.0 * fx_glow_ref(v / 4080.0, a / 4080.0));
            if (e > me) me = e;
            if (2 * a <= 4080 - v && c != v + a) exact = 0;
            if (c < pv || c > 4080) mono = 0;
            pv = c;
        }
    chk(me <= 2.0 && exact && mono, "Lab glow composite: within %.2f Q4 levels of the formula, the halo as it is below the"
        " knee (%s), rising and never past white (%s)", me, exact ? "yes" : "NO", mono ? "yes" : "NO");
    int Ys = (299 * 230 + 587 * 240 + 114 * 228 + 500) / 1000, Yl = (299 * 40 + 587 * 60 + 114 * 40 + 500) / 1000;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x += 2)
            f[(y * W + x) / 2] = x < edge ? word(564 * (228 - Ys) / 1000, Ys, 713 * (230 - Ys) / 1000, Ys)
                                          : word(564 * (40 - Yl) / 1000, Yl, 713 * (40 - Yl) / 1000, Yl);
    memcpy(o, f, (size_t)W * H * 2);
    yl_begin_frame(g);
    for (int y = 0; y < H; y++) yl_gather_chunk(g, o + y * W / 2, W, 0, y);
    yl_build(g, 0, H, YL_WHOLE);
    for (int y = 0; y < H; y++) yl_apply_chunk(g, o + y * W / 2, W, 0, y);
    const int r = H / 2 * W / 2;
    int in = fx_r_of(o[r + 20]) - fx_r_of(f[r + 20]), near = fx_r_of(o[r + edge / 2 - 2]) - fx_r_of(f[r + edge / 2 - 2]);
    int out = fx_r_of(o[r + edge / 2 + 2]) - fx_r_of(f[r + edge / 2 + 2]);
    /* 11-21: the old composite (core left out, hard clamp) gave inside +25, 4 px inside the edge +8, outside +43 */
    chk(in > 10 && near * 2 >= in && out > 0, "Lab Halation, sky edge: red added inside %+d, 4 px inside the edge %+d (at least"
        " half: no pale rim), 4 px outside %+d", in, near, out);
    free(o);
    free(f);
    free(mem);
    free(g);
}
static void t_fx_lab(void)
{
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    int hk = 0, gk = 0, hk16 = 0, post;
    double rvh, rvb, rvhb, sd, sd8, sd65, cm;
    char tb[200];
    t_fx_lab_glow();
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=lab\nhalation=lab\nbloom=lab\ngauge=8\nmist=off\n", wf_films[fC].id);
    wf_fx_retry_ms = 500;
    mod_init(1);
    chk(wf_grain == WF_GRAIN_LAB && wf_halo == WF_HALO_LAB && wf_bloom == WF_BLOOM_LAB && wf_gauge == WF_GAUGE_8,
        "cfg grain / halation / bloom = lab, gauge = 8 read (%d %d %d %d)", wf_grain, wf_halo, wf_bloom, wf_gauge);
    chk(!strcmp(wf_row_text(WF_ROW_GRAIN), "Lab") && !strcmp(wf_row_text(WF_ROW_HALO), "Lab") && !strcmp(wf_row_text(WF_ROW_BLOOM), "Lab")
        && !strcmp(wf_row_text(WF_ROW_GAUGE), "8 mm") && !strcmp(wf_fx_gstock_name(), "Lab"), "rows Lab / Lab / Lab / 8 mm, stock"
        " '%s'", wf_fx_gstock_name());
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    disc_r = 60;
    gen_frame(in, FW, FH, 1);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    chk(WAIT(look_table_ready(), 10000), "table published");
    post = wf_fx_mode();
    chk(wf_fx_keys(post, &hk, &gk) && WF_FXK_HLAB(hk) && WF_FXK_BLAB(hk) && !WF_FXK_HALO(hk) && !WF_FXK_BLOOM(hk)
        && WF_FXK_HGAUGE(hk) == WF_GAUGE_8 && WF_FXK_GLAB(gk) && WF_FXK_GGAUGE(gk) == WF_GAUGE_8 && !(gk & 0x1c0),
        "Lab keys: stage %x, grain %x", hk, gk);
    chk(WAIT(wf_fx_hk == hk && wf_fx_gk == gk, 120000) && LOGW("fx halation lab, bloom lab, 8mm Lab ready", 5000)
        && LOGW("fx grain lab 8mm ready", 5000), "Lab ready: '%s' | '%s'", log_line("fx halation "), log_line("fx grain "));
    chk(wf_fx_state(WF_FX_HAL) == WF_FXS_READY && wf_fx_state(WF_FX_BLM) == WF_FXS_READY && wf_fx_state(WF_FX_GRN) == WF_FXS_READY,
        "menu state: all three ready");
    usleep(200000);
    log_mark();
    chk(shot(in, out, FW, FH) == 0, "8 mm, all three Lab: the frame's words only");
    chk(LOGW("shot #", 5000), "all three: a shot line");
    const char *sl = log_line("shot #");
    chk(strstr(sl, " + halation lab+bloom lab ") && strstr(sl, " + grain "), "all three: shot line '%s'", sl);

    /* the glows alone (grain off), 35 mm: a red-orange halo, a glow in the light's colour, both more than either */
    wf_grain = 0;
    wf_gauge = 1;
    double ryh = fx_lab_shot("Halation Lab", WF_HALO_LAB, 0, &rvh);
    sl = log_line("shot #");
    chk(strstr(sl, " + halation lab ") && !strstr(sl, "bloom") && LOGW("fx halation lab, bloom off, 35mm Lab ready", 5000),
        "Halation Lab: '%s' | '%s'", sl, log_line("fx halation "));
    chk(ryh > 0.3 && rvh > 0.2, "Halation Lab: a red-orange halo round the light (dY %+.2f, dV %+.2f)", ryh, rvh);
    cm = corner(ref, out, FW, &sd);
    chk(cm == 0 && sd == 0, "Halation Lab: far from the light the frame is the film look alone (%+.3f, sd %.3f)", cm, sd);
    double ryb = fx_lab_shot("Bloom Lab", 0, WF_BLOOM_LAB, &rvb);
    chk(strstr(log_line("shot #"), " + bloom lab ") && !strstr(log_line("shot #"), "halation"), "Bloom Lab: '%s'", log_line("shot #"));
    chk(ryb > 0.3 && fabs(rvb) < rvh / 2, "Bloom Lab: a glow in the light's own colour (dY %+.2f, dV %+.2f; halation dV %+.2f)",
        ryb, rvb, rvh);
    double ryhb = fx_lab_shot("both Lab", WF_HALO_LAB, WF_BLOOM_LAB, &rvhb);
    chk(ryhb > ryh && ryhb > ryb && rvhb > rvb, "both Lab: more than either alone (dY %+.2f vs %+.2f / %+.2f, dV %+.2f)", ryhb,
        ryh, ryb, rvhb);
#ifndef WF_BUILD_5D2
    {                                            /* the Diana halves against the whole frame: the light crosses the seam (the
                                                    centre row); its glow in the rows above and below it, as WindyHalation's */
        int x0 = FW / 2 - disc_r - 40, x1 = FW / 2 + disc_r + 40;
        double wa = box_dy(ref, out, FW, x0, x1, FH / 2 - 6, FH / 2), wb = box_dy(ref, out, FW, x0, x1, FH / 2, FH / 2 + 6);
        double wa2 = box_dy(ref, out, FW, x0, x1, FH / 2 - 60, FH / 2 - 6), wb2 = box_dy(ref, out, FW, x0, x1, FH / 2 + 6, FH / 2 + 60);
        halves = 1;
        log_mark();
        shot(in, out2, FW, FH);
        halves = 0;
        chk(LOGW("(TOP ", 5000), "both Lab, halves shot: %s", log_line("(TOP "));
        double ha = box_dy(ref, out2, FW, x0, x1, FH / 2 - 6, FH / 2), hb = box_dy(ref, out2, FW, x0, x1, FH / 2, FH / 2 + 6);
        double ha2 = box_dy(ref, out2, FW, x0, x1, FH / 2 - 60, FH / 2 - 6), hb2 = box_dy(ref, out2, FW, x0, x1, FH / 2 + 6, FH / 2 + 60);
        printf("INFO Lab halves seam: whole %+.2f | %+.2f (wide %+.2f | %+.2f), halves %+.2f | %+.2f (wide %+.2f | %+.2f), max diff"
               " %d, mean %+.3f\n", wa, wb, wa2, wb2, ha, hb, ha2, hb2, max_d(out, out2, NW), mean_dy(out, out2, NW));
        /* 11-21 EOS M: at the seam +15.81 / +15.65 (whole +15.76 / +15.55), 6..60 rows off +19.86 / +20.89 (+19.91 / +19.49):
         * below, the BOT fades from the TOP's glow over YL_XF rows */
        chk(fabs(ha - wa) < 1.0 && fabs(hb - wb) < 1.0 && fabs(ha2 - wa2) < 1.0 && fabs(hb2 - wb2) < 1.5, "both Lab, halves ="
            " whole frame round the seam: above %+.2f / %+.2f (whole %+.2f / %+.2f), below %+.2f / %+.2f (whole %+.2f / %+.2f)",
            ha, ha2, wa, wa2, hb, hb2, wb, wb2);
    }
    {                                            /* a white band over the frame's top, far from the seam: the BOT's first rows
                                                    fade from the TOP's own seam rows, not from the band's glow (11-21: with
                                                    both on, the bloom's seam rows were kept past the halation's block and
                                                    written over by the apply and the bloom grid; IMG_8553 on the 5D2: +247
                                                    levels in the BOT's first rows) */
        const int bn = 96 * (FW / 2), s0 = (FH / 2 - 16) * (FW / 2), sn = (16 + 256) * (FW / 2);   /* YL_XF rows */
        uint32_t *keep = malloc((size_t)bn * 4);
        memcpy(keep, in, (size_t)bn * 4);
        for (int i = 0; i < bn; i++) in[i] = word(0, 255, 0, 255);
        log_mark();
        shot(in, out, FW, FH);
        chk(LOGW("shot #", 5000), "white band, whole frame: %s", log_line("shot #"));
        halves = 1;
        log_mark();
        shot(in, out2, FW, FH);
        halves = 0;
        chk(LOGW("(TOP ", 5000), "white band, halves: %s", log_line("(TOP "));   /* its line before the next case's mark */
        memcpy(in, keep, (size_t)bn * 4);
        free(keep);
        int md = max_d(out + s0, out2 + s0, sn);
        double mm = mean_dy(out + s0, out2 + s0, sn);
        printf("INFO Lab halves, white band at the top: round the seam max diff %d, mean %+.3f\n", md, mm);
        chk(md <= 12 && fabs(mm) < 0.25, "both Lab, white band at the top: halves = whole frame round the seam (max diff %d,"
            " mean %+.3f)", md, mm);
    }
#endif

    /* the rows together: a Lab value in one glow row takes the other's WindyFilm value out (the menu shows it unused) */
    fx_lab_shot("Wide + Bloom Lab", 1, WF_BLOOM_LAB, 0);
    chk(wf_fx_state(WF_FX_HAL) == WF_FXS_UNUSED && wf_fx_state(WF_FX_BLM) == WF_FXS_READY && !strstr(log_line("shot #"), "halation"),
        "Halation Wide + Bloom Lab: the bloom alone, Halation unused ('%s')", log_line("shot #"));
    fx_lab_shot("Halation Lab + Bloom Normal", WF_HALO_LAB, 2, 0);
    chk(wf_fx_state(WF_FX_BLM) == WF_FXS_UNUSED && wf_fx_state(WF_FX_HAL) == WF_FXS_READY && !strstr(log_line("shot #"), "bloom"),
        "Halation Lab + Bloom Normal: the halo alone, Bloom unused ('%s')", log_line("shot #"));
    wf_halo = 1;
    wf_bloom = 0;
    wf_gauge = WF_GAUGE_8;
    wf_fx_keys(post, &hk, &gk);
    wf_gauge = 0;
    wf_fx_keys(post, &hk16, &gk);
    chk(hk == hk16 && WF_FXK_HGAUGE(hk) == 0, "8 mm with Wide: the WindyFilm stage as 16 mm (key %x = %x)", hk, hk16);
    wf_halo = 0;

    /* the grain alone on a mid grey (its zone weights peak there): film-lab's strength at full size is ~2 levels sd at
     * 8 mm and ~0.7 at 65 mm (amount 50 / 15, the 800-row gain); no mean shift beyond the draw (the 300 x 300 px corner
     * holds ~60 of the 8 mm grain's 38 px cells: its mean has an sd of ~0.25 levels) */
    for (int i = 0; i < NW; i++) in[i] = word(0, 128, 0, 128);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    wf_grain = WF_GRAIN_LAB;
    wf_gauge = WF_GAUGE_8;
    log_mark();
    chk(wf_fx_keys(post, &hk, &gk) && !hk && WAIT(wf_fx_gk == gk && !wf_fx_hk, 120000),
        "Grain Lab 8 mm ready (%s)", log_line("fx grain"));
    usleep(200000);
    shot(in, out, FW, FH);
    cm = corner(ref, out, FW, &sd8);
    wf_gauge = 2;
    log_mark();
    chk(wf_fx_keys(post, &hk, &gk) && WAIT(wf_fx_gk == gk, 120000) && LOGW("fx grain lab 65mm ready", 5000),
        "Grain Lab 65 mm ready (%s)", log_line("fx grain "));
    usleep(200000);
    shot(in, out, FW, FH);
    corner(ref, out, FW, &sd65);
    printf("INFO Lab grain on mid grey (Y %d): sd 8 mm %.3f, 65 mm %.3f, mean %+.3f\n", WY0(ref[0]), sd8, sd65, cm);
    chk(sd8 > 1.2 && sd8 < 3.0 && sd65 > 0.35 && sd65 < 1.2 && sd8 > sd65 && fabs(cm) < 0.75,
        "Grain Lab sd on mid grey: 8 mm %.3f (1.2 .. 3), 65 mm %.3f (0.35 .. 1.2), mean %+.3f", sd8, sd65, cm);

    /* the EXIF / print text, the rows' steps, the config written back */
    struct wf_look lk;
    wf_look_now(&lk);
    lk.gauge = WF_GAUGE_8;
    const char *t = wf_exif_text(&lk, WF_GRAIN_LAB, WF_HALO_LAB, WF_BLOOM_LAB, 0, WF_EV_NONE, tb, sizeof tb);
    chk(strstr(t, " | Grain Lab | Halation Lab | Bloom Lab | Gauge 8 mm") != 0, "EXIF text, Lab: '%s'", t);
    t = wf_exif_text(&lk, 2, 1, 0, 0, WF_EV_NONE, tb, sizeof tb);
    chk(strstr(t, " | Halation Wide | Gauge 16 mm") != 0, "EXIF text, WindyFilm at 8 mm: as 16 mm ('%s')", t);
    wf_halo = WF_HALO_LAB;
    wf_bloom = WF_BLOOM_LAB;
    wf_gauge = WF_GAUGE_8;
    wf_row_step(WF_ROW_GAUGE, 1);
    chk(wf_gauge == 0 && !strcmp(wf_row_text(WF_ROW_GAUGE), "16 mm"), "Gauge 8 mm + 1 = 16 mm (%d)", wf_gauge);
    wf_gauge = 2;
    wf_row_step(WF_ROW_GAUGE, 1);
    chk(wf_gauge == WF_GAUGE_8, "Gauge 65 mm + 1 wraps to 8 mm (%d)", wf_gauge);
    wf_row_step(WF_ROW_GAUGE, -1);
    chk(wf_gauge == 2, "Gauge 8 mm - 1 = 65 mm (%d)", wf_gauge);
    {                                            /* modernUI: the film OS's notes (wf_row_note), what does not run as shown */
        int g0 = wf_grain, h0 = wf_halo, b0 = wf_bloom, ga0 = wf_gauge, k = -1;
        const char *t;
        wf_grain = WF_GRAIN_LAB;
        wf_gauge = WF_GAUGE_8;
        chk(!wf_row_note(WF_ROW_GRAIN, 0) && !wf_row_note(WF_ROW_HALO, 0) && !wf_row_note(WF_ROW_BLOOM, 0)
            && !wf_row_note(WF_ROW_GAUGE, &k) && k == WF_NOTE_NONE, "notes: all three Lab at 8 mm, none (%d)", k);
        wf_halo = 1;
        t = wf_row_note(WF_ROW_HALO, &k);
        chk(t && k == WF_NOTE_UNUSED && strstr(t, "Bloom is Lab"), "notes: Halation Wide with Bloom Lab not used ('%s')", t ? t : "");
        wf_halo = WF_HALO_LAB;
        wf_bloom = 2;
        t = wf_row_note(WF_ROW_BLOOM, &k);
        chk(t && k == WF_NOTE_UNUSED && strstr(t, "Halation is Lab"), "notes: Bloom Normal with Halation Lab not used ('%s')",
            t ? t : "");
        wf_halo = 3;
        t = wf_row_note(WF_ROW_BLOOM, &k);
        chk(t && k == WF_NOTE_UNUSED && strstr(t, "Wrap"), "notes: Bloom with Wrap not used ('%s')", t ? t : "");
        wf_grain = 2;
        wf_halo = 1;
        int kg = 0, kh = 0, kb = 0, kga = 0;
        chk(wf_row_note(WF_ROW_GRAIN, &kg) && wf_row_note(WF_ROW_HALO, &kh) && wf_row_note(WF_ROW_BLOOM, &kb)
            && wf_row_note(WF_ROW_GAUGE, &kga) && kg == WF_NOTE_AS16 && kh == WF_NOTE_AS16 && kb == WF_NOTE_AS16
            && kga == WF_NOTE_LABGAUGE, "notes: WindyFilm at 8 mm run as 16 mm, the gauge a Lab one (%d %d %d %d)", kg, kh, kb, kga);
        wf_gauge = 0;
        chk(!wf_row_note(WF_ROW_GRAIN, 0) && !wf_row_note(WF_ROW_HALO, 0) && !wf_row_note(WF_ROW_BLOOM, 0)
            && !wf_row_note(WF_ROW_GAUGE, 0), "notes: WindyFilm at 16 mm, none");
        wf_grain = g0;
        wf_halo = h0;
        wf_bloom = b0;
        wf_gauge = ga0;
    }
    wf_row_step(WF_ROW_GAUGE, 1);
    h_clock_add_ms += 2000;
    usleep(400000);
    long n;
    char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && strstr(b, "\ngrain=lab\nhalation=lab\nbloom=lab\ngauge=8\n"), "config written: grain / halation / bloom lab, gauge 8");
    free(b);

    /* the comparison strip (harness only, tests/ui/lab_strip.sh, WF_FX_DUMP=1): Off, the WindyFilm defaults (Grain
     * Normal, Halation Wide, Bloom Normal, 35 mm) and all three Lab at 35 / 8 / 16 / 65 mm on one frame (WF_FX_UYVY, an
     * FW x FH UYVY file; else the disc), each written whole as lab_<name>.ppm */
    if (getenv("WF_FX_DUMP")) {
        static const struct { const char *nm; int g, h, b, ga; } S[] = {
            { "off", 0, 0, 0, 1 }, { "classic", 2, 1, 2, 1 }, { "lab35", WF_GRAIN_LAB, WF_HALO_LAB, WF_BLOOM_LAB, 1 },
            { "lab8", WF_GRAIN_LAB, WF_HALO_LAB, WF_BLOOM_LAB, WF_GAUGE_8 },
            { "lab16", WF_GRAIN_LAB, WF_HALO_LAB, WF_BLOOM_LAB, 0 }, { "lab65", WF_GRAIN_LAB, WF_HALO_LAB, WF_BLOOM_LAB, 2 } };
        const char *src = getenv("WF_FX_UYVY");
        long nb = 0;
        char *fb = src ? get_file(src, &nb) : 0;
        int own = fb && nb == (long)NW * 4;
        if (own)
            memcpy(in, fb, NW * 4);
        else
            gen_frame(in, FW, FH, 1);
        free(fb);
        printf("lab strip on %s\n", own ? src : "the disc frame");
        for (unsigned i = 0; i < sizeof S / sizeof *S; i++) {
            char pth[40];
            wf_grain = S[i].g; wf_halo = S[i].h; wf_bloom = S[i].b; wf_gauge = S[i].ga;
            wf_fx_keys(post, &hk, &gk);
            chk(WAIT(wf_fx_hk == hk && wf_fx_gk == gk, 120000), "strip %s: stage key %x, grain key %x", S[i].nm, hk, gk);
            usleep(200000);
            log_mark();
            shot(in, out, FW, FH);
            printf("  %s: %s\n", S[i].nm, LOGW("shot #", 5000) ? log_line("shot #") : "(no shot line)");
            snprintf(pth, sizeof pth, "lab_%s.ppm", S[i].nm);
            dump_full(pth, out, FW, FH);
        }
    }
    wf_grain = 0;
    wf_halo = 0;
    wf_bloom = 0;
    chk(WAIT(!wf_fx_hk && !wf_fx_gk && h_arena_live == 0, 20000), "all off: the memory given back (%d live)", h_arena_live);
    mod_deinit();
}

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
        for (int ga = 0; ga <= 2; ga++) {        /* 16 / 35 / 65 mm (8 mm is Lab's: the WindyFilm models take it as 16 mm) */
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
        for (int ga = 0; ga <= 2; ga++) {
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

/* ------------------------------------------------------------------ MIST:levels
 * Owner 11-15 ("kiểm tra luôn cái mist đi bạn"): every Mist level with Halation and Bloom Off on one scene: a white light
 * (the disc, centre), an orange lamp (left), green leaves with 4 x 4 px sky gaps (right). Per level: the ready line, the
 * shot line, the EXIF text, the glow round the light (near, wide), the dark far away, no colour cast round the white light,
 * the lamp's glow in the lamp's colour, the glow round the sky gaps, the 16 px cell pattern of the glow along a row and a
 * column, the TOP / BOT seam (5D2: every shot; EOS M: Diana halves at white 1), the time. Then the ML menu entry, the
 * leaves in daylight (no glowing dots) and at night, a sky / shadow edge at white 1 and black 1 (how far the shadows lift),
 * Mist 1/4 + Bloom Normal + Halation Wide (the glows add, no white wash), and the shot line with every effect on (halves):
 * it keeps its EV. Harness (tests/ui/mist_strip.sh), WF_FX_DUMP=1: mist_<level>.ppm (1/2 size) and mist_<level>_c<k>.ppm
 * (1:1 crops round WF_FX_CROP "X,Y;X,Y", frame pixels), combo_<i>[_c<k>].ppm (Off, Mist 1/4, Bloom Normal + Halation
 * Film, all three); with WF_FX_UYVY (an FW x FH UYVY frame) and WF_FX_EV8 (that photo's EV100 x 8) only the strip runs. */
#define MS_LX (FW / 4)                           /* the orange lamp: centre x (row FH / 2), radius */
#define MS_LR 40
#define MS_GX (3 * FW / 4)                       /* the leaves: centre x (row FH / 2), half size; a 4 x 4 px clipped sky gap every
                                                    MS_GS px (the owner's 11-12 canopy: every gap glowed as a round dot) */
#define MS_GR 480
#define MS_GS 160
extern const int wf_t_me_help;
static uint8_t *menu_named(const char *nm);
static int menu_set(const char *nm);
static void mist_scene(uint32_t *p)
{
    int wpr = FW / 2;
    gen_frame(p, FW, FH, 1);
    for (int y = FH / 2 - MS_GR; y < FH / 2 + MS_GR; y++)
        for (int x = (MS_GX - MS_GR) / 2; x < (MS_GX + MS_GR) / 2; x++) {
            uint32_t r = rnd();
            int gx = (2 * x) % MS_GS, gy = y % MS_GS;
            int gap = gx >= MS_GS / 2 - 2 && gx < MS_GS / 2 + 2 && gy >= MS_GS / 2 - 2 && gy < MS_GS / 2 + 2;
            p[y * wpr + x] = gap ? word(0, 255, 0, 255)
                                 : word(-14 + (int)(r & 3), 72 + (int)(r >> 4 & 7) - 4, -22 + (int)(r >> 8 & 3), 72 + (int)(r >> 12 & 7) - 4);
        }
    for (int y = FH / 2 - MS_LR; y <= FH / 2 + MS_LR; y++)
        for (int x = (MS_LX - MS_LR) / 2; x <= (MS_LX + MS_LR) / 2; x++) {
            int dx = 2 * x + 1 - MS_LX, dy = y - FH / 2;
            if (dx * dx + dy * dy <= MS_LR * MS_LR)
                p[y * wpr + x] = word(-40, 225, 70, 225);   /* R clipped (323), G 189, B 154 */
        }
}
static void sky_scene(uint32_t *p)               /* a light blue sky (232) over a shadow (30), the edge at FH / 2 */
{
    int wpr = FW / 2;
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < wpr; x++) {
            uint32_t r = rnd();
            int sky = y < FH / 2, Y = sky ? 232 : 30;
            p[y * wpr + x] = word((int)(r & 3) - 2 + (sky ? 6 : 0), Y + (int)(r >> 4 & 7) - 4, (int)(r >> 8 & 3) - 2 - (sky ? 4 : 0),
                                  Y + (int)(r >> 12 & 7) - 4);
        }
}
/* a clipped white disc of radius 80 on a dark grey (Y 30), its centre at x FW / 2, row FH / 2 + oy: a light ON the TOP /
 * BOT seam (oy 0), or above it (oy < -80) */
static void seam_scene(uint32_t *p, int oy)
{
    int wpr = FW / 2, cx = FW / 2, cy = FH / 2 + oy;
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < wpr; x++) {
            int d0 = (2 * x - cx) * (2 * x - cx) + (y - cy) * (y - cy), d1 = (2 * x + 1 - cx) * (2 * x + 1 - cx) + (y - cy) * (y - cy);
            p[y * wpr + x] = word(0, d0 < 80 * 80 ? 255 : 30, 0, d1 < 80 * 80 ? 255 : 30);
        }
}
/* halves against the whole frame (the truth) round the seam: the worst mean (b - a) of 16 x 16 boxes, rows FH / 2 - 64 ..
 * FH / 2 + 255, x FW / 2 +-320 */
static double seam_err(const uint32_t *a, const uint32_t *b)
{
    int wpr = FW / 2;
    double worst = 0;
    for (int y0 = FH / 2 - 64; y0 < FH / 2 + 256; y0 += 16)
        for (int x0 = FW / 2 - 320; x0 < FW / 2 + 320; x0 += 16) {
            double s = 0;
            for (int y = y0; y < y0 + 16; y++)
                for (int x = x0 / 2; x < x0 / 2 + 8; x++)
                    s += WY0(b[y * wpr + x]) - WY0(a[y * wpr + x]) + WY1(b[y * wpr + x]) - WY1(a[y * wpr + x]);
            s /= 2 * 16 * 8;
            if (fabs(s) > fabs(worst))
                worst = s;
        }
    return worst;
}
/* mean (b - a) over an annulus r0 <= r < r1 round (cx, cy): lane 'y' luma, 'u' / 'v' chroma, 't' / 'b' luma of its top /
 * bottom half only (rows above / below cy) */
static double ring_at(const uint32_t *a, const uint32_t *b, int cx, int cy, int r0, int r1, int lane)
{
    double s = 0;
    long n = 0;
    int wpr = FW / 2;
    for (int y = cy - r1; y <= cy + r1; y++)
        for (int x = (cx - r1) / 2; x <= (cx + r1) / 2; x++) {
            int dx = 2 * x + 1 - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (y < 0 || y >= FH || x < 0 || x >= wpr || d2 < r0 * r0 || d2 >= r1 * r1 || (lane == 't' && y >= cy)
                || (lane == 'b' && y < cy))
                continue;
            uint32_t A = a[y * wpr + x], B = b[y * wpr + x];
            s += lane == 'u' ? WU(B) - WU(A) : lane == 'v' ? WV(B) - WV(A) : (WY0(B) - WY0(A) + WY1(B) - WY1(A)) * 0.5;
            n++;
        }
    return n ? s / n : 0;
}
/* a light centred on row cy, a TOP / BOT seam through it (5D2: every shot; EOS M: Diana halves): its glow below against
 * the same place above, the worst dY below - dY above of 16 x 16 boxes at x cx +-320, 32 .. 223 rows away (dY = b - a;
 * the disc is round: a whole frame gives ~0). Mirror rows only eroded (until 11-15): -40 codes at Black 1, two dark lobes */
static double seam_lobe(const uint32_t *a, const uint32_t *b, int cx, int cy)
{
    int wpr = FW / 2;
    double worst = 0;
    for (int dy = 32; dy < 224; dy += 16)
        for (int dx = -320; dx < 320; dx += 16) {
            double s = 0;
            for (int yy = 0; yy < 16; yy++)
                for (int xx = 0; xx < 8; xx++) {
                    int x = (cx + dx) / 2 + xx, yb = cy + dy + yy, ya = cy - dy - yy;
                    uint32_t A = a[yb * wpr + x], B = b[yb * wpr + x], C = a[ya * wpr + x], D = b[ya * wpr + x];
                    s += (WY0(B) - WY0(A) + WY1(B) - WY1(A)) - (WY0(D) - WY0(C) + WY1(D) - WY1(C));
                }
            s /= 2 * 16 * 8;
            if (fabs(s) > fabs(worst))
                worst = s;
        }
    return worst;
}
static double ring_hi(const uint32_t *b, int cx, int cy, int r0, int r1, int thr)   /* share of pixels with Y >= thr */
{
    long n = 0, h = 0;
    int wpr = FW / 2;
    for (int y = cy - r1; y <= cy + r1; y++)
        for (int x = (cx - r1) / 2; x <= (cx + r1) / 2; x++) {
            int dx = 2 * x + 1 - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (y < 0 || y >= FH || x < 0 || x >= wpr || d2 < r0 * r0 || d2 >= r1 * r1)
                continue;
            h += (WY0(b[y * wpr + x]) >= thr) + (WY1(b[y * wpr + x]) >= thr);
            n += 2;
        }
    return n ? (double)h / n : 0;
}
/* round the leaves' sky gaps (the patch's outer gaps left out): mean dY at 6..40 px minus at 56..72 px (between gaps) */
static double gap_dots(const uint32_t *a, const uint32_t *b)
{
    double s = 0;
    int n = 0;
    for (int gy = (FH / 2 - MS_GR) / MS_GS * MS_GS + 2 * MS_GS; gy + MS_GS < FH / 2 + MS_GR - MS_GS; gy += MS_GS)
        for (int gx = (MS_GX - MS_GR) / MS_GS * MS_GS + 2 * MS_GS; gx + MS_GS < MS_GX + MS_GR - MS_GS; gx += MS_GS) {
            int x = gx + MS_GS / 2, y = gy + MS_GS / 2;
            s += ring_at(a, b, x, y, 6, 40, 'y') - ring_at(a, b, x, y, 56, 72, 'y');
            n++;
        }
    return n ? s / n : 0;
}
static void corner_uv(const uint32_t *a, const uint32_t *b, double *du, double *dv)   /* corner()'s box: mean dU, dV */
{
    double su = 0, sv = 0;
    long n = 0;
    int wpr = FW / 2;
    for (int y = 100; y < 400; y++)
        for (int x = 50; x < 200; x++, n++) {
            su += WU(b[y * wpr + x]) - WU(a[y * wpr + x]);
            sv += WV(b[y * wpr + x]) - WV(a[y * wpr + x]);
        }
    *du = su / n;
    *dv = sv / n;
}
/* the 16 px cell pattern of the glow: its dY profile from the disc outwards along the centre row (vert 0) or column (1),
 * 17 lines averaged, minus its 33 px running mean, averaged by phase (frame x or y) % 16: max - min of the 16 means */
static double cell_pattern(const uint32_t *a, const uint32_t *b, int vert)
{
    enum { NP = 600 };                           /* ends short of the leaves */
    static double p[NP];
    double ph[16] = { 0 };
    int nph[16] = { 0 }, wpr = FW / 2, cx = FW / 2, cy = FH / 2, d0 = disc_r + 64;
    for (int i = 0; i < NP; i++) {
        double s = 0;
        for (int k = -8; k <= 8; k++) {
            int x = vert ? cx + 2 * k : cx + d0 + i, y = vert ? cy - d0 - i : cy + k;
            uint32_t A = a[y * wpr + x / 2], B = b[y * wpr + x / 2];
            s += (x & 1) ? WY1(B) - WY1(A) : WY0(B) - WY0(A);
        }
        p[i] = s / 17;
    }
    for (int i = 16; i < NP - 16; i++) {
        double m = 0;
        for (int k = -16; k <= 16; k++)
            m += p[i + k];
        int pos = vert ? cy - d0 - i : cx + d0 + i;
        ph[pos & 15] += p[i] - m / 33;
        nph[pos & 15]++;
    }
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k < 16; k++) {
        double v = ph[k] / nph[k];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    return hi - lo;
}
static void dump_crop(const char *path, const uint32_t *p, int cx, int cy)   /* 480 x 320 at 1:1 round (cx, cy) */
{
    enum { CW = 480, CH = 320 };
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int x0 = clampi(cx - CW / 2, 0, FW - CW) & ~1, y0 = clampi(cy - CH / 2, 0, FH - CH), wpr = FW / 2;
    unsigned char row[CW * 3];
    fprintf(f, "P6\n%d %d\n255\n", CW, CH);
    for (int y = y0; y < y0 + CH; y++) {
        for (int x = 0; x < CW; x++) {
            uint32_t q = p[(size_t)y * wpr + (x0 + x) / 2];
            int u = WU(q), v = WV(q), yy = ((x0 + x) & 1) ? WY1(q) : WY0(q);
            row[x * 3] = (unsigned char)clampi(yy + 1402 * v / 1000, 0, 255);
            row[x * 3 + 1] = (unsigned char)clampi(yy - (344 * u + 714 * v) / 1000, 0, 255);
            row[x * 3 + 2] = (unsigned char)clampi(yy + 1772 * u / 1000, 0, 255);
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}
static int ms_ncrop, ms_crop[4][2];
static void ms_dump(const char *stem, const uint32_t *p)
{
    char pth[48];
    if (!getenv("WF_FX_DUMP"))
        return;
    snprintf(pth, sizeof pth, "%s.ppm", stem);
    dump_half(pth, p, FW, FH);
    for (int k = 0; k < ms_ncrop; k++) {
        snprintf(pth, sizeof pth, "%s_c%d.ppm", stem, k);
        dump_crop(pth, p, ms_crop[k][0], ms_crop[k][1]);
    }
}
static void ms_ev(int ev8)                       /* the shot's EV100 x 8: lens_info's shutter for wf_shot_ev8 'M' (the host's
                                                 * shooting_mode 3), the LiveView frame's for 'F' */
{
    int sv = h_li_get("iso") ? h_li_get("iso") : h_li_get("iso_auto");
    h_li_set("shutter", clampi(ev8 - 8 - h_li_get("aperture") + sv, 8, 200));
    vp_bytes[2] = (uint8_t)clampi(ev8 - 8 - vp_bytes[1] + vp_bytes[0] + h_li_get("ae"), 8, 200);
}
extern int get_ms_clock(void);
/* the Darkroom's stage (wf_dark.c step 3) on a developed frame src -> dst, the CR2's EV ev8 (fr.dev_ev8): after a log
 * mark, its "yuv DARK" line */
static const char *dk_stage(const uint32_t *src, uint32_t *dst, int ev8)
{
    memcpy(dst, src, NW * 4);
    struct wf_frame fr;
    memset(&fr, 0, sizeof fr);
    fr.fmt = WF_FMT_UYVY8;
    fr.part = WF_PART_FULL;
    fr.devd = 1;
    fr.dev_ev8 = ev8;
    fr.w = fr.fw = FW;
    fr.h = fr.fh = FH;
    fr.pitch = FW * 2;
    fr.data = fr.dst = dst;
    fr.t_post = get_ms_clock();
    log_mark();
    int st = -1, tries = 0;
    while (wf_shot_run(&fr, &st) != 0 && ++tries < 100)
        usleep(100000);
    LOGW("yuv DARK", 5000);
    return log_line("yuv DARK");
}
/* the mist level (halation / bloom as set), after a log mark: its key, then a shot of src; the mist's seconds from the
 * shot line (" + mist 9.2", -1 none) */
static double ms_shot(int lv, const uint32_t *src, uint32_t *dst)
{
    log_mark();
    wf_mist = lv;
    chk(WAIT(lv ? wf_fx_mk == lv : !wf_fx_mk, 60000), "mist %d: key %d", lv, wf_fx_mk);
    shot(src, dst, FW, FH);
    LOGW("shot #", 5000);
    const char *s = strstr(log_line("shot #"), " + mist ");
    return s ? atof(s + 8) : -1;
}
/* Off, Mist 1/4, Bloom Normal + Halation Wide, all three on `in`: combo_<i> dumps; with g: the disc's near glow of each,
 * with hi: the share of its pixels at Y >= 250 */
static void ms_combo(double *g, double *hi)
{
    static const int M[4] = { 0, 2, 0, 2 }, HB[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; i++) {
        char st[16];
        wf_halo = HB[i];
        wf_bloom = HB[i] ? 2 : 0;
        chk(WAIT(HB[i] ? wf_fx_hk && WF_FXK_HALO(wf_fx_hk) == 1 && WF_FXK_BLOOM(wf_fx_hk) == 2 : !wf_fx_hk, 60000),
            "combo %d: halation-stage key %x", i, wf_fx_hk);
        ms_shot(M[i], in, out);
        printf("  combo %d: %s\n", i, log_line("shot #"));
        snprintf(st, sizeof st, "combo_%d", i);
        ms_dump(st, out);
        if (g) {
            g[i] = ring_at(ref, out, FW / 2, FH / 2, disc_r + 8, disc_r + 60, 'y');
            hi[i] = ring_hi(out, FW / 2, FH / 2, disc_r + 8, disc_r + 60, 250);
        }
    }
    wf_halo = wf_bloom = 0;
}
static void t_mist_levels(void)
{
    static const char *const nm[WF_MIST_MAX + 1] = { "off", "white 1/8", "white 1/4", "white 1/2", "white 1", "black 1/8",
                                                     "black 1/4", "black 1/2", "black 1" };
    static const char *const ex[WF_MIST_MAX + 1] = { "", " | Mist 1/8", " | Mist 1/4", " | Mist 1/2", " | Mist 1",
                                                     " | Black Mist 1/8", " | Black Mist 1/4", " | Black Mist 1/2", " | Black Mist 1" };
    const char *src = getenv("WF_FX_UYVY"), *crop = getenv("WF_FX_CROP"), *evs = getenv("WF_FX_EV8");
    int ev0 = 64;                                /* EV 8: a lamp-lit room, the hot ramp at 5/6 */
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nbloom=off\nmist=off\n", wf_films[fC].id);
    mod_init(1);
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    disc_r = 80;
    ms_ncrop = 0;
    for (const char *c = crop; c && *c && ms_ncrop < 4;) {
        if (sscanf(c, "%d,%d", &ms_crop[ms_ncrop][0], &ms_crop[ms_ncrop][1]) == 2)
            ms_ncrop++;
        if ((c = strchr(c, ';')))
            c++;
    }
    if (src) {                                   /* the harness strip on a photo */
        long n = 0;
        char *b = get_file(src, &n);
        int ok = b && n == (long)NW * 4;
        chk(ok, "the frame %s (%ld bytes)", src, n);
        if (ok)
            memcpy(in, b, NW * 4);
        free(b);
        if (ok) {
            ms_ev(evs ? atoi(evs) : ev0);
            chk(WAIT(look_table_ready(), 10000), "table published");
            printf("mist strip on %s, EV8 %d\n", src, evs ? atoi(evs) : ev0);
            for (int lv = 0; lv <= WF_MIST_MAX; lv++) {
                char st[16];
                ms_shot(lv, in, out);
                printf("  %s\n", log_line("shot #"));
                snprintf(st, sizeof st, "mist_%d", lv);
                ms_dump(st, out);
            }
            ms_combo(0, 0);
        }
        ms_ev(ev0);
        mod_deinit();
        return;
    }

    /* every level on the scene */
    ms_ev(ev0);
    mist_scene(in);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    chk(WAIT(look_table_ready(), 10000), "table published");
    ms_dump("mist_0", ref);
    const int cx = FW / 2, cy = FH / 2;
    double nr[WF_MIST_MAX + 1], wd[WF_MIST_MAX + 1], far[WF_MIST_MAX + 1], du[WF_MIST_MAX + 1], dv[WF_MIST_MAX + 1];
    double lu[WF_MIST_MAX + 1], lv_[WF_MIST_MAX + 1], gd[WF_MIST_MAX + 1], ch[WF_MIST_MAX + 1], cv[WF_MIST_MAX + 1];
    double sm[WF_MIST_MAX + 1], sec[WF_MIST_MAX + 1], fu[WF_MIST_MAX + 1], fv[WF_MIST_MAX + 1], nt[WF_MIST_MAX + 1];
    double nbt[WF_MIST_MAX + 1], lb[WF_MIST_MAX + 1];
    struct wf_look lk;
    char tb[200];
    wf_look_now(&lk);
    printf("  level        near (top / bottom)        wide    far (dU dV)  | disc dU   dV | lamp dU   dV | gaps | cells h  v |"
           " seam  lobe | mist s (qemu), EV8 %d\n", ev0);
    for (int lv = 1; lv <= WF_MIST_MAX; lv++) {
        char st[16];
        sec[lv] = ms_shot(lv, in, out);
        const char *sl = log_line("shot #");
        snprintf(tb, sizeof tb, "fx mist %s ready", nm[lv]);
        chk(log_has(tb), "%s: ready line '%s'", nm[lv], log_line("fx mist"));
        chk(strstr(sl, " + mist ") && !strstr(sl, "halation") && !strstr(sl, "bloom") && !strstr(sl, " + mist -"),
            "%s: shot line '%s'", nm[lv], sl);
        const char *t = wf_exif_text(&lk, 0, 0, 0, lv, WF_EV_NONE, tb, sizeof tb);
        chk(strstr(t, ex[lv]) && (lv > 4 || !strstr(t, "Black")), "%s: EXIF text '%s'", nm[lv], t);
        nr[lv] = ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 60, 'y');
        wd[lv] = ring_at(ref, out, cx, cy, disc_r + 300, disc_r + 700, 'y');
        far[lv] = corner(ref, out, FW, 0);
        corner_uv(ref, out, &fu[lv], &fv[lv]);
        nt[lv] = ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 60, 't');
        nbt[lv] = ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 60, 'b');
        du[lv] = ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 120, 'u');
        dv[lv] = ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 120, 'v');
        lu[lv] = ring_at(ref, out, MS_LX, cy, MS_LR + 6, MS_LR + 60, 'u');
        lv_[lv] = ring_at(ref, out, MS_LX, cy, MS_LR + 6, MS_LR + 60, 'v');
        gd[lv] = gap_dots(ref, out);
        ch[lv] = cell_pattern(ref, out, 0);
        cv[lv] = cell_pattern(ref, out, 1);
        sm[lv] = rows_dy(ref, out, FW, FH / 2 - 3, FH / 2 - 1) - rows_dy(ref, out, FW, FH / 2, FH / 2 + 2);
        lb[lv] = seam_lobe(ref, out, cx, cy);
        printf("  %-10s %+7.2f (%+7.2f / %+7.2f) %+6.2f %+5.2f (%+5.2f %+5.2f) | %+5.2f %+5.2f | %+6.2f %+5.2f | %+5.2f |"
               " %5.2f %5.2f | %+5.2f %+5.1f | %.1f\n", nm[lv], nr[lv], nt[lv], nbt[lv], wd[lv], far[lv], fu[lv], fv[lv], du[lv],
               dv[lv], lu[lv], lv_[lv], gd[lv], ch[lv], cv[lv], sm[lv], lb[lv], sec[lv]);
        snprintf(st, sizeof st, "mist_%d", lv);
        ms_dump(st, out);
        if (lv == 4)
            memcpy(out2, out, NW * 4);
    }
    for (int s = 1; s < 4; s++) {
        chk(nr[s + 1] > nr[s] && wd[s + 1] > wd[s], "white %s > %s: near %+.2f > %+.2f, wide %+.2f > %+.2f", nm[s + 1] + 6,
            nm[s] + 6, nr[s + 1], nr[s], wd[s + 1], wd[s]);
        chk(nr[s + 5] > nr[s + 4], "black %s > %s: near %+.2f > %+.2f", nm[s + 5] + 6, nm[s + 4] + 6, nr[s + 5], nr[s + 4]);
    }
    for (int s = 1; s <= 4; s++)
        chk(wd[s + 4] < wd[s], "black %s: less wide glow than white (%+.2f < %+.2f)", nm[s + 4] + 6, wd[s + 4], wd[s]);
    chk(nr[5] > 1.0, "black 1/8 is visible: near glow %+.2f", nr[5]);
    int bad_far = 0, bad_cast = 0, bad_lamp = 0, bad_cell = 0, bad_seam = 0, bad_dark = 0, bad_lobe = 0;
    double wlb = 0;
    (void)bad_seam;
    for (int lv = 1; lv <= WF_MIST_MAX; lv++) {
        bad_lobe += fabs(lb[lv]) >= 3.0;
        if (fabs(lb[lv]) > fabs(wlb))
            wlb = lb[lv];
        bad_far += fabs(far[lv]) >= 1.0;
        bad_dark += fabs(fu[lv]) >= 0.2 || fabs(fv[lv]) >= 0.2;   /* 11-15: -0.5 / -1.0 in U and V, rounded down */
        bad_cast += fabs(du[lv]) >= 1.0 || fabs(dv[lv]) >= 1.0;
        bad_lamp += !(lv_[lv] > 0.2 && lu[lv] < 0);
        bad_cell += ch[lv] >= 0.5 || cv[lv] >= 0.5;
#ifdef WF_BUILD_5D2
        bad_seam += fabs(sm[lv]) >= 1.5;
#endif
    }
    chk(!bad_far, "the dark far from any light stays (%d levels moved it 1 code or more)", bad_far);
    chk(!bad_dark, "the dark far from any light keeps its colour (%d levels moved U or V 0.2 code or more)", bad_dark);
    chk(!bad_cast, "no colour cast round the white light (%d levels with |dU| or |dV| >= 1)", bad_cast);
    chk(!bad_lamp, "the orange lamp's glow is orange: V up, U down (%d levels not)", bad_lamp);
    chk(!bad_cell, "no 16 px cell pattern in the glow (%d levels at 0.5 code or more)", bad_cell);
#ifdef WF_BUILD_5D2
    chk(!bad_seam, "no step at the TOP / BOT seam (%d levels at 1.5 codes or more)", bad_seam);
    chk(!bad_lobe, "the light on the TOP / BOT seam glows alike above and below (%d levels at 3 codes or more, worst %+.1f)",
        bad_lobe, wlb);
#else
    chk(!bad_lobe, "whole frame: the light's glow alike above and below (%d levels at 3 codes or more, worst %+.1f)", bad_lobe,
        wlb);
    /* Diana halves at white 1 and black 1: the TOP has no rows below the seam (it mirrors its own, a light only where it
     * reaches the seam) and the BOT meets it there; a light on the seam: no step, no dark lobes under it, the whole frame's
     * glow (until 11-15: -0.55 code over the frame, near glow top / bottom 120 / 109 against 133 at white 1) */
    for (int lv = 4; lv <= 8; lv += 4) {
        if (lv == 8)
            ms_shot(lv, in, out2);               /* the whole frame's black 1 (out2 holds white 1) */
        halves = 1;
        ms_shot(lv, in, out);
        halves = 0;
        double hm = mean_dy(out2, out, NW), hl = seam_lobe(ref, out, cx, cy);
        double sa = rows_dy(ref, out, FW, FH / 2 - 3, FH / 2 - 1), sb = rows_dy(ref, out, FW, FH / 2, FH / 2 + 2);
        chk(LOGW("(TOP ", 3000) && fabs(hm) < 0.5 && fabs(sa - sb) < 1.5 && fabs(hl) < 3.0, "%s, Diana halves: vs whole frame "
            "dY %+.3f, seam rows above %+.2f, below %+.2f, lobe %+.1f ('%s')", nm[lv], hm, sa, sb, hl, log_line("shot #"));
        printf("  %s, halves: near glow top %+.2f / bottom %+.2f (whole frame %+.2f / %+.2f)\n", nm[lv],
               ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 60, 't'), ring_at(ref, out, cx, cy, disc_r + 8, disc_r + 60, 'b'),
               nt[lv], nbt[lv]);
        snprintf(tb, sizeof tb, "halves_%d", lv);
        ms_dump(tb, out);
    }
    /* a light on the seam, just above it (no phantom of it below) and further up: halves against the whole frame (eroded
     * mirror until 11-15: -32 / -64, +33 / +41, +20 / +18 at white / black 1). The horizon: no light, the sky mirrors as sky
     * and the BOT's pull lays a skirt over the ground below the seam, the same before and after (a limit of the halves) */
    {
        static const int OY[3] = { 0, -104, -200 };
        double se[2][3];
        uint32_t *keep = malloc(NW * 4);
        memcpy(keep, in, NW * 4);
        double sh[2];                            /* and a horizon on the seam (sky above, shadow below): no light, mirrored */
        for (int i = 0; i < 4; i++) {
            if (i < 3)
                seam_scene(in, OY[i]);
            else
                sky_scene(in);
            for (int k = 0; k < 2; k++) {
                ms_shot(k ? 8 : 4, in, out2);
                halves = 1;
                ms_shot(k ? 8 : 4, in, out);
                halves = 0;
                *(i < 3 ? &se[k][i] : &sh[k]) = seam_err(out2, out);
            }
        }
        printf("  halves - whole round the seam (worst 16 px box): light on it white 1 %+.1f black 1 %+.1f; 24 px above it %+.1f"
               " %+.1f; 120 px above %+.1f %+.1f; horizon %+.1f %+.1f\n", se[0][0], se[1][0], se[0][1], se[1][1], se[0][2],
               se[1][2], sh[0], sh[1]);
        int bad = 0;
        for (int i = 0; i < 3; i++)
            bad += fabs(se[0][i]) >= 4.0 || fabs(se[1][i]) >= 4.0;
        chk(!bad, "a light on / above the seam: halves within 4 codes of the whole frame (%d cases not)", bad);
        chk(sh[0] < 45 && sh[1] < 45, "the horizon's skirt no worse than before 11-15 (+41.5 / +42.6): %+.1f / %+.1f", sh[0], sh[1]);
        memcpy(in, keep, NW * 4);
        free(keep);
    }
#endif

    /* the ML menu entry */
    uint8_t *me = menu_named("Mist");
    const char *hp = me ? *(const char **)(me + wf_t_me_help) : 0;
    chk(me && hp && strstr(hp, "Diffusion"), "ML menu: a 'Mist' entry, help '%s'", hp ? hp : "");
    wf_mist = WF_MIST_MAX;
    chk(menu_set("Mist") == 0 && wf_mist == 0, "ML menu Mist: SET on Black 1 wraps to Off (%d)", wf_mist);
    chk(menu_set("Mist") == 0 && wf_mist == 1 && !strcmp(wf_row_text(WF_ROW_MIST), "1/8"), "ML menu Mist: then '%s'",
        wf_row_text(WF_ROW_MIST));
    chk(wf_row_avail(WF_ROW_MIST) == 1, "the Mist row is available for a film");

    /* the leaves' sky gaps at white 1: daylight (EV 15: a gap is sky, barely over the clip) and night (EV 5: a gap is a lamp) */
    ms_ev(15 * 8);
    ms_shot(4, in, out);
    chk(strstr(log_line("shot #"), " EV15") != 0, "the test sets the shot's EV: '%s'", log_line("shot #"));
    double gday = gap_dots(ref, out);
    ms_ev(5 * 8);
    ms_shot(4, in, out);
    double gnight = gap_dots(ref, out), lnight = ring_at(ref, out, MS_LX, cy, MS_LR + 6, MS_LR + 60, 'y');
    ms_ev(ev0);
    printf("  white 1 gaps: EV15 %+.2f, EV%d %+.2f, EV5 %+.2f (lamp ring at EV5 %+.2f)\n", gday, ev0 / 8, gd[4], gnight, lnight);
    chk(gday < 2.0, "white 1 in daylight: no glowing dots in the leaves' sky gaps (%+.2f codes round a gap)", gday);
    chk(gnight > gday, "at night a gap glows more than in daylight (%+.2f > %+.2f)", gnight, gday);

    /* a Darkroom print: the effects' EV is the CR2's own (fr.dev_ev8), not the dials' at develop time (EV5 here) */
    {
        uint32_t *dref = malloc(NW * 4);
        ms_ev(5 * 8);
        wf_mist = 0;
        chk(WAIT(!wf_fx_mk, 60000), "darkroom: mist off, key %d", wf_fx_mk);
        dk_stage(in, dref, 15 * 8);
        wf_mist = 4;
        chk(WAIT(wf_fx_mk == 4, 60000), "darkroom: mist white 1, key %d", wf_fx_mk);
        const char *dl = dk_stage(in, out, 15 * 8);
        chk(strstr(dl, " ev15R") != 0, "darkroom, CR2 EV15 under dials at EV5: '%s'", dl);
        double dday = gap_dots(dref, out);
        dl = dk_stage(in, out, 5 * 8);
        chk(strstr(dl, " ev5R") != 0, "darkroom, CR2 EV5: '%s'", dl);
        double dnight = gap_dots(dref, out);
        dl = dk_stage(in, out, WF_EV_BAD);
        chk(strstr(dl, " ev?") != 0, "darkroom, a CR2 without exposure tags: '%s'", dl);
        ms_ev(ev0);
        printf("  darkroom white 1 gaps, dials at EV5: CR2 EV15 %+.2f, CR2 EV5 %+.2f\n", dday, dnight);
        chk(dday < 2.0, "darkroom, a daylight CR2 under night dials: no glowing dots (%+.2f)", dday);
        chk(dnight > dday, "darkroom: the CR2's night glows more than its day (%+.2f > %+.2f)", dnight, dday);
        free(dref);
    }

    /* Mist 1/4 + Bloom Normal + Halation Wide: the glows add, no white wash */
    double g[4], hi[4];
    ms_combo(g, hi);
    printf("  combo near glow: off %+.2f, mist 1/4 %+.2f, bloom+halation %+.2f, all %+.2f; Y >= 250: %.3f %.3f %.3f %.3f\n", g[0],
           g[1], g[2], g[3], hi[0], hi[1], hi[2], hi[3]);
    chk(g[3] > g[1] && g[3] > g[2] && g[3] < g[1] + g[2] + 2.0, "the glows add: %+.2f between max(%+.2f, %+.2f) and their sum",
        g[3], g[1], g[2]);
    chk(hi[3] < 0.05, "no white wash round the light: %.1f %% of the near ring at Y >= 250", 100 * hi[3]);

    /* a sky over a shadow at white 1 and black 1: how far the shadows lift */
    sky_scene(in);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fC][0], flags_of(fC, 1));
    double sn[2], sf[2], sk[2];
    for (int k = 0; k < 2; k++) {
        ms_shot(k ? 8 : 4, in, out);
        sk[k] = rows_dy(ref, out, FW, FH / 2 - 500, FH / 2 - 200);
        sn[k] = rows_dy(ref, out, FW, FH / 2 + 200, FH / 2 + 500);
        sf[k] = rows_dy(ref, out, FW, FH - 400, FH - 100);
        snprintf(tb, sizeof tb, "sky_%d", k);
        ms_dump(tb, out);
    }
    double s0 = rows_dy(in, ref, FW, FH - 400, FH - 100) + 30, k0 = rows_dy(in, ref, FW, 100, 400) + 232;
    printf("  sky / shadow (film: shadow ~%.0f, sky ~%.0f): white 1 shadow near %+.2f far %+.2f sky %+.2f; black 1 near %+.2f far "
           "%+.2f sky %+.2f\n", s0, k0, sn[0], sf[0], sk[0], sn[1], sf[1], sk[1]);
    chk(sn[1] < sn[0] && sf[1] < sf[0], "black 1 lifts the shadows less than white 1 (%+.2f < %+.2f)", sn[1], sn[0]);
    chk(sn[0] < 40.0, "white 1: the shadow by the sky lifts %+.2f codes (under 40)", sn[0]);

    /* the shot line with every effect on, halves: it keeps its EV (5D2 RAWB11 note: it lost it past 110 characters) */
    mist_scene(in);
    wf_mist = 2;
    wf_halo = 1;
    wf_bloom = 3;
    wf_grain = WF_GRAIN_CAN ? 2 : 0;
    wf_hl = 3;
    wf_sh = -2;
    chk(WAIT(wf_fx_mk == 2 && wf_fx_hk && WF_FXK_BLOOM(wf_fx_hk) == 3 && (!WF_GRAIN_CAN || wf_fx_gk), 120000)
        && WAIT(look_table_ready(), 10000), "every effect ready");
    halves = 1;
    log_mark();
    shot(in, out, FW, FH);
    halves = 0;
    LOGW("shot #", 5000);
    usleep(300000);
    const char *l1 = log_line(" s = look "), *l2 = log_line("shot #");
    char all[400];
    snprintf(all, sizeof all, "%s%s", l1, l1 == l2 || !strcmp(l1, l2) ? "" : l2);
    printf("  long line: '%s'\n             '%s'\n", l1, l2);
    chk(strstr(all, " + mist ") && strstr(all, " + halation+bloom strong ") && (!WF_GRAIN_CAN || strstr(all, " + grain "))
        && strstr(all, "(TOP ") && strstr(all, " EV"), "the shot line keeps every part and its EV: '%s' / '%s'", l1, l2);
    chk(strlen(l1) <= 120 && strlen(l2) <= 120, "each log line fits (%d, %d characters)", (int)strlen(l1), (int)strlen(l2));
    wf_mist = wf_halo = wf_bloom = wf_grain = 0;
    wf_hl = wf_sh = 0;
    chk(WAIT(!wf_fx_mk && !wf_fx_hk && !wf_fx_gk && h_arena_live == 0, 30000), "all off: memory given back (%d live)",
        h_arena_live);
    mod_deinit();
}

/* ------------------------------------------------------------------ LOOK:sweep (harness only, tools/look_sweep.sh)
 * The real pack (WF_LOOK_PACK = a build's WFFILMS.DAT) and a job file (WF_LOOK_JOBS), one shot per line:
 *   <frame.uyvy> <ev8> <film id> <print 0..18> <strength 0..2> <grain 0..6> <halation 0..3> <bloom 0..3> <mist 0..8>
 *   <gauge 0..2> <out.ppm> <c|d> [X,Y]  (also <out>_c.ppm: 960 x 640 at 1:1 round X, Y)
 * The print is the variant (a chain film's print stock: WF_PRINT_STOCK0 + n); a film without it falls back as the
 * camera does (wf_variant_eff) and the job fails as not ready.
 * c = the camera's hook (the in-camera path), d = the Darkroom's look stage (wf_shot_run with devd, as wf_dark.c). The
 * frames are the Darkroom's develop of the owner's CR2s (wf_dark_test devset: sRGB for the films, each Film LUT can's
 * own cube for that can), outside the repository; the output a third-size PPM (3 x 3 box). Film id 0 = the frame as it
 * is (the neutral develop, through the same third-size and 1:1 dumps). WF_LOOK_CUBES = a card's ML/DATA: its cubes are
 * linked into this card so a can loads (wf_load wants its cube on the card; the stage itself uses the develop). An
 * <out> ending in .uyvy is the full frame as it is (5184 x 3456 UYVY8: the print finishes' input). Prints
 * "look <out> | <log line> | exif <the look text the print's comment gets>" per job. Not in test.sh's list: it needs
 * the env. */
static void dump_third(const char *path, const uint32_t *p)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int ow = FW / 3, oh = FH / 3;
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    unsigned char *row = malloc((size_t)ow * 3);
    for (int oy = 0; row && oy < oh; oy++) {
        for (int ox = 0; ox < ow; ox++) {
            int sy = 0, su = 0, sv = 0;
            for (int y = 3 * oy; y < 3 * oy + 3; y++)
                for (int x = 3 * ox; x < 3 * ox + 3; x++) {
                    uint32_t q = p[(size_t)y * (FW / 2) + x / 2];
                    sy += (int)(x & 1 ? q >> 24 : (q >> 8) & 255);
                    su += (int8_t)(q & 255);
                    sv += (int8_t)((q >> 16) & 255);
                }
            int c[3] = { 1000 * sy + 1402 * sv, 1000 * sy - 344 * su - 714 * sv, 1000 * sy + 1772 * su };
            for (int k = 0; k < 3; k++)
                row[3 * ox + k] = (unsigned char)clampi((c[k] + 4500) / 9000, 0, 255);
        }
        fwrite(row, 1, (size_t)ow * 3, f);
    }
    free(row);
    fclose(f);
}
static void dump_crop1(const char *path, const uint32_t *p, int cx, int cy)   /* 960 x 640 at 1:1 round cx, cy */
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int w = 960, h = 640, x0 = clampi(cx - w / 2, 0, FW - w) & ~1, y0 = clampi(cy - h / 2, 0, FH - h);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    unsigned char *row = malloc((size_t)w * 3);
    for (int y = 0; row && y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t q = p[(size_t)(y0 + y) * (FW / 2) + (x0 + x) / 2];
            int yy = (int)(x & 1 ? q >> 24 : (q >> 8) & 255), u = (int8_t)(q & 255), v = (int8_t)((q >> 16) & 255);
            row[3 * x] = (unsigned char)clampi(yy + (1402 * v + 500) / 1000, 0, 255);
            row[3 * x + 1] = (unsigned char)clampi(yy - (344 * u + 714 * v) / 1000, 0, 255);
            row[3 * x + 2] = (unsigned char)clampi(yy + (1772 * u + 500) / 1000, 0, 255);
        }
        fwrite(row, 1, (size_t)w * 3, f);
    }
    free(row);
    fclose(f);
}
static void t_look_sweep(void)
{
    const char *pk = getenv("WF_LOOK_PACK"), *jobs = getenv("WF_LOOK_JOBS");
    long n = 0;
    char *b = pk ? get_file(pk, &n) : 0;
    chk(b && jobs, "WF_LOOK_PACK %s (%ld B), WF_LOOK_JOBS %s", pk ? pk : "-", n, jobs ? jobs : "-");
    if (!b || !jobs) {
        free(b);
        return;
    }
    put_file("ML/DATA/WFFILMS.DAT", b, n);
    free(b);
    const char *stp = getenv("WF_LOOK_STAGE"), *xcfg = getenv("WF_LOOK_CFG");   /* the knob rows: their pack + config */
    if (stp) {
        b = get_file(stp, &n);
        chk(b != 0, "WF_LOOK_STAGE %s (%ld B)", stp, n);
        if (b)
            put_file("ML/DATA/WFSTAGE.DAT", b, n);
        free(b);
    }
    put_cfg("film=26\noutput=film\nprint=scan\nstrength=normal\ngrain=off\nhalation=off\nbloom=off\nmist=off\n%s\n",
            xcfg ? xcfg : "");
    const char *cubes = getenv("WF_LOOK_CUBES");
    if (cubes) {
        DIR *dd = opendir(cubes);
        struct dirent *de;
        int nc = 0;
        while (dd && (de = readdir(dd))) {
            size_t l = strlen(de->d_name);
            if (l < 5 || (strcmp(de->d_name + l - 4, ".CUB") && strcmp(de->d_name + l - 4, ".cub")))
                continue;
            char sp[800], dp[300];
            snprintf(sp, sizeof sp, "%s/%s", cubes, de->d_name);
            snprintf(dp, sizeof dp, "ML/DATA/%s", de->d_name);
            nc += link(sp, dp) == 0 || symlink(sp, dp) == 0;
        }
        if (dd)
            closedir(dd);
        chk(nc > 0, "WF_LOOK_CUBES %s: %d cubes linked", cubes, nc);
    }
    wf_fx_retry_ms = 500;
    mod_init(1);
#ifndef WF_BUILD_5D2
    h_prop_send(PROP_ASPECT, 0);
#endif
    FILE *f = fopen(jobs, "r");
    chk(f != 0, "open %s", jobs);
    char ln[1200], cur[512] = "";
    int nj = 0, bad = 0;
    while (f && fgets(ln, sizeof ln, f)) {
        char fp[512], op[512], pth[8];
        int ev8, id, pr, sg, gr, ha, bl, mi, ga, ccx = -1, ccy = -1;
        int nf = sscanf(ln, "%511s %d %d %d %d %d %d %d %d %d %511s %7s %d,%d", fp, &ev8, &id, &pr, &sg, &gr, &ha, &bl, &mi,
                        &ga, op, pth, &ccx, &ccy);
        if (nf != 12 && nf != 14)
            continue;
        if (strcmp(fp, cur)) {
            char *fb = get_file(fp, &n);
            int ok = fb && n == (long)NW * 4;
            chk(ok, "the frame %s (%ld bytes)", fp, n);
            if (ok)
                memcpy(in, fb, NW * 4);
            free(fb);
            if (!ok) {
                cur[0] = 0;
                continue;
            }
            snprintf(cur, sizeof cur, "%s", fp);
        }
        if (id == 0) {                           /* the neutral develop as it is: no look, no stage */
            memcpy(out, in, NW * 4);
            printf("look %s | neutral develop\n", op);
            goto dump;
        }
        ms_ev(ev8);
        int lr = wf_loaded() == id ? WF_LOAD_OK : wf_load(id);
        wf_print = pr;
        wf_strength = sg;
        wf_grain = gr;
        wf_halo = ha;
        wf_bloom = bl;
        wf_mist = mi;
        wf_gauge = ga;
        struct wf_look lk;
        int hk = 0, gk = 0, mk;
        wf_look_now(&lk);
        wf_fx_keys(lk.post, &hk, &gk);
        mk = wf_fx_mkey(lk.post);
        int rdy = lr == WF_LOAD_OK && WAIT(look_table_ready(), 20000)
               && WAIT(wf_fx_hk == hk && wf_fx_gk == gk && (wf_fx_mk & 15) == mk, 120000);
        wf_look_now(&lk);
        if (!rdy || lk.row < 0 || wf_films[lk.row].id != id || lk.variant != pr) {
            chk(0, "look %s: film %d print %d not ready (load %d, row %d variant %d, keys %x/%x %x/%x %x/%x)", op, id, pr, lr,
                lk.row, lk.variant, wf_fx_hk, hk, wf_fx_gk, gk, wf_fx_mk, mk);
            bad++;
            continue;
        }
        log_mark();
        char et[200];
        wf_exif_text(&lk, lk.grain, lk.halo, lk.bloom, lk.mist, WF_EV_NONE, et, sizeof et);
        if (pth[0] == 'd') {                     /* the Darkroom's stage on the developed frame, the frame's EV */
            printf("look %s | %s | exif %s\n", op, dk_stage(in, out, ev8), et);
        } else {
            shot(in, out, FW, FH);
            LOGW("shot #", 5000);
            printf("look %s | %s | exif %s\n", op, log_line("shot #"), et);
        }
    dump:
        if (strlen(op) > 5 && !strcmp(op + strlen(op) - 5, ".uyvy")) {
            FILE *uf = fopen(op, "wb");          /* the full frame (the print finishes' input) */
            chk(uf && fwrite(out, 4, NW, uf) == (size_t)NW, "write %s", op);
            if (uf)
                fclose(uf);
            nj++;
            continue;
        }
        dump_third(op, out);
        if (nf == 14) {                          /* "X,Y": also <out>_c.ppm, 960 x 640 at 1:1 (grain, halation edges) */
            char cp[520];
            snprintf(cp, sizeof cp, "%.*s_c.ppm", (int)(strlen(op) > 4 ? strlen(op) - 4 : 0), op);
            dump_crop1(cp, out, ccx, ccy);
        }
        nj++;
    }
    if (f)
        fclose(f);
    chk(bad == 0, "look sweep: %d shots, %d not ready", nj, bad);
    wf_mist = wf_halo = wf_bloom = wf_grain = 0;
    wf_gauge = WF_GAUGE_DEF;
    mod_deinit();
}

/* ------------------------------------------------------------------ CFG */
static void t_cfg(void)
{
    std_pack();
    const char *code1 = wf_flut_code[1];
    put_cfg("# comment\nfilm=%d\n output = LUT\ncurve=CLOG2\nflut=%s\nlutei=-2\nincurve=FILM1\nprint=CINE\nlight=tungsten\nstrength=strong\n"
            "hl=3\nsh= -2\ngrain=fine\nhalation=spkbloom\nmist=b2\npreview=fast\nexposure=smooth\nexptrim=-5\nexif=off\ninput=jpeg\nbogus=1\n"
            "blind=OFF\nframe=BlackWide\ninfo=roll\ndate=DMY\njpeg=PrintOver\njpeg2=Folder\ndkscreen=Off\ndkphoto=Off\n"
            "rolllook=ON\nshoulder=D\n"
            "qkeys=LR\nos=OFF\nfinder=Lines\nplate=OFF\nplay=Prints\ncr2=Move\nuserwb=1,5600,2,-3\n",
            wf_films[fA].id, code1);
    mod_init(1);
    chk(wf_film_id == wf_films[fA].id && wf_out == WF_OUT_LLUT && wf_curve == 4 && wf_flut == 1 && wf_lut_ei == -2,
        "film %d output %d curve %d flut %d lutei %d", wf_film_id, wf_out, wf_curve, wf_flut, wf_lut_ei);
    chk(wf_incurve == WF_INC_FILM1 && wf_incurve_t() == (wf_ll_curve_have(WF_INC_FILM1) ? WF_INC_FILM1 : WF_INC_SRGB),
        "incurve %d (in use %d)", wf_incurve, wf_incurve_t());
    chk(wf_print == 1 && wf_light == WF_LIGHT_TUNGSTEN && wf_strength == 2 && wf_grain == 1 && wf_halo == 2 && wf_bloom == 2 && wf_mist == 7
        && wf_pv_mode == 2 && wf_gauge == WF_GAUGE_DEF, "print %d light %d strength %d grain %d halation %d bloom %d mist %d "
        "preview %d", wf_print, wf_light, wf_strength, wf_grain, wf_halo, wf_bloom, wf_mist, wf_pv_mode);
    chk(wf_hl == 3 && wf_sh == -2, "hl %d sh %d", wf_hl, wf_sh);
    chk(wf_exp_mode == (WF_EXP_CAN ? WF_EXP_SMOOTH : WF_EXP_CANON) && wf_exp_trim == -5 && wf_exif_off == 1,
        "exposure %d exptrim %d exif off %d", wf_exp_mode, wf_exp_trim, wf_exif_off);
    chk(wf_blind == 0, "blind=OFF read: Blind roll off (%d)", wf_blind);
    chk(wf_roll_look8 == 1, "rolllook=ON read: a JPEG 8-bit roll (%d)", wf_roll_look8);
    chk(wf_rb_hue == WF_DEV_HUE_D, "shoulder=D read: the chain films' shoulder D (%d)", wf_rb_hue);
    chk(wf_qk == WF_QK_LR, "qkeys=LR read: Quick keys L/R ISO (%d)", wf_qk);
    chk(wf_os == 0, "os=OFF read: Camera interface Canon + ML (%d)", wf_os);
    chk(wf_vf == WF_VF_LINES, "finder=Lines read: the LiveView finder Bright lines (%d)", wf_vf);
    chk(wf_plate == 0, "plate=OFF read: the Top plate off (%d)", wf_plate);
    chk(wf_playv == 1, "play=Prints read: PLAY shows Prints (%d)", wf_playv);
    chk(wf_pr_cr2 == WF_PC_MOVE, "cr2=Move read: RAW after Darkroom Move to ROLLS (%d)", wf_pr_cr2);
    chk(wf_pr_frame == 4 && wf_pr_info == 2 && wf_pr_date == 2 && wf_pr_jpeg == WF_PJ_PRINT && wf_pr_jpg2 == WF_J2_FOLDER
        && wf_dk_scr == WF_DS_OFF, "print finishes read: frame %d info %d date %d jpeg %d jpeg2 %d dkscreen %d (want 4 2 2 "
        "3 1 1)", wf_pr_frame, wf_pr_info, wf_pr_date, wf_pr_jpeg, wf_pr_jpg2, wf_dk_scr);
    chk(wf_dk_photo == 0, "dkphoto=Off read: the Darkroom's photos off (%d)", wf_dk_photo);
    chk(WAIT(h_kelvin_last == 3200, 5000), "Light Tungsten applied at start (%dK)", h_kelvin_last);
    wf_row_step(WF_ROW_GRAIN, 1);
    h_clock_add_ms += 2000;
    usleep(300000);
    long n;
    char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    char want[700];
    snprintf(want, sizeof want, "input=jpeg\nfilm=%d\noutput=lut\ncurve=clog2\nflut=%s\nlutei=-2\nincurve=film1\nprint=cine\nlight=tungsten\n"
             "strength=strong\nhl=3\nsh=-2\npaper=glossy\ngrain=normal\nhalation=spk\nbloom=normal\ngauge=35\nmist=b2\npreview=fast\n"
             "exposure=%s\n"
             "exptrim=-5\nexif=off\nblind=off\nframe=blackwide\ninfo=roll\ndate=dmy\njpeg=printover\njpeg2=folder\ncr2=move\n"
             "dkscreen=off\n"
             "dkphoto=off\n"
             "rolllook=on\n"
             "shoulder=d\n"
             "qkeys=lr\n"
             "os=off\n"
             "finder=lines\n"
             "plate=off\n"
             "play=prints\n"
             "stage=0,0,0,0,0,0,2,0,0,0,0\n"
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
    wf_qk = WF_QK_DOWN;
    wf_os = 1;                                   /* the default: Camera interface WindyFilm */
    wf_vf = WF_VF_FULL;                          /* the default: the LiveView finder Full */
    wf_pr_cr2 = WF_PC_KEEP;
    wf_roll_look8 = 0;                           /* the other checks: a roll keeps Canon's JPEG */
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
    /* RAWB11: Bloom split from Halation. An old file's halation word carries its bloom; a bloom= line wins */
    static const struct { const char *cfg; int h, b; } mig[] = {
        { "halation=bloom\n", 1, 2 },            { "halation=spkbloom\n", 2, 2 },        { "halation=halation\n", 1, 0 },
        { "halation=spk\n", 2, 0 },              { "halation=off\n", 0, 0 },             { "bloom=low\nhalation=spkbloom\n", 2, 1 },
        { "halation=halation\nbloom=strong\n", 1, 3 }, { "halation=off\nbloom=normal\n", 0, 2 },
    };
    for (int i = 0; i < (int)(sizeof mig / sizeof *mig); i++) {
        put_cfg("%s", mig[i].cfg);
        wf_cfg_load();
        chk(wf_halo == mig[i].h && wf_bloom == mig[i].b, "config '%.*s': halation %d bloom %d (want %d %d)",
            (int)strlen(mig[i].cfg) - 1, mig[i].cfg, wf_halo, wf_bloom, mig[i].h, mig[i].b);
    }
    put_cfg("halation=bogus\nbloom=bogus\n");
    wf_cfg_load();
    chk(wf_halo == WF_HALO_DEF && wf_bloom == WF_BLOOM_DEF, "unknown words: the defaults (%d %d)", wf_halo, wf_bloom);
    /* Mist: every word, unchanged since the first Mist commit (4d10a0b), reads its level; an unknown word is Off */
    static const char *const mw[] = { "off", "w8", "w4", "w2", "w1", "b8", "b4", "b2", "b1" };
    int mbad = 0;
    for (int i = 0; i <= WF_MIST_MAX; i++) {
        put_cfg("mist=%s\n", mw[i]);
        wf_cfg_load();
        mbad += wf_mist != (WF_MIST_CAN ? i : 0);
    }
    put_cfg("mist=b2\n");
    wf_cfg_load();
    put_cfg("mist=bogus\n");
    wf_cfg_load();
    chk(!mbad && wf_mist == 0, "config mist=off..b1 read as levels 0..8 (%d wrong), mist=bogus is Off (%d)", mbad, wf_mist);
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
    chk(!strcmp(wf_row_text(WF_ROW_LIGHT), "Auto"), "Light: '%s' (owner 11-17: Auto / Day / Cloudy / Shade / Tungsten; "
        "on the EOS M Auto = Canon's Auto WB in the Darkroom)", wf_row_text(WF_ROW_LIGHT));
    int day = wf_films[kd].kelvin, cloudy = (day * 5 / 4 + 50) / 100 * 100, shade = (day * 29 / 20 + 50) / 100 * 100;
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == day, 3000) && h_li_get("wb_mode") == 9 && !strcmp(wf_row_text(WF_ROW_LIGHT), "Day"),
        "Day: %dK from the film (%s)", h_kelvin_last, wf_films[kd].code);
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == cloudy, 3000), "Cloudy: %dK (want %d)", h_kelvin_last, cloudy);
    wf_row_step(WF_ROW_LIGHT, 1);
    chk(WAIT(h_kelvin_last == shade, 3000) && !strcmp(wf_row_text(WF_ROW_LIGHT), "Shade"), "Shade: %dK (want %d, '%s')",
        h_kelvin_last, shade, wf_row_text(WF_ROW_LIGHT));
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

/* ------------------------------------------------------------------ the Darkroom's camera
 * Its RAW+L pairs (WF_DARK_CR2: a landscape and two portraits, Orientation 6 / 8, Canon's L JPEG beside each CR2; read
 * only, never committed): EOS M the Day09 pairs, 5D2 the owner's 100EOS5D pairs (11-17, ISO 200 daylight). The CR2's raw
 * block (ML packed 14-bit, pitch x rows), the camera's SRM chunk (srm_malloc_suite, host_ml.c H_SRM_SZ), the model the
 * logo's lettering names. */
#ifdef WF_BUILD_5D2
#define DK_T_DIR   "/mnt/f/MagicLatern/_test mau film/5D2/100EOS5D"
#define DK_T_CAM   "5D2"
#define DK_T_RAW   (10136u * 3804u)
#define DK_T_SRMK  "+38720K"
#define DK_T_MODEL "Canon EOS 5D Mark II"
#define DK_T_SHOOT (7 << 20)                     /* ROLL:mem's biggest shoot block: halation's 6.7 MB fits, the 7.5 MB head not */
static const char *const mem_pairs[3] = { "IMG_8380", "IMG_8419", "IMG_8381" };   /* landscape, Orientation 6, 8 */
#else
#define DK_T_DIR   "/mnt/f/MagicLatern/_research/raw-path/cr2/Day09_EOSM"
#define DK_T_CAM   "EOS M"
#define DK_T_RAW   (9240u * 3528u)
#define DK_T_SRMK  "+31888K"
#define DK_T_MODEL "Canon EOS M"
#define DK_T_SHOOT (6 << 20)                     /* ROLL:mem's biggest shoot block: halation's 5.7 MB fits, the 7 MB head not */
static const char *const mem_pairs[3] = { "IMG_0208", "IMG_0206", "IMG_0212" };   /* landscape, Orientation 6, 8 */
#endif
static const char *dk_t_dir(void)                /* the pairs' folder, "" = none here (the modes that need it skip) */
{
    static char t[300];
    const char *dir = getenv("WF_DARK_CR2");
    if (!dir)
        dir = DK_T_DIR;
    snprintf(t, sizeof t, "%s/%s.CR2", dir, mem_pairs[2]);
    return access(t, R_OK) == 0 ? dir : "";
}

/* ------------------------------------------------------------------ LIGHT:awb */
/* Light Auto = Auto WB (owner 11-17): the Darkroom develops a CR2 with Canon's Auto WB levels of the shot (ColorData
 * short 68..71), whatever WB was set on the camera (the as-shot levels, 63..66); Day / Cloudy / Tungsten keep the as-shot
 * ones (the camera was set to them at the shot); a CR2 without Auto levels: as shot. The camera's WB is never touched. */
/* a CR2 head (little endian) down to ColorData: IFD0 -> EXIF (0x8769) -> MakerNote (0x927C) -> ColorData (0x4001, cnt
 * shorts at 96), no raw IFD (the parse returns "no raw strip" after the WB fields). Returns the size */
static uint32_t awb_head(uint8_t *b, uint32_t cnt, const int *as, const int *au)
{
    uint32_t n = 96 + 2 * cnt;
    memset(b, 0, n);
    #define W16(o, v) do { b[o] = (uint8_t)(v); b[(o) + 1] = (uint8_t)((v) >> 8); } while (0)
    #define W32(o, v) do { W16(o, (v) & 0xFFFF); W16((o) + 2, (uint32_t)(v) >> 16); } while (0)
    b[0] = b[1] = 'I'; W16(2, 42); W32(4, 16); b[8] = 'C'; b[9] = 'R'; b[10] = 2;
    W16(16, 1); W16(18, 0x8769); W16(20, 4); W32(22, 1); W32(26, 40);            /* IFD0: the EXIF IFD at 40 */
    W16(40, 1); W16(42, 0x927C); W16(44, 7); W32(46, 2); W32(50, 64);           /* EXIF: the MakerNote at 64 */
    W16(64, 1); W16(66, 0x4001); W16(68, 3); W32(70, cnt); W32(74, 96);          /* MakerNote: ColorData at 96 */
    W16(96, 10);                                                                 /* ColorData version (the EOS M: 10) */
    int idx = cnt == 582 ? 25 : 63;
    for (int c = 0; c < 4; c++) {
        W16(96 + 2 * (idx + c), as[c]);
        if (au && idx == 63) W16(96 + 2 * (68 + c), au[c]);
    }
    if (idx == 63) { W16(96 + 2 * 67, 5200); W16(96 + 2 * 72, 4300); }           /* the colour temperatures */
    #undef W16
    #undef W32
    return n;
}
/* the scales' red / blue to green (Q12) = 2 R / (G1 + G2), 2 B / (G1 + G2) of the levels used */
static int awb_ratio_ok(const int32_t *m, const int *lv)
{
    int32_t r0 = (int32_t)(((uint32_t)lv[0] << 13) / (uint32_t)(lv[1] + lv[2]));
    int32_t r2 = (int32_t)(((uint32_t)lv[3] << 13) / (uint32_t)(lv[1] + lv[2]));
    int32_t q0 = (int32_t)(((int64_t)m[0] << 12) / m[1]), q2 = (int32_t)(((int64_t)m[2] << 12) / m[1]);
    return q0 >= r0 - 2 && q0 <= r0 + 2 && q2 >= r2 - 2 && q2 <= r2 + 2;
}
static void t_light_awb(void)
{
    static uint8_t b[96 + 2 * 1312];
    static const int as[4] = { 1935, 1024, 1024, 1792 };     /* IMG_0208 (Day09): shot on Daylight */
    static const int au[4] = { 2236, 1024, 1024, 1502 };     /* .. Canon's Auto WB of the same scene */
    struct wf_cr2_info inf;
    uint32_t n = awb_head(b, 1312, as, au);
    int rc = wf_cr2_parse(b, n, n, &inf);
    chk(rc == WF_CR2_E_NOSTRIP && inf.cd_count == 1312 && !memcmp(inf.wb, as, sizeof as) && !memcmp(inf.wb_auto, au, sizeof au),
        "ColorData 1312: as shot %d %d %d %d, auto %d %d %d %d (rc %d)", inf.wb[0], inf.wb[1], inf.wb[2], inf.wb[3],
        inf.wb_auto[0], inf.wb_auto[1], inf.wb_auto[2], inf.wb_auto[3], rc);
    const int *use = 0;
    int32_t m[3];
    int wk = wf_dk_wb_pick(inf.wb, inf.wb_auto, 1, &use);
    int ok = wf_dk_scales(WF_DK_EOSM, use, 2048, 15000, 100, m);
    chk(wk == WF_DKWB_AUTO && use == inf.wb_auto && ok && awb_ratio_ok(m, au), "Light Auto on a Daylight shot: the Auto "
        "levels (pick %d, R/G %d B/G %d Q12)", wk, (int)(((int64_t)m[0] << 12) / m[1]), (int)(((int64_t)m[2] << 12) / m[1]));
    for (int L = 1; L < 4; L++) {                             /* Day, Cloudy, Tungsten: the camera was set to them */
        wk = wf_dk_wb_pick(inf.wb, inf.wb_auto, 0, &use);
        ok = wf_dk_scales(WF_DK_EOSM, use, 2048, 15000, 100, m);
        chk(wk == WF_DKWB_ASSHOT && use == inf.wb && ok && awb_ratio_ok(m, as), "Light %d: as shot (pick %d)", L, wk);
    }
    static const int bad[4] = { 2236, 0, 1024, 1502 };        /* a level out of range: no Auto levels */
    n = awb_head(b, 1312, as, bad);
    rc = wf_cr2_parse(b, n, n, &inf);
    wk = wf_dk_wb_pick(inf.wb, inf.wb_auto, 1, &use);
    chk(!inf.wb_auto[0] && !inf.wb_auto[3] && wk == WF_DKWB_NOAUTO && use == inf.wb, "bad Auto levels: none, as shot "
        "(pick %d)", wk);
    n = awb_head(b, 582, as, 0);                              /* an older layout (582 entries): not read */
    rc = wf_cr2_parse(b, n, n, &inf);
    wk = wf_dk_wb_pick(inf.wb, inf.wb_auto, 1, &use);
    chk(inf.cd_count == 582 && !memcmp(inf.wb, as, sizeof as) && !inf.wb_auto[0] && wk == WF_DKWB_NOAUTO && use == inf.wb,
        "ColorData 582: as shot only, Light Auto falls back to it (pick %d)", wk);
    wk = wf_dk_wb_pick(as, 0, 1, &use);
    chk(wk == WF_DKWB_NOAUTO && use == as, "no Auto levels at all: as shot");
    n = awb_head(b, 1250, as, au);                            /* the 5D2's layout (ColorData 4, 1250 entries) */
    rc = wf_cr2_parse(b, n, n, &inf);
    wk = wf_dk_wb_pick(inf.wb, inf.wb_auto, 1, &use);
    ok = wf_dk_scales(WF_DK_5D2, use, 1024, 16000, 200, m);
    chk(inf.cd_count == 1250 && !memcmp(inf.wb, as, sizeof as) && !memcmp(inf.wb_auto, au, sizeof au) && wk == WF_DKWB_AUTO
        && ok && awb_ratio_ok(m, au), "ColorData 1250 (5D2): as shot 63..66, auto 68..71, Light Auto picks the auto ones "
        "(pick %d)", wk);

    /* the owner's CR2: the head as the Darkroom reads it (EOS M: Day09 IMG_0208, shot on Daylight; 5D2: 100EOS5D
       IMG_8380, ISO 200 Auto WB) */
    const char *dir = dk_t_dir();
    char t[300];
    snprintf(t, sizeof t, "%s/%s.CR2", dir, mem_pairs[0]);
    FILE *f = *dir ? fopen(t, "rb") : 0;
    if (!f) {
        chk(1, "SKIPPED: no %s.CR2 (WF_DARK_CR2)", mem_pairs[0]);
        return;
    }
    static uint8_t h[256 << 10];
    fseek(f, 0, SEEK_END);
    long fs = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t got = fread(h, 1, sizeof h, f);
    fclose(f);
    rc = wf_cr2_parse(h, (uint32_t)got, (uint32_t)fs, &inf);
    wk = wf_dk_wb_pick(inf.wb, inf.wb_auto, 1, &use);
#ifdef WF_BUILD_5D2
    static const int cas[4] = { 2013, 1024, 1024, 1920 }, cau[4] = { 2013, 1024, 1024, 1920 };   /* IMG_8380: shot on AWB */
    const int ncd = 1250;
#else
    const int *cas = as, *cau = au, ncd = 1312;
#endif
    chk(rc == WF_CR2_OK && inf.cd_count == ncd && !memcmp(inf.wb, cas, sizeof as) && !memcmp(inf.wb_auto, cau, sizeof au)
        && wk == WF_DKWB_AUTO, "%s: ColorData %d, as shot %d %d %d %d, auto %d %d %d %d, Light Auto picks the auto ones "
        "(rc %d)", mem_pairs[0], inf.cd_count, inf.wb[0], inf.wb[1], inf.wb[2], inf.wb[3], inf.wb_auto[0], inf.wb_auto[1],
        inf.wb_auto[2], inf.wb_auto[3], rc);
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
 * row starts on WF_PRINT_CHAIN_DEF (2383; since 11-15, not the first stock 2302, the B&W print) and steps to the next,
 * and each shot uses that stock's table (the shot's own fallback, wf_shot.c wf_variant_eff, picks the same print). */
/* a chain film's start print (WF_PRINT_CHAIN_DEF when it has it, else its first stock) and the next one it has, -1 if none
 * (also the STAGE cases' first / second print) */
static void chain_prints(int kc, int *v1, int *v2)
{
    *v1 = *v2 = -1;
    for (int v = WF_PRINT_STOCK0; v < WF_NPRINTS && *v1 < 0; v++)
        if (wf_films[kc].prints & (1u << v))
            *v1 = v;
    if (wf_films[kc].prints & (1u << WF_PRINT_CHAIN_DEF))
        *v1 = WF_PRINT_CHAIN_DEF;
    for (int i = 1; i < WF_NPRINTS && *v1 >= 0 && *v2 < 0; i++)
        if (wf_films[kc].prints & (1u << ((*v1 + i) % WF_NPRINTS)))
            *v2 = (*v1 + i) % WF_NPRINTS;
}
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
    int nchain = 0, nbw = 0, kb = -1;
    for (int k = 0; k < wf_nfilms; k++)
        if (!(wf_films[k].flags & WF_F_LUTCAN) && (wf_films[k].prints >> WF_PRINT_STOCK0)) {
            nchain++;
            if (wf_films[k].flags & WF_F_BW) {       /* Double-X 5222: the chain renders it grey only on 2302 */
                nbw++;
                kb = k;
                chk((wf_films[k].prints >> WF_PRINT_STOCK0) == 1 && !(wf_films[k].prints & 7),
                    "B&W chain film %s offers 2302 only, the B&W print", wf_films[k].code);
            } else
                chk(wf_films[k].prints & (1u << WF_PRINT_CHAIN_DEF), "chain film %s offers 2383, the default print",
                    wf_films[k].code);
            chk(!(wf_films[k].prints & (1u << (WF_PRINT_STOCK0 + 14))), "chain film %s offers no 3513D (no table)",
                wf_films[k].code);
        }
    printf("  %d chain films (%d B&W)\n", nchain, nbw);
    chk(nbw == 1, "one B&W chain film, Double-X 5222 (%d)", nbw);
    chain_prints(kc, &v1, &v2);
    chk(wf_films[kc].flags & WF_F_READY, "chain film %d %s: READY (its print tables were there at build time)",
        wf_films[kc].id, wf_films[kc].code);
    chk(v1 >= 0 && v2 >= 0 && !(wf_films[kc].prints & 7), "chain film: two print stocks, no Scan / Cine / Paper");
    if (v2 < 0)
        return;
    /* one pack for the whole case: the module keeps the first pack index of this process */
    struct pk ec[] = { { kc, v1, tab_make(12, 0, 0, 0) }, { kc, v2, tab_make(-10, 0, 0, 0) },
                       { kb, WF_PRINT_STOCK0, tab_make(8, 0, 0, 1) } };
    pack_write(ec, kb >= 0 ? 3 : 2, 0);
    put_cfg("film=%d\noutput=film\nprint=scan\ngrain=off\nhalation=off\nmist=off\n", wf_films[kc].id);
    mod_init(1);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(wf_avail(wf_films[kc].id), 5000) && wf_load(wf_films[kc].id) == WF_LOAD_OK && wf_loaded() == wf_films[kc].id,
        "chain film: on the card and loads");
    char t1[16] = "", t2[16] = "";
    snprintf(t1, sizeof t1, "%s", wf_row_text(WF_ROW_PRINT));
    chk(wf_row_avail(WF_ROW_PRINT) && strcmp(t1, "Scan") && strcmp(t1, "Cine") && strcmp(t1, "Paper"),
        "chain film: Print stock row on a print stock (%s), not Scan", t1);
    chk(v1 != WF_PRINT_CHAIN_DEF || !strcmp(t1, "2383"), "chain film: Print = Scan starts on 2383, the default print (%s)", t1);
    film_shot("chain film, default print stock", kc, v1, 1, 12, 1.5);
    chk(wf_row_step(WF_ROW_PRINT, 1), "chain film: Print stock steps");
    snprintf(t2, sizeof t2, "%s", wf_row_text(WF_ROW_PRINT));
    chk(strcmp(t1, t2) != 0, "chain film: the next stock (%s -> %s)", t1, t2);
    film_shot("chain film, second print stock", kc, v2, 1, -10, 1.5);
    if (kb >= 0) {
        /* the B&W chain film has no 2383: the Print row falls back to its one stock, 2302, and the shot uses that table */
        chk(WAIT(wf_avail(wf_films[kb].id), 5000) && wf_load(wf_films[kb].id) == WF_LOAD_OK
            && wf_loaded() == wf_films[kb].id, "B&W chain film %s: on the card and loads", wf_films[kb].code);
        snprintf(t1, sizeof t1, "%s", wf_row_text(WF_ROW_PRINT));
        chk(!strcmp(t1, "2302"), "B&W chain film: the Print row is on 2302, its only print (%s)", t1);
        film_shot("B&W chain film, 2302", kb, WF_PRINT_STOCK0, 1, 8, 1.5);
    }
    mod_deinit();
}

/* ------------------------------------------------------------------ STAGE: film-stage knob rows (ML/DATA/WFSTAGE.DAT)
 * A synthetic stage pack whose halves compose to the identity at default knobs: X, A1 and B1 straight lines, A2 and B2
 * the grid itself, an interlayer part of 0.05 taken out of A2's base. So Film exp / Push move Y up, a printer point
 * takes 0.025 off its channel, interlayer 0 / 200 % gives -0.05 / +0.05. Entries: (film, start print: 2383) naming the pack's
 * table of it, (film, second print) naming a table the pack does not hold (must be refused). Version 2 (11-16): the
 * knob data of the last four rows, synthetic too: bs 0.05 per channel, the print bleach gain falling from 1 to 0 over
 * s = 0..2 (steep, so the rows' small amounts show: WF_SG_BL_SHIFT); the three channels' curves are the same line, so Neutral neg / print change nothing here (the composer's
 * blend is held against the chain by tools/mk_wfstage.c check).
 * Cases: "" the rows and shots, "bad" a byte of the negative half flipped, "missing" no WFSTAGE.DAT (+ CFG parsing),
 * "dark" the Darkroom's stage with knob rows set against the camera's shot (stage_dark), "v1" a version 1 pack (no knob
 * data: the last four rows hidden and read as 0, a cfg that sets them notwithstanding). */
static struct wf_sg_neg *sg_n;
static struct wf_sg_prn *sg_p;
static struct wf_sg_pair sg_pr;
static struct wf_sg_kd sg_kd;
static void stage_halves(void)
{
    sg_n = calloc(1, sizeof *sg_n);
    sg_p = calloc(1, sizeof *sg_p);
    for (int i = 0; i <= WF_SG_NX; i++)
        sg_n->X[i] = (uint16_t)clampi((int)((int64_t)i * 65536 / (WF_SG_NX - 1)), 0, 65535);
    for (int c = 0; c < 3; c++) {
        sg_n->lo[c] = sg_p->lo[c] = 0;
        sg_n->sc[c] = sg_p->sc[c] = 32 * 65536;  /* density 0..1 over the 33 nodes */
        for (int i = 0; i < WF_SG_N1; i++) {
            sg_n->A1[c][i] = (uint16_t)(i * 64);   /* density = x (Q14) */
            sg_p->B1[c][i] = (uint16_t)(i * 64);   /* print density = 4 x printing density / 4 (Q12) */
        }
    }
    for (int i = 0; i < WF_SG_A3; i++) {
        int nd[3] = { i / (33 * 33), i / 33 % 33, i % 33 };
        for (int c = 0; c < 3; c++) {
            sg_n->A2[i][c] = (int16_t)(nd[c] * 4096 / 32 - 205);   /* base: the node less the interlayer part */
            sg_n->A2[i][3 + c] = 205;                              /* interlayer part 0.05 (Q12) */
        }
    }
    for (int i = 0; i < WF_SG_B3; i++) {
        int nd[3] = { i / (33 * 33), i / 33 % 33, i % 33 };
        for (int c = 0; c < 3; c++)
            sg_p->B2[i][c] = (uint16_t)(nd[c] * 65535 / 32);
    }
    sg_pr.fit = 0;
    sg_pr.bk = 0;
    sg_pr.inv = 65537;                           /* 2^32 / 65535 */
    sg_kd.bs = 3 * 3277;
    for (int i = 0; i < WF_SG_N1; i++)
        sg_kd.g[i] = (uint16_t)(i < 32 ? 65535 - i * 2048 : 0);
}
static void stage_write(int k, int v1, int v2, const uint32_t *T1, int bad, int ver)
{
    uint32_t nb = sizeof *sg_n, pb = sizeof *sg_p, io = 64, no = io + 28 * 2, po = no + 4, dof = (po + 4 + 3) & ~3u;
    uint32_t pof = dof + nb, kof = pof + pb, sz = kof + (ver == 2 ? sizeof sg_kd : 0);
    uint8_t *b = calloc(1, sz);
    memcpy(b, "WFSG", 4);
    uint16_t h[8] = { (uint16_t)ver, 2, 1, 1, WF_SG_NA, WF_SG_NB, WF_SG_N1, WF_SG_NX };
    memcpy(b + 4, h, 16);
    le32(b + 20, nb); le32(b + 24, pb); le32(b + 28, io); le32(b + 32, no); le32(b + 36, po); le32(b + 40, dof);
    le32(b + 44, pof); le32(b + 52, sz);
    for (int e = 0; e < 2; e++) {
        uint8_t *x = b + io + 28 * e;
        x[0] = wf_films[k].id; x[1] = e ? v2 : v1; x[2] = 0; x[3] = 0;
        strncpy((char *)x + 4, wf_films[k].code, 8);
        le32(x + 12, sg_pr.fit); le32(x + 16, sg_pr.bk); le32(x + 20, sg_pr.inv);
        le32(x + 24, fnv(2166136261u, (const uint8_t *)T1, TW * 4));   /* entry 1 names T1 too: not its print's table */
    }
    memcpy(b + dof, sg_n, nb);
    memcpy(b + pof, sg_p, pb);
    if (ver == 2) {
        memcpy(b + kof, &sg_kd, sizeof sg_kd);
        le32(b + 56, kof);
        le32(b + 60, fnv(2166136261u, b + kof, sizeof sg_kd));
    }
    le32(b + no, fnv(2166136261u, b + dof, nb));
    le32(b + po, fnv(2166136261u, b + pof, pb));
    le32(b + 48, fnv(2166136261u, b + io, po + 4 - io));
    if (bad == 'N')
        b[dof + nb / 2] ^= 0x10;                 /* after the checksum: the negative half fails it */
    put_file("ML/DATA/WFSTAGE.DAT", b, sz);
    free(b);
}
/* a film shot through the composed table of these knobs: bit-exact vs wf_sg_compose run here on the same halves */
static double stage_shot(const char *what, int k, int v, const struct wf_sg_knobs *kn)
{
    static uint32_t Te[TW];
    static uint16_t pd[TW * 3];
    wf_sg_compose(sg_n, sg_p, &sg_pr, kn, wf_sg_kdata() ? &sg_kd : 0, 0, pd, Te);
    tabs[k][v] = Te;
    chk(WAIT(look_table_ready(), 10000), "%s: composed table published", what);
    log_mark();
    int sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, Te, flags_of(k, 1));
    int d = ndiff(out, ref, NW);
    double m = mean_dy(in, out, NW);
    chk(d == 0 && sb == 0, "%s: bit-exact vs the table wf_sg_compose makes from the same halves (%d words differ)", what, d);
    chk(LOGW(": film ", 3000), "%s: log '%s'", what, log_line(": film "));
    return m;
}
static int stage_rows_avail(void)
{
    int n = 0;
    for (int r = WF_ROW_SEXP; r <= WF_ROW_SNP; r++)
        n += wf_row_avail(r) != 0;
    return n;
}
/* the spread of Y(a) - Y(b) over the frame (grain) */
static double sd_dy(const uint32_t *a, const uint32_t *b, int n)
{
    double s = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        int d0 = (int)((a[i] >> 8) & 255) - (int)((b[i] >> 8) & 255), d1 = (int)(a[i] >> 24) - (int)(b[i] >> 24);
        s += d0 + d1;
        s2 += d0 * d0 + d1 * d1;
    }
    double m = s / (2.0 * n);
    return sqrt(s2 / (2.0 * n) - m * m);
}
/* mean |dY| of the 16 x 16 block means: two grain fields with other seeds average out, a look that differs does not */
static double blk_dy(const uint32_t *a, const uint32_t *b)
{
    double t = 0;
    int nb = 0;
    for (int by = 0; by + 16 <= FH; by += 16)
        for (int bx = 0; bx + 16 <= FW; bx += 16) {
            int s = 0;
            for (int y = by; y < by + 16; y++)
                for (int x = bx; x < bx + 16; x += 2) {
                    uint32_t p = a[(size_t)y * (FW / 2) + x / 2], q = b[(size_t)y * (FW / 2) + x / 2];
                    s += (int)((p >> 8) & 255) + (int)(p >> 24) - (int)((q >> 8) & 255) - (int)(q >> 24);
                }
            t += fabs(s / 256.0);
            nb++;
        }
    return t / nb;
}
/* STAGE:dark (wf-print-frame's gap, 11-15): the Darkroom's stage (wf_shot_run with devd, as wf_dark.c step 3: the
 * develop's frame through the look) of a chain film with knob rows set and the finish off its defaults, against the
 * camera's shot of the same frame at the same EV. Film exp +1 and Printer G +2 make a composed table (lk->stk): both
 * paths use it (bit-exact vs wf_sg_compose with effects off). Halation Tight + Bloom Strong at Gauge 16 mm: the two
 * paths word for word, and not the 35 mm stage. Grain Even Normal at Gauge 65 mm (its seed is per shot, so not word
 * for word): the same look under the grain (16 px block means) and the same grain spread. */
static void stage_dark(int kc, int v1)
{
    static uint32_t Te[TW];
    static uint16_t pd[TW * 3];
    struct wf_look lk;
    char cl[200];
    const int ev = 12 * 8;
    int hk = 0, gk = 0;
    ms_ev(ev);
    for (int i = 0; i < 3; i++)
        wf_row_step(WF_ROW_SEXP, 1);
    for (int i = 0; i < 2; i++)
        wf_row_step(WF_ROW_SPG, 1);
    chk(!strcmp(wf_row_text(WF_ROW_SEXP), "+1") && !strcmp(wf_row_text(WF_ROW_SPG), "+2"), "knob rows: Film exp %s, Printer G %s",
        wf_row_text(WF_ROW_SEXP), wf_row_text(WF_ROW_SPG));
    struct wf_sg_knobs kn = { 3, 0, 0, { 0, 2, 0 }, WF_SG_IIE_DEF };
    wf_sg_compose(sg_n, sg_p, &sg_pr, &kn, &sg_kd, 0, pd, Te);
    tabs[kc][v1] = Te;
    wf_look_now(&lk);
    chk(lk.stk != 0 && lk.row == kc && lk.variant == v1 && WAIT(look_table_ready(), 10000),
        "knobs set: the look asks for a composed table (key %x), published", lk.stk);

    /* effects off: the composed table on both paths */
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, Te, flags_of(kc, 1));
    const char *dl = dk_stage(in, out, ev);
    int d = ndiff(out, ref, NW);
    chk(d == 0 && strstr(dl, ": film ") && strstr(dl, wf_films[kc].code), "darkroom, knobs set, effects off: bit-exact vs "
        "the composed table (%d words differ; '%s')", d, dl);
    chk(mean_dy(in, out, NW) > 4, "darkroom: the knobs' table, not the pack's (Film exp +1 brighter: mean dY %+.2f)",
        mean_dy(in, out, NW));
    log_mark();
    shot(in, out2, FW, FH);
    d = ndiff(out2, out, NW);
    chk(d == 0, "camera shot, same knobs: = the darkroom's frame word for word (%d differ)", d);
#ifdef WF_BUILD_5D2
    /* the 5D2 has no Darkroom, and its shots are TOP + BOT halves: the halation stage at the seam is not the whole
     * frame's, so the effects are compared on the EOS M only */
    (void)cl;
    (void)hk;
    (void)gk;
    chk(1, "5D2: no Darkroom, effects compared on the EOS M");
#else
    /* Halation Tight + Bloom Strong at Gauge 16 mm, no grain */
    wf_grain = 0;
    wf_halo = 2;
    wf_bloom = 3;
    wf_gauge = 0;
    wf_look_now(&lk);
    wf_fx_keys(lk.post, &hk, &gk);
    chk(WAIT(hk && wf_fx_hk == hk && !wf_fx_gk, 120000) && WAIT(look_table_ready(), 10000) && WF_FXK_HALO(hk) == 2
        && WF_FXK_BLOOM(hk) == 3 && WF_FXK_HGAUGE(hk) == 0, "Halation Tight + Bloom Strong at 16 mm ready (key %x)", wf_fx_hk);
    log_mark();
    shot(in, out2, FW, FH);
    LOGW("shot #", 5000);
    snprintf(cl, sizeof cl, "%s", log_line("shot #"));
    dl = dk_stage(in, out, ev);
    d = ndiff(out, out2, NW);
    chk(d == 0 && strstr(cl, " + halation+bloom strong ") && strstr(dl, " h"), "Tight + Strong at 16 mm: darkroom = camera "
        "word for word (%d differ; '%s' / '%s')", d, cl, dl);
    int dh = ndiff(out, ref, NW);
    chk(dh > NW / 100, "the halation stage ran on the composed look (%d words of %d moved)", dh, NW);
    memcpy(ref, out, NW * 4);                    /* the 16 mm frame */
    wf_gauge = WF_GAUGE_DEF;
    wf_look_now(&lk);
    wf_fx_keys(lk.post, &hk, &gk);
    chk(WAIT(wf_fx_hk == hk && WF_FXK_HGAUGE(wf_fx_hk) == WF_GAUGE_DEF, 120000), "35 mm: key %x", wf_fx_hk);
    dk_stage(in, out, ev);
    d = ndiff(out, ref, NW);
    chk(d > 0, "darkroom: Gauge 16 mm is not the 35 mm stage (%d words differ, mean dY %+.2f)", d, mean_dy(ref, out, NW));

#if WF_GRAIN_CAN
    /* Grain Even Normal at Gauge 65 mm: the grain's seed is per shot */
    wf_gauge = 2;
    wf_look_now(&lk);
    wf_fx_keys(lk.post, &hk, &gk);
    chk(WAIT(wf_fx_hk == hk && WF_FXK_HGAUGE(wf_fx_hk) == 2, 120000), "65 mm, no grain: key %x", wf_fx_hk);
    dk_stage(in, ref, ev);                       /* the 65 mm look without grain */
    wf_grain = 5;
    wf_look_now(&lk);
    wf_fx_keys(lk.post, &hk, &gk);
    chk(WAIT(gk && wf_fx_gk == gk && wf_fx_hk == hk, 120000) && WF_FXK_GGAUGE(gk) == 2 && !strcmp(wf_row_text(WF_ROW_GRAIN),
        "Even Normal"), "Grain %s at 65 mm ready (grain key %x)", wf_row_text(WF_ROW_GRAIN), wf_fx_gk);
    log_mark();
    shot(in, out2, FW, FH);
    LOGW("shot #", 5000);
    usleep(300000);                              /* a long shot line comes in two: " + grain" may be on the first */
    int cg = log_has(" + grain ");
    snprintf(cl, sizeof cl, "%s", log_line("shot #"));
    dl = dk_stage(in, out, ev);
    double sc = sd_dy(out2, ref, NW), sdk = sd_dy(out, ref, NW), bd = blk_dy(out, out2);
    printf("  grain Even Normal 65 mm: spread camera %.2f, darkroom %.2f; 16 px blocks |dY| %.3f\n", sc, sdk, bd);
    chk(cg && strstr(dl, " g"), "grain ran on both paths ('%s' / '%s')", cl, dl);
    chk(sc > 0.5 && sdk > 0.8 * sc && sdk < 1.25 * sc, "the same grain spread (camera %.2f, darkroom %.2f)", sc, sdk);
    chk(bd < 0.5, "the same look under the grain: 16 px block means within %.3f codes", bd);
#endif
#endif
    wf_grain = wf_halo = wf_bloom = 0;
    wf_gauge = WF_GAUGE_DEF;
}
static void t_stage(const char *cs)
{
    int kc = -1, v1 = -1, v2 = -1;
    for (int k = 0; k < wf_nfilms && kc < 0; k++)
        if (!(wf_films[k].flags & WF_F_LUTCAN) && (wf_films[k].prints >> WF_PRINT_STOCK0))
            kc = k;
    if (kc < 0) {
        printf("  (no chain film in this catalogue: STAGE skipped)\n");
        return;
    }
    chain_prints(kc, &v1, &v2);                  /* v1 = the print the film starts on (2383), v2 the next one */
    if (v2 < 0) {
        printf("  (chain film with one print: STAGE skipped)\n");
        return;
    }
    uint32_t *T1 = tab_make(0, 0, 0, 0), *T2 = tab_make(-10, 0, 0, 0);
    struct pk ec[] = { { kc, v1, T1 }, { kc, v2, T2 } };
    pack_write(ec, 2, 0);
    stage_halves();
    struct wf_sg_knobs k0 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
    {   /* the synthetic halves: identity at default knobs on the in-gamut nodes (the composition clamps a node's RGB) */
        static uint32_t Te[TW];
        static uint16_t pd[TW * 3];
        int md = 0;
        wf_sg_compose(sg_n, sg_p, &sg_pr, &k0, &sg_kd, 0, pd, Te);
        for (int i = 0; i < TW; i++) {
            int y = clampi(16 * (i / 289), 0, 255), u = clampi(16 * (i / 17 % 17), 0, 255) - 128;
            int v = clampi(16 * (i % 17), 0, 255) - 128;
            double r = y + 1.402 * v, g = y - 0.344136 * u - 0.714136 * v, bl = y + 1.772 * u;
            if (r < 0 || r > 255 || g < 0 || g > 255 || bl < 0 || bl > 255)
                continue;
            for (int s = 0; s < 24; s += 8) {
                int a = (int8_t)(Te[i] >> s), c = (int8_t)(T1[i] >> s);
                if (s == 8) { a = (Te[i] >> 8) & 255; c = (T1[i] >> 8) & 255; }
                md = abs(a - c) > md ? abs(a - c) : md;
            }
        }
        chk(md <= 2, "synthetic halves: default composition within 2 codes of the identity, in-gamut nodes (max %d)", md);
    }
    int v1p = !strcmp(cs, "v1");
    if (strcmp(cs, "missing"))
        stage_write(kc, v1, v2, T1, !strcmp(cs, "bad") ? 'N' : 0, v1p ? 1 : 2);
    put_cfg("film=%d\noutput=film\nprint=scan\ngrain=off\nhalation=off\nmist=off\n%s", wf_films[kc].id,
            !strcmp(cs, "missing") ? "stage=-6,3,1,-12,12,5,0\n" : !strcmp(cs, "bad") ? "stage=1,2,x,0,0,0,4\n"
            : v1p ? "stage=3,0,0,0,0,0,4,2,3,4,1\n" : !strcmp(cs, "dark") ? "stage=0,0,0,0,0,0,4,0,0,0,0\n" : "");
    mod_init(1);
    gen_frame(in, FW, FH, 0);
    {
        int av = WAIT(wf_avail(wf_films[kc].id), 5000), ld = wf_load(wf_films[kc].id);
        chk(av && ld == WF_LOAD_OK, "chain film %s loads (avail %d, load %d, flags %x, prints %x, ws %d)", wf_films[kc].code,
            av, ld, wf_films[kc].flags, wf_films[kc].prints, wf_ws_state());
    }
    if (!strcmp(cs, "missing")) {
        chk(WAIT(wf_sg_state() == -1, 5000) && LOGW("stage ML/DATA/WFSTAGE.DAT missing", 3000),
            "no stage pack: state %d, log '%s'", wf_sg_state(), log_line("stage"));
        chk(!strcmp(wf_row_text(WF_ROW_SEXP), "-2") && !strcmp(wf_row_text(WF_ROW_SPUSH), "Push 1")
            && !strcmp(wf_row_text(WF_ROW_SCOMP), "On") && !strcmp(wf_row_text(WF_ROW_SPR), "-12")
            && !strcmp(wf_row_text(WF_ROW_SPG), "+12") && !strcmp(wf_row_text(WF_ROW_SPB), "+5")
            && !strcmp(wf_row_text(WF_ROW_SIIE), "0%"), "CFG stage=-6,3,1,-12,12,5,0 read: %s %s %s %s %s %s %s",
            wf_row_text(WF_ROW_SEXP), wf_row_text(WF_ROW_SPUSH), wf_row_text(WF_ROW_SCOMP), wf_row_text(WF_ROW_SPR),
            wf_row_text(WF_ROW_SPG), wf_row_text(WF_ROW_SPB), wf_row_text(WF_ROW_SIIE));
        chk(stage_rows_avail() == 0, "no stage pack: the seven knob rows greyed");
        tabs[kc][v1] = T1;
        film_shot("no stage pack, knobs set: the pack's table", kc, v1, 1, 0, 0.5);
        put_cfg("stage=7,0,0,0,0,0,4\n");          /* Film exp out of range: the whole line is ignored */
        wf_cfg_load();
        chk(wf_sg_exp == -6 && wf_sg_iie == 0, "CFG: a stage line with a value out of range is ignored");
        put_cfg("stage=1,-1,0,2,-3,4,6\n");
        wf_cfg_load();
        chk(wf_sg_exp == 1 && wf_sg_push == -1 && wf_sg_comp == 0 && wf_sg_prn[0] == 2 && wf_sg_prn[1] == -3
            && wf_sg_prn[2] == 4 && wf_sg_iie == 6 && !wf_sg_bl && !wf_sg_pbl && !wf_sg_nn && !wf_sg_np,
            "CFG: stage=1,-1,0,2,-3,4,6 read (seven values: the last four rows 0)");
        put_cfg("stage=0,0,0,0,0,0,4,4,3,2,1\n");
        wf_cfg_load();
        chk(wf_sg_exp == 0 && wf_sg_iie == 4 && wf_sg_bl == 4 && wf_sg_pbl == 3 && wf_sg_nn == 2 && wf_sg_np == 1,
            "CFG: stage=0,0,0,0,0,0,4,4,3,2,1 read (eleven values)");
        put_cfg("stage=1,0,0,0,0,0,4,1\n");          /* eight values: neither form, ignored */
        wf_cfg_load();
        chk(wf_sg_exp == 0 && wf_sg_bl == 4, "CFG: a stage line of eight values is ignored");
        put_cfg("stage=1,0,0,0,0,0,4,0,0,0,5\n");    /* Neutral print out of range */
        wf_cfg_load();
        chk(wf_sg_exp == 0 && wf_sg_np == 1, "CFG: a stage line with the last value out of range is ignored");
        put_cfg("stage=1,0,0,0,0,0,4,0,0,0,0,0\n");  /* twelve values */
        wf_cfg_load();
        chk(wf_sg_exp == 0 && wf_sg_np == 1, "CFG: a stage line of twelve values is ignored");
        mod_deinit();
        return;
    }
    if (v1p) {
        /* a version 1 pack: no knob data, the seven rows of before; the cfg's last four values stay in the cfg but
           compose nothing (the key is Film exp +1's alone) */
        struct wf_look lk;
        chk(WAIT(wf_sg_state() == 1, 5000) && !wf_sg_kdata() && stage_rows_avail() == 7 && !wf_row_avail(WF_ROW_SBL)
            && !wf_row_avail(WF_ROW_SNP), "version 1 pack: read, no knob data, the seven rows on, the last four greyed");
        wf_look_now(&lk);
        struct wf_sg_knobs k1 = { 3, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
        chk(lk.stk == wf_sg_key(&k1) && !lk.sbl && !lk.spbl && !lk.snn && !lk.snp, "version 1 pack: the key %x is Film "
            "exp +1's (%x), the last four rows read as 0", lk.stk, wf_sg_key(&k1));
        double m = stage_shot("version 1 pack: Film exp +1", kc, v1, &k1);
        chk(m > 7, "version 1 pack: Film exp +1 brighter, mean dY %+.2f", m);
        wf_row_step(WF_ROW_SPG, 1);
        h_clock_add_ms += 2000;
        usleep(400000);
        long n;
        char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
        chk(b && strstr(b, "stage=3,0,0,0,1,0,4,2,3,4,1\n"), "version 1 pack: the cfg keeps the last four rows as set");
        free(b);
        mod_deinit();
        return;
    }
    chk(WAIT(wf_sg_state() == 1, 5000) && LOGW("stage ok: 1 of 2 pairs match", 3000),
        "stage pack read, the entry naming another table refused: state %d, '%s'", wf_sg_state(), log_line("stage"));
    struct wf_look lk;
    if (!strcmp(cs, "bad")) {
        /* a bad stage line keeps the defaults: Interlayer starts at 50 % (WF_SG_IIE_START), a composed table from the
           start, so the bad negative half shows at once */
        chk(wf_sg_iie == WF_SG_IIE_START && !wf_sg_exp && !strcmp(wf_row_text(WF_ROW_SIIE), "50%"),
            "a bad stage line keeps the defaults (Interlayer '%s')", wf_row_text(WF_ROW_SIIE));
        chk(LOGW("stage negative half 0 bad", 5000), "a bad negative half: logged ('%s')", log_line("stage"));
        chk(WAIT(stage_rows_avail() == 0, 3000) && wf_look_now(&lk) == WF_OUT_FILM && lk.stk == 0,
            "a bad negative half: knob rows greyed, the look back on the pack's table");
        tabs[kc][v1] = T1;
        film_shot("bad negative half: the pack's table", kc, v1, 1, 0, 0.5);
        mod_deinit();
        return;
    }
    chk(stage_rows_avail() == 11 && wf_sg_has(kc, v1) && !wf_sg_has(kc, v2) && wf_sg_kdata(),
        "first print: the eleven knob rows on (a version 2 pack: knob data)");
    if (!*cs) {
        /* no stage line: Interlayer starts at 50 % (owner 11-16, to shoot it), a composed table from the first shot */
        struct wf_sg_knobs ks = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_START };
        char eb[300];
        wf_look_now(&lk);
        wf_exif_text(&lk, 0, 0, 0, 0, WF_EV_NONE, eb, sizeof eb);
        chk(WF_SG_IIE_START == 2 && wf_sg_iie == WF_SG_IIE_START && !strcmp(wf_row_text(WF_ROW_SIIE), "50%")
            && lk.stk == wf_sg_key(&ks) && lk.stk && lk.siie == WF_SG_IIE_START && strstr(eb, " | Interlayer 50%"),
            "no stage line: Interlayer starts at '%s', key %x (want %x), EXIF '%s'", wf_row_text(WF_ROW_SIIE), lk.stk,
            wf_sg_key(&ks), eb);
        stage_shot("Interlayer 50% (the start)", kc, v1, &ks);
        /* the WindyFilm UI's rows follow the film under the cursor before SET (owner 11-16): wf_row_*_for */
        int fn = -1, fl = -1, idc = wf_films[kc].id;
        for (int k = 0; k < wf_nfilms; k++) {
            if (fn < 0 && (wf_films[k].flags & (WF_F_LUTCAN | WF_F_READY)) == WF_F_READY
                && !(wf_films[k].prints >> WF_PRINT_STOCK0) && (wf_films[k].prints & (1u << WF_PRINT_SCAN)))
                fn = k;
            if (fl < 0 && (wf_films[k].flags & WF_F_LUTCAN))
                fl = k;
        }
        chk(wf_row_avail(WF_ROW_SIIE) && wf_row_avail_for(WF_ROW_SIIE, idc) && wf_row_avail_for(WF_ROW_SBL, idc)
            && !strcmp(wf_row_text_for(WF_ROW_PRINT, idc), wf_row_text(WF_ROW_PRINT)) && wf_row_avail_for(WF_ROW_SIIE, -7),
            "browser on the loaded chain film (or an unknown id): its rows as the menu's");
        if (fn >= 0) {
            chk(!wf_row_avail_for(WF_ROW_SIIE, wf_films[fn].id) && !wf_row_avail_for(WF_ROW_SBL, wf_films[fn].id)
                && !wf_row_avail_for(WF_ROW_SEXP, wf_films[fn].id) && wf_row_avail_for(WF_ROW_STRENGTH, wf_films[fn].id)
                && wf_row_avail(WF_ROW_SIIE) && !strcmp(wf_row_text_for(WF_ROW_PRINT, wf_films[fn].id), "Scan")
                && strcmp(wf_row_text(WF_ROW_PRINT), "Scan"),
                "browser on film %s (not loaded, no stage pair): knob rows greyed, Strength on, Print '%s' (loaded: '%s')",
                wf_films[fn].code, wf_row_text_for(WF_ROW_PRINT, wf_films[fn].id), wf_row_text(WF_ROW_PRINT));
        }
        if (fl >= 0) {
            int idl = wf_films[fl].id;
            chk(!wf_row_avail_for(WF_ROW_SIIE, idl) && !wf_row_avail_for(WF_ROW_PRINT, idl)
                && !wf_row_avail_for(WF_ROW_STRENGTH, idl) && wf_row_avail_for(WF_ROW_HL, idl)
                && wf_row_avail_for(WF_ROW_FLUT, idl), "browser on can %s: Log + LUT rows (knobs, Print, Strength greyed; "
                "Highlights on)", wf_films[fl].code);
        }
        wf_out = WF_OUT_LOG;                          /* plain log: the chain film is not loaded, SET gives it Film */
        chk(WAIT(!wf_loaded() && !wf_row_avail(WF_ROW_SIIE), 2000) && wf_row_avail_for(WF_ROW_SIIE, idc)
            && wf_row_avail_for(WF_ROW_STRENGTH, idc) && !wf_row_avail(WF_ROW_STRENGTH),
            "plain log: the menu's knob rows greyed, the browser's on the chain film (SET loads it on Film)");
        wf_out = WF_OUT_FILM;
        chk(WAIT(wf_loaded() == idc, 2000), "Film again: the chain film loaded");
        chk(wf_row_step(WF_ROW_SIIE, 1) && wf_row_step(WF_ROW_SIIE, 1), "Interlayer up to 100%%");
        wf_look_now(&lk);
        chk(lk.stk == 0, "Interlayer 100 %%: the shipped table again (key %x)", lk.stk);
    }
    chk(!strcmp(wf_row_text(WF_ROW_SEXP), "0") && !strcmp(wf_row_text(WF_ROW_SPUSH), "0")
        && !strcmp(wf_row_text(WF_ROW_SCOMP), "Off") && !strcmp(wf_row_text(WF_ROW_SPG), "0")
        && !strcmp(wf_row_text(WF_ROW_SIIE), "100%"), "knob texts at the shipped table (Interlayer %s)",
        wf_row_text(WF_ROW_SIIE));
    tabs[kc][v1] = T1;
    film_shot("Interlayer 100 percent, the rest 0: the pack's table", kc, v1, 1, 0, 0.5);
    if (!strcmp(cs, "dark")) {
        stage_dark(kc, v1);
        mod_deinit();
        return;
    }
    /* Film exp +1 */
    log_mark();
    for (int i = 0; i < 3; i++)
        chk(wf_row_step(WF_ROW_SEXP, 1), "Film exp steps up");
    chk(!strcmp(wf_row_text(WF_ROW_SEXP), "+1"), "Film exp text '%s'", wf_row_text(WF_ROW_SEXP));
    chk(LOGW("composed", 5000), "composed: '%s'", log_line("composed"));
    struct wf_sg_knobs k1 = { 3, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
    double m = stage_shot("Film exp +1", kc, v1, &k1);
    chk(m > 7 && m < 14, "Film exp +1: brighter, mean dY %+.2f (the synthetic axis: +0.05 = +12.75 codes unclipped)", m);
    wf_look_now(&lk);
    uint32_t key1 = lk.stk;
    /* Film exp +2/3 and Push +1/3: the same exposure axis, the same key and table */
    chk(wf_row_step(WF_ROW_SEXP, -1) && wf_row_step(WF_ROW_SPUSH, 1) && !strcmp(wf_row_text(WF_ROW_SPUSH), "Push 1/3"),
        "Film exp +0.7, Push 1/3 ('%s', '%s')", wf_row_text(WF_ROW_SEXP), wf_row_text(WF_ROW_SPUSH));
    wf_look_now(&lk);
    chk(lk.stk == key1 && key1, "Film exp +2/3 + Push +1/3 = the key of Film exp +1 (%x / %x)", lk.stk, key1);
    stage_shot("Film exp +2/3 + Push +1/3", kc, v1, &k1);
    /* Exp comp: both out, the pack's table again */
    chk(wf_row_step(WF_ROW_SCOMP, 1) && !strcmp(wf_row_text(WF_ROW_SCOMP), "On"), "Exp comp On");
    wf_look_now(&lk);
    chk(lk.stk == 0 && !wf_row_avail(WF_ROW_SEXP) && !wf_row_avail(WF_ROW_SPUSH) && wf_row_avail(WF_ROW_SPR),
        "Exp comp: no offset left (key %x), Film exp / Push greyed", lk.stk);
    tabs[kc][v1] = T1;
    film_shot("Exp comp: the pack's table", kc, v1, 1, 0, 0.5);
    /* Printer G +4: green down 0.1 */
    chk(wf_row_step(WF_ROW_SCOMP, 1) && wf_row_step(WF_ROW_SEXP, -1) && wf_row_step(WF_ROW_SEXP, -1)
        && wf_row_step(WF_ROW_SPUSH, -1), "knobs back to 0");
    for (int i = 0; i < 4; i++)
        wf_row_step(WF_ROW_SPG, 1);
    chk(!strcmp(wf_row_text(WF_ROW_SPG), "+4"), "Printer G '%s'", wf_row_text(WF_ROW_SPG));
    struct wf_sg_knobs k2 = { 0, 0, 0, { 0, 4, 0 }, WF_SG_IIE_DEF };
    m = stage_shot("Printer G +4", kc, v1, &k2);
    chk(m < -5, "Printer G +4: darker (green down), mean dY %+.2f", m);
    for (int i = 0; i < 4; i++)
        wf_row_step(WF_ROW_SPG, -1);
    /* the last four rows (the knob data): Bleach bypass adds 1/64 per step of the sum of the densities (less bs) to each
       channel, brighter here; it stops at 100 % */
    chk(wf_row_step(WF_ROW_SBL, 1) && !strcmp(wf_row_text(WF_ROW_SBL), "25%"), "Bleach bypass '%s'", wf_row_text(WF_ROW_SBL));
    struct wf_sg_knobs k4 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF, 1 };
    wf_look_now(&lk);
    chk(lk.stk == wf_sg_key(&k4) && lk.sbl == 1, "Bleach bypass 25 %%: key %x", lk.stk);
    m = stage_shot("Bleach bypass 25%", kc, v1, &k4);
    chk(m > 1, "Bleach bypass 25%%: brighter on the synthetic halves, mean dY %+.2f", m);
    for (int i = 0; i < 3; i++)
        wf_row_step(WF_ROW_SBL, 1);
    chk(!strcmp(wf_row_text(WF_ROW_SBL), "100%") && !wf_row_step(WF_ROW_SBL, 1), "Bleach bypass 100%%, no further step");
    k4.bleach = 4;
    m = stage_shot("Bleach bypass 100%", kc, v1, &k4);
    chk(m > 5, "Bleach bypass 100%%: brighter, mean dY %+.2f", m);
    for (int i = 0; i < 4; i++)
        wf_row_step(WF_ROW_SBL, -1);
    /* Print bleach 100 %: the gain falls with the print densities' sum, darker */
    for (int i = 0; i < 4; i++)
        chk(wf_row_step(WF_ROW_SPBL, 1), "Print bleach steps up");
    struct wf_sg_knobs k5 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF, 0, 4 };
    m = stage_shot("Print bleach 100%", kc, v1, &k5);
    chk(m < -1 && !strcmp(wf_row_text(WF_ROW_SPBL), "100%"), "Print bleach 100%%: darker, mean dY %+.2f", m);
    /* Neutral neg 100 % + Neutral print 50 % + Print bleach 25 %: a key of its own, the EXIF text names all three */
    for (int i = 0; i < 3; i++)
        wf_row_step(WF_ROW_SPBL, -1);
    for (int i = 0; i < 4; i++)
        wf_row_step(WF_ROW_SNN, 1);
    for (int i = 0; i < 2; i++)
        wf_row_step(WF_ROW_SNP, 1);
    struct wf_sg_knobs k6 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF, 0, 1, 4, 2 };
    wf_look_now(&lk);
    {
        char eb[300];
        wf_exif_text(&lk, 0, 0, 0, 0, WF_EV_NONE, eb, sizeof eb);
        chk(lk.stk == wf_sg_key(&k6) && strstr(eb, " | Print bleach 25% | Neutral neg 100% | Neutral print 50%"),
            "three of the last four rows: key %x, EXIF '%s'", lk.stk, eb);
    }
    stage_shot("Neutral neg 100% + Neutral print 50% + Print bleach 25%", kc, v1, &k6);
    wf_row_step(WF_ROW_SPBL, -1);
    for (int i = 0; i < 4; i++)
        wf_row_step(WF_ROW_SNN, -1);
    for (int i = 0; i < 2; i++)
        wf_row_step(WF_ROW_SNP, -1);
    wf_look_now(&lk);
    chk(lk.stk == 0, "the last four rows back at 0: the pack's table (key %x)", lk.stk);
    /* Interlayer 200 %: +0.05 on every channel; the row stops there */
    for (int i = 0; i < 4; i++)
        wf_row_step(WF_ROW_SIIE, 1);
    chk(!strcmp(wf_row_text(WF_ROW_SIIE), "200%") && !wf_row_step(WF_ROW_SIIE, 1), "Interlayer 200%%, no further step");
    struct wf_sg_knobs k3 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_MAX };
    m = stage_shot("Interlayer 200%", kc, v1, &k3);
    chk(m > 7, "Interlayer 200%%: the synthetic interlayer part adds 0.05, mean dY %+.2f", m);
    /* the second print: its entry names another table, refused; its pack table as it is, knobs or not */
    chk(wf_row_step(WF_ROW_PRINT, 1), "Print stock steps to the second print");
    chk(stage_rows_avail() == 0, "second print (no matching stage entry): the knob rows greyed");
    film_shot("second print, Interlayer 200 percent set: its own pack table", kc, v2, 1, -10, 1.5);
    /* the knob rows are saved */
    h_clock_add_ms += 2000;
    usleep(400000);
    long n;
    char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && strstr(b, "stage=0,0,0,0,0,0,8,0,0,0,0\n"), "CFG: stage=0,0,0,0,0,0,8,0,0,0,0 saved");
    free(b);
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
    if (!strcmp(cs, "odd")) {                    /* every file handle odd: DryOS gives 3, 5, 7 as often as even ones (RAWB8) */
        unlink(LOG_PATH);
        h_fio_odd_fd = 1;
        mod_init(0);
        for (int i = 0; i < 20; i++) {
            wf_log("odd handle line %d", i);
            usleep(30000);
        }
        chk(LOGW("odd handle line 19\n", 3000), "odd handles: the log made and appended");
        b = get_file(LOG_PATH, &n);
        chk(b && count_str(b, "odd handle line ") == 20 && !strstr(b, "card refused"), "all 20 lines, no refusal (%d)",
            b ? count_str(b, "odd handle line ") : -1);
        free(b);
        mod_deinit();
        h_fio_odd_fd = 0;
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
static int rb_ref_auto = 1;                      /* WF_DEV_CUBE: 0 = no auto exposure (a chain film's log table) */
static int rb_ref_hue;                           /* WF_DEV_HUE_* of the reference (A: each channel on its own) */
static int rb_ref_trim;                          /* 1: i's colour trim on the reference's table (wf_rawb_logpk_build) */
static int rb_ref_paper;                         /* WF_PAPER_* on the reference's table, last (as B) */
static void rb_ref(uint32_t *dst, const uint32_t *canon, int src, int roll)
{
    static struct wf_dev_tabs T;
    struct wf_ll_info inf;
    int cu = src == WF_DEV_CUBE;
    int rc = src == WF_DEV_DISP ? wf_dev_build_disp(&T, WF_DEV_EOSM, "ML/DATA/LEICAVSR.CUB", 0, rb_ref_hl, rb_ref_sh, &inf)
                                : wf_dev_build(&T, src, cu ? rb_ref_curve : 0, WF_DEV_EOSM, cu ? rb_ref_cube : 0, 0,
                                               cu && rb_ref_auto, &inf);
    if (!rc && rb_ref_trim)
        wf_dev_trim(&T, WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB);
    if (!rc)
        wf_dev_paper(&T, rb_ref_paper);
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
    j.hue = rb_ref_hue;
    struct wf_dev_stat st;
    int rp = wf_dev_prepare(scr, &T, &j, &st);
    wf_dev_rows(scr, 0, FH, dst, FW * 2);
    printf("reference develop: build %d, levels %d / %d, prepare %d, wb %d, clip %d, roll %d, shoulder %d, trim %d\n", rc,
           lb, lw, rp, st.wb_src, st.clip_quads, st.roll, st.hue, rb_ref_trim);
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

    /* Mist with the develop (owner 11-15): on the developed codes, before the film table (as on an 8-bit shot); off again:
     * its block given back, the develop bit-exact as before */
    wf_mist = 2;
    chk(WAIT(wf_fx_mk == 2, 60000) && h_arena_live == live0 + 2, "B + Mist 1/4: the mist block next to the develop's (%d blocks)",
        h_arena_live - live0);
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, ref, NW) > NW / 50, "B + Mist 1/4: the developed frame misted (%d words differ from the plain "
        "develop)", ndiff(out, ref, NW));
    chk(LOGW(": film ", 3000) && strstr(log_line(": film "), " raw") && strstr(log_line(": film "), " m")
        && LOGW("shot #", 3000) && strstr(log_line("shot #"), " + mist "),
        "B + Mist 1/4 logged: '%s' / '%s'", log_line(": film "), log_line("shot #"));
    wf_mist = 0;
    chk(WAIT(!wf_fx_mk && h_arena_live == live0 + 1, 20000), "Mist off: its block given back (%d)", h_arena_live - live0);
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "Mist off: developed bit-exact again");

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
    chk(strstr(log_line("rawb: black"), "roll 1") != 0 && strstr(log_line("rawb: black"), "shoulder a "),
        "Log + LUT: shoulder on, each channel on its own (a can: not the chain films' I, '%s')", log_line("rawb: black"));
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
        /* Paper Warm on the display cube (11-16): the tables from LEICAVSR.RBT (its key without the paper), the paper
           after the read; the same frame as the direct develop with wf_dev_paper on its table */
        chk(LOGW("rawb: tables disp c0 LEICAVSR.CUB", 30000) && WAIT(wf_rawb_state() == WF_RB_READY, 5000),
            "Leica Vivid 0 / 0 again: '%s'", log_line("rawb: tables disp"));
        log_mark();
        wf_paper = WF_PAPER_WARM;
        chk(LOGW(", paper Warm", 30000) && strstr(log_line("rawb: tables disp c0 LEICAVSR.CUB"), "LEICAVSR.RBT hit, paper Warm"),
            "Leica Vivid Warm: the .RBT read, then the paper ('%s')", log_line("rawb: tables disp"));
        rb_ref_paper = WF_PAPER_WARM;
        rb_ref(ref, in, WF_DEV_DISP, 1);
        log_mark();
        sb = shot(in, out, FW, FH);
        chk(sb == 0 && LOGW("rawb: done", 3000) && ndiff(out, ref, NW) == 0 && ndiff(out, l0, NW) > NW / 4,
            "Leica Vivid Warm: bit-exact (%d words differ), toned (%d words moved)", ndiff(out, ref, NW), ndiff(out, l0, NW));
        wf_paper = 0; rb_ref_paper = 0;
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

    /* both modes at once (owner 11-14 evening, DESIGN 3.6): a roll is loaded and RAW develop is on: the shot is
     * developed by B AND counts as a roll frame; a darkroom run takes B's shoot memory, B builds its tables after it */
    chk(WAIT(wf_rawb_state() == WF_RB_READY, 3000) && h_arena_live == live0 + 1, "roll + B: tables ready");
    h_set_folder_number(100);
    h_set_file_number(300);
    log_mark();
    chk(wf_roll_req_load(wf_films[fA].id, 12) == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000) && wf_rawb == 1
        && LOGW("roll: RAW develop stays on", 3000), "roll + B: the roll loads, RAW develop stays on");
    pic_quality = 0x3070000;
    drive_mode = 0;
    rb_point(RB_A, 0);
    rb_ref(rb_dev, in, WF_DEV_SRGB, 1);
    memcpy(ref, rb_dev, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1));
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && LOGW("rawb: done rows 3456,", 5000) && ndiff(out, ref, NW) == 0,
        "roll + B: the shot is developed by B, bit-exact (%d words differ)", ndiff(out, ref, NW));
    put_file("DCIM/100CANON/IMG_0301.CR2", "II*", 3);
    h_set_file_number(301);
    chk(WAIT(wf_roll_now()->nshot == 1, 5000) && LOGW(" frame 1/12 IMG_0301", 3000),
        "roll + B: the same shot counts as frame 1 (%d/12, '%s')", wf_roll_now()->nshot, log_line(" frame 1/12"));
    log_mark();
    int ds = wf_dark_start();
    chk(ds == 0, "roll + B: the darkroom starts (%d)", ds);
    chk(LOGW("dark: RAW develop tables given back", 5000) && WAIT(!wf_dark_busy(), 15000),
        "roll + B: the run waits for B's shoot memory ('%s')", log_line("dark: RAW"));
    chk(LOGW("dark: roll", 3000), "roll + B: the run ends on the host (no SRM): '%s'", log_line("dark: roll"));
    chk(LOGW("rawb: tables srgb", 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 3000) && WAIT(h_arena_live == live0 + 1, 3000),
        "roll + B: after the run B builds its tables again (blocks %d)", h_arena_live);
    /* the darkroom's tables are B's choice (wf_dark_src): Leica Vivid (WF_FLUT_DISP) = the sRGB develop + the display cube
     * on its codes, not log + cube; the CinePrint cans = log + their C2 cube (the curve names it), as the V-Log cans. The
     * host run ends at "no memory" (no SRM raw) before the build, so the choice is checked here, the build by B's checks */
    {
        char nb[WF_LL_NAME];
        const char *cp = wf_loglut_name(nb, WF_FLUT_CURVE(WF_FLUT_TUNG0), WF_FLUT_TUNG0, 0);
        chk(wf_dark_src(WF_OUT_LLUT, WF_FLUT_LEICAV) == WF_DEV_DISP && wf_dark_src(WF_OUT_LLUT, WF_FLUT_TUNG0) == WF_DEV_CUBE
            && wf_dark_src(WF_OUT_LLUT, 1) == WF_DEV_CUBE && wf_dark_src(WF_OUT_LLUT, 0) == WF_DEV_CUBE
            && wf_dark_src(WF_OUT_FILM, WF_FLUT_LEICAV) == WF_DEV_SRGB && wf_dark_src(WF_OUT_LOG, 0) == WF_DEV_LOG
            && wf_dark_src(WF_OUT_OFF, 0) == 0, "darkroom tables: B's choice (Leica Vivid disp, cubes cube, film srgb, log log)");
        chk(cp && strlen(cp) > 6 && strcmp(cp + strlen(cp) - 6, "C2.CUB") == 0,
            "darkroom tables: a CinePrint can names its Canon Log 2 cube ('%s')", cp ? cp : "-");
    }
    log_mark();
    wf_out = WF_OUT_LLUT;
    wf_flut = WF_FLUT_LEICAV;
    chk(LOGW("rawb: tables disp c0 LEICAVSR.CUB", 30000) && WAIT(wf_rawb_state() == WF_RB_READY, 3000),
        "roll + B, Leica Vivid: B's display tables ('%s')", log_line("rawb: tables"));
    {   /* the darkroom's own build keys B's .RBT the same way (wf_rawb_rbt_key): its read of the file B just wrote hits.
         * (The host run below ends at "no memory" before its build, so this is the darkroom's call sequence.) */
        struct wf_look lk;
        char nb[WF_LL_NAME], rp[WF_LL_NAME];
        uint32_t rk[WF_RBT_NKEY];
        struct wf_dev_tabs *tb = malloc(sizeof *tb);
        wf_look_now(&lk);
        int sr = wf_dark_src(lk.output, lk.flut);
        const char *cb = wf_loglut_name(nb, lk.curve, lk.flut, 0);
        int kr = cb ? wf_rawb_rbt_key(&lk, sr, cb, rp, rk) : -9;
        int rr = kr == 0 ? wf_llb_read(rp, rk, WF_RBT_NKEY, tb, sizeof *tb) : -9;
        chk(sr == WF_DEV_DISP && kr == 0 && strcmp(rp, "ML/DATA/LEICAVSR.RBT") == 0 && rr == WF_LLB_HIT
            && tb->src == WF_DEV_DISP && tb->cam == WF_DEV_EOSM && tb->curve == 0,
            "roll + B, Leica Vivid: the darkroom's key reads B's .RBT (key %d, read %d, '%s')", kr, rr, kr == 0 ? rp : "-");
        lk.lei = (int8_t)(lk.lei + 1);                         /* another LUT EI: another key */
        rr = wf_rawb_rbt_key(&lk, sr, cb, rp, rk) == 0 ? wf_llb_read(rp, rk, WF_RBT_NKEY, tb, sizeof *tb) : -9;
        chk(rr == WF_LLB_KEY, "roll + B, Leica Vivid: another LUT EI is another key (%d)", rr);
        free(tb);
    }
    log_mark();
    ds = wf_dark_start();
    chk(ds == 0 && LOGW("dark: roll 001 start, 1 of 1 to develop, look output 2", 5000) && WAIT(!wf_dark_busy(), 15000),
        "roll + B, Leica Vivid: a darkroom run with the look (%d, '%s')", ds, log_line("dark: roll"));
    chk(LOGW("rawb: tables disp c0 LEICAVSR.CUB", 10000) && LOGW(", LEICAVSR.RBT hit", 3000)
        && WAIT(wf_rawb_state() == WF_RB_READY, 3000) && WAIT(h_arena_live == live0 + 1, 3000),
        "roll + B, Leica Vivid: B's rebuild after the run reads its .RBT ('%s', blocks %d)", log_line("rawb: tables"), h_arena_live);
    wf_flut = 0;
    wf_out = WF_OUT_FILM;
    chk(wf_roll_req_close() == 0 && WAIT(wf_roll_now()->state == WF_RS_DONE, 5000), "roll + B: Close roll");
    wf_rawb = 0;
    chk(WAIT(h_arena_live == live0, 3000), "off again: memory given back");
    mod_deinit();
    free(alt); free(rb_dev);
}

/* RAWB chain (option (c), wf_rawb.h): a chain film on RAW takes its pair's log table from ML/DATA/WFLOG.DAT. The whole look
 * is in the develop (WF_DEV_CUBE on the pack's curve, LUT EI 0, no auto exposure, the shoulder on) and the stage after it
 * adds the effects only: the shot = the direct develop with that table as a .cube, bit-exact. A print the pack lacks, a
 * look the log table cannot express (strength here; knob rows and Highlights / Shadows take the same branch) and a bad
 * pack keep the 8-bit chain: the sRGB base develop + the print's film table, bit-exact, with the reason in the log. */
#define LG_TB (33 * 33 * 33 * 6)
static void lg_write(const struct wf_film *f, int v, const uint8_t *t, int bad)
{
    static uint8_t b[512 + LG_TB];
    memset(b, 0, 512);
    memcpy(b, "WFLG", 4);
    b[4] = 1; b[6] = 1;
    le32(b + 8, 33); le32(b + 12, LG_TB); le32(b + 16, 32); le32(b + 20, 512);
    b[28] = WF_LL_SLOG3; b[29] = 1;
    uint8_t *x = b + 32;
    x[0] = f->id; x[1] = (uint8_t)v;
    strncpy((char *)x + 4, f->code, 8);
    le32(x + 12, 512); le32(x + 16, LG_TB);
    uint32_t a = 1, c = 0;
    for (int i = 0; i < LG_TB; i++) { a = (a + t[i]) % 65521u; c = (c + a) % 65521u; }
    le32(x + 20, c << 16 | a);
    le32(b + 24, fnv(2166136261u, x, 24));
    memcpy(b + 512, t, LG_TB);
    if (bad == 'M') b[0] = 'X';
    put_file("ML/DATA/WFLOG.DAT", b, sizeof b);
}
static void t_rawb_chain(void)
{
    if (!rb_map) rb_map = map_at(RB_Z, 0x8000000u - 4096);
    rb_dev = malloc(NW * 4);
    int kc = -1, v1 = -1, v2 = -1;
    for (int k = 0; k < wf_nfilms && kc < 0; k++)
        if (!(wf_films[k].flags & (WF_F_LUTCAN | WF_F_BW)) && wf_films[k].group == WF_GRP_STAGE
            && (wf_films[k].prints >> WF_PRINT_STOCK0))
            kc = k;
    if (kc < 0) {
        printf("  (no chain film in this catalogue: skipped)\n");
        return;
    }
    const struct wf_film *f = &wf_films[kc];
    chain_prints(kc, &v1, &v2);
    chk(v1 >= 0 && v2 >= 0, "chain film %s: two prints (%d, %d)", f->code, v1, v2);
    if (v2 < 0)
        return;
    gen_frame(in, FW, FH, 0);
    rb_raw((uint8_t *)(uintptr_t)RB_A, in);
    struct pk ec[] = { { kc, v1, tab_make(12, 0, 0, 0) }, { kc, v2, tab_make(-10, 0, 0, 0) } };
    pack_write(ec, 2, 0);
    /* (kc, v1)'s log table: a print-like S curve on the S-Log3 code with a warm tint; v2 is not in the pack */
    uint8_t *t = malloc(LG_TB);
    for (int b = 0, i = 0; b < 33; b++)
        for (int g = 0; g < 33; g++)
            for (int r = 0; r < 33; r++, i++) {
                double x[3] = { r / 32.0, g / 32.0, b / 32.0 }, k3[3] = { 1.03, 1.0, 0.94 };
                for (int c = 0; c < 3; c++) {
                    double o = k3[c] / (1.0 + exp(-(x[c] - 0.42) * 9.0));
                    int q = (int)lrint((o > 1 ? 1 : o) * 65535.0);
                    t[6 * i + 2 * c] = (uint8_t)q;
                    t[6 * i + 2 * c + 1] = (uint8_t)(q >> 8);
                }
            }
    lg_write(f, v1, t, 0);
    FILE *cf = fopen("ML/DATA/LGREF.CUB", "w");      /* the same table as a text cube: exact u16 values */
    fprintf(cf, "TITLE \"log pack reference\"\nLUT_3D_SIZE 33\n");
    for (int i = 0; i < 33 * 33 * 33; i++) {
        const uint8_t *p = t + 6 * i;
        fprintf(cf, "%.6f %.6f %.6f\n", (p[0] | p[1] << 8) / 65535.0, (p[2] | p[3] << 8) / 65535.0,
                (p[4] | p[5] << 8) / 65535.0);
    }
    fclose(cf);
    put_cfg("film=%d\noutput=film\nprint=scan\ngrain=off\nhalation=off\nmist=off\n", f->id);
    mod_init(1);
    h_prop_send(PROP_ASPECT, 0);
    chk(WAIT(look_table_ready(), 10000), "chain film %s: its 8-bit table published", f->code);
    usleep(300000);
    int live0 = h_arena_live;
    pic_quality = 0x3070000;                     /* RAW + L fine */
    drive_mode = 0;
    memset(h_pb, 0, sizeof h_pb);
    memset(h_edmac, 0, sizeof h_edmac);
    rb_point(RB_A, 0);
    struct wf_look lk;
    wf_look_now(&lk);
    chk(lk.variant == v1 && wf_rawb_logpk(&lk) == WF_RLG_OK, "chain film on print %d: the log pack's path (%s)", lk.variant,
        wf_rawb_logpk_why(wf_rawb_logpk(&lk)));
    char want[96];

    /* on: the pair's log table from the pack (the SD read + build logged), a shot = the direct develop with it */
    log_mark();
    wf_rawb = 1;
    snprintf(want, sizeof want, "rawb: tables log pack %s v%d c1 ", f->code, v1);
    chk(LOGW(want, 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "on: the log pack's tables ('%s')",
        log_line("rawb: tables"));
    log_mark();
    int sb = shot(in, out, FW, FH);
    rb_ref_curve = WF_LL_SLOG3;
    rb_ref_cube = "ML/DATA/LGREF.CUB";
    rb_ref_auto = 0;
    rb_ref_hue = WF_DEV_HUE_I;
    rb_ref_trim = 1;
    rb_ref(ref, in, WF_DEV_CUBE, 1);
    chk(wf_rb_hue == WF_DEV_HUE_I, "no shoulder= in WINDYFLM.CFG: the chain films' shoulder I (%d)", wf_rb_hue);
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "log pack shot: bit-exact vs the direct develop with its table as a .cube "
        "(S-Log3, LUT EI 0, no auto exposure, shoulder I + its colour trim), no film table after it (%d words differ)",
        ndiff(out, ref, NW));
    /* the trim moves chroma only: without it every Y byte is the same, U / V nearer 0 with it */
    rb_ref_trim = 0;
    rb_ref(ref, in, WF_DEV_CUBE, 1);
    {
        long ydiff = 0, cdiff = 0, cin = 0, cout = 0;
        for (int i = 0; i < NW; i++) {
            ydiff += (out[i] & 0xff00ff00u) != (ref[i] & 0xff00ff00u);
            cdiff += (out[i] & 0x00ff00ffu) != (ref[i] & 0x00ff00ffu);
            cin += abs((int8_t)(ref[i] & 255)) + abs((int8_t)(ref[i] >> 16));
            cout += abs((int8_t)(out[i] & 255)) + abs((int8_t)(out[i] >> 16));
        }
        chk(ydiff == 0 && cdiff > 0 && cout < cin, "i's colour trim: %ld words with another Y (want 0), %ld with other U / V, "
            "mean |U| + |V| %.2f -> %.2f", ydiff, cdiff, (double)cin / NW, (double)cout / NW);
        printf("trim: %ld of %d words change U / V, Y none; mean |U| + |V| %.2f -> %.2f (%.1f %%)\n", cdiff, NW,
               (double)cin / NW, (double)cout / NW, cin ? 100.0 * (cout - cin) / cin : 0.0);
    }
    chk(LOGW("rawb: done rows 3456,", 5000) && strstr(log_line("rawb: black"), "roll 1")
        && strstr(log_line("rawb: black"), "shoulder i "), "developed, shoulder I: '%s'", log_line("rawb: black"));
    chk(LOGW(": film ", 3000) && strstr(log_line(": film "), " raw"), "shot line marks the develop: '%s'", log_line(": film "));
    memcpy(out2, out, NW * 4);
    /* shoulder=a: the frame as before the owner's pick (RAWB14), bit for bit; shoulder=d: the hue-locked one. Leaving i
       rebuilds the table (i's colour trim lives in it: rb_key), a -> d does not. */
    static const char hk[3] = { 'a', 'i', 'd' };
    for (int hv = WF_DEV_HUE_A; hv <= WF_DEV_HUE_D; hv += WF_DEV_HUE_D - WF_DEV_HUE_A) {
        log_mark();
        wf_rb_hue = hv;
        if (hv == WF_DEV_HUE_A)
            chk(LOGW(want, 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "shoulder=a: the table again without the "
                "trim ('%s')", log_line("rawb: tables"));
        sb = shot(in, out, FW, FH);
        rb_ref_hue = hv;
        rb_ref(ref, in, WF_DEV_CUBE, 1);
        int dv = ndiff(out, out2, NW);
        char sw[16];
        snprintf(sw, sizeof sw, "shoulder %c ", hk[hv]);
        /* A must change the frame; D may equal I here: this frame's light stays under the look's white, where I is D
           (dev mode hue covers I's weights above it) */
        chk(sb == 0 && ndiff(out, ref, NW) == 0 && (hv == WF_DEV_HUE_D || dv > 0), "shoulder=%c: bit-exact vs the direct "
            "develop with hue %d (%d words differ), %d words of %d differ from I", hk[hv], hv, ndiff(out, ref, NW), dv, NW);
        chk(LOGW("rawb: done rows 3456,", 5000) && strstr(log_line("rawb: black"), sw), "shoulder=%c logged: '%s'", hk[hv],
            log_line("rawb: black"));
        printf("shoulder=%c: %d of %d words differ from I\n", hk[hv], dv, NW);
    }
    /* Paper Matte on the chain table (11-16, shoulder d here): rb_key holds it, the table is the pack's + wf_dev_paper */
    {
        uint32_t *gl = malloc(NW * 4);
        memcpy(gl, out, NW * 4);
        log_mark();
        wf_paper = WF_PAPER_MATTE;
        chk(LOGW("rawb: paper Matte on the chain table", 10000) && LOGW(want, 10000)
            && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "Matte: the log pack's table again, papered ('%s')",
            log_line("rawb: paper"));
        sb = shot(in, out, FW, FH);
        rb_ref_paper = WF_PAPER_MATTE;
        rb_ref(ref, in, WF_DEV_CUBE, 1);
        chk(sb == 0 && ndiff(out, ref, NW) == 0 && ndiff(out, gl, NW) > NW / 4, "chain film Matte: bit-exact vs the direct "
            "develop with wf_dev_paper on its table (%d words differ), %d words moved", ndiff(out, ref, NW),
            ndiff(out, gl, NW));
        log_mark();
        wf_paper = 0;
        rb_ref_paper = 0;
        chk(LOGW(want, 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000) && !log_has("rawb: paper"),
            "Glossy: the table without the paper ('%s')", log_line("rawb: tables"));
        free(gl);
    }
    wf_rb_hue = WF_DEV_HUE_I;
    rb_ref_hue = WF_DEV_HUE_A;
    rb_ref_auto = 1;
    rb_ref_cube = 0;

    /* strength 30 %: the log table has no strength, the 8-bit chain does it (base develop + the print's table) */
    log_mark();
    wf_strength = 0;
    snprintf(want, sizeof want, "rawb: chain film %s v%d on the 8-bit chain (strength)", f->code, v1);
    chk(LOGW("rawb: tables srgb", 10000) && LOGW(want, 3000), "strength 30 %%: the 8-bit chain ('%s')",
        log_line("rawb: chain film"));
    chk(WAIT(wf_rawb_state() == WF_RB_READY && look_table_ready(), 10000), "strength 30 %%: tables ready");
    log_mark();
    sb = shot(in, out, FW, FH);
    rb_ref(rb_dev, in, WF_DEV_SRGB, 1);
    memcpy(ref, rb_dev, NW * 4);
    ref_film(ref, NW, tabs[kc][v1], flags_of(kc, 0));
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "strength 30 %%: bit-exact vs the base develop + the print's table at 30 %% "
        "(%d words differ)", ndiff(out, ref, NW));
    log_mark();
    wf_strength = 1;
    snprintf(want, sizeof want, "rawb: tables log pack %s v%d c1 ", f->code, v1);
    chk(LOGW(want, 10000), "strength 100 %%: the log pack again ('%s')", log_line("rawb: tables"));

    /* the second print is not in the pack: one line, then the 8-bit chain for that print */
    log_mark();
    chk(wf_row_step(WF_ROW_PRINT, 1), "print steps");
    wf_look_now(&lk);
    chk(lk.variant == v2, "the second print (%d, want %d)", lk.variant, v2);
    snprintf(want, sizeof want, "rawb: log pack %s v%d: not in the pack", f->code, v2);
    chk(LOGW(want, 10000) && LOGW("rawb: tables srgb", 10000) && LOGW("(not in the log pack)", 3000),
        "a print the pack lacks: '%s', then the 8-bit chain", log_line("rawb: log pack"));
    chk(WAIT(wf_rawb_state() == WF_RB_READY && look_table_ready(), 10000), "second print: tables ready");
    log_mark();
    sb = shot(in, out, FW, FH);
    memcpy(ref, rb_dev, NW * 4);
    ref_film(ref, NW, tabs[kc][v2], flags_of(kc, 1));
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "second print: bit-exact vs the base develop + its table (%d words differ)",
        ndiff(out, ref, NW));

    /* back to the first print: the log pack again, the same frame */
    log_mark();
    chk(wf_row_step(WF_ROW_PRINT, -1), "print back");
    snprintf(want, sizeof want, "rawb: tables log pack %s v%d c1 ", f->code, v1);
    chk(LOGW(want, 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "first print: the log pack again");
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, out2, NW) == 0, "first print: the log pack shot again, bit-exact (%d)", ndiff(out, out2, NW));

    /* a bad pack (its magic): one line, every chain film on the 8-bit chain until the next boot */
    wf_rawb = 0;
    chk(WAIT(h_arena_live == live0, 3000) && wf_rawb_state() == WF_RB_OFF, "off: the tables' memory given back");
    lg_write(f, v1, t, 'M');
    log_mark();
    wf_rawb = 1;
    snprintf(want, sizeof want, "rawb: log pack %s v%d: not a log pack", f->code, v1);
    chk(LOGW(want, 10000) && LOGW("rawb: tables srgb", 10000) && LOGW("(no log pack)", 3000),
        "a bad pack: '%s', then the 8-bit chain", log_line("rawb: log pack"));
    chk(WAIT(wf_rawb_state() == WF_RB_READY, 10000), "bad pack: tables ready");
    log_mark();
    sb = shot(in, out, FW, FH);
    memcpy(ref, rb_dev, NW * 4);
    ref_film(ref, NW, tabs[kc][v1], flags_of(kc, 1));
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "bad pack: bit-exact vs the base develop + the print's table (%d)",
        ndiff(out, ref, NW));
    wf_rawb = 0;
    mod_deinit();
    free(t); free(rb_dev);
}

/* RAWB knobs (option (c) milestones 2-3, wf_rawb.c "the knob rows on RAW"): a chain film on RAW with knobs moved takes a
 * log table composed on the camera from the stage halves (WFSTAGE.DAT: the synthetic halves of STAGE) and WFSTLOG.DAT
 * (written here from tools/mk_wfstlog.py's description, v2: one entry, the x section = each node's own S-Log3 codes, a
 * shift section of synthetic rows), plus the shipped table's residual. The reference: wf_sg_neg_pass_x /
 * wf_sg_prn_pass_rgb on the same halves at the knobs and at default (the composer; tools/mk_wfstage.c logcheck holds it
 * against the chain), the residual worked out here in quarter codes (half away from 0, clipped at +-127) from the shipped
 * table and, for an exposure offset, read at the shift row's positions with this test's own integer trilinear (the
 * wf_stagek.h spec), written as a text cube and developed directly: the shot must be bit-exact. The shipped table = the
 * default composition + a smooth residual of a few codes + three nodes 40 codes off (they clip, up to 9 values; fewer
 * where the table saturates). The shift rows: node k of offset o at k x 256 + o k 8 (Q8, clamped to 32 x 256), so the
 * fractions vary and the top nodes clamp; the real rows (tools/mk_wfstage.c logshift) are held by tools/mk_wfstage.c
 * logdat. */
#define SL_N (33 * 33 * 33)
#define SL_NSH 19                                /* offsets -9 .. +9 thirds of a stop */
#define SL_OMIN (-9)
static uint16_t *sl_x;                           /* the x section: node (r, g, b) -> r / 32, g / 32, b / 32 (Q16) */
static int sl_pos(int o, int k) { return clampi(k * 256 + o * k * 8, 0, 32 * 256); }
static void sl_write(const struct wf_film *f, int v, const uint8_t *t, int bad)
{
    uint32_t n = 1, so = 48 + 36 * n, xo = (so + SL_NSH * 33 * 2 + 511) & ~511u, sz = xo + SL_N * 6;
    uint8_t *b = calloc(1, sz);
    memcpy(b, "WFSL", 4);
    b[4] = 2; b[6] = (uint8_t)n; b[8] = 33; b[10] = WF_LL_SLOG3; b[11] = 1;
    le32(b + 12, 48); le32(b + 16, xo); le32(b + 20, sz); le32(b + 32, so);
    b[36] = SL_NSH; b[38] = (uint8_t)SL_OMIN; b[39] = 0xFF;
    for (int o = 0; o < SL_NSH; o++)
        for (int k = 0; k < 33; k++) {
            int p = sl_pos(o + SL_OMIN, k);
            b[so + 66 * o + 2 * k] = (uint8_t)p;
            b[so + 66 * o + 2 * k + 1] = (uint8_t)(p >> 8);
        }
    le32(b + 40, fnv(2166136261u, b + so, SL_NSH * 66));
    uint8_t *x = b + 48;
    x[0] = f->id; x[1] = (uint8_t)v; x[2] = 0;
    strncpy((char *)x + 4, f->code, 8);
    le32(x + 12, (uint32_t)sg_pr.fit); le32(x + 16, (uint32_t)sg_pr.bk); le32(x + 20, sg_pr.inv);
    uint32_t a = 1, c = 0;
    for (int i = 0; i < SL_N * 6; i++) { a = (a + t[i]) % 65521u; c = (c + a) % 65521u; }
    le32(x + 24, c << 16 | a);
    le32(x + 28, fnv(2166136261u, (const uint8_t *)sg_n, sizeof *sg_n) ^ (bad == 'H'));   /* 'H': other halves */
    le32(x + 32, fnv(2166136261u, (const uint8_t *)sg_p, sizeof *sg_p));
    memcpy(b + xo, sl_x, SL_N * 6);
    le32(b + 24, fnv(2166136261u, b + 48, 36 * n));
    le32(b + 28, fnv(2166136261u, b + xo, SL_N * 6));
    put_file("ML/DATA/WFSTLOG.DAT", b, sz);
    free(b);
}
static int sl_lerp(int a, int b, int f) { return a + (((b - a) * f + 128) >> 8); }
/* the residual x 64 (channel c) at node positions p (Q8 per axis): lerp over R, then G, then B */
static int sl_moved(const int8_t *q, const int p[3], int c)
{
    int i[3], f[3];
    for (int a = 0; a < 3; a++) {
        i[a] = p[a] >> 8 > 31 ? 31 : p[a] >> 8;
        f[a] = p[a] - 256 * i[a];
    }
    int e[2][2];
    for (int db = 0; db < 2; db++)
        for (int dg = 0; dg < 2; dg++) {
            int n0 = i[0] + 33 * (i[1] + dg + 33 * (i[2] + db));
            e[db][dg] = sl_lerp(q[3 * n0 + c] * 64, q[3 * (n0 + 1) + c] * 64, f[0]);
        }
    return sl_lerp(sl_lerp(e[0][0], e[0][1], f[1]), sl_lerp(e[1][0], e[1][1], f[1]), f[2]);
}
/* the table B must build for knobs k: composed(k) + the residual of t against composed(default), read at the node the
 * exposure offset (k->fexp, unkeyed: the whole offset) moves to, as a text cube */
static int sl_nmoved;                            /* sl_ref: values where the moved read differs from the node's own */
static int sl_ref(const uint8_t *t, const struct wf_sg_knobs *k, const char *path)
{
    static uint16_t pd[SL_N * 3], c0[SL_N * 3], ck[SL_N * 3], gn[SL_N];
    static int8_t q[SL_N * 3];
    struct wf_sg_knobs k0 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
    wf_sg_neg_pass_x(sg_n, &sg_pr, &k0, &sg_kd, sl_x, SL_N, pd);
    wf_sg_prn_pass_rgb(sg_p, &sg_pr, &k0, &sg_kd, pd, SL_N, 0, c0, 0);
    wf_sg_neg_pass_x(sg_n, &sg_pr, k, &sg_kd, sl_x, SL_N, pd);
    wf_sg_prn_pass_rgb(sg_p, &sg_pr, k, &sg_kd, pd, SL_N, 0, ck, gn);
    int clip = 0;
    for (int j = 0; j < SL_N * 3; j++) {
        int d = (t[2 * j] | t[2 * j + 1] << 8) - c0[j];
        int v = (int)(d >= 0 ? floor(d / 64.0 + 0.5) : -floor(-d / 64.0 + 0.5));
        if (v > 127 || v < -127) { v = v > 0 ? 127 : -127; clip++; }
        q[j] = (int8_t)v;
    }
    FILE *cf = fopen(path, "w");
    fprintf(cf, "TITLE \"knob table reference\"\nLUT_3D_SIZE 33\n");
    sl_nmoved = 0;
    for (int i = 0; i < SL_N; i++) {
        int p[3] = { sl_pos(k->fexp, i % 33), sl_pos(k->fexp, i / 33 % 33), sl_pos(k->fexp, i / 1089) };
        double o[3];
        for (int ch = 0; ch < 3; ch++) {
            int r = k->fexp ? sl_moved(q, p, ch) : 64 * q[3 * i + ch];
            sl_nmoved += r != 64 * q[3 * i + ch];
            if (wf_sg_res_w(k, &sg_kd))          /* Print bleach gain x the Bleach bypass fade (wf_stagek.h) */
                r = (r * gn[i] + 32768) >> 16;
            o[ch] = clampi(ck[3 * i + ch] + r, 0, 65535) / 65535.0;
        }
        fprintf(cf, "%.6f %.6f %.6f\n", o[0], o[1], o[2]);
    }
    fclose(cf);
    return clip;
}
/* one knob shot with the rows as they stand (the caller marked the log before moving them): B's knob table (its log line
 * names the knobs unkeyed), the shot bit-exact vs sl_ref's cube; returns the words that differ from out2 (the shipped
 * table's frame) */
static int sl_shot(const struct wf_film *f, int v, const uint8_t *t, int nclip, const char *cwant, const char *what)
{
    struct wf_look lk;
    struct wf_sg_knobs kk;
    char want[160];
    wf_look_now(&lk);
    wf_sg_unkey(lk.stk, &kk);
    chk(lk.stk && wf_rawb_logpk(&lk) == WF_RLG_OK, "%s: a knob key %x on the RAW path (%s)", what, lk.stk,
        wf_rawb_logpk_why(wf_rawb_logpk(&lk)));
    snprintf(want, sizeof want, "rawb: tables knobs %s v%d k%x exp %d prn %d %d %d interlayer %d bleach %d %d neutral %d "
             "%d c1 ", f->code, v, (unsigned)lk.stk, kk.fexp, kk.prn[0], kk.prn[1], kk.prn[2], kk.iie * 25,
             kk.bleach * 25, kk.pbleach * 25, kk.nneg * 25, kk.nprn * 25);
    chk(LOGW(want, 15000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "%s: '%s'", what, log_line("rawb: tables"));
    chk(strstr(log_line("rawb: knob table"), cwant) != 0, "%s: the residual's clip logged: '%s'", what,
        log_line("rawb: knob table"));
    int clip = sl_ref(t, &kk, "ML/DATA/SLREF.CUB");
    chk(!kk.fexp || sl_nmoved > SL_N, "%s: the moved read changes %d of %d residual values", what, sl_nmoved, SL_N * 3);
    log_mark();
    int sb = shot(in, out, FW, FH);
    rb_ref_cube = "ML/DATA/SLREF.CUB";
    rb_ref(ref, in, WF_DEV_CUBE, 1);
    int dv = ndiff(out, out2, NW);
    chk(sb == 0 && ndiff(out, ref, NW) == 0 && clip == nclip && dv > 0, "%s: bit-exact vs the direct develop with the "
        "composed table + the residual%s (%d words differ; %d clipped), %d words differ from the shipped table's", what,
        kk.fexp ? " read at the moved node" : "", ndiff(out, ref, NW), clip, dv);
    chk(LOGW("rawb: done rows 3456,", 5000), "%s: developed", what);
    printf("knobs: %s: %d of %d words differ from the shipped table's frame\n", what, dv, NW);
    return dv;
}
/* the rows back to the shipped table: key 0, the log pack, out2 again */
static void sl_default(const struct wf_film *f, int v, const char *what)
{
    struct wf_look lk;
    char want[96];
    wf_look_now(&lk);
    chk(lk.stk == 0, "%s: the default key (%x)", what, lk.stk);
    snprintf(want, sizeof want, "rawb: tables log pack %s v%d c1 ", f->code, v);
    chk(LOGW(want, 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "%s: the log pack's tables ('%s')", what,
        log_line("rawb: tables"));
    log_mark();
    int sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, out2, NW) == 0, "%s: the shipped table's shot, bit-exact (%d)", what, ndiff(out, out2, NW));
}
static void sl_steps(int row, int n)
{
    for (int i = 0; i < (n < 0 ? -n : n); i++)
        chk(wf_row_step(row, n < 0 ? -1 : 1), "row %d steps %s", row, n < 0 ? "down" : "up");
}
static void t_rawb_knobs(void)
{
    if (!rb_map) rb_map = map_at(RB_Z, 0x8000000u - 4096);
    rb_dev = malloc(NW * 4);
    int kc = -1, v1 = -1, v2 = -1;
    for (int k = 0; k < wf_nfilms && kc < 0; k++)
        if (!(wf_films[k].flags & (WF_F_LUTCAN | WF_F_BW)) && wf_films[k].group == WF_GRP_STAGE
            && (wf_films[k].prints >> WF_PRINT_STOCK0))
            kc = k;
    if (kc < 0) {
        printf("  (no chain film in this catalogue: skipped)\n");
        return;
    }
    const struct wf_film *f = &wf_films[kc];
    chain_prints(kc, &v1, &v2);
    chk(v1 >= 0 && v2 >= 0, "chain film %s: two prints (%d, %d)", f->code, v1, v2);
    if (v2 < 0)
        return;
    gen_frame(in, FW, FH, 0);
    rb_raw((uint8_t *)(uintptr_t)RB_A, in);
    const uint32_t *T1 = tab_make(12, 0, 0, 0);
    struct pk ec[] = { { kc, v1, T1 }, { kc, v2, tab_make(-10, 0, 0, 0) } };
    pack_write(ec, 2, 0);
    stage_halves();
    stage_write(kc, v1, v2, T1, 0, 2);
    sl_x = malloc(SL_N * 6);
    for (int b = 0, i = 0; b < 33; b++)
        for (int g = 0; g < 33; g++)
            for (int r = 0; r < 33; r++, i++) {
                int nd[3] = { r, g, b };
                for (int c = 0; c < 3; c++)
                    sl_x[3 * i + c] = (uint16_t)clampi(nd[c] * 65536 / 32, 0, 65535);
            }
    /* the shipped table: the default composition + a smooth residual (a few codes) + three nodes 40 codes up */
    uint8_t *t = malloc(LG_TB);
    {
        static uint16_t pd[SL_N * 3], c0[SL_N * 3];
        struct wf_sg_knobs k0 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
        wf_sg_neg_pass_x(sg_n, &sg_pr, &k0, &sg_kd, sl_x, SL_N, pd);
        wf_sg_prn_pass_rgb(sg_p, &sg_pr, &k0, &sg_kd, pd, SL_N, 0, c0, 0);
        for (int i = 0; i < SL_N; i++)
            for (int c = 0; c < 3; c++) {
                int r = i % 33, g = i / 33 % 33, b = i / 1089;
                double d = 4.0 * sin(0.37 * r + 0.23 * g + 0.11 * b + c) * 257 + (r == g && g == b && r >= 28 ? -600 : 0);
                if (i == 5000 || i == 17000 || i == 30000) d = 40 * 257;
                int q = clampi((int)lrint(c0[3 * i + c] + d), 0, 65535);
                t[6 * i + 2 * c] = (uint8_t)q;
                t[6 * i + 2 * c + 1] = (uint8_t)(q >> 8);
            }
    }
    lg_write(f, v1, t, 0);
    sl_write(f, v1, t, 0);
    struct wf_sg_knobs kd = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
    int nclip = sl_ref(t, &kd, "ML/DATA/SLREF.CUB");   /* the residual's clips: the same for every knob */
    char cwant[96];
    snprintf(cwant, sizeof cwant, "residual clipped at %d of %d values", nclip, SL_N * 3);
    chk(nclip >= 6 && nclip <= 9, "the test table: %d residual values beyond +-127 quarter codes", nclip);
    FILE *cf = fopen("ML/DATA/LGREF.CUB", "w");
    fprintf(cf, "TITLE \"log pack reference\"\nLUT_3D_SIZE 33\n");
    for (int i = 0; i < SL_N; i++) {
        const uint8_t *p = t + 6 * i;
        fprintf(cf, "%.6f %.6f %.6f\n", (p[0] | p[1] << 8) / 65535.0, (p[2] | p[3] << 8) / 65535.0,
                (p[4] | p[5] << 8) / 65535.0);
    }
    fclose(cf);
    /* Interlayer 100 %: the shipped table to start from (a fresh cfg starts at 50 %, STAGE checks that) */
    put_cfg("film=%d\noutput=film\nprint=scan\ngrain=off\nhalation=off\nmist=off\nstage=0,0,0,0,0,0,4,0,0,0,0\n", f->id);
    mod_init(1);
    h_prop_send(PROP_ASPECT, 0);
    chk(WAIT(look_table_ready(), 10000), "chain film %s: its 8-bit table published", f->code);
    chk(WAIT(wf_sg_state() == 1, 5000) && wf_sg_has(kc, v1), "the stage pack read: the pair's knob rows (state %d)",
        wf_sg_state());
    usleep(300000);
    int live0 = h_arena_live;
    pic_quality = 0x3070000;                     /* RAW + L fine */
    drive_mode = 0;
    memset(h_pb, 0, sizeof h_pb);
    memset(h_edmac, 0, sizeof h_edmac);
    rb_point(RB_A, 0);
    struct wf_look lk;
    struct wf_sg_knobs kk;
    char want[120];
    rb_ref_curve = WF_LL_SLOG3;
    rb_ref_auto = 0;
    rb_ref_hue = WF_DEV_HUE_I;
    rb_ref_trim = 1;

    /* default knobs: the shipped table (the pack path) */
    log_mark();
    wf_rawb = 1;
    snprintf(want, sizeof want, "rawb: tables log pack %s v%d c1 ", f->code, v1);
    chk(LOGW(want, 10000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "default knobs: the log pack's tables ('%s')",
        log_line("rawb: tables"));
    log_mark();
    int sb = shot(in, out, FW, FH);
    rb_ref_cube = "ML/DATA/LGREF.CUB";
    rb_ref(ref, in, WF_DEV_CUBE, 1);
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "default knobs: bit-exact vs the shipped table (%d words differ)",
        ndiff(out, ref, NW));
    memcpy(out2, out, NW * 4);

    /* Interlayer 0 / 50 / 200 %: the composed table + the residual */
    static const int iies[3] = { 0, 2, 8 };
    int cur = WF_SG_IIE_DEF;
    for (int q = 0; q < 3; q++) {
        while (cur > iies[q]) { chk(wf_row_step(WF_ROW_SIIE, -1), "Interlayer steps down"); cur--; }
        while (cur < iies[q]) { chk(wf_row_step(WF_ROW_SIIE, 1), "Interlayer steps up"); cur++; }
        log_mark();
        wf_look_now(&lk);
        chk(lk.stk && lk.siie == iies[q] && wf_rawb_logpk(&lk) == WF_RLG_OK, "Interlayer %d %%: a knob key %x on the RAW "
            "path (%s)", iies[q] * 25, lk.stk, wf_rawb_logpk_why(wf_rawb_logpk(&lk)));
        snprintf(want, sizeof want, "rawb: tables knobs %s v%d k%x exp 0 prn 0 0 0 interlayer %d bleach 0 0 neutral 0 0 "
                 "c1 ", f->code, v1, (unsigned)lk.stk, iies[q] * 25);
        chk(LOGW(want, 15000) && WAIT(wf_rawb_state() == WF_RB_READY, 2000), "Interlayer %d %%: '%s'", iies[q] * 25,
            log_line("rawb: tables"));
        chk(strstr(log_line("rawb: knob table"), cwant) != 0, "the residual's clip logged: '%s'",
            log_line("rawb: knob table"));
        wf_sg_unkey(lk.stk, &kk);
        int clip = sl_ref(t, &kk, "ML/DATA/SLREF.CUB");
        log_mark();
        sb = shot(in, out, FW, FH);
        rb_ref_cube = "ML/DATA/SLREF.CUB";
        rb_ref(ref, in, WF_DEV_CUBE, 1);
        int dv = ndiff(out, out2, NW);
        chk(sb == 0 && ndiff(out, ref, NW) == 0 && clip == nclip && dv > 0, "Interlayer %d %%: bit-exact vs the direct develop "
            "with the composed table + residual (%d words differ; %d clipped), %d words differ from 100 %%",
            iies[q] * 25, ndiff(out, ref, NW), clip, dv);
        chk(LOGW("rawb: done rows 3456,", 5000), "Interlayer %d %%: developed", iies[q] * 25);
        printf("knobs: Interlayer %d %%: %d of %d words differ from 100 %%\n", iies[q] * 25, dv, NW);
    }

    /* the exposure offset (milestone 3): Film exp +1 (Interlayer back at 100 %), the residual read at the moved node */
    log_mark();
    sl_steps(WF_ROW_SIIE, WF_SG_IIE_DEF - cur);
    cur = WF_SG_IIE_DEF;
    sl_steps(WF_ROW_SEXP, 3);
    int d1 = sl_shot(f, v1, t, nclip, cwant, "Film exp +1");
    /* Push / pull -1/3 x 3 cancels it: offset 0, the default key, the shipped table */
    log_mark();
    sl_steps(WF_ROW_SPUSH, -3);
    sl_default(f, v1, "Film exp +1 + Push -1");
    /* Film exp -2 + Push -1 = offset -9 (the bottom row), Printer R +12 */
    log_mark();
    sl_steps(WF_ROW_SEXP, -9);
    sl_steps(WF_ROW_SPR, 12);
    int d2 = sl_shot(f, v1, t, nclip, cwant,"Film exp -2 + Push -1, Printer R +12");
    /* Exp comp on: no offset left (the residual at the node itself), Printer R +12 alone */
    log_mark();
    chk(wf_row_step(WF_ROW_SCOMP, 1), "Exp comp on");
    wf_look_now(&lk);
    wf_sg_unkey(lk.stk, &kk);
    chk(kk.fexp == 0 && kk.prn[0] == 12, "Exp comp on: offset %d, Printer R %d in the key", kk.fexp, kk.prn[0]);
    int d3 = sl_shot(f, v1, t, nclip, cwant, "Exp comp on, Printer R +12");
    /* a mixed set: Film exp +5/3, Push +2/3 (offset +7), Printer R +3 G -2 B +4, Interlayer 150 % */
    log_mark();
    chk(wf_row_step(WF_ROW_SCOMP, 1), "Exp comp off");
    sl_steps(WF_ROW_SEXP, 11);
    sl_steps(WF_ROW_SPUSH, 5);
    sl_steps(WF_ROW_SPR, -9);
    sl_steps(WF_ROW_SPG, -2);
    sl_steps(WF_ROW_SPB, 4);
    sl_steps(WF_ROW_SIIE, 2);
    int d4 = sl_shot(f, v1, t, nclip, cwant, "mixed: offset +7, Printer +3 -2 +4, Interlayer 150 %");
    printf("knobs: words off the shipped table's frame: %d / %d / %d / %d\n", d1, d2, d3, d4);
    /* strength 30 % with knobs: the 8-bit chain (wf_film_words' mix); back at 100 %, the knob table again */
    log_mark();
    wf_strength = 0;
    wf_look_now(&lk);
    snprintf(want, sizeof want, "rawb: chain film %s v%d on the 8-bit chain (strength)", f->code, v1);
    chk(lk.stk && wf_rawb_logpk(&lk) == WF_RLG_STRENGTH && LOGW("rawb: tables srgb", 10000) && LOGW(want, 3000),
        "knobs + strength 30 %%: the 8-bit chain ('%s')", log_line("rawb: chain film"));
    chk(WAIT(wf_rawb_state() == WF_RB_READY && look_table_ready(), 10000), "knobs + strength 30 %%: tables ready");
    log_mark();
    wf_strength = 1;
    sl_shot(f, v1, t, nclip, cwant, "mixed again at strength 100 %");
    /* every row back: the shipped table, the same frame */
    log_mark();
    sl_steps(WF_ROW_SEXP, -5);
    sl_steps(WF_ROW_SPUSH, -2);
    sl_steps(WF_ROW_SPR, -3);
    sl_steps(WF_ROW_SPG, 2);
    sl_steps(WF_ROW_SPB, -4);
    sl_steps(WF_ROW_SIIE, -2);
    sl_default(f, v1, "default knobs again");
    /* the last four rows on RAW (11-16): Bleach bypass 50 %, Print bleach 75 % (its gain scales the residual), Neutral
       neg 100 %, Neutral print 25 %; then with Film exp +1 on top (the residual read at the moved node, then scaled) */
    log_mark();
    sl_steps(WF_ROW_SBL, 2);
    sl_steps(WF_ROW_SPBL, 3);
    sl_steps(WF_ROW_SNN, 4);
    sl_steps(WF_ROW_SNP, 1);
    int d5 = sl_shot(f, v1, t, nclip, cwant, "Bleach bypass 50 %, Print bleach 75 %, Neutral neg 100 %, print 25 %");
    log_mark();
    sl_steps(WF_ROW_SEXP, 3);
    int d6 = sl_shot(f, v1, t, nclip, cwant, "the same + Film exp +1");
    printf("knobs: the last four rows: %d / %d words off the shipped table's frame\n", d5, d6);
    log_mark();
    sl_steps(WF_ROW_SEXP, -3);
    sl_steps(WF_ROW_SBL, -2);
    sl_steps(WF_ROW_SPBL, -3);
    sl_steps(WF_ROW_SNN, -4);
    sl_steps(WF_ROW_SNP, -1);
    sl_default(f, v1, "the last four rows back at 0");

    /* a WFSTLOG.DAT made for other halves: one line, this pair's knob tables on the 8-bit chain */
    wf_rawb = 0;
    chk(WAIT(h_arena_live == live0, 3000) && wf_rawb_state() == WF_RB_OFF, "off: the tables' memory given back");
    sl_write(f, v1, t, 'H');
    chk(wf_row_step(WF_ROW_SIIE, -1), "Interlayer 75 %%");
    log_mark();
    wf_rawb = 1;
    snprintf(want, sizeof want, "rawb: knob table %s v%d k", f->code, v1);
    chk(LOGW(want, 15000) && strstr(log_line("rawb: knob table"), "packs of another build")
        && LOGW("rawb: tables srgb", 10000), "other halves: '%s', then the 8-bit chain", log_line("rawb: knob table"));
    wf_look_now(&lk);
    chk(wf_rawb_logpk(&lk) == WF_RLG_KNOBS, "other halves: the pair's knob rows on the 8-bit chain until the next boot");
    wf_rawb = 0;
    mod_deinit();
    free(t); free(sl_x); free(rb_dev);
    rb_ref_hue = WF_DEV_HUE_A;
    rb_ref_auto = 1;
    rb_ref_cube = 0;
    rb_ref_trim = 0;
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

    /* Mist with the develop (owner 11-15): on the developed codes of both halves, before the film table (as on an 8-bit
     * shot); off again: its block given back, the develop bit-exact as before */
    wf_mist = 2;
    chk(WAIT(wf_fx_mk == 2, 60000) && h_arena_live == live0 + 2, "B + Mist 1/4: the mist block next to the develop's (%d blocks)",
        h_arena_live - live0);
    log_mark();
    sb = shot(in, out, FW, FH);
    usleep(500000);
    chk(sb == 0 && LOGW("BOT half continues", 3000) && ndiff(out, ref, nh) > nh / 50 && ndiff(out + nh, ref + nh, NW - nh) > nh / 50,
        "B + Mist 1/4: both developed halves misted (%d / %d words differ from the plain develop)", ndiff(out, ref, nh),
        ndiff(out + nh, ref + nh, NW - nh));
    chk(LOGW(": film ", 3000) && strstr(log_line(": film "), " raw") && strstr(log_line(": film "), " m")
        && strstr(log_line("shot #"), " + mist ") && strstr(log_line("shot #"), "(TOP "),
        "B + Mist 1/4 logged: '%s' / '%s'", log_line(": film "), log_line("shot #"));
    wf_mist = 0;
    chk(WAIT(!wf_fx_mk && h_arena_live == live0 + 1, 20000), "Mist off: its block given back (%d)", h_arena_live - live0);
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, ref, NW) == 0, "Mist off: developed bit-exact again");

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

/* ------------------------------------------------------------------ ROLL: Roll + Darkroom in the module (DESIGN 3.6)
 * Both modes at once (owner 11-14 evening): a roll load leaves RAW develop alone and RAW develop leaves the roll
 * alone (RAWB checks a B shot that also counts, and a darkroom run taking B's memory). Frames count from the card's
 * file number + CR2; PLAY is blocked while a roll is in the camera (Blind roll). The darkroom run itself is
 * tools/dr_dark_check.sh (the chain) and a camera run (memory). */
extern const int wf_t_me_size, wf_t_me_name, wf_t_me_priv, wf_t_me_select, wf_t_me_children;
int wf_t_me_shidden(const void *e);
static uint8_t *menu_named(const char *nm)
{
    uint8_t *top = h_menu;
    uint8_t *e = top ? *(uint8_t **)(top + wf_t_me_children) : 0;
    for (int i = 0; e && i < 64 && *(void **)(e + wf_t_me_priv) != (void *)-1; i++, e += wf_t_me_size) {
        const char *n = *(const char **)(e + wf_t_me_name);
        if (n && !strcmp(n, nm))
            return e;
    }
    return 0;
}
static int menu_set(const char *nm)                 /* SET on the entry: its select, as ML calls it */
{
    uint8_t *e = menu_named(nm);
    void (*sel)(void *, int) = e ? *(void (**)(void *, int))(e + wf_t_me_select) : 0;
    if (!sel)
        return -1;
    sel(*(void **)(e + wf_t_me_priv), 1);
    return 0;
}
#define H_KEY_HALFSHUTTER 1                       /* ML module.h MODULE_KEY_PRESS_HALFSHUTTER / _SET (as WF_KEY_PLAY) */
#define H_KEY_FULLSHUTTER 3
#define H_KEY_SET 9
#define H_KEY_INFO 23
#define H_ROLLF "ML/SETTINGS/WFROLL.TXT"
static int roll_cr2_yes(void *ctx, int folder, int file) { (void)ctx; (void)folder; (void)file; return 1; }
static char *roll_file_wait(const char *want, long *n, int ms)   /* wf_task writes it after the state changed in memory */
{
    char *b = 0;
    for (int t = 0; t <= ms; t += 100) {
        free(b);
        b = get_file(H_ROLLF, n);
        if (b && *n == 1536 && strstr(b, want))
            break;
        usleep(100000);
    }
    return b;
}
/* ROLL:save / short / back - the roll file on RAWB8's card (owner 11-15): "not saved, retry" every 3 s, 0 bytes after a
 * reboot. Every file handle odd here (DryOS hands out 3, 5, 7 as often as even ones; RAWB8 took them for errors and left
 * them open). save: the empty WFROLL.TXT that card was left with; short: no file, the first write cut short; back: a
 * roll file read at boot. One boot per case (the module loads once per boot) */
static void t_roll_save(const char *cs)
{
    long n = 0;
    char *b;
    std_pack();
    h_set_folder_number(100);
    h_set_file_number(300);
    h_fio_odd_fd = 1;
    snprintf(h_fio_cpath, sizeof h_fio_cpath, "%s", H_ROLLF);
    const int film = wf_films[fA].id;
    if (!strcmp(cs, "save")) {
        put_file(H_ROLLF, "", 0);
        log_mark();
        mod_init(1);
        chk(LOGW("roll: " H_ROLLF " is empty", 3000) && wf_roll_now()->state == WF_RS_EMPTY,
            "RAWB8's empty roll file at boot: no roll (%s)", log_line("roll: "));
        int rc = wf_roll_req_load(film, 12);
        chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000), "a roll loads (%d)", rc);
        b = roll_file_wait("state=loaded", &n, 3000);
        chk(b && n == 1536 && h_fio_cpath_n == 0 && !log_has("not saved"),
            "odd handles, the empty file: saved in place at once (%ld bytes, %d creates)", n, h_fio_cpath_n);
        free(b);
    } else if (!strcmp(cs, "short")) {
        unlink(H_ROLLF);
        snprintf(h_fio_wpath, sizeof h_fio_wpath, "%s", H_ROLLF);
        h_fio_wmax = 100;
        log_mark();
        mod_init(1);
        int rc = wf_roll_req_load(film, 12);
        chk(rc == 0 && LOGW("roll: " H_ROLLF " not saved (write rc 100), retry", 3000),
            "a short first write: logged with its step ('%s')", log_line("not saved"));
        b = roll_file_wait("state=loaded", &n, 6000);
        long nl = 0;
        char *lg = get_file(LOG_PATH, &nl);
        int said = lg ? count_str(lg + (nl > log_at ? log_at : nl), "not saved") : -1;
        free(lg);
        chk(b && n == 1536 && LOGW("roll: " H_ROLLF " saved after 2 tries", 2000) && said == 1 && h_fio_cpath_n == 1,
            "saved by the next try, in place (%ld bytes, %d 'not saved' lines, %d creates)", n, said, h_fio_cpath_n);
        free(b);
        h_fio_wpath[0] = 0;
    } else {                                     /* back: a roll file from an earlier boot, read through an odd handle */
        struct wf_roll r;
        char t[1536];
        wf_roll_reset(&r);
        wf_roll_load(&r, film, 12, 100, 290);
        wf_roll_seen(&r, 100, 292, roll_cr2_yes, 0);
        chk(wf_roll_text(&r, t, sizeof t) == 1536, "a roll file of 2 frames made");
        put_file(H_ROLLF, t, sizeof t);
        log_mark();
        mod_init(1);
        chk(LOGW("roll: 001 loaded film", 3000) && wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->nshot == 2,
            "the roll read back at boot (%s)", log_line("roll: "));
    }
    chk(wf_roll_req_close() == 0 && WAIT(wf_roll_now()->state == WF_RS_DONE, 5000), "the roll closes");
    b = roll_file_wait("state=done", &n, 3000);
    chk(b && h_fio_cpath_n == (!strcmp(cs, "short") ? 1 : 0), "state=done on the card, rewritten in place (%d creates)",
        h_fio_cpath_n);
    free(b);
    mod_deinit();
    h_fio_cpath[0] = 0;
    h_fio_odd_fd = 0;
}
/* ROLL:lock - the shutter held on a finished roll (owner 11-15): a roll of 3, the close-and-load request, the new roll
 * free, a roll of 6 full again, unload lets go */
static int roll_shoot(int from, int n)
{
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        char p[64];
        int n0 = wf_roll_now()->nshot;
        snprintf(p, sizeof p, "DCIM/100CANON/IMG_%04d.CR2", from + i);
        put_file(p, "II*", 3);
        h_set_file_number(from + i);
        ok = WAIT(wf_roll_now()->nshot == n0 + 1, 5000);
    }
    return ok;
}
static void t_roll_lock(void)
{
    std_pack();
    h_set_folder_number(100);
    h_set_file_number(400);
    mod_init(1);
    const int film = wf_films[fA].id, film2 = wf_films[fC].id;
    int rc = wf_roll_req_load(film, 3);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->exp == 3, 5000), "a roll of 3 loads (%d)", rc);
    chk(wf_roll_req_swap(film2, 6, 0) == -1, "close-and-load refused while the roll still shoots");
    chk(roll_shoot(401, 3) && wf_roll_now()->state == WF_RS_FULL && LOGW("roll: shutter blocked (full)", 3000)
        && (icu_uilock & 1), "3 frames: full, the shutter held (%d/3, lock %x)", wf_roll_now()->nshot, icu_uilock);
    usleep(300000);
    cbr_key(H_KEY_SET);                          /* the first notice (5 s) away: SET drops it (the Darkroom is asked for) */
    usleep(400000);
    log_mark();
    chk(cbr_key(H_KEY_HALFSHUTTER) == 0 && LOGW("roll: end notice full", 3000), "half-shutter: kept, the notice at once");
    lv = 0;
    chk(cbr_key(H_KEY_HALFSHUTTER) == 1, "no LiveView (Canon's own screens): the key passes, ML's lock holds the shutter");
    lv = 1;
    chk(wf_roll_req_swap(film2, 7, 0) == -2 && wf_roll_req_swap(9999, 6, 0) == -2, "close-and-load: a bad count / film refused");
    rc = wf_roll_req_swap(film2, 6, 0);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->film == film2 && wf_roll_now()->exp == 6
        && wf_roll_now()->num == 2, 5000) && LOGW("roll: 001 on the shelf (a new roll): 3/3, 3 to develop, rewound", 3000)
        && LOGW("roll: 002 loaded", 3000) && file_exists("ROLLS/ROLL001/WFROLL.TXT"),
        "close-and-load: roll 001 on the shelf (3 to develop), roll 002 x6 loaded (%d, %s)", rc, log_line("roll: 00"));
    chk(LOGW("roll: shutter free", 3000) && !(icu_uilock & 1) && cbr_key(H_KEY_HALFSHUTTER) == 1,
        "the new roll: the shutter free, the half-shutter passes");
    chk(roll_shoot(404, 6) && wf_roll_now()->state == WF_RS_FULL && WAIT(icu_uilock & 1, 3000),
        "6 frames: full, held again (%d/6)", wf_roll_now()->nshot);
    /* one screen (owner 11-17): the browser's mode chip sets the full roll aside for JPEG: the shutter free, the look
       built for shots, PLAY Canon's, its 6 frames kept; a new file is no frame; back on Darkroom it is held again; a
       close ends the set-aside */
    log_mark();
    chk(wf_roll_shelve(1) == 0 && wf_roll_shelved() && LOGW("roll: 002 set aside", 3000) && LOGW("roll: shutter free", 3000)
        && !(icu_uilock & 1) && wf_look_use() == WF_LU_SHOTS && !wf_roll_blocks_play() && cbr_key(H_KEY_HALFSHUTTER) == 1,
        "set aside: the shutter free (lock %x), the look for shots (%d), PLAY free", icu_uilock, wf_look_use());
    put_file("DCIM/100CANON/IMG_0410.CR2", "II*", 3);
    h_set_file_number(410);
    usleep(1500000);
    chk(wf_roll_now()->nshot == 6 && wf_roll_left(wf_roll_now()) == 6 && wf_roll_now()->state == WF_RS_FULL,
        "a shot while set aside: not a frame (%d/6, %d left)", wf_roll_now()->nshot, wf_roll_left(wf_roll_now()));
    log_mark();
    chk(wf_roll_shelve(0) == 0 && !wf_roll_shelved() && LOGW("roll: 002 back in use (no longer set aside)", 3000)
        && LOGW("roll: shutter blocked (full)", 3000) && (icu_uilock & 1) && wf_look_use() == WF_LU_NONE,
        "back on Darkroom: held again (lock %x), nothing of the look (%d)", icu_uilock, wf_look_use());
    wf_roll_look8 = 1;                           /* owner 11-17: a Film stage roll in JPEG 8-bit: the look at the shot */
    chk(wf_look_use() == WF_LU_SHOTS, "a JPEG 8-bit roll: the look for shots (%d)", wf_look_use());
    wf_roll_look8 = 0;
    chk(wf_look_use() == WF_LU_NONE, "rolllook off again: nothing of the look (%d)", wf_look_use());
    chk(wf_roll_shelve(1) == 0 && wf_roll_req_close() == 0 && WAIT(!wf_roll_open(wf_roll_now()), 5000)
        && LOGW("roll: 002 no longer set aside (close)", 3000) && !wf_roll_shelved() && wf_roll_shelve(1) == -1,
        "closed while set aside: no longer set aside, nothing left to set aside");
    chk(wf_roll_req_load(film, 3) == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000) && roll_shoot(411, 3)
        && wf_roll_now()->state == WF_RS_FULL && WAIT(icu_uilock & 1, 3000), "a roll of 3 again: full, held (%d/3)",
        wf_roll_now()->nshot);
    log_mark();
    mod_deinit();
    chk(log_has("roll: shutter free") && !(icu_uilock & 1), "unload: the shutter free (lock %x)", icu_uilock);
}
/* ROLL:dark / ROLL:boot - the owner's RAWB9 (11-15): every roll frame took the in-camera look (23..36 s), the Darkroom
 * never started ("dark: no task (5)": DryOS's table full, the UI task on its last slot), and at boot the roll came only
 * after the grain build (10.9 / 15.2 s). dark: no roll = the look as before; a roll in the camera = nothing of the look
 * built, its frames Canon's own JPEG; the Darkroom starts with the table full and builds the look for its run. boot: a
 * roll file at boot = the roll read first, nothing built; closed = the look for shots again */
static volatile int ui_hold;
static void ui_task_stand_in(void *a) { (void)a; while (ui_hold) usleep(20000); }
static void dump_frame(const char *path, const uint32_t *p, int w, int h)   /* 1/8 size RGB PPM for the report */
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int ow = w / 8, oh = h / 8;
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            uint32_t q = p[(size_t)y * 8 * (w / 2) + x * 4];
            int u = (int8_t)(q & 255), yy = (q >> 8) & 255, v = (int8_t)((q >> 16) & 255);
            unsigned char c[3] = { (unsigned char)clampi(yy + 1402 * v / 1000, 0, 255),
                                   (unsigned char)clampi(yy - (344 * u + 714 * v) / 1000, 0, 255),
                                   (unsigned char)clampi(yy + 1772 * u / 1000, 0, 255) };
            fwrite(c, 1, 3, f);
        }
    fclose(f);
}
static void t_roll_dark(const char *cs)
{
    std_pack();
    const int film = wf_films[fC].id;
    put_cfg("film=%d\noutput=film\ngrain=normal\nhalation=bloom\nmist=w4\n", film);   /* Mist in: owner 11-15 */
    wf_fx_retry_ms = 500;
    h_set_folder_number(100);
    if (!strcmp(cs, "boot")) {
        struct wf_roll r;
        char t[1536];
        wf_roll_reset(&r);
        wf_roll_load(&r, film, 3, 100, 600);
        wf_roll_seen(&r, 100, 601, roll_cr2_yes, 0);
        chk(wf_roll_text(&r, t, sizeof t) == 1536, "a roll file of 1 frame made");
        put_file(H_ROLLF, t, sizeof t);
        h_set_file_number(601);
        mod_init(1);
        chk(LOGW("roll: 001 loaded film", 3000) && wf_roll_now()->state == WF_RS_LOADED, "the roll read back at boot (%s)",
            log_line("roll: "));
        usleep(1500000);
        chk(!log_has("fx grain") && !log_has("fx halation") && !log_has("fx mist") && !wf_fx_hk && !wf_fx_gk && !wf_fx_mk,
            "a roll at boot: nothing of the look built ('%s')", log_line("fx "));
        log_mark();
        chk(wf_roll_req_close() == 0 && WAIT(wf_roll_now()->state == WF_RS_DONE, 5000), "the roll closes");
        chk(WAIT(wf_fx_hk && wf_fx_gk && wf_fx_mk == 2, 120000) && LOGW("fx grain", 3000), "closed: the look built for shots again ('%s')",
            log_line("fx grain"));
        mod_deinit();
        return;
    }
    h_set_file_number(500);
    mod_init(1);
    chk(WAIT(wf_fx_hk && wf_fx_gk && wf_fx_mk == 2, 120000), "no roll: the look built for shots, as before ('%s')", log_line("fx grain"));
    h_prop_send(PROP_ASPECT, 0);
    disc_r = 60;
    gen_frame(in, FW, FH, 1);
    chk(WAIT(look_table_ready(), 10000), "table published");
    log_mark();
    int sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, in, NW) > 0 && LOGW("shot #1 ", 5000) && log_has(" + mist 0."),   /* (5D2: 2 lines) */
        "no roll: a shot gets the look, Mist in ('%s')", log_line("shot #"));
    dump_frame("frame_canon.ppm", in, FW, FH);
    dump_frame("frame_look_noroll.ppm", out, FW, FH);
    int rc = wf_roll_req_load(film, 3);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000), "a roll of 3 loads (%d)", rc);
    chk(WAIT(!wf_fx_hk && !wf_fx_gk && !wf_fx_mk && h_arena_live == 0, 20000), "a roll in the camera: the effects given back (%d blocks)",
        h_arena_live);
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(sb == 0 && ndiff(out, in, NW) == 0, "a roll frame: Canon's own JPEG, untouched (%d words differ)", ndiff(out, in, NW));
    dump_frame("frame_roll.ppm", out, FW, FH);
    chk(LOGW("roll frame", 3000) && !log_has("shot #") && !log_has(": film "), "logged as a roll frame, no look ('%s')",
        log_line("roll frame"));
    chk(roll_shoot(501, 3) && wf_roll_now()->state == WF_RS_FULL, "3 frames: the roll is full (%d/3)", wf_roll_now()->nshot);
    usleep(1000000);
    chk(!log_has("fx grain") && !log_has("fx halation") && !log_has("fx mist") && !wf_fx_hk && !wf_fx_gk && !wf_fx_mk,
        "nothing of the look built while the roll is in the camera");
    ui_hold = 1;                                 /* DryOS's table full, the UI task on its last slot (RAWB9 216.1 s) */
    h_task_slots = 1;
    int ut = task_create("wf_ui", 0x1B, 0x2000, (void *)ui_task_stand_in, 0);
    chk(ut > 0 && !(ut & 1) && h_task_slots == 0, "the UI task takes the table's last slot (%x)", ut);
    log_mark();
    h_lvp = 1;                                   /* the EOS M's pause: LiveView off, and the display with it (RAWB18) */
    h_disp_pause_off = 1;
    int on0 = h_disp_ons, r0 = h_task_refused, ds = wf_dark_start();
    chk(ds == 0 && !log_has("dark: no task") && h_task_refused == r0,
        "the Darkroom starts with the task table full (%d, %d task_create refused)", ds, h_task_refused - r0);
    chk(LOGW("dark: roll 001 start, 3 of 3 to develop", 5000), "the run goes ('%s')", log_line("dark: roll"));
    chk(LOGW("dark: look ", 120000) && strstr(log_line("dark: look "), "halation") && strstr(log_line("dark: look "), "grain")
        && strstr(log_line("dark: look "), ", mist") && !strstr(log_line("dark: look "), "mist -"),
        "the run builds the look first, the roll's Mist with it ('%s')", log_line("dark: look "));
    {
        struct wf_look lk;                       /* the prints' note: the look the run takes (wf_dark.c dk_frame step 4) */
        char tx[200];
        wf_look_now(&lk);
        const char *t = wf_exif_text(&lk, lk.grain, lk.halo, lk.bloom, lk.mist, WF_EV_NONE, tx, sizeof tx);
        chk(lk.mist == 2 && strstr(t, " | Halation Wide | Bloom Normal") && strstr(t, " | Mist 1/4"),
            "the run's print note: '%s'", t);
    }
    chk(WAIT(!wf_dark_busy(), 30000) && LOGW("dark: roll 001 end", 3000), "the run ends (host: no SRM, '%s')",
        log_line("dark: roll 001 end"));
    chk(log_has("dark: screen at the start: display 1 -> 0, LiveView 1 -> 0 (paused)"),
        "the screen: LiveView paused, the display went off ('%s')", log_line("dark: screen at the start"));
    chk(strstr(log_line("dark: display off after the pause"), "turned on again, display 1 after ") && h_disp_ons == on0 + 1,
        "the screen: turned on again after the pause ('%s', %d asks)", log_line("dark: display off"), h_disp_ons - on0);
    chk(log_has("dark: screen at the end: display 1 -> 1, LiveView 1 (resumed), display turned on 1 times") && lv && h_disp,
        "the screen: on at the end, LiveView back ('%s')", log_line("dark: screen at the end"));
    h_lvp = h_disp_pause_off = 0;
    chk(WAIT(!wf_fx_hk && !wf_fx_gk && !wf_fx_mk && h_arena_live == 0, 20000), "after the run, the roll still in: the effects given back (%d)",
        h_arena_live);
    ui_hold = 0;
    chk(WAIT(h_task_slots == 1, 2000), "the UI task gave its slot back");
    h_task_slots = -1;
    mod_deinit();
}
/* ROLL:mem - the owner's RAWB17 EOS M (11-16): every Darkroom roll develop ended "no memory" ("dark: srm chunks
 * 46000064+31888K", "dark: memory raw 6000064 frame 0 (srm)", "0 developed, 3 left"): ONE SRM chunk of 31,888 KB (the
 * raw fits, 53 KB left) and no 35 MB block of shoot memory for the frame. Here srm_malloc_suite gives that layout
 * (h_srm_n chunks side by side, the camera's uncached alias) and shoot memory has no block over h_shoot_max; three real
 * RAW+L pairs of the camera (dk_t_dir: a landscape and two portraits, Orientation 6 / 8; read only, never committed) through
 * the whole run in the module, with the owner's print finishes (frame 1, info 1, date 2). Each roll is shot to its end
 * (FULL, as the owner's): the run rewinds it (RAWB17 left a FULL roll FULL: "0 developed, 0 failed, 3 left", no reason).
 *   short: no shoot memory at all: still "no memory", now with the "memory short" lines naming what was short;
 *   full:  three SRM chunks side by side: the frame in one block after the raw (the old way), 3 prints;
 *   split: the camera's one chunk, shoot blocks up to 16 MB: the frame split (head from shoot memory, tail over the
 *          spent raw), each print = the one-block print byte for byte after Canon's APP1 (only our comment there names
 *          the roll); grain off (its seed is per shot), halation + bloom + mist on;
 *   chunks: no shoot block over DK_T_SHOOT (EOS M 6 MB, 5D2 7 MB: halation's state fits, the head not) and the shoot
 *          suite in 640 KB chunks: the head in 12 (EOS M) / 13 (5D2) pieces (bands and thumbnail runs cross them), each
 *          print = the one-block print byte for byte;
 *   owner: the owner's look (film table, halation + bloom, grain, mist), one chunk, shoot blocks up to DK_T_SHOOT only:
 *          the head from a shoot suite's chunks, 3 of 3 developed. */
static int mem_copy(const char *src, const char *dst)
{
    FILE *a = fopen(src, "rb"), *b = a ? fopen(dst, "wb") : 0;
    static char buf[1 << 16];
    size_t k;
    int ok = a && b;
    while (ok && (k = fread(buf, 1, sizeof buf, a)) > 0)
        ok = fwrite(buf, 1, k, b) == k;
    if (a) fclose(a);
    if (b) fclose(b);
    return ok;
}
static int mem_shoot(const char *dir, int from)
{
    int ok = 1;
    for (int i = 0; i < 3 && ok; i++) {
        char s[300], d[64];
        int n0 = wf_roll_now()->nshot;
        snprintf(s, sizeof s, "%s/%s.JPG", dir, mem_pairs[i]);
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.JPG", from + i);
        ok = mem_copy(s, d);
        snprintf(s, sizeof s, "%s/%s.CR2", dir, mem_pairs[i]);
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.CR2", from + i);
        ok = ok && mem_copy(s, d);
        h_set_file_number(from + i);
        ok = ok && WAIT(wf_roll_now()->nshot == n0 + 1, 5000);
    }
    return ok;
}
static int mem_roll(int film, int num, const char *dir, int from)   /* a new roll of 3 with the three pairs */
{
    const struct wf_roll *r = wf_roll_now();
    if (r->state != WF_RS_EMPTY && r->state != WF_RS_DONE && (wf_roll_req_close() != 0
                                                             || !WAIT(wf_roll_now()->state == WF_RS_DONE, 5000)))
        return 0;
    return wf_roll_req_load(film, 3) == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->num == num, 5000)
        && mem_shoot(dir, from) && wf_roll_now()->state == WF_RS_FULL;
}
static int mem_run(const char *what, int num)    /* one Darkroom run: 0 = it ended (lines after the mark) */
{
    char e[40];
    snprintf(e, sizeof e, "dark: roll %03d end", num);
    log_mark();
    int ds = wf_dark_start();
    chk(ds == 0 && LOGW("dark: roll ", 5000), "%s: the run starts (%d)", what, ds);
    int ok = WAIT(log_has(e) && !wf_dark_busy(), 1500000);
    chk(ok, "%s: the run ends ('%s')", what, log_line(e));
    return ok ? 0 : -1;
}
static long mem_after_app1(const unsigned char *b, long n)   /* the first byte after the APP1 segment, 0 = none */
{
    long o = 2;
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8)
        return 0;
    while (o + 4 <= n && b[o] == 0xFF && b[o + 1] != 0xDA) {
        long len = (long)b[o + 2] << 8 | b[o + 3];
        if (b[o + 1] == 0xE1)
            return o + 2 + len;
        o += 2 + len;
    }
    return 0;
}
static int mem_dims(const unsigned char *b, long n, int *w, int *h)   /* the print's SOF0 (after the APP1: its thumbnail has one) */
{
    for (long o = mem_after_app1(b, n); o > 0 && o + 9 < n; o++)
        if (b[o] == 0xFF && b[o + 1] == 0xC0) {
            *h = b[o + 5] << 8 | b[o + 6];
            *w = b[o + 7] << 8 | b[o + 8];
            return 1;
        }
    return 0;
}
static char *mem_print(int num, int i, int file, long *n)
{
    char p[64];
    snprintf(p, sizeof p, "ROLLS/ROLL%03d/F%02d_%04d.JPG", num, i + 1, file);
    return get_file(p, n);
}
static void mem_prints(const char *what, int num, int from, char **keep, long *keepn)
{
    for (int i = 0; i < 3; i++) {
        long n = 0;
        char *b = mem_print(num, i, from + i, &n);
        int w = 0, h = 0, eoi = b && n > 2 && (unsigned char)b[n - 2] == 0xFF && (unsigned char)b[n - 1] == 0xD9;
        int dims = b && mem_dims((unsigned char *)b, n, &w, &h);
        chk(b && n > (1 << 20) && eoi && dims && w > FW && h > FH && mem_after_app1((unsigned char *)b, n) > 0,
            "%s: F%02d print %ld KB, %dx%d (a frame round the %d x %d picture), APP1 + EOI", what, i + 1, n >> 10, w, h, FW,
            FH);
        if (keep) {
            keep[i] = b;
            keepn[i] = n;
        } else
            free(b);
    }
}
/* roll num's three prints = the one-block prints, byte for byte after Canon's APP1 (only our comment there names the roll) */
static void mem_same(const char *what, int num, int from, char **ref, const long *refn)
{
    for (int i = 0; i < 3; i++) {
        long n = 0;
        char *b = mem_print(num, i, from + i, &n);
        long a = b ? mem_after_app1((unsigned char *)b, n) : 0, ar = ref[i] ? mem_after_app1((unsigned char *)ref[i], refn[i]) : 0;
        chk(b && ref[i] && a > 0 && ar > 0 && n - a == refn[i] - ar && !memcmp(b + a, ref[i] + ar, (size_t)(n - a)),
            "%s: F%02d (%s) = the one-block print byte for byte after APP1 (%ld / %ld B)", what, i + 1, mem_pairs[i], n - a,
            refn[i] - ar);
        free(b);
    }
}
/* the head's pieces when a shoot suite gives chunks of csz bytes (dk_split: whole rows of each, 32-byte aligned) */
static int mem_pieces(int K, int csz)
{
    int rows = (csz - 31) / (FW * 2);
    return rows > 0 ? (K + rows - 1) / rows : 0;
}
static int mem_k(const char *ms)                 /* the K of "frame split at row K" */
{
    const char *kp = strstr(ms, "frame split at row ");
    return kp ? atoi(kp + 19) : 0;
}
static void t_roll_mem(void)
{
    const char *dir = dk_t_dir();
    char t[300];
    if (!*dir) {
        chk(1, "SKIPPED: no %s RAW+L pairs (WF_DARK_CR2)", DK_T_CAM);
        return;
    }
    std_pack();
    const char *pd = getenv("WF_PRINT_DAT");     /* the build's WFPRINT.DAT (test.sh): the caption's font pack, as on the card */
    int cap = pd && access(pd, R_OK) == 0 && mem_copy(pd, "ML/DATA/WFPRINT.DAT");
    chk(1, "%s", cap ? "WFPRINT.DAT on the card: prints with the caption" : "no WFPRINT.DAT (WF_PRINT_DAT): no caption");
    const int film = wf_films[fC].id;
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=halation\nbloom=normal\nmist=w4\n", film);
    wf_fx_retry_ms = 500;
    h_set_folder_number(100);
    h_set_file_number(500);
    mod_init(1);
    wf_pr_frame = 1;                             /* the owner's log: "print frame 1 info 1 date 2" */
    wf_pr_info = 1;
    wf_pr_date = 2;
    chk(mem_roll(film, 1, dir, 501), "roll 001: 3 real RAW+L frames shot (%d)", wf_roll_now()->nshot);

    /* short: no shoot memory, the camera's one SRM chunk */
    h_srm_n = 1;
    h_shoot_fail = 1;
    mem_run("short", 1);
    chk(log_has("dark: srm chunks ") && strstr(log_line("dark: srm chunks"), DK_T_SRMK), "short: the camera's chunk ('%s')",
        log_line("dark: srm chunks"));
    snprintf(t, sizeof t, "dark: memory short: raw %d KB ok, frame %d KB in one block none",
             (int)((DK_T_RAW + 4096u) >> 10), (int)(((uint32_t)FW * 2u * FH) >> 10));
    chk(log_has(t) && strstr(log_line("memory short: split head "), "biggest shoot block 0 KB") != 0,
        "short: the lines name what was short ('%s' / '%s')", log_line("memory short: raw"), log_line("memory short: split"));
    chk(!log_has("no frame to develop"), "short: a full roll is rewound for the run");
    chk(strstr(log_line("dark: roll 001 end"), "0 developed, 0 failed, 3 left") && strstr(log_line("dark: roll 001 end"),
        ": no memory"), "short: no memory, nothing lost ('%s')", log_line("dark: roll 001 end"));
    h_shoot_fail = 0;

    /* full: three SRM chunks side by side, shoot blocks up to 16 MB: the frame in one block after the raw */
    h_shoot_max = 16 << 20;
    h_srm_n = 3;
    usleep(800000);                              /* past wf_fx_retry_ms: the run builds the effects the short run could not */
    mem_run("full", 1);
    chk(log_has("dark: memory raw ") && strstr(log_line("dark: memory raw"), "(srm)")
        && !log_has("frame split"), "full: the frame in one block ('%s')", log_line("dark: memory raw"));
    chk(strstr(log_line("dark: roll 001 end"), "3 developed, 0 failed, 0 left") != 0, "full: 3 of 3 ('%s')",
        log_line("dark: roll 001 end"));
    chk(strstr(log_line("dark: F01 IMG_"), ", wb auto,") && strstr(log_line("dark: F03 IMG_"), ", wb auto,")
        && !log_has("no Auto WB levels"), "full: Light Auto = Canon's Auto WB of each CR2 ('%s')", log_line("dark: F01 IMG_"));
    chk(log_has("dev time: Darkroom "), "full: each print's develop time logged ('%s')", log_line("dev time: Darkroom"));
    chk(strstr(log_line("dark: look ready"), "halation + bloom normal") && strstr(log_line("dark: look ready"), ", mist")
        && !strstr(log_line("dark: look ready"), " -"), "full: the look's effects in ('%s')", log_line("dark: look ready"));
    char *ref[3] = { 0, 0, 0 };
    long refn[3] = { 0, 0, 0 };
    mem_prints("full", 1, 501, ref, refn);

    /* split: the camera's layout (one SRM chunk: EOS M 31,888 KB, 5D2 38,720 KB), shoot blocks up to 16 MB */
    h_srm_n = 1;
    chk(mem_roll(film, 2, dir, 504), "roll 002: the same 3 pairs again (%d)", wf_roll_now()->nshot);
    h_lvp = h_disp_pause_off = 1;                /* the camera's pause: LiveView off, the display off with it (RAWB18) */
    mem_run("split", 2);
    chk(log_has("dark: screen at the start: display 1 -> 0, LiveView 1 -> 0 (paused)")
        && strstr(log_line("dark: display off after the pause"), "display 1 after ") && !log_has("dark: display off before")
        && log_has("display turned on 1 times"), "split: the display turned on again after the pause, on for all 3 frames "
        "('%s')", log_line("dark: screen at the end"));
    h_lvp = h_disp_pause_off = 0;
    const char *ms = log_line("frame split at row ");
    int K = mem_k(ms);
    chk(K > 0 && K % 64 == 0 && K <= 1024 && strstr(ms, "from shoot (1 piece"), "split: the head of %d rows from one shoot "
        "block ('%s')", K, ms);
    chk(strstr(log_line("dark: roll 002 end"), "3 developed, 0 failed, 0 left") != 0, "split: 3 of 3 ('%s')",
        log_line("dark: roll 002 end"));
    chk(log_has("date stamp ") && !log_has("not drawn") && log_has("dark: F01 frame white"), "split: frame + date stamp drawn");
    chk(!cap || (log_has("dark: F01 caption ") && log_has("dark: F03 caption ") && !log_has("scratch: none")),
        "split: the caption drawn (WFPRINT.DAT read into the scratch after the tail: '%s')", log_line("dark: F01 caption"));
    mem_same("split", 2, 504, ref, refn);

    /* chunks: no shoot block for the head (6 MB at most: halation's 5.7 MB fits, a 7 MB head does not) and the shoot
       suite scattered in 640 KB chunks: the head in 12 pieces of 63 rows, so the encoder's 16-row bands and the
       thumbnails' runs cross pieces; each print = the one-block print byte for byte after APP1 */
    h_shoot_max = DK_T_SHOOT;
    h_shoot_chunk = 640 << 10;
    chk(mem_roll(film, 3, dir, 507), "roll 003: 3 frames (%d)", wf_roll_now()->nshot);
    h_lvp = h_disp_pause_off = h_disp_stuck = 1; /* and a display that does not come back: the run goes on dark */
    mem_run("chunks", 3);
    chk(strstr(log_line("dark: display off after the pause"), "display 0 after ")
        && strstr(log_line("dark: display off before F02"), "(not asked again this run)") && !log_has("display off before F03")
        && strstr(log_line("dark: display off after the run"), "display 0 after ")
        && log_has("dark: screen at the end: display 0 -> 0, LiveView 1 (resumed), display turned on 4 times"),
        "chunks: a display that stays off: asked 3 times in a row, then not, once more after the run ('%s')",
        log_line("dark: screen at the end"));
    h_lvp = h_disp_pause_off = h_disp_stuck = 0;
    h_disp = 1;
    K = mem_k(log_line("frame split at row "));
    snprintf(t, sizeof t, "from shoot chunks (%d pieces", mem_pieces(K, 640 << 10));
    chk(K > 0 && strstr(log_line("frame split at row "), t) != 0 && mem_pieces(K, 640 << 10) >= 10, "chunks: the head of %d "
        "rows in %d pieces ('%s')", K, mem_pieces(K, 640 << 10), log_line("frame split at row "));
    chk(strstr(log_line("dark: roll 003 end"), "3 developed, 0 failed, 0 left") != 0, "chunks: 3 of 3 ('%s')",
        log_line("dark: roll 003 end"));
    mem_same("chunks", 3, 507, ref, refn);
    for (int i = 0; i < 3; i++)
        free(ref[i]);
    h_shoot_chunk = 0;

    /* owner: the owner's look with grain, shoot blocks up to DK_T_SHOOT (the head in two chunks of the suite) */
    wf_grain = 2;
    chk(mem_roll(film, 4, dir, 510), "roll 004: 3 frames (%d)", wf_roll_now()->nshot);
    mem_run("owner", 4);
    K = mem_k(log_line("frame split at row "));
    snprintf(t, sizeof t, "from shoot chunks (%d pieces", mem_pieces(K, DK_T_SHOOT));
    chk(K > 0 && strstr(log_line("frame split at row "), t) != 0 && mem_pieces(K, DK_T_SHOOT) == 2, "owner: the head from a "
        "shoot suite's chunks ('%s')", log_line("frame split at row "));
    chk(strstr(log_line("dark: look ready"), "halation + bloom normal, grain") && !strstr(log_line("dark: look ready"), " -"),
        "owner: the owner's look built ('%s')", log_line("dark: look ready"));
    chk(strstr(log_line("dark: roll 004 end"), "3 developed, 0 failed, 0 left") != 0, "owner: 3 of 3 ('%s')",
        log_line("dark: roll 004 end"));
    mem_prints("owner", 4, 510, 0, 0);
    h_srm_n = 0;
    h_shoot_max = 0;
    mod_deinit();
}

/* ROLL:jpeg - Camera JPEG Overwrite and the logo (owner 11-16) on the three real pairs, the whole run in the module
 * (memory as ROLL:mem full: three SRM chunks side by side):
 *   keep:  the default: each camera JPEG untouched, each print with our Exif thumbnail in Canon's slot (not Canon's);
 *   copy:  frame Off, Overwrite: each IMG_nnnn.JPG = its roll print byte for byte, the CR2s untouched, no .WFT left;
 *   frame: a white frame + info + roll + date and the logo pack on the card (WF_LOGO_DAT: test.sh makes it from our own
 *          tests/host/logo_test.svg, keyed "Canon"), Overwrite: the logo lines; each IMG_nnnn.JPG a 5184 x 3456 JPEG of
 *          the picture (Canon's size, our comment, our thumbnail); one JPEG gone before the run: that frame printed,
 *          no JPEG made for it; the CR2s untouched, no .WFT left.
 *   pictures (owner 11-17: "sao jpg nó overwrite rồi mà preview vẫn là ảnh chưa qua xử lí", Canon's colours in
 *          playback even after a power cycle; DESIGN 3.9 "Playback of an overwritten RAW+L pair"): every overwritten
 *          IMG_nnnn.JPG (copy, frame, both) holds only our two pictures and nothing of Canon's (jp_pics); the CR2
 *          beside it still carries Canon's own thumbnail (copy), so Canon's previews of the frame are only there. */
static int jp_same(const char *a, const char *b)  /* two files byte for byte */
{
    long na = 0, nb = 0;
    char *x = get_file(a, &na), *y = get_file(b, &nb);
    int ok = x && y && na == nb && na > 0 && !memcmp(x, y, (size_t)na);
    free(x);
    free(y);
    return ok;
}
static int jp_cr2s(const char *dir, int from)    /* the three CR2s on the card = the pairs' own */
{
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        char s[300], d[64];
        snprintf(s, sizeof s, "%s/%s.CR2", dir, mem_pairs[i]);
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.CR2", from + i);
        ok &= jp_same(s, d);
    }
    return ok;
}
static int jp_wft(int from)                      /* no IMG_nnnn.WFT left */
{
    int n = 0;
    for (int i = 0; i < 3; i++) {
        char d[64];
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.WFT", from + i);
        n += access(d, F_OK) == 0;
    }
    return n;
}
/* the Exif thumbnail in a JPEG's APP1 (after SOI): 0 = none; *len its length */
static const unsigned char *jp_thumb(const unsigned char *b, long n, uint32_t *len)
{
    long e = mem_after_app1(b, n);
    if (e <= 2 || b[2] != 0xFF || b[3] != 0xE1)
        return 0;
    uint32_t off = 0, room = 0, al = (uint32_t)(e - 2);
    if (wf_dk_xthumb_slot(b + 2, al, &off, &room) != 0)
        return 0;
    const unsigned char *t = b + 2 + 10;         /* its length: the segments to SOS, then the coded data to EOI */
    uint32_t k = off - 10, i = k + 2, tn = al - 10;
    *len = 0;
    while (i + 4 <= tn && t[i] == 0xFF && t[i + 1] != 0xDA)
        i += 2 + (uint32_t)(t[i + 2] << 8 | t[i + 3]);
    if (i + 4 <= tn && t[i] == 0xFF)
        i += 2 + (uint32_t)(t[i + 2] << 8 | t[i + 3]);
    for (; i + 1 < tn; i++)
        if (t[i] == 0xFF && t[i + 1] == 0xD9) {
            *len = i + 2 - k;
            break;
        }
    return b + 2 + off;
}
/* the pictures in a DCIM JPEG after Overwrite (owner 11-17, playback still Canon's): two image starts only (the main
 * image's SOI and IFD1's thumbnail), no APP2 (MPF: a second picture), nothing after the main image's EOI, the thumbnail
 * 160 x 120 with Y 2x1 like Canon's, and nothing of the pair's own JPEG c: neither its thumbnail nor the first 4 KB of
 * its scan anywhere in the file (a real EOS M JPEG holds exactly those two pictures: tools/jpg_pictures.py). 0 = all
 * hold, else the first that failed. */
static const char *jp_pics(const unsigned char *b, long n, const unsigned char *c, long cn)
{
    if (!b || n < 65536 || !c || cn < 65536)
        return "a file missing";
    int st = 0;
    for (long o = 0; o + 2 < n; o++)
        st += b[o] == 0xFF && b[o + 1] == 0xD8 && b[o + 2] == 0xFF;
    if (st != 2)
        return "not two image starts";
    long o = 2, sos = 0;
    while (o + 4 <= n && b[o] == 0xFF) {
        if (b[o + 1] == 0xE2)
            return "an APP2";
        if (b[o + 1] == 0xDA) {
            sos = o;
            break;
        }
        o += 2 + (b[o + 2] << 8 | b[o + 3]);
    }
    if (!sos)
        return "no SOS";
    long e = sos;
    while (e + 1 < n && !(b[e] == 0xFF && b[e + 1] == 0xD9))
        e++;
    if (e + 2 != n)
        return "bytes after the EOI";
    uint32_t tl = 0, tc = 0;
    const unsigned char *t = jp_thumb(b, n, &tl), *tq = jp_thumb(c, cn, &tc);
    if (!t || !tq || tl < 1000 || tc < 1000)
        return "no thumbnail";
    int tw = 0, th = 0, ty = 0;
    for (uint32_t i = 2; i + 12 < tl && t[i] == 0xFF; i += 2 + (uint32_t)(t[i + 2] << 8 | t[i + 3]))
        if (t[i + 1] == 0xC0) {
            th = t[i + 5] << 8 | t[i + 6];
            tw = t[i + 7] << 8 | t[i + 8];
            ty = t[i + 11];
            break;
        }
    if (tw != 160 || th != 120 || ty != 0x21)
        return "the thumbnail not 160 x 120 Y 2x1";
    if (memmem(b, (size_t)n, tq, tc))
        return "Canon's thumbnail in it";
    long cs = 2;
    while (cs + 4 <= cn && c[cs] == 0xFF && c[cs + 1] != 0xDA)
        cs += 2 + (c[cs + 2] << 8 | c[cs + 3]);
    if (cs + 4 > cn)
        return "no scan in Canon's JPEG";
    cs += 2 + (c[cs + 2] << 8 | c[cs + 3]);
    if (cs + 4096 > cn)
        return "no scan in Canon's JPEG";
    if (memmem(b, (size_t)n, c + cs, 4096))
        return "Canon's scan in it";
    return 0;
}
/* the fitted print's resample (wf_pf_scale) on a 600 x 400 test picture: its rows, and the exact area mean of an output
   pixel's source area (doubles) */
static uint8_t *sc_img;
static uint32_t *sc_row(void *c, int y) { (void)c; return (uint32_t *)(sc_img + y * 600 * 2); }
static double sc_area(int x, int y, int dw, int dh)
{
    double a = x * 600.0 / dw, b = (x + 1) * 600.0 / dw, c = y * 400.0 / dh, d = (y + 1) * 400.0 / dh, s = 0;
    for (int j = (int)c; j < d; j++) {
        double wy = fmin(d, j + 1) - fmax(c, j);
        for (int k = (int)a; k < b; k++)
            s += (fmin(b, k + 1) - fmax(a, k)) * wy * sc_img[(j * 600 + k) * 2 + 1];
    }
    return s / ((b - a) * (d - c));
}

/* the log line of a print fitted into Canon's size (wf_dark.c dk_fit): frame fr, Info = roll, the shot's orientation */
static const char *jp_fitline(char *b, size_t n, int i, int fr, int orient)
{
    struct wf_pf_lay P;
    b[0] = 0;
    if (!wf_pf_fit(&P, fr, WF_PIN_ROLL, FW, FH, orient, FW, FH))
        return b;
    int pw = P.iw, ph = P.ih;
    wf_pf_pad(&P, FW, FH);
    snprintf(b, n, "dark: F%02d camera JPEG: the print fitted %dx%d, picture %dx%d at %d,%d, logo", i + 1, FW, FH, pw, ph,
             P.ix, P.iy);
    return b;
}
/* the Prints screens' pictures (modernUI M5): ROLLS/ROLLnnn/Fnn_nnnn.WFP beside each of a roll's n prints, the layout
 * of wf_dark.h (head, contact sheet picture, screen picture), the print in it (bars black, the picture not flat, the
 * sheet its 4 x 4 means), and wf_prints_scan lists them */
static void prv_check(int num, int from, int n, const char *tag)
{
    for (int i = 0; i < n; i++) {
        char p[64];
        long sz = 0;
        snprintf(p, sizeof p, "ROLLS/ROLL%03d/F%02d_%04d.WFP", num, i + 1, from + i);
        unsigned char *b = (unsigned char *)get_file(p, &sz);
        struct wf_prv_head h;
        if (b && sz >= (long)sizeof h)
            memcpy(&h, b, sizeof h);
        int ok = b && sz == (long)WF_PRV_BYTES && wf_prv_head_ok(&h) && h.frame == i && h.film == wf_roll_now()->film;
        chk(ok, "%s: F%02d %s (%ld B, want %u)", tag, i + 1, p, sz, (unsigned)WF_PRV_BYTES);
        if (!ok) {
            free(b);
            continue;
        }
        const uint32_t *sh = (const uint32_t *)(b + WF_PRV_SHEET_OFF), *pic = (const uint32_t *)(b + WF_PRV_PIC_OFF);
        int lo = 255, hi = 0, bar = 0, sheet_off = 0;
        for (int y = 0; y < WF_PRV_H; y++)
            for (int x = 0; x < WF_PRV_W / 2; x++) {
                uint32_t q = pic[y * (WF_PRV_W / 2) + x];
                int in = y >= h.py && y < h.py + h.ph && x * 2 >= h.px && x * 2 < h.px + h.pw;
                int a = (q >> 8) & 0xFF, c = q >> 24;
                if (!in && q)
                    bar++;
                if (in) {
                    lo = a < lo ? a : lo;
                    hi = c > hi ? c : hi;
                }
            }
        for (int y = 0; y < WF_PRV_SH; y++)
            for (int x = 0; x < WF_PRV_SW / 2; x++) {
                int s = 0;
                for (int j = 0; j < 4; j++)
                    for (int k = 0; k < 4; k++) {
                        uint32_t q = pic[(y * 4 + j) * (WF_PRV_W / 2) + x * 4 + k];
                        s += (int)((q >> 8) & 0xFF) + (int)(q >> 24);
                    }
                int m = (s + 16) / 32, t = (int)((sh[y * (WF_PRV_SW / 2) + x] >> 8) & 0xFF)
                                          + (int)(sh[y * (WF_PRV_SW / 2) + x] >> 24);
                if (abs(t / 2 - m) > 2)
                    sheet_off++;
            }
        chk(h.pw >= 300 && h.ph >= 180 && !bar && hi - lo > 40 && !sheet_off
            && (h.orient == 1 || h.orient == 3 || h.orient == 6 || h.orient == 8),
            "%s: F%02d picture %dx%d at %d,%d, o%d, Y %d..%d, bars %d, sheet off %d", tag, i + 1, h.pw, h.ph, h.px, h.py,
            h.orient, lo, hi, bar, sheet_off);
        free(b);
    }
    char want[48];
    snprintf(want, sizeof want, "dark: F01 Prints picture F01_%04d.WFP, %d KB, o", from, (int)(WF_PRV_BYTES >> 10));
    chk(log_has(want), "%s: '%s'", tag, log_line("Prints picture"));
    static struct wf_prints ps;
    int rc = wf_prints_scan(&ps), at = -1;
    for (int k = 0; k < ps.n; k++)
        if (ps.r[k].num == num && ps.r[k].dev == 1)
            at = k;
    char p0[96] = "";
    if (at >= 0)
        wf_prints_path(&ps.r[at], 0, p0, sizeof p0);
    char w0[48];
    snprintf(w0, sizeof w0, "ROLLS/ROLL%03d/F01_%04d.WFP", num, from);
    chk(!rc && at >= 0 && ps.r[at].n == n && ps.r[at].fr[0] == 1 && ps.r[at].file[n - 1] == from + n - 1
        && strstr(p0, w0), "%s: wf_prints_scan rc %d, %d rolls, roll %03d at %d (%d frames), %s", tag, rc, ps.n, num, at,
        at >= 0 ? ps.r[at].n : -1, p0);
}

static void t_roll_jpeg(void)
{
    {   /* Camera JPEG Overwrite with a frame (owner 11-17): the print fitted into 5184 x 3456 (wf_pf_fit / wf_pf_pad)
           and its picture's area resample (wf_pf_scale) */
        struct wf_pf_lay P;
        int f1 = wf_pf_fit(&P, WF_PFR_WHITE, WF_PIN_ROLL, 5184, 3456, 1, 5184, 3456);
        int s1 = P.iw == 4320 && P.ih == 2880 && P.cw == 4512 && P.ch == 3456 && P.ix == 96 && P.ty == 2976;
        wf_pf_pad(&P, 5184, 3456);
        int p1 = P.cw == 5184 && P.ch == 3456 && P.ix == 432 && P.iy == 96 && P.tx == 432 && P.ty == 2976;
        int f6 = wf_pf_fit(&P, WF_PFR_BLACK, WF_PIN_ROLL, 5184, 3456, 6, 5184, 3456);
        int s6 = P.iw == 4560 && P.ih == 3040 && P.cw == 5168 && P.ch == 3232;
        wf_pf_pad(&P, 5184, 3456);
        int p6 = P.ix == 104 && P.iy == 208 && P.cw == 5184 && P.ch == 3456;
        int f0 = wf_pf_fit(&P, WF_PFR_OFF, WF_PIN_ROLL, 5184, 3456, 1, 5184, 3456), all = 0, nall = 0;
        static const int ors[4] = { 1, 3, 6, 8 };
        for (int fr = WF_PFR_WHITE; fr < WF_PFR_N; fr++)
            for (int in = 0; in < WF_PIN_N; in++)
                for (int k = 0; k < 4; k++) {
                    struct wf_pf_lay Q;
                    nall++;
                    if (!wf_pf_fit(&Q, fr, in, 5184, 3456, ors[k], 5184, 3456) || Q.iw * 2 <= 5184 || (Q.iw & 1))
                        continue;
                    wf_pf_pad(&Q, 5184, 3456);
                    all += Q.cw == 5184 && Q.ch == 3456 && !(Q.ix & 1) && Q.ix >= 0 && Q.iy >= 0 && Q.ix + Q.iw <= 5184
                           && Q.iy + Q.ih <= 3456
                           && (!Q.tw || (!(Q.tx & 1) && Q.tx + Q.tw <= 5184 && Q.ty + Q.th <= 3456));
                }
        chk(f1 && s1 && p1 && f6 && s6 && p6 && !f0 && all == nall, "fit: landscape %d %d %d, portrait %d %d %d, "
            "Off %d, every frame / info / orientation %d of %d inside 5184x3456", f1, s1, p1, f6, s6, p6, !f0, all,
            nall);
        sc_img = malloc(600 * 400 * 2);
        uint32_t sn = wf_pf_scale_bytes(500);
        uint8_t *mem = malloc(sn);
        for (int y = 0; sc_img && y < 400; y++)
            for (int x = 0; x < 600; x++) {
                sc_img[(y * 600 + x) * 2 + 1] = (uint8_t)((x * 37 + y * 11 + (x * y >> 7)) & 255);
                sc_img[(y * 600 + x) * 2] = (uint8_t)(x & 1 ? -20 : 10);   /* U 10, V -20 */
            }
        struct wf_pf_scale S;
        int ok = sc_img && mem && wf_pf_scale_init(&S, sc_row, 0, 600, 400, 500, 334, mem, sn) == 0, flat = 1;
        double worst = 0;
        for (int y = 0; ok && y < 334; y++) {
            const uint8_t *r = (const uint8_t *)wf_pf_scale_row(&S, y);
            for (int x = 0; x < 500; x++) {
                double e = fabs(r[2 * x + 1] - sc_area(x, y, 500, 334));
                if (e > worst)
                    worst = e;
                if (!(x & 1) && ((int8_t)r[2 * x] != 10 || (int8_t)r[2 * x + 2] != -20))
                    flat = 0;
            }
        }
        int half = mem && wf_pf_scale_init(&S, sc_row, 0, 600, 400, 300, 334, mem, sn) == 0;
        int small = mem && wf_pf_scale_init(&S, sc_row, 0, 600, 400, 500, 334, mem, sn - 4) == 0;
        chk(ok && worst <= 1.25 && flat && !half && !small, "scale: 600x400 to 500x334 within %.2f of the area mean, "
            "chroma flat %d, half size refused %d, short memory refused %d", worst, flat, !half, !small);
        free(sc_img);
        free(mem);
        sc_img = 0;
    }
    const char *dir = dk_t_dir();
    char fl1[120], fl2[120];
    if (!*dir) {
        chk(1, "SKIPPED: no %s RAW+L pairs (WF_DARK_CR2)", DK_T_CAM);
        return;
    }
    std_pack();
    const char *pd = getenv("WF_PRINT_DAT"), *ld = getenv("WF_LOGO_DAT");
    int cap = pd && access(pd, R_OK) == 0 && mem_copy(pd, "ML/DATA/WFPRINT.DAT");
    chk(cap, "WFPRINT.DAT on the card (WF_PRINT_DAT %s)", pd ? pd : "-");
    const int film = wf_films[fC].id;
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nbloom=off\nmist=off\n", film);
    wf_fx_retry_ms = 500;
    h_set_folder_number(100);
    h_set_file_number(500);
    mod_init(1);
    h_shoot_max = 16 << 20;
    h_srm_n = 3;

    /* keep: the default */
    wf_pr_frame = 0;
    wf_pr_info = 1;
    wf_pr_date = 0;
    chk(wf_pr_jpeg == WF_PJ_KEEP, "Camera JPEG: Keep by default (%d)", wf_pr_jpeg);
    chk(mem_roll(film, 1, dir, 501), "roll 001: 3 real RAW+L frames shot (%d)", wf_roll_now()->nshot);
    /* and Screen On's keep-alive (owner 11-17, RAWB23: Canon turned the EOS M's display off in the middle of a frame and
     * it stayed dark until the next frame): the test stands in for the Darkroom screen's tick (wf_dark_keep, ui 1) while
     * F02 runs; its times run ahead of the clock (a look a second) */
    {
        log_mark();
        int ds = wf_dark_start();
        chk(ds == 0 && LOGW("dark: F01 screen: relit ", 600000), "keep: F01's screen line ('%s')",
            log_line("dark: F01 screen"));
        usleep(30000);                           /* F02 begun */
        int t = get_ms_clock(), on0 = h_disp_ons;
        int k0 = wf_dark_keep(t, 1);             /* on: nothing (and the worker leaves it to the UI from here) */
        h_disp = 0;                              /* Canon turns it off */
        int k1 = wf_dark_keep(t + 1000, 1), h1 = h_disp;   /* turned on again */
        int k2 = wf_dark_keep(t + 1500, 1);      /* within its second: not looked at */
        int k3 = wf_dark_keep(t + 2000, 1);      /* on: the ask took */
        h_disp_stuck = 1;                        /* off again, and it does not come back */
        h_disp = 0;
        int k4 = wf_dark_keep(t + 3000, 1), k5 = wf_dark_keep(t + 4000, 1), k6 = wf_dark_keep(t + 5000, 1);
        int k7 = wf_dark_keep(t + 6000, 1), k8 = wf_dark_keep(t + 7000, 1);   /* 3 asks that did not take: given up */
        int asks = h_disp_ons - on0;
        h_disp_stuck = 0;
        h_disp = 1;
        chk(!k0 && k1 && h1 && !k2 && !k3 && k4 && k5 && k6 && !k7 && !k8 && asks == 4,
            "screen on: relit %d (display %d), %d %d, stuck %d %d %d, given up %d %d, %d asks", k1, h1, k2, k3, k4, k5,
            k6, !k7, !k8, asks);
        int ok = WAIT(log_has("dark: roll 001 end") && !wf_dark_busy(), 1500000);
        chk(ok, "keep: the run ends ('%s')", log_line("dark: roll 001 end"));
        chk(strstr(log_line("dark: F02 screen: "), "relit 4, first off at ") && log_has("dark: F01 screen: relit 0")
            && !strstr(log_line("dark: F01 screen: "), "first off") && log_has("dark: F03 screen: relit 0")
            && log_has("dark: display off while F02 runs: turned on 3 times, it stays off (not asked again this run)")
            && log_has("dark: Screen On: display found off while frames ran, turned on again 4 times")
            && !log_has("dark: display off before"),
            "screen on: per frame ('%s', '%s'), the run ('%s')", log_line("dark: F01 screen"),
            log_line("dark: F02 screen"), log_line("dark: Screen On:"));
    }
    chk(strstr(log_line("dark: roll 001 start"), "jpeg 0 logo 0K") && strstr(log_line("dark: roll 001 end"),
        "3 developed, 0 failed, 0 left"), "keep: 3 of 3, jpeg 0 ('%s')", log_line("dark: roll 001 end"));
    for (int i = 0; i < 3; i++) {
        char s[300], d[64];
        long n = 0, nc = 0;
        snprintf(s, sizeof s, "%s/%s.JPG", dir, mem_pairs[i]);
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.JPG", 501 + i);
        char *b = mem_print(1, i, 501 + i, &n), *c = get_file(s, &nc);
        uint32_t tl = 0, tc = 0;
        const unsigned char *tp = b ? jp_thumb((unsigned char *)b, n, &tl) : 0;
        const unsigned char *tq = c ? jp_thumb((unsigned char *)c, nc, &tc) : 0;
        chk(jp_same(s, d), "keep: IMG_%04d.JPG untouched", 501 + i);
        chk(tp && tq && tl > 1000 && tl < 20000 && (tl != tc || memcmp(tp, tq, tl)) && tp[0] == 0xFF && tp[1] == 0xD8
            && tp[2] == 0xFF && tp[3] == 0xDB, "keep: F%02d print's Exif thumbnail ours (%u B, Canon's %u B)", i + 1, tl, tc);
        free(b);
        free(c);
    }
    chk(log_has("dark: F01 print Exif thumbnail 160x120 q"), "keep: '%s'", log_line("print Exif thumbnail"));
    chk(jp_cr2s(dir, 501) && !jp_wft(501), "keep: CR2s untouched, no .WFT");
    prv_check(1, 501, 3, "keep");

    /* copy: frame Off, Overwrite; and Screen Off (owner 11-16, DESIGN 3.11): the test stands in for the Darkroom screen's
     * tick (wf_dark_dark) and keys (wf_dark_wake; a key "WF_DS_WAKE_MS ago" = the time without a key is over) */
    int dc_keep = wf_dark_dcim_changed();        /* Keep: DCIM as it was (checked after roll 002) */
    int cat0 = h_cat_n, cr0 = h_cat_rescans;     /* Keep: Canon's catalog not asked either */
    wf_pr_jpeg = WF_PJ_OVER;
    wf_dk_scr = WF_DS_OFF;
    chk(mem_roll(film, 2, dir, 504), "roll 002: 3 frames (%d)", wf_roll_now()->nshot);
    {
        log_mark();
        int on0 = h_disp_ons, off0 = h_disp_offs, ds = wf_dark_start(), now = get_ms_clock();
        chk(ds == 0 && LOGW("dark: Screen Off: the display goes dark 10 s after the last key", 120000),
            "screen off: the run says so ('%s')", log_line("dark: Screen Off"));
        now = get_ms_clock();
        wf_dark_wake(now);                       /* a key now (the start may be more than 10 s back on the host) */
        int d0 = wf_dark_dark(now);              /* a key a moment ago: shown */
        int w0 = wf_dark_wake(now - WF_DS_WAKE_MS);   /* (screen shown: not a wake, the key's time set back) */
        int d1 = wf_dark_dark(now), h1 = h_disp; /* due: turned off */
        int w1 = wf_dark_wake(now), h2 = h_disp; /* a key on the dark screen: woken, the key does nothing else */
        int d2 = wf_dark_dark(now);              /* within its 10 s: shown */
        chk(!d0 && !w0 && d1 && !h1 && h_disp_offs == off0 + 1 && w1 && h2 && h_disp_ons == on0 + 1 && !d2,
            "screen off: shown %d, dark %d (display %d, %d offs), woken %d (display %d, %d ons), shown again %d", !d0, d1, h1,
            h_disp_offs - off0, w1, h2, h_disp_ons - on0, !d2);
        usleep(2100000);                         /* (asks to turn it off: 2 s apart) */
        now = get_ms_clock();
        wf_dark_wake(now - WF_DS_WAKE_MS);
        chk(wf_dark_dark(now) && !h_disp && h_disp_offs == off0 + 2, "screen off: dark again 10 s after the wake (%d offs)",
            h_disp_offs - off0);
        int ok = WAIT(log_has("dark: roll 002 end") && !wf_dark_busy(), 1500000);
        chk(ok, "copy: the run ends ('%s')", log_line("dark: roll 002 end"));
        chk(h_disp && !log_has("dark: display off before") && !log_has("dark: display off after the pause")
            && log_has("dark: Screen Off: display turned off 2 times, woken ") && !wf_dark_dark(get_ms_clock())
            && !log_has("dark: F01 screen:") && !wf_dark_keep(get_ms_clock(), 1),
            "screen off: dark for the frames, on at the end (display %d, '%s', '%s')", h_disp,
            log_line("dark: Screen Off: display turned"), log_line("dark: screen at the end"));
    }
    wf_dk_scr = WF_DS_ON;
    chk(strstr(log_line("dark: roll 002 start"), "jpeg 1") && strstr(log_line("dark: roll 002 end"),
        "3 developed, 0 failed, 0 left"), "copy: 3 of 3 ('%s')", log_line("dark: roll 002 end"));
    for (int i = 0; i < 3; i++) {
        char d[64], p[64], e[80], s[300], r[64];
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.JPG", 504 + i);
        snprintf(p, sizeof p, "ROLLS/ROLL002/F%02d_%04d.JPG", i + 1, 504 + i);
        snprintf(e, sizeof e, "dark: F%02d camera JPEG IMG_%04d.JPG overwritten: the print,", i + 1, 504 + i);
        chk(jp_same(d, p) && log_has(e), "copy: IMG_%04d.JPG = its roll print byte for byte ('%s')", 504 + i, log_line(e));
        /* the pictures (owner 11-17): ours only in the JPEG; Canon's thumbnail still in the CR2 (its previews) */
        snprintf(s, sizeof s, "%s/%s.JPG", dir, mem_pairs[i]);
        snprintf(r, sizeof r, "DCIM/100CANON/IMG_%04d.CR2", 504 + i);
        long bn = 0, cn = 0, rn = 0;
        char *bb = get_file(d, &bn), *cb = get_file(s, &cn), *rb = get_file(r, &rn);
        const char *why = jp_pics((unsigned char *)bb, bn, (unsigned char *)cb, cn);
        uint32_t tc = 0;
        const unsigned char *tq = cb ? jp_thumb((unsigned char *)cb, cn, &tc) : 0;
        chk(!why, "copy: IMG_%04d.JPG holds only our two pictures, nothing of Canon's (%s)", 504 + i, why ? why : "yes");
        if (i == 0) {                            /* the check itself: Canon's own JPEG is caught */
            const char *own = jp_pics((unsigned char *)cb, cn, (unsigned char *)cb, cn);
            chk(own && !strcmp(own, "Canon's thumbnail in it"), "pictures: Canon's own JPEG caught (%s)", own ? own : "no");
        }
        chk(rb && tq && tc > 1000 && memmem(rb, (size_t)rn, tq, tc), "copy: IMG_%04d.CR2 still carries Canon's own "
            "thumbnail (%u B): Canon's previews of the frame are only in the CR2", 504 + i, tc);
        free(bb);
        free(cb);
        free(rb);
    }
    chk(jp_cr2s(dir, 504) && !jp_wft(504), "copy: CR2s untouched, no .WFT");
    /* Canon's PLAY after a run that changed DCIM (owner 11-21, 5D2 bug 3: PLAY's list is Canon's file catalog, read at
     * card mount): roll 001 (Keep) left DCIM alone and asked nothing; roll 002 replaced its camera JPEGs, and after
     * each frame Canon's catalog read that number again (FM_GetObjectHandleByPath on the DCIM JPEG, FC_ReScanFile on
     * its handle). Done: no notice on the way into PLAY, the key Canon's */
    {
#ifdef WF_BUILD_5D2
        const uint32_t fh = 0xFF8453FCu, fr = 0xFF856A64u;
#else
        const uint32_t fh = 0xFF118EB0u, fr = 0xFF1167B0u;
#endif
        int pok = cat0 + 3 <= 8;
        for (int k = 0; pok && k < 3; k++) {
            char p[40];
            snprintf(p, sizeof p, "B:/DCIM/100CANON/IMG_%04d.JPG", 504 + k);
            pok = !strcmp(h_cat_path[cat0 + k], p) && h_cat_hseen[cr0 + k] == h_cat_h;
        }
        chk(cat0 == 0 && cr0 == 0 && h_cat_n == 3 && h_cat_rescans == 3 && pok && h_cat_fn[0] == fh && h_cat_fn[1] == fr
            && log_has("dark: F01 Canon's file list read again (PLAY shows the new print)")
            && !log_has("Canon's file list not read again"),
            "catalog: Keep %d / %d calls, Overwrite %d handles + %d rescans of IMG_0504..0506.JPG (%s, '%s'), "
            "at %x / %x",
            cat0, cr0, h_cat_n - cat0, h_cat_rescans - cr0, pok ? "the frames' JPEGs" : h_cat_path[0],
            log_line("Canon's file list"), (unsigned)h_cat_fn[0], (unsigned)h_cat_fn[1]);
        int hp = h_play, n0 = h_notify_n;
        h_play = 0;
        int k1 = cbr_key(WF_KEY_PLAY);
        h_play = hp;
        chk(!dc_keep && !wf_dark_dcim_changed() && k1 == 1 && h_notify_n == n0,
            "PLAY after the run, catalog told: Keep %d, Overwrite %d, into PLAY: passes %d, notices %d",
            dc_keep, wf_dark_dcim_changed(), k1, h_notify_n - n0);
    }

    /* frame: a white frame + details + the logo, Overwrite, one camera JPEG gone */
    int lg = ld && access(ld, R_OK) == 0 && mem_copy(ld, "ML/DATA/WFLOGO.DAT");
    chk(lg, "WFLOGO.DAT on the card (WF_LOGO_DAT %s)", ld ? ld : "-");
    wf_pr_frame = 1;
    wf_pr_info = 2;
    wf_pr_date = 2;
    chk(mem_roll(film, 3, dir, 507), "roll 003: 3 frames (%d)", wf_roll_now()->nshot);
    remove("DCIM/100CANON/IMG_0509.JPG");
    int cr3 = h_cat_rescans;
    h_cat_rc = 0x82110007u;                      /* Canon's catalog refuses: the notice is the way */
    mem_run("frame", 3);
    h_cat_rc = 0;
    /* each frame that changed DCIM asked (F03 has no camera JPEG: nothing changed, nothing asked); every refusal
     * logged; into PLAY the notice, the key still Canon's; in PLAY no notice */
    {
        int hp = h_play, n0 = h_notify_n;
        h_play = 0;
        int k1 = cbr_key(WF_KEY_PLAY), shown = h_notify_n == n0 + 1 && strstr(h_notify, "PLAY may miss new prints")
                                               && strstr(h_notify, "Card out and in, or power off/on");
        h_play = 1;
        int n1 = h_notify_n, k2 = cbr_key(WF_KEY_PLAY);
        h_play = hp;
        chk(h_cat_rescans - cr3 == 2 && wf_dark_dcim_changed()
            && log_has("dark: F01 Canon's file list not read again (Canon 82110007): PLAY may miss it till card out/in "
                       "or power off/on")
            && log_has("dark: F02 Canon's file list not read again") && !log_has("dark: F03 Canon's file list")
            && k1 == 1 && shown && k2 == 1 && h_notify_n == n1,
            "catalog refused: %d rescans, notice flag %d ('%s'), into PLAY: passes %d, notice %d ('%s'), out: %d, %d",
            h_cat_rescans - cr3, wf_dark_dcim_changed(), log_line("Canon's file list not"), k1, shown, h_notify, k2,
            h_notify_n - n1);
    }
    chk(strstr(log_line("dark: roll 003 end"), "3 developed, 0 failed, 0 left") != 0, "frame: 3 of 3 ('%s')",
        log_line("dark: roll 003 end"));
    chk(log_has("dark: F01 logo Canon ") && log_has("+ lettering " DK_T_MODEL) && log_has("dark: F02 logo Canon "),
        "frame: the logo drawn ('%s')", log_line("dark: F01 logo"));
    chk(log_has("dark: F01 caption ") && strstr(log_line("dark: F01 caption"), DK_T_MODEL) == 0,
        "frame: line 1 without the model the lettering names ('%s')", log_line("dark: F01 caption"));
    for (int i = 0; i < 2; i++) {
        char d[64], e[80];
        long n = 0;
        snprintf(d, sizeof d, "DCIM/100CANON/IMG_%04d.JPG", 507 + i);
        snprintf(e, sizeof e, "dark: F%02d camera JPEG IMG_%04d.JPG overwritten: the print fitted %dx%d", i + 1,
                 507 + i, FW, FH);
        char *b = get_file(d, &n);
        long pn = 0;
        char *pb = mem_print(3, i, 507 + i, &pn);
        int w = 0, h = 0, eoi = b && n > 2 && (unsigned char)b[n - 2] == 0xFF && (unsigned char)b[n - 1] == 0xD9;
        uint32_t tl = 0;
        const unsigned char *tp = b ? jp_thumb((unsigned char *)b, n, &tl) : 0;
        chk(b && eoi && mem_dims((unsigned char *)b, n, &w, &h) && w == FW && h == FH && log_has(e)
            && memmem(b, (size_t)(n < 65536 ? n : 65536), "WindyFilm Darkroom", 18) && tp && tl > 1000 && pb && pn != n,
            "frame: IMG_%04d.JPG the print fitted %dx%d (%ld B, the roll print %ld B), our comment + thumbnail (%u B) "
            "('%s')", 507 + i, w, h, n, pn, tl, log_line(e));
        char s[300];
        long cn = 0;
        snprintf(s, sizeof s, "%s/%s.JPG", dir, mem_pairs[i]);
        char *cb = get_file(s, &cn);
        const char *why = jp_pics((unsigned char *)b, n, (unsigned char *)cb, cn);
        chk(!why, "frame: IMG_%04d.JPG holds only our two pictures, nothing of Canon's (%s)", 507 + i, why ? why : "yes");
        free(cb);
        free(b);
        free(pb);
    }
    chk(log_has(jp_fitline(fl1, sizeof fl1, 0, WF_PFR_WHITE, 1)) && log_has(jp_fitline(fl2, sizeof fl2, 1, WF_PFR_WHITE, 6))
        && log_has("dark: F01 camera JPEG caption "), "frame: the print laid out again round the picture scaled down "
        "('%s', '%s')", log_line("dark: F01 camera JPEG: the print"), log_line("dark: F02 camera JPEG: the print"));
    chk(log_has("dark: F03 camera JPEG IMG_0509.JPG not there: not overwritten") && access("DCIM/100CANON/IMG_0509.JPG",
        F_OK) != 0, "frame: no JPEG for F03, none made ('%s')", log_line("dark: F03 camera JPEG"));
    chk(jp_cr2s(dir, 507) && !jp_wft(507), "frame: CR2s untouched, no .WFT");
    for (int i = 0; i < 3; i++) {                /* the prints framed; F03 without Canon's APP1 (no JPEG) */
        long n = 0;
        int w = 0, h = 0;
        char *b = mem_print(3, i, 507 + i, &n);
        int dims = b && n > 4 && (i < 2 ? mem_dims((unsigned char *)b, n, &w, &h) : 1);
        for (long o = 2; i == 2 && b && o + 9 < n && !w; o++)
            if ((unsigned char)b[o] == 0xFF && (unsigned char)b[o + 1] == 0xC0) {
                h = (unsigned char)b[o + 5] << 8 | (unsigned char)b[o + 6];
                w = (unsigned char)b[o + 7] << 8 | (unsigned char)b[o + 8];
            }
        chk(dims && w > FW && h > FH, "frame: F%02d print %dx%d (framed)", i + 1, w, h);
        free(b);
    }
    const char *ow = getenv("WF_OW_OUT");
    if (ow) {                                    /* previews for the owner: these DCIM files, then a black frame roll */
        char s[64], d[300];
        int okw = 1, okb = 1;
        for (int i = 0; i < 2; i++) {
            snprintf(s, sizeof s, "DCIM/100CANON/IMG_%04d.JPG", 507 + i);
            snprintf(d, sizeof d, "%s/ow_white_F%02d.JPG", ow, i + 1);
            okw &= mem_copy(s, d);
        }
        wf_pr_frame = WF_PFR_BLACK;
        chk(mem_roll(film, 4, dir, 510), "roll 004: 3 frames (%d)", wf_roll_now()->nshot);
        mem_run("frame black", 4);
        for (int i = 0; i < 3; i++) {
            char e[80];
            snprintf(e, sizeof e, "dark: F%02d camera JPEG IMG_%04d.JPG overwritten: the print fitted %dx%d", i + 1,
                     510 + i, FW, FH);
            snprintf(s, sizeof s, "DCIM/100CANON/IMG_%04d.JPG", 510 + i);
            snprintf(d, sizeof d, "%s/ow_black_F%02d.JPG", ow, i + 1);
            okb &= log_has(e) && mem_copy(s, d);
        }
        chk(okw && okb, "previews in %s: white %d, black %d", ow, okw, okb);
    }
    /* RAWB28 (owner 11-20): one JPEG in DCIM, the print is the roll's copy in ROLLS and is never copied again.
     * Over + print, a black frame + details + the logo: each IMG_nnnn.JPG = the picture alone at 5184 x 3456, nothing
     * else written (no WFP_ file, even with jpeg2=folder: no 100WFILM/), ROLLS holds the prints only.
     * Print + over, a white frame: each IMG_nnnn.JPG = the print fitted into 5184 x 3456, the picture alone added to the
     * run's ROLLS folder under the shot's name (ROLLS/ROLLnnn/IMG_nnnn.JPG beside Fnn_nnnn.JPG). No .WFT left, the CR2s
     * untouched */
    wf_pr_frame = WF_PFR_BLACK;
    wf_pr_jpeg = WF_PJ_BOTH;
    wf_pr_jpg2 = WF_J2_FOLDER;                   /* read, no longer used */
    for (int pass = 0, rn = ow ? 5 : 4; pass < 2; pass++, rn++) {
        int from = 513 + pass * 3;
        const char *what = pass ? "print + over" : "over + print";
        char e[96];
        if (pass) {
            wf_pr_jpeg = WF_PJ_PRINT;
            wf_pr_frame = WF_PFR_WHITE;
        }
        chk(mem_roll(film, rn, dir, from), "roll %03d: 3 frames (%d)", rn, wf_roll_now()->nshot);
        mem_run(what, rn);
        snprintf(e, sizeof e, "dark: roll %03d end", rn);
        chk(strstr(log_line(e), "3 developed, 0 failed, 0 left") != 0, "%s: 3 of 3 ('%s')", what, log_line(e));
        chk(log_has(pass ? "dark: Camera JPEG Print + over: the print over IMG_nnnn.JPG, the picture as ROLLS/ROLLnnn/"
                           "IMG_nnnn.JPG"
                         : "dark: Camera JPEG Over + print: the picture over IMG_nnnn.JPG, the print in ROLLS only"),
            "%s: the run says where ('%s')", what, log_line("dark: Camera JPEG "));
        if (pass)
            chk(log_has(jp_fitline(fl2, sizeof fl2, 1, wf_pr_frame, 6)), "%s: F02 (portrait) fitted ('%s')", what,
                log_line("dark: F02 camera JPEG: the print"));
        else
            chk(!log_has("camera JPEG: the print fitted"), "%s: no print fitted ('%s')", what,
                log_line("camera JPEG: the print fitted"));
        for (int i = 0; i < 3; i++) {
            char a[64], c[64], e1[96], e2[96], e3[96], t1[64], t2[64];
            snprintf(a, sizeof a, "DCIM/100CANON/IMG_%04d.JPG", from + i);
            snprintf(c, sizeof c, "ROLLS/ROLL%03d/IMG_%04d.JPG", rn, from + i);
            snprintf(e1, sizeof e1, "dark: F%02d camera JPEG IMG_%04d.JPG overwritten: the %s %dx%d,", i + 1, from + i,
                     pass ? "print fitted" : "picture", FW, FH);
            snprintf(e2, sizeof e2, "dark: F%02d camera JPEG ROLL%03d/IMG_%04d.JPG written: the picture %dx%d,", i + 1, rn,
                     from + i, FW, FH);
            snprintf(e3, sizeof e3, "dark: F%02d camera JPEG: the print in DCIM written, the picture in ROLLS written, ",
                     i + 1);
            long na = 0, nc = 0, np = 0;
            char *ba = get_file(a, &na), *bc = pass ? get_file(c, &nc) : 0, *bp = mem_print(rn, i, from + i, &np);
            int wa = 0, ha = 0, wc = 0, hc = 0;
            uint32_t ta = 0, tc = 0;
            const unsigned char *pa = ba ? jp_thumb((unsigned char *)ba, na, &ta) : 0;
            const unsigned char *pc = bc ? jp_thumb((unsigned char *)bc, nc, &tc) : 0;
            int da = ba && mem_dims((unsigned char *)ba, na, &wa, &ha), dc = bc && mem_dims((unsigned char *)bc, nc, &wc, &hc);
            chk(da && wa == FW && ha == FH && bp && np != na && pa && ta > 1000 && log_has(e1)
                && memmem(ba, (size_t)(na < 65536 ? na : 65536), "WindyFilm Darkroom", 18),
                "%s: F%02d IMG_%04d.JPG the %s %dx%d (%ld B), print %ld B, thumbnail %u B ('%s')", what, i + 1, from + i,
                pass ? "print fitted" : "picture", wa, ha, na, np, ta, log_line(e1));
            if (pass)
                chk(dc && wc == FW && hc == FH && na != nc && np != nc && pc && tc > 1000 && (ta != tc || memcmp(pa, pc, ta))
                    && memmem(bc, (size_t)(nc < 65536 ? nc : 65536), "WindyFilm Darkroom", 18) && log_has(e2) && log_has(e3),
                    "%s: F%02d %s the picture %dx%d (%ld B), thumbnail %u B ('%s', '%s')", what, i + 1, c, wc, hc, nc, tc,
                    log_line(e2), log_line(e3));
            else
                chk(access(c, F_OK) != 0 && !log_has("in ROLLS written") && !log_has("WFP_"),
                    "over + print: F%02d nothing but IMG_%04d.JPG written ('%s')", i + 1, from + i, log_line("WFP_"));
            snprintf(t1, sizeof t1, "DCIM/100CANON/IMG_%04d.WFT", from + i);
            snprintf(t2, sizeof t2, "ROLLS/ROLL%03d/IMG_%04d.WFT", rn, from + i);
            chk(access(t1, F_OK) != 0 && access(t2, F_OK) != 0, "%s: F%02d no .WFT left", what, i + 1);
            char s[300];
            long cn = 0;
            snprintf(s, sizeof s, "%s/%s.JPG", dir, mem_pairs[i]);
            char *cb = get_file(s, &cn);
            const char *why = jp_pics((unsigned char *)ba, na, (unsigned char *)cb, cn);
            const char *why2 = pass ? jp_pics((unsigned char *)bc, nc, (unsigned char *)cb, cn) : 0;
            chk(!why && !why2, "%s: IMG_%04d.JPG holds only our two pictures, nothing of Canon's (%s %s)", what, from + i,
                why ? why : "yes", why2 ? why2 : "yes");
            free(cb);
            free(ba);
            free(bc);
            free(bp);
        }
        chk(jp_cr2s(dir, from), "%s: CR2s untouched", what);
    }
    chk(access("DCIM/100WFILM", F_OK) != 0 && access("DCIM/100CANON/WFP_0513.JPG", F_OK) != 0,
        "no second JPEG in DCIM any more (no 100WFILM/, no WFP_)");
    wf_pr_jpg2 = WF_J2_PREFIX;
    wf_pr_frame = 0;
    wf_pr_info = 1;
    wf_pr_date = 0;
    wf_pr_jpeg = WF_PJ_KEEP;
    remove("ML/DATA/WFLOGO.DAT");
    h_srm_n = 0;
    h_shoot_max = 0;
    mod_deinit();
}

/* ROLL:cr2mv - RAW after Darkroom "Move to ROLLS" (owner 11-17, RAWB26, DESIGN 3.9) on the three real pairs, the whole
 * run in the module (memory as ROLL:jpeg):
 *   row:   Keep in DCIM by default, the menu row shown on both cameras, SET steps it;
 *   keep:  Move set with Camera JPEG Keep: said once at the start, every CR2 stays in DCIM byte for byte;
 *   move:  roll 002, Overwrite + Move: F01's CR2 moves to ROLLS/ROLL002/ byte for byte (EOS M renamed, 5D2 copied and
 *          read back) and leaves DCIM, IMG_0704.JPG = its print; F02's copy reads back wrong (a byte flipped in its
 *          write; on the EOS M its rename refused first): no copy left, its CR2 in DCIM; F03 shot RAW only (its JPEG
 *          gone): its CR2 kept;
 *   again: roll 002 developed again (ROLL002B): every frame found (F01 read from ROLLS/ROLL002/, nothing skipped), F02
 *          moves now (EOS M: the rename refused again, the copy moves it), F03 still kept, each pass-2 print made;
 *   print: roll 003, Print + over with a white frame (RAWB28): each frame's picture in ROLLS/ROLL003/IMG_nnnn.JPG and its
 *          CR2 beside it; F02's picture reads back wrong (a byte flipped in its write): its CR2 stays in DCIM. */
static void t_roll_cr2mv(void)
{
    const char *dir = dk_t_dir();
    if (!*dir) {
        chk(1, "SKIPPED: no %s RAW+L pairs (WF_DARK_CR2)", DK_T_CAM);
        return;
    }
    char s1[300], s2[300], s3[300];
    snprintf(s1, sizeof s1, "%s/%s.CR2", dir, mem_pairs[0]);
    snprintf(s2, sizeof s2, "%s/%s.CR2", dir, mem_pairs[1]);
    snprintf(s3, sizeof s3, "%s/%s.CR2", dir, mem_pairs[2]);
    std_pack();
    const int film = wf_films[fC].id;
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nbloom=off\nmist=off\n", film);
    wf_fx_retry_ms = 500;
    h_set_folder_number(100);
    h_set_file_number(700);
    mod_init(1);
    h_shoot_max = 16 << 20;
    h_srm_n = 3;
    wf_pr_frame = 0;
    wf_pr_info = 1;
    wf_pr_date = 0;
    uint8_t *row = menu_named("RAW after Darkroom");
    chk(wf_pr_cr2 == WF_PC_KEEP && row && !wf_t_me_shidden(row), "RAW after Darkroom: Keep in DCIM by default (%d), "
        "the row shown (%s)", wf_pr_cr2, DK_T_CAM);
    int m1 = menu_set("RAW after Darkroom") == 0 && wf_pr_cr2 == WF_PC_MOVE;
    int m2 = menu_set("RAW after Darkroom") == 0 && wf_pr_cr2 == WF_PC_KEEP;
    chk(m1 && m2, "SET on the row: Move to ROLLS (%d), then Keep in DCIM again (%d)", m1, m2);
    wf_pr_cr2 = WF_PC_MOVE;

    /* keep: Camera JPEG Keep, nothing moves */
    wf_pr_jpeg = WF_PJ_KEEP;
    chk(mem_roll(film, 1, dir, 701), "roll 001: 3 real RAW+L frames shot (%d)", wf_roll_now()->nshot);
    mem_run("keep", 1);
    chk(log_has("dark: RAW after Darkroom Move to ROLLS, but Camera JPEG is Keep: the CR2s stay in DCIM")
        && strstr(log_line("dark: roll 001 end"), "3 developed, 0 failed, 0 left") && !log_has(".CR2 moved to ")
        && !log_has(".CR2 kept in DCIM"), "keep: said once, 3 developed ('%s')", log_line("dark: roll 001 end"));
    chk(jp_cr2s(dir, 701) && !file_exists("ROLLS/ROLL001/IMG_0701.CR2"), "keep: the three CR2s in DCIM, byte for byte");

    /* move: Overwrite; F02's copy reads back wrong; F03 RAW only */
    wf_pr_jpeg = WF_PJ_OVER;
    chk(mem_roll(film, 2, dir, 704), "roll 002: 3 real RAW+L frames shot (%d)", wf_roll_now()->nshot);
    remove("DCIM/100CANON/IMG_0706.JPG");        /* F03 shot RAW only */
#ifndef WF_BUILD_5D2
    snprintf(h_fio_ren_fail, sizeof h_fio_ren_fail, "%s", "IMG_0705.CR2");   /* the EOS M: F02's rename refused */
#endif
    snprintf(h_fio_wpath, sizeof h_fio_wpath, "%s", "ROLLS/ROLL002/IMG_0705.CR2");
    h_fio_wflip = 1;                             /* and a byte flipped in its copy's first write */
    mem_run("move", 2);
    int flipped = !h_fio_wflip;
    h_fio_wflip = 0;
    h_fio_wpath[0] = 0;
    h_fio_ren_fail[0] = 0;
#ifdef WF_BUILD_5D2
    const char *how = "(copied and read back, ", *how2 = "(copied and read back, ", *bad = "(not the same)";
#else
    const char *how = "(renamed, ", *how2 = "(rename failed: copied and read back, ";
    const char *bad = "(not the same, after the rename failed)";
#endif
    chk(log_has("dark: RAW after Darkroom Move to ROLLS: each CR2 goes to ROLLS/ROLL002/ once its frame's JPEG is in DCIM")
        && strstr(log_line("dark: roll 002 end"), "3 developed, 0 failed, 0 left"), "move: 3 developed ('%s')",
        log_line("dark: roll 002 end"));
    chk(strstr(log_line("dark: F01 IMG_0704.CR2 moved to ROLLS/ROLL002/ "), how)
        && jp_same(s1, "ROLLS/ROLL002/IMG_0704.CR2") && !file_exists("DCIM/100CANON/IMG_0704.CR2")
        && jp_same("DCIM/100CANON/IMG_0704.JPG", "ROLLS/ROLL002/F01_0704.JPG"),
        "move: F01's CR2 in ROLLS/ROLL002 byte for byte, gone from DCIM, IMG_0704.JPG = the print ('%s')",
        log_line("dark: F01 IMG_0704.CR2"));
    chk(flipped && strstr(log_line("dark: F02 IMG_0705.CR2 kept in DCIM: the copy to ROLLS/ROLL002/ failed ("), bad)
        && jp_same(s2, "DCIM/100CANON/IMG_0705.CR2") && !file_exists("ROLLS/ROLL002/IMG_0705.CR2"),
        "move: F02's copy read back wrong: no copy left, its CR2 in DCIM byte for byte ('%s')",
        log_line("dark: F02 IMG_0705.CR2"));
    chk(log_has("dark: F03 IMG_0706.CR2 kept in DCIM: its JPEG was not written") && jp_same(s3, "DCIM/100CANON/IMG_0706.CR2")
        && !file_exists("ROLLS/ROLL002/IMG_0706.CR2"), "move: F03 RAW only: its CR2 kept ('%s')",
        log_line("dark: F03 IMG_0706.CR2"));

    /* again: roll 002 developed again, F01's CR2 read from ROLLS/ROLL002, F02 moves now */
#ifndef WF_BUILD_5D2
    snprintf(h_fio_ren_fail, sizeof h_fio_ren_fail, "%s", "IMG_0705.CR2");   /* refused again: the copy moves it */
#endif
    log_mark();
    int ds = wf_dark_start_roll(2, 1, film);
    int ok = ds == 0 && WAIT(log_has("dark: roll 002 end") && !wf_dark_busy(), 1500000);
    h_fio_ren_fail[0] = 0;
    chk(ok && strstr(log_line("dark: roll 002 develop 2: film "), "3 frames, prints in ROLLS/ROLL002B")
        && !log_has("is gone: skipped") && strstr(log_line("dark: roll 002 end"), "3 developed, 0 failed, 0 left"),
        "again (%d): 3 frames, none skipped ('%s', '%s')", ds, log_line("dark: roll 002 develop"),
        log_line("dark: roll 002 end"));
    chk(log_has("dark: F01 IMG_0704.CR2 read from ROLLS/ROLL002/") && !log_has("dark: F01 IMG_0704.CR2 moved")
        && !log_has("dark: F01 IMG_0704.CR2 kept") && jp_same(s1, "ROLLS/ROLL002/IMG_0704.CR2"),
        "again: F01's CR2 read from ROLLS/ROLL002/ and left there byte for byte");
    chk(strstr(log_line("dark: F02 IMG_0705.CR2 moved to ROLLS/ROLL002/ "), how2)
        && jp_same(s2, "ROLLS/ROLL002/IMG_0705.CR2") && !file_exists("DCIM/100CANON/IMG_0705.CR2"),
        "again: F02's CR2 moved now ('%s')", log_line("dark: F02 IMG_0705.CR2"));
    chk(log_has("dark: F03 IMG_0706.CR2 kept in DCIM: its JPEG was not written") && jp_same(s3, "DCIM/100CANON/IMG_0706.CR2"),
        "again: F03 still RAW only, its CR2 kept");
    for (int i = 0; i < 3; i++) {
        char p[64];
        long n = 0;
        snprintf(p, sizeof p, "ROLLS/ROLL002B/F%02d_%04d.JPG", i + 1, 704 + i);
        char *b = get_file(p, &n);
        chk(b && n > (1 << 20), "again: %s %ld KB", p, n >> 10);
        free(b);
    }
    chk(jp_same("DCIM/100CANON/IMG_0704.JPG", "ROLLS/ROLL002B/F01_0704.JPG"), "again: IMG_0704.JPG = the pass-2 print");

    /* print: Print + over with a frame: the picture beside the print and the CR2; F02's picture write goes wrong */
    wf_pr_jpeg = WF_PJ_PRINT;
    wf_pr_frame = WF_PFR_WHITE;
    chk(mem_roll(film, 3, dir, 707), "roll 003: 3 real RAW+L frames shot (%d)", wf_roll_now()->nshot);
    snprintf(h_fio_wpath, sizeof h_fio_wpath, "%s", "ROLLS/ROLL003/IMG_0708.WFT");
    h_fio_wflip = 1;
    mem_run("print", 3);
    int flip3 = !h_fio_wflip;
    h_fio_wflip = 0;
    h_fio_wpath[0] = 0;
    chk(strstr(log_line("dark: roll 003 end"), "3 developed, 0 failed, 0 left") != 0, "print: 3 developed ('%s')",
        log_line("dark: roll 003 end"));
    for (int i = 0; i < 3; i += 2) {
        char p[64], c[64], j[64], m[64];
        snprintf(p, sizeof p, "ROLLS/ROLL003/IMG_%04d.JPG", 707 + i);
        snprintf(c, sizeof c, "ROLLS/ROLL003/IMG_%04d.CR2", 707 + i);
        snprintf(j, sizeof j, "DCIM/100CANON/IMG_%04d.JPG", 707 + i);
        snprintf(m, sizeof m, "dark: F%02d IMG_%04d.CR2 moved to ROLLS/ROLL003/ ", i + 1, 707 + i);
        char sp[300];
        snprintf(sp, sizeof sp, "%s/%s.CR2", dir, mem_pairs[i]);
        long np = 0, nj = 0, nr = 0;
        char *bp = get_file(p, &np), *bj = get_file(j, &nj), *br = mem_print(3, i, 707 + i, &nr);
        int w = 0, h = 0, d = bp && mem_dims((unsigned char *)bp, np, &w, &h);
        chk(d && w == FW && h == FH && bj && br && np != nj && np != nr && nj != nr && log_has(m) && jp_same(sp, c),
            "print: F%02d the picture %s %dx%d, the print in DCIM, the CR2 moved beside "
            "them ('%s')", i + 1, p, w, h, log_line(m));
        char dc[64];
        snprintf(dc, sizeof dc, "DCIM/100CANON/IMG_%04d.CR2", 707 + i);
        chk(!file_exists(dc), "print: F%02d CR2 gone from DCIM", i + 1);
        free(bp);
        free(bj);
        free(br);
    }
    char sp2[300];
    snprintf(sp2, sizeof sp2, "%s/%s.CR2", dir, mem_pairs[1]);
    chk(flip3 && log_has("dark: F02 camera JPEG: the print in DCIM written, the picture in ROLLS not written")
        && log_has("dark: F02 IMG_0708.CR2 kept in DCIM: its JPEG was not written") && jp_same(sp2, "DCIM/100CANON/IMG_0708.CR2")
        && !file_exists("ROLLS/ROLL003/IMG_0708.CR2") && !file_exists("ROLLS/ROLL003/IMG_0708.JPG")
        && !file_exists("ROLLS/ROLL003/IMG_0708.WFT"), "print: F02's picture read back wrong: nothing left of it in ROLLS, "
        "its CR2 kept in DCIM ('%s', '%s')", log_line("dark: F02 camera JPEG: the print in DCIM"),
        log_line("dark: F02 IMG_0708.CR2"));
    wf_pr_frame = 0;
    wf_pr_cr2 = WF_PC_KEEP;
    wf_pr_jpeg = WF_PJ_KEEP;
    h_srm_n = 0;
    h_shoot_max = 0;
    mod_deinit();
}

/* ROLL:shelf - many rolls waiting, develop again (owner 11-17, DESIGN 3.15), on real RAW+L pairs: the browser's
 * close-and-load puts the full roll 001 on the shelf (ROLLS/ROLL001/WFROLL.TXT, rewound, 3 to develop) and loads roll
 * 002; the Darkroom develops roll 001 from the shelf while the camera's roll 002 waits full (F02's CR2 gone: skipped);
 * then develops it again with another film into ROLLS/ROLL001B ("develop 2" in each print's UserComment, the first
 * prints untouched, the film before back); the camera's roll developed goes on the shelf too. The shelf after a reboot
 * is ROLL:shelfboot (the module loads once per process: a second init here would find it unloading) */
static void t_roll_shelf(void)
{
    const char *dir = dk_t_dir();
    char t2[300];
    if (!*dir) {
        chk(1, "SKIPPED: no %s RAW+L pairs (WF_DARK_CR2)", DK_T_CAM);
        return;
    }
    snprintf(t2, sizeof t2, "%s/%s.CR2", dir, mem_pairs[1]);
    std_pack();
    const int film = wf_films[fC].id, film2 = wf_films[fB].id;
    const struct wf_film *f2 = wf_film_by_id(film2);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nbloom=off\nmist=off\n", film);
    wf_fx_retry_ms = 500;
    h_set_folder_number(100);
    h_set_file_number(600);
    mod_init(1);
    h_shoot_max = 16 << 20;
    h_srm_n = 3;
    wf_pr_frame = 0;
    wf_pr_info = 1;
    wf_pr_date = 0;
    wf_pr_jpeg = WF_PJ_KEEP;
    chk(LOGW("roll: shelf 0 rolls (empty)", 10000), "an empty card: the shelf empty ('%s')", log_line("roll: shelf"));
    chk(mem_roll(film, 1, dir, 601), "roll 001: 3 real RAW+L frames, full (%d)", wf_roll_now()->nshot);
    log_mark();
    int rc = wf_roll_req_swap(film, 3, 0);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->num == 2, 5000)
        && LOGW("roll: 001 on the shelf (a new roll): 3/3, 3 to develop, rewound", 3000)
        && LOGW("roll: shelf 1 roll (001 3 to develop)", 5000) && file_exists("ROLLS/ROLL001/WFROLL.TXT"),
        "close-and-load: roll 001 on the shelf, roll 002 loaded (%d, '%s')", rc, log_line("roll: shelf"));
    const struct wf_shelf *s = wf_shelf_now();
    chk(s->n == 1 && s->e[0].num == 1 && s->e[0].dev == 1 && s->e[0].left == 3 && s->e[0].state == WF_RS_REWOUND
        && s->e[0].film == film, "the shelf: roll 001, 3 to develop (%d rolls, left %d)", s->n, s->e[0].left);
    {                                            /* the shelf copy: roll 001 as it left the camera */
        long n = 0;
        char *b = get_file("ROLLS/ROLL001/WFROLL.TXT", &n);
        struct wf_roll q;
        chk(b && n == WF_ROLL_FILE && wf_roll_parse(&q, b, (int)n) == 0 && q.num == 1 && q.film == film && q.nshot == 3
            && q.state == WF_RS_REWOUND && wf_roll_left(&q) == 3 && q.dev == 0 && !strstr(b, "dev="),
            "ROLLS/ROLL001/WFROLL.TXT: roll 001 rewound, 3 frames to develop, no dev= (%ld B)", n);
        free(b);
    }
    chk(mem_shoot(dir, 604) && wf_roll_now()->state == WF_RS_FULL, "roll 002: 3 frames, full (%d)", wf_roll_now()->nshot);

    remove("DCIM/100CANON/IMG_0602.CR2");        /* F02's CR2 gone from the card */
    log_mark();
    int iso_n0 = h_iso_n;                        /* the Darkroom's own film changes set no ISO (owner 11-17) */
    int ds = wf_dark_start_roll(1, 0, 0);
    chk(ds == 0 && LOGW("dark: roll 001 F02 100/IMG_0602.CR2 is gone: skipped", 10000)
        && LOGW("dark: roll 001 from the shelf: film ", 5000)
        && strstr(log_line("dark: roll 001 from the shelf"), "2 to develop, prints in ROLLS/ROLL001"),
        "the shelf roll 001 starts (%d): F02 skipped ('%s')", ds, log_line("dark: roll 001 from the shelf"));
    int ok = ds == 0 && WAIT(log_has("dark: roll 001 end") && !wf_dark_busy(), 1500000);
    chk(ok && strstr(log_line("dark: roll 001 end"), "2 developed, 0 failed, 0 left"), "roll 001: 2 developed ('%s')",
        log_line("dark: roll 001 end"));
    long n1 = 0, n3 = 0;
    char *p1 = mem_print(1, 0, 601, &n1), *p3 = mem_print(1, 2, 603, &n3);
    chk(p1 && p3 && n1 > (1 << 20) && n3 > (1 << 20) && !file_exists("ROLLS/ROLL001/F02_0602.JPG"),
        "roll 001's prints: F01 %ld KB, F03 %ld KB, no F02", n1 >> 10, n3 >> 10);
    chk(wf_roll_now()->num == 2 && wf_roll_now()->state == WF_RS_FULL && wf_roll_left(wf_roll_now()) == 3
        && wf_loaded() == film, "the camera's roll 002 untouched (state %d, %d left), film %d loaded",
        wf_roll_now()->state, wf_roll_left(wf_roll_now()), wf_loaded());
    chk(LOGW("roll: shelf 1 roll (001 done 2/3)", 5000), "the shelf: roll 001 developed ('%s')", log_line("roll: shelf"));

    chk(mem_copy(t2, "DCIM/100CANON/IMG_0602.CR2"), "F02's CR2 back on the card");
    log_mark();
    ds = wf_dark_start_roll(1, 1, film2);
    char e[128];
    snprintf(e, sizeof e, "dark: roll 001 develop 2: film %s, 3 frames, prints in ROLLS/ROLL001B", f2 ? f2->name : "?");
    chk(ds == 0 && LOGW(e, 10000), "develop again (%d): '%s'", ds, log_line("dark: roll 001 develop"));
    ok = ds == 0 && WAIT(log_has("dark: roll 001 end") && !wf_dark_busy(), 1500000);
    snprintf(e, sizeof e, "dark: film %d back", film);
    chk(ok && strstr(log_line("dark: roll 001 end"), "3 developed, 0 failed, 0 left") && log_has(e) && wf_loaded() == film,
        "develop 2: 3 developed, film %d back (%d) ('%s')", film, wf_loaded(), log_line("dark: roll 001 end"));
    chk(h_iso_n == iso_n0, "the shelf roll and the develop again with film %d: the ISO untouched (%d calls)", film2,
        h_iso_n - iso_n0);
    for (int i = 0; i < 3; i++) {
        char pth[64];
        long n = 0;
        snprintf(pth, sizeof pth, "ROLLS/ROLL001B/F%02d_%04d.JPG", i + 1, 601 + i);
        char *b = get_file(pth, &n);
        chk(b && n > (1 << 20) && memmem(b, (size_t)(n < 65536 ? n : 65536), "| develop 2", 11), "develop 2: %s %ld KB, "
            "'develop 2' in its UserComment", pth, n >> 10);
        free(b);
    }
    long m1 = 0, m3 = 0, rn = 0;
    char *q1 = mem_print(1, 0, 601, &m1), *q3 = mem_print(1, 2, 603, &m3), *rt = get_file("ROLLS/ROLL001B/WFROLL.TXT", &rn);
    chk(q1 && q3 && m1 == n1 && m3 == n3 && !memcmp(q1, p1, (size_t)n1) && !memcmp(q3, p3, (size_t)n3)
        && !file_exists("ROLLS/ROLL001/F02_0602.JPG"), "the first prints untouched (ROLLS/ROLL001)");
    snprintf(e, sizeof e, "film=%d\nexp=3\nstate=done\ndev=2\nown=%d\n", film2, film);
    chk(rt && rn == 1536 && strstr(rt, e), "ROLLS/ROLL001B/WFROLL.TXT: develop 2, film %d, own %d", film2, film);
    free(p1); free(p3); free(q1); free(q3); free(rt);
    chk(LOGW("roll: shelf 1 roll (001B done 3/3)", 5000), "the shelf: roll 001's pass 2 ('%s')", log_line("roll: shelf"));

    mem_run("roll 002", 2);                      /* the camera's roll developed: on the shelf as well */
    chk(LOGW("roll: 002 on the shelf (developed): 3/3, 0 to develop, done", 5000)
        && LOGW("roll: shelf 2 rolls (002 done 3/3, 001B done 3/3)", 5000), "roll 002 developed: on the shelf ('%s')",
        log_line("roll: shelf"));
    h_srm_n = 0;
    h_shoot_max = 0;
    mod_deinit();
}

/* ROLL:shelfboot - the shelf at boot is the card's (owner 11-17, DESIGN 3.15): a card with ROLLS/ROLL001 (developed)
 * and its pass ROLL001B (2 left), ROLL003 (rewound, 3 left), ROLL004 (a folder, no roll file: developed before the
 * shelf) and the camera's roll 002 developed, no copy yet. Boot: roll 002's copy made ("developed before the shelf"),
 * the list newest first down from 002 (wrapping: 003 after 001), ROLL004 counted; a new roll skips 003 (on the shelf)
 * and is 004 (no roll file there); then the list goes down from 004 */
static void sb_roll(const char *dir, int num, int dev, int film, int film2, int done, int devd)
{
    struct wf_roll r;
    char p[48], t[WF_ROLL_FILE];
    wf_roll_reset(&r);
    wf_roll_load(&r, film, 3, 100, 600 + num * 10);
    r.num = num;
    wf_roll_seen(&r, 100, 603 + num * 10, roll_cr2_yes, 0);
    wf_roll_rewind(&r);
    for (int i = 0; i < done; i++)
        wf_roll_mark(&r, i, WF_RF_DEV);
    if (dev >= 2) {
        wf_roll_again(&r, film2, 0, 0);
        for (int i = 0; i < devd; i++)
            wf_roll_mark(&r, i, WF_RF_DEV);
    }
    mkdir("ROLLS", 0777);
    if (!dir) {                                  /* the camera's roll file */
        wf_roll_text(&r, t, sizeof t);
        put_file(H_ROLLF, t, sizeof t);
        return;
    }
    snprintf(p, sizeof p, "ROLLS/%s", dir);
    mkdir(p, 0777);
    if (num <= 0)
        return;
    snprintf(p, sizeof p, "ROLLS/%s/WFROLL.TXT", dir);
    wf_roll_text(&r, t, sizeof t);
    put_file(p, t, sizeof t);
}
static void t_roll_shelfboot(void)
{
    std_pack();
    const int film = wf_films[fC].id;
    const int film2 = wf_films[fB].id;
    sb_roll("ROLL001", 1, 1, film, 0, 3, 0);     /* developed */
    sb_roll("ROLL001B", 1, 2, film, film2, 3, 1);   /* its develop 2, stopped after F01 */
    sb_roll("ROLL003", 3, 1, film, 0, 0, 0);     /* rewound, 3 to develop */
    sb_roll("ROLL004", 0, 1, film, 0, 0, 0);     /* a folder alone */
    sb_roll(0, 2, 1, film, 0, 3, 0);             /* the camera's roll 002: developed */
    h_set_folder_number(100);
    h_set_file_number(700);
    mod_init(1);
    chk(LOGW("roll: 002 on the shelf (developed before the shelf): 3/3, 0 to develop, done", 10000)
        && file_exists("ROLLS/ROLL002/WFROLL.TXT"), "boot: the camera's developed roll 002 copied to the shelf ('%s')",
        log_line("roll: 002 on the shelf"));
    chk(LOGW("roll: shelf 3 rolls (002 done 3/3, 001B 2 to develop, 003 3 to develop)", 5000),
        "boot: the shelf from the card, newest first down from 002 ('%s')", log_line("roll: shelf "));
    chk(LOGW("roll: shelf: 1 roll folder without a roll file (developed before the shelf): ROLL004", 3000),
        "boot: ROLL004 counted, not listed ('%s')", log_line("without a roll file"));
    const struct wf_shelf *s = wf_shelf_now();
    chk(s->n == 3 && s->e[1].num == 1 && s->e[1].dev == 2 && s->e[1].film == film2 && s->e[1].own == film
        && s->e[1].left == 2 && s->e[1].ndev == 1 && s->e[2].num == 3 && s->e[2].state == WF_RS_REWOUND,
        "the shelf entries: 001B film %d own %d, 2 left; 003 rewound (%d rolls)", s->e[1].film, s->e[1].own, s->n);
    chk(h_iso_n == 0 && !log_has(": ISO "), "boot with a roll in the camera: the ISO untouched (%d calls)", h_iso_n);
    log_mark();
    int rc = wf_roll_req_load(film, 3);
    int seen3 = 0;                               /* never shown loaded under the shelf's number */
    for (int t = 0; t < 500 && !(wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->num == 4); t++) {
        seen3 |= wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->num == 3;
        usleep(10000);
    }
    chk(rc == 0 && wf_roll_now()->state == WF_RS_LOADED && wf_roll_now()->num == 4 && !seen3
        && LOGW("roll: ROLL003 is on the card's shelf already: this roll is 004", 3000) && LOGW("roll: 004 loaded", 3000),
        "a new roll: not over the shelf's 003, roll %03d ('%s')", wf_roll_now()->num, log_line("on the card's shelf"));
    chk(LOGW("roll: shelf 3 rolls (003 3 to develop, 002 done 3/3, 001B 2 to develop)", 5000),
        "the list down from the camera's 004 ('%s')", log_line("roll: shelf "));
    mod_deinit();
}

/* ISO - owner 11-17 ("lúc set film sao vụ iso ko nhận đúng theo film roll nữa"): the browser's SET that loads a film
 * (wf_load + wf_iso_film) and a roll load set the camera's ISO to the film's speed at the nearest step the body takes.
 * Nothing at boot, nothing for a bare wf_load (the Darkroom's own film changes); Auto ISO gives way; HTP's floor; a body
 * on whole stops; a body that takes none (a scene mode) */
static int k_speed(int iso)                      /* a ready colour film of that box speed with a Scan table, -1 */
{
    for (int k = 0; k < wf_nfilms; k++)
        if (wf_films[k].iso == iso && colour_film(k) && (wf_films[k].prints & 1))
            return k;
    return -1;
}
/* the quick keys (owner 11-17, DESIGN 3.17): EOS M stills LiveView. DOWN ring: DOWN = the ring sets ISO, again aperture
 * (M, a lens with contacts), again the camera's; L/R ISO: LEFT / RIGHT = ISO, DOWN = aperture; SET in MF = Magic Zoom.
 * DOWN and SET always go on (the core's long presses); a long DOWN's TRASH, 6 s, the shutter or another key give the
 * ring back; nothing in the ML menu, a Canon dialog, movie mode or Canon's zoom. The 5D2: all keys go on. */
#define QK_HALF 1                                 /* ML module.h MODULE_KEY_* */
#define QK_WL 7
#define QK_WR 8
#define QK_SET 9
#define QK_UP 12
#define QK_RIGHT 15
#define QK_LEFT 16
#define QK_DOWN 19
#define QK_TRASH 25
/* the EOS M's real ring in plain LiveView (owner RAWB28 KEYTRACE.LOG 11-20): Canon's key event 0x54 reaches ML with no
 * code (ML's module_translate_key gives the module key 0), then Canon moves its own code (Tv in M / Tv, the aperture
 * in Av) and ML's property handler puts it into lens_info */
static void qk_canon_turn(const char *field, int code)
{
    chk(cbr_key(0) == 1, "Canon's 0x54 (key 0) goes on");
    if (field)
        h_li_set(field, code);
}
static int qk_quiet(const char *what)             /* every key goes on and no ring / zoom log */
{
    log_mark();
    int n = h_iso_n, a = h_av_n, r = cbr_key(QK_DOWN) == 1 && cbr_key(QK_WR) == 1 && cbr_key(QK_SET) == 1;
    usleep(300000);
    r = r && h_iso_n == n && h_av_n == a && !log_has("keys: ring") && !log_has("Magic Zoom");
    chk(r, "%s: the keys go on, nothing set ('%s')", what, log_line("keys:"));
    cbr_key(QK_HALF);
    return r;
}
static void t_qkeys(void)
{
    struct pk e[] = { { fC, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 1, 0);
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\n", wf_films[fC].id);
    h_li_set("iso", 88);
    h_li_set("aperture", 32);
    h_li_set("amin", 24);
    h_li_set("amax", 80);
    h_li_set("name0", 'E');
    shooting_mode = 3;
    af_mode = 0;
    mod_init(1);
    usleep(300000);
    uint8_t *qrow = menu_named("Quick keys");
    if (wf_cam() != WF_CAM_EOSM) {
        chk(qrow && wf_t_me_shidden(qrow), "5D2: the Quick keys row hidden (the 5D2 has its own ISO button and dials)");
        chk(wf_qk == WF_QK_DOWN, "5D2: the row reads DOWN ring (one config for both)");
        log_mark();
        int n = h_iso_n;
        chk(cbr_key(QK_DOWN) == 1 && cbr_key(QK_WR) == 1 && cbr_key(QK_RIGHT) == 1 && cbr_key(QK_SET) == 1,
            "5D2: the quick keys stay off, every key goes on");
        usleep(300000);
        chk(h_iso_n == n && h_mz_n == 0 && !log_has("keys:"), "5D2: no ISO, no Magic Zoom, no keys log");
        mod_deinit();
        return;
    }
    chk(wf_qk == WF_QK_DOWN, "Quick keys: DOWN ring by default");
    chk(qrow && !wf_t_me_shidden(qrow), "EOS M: the Quick keys row shown");
    /* DOWN ring: ISO */
    log_mark();
    chk(cbr_key(QK_DOWN) == 1, "DOWN goes on (the core's long press: a long DOWN still opens the browser)");
    chk(LOGW("keys: ring ISO (ISO 400)", 3000), "DOWN: the ring sets ISO ('%s')", log_line("keys:"));
    chk(cbr_key(QK_WR) == 0, "a ring turn is used while the ring sets ISO");
    chk(WAIT(h_li_get("iso") == 91, 3000) && LOGW("keys: ISO 500", 3000), "ring right: ISO 500 (code %d)", h_li_get("iso"));
    cbr_key(QK_WL);
    cbr_key(QK_WL);
    chk(WAIT(h_li_get("iso") == 85, 3000) && LOGW("keys: ISO 320", 3000), "two turns left: ISO 320 (code %d)",
        h_li_get("iso"));
    /* DOWN again (M, a lens with contacts): aperture */
    log_mark();
    chk(cbr_key(QK_DOWN) == 1 && LOGW("keys: ring aperture (f/2.8)", 3000), "DOWN again in M: the ring sets aperture ('%s')",
        log_line("keys:"));
    chk(cbr_key(QK_WR) == 0 && WAIT(h_li_get("aperture") == 35, 3000) && LOGW("keys: f/3.2", 3000),
        "ring right: f/3.2 (code %d)", h_li_get("aperture"));
    for (int i = 0; i < 4; i++)
        cbr_key(QK_WL);
    chk(WAIT(h_li_get("aperture") == 24, 3000) && LOGW("keys: f/2.0", 3000), "four left: f/2.0, the lens' widest (code %d)",
        h_li_get("aperture"));
    int an = h_av_n;
    cbr_key(QK_WL);
    usleep(300000);
    chk(h_av_n == an && h_li_get("aperture") == 24, "past the widest: nothing asked (%d calls)", h_av_n - an);
    log_mark();
    chk(cbr_key(QK_DOWN) == 1 && LOGW("keys: ring back to the camera", 3000), "DOWN a third time: the camera's ring");
    chk(cbr_key(QK_WR) == 1, "a ring turn goes on to Canon again");
    /* a long DOWN (the core's TRASH, the browser), 6 s, the shutter, UP: the ring goes back */
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "DOWN: ISO ring again");
    log_mark();
    cbr_key(QK_TRASH);
    chk(cbr_key(QK_WR) == 1 && LOGW("keys: ring back to the camera", 3000), "a long DOWN (TRASH): the ring back at once");
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "DOWN: ISO ring");
    log_mark();
    h_clock_add_ms += 7000;
    chk(LOGW("keys: ring back to the camera", 3000) && cbr_key(QK_WR) == 1, "6 s without a turn: the ring back");
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "DOWN: ISO ring");
    log_mark();
    cbr_key(QK_HALF);
    chk(cbr_key(QK_WR) == 1 && LOGW("keys: ring back to the camera", 3000), "half-shutter: the ring back");
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "DOWN: ISO ring");
    log_mark();
    cbr_key(QK_UP);
    chk(cbr_key(QK_WR) == 1 && LOGW("keys: ring back to the camera", 3000), "UP (Canon's Av +-): the ring back");
    /* the camera's own sequence (owner RAWB28 KEYTRACE.LOG, 14.1 s: E 54 + E 2c = DOWN, then E 54 alone four times while
     * the ring was ISO, and WINDYFLM.LOG shows no ISO step): no WHEEL code ever comes, Canon moves Tv a third a turn */
    h_li_set("shutter", 104);                     /* 1/125 */
    log_mark();
    qk_canon_turn(0, 0);                          /* E 54 right before the DOWN */
    chk(cbr_key(QK_DOWN) == 1 && LOGW("keys: ring ISO (ISO 320)", 3000)
        && LOGW("keys: ring read from Canon's Tv (code 104, mode 3)", 3000), "replay: DOWN = the ISO ring, read from Tv ('%s')",
        log_line("keys: ring read"));
    usleep(100000);
    int tn = h_tv_n;
    qk_canon_turn("shutter", 107);                /* E 54, Canon: 1/125 -> 1/160 */
    chk(WAIT(h_li_get("iso") == 88, 3000) && LOGW("keys: ISO 400", 3000), "replay: Canon's Tv a third up -> ISO 400 (code %d)",
        h_li_get("iso"));
    chk(LOGW("keys: ring turn 1 (Tv code 104 -> 107, put back: 104)", 3000) && h_li_get("shutter") == 104 && h_tv_n == tn + 1,
        "replay: Tv put back to 1/125 (code %d, %d asks, '%s')", h_li_get("shutter"), h_tv_n - tn, log_line("ring turn"));
    qk_canon_turn("shutter", 101);                /* a third down (1/100) */
    chk(WAIT(h_li_get("iso") == 85, 3000) && WAIT(h_li_get("shutter") == 104, 2000), "replay: a turn down -> ISO 320, Tv back");
    qk_canon_turn("shutter", 109);                /* two clicks before one look (1/200) */
    chk(WAIT(h_li_get("iso") == 91, 3000) && LOGW("keys: ring turn 2 (Tv code 104 -> 109", 3000), "replay: two clicks at once "
        "-> two thirds up, ISO 500 (code %d)", h_li_get("iso"));
    h_clock_add_ms += 5000;
    qk_canon_turn("shutter", 107);
    chk(WAIT(h_li_get("iso") == 93, 3000), "replay: a turn 5 s later -> ISO 640 (code %d)", h_li_get("iso"));
    log_mark();
    h_clock_add_ms += 4000;
    usleep(300000);
    chk(!log_has("ring back to the camera"), "replay: the turns keep the ring (4 s after the last one)");
    /* the aperture ring in M: Canon's Tv turn steps the aperture */
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring aperture (f/2.0)", 3000), "replay: DOWN again = the aperture ring ('%s')", log_line("keys:"));
    usleep(100000);
    qk_canon_turn("shutter", 107);
    chk(WAIT(h_li_get("aperture") == 27, 3000) && WAIT(h_li_get("shutter") == 104, 2000) && LOGW("keys: f/2.2", 3000),
        "replay: aperture ring, Canon's Tv up -> f/2.2, Tv back (code %d)", h_li_get("aperture"));
    /* a body that keeps its Tv: no ISO on top, the ring goes back */
    cbr_key(QK_HALF);
    usleep(200000);
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "replay: ISO ring");
    usleep(100000);
    h_tv_refuse = 1;
    int rin = h_iso_n;
    qk_canon_turn("shutter", 107);
    chk(LOGW("keys: the camera kept Tv code 107 (asked 104): the ring goes back to the camera", 3000)
        && LOGW("keys: ring back to the camera", 3000), "replay: Tv not put back: told, the ring back ('%s')",
        log_line("kept"));
    usleep(200000);
    chk(h_iso_n == rin, "replay: no ISO step on top of Canon's Tv (%d asks)", h_iso_n - rin);
    h_tv_refuse = 0;
    h_li_set("shutter", 104);
    /* Av: Canon's ring moves the aperture: put back, ISO a third */
    shooting_mode = 2;
    h_li_set("aperture", 32);
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "replay Av: ISO ring");
    usleep(100000);
    int iso0 = h_li_get("iso");
    qk_canon_turn("aperture", 35);
    chk(WAIT(h_li_get("aperture") == 32, 3000) && WAIT(h_li_get("iso") == wf_roll_iso_step(iso0, 1, 72, 128, 1), 3000)
        && LOGW("keys: ring turn 1 (aperture code 32 -> 35, put back: 32)", 3000), "replay Av: Canon's aperture up -> put "
        "back, ISO a third up (code %d -> %d)", iso0, h_li_get("iso"));
    cbr_key(QK_HALF);
    usleep(200000);                               /* (wf_task sees the ring go back before the next DOWN) */
    /* P: the ring shifts the program, no ISO ring */
    shooting_mode = 0;
    log_mark();
    tn = h_tv_n;
    rin = h_iso_n;
    cbr_key(QK_DOWN);
    chk(LOGW("keys: in P the ring shifts the program: the ISO ring needs Tv, Av or M", 3000), "replay P: told ('%s')",
        log_line("keys:"));
    qk_canon_turn("shutter", 107);
    usleep(300000);
    chk(h_tv_n == tn && h_iso_n == rin && h_li_get("shutter") == 107, "replay P: Canon's shift left alone");
    h_li_set("shutter", 104);
    shooting_mode = 3;
    h_li_set("aperture", 24);
    /* Av: no aperture step (Canon's ring sets it); a scene mode: no ISO ring */
    shooting_mode = 2;
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "Av: DOWN = ISO ring");
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring back to the camera", 3000) && !log_has("ring aperture"), "Av: DOWN again = the camera's ring");
    shooting_mode = 9;
    log_mark();
    int in = h_iso_n;
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ISO is set by the camera in this mode", 3000) && cbr_key(QK_WR) == 1 && h_iso_n == in,
        "a scene mode: no ISO ring ('%s')", log_line("keys:"));
    shooting_mode = 3;
    /* not stills LiveView with nothing on top */
    h_menu_shown = 1;
    qk_quiet("ML menu");
    h_menu_shown = 0;
    gui_state = 1;
    qk_quiet("a Canon dialog (gui_state)");
    gui_state = 0;
    h_disp_idle = 0;
    qk_quiet("not display_idle");
    h_disp_idle = 1;
    h_movie = 1;
    qk_quiet("movie mode");
    h_movie = 0;
    lv_dispsize = 5;
    qk_quiet("Canon's 5x zoom");
    lv_dispsize = 1;
    lv = 0;
    qk_quiet("no LiveView");
    lv = 1;
    /* Auto ISO; a body that takes whole stops */
    h_li_set("iso", 0);
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO (ISO AUTO)", 3000), "Auto: the ring shows ISO AUTO");
    in = h_iso_n;
    cbr_key(QK_WL);
    usleep(300000);
    chk(h_li_get("iso") == 0 && h_iso_n == in, "Auto, turned down: Auto stays, nothing asked");
    cbr_key(QK_WR);
    chk(WAIT(h_li_get("iso") == 72, 3000) && LOGW("keys: ISO 100", 3000), "Auto, turned up: ISO 100");
    h_iso_whole = 1;
    log_mark();
    cbr_key(QK_WR);
    chk(WAIT(h_li_get("iso") == 80, 3000) && LOGW("keys: ISO 200 (whole stops)", 3000), "whole stops only: 100 -> 200 ('%s')",
        log_line("keys:"));
    h_iso_whole = 0;
    h_iso_refuse = 1;
    log_mark();
    cbr_key(QK_WR);
    chk(LOGW("keys: ISO 250 not taken by the camera (ISO 200)", 3000) && h_li_get("iso") == 80, "refused: '%s'",
        log_line("keys:"));
    h_iso_refuse = 0;
    cbr_key(QK_HALF);
    /* L/R ISO */
    log_mark();
    chk(menu_set("Quick keys") == 0 && wf_qk == WF_QK_LR, "the row: L/R ISO (%d)", wf_qk);
    chk(cbr_key(QK_RIGHT) == 0 && WAIT(h_li_get("iso") == 83, 3000) && LOGW("keys: ISO 250", 3000),
        "L/R ISO: RIGHT is used, a third up (code %d)", h_li_get("iso"));
    chk(cbr_key(QK_LEFT) == 0 && WAIT(h_li_get("iso") == 80, 3000), "LEFT a third down (code %d)", h_li_get("iso"));
    log_mark();
    chk(cbr_key(QK_DOWN) == 1 && LOGW("keys: ring aperture (f/2.0)", 3000), "L/R ISO: DOWN = the aperture ring ('%s')",
        log_line("keys:"));
    chk(cbr_key(QK_WR) == 0 && WAIT(h_li_get("aperture") == 27, 3000) && LOGW("keys: f/2.2", 3000), "ring right: f/2.2");
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring back to the camera", 3000) && cbr_key(QK_WR) == 1, "DOWN again: the camera's ring");
    h_clock_add_ms += 2000;
    usleep(300000);
    long n;
    char *b = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(b && strstr(b, "qkeys=lr\n"), "config: qkeys=lr");
    free(b);
    /* Off */
    chk(menu_set("Quick keys") == 0 && wf_qk == WF_QK_OFF, "the row: Off (%d)", wf_qk);
    af_mode = 3;
    log_mark();
    in = h_iso_n;
    chk(cbr_key(QK_RIGHT) == 1 && cbr_key(QK_LEFT) == 1 && cbr_key(QK_DOWN) == 1 && cbr_key(QK_WR) == 1
        && cbr_key(QK_SET) == 1, "Off: every key goes on");
    usleep(300000);
    chk(h_iso_n == in && !log_has("keys:"), "Off: nothing set, no log");
    chk(menu_set("Quick keys") == 0 && wf_qk == WF_QK_DOWN, "the row: DOWN ring again");
    /* Magic Zoom with manual focus */
    log_mark();
    chk(cbr_key(QK_SET) == 1 && LOGW("keys: Magic Zoom on", 3000), "MF: SET turns Magic Zoom on (SET goes on: a long SET is "
        "Canon's Quick Control)");
    chk(WAIT(h_mz_cd == 40, 2000), "Magic Zoom: its countdown set (%d)", h_mz_cd);
    int m0 = h_mz_n;
    usleep(400000);
    chk(h_mz_n >= m0 + 3, "kept up every pass (%d in 0.4 s)", h_mz_n - m0);
    log_mark();
    chk(cbr_key(QK_SET) == 1 && LOGW("keys: Magic Zoom off", 3000) && WAIT(h_mz_cd == 0, 2000), "SET again: off (%d)", h_mz_cd);
    m0 = h_mz_n;
    usleep(300000);
    chk(h_mz_n == m0, "off: the countdown is left alone");
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom on", 3000), "SET: on");
    log_mark();
    gui_state = 1;                                /* a long SET: Canon's Quick Control screen */
    chk(LOGW("keys: Magic Zoom off (the screen changed)", 3000) && WAIT(h_mz_cd == 0, 2000), "a long SET (Canon's Q "
        "screen): Magic Zoom off");
    gui_state = 0;
    log_mark();
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom on", 3000), "SET: on");
    af_mode = 0;
    chk(LOGW("keys: Magic Zoom off (focus on AF again)", 3000) && WAIT(h_mz_cd == 0, 2000), "focus back on AF: off");
    log_mark();
    m0 = h_mz_n;
    chk(cbr_key(QK_SET) == 1, "AF: SET goes on");
    usleep(300000);
    chk(!log_has("Magic Zoom") && h_mz_n == m0, "AF: SET does nothing (as before)");
    af_mode = 3;
    h_mz_en = 0;
    log_mark();
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom needs ML Overlay > Magic Zoom", 3000) && h_mz_n == m0, "Magic Zoom off in ML: told, not set");
    h_mz_en = 1;
    h_gdraw = 0;
    log_mark();
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom needs ML Overlay > Global Draw", 3000), "Global Draw off: told");
    h_gdraw = 1;
    /* a lens without contacts: manual focus by itself, no aperture ring */
    h_li_set("name0", 0);
    af_mode = 0;
    log_mark();
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom on", 3000), "a lens without contacts counts as MF");
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom off", 3000), "SET: off");
    log_mark();
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring ISO", 3000), "no contacts: DOWN = ISO ring");
    cbr_key(QK_DOWN);
    chk(LOGW("keys: ring back to the camera", 3000) && !log_has("ring aperture"), "no contacts: no aperture ring");
    h_li_set("name0", 'E');
    af_mode = 3;
    log_mark();
    cbr_key(QK_SET);
    chk(LOGW("keys: Magic Zoom on", 3000), "SET: on before the unload");
    mod_deinit();
    chk(h_mz_cd == 0, "unload: Magic Zoom from the keys goes with the module (%d)", h_mz_cd);
}

/* OSVF: the film OS's LiveView finder (src/wf_osvf.c, docs/MODERNUI.md M3). Its two lvinfo items, called as ML's
   lvinfo.c calls them (0 px wide while it is down, the whole bar and custom drawing while it is up, painting at the
   bar's row); when it comes up and goes down from wf_task (each logged): the ML menu, Global Draw off / Canon's own
   view (INFO), the finder set to Canon + ML, Camera interface Canon + ML, PLAY, movie mode, the display off, out of
   LiveView; ML asked to redraw its bars on each change and when a value changes; the middle painted every 500 ms and
   at once in a new mode, given back when it goes in plain LiveView, left alone outside LiveView. */
extern int wf_stub_hud_fonts, wf_stub_hud_n[3], wf_stub_hud_y[3], wf_stub_hud_vf[3];
void wf_t_lvi_call(void *items, int i, int can_draw, int y, int *w, int *h, int *custom, int *bar, int *prio);
const char *wf_t_lvi_name(void *items, int i);
static int vf_item(int i, int can_draw, int bar_y, int *w)
{
    int h, custom, bar, prio;
    wf_t_lvi_call(h_lvinfo_items, i, can_draw, bar_y + 2, w, &h, &custom, &bar, &prio);
    return *w == 0 ? custom == 0 : h == 32 && custom == 1 && prio == 127;
}
static void vf_flip(const char *what, volatile int *knob, int down_v, int up_v)
{
    log_mark();
    int d0 = h_lens_dirty;
    *knob = down_v;
    chk(LOGW("osvf: LiveView finder down", 3000), "%s: the finder goes down ('%s')", what, log_line("osvf:"));
    int w = -1;
    chk(vf_item(0, 0, 0, &w) && w == 0 && vf_item(1, 0, 448, &w) && w == 0, "%s: both items 0 px (ML's own items back)",
        what);
    chk(h_lens_dirty > d0, "%s: ML asked to redraw its bars", what);
    log_mark();
    *knob = up_v;
    chk(LOGW("osvf: LiveView finder up", 3000), "%s back: the finder up again ('%s')", what, log_line("osvf:"));
    chk(vf_item(0, 0, 0, &w) && w == 720, "%s back: the top item takes the bar (%d px)", what, w);
}
static void t_osvf(void)
{
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\nos=on\nfinder=full\n", wf_films[fC].id);
    shooting_mode = 3;                           /* M */
    lv = 1;
    h_gdraw = 1;
    h_menu_shown = 0;
    h_play = 0;
    h_movie = 0;
    h_disp = 1;
    wf_stub_hud_fonts = 1;
    mod_init(0);
    chk(h_lvinfo_n == 2 && h_lvinfo_items, "two lvinfo items registered (%d)", h_lvinfo_n);
    chk(h_lvinfo_items && !strcmp(wf_t_lvi_name(h_lvinfo_items, 0), "WindyFilm finder")
        && strcmp(wf_t_lvi_name(h_lvinfo_items, 0), wf_t_lvi_name(h_lvinfo_items, 1)),
        "the items: 'WindyFilm finder' + a second name of its own (ML's menu lists them by name)");
    chk(wf_os == 1 && wf_vf == WF_VF_FULL, "config: os=on finder=full (%d %d)", wf_os, wf_vf);
    chk(LOGW("osvf: LiveView finder up (mode 0, bars at 0 / 448)", 3000), "the finder comes up ('%s')",
        log_line("osvf:"));
    int w = -1, n0[3];
    chk(vf_item(0, 0, 0, &w) && w == 720, "top item: the whole bar, priority 127, custom drawing (%d px)", w);
    chk(vf_item(1, 0, 448, &w) && w == 720, "bottom item: the whole bar (%d px)", w);
    memcpy(n0, wf_stub_hud_n, sizeof n0);
    vf_item(0, 1, 0, &w);
    vf_item(1, 1, 448, &w);
    chk(wf_stub_hud_n[0] == n0[0] + 1 && wf_stub_hud_y[0] == 0 && wf_stub_hud_vf[0] == WF_VF_FULL,
        "can_draw: the top band painted at row 0 (%d calls, row %d)", wf_stub_hud_n[0] - n0[0], wf_stub_hud_y[0]);
    chk(wf_stub_hud_n[1] == n0[1] + 1 && wf_stub_hud_y[1] == 448, "can_draw: the bottom band at row 448 (row %d)",
        wf_stub_hud_y[1]);
    chk(WAIT(wf_stub_hud_n[2] > 0, 2000) && wf_stub_hud_y[2] == 32 && wf_stub_hud_vf[2] == WF_VF_FULL,
        "the middle painted from wf_task, under the top bar (row %d, mode %d)", wf_stub_hud_y[2], wf_stub_hud_vf[2]);
    int m1 = wf_stub_hud_n[2];
    usleep(1300000);
    chk(wf_stub_hud_n[2] >= m1 + 2, "the middle again every 500 ms (%d in 1.3 s)", wf_stub_hud_n[2] - m1);
    /* a value of ours changes: ML asked to redraw its bars (our bands) */
    int d0 = h_lens_dirty;
    h_ae_value = -12;
    chk(WAIT(h_lens_dirty > d0, 2000), "the meter moved: ML asked to redraw its bars");
    usleep(300000);
    d0 = h_lens_dirty;
    usleep(600000);
    chk(h_lens_dirty == d0, "nothing changed: no redraw asked (%d)", h_lens_dirty - d0);
    /* a mode change: the middle again at once in the new mode (the old marks cleared first) */
    wf_vf_set(WF_VF_LINES);
    chk(WAIT(wf_stub_hud_vf[2] == WF_VF_LINES, 1000), "Bright lines: the middle in the new mode (%d)", wf_stub_hud_vf[2]);
    vf_item(0, 1, 0, &w);
    chk(wf_stub_hud_vf[0] == WF_VF_LINES, "Bright lines: the bands in the new mode");
    wf_vf_set(WF_VF_FULL);
    /* the ways down and back */
    vf_flip("the ML menu", &h_menu_shown, 1, 0);
    vf_flip("Global Draw off / Canon's view (INFO)", &h_gdraw, 0, 1);
    vf_flip("PLAY", &h_play, 1, 0);
    vf_flip("movie mode", &h_movie, 1, 0);
    vf_flip("the display off", &h_disp, 0, 1);
    /* the finder set to Canon + ML, Camera interface Canon + ML (the setters: no knob) */
    log_mark();
    int mc = wf_stub_hud_n[2];
    wf_vf_set(WF_VF_OFF);
    chk(LOGW("osvf: LiveView finder down", 3000), "finder Canon + ML: down");
    chk(WAIT(wf_stub_hud_n[2] > mc && wf_stub_hud_vf[2] == -1, 1000), "down in plain LiveView: the middle given back (%d)",
        wf_stub_hud_vf[2]);
    chk(vf_item(0, 0, 0, &w) && w == 0, "finder Canon + ML: 0 px");
    log_mark();
    wf_vf_set(WF_VF_MIN);
    chk(LOGW("osvf: LiveView finder up (mode 1", 3000), "Minimal: up ('%s')", log_line("osvf:"));
    log_mark();
    wf_os_set(0);
    chk(LOGW("osvf: LiveView finder down", 3000), "Camera interface Canon + ML: down");
    log_mark();
    wf_os_set(1);
    chk(LOGW("osvf: LiveView finder up", 3000), "Camera interface WindyFilm: up");
    /* outside LiveView: down, no middle painted or cleared (the screen is not LiveView's) */
    log_mark();
    lv = 0;
    chk(LOGW("osvf: LiveView finder down", 3000), "out of LiveView: down");
    mc = wf_stub_hud_n[2];
    usleep(300000);
    chk(wf_stub_hud_n[2] == mc, "out of LiveView: the middle left alone (%d calls)", wf_stub_hud_n[2] - mc);
    log_mark();
    lv = 1;
    chk(LOGW("osvf: LiveView finder up", 3000), "LiveView back: up");
    /* the config keeps the finder's mode */
    wf_vf_set(WF_VF_LINES);
    usleep(1500000);
    mod_deinit();
    long n;
    char *cfg = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(cfg && strstr(cfg, "finder=lines\n"), "config saved with finder=lines");
    free(cfg);
    chk(vf_item(0, 0, 0, &w) && w == 0 && vf_item(1, 0, 448, &w) && w == 0,
        "unloaded: both items 0 px for good (ML never frees module items)");
    wf_stub_hud_fonts = 0;
}

/* the 5D2's Top plate (docs/MODERNUI.md M4): wf_task opens the UI on it (the stub logs "browser: not in this build
   (screen 4)") when Canon's shooting screen without LiveView has been up for 300 ms; once per screen (re-armed when that
   screen goes); never with Top plate off, Camera interface Canon + ML, in LiveView, under a menu / PLAY / a Canon
   screen; never on the EOS M. The config keeps plate=on|off */
static int plate_opened(int ms)
{
    return LOGW("plate: Canon's shooting screen: the Top plate", ms) && LOGW("browser: not in this build (screen 4)", 500);
}

static void plate_screen_again(volatile int *flag, int off, int on)
{
    *flag = off;
    usleep(300000);
    log_mark();
    *flag = on;
}

static void t_plate(void)
{
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\nos=on\nplate=on\n", wf_films[fC].id);
    shooting_mode = 2;                           /* Av */
    lv = 1;
    h_menu_shown = h_play = h_movie = 0;
    h_disp = 1;
    gui_state = 0;
    h_gui_mode = 0;
    mod_init(0);
    chk(wf_plate == 1, "config: plate=on (%d)", wf_plate);
    chk(wf_playv == 0, "config: play=canon by default (%d)", wf_playv);
    log_mark();
    usleep(800000);
    chk(!log_has("plate:"), "LiveView: no Top plate");
    lv = 0;
#ifdef WF_BUILD_5D2
    chk(plate_opened(3000), "out of LiveView, Canon's shooting screen: the Top plate ('%s')", log_line("plate:"));
    log_mark();
    usleep(900000);
    chk(!log_has("plate:"), "once per screen ('%s')", log_line("plate:"));
    plate_screen_again(&h_disp, 0, 1);
    chk(plate_opened(3000), "the display off and on again: the Top plate again");
    plate_screen_again(&h_gui_mode, 3, 0);
    chk(plate_opened(3000), "back from a Canon screen (gui mode 3): the Top plate again");
    plate_screen_again(&gui_state, 1, 0);
    chk(plate_opened(3000), "Canon's GUI busy and idle again: the Top plate again");
    /* not while something is on top; once it goes, the plate */
    static const char *const top_name[3] = { "the ML menu", "PLAY", "a Canon screen" };
    volatile int *const top_flag[3] = { &h_menu_shown, &h_play, &h_gui_mode };
    for (int k = 0; k < 3; k++) {
        h_disp = 0;
        usleep(300000);
        *top_flag[k] = k == 2 ? 3 : 1;
        h_disp = 1;
        log_mark();
        usleep(800000);
        chk(!log_has("plate:"), "under %s: no Top plate", top_name[k]);
        *top_flag[k] = 0;
        chk(plate_opened(3000), "%s gone: the Top plate", top_name[k]);
    }
    /* Canon saving a shot: not yet */
    h_disp = 0;
    usleep(300000);
    h_li_set("job", 1);
    log_mark();
    h_disp = 1;
    usleep(800000);
    chk(!log_has("plate:"), "Canon saving a shot: no Top plate yet");
    h_li_set("job", 0);
    chk(plate_opened(3000), "the shot saved: the Top plate");
    /* Top plate off: never; on again while the screen is up: not this screen, the next one */
    wf_plate_set(0);
    plate_screen_again(&h_disp, 0, 1);
    usleep(900000);
    chk(!log_has("plate:"), "Top plate off: no Top plate");
    wf_plate_set(1);
    usleep(900000);
    chk(!log_has("plate:"), "Top plate on again: not over the screen that is up");
    plate_screen_again(&h_disp, 0, 1);
    chk(plate_opened(3000), "Top plate on, the next screen: the Top plate");
    /* the plate's dials: main = shutter (M, Tv) / aperture (Av); rear = aperture (M) / compensation (P, Tv, Av) */
    h_li_set("name0", 'E');
    h_li_set("amin", 24);
    h_li_set("amax", 80);
    shooting_mode = 3;
    h_li_set("shutter", 96);
    h_li_set("aperture", 40);
    int tv0 = h_tv_n, av0 = h_av_n, ae0 = h_ae_n;
    chk(wf_plate_dial(0, 1) == 1 && h_li_get("shutter") == 99 && h_tv_n == tv0 + 1, "M, main dial right: shutter 96 -> "
        "99 (%d)", h_li_get("shutter"));
    chk(wf_plate_dial(1, -1) == 1 && h_li_get("aperture") == 37 && h_av_n == av0 + 1, "M, rear wheel up: aperture 40 -> "
        "37 (%d)", h_li_get("aperture"));
    shooting_mode = 2;
    chk(wf_plate_dial(0, 1) == 1 && h_li_get("aperture") == 40, "Av, main dial: aperture back to 40 (%d)",
        h_li_get("aperture"));
    h_li_set("ae", 0);
    chk(wf_plate_dial(1, 1) == 1 && h_li_get("ae") == 3 && h_ae_n == ae0 + 1, "Av, rear wheel down: +1/3 EV (ae %d)",
        h_li_get("ae"));
    h_li_set("ae", 16);
    chk(wf_plate_dial(1, 1) == 0 && h_li_get("ae") == 16, "+2 EV: the end (ae %d)", h_li_get("ae"));
    shooting_mode = 4;
    chk(wf_plate_dial(0, 1) == 0 && wf_plate_dial(1, 1) == 0, "BULB: the dials change nothing");
    h_li_set("ae", 0);
    shooting_mode = 2;
    /* Camera interface Canon + ML: never */
    wf_os_set(0);
    plate_screen_again(&h_disp, 0, 1);
    usleep(900000);
    chk(!log_has("plate:"), "Camera interface Canon + ML: no Top plate");
    wf_os_set(1);
#else
    usleep(1200000);
    chk(!log_has("plate:"), "EOS M: no Top plate ('%s')", log_line("plate:"));
#endif
    wf_plate_set(0);
    usleep(1500000);
    mod_deinit();
    long n;
    char *cfg = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(cfg && strstr(cfg, "plate=off\n"), "config saved with plate=off");
    free(cfg);
    lv = 1;
    shooting_mode = 3;
}

static void t_iso(void)
{
    int k400 = k_speed(400), k50 = k_speed(50), k250 = k_speed(250);
    chk(k400 >= 0 && k50 >= 0 && k250 >= 0, "catalogue films of speed 400 / 50 / 250 (%d %d %d)", k400, k50, k250);
    if (k400 < 0 || k50 < 0 || k250 < 0)
        return;
    struct pk e[] = { { k400, 0, tab_make(0, 0, 0, 0) }, { k50, 0, tab_make(0, 0, 0, 0) }, { k250, 0, tab_make(0, 0, 0, 0) } };
    pack_write(e, 3, 0);
    const int i400 = wf_films[k400].id, i50 = wf_films[k50].id, i250 = wf_films[k250].id;
    h_li_set("iso", 0);                          /* Auto ISO */
    h_set_folder_number(100);
    h_set_file_number(300);
    mod_init(1);
    usleep(1200000);
    chk(h_iso_n == 0 && !log_has(": ISO "), "boot: the ISO untouched (%d calls)", h_iso_n);
    chk(WAIT(wf_load(i400) == WF_LOAD_OK, 5000), "film %d (ISO 400) loads", i400);
    usleep(600000);
    chk(h_iso_n == 0 && h_li_get("iso") == 0, "a bare wf_load (the Darkroom's own film changes): no ISO (%d calls)",
        h_iso_n);
    log_mark();
    wf_iso_film(i400, 0);
    chk(WAIT(h_iso_n == 1, 3000) && h_li_get("iso") == 88 && LOGW("load: ISO 400 (film speed, was Auto ISO)", 3000),
        "SET on the 400 film: ISO 400 from Auto (code %d, '%s')", h_li_get("iso"), log_line("load: ISO"));
    log_mark();
    chk(wf_load(i50) == WF_LOAD_OK, "film %d (ISO 50) loads", i50);
    wf_iso_film(i50, 0);
    chk(WAIT(h_iso_n == 2, 3000) && h_li_get("iso") == 72
        && LOGW("load: ISO 100 (film speed 50, the nearest the camera takes)", 3000),
        "the 50 film: ISO 100, the lowest (code %d, '%s')", h_li_get("iso"), log_line("load: ISO"));
    h_htp = 1;
    log_mark();
    wf_iso_film(i50, 0);
    chk(WAIT(h_iso_n == 3, 3000) && h_li_get("iso") == 80
        && LOGW("load: ISO 200 (film speed 50, the nearest the camera takes)", 3000),
        "Highlight Tone Priority: ISO 200 is the floor (code %d, '%s')", h_li_get("iso"), log_line("load: ISO"));
    h_htp = 0;
    h_iso_whole = 1;
    log_mark();
    chk(wf_load(i250) == WF_LOAD_OK, "film %d (ISO 250) loads", i250);
    wf_iso_film(i250, 0);
    chk(WAIT(h_iso_n == 5, 3000) && h_li_get("iso") == 80
        && LOGW("load: ISO 200 (film speed 250; the camera takes whole stops only)", 3000),
        "a body on whole stops: 250 refused, ISO 200 (%d calls, code %d, '%s')", h_iso_n, h_li_get("iso"),
        log_line("load: ISO"));
    h_iso_whole = 0;
    log_mark();
    wf_iso_film(i250, 0);
    chk(WAIT(h_iso_n == 6, 3000) && h_li_get("iso") == 83 && LOGW("load: ISO 250 (film speed)", 3000),
        "1/3 stops again: ISO 250 (code %d)", h_li_get("iso"));
    h_iso_refuse = 1;
    log_mark();
    chk(wf_load(i400) == WF_LOAD_OK, "film %d loads again", i400);
    wf_iso_film(i400, 0);
    chk(WAIT(h_iso_n == 7, 3000) && LOGW("load: ISO 400 not taken by the camera (ISO stays 250)", 3000)
        && h_li_get("iso") == 83, "a body that takes no ISO: said, the ISO stays 250 ('%s')", log_line("load: ISO"));
    h_iso_refuse = 0;
    log_mark();                                  /* a roll load (both cameras: the 5D2 has rolls since dark-5d2) */
    int rc = wf_roll_req_load(i50, 3);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000) && LOGW("roll: 001 loaded", 3000)
        && LOGW("roll: ISO 100 (film speed 50, the nearest the camera takes)", 3000) && h_li_get("iso") == 72,
        "a roll of the 50 film: loaded, ISO 100 (%d, code %d, '%s')", rc, h_li_get("iso"), log_line("roll: ISO"));
    usleep(600000);
    chk(h_iso_n == 8, "one ISO set for the roll load (%d calls)", h_iso_n);
    mod_deinit();
}

static void t_roll(const char *cs)
{
    if (!strcmp(cs, "shelf")) {
        t_roll_shelf();
        return;
    }
    if (!strcmp(cs, "shelfboot")) {
        t_roll_shelfboot();
        return;
    }
    if (!strcmp(cs, "mem")) {
        t_roll_mem();
        return;
    }
    if (!strcmp(cs, "jpeg")) {
        t_roll_jpeg();
        return;
    }
    if (!strcmp(cs, "cr2mv")) {
        t_roll_cr2mv();
        return;
    }
    if (!strcmp(cs, "save") || !strcmp(cs, "short") || !strcmp(cs, "back")) {
        t_roll_save(cs);
        return;
    }
    if (!strcmp(cs, "lock")) {
        t_roll_lock();
        return;
    }
    if (!strcmp(cs, "dark") || !strcmp(cs, "boot")) {
        t_roll_dark(cs);
        return;
    }
    std_pack();
    h_set_folder_number(100);
    h_set_file_number(206);
    mod_init(1);
    chk(menu_named("Film roll") && menu_named("Blind roll") && menu_named("Darkroom") && menu_named("End roll now")
        && menu_named("Close roll"), "menu: Film roll, Blind roll, Darkroom, End roll now, Close roll");
    chk(wf_roll_now()->state == WF_RS_EMPTY && cbr_key(WF_KEY_PLAY) == 1, "no roll: PLAY passes");
    usleep(300000);
    chk(h_uilock_n == 0 && !log_has("roll: shutter") && !log_has("roll: image quality") && cbr_key(H_KEY_HALFSHUTTER) == 1
        && cbr_key(H_KEY_FULLSHUTTER) == 1, "no roll: as before (no UI lock (%d calls), no RAW+L, the shutter keys pass)",
        h_uilock_n);
    const int film = wf_films[fA].id;
    log_mark();
    wf_rawb = 1;
    int rc = wf_roll_req_load(film, 12);
    chk(rc == 0, "RAW develop on: the roll load is queued (%d)", rc);
    chk(WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000) && LOGW("roll: RAW develop stays on", 3000) && wf_rawb == 1,
        "roll loaded, RAW develop stays on (state %d, rawb %d)", wf_roll_now()->state, wf_rawb);
    chk(wf_roll_shooting() && wf_blind && cbr_key(WF_KEY_PLAY) == 0, "roll loaded: it shoots, PLAY blocked (Blind roll)");
    chk(menu_named("RAW develop") && menu_set("RAW develop") == -1, "menu RAW develop: a plain toggle again (no select)");
    wf_rawb = 0;
    usleep(600000);
    chk(wf_roll_now()->state == WF_RS_LOADED && wf_rawb == 0, "RAW develop off: the roll stays loaded (%d)",
        wf_roll_now()->state);
    wf_rawb = 1;
    usleep(600000);
    chk(wf_roll_now()->state == WF_RS_LOADED && wf_roll_shooting() && !log_has("roll: RAW develop off"),
        "RAW develop on again: the roll stays loaded, no switch-off either way (%d)", wf_roll_now()->state);
    put_file("DCIM/100CANON/IMG_0207.CR2", "II*", 3);
    h_set_file_number(207);
    chk(WAIT(wf_roll_now()->nshot == 1, 5000), "a RAW+L frame counts with RAW develop on (%d/12)", wf_roll_now()->nshot);
    h_set_file_number(208);                         /* a JPEG-only shot: no CR2, not a frame */
    usleep(600000);
    chk(wf_roll_now()->nshot == 1, "a shot without a CR2 does not count (%d/12)", wf_roll_now()->nshot);
    chk(wf_roll_req_rewind() == 0 && WAIT(wf_roll_now()->state == WF_RS_REWOUND, 5000) && wf_roll_now()->nshot == 1
        && !wf_roll_shooting() && wf_rawb == 1, "End roll now: rewound with its frame, RAW develop stays on (state %d)",
        wf_roll_now()->state);
    chk(LOGW("roll: end notice rewound", 3000), "End roll now: the end notice shows (rewound)");
    chk(LOGW("roll: shutter blocked (rewound)", 3000) && (icu_uilock & 1), "rewound with a frame to develop: the shutter held "
        "(UI lock %x)", icu_uilock);
    chk(cbr_key(WF_KEY_PLAY) == 0, "rewound roll: PLAY still blocked until it is developed or closed");
    chk(wf_roll_req_load(film, 24) == -1, "a roll with frames in the camera: a new load is refused");
    chk(wf_roll_req_close() == 0 && WAIT(wf_roll_now()->state == WF_RS_DONE, 5000) && wf_rawb == 1
        && cbr_key(WF_KEY_PLAY) == 1, "Close roll: done, RAW develop stays on, PLAY passes");
    chk(LOGW("roll: shutter free", 3000) && !(icu_uilock & 1) && cbr_key(H_KEY_HALFSHUTTER) == 1
        && LOGW("roll: image quality back to", 3000), "closed: as with no roll (shutter free, lock %x, image quality back)",
        icu_uilock);
    wf_rawb = 0;
    rc = wf_roll_req_load(film, 12);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000) && wf_rawb == 0,
        "RAW develop off: a roll loads, RAW develop stays off (%d, %d)", rc, wf_rawb);
    chk(wf_roll_req_close() == 0 && WAIT(wf_roll_now()->state == WF_RS_DONE, 5000), "the empty roll closes");
    /* the end-of-roll notice (owner 11-15): the 12th frame shows it, SET opens the Darkroom while it is up (no UI in
     * this build: the stub logs it), a key drops it, LiveView back shows it again */
    rc = wf_roll_req_load(film, 12);
    chk(rc == 0 && WAIT(wf_roll_now()->state == WF_RS_LOADED, 5000), "a roll of 12 loads (%d)", rc);
    usleep(600000);
    log_mark();
    int ok = 1;
    for (int i = 0; i < 12 && ok; i++) {
        char p[64];
        snprintf(p, sizeof p, "DCIM/100CANON/IMG_%04d.CR2", 209 + i);
        put_file(p, "II*", 3);
        h_set_file_number(209 + i);
        ok = WAIT(wf_roll_now()->nshot == i + 1, 5000);
    }
    chk(ok && wf_roll_now()->state == WF_RS_FULL && LOGW("roll: end notice full", 3000) && log_has(", end of roll"),
        "12 frames: FULL, the end notice shows (%d/12, state %d)", wf_roll_now()->nshot, wf_roll_now()->state);
    chk(LOGW("roll: shutter blocked (full)", 3000) && (icu_uilock & 1) && wf_roll_shutter_locked() == 1,
        "a full roll: the shutter held (ML UI lock %x)", icu_uilock);
    usleep(300000);
    chk(cbr_key(H_KEY_SET) == 0 && LOGW("browser: not in this build (screen 2)", 2000),
        "SET while the notice is up: taken, the Darkroom is asked for");
    usleep(600000);
    chk(cbr_key(H_KEY_SET) == 1, "SET after the notice went: passes to Canon");
    log_mark();
    lv = 0;
    usleep(600000);
    chk(!log_has("roll: end notice"), "no LiveView: no notice");
    lv = 1;
    chk(LOGW("roll: end notice full", 3000), "LiveView back on a full roll: the notice again");
    log_mark();
    chk(cbr_key(H_KEY_INFO) == 0 && LOGW("browser: not in this build (screen 1)", 2000),
        "INFO while the notice is up: taken, the browser in Darkroom mode is asked for (a new roll)");
    usleep(300000);
    log_mark();
    chk(cbr_key(H_KEY_HALFSHUTTER) == 0 && cbr_key(H_KEY_FULLSHUTTER) == 0 && LOGW("roll: end notice full", 3000),
        "half / full shutter on the full roll: kept from Canon, the notice at once");
    h_play = 1;
    chk(cbr_key(H_KEY_HALFSHUTTER) == 1, "in PLAY the half-shutter passes (Canon's way back; ML's lock still holds)");
    h_play = 0;
    log_mark();
    h_movie = 1;
    lv = 0;
    usleep(600000);
    lv = 1;
    usleep(900000);
    chk(!log_has("roll: end notice"), "movie mode: no notice");
    chk(log_has("roll: shutter free") && !(icu_uilock & 1) && cbr_key(H_KEY_HALFSHUTTER) == 1,
        "movie mode: the shutter free (lock %x)", icu_uilock);
    h_movie = 0;
    chk(LOGW("roll: shutter blocked (full)", 3000) && (icu_uilock & 1), "photo mode again: held again");
    log_mark();
    __recording = 1;
    chk(LOGW("roll: shutter free", 3000) && WAIT(!(icu_uilock & 1), 1000), "recording: free");
    __recording = 0;
    chk(WAIT(icu_uilock & 1, 3000), "not recording: held again");
    log_mark();
    chk(wf_roll_req_close() == 0 && WAIT(wf_roll_now()->state == WF_RS_DONE, 5000), "the full roll closes");
    chk(LOGW("roll: shutter free", 3000) && !(icu_uilock & 1) && wf_roll_shutter_locked() == 0, "closed: the shutter free");
    long n = 0;
    char *b = 0;
    for (int i = 0; i < 50; i++) {                  /* wf_task writes the file after the state changed in memory */
        free(b);
        b = get_file("ML/SETTINGS/WFROLL.TXT", &n);
        if (b && strstr(b, "state=done"))
            break;
        usleep(100000);
    }
    chk(b && n == 1536 && strstr(b, "state=done"), "roll file: 1536 bytes, state=done (%ld)", n);
    free(b);
    mod_deinit();
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

/* ------------------------------------------------------------------ PAPER: the print's paper (owner 11-16, mk_paper.py)
 * Glossy = the tables before the feature, bit for bit. Matte: Dmax ~1.7 instead of ~2.2 (black at code ~28), the
 * shadows a little flatter and less saturated, white kept. Warm / Cool: a cream / blue-white base in the highlights and
 * a light tone in the shadows, the neutral axis tinted, not the picture. Last in every table (after Highlights /
 * Shadows); plain Log and Off have none. */
static int pp_c16(int p, int c, int code) { return wf_paper_ch(p, c, code * 16); }
static void t_paper(void)
{
    /* 1. the model */
    int bad = 0, dec = 0;
    for (int c = 0; c < 3; c++)
        for (int x = -64; x <= 4160; x++)
            bad += wf_paper_ch(WF_PAPER_GLOSSY, c, x) != x;
    int y = 1000, u = -300, v = 200;
    wf_paper_ycc16(WF_PAPER_GLOSSY, &y, &u, &v);
    chk(bad == 0 && y == 1000 && u == -300 && v == 200, "Glossy: every channel and Y'CbCr unchanged (%d off)", bad);
    for (int p = WF_PAPER_MATTE; p < WF_NPAPER; p++)
        for (int c = 0; c < 3; c++)
            for (int x = 1; x <= 4096; x++)
                dec += wf_paper_ch(p, c, x) < wf_paper_ch(p, c, x - 1);
    chk(dec == 0, "Matte / Warm / Cool: every channel non-decreasing (%d steps down)", dec);
    int m0 = pp_c16(WF_PAPER_MATTE, 0, 0), m118 = pp_c16(WF_PAPER_MATTE, 0, 118), m255 = pp_c16(WF_PAPER_MATTE, 0, 255);
    int same = 1;
    for (int x = 0; x < 4096; x += 7)
        same &= pp_c16(WF_PAPER_MATTE, 1, x) == pp_c16(WF_PAPER_MATTE, 0, x)
                && pp_c16(WF_PAPER_MATTE, 2, x) == pp_c16(WF_PAPER_MATTE, 0, x);
    chk(same && m0 >= 27 * 16 && m0 <= 29 * 16 && m118 >= 119 * 16 && m118 <= 123 * 16 && abs(m255 - 255 * 16) <= 8,
        "Matte: one curve for R / G / B; black -> %.1f, 118 -> %.1f, 255 -> %.1f", m0 / 16.0, m118 / 16.0, m255 / 16.0);
    int s1 = pp_c16(WF_PAPER_MATTE, 0, 48) - pp_c16(WF_PAPER_MATTE, 0, 16);
    int s2 = pp_c16(WF_PAPER_MATTE, 0, 230) - pp_c16(WF_PAPER_MATTE, 0, 200);
    /* the surface flare adds the same light everywhere: it flattens the deepest tones most (~0.7 there), not the top */
    chk(s1 < 26 * 16 && s1 > 18 * 16 && s2 >= 29 * 16, "Matte: the shadows flatter (16..48 spans %.1f codes of 32), the "
        "highlights not (200..230 spans %.1f of 30)", s1 / 16.0, s2 / 16.0);
    int wr = pp_c16(WF_PAPER_WARM, 0, 255), wg = pp_c16(WF_PAPER_WARM, 1, 255), wb = pp_c16(WF_PAPER_WARM, 2, 255);
    int cr = pp_c16(WF_PAPER_COOL, 0, 255), cg = pp_c16(WF_PAPER_COOL, 1, 255), cb = pp_c16(WF_PAPER_COOL, 2, 255);
    chk(abs(wr - 4080) <= 8 && wg > wb && abs(wg - 250 * 16) <= 16 && abs(wb - 244 * 16) <= 16,
        "Warm white: %.1f %.1f %.1f (cream: R > G > B)", wr / 16.0, wg / 16.0, wb / 16.0);
    chk(abs(cb - 4080) <= 8 && cg > cr && abs(cg - 249 * 16) <= 16 && abs(cr - 247 * 16) <= 16,
        "Cool white: %.1f %.1f %.1f (blue-white: B > G > R)", cr / 16.0, cg / 16.0, cb / 16.0);
    int nu = 0;
    for (int yy = 0; yy <= 4080; yy += 16) {
        y = yy; u = 0; v = 0;
        wf_paper_ycc16(WF_PAPER_MATTE, &y, &u, &v);
        nu += u != 0 || v != 0;
    }
    chk(nu == 0, "Matte keeps grey grey: Cb = Cr = 0 on the whole axis (%d off)", nu);
    int tint[2][2][2];                           /* [warm/cool][white/deep][Cb/Cr] */
    for (int p = 0; p < 2; p++)
        for (int d = 0; d < 2; d++) {
            y = d ? 24 * 16 : 4080; u = v = 0;
            wf_paper_ycc16(p ? WF_PAPER_COOL : WF_PAPER_WARM, &y, &u, &v);
            tint[p][d][0] = u; tint[p][d][1] = v;
        }
    chk(tint[0][0][0] < 0 && tint[0][0][1] > 0 && tint[0][1][0] < 0 && tint[0][1][1] > 0,
        "Warm: grey gets Cb < 0, Cr > 0 at white (%d, %d) and in the shadows (%d, %d), x16", tint[0][0][0], tint[0][0][1],
        tint[0][1][0], tint[0][1][1]);
    chk(tint[1][0][0] > 0 && tint[1][0][1] < 0 && tint[1][1][0] > 0 && tint[1][1][1] < 0,
        "Cool: grey gets Cb > 0, Cr < 0 at white (%d, %d) and in the shadows (%d, %d), x16", tint[1][0][0], tint[1][0][1],
        tint[1][1][0], tint[1][1][1]);
    chk(abs(tint[0][0][0]) < 8 * 16 && abs(tint[0][0][1]) < 8 * 16 && abs(tint[1][0][0]) < 8 * 16
        && abs(tint[1][0][1]) < 8 * 16, "the tints stay subtle (under 8 codes of Cb / Cr at white)");
    int pk = 0;
    for (int p = 0; p < WF_NPAPER; p++) {
        int f = WF_FT_PAPER(p) | WF_FT_HS(3, -2) | WF_FT_S(125) | WF_FT_V21;
        pk += WF_FT_POF(f) != p || WF_FT_HOF(f) != 3 || WF_FT_SHOF(f) != -2 || WF_FT_SOF(f) != 125
              || (WF_FT_PAPER(p) & 0x00ffffff) != 0;
    }
    chk(pk == 0 && WF_FT_PAPER(0) == 0, "WF_FT_PAPER: bits 24-25, Glossy adds no bit, strength / H / S untouched (%d bad)", pk);
    chk(!strcmp(wf_paper_name(0), "Glossy") && !strcmp(wf_paper_name(1), "Matte") && !strcmp(wf_paper_name(2), "Warm")
        && !strcmp(wf_paper_name(3), "Cool") && !strcmp(wf_paper_name(9), "Glossy"), "names Glossy / Matte / Warm / Cool");

    /* 2. film sets: Glossy is the builder before the feature (the HS gold, built before Highlights / Shadows too) */
    static struct wf_ftabs t1, t2, t3;
    int ng = (int)(sizeof hs_gold / sizeof *hs_gold), gb = 0;
    size_t end = offsetof(struct wf_ftabs, src);
    for (int i = 0; i < ng; i++) {
        uint32_t *T = tab_make(hs_gold[i].dy, hs_gold[i].du, hs_gold[i].dv, hs_gold[i].mono);
        memset(&t1, 0, sizeof t1);
        wf_film_build(&t1, T, hs_gold[i].flags | WF_FT_PAPER(WF_PAPER_GLOSSY));
        gb += fnv(2166136261u, (const uint8_t *)&t1, end) != hs_gold[i].fnv;
        free(T);
    }
    chk(gb == 0, "Glossy: %d film sets bit-identical to the builder before the Paper row (%d differ)", ng, gb);
    uint32_t *TI = tab_make(0, 0, 0, 0), *TC = tab_make(12, 6, -6, 0), *TM = tab_make(0, 0, 0, 1);
    memset(&t1, 0, sizeof t1);
    memset(&t2, 0, sizeof t2);
    wf_film_build(&t1, TI, WF_FT_V21);
    wf_film_build(&t2, TI, WF_FT_V21 | WF_FT_PAPER(WF_PAPER_MATTE));
    int ndec = 0;
    for (int i = 1; i < 256; i++) ndec += t2.N[i] < t2.N[i - 1];
    chk(t2.N[0] >= 27 && t2.N[0] <= 29 && t2.N[255] >= 254 && t2.N[118] >= 119 && t2.N[118] <= 123 && ndec == 0
        && t1.flags == t2.flags && t2.src == TI, "Matte set on the identity: N 0 -> %d, 118 -> %d, 255 -> %d, never down",
        t2.N[0], t2.N[118], t2.N[255]);
    /* the kernel on greys and on a dark colour: grey = the matte curve of the glossy grey (+-1), less colour in the dark */
    uint32_t g1[128], g2[128];
    for (int i = 0; i < 128; i++) g1[i] = g2[i] = word(0, 2 * i, 0, 2 * i + 1);
    wf_film_words(g1, 128, (int)(uintptr_t)&t1);
    wf_film_words(g2, 128, (int)(uintptr_t)&t2);
    int ge = 0, gc = 0;
    for (int i = 0; i < 128; i++) {
        int e0 = (pp_c16(WF_PAPER_MATTE, 0, WY0(g1[i])) + 8) >> 4, e1 = (pp_c16(WF_PAPER_MATTE, 0, WY1(g1[i])) + 8) >> 4;
        ge += abs(WY0(g2[i]) - e0) > 1 || abs(WY1(g2[i]) - e1) > 1;
        gc += abs(WU(g2[i])) > 1 || abs(WV(g2[i])) > 1;
    }
    chk(ge == 0 && gc == 0, "Matte shot greys: the curve of the Glossy grey (+-1, %d off), still grey (%d coloured)", ge, gc);
    uint32_t d1 = word(36, 40, -36, 40), d2 = d1;
    wf_film_words(&d1, 1, (int)(uintptr_t)&t1);
    wf_film_words(&d2, 1, (int)(uintptr_t)&t2);
    chk(abs(WU(d2)) + abs(WV(d2)) < abs(WU(d1)) + abs(WV(d1)) && WY0(d2) > WY0(d1),
        "Matte: a dark colour comes out lighter and less saturated (Y %d -> %d, |U| + |V| %d -> %d)", WY0(d1), WY0(d2),
        abs(WU(d1)) + abs(WV(d1)), abs(WU(d2)) + abs(WV(d2)));
    /* Highlights / Shadows first: Shadows -5 on Matte cannot pull black under the paper's Dmax */
    memset(&t3, 0, sizeof t3);
    wf_film_build(&t3, TC, WF_FT_V21 | WF_FT_HS(0, -5) | WF_FT_PAPER(WF_PAPER_MATTE));
    chk(t3.N[0] >= 27, "Matte + Shadows -5: black still at the paper's black (%d)", t3.N[0]);
    /* Warm / Cool: the neutral node's colour at the top and in the shadows; a B&W film is toned too */
    for (int p = WF_PAPER_WARM; p <= WF_PAPER_COOL; p++) {
        int sgn = p == WF_PAPER_WARM ? 1 : -1, okt = 1;
        for (int k = 0; k < 2; k++) {
            memset(&t3, 0, sizeof t3);
            wf_film_build(&t3, k ? TM : TI, (k ? WF_FT_MONO : 0) | WF_FT_V21 | WF_FT_PAPER(p));
            int top = (WF_NL - 1) * 289 + 8 * 17 + 8, dk = 6 * 289 + 8 * 17 + 8;
            int Ut = (int)(t3.C[top] & 0xffff) - 128, Vt = (int)(t3.C[top] >> 16) - 128;
            int Ud = (int)(t3.C[dk] & 0xffff) - 128, Vd = (int)(t3.C[dk] >> 16) - 128;
            okt &= sgn * Vt > 0 && sgn * Ut < 0 && sgn * Vd >= 0 && sgn * Ud <= 0 && (Vd || Ud) && abs(Ut) < 8 && abs(Vt) < 8;
            printf("%s %s: neutral node top U %d V %d, level 6 U %d V %d, N 255 -> %d\n", wf_paper_name(p),
                   k ? "B&W" : "colour", Ut, Vt, Ud, Vd, t3.N[255]);
        }
        chk(okt, "%s: the neutral axis toned at the top and in the shadows, colour and B&W films", wf_paper_name(p));
    }
    free(TI); free(TC); free(TM);

    /* 3. through the module: the row, a film shot (+ log, EXIF), the preview */
    std_pack();
    put_cfg("film=%d\noutput=film\ngrain=off\nhalation=off\nmist=off\npreview=on\n", wf_films[fA].id);
    mod_init(1);
    chk(!strcmp(wf_row_name(WF_ROW_PAPER), "Paper") && WF_ROW_PAPER == WF_ROW_PRINT + 1 && WF_NROWS == 23
        && !strcmp(wf_row_text(WF_ROW_PAPER), "Glossy") && wf_paper == 0, "row Paper under Print stock, Glossy without the "
        "key in the file");
    chk(wf_row_avail(WF_ROW_PAPER), "the row is live with a film");
    static const char *const cyc[5] = { "Matte", "Warm", "Cool", "Glossy", "Cool" };
    int cy = 0;
    for (int i = 0; i < 4; i++)
        cy += wf_row_step(WF_ROW_PAPER, 1) && !strcmp(wf_row_text(WF_ROW_PAPER), cyc[i]);
    cy += wf_row_step(WF_ROW_PAPER, -1) && !strcmp(wf_row_text(WF_ROW_PAPER), cyc[4]);
    chk(cy == 5, "steps: Matte, Warm, Cool, Glossy (wraps), back to Cool (%d of 5)", cy);
    wf_row_step(WF_ROW_PAPER, 1);
    wf_row_step(WF_ROW_PAPER, 1);
    struct wf_look lk;
    wf_look_now(&lk);
    chk(wf_paper == WF_PAPER_MATTE && lk.paper == WF_PAPER_MATTE
        && lk.ftflags == (flags_of(fA, 1) | WF_FT_PAPER(WF_PAPER_MATTE)), "the look: paper %d, flags %x", lk.paper,
        (unsigned)lk.ftflags);
    gen_frame(in, FW, FH, 0);
    chk(WAIT(look_table_ready(), 10000), "table published");
    char p[64];
    log_mark();
    h_li_set("job", 1);
    int sb = shot(in, out, FW, FH);
    jpg_name(p, 9); jpeg_write(p, 0, 264, 0);
    h_set_file_number(9);
    h_li_set("job", 0);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1) | WF_FT_PAPER(WF_PAPER_MATTE));
    memcpy(out2, in, NW * 4);
    ref_film(out2, NW, tabs[fA][0], flags_of(fA, 1));
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "film shot Matte: bit-exact vs wf_film_build with WF_FT_PAPER(1) (%d differ)",
        ndiff(out, ref, NW));
    chk(ndiff(out, out2, NW) > NW / 4, "... and not the Glossy picture (%d of %d words differ)", ndiff(out, out2, NW), NW);
    chk(LOGW(" paper Matte:", 5000) && strstr(log_line("shot #"), " paper Matte:"), "shot line: '%s'", log_line("shot #"));
    chk(strstr(log_line("yuv "), " p1 ") != 0, "yuv line: '%s'", log_line("yuv "));
    chk(LOGW("exif IMG_0009.JPG ok", 8000) && strstr(comment(p), " | Paper Matte"), "EXIF: '%s'", comment(p));
    chk(WAIT(wf_pv_state() == WF_PV_READY, 10000), "preview ready (state %d)", wf_pv_state());
    int d = hs_pv_diff(fA, flags_of(fA, 1) | WF_FT_PAPER(WF_PAPER_MATTE));
    chk(d == 0, "preview Matte: bit-exact vs the shot's kernel (%d words differ)", d);
    wf_row_step(WF_ROW_PAPER, 1);
    chk(WAIT(wf_pv_state() == WF_PV_READY, 10000), "preview rebuilt after Warm (state %d)", wf_pv_state());
    d = hs_pv_diff(fA, flags_of(fA, 1) | WF_FT_PAPER(WF_PAPER_WARM));
    chk(d == 0, "preview Warm: the new set (%d words differ)", d);
    log_mark();
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1) | WF_FT_PAPER(WF_PAPER_WARM));
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "film shot Warm: bit-exact (%d differ)", ndiff(out, ref, NW));
    wf_hl = 2;                                   /* with Highlights: both in the key, paper after the bend */
    log_mark();
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_film(ref, NW, tabs[fA][0], flags_of(fA, 1) | WF_FT_HS(2, 0) | WF_FT_PAPER(WF_PAPER_WARM));
    chk(ndiff(out, ref, NW) == 0 && sb == 0 && LOGW(" hl+2 sh0 paper Warm:", 5000), "Warm + Highlights +2: bit-exact "
        "(%d differ), '%s'", ndiff(out, ref, NW), log_line("shot #"));
    wf_hl = 0;
    wf_row_step(WF_ROW_PAPER, 1);
    wf_row_step(WF_ROW_PAPER, 1);
    chk(wf_paper == WF_PAPER_GLOSSY, "back to Glossy");
    log_mark();
    sb = shot(in, out, FW, FH);
    chk(ndiff(out, out2, NW) == 0 && sb == 0, "film shot Glossy: the picture without the feature (%d differ)",
        ndiff(out, out2, NW));
    chk(LOGW("shot #", 5000) && !strstr(log_line("shot #"), "paper") && !strstr(log_line("yuv "), " p1 ")
        && !strstr(log_line("yuv "), " p2 ") && !strstr(log_line("yuv "), " p3 "),
        "Glossy: nothing in the log lines: '%s'", log_line("shot #"));

    /* 4. Log + LUT: the paper after the load and the bend (the .FRT holds neither); a change loads again (cache hit) */
    static struct wf_lltabs ll, l2, lb;
    struct wf_ll_info inf;
    cube_write("ML/DATA/VLOG.CUB", 9);
    wf_curve = WF_CURVE_VLOG;
    wf_flut = 0;
    wf_paper = WF_PAPER_MATTE;
    log_mark();
    wf_out = WF_OUT_LLUT;
    chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
        && strstr(log_line("lut VLOG.CUB"), " paper Matte"), "Log + LUT Matte: '%s'", log_line("lut VLOG.CUB"));
    chk(wf_row_avail(WF_ROW_PAPER), "the row is live with Log + LUT");
    int rc = wf_loglut_load("ML/DATA/VLOG.CUB", WF_CURVE_VLOG, &ll, &inf);
    memcpy(&lb, &ll, sizeof ll);
    wf_ll_paper(&lb, WF_PAPER_MATTE);
    chk(rc == WF_LL_OK && ll_same(&lb, wf_ll_tables()) && !ll_same(&ll, wf_ll_tables()),
        "published tables = wf_loglut_load + wf_ll_paper Matte (rc %d)", rc);
    rc = wf_loglut_load_cached("ML/DATA/VLOG.CUB", "ML/DATA/VLOG.FRT", WF_CURVE_VLOG, &l2, &inf);
    chk(rc == WF_LL_OK && inf.cached && ll_same(&ll, &l2), "ML/DATA/VLOG.FRT holds the tables without the paper (rc %d, "
        "cache %d)", rc, inf.cached);
    memcpy(&l2, &ll, sizeof ll);
    wf_ll_paper(&l2, WF_PAPER_GLOSSY);
    chk(ll_same(&ll, &l2), "wf_ll_paper Glossy: nothing");
    for (int i = 0; i < 128; i++) g1[i] = g2[i] = word(0, 2 * i, 0, 2 * i + 1);
    wf_ll_words_c(g1, 128, (int)(uintptr_t)&ll);
    wf_ll_words_c(g2, 128, (int)(uintptr_t)wf_ll_tables());
    ge = gc = 0;
    for (int i = 0; i < 128; i++) {
        int e0 = (pp_c16(WF_PAPER_MATTE, 0, WY0(g1[i])) + 8) >> 4, e1 = (pp_c16(WF_PAPER_MATTE, 0, WY1(g1[i])) + 8) >> 4;
        ge += abs(WY0(g2[i]) - e0) > 1 || abs(WY1(g2[i]) - e1) > 1;
        gc += abs(WU(g2[i]) - WU(g1[i])) > 1 || abs(WV(g2[i]) - WV(g1[i])) > 1;
    }
    chk(ge == 0 && gc == 0, "Log + LUT Matte greys: the curve of the plain grey (+-1, %d off), colour as before (%d)", ge, gc);
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&lb);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "Log + LUT shot Matte: bit-exact vs wf_ll_words on the papered tables");
    chk(LOGW(" p1", 5000) && strstr(log_line("yuv "), " p1"), "yuv line: '%s'", log_line("yuv "));
    log_mark();
    wf_paper = WF_PAPER_COOL;
    wf_hl = 2;
    chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000)
        && strstr(log_line("lut VLOG.CUB"), " cache ") && strstr(log_line("lut VLOG.CUB"), " hl+2 sh0 paper Cool"),
        "Cool + Highlights +2: loaded again from the .FRT: '%s'", log_line("lut VLOG.CUB"));
    memcpy(&lb, &ll, sizeof ll);
    wf_hs_bend(lb.N, 2, 0);
    wf_ll_paper(&lb, WF_PAPER_COOL);
    chk(ll_same(&lb, wf_ll_tables()), "tables = load, N bent +2, then the Cool paper");
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_ll_words, (int)(uintptr_t)&lb);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "Log + LUT shot Cool + Highlights +2: bit-exact");
    log_mark();
    wf_hl = 0;
    wf_paper = WF_PAPER_GLOSSY;
    chk(LOGW("lut VLOG.CUB", 60000) && WAIT(wf_ll_state(WF_CURVE_VLOG) == WF_LL_OK, 5000) && ll_same(&ll, wf_ll_tables())
        && !strstr(log_line("lut VLOG.CUB"), "paper"), "Glossy: the loaded tables as they are: '%s'", log_line("lut VLOG.CUB"));
    log_mark();
    wf_hl = 1;                                   /* Glossy again: Highlights alone bends N, no load */
    chk(LOGW("lut: highlights +1 shadows 0", 10000) && !log_has("lut VLOG.CUB"), "Glossy + Highlights: the fast path ('%s')",
        log_line("lut"));
    wf_hl = 0;

    /* 5. flat Log and Off: no paper */
    wf_paper = WF_PAPER_MATTE;
    wf_out = WF_OUT_LOG;
    chk(!wf_row_avail(WF_ROW_PAPER), "row greyed for flat Log");
    wf_look_now(&lk);
    chk(lk.paper == 0, "flat Log: the look carries no paper");
    sb = shot(in, out, FW, FH);
    memcpy(ref, in, NW * 4);
    ref_fn(ref, NW, wf_log_words, WF_CURVE_VLOG);
    chk(ndiff(out, ref, NW) == 0 && sb == 0, "flat Log with Matte set: plain wf_log_words");
    wf_out = WF_OUT_OFF;
    chk(!wf_row_avail(WF_ROW_PAPER), "row greyed for Off");

    /* 6. the file: paper= saved, read back, an unknown word = Glossy */
    wf_row_step(WF_ROW_PAPER, 1);                /* Warm: also marks the file to be saved */
    h_clock_add_ms += 2000;
    usleep(400000);
    long n;
    char *f = get_file("ML/SETTINGS/WINDYFLM.CFG", &n);
    chk(f && strstr(f, "\nsh=0\npaper=warm\ngrain="), "saved: paper=warm after sh=");
    free(f);
    put_cfg("paper=cool\n");
    wf_cfg_load();
    chk(wf_paper == WF_PAPER_COOL, "read back: paper %d", wf_paper);
    put_cfg("paper=shiny\n");
    wf_cfg_load();
    chk(wf_paper == WF_PAPER_GLOSSY, "unknown word: Glossy (%d)", wf_paper);
    wf_paper = 0;
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
    else if (!strcmp(mode, "FX")) { if (!strcmp(cs, "bloom")) t_fx_bloom(); else if (!strcmp(cs, "lab")) t_fx_lab(); else t_fx(); }
    else if (!strcmp(mode, "FXAB")) t_fxab();
    else if (!strcmp(mode, "MIST")) { if (!strcmp(cs, "levels")) t_mist_levels(); else t_mist(); }
    else if (!strcmp(mode, "LOOK")) t_look_sweep();
    else if (!strcmp(mode, "EXIF")) t_exif();
    else if (!strcmp(mode, "EXPSIM")) t_expsim();
    else if (!strcmp(mode, "CFG")) t_cfg();
    else if (!strcmp(mode, "PACK")) t_pack(cs, argc > 3 ? argv[3] : 0);
    else if (!strcmp(mode, "LIGHT") && !strcmp(cs, "awb")) t_light_awb();
    else if (!strcmp(mode, "LIGHT")) t_light();
    else if (!strcmp(mode, "ISO")) t_iso();
    else if (!strcmp(mode, "QKEYS")) t_qkeys();
    else if (!strcmp(mode, "OSVF")) t_osvf();
    else if (!strcmp(mode, "PLATE")) t_plate();
    /* test.sh passes "PRINT:chain" as MODE PRINT, CASE chain (until 11-15 that ran t_print a second time) */
    else if (!strcmp(mode, "PRINT") && !strcmp(cs, "chain")) t_print_chain();
    else if (!strcmp(mode, "PRINT")) t_print();
    else if (!strcmp(mode, "PRINT:chain")) t_print_chain();
    else if (!strcmp(mode, "STAGE")) t_stage(cs);
    else if (!strcmp(mode, "LOG")) t_log(cs);
#ifndef WF_BUILD_5D2
    else if (!strcmp(mode, "RAWB") && !strcmp(cs, "chain")) t_rawb_chain();
    else if (!strcmp(mode, "RAWB") && !strcmp(cs, "knobs")) t_rawb_knobs();
#endif
    else if (!strcmp(mode, "RAWB")) t_rawb();
    else if (!strcmp(mode, "HS")) t_hs();
    else if (!strcmp(mode, "PAPER")) t_paper();
    else if (!strcmp(mode, "ROLL")) t_roll(cs);
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
    char open_names[256];
    int open_n = h_fio_open_now(open_names, sizeof open_names);
    chk(open_n == 0 && h_fio_rm_open == 0, "every card file closed (%d open: %s), no remove of an open file (%d)", open_n,
        open_names, h_fio_rm_open);
    if (nfail)
        log_tail(25);
    printf("RESULT %s %s %s: %d pass, %d fail\n", CAM_NAME, mode, cs, npass, nfail);
    fflush(stdout);
    _exit(nfail ? 1 : 0);                        /* the module's worker stays blocked on its semaphore: no join */
}
