/* wf_cr2_test.c - host test of the Darkroom's CR2 reader (src/wf_cr2.c, WindyStrife), linked with the module object
 * (module flags) and run under qemu-arm by tools/dr_cr2_check.sh.
 *
 *   wf_cr2_test check CR2 REFDIR [force8|force1|short]
 *        decode CR2 through rd() in 256 KB chunks (short: odd short reads) and compare bit for bit with
 *        REFDIR/raw.r14 and REFDIR/meta.txt from tools/cr2_dec.c (size, pitch, ISO, as-shot WB); guard bytes around the
 *        raw must survive. force8 / force1: the slower output paths (needs the -DWF_CR2_TEST object).
 *   wf_cr2_test bad CR2
 *        broken inputs: short head, not a CR2, truncated strip (rd ends early / strip_len cut), a marker or garbage in
 *        the entropy data, broken DHT / SOF / slices: an error where one is due, the guard bytes always intact.
 *   wf_cr2_test bench CR2 cached|uncached [REFDIR]
 *        memsim (tools/dr_cr2_check.sh): the raw at 0x48000000 (the NCNB shoot pool region the plugin is given), the
 *        input buffer cached (heap) or uncached (in that region); phase 1 = begin + all steps. REFDIR: compare after.
 * Prints PASS / FAIL lines and "RESULT n pass m fail". */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "wf_cr2.h"

extern int wf_cr2_force_mode __attribute__((weak));
void __attribute__((noinline)) prof_mark(int k) { __asm__ __volatile__("" :: "r"(k) : "memory"); }

static int npass, nfail;
#define CHECK(c, ...) do { if (c) npass++; else { nfail++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); \
                           printf("\n"); } } while (0)

#define GUARD 4096
#define INBUF (256 * 1024 + 2)

/* ------------------------------------------------------------------ readers */
struct frd { int fd; uint32_t chunk; uint32_t calls; int odd; };
static int rd_file(void *ctx, void *buf, uint32_t n)
{
    struct frd *f = ctx;
    uint32_t want = n < f->chunk ? n : f->chunk;
    if (f->odd) {                                    /* odd short reads: 1 .. 40000 bytes */
        uint32_t k = (f->calls * 2654435761u >> 7) % 40000u + 1;
        if (want > k) want = k;
    }
    f->calls++;
    ssize_t got = read(f->fd, buf, want);
    return got < 0 ? -1 : (int)got;
}

struct mrd { const uint8_t *b; uint32_t pos, end, chunk; };
static int rd_mem(void *ctx, void *buf, uint32_t n)
{
    struct mrd *m = ctx;
    uint32_t left = m->end > m->pos ? m->end - m->pos : 0;
    if (n > left) n = left;
    if (n > m->chunk) n = m->chunk;
    memcpy(buf, m->b + m->pos, n);
    m->pos += n;
    return (int)n;
}

/* ------------------------------------------------------------------ helpers */
static uint8_t *load(const char *path, uint32_t *n)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    fstat(fd, &st);
    uint8_t *b = malloc(st.st_size + 16);
    uint32_t k = 0;
    while (k < (uint32_t)st.st_size) {
        ssize_t g = read(fd, b + k, st.st_size - k);
        if (g <= 0) break;
        k += g;
    }
    close(fd);
    *n = k;
    return b;
}

static int parse_file(const uint8_t *f, uint32_t fn, struct wf_cr2_info *inf)
{
    uint32_t nh = fn < WF_CR2_HEAD ? fn : WF_CR2_HEAD;
    int r;
    for (int i = 0; i < 4; i++) {
        r = wf_cr2_parse(f, nh, fn, inf);
        if (r != WF_CR2_E_SHORT) break;
        nh = inf->need;
    }
    if (r) return r;
    uint32_t n = inf->strip_len < WF_CR2_LJHEAD ? inf->strip_len : WF_CR2_LJHEAD;
    return wf_cr2_parse_ljpeg(f + inf->strip_off, n, inf);
}

static uint8_t *raw_alloc(const struct wf_cr2_info *inf, uint8_t **base)
{
    size_t rn = (size_t)inf->pitch * inf->h;
    uint8_t *b = malloc(rn + 2 * GUARD);
    memset(b, 0x5A, GUARD);
    memset(b + GUARD, 0xA5, rn);
    memset(b + GUARD + rn, 0x5A, GUARD);
    *base = b;
    return b + GUARD;
}

static int guards_ok(const uint8_t *base, size_t rn)
{
    for (int i = 0; i < GUARD; i++)
        if (base[i] != 0x5A || base[GUARD + rn + i] != 0x5A) return 0;
    return 1;
}

static int decode(const struct wf_cr2_info *inf, uint8_t *raw, uint8_t *inbuf, int (*rd)(void *, void *, uint32_t),
                  void *ctx, int *steps)
{
    static struct wf_cr2_dec d;
    int r = wf_cr2_dec_begin(&d, inf, raw, inf->pitch, inbuf, INBUF, rd, ctx);
    if (r) return r;
    int k = 0, prev = -1;
    while ((r = wf_cr2_dec_step(&d, 64)) > 0) {
        if (d.jrow <= prev) return -100;            /* no progress */
        prev = d.jrow;
        k++;
    }
    if (steps) *steps = k + 1;
    return r;
}

/* ------------------------------------------------------------------ check */
static int do_check(const char *path, const char *refdir, const char *opt)
{
    uint32_t fn;
    uint8_t *f = load(path, &fn);
    CHECK(f, "%s: cannot read", path);
    if (!f) return 1;
    struct wf_cr2_info inf;
    int r = parse_file(f, fn, &inf);
    CHECK(r == 0, "%s: parse %d %s", path, r, wf_cr2_err(r));
    if (r) return 1;
    printf("cr2 %s: %dx%d pitch %d (jpeg %dx%d x %d, %d bit, slices %d x %d + %d), ISO %d, WB %d %d %d %d (ColorData %d), "
           "orient %d, strip %u + %u, data at +%u, head %u B, IFD0 JPEG %dx%d %u B, thumb %u B, RGB %dx%d x%d %u B\n",
           path, inf.w, inf.h, inf.pitch, inf.jw, inf.jh, inf.nc, inf.prec, inf.sl_n, inf.sl_w, inf.sl_last, inf.iso,
           inf.wb[0], inf.wb[1], inf.wb[2], inf.wb[3], inf.cd_count, inf.orient, inf.strip_off, inf.strip_len,
           inf.data_off, inf.need, inf.jpg_w, inf.jpg_h, inf.jpg_len, inf.thumb_len, inf.rgb_w, inf.rgb_h,
           inf.rgb_bits, inf.rgb_len);
    free(f);

    if (opt && !strcmp(opt, "force8")) { if (&wf_cr2_force_mode) wf_cr2_force_mode = 8; else { printf("no test object\n"); return 1; } }
    if (opt && !strcmp(opt, "force1")) { if (&wf_cr2_force_mode) wf_cr2_force_mode = 1; else { printf("no test object\n"); return 1; } }
    uint8_t *base, *raw = raw_alloc(&inf, &base);
    uint8_t *inbuf = malloc(INBUF);
    struct frd fr = { open(path, O_RDONLY), 256 * 1024, 0, opt && !strcmp(opt, "short") };
    lseek(fr.fd, inf.strip_off, SEEK_SET);
    int steps = 0;
    r = decode(&inf, raw, inbuf, rd_file, &fr, &steps);
    close(fr.fd);
    size_t rn = (size_t)inf.pitch * inf.h;
    CHECK(r == 0, "%s: decode %d %s", path, r, wf_cr2_err(r));
    CHECK(guards_ok(base, rn), "%s: guard bytes changed", path);

    char p2[1024];
    snprintf(p2, sizeof p2, "%s/raw.r14", refdir);
    uint32_t refn;
    uint8_t *ref = load(p2, &refn);
    CHECK(ref && refn == rn, "%s: raw.r14 %u B, ours %u B", path, ref ? refn : 0, (unsigned)rn);
    if (ref && refn == rn) {
        size_t bad = 0, first = (size_t)-1;
        for (size_t i = 0; i < rn; i++)
            if (raw[i] != ref[i]) { if (first == (size_t)-1) first = i; bad++; }
        CHECK(bad == 0, "%s: %u bytes differ from cr2_dec (first at %u: row %u)", path, (unsigned)bad,
              (unsigned)first, (unsigned)(first / inf.pitch));
        if (!bad) printf("PASS %s bit-exact (%u bytes, %d steps, %u reads, mode %s)\n", path, (unsigned)rn, steps,
                         fr.calls, opt ? opt : "16");
    }
    free(ref);
    snprintf(p2, sizeof p2, "%s/meta.txt", refdir);
    FILE *m = fopen(p2, "r");
    int W = 0, H = 0, P = 0, iso = 0, wb[4] = { 0 }, wm = 0;
    float bl = 0;
    CHECK(m && fscanf(m, "%d %d %d %d %f %d %d %d %d %d", &W, &H, &P, &iso, &bl, &wb[0], &wb[1], &wb[2], &wb[3], &wm) == 10,
          "%s: meta.txt", path);
    if (m) fclose(m);
    CHECK(W == inf.w && H == inf.h && P == inf.pitch && iso == inf.iso, "%s: size / ISO %d %d %d %d vs %d %d %d %d", path,
          W, H, P, iso, inf.w, inf.h, inf.pitch, inf.iso);
    CHECK(wb[0] == inf.wb[0] && wb[1] == inf.wb[1] && wb[2] == inf.wb[2] && wb[3] == inf.wb[3], "%s: WB", path);
    free(base);
    free(inbuf);
    return 0;
}

/* ------------------------------------------------------------------ broken inputs */
static int run_mem(const struct wf_cr2_info *inf, const uint8_t *f, uint32_t end, int *guard)
{
    uint8_t *base, *raw = raw_alloc(inf, &base);
    uint8_t *inbuf = malloc(INBUF);
    struct mrd m = { f, inf->strip_off, end, 200 * 1024 };
    int r = decode(inf, raw, inbuf, rd_mem, &m, 0);
    *guard = guards_ok(base, (size_t)inf->pitch * inf->h);
    free(base);
    free(inbuf);
    return r;
}

static int do_bad(const char *path)
{
    uint32_t fn;
    uint8_t *f = load(path, &fn);
    CHECK(f, "%s: cannot read", path);
    if (!f) return 1;
    struct wf_cr2_info inf, t;
    int r = wf_cr2_parse(f, 1000, fn, &t);
    CHECK(r == WF_CR2_E_SHORT && t.need > 1000, "short head: %d need %u", r, t.need);
    printf("short head 1000 B: %s, need %u\n", wf_cr2_err(r), t.need);
    r = wf_cr2_parse(f, 8, fn, &t);
    CHECK(r == WF_CR2_E_SHORT, "8-byte head: %d", r);
    r = parse_file(f, fn, &inf);
    CHECK(r == 0, "parse %d", r);
    if (r) return 1;
    uint8_t keep = f[8];
    f[8] = 'X';
    CHECK(wf_cr2_parse(f, WF_CR2_HEAD, fn, &t) == WF_CR2_E_NOTCR2, "not a CR2");
    f[8] = keep;
    CHECK(wf_cr2_parse(f, WF_CR2_HEAD, 20000, &t) == WF_CR2_E_IFD, "file shorter than its IFDs: %d",
          wf_cr2_parse(f, WF_CR2_HEAD, 20000, &t));

    int g;
    /* rd ends at 60 % of the strip */
    r = run_mem(&inf, f, inf.strip_off + inf.strip_len / 10 * 6, &g);
    printf("rd ends at 60%%: %d %s\n", r, wf_cr2_err(r));
    CHECK(r == WF_CR2_E_TRUNC && g, "rd ends early: %d guard %d", r, g);
    /* strip_len cut */
    t = inf;
    t.strip_len = inf.strip_len / 3;
    r = run_mem(&t, f, fn, &g);
    CHECK(r == WF_CR2_E_TRUNC && g, "strip_len cut: %d guard %d", r, g);
    /* the strip loses its last 64 bytes */
    t = inf;
    t.strip_len = inf.strip_len - 64;
    r = run_mem(&t, f, fn, &g);
    printf("strip - 64 B: %d %s\n", r, wf_cr2_err(r));
    CHECK(r == WF_CR2_E_TRUNC && g, "strip - 64 B: %d guard %d", r, g);
    /* a marker in the middle of the entropy data */
    uint8_t *c = malloc(fn);
    memcpy(c, f, fn);
    uint32_t mid = inf.strip_off + inf.strip_len / 2;
    c[mid] = 0xFF; c[mid + 1] = 0xD3;
    r = run_mem(&inf, c, fn, &g);
    printf("marker in the middle: %d %s\n", r, wf_cr2_err(r));
    CHECK(r < 0 && g, "marker in the middle: %d guard %d", r, g);
    /* garbage: 64 KB of pseudo-random bytes in the middle, and right after the header */
    memcpy(c, f, fn);
    uint32_t x = 12345;
    for (int i = 0; i < 65536; i++) { x = x * 1103515245u + 12345u; c[mid + i] = (uint8_t)(x >> 16); }
    r = run_mem(&inf, c, fn, &g);
    printf("garbage in the middle: %d %s\n", r, wf_cr2_err(r));
    CHECK(g, "garbage in the middle: guard changed (r %d)", r);
    memcpy(c, f, fn);
    for (int i = 0; i < 4096; i++) { x = x * 1103515245u + 12345u; c[inf.strip_off + inf.data_off + i] = (uint8_t)(x >> 16); }
    r = run_mem(&inf, c, fn, &g);
    printf("garbage at the start: %d %s\n", r, wf_cr2_err(r));
    CHECK(g, "garbage at the start: guard changed (r %d)", r);
    /* all 0xFF 0xFF... (bits of ones, a code not in the table at some point) */
    memcpy(c, f, fn);
    for (int i = 0; i < 4096; i++) c[inf.strip_off + inf.data_off + i] = (uint8_t)(i & 1 ? 0x00 : 0xFF);
    r = run_mem(&inf, c, fn, &g);
    printf("all-ones bits: %d %s\n", r, wf_cr2_err(r));
    CHECK(g, "all-ones bits: guard changed (r %d)", r);
    free(c);

    /* header damage, on a copy of the strip head */
    uint8_t h[WF_CR2_LJHEAD];
    memcpy(h, f + inf.strip_off, sizeof h);
    uint32_t k = 2;
    while (h[k + 1] != 0xC4) k += 2 + (h[k + 2] << 8 | h[k + 3]);
    uint8_t bk = h[k + 5];
    h[k + 5] = 3;                                    /* three codes of length 1 */
    t = inf;
    r = wf_cr2_parse_ljpeg(h, sizeof h, &t);
    if (r == 0) {                                    /* the counts still parse: begin must refuse the table */
        static struct wf_cr2_dec d;
        uint8_t *base, *raw = raw_alloc(&t, &base);
        uint8_t *ib = malloc(INBUF);
        struct mrd m = { f, inf.strip_off, fn, 200 * 1024 };
        r = wf_cr2_dec_begin(&d, &t, raw, t.pitch, ib, INBUF, rd_mem, &m);
        free(ib);
        free(base);
    }
    printf("DHT overflow: %d %s\n", r, wf_cr2_err(r));
    CHECK(r == WF_CR2_E_DHT || r == WF_CR2_E_MARKER, "DHT overflow: %d", r);
    h[k + 5] = bk;
    k = 2;
    while (h[k + 1] != 0xC3) k += 2 + (h[k + 2] << 8 | h[k + 3]);
    bk = h[k + 9];
    h[k + 9] = 5;                                    /* five components */
    t = inf;
    CHECK(wf_cr2_parse_ljpeg(h, sizeof h, &t) == WF_CR2_E_SOF, "SOF nc 5");
    h[k + 9] = bk;
    t = inf;
    t.sl_w += 8;
    CHECK(wf_cr2_parse_ljpeg(h, sizeof h, &t) == WF_CR2_E_SLICE, "slices");
    t = inf;
    CHECK(wf_cr2_parse_ljpeg(h, 40, &t) == WF_CR2_E_SHORT, "ljpeg head short");
    /* a too small input buffer / odd pitch */
    {
        static struct wf_cr2_dec d;
        uint8_t *base, *raw = raw_alloc(&inf, &base);
        uint8_t *ib = malloc(INBUF);
        CHECK(wf_cr2_dec_begin(&d, &inf, raw, inf.pitch, ib, wf_cr2_inbuf_min(&inf) - 1, rd_mem, 0) == WF_CR2_E_PARAM,
              "small inbuf");
        CHECK(wf_cr2_dec_begin(&d, &inf, raw, inf.pitch - 2, ib, INBUF, rd_mem, 0) == WF_CR2_E_PARAM, "small pitch");
        CHECK(wf_cr2_dec_step(&d, 1) == WF_CR2_E_PARAM, "step after a failed begin");
        free(ib);
        free(base);
    }
    printf("sizeof struct wf_cr2_dec %u, struct wf_cr2_info %u, inbuf min %u\n", (unsigned)sizeof(struct wf_cr2_dec),
           (unsigned)sizeof(struct wf_cr2_info), wf_cr2_inbuf_min(&inf));
    free(f);
    return 0;
}

/* ------------------------------------------------------------------ memsim */
#define FB_ADDR 0x48000000u
static int do_bench(const char *path, int uncached, const char *refdir)
{
    uint32_t fn;
    uint8_t *f = load(path, &fn);
    if (!f) return 1;
    struct wf_cr2_info inf;
    int r = parse_file(f, fn, &inf);
    free(f);
    if (r) { printf("parse %d\n", r); return 1; }
    size_t rn = (size_t)inf.pitch * inf.h;
    uint8_t *fb = mmap((void *)FB_ADDR, rn + INBUF + 8192, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    if (fb != (uint8_t *)FB_ADDR) { perror("mmap"); return 2; }
    uint8_t *raw = fb, *inbuf = uncached ? fb + ((rn + 31) & ~(size_t)31) : malloc(INBUF);
    struct frd fr = { open(path, O_RDONLY), 256 * 1024, 0, 0 };
    lseek(fr.fd, inf.strip_off, SEEK_SET);
    static struct wf_cr2_dec d;
    prof_mark(1);
    r = wf_cr2_dec_begin(&d, &inf, raw, inf.pitch, inbuf, INBUF, rd_file, &fr);
    if (!r)
        while ((r = wf_cr2_dec_step(&d, 64)) > 0) {}
    prof_mark(2);
    close(fr.fd);
    printf("bench %s: input %s, decode %d %s, mode %d, %u reads\n", path, uncached ? "uncached" : "cached", r,
           wf_cr2_err(r), d.mode, fr.calls);
    if (refdir) {
        char p2[1024];
        snprintf(p2, sizeof p2, "%s/raw.r14", refdir);
        uint32_t refn;
        uint8_t *ref = load(p2, &refn);
        CHECK(ref && refn == rn && !memcmp(ref, raw, rn), "bench output differs from cr2_dec");
        free(ref);
    }
    return r;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    if (argc >= 4 && !strcmp(argv[1], "check")) do_check(argv[2], argv[3], argc > 4 ? argv[4] : 0);
    else if (argc >= 3 && !strcmp(argv[1], "bad")) do_bad(argv[2]);
    else if (argc >= 4 && !strcmp(argv[1], "bench")) do_bench(argv[2], !strcmp(argv[3], "uncached"), argc > 4 ? argv[4] : 0);
    else { fprintf(stderr, "usage: %s check CR2 REFDIR [force8|force1|short] | bad CR2 | bench CR2 cached|uncached [REF]\n",
                   argv[0]); return 2; }
    printf("RESULT %d pass %d fail\n", npass, nfail);
    return nfail ? 1 : 0;
}
