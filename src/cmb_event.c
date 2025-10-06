/*
 * cmb_event.c - event view of discrete event simulation.
 * Provides routines to handle clock sequencing and event scheduling.
 *
 * Uses a hashheap data structure, i.e. a binary heap combined with an open
 * addressing hash map, both allocated contiguously in memory for the best
 * possible memory performance. The hash map uses a Fibonacci hash, aka Knuth's
 * multiplicative method, combined with simple linear probing and lazy
 * deletions from the hash map when events leave the heap.
 *
 * See also: Malte Skarupke (2018), "Fibonacci Hashing: The Optimization
 *   that the World Forgot (or: a Better Alternative to Integer Modulo)",
 *   https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
 *
 * Structure of this file: The data structures are defined first, then the
 * hash map implementation, the heap implementation, and last the user API for
 * the cmb_event hashheap event queue.
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

#include <stdbool.h>

#include "cmb_event.h"
#include "cmb_logger.h"

#include "cmi_config.h"
#include "cmi_memutils.h"

/*****************************************************************************/
/*                          The data structures                              */
/*****************************************************************************/

/*
 * sim_time : The simulation clock. It can be initiated to start from a
 * negative value, but it can only increase once initiated, never go back.
 * Read-only for the user application, only accessible through cmb_time()
 */
static CMB_THREAD_LOCAL double sim_time = 0.0;

/*
 * struct heap_tag : The record to store an event with its context.
 * These tags only exist as members of the event queue array, never alone.
 * The tuple (action, subject, object) is the actual event, scheduled to
 * execute at (time, priority). The handle is a unique event identifier, the
 * hash_index a reference to where in the hash map to maintain its location.
 * Padded to 64 bytes size for efficiency reasons.
 */
struct heap_tag {
    uint64_t handle;
    uint64_t hash_index;
    cmb_event_func *action;
    void *subject;
    void *object;
    double time;
    int16_t priority;
    unsigned char padding_bytes[14];
};

/* The event heap, stored as a resizable array of cmb_event_tags */
static CMB_THREAD_LOCAL struct heap_tag *event_heap = NULL;

/* The event counter, for assigning new event handle numbers */
static CMB_THREAD_LOCAL uint64_t event_counter = 0u;

/*
 * heap_exp : The heap and hash map need to be sized as powers of two, the hash
 * map with twice as many entries as the heap. heap_exp defines a heap size of
 * 2^heap_exp and a hash map of 2^(heap_exp + 1). Start small and fast,
 * e.g., heap size 2^5 = 32 entries, total size of the heaphash structure less
 * than one page of memory and well inside the L1 cache size. It will increase
 * exponentially as needed, but the number of entries in the event queue at
 * the same time can be surprisingly small even in a large model.
 */
static CMB_THREAD_LOCAL uint16_t heap_exp = 5u;

/* Current size of the heap, invariant equal to 2^heap_exp once initialized */
static CMB_THREAD_LOCAL uint64_t heap_size = 0u;

/* The number of heap elements currently in use. */
static CMB_THREAD_LOCAL uint64_t heap_count = 0u;

/*
 * struct hash_tag : Hash mapping from event handle to heap position.
 * Heap index value zero indicates a tombstone, event is no longer in heap.
 */
struct hash_tag {
    uint64_t handle;
    uint64_t heap_index;
};

/*
 * event_hash - The hash map, physically located in a contiguous memory area
 * with the event queue heap array. There are twice as many slots in the hash
 * map as in the heap array to ensure that the hash map load factor remains
 * below 50 % before both get resized to twice the previous size.
 */
static CMB_THREAD_LOCAL struct hash_tag *event_hash = NULL;

/* Current size of the hash, invariant equal to 2 * heap_size once initialized */
static CMB_THREAD_LOCAL uint64_t hash_size = 0u;

/*****************************************************************************/
/*                            The event hash map                             */
/*****************************************************************************/

/*
 * hash_handle : Fibonacci hash function, see
 * https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
 */
uint64_t hash_handle(const uint64_t handle)
{
    /*
     * The "magic number" is approx 2^64 / phi, the golden ratio.
     * The right shift maps to the hash map size, twice the heap size.
     */
    return (handle * 11400714819323198485llu) >> (64u - (heap_exp + 1));
}

/*
 * hash_find_handle : Find the heap index of a given handle, zero if not found.
 * Uses a bitmap with all ones in the first positions to wrap around fast,
 * instead of using the modulo operator. In effect, simulates overflow in an
 * unsigned integer of (heap_exp + 1) bits.
 */
uint64_t hash_find_handle(const uint64_t handle)
{
    const uint64_t bitmap = hash_size - 1u;
    uint64_t hash = hash_handle(handle);
    do {
        if (event_hash[hash].handle == handle) {
            /* Found, return the heap index (possibly a tombstone zero) */
            return event_hash[hash].heap_index;
        }

        /* Not there, linear probing, try next, possibly looping around */
        hash = (hash + 1u) & bitmap;
    } while (event_hash[hash].handle != 0u);

    /* Got to an empty slot, the handle is not in hash map */
    return 0u;
}

/*
 * hash_find_slot : Find the first free hash map slot for the given handle
 */
uint64_t hash_find_slot(const uint64_t handle)
{
    const uint64_t bitmap = hash_size - 1u;
    uint64_t hash = hash_handle(handle);
    for (;;) {
        /* Guaranteed to find a slot eventually, < 50 % hash load factor */
        if (event_hash[hash].heap_index == 0u) {
            /* Found a free slot */
            return hash;
        }

        /* Already taken, linear probing, try next, possibly looping around */
        hash = (hash + 1u) & bitmap;
    }
}

/*
 * Rehash old hash entries to new (current) hash map, removing any tombstones.
 */
void hash_rehash(const struct hash_tag *old_hash_map,
                 const uint64_t old_hash_size)
{
    for (uint64_t ui = 0u; ui < old_hash_size; ui++) {
        const uint64_t handle = old_hash_map[ui].handle;
        if (handle != 0u) {
            /* Something is here */
            const uint64_t heapidx = old_hash_map[ui].heap_index;
            if (heapidx != 0u) {
                /* It is not a tombstone */
                const uint64_t hashidx = hash_find_slot(handle);
                event_hash[hashidx].handle = handle;
                event_hash[hashidx].heap_index = heapidx;
                event_heap[heapidx].hash_index = hashidx;
            }
        }
    }
}

/*****************************************************************************/
/*                              The event heap                               */
/*****************************************************************************/

/* Test if heaptag index a should go before index b. If so, return true */
static bool heap_order_check(const uint64_t a, const uint64_t b)
{
    cmb_assert_release(event_heap != NULL);

    bool ret = false;
    if (event_heap[a].time < event_heap[b].time) {
        ret = true;
    }
    else if (event_heap[a].time == event_heap[b].time) {
        if (event_heap[a].priority > event_heap[b].priority) {
            ret = true;
        }
        else if (event_heap[a].priority == event_heap[b].priority) {
            cmb_assert_debug(event_heap[a].handle != event_heap[b].handle);
            if (event_heap[a].handle < event_heap[b].handle) {
                ret = true;
            }
        }
    }

    return ret;
}

/*
 * heap_grow: doubling the available heap and hash map sizes.
 * The old heap is memcpy'd into its new location, each event at the same
 * index as before. The new hash map is initialized to all zeros, the old
 * hash map is memcpy'd together with the old heap into the area that now
 * belongs to the new heap. From there, valid hash entries are rehashed into
 * their new locations in the new hash map. This works, since there is no
 * memory overlap between the copy of the old hash map and the new one.
 */
static void heap_grow(void)
{
    cmb_assert_release(event_heap != NULL);
    cmb_assert_release(heap_size < (UINT32_MAX / 2u));
    cmb_assert_debug(event_hash != NULL);
    cmb_assert_debug(cmi_is_power_of_two(heap_size));
    cmb_assert_debug(cmi_is_power_of_two(hash_size));

    /* Set the new heap size, i.e. the max number of events in the queue */
    heap_exp++;
    const uint64_t old_heap_size = heap_size;
    heap_size = 1u << heap_exp;
    cmb_assert_debug(heap_size == 2 * old_heap_size);
    const uint64_t old_hash_size = hash_size;
    hash_size = 2u * heap_size;

    /* Calculate the page-aligned memory footprint it will need */
    const size_t heapbytes = (heap_size + 2u) * sizeof(struct heap_tag);
    const size_t hashbytes = hash_size * sizeof(struct hash_tag);
    const size_t newsz = heapbytes + hashbytes;
    const size_t pagesz = cmi_get_pagesize();
    const size_t npages = (size_t)(newsz + pagesz - 1u) / pagesz;
    cmb_assert_debug(npages >= 1u);

    /* Save the old address and allocate the new area */
    struct heap_tag *old_heaploc = event_heap;
    unsigned char * newloc = cmi_aligned_alloc(pagesz, npages * pagesz);

    /* Copy the old heap straight into the new */
    struct heap_tag *new_heaploc = (struct heap_tag *)newloc;
    const size_t old_heapbytes = (old_heap_size + 2u) * sizeof(struct heap_tag);
    cmi_memcpy(new_heaploc, old_heaploc, old_heapbytes);
    event_heap = new_heaploc;

    /* Rehash the old hash map into the new */
    struct hash_tag *old_hashloc = event_hash;
    struct hash_tag *new_hashloc = (struct hash_tag *)(newloc + heapbytes);
    event_hash = new_hashloc;
    cmi_memset(event_hash, 0u, hashbytes);
    hash_rehash(old_hashloc, old_hash_size);

    /* Free the old heap and hash map */
    cmi_aligned_free(old_heaploc);
}

/* heap_up : Bubble a tag at index k upwards into its right place */
static void heap_up(uint64_t k)
{
    cmb_assert_debug(event_heap != NULL);
    cmb_assert_debug(k <= heap_count);

    /* Place a working copy at index 0 */
    event_heap[0] = event_heap[k];
    /* A binary tree, parent node at k / 2 */
    uint64_t l;
    while ((l = (k >> 1)) > 0) {
        if (heap_order_check(0, l)) {
            /* Our candidate event goes before the one at l, swap them */
            event_heap[k] = event_heap[l];
            const uint64_t khash = event_heap[k].hash_index;
            event_hash[khash].heap_index = k;
            k = l;
        }
        else {
            break;
        }
    }

    /* Copy the candidate into its correct slot */
    event_heap[k] = event_heap[0];
    const uint64_t khash = event_heap[k].hash_index;
    event_hash[khash].heap_index = k;
}

/* heap_down : Bubble a tag at index k downwards into its right place */
static void heap_down(uint64_t k)
{
    cmb_assert_debug(event_heap != NULL);
    cmb_assert_debug(k <= heap_count);

    /* Place a working copy at index 0 */
    event_heap[0] = event_heap[k];

    /* Binary heap, children at 2x and 2x + 1 */
    uint64_t j = heap_count >> 1;
    while (k <= j) {
        uint64_t l = k << 1;
        if (l < heap_count) {
            const uint64_t r = l + 1;
            if (heap_order_check(r, l)) {
                l++;
            }
        }

        if (heap_order_check(0, l)) {
            break;
        }

        /* Swap with child */
        event_heap[k] = event_heap[l];
        const uint64_t khash = event_heap[k].hash_index;
        event_hash[khash].heap_index = k;
        k = l;
    }

    /* Copy the event into its correct position */
    event_heap[k] = event_heap[0];
    const uint64_t khash = event_heap[k].hash_index;
    event_hash[khash].heap_index = k;
}

/*****************************************************************************/
/*                        The cmb_event_* public API                         */
/*****************************************************************************/

/*
 * cmb_time : Return current simulation time.
 */
double cmb_time(void)
{
    return sim_time;
}

/*
 * cmb_event_queue_init : Set starting simulation time, allocate and initialize
 * hashheap for use. Allocates contiguous memory aligned to an integer number
 * of memory pages for efficiency.
 */
void cmb_event_queue_init(const double start_time)
{
    cmb_assert_release(event_heap == NULL);
    cmb_assert_debug(event_hash == NULL);

    sim_time = start_time;
    heap_count = 0;

    /* Use initial value of heap_exp for sizing, hard-coded at top of file */
    heap_size = 1u << heap_exp;
    hash_size = 2 * heap_size;

    /* Calculate the memory size needed, page aligned */
    const size_t heapbytes = (heap_size + 2u) * sizeof(struct heap_tag);
    const size_t hashbytes = (heap_size * 2u) * sizeof(struct hash_tag);
    const size_t initsz = heapbytes + hashbytes;
    const size_t pagesz = cmi_get_pagesize();
    const size_t npages = (size_t)(initsz + pagesz - 1u) / pagesz;
    cmb_assert_debug(npages >= 1u);

    /* Allocate it and set pointers to heap and hash parts */
    const unsigned char *alignedbytes = cmi_aligned_alloc(pagesz, npages * pagesz);
    event_heap = (struct heap_tag *)alignedbytes;
    event_hash = (struct hash_tag *)(alignedbytes + heapbytes);

    /* Initialize the new hash map to all zeros */
    cmi_memset(event_hash, 0u, hashbytes);
}

/*
 * cmb_event_queue_destroy : Clean up, deallocating space.
 * Note that hash_exp is not reset to initial value.
 */
void cmb_event_queue_destroy(void)
{
    cmb_assert_release(event_heap != NULL);
    cmb_assert_debug(event_hash != NULL);

    cmi_aligned_free(event_heap);
    event_heap = NULL;
    event_hash = NULL;
    heap_size = 0;
    hash_size = 0;
    heap_count = 0;
}

/*
 * cmb_event_schedule : Insert event in event queue as indicated by activation
 * time t and priority p, return unique event handle.
 * Resizes hashheap if necessary.
 */
uint64_t cmb_event_schedule(cmb_event_func *action,
                            void *subject,
                            void *object,
                            const double time,
                            const int16_t priority)
{
    cmb_assert_release(time >= sim_time);
    cmb_assert_release(heap_count <= heap_size);
    cmb_assert_release(event_heap != NULL);
    cmb_assert_debug(event_hash != NULL);

    /* Do we have space? */
    if (heap_count == heap_size) {
       heap_grow();
    }

    /* Now we have */
    cmb_assert_debug(heap_count < heap_size);
    heap_count++;
    const uint64_t handle = ++event_counter;

    /* Initialize the heaptag for the event */
    event_heap[heap_count].handle = handle;
    event_heap[heap_count].action = action;
    event_heap[heap_count].subject = subject;
    event_heap[heap_count].object = object;
    event_heap[heap_count].time = time;
    event_heap[heap_count].priority = priority;

    /* Initialize the hashtag for the event, pointing it to the heaptag */
    const uint64_t hash = hash_find_slot(handle);
    event_hash[hash].handle = handle;
    event_hash[hash].heap_index = heap_count;

    /* Point the heaptag to the hashtag, and reshuffle heap */
    event_heap[heap_count].hash_index = hash;
    heap_up(heap_count);

    return handle;
}

/*
 * cmb_event_is_scheduled : Is the given event scheduled?
 */
bool cmb_event_is_scheduled(const uint64_t handle)
{
    cmb_assert_release(event_heap != NULL);
    cmb_assert_debug(event_hash != NULL);

    return (hash_find_handle(handle) != 0u) ? true : false;
}

/*
 * cmb_event_time : The currently scheduled time for the given event
 */
double cmb_event_time(const uint64_t handle)
{
    cmb_assert_release(event_heap != NULL);
    cmb_assert_debug(event_hash != NULL);

    const uint64_t idx = hash_find_handle(handle);
    cmb_assert_release(idx != 0u);

    return event_heap[idx].time;
}

/*
 * cmb_event_priority : The current priority for the given event
 */
int16_t cmb_event_priority(uint64_t handle)
{
    cmb_assert_release(event_heap != NULL);
    cmb_assert_debug(event_hash != NULL);

    /* For now, just assert that this event must be in the heap */
    const uint64_t idx = hash_find_handle(handle);
    return event_heap[idx].priority;
}

/*
 * cmb_event_execute_next : Remove and execute the next event, update clock.
 * The next event is always in position 1, while position 0 is working space
 * for the heap. The event may schedule other events, needs to have a
 * consistent heap state without itself. Temporarily saves the next event to
 * workspace at the end of list before executing it, to ensure a consistent
 * heap and hash.
 */
bool cmb_event_execute_next(void)
{
    if ((event_heap == NULL) || (heap_count == 0u)) {
        /* Nothing to do */
        return false;
    }

    /* Advance clock */
    sim_time = event_heap[1u].time;

    /* Copy the event to working space at the end of the heap */
    const uint64_t tmp = heap_count + 1u;
    event_heap[tmp] = event_heap[1u];

    /* Mark it as deleted (a tombstone) in the hash map */
    const uint64_t nxthash = event_heap[tmp].hash_index;
    event_hash[nxthash].heap_index = 0u;

    /* Reshuffle the heap */
    event_heap[1u] = event_heap[heap_count];
    const uint64_t hashidx = event_heap[1u].hash_index;
    event_hash[hashidx].heap_index = 1u;
    heap_count--;
    heap_down(1);

    /* Execute the event */
    (*event_heap[tmp].action)(event_heap[tmp].subject,
                                   event_heap[tmp].object);

    return true;
}

/*
 * cmb_event_cancel : Cancel the given event and reshuffle heap
 * Precondition: Event must be in heap.
 */
void cmb_event_cancel(const uint64_t handle)
{
    const uint64_t heapidx = hash_find_handle(handle);
    cmb_assert_release(heapidx != 0u);
    cmb_assert_debug(event_heap[heapidx].handle == handle);

    /* Lazy deletion, tombstone it */
    uint64_t hashidx = event_heap[heapidx].hash_index;
    event_hash[hashidx].heap_index = 0u;

    /* Remove event from heap position heapidx */
    if (heapidx == heap_count) {
        heap_count--;
    }
    else if (heap_order_check(heapidx, heap_count)) {
        event_heap[heapidx] = event_heap[heap_count];
        hashidx = event_heap[heapidx].hash_index;
        event_hash[hashidx].heap_index = heapidx;
        heap_count--;
        heap_down(heapidx);
    }
    else {
        event_heap[heapidx] = event_heap[heap_count];
        hashidx = event_heap[heapidx].hash_index;
        event_hash[hashidx].heap_index = heapidx;
        heap_count--;
        heap_up(heapidx);
    }
}

/*
 * cmb_event_reschedule : Reschedule the given event and reshuffle heap
 * Precondition: The event must be in heap.
 */
void cmb_event_reschedule(const uint64_t handle, const double time)
{
    cmb_assert_release(time >= sim_time);

    const uint64_t heapidx = hash_find_handle(handle);
    cmb_assert_release(heapidx != 0u);
    cmb_assert_debug(heapidx <= heap_count);

    const double tmp = event_heap[heapidx].time;
    event_heap[heapidx].time = time;
    if (time > tmp) {
        heap_down(heapidx);
    }
    else {
        heap_up(heapidx);
    }
}

/*
 * Reprioritize the given event and reshuffle heap
 * Precondition: The event must be in heap.
 */
void cmb_event_reprioritize(const uint64_t handle,
                            const int16_t priority)
{
    const uint64_t heapidx = hash_find_handle(handle);
    cmb_assert_release(heapidx != 0u);
    cmb_assert_debug(heapidx <= heap_count);

    const int tmp = event_heap[heapidx].priority;
    event_heap[heapidx].priority = priority;
    if (priority < tmp) {
        heap_down(heapidx);
    }
    else {
        heap_up(heapidx);
    }
}

/*
 * event_match : Wildcard search helper function to get the condition
 * out of the next three functions.
 */
static bool event_match(cmb_event_func *action,
                        const void *subject,
                        const void *object,
                        const struct heap_tag *event)
{
    bool ret = true;
    if ( ((action != event->action) && (action != CMB_ANY_ACTION))
      || ((subject != event->subject) && (subject != CMB_ANY_SUBJECT))
      || ((object != event->object) && (object != CMB_ANY_OBJECT))) {
        ret = false;
    }

    return ret;
}

/*
 * cmb_event_find : Locate a specific event, using the CMB_ANY_* constants as
 * wildcards in the respective positions. Returns the handle of the event, or
 * zero if none found.
 */
uint64_t cmb_event_find(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    for (uint64_t ui = 1u; ui <= heap_count; ui++) {
        const struct heap_tag *event = &(event_heap[ui]);
        if (event_match(action, subject, object, event)) {
            return event->handle;
        }
    }

    /* Not found */
    return 0u;
}

/*
 * cmb_event_count : Count matching events using CMB_ANY_* as wildcards.
 * Returns the number of matching events, possibly zero.
 */
uint64_t cmb_event_count(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    /* Note that NULL may be a valid argument here */
    uint64_t cnt = 0u;
    for (uint64_t ui = 1u; ui <= heap_count; ui++) {
        const struct heap_tag *event = &(event_heap[ui]);
        if (event_match(action, subject, object, event)) {
            cnt++;
        }
    }

    return cnt;
}

/*
 * cmb_event_cancel_all : Cancel all matching events.
 * Two-pass approach: Allocate temporary storage for the list of
 * matching handles in the first pass, then cancel these in the
 * second pass. Avoids any possible issues caused by modification
 * (reshuffling) of the heap while iterating over it.
 * Returns the number of events cancelled, possibly zero.
 */
uint64_t cmb_event_cancel_all(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    /* Note that NULL may be a valid argument here */
    uint64_t cnt = 0u;

    /* Allocate space enough to match everything in the heap */
    uint64_t *tmp = cmi_malloc(heap_count * sizeof(*tmp));

    /* First pass, recording the matches */
    for (uint64_t ui = 1; ui <= heap_count; ui++) {
        const struct heap_tag *event = &(event_heap[ui]);
        if (event_match(action, subject, object, event)) {
            /* Matched, note it on the list */
            tmp[cnt++] = event_heap[ui].handle;
        }
    }

    /* Second pass, cancel the matching events, never mind the
     * heap reshuffling underneath us for each cancel.
     */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        cmb_event_cancel(tmp[ui]);
    }

    cmi_free(tmp);
    return cnt;
}

/*
 * cmb_event_heap_print : Print content of event heap, useful for debugging
 */
void cmb_event_heap_print(FILE *fp)
{
    fprintf(fp, "Event heap:\n");
    for (uint64_t ui = 1u; ui <= heap_count; ui++) {
        /*
         * Use a contrived cast to circumvent strict ban on conversion
         * between function and object pointer in ISO C.
         */
        static_assert(sizeof(event_heap[ui].action) == sizeof(void*),
            "Pointer to function expected to be same size as pointer to void");

        fprintf(fp, "heap index %llu: time %#8.4g pri %d: handle %llu hash index %llu : %p  %p  %p\n", ui,
                event_heap[ui].time,
                event_heap[ui].priority,
                event_heap[ui].handle,
                event_heap[ui].hash_index,
                *(void**)(&(event_heap[ui].action)),
                event_heap[ui].subject,
                event_heap[ui].object);
    }
}

/*
 * cmb_event_hash_print : Print content of hash map, useful for debugging
 */
void cmb_event_hash_print(FILE *fp)
{
    fprintf(fp, "Event hash map:\n");
    for (uint64_t ui = 0u; ui < hash_size; ui++) {
        fprintf(fp, "hash index %llu: handle %llu  heap index %llu\n", ui,
                event_hash[ui].handle,
                event_hash[ui].heap_index);
    }
}