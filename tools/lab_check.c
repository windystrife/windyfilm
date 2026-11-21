/* lab_check.c - host harness of the Lab effects (src/yl_lab.c) on a UYVY frame (WindyStrife's own tool).
 *
 *   lab_check IN.uyvy W H WHAT GAUGE OUT [HALVES]
 *     WHAT  hal | bloom | both | grain | all (all = bloom + halation, then grain, as the shot runs them)
 *     GAUGE 0 8 mm, 1 16 mm, 2 35 mm, 3 65 mm (film-lab preset order)
 *     writes OUT_in.ppm (the frame as RGB, clamped full-range BT.601: what the reference CLI gets) and OUT_cam.ppm (the
 *     camera's result as RGB), and prints "us gather build apply grain" (host time) and the stage's memory.
 *     HALVES 1: as two halves (TOP rows 0 .. H/2 - 1, then BOT), like a Diana / 5D2 frame.
 * Chunks of WF_CHUNK words per row segment, as wf_shot.c runs them. tools/lab_check.sh runs it against the reference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "yl_lab.h"

#define CHUNK 288

static long us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000L + t.tv_nsec / 1000;
}

static int c8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void ppm(const char *path, const uint32_t *f, int w, int h)
{
    FILE *o = fopen(path, "wb");
    unsigned char *row = malloc(3 * w);
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            uint32_t q = f[(y * w + x) / 2];
            int U = (int8_t)q, Y0 = (q >> 8) & 255, V = (int8_t)(q >> 16), Y1 = q >> 24;
            int rv = (359 * V) >> 8, gu = (88 * U + 183 * V) >> 8, bu = (454 * U) >> 8;
            unsigned char *p = row + 3 * x;
            p[0] = c8(Y0 + rv); p[1] = c8(Y0 - gu); p[2] = c8(Y0 + bu);
            p[3] = c8(Y1 + rv); p[4] = c8(Y1 - gu); p[5] = c8(Y1 + bu);
        }
        fwrite(row, 1, 3 * w, o);
    }
    free(row);
    fclose(o);
}

typedef void (*rowfn)(void *ctx, uint32_t *w, int npix, int x0, int y);
static void gather_fn(void *c, uint32_t *w, int n, int x0, int y) { yl_gather_chunk(c, w, n, x0, y); }
static void gather2_fn(void *c, uint32_t *w, int n, int x0, int y) { yl_gather2_chunk(c, w, n, x0, y); }
static void apply_fn(void *c, uint32_t *w, int n, int x0, int y) { yl_apply_chunk(c, w, n, x0, y); }
static void grain_fn(void *c, uint32_t *w, int n, int x0, int y) { yl_grain_row(c, w, n, x0, y); }

static long pass(uint32_t *f, int w, int y0, int y1, rowfn fn, void *ctx)
{
    static uint32_t scr[CHUNK];
    long t = us();
    for (int y = y0; y < y1; y++)
        for (int x0 = 0; x0 < w; x0 += 2 * CHUNK) {
            int m = (w - x0) / 2 < CHUNK ? (w - x0) / 2 : CHUNK;
            uint32_t *sp = f + (y * w + x0) / 2;
            memcpy(scr, sp, 4 * m);
            fn(ctx, scr, 2 * m, x0, y);
            memcpy(sp, scr, 4 * m);
        }
    return us() - t;
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "lab_check IN.uyvy W H hal|bloom|both|grain|all GAUGE OUT [HALVES]\n");
        return 2;
    }
    int W = atoi(argv[2]), H = atoi(argv[3]), ga = atoi(argv[5]), halves = argc > 7 && atoi(argv[7]);
    const char *what = argv[4], *out = argv[6];
    int hal = !strcmp(what, "hal") || !strcmp(what, "both") || !strcmp(what, "all");
    int blm = !strcmp(what, "bloom") || !strcmp(what, "both") || !strcmp(what, "all");
    int grn = !strcmp(what, "grain") || !strcmp(what, "all");
    size_t nw = (size_t)W * H / 2;
    uint32_t *f = malloc(4 * nw);
    FILE *in = fopen(argv[1], "rb");
    char pth[512];
    if (!in || fread(f, 4, nw, in) != nw) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    fclose(in);
    snprintf(pth, sizeof pth, "%s_in.ppm", out);
    ppm(pth, f, W, H);
    long tg = 0, tb = 0, ta = 0, tr = 0, ti = 0;
    if (hal || blm) {
        yl_params p = { hal, blm, ga };
        uint32_t nb = yl_mem_bytes(W, H, hal, blm);
        yl_ctx *g = malloc(sizeof(yl_ctx));
        void *mem = malloc(nb);
        long t = us();
        yl_init(g, &p, W, H, mem);
        ti = us() - t;
        printf("stage %u KB (halation %d bloom %d gauge %d) init %ld us\n", nb >> 10, hal, blm, ga, ti);
        if (!halves) {
            tg += pass(f, W, 0, H, gather_fn, g);
            t = us(); yl_build(g, 0, H, YL_WHOLE); tb += us() - t;
            if (g->two) {
                tg += pass(f, W, 0, H, gather2_fn, g);
                t = us(); yl_build2(g, 0, H, YL_WHOLE); tb += us() - t;
            }
            ta += pass(f, W, 0, H, apply_fn, g);
        } else {
            int s = H / 2;
            tg += pass(f, W, 0, s, gather_fn, g);
            t = us(); yl_build(g, 0, s, YL_TOP); tb += us() - t;
            if (g->two) {
                tg += pass(f, W, 0, s, gather2_fn, g);
                t = us(); yl_build2(g, 0, s, YL_TOP); tb += us() - t;
            }
            ta += pass(f, W, 0, s, apply_fn, g);
            tg += pass(f, W, s, H, gather_fn, g);
            t = us(); yl_build(g, s, H, YL_BOTTOM); tb += us() - t;
            if (g->two) {
                tg += pass(f, W, s, H, gather2_fn, g);
                t = us(); yl_build2(g, s, H, YL_BOTTOM); tb += us() - t;
            }
            ta += pass(f, W, s, H, apply_fn, g);
        }
        free(mem);
        free(g);
    }
    if (grn) {
        yl_grain gr;
        void *tile = malloc(YL_GBYTES), *scr = malloc(YL_GSCRATCH);
        long t = us();
        yl_grain_init(&gr, ga, H, 0, tile, scr);
        printf("grain tile init %ld us, clump %d px, max |noise| %d (Q14)\n", us() - t, gr.clump, gr.maxabs);
        if (getenv("LAB_GX")) {             /* test: the zone weights x LAB_GX (the reference takes shadows / midtones / highlights over 100) */
            int gx = atoi(getenv("LAB_GX"));
            for (int v = 0; v < 256; v++) gr.kz[v] = (int16_t)(gr.kz[v] * gx > 32767 ? 32767 : gr.kz[v] * gx);
        }
        yl_grain_shot(&gr, 1);
        tr += pass(f, W, 0, H, grain_fn, &gr);
        free(tile);
        free(scr);
    }
    printf("us gather %ld build %ld apply %ld grain %ld\n", tg, tb, ta, tr);
    snprintf(pth, sizeof pth, "%s_cam.ppm", out);
    ppm(pth, f, W, H);
    free(f);
    return 0;
}
