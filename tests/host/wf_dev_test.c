/* wf_dev_test.c - host test of the RAW develop "B" (src/wf_dev.c, WindyStrife). Static ARM Linux program (qemu-arm),
 * linked with the module objects wf_dev.o, wf_loglut.o, wf_llk.o, wf_llk_arm.o, wf_logout.o and tests/host/wf_dev_shim.c.
 *
 *   wf_dev_test px                pixel look: wf_dev_px vs the Log + LUT JPEG path (wf_loglut_load + wf_ll_words) on the
 *                                 same linear light, 5 curves x cubes 2 / 17 / 33 / 65, LUT EI, Film LUT auto exposure;
 *                                 the SRGB base develop vs its own input
 *   wf_dev_test frame             synthetic shot (wf_dev_scene.c, 1024 x 640): WB / exposure fit, patch match vs the JPEG
 *                                 path, hot pixels outside the active area, row bands, highlight rebuild (LOG output), 5D2
 *   wf_dev_test full DIR          the EOS M frame (5184 x 3456 of a 5280 x 3528 raw, pitch 9240) from DIR/scene.r14 and
 *                                 scene.uyvy (made natively by wf_dev_scene, see tools/dev_check.sh): fit, patch match,
 *                                 border; writes DIR/dev.uyvy, ref.uyvy and the develop checksum (= the bench's)
 *   wf_dev_test bad               jobs wf_dev_prepare must refuse
 *   wf_dev_test span              the asm span kernel (src/wf_dev_arm.S) = the C reference bit for bit: random raw
 *                                 below clip, 3 node sources x 4 Bayer phases x 9 WB scales (matrix shifts down to 7)
 *   wf_dev_test hue               the film shoulder I / D (j.hue): luma weights = each gamut's Y row; the asm span kernel
 *                                 wf_dv_span_hs = its C reference with every path taken; within 2 codes of A on a
 *                                 neutral ramp, both cameras
 *   wf_dev_test roll              the film shoulder on a neutral exposure ramp: CUBE (identity, film), SRGB, LOG, both cameras
 *   wf_dev_test rsrc              which buffer holds the shot (src/wf_rsrc.c): pointer right / at the previous shot / 0,
 *                                 no match, both buffers the same frame, no address, a frame with no mid-tones
 *   wf_dev_test pack              the chain films' log pack (wf_dev_build_pack): each entry's tables = its text cube's
 *                                 byte for byte (two curves, LUT EI, both cameras), shared tables, refusals (missing,
 *                                 unlisted, bad header / index / checksum, truncated)
 *   wf_dev_test packreal PACK [L33 ID VARIANT CODE]..  a real WFLOG.DAT: every entry builds; each named entry = a
 *                                 text cube of its .L33, byte for byte, both cameras
 *   wf_dev_test pick DIR1 DIR2..  real pairs: DIR1's Canon frame + curve against every DIR's raw (DIR1 must win)
 *   wf_dev_test look DIR CUBE R   a real pair through a card cube, film shoulder R (writes DIR/look_r<R>_k<knee>.uyvy)
 *   wf_dev_test cr2 DIR [X0 Y0]   a real EOS M RAW+L pair prepared by tools/dev_cr2.sh: WB fit vs Canon's as-shot WB;
 *                                 the develop vs Log + LUT of Canon's JPEG on flat blocks (with A1's curve when
 *                                 dev_a1ref made ref_a1.uyvy + dec.bin: raw-unclipped blocks checked, clipped blocks
 *                                 reported); the sRGB base develop vs the JPEG
 *   wf_dev_test leica DIR CUBE [H S]  a dev_cr2.sh KEEP=1 pair through Leica Vivid on B (WF_DEV_DISP: the sRGB develop,
 *                                 then the display cube on the sRGB codes): DIR/dev_srgbr.uyvy, DIR/dev_leica.uyvy
 * Cubes are written by the test itself (synthetic "film" cubes) into the current directory. Last line:
 *   RESULT dev <mode>: N pass, M fail */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wf_dev.h"
#include "wf_dev_scene.h"
#include "wf_rsrc.h"
#define WF_LL_EXTERN_ENC
#include "wf_loglut_tab.h"

extern int wf_ll_ei, wf_ll_auto;

static int np, nf;
#define CHECK(c, ...) do { if (c) np++; else { nf++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------------ cubes */
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
#define write_cube sc_write_cube

/* ------------------------------------------------------------------ helpers */
static uint32_t rnd_st = 777;
static uint32_t rnd(void) { rnd_st = rnd_st * 1103515245u + 12345u; return rnd_st >> 8; }

static uint32_t word(int y0, int y1, int u, int v)
{
    return (uint32_t)(u & 255) | (uint32_t)y0 << 8 | (uint32_t)(v & 255) << 16 | (uint32_t)y1 << 24;
}
static int wY0(uint32_t w) { return (w >> 8) & 255; }
static int wY1(uint32_t w) { return w >> 24; }
static int wU(uint32_t w) { return (int8_t)(w & 255); }
static long ndiff(const uint32_t *a, const uint32_t *b, int n)
{
    long d = 0;
    for (int i = 0; i < n; i++) d += a[i] != b[i];
    return d;
}
static int wV(uint32_t w) { return (int8_t)((w >> 16) & 255); }

static void ycc2rgb8(double Y, double Cb, double Cr, double o[3])
{
    o[0] = Y + 1.402 * Cr;
    o[1] = Y - 0.344136286201022 * Cb - 0.714136286201022 * Cr;
    o[2] = Y + 1.772 * Cb;
}

/* linear Rec.709 -> x Q14 in the working gamut of curve (1..5), or Rec.709 for curve 0 */
static void lin2x(int curve, const double lin[3], int32_t x[3])
{
    for (int r = 0; r < 3; r++) {
        double s = 0;
        for (int c = 0; c < 3; c++)
            s += (curve ? wf_ll_mat[curve - 1][3 * r + c] / 1048576.0 : r == c) * lin[c];
        s *= 16384;
        x[r] = s < 0 ? 0 : s > WF_DEV_XMAX ? WF_DEV_XMAX : (int32_t)floor(s + 0.5);
    }
}

struct st { double s, m; int n; int h[16]; };
static void st_add(struct st *a, double d)
{
    d = fabs(d);
    a->s += d;
    if (d > a->m) a->m = d;
    a->n++;
    int k = (int)(d + 0.5);
    a->h[k > 15 ? 15 : k]++;
}
static double st_mean(const struct st *a) { return a->n ? a->s / a->n : 0; }
static int st_p99(const struct st *a)       /* the smallest whole-code error >= 99 % of the samples are within */
{
    int acc = 0;
    for (int k = 0; k < 16; k++) { acc += a->h[k]; if (acc >= a->n * 0.99) return k; }
    return 15;
}

/* ------------------------------------------------------------------ px */
static struct wf_lltabs *T;
static struct wf_dev_tabs *D;

/* one comparison set: random JPEG codes + every grey; JPEG path = wf_ll_words on the codes; develop = wf_dev_px on the
   linear light of the same codes */
static void px_compare(int curve, const char *what, struct st *sy, struct st *sc)
{
    for (int i = 0; i < 20000 + 256; i++) {
        int Y, U, V;
        double c8[3];
        if (i < 256) { Y = i; U = V = 0; }
        else {
            do {
                Y = (int)(rnd() % 256); U = (int)(rnd() % 256) - 128; V = (int)(rnd() % 256) - 128;
                ycc2rgb8(Y, U, V, c8);
            } while (c8[0] < -0.5 || c8[1] < -0.5 || c8[2] < -0.5 || c8[0] > 255.5 || c8[1] > 255.5 || c8[2] > 255.5);
        }
        uint32_t w = word(Y, Y, U, V);
        wf_ll_words(&w, 1, (int)T);
        ycc2rgb8(Y, U, V, c8);
        double lin[3];
        for (int c = 0; c < 3; c++) lin[c] = sc_srgb_eotf(clamp01(c8[c] / 255.0));
        int32_t x[3];
        lin2x(curve, lin, x);
        int o[3];
        wf_dev_px(D, x, o);
        int dy = ((o[0] + 128) >> 8) - wY0(w), du = ((o[1] + 128) >> 8) - 128 - wU(w), dv = ((o[2] + 128) >> 8) - 128 - wV(w);
        st_add(sy, dy);
        st_add(sc, du);
        st_add(sc, dv);
    }
    (void)what;
}

static void px_case(int curve, int n, int ident, int ei, int autoexp, double lim_mean, int lim_p99)
{
    char path[64];
    snprintf(path, sizeof path, "C%d_%d%s.CUB", curve, n, ident ? "I" : "F");
    write_cube(path, n, ident);
    struct wf_ll_info inf;
    wf_ll_ei = ei;
    wf_ll_auto = autoexp;
    int rc = wf_loglut_load(path, curve, T, &inf);
    CHECK(rc == WF_LL_OK, "wf_loglut_load %s: %d", path, rc);
    rc = wf_dev_build(D, WF_DEV_CUBE, curve, WF_DEV_EOSM, path, ei, autoexp, &inf);
    CHECK(rc == WF_LL_OK, "wf_dev_build %s: %d", path, rc);
    if (autoexp)
        CHECK((int8_t)T->pad[0] == D->auto10, "%s auto exposure: JPEG path %d, develop %d (0.1 EV)", path, (int8_t)T->pad[0],
              D->auto10);
    struct st sy = { 0 }, sc = { 0 };
    px_compare(curve, path, &sy, &sc);
    printf("px curve %d cube %2d%s EI %+d auto %d (%+.1f EV): Y mean %.3f p99 %d max %.0f | C mean %.3f p99 %d max %.0f\n",
           curve, n, ident ? " identity" : " film    ", ei, autoexp, D->auto10 / 10.0, st_mean(&sy), st_p99(&sy), sy.m,
           st_mean(&sc), st_p99(&sc), sc.m);
    CHECK(st_mean(&sy) <= lim_mean && st_p99(&sy) <= lim_p99, "%s Y mean %.3f p99 %d", path, st_mean(&sy), st_p99(&sy));
    CHECK(st_mean(&sc) <= lim_mean && st_p99(&sc) <= lim_p99, "%s C mean %.3f p99 %d", path, st_mean(&sc), st_p99(&sc));
    remove(path);
}

static void mode_px(void)
{
    for (int curve = 1; curve <= WF_LL_NCURVE; curve++) {
        px_case(curve, 2, 1, 0, 0, 0.6, 2);
        px_case(curve, 33, 0, 0, 0, 0.6, 2);
    }
    px_case(WF_LL_VLOG, 17, 0, 0, 0, 0.6, 2);
    px_case(WF_LL_VLOG, 65, 0, 0, 0, 0.6, 2);
    px_case(WF_LL_VLOG, 33, 0, 3, 0, 0.6, 2);    /* LUT EI +1 EV: gain 2 (the log segment above white) */
    px_case(WF_LL_VLOG, 33, 0, -2, 0, 0.6, 2);
    px_case(WF_LL_VLOG, 33, 0, 0, 1, 0.6, 2);    /* Film LUT auto exposure */
    px_case(WF_LL_SLOG3, 33, 0, 2, 1, 0.6, 2);
    /* SRGB base develop: the linear light of a JPEG code comes back as that code (Rec.709, sRGB OETF) */
    int rc = wf_dev_build(D, WF_DEV_SRGB, 0, WF_DEV_EOSM, 0, 0, 0, 0);
    CHECK(rc == WF_LL_OK, "SRGB build %d", rc);
    struct st sy = { 0 }, sc = { 0 };
    for (int i = 0; i < 20256; i++) {
        int Y, U, V;
        double c8[3];
        if (i < 256) { Y = i; U = V = 0; }
        else do {
            Y = (int)(rnd() % 256); U = (int)(rnd() % 256) - 128; V = (int)(rnd() % 256) - 128;
            ycc2rgb8(Y, U, V, c8);
        } while (c8[0] < -0.5 || c8[1] < -0.5 || c8[2] < -0.5 || c8[0] > 255.5 || c8[1] > 255.5 || c8[2] > 255.5);
        ycc2rgb8(Y, U, V, c8);
        double lin[3], ycc[3], q8[3];
        for (int c = 0; c < 3; c++) lin[c] = sc_srgb_eotf(clamp01(c8[c] / 255.0));
        for (int c = 0; c < 3; c++) q8[c] = 255 * clamp01(c8[c] / 255.0);
        sc_ycc(q8, ycc);
        int32_t x[3];
        lin2x(0, lin, x);
        int o[3];
        wf_dev_px(D, x, o);
        st_add(&sy, o[0] / 256.0 - ycc[0]);
        st_add(&sc, o[1] / 256.0 - 128 - ycc[1]);
        st_add(&sc, o[2] / 256.0 - 128 - ycc[2]);
    }
    printf("px SRGB base develop vs its input: Y mean %.3f p99 %d max %.2f | C mean %.3f p99 %d max %.2f\n", st_mean(&sy),
           st_p99(&sy), sy.m, st_mean(&sc), st_p99(&sc), sc.m);
    CHECK(st_mean(&sy) <= 0.5 && sy.m <= 2.0 && st_mean(&sc) <= 0.5 && sc.m <= 2.0, "SRGB identity");
}

/* ------------------------------------------------------------------ frame */
static void job_of(const struct sc_shot *s, struct wf_dev_job *j)
{
    memset(j, 0, sizeof *j);
    j->raw = s->raw;
    j->pitch = s->pitch;
    j->ax0 = s->ax0; j->ay0 = s->ay0; j->ax1 = s->ax0 + s->w; j->ay1 = s->ay0 + s->h;
    j->x0 = s->ax0; j->y0 = s->ay0; j->w = s->w; j->h = s->h;
    j->black = s->black; j->white = s->white; j->cfa = 0;
    j->canon = s->uyvy; j->canon_y0 = 0; j->canon_h = s->h;
}

static int fit_check(const struct sc_shot *s, const struct wf_dev_stat *st, const char *what)
{
    double m[3];
    for (int c = 0; c < 3; c++) m[c] = st->m_q8[c] / 4294967296.0;
    double er = m[0] / m[1] / (s->m[0] / s->m[1]) - 1, eb = m[2] / m[1] / (s->m[2] / s->m[1]) - 1, eg = m[1] / s->m[1] - 1;
    printf("%s fit: src %d tier %d n %d; R/G %+.2f %%, B/G %+.2f %%, exposure (G) %+.2f %%\n", what, st->wb_src, st->fit_tier,
           st->fit_n, 100 * er, 100 * eb, 100 * eg);
    CHECK(st->wb_src == 1, "%s: WB not fitted (src %d)", what, st->wb_src);
    CHECK(fabs(er) < 0.015 && fabs(eb) < 0.015 && fabs(eg) < 0.03, "%s fit off", what);
    return 0;
}

/* patch means of the develop vs the JPEG path, patches without clipping */
static void patch_check(const struct sc_shot *s, const uint32_t *dev, const uint32_t *ref, const char *what, double lim_mean,
                        double lim_max)
{
    struct st sy = { 0 }, sc = { 0 };
    const int W2 = s->w / 2, step = s->w > 2048 ? 4 : 1;   /* the full frame: every 4th row and pair */
    static double a[40][3], b[40][3];
    static int cnt[40];
    memset(a, 0, sizeof a); memset(b, 0, sizeof b); memset(cnt, 0, sizeof cnt);
    for (int y = 4; y < s->h - 4; y += step)
        for (int x = 4; x < s->w - 5; x += 2 * step) {
            double lin[3];
            int z, id, i1;
            sc_scene(s, x, y, lin, &z, &id);
            if (id < 0) continue;
            sc_scene(s, x - 4, y, lin, &z, &i1); if (i1 != id) continue;
            sc_scene(s, x + 5, y, lin, &z, &i1); if (i1 != id) continue;
            sc_scene(s, x, y - 4, lin, &z, &i1); if (i1 != id) continue;
            sc_scene(s, x, y + 4, lin, &z, &i1); if (i1 != id) continue;
            uint32_t d = dev[y * W2 + x / 2], r = ref[y * W2 + x / 2];
            a[id][0] += wY0(d) + wY1(d); a[id][1] += 2 * wU(d); a[id][2] += 2 * wV(d);
            b[id][0] += wY0(r) + wY1(r); b[id][1] += 2 * wU(r); b[id][2] += 2 * wV(r);
            cnt[id] += 2;
        }
    for (int id = 0; id < 40; id++) {
        int n = cnt[id];
        if (!n) continue;
        st_add(&sy, (a[id][0] - b[id][0]) / n);
        st_add(&sc, (a[id][1] - b[id][1]) / n);
        st_add(&sc, (a[id][2] - b[id][2]) / n);
    }
    printf("%s patches vs JPEG path (%d patches, means): Y mean %.3f max %.2f | C mean %.3f max %.2f\n", what, sy.n, st_mean(&sy),
           sy.m, st_mean(&sc), sc.m);
    CHECK(sy.n == 40, "%s: %d patches", what, sy.n);
    CHECK(st_mean(&sy) <= lim_mean && sy.m <= lim_max && st_mean(&sc) <= lim_mean && sc.m <= lim_max, "%s patch match", what);
}

/* the 2 px frame border vs 2 px inside, where the scene is flat there: a hot pixel outside the active area would show */
static void border_check(const struct sc_shot *s, const uint32_t *dev, const char *what)
{
    const int W2 = s->w / 2;
    int bad = 0, n = 0;
#define DY(x, y) (((x) & 1) ? wY1(dev[(y) * W2 + (x) / 2]) : wY0(dev[(y) * W2 + (x) / 2]))
    for (int y = 0; y < s->h; y++)
        for (int k = 0; k < 2; k++) {
            int xl = k, xr = s->w - 1 - k;
            double l1[3], l2[3];
            int z, id;
            sc_scene(s, xl, y, l1, &z, &id); sc_scene(s, xl + 4, y, l2, &z, &id);
            if (l1[0] == l2[0] && l1[1] == l2[1]) { n++; bad += abs(DY(xl, y) - DY(xl + 4, y)) > 4; }
            sc_scene(s, xr, y, l1, &z, &id); sc_scene(s, xr - 4, y, l2, &z, &id);
            if (z != SC_Z_DETAIL && fabs(l1[1] - l2[1]) < 0.002) { n++; bad += abs(DY(xr, y) - DY(xr - 4, y)) > 4; }
        }
    for (int x = 0; x < s->w; x++)
        for (int k = 0; k < 2; k++) {
            int yt = k, yb = s->h - 1 - k;
            double l1[3], l2[3];
            int z, id;
            sc_scene(s, x, yt, l1, &z, &id); sc_scene(s, x, yt + 4, l2, &z, &id);
            if (z != SC_Z_LAMP && l1[0] == l2[0] && l1[1] == l2[1]) { n++; bad += abs(DY(x, yt) - DY(x, yt + 4)) > 4; }
            sc_scene(s, x, yb, l1, &z, &id); sc_scene(s, x, yb - 4, l2, &z, &id);
            if (z != SC_Z_DETAIL && l1[0] == l2[0] && l1[1] == l2[1]) { n++; bad += abs(DY(x, yb) - DY(x, yb - 4)) > 4; }
        }
#undef DY
    printf("%s border: %d of %d flat border pixels differ by > 4 codes from 4 px inside\n", what, bad, n);
    CHECK(n > 1000 && bad == 0, "%s border (hot pixels outside the active area?)", what);
}

/* V-Log code (0..1) -> linear */
static double vlog_inv(double v)
{
    return v < 0.181 ? (v - 0.125) / 5.6 : pow(10.0, (v - 0.598206) / 0.241514) - 0.00873;
}

/* highlight rebuild: LOG output (V-Log identity nodes) decoded back to linear V-Gamut, zone means vs the scene */
static void highlight_check(const struct sc_shot *s, void *scr, const struct wf_dev_job *j)
{
    int rc = wf_dev_build(D, WF_DEV_LOG, WF_LL_VLOG, s->cam, 0, 0, 0, 0);
    CHECK(rc == 0, "LOG build %d", rc);
    struct wf_dev_stat st;
    rc = wf_dev_prepare(scr, D, j, &st);
    CHECK(rc == 0, "prepare (LOG) %d", rc);
    uint32_t *out = malloc((size_t)s->w * s->h * 2);
    wf_dev_rows(scr, 0, s->h, out, s->w * 2);
    printf("highlights: clipped quads %d, ratio samples %d, blocks with clipping %d, filled %d, highlight px %u\n", st.clip_quads,
           st.hl_samples, st.hl_blocks, st.hl_fill, st.hl_px);
    const double cl = s->white - s->black;
    double sum[4][3] = { { 0 } }, tru[4][3] = { { 0 } }, ysum[4] = { 0 };
    int cnt[4] = { 0 };
    const int W2 = s->w / 2;
    for (int y = 6; y < s->h / 2 - 6; y++)
        for (int x = (int)(0.6 * s->w) + 8; x < s->w - 8; x += 2) {
            /* zone = channels whose true raw is at the clip, the same in the 7 x 7 window of both pixels */
            int zone = -1, same = 1;
            for (int dy = -3; dy <= 3 && same; dy++)
                for (int dx = -3; dx <= 4 && same; dx++) {
                    int k = 0;
                    for (int c = 0; c < 3; c++) k += sc_rawval(s, x + dx, y + dy, c) >= cl * 0.97;
                    if (zone < 0) zone = k;
                    else if (k != zone) same = 0;
                }
            if (!same) continue;
            uint32_t w = out[y * W2 + x / 2];
            for (int e = 0; e < 2; e++) {
                double c8[3], lin[3], t[3];
                int z, id;
                ycc2rgb8(e ? wY1(w) : wY0(w), wU(w), wV(w), c8);
                sc_scene(s, x + e, y, lin, &z, &id);
                for (int r = 0; r < 3; r++) {
                    t[r] = 0;
                    for (int c = 0; c < 3; c++) t[r] += wf_ll_mat[WF_LL_VLOG - 1][3 * r + c] / 1048576.0 * lin[c];
                    sum[zone][r] += vlog_inv(c8[r] / 255.0);
                    tru[zone][r] += t[r];
                }
                ysum[zone] += e ? wY1(w) : wY0(w);
                cnt[zone]++;
            }
        }
    static const char *zn[4] = { "none clipped", "1 channel", "2 channels", "all 3" };
    for (int z = 0; z < 4; z++) {
        if (!cnt[z]) { CHECK(0, "highlight zone %s empty", zn[z]); continue; }
        double rg = sum[z][0] / sum[z][1], bg = sum[z][2] / sum[z][1], trg = tru[z][0] / tru[z][1], tbg = tru[z][2] / tru[z][1];
        double lum = sum[z][1] / tru[z][1];
        printf("highlights %-18s %6d px: R/G %.3f (scene %.3f, %+.1f %%), B/G %.3f (scene %.3f, %+.1f %%), G / scene G %.2f, Y %.1f\n",
               zn[z], cnt[z], rg, trg, 100 * (rg / trg - 1), bg, tbg, 100 * (bg / tbg - 1), lum, ysum[z] / cnt[z]);
        if (z < 3) {
            CHECK(fabs(rg / trg - 1) < 0.06 && fabs(bg / tbg - 1) < 0.06, "highlight zone %s colour off", zn[z]);
        } else {
            CHECK(fabs(rg - 1) < 0.04 && fabs(bg - 1) < 0.04, "all-clipped core not neutral (R/G %.3f B/G %.3f)", rg, bg);
        }
        if (z)
            CHECK(ysum[z] / cnt[z] >= ysum[z - 1] / cnt[z - 1] - 0.5, "zone %s darker than zone %s", zn[z], zn[z - 1]);
    }
    free(out);
}

static void run_frame(int cam, int w, int h, const char *dir)
{
    struct sc_shot s;
    sc_default(&s, w, h, cam);
    char what[32];
    snprintf(what, sizeof what, "%s %dx%d", cam ? "5D2" : "EOSM", w, h);
    if (dir)                                     /* the full frame: rendered natively by wf_dev_scene (tools/dev_check.sh) */
        CHECK(sc_load(&s, dir) == 0, "%s: no scene in %s", what, dir);
    else
        CHECK(sc_render(&s) == 0, "render");
    write_cube("FRAME.CUB", 33, 0);
    wf_ll_ei = 0;
    wf_ll_auto = 0;
    struct wf_ll_info inf;
    CHECK(wf_loglut_load("FRAME.CUB", WF_LL_VLOG, T, &inf) == 0, "load");
    CHECK(wf_dev_build(D, WF_DEV_CUBE, WF_LL_VLOG, cam, "FRAME.CUB", 0, 0, &inf) == 0, "build");
    struct wf_dev_job j;
    job_of(&s, &j);
    size_t nb = wf_dev_scratch_bytes(w, h);
    void *scr = malloc(nb);
    struct wf_dev_stat st;
    int rc = wf_dev_prepare(scr, D, &j, &st);
    CHECK(rc == 0, "%s prepare %d", what, rc);
    printf("%s: scratch %u B, tables %u B\n", what, (unsigned)nb, (unsigned)sizeof *D);
    fit_check(&s, &st, what);
    uint32_t *dev = malloc((size_t)w * h * 2), *ref = malloc((size_t)w * h * 2);
    wf_dev_rows(scr, 0, h, dev, w * 2);
    memcpy(ref, s.uyvy, (size_t)w * h * 2);
    wf_ll_words(ref, w * h / 2, (int)T);
    patch_check(&s, dev, ref, what, 1.0, 2.5);
    border_check(&s, dev, what);
    if (w <= 2048) {
        /* row bands in any order give the same frame */
        uint32_t *b2 = malloc((size_t)w * h * 2);
        int cut[4] = { 0, 37, 300, h };
        for (int k = 2; k >= 0; k--)
            wf_dev_rows(scr, cut[k], cut[k + 1], b2 + (size_t)cut[k] * (w / 2), w * 2);
        CHECK(!memcmp(b2, dev, (size_t)w * h * 2), "%s: row bands differ from one call", what);
        free(b2);
        /* a caller's WB (the fitted scales passed back) gives the same frame */
        struct wf_dev_job j2 = j;
        struct wf_dev_stat st2;
        for (int c = 0; c < 3; c++) j2.wb_q8[c] = st.m_q8[c];
        j2.canon = 0;
        CHECK(wf_dev_prepare(scr, D, &j2, &st2) == 0 && st2.wb_src == 2, "caller WB");
        wf_dev_rows(scr, 0, h, b2 = malloc((size_t)w * h * 2), w * 2);
        CHECK(!memcmp(b2, dev, (size_t)w * h * 2), "%s: caller WB frame differs", what);
        /* the span kernel (src/wf_dev_arm.S) against its C reference */
        CHECK(st.fast, "%s: span kernel off (sh %d)", what, st.sh);
        wf_dev_kernel(1);
        CHECK(wf_dev_prepare(scr, D, &j, &st2) == 0, "prepare (reference)");
        wf_dev_rows(scr, 0, h, b2, w * 2);
        wf_dev_kernel(0);
        CHECK(ndiff(b2, dev, w * h / 2) == 0, "%s: span kernel differs from its C reference in %ld words", what,
              ndiff(b2, dev, w * h / 2));
        printf("%s: matrix shift %d, span kernel %s, highlight path %u px\n", what, st.sh, st.fast ? "on" : "off",
               (unsigned)st.hl_px);
        free(b2);
        /* a window at x0 + 2 / + 6 (inside a pixblock, as Canon's JPEG crop): the same pixels where no clipping is near */
        for (int dx = 2; dx <= 6; dx += 4) {
            struct wf_dev_job j3 = j2;
            j3.x0 += dx;
            j3.w -= 32;
            CHECK(wf_dev_prepare(scr, D, &j3, &st2) == 0, "%s: window at x0 + %d refused", what, dx);
            uint32_t *b3 = malloc((size_t)j3.w * h * 2);
            wf_dev_rows(scr, 0, h, b3, j3.w * 2);
            long nd = 0, nt = 0;
            for (int y = 0; y < h; y++)
                for (int x = 8; x < (int)(0.55 * w) - 16; x += 2, nt++)
                    nd += b3[(size_t)y * (j3.w / 2) + x / 2] != dev[(size_t)y * (w / 2) + (x + dx) / 2];
            CHECK(nd == 0, "%s: window at x0 + %d differs in %ld of %ld pairs (patch zone)", what, dx, nd, nt);
            free(b3);
        }
        highlight_check(&s, scr, &j);
    }
    if (dir) {                                   /* the frames for a look (and the bench's checksum) */
        char p[256];
        FILE *f;
        snprintf(p, sizeof p, "%s/dev.uyvy", dir);
        if ((f = fopen(p, "wb"))) { fwrite(dev, 1, (size_t)w * h * 2, f); fclose(f); }
        snprintf(p, sizeof p, "%s/ref.uyvy", dir);
        if ((f = fopen(p, "wb"))) { fwrite(ref, 1, (size_t)w * h * 2, f); fclose(f); }
        uint32_t ck = 0;
        for (size_t i = 0; i < (size_t)w * h / 2; i++) ck = (ck << 5 | ck >> 27) ^ dev[i];
        printf("%s: wrote %s/dev.uyvy, ref.uyvy; develop checksum %08x\n", what, dir, ck);
    }
    remove("FRAME.CUB");
    free(dev); free(ref); free(scr);
    sc_free(&s);
}

/* random raw below the clip (every block on the span kernel): src/wf_dev_arm.S against its C reference for the three
   node sources, the four Bayer phases and WB scales from 1/4 to 64 x the default (matrix shifts down to where the
   kernel turns off) */
static void mode_span(void)
{
    const int w = 512, h = 96;
    struct sc_shot s;
    sc_default(&s, w, h, 0);
    CHECK(sc_render(&s) == 0, "render");
    int thr = s.black + (s.white - s.black) * 96 / 100;
    uint16_t *v = malloc((size_t)s.raw_w * 2);
    for (int y = 0; y < s.raw_h; y++) {
        for (int x = 0; x < s.raw_w; x++) {
            uint32_t r = rnd() % 100;
            v[x] = (uint16_t)(r < 10 ? rnd() % (s.black + 64) : r < 30 ? thr - 1 - rnd() % 512 : rnd() % thr);
        }
        sc_pack14(v, s.raw + (size_t)y * s.pitch, s.raw_w);
    }
    free(v);
    write_cube("SPAN.CUB", 33, 0);
    void *scr = malloc(wf_dev_scratch_bytes(w, h));
    uint32_t *a = malloc((size_t)w * h * 2), *b = malloc((size_t)w * h * 2);
    struct wf_dev_job j;
    struct wf_dev_stat st;
    job_of(&s, &j);
    j.canon = 0;
    int runs = 0, fast = 0, shmin = 99;
    for (int src = 1; src <= 3; src++) {
        CHECK(wf_dev_build(D, src, WF_LL_VLOG, 0, src == WF_DEV_CUBE ? "SPAN.CUB" : 0, 0, 0, 0) == 0, "build %d", src);
        for (int k = 0; k < 3; k++) j.wb_q8[k] = 0;
        CHECK(wf_dev_prepare(scr, D, &j, &st) == 0, "prepare");
        int32_t m0[3] = { st.m_q8[0], st.m_q8[1], st.m_q8[2] };
        for (int cfa = 0; cfa < 4; cfa++)
            for (int e = 0; e <= 8; e++) {
                j.cfa = cfa;
                for (int k = 0; k < 3; k++) j.wb_q8[k] = (int32_t)(((int64_t)m0[k] << e) >> 2);
                CHECK(wf_dev_prepare(scr, D, &j, &st) == 0, "prepare src %d cfa %d scale %d", src, cfa, e);
                CHECK(st.hl_px == 0 && st.clip_quads == 0, "clipping in the random frame");
                wf_dev_rows(scr, 0, h, a, w * 2);
                wf_dev_kernel(1);
                wf_dev_rows(scr, 0, h, b, w * 2);
                wf_dev_kernel(0);
                long nd = ndiff(a, b, w * h / 2);
                CHECK(nd == 0, "span src %d cfa %d scale 2^%d / 4 (sh %d): %ld of %d words differ", src, cfa, e, st.sh, nd,
                      w * h / 2);
                runs++;
                fast += st.fast;
                if (st.fast && st.sh < shmin) shmin = st.sh;
            }
    }
    CHECK(fast >= runs / 2 && fast < runs, "span kernel on in %d of %d runs (want most, not all)", fast, runs);
    printf("span: %d runs (%d on the span kernel, lowest matrix shift %d), %d x %d random raw: asm = C reference\n", runs,
           fast, shmin, w, h);
    remove("SPAN.CUB");
    free(a); free(b); free(scr);
    sc_free(&s);
}

/* hue: the film shoulder I / D (wf_dev.h WF_DEV_HUE_*, the chain films' RAW develop).
   1. its luma weights are the Y row of each log gamut: Rec.709 Y through wf_ll_mat inverted;
   2. wf_dv_span_hs = its C reference bit for bit: random raw below the clip through a film cube on S-Log3, the
      shoulder on, 4 Bayer phases x 9 WB scales, I and D; every one of the kernel's paths taken (the pre-test's plain
      lookups, w = 0, w > 0); A with roll stays wf_dv_span_s;
   3. on a neutral exposure ramp through the clip (roll's raw), I and D stay within 2 codes of A everywhere (a neutral's
      luma index is its own: the same knee and target) and differ from it nowhere below the knee;
   4. the colour trim i takes on the chain films (wf_dev_trim, WF_DEV_HUE_I_SAT / _VIB): 0 / 0 leaves the table as built;
      SAT / VIB (-19 / -19) keeps every node's Y' and every neutral, touches nothing but the nodes, and scales each
      node's Cb and Cr by (1 + SAT / 100) x (1 + VIB / 100 .. 1) with its sign kept (within a code of rounding). */
extern const int16_t *wf_dev_hue_luma(int curve);
extern uint32_t wf_dev_hue_paths[3];
static const int roll_gdn[3];                    /* roll's ramp (below) */
static double roll_ev(int x, int w);
static double enc_ev(int i, int i0);
static void inv3(const double *m, double *o)
{
    double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    o[0] = (m[4] * m[8] - m[5] * m[7]) / d; o[1] = (m[2] * m[7] - m[1] * m[8]) / d; o[2] = (m[1] * m[5] - m[2] * m[4]) / d;
    o[3] = (m[5] * m[6] - m[3] * m[8]) / d; o[4] = (m[0] * m[8] - m[2] * m[6]) / d; o[5] = (m[2] * m[3] - m[0] * m[5]) / d;
    o[6] = (m[3] * m[7] - m[4] * m[6]) / d; o[7] = (m[1] * m[6] - m[0] * m[7]) / d; o[8] = (m[0] * m[4] - m[1] * m[3]) / d;
}
static void mode_hue(void)
{
    /* 1. weights */
    static const double y709[3] = { 0.2126729, 0.7151522, 0.0721750 };
    double worst = 0;
    for (int cv = 1; cv <= WF_LL_NCURVE; cv++) {
        double m[9], im[9];
        for (int k = 0; k < 9; k++) m[k] = wf_ll_mat[cv - 1][k] / 1048576.0;
        inv3(m, im);
        const int16_t *lw = wf_dev_hue_luma(cv);
        CHECK(lw && lw[0] + lw[1] + lw[2] == 32768 && lw[0] > 0 && lw[1] > 0 && lw[2] <= 0, "curve %d: weights", cv);
        for (int c = 0; c < 3 && lw; c++) {
            double y = y709[0] * im[c] + y709[1] * im[3 + c] + y709[2] * im[6 + c];
            double e = fabs(lw[c] / 32768.0 - y);
            if (e > worst) worst = e;
            CHECK(e < 3.0 / 32768, "curve %d channel %d: weight %d / 32768 vs the gamut's Y %.6f", cv, c, lw[c], y);
        }
    }
    printf("hue: luma weights = the Y row of each log gamut (worst %.2f / 32768)\n", worst * 32768);

    /* 2. the kernel */
    const int w = 512, h = 96;
    struct sc_shot s;
    sc_default(&s, w, h, 0);
    CHECK(sc_render(&s) == 0, "render");
    int thr = s.black + (s.white - s.black) * 96 / 100;
    uint16_t *v = malloc((size_t)s.raw_w * 2);
    for (int y = 0; y < s.raw_h; y++) {
        for (int x = 0; x < s.raw_w; x++) {
            uint32_t r = rnd() % 100;
            v[x] = (uint16_t)(r < 10 ? rnd() % (s.black + 64) : r < 40 ? thr - 1 - rnd() % 1024 : rnd() % thr);
        }
        sc_pack14(v, s.raw + (size_t)y * s.pitch, s.raw_w);
    }
    free(v);
    write_cube("HUE.CUB", 33, 0);
    CHECK(wf_dev_build(D, WF_DEV_CUBE, WF_LL_SLOG3, 0, "HUE.CUB", 0, 0, 0) == 0, "build");
    void *scr = malloc(wf_dev_scratch_bytes(w, h));
    uint32_t *a = malloc((size_t)w * h * 2), *b = malloc((size_t)w * h * 2), *c0 = malloc((size_t)w * h * 2);
    struct wf_dev_job j;
    struct wf_dev_stat st;
    job_of(&s, &j);
    j.canon = 0;
    j.roll = 1;
    for (int k = 0; k < 3; k++) j.wb_q8[k] = 0;
    CHECK(wf_dev_prepare(scr, D, &j, &st) == 0, "prepare");
    int32_t m0[3] = { st.m_q8[0], st.m_q8[1], st.m_q8[2] };
    int runs = 0, fast = 0, rolled = 0;
    long moved = 0;
    memset(wf_dev_hue_paths, 0, sizeof wf_dev_hue_paths);
    for (int hue = WF_DEV_HUE_I; hue <= WF_DEV_HUE_D; hue++)
        for (int cfa = 0; cfa < 4; cfa++)
            for (int e = 0; e <= 8; e++) {
                j.cfa = cfa;
                for (int k = 0; k < 3; k++) j.wb_q8[k] = (int32_t)(((int64_t)m0[k] << e) >> 2);
                j.hue = WF_DEV_HUE_A;
                CHECK(wf_dev_prepare(scr, D, &j, &st) == 0 && st.hue == WF_DEV_HUE_A, "prepare A");
                wf_dev_rows(scr, 0, h, c0, w * 2);
                j.hue = hue;
                CHECK(wf_dev_prepare(scr, D, &j, &st) == 0, "prepare hue %d cfa %d scale %d", hue, cfa, e);
                CHECK(st.hl_px == 0 && st.clip_quads == 0, "clipping in the random frame");
                CHECK(st.hue == (st.roll ? hue : WF_DEV_HUE_A), "stat hue %d (roll %d, want %d)", st.hue, st.roll, hue);
                wf_dev_rows(scr, 0, h, a, w * 2);
                wf_dev_kernel(1);
                wf_dev_rows(scr, 0, h, b, w * 2);
                wf_dev_kernel(0);
                long nd = ndiff(a, b, w * h / 2);
                CHECK(nd == 0, "span_hs hue %d cfa %d scale 2^%d / 4 (sh %d, fast %d): %ld of %d words differ", hue, cfa, e,
                      st.sh, st.fast, nd, w * h / 2);
                runs++;
                fast += st.fast;
                rolled += st.roll;
                moved += ndiff(a, c0, w * h / 2);
            }
    CHECK(rolled == runs && fast >= runs / 2, "shoulder in %d of %d runs, span kernel in %d", rolled, runs, fast);
    CHECK(wf_dev_hue_paths[0] && wf_dev_hue_paths[1] && wf_dev_hue_paths[2], "paths: plain %u, w 0 %u, w > 0 %u",
          wf_dev_hue_paths[0], wf_dev_hue_paths[1], wf_dev_hue_paths[2]);
    CHECK(moved > 0, "I / D change no word against A");
    printf("hue: %d runs (%d on the span kernel), span_hs = C reference; C reference pixels: plain %u, w 0 %u, w > 0 %u; "
           "%ld words differ from A\n", runs, fast, wf_dev_hue_paths[0], wf_dev_hue_paths[1], wf_dev_hue_paths[2], moved);
    free(scr); free(a); free(b); free(c0);
    sc_free(&s);

    /* 3. the neutral ramp (roll_case's raw: -7 .. +5.5 EV over grey, G clips first, R last) */
    for (int cam = 0; cam < 2; cam++) {
        const int rw2 = 1024, rh2 = 64, x0 = 84, y0 = 64, rw = ((x0 + rw2 + 24) + 7) & ~7, rh = y0 + rh2 + 20,
                  pitch = rw / 8 * 14;
        const int black = cam ? 1024 : 2048, white = cam ? 15600 : 12636;
        uint16_t *row = malloc((size_t)rw * 2);
        uint8_t *raw = malloc((size_t)rh * pitch + 16);
        for (int y = 0; y < rh; y++) {
            for (int x = 0; x < rw; x++) {
                int xo = x - x0 < 0 ? 0 : x - x0 > rw2 - 1 ? rw2 - 1 : x - x0;
                int ch = ((x - x0) & 1) + ((y - y0) & 1);
                double vv = x < 72 ? 0 : roll_gdn[ch] * pow(2.0, roll_ev(xo, rw2));
                int r = black + (int)(vv + 0.5);
                row[x] = (uint16_t)(r > white ? white : r);
            }
            sc_pack14(row, raw + (size_t)y * pitch, rw);
        }
        struct wf_dev_job jr;
        memset(&jr, 0, sizeof jr);
        jr.raw = raw; jr.pitch = pitch;
        jr.ax0 = 72; jr.ay0 = 52; jr.ax1 = rw; jr.ay1 = rh;
        jr.x0 = x0; jr.y0 = y0; jr.w = rw2; jr.h = rh2;
        jr.black = black; jr.white = white; jr.cfa = 0; jr.roll = 1;
        for (int k = 0; k < 3; k++)
            jr.wb_q8[k] = (int32_t)(0.18 * 16777216.0 * 256.0 / roll_gdn[k] + 0.5);
        CHECK(wf_dev_build(D, WF_DEV_CUBE, WF_LL_SLOG3, cam, "HUE.CUB", 0, 0, 0) == 0, "ramp build");
        void *sr = malloc(wf_dev_scratch_bytes(rw2, rh2));
        uint32_t *fa = malloc((size_t)rw2 * rh2 * 2), *fh = malloc((size_t)rw2 * rh2 * 2);
        struct wf_dev_stat sa, sh2;
        CHECK(wf_dev_prepare(sr, D, &jr, &sa) == 0 && sa.roll, "ramp A");
        wf_dev_rows(sr, 0, rh2, fa, rw2 * 2);
        double ek = enc_ev(sa.ro_k, sa.ro_grey);
        for (int hue = WF_DEV_HUE_I; hue <= WF_DEV_HUE_D; hue++) {
            jr.hue = hue;
            CHECK(wf_dev_prepare(sr, D, &jr, &sh2) == 0 && sh2.hue == hue && sh2.ro_k == sa.ro_k && sh2.ro_c == sa.ro_c,
                  "ramp hue %d: the same shoulder (k %d c %d vs %d %d)", hue, sh2.ro_k, sh2.ro_c, sa.ro_k, sa.ro_c);
            wf_dev_rows(sr, 0, rh2, fh, rw2 * 2);
            int dmax = 0, below = 0, shown = 0;
            for (int y = 2; y < rh2 - 2; y++)
                for (int x = 8; x < rw2 - 8; x += 2) {
                    uint32_t p = fa[y * (rw2 / 2) + x / 2], q = fh[y * (rw2 / 2) + x / 2];
                    int dw = 0;
                    for (int sb = 0; sb < 32; sb += 8) {
                        int d = abs((int)((p >> sb) & 255) - (int)((q >> sb) & 255));
                        if (d > dw) dw = d;
                    }
                    if (dw > dmax) dmax = dw;
                    if (dw > 2 && y == rh2 / 2 && shown++ < 8)
                        printf("  x %d (%+.2f EV): A Y %d %d U %d V %d | hue %d: Y %d %d U %d V %d\n", x, roll_ev(x, rw2),
                               wY0(p), wY1(p), wU(p), (int8_t)(p >> 16), hue, wY0(q), wY1(q), wU(q), (int8_t)(q >> 16));
                    below += p != q && roll_ev(x + 4, rw2) < ek - 0.05;
                }
            CHECK(dmax <= 2 && below == 0, "%s ramp hue %d: %d codes from A at most (want <= 2), %d pairs differ below the knee",
                  cam ? "5D2" : "EOSM", hue, dmax, below);
            printf("hue: %s neutral ramp, %s: within %d codes of A, none below the knee (%.2f EV)\n", cam ? "5D2" : "EOSM",
                   hue == WF_DEV_HUE_I ? "I" : "D", dmax, ek);
        }
        free(sr); free(fa); free(fh); free(row); free(raw);
    }

    /* 4. the colour trim */
    {
        static struct wf_dev_tabs T1;
        CHECK(wf_dev_build(D, WF_DEV_CUBE, WF_LL_SLOG3, 0, "HUE.CUB", 0, 0, 0) == 0, "trim build");
        memcpy(&T1, D, sizeof T1);
        wf_dev_trim(&T1, 0, 0);
        CHECK(!memcmp(&T1, D, sizeof T1), "trim 0 / 0: the table as built");
        memcpy(&T1, D, sizeof T1);
        wf_dev_trim(&T1, WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB);
        CHECK(!memcmp((const char *)&T1 + sizeof T1.node, (const char *)D + sizeof T1.node, sizeof T1 - sizeof T1.node),
              "trim: only the nodes change");
        int nn = 0, nch = 0, bad = 0;
        double kmin = 2, kmax = 0, khi = 1 + WF_DEV_HUE_I_SAT / 100.0, klo = khi * (1 + WF_DEV_HUE_I_VIB / 100.0);
        for (int b = 0; b < WF_DEV_G; b++)
            for (int g = 0; g < WF_DEV_G; g++)
                for (int r = 0; r < WF_DEV_G; r++) {
                    const uint32_t *p = D->node + 2 * (r + WF_DEV_G * g + WF_DEV_BSTR * b), *q = T1.node + (p - D->node);
                    int c0[2] = { (int)(p[0] >> 16) - 257, (int)p[1] - 257 }, c1[2] = { (int)(q[0] >> 16) - 257, (int)q[1] - 257 };
                    nn++;
                    if ((p[0] & 0xffff) != (q[0] & 0xffff)) {
                        if (bad++ < 4) printf("  node %d %d %d: Y' %u -> %u\n", r, g, b, p[0] & 0xffff, q[0] & 0xffff);
                        continue;
                    }
                    if (!c0[0] && !c0[1]) {
                        if (c1[0] || c1[1]) bad++;
                        continue;
                    }
                    nch += c1[0] != c0[0] || c1[1] != c0[1];
                    for (int k = 0; k < 2; k++) {
                        int a = abs(c0[k]), z = abs(c1[k]);
                        if ((c0[k] > 0 && c1[k] < 0) || (c0[k] < 0 && c1[k] > 0) || z > a || z < (int)(klo * a) - 1 ||
                            z > (int)(khi * a + 0.5) + 1) {
                            if (bad++ < 8) printf("  node %d %d %d: c%d %d -> %d\n", r, g, b, k, c0[k], c1[k]);
                        }
                        if (a >= 64) {
                            double kk = (double)z / a;
                            if (kk < kmin) kmin = kk;
                            if (kk > kmax) kmax = kk;
                        }
                    }
                }
        CHECK(bad == 0 && nch > nn / 2, "trim %d / %d: %d nodes off (Y', a neutral, sign or a scale outside %.3f .. %.3f), "
              "%d of %d nodes moved", WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB, bad, klo, khi, nch, nn);
        printf("hue: trim %d / %d: %d of %d nodes moved, Y' and neutrals kept, chroma x %.3f .. %.3f (nodes with |c| >= 64)\n",
               WF_DEV_HUE_I_SAT, WF_DEV_HUE_I_VIB, nch, nn, kmin, kmax);
    }
    remove("HUE.CUB");
}

/* flat 16 x 16 blocks of the camera JPEG (Y range <= 8, not near 0 or 255): mean Y, Cb, Cr of a minus b */
static int cmp_dbl(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return (d > 0) - (d < 0); }
/* m (optional, one byte per 16 x 16 block, w / 16 per row): only the blocks with m == want */
static double flat_compare_m(const uint32_t *src, const uint32_t *a, const uint32_t *b, int w, int h, const char *what,
                             const uint8_t *m, int want)
{
    int nb = 0, band_n[3] = { 0, 0, 0 };
    double sd = 0, sad = 0, sc = 0, band_d[3] = { 0, 0, 0 };
    double *ad = malloc(sizeof(double) * (size_t)(w / 16) * (h / 16));
    for (int by = 0; by + 16 <= h; by += 16)
        for (int bx = 0; bx + 16 <= w; bx += 16) {
            if (m && m[(by / 16) * (w / 16) + bx / 16] != want)
                continue;
            int mn = 255, mx = 0;
            double s[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
            for (int y = by; y < by + 16; y++)
                for (int x = bx; x < bx + 16; x += 2) {
                    size_t i = (size_t)y * (w / 2) + x / 2;
                    int y0 = wY0(src[i]), y1 = wY1(src[i]);
                    int lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
                    if (lo < mn) mn = lo;
                    if (hi > mx) mx = hi;
                    s[0][0] += wY0(a[i]) + wY1(a[i]); s[0][1] += 2 * wU(a[i]); s[0][2] += 2 * wV(a[i]);
                    s[1][0] += wY0(b[i]) + wY1(b[i]); s[1][1] += 2 * wU(b[i]); s[1][2] += 2 * wV(b[i]);
                }
            if (mx - mn > 8 || mn < 12 || mx > 243)
                continue;
            double dY = (s[0][0] - s[1][0]) / 256, dC = (fabs(s[0][1] - s[1][1]) + fabs(s[0][2] - s[1][2])) / 512;
            double yb = s[1][0] / 256;
            int band = yb < 85 ? 0 : yb < 170 ? 1 : 2;
            band_n[band]++; band_d[band] += dY;
            sd += dY; sad += fabs(dY); sc += dC; ad[nb++] = fabs(dY);
        }
    double p90 = 0;
    if (nb) { qsort(ad, (size_t)nb, sizeof *ad, cmp_dbl); p90 = ad[nb * 9 / 10]; }
    printf("%s: %d flat blocks: dY mean %+.2f, |dY| mean %.2f p90 %.1f | |dC| mean %.2f | dY shadows %+.1f (%d) mids %+.1f (%d)"
           " highs %+.1f (%d)\n", what, nb, nb ? sd / nb : 0, nb ? sad / nb : 0, p90, nb ? sc / nb : 0,
           band_n[0] ? band_d[0] / band_n[0] : 0, band_n[0], band_n[1] ? band_d[1] / band_n[1] : 0, band_n[1],
           band_n[2] ? band_d[2] / band_n[2] : 0, band_n[2]);
    free(ad);
    return nb ? sad / nb : 99;
}
static double flat_compare(const uint32_t *src, const uint32_t *a, const uint32_t *b, int w, int h, const char *what)
{
    return flat_compare_m(src, a, b, w, h, what, 0, 0);
}

/* per 16 x 16 output block: 1 when a raw photosite within 2 px of the block is at or above thr (the develop rebuilds
   it: no matching input on the JPEG side, Canon clips it), else 0 */
static uint8_t *clip_blocks(const struct wf_dev_job *j, int W, int H, int thr, int *nclip)
{
    int bw = j->w / 16, bh = j->h / 16;
    uint8_t *m = calloc((size_t)bw * bh, 1);
    int16_t *r = malloc(sizeof(int16_t) * (size_t)W);
    *nclip = 0;
    for (int y = j->y0 - 2; y < j->y0 + bh * 16 + 2; y++) {
        if (y < 0 || y >= H) continue;
        wf_dev_unpack14(j->raw + (size_t)y * j->pitch, r, W & ~7, 0);
        int bys[2] = { (y - j->y0 - 2) / 16, (y - j->y0 + 2) / 16 };
        for (int x = j->x0 - 2; x < j->x0 + bw * 16 + 2 && x < (W & ~7); x++) {
            if (r[x] < thr) continue;
            int bxs[2] = { (x - j->x0 - 2) / 16, (x - j->x0 + 2) / 16 };
            for (int a = 0; a < 2; a++)
                for (int b = 0; b < 2; b++) {
                    int by = bys[a], bx = bxs[b];
                    if (y - j->y0 + 2 < 0 || x - j->x0 + 2 < 0) continue;
                    if (by >= 0 && by < bh && bx >= 0 && bx < bw && !m[by * bw + bx]) { m[by * bw + bx] = 1; (*nclip)++; }
                }
        }
    }
    free(r);
    return m;
}

static int read_all(const char *dir, const char *name, void *p, size_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t r = fread(p, 1, n, f);
    fclose(f);
    return r == n ? 0 : -1;
}
static void write_all(const char *dir, const char *name, const void *p, size_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}

/* a real EOS M shot (tools/dev_cr2.sh: tools/cr2_dec + tools/jpg_uyvy.py in dir: raw.r14, meta.txt, jpeg.uyvy and
   align.txt for the window at x0, y0). The develop with the fitted WB against Canon's JPEG through the same Log + LUT
   (the synthetic film cube, V-Log) and, with the sRGB nodes, against the JPEG itself, on flat blocks; the fit against
   Canon's as-shot WB. Writes dev_lut.uyvy, ref_lut.uyvy, dev_srgb.uyvy for a look. */
static void mode_cr2(const char *dir, int x0, int y0)
{
    char path[512];
    int W = 0, H = 0, pitch = 0, iso = 0, wb[4] = { 0, 0, 0, 0 }, dx = 99, dy = 99, jw = 0, jh = 0, wmeas = 0;
    double blk = 0, pk = 0;
    FILE *f;
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if ((f = fopen(path, "r"))) {
        int n = fscanf(f, "%d %d %d %d %lf %d %d %d %d %d", &W, &H, &pitch, &iso, &blk, &wb[0], &wb[1], &wb[2], &wb[3],
                       &wmeas);
        CHECK(n == 9 || n == 10, "meta.txt");
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/align.txt", dir);
    if ((f = fopen(path, "r"))) {
        CHECK(fscanf(f, "%d %d %lf %d %d", &dx, &dy, &pk, &jw, &jh) == 5, "align.txt");
        fclose(f);
    }
    CHECK(W > 0 && jw > 0, "%s: no meta.txt / align.txt", dir);
    if (W <= 0 || jw <= 0) return;
    CHECK(dx == 0 && dy == 0, "%s: the JPEG sits %+d %+d px off the window at %d, %d", dir, dx, dy, x0, y0);
    uint8_t *raw = malloc((size_t)H * pitch + 16);
    uint32_t *jpg = malloc((size_t)jw * jh * 2), *dev = malloc((size_t)jw * jh * 2), *ref = malloc((size_t)jw * jh * 2);
    CHECK(read_all(dir, "raw.r14", raw, (size_t)H * pitch) == 0 && read_all(dir, "jpeg.uyvy", jpg, (size_t)jw * jh * 2) == 0,
          "%s: raw.r14 / jpeg.uyvy", dir);
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = W; j.ay1 = H;   /* EOS M CR2: image data from 72, 52 to the right / bottom edge */
    j.x0 = x0; j.y0 = y0; j.w = jw; j.h = jh;
    /* white: the shot's measured clip pile-up when it has one, else the pile-ups measured in the Day09 / 1114 CR2s per
       ISO (the 1/3-stop ISOs clip lower: 160 / 320 at 12636), else 15200. On the camera this comes from the raw backend
       (CORE-NOTES 10.5). */
    int wtab = iso == 100 ? 13582 : iso == 160 || iso == 320 ? 12636 : iso == 200 ? 15303 : 15200;
    j.black = (int)(blk + 0.5); j.white = wmeas > j.black + 4096 ? wmeas : wtab; j.cfa = 0;
    j.canon = jpg; j.canon_y0 = 0; j.canon_h = jh;
    /* main's A1 (Canon input curve) when dev_cr2.sh made it: the curve table for the fit and the JPEG path's frame */
    static uint32_t dec[1022];
    uint32_t *ref_a1 = malloc((size_t)jw * jh * 2);
    int have_a1 = read_all(dir, "dec.bin", dec, sizeof dec) == 0 && read_all(dir, "ref_a1.uyvy", ref_a1, (size_t)jw * jh * 2) == 0;
    if (have_a1)
        j.dec = dec;
    write_cube("FRAME.CUB", 33, 0);
    wf_ll_ei = 0;
    wf_ll_auto = 0;
    struct wf_ll_info inf;
    CHECK(wf_loglut_load("FRAME.CUB", WF_LL_VLOG, T, &inf) == 0, "load");
    CHECK(wf_dev_build(D, WF_DEV_CUBE, WF_LL_VLOG, WF_DEV_EOSM, "FRAME.CUB", 0, 0, &inf) == 0, "build");
    void *scr = malloc(wf_dev_scratch_bytes(jw, jh));
    struct wf_dev_stat st;
    int rc = wf_dev_prepare(scr, D, &j, &st);
    CHECK(rc == 0, "%s: prepare %d", dir, rc);
    if (rc == 0) {
        wf_dev_rows(scr, 0, jh, dev, jw * 2);
        memcpy(ref, jpg, (size_t)jw * jh * 2);
        wf_ll_words(ref, jw * jh / 2, (int)T);
        double ar = wb[1] + wb[2] ? 2.0 * wb[0] / (wb[1] + wb[2]) : 0, ab = wb[1] + wb[2] ? 2.0 * wb[3] / (wb[1] + wb[2]) : 0;
        double fr = (double)st.m_q8[0] / st.m_q8[1], fb = (double)st.m_q8[2] / st.m_q8[1];
        printf("cr2 %s: ISO %d black %d white %d%s | WB src %d tier %d n %d: R/G %.3f B/G %.3f | Canon as-shot R/G %.3f B/G "
               "%.3f (fit %+.1f %%, %+.1f %%) | sh %d, span kernel %s, clipped quads %d, highlight px %u\n",
               dir, iso, j.black, j.white, j.white == wmeas ? " (measured)" : " (table)", st.wb_src, st.fit_tier, st.fit_n, fr, fb, ar, ab, ar ? 100 * (fr / ar - 1) : 0,
               ab ? 100 * (fb / ab - 1) : 0, st.sh, st.fast ? "on" : "off", st.clip_quads, (unsigned)st.hl_px);
        CHECK(st.wb_src == 1, "%s: no WB fit", dir);
        if (have_a1) {
            /* the matching input = blocks without clipped raw (the develop rebuilds the rest, above Canon's clip) */
            int nclip = 0;
            uint8_t *cm = clip_blocks(&j, W, H, j.black + (j.white - j.black) * 97 / 100, &nclip);
            printf("cr2 %s: %d of %d blocks of 16 x 16 hold clipped raw (rebuilt highlights)\n", dir, nclip,
                   (jw / 16) * (jh / 16));
            double e = flat_compare_m(jpg, dev, ref_a1, jw, jh, "cr2 develop vs Log + LUT of the JPEG, A1 curve, raw unclipped",
                                      cm, 0);
            CHECK(e < 20, "%s: develop far from the JPEG path (|dY| mean %.1f)", dir, e);
            flat_compare_m(jpg, dev, ref_a1, jw, jh, "cr2 develop vs Log + LUT of the JPEG, A1 curve, raw clipped", cm, 1);
            write_all(dir, "ref_a1_ok", "", 0);
            free(cm);
        }
        flat_compare(jpg, dev, ref, jw, jh, "cr2 develop vs Log + LUT of the JPEG, sRGB decode");
        write_all(dir, "dev_lut.uyvy", dev, (size_t)jw * jh * 2);
        write_all(dir, "ref_lut.uyvy", ref, (size_t)jw * jh * 2);
        CHECK(wf_dev_build(D, WF_DEV_SRGB, WF_LL_VLOG, WF_DEV_EOSM, 0, 0, 0, 0) == 0, "build sRGB");
        CHECK(wf_dev_prepare(scr, D, &j, &st) == 0, "prepare sRGB");
        wf_dev_rows(scr, 0, jh, dev, jw * 2);
        flat_compare(jpg, dev, jpg, jw, jh, "cr2 develop (sRGB nodes) vs Canon's JPEG");
        write_all(dir, "dev_srgb.uyvy", dev, (size_t)jw * jh * 2);
    }
    remove("FRAME.CUB");
    free(raw); free(jpg); free(dev); free(ref); free(ref_a1); free(scr);
}

/* leica DIR CUBE [H S]: one real EOS M shot (dev_cr2.sh KEEP=1 files, dec.bin when A1 made it) through the Leica Vivid
   path of RAW develop B: the sRGB develop with the film shoulder (j.roll 1, as wf_rawb.c), then the display cube on the
   sRGB codes (WF_DEV_DISP, no log step). Writes dev_srgbr.uyvy (the develop alone) and dev_leica.uyvy. */
static void mode_leica(const char *dir, const char *cube, int hl, int sh)
{
    char path[512];
    int W = 0, H = 0, pitch = 0, iso = 0, wb[4], dx = 99, dy = 99, jw = 0, jh = 0, wmeas = 0;
    double blk = 0, pk = 0;
    FILE *f;
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if ((f = fopen(path, "r"))) {
        int n = fscanf(f, "%d %d %d %d %lf %d %d %d %d %d", &W, &H, &pitch, &iso, &blk, &wb[0], &wb[1], &wb[2], &wb[3],
                       &wmeas);
        CHECK(n == 9 || n == 10, "meta.txt");
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/align.txt", dir);
    if ((f = fopen(path, "r"))) {
        CHECK(fscanf(f, "%d %d %lf %d %d", &dx, &dy, &pk, &jw, &jh) == 5, "align.txt");
        fclose(f);
    }
    CHECK(W > 0 && jw > 0 && dx == 0 && dy == 0, "%s: meta.txt / align.txt", dir);
    if (W <= 0 || jw <= 0) return;
    uint8_t *raw = malloc((size_t)H * pitch + 16);
    uint32_t *jpg = malloc((size_t)jw * jh * 2), *dev = malloc((size_t)jw * jh * 2);
    CHECK(read_all(dir, "raw.r14", raw, (size_t)H * pitch) == 0 && read_all(dir, "jpeg.uyvy", jpg, (size_t)jw * jh * 2) == 0,
          "%s: raw.r14 / jpeg.uyvy", dir);
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = W; j.ay1 = H;
    j.x0 = 84; j.y0 = 64; j.w = jw; j.h = jh;
    int wtab = iso == 100 ? 13582 : iso == 160 || iso == 320 ? 12636 : iso == 200 ? 15303 : 15200;
    j.black = (int)(blk + 0.5); j.white = wmeas > j.black + 4096 ? wmeas : wtab; j.cfa = 0;
    j.canon = jpg; j.canon_y0 = 0; j.canon_h = jh;
    j.roll = 1;
    static uint32_t dec[1022];
    if (read_all(dir, "dec.bin", dec, sizeof dec) == 0)
        j.dec = dec;                                 /* Canon's frame decoded with its own curve for the WB fit */
    void *scr = malloc(wf_dev_scratch_bytes(jw, jh));
    struct wf_dev_stat st;
    struct wf_ll_info inf;
    for (int pass = 0; pass < 2; pass++) {
        int rc = pass ? wf_dev_build_disp(D, WF_DEV_EOSM, cube, 0, hl, sh, &inf)
                      : wf_dev_build(D, WF_DEV_SRGB, 0, WF_DEV_EOSM, 0, 0, 0, &inf);
        CHECK(rc == 0, "%s: build %s rc %d line %d", dir, pass ? "disp" : "srgb", rc, inf.line);
        if (rc) break;
        rc = wf_dev_prepare(scr, D, &j, &st);
        CHECK(rc == 0, "%s: prepare %d", dir, rc);
        if (rc) break;
        wf_dev_rows(scr, 0, jh, dev, jw * 2);
        write_all(dir, pass ? "dev_leica.uyvy" : "dev_srgbr.uyvy", dev, (size_t)jw * jh * 2);
        printf("leica %s: %s, ISO %d, black %d white %d, roll %d, wb src %d, h %d s %d\n", dir, pass ? "disp" : "srgb",
               iso, j.black, j.white, st.roll, st.wb_src, hl, sh);
    }
    free(raw); free(jpg); free(dev); free(scr);
}

static void mode_bad(void)
{
    struct sc_shot s;
    sc_default(&s, 256, 128, 0);
    sc_render(&s);
    CHECK(wf_dev_build(D, WF_DEV_LOG, WF_LL_VLOG, 0, 0, 0, 0, 0) == 0, "build");
    void *scr = malloc(wf_dev_scratch_bytes(256, 128));
    struct wf_dev_job j, b;
    job_of(&s, &j);
    CHECK(wf_dev_prepare(scr, D, &j, 0) == 0, "good job refused");
    b = j; b.x0 += 1; b.w -= 32; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "x0 odd");
    b = j; b.w -= 8; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "w not a multiple of 32");
    b = j; b.y0 += 1; b.h -= 2; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "y0 odd");
    b = j; b.x0 -= 8; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "window left of the active area");
    b = j; b.pitch = 64; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "pitch too small");
    b = j; b.white = b.black + 100; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "white too close to black");
    b = j; b.cfa = 4; CHECK(wf_dev_prepare(scr, D, &b, 0) == -1, "cfa 4");
    CHECK(wf_dev_build(D, 9, 1, 0, 0, 0, 0, 0) == WF_LL_E_MODE, "source 9");
    CHECK(wf_dev_build(D, WF_DEV_CUBE, 7, 0, "X.CUB", 0, 0, 0) == WF_LL_E_MODE, "curve 7");
    CHECK(wf_dev_build(D, WF_DEV_CUBE, 2, 0, "NOFILE.CUB", 0, 0, 0) != 0, "missing cube");
    /* unpack round trip */
    uint16_t v[64], u[64];
    uint8_t pk[112 + 16];
    int16_t o[64];
    for (int i = 0; i < 64; i++) v[i] = (uint16_t)((i * 2741 + 13) & 16383);
    sc_pack14(v, pk, 64);
    wf_dev_unpack14(pk, o, 64, 2048);
    int ok = 1;
    for (int i = 0; i < 64; i++) { u[i] = (uint16_t)(o[i] + 2048); ok &= u[i] == v[i]; }
    CHECK(ok, "unpack14 round trip");
    free(scr);
    sc_free(&s);
}

/* x of enc index i (wf_dv_idx inverted: 8-bit mantissa, linear inside an octave) and stops between two indexes */
static double enc_x(int i) { int s = (i >> 8) - 1; return s < 0 ? (double)i : (double)(256 + (i & 255)) * (double)(1 << s); }
static double enc_ev(int i, int i0) { return log2(enc_x(i) / enc_x(i0)); }

/* look DIR CUBE ROLL [KNEE [FIT [LIFT]]]: one real shot (dev_cr2.sh KEEP=1 files: raw.r14, meta.txt, align.txt, jpeg.uyvy,
   dec.bin) through a card cube on V-Log, film shoulder ROLL 0 / 1, knee KNEE enc steps below the first clip. WB / scale:
   Canon's as-shot WB at match.py's scene scale (grey = 11352 / 2^3.3 DN), as the a1-vs-b renders; FIT 1 = the fit.
   LIFT: the shoulder ends on the look's own luma LIFT enc steps above the sensor's white, -1 = the look's white
   (wf_dev_roll_lift; not given = the library default, 384).
   Writes DIR/look_r<ROLL>_k<KNEE>[_l<LIFT>].uyvy. Look checks only: no pass / fail on the picture. */
extern int wf_dev_roll_knee, wf_dev_roll_lift;
static void mode_look(const char *dir, const char *cube, int roll, int knee, int fit, int lift)
{
    char path[512];
    int W = 0, H = 0, pitch = 0, iso = 0, wb[4] = { 0, 0, 0, 0 }, dx = 99, dy = 99, jw = 0, jh = 0, wmeas = 0;
    double blk = 0, pk = 0;
    FILE *f;
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if ((f = fopen(path, "r"))) {
        int n = fscanf(f, "%d %d %d %d %lf %d %d %d %d %d", &W, &H, &pitch, &iso, &blk, &wb[0], &wb[1], &wb[2], &wb[3],
                       &wmeas);
        CHECK(n == 9 || n == 10, "meta.txt");
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/align.txt", dir);
    if ((f = fopen(path, "r"))) {
        CHECK(fscanf(f, "%d %d %lf %d %d", &dx, &dy, &pk, &jw, &jh) == 5, "align.txt");
        fclose(f);
    }
    if (W <= 0 || jw <= 0) { CHECK(0, "%s: no meta.txt / align.txt", dir); return; }
    uint8_t *raw = malloc((size_t)H * pitch + 16);
    uint32_t *jpg = malloc((size_t)jw * jh * 2), *dev = malloc((size_t)jw * jh * 2);
    CHECK(read_all(dir, "raw.r14", raw, (size_t)H * pitch) == 0 && read_all(dir, "jpeg.uyvy", jpg, (size_t)jw * jh * 2) == 0,
          "%s: raw.r14 / jpeg.uyvy", dir);
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = W; j.ay1 = H;
    j.x0 = 84; j.y0 = 64; j.w = jw; j.h = jh;
    int wtab = iso == 100 ? 13582 : iso == 160 || iso == 320 ? 12636 : iso == 200 ? 15303 : 15200;
    j.black = (int)(blk + 0.5); j.white = wmeas > j.black + 4096 ? wmeas : wtab; j.cfa = 0;
    j.canon = jpg; j.canon_y0 = 0; j.canon_h = jh;
    static uint32_t dec[1022];
    if (read_all(dir, "dec.bin", dec, sizeof dec) == 0)
        j.dec = dec;
    if (!fit) {                                  /* raw unit -> lin Q24 x 256: 0.18 / (11352 / 2^3.3) per DN, as-shot WB */
        double g = 0.18 / (11352.0 / pow(2.0, 3.3)) * 16777216.0 * 256.0;
        j.wb_q8[0] = (int32_t)(g * wb[0] / wb[1] + 0.5);
        j.wb_q8[1] = (int32_t)(g + 0.5);
        j.wb_q8[2] = (int32_t)(g * wb[3] / wb[2] + 0.5);
    }
    j.roll = roll;
    wf_dev_roll_knee = knee;
    if (lift != -2)
        wf_dev_roll_lift = lift;
    struct wf_ll_info inf;
    int rc = wf_dev_build(D, WF_DEV_CUBE, WF_LL_VLOG, WF_DEV_EOSM, cube, 0, 0, &inf);
    CHECK(rc == 0, "build %s: %d", cube, rc);
    void *scr = malloc(wf_dev_scratch_bytes(jw, jh));
    struct wf_dev_stat st;
    rc = rc ? rc : wf_dev_prepare(scr, D, &j, &st);
    CHECK(rc == 0, "%s: prepare %d", dir, rc);
    if (rc == 0) {
        wf_dev_rows(scr, 0, jh, dev, jw * 2);
        printf("look %s: ISO %d white %d, wb src %d, roll %d", dir, iso, j.white, st.wb_src, st.roll);
        if (st.roll)
            printf(": knee %+.2f, sensor white %+.2f, look white %+.2f EV over grey, ends at Y %.1f", enc_ev(st.ro_k, st.ro_grey),
                   enc_ev(st.ro_c, st.ro_grey), enc_ev(st.ro_w, st.ro_grey), st.ro_ytop / 256.0);
        printf("; clipped quads %d, highlight px %u\n", st.clip_quads, (unsigned)st.hl_px);
        if (lift != -2)
            snprintf(path, sizeof path, "look_r%d_k%d_l%d.uyvy", roll, knee, lift);
        else
            snprintf(path, sizeof path, "look_r%d_k%d.uyvy", roll, knee);
        write_all(dir, path, dev, (size_t)jw * jh * 2);
    }
    free(scr); free(raw); free(jpg); free(dev);
}

/* roll: the film shoulder (wf_dv_roll) on a neutral exposure ramp, -7 .. +5.5 EV over grey across the frame, every row
   the same, white-balanced raw (grey 500 / 1000 / 625 DN in R / G / B: G clips first, R last). Per camera and node
   source: below the knee the words are the look's as is; Y never falls along the ramp and has no step; past the
   sensor's white (R's clip, the all-clipped plateau) Y sits on the look's white, neutral; LOG ignores roll. */
static const int roll_gdn[3] = { 500, 1000, 625 };
static double roll_ev(int x, int w) { return -7.0 + 12.5 * x / (w - 1); }

static void roll_case(int cam, int src, int ident, const char *what)
{
    const int w = 1024, h = 64, x0 = 84, y0 = 64, rw = ((x0 + w + 24) + 7) & ~7, rh = y0 + h + 20, pitch = rw / 8 * 14;
    const int black = cam ? 1024 : 2048, white = cam ? 15600 : 12636;
    uint16_t *row = malloc((size_t)rw * 2);
    uint8_t *raw = malloc((size_t)rh * pitch + 16);
    for (int y = 0; y < rh; y++) {
        for (int x = 0; x < rw; x++) {
            int xo = x - x0 < 0 ? 0 : x - x0 > w - 1 ? w - 1 : x - x0;
            int ch = ((x - x0) & 1) + ((y - y0) & 1);  /* RGGB at (x0, y0): 0 R, 1 G, 2 B */
            double v = x < 72 ? 0 : roll_gdn[ch] * pow(2.0, roll_ev(xo, w));
            int r = black + (int)(v + 0.5);
            row[x] = (uint16_t)(r > white ? white : r);
        }
        sc_pack14(row, raw + (size_t)y * pitch, rw);
    }
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = raw; j.pitch = pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = rw; j.ay1 = rh;
    j.x0 = x0; j.y0 = y0; j.w = w; j.h = h;
    j.black = black; j.white = white; j.cfa = 0;
    for (int c = 0; c < 3; c++)                  /* raw unit -> lin Q24 x 256: grey (0.18) at roll_gdn */
        j.wb_q8[c] = (int32_t)(0.18 * 16777216.0 * 256.0 / roll_gdn[c] + 0.5);
    struct wf_ll_info inf;
    if (src == WF_DEV_CUBE) write_cube("ROLL.CUB", 33, ident);
    int rc = wf_dev_build(D, src, WF_LL_VLOG, cam, src == WF_DEV_CUBE ? "ROLL.CUB" : 0, 0, 0, &inf);
    CHECK(rc == 0, "%s: build %d", what, rc);
    void *scr = malloc(wf_dev_scratch_bytes(w, h));
    uint32_t *a = malloc((size_t)w * h * 2), *b = malloc((size_t)w * h * 2);
    struct wf_dev_stat s0, s1;
    rc = wf_dev_prepare(scr, D, &j, &s0);
    CHECK(rc == 0 && !s0.roll, "%s: prepare (roll 0) %d", what, rc);
    wf_dev_rows(scr, 0, h, a, w * 2);
    j.roll = 1;
    rc = wf_dev_prepare(scr, D, &j, &s1);
    CHECK(rc == 0, "%s: prepare (roll 1) %d", what, rc);
    wf_dev_rows(scr, 0, h, b, w * 2);
    const int W2 = w / 2;
    if (src == WF_DEV_LOG) {
        CHECK(!s1.roll && ndiff(a, b, w * h / 2) == 0, "%s: LOG took the shoulder (roll %d, %ld words differ)", what, s1.roll,
              ndiff(a, b, w * h / 2));
        printf("roll %s: no shoulder on LOG, frame unchanged\n", what);
    } else if (!s1.roll) {
        CHECK(0, "%s: no shoulder", what);
    } else {
        double ek = enc_ev(s1.ro_k, s1.ro_grey), ec = enc_ev(s1.ro_c, s1.ro_grey), ew = enc_ev(s1.ro_w, s1.ro_grey);
        double ec_want = log2((double)(white - black) / roll_gdn[0]);
        int ytop = (s1.ro_ytop + 128) >> 8;
        /* the same words below the knee (all rows; the demosaic reaches 2 px = 0.03 EV) */
        long nd = 0, nt = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x += 2)
                if (roll_ev(x + 4, w) < ek - 0.05) {
                    nt++;
                    nd += a[y * W2 + x / 2] != b[y * W2 + x / 2];
                }
        /* along the middle row: drops, steps, the plateau past the sensor's white */
        const uint32_t *ra = a + (h / 2) * W2, *rb = b + (h / 2) * W2;
        int drop = 0, step = 0, drop0 = 0, nplat = 0, offw = 0, offc = 0, ya_plat = 0, yp = -1, yp0 = -1;
        int u0 = 999, u1 = -999, v0 = 999, v1 = -999;
        for (int x = 8; x < w - 8; x++) {
            uint32_t wa = ra[x / 2], wb = rb[x / 2];
            int ya = x & 1 ? wY1(wa) : wY0(wa), yb = x & 1 ? wY1(wb) : wY0(wb);
            if (yp >= 0) {
                drop += yb < yp - 1;
                step += abs(yb - yp) > 3;
                drop0 += ya < yp0 - 1;
            }
            yp = yb; yp0 = ya;
            if (roll_ev(x - 4, w) > ec + 0.05) {
                nplat++;
                offw += yb < ytop - 2 || yb > ytop + 1;
                offc += abs(wU(wb)) > 3 || abs(wV(wb)) > 3;
                if (wU(wb) < u0) u0 = wU(wb);
                if (wU(wb) > u1) u1 = wU(wb);
                if (wV(wb) < v0) v0 = wV(wb);
                if (wV(wb) > v1) v1 = wV(wb);
                if (ya > ya_plat) ya_plat = ya;
            }
        }
        printf("roll %s: knee %+.2f EV, sensor white %+.2f EV (want %+.2f), look white %+.2f EV, Y %d; below the knee %ld of "
               "%ld words changed; ramp drops %d (look as is %d), steps > 3: %d; past the sensor white %d px: off Y %d, U %d..%d "
               "V %d..%d (look as is ends at Y %d)\n", what, ek, ec, ec_want, ew, ytop, nd, nt, drop, drop0, step, nplat, offw,
               u0, u1, v0, v1, ya_plat);
        CHECK(fabs(ec - ec_want) < 0.02, "%s: sensor white %+.2f EV, want %+.2f", what, ec, ec_want);
        CHECK(ek < ec - 0.4 && nt > 1000 && nd == 0, "%s: below the knee %ld of %ld words changed", what, nd, nt);
        CHECK(drop == 0 && step == 0, "%s: ramp drops %d, steps %d", what, drop, step);
        /* the look's own white: neutral for the identity cube and sRGB; a film cube's white may carry its tint, one tint */
        int film = src == WF_DEV_CUBE && !ident;
        CHECK(nplat > 20 && offw == 0 && (film ? u1 - u0 <= 1 && v1 - v0 <= 1 : offc == 0),
              "%s: past the sensor white %d px: off Y %d, U %d..%d V %d..%d", what, nplat, offw, u0, u1, v0, v1);
        if (src == WF_DEV_CUBE && ident)
            CHECK(ya_plat < ytop - 10, "%s: the look as is already ends at %d (Y %d): nothing tested", what, ya_plat, ytop);
    }
    remove("ROLL.CUB");
    free(scr); free(a); free(b); free(raw); free(row);
}

/* rsrc: which buffer holds the shot (src/wf_rsrc.c) on the synthetic shot, both cameras. Buffers: A = this shot's raw;
   P = the "previous shot", the same scene 40 rows and 96 px away (the camera moved); M = another picture (A's rows
   mirrored); Z = noise; A2 = a copy of A (the other buffer still holding the same frame). The pointer is candidate 0. */
static void rs_case(const char *what, const uint32_t *cand, int nc, const uint32_t *yuv, const struct wf_rs_geom *g,
                    int want, int why)
{
    struct wf_rs_res r;
    int got = wf_rs_pick(cand, nc, yuv, g, 0, &r);
    printf("rsrc %-34s pick %2d (want %2d) %-7s blocks %3d scores", what, got, want, wf_rs_why(r.why), r.n);
    for (int k = 0; k < nc; k++) printf(" %5d", r.score[k]);
    printf("\n");
    CHECK(got == want && r.pick == want && r.why == why, "rsrc %s: pick %d (%s), want %d (%s)", what, got, wf_rs_why(r.why), want,
          wf_rs_why(why));
}

static void mode_rsrc(void)
{
    for (int cam = 0; cam < 2; cam++) {
        struct sc_shot s;
        sc_default(&s, 1024, 640, cam);
        CHECK(sc_render(&s) == 0, "render");
        size_t nb = (size_t)s.raw_h * s.pitch + 16;
        uint8_t *P = calloc(1, nb), *M = calloc(1, nb), *Z = malloc(nb), *A2 = malloc(nb);
        for (int y = 0; y < s.raw_h; y++) {
            int yp = y + 40, ym = s.raw_h - 2 - y;
            if (yp < s.raw_h) memcpy(P + (size_t)y * s.pitch + 12 * 14, s.raw + (size_t)yp * s.pitch, (size_t)s.pitch - 12 * 14);
            if (ym >= 0) memcpy(M + (size_t)y * s.pitch, s.raw + (size_t)ym * s.pitch, s.pitch);
        }
        for (size_t i = 0; i < nb; i++) Z[i] = (uint8_t)rnd();
        memcpy(A2, s.raw, nb);
        struct wf_rs_geom g = { s.pitch, s.ax0, s.ay0, s.w, s.h, s.black, 1 };
        const char *cn = cam ? "5D2" : "EOSM";
        char w[64];
        uint32_t a = (uint32_t)(uintptr_t)s.raw, p = (uint32_t)(uintptr_t)P, m = (uint32_t)(uintptr_t)M,
                 z = (uint32_t)(uintptr_t)Z, a2 = (uint32_t)(uintptr_t)A2;
        { uint32_t c[2] = { a, m };  snprintf(w, sizeof w, "%s pointer right", cn);        rs_case(w, c, 2, s.uyvy, &g, 0, WF_RS_OK); }
        { uint32_t c[2] = { p, a };  snprintf(w, sizeof w, "%s pointer at the previous shot", cn); rs_case(w, c, 2, s.uyvy, &g, 1, WF_RS_OK); }
        { uint32_t c[3] = { 0, a, m }; snprintf(w, sizeof w, "%s pointer 0 (formula, other)", cn); rs_case(w, c, 3, s.uyvy, &g, 1, WF_RS_OK); }
        { uint32_t c[2] = { z, m };  snprintf(w, sizeof w, "%s no match", cn);             rs_case(w, c, 2, s.uyvy, &g, -1, WF_RS_NOMATCH); }
        { uint32_t c[2] = { a, a2 }; snprintf(w, sizeof w, "%s both buffers this frame", cn); rs_case(w, c, 2, s.uyvy, &g, 0, WF_RS_OK); }
        { uint32_t c[2] = { 0, 0 };  snprintf(w, sizeof w, "%s no address", cn);           rs_case(w, c, 2, s.uyvy, &g, -1, WF_RS_NOCAND); }
        uint32_t *flat = malloc((size_t)s.w * s.h * 2);
        for (int i = 0; i < s.w * s.h / 2; i++) flat[i] = 0xf800f800u;   /* Y 248, chroma 0 */
        { uint32_t c[2] = { a, m };  snprintf(w, sizeof w, "%s white frame (few blocks)", cn); rs_case(w, c, 2, flat, &g, -1, WF_RS_FEW); }
        free(flat); free(P); free(M); free(Z); free(A2);
        sc_free(&s);
    }
}

/* pick DIR1 DIR2 ...: real pairs (dev_cr2.sh KEEP=1): Canon's frame and curve of DIR1 against the raw of every DIR */
static void mode_pick(int n, char **dirs)
{
    if (n > WF_RS_NCAND) n = WF_RS_NCAND;
    uint32_t cand[WF_RS_NCAND] = { 0 };
    uint8_t *raw[WF_RS_NCAND] = { 0 };
    int W = 0, H = 0, pitch = 0, jw = 0, jh = 0, dx, dy;
    double blk = 0, pk;
    for (int k = 0; k < n; k++) {
        char path[512];
        int iso, wb[4];
        FILE *f;
        snprintf(path, sizeof path, "%s/meta.txt", dirs[k]);
        if (!(f = fopen(path, "r"))) { CHECK(0, "%s: no meta.txt", dirs[k]); return; }
        CHECK(fscanf(f, "%d %d %d %d %lf %d %d %d %d", &W, &H, &pitch, &iso, &blk, &wb[0], &wb[1], &wb[2], &wb[3]) == 9, "meta");
        fclose(f);
        raw[k] = malloc((size_t)H * pitch + 16);
        CHECK(read_all(dirs[k], "raw.r14", raw[k], (size_t)H * pitch) == 0, "%s: raw.r14", dirs[k]);
        cand[k] = (uint32_t)(uintptr_t)raw[k];
    }
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/align.txt", dirs[0]);
    if (!(f = fopen(path, "r")) || fscanf(f, "%d %d %lf %d %d", &dx, &dy, &pk, &jw, &jh) != 5) { CHECK(0, "align.txt"); return; }
    fclose(f);
    uint32_t *yuv = malloc((size_t)jw * jh * 2);
    static uint32_t dec[1022];
    CHECK(read_all(dirs[0], "jpeg.uyvy", yuv, (size_t)jw * jh * 2) == 0, "jpeg.uyvy");
    int hd = read_all(dirs[0], "dec.bin", dec, sizeof dec) == 0;
    struct wf_rs_geom g = { pitch, 84, 64, jw, jh, (int)(blk + 0.5), 1 };
    struct wf_rs_res r;
    int got = wf_rs_pick(cand, n, yuv, &g, hd ? dec : 0, &r);
    printf("pick %s (curve %s): candidate %d, %s, blocks %d, scores", dirs[0], hd ? "dec.bin" : "code^2", got, wf_rs_why(r.why), r.n);
    for (int k = 0; k < n; k++) printf(" %d", r.score[k]);
    printf("\n");
    CHECK(got == 0, "%s: its own raw not picked (%d, %s)", dirs[0], got, wf_rs_why(r.why));
    free(yuv);
    for (int k = 0; k < n; k++) free(raw[k]);
}

static void mode_roll(void)
{
    for (int cam = 0; cam < 2; cam++) {
        char what[48];
        const char *cn = cam ? "5D2" : "EOSM";
        snprintf(what, sizeof what, "%s CUBE V-Log identity", cn); roll_case(cam, WF_DEV_CUBE, 1, what);
        snprintf(what, sizeof what, "%s CUBE film", cn); roll_case(cam, WF_DEV_CUBE, 0, what);
        snprintf(what, sizeof what, "%s SRGB", cn); roll_case(cam, WF_DEV_SRGB, 0, what);
        snprintf(what, sizeof what, "%s LOG", cn); roll_case(cam, WF_DEV_LOG, 0, what);
    }
}

/* ------------------------------------------------------------------ pack: the chain films' log pack (WFLOG.DAT) */
/* The format is written here from wf_dev.h's description, independently of tools/mk_wflog.py. Every u16 value survives
   a "%.6f" text cube exactly (the parser's Q24 and its u16 rounding), so the pack's tables must equal the text cube's. */
#define PK_TB WF_DEV_PK_BYTES
#define PK_P0 3                                  /* WF_PRINT_STOCK0 (wf_api.h): the first chain print variant */
static void pk_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void pk_put32(uint8_t *p, uint32_t v) { pk_put16(p, v & 0xffff); pk_put16(p + 2, v >> 16); }
static uint32_t pk_adler(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return b << 16 | a;
}
static uint32_t pk_fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
struct pk_ent { int id, var, flags; const char *code; int tab; };

/* header + index + the tables at 512-byte boundaries (as mk_wflog.py places them); returns the size */
static size_t pk_build(uint8_t *buf, uint8_t *const *tab, int ntab, const struct pk_ent *e, int ne, int curve, int feed)
{
    uint32_t doff = (32 + ne * 24 + 511) & ~511u, toff[8];
    size_t end = doff;
    for (int k = 0; k < ntab; k++) {
        toff[k] = (uint32_t)end;
        memcpy(buf + end, tab[k], PK_TB);
        end = (end + PK_TB + 511) & ~(size_t)511;
    }
    memset(buf, 0, doff);
    memcpy(buf, "WFLG", 4);
    pk_put16(buf + 4, 1); pk_put16(buf + 6, (uint32_t)ne); pk_put32(buf + 8, WF_DEV_G); pk_put32(buf + 12, PK_TB);
    pk_put32(buf + 16, 32); pk_put32(buf + 20, doff);
    buf[28] = (uint8_t)curve; buf[29] = (uint8_t)feed;
    for (int i = 0; i < ne; i++) {
        uint8_t *x = buf + 32 + 24 * i;
        x[0] = (uint8_t)e[i].id; x[1] = (uint8_t)e[i].var; x[2] = (uint8_t)e[i].flags;
        strncpy((char *)x + 4, e[i].code, 8);
        pk_put32(x + 12, toff[e[i].tab]); pk_put32(x + 16, PK_TB); pk_put32(x + 20, pk_adler(tab[e[i].tab], PK_TB));
    }
    pk_put32(buf + 24, pk_fnv(buf + 32, (size_t)ne * 24));
    return end;
}
static void pk_save(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf, 1, n, f); fclose(f); }
}
/* the same table as a text cube (wf_dev_build's own reader) */
static void pk_cube(const char *path, const uint8_t *t)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "TITLE \"wf_dev pack test\"\nLUT_3D_SIZE 33\n");
    for (int i = 0; i < WF_DEV_NODES; i++) {
        const uint8_t *p = t + 6 * i;
        fprintf(f, "%.6f %.6f %.6f\n", (p[0] | p[1] << 8) / 65535.0, (p[2] | p[3] << 8) / 65535.0,
                (p[4] | p[5] << 8) / 65535.0);
    }
    fclose(f);
}
static int pk_try(const char *pack, int id, int var, const char *code, struct wf_dev_pkinfo *pi)
{
    struct wf_ll_info inf;
    return wf_dev_build_pack(D, 0, pack, id, var, code, 0, &inf, pi);
}

/* wf_dev_build_gen / wf_dev_pack_samples: the samples of a table in memory, n nodes a call; fail: the call (1-based)
   that returns WF_LL_E_TRUNC instead */
struct pk_gen { const uint8_t *t; int node, calls, fail, maxn, bad; uint8_t *got; };
static int pk_fill(void *ctx, uint16_t *rgb, int n)
{
    struct pk_gen *g = ctx;
    if (++g->calls == g->fail)
        return WF_LL_E_TRUNC;
    if (n != WF_DEV_G * WF_DEV_G || g->node + n > WF_DEV_NODES)
        g->bad++;
    for (int i = 0; i < 3 * n && g->node * 3 + i < WF_DEV_NODES * 3; i++)
        rgb[i] = (uint16_t)(g->t[2 * (g->node * 3 + i)] | g->t[2 * (g->node * 3 + i) + 1] << 8);
    g->node += n;
    return WF_LL_OK;
}
static int pk_take(void *ctx, const uint8_t *le, int n)
{
    struct pk_gen *g = ctx;
    if (++g->calls == g->fail)
        return WF_LL_E_TRUNC;
    if (n < 1 || n > 1364 || g->node + n > WF_DEV_NODES)
        g->bad++;
    else
        memcpy(g->got + 6 * g->node, le, 6 * (size_t)n);
    if (n > g->maxn) g->maxn = n;
    g->node += n;
    return WF_LL_OK;
}

static void mode_pack(void)
{
    /* three tables: the synthetic film, the film with a hue shift and noise in the low bits, a B&W one (R' = G' = B') */
    uint8_t *tab[3];
    for (int k = 0; k < 3; k++) {
        tab[k] = malloc(PK_TB);
        for (int b = 0, i = 0; b < WF_DEV_G; b++)
            for (int g = 0; g < WF_DEV_G; g++)
                for (int r = 0; r < WF_DEV_G; r++, i++) {
                    double in[3] = { r / 32.0, g / 32.0, b / 32.0 }, o[3];
                    sc_film(in, o);
                    if (k == 1) { o[0] = clamp01(o[0] * 0.97 + 0.01); o[2] = clamp01(o[2] * 1.02); }
                    if (k == 2) o[0] = o[1] = o[2] = 0.299 * o[0] + 0.587 * o[1] + 0.114 * o[2];
                    for (int c = 0; c < 3; c++) {
                        int v = (int)lrint(clamp01(o[c]) * 65535.0) + (k == 1 ? (int)(rnd() % 7) - 3 : 0);
                        pk_put16(tab[k] + 6 * i + 2 * c, (uint32_t)(v < 0 ? 0 : v > 65535 ? 65535 : v));
                    }
                }
        char nm[32];
        snprintf(nm, sizeof nm, "PK%d.CUB", k);
        pk_cube(nm, tab[k]);
    }
    static const struct pk_ent ent[4] = {
        { 89, PK_P0, 0, "G00", 0 }, { 89, PK_P0 + 1, 0, "G00", 0 },   /* one table, two prints */
        { 90, PK_P0, 0, "G01", 1 }, { 91, PK_P0, 2, "G02", 2 } };
    uint8_t *buf = malloc(64 * 1024 + 3 * (PK_TB + 512)), *mut = malloc(64 * 1024 + 3 * (PK_TB + 512));
    size_t n = pk_build(buf, tab, 3, ent, 4, WF_LL_SLOG3, 1);
    pk_save("WFLOG.DAT", buf, n);

    /* every entry = its text cube, byte for byte: curves S-Log3 / LogC3, LUT EI 0 / +2, both cameras */
    struct wf_dev_tabs *C = malloc(sizeof *C);
    struct wf_ll_info inf, cinf;
    struct wf_dev_pkinfo pi, p0 = { 0 };
    static const int curves[2] = { WF_LL_SLOG3, WF_LL_LOGC3 };
    for (int cv = 0; cv < 2; cv++) {
        if (cv) { buf[28] = (uint8_t)curves[cv]; pk_save("WFLOG3.DAT", buf, n); buf[28] = WF_LL_SLOG3; }
        const char *pack = cv ? "WFLOG3.DAT" : "WFLOG.DAT";
        for (int i = 0; i < 4; i++)
            for (int cam = 0; cam < WF_DEV_NCAM; cam++)
                for (int ei = 0; ei <= 6; ei += 6) {
                    char nm[32];
                    snprintf(nm, sizeof nm, "PK%d.CUB", ent[i].tab);
                    memset(D, 0, sizeof *D);
                    memset(C, 0, sizeof *C);
                    int rc = wf_dev_build_pack(D, cam, pack, ent[i].id, ent[i].var, ent[i].code, ei, &inf, &pi);
                    int rcc = wf_dev_build(C, WF_DEV_CUBE, curves[cv], cam, nm, ei, 0, &cinf);
                    CHECK(rc == 0 && rcc == 0, "%s entry %d cam %d ei %d: rc %d (%s), cube rc %d", pack, i, cam, ei, rc,
                          wf_dev_pk_errstr(rc), rcc);
                    CHECK(!memcmp(D, C, sizeof *D), "%s entry %d cam %d ei %d: tables differ from the text cube's", pack,
                          i, cam, ei);
                    CHECK(D->n == WF_DEV_G && D->curve == curves[cv] && D->src == WF_DEV_CUBE && D->ei == ei,
                          "%s entry %d: n %d curve %d src %d ei %d", pack, i, D->n, D->curve, D->src, D->ei);
                    CHECK(inf.n == WF_DEV_G && inf.samples == WF_DEV_NODES && inf.bytes == PK_TB
                          && !strcmp(inf.title, ent[i].code), "%s entry %d info: n %d samples %d bytes %u title %s",
                          pack, i, inf.n, inf.samples, (unsigned)inf.bytes, inf.title);
                    CHECK(pi.curve == curves[cv] && pi.feed == 1 && pi.count == 4 && pi.flags == ent[i].flags,
                          "%s entry %d pkinfo: curve %d feed %d count %d flags %d", pack, i, pi.curve, pi.feed, pi.count,
                          pi.flags);
                    if (i == 0 && cam == 0 && ei == 0) p0 = pi;
                    if (i == 1 && cam == 0 && ei == 0)
                        CHECK(pi.off == p0.off && pi.sum == p0.sum, "one table for two prints: off %u / %u", pi.off, p0.off);
                }
    }
    printf("pack: %u bytes, 4 entries, 3 tables; G00 at %u, Adler-32 %08x, index FNV %08x\n", (unsigned)n, p0.off,
           p0.sum, p0.ixsum);

    /* refusals */
    CHECK(pk_try("NOPACK.DAT", 89, PK_P0, "G00", &pi) == WF_LL_E_OPEN, "missing pack");
    CHECK(pk_try("WFLOG.DAT", 92, PK_P0, "G03", &pi) == WF_DEV_PK_E_NOENT, "unlisted id");
    CHECK(pk_try("WFLOG.DAT", 89, PK_P0 + 2, "G00", &pi) == WF_DEV_PK_E_NOENT, "unlisted print");
    CHECK(pk_try("WFLOG.DAT", 89, PK_P0, "G01", &pi) == WF_DEV_PK_E_NOENT, "code of another row");
    CHECK(pk_try("WFLOG.DAT", 89, PK_P0, "G00XXXXXX", &pi) == WF_DEV_PK_E_NOENT, "code longer than 8");
    CHECK(wf_dev_build_pack(D, 2, "WFLOG.DAT", 89, PK_P0, "G00", 0, 0, 0) == WF_LL_E_MODE, "camera 2");
    struct { int at, val; int rc; const char *what; } mu[] = {
        { 0, 'X', WF_DEV_PK_E_FORMAT, "magic" }, { 4, 2, WF_DEV_PK_E_FORMAT, "version 2" },
        { 6, 0, WF_DEV_PK_E_FORMAT, "count 0" }, { 8, 17, WF_DEV_PK_E_FORMAT, "grid 17" },
        { 13, 0, WF_DEV_PK_E_FORMAT, "table size" }, { 16, 64, WF_DEV_PK_E_FORMAT, "index offset" },
        { 23, 0x7f, WF_DEV_PK_E_FORMAT, "data offset past the end" }, { 28, 0, WF_DEV_PK_E_FORMAT, "curve 0" },
        { 28, WF_LL_NCURVE + 1, WF_DEV_PK_E_FORMAT, "curve 6" }, { 24, -1, WF_DEV_PK_E_INDEX, "index checksum" },
        { 32 + 12 + 3, 0x7f, WF_DEV_PK_E_INDEX, "entry offset (index checksum fixed)" },
        { -1, 0, WF_DEV_PK_E_SUM, "one table byte" } };
    for (unsigned m = 0; m < sizeof mu / sizeof mu[0]; m++) {
        memcpy(mut, buf, n);
        if (mu[m].at < 0) mut[p0.off + 100003] ^= 0x10;
        else if (mu[m].val < 0) mut[mu[m].at] ^= 1;
        else mut[mu[m].at] = (uint8_t)mu[m].val;
        if (mu[m].at == 32 + 12 + 3) pk_put32(mut + 24, pk_fnv(mut + 32, 4 * 24));
        pk_save("BAD.DAT", mut, n);
        int rc = pk_try("BAD.DAT", 89, PK_P0, "G00", &pi);
        CHECK(rc == mu[m].rc, "%s: rc %d (%s), want %d", mu[m].what, rc, wf_dev_pk_errstr(rc), mu[m].rc);
    }
    pk_save("BAD.DAT", buf, n - 1000);           /* cut inside the last table (G02's) */
    int rc = pk_try("BAD.DAT", 91, PK_P0, "G02", &pi);
    CHECK(rc == WF_DEV_PK_E_INDEX, "truncated pack, last table: rc %d", rc);
    CHECK(pk_try("BAD.DAT", 90, PK_P0, "G01", &pi) == 0, "truncated pack, an earlier table still builds");
    pk_save("BAD.DAT", buf, 32 + 2 * 24);        /* cut inside the index */
    CHECK(pk_try("BAD.DAT", 89, PK_P0, "G00", &pi) == WF_DEV_PK_E_FORMAT, "pack cut in the index");
    CHECK(!strcmp(wf_dev_pk_errstr(WF_DEV_PK_E_SUM), "table checksum") && wf_dev_pk_errstr(WF_LL_E_OPEN)[0],
          "error texts");

    /* generated samples (the RAW knob tables, wf_rawb.c): the same samples make the same tables as the pack entry */
    int ngen = 0;
    for (int i = 0; i < 4; i++)
        for (int cam = 0; cam < WF_DEV_NCAM; cam++)
            for (int ei = 0; ei <= 6; ei += 6) {
                struct pk_gen g = { tab[ent[i].tab], 0, 0, 0, 0, 0, 0 };
                memset(D, 0, sizeof *D);
                memset(C, 0, sizeof *C);
                int rp = wf_dev_build_pack(D, cam, "WFLOG.DAT", ent[i].id, ent[i].var, ent[i].code, ei, &inf, &pi);
                int rg = wf_dev_build_gen(C, cam, WF_LL_SLOG3, ei, pk_fill, &g, &cinf);
                CHECK(rp == 0 && rg == 0 && g.node == WF_DEV_NODES && g.calls == WF_DEV_G && !g.bad,
                      "gen entry %d cam %d ei %d: rc %d / %d, %d nodes in %d calls (%d bad)", i, cam, ei, rp, rg, g.node,
                      g.calls, g.bad);
                CHECK(!memcmp(D, C, sizeof *D), "gen entry %d cam %d ei %d: tables differ from the pack's", i, cam, ei);
                CHECK(cinf.n == WF_DEV_G && cinf.samples == WF_DEV_NODES && cinf.bytes == PK_TB,
                      "gen entry %d info: n %d samples %d bytes %u", i, cinf.n, cinf.samples, (unsigned)cinf.bytes);
                ngen++;
            }
    {
        struct pk_gen g = { tab[1], 0, 0, 3, 0, 0, 0 };
        CHECK(wf_dev_build_gen(C, 0, WF_LL_SLOG3, 0, pk_fill, &g, &cinf) == WF_LL_E_TRUNC && g.calls == 3,
              "gen: the fill's code stops the build (%d calls)", g.calls);
        CHECK(wf_dev_build_gen(C, 0, 0, 0, pk_fill, &g, 0) == WF_LL_E_MODE
              && wf_dev_build_gen(C, 0, WF_LL_NCURVE + 1, 0, pk_fill, &g, 0) == WF_LL_E_MODE
              && wf_dev_build_gen(C, WF_DEV_NCAM, WF_LL_SLOG3, 0, pk_fill, &g, 0) == WF_LL_E_MODE
              && wf_dev_build_gen(C, 0, WF_LL_SLOG3, 0, 0, &g, 0) == WF_LL_E_MODE, "gen: curve / camera / no fill");
    }
    /* an entry's samples as they are */
    {
        uint8_t *got = malloc(PK_TB);
        struct pk_gen g = { 0, 0, 0, 0, 0, 0, got };
        memset(got, 0, PK_TB);
        int rs = wf_dev_pack_samples("WFLOG.DAT", 90, PK_P0, "G01", pk_take, &g, &pi);
        CHECK(rs == 0 && g.node == WF_DEV_NODES && !g.bad && g.maxn == 1364 && !memcmp(got, tab[1], PK_TB)
              && pi.sum == pk_adler(tab[1], PK_TB) && pi.curve == WF_LL_SLOG3 && pi.count == 4,
              "samples: rc %d, %d nodes in %d calls (largest %d, %d bad), bytes %s, sum %08x", rs, g.node, g.calls,
              g.maxn, g.bad, memcmp(got, tab[1], PK_TB) ? "differ" : "equal", pi.sum);
        memcpy(mut, buf, n);
        mut[pi.off + 100003] ^= 0x10;
        pk_save("BAD.DAT", mut, n);
        g = (struct pk_gen){ 0, 0, 0, 0, 0, 0, got };
        rs = wf_dev_pack_samples("BAD.DAT", 90, PK_P0, "G01", pk_take, &g, &pi);
        CHECK(rs == WF_DEV_PK_E_SUM && g.node == WF_DEV_NODES, "samples: a flipped table byte -> rc %d after %d nodes", rs,
              g.node);
        g = (struct pk_gen){ 0, 0, 0, 5, 0, 0, got };
        rs = wf_dev_pack_samples("WFLOG.DAT", 90, PK_P0, "G01", pk_take, &g, &pi);
        CHECK(rs == WF_LL_E_TRUNC && g.calls == 5, "samples: the callback's code stops the read (rc %d, %d calls)", rs,
              g.calls);
        CHECK(wf_dev_pack_samples("NOPACK.DAT", 90, PK_P0, "G01", pk_take, &g, &pi) == WF_LL_E_OPEN
              && wf_dev_pack_samples("WFLOG.DAT", 92, PK_P0, "G03", pk_take, &g, &pi) == WF_DEV_PK_E_NOENT,
              "samples: missing pack, unlisted entry");
        free(got);
    }
    printf("pack: generated samples = the pack's tables on %d builds; samples streamed as they are\n", ngen);
    free(C); free(buf); free(mut);
    for (int k = 0; k < 3; k++) free(tab[k]);
}

/* packreal PACK [L33 ID VARIANT CODE]..: every entry of a real WFLOG.DAT (tools/mk_wflog.py) builds, and each named
   entry = the text cube this test writes from its .L33 (exact u16 values), byte for byte, on both cameras. Not the
   generator's own KEEP_CUBE=1 cube: its 6-decimal chain values go through the parser's Q24 and come out 1 / 65535 off
   the .L33 in 34..76 samples per table (double rounding; one node byte of G38P15 shows it). */
static void mode_packreal(int argc, char **argv)
{
    FILE *f = fopen(argv[0], "rb");
    uint8_t h[32];
    CHECK(f && fread(h, 1, 32, f) == 32, "read %s", argv[0]);
    if (!f) return;
    int n = h[6] | h[7] << 8, curve = h[28], bad = 0;
    uint8_t *ix = malloc((size_t)n * 24);
    CHECK(fread(ix, 24, (size_t)n, f) == (size_t)n, "index");
    fclose(f);
    struct wf_ll_info inf;
    struct wf_dev_pkinfo pi;
    for (int i = 0; i < n; i++) {
        char code[9];
        memcpy(code, ix + 24 * i + 4, 8);
        code[8] = 0;
        int rc = wf_dev_build_pack(D, i & 1, argv[0], ix[24 * i], ix[24 * i + 1], code, 0, &inf, &pi);
        if (rc && bad++ < 5)
            printf("entry %d (%s v%d): rc %d %s\n", i, code, ix[24 * i + 1], rc, wf_dev_pk_errstr(rc));
    }
    CHECK(!bad, "%d of %d entries failed", bad, n);
    printf("packreal: %d entries built (curve %d, feed %d), %d failed\n", n, curve, h[29], bad);
    struct wf_dev_tabs *C = malloc(sizeof *C);
    uint8_t *l33 = malloc(PK_TB + 1);
    for (int a = 1; a + 3 < argc; a += 4) {
        FILE *g = fopen(argv[a], "rb");
        size_t got = g ? fread(l33, 1, PK_TB + 1, g) : 0;
        if (g) fclose(g);
        CHECK(got == PK_TB, "%s: %u bytes", argv[a], (unsigned)got);
        pk_cube("PKREAL.CUB", l33);
        for (int cam = 0; cam < WF_DEV_NCAM; cam++) {
            memset(D, 0, sizeof *D);
            memset(C, 0, sizeof *C);
            int rc = wf_dev_build_pack(D, cam, argv[0], atoi(argv[a + 1]), atoi(argv[a + 2]), argv[a + 3], 0, &inf, &pi);
            int rcc = wf_dev_build(C, WF_DEV_CUBE, curve, cam, "PKREAL.CUB", 0, 0, 0);
            CHECK(rc == 0 && rcc == 0 && !memcmp(D, C, sizeof *D), "%s cam %d: rc %d cube rc %d, %s", argv[a + 3], cam,
                  rc, rcc, rc || rcc ? "-" : "tables differ from the cube's");
            int nd = 0, first = -1;
            for (unsigned k = 0; k < sizeof *D; k++)
                if (((uint8_t *)D)[k] != ((uint8_t *)C)[k]) { nd++; if (first < 0) first = (int)k; }
            printf("packreal %s v%s cam %d: %s (offset %u, Adler-32 %08x; %d bytes differ, first at %d of %u)\n",
                   argv[a + 3], argv[a + 2], cam, !rc && !rcc && !nd ? "= its text cube" : "DIFFERS", pi.off, pi.sum,
                   nd, first, (unsigned)sizeof *D);
        }
    }
    free(l33);
    free(C);
    free(ix);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "px";
    T = malloc(sizeof *T);
    D = malloc(sizeof *D);
    if (!strcmp(mode, "px")) mode_px();
    else if (!strcmp(mode, "frame")) { run_frame(0, 1024, 640, 0); run_frame(1, 1024, 640, 0); }
    else if (!strcmp(mode, "full")) run_frame(0, 5184, 3456, argc > 2 ? argv[2] : 0);
    else if (!strcmp(mode, "bad")) mode_bad();
    else if (!strcmp(mode, "span")) mode_span();
    else if (!strcmp(mode, "hue")) mode_hue();
    else if (!strcmp(mode, "roll")) mode_roll();
    else if (!strcmp(mode, "rsrc")) mode_rsrc();
    else if (!strcmp(mode, "pack")) mode_pack();
    else if (!strcmp(mode, "packreal") && argc > 2) mode_packreal(argc - 2, argv + 2);
    else if (!strcmp(mode, "pick") && argc > 3) mode_pick(argc - 2, argv + 2);
    else if (!strcmp(mode, "leica") && argc > 3)
        mode_leica(argv[2], argv[3], argc > 5 ? atoi(argv[4]) : 0, argc > 5 ? atoi(argv[5]) : 0);
    else if (!strcmp(mode, "cr2") && argc > 2) mode_cr2(argv[2], argc > 4 ? atoi(argv[3]) : 84, argc > 4 ? atoi(argv[4]) : 64);
    else if (!strcmp(mode, "look") && argc > 4)
        mode_look(argv[2], argv[3], atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 128, argc > 6 ? atoi(argv[6]) : 0,
                  argc > 7 ? atoi(argv[7]) : -2);
    else { printf("unknown mode %s\n", mode); return 2; }
    printf("RESULT dev %s: %d pass, %d fail\n", mode, np, nf);
    return nf != 0;
}
