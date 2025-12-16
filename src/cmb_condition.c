/*
* cmb_condition.c : A condition variable class that allows a process to wait for an
 *        arbitrary condition to become true and be reactivated at that point.
 *        It does not assign any resource, just signals that the condition is
 *        fulfilled. The application provides the demand predicate function to
 *        be evaluated.
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

#include "cmb_condition.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"

struct cmb_condition *cmb_condition_create(void)
{
    struct cmb_condition *r = cmi_malloc(sizeof *r);
    cmi_memset(r, 0, sizeof *r);

    return r;
}

void cmb_condition_initialize(struct cmb_condition *cvp,
                              const char *name)
{
    cmb_assert_release(cvp != NULL);
    cmb_assert_release(name != NULL);

    cmi_resourcebase_initialize((struct cmi_resourcebase *)cvp, name);
    cmb_resourceguard_initialize(&(cvp->guard), (struct cmi_resourcebase *)cvp);
}

void cmb_condition_terminate(struct cmb_condition *cvp)
{
    cmb_assert_release(cvp != NULL);

    cmb_resourceguard_terminate(&(cvp->guard));
    cmi_resourcebase_terminate((struct cmi_resourcebase *)cvp);
}

void cmb_condition_destroy(struct cmb_condition *cvp)
{
    cmb_assert_release(cvp != NULL);

    cmb_condition_terminate(cvp);
    cmi_free(cvp);
}

int64_t cmb_condition_wait(struct cmb_condition *cvp,
                           cmb_condition_demand_func *dmnd,
                           const void *ctx)
{
    cmb_assert_release(cvp != NULL);
    cmb_assert_release(dmnd != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)cvp;
    cmb_logger_info(stdout, "Waiting for condition %s", rbp->name);
    int64_t sig =  cmb_resourceguard_wait(&(cvp->guard),
                                          (cmb_resourceguard_demand_func *)dmnd,
                                          ctx);

    cmb_logger_info(stdout, "Condition %s returning signal %" PRIi64, rbp->name, sig);

    return sig;
}

/*
 * cond_waitwu_evt : The event that actually resumes the process coroutine
 */
static void cond_waitwu_evt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64 " wait type %d",
            pp->name, (int64_t)arg, pp->waitsfor.type);
    cmb_assert_debug(pp->waitsfor.type == CMI_WAITABLE_RESOURCE);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
}

/*
 * Two-pass approach to avoid mutate-while-iterate bugs: First iterate over the
 * hashheap, note the handles for any that evaluate to true, schedule
 * reactivation event, then remove those from the hashheap.
 *
 * Note that this may lead to spurious wakeups, since we cannot know what the
 * first process to resume will do to whatever state that determines the
 * condition. Each resumed process will need to loop on its demand predicate and
 * wait again if not true.
 *
 * Also note that this runs atomically within one thread, since none of the
 * resumed processes (coroutines) will get the CPU before the one running this
 * chooses to yield it. Hence, safe to schedule the wakeup events in the first
 * iteration where the process pointers are easily available.
 */
bool cmb_condition_signal(struct cmb_condition *cvp)
{
    cmb_assert_release(cvp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)cvp;
    cmb_logger_info(stdout, "Signalling condition %s", rbp->name);

    uint64_t cnt = 0u;
    struct cmi_hashheap *hp = (struct cmi_hashheap *)&(cvp->guard);
    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        cmb_logger_info(stdout, "None waiting for %s", rbp->name);
        return false;
    }

    /* Allocate space enough to reactivate everything in the heap */
    uint64_t *tmp = cmi_malloc(hp->heap_count * sizeof(*tmp));

    /* First pass, recording the satisfied demand predicates */
    for (uint64_t ui = 1; ui <= hp->heap_count; ui++) {
        /* Decode the hashheap item */
        struct cmi_heap_tag *htp = &(hp->heap[ui]);
        void **item = htp->item;
        struct cmb_process *pp = item[0];
        cmb_condition_demand_func *demand = item[1];
        const void *ctx = item[2];

        if ((*demand)(cvp, pp, ctx)) {
            /* Satisfied, note it on the list, schedule wakeup event */
            cmb_logger_info(stdout, "Condition %s satisfied for process %s",
                            rbp->name, pp->name);
            tmp[cnt++] = htp->handle;
            const double time = cmb_time();
            const int64_t priority = cmb_process_get_priority(pp);
            (void)cmb_event_schedule(cond_waitwu_evt, pp,
                                     (void *)CMB_PROCESS_SUCCESS,
                                     time, priority);
        }
    }

    /* Second pass, remove the satisfied waiters from the hashheap */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        cmi_hashheap_remove(hp, tmp[ui]);
    }

    cmi_free(tmp);
    return (cnt > 0u);
}

bool cmi_condition_cancel(struct cmb_condition *cvp,
                          struct cmb_process *pp)
{
    cmb_assert_release(cvp != NULL);
    cmb_assert_release(pp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)cvp;
    cmb_logger_info(stdout, "Cancelling condition %s for process %s",
                    rbp->name, pp->name);

    return cmb_resourceguard_cancel((struct cmb_resourceguard *)cvp, pp);
}

bool cmi_condition_remove(struct cmb_condition *cvp,
                          const struct cmb_process *pp)
{
    cmb_assert_release(cvp != NULL);
    cmb_assert_release(pp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)cvp;
    cmb_logger_info(stdout, "Removing process %s from condition %s",
                    pp->name, rbp->name);

    return cmb_resourceguard_remove((struct cmb_resourceguard *)cvp, pp);
}
