/* wf_dark_test.c - Roll + Darkroom, the per-frame chain on the host (docs/DESIGN.md 3.4), WindyStrife's own code: the
 * module objects (wf_cr2.o, wf_dev.o, wf_jpe.o, wf_darkp.o, module flags) linked into a static ARM Linux program run by
 * qemu-arm-static, doing what the darkroom task does with one frame of a roll:
 *   CR2 head -> wf_cr2_parse / parse_ljpeg -> wf_cr2_dec_step in bands (rd() reads 256 KB) -> wf_dev_levels ->
 *   wf_dk_scales (Canon's as-shot WB + the scene scale + ISO trim) -> wf_dev_build (sRGB base develop) / prepare / rows in
 *   64-row bands (thumbnails gathered) -> wf_jpe at q95 in 16-row bands, Canon's APP1 of the paired JPEG with our
 *   UserComment -> OUT/<name>.JPG, print.l8 / wire.l8.
 *
 *   wf_dark_test frame CR2 JPG OUTDIR [JPEG.UYVY]
 *     JPEG.UYVY (tools/dr_dark_check.sh: Canon's JPEG as its UYVY frame) adds the develop's WB / exposure fit against
 *     Canon's own frame, for the per-ISO trim table: "trim ISO n: fit vs scene scale +x.xx EV, WB R/G .. B/G ..".
 *   wf_dark_test units     wf_darkp.c on synthetic input (APP1 walk, comment, scales, thumbnails)
 * Prints RESULT pass / fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "wf_cr2.h"
#include "wf_dev.h"
#include "wf_jpe.h"
#include "wf_darkp.h"

static int pass, fail;
#define CHECK(c, ...) do { if (c) pass++; else { fail++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); \
                           printf("\n"); } } while (0)

/* ------------------------------------------------------------------ units */
static void put16(uint8_t *p, unsigned v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void units(void)
{
    /* a JPEG head: SOI, APP0 (16), APP1 Exif with IFD0 -> Exif IFD -> UserComment of 64 zero bytes */
    static uint8_t j[512];
    memset(j, 0, sizeof j);
    int o = 0;
    j[o++] = 0xFF; j[o++] = 0xD8;
    j[o++] = 0xFF; j[o++] = 0xE0; j[o++] = 0; j[o++] = 16; o += 14;
    int a1 = o;
    j[o++] = 0xFF; j[o++] = 0xE1;
    int lenpos = o; o += 2;
    memcpy(j + o, "Exif\0\0", 6); o += 6;
    uint8_t *t = j + o;
    memcpy(t, "II", 2); put16(t + 2, 42); put32(t + 4, 8);
    put16(t + 8, 1);                                         /* IFD0: one entry, 0x8769 -> 26 */
    put16(t + 10, 0x8769); put16(t + 12, 4); put32(t + 14, 1); put32(t + 18, 26); put32(t + 22, 0);
    put16(t + 26, 1);                                        /* Exif IFD: 0x9286 UNDEFINED 64 at 44 */
    put16(t + 28, 0x9286); put16(t + 30, 7); put32(t + 32, 64); put32(t + 36, 44); put32(t + 40, 0);
    int tlen = 44 + 64;
    o += tlen;
    unsigned l = (unsigned)(o - a1 - 2);
    j[lenpos] = (uint8_t)(l >> 8); j[lenpos + 1] = (uint8_t)l;
    j[o++] = 0xFF; j[o++] = 0xDB;
    uint32_t off = 0, len = 0;
    CHECK(wf_dk_app1(j, (uint32_t)o, &off, &len) == 0 && off == (uint32_t)a1 && len == l + 2, "app1 %u %u", off, len);
    CHECK(wf_dk_app1(j, (uint32_t)a1 + 8, &off, &len) == -1, "app1 cut short");
    CHECK(wf_dk_comment(j + a1, len, "WindyFilm Darkroom: test") == 0, "comment");
    CHECK(!memcmp(t + 44, "ASCII\0\0\0WindyFilm Darkroom: test", 32) && t[44 + 32] == 0, "comment bytes");
    char big[80];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    CHECK(wf_dk_comment(j + a1, len, big) == -1, "comment too long refused");
    uint8_t jb[4] = { 0xFF, 0xD8, 0xFF, 0xDA };
    CHECK(wf_dk_app1(jb, 4, &off, &len) == -1, "no app1");

    /* scales: as-shot WB gives the ratios, daylight without it; exposure from the clip */
    int32_t m[3];
    int wb[4] = { 2048, 1024, 1024, 1536 };
    CHECK(wf_dk_scales(WF_DK_EOSM, wb, 2048, 14336, 999, m) == 1, "as-shot");
    double r = (double)m[0] / m[1], b = (double)m[2] / m[1];
    CHECK(fabs(r - 2.0) < 0.002 && fabs(b - 1.5) < 0.002, "ratios %.4f %.4f", r, b);
    double g = m[1] / 4294967296.0 * 12288;                  /* the clip in linear */
    double gw = 1.773 * pow(2.0, wf_dk_trim10(WF_DK_EOSM, 999) / 10.0);
    CHECK(fabs(g / gw - 1) < 0.006, "clip %.4f (want %.4f)", g, gw);
    CHECK(wf_dk_scales(WF_DK_EOSM, 0, 2048, 14336, 999, m) == 0 && fabs((double)m[0] / m[1] - 9892 / 4096.0) < 0.002, "daylight");
    int z[4] = { 0, 0, 0, 0 };
    CHECK(wf_dk_scales(WF_DK_EOSM, z, 2048, 14336, 999, m) == 0, "zero WB = daylight");

    /* thumbnails: a frame whose Y = column / 18 + 10 * (row / 18) (mod 256) gives exact box values */
    int W = 5184, H = 3456;
    uint32_t *rows = malloc((size_t)W * 2 * 64);
    static uint8_t pr[WF_DK_PW * WF_DK_PH], wi[WF_DK_TW * WF_DK_TH];
    struct wf_dk_thumb th;
    wf_dk_thumb_begin(&th, W, H);
    for (int y = 0; y < H; y += 64) {
        int n = H - y < 64 ? H - y : 64;
        for (int r2 = 0; r2 < n; r2++)
            for (int x = 0; x < W / 2; x++) {
                int yy = y + r2;
                int v0 = (2 * x / 18 + 10 * (yy / 18)) & 0xFF, v1 = ((2 * x + 1) / 18 + 10 * (yy / 18)) & 0xFF;
                rows[r2 * (W / 2) + x] = 0x80u | (uint32_t)v0 << 8 | 0x80u << 16 | (uint32_t)v1 << 24;
            }
        wf_dk_thumb_rows(&th, rows, W * 2, y, n, pr);
    }
    CHECK(wf_dk_thumb_done(&th) == WF_DK_PH, "thumb rows %d", wf_dk_thumb_done(&th));
    int bad = 0;
    for (int y = 0; y < WF_DK_PH; y++)
        for (int x = 0; x < WF_DK_PW; x++)
            bad += pr[y * WF_DK_PW + x] != ((x + 10 * y) & 0xFF);
    CHECK(bad == 0, "thumb values: %d wrong", bad);
    wf_dk_wire(pr, wi);
    CHECK(wi[0] == (uint8_t)((16 * 0 + 4 * (0 + 1 + 2 + 3) + 40 * (0 + 1 + 2 + 3) + 8) >> 4), "wire %d", wi[0]);
    free(rows);
}

/* ------------------------------------------------------------------ one frame */
static uint8_t *slurp(const char *p, uint32_t max, uint32_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    uint8_t *b = malloc(max);
    *n = (uint32_t)fread(b, 1, max, f);
    fclose(f);
    return b;
}
static int rd(void *ctx, void *buf, uint32_t n)
{
    if (n > 256 * 1024) n = 256 * 1024;                     /* the camera's read size */
    return (int)fread(buf, 1, n, (FILE *)ctx);
}
struct sink { FILE *f; uint32_t n; };
static int out(void *ctx, const uint8_t *p, uint32_t n)
{
    struct sink *s = ctx;
    s->n += n;
    return fwrite(p, 1, n, s->f) == n ? 0 : -1;
}

static void frame(const char *cr2, const char *jpg, const char *od, const char *juyvy)
{
    FILE *f = fopen(cr2, "rb");
    CHECK(f != 0, "open %s", cr2);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    uint32_t fsize = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    static uint8_t head[WF_CR2_HEAD];
    uint32_t nh = (uint32_t)fread(head, 1, sizeof head, f);
    struct wf_cr2_info inf;
    int rc = wf_cr2_parse(head, nh, fsize, &inf);
    CHECK(rc == 0, "parse %s", wf_cr2_err(rc));
    if (rc) { fclose(f); return; }
    uint8_t lj[WF_CR2_LJHEAD];
    fseek(f, inf.strip_off, SEEK_SET);
    uint32_t nl = (uint32_t)fread(lj, 1, sizeof lj, f);
    rc = wf_cr2_parse_ljpeg(lj, nl, &inf);
    CHECK(rc == 0, "ljpeg %s", wf_cr2_err(rc));
    if (rc) { fclose(f); return; }
    uint8_t *raw = malloc((size_t)inf.pitch * inf.h + 4096);
    uint32_t ibn = 256 * 1024;
    CHECK(ibn >= wf_cr2_inbuf_min(&inf), "inbuf");
    uint8_t *ib = malloc(ibn);
    static struct wf_cr2_dec d;
    fseek(f, inf.strip_off, SEEK_SET);
    rc = wf_cr2_dec_begin(&d, &inf, raw, inf.pitch, ib, ibn, rd, f);
    int steps = 0;
    while (rc >= 0 && (rc = wf_cr2_dec_step(&d, 64)) > 0)
        steps++;
    fclose(f);
    CHECK(rc == 0, "decode %s", wf_cr2_err(rc));
    free(ib);
    if (rc) { free(raw); return; }

    /* levels and scales (EOS M L frame at 84, 64 of the CR2's sensor image) */
    int W = 5184, H = 3456;
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = inf.pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = inf.w; j.ay1 = inf.h;
    j.x0 = 84; j.y0 = 64; j.w = W; j.h = H; j.cfa = 0;
    void *scr = malloc(wf_dev_scratch_bytes(W, H));
    int lb = 0, lw = 0;
    wf_dev_levels(scr, &j, 16, 64, &lb, &lw);
    j.black = lb ? lb : 2048;
    j.white = lw ? lw : wf_dk_white(WF_DK_EOSM, inf.iso);
    int as = wf_dk_scales(WF_DK_EOSM, inf.wb, j.black, j.white, inf.iso, j.wb_q8);
    j.roll = 1;
    static struct wf_dev_tabs T;
    CHECK(wf_dev_build(&T, WF_DEV_SRGB, 1, WF_DEV_EOSM, 0, 0, 0, 0) == 0, "build sRGB");
    struct wf_dev_stat st;
    rc = wf_dev_prepare(scr, &T, &j, &st);
    CHECK(rc == 0 && st.wb_src == 2, "prepare %d src %d", rc, st.wb_src);
    printf("frame %s: ISO %d %d steps, black %d white %d%s, WB %s R/G %.3f B/G %.3f, trim %+d/10 EV, shoulder %d\n", cr2,
           inf.iso, steps, j.black, j.white, lw ? " (pile-up)" : " (table)", as ? "as-shot" : "daylight",
           (double)st.m_q8[0] / st.m_q8[1], (double)st.m_q8[2] / st.m_q8[1], wf_dk_trim10(WF_DK_EOSM, inf.iso), st.roll);

    /* the fit against Canon's own frame (the at-shot B): how far the scene scale is from Canon's exposure */
    uint32_t *frm = malloc((size_t)W * H * 2);
    if (juyvy) {
        FILE *u = fopen(juyvy, "rb");
        CHECK(u && fread(frm, 2, (size_t)W * H, u) == (size_t)W * H, "jpeg.uyvy");
        if (u) fclose(u);
        struct wf_dev_job j2 = j;
        memset(j2.wb_q8, 0, sizeof j2.wb_q8);
        j2.canon = frm; j2.canon_y0 = 0; j2.canon_h = H;
        struct wf_dev_stat s2;
        if (wf_dev_prepare(scr, &T, &j2, &s2) == 0 && s2.wb_src == 1) {
            double ev = log2((double)s2.m_q8[1] / st.m_q8[1]);
            printf("trim ISO %d: fit vs scene scale %+.2f EV (tier %d n %d), WB fit R/G %.3f B/G %.3f vs as-shot %.3f %.3f\n",
                   inf.iso, ev, s2.fit_tier, s2.fit_n, (double)s2.m_q8[0] / s2.m_q8[1], (double)s2.m_q8[2] / s2.m_q8[1],
                   (double)st.m_q8[0] / st.m_q8[1], (double)st.m_q8[2] / st.m_q8[1]);
        } else {
            printf("trim ISO %d: no fit\n", inf.iso);
        }
        CHECK(wf_dev_prepare(scr, &T, &j, &st) == 0, "prepare again");
    }

    /* develop in bands with the thumbnails */
    static uint8_t pr[WF_DK_PW * WF_DK_PH], wi[WF_DK_TW * WF_DK_TH];
    struct wf_dk_thumb th;
    wf_dk_thumb_begin(&th, W, H);
    for (int y = 0; y < H; y += 64) {
        int yb = y + 64 < H ? y + 64 : H;
        wf_dev_rows(scr, y, yb, frm + y * (W / 2), W * 2);
        wf_dk_thumb_rows(&th, frm + y * (W / 2), W * 2, y, yb - y, pr);
    }
    CHECK(wf_dk_thumb_done(&th) == WF_DK_PH, "thumbs");
    wf_dk_wire(pr, wi);

    /* the print: Canon's APP1 + our comment */
    uint32_t jn = 0, ao = 0, al = 0;
    uint8_t *jh = jpg ? slurp(jpg, 65536 + 16, &jn) : 0;
    uint8_t *app1 = 0;
    if (jh && wf_dk_app1(jh, jn, &ao, &al) == 0) {
        app1 = jh + ao;
        CHECK(wf_dk_comment(app1, al, "WindyFilm Darkroom: host test | sRGB base develop") == 0, "comment in Canon's APP1");
    }
    CHECK(app1 != 0, "no APP1 in %s", jpg ? jpg : "-");
    const char *b = strrchr(cr2, '/');
    b = b ? b + 1 : cr2;
    char p[600], name[64];
    snprintf(name, sizeof name, "%.8s", b);
    snprintf(p, sizeof p, "%s/%s.JPG", od, name);
    struct sink s = { fopen(p, "wb"), 0 };
    CHECK(s.f != 0, "create %s", p);
    static uint8_t ob[64 * 1024];
    uint8_t *e = malloc(wf_jpe_bytes());
    rc = wf_jpe_begin((struct wf_jpe *)e, W, H, 95, app1, (int)al, ob, sizeof ob, out, &s);
    for (int y = 0; rc == 0 && y < H; y += 16)
        rc = wf_jpe_rows((struct wf_jpe *)e, frm + y * (W / 2), W * 2, H - y < 16 ? H - y : 16);
    if (rc == 0) rc = wf_jpe_end((struct wf_jpe *)e);
    fclose(s.f);
    CHECK(rc == 0, "encode %d", rc);
    printf("print %s: %u bytes, APP1 %u bytes\n", p, s.n, al);
    snprintf(p, sizeof p, "%s/%s.dev.uyvy", od, name);
    FILE *u = fopen(p, "wb");
    if (u) { fwrite(frm, 2, (size_t)W * H, u); fclose(u); }
    snprintf(p, sizeof p, "%s/%s.print.l8", od, name);
    if ((u = fopen(p, "wb"))) { fwrite(pr, 1, sizeof pr, u); fclose(u); }
    snprintf(p, sizeof p, "%s/%s.wire.l8", od, name);
    if ((u = fopen(p, "wb"))) { fwrite(wi, 1, sizeof wi, u); fclose(u); }
    free(e); free(jh); free(frm); free(scr); free(raw);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "units"))
        units();
    else if (argc >= 5 && !strcmp(argv[1], "frame"))
        frame(argv[2], strcmp(argv[3], "-") ? argv[3] : 0, argv[4], argc > 5 ? argv[5] : 0);
    else {
        fprintf(stderr, "usage: wf_dark_test units | frame CR2 JPG|- OUTDIR [JPEG.UYVY]\n");
        return 2;
    }
    printf("RESULT %d / %d\n", pass, fail);
    return fail ? 1 : 0;
}
