/*
 * cmi_config.h - preprocessor macros to identify architecture, compiler, and
 *                operating system, defining macros for portability.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-26.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CIMBA_CMI_CONFIG_H
#define CIMBA_CMI_CONFIG_H

/*
 * Identify the processor architecture. So far, only AMD64/x86-64 is supported.
 */
#define CMI_AMD64 1
#if (defined (__amd64__) || defined (__amd64) || defined (__x86_64__) || \
     defined (__x86_64) || defined (_M_X64) || defined (_M_AMD64))
  #define CMI_ARCH CMI_AMD64
#else
  #error "Platform architecture not yet supported."
#endif

/*
 * Identify the operating system. So far, only Linux and Windows are supported.
 */
#define CMI_LINUX 1
#define CMI_WINDOWS 2
#if defined (__linux__) || defined (__linux) || defined (linux)
  #define CMI_OS CMI_LINUX
#elif defined (_WIN64) || defined (_WIN32) || defined (__WIN32__)
  #define CMI_OS CMI_WINDOWS
#else
  #error "Platform operating system not yet supported."
#endif

/*
 * Identify the compiler. So far, only GCC and Clang are supported.
 * Test for Clang first, in case it defines __GNUC__ for compatibility reasons.
 */
#define CMI_GCC 1
#define CMI_CLANG 2
#if defined (__clang__)
  #define CMI_COMPILER CMI_CLANG
#elif defined (__GNUC__)
  #define CMI_COMPILER CMI_GCC
#else
  #error "Compiler not yet supported."
#endif

#if CMI_OS == CMI_LINUX
    #define CMB_THREAD_LOCAL _Thread_local __attribute__((tls_model("initial-exec")))
#else
    #define CMB_THREAD_LOCAL _Thread_local
#endif


/*
 * Worker-recovery non-local jump (see worker_thread_func in cimba.c and
 * cmi_logger_error in cmb_logger.c). A trial that calls cmb_logger_error abandons
 * whatever coroutine it is running in and jumps straight back to the worker
 * loop on the thread's own stack, i.e. the jump crosses from a coroutine stack
 * to the thread stack.
 *
 * On Win64 the C library longjmp performs SEH stack unwinding (RtlUnwindEx),
 * which validates each frame against the TEB stack bounds and faults with
 * STATUS_BAD_STACK when the target frame lives on a different stack. The GCC
 * builtins do a plain register restore with no unwinding - exactly the Linux
 * longjmp behaviour - which is what a cross-stack jump needs. The TEB stack
 * fields are then put back by cmi_coroutine_reset_to_main once we have landed.
 * The jmp_buf storage is reused as the builtin buffer (it is far larger than
 * the five words __builtin_setjmp needs).
 *
 * Linux keeps the C library setjmp/longjmp: it unwinds nothing across stacks
 * anyway, and the libc longjmp is the one AddressSanitizer interposes for its
 * no-return stack cleanup.
 */
#if CMI_OS == CMI_WINDOWS
    #define CMI_RECOVERY_SET(buf)   __builtin_setjmp((void **)(buf))
    #define CMI_RECOVERY_JUMP(buf)  __builtin_longjmp((void **)(buf), 1)
#else
    #define CMI_RECOVERY_SET(buf)   setjmp(buf)
    #define CMI_RECOVERY_JUMP(buf)  longjmp((buf), 1)
#endif


#endif /* CIMBA_CMI_CONFIG_H */