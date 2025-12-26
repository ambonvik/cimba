/*
 * cmi_config.h - preprocessor macros to identify architecture, compiler, and
 *                operating system, defining macros for portability.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
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
 * Identify the processor architecture. So far, only AMD64/x86-64 supported.
 */
#define CMI_AMD64 1
#if (defined (__amd64__) || defined (__amd64) || defined (__x86_64__) || \
     defined (__x86_64) || defined (_M_X64) || defined (_M_AMD64))
  #define CMI_ARCH CMI_AMD64
#else
  #error "Platform architecture not yet supported."
#endif

/*
 * Identify the operating system. So far, only Linux and Windows supported.
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
 * Identify the compiler. So far, only GCC and Clang supported.
 * Test for Clang first, in case it defines __GNUC__ for compatibility reasons.
 */
#define CMI_GCC 1
#define CMI_CLANG 2
#if defined (__clang__)
  #define CMI_COMPILER CMI_CLANG
  #define CMB_THREAD_LOCAL _Thread_local
#elif defined (__GNUC__)
  #define CMI_COMPILER CMI_GCC
  #define CMB_THREAD_LOCAL _Thread_local
#else
  #error "Compiler not yet supported."
#endif

#endif /* CIMBA_CMI_CONFIG_H */