/*
 * cmb_event.c - event view of discrete event simulation.
 * Provides routines to handle clock sequencing and event scheduling.
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025-26.
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

#include <assert.h>
#include <stdbool.h>

#include "cmb_assert.h"
#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "cmi_config.h"
#include "cmi_hashheap.h"
#include "cmi_memutils.h"
#include "cmi_process.h"
#include "cmi_slist.h"


/*
 * sim_time - The simulation clock. It can be initiated to start from a
 * negative value, but it can only increase once initiated, never go back.
 * Read-only for the user application, only accessible through cmb_time()
 */
static CMB_THREAD_LOCAL double sim_time = 0.0;

/*
 * event_queue - The main event queue, implemented as a hash/heap.
 */
static CMB_THREAD_LOCAL struct cmi_hashheap *event_queue = NULL;

/* The initial capacity of the heap is 2^QUEUE_INIT_EXP items, resizing as needed */
#define QUEUE_INIT_EXP 3


/* The memory layout of an event */
struct event_peek {
    cmb_event_func *action;
    void *subject;
    void *object;
    struct cmi_slist_head waiters;
};

static_assert(sizeof(struct event_peek) == 4 * sizeof(void *), "Unexpected size");

/*
 * cmb_time - Return current simulation time.
 */
double cmb_time(void)
{
    return sim_time;
}

/*
 * heap_order_check - Test if heap_tag *a should go before *b. If so, return true.
 * Prioritization corresponds to the event queue order, where lower reactivation
 * times (dsortkey) go before higher, if equal, then higher priority (isortkey) before
 * lower, and if that also equal, FIFO order based on handle value.
 */
static bool heap_order_check(const struct cmi_heap_tag *a,
                             const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->dsortkey < b->dsortkey) {
        ret = true;
    }
    else if (a->dsortkey == b->dsortkey) {
        if (a->isortkey > b->isortkey) {
            ret = true;
        }
        else if (a->isortkey == b->isortkey) {
            if (a->key < b->key) {
                ret = true;
            }
        }
    }

    return ret;
}

/*
 * cmb_event_queue_initialize - Set starting simulation time, allocate and initialize
 * hashheap for use. Allocates contiguous memory aligned to an integer number
 * of memory pages for efficiency.
 */
void cmb_event_queue_initialize(const double start_time)
{
    sim_time = start_time;

    event_queue = cmi_hashheap_create();
    cmi_hashheap_initialize(event_queue, QUEUE_INIT_EXP, heap_order_check);
}

/*
 * cmb_event_queue_terminate - Clean up, deallocating space.
 */
void cmb_event_queue_terminate(void)
{
    cmi_hashheap_terminate(event_queue);
    cmi_hashheap_destroy(event_queue);
    event_queue = NULL;
    sim_time = 0.0;
}

/*
 * cmb_event_queue_clear - Clean up, deallocating space.
 */
void cmb_event_queue_clear(void)
{
    cmi_hashheap_clear(event_queue);
}

/*
 * cmb_event_queue_is_empty - Is the event queue empty?
 */
bool cmb_event_queue_is_empty(void)
{
    return cmi_hashheap_is_empty(event_queue);
}

/*
 * cmb_event_queue_count - Returns current number of events in the queue.
 */
extern uint64_t cmb_event_queue_count(void)
{
    return cmi_hashheap_count(event_queue);
}

/*
 * cmb_event_schedule - Insert the event in the event queue as indicated by
 * activation time t and priority p, return a unique event handle.
 * Resizes hashheap if necessary.
 */
uint64_t cmb_event_schedule(cmb_event_func *action,
                            void *subject,
                            void *object,
                            const double time,
                            const int64_t priority)
{
    cmb_assert_release(time >= sim_time);
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_enqueue(event_queue,
                                (void *)action,
                                subject,
                                object,
                                NULL,
                                0u,
                                time,
                                priority);
}

/*
 * cmb_event_is_scheduled - Is the given event scheduled?
 */
bool cmb_event_is_scheduled(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_is_enqueued(event_queue, handle);
}

/*
 * cmb_event_time - The currently scheduled time for the given event
 */
double cmb_event_time(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_dkey(event_queue, handle);
}

/*
 * cmb_event_priority - The current priority for the given event
 */
int64_t cmb_event_priority(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_ikey(event_queue, handle);
}

/*
 * wakeup_event_event - The event that resumes the process after being scheduled by
 *           cmb_process_wait_event
 */
static void wakeup_event_event(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);
    struct cmb_process *pp = (struct cmb_process *)vp;

    cmb_logger_info(stdout, "Wakes %s signal %" PRIi64, pp->name, (int64_t)arg);
    cmb_assert_debug(!cmi_slist_is_empty(&(pp->awaits)));

    const bool found = cmi_process_remove_awaitable(pp,
                                                    CMI_PROCESS_AWAITABLE_EVENT,
                                                    NULL);
    cmb_assert_debug(found == true);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
    else {
        cmb_logger_warning(stdout,
                          "Event wait wakeup call found process %s dead",
                          cmb_process_name(pp));
    }
}

void wake_event_waiters(struct cmi_slist_head *waiters,
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

        (void)cmb_event_schedule(wakeup_event_event, pp, (void *)signal,
                                 time, priority);
        cmi_mempool_free(&cmi_process_waitertags, pw);
    }

    cmb_assert_debug(cmi_slist_is_empty(waiters));
}

/*
 * cmb_event_execute_next - Remove and execute the next event, update the clock.
 * cmi_hashheap_dequeue returns a pointer to the current location of the event.
 * This location is at the end of the heap and will be overwritten by the first
 * addition to the heap, such as scheduling a wakeup event. Copy the values to
 * local variables before executing the event.
 */
bool cmb_event_execute_next(void)
{
    if (cmb_event_queue_is_empty()) {
        return false;
    }

    /* Advance clock to time of the next event */
    sim_time = cmi_hashheap_peek_dkey(event_queue);

    /* Pull off the next event and decode it */
    struct event_peek tmp = *(struct event_peek *)cmi_hashheap_dequeue(event_queue);

    /* Schedule wakeup events for any processes waiting for this to happen */
    if (!cmi_slist_is_empty(&(tmp.waiters))) {
        wake_event_waiters(&(tmp.waiters), CMB_PROCESS_SUCCESS);
    }

    /* Execute the event */
    (*tmp.action)(tmp.subject, tmp.object);

    return true;
}

/*
 * cmb_event_queue_execute - Executes event queue until empty.
 * Schedule an event containing cmb_event_queue_clear to terminate the
 * simulation at the correct time or other conditions.
 */
void cmb_event_queue_execute(void)
{
    cmb_assert_release(event_queue != NULL);

    cmb_logger_info(stdout, "Starting simulation run");
    while (cmb_event_execute_next()) { }

    cmb_logger_info(stdout, "No more events in queue");
}

/*
 * cmb_event_cancel - Cancel the given event and reshuffle the heap.
 * Notifies any processes waiting for the event that it is canceled.
 */
bool cmb_event_cancel(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    if (!cmi_hashheap_is_enqueued(event_queue, handle)) {
        return false;
    }

    struct event_peek tmp = *(struct event_peek *)cmi_hashheap_item(event_queue, handle);

    (void)cmi_hashheap_cancel(event_queue, handle);

    if (!cmi_slist_is_empty(&(tmp.waiters))) {
        wake_event_waiters(&(tmp.waiters), CMB_PROCESS_CANCELLED);
    }

    return true;
}

/*
 * cmb_event_reschedule - Reschedule the given event to the given absolute time.
 * Precondition: The event must be in heap.
 */
void cmb_event_reschedule(const uint64_t handle, const double time)
{
    cmb_assert_release(time >= sim_time);
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    cmb_assert_release(cmi_hashheap_is_enqueued(event_queue, handle));

    /* Do not change the priority isortkey */
    const int64_t pri = cmi_hashheap_ikey(event_queue, handle);

    cmi_hashheap_reprioritize(event_queue, handle, time, pri);
}

/*
 * Reprioritize the given event and reshuffle heap
 * Precondition: The event must be in heap.
 */
void cmb_event_reprioritize(const uint64_t handle,
                            const int64_t priority)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    cmb_assert_release(cmi_hashheap_is_enqueued(event_queue, handle));

    const double time = cmi_hashheap_dkey(event_queue, handle);
    cmb_assert_debug(time >= sim_time);

    cmi_hashheap_reprioritize(event_queue, handle, time, priority);
}

/*
 * cmb_event_pattern_find - Locate a specific event, using the CMB_ANY_*
 * constants as wildcards in the respective positions. Returns the handle of
 * the event, or zero if none is found.
 */
uint64_t cmb_event_pattern_find(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    cmb_assert_release(event_queue != NULL);

    const void *vaction = *(void**)&action;
    return cmi_hashheap_pattern_find(event_queue,
                                     vaction,
                                     subject,
                                     object,
                                     CMI_ANY_ITEM);
}

/*
 * cmb_event_pattern_count - Count matching events using CMB_ANY_* as wildcards.
 * Returns the number of matching events, possibly zero.
 */
uint64_t cmb_event_pattern_count(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    cmb_assert_release(event_queue != NULL);

    const void *vaction = *(void**)&action;
    return cmi_hashheap_pattern_count(event_queue,
                                      vaction,
                                      subject,
                                      object,
                                      CMI_ANY_ITEM);
}

/*
 * cmb_event_cancel_all - Cancel all matching events.
 * Two-pass approach: Allocate temporary storage for the list of matching
 * handles in the first pass, then cancel these in the second pass.
 * Returns the number of events canceled, possibly zero.
 * Duplicates code from cmi_hashheap to also cancel any processes
 * waiting for canceled events.
 */
uint64_t cmb_event_pattern_cancel(cmb_event_func *action,
                                  const void *subject,
                                  const void *object)
{
    cmb_assert_release(event_queue != NULL);

    uint64_t cnt = 0u;
    if ((event_queue->heap == NULL) || (event_queue->heap_count == 0u)) {
        return cnt;
    }

    /* Allocate space enough to match everything in the heap */
    const uint64_t hcnt = event_queue->heap_count;
    uint64_t *tmp = cmi_malloc(hcnt * sizeof(*tmp));

    /* First pass, recording the matches */
    const void *vaction = *(void**)&action;
    for (uint64_t ui = 1; ui <= hcnt; ui++) {
        const struct cmi_heap_tag *htp = &(event_queue->heap[ui]);
        if (((vaction == htp->item[0]) || (action == CMB_ANY_ACTION))
               && ((subject == htp->item[1]) || (subject == CMB_ANY_SUBJECT))
               && ((object == htp->item[2]) || (object == CMB_ANY_OBJECT))) {
             /* Matched, note it on the list */
            tmp[cnt++] = htp->key;
        }
    }

    /* Second pass, cancel the matching events */
    for (uint64_t ui = 0u; ui < cnt; ui++) {
        cmb_event_cancel(tmp[ui]);
    }

    cmi_free(tmp);
    return cnt;
}

/*
 * cmb_event_queue_print - Print content of event heap, useful for debugging
 */
void cmb_event_queue_print(FILE *fp)
{
    cmb_assert_release(event_queue != NULL);
    const uint64_t hcnt = event_queue->heap_count;

    fprintf(fp, "---------------- Event queue ----------------\n");
    for (uint64_t ui = 1u; ui <= hcnt; ui++) {
        const struct cmi_heap_tag *htp = &(event_queue->heap[ui]);
        fprintf(fp,
                "time %#8.4g prio %" PRIi64 ": key %" PRIu64 " %p %p %p %p\n",
                htp->dsortkey,
                htp->isortkey,
                htp->key,
                htp->item[0],
                htp->item[1],
                htp->item[2],
                htp->item[3]);
    }
    fprintf(fp, "---------------------------------------------\n");
    fflush(fp);
}

/*
 * Register a waiting process at the event in its current location
 */
void cmi_event_add_waiter(const uint64_t key, struct cmb_process *pp)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    cmb_assert_release(cmi_hashheap_is_enqueued(event_queue, key));

    struct cmi_process_waiter *tag = cmi_mempool_alloc(&cmi_process_waitertags);
    tag->proc = pp;

    struct event_peek *tmp = (struct event_peek *)cmi_hashheap_item(event_queue, key);
    cmi_slist_push(&(tmp->waiters), &(tag->listhead));
}

/*
 * Remove a waiting process from the event in its current location
 */
bool cmi_event_remove_waiter(const uint64_t key, const struct cmb_process *pp)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    cmb_assert_release(cmi_hashheap_is_enqueued(event_queue, key));

    struct event_peek *tmp = (struct event_peek *)cmi_hashheap_item(event_queue, key);
    struct cmi_slist_head *whead = &(tmp->waiters);
    while (whead->next != NULL) {
        struct cmi_process_waiter *pw = cmi_container_of(whead->next,
                                                  struct cmi_process_waiter,
                                                  listhead);
        if (pw->proc == pp) {
            cmi_slist_pop(whead);
            cmi_mempool_free(&cmi_process_waitertags, pw);
            return true;
        }
        else {
            whead = whead->next;
        }
    }

    return false;
}
