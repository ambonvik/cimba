/*
 * cmi_mempool.c - internal memory pool for generic small objects.
 *
 * The memory pool is handled as a stack of objects, popping from and pushing
 * to the front end of the pool. Memory is allocated in larger contiguous
 * areas, reinterpreted as a singly linked list by using the first 8 bytes to
 * store the address of the next object. In addition, a separate array keeps
 * track of the allocated areas to be able to free them all at the end.
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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

#include "cmi_mempool.h"
#include "cmi_slist.h"

/* Initial size of the memory chunk list as such */
#define CHUNK_LIST_SIZE 64u

/* List entry for keeping track of all thread local pools */
struct static_pools_tag {
    struct cmi_mempool *pool;
    struct cmi_slist_head head;
};

/* The list of all thread local pools */
CMB_THREAD_LOCAL struct cmi_slist_head static_pools = { NULL };

/*
 * cmi_mempool_create - Allocate memory for a (zeroed) memory pool object.
 */
struct cmi_mempool *cmi_mempool_create(void)
{
    struct cmi_mempool *mp = cmi_malloc(sizeof(*mp));
    cmi_memset(mp, 0u, sizeof(*mp));
    mp->cookie = CMI_UNINITIALIZED;

    return mp;
}

/*
 * cmi_mempool_initialize - Set up a memory pool for objects of size obj_sz bytes.
 * chunk_list keeps track of the allocated memory to be able to free it later.
 * The chunk_list resizes as needed, starting from CHUNK_LIST_SIZE defined above.
 * next_obj points to the first available object in the pool, NULL if empty.
 *
 * We allocate memory aligned to page size. The allocator requires the total
 * amount of memory to be a multiple of the page size. We calculate and store
 * both the chunk size in bytes (the smallest multiple of page sizes that
 * provides space for at least the requested number of objects) and the maximum
 * number of objects that fits in that memory size (allowing for leftovers if
 * the object size is not a divisor of page size).
 *
 * We'll allocate actual object memory on the first call to cmi_mempool_alloc,
 * hence leaving the object list empty for now.
 */
void cmi_mempool_initialize(struct cmi_mempool *mp,
                            const size_t obj_sz,
                            const uint64_t obj_num)
{
    cmb_assert_release((obj_sz % 8u) == 0);
    cmb_assert_release(obj_num > 0u);

    mp->cookie = CMI_INITIALIZED;
    mp->obj_sz = obj_sz;

    /* Calculate the size of memory to allocate in each chunk */
    const size_t page_sz = cmi_pagesize();
    const size_t total_sz = obj_num * obj_sz;
    mp->incr_sz = ((total_sz + page_sz - 1u) / page_sz) * page_sz;
    cmb_assert_debug((mp->incr_sz % page_sz) == 0u);
    cmb_assert_debug(mp->incr_sz >= total_sz);

    /* Calculate the number of objects that will fit in each chunk */
    mp->incr_num = mp->incr_sz / mp->obj_sz;
    cmb_assert_debug(mp->incr_num >= obj_num);
    cmb_assert_debug((mp->incr_num * mp->obj_sz) <= mp->incr_sz);

    /* The initial growth parameters */
    mp->current_incr = 0u;
    mp->virgin_ptr = NULL;
    mp->virgin_rem = 0u;

    /* Allocate the initial array of pointers to the memory chunks */
    mp->chunk_list_len = CHUNK_LIST_SIZE;
    mp->chunk_list_cnt = 0u;
    mp->chunk_list = cmi_malloc(mp->chunk_list_len * sizeof(void *));

    /* Leave the actual object list empty */
    mp->next_obj = NULL;
}

/*
 * cmi_mempool_terminate - Free all memory allocated to the memory pool except
 * the cmi_mempool object itself. All allocated objects from the pool will
 * become invalid.
 */
void cmi_mempool_terminate(struct cmi_mempool *mp)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(mp->cookie == CMI_INITIALIZED);
    cmb_assert_release(mp->chunk_list != NULL);

    /* Free all allocated memory chunks */
    for (uint64_t i = 0u; i < mp->chunk_list_cnt; ++i) {
        cmi_aligned_free(mp->chunk_list[i]);
    }

    /* Free the list tracking the chunks */
    cmi_free(mp->chunk_list);

    /* Reset metadata to prevent double-teardown */
    mp->chunk_list = NULL;
    mp->chunk_list_cnt = 0;
    mp->chunk_list_len = 0;
    mp->next_obj = NULL;
    mp->virgin_ptr = NULL;
    mp->virgin_rem = 0;
    mp->cookie = CMI_UNINITIALIZED;
}

/*
 * cmi_mempool_destroy - Free all memory that was allocated to the pool,
 * including the mempool object itself. All application pointers to objects
 * previously allocated from this pool will become invalid.
 */
void cmi_mempool_destroy(struct cmi_mempool *mp)
{
    cmb_assert_release(mp != NULL);

    cmi_mempool_terminate(mp);
    cmi_free(mp);
}

/*
 * cmi_mempool_expand - Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num. The allocated memory is aligned to
 * the system memory page size.
 */
void cmi_mempool_expand(struct cmi_mempool *mp)
{
    cmb_assert_release(mp->next_obj == NULL);
    cmb_assert_release((mp->cookie == CMI_INITIALIZED)
                       || (mp->cookie == CMI_THREAD_STATIC));

    if (mp->cookie == CMI_THREAD_STATIC) {
        /* Initial lazy allocation of thread local static memory pool */
        struct static_pools_tag *stp = cmi_malloc(sizeof(*stp));
        stp->pool = mp;
        cmi_slist_push(&static_pools, &(stp->head));

        /* Initialize sets cookie to CMI_INITIALIZED and allocates chunk_list */
        cmi_mempool_initialize(mp, mp->obj_sz, mp->incr_num);
    }

    /* Doubling on each increment */
    if (mp->current_incr == 0u) {
        mp->current_incr = (uint32_t)mp->incr_num;
    } else {
        /* Double it, up to some reasonable limit */
        if (mp->current_incr < 1024u) {
            mp->current_incr *= 2u;
        }
    }

    /* Calculate total size for this new slab */
    const size_t total_sz = (size_t)mp->current_incr * mp->obj_sz;

    /* Expand the chunk list if necessary */
    if (mp->chunk_list_cnt == mp->chunk_list_len) {
        mp->chunk_list_len += CHUNK_LIST_SIZE;
        mp->chunk_list = cmi_realloc(mp->chunk_list, mp->chunk_list_len * sizeof(void *));
    }

    /* Allocate the new chunk */
    const size_t pagesz = cmi_pagesize();
    void *ap = cmi_aligned_alloc(pagesz, total_sz);
    mp->chunk_list[mp->chunk_list_cnt++] = ap;

    /* Set up the virgin memory tracker */
    mp->virgin_ptr = ap;
    mp->virgin_rem = mp->current_incr;
}

/*
 * cmi_mempool_cleanup - Function to deallocate any allocated memory in the
 * thread local pools. Call when exiting a pthread.
 */
void cmi_mempool_cleanup(void *arg)
{
    cmb_unused(arg);

    while (!cmi_slist_is_empty(&static_pools)) {
        struct cmi_slist_head *phead = cmi_slist_pop(&static_pools);
        struct static_pools_tag *stp = cmi_container_of(phead,
                                                        struct static_pools_tag,
                                                        head);
        cmi_mempool_terminate(stp->pool);
        cmi_free(stp);
    }
 }
