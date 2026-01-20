/*
 * cmi_mempool.h -  Reusable memory pool for generic small objects, avoiding
 * time-consuming calls to `malloc()` and `free()`
 *
 * It allocates memory in chunks with space for some number of objects, adding
 * more as needed. It maintains a list of available objects and a list of
 * allocated chunks to enable cleanup.
 *
 * Several memory pools can co-exist. Each memory pool maintains objects of a
 * certain size, such as 32 or 64 bytes. The object size must be a multiple of
 * 8 bytes. Allocates new memory in chunks of an integral muliple of the system
 * memory page size, which may not be an exact multiple of the object size.
 * Hence, the `obj_num`is considered mild guidance on the minimum number of
 * objects to allocate in each chunk of additional pool capacity, not an exact
 * figure.
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

#include "cmi_memutils.h"

/* Additional cookie value for the predefined memory pools */
#define CMI_THREAD_STATIC 0x057A71C0057A71C0u

/*
 * A memory pool for reusable objects of a particular size.
 */
struct cmi_mempool {
    uint64_t cookie;
    size_t obj_sz;
    size_t incr_num;
    size_t incr_sz;
    uint64_t chunk_list_len;
    uint64_t chunk_list_cnt;
    void **chunk_list;
    void *next_obj;
};

/*
 * Allocate memory for a cmi_mempool struct, not yet for the objects to
 * be contained in the pool.
 */
extern struct cmi_mempool *cmi_mempool_create(void);

/*
 * Initialize a memory pool for reusable objects of size `obj_sz` bytes.
 *
 * The initial memory allocation is obj_sz * obj_num bytes, later incrementing
 * by the same amount whenever needed. obj_sz must be a multiple of 8 bytes.
 * The memory allocation will be aligned to a page boundary. This will require
 * obj_sz * obj_num to be an integer multiple of the page size. We will
 * quietly adjust obj_num upwards to make this happen. Hence, obj_num should
 * be seen as indicating a minimum initial (and later increment) number, not an
 * absolute.
 */
extern void cmi_mempool_initialize(struct cmi_mempool *mp,
                                   size_t obj_sz,
                                   uint64_t obj_num);

/*
 * Free all memory allocated to the memory pool except the `cmi_mempool`
 * object itself. All allocated objects from the pool will become invalid.
 */
extern void cmi_mempool_terminate(struct cmi_mempool *mp);

/*
 * Free all memory allocated to the memory pool and the cmi_mempool object
 * itself. All allocated objects from the pool will become invalid.
 */
extern void cmi_mempool_destroy(struct cmi_mempool *mp);

/*
 * Increase the memory pool size by a chunk, the same amount as originally
 * allocated, obj_sz * obj_num. The allocated memory is aligned to the system
 * memory page size.
 */
extern void cmi_mempool_expand(struct cmi_mempool *mp);

/*
 * Pop an object off the pool stack, allocating more objects if necessary.
 */
static inline void *cmi_mempool_alloc(struct cmi_mempool *mp)
{
    cmb_assert_release(mp != NULL);
    /* Allow for the first call to the predefined memory pools to be initialized */
    cmb_assert_debug((mp->cookie == CMI_INITIALIZED)
                       || (mp->cookie == CMI_THREAD_STATIC));

    if (mp->next_obj == NULL) {
        /* Pool empty, refill it, initialize first if needed */
        cmi_mempool_expand(mp);
    }

    void *op = mp->next_obj;
    cmb_assert_debug(op != NULL);
    mp->next_obj = *(void **)op;

    return op;
}

/*
 * Push an object back on the pool stack for later reuse.
 */
static inline void cmi_mempool_free(struct cmi_mempool *mp, void *op)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(mp->cookie == CMI_INITIALIZED);
    cmb_assert_release(op != NULL);

    *(void **)op = mp->next_obj;
    mp->next_obj = op;
}

/*
 * Deallocate any allocated memory in the thread local pools.
 * Call when exiting a pthread.
 */
extern void cmi_mempool_cleanup(void *arg);

#endif /* CIMBA_CMI_MEMPOOL_H */