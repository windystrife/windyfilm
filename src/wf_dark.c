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
#define WF_DARK_CAN 1                            /* both cameras (the 5D2 since 11-17: DESIGN 3.18, its CR2 in wf_darkp.h) */
#if WF_DARK_CAN
#include "wf_darkp.h"
#include "wf_cr2.h"
#include "wf_dev.h"
#include "wf_jpe.h"
#if WF_DK_EV_BAD != WF_EV_BAD
#error "wf_darkp.h WF_DK_EV_BAD must equal wf_fx.h WF_EV_BAD"
#endif
#include "wf_print.h"
#include "wf_photo.h"
#endif

/* ML core (EOSM_202.sym / 5D2_212.sym both export these) */
extern void *FIO_OpenFile(const char *name, unsigned mode);
extern int   FIO_ReadFile(void *f, void *buf, size_t n);
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);
struct fio_file;                                 /* DryOS directory entries, opaque (as wf_loglut.c): the shelf's list */
struct wf_file_info { uint16_t mode; uint32_t size; uint64_t timestamp; char name[0x80]; };
extern struct fio_file *alloc_fio_file(void);
extern struct wf_file_info convert_fio_file_info(struct fio_file *f);
extern void *FIO_FindFirstEx(const char *dir, struct fio_file *f);   /* error: 0 or odd */
extern int   FIO_FindNextEx(void *d, struct fio_file *f);             /* 0 = next entry */
extern void  FIO_FindClose(void *d);
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
static volatile int req_kind, req_film, req_exp, req_devd;  /* 1 load, 2 rewind, 3 close, 4 close + load (swap) */
static int poll_F = -1, poll_D = -1, poll_stable, wind_t = -100000;
static int pq_old = -1, rv_old = -1, prop_t;
static int hud_on, hud_fonts_on, hud_t, hud_mode = -1, info_t = -100000, hud_sig, hud_job;
static int hud_font_again, hud_font_t, hud_font_n;  /* the font pack read again while Canon held the card */
static volatile int dk_running;                   /* a darkroom run owns the roll (asked for, or in wf_task now) */
static volatile int dk_go;                        /* wf_dark_start asked: wf_task's next wf_dark_poll runs it */
static int nt_state = -1, nt_req, nt_t, nt_len, nt_mode = -1, nt_pt;   /* the end-of-roll notice (roll_notice) */
static volatile int nt_up, nt_drop, nt_poke;
static volatile int sh_k;                         /* the shutter held now (roll_shutter): 0 no, 1 full, 2 rewound */
static volatile int aside;                        /* the roll set aside (wf_roll_shelve, wf_dark.h): RAM only */
static struct wf_shelf shv;                       /* the shelf list (wf_dark.h; wf_task writes it, the UI reads it) */
static volatile int sh_scan = 1;                  /* list the card's ROLLS again at the next idle poll (boot: 1) */
static struct wf_roll *dkr __attribute__((unused)) = &roll;   /* the roll a darkroom run develops: the camera's, */
                                                  /* or sh_roll (the 5D2: no darkroom, unused) */

const struct wf_roll *wf_roll_now(void) { return &roll; }
uint32_t wf_roll_gen(void) { return roll_gen_n; }
static void roll_changed(void) { roll_gen_n++; wf_gen_bump(); }
int wf_roll_shooting(void) { return roll.state == WF_RS_LOADED; }
int wf_roll_shelved(void) { return aside && wf_roll_open(&roll); }
int wf_roll_blocks_play(void) { return wf_blind && wf_roll_open(&roll) && !wf_roll_shelved() && !dk_running; }

/* any task (the UI's mode chip); wf_task's next poll applies it (shutter, look, notice) */
int wf_roll_shelve(int on)
{
    if (on && !wf_roll_open(&roll))
        return -1;
    if (!on == !aside)
        return 0;
    aside = on ? 1 : 0;
    wf_log("roll: %03d %s (%d/%d %s)", roll.num, on ? "set aside: the camera shoots in another mode, PLAY develops it"
           : "back in use (no longer set aside)", roll.nshot, roll.exp, wf_roll_state_name(roll.state));
    roll_changed();
    return 0;
}
int wf_dark_busy(void) { return dk_running; }
int wf_roll_wind(void) { return wf_roll_open(&roll) && get_ms_clock() - wind_t < 1000; }   /* the finder's wind-on cue */
const struct wf_shelf *wf_shelf_now(void) { return &shv; }
void wf_shelf_rescan(void) { sh_scan = 1; }

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
int wf_roll_req_swap(int film, int exp, int devd)
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
    req_devd = devd != 0;
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

/* ------------------------------------------------------------------ the shelf (owner 11-17, DESIGN 3.15) */
#if WF_DARK_CAN
static char sh_txt[WF_ROLL_FILE] __attribute__((aligned(32)));
static uint8_t sh_pass[1000];                     /* the scan: the newest develop pass of each roll number on the card */
static struct wf_roll sh_roll, sh_tmp;            /* the shelf roll a run develops; the scan's roll read back */

/* "ROLL021" or "ROLL021B" of a roll (the logs' name for it) */
static const char *shelf_nm(const struct wf_roll *r, char *b) { wf_roll_name(r->num, r->dev, b); return b; }

/* a roll's shelf copy (wf_roll_dir level 2), written whole: in place when it is there, else created (Canon idle:
   wf_task's idle poll or a darkroom run), the folders made first. why: the log line's reason (0 = none). 0 = ok */
static int shelf_put(const struct wf_roll *r, const char *why)
{
    char root[16], p[48], nm[12];
    struct wf_fe e = { 0, 0 };
    uint32_t sz = 0;
    p[0] = 0;
    if (wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_text(r, sh_txt, sizeof sh_txt) != WF_ROLL_FILE
        || wf_roll_dir(r, root, 0, p, sizeof p) < 0 || wf_fmkdir(p, &e) != 0 || wf_roll_dir(r, root, 1, p, sizeof p) < 0
        || wf_fmkdir(p, &e) != 0 || wf_roll_dir(r, root, 2, p, sizeof p) < 0) {
        wf_log("roll: %s not on the shelf: folder %s %s %d", shelf_nm(r, nm), p, wf_fe_name(e.step), e.rc);
        return -1;
    }
    void *f = wf_fopen_w(p, FIO_GetFileSize(p, &sz) == 0 ? WF_FO_AT : WF_FO_NEW, 0, &e);
    int w = f ? FIO_WriteFile(f, sh_txt, WF_ROLL_FILE) : -1;
    if (f)
        FIO_CloseFile(f);
    if (w != WF_ROLL_FILE) {
        wf_log("roll: %s not on the shelf: %s %s %d", shelf_nm(r, nm), p + 3, f ? "write" : wf_fe_name(e.step),
               f ? w : e.rc);
        return -1;
    }
    if (why)
        wf_log("roll: %03d on the shelf (%s): %d/%d, %d to develop, %s", r->num, why, r->nshot, r->exp, wf_roll_left(r),
               wf_roll_state_name(r->state));
    sh_scan = 1;
    return 0;
}

/* a pass's roll file back from the card into r (the folder names the pass); 0 = ok, -1 none, -2 unreadable */
static int shelf_read(int num, int dev, struct wf_roll *r)
{
    char root[16], p[48];
    uint32_t sz = 0;
    r->num = num;
    r->dev = dev < 2 ? 0 : dev;
    if (wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_dir(r, root, 2, p, sizeof p) < 0
        || FIO_GetFileSize(p, &sz) != 0 || sz == 0)
        return -1;
    uint32_t n = sz < WF_ROLL_FILE ? sz : WF_ROLL_FILE;
    if (wf_card_read(p, 0, sh_txt, n, 0) != 0 || wf_roll_parse(r, sh_txt, (int)n) != 0 || r->num != num)
        return -2;
    r->dev = dev < 2 ? 0 : dev;                  /* the folder wins */
    return 0;
}

static int shelf_has(int num) { uint32_t sz = 0; char root[16], p[48]; struct wf_roll t; t.num = num; t.dev = 0;
    return wf_roll_root(wf_dcim_dir(), root, sizeof root) >= 0 && wf_roll_dir(&t, root, 2, p, sizeof p) >= 0
           && FIO_GetFileSize(p, &sz) == 0; }

/* the card's ROLLS listed again (wf_task, Canon idle, no run): each ROLLnnn[B..Z] folder's newest pass with a roll
   file, newest first down from the camera's roll number (the numbers wrap 999 -> 1), WF_SHELF_MAX of them; logged
   when the list changed (folders without a roll file: rolls developed before the shelf) */
static void __attribute__((noinline)) shelf_scan(void)
{
    static struct wf_shelf ns;
    char root[16], d[24], lb[100], nf[40];
    int nofile = 0, nfo = 0, cam = wf_roll_open(&roll) ? roll.num : 0, t0 = get_ms_clock();
    sh_scan = 0;
    memset(&ns, 0, sizeof ns);
    memset(sh_pass, 0, sizeof sh_pass);
    nf[0] = 0;
    if (roll.state == WF_RS_DONE && roll.nshot > 0 && roll.num >= 1 && roll.num <= 999 && !shelf_has(roll.num)
        && roll.dev < 2) {                       /* the camera's last roll, developed before the shelf: its copy now */
        shelf_put(&roll, "developed before the shelf");
        sh_scan = 0;
    }
    if (wf_roll_root(wf_dcim_dir(), root, sizeof root) >= 0) {
        snprintf(d, sizeof d, "%sROLLS/", root);
        struct fio_file *f = alloc_fio_file();
        void *h = f ? FIO_FindFirstEx(d, f) : 0;
        if (h && !((uintptr_t)h & 1)) {
            do {
                struct wf_file_info fi = convert_fio_file_info(f);
                int n, v;
                if ((fi.mode & 0x10) && wf_roll_name_parse(fi.name, &n, &v) == 0 && n <= 999 && v > sh_pass[n])
                    sh_pass[n] = (uint8_t)v;
            } while (FIO_FindNextEx(h, f) == 0);
            FIO_FindClose(h);
        }
        if (f)
            __mem_free(f);
    }
    int top = roll.num >= 1 && roll.num <= 999 ? roll.num : 999;
    for (int k = 0; k < 999; k++) {
        int n = top - k < 1 ? top - k + 999 : top - k, got = -1;
        if (!sh_pass[n] || n == cam)             /* (the camera's open roll: the Darkroom lists it itself) */
            continue;
        if (ns.n >= WF_SHELF_MAX) {
            ns.more++;
            continue;
        }
        for (int v = sh_pass[n]; v >= 1 && got != 0; v--)
            got = shelf_read(n, v, &sh_tmp);
        if (got != 0) {
            if (nofile++ < 3)
                nfo += snprintf(nf + nfo, sizeof nf - nfo, " ROLL%03d", n);   /* (3 x 8 chars: fits) */
            continue;
        }
        if (sh_tmp.nshot <= 0)
            continue;
        struct wf_shelf_ent *e = &ns.e[ns.n++];
        e->num = (int16_t)sh_tmp.num;
        e->dev = (int16_t)(sh_tmp.dev < 2 ? 1 : sh_tmp.dev);
        e->film = (int16_t)sh_tmp.film;
        e->own = (int16_t)(sh_tmp.own ? sh_tmp.own : sh_tmp.film);
        e->exp = (int16_t)sh_tmp.exp;
        e->state = (int8_t)sh_tmp.state;
        e->nshot = (int8_t)sh_tmp.nshot;
        e->left = (int8_t)wf_roll_left(&sh_tmp);
        e->ndev = (int8_t)wf_roll_ndev(&sh_tmp);
    }
    int same = ns.n == shv.n && ns.more == shv.more;
    for (int k = 0; same && k < (int)sizeof ns.e; k++)    /* (no memcmp in the module's imports) */
        same = ((const uint8_t *)ns.e)[k] == ((const uint8_t *)shv.e)[k];
    ns.gen = shv.gen + 1;
    shv = ns;
    if (same && shv.gen > 1)
        return;
    int o = 0;
    lb[0] = 0;
    for (int k = 0; k < shv.n && o < (int)sizeof lb - 24; k++) {
        const struct wf_shelf_ent *e = &shv.e[k];
        char nm[12];
        wf_roll_name(e->num, e->dev, nm);
        o += e->left ? snprintf(lb + o, sizeof lb - o, "%s%s %d to develop", k ? ", " : "", nm + 4, e->left)
                     : snprintf(lb + o, sizeof lb - o, "%s%s done %d/%d", k ? ", " : "", nm + 4, e->ndev, e->nshot);
    }
    if (o >= (int)sizeof lb - 24 && o < (int)sizeof lb - 4)
        snprintf(lb + o, sizeof lb - o, " ..");
    wf_log("roll: shelf %d roll%s (%s), %dms", shv.n, shv.n == 1 ? "" : "s", shv.n ? lb : "empty", get_ms_clock() - t0);
    if (shv.more)
        wf_log("roll: shelf: %d older roll%s not listed (the newest %d are)", shv.more, shv.more == 1 ? "" : "s",
               WF_SHELF_MAX);
    if (nofile)
        wf_log("roll: shelf: %d roll folder%s without a roll file (developed before the shelf):%s%s", nofile,
               nofile == 1 ? "" : "s", nf, nofile > 3 ? " .." : "");
}
#endif

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
        if (wf_hud_lock(50)) {
            wfdr_notice(bmp_vram(), WF_BMPPITCH, WF_HUD_CLEAR);
            wf_hud_lock(-1);
        }
        redraw();                                /* Canon's own LiveView marks under the box come back */
        hud_mode = -1;                           /* (the HUD paints again after it) */
        nt_len = nt_drop = nt_up = 0;
        return;
    }
    if ((mode != nt_mode || now - nt_pt >= 500) && wf_hud_lock(50)) {
        wfdr_notice(bmp_vram(), WF_BMPPITCH, mode);
        wf_hud_lock(-1);
        nt_mode = mode;
        nt_pt = now;
    }
    nt_up = 1;
}

/* the quick keys' box (wf_menu.c, DESIGN 3.17; wf_dark.h) */
void wf_dark_toast(const char *big, const char *k1, const char *t1, const char *k2, const char *t2, int mode)
{
    if (mode < 0) {
        wfdr_toast(0, 0, 0, 0, 0, 0, 0, WF_HUD_CLEAR);
        return;
    }
    if (!wf_hud_lock(50))
        return;
    wfdr_toast(bmp_vram(), WF_BMPPITCH, big, k1, t1, k2, t2, mode);
    wf_hud_lock(-1);
    if (mode == WF_HUD_CLEAR) {
        redraw();                                /* Canon's own LiveView marks under the box come back */
        hud_mode = -1;                           /* (the roll HUD paints again after it) */
    }
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
    int fwant = wf_roll_open(&roll) || wf_osvf_wants();   /* the roll HUD's, or the film OS's LiveView finder's */
    if (fwant && !hud_fonts_on && !wf_ui_is_open() && wf_hud_lock(200)) {
        hud_font_again = wfdr_hud_fonts(1) == 1;
        wf_hud_lock(-1);
        hud_fonts_on = 1;
        hud_font_t = now;
        hud_font_n = 0;
    } else if (!fwant && hud_fonts_on && !wf_ui_is_open() && !wf_osvf_poll(now) && wf_hud_lock(200)) {
        wfdr_hud_fonts(0);                       /* (the finder is down first: it paints with them) */
        wf_hud_lock(-1);
        hud_fonts_on = 0;
    } else if (hud_fonts_on && hud_font_again && !wf_ui_is_open() && now - hud_font_t >= 2000 && wf_hud_lock(200)) {
        hud_font_t = now;                        /* (the UI reads it again itself while it is open) */
        hud_font_again = wfdr_hud_fonts(2) == 1;
        wf_hud_lock(-1);
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
    if (wf_osvf_poll(now)) {                     /* the film OS's LiveView finder has the screen (wf_osvf.c) */
        if (hud_on && hud_mode >= 0 && !wf_ui_is_open())
            redraw();                            /* the roll HUD's chip and counter go (the finder paints again) */
        hud_on = 0;
        hud_mode = -1;
        return;
    }
    if (want) {
        int blink = now - wind_t < 1000, t = now - info_t;
        int mode = t < HUD_INFO_MS ? WF_HUD_FULL : t < HUD_INFO_MS + HUD_FADE_MS ? WF_HUD_FADE : WF_HUD_SMALL;
        if ((!hud_on || mode != hud_mode || blink || now - hud_t >= 500) && !gui_menu_shown() && !wf_ui_is_open()) {
            /* (again right before the pixels: the ML menu or the UI may have come since the top; never over them) */
            /* the first small pass clears what the full HUD drew (only our own rectangles) */
            if (wf_hud_lock(50)) {
                wfdr_hud(bmp_vram(), WF_BMPPITCH, blink, mode == WF_HUD_SMALL && hud_mode != WF_HUD_SMALL ? WF_HUD_CLEAR : mode);
                wf_hud_lock(-1);
                hud_t = now;
                hud_mode = mode;
            }
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
    if (k != 2 && aside) {                       /* a load, close or close-and-load ends the set-aside; a rewind keeps it */
        aside = 0;
        wf_log("roll: %03d no longer set aside (%s)", roll.num, k == 3 ? "close" : "a new roll");
    }
    if (k == 4) {                                /* the browser's close-and-load: the finished roll goes on the shelf */
        int ok = 0;
#if WF_DARK_CAN
        if (req_devd) {                          /* developed at the shot (RAW develop / JPEG 8-bit): it ends here */
            if (wf_roll_close(&roll) == 0)
                wf_log("roll: %03d closed for a new roll: %d frame(s) developed at the shot", roll.num, roll.nshot);
        } else {
            wf_roll_rewind(&roll);
        }
        roll_dirty = 1;
        ok = shelf_put(&roll, req_devd ? "developed at the shot" : "a new roll") == 0 || req_devd;
        if (ok && roll.state != WF_RS_DONE)
            roll.state = WF_RS_EMPTY;            /* it left the camera (its number stays: the new roll is the next) */
#endif
        if (!ok)                                 /* never a frame lost: the roll stays, no new one */
            wf_log("roll: %03d stays in the camera (not on the shelf): the new roll is not loaded", roll.num);
        k = ok ? 1 : 0;
    }
    if (k == 1) {
        static struct wf_roll ld;                /* the new roll made aside (after a close-and-load's changes above): */
        ld = roll;                               /* its number settled by the shelf's card reads before it shows */
        if (wf_card_nums(&D, &F) != 0) {
            wf_log("roll: no file number, load refused");
        } else if (wf_roll_load(&ld, req_film, req_exp, D, F) == 0) {
#if WF_DARK_CAN
            int n0 = ld.num;
            for (int t = 0; t < 64 && shelf_has(ld.num); t++)   /* never over a shelf roll */
                ld.num = ld.num >= 999 ? 1 : ld.num + 1;
            if (ld.num != n0)
                wf_log("roll: ROLL%03d is on the card's shelf already: this roll is %03d", n0, ld.num);
            sh_scan = 1;                         /* the list goes down from the new number (the camera's roll not in it) */
#endif
            roll = ld;
            int rc = wf_loaded() == req_film ? WF_LOAD_OK : wf_load(req_film);
            wf_log("roll: %03d loaded, %s x%d at %d/%04d (film load %d)", roll.num, f ? f->name : "?", roll.exp, D, F, rc);
            if (rc == WF_LOAD_OK)
                wf_iso_film(req_film, 1);        /* owner 11-17: the camera's ISO to the film's speed (wf_menu.c) */
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
#if WF_DARK_CAN
            if (roll.nshot > 0)                  /* on the shelf as closed: the Darkroom can develop it again */
                shelf_put(&roll, "closed");
#endif
        }
    }
    WF_BARRIER();
    req_kind = 0;
    roll_changed();
}

#if WF_DARK_CAN
static void ph_poll(int idle);
#endif
void wf_roll_poll(void)
{
    if (!roll_read_done) {
        roll_read_done = 1;
        roll_read();
        roll_changed();
    }
    if (dk_running) {
        wf_osvf_poll(get_ms_clock());            /* the LiveView finder goes down for the run (wf_dark_busy) */
        hud_on = 0;
        nt_state = -1;                           /* a run rewinds the roll: no end notice for that */
        nt_req = nt_len = nt_up = 0;
        roll_shutter(0);                         /* developing: the shutter free */
        return;
    }
    int idle = wf_job_state() == 0 && !wf_shot_pending();
    if (idle)
        roll_req();
#if WF_DARK_CAN
    if (idle && sh_scan)
        shelf_scan();
    ph_poll(idle);                               /* the Darkroom screen's photos (RAWB28) */
#endif
    if (aside && !wf_roll_open(&roll)) {         /* developed (done) or closed: nothing set aside any more */
        aside = 0;
        wf_log("roll: %03d %s: no longer set aside", roll.num, wf_roll_state_name(roll.state));
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
/* the camera's L frame in its CR2 (wf_darkp.h wf_dk_geom: EOS M 5184 x 3456 at 84, 64; 5D2 5616 x 3744 at 168, 56, its
   develop 5632 columns wide) */
#ifdef WF_BUILD_5D2
#define DK_CAM      WF_DK_5D2
#define DK_DEV      WF_DEV_5D2
static const struct wf_dk_geom dkg __attribute__((unused)) = WF_DK_GEOM_5D2;
#else
#define DK_CAM      WF_DK_EOSM
#define DK_DEV      WF_DEV_EOSM
static const struct wf_dk_geom dkg __attribute__((unused)) = WF_DK_GEOM_EOSM;
#endif
#define DK_W        (dkg.w)
#define DK_H        (dkg.h)
#define DK_X0       (dkg.x0)
#define DK_Y0       (dkg.y0)
#define DK_RD       (256 * 1024)                 /* card reads / writes through the DMA buffer */
#define DK_IN       (256 * 1024)                 /* the decoder's cached input */
#define DK_OB       (64 * 1024)                  /* encoder output buffer */
#define DK_APP1     (64 * 1024)
#define DK_BAND     64
#define DK_JROWS    64
#define DK_BAT_GO   30
#define DK_BAT_STOP 15
#define DK_RAW_N    ((uint32_t)dkg.pitch * (uint32_t)dkg.sh)   /* the CR2's sensor image, ML packed 14-bit (EOS M 9240 x
                                                    3528 = 31.1 MB, 5D2 10136 x 3804 = 36.8 MB) */

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
int wf_dark_eta_left(const struct wf_dark_view *v, int left)
{
    int per = v->avg_ms ? v->avg_ms : 55000;
    return left > 0 ? left * (per / 100) / 10 : 0;
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
volatile int wf_pr_frame, wf_pr_info = 1, wf_pr_date, wf_pr_jpeg, wf_pr_jpg2, wf_pr_cr2;
volatile int wf_dk_scr;                          /* Screen Off (wf_dark.h, DESIGN 3.11): read live, a run follows a change */
volatile int wf_dk_photo = 1;                    /* the Darkroom's photos (wf_dark.h, RAWB28) */

/* ------------------------------------------------------------------ the Darkroom's photos (owner RAWB28, wf_photo.h)
 * The Darkroom screen asks each tick for the roll it shows (wf_dark_photos: the UI task, no card access); wf_task's idle
 * poll (ph_poll; never in a run) reads its frames a CR2 at a time through wf_photo.c: the frame the run prints first
 * (its cell and the print sketch's picture), then the other cells in order, PH_BUDGET ms of reads a poll. One block
 * while asked (the cells, the picture, the 16-bit scratch, the rows) and a DMA buffer, given back PH_ASK_MS after the
 * last ask (the screen closed, INFO off), at a run's start and at unload. A sheet starts again when the roll asked for
 * or its frames change (ph_sig: a new frame, a develop, a shelf pass). A frame's CR2 in DCIM, else where RAW files
 * Move to ROLLS put it. */
#if WF_DARK_CAN
#define PH_ASK_MS 3000
#define PH_BUDGET 100
#define PH_DMA    (32 * 1024)
#define PH_CELLS  (WF_DK_SHEET * WF_PH_CW * WF_PH_CH)
#define PH_BIG    (WF_PH_BW * WF_PH_BH)
#define PH_MEM    (PH_CELLS + PH_BIG + PH_BIG * 2 + WF_PH_ROWS * 2)
typedef char ph_mem_check[PH_CELLS >= WF_PH_LMAX * WF_PH_LW * WF_PH_LH && (PH_CELLS + PH_BIG) % 4 == 0 ? 1 : -1];
static struct wf_dark_photos ph, ph_empty;       /* what the UI reads (wf_task writes ph) */
static volatile int ph_ask_num = -1, ph_ask_cam, ph_ask_t;
static uint8_t *ph_mem, *ph_dma, *ph_old, *ph_old_dma;   /* dropped blocks are freed a poll later (a draw may be on) */
static uint16_t *ph_t16, *ph_rows;
static struct wf_roll ph_shroll;                 /* a shelf roll's frames: its roll file read back */
static const struct wf_roll *ph_r;               /* the roll read: the camera's or ph_shroll */
static uint32_t ph_sig;
static int ph_next, ph_t0, ph_nok, ph_nnone, ph_fail_t = -100000;
static struct wf_ph ph_t;

const struct wf_dark_photos *wf_dark_photos(int num, int cam)
{
    ph_ask_num = num;
    ph_ask_cam = cam;
    ph_ask_t = get_ms_clock();
    WF_BARRIER();
    return ph.n > 0 && ph.num == num && ph.cam == cam ? &ph : &ph_empty;
}

static void ph_free_old(void)
{
    if (ph_old)
        __mem_free(ph_old);
    if (ph_old_dma)
        __mem_free(ph_old_dma);
    ph_old = ph_old_dma = 0;
}
static void ph_drop(void)
{
    ph.n = 0;
    ph.num = -1;
    WF_BARRIER();
    ph.gen++;
    ph_free_old();
    ph_old = ph_mem;
    ph_old_dma = ph_dma;
    ph_mem = ph_dma = 0;
    ph_sig = 0;
}

/* what the sheet shows of a roll: 0 = nothing (no roll, no frame); *dev = a shelf roll's pass */
static uint32_t ph_sig_of(int num, int cam, int *dev)
{
    int v[6];
    *dev = 0;
    if (cam) {
        if (!wf_roll_open(&roll) || roll.nshot <= 0)
            return 0;
        v[0] = roll.num; v[1] = roll.nshot; v[2] = wf_roll_next_dev(&roll); v[3] = roll.file[roll.nshot - 1];
        v[4] = roll.folder[roll.nshot - 1]; v[5] = -1;
    } else {
        int k = 0;
        while (k < shv.n && shv.e[k].num != num)
            k++;
        if (k >= shv.n || shv.e[k].nshot <= 0)
            return 0;
        *dev = shv.e[k].dev;
        v[0] = num; v[1] = shv.e[k].nshot; v[2] = shv.e[k].left; v[3] = shv.e[k].dev; v[4] = shv.e[k].ndev; v[5] = 1;
    }
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++)
        h = (h ^ (uint32_t)v[i]) * 16777619u;
    return h | 1;
}

static void ph_start(int num, int cam, uint32_t sig, int dev)
{
    ph.n = 0;
    WF_BARRIER();
    ph_sig = sig;
    ph.num = num;
    ph.cam = cam;
    ph.gen++;
    if (!sig)
        return;                                  /* nothing to show: the easel's sketch */
    if (cam)
        ph_r = &roll;
    else if (shelf_read(num, dev, &ph_shroll) == 0)
        ph_r = &ph_shroll;
    else {
        wf_log("dark: photos: roll %d (pass %d) not read back", num, dev);
        return;
    }
    int n = ph_r->nshot > WF_DK_SHEET ? WF_DK_SHEET : ph_r->nshot, f = wf_roll_next_dev(ph_r);
    if (n <= 0)
        return;
    memset(ph.st, 0, sizeof ph.st);
    ph.cw = n <= WF_PH_LMAX ? WF_PH_LW : WF_PH_CW;
    ph.ch = n <= WF_PH_LMAX ? WF_PH_LH : WF_PH_CH;
    ph.big_frame = f >= 0 && f < n ? f : 0;
    ph.big_st = WF_PHS_WAIT;
    ph.big_w = ph.big_h = 0;
    ph.cell = ph_mem;
    ph.big = ph_mem + PH_CELLS;
    ph_next = ph_nok = ph_nnone = 0;
    ph_t0 = get_ms_clock();
    WF_BARRIER();
    ph.n = n;
    ph.gen++;
}

struct ph_fh { void *f; uint32_t pos; };
static int ph_rd(void *ctx, uint32_t pos, void *dst, uint32_t n)   /* wf_photo.h's reader: FIO through ph_dma */
{
    struct ph_fh *h = ctx;
    uint8_t *d = dst;
    if (pos != h->pos) {
        if (FIO_SeekSkipFile(h->f, pos, 0) != (int64_t)pos)
            return -1;
        h->pos = pos;
    }
    while (n) {
        uint32_t k = n > PH_DMA ? PH_DMA : n;
        if (FIO_ReadFile(h->f, ph_dma, k) != (int)k)
            return -1;
        memcpy(d, ph_dma, k);
        d += k;
        n -= k;
        h->pos += k;
    }
    return 0;
}

/* frame i's cell (big: and the print sketch's picture), its state set last */
static void ph_frame(int i, int big)
{
    char p[96], root[16];
    uint32_t sz = 0;
    struct wf_ph_src s;
    struct ph_fh h = { 0, 0 };
    int ok = 0;
    if (wf_roll_cr2(ph_r, i, wf_dcim_dir(), wf_file_prefix(), "CR2", p, sizeof p) < 0 || FIO_GetFileSize(p, &sz) != 0
        || sz < 4096) {                          /* moved beside the prints by RAW files Move to ROLLS? */
        if (wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0
            || wf_roll_cr2_moved(ph_r->num, ph_r->file[i], root, wf_file_prefix(), p, sizeof p) < 0
            || FIO_GetFileSize(p, &sz) != 0 || sz < 4096)
            p[0] = 0;
    }
    if (p[0]) {
        h.f = FIO_OpenFile(p, 0);
        if (!WF_FH_OK(h.f))
            h.f = 0;
    }
    if (h.f && wf_ph_find(ph_rd, &h, (uint8_t *)ph_rows, &s) == 0) {
        ok = wf_ph_cell(ph_rd, &h, &s, &ph_t, ph.cw, ph.ch, ph_rows, ph_t16, ph.cell + i * ph.cw * ph.ch) == 0;
        int bw = 0, bh = 0;
        if (ok && big && wf_ph_big(ph_rd, &h, &s, &ph_t, ph_rows, ph_t16, ph.big, &bw, &bh) == 0) {
            ph.big_w = bw;
            ph.big_h = bh;
            WF_BARRIER();
            ph.big_st = WF_PHS_OK;
        }
    }
    if (h.f)
        FIO_CloseFile(h.f);
    if (big && ph.big_st != WF_PHS_OK)
        ph.big_st = WF_PHS_NONE;
    WF_BARRIER();
    ph.st[i] = ok ? WF_PHS_OK : WF_PHS_NONE;
    if (ok)
        ph_nok++;
    else
        ph_nnone++;
    ph.gen++;
}

static void ph_poll(int idle)
{
    ph_free_old();
    int now = get_ms_clock();
    if (!wf_dk_photo || ph_ask_num < 0 || now - ph_ask_t > PH_ASK_MS) {
        if (ph_mem)
            ph_drop();
        return;
    }
    if (!idle)
        return;
    int num = ph_ask_num, cam = ph_ask_cam, dev = 0;
    uint32_t sig = ph_sig_of(num, cam, &dev);
    if (sig && !ph_mem) {
        if (now - ph_fail_t < 10000)
            return;
        ph_mem = __mem_malloc(PH_MEM, 0, "wf_dark.c", __LINE__);
        ph_dma = __mem_malloc(PH_DMA, WF_MEM_DMA, "wf_dark.c", __LINE__);
        if (!ph_mem || !ph_dma) {
            if (ph_mem)
                __mem_free(ph_mem);
            if (ph_dma)
                __mem_free(ph_dma);
            ph_mem = ph_dma = 0;
            ph_fail_t = now;
            wf_log("dark: photos: no memory (%d KB), the sketch", (PH_MEM + PH_DMA) >> 10);
            return;
        }
        ph_t16 = (uint16_t *)(ph_mem + PH_CELLS + PH_BIG);
        ph_rows = ph_t16 + PH_BIG;
        ph_sig = 0;
    }
    if (sig != ph_sig || ph.num != num || ph.cam != cam) {
        ph_start(num, cam, sig, dev);
        return;
    }
    if (!sig || ph.n <= 0 || ph_next > ph.n)
        return;
    while (ph_next <= ph.n && get_ms_clock() - now < PH_BUDGET) {
        int k = ph_next++;
        if (!k)
            ph_frame(ph.big_frame, 1);
        else if (k - 1 != ph.big_frame)
            ph_frame(k - 1, 0);
    }
    if (ph_next > ph.n)
        wf_log("dark: photos %s %d: %d frames read, %d without, picture %dx%d, %dms", cam ? "camera roll" : "roll",
               ph_r->num, ph_nok, ph_nnone, ph.big_w, ph.big_h, get_ms_clock() - ph_t0);
}
#else
static struct wf_dark_photos ph_empty;
const struct wf_dark_photos *wf_dark_photos(int num, int cam) { (void)num; (void)cam; return &ph_empty; }
#endif
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
    dk_lgk = wf_rawb_logpk_build(t, scr, wf_dev_scratch_bytes(dkg.dw, DK_H), lk, &ms, &pi) == WF_LL_OK;
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
static int dk_pjp;                               /* this run's Camera JPEG (wf_pr_jpeg at its start, wf_dark.h WF_PJ_*):
                                                    which JPEG the shot's own IMG_nnnn.JPG in DCIM becomes */
static int dk_pcr;                               /* RAW after Darkroom (wf_pr_cr2): 1 = Move to ROLLS (dk_cr2_move) */

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

/* Screen On keep-alive (owner 11-17: on RAWB23 the EOS M's display flashed and went dark while a frame developed with
 * Screen On, and came back at the end; a frame takes ~185 s and dk_screen looks only at frame boundaries). Every
 * DK_KEEP_MS while a Screen On run lives, a display that reads off is turned on again (ML's display_on, as dk_screen).
 * Called from the Darkroom screen's tick (UI task, prio 0x1B, above wf_task's 0x1E: the worker's long steps do not hold
 * it back; ui = 1) and from the worker's band yields (the UI may be closed, half-shutter; ui = 0: only when the UI has
 * not kept it for DK_KEEP_UI_MS, so the two tasks do not both ask).
 * Stopping Canon from blanking it: Canon's auto power-off is prohibited for the run (powersave_prohibit at the start,
 * asked again here every DK_PROHIBIT_MS), and ML's idle countdowns do not blank a paused LiveView (liveview_display_idle
 * is 0, they reset themselves). idle_wakeup_reset_counters is NOT called: with ML's "turn display off" countdown at 0,
 * its reset makes ML's idle_display_on run ResumeLiveView, mid-run.
 * No redraw on a re-light: the bitmap keeps the Darkroom while dark (presented as usual), the UI rewrites our palette
 * on the tick the display reads on again (wf_ui.c), and the next redraw (1 s) is the usual double-buffered copy.
 * Each frame counts its re-lights and when it first read off (ms into the frame) for its screen line. Its own give-up,
 * as dk_screen's: an ask still off at the next look is a miss, DK_DISP_TRIES in a row and none again this run. */
#define DK_KEEP_MS 1000
#define DK_KEEP_UI_MS 3000                       /* the worker asks only when the UI has not kept it for this long */
#define DK_PROHIBIT_MS 30000
static volatile int dk_keep_ms, dk_keep_ui, dk_keep_pend, dk_keep_miss, dk_keep_said, dk_prohibit_ms;
static volatile int dk_relit, dk_off_first, dk_relit_run;   /* this frame's re-lights, its first off (ms into it, -1
                                                               none), the run's re-lights */
int wf_dark_keep(int now, int ui)
{
    if (!dk_running || !dk_dark_ok || wf_dk_scr != WF_DS_ON || ml_shutdown_requested || wf_unloading)
        return 0;
    if (ui)
        dk_keep_ui = now;
    else if (now - dk_keep_ui < DK_KEEP_UI_MS)
        return 0;
    if (now - dk_keep_ms < DK_KEEP_MS)
        return 0;
    dk_keep_ms = now;
    if (now - dk_prohibit_ms >= DK_PROHIBIT_MS) {
        dk_prohibit_ms = now;
        powersave_prohibit();
    }
    if (display_is_on()) {
        if (dk_keep_pend)
            dk_keep_miss = 0;                    /* the last ask took */
        dk_keep_pend = 0;
        return 0;
    }
    if (dk_off_first < 0)
        dk_off_first = now - dk_t0;
    if (dk_keep_pend) {                          /* asked a second ago and still off */
        dk_keep_pend = 0;
        dk_keep_miss++;
    }
    if (dk_keep_miss >= DK_DISP_TRIES) {
        if (!dk_keep_said) {
            dk_keep_said = 1;
            wf_log("dark: display off while F%02d runs: turned on %d times, it stays off (not asked again this run)",
                   dv.frame + 1, DK_DISP_TRIES);
        }
        return 0;
    }
    display_on();
    dk_keep_pend = 1;
    dk_relit++;
    dk_relit_run++;
    return 1;
}

static void dk_screen_line(int i)                /* Screen On: the frame's re-lights (its own line: the frame's is full) */
{
    if (wf_dk_scr != WF_DS_ON)
        return;
    if (dk_off_first >= 0)
        wf_log("dark: F%02d screen: relit %d, first off at %d ms", i + 1, dk_relit, dk_off_first);
    else
        wf_log("dark: F%02d screen: relit %d", i + 1, dk_relit);
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
#define DK_RAW_P    (dkg.pitch)                  /* the CR2's raw pitch (EOS M 5280 px x 14 bit, 5D2 5792 px): K is set for it */
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
 * a 16-row buffer + the logo's maps (the larger: the stamp is drawn first); with Overwrite (+ print) also the print
 * fitted to Canon's size (dk_fit: its band, row buffer, maps and the picture's scale buffers) */
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
        if (dk_pjp != WF_PJ_KEEP && wf_pf_fit(&L, dk_pfr, dk_pin, DK_W, DK_H, o, DK_W, DK_H)) {   /* dk_fit's */
            uint32_t k = WF_PF_MAXB + ((wf_pf_band_bytes(&L) + 3) & ~3u) + (uint32_t)DK_W * 2 * 16
                         + (dk_lgn ? DK_LG_B : 0) + wf_pf_scale_bytes(L.iw);
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

/* the biggest shoot block there is now, KB (taken and given back at once) */
static int dk_biggest(void)
{
    int big = 0;
    void *t = shoot_malloc_suite_contig(0);
    if (t) {
        void *c = GetFirstChunkFromSuite(t);
        big = c ? GetSizeOfMemoryChunk(c) >> 10 : 0;
        shoot_free_suite(t);
    }
    return big;
}

/* no memory: what was short, and the biggest shoot block there is now */
static void dk_short(uint32_t fin)
{
    int big = dk_biggest(), K = dk_split_rows(fin, DK_RAW_P);
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
    M.scr = __mem_malloc(wf_dev_scratch_bytes(dkg.dw, DK_H), 0, "wf_dark.c", __LINE__);
    if (!M.dma || !M.in || !M.ob || !M.app1 || !M.tabs || !M.scr) {
        wf_log("dark: no memory for the buffers / tables");
        return -1;
    }
    uint32_t fin = dk_fin_need();
#ifdef WF_BUILD_5D2
    /* the 5D2's first runs (DESIGN 3.18, a camera probe): the shoot memory left in one block after the look, before the
       run takes any, against what the frame wants (one block, or the split's head) */
    {
        int K = dk_split_rows(fin, DK_RAW_P);
        wf_log("dark: 5D2 memory: biggest shoot block %d KB (frame %d KB in one block, split head %d KB)", dk_biggest(),
               (int)(DK_FRAME_N >> 10), K > 0 ? (int)(((uint32_t)K * DK_FP) >> 10) : 0);
    }
#endif
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
    wf_dark_keep(get_ms_clock(), 0);             /* Screen On: kept on when the UI is closed */
}

/* ------------------------------------------------------------------ print finishes (DESIGN 3.9, wf_print.h) */
static const char *const dk_pfr_name[WF_PFR_N] = { "off", "white", "black", "white wide", "black wide" };
/* the print's fonts, logo, caption text and shot (dk_finish), kept for the camera's JPEG (dk_fit); static: wf_task's
   stack is small (the fonts alone 8 KB) */
static struct wf_pf_font dk_F[WF_PF_NFONT];
static struct wf_pf_logo dk_lg;
static struct wf_pf_txt dk_tx;
static struct wf_pf_lpos dk_lp;
static struct wf_dk_shot dk_s;
static char dk_rl[40];
static int dk_fok;

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
    struct wf_pf_font *F = dk_F;
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
    memset(&dk_lg, 0, sizeof dk_lg);
    uint32_t lo = fok ? (pn + 31) & ~31u : 0;
    if (dk_lgn && (dk_lgn > WF_PF_LOGO_MAXB || lo + dk_lgn > WF_PF_MAXB)) {
        if (!dk_lg_said)
            wf_log("dark: %s %d KB: no room beside the fonts (%d KB): no logo", DK_LG_PACK, (int)(dk_lgn >> 10),
                   (int)(lo >> 10));
        dk_lg_said = 1;
    } else if (dk_lgn) {
        void *g = FIO_OpenFile(DK_LG_PACK, 0);
        int ok = WF_FH_OK(g) && dk_read_at(g, 0, scr + lo, dk_lgn) == 0
                 && wf_pf_logo(&dk_lg, scr + lo, dk_lgn, s.model) == 0;
        if (WF_FH_OK(g))
            FIO_CloseFile(g);
        if (!ok) {
            memset(&dk_lg, 0, sizeof dk_lg);
            if (!dk_lg_said)
                wf_log("dark: %s bad (%d B): no logo", DK_LG_PACK, (int)dk_lgn);
            dk_lg_said = 1;
        }
    }
    struct wf_pf_cap c;
    char rl[40], dr[200];
    rl[0] = 0;
    if (dk_pin == WF_PIN_ROLL)
        snprintf(rl, sizeof rl, "ROLL %03d " WF_PF_DOT " %02d/%02d", dkr->num, i + 1, dkr->exp);
    wf_pf_caption(&s, look, rl, &dk_tx, &c);
    dk_s = s;                                    /* for the camera's JPEG (dk_fit) */
    memcpy(dk_rl, rl, sizeof dk_rl);
    dk_fok = fok;
    uint32_t mo = WF_PF_MAXB + nb + nr, ln = wf_pf_logo_place(L, fok ? F : 0, &dk_lg, &c, &dk_lp);
    if (dk_lp.n && ln > M.fin_n - mo) {          /* no room for the maps: the caption as before */
        wf_log("dark: F%02d logo needs %d KB of scratch, %d left: not drawn", i + 1, (int)(ln >> 10),
               (int)((M.fin_n - mo) >> 10));
        dk_lp.n = 0;
        c.rsv = c.roll3 = 0;
    }
    if (dk_lp.n)
        wf_pf_cap_logo(&dk_tx, &c, &dk_lg);      /* the model's words the logo says, out of line 1 */
    wf_pf_band(L, fok ? F : 0, &c, *band, dr, sizeof dr);
    int dims = al ? wf_dk_dims(M.app1 + ao, al, L->cw, L->ch) : -1;
    wf_log("dark: F%02d frame %s o%d %dx%d (Exif size %s), band %d KB, %dms", i + 1, dk_pfr_name[L->frame], orient, L->cw,
           L->ch, dims == 0 ? "set" : "not set", (int)(nb >> 10), get_ms_clock() - t1);
    if (fok)
        wf_log("dark: F%02d caption %s", i + 1, dr);
    if (dk_lp.n && wf_pf_logo_maps(L, &dk_lp, scr + mo, M.fin_n - mo) == 0)
        wf_log("dark: F%02d logo %s %dx%d%s%s, %d KB", i + 1, dk_lg.maker, dk_lp.w[0], dk_lp.h[0],
               dk_lp.n > 1 ? " + lettering " : "", dk_lp.n > 1 ? s.model : "", (int)(ln >> 10));
    else if (dk_lp.n)
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

/* the canvas L into path (a new file): Canon's APP1 (al bytes, none = JFIF), the picture's rows (rf(rctx, y): the
 * frame's, dk_row, or the fitted print's scaled picture) through L's band / logo, the darkroom screen's thumbnails
 * (what 0: the print) and the Exif thumbnail. w: the bytes and their hash (dk_verify).
 * DK_OK / DK_STOP / DK_FATAL (dk_why); a file not finished is removed */
static int dk_encode(int i, const char *path, const struct wf_pf_lay *L, uint8_t *app1, uint32_t al, const uint8_t *band,
                     uint32_t *rowbuf, int what, struct dk_wr *w, wf_pf_rowfn *rf, void *rctx)
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
        int pitch, n = wf_pf_band_rows_fn(L, y, rf, rctx, DK_FP, band, rowbuf, &rows, &pitch), py = y - L->iy;
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
            wf_dark_keep(get_ms_clock(), 0);
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

/* Camera JPEG Overwrite with a frame (owner 11-17, RAWB23: "the DCIM file has the film colour but no frame, date, logo
 * or caption"): the DCIM file is the whole print at Canon's 5184 x 3456. The frame is laid out again round the picture
 * scaled down until the canvas fits, centred, the margin in the frame colour (wf_pf_fit / wf_pf_pad: 4320 x 2880 in a
 * 4512 x 3456 frame landscape, 4560 x 3040 portrait, thin frame with the details); the caption band and the logo drawn
 * again at that size from the print's fonts, logo pack and text (dk_finish left them in the scratch's pack area, which
 * nothing after it touches), the print fonts at their own size, so the text reads as on the print; the picture (date
 * stamp in it) area-resampled from the frame (wf_pf_scale). The roll print is written and checked by now: its band,
 * row buffer and logo maps are free. 0 = P, *band, *rowbuf and dk_sc set; -1 = no fit / no room (logged: the picture
 * alone, as before) */
static struct wf_pf_scale dk_sc;
static int dk_fit(int i, const char *look, int orient, struct wf_pf_lay *P, uint8_t **band, uint32_t **rowbuf)
{
    uint8_t *scr = M.fin;
    if (!wf_pf_fit(P, dk_pfr, dk_pin, DK_W, DK_H, orient, DK_W, DK_H)) {
        wf_log("dark: F%02d camera JPEG: the print does not fit %dx%d: the picture alone", i + 1, DK_W, DK_H);
        return -1;
    }
    uint32_t nb = (wf_pf_band_bytes(P) + 3) & ~3u, mo = WF_PF_MAXB + nb + (uint32_t)DK_W * 2 * 16;
    uint32_t sn = wf_pf_scale_bytes(P->iw), ln = 0;
    if (!scr || mo + sn > M.fin_n) {
        wf_log("dark: F%02d camera JPEG: the fitted print needs %d KB of scratch, %d: the picture alone", i + 1,
               (int)((mo + sn) >> 10), (int)(M.fin_n >> 10));
        return -1;
    }
    struct wf_pf_cap c;
    wf_pf_caption(&dk_s, look, dk_rl, &dk_tx, &c);
    ln = (wf_pf_logo_place(P, dk_fok ? dk_F : 0, &dk_lg, &c, &dk_lp) + 3) & ~3u;
    if (dk_lp.n && mo + ln + sn > M.fin_n) {     /* no room for the maps: the caption without the logo */
        dk_lp.n = 0;
        c.rsv = c.roll3 = 0;
    }
    if (dk_lp.n)
        wf_pf_cap_logo(&dk_tx, &c, &dk_lg);
    *band = scr + WF_PF_MAXB;
    *rowbuf = (uint32_t *)(scr + WF_PF_MAXB + nb);
    char dr[200];
    wf_pf_band(P, dk_fok ? dk_F : 0, &c, *band, dr, sizeof dr);
    uint32_t so = mo;
    if (dk_lp.n && wf_pf_logo_maps(P, &dk_lp, scr + mo, ln) == 0)
        so = mo + ln;                            /* (the maps first, their resample tables after them: free again) */
    if (wf_pf_scale_init(&dk_sc, dk_row, 0, DK_W, DK_H, P->iw, P->ih, scr + so, M.fin_n - so) != 0) {
        wf_log("dark: F%02d camera JPEG: picture %dx%d not scaled: the picture alone", i + 1, P->iw, P->ih);
        return -1;
    }
    int pw = P->iw, ph = P->ih;
    wf_pf_pad(P, DK_W, DK_H);
    wf_log("dark: F%02d camera JPEG: the print fitted %dx%d, picture %dx%d at %d,%d%s", i + 1, DK_W, DK_H, pw, ph,
           P->ix, P->iy, P->nink ? ", logo" : "");
    if (dk_fok && P->tw)
        wf_log("dark: F%02d camera JPEG caption %s", i + 1, dr);
    return 0;
}

/* one DCIM file of Camera JPEG (owner 11-16, 11-17), only once the roll print (out, pw) reads back right: a temp file
 * next to dst (dst's name, .WFT), read back, then renamed onto dst (wf_frename: an older dst removed first). kind:
 * DK_CJ_COPY = the roll print's bytes (frame Off), DK_CJ_PIC = the picture alone at Canon's 5184 x 3456 (the date
 * stamp is in it), DK_CJ_FIT = the print fitted into 5184 x 3456 (dk_fit, a second encode: Canon's playback keeps the
 * size it made; no fit / no room: the picture alone, or with need_fit nothing). Both encodes set the Exif size back to
 * Canon's and carry their own Exif thumbnail. Never the CR2. nm names dst in the log, verb says what happened to it.
 * Returns DK_OK written, DK_ERR not (logged: whatever fails, dst stays as it was, or, renamed away, the .WFT holds it),
 * DK_STOP when stopped in the encode */
enum { DK_CJ_COPY, DK_CJ_PIC, DK_CJ_FIT };
static int dk_dcim_put(int i, const char *dst, const char *nm, const char *verb, int kind, int need_fit,
                       const char *out, const struct dk_wr *pw, uint8_t *app1, uint32_t al, const struct wf_pf_lay *L,
                       uint32_t *rowbuf, const char *look)
{
    int t0 = get_ms_clock(), n = 0, v = 0, fit = 0;
    char tmp[96];
    struct wf_fe e = { 0, 0 };
    uint32_t sz = 0;
    while (dst[n])
        n++;
    if (n < 5 || n >= (int)sizeof tmp || dst[n - 4] != '.') {
        wf_log("dark: F%02d camera JPEG: path %s", i + 1, dst);
        return DK_ERR;
    }
    memcpy(tmp, dst, (size_t)n - 3);
    memcpy(tmp + n - 3, "WFT", 4);
    if (FIO_GetFileSize(tmp, &sz) == 0 && wf_fremove(tmp, &e) != 0) {
        wf_log("dark: F%02d old %s not removed (%s %d): %s kept", i + 1, tmp, wf_fe_name(e.step), e.rc, nm);
        return DK_ERR;
    }
    struct dk_wr w;
    if (kind == DK_CJ_COPY) {                    /* the canvas is the picture: the print's bytes */
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
            wf_log("dark: F%02d camera JPEG: temp copy failed (%s): %s kept", i + 1,
                   !o ? wf_fe_name(e.step) : w.err == 2 ? "read" : "write", nm);
            wf_fremove(tmp, &e);
            return DK_ERR;
        }
        v = dk_verify(tmp, pw, app1, al);
    } else {                                     /* 5184 x 3456, the Exif size back to Canon's */
        struct wf_pf_lay P;
        uint8_t *band = 0;
        uint32_t *rb = rowbuf;
        fit = kind == DK_CJ_FIT && dk_fit(i, look, L->orient, &P, &band, &rb) == 0;
        if (!fit && need_fit) {                  /* the picture alone is the other file already */
            wf_log("dark: F%02d camera JPEG %s not written: the print did not fit", i + 1, nm);
            return DK_ERR;
        }
        if (!fit) {                              /* the picture alone */
            wf_pf_layout(&P, WF_PFR_OFF, 0, DK_W, DK_H, 1);
            band = 0;
            rb = rowbuf;
        }
        int dims = wf_dk_dims(app1, al, DK_W, DK_H);
        int rc = dk_encode(i, tmp, &P, app1, al, band, rb, 1, &w, fit ? wf_pf_scale_row : dk_row,
                           fit ? (void *)&dk_sc : 0);
        if (rc == DK_STOP) {
            wf_log("dark: F%02d camera JPEG %s kept (stopped)", i + 1, nm);
            return DK_STOP;
        }
        if (rc != DK_OK) {
            wf_log("dark: F%02d camera JPEG: %s encode failed (%s): %s kept", i + 1, fit ? "print" : "picture", dk_why,
                   nm);
            dk_why = "";
            return DK_ERR;
        }
        if (dims)
            wf_log("dark: F%02d camera JPEG: Exif size not set back", i + 1);
        v = dk_verify(tmp, &w, app1, al);
    }
    if (v) {
        wf_log("dark: F%02d camera JPEG: temp file did not read back (%d): %s kept", i + 1, v, nm);
        wf_fremove(tmp, &e);
        return DK_ERR;
    }
    int had = FIO_GetFileSize(dst, &sz) == 0;
    if (wf_frename(tmp, dst, &e) != 0) {
        int still = FIO_GetFileSize(dst, &sz) == 0;
        wf_log("dark: F%02d camera JPEG: rename failed (%s %d): %s", i + 1, wf_fe_name(e.step), e.rc,
               still ? "the file kept" : had ? "print left in the .WFT file" : "nothing written");
        if (still || !had)
            wf_fremove(tmp, &e);
        return DK_ERR;
    }
    char wh[24];
    snprintf(wh, sizeof wh, " %dx%d", DK_W, DK_H);
    wf_log("dark: F%02d camera JPEG %s %s: %s%s, %d KB, %dms", i + 1, nm, verb, kind == DK_CJ_COPY ? "the print" : fit
           ? "the print fitted" : "the picture", kind == DK_CJ_COPY ? "" : wh, (int)(w.n >> 10), get_ms_clock() - t0);
    return DK_OK;
}

/* Camera JPEG (owner 11-16: shoot fast, the darkroom's print goes over the camera's JPEG later, the ROLLS copy stays;
 * RAWB28, 11-20: one JPEG in DCIM, the one Canon's PLAY shows; the print is always the roll's copy in ROLLS, never
 * copied again). Overwrite: the shot's JPEG becomes the print (frame Off: its bytes; a frame: the print fitted into
 * 5184 x 3456). Over + print: with a frame the shot's JPEG becomes the picture alone at 5184 x 3456, nothing else
 * (no WFP_ file any more). Print + over: with a frame the shot's JPEG becomes the print fitted, and the picture alone
 * goes into the run's ROLLS folder under the shot's own name, ROLLS/ROLL021/IMG_0206.JPG beside F01_0206.JPG (and the
 * CR2 RAW files Move to ROLLS puts there). Frame Off: one file in every mode (the print is the picture). A JPEG
 * missing (RAW only) or without Canon's Exif: nothing. Returns DK_STOP when stopped, else DK_OK: the print is done.
 * *jok = 1 when every JPEG it makes is written and read back (RAW after Darkroom moves the CR2 only then,
 * dk_cr2_move). */
static int dk_camjpg(int i, const char *jpg, const char *out, const struct dk_wr *pw, uint8_t *app1, uint32_t al,
                     const struct wf_pf_lay *L, uint32_t *rowbuf, const char *look, int *jok)
{
    int t0 = get_ms_clock(), b = 0, n = 0;
    char tmp[96], d2[96];
    uint32_t sz = 0;
    *jok = 0;
    while (jpg[n])
        n++;
    for (b = n; b > 0 && jpg[b - 1] != '/';)
        b--;
    if (n < 5 || n >= (int)sizeof tmp || jpg[n - 4] != '.') {
        wf_log("dark: F%02d camera JPEG: path %s", i + 1, jpg);
        return DK_OK;
    }
    memcpy(tmp, jpg, (size_t)n - 3);
    memcpy(tmp + n - 3, "WFT", 4);
    if (FIO_GetFileSize(jpg, &sz) != 0) {        /* RAW only, or a run before renamed it away and stopped */
        wf_log("dark: F%02d camera JPEG %s not there: not overwritten%s", i + 1, jpg + b,
               FIO_GetFileSize(tmp, &sz) == 0 ? " (its .WFT holds a print)" : "");
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
    int pic = dk_pjp == WF_PJ_BOTH && L->frame, two = dk_pjp == WF_PJ_PRINT && L->frame;
    int r1 = dk_dcim_put(i, jpg, jpg + b, "overwritten", !L->frame ? DK_CJ_COPY : pic ? DK_CJ_PIC : DK_CJ_FIT, 0, out,
                         pw, app1, al, L, rowbuf, look);
    if (r1 == DK_STOP || !two) {
        *jok = r1 == DK_OK;
        return r1 == DK_STOP ? DK_STOP : DK_OK;
    }
    int o = 0, s1 = -1, s2 = -1;                 /* Print + over: the picture beside the roll print, the shot's name */
    for (; out[o]; o++)
        if (out[o] == '/') {
            s2 = s1;
            s1 = o;
        }
    if (s1 < 0 || s1 + 1 + n - b >= (int)sizeof d2) {
        wf_log("dark: F%02d camera JPEG: no ROLLS name for the picture of %s", i + 1, jpg + b);
        return DK_OK;
    }
    memcpy(d2, out, (size_t)s1 + 1);
    memcpy(d2 + s1 + 1, jpg + b, (size_t)(n - b) + 1);
    int r2 = dk_dcim_put(i, d2, d2 + s2 + 1, "written", DK_CJ_PIC, 0, out, pw, app1, al, L, rowbuf, look);
    if (r2 == DK_STOP)
        return DK_STOP;
    *jok = r1 == DK_OK && r2 == DK_OK;
    wf_log("dark: F%02d camera JPEG: the print in DCIM %s, the picture in ROLLS %s, %dms", i + 1,
           r1 == DK_OK ? "written" : "not written", r2 == DK_OK ? "written" : "not written", get_ms_clock() - t0);
    return DK_OK;
}

/* RAW after Darkroom "Move to ROLLS" (owner 11-17, DESIGN 3.9): once the frame's camera JPEG(s) are written and read
 * back (jok), its CR2 leaves DCIM for the roll's first ROLLS folder (mv, wf_roll_cr2_moved), bytes unchanged, so
 * Canon's PLAY finds only our JPEG; the shelf and develop again read it there. EOS M: a rename (its ML core's
 * FIO_RenameFile is Canon's own), ok when the source is gone and the target has its size; a rename that leaves the
 * source in place falls back to the copy. 5D2 (its ML core's FIO_RenameFile is a copy + remove that reads nothing
 * back): the copy. The copy: through M.dma into a new file, then both files read back in DK_RD chunks and compared byte
 * for byte (M.in holds the source's: the decoder's input, free after the decode), and only then the source removed;
 * whatever fails, the copy goes and the CR2 stays in DCIM. A target already there: the same bytes = the source
 * removed, else both kept. One log line a frame. */
static const char *dk_base(const char *p)       /* the file name of a path */
{
    const char *b = p;
    for (; *p; p++)
        if (*p == '/')
            b = p + 1;
    return b;
}
static int dk_neq(const uint8_t *a, const uint8_t *b, uint32_t n)   /* (no memcmp in the module's imports); words while
                                                                      both are aligned (M.in, M.dma: allocations) */
{
    uint32_t w = ((uintptr_t)a | (uintptr_t)b) & 3 ? 0 : n >> 2;
    const uint32_t *x = (const uint32_t *)(const void *)a, *y = (const uint32_t *)(const void *)b;
    for (uint32_t k = 0; k < w; k++)
        if (x[k] != y[k])
            return 1;
    for (uint32_t k = w << 2; k < n; k++)
        if (a[k] != b[k])
            return 1;
    return 0;
}
static int dk_cr2_same(const char *a, const char *b, uint32_t n)   /* 0 = n bytes the same; -1 open, -2 read, -3 not the
                                                                       same, -4 stopped */
{
    void *f = FIO_OpenFile(a, 0), *g = WF_FH_OK(f) ? FIO_OpenFile(b, 0) : 0;
    int rc = WF_FH_OK(f) && WF_FH_OK(g) ? 0 : -1;
    for (uint32_t at = 0; !rc && at < n; at += DK_RD) {
        uint32_t k = n - at > DK_RD ? DK_RD : n - at;
        if (FIO_ReadFile(f, M.dma, k) != (int)k)
            rc = -2;
        else {
            memcpy(M.in, M.dma, k);              /* (M.in: DK_IN = DK_RD bytes) */
            rc = FIO_ReadFile(g, M.dma, k) != (int)k ? -2 : dk_neq(M.in, M.dma, k) ? -3 : dk_should_stop() ? -4 : 0;
        }
        if (!rc)
            dk_yield();
    }
    if (WF_FH_OK(g))
        FIO_CloseFile(g);
    if (WF_FH_OK(f))
        FIO_CloseFile(f);
    return rc;
}
static const char *const dk_cp_why[] = { "", "open", "create", "read", "write", "size", "read back", "not the same",
                                         "stopped" };
static int dk_cr2_copy(const char *src, const char *dst, uint32_t n)   /* 0 = copied and read back, else dk_cp_why's */
{
    struct wf_fe e = { 0, 0 };
    uint32_t sz = 0;
    void *f = FIO_OpenFile(src, 0), *o = WF_FH_OK(f) ? wf_fopen_w(dst, WF_FO_NEW, 0, &e) : 0;
    int rc = !WF_FH_OK(f) ? 1 : !o ? 2 : 0;
    for (uint32_t at = 0; !rc && at < n; at += DK_RD) {
        uint32_t k = n - at > DK_RD ? DK_RD : n - at;
        rc = FIO_ReadFile(f, M.dma, k) != (int)k ? 3 : FIO_WriteFile(o, M.dma, k) != (int)k ? 4 : dk_should_stop() ? 8 : 0;
        if (!rc)
            dk_yield();
    }
    if (o)
        FIO_CloseFile(o);
    if (WF_FH_OK(f))
        FIO_CloseFile(f);
    if (!rc && (FIO_GetFileSize(dst, &sz) != 0 || sz != n))
        rc = 5;
    int s = rc ? 0 : dk_cr2_same(src, dst, n);
    return rc ? rc : s == -3 ? 7 : s == -4 ? 8 : s ? 6 : 0;
}
static void dk_cr2_move(int i, const char *cr2, const char *mv, const char *rn, int jok)
{
    int t0 = get_ms_clock(), renfail = 0, dl = mv ? (int)(dk_base(mv) - mv) - 1 : -1;
    const char *nm = dk_base(cr2);
    struct wf_fe e = { 0, 0 };
    uint32_t sz = 0, dz = 0;
    char dir[96];
    if (!jok) {
        wf_log("dark: F%02d %s kept in DCIM: its JPEG was not written", i + 1, nm);
        return;
    }
    if (dl <= 0 || dl >= (int)sizeof dir || FIO_GetFileSize(cr2, &sz) != 0) {
        wf_log("dark: F%02d %s kept in DCIM: %s", i + 1, nm, dl <= 0 || dl >= (int)sizeof dir ? "no ROLLS name"
               : "not there");
        return;
    }
    memcpy(dir, mv, (size_t)dl);
    dir[dl] = 0;
    if (wf_fmkdir(dir, &e) != 0) {
        wf_log("dark: F%02d %s kept in DCIM: folder ROLLS/%s not made (%s %d)", i + 1, nm, rn, wf_fe_name(e.step), e.rc);
        return;
    }
    if (FIO_GetFileSize(mv, &dz) == 0) {         /* that name there already: a run stopped after its copy, or the same
                                                    number from another DCIM folder */
        int same = dz == sz && dk_cr2_same(cr2, mv, sz) == 0;
        if (same && wf_fremove(cr2, &e) == 0)
            wf_log("dark: F%02d %s moved to ROLLS/%s/ (the same file was there, %d KB)", i + 1, nm, rn, (int)(sz >> 10));
        else
            wf_log("dark: F%02d %s kept in DCIM: ROLLS/%s/ has %s file of that name", i + 1, nm, rn,
                   same ? "the same (not removed from DCIM)" : "another");
        return;
    }
#ifndef WF_BUILD_5D2
    if (wf_frename(cr2, mv, &e) == 0) {
        if (FIO_GetFileSize(mv, &dz) == 0 && dz == sz)
            wf_log("dark: F%02d %s moved to ROLLS/%s/ (renamed, %d KB, %dms)", i + 1, nm, rn, (int)(sz >> 10),
                   get_ms_clock() - t0);
        else
            wf_log("dark: F%02d %s renamed to ROLLS/%s/ but it reads %d bytes, not %d", i + 1, nm, rn, (int)dz, (int)sz);
        return;
    }
    if (FIO_GetFileSize(cr2, &dz) != 0) {        /* gone from DCIM and not in ROLLS: nothing left to copy */
        wf_log("dark: F%02d %s rename to ROLLS/%s/ failed (%s %d) with the file gone from DCIM", i + 1, nm, rn,
               wf_fe_name(e.step), e.rc);
        return;
    }
    renfail = 1;                                 /* still in DCIM: the copy */
#endif
    int rc = dk_cr2_copy(cr2, mv, sz);
    if (rc) {
        if (FIO_GetFileSize(mv, &dz) == 0)
            wf_fremove(mv, &e);
        wf_log("dark: F%02d %s kept in DCIM: the copy to ROLLS/%s/ failed (%s%s)", i + 1, nm, rn, dk_cp_why[rc],
               renfail ? ", after the rename failed" : "");
        return;
    }
    if (wf_fremove(cr2, &e) != 0) {
        wf_log("dark: F%02d %s copied to ROLLS/%s/ but not removed from DCIM (%s %d): both kept", i + 1, nm, rn,
               wf_fe_name(e.step), e.rc);
        return;
    }
    wf_log("dark: F%02d %s moved to ROLLS/%s/ (%scopied and read back, %d KB, %dms)", i + 1, nm, rn,
           renfail ? "rename failed: " : "", (int)(sz >> 10), get_ms_clock() - t0);
}

static int dk_frame(int i, const struct wf_look *lk, int src, int *bytes)
{
    char cr2[96], jpg[96], out[96], root[16], txt[200], t2[200], mv[96], rn[12];
    int t = get_ms_clock(), rc;
    dk_why = "";
    if (wf_roll_cr2(dkr, i, wf_dcim_dir(), wf_file_prefix(), "CR2", cr2, sizeof cr2) < 0
        || wf_roll_cr2(dkr, i, wf_dcim_dir(), wf_file_prefix(), "JPG", jpg, sizeof jpg) < 0
        || wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_out(dkr, i, root, out, sizeof out) < 0) {
        dk_why = "path";
        return DK_ERR;
    }
    int mvk = wf_roll_cr2_moved(dkr->num, dkr->file[i], root, wf_file_prefix(), mv, sizeof mv);   /* RAW after Darkroom */
    wf_roll_name(dkr->num, 1, rn);
    /* 1. load: head, the lossless JPEG header, the decode */
    dk_phase(WF_DP_LOAD, 0);
    dv.print_ready = 0;
    memset(dv_print, 0, sizeof dv_print);
    uint32_t fsize = 0;
    const char *src_cr2 = cr2;
    if (FIO_GetFileSize(cr2, &fsize) != 0 || fsize < 4096) {   /* moved beside the prints by RAW after Darkroom? */
        if (mvk < 0 || FIO_GetFileSize(mv, &fsize) != 0 || fsize < 4096) { dk_why = "no CR2"; return DK_ERR; }
        src_cr2 = mv;
        wf_log("dark: F%02d %s read from ROLLS/%s/", i + 1, dk_base(mv), rn);
    }
    void *f = FIO_OpenFile(src_cr2, 0);
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
    if (rc == 0 && (dk_inf.w != dkg.sw || dk_inf.h != dkg.sh || dk_inf.pitch != dkg.pitch || DK_IN < wf_cr2_inbuf_min(&dk_inf)
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
    j.ax0 = dkg.ax0; j.ay0 = dkg.ay0; j.ax1 = dk_inf.w; j.ay1 = dk_inf.h;   /* image data from (EOS M 72, 52; 5D2 152, 52) */
    j.x0 = DK_X0 - dkg.dx; j.y0 = DK_Y0; j.w = dkg.dw; j.h = DK_H; j.cfa = dkg.cfa;   /* 5D2: 5632 wide, the print 16 in */
    int lb = 0, lw = 0;
    wf_dev_levels(M.scr, &j, dkg.bx0, dkg.bx1, &lb, &lw);
    j.black = lb ? lb : dkg.black;
    j.white = lw ? lw : wf_dk_white(DK_CAM, dk_inf.iso);
    const int *wbl;                              /* Light Auto: Canon's Auto WB of the shot (owner 11-17), else as shot */
    int wk = wf_dk_wb_pick(dk_inf.wb, dk_inf.wb_auto, lk->light == WF_LIGHT_AUTO, &wbl);
    if (wk == WF_DKWB_NOAUTO && !dk_wa_said) {
        dk_wa_said = 1;
        wf_log("dark: IMG_%04d: no Auto WB levels in the CR2 (ColorData %d): the as-shot WB (said once a run)",
               dkr->file[i], dk_inf.cd_count);
    }
    int as = wf_dk_scales(DK_CAM, wbl, j.black, j.white, dk_inf.iso, j.wb_q8);
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
            if (dkg.dw == DK_W)
                wf_dev_rows(M.scr, ya, ye, d, DK_FP);
            else                                 /* 5D2: Canon's 5616 columns of the 5632 developed */
                wf_dev_rows_win(M.scr, ya, ye, d, DK_FP, dkg.dx, DK_W);
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
    if (dkr->dev >= 2)                           /* a develop again (owner 11-17): its pass; the film is in body */
        snprintf(txt, sizeof txt, "WindyFilm Darkroom: %s | ROLL %03d F%02d | develop %d", body, dkr->num, i + 1, dkr->dev);
    else
        snprintf(txt, sizeof txt, "WindyFilm Darkroom: %s | ROLL %03d F%02d", body, dkr->num, i + 1);
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
    rc = dk_encode(i, out, &L, al ? M.app1 + ao : 0, al, band, rowbuf, 0, &wr, dk_row, 0);
    if (rc != DK_OK)
        return rc;
    if (dk_pjp != WF_PJ_KEEP) {                  /* stopped in it: the print is done, the run stops at the next frame */
        int jok = 0;
        dk_camjpg(i, jpg, out, &wr, M.app1 + ao, al, &L, rowbuf, t2, &jok);
        if (dk_pcr && src_cr2 == cr2)            /* RAW after Darkroom Move to ROLLS: once the JPEG(s) are in DCIM */
            dk_cr2_move(i, cr2, mvk < 0 ? 0 : mv, rn, jok);
    }
    dk_t[3] = get_ms_clock() - t;
    wf_dev_note(lk, WF_PATH_DARK, dk_t[0] + dk_t[1] + dk_t[2] + dk_t[3]);   /* the whole print, dry included (11-17) */
    *bytes = (int)wr.n;
    dv.print_ready = 2;
    wf_log("dark: F%02d IMG_%04d iso %d: load %d expose %d fix %d dry %d ms, %d KB, wb %s, trim %d, black %d white %d%s, "
           "shoulder %s %dms, look %s", i + 1, dkr->file[i], dk_inf.iso, dk_t[0], dk_t[1], dk_t[2], dk_t[3],
           (int)(wr.n >> 10), !as ? "daylight" : wk == WF_DKWB_AUTO ? "auto" : "as-shot", wf_dk_trim10(DK_CAM, dk_inf.iso),
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

static volatile int dk_rnum, dk_ragain, dk_rfilm;   /* wf_dark_start_roll's ask: a shelf roll, develop again, film */
static int dk_film_prev;                          /* the film loaded before a shelf roll's own (put back after the run) */

/* a run's roll file saved: the camera's (WFROLL.TXT), or a shelf roll's copy in its ROLLS folder */
static int dk_save(void) { return dkr == &roll ? roll_save() : shelf_put(dkr, 0); }

/* a frame of a shelf roll still to develop: its CR2 on the card, in DCIM or moved beside the prints by RAW after
   Darkroom (ROLLS/ROLLnnn/)? (in neither: skipped, logged) */
static int dk_has_cr2(void *ctx, int folder, int file)
{
    const struct wf_roll *r = ctx;
    char root[16], mv[96];
    uint32_t sz = 0;
    if (roll_has_cr2(0, folder, file)
        || (wf_roll_root(wf_dcim_dir(), root, sizeof root) >= 0
            && wf_roll_cr2_moved(r->num, file, root, wf_file_prefix(), mv, sizeof mv) > 0
            && FIO_GetFileSize(mv, &sz) == 0 && sz > 0))
        return 1;
    int i = 0;
    while (i < r->nshot - 1 && !(r->folder[i] == folder && r->file[i] == file))
        i++;
    wf_log("dark: roll %03d F%02d %d/IMG_%04d.CR2 is gone: skipped", r->num, i + 1, folder, file);
    return 0;
}

/* a shelf roll for this run (dk_rnum): its newest pass read back from the card, made ready (rewound, a gone CR2 kept
 * out), or its next pass made for a develop again (owner 11-17) with its own folder and roll file; its film loaded
 * (the film before goes back after the run). "" = ready, else why not */
static const char *dk_shelf_begin(void)
{
    const struct wf_shelf_ent *e = 0;
    char nm[12];
    for (int k = 0; k < shv.n; k++)
        if (shv.e[k].num == dk_rnum)
            e = &shv.e[k];
    if (!e || shelf_read(e->num, e->dev, &sh_roll) != 0) {
        wf_log("dark: roll %03d: its roll file is not on the card (%s)", dk_rnum, e ? "unreadable" : "not on the shelf");
        return "The roll's file is not on the card";
    }
    int n;
    if (dk_ragain) {
        int film = dk_rfilm && wf_film_by_id(dk_rfilm) ? dk_rfilm : sh_roll.own ? sh_roll.own : sh_roll.film;
        if ((n = wf_roll_again(&sh_roll, film, dk_has_cr2, &sh_roll)) <= 0) {
            wf_log("dark: roll %03d develop again: %s", sh_roll.num, n < 0 ? "the last pass is made" : "no CR2 left");
            return n < 0 ? "No more develops of this roll" : "Its CR2 files are gone";
        }
        const struct wf_film *f = wf_film_by_id(sh_roll.film);
        wf_log("dark: roll %03d develop %d: film %s, %d frame%s, prints in ROLLS/%s", sh_roll.num, sh_roll.dev,
               f ? f->name : "?", n, n == 1 ? "" : "s", shelf_nm(&sh_roll, nm));
    } else {
        n = wf_roll_shelf_prep(&sh_roll, dk_has_cr2, &sh_roll);
        const struct wf_film *f = wf_film_by_id(sh_roll.film);
        wf_log("dark: roll %03d from the shelf: film %s, %d to develop, prints in ROLLS/%s", sh_roll.num,
               f ? f->name : "?", n, shelf_nm(&sh_roll, nm));
        if (n <= 0)
            return "Its CR2 files are gone";
    }
    if (shelf_put(&sh_roll, 0) != 0)
        return "Cannot write the roll file";
    dkr = &sh_roll;
    int was = wf_loaded();
    if (sh_roll.film != was) {
        int rc = wf_load(sh_roll.film);
        wf_log("dark: film %d for roll %03d (load %d), film %d back after the run", sh_roll.film, sh_roll.num, rc, was);
        if (rc != WF_LOAD_OK)
            return "The film's table is not on the card";
        dk_film_prev = was;
    }
    return "";
}

/* the run, inside wf_task (wf_dark_poll) */
static void dk_task(void)
{
    int t_run = get_ms_clock(), ndone = 0, nerr = 0, paused = 0, ms_sum = 0;
    dk_stopk = 0;
    ph_drop();                                   /* the photos' memory back for the run */
    ph_free_old();
    const char *end_why = "";
    dkr = &roll;
    dk_film_prev = 0;
    if (dk_rnum) {                               /* a shelf roll (owner 11-17) */
        const char *why = dk_shelf_begin();
        if (why[0]) {
            if (dk_film_prev > 0)
                wf_load(dk_film_prev);
            dkr = &roll;
            dv.frame = -1;
            dv.phase = WF_DP_NONE;
            dv.msg = why;
            dv.state = WF_DR_FAIL;
            wf_log("dark: roll %03d not developed: %s", dk_rnum, why);
            sh_scan = 1;
            roll_changed();
            dv_bump();
            WF_BARRIER();
            dk_running = 0;
            return;
        }
    }
    dv.num = dkr->num;
    dv.dev = dkr->dev < 2 ? 1 : dkr->dev;
    dv.film = dkr->film;
    int st0 = dkr->state;
    /* a full roll too (RAWB17 and before: a FULL roll stayed FULL, wf_roll_next_dev found nothing, "0 developed, 0 failed,
       3 left" with no reason once the memory was there) */
    if ((dkr->state == WF_RS_LOADED && dkr->nshot > 0) || dkr->state == WF_RS_FULL) {
        wf_roll_rewind(dkr);
        dk_save();
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
    dv.total = dkr->nshot;
    dv.done = wf_roll_ndev(dkr);
    dv.left = wf_roll_left(dkr);
    dk_pfr = wf_pr_frame > WF_PFR_OFF && wf_pr_frame < WF_PFR_N ? wf_pr_frame : WF_PFR_OFF;   /* the whole run's */
    dk_pin = wf_pr_info > WF_PIN_OFF && wf_pr_info < WF_PIN_N ? wf_pr_info : WF_PIN_OFF;
    dk_pdt = wf_pr_date > WF_PDT_OFF && wf_pr_date < WF_PDT_N ? wf_pr_date : WF_PDT_OFF;
    dk_pjp = wf_pr_jpeg > WF_PJ_KEEP && wf_pr_jpeg < WF_PJ_N ? wf_pr_jpeg : WF_PJ_KEEP;
    dk_pcr = wf_pr_cr2 == WF_PC_MOVE;
    dk_pf_said = dk_lg_said = dk_wa_said = 0;
    dk_lgn = 0;
    if (dk_pfr && (FIO_GetFileSize(DK_LG_PACK, &dk_lgn) != 0 || dk_lgn < 40))
        dk_lgn = 0;                              /* no WFLOGO.DAT: the caption as text alone */
    wf_log("dark: roll %03d start, %d of %d to develop, look output %d, print frame %d info %d date %d jpeg %d logo %dK",
           dkr->num, dv.left, dv.total, lk.output, dk_pfr, dk_pin, dk_pdt, dk_pjp, (int)(dk_lgn >> 10));
    if (dk_pjp == WF_PJ_BOTH || dk_pjp == WF_PJ_PRINT)
        wf_log("dark: Camera JPEG %s: %s", dk_pjp == WF_PJ_BOTH ? "Over + print" : "Print + over",
               !dk_pfr ? "frame Off, so as Overwrite (one file)" : dk_pjp == WF_PJ_BOTH
               ? "the picture over IMG_nnnn.JPG, the print in ROLLS only"
               : "the print over IMG_nnnn.JPG, the picture as ROLLS/ROLLnnn/IMG_nnnn.JPG");   /* < WF_LOG_W */
    if (dk_pcr && dk_pjp == WF_PJ_KEEP)
        wf_log("dark: RAW after Darkroom Move to ROLLS, but Camera JPEG is Keep: the CR2s stay in DCIM");
    else if (dk_pcr) {
        char rn[12];
        wf_roll_name(dkr->num, 1, rn);
        wf_log("dark: RAW after Darkroom Move to ROLLS: each CR2 goes to ROLLS/%s/ once its frame's JPEG is in DCIM", rn);
    }
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
    dk_relit = dk_relit_run = dk_keep_said = dk_keep_pend = dk_keep_miss = 0;
    dk_off_first = -1;
    dk_keep_ms = dk_prohibit_ms = get_ms_clock();   /* (the first look a second in: dk_screen asks first) */
    dk_keep_ui = dk_keep_ms - DK_KEEP_UI_MS;     /* the UI has not kept it yet */
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
            if (got == WF_LLB_HIT && (M.tabs->src != src || M.tabs->curve != curve || M.tabs->cam != DK_DEV
                                      || M.tabs->n < 2 || M.tabs->n > 65))
                got = WF_LLB_BAD;
        }
        if (got != WF_LLB_HIT && rc == WF_LL_OK && src == WF_DEV_DISP)   /* LUT EI by hand, H / S on the cube's output (as B) */
            rc = wf_dev_build_disp(M.tabs, DK_DEV, cube, lk.lei, lk.hl, lk.sh, &li);
        else if (got != WF_LLB_HIT && rc == WF_LL_OK)
            rc = wf_dev_build(M.tabs, src, curve, DK_DEV, cube,
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
        if (wf_roll_root(wf_dcim_dir(), root, sizeof root) < 0 || wf_roll_dir(dkr, root, 0, d, sizeof d) < 0
            || wf_fmkdir(d, &e) != 0 || wf_roll_dir(dkr, root, 1, d, sizeof d) < 0 || wf_fmkdir(d, &e) != 0) {
            wf_log("dark: folder %s: %s %d", d, wf_fe_name(e.step), e.rc);
            end_why = "cannot make the ROLLS folder";
        }
    }
    while (!end_why[0]) {
        int i = wf_roll_next_dev(dkr);
        if (i < 0) {
            if (wf_roll_left(dkr) > 0) {       /* frames left that the roll does not hand out: say why, not a blank end */
                wf_log("dark: roll state %d (at start %d), %d left: no frame to develop", dkr->state, st0,
                       wf_roll_left(dkr));
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
        dv.left = wf_roll_left(dkr);
        dk_relit = 0;                            /* Screen On: this frame's re-lights (wf_dark_keep), its first off */
        dk_off_first = -1;
        dk_t0 = get_ms_clock();
        int bytes = 0, r = dk_frame(i, &lk, src, &bytes);
        dk_screen_line(i);
        if (r == DK_STOP) {
            end_why = dk_why;
            break;
        }
        if (r == DK_OK) {
            wf_roll_mark(dkr, i, WF_RF_DEV);
            ndone++;
            ms_sum += get_ms_clock() - dk_t0;
            dv.avg_ms = ms_sum / ndone;
            dk_hang(i);
        } else {
            wf_log("dark: F%02d IMG_%04d failed: %s", i + 1, dkr->file[i], dk_why);
            if (r == DK_FATAL) {
                end_why = dk_why;
                break;
            }
            wf_roll_mark(dkr, i, WF_RF_ERR);
            nerr++;
        }
        if (dk_save() != 0)
            wf_log("dark: roll file not saved");
        dv.done = wf_roll_ndev(dkr);
        dv.left = wf_roll_left(dkr);
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
    if (dk_relit_run)
        wf_log("dark: Screen On: display found off while frames ran, turned on again %d times", dk_relit_run);
    int left = wf_roll_left(dkr);
    dv.frame = -1;
    dv.phase = WF_DP_NONE;
    dv.left = left;
    dv.msg = end_why;
    dv.state = !left ? WF_DR_DONE : dk_stopk || !end_why[0] ? WF_DR_STOPPED : WF_DR_FAIL;
    wf_log("dark: roll %03d end: %d developed, %d failed, %d left, %d s%s%s", dkr->num, ndone, nerr, left,
           (get_ms_clock() - t_run) / 1000, end_why[0] ? ": " : "", end_why);
    if (dkr == &roll && roll.state == WF_RS_DONE)   /* the camera's roll developed: on the shelf (develop it again) */
        shelf_put(&roll, "developed");
    if (dk_film_prev > 0) {                      /* a shelf roll's own film: the one before back */
        int rc = wf_load(dk_film_prev);
        wf_log("dark: film %d back (load %d)", dk_film_prev, rc);
        dk_film_prev = 0;
    }
    sh_scan = 1;
    roll_changed();
    dv_bump();
    WF_BARRIER();
    dk_running = 0;
}

#else
int wf_dark_start(void) { dv.msg = "The Darkroom runs on the EOS M first"; dv_bump(); return -3; }
int wf_dark_start_roll(int num, int again, int film) { (void)num; (void)again; (void)film; return wf_dark_start(); }
void wf_dark_stop(void) {}
void wf_dark_poll(void) {}
int wf_dark_wake(int now) { (void)now; return 0; }
int wf_dark_dark(int now) { (void)now; return 0; }
int wf_dark_keep(int now, int ui) { (void)now; (void)ui; return 0; }
#endif

#if WF_DARK_CAN
static int dk_start(int num, int again, int film)
{
    if (dk_running)
        return -2;
    if (!wf_cam_ok || wf_unloading || wf_job_state() || is_movie_mode() || req_kind || wf_rawb_busy())
        return -3;                               /* (a shot's B develop still runs: 30 s) */
    if (!wf_worker_up()) {                       /* a boot's first seconds (wf_main.c tries it again): the look stage */
        dv.msg = "Starting: try again in a moment";   /* runs on the worker */
        dv_bump();
        return -3;
    }
    int n = roll.state == WF_RS_LOADED ? roll.nshot : wf_roll_left(&roll);
    if (num) {                                   /* a shelf roll: its frames left, or all of them again */
        n = 0;
        for (int k = 0; k < shv.n; k++)
            if (shv.e[k].num == num)
                n = again ? shv.e[k].nshot : shv.e[k].left;
    }
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
    dk_rnum = num;
    dk_ragain = again && num;
    dk_rfilm = film;
    WF_BARRIER();
    dk_running = 1;                              /* the roll is the run's from here (the shutter goes free) */
    dk_go = 1;                                   /* wf_task's next loop runs it: no task to create, none to be refused */
    dv_bump();
    return 0;
}

int wf_dark_start(void) { return dk_start(0, 0, 0); }
int wf_dark_start_roll(int num, int again, int film) { return dk_start(num, again, film); }

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
#if WF_DARK_CAN
    ph_drop();
    ph_free_old();
#endif
    roll_shutter(1);                             /* the shutter never stays held without the module */
    if (hud_fonts_on)
        wfdr_hud_fonts(0);
}
