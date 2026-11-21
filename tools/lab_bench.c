/* lab_bench.c - memsim cost of the Lab effects per frame (WindyStrife, 11-21). Run by tools/lab_bench.sh in WSL, linked
 * with the module's own yl_lab.o (build/<cam>/obj: module flags, soft float). The frame (a UYVY file, as
 * tools/lab_check.py uyvy writes it) sits at 0x48000000, the region tools/lab_bench.sh gives memsim as the uncached
 * shoot pool, as Canon's YUV buffer is; every pass goes through a cached scratch of 288 words with wf_shot.c's LDM / STM
 * copy. memsim counts phases by the order of the prof_mark calls (the 9th and later all go to phase 9), so there are
 * nine, each over the whole frame (with HALVES: the TOP rows, then the BOT rows; the costs are those of wf_shot.c's
 * order, the frame is not):
 *   1 yl_init + yl_grain_init (wf_task: once per setting / gauge)
 *   2 a pass of copies only: the colour pass's own reads and writes, which the shot has anyway
 *   3 that pass with the gather after the colour (the gather's own cost = 3 - 2)
 *   4 yl_build                 5 the gather2 pass (both glows; reads only)            6 yl_build2
 *   7 the apply pass: a pass of its own (reads, apply, writes)
 *   8 a pass of copies + the grain (its own cost = 8 - 2: it rides on the apply pass, or on the colour pass alone)
 *   9 the end (the frame hash)
 * Usage: lab_bench FRAME.uyvy W H hal|bloom|both GAUGE [HALVES]   (HALVES 1: TOP rows then BOT rows, as a 5D2 shot) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "yl_lab.h"

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

static void copyw(uint32_t *d, const uint32_t *s, int n)
{
    int m8 = n & ~7;
    if (m8)
        wf_copy8(d, s, m8);
    for (int i = m8; i < n; i++)
        d[i] = s[i];
}

#define FB_ADDR 0x48000000u
#define CHUNK 288
static uint32_t scr[CHUNK];

enum { P_COPY, P_GATHER, P_GATHER2, P_APPLY, P_GRAIN };
static void pass(uint32_t *f, int w, int y0, int y1, int what, yl_ctx *g, yl_grain *gr)
{
    for (int y = y0; y < y1; y++)
        for (int x0 = 0; x0 < w; x0 += 2 * CHUNK) {
            int m = (w - x0) / 2 < CHUNK ? (w - x0) / 2 : CHUNK;
            uint32_t *sp = f + ((size_t)y * w + x0) / 2;
            copyw(scr, sp, m);
            if (what == P_GATHER) yl_gather_chunk(g, scr, 2 * m, x0, y);
            else if (what == P_GATHER2) yl_gather2_chunk(g, scr, 2 * m, x0, y);
            else if (what == P_APPLY) yl_apply_chunk(g, scr, 2 * m, x0, y);
            else if (what == P_GRAIN) yl_grain_row(gr, scr, 2 * m, x0, y);
            if (what != P_GATHER2)
                copyw(sp, scr, m);
        }
}

static yl_ctx ctx;
static yl_grain grn;

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "lab_bench FRAME.uyvy W H hal|bloom|both GAUGE [HALVES]\n");
        return 2;
    }
    int W = atoi(argv[2]), H = atoi(argv[3]), ga = atoi(argv[5]), halves = argc > 6 && atoi(argv[6]);
    int hal = strcmp(argv[4], "bloom") != 0, blm = strcmp(argv[4], "hal") != 0;
    size_t nb = (size_t)W * H * 2;
    uint32_t *f = mmap((void *)FB_ADDR, nb + 4096, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (f != (uint32_t *)FB_ADDR) { perror("mmap"); return 2; }
    FILE *in = fopen(argv[1], "rb");
    if (!in || fread(f, 1, nb, in) != nb) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    fclose(in);
    yl_params p = { hal, blm, ga };
    uint32_t mb = yl_mem_bytes(W, H, hal, blm);
    void *mem = malloc(mb), *tile = malloc(YL_GBYTES), *gs = malloc(YL_GSCRATCH);
    prof_mark(1);
    yl_init(&ctx, &p, W, H, mem);
    yl_grain_init(&grn, ga, H, 0, tile, gs);
    yl_grain_shot(&grn, 1);
    int nh = halves ? 2 : 1, s = H / 2;
    for (int k = 0; k < 7; k++) {                /* phases 2..8 */
        prof_mark(2 + k);
        for (int hh = 0; hh < nh; hh++) {
            int ya = halves ? hh * s : 0, yb = halves ? (hh ? H : s) : H, md = !halves ? YL_WHOLE : hh ? YL_BOTTOM : YL_TOP;
            if (k == 0) pass(f, W, ya, yb, P_COPY, &ctx, &grn);
            else if (k == 1) { if (!hh) yl_begin_frame(&ctx); pass(f, W, ya, yb, P_GATHER, &ctx, &grn); }
            else if (k == 2) yl_build(&ctx, ya, yb, md);
            else if (k == 3) { if (ctx.two) pass(f, W, ya, yb, P_GATHER2, &ctx, &grn); }
            else if (k == 4) { if (ctx.two) yl_build2(&ctx, ya, yb, md); }
            else if (k == 5) pass(f, W, ya, yb, P_APPLY, &ctx, &grn);
            else pass(f, W, ya, yb, P_GRAIN, &ctx, &grn);
        }
    }
    prof_mark(9);
    uint32_t h = 2166136261u;
    const uint8_t *b = (const uint8_t *)f;
    for (size_t i = 0; i < nb; i++) h = (h ^ b[i]) * 16777619u;
    printf("lab_bench %s %dx%d gauge %d halves %d: stage %u KB, frame hash %08x\n", argv[4], W, H, ga, halves, mb >> 10, h);
    return 0;
}
