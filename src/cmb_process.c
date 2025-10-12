/*
 * cmi_process.c - the simulated processes
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

#include "cmi_memutils.h"

/*
 * struct cmb_process : Inherits all properties from struct cmi_coroutine by
 * composition and adds the name, priority, and handle of wakeup event (if the
 * process is holding, i.e. scheduled for a wakeup event, otherwise zero).
 */
#define CMB_PROCESS_NAMEBUF_SZ 32
struct cmb_process {
    struct cmi_coroutine cr;
    char name[CMB_PROCESS_NAMEBUF_SZ];
    int16_t priority;
    uint64_t wakeup_handle;
};

struct cmb_process *cmb_process_create(const char *name,
                                       cmb_process_func foo,
                                       void *context,
                                       int16_t priority)
{
    /* Allocate memory and initialize the cmi_coroutine parts */
    struct cmb_process *pp = cmi_malloc(sizeof(*pp));
    cmi_coroutine_init((struct cmi_coroutine *)pp,
                       (cmi_coroutine_func *)foo,
                       context,
              CMB_PROCESS_STACK_SIZE);

    /* Initialize the cmi_process parts */
    (void)cmb_process_set_name(pp, name);
    pp->priority = priority;
    pp->wakeup_handle = 0ull;

    return pp;
}

void cmb_process_destroy(struct cmb_process *pp)
{
    cmb_assert_debug(pp != NULL);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmb_assert_debug(cp != cmi_coroutine_get_main());
    cmb_assert_debug(cp != cmi_coroutine_get_current());

    if (cp->stack != NULL) {
        cmi_free(cp->stack);
    }

    cmi_free(pp);
}

static void process_start_event(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    cmi_coroutine_start(cp, arg);
}

static void process_wakeup_event(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    pp->wakeup_handle = 0ull;

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    (void)cmi_coroutine_resume(cp, arg);
}

static void process_interrupt_event(void *vp, void *arg)
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
        cmb_logger_info(stdout, "process_interrupt_event: tgt %s not holding", tgt->name);
    }
}

static void process_stop_event(void *vp, void *arg) {
    cmb_assert_debug(vp != NULL);

    struct cmb_process *tgt = (struct cmb_process *)vp;
    if (tgt->wakeup_handle != 0ull) {
        cmb_event_cancel(tgt->wakeup_handle);
        tgt->wakeup_handle = 0ull;
    }

    struct cmi_coroutine *cp = (struct cmi_coroutine *)tgt;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        cmi_coroutine_stop(cp, arg);
    }
    else {
        cmb_logger_warning(stdout, "process_stop_event: tgt %s not running", tgt->name);
    }
 }


void cmb_process_start(struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    cmb_logger_info(stdout, "Starting %s", pp->name);
    const double t = cmb_time();
    const int16_t pri = pp->priority;

    cmb_event_schedule(process_start_event, pp, NULL, t, pri);
}

const char *cmb_process_get_name(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->name;
}

char *cmb_process_set_name(struct cmb_process *cp, const char *name)
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

int16_t cmb_process_get_priority(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->priority;
}

int16_t cmb_process_set_priority(struct cmb_process *pp, const int16_t pri)
{
    cmb_assert_release(pp != NULL);

    const int16_t oldpri = pp->priority;
    pp->priority = pri;

    return oldpri;
}

void *cmb_process_get_exit_value(const struct cmb_process *pp)
{
    return cmi_coroutine_get_exit_value((struct cmi_coroutine *)pp);
}

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

int64_t cmb_process_hold(const double dur)
{
    cmb_assert_release(dur >= 0.0);

    /* Schedule a wakeup call at time + dur and yield */
    const double t = cmb_time() + dur;
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_debug(pp != NULL);
    /* Not already holding, are we? */
    cmb_assert_debug(pp->wakeup_handle == 0ull);
    cmb_logger_info(stdout, "Holding until time %f", t);

    const int16_t pri = cmb_process_get_priority(pp);
    pp->wakeup_handle = cmb_event_schedule(process_wakeup_event,
                                    pp, NULL, t, pri);

    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    return ret;
}

void cmb_process_exit(void *retval)
{
    cmb_logger_info(stdout, "Exiting, value %p", retval);

    cmi_coroutine_exit(retval);
}

/*
 * cmb_process_interrupt : Schedule an interrupt event for the target process
 * at the current time with priority pri. Non-blocking call, returns to the
 * calling process immediately.
 */
void cmb_process_interrupt(struct cmb_process *pp,
                           const int64_t sig,
                           const int16_t pri)
{
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(sig != 0);
    cmb_logger_info(stdout, "Tnterrupting %s with signal %lld priority %d", pp->name, sig, pri);

    if (pp->wakeup_handle != 0ull) {
        const double t = cmb_time();
        (void)cmb_event_schedule(process_interrupt_event,
                                 pp, (void *)sig, t, pri);
    }
    else {
        cmb_logger_warning(stdout,
                  "cmb_process_interrupt: tgt %s not holding", pp->name);
    }
}

void cmb_process_stop(struct cmb_process *pp, void *retval)
{
    cmb_assert_debug(pp != NULL);
    cmb_logger_info(stdout, "Stopping %s value %p", pp->name, retval);

    const double t = cmb_time();
    const int16_t pri = 5;
    (void)cmb_event_schedule(process_stop_event, pp, retval, t, pri);
}