;
; cmi_coroutine_context_Win64.asm
; Context switch and trampoline for coroutines,
; For 64-bits Windows on AMD64/x86-64 architecture.
; Written in NASM syntax.
;
; Copyright (c) Asbjørn M. Bonvik 2025.
;
; Adapted from Malte Skarupke (2013): "Handmade Coroutines for Windows",
;   https://probablydance.com/2013/02/20/handmade-coroutines-for-windows/,
;   All code examples in link placed in public domain by author.
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
global switch_to_context
global stack_switch_finish
global callable_context_start
global asm_test

section .text

asm_test:
    mov rax, rcx
    ret

switch_to_context:
    ; Store NT_TIB stack info members
    push qword [gs:8]
    push qword [gs:16]
    ; Save flags register
    pushfq
    ; Allocate space and save MXCSR
    sub rsp, 8
    stmxcsr [rsp + 4]
    ; Store rbx and r12 to r15 on the stack
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    ; Align stack and allocate space for XMM registers
    sub rsp, 176         ; Space for XMM6-XMM15 (16 bytes each, aligned)
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
    movaps [rsp + 144], xmm15
    mov [rcx], rsp    ; store stack pointer
    ret

stack_switch_finish:
    ; set up the other guy's stack pointers
    mov rsp, rdx
    ; and we are now in the other context
    ; restore registers
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
    add rsp, 176
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
    ; Restore NT_TIB stack info members. This is needed because _chkstk will
    ; use these members to step the stack,S so we need to make sure that it, and
    ; any other functions that use the stack information, get correct values
    pop qword [gs:16]
    pop qword [gs:8]
    ret    ; go to whichever code is used by the other stack

callable_context_start:
    mov rcx, rdi    ; function_argument
    call rbp        ; function
    mov rdx, [rbx]  ; caller_stack_top
    jmp stack_switch_finish