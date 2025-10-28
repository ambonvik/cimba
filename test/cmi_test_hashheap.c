/*
* Test script for the combined binary heap / hash map structure.
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

#include "cmb_random.h"

#include "cmi_hashheap.h"
#include "cmi_test.h"

/*
 * Test if heap_tag *a should go before *b. If so, return true.
 * Default heap compare function, corresponds to event queue order, where
 * dkey = reactivation time, ikey = priority, ukey = not used, use handle FIFO.
 */
static bool heap_order_check(const struct cmi_heap_tag *a,
                             const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->dkey < b->dkey) {
        ret = true;
    }
    else if (a->dkey == b->dkey) {
        if (a->ikey > b->ikey) {
            ret = true;
        }
        else if (a->ikey == b->ikey) {
            if (a->handle < b->handle) {
                ret = true;
            }
        }
    }

    return ret;
}

int main(void)
{
    cmb_random_initialize(cmb_random_get_hwseed());

    cmi_test_print_line("-");
    printf("Testing event queue\n");
    printf("Creating hash heap: cmi_hashheap_create ...\n");
    struct cmi_hashheap *hhp = cmi_hashheap_create();
    printf("Initializing hash heap: cmi_hashheap_initialize ...\n");
    cmi_hashheap_initialize(hhp, 3u, heap_order_check);
    printf("Destroying hash heap: cmi_hashheap_destroy ...\n");
    cmi_hashheap_destroy(hhp);

    printf("\nCreating another hash heap: cmi_hashheap_create ...\n");
    hhp = cmi_hashheap_create();
    printf("Initializing hash heap: cmi_hashheap_initialize ...\n");
    cmi_hashheap_initialize(hhp, 3u, heap_order_check);
    printf("Adding an item: cmi_hashheap_enqueue ... ");
    uint64_t handle = cmi_hashheap_enqueue(hhp, NULL, NULL, NULL, NULL, 1.0, 1);
    printf("returned handle %llu\n", handle);
    printf("Peekaboo: cmi_hashheap_peek ... \n");
    (void)cmi_hashheap_peek_item(hhp);
    printf("Pulling out an item: cmi_hashheap_dequeue ... \n");
    (void)cmi_hashheap_dequeue(hhp);
    printf("Destroying hash heap: cmi_hashheap_destroy ...\n");
    cmi_hashheap_destroy(hhp);

    printf("\nCreating another hash heap: cmi_hashheap_create ...\n");
    hhp = cmi_hashheap_create();
    printf("Initializing hash heap: cmi_hashheap_initialize ...\n");
    cmi_hashheap_initialize(hhp, 3u, heap_order_check);
    printf("Adding 5 items: cmi_hashheap_enqueue ... \n");
    uint64_t itemcnt = 0u;
    for (unsigned ui = 0; ui < 5; ui++) {
        const double d = cmb_random();
        const int64_t i = cmb_random_dice(0, 1000);
        (void)cmi_hashheap_enqueue(hhp, (void *)(++itemcnt), NULL, NULL, NULL, d, i);
    }

    cmi_hashheap_print(hhp, stdout);
    void **item = NULL;
    while ((item = cmi_hashheap_dequeue(hhp)) != NULL) {
        printf("Dequeued item: %p\n", item[0]);
        cmi_hashheap_print(hhp, stdout);
   }

    printf("Adding 10 items, forcing a resizing ... \n");
    for (unsigned ui = 0; ui < 10; ui++) {
        const double d = cmb_random();
        const int64_t i = cmb_random_dice(0, 1000);
        (void)cmi_hashheap_enqueue(hhp, (void *)(++itemcnt), NULL, NULL, NULL, d, i);
    }

    printf("We now have %llu items\n", cmi_hashheap_count(hhp));
    cmi_hashheap_print(hhp, stdout);

    while ((item = cmi_hashheap_dequeue(hhp)) != NULL) {
        printf("Dequeued item: %p\n", item[0]);
        if (cmi_hashheap_count(hhp) > 0u) {
            void **nxtitem = cmi_hashheap_peek_item(hhp);
            cmb_assert_debug(nxtitem != NULL);
            const double d = cmi_hashheap_peek_dkey(hhp);
            printf("Coming next: %p %f\n", nxtitem[0], d);
        }
        else {
            printf("No more items\n");
        }
    }

    printf("Destroying hash heap: cmi_hashheap_destroy ...\n");
    cmi_hashheap_destroy(hhp);


    cmi_test_print_line("=");
    return 0;
}