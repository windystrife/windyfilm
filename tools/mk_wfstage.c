/* mk_wfstage.c - WindyStrife, host only: the two halves of the film-stage chain as card data (DESIGN 1a, src/wf_stagek.h).
 *
 * Built and run by tools/mk_wfstage.py, never linked into the module. It links the chain in the port tree (FR_SRC) the way
 * tools/mk_genesis_cube.c does and drives it as a black box: inputs, the exposure offset and the printer offsets go in,
 * the outputs and the values the chain's own trace prints come out (mk_wfstage_hook.h). No formula of the chain is
 * written here; every table is a measurement of the chain.
 *
 *   mk_wfstage neg NEG OUT      negative half of stock NEG (struct wf_sg_neg)
 *   mk_wfstage print PRN OUT    print half of print stock PRN (struct wf_sg_prn)
 *   mk_wfstage pairs OUT        fit / black / white of all 46 x 16 pairs, as tools/mk_genesis_cube.c places them
 *                               (int32 fit Q16 stops, int32 black Q16, int32 white Q16 per pair, negative-major)
 *   mk_wfstage check NEG PRN NEGFILE PRNFILE [T17]
 *                               src/wf_stagek.c composed at several knob sets against the chain run directly at the
 *                               4913 nodes (and, with T17, the default composition against the shipped table); each
 *                               knob set also composed from its key alone (as the module does) must give the same words
 *   mk_wfstage shipped NEG PRN NEGFILE PRNFILE T17
 *                               one line: the default composition against a shipped table
 *   mk_wfstage dat DAT LIST     WFSTAGE.DAT read back as the module reads it; every entry at default knobs against
 *                               its shipped table (LIST: the .T17 paths in index order), Film exp +1 lifts grey
 *
 * How each half is measured (the chain's axes, in its own terms):
 *   A1   density per channel over the exposure axis x 0..1: linear input 0 (x = 0) and an exposure sweep (x = stops/20).
 *   X    the exposure axis value of a display code: the exposure that brings that input's green density to a reference
 *        density on the steepest part of A1 (bisection), x = xref - stops/20. Also a log-spaced linear grid, used to
 *        drive A2.
 *   A2   for each node of a 33^3 grid over the density each channel reaches: the input whose densities are the node's
 *        (A1 inverted per channel, then the linear grid), printing density read from the trace with the printer at 0
 *        (so the chain's auto-balance is inside), at interlayer 0 (base) and 1 (base + quad).
 *   B1   print density per channel over printing density 0..4: a fixed input, the printing density set by the printer
 *        offsets.
 *   B2   for each node of a 33^3 grid over the print density each channel reaches: B1 inverted, set by the printer
 *        offsets, the chain's raw output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "fr_glook.h"
#include "wf_stagek.h"

static uint16_t mk_pd[WF_SG_NODES * 3];             /* wf_sg_compose scratch */

/* ---- the chain's trace, read at full precision ---- */
static double cap_dens[3], cap_pd[3], cap_d[3];
int mk_wfstage_trace(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int i, r = 0;
    va_start(ap, fmt);
    if (!strncmp(fmt, "film: negd", 10)) {
        for (i = 0; i < 3; i++) cap_dens[i] = va_arg(ap, double);
        for (i = 0; i < 3; i++) cap_pd[i] = va_arg(ap, double);
    } else if (!strncmp(fmt, "film: printD", 12)) {
        for (i = 0; i < 3; i++) cap_d[i] = va_arg(ap, double);
    } else if (!strncmp(fmt, "film: ", 6)) {
    } else
        r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

static void run(const double lin[3], const gl_film_params *p, double out[3])
{
    int32_t in[3], o[3];
    int c;
    for (c = 0; c < 3; c++) {
        double v = lin[c] * GL_ONE;
        in[c] = v < 0 ? 0 : v > 2.0e9 ? 2000000000 : (int32_t)llround(v);
    }
    gl_film_lin(in, p, o);
    for (c = 0; c < 3; c++) out[c] = o[c] / (double)GL_ONE;
}
static int32_t Q(double v) { return (int32_t)llround(v * GL_ONE); }
static double dlin(double q) { int32_t l = gl_disp_to_lin(Q(q)); return l < 0 ? 0 : l / (double)GL_ONE; }

/* ---- 1D helper over tables sampled on 0..1 ---- */
static double i1inv(const double *t, int n, double v)   /* monotonic, either direction */
{
    int k;
    double s = t[n - 1] < t[0] ? -1 : 1;
    v *= s;
    if (v <= s * t[0]) return 0;
    if (v >= s * t[n - 1]) return 1;
    for (k = 0; k < n - 1; k++)
        if (s * t[k + 1] >= v) {
            double d = s * (t[k + 1] - t[k]);
            return (k + (d > 0 ? (v - s * t[k]) / d : 0)) / (n - 1);
        }
    return 1;
}
static uint16_t u16q(double v) { long r = lround(v); return r < 0 ? 0 : r > 65535 ? 65535 : (uint16_t)r; }
static int16_t s16q(double v) { long r = lround(v); return r < -32768 ? -32768 : r > 32767 ? 32767 : (int16_t)r; }

/* ---- the placement of tools/mk_genesis_cube.c: exposure for 18 % grey -> 0.18 between the pinned ends ---- */
static void place(gl_film_params p, int32_t *fit, int32_t *bk, int32_t *wt)
{
    int32_t in[3], out[3], wl = gl_disp_to_lin(GL_ONE) < 0 ? 0 : gl_disp_to_lin(GL_ONE);
    double b, w, bestd = 1e9;
    int e, best = 0;
    in[0] = in[1] = in[2] = 0; gl_film_lin(in, &p, out); b = out[1] / (double)GL_ONE;
    for (e = -600; e <= 600; e += 5) {
        double gv, pos, d;
        p.exposure = (int32_t)((int64_t)e * GL_ONE / 100);
        in[0] = in[1] = in[2] = (int32_t)(0.18 * GL_ONE); gl_film_lin(in, &p, out); gv = out[1] / (double)GL_ONE;
        in[0] = in[1] = in[2] = wl; gl_film_lin(in, &p, out); w = out[1] / (double)GL_ONE;
        pos = w > b ? (gv - b) / (w - b) : 0; d = fabs(pos - 0.18);
        if (d < bestd) { bestd = d; best = p.exposure; }
    }
    p.exposure = best;
    *fit = best;
    in[0] = in[1] = in[2] = 0; gl_film_lin(in, &p, out); *bk = out[1];
    in[0] = in[1] = in[2] = wl; gl_film_lin(in, &p, out); *wt = out[1];
}

/* ---- negative half ---- */
#define NL 2049
static double A1d[3][WF_SG_N1], xref, dref, Lg[NL], Lx[NL];

/* exposure axis value of linear L (see the header) */
static double xof(double L, gl_film_params p)
{
    double lin[3] = { L, L, L }, o[3], a = -45, b = 45;
    int it;
    for (it = 0; it < 48; it++) {
        double m = 0.5 * (a + b);
        p.exposure = Q(m);
        run(lin, &p, o);
        if (cap_dens[1] >= dref) b = m; else a = m;
    }
    return xref - b / 20.0;
}
static double lin_for_x(double x)
{
    int j;
    if (x <= Lx[0]) return 0;
    if (x >= Lx[NL - 1]) return Lg[NL - 1];
    for (j = 0; j < NL - 1; j++)
        if (Lx[j + 1] >= x) {
            double d = Lx[j + 1] - Lx[j], f = d > 0 ? (x - Lx[j]) / d : 0;
            return pow(10, log10(Lg[j]) + f * (log10(Lg[j + 1]) - log10(Lg[j])));
        }
    return Lg[NL - 1];
}

static int make_neg(int neg, struct wf_sg_neg *N)
{
    gl_film_params p0, p;
    double lin[3] = { 0, 0, 0 }, o[3], best = -1, perr = 0;
    int c, k, i, j, mono = 1, ax[3];
    gl_film_defaults(&p0);
    gl_film_stock(neg);
    gl_print_stock(1);
    memset(N, 0, sizeof *N);
    p = p0;
    for (k = 0; k < WF_SG_N1; k++) {
        p.exposure = Q(20.0 * k / (WF_SG_N1 - 1));
        run(lin, &p, o);
        for (c = 0; c < 3; c++) {
            A1d[c][k] = cap_dens[c];
            N->A1[c][k] = u16q(cap_dens[c] * 16384);
            if (k && A1d[c][k] < A1d[c][k - 1]) mono = 0;
        }
    }
    if (!mono) { fprintf(stderr, "neg %d: density not monotonic in exposure\n", neg); return 1; }
    for (k = 1; k < WF_SG_N1 - 1; k++)
        if (A1d[1][k + 1] - A1d[1][k] > best) { best = A1d[1][k + 1] - A1d[1][k]; xref = (k + 0.5) / (WF_SG_N1 - 1); }
    p.exposure = Q(20.0 * xref);
    run(lin, &p, o);
    dref = cap_dens[1];
    p = p0;
    for (j = 0; j < NL; j++) { Lg[j] = pow(10, -4 + 7.3 * j / (NL - 1)); Lx[j] = xof(Lg[j], p); }
    for (i = 0; i < WF_SG_NX; i++) N->X[i] = u16q(xof(dlin(i / 4.0 / 255.0), p) * 65536);
    N->X[WF_SG_NX] = N->X[WF_SG_NX - 1];
    /* A2 axes: the quantised A1 range of each channel */
    for (c = 0; c < 3; c++) {
        int lo = 65535, hi = 0;
        for (k = 0; k < WF_SG_N1; k++) { if (N->A1[c][k] < lo) lo = N->A1[c][k]; if (N->A1[c][k] > hi) hi = N->A1[c][k]; }
        if (hi <= lo) { fprintf(stderr, "neg %d: channel %d has no density range\n", neg, c); return 1; }
        N->lo[c] = lo;
        N->sc[c] = (int32_t)llround((WF_SG_NA - 1) * 1073741824.0 / (hi - lo));
    }
    {
        gl_film_params pa = p0;
        double vmax = 0;
        pa.exposure = 0;                         /* printer 0: the auto-balance stays inside the base */
        for (ax[0] = 0; ax[0] < WF_SG_NA; ax[0]++)
            for (ax[1] = 0; ax[1] < WF_SG_NA; ax[1]++)
                for (ax[2] = 0; ax[2] < WF_SG_NA; ax[2]++) {
                    int nd = (ax[0] * WF_SG_NA + ax[1]) * WF_SG_NA + ax[2];
                    double t[3], li[3], b0[3];
                    for (c = 0; c < 3; c++) {
                        /* the node's density: the inverse of the module's grid = ((d - lo) * sc) >> 14 */
                        t[c] = (N->lo[c] + ax[c] * 1073741824.0 / N->sc[c]) / 16384.0;
                        li[c] = lin_for_x(i1inv(A1d[c], WF_SG_N1, t[c]));
                    }
                    pa.iie = 0; run(li, &pa, o);
                    for (c = 0; c < 3; c++) {
                        b0[c] = cap_pd[c];
                        if (fabs(cap_dens[c] - t[c]) > perr) perr = fabs(cap_dens[c] - t[c]);
                    }
                    pa.iie = GL_ONE; run(li, &pa, o);
                    for (c = 0; c < 3; c++) {
                        double q = cap_pd[c] - b0[c];
                        N->A2[nd][c] = s16q(b0[c] * 4096);
                        N->A2[nd][3 + c] = s16q(q * 4096);
                        if (fabs(b0[c]) > vmax) vmax = fabs(b0[c]);
                        if (fabs(q) > vmax) vmax = fabs(q);
                    }
                }
        if (vmax >= 7.99) { fprintf(stderr, "neg %d: printing density %.3f does not fit Q12\n", neg, vmax); return 1; }
        fprintf(stderr, "neg %2d: density R %.3f G %.3f B %.3f, x(display white) %.4f, node placement %.5f, |pd| <= %.3f\n",
                neg, A1d[0][WF_SG_N1 - 1], A1d[1][WF_SG_N1 - 1], A1d[2][WF_SG_N1 - 1], N->X[WF_SG_NX - 1] / 65536.0, perr, vmax);
    }
    return 0;
}

/* ---- print half ---- */
static int make_prn(int prn, struct wf_sg_prn *P)
{
    gl_film_params p0, pb;
    double lin[3] = { 0, 0, 0 }, o[3], pd0[3], B1d[3][WF_SG_N1], perr = 0;
    int c, k, ax[3];
    gl_film_defaults(&p0);
    gl_film_stock(0);
    gl_print_stock(prn);
    memset(P, 0, sizeof *P);
    pb = p0;
    run(lin, &pb, o);
    for (c = 0; c < 3; c++) pd0[c] = cap_pd[c];
    for (k = 0; k < WF_SG_N1; k++) {
        double P4 = 4.0 * k / (WF_SG_N1 - 1);
        for (c = 0; c < 3; c++) pb.printer[c] = Q(pd0[c] - P4);
        run(lin, &pb, o);
        for (c = 0; c < 3; c++) {
            B1d[c][k] = cap_d[c];
            P->B1[c][k] = u16q(cap_d[c] * 4096);
        }
    }
    for (c = 0; c < 3; c++) {
        int lo = 65535, hi = 0;
        for (k = 0; k < WF_SG_N1; k++) { if (P->B1[c][k] < lo) lo = P->B1[c][k]; if (P->B1[c][k] > hi) hi = P->B1[c][k]; }
        if (hi <= lo) { fprintf(stderr, "print %d: channel %d has no density range\n", prn, c); return 1; }
        P->lo[c] = lo;
        P->sc[c] = (int32_t)llround((WF_SG_NB - 1) * 268435456.0 / (hi - lo));
    }
    for (ax[0] = 0; ax[0] < WF_SG_NB; ax[0]++)
        for (ax[1] = 0; ax[1] < WF_SG_NB; ax[1]++)
            for (ax[2] = 0; ax[2] < WF_SG_NB; ax[2]++) {
                int nd = (ax[0] * WF_SG_NB + ax[1]) * WF_SG_NB + ax[2];
                double t[3];
                pb = p0;
                for (c = 0; c < 3; c++) {
                    t[c] = (P->lo[c] + ax[c] * 268435456.0 / P->sc[c]) / 4096.0;
                    pb.printer[c] = Q(pd0[c] - 4.0 * i1inv(B1d[c], WF_SG_N1, t[c]));
                }
                run(lin, &pb, o);
                for (c = 0; c < 3; c++) {
                    if (fabs(cap_d[c] - t[c]) > perr) perr = fabs(cap_d[c] - t[c]);
                    P->B2[nd][c] = u16q(o[c] * 65536);
                }
            }
    fprintf(stderr, "print %2d: density R %.3f..%.3f, node placement %.5f\n", prn, P->lo[0] / 4096.0,
            (P->lo[0] + (WF_SG_NB - 1) * 268435456.0 / P->sc[0]) / 4096.0, perr);
    return 0;
}

static int wr(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n) { perror(path); if (f) fclose(f); return 1; }
    return fclose(f) ? 1 : 0;
}
static void *rd(const char *path, size_t n)
{
    FILE *f = fopen(path, "rb");
    void *p = malloc(n);
    if (!f || !p || fread(p, 1, n, f) != n) { fprintf(stderr, "%s: cannot read %u bytes\n", path, (unsigned)n); exit(1); }
    fclose(f);
    return p;
}

/* ---- check: the module's composition against the chain ---- */
static void ycc(double r, double g, double b, double *o)
{
    o[0] = 0.299 * r + 0.587 * g + 0.114 * b;
    o[1] = -0.168736 * r - 0.331264 * g + 0.5 * b;
    o[2] = 0.5 * r - 0.418688 * g - 0.081312 * b;
}
static int check(int neg, int prn, const char *nf, const char *pf, const char *t17)
{
    struct wf_sg_neg *N = rd(nf, sizeof *N);
    struct wf_sg_prn *P = rd(pf, sizeof *P);
    uint32_t *T = malloc(4913 * 4), *S = t17 ? rd(t17, 4913 * 4) : 0;
    gl_film_params p0, p;
    struct wf_sg_pair pr;
    int32_t fit, bk, wt;
    int ki, c, fail = 0;
    static const struct { const char *name; struct wf_sg_knobs k; } ks[] = {
        { "default", { 0, 0, 0, { 0, 0, 0 }, 4 } },
        { "exp +1", { 3, 0, 0, { 0, 0, 0 }, 4 } },
        { "exp -2", { -6, 0, 0, { 0, 0, 0 }, 4 } },
        { "exp +2", { 6, 0, 0, { 0, 0, 0 }, 4 } },
        { "push +1", { 0, 3, 0, { 0, 0, 0 }, 4 } },
        { "push -1 comp", { 0, -3, 1, { 0, 0, 0 }, 4 } },
        { "printer R +12", { 0, 0, 0, { 12, 0, 0 }, 4 } },
        { "printer G -12", { 0, 0, 0, { 0, -12, 0 }, 4 } },
        { "printer B +6", { 0, 0, 0, { 0, 0, 6 }, 4 } },
        { "interlayer 0", { 0, 0, 0, { 0, 0, 0 }, 0 } },
        { "interlayer 200", { 0, 0, 0, { 0, 0, 0 }, 8 } },
        { "mixed", { 2, -1, 0, { 3, -2, 4 }, 6 } },
    };
    gl_film_defaults(&p0);
    gl_film_stock(neg);
    gl_print_stock(prn);
    place(p0, &fit, &bk, &wt);
    pr.fit = fit; pr.bk = bk; pr.inv = (uint32_t)llround(4294967296.0 / (wt - bk));
    printf("pair neg %d print %d: fit %+.2f stops, ends %.4f %.4f\n", neg, prn, fit / 65536.0, bk / 65536.0, wt / 65536.0);
    for (ki = 0; ki < (int)(sizeof ks / sizeof ks[0]); ki++) {
        const struct wf_sg_knobs *k = &ks[ki].k;
        double mx[3] = { 0, 0, 0 }, sum = 0, gmx = 0;
        int n, hist[41] = { 0 }, cnt = 0, p99 = 0, acc = 0;
        wf_sg_compose(N, P, &pr, k, 0, mk_pd, T);
        {
            /* the module composes from the key alone (wf_sg_unkey): it must give the very same words */
            static uint32_t T2[4913];
            struct wf_sg_knobs ku;
            wf_sg_unkey(wf_sg_key(k), &ku);
            wf_sg_compose(N, P, &pr, &ku, 0, mk_pd, T2);
            if (memcmp(T, T2, sizeof T2)) {
                printf("  %-15s: the key's knobs compose to other words\n", ks[ki].name);
                fail = 1;
            }
        }
        p = p0;
        /* Exp comp: the chain's exposure becomes -push, which would also drop the fitted placement (it rides on the
         * exposure); the module keeps the placement, so the reference is the placement alone */
        p.exposure = fit + (k->comp ? 0 : Q(k->fexp / 3.0));
        p.push_pull = k->comp ? 0 : k->push * 6554;
        for (c = 0; c < 3; c++) p.printer[c] = (k->prn[c] * 104858) >> 6;
        p.iie = k->iie * 16384;
        for (n = 0; n < 4913; n++) {
            int Y = n / 289 * 16, U = n / 17 % 17 * 16, V = n % 17 * 16, ing = 1;
            double rgb[3], o[3], ref[3], got[3];
            if (Y > 255) Y = 255;
            if (U > 255) U = 255;
            if (V > 255) V = 255;
            rgb[0] = Y + 1.402 * (V - 128); rgb[1] = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128); rgb[2] = Y + 1.772 * (U - 128);
            for (c = 0; c < 3; c++) {
                if (rgb[c] < 0 || rgb[c] > 255) ing = 0;
                rgb[c] = rgb[c] < 0 ? 0 : rgb[c] > 255 ? 255 : rgb[c];
                rgb[c] = dlin(rgb[c] / 255.0);
            }
            run(rgb, &p, o);
            for (c = 0; c < 3; c++) {
                double v = (o[c] - bk / 65536.0) / ((wt - bk) / 65536.0);
                o[c] = (v < 0 ? 0 : v > 1 ? 1 : v) * 255;
            }
            ycc(o[0], o[1], o[2], ref);
            got[0] = (T[n] >> 8) & 255; got[1] = (int8_t)(T[n] & 255); got[2] = (int8_t)((T[n] >> 16) & 255);
            for (c = 0; c < 3; c++) {
                double d = fabs(got[c] - ref[c]);
                if (d > mx[c]) mx[c] = d;
                if (ing && d > gmx) gmx = d;
                sum += d; cnt++;
                hist[d > 40 ? 40 : (int)d]++;
            }
        }
        for (n = 0; n <= 40; n++) { acc += hist[n]; if (acc >= 0.99 * cnt) { p99 = n + 1; break; } }
        printf("  %-15s vs chain: Y max %5.2f  U max %5.2f  V max %5.2f  mean %.3f  p99 < %d  in-gamut max %.2f\n",
               ks[ki].name, mx[0], mx[1], mx[2], sum / cnt, p99, gmx);
        if (ki == 0 && S) {
            double sm = 0, m2[3] = { 0, 0, 0 };
            for (n = 0; n < 4913; n++) {
                int a[3] = { (T[n] >> 8) & 255, (int8_t)(T[n] & 255), (int8_t)((T[n] >> 16) & 255) };
                int b[3] = { (S[n] >> 8) & 255, (int8_t)(S[n] & 255), (int8_t)((S[n] >> 16) & 255) };
                for (c = 0; c < 3; c++) { double d = abs(a[c] - b[c]); sm += d; if (d > m2[c]) m2[c] = d; }
            }
            printf("  default vs shipped %s: Y max %.0f U max %.0f V max %.0f mean %.3f\n", t17, m2[0], m2[1], m2[2], sm / (3 * 4913.0));
        }
        if (mx[0] > 32 || sum / cnt > 1.5) fail = 1;
    }
    return fail;
}

/* dat DAT LIST: WFSTAGE.DAT read the way the module reads it (header, index, blob checksums), every entry composed at
 * default knobs from its blobs and stored pair and held against its shipped table (LIST: one .T17 path per entry, in
 * index order, written by tools/mk_wfstage.py); plus Film exp +1 on each entry must lift the neutral axis */
static uint32_t dat_fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
static int dat(const char *path, const char *list)
{
    FILE *f = fopen(path, "rb"), *l = fopen(list, "r");
    if (!f || !l) { fprintf(stderr, "dat: cannot open %s / %s\n", path, list); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    uint8_t *b = malloc(sz);
    fseek(f, 0, SEEK_SET);
    if (fread(b, 1, sz, f) != (size_t)sz) return 1;
    fclose(f);
    uint32_t u[16];
    uint16_t h[8];
    memcpy(h, b + 4, 16);
    memcpy(u, b + 20, 44);
    int n = h[1], nn = h[2], np = h[3], fail = 0, nbig = 0, e;
    uint32_t io = u[2], no = u[3], po = u[4], dof = u[5], pof = u[6];
    if (memcmp(b, "WFSG", 4) || h[0] != 1 || h[4] != WF_SG_NA || h[5] != WF_SG_NB || h[6] != WF_SG_N1 || h[7] != WF_SG_NX
        || u[0] != sizeof(struct wf_sg_neg) || u[1] != sizeof(struct wf_sg_prn) || io != 64 || no != io + 28u * n
        || po != no + 4u * nn || dof % 4 || pof != dof + nn * u[0] || u[8] != (uint32_t)sz
        || u[7] != dat_fnv(b + io, po + 4u * np - io)) {
        printf("dat: header / index checksum bad\n");
        return 1;
    }
    for (e = 0; e < nn; e++)
        if (dat_fnv(b + dof + e * u[0], u[0]) != *(uint32_t *)(b + no + 4 * e)) { printf("dat: negative %d bad\n", e); return 1; }
    for (e = 0; e < np; e++)
        if (dat_fnv(b + pof + e * u[1], u[1]) != *(uint32_t *)(b + po + 4 * e)) { printf("dat: print %d bad\n", e); return 1; }
    double wmean = 0, smean = 0;
    int wmax = 0;
    char wname[300] = "", t17[300];
    for (e = 0; e < n; e++) {
        const uint8_t *x = b + io + 28 * e;
        struct wf_sg_pair pr;
        struct wf_sg_knobs k0 = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF }, k1 = { 3, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
        static uint32_t T[4913], T1[4913];
        if (!fgets(t17, sizeof t17, l)) { printf("dat: list short\n"); return 1; }
        t17[strcspn(t17, "\r\n")] = 0;
        uint32_t *S = rd(t17, 4913 * 4);
        memcpy(&pr.fit, x + 12, 4); memcpy(&pr.bk, x + 16, 4); memcpy(&pr.inv, x + 20, 4);
        if (*(uint32_t *)(x + 24) != dat_fnv((const uint8_t *)S, 4913 * 4)) { printf("dat: %s is not the table entry %d names\n", t17, e); return 1; }
        const struct wf_sg_neg *N = (const void *)(b + dof + x[2] * u[0]);
        const struct wf_sg_prn *P = (const void *)(b + pof + x[3] * u[1]);
        wf_sg_compose(N, P, &pr, &k0, 0, mk_pd, T);
        wf_sg_compose(N, P, &pr, &k1, 0, mk_pd, T1);
        double s = 0;
        int mx = 0, up = 0, c, q;
        for (q = 0; q < 4913; q++) {
            int a[3] = { (T[q] >> 8) & 255, (int8_t)(T[q] & 255), (int8_t)((T[q] >> 16) & 255) };
            int d3[3] = { (S[q] >> 8) & 255, (int8_t)(S[q] & 255), (int8_t)((S[q] >> 16) & 255) };
            for (c = 0; c < 3; c++) { int d = abs(a[c] - d3[c]); s += d; if (d > mx) mx = d; }
        }
        for (q = 2; q < 15; q++) {                     /* neutral nodes Y 32..224, U = V = 128 */
            int nd = q * 289 + 8 * 17 + 8;
            up += ((T1[nd] >> 8) & 255) > ((T[nd] >> 8) & 255);
        }
        s /= 3 * 4913.0;
        smean += s;
        if (s > wmean) { wmean = s; snprintf(wname, sizeof wname, "%s", t17); }
        if (mx > wmax) wmax = mx;
        if (s > 1.0) nbig++;
        if (s > 1.0 || mx > 40 || up < 10) { printf("dat: %s mean %.3f max %d, Film exp +1 lifts %d of 13 grey nodes\n", t17, s, mx, up); fail = 1; }
        free(S);
    }
    printf("dat: %d entries (%d negatives, %d prints): default vs shipped mean of means %.3f, worst mean %.3f (%s), worst max %d, %d over 1.0\n",
           n, nn, np, smean / n, wmean, wname, wmax, nbig);
    free(b);
    fclose(l);
    return fail;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "use: mk_wfstage neg NEG OUT | print PRN OUT | pairs OUT | check NEG PRN NEGFILE PRNFILE [T17]"
                        " | shipped NEG PRN NEGFILE PRNFILE T17 | dat DAT LIST\n");
        return 2;
    }
    gl_init();
    if (!strcmp(argv[1], "neg") && argc == 4) {
        static struct wf_sg_neg N;
        return make_neg(atoi(argv[2]), &N) || wr(argv[3], &N, sizeof N);
    }
    if (!strcmp(argv[1], "print") && argc == 4) {
        static struct wf_sg_prn P;
        return make_prn(atoi(argv[2]), &P) || wr(argv[3], &P, sizeof P);
    }
    if (!strcmp(argv[1], "pairs") && argc == 3) {
        static int32_t rec[46 * 16 * 3];
        gl_film_params p0;
        int n, q;
        gl_film_defaults(&p0);
        for (n = 0; n < 46; n++)
            for (q = 0; q < 16; q++) {
                gl_film_stock(n);
                gl_print_stock(q);
                place(p0, &rec[(n * 16 + q) * 3], &rec[(n * 16 + q) * 3 + 1], &rec[(n * 16 + q) * 3 + 2]);
            }
        return wr(argv[2], rec, sizeof rec);
    }
    if (!strcmp(argv[1], "check") && argc >= 6)
        return check(atoi(argv[2]), atoi(argv[3]), argv[4], argv[5], argc > 6 ? argv[6] : 0);
    if (!strcmp(argv[1], "shipped") && argc == 7) {
        /* one line: the default composition against a shipped table (Y / U / V max and mean |difference|) */
        struct wf_sg_neg *N = rd(argv[4], sizeof *N);
        struct wf_sg_prn *P = rd(argv[5], sizeof *P);
        uint32_t *S = rd(argv[6], 4913 * 4), T[4913];
        struct wf_sg_knobs k = { 0, 0, 0, { 0, 0, 0 }, WF_SG_IIE_DEF };
        struct wf_sg_pair pr;
        gl_film_params p0;
        int32_t fit, bk, wt;
        double m[3] = { 0, 0, 0 }, s = 0;
        int n, c;
        gl_film_defaults(&p0);
        gl_film_stock(atoi(argv[2]));
        gl_print_stock(atoi(argv[3]));
        place(p0, &fit, &bk, &wt);
        pr.fit = fit; pr.bk = bk; pr.inv = (uint32_t)llround(4294967296.0 / (wt - bk));
        wf_sg_compose(N, P, &pr, &k, 0, mk_pd, T);
        for (n = 0; n < 4913; n++) {
            int a[3] = { (T[n] >> 8) & 255, (int8_t)(T[n] & 255), (int8_t)((T[n] >> 16) & 255) };
            int b[3] = { (S[n] >> 8) & 255, (int8_t)(S[n] & 255), (int8_t)((S[n] >> 16) & 255) };
            for (c = 0; c < 3; c++) { double d = abs(a[c] - b[c]); s += d; if (d > m[c]) m[c] = d; }
        }
        printf("%s: composed vs shipped Y max %.0f U max %.0f V max %.0f mean %.3f\n", argv[6], m[0], m[1], m[2], s / (3 * 4913.0));
        return 0;
    }
    if (!strcmp(argv[1], "dat") && argc == 4)
        return dat(argv[2], argv[3]);
    fprintf(stderr, "bad arguments\n");
    return 2;
}
