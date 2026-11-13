/* wf_loglut.c - "Log + LUT" output modes for WindyFilm (WindyStrife). See wf_loglut.h.
 *
 * Load time only (wf_task). Integer arithmetic, no float, no strtof: armv5t, -marm, no LDRD/STRD.
 * 64-bit products are SMULL/UMULL; the one 64-bit division per channel (domain scale) is
 * __aeabi_uldivmod; the node index divisions use exact reciprocals (no __aeabi_uidiv). Every symbol
 * used is exported by the EOS M 2.0.2 core (FIO_GetFileSize, FIO_OpenFile, FIO_ReadFile,
 * FIO_CloseFile, FIO_SeekSkipFile, FIO_CreateFile, FIO_WriteFile, FIO_RemoveFile, FIO_FindFirstEx,
 * FIO_FindNextEx, FIO_FindClose, alloc_fio_file, convert_fio_file_info, __mem_malloc, __mem_free,
 * memcpy, memmove, __aeabi_uldivmod): checked with fxlab/checksyms.py against EOSM_202.sym.
 *
 * .cube rules (decided here, see INTEGRATION.md):
 *  - 3D only: LUT_3D_SIZE 2..65 (plain integer), given once, before the data.
 *  - LUT_1D_SIZE or LUT_1D_INPUT_RANGE anywhere (1D LUT, or a 1D shaper + 3D file): rejected.
 *  - TITLE, '#' comments (whole lines, or after the three numbers), blank lines, CRLF, tabs,
 *    leading spaces, UTF-8 BOM, no newline at the end: accepted. CR-only files: rejected (E_LINE).
 *  - a line of WF_LL_BUF (8 KB) bytes or more: skipped if it is a comment or TITLE, else E_LINE,
 *    wherever the reads fall (the same result with any buffer alignment).
 *  - DOMAIN_MIN / DOMAIN_MAX (3 numbers) and Resolve's LUT_3D_INPUT_RANGE (2 numbers) are
 *    HONOURED: grid coordinate = (log code - min) / (max - min) * (N - 1), clamped. Accepted when
 *    -1 <= min <= 1, max <= 64 and max - min >= 1/16 per channel, else E_DOMAIN (review F1).
 *  - other keywords before the data: ignored and counted (info.skipped); any keyword after the
 *    first data line: E_SYNTAX.
 *  - numbers: [+-] digits [. digits] [e|E [+-] digits], parsed to Q24 (9 significant digits);
 *    anything else (nan, "0,5", "1..2", a 4th number) = E_SYNTAX. Values outside 0..1 are clamped
 *    and counted (info.clamped). Samples are u16 0..65535 = 0..1, R fastest as in the file.
 *  - fewer than N^3 samples = E_TRUNC, more = E_EXTRA. A last line without a newline that has fewer
 *    fraction digits in a number than the data line before it (all three equal there) is a cut
 *    line: E_TRUNC (review F2).
 *
 * Composite (per node, see wf_loglut.h): y, cb, cr -> R'G'B' Q20 code (clamped 0..255, as
 * wf_logout_ref.c) -> sRGB EOTF (1/4-code table, linear interpolation, Q24 linear) -> Rec.709 to log
 * gamut (Q20, int64 sums) -> log curve (wf_lenc values, linear interpolation, Q20 code) -> grid
 * coordinate Q16 -> TETRAHEDRAL interpolation of the cube at its native grid (Q16 weights, u32
 * sums) -> Y, Cb, Cr (BT.601 full range, x 2^48) -> rounded, clamped to 0..255 / -128..127, stored
 * in the kernel's form (N, Y; C = (2 Cb + 257) | (2 Cr + 257) << 16; L and K after the parse).
 * The node set (36,193 = 256 neutral + 33^3) is built while the file streams: a prepass maps every
 * node to its blue grid cell, and each node is evaluated as soon as the two B-slabs of its cell
 * have been read (two slabs of N x N RGB u16 held, 50.7 KB at 65^3).
 *
 * Cost (qemu-arm instruction counts, cnt.sh; INTEGRATION.md has the table): 65^3 load 232.4 M
 * (parse alone 176.2 M = 23.8 per byte of the 7.4 MB file), 33^3 61.2 M, 17^3 35.1 M; a cache hit
 * (wf_loglut_load_cached, ML/DATA/<LOG>.FRT, 181,436 B) 1.05 M + the file read.
 *
 * Line framing: buf[pos..lim) holds complete lines (lim = after the last '\n' read), so every line
 * is scanned once, by the parser itself. Every FIO read is WF_LL_BUF bytes at a multiple of
 * WF_LL_BUF (review F6); the partial line in front of it is kept in the 2 x WF_LL_BUF + 1 cached
 * buffer. Tested with 8 KB and 61-byte buffers (parsetest, v1check). */
#include <stdint.h>
#include <stddef.h>
#include "wf_loglut.h"
#include "wf_loglut_tab.h"

/* ML / DryOS API (ML/src/fio-ml.h, mem.h; declared here so the file builds without ML headers) */
extern void *FIO_OpenFile(const char *name, unsigned mode);   /* 0 on error */
extern int   FIO_ReadFile(void *f, void *buf, size_t n);      /* buf must be uncacheable (fio_malloc) */
extern void  FIO_CloseFile(void *f);
extern int   FIO_GetFileSize(const char *name, uint32_t *size);
extern void *__mem_malloc(size_t len, unsigned flags, const char *file, unsigned line);
extern void  __mem_free(void *p);
extern void *memcpy(void *d, const void *s, size_t n);
extern void *memmove(void *d, const void *s, size_t n);
#define WF_O_RDONLY  0
#define WF_MEM_DMA   1                   /* uncacheable, for FIO */
#define WF_MEM_TMP   2                   /* freed soon (may use shoot memory) */

#ifndef WF_LL_BUF
#define WF_LL_BUF    8192                /* FIO read size; a line must fit (comments / TITLE need not) */
#endif
#define WF_LL_BUFC   (2 * WF_LL_BUF + 1)  /* cached line buffer: partial line + one read + sentinel */
#define WF_LL_MAXFILE (32u << 20)
#define WF_LL_MAXAUX 65536
#define WF_LL_SAT    0x7fffffff          /* ~ +128.0 in Q24 */
#define WF_LL_ONE    (1 << 24)

/* character classes (wf_ll_cls): blank = space, tab, CR, VT, FF; end of the content = '\n', '#' */
#define WF_CL(c) wf_ll_cls[(uint8_t)(c)]
#define WF_WS(c) (WF_CL(c) & 1)
#define WF_END(c) (WF_CL(c) & 2)
#define WF_M9 100000000u                 /* m < 10^8 <=> fewer than 9 significant digits taken */

/* ------------------------------------------------------------------ memory (wf_task only) */
static uint32_t wf_ll_cur, wf_ll_peak;

static void *wf_ll_alloc(size_t n, unsigned fl)
{
    void *p = __mem_malloc(n, fl, "wf_loglut.c", 0);
    if (p) {
        wf_ll_cur += n;
        if (wf_ll_cur > wf_ll_peak)
            wf_ll_peak = wf_ll_cur;
    }
    return p;
}

static void wf_ll_free(void *p, size_t n)
{
    if (p) {
        __mem_free(p);
        wf_ll_cur -= n;
    }
}

/* ------------------------------------------------------------------ .cube parser */
struct wf_ll_p {
    struct wf_ll_info *info;
    wf_ll_hdr_fn *hdr;
    wf_ll_smp_fn *smp;
    void *ctx;
    struct wf_ll_hdr h;
    int have_size, data, aux;
    int total;                           /* n^3 */
    const char *sent;                    /* the buffer's sentinel '\n' (ends a last line without one) */
    int fdok;                            /* fd[] valid: a data line ended a buffer fill with equal counts */
    int8_t fd[3];                        /* its fraction digits per number (final-line cut check) */
};

/* fraction digits of the three numbers of a (valid) data line: digits after '.', before 'e' */
static void wf_ll_fdig(const char *s, int8_t *fd)
{
    for (int i = 0; i < 3; i++) {
        int n = 0, st = 0;                         /* st 0 integer part, 1 fraction, 2 exponent */
        while (WF_WS(*s)) s++;
        for (; !(WF_CL(*s) & 3); s++) {
            if (*s == '.') st = 1;
            else if ((*s | 32) == 'e') st = 2;
            else if (st == 1 && n < 99) n++;
        }
        fd[i] = (int8_t)n;
    }
}

/* number -> Q24 (saturated to about +-128). Returns the char after it, 0 on a syntax error.
 * The first 9 significant digits are taken (m < 10^8 before a digit <=> fewer than 9 so far),
 * the rest dropped: an integer digit then scales by 10, a fraction digit is ignored. */
static const char *wf_ll_num(const char *s, int32_t *out)
{
    const uint8_t *p = (const uint8_t *)s, *q;
    int neg = 0, e10 = 0, any;
    uint32_t m = 0, d;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    q = p;
    while ((d = (uint32_t)*p - '0') < 10) {
        if (m < WF_M9) m = m * 10 + d; else e10++;
        p++;
    }
    any = p != q;
    if (*p == '.') {
        q = ++p;
        while (m < WF_M9 && (d = (uint32_t)*p - '0') < 10) {
            m = m * 10 + d;
            p++;
        }
        e10 -= p - q;
        while ((uint32_t)*p - '0' < 10)
            p++;
        any |= p != q;
    }
    if (!any)
        return 0;
    if ((*p | 32) == 'e') {
        int en = 0, es = 0, ed = 0;
        p++;
        if (*p == '-') { es = 1; p++; } else if (*p == '+') p++;
        for (; (uint32_t)*p - '0' < 10; p++) {
            if (en < 1000) en = en * 10 + (*p - '0');
            ed = 1;
        }
        if (!ed)
            return 0;
        e10 += es ? -en : en;
    }
    if (!(WF_CL(*p) & 3))
        return 0;
    s = (const char *)p;
    int32_t v;
    if (m == 0) {
        v = 0;
    } else if (e10 >= 0) {
        /* m * 10^e10 >= 1: saturate at 128 */
        if (e10 > 2 || m >= (e10 == 0 ? 128u : e10 == 1 ? 13u : 2u))
            v = WF_LL_SAT;
        else
            v = (int32_t)((m * wf_ll_p10[e10]) << 24);
    } else {
        int d = -e10;
        if (d > 26)
            v = 0;
        else if (d <= 7 && m >= 128u * wf_ll_p10[d])
            v = WF_LL_SAT;
        else
            v = (int32_t)(((uint64_t)m * wf_ll_r10[d] + 0x80000000u) >> 32);
    }
    *out = neg ? -v : v;
    return s;
}

static int wf_ll_aux(struct wf_ll_p *P)
{
    return ++P->aux > WF_LL_MAXAUX ? WF_LL_E_LINES : WF_LL_OK;
}

static int wf_ll_iskw(const char *w, int n, const char *k)
{
    int i = 0;
    for (; i < n && k[i]; i++)
        if (w[i] != k[i])
            return 0;
    return i == n && !k[i];
}

static int wf_ll_wordc(char c)
{
    return (unsigned)((c | 32) - 'a') < 26 || (unsigned)(c - '0') < 10 || c == '_';
}

/* k numbers (Q24) then the end of the line */
static int wf_ll_nums(const char *s, int32_t *v, int k)
{
    for (int i = 0; i < k; i++) {
        while (WF_WS(*s)) s++;
        if (WF_END(*s) || !(s = wf_ll_num(s, &v[i])))
            return 0;
    }
    while (WF_WS(*s)) s++;
    return WF_END(*s);
}

static int wf_ll_keyword(struct wf_ll_p *P, const char *s)
{
    const char *w = s;
    int n = 0;
    while (wf_ll_wordc(*s)) { s++; n++; }
    if (P->data)
        return WF_LL_E_SYNTAX;                     /* keywords only before the data */
    if (wf_ll_iskw(w, n, "TITLE")) {
        char *t = P->info->title;
        int i = 0;
        while (WF_WS(*s) || *s == '"') s++;
        while (*s != '\n' && *s != '"' && i < (int)sizeof P->info->title - 1)
            t[i++] = *s++;
        t[i] = 0;
        return WF_LL_OK;
    }
    if (wf_ll_iskw(w, n, "LUT_1D_SIZE") || wf_ll_iskw(w, n, "LUT_1D_INPUT_RANGE"))
        return WF_LL_E_1D;
    if (wf_ll_iskw(w, n, "LUT_3D_SIZE")) {
        int v = 0, d = 0;
        if (P->have_size)
            return WF_LL_E_SIZE;
        while (WF_WS(*s)) s++;
        for (; (unsigned)(*s - '0') < 10; s++, d++)
            if (v < 1000) v = v * 10 + (*s - '0');
        while (WF_WS(*s)) s++;
        if (!d || !WF_END(*s) || v < 2 || v > 65)
            return WF_LL_E_SIZE;
        P->have_size = 1;
        P->h.n = P->info->n = v;
        P->total = v * v * v;
        return WF_LL_OK;
    }
    if (wf_ll_iskw(w, n, "DOMAIN_MIN"))
        return wf_ll_nums(s, P->h.dmin, 3) ? WF_LL_OK : WF_LL_E_DOMAIN;
    if (wf_ll_iskw(w, n, "DOMAIN_MAX"))
        return wf_ll_nums(s, P->h.dmax, 3) ? WF_LL_OK : WF_LL_E_DOMAIN;
    if (wf_ll_iskw(w, n, "LUT_3D_INPUT_RANGE")) {
        int32_t r[2];
        if (!wf_ll_nums(s, r, 2))
            return WF_LL_E_DOMAIN;
        for (int c = 0; c < 3; c++) {
            P->h.dmin[c] = r[0];
            P->h.dmax[c] = r[1];
        }
        return WF_LL_OK;
    }
    P->info->skipped++;                            /* unknown keyword before the data */
    return WF_LL_OK;
}

static const char *wf_ll_eol(const char *s)
{
    while (*s != '\n') s++;
    return s;
}

/* a data line; *nl = its '\n' */
static int wf_ll_data(struct wf_ll_p *P, const char *s, const char **nl)
{
    struct wf_ll_info *in = P->info;
    if (!P->have_size)
        return WF_LL_E_NOSIZE;
    if (!P->data) {
        /* header complete: check the domain, then let the consumer set up */
        for (int c = 0; c < 3; c++) {
            int32_t lo = P->h.dmin[c], hi = P->h.dmax[c];
            /* (lo is checked first, so lo + 1/16 cannot overflow; hi may be saturated negative) */
            if (lo < -WF_LL_ONE || lo > WF_LL_ONE || hi > 64 * WF_LL_ONE || hi < lo + WF_LL_ONE / 16)
                return WF_LL_E_DOMAIN;
            if (lo != 0 || hi != WF_LL_ONE)
                in->domain = 1;
        }
        P->data = 1;
        if (P->hdr) {
            int r = P->hdr(P->ctx, &P->h);
            if (r)
                return r;
        }
    }
    if (in->samples >= P->total)
        return WF_LL_E_EXTRA;
    const char *s0 = s;
    int32_t v[3];
    for (int i = 0; i < 3; i++) {
        while (WF_WS(*s)) s++;
        if (WF_END(*s))                            /* a cut last line is truncated, else short */
            return wf_ll_eol(s) == P->sent ? WF_LL_E_TRUNC : WF_LL_E_SYNTAX;
        if (!(s = wf_ll_num(s, &v[i])))
            return WF_LL_E_SYNTAX;
    }
    while (WF_WS(*s)) s++;
    if (*s != '\n') {
        if (*s != '#')
            return WF_LL_E_SYNTAX;
        s = wf_ll_eol(s);
    }
    if (s == P->sent && P->fdok) {
        /* the last line, without '\n': a file cut inside its last number still has 3 numbers.
           Fixed-precision writers (the previous data line's 3 numbers have the same number of
           fraction digits) give every number that many: fewer = cut */
        int8_t fd[3];
        wf_ll_fdig(s0, fd);
        for (int i = 0; i < 3; i++)
            if (fd[i] < P->fd[i])
                return WF_LL_E_TRUNC;
    }
    *nl = s;
    uint16_t o[3];
    for (int i = 0; i < 3; i++) {
        int32_t x = v[i];
        if (x < 0) { x = 0; in->clamped++; }
        else if (x > WF_LL_ONE) { x = WF_LL_ONE; in->clamped++; }
        o[i] = (uint16_t)((x - (x >> 16) + 128) >> 8);   /* x * 65535 / 2^24, +-1 */
    }
    if (P->smp)
        P->smp(P->ctx, o);
    in->samples++;
    return WF_LL_OK;
}

/* one line (a '\n' is guaranteed before or at the sentinel); *nl = its '\n' unless an error */
static int wf_ll_line(struct wf_ll_p *P, const char *s, const char **nl)
{
    while (WF_WS(*s)) s++;
    char c = *s;
    if ((WF_CL(c) & 4) || c == '-' || c == '+' || c == '.')
        return wf_ll_data(P, s, nl);
    *nl = wf_ll_eol(s);
    if (WF_END(c))
        return wf_ll_aux(P);
    if ((unsigned)((c | 32) - 'a') < 26) {
        int r = wf_ll_keyword(P, s);
        return r ? r : wf_ll_aux(P);
    }
    return WF_LL_E_SYNTAX;
}

/* a line too long for the buffer may only be a comment or a TITLE */
static int wf_ll_skippable(const char *s)
{
    while (WF_WS(*s)) s++;
    if (*s == '#')
        return 1;
    return s[0] == 'T' && s[1] == 'I' && s[2] == 'T' && s[3] == 'L' && s[4] == 'E' && !wf_ll_wordc(s[5]);
}

int wf_ll_parse(const char *path, wf_ll_hdr_fn *hdr, wf_ll_smp_fn *smp, void *ctx, struct wf_ll_info *info)
{
    struct wf_ll_p P;
    char *z = (char *)&P;
    for (unsigned i = 0; i < sizeof P; i++) z[i] = 0;
    z = (char *)info;
    for (unsigned i = 0; i < sizeof *info; i++) z[i] = 0;
    P.info = info; P.hdr = hdr; P.smp = smp; P.ctx = ctx;
    for (int c = 0; c < 3; c++) P.h.dmax[c] = WF_LL_ONE;
    wf_ll_cur = wf_ll_peak = 0;

    uint32_t fsz = 0;
    if (!path || FIO_GetFileSize(path, &fsz) != 0)
        return WF_LL_E_OPEN;
    if (fsz > WF_LL_MAXFILE)
        return WF_LL_E_FSIZE;
    void *f = FIO_OpenFile(path, WF_O_RDONLY);
    if (!f)
        return WF_LL_E_OPEN;
    char *dma = wf_ll_alloc(WF_LL_BUF, WF_MEM_DMA | WF_MEM_TMP);
    char *buf = wf_ll_alloc(WF_LL_BUFC, WF_MEM_TMP);
    int rc = dma && buf ? WF_LL_OK : WF_LL_E_NOMEM;
    /* buf[pos..lim) = complete lines (lim is just after the last '\n' read), buf[lim..end) = the
       start of the next line (< WF_LL_BUF bytes), buf[end] = '\n' (sentinel). Every FIO read is
       WF_LL_BUF bytes at a file offset that is a multiple of WF_LL_BUF (sector-aligned, constant
       size); the partial line is moved to the front of the 2 x WF_LL_BUF cached buffer first. */
    int pos = 0, lim = 0, end = 0, eof = 0, skip = 0, first = 1;
    if (buf)
        buf[0] = '\n';
    while (!rc) {
        if (pos >= lim && !eof) {
            /* no complete line left: keep the partial one, read more after it */
            if (end - pos >= WF_LL_BUF) {          /* no '\n' in WF_LL_BUF bytes */
                if (!skip && !wf_ll_skippable(buf + pos)) {
                    info->line++;
                    rc = WF_LL_E_LINE;
                    break;
                }
                skip = 1;                          /* long comment / TITLE: drop it */
                pos = end = 0;
            } else if (pos > 0) {
                memmove(buf, buf + pos, end - pos);
                end -= pos;
                pos = 0;
            }
            int old = end;
            int n = FIO_ReadFile(f, dma, WF_LL_BUF);
            if (n < 0 || n > WF_LL_BUF) {
                rc = WF_LL_E_READ;
                break;
            }
            if (n == 0) {
                eof = 1;
            } else {
                memcpy(buf + end, dma, n);
                end += n;
                info->bytes += n;
                if (first) {
                    first = 0;
                    if (end >= 3 && (uint8_t)buf[0] == 0xEF && (uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF)
                        pos = 3;                   /* UTF-8 BOM */
                }
            }
            buf[end] = '\n';
            P.sent = buf + end;
            lim = 0;                               /* the kept part has no '\n' */
            for (int i = end - 1; i >= old; i--)
                if (buf[i] == '\n') {
                    lim = i + 1;
                    break;
                }
            continue;
        }
        if (pos >= end)
            break;                                 /* end of file */
        info->line++;                              /* (after lim: the last line, no '\n') */
        const char *nl = 0;
        if (!skip && end - pos >= WF_LL_BUF && wf_ll_eol(buf + pos) - (buf + pos) >= WF_LL_BUF) {
            /* a line of >= WF_LL_BUF bytes that still fit the 2 x WF_LL_BUF window: the same rule
               as at a refill, so the result never depends on where the reads fall (only the first
               line after a refill can be this long: cheap) */
            if (!wf_ll_skippable(buf + pos)) {
                rc = WF_LL_E_LINE;
                break;
            }
            skip = 1;
        }
        if (skip) {
            skip = 0;
            nl = wf_ll_eol(buf + pos);
        } else {
            int ns = info->samples;
            rc = wf_ll_line(&P, buf + pos, &nl);
            if (!rc && nl + 1 == buf + lim && info->samples != ns) {
                /* a data line that ends this fill: the final line's predecessor is one of these */
                wf_ll_fdig(buf + pos, P.fd);
                P.fdok = P.fd[0] > 0 && P.fd[0] == P.fd[1] && P.fd[1] == P.fd[2];
            }
        }
        if (!rc)
            pos = nl < P.sent ? (int)(nl - buf) + 1 : end;
    }
    if (!rc) {
        if (!P.have_size)
            rc = WF_LL_E_NOSIZE;
        else if (info->samples != P.total)
            rc = WF_LL_E_TRUNC;
    }
    FIO_CloseFile(f);
    wf_ll_free(buf, WF_LL_BUFC);
    wf_ll_free(dma, WF_LL_BUF);
    info->peak = wf_ll_peak;
    return rc;
}

/* ------------------------------------------------------------------ composite builder */
/* YCbCr -> R'G'B', BT.601 full range (JFIF), Q20 */
#define K_RV 1470104                             /* 1.402    */
#define K_GU  360857                             /* 0.344136 */
#define K_GV  748830                             /* 0.714136 */
#define K_BU 1858077                             /* 1.772    */

struct wf_ll_b {
    int n, nn3, gmax;
    struct wf_lltabs *out;
    uint16_t *slab;                              /* two B-slabs of n*n RGB samples */
    uint8_t *kb;                                 /* per node: the blue grid cell */
    const int32_t *mat;
    const uint16_t *enc;
    int32_t eik, eib, eid;                       /* LUT EI: gain Q16 (0 = none), the curve's log offset b (Q24) and step
                                                    per stop (log code Q20), see wf_ll_coords */
    int32_t off[3], K[3];                        /* domain: g = (code_Q20 - off) * K >> 32 */
    int ir, ig, ib;
    uint16_t *wp;
};

#define WF_LL_NN (256 + WF_LL_NODES)

int wf_ll_ei;                                    /* LUT EI in 1/3 EV (wf_loglut.h); wf_task sets it before a load */

/* node -> grid coordinates (Q16, 0 .. (n-1) << 16) */
static void wf_ll_coords(const struct wf_ll_b *B, int y, int cb, int cr, int32_t *g)
{
    int32_t base = y << 20;
    int32_t q[3] = { base + K_RV * cr, base - K_GU * cb - K_GV * cr, base + K_BU * cb };
    int32_t lin[3];
    for (int c = 0; c < 3; c++) {
        int32_t v = q[c] < 0 ? 0 : q[c] > (255 << 20) ? (255 << 20) : q[c];
        int idx = v >> 18;
        uint32_t fr = (v >> 4) & 0x3fff, d0 = wf_ll_dec[idx];
        lin[c] = (int32_t)(d0 + (((wf_ll_dec[idx + 1] - d0) * fr) >> 14));
    }
    for (int c = 0; c < 3; c++) {
        const int32_t *m = B->mat + 3 * c;
        int64_t s = (int64_t)m[0] * lin[0] + (int64_t)m[1] * lin[1] + (int64_t)m[2] * lin[2];
        int32_t x = (int32_t)((s + (1 << 19)) >> 20), up = 0;
        x = x < 0 ? 0 : x;
        if (B->eik) {                            /* LUT EI x auto: gain x scene light (<= 8.0: fits, x <= 2^28) */
            x = (int32_t)(((int64_t)x * B->eik + 32768) >> 16);
            if (x > WF_LL_ONE) {                 /* above white each curve is a log10(x + b) + c, so with x / 2^up <= 1: */
                int32_t h = x;                   /* log(x) = log((x + b) / 2^up - b) + up a log10 2, exact */
                while (h > WF_LL_ONE) { h >>= 1; up++; }
                x = (int32_t)(((int64_t)x + B->eib + (1 << (up - 1))) >> up) - B->eib;
            }
        }
        x = x > WF_LL_ONE ? WF_LL_ONE : x;
        int idx = x >> 12;
        int32_t u;
        if (idx >= 4096) {
            u = (int32_t)B->enc[4096] << 12;
        } else {
            int32_t e0 = B->enc[idx];
            u = (e0 << 12) + ((int32_t)B->enc[idx + 1] - e0) * (x & 4095);
        }
        u += up * B->eid;
        int32_t gg = (int32_t)(((int64_t)(u - B->off[c]) * B->K[c]) >> 32);
        g[c] = gg < 0 ? 0 : gg > B->gmax ? B->gmax : gg;
    }
}

/* tetrahedral at the grid point g, blue cell k (slabs k and k+1 in the ring) -> Y, U, V codes */
static void wf_ll_eval(const struct wf_ll_b *B, int k, const int32_t *g, int *o)
{
    int n = B->n, i0, i1;
    uint32_t f[3];
    i0 = g[0] >> 16; if (i0 > n - 2) i0 = n - 2;
    i1 = g[1] >> 16; if (i1 > n - 2) i1 = n - 2;
    f[0] = (uint32_t)(g[0] - (i0 << 16));
    f[1] = (uint32_t)(g[1] - (i1 << 16));
    f[2] = (uint32_t)(g[2] - (k << 16));
    const uint16_t *p0 = B->slab + (k & 1) * B->nn3 + (i1 * n + i0) * 3;
    int st[3] = { 3, 3 * n, (k & 1) ? -B->nn3 : B->nn3 };
    int a = 0, b = 1, c = 2, t;
    if (f[a] < f[b]) { t = a; a = b; b = t; }
    if (f[b] < f[c]) { t = b; b = c; c = t; }
    if (f[a] < f[b]) { t = a; a = b; b = t; }
    const uint16_t *p1 = p0 + st[a], *p2 = p1 + st[b], *p3 = p2 + st[c];
    uint32_t w0 = 65536 - f[a], w1 = f[a] - f[b], w2 = f[b] - f[c], w3 = f[c];
    int32_t h[3];
    for (int ch = 0; ch < 3; ch++)                 /* <= 65536 * 65535: fits u32 */
        h[ch] = (int32_t)((w0 * p0[ch] + w1 * p1[ch] + w2 * p2[ch] + w3 * p3[ch]) >> 1);
    for (int r = 0; r < 3; r++) {
        const int32_t *A = wf_ll_ycc[r];
        int64_t s = (int64_t)A[0] * h[0] + (int64_t)A[1] * h[1] + (int64_t)A[2] * h[2];
        o[r] = (int)((s + (1LL << 47)) >> 48);
    }
}

/* node i -> grid coordinates. The node set of wf_llk.c, in kb[] order: N (y = 0..255,
   u = v = 128), then level l (y = 8 l), U node b, V node c (u = 8 b, v = 8 c), l, b, c = 0..32.
   y = 256 and u, v = 256 (Cb, Cr = +128) are outside the JPEG's codes but valid inputs of the
   composite (R'G'B' is clamped to 0..255 first): they are the far ends of the last cells. */
static void wf_ll_nodeg(const struct wf_ll_b *B, int i, int32_t *g)
{
    if (i < 256) {
        wf_ll_coords(B, i, 0, 0, g);
        return;
    }
    /* j / 1089 and r / 33 by exact reciprocals (checked for every j < 35937, r < 1089): no
       __aeabi_uidiv call per node */
    uint32_t j = (uint32_t)(i - 256), l = (j * 30813u) >> 25, r = j - l * WF_LL_LSTEP;
    uint32_t b = (r * 993u) >> 15, c = r - b * WF_LL_NC;
    wf_ll_coords(B, (int)l * 8, (int)b * 8 - 128, (int)c * 8 - 128, g);
}

/* first pass: the blue grid cell of every node */
static void wf_ll_prepass(struct wf_ll_b *B)
{
    for (int i = 0; i < WF_LL_NN; i++) {
        int32_t g[3];
        wf_ll_nodeg(B, i, g);
        int c = g[2] >> 16;
        B->kb[i] = (uint8_t)(c > B->n - 2 ? B->n - 2 : c);
    }
}

/* evaluate the nodes in blue cell k (slabs k and k+1 complete) */
static void wf_ll_cell(struct wf_ll_b *B, int k)
{
    const uint8_t *kb = B->kb, *p = kb, *pe = kb + WF_LL_NN;
    for (;;) {
        while (p < pe && *p != k) p++;
        if (p >= pe)
            return;
        int i = p++ - kb;
        int32_t g[3];
        int o[3];
        wf_ll_nodeg(B, i, g);
        wf_ll_eval(B, k, g, o);
        int Y = o[0] < 0 ? 0 : o[0] > 255 ? 255 : o[0];
        int U = o[1] < -128 ? -128 : o[1] > 127 ? 127 : o[1];
        int V = o[2] < -128 ? -128 : o[2] > 127 ? 127 : o[2];
        if (i < 256) {
            B->out->N[i] = (uint8_t)Y;
        } else {
            B->out->C[i - 256] = (uint32_t)(2 * U + 257) | (uint32_t)(2 * V + 257) << 16;
            B->out->Y[i - 256] = (uint8_t)Y;
        }
    }
}

/* n / d on 64-bit values by shift and subtract (load time only): no __aeabi_uldivmod in the module (docs/DESIGN.md 3,
   the camera loader trap); saturates at 2^32 - 1 */
static uint32_t wf_ll_udiv64(uint64_t n, uint64_t d)
{
    uint64_t q = 0, r = 0;
    if (!d)
        return 0xffffffffu;
    for (int b = 63; b >= 0; b--) {
        r = r << 1 | ((n >> b) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << b; }
    }
    return q > 0xffffffffu ? 0xffffffffu : (uint32_t)q;
}

static int wf_ll_hdr_cb(void *ctx, const struct wf_ll_hdr *h)
{
    struct wf_ll_b *B = ctx;
    int n = h->n;
    B->n = n;
    B->nn3 = n * n * 3;
    B->gmax = (n - 1) << 16;
    for (int c = 0; c < 3; c++) {
        uint32_t r24 = (uint32_t)(h->dmax[c] - h->dmin[c]);            /* >= 2^20 (checked) */
        B->K[c] = (int32_t)wf_ll_udiv64((uint64_t)(n - 1) << 52, (uint64_t)r24 * 255u);
        B->off[c] = (int32_t)(((int64_t)h->dmin[c] * 255) >> 4);      /* Q24 -> code Q20 */
    }
    B->slab = wf_ll_alloc((size_t)2 * B->nn3 * sizeof(uint16_t), WF_MEM_TMP);
    B->kb = wf_ll_alloc(WF_LL_NN, WF_MEM_TMP);
    if (!B->slab || !B->kb)
        return WF_LL_E_NOMEM;
    B->wp = B->slab;
    wf_ll_prepass(B);
    return WF_LL_OK;
}

static void wf_ll_smp(void *ctx, const uint16_t *rgb)
{
    struct wf_ll_b *B = ctx;
    uint16_t *w = B->wp;
    w[0] = rgb[0]; w[1] = rgb[1]; w[2] = rgb[2];
    B->wp = w + 3;
    if (++B->ir < B->n)
        return;
    B->ir = 0;
    if (++B->ig < B->n)
        return;
    B->ig = 0;
    if (B->ib > 0)
        wf_ll_cell(B, B->ib - 1);                   /* slabs ib-1 and ib complete */
    B->ib++;
    B->wp = B->slab + (B->ib & 1) * B->nn3;
}

/* round(2^(t/3) 65536), t = WF_LL_EI_MIN..WF_LL_EI_MAX; b of the log segment (Q24) and a log10(2) in log code Q20
   (x 65280 x 4096): S-Log3 a = 261.5 / 1023, b = 0.01; V-Log a = 0.241514, b = 0.00873; LogC3 EI800 a = 0.247190,
   b = 0.052272 / 5.555556; Canon Log 2 a = 0.24136077, b = 0.9 / 87.09937546; Canon Log 3 a = 0.36726845,
   b = 0.9 / 14.98325 (its log segment, x / 0.9 > 0.014; mk_logtab.py clog2 / clog3: x / 0.9 inside the log) */
static const int32_t wf_ll_eik[WF_LL_EI_MAX - WF_LL_EI_MIN + 1] = { 32768, 41285, 52016, 65536, 82570, 104032, 131072,
                                                                    165140, 208064, 262144 };
static const int32_t wf_ll_eib[WF_LL_NCURVE] = { 167772, 146465, 157856, 173359, 1007758 },
                     wf_ll_eid[WF_LL_NCURVE] = { 20575288, 19439817, 19896687, 19427483, 29561978 };

int wf_ll_auto;                                  /* wf_loglut.h: 1 = expose the cube for 18 % grey -> 118 */

/* one build of the tables at scene-light gain `gain` (Q16, 65536 = none) */
static int wf_ll_build(const char *path, int mode, struct wf_lltabs *out, struct wf_ll_info *info, int32_t gain)
{
    struct wf_ll_b B;
    char *z = (char *)&B;
    for (unsigned i = 0; i < sizeof B; i++) z[i] = 0;
    B.out = out;
    B.mat = wf_ll_mat[mode - 1];
    B.enc = wf_ll_enc[mode - 1];
    if (gain != 65536) {
        B.eik = gain;
        B.eib = wf_ll_eib[mode - 1];
        B.eid = wf_ll_eid[mode - 1];
    }
    int rc = wf_ll_parse(path, wf_ll_hdr_cb, wf_ll_smp, &B, info);
    wf_ll_free(B.kb, WF_LL_NN);
    wf_ll_free(B.slab, (size_t)2 * B.nn3 * sizeof(uint16_t));
    return rc;
}

/* input luma (Q8) where the neutral axis N first rises above t2 / 2; -1: N[0] is above already, 256 << 8: never */
static int wf_ll_cross(const uint8_t *N, int t2)
{
    if (2 * N[0] > t2)
        return -1;
    for (int i = 0; i < 255; i++)
        if (2 * N[i + 1] > t2)                   /* 2 N[i] <= t2 < 2 N[i + 1] */
            return (i << 8) + ((t2 - 2 * N[i]) << 8) / (2 * (N[i + 1] - N[i]));
    return 256 << 8;
}

/* linear light (Q24) of an input luma in Q8 (the node decoding of wf_ll_coords) */
static uint32_t wf_ll_lin8(int y8)
{
    int idx = y8 >> 6;
    uint32_t fr = (uint32_t)(y8 << 8) & 0x3fff, d0 = wf_ll_dec[idx];
    return d0 + (((wf_ll_dec[idx + 1] - d0) * fr) >> 14);
}

/* n / d for n < 2^40 by shift and subtract: GCC leaves a __aeabi_ldivmod reference for a 64-bit division here,
   and the camera's TCC loader has none (build.sh link check) */
static uint32_t wf_ll_div40(uint64_t n, uint32_t d)
{
    uint64_t q = 0, r = 0;
    for (int b = 39; b >= 0; b--) {
        r = r << 1 | ((n >> b) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << b; }
    }
    return q > 0xffffffffu ? 0xffffffffu : (uint32_t)q;
}

/* round(10 log2(g / 65536)), g > 0 (Q16) */
static int wf_ll_ev10(uint32_t g)
{
    static const uint32_t mid[10] = { 67847, 72717, 77936, 83530, 89525, 95950, 102837, 110218, 118129, 126607 };
    int e = 0, k = 0;
    while (g >= 131072) { g >>= 1; e += 10; }
    while (g < 65536) { g <<= 1; e -= 10; }
    while (k < 10 && g >= mid[k]) k++;           /* mid[k] = 2^((k + 0.5) / 10) 65536 */
    return e + k;
}

/* Film LUT cubes (wf_ll_auto): built once at gain 1, the input luma whose output is 118 (the middle of the 117.5 and
   118.5 crossings of the neutral axis) gives the gain that puts 18 % grey (input 118) there: lin(y*) / lin(118),
   0.25 .. 4. The LUT EI multiplies it (0.125 .. 8 in all), then the tables are built again (unless the gain is within
   1/40 EV of 1). pad[0] = the auto part in 0.1 EV (int8), pad[1] = 0xa5 (auto on), kept in the .FRT. */
int wf_loglut_load(const char *path, int mode, struct wf_lltabs *out, struct wf_ll_info *info)
{
    struct wf_ll_info tmp;
    if (!info)
        info = &tmp;
    if (mode < WF_LL_SLOG3 || mode > WF_LL_NCURVE || !out) {
        char *z = (char *)info;
        for (unsigned i = 0; i < sizeof *info; i++) z[i] = 0;
        return WF_LL_E_MODE;
    }
    int ei = wf_ll_ei >= WF_LL_EI_MIN && wf_ll_ei <= WF_LL_EI_MAX ? wf_ll_ei : 0;
    int32_t gain = wf_ll_eik[ei - WF_LL_EI_MIN], ga = 65536;
    int rc = wf_ll_build(path, mode, out, info, wf_ll_auto ? 65536 : gain);
    if (!rc && wf_ll_auto) {
        int lo = wf_ll_cross(out->N, 235), hi = wf_ll_cross(out->N, 237);
        if (lo < 0 || hi < 0)
            ga = 16384;                          /* grey comes out above 118 even from black: the darkest */
        else if (lo > 255 << 8 || hi > 255 << 8)
            ga = 262144;                         /* never reaches 118: the brightest */
        else {
            uint32_t g = wf_ll_div40((uint64_t)wf_ll_lin8((lo + hi + 1) >> 1) << 16, wf_ll_lin8(118 << 8));
            ga = g < 16384 ? 16384 : g > 262144 ? 262144 : (int32_t)g;
        }
        uint64_t t = ((uint64_t)(uint32_t)ga * (uint32_t)gain + 32768) >> 16;   /* unsigned: no __aeabi_ldivmod */
        int32_t tot = t < 8192 ? 8192 : t > 524288 ? 524288 : (int32_t)t;
        if (tot < 64410 || tot > 66682)          /* off by more than 1/40 EV: build at the corrected gain */
            rc = wf_ll_build(path, mode, out, info, tot);
    }
    if (!rc) {                                     /* the node Y' on the neutral axis equals 8 l's N */
        for (int l = 0; l < WF_LL_NL; l++)
            out->L[l] = 8 * out->N[l * 8 > 255 ? 255 : l * 8] - 4 - 8 * WF_LL_KOFF;
        for (int j = 0; j < (int)sizeof out->K; j++)
            out->K[j] = (uint8_t)(j < 512 ? 0 : j > 512 + 255 ? 255 : j - 512);
        out->pad[0] = (uint8_t)(int8_t)(wf_ll_auto ? wf_ll_ev10((uint32_t)ga) : 0);
        out->pad[1] = wf_ll_auto ? 0xa5 : 0;
        out->pad[2] = 0;
    }
    return rc;
}

/* ------------------------------------------------------------------ table cache */
/* file = struct wf_llc_hdr, struct wf_lltabs, FNV-1a (u32) of both. The key (first 8 words) says
   which builder made it from which cube; bump WF_LLC_VER whenever the tables of a cube change. */
#define WF_LLC_MAGIC 0x544c5246u                 /* "FRLT" */
#define WF_LLC_VER   3                           /* 3: 33 x 33 x 33 tables, kernel layout (round 2) */
#define WF_LLC_KEY   8
#define WF_LLC_BLK   512                         /* spaced content samples: 16 sector reads */
#define WF_LLC_NBLK  16
struct wf_llc_hdr {
    uint32_t magic, ver, mode, tabsize;
    uint32_t csize, ctlo, cthi, chash;           /* cube: size, time stamp (lo, hi), FNV-1a of the samples */
    int32_t n, samples, clamped, skipped, domain;
    char title[24];
};

/* DryOS directory entries are opaque to modules: alloc_fio_file + convert_fio_file_info (fio-ml) */
struct fio_file;
struct wf_file_info { uint16_t mode; uint32_t size; uint64_t timestamp; char name[0x80]; };
extern struct fio_file *alloc_fio_file(void);
extern struct wf_file_info convert_fio_file_info(struct fio_file *f);
extern void *FIO_FindFirstEx(const char *dir, struct fio_file *f);   /* error: 0 or odd */
extern int   FIO_FindNextEx(void *d, struct fio_file *f);             /* 0 = next entry */
extern void  FIO_FindClose(void *d);
extern void *FIO_CreateFile(const char *name);                        /* 0 on error */
extern int   FIO_WriteFile(void *f, const void *buf, size_t n);
extern int   FIO_RemoveFile(const char *name);
extern int64_t FIO_SeekSkipFile(void *f, int64_t pos, int whence);    /* new position, < 0 on error */
#define WF_SEEK_SET  0

static uint32_t wf_llc_fnv(uint32_t h, const uint8_t *p, uint32_t n)
{
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static int wf_llc_name(const char *a, const char *b)       /* same name, ignoring case */
{
    for (;; a++, b++) {
        int x = (uint8_t)*a, y = (uint8_t)*b;
        x -= (unsigned)(x - 'a') < 26 ? 32 : 0;
        y -= (unsigned)(y - 'a') < 26 ? 32 : 0;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/* size and time stamp of a file, from its directory listing; 0 = found. noinline, like the other
   cache helpers: their locals (dir[48], a 144-byte file info) must not stay on the 4 KB stack of
   wf_task during the parse (review F4) */
static int __attribute__((noinline)) wf_llc_stat(const char *path, struct wf_llc_hdr *k)
{
    char dir[48];
    const char *nm = path;
    for (const char *s = path; *s; s++)
        if (*s == '/')
            nm = s + 1;
    int dl = nm - path, rc = -1;
    if (dl >= (int)sizeof dir)
        return -1;
    memcpy(dir, path, dl);
    dir[dl] = 0;
    struct fio_file *f = alloc_fio_file();
    if (!f)
        return -1;
    void *d = FIO_FindFirstEx(dir, f);
    if (d && !((uintptr_t)d & 1)) {
        do {
            struct wf_file_info fi = convert_fio_file_info(f);
            if (!(fi.mode & 0x10) && wf_llc_name(fi.name, nm)) {
                k->csize = fi.size;
                k->ctlo = (uint32_t)fi.timestamp;
                k->cthi = (uint32_t)(fi.timestamp >> 32);
                rc = 0;
                break;
            }
        } while (FIO_FindNextEx(d, f) == 0);
        FIO_FindClose(d);
    }
    __mem_free(f);
    return rc;
}

/* content key of the cube: FNV-1a of its first 8 KB and of 16 sector-aligned 512-byte blocks at
   k/16 of the file (k = 1..15) and at its last sector. Two cubes of the same size and time stamp
   (fixed-width %.6f data; files unzipped or copied with their time) then differ in the key unless
   they differ only by a small local edit (review F3). 0 = not readable (no cache). */
static uint32_t __attribute__((noinline)) wf_llc_key(const char *cube, uint32_t size, char *dma)
{
    void *f = FIO_OpenFile(cube, WF_O_RDONLY);
    if (!f)
        return 0;
    uint32_t h = 2166136261u;
    int n = FIO_ReadFile(f, dma, WF_LL_BUF);
    int ok = n >= 0 && n <= WF_LL_BUF;
    if (ok)
        h = wf_llc_fnv(h, (const uint8_t *)dma, (uint32_t)n);
    for (int k = 1; ok && k <= WF_LLC_NBLK && size > WF_LL_BUF; k++) {
        uint32_t off = (k < WF_LLC_NBLK ? (size >> 4) * k : size - 1) & ~(uint32_t)(WF_LLC_BLK - 1);
        if (off < WF_LL_BUF)
            continue;                              /* inside the head */
        ok = FIO_SeekSkipFile(f, off, WF_SEEK_SET) == (int64_t)off;
        n = ok ? FIO_ReadFile(f, dma, WF_LLC_BLK) : -1;
        ok = n > 0 && n <= WF_LLC_BLK;
        if (ok)
            h = wf_llc_fnv(h ^ off, (const uint8_t *)dma, (uint32_t)n);
    }
    FIO_CloseFile(f);
    return ok ? (h ? h : 1) : 0;
}

/* n bytes from / to a file through the DMA buffer, hashing them */
static int wf_llc_io(void *f, char *dma, uint8_t *p, uint32_t n, uint32_t *hash, int wr)
{
    while (n) {
        int k = n > WF_LL_BUF ? WF_LL_BUF : (int)n;
        if (wr) {
            memcpy(dma, p, k);
            if (FIO_WriteFile(f, dma, k) != k)
                return -1;
        } else {
            if (FIO_ReadFile(f, dma, k) != k)
                return -1;
            memcpy(p, dma, k);
        }
        if (hash)
            *hash = wf_llc_fnv(*hash, p, k);
        p += k;
        n -= k;
    }
    return 0;
}

#define WF_LLC_FILE (sizeof(struct wf_llc_hdr) + sizeof(struct wf_lltabs) + 4)

static int __attribute__((noinline)) wf_llc_read(const char *cache, const struct wf_llc_hdr *key, char *dma,
                                                 struct wf_lltabs *out, struct wf_ll_info *info)
{
    uint32_t sz = 0, hash = 2166136261u, tail = 0;
    struct wf_llc_hdr h;
    if (FIO_GetFileSize(cache, &sz) != 0 || sz != WF_LLC_FILE)
        return -1;
    void *f = FIO_OpenFile(cache, WF_O_RDONLY);
    if (!f)
        return -1;
    int rc = wf_llc_io(f, dma, (uint8_t *)&h, sizeof h, &hash, 0);
    for (int i = 0; !rc && i < WF_LLC_KEY; i++)
        if (((const uint32_t *)&h)[i] != ((const uint32_t *)key)[i])
            rc = -1;
    if (!rc && (h.n < 2 || h.n > 65))
        rc = -1;
    if (!rc)
        rc = wf_llc_io(f, dma, (uint8_t *)out, sizeof *out, &hash, 0);
    if (!rc)
        rc = wf_llc_io(f, dma, (uint8_t *)&tail, 4, 0, 0);
    FIO_CloseFile(f);
    if (rc || tail != hash)
        return -1;
    info->n = h.n;
    info->samples = h.samples;
    info->clamped = h.clamped;
    info->skipped = h.skipped;
    info->domain = h.domain;
    info->bytes = sz;
    info->peak = WF_LL_BUF;
    info->cached = 1;
    memcpy(info->title, h.title, sizeof h.title);
    info->title[sizeof info->title - 1] = 0;
    return 0;
}

static void __attribute__((noinline)) wf_llc_write(const char *cache, struct wf_llc_hdr *h, char *dma,
                                                   struct wf_lltabs *t, const struct wf_ll_info *info)
{
    uint32_t hash = 2166136261u;
    h->n = info->n;
    h->samples = info->samples;
    h->clamped = info->clamped;
    h->skipped = info->skipped;
    h->domain = info->domain;
    memcpy(h->title, info->title, sizeof h->title);
    FIO_RemoveFile(cache);
    void *f = FIO_CreateFile(cache);
    if (!f)
        return;
    int rc = wf_llc_io(f, dma, (uint8_t *)h, sizeof *h, &hash, 1);
    if (!rc)
        rc = wf_llc_io(f, dma, (uint8_t *)t, sizeof *t, &hash, 1);
    if (!rc)
        rc = wf_llc_io(f, dma, (uint8_t *)&hash, 4, 0, 1);
    FIO_CloseFile(f);
    if (rc)
        FIO_RemoveFile(cache);                   /* a cut file would fail its size check anyway */
}

int wf_loglut_load_cached(const char *cube, const char *cache, int mode, struct wf_lltabs *out,
                          struct wf_ll_info *info)
{
    struct wf_ll_info tmp;
    struct wf_llc_hdr k;
    int key = 0;
    if (!info)
        info = &tmp;
    char *z = (char *)&k;
    for (unsigned i = 0; i < sizeof k; i++) z[i] = 0;
    if (cube && cache && out && mode >= WF_LL_SLOG3 && mode <= WF_LL_NCURVE) {
        char *dma = __mem_malloc(WF_LL_BUF, WF_MEM_DMA | WF_MEM_TMP, "wf_loglut.c", 0);
        if (dma) {
            if (wf_llc_stat(cube, &k) == 0 && (k.chash = wf_llc_key(cube, k.csize, dma)) != 0) {
                k.magic = WF_LLC_MAGIC;
                k.ver = WF_LLC_VER;
                k.mode = (uint32_t)mode | (uint32_t)(wf_ll_ei & 0xff) << 8    /* LUT EI 0, no auto: */
                         | (uint32_t)(wf_ll_auto != 0) << 16;                 /* the key as before */
                k.tabsize = sizeof *out;
                key = 1;
                z = (char *)info;
                for (unsigned i = 0; i < sizeof *info; i++) z[i] = 0;
                if (wf_llc_read(cache, &k, dma, out, info) == 0) {
                    __mem_free(dma);
                    return WF_LL_OK;
                }
            }
            __mem_free(dma);
        }
    }
    int rc = wf_loglut_load(cube, mode, out, info);
    if (!rc && key) {
        char *dma = __mem_malloc(WF_LL_BUF, WF_MEM_DMA | WF_MEM_TMP, "wf_loglut.c", 0);
        if (dma) {
            wf_llc_write(cache, &k, dma, out, info);
            __mem_free(dma);
        }
    }
    return rc;
}

const char *wf_flut_name[WF_FLUT_N] = { "Technical", "Vision3 250D", "Vision3 500T", "Portra 400", "Pro 400H",
                                       "Fuji 3513", "Kodak 2383 v3", "Kodak 2383 v5", "Kodak Ektar", "Tung Neutral", "Luca Bono",
                                       "Tung EKTA P", "Tung 250D P", "Tung 250D M", "Tung 500T P", "Tung 500T M", "Tung EKTA M",
                                       "Tung EKTAa P", "Tung EKTAa M", "Tung EXR P", "Tung EXR M", "Tung EXRa P", "Tung EXRa M" };
const char *const wf_flut_code[WF_FLUT_N] = { "", "V250D", "V500T", "PRT400", "FP400H",
                                             "FJ3513", "KD83V3", "KD83V5", "EKTAR5", "TUNGNT", "LBONO", "EKTAP",
                                             "T250DP", "T250DM", "T500TP", "T500TM", "TEKTAM", "TEKAAP", "TEKAAM", "TEXRP", "TEXRM",
                                             "TEXRAP", "TEXRAM" };

/* V14T (owner 2026-11-13 "slim it"): the file names are built from wf_flut_code (padded with '_' to six characters) instead of
 * two 23 x 5 tables of strings; the Technical names stay fixed strings so wf_loglut_path(mode) pointers never change. */
static const char *const wf_ll_tech[2][WF_LL_NCURVE] = {
    { "ML/DATA/SLOG3.CUB", "ML/DATA/VLOG.CUB", "ML/DATA/LOGC3.CUB", "ML/DATA/CLOG2.CUB", "ML/DATA/CLOG3.CUB" },
    { "ML/DATA/SLOG3.FRT", "ML/DATA/VLOG.FRT", "ML/DATA/LOGC3.FRT", "ML/DATA/CLOG2.FRT", "ML/DATA/CLOG3.FRT" } };

const char *wf_loglut_name(char *b, int mode, int lut, int frt)
{
    static const char suf[WF_LL_NCURVE][2] = { { 'S', '3' }, { 'V', 'L' }, { 'L', 'C' }, { 'C', '2' }, { 'C', '3' } };
    if (mode < WF_LL_SLOG3 || mode > WF_LL_NCURVE)
        return 0;
    if (lut <= 0 || lut >= WF_FLUT_N)
        return wf_ll_tech[frt != 0][mode - 1];
    const char *c = wf_flut_code[lut];
    char *p = b + 8;
    memcpy(b, "ML/DATA/", 8);
    for (int i = 0; i < 6; i++)
        *p++ = *c ? *c++ : '_';                 /* "V250D" -> "V250D_" */
    *p++ = suf[mode - 1][0];
    *p++ = suf[mode - 1][1];
    memcpy(p, frt ? ".FRT" : ".CUB", 5);
    return b;
}

const char *wf_loglut_path(int mode)
{
    return mode >= WF_LL_SLOG3 && mode <= WF_LL_NCURVE ? wf_ll_tech[0][mode - 1] : 0;
}

const char *wf_loglut_cache_path(int mode)
{
    return mode >= WF_LL_SLOG3 && mode <= WF_LL_NCURVE ? wf_ll_tech[1][mode - 1] : 0;
}

const char *wf_loglut_errstr(int err)
{
    static const char *const s[WF_LL_E_N] = {
        "ok", "no file", "read error", "no memory", "file > 32 MB", "1D LUT", "no LUT_3D_SIZE",
        "bad 3D size", "bad DOMAIN", "bad data line", "truncated", "extra data", "line too long",
        "too many lines", "bad mode" };
    return err >= 0 && err < WF_LL_E_N ? s[err] : "?";
}
