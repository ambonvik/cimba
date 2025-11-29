/*
 * cmi_resource.c - a simple binary semaphore supporting acquire, release, and
 * preempt methods. Can only be held by one process at a time. Assigned to
 * waiting processes in priority order, then FIFO tie-breaker order.
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

#include "cmb_logger.h"
#include "cmb_resource.h"

#include "cmi_memutils.h"

/*
 * cmb_resource_create : Allocate memory for a resource object.
 */
struct cmb_resource *cmb_resource_create(void)
{
    struct cmb_resource *rp = cmi_malloc(sizeof(*rp));
    cmi_memset(rp, 0, sizeof(*rp));
    ((struct cmi_resourcebase *)rp)->cookie = CMI_UNINITIALIZED;

    return rp;
}

static void record_sample(struct cmb_resource *rp);

/*
 * drop_holder : force a holder process to drop the resource
 */
void drop_holder(struct cmi_holdable *hrp,
                 const struct cmb_process *pp,
                 const uint64_t handle)
{
    cmb_assert_release(hrp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)hrp)->cookie == CMI_INITIALIZED);
    cmb_assert_release(pp != NULL);
    cmb_unused(handle);

    struct cmb_resource *rp = (struct cmb_resource *)hrp;
    cmb_assert_debug(rp->holder == pp);
    rp->holder = NULL;
    record_sample(rp);
    cmi_resourceguard_signal(&(rp->guard));
}

/*
 * cmb_resource_initialize : Make an allocated resource object ready for use.
 */
#define HOLDER_INIT_EXP 3u

void cmb_resource_initialize(struct cmb_resource *rp, const char *name)
{
    cmb_assert_release(rp != NULL);
    cmb_assert_release(name != NULL);

    cmi_holdable_initialize(&(rp->core), name);
    cmi_resourceguard_initialize(&(rp->guard), &(rp->core.base));

    rp->core.drop = drop_holder;
    rp->holder = NULL;

    rp->is_recording = false;
    cmb_timeseries_initialize(&(rp->history));
}

/*
 * cmb_resource_terminate : Un-initializes a resource object.
 */
void cmb_resource_terminate(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    if (rp->holder != NULL) {
        drop_holder(&(rp->core), rp->holder, 0u);
    }

    cmb_timeseries_terminate(&(rp->history));
    cmi_resourceguard_terminate(&(rp->guard));
    cmi_holdable_terminate(&(rp->core));
}

/*
 * cmb_resource_destroy : Deallocates memory for a resource object.
 */
void cmb_resource_destroy(struct cmb_resource *rp)
{
    cmb_resource_terminate(rp);
    cmi_free(rp);
}

static void record_sample(struct cmb_resource *rp) {
    cmb_assert_release(rp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    if (rp->is_recording) {
        struct cmb_timeseries *ts = &(rp->history);
        const double x = (rp->holder != NULL) ? 1.0 : 0.0;
        const double t = cmb_time();
        cmb_timeseries_add(ts, x, t);
    }
}

void cmb_resource_start_recording(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    rp->is_recording = true;
    record_sample(rp);
}

void cmb_resource_stop_recording(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    record_sample(rp);
    rp->is_recording = false;
}

struct cmb_timeseries *cmb_resource_get_history(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return &(rp->history);
}

void cmb_resource_print_report(struct cmb_resource *rp, FILE *fp) {
    cmb_assert_release(rp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    fprintf(fp, "Resource utilization for %s:\n", rbp->name);
    const struct cmb_timeseries *ts = &(rp->history);
    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);

    cmb_timeseries_print_histogram(ts, fp, 2u, 0.0, 1.0);
}

/*
 * is_available : pre-packaged demand function for a cmb_resource
 */
static bool is_available(const struct cmi_resourcebase *rbp,
                         const struct cmb_process *pp,
                         const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmb_unused(ctx);

    const struct cmb_resource *rp = (struct cmb_resource *)rbp;

    return (rp->holder == NULL);
}

static void resource_grab(struct cmb_resource *rp, struct cmb_process *pp)
{
    rp->holder = pp;
    struct cmi_holdable *hrp = (struct cmi_holdable *)rp;
    cmi_list_add32(&(pp->resources_listhead), 0.0, 0u, hrp);
}

int64_t cmb_resource_acquire(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;
    cmb_logger_info(stdout, "Acquiring resource %s", rbp->name);

    struct cmb_process *pp = cmb_process_get_current();
    if (rp->holder == NULL) {
        /* Easy, grab it */
        resource_grab(rp, pp);
        record_sample(rp);
        cmb_logger_info(stdout, "Acquired %s", rbp->name);
        return CMB_PROCESS_SUCCESS;
    }

    /* Wait at the front door until resource becomes available */
     const int64_t ret = cmi_resourceguard_wait(&(rp->guard),
                                                is_available,
                                                NULL);

    /* Now we got past the front door, or perhaps thrown out by the guard */
    if (ret == CMB_PROCESS_SUCCESS) {
        /* All good, grab the resource */
        resource_grab(rp, pp);
        record_sample(rp);
        cmb_logger_info(stdout, "Acquired %s", rbp->name);
    }
    else {
        cmb_logger_info(stdout,
                        "Did not acquire %s, code %lld",
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
    cmb_assert_release(((struct cmi_resourcebase *)rp)->cookie == CMI_INITIALIZED);

    struct cmi_holdable *hrp = (struct cmi_holdable *)rp;
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_debug(pp != NULL);
    cmi_list_remove32(&(pp->resources_listhead), hrp);

    cmb_assert_debug(rp->holder == pp);
    rp->holder = NULL;
    record_sample(rp);

    cmb_logger_info(stdout, "Released %s", hrp->base.name);
    struct cmi_resourceguard *rgp = &(rp->guard);
    cmi_resourceguard_signal(rgp);
}

/*
 * prpwuevt : The event handler that actually resumes the process coroutine after
 * being scheduled by cmb_resource_preempt
 */
static void prpwuevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
}

int64_t cmb_resource_preempt(struct cmb_resource *rp)
{
    cmb_assert_release(rp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)rp)->cookie == CMI_INITIALIZED);

    int64_t ret;
    struct cmb_process *pp = cmb_process_get_current();
    const int64_t myprio = pp->priority;
    struct cmi_holdable *hrp = (struct cmi_holdable *)rp;
    cmb_logger_info(stdout, "Preempting resource %s", hrp->base.name);

    struct cmb_process *victim = rp->holder;
    if (victim == NULL) {
        /* Easy, grab it */
        cmb_logger_info(stdout, "Preempt found %s free", hrp->base.name);
        resource_grab(rp, pp);
        record_sample(rp);
        ret = CMB_PROCESS_SUCCESS;
    }
    else if (myprio >= victim->priority) {
        /* Kick it out. No record_sample needed, remains occupied. */
        cmi_list_remove32(&(victim->resources_listhead), hrp);
        rp->holder = NULL;
        (void)cmb_event_schedule(prpwuevt,
                                (void *)victim,
                                (void *)CMB_PROCESS_PREEMPTED,
                                 cmb_time(),
                                 victim->priority);

        /* Take its place */
        resource_grab(rp, pp);
        cmb_logger_info(stdout,
                       "Preempted %s from process %s",
                        hrp->base.name,
                        victim->name);
        ret = CMB_PROCESS_SUCCESS;
    }
    else {
        /* Wait politely at the front door until resource becomes available */
        cmb_logger_info(stdout,
                        "%s not preempted, holder %s priority %lld > my priority %lld",
                         hrp->base.name,
                         victim->name,
                         victim->priority,
                         myprio);
        ret = cmb_resource_acquire(rp);
    }

    return ret;
}
