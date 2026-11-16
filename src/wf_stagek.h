/* wf_stagek.h - WindyStrife: a film-stage film's 17^3 YUV table composed from two halves (branch film-stage, DESIGN 1a).
 *
 * The film-stage films (catalogue ids 85..88 and 89..133 x the 16 print stocks) ship one 17^3 table per pair, baked at
 * the chain's default settings. To move the chain's own knobs on the camera (Film exp, Push / pull, Exp comp, Printer
 * R / G / B, Interlayer) the generator also writes the chain in two halves, as sampled card data (ML/DATA/WFSTAGE.DAT,
 * tools/mk_wfstage.py):
 *   negative half (one per negative stock): X[] display code -> exposure axis x, A1[c][] x -> density per channel,
 *                 A2 33^3 density -> printing density at interlayer 0 (base) and its interlayer part (quad)
 *   print half    (one per print stock):    B1[c][] printing density / 4 -> print density per channel,
 *                 B2 33^3 print density -> display (the print's raw output, before the ends are pinned)
 *   pair          (one per film x print):   the fitted exposure and the two pinned ends of the shipped table
 * wf_sg_compose() walks the 4913 nodes (two passes, one half each): node Y'CbCr -> R'G'B' (clamped) -> X + exposure offset -> A1 -> A2
 * (tetrahedral) -> + interlayer x quad + timing - printer -> B1 -> B2 (tetrahedral) -> pinned ends -> Y'CbCr word, the
 * same words a .T17 holds. Generic 1D / 3D interpolation only: no formula of the chain is in here, the halves are data.
 * Integer arithmetic (armv5t, no float, no 64-bit division); runs in wf_task when a knob or the film changes, never per
 * pixel: the per-pixel kernel and its cost stay as they are.
 *
 * Accuracy (tools/mk_wfstage.c check, 7 pairs, against the chain run directly at the 4913 nodes): at default knobs
 * mean 0.2..0.3 codes, max 4..10 codes on saturated nodes near the gamut edge; the shipped 33^3-cube tables measure
 * mean 0.15, max 2..18 the same way. Default knobs use the shipped table, not this. */
#ifndef WF_STAGEK_H
#define WF_STAGEK_H
#include <stdint.h>

#define WF_SG_NA 33                        /* negative 3D grid */
#define WF_SG_NB 33                        /* print 3D grid */
#define WF_SG_N1 257                       /* 1D tables over 0..1 (index = x >> 8) */
#define WF_SG_NX 1021                      /* X: display code c = i / 4, i = 0..1020 */
#define WF_SG_A3 (WF_SG_NA * WF_SG_NA * WF_SG_NA)
#define WF_SG_B3 (WF_SG_NB * WF_SG_NB * WF_SG_NB)

/* the card blobs, read as they are (little-endian, 4-byte aligned) */
struct wf_sg_neg {
    int32_t lo[3], sc[3];                  /* A2 axis c: grid = ((A1 value - lo) * sc) >> 14, Q16 grid units */
    uint16_t X[WF_SG_NX + 1];              /* exposure axis x, Q16 (one pad entry) */
    uint16_t A1[3][WF_SG_N1];              /* density, Q14 */
    uint16_t pad;
    int16_t A2[WF_SG_A3][6];               /* base R G B, quad R G B: printing density, Q12 */
};
struct wf_sg_prn {
    int32_t lo[3], sc[3];                  /* B2 axis c: grid = ((B1 value - lo) * sc) >> 12 */
    uint16_t B1[3][WF_SG_N1];              /* print density, Q12 */
    uint16_t pad;
    uint16_t B2[WF_SG_B3][3];              /* raw display R G B, Q16 */
    uint16_t pad2;
};
struct wf_sg_pair {
    int32_t fit;                           /* fitted exposure, Q16 stops */
    int32_t bk;                            /* raw output pinned to 0, Q16 */
    uint32_t inv;                          /* 2^32 / (white - black) */
};

/* knob ranges (menu rows, WINDYFLM.CFG) */
#define WF_SG_FEXP_MAX 6                   /* Film exp: -6..+6 thirds of a stop */
#define WF_SG_PUSH_MAX 3                   /* Push / pull: -3..+3 thirds of a stop */
#define WF_SG_PRN_MAX 12                   /* Printer R / G / B: -12..+12 printer points (0.025 logE each) */
#define WF_SG_IIE_MAX 8                    /* Interlayer: 0..8 = 0..200 % in 25 % steps */
#define WF_SG_IIE_DEF 4                    /* 100 %, the chain's default: the shipped table */
#define WF_SG_IIE_START 2                  /* 50 %: where the Interlayer row starts, a fresh WINDYFLM.CFG and a bad
                                              stage= line (owner 11-16, to shoot it); a composed table, not the shipped
                                              one: a saved stage= line keeps its own value */
#define WF_SG_OFF_MAX (WF_SG_FEXP_MAX + WF_SG_PUSH_MAX)   /* Film exp + Push / pull, thirds of a stop */
#define WF_SG_AMT_MAX 4                    /* Bleach bypass, Print bleach, Neutral neg, Neutral print: 0..4 = 0..100 %
                                              in 25 % steps (the chain's bleach, print_bleach, neutral_tonescale,
                                              neutral_density; 0 is the chain's default) */
#define WF_SG_BL_SHIFT 6                   /* the two bleach rows: the chain's amount = step / 64, 100 % = 1/16. The
                                              chain takes 0..1, but its 0.25 already lifts mid grey from L* 55 to 85
                                              (Bleach bypass) or drops it to 21 (Print bleach); at 1/16: 68 / 43, the
                                              colour -18 / -22 % (C*), 2 chain films x 16 frames. The neutral rows
                                              blend 0..1 (step / 4) */
struct wf_sg_knobs { int fexp, push, comp, prn[3], iie, bleach, pbleach, nneg, nprn; };

/* The knob data of the last four rows (WFSTAGE.DAT version 2, tools/mk_wfstage.c kd): measured on the chain like the
 * halves, one set for every pair.
 *   Bleach bypass (negative): the chain adds amount (step / 64) x the sum of the three printing densities to each channel, after
 *     the interlayer and before the timing. A2's base was measured with the auto-balance inside, so the sum the bleach
 *     reads is the sum of the A2 values less bs (Q16 printing density, the three auto-balance offsets).
 *   Print bleach: the chain scales the print's linear light by 10^(-amount x the sum of the three print densities)
 *     before its gamut map (which keeps ratios) and its encode (a power): on the raw output that is one gain, g[] over
 *     s = amount x the sum of B1's three outputs, s = 0..16 on the 1D axis (Q12 s), Q16 (65535 = 1); amount = step / 64
 *     (at 1/16 the rows reach s < 1, the first 17 entries; g holds the chain's whole range).
 *   Neutral neg / Neutral print: R's and B's curves move toward green's at the same input (A1, B1), no data. */
struct wf_sg_kd {
    int32_t bs;
    uint16_t g[WF_SG_N1];
    uint16_t pad;
};

/* 1 = these knobs give the shipped table (no offset left after Exp comp, printer 0, interlayer 100 %, the last four
 * rows at 0) */
int wf_sg_default(const struct wf_sg_knobs *k);
/* a nonzero key for knob values that need a composed table (0 = default knobs), so two equal sets share one table;
 * the table depends on the key alone: wf_sg_unkey gives knobs that compose to the very same words. key = 1 + 2 x idx,
 * idx mixed radix over offset (19) x printer R G B (25 each) x interlayer (9) x the last four rows (5 each):
 * idx < 1 669 921 875, the key < 2^32 */
uint32_t wf_sg_key(const struct wf_sg_knobs *k);
void wf_sg_unkey(uint32_t key, struct wf_sg_knobs *k);
/* the composed table in two passes, so the camera holds one half at a time (wf_shot.c reads the negative half, runs
 * pass 1, frees it, then reads the print half): pd = WF_SG_NODES x 3 printing densities / 4 (B1's axis, Q16, 65535
 * standing for the top end), T = the 4913 words; bw: U and V bytes 0 (a B&W film). kd: the knob data, or 0 (a version
 * 1 pack): the last four rows are then read as 0 */
#define WF_SG_NODES 4913
void wf_sg_neg_pass(const struct wf_sg_neg *n, const struct wf_sg_pair *pr, const struct wf_sg_knobs *k,
                    const struct wf_sg_kd *kd, uint16_t *pd);
void wf_sg_prn_pass(const struct wf_sg_prn *p, const struct wf_sg_pair *pr, const struct wf_sg_knobs *k,
                    const struct wf_sg_kd *kd, const uint16_t *pd, int bw, uint32_t *T);
void wf_sg_compose(const struct wf_sg_neg *n, const struct wf_sg_prn *p, const struct wf_sg_pair *pr,
                   const struct wf_sg_knobs *k, const struct wf_sg_kd *kd, int bw, uint16_t *pd, uint32_t *T);

/* The RAW develop's log grid (chain films on RAW, option (c), wf_rawb.h): the same two passes over the 33^3 nodes of a
 * chain film's log table (ML/DATA/WFLOG.DAT, the .cube order, red fastest), so the knob rows reach B and the Darkroom.
 * The node's input is not a display code but a log code (the develop's curve, decoded and brought to linear Rec.709
 * as tools/mk_genesis_cube.c DOMAIN does): xn holds each node's exposure axis value per channel (Q16, the stage log
 * pack's section, measured like X; it does not depend on the negative), pr is the pair's log placement (its fit and
 * ends as the .L33 was placed), rgb = R' G' B' u16 value x 65535 per node, the samples a .L33 holds (bw: R' = G' = B'
 * = the BT.601 luma, as a B&W .L33). Both passes take any run of nodes (xn, pd, rgb point at its first), so the camera
 * can stream xn in and rgb out in slabs; pd is 3 x u16 per node as above. The x section and the placements ship in
 * ML/DATA/WFSTLOG.DAT (tools/mk_wfstlog.py). wf_rawb.c rb_sl_build composes twice (default and the knobs) and adds the
 * shipped table's residual (.L33 - default composition, quarter 8-bit codes) to the knobs' samples: default knobs never
 * get here (the shipped table itself), and the composition's own 0.15-code mean error against the chain stays out. */
#define WF_SG_LG 33
#define WF_SG_LNODES (WF_SG_LG * WF_SG_LG * WF_SG_LG)
void wf_sg_neg_pass_x(const struct wf_sg_neg *n, const struct wf_sg_pair *pr, const struct wf_sg_knobs *k,
                      const struct wf_sg_kd *kd, const uint16_t *xn, int nodes, uint16_t *pd);
void wf_sg_prn_pass_rgb(const struct wf_sg_prn *p, const struct wf_sg_pair *pr, const struct wf_sg_knobs *k,
                        const struct wf_sg_kd *kd, const uint16_t *pd, int nodes, int bw, uint16_t *rgb, uint16_t *gn);

/* The RAW knob table's residual (wf_rawb.c rb_sl_build; tools/mk_wfstage.c logcheck / logdat run the same code):
 * wf_sg_res_quant: r = the shipped log table's samples (le: u16 little-endian) less the default composition's (rgb),
 * n nodes x 3, in quarter 8-bit codes (64 Q16 units, half away from 0), clipped at +-127; returns the clipped count.
 * wf_sg_res_add: rgb (the knobs' samples of nodes node0 .. node0 + n - 1) += r x 64 read at the node the knobs' exposure
 * offset moves it to. pos: WFSTLOG.DAT's shift row for the offset (WF_SG_LG axis positions, node units Q8: where the
 * S-Log3 linear is 2^(offset / 3) times the node's), or 0 for none (r at the node itself, Interlayer / printer).
 * gn: the residual's weight at each node (Q16, wf_sg_prn_pass_rgb's gn), r x 64 x gn rounded >> 16; 0 when
 * wf_sg_res_w() is 0 (r as it is). The weight is the Print bleach gain (it scales the print's output, so it scales the
 * composition's error the residual stands for) x (4 - Bleach bypass step) / 4: Bleach bypass moves the knee (100 %:
 * 2.7 nodes down on G03P01) and the shipped table's residual sits at the old one, so it fades out; at 100 % the RAW
 * table is the composition alone, as the 8-bit table always is (tools/mk_wfstage.c logcheck: the knee then within
 * 0.05 node of the chain on 5 pairs, where the residual at the node put it up to 0.63 node late).
 * wf_sg_prn_pass_rgb writes gn[i] for each node when gn is not 0 (65535 when both rows are 0).
 * Per axis i0 = min(p >> 8, WF_SG_LG - 2), f = p - 256 i0; lerp(a, b) = a + (((b - a) f + 128) >> 8) on r x 64, over R,
 * then G, then B; the sum clamped to 0 .. 65535. r holds all WF_SG_LNODES nodes (the moved read reaches any of them). */
#define WF_SG_SHIFT_Q 8
int  wf_sg_res_quant(const uint8_t *le, const uint16_t *rgb, int n, int8_t *r);
int  wf_sg_res_w(const struct wf_sg_knobs *k, const struct wf_sg_kd *kd);   /* 1 = the residual takes gn */
void wf_sg_res_add(const int8_t *r, const uint16_t *pos, const uint16_t *gn, int node0, int n, uint16_t *rgb);
#endif
