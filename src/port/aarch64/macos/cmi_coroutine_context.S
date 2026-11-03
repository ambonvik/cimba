/*
 * Context switch and coroutine trampoline for arm64 macOS.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

.text
.p2align 2

.globl _cmi_coroutine_context_switch
_cmi_coroutine_context_switch:
    sub sp, sp, #176
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8, d9, [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [sp, #160]

    mov x9, sp
    str x9, [x0]
    ldr x9, [x1]
    mov sp, x9

    ldp x9, x10, [sp, #160]
    msr fpcr, x9
    msr fpsr, x10
    ldp d14, d15, [sp, #144]
    ldp d12, d13, [sp, #128]
    ldp d10, d11, [sp, #112]
    ldp d8, d9, [sp, #96]
    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp, #0]
    add sp, sp, #176

    mov x0, x2
    ret

.p2align 2
.globl _cmi_coroutine_trampoline
_cmi_coroutine_trampoline:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x0, x20
    mov x1, x21
    blr x19
    mov x16, x22
    ldp x29, x30, [sp], #16
    br x16

.subsections_via_symbols
