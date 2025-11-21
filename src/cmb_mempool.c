/*
 * cmb_mempool.c - internal memory pool for generic small objects.
 *
 * The memory pool is handled as a stack of objects, popping from and pushing
 * to the front end of the pool. Memory is allocated in larger contiguous
 * areas, reinterpreted as a singly linked list by using the first 8 bytes to
 * store the address of the next object. In addition, a separate array keeps
 * track of the allocated areas to be able to free them all at the end.
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

#include <stdio.h>

#include "cmb_mempool.h"

#include "cmi_memutils.h"

/*
 * cmb_mempool_create : Allocate memory for a (zeroed) memory pool object.
 */
struct cmb_mempool *cmb_mempool_create(void)
{
    struct cmb_mempool *mp = cmi_malloc(sizeof(*mp));
    cmi_memset(mp, 0u, sizeof(*mp));
    mp->cookie = CMI_UNINITIALIZED;

    return mp;
}

#define CHUNK_LIST_SIZE 64u

/*
 * cmb_mempool_initialize : Set up a memory pool for objects of size obj_sz bytes.
 * chunk_list keeps track of the allocated memory to be able to free it later.
 * The chunk_list resizes as needed, starting from CHUNK_LIST_SIZE defined above.
 * next_obj points to the first available object in the pool, NULL if empty.
 *
 * We will be allocating memory aligned to page size. The allocator will reguire
 * the total amount of memory to be a multiple of the page size. We ccalculate
 * and store both the chunk size in bytes (smallest multiple of page sizes that
 * provide space for at least the requested number of objects) and the maximum
 * number of objects that fits in that memory size (allowing for leftovers if
 * the object size is not a divisor of page size).
 *
 * We'll allocate actual object memory on first call to cmi_mempool_get, hence
 * leaving the object list empty for now.
 */
void cmb_mempool_initialize(struct cmb_mempool *mp,
                            const uint64_t obj_num,
                            const size_t obj_sz)
{
    cmb_assert_release((obj_sz % 8u) == 0);
    cmb_assert_release(obj_num > 0u);

    mp->cookie = CMI_INITIALIZED;
    mp->obj_sz = obj_sz;

    /* Calculate the size of memory to allocate in each chunk */
    const size_t page_sz = cmi_get_pagesize();
    const size_t total_sz = obj_num * obj_sz;
    mp->incr_sz = ((total_sz + page_sz - 1u) / page_sz) * page_sz;
    cmb_assert_debug((mp->incr_sz % page_sz) == 0u);
    cmb_assert_debug(mp->incr_sz >= total_sz);

    /* Calculate the number of objects that will fit in each chunk */
    mp->incr_num = mp->incr_sz / mp->obj_sz;
    cmb_assert_debug(mp->incr_num >= obj_num);
    cmb_assert_debug((mp->incr_num * mp->obj_sz) <= mp->incr_sz);

    /* Allocate the initial array of pointers to the memory chunks */
    mp->chunk_list_len = CHUNK_LIST_SIZE;
    mp->chunk_list_cnt = 0u;
    mp->chunk_list = cmi_malloc(mp->chunk_list_len * sizeof(void *));

    /* Leave the actual object list empty */
    mp->next_obj = NULL;
}

/*
 * cmb_mempool_terminate : Free all memory allocated to the memory pool except
 * the cmb_mempool object itself. All allocated objects from the pool will
 * become invalid.
 */
void cmb_mempool_terminate(struct cmb_mempool *mp)
{
    cmb_assert_release(mp != NULL);

    if (mp->chunk_list != NULL) {
        cmb_assert_debug(mp->chunk_list_cnt > 0u);
        for (uint64_t ui = 0u; ui < mp->chunk_list_cnt; ui++) {
            cmi_aligned_free(mp->chunk_list[ui]);
        }

        cmi_free(mp->chunk_list);
        mp->chunk_list = NULL;
        mp->chunk_list_cnt = 0u;
        mp->next_obj = NULL;
    }
}

/*
 * cmb_mempool_destroy : Free all memory that was allocated to the pool,
 * including the mempool object itself. All application pointers to objects
 * previously allocated from this pool will become invalid.
 */
void cmb_mempool_destroy(struct cmb_mempool *mp)
{
    cmb_assert_release(mp != NULL);

    cmb_mempool_terminate(mp);
    cmi_free(mp);
}

/*
 * cmb_mempool_expand : Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num. The allocated memory is aligned to
 * the system memory page size.
 */
void cmb_mempool_expand(struct cmb_mempool *mp)
{
    cmb_assert_release(mp->next_obj == NULL);
    cmb_assert_release(mp->cookie == CMI_INITIALIZED);

    /* Expand the area list if necessary */
    if (++mp->chunk_list_cnt == mp->chunk_list_len) {
        mp->chunk_list_len += CHUNK_LIST_SIZE;
        cmi_realloc(mp->chunk_list, mp->chunk_list_len);
    }

    /* Allocate another contiguous array of objects, aligned to page size */
    const size_t pagesz = cmi_get_pagesize();
    void *ap = cmi_aligned_alloc(pagesz, mp->incr_sz);
    mp->chunk_list[mp->chunk_list_cnt - 1u] = ap;

    /* Initialize the objects */
    mp->next_obj = ap;
    void **vp = ap;
    const unsigned uincr = mp->obj_sz / 8u;
    for (unsigned ui = 0u; ui < mp->incr_num - 1u; ++ui) {
        *vp = vp + uincr;
        vp = *vp;
    }

    /* Set the next pointer in the last object to NULL, end of list */
    *vp = NULL;

    cmb_assert_debug(mp->next_obj != NULL);
}
