/*
 * cmb_process.c - the simulated processes
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
#include <stdio.h>

#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "cmi_coroutine.h"
#include "cmi_holdable.h"
#include "cmi_mempool.h"
#include "cmi_memutils.h"
#include "cmi_process.h"
#include "cmb_resource.h"

CMB_THREAD_LOCAL struct cmi_mempool cmi_process_awaitabletags = {
    CMI_THREAD_STATIC,
    sizeof(struct cmi_process_awaitable),
    128u,
    0u, 0u, 0u, NULL, NULL
};

CMB_THREAD_LOCAL struct cmi_mempool cmi_process_holdabletags = {
    CMI_THREAD_STATIC,
    sizeof(struct cmi_process_holdable),
    128u,
    0u, 0u, 0u, NULL, NULL
};

CMB_THREAD_LOCAL struct cmi_mempool cmi_process_waitertags = {
    CMI_THREAD_STATIC,
    sizeof(struct cmi_process_waiter),
    256u,
    0u, 0u, 0u, NULL, NULL
};

/*
 * cmb_process_create - Allocate memory for the process.
 */
struct cmb_process *cmb_process_create(void)
{
    struct cmb_process *pp = cmi_malloc(sizeof(*pp));
    cmi_memset(pp, 0, sizeof(*pp));

    return pp;
}

/*
 * cmb_process_initialize - Set up a process object and allocate its
 * coroutine stack. Does not start the process yet.
 */
void cmb_process_initialize(struct cmb_process *pp,
                                       const char *name,
                                       cmb_process_func foo,
                                       void *context,
                                       const int64_t priority)
{
    cmb_assert_release(pp != NULL);

    cmi_coroutine_initialize((struct cmi_coroutine *)pp,
                       (cmi_coroutine_func *)foo,
                       context,
                       (cmi_coroutine_exit_func *)cmb_process_exit,
                       CMB_PROCESS_STACK_SIZE);

    (void)cmb_process_set_name(pp, name);
    pp->priority = priority;

    cmi_slist_initialize(&pp->awaits);
    cmi_slist_initialize(&pp->waiters);
    cmi_slist_initialize(&pp->resources);
}

/*
 * cmb_process_terminate - Deallocates memory for the underlying coroutine stack
 * but not for the process object itself. The process exit value is still there.
 *
 * If necessary, the terminated process can still be restarted by first calling
 * _initialize and then _start, but until a use case for this is identified, we
 * do not bother to provide a cmb_process_reset function.
 */
extern void cmb_process_terminate(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    /* Should not have any waiters or hold any resources at this point */
    cmi_slist_terminate(&pp->awaits);
    cmi_slist_terminate(&pp->waiters);
    cmi_slist_terminate(&pp->resources);

    cmi_coroutine_terminate((struct cmi_coroutine *)pp);
}

/*
 * cmb_process_destroy - Free allocated memory, including the coroutine stack
 * if still present.
 */
void cmb_process_destroy(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    cmb_process_terminate(pp);
    cmi_free(pp);
}

/*
 * process_start_event - The event that actually starts the process
 * coroutine after being scheduled by cmb_process_start.
 */
static void process_start_event(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmi_coroutine_start(cp, arg);
}

/*
 * cmb_process_start - Schedule a start event for the process
 */
void cmb_process_start(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    const double t = cmb_time();
    const int64_t pri = pp->priority;

    const uint64_t handle = cmb_event_schedule(process_start_event, pp, NULL, t, pri);
    cmb_logger_info(stdout, "Scheduled start event %" PRIu64 " %s %p",
                    handle, pp->name, (void *)pp);
}

/*
 * cmb_process_set_name - Change the process name, returning a const char *
 * to the new name.
 *
 * Note that the name is contained in a fixed size buffer and may be truncated
 * if too long to fit into the buffer.
 */
void cmb_process_set_name(struct cmb_process *cp, const char *name)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(name != NULL);

    const int r = snprintf(cp->name, CMB_PROCESS_NAMEBUF_SZ, "%s", name);
    cmb_assert_release(r >= 0);
}

void cmb_process_set_context(struct cmb_process *pp, void *context)
{
    cmb_assert_release(pp != NULL);

    cmi_coroutine_set_context((struct cmi_coroutine *)pp, context);
}

void cmb_process_set_priority(struct cmb_process *pp, const int64_t pri)
{
    cmb_assert_release(pp != NULL);

    cmb_logger_info(stdout, "Changes priority from %" PRIi64 " to %" PRIi64,
                    pp->priority, pri);
    pp->priority = pri;

    /* Any priority queues containing this process? */
    const struct cmi_slist_head *ahead = &(pp->awaits);
    while (ahead->next != NULL) {
        const struct cmi_process_awaitable *awp = cmi_container_of(ahead->next,
                                                   struct cmi_process_awaitable,
                                                   listhead);
        if (awp->type == CMI_PROCESS_AWAITABLE_TIME) {
            /* Either in a hold() or a scheduled timer, reshuffle event queue */
            cmb_assert_debug(awp->handle != UINT64_C(0));
            cmb_event_reprioritize(awp->handle, pri);
        }
        else if (awp->type == CMI_PROCESS_AWAITABLE_RESOURCE) {
            /* Waiting for some resource, reshuffle resource guard queue */
            cmb_assert_debug(awp->handle != UINT64_C(0));
            struct cmb_resourceguard *rgp = awp->ptr;
            const struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
            const double dkey = cmi_hashheap_dkey(hp, awp->handle);
            cmi_hashheap_reprioritize(hp, awp->handle, dkey, pri);
        }
        else {
            /* Something else, not a priority queue */
            cmb_assert_debug(awp->handle == UINT64_C(0));
        }

        ahead = ahead->next;
    }
    /* Is this process holding any resources that need to update records? */
    const struct cmi_slist_head *rhead = &(pp->resources);
    while (rhead->next != NULL) {
        const struct cmi_process_holdable *hrp = cmi_container_of(rhead->next,
                                                    struct cmi_process_holdable,
                                                    listhead);
        cmb_assert_debug(hrp != NULL);
        if (hrp->handle != UINT64_C(0)) {
            (*(hrp->res->reprio))(hrp->res, hrp->handle, pri);
        }

        rhead = rhead->next;
    }
}

/*
 * cmb_process_exit_value - Returns the stored exit value from the process,
 * as set by cmb_process_exit, cmb_process_stop, or simply returned by the
 * process function. Will return NULL if the process has not yet finished.
 */
void *cmb_process_exit_value(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    const struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cmi_coroutine_status(cp) != CMI_COROUTINE_FINISHED) {
        cmb_logger_warning(stdout,
                           "Requested exit value but process %s has not yet finished",
                           pp->name);
        return NULL;
    }

    return cmi_coroutine_exit_value(cp);
}

void cmi_process_add_awaitable(struct  cmb_process *pp,
                               const enum cmi_process_awaitable_type type,
                               void *awaitable,
                               const uint64_t handle)
{
    cmb_assert_debug(pp != NULL);

    cmb_logger_info(stdout, "Adding awaitable %p %" PRIu64 " type %d", awaitable, handle, type);
    struct cmi_process_awaitable *awp = cmi_mempool_alloc(&cmi_process_awaitabletags);
    awp->type = type;
    awp->ptr = awaitable;
    awp->handle = handle;

    struct cmi_slist_head *head = &(pp->awaits);
    cmi_slist_push(head, &(awp->listhead));
}

bool cmi_process_remove_awaitable(struct cmb_process *pp,
                                  const enum cmi_process_awaitable_type type,
                                  const void *awaitable,
                                  const uint64_t handle)
{
    cmb_assert_debug(pp != NULL);

    cmb_logger_info(stdout, "Removing awaitable %p %" PRIu64 " type %d", awaitable, handle, type);
    struct cmi_slist_head *ahead = &(pp->awaits);
    while (ahead->next != NULL) {
        struct cmi_process_awaitable *awp = cmi_container_of(ahead->next,
                                                  struct cmi_process_awaitable,
                                                  listhead);
        if ((awp->type == type)
            && ((awaitable == NULL) || (awp->ptr == awaitable))
            && ((handle == 0u) || (awp->handle == handle))) {
            (void)cmi_slist_pop(ahead);
            cmi_mempool_free(&cmi_process_awaitabletags, awp);
            return true;
        }
        ahead = ahead->next;
    }

    return false;
}

/*
 * process_wakeup_event_time - The event that resumes the process after being scheduled by
 *           cmb_process_timer or cmb_process_hold
 */
static void process_wakeup_event_time(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    struct cmb_process *pp = (struct cmb_process *)vp;

    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64, pp->name, (int64_t)arg);
    cmb_assert_debug(!cmi_slist_is_empty(&(pp->awaits)));

    const bool found = cmi_process_remove_awaitable(pp,
                                                    CMI_PROCESS_AWAITABLE_TIME,
                                                    NULL, 0u);
    cmb_assert_debug(found == true);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_hold - Suspend a process for a specified duration of
 * simulated time.
 *
 * Returns 0 (CMB_PROCESS_SUCCESS) when returning normally after the
 * specified duration, something else if not.
 */
int64_t cmb_process_hold(const double dur)
{
    cmb_assert_release(dur >= 0.0);

    /* Will return NULL for the main coroutine, not a cmb_process */
    struct cmb_process *pp = cmb_process_current();
    cmb_assert_debug(pp != NULL);
    cmb_logger_info(stdout, "Hold for %f", dur);

    /* Not already holding, are we? */
    if (!cmi_slist_is_empty(&(pp->awaits))) {
        const struct cmi_process_awaitable *aw = cmi_container_of(pp->awaits.next, struct cmi_process_awaitable, listhead);
        cmb_logger_warning(stdout, "Awaiting %p %" PRIu64 " type %d", aw->ptr, aw->handle, aw->type);
    }
    cmb_assert_debug(cmi_slist_is_empty(&(pp->awaits)));
    const double t = cmb_time() + dur;

    /* Schedule a wakeup event and add it to our list */
    const int64_t pri = cmb_process_priority(pp);
    const uint64_t handle = cmb_event_schedule(process_wakeup_event_time, pp, NULL, t, pri);
    cmi_process_add_awaitable(pp, CMI_PROCESS_AWAITABLE_TIME, NULL, handle);
    cmb_logger_info(stdout, "Scheduled timeout event %" PRIu64 " at %f", handle, t);

    /* Yield to the dispatcher and collect the return signal value */
    const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

    /* Back here again, possibly much later. */
    if (sig != CMB_PROCESS_SUCCESS) {
        /* Whatever woke us up was not the scheduled wakeup call */
        cmb_logger_info(stdout, "Woken up by signal %" PRIi64, sig);
        cmi_process_remove_awaitable(pp, CMI_PROCESS_AWAITABLE_TIME, NULL, handle);
        if (cmb_event_is_scheduled(handle)) {
            cmb_event_cancel(handle);
        }
    }

    return sig;
}

/*
 * process_wakeup_event_process - The event that resumes the process after being scheduled by
 *           cmb_process_wait_process
 */
static void process_wakeup_event_process(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    struct cmb_process *pp = (struct cmb_process *)vp;

    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64, pp->name, (int64_t)arg);
    cmb_assert_debug(!cmi_slist_is_empty(&(pp->awaits)));

    /* Cannot be waiting for more than one process */
    const bool found = cmi_process_remove_awaitable(pp,
                                                    CMI_PROCESS_AWAITABLE_PROCESS,
                                                    NULL, 0u);
    cmb_assert_debug(found == true);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
    else {
         cmb_logger_warning(stdout,
                           "Process wait wakeup call found process %s dead",
                           cmb_process_name(pp));
    }
}

static void add_waiter_tag(struct cmi_slist_head *head, struct cmb_process *waiter)
{
    cmb_assert_debug(head != NULL);
    cmb_assert_debug(waiter != NULL);

    struct cmi_process_waiter *waiter_tag = cmi_mempool_alloc(&cmi_process_waitertags);
    waiter_tag->proc = waiter;
    cmi_slist_push(head, &(waiter_tag->listhead));
}

/*
 * cmb_process_wait_process - Wait for some other process (awaited) to finish.
 * Returns immediately if the awaited process already is finished. Note that
 * we cannot assert that the awaits list is empty here, since we may have set a
 * timer as a timeout on this call.
 */
int64_t cmb_process_wait_process(struct cmb_process *awaited)
{
    cmb_assert_release(awaited != NULL);

    struct cmb_process *me = cmb_process_current();
    cmb_assert_release(me != NULL);

    cmb_logger_info(stdout, "Wait for process %s", awaited->name);

    if (cmb_process_status(awaited) == CMB_PROCESS_FINISHED) {
        /* Already done, nothing to wait for */
        return CMB_PROCESS_SUCCESS;
    }
    else {
        /* Wait for it to finish, register it both here and there */
        cmi_process_add_awaitable(me, CMI_PROCESS_AWAITABLE_PROCESS, awaited, 0u);
        add_waiter_tag(&(awaited->waiters), me);

        /* Yield to the dispatcher and collect the return signal value */
        const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

        /* Possibly much later */
        return sig;
    }
}

/* Friendly functions in cmi_event.c, not part of the public interface */
extern void cmi_event_add_waiter(uint64_t handle, struct cmb_process *pp);
extern bool cmi_event_remove_waiter(uint64_t handle, const struct cmb_process *pp);

/*
 * cmb_process_wait_event - Wait for an event to occur.
 */
int64_t cmb_process_wait_event(const uint64_t ev_handle)
{
    cmb_assert_release(ev_handle != UINT64_C(0));
    cmb_assert_release(cmb_event_is_scheduled(ev_handle));

    /* Cannot be called from the main process, which will return NULL here */
    struct cmb_process *me = cmb_process_current();
    cmb_assert_release(me != NULL);

    cmb_logger_info(stdout, "Waiting for event %" PRIu64, ev_handle);

    /* Add the current process to the list of processes waiting for the event */
    cmi_event_add_waiter(ev_handle, me);

    /* Add the event to our list of things to be waited for */
    cmi_process_add_awaitable(me, CMI_PROCESS_AWAITABLE_EVENT, NULL, ev_handle);

    /* Yield to the dispatcher and collect the return signal value */
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /* Possibly much later */
    return ret;
}

static void wake_process_waiters(struct cmi_slist_head *waiters,
                                 const int64_t signal)
{
    cmb_assert_debug(waiters != NULL);

    while (!cmi_slist_is_empty(waiters)) {
        struct cmi_slist_head *head = cmi_slist_pop(waiters);
        struct cmi_process_waiter *pw = cmi_container_of(head,
                                                      struct cmi_process_waiter,
                                                      listhead);
        struct cmb_process *pp = pw->proc;
        cmb_assert_debug(pp != NULL);
        const double time = cmb_time();
        const int64_t priority = cmb_process_priority(pp);
        (void)cmb_event_schedule(process_wakeup_event_process, pp, (void *)signal,
                                 time, priority);
        cmi_mempool_free(&cmi_process_waitertags, pw);
    }

    cmb_assert_debug(cmi_slist_is_empty(waiters));
}

void cmi_process_drop_resources(struct cmb_process *pp)
{
    cmb_assert_debug(pp != NULL);

    struct cmi_slist_head *held = &(pp->resources);
    while (!cmi_slist_is_empty(held)) {
        struct cmi_slist_head *head = cmi_slist_pop(held);
        struct cmi_process_holdable *ph = cmi_container_of(head,
                                                    struct cmi_process_holdable,
                                                    listhead);
        struct cmi_holdable *hrp = ph->res;
        const uint64_t handle = ph->handle;
        (*(hrp->drop))(hrp, pp, handle);

        cmi_mempool_free(&cmi_process_holdabletags, ph);
     }

    cmb_assert_debug(cmi_slist_is_empty(held));
}

bool cmi_process_remove_waiter(struct cmb_process *pp,
                               const struct cmb_process *waiter)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(waiter != NULL);

    struct cmi_slist_head *waiters = &(pp->waiters);
    cmb_assert_debug(!cmi_slist_is_empty(waiters));

    while (waiters->next != NULL) {
        struct cmi_process_waiter *pw = cmi_container_of(waiters->next,
                                                  struct cmi_process_waiter,
                                                  listhead);
        if (pw->proc == waiter) {
            cmi_slist_pop(waiters);
            cmi_mempool_free(&cmi_process_waitertags, pw);
            return true;
        }
        else {
            waiters = waiters->next;
        }
    }

    return false;
}

bool cmi_process_remove_holdable(struct cmb_process *pp,
                                 const struct cmi_holdable *holdable)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(holdable != NULL);

    struct cmi_slist_head *held = &(pp->resources);

    bool found = false;
    while (held->next != NULL) {
        struct cmi_process_holdable *ph = cmi_container_of(held->next,
                                                  struct cmi_process_holdable,
                                                  listhead);
        if (ph->res == holdable) {
            found = true;
            cmi_slist_pop(held);
            cmi_mempool_free(&cmi_process_holdabletags, ph);
        }
        else {
            held = held->next;
        }
    }

    return found;
}

void cmi_process_cancel_awaiteds(struct cmb_process *pp)
{
    cmb_assert_debug(pp != NULL);

    struct cmi_slist_head *awaits = &(pp->awaits);
    while (!cmi_slist_is_empty(awaits)) {
        struct cmi_slist_head *head = cmi_slist_pop(awaits);
        struct cmi_process_awaitable *pa = cmi_container_of(head,
                                                      struct cmi_process_awaitable,
                                                      listhead);

        if (pa->type == CMI_PROCESS_AWAITABLE_TIME) {
            /* Waits for some timeout (hold or timer), cancel it */
            cmb_assert_debug(pa->ptr == NULL);
            cmb_assert_debug(pa->handle != UINT64_C(0));
            cmb_logger_info(stdout, "Cancels timeout event %" PRIu64, pa->handle);
            const bool found = cmb_event_cancel(pa->handle);
            cmb_logger_info(stdout, "Event %" PRIu64 " %s",
                            pa->handle, found ? "found" : "not found");
        }
        else if (pa->type == CMI_PROCESS_AWAITABLE_RESOURCE) {
            cmb_assert_debug(pa->handle != UINT64_C(0));
            cmb_assert_debug(pa->ptr != NULL);
            struct cmb_resourceguard *rgp = pa->ptr;
            cmb_logger_info(stdout, "Cancels resource %s", rgp->guarded_resource->name);
            const bool found = cmb_resourceguard_remove(rgp, pp);
            cmb_logger_info(stdout, "Resource %s %s", rgp->guarded_resource->name, found ? "found" : "not found");
        }
        else if (pa->type == CMI_PROCESS_AWAITABLE_PROCESS) {
            /* Waits for a process to end, remove ourselves from the waiter list */
            cmb_assert_debug(pa->handle == UINT64_C(0));
            cmb_assert_debug(pa->ptr != NULL);
            struct cmb_process *pw = (struct cmb_process *)pa->ptr;
            cmb_logger_info(stdout, "Cancels wait for process %s", pw->name);
            const bool found = cmi_process_remove_waiter(pw, pp);
            cmb_logger_info(stdout, "Process %s %s", pw->name, found ? "found" : "not found");
        }
        else if (pa->type == CMI_PROCESS_AWAITABLE_EVENT) {
            /* Waits for a specific event, remove ourselves from the event's list */
            cmb_assert_debug(pa->ptr == NULL);
            cmb_assert_debug(pa->handle != UINT64_C(0));
            cmb_logger_info(stdout, "Cancels wait for event %" PRIu64, pa->handle);
            const bool found = cmi_event_remove_waiter(pa->handle, pp);
            cmb_logger_info(stdout, "Event %" PRIu64 " %s", pa->handle, found ? "found" : "not found");
        }

        /* Recycle the tag */
        cmi_mempool_free(&cmi_process_awaitabletags, pa);
    }

    /* Make sure any previously scheduled wakeup event does not happen */
    cmb_event_pattern_cancel(CMB_ANY_ACTION, pp, CMB_ANY_OBJECT);
}

/*
 * process_wakeup_event_interrupt - The event that actually interrupts the
 * process coroutine after being scheduled by cmb_process_interrupt.
 */
static void process_wakeup_event_interrupt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    cmb_assert_debug((int64_t)arg != CMB_PROCESS_SUCCESS);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Interrupts %s signal %" PRIi64,
                    tgt->name, (int64_t)arg);

    /* Interrupt it from whatever it is doing or waiting for */
    cmi_process_cancel_awaiteds(tgt);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_interrupt - Schedule an interrupt event for the target process
 * at the current time with priority pri. Non-blocking call, it will return to
 * the calling process immediately.
 */
void cmb_process_interrupt(struct cmb_process *pp,
                           const int64_t sig,
                           const int64_t pri)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(sig != 0);
    cmb_logger_info(stdout, "Interrupt %s signal %" PRIi64 " priority %" PRIi64,
                    pp->name, sig, pri);

    const double t = cmb_time();
    (void)cmb_event_schedule(process_wakeup_event_interrupt, pp, (void *)sig, t, pri);
}

/*
 * cmb_process_exit - Terminate the current process with the given return value.
 */
void cmb_process_exit(void *retval)
{
    /* Will return NULL if called from the main coroutine (dispatcher) */
    struct cmb_process *pp = cmb_process_current();
    cmb_assert_debug(pp != NULL);

    cmb_logger_info(stdout, "Exit with value %p", retval);
    cmi_process_drop_resources(pp);
    cmi_process_cancel_awaiteds(pp);
    wake_process_waiters(&(pp->waiters), CMB_PROCESS_SUCCESS);
    cmi_coroutine_exit(retval);
}

/*
 * cmb_process_stop - Terminate the target process by scheduling a stop event.
 * Sets the target process exit value to the argument value retval. The meaning
 * of return values for an externally terminated process is application defined.
 *
 * Does not transfer control to the target process. Does not destroy its memory
 * allocation. The target process can be restarted from the beginning by calling
 * cmb_process_start(tgt) again.
 *
 * The stop event will deal with any processes waiting for the target and
 * release any resources held by the target.
 */
void cmb_process_stop(struct cmb_process *tgt, void *retval)
{
    cmb_assert_debug(tgt != NULL);
    cmb_logger_info(stdout, "Stop %s value %p", tgt->name, retval);

    if (cmb_process_status(tgt) != CMB_PROCESS_RUNNING) {
        cmb_logger_warning(stdout, "cmb_process_stop: tgt %s not running", tgt->name);
        return;
    }

    /* Stop the underlying coroutine, set its exit value */
    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    cmi_coroutine_stop(cp, retval);

    /* Clean up unfinished business */
    cmi_process_cancel_awaiteds(tgt);
    cmi_process_drop_resources(tgt);
    wake_process_waiters(&(tgt->waiters), CMB_PROCESS_STOPPED);
}