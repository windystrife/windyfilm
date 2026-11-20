/* wf_roll.c - WindyFilm roll state, roll file text and names (wf_roll.h, docs/DESIGN.md 3.6). WindyStrife's own code.
 * Pure: no ML call, no file, no allocation (the card glue is wf_dark.c). Module rules: formats only %d %x %X %s, no
 * 64-bit division. */
#include <stdint.h>
#include <stddef.h>
#include "wf_roll.h"

#ifdef WF_HOST_PURE
#include <stdio.h>
#else
extern int snprintf(char *buf, size_t n, const char *fmt, ...);
#endif

static const char *const rs_name[WF_RS_N] = { "empty", "loaded", "full", "rewound", "done" };
const char *wf_roll_state_name(int st) { return st >= 0 && st < WF_RS_N ? rs_name[st] : "?"; }

/* ------------------------------------------------------------------ state machine */
void wf_roll_reset(struct wf_roll *r)
{
    char *p = (char *)r;
    for (size_t i = 0; i < sizeof *r; i++)
        p[i] = 0;
    r->exp = 12;
}

int wf_roll_open(const struct wf_roll *r)
{
    return r->state == WF_RS_LOADED || r->state == WF_RS_FULL || r->state == WF_RS_REWOUND;
}

int wf_roll_can_shoot(const struct wf_roll *r) { return r->state == WF_RS_LOADED && r->nshot < r->exp; }

int wf_roll_load(struct wf_roll *r, int film, int exp, int folder, int file)
{
    if (!wf_roll_ok_exp(exp))
        return -2;
    if (wf_roll_open(r) && r->nshot > 0)
        return -1;
    if (!(r->state == WF_RS_LOADED && r->nshot == 0)) {    /* a new roll (a reload of an unused one keeps its number) */
        r->num = r->num >= 999 ? 1 : r->num + 1;
    }
    r->film = film;
    r->exp = exp;
    r->state = WF_RS_LOADED;
    r->nshot = 0;
    r->dev = r->own = 0;
    r->seen_folder = folder;
    r->seen_file = file;
    for (int i = 0; i < WF_ROLL_MAXEXP; i++) {
        r->folder[i] = r->file[i] = 0;
        r->fst[i] = 0;
    }
    return 0;
}

int wf_roll_seen(struct wf_roll *r, int folder, int file, int (*has_cr2)(void *ctx, int folder, int file), void *ctx)
{
    if (folder < 100 || folder > 999 || file < 1 || file > 9999)
        return 0;
    if (r->state != WF_RS_LOADED) {
        r->seen_folder = folder;
        r->seen_file = file;
        return 0;
    }
    int f = r->seen_folder, n = r->seen_file, added = 0;
    if (f < 100 || f > 999 || n < 0 || n > 9999) {      /* nothing known: start from here */
        r->seen_folder = folder;
        r->seen_file = file;
        return 0;
    }
    /* walk from the last seen number to the new one (at most 64 steps: a larger gap, or a number that went back, means
       a card swap or a reset of the numbering: the count starts afresh from there, nothing is added) */
    int steps = 0;
    for (int f2 = f, n2 = n; steps <= 64 && !(f2 == folder && n2 == file); steps++)
        if (++n2 > 9999) {
            n2 = 1;
            if (++f2 > 999)
                steps = 65;
        }
    if (steps > 64) {
        r->seen_folder = folder;
        r->seen_file = file;
        return 0;
    }
    for (int k = 0; k < steps; k++) {
        if (++n > 9999) {
            n = 1;
            if (++f > 999)
                break;
        }
        if (r->nshot >= r->exp)
            continue;                                   /* beyond the roll: Canon keeps the file, the roll does not */
        if (has_cr2 && !has_cr2(ctx, f, n))
            continue;                                   /* a shot without a CR2 (quality changed behind our back) */
        int dup = 0;
        for (int i = 0; i < r->nshot; i++)
            if (r->folder[i] == f && r->file[i] == n)
                dup = 1;
        if (dup)
            continue;
        r->folder[r->nshot] = (uint16_t)f;
        r->file[r->nshot] = (uint16_t)n;
        r->fst[r->nshot] = WF_RF_SHOT;
        r->nshot++;
        added++;
    }
    r->seen_folder = folder;
    r->seen_file = file;
    if (r->nshot >= r->exp)
        r->state = WF_RS_FULL;
    return added;
}

int wf_roll_rewind(struct wf_roll *r)
{
    if (r->state == WF_RS_FULL || (r->state == WF_RS_LOADED && r->nshot > 0)) {
        r->state = WF_RS_REWOUND;
        return 0;
    }
    return r->state == WF_RS_REWOUND ? 0 : -1;
}

int wf_roll_next_dev(const struct wf_roll *r)
{
    if (r->state != WF_RS_REWOUND)
        return -1;
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++)
        if (r->fst[i] == WF_RF_SHOT)
            return i;
    return -1;
}

int wf_roll_left(const struct wf_roll *r)
{
    int n = 0;
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++)
        n += r->fst[i] == WF_RF_SHOT;
    return n;
}

int wf_roll_ndev(const struct wf_roll *r)
{
    int n = 0;
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++)
        n += r->fst[i] == WF_RF_DEV;
    return n;
}

void wf_roll_mark(struct wf_roll *r, int i, char st)
{
    if (i < 0 || i >= r->nshot || i >= WF_ROLL_MAXEXP)
        return;
    r->fst[i] = st;
    if (r->state == WF_RS_REWOUND && !wf_roll_left(r))
        r->state = WF_RS_DONE;
}

int wf_roll_close(struct wf_roll *r)
{
    if (!wf_roll_open(r))
        return -1;
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++)
        if (r->fst[i] == WF_RF_SHOT)
            r->fst[i] = WF_RF_KEPT;
    r->state = WF_RS_DONE;
    return 0;
}

int wf_roll_shelf_prep(struct wf_roll *r, int (*has_cr2)(void *ctx, int folder, int file), void *ctx)
{
    if (r->state == WF_RS_FULL || (r->state == WF_RS_LOADED && r->nshot > 0))
        r->state = WF_RS_REWOUND;
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++)
        if (r->fst[i] == WF_RF_SHOT && has_cr2 && !has_cr2(ctx, r->folder[i], r->file[i]))
            r->fst[i] = WF_RF_KEPT;
    if (r->state == WF_RS_REWOUND && !wf_roll_left(r))
        r->state = WF_RS_DONE;
    return wf_roll_left(r);
}

int wf_roll_again(struct wf_roll *r, int film, int (*has_cr2)(void *ctx, int folder, int file), void *ctx)
{
    int d = r->dev < 2 ? 1 : r->dev;
    if (d >= WF_ROLL_MAXDEV || r->nshot <= 0 || r->num <= 0)
        return -1;
    if (!r->own)
        r->own = r->film;
    r->dev = d + 1;
    r->film = film;
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++)
        r->fst[i] = !has_cr2 || has_cr2(ctx, r->folder[i], r->file[i]) ? WF_RF_SHOT : WF_RF_KEPT;
    r->state = wf_roll_left(r) ? WF_RS_REWOUND : WF_RS_DONE;
    return wf_roll_left(r);
}

/* ------------------------------------------------------------------ text */
static int put(char *b, int n, int o, const char *s)
{
    while (*s && o < n)
        b[o++] = *s++;
    return o;
}

int wf_roll_text(const struct wf_roll *r, char *buf, int n)
{
    if (n < WF_ROLL_FILE)
        return -1;
    char t[48];
    int o = 0;
    snprintf(t, sizeof t, "WFROLL 1\nroll=%d\nfilm=%d\nexp=%d\nstate=", r->num, r->film, r->exp);
    o = put(buf, WF_ROLL_FILE, o, t);
    o = put(buf, WF_ROLL_FILE, o, wf_roll_state_name(r->state));
    if (r->dev >= 2) {                            /* a develop again only: the first pass's file is as before */
        snprintf(t, sizeof t, "\ndev=%d\nown=%d", r->dev, r->own);
        o = put(buf, WF_ROLL_FILE, o, t);
    }
    snprintf(t, sizeof t, "\nseen=%d,%d\n", r->seen_folder, r->seen_file);
    o = put(buf, WF_ROLL_FILE, o, t);
    for (int i = 0; i < r->nshot && i < WF_ROLL_MAXEXP; i++) {
        char st[2] = { r->fst[i] ? r->fst[i] : WF_RF_SHOT, 0 };
        snprintf(t, sizeof t, "f=%d,%d,%s\n", r->folder[i], r->file[i], st);
        o = put(buf, WF_ROLL_FILE, o, t);
    }
    o = put(buf, WF_ROLL_FILE, o, "end\n");
    if (o > WF_ROLL_FILE - 1)
        return -1;
    while (o < WF_ROLL_FILE - 1)
        buf[o++] = ' ';
    buf[o++] = '\n';
    return o;
}

/* parse a decimal at s (optional '-'), *s moved past it; 0 digits -> -1 via *ok = 0 */
static int num(const char **s, const char *e, int *ok)
{
    int v = 0, d = 0, neg = 0;
    if (*s < e && **s == '-') { neg = 1; (*s)++; }
    while (*s < e && **s >= '0' && **s <= '9' && d < 9) {
        v = v * 10 + (**s - '0');
        (*s)++;
        d++;
    }
    if (!d)
        *ok = 0;
    return neg ? -v : v;
}

static int key(const char **s, const char *e, const char *k)
{
    const char *p = *s;
    while (*k) {
        if (p >= e || *p != *k)
            return 0;
        p++;
        k++;
    }
    *s = p;
    return 1;
}

int wf_roll_parse(struct wf_roll *r, const char *buf, int n)
{
    wf_roll_reset(r);
    const char *s = buf, *e = buf + n;
    if (!key(&s, e, "WFROLL 1\n"))
        return -1;
    int ok = 1, have_end = 0;
    while (s < e && ok && !have_end) {
        const char *le = s;
        while (le < e && *le != '\n')
            le++;
        if (key(&s, le, "roll=")) r->num = num(&s, le, &ok);
        else if (key(&s, le, "film=")) r->film = num(&s, le, &ok);
        else if (key(&s, le, "exp=")) r->exp = num(&s, le, &ok);
        else if (key(&s, le, "dev=")) r->dev = num(&s, le, &ok);
        else if (key(&s, le, "own=")) r->own = num(&s, le, &ok);
        else if (key(&s, le, "state=")) {
            int st = -1;
            for (int i = 0; i < WF_RS_N; i++) {
                const char *p = s;
                if (key(&p, le, rs_name[i]) && p == le)
                    st = i;
            }
            if (st < 0)
                ok = 0;
            r->state = st < 0 ? 0 : st;
        } else if (key(&s, le, "seen=")) {
            r->seen_folder = num(&s, le, &ok);
            if (!key(&s, le, ",")) ok = 0;
            r->seen_file = num(&s, le, &ok);
        } else if (key(&s, le, "f=")) {
            int i = r->nshot;
            int fo = num(&s, le, &ok), fi;
            if (!key(&s, le, ",")) ok = 0;
            fi = num(&s, le, &ok);
            if (!key(&s, le, ",")) ok = 0;
            char st = s < le ? *s : 0;
            if (i >= WF_ROLL_MAXEXP || fo < 100 || fo > 999 || fi < 1 || fi > 9999
                || (st != WF_RF_SHOT && st != WF_RF_DEV && st != WF_RF_ERR && st != WF_RF_KEPT))
                ok = 0;
            else {
                r->folder[i] = (uint16_t)fo;
                r->file[i] = (uint16_t)fi;
                r->fst[i] = st;
                r->nshot = i + 1;
            }
        } else if (key(&s, le, "end"))
            have_end = 1;
        else if (s < le)
            ok = 0;                                     /* an unknown line: not ours */
        s = le < e ? le + 1 : e;
    }
    if (!ok || !have_end || r->num < 0 || r->num > 999 || !wf_roll_ok_exp(r->exp) || r->nshot > r->exp || r->dev < 0
        || r->dev > WF_ROLL_MAXDEV || r->own < 0) {
        wf_roll_reset(r);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ names */
static int cpy(char *out, int n, const char *s)
{
    int o = 0;
    while (s[o] && o < n - 1) {
        out[o] = s[o];
        o++;
    }
    out[o] = 0;
    return s[o] ? -1 : o;
}

int wf_roll_root(const char *dcim_dir, char *out, int n)
{
    /* everything up to and including the '/' before "DCIM" */
    const char *p = dcim_dir, *hit = 0;
    for (; *p; p++)
        if ((p == dcim_dir || p[-1] == '/') && p[0] == 'D' && p[1] == 'C' && p[2] == 'I' && p[3] == 'M')
            hit = p;
    if (!hit || n < 2)
        return -1;
    int k = (int)(hit - dcim_dir);
    if (k >= n)
        return -1;
    for (int i = 0; i < k; i++)
        out[i] = dcim_dir[i];
    out[k] = 0;
    return k;
}

int wf_roll_cr2(const struct wf_roll *r, int i, const char *dcim_dir, const char *prefix, const char *ext, char *out, int n)
{
    if (i < 0 || i >= r->nshot || i >= WF_ROLL_MAXEXP)
        return -1;
    /* dcim_dir = ".../DCIM/100CANON" (maybe with a trailing '/'): keep the part before the folder number and the
       folder's 5 letters after it */
    char base[64];
    if (cpy(base, sizeof base, dcim_dir) < 0)
        return -1;
    int L = 0;
    while (base[L]) L++;
    if (L && base[L - 1] == '/')
        base[--L] = 0;
    int c = L;
    while (c > 0 && base[c - 1] != '/')
        c--;
    if (L - c < 3 || base[c] < '0' || base[c] > '9')
        return -1;
    const char *letters = base + c + 3;                 /* "CANON" */
    base[c] = 0;
    int k = snprintf(out, n, "%s%d%s/%s%04d.%s", base, r->folder[i], letters, prefix, r->file[i], ext);
    return k > 0 && k < n ? k : -1;
}

static int digits(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9')
            return 0;
    return 1;
}

int wf_roll_jpg2(const char *jpg, int folder, const char *ext, char *dir, int dn, char *out, int n)
{
    static const char our[] = WF_J2_LETTERS, fo[] = WF_J2_SUFFIX;
    int L = 0, b, fs, same = 1;
    while (jpg[L])
        L++;
    for (b = L; b > 0 && jpg[b - 1] != '/';)
        b--;                                     /* the file name: IMG_0208.JPG */
    if (L - b != 12 || jpg[b + 8] != '.' || !digits(jpg + b + 4, 4) || b < 10)
        return -1;
    for (fs = b - 1; fs > 0 && jpg[fs - 1] != '/';)
        fs--;                                    /* the folder: 100CANON */
    if (b - 1 - fs != 8 || !digits(jpg + fs, 3))
        return -1;
    for (int k = 0; k < 4; k++)
        same &= jpg[b + k] == our[k];
    if (same)
        return -1;
    int dl = folder ? fs + 3 : b - 1;
    if (dl + (folder ? 5 : 0) >= dn)
        return -1;
    for (int k = 0; k < dl; k++)
        dir[k] = jpg[k];
    for (int k = 0; folder && k < 5; k++)
        dir[dl++] = fo[k];
    dir[dl] = 0;
    char num[5] = { jpg[b + 4], jpg[b + 5], jpg[b + 6], jpg[b + 7], 0 };
    int k = snprintf(out, n, "%s/%s%s.%s", dir, our, num, ext);
    return k > 0 && k < n ? k : -1;
}

void wf_roll_name(int num, int dev, char *out)
{
    char s[2] = { dev >= 2 && dev <= WF_ROLL_MAXDEV ? (char)('A' + dev - 1) : 0, 0 };
    snprintf(out, 12, "ROLL%03d%s", num, s);
}

int wf_roll_name_parse(const char *s, int *num, int *dev)
{
    static const char R4[] = "ROLL";
    int k = 0, v = 0;
    for (; k < 4; k++)
        if ((s[k] & ~32) != R4[k])
            return -1;
    for (; k < 7; k++) {
        if (s[k] < '0' || s[k] > '9')
            return -1;
        v = v * 10 + s[k] - '0';
    }
    int d = 1, c = s[7] & ~32;
    if (s[7]) {
        if (c < 'B' || c > 'A' + WF_ROLL_MAXDEV - 1 || s[8])
            return -1;
        d = c - 'A' + 1;
    }
    if (v < 1)
        return -1;
    *num = v;
    *dev = d;
    return 0;
}

int wf_roll_dir(const struct wf_roll *r, const char *root, int level, char *out, int n)
{
    char nm[12];
    wf_roll_name(r->num, r->dev, nm);
    int k = level <= 0 ? snprintf(out, n, "%sROLLS", root)
          : level == 1 ? snprintf(out, n, "%sROLLS/%s", root, nm) : snprintf(out, n, "%sROLLS/%s/" WF_ROLL_SHELF, root, nm);
    return k > 0 && k < n ? k : -1;
}

int wf_roll_out(const struct wf_roll *r, int i, const char *root, char *out, int n)
{
    if (i < 0 || i >= r->nshot || i >= WF_ROLL_MAXEXP)
        return -1;
    char nm[12];
    wf_roll_name(r->num, r->dev, nm);
    int k = snprintf(out, n, "%sROLLS/%s/F%02d_%04d.JPG", root, nm, i + 1, r->file[i]);
    return k > 0 && k < n ? k : -1;
}

int wf_roll_cr2_moved(int num, int file, const char *root, const char *prefix, char *out, int n)
{
    if (num < 1 || num > 999 || file < 0 || file > 9999)
        return -1;
    char nm[12];
    wf_roll_name(num, 1, nm);
    int k = snprintf(out, n, "%sROLLS/%s/%s%04d.CR2", root, nm, prefix, file);
    return k > 0 && k < n ? k : -1;
}

/* ------------------------------------------------------------------ the film's speed (owner 11-17) */
/* Canon's 1/3-stop ISO steps and their raw codes (ML's lens.c tables hold every 1/8 code; Canon's dial stops at these) */
static const uint16_t iso_v[] = { 100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200,
                                  4000, 5000, 6400, 8000, 10000, 12800, 16000, 20000, 25600 };
static const uint8_t iso_c[] = { 72, 75, 77, 80, 83, 85, 88, 91, 93, 96, 99, 101, 104, 107, 109, 112,
                                 115, 117, 120, 123, 125, 128, 131, 133, 136 };
#define ISO_N ((int)(sizeof iso_c / sizeof *iso_c))

int wf_roll_iso(int speed, int lo, int hi, int third, int *iso)
{
    if (iso)
        *iso = 0;
    if (speed <= 0)
        return 0;
    if (speed > 25600)
        speed = 25600;
    int best = -1;
    for (int i = 0; i < ISO_N; i++) {
        int c = iso_c[i], v = iso_v[i];
        if (c < lo || c > hi || (!third && (c - 72) % 8))
            continue;
        if (best < 0) {
            best = i;
            continue;
        }
        /* nearer by ratio: max(s, v) / min(s, v) < max(s, b) / min(s, b), cross-multiplied (25600^2 fits 32 bits);
           a tie keeps the lower step */
        uint32_t b = iso_v[best], s = (uint32_t)speed, u = (uint32_t)v;
        uint32_t lv = (s > u ? s : u) * (s > b ? b : s), lb = (s > b ? s : b) * (s > u ? u : s);
        if (lv < lb)
            best = i;
    }
    if (best < 0)
        return 0;
    if (iso)
        *iso = iso_v[best];
    return iso_c[best];
}

int wf_roll_iso_of(int code)
{
    for (int i = 0; i < ISO_N; i++)
        if (iso_c[i] == code)
            return iso_v[i];
    return 0;
}

/* the quick keys (owner 11-17, wf_menu.c, DESIGN 3.17) */
static int iso_ok(int i, int lo, int hi, int third)
{
    return iso_c[i] >= lo && iso_c[i] <= hi && (third || (iso_c[i] - 72) % 8 == 0);
}

int wf_roll_iso_step(int code, int dir, int lo, int hi, int third)
{
    if (!dir || (!code && dir < 0))
        return code;
    if (dir > 0) {
        for (int i = 0; i < ISO_N; i++)
            if (iso_c[i] > code && iso_ok(i, lo, hi, third))
                return iso_c[i];
    } else {
        for (int i = ISO_N - 1; i >= 0; i--)
            if (iso_c[i] < code && iso_ok(i, lo, hi, third))
                return iso_c[i];
    }
    return code;
}

int wf_roll_av_step(int code, int dir, int lo, int hi, int half)
{
    if (!dir || code <= 0)
        return code;
    int s = dir > 0 ? 1 : -1;
    for (int c = code + s; c >= lo && c <= hi; c += s) {
        int m = c & 7;
        if (c == lo || c == hi || (half ? m == 0 || m == 4 : m == 0 || m == 3 || m == 5))
            return c;
    }
    return code;
}

int wf_roll_dial_clicks(int from, int to)
{
    if (from == to || from <= 0 || to <= 0)
        return 0;
    int s = to > from ? 1 : -1, half = (from & 7) == 4 || (to & 7) == 4, n = 0;
    int lo = s > 0 ? from + 1 : to, hi = s > 0 ? to : from - 1;
    for (int c = lo; c <= hi; c++) {
        int m = c & 7;
        if (half ? m == 0 || m == 4 : m == 0 || m == 3 || m == 5)
            n++;
    }
    return s * (n ? n : 1);
}

/* Canon's f-numbers by stop (code 8 = f/1.0, 8 codes a stop): whole, +1/3, +1/2, +2/3; the eighths between them
 * (a lens' own end) show as the nearest of these */
int wf_roll_av_f10(int code)
{
    static const short f0[14] = { 10, 14, 20, 28, 40, 56, 80, 110, 160, 220, 320, 450, 640, 910 };
    static const short f3[14] = { 11, 16, 22, 32, 45, 63, 90, 130, 180, 250, 360, 510, 720, 1000 };
    static const short f4[14] = { 12, 18, 25, 35, 45, 67, 95, 130, 190, 270, 380, 540, 760, 1100 };
    static const short f5[14] = { 12, 18, 25, 35, 50, 71, 100, 140, 200, 290, 400, 570, 810, 1100 };
    if (code < 8)
        return 0;
    int s = (code - 8) >> 3, m = (code - 8) & 7;
    if (m == 7) { s++; m = 0; }
    else if (m == 1) m = 0;
    else if (m == 2) m = 3;
    else if (m == 6) m = 5;
    if (s >= 14)
        return 0;
    return m == 0 ? f0[s] : m == 3 ? f3[s] : m == 4 ? f4[s] : f5[s];
}
