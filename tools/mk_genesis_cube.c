/* mk_genesis_cube.c - write a .cube whose values are the Genesis 1.6.6 film chain (declared exception, DESIGN 1a).
 *
 * Host tool only, never linked into the module.  It samples the chain in the Film Roll tree (FR_SRC) on an N^3 grid
 * of sRGB display codes, so WindyFilm's own converter (tools/mk_wtab.py cube) can turn the result into the 17^3
 * Y'CbCr table a WindyFilm film already is.  Nothing of the plugin is linked or called at run time.
 *
 * build: gcc -O2 -I$FR_SRC -o mk_genesis_cube mk_genesis_cube.c $FR_SRC/fr_glook.c $FR_SRC/fr_glook_film.c \
 *            $FR_SRC/fr_glook_print.S -lm
 * use:   mk_genesis_cube N NEG PRINT PUSHPULL BLEACH PBLEACH OUT.cube [NEUTRAL [GREY]]
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
 * comes out where it went in; an empty GREY argument keeps the default.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
        fprintf(stderr, "use: %s N NEG PRINT PUSHPULL BLEACH PBLEACH OUT.cube [NEUTRAL [GREY]]\n", argv[0]);
        return 2;
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
        const int32_t probe = disp_code_lin((int32_t)(GENESIS_GREY_CODE * GL_ONE));
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
            in[0] = in[1] = in[2] = disp_code_lin(GL_ONE);
            gl_film_lin(in, &p, out);
            wt = out[1] / (double)GL_ONE;
            pos = wt > bk ? (gv - bk) / (wt - bk) : 0.0;
            d = pos - grey;
            if (d < 0) d = -d;
            if (d < bestd) { bestd = d; best = p.exposure; }
        }
        p.exposure = best;

        in[0] = in[1] = in[2] = 0;
        gl_film_lin(in, &p, out);
        out_black = out[1] / (double)GL_ONE;
        in[0] = in[1] = in[2] = disp_code_lin(GL_ONE);
        gl_film_lin(in, &p, out);
        out_white = out[1] / (double)GL_ONE;

        in[0] = in[1] = in[2] = probe;
        gl_film_lin(in, &p, out);
        fprintf(stderr, "fit: exposure %+.2f stops, print density %.3f..%.3f, "
                        "18%% grey code %.3f -> %.3f (asked %.3f), white -> 1.000, black -> 0.000\n",
                best / (double)GL_ONE, out_black, out_white, GENESIS_GREY_CODE,
                level(out[1] / (double)GL_ONE), grey);
    }

    FILE *f = fopen(argv[7], "wb");
    if (!f) { perror(argv[7]); return 1; }
    fprintf(f, "TITLE \"Genesis neg %d print %d (declared exception)\"\n", neg, prn);
    fprintf(f, "LUT_3D_SIZE %d\n", n);
    fprintf(f, "# Domain: sRGB display code 0..1.  Values: the film chain's output, print ends pinned to 0..1.\n");

    for (b = 0; b < n; b++)
        for (g = 0; g < n; g++)
            for (r = 0; r < n; r++) {                  /* red fastest, the .cube convention */
                int code[3];
                code[0] = r; code[1] = g; code[2] = b;
                for (i = 0; i < 3; i++) {
                    /* the camera's words are display-referred: undo the display curve, then the chain */
                    int32_t q = (int32_t)((int64_t)code[i] * GL_ONE / (n - 1));
                    in[i] = disp_code_lin(q);
                }
                gl_film_lin(in, &p, out);
                fprintf(f, "%.6f %.6f %.6f\n",
                        level(out[0] / (double)GL_ONE), level(out[1] / (double)GL_ONE),
                        level(out[2] / (double)GL_ONE));
            }
    fclose(f);
    return 0;
}
