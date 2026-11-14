/* wf_jpe_test.c - host test of the Darkroom JPEG encoder src/wf_jpe.c (WindyStrife). Linked with the module object
 * (module flags) and run under qemu-arm by tools/dr_jpe_check.sh, which decodes the outputs with standard decoders
 * (djpeg, PIL) and measures PSNR.
 *
 *   wf_jpe_test enc IN.uyvy W H Q BAND OBUF OUT.jpg [APP1]   encode a UYVY file in bands of BAND rows through an
 *                                                           OBUF-byte output buffer; APP1 = a file with a whole APP1
 *                                                           segment to pass through. Prints bytes and an FNV of them.
 *   wf_jpe_test synth DIR                                    synthetic frames DIR/<name>.uyvy + list.txt (name w h)
 *   wf_jpe_test same IN.uyvy W H Q                           the bytes do not depend on BAND / OBUF: 8 rows, 16, 40,
 *                                                           the whole frame x obuf 64, 4096, 1 MB
 *   wf_jpe_test fail                                         argument / row / state errors and a failing out()
 *   wf_jpe_test bench IN.uyvy W H Q                          memsim: phase 1 = load (not timed), 2 = the encode */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "wf_jpe.h"

void __attribute__((noinline)) prof_mark(int k) { __asm__ __volatile__("" :: "r"(k) : "memory"); }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); \
                           printf("\n"); } } while (0)

/* ------------------------------------------------------------------ output sinks */
struct sink { uint8_t *p; uint32_t n, cap; uint32_t fnv; int fail_after; int calls; };
static int sink_out(void *ctx, const uint8_t *p, uint32_t n)
{
    struct sink *s = ctx;
    s->calls++;
    if (s->fail_after >= 0 && s->n + n > (uint32_t)s->fail_after) return -5;
    for (uint32_t i = 0; i < n; i++) s->fnv = (s->fnv ^ p[i]) * 16777619u;
    if (s->p)
    {
        if (s->n + n > s->cap) return -6;
        memcpy(s->p + s->n, p, n);
    }
    s->n += n;
    return 0;
}

/* the bench: on the camera out() hands the buffer to FIO_WriteFile (DMA), the CPU never reads it back */
static int bench_out(void *ctx, const uint8_t *p, uint32_t n) { (void)p; ((struct sink *)ctx)->n += n; return 0; }

static void *rdfile(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    fseek(f, 0, SEEK_END);
    long k = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = malloc(k ? k : 1);
    if (fread(p, 1, k, f) != (size_t)k) { fclose(f); free(p); return 0; }
    fclose(f);
    *n = k;
    return p;
}

static int encode(const uint32_t *fr, int w, int h, int q, int band, uint32_t obn, const uint8_t *app1, int app1n,
                  struct sink *s)
{
    struct wf_jpe *e = malloc(wf_jpe_bytes());
    uint8_t *ob = malloc(obn);
    int rc = wf_jpe_begin(e, w, h, q, app1, app1n, ob, obn, sink_out, s);
    for (int y = 0; !rc && y < h; y += band)
        rc = wf_jpe_rows(e, fr + (size_t)y * (w / 2), w * 2, y + band <= h ? band : h - y);
    if (!rc) rc = wf_jpe_end(e);
    if (!rc && wf_jpe_written(e) != s->n) rc = -100;
    free(ob);
    free(e);
    return rc;
}

/* ------------------------------------------------------------------ synthetic frames */
static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static uint32_t word(int y0, int y1, int u, int v)
{
    return (uint32_t)(uint8_t)u | (uint32_t)y0 << 8 | (uint32_t)(uint8_t)v << 16 | (uint32_t)y1 << 24;
}
static int wr(const char *dir, const char *name, const uint32_t *fr, int w, int h, FILE *list)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s.uyvy", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    fwrite(fr, 4, (size_t)w * h / 2, f);
    fclose(f);
    fprintf(list, "%s %d %d\n", name, w, h);
    return 0;
}
static int synth(const char *dir)
{
    char p[512];
    snprintf(p, sizeof p, "%s/list.txt", dir);
    FILE *list = fopen(p, "w");
    if (!list) return 2;
    static const int sz[][2] = { { 64, 32 }, { 18, 10 }, { 2, 1 }, { 30, 17 }, { 640, 480 }, { 5184, 3456 } };
    for (unsigned k = 0; k < sizeof sz / sizeof *sz; k++)
    {
        int w = sz[k][0], h = sz[k][1];
        uint32_t *fr = malloc((size_t)w * h * 2 + 4);
        const char *names[] = { "ramp", "flat", "check", "noise" };
        for (int m = 0; m < 4; m++)
        {
            if (w == 5184 && m != 0 && m != 3) continue;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w / 2; x++)
                {
                    uint32_t *o = &fr[(size_t)y * (w / 2) + x];
                    int px = 2 * x;
                    switch (m)
                    {
                    case 0: *o = word(px * 255 / (w > 1 ? w - 1 : 1), (px + 1) * 255 / (w > 1 ? w - 1 : 1) > 255 ? 255 :
                                      (px + 1) * 255 / (w > 1 ? w - 1 : 1), (y * 200 / (h > 1 ? h : 1)) - 100,
                                      100 - (x * 400 / (w > 1 ? w : 1)) % 200);
                            break;
                    case 1: *o = (y / 8 + x / 8) & 1 ? word(200, 200, -40, 60) : word(35, 35, 50, -20); break;
                    case 2: *o = ((x ^ y) & 1) ? word(255, 0, 127, -128) : word(0, 255, -128, 127); break;
                    case 3: { uint32_t r = rnd(), s2 = rnd(); *o = word(r & 255, (r >> 8) & 255, (int8_t)(s2 & 255),
                                                                         (int8_t)((s2 >> 8) & 255)); } break;
                    }
                }
            char nm[64];
            snprintf(nm, sizeof nm, "%s_%dx%d", names[m], w, h);
            wr(dir, nm, fr, w, h, list);
        }
        free(fr);
    }
    fclose(list);
    return 0;
}

/* ------------------------------------------------------------------ errors */
static int fail_tests(void)
{
    struct wf_jpe *e = malloc(wf_jpe_bytes());
    uint8_t ob[1024];
    struct sink s = { 0, 0, 0, 2166136261u, -1, 0 };
    uint32_t *fr = calloc(64 * 40, 2);
    for (int i = 0; i < 64 * 40 / 2; i++) fr[i] = word(i & 255, (i * 7) & 255, (int8_t)i, (int8_t)(i * 3));
    CHECK(wf_jpe_begin(e, 63, 8, 90, 0, 0, ob, sizeof ob, sink_out, &s) == WF_JPE_E_ARG, "odd width");
    CHECK(wf_jpe_rows(e, fr, 128, 8) == WF_JPE_E_ARG, "rows after a failed begin");
    CHECK(wf_jpe_begin(e, 64, 0, 90, 0, 0, ob, sizeof ob, sink_out, &s) == WF_JPE_E_ARG, "h 0");
    CHECK(wf_jpe_begin(e, 64, 8, 90, 0, 0, ob, 1000, sink_out, &s) == WF_JPE_E_ARG, "obuf 1000");
    uint8_t bad1[6] = { 0xFF, 0xE1, 0, 9, 1, 2 };
    CHECK(wf_jpe_begin(e, 64, 8, 90, bad1, 6, ob, sizeof ob, sink_out, &s) == WF_JPE_E_ARG, "APP1 length");
    bad1[3] = 4;
    CHECK(wf_jpe_begin(e, 64, 8, 90, bad1, 6, ob, sizeof ob, sink_out, &s) == WF_JPE_OK, "APP1 ok");
    CHECK(wf_jpe_rows(e, fr, 128, 4) == WF_JPE_E_ROWS, "4 rows mid-frame");
    CHECK(wf_jpe_rows(e, fr, 128, 8) == WF_JPE_E_ROWS, "error sticks");

    s.n = 0; s.fnv = 2166136261u;
    CHECK(wf_jpe_begin(e, 64, 40, 90, 0, 0, ob, sizeof ob, sink_out, &s) == 0, "begin");
    CHECK(wf_jpe_rows(e, fr, 128, 16) == 0, "16 rows");
    CHECK(wf_jpe_end(e) == WF_JPE_E_STATE, "end before the last row");
    CHECK(wf_jpe_begin(e, 64, 40, 90, 0, 0, ob, sizeof ob, sink_out, &s) == 0, "begin again");
    CHECK(wf_jpe_rows(e, fr, 128, 32) == 0 && wf_jpe_rows(e, fr, 128, 16) == WF_JPE_E_ROWS, "past the bottom");
    CHECK(wf_jpe_begin(e, 64, 40, 90, 0, 0, ob, sizeof ob, sink_out, &s) == 0, "begin 3");
    CHECK(wf_jpe_rows(e, fr, 128, 32) == 0 && wf_jpe_rows(e, fr, 128, 8) == 0 && wf_jpe_end(e) == 0, "32 + 8 rows");
    CHECK(wf_jpe_end(e) != 0 && wf_jpe_rows(e, fr, 128, 8) != 0, "nothing after end");

    /* out() fails part way: the error comes back, no more out() calls, every later call fails */
    for (int after = 0; after < 1200; after += 97)
    {
        struct sink f = { 0, 0, 0, 2166136261u, after, 0 };
        uint8_t ob2[1024];
        int rc = wf_jpe_begin(e, 64, 40, 100, 0, 0, ob2, sizeof ob2, sink_out, &f);
        for (int y = 0; !rc && y < 40; y += 8) rc = wf_jpe_rows(e, fr, 128, 8);
        if (!rc) rc = wf_jpe_end(e);
        int calls = f.calls;
        CHECK(rc == WF_JPE_E_OUT, "out() failing after %d bytes: rc %d", after, rc);
        CHECK(wf_jpe_rows(e, fr, 128, 8) == WF_JPE_E_OUT && wf_jpe_end(e) == WF_JPE_E_OUT && f.calls == calls,
              "after the failure (%d bytes)", after);
        CHECK(f.n <= (uint32_t)after, "bytes taken %d > %d", f.n, after);
    }
    free(fr);
    free(e);
    printf("fail: struct wf_jpe %d B\n", (int)wf_jpe_bytes());
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: see the header\n"); return 2; }
    const char *m = argv[1];
    if (!strcmp(m, "synth") && argc == 3) return synth(argv[2]);
    if (!strcmp(m, "fail"))
    {
        fail_tests();
        printf("RESULT fail %d / %d\n", checks - fails, fails);
        return fails ? 1 : 0;
    }
    if (argc < 6) { fprintf(stderr, "usage: see the header\n"); return 2; }
    size_t n;
    int w = atoi(argv[3]), h = atoi(argv[4]), q = atoi(argv[5]);
    uint32_t *fr;
    if (!strcmp(m, "bench"))
    {
        /* the frame is ordinary cached memory (on the camera: the develop's band, just written); the output buffer is in
           the NCNB range memsim is told about (fb=0x48000000), like a DMA buffer for FIO_WriteFile */
        void *fb = mmap((void *)0x48000000, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (fb == MAP_FAILED) { perror("mmap"); return 2; }
        prof_mark(1);
        fr = rdfile(argv[2], &n);
        if (!fr || n < (size_t)w * h * 2) return 2;
        struct wf_jpe *e = malloc(wf_jpe_bytes());
        struct sink s = { 0, 0, 0, 2166136261u, -1, 0 };
        prof_mark(2);
        int rc = wf_jpe_begin(e, w, h, q, 0, 0, fb, 256 * 1024, bench_out, &s);
        for (int y = 0; !rc && y < h; y += 16)
            rc = wf_jpe_rows(e, fr + (size_t)y * (w / 2), w * 2, y + 16 <= h ? 16 : h - y);
        if (!rc) rc = wf_jpe_end(e);
        prof_mark(3);
        printf("bench: %dx%d q%d rc %d bytes %d fnv %08x\n", w, h, q, rc, (int)s.n, s.fnv);
        return rc ? 1 : 0;
    }
    fr = rdfile(argv[2], &n);
    if (!fr || n < (size_t)w * h * 2) { fprintf(stderr, "short input\n"); return 2; }
    if (!strcmp(m, "enc") && argc >= 9)
    {
        int band = atoi(argv[6]);
        uint32_t obn = (uint32_t)atoi(argv[7]);
        uint8_t *app1 = 0;
        size_t an = 0;
        if (argc > 9) app1 = rdfile(argv[9], &an);
        struct sink s = { malloc(64u << 20), 0, 64u << 20, 2166136261u, -1, 0 };
        int rc = encode(fr, w, h, q, band, obn, app1, (int)an, &s);
        if (rc) { printf("enc: rc %d\n", rc); return 1; }
        FILE *f = fopen(argv[8], "wb");
        fwrite(s.p, 1, s.n, f);
        fclose(f);
        printf("enc %s: %dx%d q%d band %d obuf %d -> %d bytes, fnv %08x\n", argv[2], w, h, q, band, (int)obn, (int)s.n, s.fnv);
        return 0;
    }
    if (!strcmp(m, "same"))
    {
        static const int bands[] = { 8, 16, 40, 0 };
        static const uint32_t obs[] = { 1024, 4096, 1u << 20 };
        uint32_t ref = 0, refn = 0;
        for (int b = 0; b < 4; b++)
            for (int o = 0; o < 3; o++)
            {
                struct sink s = { 0, 0, 0, 2166136261u, -1, 0 };
                int rc = encode(fr, w, h, q, bands[b] ? bands[b] : h, obs[o], 0, 0, &s);
                CHECK(rc == 0, "same: band %d obuf %d rc %d", bands[b], (int)obs[o], rc);
                if (!b && !o) { ref = s.fnv; refn = s.n; }
                CHECK(s.fnv == ref && s.n == refn, "band %d obuf %d: %08x %d bytes, first %08x %d", bands[b],
                      (int)obs[o], s.fnv, (int)s.n, ref, (int)refn);
            }
        printf("same %s q%d: %d bytes fnv %08x\nRESULT same %d / %d\n", argv[2], q, (int)refn, ref, checks - fails,
               fails);
        return fails ? 1 : 0;
    }
    fprintf(stderr, "unknown mode\n");
    return 2;
}
