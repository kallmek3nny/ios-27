/* Original SDK-free AArch64 Darwin ObjC message entrypoints.
 * Preserve integer, floating-point and indirect-result argument registers.
 * Unknown sends fail closed with a zero result; callers can diagnose a
 * failed ios27_objc20b_resolve before invoking native code. */
.text
.p2align 2
.globl _objc_msgSend
.globl _objc_msgSendSuper2
_objc_msgSend:
    sub sp, sp, #224
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x8, [sp, #64]
    str x30, [sp, #72]
    stp q0, q1, [sp, #80]
    stp q2, q3, [sp, #112]
    stp q4, q5, [sp, #144]
    stp q6, q7, [sp, #176]
    bl _ios27_objc20b_resolve_global
    mov x16, x0
    b Lrestore
_objc_msgSendSuper2:
    sub sp, sp, #224
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x8, [sp, #64]
    str x30, [sp, #72]
    stp q0, q1, [sp, #80]
    stp q2, q3, [sp, #112]
    stp q4, q5, [sp, #144]
    stp q6, q7, [sp, #176]
    bl _ios27_objc20b_resolve_super_global
    mov x16, x0
    cbz x16, Lrestore
    ldr x0, [sp, #0]
    ldr x0, [x0] /* objc_super.receiver, validated by resolver */
    str x0, [sp, #0]
Lrestore:
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldr x8, [sp, #64]
    ldr x30, [sp, #72]
    ldp q0, q1, [sp, #80]
    ldp q2, q3, [sp, #112]
    ldp q4, q5, [sp, #144]
    ldp q6, q7, [sp, #176]
    add sp, sp, #224
    cbz x16, Lunknown
    br x16
Lunknown:
    mov x0, xzr
    movi v0.16b, #0
    ret
