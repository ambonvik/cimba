/*
 * cmb_event.c - event view of discrete event simulation.
 * Provides routines to handle clock sequencing and event scheduling.
 *
 * https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
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
#include "cmi_memutils.h"

/* The simulation clock, read-only for user application */
static CMB_THREAD_LOCAL double sim_time = 0.0;

/* cmb_time : Return current simulation time. */
double cmb_time(void) {
    return sim_time;
}

/*
 * struct heap_tag : The record to store an event with its context.
 * These tags only exist as members of the event queue array, never seen alone.
 * The tuple (action, subject, object) is the actual event, scheduled to
 * execute at (time, priority). The handle is a unique identifier, the
 * hash_index a reference to where in the hash map to update its location.
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

static_assert(sizeof(struct heap_tag) == 64,
    "Something went very wrong with the padding in heap_tag");

/* The event queue, a heap stored as a resizable array of cmb_event_tags */
static CMB_THREAD_LOCAL struct heap_tag *event_heap = NULL;

/* The event counter, for assigning new event handle numbers */
static CMB_THREAD_LOCAL uint64_t event_counter = 0u;

/*
 * The heap and hash map need to be sized as powers of two, the hash map with
 * twice as many entries as the heap. heap_exp defines a heap size of
 * 2^heap_exp and a hash map of 2^(heap_exp + 1). Start small and fast,
 * heap size 2^5 = 32 entries, total size of the heaphash structure less than
 * one page of memory and well inside the L1 cache size. It will increase
 * exponentially as needed, but the number of entries in the event queue at
 * the same time can be surprisingly small even in a large model.
 */
static CMB_THREAD_LOCAL uint16_t heap_exp = 5u;

/* Current size of the heap */
static CMB_THREAD_LOCAL uint64_t heap_size = 0u;

/* The number of heap elements in use*/
static CMB_THREAD_LOCAL uint64_t heap_count = 0u;

/*
 * struct hash_tag : Hash mapping from handle to event queue position.
 * Heap index value zero is a tombstone, event no longer in queue.
 *
 * Position 0 in the heap is used as working space for the heap_* functions,
 * not as a location for any scheduled event. The next event is in position 1.
 */
struct hash_tag {
    uint64_t handle;
    uint64_t heap_index;
};

static_assert(sizeof(struct hash_tag) == 16,
    "Something went wrong with the size of hash entries");

/*
 * event_hash - The hash map, physically located in a contiguous memory area
 * with the event queue heap array. There are twice as many slots in the hash map as
 * in the heap array to ensure that the hash map utilization remains below 50 % before
 * both get resized to twice the previous size.
 */
static CMB_THREAD_LOCAL struct hash_tag *event_hash = NULL;

/* Allocate and initialize the event queue, reserving two slots for working space */
void cmb_event_queue_init(const double start_time) {
    cmb_assert_release(event_heap == NULL);
    cmb_assert_release(event_hash == NULL);

    heap_size = 1u << heap_exp;
    const size_t heapbytes = (heap_size + 2u) * sizeof(struct heap_tag);
    const size_t hashbytes = (heap_size * 2u) * sizeof(struct hash_tag);
    const size_t initsz = heapbytes + hashbytes;
    const size_t pagesz = cmi_get_pagesize();
    const size_t npages = (size_t)(initsz + pagesz - 1u) / pagesz;
    cmb_assert_debug(npages >= 1u);

    const unsigned char *alignedbytes = cmi_aligned_alloc(pagesz, npages);
    event_heap = (struct heap_tag *)alignedbytes;
    event_hash = (struct hash_tag *)(alignedbytes + heapbytes);
    cmb_assert_debug((void *)event_hash == (void *)(event_heap + heap_size + 1));
    /* Initialize the new hash map to all zeros */
    cmi_memset(event_hash, 0u, hashbytes);

    sim_time = start_time;
    heap_count = 0;
}

/* Clean up, deallocating space */
void cmb_event_queue_destroy(void) {
    cmb_assert_release(event_heap != NULL);

    cmi_aligned_free(event_heap);
    event_heap = NULL;
    event_hash = NULL;
    heap_size = heap_count = 0;
}

/* Test if heaptag index a should go before index b. If so, return true */
static bool heap_check(const uint64_t a, const uint64_t b) {
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
 * Hash function, see
 * https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
 */
uint64_t hash_handle(const uint64_t handle, const unsigned shift) {
    /* The "magic number" is approx 2^64 / phi, the golden ratio */
    return (handle * 11400714819323198485llu) >> shift;
}

/* Find the heap index of a given handle */
uint64_t hash_find_handle(const uint64_t handle, const unsigned shift) {
    uint64_t hash = hash_handle(handle, shift);
    do {
        if (event_hash[hash].handle == handle) {
            /* Found, return the heap index (possibly a tombstone) */
            return event_hash[hash].heap_index;
        }
        else if (++hash >= 2u * heap_size) {
            /* Loop around */
            hash = 0u;
        }
    } while (event_hash[hash].handle != 0u);

    /* Empty slot, not found */
    return 0u;
}

/* FInd hte first free hash map slot for the given handle */
uint64_t hash_find_slot(const uint64_t handle, const unsigned shift) {
    uint64_t hash = hash_handle(handle, shift);
    for (;;) {
        /* Guaranteed to find a slot sooner or later */
        if (event_hash[hash].heap_index == 0u) {
            /* Found a free slot */
            return hash;
        }
        else if (++hash >= 2u * heap_size) {
            /* Loop around */
            hash = 0u;
        }
    }
}

/*
 * Growing the heap, doubling the available heap and hash map sizes.
 */
static void heap_grow(void) {
    /* TODO: Rewrite for hashheap */
    cmb_assert_release(event_heap != NULL);
    cmb_assert_release(event_hash != NULL);
    cmb_assert_release(heap_size < (UINT32_MAX / 2u));

    heap_exp++;
    const uint64_t old_heap_size = heap_size;
    heap_size = 1u << heap_exp;
    cmb_assert_debug(heap_size == 2 * old_heap_size);

    const size_t heapbytes = (heap_size + 2u) * sizeof(struct heap_tag);
    const size_t hashbytes = (heap_size * 2u) * sizeof(struct hash_tag);
    const size_t newsz = heapbytes + hashbytes;
    const size_t pagesz = cmi_get_pagesize();
    const size_t npages = (size_t)(newsz + pagesz - 1u) / pagesz;
    cmb_assert_debug(npages >= 1u);

    /*
     * Reallocate heap and hash map to twice the size, copying the heap into
     * the new location. The old hash table also gets copied into the lower
     * half of the new area and will need to be rehashed into its new place.
     */
    const unsigned char *alignedbytes = cmi_aligned_realloc(event_heap, pagesz, npages);
    event_heap = (struct heap_tag *)alignedbytes;
    event_hash = (struct hash_tag *)(alignedbytes + heapbytes);
    cmb_assert_debug((void *)event_hash == (void *)(event_heap + heap_size + 1u));

    /* Initialize the new hash map to all zeros */
    cmi_memset(event_hash, 0u, hashbytes);
    struct hash_tag *old_hash = (struct hash_tag *)(event_heap + old_heap_size + 1u);

    /* TODO: Rehash the old hash entries to the new hash map, removing any tombstones. */
}

/* Bubble a tag at index k upwards into its right place */
static void heap_up(uint64_t k) {
    /* Place a working copy at index 0 */
    event_heap[0] = event_heap[k];
    /* A binary tree, parent node at k / 2 */
    uint64_t l;
    while ((l = (k >> 1)) > 0) {
        if (heap_check(0, l)) {
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

/* Bubble a tag at index k downwards into its right place */
static void heap_down(uint64_t k) {
    /* Place a working copy at index 0 */
    event_heap[0] = event_heap[k];

    /* Binary heap, children at 2x and 2x + 1 */
    uint64_t j = heap_count >> 1;
    while (k <= j) {
        uint64_t l = k << 1;
        if (l < heap_count) {
            const uint64_t r = l + 1;
            if (heap_check(r, l)) {
                l++;
            }
        }

        if (heap_check(0, l)) {
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

/*
 * Insert event in event queue as indicated by (relative)
 * reactivation time t and priority p
 */
uint64_t cmb_event_schedule(cmb_event_func *action,
                        void *subject,
                        void *object,
                        const double time,
                        const int16_t priority) {
    cmb_assert_release(time >= sim_time);
    cmb_assert_release(heap_count <= heap_size);
    cmb_assert_release(event_heap != NULL);

    if (heap_count == heap_size) {
       heap_grow();
    }

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
    const uint64_t hash = hash_find_slot(handle, 64u - heap_exp);
    event_hash[hash].handle = handle;
    event_hash[hash].heap_index = heap_count;

    /* Point the heaptag to the hashtag, and reshuffle heap */
    event_heap[heap_count].hash_index = hash;
    heap_up(heap_count);

    return handle;
}

/*
 * Remove and execute the next event, update simulation clock.
 * The next event is always in position 1, while position 0 is working space for
 * the heap. The event may schedule other events, needs to have a consistent
 * heap state without itself. Temporarily saves the next event to workspace at
 * the end of list before executing it, to ensure a consistent heap and hash.
 */
bool cmb_event_execute_next(void) {
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

/* Cancel the event in position idx and reshuffle heap */
bool cmb_event_cancel(const uint64_t handle) {
    const uint64_t heapidx = hash_find_handle(handle, 64u - heap_exp);
    if (heapidx == 0u) {
        /* Not found */
        return false;
    }

    cmb_assert_debug(event_heap[heapidx].handle == handle);

    /* Tombstone it */
   uint64_t hashidx = event_heap[heapidx].hash_index;
    event_hash[hashidx].heap_index = 0u;

    /* Remove event from heap position heapidx */
    if (heap_check(heapidx, heap_count)) {
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

    return true;
}

/* Reschedule the event in position idx and reshuffle heap */
bool cmb_event_reschedule(const uint64_t handle, const double time) {
    cmb_assert_release(time >= sim_time);

    const uint64_t heapidx = hash_find_handle(handle, 64 - heap_exp);
    cmb_assert_release(heapidx <= heap_count);
    if (heapidx == 0u) {
        /* Not found */
        return false;
    }

    const double tmp = event_heap[heapidx].time;
    event_heap[heapidx].time = time;
    if (time > tmp) {
        heap_down(heapidx);
    }
    else {
        heap_up(heapidx);
    }

    return true;
}

/* Reprioritize the event in position idx and reshuffle heap */
bool cmb_event_reprioritize(const uint64_t handle, const int16_t priority) {
    const uint64_t heapidx = hash_find_handle(handle, 64 - heap_exp);
    cmb_assert_release(heapidx <= heap_count);

    const int tmp = event_heap[heapidx].priority;
    event_heap[heapidx].priority = priority;
    if (priority < tmp) {
        heap_down(heapidx);
    }
    else {
        heap_up(heapidx);
    }

    return true;
}

/* Locate a specific event, using CMB_EVENT_ANY as a wildcard */
uint64_t cmb_event_find(cmb_event_func *action,
                        const void *subject,
                        const void *object) {
    for (uint64_t i = 1; i <= heap_count; i++) {
        if (((action == CMB_ANY_ACTION)
                || (action == event_heap[i].action))
            && ((subject == CMB_ANY_SUBJECT)
                || (subject == event_heap[i].subject))
            && ((object == CMB_ANY_OBJECT)
                || (object == event_heap[i].object))) {
                return i;
        }
    }

    /* Not found */
    return 0;
}

/* Print content of event queue for debugging purposes */
void cmb_event_queue_print(FILE *fp) {
    for (uint64_t ui = 1; ui <= heap_count; ui++) {
        /*
         * Use a contrived cast to circumvent strict ban on conversion
         * between function and object pointer
         */
        static_assert(sizeof(event_heap[ui].action) == sizeof(void*),
            "Pointer to function expected to be same size as pointer to void");

        fprintf(fp, "%llu: time %#8.4g pri %d: %llu  %llu : %p  %p  %p\n", ui,
                event_heap[ui].time,
                event_heap[ui].priority,
                event_heap[ui].handle,
                event_heap[ui].hash_index,
                *(void**)(&(event_heap[ui].action)),
                event_heap[ui].subject,
                event_heap[ui].object);
    }
}
