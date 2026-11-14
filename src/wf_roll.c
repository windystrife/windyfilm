/* wf_roll.c - WindyFilm roll state, roll file text and names (wf_roll.h, docs/DESIGN.md 3.4). WindyStrife's own code.
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
    if (!ok || !have_end || r->num < 0 || r->num > 999 || !wf_roll_ok_exp(r->exp) || r->nshot > r->exp) {
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

int wf_roll_dir(const struct wf_roll *r, const char *root, int level, char *out, int n)
{
    int k = level <= 0 ? snprintf(out, n, "%sROLLS", root) : snprintf(out, n, "%sROLLS/ROLL%03d", root, r->num);
    return k > 0 && k < n ? k : -1;
}

int wf_roll_out(const struct wf_roll *r, int i, const char *root, char *out, int n)
{
    if (i < 0 || i >= r->nshot || i >= WF_ROLL_MAXEXP)
        return -1;
    int k = snprintf(out, n, "%sROLLS/ROLL%03d/F%02d_%04d.JPG", root, r->num, i + 1, r->file[i]);
    return k > 0 && k < n ? k : -1;
}
