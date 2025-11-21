/**
 * @file cmb_mempool.h
 * @brief Reusable memory pool for generic small objects, avoiding time-
 * consuming calls to `malloc()` and `free()`
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
 */

/*
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

#include "cmi_memutils.h"

/**
 * @brief A memory pool for reuseable objects of a particular size.
 */
struct cmb_mempool {
    uint64_t cookie;        /**< Initialization trap */
    size_t obj_sz;          /**< Size in bytes of the objects in this pool */
    size_t incr_num;        /**< The number of objects to add in each new chunk */
    size_t incr_sz;         /**< The size increase in bytes */
    uint64_t chunk_list_len; /**< The length of the list of allocated chunks */
    uint64_t chunk_list_cnt; /**< The number of chunks in use now */
    void **chunk_list;       /**< The list of allocated memory chunks */
    void *next_obj;          /**< The head of the available objects list */
};

/**
 * brief Allocate memory for a `cmb_mempool` struct, not yet for the objects to
 *       be contained in the pool.
 */
extern struct cmb_mempool *cmb_mempool_create(void);

/**
 * brief Initialize a memory pool for reuseable objects of size `obj_sz` bytes.
 *
 * The initial memory allocation is `obj_sz * obj_num` bytes, later incrementing
 * by the same amount whenever needed. `obj_sz must be a multiple of 8 bytes.
 * The memory allocation will be aligned to a page boundary. This will require
 * `obj_sz * obj_num` to be an integer multiple of the page size. We will
 * quietly adjust `obj_num` upwards to make this happen. Hence, `obj_num` should
 * be seen as indicating a minimum initial (and later increment) number, not an
 * absolute.
 *
 * @param mp Pointer to the memory pool.
 * @param obj_num The number of objects to allocate room for in each new chunk.
 * @param obj_sz The size of each object in bytes.
 */
extern void cmb_mempool_initialize(struct cmb_mempool *mp,
                                   uint64_t obj_num,
                                   size_t obj_sz);

/**
 * brief  Free all memory allocated to the memory pool except the `cmb_mempool`
 * object itself. All allocated objects from the pool will become invalid.
 *
 * @param mp Pointer to the memory pool.
 */
extern void cmb_mempool_terminate(struct cmb_mempool *mp);

/**
 * @brief  Free all memory allocated to the memory pool and the
 * `cmb_mempool` object itself. All allocated objects from the pool will become
 * invalid.
*
 * @param mp Pointer to the memory pool.
 */
extern void cmb_mempool_destroy(struct cmb_mempool *mp);

/**
 * @brief  Increase the memory pool size by a chunk, the same amount as
 *         originally allocated, `obj_sz * obj_num`. The allocated memory is
 *         aligned to the system memory page size.
 *
 * @param mp Pointer to the memory pool.
 */
extern void cmb_mempool_expand(struct cmb_mempool *mp);

/**
 * @brief Pop an object off the pool stack, allocating more objects
 *        if necessary.
 *
 * @param mp Pointer to the memory pool.
 * @return  Pointer to an object from the pool for use, similar to obtaining it
 *          from `malloc(sizeof(object))`
 */
static inline void *cmb_mempool_get(struct cmb_mempool *mp)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(mp->cookie == CMI_INITIALIZED);

    if (mp->next_obj == NULL) {
        /* Pool empty, refill it */
        cmb_mempool_expand(mp);
    }

    void *op = mp->next_obj;
    cmb_assert_debug(op != NULL);
    mp->next_obj = *(void **)op;

    return op;
}

/**
 * @brief Push an object back on the pool stack for later reuse.
 *
 * @param mp Pointer to the memory pool.
 * @param op Pointer to an object for recycling.
 */
static inline void cmb_mempool_put(struct cmb_mempool *mp, void *op)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(mp->cookie == CMI_INITIALIZED);
    cmb_assert_release(op != NULL);

    *(void **)op = mp->next_obj;
    mp->next_obj = op;
}

#endif /* CIMBA_CMB_MEMPOOL_H */