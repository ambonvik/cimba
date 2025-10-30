/*
 * cmi_resource.h - guarded resources that the processes can queue for
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
#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_resource.h"

#include "cmi_hashheap.h"
#include "cmi_memutils.h"

/*
 * cmi_resource_core_initialize : Make an already allocated resource core
 * object ready for use with a given capacity.
 */
void cmi_resource_core_initialize(struct cmi_resource_core *rcp,
                                  const uint64_t capacity)
{
    cmb_assert_release(rcp != NULL);
    cmb_assert_release(capacity > 0u);

    rcp->capacity = capacity;
    rcp->in_use = 0u;
    rcp->holders = NULL;
}

/*
 * cmi_resource_core_terminate : Un-initializes a resource core object.
 */
void cmi_resource_core_terminate(struct cmi_resource_core *rcp)
{
    cmb_assert_release(rcp != NULL);
    cmb_assert_release(rcp->holders == NULL);
}


/*
 * guard_queue_check : Test if heap_tag *a should go before *b. If so, return true.
 * Ranking higher priority (dkey) before lower, then FIFO based on handle value.
 */
static bool guard_queue_check(const struct cmi_heap_tag *a,
                              const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->ikey > b->ikey) {
        ret = true;
    }
    else if (a->ikey == b->ikey) {
        if (a->handle < b->handle) {
            ret = true;
        }
    }

    return ret;
}

/* Start very small and fast, 2^GUARD_INIT_EXP = 8 slots in the initial queue */
#define GUARD_INIT_EXP 3u

void cmi_resource_guard_initialize(struct cmi_resource_guard *rgp)
{
    cmb_assert_release(rgp != NULL);

    cmi_hashheap_initialize((struct cmi_hashheap *)rgp,
                            GUARD_INIT_EXP,
                            guard_queue_check);
}

void cmi_resource_guard_terminate(struct cmi_resource_guard *rgp)
{
    cmb_assert_release(rgp != NULL);

    cmi_hashheap_terminate((struct cmi_hashheap *)rgp);
}

/*
 * cmi_resource_guard_wait : Enqueue and suspend the calling process until it
 * reaches the front of the priority queue and its demand function returns true.
 * ctx is whatever context the demand function needs to evaluate if it is
 * satisfied or not, such as the number of units needed from the resource or
 * something more complex and user application defined.
 * Returns whatever signal was received when the process was reactivated.
 * Cannot be called from the main process, will fire an assert if attempted.
 */
int64_t cmi_resource_guard_wait(struct cmi_resource_guard *rgp,
                                cmb_resource_demand_func *demand,
                                void *ctx)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(demand != NULL);
    cmb_assert_release(ctx != NULL);

    /* cmb_process_get_current returns NULL if called from the main process */
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);

    /* Check if demand is already satisfied */
    struct cmb_resource *rp = cmi_container_of(rgp, struct cmb_resource, front);
    if ((*demand)(rp, pp, ctx) == true) {
        return CMB_RESOURCE_ACQUIRE_NORMAL;
    }

    /* Contrived cast to suppress compiler warning about pointer conversion */
    void *vdemand = *(void **)&demand;

    const double entry_time = cmb_time();
    const int64_t priority = cmb_process_get_priority(pp);

    cmi_hashheap_enqueue((struct cmi_hashheap *)rgp,
                         (void *)pp,
                         vdemand,
                         ctx,
                         NULL,
                         entry_time,
                         priority);

    /* Yield to the scheduler, collect the return signal value when resumed */
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /*
     * Back here again, possibly much later. Return whatever signal was passed
     * through the resume call that took us back here.
     */
    return ret;
}

/*
 * prwuevt : The event handler that actually resumes the process coroutine after
 * being scheduled by cmb_resource_guard_wait
 */
static void prwuevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)vp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
    else {
        const struct cmb_process *pp = (struct cmb_process *)vp;
        cmb_logger_warning(stdout,
                          "process wait wakeup call found process %s dead",
                           cmb_process_get_name(pp));
    }
}

/*
 * cmi_resource_guard_signal : Plings the bell for a resource guard to check if
 * any of the waiting processes should be resumed. Will evaluate the demand
 * function for the first process in the queue, if any, and will resume it if
 * (and only if) its demand function (*demand)(pp, rp, ctx) returns true.
 *
 * Resumes zero or one waiting processes. Call it again if there is a chance
 * that more than one process could be ready, e.g. if some process just returned
 * five units of a resource and there are several processes waiting for one
 * unit each.
 *
 * In cases where some waiting process needs to bypass another, e.g. if there
 * are three available units of the resource, the first process in the queue
 * demands five, and there are three more behind it that demands one each, it is
 * up to the application to dynamically change process priorities to bring the
 * correct process to the front of the queue.
 *
 * We have four 64-bit payload fields in the hash_heap entries. Usage:
 * item[0] - pointer to the process itself
 * item[1] - pointer to its demand function
 * item[2] - its context pointer
 * item[3] - not used here
 */
bool cmi_resource_guard_signal(struct cmi_resource_guard *rgp)
{
    cmb_assert_release(rgp != NULL);

    struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
    if (cmi_hashheap_is_empty(hp)) {
        return false;
    }

    /* Decode first entry in the hashheap */
    void **item = cmi_hashheap_peek_item(hp);
    struct cmb_process *pp = (struct cmb_process *)(item[0]);
    cmb_resource_demand_func *demand = *(cmb_resource_demand_func **)&(item[1]);
    void *ctx = item[2];

    /* Is the demand met? */
    struct cmb_resource *rp = cmi_container_of(rgp, struct cmb_resource, front);
    if ((*demand)(rp, pp, ctx)) {
        /* Yes, pull the process off the queue and schedule a wakeup event */
        (void)cmi_hashheap_dequeue(hp);
        const double time = cmb_time();
        const int64_t priority = cmb_process_get_priority(pp);
        (void)cmb_event_schedule(prwuevt,
                                pp,
                                (void *)CMB_RESOURCE_ACQUIRE_NORMAL,
                                 time,
                                 priority);
        return true;
    }
    else {
        /* No, leave it where it is */
        return false;
    }
}

/*
 * cmi_resource_guard_cancel : Remove this process from the priority queue and
 * schedule a wakeup event with a CMB_PROCESS_WAIT_CANCELLED signal.
 * Returns true if found and successfully cancelled, false if not.
 */
bool cmi_resource_guard_cancel(struct cmi_resource_guard *rgp,
                                   struct cmb_process *pp)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(pp != NULL);

    struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
    const uint64_t handle = cmi_hashheap_pattern_find(hp,
                                                     (void *)pp,
                                                     CMI_ANY_ITEM,
                                                     CMI_ANY_ITEM,
                                                     CMI_ANY_ITEM);

    if (handle != 0u) {
        (void)cmi_hashheap_cancel(hp, handle);
        const double time = cmb_time();
        const int64_t priority = cmb_process_get_priority(pp);
        (void)cmb_event_schedule(prwuevt,
                                pp,
                                (void *)CMB_RESOURCE_ACQUIRE_CANCELLED,
                                 time,
                                 priority);
        return true;
    }
    else {
        return false;
    }
}

/*
 * cmb_resource_create : Allocate memory for a resource object.
 */
struct cmb_resource *cmb_resource_create(void)
{
    struct cmb_resource *rp = cmi_malloc(sizeof(*rp));
    cmi_memset(rp, 0, sizeof(*rp));

    return rp;
}

/*
 * cmb_resource_initialize : Make an already allocated resource object ready for
 * use with a given capacity.
 */
void cmb_resource_initialize(struct cmb_resource *rp, uint64_t capacity)
{
    cmb_assert_release(rp != NULL);

    cmi_resource_core_initialize(&(rp->core), capacity);
    cmi_resource_guard_initialize(&(rp->front));
}

/*
 * cmb_resource_terminate : Un-initializes a resource object.
 */
void cmb_resource_terminate(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    cmi_resource_core_terminate(&(rp->core));
    cmi_resource_guard_terminate(&(rp->front));
}

/*
 * cmb_resource_destroy : Deallocates memory for a resource object.
 */
void cmb_resource_destroy(struct cmb_resource *rp)
{
    cmb_resource_terminate(rp);
    cmi_free(rp);
}

static bool resource_available(struct cmb_resource *rp,
                               struct cmb_process *pp,
                               void *ctx)
{
    const uint64_t amnt = (uint64_t)ctx;
    const uint64_t avail = rp->core.capacity - rp->core.in_use;

    return (avail >= amnt);
}

int64_t cmb_resource_acquire(struct cmb_resource *rp,
                             const uint64_t amount)
{
    cmb_assert_release(rp != NULL);
    cmb_assert_release(amount > 0u);
    cmb_assert_release(amount <= rp->core.capacity);

    const int64_t ret = cmi_resource_guard_wait(&(rp->front),
                                                resource_available,
                                                (void *)amount);

    if (ret == CMB_RESOURCE_ACQUIRE_NORMAL) {
        rp->core.in_use -= amount;
        struct cmb_process *pp = cmb_process_get_current();
        cmi_processtag_list_add(&(rp->core.holders), pp, (void *)amount);
    }

    return ret;
}

/*
 * cmb_resource_release : Release a given amount of the resource.
 */
void cmb_resource_release(struct cmb_resource *rp, uint64_t amount) {
    cmb_assert_release(rp != NULL);
    cmb_assert_release(amount > 0u);
    cmb_assert_release(amount <= rp->core.in_use);

    /* find holder, check that it has that much, decrement, remove from holders
     * if it was everything it held, signal front gate to let in someone else
     */
}

