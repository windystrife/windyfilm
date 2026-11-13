@ wf_logout_arm.S - plain log output modes (S-Log3 / V-Log / LogC3 / Canon Log 2 / 3) for WindyFilm,
@ hand-scheduled ARMv5TE (ARM946E-S: SMULxy / SMLAxy / SMLAWy), bit-exact with wf_logout_ref.c
@ (the C reference: read its comment for the maths). WindyStrife.
@
@   void wf_log_words(uint32_t *p, int n, int mode)     wf_chunks callback, n UYVY words in place, mode 1..5
@
@ Per word {U s8, Y0, V s8, Y1}, both pixels interleaved, no interlocks in the ARM946E-S model:
@  - ir = ((y << 16) + 8192 + tr) >> 14 = 4 y + dr, dr = (8192 + tr) >> 14 depends on V only (65536 y is a
@    multiple of 16384); same for dg (U, V) and db (U). wf_lasm (mk_logasm_tab.py) holds the U / V tables of
@    the per-word LDRH offsets and wf_dec padded on both sides, so CLAMP(i, 0, 1020) costs nothing:
@    l = LDRH [B + 8 y, off], off = 2 d + D from the tables (offG = 2 ((GA4 + GB4) >> 16) via SMULTT).
@  - lr|lg|lb of both pixels packed in halves (LR = lr0 | lr1 << 16 ...), the 3x3 Q14 matrix as SMLAxy with
@    packed coefficient halves (wf_lasm_k: KA..KE, KE top half = 2), each row chain started from CT.
@  - wf_lenc index: the rows of wf_lmat are >= 0 and sum to 16384, wf_dec <= 16384, so S = m.l is in
@    [0, 2^28], xr = (S + 8192) >> 14 is in [0, 16384] (both CLAMPs are dead) and (xr + 2) >> 2 =
@    (S + 40960) >> 16. CT = 40960 + (t << 16), t = (wf_lenc[mode - 1] - B) / 2 (build.sh lenc_layout
@    guard on the linked module: wf_lenc and wf_lasm in one section, wf_lenc - B even and in [-65536, 24566],
@    so every 2 (idx + t), idx 0..4096, is an even LDRH offset in [-65536, 65534]): acc = S + CT has idx + t in
@    its top half, SMULTT(acc, KE) = 2 idx + 2 t is the LDRH offset from B, so one base register serves all tables.
@  - Y' per pixel: MLA chain 19595 r + 38470 g + 7471 b + 2^23 (mod 2^32 exact, top byte = ys).
@  - Cb/Cr from d1 = er - eg, d2 = eb - eg: -2765 er - 5427 eg + 8192 eb = -2765 d1 + 8192 d2 and
@    8192 er - 6860 eg - 1332 eb = 8192 d1 - 1332 d2. With ds = d << 14 (|d| <= 2 max(wf_lenc) < 2^17),
@    SMLAWB(d1s, -22120, 2^23 + d2s) = 2^23 + 2 Xu and SMLAWT(d2s, -10656, 2^23 + d1s) = 2^23 + 2 Xv
@    exactly (22120 = 8 * 2765, 10656 = 8 * 1332), and the top byte of 2^23 + 2 X (mod 2^32) is
@    (uint8_t)((X + 2^22) >> 23). The CLAMP(.., -128, 127) is dead: |X| <= 16384 max(wf_lenc) and
@    mk_logasm_tab.py asserts max(wf_lenc) <= 65279 (today 39110: -77 <= u, v <= 77).
@ Unrolled x4; n % 4 = 1, 2, 3 enters at the fourth, third, second copy (computed branch). No LDRD/STRD,
@ no Thumb, no ARMv6, MUL/MLA with Rd != Rm. Relocations: R_ARM_ABS32 literals only. Stack: 9 saved
@ registers + a 40-byte constant frame (sp-relative loads, no call). Tables: wf_lasm + wf_lasm_k
@ (wf_logout_tab.c), wf_lenc (wf_logtab.h).

        .syntax unified
        .arm
        .cpu    arm946e-s
        .text
        .align  2

@ one word: r0 p (post-incremented), r1 B, r2 CT, r12 KE; r3-r11, lr temporaries; leaves the word stored
@ (last = 1: also loads the end pointer into r6 for the loop test)
.macro  WF_LOGWORD last=0
        ldr     r3, [r0], #4                @ wd
        ldr     lr, [sp]                    @ KA = m0 | m1 << 16
        mov     r6, r3, lsl #24             @ U << 24
        and     r7, r3, #0xFF0000           @ V << 16
        add     r6, r1, r6, asr #21         @ B + 8 cb  (U table centred on B)
        add     r7, r1, r7, lsr #13         @ B + 8 V   (V table at +1024)
        and     r8, r3, #0xFF00             @ y0 << 8
        ldr     r9, [r6]                    @ offB
        ldr     r10, [r7, #1024]            @ offR
        ldr     r11, [r6, #4]               @ GA4
        ldr     r4, [r7, #1028]             @ GB4
        add     r6, r1, r8, lsr #5          @ pp0 = B + 8 y0
        mov     r8, r3, lsr #24             @ y1
        add     r7, r1, r8, lsl #3          @ pp1 = B + 8 y1
        ldrh    r3, [r6, r10]               @ lr0
        add     r11, r11, r4                @ GA4 + GB4
        ldrh    r4, [r7, r10]               @ lr1
        smultt  r11, r11, r12               @ offG = 2 ((GA4 + GB4) >> 16)
        ldrh    r5, [r6, r9]                @ lb0
        ldrh    r8, [r7, r9]                @ lb1
        ldrh    r10, [r6, r11]              @ lg0
        ldrh    r9, [r7, r11]               @ lg1
        orr     r3, r3, r4, lsl #16         @ LR
        orr     r5, r5, r8, lsl #16         @ LB
        ldr     r6, [sp, #4]                @ KB = m2 | m3 << 16
        orr     r10, r10, r9, lsl #16       @ LG
@ row R (pixels 0 / 1 in r7 / r8)
        smlabb  r7, r3, lr, r2              @ lr0 m0 + CT
        smlatb  r8, r3, lr, r2              @ lr1 m0 + CT
        smlabt  r7, r10, lr, r7             @ + lg m1
        smlatt  r8, r10, lr, r8
        smlabb  r7, r5, r6, r7              @ + lb m2
        smlatb  r8, r5, r6, r8
        ldr     r4, [sp, #8]                @ KC = m4 | m5 << 16
        smultt  r7, r7, r12                 @ 2 (idx + t)
        smultt  r8, r8, r12
@ row G (r9 / r11), interleaved with the R code loads
        smlabt  r9, r3, r6, r2              @ lr0 m3 + CT
        ldrh    r7, [r1, r7]                @ r0c
        smlatt  r11, r3, r6, r2
        ldrh    r8, [r1, r8]                @ r1c
        smlabb  r9, r10, r4, r9             @ + lg m4
        smlatb  r11, r10, r4, r11
        smlabt  r9, r5, r4, r9              @ + lb m5
        smlatt  r11, r5, r4, r11
        ldr     r4, [sp, #12]               @ KD = m6 | m7 << 16
        smultt  r9, r9, r12
        smultt  r11, r11, r12
@ row B (r6 / lr), interleaved with the G code loads
        smlabb  r6, r3, r4, r2              @ lr0 m6 + CT
        ldrh    r9, [r1, r9]                @ g0c
        smlatb  lr, r3, r4, r2
        ldrh    r11, [r1, r11]              @ g1c
        smlabt  r6, r10, r4, r6             @ + lg m7
        smlatt  lr, r10, r4, lr
        smlabb  r6, r5, r12, r6             @ + lb m8
        smlatb  lr, r5, r12, lr
        ldr     r3, [sp, #20]               @ 19595
        smultt  r6, r6, r12
        smultt  lr, lr, r12
        mov     r4, #0x800000               @ 2^23
        mov     r5, r9, lsl #14
        ldrh    r6, [r1, r6]                @ b0c
        ldrh    lr, [r1, lr]                @ b1c
        add     r5, r5, r11, lsl #14        @ eg << 14
        add     r10, r7, r8                 @ er
@ Y' (a0 / a1 in r7 / r8) and Cb / Cr
        mla     r7, r3, r7, r4              @ 19595 r + 2^23
        mla     r8, r3, r8, r4
        rsb     r10, r5, r10, lsl #14       @ d1s = (er - eg) << 14
        ldr     r3, [sp, #24]               @ 38470
        add     r4, r6, lr                  @ eb (r4 = 2^23 is dead after the first MLAs)
        rsb     r4, r5, r4, lsl #14         @ d2s = (eb - eg) << 14
        mla     r7, r9, r3, r7              @ + 38470 g
        mla     r8, r11, r3, r8
        ldr     r3, [sp, #28]               @ 7471
        ldr     r5, [sp, #32]               @ KU = -22120 | -10656 << 16
        mla     r7, r6, r3, r7              @ + 7471 b
        mla     r8, lr, r3, r8
        add     r9, r4, #0x800000           @ 2^23 + d2s = 2^23 + 2 (8192 d2)
        add     r11, r10, #0x800000         @ 2^23 + d1s = 2^23 + 2 (8192 d1)
        smlawb  r9, r10, r5, r9             @ + 2 (-2765 d1) = 2^23 + 2 Xu: top byte = (uint8_t)u
        smlawt  r11, r4, r5, r11            @ + 2 (-1332 d2) = 2^23 + 2 Xv: top byte = (uint8_t)v
        and     r3, r8, #0xFF000000         @ ys1 << 24
        and     r5, r7, #0xFF000000         @ ys0 << 24
        orr     r3, r3, r9, lsr #24         @ | u
        orr     r5, r5, r11, lsr #24        @ ys0 << 24 | v
.if \last
        ldr     r6, [sp, #36]               @ end
.endif
        orr     r3, r3, r5, ror #16         @ | ys0 << 8 | v << 16
        str     r3, [r0, #-4]
.endm

        .global wf_log_words
        .type   wf_log_words, %function
@ frame: [sp..sp+16] KA..KE (KE also lives in r12), [sp+20..32] 19595, 38470, 7471, KU, [sp+36] end
wf_log_words:
        cmp     r1, #0
        bxle    lr
        push    {r4-r11, lr}
        sub     sp, sp, #40
        sub     r2, r2, #1                  @ mode - 1
        ldr     r12, .Llit_k
        add     r3, r2, r2, lsl #2          @ 5 (mode - 1)
        add     r12, r12, r3, lsl #2
        ldmia   r12, {r4-r8}                @ KA..KE of this mode
        ldr     r3, .Llit_c
        ldmia   r3, {r9-r12}                @ 19595, 38470, 7471, KU
        stmia   sp, {r4-r12}
        add     r3, r0, r1, lsl #2          @ end
        str     r3, [sp, #36]
        and     r4, r1, #3                  @ n % 4: entry copy
        ldr     r1, .Llit_lasm              @ B = wf_lasm + 1024
        ldr     r3, .Llit_enc
        mov     r12, #0x2000
        orr     r12, r12, #2                @ 8194 = sizeof wf_lenc[0]
        mla     r3, r2, r12, r3             @ wf_lenc[mode - 1]
        sub     r3, r3, r1                  @ 2 t (even)
        mov     r2, #0xA000
        add     r2, r2, r3, lsl #15         @ CT = 40960 + (t << 16)
        mov     r12, r8                     @ KE stays in r12 for the whole loop
        add     pc, pc, r4, lsl #2          @ pc reads as .Ltab: n % 4 = 0 / 1 / 2 / 3
        nop
.Ltab:  b       .Lloop                      @ 0: four copies per pass
        b       .Lc3                        @ 1: last copy first
        b       .Lc2                        @ 2
        b       .Lc1                        @ 3

.Lloop:
        WF_LOGWORD
.Lc1:
        WF_LOGWORD
.Lc2:
        WF_LOGWORD
.Lc3:
        WF_LOGWORD 1
        cmp     r0, r6
        bne     .Lloop

        add     sp, sp, #40
        pop     {r4-r11, pc}

        .align  2
.Llit_k:    .word   wf_lasm_k
.Llit_lasm: .word   wf_lasm + 1024
.Llit_c:    .word   wf_lasm_c
.Llit_enc:  .word   wf_lenc
        .size   wf_log_words, .-wf_log_words

        .section .rodata
        .align  2
wf_lasm_c:
        .word   19595, 38470, 7471, (-22120 & 0xFFFF) | (-10656 << 16)
        .size   wf_lasm_c, .-wf_lasm_c
