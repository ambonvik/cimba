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

    const struct cmi_hash_tag * restrict hm = hp->hash_map;
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

/* heap_up : Bubble a tag at index k upwards into its right place */
static void heap_up(const struct cmi_hashheap *hp, uint64_t k)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(k <= hp->heap_count);

    /* Place a working copy at index 0 */
    struct cmi_heap_tag * restrict heap = hp->heap;
    struct cmi_hash_tag * restrict hash = hp->hash_map;
    cmi_heap_compare_func * const compare = hp->heap_compare;
    heap[0] = heap[k];

    /* A binary tree, parent node at k / 2 */
    uint64_t l;
    while ((l = (k >> 1)) > 0) {
        if ((*compare)(&(heap[0]), &(heap[l]))) {
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
static void heap_down(const struct cmi_hashheap *hp, uint64_t k)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(k <= hp->heap_count);

    /* Place a working copy at index 0 */
    struct cmi_heap_tag * restrict heap = hp->heap;
    struct cmi_hash_tag * restrict hash = hp->hash_map;
    cmi_heap_compare_func * const compare = hp->heap_compare;
    heap[0] = heap[k];

    /* Binary heap, children at 2x and 2x + 1 */
    uint64_t j = (hp->heap_count >> 1);
    while (k <= j) {
        uint64_t l = k << 1;
        const uint64_t r = l + 1;
        if (r <= hp->heap_count && (*compare)(&(heap[r]), &(heap[l]))) {
            l = r;
        }

        if ((*compare)(&(heap[0]), &(heap[l]))) {
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
                                     const double dkey,
                                     const int64_t ikey,
                                     const uint64_t ukey)
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
    struct cmi_heap_tag * restrict heap = hp->heap;
    struct cmi_hash_tag * restrict hash = hp->hash_map;

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

    const uint64_t heapcnt = hp->heap_count;
    if ((hp->heap == NULL) || (heapcnt == 0u)) {
        /* Nothing to do */
        return NULL;
    }

    /* Copy the event to working space at the end of the heap. */
    const uint64_t tmp = heapcnt + 1u;
    struct cmi_heap_tag * restrict heap = hp->heap;
    struct cmi_hash_tag * restrict hash = hp->hash_map;
    heap[tmp] = heap[1u];

    /* Mark it as deleted (a tombstone) in the hash map */
    uint64_t idx = heap[tmp].hash_index;
    hash[idx].heap_index = 0u;

    /* Reshuffle the heap */
    if (heapcnt > 1u) {
        heap[1u] = heap[heapcnt];
        idx = hp->heap[1u].hash_index;
        hash[idx].heap_index = 1u;
        hp->heap_count = heapcnt - 1u;
        if (hp->heap_count > 1u) {
            heap_down(hp, 1u);
        }
    }
    else {
        hp->heap_count = 0u;
    }

    /* Return a pointer to the start of the payload array */
    return heap[tmp].item;
}

/*
 * cmi_hashheap_peek : Returns a pointer to the location of the item currently
 * at the top of the priority queue, without removing it.
 */
void **cmi_hashheap_peek(const struct cmi_hashheap *hp) {
    cmb_assert_release(hp != NULL);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        /* Nothing to do */
        return NULL;
    }

    struct cmi_heap_tag *first = &(hp->heap[1]);
    void **item = first->item;

    return item;
}

/*
 * cmi_hashheap_cancel : Cancel the given event and reshuffle heap
 */
bool cmi_hashheap_cancel(struct cmi_hashheap *hp, const uint64_t handle)
{
    cmb_assert_release(hp != NULL);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
         return false;
    }

    const uint64_t heapidx = hash_find_handle(hp, handle);
    if (heapidx == 0u) {
        return false;
    }
    else {
        cmb_assert_debug(hp->heap[heapidx].handle == handle);

        /* Lazy deletion, tombstone it */
        uint64_t hashidx = hp->heap[heapidx].hash_index;
        hp->hash_map[hashidx].heap_index = 0u;

        /* Remove event from heap position heapidx */
        if (heapidx == hp->heap_count) {
            hp->heap_count--;
        }
        else {
            const struct cmi_heap_tag *a = &(hp->heap[heapidx]);
            const uint64_t heapcnt = hp->heap_count;
            const struct cmi_heap_tag *b = &(hp->heap[heapcnt]);
            if ((*hp->heap_compare)(a, b)) {
                hp->heap[heapidx] = hp->heap[heapcnt];
                hashidx = hp->heap[heapidx].hash_index;
                hp->hash_map[hashidx].heap_index = heapidx;
                hp->heap_count--;
                heap_down(hp, heapidx);
            }
            else {
                hp->heap[heapidx] = hp->heap[heapcnt];
                hashidx = hp->heap[heapidx].hash_index;
                hp->hash_map[hashidx].heap_index = heapidx;
                hp->heap_count--;
                heap_up(hp, heapidx);
            }
        }

        return true;
    }
}

/*
 * cmi_hashheap_is_enqueued : Is the given item currently in the queue?
 */
bool cmi_hashheap_is_enqueued(struct cmi_hashheap *hp, uint64_t handle)
{
    cmb_assert_release(hp != NULL);

    bool ret;
    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        ret = false;
    }
    else {
        ret = (hash_find_handle(hp, handle) != 0u);
    }

     return ret;
}

/*
 * cmi_hashheap_get_dkey/ikey/ukey : Get the dkey/ikey/ukey for the given item.
 */
double cmi_hashheap_get_dkey(const struct cmi_hashheap *hp,
                             const uint64_t handle)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);

    const uint64_t idx = hash_find_handle(hp, handle);
    cmb_assert_release(idx != 0u);

    return hp->heap[idx].dkey;
}

int64_t cmi_hashheap_get_ikey(const struct cmi_hashheap *hp,
                              const uint64_t handle)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);

    const uint64_t idx = hash_find_handle(hp, handle);
    cmb_assert_release(idx != 0u);

    return hp->heap[idx].ikey;

}

uint64_t cmi_hashheap_get_ukey(const struct cmi_hashheap *hp,
                               const uint64_t handle)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);

    const uint64_t idx = hash_find_handle(hp, handle);
    cmb_assert_release(idx != 0u);

    return hp->heap[idx].ukey;

}

/*
 * cmi_hashheap_reprioritize: Changes one or more of the prioritization keys
 * and reshuffles the heap.
 * Precondition: The event is in the event queue.
 */
void cmi_hashheap_reprioritize(const struct cmi_hashheap *hp,
                               const uint64_t handle,
                               const double dkey,
                               const int64_t ikey,
                               const uint64_t ukey) {
    cmb_assert_release(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);
    cmb_assert_debug(hp->heap_compare != NULL);

    const uint64_t idx = hash_find_handle(hp, handle);
    cmb_assert_release(idx != 0u);

    /* Save a copy of the old values */
    hp->heap[0] = hp->heap[idx];

    hp->heap[idx].dkey = dkey;
    hp->heap[idx].ikey = ikey;
    hp->heap[idx].ukey = ukey;

    if ((*hp->heap_compare)(&(hp->heap[0]), &(hp->heap[idx]))) {
        /* The old values should go before the new ones, item heading down */
        heap_down(hp, idx);
    }
    else {
        /* Other way around, item should rise in the heap */
        heap_up(hp, idx);
    }
}

/*
 * item_match : Wildcard search helper function to get the condition
 * out of the next three functions.
 */
static bool item_match(const struct cmi_heap_tag *htp,
                       const void *val1,
                       const void *val2,
                       const void *val3)
{
    cmb_assert_debug(htp != NULL);

    bool ret = true;
    if ( ((val1 != htp->item[0]) && (val1 != CMI_ANY_ITEM))
      || ((val2 != htp->item[1]) && (val2 != CMI_ANY_ITEM))
      || ((val3 != htp->item[2]) && (val3 != CMI_ANY_ITEM))) {
        ret = false;
    }

    return ret;
}

/*
 * cmi_hashheap_find : Locate a specific event, using the CMB_ANY_* constants as
 * wildcards in the respective positions. Returns the handle of the event, or
 * zero if none found. Simple linear search from the start of the heap.
 */
uint64_t cmi_hashheap_find(const struct cmi_hashheap *hp,
                           const void *val1,
                           const void *val2,
                           const void *val3)
{
    cmb_assert_debug(hp != NULL);

    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        if (item_match(htp, val1, val2, val3)) {
            return htp->handle;
        }
    }

    /* Not found */
    return 0u;
}

/*
 * cmi_hashheap_count : Count matching items using CMB_ANY_ITEM as wildcard.
 * Returns the number of matching items, possibly zero.
 */
uint64_t cmi_hashheap_count(const struct cmi_hashheap *hp,
                            const void *val1,
                            const void *val2,
                            const void *val3)
{
    cmb_assert_debug(hp != NULL);

    uint64_t cnt = 0u;
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        if (item_match(htp, val1, val2, val3)) {
            cnt++;
        }
    }

    return cnt;
}

/*
 * cmi_hashheap_cancel_all : Cancel all matching items.
 * Two-pass approach: Allocate temporary storage for the list of
 * matching handles in the first pass, then cancel these in the
 * second pass. Avoids any possible issues caused by modification
 * (reshuffling) of the heap while iterating over it.
 * Returns the number of items cancelled, possibly zero.
 */
uint64_t cmi_hashheap_cancel_all(struct cmi_hashheap *hp,
                              const void *val1,
                              const void *val2,
                              const void *val3)
{
    cmb_assert_debug(hp != NULL);

    uint64_t cnt = 0u;
    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        return cnt;
    }

    /* Allocate space enough to match everything in the heap */
    uint64_t *tmp = cmi_malloc(hp->heap_count * sizeof(*tmp));

    /* First pass, recording the matches */
    for (uint64_t ui = 1; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        if (item_match(htp, val1, val2, val3)) {
            /* Matched, note it on the list */
            tmp[cnt++] = htp->handle;
        }
    }

    /* Second pass, cancel the matching events, never mind the
     * heap reshuffling underneath us for each cancel.
     */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        cmi_hashheap_cancel(hp, tmp[ui]);
    }

    cmi_free(tmp);
    return cnt;
}

/*
 * cmi_hashheap_print : Print content of event heap, useful for debugging
 */
void cmi_hashheap_print(const struct cmi_hashheap *hp, FILE *fp)
{
    cmb_assert_debug(hp != NULL);

    fprintf(fp, "----------------- Hash heap -----------------\n");
    fprintf(fp, "Heap section:\n");
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        fprintf(fp, "heap index %llu: handle %llu dkey %#8.4g ikey %lld ukey %llu : hash %llu : %p  %p  %p\n",
                ui,
                htp->handle,
                htp->dkey,
                htp->ikey,
                htp->ukey,
                htp->hash_index,
                htp->item[0],
                htp->item[1],
                htp->item[2]);
    }

    fprintf(fp, "\nHash map section:\n");
    for (uint64_t ui = 0u; ui < hp->hash_size; ui++) {
        const struct cmi_hash_tag *htp = &(hp->hash_map[ui]);
        fprintf(fp, "hash index %llu: handle %llu heap %llu\n", ui,
                htp->handle,
                htp->heap_index);
    }

    fprintf(fp, "---------------------------------------------\n");
}