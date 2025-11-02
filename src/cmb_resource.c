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

/*******************************************************************************
 * cmi_resource_guard: The hashheap that handles a resource wait list
 ******************************************************************************/

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

void cmi_resource_guard_initialize(struct cmi_resource_guard *rgp,
                                   struct cmi_resource_base *rbp)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(rbp != NULL);

    cmi_hashheap_initialize((struct cmi_hashheap *)rgp,
                            GUARD_INIT_EXP,
                            guard_queue_check);

    rgp->guarded_resource = rbp;
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

    /* cmb_process_get_current returns NULL if called from the main process */
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);

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

    cmb_logger_info(stdout,
                   "Waits in line for %s",
                    rgp->guarded_resource->name);

    /* Yield to the scheduler, collect the return signal value when resumed */
    const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

    /* Back here, possibly much later. Return the signal that resumed us. */
    return sig;
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
    const void *ctx = item[2];

    /* Is the demand met? */
    const struct cmi_resource_base *rbp = rgp->guarded_resource;
    if ((*demand)(rbp, pp, ctx)) {
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

/*******************************************************************************
 * cmi_resource_base: The virtual parent "class" of the different resource types
 ******************************************************************************/

/*
 * base_scram : dummy scram function, to be replaced by appropriate scram in
 * derived classes.
 */
void base_scram(struct cmi_resource_base *rbp,
                const struct cmb_process *pp,
                const uint64_t handle)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmi_unused(handle);

    cmb_logger_error(stderr,
                    "Resource base scram, process %s resource %s, unknown derived class",
                    pp->name,
                    rbp->name);
}

/*
 * cmi_resource_base_initialize : Make an already allocated resource core
 * object ready for use with a given capacity.
 */
void cmi_resource_base_initialize(struct cmi_resource_base *rbp,
                                  const char *name)
{
    cmb_assert_release(rbp != NULL);

    cmb_resource_base_set_name(rbp, name);
    rbp->scram = base_scram;
}

/*
 * cmi_resource_base_terminate : Un-initializes a resource base object.
 */
void cmi_resource_base_terminate(struct cmi_resource_base *rbp)
{
    cmi_unused(rbp);
}

/*
 * cmb_resource_set_name : Change the resource name.
 *
 * The name is contained in a fixed size buffer and will be truncated if it is
 * too long to fit into the buffer, leaving one char for the \0 at the end.
 */
void cmb_resource_base_set_name(struct cmi_resource_base *rbp, const char *name)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(name != NULL);

    const int r = snprintf(rbp->name, CMB_RESOURCE_NAMEBUF_SZ, "%s", name);
    cmb_assert_release(r >= 0);
}

/*******************************************************************************
 * cmb_resource : A simple resource object, formally a binary semaphore
 ******************************************************************************/

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
 * cmb_resource_initialize : Make an allocated resource object ready for use.
 */
#define HOLDER_INIT_EXP 3u

/*
 * resource_scram : forcibly eject holder process without resuming it.
 */
void resource_scram(struct cmi_resource_base *rbp,
                    const struct cmb_process *pp,
                    const uint64_t handle)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmi_unused(handle);

    struct cmb_resource *rp = (struct cmb_resource *)rbp;
    cmb_assert_debug(rp->holder == pp);
    rp->holder = NULL;

    cmi_resource_guard_signal(&(rp->front_guard));
}

void cmb_resource_initialize(struct cmb_resource *rp, const char *name)
{
    cmb_assert_release(rp != NULL);
    cmb_assert_release(name != NULL);

    cmi_resource_base_initialize(&(rp->core), name);
    cmi_resource_guard_initialize(&(rp->front_guard), &(rp->core));

    rp->core.scram = resource_scram;
    rp->holder = NULL;
}

/*
 * cmb_resource_terminate : Un-initializes a resource object.
 */
void cmb_resource_terminate(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    cmi_resource_guard_terminate(&(rp->front_guard));
    cmi_resource_base_terminate(&(rp->core));
}

/*
 * cmb_resource_destroy : Deallocates memory for a resource object.
 */
void cmb_resource_destroy(struct cmb_resource *rp)
{
    cmb_resource_terminate(rp);
    cmi_free(rp);
}

static bool resource_available(const struct cmi_resource_base *rbp,
                               const struct cmb_process *pp,
                               const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmi_unused(ctx);

    const struct cmb_resource *rp = (struct cmb_resource *)rbp;

    return (rp->holder == NULL);
}

static void resource_grab(struct cmb_resource *rp, struct cmb_process *pp)
{
    rp->holder = pp;
    struct cmi_resource_base *rbp = (struct cmi_resource_base *)rp;
    cmi_resourcetag_list_add(&(pp->resource_listhead), rbp, 0u);
}

int64_t cmb_resource_acquire(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    struct cmi_resource_base *rbp = (struct cmi_resource_base *)rp;
    cmb_logger_info(stdout, "Acquiring resource %s", rbp->name);

    struct cmb_process *pp = cmb_process_get_current();
    if (rp->holder == NULL) {
        /* Easy, grab it */
        resource_grab(rp, pp);
        cmb_logger_info(stdout, "Acquired %s", rbp->name);
        return CMB_RESOURCE_ACQUIRE_NORMAL;
    }

    /* Wait at the front door until resource becomes available */
     const int64_t ret = cmi_resource_guard_wait(&(rp->front_guard),
                                                resource_available,
                                                NULL);

    /* Now we got past the front door, or perhaps thrown out by the guard */
    if (ret == CMB_RESOURCE_ACQUIRE_NORMAL) {
        /* All good, grab the resource */
        resource_grab(rp, pp);
        cmb_logger_info(stdout, "Acquired %s", rbp->name);
    }
    else {
        cmb_logger_info(stdout,
                        "Cancelled from acquiring %s, code %lld",
                        rbp->name,
                        ret);
    }

    return ret;
}

/*
 * cmb_resource_release : Release a resource.
 */
void cmb_resource_release(struct cmb_resource *rp) {
    cmb_assert_release(rp != NULL);

    struct cmi_resource_base *rbp = (struct cmi_resource_base *)rp;
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_debug(pp != NULL);
    cmi_resourcetag_list_remove(&(pp->resource_listhead), rbp);

    cmb_assert_debug(rp->holder == pp);
    rp->holder = NULL;

    cmb_logger_info(stdout, "Released %s", rbp->name);
    struct cmi_resource_guard *rgp = &(rp->front_guard);
    cmi_resource_guard_signal(rgp);
}

int64_t cmb_resource_preempt(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    int64_t ret;
    struct cmb_process *pp = cmb_process_get_current();
    const int64_t myprio = pp->priority;
    struct cmi_resource_base *rbp = (struct cmi_resource_base *)rp;
    cmb_logger_info(stdout, "Preempting resource %s", rbp->name);

    struct cmb_process *victim = rp->holder;
    if (victim == NULL) {
        /* Easy, grab it */
        cmb_logger_info(stdout, "Preempt found %s free", rbp->name);
        resource_grab(rp, pp);
        ret = CMB_RESOURCE_ACQUIRE_NORMAL;
    }
    else if (myprio >= victim->priority) {
        /* Kick it out */
        cmi_resourcetag_list_remove(&(victim->resource_listhead), rbp);
        rp->holder = NULL;
        (void)cmb_event_schedule(prwuevt,
                                (void *)victim,
                                (void *)CMB_RESOURCE_HOLD_PREEMPTED,
                                 cmb_time(),
                                 victim->priority);

        /* Take its place */
        resource_grab(rp, pp);
        cmb_logger_info(stdout,
                       "Preempted %s from process %s",
                        rbp->name,
                        victim->name);
        ret = CMB_RESOURCE_ACQUIRE_NORMAL;
    }
    else {
        /* Wait politely at the front door until resource becomes available */
        cmb_logger_info(stdout,
                        "%s not preempted from %s, priority %lld > my priority %lld",
                         rbp->name,
                         victim->name,
                         victim->priority,
                         myprio);
        ret = cmb_resource_acquire(rp);
    }

    return ret;
}

/*******************************************************************************
 * cmb_store : Resource with integer-valued capacity, a counting semaphore
 ******************************************************************************/

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
void store_scram(struct cmi_resource_base *rbp,
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
    cmi_resource_guard_signal(&(sp->front_guard));
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

    cmi_resource_base_initialize(&(sp->core), name);
    sp->core.scram = store_scram;

    cmi_resource_guard_initialize(&(sp->front_guard), &(sp->core));
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
    cmi_resource_guard_terminate(&(sp->front_guard));
    cmi_resource_base_terminate(&(sp->core));
}

/*
 * cmb_store_destroy : Deallocates memory for a store object.
 */
void cmb_store_destroy(struct cmb_store *sp)
{
    cmb_store_terminate(sp);
    cmi_free(sp);
}



