/*
 * cmb_event.c - event view of discrete event simulation.
 * Provides routines to handle clock sequencing and event scheduling.
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025.
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

#include <stdbool.h>

#include "cmb_assert.h"
#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "cmi_config.h"
#include "cmi_hashheap.h"
#include "cmi_memutils.h"
#include "cmi_processtag.h"

/*
 * sim_time : The simulation clock. It can be initiated to start from a
 * negative value, but it can only increase once initiated, never go back.
 * Read-only for the user application, only accessible through cmb_time()
 */
static CMB_THREAD_LOCAL double sim_time = 0.0;

/*
 * event_queue : The main event queue, implemented as a hash/heap.
 */
static CMB_THREAD_LOCAL struct cmi_hashheap *event_queue = NULL;

/* Initial capacity of heap is 2^QUEUE_INIT_EXP items, resizing as needed */
#define QUEUE_INIT_EXP 3

/*
 * cmb_time : Return current simulation time.
 */
double cmb_time(void)
{
    return sim_time;
}

/*
 * heap_order_check : Test if heap_tag *a should go before *b. If so, return true.
 * Prioritization corresponds to event queue order, where the heap tag fields
 * dkey = reactivation time, ikey = priority, ukey = not used, uses FIFO based
 * on the event handle value instead.
 */
static bool heap_order_check(const struct cmi_heap_tag *a,
                             const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->dkey < b->dkey) {
        ret = true;
    }
    else if (a->dkey == b->dkey) {
        if (a->ikey > b->ikey) {
            ret = true;
        }
        else if (a->ikey == b->ikey) {
            if (a->handle < b->handle) {
                ret = true;
            }
        }
    }

    return ret;
}

/*
 * cmb_event_queue_init : Set starting simulation time, allocate and initialize
 * hashheap for use. Allocates contiguous memory aligned to an integer number
 * of memory pages for efficiency.
 */
void cmb_event_queue_init(const double start_time)
{
    cmb_assert_release(event_queue == NULL);

    sim_time = start_time;

    event_queue = cmi_hashheap_create();
    cmi_hashheap_init(event_queue, QUEUE_INIT_EXP, heap_order_check);
}

/*
 * cmb_event_queue_clear : Clean up, deallocating space.
 */
void cmb_event_queue_clear(void)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_debug(event_queue != NULL);

    cmi_hashheap_clear(event_queue);
}

/*
 * cmb_event_queue_destroy : Clean up, deallocating space.
 */
void cmb_event_queue_destroy(void)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_debug(event_queue != NULL);

    cmi_hashheap_destroy(event_queue);
}

/*
 * cmb_event_queue_is_empty : Is the event queue empty?
 */
bool cmb_event_queue_is_empty(void)
{
    return cmi_hashheap_is_empty(event_queue);
}

/*
 * cmb_event_queue_count : Returns current number of events in the queue.
 */
extern uint64_t cmb_event_queue_count(void)
{
    return cmi_hashheap_count(event_queue);
}

/*
 * cmb_event_schedule : Insert event in event queue as indicated by activation
 * time t and priority p, return unique event handle.
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

    /* Use contrived cast to suppress warning about converting function ptr. */
    void *vaction = *(void**)(&(action));
    return  cmi_hashheap_enqueue(event_queue,
                                 vaction,
                                 subject,
                                 object,
                                 NULL,
                                 time,
                                 priority);
}

/*
 * cmb_event_is_scheduled : Is the given event scheduled?
 */
bool cmb_event_is_scheduled(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_is_enqueued(event_queue, handle);
}

/*
 * cmb_event_time : The currently scheduled time for the given event
 */
double cmb_event_time(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_get_dkey(event_queue, handle);
}

/*
 * cmb_event_priority : The current priority for the given event
 */
int64_t cmb_event_priority(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    return cmi_hashheap_get_ikey(event_queue, handle);
}

/*
 * cmi_event_tag_loc : Get a pointer to the location of the fourth payload
 * item, to be used for a list of processes waiting for this event.
 * Not part of the public API for the cmb_event module, used by cmb_process.c
 */
struct cmi_processtag **cmi_event_tag_loc(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);

    void **tmp = cmi_hashheap_get_item(event_queue, handle);
    cmb_assert_debug(tmp != NULL);

    return (struct cmi_processtag **)&(tmp[3]);
}

/*
 * cmb_event_execute_next : Remove and execute the next event, update clock.
 * cmi_hashheap_dequeue returns a pointer to the location of the event.
 * Copy the values to local variables before executing the event.
 */

bool cmb_event_execute_next(void)
{
    if (cmb_event_queue_is_empty()) {
        return false;
    }

    /* Advance clock to time of next event */
    sim_time = cmi_hashheap_peek_dkey(event_queue);

    /* Pull off next event and decode it */
    void **tmp = cmi_hashheap_dequeue(event_queue);
    cmb_event_func *action = *(cmb_event_func **)(&tmp[0]);
    void *subject = tmp[1];
    void *object = tmp[2];
    void **wait_loc = &(tmp[3]);
    if (*wait_loc != NULL) {
        cmi_processtag_list_wake_all((struct cmi_processtag **)wait_loc,
                                     CMB_PROCESS_WAIT_NORMAL);
    }

    /* Execute the event */
    (*action)(subject, object);

    return true;
}

/*
 * cmb_run : Executes event list until empty or otherwise stopped.
 * Schedule an event containing cmb_event_list_destroy to terminate the
 * simulation at a given point, or use cmb_event_cancel_all.
 */
void cmb_run(void)
{
    cmb_assert_release(event_queue != NULL);

    cmb_logger_info(stdout, "Starting simulation run");
    while (cmb_event_execute_next()) { }

    cmb_logger_info(stdout, "No more events in queue");
}


/*
 * cmb_event_cancel : Cancel the given event and reshuffle heap. Notifies any
 * processes waiting for the event that it is cancelled.
 * Precondition: Event must be in heap.
 */
void cmb_event_cancel(const uint64_t handle)
{
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    cmb_assert_release(cmi_hashheap_is_enqueued(event_queue, handle));

    void **tmp = cmi_hashheap_get_item(event_queue, handle);
    cmb_assert_debug(tmp != NULL);
    void **wait_loc = &(tmp[3]);
    if (*wait_loc != NULL) {
        cmi_processtag_list_wake_all((struct cmi_processtag **)wait_loc,
                                     CMB_PROCESS_WAIT_CANCELLED);
    }

    (void)cmi_hashheap_cancel(event_queue, handle);
}

/*
 * cmb_event_reschedule : Reschedule the given event to the given absolute time.
 * Precondition: The event must be in heap.
 */
void cmb_event_reschedule(const uint64_t handle, const double time)
{
    cmb_assert_release(time >= sim_time);
    cmb_assert_release(event_queue != NULL);
    cmb_assert_release(cmi_hashheap_count(event_queue) > 0u);
    cmb_assert_release(cmi_hashheap_is_enqueued(event_queue, handle));

    /* Do not change the priority ikey */
    const int64_t pri = cmi_hashheap_get_ikey(event_queue, handle);

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

    /* Do not change the priority ikey, ukey not used */
    const double time = cmi_hashheap_get_dkey(event_queue, handle);
    cmb_assert_debug(time >= sim_time);

    cmi_hashheap_reprioritize(event_queue, handle, time, priority);
}

/*
 * cmb_event_pattern_find : Locate a specific event, using the CMB_ANY_*
 * constants as wildcards in the respective positions. Returns the handle of
 * the event, or zero if none found.
 */
uint64_t cmb_event_pattern_find(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    cmb_assert_release(event_queue != NULL);

    const void *vaction = *(void**)(&(action));
    return cmi_hashheap_pattern_find(event_queue,
                                 vaction,
                                 subject,
                                 object,
                                 CMI_ANY_ITEM);
}

/*
 * cmb_event_pattern_count : Count matching events using CMB_ANY_* as wildcards.
 * Returns the number of matching events, possibly zero.
 */
uint64_t cmb_event_pattern_count(cmb_event_func *action,
                        const void *subject,
                        const void *object)
{
    cmb_assert_release(event_queue != NULL);

    const void *vaction = *(void**)(&(action));
    return cmi_hashheap_pattern_count(event_queue,
                                  vaction,
                                 subject,
                                 object,
                                 CMI_ANY_ITEM);
}

/*
 * cmb_event_cancel_all : Cancel all matching events.
 * Two-pass approach: Allocate temporary storage for the list of matching
 * handles in the first pass, then cancel these in the second pass.
 * Returns the number of events cancelled, possibly zero.
 * Duplicating code from cmi_hashheap in order to also cancel any processes
 * waiting for cancelled events.
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
    const void *vaction = *(void**)(&(action));
    for (uint64_t ui = 1; ui <= hcnt; ui++) {
        const struct cmi_heap_tag *htp = &(event_queue->heap[ui]);
        if (((vaction == htp->item[0]) || (action == CMB_ANY_ACTION))
               && ((subject == htp->item[1]) || (subject == CMB_ANY_SUBJECT))
               && ((object == htp->item[2]) || (object == CMB_ANY_OBJECT))) {
             /* Matched, note it on the list */
            tmp[cnt++] = htp->handle;
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
 * cmb_event_queue_print : Print content of event heap, useful for debugging
 */
void cmb_event_queue_print(FILE *fp)
{
    cmb_assert_release(event_queue != NULL);
    const uint64_t hcnt = event_queue->heap_count;

    fprintf(fp, "---------------- Event queue ----------------\n");
    for (uint64_t ui = 1u; ui <= hcnt; ui++) {
        struct cmi_heap_tag *htp = &(event_queue->heap[ui]);
        fprintf(fp, "time %#8.4g prio %lld : handle %llu %p %p %p %p\n",
                htp->dkey,
                htp->ikey,
                htp->handle,
                htp->item[0],
                htp->item[1],
                htp->item[2],
                htp->item[3]);
        if (htp->item[3] != NULL) {
            cmi_processtag_list_print((struct cmi_processtag **)&(htp->item[3]), fp);
        }
    }
    fprintf(fp, "---------------------------------------------\n");
    fflush(fp);
}