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
 * cmb_process_initialize : Set up process object and allocate coroutine stack.
 * Does not start the process yet.
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
    pp->waitsfor.handle = 0ull;
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

    /* todo: make sure we recycle any waiting tags here */
    pp->waiters_listhead = NULL;
    pp->resources_listhead = NULL;

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

void *cmb_process_get_context(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return cmi_coroutine_get_context((struct cmi_coroutine *)pp);
}

void cmb_process_set_context(struct cmb_process *pp, void *context)
{
    cmb_assert_release(pp != NULL);

    cmi_coroutine_set_context((struct cmi_coroutine *)pp, context);
}

int64_t cmb_process_get_priority(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->priority;
}

void cmb_process_set_priority(struct cmb_process *pp, const int64_t pri)
{
    cmb_assert_release(pp != NULL);

    const int64_t oldpri = pp->priority;
    pp->priority = pri;
    cmb_logger_info(stdout, "Changed priority from %lld to %lld", oldpri, pri);

    /* Any priority queues containing this process? */
    if (pp->waitsfor.type == CMI_WAITABLE_CLOCK) {
        const uint64_t handle = pp->waitsfor.handle;
        cmb_assert_debug(handle != 0ull);
        cmb_event_reprioritize(handle, pri);
    }
    else if (pp->waitsfor.type == CMI_WAITABLE_RESOURCE) {
        const uint64_t handle = pp->waitsfor.handle;
        cmb_assert_debug(handle != 0ull);
        struct cmi_resourceguard *rgp = pp->waitsfor.ptr;
        const struct cmi_hashheap *hp = (struct cmi_hashheap *)rgp;
        const double dkey = cmi_hashheap_get_dkey(hp, handle);
        const int64_t check = cmi_hashheap_get_ikey(hp, handle);
        cmb_assert_debug(check == oldpri);
        cmi_hashheap_reprioritize(hp, handle, dkey, pri);
    }

    /* Is this process holding any resources that need to update records? */
    struct cmi_list_tag32 *rtag = pp->resources_listhead;
    while (rtag != NULL) {
        struct cmi_holdable *hrp = rtag->ptr;
        cmb_assert_debug(hrp != NULL);
        const uint64_t handle = rtag->uint;
        if (handle != 0ull) {
            (*(hrp->reprio))(hrp, handle, pri);
        }

        rtag = rtag->next;
    }
}

/*
 * cmb_process_get_exit_value : Returns the stored exit value from the process,
 * as set by cmb_process_exit, cmb_process_stop, or simply returned by the
 * process function. Will return NULL if the process has not yet finished.
 */
void *cmb_process_get_exit_value(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    const struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cmi_coroutine_get_status(cp) != CMI_COROUTINE_FINISHED) {
        cmb_logger_warning(stdout,
                           "Requested exit value but process %s has not yet finished",
                           pp->name);
    }

    /* Will just return NULL if not yet finished */
    return cmi_coroutine_get_exit_value(cp);
}

/*
 * cmb_process_get_current : Returns a pointer to the currently executing
 * process, i.e. the calling process itself. Returns NULL if called from outside
 * a named process, such as the main process that executes the event scheduler.
 */
struct cmb_process *cmb_process_get_current(void)
{
    struct cmb_process *rp;
    const struct cmi_coroutine *cp = cmi_coroutine_get_current();
    const struct cmi_coroutine *mp = cmi_coroutine_get_main();
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
 * phwuevt : The event that resumes the process after being scheduled by
 *           cmb_process_hold.
 */
static void phwuevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.ptr = NULL;
    pp->waitsfor.handle = 0ull;

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_hold : Suspend process for specified duration of simulated time.
 *
 * Returns 0 (CMB_PROCESS_SUCCESS) when returning normally after the
 * specified duration, something else if not.
 */
int64_t cmb_process_hold(const double dur)
{
    cmb_assert_release(dur >= 0.0);

    /* Schedule a wakeup call at time + dur and yield */
    const double t = cmb_time() + dur;
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_debug(pp != NULL);

    /* Not already holding, are we? */
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == 0ull);
    cmb_logger_info(stdout, "Hold until time %f", t);

    /* Set an alarm clock */
    const int64_t pri = cmb_process_get_priority(pp);
    pp->waitsfor.type = CMI_WAITABLE_CLOCK;
    pp->waitsfor.handle = cmb_event_schedule(phwuevt,
                                    pp, NULL, t, pri);

    /* Yield to the scheduler and collect the return signal value */
    const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

    /* Back here again, possibly much later. */
    if (sig != CMB_PROCESS_SUCCESS) {
        /* Whatever woke us up was not the scheduled wakeup call */
        cmb_logger_info(stdout, "Woken up, signal %lld", sig);
        if (pp->waitsfor.handle != 0ull) {
            /* Should be handled already by wakeup event, but just in case */
            cmb_event_cancel(pp->waitsfor.handle);
        }
    }

    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.handle = 0ull;

    return sig;
}

/*
 * ptwuevt : The event that resumes the process after being scheduled by
 *           cmb_process_wait_*.
 */
static void ptwuevt(void *vp, void *arg)
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
 * cmb_process_wait_process : Wait for some other process (awaited) to finish.
 * Returns immediately if the awaited process already is finished.
 */
int64_t cmb_process_wait_process(struct cmb_process *awaited)
{
    cmb_assert_release(awaited != NULL);

    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == 0ull);
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
        cmi_list_add16(&(awaited->waiters_listhead), pp);

        /* Yield to the scheduler and collect the return signal value */
        const int64_t sig = (int64_t)cmi_coroutine_yield(NULL);

        /* Possibly much later */
        pp->waitsfor.type = CMI_WAITABLE_NONE;
        pp->waitsfor.ptr = NULL;
        return sig;
    }
}

/* A friendly function in cmi_event.c, not part of public interface */
extern struct cmi_list_tag16 **cmi_event_tag_loc(uint64_t handle);

/*
 * cmb_process_wait_event : Wait for an event to occur.
 */
int64_t cmb_process_wait_event(const uint64_t ev_handle)
{
    cmb_assert_release(ev_handle != 0ull);
    cmb_assert_release(cmb_event_is_scheduled(ev_handle));

    /* Cannot be called from main process, which will return NULL here */
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_NONE);
    cmb_assert_debug(pp->waitsfor.ptr == NULL);
    cmb_assert_debug(pp->waitsfor.handle == 0ull);

    /* Add the current process to the list of processes waiting for the event */
    struct cmi_list_tag16 **loc = cmi_event_tag_loc(ev_handle);
    cmi_list_add16(loc, pp);

    /* Yield to the scheduler and collect the return signal value */
    pp->waitsfor.type = CMI_WAITABLE_EVENT;
    pp->waitsfor.handle = ev_handle;
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /* Possibly much later */
    pp->waitsfor.type = CMI_WAITABLE_NONE;
    pp->waitsfor.handle = 0ull;
    return ret;
}

/* Note: extern scope as an internal function, used by cmb_event.c */
void cmi_process_wake_all(struct cmi_list_tag16 **ptloc, const int64_t signal)
{
    cmb_assert_debug(ptloc != NULL);

    /* Unlink the tag chain */
    struct cmi_list_tag16 *ptag = *ptloc;
    *ptloc = NULL;

    /* Process it, scheduling a wakeup call for each process */
    while (ptag != NULL) {
        struct cmb_process *pp = ptag->ptr;
        cmb_assert_debug(pp != NULL);
        const double time = cmb_time();
        const int64_t priority = cmb_process_get_priority(pp);
        (void)cmb_event_schedule(ptwuevt,
                                 pp,
                                 (void *)signal,
                                 time,
                                 priority);

        struct cmi_list_tag16 *tmp = ptag->next;
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
    struct cmb_process *pp = cmb_process_get_current();
    const struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp != cmi_coroutine_get_main());

    cmb_logger_info(stdout, "Exit with value %p", retval);

    if (pp->waiters_listhead != NULL) {
        cmi_process_wake_all(&(pp->waiters_listhead), CMB_PROCESS_SUCCESS);
    }

    cmi_coroutine_exit(retval);
}

static void stop_waiting(struct cmb_process *tgt)
{
    cmb_assert_debug(tgt != NULL);

    if (tgt->waitsfor.type == CMI_WAITABLE_NONE) {
        cmb_assert_debug(tgt->waitsfor.ptr == NULL);
        cmb_assert_debug(tgt->waitsfor.handle == 0ull);

        /* Nothing to do */
        return;
    }

    if (tgt->waitsfor.type == CMI_WAITABLE_CLOCK) {
        cmb_event_cancel(tgt->waitsfor.handle);
    }
    else if (tgt->waitsfor.type == CMI_WAITABLE_EVENT) {
        struct cmi_list_tag16 **loc = cmi_event_tag_loc(tgt->waitsfor.handle);
        const bool found = cmi_list_remove16(loc, tgt);
        cmb_assert_debug(found == true);
    }
    else if (tgt->waitsfor.type == CMI_WAITABLE_PROCESS) {
        const bool found = cmi_list_remove16(&(tgt->waiters_listhead), tgt);
        cmb_assert_debug(found == true);
    }
    else if (tgt->waitsfor.type == CMI_WAITABLE_RESOURCE) {
        struct cmi_resourceguard *rgp = tgt->waitsfor.ptr;
        const bool found = cmi_resourceguard_remove(rgp, tgt);
        cmb_assert_debug(found == true);
    }

    tgt->waitsfor.type = CMI_WAITABLE_NONE;
    tgt->waitsfor.ptr = NULL;
    tgt->waitsfor.handle = 0ull;
}

/*
 * phintevt : The event handler that actually interrupts the
 * process coroutine after being scheduled by cmb_process_interrupt.
 */
static void phintevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    cmb_assert_debug((int64_t)arg != CMB_PROCESS_SUCCESS);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    stop_waiting(tgt);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_interrupt : Schedule an interrupt event for the target process
 * at the current time with priority pri. Non-blocking call, returns to the
 * calling process immediately.
 */
void cmb_process_interrupt(struct cmb_process *pp,
                           const int64_t sig,
                           const int64_t pri)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(sig != 0);
    cmb_logger_info(stdout, "Interrupt %s signal %lld priority %lld", pp->name, sig, pri);

    const double t = cmb_time();
    (void)cmb_event_schedule(phintevt, pp, (void *)sig, t, pri);
}

void cmi_process_drop_all(struct cmb_process *pp, struct cmi_list_tag32 **rtloc)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(rtloc != NULL);

    /* Unlink the tag chain */
    struct cmi_list_tag32 *rtag = *rtloc;
    *rtloc = NULL;

    /* Process it, calling scram() for each resource */
    while (rtag != NULL) {
        struct cmi_holdable *hrp = rtag->ptr;
        const uint64_t handle = rtag->uint;
        (*(hrp->drop))(hrp, pp, handle);

        struct cmi_list_tag32 *tmp = rtag->next;
        cmi_mempool_put(&cmi_mempool_32b, rtag);
        rtag = tmp;
    }

    cmb_assert_debug(*rtloc == NULL);
}

/*
 * pstopevt : The event handler that actually stops the process
 * coroutine after being scheduled by cmb_process_stop.
 */
static void pstopevt(void *vp, void *arg) {
    cmb_assert_debug(vp != NULL);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    stop_waiting(tgt);

    /* Release any resources held by this process */
    if (tgt->resources_listhead != NULL) {
        cmi_process_drop_all(tgt, &(tgt->resources_listhead));
    }

    /* Wake up any processes waiting for this process to finish */
    if (tgt->waiters_listhead != NULL) {
        cmi_process_wake_all(&(tgt->waiters_listhead), CMB_PROCESS_STOPPED);
    }

    /* Stop the underlying coroutine */
    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        cmi_coroutine_stop(cp, arg);
    }
    else {
        cmb_logger_warning(stdout,
                           "pstopevt: tgt %s not running",
                           tgt->name);
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

    /* Make sure all normal events happen first to ensure consistent state */
    const int64_t pri = INT64_MIN;
    const double t = cmb_time();
    (void)cmb_event_schedule(pstopevt, pp, retval, t, pri);
}