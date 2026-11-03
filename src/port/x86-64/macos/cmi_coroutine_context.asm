;
; Context switch and coroutine trampoline for x86-64 macOS.
; Mach-O symbols use a leading underscore; the register convention is SysV.
;
; Copyright (c) FBarrca 2026.
; Licensed under the Apache License, Version 2.0.

bits 64
default rel

section .text
global _cmi_coroutine_context_switch
global _cmi_coroutine_trampoline

%macro save_context 0
    pushfq
    %ifndef NMXCSR
        sub rsp, 8
        stmxcsr [rsp + 4]
    %endif
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro load_context 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    %ifndef NMXCSR
        ldmxcsr [rsp + 4]
        add rsp, 8
    %endif
    popfq
%endmacro

_cmi_coroutine_context_switch:
    save_context
    mov [rdi], rsp
    mov rsp, [rsi]
    load_context
    mov rax, rdx
    pop r9
    jmp r9

_cmi_coroutine_trampoline:
    mov rdi, r13
    mov rsi, r14
    xor rax, rax
    call r12
    push rdi
    mov rdi, rax
    jmp r15
