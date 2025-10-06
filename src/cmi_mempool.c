/*
 * cmi_mempool.c - internal memory pool for generic small objects.
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

#include "cmi_mempool.h"
#include "cmi_memutils.h"

#define AREA_LIST_SIZE 64u

/*
 * struct cmi_mempool : The memory pool for a particular size object.
 */
struct cmi_mempool {
    size_t obj_sz;
    size_t incr_num;
    uint64_t area_list_len;
    uint64_t area_list_cnt;
    void **area_list;
    void *next_obj;
};

/*
 * cmi_mempool_create : Set up a memory pool for objects of size obj_sz bytes.
 * area_list keeps track of the allocated memory to be able to free it later.
 * The area_list resizes as needed, starting from AREA_LIST_SIZE defined above.
 * next_obj points to the first available object in the pool, NULL if empty.
 *
 * We will be allocating memory aligned to page size. The allocator will reguire
 * the total amount of memory to be a multiple of the page size. We round the
 * number of objects upwards to make it an integer multiple of the page size.
 */
struct cmi_mempool *cmi_mempool_create(const uint64_t obj_num, const size_t obj_sz)
{
    cmb_assert_release((obj_sz % 8u) == 0);
    cmb_assert_release(obj_num > 0u);

    /* Ensure that obj_num * obj_sz become a multiple of page size */
    const size_t pagesz = cmi_get_pagesize();
    const uint64_t obj_num_adj = ((obj_num * obj_sz + pagesz - 1u) / pagesz)
                                * (pagesz / obj_sz);

    struct cmi_mempool *mp = cmi_malloc(sizeof(*mp));
    mp->obj_sz = obj_sz;
    mp->incr_num = obj_num_adj;
    cmb_assert_debug(((mp->obj_sz * mp->incr_num) % pagesz) == 0u);

    mp->area_list_len = AREA_LIST_SIZE;
    mp->area_list_cnt = 0u;
    mp->area_list = cmi_malloc(mp->area_list_len * sizeof(void *));

    /* We'll allocate actual object memory on first call to cmi_mempool_get */
    mp->next_obj = NULL;

    return mp;
}

/*
 * cmi_mempool_destroy : Free all memory that was allocated to the pool,
 * including the mempool object itself. All application pointers to objects
 * previously allocated from this pool will become invalid.
 */
void cmi_mempool_destroy(struct cmi_mempool *mp)
{
    cmb_assert_release(mp != NULL);

    /* Free all allocated memory areas, remembering that they were aligned */
    for (uint64_t ui = 0u; ui < mp->area_list_cnt; ui++) {
        cmi_aligned_free(mp->area_list[ui]);
    }

    /* Free the area list, and then the pool itself */
    cmi_free(mp->area_list);
    cmi_free(mp);
}

/*
 * mempool_expand : Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num. The allocated memory is aligned to
 * the system memory page size.
 */
static void mempool_expand(struct cmi_mempool *mp)
{
    cmb_assert_release(mp->next_obj == NULL);

    /* Expand the area list if necessary */
    if (++mp->area_list_cnt == mp->area_list_len) {
        mp->area_list_len += AREA_LIST_SIZE;
        cmi_realloc(mp->area_list, mp->area_list_len);
    }

    /* Allocate another contiguous array of objects, aligned to page size */
    const size_t pagesz = cmi_get_pagesize();
    void *ap = cmi_aligned_alloc(pagesz, mp->obj_sz * mp->incr_num);
    mp->area_list[mp->area_list_cnt - 1u] = ap;

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

/*
 * cmi_mempool_get : Pop an object off the pool stack, allocating more objects
 * if necessary.
 */
void *cmi_mempool_get(struct cmi_mempool *mp)
{
    cmb_assert_release(mp != NULL);
    if (mp->next_obj == NULL) {
        /* Pool empty, refill it */
        mempool_expand(mp);
    }

    void *op = mp->next_obj;
    cmb_assert_debug(op != NULL);
    mp->next_obj = *(void **)op;

    return op;
}

/*
 * cmi_mempool_put : Push an object back on the pool stack.
 */
void cmi_mempool_put(struct cmi_mempool *mp, void *op)
{
    cmb_assert_release(mp != NULL);
    cmb_assert_release(op != NULL);

    *(void **)op = mp->next_obj;
    mp->next_obj = op;
}