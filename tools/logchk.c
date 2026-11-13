/* logchk.c - in-repo bit-exact gate of the plain log output modes (S-Log3 / V-Log / LogC3 / Canon Log 2 / 3), run by
 * test.sh (mode LOGCHK) under qemu-arm -cpu arm946, an ARMv5TE CPU: an ARMv6+ instruction in the kernel traps.
 *   candidate  the shipped wf_logout.o of build.sh (wf_logout_arm.S + wf_logout_tab.c), linked unchanged
 *   reference  wf_logout_ref.c, the C reference, built by test.sh with build.sh's MODFLAGS and
 *              -Dwf_log_words=ref_log_words -Dwf_lenc=ref_lenc (its own copy of the tables)
 * usage: logchk MODE [NRAND]       MODE 1..5 (test.sh runs the five modes in parallel), NRAND default 10000000
 * Sets per mode (one line each, then "OK bit-exact" or "FAIL"):
 *   A       all 2^24 (U, V, Y0) with Y1 = Y0
 *   B       all 2^24 (U, V, Y1) with Y0 = 255 - Y1 (the two pixels of a word differ)
 *           A and B in chunks of 288 - (chunk % 4) words, so all four n % 4 entry paths of the x4 unrolled loop
 *           run, each chunk at word offset (chunk % 32) of the guarded area
 *   random  NRAND xorshift32 words, chunk lengths 1..288 at random word offsets 0..31
 *   small   n = 1..16 at every word offset 0..31 (n % 4 = 0..3, no full pass / one / several passes), random words
 *   n<=0    n = 0, -1, -2, -3, -4, -288, INT_MIN at offsets 0..31: nothing may be written
 * Every call: the whole area outside [p, p + n) holds guard words (a new random pattern per call) and every
 * changed one counts as a write outside the buffer (candidate and reference); the candidate is called through
 * kept_call, which checks that r4-r11 and sp come back unchanged (AAPCS callee-saved). Any mismatch, guard
 * write or register error: FAIL, exit 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

extern void wf_log_words(uint32_t *p, int n, int mode);     /* wf_logout.o (asm) */
extern void ref_log_words(uint32_t *p, int n, int mode);    /* wf_logout_ref.c, renamed */

/* wf_log_words(p, n, mode) with r4..r11 = known values; returns 0 when r4..r11 and sp are unchanged after it,
 * else a bit per bad register (bit 4..11) and bit 13 for sp */
int kept_call(uint32_t *p, int n, int mode);
__asm__(
    "    .text\n"
    "    .align 2\n"
    "    .arm\n"
    "    .global kept_call\n"
    "    .type kept_call, %function\n"
    "kept_call:\n"
    "    push  {r4-r11, lr}\n"
    "    ldr   r12, =kc_sp\n"
    "    str   sp, [r12]\n"
    "    ldr   r4, =0xC0DE0004\n"
    "    ldr   r5, =0x5A5A0005\n"
    "    ldr   r6, =0x0F0F0006\n"
    "    ldr   r7, =0xF00D0007\n"
    "    ldr   r8, =0x8BADF008\n"
    "    ldr   r9, =0x12340009\n"
    "    ldr   r10, =0xDEAD000A\n"
    "    ldr   r11, =0xBEEF000B\n"
    "    bl    wf_log_words\n"
    "    mov   r0, #0\n"
    "    ldr   r12, =kc_sp\n"
    "    ldr   r12, [r12]\n"
    "    cmp   r12, sp\n"
    "    orrne r0, r0, #0x2000\n"
    "    ldr   r12, =0xC0DE0004\n"
    "    cmp   r4, r12\n"
    "    orrne r0, r0, #0x10\n"
    "    ldr   r12, =0x5A5A0005\n"
    "    cmp   r5, r12\n"
    "    orrne r0, r0, #0x20\n"
    "    ldr   r12, =0x0F0F0006\n"
    "    cmp   r6, r12\n"
    "    orrne r0, r0, #0x40\n"
    "    ldr   r12, =0xF00D0007\n"
    "    cmp   r7, r12\n"
    "    orrne r0, r0, #0x80\n"
    "    ldr   r12, =0x8BADF008\n"
    "    cmp   r8, r12\n"
    "    orrne r0, r0, #0x100\n"
    "    ldr   r12, =0x12340009\n"
    "    cmp   r9, r12\n"
    "    orrne r0, r0, #0x200\n"
    "    ldr   r12, =0xDEAD000A\n"
    "    cmp   r10, r12\n"
    "    orrne r0, r0, #0x400\n"
    "    ldr   r12, =0xBEEF000B\n"
    "    cmp   r11, r12\n"
    "    orrne r0, r0, #0x800\n"
    "    pop   {r4-r11, pc}\n"
    "    .ltorg\n"
    "    .size kept_call, .-kept_call\n");
uint32_t kc_sp;

#define CH   288                                       /* wf_chunks chunk (words) */
#define OFFS 32                                        /* word offsets of a chunk in the area */
#define AREA (8 + OFFS + CH + 8)                       /* 8 guard words before offset 0, 8 after the longest chunk */
static uint32_t area_r[AREA] __attribute__((aligned(32))), area_c[AREA] __attribute__((aligned(32)));
static uint64_t nbad, nguard, nreg, nwords, ncalls, npath[4];
static int shown;
static uint32_t gx = 0x9E3779B9u;                      /* guard pattern generator */

static uint32_t xs(uint32_t *x) { *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5; return *x; }

/* one call of both kernels on in[0..n) placed at word offset off (n may be <= 0: nothing to copy, nothing written) */
static void run(const uint32_t *in, int n, int off, int mode)
{
    uint32_t g = xs(&gx) | 1;                          /* odd: never a pattern of equal words */
    for (int i = 0; i < AREA; i++) area_r[i] = area_c[i] = g + 0x01000193u * (uint32_t)i;
    uint32_t *pr = area_r + 8 + off, *pc = area_c + 8 + off;
    int m = n > 0 ? n : 0;
    memcpy(pr, in, (size_t)m * 4);
    memcpy(pc, in, (size_t)m * 4);
    ref_log_words(pr, n, mode);
    int r = kept_call(pc, n, mode);
    if (r) {
        nreg++;
        if (shown < 8) { shown++; printf("    callee-saved register(s) changed: mask %#x (n %d off %d)\n", r, n, off); }
    }
    for (int i = 0; i < m; i++)
        if (pr[i] != pc[i]) {
            nbad++;
            if (shown < 8) { shown++; printf("    mismatch: in %08x ref %08x asm %08x (n %d i %d off %d)\n", in[i], pr[i], pc[i], n, i, off); }
        }
    for (int i = 0; i < AREA; i++) {
        if (i >= 8 + off && i < 8 + off + m) continue;
        uint32_t want = g + 0x01000193u * (uint32_t)i;
        if (area_c[i] != want || area_r[i] != want) {
            nguard++;
            if (shown < 8) { shown++; printf("    write outside [p, p + n): word %d of the area (n %d off %d) asm %08x ref %08x\n",
                                             i - 8 - off, n, off, area_c[i], area_r[i]); }
        }
    }
    nwords += (uint64_t)m; ncalls++;
    if (n > 0) npath[n & 3]++;
}

static int report(int mode, const char *set)
{
    printf("mode %d %-6s calls %8llu words %9llu (n%%4 0/1/2/3: %llu/%llu/%llu/%llu) mismatches %llu guard %llu regs %llu\n", mode, set,
           (unsigned long long)ncalls, (unsigned long long)nwords, (unsigned long long)npath[0], (unsigned long long)npath[1],
           (unsigned long long)npath[2], (unsigned long long)npath[3], (unsigned long long)nbad, (unsigned long long)nguard,
           (unsigned long long)nreg);
    int fail = nbad || nguard || nreg || !ncalls;
    nbad = nguard = nreg = nwords = ncalls = 0; npath[0] = npath[1] = npath[2] = npath[3] = 0; shown = 0;
    fflush(stdout);
    return fail;
}

int main(int argc, char **argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    long nrand = argc > 2 ? atol(argv[2]) : 10000000L;
    if (mode < 1 || mode > 5 || nrand < 0) { fprintf(stderr, "usage: logchk MODE(1..5) [NRAND]\n"); return 2; }
    int fail = 0;
    uint32_t buf[CH];
    for (int set = 0; set < 2; set++) {
        uint32_t base = 0;
        for (int c = 0; base < (1u << 24); c++) {
            int n = CH - (c & 3);
            if ((1u << 24) - base < (uint32_t)n) n = (int)((1u << 24) - base);
            for (int i = 0; i < n; i++) {
                uint32_t idx = base + (uint32_t)i, u = idx & 255, v = (idx >> 8) & 255, y = idx >> 16;
                uint32_t y0 = set ? 255 - y : y;
                buf[i] = u | y0 << 8 | v << 16 | y << 24;
            }
            run(buf, n, c % OFFS, mode);
            base += (uint32_t)n;
        }
        fail |= report(mode, set ? "B" : "A");
    }
    uint32_t x = 0x12345678u + (uint32_t)mode;
    if (nrand > 0) {
        for (long done = 0; done < nrand; ) {
            int n = (int)(xs(&x) % CH) + 1, off = (int)(xs(&x) % OFFS);
            if (n > nrand - done) n = (int)(nrand - done);
            for (int i = 0; i < n; i++) buf[i] = xs(&x);
            run(buf, n, off, mode);
            done += n;
        }
        fail |= report(mode, "random");
    } else {
        printf("mode %d random skipped (NRAND 0)\n", mode);   /* WF_LOGCHK_NRAND=0: sets A, B, small, n<=0 only */
    }
    for (int n = 1; n <= 16; n++)
        for (int off = 0; off < OFFS; off++) {
            for (int i = 0; i < n; i++) buf[i] = xs(&x);
            run(buf, n, off, mode);
        }
    fail |= report(mode, "small");
    static const int nz[] = { 0, -1, -2, -3, -4, -CH, INT_MIN };
    for (unsigned k = 0; k < sizeof nz / sizeof nz[0]; k++)
        for (int off = 0; off < OFFS; off++)
            run(buf, nz[k], off, mode);
    fail |= report(mode, "n<=0");
    printf("%s\n", fail ? "FAIL" : "OK bit-exact");
    return fail;
}
