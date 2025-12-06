/*
 * Test script for memory pool.
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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cmi_mempool.h"

#include "test.h"

int main(void)
{
    cmi_test_print_line("=");
    printf("Testing built-in memory pools\n");

    printf("cmi_mempool_get(&cmi_mempool_32b): ... ");
    void *vp = cmi_mempool_get(&cmi_mempool_32b);
    printf("got %p\n", vp);

    printf("cmi_mempool_put(&cmi_mempool_32b): ... ");
    cmi_mempool_put(&cmi_mempool_32b, vp);
    printf("done\n");

    printf("cmi_mempool_get(&cmi_mempool_64b): ... ");
    vp = cmi_mempool_get(&cmi_mempool_64b);
    printf("got %p\n", vp);

    printf("cmi_mempool_put(&cmi_mempool_64b): ... ");
    cmi_mempool_put(&cmi_mempool_64b, vp);
    printf("done\n");

    cmi_test_print_line("-");

    printf("Testing created memory pools\n");
    size_t obj_sz = 32u;
    uint64_t obj_num = 16u;
    printf("cmi_mempool_create: %" PRIu64 " objects size %" PRIu64 "\n",
           obj_num, obj_sz);
    struct cmi_mempool *mp = cmi_mempool_create();
    cmi_mempool_initialize(mp, obj_sz, obj_num);

    printf("cmi_mempool_get: ... ");
    vp = cmi_mempool_get(mp);
    printf("got %p\n", vp);

    printf("cmi_mempool_put: ... ");
    cmi_mempool_put(mp, vp);
    printf("done\n");

    printf("cmi_mempool_destroy: Deleting the pool ... ");
    cmi_mempool_destroy(mp);
    printf("done\n");

    obj_sz = 64u;
    obj_num = 57u;
    printf("cmi_mempool_create: %" PRIu64 " objects size %" PRIu64 "\n",
           obj_num, obj_sz);
    mp = cmi_mempool_create();
    cmi_mempool_initialize(mp, obj_sz, obj_num);

    printf("cmi_mempool_get: pulling out 101 of them, forcing a pool expand ... ");
    void *vp_first = cmi_mempool_get(mp);
    for (unsigned ui = 0; ui < 100; ui++) {
        vp = cmi_mempool_get(mp);
    }
    printf("done\n");
    printf("First %p\n", vp_first);
    printf("Last %p\n", vp);

    printf("cmi_mempool_put: returning the first and last ... ");
    cmi_mempool_put(mp, vp_first);
    cmi_mempool_put(mp, vp);
    printf("done\n");

    printf("cmi_mempool_destroy: Deleting the pool ... ");
    cmi_mempool_terminate(mp);
    cmi_mempool_destroy(mp);
    printf("done\n");

    cmi_test_print_line("=");
}