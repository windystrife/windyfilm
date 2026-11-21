/* wf_prints_ui.c - the film OS's Prints screen and contact sheet (docs/MODERNUI.md, modernUI M5), in the browser's UI
 * task.
 *
 * The Darkroom writes a small picture of every print beside it (ROLLS/ROLLnnn/Fnn_nnnn.WFP, wf_dark.h WF_PRV_*: a
 * screen picture of 336 x 224 and a contact sheet picture of 84 x 56, UYVY words). Prints shows one print large (INFO:
 * the whole screen), frame by frame across the rolls; SET opens the roll's contact sheet, its frames six to a strip.
 * Opened from Home's Prints tile, and from PLAY (Home, the 5D2's Top plate, shooting) while Settings > PLAY shows =
 * Prints. Its own PLAY is Canon's playback.
 *
 * No JPEG decoder, no YUV layer: the pictures go through the UI's palette, in the 103 entries the cans use on the other
 * screens (C_CAN0 ..), set from the picture itself: a median cut over a 4-4-4 histogram, each 5-5-5 colour's nearest
 * entry found once (the table), a 4 x 4 ordered dither. The same on both cameras, with or without LiveView.
 * Memory (malloc, all freed on leave): the picture 147 KB and the table 32 KB while Prints is up; the sheet's pixels
 * 165 KB while its sheet is kept. The sheet fills in over a few ticks: every frame read twice (the colours of all, then
 * each through them). Our own layout and marks (DESIGN 1: nothing of M-Film). */
#include "wf_ui_int.h"
#include "../wf_dark.h"

int wf_roll_blocks_play(void);                              /* wf_core.h: blind roll, PLAY is eaten */

#define PIC_WORDS   (WF_PRV_W / 2 * WF_PRV_H)
#define SH_WORDS    (WF_PRV_SW / 2 * WF_PRV_SH)
#define SH_PIX      (WF_PRV_SW * WF_PRV_SH)
#define SH_RD       (WF_PRV_SHEET_OFF + SH_WORDS * 4)        /* one read: the head and the sheet picture */
#define NSLOT       WFUI_CAN_SLOTS
#define LUT_N       32768
#define HIST_N      4096
#define MSG_MS      2500
#define BUILD_MS    40                                      /* the sheet's reads per tick */

#define PV_X        72                                      /* Prints: the picture's box */
#define PV_Y        48
#define PV_W        576
#define PV_H        384
#define SH_COLS     6                                       /* the contact sheet */
#define SH_CW       108
#define SH_CH       64
#define SH_X0       36
#define SH_Y0       48
#define SH_TX       22                                      /* the frame's picture in its cell (the number before it) */
#define SH_TY       4

static struct wf_prints ps;
static int pr_roll, pr_k, pr_full, pr_sheet;               /* roll (ps.r), frame on it, INFO's whole screen, sheet */
static const char *msg;
static int msg_until;
static int glow_set;

static uint32_t *pic;                                       /* the screen picture of (pic_roll, pic_k) */
static struct wf_prv_head pic_h;
static int pic_roll = -1, pic_k = -1, pic_ok;
static uint8_t *lut;                                        /* 5-5-5 colour -> palette slot (0xFF: not yet) */
static int lut_pal;                                         /* whose colours the table and the entries hold: 1 the
                                                               picture's, 2 the sheet's */
static uint8_t pic_pal[NSLOT][3], sh_pal[NSLOT][3];
static int pic_npal, sh_npal;

static uint8_t *sh_px;                                      /* the sheet: each frame's pixels (hardware entries) */
static uint32_t *sh_buf;                                    /* one frame's read */
static uint32_t *hist;                                      /* the sheet's colours while it is read */
static int sh_roll = -1, sh_step, sh_sel;
static uint8_t sh_ok[WF_ROLL_MAXEXP];

static void say(const char *s) { msg = s; msg_until = get_ms_clock() + MSG_MS; }
static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* ------------------------------------------------------------------ colours */
static void yuv_rgb(int y, int u, int v, int *r, int *g, int *b)   /* the encoder's YCbCr (JFIF, U / V signed) */
{
    *r = y + ((359 * v) >> 8);
    *g = y - ((88 * u + 183 * v) >> 8);
    *b = y + ((454 * u) >> 8);
}

static void hist_word(uint32_t *h, uint32_t q)              /* a UYVY word's two pixels into the histogram */
{
    int u = (int8_t)(q & 0xFF), v = (int8_t)((q >> 16) & 0xFF), r, g, b;
    for (int i = 0; i < 2; i++)
    {
        yuv_rgb(i ? (int)(q >> 24) : (int)((q >> 8) & 0xFF), u, v, &r, &g, &b);
        h[(clamp8(r) >> 4) << 8 | (clamp8(g) >> 4) << 4 | clamp8(b) >> 4]++;
    }
}

struct mbox { uint8_t lo[3], hi[3]; uint32_t n; };

/* the box's count; fit: shrink it to the bins that have any */
static uint32_t box_count(struct mbox *b, const uint32_t *h, int fit)
{
    uint8_t lo[3] = { 15, 15, 15 }, hi[3] = { 0, 0, 0 };
    uint32_t n = 0;
    for (int r = b->lo[0]; r <= b->hi[0]; r++)
        for (int g = b->lo[1]; g <= b->hi[1]; g++)
            for (int c = b->lo[2]; c <= b->hi[2]; c++)
            {
                uint32_t k = h[r << 8 | g << 4 | c];
                if (!k) continue;
                n += k;
                if (r < lo[0]) lo[0] = r;
                if (r > hi[0]) hi[0] = r;
                if (g < lo[1]) lo[1] = g;
                if (g > hi[1]) hi[1] = g;
                if (c < lo[2]) lo[2] = c;
                if (c > hi[2]) hi[2] = c;
            }
    if (fit && n)
        for (int a = 0; a < 3; a++) { b->lo[a] = lo[a]; b->hi[a] = hi[a]; }
    if (fit) b->n = n;
    return n;
}

/* up to kmax colours for histogram h: split the box with the most pixels x its longest side (green weighs most) at its
   median, until kmax or nothing splits; each colour the mean of its box. The number made */
static int median_cut(const uint32_t *h, uint8_t (*out)[3], int kmax)
{
    static struct mbox bx[NSLOT];
    static const uint8_t wt[3] = { 3, 4, 2 };
    int nb = 1;
    bx[0] = (struct mbox){ { 0, 0, 0 }, { 15, 15, 15 }, 0 };
    if (!box_count(&bx[0], h, 1)) return 0;
    while (nb < kmax)
    {
        int best = -1, ax = 0;
        uint32_t bs = 0;
        for (int i = 0; i < nb; i++)
            for (int a = 0; a < 3; a++)
            {
                uint32_t s = (uint32_t)(bx[i].hi[a] - bx[i].lo[a]) * wt[a] * bx[i].n;
                if (s > bs) { bs = s; best = i; ax = a; }
            }
        if (best < 0) break;
        struct mbox *b = &bx[best], c = *b, pl = *b;
        uint32_t acc = 0;
        int m = b->lo[ax];
        for (int v = b->lo[ax]; v < b->hi[ax]; v++)
        {
            pl.lo[ax] = pl.hi[ax] = (uint8_t)v;
            acc += box_count(&pl, h, 0);
            m = v;
            if (acc * 2 >= b->n) break;
        }
        b->hi[ax] = (uint8_t)m;
        c.lo[ax] = (uint8_t)(m + 1);
        box_count(b, h, 1);
        box_count(&c, h, 1);
        bx[nb++] = c;
    }
    for (int i = 0; i < nb; i++)
    {
        uint32_t s[3] = { 0, 0, 0 }, n = 0;
        for (int r = bx[i].lo[0]; r <= bx[i].hi[0]; r++)
            for (int g = bx[i].lo[1]; g <= bx[i].hi[1]; g++)
                for (int c = bx[i].lo[2]; c <= bx[i].hi[2]; c++)
                {
                    uint32_t k = h[r << 8 | g << 4 | c];
                    s[0] += k * (uint32_t)(r * 16 + 8);
                    s[1] += k * (uint32_t)(g * 16 + 8);
                    s[2] += k * (uint32_t)(c * 16 + 8);
                    n += k;
                }
        for (int a = 0; a < 3; a++) out[i][a] = (uint8_t)(n ? s[a] / n : 0);
    }
    return nb;
}

/* the colours into the palette entries, the table emptied for them */
static void pal_use(int which)
{
    const uint8_t (*p)[3] = which == 2 ? sh_pal : pic_pal;
    int n = which == 2 ? sh_npal : pic_npal;
    for (int i = 0; i < n; i++) wfp_set_rgb(C_CAN0 + i, p[i][0], p[i][1], p[i][2]);
    if (lut) memset(lut, 0xFF, LUT_N);
    lut_pal = which;
}

static uint8_t nearest(int r, int g, int b)
{
    const uint8_t (*p)[3] = lut_pal == 2 ? sh_pal : pic_pal;
    int n = lut_pal == 2 ? sh_npal : pic_npal, best = 0;
    uint32_t bd = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++)
    {
        int dr = r - p[i][0], dg = g - p[i][1], db = b - p[i][2];
        uint32_t d = (uint32_t)(3 * dr * dr + 4 * dg * dg + 2 * db * db);
        if (d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}

/* one pixel through the palette: dt the ordered dither's offset */
static uint8_t map_px(int y, int u, int v, int dt)
{
    int r, g, b;
    yuv_rgb(y, u, v, &r, &g, &b);
    r = clamp8(r + dt);
    g = clamp8(g + dt);
    b = clamp8(b + dt);
    int key = (r >> 3) << 10 | (g >> 3) << 5 | b >> 3;
    uint8_t q = lut[key];
    if (q == 0xFF) q = lut[key] = nearest((r & ~7) | 4, (g & ~7) | 4, (b & ~7) | 4);
    return PX(C_CAN0 + q);
}
static int dith(int x, int y) { return wfg_bayer[y & 3][x & 3] * 2 - 15; }

/* ------------------------------------------------------------------ the picture */
static int need_lut(void)
{
    if (!lut && (lut = malloc(LUT_N))) memset(lut, 0xFF, LUT_N);
    return lut != 0;
}

/* frame k of roll ri: its screen picture and colours. 1 = there */
static int pic_load(int ri, int k)
{
    if (pic_roll == ri && pic_k == k)
    {
        if (pic_ok && lut_pal != 1) pal_use(1);             /* (back from the sheet: its colours again) */
        return pic_ok;
    }
    pic_roll = ri;
    pic_k = k;
    pic_ok = 0;
    if ((!pic && !(pic = malloc(PIC_WORDS * 4))) || !need_lut())
    {
        wf_log("ui: Prints: no memory for the picture");
        return 0;
    }
    char p[96] = "";
    uint32_t sz = 0;
    int rc = wf_prints_path(&ps.r[ri], k, p, sizeof p) < 0 ? WF_RD_MISSING : wfui_read(p, 0, &pic_h, sizeof pic_h, &sz);
    if (rc == WF_RD_OK && (sz != WF_PRV_BYTES || !wf_prv_head_ok(&pic_h))) rc = WF_RD_IO;
    if (rc == WF_RD_OK) rc = wfui_read(p, WF_PRV_PIC_OFF, pic, PIC_WORDS * 4, &sz);
    uint32_t *h = rc == WF_RD_OK ? malloc(HIST_N * 4) : 0;
    if (!h)
    {
        wf_log("ui: Prints: %s not read (%d)", p, rc);
        return 0;
    }
    memset(h, 0, HIST_N * 4);
    for (int y = pic_h.py; y < pic_h.py + pic_h.ph; y++)
        for (int x = pic_h.px; x < pic_h.px + pic_h.pw; x += 2)
            hist_word(h, pic[y * (WF_PRV_W / 2) + x / 2]);
    pic_npal = median_cut(h, pic_pal, NSLOT);
    free(h);
    pal_use(1);
    pic_ok = 1;
    wf_log("ui: Prints: ROLL%03d F%02d, %d colours", ps.r[ri].num, ps.r[ri].fr[k], pic_npal);
    return 1;
}

static int pic_y(const uint32_t *row, int x)
{
    uint32_t q = row[x >> 1];
    return x & 1 ? (int)(q >> 24) : (int)((q >> 8) & 0xFF);
}

/* a display position (1/128 px of the print as shown, u across, v down) in the stored picture by the orientation:
   6 = stored turned left (shown turned right), 8 the other way, 3 upside down */
static void pic_src(int u, int v, int *sx, int *sy)
{
    int W = (pic_h.pw - 1) * 128, H = (pic_h.ph - 1) * 128;
    switch (pic_h.orient)
    {
    case 6:  *sx = v;     *sy = H - u; break;
    case 8:  *sx = W - v; *sy = u;     break;
    case 3:  *sx = W - u; *sy = H - v; break;
    default: *sx = u;     *sy = v;     break;
    }
}

/* the print as shown (its orientation) fitted into the box, centred: Y bilinear, colour from the nearest pair */
static void pic_draw(int bx, int by, int bw, int bh)
{
    int turn = pic_h.orient == 6 || pic_h.orient == 8;
    int pw = turn ? pic_h.ph : pic_h.pw, ph = turn ? pic_h.pw : pic_h.ph, dw = bw, dh = bw * ph / pw;
    if (dh > bh) { dh = bh; dw = bh * pw / ph; }
    if (dw < 1 || dh < 1) return;
    int dx = bx + (bw - dw) / 2, dy = by + (bh - dh) / 2;
    for (int y = 0; y < dh; y++)
    {
        int v = ((2 * y + 1) * ph * 128) / (2 * dh) - 64;
        v = v < 0 ? 0 : v > (ph - 1) * 128 ? (ph - 1) * 128 : v;
        uint8_t *o = wfg_cv + (dy + y) * wfg_pitch + dx;
        for (int x = 0; x < dw; x++)
        {
            int u = ((2 * x + 1) * pw * 128) / (2 * dw) - 64, sx, sy;
            u = u < 0 ? 0 : u > (pw - 1) * 128 ? (pw - 1) * 128 : u;
            pic_src(u, v, &sx, &sy);
            int x0 = pic_h.px + (sx >> 7), fx = sx & 127, x1 = fx ? x0 + 1 : x0;
            int y0 = pic_h.py + (sy >> 7), fy = sy & 127, y1 = fy ? y0 + 1 : y0;
            const uint32_t *r0 = pic + y0 * (WF_PRV_W / 2), *r1 = pic + y1 * (WF_PRV_W / 2);
            int a = pic_y(r0, x0) * (128 - fx) + pic_y(r0, x1) * fx;
            int c = pic_y(r1, x0) * (128 - fx) + pic_y(r1, x1) * fx;
            int yy = (a * (128 - fy) + c * fy + 8192) >> 14;
            uint32_t q = (fy < 64 ? r0 : r1)[(fx < 64 ? x0 : x1) >> 1];
            o[x] = map_px(yy, (int8_t)(q & 0xFF), (int8_t)((q >> 16) & 0xFF), dith(dx + x, dy + y));
        }
    }
}

/* ------------------------------------------------------------------ the contact sheet */
static void sheet_drop(void)                                /* its build stopped (the pixels kept for a roll it made) */
{
    if (hist) { free(hist); hist = 0; }
    if (sh_roll >= 0 && sh_step < 2 * ps.r[sh_roll].n) sh_roll = -1;
}

static void sheet_free(void)
{
    if (hist) { free(hist); hist = 0; }
    if (sh_px) { free(sh_px); sh_px = 0; }
    if (sh_buf) { free(sh_buf); sh_buf = 0; }
    sh_roll = -1;
}

/* the sheet of roll ri: its memory, its build from the start (the ticks read it). 0 = no memory */
static int sheet_begin(int ri)
{
    if (sh_roll == ri && sh_px) return 1;
    for (int tries = 0; tries < 2; tries++)
    {
        if (!sh_px) sh_px = malloc(WF_ROLL_MAXEXP * SH_PIX);
        if (!sh_buf) sh_buf = malloc(SH_RD);
        if (!hist) hist = malloc(HIST_N * 4);
        if (sh_px && sh_buf && hist && need_lut()) break;
        if (pic) { free(pic); pic = 0; pic_roll = pic_k = -1; }   /* the picture is read again when Prints needs it */
    }
    if (!sh_px || !sh_buf || !hist || !lut)
    {
        sheet_free();
        wf_log("ui: contact sheet: no memory");
        return 0;
    }
    memset(hist, 0, HIST_N * 4);
    memset(sh_ok, 0, sizeof sh_ok);
    sh_roll = ri;
    sh_step = 0;
    return 1;
}

/* one read of the sheet's build: steps 0 .. n-1 its colours, n .. 2n-1 each frame through them. 1 = done one */
static int sheet_step(void)
{
    const struct wf_prints_roll *r = &ps.r[sh_roll];
    if (sh_step >= 2 * r->n) return 0;
    int k = sh_step < r->n ? sh_step : sh_step - r->n, pass2 = sh_step >= r->n;
    char p[96];
    uint32_t sz = 0;
    struct wf_prv_head h;
    int ok = (!pass2 || sh_ok[k]) && wf_prints_path(r, k, p, sizeof p) >= 0
             && wfui_read(p, 0, sh_buf, SH_RD, &sz) == WF_RD_OK && sz == WF_PRV_BYTES;
    if (ok)
    {
        memcpy(&h, sh_buf, sizeof h);
        ok = wf_prv_head_ok(&h);
    }
    const uint32_t *w = sh_buf + WF_PRV_SHEET_OFF / 4;
    if (!pass2)
    {
        sh_ok[k] = (uint8_t)ok;
        if (ok)
            for (int i = 0; i < SH_WORDS; i++) hist_word(hist, w[i]);
        if (!ok) wf_log("ui: contact sheet: ROLL%03d F%02d no picture", r->num, r->fr[k]);
    }
    else if (ok)
    {
        if (lut_pal != 2) pal_use(2);
        uint8_t *o = sh_px + k * SH_PIX;
        for (int y = 0; y < WF_PRV_SH; y++)
            for (int x = 0; x < WF_PRV_SW; x += 2)
            {
                uint32_t q = w[y * (WF_PRV_SW / 2) + x / 2];
                int u = (int8_t)(q & 0xFF), v = (int8_t)((q >> 16) & 0xFF);
                o[y * WF_PRV_SW + x] = map_px((int)((q >> 8) & 0xFF), u, v, dith(x, y));
                o[y * WF_PRV_SW + x + 1] = map_px((int)(q >> 24), u, v, dith(x + 1, y));
            }
    }
    else
        sh_ok[k] = 0;
    sh_step++;
    if (sh_step == r->n)                                    /* all read once: the sheet's colours */
    {
        sh_npal = median_cut(hist, sh_pal, NSLOT);
        free(hist);
        hist = 0;
        pal_use(2);
        wf_log("ui: contact sheet: ROLL%03d, %d frames, %d colours", r->num, r->n, sh_npal);
    }
    return 1;
}

static int sheet_done(void) { return sh_roll >= 0 && sh_step >= 2 * ps.r[sh_roll].n; }

/* ------------------------------------------------------------------ drawing */
static void toast(void)
{
    if (!msg) return;
    int w = ui_textw(F_15, msg) + 32, x0 = (WFUI_W - w) / 2;
    wfg_rrect(x0, 388, x0 + w - 1, 415, 13, C_FRAME, C_AMBER, 1);
    ui_text(F_15, x0 + 16, 393, msg, C_TXT);
}

static void roll_name(char *b, int n, const struct wf_prints_roll *r)
{
    char nm[12];
    wf_roll_name(r->num, r->dev, nm);
    snprintf(b, n, "%s", nm + 4);                           /* "021", "021B" */
}

/* after the title: what is shown, clipped before the bar's right group */
static void top_info(const char *title, const char *info)
{
    int xr = wfos_top_bar(title), x = 48 + ui_textw(F_D20, title) + 18;
    if (x < xr - 20) ui_text_clip(F_15, x, 11, info, C_TXT2, xr - 14);
}

static void bg_plain(void)
{
    if (!glow_set)
    {
        uint8_t rgb[3] = { 34, 36, 42 };
        wfg_glow_centre(-400, -400, -400, -400);            /* no glow: a plain gradient (the pictures carry colour) */
        wfp_set_glow(rgb);
        glow_set = 1;
    }
    wfg_background(0, WFUI_H);
}

static void none_render(void)
{
    bg_plain();
    wfos_top_bar("Prints");
    const char *a = "No prints yet", *b = "The Darkroom makes them: each roll it develops shows here";
    ui_text(F_D20, (WFUI_W - ui_textw(F_D20, a)) / 2, 196, a, C_TXT);
    ui_text(F_15, (WFUI_W - ui_textw(F_15, b)) / 2, 232, b, C_TXT2);
    static const char *const kv[] = { "PLAY", "Canon's playback", "MENU", "Home", 0 };
    wfos_footer(kv);
    toast();
}

static void pv_render(void)
{
    const struct wf_prints_roll *r = &ps.r[pr_roll];
    int ok = pic_ok && pic_roll == pr_roll && pic_k == pr_k;
    if (pr_full && ok)
    {
        wfg_rect(0, 0, WFUI_W - 1, WFUI_H - 1, C_STRIP);
        pic_draw(0, 0, WFUI_W, WFUI_H);
        toast();
        return;
    }
    bg_plain();
    char rn[8], info[96];
    roll_name(rn, sizeof rn, r);
    const struct wf_film *f = ok ? wf_film_by_id(pic_h.film) : 0;
    snprintf(info, sizeof info, "ROLL %s   F%02d   %d of %d%s%s", rn, r->fr[pr_k], pr_k + 1, r->n, f ? "   " : "",
             f ? f->shortname : "");
    top_info("Prints", info);
    if (ok)
        pic_draw(PV_X, PV_Y, PV_W, PV_H);
    else
    {
        wfg_rrect(PV_X + 96, PV_Y + 120, PV_X + PV_W - 97, PV_Y + PV_H - 121, 10, C_STRIP, C_LINE, 1);
        const char *s = "No picture for this frame";
        ui_text(F_15, (WFUI_W - ui_textw(F_15, s)) / 2, PV_Y + PV_H / 2 - 9, s, C_TXT2);
    }
    static const char *const kv[] = { "<  >", "Frame", "UP  DOWN", "Roll", "SET", "Sheet", "INFO", "Full",
                                      "PLAY", "Canon's", "MENU", "Home", 0 };
    wfos_footer(kv);
    toast();
}

static void sh_render(void)
{
    const struct wf_prints_roll *r = &ps.r[pr_roll];
    bg_plain();
    char rn[8], info[80];
    roll_name(rn, sizeof rn, r);
    snprintf(info, sizeof info, "ROLL %s   %d prints", rn, r->n);
    top_info("Contact sheet", info);
    int rows = (r->n + SH_COLS - 1) / SH_COLS, built = sh_roll == pr_roll ? sh_step - r->n : -1;
    for (int j = 0; j < rows; j++)
        wfg_rect(SH_X0 - 6, SH_Y0 + j * SH_CH, SH_X0 + SH_COLS * SH_CW + 5, SH_Y0 + j * SH_CH + SH_CH - 3, C_STRIP);
    for (int k = 0; k < r->n; k++)
    {
        int cx = SH_X0 + (k % SH_COLS) * SH_CW, cy = SH_Y0 + (k / SH_COLS) * SH_CH;
        int tx = cx + SH_TX, ty = cy + SH_TY, sel = k == sh_sel;
        char nb[4];
        snprintf(nb, sizeof nb, "%d", r->fr[k]);
        ui_text(F_13, cx + SH_TX - 4 - ui_textw(F_13, nb), ty + 1, nb, sel ? C_AMBER : C_AMBERD);
        if (k < built && sh_ok[k])
            for (int y = 0; y < WF_PRV_SH; y++)
                memcpy(wfg_cv + (ty + y) * wfg_pitch + tx, sh_px + k * SH_PIX + y * WF_PRV_SW, WF_PRV_SW);
        else
        {
            wfg_rect(tx, ty, tx + WF_PRV_SW - 1, ty + WF_PRV_SH - 1, C_HOLE);
            if (k < built)                                  /* read, no picture */
                ui_text(F_13, tx + (WF_PRV_SW - ui_textw(F_13, "none")) / 2, ty + 19, "none", C_TXT3);
        }
        if (sel) wfg_rrect(tx - 3, ty - 3, tx + WF_PRV_SW + 2, ty + WF_PRV_SH + 2, 3, -1, C_AMBER, 2);
    }
    static const char *const kv[] = { "ARROWS", "Frame", "SET", "Print", "PLAY", "Canon's playback", "MENU", "Back",
                                      0 };
    wfos_footer(kv);
    toast();
}

/* ------------------------------------------------------------------ moving */
static void show(int ri, int k)
{
    pr_roll = ri;
    pr_k = k;
    if (!pr_sheet) pic_load(ri, k);
}

static void frame_step(int d)                               /* across the rolls: past a roll's end the next newer one */
{
    int ri = pr_roll, k = pr_k + d;
    if (k >= ps.r[ri].n)
    {
        if (ri == 0) { say("The newest print"); return; }
        ri--;
        k = 0;
    }
    else if (k < 0)
    {
        if (ri + 1 >= ps.n) { say("The oldest print"); return; }
        ri++;
        k = ps.r[ri].n - 1;
    }
    show(ri, k);
}

static void roll_step(int d)                                /* +1 the next older roll, -1 newer */
{
    int ri = pr_roll + d;
    if (ri < 0 || ri >= ps.n) { say(d > 0 ? "The oldest roll" : "The newest roll"); return; }
    show(ri, 0);
}

static void canon_play(void)
{
    if (wf_roll_blocks_play()) { say("Blind roll: Canon's playback after the Darkroom"); return; }
    wf_log("ui: Prints -> Canon's PLAY");
    wfui_close_after(WFUI_AFTER_PLAY);
}

static void sheet_open(void)
{
    if (!sheet_begin(pr_roll)) { say("No memory for the contact sheet"); return; }
    pr_sheet = 1;
    pr_full = 0;
    sh_sel = pr_k;
    if (sheet_done()) pal_use(2);
    wf_log("ui: contact sheet ROLL%03d", ps.r[pr_roll].num);
}

static void sheet_close(int k)
{
    sheet_drop();
    pr_sheet = 0;
    show(pr_roll, k);                                       /* (its colours the picture's again) */
}

static int pv_key(int k)
{
    switch (k)
    {
    case MODULE_KEY_PRESS_LEFT: case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_UP:     frame_step(-1); break;
    case MODULE_KEY_PRESS_RIGHT: case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_DOWN: frame_step(1); break;
    case MODULE_KEY_PRESS_UP:   roll_step(-1); break;
    case MODULE_KEY_PRESS_DOWN: case MODULE_KEY_PICSTYLE: roll_step(1); break;
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER: case MODULE_KEY_Q: sheet_open(); break;
    case MODULE_KEY_INFO:       pr_full = !pr_full; break;
    case MODULE_KEY_MENU:
        if (pr_full) pr_full = 0;
        else wfui_goto(WFUI_SCR_HOME);
        break;
    case MODULE_KEY_PLAY:       canon_play(); break;
    case MODULE_KEY_LV:         return 1;
    }
    return 0;
}

static int sh_key(int k)
{
    int n = ps.r[pr_roll].n;
    switch (k)
    {
    case MODULE_KEY_PRESS_LEFT: case MODULE_KEY_WHEEL_LEFT: case MODULE_KEY_WHEEL_UP:
        if (sh_sel > 0) sh_sel--;
        break;
    case MODULE_KEY_PRESS_RIGHT: case MODULE_KEY_WHEEL_RIGHT: case MODULE_KEY_WHEEL_DOWN:
        if (sh_sel + 1 < n) sh_sel++;
        break;
    case MODULE_KEY_PRESS_UP:   if (sh_sel >= SH_COLS) sh_sel -= SH_COLS; break;
    case MODULE_KEY_PRESS_DOWN: if (sh_sel + SH_COLS < n) sh_sel += SH_COLS; break;
    case MODULE_KEY_PICSTYLE:                               /* (5D2) the next older roll's sheet, round */
        sheet_drop();
        pr_roll = (pr_roll + 1) % ps.n;
        pr_k = 0;
        sheet_open();
        break;
    case MODULE_KEY_PRESS_SET: case MODULE_KEY_JOY_CENTER: case MODULE_KEY_Q: case MODULE_KEY_INFO:
    case MODULE_KEY_MENU:
        sheet_close(sh_sel);
        break;
    case MODULE_KEY_PLAY:       canon_play(); break;
    case MODULE_KEY_LV:         return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ dispatch (wf_dark_ui.c) */
void wfpr_enter(void)
{
    msg = 0;
    glow_set = 0;
    pr_full = pr_sheet = 0;
    lut_pal = 0;
    pic_roll = pic_k = -1;
    sh_roll = -1;
    if (wf_prints_scan(&ps) < 0) ps.n = 0;                  /* (the card listed again on each way in) */
    int frames = 0;
    for (int i = 0; i < ps.n; i++) frames += ps.r[i].n;
    wf_log("ui: Prints: %d rolls, %d prints", ps.n, frames);
    if (ps.n) show(0, ps.r[0].n - 1);                       /* the newest print */
}

void wfpr_leave(void)
{
    sheet_free();
    if (pic) { free(pic); pic = 0; }
    if (lut) { free(lut); lut = 0; }
    pic_roll = pic_k = -1;
    msg = 0;
    wfp_init_fixed();                                       /* the cans' entries back for the other screens */
    wfc_pal_again();
}

void wfpr_render(void)
{
    if (!ps.n) none_render();
    else if (pr_sheet) sh_render();
    else pv_render();
}

int wfpr_key(int k)
{
    msg = 0;
    if (!ps.n)
    {
        if (k == MODULE_KEY_PLAY) canon_play();
        else if (k == MODULE_KEY_MENU) wfui_goto(WFUI_SCR_HOME);
        return k == MODULE_KEY_LV;
    }
    return pr_sheet ? sh_key(k) : pv_key(k);
}

int wfpr_tick(int now)
{
    int moved = 0;
    if (msg && now - msg_until >= 0) { msg = 0; moved = 1; }
    if (pr_sheet && sh_roll >= 0 && !sheet_done())
    {
        int t0 = get_ms_clock();
        do moved |= sheet_step();
        while (!sheet_done() && get_ms_clock() - t0 < BUILD_MS);
    }
    return moved;
}

#ifdef WF_HOST
int wfpr_host_roll(void) { return pr_roll; }
int wfpr_host_k(void) { return pr_k; }
int wfpr_host_sheet(void) { return pr_sheet; }
int wfpr_host_full(void) { return pr_full; }
int wfpr_host_sel(void) { return sh_sel; }
int wfpr_host_built(void) { return sheet_done(); }
int wfpr_host_npal(void) { return pr_sheet ? sh_npal : pic_npal; }
int wfpr_host_pic_ok(void) { return pic_ok && pic_roll == pr_roll && pic_k == pr_k; }
const char *wfpr_host_msg(void) { return msg ? msg : ""; }
#endif
