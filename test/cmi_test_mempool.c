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
#include <stdio.h>

#include "cmi_mempool.h"
#include "cmi_test.h"

int main(void)
{
    cmi_test_print_line("-");
    printf("Testing memory pool\n");
    size_t obj_sz = 32u;
    uint64_t obj_num = 16u;
    printf("cmi_mempool_create: %llu objects size %llu\n", obj_num, obj_sz);
    struct cmi_mempool *mp = cmi_mempool_create(obj_num, obj_sz);

    printf("cmi_mempool_get: ... ");
    void *vp = cmi_mempool_get(mp);
    printf("got %p\n", vp);

    printf("cmi_mempool_put: ... ");
    cmi_mempool_put(mp, vp);
    printf("done\n");

    printf("cmi_mempool_destroy: Deleting the pool ... ");
    cmi_mempool_destroy(mp);
    printf("done\n");

    cmi_test_print_line("-");
    obj_sz = 64u;
    obj_num = 64u;
    printf("cmi_mempool_create: %llu objects size %llu\n", obj_num, obj_sz);
    mp = cmi_mempool_create(obj_num, obj_sz);

    printf("cmi_mempool_get: pulling out 101 of them ... ");
    void *vp_first = cmi_mempool_get(mp);
    for (unsigned ui = 0; ui < 100; ui++) {
        vp = cmi_mempool_get(mp);
    }
    printf("done\n");
    printf("First %p\n", vp_first);
    printf("Last %p\n", vp);

    printf("cmi_mempool_put: returning the first and last ... ");
    cmi_mempool_put(mp, vp_first);
    printf("done\n");

    printf("cmi_mempool_destroy: Deleting the pool ... ");
    cmi_mempool_destroy(mp);
    printf("done\n");


    cmi_test_print_line("=");
}