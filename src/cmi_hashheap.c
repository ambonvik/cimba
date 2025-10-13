/*
 * cmi_hashheap.c - Implements the hashheap priority queue, i.e. a binary heap
 * combined with an open addressing hash map, both allocated contiguously in
 * a shared memory array for the best possible memory performance. The array
 * resizes as needed, always in powers of two, where the number of hash map
 * slots is twice the heap size, guaranteeing less than 50 % load factor.
 *
 * The hash map uses a Fibonacci hash, aka Knuth's multiplicative method,
 * combined with simple linear probing and lazy deletions from the hash map when
 * items leave the heap.
 *
 * See also: Malte Skarupke (2018), "Fibonacci Hashing: The Optimization
 *   that the World Forgot (or: a Better Alternative to Integer Modulo)",
 *   https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
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

#include "cmi_hashheap.h"
#include "cmi_config.h"
#include "cmi_memutils.h"

/*
 * hash_handle : Fibonacci hash function.
 *
 * The "magic number" is approx 2^64 / phi, the golden ratio.
 * The right shift maps to the hash map size, twice the heap size.
 * See also:
 * https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
 */
uint64_t hash_handle(const struct cmi_hashheap *hp, const uint64_t handle)
{
    cmb_assert_debug(hp != NULL);

    return (handle * 11400714819323198485llu) >> (64u - (hp->heap_exp + 1));
}

/*
 * hash_find_handle : Find the heap index of a given handle, zero if not found.
 * Uses a bitmap with all ones in the first positions to wrap around fast,
 * instead of using the modulo operator. In effect, simulates overflow in an
 * unsigned integer of (heap_exp + 1) bits.
 */
uint64_t hash_find_handle(const struct cmi_hashheap *hp, const uint64_t handle)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->hash_map != NULL);

    const struct cmi_hash_tag *hm = hp->hash_map;
    uint64_t hash = hash_handle(hp, handle);
    const uint64_t bitmap = hp->hash_size - 1u;
     do {
        if (hm[hash].handle == handle) {
            /* Found, return the heap index (possibly a tombstone zero) */
            return hm[hash].heap_index;
        }

        /* Not there, linear probing, try next, possibly looping around */
        hash = (hash + 1u) & bitmap;
    } while (hm[hash].handle != 0u);

    /* Got to an empty slot, the handle is not in hash map */
    return 0u;
}

/*
 * hash_find_slot : Find the first free hash map slot for the given handle
 */
uint64_t hash_find_slot(const struct cmi_hashheap *hp, const uint64_t handle)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->hash_map != NULL);

    const struct cmi_hash_tag *hm = hp->hash_map;
    uint64_t hash = hash_handle(hp, handle);
    const uint64_t bitmap = hp->hash_size - 1u;
    for (;;) {
        /* Guaranteed to find a slot eventually, < 50 % hash load factor */
        if (hm[hash].heap_index == 0u) {
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
void hash_rehash(const struct cmi_hashheap *hp,
                 const struct cmi_hash_tag *old_hash_map,
                 const uint64_t old_hash_size)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->hash_map != NULL);
    cmb_assert_debug(old_hash_map != NULL);

    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;

    for (uint64_t ui = 0u; ui < old_hash_size; ui++) {
        const uint64_t handle = old_hash_map[ui].handle;
        if (handle != 0u) {
            /* Something is here */
            const uint64_t heapidx = old_hash_map[ui].heap_index;
            if (heapidx != 0u) {
                /* It is not a tombstone */
                const uint64_t hashidx = hash_find_slot(hp, handle);
                hash[hashidx].handle = handle;
                hash[hashidx].heap_index = heapidx;
                heap[heapidx].hash_index = hashidx;
            }
        }
    }
}

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

/* heap_up : Bubble a tag at index k upwards into its right place */
static void heap_up(struct cmi_hashheap *hp, uint64_t k)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(k <= hp->heap_count);

    /* Place a working copy at index 0 */
    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;
    heap[0] = heap[k];

    /* A binary tree, parent node at k / 2 */
    uint64_t l;
    while ((l = (k >> 1)) > 0) {
        if ((*(hp->heap_compare))(&(heap[0]), &(heap[l]))) {
            /* Our candidate event goes before the one at l, swap them */
            heap[k] = heap[l];
            const uint64_t khash = heap[k].hash_index;
            hash[khash].heap_index = k;
            k = l;
        }
        else {
            break;
        }
    }

    /* Copy the candidate into its correct slot */
    heap[k] = heap[0];
    const uint64_t khash = heap[k].hash_index;
    hash[khash].heap_index = k;
}

/* heap_down : Bubble a tag at index k downwards into its right place */
static void heap_down(struct cmi_hashheap *hp, uint64_t k)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(k <= hp->heap_count);

    /* Place a working copy at index 0 */
    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;
    heap[0] = heap[k];

    /* Binary heap, children at 2x and 2x + 1 */
    uint64_t j = (hp->heap_count >> 1);
    while (k <= j) {
        uint64_t l = k << 1;
        if (l < hp->heap_count) {
            const uint64_t r = l + 1;
            if ((*(hp->heap_compare))(&(heap[r]), &(heap[l]))) {
                l++;
            }
        }

        if ((*(hp->heap_compare))(&(heap[0]), &(heap[l]))) {
            break;
        }

        /* Swap with child */
        heap[k] = heap[l];
        const uint64_t khash = heap[k].hash_index;
        hash[khash].heap_index = k;
        k = l;
    }

    /* Copy the event into its correct position */
    heap[k] = heap[0];
    const uint64_t khash = heap[k].hash_index;
    hash[khash].heap_index = k;
}

/*
 * hashheap_grow: doubling the available heap and hash map sizes.
 * The old heap is memcpy'd into its new location, each event at the same
 * index as before. The new hash map is initialized to all zeros, the old
 * hash map is memcpy'd together with the old heap into the area that now
 * belongs to the new heap. From there, valid hash entries are rehashed into
 * their new locations in the new hash map. This works, since there is no
 * memory overlap between the copy of the old hash map and the new one.
 */
static void hashheap_grow(struct cmi_hashheap *hp)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->hash_map != NULL);
    cmb_assert_release(hp->heap_size < (UINT32_MAX / 2u));
    cmb_assert_debug(cmi_is_power_of_two(hp->heap_size));
    cmb_assert_debug(cmi_is_power_of_two(hp->hash_size));

    /* Set the new heap size, i.e. the max number of events in the queue */
    hp->heap_exp++;
    const uint64_t old_heapsz = hp->heap_size;
    hp->heap_size = 1u << hp->heap_exp;
    cmb_assert_debug(hp->heap_size == 2 * old_heapsz);
    const uint64_t old_hashsz = hp->hash_size;
    hp->hash_size = 2u * hp->heap_size;

    /* Calculate the page-aligned memory footprint it will need */
    const size_t heapbts = (hp->heap_size + 2u) * sizeof(struct cmi_heap_tag);
    const size_t hashbts = hp->hash_size * sizeof(struct cmi_hash_tag);
    const size_t newsz = heapbts + hashbts;
    const size_t pagesz = cmi_get_pagesize();
    const size_t npages = (size_t)(newsz + pagesz - 1u) / pagesz;
    cmb_assert_debug(npages >= 1u);

    /* Save the old address and allocate the new area */
    struct cmi_heap_tag *old_heaploc = hp->heap;
    unsigned char *newloc = cmi_aligned_alloc(pagesz, npages * pagesz);

    /* Copy the old heap straight into the new */
    struct cmi_heap_tag *new_heaploc = (struct cmi_heap_tag *)newloc;
    const size_t old_heapbts = (old_heapsz + 2u) * sizeof(struct cmi_heap_tag);
    cmi_memcpy(new_heaploc, old_heaploc, old_heapbts);
    hp->heap = new_heaploc;

    /* Rehash the old hash map into the new */
    struct cmi_hash_tag *old_hashloc = hp->hash_map;
    struct cmi_hash_tag *new_hashloc = (struct cmi_hash_tag *)(newloc + heapbts);
    hp->hash_map = new_hashloc;
    cmi_memset(hp->hash_map, 0u, hashbts);
    hash_rehash(hp, old_hashloc, old_hashsz);

    /* Free the old heap and hash map */
    cmi_aligned_free(old_heaploc);
}

static void hashheap_nullify(struct cmi_hashheap *hp)
{
    cmb_assert_debug(hp != NULL);

    hp->heap = NULL;
    hp->heap_exp = 0u;
    hp->heap_size = 0u;
    hp->heap_count = 0u;
    hp->heap_compare = NULL;
    hp->hash_map = NULL;
    hp->hash_size = 0u;
    hp->item_counter = 0u;
}

struct cmi_hashheap *cmi_hashheap_create(void)
{
    struct cmi_hashheap *hp = cmi_malloc(sizeof(*hp));
    hashheap_nullify(hp);

    return hp;
}

/*
 * cmi_hashheap_init : Initialize hashheap for use.
 *
 * Allocates a contiguous memory array aligned to an integer number of memory
 * pages for efficiency.
 */
void cmi_hashheap_init(struct cmi_hashheap *hp,
                      const uint16_t hexp,
                      cmi_heap_compare_func *cmp)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hp->heap == NULL);
    cmb_assert_release(hp->hash_map == NULL);
    cmb_assert_release(hexp > 0u);

    /* Use initial value of heap_exp for sizing */
    hp->heap_exp = hexp;
    hp->heap_size = 1u << hp->heap_exp;
    hp->hash_size = 2u * hp->heap_size;
    hp->heap_count = 0u;

    hp->heap_compare = cmp;

    /* Calculate the memory size needed, page aligned */
    const size_t heapbts = (hp->heap_size + 2u) * sizeof(struct cmi_heap_tag);
    const size_t hashbts = (hp->heap_size * 2u) * sizeof(struct cmi_hash_tag);
    const size_t initsz = heapbts + hashbts;
    const size_t pagesz = cmi_get_pagesize();
    const size_t npages = (size_t)(initsz + pagesz - 1u) / pagesz;
    cmb_assert_debug(npages >= 1u);

    /* Allocate it and set pointers to heap and hash parts */
    const unsigned char *abts = cmi_aligned_alloc(pagesz, npages * pagesz);
    hp->heap = (struct cmi_heap_tag *)abts;
    hp->hash_map = (struct cmi_hash_tag *)(abts + heapbts);

    /* Initialize the new hash map to all zeros */
    cmi_memset(hp->hash_map, 0u, hashbts);
}

/*
 * cmi_hashheap_clear : Reset hashheap to initial state.
 */
void cmi_hashheap_clear(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    if (hp->heap != NULL) {
        cmi_aligned_free(hp->heap);
    }

    hashheap_nullify(hp);
}


/*
 * cmi_hashheap_destroy : Clean up, deallocating space.
 * Note that hash_exp is not reset to initial value.
 */
void cmi_hashheap_destroy(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    if (hp->heap != NULL) {
        cmi_hashheap_clear(hp);
    }

    cmi_free(hp);
}

/*
 * cmi_hashheap_enqueue : Insert item in queue, return unique event handle.
 * Resizes hashheap if necessary.
 */
uint64_t cmi_hashheap_enqueue(struct cmi_hashheap *hp,
                                     void *pl1,
                                     void *pl2,
                                     void *pl3,
                                     double dkey,
                                     int64_t ikey,
                                     uint64_t ukey)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->hash_map != NULL);
    cmb_assert_release(hp->heap_count <= hp->heap_size);
    cmb_assert_debug(cmi_is_power_of_two(hp->heap_size));
    cmb_assert_debug(cmi_is_power_of_two(hp->hash_size));

    /* Do we have space? */
    if (hp->heap_count == hp->heap_size) {
       hashheap_grow(hp);
    }

    /* Now we have */
    cmb_assert_debug(hp->heap_count < hp->heap_size);
    const uint64_t handle = ++hp->item_counter;
    const uint64_t hc = ++hp->heap_count;

    /* Initialize the heaptag for the event */
    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;

    heap[hc].handle = handle;
    heap[hc].item[0] = pl1;
    heap[hc].item[1] = pl2;
    heap[hc].item[2] = pl3;
    heap[hc].dkey = dkey;
    heap[hc].ikey = ikey;
    heap[hc].ukey = ukey;

    /* Initialize the hashtag for the event, pointing it to the heaptag */
    const uint64_t idx = hash_find_slot(hp, handle);
    hash[idx].handle = handle;
    hash[idx].heap_index = hc;

    /* Point the heaptag to the hashtag, and reshuffle heap */
    heap[hc].hash_index = idx;
    heap_up(hp, hc);

    return handle;
}

/*
 * cmi_hashheap_dequeue : Remove and return the next item.
 *
 * The next event is always in position 1, while position 0 is working space
 * for the heap. Temporarily saves the next item to workspace at the end of
 * list before returning it, to ensure a consistent heap and hash on return.
 *
 * Note that this space will be overwritten by the next enqueue call, not a
 * valid pointer for very long.
 */
void **cmi_hashheap_dequeue(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        /* Nothing to do */
        return NULL;
    }

    /*
     * Copy the event to working space at the end of the heap.
     * This is safe, since we allocated two slots more than the heap_size.
     */
    const uint64_t tmp = hp->heap_count + 1u;
    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;
    heap[tmp] = heap[1u];

    /* Mark it as deleted (a tombstone) in the hash map */
    uint64_t idx = heap[tmp].hash_index;
    hash[idx].heap_index = 0u;

    /* Reshuffle the heap */
    heap[1u] = heap[hp->heap_count];
    idx = hp->heap[1u].hash_index;
    hash[idx].heap_index = 1u;
    hp->heap_count--;
    if (hp->heap_count > 1u) {
        heap_down(hp, 1u);
    }

    /* Return a pointer to the start of the payload array */
    return heap[tmp].item;
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