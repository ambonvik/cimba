/*
 * cmb_logger.h - centralized logging functions with simulation timestamps
 * Each call to the logger tags the message with a logging flag value.
 * The flag value  is matched against the simulation logging mask. If a
 * bitwise or of the current mask and the provided flags is non-zero, the
 * message gets printed. This allows more combinations of system and user
 * logging levels than a simple linear logging verbosity level.
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025.
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

#ifndef CIMBA_CMB_LOGGER_H
#define CIMBA_CMB_LOGGER_H

#include <stdarg.h>
#include <stdint.h>
#include "cmi_config.h"

/* Using 32-bit unsigned for the flags, top four bits reserved for Cimba use. */
#define CMI_LOGGER_FATAL    0x80000000
#define CMI_LOGGER_ERROR    0x40000000
#define CMI_LOGGER_WARNING  0x20000000
#define CMI_LOGGER_INFO     0x10000000

/* The current logging level. Initially everything on. */
extern CMB_THREAD_LOCAL uint32_t cmi_logger_mask;

/*
 * Set callback function to format simulation times to character
 * strings for output.
 */
extern void cmb_set_timeformatter(char *(*fp)(double));

/*
 * The core logging function, like vfprintf but with logging flags in front
 * of argument list.
 */
#if CMB_COMPILER == GCC || CMB_COMPILER == CLANG
    /*
     * Enlist the compiler's help in typechecking the arguments vs the
     * format string.
     */
    extern int cmb_vfprintf(uint32_t flags,
                            FILE *fp,
                            const char *fmtstr, va_list args)
                            __attribute__((format(printf, 3, 0)));
#else
    extern int cmb_vfprintf(uint32_t flags,
                            FILE *fp,
                            const char *fmtstr,
                            va_list args);
#endif

/*
 * Wrapper functions for predefined message levels.
 * cmb_fatal() terminates the entire simulation, cmb_error() terminates the
 * current replication thread only.
 * Use appropriate function attributes to avoid spurious compiler warnings in
 * unreachable code. No portable way to do this more elegantly, unfortunately.
 */
#if CMB_COMPILER == GCC || CMB_COMPILER == CLANG
    extern void cmb_fatal(FILE *fp, char *fmtstr, ...)
                          __attribute__((noreturn, format(printf,2,3)));
    extern void cmb_error(FILE *fp, char *fmtstr, ...)
                          __attribute__((noreturn, format(printf,2,3)));
    extern void cmb_warning(FILE *fp, char *fmtstr, ...)
                          __attribute__((format(printf,2,3)));
    extern void cmb_info(FILE *fp, char *fmtstr, ...)
                          __attribute__((format(printf,2,3)));
#elif CMB_COMPILER == MSVC
    extern __declspec(noreturn) void cmb_fatal(FILE *fp, char *fmtstr, ...);
    extern __declspec(noreturn) void cmb_error(FILE *fp, char *fmtstr, ...);
    extern void cmb_warning(FILE *fp, char *fmtstr, ...);
    extern void cmb_info(FILE *fp, char *fmtstr, ...);
#else
    #warning "CMB_COMPILER does not match any predefined values"
    extern void cmb_fatal(FILE *fp, char *fmtstr, ...);
    extern void cmb_warning(FILE *fp, char *fmtstr, ...);
    extern void cmb_info(FILE *fp, char *fmtstr, ...);
#endif
#endif /* CIMBA_CMB_LOGGER_H */