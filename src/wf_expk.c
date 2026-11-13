/* wf_expk.c - the display filter's exposure gain on n UYVY words {U s8, Y0, V s8, Y1} (wf_expsim.h): one table
 * lookup per luma, one per pair for the chroma scale. Compiled -O2 (per-pixel loop), merged with ld -r.
 * Chroma: u' = (u * c + 128) >> 8 on the biased byte ub = u + 128: ub * c - (128 c - 128), the same integer without
 * a sign extension; the clamp to -128..127 is a rare branch (taken only when the gain pushes a saturated colour out). */
#include <stdint.h>
#include "wf_expsim.h"

void wf_exp_words(uint32_t *p, int n, const struct wf_exp_tab *t)
{
    const uint8_t *gy = t->gy;
    const uint16_t *cs = t->cs;
    for (int k = 0; k < n; k++) {
        uint32_t wd = p[k];
        uint32_t y0 = (wd >> 8) & 255, y1 = wd >> 24;
        uint32_t c = cs[(y0 + y1 + 1) >> 1];
        uint32_t m = (wd & 0x00ff00ffu) ^ 0x00800080u;            /* U + 128, V + 128 */
        int32_t c7 = (int32_t)(c << 7) - 128;
        int32_t u = ((int32_t)((m & 255) * c) - c7) >> 8;
        int32_t v = ((int32_t)((m >> 16) * c) - c7) >> 8;
        if (((u + 128) | (v + 128)) >> 8) {                       /* out of -128..127 */
            if (u > 127) u = 127;
            if (u < -128) u = -128;
            if (v > 127) v = 127;
            if (v < -128) v = -128;
        }
        p[k] = ((uint32_t)u & 255) | (uint32_t)gy[y0] << 8 | ((uint32_t)v & 255) << 16 | (uint32_t)gy[y1] << 24;
    }
}
