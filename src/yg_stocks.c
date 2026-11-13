/* yg_stocks.c - per-stock grain parameters ("analyse once, ship the results").
 * Module copy of proto/grain_yedlin/yg_stocks.c plus stocks 4 (Neg 160) and 5 (Neg 250) at the end.
 *
 * Colour negative curves: Kodak VISION3 500T 5219, H-1-5219t p.4 "rms granularity", 48 um
 * aperture, read by eye from the chart (+-15%), x axis converted to net density with the
 * density curves on the same chart (phase 1 research notes):
 *   R: Dnet .02 .07 .20 .42 .70 1.00 1.28 1.55 1.72 1.80
 *      sig  .0057 .0070 .0100 .0072 .0062 .0062 .0059 .0054 .0052 .0056
 *   G: Dnet 0 .08 .22 .52 .85 1.15 1.45 1.78 1.98 2.12
 *      sig  .0075 .0110 .0135 .0087 .0072 .0074 .0081 .0074 .0065 .0056
 *   B: Dnet .05 .12 .25 .58 .92 1.18 1.42 1.72 1.95 2.07
 *      sig  .028 .030 .0303 .0236 .0188 .0190 .0193 .0163 .0128 .0100
 * Mid grey (camera stop 0) from the H-1-5219t p.3 sensitometry: net R .70 G .92 B .86.
 * ISO 400 "Portra-400-like" = the 5219 shape scaled x0.8 (guess: Portra 400 is finer than 500T;
 * Kodak publishes only PGI for Portra, E-4050).
 * Slide: Velvia 50 RMS 9 (Fuji, via cacreeks.com table: likely); per-layer ratios and the
 * sqrt(D) shape are guesses. B&W: Tri-X 400 RMS 17 at net D 1.0 (Kodak F-4017: proven),
 * sigma ~ sqrt(D) (Siedentopf/Selwyn: likely for silver).
 * rho_max = display density of the black (neg v=8, slide v=3, B&W v=5), so the print/paper/slide black meets
 * the look's black: values as shipped, not yet from a public source; PLAN 3.2.4 b re-derives them per film from
 * our own T17 and the published Dmax (films/catalog.py rho_for, off until then).
 * chroma_shift 1 halves the chroma part of colour grain (guess: perceptual + JPEG chroma).
 * Dye-cloud diameters 6.5 / 15 um (Stephenson & Saunders 2007, Graves-Saunders spectrum),
 * scaled by (ISO/200)^0.25 (inference from two Kodak datasheets), blue layer x1.73 linear
 * (spektrafilm grain.py blue area x3: guess), green x0.9.
 */
#include "yg_grain.h"

#define S5219_R { .02f, .07f, .20f, .42f, .70f, 1.00f, 1.28f, 1.55f, 1.72f, 1.80f }
#define S5219_G { 0.0f, .08f, .22f, .52f, .85f, 1.15f, 1.45f, 1.78f, 1.98f, 2.12f }
#define S5219_B { .05f, .12f, .25f, .58f, .92f, 1.18f, 1.42f, 1.72f, 1.95f, 2.07f }
#define G5219_R(k) { k*.0057f, k*.0070f, k*.0100f, k*.0072f, k*.0062f, k*.0062f, k*.0059f, k*.0054f, k*.0052f, k*.0056f }
#define G5219_G(k) { k*.0075f, k*.0110f, k*.0135f, k*.0087f, k*.0072f, k*.0074f, k*.0081f, k*.0074f, k*.0065f, k*.0056f }
#define G5219_B(k) { k*.0280f, k*.0300f, k*.0303f, k*.0236f, k*.0188f, k*.0190f, k*.0193f, k*.0163f, k*.0128f, k*.0100f }

const yg_stock yg_stocks[] = {
    {   /* 0: ISO 400 colour negative, Portra-400-like */
        "Neg 400", YG_NEG, 400, 0.0f, 1.0f, { 1.0f, 0.9f, 1.73f }, 6.5f, 15.0f, 0.25f,
        { 10, 10, 10 },
        { S5219_R, S5219_G, S5219_B },
        { G5219_R(0.8f), G5219_G(0.8f), G5219_B(0.8f) },
        { 0.70f, 0.92f, 0.86f }, 0.60f, 2.6f, 3.31f, 3, 1
    },
    {   /* 1: ISO 800 tungsten = VISION3 500T 5219 rated EI 800 (CineStill 800T is 5219 without remjet) */
        "Neg 800T", YG_NEG, 500, 0.68f, 1.0f, { 1.0f, 0.9f, 1.73f }, 6.5f, 15.0f, 0.25f,
        { 10, 10, 10 },
        { S5219_R, S5219_G, S5219_B },
        { G5219_R(1.0f), G5219_G(1.0f), G5219_B(1.0f) },
        { 0.70f, 0.92f, 0.86f }, 0.60f, 2.6f, 3.31f, 3, 1
    },
    {   /* 2: slide ISO 50, Velvia-50-like: dn = total slide density, Dmin first */
        "Slide 50", YG_SLIDE, 50, 0.0f, 1.0f, { 1.0f, 0.9f, 1.73f }, 6.5f, 15.0f, 0.20f,
        { 8, 8, 8 },
        { { .15f, .3f, .6f, 1.0f, 1.5f, 2.0f, 2.8f, 3.6f },
          { .15f, .3f, .6f, 1.0f, 1.5f, 2.0f, 2.8f, 3.6f },
          { .15f, .3f, .6f, 1.0f, 1.5f, 2.0f, 2.8f, 3.6f } },
        { { .0029f, .0041f, .0058f, .0075f, .0092f, .0106f, .0125f, .0142f },
          { .0035f, .0049f, .0070f, .0090f, .0110f, .0127f, .0150f, .0171f },
          { .0046f, .0066f, .0093f, .0120f, .0147f, .0170f, .0201f, .0228f } },
        { 0.80f, 0.80f, 0.80f }, 1.0f, 1.0f, 4.24f, 3, 1
    },
    {   /* 3: B&W ISO 400, Tri-X-like: sigma = .017 sqrt(Dnet), fog floor; grade-2 paper */
        "B&W 400", YG_BW, 400, 0.0f, 1.0f, { 1.1f, 1.1f, 1.1f }, 6.5f, 15.0f, 0.0f,
        { 9, 9, 9 },
        { { 0.0f, .05f, .1f, .2f, .4f, .7f, 1.0f, 1.4f, 2.0f } },
        { { .0035f, .0038f, .0054f, .0076f, .0108f, .0142f, .0170f, .0201f, .0240f } },
        { 0.70f, 0.70f, 0.70f }, 0.62f, 1.9f, 3.76f, 1, 0
    },
    /* module additions (wf_fx.c, WindyStrife films). Both are the 5219 shape with a smaller box speed
     * (dye clouds (ISO/200)^0.25) and a smaller RMS scale. GUESS: sigma_D ~ sqrt(ISO) from the Neg 400 row
     * (0.8 at 400): 0.8 * sqrt(250/400) = 0.63, 0.8 * sqrt(160/400) = 0.51. */
    {   /* 4: ISO 160 colour negative, finer (Pro Neg Std) */
        "Neg 160", YG_NEG, 160, 0.0f, 1.0f, { 1.0f, 0.9f, 1.73f }, 6.5f, 15.0f, 0.25f,
        { 10, 10, 10 },
        { S5219_R, S5219_G, S5219_B },
        { G5219_R(0.51f), G5219_G(0.51f), G5219_B(0.51f) },
        { 0.70f, 0.92f, 0.86f }, 0.60f, 2.6f, 3.31f, 3, 1
    },
    {   /* 5: ISO 250 colour negative, 5219 shape (Eterna 250) */
        "Neg 250", YG_NEG, 250, 0.0f, 1.0f, { 1.0f, 0.9f, 1.73f }, 6.5f, 15.0f, 0.25f,
        { 10, 10, 10 },
        { S5219_R, S5219_G, S5219_B },
        { G5219_R(0.63f), G5219_G(0.63f), G5219_B(0.63f) },
        { 0.70f, 0.92f, 0.86f }, 0.60f, 2.6f, 3.31f, 3, 1
    },
};
const int yg_nstocks = (int)(sizeof yg_stocks / sizeof yg_stocks[0]);
