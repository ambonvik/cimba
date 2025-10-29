/*
 * cmb_mempool.h - internal memory pool for generic small objects.
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
#ifndef CIMBA_CMB_MEMPOOL_H
#define CIMBA_CMB_MEMPOOL_H

#include <stdint.h>

#include "cmb_assert.h"

/*
 * struct cmb_mempool : The memory pool for a particular size object.
 */
struct cmb_mempool {
    size_t obj_sz;
    size_t incr_num;
    size_t incr_sz;
    uint64_t chunk_list_len;
    uint64_t chunk_list_cnt;
    void **chunk_list;
    void *next_obj;
};

/*
 * cmb_mempool_create : Allocate a (zero-initialzied) cmb_mempool object.
 */
extern struct cmb_mempool *cmb_mempool_create(void);

/*
 * cmb_mempool_initialize : Set up memory pool for objects of size obj_sz bytes.
 *
 * The initial memory allocation is obj_sz * obj_num bytes, later incrementing
 * by the same amount whenever needed. obj_sz must be a multiple of 8 bytes.
 * The memory allocation will be aligned to a page boundary. This will require
 * obj_sz * obj_num to be an integer multiple of the page size. We will quietly
 * adjust obj_num upwards to make this happen. Hence, obj_num should be seen as
 * indicating a minimum initial (and later increment) number, not an absolute.
 */
extern void cmb_mempool_initialize(struct cmb_mempool *mp,
                                   uint64_t obj_num,
                                   size_t obj_sz);

/*
 * cmb_mempool_terminate : Free all memory allocated to the memory pool except
 * the cmb_mempool object itself. All allocated objects from the pool will
 * become invalid.
 */
extern void cmb_mempool_terminate(struct cmb_mempool *mp);

/*
 * cmb_mempool_destroy : Free all memory allocated to the memory pool and the
 * cmb_mempool object itself. All allocated objects from the pool will become
 * invalid.
 */
extern void cmb_mempool_destroy(struct cmb_mempool *mp);

/*
 * cmb_mempool_expand : Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num. The allocated memory is aligned to
 * the system memory page size.
 */
extern void cmb_mempool_expand(struct cmb_mempool *mp);

/*
 * cmb_mempool_get : Pop an object off the pool stack, allocating more objects
 * if necessary.
 */
static inline void *cmb_mempool_get(struct cmb_mempool *mp)
{
    cmb_assert_release(mp != NULL);
    if (mp->next_obj == NULL) {
        /* Pool empty, refill it */
        cmb_mempool_expand(mp);
    }

    void *op = mp->next_obj;
    cmb_assert_debug(op != NULL);
    mp->next_obj = *(void **)op;

    return op;
}

/*
 * cmb_mempool_put : Push an object back on the pool stack.
 */
static inline void cmb_mempool_put(struct cmb_mempool *mp, void *op)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(op != NULL);

    *(void **)op = mp->next_obj;
    mp->next_obj = op;
}

#endif //CIMBA_CMB_MEMPOOL_H