;
; cmi_coroutine_context_Win64.asm
;
; Context switch and launcher/trampoline for coroutines.
; For 64-bits Windows on AMD64/x86-64 architecture.
; Written in NASM syntax.
;
; Copyright (c) Asbjørn M. Bonvik 2025.
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
global cmi_coroutine_trampoline
global cmi_coroutine_get_rsp
global cmi_coroutine_get_stackbase
global cmi_coroutine_get_stacklimit

section .text

;-------------------------------------------------------------------------------
; Callable function to return the current StackBase (top of allocated stack)
;
cmi_coroutine_get_stackbase:
    mov rax, [gs:8]
    ret

;-------------------------------------------------------------------------------
; Callable funnction to return the current StackLimit (bottom of allocated stack)
;
cmi_coroutine_get_stacklimit:
    mov rax, [gs:16]
    ret

;-------------------------------------------------------------------------------
; Macro to store relevant registers to current stack
; In effect taking a (sub-)continuation at this point in execution.
; Assumes that the stack is off 16-byte alignment by 8 bytes at the start of
; this macro (i.e. it was 16-byte aligned, then RIP got pushed, now we are here)
;
%macro save_context 0
    ; Save NT_TIB StackBase, the start of the stack (highest address)
    mov rax, [gs:8]
    push rax
    ; Save NT_TIB StackLimit, the end of the stack (lowest address)
    mov [gs:16], rax
    push rax
    ; Save flags register
    pushfq
    ; Allocate space and save MXCSR (SSE status register)
    sub rsp, 8
    stmxcsr [rsp + 4]
    ; Save general purpose registers
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    ; XMM6-15 : 10 registers, 16 bytes each, 160 bytes needed.
    ; We have pushed 12 8-byte registers so far, plus the
    ; implicit RIP return address. We need 8 bytes extra to align
    ; to 16-byte boundary again.
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
%endmacro

;-------------------------------------------------------------------------------
; Macro to load relevant registers from current stack
;
%macro load_context 0
    ; Restore XMM registers from stack
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
    ; Restore general purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ; Restore MXCSR
    ldmxcsr [rsp + 4]
    add rsp, 8
    ; Restore flags
    popfq
    ; Restore NT_TIB stack info members.
    pop rax
    mov [gs:16], rax
    pop rax
    mov [gs:8], rax
%endmacro

;-------------------------------------------------------------------------------
; Callable function void *cmi_coroutine_context_switch(void **old,
;                                                      void **new,
;                                                      void *ret)
; Arguments:
;   void **old - RCX - address for storing current stack pointer
;   void **new - RDX - address for reading new stack pointer
;   void *ret  - R8  - return value passed from old to new context
; Return value:
;   void *     - RAX - whatever was given as the third argument
; Error handling:
;   None - the samurai returns victorious or not at all
;
cmi_coroutine_context_switch:
    ; Push all callee-saved registers to current stack
    save_context
    ; Store old stack pointer to address given as first argument RCX
    mov [rcx], rsp
    ; Load content of the address in second argument RDX as new stack pointer
    mov rsp, [rdx]
    ; We are now in the new context, restore registers from new stack
    load_context
    ; Load whatever was in the third argument R8 as return value in RAX
    mov rax, r8
    ; Return to wherever the new context was transferring from earlier
    ret

;-------------------------------------------------------------------------------
; Not callable, preloaded as stack return address when activating a coroutine,
; to be "called" by the first cmi_coroutine_context_switch RET instruction.
; Launch the new coroutine by calling its function and waiting, ready to catch
; it if the coroutine function ever attempts to return. If it does, transfer
; control to the exit function loaded in R15 with the value returned from
; the coroutine function as argument.
; Assumes that the stack is 16-byte aligned at the start of the function.
; Expected register content:
;   R12 - coroutine function address
;   R13 - its first argument cp, pointer to this coroutine
;   R14 - its second argument arg, pointer to void
;   R15 - address of coroutine exit function, usually cmb_coroutine_exit
;
cmi_coroutine_trampoline:
    ; Not a leaf function, needs to obey Win64 ABI calling convention
    ; requiring the stack to be 16-byte aligned before a call
    ; and requiring 32 bytes of "shadow space" for the callee.
    ; Ensure alignment...
    and rsp, -16
    ; ...and the shadow space
    sub rsp, 32
    ; Will soon call coroutine function foo(cp, arg).
    ; The address of foo is now in R12, cp in R13, and arg in R14.
    ; The arguments for foo need to be in RCX and RDX, move them there.
    mov rcx, r13
    mov rdx, r14
    ; Clear the return register. Probably not necessary, just to be sure.
    xor rax, rax
    ; And off it goes. We'll be waiting here in case it returns.
    call r12
    ; If we arrive here, it did return. The return value is now in RAX.
    ; Push RCX to (un)align stack to 16-byte boundary since the jmp will
    ; not be storing a return pointer before the callee stack frame.
    push rcx
    mov rcx, rax
    ; Jump to whatever exit function was given when setting up the trampoline.
    jmp r15