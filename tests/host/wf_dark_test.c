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
 *                          the font pack WFPRINT.DAT when it is in the work dir, the date stamp, plain-path identity),
 *                          the logo (pack, caption line 1, placement, colours, orientations) and the Exif thumbnail
 *   wf_dark_test finish CR2 JPG OUTDIR TAG FRAME INFO DATE PACK|- LOOK ROLL|- [LOGO|- [MODEL]]
 *     the frame's print as the darkroom makes it with the print finishes (wf_dark.c dk_finish): FRAME / INFO / DATE =
 *     WF_PFR_* / WF_PIN_* / WF_PDT_*, LOOK = the comment text line 3 comes from, ROLL = the roll text right of line 1,
 *     LOGO = a WFLOGO.DAT (tools/mk_wflogo.py), MODEL = another camera's name for the caption and the logo (a preview).
 *     Every print gets its Exif thumbnail as the darkroom's (wf_dark.c dk_xthumb).
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
static int dk_cam = WF_DK_EOSM;                       /* env WF_DK_CAM=5D2: the 5D2 frame (wf_dk_geom) */
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

/* ------------------------------------------------------------------ the logo (owner 11-16) and the Exif thumbnail */
static uint32_t fnv32(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
/* a logo pack in tools/mk_wflogo.py's format: a 200 x 40 wordmark keyed maker (its letters 4..36, a full box x 10..189),
   and when model != 0 a 120 x 24 lettering keyed model (letters 2..20, box x 5..114). Returns its size. */
static uint32_t mk_logo(uint8_t *b, const char *maker, const char *model)
{
    int ni = model ? 2 : 1;
    uint32_t off = 16 + ni * 24, o = off;
    static const int dim[2][6] = { { 200, 40, 4, 36, 10, 190 }, { 120, 24, 2, 20, 5, 115 } };
    memset(b, 0, 16 + 48 + 200 * 40 + 120 * 24 + 64);
    for (int k = 0; k < ni; k++) {
        const int *d = dim[k];
        uint8_t *e = b + 16 + k * 24;
        e[0] = (uint8_t)k;
        e[1] = 8;
        put16(e + 2, d[0]); put16(e + 4, d[1]); put16(e + 6, d[2]); put16(e + 8, d[3]);
        put32(e + 12, o);
        for (int y = d[2]; y < d[3]; y++)
            memset(b + o + y * d[0] + d[4], 255, d[5] - d[4]);
        o += d[0] * d[1];
    }
    for (int k = 0; k < ni; k++) {
        const char *key = k ? model : maker;
        put32(b + 16 + k * 24 + 16, o);
        memcpy(b + o, key, strlen(key) + 1);
        o += strlen(key) + 1;
    }
    memcpy(b, "WFLG", 4);
    put16(b + 4, 1);
    put16(b + 6, ni);
    put32(b + 8, o);
    put32(b + 12, fnv32(b + 16, o - 16));
    return o;
}
/* an APP1 with IFD0 (Orientation) and IFD1 (JPEGInterchangeFormat / Length): a 100-byte "thumbnail" at tn, then pad
   bytes (tail: the last one set). Returns the APP1's length */
static uint32_t mk_xapp1(uint8_t *a, uint32_t tn, uint32_t pad, int tail)
{
    uint32_t n = 10 + tn + 100 + pad;
    memset(a, 0, n);
    a[0] = 0xFF; a[1] = 0xE1; a[2] = (uint8_t)((n - 2) >> 8); a[3] = (uint8_t)(n - 2);
    memcpy(a + 4, "Exif\0\0", 6);
    uint8_t *t = a + 10;
    memcpy(t, "II", 2); put16(t + 2, 42); put32(t + 4, 8);
    put16(t + 8, 1);                                         /* IFD0: Orientation 1, next = IFD1 at 26 */
    put16(t + 10, 0x0112); put16(t + 12, 3); put32(t + 14, 1); put16(t + 18, 1);
    put32(t + 22, 26);
    put16(t + 26, 2);                                        /* IFD1 */
    put16(t + 28, 0x0201); put16(t + 30, 4); put32(t + 32, 1); put32(t + 36, tn);
    put16(t + 40, 0x0202); put16(t + 42, 4); put32(t + 44, 1); put32(t + 48, 100);
    put32(t + 52, 0);
    t[tn] = 0xFF; t[tn + 1] = 0xD8;
    for (int i = 2; i < 98; i++) t[tn + i] = (uint8_t)(0x40 + i);
    t[tn + 98] = 0xFF; t[tn + 99] = 0xD9;
    if (tail && pad) t[tn + 100 + pad - 1] = 0x55;
    return n;
}
/* the canvas pixel (shown position u, v) of L as stored: Y, U, V of the rows wf_pf_band_rows gives */
static void pix_at(const struct wf_pf_lay *L, const uint32_t *img, const uint8_t *band, uint32_t *rb, int x, int y,
                   int *Y, int *U, int *V)
{
    const uint32_t *rows;
    int pitch, y0 = y & ~15;
    wf_pf_band_rows(L, y0, img, L->iw * 2, band, rb, &rows, &pitch);
    uint32_t q = rows[(y - y0) * (pitch / 4) + x / 2];
    *Y = (int)((q >> (x & 1 ? 24 : 8)) & 0xFF);
    *U = (int8_t)(q & 0xFF);
    *V = (int8_t)((q >> 16) & 0xFF);
}

static void lunits(void)
{
    /* the pack: parsed for a Canon model with its lettering, a Canon model without one, refused for another maker /
       no model (no logo, still 0), refused when changed, short or not a pack */
    static uint8_t pk[16 + 48 + 200 * 40 + 120 * 24 + 64];
    uint32_t n = mk_logo(pk, "Canon", "Canon EOS M");
    struct wf_pf_logo g;
    CHECK(wf_pf_logo(&g, pk, n, "Canon EOS M") == 0 && g.wm.a && g.lt.a && g.wm.w == 200 && g.lt.h == 24
          && !strcmp(g.maker, "Canon"), "logo pack: EOS M wordmark + lettering");
    CHECK(wf_pf_logo(&g, pk, n, "Canon EOS 6D") == 0 && g.wm.a && !g.lt.a, "logo pack: another Canon, no lettering");
    CHECK(wf_pf_logo(&g, pk, n, "NIKON Z 6") == 0 && !g.wm.a && !g.lt.a, "logo pack: another maker, no logo");
    CHECK(wf_pf_logo(&g, pk, n, "Canonet") == 0 && !g.wm.a, "logo pack: 'Canonet' is not Canon's");
    CHECK(wf_pf_logo(&g, pk, n, 0) == 0 && !g.wm.a, "logo pack: no model, no logo");
    pk[n / 2] ^= 1;
    CHECK(wf_pf_logo(&g, pk, n, "Canon EOS M") == -1, "logo pack: a changed byte is refused");
    pk[n / 2] ^= 1;
    CHECK(wf_pf_logo(&g, pk, n - 1, "Canon EOS M") == -1 && wf_pf_logo(&g, pk, 20, "Canon EOS M") == -1,
          "logo pack: a short pack is refused");
    pk[0] = 'X';
    CHECK(wf_pf_logo(&g, pk, n, "Canon EOS M") == -1, "logo pack: not a pack");
    pk[0] = 'W';

    /* the caption's line 1: the lettering names the model (dropped), else the wordmark names the maker ("EOS 6D") */
    struct wf_dk_shot s;
    static struct wf_pf_txt tx;
    struct wf_pf_cap c;
    memset(&s, 0, sizeof s);
    strcpy(s.model, "Canon EOS M");
    strcpy(s.lens, "EF-M22mm f/2 STM");
    wf_pf_logo(&g, pk, n, s.model);
    wf_pf_caption(&s, 0, 0, &tx, &c);
    wf_pf_cap_logo(&tx, &c, &g);
    CHECK(c.n1 == 1 && !strcmp(c.l1[0], "EF-M22mm f/2 STM"), "cap_logo with lettering: n1 %d '%s'", c.n1, c.l1[0]);
    strcpy(s.model, "Canon EOS 6D");
    wf_pf_logo(&g, pk, n, s.model);
    wf_pf_caption(&s, 0, 0, &tx, &c);
    wf_pf_cap_logo(&tx, &c, &g);
    CHECK(c.n1 == 2 && !strcmp(c.l1[0], "EOS 6D"), "cap_logo without lettering: '%s'", c.l1[0]);

    /* the bottom border (no caption): wordmark + lettering side by side, centred, inside the border, in every orientation;
       the stored rects mirror each other (o1 / o3, o6 / o8); the colours: light on black, Canon's red on white */
    int W = 1296, H = 864;                                   /* a quarter of the EOS M frame */
    uint32_t *img = grey(W, H);
    static uint32_t rb[2048 * 16];
    static const int ors[4] = { 1, 3, 6, 8 };
    struct wf_pf_ink K[4][2];
    wf_pf_logo(&g, pk, n, "Canon EOS M");
    for (int fk = 0; fk < 2; fk++)
        for (int k = 0; k < 4; k++) {
            struct wf_pf_lay L;
            struct wf_pf_lpos P;
            memset(&c, 0, sizeof c);
            wf_pf_layout(&L, fk ? WF_PFR_WHITE_W : WF_PFR_BLACK_W, WF_PIN_OFF, W, H, ors[k]);
            uint32_t need = wf_pf_logo_place(&L, 0, &g, &c, &P);
            uint8_t *mem = malloc(need + 4);
            int ok = P.n == 2 && need > 0 && wf_pf_logo_maps(&L, &P, mem, need) == 0 && L.nink == 2 && !c.rsv;
            int dw0 = L.dw - 2 * L.bd, top = L.bd + (L.dh - L.bd - L.bb);
            ok = ok && P.v[0] >= top && P.v[0] + P.h[0] <= L.dh && P.v[1] >= top && P.v[1] + P.h[1] <= L.dh
                 && abs((P.u[0] - L.bd) - (L.bd + dw0 - P.u[1] - P.w[1])) <= 1 && P.u[0] + P.w[0] < P.u[1];
            for (int i = 0; i < 2 && ok; i++) {
                K[k][i] = L.ink[i];
                ok = L.ink[i].x >= 0 && L.ink[i].y >= 0 && L.ink[i].x + L.ink[i].w <= L.cw && L.ink[i].y + L.ink[i].h <= L.ch;
            }
            CHECK(ok, "border logo %s o%d: n %d need %u, wm %d,%d %dx%d, lt %d,%d %dx%d", fk ? "white" : "black", ors[k], P.n,
                  need, P.u[0], P.v[0], P.w[0], P.h[0], P.u[1], P.v[1], P.w[1], P.h[1]);
            /* the wordmark's middle: full cover = the ink's colour, the lettering's = the frame's caption grey */
            int Y, U, V, mx = L.ink[0].x + L.ink[0].w / 2, my = L.ink[0].y + L.ink[0].h / 2;
            pix_at(&L, img, 0, rb, mx & ~1, my, &Y, &U, &V);
            CHECK(fk ? Y == 76 && U == -24 && V == 82 : Y == 228 && U == 0 && V == 0, "border logo %s o%d: wordmark Y %d U %d V %d",
                  fk ? "white" : "black", ors[k], Y, U, V);
            pix_at(&L, img, 0, rb, (L.ink[1].x + L.ink[1].w / 2) & ~1, L.ink[1].y + L.ink[1].h / 2, &Y, &U, &V);
            CHECK(Y == L.fg1 && U == 0 && V == 0, "border logo o%d: lettering Y %d (want %d)", ors[k], Y, L.fg1);
            /* outside the inks: the frame as before */
            pix_at(&L, img, 0, rb, 0, L.ch - 2, &Y, &U, &V);
            CHECK(Y == L.bg, "border logo o%d: frame corner Y %d", ors[k], Y);
            free(mem);
        }
    {
        struct wf_pf_lay L;
        wf_pf_layout(&L, WF_PFR_WHITE_W, WF_PIN_OFF, W, H, 1);
        CHECK(K[1][0].x == L.cw - K[0][0].x - K[0][0].w && K[1][0].y == L.ch - K[0][0].y - K[0][0].h
              && K[1][0].w == K[0][0].w && K[2][0].w == K[3][0].w && K[2][0].h == K[0][0].w
              && K[3][0].x == L.cw - K[2][0].x - K[2][0].w && K[3][0].y == L.ch - K[2][0].y - K[2][0].h,
              "border logo: the stored rects mirror (o1 %d,%d o3 %d,%d o6 %d,%d o8 %d,%d)", K[0][0].x, K[0][0].y,
              K[1][0].x, K[1][0].y, K[2][0].x, K[2][0].y, K[3][0].x, K[3][0].y);
    }
    /* another maker's shot: nothing placed, the caption untouched */
    {
        struct wf_pf_lay L;
        struct wf_pf_lpos P;
        wf_pf_logo(&g, pk, n, "NIKON Z 6");
        wf_pf_layout(&L, WF_PFR_WHITE, WF_PIN_OFF, W, H, 1);
        memset(&c, 0, sizeof c);
        CHECK(wf_pf_logo_place(&L, 0, &g, &c, &P) == 0 && P.n == 0 && !c.rsv && !c.roll3, "another maker: no logo");
    }
    /* the caption band with the fonts: right of line 1-2, the roll moved to line 3, line 1 / 2 kept short of it */
    uint32_t pn = 0;
    uint8_t *fp = slurp("WFPRINT.DAT", WF_PF_MAXB + 1, &pn);
    struct wf_pf_font F[WF_PF_NFONT];
    if (fp && pn && wf_pf_fonts(F, fp, pn) == 0) {
        wf_pf_logo(&g, pk, n, "Canon EOS M");
        for (int k = 0; k < 4; k++) {
            struct wf_pf_lay L;
            struct wf_pf_lpos P;
            wf_pf_layout(&L, WF_PFR_WHITE, WF_PIN_ROLL, 5184, 3456, ors[k]);
            memset(&s, 0, sizeof s);
            strcpy(s.model, "Canon EOS M");
            strcpy(s.lens, "EF-M15-45mm f/3.5-6.3 IS STM");
            wf_pf_caption(&s, "WindyFilm: Portra 400 | Print Cine | Grain Normal", "ROLL 021 \x7f 07/24", &tx, &c);
            uint32_t need = wf_pf_logo_place(&L, F, &g, &c, &P);
            int dw0 = L.dw - 2 * L.bd;
            CHECK(P.n == 2 && need > 0 && need < 64 * 1024 && c.rsv > P.w[0] && c.roll3 == 1
                  && P.u[0] + P.w[0] == L.bd + dw0 && P.u[1] + P.w[1] == L.bd + dw0 && P.w[1] <= P.w[0]
                  && P.v[0] >= L.bd + (L.dh - L.bd - L.bb) && P.v[1] > P.v[0],
                  "caption logo o%d: n %d need %u rsv %d, wm %d,%d %dx%d, lt %d,%d %dx%d", ors[k], P.n, need, c.rsv, P.u[0],
                  P.v[0], P.w[0], P.h[0], P.u[1], P.v[1], P.w[1], P.h[1]);
            wf_pf_cap_logo(&tx, &c, &g);
            uint8_t *band = malloc(wf_pf_band_bytes(&L));
            char dr[200];
            wf_pf_band(&L, F, &c, band, dr, sizeof dr);
            CHECK(!strncmp(dr, "EF-M15-45mm", 11) && strstr(dr, " + ROLL 021"), "caption logo o%d: drawn '%s'", ors[k], dr);
            free(band);
        }
    } else
        printf("units: no WFPRINT.DAT here, the caption logo checks skipped\n");
    free(fp);
    free(img);

    /* the Exif thumbnail's slot: to the APP1's end when only zeros follow (Canon's), else its own length */
    static uint8_t a[16384], a0[16384];
    uint32_t al = mk_xapp1(a, 64, 3000, 0), off = 0, room = 0;
    CHECK(wf_dk_xthumb_slot(a, al, &off, &room) == 0 && off == 10 + 64 && room == 3100, "slot: off %u room %u", off, room);
    al = mk_xapp1(a, 64, 3000, 1);
    CHECK(wf_dk_xthumb_slot(a, al, &off, &room) == 0 && room == 100, "slot with a byte after it: room %u", room);
    al = mk_xapp1(a, 64, 2, 1);
    CHECK(wf_dk_xthumb_slot(a, al, &off, &room) == 0 && room == 102, "slot with 2 bytes after it: room %u", room);
    /* a thumbnail of ours into it: our JFIF APP0 dropped, IFD1's length set, the rest zero; too big = left as it was */
    al = mk_xapp1(a, 64, 12000, 0);
    uint32_t *tp = malloc(WF_DK_XBYTES);
    for (int i = 0; i < WF_DK_XW / 2 * WF_DK_XH; i++)
        tp[i] = 0x00800080u | (uint32_t)(i & 63) << 8 | (uint32_t)(i & 63) << 24;
    static uint8_t jb[64 * 1024], ob[8192];
    sk.b = jb; sk.n = 0; sk.cap = sizeof jb;
    uint8_t *e = malloc(wf_jpe_bytes());
    int rc = wf_jpe_begin((struct wf_jpe *)e, WF_DK_XW, WF_DK_XH, 50, 0, 0, ob, sizeof ob, out2, &sk);
    if (!rc) rc = wf_jpe_rows((struct wf_jpe *)e, tp, WF_DK_XW * 2, WF_DK_XH);
    if (!rc) rc = wf_jpe_end((struct wf_jpe *)e);
    uint32_t app0 = jb[2] == 0xFF && jb[3] == 0xE0 ? 2u + (jb[4] << 8 | jb[5]) : 0;
    CHECK(rc == 0 && app0 > 0 && sk.n - app0 < 12100, "thumbnail jpeg %u B (APP0 %u)", sk.n, app0);
    rc = wf_dk_xthumb_put(a, al, jb, sk.n);
    uint32_t m = sk.n - app0, zero = 1;
    for (uint32_t i = 10 + 64 + m; i < al; i++)
        zero &= a[i] == 0;
    CHECK(rc == 0 && a[74] == 0xFF && a[75] == 0xD8 && !memcmp(a + 76, jb + 2 + app0, m - 2) && rdv(a + 10 + 48, 4, 1) == m
          && zero, "put: %d, %u B at 74, IFD1 length %u", rc, m, rdv(a + 10 + 48, 4, 1));
    al = mk_xapp1(a, 64, 100, 0);
    memcpy(a0, a, al);
    CHECK(wf_dk_xthumb_put(a, al, jb, sk.n) == -1 && !memcmp(a, a0, al), "put too big: refused, the APP1 as it was");
    CHECK(wf_dk_xthumb_put(a, al, jb + 1, sk.n - 1) == -1, "put: not a JPEG");
    free(e);

    /* the thumbnail's picture: a flat canvas fitted with black bars (wide: top / bottom, tall: left / right), every row
       fed in the encoder's 16-row bands */
    static const int cs[2][2] = { { 5184, 3456 }, { 2000, 3008 } };
    for (int k = 0; k < 2; k++) {
        int cw = cs[k][0], ch = cs[k][1];
        uint32_t *row = malloc((size_t)cw * 2 * 16);
        for (int i = 0; i < cw / 2 * 16; i++)
            row[i] = 0x0Au | 200u << 8 | (uint32_t)(uint8_t)-20 << 16 | 200u << 24;
        static struct wf_dk_xth xt;
        wf_dk_xth_begin(&xt, cw, ch, tp);
        for (int y = 0; y < ch; y += 16)
            wf_dk_xth_rows(&xt, row, cw * 2, y, ch - y < 16 ? ch - y : 16, tp);
        uint32_t mid = tp[(WF_DK_XH / 2) * (WF_DK_XW / 2) + WF_DK_XW / 4], c0 = tp[0];
        uint32_t edge = k ? tp[(WF_DK_XH / 2) * (WF_DK_XW / 2)] : tp[WF_DK_XW / 4];
        CHECK(wf_dk_xth_done(&xt) && mid == row[0] && c0 == 0 && edge == 0 && (k ? xt.th == WF_DK_XH && xt.tw < WF_DK_XW
              : xt.tw == WF_DK_XW && xt.th < WF_DK_XH), "xth %dx%d: %dx%d at %d,%d, mid %08x edge %08x", cw, ch, xt.tw,
              xt.th, xt.ox, xt.oy, mid, edge);
        free(row);
    }
    free(tp);
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

/* the print finishes of a finish run (0 = the frame mode); logo = a WFLOGO.DAT, model = the name the caption and the
   logo take instead of the shot's (a preview of another camera's print) */
struct fin { const char *tag, *pack, *look, *roll; int frame, info, date; const char *logo, *model; };

/* wf_dark.c dk_finish on the host: the date stamp into frm, the canvas layout, the caption band, the logo's maps, the
   APP1's size */
static void finish(const struct fin *fn, const struct wf_dk_shot *cs, uint8_t *app1, uint32_t al, uint32_t *frm, int W,
                   int H, struct wf_pf_lay *L, uint8_t **band, uint32_t **rowbuf, uint8_t **maps)
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
    if (fn->model)                                           /* a preview: another camera's name on this picture */
        snprintf(s.model, sizeof s.model, "%s", fn->model);
    struct wf_pf_logo g;                                     /* the logo pack (tools/mk_wflogo.py), as dk_finish reads it */
    memset(&g, 0, sizeof g);
    uint32_t ln = 0;
    uint8_t *lk = fn->logo ? slurp(fn->logo, WF_PF_LOGO_MAXB + 1, &ln) : 0;
    CHECK(!fn->logo || (lk && ln <= WF_PF_LOGO_MAXB && wf_pf_logo(&g, lk, ln, s.model) == 0), "logo pack %s",
          fn->logo ? fn->logo : "-");
    struct wf_pf_txt tx;
    struct wf_pf_cap c;
    struct wf_pf_lpos P;
    char dr[200];
    wf_pf_caption(&s, fn->look, fn->roll, &tx, &c);
    uint32_t need = wf_pf_logo_place(L, fok ? F : 0, &g, &c, &P);
    if (P.n)
        wf_pf_cap_logo(&tx, &c, &g);
    wf_pf_band(L, fok ? F : 0, &c, *band, dr, sizeof dr);
    if (P.n) {
        *maps = malloc(need + 4);
        CHECK(wf_pf_logo_maps(L, &P, *maps, need) == 0, "logo maps");
        printf("finish: logo %s %dx%d at %d,%d%s%s, maps %u B\n", g.maker, P.w[0], P.h[0], P.u[0], P.v[0],
               P.n > 1 ? " + lettering " : "", P.n > 1 ? s.model : "", need);
    } else if (fn->logo)
        printf("finish: logo pack read, no logo for '%s'\n", s.model);
    int dims = app1 ? wf_dk_dims(app1, al, L->cw, L->ch) : -1;
    CHECK(!app1 || dims == 0, "Exif pixel size not set");
    printf("finish: frame %d info %d o%d, caption \"%s\"\n", fn->frame, fn->info, orient, fok ? dr : "");
    free(pk);
    free(lk);
}

/* the Exif thumbnail as wf_dark.c dk_xthumb makes it: xt's picture (gathered from the canvas's rows while they were
   encoded) as a JPEG, quality down from 90 until it fits the APP1's slot. Returns the quality, 0 = it did not fit */
static int xthumb(uint8_t *app1, uint32_t al, const struct wf_dk_xth *xt, const uint32_t *xb, uint32_t *n)
{
    static uint8_t jb[48 * 1024], ob[8192];
    uint8_t *e = malloc(wf_jpe_bytes());
    int q = 90, ok = 0;
    for (; q >= 40 && !ok; q -= 10) {
        sk.b = jb; sk.n = 0; sk.cap = sizeof jb;
        ok = wf_dk_xth_done(xt) && wf_jpe_begin((struct wf_jpe *)e, WF_DK_XW, WF_DK_XH, q, 0, 0, ob, sizeof ob, out2, &sk) == 0
             && wf_jpe_rows((struct wf_jpe *)e, xb, WF_DK_XW * 2, WF_DK_XH) == 0 && wf_jpe_end((struct wf_jpe *)e) == 0
             && wf_dk_xthumb_put(app1, al, jb, sk.n) == 0;
    }
    free(e);
    *n = sk.n;
    return ok ? q + 10 : 0;
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

    /* levels and scales (the camera's L frame in its CR2: wf_dk_geom; EOS M at 84, 64, 5D2 at 168, 56) */
    const struct wf_dk_geom *g = wf_dk_geom(dk_cam);
    const int dcam = dk_cam == WF_DK_5D2 ? WF_DEV_5D2 : WF_DEV_EOSM;
    int W = g->w, H = g->h;
    CHECK(inf.w == g->sw && inf.h == g->sh && inf.pitch == g->pitch, "sensor image %dx%d pitch %d (want %dx%d %d)", inf.w,
          inf.h, inf.pitch, g->sw, g->sh, g->pitch);
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = inf.pitch;
    j.ax0 = g->ax0; j.ay0 = g->ay0; j.ax1 = inf.w; j.ay1 = inf.h;
    j.x0 = g->x0 - g->dx; j.y0 = g->y0; j.w = g->dw; j.h = H; j.cfa = g->cfa;
    void *scr = malloc(wf_dev_scratch_bytes(g->dw, H));
    int lb = 0, lw = 0;
    wf_dev_levels(scr, &j, g->bx0, g->bx1, &lb, &lw);
    j.black = lb ? lb : g->black;
    j.white = lw ? lw : wf_dk_white(dk_cam, inf.iso);
    int as = wf_dk_scales(dk_cam, inf.wb, j.black, j.white, inf.iso, j.wb_q8);
    j.roll = 1;
    static struct wf_dev_tabs T;
    CHECK(wf_dev_build(&T, WF_DEV_SRGB, 1, dcam, 0, 0, 0, 0) == 0, "build sRGB");
    struct wf_dev_stat st;
    rc = wf_dev_prepare(scr, &T, &j, &st);
    CHECK(rc == 0 && st.wb_src == 2, "prepare %d src %d", rc, st.wb_src);
    printf("frame %s: ISO %d EV8 %d, %d steps, black %d white %d%s, WB %s R/G %.3f B/G %.3f, trim %+d/10 EV, shoulder %d,"
           " orient %d, cd %d\n", cr2, inf.iso, wf_dk_ev8(inf.iso, inf.et_n, inf.et_d, inf.fn_n, inf.fn_d), steps, j.black,
           j.white, lw ? " (pile-up)" : " (table)", as ? "as-shot" : "daylight",
           (double)st.m_q8[0] / st.m_q8[1], (double)st.m_q8[2] / st.m_q8[1], wf_dk_trim10(dk_cam, inf.iso), st.roll,
           inf.orient, inf.cd_count);

    /* the fit against Canon's own frame (the at-shot B): how far the scene scale is from Canon's exposure. The 5D2's
       develop is wider than Canon's frame: the fit takes a 5600-column window inside it (a multiple of 32) */
    uint32_t *frm = malloc((size_t)W * H * 2);
    if (juyvy) {
        FILE *u = fopen(juyvy, "rb");
        CHECK(u && fread(frm, 2, (size_t)W * H, u) == (size_t)W * H, "jpeg.uyvy");
        if (u) fclose(u);
        struct wf_dev_job j2 = j;
        memset(j2.wb_q8, 0, sizeof j2.wb_q8);
        j2.canon = frm; j2.canon_y0 = 0; j2.canon_h = H;
        if (g->dw != W) {
            j2.x0 = g->x0 + 8; j2.w = (W - 16) & ~31;
            j2.canon = frm + 4; j2.canon_pw = W / 2;
        }
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
        if (g->dw == W)
            wf_dev_rows(scr, y, yb, frm + y * (W / 2), W * 2);
        else
            wf_dev_rows_win(scr, y, yb, frm + y * (W / 2), W * 2, g->dx, W);
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
    uint8_t *band = 0, *maps = 0;
    uint32_t *rowbuf = 0;
    wf_pf_layout(&L, WF_PFR_OFF, 0, W, H, 1);
    if (fn && (fn->frame || fn->date))
        finish(fn, &cs, app1, al, frm, W, H, &L, &band, &rowbuf, &maps);
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
    static struct wf_dk_xth xt;                              /* the Exif thumbnail (wf_dark.c dk_encode) */
    static uint32_t xb[WF_DK_XW / 2 * WF_DK_XH];
    uint32_t xo = 0, xroom = 0, xn = 0;
    int xth = app1 && wf_dk_xthumb_slot(app1, al, &xo, &xroom) == 0;
    CHECK(!app1 || xth, "no Exif thumbnail slot in Canon's APP1");
    if (xth)
        wf_dk_xth_begin(&xt, L.cw, L.ch, xb);
    uint8_t *e = malloc(wf_jpe_bytes());
    rc = wf_jpe_begin((struct wf_jpe *)e, L.cw, L.ch, 95, app1, (int)al, ob, sizeof ob, out, &s);
    for (int y = 0; rc == 0 && y < L.ch; y += 16) {
        const uint32_t *rows;
        int pitch, n = wf_pf_band_rows(&L, y, frm, W * 2, band, rowbuf, &rows, &pitch);
        rc = wf_jpe_rows((struct wf_jpe *)e, rows, pitch, n);
        if (xth)
            wf_dk_xth_rows(&xt, rows, pitch, y, n, xb);
    }
    if (rc == 0) rc = wf_jpe_end((struct wf_jpe *)e);
    int xq = xth && rc == 0 ? xthumb(app1, al, &xt, xb, &xn) : 0;
    if (xq) {                                                /* the APP1 again, at offset 2 (after SOI) */
        fseek(s.f, 2, SEEK_SET);
        CHECK(fwrite(app1, 1, al, s.f) == al, "APP1 rewrite");
    }
    fclose(s.f);
    CHECK(rc == 0, "encode %d", rc);
    CHECK(!xth || xq, "Exif thumbnail did not fit its slot (%u B)", xroom);
    printf("print %s: %u bytes, APP1 %u bytes, Exif thumbnail q%d %u B of %u\n", p, s.n, al, xq, xn, xroom);
    printf("canvas %d %d %d %d %d\n", L.cw, L.ch, L.ix, L.iy, L.orient);
    free(band);
    free(rowbuf);
    free(maps);
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
 *   D <tag> <srgb|cube|disp> <curve> <flut> <cube|-> <1|3> [a|i|d]  a develop, as wf_dark.c builds it for a look (cube:
 *                                                           LUT EI 0, auto exposure when flut; disp: Highlights / Shadows
 *                                                           0), then
 *        every frame through it: 1 = OUTDIR/<name>_<tag>.uyvy (5184 x 3456), 3 = OUTDIR/<name>_<tag>.ppm (a third, 3 x 3
 *        box, the JFIF matrix). a / i / d: the film shoulder's colour (j.hue, WF_DEV_HUE_*; default a); a chain film's
 *        RAW develop is "cube 1 0 <its log table> 1 i" (S-Log3, no auto exposure, shoulder I as wf_dark.c / wf_rawb.c) */
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
        char a[64], b[512], c[64], cube[512], hk[8] = "a";
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
        } else if (sscanf(ln, "D %63s %63s %d %d %511s %d %7s", a, c, &curve, &flut, cube, &down, hk) >= 6) {
            int hue = hk[0] == 'i' ? WF_DEV_HUE_I : hk[0] == 'd' ? WF_DEV_HUE_D : WF_DEV_HUE_A;
            int trim = hue == WF_DEV_HUE_I && strcmp(hk, "i0");   /* i = the camera's (+ its colour trim), i0 = without */
            int src = !strcmp(c, "srgb") ? WF_DEV_SRGB : !strcmp(c, "cube") ? WF_DEV_CUBE : !strcmp(c, "disp") ? WF_DEV_DISP : 0;
            const char *cf = strcmp(cube, "-") ? cube : 0;
            struct wf_ll_info li;
            int rc = src == WF_DEV_DISP ? wf_dev_build_disp(&T, WF_DEV_EOSM, cf, 0, 0, 0, &li)
                   : src ? wf_dev_build(&T, src, src == WF_DEV_SRGB ? 0 : curve, WF_DEV_EOSM, cf, 0, src == WF_DEV_CUBE && flut != 0, &li)
                   : -1;
            CHECK(rc == 0, "develop %s: build %s curve %d %s: %d", a, c, curve, cube, rc);
            if (rc)
                continue;
            if (trim)
                wf_dev_trim(&T, WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB);
            for (int i = 0; i < nf; i++) {
                struct wf_dev_job j = fr[i].j;
                j.roll = src != WF_DEV_LOG;
                j.hue = hue;
                struct wf_dev_stat st;
                memset(&st, 0, sizeof st);
                rc = wf_dev_prepare(scr, &T, &j, &st);
                CHECK(rc == 0, "%s %s: prepare %d", fr[i].name, a, rc);
                CHECK(rc || st.hue == (st.roll && src == WF_DEV_CUBE && curve >= 1 && curve <= 5 ? hue : WF_DEV_HUE_A),
                      "%s %s: shoulder %d, asked %d (roll %d)", fr[i].name, a, st.hue, hue, st.roll);
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
                printf("develop %s %s: %s (roll %d k %d c %d, shoulder %s%s)\n", fr[i].name, a, p, st.roll, st.ro_k,
                       st.ro_c, wf_dev_hue_name(st.hue), trim ? ", trim" : "");
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
    const char *cm = getenv("WF_DK_CAM");
    if (cm && !strcmp(cm, "5D2"))
        dk_cam = WF_DK_5D2;
    if (argc >= 2 && !strcmp(argv[1], "units")) {
        units();
        punits();
        lunits();
    } else if (argc >= 5 && !strcmp(argv[1], "frame"))
        frame(argv[2], strcmp(argv[3], "-") ? argv[3] : 0, argv[4], argc > 5 ? argv[5] : 0, 0);
    else if (argc >= 12 && !strcmp(argv[1], "finish")) {
        struct fin fn = { argv[5], strcmp(argv[9], "-") ? argv[9] : 0, argv[10], strcmp(argv[11], "-") ? argv[11] : 0,
                          atoi(argv[6]), atoi(argv[7]), atoi(argv[8]),
                          argc > 12 && strcmp(argv[12], "-") ? argv[12] : 0, argc > 13 && strcmp(argv[13], "-") ? argv[13] : 0 };
        frame(argv[2], strcmp(argv[3], "-") ? argv[3] : 0, argv[4], 0, &fn);
    } else if (argc >= 4 && !strcmp(argv[1], "devset"))
        devset(argv[2], argv[3]);
    else {
        fprintf(stderr, "usage: wf_dark_test units | frame CR2 JPG|- OUTDIR [JPEG.UYVY]\n"
                        "       | finish CR2 JPG|- OUTDIR TAG FRAME INFO DATE PACK|- LOOK ROLL|- [LOGO|- [MODEL]]\n"
                        "       | devset LIST OUTDIR\n");
        return 2;
    }
    printf("RESULT %d / %d\n", pass, fail);
    return fail ? 1 : 0;
}
