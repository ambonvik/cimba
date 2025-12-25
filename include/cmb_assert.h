/**
 * @file cmb_assert.h
 * @brief Custom replacement for assert.h
 *
 * Provides more detailed error messages then the standard `assert`, and
 * distinguishes between debug asserts (like `assert.h`) and release asserts
 * that remain if `NDEBUG` is defined but go away if `NASSERT` is defined.
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

#ifndef CIMBA_CMB_ASSERT_H
#define CIMBA_CMB_ASSERT_H

#include "cmi_config.h"

#ifndef NASSERT
    #if CMB_COMPILER == GCC || CMB_COMPILER == CLANG
        /**
         * @brief The function that reports and aborts when a `cmb_assert_*`is
         * triggered.
         *
         * Important advice: Place a debugger breakpoint in this
         * function, and you will be able to see the call stack and variable
         * values at that point.
         */
        extern void cmi_assert_failed(const char *sourcefile,
                                      const char *func,
                                      int line,
                                      const char *condition)
                                              __attribute__((noreturn));

    #elif CMB_COMPILER == MSVC
        extern __declspec(noreturn) void cmi_assert_failed(const char *sourcefile,
                                      const char *func, int line,
                                      const char *condition);
    #else
        extern void cmi_assert_failed(const char *sourcefile, const char *func,
                              int line, const char *condition);
    #endif

    #ifndef NDEBUG
        /**
         * @brief Drop-in replacement for standard `assert()`, but reporting the
         * simulation time and the simulated process where it triggered.
         *
         * Disappears if `NDEBUG` (or `NASSERT`) is defined. Typically used for
         * verifying invariants and postconditions during development.
         */
        #define cmb_assert_debug(x) ((x) ? (void)(0) : (cmi_assert_failed(__FILE_NAME__, __func__, __LINE__, #x)))
    #else
        #define cmb_assert_debug(x) ((void)(0))
    #endif /* ifndef NDEBUG */

    /**
     * @brief Like `cmb_assert_debug()`, but remains in code also if `NDEBUG` is
     * defined. Typically used for verifying parameter values as valid
     * preconditions to function calls.
     *
     * Disappears if `NASSERT` is defined, e.g., for  production use of a
     * thoroughly debugged model where all parameters are known to be valid and
     * the last ounce of speed is wanted.
     */
    #define cmb_assert_release(x) ((x) ? (void)(0) : (cmi_assert_failed(__FILE_NAME__, __func__, __LINE__, #x)))
#else
    #define cmb_assert_debug(x) ((void)(0))
    #define cmb_assert_release(x) ((void)(0))
#endif

/**
 * @brief Convenience shorthand for cmb_assert_debug()
 */
#define cmb_assert(x) cmb_assert_debug(x)

/**
 * @brief Macro to suppress "unused argument" compiler warning for functions
 * where some generic argument is intentionally unused in that instance.
 *
 * `__attribute__ ((unused))` could be used instead, but it is a GCC extension,
 * not portable. In C23 `[[maybe_unused]]` will do the trick, but again, MVSC.
 * Cimba is written in C17 and avoids C23 innovations until more widely
 * supported by compilers.
 *
 * Placed in this header file because it typically will be used together with
 * the precondition asserts at the start of a function.
 */
#define cmb_unused(x) ((void)(x))


#endif /* CIMBA_CMB_ASSERT_H */