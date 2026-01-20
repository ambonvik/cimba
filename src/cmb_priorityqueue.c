/*
 * cmb_priorityqueue.c - a two-headed fixed-capacity resource where one or more
 * producer processes can put objects into the one end, and one or more
 * consumer processes can get objects out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait. Objects are retrieved from the queue in priority order.
 *
 * We use a cmi_hashheap for this, but have not yet exposed any methods to access or
 * cancel objects already scheduled. May be added later.
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

#include <inttypes.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_priorityqueue.h"
#include "cmb_process.h"

#include "cmi_memutils.h"

/* The initial heap size will be 2^INITIAL_QUEUE_SIZE */
#define INITIAL_QUEUE_SIZE 3u

/*
 * compare_func - Test if heap_tag *a should go before *b. If so, return true.
 * Order by isortkey (priority) only.
 */
static bool compare_func(const struct cmi_heap_tag *a, const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    return (a->isortkey > b->isortkey);
}

struct cmb_priorityqueue *cmb_priorityqueue_create(void)
{
    struct cmb_priorityqueue *pqp = cmi_malloc(sizeof *pqp);
    cmi_memset(pqp, 0, sizeof *pqp);
    ((struct cmi_resourcebase *)pqp)->cookie = CMI_UNINITIALIZED;

    return pqp;
}

void cmb_priorityqueue_initialize(struct cmb_priorityqueue *pqp,
                                  const char *name,
                                  const uint64_t capacity)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_resourcebase_initialize(&(pqp->core), name);
    cmb_resourceguard_initialize(&(pqp->front_guard), &(pqp->core));
    cmb_resourceguard_initialize(&(pqp->rear_guard), &(pqp->core));
    cmi_hashheap_initialize(&(pqp->queue), INITIAL_QUEUE_SIZE, compare_func);
    pqp->capacity = capacity;
    pqp->is_recording = false;
    cmb_timeseries_initialize(&(pqp->history));
}

void cmb_priorityqueue_terminate(struct cmb_priorityqueue *pqp)
{
    cmb_assert_release(pqp != NULL);

    cmb_timeseries_terminate(&(pqp->history));
    cmi_hashheap_terminate(&(pqp->queue));
    cmb_resourceguard_terminate(&(pqp->rear_guard));
    cmb_resourceguard_terminate(&(pqp->front_guard));
    cmi_resourcebase_terminate(&(pqp->core));
}

void cmb_priorityqueue_destroy(struct cmb_priorityqueue *pqp)
{
    cmb_assert_release(pqp != NULL);

    cmb_priorityqueue_terminate(pqp);
    cmi_free(pqp);
}

/*
 * has_content - pre-packaged demand function for a cmb_priorityqueue, allowing
 * the getting process to grab some whenever there is something to grab.
 */
static bool has_content(const struct cmi_resourcebase *rbp,
                        const struct cmb_process *pp,
                        const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_priorityqueue *pqp = (struct cmb_priorityqueue *)rbp;

    return (pqp->queue.heap_count > 0u);
}

/*
 * has_space - pre-packaged demand function for a cmb_priorityqueue, allowing
 * the putting process to stuff in some whenever there is space.
 */
static bool has_space(const struct cmi_resourcebase *rbp,
                      const struct cmb_process *pp,
                      const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_priorityqueue *pqp = (struct cmb_priorityqueue *)rbp;

    return (pqp->queue.heap_count < pqp->capacity);
}

static void record_sample(struct cmb_priorityqueue *pqp) {
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);

    if (pqp->is_recording) {
        struct cmb_timeseries *ts = &(pqp->history);
        cmb_timeseries_add(ts, (double)(pqp->queue.heap_count), cmb_time());
    }
}

void cmb_priorityqueue_start_recording(struct cmb_priorityqueue *pqp)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);

    pqp->is_recording = true;
    record_sample(pqp);
}

void cmb_priorityqueue_stop_recording(struct cmb_priorityqueue *pqp)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);

    record_sample(pqp);
    pqp->is_recording = false;
}

struct cmb_timeseries *cmb_priorityqueue_history(struct cmb_priorityqueue *pqp)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);

    return &(pqp->history);
}

void cmb_priorityqueue_print_report(struct cmb_priorityqueue *pqp, FILE *fp) {
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);

    fprintf(fp, "Queue lengths for %s:\n", pqp->core.name);
    const struct cmb_timeseries *ts = &(pqp->history);

    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);

    const unsigned nbin = (pqp->capacity > 20) ? 20 : pqp->capacity + 1;
    cmb_timeseries_print_histogram(ts, fp, nbin, 0.0, (double)(pqp->capacity + 1u));
}

int64_t cmb_priorityqueue_get(struct cmb_priorityqueue *pqp, void **objectloc)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(objectloc != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)pqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    cmb_logger_info(stdout, "Gets an object from %s, length now %" PRIu64,
                    rbp->name, pqp->queue.heap_count);
    while (true) {
        cmb_assert_debug(pqp->queue.heap_count <= pqp->capacity);
        if (pqp->queue.heap_count > 0u) {
            /* There is one ready */
            void **item = cmi_hashheap_dequeue(&(pqp->queue));
            cmb_assert_debug(item != NULL);
            *objectloc = *item;

            if (pqp->is_recording) {
                record_sample(pqp);
            }

            cmb_logger_info(stdout, "Success, got %p", *objectloc);
            cmb_resourceguard_signal(&(pqp->rear_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(pqp->queue.heap_count == 0u);
        cmb_logger_info(stdout, "Waiting for an object");
        const int64_t sig = cmb_resourceguard_wait(&(pqp->front_guard),
                                                   has_content,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %" PRIi64 " returns without object",
                            sig);
            *objectloc = NULL;
            cmb_assert_debug(pqp->queue.heap_count <= pqp->capacity);

            return sig;
        }
    }
}

int64_t cmb_priorityqueue_put(struct cmb_priorityqueue *pqp,
                              void **objectloc,
                              const int64_t priority,
                              uint64_t *handleloc)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(objectloc != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)pqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_logger_info(stdout, "Puts object %p priority %" PRIi64 " into %s, length %" PRIu64,
                    *objectloc, priority, rbp->name, pqp->queue.heap_count);
    while (true) {
        cmb_assert_debug(pqp->queue.heap_count <= pqp->capacity);
        if (pqp->queue.heap_count < pqp->capacity) {
            /* There is space */
            const uint64_t handle = cmi_hashheap_enqueue(&(pqp->queue),
                                                         *objectloc,
                                                         NULL, NULL, NULL,
                                                         0u, 0.0, priority);
            if (handleloc != NULL) {
                *handleloc = handle;
            }

            record_sample(pqp);
            cmb_logger_info(stdout, "Success, put %p", *objectloc);
            cmb_resourceguard_signal(&(pqp->front_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(pqp->queue.heap_count == pqp->capacity);
        cmb_logger_info(stdout, "Waiting for space");
        const int64_t sig = cmb_resourceguard_wait(&(pqp->rear_guard),
                                                   has_space,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %" PRIi64 ", could not put object %p into %s",
                            sig, *objectloc, rbp->name);
            cmb_assert_debug(pqp->queue.heap_count <= pqp->capacity);

            return sig;
        }
    }
}
