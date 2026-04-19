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

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

#include "cmi_hashheap.h"
#include "cmi_memutils.h"

/*
 * cmi_hashheap_create - Allocate memory for a new hashheap struct.
 */
struct cmi_hashheap *cmi_hashheap_create(void)
{
    struct cmi_hashheap *hp = cmi_malloc(sizeof(*hp));
    cmi_memset(hp, 0u, sizeof(*hp));

    return hp;
}

/*
 * default_compare - Test if heap_tag *a should go before *b. If so, return true.
 * Ranking corresponds to the event queue order, where lower reactivation times
 * (rank_d64) go before higher, if equal, then higher priority (rank_i64) before
 * lower, and if that also is equal, FIFO order based on hash key value.
 */
static bool default_compare(const struct cmi_heap_tag *a,
                            const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    if (a->rank_d64 < b->rank_d64) {
        return true;
    }
    if (a->rank_d64 > b->rank_d64) {
        return false;
    }

    if (a->rank_i64 > b->rank_i64) {
        return true;
    }
    if (a->rank_i64 < b->rank_i64) {
        return false;
    }

    if (a->hash_key < b->hash_key) {
        return true;
    }

    return false;
}

/*
 * cmi_hashheap_initialize - Initialize hashheap for use.
 * Allocates a contiguous memory array aligned to an integer number of memory
 * pages for efficiency.
 */
void cmi_hashheap_initialize(struct cmi_hashheap *hp,
                             const uint16_t hexp,
                             cmi_heap_compare_func *cmp)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hp->heap == NULL);
    cmb_assert_release(hp->hash_map == NULL);
    cmb_assert_release(hexp > 0u);

    /* Initialize the powers-of-two growth parameters */
    hp->heap_exp_init = hexp;
    hp->heap_exp_cur = hexp;
    hp->heap_size = 1u << hp->heap_exp_cur;

    const size_t heap_bts = (hp->heap_size + 1u) * sizeof(struct cmi_heap_tag);
    const size_t hash_bts = (hp->heap_size << 1u) * sizeof(struct cmi_hash_tag);
    const size_t total_bts = heap_bts + hash_bts;

    const size_t page_bts = cmi_pagesize();
    cmb_assert_debug(page_bts > 0 && (page_bts & (page_bts - 1)) == 0);
    const size_t rndtot_bts = (total_bts + page_bts - 1u) & ~(page_bts - 1u);

    unsigned char *mem = cmi_aligned_alloc(page_bts, rndtot_bts);
    cmi_memset(mem, 0u, rndtot_bts);

    hp->heap = (struct cmi_heap_tag *)mem;
    hp->hash_map = (struct cmi_hash_tag *)(mem + heap_bts);

    hp->heap_count = 0u;
    hp->item_counter = 0u;
    hp->heap_compare = (cmp == NULL) ? default_compare : cmp;

    /* Lazy initialization of hashmap, only at first actual need for it */
    hp->map_active = false;
}

/*
 * cmi_hashheap_terminate - Deallocate the internal structures
 */
void cmi_hashheap_terminate(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    if (hp->heap != NULL) {
        cmi_aligned_free(hp->heap);
        hp->heap = NULL;
        hp->hash_map = NULL;
    }
}

/*
 * cmi_hashheap_destroy - Clean up, deallocating space.
 */
void cmi_hashheap_destroy(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    cmi_hashheap_terminate(hp);
    cmi_free(hp);
}

/*
 * cmi_hashheap_clear - Flush out the hashheap. Does not reset the item counter
 * for issuing new keys, does not free space, does not shrink the heap to its
 * initial size, just empties it.
 */
void cmi_hashheap_clear(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    if (hp->heap != NULL) {
        const size_t heap_bts = (hp->heap_size + 2u) * sizeof(struct cmi_heap_tag);
        const size_t hash_bts = (hp->heap_size << 1u) * sizeof(struct cmi_hash_tag);
        const size_t total_bts = heap_bts + hash_bts;
        cmi_memset(hp->heap, 0u, total_bts);

        hp->heap_count = 0u;
        hp->map_active = false;
    }
}

/*
 * cmi_hashheap_reset - Hard reset to newly initialized state
 */
void cmi_hashheap_reset(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hp->heap != NULL);

    const uint16_t hexp = hp->heap_exp_init;
    cmi_heap_compare_func *cmp = hp->heap_compare;

    cmi_hashheap_terminate(hp);
    cmi_hashheap_initialize(hp, hexp, cmp);
}


/*
 * hash_key - Fibonacci hash function.
 * The "magic number" is approx 2^64 / phi, the golden ratio.
 * The right shift maps to the hash map size, twice the heap size.
 */
static uint64_t hash_key(const struct cmi_hashheap *hp, const uint64_t key)
{
    cmb_assert_debug(hp != NULL);

    const uint64_t hash = (key * UINT64_C(11400714819323198485))
                            >> (64u - (hp->heap_exp_cur + 1));

    cmb_assert_debug(hash < (hp->heap_size << 1u));
    return hash;
}

/*
 * hash_find_slot - Find the first free hash map slot for the given hash_key.
 * Uses a bitmap to loop around efficiently.
 */
static uint64_t hash_find_slot(const struct cmi_hashheap *hp, const uint64_t key)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->hash_map != NULL);

    const struct cmi_hash_tag *hm = hp->hash_map;
    uint64_t hash = hash_key(hp, key);
    const uint64_t hash_size = hp->heap_size << 1u;
    const uint64_t bitmap = hash_size - 1u;
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
 * hash_init - Initializes hashmap from current heap for first use
 */
static void hash_init(const struct cmi_hashheap *hp)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->hash_map != NULL);

    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        struct cmi_heap_tag *htp = &(hp->heap[ui]);
        htp->hash_index = hash_find_slot(hp, htp->hash_key);
        const uint64_t hashidx = htp->hash_index;
        hp->hash_map[hashidx].hash_key = htp->hash_key;
        hp->hash_map[hashidx].heap_index = ui;
    }
}

/*
 * hash_rehash - Rehash old hash entries to a new hash map, removing tombstones.
 */
static void hash_rehash(const struct cmi_hashheap *hp,
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
        const uint64_t key = old_hash_map[ui].hash_key;
        if (key != 0u) {
            /* Something is here */
            const uint64_t heapidx = old_hash_map[ui].heap_index;
            if (heapidx != 0u) {
                /* It is not a tombstone */
                const uint64_t hashidx = hash_find_slot(hp, key);
                hash[hashidx].hash_key = key;
                hash[hashidx].heap_index = heapidx;
                heap[heapidx].hash_index = hashidx;
            }
        }
    }
}

/*
 * heap_up - Bubble a tag at index k upwards into its right place
 */
static void heap_up(const struct cmi_hashheap *hp, uint64_t k)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(k <= hp->heap_count);

    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;

    const struct cmi_heap_tag hole_tag = heap[k];
    cmi_heap_compare_func *compare = hp->heap_compare;
    const bool defcmp = (compare == default_compare);

    /* A binary tree, parent node at k / 2 */
    uint64_t l;
    while ((l = (k >> 1)) > 0) {
        /* Trading one more branch for one less redirect in the default case */
        const bool before = (defcmp) ? default_compare(&hole_tag, &(heap[l]))
                                     : (*compare)(&(hole_tag), &(heap[l]));
        if (before) {
            heap[k] = heap[l];
            if (hp->map_active) {
                const uint64_t khash = heap[k].hash_index;
                hash[khash].heap_index = k;
            }

            k = l;
        }
        else {
            break;
        }
    }

    /* Copy the candidate into its correct slot */
    heap[k] = hole_tag;
    if (hp->map_active) {
        const uint64_t khash = heap[k].hash_index;
        hash[khash].heap_index = k;
    }
}

/*
 * heap_down - Bubble a tag at index k downwards into its right place
 */
static void heap_down(const struct cmi_hashheap *hp, uint64_t k)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(k <= hp->heap_count);

    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;

    const struct cmi_heap_tag hole_tag = heap[k];
    cmi_heap_compare_func *compare = hp->heap_compare;
    const bool defcmp = (compare == default_compare);

    /* Binary heap, children at 2x and 2x + 1 */
    const uint64_t j = (hp->heap_count >> 1);
    bool before;
    while (k <= j) {
        uint64_t l = k << 1;
        const uint64_t r = l + 1;
        if (r <= hp->heap_count) {
            before = (defcmp) ? default_compare(&heap[r], &(heap[l]))
                              : (*compare)(&(heap[r]), &(heap[l]));
            if (before) {
                l = r;
            }
        }

        before = (defcmp) ? default_compare(&hole_tag, &(heap[l]))
                          : (*compare)(&(hole_tag), &(heap[l]));
        if (before) {
            break;
        }
        else {
            heap[k] = heap[l];
            if (hp->map_active) {
                const uint64_t khash = heap[k].hash_index;
                hash[khash].heap_index = k;
            }

            k = l;
        }
    }

    /* Copy the candidate into its correct slot */
    heap[k] = hole_tag;
    if (hp->map_active) {
        const uint64_t khash = heap[k].hash_index;
        hash[khash].heap_index = k;
    }
}

/*
 * hashheap_grow: doubling the available heap and hash map sizes.
 * The old heap is memcpy'd into its new location, each event at the same
 * index as before. The new hash map is initialized to all zeros, the old
 * hash map is memcpy'd together with the old heap into the area that now
 * belongs to the new heap. From there, valid hash entries are rehashed into
 * their new locations in the new hash map. This works since there is no
 * memory overlap between the copy of the old hash map and the new one.
 */
static void hashheap_grow(struct cmi_hashheap *hp)
{
cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->hash_map != NULL);

    const uint64_t old_heapsz = hp->heap_size;

    hp->heap_exp_cur++;
    hp->heap_size = 1u << hp->heap_exp_cur;

    const size_t heap_bts = (hp->heap_size + 1u) * sizeof(struct cmi_heap_tag);
    const size_t hash_bts = (hp->heap_size << 1u) * sizeof(struct cmi_hash_tag);
    const size_t total_bts = heap_bts + hash_bts;

    const size_t pagesz = cmi_pagesize();
    const size_t rndtot_bts = (total_bts + pagesz - 1u) & ~(pagesz - 1u);

    unsigned char *mem_new = cmi_aligned_alloc(pagesz, rndtot_bts);
    cmi_memset(mem_new, 0u, rndtot_bts);

    struct cmi_heap_tag *heap_new = (struct cmi_heap_tag *)mem_new;
    struct cmi_hash_tag *hash_new = (struct cmi_hash_tag *)(mem_new + heap_bts);

    const size_t old_heap_bts = (old_heapsz + 1u) * sizeof(struct cmi_heap_tag);
    cmi_memcpy(heap_new, hp->heap, old_heap_bts);

    /* Save old pointers */
    const struct cmi_hash_tag *hash_old = hp->hash_map;
    struct cmi_heap_tag *heap_old_block = hp->heap;
    hp->heap = heap_new;
    hp->hash_map = hash_new;

    if (hp->map_active) {
        /* Rehash into new hashmap */
        const uint64_t old_hashsz = old_heapsz << 1u;
        hash_rehash(hp, hash_old, old_hashsz);
    }

    cmi_aligned_free(heap_old_block);
}


/*
 * cmi_hashheap_enqueue - Insert item in queue, return unique event hash_key.
 * Resizes hashheap if necessary.
 */
uint64_t cmi_hashheap_enqueue(struct cmi_hashheap *hp,
                              void *pl1,
                              void *pl2,
                              void *pl3,
                              void *pl4,
                              uint64_t hashkey,
                              const double rank_d64,
                              const int64_t rank_i64)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->hash_map != NULL);
    cmb_assert_release(hp->heap_count <= hp->heap_size);

    /* Do we have space? */
    if (hp->heap_count == hp->heap_size) {
       hashheap_grow(hp);
    }

    /* Now we have, put the new entry at the end */
    cmb_assert_debug(hp->heap_count < hp->heap_size);
    const uint64_t hc = ++hp->heap_count;
    hp->item_counter += 1u;
    if (hashkey == 0u) {
        hashkey = hp->item_counter;
    }

    struct cmi_heap_tag *heap = hp->heap;
    struct cmi_hash_tag *hash = hp->hash_map;

    heap[hc].hash_key = hashkey;
    heap[hc].rank_d64 = rank_d64;
    heap[hc].rank_i64 = rank_i64;
    heap[hc].item[0] = pl1;
    heap[hc].item[1] = pl2;
    heap[hc].item[2] = pl3;
    heap[hc].item[3] = pl4;

    if (hp->map_active) {
        const uint64_t idx = hash_find_slot(hp, hashkey);
        hash[idx].hash_key = hashkey;
        hash[idx].heap_index = hc;
        heap[hc].hash_index = idx;
    }

    /* Shuffle it up into its right place */
    heap_up(hp, hc);

    cmb_assert_debug(hashkey > 0u);
    return hashkey;
}

/*
 * cmi_hashheap_dequeue - Remove and return the next item.
 * Temporarily saves the event to location 0 of the heap. Note that this space
 * will be overwritten by the next call to dequeue. It is not a valid pointer
 * for very long.
 */
void **cmi_hashheap_dequeue(struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    const uint64_t heapcnt = hp->heap_count;
    if ((hp->heap == NULL) || (heapcnt == 0u)) {
        /* Nothing to do */
        return NULL;
    }

    /* Copy the event to the working space at index 0. */
    struct cmi_heap_tag *heap = hp->heap;
    heap[0u] = heap[1u];

    if (hp->map_active) {
        /* Mark it as deleted (a tombstone) in the hash map */
        const uint64_t idx = heap[0u].hash_index;
        hp->hash_map[idx].heap_index = 0u;
    }

    /* Reshuffle the heap */
    if (heapcnt > 1u) {
        heap[1u] = heap[heapcnt];
        if (hp->map_active) {
            const uint64_t idx = hp->heap[1u].hash_index;
            hp->hash_map[idx].heap_index = 1u;
        }

        hp->heap_count = heapcnt - 1u;
        if (hp->heap_count > 1u) {
            heap_down(hp, 1u);
        }
    }
    else {
        hp->heap_count = 0u;
    }

    return heap[0u].item;
}

/*
 * cmi_hashheap_remove - Remove the given entry and reshuffle the heap
 */
bool cmi_hashheap_remove(struct cmi_hashheap *hp, const uint64_t hashkey)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hashkey != 0u);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
         return false;
    }

    if (!hp->map_active) {
        /* Lazy activation, now's the time */
        hash_init(hp);
        hp->map_active = true;
    }

    const uint64_t heapidx = cmi_hash_find_index(hp, hashkey);
    if (heapidx == 0u) {
        return false;
    }

    /* Lazy hashmap deletion, tombstone it */
    cmb_assert_debug(hp->heap[heapidx].hash_key == hashkey);
    uint64_t hashidx = hp->heap[heapidx].hash_index;
    hp->hash_map[hashidx].heap_index = 0u;

    /* Remove entry from heap */
    if (heapidx == hp->heap_count) {
        hp->heap_count--;
    }
    else {
        /* Move the last item into the now empty slot, reshuffle */
        const uint64_t heapcnt = hp->heap_count;
        const struct cmi_heap_tag *a = &(hp->heap[heapidx]);
        const struct cmi_heap_tag *b = &(hp->heap[heapcnt]);
        const bool defcmp = (hp->heap_compare == default_compare);
        const bool move_down = (defcmp) ? default_compare(a, b)
                                     : (*hp->heap_compare)(a, b);
        hp->heap[heapidx] = hp->heap[heapcnt];
        hashidx = hp->heap[heapidx].hash_index;
        hp->hash_map[hashidx].heap_index = heapidx;
        hp->heap_count--;
        if (move_down) {
            heap_down(hp, heapidx);
        }
        else {
            heap_up(hp, heapidx);
        }
    }

    return true;
}

/*
 * hash_find_index - Find the heap index of a given hashkey, zero if not found.
 * Uses a bitmap with all ones in the first positions to wrap around fast,
 * instead of using the modulo operator. In effect, simulates overflow in an
 * unsigned integer of (heap_exp_cur + 1) bits.
 */
uint64_t cmi_hash_find_index(struct cmi_hashheap *hp, const uint64_t hashkey)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(hp->hash_map != NULL);

    if (!hp->map_active) {
        /* Initialize hashmap */
        hash_init(hp);
        hp->map_active = true;
    }

    const uint64_t hash_size = hp->heap_size << 1u;
    const uint64_t bitmap = hash_size - 1u;
    const struct cmi_hash_tag *hm = hp->hash_map;
    uint64_t hash = hash_key(hp, hashkey);
    const uint64_t hash_start = hash;
    for (;;) {
        if (hm[hash].hash_key == hashkey) {
            /* Found, return the heap index (possibly a tombstone zero) */
            return hm[hash].heap_index;
        }

        /* If we reached a never-used slot, the hashkey is not in the hash map */
        if (hm[hash].hash_key == 0u) {
            return 0u;
        }

        /* Not in slot, use linear probing, try next, possibly looping around */
        hash = (hash + 1u) & bitmap;

        /* If we've wrapped around to where we started, the hashkey is not here */
        if (hash == hash_start) {
            return 0u;
        }
    }
}

/*
 * cmi_hashheap_item - Return a pointer to the current location of the item
 */
void **cmi_hashheap_item(struct cmi_hashheap *hp, const uint64_t hashkey)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hashkey != 0u);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);

    const uint64_t idx = cmi_hash_find_index(hp, hashkey);
    cmb_assert_release(idx != 0u);

    return hp->heap[idx].item;
}

/*
 * cmi_hashheap_drank - Get the rank_d64 for the given item.
 */
double cmi_hashheap_drank(struct cmi_hashheap *hp,
                          const uint64_t hashkey)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hashkey != 0u);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);

    const uint64_t idx = cmi_hash_find_index(hp, hashkey);
    cmb_assert_release(idx != 0u);

    return hp->heap[idx].rank_d64;
}

/*
 * cmi_hashheap_irank - Get the rank_i64 for the given item.
 */
int64_t cmi_hashheap_irank(struct cmi_hashheap *hp,
                           const uint64_t hashkey)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hashkey != 0u);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);

    const uint64_t idx = cmi_hash_find_index(hp, hashkey);
    cmb_assert_release(idx != 0u);

    return hp->heap[idx].rank_i64;

}

/*
 * cmi_hashheap_reprioritize: Changes one or more of the prioritization keys
 * and reshuffles the heap. Turns on the hashmap if not already active.
 */
void cmi_hashheap_reprioritize(struct cmi_hashheap *hp,
                               const uint64_t hashkey,
                               const double drank,
                               const int64_t irank)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(hashkey != 0u);
    cmb_assert_debug(hp->heap != NULL);
    cmb_assert_debug(hp->heap_count != 0u);
    cmb_assert_debug(hp->heap_compare != NULL);

    if (!hp->map_active) {
        hash_init(hp);
        hp->map_active = true;
    }

    const uint64_t idx = cmi_hash_find_index(hp, hashkey);
    cmb_assert_release(idx != 0u);

    const struct cmi_heap_tag old_tagval = hp->heap[idx];
    hp->heap[idx].rank_d64 = drank;
    hp->heap[idx].rank_i64 = irank;

    const bool defcmp = (hp->heap_compare == default_compare);
    const bool move_down = (defcmp) ? default_compare(&old_tagval, &(hp->heap[idx]))
                                    : (*hp->heap_compare)(&old_tagval, &(hp->heap[idx]));
    if (move_down) {
        heap_down(hp, idx);
    }
    else {
        heap_up(hp, idx);
    }
}

/*
 * item_match - Wildcard search helper function to get the condition
 * out of the next three functions.
 */
static bool item_match(const struct cmi_heap_tag *htp,
                       const void *val1,
                       const void *val2,
                       const void *val3,
                       const void *val4)
{
    cmb_assert_debug(htp != NULL);

    bool ret = true;
    if ( ((val1 != htp->item[0]) && (val1 != CMI_ANY_ITEM))
      || ((val2 != htp->item[1]) && (val2 != CMI_ANY_ITEM))
      || ((val3 != htp->item[2]) && (val3 != CMI_ANY_ITEM))
      || ((val4 != htp->item[3]) && (val4 != CMI_ANY_ITEM))) {
        ret = false;
    }

    return ret;
}

/*
 * cmi_hashheap_find - Locate a specific event, using CMB_ANY_ITEM as a
 * wildcard in the respective positions. Returns the hash_key of the item, or
 * zero if none is found. Simple linear search from the start of the heap.
 */
uint64_t cmi_hashheap_pattern_find(const struct cmi_hashheap *hp,
                                   const void *val1,
                                   const void *val2,
                                   const void *val3,
                                   const void *val4)
{
    cmb_assert_debug(hp != NULL);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        return 0u;
    }

    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        if (item_match(htp, val1, val2, val3, val4)) {
            return hp->heap[ui].hash_key;
        }
    }

    /* Not found */
    return 0u;
}

/*
 * cmi_hashheap_pattern_count - Count matching items using CMB_ANY_ITEM as a
 * wildcard. Returns the number of matching items, possibly zero.
 */
uint64_t cmi_hashheap_pattern_count(const struct cmi_hashheap *hp,
                                    const void *val1,
                                    const void *val2,
                                    const void *val3,
                                    const void *val4)
{
    cmb_assert_debug(hp != NULL);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        return 0u;
    }

    uint64_t cnt = 0u;
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        if (item_match(htp, val1, val2, val3, val4)) {
            cnt++;
        }
    }

    return cnt;
}

/*
 * cmi_hashheap_pattern_cancel - Cancel all matching items.
 * Two-pass approach: Allocate temporary storage for the list of
 * matching keys in the first pass, then cancel these in the
 * second pass. Avoids any possible issues caused by modification
 * (reshuffling) of the heap while iterating over it.
 * Returns the number of items canceled, possibly zero.
 */
uint64_t cmi_hashheap_pattern_cancel(struct cmi_hashheap *hp,
                                     const void *val1,
                                     const void *val2,
                                     const void *val3,
                                     const void *val4)
{
    cmb_assert_debug(hp != NULL);

    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        return 0u;
    }

    /* Allocate space enough to match everything in the heap */
    uint64_t *tmp = cmi_malloc(hp->heap_count * sizeof(*tmp));

    /* First pass, recording the matches */
    uint64_t cnt = 0u;

    for (uint64_t ui = 1; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        if (item_match(htp, val1, val2, val3, val4)) {
            /* Matched, note it on the list */
            tmp[cnt++] = hp->heap[ui].hash_key;
        }
    }

    /*
     * Second pass, remove the matching events, never mind the
     * heap reshuffling underneath us for each cancellation.
     */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        cmi_hashheap_remove(hp, tmp[ui]);
    }

    cmi_free(tmp);
    return cnt;
}

/*
 * cmi_hashheap_print - Print content of event heap, useful for debugging
 */
void cmi_hashheap_print(const struct cmi_hashheap *hp, FILE *fp)
{
    cmb_assert_debug(hp != NULL);

    fprintf(fp, "---------------------------------- Hash heap -----------------------------------\n");
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        fprintf(fp, "%" PRIu64 ": hash_key %" PRIu64 " %#8.4g %" PRIi64 " : hash idx %" PRIu64 " : %p %p %p %p\n",
                ui,
                htp->hash_key,
                htp->rank_d64,
                htp->rank_i64,
                htp->hash_index,
                htp->item[0],
                htp->item[1],
                htp->item[2],
                htp->item[3]);
    }

    fprintf(fp, "--------------------------------------------------------------------------------\n");
    fflush(fp);
}
