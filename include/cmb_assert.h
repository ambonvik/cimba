/*
 * cmb_assert.h - custom replacement for assert.h
 * Provides more detailed error messages when firing, and distinguishes
 * between debug asserts (like assert.h) and release asserts that remain
 * if NDEBUG is defined but go away if NASSERT is defined.
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

#ifndef CIMBA_CMB_ASSERT_H
#define CIMBA_CMB_ASSERT_H

/*
 * Custom assert() with some more info when failing. Failed asserts call cmb_fatal().
 * cmb_assert_release() remains if NDEBUG is defined, but goes away if NASSERT is defined.
 * cmb_assert_debug() goes away if NDEBUG is defined (like standard assert()) or if NASSERT is defined.
 * cmb_assert() is shorthand for cmb_assert_debug()
 */
#ifndef NASSERT
    #if CMB_COMPILER == GCC || CMB_COMPILER == CLANG
        extern void cmi_assert_failed(const char *sourcefile, const char *func, int line, const char *condition) __attribute__((noreturn));
#elif CMB_COMPILER == MSVC
extern __declspec(noreturn) void cmi_assert_failed(const char *sourcefile, const char *func, int line, const char *condition) __attribute__((noreturn));
#else
extern void cmi_assert_failed(const char *sourcefile, const char *func, int line, const char *condition);
#endif

#ifndef NDEBUG
#define cmb_assert_debug(x) ((x) ? (void)(0) : (cmi_assert_failed(__FILE_NAME__, __func__, __LINE__, #x)))
#else
#define cmb_assert_debug(x) ((void)(0))
#endif /* ifndef NDEBUG */
#define cmb_assert_release(x) ((x) ? (void)(0) : (cmi_assert_failed(__FILE_NAME__, __func__, __LINE__, #x)))
#define cmb_assert(x) cmb_assert_debug(x)
#else
#define cmb_assert_debug(x) ((void)(0))
#define cmb_assert_release(x) ((void)(0))
#define cmb_assert(x) cmb_assert_debug(x)
#endif

#endif /* CIMBA_CMB_ASSERT_H */