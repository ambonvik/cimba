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
static CMB_THREAD_LOCAL double cmi_event_sim_time = 0.0;

double cmb_time(void) {
    return cmi_event_sim_time;
}

/*
 * The tag to store an event with its context.
 * These tags only exist as members of the event queue, never seen alone.
 * Padded to 64 bytes size for efficiency reasons.
 */
struct cmb_event_tag {
    uint64_t handle;
    uint64_t hash_index;
    cmb_event_func *action;
    void *subject;
    void *object;
    double time;
    int16_t priority;
    unsigned char padding_bytes[14];
};

/* The event queue, a heap stored as a resizable array */
static CMB_THREAD_LOCAL struct cmb_event_tag *cmi_event_queue = NULL;

/* The event counter, for assigning new handle numbers */
static CMB_THREAD_LOCAL uint64_t cmi_event_counter = 0u;

/* The initial heap size */
static const uint16_t heap_chunk = 128;
/* Current size of the heap */
static CMB_THREAD_LOCAL uint64_t heap_size = 0;
/* The number of heap elements in use*/
static CMB_THREAD_LOCAL uint64_t heap_count = 0;

/*
 * Hash mapping from handle to event queue position.
 * Heap index value zero is a tombstone, event no longer in queue.
 *
 * Position 0 in the heap is used as working space for the heap_* functions,
 * not as a location for any scheduled event. The next event is in position 1.
 */
struct cmb_event_hash {
    uint64_t handle;
    uint64_t heap_index;
};

/* The hash map */
static CMB_THREAD_LOCAL struct cmb_event_hash *cmi_event_hashmap = NULL;

/* Manage the event queue itself, reserving two slots for working space */
void cmb_event_queue_init(const double start_time) {
    /* TODO: Rewrite for hashheap */
    cmb_assert_release(cmi_event_queue == NULL);
    cmb_assert_debug(sizeof(struct cmb_event_tag) == 64);

    cmi_event_sim_time = start_time;
    heap_size = heap_chunk;
    const size_t new_size = (heap_size + 2) * sizeof(struct cmb_event_tag);
    cmi_event_queue = cmi_malloc(new_size);
    heap_count = 0;
}

/* Clean up, deallocating space */
void cmb_event_queue_destroy(void) {
    /* TODO: Rewrite for hashheap */
    cmb_assert_release(cmi_event_queue != NULL);

    cmi_free(cmi_event_queue);
    cmi_event_queue = NULL;
    heap_size = heap_count = 0;
}

/* Test if tag a should go before b. If so, return true */
static bool heap_check(const uint64_t a, const uint64_t b) {
    cmb_assert_release(cmi_event_queue != NULL);
    bool ret = false;
    if (cmi_event_queue[a].time < cmi_event_queue[b].time) {
        ret = true;
    }
    else if (cmi_event_queue[a].time == cmi_event_queue[b].time) {
        if (cmi_event_queue[a].priority > cmi_event_queue[b].priority) {
            ret = true;
        }
        else if (cmi_event_queue[a].priority == cmi_event_queue[b].priority) {
            cmb_assert_debug(cmi_event_queue[a].handle != cmi_event_queue[b].handle);
            if (cmi_event_queue[a].handle < cmi_event_queue[b].handle) {
                ret = true;
            }
        }
    }

    return ret;
}

/*
 * Increase heap size by a chunk, adding two for temp storage
 */
static void heap_grow(void) {
    /* TODO: Rewrite for hasheap */
    cmb_assert_release(cmi_event_queue != NULL);
    cmb_assert_release(heap_size < (UINT32_MAX - heap_chunk));

    heap_size += heap_chunk;
    const size_t new_size = (heap_size + 2) * sizeof(struct cmb_event_tag);
    cmi_event_queue = cmi_realloc(cmi_event_queue, new_size);
 }

/* Bubble a tag upwards into place */
static void heap_up(uint64_t k) {
    /* TODO: Update hash map */
    cmi_event_queue[0] = cmi_event_queue[k];
    uint64_t l;
    while ((l = (k >> 1)) > 0) {
        if (heap_check(0, l)) {
            cmi_event_queue[k] = cmi_event_queue[l];
            k = l;
        }
        else {
            break;
        }
    }

    cmi_event_queue[k] = cmi_event_queue[0];
}

static void heap_down(uint64_t k) {
    /* TODO: Update hash map */
    cmi_event_queue[0] = cmi_event_queue[k];
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

        cmi_event_queue[k] = cmi_event_queue[l];
        k = l;
    }

    cmi_event_queue[k] = cmi_event_queue[0];
}

/*
 * Insert event in event queue as indicated by (relative)
 * reactivation time t and priority p
 */
void cmb_event_schedule(cmb_event_func *action,
                        void *subject,
                        void *object,
                        const double rel_time,
                        const int16_t priority) {
    /* TODO: Update hash map */
    cmb_assert_release(rel_time >= 0.0);
    cmb_assert_release(heap_count <= heap_size);
    cmb_assert_release(cmi_event_queue != NULL);

    if (heap_count == heap_size) {
       heap_grow();
    }

    heap_count++;
    cmi_event_queue[heap_count].handle = ++cmi_event_counter;;
    cmi_event_queue[heap_count].action = action;
    cmi_event_queue[heap_count].subject = subject;
    cmi_event_queue[heap_count].object = object;
    cmi_event_queue[heap_count].time = cmb_time() + rel_time;
    cmi_event_queue[heap_count].priority = priority;

    heap_up(heap_count);
}

/*
 * Remove and execute the next event, update simulation clock.
 * The next event is always in position 1, while position 0 is working space for
 * the heap. The event may schedule other events, needs to have a consistent
 * heap state without itself. Temporarily saves the next event to workspace at
 * the end of list before executing it. Does not shrink the heap array even if
 * unused chunks at end.
 */
bool cmb_event_execute_next(void) {
    /* TODO: Update hash map */
    if ((cmi_event_queue == NULL) || (heap_count == 0)) {
        /* Nothing to do */
        return false;
    }

    /* Advance clock, remove next event, reshuffle heap */
    cmi_event_sim_time = cmi_event_queue[1].time;
    const uint64_t tmp = heap_count + 1;
    cmi_event_queue[tmp] = cmi_event_queue[1];
    cmi_event_queue[1] = cmi_event_queue[heap_count];
    heap_count--;
    heap_down(1);

    /* Execute next event */
    (*cmi_event_queue[tmp].action)(cmi_event_queue[tmp].subject,
                                   cmi_event_queue[tmp].object);

    return true;
}

/* Cancel the event in position idx and reshuffle heap */
bool cmb_event_cancel(const uint64_t index) {
    /* TODO: Rewrite for hashheap handle */
    cmb_assert_release(index <= heap_count);

    if (heap_check(index, heap_count)) {
        cmi_event_queue[index] = cmi_event_queue[heap_count];
        heap_count--;
        heap_down(index);
    }
    else {
        cmi_event_queue[index] = cmi_event_queue[heap_count];
        heap_count--;
        heap_up(index);
    }

    return true;
}

/* Resdchedule the event in position idx and reshuffle heap */
bool cmb_event_reschedule(const uint64_t index, const double time) {
    /* TODO: Rewrite for hasheap handle */
    cmb_assert_release(index <= heap_count);
    cmb_assert_release(time >= cmi_event_sim_time);

    const double tmp = cmi_event_queue[index].time;
    cmi_event_queue[index].time = time;
    if (time > tmp) {
        heap_down(index);
    }
    else {
        heap_up(index);
    }

    return true;
}

/* Reprioritize the event in position idx and reshuffle heap */
bool cmb_event_reprioritize(const uint64_t index, const int16_t priority) {
    /* TODO: Rewrite for hasheap handle */
    cmb_assert_release(index <= heap_count);

    const int tmp = cmi_event_queue[index].priority;
    cmi_event_queue[index].priority = priority;
    if (priority < tmp) {
        heap_down(index);
    }
    else {
        heap_up(index);
    }

    return true;
}

/* Locate a specific event, using CMB_EVENT_ANY as a wildcard */
uint64_t cmb_event_find(cmb_event_func *action,
                        const void *subject,
                        const void *object) {
    /* TODO: Rewrite for hashheap handle */
    for (uint64_t i = 1; i <= heap_count; i++) {
        if (((action == CMB_ANY_ACTION)
                || (action == cmi_event_queue[i].action))
            && ((subject == CMB_ANY_SUBJECT)
                || (subject == cmi_event_queue[i].subject))
            && ((object == CMB_ANY_OBJECT)
                || (object == cmi_event_queue[i].object))) {
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
        static_assert(sizeof(cmi_event_queue[ui].action) == sizeof(void*),
            "Pointer to function expected to be same size as pointer to void");

        fprintf(fp, "%llu: time %#8.4g pri %d: %llu  %llu : %p  %p  %p\n", ui,
                cmi_event_queue[ui].time,
                cmi_event_queue[ui].priority,
                cmi_event_queue[ui].handle,
                cmi_event_queue[ui].hash_index,
                *(void**)(&(cmi_event_queue[ui].action)),
                cmi_event_queue[ui].subject,
                cmi_event_queue[ui].object);
    }
}

/*
 * Hash function, see
 * https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
 */
uint64_t fibonacci_hash(const uint64_t hash, const unsigned shift) {
    return (hash * 11400714819323198485llu) >> shift;
}