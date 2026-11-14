/* dev_bench.c - memsim timing of the RAW develop "B" (src/wf_dev.c, WindyStrife) on the EOS M frame.
 *
 *   dev_bench DIR [CUBE]     DIR/scene.r14 + DIR/scene.uyvy from tests/host/wf_dev_scene.c (5184 x 3456 in a 5280 x 3528
 *                            raw, pitch 9240); CUBE default DIR/FILM33.CUB (V-Log)
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
    if (argc > 2) snprintf(cube, sizeof cube, "%s", argv[2]);
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
    j.black = 2048; j.white = 15200; j.cfa = 0;
    j.canon = frame; j.canon_y0 = 0; j.canon_h = h;
    struct wf_ll_info inf;
    struct wf_dev_stat st;

    prof_mark(1);
    int rc = wf_dev_build(t, WF_DEV_CUBE, WF_LL_VLOG, WF_DEV_EOSM, cube, 0, 0, &inf);
    prof_mark(2);
    int rp = rc ? -1 : wf_dev_prepare(scr, t, &j, &st);
    prof_mark(3);
    if (!rp)
        wf_dev_rows(scr, 0, h, frame, w * 2);
    prof_mark(4);

    if (rc || rp) {
        fprintf(stderr, "dev_bench: build %d, prepare %d\n", rc, rp);
        return 3;
    }
    uint32_t ck = 0;
    for (size_t i = 0; i < (size_t)w * h / 2; i++) ck = (ck << 5 | ck >> 27) ^ frame[i];
    printf("dev_bench: %dx%d, cube n %d, scratch %u B, tables %u B, wb src %d (n %d), clipped quads %d, highlight px %u, "
           "develop checksum %08x\n", w, h, inf.n, (unsigned)wf_dev_scratch_bytes(w, h), (unsigned)sizeof *t, st.wb_src,
           st.fit_n, st.clip_quads, st.hl_px, ck);
    return 0;
}
