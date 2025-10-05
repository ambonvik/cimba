/*
 * cmi_mempool.c - internal memory pool for generic small objects.
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
 */
struct cmi_mempool *cmi_mempool_create(const uint64_t obj_num, const size_t obj_sz)
{
    cmb_assert_release((obj_sz % 8u) == 0);
    cmb_assert_release(obj_num > 0u);

    struct cmi_mempool *mp = cmi_malloc(sizeof(*mp));
    mp->obj_sz = obj_sz;
    mp->incr_num = obj_num;
    mp->area_list_len = AREA_LIST_SIZE;
    mp->area_list_cnt = 0u;
    mp->area_list = cmi_malloc(mp->area_list_len * sizeof(void *));
    mp->next_obj = NULL;

    return mp;
}

/*
 * cmi_mempool_expand : Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num.
 */
void cmi_mempool_expand(struct cmi_mempool *mp)
{
    cmb_assert_release(mp->next_obj == NULL);

    /* Expand the area list if necessary */
    if (++mp->area_list_cnt == mp->area_list_len) {
        mp->area_list_len += AREA_LIST_SIZE;
        cmi_realloc(mp->area_list, mp->area_list_len);
    }

    /* Allocate another contiguous array of objects */
    void *ap = cmi_malloc(mp->obj_sz * mp->incr_num);
    mp->area_list[mp->area_list_cnt - 1u] = ap;

    /* Initialize the objects */
    mp->next_obj = ap;
    void **vp = ap;
    unsigned uincr = mp->obj_sz / 8u;
    /* ... */
}

