/*
 * cmi_memutils_Win64.c - System dependent utility functions
 *
 * Copyright (c) Asbjørn M. Bonvik 2025.
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

#include <malloc.h>
#include <windows.h>

#include "cmb_assert.h"
#include "cmi_memutils.h"

/* Inlined function from cmi_memutils.h */
extern void *cmi_malloc(size_t sz);
extern void *cmi_calloc(unsigned n, size_t sz);
extern void *cmi_realloc(void *p, size_t sz);
extern void cmi_free(void *p);
extern void *cmi_memcpy(void *dest, const void *src, size_t sz);
extern void *cmi_memset(void *ptr, int c, size_t n);
extern bool cmi_is_power_of_two(size_t n);


/*
 * cmi_get_pagesize : Get page size from OS.
 * Should be 4096 bytes, but better check.
 */
size_t cmi_get_pagesize(void)
{
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);

    return sys_info.dwPageSize;
}

/*
 * cmi_aligned_alloc : Allocate memory aligned to some alignment value > 8
 * (as malloc gives by defeult). See also:
 * https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc
 *
 * Strict requirements to arguments, need to be powers of two, multiples of 8 (bytes),
 * and the sz argument needs to be an integer multiple of the alignment.
 * Usage example: align to page size, allocate an integer multiple of page size.
 */
void *cmi_aligned_alloc(const size_t align, const size_t sz)
{
    cmb_assert_debug(align > 8u);
    cmb_assert_debug((align % sizeof(void*)) == 0u);
    cmb_assert_debug(cmi_is_power_of_two(align));
    cmb_assert_debug(sz > 8u);
    cmb_assert_debug((sz % align) == 0u);

    /* Note reversed order of arguments vs C standard aligned_alloc */
    void *r = _aligned_malloc(sz, align);
    cmb_assert_release(r != NULL);

    return r;
}

/*
 * cmi_aligned_free : Free a previously allocated aligned memory area.
 * Windows requires a separate function for this, see also:
 *   https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-free
 */
void cmi_aligned_free(void *p)
{
    cmb_assert_debug(p != NULL);
    _aligned_free(p);
}

/*
 * cmi_aligned_realloc : Reallocate a previously allocated aligned memory area.
 * No standard C function for this, only in Windows.
 *   https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-realloc
 *
 * However, we keep the argument order of a hypothetical standard C function for
 * consistency with standard realloc(ptr, sz) and aligned_alloc(alignment, sz)
 */
void *cmi_aligned_realloc(void *p, const size_t align, const size_t sz)
{
    cmb_assert_debug(p != NULL);
    cmb_assert_debug(align > 8u);
    cmb_assert_debug((align % sizeof(void*)) == 0u);
    cmb_assert_debug(cmi_is_power_of_two(align));
    cmb_assert_debug(sz > 8u);
    cmb_assert_debug((sz % align) == 0u);

    /* Note reversed order of arguments vs C standard aligned_alloc */
    void *r = _aligned_realloc(p, sz, align);
    cmb_assert_release(r != NULL);

    return r;
}