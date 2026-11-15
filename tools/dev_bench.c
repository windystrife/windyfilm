/* dev_bench.c - memsim timing of the RAW develop "B" (src/wf_dev.c, WindyStrife) on the EOS M frame.
 *
 *   dev_bench DIR [CUBE]     DIR/scene.r14 + DIR/scene.uyvy from tests/host/wf_dev_scene.c (5184 x 3456 in a 5280 x 3528
 *                            raw, pitch 9240); CUBE default DIR/FILM33.CUB (V-Log)
 *   dev_bench DIR srgb       the shipped film case (src/wf_rawb.c): SRGB tables, the shot's levels (wf_dev_levels, in
 *                            phase 2) and the film shoulder (roll 1)
 *   dev_bench DIR CUBE|- HUE [eosm|5d2 [CURVE]]   a chain film's case (wf_rawb.c): the cube (- = DIR/FILM33.CUB) on
 *                            CURVE (WF_LL_*, default V-Log; a chain film's log table: 1 = S-Log3), no auto exposure, the
 *                            shot's levels, the film shoulder with its colour HUE = a / i / d (j.hue, WF_DEV_HUE_*);
 *                            5d2 = the 5D2's develop (matrix, wf_dev_build WF_DEV_5D2; link the 5D2 object) on the same
 *                            scene. The shoulder's tables get their own phase through j.ms: phases 2 levels + prepare up
 *                            to the shoulder, 3 the shoulder tables (A: per-channel enc tables; I / D: the per-shot shift
 *                            table T[im] with its weights), 4 the rest of prepare, 5 the rows.
 * DIR/crop.txt "X0 Y0" (optional): a real EOS M pair as tools/cr2_dec.c + jpg_uyvy.py write it (raw.r14 as scene.r14,
 * jpeg.uyvy as scene.uyvy): the develop window's corner in the raw (84 64 for the JPEG L; default 72 52, the scene's)
 * and the CR2's image data to the raw's edge (active area 72..5280 x 52..3528).
 * The raw and the frame sit in one region at 0x48000000 that tools/dev_bench.sh gives memsim as the NCNB shoot pool
 * (fb=, fbn=): the raw is read uncached as on the camera, the frame is Canon's YUV buffer (the WB fit reads it, then the
 * rows overwrite it). Tables and scratch are ordinary (cached) heap. Phases (prof_mark): 1 wf_dev_build (load time,
 * wf_task), 2 wf_dev_prepare (per shot: pass A, fit, map), 3 wf_dev_rows (the frame). Prints the develop checksum: it
 * equals wf_dev_test full's on the same scene (the same objects). Linked with the module objects (module flags) and
 * tests/host/wf_dev_shim.c's FIO / memory calls; wf_copyw here is wf_shot.c's LDM / STM version. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "wf_dev.h"

void __attribute__((noinline)) prof_mark(int k) { __asm__ __volatile__("" :: "r"(k) : "memory"); }

/* wf_shot.c: 8-word LDM / STM bursts (no LDRD / STRD) */
extern void wf_copy8(uint32_t *dst, const uint32_t *src, int words);
__asm__(".text\n .align 2\n .global wf_copy8\n .type wf_copy8, %function\n"
        "wf_copy8:\n"
        " stmfd sp!, {r4-r10}\n"
        "1: ldmia r1!, {r3-r10}\n"
        " stmia r0!, {r3-r10}\n"
        " subs r2, r2, #8\n"
        " bgt 1b\n"
        " ldmfd sp!, {r4-r10}\n"
        " bx lr\n"
        " .size wf_copy8, .-wf_copy8\n");

void wf_copyw(uint32_t *d, const uint32_t *s, int n)
{
    int m8 = n & ~7;
    if (m8)
        wf_copy8(d, s, m8);
    for (int i = m8; i < n; i++)
        d[i] = s[i];
}

#define FB_ADDR 0x48000000u

static int bench_ph = 2;                         /* j.ms: each call starts the next memsim phase (the shoulder tables) */
static int bench_ms(void) { prof_mark(++bench_ph); return 0; }

static int rd(const char *dir, const char *name, void *p, size_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    size_t k = fread(p, 1, n, f);
    fclose(f);
    return k == n ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s DIR [CUBE]\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1];
    char cube[512];
    if (argc > 2 && strcmp(argv[2], "-")) snprintf(cube, sizeof cube, "%s", argv[2]);
    else snprintf(cube, sizeof cube, "%s/FILM33.CUB", dir);
    const int w = 5184, h = 3456, rw = 5280, rh = 3528, pitch = rw / 8 * 14;
    const size_t raw_n = (size_t)rh * pitch, frame_n = (size_t)w * h * 2;
    uint8_t *fb = mmap((void *)FB_ADDR, raw_n + frame_n + 4096, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (fb != (uint8_t *)FB_ADDR) { perror("mmap"); return 2; }
    uint8_t *raw = fb;
    uint32_t *frame = (uint32_t *)(fb + raw_n);
    if (rd(dir, "scene.r14", raw, raw_n) || rd(dir, "scene.uyvy", frame, frame_n))
        return 2;
    struct wf_dev_tabs *t = malloc(sizeof *t);
    void *scr = malloc(wf_dev_scratch_bytes(w, h));
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = 72 + w; j.ay1 = 52 + h;
    j.x0 = 72; j.y0 = 52; j.w = w; j.h = h;
    char cp[512];
    snprintf(cp, sizeof cp, "%s/crop.txt", dir);
    FILE *cf = fopen(cp, "r");
    if (cf) {                                    /* a real CR2: image data to the raw's edge (72..5280 x 52..3528) */
        if (fscanf(cf, "%d %d", &j.x0, &j.y0) != 2)
            j.x0 = 72, j.y0 = 52;
        j.ax1 = rw; j.ay1 = rh;
        fclose(cf);
    }
    j.black = 2048; j.white = 15200; j.cfa = 0;
    j.canon = frame; j.canon_y0 = 0; j.canon_h = h;
    struct wf_ll_info inf;
    struct wf_dev_stat st;

    int srgb = argc > 2 && !strcmp(argv[2], "srgb"), lb = 0, lw = 0;
    int hue = argc > 3 ? (argv[3][0] == 'i' ? WF_DEV_HUE_I : argv[3][0] == 'd' ? WF_DEV_HUE_D : WF_DEV_HUE_A) : -1;
    int cam = argc > 4 && !strcmp(argv[4], "5d2") ? WF_DEV_5D2 : WF_DEV_EOSM;
    int curve = argc > 5 ? atoi(argv[5]) : WF_LL_VLOG;
    prof_mark(1);
    int rc = srgb ? wf_dev_build(t, WF_DEV_SRGB, 0, cam, 0, 0, 0, &inf)
                  : wf_dev_build(t, WF_DEV_CUBE, curve, cam, cube, 0, 0, &inf);
    if (hue == WF_DEV_HUE_I && !rc)              /* as wf_rawb_logpk_build: i's colour trim on the built table */
        wf_dev_trim(t, WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB);
    prof_mark(2);
    if ((srgb || hue >= 0) && !rc) {             /* as wf_rawb.c: levels, then the shoulder */
        wf_dev_levels(scr, &j, 16, 64, &lb, &lw);
        if (lb) j.black = lb;
        if (lw) j.white = lw;
        j.roll = 1;
        j.hue = hue > 0 ? hue : WF_DEV_HUE_A;
        if (hue >= 0)
            j.ms = bench_ms;
    }
    int rp = rc ? -1 : wf_dev_prepare(scr, t, &j, &st);
    prof_mark(++bench_ph);
    if (!rp)
        wf_dev_rows(scr, 0, h, frame, w * 2);
    prof_mark(++bench_ph);

    if (rc || rp) {
        fprintf(stderr, "dev_bench: build %d, prepare %d\n", rc, rp);
        return 3;
    }
    uint32_t ck = 0;
    for (size_t i = 0; i < (size_t)w * h / 2; i++) ck = (ck << 5 | ck >> 27) ^ frame[i];
    printf("dev_bench: %dx%d, cube n %d, scratch %u B, tables %u B, wb src %d (n %d), clipped quads %d, highlight px %u, "
           "develop checksum %08x\n", w, h, inf.n, (unsigned)wf_dev_scratch_bytes(w, h), (unsigned)sizeof *t, st.wb_src,
           st.fit_n, st.clip_quads, st.hl_px, ck);
    if (srgb || hue >= 0)
        printf("dev_bench: %s curve %d, window %d %d, levels black %d white %d (0 = none), roll %d, shoulder %d (k %d c %d), "
               "%s\n", srgb ? "srgb" : "cube", srgb ? 0 : curve, j.x0, j.y0, lb, lw, st.roll, st.hue, st.ro_k, st.ro_c,
               cam == WF_DEV_5D2 ? "5D2 develop" : "EOS M develop");
    return 0;
}
