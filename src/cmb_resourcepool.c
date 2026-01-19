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
 * to be preempted at the front, i.e. lowest priority and last in.
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
    void *padding[2];
};

/*
 * cmb_resourcepool_create - Allocate memory for a pool object.
 */
struct cmb_resourcepool *cmb_resourcepool_create(void)
{
    struct cmb_resourcepool *sp = cmi_malloc(sizeof(*sp));
    cmi_memset(sp, 0, sizeof(*sp));
    ((struct cmi_resourcebase *)sp)->cookie = CMI_UNINITIALIZED;

    return sp;
}

/*
 * holder_queue_check - Test if heap_tag *a should go before *b. If so, return
 * true. Ranking lower priority (dkey) before higher, then LIFO based on handle
 * value. Used to identify the most likely victim for resource preemption, hence
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
 * resourcepool_drop_holder - forcibly eject a holder process without resuming it.
 */
static void resourcepool_drop_holder(struct cmi_holdable *rbp,
                                     const struct cmb_process *pp,
                                     const uint64_t handle)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmb_assert_release(handle != 0u);

    struct cmb_resourcepool *sp = (struct cmb_resourcepool *)rbp;
    struct cmi_hashheap *hp = &(sp->holders);
    const struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hp, handle);
    cmb_assert_debug(pi->holder == pp);
    cmb_assert_debug(pi->amount > 0u);
    cmb_assert_debug(pi->amount <= sp->in_use);

    const bool ret = cmi_hashheap_cancel(hp, handle);
    cmb_assert_debug(ret == true);

    sp->in_use -= pi->amount;
    cmb_assert_debug(sp->in_use < sp->capacity);
    cmb_resourceguard_signal(&(sp->guard));
}

/*
 * reprioritize_holder - handle changed priority for one of the holders.
 */
static void reprioritize_holder(struct cmi_holdable *rbp,
                                const uint64_t handle,
                                const int64_t pri)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(handle != 0u);

    const struct cmb_resourcepool *sp = (struct cmb_resourcepool *)rbp;
    const struct cmi_hashheap *hp = &(sp->holders);
    const double dkey = cmi_hashheap_dkey(hp, handle);
    cmi_hashheap_reprioritize(hp, handle, dkey, pri);
}

/*
 * cmb_resourcepool_initialize - Make an allocated pool object ready for use.
 */
#define HOLDERS_INIT_EXP 3u

void cmb_resourcepool_initialize(struct cmb_resourcepool *rsp,
                                  const char *name,
                                  const uint64_t capacity)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_holdable_initialize(&(rsp->core), name);
    rsp->core.drop = resourcepool_drop_holder;
    rsp->core.reprio = reprioritize_holder;

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_resourceguard_initialize(&(rsp->guard), rbp);
    cmi_hashheap_initialize(&(rsp->holders),
                            HOLDERS_INIT_EXP,
                            holder_queue_check);

    rsp->capacity = capacity;
    rsp->in_use = 0u;

    rsp->is_recording = false;
    cmb_timeseries_initialize(&(rsp->history));
}

/*
 * cmb_resourcepool_terminate - Un-initializes a pool object.
 */
void cmb_resourcepool_terminate(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);

    cmb_timeseries_terminate(&(rsp->history));
    cmi_hashheap_terminate(&(rsp->holders));
    cmb_resourceguard_terminate(&(rsp->guard));
    cmi_holdable_terminate(&(rsp->core));
}

/*
 * cmb_resourcepool_destroy - Deallocates memory for a pool object.
 */
void cmb_resourcepool_destroy(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);

    cmb_resourcepool_terminate(rsp);
    cmi_free(rsp);
}

/*
 * is_available - pre-packaged demand function for a cmb_resourcepool,
 * allowing the requesting process to grab some whenever there is something
 * to grab.
 */
static bool is_available(const struct cmi_resourcebase *rbp,
                         const struct cmb_process *pp,
                         const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_resourcepool *rsp = (struct cmb_resourcepool *)rbp;
    const uint64_t avail = rsp->capacity - rsp->in_use;

    return (avail > 0u);
}

static uint64_t reset_holder(const struct cmi_hashheap *hp,
                             const uint64_t holder_handle,
                             const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(holder_handle != 0u);
    cmb_assert_release(amount > 0u);

    struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hp, holder_handle);
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
    cmb_timeseries_print_histogram(ts, fp, nbin, 0.0, (double)(rsp->capacity + 1u));
}

static uint64_t find_handle(const struct cmi_slist_head *rlst,
                            const struct cmi_holdable *hrp)
{
    cmb_assert_release(rlst != NULL);
    cmb_assert_release(hrp != NULL);

    while (rlst->next != NULL) {
        const struct cmi_process_holdable *hp = cmi_container_of(rlst->next,
                                                    struct cmi_process_holdable,
                                                    listhead);
        if (hp->res == hrp) {
            return hp->handle;
        }

        rlst = rlst->next;
    }

    return 0u;
}

static uint64_t amount_held(const struct cmb_resourcepool *rsp, const uint64_t handle)
{
    cmb_assert_debug(rsp != NULL);
    cmb_assert_debug(((struct cmi_resourcebase *)rsp)->cookie == CMI_INITIALIZED);
    cmb_assert_debug(handle != 0u);

    const struct cmi_hashheap *hp = &(rsp->holders);
    const struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hp, handle);

    return pi->amount;
}

uint64_t cmb_resourcepool_held_by_process(const struct cmb_resourcepool *rsp,
                                          const struct cmb_process *pp)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)rsp)->cookie == CMI_INITIALIZED);
    cmb_assert_release(pp != NULL);

    uint64_t ret = 0u;
    const struct cmi_holdable *hrp = (struct cmi_holdable *)rsp;
    const struct cmi_slist_head *rlst = &(pp->resources);
    const uint64_t handle = find_handle(rlst, hrp);
    if (handle != 0u) {
        ret = amount_held(rsp, handle);
    }

    return ret;
}

static uint64_t add_new_holder(struct cmi_hashheap *hp,
                               const struct cmb_process *pp,
                               const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(amount > 0u);

    const uint64_t new_handle = cmi_hashheap_enqueue(hp, (void *)pp,
                                                     (void *)amount,
                                                     NULL, NULL,
                                                     cmb_time(),
                                                     pp->priority);
    cmb_assert_debug(new_handle != 0u);

    return new_handle;
}

static void add_to_holder(const struct cmi_hashheap *hp,
                          const uint64_t holder_handle,
                          const uint64_t amount)
{
    cmb_assert_release(hp != NULL);
    cmb_assert_release(holder_handle != 0u);
    cmb_assert_release(amount > 0u);

    struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hp, holder_handle);
    pi->amount += amount;
}

static uint64_t sum_holder_items(const struct cmi_hashheap *hp)
{
    cmb_assert_release(hp != NULL);

    uint64_t sum = 0u;
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        const struct cmi_heap_tag *htp = &(hp->heap[ui]);
        sum += (uint64_t)(htp->item[1]);
    }

    return sum;
}

static uint64_t update_record(struct cmb_resourcepool *pool,
                              struct cmb_process *proc,
                              uint64_t proc_handle,
                              const uint64_t amount)
{
    cmb_assert_release(pool != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pool)->cookie == CMI_INITIALIZED);
    cmb_assert_release(proc != NULL);
    cmb_assert_release(amount > 0u);


    if (proc_handle != 0u) {
        /* Must hold some already, add to the existing entry */
        add_to_holder(&(pool->holders), proc_handle, amount);
    }
    else {
        /* Not held already, create a new holder entry for the process */
        proc_handle = add_new_holder(&(pool->holders), proc, amount);

        /* Add this resource pool to the list of resources held by the process */
        struct cmi_process_holdable *hp = cmi_mempool_alloc(&cmi_process_holdabletags);
        hp->res = (struct cmi_holdable *)pool;
        hp->handle = proc_handle;
        hp->amount = amount;
        cmi_slist_push(&(proc->resources), &(hp->listhead));
    }

    return proc_handle;
}

/*
 * cmb_pool_acquire_inner - Acquire, perhaps preempt, and, if necessary, wait
 * for a req_amount of the pool resource. The calling process may already hold
 * some and try to increase its holding or acquire its first helping.
 */
int64_t cmi_pool_acquire_inner(struct cmb_resourcepool *rp,
                               const uint64_t req_amount,
                               const bool preempt)
{
    cmb_assert_release(rp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)rp)->cookie == CMI_INITIALIZED);
    cmb_assert_release(req_amount > 0u);
    cmb_assert_debug(rp->in_use <= rp->capacity);

    struct cmb_process *caller = cmb_process_current();

    /* Does the caller already hold some? */
    uint64_t initially_held = 0u;
    struct cmi_hashheap *hhp = &(rp->holders);
    const struct cmi_holdable *hrp = (struct cmi_holdable *)rp;
    uint64_t caller_hdle = find_handle(&(caller->resources), hrp);

    if (caller_hdle != 0u) {
        /* It does. Note the amount in case we need to roll back to here */
        initially_held = amount_held(rp, caller_hdle);
    }

    cmb_logger_info(stdout, "Has %" PRIu64 ", requests %" PRIu64 " from %s",
                    initially_held, req_amount, hrp->base.name);

    /*
     * Greedy approach, first grab what is available, then preempt from lower
     * priority processes (if allowed), finally wait in line for the remaining
     * amount if any.
     */
    uint64_t rem_claim = req_amount;
    while (true) {
        const uint64_t available = rp->capacity - rp->in_use;

        /* First, take anything that is available already */
        if (available >= rem_claim) {
            /* Grab what we need */
            rp->in_use += rem_claim;
            cmb_assert_debug(rp->in_use <= rp->capacity);
            record_sample(rp);
            (void)update_record(rp, caller, caller_hdle, rem_claim);

            cmb_assert_debug(rp->in_use <= rp->capacity);
            cmb_assert_debug(sum_holder_items(hhp) == rp->in_use);
            cmb_logger_info(stdout,
                            "Success, %" PRIu64 " was already available",
                            rem_claim);

            /* In case someone else can use the leftovers */
            cmb_resourceguard_signal(&(rp->guard));

            return CMB_PROCESS_SUCCESS;
        }
        else if (available > 0u) {
            /* Grab what is there */
            rp->in_use += available;
            cmb_assert_debug(rp->in_use <= rp->capacity);
            record_sample(rp);
            rem_claim -= available;
            caller_hdle = update_record(rp, caller, caller_hdle, available);

            cmb_assert_debug(rp->in_use <= rp->capacity);
            cmb_assert_debug(sum_holder_items(hhp) == rp->in_use);
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
                void **item = cmi_hashheap_dequeue(hhp);
                struct cmb_process *victim = (struct cmb_process *)item[0];
                const uint64_t loot = (uint64_t) item[1];
                cmb_assert_debug(loot > 0u);
                cmb_assert_debug(loot <= rp->in_use);

                /* Remove the resource from the victim's resource list */
                const bool found = cmi_process_remove_holdable(victim, hrp);
                cmb_assert_debug(found == true);

                /* Schedule a wakeup for it, but do not switch context yet */
                cmb_process_interrupt(victim, CMB_PROCESS_PREEMPTED, victim->priority);

                 /* Split the loot */
                if (loot < rem_claim) {
                    /* Take it all. */
                    caller_hdle = update_record(rp, caller, caller_hdle, loot);
                    rem_claim -= loot;

                    /* The quantity in use is unchanged, just changes hands. */
                    cmb_assert_debug(rp->in_use <= rp->capacity);
                    cmb_assert_debug(sum_holder_items(hhp) == rp->in_use);
                    cmb_logger_info(stdout,
                                    "Got %" PRIu64 " from %s, still needs %" PRIu64 "",
                                    loot, victim->name, rem_claim);
                }
                else {
                    /* Take what we need, put back the rest */
                    (void)update_record(rp, caller, caller_hdle, rem_claim);
                    const uint64_t surplus = loot - rem_claim;
                    rp->in_use -= surplus;
                    cmb_assert_debug(rp->in_use <= rp->capacity);
                    record_sample(rp);

                    cmb_assert_debug(sum_holder_items(hhp) == rp->in_use);
                    cmb_logger_info(stdout,
                                    "Success, got %" PRIu64 " from %s, put back %" PRIu64,
                                    loot, victim->name, surplus);

                    /* In case someone else can use the leftovers */
                    cmb_resourceguard_signal(&(rp->guard));

                    return CMB_PROCESS_SUCCESS;
                }
            }

            cmb_logger_info(stdout,
                            "No more victims, still wants %" PRIu64 " more",
                            rem_claim);
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        const int64_t sig = cmb_resourceguard_wait(&(rp->guard), is_available, NULL);
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
                cmb_assert_debug(caller_hdle != 0u);
                const uint64_t surplus = reset_holder(hhp, caller_hdle, initially_held);
                rp->in_use -= surplus;
                cmb_assert_debug(rp->in_use <= rp->capacity);
                record_sample(rp);

                cmb_resourceguard_signal(&(rp->guard));
            }
            else {
                /* Had nothing, put back all. */
                const uint64_t holds_now = cmb_resourcepool_held_by_process(rp, caller);
                rp->in_use -= holds_now;
                cmb_assert_debug(rp->in_use <= rp->capacity);
                record_sample(rp);

                /* There may be a record created during this call, delete it */
                const bool found = cmi_process_remove_holdable(caller, hrp);
                if (found == true) {
                    cmb_assert_debug(caller_hdle != 0u);
                    cmi_hashheap_cancel(hhp, caller_hdle);
                }
            }

            cmb_assert_debug(rp->in_use <= rp->capacity);
            cmb_assert_debug(sum_holder_items(hhp) == rp->in_use);

            return sig;
        }
    }
}


int64_t cmb_resourcepool_acquire(struct cmb_resourcepool *rsp, const uint64_t req_amount)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(req_amount > 0u);
    cmb_assert_release(req_amount <= rsp->capacity);

    return cmi_pool_acquire_inner(rsp, req_amount, false);
}


int64_t cmb_resourcepool_preempt(struct cmb_resourcepool *rsp, const uint64_t req_amount)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(req_amount > 0u);
    cmb_assert_release(req_amount <= rsp->capacity);

    return cmi_pool_acquire_inner(rsp, req_amount, true);
}


/*
 * cmb_resourcepool_release - Release rel_amount of the resource, not necessarily
 * everything that the calling process holds.
 */
void cmb_resourcepool_release(struct cmb_resourcepool *rsp, const uint64_t rel_amount)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)rsp)->cookie == CMI_INITIALIZED);
    cmb_assert_release(rel_amount > 0u);
    cmb_assert_release(rsp->in_use >= rel_amount);
    cmb_assert_release(rel_amount <= rsp->capacity);

    struct cmb_process *pp = cmb_process_current();
    cmb_assert_debug(pp != NULL);

    struct cmi_holdable *hrp = (struct cmi_holdable *)rsp;
    const uint64_t caller_handle = find_handle(&(pp->resources), hrp);
    cmb_assert_debug(caller_handle != 0u);

    struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(&(rsp->holders), caller_handle);
    cmb_assert_debug(pi->holder == pp);
    cmb_logger_info(stdout,
                    "Has %" PRIu64 ", releasing %" PRIu64 ", total in use %" PRIu64,
                    pi->amount, rel_amount, rsp->in_use);
    cmb_assert_debug(pi->amount >= rel_amount);
    cmb_assert_debug(pi->amount <= rsp->in_use);

    if (pi->amount == rel_amount) {
        /* All we had, delete the record from the resource */
        bool found = cmi_hashheap_cancel(&(rsp->holders), caller_handle);
        cmb_assert_debug(found == true);

        /* ...and from the process */
        found = cmi_process_remove_holdable(pp, hrp);
        cmb_assert_debug(found == true);
    }
    else {
        /* Just decrement our holding */
        pi->amount -= rel_amount;
    }

    /* Put it back and pling the front desk bell */
    rsp->in_use -= rel_amount;
    cmb_assert_debug(rsp->in_use <= rsp->capacity);
    record_sample(rsp);
    cmb_logger_info(stdout, "Released %" PRIu64 " of %s", rel_amount, hrp->base.name);

    struct cmb_resourceguard *rgp = &(rsp->guard);
    cmb_resourceguard_signal(rgp);
}
