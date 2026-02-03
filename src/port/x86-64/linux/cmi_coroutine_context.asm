;
; cmi_coroutine_context.asm
;
; Context switch and launcher/trampoline for coroutines.
; For 64-bits Linux on AMD64/x86-64 architecture.
; See https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
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

bits 64
default rel

section .text
global cmi_coroutine_context_switch
global cmi_coroutine_trampoline

;-------------------------------------------------------------------------------
; Macro to store relevant registers to current stack
; In effect taking a (sub-)continuation at this point in execution.
;
%macro save_context 0
    ; Save flags register
    pushfq
    ; Allocate space and save MXCSR (SSE status register)
    sub rsp, 8
    stmxcsr [rsp + 4]
    ; Save general purpose registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
%endmacro

;-------------------------------------------------------------------------------
; Macro to load relevant registers from current stack
;
%macro load_context 0
    ; Restore general purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ; Restore MXCSR
    ldmxcsr [rsp + 4]
    add rsp, 8
    ; Restore flags
    popfq
%endmacro

;-------------------------------------------------------------------------------
; cmi_coroutine_context_switch: Callable function.
;       void *cmi_coroutine_context_switch( void **old,
;                                           void **new,
;                                           void *ret )
; Arguments:
;   void **old - register RDI - address for storing current stack pointer
;   void **new - register RSI - address for reading new stack pointer
;   void *ret  - register RDX - return value passed from old to new context
; Return value:
;   void *     - register RAX - whatever was given as the third argument
; Error handling:
;   None - The samurai returns victorious or not at all. Loading a NULL stack
;          pointer will have immediate consequences, no assert() needed to trap.
;
cmi_coroutine_context_switch:
    ; Push all callee-saved registers to current stack
    save_context
    ; Store old stack pointer to address given as first argument RDI
    mov [rdi], rsp
    ; Load content of the address in second argument RSI as new stack pointer
    mov rsp, [rsi]
    ; We are now in the new context, restore registers from new stack
    load_context
    ; Load whatever was in the third argument RDX as return value in RAX
    mov rax, rdx
    ; Return to wherever the new context was transferring from earlier
    ret

;-------------------------------------------------------------------------------
; cmi_coroutine_trampoline: Not callable, preloaded as stack return address when
;                           activating a coroutine, to be "called" by the first
;                           cmi_coroutine_context_switch  RET instruction.
;
; Launch the new coroutine by calling its function and waiting, ready to catch
; it if the coroutine function ever attempts to return. If it does, transfer
; control to the exit function loaded in R15 with the value returned from
; the coroutine function as argument.
; Assumes that the stack is 16-byte aligned at the start of the function.
; Expected register content:
;   R12 - coroutine function address
;   R13 - its first argument cp, pointer to this coroutine
;   R14 - its second argument arg, pointer to void
;   R15 - address of coroutine exit function, usually cmi_coroutine_exit
;
cmi_coroutine_trampoline:
    ; Is not a leaf function, needs to obey Linux ABI calling convention.
    ; Expected struct cmi_coroutine layout (from cmi_coroutine.h):
    ; - parent (8 bytes, offset 0)
    ; - caller (8 bytes, offset 8)
    ; - stack (8 bytes, offset 16)
    ; - stack_base (8 bytes, offset 24)
    ; - stack_limit (8 bytes, offset 32)
    ;
    ; Will soon call coroutine function foo(cp, arg).
    ; The address of foo is now in R12, cp in R13, and arg in R14.
    ; The arguments for foo need to be in RDI and RSI, move them there.
    mov rdi, r13
    mov rsi, r14
    ; Clear the return register. Probably not necessary, just to be sure.
    xor rax, rax
    ; And off it goes. We'll be waiting here in case it returns.
    call r12
    ; If we arrive here, it did return. The return value is now in RAX.
    ; Push RDI to (un)align stack to 16-byte boundary since the jmp will
    ; not be storing a return pointer before the callee stack frame.
    push rdi
    ; Load the coroutine return value as the argument to the exit function.
    mov rdi, rax
    ; Jump to whatever exit function was given when setting up the trampoline.
    jmp r15