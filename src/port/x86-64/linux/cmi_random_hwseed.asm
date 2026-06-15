;
; cmi_random_hwseed.asm
; Interface to CPU entropy sources.
; For 64-bits Linux on AMD64/x86-64 architecture.
; Written in NASM syntax.
;
; Copyright (c) Asbjørn M. Bonvik 2025-26.
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

default rel

; Explicit Intel CET posture: This object honors no CET features.
; GNU_PROPERTY_X86_FEATURE_1_AND with value 0 -> no IBT, no SHSTK. The linker
; AND-merges this across all inputs, so it keeps Intel CET disabled for any
; binary that links Cimba.
section .note.gnu.property note alloc noexec nowrite align=8
    dd 4                    ; n_namesz  = sizeof "GNU\0"
    dd 16                   ; n_descsz  = size of the property array (incl. pad)
    dd 5                    ; n_type    = NT_GNU_PROPERTY_TYPE_0
    db "GNU", 0             ; n_name
    dd 0xc0000002           ; pr_type   = GNU_PROPERTY_X86_FEATURE_1_AND
    dd 4                    ; pr_datasz = 4
    dd 0                    ; pr_data   = 0  -> no IBT (0x1), no SHSTK (0x2)
    dd 0                    ; pad to 8-byte alignment

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global cmi_cpu_has_rdseed
global cmi_cpu_has_rdrand
global cmi_rdseed
global cmi_rdrand

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

; Get rdseed from system entropy buffer
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
