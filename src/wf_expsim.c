/* wf_expsim.c - "Preview exposure" (wf_expsim.h, docs/EXPSIM.md): the dEV between the shot settings and the
 * LiveView frame, its smoothing, the gain tables, and the control of Canon's ExpSim. Compiled with the ML
 * headers (struct lens_info, bmp_printf), like wf_menu.c. The per-pixel kernel is wf_expk.c. */
#include <dryos.h>
#include <lens.h>
#include <bmp.h>
#include <property.h>
#include "wf_expsim.h"
#include "wf_exptab.h"                   /* wf_exp_dec[256], wf_exp_g8[8] (mk_exptab.py) */

extern int shooting_mode;                /* Canon mode dial: 0 P, 1 Tv, 2 Av, 3 M, 0x14 movie, ... */
extern int lv;                           /* ML: LiveView is running */
extern int get_expsim(void);
extern int wf_yuv_active(void);          /* wf_shot.c: our hooks and worker are up */
extern int get_global_draw_setting(void);   /* ML: Overlay > Global Draw (bit 0 = in LiveView) */             /* ML: PROP_LIVE_VIEW_VIEWTYPE, 3 (off) reads as 0; 1 = on */
extern void wf_log(const char *fmt, ...);

#ifdef WF_BUILD_5D2
/* 5D2 build: Preview exposure does not exist (wf_cam.h WF_EXP_CAN 0). It is made of the EOS M's property 0x80000034 and the
 * frame parameters pointer at 0x404C8, neither of which is known on the 5D2 2.1.2, so none of it is compiled: this file
 * is only the state wf_shot.c / wf_menu.c read (always Canon, never active) and no-op entry points. build.sh checks that
 * this object imports nothing. */
volatile int wf_exp_mode = WF_EXP_CANON;
volatile int wf_exp_trim;
volatile uintptr_t wf_exp_vpp;
volatile int wf_exp_ctl;
volatile int wf_exp_dev_last = WF_EXP_BAD;
volatile int wf_exp_q_last;
volatile int wf_exp_flash_last;
int wf_exp_active(void) { return 0; }
const struct wf_exp_tab *wf_exp_frame(void) { return 0; }
void wf_exp_poll(void) { }
void wf_exp_deinit(void) { }
void wf_exp_words(uint32_t *p, int n, const struct wf_exp_tab *t) { (void)p; (void)n; (void)t; }
#else
#define WF_PROP_EXPSIM  0x80000034       /* PROP_LIVE_VIEW_VIEWTYPE; EOS M: GUI_SetLvScreenSet (ROM 0xff1b5294) writes it
                                          * through _prop_request_change (0xff11fb04) like ML does; the Canon menu maps
                                          * Enable / During DOF / Disable to 1 / 0 / 3 (ROM 0xff6719ac) */
#define WF_EXPSIM_ON    1
#define WF_EXPSIM_OFF   3
#define SHOOTMODE_M_    3
#define SHOOTMODE_MOVIE_ 0x14

volatile int wf_exp_mode = WF_EXP_CANON;
volatile int wf_exp_trim;
volatile uintptr_t wf_exp_vpp = 0x404C8; /* EOS M 2.0.2: VIDEO_PARAMETERS_SRC_3 (ML consts.h): *this = a pointer to the
                                          * frame parameters: +0 ISO, +1 aperture, +2 shutter (raw, 1/8 EV) */
volatile int wf_exp_ctl;
volatile int wf_exp_dev_last = WF_EXP_BAD;
volatile int wf_exp_q_last;
volatile int wf_exp_flash_last;

/* ------------------------------------------------------------------------------------------ dEV */
static int wf_rng(int v, int lo, int hi) { return v >= lo && v <= hi; }

int wf_exp_dev8(const struct wf_exp_in *in)
{
    if (in->metered) {
        /* the camera meters the shot to its reference level and adds the compensation; the auto-exposed LiveView
         * frame sits at the same level, so the difference is the compensation */
        if (!wf_rng(in->ec, -64, 64))
            return WF_EXP_BAD;
        return in->ec + in->trim;
    }
    /* manual exposure: EV of a frame = Sv - Tv - Av = -(Tv + Av - Sv) = -(shutter + aperture - ISO) raw, so
     * dEV = EV(shot) - EV(frame) = (frame shutter + aperture - ISO) - (shot shutter + aperture - ISO) */
    if (!wf_rng(in->stv, 8, 200) || !wf_rng(in->sav, 8, 100) || !wf_rng(in->siso, 16, 160)
        || !wf_rng(in->ftv, 8, 200) || !wf_rng(in->fav, 8, 100) || !wf_rng(in->fiso, 16, 160))
        return WF_EXP_BAD;
    return (in->ftv + in->fav - in->fiso) - (in->stv + in->sav - in->siso) + in->trim;
}

/* ------------------------------------------------------------------------------------ smoothing */
void wf_exp_sm_reset(struct wf_exp_sm *s)
{
    s->s16 = s->q = s->flash = s->init = 0;
}

static int wf_exp_clampq(int v) { return v < WF_EXP_QMIN ? WF_EXP_QMIN : v > WF_EXP_QMAX ? WF_EXP_QMAX : v; }

int wf_exp_sm_step(struct wf_exp_sm *s, int dev8)
{
    if (dev8 == WF_EXP_BAD)                       /* no inputs: hold what is applied (flash: none) */
        return s->flash ? 0 : s->q;
    int first = !s->init;
    if (first) {
        s->s16 = dev8 * 16;
        s->init = 1;
    } else {
        s->s16 += (dev8 * 16 - s->s16) / 4;       /* IIR, 1/4 per frame */
    }
    int v = (s->s16 + 8) >> 4;                    /* smoothed dEV, 1/8 EV */
    if (!s->flash && v < WF_EXP_FLASH_ON)
        s->flash = 1;
    else if (s->flash && v >= WF_EXP_FLASH_OFF)
        s->flash = 0;
    if (s->flash) {
        s->q = 0;
        return 0;
    }
    v = wf_exp_clampq(v);
    int d = v - s->q;
    if (first || d >= WF_EXP_HYST || d <= -WF_EXP_HYST)   /* hysteresis: +-1/4 EV */
        s->q = v;
    return s->q;
}

/* ------------------------------------------------------------------------------------- tables */
static uint32_t wf_exp_gain(int q)               /* 2^(q/8) in Q16 */
{
    int e = q >> 3;
    uint32_t g = wf_exp_g8[q & 7];
    return e >= 0 ? g << e : (g + (1u << (-e - 1))) >> -e;
}

static int wf_exp_enc(uint32_t L)                /* nearest code whose linear value is L (Q16) */
{
    if (L >= 65535)
        return 255;
    int lo = 0, hi = 255;                        /* largest y with dec[y] <= L */
    while (lo < hi) {
        int m = (lo + hi + 1) >> 1;
        if (wf_exp_dec[m] <= L) lo = m; else hi = m - 1;
    }
    if (lo < 255 && 2 * L >= (uint32_t)wf_exp_dec[lo] + wf_exp_dec[lo + 1])
        lo++;
    return lo;
}

void wf_exp_build(struct wf_exp_tab *t, int q)
{
    uint32_t G = wf_exp_gain(q);
    t->q = q;
    t->cs[0] = 256;
    for (int y = 0; y < 256; y++) {
        uint64_t L64 = ((uint64_t)wf_exp_dec[y] * G + 32768) >> 16;
        uint32_t L = L64 > 0xfffffff ? 0xfffffff : (uint32_t)L64;
        int yy = wf_exp_enc(L);
        t->gy[y] = yy;
        if (y == 0)
            continue;
        uint32_t r = ((uint32_t)yy * 256 + y / 2) / y;     /* Y'/Y, Q8 */
        if (L > 65535) {                         /* clipped: the chroma ramps down to 0 (white) between L = 1 and 4 times white */
            uint32_t f = L >= 4 * 65535u ? 0 : (4 * 65535u - L) * 256 / (3 * 65535u);
            r = (r * f) >> 8;
        }
        t->cs[y] = r > 1024 ? 1024 : r;
    }
}

/* ------------------------------------------------------------------------------ per frame state */
static struct wf_exp_sm wf_exp_s;
static struct wf_exp_tab wf_exp_t;
static int wf_exp_built;

/* flag reads only (wf_dispf(0) calls it from any task) */
int wf_exp_active(void)
{
    return wf_exp_mode == WF_EXP_SMOOTH && lv && !is_movie_mode() && shooting_mode != SHOOTMODE_MOVIE_
        && !is_play_mode() && get_expsim() == 0;
}

static void wf_exp_read(struct wf_exp_in *in)
{
    const volatile uint8_t *vp = *(const volatile uint8_t *volatile *)wf_exp_vpp;
    in->fiso = vp ? vp[0] : 0;
    in->fav = vp ? vp[1] : 0;
    in->ftv = vp ? vp[2] : 0;
    in->stv = lens_info.raw_shutter;
    in->sav = lens_info.raw_aperture;
    in->siso = lens_info.raw_iso;
    in->ec = lens_info.ae;
    in->trim = wf_exp_trim;
    in->metered = !(shooting_mode == SHOOTMODE_M_ && lens_info.raw_iso != 0);   /* P, Tv, Av, scene, M + Auto ISO */
}

const struct wf_exp_tab *wf_exp_frame(void)
{
    if (!wf_exp_active()) {
        wf_exp_sm_reset(&wf_exp_s);
        wf_exp_q_last = 0;
        wf_exp_flash_last = 0;
        wf_exp_dev_last = WF_EXP_BAD;
        return 0;
    }
    struct wf_exp_in in;
    wf_exp_read(&in);
    int dev = wf_exp_dev8(&in);
    int q = wf_exp_sm_step(&wf_exp_s, dev);
    wf_exp_dev_last = dev;
    wf_exp_q_last = q;
    wf_exp_flash_last = wf_exp_s.flash;
    if (!q)
        return 0;
    if (!wf_exp_built || wf_exp_t.q != q) {
        wf_exp_build(&wf_exp_t, q);
        wf_exp_built = 1;
    }
    return &wf_exp_t;
}

/* ------------------------------------------------------------------------------------ wf_task */
static int wf_exp_set;                           /* we wrote "off" and still owe the "on" */
static int wf_exp_tries;
static int wf_exp_drawn;
#define WF_FLASH_X  300
#define WF_FLASH_Y  40

void wf_exp_poll(void)
{
    static int lq = 1000, lf = -1, lc = -1;
    /* only while the filter can run (hooks up, Global Draw on): else ExpSim off would leave an uncorrected picture */
    int want = wf_exp_mode == WF_EXP_SMOOTH && wf_yuv_active() && (get_global_draw_setting() & 1) && lv && !is_movie_mode()
        && shooting_mode != SHOOTMODE_MOVIE_ && !is_play_mode();
    int es = get_expsim();
    if (want) {
        if (es != 0) {                            /* ExpSim on: ask the camera for Disable, as Canon's menu does */
            if (wf_exp_tries < 3) {
                int v = WF_EXPSIM_OFF;
                wf_exp_tries++;
                wf_exp_set = 1;
                prop_request_change_wait(WF_PROP_EXPSIM, &v, 4, 1000);
                if (get_expsim() == 0) {
                    wf_exp_ctl = WF_EXPC_SET;
                    wf_log("expsim: off by us (try %d)", wf_exp_tries);
                } else
                    wf_log("expsim: write not taken (try %d, get_expsim %d)", wf_exp_tries, get_expsim());
            } else {
                wf_exp_ctl = WF_EXPC_FAIL;        /* the write does not take: the user sets Disable in Canon's menu */
            }
        } else {
            wf_exp_ctl = wf_exp_set ? WF_EXPC_SET : WF_EXPC_USER;
            wf_exp_tries = 0;
        }
    } else {
        if (wf_exp_set && es == 0) {              /* give it back (not when the user already changed it) */
            int v = WF_EXPSIM_ON;
            prop_request_change_wait(WF_PROP_EXPSIM, &v, 4, 1000);
            wf_log("expsim: back on (get_expsim %d)", get_expsim());
        }
        wf_exp_set = 0;
        wf_exp_tries = 0;
        wf_exp_ctl = WF_EXPC_IDLE;
    }
    int act = wf_exp_active();
    if (act && wf_exp_flash_last) {
        bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_BLACK), WF_FLASH_X, WF_FLASH_Y, " flash? ");
        wf_exp_drawn = 1;
    } else if (wf_exp_drawn) {
        bmp_fill(COLOR_EMPTY, WF_FLASH_X, WF_FLASH_Y, 100, 24);
        wf_exp_drawn = 0;
    }
    if (wf_exp_q_last != lq || wf_exp_flash_last != lf || wf_exp_ctl != lc) {
        lq = wf_exp_q_last; lf = wf_exp_flash_last; lc = wf_exp_ctl;
        if (act) {
            const volatile uint8_t *vp = *(const volatile uint8_t *volatile *)wf_exp_vpp;
            wf_log("expsim: dEV %d q %d flash %d ctl %d | frame iso %d av %d tv %d | shot iso %d av %d tv %d ec %d m %d",
                   wf_exp_dev_last, lq, lf, lc, vp ? vp[0] : -1, vp ? vp[1] : -1, vp ? vp[2] : -1,
                   lens_info.raw_iso, lens_info.raw_aperture, lens_info.raw_shutter, lens_info.ae, shooting_mode);
        }
    }
}

/* wf_deinit (module unload): the camera's ExpSim setting back (no waiting there) */
void wf_exp_deinit(void)
{
    if (wf_exp_set && get_expsim() == 0) {
        int v = WF_EXPSIM_ON;
        prop_request_change(WF_PROP_EXPSIM, &v, 4);
    }
    wf_exp_set = 0;                              /* (the mode stays: wf_menu_deinit may still save it; wf_unloading stops the filter) */
}
#endif /* WF_BUILD_5D2 */
