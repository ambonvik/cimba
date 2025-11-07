/*
 * cmb_store.c - a counting semaphore that supports acquire, release, and
 * preempt in specific amounts against a fixed resource capacity, where a
 * process also can acquire more of a resource it already holds some amount
 * of, or release parts of its holding. Several processes can be holding parts
 * of the resource capacity at the same time, possibly also different amounts.
 *
 * The cmb_store adds numeric values for capacity and usage to the base
 * resource. These values are unsigned integers to avoid any rounding issues
 * from floating-point calculations, both faster and higher resolution (if
 * scaled properly to 64-bit range).
 *
 * It assigns amounts to processes in a greedy fashion, where the acquiring
 * process will first grab whatever amount is available, then wait for some more
 * to become available, and repeat until the requested amount is acquired.
 *
 * Preempt is similar to acquire, except that the prempting process also will
 * grab resources from any lower-priority processes holding some.
 *
 * The holders list is now a hashheap, since we may need to handle many separate
 * processes acquiring, holding, releasing, and preempting various amounts of
 * the resource capacity. The hashheap is sorted to keep the holder most likely
 * to be preempted at the front, i.e. lowest priority and last in.
 *
 * Copyright (c) Asbjørn M. Bonvik 2025.
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

#include <stdio.h>

#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_store.h"

#include "cmi_memutils.h"


/*
 * cmb_store_create : Allocate memory for a store object.
 */
struct cmb_store *cmb_store_create(void)
{
    struct cmb_store *sp = cmi_malloc(sizeof(*sp));
    cmi_memset(sp, 0, sizeof(*sp));

    return sp;
}

/*
 * holder_queue_check : Test if heap_tag *a should go before *b. If so, return
 * true. Ranking lower priority (dkey) before higher, then LIFO based on handle
 * value. Used to identify most likely victim for a resource preemption, hence
 * opposite order of the waiting room.
 */
static bool holder_queue_check(const struct cmi_heap_tag *a,
                               const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->ikey < b->ikey) {
        ret = true;
    }
    else if (a->ikey == b->ikey) {
        if (a->handle > b->handle) {
            ret = true;
        }
    }

    return ret;
}

/*
 * store_scram : forcibly eject a holder process without resuming it.
 */
void store_scram(struct cmi_resourcebase *rbp,
                 const struct cmb_process *pp,
                 const uint64_t handle)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmb_assert_release(handle != 0u);

    struct cmb_store *sp = (struct cmb_store *)rbp;
    struct cmi_hashheap *hp = &(sp->holders);
    void **item = cmi_hashheap_get_item(hp, handle);
    cmb_assert_debug(item[0] == pp);

    const uint64_t held = (uint64_t)item[1];
    cmb_assert_debug(held > 0u);
    cmb_assert_debug(held <= sp->in_use);

    const bool ret = cmi_hashheap_cancel(hp, handle);
    cmb_assert_debug(ret == true);

    sp->in_use -= held;
    cmb_assert_debug(sp->in_use < sp->capacity);
    cmi_resourceguard_signal(&(sp->front_guard));
}

/*
 * storee_reprio : dummy reprio function. Does nothing.
 */
void store_reprio(struct cmi_resourcebase *rbp,
                  const uint64_t handle,
                  const int64_t pri)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(handle != 0u);

    struct cmb_store *sp = (struct cmb_store *)rbp;
    struct cmi_hashheap *hp = &(sp->holders);
    const double dkey = cmi_hashheap_get_dkey(hp, handle);
    cmi_hashheap_reprioritize(hp, handle, dkey, pri);
}

/*
 * cmb_store_initialize : Make an allocated store object ready for use.
 */
#define HOLDERS_INIT_EXP 3u

void cmb_store_initialize(struct cmb_store *sp,
                          const char *name,
                          const uint64_t capacity)
{
    cmb_assert_release(sp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_resourcebase_initialize(&(sp->core), name);
    sp->core.scram = store_scram;
    sp->core.reprio = store_reprio;

    cmi_resourceguard_initialize(&(sp->front_guard), &(sp->core));
    cmi_hashheap_initialize(&(sp->holders),
                            HOLDERS_INIT_EXP,
                            holder_queue_check);

    sp->capacity = capacity;
    sp->in_use = 0u;
}

/*
 * cmb_store_terminate : Un-initializes a store object.
 */
void cmb_store_terminate(struct cmb_store *sp)
{
    cmb_assert_release(sp != NULL);

    cmi_hashheap_terminate(&(sp->holders));
    cmi_resourceguard_terminate(&(sp->front_guard));
    cmi_resourcebase_terminate(&(sp->core));
}

/*
 * cmb_store_destroy : Deallocates memory for a store object.
 */
void cmb_store_destroy(struct cmb_store *sp)
{
    cmb_assert_release(sp != NULL);

    cmb_store_terminate(sp);
    cmi_free(sp);
}

/*
 * store_available : pre-packaged demand function for a cmb_store, allowing the
 * requesting process to grab some whenever there is something to grab,
 */
static bool store_available(const struct cmi_resourcebase *rbp,
                            const struct cmb_process *pp,
                            const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_store *sp = (struct cmb_store *)rbp;
    const uint64_t avail = sp->capacity - sp->in_use;

    return (avail > 0u);
}

static void store_add_to_holder(const struct cmi_hashheap *hp,
                                const uint64_t holder_handle,
                                const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(holder_handle != 0u);
    cmb_assert_release(amount > 0u);

    void **item = cmi_hashheap_get_item(hp, holder_handle);
    const uint64_t cur_amt = (uint64_t)item[1];
    const uint64_t new_amt = cur_amt + amount;
    item[1] = (void *)new_amt;
}

static uint64_t store_reset_holder(const struct cmi_hashheap *hp,
                                   const uint64_t holder_handle,
                                   const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(holder_handle != 0u);
    cmb_assert_release(amount > 0u);

    void **item = cmi_hashheap_get_item(hp, holder_handle);
    const uint64_t old_amt = (uint64_t)item[1];
    cmb_assert_debug(old_amt >= amount);

   item[1] = (void *)amount;

    const uint64_t surplus = old_amt - amount;
    cmb_assert_debug(surplus <= old_amt);

    return surplus;
}

uint64_t cmb_store_held_by_process(struct cmb_store *sp,
                                   struct cmb_process *pp)
{
    cmb_assert_release(sp != NULL);
    cmb_assert_release(pp != NULL);

    uint64_t ret = 0u;
    struct cmi_resourcetag **rtloc = &(pp->resources_listhead);
    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)sp;
    const uint64_t handle = cmi_resourcetag_list_find_handle(rtloc, rbp);
    if (handle != 0u) {
        const struct cmi_hashheap *hp = &(sp->holders);
        void **item = cmi_hashheap_get_item(hp, handle);
        ret = (uint64_t)item[1];
    }

    return ret;
}

static uint64_t store_add_new_holder(struct cmi_hashheap *hp,
                                     const struct cmb_process *pp,
                                     const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(amount > 0u);

    const uint64_t new_handle = cmi_hashheap_enqueue(hp,
                                                    (void *)pp,
                                                    (void *)amount,
                                                    NULL,
                                                    NULL,
                                                    cmb_time(),
                                                    pp->priority);
    cmb_assert_debug(new_handle != 0u);

    return new_handle;
}

static uint64_t store_sum_holder_items(const struct cmi_hashheap *hp,
                                       const uint16_t idx)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(idx < 4);

    uint64_t sum = 0u;
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        sum += (uint64_t)(htp->item[idx]);
    }

    return sum;
}

static uint64_t store_update_record(struct cmb_store *store,
                                    struct cmi_hashheap *store_holders,
                                    struct cmb_process *caller,
                                    struct cmi_resourcetag **caller_rtloc,
                                    uint64_t caller_handle,
                                    const uint64_t amount)
{
    cmb_assert_release(store != NULL);
    cmb_assert_release(caller != NULL);
    cmb_assert_release(amount > 0u);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)store;

    if (caller_handle != 0u) {
        /* Must hold some already, add to existing entry */
        store_add_to_holder(store_holders, caller_handle, amount);
    }
    else {
        /* Not held already, create a new entry and cross-reference */
        caller_handle = store_add_new_holder(store_holders, caller, amount);
        cmi_resourcetag_list_add(caller_rtloc, rbp, caller_handle);
    }

    return caller_handle;
}

/*
 * cmb_store_acquire_inner : Acquire, perhaps preempt, and if necessary wait for
 * an claim_amount of the store resource. The calling process may already hold
 * some and try to increase its holding with this call, or obtain its first
 * helping.
 */
int64_t cmi_store_acquire_inner(struct cmb_store *sp,
                                const uint64_t claim_amount,
                                const bool preempt)
{
    cmb_assert_release(sp != NULL);
    cmb_assert_release(claim_amount > 0u);
    cmb_assert_debug(sp->in_use <= sp->capacity);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)sp;
    struct cmb_process *caller = cmb_process_get_current();

    /* Does the caller already hold some? */
    uint64_t initially_held = 0u;
    struct cmi_hashheap *hp = &(sp->holders);
    struct cmi_resourcetag **caller_rtloc = &(caller->resources_listhead);
    uint64_t caller_handle = cmi_resourcetag_list_find_handle(caller_rtloc,
                                                              rbp);
    if (caller_handle != 0u) {
        /* It does, note the amount in case we need to roll back to here */
        void **item = cmi_hashheap_get_item(hp, caller_handle);
        initially_held = (uint64_t)item[1];
    }

    cmb_logger_info(stdout, "Has %lld, requests %lld more from %s",
                    initially_held, claim_amount, rbp->name);

    /*
     * Greedy approach, first grab what is available, then preempt from lower
     * priority processes (if allowed), finally wait in line for the remaining
     * amount if any.
     */
    uint64_t rem_claim = claim_amount;
    while (true) {
        const uint64_t available = sp->capacity - sp->in_use;

        /* First take anything that is available already */
        if (available >= rem_claim) {
            /* Grab what we need */
            sp->in_use += rem_claim;
            cmb_assert_debug(sp->in_use <= sp->capacity);
            (void)store_update_record(sp,
                                      hp,
                                      caller,
                                      caller_rtloc,
                                      caller_handle,
                                      rem_claim);

            cmb_assert_debug(sp->in_use <= sp->capacity);
            cmb_assert_debug(store_sum_holder_items(hp, 1) == sp->in_use);
            cmb_logger_info(stdout,
                            "Success, %llu was already available",
                            rem_claim);

            /* In case someone else can use the leftovers */
            cmi_resourceguard_signal(&(sp->front_guard));

            return CMB_PROCESS_SUCCESS;
        }
        else if (available > 0u) {
            /* Grab what is there */
            sp->in_use += available;
            cmb_assert_debug(sp->in_use <= sp->capacity);
            rem_claim -= available;
            caller_handle = store_update_record(sp,
                                                hp,
                                                caller,
                                                caller_rtloc,
                                                caller_handle,
                                                available);

            cmb_assert_debug(sp->in_use <= sp->capacity);
            cmb_assert_debug(store_sum_holder_items(hp, 1) == sp->in_use);
            cmb_logger_info(stdout,
                            "Found %llu available, still wants %llu",
                            available,
                            rem_claim);
        }

        /* We have taken what was available, and we still want more */
        cmb_assert_debug(rem_claim > 0u);
        if (preempt) {
            /* Look for victims to mug for more */
            while (!cmi_hashheap_is_empty(hp)
                   && cmi_hashheap_peek_ikey(hp) < caller->priority) {
                /* There is one, pull it off the holders' hashheap */
                void **item = cmi_hashheap_dequeue(hp);
                struct cmb_process *victim = (struct cmb_process *)item[0];
                const uint64_t loot = (uint64_t) item[1];
                cmb_assert_debug(loot > 0u);
                cmb_assert_debug(loot <= sp->in_use);

                /* Remove the resource from victim's resource list */
                struct cmi_resourcetag **victim_rtloc
                                            = &(victim->resources_listhead);
                const bool found = cmi_resourcetag_list_remove(victim_rtloc,
                                                               rbp);
                cmb_assert_debug(found == true);

                /* Schedule a wakeup for it, but do not switch context yet */
                cmb_process_interrupt(victim,
                                      CMB_PROCESS_PREEMPTED,
                                      victim->priority);

                 /* Split the loot */
                if (loot < rem_claim) {
                    /* Take it all. */
                    caller_handle = store_update_record(sp,
                                                        hp,
                                                        caller,
                                                        caller_rtloc,
                                                        caller_handle,
                                                        loot);
                    rem_claim -= loot;

                    /* The quantity in use is unchanged, just changes hands. */
                    cmb_assert_debug(sp->in_use <= sp->capacity);
                    cmb_assert_debug(store_sum_holder_items(hp, 1)
                                     == sp->in_use);
                    cmb_logger_info(stdout,
                                    "Got %llu from %s, still needs %llu",
                                    loot,
                                    victim->name,
                                    rem_claim);
                }
                else {
                    /* Take what we need, put back the rest */
                    (void)store_update_record(sp,
                                              hp,
                                              caller,
                                              caller_rtloc,
                                              caller_handle,
                                              rem_claim);
                    const uint64_t surplus = loot - rem_claim;
                    sp->in_use -= surplus;

                    cmb_assert_debug(sp->in_use <= sp->capacity);
                    cmb_assert_debug(store_sum_holder_items(hp, 1)
                                     == sp->in_use);
                    cmb_logger_info(stdout,
                                    "Success, got %llu from %s, put back %llu",
                                    loot,
                                    victim->name,
                                    surplus);

                    /* In case someone else can use the leftovers */
                    cmi_resourceguard_signal(&(sp->front_guard));

                    return CMB_PROCESS_SUCCESS;
                }
            }

            cmb_logger_info(stdout,
                            "No more victims, still wants %llu more",
                            rem_claim);
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        const int64_t sig = cmi_resourceguard_wait(&(sp->front_guard),
                                                   store_available,
                                                   NULL);
        if (sig == CMB_PROCESS_PREEMPTED) {
            /* Got thrown out instead, unwind. */
            cmb_logger_info(stdout, "Preempted, returning empty-handed");

            return sig;
        }
        else if (sig != CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,
                            "Interrupted by signal %lld, returning unchanged",
                            sig);
            if (initially_held > 0u) {
                /* Put back difference. It had some, there should be a record */
                cmb_assert_debug(caller_handle != 0u);
                const uint64_t surplus = store_reset_holder(hp,
                                                            caller_handle,
                                                            initially_held);
                sp->in_use -= surplus;
                cmb_assert_debug(sp->in_use <= sp->capacity);
                cmi_resourceguard_signal(&(sp->front_guard));
            }
            else {
                /* Put back all. */
                const uint64_t holds_now = cmb_store_held_by_process(sp, caller);
                sp->in_use -= holds_now;
                const bool found = cmi_resourcetag_list_remove(caller_rtloc,
                                                               rbp);
                /* There may be a record created during this call, delete it */
                if (found == true) {
                    cmb_assert_debug(caller_handle != 0u);
                    cmi_hashheap_cancel(hp, caller_handle);
                }
            }

            cmb_assert_debug(sp->in_use <= sp->capacity);
            cmb_assert_debug(store_sum_holder_items(hp, 1) == sp->in_use);

            return sig;
        }
    }
}

int64_t cmb_store_acquire(struct cmb_store *sp, const uint64_t amount)
{
    cmb_assert_release(sp != NULL);
    cmb_assert_release(amount > 0u);
    cmb_assert_release(amount <= sp->capacity);

    return cmi_store_acquire_inner(sp, amount, false);
}

int64_t cmb_store_preempt(struct cmb_store *sp, const uint64_t amount)
{
    cmb_assert_release(sp != NULL);
    cmb_assert_release(amount > 0u);
    cmb_assert_release(amount <= sp->capacity);

    return cmi_store_acquire_inner(sp, amount, true);
}

/*
 * cmb_store_release : Release an amount of the resource, not necessarily
 * everything that the calling process holds.
 */
void cmb_store_release(struct cmb_store *sp, const uint64_t amount)
{
    cmb_assert_release(sp != NULL);
    cmb_assert_release(amount > 0u);
    cmb_assert_release(sp->in_use >= amount);
    cmb_assert_release(amount < sp->capacity);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)sp;
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_debug(pp != NULL);

    struct cmi_resourcetag **rtloc = &(pp->resources_listhead);
    const uint64_t caller_handle = cmi_resourcetag_list_find_handle(rtloc, rbp);
    cmb_assert_debug(caller_handle != 0u);

    void **item = cmi_hashheap_get_item(&(sp->holders), caller_handle);
    cmb_assert_debug(item[0] == pp);
    const uint64_t held = (uint64_t)item[1];
    cmb_logger_info(stdout,
                    "Has %lld, releasing %lld, total in use %llu",
                    held,
                    amount,
                    sp->in_use);
    cmb_assert_debug(held >= amount);
    cmb_assert_debug(held <= sp->in_use);

    if (held == amount) {
        /* All we had, delete the record from the resource */
        bool found = cmi_hashheap_cancel(&(sp->holders), caller_handle);
        cmb_assert_debug(found == true);

        /* ...and from the process */
        found = cmi_resourcetag_list_remove(&(pp->resources_listhead), rbp);
        cmb_assert_debug(found == true);
    }
    else {
        /* Just decrement our holding */
        item[1] = (void *)(held - amount);
    }

    /* Put it back and pling the front desk bell */
    sp->in_use -= amount;
    cmb_logger_info(stdout, "Released %lld of %s", amount, rbp->name);
    struct cmi_resourceguard *rgp = &(sp->front_guard);
    cmi_resourceguard_signal(rgp);
}
