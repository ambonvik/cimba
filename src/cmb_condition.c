/*
* cmb_condition.c - A condition variable class that allows a process to wait for an
 *        arbitrary condition to become true and be reactivated at that point.
 *        It does not assign any resource, just signals that the condition is
 *        fulfilled. The application provides the demand predicate function to
 *        be evaluated.
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

#include "cmb_condition.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"
#include "cmi_process.h"

/* Temporary index buffer for signaling, static thread local for efficiency */
static CMB_THREAD_LOCAL uint64_t *signal_buf = NULL;
static CMB_THREAD_LOCAL uint64_t signal_buf_size = 0u;
static CMB_THREAD_LOCAL const struct cmi_hashheap *signal_buf_hp = NULL;


struct cmb_condition *cmb_condition_create(void)
{
    struct cmb_condition *cp = cmi_malloc(sizeof *cp);
    cmi_memset(cp, 0, sizeof *cp);
    struct cmi_resourcebase *rbp = &(cp->base);
    rbp->cookie = CMI_UNINITIALIZED;

    /* Add teardown function to the memregistry in case we need to bail out */
    cmi_dlist_initialize(&(cp->destroy.node));
    cp->destroy.teardown = (cmi_teardown_func *)cmb_condition_destroy;
    cp->destroy.object = cp;
    cmi_memregistry_add(&(cp->destroy));

    cmb_assert_debug(rbp->cookie == CMI_UNINITIALIZED);
    return cp;
}

void cmb_condition_initialize(struct cmb_condition *cp,
                              const char *name)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(name != NULL);
    /* Might get raw memory with random content, cannot assert _UNINITIALIZED */

    /* Initializing base class creates a memregistry item. */
    struct cmi_resourcebase *rbp = &(cp->base);
    cmi_resourcebase_initialize(rbp, name);

    /* The memregistry item is now pointing to cmi_resourcebase_terminate.
     * Redirect it to ours (do not call cmb_logger_error in the meantime!) */
    cmb_assert_debug(rbp->terminate.object == cp);
    rbp->terminate.teardown = (cmi_teardown_func *)cmb_condition_terminate;
    /* OK, consistent now, continue own initialization */

    /* Connect the resource guard to the actual resource */
    cmb_resourceguard_initialize(&(cp->guard), rbp);

    cmb_assert_debug(rbp->cookie == CMI_INITIALIZED);
}

void cmb_condition_terminate(struct cmb_condition *cp)
{
    cmb_assert_release(cp != NULL);
    struct cmi_resourcebase *rbp = &(cp->base);
    cmb_assert_release((rbp->cookie == CMI_INITIALIZED)
                    || cmi_memregistry_is_abandoning);

    if (rbp->cookie == CMI_INITIALIZED) {
        cmb_resourceguard_terminate(&(cp->guard));

        /* Terminating the parent class will also clear the memregistry item */
        cmi_resourcebase_terminate(rbp);
    }

    cmb_assert_debug(rbp->cookie == CMI_UNINITIALIZED);
}

void cmb_condition_destroy(struct cmb_condition *cp)
{
    cmb_assert_release(cp != NULL);
    const struct cmi_resourcebase *rbp = &(cp->base);
    /* Call cmb_condition_terminate first, please */
    cmb_assert_release(rbp->cookie == CMI_UNINITIALIZED);

    if (!cmi_memregistry_is_abandoning) {
        /* Destroying normally, remove from register */
        cmi_memregistry_remove(&(cp->destroy));
    }

    cmi_free(cp);
}

int64_t cmb_condition_wait(struct cmb_condition *cp,
                           cmb_condition_demand_func *dmnd,
                           const void *ctx)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(dmnd != NULL);

    struct cmb_process *caller = cmb_process_current();
    /* Should not be called from the dispatcher */
    cmb_assert_release(caller != NULL);
    cmb_assert_debug(caller->tiebreaker == CMI_PROCESS_TB_CLEARED);
    caller->tiebreaker = CMI_PROCESS_TB_ARMED;

    cmb_logger_info(stdout, "Waiting for condition %s",
                    ((struct cmi_resourcebase *)cp)->name);
    const int64_t sig =  cmb_resourceguard_wait(&(cp->guard),
                                          (cmb_resourceguard_demand_func *)dmnd,
                                          ctx);
    cmb_logger_info(stdout, "Condition %s returning signal %" PRIi64,
                    ((struct cmi_resourcebase *)cp)->name, sig);
    caller->tiebreaker = CMI_PROCESS_TB_CLEARED;

    return sig;
}

/*
 * wakeup_event_condition - The event that actually resumes the process coroutine
 */
static void wakeup_event_condition(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmb_process *pp = (struct cmb_process *)vp;
    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64, pp->name, (int64_t)arg);

    /* Check if the process still is waiting for this event */
    const uint64_t key = (uint64_t)arg;
    if (!cmi_process_awaiting_key(pp, key)) {
        /* Stale, just discard */
        return;
    }

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, (void *)CMB_PROCESS_SUCCESS);
    }
}

/*
 * Comparison function to sort array of heap indices in FIFO order
 * Used with standard library qsort(), so the args are pointers into the
 * signal_buf array of heap indices.
 */
static int sigcmp(const void *a, const void *b) {
    const uint64_t ia = *(const uint64_t *)a;
    const uint64_t ib = *(const uint64_t *)b;
    if (ia == ib) {
        return 0;
    }

    const struct cmi_heap_tag *htpa = &(signal_buf_hp->heap[ia]);
    const struct cmi_heap_tag *htpb = &(signal_buf_hp->heap[ib]);

    return (*(signal_buf_hp->heap_compare))(htpa, htpb) ? -1 : 1;
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
uint64_t cmb_condition_signal(struct cmb_condition *cp)
{
    cmb_assert_release(cp != NULL);

    cmb_logger_info(stdout, "Signalling condition %s",
                    ((struct cmi_resourcebase *)cp)->name);

    struct cmi_hashheap *hp = (struct cmi_hashheap *)&(cp->guard);
    if ((hp->heap == NULL) || (hp->heap_count == 0u)) {
        cmb_logger_info(stdout, "None waiting for %s",
            ((struct cmi_resourcebase *)cp)->name);
        return 0u;
    }

    /* Make sure the buffer is large enough to reactivate everything in the heap */
    if (hp->heap_count > signal_buf_size) {
        signal_buf = cmi_realloc(signal_buf, hp->heap_count * sizeof(uint64_t));
        signal_buf_size = hp->heap_count;
    }

    /* Record the satisfied demand predicates */
    uint64_t cnt = 0u;
    for (uint64_t ui = 1u; ui <= hp->heap_count; ui++) {
        struct cmi_heap_tag *htp = &(hp->heap[ui]);
        void **item = htp->item;
        struct cmb_process *pp = item[0];
        cmb_condition_demand_func *demand = item[1];
        const void *ctx = item[2];

        if ((*demand)(cp, pp, ctx)) {
            /* Satisfied, note it on the list */
            cmb_logger_info(stdout, "Condition %s satisfied for process %s",
                            ((struct cmi_resourcebase *)cp)->name, pp->name);
            signal_buf[cnt++] = ui;
        }
    }

    /* Sort the satisfied customers in correct queue order */
    signal_buf_hp = hp;
    qsort(signal_buf, cnt, sizeof(uint64_t), sigcmp);
    signal_buf_hp = NULL;

    /* Schedule wakeup events in queue order */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        const uint64_t inx = signal_buf[ui];
        struct cmi_heap_tag *htp = &(hp->heap[inx]);
        void **item = htp->item;
        struct cmb_process *pp = item[0];

        const uint64_t key = htp->hash_key;
        const double time = cmb_time();
        const int64_t priority = cmb_process_priority(pp);
        (void)cmb_event_schedule(wakeup_event_condition, pp,
                                 (void *)key, time, priority);
        /* Store the hash key for our next pass */
        signal_buf[ui] = key;
    }

    /* Remove the satisfied waiters from the hashheap */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        cmi_hashheap_remove(hp, signal_buf[ui]);
    }

    return cnt;
}

bool cmb_condition_cancel(struct cmb_condition *cp,
                          struct cmb_process *pp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(pp != NULL);

    cmb_logger_info(stdout, "Cancelling condition %s for process %s",
                    cp->base.name, pp->name);

    return cmb_resourceguard_cancel(&(cp->guard), pp);
}

bool cmb_condition_remove(struct cmb_condition *cp,
                          const struct cmb_process *pp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(pp != NULL);

    cmb_logger_info(stdout, "Removing process %s from condition %s",
                    pp->name, cp->base.name);

    return cmb_resourceguard_remove(&(cp->guard), pp);
}

/*
 * cmi_condition_thread_cleanup - Function to deallocate any allocated memory
 * in the thread local signal buffer. Call when exiting a pthread.
 */
void cmi_condition_thread_cleanup(void)
{
    if (signal_buf != NULL) {
        cmi_free(signal_buf);
        signal_buf = NULL;
        signal_buf_size = 0u;
    }
}
