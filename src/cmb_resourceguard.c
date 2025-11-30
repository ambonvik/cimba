/*
 * cmb_resourceguard.c - the gatekeeper class for resources a process can wait
 * for. It is derived from cmi_hashheap by composition and inherits its methods,
 * adding a pointer to the resource it guards.
 *
 * A process will register itself and a predicate demand function when first
 * joining the priority queue. The demand function evaluates whether the
 * necessary condition to grab the resource is in place, such as at least one
 * part being available in a store or a buffer space being available.
 *
 * When some other process signals the resource guard, it evaluates the demand
 * function for the first process in the priority queue. If true, the process is
 * resumed and can grab the resource. When done, the resumed process puts the
 * resource back and signals the guard to evaluate waiting demand functions
 * again.
 *
 * The hashheap entries provide four 64-bit payload fields. Usage:
 * item[0] - pointer to the waiting process
 * item[1] - pointer to its demand function
 * item[2] - its context pointer
 * item[3] - not used here
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

#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_resourceguard.h"
#include "cmb_resourcebase.h"

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

void cmb_resourceguard_initialize(struct cmb_resourceguard *rgp,
                                  struct cmb_resourcebase *rbp)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(rbp != NULL);

    cmi_hashheap_initialize((struct cmi_hashheap *)rgp,
                            GUARD_INIT_EXP,
                            guard_queue_check);

    rgp->guarded_resource = rbp;
    rgp->observers = NULL;
}

void cmb_resourceguard_terminate(struct cmb_resourceguard *rgp)
{
    cmb_assert_release(rgp != NULL);

    cmi_hashheap_terminate((struct cmi_hashheap *)rgp);
}

/*
 * cmb_resourceguard_wait : Enqueue and suspend the calling process until it
 * reaches the front of the priority queue and its demand function returns true.
 * ctx is whatever context the demand function needs to evaluate if it is
 * satisfied or not, such as the number of units needed from the resource or
 * something more complex and user application defined.
 * Returns whatever signal was received when the process was reactivated.
 * Cannot be called from the main process, will fire an assert if attempted.
 */
int64_t cmb_resourceguard_wait(struct cmb_resourceguard *rgp,
                               cmb_resourceguard_demand_func *demand,
                               const void *ctx)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(demand != NULL);

    /* cmb_process_get_current returns NULL if called from the main process */
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == 0ull);

    const double entry_time = cmb_time();
    const int64_t priority = cmb_process_get_priority(pp);

    const uint64_t handle = cmi_hashheap_enqueue((struct cmi_hashheap *)rgp,
                                                 (void *)pp,
                                                 (void *)demand,
                                                 (void *)ctx,
                                                 NULL,
                                                 entry_time,
                                                 priority);

    pp->waitsfor.type = CMI_WAITABLE_RESOURCE;
    pp->waitsfor.ptr = rgp;
    pp->waitsfor.handle = handle;

    cmb_logger_info(stdout,
                    "Waits in line for %s",
                    rgp->guarded_resource->name);

    /* Yield to the scheduler, collect the return signal value when resumed */
    const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

    /* Back here, possibly much later. Return the signal that resumed us. */
    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.ptr = NULL;
    pp->waitsfor.handle = 0ull;

    return sig;
}

/*
 * resgrd_waitwu_evt : The event that actually resumes the process coroutine
 */
static void resgrd_waitwu_evt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
}

/*
 * cmb_resourceguard_signal : Plings the bell for a resource guard to check if
 * any of the waiting processes should be resumed. Will evaluate the demand
 * function for the first process in the queue, if any, and will resume it if
 * (and only if) its demand function (*demand)(pp, rp, ctx) returns true.
 *
 * Resumes zero or one waiting processes. Call it again if there is a chance
 * that more than one process could be ready, e.g. if some process just returned
 * five units of a resource and there are several processes waiting for one
 * unit each. This does not allow priority inversion where lower-priority
 * processes could monopolize the resource while a higher-priority process is
 * starved.
 *
 * In cases where some waiting process needs to bypass another, e.g. if there
 * are three available units of the resource, the first process in the queue
 * demands five, and there are three more behind it that demand one each, it is
 * up to the application to dynamically change process priorities to bring the
 * correct process to the front of the queue (and manage the risk of successive
 * lower-priority processes permanently starving the higher-priority one).
 */
bool cmb_resourceguard_signal(struct cmb_resourceguard *rgp)
{
    cmb_assert_release(rgp != NULL);

    struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
    if (cmi_hashheap_is_empty(hp)) {
        return false;
    }

    /* Decode first entry in the hashheap */
    void **item = cmi_hashheap_peek_item(hp);
    struct cmb_process *pp = item[0];
    cmb_resourceguard_demand_func *demand = item[1];
    const void *ctx = item[2];

    /* Evaluate its demand predicate */
    bool ret = false;
    const struct cmb_resourcebase *rbp = rgp->guarded_resource;
    if ((*demand)(rbp, pp, ctx)) {
        /* Yes, pull the process off the queue and schedule a wakeup event */
        (void)cmi_hashheap_dequeue(hp);
        const double time = cmb_time();
        const int64_t priority = cmb_process_get_priority(pp);
        (void)cmb_event_schedule(resgrd_waitwu_evt,
                                 pp,
                                 (void *)CMB_PROCESS_SUCCESS,
                                 time,
                                 priority);
        ret = true;
    }

    /* Forward the signal to any observers */
    const struct cmi_list_tag16 *tmp = rgp->observers;
    while (tmp != NULL) {
        struct cmb_resourceguard *obs = (struct cmb_resourceguard *)tmp->ptr;
        cmb_resourceguard_signal(obs);
        tmp = tmp->next;
    }

    return ret;
}

/*
 * cmb_resourceguard_cancel : Remove this process from the priority queue and
 * schedule a wakeup event with a CMB_PROCESS_CANCELLED signal.
 * Returns true if found and successfully cancelled, false if not.
 */
bool cmb_resourceguard_cancel(struct cmb_resourceguard *rgp,
                               struct cmb_process *pp)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(pp != NULL);

    bool ret = false;
    struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
    const uint64_t handle = pp->waitsfor.handle;
    if (handle != 0u) {
        (void)cmi_hashheap_cancel(hp, handle);
        const double time = cmb_time();
        const int64_t priority = cmb_process_get_priority(pp);
        (void)cmb_event_schedule(resgrd_waitwu_evt,
                                 pp,
                                 (void *)CMB_PROCESS_CANCELLED,
                                 time,
                                 priority);
        ret = true;
    }

    return ret;
}

/*
 * cmb_resourceguard_remove : Remove this process from the priority queue.
 * Returns true if found and successfully removed, false if not.
 */
bool cmb_resourceguard_remove(struct cmb_resourceguard *rgp,
                              const struct cmb_process *pp)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(pp != NULL);

    bool ret = false;
    struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
    const uint64_t handle = pp->waitsfor.handle;
    if (handle != 0u) {
        (void)cmi_hashheap_cancel(hp, handle);
        ret = true;
    }

    return ret;
}

/*
 * cmb_resourceguard_register : Register another resource guard as an observer
 * of this one, forwarding signals and causing the observer to evaluate its
 * demand predicates as well.
 *
 * When registering observers, do not create any cycles where e.g. condition A
 * gets signalled from B, B gets signalled from C, and C gets signalled from A.
 * That will not end well.
 */
void cmb_resourceguard_register(struct cmb_resourceguard *rgp,
                                struct cmb_resourceguard *obs)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(obs != NULL);

    cmi_list_add16(&(rgp->observers), obs);
}

/*
 * cmb_resourceguard_unregister : Un-register another resource guard as an observer
 * of this one, forwarding signals and causing the observer to evaluate its
 * demand predicates as well. Returns true if the found, false if not.
 */
bool cmb_resourceguard_unregister(struct cmb_resourceguard *rgp,
                                  struct cmb_resourceguard *obs)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(obs != NULL);

    return cmi_list_remove16(&(rgp->observers), obs);
}

