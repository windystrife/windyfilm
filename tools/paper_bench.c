/* paper_bench.c - memsim cost of the Paper row's table passes (WindyStrife, 11-16). Run by tools/paper_bench.sh in WSL.
 * Linked with the EOS M module objects (build/eosm/obj: module flags) and tests/host/wf_dev_shim.c. Each pass runs once
 * per look change on wf_task (never per pixel: the shot kernels read the same tables as before). Phases (prof_mark):
 *   1 wf_film_build Glossy (the 8-bit film set as before)    2 the same with Matte    3 with Warm
 *   4 wf_ll_paper Matte on a Log + LUT set                    5 wf_dev_paper Matte on a 33^3 develop table (B / Darkroom)
 * Inputs are synthetic (a shifted identity film, a neutral Log + LUT set, an sRGB-like develop table): the passes have no
 * data-dependent loops, so their cost does not depend on the look. Prints a checksum of each result. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wf_film.h"
#include "wf_loglut.h"
#include "wf_dev.h"

void __attribute__((noinline)) prof_mark(int k) { __asm__ __volatile__("" :: "r"(k) : "memory"); }

static int cl(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint32_t fnv(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

static uint32_t T[4913];
static struct wf_ftabs ft[3];
static struct wf_lltabs ll;
static struct wf_dev_tabs dv;

int main(void)
{
    for (int yi = 0; yi < 17; yi++)              /* identity + (12, 6, -6), as the host tests' colour film */
        for (int ui = 0; ui < 17; ui++)
            for (int vi = 0; vi < 17; vi++) {
                int y = cl(16 * yi, 0, 255), u = cl(16 * ui, 0, 255) - 128, v = cl(16 * vi, 0, 255) - 128;
                T[yi * 289 + ui * 17 + vi] = (uint32_t)(uint8_t)cl(u + 6, -128, 127) | (uint32_t)cl(y + 12, 0, 255) << 8
                                           | (uint32_t)(uint8_t)cl(v - 6, -128, 127) << 16;
            }
    for (int y = 0; y < 256; y++)                /* a neutral Log + LUT set: N the identity, nodes at their own luma */
        ll.N[y] = (uint8_t)y;
    for (int j = 0; j < 1280; j++)
        ll.K[j] = (uint8_t)cl(j - 512, 0, 255);
    for (int l = 0; l < WF_LL_NL; l++) {
        ll.L[l] = 8 * ll.N[l * 8 > 255 ? 255 : l * 8] - 4 - 8 * WF_LL_KOFF;
        for (int k = 0; k < WF_LL_LSTEP; k++) {
            int u = (k / WF_LL_NC) * 8 - 128, v = (k % WF_LL_NC) * 8 - 128;
            ll.Y[l * WF_LL_LSTEP + k] = (uint8_t)cl(l * 8, 0, 255);
            ll.C[l * WF_LL_LSTEP + k] = (uint32_t)(2 * cl(u, -128, 127) + 257) | (uint32_t)(2 * cl(v, -128, 127) + 257) << 16;
        }
    }
    for (int b = 0; b < WF_DEV_G; b++)           /* the develop table: node Y9 / Cb9 / Cr9 from its grid position */
        for (int g = 0; g < WF_DEV_G; g++)
            for (int r = 0; r < WF_DEV_G; r++) {
                int R = r * 255 / (WF_DEV_G - 1), G = g * 255 / (WF_DEV_G - 1), B = b * 255 / (WF_DEV_G - 1);
                int Y = (19595 * R + 38470 * G + 7471 * B + 32768) >> 16;
                int u = (-11059 * R - 21709 * G + 32768 * B + 32768) >> 16, v = (32768 * R - 27439 * G - 5329 * B + 32768) >> 16;
                uint32_t *nd = dv.node + 2 * (r + WF_DEV_G * g + WF_DEV_BSTR * b);
                nd[0] = (uint32_t)(cl(2 * Y, 0, 510) + 1) | (uint32_t)(cl(2 * (u + 128), 0, 510) + 1) << 16;
                nd[1] = (uint32_t)(cl(2 * (v + 128), 0, 510) + 1);
            }
    prof_mark(1);
    wf_film_build(&ft[0], T, WF_FT_V21);
    prof_mark(2);
    wf_film_build(&ft[1], T, WF_FT_V21 | WF_FT_PAPER(WF_PAPER_MATTE));
    prof_mark(3);
    wf_film_build(&ft[2], T, WF_FT_V21 | WF_FT_PAPER(WF_PAPER_WARM));
    prof_mark(4);
    wf_ll_paper(&ll, WF_PAPER_MATTE);
    prof_mark(5);
    wf_dev_paper(&dv, WF_PAPER_MATTE);
    prof_mark(6);
    size_t fe = offsetof(struct wf_ftabs, src);
    printf("paper_bench: film %08x %08x %08x, ll %08x, dev %08x; matte film N[0] %d, ll N[0] %d\n", fnv(&ft[0], fe),
           fnv(&ft[1], fe), fnv(&ft[2], fe), fnv(&ll, sizeof ll), fnv(dv.node, sizeof dv.node), ft[1].N[0], ll.N[0]);
    return 0;
}
