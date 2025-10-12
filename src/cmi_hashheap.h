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
 * in the hash map it is located.
 *
 * Note that the heap tag is 8 * 8 = 64 bytes large.
 */
struct cmi_heap_tag {
    uint64_t handle;
    uint64_t hash_index;
    double dkey;
    int64_t ikey;
    uint64_t ukey;
    uintptr_t payload[3];
};

/*
 * typedef cmi_heap_compare_func : Return true if a goes before b in the
 * priority queue order.
 */
typedef bool (cmi_heap_compare_func)(struct cmi_heap_tag *a,
                                     struct cmi_heap_tag *b);

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
 */

struct cmi_hashheap {
    struct cmi_heap_tag *heap;
    uint16_t heap_exp;
    uint64_t heap_size;
    uint64_t heap_count;
    cmi_heap_compare_func *heap_compare;
    struct cmi_hash_tag *hash_map;
    uint64_t hash_size;
};

/*
 * cmi_hashheap_create : Allocate memory for a new priority queue.
 * Initializes the pointers to NULL, call cmi_hashmap_init next.
 */
extern struct cmi_hashheap *cmi_hashheap_create(void);

/*
 * cmi_hashheap_init : Allocate and initiate the actual heap/hash array.
 * hex is the heap_exp, e.g. hex = 5 gives an initial heap size of 2^5 = 32 and
 * a hash map size of 2^(5+1) = 64.
 *
 * A separate function from _create to allow for inheritance by composition.
 */
extern void cmi_hasheap_init(struct cmi_hashheap *hp,
                             int16_t hex,
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
 * cmb_hashheap_enqueue: Insert an item into the priority queue
 */
extern uint64_t cmb_event_schedule(cmb_event_func *action,
                                   void *subject,
                                   void *object,
                                   double time,
                                   int16_t priority);

/*
 *  cmb_event_next : Removes and executes the first event in the event queue.
 *  If both reactivation time and priority equal, first in first out order.
 *
 *  Returns true for success, false for failure (e.g., empty event list), for
 *  use in loops like while(cmb_event_execute_next()) { ... }
 */
extern bool cmb_event_execute_next(void);

/*
 * cmb_event_is_scheduled : Is the given event currently in the event queue?
 */
extern bool cmb_event_is_scheduled(uint64_t handle);

/*
 * cmb_event_time : Get the currently scheduled time for an event
 * Precondition: The event is in the event queue.
 * If in doubt, call cmb_event_is_scheduled(handle) first to verify.
 */
extern double cmb_event_time(uint64_t handle);

/*
 * cmb_event_priority : Get the current priority for an event
 * Precondition: The event is in the event queue.
 */
extern int16_t cmb_event_priority(uint64_t handle);

/*
 * cmb_event_cancel: Remove event from event queue.
 * Precondition: The event is in the event queue.
 */
extern void cmb_event_cancel(uint64_t handle);

/*
 * cmb_event_reschedule: Reschedules event at index to another (absolute) time
 * Precondition: The event is in the event queue.
 */
extern void cmb_event_reschedule(uint64_t handle, double time);

/*
 * cmb_event_reprioritize: Reprioritizes event to another priority level
 * Precondition: The event is in the event queue.
 */
extern void cmb_event_reprioritize(uint64_t handle, int16_t priority);

/*
 * cmb_event_find: Search in event list for an event matching the given pattern
 * and return its handle if one exists in the queue. Return zero if no match.
 * CMB_ANY_* are wildcarda, matching any value in its position.
 *
 * Will start the search from the beginning of the event queue each time,
 * since the queue may have changed in the meantime. There is no guarantee
 * for it returning the event that will execute first, only that it will find
 * some event that matches the search pattern if one exists in the queue. The
 * sequence in which events are found is unspecified.
 */

#define CMB_ANY_ACTION ((cmb_event_func *)0xFFFFFFFFFFFFFFFFull)
#define CMB_ANY_SUBJECT ((void *)0xFFFFFFFFFFFFFFFFull)
#define CMB_ANY_OBJECT ((void *)0xFFFFFFFFFFFFFFFFull)

extern uint64_t cmb_event_find(cmb_event_func *action,
                               const void *subject,
                               const void *object);

/* cmb_event_count : Similarly, count the number of matching events. */
extern uint64_t cmb_event_count(cmb_event_func *action,
                                const void *subject,
                                const void *object);

/*
 * cmb_event_cancel_all : Cancel all matching events, returns the number
 * of events that were cancelled, possibly zero. Use e.g. for cancelling all
 * events related to some subject or object if that thing no longer is alive in
 * the simulation.
 */
extern uint64_t cmb_event_cancel_all(cmb_event_func *action,
                                     const void *subject,
                                     const void *object);

/*
 * cmb_event_heap_print : Print the current content of the event heap.
 * Intended for debugging use, will print hexadecimal pointer values and
 * similar raw data values from the event tag structs.
 */
extern void cmb_event_heap_print(FILE *fp);

/*
 * cmb_event_hash_print : Print the current content of the hash map.
 * Intended for debugging use, will print 64-bit handles and indexes.
 */
extern void cmb_event_hash_print(FILE *fp);

#endif /* CIMBA_CMI_HASHHEAP_H */