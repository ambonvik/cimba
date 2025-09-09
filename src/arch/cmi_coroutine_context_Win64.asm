;
; cmi_coroutine_context_Win64.asm
; Context switch and trampoline for coroutines,
; For 64-bits Windows on AMD64/x86-64 architecture.
; Written in NASM syntax.
;
; Copyright (c) Asbjørn M. Bonvik 2025.
;
; Adapted from:
;   Hirbod Banham (2023): "User Context Switcher"
;       https://github.com/HirbodBehnam/UserContextSwitcher
;       Open source under MIT license.
; and from:
;   Malte Skarupke (2013): "Handmade Coroutines for Windows",
;       https://probablydance.com/2013/02/20/handmade-coroutines-for-windows/,
;       All code examples in the linked page placed in public domain by its author.
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;   http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

SECTION .text
global cmi_coroutine_context_switch
global cmi_coroutine_launcher
global asm_test

section .text

asm_test:
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 32

    mov rax, r8

    mov rsp, rbp
    pop rbp
    ret

cmi_coroutine_context_switch:
    ; Store NT_TIB stack info members. Needed for Windows _chkstk.
    mov rax, [gs:8]
    push rax
    mov rax, [gs:16]
    push rax
    ; Save flags register
    pushfq
    ; Allocate space and save MXCSR (SSE status register)
    sub rsp, 8
    stmxcsr [rsp + 4]
    ; Store general purpose registers on the stack
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    ; Align stack and allocate space for XMM registers.
    ; XMM6-15 : 10 registers, 16 bytes each, aligned to 16-byte boundary
    sub rsp, 168
    ; Save XMM registers
    movaps [rsp + 144], xmm15
    movaps [rsp + 128], xmm14
    movaps [rsp + 112], xmm13
    movaps [rsp + 96], xmm12
    movaps [rsp + 80], xmm11
    movaps [rsp + 64], xmm10
    movaps [rsp + 48], xmm9
    movaps [rsp + 32], xmm8
    movaps [rsp + 16], xmm7
    movaps [rsp + 0], xmm6
    ; Store stack pointer to address given as first argument
    mov [rcx], rsp

context_switch_finish:
    ; Load the address given as second argument to new stack pointer
    mov rsp, rdx
    ; Restore XMM registers to new context
    movaps xmm6, [rsp + 0]
    movaps xmm7, [rsp + 16]
    movaps xmm8, [rsp + 32]
    movaps xmm9, [rsp + 48]
    movaps xmm10, [rsp + 64]
    movaps xmm11, [rsp + 80]
    movaps xmm12, [rsp + 96]
    movaps xmm13, [rsp + 112]
    movaps xmm14, [rsp + 128]
    movaps xmm15, [rsp + 144]
    add rsp, 168
    ; Restore MXCSR
    ldmxcsr [rsp + 4]
    add rsp, 8
    ; Restore flags
    popfq
    ; Restore general purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ; Restore NT_TIB stack info members.
    pop rax
    mov [gs:16], rax
    pop rax
    mov [gs:8], rax
    ; Load whatever was in the third argument as return value
    mov rax, r8
    ; Return to wherever the new context was calling from
    ret

; Launch a new coroutine by calling its function and wait as
; the return at the end of its stack, ready to catch it if
; the coroutine function attempts to return. If so, set the
; coroutine state to finished and store its return value in
; the coroutine struct before yielding for the last time.
cmi_coroutine_launcher:
    ; Not a leaf function, needs to obey Win64 ABI calling convention
    ; requiring the stack to be 16-byte aligned before a call
    ; and requires 32 bytes of "shadow space" for the callee.
    and rsp, -16
    sub rsp, 32

; TODO - work out the details here