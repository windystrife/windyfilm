/* mk_genesis_cube.c - write a .cube whose values are the Genesis 1.6.6 film chain (declared exception, DESIGN 1a).
 *
 * Host tool only, never linked into the module.  It samples the chain in the Film Roll tree (FR_SRC) on an N^3 grid
 * of sRGB display codes, so WindyFilm's own converter (tools/mk_wtab.py cube) can turn the result into the 17^3
 * Y'CbCr table a WindyFilm film already is.  Nothing of the plugin is linked or called at run time.
 *
 * build: gcc -O2 -I$FR_SRC -o mk_genesis_cube mk_genesis_cube.c $FR_SRC/fr_glook.c $FR_SRC/fr_glook_film.c \
 *            $FR_SRC/fr_glook_print.S -lm
 * use:   mk_genesis_cube N NEG PRINT PUSHPULL BLEACH PBLEACH OUT.cube [NEUTRAL [GREY [WHITE [DOMAIN]]]]
 *
 * Placement.  The chain is calibrated for S-Log3 codes and it ends on the print's dye density, so its black is the
 * print's base fog (~0.03) and its white is whatever the print stock reaches (~0.65..0.71): the whole picture sits
 * inside 0.03..0.71 and no input exposure can lift the white without burying the mids, because the span between grey
 * and white is the print's own contrast.  Rather than stretch the input stops (which crushes everything below code
 * 0.35 into the toe - measured 14/11/2026), the two ends are pinned on the OUTPUT: base fog becomes display black
 * and print white becomes display white, i.e. printer points, exactly how a print is placed on a scanner.  What is
 * left between them is the film's own curve, untouched.  GREY is where 18 % grey then lands (the exposure offset is
 * fitted for it) and is reported along with black and white.  It is a display code: the probe is the JPEG's 18 %
 * grey code, GENESIS_GREY_CODE (tools/mk_genesis_place.h), and by default it lands on that same code, so a grey patch
 * comes out where it went in; an empty GREY argument keeps the default.  WHITE is not read (the tool pins white).
 *
 * DOMAIN (option (c), the RAW develop): "srgb" (default) is the grid above, the camera's 8-bit words.  "slog3",
 * "logc3" or "vlog" is the grid the Darkroom's RAW develop reads a Log + LUT can's cube in (src/wf_dev.h WF_DEV_CUBE,
 * curves 1 S-Log3 / S-Gamut3.Cine, 3 LogC3 EI800 / AWG3, 2 V-Log / V-Gamut): a node is a log code (0..1 on each axis)
 * of scene-linear light in that gamut, 1.0 = the JPEG path's linear white.  S-Log3 and LogC3 are decoded with the
 * chain's own curve (gl_log_to_lin, the decode gl_film() does on its S-Log3 input), V-Log with Panasonic's.  The bare
 * curve name feeds the linear values to the chain as they are, in the camera gamut.  "<curve>:M" (what
 * mk_genesis_films.py log passes by default) brings them back to linear Rec.709 first with the inverse of M, the
 * develop's Rec.709 -> gamut matrix (nine Q20 integers, comma separated, tools/mk_loglut_tab.py's = wf_loglut_tab.h
 * wf_ll_mat), the input the 8-bit tables see.  The port's chain has no input matrix (it drops the plugin's XYZ /
 * XYZ_to_Neg step, fr_glook_film.c), so it takes whatever primaries it gets: fed S-Gamut3.Cine as it is, colours read
 * paler and a blue sky under Eterna / 3523XD turns pink (owner's IMG_6849, 15/11/2026).  Placement then
 * probes scene-linear 0.18 (the develop's 18 % grey at LUT EI 0; a neutral is the same in every gamut) and puts it on
 * the same GREY code as the 8-bit table; black is the chain at linear 0 and white the brightest neutral output over the
 * whole log range, so above about +1.3 EV both families end on the print's white (the RAW develop's film shoulder,
 * job.roll, then folds the sensor's last stops into it).
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "fr_glook.h"
#include "mk_genesis_place.h"

/* the chain's output ends, filled by the placement pass */
static double out_black = 0.0, out_white = 1.0;

/* display code -> linear, the chain's input for a display-referred frame */
static int32_t disp_code_lin(int32_t q)
{
    int32_t l = gl_disp_to_lin(q);
    return l < 0 ? 0 : l;
}

/* DOMAIN: 0 srgb, else a log curve; to709 = the gamut is brought back to Rec.709 (else the camera gamut as it is) */
enum { DOM_SRGB = 0, DOM_SLOG3, DOM_LOGC3, DOM_VLOG };
static int vlog_dom = 0;                        /* any log domain */
static int dom = DOM_SRGB, to709 = 0;
static double vg_to_709[9];                     /* inverse of the develop's Rec.709 -> gamut */

/* log code -> scene-linear: S-Log3 / LogC3 the chain's own decode, V-Log Panasonic's (tools/mk_loglut_tab.py vlog) */
static double vlog_lin(double v)
{
    if (dom == DOM_SLOG3 || dom == DOM_LOGC3)
        return gl_log_to_lin(dom == DOM_SLOG3 ? GL_SLOG3 : GL_LOGC3, (int32_t)(v * GL_ONE + 0.5)) / (double)GL_ONE;
    return v < 0.181 ? (v - 0.125) / 5.6 : pow(10.0, (v - 0.598206) / 0.241514) - 0.00873;
}

static int inv3(const double m[9], double o[9])
{
    double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6])
               + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (fabs(det) < 1e-12)
        return -1;
    o[0] = (m[4] * m[8] - m[5] * m[7]) / det; o[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    o[2] = (m[1] * m[5] - m[2] * m[4]) / det; o[3] = (m[5] * m[6] - m[3] * m[8]) / det;
    o[4] = (m[0] * m[8] - m[2] * m[6]) / det; o[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    o[6] = (m[3] * m[7] - m[4] * m[6]) / det; o[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    o[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return 0;
}

/* grid node k (0..n-1 per axis) -> the chain's linear input, GL_ONE = 1.0 (srgb: the same integers as before the
   DOMAIN option, so the 8-bit tables come out byte for byte as they did) */
static void node_lin(const int k[3], int n, int32_t in[3])
{
    int i;
    if (!vlog_dom) {
        for (i = 0; i < 3; i++)
            in[i] = disp_code_lin((int32_t)((int64_t)k[i] * GL_ONE / (n - 1)));
        return;
    }
    double vg[3], l;
    for (i = 0; i < 3; i++) {
        vg[i] = vlog_lin(k[i] / (double)(n - 1));
        if (vg[i] < 0.0) vg[i] = 0.0;            /* the develop clamps x at 0 before the curve */
    }
    for (i = 0; i < 3; i++) {
        l = to709 ? vg_to_709[3 * i] * vg[0] + vg_to_709[3 * i + 1] * vg[1] + vg_to_709[3 * i + 2] * vg[2] : vg[i];
        in[i] = l <= 0.0 ? 0 : (int32_t)(l * GL_ONE + 0.5);
    }
}

/* the neutral white the placement pins: srgb the chain at display white, vlog the brightest neutral of the range */
static double neutral_white(const gl_film_params *p)
{
    int32_t in[3], out[3];
    double w = 0.0;
    int k, c[3];
    if (!vlog_dom) {
        in[0] = in[1] = in[2] = disp_code_lin(GL_ONE);
        gl_film_lin(in, p, out);
        return out[1] / (double)GL_ONE;
    }
    for (k = 0; k <= 64; k++) {
        c[0] = c[1] = c[2] = k;
        node_lin(c, 65, in);
        gl_film_lin(in, p, out);
        if (out[1] / (double)GL_ONE > w) w = out[1] / (double)GL_ONE;
    }
    return w;
}

/* pin base fog to 0 and print white to 1: out = (raw - black) / (white - black) */
static double level(double raw)
{
    double d = out_white - out_black;
    if (d < 1e-6) d = 1e-6;
    raw = (raw - out_black) / d;
    if (raw < 0.0) raw = 0.0;
    if (raw > 1.0) raw = 1.0;
    return raw;
}

int main(int argc, char **argv)
{
    int n, neg, prn, i, b, g, r;
    gl_film_params p;
    int32_t in[3], out[3];

    if (argc < 8) {
        fprintf(stderr, "use: %s N NEG PRINT PUSHPULL BLEACH PBLEACH OUT.cube [NEUTRAL [GREY [WHITE [DOMAIN]]]]\n",
                argv[0]);
        return 2;
    }
    if (argc > 11 && argv[11][0] && strcmp(argv[11], "srgb")) {
        const char *a = argv[11], *c = strchr(a, ':');
        size_t l = c ? (size_t)(c - a) : strlen(a);
        dom = l == 5 && !strncmp(a, "slog3", 5) ? DOM_SLOG3 : l == 5 && !strncmp(a, "logc3", 5) ? DOM_LOGC3
            : l == 4 && !strncmp(a, "vlog", 4) ? DOM_VLOG : -1;
        if (dom < 0) {
            fprintf(stderr, "DOMAIN must be srgb, or slog3 / logc3 / vlog [:<9 Q20 integers, Rec.709 -> gamut>]\n");
            return 2;
        }
        if (c) {
            double m[9];
            if (sscanf(c + 1, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6],
                       &m[7], &m[8]) != 9) {
                fprintf(stderr, "DOMAIN %s: the matrix is nine comma-separated Q20 integers\n", a);
                return 2;
            }
            for (i = 0; i < 9; i++) m[i] /= 1048576.0;
            if (inv3(m, vg_to_709)) { fprintf(stderr, "DOMAIN %s: singular matrix\n", a); return 2; }
            to709 = 1;
        }
        vlog_dom = 1;
        gl_init();                               /* gl_log_to_lin's tables */
    }
    n = atoi(argv[1]);
    neg = atoi(argv[2]);
    prn = atoi(argv[3]);
    if (n < 2 || n > 65) { fprintf(stderr, "N must be 2..65\n"); return 2; }

    gl_init();
    gl_film_defaults(&p);
    gl_film_stock(neg);
    gl_print_stock(prn);
    p.push_pull = (int)(atof(argv[4]) * GL_ONE);       /* logE */
    p.bleach = (int)(atof(argv[5]) * GL_ONE);
    p.print_bleach = (int)(atof(argv[6]) * GL_ONE);
    if (argc > 8)
        p.neutral_tonescale = p.neutral_density = (int)(atof(argv[8]) * GL_ONE);

    /* Placement: exposure first so that the 18 % grey code, once the ends are pinned, lands on GREY.  Raising exposure
     * lifts grey faster than white (white saturates against the print's shoulder), so the position after levels is
     * monotonic in exposure and a plain scan finds it. */
    {
        double grey = GENESIS_GREY_CODE, bestd = 1e9;
        int e, best = 0;
        /* 18 % grey: the JPEG's code decoded as the 8-bit grid does, or scene-linear 0.18 on the RAW develop's grid */
        const int32_t probe = vlog_dom ? (int32_t)(0.18 * GL_ONE + 0.5)
                                       : disp_code_lin((int32_t)(GENESIS_GREY_CODE * GL_ONE));
        double bk, wt;
        if (argc > 9 && argv[9][0]) grey = atof(argv[9]);
        if (grey < 0.03) grey = 0.03;
        if (grey > 0.70) grey = 0.70;

        in[0] = in[1] = in[2] = 0;
        gl_film_lin(in, &p, out);
        bk = out[1] / (double)GL_ONE;

        for (e = -600; e <= 600; e += 5) {              /* -6.0 .. +6.0 stops in 0.05 */
            double d, gv, pos;
            p.exposure = (int32_t)((int64_t)e * GL_ONE / 100);
            in[0] = in[1] = in[2] = probe;
            gl_film_lin(in, &p, out);
            gv = out[1] / (double)GL_ONE;
            wt = neutral_white(&p);
            pos = wt > bk ? (gv - bk) / (wt - bk) : 0.0;
            d = pos - grey;
            if (d < 0) d = -d;
            if (d < bestd) { bestd = d; best = p.exposure; }
        }
        /* vlog: then 1/400 stop around it. A steep pair (EXR 5287) moves grey about 4 codes per 0.05 stop, so the
           coarse step alone leaves two log and 8-bit twins up to 5 codes apart. The 8-bit tables keep the coarse scan
           they were made with: srgb output stays byte for byte as before. */
        if (vlog_dom) {
            int32_t c0 = best;
            for (e = -20; e <= 20; e++) {
                double d, gv, pos;
                p.exposure = c0 + (int32_t)((int64_t)e * GL_ONE / 400);
                in[0] = in[1] = in[2] = probe;
                gl_film_lin(in, &p, out);
                gv = out[1] / (double)GL_ONE;
                wt = neutral_white(&p);
                pos = wt > bk ? (gv - bk) / (wt - bk) : 0.0;
                d = pos - grey;
                if (d < 0) d = -d;
                if (d < bestd) { bestd = d; best = p.exposure; }
            }
        }
        p.exposure = best;

        in[0] = in[1] = in[2] = 0;
        gl_film_lin(in, &p, out);
        out_black = out[1] / (double)GL_ONE;
        out_white = neutral_white(&p);

        in[0] = in[1] = in[2] = probe;
        gl_film_lin(in, &p, out);
        fprintf(stderr, "fit: exposure %+.2f stops, print density %.3f..%.3f, "
                        "18%% grey %s %.3f -> %.3f (asked %.3f), white -> 1.000, black -> 0.000\n",
                best / (double)GL_ONE, out_black, out_white, vlog_dom ? "lin" : "code",
                vlog_dom ? 0.18 : GENESIS_GREY_CODE, level(out[1] / (double)GL_ONE), grey);
    }

    FILE *f = fopen(argv[7], "wb");
    if (!f) { perror(argv[7]); return 1; }
    fprintf(f, "TITLE \"Genesis neg %d print %d (declared exception)\"\n", neg, prn);
    fprintf(f, "LUT_3D_SIZE %d\n", n);
    if (vlog_dom)
        fprintf(f, "# Domain: %s code 0..1 (the RAW develop's grid), chain fed %s.  Values: the film chain's output, "
                   "print ends pinned to 0..1.\n",
                dom == DOM_SLOG3 ? "S-Log3 / S-Gamut3.Cine" : dom == DOM_LOGC3 ? "LogC3 EI800 / AWG3" : "V-Log / V-Gamut",
                to709 ? "linear Rec.709" : "the camera gamut as it is");
    else
        fprintf(f, "# Domain: sRGB display code 0..1.  Values: the film chain's output, print ends pinned to 0..1.\n");

    for (b = 0; b < n; b++)
        for (g = 0; g < n; g++)
            for (r = 0; r < n; r++) {                  /* red fastest, the .cube convention */
                int code[3];
                code[0] = r; code[1] = g; code[2] = b;
                /* srgb: the camera's words are display-referred, undo the display curve; vlog: undo the log curve and
                   the gamut; then the chain */
                node_lin(code, n, in);
                gl_film_lin(in, &p, out);
                fprintf(f, "%.6f %.6f %.6f\n",
                        level(out[0] / (double)GL_ONE), level(out[1] / (double)GL_ONE),
                        level(out[2] / (double)GL_ONE));
            }
    fclose(f);
    return 0;
}
