/**
 * @file cmb_logger.h
 * @brief Centralized logging functions with simulation timestamps.
 *
 * Each call to the logger tags the message with a logging flag value.
 * The flag value  is matched against the simulation logging mask. If a
 * bitwise or of the current mask and the provided flags is non-zero, the
 * message gets printed. This allows more combinations of system and user
 * logging levels than a simple linear logging verbosity level.
 *
 * Using 32-bit unsigned for the flags, top four bits reserved for Cimba use.
 */

/*
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
#include <stdio.h>

#include "cmi_config.h"

/**
 * @brief Flag value for fatal error, terminates program.
 */
#define CMB_LOGGER_FATAL    0x80000000ul
/**
 * @brief Flag value for error, terminates thread.
 */
#define CMB_LOGGER_ERROR    0x40000000ul
/**
 * @brief Flag value for a warning message.
 */
#define CMB_LOGGER_WARNING  0x20000000ul
/**
 * @brief Flag value for an information message.
 */
#define CMB_LOGGER_INFO     0x10000000ul

/**
 * @brief Turn on logging flags according to the bitmask, for example
 * `cmb_logger_flags_on(CMB_LOGGER_INFO)`, or some user-defined mask.
 * The user application can freely define up to 28 different flag values for
 * fine-grained logging control.
 *
 * The initial value is `0xFFFFFFFF', printing everything.
 *
 * @param flags A 32-bit bitmask, top 4 bits reserved for Cimba use, the rest
 *              user defined.
 */
 extern void cmb_logger_flags_on(uint32_t flags);

/**
 * @brief Turn off logging flags according to the bitmask, for example
 * `cmb_logger_flags_off(CMB_LOGGER_INFO)`, or some user-defined mask.
 *
 * The initial value is `0xFFFFFFFF', printing everything. Use this function to
 * selectively turn off unwanted verbosity.
 *
 * @param flags A 32-bit bitmask, top 4 bits reserved for Cimba use, the rest
 *              user defined.
 */
extern void cmb_logger_flags_off(uint32_t flags);

/**
 * @brief Prototype function to format simulation times into strings for output,
 * taking a `double`as argument and returning a `const char *`.
 */
typedef const char *(cmb_timeformatter_func)(double t);

/**
 * @brief Set function to format simulation times into strings for output.
 *
 * @param tf A formatting function taking a `double`as argument and returning a
 * `const char *`.
 */
extern void cmb_set_timeformatter(cmb_timeformatter_func *tf);

#if CMB_COMPILER == GCC || CMB_COMPILER == CLANG
    /**
    * @brief The core logging function, like vfprintf but with logging flags in
    *        front of argument list. Will usually be called from one of the
    *        wrapper functions, e.g., `cmb_logger_info`, not directly.
    *
    * @param fp File pointer, possibly `stdout` or `stderr`
    * @param flags Bitmask for this logging call, to be matched against the
    *              current logging bitmask. If bitwise `and` is non-zero, this
    *              logging message will be printed.
    * @param func  The function name where it is called from.
    * @param line  The line number it is called from.
    * @param fmtstr A printf-like format string.
    * @param args  The arguments to the format string.
    */
    extern int cmb_vfprintf(FILE *fp,
                            uint32_t flags,
                            const char *func,
                            int line,
                            const char *fmtstr,
                            va_list args)
                            __attribute__((format(printf, 3, 0)));
#else
    /**
    * @brief The core logging function, like vfprintf but with logging flags in
    *        front of argument list. Will usually be called from one of the
    *        wrapper functions, e.g., `cmb_logger_info`, not directly.
    *
    * @param fp File pointer, possibly `stdout` or `stderr`
    * @param flags Bitmask for this logging call, to be matched against the
    *              current logging bitmask. If bitwise and is non-zero, this
    *              will be printed.
    * @param func  The function name where it is called from.
    * @param line  The line number it is called from.
    * @param fmtstr A printf-like format string.
    * @param args  The arguments to the format string.
    */
    extern int cmb_vfprintf(FILE *fp,
                            uint32_t flags,
                            const char *func,
                            int line,
                            const char *fmtstr,
                            va_list args);
#endif

/*
 * Wrapper functions for predefined message levels.
 * cmb_logger_fatal() terminates the entire simulation,
 * cmb_logger_error() terminates the current replication thread only.
 *
 * Use appropriate function attributes to avoid spurious compiler warnings in
 * unreachable code. No portable way to do this more elegantly, unfortunately.
 */

/**
 * @brief Wrapper for a fatal error message. Terminates the program when called.
 *        Written as a macro to provide the calling function name and line
 *        number automatically.
 * @param fp File pointer, possibly `stderr`
 * @param fmtstr printf-like format string
 * @param ... Arguments to the format string.
 */
#define cmb_logger_fatal(fp, fmtstr, ...) \
    cmi_logger_fatal(fp, __func__, __LINE__, fmtstr,##__VA_ARGS__)

/**
 * @brief Wrapper for an error message. Terminates the thread when called.
 *        Written as a macro to provide the calling function name and line
 *        number automatically.
 * @param fp File pointer, possibly `stderr`
 * @param fmtstr printf-like format string
 * @param ... Arguments to the format string.
 */
#define cmb_logger_error(fp, fmtstr, ...) \
    cmi_logger_error(fp, __func__, __LINE__, fmtstr, ##__VA_ARGS__)

/**
 * @brief Wrapper for a warning message. Written as a macro to provide the
 *        calling function name and line number automatically.
 * @param fp File pointer, possibly `stdout` or `stderr`
 * @param fmtstr printf-like format string
 * @param ... Arguments to the format string.
 */
#define cmb_logger_warning(fp, fmtstr, ...) \
    cmi_logger_warning(fp, __func__, __LINE__, fmtstr, ##__VA_ARGS__)

/**
 * @brief Wrapper for an information message. Written as a macro to provide the
 *        calling function name and line number automatically.
 * @param fp File pointer, possibly `stdout`
 * @param fmtstr printf-like format string
 * @param ... Arguments to the format string.
 */

#define cmb_logger_info(fp, fmtstr, ...) \
    cmi_logger_info(fp, __func__, __LINE__, fmtstr, ##__VA_ARGS__)

/**
 * @brief Wrapper for an application-defined message. Written as a macro to
 *        provide the calling function name and line number automatically.
 * @param fp File pointer, possibly `stdout`
 * @param flags The user-defined logging bitmask for this logging call, to be
 *              matched against the current logging bitmask. If bitwise `and` is
 *              non-zero, this will be printed.
 * @param fmtstr printf-like format string
 * @param ... Arguments to the format string.
 */

#define cmb_logger_user(fp, flags, fmtstr, ...) \
    cmi_logger_user(fp, flags, __func__, __LINE__, fmtstr, ##__VA_ARGS__)

/** @cond */
#if CMB_COMPILER == GCC || CMB_COMPILER == CLANG
    extern void cmi_logger_fatal(FILE *fp, const char *func, int line, char *fmtstr, ...)
                          __attribute__((noreturn, format(printf,4,5)));
    extern void cmi_logger_error(FILE *fp, const char *func, int line, char *fmtstr, ...)
                          __attribute__((noreturn, format(printf,4,5)));
    extern void cmi_logger_warning(FILE *fp, const char *func, int line, char *fmtstr, ...)
                          __attribute__((format(printf,4,5)));
    extern void cmi_logger_info(FILE *fp, const char *func, int line, char *fmtstr, ...)
                          __attribute__((format(printf,4,5)));
    extern void cmi_logger_user(FILE *fp, uint32_t flags, const char *func, int line, char *fmtstr, ...)
                          __attribute__((format(printf,5,6)));
#elif CMB_COMPILER == MSVC
    extern __declspec(noreturn) void cmi_logger_fatal(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern __declspec(noreturn) void cmi_logger_error(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern void cmi_logger_warning(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern void cmi_logger_info(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern void cmi_logger_user(FILE *fp, uint32_t flags, const char *func, int line, char *fmtstr, ...)
#else
    #warning "CMB_COMPILER does not match any predefined values"
    extern void cmi_logger_fatal(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern void cmi_logger_warning(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern void cmi_logger_info(FILE *fp, const char *func, int line, char *fmtstr, ...);
    extern void cmi_logger_user(FILE *fp, uint32_t flags, const char *func, int line, char *fmtstr, ...);
#endif
/** @endcond */

#endif /* CIMBA_CMB_LOGGER_H */