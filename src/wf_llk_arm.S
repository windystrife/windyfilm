@ wf_llk_arm.S - "Log + LUT" shot kernel, hand-scheduled ARMv5T (ARM946E-S), bit-exact with
@ wf_ll_words_c (wf_llk.c, the reference: read its comment for the maths). WindyStrife.
@
@   void wf_ll_words(uint32_t *p, int n, int tabs)    wf_chunks callback, n UYVY words in place,
@                                                     tabs = (int)(struct wf_lltabs *)
@
@ Smooth pairs (|y0 - y1| < 8) here, 89 instructions per pair (88 on the fu < fv side); edge pairs
@ go to wf_ll_edge_w (wf_llk.c) through an ABS32 literal + BLX (no R_ARM_CALL/JUMP24: as
@ an earlier kernel). The triangle is specialised per diagonal side, so every table access is one load
@ at an immediate offset: from t (N, K, L), from t + o (Y), from t + 4 o + 0x9200 (C).
@ No LDRD/STRD; only ARMv4T/v5T instructions (MUL/MLA with Rd != Rm).
@ Struct offsets: wf_loglut.h (checked there by a compile-time assert, and by the host test, test.sh SHOT).

        .syntax unified
        .arm
        .text

        .equ OFF_L,   1536              @ int32_t L[33]
        .equ OFF_Y,   1668              @ uint8_t Y[35937]
        .equ CB_HI,   0x9200            @ C at 37608 = 0x9200 + 232
        .equ CB_LO,   232
        .equ LV_HI,   0x1100            @ one level of C: 1089 * 4 = 4356 = 0x1100 + 4

        .global wf_ll_words
        .type   wf_ll_words, %function
@ r0 p, r1 end, r2 t; per pair: r4 wd, r9 l, r12 uv, r3 r5-r8 r10 r11 lr temporaries
wf_ll_words:
        push    {r4-r12, lr}            @ 40 bytes: sp stays 8-aligned for the C call
        cmp     r1, #0
        ble     .Ldone
        add     r1, r0, r1, lsl #2
.Lloop:
        ldr     r4, [r0]                @ wd = {U s8, Y0, V s8, Y1}
        mov     r5, r4, lsr #8
        and     r5, r5, #255            @ y0
        sub     r6, r5, r4, lsr #24     @ y0 - y1
        add     r6, r6, #7
        cmp     r6, #14
        bhi     .Ledge
        eor     r3, r4, #0x80
        eor     r3, r3, #0x800000       @ x: u = bits 0..7, v = bits 16..23
        add     r5, r5, r4, lsr #24
        add     r5, r5, #1              @ m2 = y0 + y1 + 1
        and     r6, r3, #7              @ fu
        and     r7, r3, #255            @ u
        mov     r7, r7, lsr #3
        add     r7, r7, r7, lsl #5      @ (u >> 3) * 33
        mov     r3, r3, lsr #16         @ v | y1 << 8
        and     r8, r3, #7              @ fv
        and     r3, r3, #255            @ v
        add     r7, r7, r3, lsr #3      @ cell
        mov     r9, r5, lsr #4          @ l
        mov     r3, r5, lsr #1
        and     r3, r3, #7              @ fl
        add     r10, r9, r9, lsl #5     @ 33 l
        add     r10, r10, r10, lsl #5   @ 1089 l
        add     r7, r7, r10             @ o
        add     r11, r2, r7, lsl #2
        add     r11, r11, #CB_HI        @ C[o] at [r11, #CB_LO]
        cmp     r6, r8
        blt     .Ls1

@ side fu >= fv: w0 = 8 - fu (r6), w1 = fu - fv (r10), w2 = fv (r8); nodes o, o + 33, o + 34
        sub     r10, r6, r8
        rsb     r6, r6, #8
        ldr     r12, [r11, #CB_LO]
        ldr     r5, [r11, #CB_LO + 132]
        mul     lr, r12, r6
        ldr     r12, [r11, #CB_LO + 136]
        mla     lr, r5, r10, lr
        add     r11, r11, #LV_HI
        mla     lr, r12, r8, lr         @ a: level l
        ldr     r12, [r11, #CB_LO + 4]
        mul     r5, r12, r6
        ldr     r12, [r11, #CB_LO + 4 + 132]
        mla     r5, r12, r10, r5
        ldr     r12, [r11, #CB_LO + 4 + 136]
        mla     r5, r12, r8, r5         @ b: level l + 1
        sub     r5, r5, lr
        mul     r12, r5, r3             @ (b - a) fl
        add     r12, r12, lr, lsl #3    @ a (8 - fl) + b fl
        mov     r12, r12, lsr #7
        bic     r12, r12, #0xfe00       @ uv: U bits 0..7, V bits 16..23
        add     r11, r2, r7
        ldrb    r3, [r11, #OFF_Y]
        ldrb    r5, [r11, #OFF_Y + 33]
        mul     lr, r3, r6
        ldrb    r3, [r11, #OFF_Y + 34]
        mla     lr, r5, r10, lr
        ldrb    r5, [r11, #OFF_Y + 1089]
        mla     lr, r3, r8, lr          @ Y' sum, level l
        ldrb    r3, [r11, #OFF_Y + 1122]
        mul     r7, r5, r6
        ldrb    r5, [r11, #OFF_Y + 1123]
        mla     r7, r3, r10, r7
        mla     r7, r5, r8, r7          @ Y' sum, level l + 1
        b       .Ltail

@ side fu < fv: w0 = 8 - fv (r8), w1 = fv - fu (r10), w2 = fu (r6); nodes o, o + 1, o + 34
.Ls1:
        sub     r10, r8, r6
        rsb     r8, r8, #8
        ldr     r12, [r11, #CB_LO]
        ldr     r5, [r11, #CB_LO + 4]
        mul     lr, r12, r8
        ldr     r12, [r11, #CB_LO + 136]
        mla     lr, r5, r10, lr
        add     r11, r11, #LV_HI
        mla     lr, r12, r6, lr
        ldr     r12, [r11, #CB_LO + 4]
        mul     r5, r12, r8
        ldr     r12, [r11, #CB_LO + 4 + 4]
        mla     r5, r12, r10, r5
        ldr     r12, [r11, #CB_LO + 4 + 136]
        mla     r5, r12, r6, r5
        sub     r5, r5, lr
        mul     r12, r5, r3
        add     r12, r12, lr, lsl #3
        mov     r12, r12, lsr #7
        bic     r12, r12, #0xfe00
        add     r11, r2, r7
        ldrb    r3, [r11, #OFF_Y]
        ldrb    r5, [r11, #OFF_Y + 1]
        mul     lr, r3, r8
        ldrb    r3, [r11, #OFF_Y + 34]
        mla     lr, r5, r10, lr
        ldrb    r5, [r11, #OFF_Y + 1089]
        mla     lr, r3, r6, lr
        ldrb    r3, [r11, #OFF_Y + 1090]
        mul     r7, r5, r8
        ldrb    r5, [r11, #OFF_Y + 1123]
        mla     r7, r3, r10, r7
        mla     r7, r5, r6, r7

@ E' per pixel on the line through the two levels, then clamp(N[y] + E) = t[N[y] + E']
.Ltail:
        add     r3, r2, r9, lsl #2
        ldr     r5, [r3, #OFF_L]        @ L[l]
        ldr     r6, [r3, #OFF_L + 4]    @ L[l + 1]
        mov     r9, r9, lsl #3          @ 8 l
        and     r8, r4, #0xff00         @ y0 << 8
        sub     lr, lr, r5              @ da
        sub     r7, r7, r6              @ db
        sub     r7, r7, lr              @ d = db - da
        mov     lr, lr, lsl #3          @ 8 da
        rsb     r5, r9, r8, lsr #8      @ y0 - 8 l
        rsb     r6, r9, r4, lsr #24     @ y1 - 8 l
        mla     r3, r7, r5, lr          @ 64 E0'
        mla     r10, r7, r6, lr         @ 64 E1'
        ldrb    r5, [r2, r8, lsr #8]    @ N[y0]
        ldrb    r6, [r2, r4, lsr #24]   @ N[y1]
        add     r5, r5, r3, asr #6
        add     r6, r6, r10, asr #6
        ldrb    r5, [r2, r5]
        ldrb    r6, [r2, r6]
        eor     r12, r12, #0x80
        eor     r12, r12, #0x800000
        orr     r12, r12, r5, lsl #8
        orr     r12, r12, r6, lsl #24
        str     r12, [r0], #4
        cmp     r0, r1
        blo     .Lloop
.Ldone:
        pop     {r4-r12, pc}

.Ledge:                                 @ r0 p, r1 end, r2 t, r4 wd
        push    {r0-r3}
        mov     r0, r2
        mov     r1, r4
        ldr     r12, =wf_ll_edge_w
        blx     r12
        mov     r12, r0
        pop     {r0-r3}
        str     r12, [r0], #4
        cmp     r0, r1
        blo     .Lloop
        pop     {r4-r12, pc}
        .ltorg
        .size   wf_ll_words, . - wf_ll_words
#ifdef __linux__
        .section .note.GNU-stack, "", %progbits   @ host (qemu-arm harness) link only
#endif
