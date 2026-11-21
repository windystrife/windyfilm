/* host_ml.c - stub Magic Lantern for the src/ui host harness (see host_ml.h). */
#include "host_ml.h"
#undef malloc
#undef free

/* ------------------------------------------------------------------ memory */
static long live, peak, cur;
struct hdr { size_t n; size_t pad; };
void *host_malloc(size_t n)
{
    struct hdr *h = malloc(sizeof(*h) + n);
    if (!h) return 0;
    h->n = n;
    live++;
    cur += n;
    if (cur > peak) peak = cur;
    memset(h + 1, 0xA5, n);                 /* uninitialised memory is not zero on the camera either */
    return h + 1;
}
void host_free(void *p)
{
    if (!p) return;
    struct hdr *h = (struct hdr *)p - 1;
    live--;
    cur -= h->n;
    free(h);
}
long host_live_allocs(void) { return live; }
long host_peak_bytes(void) { return peak; }

/* ------------------------------------------------------------------ bitmap: the 720 x 480 area inside 960 x 540 */
#define OFF (30 * BMPPITCH + 120)
static uint8_t idle_raw[BMPPITCH * 540], real_raw[BMPPITCH * 540];
uint8_t *bmp_vram_idle(void) { return idle_raw + OFF; }
uint8_t *bmp_vram_real(void) { return real_raw + OFF; }
void bmp_idle_copy(int direction, int fullsize)
{
    (void)fullsize;
    for (int i = 0; i < 480; i++)
    {
        if (direction) memcpy(real_raw + OFF + i * BMPPITCH, idle_raw + OFF + i * BMPPITCH, 720);
        else memcpy(idle_raw + OFF + i * BMPPITCH, real_raw + OFF + i * BMPPITCH, 720);
    }
}
static int lockd;
void host_lock(int d) { lockd += d; }
int host_lock_depth(void) { return lockd; }

/* ------------------------------------------------------------------ palette: two register banks, Canon's table */
#define REG_A 0xC0F14400u
#define REG_B 0xC0FD4400u
uint32_t LCD_Palette[3 * 256 + 0x300];
static uint32_t hw_a[256], hw_b[256];
void EngDrvOut(uint32_t reg, uint32_t v)
{
    if (reg >= REG_A && reg < REG_A + 1024) hw_a[(reg - REG_A) / 4] = v;
    else if (reg >= REG_B && reg < REG_B + 1024) hw_b[(reg - REG_B) / 4] = v;
    else fprintf(stderr, "EngDrvOut: unexpected register %08x\n", reg);
}
uint32_t shamem_read(uint32_t reg)
{
    if (reg >= REG_A && reg < REG_A + 1024) return hw_a[(reg - REG_A) / 4];
    if (reg >= REG_B && reg < REG_B + 1024) return hw_b[(reg - REG_B) / 4];
    return 0;
}

/* ------------------------------------------------------------------ RBF fonts */
struct font font_dynamic[MAX_DYN_FONTS + 1];
static const char *rbfdir = ".";
static const char *slot_name[MAX_DYN_FONTS] = { "term12", "term20", "argnor23", "argnor28", "argnor32", 0, 0 };

static font *rbf_load(const char *name)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s.rbf", rbfdir, name);
    FILE *fp = fopen(p, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc(n);
    if (fread(d, 1, n, fp) != (size_t)n) { fclose(fp); free(d); return 0; }
    fclose(fp);
    font *f = calloc(1, sizeof(*f));
    memcpy(&f->hdr, d, sizeof(f->hdr));
    f->charCount = f->hdr.charLast - f->hdr.charFirst + 1;
    f->width = 8 * f->hdr.charSize / f->hdr.height;
    memcpy(&f->wTable[f->hdr.charFirst], d + f->hdr._wmapAddr, f->charCount);
    f->cTable = malloc(f->charCount * f->hdr.charSize);
    memcpy(f->cTable, d + f->hdr._cmapAddr, f->charCount * f->hdr.charSize);
    free(d);
    return f;
}

static const char *font_hidden;
void host_hide_font(const char *name) { font_hidden = name; }

uint32_t font_by_name(char *file, uint32_t fg, uint32_t bg)
{
    if (font_hidden && !strcmp(font_hidden, file)) return (7u << 16) | (bg << 8) | fg;
    for (int i = 0; i < MAX_DYN_FONTS; i++)
        if (slot_name[i] && !strcmp(slot_name[i], file) && font_dynamic[i].bitmap)
            return (i << 16) | (bg << 8) | fg;
    return (7u << 16) | (bg << 8) | fg;     /* FONT_CANON: not an RBF font */
}

/* ------------------------------------------------------------------ GUI, tasks, time */
static int front_disabled, clock_ms = 1000;
int gui_state, lv = 1;
/* the ML menu (host_menu): open / closing at a clock time / gui_stop_menu ignored; what happens under it is counted */
#define MENU_PX 0x2A                    /* a pixel of the ML menu (ML's own colour range) at the front's top-left */
static int menu_on, menu_off_at = -1, menu_sticky, under_enable, under_disable, menu_px_kept = -1, redraws;
int ml_shutdown_requested;
void canon_gui_disable_front_buffer(void) { if (menu_on) under_disable++; front_disabled = 1; }
void canon_gui_enable_front_buffer(int also_redraw) { (void)also_redraw; if (menu_on) under_enable++; front_disabled = 0; }
void redraw(void) { redraws++; }
static int ntasks, task_fail;
uint32_t task_create(const char *name, uint32_t prio, uint32_t stack, void *entry, void *arg)
{
    (void)name; (void)prio; (void)stack; (void)entry; (void)arg;
    ntasks++;
    return task_fail ? 0x13u : 0x2000u;     /* DryOS: handles even, errors odd; the harness runs the task's steps */
}
int host_tasks(void) { return ntasks; }
void host_task_fail(int f) { task_fail = f; }
void msleep(int ms) { clock_ms += ms; }
int get_ms_clock(void) { return clock_ms; }
static void menu_gone(void)
{
    menu_px_kept = real_raw[OFF] == MENU_PX;
    if (menu_px_kept) real_raw[OFF] = 0;                /* menu_close: Canon's redraw replaces the menu */
    menu_on = 0;
    menu_off_at = -1;
}
int gui_menu_shown(void)
{
    if (menu_on && menu_off_at >= 0 && clock_ms - menu_off_at >= 0) menu_gone();
    return menu_on;
}
void gui_stop_menu(void) { if (menu_on && !menu_sticky) menu_gone(); }
void host_menu(int on, int off_after_ms, int sticky)
{
    if (!on) { if (menu_on) menu_gone(); return; }
    menu_on = 1;
    menu_off_at = off_after_ms >= 0 ? clock_ms + off_after_ms : -1;
    menu_sticky = sticky;
    menu_px_kept = -1;
    real_raw[OFF] = MENU_PX;                            /* menu_open paints the screen */
}
int host_menu_px_kept(void) { return menu_px_kept; }
int host_under_enable(void) { return under_enable; }
int host_under_disable(void) { return under_disable; }
int host_redraws(void) { return redraws; }
int is_movie_mode(void) { return 0; }
static int gui_mode, gui_next = -1, gui_at, gui_lights = 1, disp = 1, gui_nreq, gui_req0_front = -1, disp_ons;
static void gui_step(void)
{
    if (gui_next >= 0 && clock_ms - gui_at >= 0)
    {
        gui_mode = gui_next;
        gui_next = -1;
        if (gui_mode && gui_lights) disp = 1;
    }
}
static int muted, bmp_offs, muted_at_req = -1, lv_starts;
int host_recording;
int display_is_on(void) { gui_step(); return disp; }
void display_on(void) { disp_ons++; disp = 1; }
void host_display_set(int on) { disp = on != 0; }           /* the Darkroom's Screen Off (fake_dark.c) */
int get_gui_mode(void) { gui_step(); return gui_mode; }
static int gui_drop, gui_last_req = -1;
void SetGUIRequestMode(int mode)
{
    gui_nreq++;
    gui_last_req = mode;
    if (!mode) gui_req0_front = front_disabled;
    else muted_at_req = muted;
    if (mode && gui_drop > 0) { gui_drop--; return; }   /* EOS M after Canon's own MENU: the ask does not take */
    gui_next = mode;
    gui_at = clock_ms + 60;
}
void host_gui_drop(int n) { gui_drop = n; }
int host_gui_last_req(void) { return gui_last_req; }
void bmp_off(void) { bmp_offs++; muted = 1; }
void bmp_on(void) { muted = 0; }
void force_liveview(void)                   /* ML: mode 0, then the LV button; LiveView is on 1.5 s later */
{
    lv_starts++;
    if (gui_mode) { gui_mode = 0; clock_ms += 1000; }
    clock_ms += 1500;
    lv = 1;
}
void host_gui_set(int mode, int display, int lights)
{
    gui_mode = mode; gui_next = -1; disp = display; gui_lights = lights; gui_nreq = 0; gui_req0_front = -1; disp_ons = 0;
    gui_drop = 0; gui_last_req = -1;
    muted = 0; bmp_offs = 0; muted_at_req = -1; lv_starts = 0;
}
int host_bmp_muted(void) { return muted; }
int host_bmp_offs(void) { return bmp_offs; }
int host_bmp_muted_at_req(void) { return muted_at_req; }
int host_lv_starts(void) { return lv_starts; }
void host_gui_later(int mode, int after_ms) { gui_next = mode; gui_at = clock_ms + after_ms; }
int host_gui_mode(void) { gui_step(); return gui_mode; }
int host_gui_requests(void) { return gui_nreq; }
int host_gui_req0_front(void) { return gui_req0_front; }
int host_display_ons(void) { return disp_ons; }
int host_front_disabled(void) { return front_disabled; }

/* ------------------------------------------------------------------ harness */
void host_init(const char *dir, uint32_t white, uint32_t black)
{
    rbfdir = dir;
    for (int i = 0; i < MAX_DYN_FONTS; i++)
        if (slot_name[i] && !font_dynamic[i].bitmap) font_dynamic[i].bitmap = (unsigned *)rbf_load(slot_name[i]);
    for (int i = 0; i < 256; i++)
    {
        /* Canon's own entries: a grey ramp with white / black where ML expects them; 0 = transparent */
        uint32_t v = i == 0 ? 0x00FF0000u : i == COLOR_WHITE ? white : i == COLOR_BLACK ? black
                   : (white & 0xFF000000u) | ((uint32_t)(i * 7 & 0xFF) << 16);
        LCD_Palette[3 * i] = REG_A + 4 * i;
        LCD_Palette[3 * i + 1] = 0;
        LCD_Palette[3 * i + 2] = v;
        LCD_Palette[0x300 + 3 * i] = REG_B + 4 * i;
        hw_a[i] = hw_b[i] = v;
    }
    memset(idle_raw, 0, sizeof(idle_raw));
    memset(real_raw, 0, sizeof(real_raw));
}

int host_palette_is_canon(void)
{
    for (int i = 0x50; i < 0xFF; i++)
        if (hw_a[i] != LCD_Palette[3 * i + 2] || hw_b[i] != LCD_Palette[3 * i + 2]) return 0;
    return 1;
}

int host_front_is_clear(void)
{
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 720; x++)
            if (real_raw[OFF + y * BMPPITCH + x]) return 0;
    return 1;
}

int host_ui_pixels_ok(void)
{
    int bad = 0;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 720; x++)
        {
            int c = real_raw[OFF + y * BMPPITCH + x];
            if (c < 0x50 || c > 0xFE) bad++;
        }
    return bad;
}

/* ML's yuv2rgb (bmp.c) on the value in register bank A */
static void entry_rgb(uint32_t e, uint8_t *o)
{
    int Y = (e >> 16) & 0xFF, U = (int8_t)((e >> 8) & 0xFF), V = (int8_t)(e & 0xFF);
    int r = Y + ((1437 * V) >> 10), g = Y - ((352 * U + 731 * V) >> 10), b = Y + ((1812 * U) >> 10);
    o[0] = r < 0 ? 0 : r > 255 ? 255 : r;
    o[1] = g < 0 ? 0 : g > 255 ? 255 : g;
    o[2] = b < 0 ? 0 : b > 255 ? 255 : b;
}

int host_dump(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    fprintf(fp, "P6\n720 480\n255\n");
    int stray = 0;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 720; x++)
        {
            int c = real_raw[OFF + y * BMPPITCH + x];
            uint8_t o[3] = { 255, 0, 255 };
            if (c >= 0x50 && c <= 0xFE) entry_rgb(hw_a[c], o);
            else stray++;
            fwrite(o, 1, 3, fp);
        }
    fclose(fp);
    return stray;
}
