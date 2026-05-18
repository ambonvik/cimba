/*
* Test script for the combined binary heap / hash map structure.
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
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmb_random.h"

#include "cmi_hashheap.h"
#include "test.h"

/*
 * Test if heap_tag *a should go before *b. If so, return true.
 * Default heap compare function, corresponds to the event queue order, where
 * rank_d64 = reactivation time, rank_i64 = priority, ukey = not used, uses hash_key FIFO.
 */
static bool heap_order_check(const struct cmi_heap_tag *a,
                             const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->rank_d64 < b->rank_d64) {
        ret = true;
    }
    else if (a->rank_d64 == b->rank_d64) {
        if (a->rank_i64 > b->rank_i64) {
            ret = true;
        }
        else if (a->rank_i64 == b->rank_i64) {
            if (a->hash_key < b->hash_key) {
                ret = true;
            }
        }
    }

    return ret;
}

void test_hashheap(uint64_t seed)
{
    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("Testing empty hashheap\n");
    printf("Create a hash heap\n");
    struct cmi_hashheap *hhp = cmi_hashheap_create();
    cmb_assert_always(hhp != NULL);
    printf("Initialize hash heap\n");
    cmi_hashheap_initialize(hhp, 3u, heap_order_check);
    printf("Terminate hash heap\n");
    cmi_hashheap_terminate(hhp);
    printf("Destroy hash heap\n");
    cmi_hashheap_destroy(hhp);

    printf("\nCreate another hash heap\n");
    hhp = cmi_hashheap_create();
    cmb_assert_always(hhp != NULL);
    printf("Initialize hash heap\n");
    cmi_hashheap_initialize(hhp, 3u, heap_order_check);
    cmb_assert_always(cmi_hashheap_is_empty(hhp) == true);
    cmb_assert_always(cmi_hashheap_count(hhp) == 0u);

    printf("Add an item, ");
    uint64_t key = cmi_hashheap_enqueue(hhp, NULL, NULL, NULL, NULL, 0u, 1.0, 1);
    printf("returned hash_key %" PRIu64 "\n", key);
    cmb_assert_always(key != 0u);
    cmb_assert_always(cmi_hashheap_is_empty(hhp) == false);
    cmb_assert_always(cmi_hashheap_count(hhp) == 1u);

    printf("Pull an item\n");
    void **item = cmi_hashheap_dequeue(hhp);
    cmb_assert_always(item != NULL);
    cmb_assert_always(item[0] == NULL);
    cmb_assert_always(cmi_hashheap_is_empty(hhp) == true);
    cmb_assert_always(cmi_hashheap_count(hhp) == 0u);

    printf("Adding 5 items\n");
    uint64_t itemcnt = 0u;
    for (unsigned ui = 0; ui < 5; ui++) {
        const double d = cmb_random();
        cmb_assert_always((d >= 0.0) && (d <= 1.0));
        const int64_t i = cmb_random_dice(0, 1000);
        cmb_assert_always((i >= 0) && (i <= 1000));
        void *val = (void *)(++itemcnt);
        uint64_t key_found = cmi_hashheap_pattern_find(hhp, val, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
        cmb_assert_always(key_found == 0u);
        key = cmi_hashheap_enqueue(hhp, val, NULL, NULL, NULL, 0u, d, i);
        cmb_assert_always(key != 0u);
        key_found = cmi_hashheap_pattern_find(hhp, val, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
        cmb_assert_always(key_found == key);
    }

    /* Have not done anything to turn it on yet */
    cmb_assert_always(hhp->map_active == false);
    cmi_hashheap_print(hhp, stdout, NULL);

    while ((item = cmi_hashheap_dequeue(hhp)) != NULL) {
        const double dcur = hhp->heap[0].rank_d64;
        printf("Dequeued item: 0x%" PRIXPTR "\n", (uintptr_t)item[0]);
        if (cmi_hashheap_count(hhp) > 0u) {
            void **nxtitem = cmi_hashheap_peek_item(hhp);
            cmb_assert_always(nxtitem != NULL);
            const double dnxt = cmi_hashheap_peek_drank(hhp);
            cmb_assert_always(dnxt >= dcur);
        }
    }

    cmb_assert_always(cmi_hashheap_count(hhp) == 0u);
    printf("Adding 10 items, forcing a resizing ... \n");
    for (unsigned ui = 0; ui < 10; ui++) {
        const double d = cmb_random();
        const int64_t i = cmb_random_dice(0, 1000);
        void *val = (void *)(++itemcnt);
        uint64_t key_found = cmi_hashheap_pattern_find(hhp, val, NULL, NULL, NULL);
        cmb_assert_always(key_found == 0u);
        key = cmi_hashheap_enqueue(hhp, val, NULL, NULL, NULL, 0u, d, i);
        cmb_assert_always(key != 0u);
        cmb_assert_always(cmi_hashheap_count(hhp) == ui + 1u);
        /* This will switch on the hash map */
        cmb_assert_always(cmi_hashheap_is_enqueued(hhp, key) == true);
        item = cmi_hashheap_item(hhp, key);
        cmb_assert_always(item != NULL);
        cmb_assert_always(item[0] == val);
        cmb_assert_always(cmi_hashheap_drank(hhp, key) == d);
        cmb_assert_always(cmi_hashheap_irank(hhp, key) == i);
        key_found = cmi_hashheap_pattern_find(hhp, val, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
        cmb_assert_always(key_found == key);
    }

    cmb_assert_always(hhp->map_active == true);
    cmb_assert_always(cmi_hashheap_count(hhp) == 10u);
    cmi_hashheap_print(hhp, stdout, NULL);

    /* We started the index count from 1, have used 1 + 5, should have hash keys 7 through 16 in heap */
    for (unsigned ui = 7; ui <= 16; ui ++) {
        cmb_assert_always(cmi_hashheap_is_enqueued(hhp, ui) == true);
        item = cmi_hashheap_item(hhp, ui);
        cmb_assert_always(item != NULL);
    }

    printf("Removing hash keys 8u and 10u\n");
    cmb_assert_always(cmi_hashheap_remove(hhp, 8u) == true);
    cmb_assert_always(cmi_hashheap_is_enqueued(hhp, 8u) == false);
    cmb_assert_always(cmi_hashheap_cancel(hhp, 10u) == true);
    cmb_assert_always(cmi_hashheap_is_enqueued(hhp, 10u) == false);

    printf("Reprioritizing hash key 9u\n");
    cmi_hashheap_reprioritize(hhp, 9u, 100.0, 10);
    cmb_assert_always(cmi_hashheap_is_enqueued(hhp, 9u) == true);
    cmb_assert_always(cmi_hashheap_drank(hhp, 9u) == 100.0);
    cmb_assert_always(cmi_hashheap_irank(hhp, 9u) == 10);

    cmi_hashheap_print(hhp, stdout, NULL);

    void *val = (void *)0xc;
    const uint64_t uidx = cmi_hashheap_pattern_find(hhp, (void *)0xc, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
    printf("Cancelling value 0x%" PRIXPTR " (hash key %" PRIu64 ")\n", (uintptr_t)val, uidx);
    uint64_t n_found = cmi_hashheap_pattern_count(hhp, (void *)0xc, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
    cmb_assert_always(n_found == 1u);
    uint64_t n_cnsl = cmi_hashheap_pattern_cancel(hhp, (void *)0xc, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
    cmb_assert_always(n_cnsl == 1u);
    n_found = cmi_hashheap_pattern_count(hhp, (void *)0xc, CMI_ANY_ITEM, CMI_ANY_ITEM, CMI_ANY_ITEM);
    cmb_assert_always(n_found == 0u);

    cmi_hashheap_print(hhp, stdout, NULL);

    while ((item = cmi_hashheap_dequeue(hhp)) != NULL) {
        printf("Dequeued item: 0x%" PRIXPTR "\n", (uintptr_t)item[0]);
        const double dcur = hhp->heap[0].rank_d64;
        if (cmi_hashheap_count(hhp) > 0u) {
            void **nxtitem = cmi_hashheap_peek_item(hhp);
            cmb_assert_always(nxtitem != NULL);
            const double dnxt = cmi_hashheap_peek_drank(hhp);
            cmb_assert_always(dnxt >= dcur);
            printf("Next item: 0x%" PRIXPTR "\n", (uintptr_t)nxtitem[0]);
        }
        else {
            printf("No more items\n");
        }
    }

    printf("Cleaning up\n");
    cmi_hashheap_terminate(hhp);
    cmi_hashheap_destroy(hhp);
    cmb_random_terminate();
    cmi_test_print_line("*");
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();

    int opt;
    while ((opt = getopt(argc, argv, "s:t")) != -1) {
        switch (opt) {
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 't':
                timing_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    const clock_t start_time = clock();

    test_hashheap(seed);

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}