;
; cmi_random_hwseed_Win64.asm
; Interface to CPU entropy sources.
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
global cmi_cpu_has_rdseed
global cmi_cpu_has_rdrand
global cmi_rdseed
global cmi_rdrand
global cmi_threadid
global cmi_rdtsc

section .text

; Get rdseed flag from cpuid
cmi_cpu_has_rdseed:
    push rbx           ; Preserve RBX as per Windows x64 calling convention
    mov eax, 7         ; Load CPUID leaf 07h
    xor ecx, ecx       ; Sub-leaf 0
    cpuid              ; Call CPUID, returning values in EBX, ECX, EDX
    mov eax, ebx       ; Move relevant result to return register EAX
    shr eax, 18        ; Shift right 18 bits
    and eax, 1         ; Mask to get just bit 18
    pop rbx            ; Restore RBX
    ret

; Get rdrand flag from cpuid
cmi_cpu_has_rdrand:
    push rbx           ; Preserve RBX to follow calling convention
    mov eax, 1         ; Load CPUID leaf 01h
    cpuid              ; Call CPUID, returning values in EBX, ECX, EDX
    mov eax, ecx       ; Move relevant result to EAX
    shr eax, 30        ; Shift right 30 bits (RDRAND is bit 30 of ECX)
    and eax, 1         ; Mask to get just that bit
    pop rbx            ; Restore RBX
    ret

; Get rdseed
cmi_rdseed:
    rdseed rax              ; Request a 64-bit true random value in RAX
    jnc cmi_rdseed_retry    ; Check carry flag, retry if not set as call failed
    ret
cmi_rdseed_retry:
    pause                   ; Entropy buffer empty, wait a few cycles for refill
    jmp cmi_rdseed          ; ... and retry

; Get rdrand (used if rdseed is not available)
cmi_rdrand:
    rdrand rax              ; Request a 64-bit true random value in RAX
    jnc cmi_rdrand          ; Retry immediately if carry flag not set
    ret

; Get current thread ID and CPU cycle count (used if nothing else is available)
cmi_threadid:
    mov rax, qword [gs:0x48]    ; Thread ID is at offset 0x48 in TIB
    ret

cmi_rdtsc:
    rdtsc                  ; Read time-stamp counter into EDX:EAX
    shl rdx, 32            ; Shift high 32 bits into position
    or rax, rdx            ; Combine with low 32 bits to form 64-bit value
    ret