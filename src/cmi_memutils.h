/*
 * cmi_memutils.h - wrappers for malloc() and his friends
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

#ifndef CIMBA_CMI_MEMUTILS_H
#define CIMBA_CMI_MEMUTILS_H

#include <stdlib.h>
#include <string.h>

#include "../include/cmb_assert.h"

/*
  * Convenience functions to encapsulate repetitive error handling
  */
inline void *cmi_malloc(const size_t sz) {
    cmb_assert_debug(sz > 0);

    void *rp = malloc(sz);
    cmb_assert_release(rp != NULL);

    return rp;
}

inline void *cmi_calloc(const unsigned n, const size_t sz) {
    cmb_assert_debug(n > 0);
    cmb_assert_debug(sz > 0);

    void *rp = calloc(n, sz);
    cmb_assert_release(rp != NULL);

    return rp;
}

inline void *cmi_realloc(void *p, const size_t sz) {
    cmb_assert_debug(p != NULL);
    cmb_assert_debug(sz > 0);

    void *rp = realloc(p, sz);
    cmb_assert_release(rp != NULL);

    return rp;
}

inline void cmi_free(void *p) {
    cmb_assert_debug(p != NULL);

    free(p);
}

static inline void *cmi_memcpy(void* dest, const void* src, const size_t sz) {
    cmb_assert_debug(dest != NULL);
    cmb_assert_debug(src != NULL);
    cmb_assert_debug(sz > 0);

    void *rp = memcpy(dest, src, sz);
    cmb_assert_release(rp != NULL);

    return rp;
}

static inline void *cmi_memset(void *ptr, const int c, const size_t n) {
    cmb_assert_debug(ptr != NULL);
    cmb_assert_debug(n > 0);

    void *rp = memset(ptr, c, n);
    cmb_assert_release(rp != NULL);
    cmb_assert_debug(rp == ptr);

    return rp;
}

/* System dependent utility functions in src/arch/cmi_memutils_*.c */
extern size_t cmi_get_pagesize(void);
extern void *cmi_aligned_alloc(size_t align, size_t sz);
extern void cmi_aligned_free(void *p);
extern void *cmi_aligned_realloc(void *p, size_t align, size_t sz);

#endif /* CIMBA_CMI_MEMUTILS_H */