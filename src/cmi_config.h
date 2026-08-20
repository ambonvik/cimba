/*
 * cmi_config.h - identify architecture, compiler, and operating system, and
 *                provide portable spellings of the compiler features used in
 *                the public headers.
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
 * Supported toolchains:
 *   - Architecture:     x86-64 / AMD64
 *   - Operating system: Linux, or Windows via MinGW-w64
 *   - Compiler:         GCC or Clang, C11 or later
 *                       (the library itself is built as C23)
 *
 * In particular, MSVC is not supported.
 */

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
 * Identify the compiler. Only GCC and Clang are supported (on Windows, through
 * MinGW-w64); MSVC is not. Test for Clang first, in case it defines __GNUC__
 * for compatibility reasons.
 */
#define CMI_GCC 1
#define CMI_CLANG 2
#if defined (__clang__)
  #define CMI_COMPILER CMI_CLANG
#elif defined (__GNUC__)
  #define CMI_COMPILER CMI_GCC
#else
  #if CMI_OS == CMI_WINDOWS
    #error "Compiler not supported: build and consume Cimba with GCC or Clang (MinGW-w64 on Windows), not MSVC."
  #else
    #error "Compiler not supported: build and consume Cimba with GCC or Clang"
  #endif
#endif

/*
 * Require C11 or later. Cimba uses _Thread_local (C11) for per-worker state,
 * which has no portable pre-C11 spelling, so this is a hard floor. It catches
 * the common mistake of consuming the headers with an older -std= and reports
 * the actual requirement rather than a confusing error from inside a header.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ < 201112L)
  #error "Cimba requires C11 or later (uses _Thread_local)."
#endif

/*
 * Portable spellings of the compiler features used in the public headers, so a
 * downstream translation unit can consume the installed headers at any standard
 * from C11 up, not only C23. These are intentionally GNU-only: the compiler
 * gate above already restricts the toolchain to GCC/Clang, so no MSVC or
 * generic-C fallback branch is needed (nor could one ever be reached). Each
 * macro is a diagnostic or optimization hint only; dropping or substituting it
 * changes neither the ABI nor any runtime behavior.
 */

/* Declares that a function never returns. Prefix the declaration:
 *     CMB_NORETURN extern void f(void);
 * Suppresses "unused" warnings on a static inline helper. Prefix it:
 *     CMB_MAYBE_UNUSED static inline int g(void) { ... } */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
    #define CMB_NORETURN     [[noreturn]]
    #define CMB_MAYBE_UNUSED [[maybe_unused]]
#else
    #define CMB_NORETURN     __attribute__((noreturn))
    #define CMB_MAYBE_UNUSED __attribute__((unused))
#endif

/* Basename of the current source file, for assert messages. __FILE_NAME__ is a
 * GNU extension (GCC 12+, Clang 9+); older GCC/Clang fall back to the full path
 * in __FILE__, a cosmetic difference only. */
#if defined(__FILE_NAME__)
    #define CMB_FILE_NAME __FILE_NAME__
#else
    #define CMB_FILE_NAME __FILE__
#endif

/* Requests printf-style format checking of a variadic function. Postfix the
 * declaration:  extern void f(const char *fmt, ...) CMB_ATTR_PRINTF(1, 2); */
#define CMB_ATTR_PRINTF(fmt_idx, first_arg) \
    __attribute__((format(printf, fmt_idx, first_arg)))

/*
 * Thread-local storage qualifier. _Thread_local is C11; on Linux we pin the TLS
 * model to initial-exec for fast access from the worker threads.
 */
#if CMI_OS == CMI_LINUX
    #define CMB_THREAD_LOCAL _Thread_local __attribute__((tls_model("initial-exec")))
#else
    #define CMB_THREAD_LOCAL _Thread_local
#endif

#endif /* CIMBA_CMI_CONFIG_H */