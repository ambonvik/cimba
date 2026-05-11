/*
 * Test script for memory pool.
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

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cmb_assert.h"

#include "cmi_mempool.h"
#include "cmi_memutils.h"

#include "test.h"

CMB_THREAD_LOCAL struct cmi_mempool mempool_32b = CMI_MEMPOOL_STATIC_INIT(32u, 128u);

void test_mempool(void)
{
    cmi_test_print_line("*");
    printf("Testing automatic memory pools\n");
    cmi_test_print_line("-");

    /* Statically initialized, no need to call cmi_mempool_initialize() */
    printf("cmi_mempool_alloc(&mempool_32b): ... ");
    void *vp = cmi_mempool_alloc(&mempool_32b);
    cmb_assert_always(vp != NULL);
    printf("done\n");

    printf("cmi_mempool_free(&mempool_32b): ... ");
    cmi_mempool_free(&mempool_32b, vp);
    printf("done\n");

    cmi_test_print_line("-");

    printf("Testing created memory pools\n");
    cmi_test_print_line("-");
    size_t obj_sz = 32u;
    uint64_t obj_num = 16u;
    const size_t page_sz = cmi_pagesize();
    printf("cmi_mempool_create: %" PRIu64 " objects size %" PRIu64 ", total %" PRIu64 " (page size %" PRIu64 ")\n",
           obj_num, obj_sz, obj_num * obj_sz, page_sz);
    struct cmi_mempool *mp = cmi_mempool_create();
    cmb_assert_always(mp != NULL);
    cmi_mempool_initialize(mp, obj_sz, obj_num);

    printf("cmi_mempool_alloc: ... ");
    vp = cmi_mempool_alloc(mp);
    cmb_assert_always(vp != NULL);
    printf("done\n");
    printf("cmi_mempool_free: ... ");
    cmi_mempool_free(mp, vp);
    printf("done\n");

    printf("cmi_mempool_destroy: Deleting the pool ... ");
    cmi_mempool_terminate(mp);
    cmi_mempool_destroy(mp);
    printf("done\n");

    obj_sz = 64u;
    obj_num = 57u;
    printf("cmi_mempool_create: %" PRIu64 " objects size %" PRIu64 ", total %" PRIu64 " (page size %" PRIu64 ")\n",
           obj_num, obj_sz, obj_num * obj_sz, page_sz);
    mp = cmi_mempool_create();
    cmb_assert_always(mp != NULL);
    cmi_mempool_initialize(mp, obj_sz, obj_num);

    printf("cmi_mempool_alloc: pulling out 101 of them, forcing a pool expand ... ");
    void *vp_first = cmi_mempool_alloc(mp);
    for (unsigned ui = 0; ui < 100; ui++) {
        vp = cmi_mempool_alloc(mp);
        cmb_assert_always(vp != NULL);
    }
    printf("done\n");

    printf("cmi_mempool_free: returning the first and last object to the pool ... ");
    cmi_mempool_free(mp, vp_first);
    cmi_mempool_free(mp, vp);
    printf("done\n");

    printf("cmi_mempool_destroy: Deleting the pool ... ");
    cmi_mempool_terminate(mp);
    cmi_mempool_destroy(mp);
    printf("done\n");

    cmi_test_print_line("*");
}

int main(void)
{
    test_mempool();

    return 0;
}