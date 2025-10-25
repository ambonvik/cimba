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

#include "cmi_coroutine.h"
#include "cmi_memutils.h"
#include "cmi_processtag.h"

/*
 * struct cmb_process : Inherits all properties from struct cmi_coroutine by
 * composition and adds the name, priority, and the handle of wakeup event (if
 * the process is holding, i.e. scheduled for a wakeup event, otherwise zero).
 */
struct cmb_process {
    struct cmi_coroutine cr;
    char name[CMB_PROCESS_NAMEBUF_SZ];
    int64_t priority;
    uint64_t wakeup_handle;
    struct cmi_processtag *waiter_tag;
};


/*
 * cmb_process_create : Allocate memory for the process, including its
 * constituent coroutine with stack. Does not start the process yet.
 */
struct cmb_process *cmb_process_create(const char *name,
                                       cmb_process_func foo,
                                       void *context,
                                       const int64_t priority)
{
    /* Allocate memory and initialize the cmi_coroutine parts */
    struct cmb_process *pp = cmi_malloc(sizeof(*pp));
    cmi_memset(pp, 0, sizeof(*pp));
    cmi_coroutine_init((struct cmi_coroutine *)pp,
                       (cmi_coroutine_func *)foo,
                       context,
                       (cmi_coroutine_exit_func *)cmb_process_exit,
                       CMB_PROCESS_STACK_SIZE);

    /* Initialize the cmi_process parts */
    (void)cmb_process_set_name(pp, name);
    pp->priority = priority;
    pp->wakeup_handle = 0ull;
    pp->waiter_tag = NULL;

    return pp;
}

/*
 * cmb_process_destroy : Free allocated memory, including the coroutine stack
 * if present.
 */
void cmb_process_destroy(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);
    cmb_assert_release(pp->waiter_tag == NULL);

    const struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp != cmi_coroutine_get_main());
    cmb_assert_debug(cp != cmi_coroutine_get_current());
    cmb_assert_debug(cp->status != CMI_COROUTINE_RUNNING);

    if (cp->stack != NULL) {
        cmi_free(cp->stack);
    }

    cmi_free(pp);
}

/*
 * pstartevt : The event handler that actually starts the process
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

    cmb_logger_info(stdout, "Start %s", pp->name);
    const double t = cmb_time();
    const int64_t pri = pp->priority;

    cmb_event_schedule(pstartevt, pp, NULL, t, pri);
}

/*
 * cmb_process_get_name : Return the process name as a const char *
 *
 * If the name for some reason needs to be changed, use cmb_process_set_name to
 * do it safely.
 */
const char *cmb_process_get_name(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->name;
}

/*
 * cmb_process_set_name : Change the process name, returning a const char *
 * to the new name.
 *
 * Note that the name is contained in a fixed size buffer and may be truncated
 * if too long to fit into the buffer.
 */
const char *cmb_process_set_name(struct cmb_process *cp, const char *name)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(name != NULL);

    const int r = snprintf(cp->name, CMB_PROCESS_NAMEBUF_SZ, "%s", name);
    cmb_assert_release(r >= 0);

    return cp->name;
}

void *cmb_process_get_context(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return cmi_coroutine_get_context((struct cmi_coroutine *)pp);
}

void *cmb_process_set_context(struct cmb_process *pp, void *context)
{
    cmb_assert_release(pp != NULL);

    return cmi_coroutine_set_context((struct cmi_coroutine *)pp, context);
}

int64_t cmb_process_get_priority(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->priority;
}

int64_t cmb_process_set_priority(struct cmb_process *pp, const int64_t pri)
{
    cmb_assert_release(pp != NULL);

    const int64_t oldpri = pp->priority;
    pp->priority = pri;

    return oldpri;
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
 * phwuevt : The event that actually resumes the
 * process coroutine after being scheduled by cmb_process_hold.
 */
static void phwuevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    pp->wakeup_handle = 0ull;

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
    (void)cmi_coroutine_resume(cp, arg);
}

/*
 * cmb_process_hold : Suspend process for specified duration of simulated time.
 *
 * Returns 0 (CMB_PROCESS_HOLD_NORMAL) when returning normally after the
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
    cmb_assert_debug(pp->wakeup_handle == 0ull);
    cmb_logger_info(stdout, "Hold until time %f", t);

    /* Set an alarm clock */
    const int64_t pri = cmb_process_get_priority(pp);
    pp->wakeup_handle = cmb_event_schedule(phwuevt,
                                    pp, NULL, t, pri);

    /* Yield to the scheduler and collect the return signal value */
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /*
     * Back here again, possibly much later. Return whatever signal was passed
     * through the resume call that got us back here.
     */
    return ret;
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
    cmb_assert_debug(pp->wakeup_handle == 0ull);
    cmb_logger_info(stdout, "Wait for process %s", awaited->name);

    /* Is it already done? */
    const struct cmi_coroutine *cp = (struct cmi_coroutine *)awaited;
    if (cp->status == CMI_COROUTINE_FINISHED) {
        return CMB_PROCESS_WAIT_NORMAL;
    }

    cmi_processtag_list_add(&(awaited->waiter_tag), pp);

    /* Yield to the scheduler and collect the return signal value */
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /* Possibly much later */
    return ret;
}

/*
 * cmb_process_wait_event : Wait for an event to occur.
 */
int64_t cmb_process_wait_event(const uint64_t ev_handle)
{
    cmb_assert_release(ev_handle != 0ull);
    cmb_assert_release(cmb_event_is_scheduled(ev_handle));

    /* Friendly function in cmi_event.c, not part of public interface */
    extern struct cmi_processtag **cmi_event_tag_loc(uint64_t handle);

    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);

    struct cmi_processtag **loc = cmi_event_tag_loc(ev_handle);
    cmi_processtag_list_add(loc, pp);

    /* Yield to the scheduler and collect the return signal value */
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /* Possibly much later */
    return ret;
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

    if (pp->waiter_tag != NULL) {
        cmi_processtag_list_wake_all(&(pp->waiter_tag), CMB_PROCESS_WAIT_NORMAL);
    }

    cmi_coroutine_exit(retval);
}

/*
 * phintevt : The event handler that actually interrupts the
 * process coroutine after being scheduled by cmb_process_interrupt.
 *
 * Note that some other interrupt may have been scheduled in the meantime,
 * possibly executing first due to higher priority. If so, the target process
 * will already have been interrupted and no longer holding. That is not an
 * error but a foreseeable circumstance. Hence, check for it, issue a warning,
 * and otherwise do nothing.
 */
static void phintevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    cmb_assert_debug((int64_t)arg != CMB_PROCESS_HOLD_NORMAL);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    if (tgt->wakeup_handle != 0ull) {
        cmb_event_cancel(tgt->wakeup_handle);
        tgt->wakeup_handle = 0ull;
        struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
        cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
        (void)cmi_coroutine_resume(cp, arg);
    }
    else {
        /* Someone else got it first, no longer holding */
        cmb_logger_warning(stdout,
                          "phintevt: tgt %s not holding",
                          tgt->name);
    }
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
    cmb_logger_info(stdout, "Tnterrupt %s signal %lld priority %llu", pp->name, sig, pri);

    if (pp->wakeup_handle != 0ull) {
        const double t = cmb_time();
        (void)cmb_event_schedule(phintevt,
                                 pp, (void *)sig, t, pri);
    }
    else {
        cmb_logger_warning(stdout,
                  "cmb_process_interrupt: tgt %s not holding", pp->name);
    }
}

/*
 * pstopevt : The event handler that actually stops the process
 * coroutine after being scheduled by cmb_process_stop.
 */
static void pstopevt(void *vp, void *arg) {
    cmb_assert_debug(vp != NULL);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    if (tgt->wakeup_handle != 0ull) {
        cmb_event_cancel(tgt->wakeup_handle);
        tgt->wakeup_handle = 0ull;
    }
    else {
        cmb_logger_warning(stdout,
                          "pstopevt: tgt %s not holding",
                          tgt->name);
    }

    if (tgt->waiter_tag != NULL) {
        cmi_processtag_list_wake_all(&(tgt->waiter_tag), CMB_PROCESS_WAIT_STOPPED);
    }

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
 * The stop event will deal with any processes waiting for the target.
 */
void cmb_process_stop(struct cmb_process *pp, void *retval)
{
    cmb_assert_debug(pp != NULL);
    cmb_logger_info(stdout, "Stop %s value %p", pp->name, retval);

    const double t = cmb_time();
    const int16_t pri = 5;
    (void)cmb_event_schedule(pstopevt, pp, retval, t, pri);
}