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
        || a->seen_folder != b->seen_folder || a->seen_file != b->seen_file || a->dev != b->dev || a->own != b->own)
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
    {                                            /* Camera JPEG Over + print's second file (owner 11-17) */
        char d[64];
        CHECK(wf_roll_jpg2("B:/DCIM/100CANON/IMG_0206.JPG", 0, "JPG", d, sizeof d, p, sizeof p) == 29
              && !strcmp(p, "B:/DCIM/100CANON/WFP_0206.JPG") && !strcmp(d, "B:/DCIM/100CANON"), "jpg2 beside: %s in %s",
              p, d);
        CHECK(wf_roll_jpg2("B:/DCIM/100CANON/IMG_0206.JPG", 0, "WFT", d, sizeof d, p, sizeof p) > 0
              && !strcmp(p, "B:/DCIM/100CANON/WFP_0206.WFT"), "jpg2 temp: %s", p);
        CHECK(wf_roll_jpg2("B:/DCIM/101CANON/_MG_9999.JPG", 1, "JPG", d, sizeof d, p, sizeof p) > 0
              && !strcmp(p, "B:/DCIM/101WFILM/WFP_9999.JPG") && !strcmp(d, "B:/DCIM/101WFILM"), "jpg2 folder: %s in %s",
              p, d);
        CHECK(wf_roll_jpg2("A:/DCIM/100EOS5D/ABCD0012.JPG", 0, "JPG", d, sizeof d, p, sizeof p) > 0
              && !strcmp(p, "A:/DCIM/100EOS5D/WFP_0012.JPG"), "jpg2 Canon's own prefix: %s", p);
        CHECK(wf_roll_jpg2("B:/DCIM/100CANON/WFP_0206.JPG", 0, "JPG", d, sizeof d, p, sizeof p) == -1,
              "jpg2: Canon's letters ours");
        CHECK(wf_roll_jpg2("B:/DCIM/100CANON/IMG_02X6.JPG", 0, "JPG", d, sizeof d, p, sizeof p) == -1
              && wf_roll_jpg2("B:/DCIM/100CANON/IMG_206.JPG", 0, "JPG", d, sizeof d, p, sizeof p) == -1
              && wf_roll_jpg2("B:/DCIM/CANON100/IMG_0206.JPG", 1, "JPG", d, sizeof d, p, sizeof p) == -1
              && wf_roll_jpg2("B:/DCIM/100CAN/IMG_0206.JPG", 0, "JPG", d, sizeof d, p, sizeof p) == -1
              && wf_roll_jpg2("IMG_0206.JPG", 0, "JPG", d, sizeof d, p, sizeof p) == -1, "jpg2: not a DCF name");
        CHECK(wf_roll_jpg2("B:/DCIM/100CANON/IMG_0206.JPG", 0, "JPG", d, 16, p, sizeof p) == -1
              && wf_roll_jpg2("B:/DCIM/100CANON/IMG_0206.JPG", 1, "JPG", d, sizeof d, p, 29) == -1, "jpg2: no room");
    }
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

    /* the shelf (owner 11-17, DESIGN 3.15): a pass's 8.3 name, a roll made ready from the shelf (a CR2 gone: kept), a
       develop again (dev= / own= in its file, its own folder), the last pass */
    {
        char nm[12];
        int n = 0, d = 0;
        wf_roll_name(21, 0, nm);
        CHECK(!strcmp(nm, "ROLL021"), "name, pass 1: %s", nm);
        wf_roll_name(21, 1, nm);
        CHECK(!strcmp(nm, "ROLL021"), "name, dev 1 = pass 1: %s", nm);
        wf_roll_name(21, 2, nm);
        CHECK(!strcmp(nm, "ROLL021B"), "name, pass 2: %s", nm);
        wf_roll_name(999, WF_ROLL_MAXDEV, nm);
        CHECK(!strcmp(nm, "ROLL999Z"), "name, pass 26: %s", nm);
        /* the film's speed (owner 11-17): Canon's step nearest by ratio, the body's range, whole stops */
        int iv = 0;
        CHECK(wf_roll_iso(400, 72, 128, 1, &iv) == 88 && iv == 400, "ISO 400: code 88 (%d)", iv);
        CHECK(wf_roll_iso(250, 72, 128, 1, &iv) == 83 && iv == 250, "ISO 250: code 83 (%d)", iv);
        CHECK(wf_roll_iso(50, 72, 128, 1, &iv) == 72 && iv == 100, "ISO 50: 100, the lowest (%d)", iv);
        CHECK(wf_roll_iso(64, 80, 128, 1, &iv) == 80 && iv == 200, "ISO 64 with HTP: 200, its lowest (%d)", iv);
        CHECK(wf_roll_iso(250, 72, 128, 0, &iv) == 80 && iv == 200, "ISO 250, whole stops: 200 (%d)", iv);
        CHECK(wf_roll_iso(300, 72, 128, 0, &iv) == 88 && iv == 400, "ISO 300, whole stops: 400 (4/3 < 3/2) (%d)", iv);
        CHECK(wf_roll_iso(180, 72, 128, 1, &iv) == 80 && iv == 200, "ISO 180: 200 (200/180 < 180/160) (%d)", iv);
        CHECK(wf_roll_iso(3200, 72, 120, 1, &iv) == 112 && iv == 3200, "ISO 3200 on the 5D2 (%d)", iv);
        CHECK(wf_roll_iso(25600, 72, 120, 1, &iv) == 120 && iv == 6400, "ISO 25600 on the 5D2: 6400, its top (%d)", iv);
        CHECK(wf_roll_iso(99999, 72, 128, 1, &iv) == 128 && iv == 12800, "a huge speed: the EOS M's top (%d)", iv);
        CHECK(wf_roll_iso(0, 72, 128, 1, &iv) == 0 && iv == 0 && wf_roll_iso(-5, 72, 128, 1, 0) == 0, "no speed: 0");
        CHECK(wf_roll_iso(400, 130, 128, 1, &iv) == 0 && iv == 0, "no step in lo..hi: 0");
        CHECK(wf_roll_iso_of(83) == 250 && wf_roll_iso_of(0) == 0 && wf_roll_iso_of(84) == 0, "the ISO of a code");
        int ibad = 0;
        for (int c = 72; c <= 136; c++) {
            int s = wf_roll_iso_of(c);
            if (s && (wf_roll_iso(s, 72, 136, 1, &iv) != c || iv != s))
                ibad++;
        }
        CHECK(!ibad, "every 1/3 step to itself (%d off)", ibad);
        /* the quick keys (owner 11-17): one ISO / aperture step, the f-number */
        CHECK(wf_roll_iso_step(88, 1, 72, 128, 1) == 91 && wf_roll_iso_step(88, -1, 72, 128, 1) == 85,
              "ISO 400 a third up / down: 500 / 320");
        CHECK(wf_roll_iso_step(0, 1, 72, 128, 1) == 72 && wf_roll_iso_step(0, -1, 72, 128, 1) == 0
              && wf_roll_iso_step(0, 1, 80, 128, 1) == 80, "Auto: up = 100 (200 with HTP), down stays Auto");
        CHECK(wf_roll_iso_step(128, 1, 72, 128, 1) == 128 && wf_roll_iso_step(72, -1, 72, 128, 1) == 72
              && wf_roll_iso_step(80, -1, 80, 128, 1) == 80, "the ends stay: 12800 up, 100 down, 200 down with HTP");
        CHECK(wf_roll_iso_step(88, 1, 72, 128, 0) == 96 && wf_roll_iso_step(83, 1, 72, 128, 0) == 88
              && wf_roll_iso_step(83, -1, 72, 128, 0) == 80, "whole stops: 400 -> 800, 250 -> 400 / 200");
        CHECK(wf_roll_iso_step(136, -1, 72, 128, 1) == 128 && wf_roll_iso_step(88, 0, 72, 128, 1) == 88,
              "above the range: down to the top; dir 0: no step");
        CHECK(wf_roll_av_step(32, 1, 24, 80, 0) == 35 && wf_roll_av_step(35, 1, 24, 80, 0) == 37
              && wf_roll_av_step(37, 1, 24, 80, 0) == 40 && wf_roll_av_step(32, -1, 24, 80, 0) == 29,
              "aperture thirds: f/2.8 -> 3.2 -> 3.5 -> 4, f/2.8 -> 2.5");
        CHECK(wf_roll_av_step(32, 1, 24, 80, 1) == 36 && wf_roll_av_step(36, 1, 24, 80, 1) == 40
              && wf_roll_av_step(36, -1, 24, 80, 1) == 32, "aperture halves: f/2.8 -> 3.5 -> 4");
        CHECK(wf_roll_av_step(24, -1, 24, 80, 0) == 24 && wf_roll_av_step(80, 1, 24, 80, 0) == 80
              && wf_roll_av_step(27, -1, 26, 80, 0) == 26 && wf_roll_av_step(26, 1, 26, 80, 0) == 27
              && wf_roll_av_step(0, 1, 24, 80, 0) == 0, "the lens' ends stay, an end off the grid counts, no aperture: 0");
        CHECK(wf_roll_av_f10(32) == 28 && wf_roll_av_f10(35) == 32 && wf_roll_av_f10(37) == 35 && wf_roll_av_f10(36) == 35
              && wf_roll_av_f10(56) == 80 && wf_roll_av_f10(64) == 110 && wf_roll_av_f10(21) == 18 && wf_roll_av_f10(8) == 10
              && wf_roll_av_f10(0) == 0 && wf_roll_av_f10(31) == 28 && wf_roll_av_f10(26) == 22,
              "f-numbers: 2.8 3.2 3.5 3.5(half) 8 11 1.8 1.0, none, eighths to the nearest");
        CHECK(wf_roll_name_parse("ROLL021", &n, &d) == 0 && n == 21 && d == 1, "parse ROLL021: %d %d", n, d);
        CHECK(wf_roll_name_parse("roll021c", &n, &d) == 0 && n == 21 && d == 3, "parse roll021c (any case): %d %d", n, d);
        CHECK(wf_roll_name_parse("ROLL999Z", &n, &d) == 0 && n == 999 && d == 26, "parse ROLL999Z: %d %d", n, d);
        CHECK(wf_roll_name_parse("ROLL021A", &n, &d) == -1 && wf_roll_name_parse("ROLL000", &n, &d) == -1
              && wf_roll_name_parse("ROLL21", &n, &d) == -1 && wf_roll_name_parse("ROLL021BB", &n, &d) == -1
              && wf_roll_name_parse("ROLLS", &n, &d) == -1 && wf_roll_name_parse("ROLL0211", &n, &d) == -1
              && wf_roll_name_parse("FILM021", &n, &d) == -1, "parse: not a pass name");

        struct wf_roll a, b2;
        wf_roll_reset(&a);
        CHECK(wf_roll_load(&a, 57, 6, 100, 600) == 0, "a roll of 6");
        a.num = 21;
        wf_roll_seen(&a, 100, 606, has_cr2, 0);
        CHECK(a.state == WF_RS_FULL && a.nshot == 6, "full at 6 (%d, state %d)", a.nshot, a.state);
        no_cr2_file = 603;
        CHECK(wf_roll_shelf_prep(&a, has_cr2, 0) == 5 && a.state == WF_RS_REWOUND && a.fst[2] == WF_RF_KEPT
              && a.fst[3] == WF_RF_SHOT, "shelf prep: rewound, F03's CR2 gone: kept, 5 to develop (state %d)", a.state);
        no_cr2_file = -1;
        roundtrip(&a, "a shelf roll");
        CHECK(wf_roll_dir(&a, "B:/", 2, p, sizeof p) > 0 && !strcmp(p, "B:/ROLLS/ROLL021/WFROLL.TXT"), "shelf file %s", p);
        wf_roll_text(&a, buf, sizeof buf);
        CHECK(!strstr(buf, "dev=") && !strstr(buf, "own="), "pass 1: no dev= / own= (the file as before)");
        for (int i = 0; i < 6; i++)
            if (a.fst[i] == WF_RF_SHOT)
                wf_roll_mark(&a, i, WF_RF_DEV);
        CHECK(a.state == WF_RS_DONE && wf_roll_shelf_prep(&a, has_cr2, 0) == 0 && a.state == WF_RS_DONE,
              "developed: nothing left (state %d)", a.state);
        no_cr2_file = 605;
        CHECK(wf_roll_again(&a, 88, has_cr2, 0) == 5 && a.dev == 2 && a.own == 57 && a.film == 88
              && a.state == WF_RS_REWOUND && a.fst[2] == WF_RF_SHOT && a.fst[4] == WF_RF_KEPT && wf_roll_ndev(&a) == 0,
              "develop again: pass 2, film 88 (own 57), every frame with its CR2 again, F05 gone: kept (dev %d own %d)",
              a.dev, a.own);
        no_cr2_file = -1;
        CHECK(wf_roll_text(&a, buf, sizeof buf) == WF_ROLL_FILE && strstr(buf, "\ndev=2\nown=57\n")
              && wf_roll_parse(&b2, buf, WF_ROLL_FILE) == 0 && b2.dev == 2 && b2.own == 57 && b2.film == 88
              && same(&a, &b2), "pass 2 reads back (dev %d own %d)", b2.dev, b2.own);
        CHECK(wf_roll_dir(&a, "B:/", 1, p, sizeof p) > 0 && !strcmp(p, "B:/ROLLS/ROLL021B"), "pass 2 folder %s", p);
        CHECK(wf_roll_out(&a, 0, "B:/", p, sizeof p) > 0 && !strcmp(p, "B:/ROLLS/ROLL021B/F01_0601.JPG"), "pass 2 out %s", p);
        CHECK(wf_roll_again(&a, 57, has_cr2, 0) == 6 && a.dev == 3 && a.own == 57 && a.film == 57,
              "pass 3 from a stopped pass 2: all 6, the roll's own film kept as own (dev %d)", a.dev);
        a.dev = WF_ROLL_MAXDEV;
        CHECK(wf_roll_again(&a, 57, has_cr2, 0) == -1 && a.dev == WF_ROLL_MAXDEV, "no pass after Z");
        wf_roll_text(&a, buf, sizeof buf);
        char *dv = strstr(buf, "dev=26");
        CHECK(dv && wf_roll_parse(&b2, buf, WF_ROLL_FILE) == 0 && b2.dev == 26, "pass 26 reads back");
        if (dv)
            dv[5] = '7';
        CHECK(wf_roll_parse(&b2, buf, WF_ROLL_FILE) == -1, "dev=27: not ours");
        struct wf_roll e0;
        wf_roll_reset(&e0);
        CHECK(wf_roll_again(&e0, 57, has_cr2, 0) == -1, "no develop again of an empty roll");
    }

    printf("RESULT roll %d / %d\n", checks - fails, fails);
    return fails != 0;
}
