@ wf_dev_arm.S - RAW develop "B" span kernel, ARMv5TE (ARM946E-S), bit-exact with wf_dv_mhc_c + wf_dv_lut_c
@ (src/wf_dev.c, the reference: read the struct wf_dv_span comment there). WindyStrife.
@
@   void wf_dv_span_s(const struct wf_dv_span *sp, int x, int n, uint32_t *out)
@       n pixel pairs from window column x (even) of the five ring rows -> n UYVY words. Per pair: the MHC demosaic of
@       both pixels into a stack slot ({row colour, G, column colour} each), then per pixel the matrix (SMULxy /
@       SMLAxy on int16 coefficients), the CLZ enc index, the tetrahedral on the packed nodes (one of six leaves,
@       every node load at an immediate offset), then the word.
@   void wf_dv_span_hs(const struct wf_dv_span *sp, int x, int n, uint32_t *out)
@       the same with the film shoulder I / D (wf_dev.h WF_DEV_HUE_*, wf_dev.c wf_dv_hue_idx): each pixel's enc
@       indices move by the shift table on its luma (PIXELH). Both come from one macro (KERNEL); wf_dv_span_s is the
@       same instructions as before the shoulder I existed.
@
@ Per pair: MHC 18 loads + 32 ALU + 6 stores; per pixel 15 matrix + 23 enc + 8 setup + 2..4 compares + 20 leaf;
@ 17 word, 10 switch / loop. The pair's values pass through a 24-byte stack slot that stays in the D-cache (the
@ store-no-allocate cache would otherwise refill a separate buffer on every chunk). v5TE: SMULxy / SMLAxy, SMULWy /
@ SMLAWy and CLZ (build.sh checks this object as v5TE, like wf_logout_arm.o). No LDRD / STRD. MUL / MLA with Rd != Rm.
@ Nothing is called.

        .syntax unified
        .arm
        .text

        .equ SP_NODE, 24                @ struct wf_dv_span
        .equ SP_ENCB, 28
        .equ SP_K,    32
        .equ SP_W,    36
        .equ SP_P,    64
        .equ SP_TB,   68                @ shoulder I / D: the shift table + off entries
        .equ SP_NOFF, 72                @ -off
        .equ SP_LC0,  76                @ luma weights c0 | c1 << 16
        .equ SP_LC2,  80                @ c2
        .equ SP_HK,   84                @ the pre-test's bound
        .equ SP_VMAX, 88                @ the CLZ index of enc index WF_DEV_NENC - 1
        .equ NENC4,   4352              @ WF_DEV_NENC: channel 1 at + 4352 entries, channel 2 at + 8704
        .equ BSL,     9216              @ blue slab: WF_DEV_BSTR * 2 words * 4 bytes
        .equ OG,      264               @ green step: 33 * 2 words * 4 bytes
        .equ ORG,     272               @ red + green

        @ stack frame
        .equ F_W,     0                 @ w[0..4]
        .equ F_END,   20                @ out + n
        .equ F_A0,    24                @ pixel A's lane sums
        .equ F_A1,    28
        .equ F_V,     32                @ the pair's values: A {nc, G, oc}, B {nc, G, oc}
        .equ F_ROW,   56                @ the five row pointers while the LUT part runs
        .equ F_NODE,  76                @ node, encb, k, out (one LDM into r1 .. r4)
        .equ F_OUT,   88
        .equ F_LOOP,  92                @ the MHC entry of the row's phase
        .equ F_SIZE,  96
        .equ F_TB,    96                @ wf_dv_span_hs: SP_TB .. SP_HK
        .equ F_NOFF,  100
        .equ F_LC0,   104
        .equ F_LC1,   108
        .equ F_HK,    112
        .equ F_VMAX,  116
        .equ F_SIZEH, 120

@ one leaf of the tetrahedral: fx >= fm >= fn the sorted fractions (registers, overwritten by the weights), vertex 1
@ at [b1, #o1], vertex 2 at [b2, #o2] (r5 = p, r6 = p + blue slab), vertex 3 at [r6, #ORG]. Out: r12 = lane sums
@ Y | Cb, lr = Cr.
        .macro  TET fx, fm, fn, b1, o1, b2, o2, join
        rsb     r7, \fx, #128           @ w0
        sub     \fx, \fx, \fm           @ w1
        sub     \fm, \fm, \fn           @ w2 (w3 = fn)
        ldr     r8, [r5]
        ldr     r12, [r5, #4]
        mul     lr, r7, r12
        mul     r12, r8, r7
        ldr     r8, [\b1, #\o1]
        ldr     r7, [\b1, #\o1 + 4]
        mla     r12, r8, \fx, r12
        mla     lr, r7, \fx, lr
        ldr     r8, [\b2, #\o2]
        ldr     r7, [\b2, #\o2 + 4]
        mla     r12, r8, \fm, r12
        mla     lr, r7, \fm, lr
        ldr     r8, [r6, #ORG]
        ldr     r7, [r6, #ORG + 4]
        mla     r12, r8, \fn, r12
        mla     lr, r7, \fn, lr
        b       \join
        .endm

@ one channel's enc entry: acc -> enc[c][idx(max(acc, 0) >> sh)] (the shift folded into the CLZ and into encb)
        .macro  ENC acc, add
        bic     \acc, \acc, \acc, asr #31
        orr     r5, \acc, r3
        clz     r5, r5
        mov     \acc, \acc, lsl r5
        mov     \acc, \acc, lsr #23
        sub     \acc, \acc, r5, lsl #8
        .if \add
        add     \acc, \acc, #\add
        .endif
        ldr     \acc, [r2, \acc, lsl #2]
        .endm

@ one pixel (values at r0, advanced): matrix, enc, tetrahedral; ends at join with r12 / lr = the lane sums
        .macro  PIXEL s, join
        ldmia   r0!, {r5, r6, r7}       @ row colour, G, column colour
        ldr     r8, [sp, #F_W]
        smulbb  r9, r5, r8
        smlabt  r9, r6, r8, r9
        ldr     r8, [sp, #F_W + 4]
        smlabb  r9, r7, r8, r9          @ acc0
        smulbt  r10, r5, r8
        ldr     r8, [sp, #F_W + 8]
        smlabb  r10, r6, r8, r10
        smlabt  r10, r7, r8, r10        @ acc1
        ldr     r8, [sp, #F_W + 12]
        smulbb  r11, r5, r8
        smlabt  r11, r6, r8, r11
        ldr     r8, [sp, #F_W + 16]
        smlabb  r11, r7, r8, r11        @ acc2
        ENC     r9, 0
        ENC     r10, NENC4
        ENC     r11, 2 * NENC4
        add     r5, r9, r10
        add     r5, r5, r11
        add     r5, r1, r5, lsr #7
        bic     r5, r5, #3              @ p = node + (sum of the cell offsets) words
        and     r9, r9, #127            @ fr
        and     r10, r10, #127          @ fg
        and     r11, r11, #127          @ fb
        add     r6, r5, #BSL            @ p + blue slab
        cmp     r9, r10
        ble     .Lg\s
        cmp     r10, r11
        bgt     .Lrgb\s
        cmp     r9, r11
        bgt     .Lrbg\s
        TET     r11, r9, r10, r6, 0, r6, 8, \join           @ fb >= fr > fg
.Lrgb\s:
        TET     r9, r10, r11, r5, 8, r5, ORG, \join         @ fr > fg > fb
.Lrbg\s:
        TET     r9, r11, r10, r5, 8, r6, 8, \join           @ fr > fb >= fg
.Lg\s:
        cmp     r11, r10
        bgt     .Lbgr\s
        cmp     r11, r9
        bgt     .Lgbr\s
        TET     r10, r9, r11, r5, OG, r5, ORG, \join        @ fg >= fr >= fb
.Lbgr\s:
        TET     r11, r10, r9, r6, 0, r6, OG, \join          @ fb > fg >= fr
.Lgbr\s:
        TET     r10, r11, r9, r5, OG, r6, OG, \join         @ fg >= fb > fr
        .endm

@ ---- the shoulder I / D (PIXELH). In its full path: r6 = dq (d (16384 - w), or d << 14 when w = 0), r7 = T[im]
@ (d low, w high), r12 = -off and r3 = the top (CLZ indices of enc index 0 and WF_DEV_NENC - 1; k is reloaded after),
@ lr = T + off entries; r5, r8 temporaries.
@ ENC of an accumulator already clamped at 0
        .macro  ENC0 acc, add
        orr     r5, \acc, r3
        clz     r5, r5
        mov     \acc, \acc, lsl r5
        mov     \acc, \acc, lsr #23
        sub     \acc, \acc, r5, lsl #8
        .if \add
        add     \acc, \acc, #\add
        .endif
        ldr     \acc, [r2, \acc, lsl #2]
        .endm

@ an accumulator (clamped at 0) -> its CLZ index (enc index - off)
        .macro  IDX acc
        orr     r5, \acc, r3
        clz     r5, r5
        mov     \acc, \acc, lsl r5
        mov     \acc, \acc, lsr #23
        sub     \acc, \acc, r5, lsl #8
        .endm

@ w > 0: the CLZ index += (dq + T[i] w) >> 16, kept within enc index 0 (r12) .. WF_DEV_NENC - 1 (r3), the enc entry
        .macro  HCH acc, add
        add     r8, lr, \acc, lsl #2
        ldrsh   r8, [r8]                @ h = the channel's own shift, quarter steps (+ 2)
        smlabt  r5, r8, r7, r6          @ dq + h w
        add     \acc, \acc, r5, asr #16
        cmp     \acc, r12
        movlt   \acc, r12
        cmp     \acc, r3
        movgt   \acc, r3
        .if \add
        add     \acc, \acc, #\add
        .endif
        ldr     \acc, [r2, \acc, lsl #2]
        .endm

@ w = 0: the CLZ index += dq >> 16 (= d >> 2), kept within the table as HCH
        .macro  DCH acc, add
        add     \acc, \acc, r6, asr #16
        cmp     \acc, r12
        movlt   \acc, r12
        cmp     \acc, r3
        movgt   \acc, r3
        .if \add
        add     \acc, \acc, #\add
        .endif
        ldr     \acc, [r2, \acc, lsl #2]
        .endm

@ one pixel with the shoulder I / D: the matrix as PIXEL and the accumulators clamped at 0 once. R and G both below hk:
@ the plain lookups (luma below the knee: shift 0). Else (out of line, after the leaves) the luma (SMULW, half scale)
@ and its enc index + 256 -> T[im]; w = 0: every channel moves by d / 4 (DCH), else by (d (1 - w) + h w) / 4 (HCH);
@ back to the leaves at .Lj.
        .macro  PIXELH s, join
        ldmia   r0!, {r5, r6, r7}       @ row colour, G, column colour
        ldr     r8, [sp, #F_W]
        smulbb  r9, r5, r8
        smlabt  r9, r6, r8, r9
        ldr     r8, [sp, #F_W + 4]
        smlabb  r9, r7, r8, r9          @ acc0
        smulbt  r10, r5, r8
        ldr     r8, [sp, #F_W + 8]
        smlabb  r10, r6, r8, r10
        smlabt  r10, r7, r8, r10        @ acc1
        ldr     r8, [sp, #F_W + 12]
        smulbb  r11, r5, r8
        smlabt  r11, r6, r8, r11
        ldr     r8, [sp, #F_W + 16]
        smlabb  r11, r7, r8, r11        @ acc2
        bic     r9, r9, r9, asr #31
        bic     r10, r10, r10, asr #31
        bic     r11, r11, r11, asr #31
        ldr     r8, [sp, #F_HK]
        cmp     r9, r8
        cmplo   r10, r8
        bhs     .Lh\s
        ENC0    r9, 0
        ENC0    r10, NENC4
        ENC0    r11, 2 * NENC4
.Lj\s:
        add     r5, r9, r10
        add     r5, r5, r11
        add     r5, r1, r5, lsr #7
        bic     r5, r5, #3              @ p = node + (sum of the cell offsets) words
        and     r9, r9, #127            @ fr
        and     r10, r10, #127          @ fg
        and     r11, r11, #127          @ fb
        add     r6, r5, #BSL            @ p + blue slab
        cmp     r9, r10
        ble     .Lg\s
        cmp     r10, r11
        bgt     .Lrgb\s
        cmp     r9, r11
        bgt     .Lrbg\s
        TET     r11, r9, r10, r6, 0, r6, 8, \join           @ fb >= fr > fg
.Lrgb\s:
        TET     r9, r10, r11, r5, 8, r5, ORG, \join         @ fr > fg > fb
.Lrbg\s:
        TET     r9, r11, r10, r5, 8, r6, 8, \join           @ fr > fb >= fg
.Lg\s:
        cmp     r11, r10
        bgt     .Lbgr\s
        cmp     r11, r9
        bgt     .Lgbr\s
        TET     r10, r9, r11, r5, OG, r5, ORG, \join        @ fg >= fr >= fb
.Lbgr\s:
        TET     r11, r10, r9, r6, 0, r6, OG, \join          @ fb > fg >= fr
.Lgbr\s:
        TET     r10, r11, r9, r5, OG, r6, OG, \join         @ fg >= fb > fr
.Lh\s:
        ldr     r8, [sp, #F_LC0]
        smulwb  r12, r9, r8
        smlawt  r12, r10, r8, r12
        ldr     r8, [sp, #F_LC1]
        smlawb  r12, r11, r8, r12       @ luma, half scale
        bic     r12, r12, r12, asr #31
        orr     r5, r12, r3
        clz     r5, r5
        mov     r12, r12, lsl r5
        mov     r12, r12, lsr #23
        sub     r12, r12, r5, lsl #8    @ its enc index - off
        ldr     lr, [sp, #F_TB]
        add     r12, lr, r12, lsl #2
        ldr     r7, [r12, #1024]        @ T[im], im = enc index + 256
        IDX     r9
        IDX     r10
        IDX     r11
        ldr     r3, [sp, #F_VMAX]       @ r3 (k) free until the next pixel: the table's top
        ldr     r12, [sp, #F_NOFF]
        movs    r6, r7, lsr #16         @ w
        beq     .Ld\s
        rsb     r6, r6, #16384
        smulbb  r6, r7, r6              @ dq = d (16384 - w)
        HCH     r9, 0
        HCH     r10, NENC4
        HCH     r11, 2 * NENC4
        ldr     r3, [sp, #F_NODE + 8]   @ k
        b       .Lj\s
.Ld\s:
        mov     r6, r7, lsl #16
        mov     r6, r6, asr #2          @ dq = d << 14
        DCH     r9, 0
        DCH     r10, NENC4
        DCH     r11, 2 * NENC4
        ldr     r3, [sp, #F_NODE + 8]   @ k
        b       .Lj\s
        .endm

@ the span function: hue 0 = wf_dv_span_s, 1 = wf_dv_span_hs (PIXELH, the shoulder's five words in its frame)
@ MHC part: r2 nn, r3 n, r4 o, r5 s, r6 ss (pointers at column x + p, advanced 4 bytes per pair by the first load of
@ each row), r7 .. r12, lr temporaries; values to [sp, #F_V ..]. LUT part: r0 values, r1 node, r2 encb, r3 k, r4 out,
@ r5 .. r12, lr temporaries.
        .macro  KERNEL name, hue
        .global \name
        .type   \name, %function
\name:
        push    {r4-r11, lr}
        .if \hue
        sub     sp, sp, #F_SIZEH
        .else
        sub     sp, sp, #F_SIZE
        .endif
        cmp     r2, #0
        ble     .L\name\()_done
        add     r12, r3, r2, lsl #2
        str     r12, [sp, #F_END]
        str     r3, [sp, #F_OUT]
        ldr     r12, [r0, #SP_NODE]
        str     r12, [sp, #F_NODE]
        ldr     r12, [r0, #SP_ENCB]
        str     r12, [sp, #F_NODE + 4]
        ldr     r12, [r0, #SP_K]
        str     r12, [sp, #F_NODE + 8]
        .if \hue
        ldr     r12, [r0, #SP_TB]
        str     r12, [sp, #F_TB]
        ldr     r12, [r0, #SP_NOFF]
        str     r12, [sp, #F_NOFF]
        ldr     r12, [r0, #SP_LC0]
        str     r12, [sp, #F_LC0]
        ldr     r12, [r0, #SP_LC2]
        str     r12, [sp, #F_LC1]
        ldr     r12, [r0, #SP_HK]
        str     r12, [sp, #F_HK]
        ldr     r12, [r0, #SP_VMAX]
        str     r12, [sp, #F_VMAX]
        .endif
        add     r12, r0, #SP_W
        ldmia   r12, {r4-r8}
        stmia   sp, {r4-r8}             @ w[0..4]
        ldr     r11, [r0, #SP_P]
        add     r12, r1, r11            @ x + p
        ldmia   r0, {r2-r6}             @ nn, n, o, s, ss at column 0
        add     r2, r2, r12, lsl #1
        add     r3, r3, r12, lsl #1
        add     r4, r4, r12, lsl #1
        add     r5, r5, r12, lsl #1
        add     r6, r6, r12, lsl #1
        adr     r12, .L\name\()_p0
        cmp     r11, #0
        adrne   r12, .L\name\()_p1
        str     r12, [sp, #F_LOOP]
        bx      r12

@ p = 0: non-green site x (pixel A), green site x + 1 (pixel B). Pointers at column x; after the first load of a row
@ at x + 2: o[x + k] = [r4, #2 (k - 2)], n / s[x + k] = [r3 / r5, #2 (k - 2)], nn / ss[x + 1] = [r2 / r6, #-2].
.L\name\()_p0:
        ldrsh   r7, [r4], #4            @ C = o0
        ldrsh   r8, [r2], #4            @ nn0
        ldrsh   r9, [r6], #4            @ ss0
        add     r8, r8, r9
        ldrsh   r9, [r4, #-8]           @ o-2
        add     r8, r8, r9
        ldrsh   r9, [r4, #0]            @ o2
        add     r8, r8, r9              @ ax2 = nn0 + ss0 + o-2 + o2
        ldrsh   r10, [r3], #4           @ n0
        ldrsh   r11, [r5], #4           @ s0
        add     r10, r10, r11           @ cs0
        ldrsh   r11, [r4, #-6]          @ o-1
        ldrsh   r12, [r4, #-2]          @ o1 = G of pixel B
        add     lr, r10, r11
        add     lr, lr, r12             @ cr = cs0 + o-1 + o1
        add     lr, lr, r7, lsl #1
        rsb     lr, r8, lr, lsl #1      @ 4 C + 2 cr - ax2
        mov     lr, lr, asr #3          @ vg
        str     lr, [sp, #F_V + 4]
        rsb     r8, r8, r7, lsl #2      @ 4 C - ax2
        add     r8, r8, r8, lsl #1      @ 12 C - 3 ax2
        add     r9, r9, r7              @ o0 + o2
        str     r7, [sp, #F_V + 0]      @ A: C
        ldrsh   r7, [r3, #-2]           @ n1
        ldrsh   lr, [r5, #-2]           @ s1
        add     r7, r7, lr              @ cs1
        add     r8, r8, r7, lsl #2
        ldrsh   lr, [r3, #-6]           @ n-1
        add     r8, r8, lr, lsl #2
        ldrsh   lr, [r5, #-6]           @ s-1
        add     r8, r8, lr, lsl #2      @ 12 C + 4 dg - 3 ax2
        mov     r8, r8, asr #4          @ vo
        str     r8, [sp, #F_V + 8]
        ldrsh   r8, [r4, #2]            @ o3
        add     r11, r11, r8            @ hv = o-1 + o3
        ldrsh   r8, [r3, #0]            @ n2
        ldrsh   lr, [r5, #0]            @ s2
        add     r8, r8, lr
        add     r10, r10, r8            @ dgg = cs0 + cs2
        ldrsh   r8, [r2, #-2]           @ nn1
        ldrsh   lr, [r6, #-2]           @ ss1
        add     r8, r8, lr              @ vv
        add     lr, r12, r12, lsl #2    @ 5 G
        add     r9, lr, r9, lsl #2
        sub     r9, r9, r10
        sub     r9, r9, r11
        add     r9, r8, r9, lsl #1      @ 10 G + 8 (o0 + o2) - 2 dgg - 2 hv + vv
        mov     r9, r9, asr #4          @ vr
        str     r9, [sp, #F_V + 12]
        add     r7, lr, r7, lsl #2
        sub     r7, r7, r10
        sub     r7, r7, r8
        add     r7, r11, r7, lsl #1     @ 10 G + 8 cs1 - 2 dgg - 2 vv + hv
        mov     r7, r7, asr #4          @ vb
        str     r12, [sp, #F_V + 16]
        str     r7, [sp, #F_V + 20]
        b       .L\name\()_lut

@ p = 1: green site x (pixel A), non-green site x + 1 (pixel B). Pointers at column x + 1; after the first load of a
@ row at x + 3: o[x + k] = [r4, #2 (k - 3)], n / s likewise, nn / ss[x] = [r2 / r6, #-6].
.L\name\()_p1:
        ldrsh   r7, [r4], #4            @ C = o1
        ldrsh   r8, [r2], #4            @ nn1
        ldrsh   r9, [r6], #4            @ ss1
        add     r8, r8, r9
        ldrsh   r9, [r4, #0]            @ o3
        add     r8, r8, r9
        ldrsh   r9, [r4, #-8]           @ o-1
        add     r8, r8, r9              @ ax2 = nn1 + ss1 + o-1 + o3
        ldrsh   r10, [r3], #4           @ n1
        ldrsh   r11, [r5], #4           @ s1
        add     r10, r10, r11           @ cs1
        ldrsh   r11, [r4, #-2]          @ o2
        ldrsh   r12, [r4, #-6]          @ o0 = G of pixel A
        add     lr, r10, r11
        add     lr, lr, r12             @ cr = cs1 + o0 + o2
        add     lr, lr, r7, lsl #1
        rsb     lr, r8, lr, lsl #1
        mov     lr, lr, asr #3          @ vg
        str     lr, [sp, #F_V + 16]
        rsb     r8, r8, r7, lsl #2
        add     r8, r8, r8, lsl #1      @ 12 C - 3 ax2
        add     r9, r9, r7              @ o-1 + o1
        str     r7, [sp, #F_V + 12]     @ B: C
        ldrsh   r7, [r3, #-6]           @ n0
        ldrsh   lr, [r5, #-6]           @ s0
        add     r7, r7, lr              @ cs0
        add     r8, r8, r7, lsl #2
        ldrsh   lr, [r3, #-2]           @ n2
        add     r8, r8, lr, lsl #2
        ldrsh   lr, [r5, #-2]           @ s2
        add     r8, r8, lr, lsl #2      @ 12 C + 4 dg - 3 ax2
        mov     r8, r8, asr #4          @ vo
        str     r8, [sp, #F_V + 20]
        ldrsh   r8, [r4, #-10]          @ o-2
        add     r11, r11, r8            @ hv = o-2 + o2
        ldrsh   r8, [r3, #-8]           @ n-1
        ldrsh   lr, [r5, #-8]           @ s-1
        add     r8, r8, lr
        add     r10, r10, r8            @ dgg = cs-1 + cs1
        ldrsh   r8, [r2, #-6]           @ nn0
        ldrsh   lr, [r6, #-6]           @ ss0
        add     r8, r8, lr              @ vv
        add     lr, r12, r12, lsl #2    @ 5 G
        add     r9, lr, r9, lsl #2
        sub     r9, r9, r10
        sub     r9, r9, r11
        add     r9, r8, r9, lsl #1
        mov     r9, r9, asr #4          @ vr
        str     r9, [sp, #F_V + 0]
        add     r7, lr, r7, lsl #2
        sub     r7, r7, r10
        sub     r7, r7, r8
        add     r7, r11, r7, lsl #1
        mov     r7, r7, asr #4          @ vb
        str     r12, [sp, #F_V + 4]
        str     r7, [sp, #F_V + 8]

.L\name\()_lut:
        add     r0, sp, #F_ROW
        stmia   r0!, {r2-r6}            @ row pointers; r0 = sp + F_NODE
        ldmia   r0, {r1-r4}             @ node, encb, k, out
        add     r0, sp, #F_V
        .if \hue
        PIXELH  \name\()a, .L\name\()_a
        .else
        PIXEL   \name\()a, .L\name\()_a
        .endif
.L\name\()_a:
        str     r12, [sp, #F_A0]
        str     lr, [sp, #F_A1]
        .if \hue
        PIXELH  \name\()b, .L\name\()_b
        .else
        PIXEL   \name\()b, .L\name\()_b
        .endif
.L\name\()_b:
        ldr     r5, [sp, #F_A0]         @ pixel A: Y | Cb lane sums
        ldr     r6, [sp, #F_A1]         @ pixel A: Cr
        and     r7, r5, #0xff00         @ y0 << 8 (the nodes hold the rounding)
        mov     r8, r12, lsr #8         @ y1 in bits 0..7
        mov     r9, r5, lsr #16
        add     r9, r9, r12, lsr #16
        mov     r9, r9, lsr #9          @ cb
        add     r10, r6, lr
        mov     r10, r10, lsr #9        @ cr
        orr     r9, r9, r7
        orr     r9, r9, r10, lsl #16
        orr     r9, r9, r8, lsl #24
        eor     r9, r9, #0x80
        eor     r9, r9, #0x800000
        str     r9, [r4], #4
        ldr     r5, [sp, #F_END]
        cmp     r4, r5
        bhs     .L\name\()_done
        str     r4, [sp, #F_OUT]
        add     r0, sp, #F_ROW
        ldmia   r0, {r2-r6}
        ldr     pc, [sp, #F_LOOP]
.L\name\()_done:
        .if \hue
        add     sp, sp, #F_SIZEH
        .else
        add     sp, sp, #F_SIZE
        .endif
        pop     {r4-r11, pc}
        .size   \name, . - \name
        .endm

        KERNEL  wf_dv_span_s, 0
        KERNEL  wf_dv_span_hs, 1
