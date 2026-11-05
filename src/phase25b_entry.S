/* AArch64 private prototype of objc_msgSend / objc_msgSendSuper2.
 * Not a drop-in libobjc and deliberately limited to registered scalar ABIs.
 * Finalizer runs AFTER IMP returns, with x0/x1 and q0/q1 preserved. */
#if defined(__APPLE__)
#define P25SYM(x) _##x
#else
#define P25SYM(x) x
#endif
.text
.p2align 2
.globl P25SYM(phase25b_objc_msgSend)
.globl P25SYM(phase25b_objc_msgSendSuper2)
P25SYM(phase25b_objc_msgSend):
    mov w9, #0
    b Lphase25b_begin
P25SYM(phase25b_objc_msgSendSuper2):
    mov w9, #1
Lphase25b_begin:
    sub sp, sp, #320
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x8, [sp, #64]
    stp x29, x30, [sp, #304]
    stp q0, q1, [sp, #80]
    stp q2, q3, [sp, #112]
    stp q4, q5, [sp, #144]
    stp q6, q7, [sp, #176]
    add x2, sp, #224
    mov w3, w9
    bl P25SYM(phase25b_prepare)
    cbz x0, Lphase25b_zero
    mov x16, x0
    ldr x0, [sp, #224] /* actual receiver (super substitution) */
    str x0, [sp, #0]
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldr x8, [sp, #64]
    ldp q0, q1, [sp, #80]
    ldp q2, q3, [sp, #112]
    ldp q4, q5, [sp, #144]
    ldp q6, q7, [sp, #176]
    blr x16
    stp x0, x1, [sp, #256]
    stp q0, q1, [sp, #272]
    add x0, sp, #224
    bl P25SYM(phase25b_finish)
    ldp x0, x1, [sp, #256]
    ldp q0, q1, [sp, #272]
    b Lphase25b_return
Lphase25b_zero:
    mov x0, xzr
    mov x1, xzr
    movi v0.16b, #0
    movi v1.16b, #0
Lphase25b_return:
    ldp x29, x30, [sp, #304]
    add sp, sp, #320
    ret

.p2align 2
.globl P25SYM(phase25b_direct_word1)
P25SYM(phase25b_direct_word1):
    stp x29, x30, [sp, #-16]!
    blr x3
    ldp x29, x30, [sp], #16
    ret
