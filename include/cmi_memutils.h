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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
  * Convenience functions to encapsulate repetitive error handling
  */
inline void *cmi_malloc(const size_t sz) {
    assert(0 < sz);

    void *rp = malloc(sz);
    assert(NULL != rp);

    return rp;
}

inline void *cmi_calloc(const unsigned n, const size_t sz) {
    assert(0 < n);
    assert(0 < sz);

    void *rp = calloc(n, sz);
    assert(NULL != rp);

    return rp;
}

inline void *cmi_realloc(void *p, const size_t sz) {
    assert(NULL != p);
    assert(0 < sz);

    void *rp = realloc(p, sz);
    assert(NULL != rp);

    return rp;
}

inline void cmi_free(void *p) {
    assert(NULL != p);

    free(p);
}

static inline void *cmi_memcpy(void* dest, const void* src, const size_t sz ) {
    assert(NULL != dest);
    assert(NULL != src);
    assert(0 < sz);

    void *rp = memcpy(dest, src, sz);
    assert(NULL != rp);
    assert(NULL != rp);

    return rp;
}

#endif /* CIMBA_CMI_MEMUTILS_H */
