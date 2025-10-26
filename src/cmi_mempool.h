/*
 * cmi_mempool.h - internal memory pool for generic small objects.
 *
 * Provides a memory pool for efficient allocation and reuse of "generic small
 * objects" of fixed size. Each memory pool maintains objects of a certain
 * size, such as 32 or 64 bytes. The object size must be a multiple of 8 bytes.
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
#ifndef CIMBA_CMI_MEMPOOL_H
#define CIMBA_CMI_MEMPOOL_H

#include <stdint.h>

#include "cmb_assert.h"

/*
 * struct cmi_mempool : The memory pool for a particular size object.
 */
struct cmi_mempool {
    size_t obj_sz;
    size_t incr_num;
    size_t incr_sz;
    uint64_t area_list_len;
    uint64_t area_list_cnt;
    void **area_list;
    void *next_obj;
};

/*
 * cmi_mempool_create : Set up a memory pool for objects of size obj_sz bytes.
 * The initial memory allocation is obj_sz * obj_num bytes, later incrementing
 * by the same amount whenever needed. obj_sz must be a multiple of 8 bytes.
 * The memory allocation will be aligned to a page boundary. This will require
 * obj_sz * obj_num to be an integer multiple of the page size. We will quietly
 * adjust obj_num upwards to make this happen. Hence, obj_num should be seen as
 * indicating a minimum initial (and later increment) number, not an absolute.
 */
extern struct cmi_mempool *cmi_mempool_create(uint64_t obj_num, size_t obj_sz);

/*
 * cmi_mempool_expand : Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num. The allocated memory is aligned to
 * the system memory page size.
 */
extern void cmi_mempool_expand(struct cmi_mempool *mp);

/*
 * cmi_mempool_destroy : Free all memory allocated to the memory pool.
 * All allocated objects from the pool will become invalid.
 */
extern void cmi_mempool_destroy(struct cmi_mempool *mp);

/*
 * cmi_mempool_get : Pop an object off the pool stack, allocating more objects
 * if necessary.
 */
static inline void *cmi_mempool_get(struct cmi_mempool *mp)
{
    cmb_assert_release(mp != NULL);
    if (mp->next_obj == NULL) {
        /* Pool empty, refill it */
        cmi_mempool_expand(mp);
    }

    void *op = mp->next_obj;
    cmb_assert_debug(op != NULL);
    mp->next_obj = *(void **)op;

    return op;
}

/*
 * cmi_mempool_put : Push an object back on the pool stack.
 */
static inline void cmi_mempool_put(struct cmi_mempool *mp, void *op)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(op != NULL);

    *(void **)op = mp->next_obj;
    mp->next_obj = op;
}

#endif //CIMBA_CMI_MEMPOOL_H