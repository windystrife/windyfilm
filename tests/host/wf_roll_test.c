/* wf_roll_test.c - host test of the roll state machine, the roll file text and the names (src/wf_roll.c).
 *   gcc -DWF_HOST_PURE -Isrc src/wf_roll.c tests/host/wf_roll_test.c && ./a.out     (tools/dr_check.sh runs it, also
 *   built for ARM with the module's flags and run under qemu) */
#include <stdio.h>
#include <string.h>
#include "wf_roll.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); \
                           printf("\n"); } } while (0)

static int no_cr2_file = -1;
static int has_cr2(void *ctx, int folder, int file) { (void)ctx; (void)folder; return file != no_cr2_file; }

static int same(const struct wf_roll *a, const struct wf_roll *b)
{
    if (a->num != b->num || a->film != b->film || a->exp != b->exp || a->state != b->state || a->nshot != b->nshot
        || a->seen_folder != b->seen_folder || a->seen_file != b->seen_file)
        return 0;
    for (int i = 0; i < a->nshot; i++)
        if (a->folder[i] != b->folder[i] || a->file[i] != b->file[i] || a->fst[i] != b->fst[i])
            return 0;
    return 1;
}

static void roundtrip(const struct wf_roll *r, const char *what)
{
    char buf[WF_ROLL_FILE + 8];
    struct wf_roll q;
    int n = wf_roll_text(r, buf, sizeof buf);
    CHECK(n == WF_ROLL_FILE, "%s: text size %d", what, n);
    CHECK(buf[WF_ROLL_FILE - 1] == '\n', "%s: ends with a newline", what);
    CHECK(wf_roll_parse(&q, buf, n) == 0, "%s: parse", what);
    CHECK(same(r, &q), "%s: round trip", what);
}

int main(void)
{
    struct wf_roll r, q;
    char p[96], buf[WF_ROLL_FILE + 8];

    wf_roll_reset(&r);
    CHECK(!wf_roll_open(&r) && !wf_roll_can_shoot(&r) && r.num == 0, "reset");
    roundtrip(&r, "empty");
    CHECK(wf_roll_load(&r, 57, 13, 100, 205) == -2, "exp 13 refused");
    CHECK(wf_roll_load(&r, 57, 12, 100, 205) == 0 && r.num == 1 && r.state == WF_RS_LOADED, "load 1");
    CHECK(wf_roll_seen(&r, 100, 205, has_cr2, 0) == 0 && r.nshot == 0, "no change, no frame");
    CHECK(wf_roll_seen(&r, 100, 207, has_cr2, 0) == 2 && r.nshot == 2 && r.file[0] == 206 && r.file[1] == 207,
          "two frames 206, 207");
    CHECK(wf_roll_load(&r, 60, 24, 100, 207) == -1 && r.film == 57, "no reload with frames on the roll");
    no_cr2_file = 208;
    CHECK(wf_roll_seen(&r, 100, 209, has_cr2, 0) == 1 && r.nshot == 3 && r.file[2] == 209, "JPEG-only 208 skipped");
    no_cr2_file = -1;
    CHECK(wf_roll_seen(&r, 100, 209, has_cr2, 0) == 0, "209 again: nothing");
    roundtrip(&r, "3 frames");
    CHECK(wf_roll_seen(&r, 100, 400, has_cr2, 0) == 0 && r.nshot == 3 && r.seen_file == 400, "a jump > 64: counted afresh");
    CHECK(wf_roll_seen(&r, 100, 409, has_cr2, 0) == 9 && r.nshot == 12 && r.state == WF_RS_FULL, "full at 12");
    CHECK(!wf_roll_can_shoot(&r) && wf_roll_open(&r), "full: no more shots, roll open");
    CHECK(wf_roll_seen(&r, 100, 412, has_cr2, 0) == 0 && r.nshot == 12, "beyond the roll: not added");
    CHECK(wf_roll_next_dev(&r) == -1, "nothing to develop before the rewind");
    CHECK(wf_roll_rewind(&r) == 0 && r.state == WF_RS_REWOUND, "rewind");
    CHECK(wf_roll_next_dev(&r) == 0 && wf_roll_left(&r) == 12, "develop from frame 1");
    wf_roll_mark(&r, 0, WF_RF_DEV);
    wf_roll_mark(&r, 1, WF_RF_ERR);
    CHECK(wf_roll_next_dev(&r) == 2 && wf_roll_left(&r) == 10 && wf_roll_ndev(&r) == 1, "resume at frame 3");
    roundtrip(&r, "rewound, 2 done");
    for (int i = 2; i < 12; i++)
        wf_roll_mark(&r, i, WF_RF_DEV);
    CHECK(r.state == WF_RS_DONE && wf_roll_ndev(&r) == 11 && wf_roll_next_dev(&r) == -1, "done");
    CHECK(wf_roll_load(&r, 61, 36, 100, 412) == 0 && r.num == 2 && r.exp == 36 && r.nshot == 0, "roll 2");
    CHECK(wf_roll_load(&r, 62, 24, 100, 412) == 0 && r.num == 2 && r.film == 62, "unused roll reloaded: same number");
    CHECK(wf_roll_rewind(&r) == -1, "no rewind of an empty roll");
    CHECK(wf_roll_seen(&r, 100, 9998, has_cr2, 0) == 0, "far jump");
    CHECK(wf_roll_seen(&r, 101, 2, has_cr2, 0) == 3 && r.folder[1] == 101 && r.file[1] == 1 && r.file[0] == 9999
          && r.file[2] == 2, "9999 -> 101/0001");
    CHECK(wf_roll_rewind(&r) == 0, "early rewind with 3 frames");
    CHECK(wf_roll_seen(&r, 101, 3, has_cr2, 0) == 0 && r.nshot == 3, "no frames after the rewind");
    roundtrip(&r, "early rewind");
    CHECK(wf_roll_close(&r) == 0 && r.state == WF_RS_DONE && r.fst[0] == WF_RF_KEPT, "close keeps the CR2s");
    CHECK(wf_roll_close(&r) == -1, "close twice");
    r.num = 999;
    CHECK(wf_roll_load(&r, 57, 12, 101, 3) == 0 && r.num == 1, "999 -> 1");

    /* names */
    wf_roll_reset(&r);
    wf_roll_load(&r, 57, 12, 100, 205);
    r.num = 21;
    wf_roll_seen(&r, 100, 206, has_cr2, 0);
    CHECK(wf_roll_cr2(&r, 0, "B:/DCIM/100CANON", "IMG_", "CR2", p, sizeof p) > 0
          && !strcmp(p, "B:/DCIM/100CANON/IMG_0206.CR2"), "cr2 path: %s", p);
    CHECK(wf_roll_cr2(&r, 0, "B:/DCIM/101CANON/", "IMG_", "JPG", p, sizeof p) > 0
          && !strcmp(p, "B:/DCIM/100CANON/IMG_0206.JPG"), "frame's own folder: %s", p);
    CHECK(wf_roll_cr2(&r, 1, "B:/DCIM/100CANON", "IMG_", "CR2", p, sizeof p) == -1, "no frame 2");
    CHECK(wf_roll_root("B:/DCIM/100CANON", p, sizeof p) == 3 && !strcmp(p, "B:/"), "root %s", p);
    CHECK(wf_roll_root("A:/DCIM/100EOS5D", p, sizeof p) == 3 && !strcmp(p, "A:/"), "root 5D2 %s", p);
    CHECK(wf_roll_root("B:/X/100CANON", p, sizeof p) == -1, "no DCIM");
    CHECK(wf_roll_dir(&r, "B:/", 0, p, sizeof p) > 0 && !strcmp(p, "B:/ROLLS"), "dir 0 %s", p);
    CHECK(wf_roll_dir(&r, "B:/", 1, p, sizeof p) > 0 && !strcmp(p, "B:/ROLLS/ROLL021"), "dir 1 %s", p);
    CHECK(wf_roll_out(&r, 0, "B:/", p, sizeof p) > 0 && !strcmp(p, "B:/ROLLS/ROLL021/F01_0206.JPG"), "out %s", p);
    CHECK(wf_roll_out(&r, 0, "B:/", p, 20) == -1, "short buffer");

    /* bad files */
    CHECK(wf_roll_parse(&q, "hello", 5) == -1, "not a roll file");
    wf_roll_text(&r, buf, sizeof buf);
    CHECK(wf_roll_parse(&q, buf, 40) == -1, "truncated");
    char *f = strstr(buf, "exp=12");
    f[4] = '3';
    CHECK(wf_roll_parse(&q, buf, WF_ROLL_FILE) == -1, "exp 32 refused");
    f[4] = '2';
    CHECK(wf_roll_ok_exp(3) && wf_roll_ok_exp(6) && !wf_roll_ok_exp(4) && !wf_roll_ok_exp(5) && !wf_roll_ok_exp(0)
          && !wf_roll_ok_exp(48), "3 and 6 exposures allowed (owner 11-15), 4 / 5 / 0 / 48 not");
    {                                            /* a roll of 3: full at the 3rd frame, its file reads back */
        struct wf_roll s3;
        wf_roll_reset(&s3);
        CHECK(wf_roll_load(&s3, 57, 3, 100, 300) == 0 && s3.exp == 3, "a roll of 3 loads");
        for (int i = 1; i <= 4; i++)
            wf_roll_seen(&s3, 100, 300 + i, has_cr2, 0);
        CHECK(s3.state == WF_RS_FULL && s3.nshot == 3, "a roll of 3: full at 3 (%d, state %d)", s3.nshot, s3.state);
        char b3[WF_ROLL_FILE];
        struct wf_roll q3;
        CHECK(wf_roll_text(&s3, b3, sizeof b3) == WF_ROLL_FILE && wf_roll_parse(&q3, b3, WF_ROLL_FILE) == 0
              && q3.exp == 3 && q3.nshot == 3 && q3.state == WF_RS_FULL, "a roll of 3 reads back");
        CHECK(wf_roll_load(&s3, 57, 6, 100, 310) == -1, "a full roll of 3 takes no new load");
    }
    f = strstr(buf, "f=100,206,S");
    f[10] = 'Z';
    CHECK(wf_roll_parse(&q, buf, WF_ROLL_FILE) == -1 && q.num == 0, "bad frame state -> reset");
    CHECK(wf_roll_text(&r, buf, 100) == -1, "text needs the full size");
    struct wf_roll big;
    wf_roll_reset(&big);
    wf_roll_load(&big, 127, 36, 999, 9000);
    big.num = 999;
    wf_roll_seen(&big, 999, 9036, has_cr2, 0);
    for (int i = 0; i < 36; i++) big.fst[i] = WF_RF_KEPT;
    big.state = WF_RS_DONE;
    roundtrip(&big, "36 frames, longest numbers");

    printf("RESULT roll %d / %d\n", checks - fails, fails);
    return fails != 0;
}
