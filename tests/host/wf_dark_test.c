/* wf_dark_test.c - Roll + Darkroom, the per-frame chain on the host (docs/DESIGN.md 3.6), WindyStrife's own code: the
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
 *   wf_dark_test units     wf_darkp.c on synthetic input (APP1 walk, comment, scales, thumbnails) and wf_print.c (the
 *                          print finishes, DESIGN 3.9: canvas layouts, the shot's Exif details, caption text, the date,
 *                          the font pack WFPRINT.DAT when it is in the work dir, the date stamp, plain-path identity)
 *   wf_dark_test finish CR2 JPG OUTDIR TAG FRAME INFO DATE PACK|- LOOK ROLL|-
 *     the frame's print as the darkroom makes it with the print finishes (wf_dark.c dk_finish): FRAME / INFO / DATE =
 *     WF_PFR_* / WF_PIN_* / WF_PDT_*, LOOK = the comment text line 3 comes from, ROLL = the roll text right of line 1.
 *     OUTDIR/<name>_<TAG>.JPG; the log line "canvas CW CH IX IY ORIENT" for tools/dr_dark_check.sh. All Off = the frame
 *     mode's print byte for byte.
 *   wf_dark_test devset LIST OUTDIR   several CR2s through several develop tables (sRGB / cube / display cube), full-size
 *                          UYVY or third-size PPM: the look sweep's base frames (tools/look_sweep.sh)
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
#include "wf_print.h"

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

    /* the Darkroom print's EV (the effects' highlight gain) from the CR2: within 0.1 EV of log2(N^2 / t) - log2(ISO / 100)
       (1/8 EV steps) over the shutter, aperture and ISO ranges, and the owner's frames (11-14 set: EV10 1/320 f/2.8 ISO 200, EV14
       1/1600 f/5.6 ISO 400, EV9 1/250 f/1.6 ISO 100 as the camera logged them) */
    static const uint32_t ets[][2] = { { 1, 4000 }, { 1, 1600 }, { 1, 320 }, { 1, 250 }, { 1, 30 }, { 1, 1 }, { 30, 1 }, { 10, 13 } };
    static const uint32_t fns[][2] = { { 10, 10 }, { 12, 10 }, { 16, 10 }, { 28, 10 }, { 56, 10 }, { 8, 1 }, { 22, 1 }, { 63, 10 } };
    static const int isos[] = { 100, 160, 200, 400, 800, 1600, 3200, 6400, 12800 };
    double worst = 0;
    for (int a = 0; a < 8; a++)
        for (int c = 0; c < 8; c++)
            for (int s = 0; s < 9; s++) {
                double n = (double)fns[c][0] / fns[c][1], tt = (double)ets[a][0] / ets[a][1];
                double want = log2(n * n / tt) - log2(isos[s] / 100.0);
                double got = wf_dk_ev8(isos[s], ets[a][0], ets[a][1], fns[c][0], fns[c][1]) / 8.0;
                if (fabs(got - want) > worst) worst = fabs(got - want);
            }
    CHECK(worst <= 0.1, "EV from the CR2: worst %.3f EV off", worst);
    int e1 = wf_dk_ev8(200, 1, 320, 28, 10), e2 = wf_dk_ev8(400, 1, 1600, 56, 10), e3 = wf_dk_ev8(100, 1, 250, 16, 10);
    CHECK((e1 + 4) >> 3 == 10 && (e2 + 4) >> 3 == 14 && (e3 + 4) >> 3 == 9, "the owner's frames: EV %d %d %d (want 10 14 9)",
          (e1 + 4) >> 3, (e2 + 4) >> 3, (e3 + 4) >> 3);
    CHECK(wf_dk_ev8(0, 1, 320, 28, 10) == WF_DK_EV_BAD && wf_dk_ev8(200, 0, 320, 28, 10) == WF_DK_EV_BAD
          && wf_dk_ev8(200, 1, 0, 28, 10) == WF_DK_EV_BAD && wf_dk_ev8(200, 1, 320, 0, 10) == WF_DK_EV_BAD
          && wf_dk_ev8(200, 1, 320, 28, 0) == WF_DK_EV_BAD, "EV: a missing value = unknown");

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

static uint8_t *slurp(const char *p, uint32_t max, uint32_t *n);

/* ------------------------------------------------------------------ print finishes (wf_print.c) */
/* a TIFF (little or big endian) as Canon writes the APP1: IFD0 Model, Orientation, Exif IFD with ExposureTime, FNumber,
   ISO, DateTimeOriginal, FocalLength, PixelX / YDimension (SHORT / LONG), LensModel. Returns its size. */
static int mk_tiff(uint8_t *t, int le, int orient, int ydim_long)
{
    memset(t, 0, 512);
    #define W16(p, v) do { if (le) { (p)[0] = (uint8_t)(v); (p)[1] = (uint8_t)((v) >> 8); } \
                           else { (p)[0] = (uint8_t)((v) >> 8); (p)[1] = (uint8_t)(v); } } while (0)
    #define W32(p, v) do { uint32_t v_ = (v); for (int q = 0; q < 4; q++) (p)[le ? q : 3 - q] = (uint8_t)(v_ >> (8 * q)); } while (0)
    memcpy(t, le ? "II" : "MM", 2); W16(t + 2, 42); W32(t + 4, 8);
    int o = 8, data = 300;
    W16(t + o, 3); o += 2;                                   /* IFD0: Model, Orientation, Exif IFD */
    W16(t + o, 0x0110); W16(t + o + 2, 2); W32(t + o + 4, 12); W32(t + o + 8, data); o += 12;
    memcpy(t + data, "Canon EOS M", 12); data += 12;
    W16(t + o, 0x0112); W16(t + o + 2, 3); W32(t + o + 4, 1); W16(t + o + 8, orient); o += 12;
    W16(t + o, 0x8769); W16(t + o + 2, 4); W32(t + o + 4, 1); W32(t + o + 8, 50); o += 12;
    W32(t + o, 0);
    o = 50;
    W16(t + o, 9); o += 2;                                   /* Exif IFD, tags ascending */
    W16(t + o, 0x829A); W16(t + o + 2, 5); W32(t + o + 4, 1); W32(t + o + 8, data); o += 12;
    W32(t + data, 1); W32(t + data + 4, 320); data += 8;
    W16(t + o, 0x829D); W16(t + o + 2, 5); W32(t + o + 4, 1); W32(t + o + 8, data); o += 12;
    W32(t + data, 63); W32(t + data + 4, 10); data += 8;
    W16(t + o, 0x8827); W16(t + o + 2, 3); W32(t + o + 4, 1); W16(t + o + 8, 320); o += 12;
    W16(t + o, 0x9003); W16(t + o + 2, 2); W32(t + o + 4, 20); W32(t + o + 8, data); o += 12;
    memcpy(t + data, "2026:11:15 10:20:30", 20); data += 20;
    W16(t + o, 0x9286); W16(t + o + 2, 7); W32(t + o + 4, 16); W32(t + o + 8, data); o += 12;
    data += 16;
    W16(t + o, 0x920A); W16(t + o + 2, 5); W32(t + o + 4, 1); W32(t + o + 8, data); o += 12;
    W32(t + data, 45); W32(t + data + 4, 1); data += 8;
    W16(t + o, 0xA002); W16(t + o + 2, 3); W32(t + o + 4, 1); W16(t + o + 8, 5184); o += 12;
    if (ydim_long) { W16(t + o, 0xA003); W16(t + o + 2, 4); W32(t + o + 4, 1); W32(t + o + 8, 3456); }
    else { W16(t + o, 0xA003); W16(t + o + 2, 3); W32(t + o + 4, 1); W16(t + o + 8, 3456); }
    o += 12;
    W16(t + o, 0xA434); W16(t + o + 2, 2); W32(t + o + 4, 30); W32(t + o + 8, data); o += 12;
    memcpy(t + data, "EF-M15-45mm f/3.5-6.3 IS STM  ", 30); data += 30;
    W32(t + o, 0);
    return data;
    #undef W16
    #undef W32
}

static uint32_t rdv(const uint8_t *p, int n, int le)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[le ? n - 1 - i : i];
    return v;
}

/* a mid-grey UYVY picture, Y = 100 + (x + y) % 40 (so the stamp's change shows against a known value) */
static uint32_t *grey(int w, int h)
{
    uint32_t *p = malloc((size_t)w * h * 2);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w / 2; x++)
            p[y * (w / 2) + x] = (uint32_t)(100 + (2 * x + y) % 40) << 8 | (uint32_t)(100 + (2 * x + 1 + y) % 40) << 24;
    return p;
}

static struct sink2 { uint8_t *b; uint32_t n, cap; } sk;
static int out2(void *ctx, const uint8_t *p, uint32_t n)
{
    struct sink2 *s = ctx;
    if (s->n + n > s->cap) return -1;
    memcpy(s->b + s->n, p, n);
    s->n += n;
    return 0;
}

static void punits(void)
{
    /* layouts: every frame / info / orientation at the EOS M L size and at an odd size; all multiples of 16, the
       picture and the caption inside the canvas, apart, the caption under the shown picture */
    static const int sz[2][2] = { { 5184, 3456 }, { 4000, 2672 } }, ors[4] = { 1, 3, 6, 8 };
    int bad = 0;
    for (int s = 0; s < 2; s++)
        for (int fr = 0; fr < WF_PFR_N; fr++)
            for (int in = 0; in < WF_PIN_N; in++)
                for (int k = 0; k < 4; k++) {
                    struct wf_pf_lay L;
                    int iw = sz[s][0], ih = sz[s][1], o = ors[k], rot = o == 6 || o == 8;
                    int r = wf_pf_layout(&L, fr, in, iw, ih, o);
                    if (r != (fr != 0)) { bad++; printf("layout %d %d o%d: ret %d\n", fr, in, o, r); continue; }
                    if (!r) { bad += L.cw != iw || L.ch != ih || L.ix || L.iy || L.tw; continue; }
                    int ok = !(L.cw & 15) && !(L.ch & 15) && !(L.ix & 15) && !(L.iy & 15) && !(L.tx & 15) && !(L.ty & 15)
                          && !(L.tw & 15) && !(L.th & 15) && L.ix + iw <= L.cw && L.iy + ih <= L.ch
                          && (rot ? L.dw == L.ch && L.dh == L.cw : L.dw == L.cw && L.dh == L.ch)
                          && L.dw == (rot ? ih : iw) + 2 * L.bd && L.dh == (rot ? iw : ih) + L.bd + L.bb;
                    if (in) {
                        ok = ok && L.tw && L.tx + L.tw <= L.cw && L.ty + L.th <= L.ch
                             && (rot ? L.th == ih && L.tw == L.bb && (L.tx >= L.ix + iw || L.tx + L.tw <= L.ix)
                                     : L.tw == iw && L.th == L.bb && (L.ty >= L.iy + ih || L.ty + L.th <= L.iy));
                        /* the caption under the shown picture: o1 below, o3 above (turned), o6 right, o8 left */
                        ok = ok && (o == 1 ? L.ty == L.iy + ih : o == 3 ? L.ty == 0 && L.iy == L.bb
                                    : o == 6 ? L.tx == L.ix + iw : L.tx == 0 && L.ix == L.bb);
                    } else
                        ok = ok && !L.tw && L.bb == L.bd;
                    if (!ok) { bad++; printf("layout %dx%d frame %d info %d o%d: cw %d ch %d ix %d iy %d t %d,%d %dx%d\n",
                                             iw, ih, fr, in, o, L.cw, L.ch, L.ix, L.iy, L.tx, L.ty, L.tw, L.th); }
                }
    CHECK(bad == 0, "layouts: %d bad", bad);
    struct wf_pf_lay L;
    wf_pf_layout(&L, WF_PFR_WHITE, WF_PIN_ON, 5184, 3456, 1);
    CHECK(L.bd == 112 && L.bb == 576 && L.cw == 5408 && L.ch == 4144 && L.bg == 255, "white + info: bd %d bb %d %dx%d",
          L.bd, L.bb, L.cw, L.ch);
    wf_pf_layout(&L, WF_PFR_BLACK_W, WF_PIN_OFF, 5184, 3456, 6);
    CHECK(L.bd == 256 && L.cw == 5184 + 512 && L.ch == 3456 + 512 && L.bg == 12, "black wide o6: %dx%d", L.cw, L.ch);

    /* the shot's details: both byte orders, SHORT / LONG dimensions, the CR2's gaps */
    static uint8_t t[512];
    for (int le = 0; le < 2; le++) {
        int n = mk_tiff(t, le, 6, le);
        struct wf_dk_shot s;
        memset(&s, 0, sizeof s);
        int got = wf_dk_shot(t, (uint32_t)n, &s);
        CHECK(got == 255 && !strcmp(s.model, "Canon EOS M") && !strcmp(s.lens, "EF-M15-45mm f/3.5-6.3 IS STM")
              && s.fl[0] == 45 && s.fl[1] == 1 && s.fn[0] == 63 && s.fn[1] == 10 && s.et[0] == 1 && s.et[1] == 320
              && s.iso == 320 && s.orient == 6 && !strcmp(s.dt, "2026:11:15 10:20:30"),
              "shot le %d: got %x model '%s' lens '%s' fl %u/%u fn %u/%u et %u/%u iso %d o %d dt '%s'", le, got, s.model,
              s.lens, s.fl[0], s.fl[1], s.fn[0], s.fn[1], s.et[0], s.et[1], s.iso, s.orient, s.dt);
        CHECK(wf_dk_shot(t, (uint32_t)n, &s) == 0, "shot: nothing filled twice");
        char b[80];
        CHECK(wf_pf_line2(&s, b, sizeof b) == 4 && !strcmp(b, "45mm   f/6.3   1/320s   ISO 320"), "line 2 '%s'", b);
        /* dimensions in an APP1 around this TIFF */
        static uint8_t a[600];
        a[0] = 0xFF; a[1] = 0xE1; a[2] = (uint8_t)((n + 8) >> 8); a[3] = (uint8_t)(n + 8);
        memcpy(a + 4, "Exif\0\0", 6);
        memcpy(a + 10, t, n);
        CHECK(wf_dk_dims(a, (uint32_t)n + 10, 5408, 4144) == 0, "dims le %d", le);
        uint32_t i0 = rdv(a + 14, 4, le), ex = 50, cnt = rdv(a + 10 + ex, 2, le), xd = 0, yd = 0;
        (void)i0;
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t *e = a + 10 + ex + 2 + 12 * i;
            unsigned tag = rdv(e, 2, le), ty = rdv(e + 2, 2, le);
            if (tag == 0xA002) xd = ty == 3 ? rdv(e + 8, 2, le) : rdv(e + 8, 4, le);
            if (tag == 0xA003) yd = ty == 3 ? rdv(e + 8, 2, le) : rdv(e + 8, 4, le);
        }
        CHECK(xd == 5408 && yd == 4144, "dims le %d: %u x %u", le, xd, yd);
        CHECK(wf_dk_dims(a, (uint32_t)n + 10, 70000, 4144) == -1, "a SHORT cannot hold 70000");
    }
    struct wf_dk_shot s, c;
    memset(&s, 0, sizeof s);
    memset(&c, 0, sizeof c);
    strcpy(s.model, "Canon EOS M"); s.orient = 1;
    strcpy(c.model, "other"); strcpy(c.lens, "EF-M22mm f/2 STM"); c.iso = 1600; c.orient = 8;
    strcpy(c.dt, "2025:01:02 03:04:05"); c.fn[0] = 2; c.fn[1] = 1;
    wf_dk_shot_gaps(&s, &c);
    CHECK(!strcmp(s.model, "Canon EOS M") && !strcmp(s.lens, "EF-M22mm f/2 STM") && s.iso == 1600 && s.orient == 1
          && !strcmp(s.dt, "2025:01:02 03:04:05") && s.fn[1] == 1, "gaps: model '%s' lens '%s' iso %d o %d", s.model,
          s.lens, s.iso, s.orient);

    /* caption text: line 2 formats, the date formats, the look's items */
    static const struct { uint32_t fl[2], fn[2], et[2]; int iso; const char *want; } l2[] = {
        { { 15, 1 }, { 56, 10 }, { 1, 4 }, 100, "15mm   f/5.6   1/4s   ISO 100" },
        { { 45, 10 }, { 11, 1 }, { 3, 10 }, 0, "4.5mm   f/11   0.3s" },
        { { 0, 0 }, { 0, 0 }, { 25, 10 }, 6400, "2.5s   ISO 6400" },
        { { 22, 1 }, { 2, 1 }, { 30, 1 }, 0, "22mm   f/2   30s" },
        { { 0, 0 }, { 0, 0 }, { 10, 8000 }, 0, "1/800s" },
    };
    for (unsigned i = 0; i < sizeof l2 / sizeof l2[0]; i++) {
        char b[80];
        memset(&s, 0, sizeof s);
        memcpy(s.fl, l2[i].fl, 8); memcpy(s.fn, l2[i].fn, 8); memcpy(s.et, l2[i].et, 8); s.iso = l2[i].iso;
        wf_pf_line2(&s, b, sizeof b);
        CHECK(!strcmp(b, l2[i].want), "line 2 #%u '%s' want '%s'", i, b, l2[i].want);
    }
    memset(&s, 0, sizeof s);
    strcpy(s.dt, "2026:11:05 10:20:30");
    char b[40];
    CHECK(wf_pf_date(&s, WF_PDT_YMD, b, sizeof b) && !strcmp(b, "'26 11 05"), "YMD '%s'", b);
    CHECK(wf_pf_date(&s, WF_PDT_DMY, b, sizeof b) && !strcmp(b, "05 11 '26"), "DMY '%s'", b);
    CHECK(wf_pf_date(&s, WF_PDT_MDY, b, sizeof b) && !strcmp(b, "11 05 '26"), "MDY '%s'", b);
    CHECK(!wf_pf_date(&s, WF_PDT_OFF, b, sizeof b), "Off: no date");
    strcpy(s.dt, "2026:13:05 10:20:30");
    CHECK(!wf_pf_date(&s, WF_PDT_YMD, b, sizeof b), "month 13");
    strcpy(s.dt, "    :  :     :  :  ");
    CHECK(!wf_pf_date(&s, WF_PDT_YMD, b, sizeof b), "blank date (Canon's unset clock)");
    s.dt[0] = 0;
    CHECK(!wf_pf_date(&s, WF_PDT_YMD, b, sizeof b), "no date");
    char lk[200];
    const char *it[8];
    strcpy(lk, "WindyFilm: Portra 400 Cine | Print Cine | Strength 100% | Light Day | Grain Normal | Halation Film | "
               "Bloom Low | Black Mist 1/8");
    int ni = wf_pf_look_items(lk, it, 8);
    CHECK(ni == 5 && !strcmp(it[0], "Portra 400 Cine") && !strcmp(it[1], "Grain Normal") && !strcmp(it[2], "Halation Film")
          && !strcmp(it[3], "Bloom Low") && !strcmp(it[4], "Black Mist 1/8"), "look items %d '%s' '%s'", ni,
          ni > 0 ? it[0] : "", ni > 1 ? it[1] : "");
    strcpy(lk, "WindyFilm: Portra 400 | Print Scan | Strength 30% | Mist 1/4 | EV12");
    ni = wf_pf_look_items(lk, it, 8);
    CHECK(ni == 3 && !strcmp(it[0], "Portra 400") && !strcmp(it[1], "Print Scan") && !strcmp(it[2], "Mist 1/4"),
          "look items (scan) %d", ni);
    struct wf_pf_txt tx;
    struct wf_pf_cap cp;
    memset(&s, 0, sizeof s);
    strcpy(s.model, "Canon EOS M");
    wf_pf_caption(&s, "WindyFilm: Vision3 250D | Grain Fine", "ROLL 021 \x7f 07/24", &tx, &cp);
    CHECK(cp.n1 == 1 && !strcmp(cp.l1[0], "Canon EOS M") && cp.l2 && !cp.l2[0] && cp.n3 == 2 && cp.roll
          && !strcmp(cp.roll, "ROLL 021 \x7f 07/24"), "caption n1 %d n3 %d", cp.n1, cp.n3);

    /* the font pack (tools/dr_dark_check.sh makes WFPRINT.DAT in the work dir) */
    uint32_t pn = 0;
    uint8_t *pk = slurp("WFPRINT.DAT", WF_PF_MAXB + 1, &pn);
    struct wf_pf_font F[WF_PF_NFONT];
    if (pk && pn) {
        CHECK(pn <= WF_PF_MAXB && wf_pf_fonts(F, pk, pn) == 0, "WFPRINT.DAT %u B not read", pn);
        CHECK(F[0].px == 84 && F[1].px == 62 && wf_pf_textw(&F[0], "H") > 30, "fonts px %d %d", F[0].px, F[1].px);
        CHECK(wf_pf_textw(&F[1], "AV") < wf_pf_textw(&F[1], "A") + wf_pf_textw(&F[1], "V"), "kerning AV");
        pk[pn / 2] ^= 1;
        CHECK(wf_pf_fonts(F, pk, pn) == -1, "a changed byte is refused");
        pk[pn / 2] ^= 1;
        CHECK(wf_pf_fonts(F, pk, pn - 1) == -1, "a short pack is refused");
        /* the band: text drawn inside the shown picture's width, the frame colour elsewhere */
        wf_pf_fonts(F, pk, pn);
        for (int k = 0; k < 4; k++) {
            wf_pf_layout(&L, WF_PFR_WHITE, WF_PIN_ROLL, 5184, 3456, ors[k]);
            uint8_t *band = malloc(wf_pf_band_bytes(&L));
            char dr[200];
            memset(&s, 0, sizeof s);
            mk_tiff(t, 1, ors[k], 0);
            wf_dk_shot(t, 512, &s);
            wf_pf_caption(&s, "WindyFilm: Portra 400 | Print Cine | Grain Normal", "ROLL 021 \x7f 07/24", &tx, &cp);
            wf_pf_band(&L, F, &cp, band, dr, sizeof dr);
            int ink = 0, out = 0, rot = ors[k] == 6 || ors[k] == 8;
            for (int y = 0; y < L.th; y++)
                for (int x = 0; x < L.tw; x++)
                    if (band[y * L.tw + x] != 255) {
                        ink++;
                        int along = rot ? y : x;                 /* across the picture's shown width */
                        out += along < 0 || along >= (rot ? 3456 : 5184);
                    }
            CHECK(ink > 20000 && out == 0, "band o%d: %d inked px, %d outside", ors[k], ink, out);
            CHECK(!strcmp(dr, "Canon EOS M  /  EF-M15-45mm f/3.5-6.3 IS STM + ROLL 021 . 07/24 | 45mm   f/6.3   1/320s   "
                              "ISO 320 | Portra 400  .  Print Cine  .  Grain Normal"), "drawn o%d: '%s'", ors[k], dr);
            free(band);
        }
    } else
        printf("units: no WFPRINT.DAT here, font pack checks skipped\n");
    free(pk);

    /* the date stamp: only the shown picture's bottom right changes, orange (V > 0, U < 0), the rest byte for byte */
    for (int k = 0; k < 4; k++) {
        int w = 5184, h = 3456, o = ors[k];
        uint32_t *p = grey(w, h), *q = grey(w, h);
        const char *dt = "'26 11 15";
        uint32_t need = wf_pf_stamp_bytes(w, h, dt);
        uint8_t *m = malloc(need);
        CHECK(need > 0 && need < 1u << 20 && wf_pf_stamp(p, w * 2, w, h, o, dt, m, need) == 0, "stamp o%d (%u B)", o, need);
        int x0 = w, y0 = h, x1 = -1, y1 = -1, orange = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w / 2; x++)
                if (p[y * (w / 2) + x] != q[y * (w / 2) + x]) {
                    if (2 * x < x0) x0 = 2 * x;
                    if (2 * x + 1 > x1) x1 = 2 * x + 1;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                    int8_t u = (int8_t)(p[y * (w / 2) + x] & 0xFF), v = (int8_t)(p[y * (w / 2) + x] >> 16);
                    orange += v > 20 && u < -5;
                }
        /* shown: the bottom right 30 % x 12 % of the shown picture; stored per orientation */
        int sw = o == 6 || o == 8 ? h : w, sh = o == 6 || o == 8 ? w : h, ok;
        (void)sw; (void)sh;
        if (o == 1) ok = x0 > w * 6 / 10 && y0 > h * 85 / 100;
        else if (o == 3) ok = x1 < w * 4 / 10 && y1 < h * 15 / 100;
        else if (o == 6) ok = x0 > w * 85 / 100 && y1 < h * 4 / 10;   /* shown bottom right = stored top right */
        else ok = x1 < w * 15 / 100 && y0 > h * 6 / 10;               /* o8: stored bottom left */
        CHECK(x1 >= 0 && ok && orange > 500, "stamp o%d: changed %d,%d..%d,%d, %d orange", o, x0, y0, x1, y1, orange);
        CHECK(wf_pf_stamp(p, w * 2, w, h, o, "'26 1A 15", m, need) == -1, "bad text refused");
        CHECK(wf_pf_stamp(p, w * 2, w, h, o, dt, m, need - 1) == -1, "small map refused");
        free(m); free(p); free(q);
    }

    /* the plain path: frame Off hands the picture's own rows, and the encode is the same bytes */
    {
        int w = 640, h = 480;
        uint32_t *p = grey(w, h);
        wf_pf_layout(&L, WF_PFR_OFF, WF_PIN_ON, w, h, 6);
        const uint32_t *rows;
        int pitch, n, okr = 1;
        static uint32_t rb[640 * 16];
        for (int y = 0; y < h; y += 16) {
            n = wf_pf_band_rows(&L, y, p, w * 2, 0, rb, &rows, &pitch);
            okr &= rows == p + y * (w / 2) && pitch == w * 2 && n == 16;
        }
        CHECK(okr && L.cw == w && L.ch == h, "frame Off: the picture's own rows");
        uint8_t *e = malloc(wf_jpe_bytes());
        static uint8_t ob[16384], b1[400000], b2[400000];
        uint32_t n1, n2;
        sk.b = b1; sk.n = 0; sk.cap = sizeof b1;
        int rc = wf_jpe_begin((struct wf_jpe *)e, w, h, 95, 0, 0, ob, sizeof ob, out2, &sk);
        for (int y = 0; rc == 0 && y < h; y += 16) rc = wf_jpe_rows((struct wf_jpe *)e, p + y * (w / 2), w * 2, 16);
        if (rc == 0) rc = wf_jpe_end((struct wf_jpe *)e);
        n1 = sk.n;
        sk.b = b2; sk.n = 0; sk.cap = sizeof b2;
        rc |= wf_jpe_begin((struct wf_jpe *)e, L.cw, L.ch, 95, 0, 0, ob, sizeof ob, out2, &sk);
        for (int y = 0; rc == 0 && y < L.ch; y += 16) {
            n = wf_pf_band_rows(&L, y, p, w * 2, 0, rb, &rows, &pitch);
            rc = wf_jpe_rows((struct wf_jpe *)e, rows, pitch, n);
        }
        if (rc == 0) rc = wf_jpe_end((struct wf_jpe *)e);
        n2 = sk.n;
        CHECK(rc == 0 && n1 == n2 && !memcmp(b1, b2, n1), "frame Off encode: %u vs %u bytes", n1, n2);
        /* a frame: the canvas rows hold the picture's words unchanged at (ix, iy) */
        wf_pf_layout(&L, WF_PFR_BLACK, WF_PIN_OFF, w, h, 1);
        static uint32_t rb2[2048 * 16];
        int same = 1;
        for (int y = 0; y < L.ch; y += 16) {
            n = wf_pf_band_rows(&L, y, p, w * 2, 0, rb2, &rows, &pitch);
            for (int r2 = 0; r2 < n; r2++) {
                int yy = y + r2 - L.iy;
                const uint32_t *row = rows + r2 * (pitch / 4);
                if (yy >= 0 && yy < h) same &= !memcmp(row + L.ix / 2, p + yy * (w / 2), w * 2) && row[0] == (12u << 8 | 12u << 24);
                else same &= row[L.cw / 4] == (12u << 8 | 12u << 24);
            }
        }
        CHECK(same, "frame rows: the picture unchanged, the border black");
        free(e); free(p);
    }
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

/* the print finishes of a finish run (0 = the frame mode) */
struct fin { const char *tag, *pack, *look, *roll; int frame, info, date; };

/* wf_dark.c dk_finish on the host: the date stamp into frm, the canvas layout, the caption band, the APP1's size */
static void finish(const struct fin *fn, const struct wf_dk_shot *cs, uint8_t *app1, uint32_t al, uint32_t *frm, int W,
                   int H, struct wf_pf_lay *L, uint8_t **band, uint32_t **rowbuf)
{
    struct wf_dk_shot s;
    memset(&s, 0, sizeof s);
    if (app1 && al > 10)
        wf_dk_shot(app1 + 10, al - 10, &s);
    int orient = s.orient ? s.orient : 1;
    wf_dk_shot_gaps(&s, cs);
    if (fn->date) {
        char b[24];
        if (!wf_pf_date(&s, fn->date, b, sizeof b))
            printf("finish: no date in the shot: no date stamp\n");
        else {
            uint32_t need = wf_pf_stamp_bytes(W, H, b);
            uint8_t *m = malloc(need);
            CHECK(wf_pf_stamp(frm, W * 2, W, H, orient, b, m, need) == 0, "stamp %s", b);
            printf("finish: date stamp %s o%d (map %u B)\n", b, orient, need);
            free(m);
        }
    }
    if (!wf_pf_layout(L, fn->frame, fn->info, W, H, orient))
        return;
    *band = malloc(wf_pf_band_bytes(L) + 4);
    *rowbuf = malloc((size_t)L->cw * 2 * 16);
    struct wf_pf_font F[WF_PF_NFONT];
    uint32_t pn = 0;
    uint8_t *pk = L->tw && fn->pack ? slurp(fn->pack, WF_PF_MAXB + 1, &pn) : 0;
    int fok = pk && pn <= WF_PF_MAXB && wf_pf_fonts(F, pk, pn) == 0;
    CHECK(!L->tw || fok, "font pack %s", fn->pack ? fn->pack : "-");
    struct wf_pf_txt tx;
    struct wf_pf_cap c;
    char dr[200];
    wf_pf_caption(&s, fn->look, fn->roll, &tx, &c);
    wf_pf_band(L, fok ? F : 0, &c, *band, dr, sizeof dr);
    int dims = app1 ? wf_dk_dims(app1, al, L->cw, L->ch) : -1;
    CHECK(!app1 || dims == 0, "Exif pixel size not set");
    printf("finish: frame %d info %d o%d, caption \"%s\"\n", fn->frame, fn->info, orient, fok ? dr : "");
    free(pk);
}

static void frame(const char *cr2, const char *jpg, const char *od, const char *juyvy, const struct fin *fn)
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
    struct wf_dk_shot cs;                                    /* the CR2's own details (the finishes' gaps) */
    memset(&cs, 0, sizeof cs);
    wf_dk_shot(head, nh, &cs);
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
    printf("frame %s: ISO %d EV8 %d, %d steps, black %d white %d%s, WB %s R/G %.3f B/G %.3f, trim %+d/10 EV, shoulder %d\n", cr2,
           inf.iso, wf_dk_ev8(inf.iso, inf.et_n, inf.et_d, inf.fn_n, inf.fn_d), steps, j.black, j.white,
           lw ? " (pile-up)" : " (table)", as ? "as-shot" : "daylight",
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
    struct wf_pf_lay L;                                      /* the canvas (wf_dark.c dk_frame's DRY phase) */
    uint8_t *band = 0;
    uint32_t *rowbuf = 0;
    wf_pf_layout(&L, WF_PFR_OFF, 0, W, H, 1);
    if (fn && (fn->frame || fn->date))
        finish(fn, &cs, app1, al, frm, W, H, &L, &band, &rowbuf);
    const char *b = strrchr(cr2, '/');
    b = b ? b + 1 : cr2;
    char p[600], name[64];
    if (fn)
        snprintf(name, sizeof name, "%.8s_%s", b, fn->tag);
    else
        snprintf(name, sizeof name, "%.8s", b);
    snprintf(p, sizeof p, "%s/%s.JPG", od, name);
    struct sink s = { fopen(p, "wb"), 0 };
    CHECK(s.f != 0, "create %s", p);
    static uint8_t ob[64 * 1024];
    uint8_t *e = malloc(wf_jpe_bytes());
    rc = wf_jpe_begin((struct wf_jpe *)e, L.cw, L.ch, 95, app1, (int)al, ob, sizeof ob, out, &s);
    for (int y = 0; rc == 0 && y < L.ch; y += 16) {
        const uint32_t *rows;
        int pitch, n = wf_pf_band_rows(&L, y, frm, W * 2, band, rowbuf, &rows, &pitch);
        rc = wf_jpe_rows((struct wf_jpe *)e, rows, pitch, n);
    }
    if (rc == 0) rc = wf_jpe_end((struct wf_jpe *)e);
    fclose(s.f);
    CHECK(rc == 0, "encode %d", rc);
    printf("print %s: %u bytes, APP1 %u bytes\n", p, s.n, al);
    printf("canvas %d %d %d %d %d\n", L.cw, L.ch, L.ix, L.iy, L.orient);
    free(band);
    free(rowbuf);
    snprintf(p, sizeof p, "%s/%s.dev.uyvy", od, name);
    FILE *u = fopen(p, "wb");
    if (u) { fwrite(frm, 2, (size_t)W * H, u); fclose(u); }
    snprintf(p, sizeof p, "%s/%s.print.l8", od, name);
    if ((u = fopen(p, "wb"))) { fwrite(pr, 1, sizeof pr, u); fclose(u); }
    snprintf(p, sizeof p, "%s/%s.wire.l8", od, name);
    if ((u = fopen(p, "wb"))) { fwrite(wi, 1, sizeof wi, u); fclose(u); }
    free(e); free(jh); free(frm); free(scr); free(raw);
}

/* ------------------------------------------------------------------ several frames x several develops (the look sweep) */
/* devset LIST OUTDIR: the Darkroom's develop (dk_frame's steps 1-2: decode, levels, as-shot WB + scene scale + ISO trim,
 * the develop tables of a look) of several CR2s through several tables, for the look sweep (tools/look_sweep.sh). LIST:
 *   F <name> <cr2>                                          a frame, decoded once and kept
 *   D <tag> <srgb|cube|disp> <curve> <flut> <cube|-> <1|3>  a develop, as wf_dark.c builds it for a look (cube: LUT EI 0,
 *                                                           auto exposure when flut; disp: Highlights / Shadows 0), then
 *        every frame through it: 1 = OUTDIR/<name>_<tag>.uyvy (5184 x 3456), 3 = OUTDIR/<name>_<tag>.ppm (a third, 3 x 3
 *        box, the JFIF matrix) */
struct dsf { char name[64]; struct wf_cr2_info inf; uint8_t *raw; struct wf_dev_job j; };

static uint8_t *decode_cr2(const char *cr2, struct wf_cr2_info *inf)
{
    FILE *f = fopen(cr2, "rb");
    CHECK(f != 0, "open %s", cr2);
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    uint32_t fsize = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    static uint8_t head[WF_CR2_HEAD];
    uint32_t nh = (uint32_t)fread(head, 1, sizeof head, f);
    int rc = wf_cr2_parse(head, nh, fsize, inf);
    uint8_t lj[WF_CR2_LJHEAD];
    if (rc == 0) {
        fseek(f, inf->strip_off, SEEK_SET);
        rc = wf_cr2_parse_ljpeg(lj, (uint32_t)fread(lj, 1, sizeof lj, f), inf);
    }
    CHECK(rc == 0, "%s: parse %s", cr2, wf_cr2_err(rc));
    uint8_t *raw = rc ? 0 : malloc((size_t)inf->pitch * inf->h + 4096), *ib = rc ? 0 : malloc(256 * 1024);
    static struct wf_cr2_dec d;
    if (raw && ib) {
        fseek(f, inf->strip_off, SEEK_SET);
        rc = wf_cr2_dec_begin(&d, inf, raw, inf->pitch, ib, 256 * 1024, rd, f);
        while (rc >= 0 && (rc = wf_cr2_dec_step(&d, 64)) > 0)
            ;
        CHECK(rc == 0, "%s: decode %s", cr2, wf_cr2_err(rc));
    }
    fclose(f);
    free(ib);
    if (rc) { free(raw); return 0; }
    return raw;
}

static void put_third(const char *p, const uint32_t *frm, int W, int H)
{
    FILE *f = fopen(p, "wb");
    CHECK(f != 0, "create %s", p);
    if (!f) return;
    int ow = W / 3, oh = H / 3;
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    uint8_t *row = malloc((size_t)ow * 3);
    for (int oy = 0; oy < oh; oy++) {
        for (int ox = 0; ox < ow; ox++) {
            int sy = 0, su = 0, sv = 0;
            for (int y = 3 * oy; y < 3 * oy + 3; y++)
                for (int x = 3 * ox; x < 3 * ox + 3; x++) {
                    uint32_t q = frm[(size_t)y * (W / 2) + x / 2];
                    sy += (int)(x & 1 ? q >> 24 : q >> 8 & 255);
                    su += (int8_t)(q & 255);
                    sv += (int8_t)(q >> 16 & 255);
                }
            int c[3] = { 1000 * sy + 1402 * sv, 1000 * sy - 344 * su - 714 * sv, 1000 * sy + 1772 * su };
            for (int k = 0; k < 3; k++) {
                int q = (c[k] + 4500) / 9000;
                row[3 * ox + k] = (uint8_t)(q < 0 ? 0 : q > 255 ? 255 : q);
            }
        }
        fwrite(row, 1, (size_t)ow * 3, f);
    }
    free(row);
    fclose(f);
}

static void devset(const char *list, const char *od)
{
    enum { NF = 16 };
    static struct dsf fr[NF];
    int nf = 0, W = 5184, H = 3456;
    void *scr = malloc(wf_dev_scratch_bytes(W, H));
    uint32_t *frm = malloc((size_t)W * H * 2);
    static struct wf_dev_tabs T;
    FILE *l = fopen(list, "r");
    CHECK(l != 0, "open %s", list);
    char ln[1024];
    while (l && fgets(ln, sizeof ln, l)) {
        char a[64], b[512], c[64], cube[512];
        int curve = 0, flut = 0, down = 3;
        if (sscanf(ln, "F %63s %511[^\n]", a, b) == 2 && nf < NF) {
            struct dsf *s = &fr[nf];
            snprintf(s->name, sizeof s->name, "%s", a);
            if (!(s->raw = decode_cr2(b, &s->inf)))
                continue;
            struct wf_dev_job *j = &s->j;
            memset(j, 0, sizeof *j);
            j->raw = s->raw; j->pitch = s->inf.pitch;
            j->ax0 = 72; j->ay0 = 52; j->ax1 = s->inf.w; j->ay1 = s->inf.h;
            j->x0 = 84; j->y0 = 64; j->w = W; j->h = H; j->cfa = 0;
            int lb = 0, lw = 0;
            wf_dev_levels(scr, j, 16, 64, &lb, &lw);
            j->black = lb ? lb : 2048;
            j->white = lw ? lw : wf_dk_white(WF_DK_EOSM, s->inf.iso);
            int as = wf_dk_scales(WF_DK_EOSM, s->inf.wb, j->black, j->white, s->inf.iso, j->wb_q8);
            printf("frame %s: ISO %d %u/%u s f/%u/%u = EV8 %d, black %d white %d, WB %s, trim %+d/10 EV\n", a, s->inf.iso,
                   s->inf.et_n, s->inf.et_d, s->inf.fn_n, s->inf.fn_d,
                   wf_dk_ev8(s->inf.iso, s->inf.et_n, s->inf.et_d, s->inf.fn_n, s->inf.fn_d), j->black, j->white,
                   as ? "as-shot" : "daylight", wf_dk_trim10(WF_DK_EOSM, s->inf.iso));
            nf++;
        } else if (sscanf(ln, "D %63s %63s %d %d %511s %d", a, c, &curve, &flut, cube, &down) == 6) {
            int src = !strcmp(c, "srgb") ? WF_DEV_SRGB : !strcmp(c, "cube") ? WF_DEV_CUBE : !strcmp(c, "disp") ? WF_DEV_DISP : 0;
            const char *cf = strcmp(cube, "-") ? cube : 0;
            struct wf_ll_info li;
            int rc = src == WF_DEV_DISP ? wf_dev_build_disp(&T, WF_DEV_EOSM, cf, 0, 0, 0, &li)
                   : src ? wf_dev_build(&T, src, src == WF_DEV_SRGB ? 0 : curve, WF_DEV_EOSM, cf, 0, src == WF_DEV_CUBE && flut != 0, &li)
                   : -1;
            CHECK(rc == 0, "develop %s: build %s curve %d %s: %d", a, c, curve, cube, rc);
            if (rc)
                continue;
            for (int i = 0; i < nf; i++) {
                struct wf_dev_job j = fr[i].j;
                j.roll = src != WF_DEV_LOG;
                struct wf_dev_stat st;
                memset(&st, 0, sizeof st);
                rc = wf_dev_prepare(scr, &T, &j, &st);
                CHECK(rc == 0, "%s %s: prepare %d", fr[i].name, a, rc);
                if (rc)
                    continue;
                for (int y = 0; y < H; y += 64)
                    wf_dev_rows(scr, y, y + 64 < H ? y + 64 : H, frm + y * (W / 2), W * 2);
                char p[700];
                int pn = snprintf(p, sizeof p, "%s/%s_%s.%s", od, fr[i].name, a, down == 1 ? "uyvy" : "ppm");
                CHECK(pn < (int)sizeof p, "%s %s: output path too long", fr[i].name, a);
                if (pn >= (int)sizeof p)
                    continue;
                if (down == 1) {
                    FILE *u = fopen(p, "wb");
                    CHECK(u && fwrite(frm, 2, (size_t)W * H, u) == (size_t)W * H, "write %s", p);
                    if (u) fclose(u);
                } else {
                    put_third(p, frm, W, H);
                }
                printf("develop %s %s: %s\n", fr[i].name, a, p);
            }
        }
    }
    if (l) fclose(l);
    for (int i = 0; i < nf; i++)
        free(fr[i].raw);
    free(frm); free(scr);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "units")) {
        units();
        punits();
    } else if (argc >= 5 && !strcmp(argv[1], "frame"))
        frame(argv[2], strcmp(argv[3], "-") ? argv[3] : 0, argv[4], argc > 5 ? argv[5] : 0, 0);
    else if (argc >= 12 && !strcmp(argv[1], "finish")) {
        struct fin fn = { argv[5], strcmp(argv[9], "-") ? argv[9] : 0, argv[10], strcmp(argv[11], "-") ? argv[11] : 0,
                          atoi(argv[6]), atoi(argv[7]), atoi(argv[8]) };
        frame(argv[2], strcmp(argv[3], "-") ? argv[3] : 0, argv[4], 0, &fn);
    } else if (argc >= 4 && !strcmp(argv[1], "devset"))
        devset(argv[2], argv[3]);
    else {
        fprintf(stderr, "usage: wf_dark_test units | frame CR2 JPG|- OUTDIR [JPEG.UYVY]\n"
                        "       | finish CR2 JPG|- OUTDIR TAG FRAME INFO DATE PACK|- LOOK ROLL|-\n"
                        "       | devset LIST OUTDIR\n");
        return 2;
    }
    printf("RESULT %d / %d\n", pass, fail);
    return fail ? 1 : 0;
}
