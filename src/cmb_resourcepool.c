/*
 * cmb_resourcepool.c - a counting semaphore that supports acquire, release, and
 * preempt in specific amounts against a fixed resource capacity, where a
 * process also can acquire more of a resource it already holds some amount
 * of, or release parts of its holding. Several processes can be holding parts
 * of the resource capacity at the same time, possibly also different amounts.
 *
 * The cmb_resourcepool adds numeric values for capacity and usage to the base
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
 * to be preempted at the front, i.e. lowest priority and last in. The hashheap
 * uses the process pointer (memory address) as its key to get O(1) access.
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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
#include <stdio.h>

#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_resourcepool.h"
#include "cmb_timeseries.h"

#include "cmi_memutils.h"
#include "cmi_process.h"

struct pool_item {
    struct cmb_process *holder;
    uint64_t amount;
};

/*
 * cmb_resourcepool_create - Allocate memory for a pool object.
 */
struct cmb_resourcepool *cmb_resourcepool_create(void)
{
    struct cmb_resourcepool *rpp = cmi_malloc(sizeof(*rpp));
    cmi_memset(rpp, 0, sizeof(*rpp));
    ((struct cmi_resourcebase *)rpp)->cookie = CMI_UNINITIALIZED;

    return rpp;
}

/*
 * holder_queue_check - Test if heap_tag *a should go before *b. If so, return
 * true. Ranking lower priority (dsortkey) before higher, then LIFO based on handle
 * value. Used to identify the most likely victim for resource preemption, hence
 * opposite order of the waiting room.
 */
static bool holder_queue_check(const struct cmi_heap_tag *a,
                               const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->isortkey < b->isortkey) {
        ret = true;
    }
    else if (a->isortkey == b->isortkey) {
        if (a->key > b->key) {
            ret = true;
        }
    }

    return ret;
}

/*
 * resourcepool_drop_holder - forcibly eject a holder process without resuming it.
 * Instantiates the cmi_holdable method.
 */
static void resourcepool_drop_holder(struct cmi_holdable *rhp,
                                     const struct cmb_process *pp)
{
    cmb_assert_release(rhp != NULL);
    cmb_assert_release(pp != NULL);

    struct cmb_resourcepool *rpp = (struct cmb_resourcepool *)rhp;
    cmb_assert_debug(rpp->in_use <= rpp->capacity);
    struct cmi_hashheap *hhp = &(rpp->holders);
    const uint64_t key = (uint64_t)pp;
    const uint64_t heapidx = cmi_hash_find_index(hhp, key);

    if (heapidx != 0u) {
        const struct pool_item *pi = (struct pool_item *)(hhp->heap[heapidx].item);
        cmb_assert_debug(pi->holder == pp);
        cmb_assert_debug(pi->amount > 0u);
        cmb_assert_debug(pi->amount <= rpp->in_use);
        rpp->in_use -= pi->amount;
        const bool ret = cmi_hashheap_cancel(hhp, key);
        cmb_assert_debug(ret == true);
        cmb_resourceguard_signal(&(rpp->guard));
    }
}

/*
 * reprioritize_holder - key changed priority for one of the holders.
 * Instantiates the cmi_holdable method.
 */
static void reprioritize_holder(struct cmi_holdable *rhp,
                                const struct cmb_process *pp,
                                const int64_t pri)
{
    cmb_assert_release(rhp != NULL);
    cmb_assert_release(pp != NULL);

    const struct cmb_resourcepool *sp = (struct cmb_resourcepool *)rhp;
    const struct cmi_hashheap *hp = &(sp->holders);
    const uint64_t key = (uint64_t)pp;
    cmi_hashheap_reprioritize(hp, key, 0.0, pri);
}

/*
 * cmb_resourcepool_initialize - Make an allocated pool object ready for use.
 */
#define HOLDERS_INIT_EXP 3u

void cmb_resourcepool_initialize(struct cmb_resourcepool *rpp,
                                  const char *name,
                                  const uint64_t capacity)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_holdable_initialize(&(rpp->core), name);
    rpp->core.drop = resourcepool_drop_holder;
    rpp->core.reprio = reprioritize_holder;

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rpp;
    cmb_resourceguard_initialize(&(rpp->guard), rbp);
    cmi_hashheap_initialize(&(rpp->holders),
                            HOLDERS_INIT_EXP,
                            holder_queue_check);

    rpp->capacity = capacity;
    rpp->in_use = 0u;

    rpp->is_recording = false;
    cmb_timeseries_initialize(&(rpp->history));
}

/*
 * cmb_resourcepool_terminate - Un-initializes a pool object.
 */
void cmb_resourcepool_terminate(struct cmb_resourcepool *rpp)
{
    cmb_assert_release(rpp != NULL);

    cmb_timeseries_terminate(&(rpp->history));
    cmi_hashheap_terminate(&(rpp->holders));
    cmb_resourceguard_terminate(&(rpp->guard));
    cmi_holdable_terminate(&(rpp->core));
}

/*
 * cmb_resourcepool_destroy - Deallocates memory for a pool object.
 */
void cmb_resourcepool_destroy(struct cmb_resourcepool *rpp)
{
    cmb_assert_release(rpp != NULL);

    cmb_resourcepool_terminate(rpp);
    cmi_free(rpp);
}

/*
 * is_available - pre-packaged demand function for a cmb_resourcepool,
 * allowing the requesting process to grab some whenever there is something
 * to grab. Instantiates the resource base method.
 */
static bool is_available(const struct cmi_resourcebase *rbp,
                         const struct cmb_process *pp,
                         const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_resourcepool *rpp = (struct cmb_resourcepool *)rbp;
    const uint64_t avail = rpp->capacity - rpp->in_use;

    return (avail > 0u);
}

/*
 * reset_holder - set held amount, e.g. for rollback in an interrupted acquire.
 * Assumes that the holder is in the hashheap.
 */
static uint64_t reset_holder(const struct cmi_hashheap *hp,
                             const struct cmb_process *pp,
                             const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(pp != NULL);
    cmb_assert_release(amount > 0u);

    const uint64_t key = (uint64_t)pp;
    struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hp, key);
    cmb_assert_debug(pi != NULL);
    cmb_assert_debug(pi->holder == pp);

    const uint64_t old_amt = pi->amount;
    cmb_assert_debug(old_amt >= amount);
    pi->amount = amount;
    const uint64_t surplus = old_amt - amount;
    cmb_assert_debug(surplus <= old_amt);

    return surplus;
}

static void record_sample(struct cmb_resourcepool *sp) {
    cmb_assert_release(sp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)sp;
    cmb_assert_debug(rbp->cookie == CMI_INITIALIZED);
    if (sp->is_recording) {
        struct cmb_timeseries *ts = &(sp->history);
        const double x = (double)(sp->in_use);
        const double t = cmb_time();
        cmb_timeseries_add(ts, x, t);
    }
}

void cmb_resourcepool_start_recording(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    rsp->is_recording = true;
    record_sample(rsp);
}

void cmb_resourcepool_stop_recording(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    record_sample(rsp);
    rsp->is_recording = false;
}

struct cmb_timeseries *cmb_resourcepool_get_history(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return &(rsp->history);
}

void cmb_resourcepool_print_report(struct cmb_resourcepool *rsp, FILE *fp) {
    cmb_assert_release(rsp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    fprintf(fp, "Pool resource utilization for %s:\n", rbp->name);
    const struct cmb_timeseries *ts = &(rsp->history);
    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);
    const unsigned nbin = (rsp->capacity > 20) ? 20 : rsp->capacity + 1;
    cmb_timeseries_histogram_print(ts, fp, nbin, 0.0, (double)(rsp->capacity + 1u));
}

uint64_t cmb_resourcepool_held_by_process(const struct cmb_resourcepool *rpp,
                                          const struct cmb_process *pp)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_debug(pp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rpp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    const uint64_t key = (uint64_t)pp;
    const struct cmi_hashheap *hhp = &(rpp->holders);
    const uint64_t heapidx = cmi_hash_find_index(hhp, key);
    if (heapidx != 0u) {
        const struct pool_item *pi = (struct pool_item *)(hhp->heap[heapidx].item);
        return pi->amount;
    }
    else {
        return 0u;
    }
}

static uint64_t sum_holder_items(const struct cmi_hashheap *hhp)
{
    cmb_assert_release(hhp != NULL);

    uint64_t sum = 0u;
    for (uint64_t ui = 1u; ui <= hhp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hhp->heap[ui]);
        sum += (uint64_t)(htp->item[1]);
    }

    return sum;
}

static void update_record(struct cmb_resourcepool *rpp,
                          struct cmb_process *pp,
                          const uint64_t amount)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_release(pp != NULL);
    cmb_assert_release(amount > 0u);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rpp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    const uint64_t key = (uint64_t)pp;
    struct cmi_hashheap *hhp = &(rpp->holders);
    const uint64_t heapidx = cmi_hash_find_index(hhp, key);
    if (heapidx != 0u) {
        /* Must hold some already, add to the existing entry */
        struct pool_item *pi = (struct pool_item *)(hhp->heap[heapidx].item);
        pi->amount += amount;
    }
    else {
        /* Add this resource pool to the list of resources held by the process */
        struct cmi_process_holdable *hp = cmi_mempool_alloc(&cmi_process_holdabletags);
        hp->res = (struct cmi_holdable *)rpp;
        cmi_slist_push(&(pp->resources), &(hp->listhead));

        /* Not held already, create a new resource pool holder entry for the process */
        const uint64_t new_key = cmi_hashheap_enqueue(hhp,
                                                     (void *)pp, (void *)amount,
                                                     NULL, NULL,
                                                     key, 0.0, pp->priority);
        cmb_assert_debug(new_key == key);
    }
}

/*
 * cmb_pool_acquire_inner - Acquire, perhaps preempt, and, if necessary, wait
 * for a req_amount of the pool resource. The calling process may already hold
 * some and try to increase its holding or acquire its first helping.
 */
int64_t cmi_pool_acquire_inner(struct cmb_resourcepool *rpp,
                               const uint64_t req_amount,
                               const bool preempt)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_release(req_amount > 0u);
    cmb_assert_debug(rpp->in_use <= rpp->capacity);

    struct cmb_process *caller = cmb_process_current();

    /* Does the caller already hold some? */
    uint64_t initially_held = 0u;
    struct cmi_hashheap *hhp = &(rpp->holders);
    const uint64_t key = (uint64_t)caller;
    const uint64_t heapidx = cmi_hash_find_index(hhp, key);
    if (heapidx != 0u) {
        /* It does. Note the amount in case we need to roll back to here */
        initially_held = (uint64_t)(hhp->heap[heapidx].item[1]);
    }

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rpp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_logger_info(stdout, "Has %" PRIu64 ", requests %" PRIu64 " from %s",
                    initially_held, req_amount, rbp->name);

    /*
     * Greedy approach, first grab what is available, then preempt from lower
     * priority processes (if allowed), finally wait in line for the remaining
     * amount if any.
     */
    uint64_t rem_claim = req_amount;
    while (true) {
        const uint64_t available = rpp->capacity - rpp->in_use;

        /* First, take anything that is available already */
        if (available >= rem_claim) {
            /* Grab what we need */
            rpp->in_use += rem_claim;
            cmb_assert_debug(rpp->in_use <= rpp->capacity);
            record_sample(rpp);
            update_record(rpp, caller, rem_claim);

            cmb_assert_debug(rpp->in_use <= rpp->capacity);
            cmb_assert_debug(sum_holder_items(hhp) == rpp->in_use);
            cmb_logger_info(stdout,
                            "Success, %" PRIu64 " was already available",
                            rem_claim);

            /* In case someone else can use the leftovers */
            cmb_resourceguard_signal(&(rpp->guard));

            return CMB_PROCESS_SUCCESS;
        }
        else if (available > 0u) {
            /* Grab what is there */
            rpp->in_use += available;
            cmb_assert_debug(rpp->in_use <= rpp->capacity);
            record_sample(rpp);
            rem_claim -= available;
            update_record(rpp, caller, available);

            cmb_assert_debug(rpp->in_use <= rpp->capacity);
            cmb_assert_debug(sum_holder_items(hhp) == rpp->in_use);
            cmb_logger_info(stdout,
                            "Found %" PRIu64 " available, still wants %" PRIu64,
                            available,  rem_claim);
        }

        /* We have taken what was available, and we still want more */
        cmb_assert_debug(rem_claim > 0u);
        if (preempt) {
            /* Look for victims to mug for more */
            while (!cmi_hashheap_is_empty(hhp)
                   && cmi_hashheap_peek_ikey(hhp) < caller->priority) {
                /* There is one, pull it off the holders' hashheap */
                const struct pool_item *pi = (struct pool_item *)cmi_hashheap_dequeue(hhp);
                struct cmb_process *victim = pi->holder;
                const uint64_t loot = pi->amount;
                cmb_assert_debug(loot > 0u);
                cmb_assert_debug(loot <= rpp->in_use);

                /* Remove the resource from the victim's resource list */
                const struct cmi_holdable *hrp = (struct cmi_holdable *)rpp;
                const bool found = cmi_process_remove_holdable(victim, hrp);
                cmb_assert_debug(found == true);

                /* Schedule a wakeup for it, but do not switch context yet */
                cmb_process_interrupt(victim, CMB_PROCESS_PREEMPTED, victim->priority);

                 /* Split the loot */
                if (loot < rem_claim) {
                    /* Add everything to our own holding */
                    update_record(rpp, caller, loot);
                    rem_claim -= loot;

                    /* The quantity in use is unchanged, just changes hands. */
                    cmb_assert_debug(rpp->in_use <= rpp->capacity);
                    cmb_assert_debug(sum_holder_items(hhp) == rpp->in_use);
                    cmb_logger_info(stdout,
                                    "Got %" PRIu64 " from %s, still needs %" PRIu64 "",
                                    loot, victim->name, rem_claim);
                }
                else {
                    /* You take what you need, and you leave the rest */
                    update_record(rpp, caller, rem_claim);
                    const uint64_t surplus = loot - rem_claim;
                    rpp->in_use -= surplus;
                    cmb_assert_debug(rpp->in_use <= rpp->capacity);
                    record_sample(rpp);

                    cmb_assert_debug(sum_holder_items(hhp) == rpp->in_use);
                    cmb_logger_info(stdout,
                                    "Success, got %" PRIu64 " from %s, put back %" PRIu64,
                                    loot, victim->name, surplus);

                    /* In case someone else can use the leftovers */
                    cmb_resourceguard_signal(&(rpp->guard));

                    return CMB_PROCESS_SUCCESS;
                }
            }

            cmb_logger_info(stdout,
                            "No more victims, still wants %" PRIu64 " more",
                            rem_claim);
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        const int64_t sig = cmb_resourceguard_wait(&(rpp->guard), is_available, NULL);
        if (sig == CMB_PROCESS_PREEMPTED) {
            /* Got thrown out instead, unwind. */
            cmb_logger_info(stdout, "Preempted, returning empty-handed");

            return sig;
        }
        else if (sig != CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,
                            "Interrupted by signal %" PRId64 ", returning unchanged",
                            sig);
            if (initially_held > 0u) {
                /* Put back the difference. It had some, there should be a record */
                const uint64_t surplus = reset_holder(hhp, caller, initially_held);
                rpp->in_use -= surplus;
                cmb_assert_debug(rpp->in_use <= rpp->capacity);
                record_sample(rpp);

                cmb_resourceguard_signal(&(rpp->guard));
            }
            else {
                /* Had nothing, put back all. */
                const uint64_t holds_now = cmb_resourcepool_held_by_process(rpp, caller);
                rpp->in_use -= holds_now;
                cmb_assert_debug(rpp->in_use <= rpp->capacity);
                record_sample(rpp);

                /* There may be a record created during this call, delete it */
                const struct cmi_holdable *hrp = (struct cmi_holdable *)rpp;
                bool found = cmi_hashheap_cancel(hhp, key);
                if (found == true) {
                    /* A record existed, so there is an entry on the caller */
                    found = cmi_process_remove_holdable(caller, hrp);
                    cmb_assert_debug(found == true);
                }
            }

            cmb_assert_debug(rpp->in_use <= rpp->capacity);
            cmb_assert_debug(sum_holder_items(hhp) == rpp->in_use);

            return sig;
        }
    }
}


int64_t cmb_resourcepool_acquire(struct cmb_resourcepool *rpp,
                                 const uint64_t req_amount)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_release(req_amount > 0u);
    cmb_assert_release(req_amount <= rpp->capacity);

    return cmi_pool_acquire_inner(rpp, req_amount, false);
}


int64_t cmb_resourcepool_preempt(struct cmb_resourcepool *rpp,
                                 const uint64_t req_amount)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_release(req_amount > 0u);
    cmb_assert_release(req_amount <= rpp->capacity);

    return cmi_pool_acquire_inner(rpp, req_amount, true);
}


/*
 * cmb_resourcepool_release - Release rel_amount of the resource, not necessarily
 * everything that the calling process holds.
 */
void cmb_resourcepool_release(struct cmb_resourcepool *rpp, const uint64_t rel_amount)
{
    cmb_assert_release(rpp != NULL);
    cmb_assert_release(rel_amount > 0u);
    cmb_assert_release(rpp->in_use >= rel_amount);
    cmb_assert_release(rel_amount <= rpp->capacity);

    const struct cmi_holdable *hrp = (struct cmi_holdable *)rpp;
    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rpp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    struct cmb_process *pp = cmb_process_current();
    cmb_assert_debug(pp != NULL);
    const uint64_t key = (uint64_t)pp;

    const struct cmi_hashheap *hhp = &(rpp->holders);
    struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hhp, key);
    cmb_assert_debug(pi->holder == pp);
    cmb_logger_info(stdout,
                    "Has %" PRIu64 ", releasing %" PRIu64 ", total in use %" PRIu64,
                    pi->amount, rel_amount, rpp->in_use);
    cmb_assert_debug(pi->amount >= rel_amount);
    cmb_assert_debug(pi->amount <= rpp->in_use);

    if (pi->amount == rel_amount) {
        /* Release all we have, delete the record from the resource */
        bool found = cmi_hashheap_cancel(&(rpp->holders), key);
        cmb_assert_debug(found == true);

        /* ...and from the process */
        found = cmi_process_remove_holdable(pp, hrp);
        cmb_assert_debug(found == true);
    }
    else {
        /* Just decrement our holding */
        pi->amount -= rel_amount;
    }

    /* Put it back and ring the front desk bell */
    rpp->in_use -= rel_amount;
    cmb_assert_debug(rpp->in_use <= rpp->capacity);
    record_sample(rpp);
    cmb_logger_info(stdout, "Released %" PRIu64 " of %s", rel_amount, hrp->base.name);

    struct cmb_resourceguard *rgp = &(rpp->guard);
    cmb_resourceguard_signal(rgp);
}
