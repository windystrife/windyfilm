/* wf_dev_scene.c - synthetic shot for the RAW develop test and bench (WindyStrife). See wf_dev_scene.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wf_dev_scene.h"

/* tools/mk_devmat.py wf_dev_camrgb / 2^20 (the module's own numbers, so a perfect fit is exact) */
const double sc_camrgb[2][9] = {
    { 600400 / 1048576.0, 391560 / 1048576.0, 56616 / 1048576.0, 83501 / 1048576.0, 745853 / 1048576.0,
      219222 / 1048576.0, 20198 / 1048576.0, 247486 / 1048576.0, 780892 / 1048576.0 },
    { 515882 / 1048576.0, 506188 / 1048576.0, 26506 / 1048576.0, 11936 / 1048576.0, 835316 / 1048576.0,
      201324 / 1048576.0, -10341 / 1048576.0, 222125 / 1048576.0, 836792 / 1048576.0 },
};

/* 40 patches, sRGB codes: a colour-checker-like set, saturated colours, deep shadows, near white */
static const uint8_t sc_pat[40][3] = {
    { 115, 82, 68 }, { 194, 150, 130 }, { 98, 122, 157 }, { 87, 108, 67 }, { 133, 128, 177 }, { 103, 189, 170 },
    { 214, 126, 44 }, { 80, 91, 166 }, { 193, 90, 99 }, { 94, 60, 108 }, { 157, 188, 64 }, { 224, 163, 46 },
    { 56, 61, 150 }, { 70, 148, 73 }, { 175, 54, 60 }, { 231, 199, 31 }, { 187, 86, 149 }, { 8, 133, 161 },
    { 243, 243, 242 }, { 200, 200, 200 }, { 160, 160, 160 }, { 122, 122, 121 }, { 85, 85, 85 }, { 52, 52, 52 },
    { 230, 30, 30 }, { 30, 200, 40 }, { 40, 60, 230 }, { 250, 240, 20 }, { 20, 230, 230 }, { 230, 30, 230 },
    { 255, 128, 0 }, { 128, 0, 255 }, { 20, 20, 20 }, { 10, 10, 30 }, { 35, 25, 15 }, { 240, 220, 200 },
    { 180, 200, 240 }, { 250, 250, 250 }, { 128, 128, 128 }, { 64, 64, 64 },
};

double sc_srgb_eotf(double v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
double sc_srgb_oetf(double v)
{
    if (v <= 0) return 0;
    if (v >= 1) return 1;
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055;
}

void sc_ycc(const double c[3], double o[3])
{
    o[0] = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
    o[1] = -0.168735891647856 * c[0] - 0.331264108352144 * c[1] + 0.5 * c[2];
    o[2] = 0.5 * c[0] - 0.418687589158345 * c[1] - 0.081312410841655 * c[2];
}

void sc_default(struct sc_shot *s, int w, int h, int cam)
{
    memset(s, 0, sizeof *s);
    s->w = w; s->h = h;
    s->ax0 = 72; s->ay0 = 52;
    s->raw_w = ((s->ax0 + w + 24) + 7) & ~7;
    s->raw_h = s->ay0 + h + 20;
    s->pitch = s->raw_w / 8 * 14;
    s->black = 2048; s->white = 15200; s->cam = cam; s->noise = 1;
    double cl = s->white - s->black;
    s->m[1] = 2.0 / cl;                          /* camera-space linear 2.0 (1 EV over JPEG white) = clip */
    s->m[0] = s->m[1] * 1.62;                    /* a warm (tungsten-ish) white balance: not daylight */
    s->m[2] = s->m[1] * 2.35;
    s->seed = 12345;
}

void sc_scene(const struct sc_shot *s, int x, int y, double lin[3], int *zone, int *id)
{
    double u = (x + 0.5) / s->w, v = (y + 0.5) / s->h;
    *id = SC_NONE;
    if (u < 0.6) {                               /* 8 x 5 patches on 18 % grey */
        double cu = u / 0.6 * 8, cv = v * 5;
        int ci = (int)cu, cj = (int)cv;
        double fu = cu - ci, fv = cv - cj;
        *zone = SC_Z_PATCH;
        if (fu > 0.12 && fu < 0.88 && fv > 0.12 && fv < 0.88 && ci < 8 && cj < 5) {
            const uint8_t *p = sc_pat[cj * 8 + ci];
            for (int c = 0; c < 3; c++) lin[c] = sc_srgb_eotf(p[c] / 255.0);
            *id = cj * 8 + ci;
            return;
        }
        lin[0] = lin[1] = lin[2] = 0.18;
        return;
    }
    if (v < 0.5) {                               /* red-orange lamp: the raw clips R first, then G, then B (centre) */
        double dx = (u - 0.8) * s->w, dy = (v - 0.25) * s->h, sg = 0.09 * (s->w < s->h ? s->w : s->h);
        double r = sqrt(dx * dx + dy * dy) / sg, I = 0.12 + 60.0 * exp(-r);
        lin[0] = I * 1.0; lin[1] = I * 0.25; lin[2] = I * 0.08;
        *zone = SC_Z_LAMP;
        return;
    }
    if (v < 0.75) {                              /* neutral ramp 0 .. 1.6 (JPEG clips at 1, the raw at 2) */
        double t = (u - 0.6) / 0.4;
        lin[0] = lin[1] = lin[2] = 1.6 * pow(t, 2.2);
        *zone = SC_Z_RAMP;
        return;
    }
    {                                            /* detail: rings (rising frequency) and a 3 px checker */
        double t = (u - 0.6) / 0.4, q = (v - 0.75) / 0.25;
        *zone = SC_Z_DETAIL;
        if (t < 0.5) {
            double r2 = (t * t + q * q) * s->w * 0.4 * 0.5;
            double a = 0.18 * (1 + 0.8 * sin(r2 * 0.05));
            lin[0] = a * 1.1; lin[1] = a; lin[2] = a * 0.8;
        } else {
            int k = ((x / 3) + (y / 3)) & 1;
            lin[0] = lin[1] = lin[2] = k ? 0.6 : 0.04;
        }
    }
}

double sc_rawval(const struct sc_shot *s, int x, int y, int c)
{
    double lin[3];
    int z, id;
    sc_scene(s, x, y, lin, &z, &id);
    const double *A = sc_camrgb[s->cam];
    double cam = A[3 * c] * lin[0] + A[3 * c + 1] * lin[1] + A[3 * c + 2] * lin[2];
    return cam / s->m[c];
}

static double sc_rand(uint32_t *st)              /* uniform (0, 1) */
{
    *st = *st * 1664525u + 1013904223u;
    return ((*st >> 8) + 0.5) / 16777216.0;
}

static double sc_gauss(uint32_t *st)
{
    double a = sc_rand(st), b = sc_rand(st);
    return sqrt(-2 * log(a)) * cos(6.283185307179586 * b);
}

void sc_pack14(const uint16_t *v, uint8_t *dst, int npx)
{
    uint16_t *d = (uint16_t *)dst;
    for (int i = 0; i < npx; i += 8, v += 8, d += 7) {
        uint32_t a = v[0], b = v[1], c = v[2], e3 = v[3], e = v[4], f = v[5], g = v[6], h = v[7];
        d[0] = (uint16_t)(a << 2 | b >> 12);
        d[1] = (uint16_t)((b & 4095) << 4 | c >> 10);
        d[2] = (uint16_t)((c & 1023) << 6 | e3 >> 8);
        d[3] = (uint16_t)((e3 & 255) << 8 | e >> 6);
        d[4] = (uint16_t)((e & 63) << 10 | f >> 4);
        d[5] = (uint16_t)((f & 15) << 12 | g >> 2);
        d[6] = (uint16_t)((g & 3) << 14 | h);
    }
}

int sc_render(struct sc_shot *s)
{
    s->raw = malloc((size_t)s->raw_h * s->pitch + 16);
    s->uyvy = malloc((size_t)s->w * s->h * 2);
    uint16_t *row = malloc((size_t)s->raw_w * 2);
    if (!s->raw || !s->uyvy || !row)
        return -1;
    uint32_t st = s->seed;
    const int cl = s->white - s->black;
    for (int sy = 0; sy < s->raw_h; sy++) {
        for (int sx = 0; sx < s->raw_w; sx++) {
            int x = sx - s->ax0, y = sy - s->ay0;
            if (x < 0 || y < 0 || x >= s->w || y >= s->h) {
                row[sx] = 16383;                 /* outside the active area: hot, must never reach the output */
                continue;
            }
            int c = ((y & 1) << 1 | (x & 1));   /* RGGB at the active corner */
            c = c == 0 ? 0 : c == 3 ? 2 : 1;
            double v = sc_rawval(s, x, y, c);
            if (s->noise)
                v += sc_gauss(&st) * sqrt(9.0 + (v > 0 ? v : 0) * 0.5);
            double q = floor(v + 0.5);
            if (q > cl) q = cl;
            q += s->black;
            if (q < 0) q = 0;
            row[sx] = (uint16_t)q;
        }
        sc_pack14(row, s->raw + (size_t)sy * s->pitch, s->raw_w);
    }
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x += 2) {
            double ycc[2][3];
            for (int e = 0; e < 2; e++) {
                double lin[3], c8[3];
                int z, id;
                sc_scene(s, x + e, y, lin, &z, &id);
                for (int c = 0; c < 3; c++) c8[c] = 255 * sc_srgb_oetf(lin[c]);
                sc_ycc(c8, ycc[e]);
            }
            int Y0 = (int)floor(ycc[0][0] + 0.5), Y1 = (int)floor(ycc[1][0] + 0.5);
            int U = (int)floor((ycc[0][1] + ycc[1][1]) / 2 + 0.5), V = (int)floor((ycc[0][2] + ycc[1][2]) / 2 + 0.5);
            Y0 = Y0 < 0 ? 0 : Y0 > 255 ? 255 : Y0;
            Y1 = Y1 < 0 ? 0 : Y1 > 255 ? 255 : Y1;
            U = U < -128 ? -128 : U > 127 ? 127 : U;
            V = V < -128 ? -128 : V > 127 ? 127 : V;
            s->uyvy[(size_t)y * (s->w / 2) + x / 2] = (uint32_t)(U & 255) | (uint32_t)Y0 << 8 | (uint32_t)(V & 255) << 16
                                                      | (uint32_t)Y1 << 24;
        }
    free(row);
    return 0;
}

void sc_free(struct sc_shot *s)
{
    free(s->raw);
    free(s->uyvy);
    s->raw = 0;
    s->uyvy = 0;
}

static double sc_c01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void sc_film(const double in[3], double o[3])
{
    double t[3];
    for (int c = 0; c < 3; c++) t[c] = 0.5 + 0.48 * tanh(2.6 * (in[c] - 0.43));
    o[0] = sc_c01(1.08 * t[0] - 0.05 * t[1] - 0.03 * t[2] + 0.02 * t[1] * t[1]);
    o[1] = sc_c01(-0.04 * t[0] + 1.07 * t[1] - 0.03 * t[2]);
    o[2] = sc_c01(-0.02 * t[0] - 0.09 * t[1] + 1.11 * t[2] - 0.03);
}

void sc_write_cube(const char *path, int n, int ident)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "TITLE \"wf_dev synthetic %s %d\"\nLUT_3D_SIZE %d\n", ident ? "identity" : "film", n, n);
    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++) {
                double in[3] = { r / (n - 1.0), g / (n - 1.0), b / (n - 1.0) }, o[3];
                if (ident) memcpy(o, in, sizeof o);
                else sc_film(in, o);
                fprintf(f, "%.6f %.6f %.6f\n", o[0], o[1], o[2]);
            }
    fclose(f);
}

static int sc_rw(const char *dir, const char *name, void *p, size_t n, int wr)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, wr ? "wb" : "rb");
    if (!f)
        return -1;
    size_t k = wr ? fwrite(p, 1, n, f) : fread(p, 1, n, f);
    fclose(f);
    return k == n ? 0 : -1;
}

int sc_save(const struct sc_shot *s, const char *dir)
{
    if (sc_rw(dir, "scene.r14", s->raw, (size_t)s->raw_h * s->pitch, 1))
        return -1;
    return sc_rw(dir, "scene.uyvy", s->uyvy, (size_t)s->w * s->h * 2, 1);
}

int sc_load(struct sc_shot *s, const char *dir)
{
    s->raw = malloc((size_t)s->raw_h * s->pitch + 16);
    s->uyvy = malloc((size_t)s->w * s->h * 2);
    if (!s->raw || !s->uyvy || sc_rw(dir, "scene.r14", s->raw, (size_t)s->raw_h * s->pitch, 0))
        return -1;
    return sc_rw(dir, "scene.uyvy", s->uyvy, (size_t)s->w * s->h * 2, 0);
}

#ifdef WF_DEV_SCENE_MAIN
/* native generator (the float scene is slow under qemu): wf_dev_scene DIR W H CAM -> DIR/scene.r14, scene.uyvy,
   FILM33.CUB */
int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s DIR W H CAM\n", argv[0]);
        return 2;
    }
    struct sc_shot s;
    sc_default(&s, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    if (sc_render(&s) || sc_save(&s, argv[1])) {
        fprintf(stderr, "scene: render / write failed\n");
        return 1;
    }
    char p[512];
    snprintf(p, sizeof p, "%s/FILM33.CUB", argv[1]);
    sc_write_cube(p, 33, 0);
    printf("scene %dx%d (raw %dx%d, pitch %d), cam %d -> %s\n", s.w, s.h, s.raw_w, s.raw_h, s.pitch, s.cam, argv[1]);
    sc_free(&s);
    return 0;
}
#endif
