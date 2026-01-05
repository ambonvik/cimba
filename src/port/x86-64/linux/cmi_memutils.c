/*
 * cmi_memutils.c - System dependent utility functions, Linux/Posix version
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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "cmb_assert.h"
#include "cmi_memutils.h"

/*
 * cmi_pagesize : Get page size from OS.
 * Should be 4096 bytes, but better check.
 */
size_t cmi_pagesize(void)
{
    return (size_t)sysconf(_SC_PAGESIZE);
}

/*
 * cmi_aligned_alloc : Allocate memory aligned to some alignment value > 8
 * (as malloc gives by default). Straightforward call to aligned_alloc under
 * Linux/Posix, not so on Windows, hence the need for a wrapper function for
 * encapsulating the difference.
 *
 * Strict requirements to arguments: powers of two, multiples of 8 (bytes),
 * and the sz argument needs to be an integer multiple of the alignment.
 * Usage example: align to page size, allocate an integer multiple of page size.
 */
void *cmi_aligned_alloc(const size_t align, const size_t sz)
{
    cmb_assert_release(align > 8u);
    cmb_assert_release((align % sizeof(void*)) == 0u);
    cmb_assert_release(cmi_is_power_of_two(align));
    cmb_assert_release(sz > 8u);
    cmb_assert_release((sz % align) == 0u);

    void *r = aligned_alloc(align, sz);
    cmb_assert_release(r != NULL);

    return r;
}

/*
 * cmi_aligned_free : Free a previously allocated aligned memory area.
 * Windows requires a separate function for this, hence the wrapper.
 */
void cmi_aligned_free(void *p)
{
    cmb_assert_release(p != NULL);
    free(p);
}

/*
 * cmi_aligned_realloc : Reallocate a previously allocated aligned memory area.
 * No standard C function for this, only in Windows, so we emulate it here.
 *
 * We keep the argument order of a hypothetical standard C function for
 * consistency with standard realloc(ptr, sz) and aligned_alloc(alignment, sz)
 */
void *cmi_aligned_realloc(void *p, const size_t align, const size_t sz)
{
    cmb_assert_release(p != NULL);
    cmb_assert_release(align > 8u);
    cmb_assert_release((align % sizeof(void*)) == 0u);
    cmb_assert_release(cmi_is_power_of_two(align));
    cmb_assert_release(sz > 8u);
    cmb_assert_release((sz % align) == 0u);

    /* Emulate realloc behavior for aligned memory on Linux */
    void *r = aligned_alloc(align, sz);
    cmb_assert_release(r != NULL);

    const size_t old_sz = malloc_usable_size(p);
    const size_t copy_sz = (old_sz < sz) ? old_sz : sz;
    memcpy(r, p, copy_sz);
    free(p);

    return r;
}