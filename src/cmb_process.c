/*
 * cmb_process.c - the simulated processes
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
#include "cmb_process.h"

#include "cmb_resource.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"

/*
 * cmb_process_create : Allocate memory for the process.
 */
struct cmb_process *cmb_process_create(void)
{
    struct cmb_process *pp = cmi_malloc(sizeof(*pp));
    cmi_memset(pp, 0, sizeof(*pp));

    return pp;
}

/*
 * cmb_process_initialize : Set up a process object and allocate its
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
    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.ptr = NULL;
    pp->waitsfor.handle = UINT64_C(0);
    pp->waiters_listhead = NULL;
    pp->resources_listhead = NULL;
}

/*
 * cmb_process_terminate : Deallocates memory for the underlying coroutine stack
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
    while (pp->resources_listhead != NULL) {
        /* It had, just recycle the tags */
        struct cmi_list_tag32 *rtag = pp->resources_listhead;
        pp->resources_listhead = rtag->next;
        cmi_mempool_put(&cmi_mempool_32b, rtag);
    }

    cmi_coroutine_terminate((struct cmi_coroutine *)pp);
}

/*
 * cmb_process_destroy : Free allocated memory, including the coroutine stack
 * if still present.
 */
void cmb_process_destroy(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    cmb_process_terminate(pp);
    cmi_free(pp);
}

/*
 * pstartevt : The event that actually starts the process
 * coroutine after being scheduled by cmb_process_start.
 */
static void pstartevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmi_coroutine_start(cp, arg);
}

/*
 * cmb_process_start : Schedule a start event for the process
 */
void cmb_process_start(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    cmb_logger_info(stdout, "Start %s %p", pp->name, (void *)pp);
    const double t = cmb_time();
    const int64_t pri = pp->priority;

    cmb_event_schedule(pstartevt, pp, NULL, t, pri);
}

/*
 * cmb_process_set_name : Change the process name, returning a const char *
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

void *cmb_process_context(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return cmi_coroutine_context((struct cmi_coroutine *)pp);
}

void cmb_process_set_context(struct cmb_process *pp, void *context)
{
    cmb_assert_release(pp != NULL);

    cmi_coroutine_set_context((struct cmi_coroutine *)pp, context);
}

int64_t cmb_process_priority(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->priority;
}

void cmb_process_set_priority(struct cmb_process *pp, const int64_t pri)
{
    cmb_assert_release(pp != NULL);

    const int64_t oldpri = pp->priority;
    pp->priority = pri;
    cmb_logger_info(stdout, "Changed priority from %" PRIi64 " to %" PRIi64,
                    oldpri, pri);

    /* Any priority queues containing this process? */
    if (pp->waitsfor.type == CMI_WAITABLE_CLOCK) {
        const uint64_t handle = pp->waitsfor.handle;
        cmb_assert_debug(handle != UINT64_C(0));
        cmb_event_reprioritize(handle, pri);
    }
    else if (pp->waitsfor.type == CMI_WAITABLE_RESOURCE) {
        const uint64_t handle = pp->waitsfor.handle;
        cmb_assert_debug(handle != UINT64_C(0));
        struct cmb_resourceguard *rgp = pp->waitsfor.ptr;
        const struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
        const double dkey = cmi_hashheap_dkey(hp, handle);
        const int64_t check = cmi_hashheap_ikey(hp, handle);
        cmb_assert_debug(check == oldpri);
        cmi_hashheap_reprioritize(hp, handle, dkey, pri);
    }

    /* Is this process holding any resources that need to update records? */
    const struct cmi_list_tag32 *rtag = pp->resources_listhead;
    while (rtag != NULL) {
        struct cmi_holdable *hrp = (struct cmi_holdable *)rtag->ptr;
        cmb_assert_debug(hrp != NULL);
        const uint64_t handle = rtag->uint;
        if (handle != UINT64_C(0)) {
            (*(hrp->reprio))(hrp, handle, pri);
        }

        rtag = rtag->next;
    }
}

/*
 * cmb_process_exit_value : Returns the stored exit value from the process,
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
    }

    /* Will just return NULL if not yet finished */
    return cmi_coroutine_exit_value(cp);
}

/*
 * cmb_process_current : Returns a pointer to the currently executing
 * process, i.e., the calling process itself. Returns NULL if called from outside
 * a named process, such as the main process that executes the event dispatcher.
 */
struct cmb_process *cmb_process_current(void)
{
    struct cmb_process *rp;
    const struct cmi_coroutine *cp = cmi_coroutine_current();
    const struct cmi_coroutine *mp = cmi_coroutine_main();
    if (cp != mp) {
        rp = (struct cmb_process *)cp;
    }
    else {
        /* The main coroutine is not defined as a named process */
        rp = NULL;
    }

    return rp;
}

/*
 * proc_holdwu_evt : The event that resumes the process after being scheduled by
 *           cmb_process_hold.
 */
static void proc_holdwu_evt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64 " wait type %d",
                    pp->name, (int64_t)arg, pp->waitsfor.type);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_CLOCK);

    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.ptr = NULL;
    pp->waitsfor.handle = UINT64_C(0);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_hold : Suspend a process for a specified duration of
 * simulated time.
 *
 * Returns 0 (CMB_PROCESS_SUCCESS) when returning normally after the
 * specified duration, something else if not.
 */
int64_t cmb_process_hold(const double dur)
{
    cmb_assert_release(dur >= 0.0);

    /* Schedule a wakeup call at time + dur and yield */
    const double t = cmb_time() + dur;
    struct cmb_process *pp = cmb_process_current();
    cmb_assert_debug(pp != NULL);

    /* Not already holding, are we? */
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == UINT64_C(0));
    cmb_logger_info(stdout, "Hold until time %f", t);

    /* Set an alarm clock */
    const int64_t pri = cmb_process_priority(pp);
    pp->waitsfor.type = CMI_WAITABLE_CLOCK;
    pp->waitsfor.handle = cmb_event_schedule(proc_holdwu_evt,
                                    pp, NULL, t, pri);

    /* Yield to the dispatcher and collect the return signal value */
    const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

    /* Back here again, possibly much later. */
    if (sig != CMB_PROCESS_SUCCESS) {
        /* Whatever woke us up was not the scheduled wakeup call */
        cmb_logger_info(stdout, "Woken up, signal %" PRIi64, sig);
        if (pp->waitsfor.handle != UINT64_C(0)) {
            /* Should be handled already by wakeup event, but just in case */
            cmb_event_cancel(pp->waitsfor.handle);
        }
    }

    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.handle = UINT64_C(0);

    return sig;
}

/*
 * proc_waitwu_evt : The event that resumes the process after being scheduled by
 *           cmb_process_wait_process or cmb_process_wait_event
 */
static void proc_waitwu_evt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64 " wait type %d",
                    pp->name, (int64_t)arg, pp->waitsfor.type);
    cmb_assert_debug((pp->waitsfor.type == CMI_WAITABLE_PROCESS)
                     || (pp->waitsfor.type == CMI_WAITABLE_EVENT));

    struct cmi_coroutine *cp = (struct cmi_coroutine *)vp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
    else {
         cmb_logger_warning(stdout,
                           "process wait wakeup call found process %s dead",
                           cmb_process_name(pp));
    }
}

/*
 * cmb_process_wait_process : Wait for some other process (awaited) to finish.
 * Returns immediately if the awaited process already is finished.
 */
int64_t cmb_process_wait_process(struct cmb_process *awaited)
{
    cmb_assert_release(awaited != NULL);

    struct cmb_process *pp = cmb_process_current();
    cmb_assert_release(pp != NULL);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == UINT64_C(0));
    cmb_logger_info(stdout, "Wait for process %s", awaited->name);

    /* Is it already done? */
    const struct cmi_coroutine *cp = (struct cmi_coroutine *)awaited;
    if (cp->status == CMI_COROUTINE_FINISHED) {
        return CMB_PROCESS_SUCCESS;
    }
    else {
        /* Nope, register it both here and there */
        pp->waitsfor.type = CMI_WAITABLE_PROCESS;
        pp->waitsfor.ptr = awaited;
        cmi_list_push(&(awaited->waiters_listhead), pp);

        /* Yield to the dispatcher and collect the return signal value */
        const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

        /* Possibly much later */
        pp->waitsfor.type = CMI_WAITABLE_NONE;
        pp->waitsfor.ptr = NULL;
        return sig;
    }
}

/* A friendly function in cmi_event.c, not part of the public interface */
extern struct cmi_list_tag **cmi_event_tag_loc(uint64_t handle);

/*
 * cmb_process_wait_event : Wait for an event to occur.
 */
int64_t cmb_process_wait_event(const uint64_t ev_handle)
{
    cmb_assert_release(ev_handle != UINT64_C(0));
    cmb_assert_release(cmb_event_is_scheduled(ev_handle));

    /* Cannot be called from the main process, which will return NULL here */
    struct cmb_process *pp = cmb_process_current();
    cmb_assert_release(pp != NULL);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == UINT64_C(0));

    /* Add the current process to the list of processes waiting for the event */
    struct cmi_list_tag **loc = cmi_event_tag_loc(ev_handle);
    cmi_list_push(loc, pp);

    /* Yield to the dispatcher and collect the return signal value */
    pp->waitsfor.type = CMI_WAITABLE_EVENT;
    pp->waitsfor.handle = ev_handle;
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /* Possibly much later */
    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.handle = UINT64_C(0);
    return ret;
}

/* Note: extern scope as an internal function, used by cmb_event.c */
void cmi_process_wake_all(struct cmi_list_tag **ptloc, const int64_t signal)
{
    cmb_assert_debug(ptloc != NULL);

    /* Unlink the tag chain */
    struct cmi_list_tag *ptag = *ptloc;
    *ptloc = NULL;

    /* Process it, scheduling a wakeup call for each process */
    while (ptag != NULL) {
        struct cmb_process *pp = ptag->ptr;
        cmb_assert_debug(pp != NULL);
        const double time = cmb_time();
        const int64_t priority = cmb_process_priority(pp);
        (void)cmb_event_schedule(proc_waitwu_evt,
                                 pp,
                                 (void *)signal,
                                 time,
                                 priority);

        struct cmi_list_tag *tmp = ptag->next;
        cmi_mempool_put(&cmi_mempool_16b, ptag);
        ptag = tmp;
    }

    cmb_assert_debug(*ptloc == NULL);
}

/*
 * cmb_process_exit : Terminate the current process with the given return value.
 */
void cmb_process_exit(void *retval)
{
    struct cmb_process *pp = cmb_process_current();
    const struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp != cmi_coroutine_main());

    cmb_logger_info(stdout, "Exit with value %p", retval);

    if (pp->waiters_listhead != NULL) {
        cmi_process_wake_all(&(pp->waiters_listhead), CMB_PROCESS_SUCCESS);
    }

    cmi_coroutine_exit(retval);
}

void cmi_process_drop_all(struct cmb_process *pp, struct cmi_list_tag32 **rtloc)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(rtloc != NULL);

    /* Unlink the tag chain */
    struct cmi_list_tag32 *rtag = *rtloc;
    *rtloc = NULL;

    /* Process it, calling drop() for each resource */
    while (rtag != NULL) {
        struct cmi_holdable *hrp = (struct cmi_holdable *)rtag->ptr;
        const uint64_t handle = rtag->uint;
        (*(hrp->drop))(hrp, pp, handle);

        struct cmi_list_tag32 *tmp = rtag->next;
        cmi_mempool_put(&cmi_mempool_32b, rtag);
        rtag = tmp;
    }

    cmb_assert_debug(*rtloc == NULL);
}

static void stop_waiting(struct cmb_process *tgt)
{
    cmb_assert_debug(tgt != NULL);

    if (tgt->waitsfor.type == CMI_WAITABLE_NONE) {
        cmb_assert_debug(tgt->waitsfor.ptr == NULL);
        cmb_assert_debug(tgt->waitsfor.handle == UINT64_C(0));

        /* Nothing to do */
        return;
    }

    if (tgt->waitsfor.type == CMI_WAITABLE_CLOCK) {
        cmb_assert_debug(tgt->waitsfor.ptr == NULL);
        cmb_assert_debug(tgt->waitsfor.handle != UINT64_C(0));
        cmb_event_cancel(tgt->waitsfor.handle);
    }
    else if (tgt->waitsfor.type == CMI_WAITABLE_EVENT) {
        cmb_assert_debug(tgt->waitsfor.ptr == NULL);
        cmb_assert_debug(tgt->waitsfor.handle != UINT64_C(0));
        struct cmi_list_tag **loc = cmi_event_tag_loc(tgt->waitsfor.handle);
        const bool found = cmi_list_remove(loc, tgt);
        cmb_assert_debug(found == true);
    }
    else if (tgt->waitsfor.type == CMI_WAITABLE_PROCESS) {
        cmb_assert_debug(tgt->waitsfor.handle == UINT64_C(0));
        cmb_assert_debug(tgt->waitsfor.ptr != NULL);
        const bool found = cmi_list_remove(&(tgt->waiters_listhead), tgt);
        cmb_assert_debug(found == true);
    }
    else if (tgt->waitsfor.type == CMI_WAITABLE_RESOURCE) {
        cmb_assert_debug(tgt->waitsfor.handle != UINT64_C(0));
        cmb_assert_debug(tgt->waitsfor.ptr != NULL);
        struct cmb_resourceguard *rgp = tgt->waitsfor.ptr;
        const bool found = cmb_resourceguard_remove(rgp, tgt);
        cmb_assert_debug(found == true);
    }

    /* Make sure any kind of already scheduled wakeup event does not happen */
    cmb_event_pattern_cancel(CMB_ANY_ACTION, tgt, CMB_ANY_OBJECT);

    /* Set to a known state, essentially in limbo */
    tgt->waitsfor.type = CMI_WAITABLE_NONE;
    tgt->waitsfor.ptr = NULL;
    tgt->waitsfor.handle = UINT64_C(0);
}

/*
 * proc_intrpt_evt : The event that actually interrupts the
 * process coroutine after being scheduled by cmb_process_interrupt.
 */
static void proc_intrpt_evt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    cmb_assert_debug((int64_t)arg != CMB_PROCESS_SUCCESS);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Interrupts %s signal %" PRIi64 " wait type %d",
                    tgt->name, (int64_t)arg, tgt->waitsfor.type);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_interrupt : Schedule an interrupt event for the target process
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

    /* Take it to a known consistent state before scheduling event to resume */
    stop_waiting(pp);

    const double t = cmb_time();
    (void)cmb_event_schedule(proc_intrpt_evt, pp, (void *)sig, t, pri);
}

/*
 * proc_stop_evt : The event that actually stops the process
 * coroutine after being scheduled by cmb_process_stop.
 */
static void proc_stop_evt(void *vp, void *arg) {
    cmb_assert_debug(vp != NULL);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Stops %s signal %" PRIi64 " wait type %d",
                    tgt->name, (int64_t)arg, tgt->waitsfor.type);

    /* Stop the underlying coroutine */
    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        cmi_coroutine_stop(cp, arg);
    }
    else {
        cmb_logger_warning(stdout, "proc_stop_evt: tgt %s not running", tgt->name);
    }

    /* Wake up any processes waiting for this process to finish */
    if (tgt->waiters_listhead != NULL) {
        cmi_process_wake_all(&(tgt->waiters_listhead), CMB_PROCESS_STOPPED);
    }

    /* Release any resources held by this process */
    if (tgt->resources_listhead != NULL) {
        cmi_process_drop_all(tgt, &(tgt->resources_listhead));
    }
}

/*
 * cmb_process_stop : Terminate the target process by scheduling a stop event.
 * Sets the target process exit value to the argument value retval. The meaning
 * of return values for an externally terminated process is application defined.
 *
 * Does not transfer control to the target process. Does not destroy its memory
 * allocation. The target process can be restarted from the beginning by calling
 * cmb_process_start(pp) again.
 *
 * The stop event will deal with any processes waiting for the target and
 * release any resources held by the target.
 */
void cmb_process_stop(struct cmb_process *pp, void *retval)
{
    cmb_assert_debug(pp != NULL);
    cmb_logger_info(stdout, "Stop %s value %p", pp->name, retval);

    /* Take it to a known consistent state before scheduling event to stop */
    stop_waiting(pp);

    const int64_t pri = pp->priority;
    const double t = cmb_time();
    (void)cmb_event_schedule(proc_stop_evt, pp, retval, t, pri);
}