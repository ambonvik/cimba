/*
 * cmi_hashheap.h - The combined heap / hash map data structure used for
 * priority queues, both as the main event queue in the simulation and for the
 * priority queues associated with each guarded resource. It provides enqueue,
 * dequeue, peek, and cancel operations, plus item search functions.
 *
 * Each item in the priority queue is a tuple of (up to) four 64-bit payload
 * values. The item is uniquely identified by a 64-bit non-zero handle returned
 * when it is enqueued. An item can be cancelled or reprioritized with reference
 * to this handle. Handles are not reused during the lifetime of the hashheap.
 *
 * An item in the queue has (up to) two priority keys for determining the
 * sorting order; one double and one signed 64-bit integer. The semantics are
 * application defined, determined by a compare function that takes two pointers
 * to items a and b, and returns a bool indicating if a should precede b in the
 * priority order. The compare function can use any other properties as well.
 * For example, in the main event queue, the priority keys would be reactivation
 * time (double), priority (integer), and the sequential event handle. A pointer
 * to the appropriate compare function is stored in the hashheap control
 * structure.
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025-26.
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

#ifndef CIMBA_CMI_HASHHEAP_H
#define CIMBA_CMI_HASHHEAP_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "cmb_assert.h"

/*
 * struct cmi_heap_tag - The record to store an item in the priority queue.
 * These tags only exist as members of the event queue array, never alone.
 * The handle is a unique event identifier, the hash_index a reference to where
 * in the hash map it is located. The item is a 4-tuple of 64-bit values, here
 * represented as void*, but could be used for any other 64-bit value depending
 * on application needs (see cmb_event.c for an example).
 *
 * Note that the heap tag is 8 * 8 = 64 bytes large.
 */
struct cmi_heap_tag {
    uint64_t handle;
    uint64_t hash_index;
    void *item[4];
    double dkey;
    int64_t ikey;
};

/*
 * typedef cmi_heap_compare_func - Return true if a goes before b in the
 * priority queue order. Prototype only, actual compare function to be provided
 * by the application when initializing a hashheap.
 */
typedef bool (cmi_heap_compare_func)(const struct cmi_heap_tag *a,
                                     const struct cmi_heap_tag *b);

/*
 * struct cmi_hash_tag - Hash mapping from event handle to heap position.
 * Heap index value zero indicates a tombstone, event is no longer in the heap.
 *
 * Note that the hashtag is 2 * 8 = 16 bytes large.
 */
struct cmi_hash_tag {
    uint64_t handle;
    uint64_t heap_index;
};

/*
 * struct cmi_hashheap - The hashheap control structure with direct pointers to
 * the heap and hash map, and a function for ordering comparison between items
 * in the heap.
 *
 * The heap and hash map need to be sized as powers of two, the hash map with
 * twice as many entries as the heap. heap_exp_cur defines a heap size of 2^heap_exp_cur
 * and a hash map size of 2^(heap_exp_cur + 1). It is advisable to start small and
 * fast, e.g., heap size 2^5 = 32 entries, total size of the hashheap structure
 * less than one page of memory and well inside the L1 cache size. The data
 * structure will grow as needed if more memory is required but cannot shrink.
 *
 * The heap_count is the number of items currently in the heap, the heap_size
 * and hash_size are the allocated number of slots, where hash_size is twice the
 * heap_size once initialized (an invariant).
 *
 * item_counter is a running count of all items seen, used to assign new handles
 * valid for this hashheap only.
 */

struct cmi_hashheap {
    struct cmi_heap_tag *heap;
    uint16_t heap_exp_init;
    uint16_t heap_exp_cur;
    uint64_t heap_size;
    uint64_t heap_count;
    cmi_heap_compare_func *heap_compare;
    struct cmi_hash_tag *hash_map;
    uint64_t hash_size;
    uint64_t item_counter;
};

/*
 * cmi_hashheap_create - Allocate memory for a new priority queue.
 * Initializes the pointers to NULL, call cmi_hashmap_initialize next.
 */
extern struct cmi_hashheap *cmi_hashheap_create(void);

/*
 * cmi_hashheap_initialize - Allocate and initiate the actual heap/hash array.
 * Separate from cmi_hashheap_create to allow for inheritance by composition.
 *
 * hexp is the initial heap_exp_cur, e.g., hexp = 5 gives an initial heap
 * size of 2^5 = 32 and a hash map size of 2^(5+1) = 64.
 *
 * cmp is the application-defined compare function for this hashheap, taking
 * pointers to two heap tags and returning true if the first should go before
 * the second, using whatever consideration is appropriate for the usage. If
 * NULL, we will sort in an increasing `dkey` order.
 */
extern void cmi_hashheap_initialize(struct cmi_hashheap *hp,
                                    uint16_t hexp,
                                    cmi_heap_compare_func *cmp);

/*
 * cmi_hashheap_clear - Empties the hash heap.
 * Does not shrink the heap to the initial size, continues at the size it has.
 * Does not reset the item counter for issuing new handles, continues series.
 */
extern void cmi_hashheap_clear(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_terminate - Return the hashheap to a newly created state
 * freeing any allocated memory for the heap and hash map.
 */
extern void cmi_hashheap_terminate(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_reset - Return the hashheap to a newly initialized state.
 * Equivalent to cmi_hashheap_terminate() followed by cmi_hashheap_initialize()
 */
extern void cmi_hashheap_reset(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_destroy - Free memory allocated for the priority queue,
 * including both the array (if not NULL) and *hp itself.
 */
extern void cmi_hashheap_destroy(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_enqueue: Insert an item (pl1, pl2, pl3, pl4) into the priority
 * queue using the priority keys dkey and ikey. The exact meaning is application
 * defined, depending on the heap compare function provided.
 *
 * Returns the handle to the new item, handle > 0.
 */
extern uint64_t cmi_hashheap_enqueue(struct cmi_hashheap *hp,
                                     void *pl1,
                                     void *pl2,
                                     void *pl3,
                                     void *pl4,
                                     double dkey,
                                     int64_t ikey);

/*
 * cmi_hashheap_dequeue - Removes the highest priority item from the queue
 * (according to the ordering given by the comparator function) and returns a
 * pointer to its current location. Note that this is a temporary location that
 * will be overwritten in the next enqueue operation.
 */
extern void **cmi_hashheap_dequeue(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_count - Returns the number of items currently in the queue.
 */
static inline uint64_t cmi_hashheap_count(const struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    return hp->heap_count;
}

/*
 * cmi_hashheap_last_handle - Returns the number of items ever enqueued.
 */
static inline uint64_t cmi_hashheap_last_handle(const struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    return hp->item_counter;
}

/*
 * cmi_hashheap_is_empty - Returns true if count = 0, false otherwise
 */
static inline bool cmi_hashheap_is_empty(const struct cmi_hashheap *hp)
{
    return ((hp == NULL) || (hp->heap_count == 0u));
}

/*
 * cmi_hashheap_peek_item - Returns a pointer to the location of the item
 * currently at the top of the priority queue, without removing it.
 */
static inline void **cmi_hashheap_peek_item(const struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    if (cmi_hashheap_is_empty(hp)) {
        return NULL;
    }

    struct cmi_heap_tag *first = &(hp->heap[1]);
    void **item = first->item;

    return item;
}

/*
 * cmi_hashheap_peek_dkey/ikey - Returns the dkey/ikey of the first item.
 *
 * These functions have no good way to return an out-of-band error value, will
 * fire an assert instead if called on an empty hashheap. Check first.
 */
static inline double cmi_hashheap_peek_dkey(const struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hp->heap != NULL);
    cmb_assert_release(hp->heap_count != 0u);

    const struct cmi_heap_tag *first = &(hp->heap[1]);

    return first->dkey;
}

static inline int64_t cmi_hashheap_peek_ikey(const struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hp->heap != NULL);
    cmb_assert_release(hp->heap_count != 0u);

    const struct cmi_heap_tag *first = &(hp->heap[1]);

    return first->ikey;
}

/*
 * cmi_hashheap_remove: Remove the item from the priority queue. Returns true if
 * found (and removed), false if not found (already removed). Either way,
 * the item will not be in the queue at the end of this call.
 */
extern bool cmi_hashheap_remove(struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_cancel - Syntactic sugar for cmi_hashheap_remove
 */
static inline bool cmi_hashheap_cancel(struct cmi_hashheap *hp, const uint64_t handle)
{
    return cmi_hashheap_remove(hp, handle);
}

/*
 * cmi_hashheap_is_enqueued - Is the given item currently in the queue?
 */
extern bool cmi_hashheap_is_enqueued(const struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_item - Return a pointer to the current location of the item
 * associated with the given handle. Note that the location is volatile and will
 * be overwritten in the next enqueue/dequeue operation, but the item value will
 * continue to be associated with the handle also when moved to a different
 * location. This function can be used to manipulate the contents of an item, but
 * this needs to be done atomically. Do not expect the item to be in the same
 * location later, retrieve it again before each use.
 */
extern void **cmi_hashheap_item(const struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_dkey/ikey - Get the dkey/ikey for the given item.
 * Precondition: The item is in the priority queue, otherwise it is an error.
 * If in doubt, call cmi_hashheap_is_enqueued(handle) first to verify.
 */
extern double cmi_hashheap_dkey(const struct cmi_hashheap *hp, uint64_t handle);
extern int64_t cmi_hashheap_ikey(const struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_reprioritize - Changes one or more of the prioritization keys.
 * Precondition: The event is in the event queue.
 */
extern void cmi_hashheap_reprioritize(const struct cmi_hashheap *hp,
                                      uint64_t handle,
                                      double dkey,
                                      int64_t ikey);

/*
 * cmi_hashheap_pattern_find - Search the priority queue for an item with values
 * matching the given pattern and return its handle if one exists in the queue,
 * i.e. (item[0] == val1) && (item[1] == val2) && (item[2] == val3) && (item[3] == val4).
 *
 * Returns zero if no match. The value zero is not a valid handle, since handles
 * start from one, hence an out-of-band-value here.
 * The item value arguments to be matched can be NULL.
 * CMI_ANY_ITEM is a wildcard, matching any item value.
 *
 * Will start the search from the beginning of the priority queue each time,
 * since the queue may have changed in the meantime. There is no guarantee
 * for it returning the highest priority item first, only that it will find
 * some item that matches the search pattern if at least one exists in the
 * queue. The sequence in which items are found is unspecified.
 */

#define CMI_ANY_ITEM ((void *)0xFFFFFFFFFFFFFFFFull)
extern uint64_t cmi_hashheap_pattern_find(const struct cmi_hashheap *hp,
                                          const void *val1,
                                          const void *val2,
                                          const void *val3,
                                          const void *val4);

/* cmi_hashheap_pattern_count - Similarly, count the number of matching items. */
extern uint64_t cmi_hashheap_pattern_count(const struct cmi_hashheap *hp,
                                           const void *val1,
                                           const void *val2,
                                           const void *val3,
                                           const void *val4);

/*
 * cmi_hashheap_pattern_cancel - Cancel all matching items, returns the number
 * of events that were canceled, possibly zero.
 */
extern uint64_t cmi_hashheap_pattern_cancel(struct cmi_hashheap *hp,
                                            const void *val1,
                                            const void *val2,
                                            const void *val3,
                                            const void *val4);

/*
 * cmi_hashheap_print - Print the current content of the heap and hash map.
 * Intended for debugging use, will print hexadecimal pointer values and
 * similar raw data values from the event tag structs.
 */
extern void cmi_hashheap_print(const struct cmi_hashheap *hp, FILE *fp);

#endif /* CIMBA_CMI_HASHHEAP_H */