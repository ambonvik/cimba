/*
 * cmi_hashheap.h - The combined heap / hash map data structure used for
 * priority queues, both as the main event queue in the simulation and for the
 * priority queues associated with each guarded resource. It provides enqueue,
 * dequeue, peek, and cancel operations, plus item search functions.
 *
 * Each item in the priority queue is a tuple of (up to) three 64-bit payload
 * values, such as (action, subject, object) for an cmb_event.
 * The item is uniquely identified by a 64-bit handle returned when it is
 * enqueued. An item can be cancelled or reprioritized with reference to this
 * handle.
 *
 * An item in the queue has (up to) three priority keys for determining the
 * sorting order; one double, one signed 64-bit integer, and one unsigned 64-bit
 * integer. The semantics are application defined, determined by a compare
 * function that takes two pointers to items a and b, and returns a bool
 * indicating if a should precede b in the priority order. For example, in the
 * main event queue, the priority keys would be reactivation time, priority, and
 * the sequential event handle. A pointer to the appropriate compare function is
 * stored in the hashheap control structure.
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025.
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

/*
 * struct cmi_heap_tag : The record to store an item in the priority queue.
 * These tags only exist as members of the event queue array, never alone.
 * The handle is a unique event identifier, the hash_index a reference to where
 * in the hash map it is located. The item is a 3-tuple of 64-bit values, here
 * represented as void*, but could be used for any other 64-bit value depending
 * on application needs (see cmb_event.c for an example).
 *
 * Note that the heap tag is 8 * 8 = 64 bytes large.
 */
struct cmi_heap_tag {
    uint64_t handle;
    uint64_t hash_index;
    void *item[3];
    double dkey;
    int64_t ikey;
    uint64_t ukey;
};

/*
 * typedef cmi_heap_compare_func : Return true if a goes before b in the
 * priority queue order.
 */
typedef bool (cmi_heap_compare_func)(const struct cmi_heap_tag *a,
                                     const struct cmi_heap_tag *b);

/*
 * struct cmi_hash_tag : Hash mapping from event handle to heap position.
 * Heap index value zero indicates a tombstone, event is no longer in heap.
 *
 * Note that the hash tag is 2 * 8 = 16 bytes large.
 */
struct cmi_hash_tag {
    uint64_t handle;
    uint64_t heap_index;
};

/*
 * struct cmi_hashheap : The hashheap control structure with direct pointers to
 * the heap and hash map, and a function for ordering comparison between items
 * in the heap.
 *
 * The heap and hash map need to be sized as powers of two, the hash  map with
 * twice as many entries as the heap. heap_exp defines a heap size of 2^heap_exp
 * and a hash map size of 2^(heap_exp + 1).
 *
 * Start small and fast, e.g., heap size 2^5 = 32 entries, total size of the
 * heaphash structure less than one page of memory and well inside the L1 cache
 * size. The data structure will grow as needed if more memory is required.
 *
 * The heap_count is the number of items currently in the heap, the heap_size
 * and hash_size are the allocated number of slots, where hash_size is twice the
 * heap_size once initialized (an invariant).
 *
 * item_counter is a running count of all items seen, used to assign new handles.
 */

struct cmi_hashheap {
    struct cmi_heap_tag *heap;
    uint16_t heap_exp;
    uint64_t heap_size;
    uint64_t heap_count;
    cmi_heap_compare_func *heap_compare;
    struct cmi_hash_tag *hash_map;
    uint64_t hash_size;
    uint64_t item_counter;
};

/*
 * cmi_hashheap_create : Allocate memory for a new priority queue.
 * Initializes the pointers to NULL, call cmi_hashmap_init next.
 *
 * hexp is the initial heap_exp, e.g. hex = 5 gives an initial heap
 * size of 2^5 = 32 and a hash map size of 2^(5+1) = 64.
 */
extern struct cmi_hashheap *cmi_hashheap_create(void);

/*
 * cmi_hashheap_init : Allocate and initiate the actual heap/hash array.
 *
 * A separate function from _create to allow for inheritance by composition.
 */
extern void cmi_hasheap_init(struct cmi_hashheap *hp,
                             uint16_t hexp,
                             cmi_heap_compare_func *cmp);

/*
 * cmi_hashheap_clear : Free memory allocated for the hash/heap array and reset
 * pointers to NULL. Does not free *hp itself.
 */
extern void cmi_hashheap_clear(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_destroy : Free memory allocated for the priority queue,
 * including both the array (if not NULL) and *hp itself.
 */
extern void cmi_hashheap_destroy(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_enqueue: Insert an item (pl1, pl2, pl3) into the priority queue
 * using the priority keys dkey, ikey, ukey. The exact meaning is application
 * defined, depending on the heap compare function provided.
 *
 * Returns the handle to the new item.
 */
extern uint64_t cmi_hashheap_enqueue(struct cmi_hashheap *hp,
                                     void *pl1,
                                     void *pl2,
                                     void *pl3,
                                     double dkey,
                                     int64_t ikey,
                                     uint64_t ukey);

/*
 * cmi_hashheap_dequeue : Removes the highest priority item from the queue
 * (according to the ordering given by the comparator function), and returns a
 * pointer to its current location. Note that this is a temporary location that
 * will be overwritten in the next enqueue operation.
 */
extern void **cmi_hasheap_dequeue(struct cmi_hashheap *hp);

/*
 * cmi_hashheap_peek : Returns a pointer to the location of the item currently
 * at the top of the priority queue, without removing it.
 */
extern void **cmi_hashheap_peek(const struct cmi_hashheap *hp);

/*
 * cmi_hashheap_cancel: Remove item from the priority queue. Returns true if
 * found (and removed), false if not found (already removed). Either way,
 * the item will not be in the queue at the end of this call.
 */
extern bool cmi_hashheap_cancel(struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_is_enqueued : Is the given item currently in the queue?
 */
extern bool cmi_hashheap_is_enqueued(struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_get_dkey/ikey/ukey : Get the dkey/ikey/ukey for the given item.
 * Precondition: The item is in the priority queue, otherwise error.
 * If in doubt, call cmi_hashheap_is_enqueued(handle) first to verify.
 */
extern double cmi_hashheap_get_dkey(const struct cmi_hashheap *hp, uint64_t handle);
extern int64_t cmi_hashheap_get_ikey(const struct cmi_hashheap *hp, uint64_t handle);
extern uint64_t cmi_hashheap_get_ukey(const struct cmi_hashheap *hp, uint64_t handle);

/*
 * cmi_hashheap_reprioritize: Changes one or more of the prioritization keys.
 * Precondition: The event is in the event queue.
 */
extern void cmi_hashheap_reprioritize(struct cmi_hashheap *hp,
                                      uint64_t handle,
                                      double dkey,
                                      int64_t ikey,
                                      uint64_t ukey);

/*
 * cmi_hashheap_find: Search the priority queue for an item with values matching
 * the given pattern and return its handle if one exists in the queue, i.e.
 *   (item[0] == val1) && (item[1] == val2) && (item[2] == val3).
 * Return zero if no match.
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
extern uint64_t cmi_hashheap_find(const struct cmi_hashheap *hp,
                                  const void *val1,
                                  const void *val2,
                                  const void *val3);

/* cmi_hashheap_count : Similarly, count the number of matching items. */
extern uint64_t cmb_event_count(const struct cmi_hashheap *hp,
                                const void *val1,
                                const void *val2,
                                const void *val3);

/*
 * cmi_hashheap_cancel_all : Cancel all matching items, returns the number
 * of events that were cancelled, possibly zero.
 */
extern uint64_t cmb_event_cancel_all(struct cmi_hashheap *hp,
                                     const void *val1,
                                     const void *val2,
                                     const void *val3);

/*
 * cmi_hashheap_print : Print the current content of the heap and hash map.
 * Intended for debugging use, will print hexadecimal pointer values and
 * similar raw data values from the event tag structs.
 */
extern void cmi_hashheap_print(const struct cmi_hashheap *hp, FILE *fp);

#endif /* CIMBA_CMI_HASHHEAP_H */