/* dev_a1ref.c - the JPEG path's reference for the RAW develop's real-shot test (WindyStrife). Host tool (ARM Linux,
 * qemu-arm-static), linked with the module's own Log + LUT objects; tools/dev_cr2.sh builds and runs it when the tree
 * has A1 (the Canon input curves: wf_ll_incurve, wf_ll_dec_c1 / wf_ll_dec_c2 in the generated wf_loglut_tab.h).
 *
 *   dev_a1ref DIR SLOT W H  -> DIR/ref_a1.uyvy: DIR/jpeg.uyvy (Canon's JPEG as UYVY, W x H) through Log + LUT with
 *                              the synthetic film cube (tests/host/wf_dev_scene.c), V-Log, input curve slot SLOT
 *                              (1 Canon Film 1, 2 Canon Neutral); and DIR/dec.bin: that slot's decode table (1022 x u32,
 *                              code i / 4 -> linear Q24), which the develop's WB fit reads the JPEG with (job.dec).
 * wf_dev_test cr2 compares the develop against ref_a1.uyvy when both files are there. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wf_loglut.h"
#include "wf_dev_scene.h"
#include "wf_loglut_tab.h"
extern int wf_ll_incurve, wf_ll_ei, wf_ll_auto;
void wf_ll_words(uint32_t *p, int n, int tabs);

int main(int argc, char **argv)
{
    if (argc < 5) {
        printf("usage: dev_a1ref DIR SLOT W H\n");
        return 2;
    }
    const char *dir = argv[1];
    int slot = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]);
    char p[512];
#if WF_LL_HAVE_C1 && WF_LL_HAVE_C2
    const uint32_t *d = slot == 1 ? wf_ll_dec_c1 : slot == 2 ? wf_ll_dec_c2 : 0;
#else
    const uint32_t *d = 0;
#endif
    if (!d) {
        printf("dev_a1ref: no curve table for slot %d in this build\n", slot);
        return 1;
    }
    sc_write_cube("A1.CUB", 33, 0);
    wf_ll_incurve = slot;
    wf_ll_ei = 0;
    wf_ll_auto = 0;
    static struct wf_lltabs T;
    struct wf_ll_info inf;
    int rc = wf_loglut_load("A1.CUB", WF_LL_VLOG, &T, &inf);
    remove("A1.CUB");
    if (rc) {
        printf("dev_a1ref: load %d\n", rc);
        return 1;
    }
    size_t n = (size_t)w * h * 2;
    uint32_t *b = malloc(n);
    snprintf(p, sizeof p, "%s/jpeg.uyvy", dir);
    FILE *f = fopen(p, "rb");
    if (!f || fread(b, 1, n, f) != n) {
        printf("dev_a1ref: %s\n", p);
        return 1;
    }
    fclose(f);
    wf_ll_words(b, w * h / 2, (int)&T);
    snprintf(p, sizeof p, "%s/ref_a1.uyvy", dir);
    f = fopen(p, "wb");
    fwrite(b, 1, n, f);
    fclose(f);
    snprintf(p, sizeof p, "%s/dec.bin", dir);
    f = fopen(p, "wb");
    fwrite(d, 4, 1022, f);
    fclose(f);
    printf("dev_a1ref: slot %d, %d x %d\n", slot, w, h);
    return 0;
}
