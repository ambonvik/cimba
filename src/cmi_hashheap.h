/*
 * cmi_hashheap.h - The combined heap / hash map data structure used for
 * priority queues, both as the main event queue in the simulation and for the
 * priority queues associated with each guarded resource.
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
 * Thereafter, three keys used for ordering the heap, one double (e.g. time),
 * one signed 64-bit int (e.g., a priority), and an unsigned 64-bit int (e.g.
 * a FIFO sequence number). The exact meaning and ordering is application
 * defined by the heap compare function.
 *
 * Finally, there are three 64-bit payload values, which could be the (action,
 * subject, object) tuple for an event, or something else in other use cases.
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
 * typedef cmi_heap_compare_func : Return true if a goes before b.
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
 * typedef cmb_hashheap_compare_func : The generic event function type
 */
typedef void (cmb_event_func)(void *subject, void *object);

/*
 * cmb_event_queue_init : Initialize the event queue itself.
 * Must be called before any events can be scheduled or executed.
 */
extern void cmb_event_queue_init(double start_time);

/*
 * cmb_event_queue_destroy : Free memory allocated for event queue and
 * reinitialize pointers. Can be reinitialized by calling cmb_event_queue_init
 * again to start a new simulation run.
 */
extern void cmb_event_queue_destroy(void);

/*
 * cmb_event_schedule: Insert event in event queue as indicated by reactivation
 * time and priority. An event cannot be scheduled at a time before current.
 * Returns the unique handle of the scheduled event.
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