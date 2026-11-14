/* wf_dev_scene.h - synthetic shot for the RAW develop test and bench (WindyStrife): one linear Rec.709 scene rendered
 * twice, as the 14-bit packed raw a camera would hold (camera matrix, white balance, exposure, Bayer mosaic, black,
 * noise, sensor clip, hot pixels outside the active area) and as the camera's own UYVY frame of the same shot (scene
 * clipped at 1.0, sRGB OETF, BT.601 full range, 4:2:2). Host-side float code, never in the module. */
#ifndef WF_DEV_SCENE_H
#define WF_DEV_SCENE_H
#include <stdint.h>

enum { SC_NONE = -1 };
enum { SC_Z_PATCH = 1, SC_Z_LAMP = 2, SC_Z_RAMP = 3, SC_Z_DETAIL = 4 };

struct sc_shot {
    /* parameters (sc_default fills them) */
    int      w, h;                               /* output frame = active area */
    int      ax0, ay0;                           /* active corner in the raw (x multiple of 8, y even) */
    int      raw_w, raw_h, pitch;                /* raw buffer */
    int      black, white, cam, noise;           /* cam 0 EOS M, 1 5D2; noise 0 = off */
    double   m[3];                               /* true scales: linear camera (WB) per raw unit, R G B */
    uint32_t seed;
    /* results */
    uint8_t  *raw;                               /* raw_h x pitch (+ 16 bytes slack) */
    uint32_t *uyvy;                              /* w x h / 2 words */
};

void   sc_default(struct sc_shot *s, int w, int h, int cam);
int    sc_render(struct sc_shot *s);             /* allocates raw / uyvy (malloc); 0 = ok */
void   sc_free(struct sc_shot *s);
/* the scene at output pixel (x, y): linear Rec.709 (unclipped), zone (SC_Z_*), patch number or lamp ring */
void   sc_scene(const struct sc_shot *s, int x, int y, double lin[3], int *zone, int *id);
/* raw value (black subtracted, before noise and clip) of channel c at output pixel (x, y) */
double sc_rawval(const struct sc_shot *s, int x, int y, int c);
/* helpers */
double sc_srgb_oetf(double v);                   /* linear 0..1 -> 0..1 */
double sc_srgb_eotf(double v);
void   sc_ycc(const double rgb8[3], double ycc[3]);   /* R'G'B' codes -> Y, Cb, Cr codes (BT.601 full, chroma signed) */
void   sc_pack14(const uint16_t *v, uint8_t *dst, int npx);
int    sc_save(const struct sc_shot *s, const char *dir);   /* dir/scene.r14 + dir/scene.uyvy; 0 = ok */
int    sc_load(struct sc_shot *s, const char *dir);         /* the same files into a shot set up by sc_default */
/* synthetic "film" cube (S-curve per channel, crosstalk, a warm offset) or the identity, n points, as a .cube file */
void   sc_write_cube(const char *path, int n, int ident);
void   sc_film(const double in[3], double o[3]);
extern const double sc_camrgb[2][9];             /* linear Rec.709 -> camera (WB): the dcraw method (mk_devmat.py) */
#endif
