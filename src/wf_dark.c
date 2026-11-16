/* wf_dark.c - Roll + Darkroom core (docs/DESIGN.md 3.6), WindyStrife's own code, written from scratch for WindyFilm.
 *
 * The roll (wf_task, wf_roll_poll): the roll file through wf_fio.h, the requests of the UI / menu, frame detection from
 * Canon's file number while Canon is idle, RAW+L and image review while a roll is in the camera, the HUD over LiveView.
 * The darkroom (wf_dark_poll: inside wf_task, one run at a time): the look's effects, memory, then per frame the CR2
 * read + decode (wf_cr2.c), the develop (wf_dev.c), the look stage through the worker (wf_shot_run with frame.devd), the
 * print encode (wf_jpe.c) and write, the roll file, the view the UI draws (struct wf_dark_view). No task of its own: the
 * owner's RAWB9 camera refused "wf_dark" every time (DryOS's table full, "tasks 104 of 104", the UI task on the last
 * slot: "dark: no task (5)"). Between its bands the run calls wf_task_yield (the ML-menu guard, the browser request);
 * the UI task (prio 0x1B) runs above wf_task (0x1E) and stays live. While a run lives it owns the roll.
 * The pure per-frame parts (scales, Exif, thumbnails) are wf_darkp.c, shared with the host test.
 *
 * The look while a roll is in the camera (owner RAWB9: each frame took 23..36 s of the in-camera look): wf_look_use. */
#include "wf_core.h"
#include "wf_dark.h"
#include "wf_fx.h"
#include "wf_loglut.h"
#include "wf_rawb.h"                             /* B gives its shoot memory back before a darkroom run */
#ifdef WF_BUILD_5D2
#define WF_DARK_CAN 0                            /* the 5D2 follows once the EOS M works (raw 5792 x 3804, GBRG) */
#else
#define WF_DARK_CAN 1
#include "wf_darkp.h"
#include "wf_cr2.h"
#include "wf_dev.h"
#include "wf_jpe.h"
#if WF_DK_EV_BAD != WF_EV_BAD
#error "wf_darkp.h WF_DK_EV_BAD must equal wf_fx.h WF_EV_BAD"
#endif
#include "wf_print.h"
#endif

/* ML core (EOSM_202.sym / 5D2_212.sym both export these) */
extern void *FIO_OpenFile(const char *name, unsigned mode);
extern int   FIO_ReadFile(void *f, void *buf, size_t n);
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
extern int  prop_request_change(unsigned property, const void *addr, size_t len);
extern void PauseLiveView(void);
extern int  ResumeLiveView(void);
extern void powersave_prohibit(void);
extern void powersave_permit(void);
extern int  display_is_on(void);
extern void display_on(void);                    /* ML: TurnOnDisplay from the GUI task (MLEV_TURN_ON_DISPLAY) */
extern void display_off(void);                   /* ML: TurnOffDisplay from the GUI task (MLEV_TURN_OFF_DISPLAY) */
extern int  is_movie_mode(void);
extern int  gui_menu_shown(void);
extern uint8_t *bmp_vram(void);
extern void redraw(void);
extern void NotifyBox(int timeout, const char *fmt, ...);
extern void LoadCalendarFromRTC(void *tm);
extern void *srm_malloc_suite(int n);
extern void  srm_free_suite(void *suite);
extern void *shoot_malloc_suite_contig(size_t size);   /* 0 = the biggest block there is */
extern void *shoot_malloc_suite(size_t size);          /* chunks anywhere (ML exmem.c) */
extern void  shoot_free_suite(void *suite);
extern void *GetFirstChunkFromSuite(void *suite);
extern void *GetNextMemoryChunk(void *suite, void *chunk);
extern void *GetMemoryAddressOfMemoryChunk(void *chunk);
extern int   GetSizeOfMemoryChunk(void *chunk);
extern int  ml_shutdown_requested;
extern int  lv, pic_quality, image_review_time, battery_level_bars;
#ifdef WF_BUILD_5D2
extern int  GetBatteryLevel(void);
#endif

/* ML property.h / module.h values (both bodies) */
#define WF_PROP_PIC_QUALITY 0x8000002f
#define WF_PICQ_RAW_L       0x3070000            /* PICQ_RAW_JPG_LARGE_FINE */
#define WF_PROP_REVIEW      0x02020006           /* PROP_IMAGE_REVIEW_TIME: 0, 2, 4, 8, 0xff */
#define WF_BMPPITCH         960
#define WF_CACHEABLE(p)     ((void *)((uint32_t)(p) & ~0x40000000u))   /* ML mem_defs.h CACHEABLE (DIGIC 4 / 5) */

/* wf_menu.c (it has ML's card headers) */
int  wf_card_nums(int *folder, int *file);       /* Canon's DCIM folder / last file number, 0 = ok */
const char *wf_dcim_dir(void);                   /* get_dcim_dir() */
const char *wf_file_prefix(void);                /* get_file_prefix() */
const char *wf_exif_text(const struct wf_look *lk, int g, int h, int bl, int m, int ev, char *b, int n);

/* ================================================================== the roll (wf_task) */
static struct wf_roll roll;
static volatile uint32_t roll_gen_n = 1;
static int roll_read_done, roll_have_file, roll_dirty, roll_save_fail_t, roll_save_fails;
static struct wf_fe roll_save_fe;
static char roll_txt[WF_ROLL_FILE] __attribute__((aligned(32)));
volatile int wf_blind = 1;
static volatile int req_kind, req_film, req_exp;  /* 1 load, 2 rewind, 3 close, 4 close + load (swap) */
static int poll_F = -1, poll_D = -1, poll_stable, wind_t = -100000;
static int pq_old = -1, rv_old = -1, prop_t;
static int hud_on, hud_fonts_on, hud_t, hud_mode = -1, info_t = -100000, hud_sig, hud_job;
static int hud_font_again, hud_font_t, hud_font_n;  /* the font pack read again while Canon held the card */
static volatile int dk_running;                   /* a darkroom run owns the roll (asked for, or in wf_task now) */
static volatile int dk_go;                        /* wf_dark_start asked: wf_task's next wf_dark_poll runs it */
static int nt_state = -1, nt_req, nt_t, nt_len, nt_mode = -1, nt_pt;   /* the end-of-roll notice (roll_notice) */
static volatile int nt_up, nt_drop, nt_poke;
static volatile int sh_k;                         /* the shutter held now (roll_shutter): 0 no, 1 full, 2 rewound */
static volatile int shelf;                        /* the roll set aside (wf_roll_shelve, wf_dark.h): RAM only */

const struct wf_roll *wf_roll_now(void) { return &roll; }
uint32_t wf_roll_gen(void) { return roll_gen_n; }
static void roll_changed(void) { roll_gen_n++; wf_gen_bump(); }
int wf_roll_shooting(void) { return roll.state == WF_RS_LOADED; }
int wf_roll_shelved(void) { return shelf && wf_roll_open(&roll); }
int wf_roll_blocks_play(void) { return wf_blind && wf_roll_open(&roll) && !wf_roll_shelved() && !dk_running; }

/* any task (the UI's mode chip); wf_task's next poll applies it (shutter, look, notice) */
int wf_roll_shelve(int on)
{
    if (on && !wf_roll_open(&roll))
        return -1;
    if (!on == !shelf)
        return 0;
    shelf = on ? 1 : 0;
    wf_log("roll: %03d %s (%d/%d %s)", roll.num, on ? "set aside: the camera shoots in another mode, PLAY develops it"
           : "back from the shelf", roll.nshot, roll.exp, wf_roll_state_name(roll.state));
    roll_changed();
    return 0;
}
int wf_dark_busy(void) { return dk_running; }

/* what the look's tables (the shot LUT, halation / grain / mist) are built for (owner RAWB9, 11-15: each roll frame took
 * 23..36 s of the in-camera look, the LUT read 21 s and the grain 8..13 s at boot and at each roll load, and the
 * Darkroom prints the roll later anyway). Any task (the hook reads it too: plain reads).
 *   WF_LU_SHOTS  no roll in the camera, as before; also a roll with RAW develop B on (the owner's opt-in: B develops
 *                each roll frame now and gives it the look, the Darkroom prints it later)
 *   WF_LU_NONE   a roll in the camera (loaded, full or rewound): nothing of the look is built (given back if it was),
 *                and a frame keeps Canon's own JPEG (the hook skips the worker): only Canon's own wait
 *   WF_LU_DARK   a darkroom run: the effects and the film table for its look (no shot LUT: its develop holds the cube) */
int wf_look_use(void)
{
    if (dk_running)
        return WF_LU_DARK;
    if (wf_roll_open(&roll) && !wf_roll_shelved() && !(WF_RAWB_CAN && wf_rawb)    /* shelved: the chip's mode shoots */
        && !wf_roll_look8)                                                        /* a JPEG 8-bit roll: the look now */
        return WF_LU_NONE;
    return WF_LU_SHOTS;
}

/* GUI task (wf_main.c wf_keypress): a key while the end-of-roll notice is up drops it (wf_task clears it on its next
 * poll); what 1 (SET / Q) opens the Darkroom on top, 2 (INFO) the browser in Darkroom mode for a new roll (the
 * Exposures row; owner 11-17: the old Roll picker screen is gone). Never in the ML menu (the
 * notice is not drawn there either). */
int wf_roll_notice_key(int what)
{
    if (!nt_up || gui_menu_shown())
        return 0;
    nt_drop = 1;
    if (what)
        wf_ui_open_scr(what == 1 ? WF_UI_SCR_DARK : WF_UI_NEW_ROLL);
    return 1;
}

/* GUI task: a half / full shutter press on the LiveView screen while the shutter is held (roll_shutter): kept from Canon
 * (ML's UI lock holds the shutter itself; this also keeps the half-press AF from starting), the notice shows at once */
int wf_roll_shutter_key(void)
{
    if (!sh_k || !lv || gui_menu_shown() || is_play_mode() || wf_ui_is_open())
        return 0;
    nt_poke = 1;
    return 1;
}
int wf_roll_shutter_locked(void) { return sh_k; }

int wf_roll_req_load(int film, int exp)
{
    if (!WF_DARK_CAN)
        return -4;
    if (req_kind || dk_running)
        return -3;
    if (wf_roll_open(&roll) && roll.nshot > 0)
        return -1;
    if (!wf_roll_ok_exp(exp) || !wf_film_by_id(film))
        return -2;
    req_film = film;
    req_exp = exp;
    WF_BARRIER();
    req_kind = 1;
    return 0;
}
int wf_roll_req_rewind(void)
{
    if (req_kind || dk_running)
        return -3;
    if (!((roll.state == WF_RS_LOADED && roll.nshot > 0) || roll.state == WF_RS_FULL))
        return -1;
    req_kind = 2;
    return 0;
}
int wf_roll_req_close(void)
{
    if (req_kind || dk_running)
        return -3;
    if (!wf_roll_open(&roll))
        return -1;
    req_kind = 3;
    return 0;
}
int wf_roll_req_swap(int film, int exp)
{
    if (!WF_DARK_CAN)
        return -4;
    if (req_kind || dk_running)
        return -3;
    if (!(roll.state == WF_RS_FULL || (roll.state == WF_RS_REWOUND && wf_roll_left(&roll) > 0)))
        return -1;
    if (!wf_roll_ok_exp(exp) || !wf_film_by_id(film))
        return -2;
    req_film = film;
    req_exp = exp;
    WF_BARRIER();
    req_kind = 4;
    return 0;
}

int wf_roll_hud(char *line, int n)
{
    if (!wf_roll_open(&roll))
        return 0;
    const struct wf_film *f = wf_film_by_id(roll.film);
    snprintf(line, n, "ROLL %03d  %s  %d/%d", roll.num, f ? f->shortname : "?", roll.nshot, roll.exp);
    return 1;
}

/* the roll file: read once at boot; saved whole, created once, then rewritten in place (trap W24) */
static void roll_read(void)
{
    uint32_t size = 0;
    wf_roll_reset(&roll);
    roll_have_file = FIO_GetFileSize(WF_ROLL_PATH, &size) == 0;
    if (!roll_have_file)
        return;
    if (size == 0) {                             /* RAWB8's card: created, then the write never came */
        wf_log("roll: %s is empty (a save that did not finish): no roll", WF_ROLL_PATH);
        return;                                  /* (roll_have_file: the next save writes it in place) */
    }
    uint32_t n = size < WF_ROLL_FILE ? size : WF_ROLL_FILE;
    if (wf_card_read(WF_ROLL_PATH, 0, roll_txt, n, 0) != 0 || wf_roll_parse(&roll, roll_txt, (int)n) != 0) {
        wf_log("roll: %s unreadable (%d bytes): no roll", WF_ROLL_PATH, (int)size);
        wf_roll_reset(&roll);
        return;
    }
    if (size != WF_ROLL_FILE)
        roll_dirty = 1;                          /* rewritten at its full size */
    wf_log("roll: %03d %s film %d %d/%d", roll.num, wf_roll_state_name(roll.state), roll.film, roll.nshot, roll.exp);
}

/* a failed save: logged once per failing step (not at every 3 s try), "saved after n tries" once it went */
static int roll_save_err(int step, int rc)
{
    if (!roll_save_fails++ || step != roll_save_fe.step)
        wf_log("roll: %s not saved (%s rc %d), retry", WF_ROLL_PATH, wf_fe_name(step), rc);
    roll_save_fe.step = step;
    roll_save_fe.rc = rc;
    return -1;
}
static int roll_save(void)
{
    if (wf_roll_text(&roll, roll_txt, sizeof roll_txt) != WF_ROLL_FILE)
        return roll_save_err(WF_FE_WRITE, -1);
    struct wf_fe e = { 0, 0 };
    void *f = wf_fopen_w(WF_ROLL_PATH, roll_have_file ? WF_FO_AT : WF_FO_NEW, 0, &e);
    if (!f) {
        if (e.step == WF_FE_OPEN && roll_have_file)
            roll_have_file = 0;                  /* gone (card swapped): created again next time */
        return roll_save_err(e.step, e.rc);
    }
    roll_have_file = 1;                          /* on the card now: a later try opens it in place (no create) */
    int w = FIO_WriteFile(f, roll_txt, WF_ROLL_FILE);
    FIO_CloseFile(f);
    if (w != WF_ROLL_FILE)
        return roll_save_err(WF_FE_WRITE, w);
    if (roll_save_fails)
        wf_log("roll: %s saved after %d tries", WF_ROLL_PATH, roll_save_fails + 1);
    roll_save_fails = 0;
    roll_dirty = 0;
    return 0;
}

static int roll_cr2_at(int folder, int file, const char *ext, char *out, int n)
{
    struct wf_roll t;
    t.nshot = 1;
    t.folder[0] = (uint16_t)folder;
    t.file[0] = (uint16_t)file;
    return wf_roll_cr2(&t, 0, wf_dcim_dir(), wf_file_prefix(), ext, out, n);
}
static int roll_has_cr2(void *ctx, int folder, int file)
{
    (void)ctx;
    char p[96];
    uint32_t sz = 0;
    return roll_cr2_at(folder, file, "CR2", p, sizeof p) > 0 && FIO_GetFileSize(p, &sz) == 0 && sz > 0;
}

/* RAW+L and image review while a roll is in the camera; Canon's values back afterwards */
static void roll_props(int idle)
{
    int now = get_ms_clock();
    if (now - prop_t < 2000 || !idle || is_movie_mode())
        return;
    int want = wf_roll_can_shoot(&roll) && !dk_running, blind = want && wf_blind;
    if (want && pic_quality != WF_PICQ_RAW_L) {
        if (pq_old < 0)
            pq_old = pic_quality;
        int v = WF_PICQ_RAW_L;
        prop_request_change(WF_PROP_PIC_QUALITY, &v, 4);
        wf_log("roll: image quality %x -> RAW+L", (unsigned)pq_old);
        prop_t = now;
    } else if (!want && pq_old >= 0) {
        if (pic_quality != pq_old)
            prop_request_change(WF_PROP_PIC_QUALITY, &pq_old, 4);
        wf_log("roll: image quality back to %x", (unsigned)pq_old);
        pq_old = -1;
        prop_t = now;
    }
    if (blind && image_review_time != 0) {
        if (rv_old < 0)
            rv_old = image_review_time;
        int v = 0;
        prop_request_change(WF_PROP_REVIEW, &v, 4);
        wf_log("roll: image review %d -> off (blind roll)", rv_old);
        prop_t = now;
    } else if (!blind && rv_old >= 0) {
        if (image_review_time == 0)
            prop_request_change(WF_PROP_REVIEW, &rv_old, 4);
        wf_log("roll: image review back to %d", rv_old);
        rv_old = -1;
        prop_t = now;
    }
}

/* the end-of-roll notice (owner 11-15, wfdr_notice): NT_FIRST_MS when the roll ends on this boot (its last frame counts,
 * End roll now), NT_AGAIN_MS each time LiveView comes back (or the menu / UI / PLAY goes) and after each shot while it
 * stays FULL / REWOUND, then the HUD's fade. A key drops it at once (wf_roll_notice_key); the darkroom run, the ML menu,
 * the UI, PLAY and movie mode never get it. Called by roll_hud with the HUD's own want, before the HUD's pixels. */
#define NT_FIRST_MS 5000
#define NT_AGAIN_MS 2500
#define NT_FADE_MS  400                          /* = HUD_FADE_MS (below) */
static void roll_notice(int want, int now)
{
    int endk = roll.state == WF_RS_FULL ? 1 : roll.state == WF_RS_REWOUND && wf_roll_left(&roll) > 0 ? 2 : 0;
    if (nt_state == WF_RS_LOADED && endk)
        nt_req = NT_FIRST_MS;                    /* the roll ended now (waits for LiveView if it is not there) */
    nt_state = roll.state;
    want = want && endk && !is_movie_mode();
    if (nt_poke) {                               /* a shutter press on the held roll (wf_roll_shutter_key): now */
        nt_poke = 0;
        if (endk) {
            nt_req = nt_req > NT_AGAIN_MS ? nt_req : NT_AGAIN_MS;
            nt_drop = 0;
        }
    }
    if (!endk)
        nt_req = 0;
    else if (info_t == now)                      /* roll_hud: LiveView back, a shot done, a new state (the HUD info rule) */
        nt_req = nt_req > NT_AGAIN_MS ? nt_req : NT_AGAIN_MS;
    if (!want) {                                 /* gone with LiveView / under the menu or UI: roll_hud's redraw covers it */
        nt_len = nt_drop = nt_up = 0;
        return;
    }
    if (nt_req && !nt_drop) {                    /* a new notice, or more time for the one on screen */
        int rest = nt_len ? nt_len - (now - nt_t) : 0;
        if (!nt_len) {
            wf_log("roll: end notice %s", endk == 1 ? "full" : "rewound");
            nt_mode = -1;
        }
        if (nt_req > rest) {
            nt_len = nt_req;
            nt_t = now;
        }
        nt_req = 0;
    }
    if (!nt_len)
        return;
    int t = now - nt_t;
    int mode = nt_drop ? WF_HUD_CLEAR : t < nt_len ? WF_HUD_FULL : t < nt_len + NT_FADE_MS ? WF_HUD_FADE : WF_HUD_CLEAR;
    if (gui_menu_shown() || wf_ui_is_open())     /* (again right before the pixels) */
        return;
    if (mode == WF_HUD_CLEAR) {
        wfdr_notice(bmp_vram(), WF_BMPPITCH, WF_HUD_CLEAR);
        redraw();                                /* Canon's own LiveView marks under the box come back */
        hud_mode = -1;                           /* (the HUD paints again after it) */
        nt_len = nt_drop = nt_up = 0;
        return;
    }
    if (mode != nt_mode || now - nt_pt >= 500) {
        wfdr_notice(bmp_vram(), WF_BMPPITCH, mode);
        nt_mode = mode;
        nt_pt = now;
    }
    nt_up = 1;
}

/* the HUD over LiveView (DESIGN 3.6, owner 11-14): the roll + film chip, the big counter and the ladder show for
 * HUD_INFO_MS, fade for HUD_FADE_MS, then only a small dim counter stays. They show again when LiveView comes back
 * (or the menu / UI / PLAY goes), after a shot, after a frame counts, and when the roll, its film or its state changes. */
#define HUD_INFO_MS 2500
#define HUD_FADE_MS 400
static void roll_hud(void)
{
    int want = wf_roll_open(&roll) && !wf_roll_shelved() && !dk_running && lv && !gui_menu_shown() && !wf_ui_is_open()
               && !is_play_mode();          /* a shelved roll (wf_roll_shelve): no HUD, no end notice */
    int now = get_ms_clock();
    if (wf_roll_open(&roll) && !hud_fonts_on && !wf_ui_is_open()) {
        hud_font_again = wfdr_hud_fonts(1) == 1;
        hud_fonts_on = 1;
        hud_font_t = now;
        hud_font_n = 0;
    } else if (!wf_roll_open(&roll) && hud_fonts_on && !wf_ui_is_open()) {
        wfdr_hud_fonts(0);
        hud_fonts_on = 0;
    } else if (hud_fonts_on && hud_font_again && !wf_ui_is_open() && now - hud_font_t >= 2000) {
        hud_font_t = now;                        /* (the UI reads it again itself while it is open) */
        hud_font_again = wfdr_hud_fonts(2) == 1;
        if (++hud_font_n >= 5 || !hud_font_again) {
            wf_log("roll: HUD fonts %s after %d reads again", hud_font_again ? "still not read" : "read", hud_font_n);
            hud_font_again = 0;
        }
    }
    int sig = (int)(roll.num * 4099u + roll.film * 37u + roll.exp * 5u + roll.state) ^ (wf_loaded() << 20);
    if (sig != hud_sig) {                        /* a new roll, film or state */
        hud_sig = sig;
        info_t = now;
    }
    if (wf_job_state())
        hud_job = 1;                             /* a shot: the info again once Canon is idle */
    else if (hud_job) {
        hud_job = 0;
        info_t = now;
    }
    if (want && !hud_on)
        info_t = now;                            /* LiveView is back */
    roll_notice(want, now);
    if (want) {
        int blink = now - wind_t < 1000, t = now - info_t;
        int mode = t < HUD_INFO_MS ? WF_HUD_FULL : t < HUD_INFO_MS + HUD_FADE_MS ? WF_HUD_FADE : WF_HUD_SMALL;
        if ((!hud_on || mode != hud_mode || blink || now - hud_t >= 500) && !gui_menu_shown() && !wf_ui_is_open()) {
            /* (again right before the pixels: the ML menu or the UI may have come since the top; never over them) */
            /* the first small pass clears what the full HUD drew (only our own rectangles) */
            wfdr_hud(bmp_vram(), WF_BMPPITCH, blink, mode == WF_HUD_SMALL && hud_mode != WF_HUD_SMALL ? WF_HUD_CLEAR : mode);
            hud_t = now;
            hud_mode = mode;
        }
    } else {
        if (hud_on && !wf_ui_is_open())
            redraw();
        hud_mode = -1;
    }
    hud_on = want;
}

/* owner 11-15: no shot on a finished roll. While the roll is FULL, or REWOUND with frames to develop, ML's UI lock holds
 * Canon's shutter (gui_uilock UILOCK_SHUTTER, what ML's own exmem.c does against ERR70; wf_task only: it waits for the
 * property). Never saved anywhere: worked out from the roll at every poll, so it lets go once the roll is closed or a
 * new one loaded, while the darkroom develops, in movie mode, while recording, and at unload (wf_dark_end). With no
 * roll nothing here ever runs gui_uilock. */
#define WF_UILOCK_SHUTTER 0x0001                 /* ML property.h UILOCK_SHUTTER */
extern int icu_uilock, __recording, __recording_custom;
extern void gui_uilock(int what);
static void roll_shutter(int let_go)
{
    int k = roll.state == WF_RS_FULL ? 1 : roll.state == WF_RS_REWOUND && wf_roll_left(&roll) > 0 ? 2 : 0;
    if (!WF_DARK_CAN || let_go || dk_running || wf_roll_shelved() || is_movie_mode() || __recording || __recording_custom)
        k = 0;                                   /* (shelved: the chip's other mode shoots, the frames wait) */
    if (!k == !sh_k) {
        sh_k = k;                                /* (full <-> rewound: the lock stays) */
        return;
    }
    gui_uilock(k ? (icu_uilock | WF_UILOCK_SHUTTER) : (icu_uilock & ~WF_UILOCK_SHUTTER));
    sh_k = k;
    if (k)
        wf_log("roll: shutter blocked (%s), UI lock %x", k == 1 ? "full" : "rewound", (unsigned)icu_uilock);
    else
        wf_log("roll: shutter free, UI lock %x", (unsigned)icu_uilock);
    roll_changed();
}

static void roll_req(void)
{
    int k = req_kind, F = 0, D = 0;
    if (!k)
        return;
    const struct wf_film *f = wf_film_by_id(req_film);
    if (k != 2 && shelf) {                       /* a load, close or close-and-load ends the shelf; a rewind keeps it */
        shelf = 0;
        wf_log("roll: %03d off the shelf (%s)", roll.num, k == 3 ? "close" : "a new roll");
    }
    if (k == 4) {                                /* the browser's close-and-load: the finished roll first */
        int left = wf_roll_left(&roll);
        if (wf_roll_close(&roll) == 0)
            wf_log("roll: %03d closed for a new roll, %d frame(s) kept undeveloped", roll.num, left);
        k = 1;
    }
    if (k == 1) {
        if (wf_card_nums(&D, &F) != 0) {
            wf_log("roll: no file number, load refused");
        } else if (wf_roll_load(&roll, req_film, req_exp, D, F) == 0) {
            int rc = wf_loaded() == req_film ? WF_LOAD_OK : wf_load(req_film);
            wf_log("roll: %03d loaded, %s x%d at %d/%04d (film load %d)", roll.num, f ? f->name : "?", roll.exp, D, F, rc);
            if (WF_RAWB_CAN && wf_rawb)          /* both modes at once (DESIGN 3.6): B at shot time, the print later */
                wf_log("roll: RAW develop stays on (frames get B now, a print later)");
            poll_F = F;
            poll_D = D;
            roll_dirty = 1;
        }
    } else if (k == 2) {
        if (wf_roll_rewind(&roll) == 0) {
            wf_log("roll: %03d rewound at %d/%d", roll.num, roll.nshot, roll.exp);
            roll_dirty = 1;
        }
    } else if (k == 3) {
        int left = wf_roll_left(&roll);
        if (wf_roll_close(&roll) == 0) {
            wf_log("roll: %03d closed, %d frame(s) kept undeveloped", roll.num, left);
            roll_dirty = 1;
        }
    }
    WF_BARRIER();
    req_kind = 0;
    roll_changed();
}

void wf_roll_poll(void)
{
    if (!roll_read_done) {
        roll_read_done = 1;
        roll_read();
        roll_changed();
    }
    if (dk_running) {
        hud_on = 0;
        nt_state = -1;                           /* a run rewinds the roll: no end notice for that */
        nt_req = nt_len = nt_up = 0;
        roll_shutter(0);                         /* developing: the shutter free */
        return;
    }
    int idle = wf_job_state() == 0 && !wf_shot_pending();
    if (idle)
        roll_req();
    if (shelf && !wf_roll_open(&roll)) {         /* developed (done) or closed: nothing on the shelf any more */
        shelf = 0;
        wf_log("roll: %03d %s: off the shelf", roll.num, wf_roll_state_name(roll.state));
        roll_changed();
    }
    if (idle && roll.state == WF_RS_LOADED) {    /* new frames: the file number stable over two polls */
        int D = 0, F = 0;
        if (wf_card_nums(&D, &F) == 0) {
            if (F != poll_F || D != poll_D) {
                poll_F = F;
                poll_D = D;
                poll_stable = 0;
            } else if (++poll_stable == 2 && (F != roll.seen_file || D != roll.seen_folder)) {
                int n0 = roll.nshot, n = wf_roll_seen(&roll, D, F, roll_has_cr2, 0);
                roll_dirty = 1;
                if (n > 0) {
                    wind_t = info_t = get_ms_clock();
                    wf_log("roll: %03d frame %d/%d IMG_%04d%s", roll.num, roll.nshot, roll.exp, roll.file[roll.nshot - 1],
                           roll.state == WF_RS_FULL ? ", end of roll" : "");
                    roll_changed();
                } else if (n0 == roll.nshot) {
                    wf_log("roll: file %d/%04d is not a frame (no CR2)", D, F);
                }
            }
        }
    }
    roll_props(idle);
    roll_shutter(0);
    if (roll_dirty && idle && get_ms_clock() - roll_save_fail_t > 3000 && roll_save() != 0)
        roll_save_fail_t = get_ms_clock();       /* (roll_save logged it) */
    roll_hud();
}

/* ================================================================== the darkroom (wf_task) */
#define DK_W        5184                        /* the EOS M L frame in its CR2 (DESIGN 3.2: x0 84, y0 64) */
#define DK_H        3456
#define DK_X0       84
#define DK_Y0       64
#define DK_RD       (256 * 1024)                 /* card reads / writes through the DMA buffer */
#define DK_IN       (256 * 1024)                 /* the decoder's cached input */
#define DK_OB       (64 * 1024)                  /* encoder output buffer */
#define DK_APP1     (64 * 1024)
#define DK_BAND     64
#define DK_JROWS    64
#define DK_BAT_GO   30
#define DK_BAT_STOP 15
#define DK_RAW_N    (9240u * 3528u)              /* the EOS M CR2's sensor image, ML packed 14-bit */

static struct wf_dark_view dv;
static uint8_t dv_print[WF_DR_PW * WF_DR_PH];
static uint8_t dv_wire[WF_DR_NWIRE][WF_DR_TW * WF_DR_TH];
static volatile int dk_stop;
static int dk_t0, dk_sec;

static void dv_bump(void) { dv.gen++; }

const struct wf_dark_view *wf_dark_view(void)
{
    if (dv.state == WF_DR_RUN || dv.state == WF_DR_STOPPING) {
        int now = get_ms_clock();
        dv.t_frame = now - dk_t0;                /* live, so the dial moves while the worker holds the frame */
        if ((now >> 10) != dk_sec) {
            dk_sec = now >> 10;
            dv.gen++;
        }
    }
    return &dv;
}

int wf_dark_eta_s(const struct wf_dark_view *v)
{
    int per = v->avg_ms ? v->avg_ms : 55000;
    int left = v->state == WF_DR_IDLE || v->state == WF_DR_STOPPED || v->state == WF_DR_FAIL ? wf_roll_left(&roll) : v->left;
    if (v->state == WF_DR_IDLE && roll.state == WF_RS_LOADED)
        left = roll.nshot;
    int s = (left * (per / 100) - (v->state == WF_DR_RUN ? v->t_frame / 100 : 0)) / 10;
    return s < 0 ? 0 : s;
}

int wf_clock_hm(void)
{
    int tm[16];
    memset(tm, 0, sizeof tm);
    LoadCalendarFromRTC(tm);                     /* struct tm: sec, min, hour, ... */
    if (tm[2] < 0 || tm[2] > 23 || tm[1] < 0 || tm[1] > 59)
        return -1;
    return tm[2] * 100 + tm[1];
}

int wf_clock_ymd(void)
{
    int tm[16];
    memset(tm, 0, sizeof tm);
    LoadCalendarFromRTC(tm);                     /* struct tm: ..., mday [3], mon [4] (0..11), year [5] (since 1900) */
    if (tm[3] < 1 || tm[3] > 31 || tm[4] < 0 || tm[4] > 11 || tm[5] < 100 || tm[5] > 299)
        return -1;
    return (1900 + tm[5]) * 10000 + (tm[4] + 1) * 100 + tm[3];
}

/* print finishes (wf_dark.h, DESIGN 3.9): the settings; a run takes them at its start */
volatile int wf_pr_frame, wf_pr_info = 1, wf_pr_date, wf_pr_jpeg;
volatile int wf_dk_scr;                          /* Screen Off (wf_dark.h, DESIGN 3.11): read live, a run follows a change */
volatile int wf_roll_look8;                      /* a JPEG 8-bit roll (wf_dark.h, DESIGN 3.14): config rolllook=, read live */
void wf_roll_look8_set(int on)
{
    on = on != 0;
    if (wf_roll_look8 == on)
        return;
    wf_roll_look8 = on;
    wf_cfg_dirty = 1;
    wf_log("roll: JPEG 8-bit %s", on ? "on (the look at the shot)" : "off");
}

#if WF_DARK_CAN
/* the develop tables a run builds for a look, B's choice (wf_rawb.c): Leica Vivid (WF_FLUT_DISP) = the sRGB develop + the
 * owner's display cube <CODE>SR.CUB on its codes, no log step; every other Film LUT can (V-Log, Canon Log 2 ...) = log +
 * cube, the curve naming the cube (wf_loglut_name) */
int wf_dark_src(int output, int flut)
{
    return output == WF_OUT_FILM ? WF_DEV_SRGB : output == WF_OUT_LOG ? WF_DEV_LOG
         : output != WF_OUT_LLUT ? 0 : WF_FLUT_DISP(flut) ? WF_DEV_DISP : WF_DEV_CUBE;
}

/* a chain film's log table from WFLOG.DAT for this run (option (c), wf_rawb.h): 1 = M.tabs built (a WF_DEV_CUBE develop,
 * the look stage then adds the effects only: frame devd 2), 0 = the 8-bit chain (the failure is logged and marked) */
static int dk_lgk;
static int dk_logpk(const struct wf_look *lk, struct wf_dev_tabs *t, void *scr)
{
    struct wf_dev_pkinfo pi;
    int ms;
    dk_lgk = wf_rawb_logpk_build(t, scr, wf_dev_scratch_bytes(DK_W, DK_H), lk, &ms, &pi) == WF_LL_OK;
    if (dk_lgk && lk->stk) {
        struct wf_sg_knobs kk;
        wf_sg_unkey(lk->stk, &kk);
        wf_log("dark: tables knobs %s v%d k%x exp %d prn %d %d %d interlayer %d bleach %d %d neutral %d %d c%d %dms "
               "(sum %x)", wf_films[lk->row].code, lk->variant, (unsigned)lk->stk, kk.fexp, kk.prn[0], kk.prn[1],
               kk.prn[2], kk.iie * 25, kk.bleach * 25, kk.pbleach * 25, kk.nneg * 25, kk.nprn * 25, pi.curve, ms,
               (unsigned)pi.sum);
    } else if (dk_lgk)
        wf_log("dark: tables log pack %s v%d c%d %dms (offset %d, %d entries, sum %x)", wf_films[lk->row].code,
               lk->variant, pi.curve, ms, (int)pi.off, pi.count, (unsigned)pi.sum);
    return dk_lgk;
}

static struct wf_cr2_dec dk_dec;
static struct wf_cr2_info dk_inf;
static uint32_t dk_jpe[1024] __attribute__((aligned(8)));   /* struct wf_jpe (wf_jpe_bytes() <= 4096) */
static struct wf_dk_thumb dk_th;
static struct wf_dk_shot dk_cs;                  /* the CR2's own details (its head, read at load): the finishes' gaps */
static int dk_pfr, dk_pin, dk_pdt;               /* this run's print finishes (wf_pr_* at its start, DESIGN 3.9) */
static int dk_pf_said;                           /* WFPRINT.DAT missing / bad: said once a run */
static int dk_wa_said;                           /* Light Auto on a CR2 without Auto WB levels: said once a run */
#define DK_PF_PACK "ML/DATA/WFPRINT.DAT"
/* the maker's wordmark + our model lettering (owner 11-16): ML/DATA/WFLOGO.DAT, made at card time by tools/mk_wflogo.py
   from the owner's own file (never in git); no file = the caption as text alone */
#define DK_LG_PACK "ML/DATA/WFLOGO.DAT"
#define DK_LG_B    (192 * 1024)                  /* its maps + resample tables at 5184 x 3456 (wf_pf_logo_place: under
                                                    150 KB for every frame style, checked when drawn) */
static uint32_t dk_lgn;                          /* WFLOGO.DAT's size at the run's start, 0 = none */
static int dk_lg_said;                           /* WFLOGO.DAT too big / bad: said once a run */
static int dk_pjp;                               /* this run's Camera JPEG (wf_pr_jpeg at its start): WF_PJ_OVER = each
                                                    print also replaces the shot's own JPEG */

static int dk_battery(void)
{
#ifdef WF_BUILD_5D2
    int b = GetBatteryLevel();
    return b >= 0 && b <= 100 ? b : -1;
#else
    int b = battery_level_bars;                  /* EOS M: bars only (ML lens.c: 0 low, 1 half, else full) */
    return b <= 0 ? 5 : b == 1 ? 30 : b == 2 ? 70 : 100;
#endif
}

/* ------------------------------------------------------------------ the screen while the run lives
 * The owner's RAWB18 EOS M (16 Nov): "the screen turns off while the Darkroom runs". The run pauses LiveView (Canon idle,
 * its buffers free for the memory above). ML's PauseLiveView does not turn the display off itself (its own powersave
 * calls display_off after it), and ML's idle countdown does not run while LiveView is paused (liveview_display_idle is 0
 * then), but the EOS M's display went dark: Canon's, it seems, once LiveView stops (no viewfinder to switch to). So the
 * display's state is logged around the pause and at the end, and the display is turned on again (ML's display_on, as its
 * powersave wakes the screen) when it reads off: after the pause, before each frame and after the run. The UI draws the
 * Darkroom into the bitmap (Canon's front buffer off), which stays on through the pause. Off and not back after
 * DK_DISP_TRIES asks in a row: the run goes on dark (developing needs no screen) and says so once. */
#define DK_DISP_WAIT 1000                        /* ms for TurnOnDisplay (ML's GUI task) to take */
#define DK_DISP_TRIES 3
#define DK_OFF_EVERY 2000                        /* ms between two asks to turn the display off (it may take a moment) */
static int dk_disp_ons, dk_disp_miss;            /* asks this run, asks in a row that did not take */
/* Screen Off (owner 11-16): dk_dark_ok while a run may go dark (from after LiveView's pause to the run's end), the last
 * key on the Darkroom screen (UI task), the last ask to turn it off, and this run's counts for the end line */
static volatile int dk_dark_ok, dk_wake_ms, dk_off_ms, dk_offs, dk_wakes;
static int dk_dark_due(int now)
{
    return dk_running && dk_dark_ok && wf_dk_scr == WF_DS_OFF && now - dk_wake_ms >= WF_DS_WAKE_MS;
}

int wf_dark_wake(int now)
{
    if (!dk_running)
        return 0;
    int dark = dk_dark_ok && wf_dk_scr == WF_DS_OFF && (now - dk_wake_ms >= WF_DS_WAKE_MS || !display_is_on());
    dk_wake_ms = now;                            /* any key restarts the time, Screen On too (a switch to Off waits) */
    if (dark) {
        dk_wakes++;
        if (!display_is_on())
            display_on();
    }
    return dark;
}

int wf_dark_dark(int now)
{
    if (!dk_dark_due(now))
        return 0;
    if (display_is_on() && now - dk_off_ms >= DK_OFF_EVERY) {
        dk_off_ms = now;
        dk_offs++;
        display_off();
    }
    return !display_is_on();
}

static int dk_screen(const char *when)
{
    if (dk_dark_due(get_ms_clock()))             /* Screen Off: dark is what the owner chose */
        return display_is_on();
    if (display_is_on()) {
        dk_disp_miss = 0;
        return 1;
    }
    if (dk_disp_miss >= DK_DISP_TRIES || ml_shutdown_requested || wf_unloading)
        return 0;
    int t0 = get_ms_clock();
    display_on();
    while (!display_is_on() && get_ms_clock() - t0 < DK_DISP_WAIT)
        msleep(20);
    int on = display_is_on();
    dk_disp_ons++;
    dk_disp_miss = on ? 0 : dk_disp_miss + 1;
    if (dk_disp_ons <= 6 || !on)
        wf_log("dark: display off %s: turned on again, display %d after %d ms%s", when, on, get_ms_clock() - t0,
               dk_disp_miss >= DK_DISP_TRIES ? " (not asked again this run)" : "");
    return on;
}

/* ------------------------------------------------------------------ memory
 * A frame needs the raw (DK_RAW_A: the CR2's sensor image, ML packed 14-bit, 31.1 MB) and the developed frame
 * (DK_FRAME_N: 5184 x 3456 UYVY, 34.2 MB). The owner's EOS M on RAWB17 (16 Nov) gave ONE SRM chunk of 31,888 KB (ML
 * gets two SRM buffers there and gives the first back to Canon) and no 35 MB block of shoot memory: "memory raw 6000064
 * frame 0 (srm)", "no memory" on every try. So the frame may lie in pieces (wf_frame.h struct wf_fseg):
 *   1. one block, as before: shoot memory (_contig), else SRM chunks after the raw;
 *   2. split at row K: rows 0 .. K - 1 (the head, K x 10 KB) in another SRM chunk, else one block of shoot memory, else
 *      the chunks of a shoot suite (whole rows each); rows K .. 3455 (the tail) over the first bytes of the raw. The
 *      develop writes tail row y at raw + (y - K) * DK_FP once row y is made, and wf_dev.c's 5-row ring reads sensor rows
 *      from DK_Y0 + y - 1 on for the rows after it: K (dk_split_rows) keeps those writes DK_SPLIT_M sensor rows behind
 *      the reads (dk_split_ok, checked again per frame with the CR2's own pitch). The raw is spent after the develop;
 *      the print finishes' scratch lies after the tail. The worker's look stage, the stamp and the encoder read the
 *      rows through the pieces: the print is the same, byte for byte, as from one block (ROLL:mem in the host test).
 * K = the least multiple of DK_BAND rows that does both: 384 rows (3.8 MB of head) with the finishes off, 704 (7.0 MB)
 * with a frame, the caption and the date. A run that gets no memory logs what was short ("dark: memory short: ..."). */
#define DK_FP       (DK_W * 2)                   /* frame pitch, bytes */
#define DK_FRAME_N  ((uint32_t)DK_FP * DK_H)
#define DK_RAW_A    (DK_RAW_N + 4096)             /* the raw's block */
#define DK_RAW_P    9240                         /* the EOS M CR2's raw pitch (5280 px x 14 bit): K is set for it */
#define DK_SPLIT_M  8                            /* sensor rows between the tail's last write and the next read */
struct dk_mem {
    void *srm, *shoot;                           /* suites (shoot: the whole frame, or the split's head) */
    uint8_t *raw, *frame;                        /* cacheable aliases; frame = row 0 */
    uint8_t *dma, *in, *ob, *app1;
    struct wf_dev_tabs *tabs;
    void *scr;
    int nseg, split;                             /* the frame's pieces (0 = one block), the first row over the raw */
    struct wf_fseg seg[WF_FSEG_MAX];
    uint8_t *fin;                                /* the print finishes' scratch (dk_finish): the raw's block after the */
    uint32_t fin_n;                              /* tail, or the whole raw block with a one-block frame */
    const char *head;                            /* where the split's head came from (log) */
};
static struct dk_mem M;

/* row y of the frame (wf_pf_rowfn), one block or pieces */
static uint32_t *dk_row(void *ctx, int y)
{
    (void)ctx;
    if (!M.nseg)
        return (uint32_t *)(M.frame + (uint32_t)y * DK_FP);
    int k = M.nseg - 1;
    while (k > 0 && y < M.seg[k].y)
        k--;
    return (uint32_t *)((uint8_t *)M.seg[k].p + (uint32_t)(y - M.seg[k].y) * DK_FP);
}
/* the end of the run of rows from y that lie in one piece */
static int dk_row_end(int y)
{
    int e = DK_H;
    for (int k = 0; k < M.nseg; k++)
        if (M.seg[k].y > y && M.seg[k].y < e)
            e = M.seg[k].y;
    return e;
}
/* the tail written over the raw stays behind the develop's reads (raw pitch p), for every tail row */
static int dk_split_ok(int K, int p)
{
    if (K <= 0 || K >= DK_H)
        return 0;
    for (int y = K; y < DK_H; y++)
        if ((uint32_t)(y - K + 1) * DK_FP > (uint32_t)(DK_Y0 + y - 1 - DK_SPLIT_M) * (uint32_t)p)
            return 0;
    return 1;
}
/* the split row: the least multiple of DK_BAND whose tail and the finishes' scratch fit the raw's block, writes behind */
static int dk_split_rows(uint32_t fin, int p)
{
    for (int K = DK_BAND; K < DK_H; K += DK_BAND)
        if ((uint32_t)(DK_H - K) * DK_FP + fin <= DK_RAW_A && dk_split_ok(K, p))
            return K;
    return -1;
}

/* the first contiguous run of suite chunks of >= need bytes, skipping [skip, skip + skipn) */
static uint8_t *dk_run(void *suite, uint32_t need, uint8_t *skip, uint32_t skipn)
{
    uint32_t a0 = 0, len = 0;
    for (void *c = GetFirstChunkFromSuite(suite); c; c = GetNextMemoryChunk(suite, c)) {
        uint32_t a = (uint32_t)GetMemoryAddressOfMemoryChunk(c), n = (uint32_t)GetSizeOfMemoryChunk(c);
        uint32_t ac = a & ~0x40000000u;
        if (skip && ac < (uint32_t)skip + skipn && ac + n > (uint32_t)skip) {
            len = 0;
            continue;
        }
        if (len && (a0 & ~0x40000000u) + len == ac)
            len += n;
        else {
            a0 = a;
            len = n;
        }
        if (len >= need)
            return WF_CACHEABLE(a0);
    }
    return 0;
}

static void dk_log_suite(const char *what, void *suite)
{
    char b[100];
    int l = 0, k = 0;
    b[0] = 0;
    for (void *c = GetFirstChunkFromSuite(suite); c && k < 6; c = GetNextMemoryChunk(suite, c), k++)
        l += snprintf(b + l, sizeof b - l, " %x+%dK", (unsigned)GetMemoryAddressOfMemoryChunk(c), GetSizeOfMemoryChunk(c) >> 10);
    wf_log("dark: %s chunks%s", what, b);
}

static void dk_free(void)
{
    if (M.srm) srm_free_suite(M.srm);
    if (M.shoot) shoot_free_suite(M.shoot);
    if (M.dma) __mem_free(M.dma);
    if (M.in) __mem_free(M.in);
    if (M.ob) __mem_free(M.ob);
    if (M.app1) __mem_free(M.app1);
    if (M.tabs) __mem_free(M.tabs);
    if (M.scr) __mem_free(M.scr);
    memset(&M, 0, sizeof M);
}

/* the print finishes' scratch this run needs (dk_finish): the date stamp's glow map, or the font pack + the caption band +
 * a 16-row buffer + the logo's maps (the larger: the stamp is drawn first) */
static uint32_t dk_fin_need(void)
{
    uint32_t n = 0;
    if (dk_pdt)
        n = 1u << 20;                            /* the stamp's map: about 0.2 MB at 5184 x 3456 (checked when drawn) */
    for (int o = 1; dk_pfr && o <= 6; o += 5) {  /* orientation 1 (= 3) and 6 (= 8): the two canvas shapes */
        struct wf_pf_lay L;
        if (wf_pf_layout(&L, dk_pfr, dk_pin, DK_W, DK_H, o)) {
            uint32_t k = WF_PF_MAXB + ((wf_pf_band_bytes(&L) + 3) & ~3u) + (uint32_t)L.cw * 2 * 16 + (dk_lgn ? DK_LG_B : 0);
            if (k > n)
                n = k;
        }
    }
    return (n + 31) & ~31u;
}

/* a split frame (memory note above): the head of K rows from another SRM chunk, one shoot block or a shoot suite's chunks,
 * the tail over the raw. 0 = M.seg set */
static int dk_split(uint32_t fin)
{
    int K = dk_split_rows(fin, DK_RAW_P), n = 0;
    if (K <= 0)
        return -1;
    uint32_t hn = (uint32_t)K * DK_FP;
    uint8_t *h = dk_run(M.srm, hn, M.raw, DK_RAW_A);
    M.head = "srm";
    if (!h && (M.shoot = shoot_malloc_suite_contig(hn)) != 0) {
        h = dk_run(M.shoot, hn, 0, 0);
        M.head = "shoot";
        if (!h) {
            shoot_free_suite(M.shoot);
            M.shoot = 0;
        }
    }
    if (h) {
        M.seg[n].y = 0;
        M.seg[n++].p = (uint32_t *)h;
    } else {                                     /* shoot memory in chunks: whole rows of each, up to WF_FSEG_MAX - 1 */
        M.shoot = shoot_malloc_suite(hn + (WF_FSEG_MAX - 1) * (DK_FP + 32));
        M.head = "shoot chunks";
        int y = 0;
        for (void *c = M.shoot ? GetFirstChunkFromSuite(M.shoot) : 0; c && y < K && n < WF_FSEG_MAX - 1;
             c = GetNextMemoryChunk(M.shoot, c)) {
            uint32_t a = (uint32_t)WF_CACHEABLE(GetMemoryAddressOfMemoryChunk(c)), a32 = (a + 31) & ~31u;
            uint32_t sz = (uint32_t)GetSizeOfMemoryChunk(c);
            int rows = sz >= a32 - a + DK_FP ? (int)((sz - (a32 - a)) / DK_FP) : 0;
            if (rows <= 0)
                continue;
            M.seg[n].y = y;
            M.seg[n++].p = (uint32_t *)a32;
            y += rows;
        }
        if (y < K) {
            if (M.shoot)
                shoot_free_suite(M.shoot);
            M.shoot = 0;
            return -1;
        }
    }
    M.seg[n].y = K;                              /* the tail over the raw */
    M.seg[n++].p = (uint32_t *)M.raw;
    M.nseg = n;
    M.split = K;
    M.frame = (uint8_t *)M.seg[0].p;
    M.fin = M.raw + (uint32_t)(DK_H - K) * DK_FP;
    M.fin_n = DK_RAW_A - (uint32_t)(DK_H - K) * DK_FP;
    return 0;
}

/* no memory: what was short, and the biggest shoot block there is now (taken and given back at once) */
static void dk_short(uint32_t fin)
{
    int big = 0, K = dk_split_rows(fin, DK_RAW_P);
    void *t = shoot_malloc_suite_contig(0);
    if (t) {
        void *c = GetFirstChunkFromSuite(t);
        big = c ? GetSizeOfMemoryChunk(c) >> 10 : 0;
        shoot_free_suite(t);
    }
    /* two lines: a log line holds 120 characters (wf_main.c WF_LOG_W) */
    wf_log("dark: memory short: raw %d KB %s, frame %d KB in one block none", (int)(DK_RAW_A >> 10),
           M.raw ? "ok" : M.srm ? "none in the srm chunks" : "no srm", (int)(DK_FRAME_N >> 10));
    wf_log("dark: memory short: split head %d KB (rows 0-%d) none, finishes %d KB, biggest shoot block %d KB",
           K > 0 ? (int)(((uint32_t)K * DK_FP) >> 10) : 0, K - 1, (int)(fin >> 10), big);
}

static int dk_alloc(void)
{
    memset(&M, 0, sizeof M);
    M.dma = __mem_malloc(DK_RD, WF_MEM_DMA, "wf_dark.c", __LINE__);
    M.in = __mem_malloc(DK_IN, 0, "wf_dark.c", __LINE__);
    M.ob = __mem_malloc(DK_OB, 0, "wf_dark.c", __LINE__);
    M.app1 = __mem_malloc(DK_APP1, 0, "wf_dark.c", __LINE__);
    M.tabs = __mem_malloc(sizeof(struct wf_dev_tabs), 0, "wf_dark.c", __LINE__);
    M.scr = __mem_malloc(wf_dev_scratch_bytes(DK_W, DK_H), 0, "wf_dark.c", __LINE__);
    if (!M.dma || !M.in || !M.ob || !M.app1 || !M.tabs || !M.scr) {
        wf_log("dark: no memory for the buffers / tables");
        return -1;
    }
    uint32_t fin = dk_fin_need();
    for (int pass = 0; pass < 2 && !M.raw; pass++) {
        if (!pass && (M.shoot = shoot_malloc_suite_contig(DK_FRAME_N)) != 0) {   /* 1. the frame in one shoot block */
            M.frame = dk_run(M.shoot, DK_FRAME_N, 0, 0);
            if (!M.frame) {
                shoot_free_suite(M.shoot);
                M.shoot = 0;
            }
        }
        M.srm = srm_malloc_suite(0);
        if (M.srm) {
            dk_log_suite("srm", M.srm);
            M.raw = dk_run(M.srm, DK_RAW_A, 0, 0);
        }
        if (!M.raw && M.shoot) {                 /* that block may hold what SRM needs: give it back, SRM once more */
            wf_log("dark: no raw block in srm beside a frame block in shoot: frame given back");
            if (M.srm)
                srm_free_suite(M.srm);
            shoot_free_suite(M.shoot);
            M.srm = M.shoot = 0;
            M.frame = 0;
        } else
            break;
    }
    if (M.raw && !M.frame)
        M.frame = dk_run(M.srm, DK_FRAME_N, M.raw, DK_RAW_A);   /* SRM chunks in a row after the raw */
    if (M.raw && M.frame) {
        M.fin = M.raw;
        M.fin_n = DK_RAW_A;
        wf_log("dark: memory raw %x frame %x (%s)", (unsigned)M.raw, (unsigned)M.frame, M.shoot ? "shoot" : "srm");
        return 0;
    }
    if (M.raw && dk_split(fin) == 0) {
        wf_log("dark: memory raw %x frame split at row %d: head %d KB from %s (%d piece%s at %x)", (unsigned)M.raw,
               M.split, (int)(((uint32_t)M.split * DK_FP) >> 10), M.head, M.nseg - 1, M.nseg > 2 ? "s" : "",
               (unsigned)M.frame);
        wf_log("dark: memory rows %d-%d over the raw, %d KB after them for the finishes (%d KB needed)", M.split, DK_H - 1,
               (int)(M.fin_n >> 10), (int)(fin >> 10));
        return 0;
    }
    wf_log("dark: memory raw %x frame %x (%s)", (unsigned)M.raw, (unsigned)M.frame, M.shoot ? "shoot" : "srm");
    dk_short(fin);
    return -1;
}

/* ------------------------------------------------------------------ card I/O through the DMA buffer */
struct dk_rd { void *f; };
static int dk_read_cb(void *ctx, void *buf, uint32_t n)
{
    struct dk_rd *r = ctx;
    if (n > DK_RD) n = DK_RD;
    int k = FIO_ReadFile(r->f, M.dma, n);
    if (k < 0)
        return -1;
    memcpy(buf, M.dma, (size_t)k);
    return k;
}
static int dk_read_at(void *f, uint32_t pos, uint8_t *dst, uint32_t n)
{
    if (FIO_SeekSkipFile(f, pos, 0) != (int64_t)pos)
        return -1;
    while (n) {
        uint32_t k = n > DK_RD ? DK_RD : n;
        if (FIO_ReadFile(f, M.dma, k) != (int)k)
            return -1;
        memcpy(dst, M.dma, k);
        dst += k;
        n -= k;
    }
    return 0;
}
/* a print written: its bytes, and their FNV-1a outside [s0, s1) (Canon's APP1, written again with the Exif thumbnail
   after the picture): dk_verify reads the file back against it before the camera's JPEG is replaced */
#define DK_FNV0 2166136261u
struct dk_wr { void *f; uint32_t n; int err; uint32_t h, s0, s1; };
static uint32_t dk_fnv(uint32_t h, const uint8_t *p, uint32_t n)
{
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static void dk_hash(struct dk_wr *w, uint32_t at, const uint8_t *p, uint32_t n)
{
    uint32_t e = at + n;
    if (at < w->s0)
        w->h = dk_fnv(w->h, p, (e < w->s0 ? e : w->s0) - at);
    if (e > w->s1) {
        uint32_t b = at > w->s1 ? at : w->s1;
        w->h = dk_fnv(w->h, p + (b - at), e - b);
    }
}
static int dk_write_cb(void *ctx, const uint8_t *p, uint32_t n)
{
    struct dk_wr *w = ctx;
    while (n && !w->err) {
        uint32_t k = n > DK_RD ? DK_RD : n;
        dk_hash(w, w->n, p, k);
        memcpy(M.dma, p, k);
        if (FIO_WriteFile(w->f, M.dma, k) != (int)k)
            w->err = 1;
        p += k;
        n -= k;
        w->n += k;
    }
    return w->err ? -1 : 0;
}
/* the file at path read back: w's size, w's hash outside the APP1 and the APP1's bytes = app1 (al bytes at offset 2).
   0 = the same; -1 size, -2 open, -3 read, -4 the APP1, -5 the rest. M.in (the decoder's input: free after the decode)
   holds each chunk while it is checked */
static int dk_verify(const char *path, const struct dk_wr *w, const uint8_t *app1, uint32_t al)
{
    uint32_t sz = 0;
    if (FIO_GetFileSize(path, &sz) != 0 || sz != w->n)
        return -1;
    void *f = FIO_OpenFile(path, 0);
    if (!WF_FH_OK(f))
        return -2;
    struct dk_wr v = { 0, 0, 0, DK_FNV0, w->s0, w->s1 };
    int bad = 0;
    while (!bad && v.n < sz) {
        uint32_t k = sz - v.n > DK_RD ? DK_RD : sz - v.n;
        if (FIO_ReadFile(f, M.dma, k) != (int)k) {
            bad = -3;
            break;
        }
        memcpy(M.in, M.dma, k);
        dk_hash(&v, v.n, M.in, k);
        for (uint32_t a = v.n > w->s0 ? v.n : w->s0, e = v.n + k < w->s1 ? v.n + k : w->s1; al && a < e; a++)
            if (M.in[a - v.n] != app1[a - w->s0]) {
                bad = -4;
                break;
            }
        v.n += k;
    }
    FIO_CloseFile(f);
    return bad ? bad : v.h != w->h ? -5 : 0;
}
struct dk_mw { uint8_t *p; uint32_t n, cap; };   /* a small JPEG (the Exif thumbnail) into memory */
static int dk_mem_cb(void *ctx, const uint8_t *p, uint32_t n)
{
    struct dk_mw *m = ctx;
    if (n > m->cap - m->n)
        return -1;
    memcpy(m->p + m->n, p, n);
    m->n += n;
    return 0;
}

/* ------------------------------------------------------------------ one frame */
enum { DK_OK = 0, DK_STOP, DK_ERR, DK_FATAL };
static const char *dk_why;
static int dk_stopk;                             /* why a run stopped: 1 MENU, 2 unload / power off */
static int dk_t[4];

static int dk_should_stop(void)
{
    if (dk_stop) { dk_why = "stopped"; dk_stopk = 1; return 1; }
    if (wf_unloading) { dk_why = "unload"; dk_stopk = 2; return 1; }
    if (ml_shutdown_requested) { dk_why = "power off"; dk_stopk = 2; return 1; }
    return 0;
}

static void dk_phase(int ph, int prog)
{
    dv.phase = ph;
    dv.prog = prog;
    dv_bump();
}

/* a band done: a moment for the tasks below wf_task, then the light part of wf_task's loop (the run lives inside it) */
static void dk_yield(void)
{
    msleep(2);
    wf_task_yield(0);
}

/* ------------------------------------------------------------------ print finishes (DESIGN 3.9, wf_print.h) */
static const char *const dk_pfr_name[WF_PFR_N] = { "off", "white", "black", "white wide", "black wide" };

/* before the encode, in M.fin (the raw's block: the develop is done with the raw): the date stamp into the frame; with a frame its layout,
 * the caption band (WFPRINT.DAT read into the scratch, the shot's details from Canon's APP1 and the CR2's for the gaps,
 * the look from the print's comment text) and a row buffer, and the APP1's pixel size. L->frame 0: the print as before. */
static void dk_finish(int i, uint32_t ao, uint32_t al, const char *look, struct wf_pf_lay *L, uint8_t **band,
                      uint32_t **rowbuf)
{
    int t0 = get_ms_clock();
    struct wf_dk_shot s;
    memset(&s, 0, sizeof s);
    if (al > 10)
        wf_dk_shot(M.app1 + ao + 10, al - 10, &s);
    int orient = s.orient ? s.orient : 1;        /* the print turns by its own APP1 (none: shown as stored) */
    wf_dk_shot_gaps(&s, &dk_cs);
    uint8_t *scr = M.fin;
    char b[24];
    *band = 0;
    *rowbuf = 0;
    if (dk_pdt) {
        uint32_t need = 0;
        if (!wf_pf_date(&s, dk_pdt, b, sizeof b))
            wf_log("dark: F%02d no date in the shot (APP1, CR2): no date stamp", i + 1);
        else if ((need = wf_pf_stamp_bytes(DK_W, DK_H, b)) == 0 || need > M.fin_n
                 || wf_pf_stamp_fn(dk_row, 0, DK_W, DK_H, orient, b, scr, need) != 0)
            wf_log("dark: F%02d date stamp %s not drawn (map %d of %d KB)", i + 1, b, (int)(need >> 10),
                   (int)(M.fin_n >> 10));
        else
            wf_log("dark: F%02d date stamp %s o%d %dms", i + 1, b, orient, get_ms_clock() - t0);
    }
    if (!wf_pf_layout(L, dk_pfr, dk_pin, DK_W, DK_H, orient))
        return;
    int t1 = get_ms_clock();
    uint32_t nb = (wf_pf_band_bytes(L) + 3) & ~3u, nr = (uint32_t)L->cw * 2 * 16;
    if (WF_PF_MAXB + nb + nr > M.fin_n) {        /* dk_fin_need sized it (about 4.5 MB) */
        wf_log("dark: F%02d frame needs %d KB of scratch: none", i + 1, (int)((WF_PF_MAXB + nb + nr) >> 10));
        wf_pf_layout(L, WF_PFR_OFF, 0, DK_W, DK_H, orient);
        return;
    }
    *band = scr + WF_PF_MAXB;
    *rowbuf = (uint32_t *)(scr + WF_PF_MAXB + nb);
    static struct wf_pf_font F[WF_PF_NFONT];      /* (wf_task: 8 KB of stack) */
    int fok = 0;
    uint32_t pn = 0;
    if (L->tw) {                                 /* the details: the print font pack (167 KB) into the scratch */
        if (FIO_GetFileSize(DK_PF_PACK, &pn) == 0 && pn > 0 && pn <= WF_PF_MAXB) {
            void *g = FIO_OpenFile(DK_PF_PACK, 0);
            if (WF_FH_OK(g)) {
                fok = dk_read_at(g, 0, scr, pn) == 0 && wf_pf_fonts(F, scr, pn) == 0;
                FIO_CloseFile(g);
            }
        }
        if (!fok && !dk_pf_said) {
            dk_pf_said = 1;
            wf_log("dark: %s missing or bad (%d B): frames without the details", DK_PF_PACK, (int)pn);
        }
    }
    /* the logo pack after the fonts in the same 512 KB; its maps after the band and the row buffer */
    static struct wf_pf_logo lg;
    memset(&lg, 0, sizeof lg);
    uint32_t lo = fok ? (pn + 31) & ~31u : 0;
    if (dk_lgn && (dk_lgn > WF_PF_LOGO_MAXB || lo + dk_lgn > WF_PF_MAXB)) {
        if (!dk_lg_said)
            wf_log("dark: %s %d KB: no room beside the fonts (%d KB): no logo", DK_LG_PACK, (int)(dk_lgn >> 10),
                   (int)(lo >> 10));
        dk_lg_said = 1;
    } else if (dk_lgn) {
        void *g = FIO_OpenFile(DK_LG_PACK, 0);
        int ok = WF_FH_OK(g) && dk_read_at(g, 0, scr + lo, dk_lgn) == 0 && wf_pf_logo(&lg, scr + lo, dk_lgn, s.model) == 0;
        if (WF_FH_OK(g))
            FIO_CloseFile(g);
        if (!ok) {
            memset(&lg, 0, sizeof lg);
            if (!dk_lg_said)
                wf_log("dark: %s bad (%d B): no logo", DK_LG_PACK, (int)dk_lgn);
            dk_lg_said = 1;
        }
    }
    static struct wf_pf_txt tx;                  /* (wf_task's stack is small) */
    static struct wf_pf_lpos lp;
    struct wf_pf_cap c;
    char rl[40], dr[200];
    rl[0] = 0;
    if (dk_pin == WF_PIN_ROLL)
        snprintf(rl, sizeof rl, "ROLL %03d " WF_PF_DOT " %02d/%02d", roll.num, i + 1, roll.exp);
    wf_pf_caption(&s, look, rl, &tx, &c);
    uint32_t mo = WF_PF_MAXB + nb + nr, ln = wf_pf_logo_place(L, fok ? F : 0, &lg, &c, &lp);
    if (lp.n && ln > M.fin_n - mo) {             /* no room for the maps: the caption as before */
        wf_log("dark: F%02d logo needs %d KB of scratch, %d left: not drawn", i + 1, (int)(ln >> 10),
               (int)((M.fin_n - mo) >> 10));
        lp.n = 0;
        c.rsv = c.roll3 = 0;
    }
    if (lp.n)
        wf_pf_cap_logo(&tx, &c, &lg);            /* the model's words the logo says, out of line 1 */
    wf_pf_band(L, fok ? F : 0, &c, *band, dr, sizeof dr);
    int dims = al ? wf_dk_dims(M.app1 + ao, al, L->cw, L->ch) : -1;
    wf_log("dark: F%02d frame %s o%d %dx%d (Exif size %s), band %d KB, %dms", i + 1, dk_pfr_name[L->frame], orient, L->cw,
           L->ch, dims == 0 ? "set" : "not set", (int)(nb >> 10), get_ms_clock() - t1);
    if (fok)
        wf_log("dark: F%02d caption %s", i + 1, dr);
    if (lp.n && wf_pf_logo_maps(L, &lp, scr + mo, M.fin_n - mo) == 0)
        wf_log("dark: F%02d logo %s %dx%d%s%s, %d KB", i + 1, lg.maker, lp.w[0], lp.h[0], lp.n > 1 ? " + lettering " : "",
               lp.n > 1 ? s.model : "", (int)(ln >> 10));
    else if (lp.n)
        wf_log("dark: F%02d logo maps failed: not drawn", i + 1);
}

/* ------------------------------------------------------------------ the encode, the Exif thumbnail, the camera's JPEG */
#define DK_XT_JPG  (40u * 1024)                  /* in M.in after the thumbnail's picture (WF_DK_XBYTES = 37.5 KB) */
#define DK_XT_JN   (48u * 1024)
#define DK_XT_OB   (DK_XT_JPG + DK_XT_JN)
#define DK_XT_OBN  (8u * 1024)

/* the Exif thumbnail (owner 11-16: Canon's playback and index show the print): xt's picture as a JPEG into the APP1's
 * slot (quality down from 90 until it fits) and the APP1 written again at offset 2 of path. 0 = done or left as it
 * was (Canon's thumbnail, logged); -1 = the rewrite failed (the print is broken) */
static int dk_xthumb(int i, const char *path, uint8_t *app1, uint32_t al, const struct wf_dk_xth *xt, int what)
{
    static const char *const wn[2] = { "print", "camera JPEG" };
    if (!wf_dk_xth_done(xt)) {
        wf_log("dark: F%02d %s Exif thumbnail: rows missing, Canon's kept", i + 1, wn[what]);
        return 0;
    }
    struct wf_jpe *je = (struct wf_jpe *)dk_jpe;
    struct dk_mw mw = { M.in + DK_XT_JPG, 0, DK_XT_JN };
    int q = 90, ok = 0;
    for (; q >= 40 && !ok; q -= 10) {
        mw.n = 0;
        ok = wf_jpe_begin(je, WF_DK_XW, WF_DK_XH, q, 0, 0, M.in + DK_XT_OB, DK_XT_OBN, dk_mem_cb, &mw) == WF_JPE_OK
            && wf_jpe_rows(je, (const uint32_t *)M.in, WF_DK_XW * 2, WF_DK_XH) == WF_JPE_OK && wf_jpe_end(je) == WF_JPE_OK
            && wf_dk_xthumb_put(app1, al, mw.p, mw.n) == 0;
    }
    if (!ok) {
        wf_log("dark: F%02d %s Exif thumbnail does not fit its slot: Canon's kept", i + 1, wn[what]);
        return 0;
    }
    struct wf_fe e = { 0, 0 };
    void *o = wf_fopen_w(path, WF_FO_AT, 2, &e);
    struct dk_wr w = { o, 0, 0, DK_FNV0, 0, 0 };
    int rc = o ? dk_write_cb(&w, app1, al) : -1;
    if (o)
        FIO_CloseFile(o);
    if (rc) {
        wf_log("dark: F%02d %s Exif thumbnail not written (%s %d)", i + 1, wn[what], o ? "write" : wf_fe_name(e.step), e.rc);
        return -1;
    }
    wf_log("dark: F%02d %s Exif thumbnail %dx%d q%d %d B", i + 1, wn[what], WF_DK_XW, WF_DK_XH, q + 10, (int)mw.n);
    return 0;
}

/* the canvas L into path (a new file): Canon's APP1 (al bytes, none = JFIF), the frame's rows through L's band / logo,
 * the darkroom screen's thumbnails (what 0: the print) and the Exif thumbnail. w: the bytes and their hash (dk_verify).
 * DK_OK / DK_STOP / DK_FATAL (dk_why); a file not finished is removed */
static int dk_encode(int i, const char *path, const struct wf_pf_lay *L, uint8_t *app1, uint32_t al, const uint8_t *band,
                     uint32_t *rowbuf, int what, struct dk_wr *w)
{
    struct wf_fe e = { 0, 0 };
    uint32_t osz = 0;
    if (FIO_GetFileSize(path, &osz) == 0 && wf_fremove(path, &e) != 0) {
        dk_why = what ? "remove old temp file" : "remove old print";
        return DK_FATAL;
    }
    void *o = wf_fopen_w(path, WF_FO_NEW, 0, &e);
    if (!o) {
        dk_why = what ? "create temp file" : "create print";
        return DK_FATAL;
    }
    memset(w, 0, sizeof *w);
    w->f = o;
    w->h = DK_FNV0;
    w->s0 = 2;                                   /* SOI, then the APP1 (wf_jpe_begin) */
    w->s1 = 2 + al;
    static struct wf_dk_xth xt;                  /* (wf_task's stack) */
    uint32_t xo = 0, xroom = 0;
    int xth = al && wf_dk_xthumb_slot(app1, al, &xo, &xroom) == 0;
    if (xth)
        wf_dk_xth_begin(&xt, L->cw, L->ch, (uint32_t *)M.in);
    struct wf_jpe *je = (struct wf_jpe *)dk_jpe;
    int rc = wf_jpe_bytes() <= sizeof dk_jpe ? wf_jpe_begin(je, L->cw, L->ch, 95, al ? app1 : 0, (int)al, M.ob, DK_OB,
                                                        dk_write_cb, w) : WF_JPE_E_ARG;
    if (!what)
        wf_dk_thumb_begin(&dk_th, DK_W, DK_H);
    int stopped = 0;
    for (int y = 0; rc == WF_JPE_OK && y < L->ch; y += 16) {   /* no frame: the frame's own rows, as before */
        const uint32_t *rows;
        int pitch, n = wf_pf_band_rows_fn(L, y, dk_row, 0, DK_FP, band, rowbuf, &rows, &pitch), py = y - L->iy;
        if (n <= 0) {
            rc = WF_JPE_E_ARG;
            break;
        }
        rc = wf_jpe_rows(je, rows, pitch, n);
        if (xth)
            wf_dk_xth_rows(&xt, rows, pitch, y, n, (uint32_t *)M.in);
        for (int ya = py, ye; !what && ya >= 0 && ya < DK_H && ya < py + n; ya = ye) {   /* the screen: the picture's rows */
            ye = dk_row_end(ya) < py + n ? dk_row_end(ya) : py + n;
            wf_dk_thumb_rows(&dk_th, dk_row(0, ya), DK_FP, ya, ye - ya, dv_print);
        }
        if (!(y & 255)) {
            dv.prog = what ? 900 + y * 100 / L->ch : 800 + y * (dk_pjp ? 100 : 200) / L->ch;
            dv_bump();
            if (dk_should_stop()) {
                stopped = 1;
                break;
            }
            wf_task_yield(0);
        }
    }
    if (rc == WF_JPE_OK && !stopped)
        rc = wf_jpe_end(je);
    FIO_CloseFile(o);
    if (rc == WF_JPE_OK && !stopped && !w->err && xth && dk_xthumb(i, path, app1, al, &xt, what) != 0)
        w->err = 1;
    if (stopped || rc != WF_JPE_OK || w->err) {
        wf_fremove(path, &e);                    /* no partial file stays on the card */
        if (stopped)
            return DK_STOP;
        dk_why = w->err ? "write (card full?)" : "encode";
        return DK_FATAL;
    }
    return DK_OK;
}

/* Camera JPEG Overwrite (owner 11-16: shoot fast, the darkroom's print goes over the camera's JPEG later, the ROLLS copy
 * stays): only once the roll print (out, pw) reads back right. A temp file IMG_nnnn.WFT next to the JPEG: frame Off =
 * the print's bytes, a frame = the picture alone at Canon's 5184 x 3456 (a second encode: Canon's playback keeps the
 * size it made), read back, then renamed over the JPEG. Never the CR2. Each step logged; whatever fails, the camera's
 * JPEG stays (or, renamed away, the .WFT holds the print). Returns DK_STOP when stopped, else DK_OK: the print is done. */
static int dk_camjpg(int i, const char *jpg, const char *out, const struct dk_wr *pw, uint8_t *app1, uint32_t al,
                     const struct wf_pf_lay *L, uint32_t *rowbuf)
{
    int t0 = get_ms_clock(), n = 0;
    char tmp[96];
    struct wf_fe e = { 0, 0 };
    uint32_t sz = 0;
    while (jpg[n])
        n++;
    if (n < 5 || n >= (int)sizeof tmp || jpg[n - 4] != '.') {
        wf_log("dark: F%02d camera JPEG: path %s", i + 1, jpg);
        return DK_OK;
    }
    memcpy(tmp, jpg, (size_t)n - 3);
    memcpy(tmp + n - 3, "WFT", 4);
    int hasj = FIO_GetFileSize(jpg, &sz) == 0, hast = FIO_GetFileSize(tmp, &sz) == 0;
    if (!hasj) {                                 /* RAW only, or a run before renamed it away and stopped */
        wf_log("dark: F%02d camera JPEG IMG_%04d.JPG not there: not overwritten%s", i + 1, roll.file[i],
               hast ? " (its .WFT holds a print)" : "");
        return DK_OK;
    }
    if (!al) {
        wf_log("dark: F%02d camera JPEG has no Exif: not overwritten", i + 1);
        return DK_OK;
    }
    int v = dk_verify(out, pw, app1, al);
    if (v) {
        wf_log("dark: F%02d roll print did not read back (%d): camera JPEG kept", i + 1, v);
        return DK_OK;
    }
    if (hast && wf_fremove(tmp, &e) != 0) {
        wf_log("dark: F%02d old %s not removed (%s %d): camera JPEG kept", i + 1, tmp, wf_fe_name(e.step), e.rc);
        return DK_OK;
    }
    struct dk_wr w;
    int rc = DK_OK, copy = !L->frame;
    if (copy) {                                  /* the canvas is the picture: the print's bytes */
        void *f = FIO_OpenFile(out, 0), *o = WF_FH_OK(f) ? wf_fopen_w(tmp, WF_FO_NEW, 0, &e) : 0;
        memset(&w, 0, sizeof w);
        w.f = o;
        w.h = DK_FNV0;
        w.s0 = pw->s0;
        w.s1 = pw->s1;
        while (o && !w.err && w.n < pw->n) {
            uint32_t k = pw->n - w.n > DK_RD ? DK_RD : pw->n - w.n;
            if (FIO_ReadFile(f, M.dma, k) != (int)k)
                w.err = 2;
            else {
                memcpy(M.in, M.dma, k);          /* (dk_write_cb writes through M.dma) */
                dk_write_cb(&w, M.in, k);
            }
        }
        if (o)
            FIO_CloseFile(o);
        if (WF_FH_OK(f))
            FIO_CloseFile(f);
        if (!o || w.err) {
            wf_log("dark: F%02d camera JPEG: temp copy failed (%s): kept", i + 1, !o ? wf_fe_name(e.step) : w.err == 2
                   ? "read" : "write");
            wf_fremove(tmp, &e);
            return DK_OK;
        }
        v = dk_verify(tmp, pw, app1, al);
    } else {                                     /* the picture alone, the Exif size back to Canon's */
        struct wf_pf_lay P;
        wf_pf_layout(&P, WF_PFR_OFF, 0, DK_W, DK_H, 1);
        int dims = wf_dk_dims(app1, al, DK_W, DK_H);
        rc = dk_encode(i, tmp, &P, app1, al, 0, rowbuf, 1, &w);
        if (rc == DK_STOP) {
            wf_log("dark: F%02d camera JPEG kept (stopped)", i + 1);
            return DK_STOP;
        }
        if (rc != DK_OK) {
            wf_log("dark: F%02d camera JPEG: picture encode failed (%s): kept", i + 1, dk_why);
            dk_why = "";
            return DK_OK;
        }
        if (dims)
            wf_log("dark: F%02d camera JPEG: Exif size not set back", i + 1);
        v = dk_verify(tmp, &w, app1, al);
    }
    if (v) {
        wf_log("dark: F%02d camera JPEG: temp file did not read back (%d): kept", i + 1, v);
        wf_fremove(tmp, &e);
        return DK_OK;
    }
    if (wf_frename(tmp, jpg, &e) != 0) {
        int still = FIO_GetFileSize(jpg, &sz) == 0;
        wf_log("dark: F%02d camera JPEG: rename failed (%s %d): %s", i + 1, wf_fe_name(e.step), e.rc,
               still ? "JPEG kept" : "print left in the .WFT file");
        if (still)
            wf_fremove(tmp, &e);
        return DK_OK;
    }
    wf_log("dark: F%02d camera JPEG IMG_%04d.JPG overwritten: %s, %d KB, %dms", i + 1, roll.file[i],
           copy ? "the print" : "the picture 5184x3456", (int)(w.n >> 10), get_ms_clock() - t0);
    return DK_OK;
}

static int dk_frame(int i, const struct wf_look *lk, int src, int *bytes)
{
    char cr2[96], jpg[96], out[96], root[16], txt[200], t2[200];
    int t = get_ms_clock(), rc;
    dk_why = "";
    if (wf_roll_cr2(&roll, i, wf_dcim_dir(), wf_file_prefix(), "CR2", cr2, sizeof cr2) < 0
        || wf_roll_cr2(&roll, i, wf_dcim_dir(), wf_file_prefix(), "JPG", jpg, sizeof jpg) < 0
        || wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_out(&roll, i, root, out, sizeof out) < 0) {
        dk_why = "path";
        return DK_ERR;
    }
    /* 1. load: head, the lossless JPEG header, the decode */
    dk_phase(WF_DP_LOAD, 0);
    dv.print_ready = 0;
    memset(dv_print, 0, sizeof dv_print);
    uint32_t fsize = 0;
    if (FIO_GetFileSize(cr2, &fsize) != 0 || fsize < 4096) { dk_why = "no CR2"; return DK_ERR; }
    void *f = FIO_OpenFile(cr2, 0);
    if (!WF_FH_OK(f)) { dk_why = "open CR2"; return DK_ERR; }
    uint32_t nh = fsize < WF_CR2_HEAD ? fsize : WF_CR2_HEAD;
    rc = dk_read_at(f, 0, M.in, nh) ? WF_CR2_E_READ : wf_cr2_parse(M.in, nh, fsize, &dk_inf);
    memset(&dk_cs, 0, sizeof dk_cs);
    if (rc == 0 && (dk_pfr || dk_pdt))
        wf_dk_shot(M.in, nh, &dk_cs);            /* the print finishes: the CR2's own details while its head is here */
    if (rc == 0) {
        uint32_t nl = fsize - dk_inf.strip_off < WF_CR2_LJHEAD ? fsize - dk_inf.strip_off : WF_CR2_LJHEAD;
        rc = dk_read_at(f, dk_inf.strip_off, M.in, nl) ? WF_CR2_E_READ : wf_cr2_parse_ljpeg(M.in, nl, &dk_inf);
    }
    if (rc == 0 && (dk_inf.w < DK_X0 + DK_W || dk_inf.h < DK_Y0 + DK_H || DK_IN < wf_cr2_inbuf_min(&dk_inf)
                    || (uint32_t)dk_inf.pitch * (uint32_t)dk_inf.h > DK_RAW_N))
        rc = WF_CR2_E_SIZE;
    if (rc == 0 && M.nseg && !dk_split_ok(M.split, dk_inf.pitch)) {   /* the tail would catch up with the raw's reads */
        wf_log("dark: F%02d raw pitch %d: the frame split at row %d is not safe", i + 1, dk_inf.pitch, M.split);
        rc = WF_CR2_E_SIZE;
    }
    struct dk_rd rdc = { f };
    if (rc == 0 && FIO_SeekSkipFile(f, dk_inf.strip_off, 0) != (int64_t)dk_inf.strip_off)
        rc = WF_CR2_E_READ;
    if (rc == 0)
        rc = wf_cr2_dec_begin(&dk_dec, &dk_inf, M.raw, dk_inf.pitch, M.in, DK_IN, dk_read_cb, &rdc);
    while (rc >= 0 && (rc = wf_cr2_dec_step(&dk_dec, DK_JROWS)) > 0) {
        dv.prog = dk_dec.jrow * 250 / (dk_dec.jh ? dk_dec.jh : 1);
        dv_bump();
        if (dk_should_stop()) { FIO_CloseFile(f); return DK_STOP; }
        dk_yield();
    }
    FIO_CloseFile(f);
    if (rc < 0) { dk_why = wf_cr2_err(rc); return DK_ERR; }
    uint32_t al = 0, ao = 0, jn = 0;
    if (FIO_GetFileSize(jpg, &jn) == 0 && jn > 1024) {   /* Canon's APP1 from the shot's own JPEG */
        void *g = FIO_OpenFile(jpg, 0);
        uint32_t n = jn < DK_APP1 ? jn : DK_APP1;
        if (WF_FH_OK(g)) {
            if (dk_read_at(g, 0, M.app1, n) != 0 || wf_dk_app1(M.app1, n, &ao, &al) != 0)
                al = 0;
            FIO_CloseFile(g);
        }
    }
    dk_t[0] = get_ms_clock() - t;
    t += dk_t[0];

    /* 2. expose: levels, scales, the develop in bands with the thumbnail */
    dk_phase(WF_DP_EXPOSE, 250);
    struct wf_dev_job j;
    memset(&j, 0, sizeof j);
    j.raw = M.raw; j.pitch = dk_inf.pitch;
    j.ax0 = 72; j.ay0 = 52; j.ax1 = dk_inf.w; j.ay1 = dk_inf.h;   /* EOS M CR2: image data from 72, 52 */
    j.x0 = DK_X0; j.y0 = DK_Y0; j.w = DK_W; j.h = DK_H; j.cfa = 0;
    int lb = 0, lw = 0;
    wf_dev_levels(M.scr, &j, 16, 64, &lb, &lw);
    j.black = lb ? lb : 2048;
    j.white = lw ? lw : wf_dk_white(WF_DK_EOSM, dk_inf.iso);
    const int *wbl;                              /* Light Auto: Canon's Auto WB of the shot (owner 11-17), else as shot */
    int wk = wf_dk_wb_pick(dk_inf.wb, dk_inf.wb_auto, lk->light == WF_LIGHT_AUTO, &wbl);
    if (wk == WF_DKWB_NOAUTO && !dk_wa_said) {
        dk_wa_said = 1;
        wf_log("dark: IMG_%04d: no Auto WB levels in the CR2 (ColorData %d): the as-shot WB (said once a run)",
               roll.file[i], dk_inf.cd_count);
    }
    int as = wf_dk_scales(WF_DK_EOSM, wbl, j.black, j.white, dk_inf.iso, j.wb_q8);
    j.roll = src != WF_DEV_LOG;
    j.hue = dk_lgk ? wf_rb_hue : WF_DEV_HUE_A;   /* chain films (B's switch, wf_rawb.h); cans + sRGB base develop: A */
    j.ms = get_ms_clock;
    struct wf_dev_stat st;
    memset(&st, 0, sizeof st);
    if (wf_dev_prepare(M.scr, M.tabs, &j, &st) != 0) { dk_why = "prepare"; return DK_ERR; }
    wf_dk_thumb_begin(&dk_th, DK_W, DK_H);
    dv.print_ready = 1;
    for (int y = 0; y < DK_H; y += DK_BAND) {    /* rows in order: a split frame's tail goes over the raw behind the reads */
        int yb = y + DK_BAND < DK_H ? y + DK_BAND : DK_H;
        for (int ya = y, ye; ya < yb; ya = ye) {  /* one piece at a time (the split is a multiple of DK_BAND: one call) */
            ye = dk_row_end(ya) < yb ? dk_row_end(ya) : yb;
            uint32_t *d = dk_row(0, ya);
            wf_dev_rows(M.scr, ya, ye, d, DK_FP);
            wf_dk_thumb_rows(&dk_th, d, DK_FP, ya, ye - ya, dv_print);
        }
        dv.expose = yb * 1000 / DK_H;
        dv.prog = 250 + yb * 450 / DK_H;
        dv_bump();
        if (dk_should_stop())
            return DK_STOP;
        dk_yield();
    }
    dk_t[1] = get_ms_clock() - t;
    t += dk_t[1];

    /* 3. fix: the look's stage through the worker (films: table + effects; Log + LUT: effects; plain log: none) */
    dk_phase(WF_DP_FIX, 700);
    struct wf_frame fr;
    memset(&fr, 0, sizeof fr);
    fr.fmt = WF_FMT_UYVY8;
    fr.part = WF_PART_FULL;
    fr.aspect = 0;
    fr.devd = dk_lgk ? 2 : 1;                    /* 2: a chain film's log table, the look stage adds the effects only */
    fr.w = fr.fw = DK_W;
    fr.h = fr.fh = DK_H;
    fr.pitch = DK_FP;
    fr.data = fr.dst = M.frame;
    fr.nseg = (uint8_t)M.nseg;                   /* a split frame: the worker reads its rows through the pieces */
    fr.seg = M.nseg ? M.seg : 0;
    fr.dev_ev8 = wf_dk_ev8(dk_inf.iso, dk_inf.et_n, dk_inf.et_d, dk_inf.fn_n, dk_inf.fn_d);   /* the shot's EV, not the dials' now */
    int status = -1, tries = 0;
    for (;;) {
        fr.t_post = get_ms_clock();
        if (wf_shot_run(&fr, &status) == 0)
            break;
        if (++tries >= 100 || dk_should_stop()) {
            if (!dk_why[0]) dk_why = "worker busy";
            return tries >= 100 ? DK_ERR : DK_STOP;
        }
        msleep(100);
    }
    dk_t[2] = get_ms_clock() - t;
    t += dk_t[2];

    /* 4. dry: the print (Canon's APP1 + our comment), written in chunks; the final thumbnail from the same bands */
    dk_phase(WF_DP_DRY, 800);
    wf_exif_text(lk, lk->grain, lk->halo, lk->bloom, lk->mist, WF_EV_NONE, txt, sizeof txt);
    wf_exif_tail(lk, WF_PATH_DARK, dk_t[0] + dk_t[1] + dk_t[2], txt, sizeof txt);   /* the time to the print (11-17) */
    snprintf(t2, sizeof t2, "%s", txt);
    const char *body = t2;
    static const char pre[] = "WindyFilm: ";
    int pk = 0;
    while (pre[pk] && body[pk] == pre[pk])
        pk++;
    if (!pre[pk])
        body += pk;
    snprintf(txt, sizeof txt, "WindyFilm Darkroom: %s | ROLL %03d F%02d", body, roll.num, i + 1);
    if (al && wf_dk_comment(M.app1 + ao, al, txt) != 0)
        wf_log("dark: F%02d no UserComment field in Canon's APP1", i + 1);
    struct wf_pf_lay L;                          /* the canvas: = the frame (5184 x 3456) without the finishes */
    uint8_t *band = 0;
    uint32_t *rowbuf = 0;
    wf_pf_layout(&L, WF_PFR_OFF, 0, DK_W, DK_H, 1);
    if (dk_pfr || dk_pdt)
        dk_finish(i, ao, al, t2, &L, &band, &rowbuf);
    if (!rowbuf && M.nseg && M.fin_n >= (uint32_t)L.cw * 2 * 16)
        rowbuf = (uint32_t *)M.fin;              /* a 16-row band across two pieces is copied here (a split at a multiple of
                                                    16 rows never makes one without a frame) */
    struct dk_wr wr;
    rc = dk_encode(i, out, &L, al ? M.app1 + ao : 0, al, band, rowbuf, 0, &wr);
    if (rc != DK_OK)
        return rc;
    if (dk_pjp == WF_PJ_OVER)                    /* stopped in it: the print is done, the run stops at the next frame */
        dk_camjpg(i, jpg, out, &wr, M.app1 + ao, al, &L, rowbuf);
    dk_t[3] = get_ms_clock() - t;
    wf_dev_note(lk, WF_PATH_DARK, dk_t[0] + dk_t[1] + dk_t[2] + dk_t[3]);   /* the whole print, dry included (11-17) */
    *bytes = (int)wr.n;
    dv.print_ready = 2;
    wf_log("dark: F%02d IMG_%04d iso %d: load %d expose %d fix %d dry %d ms, %d KB, wb %s, trim %d, black %d white %d%s, "
           "shoulder %s %dms, look %s", i + 1, roll.file[i], dk_inf.iso, dk_t[0], dk_t[1], dk_t[2], dk_t[3],
           (int)(wr.n >> 10), !as ? "daylight" : wk == WF_DKWB_AUTO ? "auto" : "as-shot", wf_dk_trim10(WF_DK_EOSM, dk_inf.iso),
           j.black, j.white,
           lw ? "" : " (table)", st.roll ? wf_dev_hue_name(st.hue) : "-", st.ro_ms, wf_status_name(status));
    return DK_OK;
}

static void dk_hang(int i)
{
    if (dv.nwire >= WF_DR_NWIRE) {                /* the oldest print leaves the wire */
        uint8_t *w0 = dv.wire[0];
        for (int k = 1; k < WF_DR_NWIRE; k++) {
            dv.wire[k - 1] = dv.wire[k];
            dv.wire_frame[k - 1] = dv.wire_frame[k];
        }
        dv.wire[WF_DR_NWIRE - 1] = w0;
        dv.nwire = WF_DR_NWIRE - 1;
    }
    wf_dk_wire(dv_print, dv.wire[dv.nwire]);
    dv.wire_frame[dv.nwire] = i;
    dv.nwire++;
    dv.hung_ms = get_ms_clock();
}

/* before the first frame: the look's effects (halation / grain / mist) and its film table, built here (wf_look_use is
 * WF_LU_DARK now: nothing of them is built while a roll is in the camera), before the big blocks are taken. A part the
 * camera has no memory for stays out of the prints; the log line says what is in ("-" = wanted, not ready). */
static void dk_look(const struct wf_look *lk)
{
    static const char *const bn[WF_BLOOM_MAX + 1] = { "", "bloom low", "bloom normal", "bloom strong" };
    int t0 = get_ms_clock(), hk = 0, gk = 0, mk = 0, n = 0;
    char b[96];
    dv.frame = -1;
    dk_phase(WF_DP_MIX, 0);
    wf_ws_poll();                                /* the look's film table (a pack read) */
    wf_fx_poll();                                /* the effects of its post code (seconds: the grain) */
    if (lk->post) {
        wf_fx_keys(lk->post, &hk, &gk);
        mk = wf_fx_mkey(lk->post);
    }
    b[0] = 0;
    if (lk->output == WF_OUT_FILM)
        n += snprintf(b + n, sizeof b - n, wf_ws_table(lk->row, lk->variant, lk->stk) ? ", film table" : ", film table -");
    if (hk && n < (int)sizeof b)                 /* the halation stage: what runs in it ("halation + bloom normal") */
        n += snprintf(b + n, sizeof b - n, ", %s%s%s%s", WF_FXK_HALO(hk) ? "halation" : "",
                      WF_FXK_HALO(hk) && WF_FXK_BLOOM(hk) ? " + " : "", bn[WF_FXK_BLOOM(hk)], wf_fx_hk == hk ? "" : " -");
    if (gk && n < (int)sizeof b)
        n += snprintf(b + n, sizeof b - n, wf_fx_gk == gk ? ", grain" : ", grain -");
    if (mk && n < (int)sizeof b)
        snprintf(b + n, sizeof b - n, wf_fx_mk == mk ? ", mist" : ", mist -");
    wf_log("dark: look ready %dms: %s", get_ms_clock() - t0, b[0] ? b + 2 : "nothing to build");
    wf_task_yield(1);
}

/* the run, inside wf_task (wf_dark_poll) */
static void dk_task(void)
{
    int t_run = get_ms_clock(), ndone = 0, nerr = 0, paused = 0, ms_sum = 0;
    dk_stopk = 0;
    const char *end_why = "";
    int st0 = roll.state;
    /* a full roll too (RAWB17 and before: a FULL roll stayed FULL, wf_roll_next_dev found nothing, "0 developed, 0 failed,
       3 left" with no reason once the memory was there) */
    if ((roll.state == WF_RS_LOADED && roll.nshot > 0) || roll.state == WF_RS_FULL) {
        wf_roll_rewind(&roll);
        roll_save();
    }
    /* RAW develop B gives its tables + scratch (shoot memory) back while the run lives (wf_rawb_poll sees
     * wf_dark_busy) and builds them again after it: that poll is wf_task's, so it runs here, until B holds nothing */
    int t_b = get_ms_clock();
    while (wf_rawb_holds() && get_ms_clock() - t_b < 10000 && !dk_stop) {   /* a cube build may take seconds */
        wf_rawb_poll();
        if (wf_rawb_holds())
            msleep(20);
    }
    if (wf_rawb_holds())
        wf_log("dark: RAW develop still holds its memory after %d ms", get_ms_clock() - t_b);
    else if (WF_RAWB_CAN && wf_rawb)
        wf_log("dark: RAW develop tables given back (%d ms), built again after the run", get_ms_clock() - t_b);
    struct wf_look lk;
    wf_look_now(&lk);
    dv.total = roll.nshot;
    dv.done = wf_roll_ndev(&roll);
    dv.left = wf_roll_left(&roll);
    dk_pfr = wf_pr_frame > WF_PFR_OFF && wf_pr_frame < WF_PFR_N ? wf_pr_frame : WF_PFR_OFF;   /* the whole run's */
    dk_pin = wf_pr_info > WF_PIN_OFF && wf_pr_info < WF_PIN_N ? wf_pr_info : WF_PIN_OFF;
    dk_pdt = wf_pr_date > WF_PDT_OFF && wf_pr_date < WF_PDT_N ? wf_pr_date : WF_PDT_OFF;
    dk_pjp = wf_pr_jpeg == WF_PJ_OVER ? WF_PJ_OVER : WF_PJ_KEEP;
    dk_pf_said = dk_lg_said = dk_wa_said = 0;
    dk_lgn = 0;
    if (dk_pfr && (FIO_GetFileSize(DK_LG_PACK, &dk_lgn) != 0 || dk_lgn < 40))
        dk_lgn = 0;                              /* no WFLOGO.DAT: the caption as text alone */
    wf_log("dark: roll %03d start, %d of %d to develop, look output %d, print frame %d info %d date %d jpeg %d logo %dK",
           roll.num, dv.left, dv.total, lk.output, dk_pfr, dk_pin, dk_pdt, dk_pjp, (int)(dk_lgn >> 10));
    dk_look(&lk);
    dk_disp_ons = dk_disp_miss = 0;
    int d0 = display_is_on(), lv0 = lv;
    if (lv) {
        PauseLiveView();
        paused = 1;
        int t_p = get_ms_clock();                /* Canon may turn the display off a moment after the pause */
        while (display_is_on() && get_ms_clock() - t_p < 500)
            msleep(20);
    }
    wf_log("dark: screen at the start: display %d -> %d, LiveView %d -> %d%s", d0, display_is_on(), lv0, lv,
           paused ? " (paused)" : "");
    dk_offs = dk_wakes = 0;
    dk_dark_ok = 1;                              /* Screen Off from here: dark WF_DS_WAKE_MS after the last key (SET) */
    if (wf_dk_scr == WF_DS_OFF)
        wf_log("dark: Screen Off: the display goes dark %d s after the last key", WF_DS_WAKE_MS / 1000);
    dk_screen("after the pause");
    powersave_prohibit();
    int src = wf_dark_src(lk.output, lk.flut), lgw = wf_rawb_logpk(&lk);
    dk_lgk = 0;
    if (!src) {
        end_why = "no look (output Off)";
    } else if (dk_alloc() != 0) {
        end_why = "no memory";
    } else if (lgw == WF_RLG_OK && dk_logpk(&lk, M.tabs, M.scr)) {
        src = WF_DEV_CUBE;                       /* the chain film's whole look is in these tables */
    } else {
        if ((lgw = wf_rawb_logpk(&lk)) != WF_RLG_NOTCHAIN)
            wf_log("dark: chain film %s v%d on the 8-bit chain (%s)", wf_films[lk.row].code, lk.variant,
                   wf_rawb_logpk_why(lgw));
        char cn[WF_LL_NAME];
        const char *cube = 0;
        struct wf_ll_info li;
        int rc = WF_LL_OK;
        if ((src == WF_DEV_CUBE || src == WF_DEV_DISP) && !(cube = wf_loglut_name(cn, lk.curve, lk.flut, 0)))
            rc = WF_LL_E_MODE;                   /* a display look names "<CODE>SR.CUB" for any curve */
        /* B's .RBT of these cube tables first (same struct, camera and key: wf_rawb_rbt_key); a miss is built and the
         * file written for B and the next run (Canon is idle here: LiveView paused, no shot) */
        int t0 = get_ms_clock(), curve = src >= WF_DEV_SRGB ? 0 : lk.curve, got = -1;
        uint32_t rk[WF_RBT_NKEY];
        char rp[WF_LL_NAME];
        int rbt = rc == WF_LL_OK && wf_rawb_rbt_key(&lk, src, cube, rp, rk) == 0;
        if (rbt) {
            got = wf_llb_read(rp, rk, WF_RBT_NKEY, M.tabs, sizeof(struct wf_dev_tabs));
            if (got == WF_LLB_HIT && (M.tabs->src != src || M.tabs->curve != curve || M.tabs->cam != WF_DEV_EOSM
                                      || M.tabs->n < 2 || M.tabs->n > 65))
                got = WF_LLB_BAD;
        }
        if (got != WF_LLB_HIT && rc == WF_LL_OK && src == WF_DEV_DISP)   /* LUT EI by hand, H / S on the cube's output (as B) */
            rc = wf_dev_build_disp(M.tabs, WF_DEV_EOSM, cube, lk.lei, lk.hl, lk.sh, &li);
        else if (got != WF_LLB_HIT && rc == WF_LL_OK)
            rc = wf_dev_build(M.tabs, src, curve, WF_DEV_EOSM, cube,
                              src == WF_DEV_CUBE ? lk.lei : 0, src == WF_DEV_CUBE && lk.flut != 0, &li);
        if (rc == WF_LL_OK) {
            static const char *const llb[4] = { "hit", "none", "key", "bad" };
            char rs[64];                         /* ", <name>.RBT hit" / ", <name>.RBT none, written 120ms" */
            int t1 = get_ms_clock(), wr = rbt && got != WF_LLB_HIT
                                          ? wf_llb_write(rp, rk, WF_RBT_NKEY, M.tabs, sizeof(struct wf_dev_tabs)) : 0;
            rs[0] = 0;
            if (rbt && got == WF_LLB_HIT)
                snprintf(rs, sizeof rs, ", %s hit", rp + 8);
            else if (rbt && !wr)
                snprintf(rs, sizeof rs, ", %s %s, written %dms", rp + 8, llb[got], get_ms_clock() - t1);
            else if (rbt)
                snprintf(rs, sizeof rs, ", %s %s, not written (%s)", rp + 8, llb[got], wf_fe_name(wr));
            int pap = cube ? lk.paper : 0;       /* the cube tables' paper after the .RBT (as B: the file is unpapered) */
            if (pap)
                wf_dev_paper(M.tabs, pap);
            wf_log("dark: tables %s c%d%s%s %dms%s%s%s",
                   src == WF_DEV_SRGB ? "srgb" : src == WF_DEV_LOG ? "log" : src == WF_DEV_DISP ? "disp" : "cube",
                   curve, cube ? " " : "", cube ? cube + 8 : "", t1 - t0, rs, pap ? ", paper " : "",
                   pap ? wf_paper_name(pap) : "");
        }
        if (rc != WF_LL_OK) {
            wf_log("dark: tables err %d %s", rc, wf_loglut_errstr(rc));
            end_why = "develop tables";
        }
    }
    if (!end_why[0]) {                           /* <root>ROLLS and <root>ROLLS/ROLLnnn */
        char root[16], d[48];
        struct wf_fe e = { 0, 0 };
        d[0] = 0;
        if (wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_dir(&roll, root, 0, d, sizeof d) < 0
            || wf_fmkdir(d, &e) != 0 || wf_roll_dir(&roll, root, 1, d, sizeof d) < 0 || wf_fmkdir(d, &e) != 0) {
            wf_log("dark: folder %s: %s %d", d, wf_fe_name(e.step), e.rc);
            end_why = "cannot make the ROLLS folder";
        }
    }
    while (!end_why[0]) {
        int i = wf_roll_next_dev(&roll);
        if (i < 0) {
            if (wf_roll_left(&roll) > 0) {       /* frames left that the roll does not hand out: say why, not a blank end */
                wf_log("dark: roll state %d (at start %d), %d left: no frame to develop", roll.state, st0,
                       wf_roll_left(&roll));
                end_why = "no frame to develop";
            }
            break;
        }
        int bat = dk_battery();
        dv.battery = bat;
        if (bat >= 0 && bat < DK_BAT_STOP) {
            end_why = "Battery low: charge, then develop the rest";
            break;
        }
        char sw[12];
        snprintf(sw, sizeof sw, "before F%02d", i + 1);
        dk_screen(sw);
        dv.frame = i;
        dv.left = wf_roll_left(&roll);
        dk_t0 = get_ms_clock();
        int bytes = 0, r = dk_frame(i, &lk, src, &bytes);
        if (r == DK_STOP) {
            end_why = dk_why;
            break;
        }
        if (r == DK_OK) {
            wf_roll_mark(&roll, i, WF_RF_DEV);
            ndone++;
            ms_sum += get_ms_clock() - dk_t0;
            dv.avg_ms = ms_sum / ndone;
            dk_hang(i);
        } else {
            wf_log("dark: F%02d IMG_%04d failed: %s", i + 1, roll.file[i], dk_why);
            if (r == DK_FATAL) {
                end_why = dk_why;
                break;
            }
            wf_roll_mark(&roll, i, WF_RF_ERR);
            nerr++;
        }
        if (roll_save() != 0)
            wf_log("dark: roll file not saved");
        dv.done = wf_roll_ndev(&roll);
        dv.left = wf_roll_left(&roll);
        roll_changed();
        dv_bump();
        wf_task_yield(1);                        /* the frame's lines to the card (no file of the run is open now) */
    }
    dk_free();
    dk_dark_ok = 0;                              /* the result shows: no more dark from the UI's tick */
    if (dk_offs) {                               /* an off asked a moment ago may still be on its way: let it land */
        int w = DK_OFF_EVERY - (get_ms_clock() - dk_off_ms);
        if (w > 0)
            msleep(w);
    }
    powersave_permit();
    int d1 = display_is_on();
    if (paused)
        ResumeLiveView();
    dk_disp_miss = 0;                            /* the screen after the run: asked again whatever the run's asks did */
    dk_screen("after the run");
    if (dk_offs && !display_is_on() && wf_unloading && !ml_shutdown_requested)
        display_on();                            /* unloading after a dark run: not left dark (dk_screen asks nothing now) */
    wf_log("dark: screen at the end: display %d -> %d, LiveView %d%s, display turned on %d times", d1, display_is_on(), lv,
           paused ? " (resumed)" : "", dk_disp_ons);
    if (dk_offs || dk_wakes)
        wf_log("dark: Screen Off: display turned off %d times, woken %d times", dk_offs, dk_wakes);
    int left = wf_roll_left(&roll);
    dv.frame = -1;
    dv.phase = WF_DP_NONE;
    dv.left = left;
    dv.msg = end_why;
    dv.state = !left ? WF_DR_DONE : dk_stopk || !end_why[0] ? WF_DR_STOPPED : WF_DR_FAIL;
    wf_log("dark: roll %03d end: %d developed, %d failed, %d left, %d s%s%s", roll.num, ndone, nerr, left,
           (get_ms_clock() - t_run) / 1000, end_why[0] ? ": " : "", end_why);
    roll_changed();
    dv_bump();
    WF_BARRIER();
    dk_running = 0;
}

#else
int wf_dark_start(void) { dv.msg = "The Darkroom runs on the EOS M first"; dv_bump(); return -3; }
void wf_dark_stop(void) {}
void wf_dark_poll(void) {}
int wf_dark_wake(int now) { (void)now; return 0; }
int wf_dark_dark(int now) { (void)now; return 0; }
#endif

#if WF_DARK_CAN
int wf_dark_start(void)
{
    if (dk_running)
        return -2;
    if (!wf_cam_ok || wf_unloading || wf_job_state() || is_movie_mode() || req_kind || wf_rawb_busy())
        return -3;                               /* (a shot's B develop still runs: 30 s) */
    if (wf_cam() != WF_CAM_EOSM) {
        dv.msg = "The Darkroom runs on the EOS M first";
        dv_bump();
        return -3;
    }
    if (!wf_worker_up()) {                       /* a boot's first seconds (wf_main.c tries it again): the look stage */
        dv.msg = "Starting: try again in a moment";   /* runs on the worker */
        dv_bump();
        return -3;
    }
    int n = roll.state == WF_RS_LOADED ? roll.nshot : wf_roll_left(&roll);
    if (n <= 0)
        return -1;
    int bat = dk_battery();
    dv.battery = bat;
    if (bat >= 0 && bat < DK_BAT_GO) {
        dv.msg = "Battery low: charge it first";
        dv_bump();
        return -3;
    }
    dk_stop = 0;
    dv.state = WF_DR_RUN;
    dv.phase = WF_DP_LOAD;
    dv.frame = -1;
    dv.msg = "";
    dv.prog = dv.expose = 0;
    dv.print_ready = 0;
    dv.nwire = 0;
    dv.avg_ms = 0;
    dk_t0 = get_ms_clock();
    dk_wake_ms = dk_t0;                          /* Screen Off: the SET that started it is the last key */
    WF_BARRIER();
    dk_running = 1;                              /* the roll is the run's from here (the shutter goes free) */
    dk_go = 1;                                   /* wf_task's next loop runs it: no task to create, none to be refused */
    dv_bump();
    return 0;
}

/* wf_task, right after wf_roll_poll: a run asked for is carried out here, the whole run (minutes) */
void wf_dark_poll(void)
{
    if (!dk_go)
        return;
    dk_go = 0;
    dk_task();
}

void wf_dark_stop(void)
{
    if (!dk_running)
        return;
    dk_stop = 1;
    dv.state = WF_DR_STOPPING;
    dv_bump();
}

#endif

void wf_dark_init(void)
{
    memset(&dv, 0, sizeof dv);
    dv.frame = -1;
    dv.battery = -1;
    dv.msg = "";
    dv.print = dv_print;
    for (int i = 0; i < WF_DR_NWIRE; i++)
        dv.wire[i] = dv_wire[i];
}

/* wf_task at unload (a run of its own already stopped at its next band: wf_unloading); one asked for and not begun is
 * dropped; Canon's quality / review back */
void wf_dark_end(void)
{
    dk_stop = 1;
    if (dk_go) {
        dk_go = 0;
        dv.state = WF_DR_STOPPED;
        dv.msg = "unload";
        WF_BARRIER();
        dk_running = 0;
    }
    if (pq_old >= 0 && pic_quality != pq_old)
        prop_request_change(WF_PROP_PIC_QUALITY, &pq_old, 4);
    if (rv_old >= 0 && image_review_time == 0)
        prop_request_change(WF_PROP_REVIEW, &rv_old, 4);
    if (roll_dirty)
        roll_save();
    roll_shutter(1);                             /* the shutter never stays held without the module */
    if (hud_fonts_on)
        wfdr_hud_fonts(0);
}
