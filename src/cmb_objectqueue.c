/*
 * cmb_objectqueue.c - a two-headed fixed-capacity resource where one or more
 * producer processes can put objects into the one end, and one or more
 * consumer processes can get objects out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
 *
 * The difference from cmb_buffer is that it only represents amounts, while
 * qmb_objectqueue tracks the individual objects passing throug the queue. An
 * object can be anything, represented by void* here.
 *
 * The queue_tags and their memory pool is defined here, since they are only
 * used internally by the cmb_objectqueue class.
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


#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_objectqueue.h"
#include "cmb_process.h"

#include "cmi_mempool.h"
#include "cmi_memutils.h"

/*
 * struct queue_tag : A tag for the singly linked list that is a queue.
 * Padded by 8 bytes to add up to a 32-byte object.
 */
struct queue_tag {
    struct queue_tag *next;
    double timestamp;
    uint64_t padding;
    void *object;
};

struct cmb_objectqueue *cmb_objectqueue_create(void)
{
    struct cmb_objectqueue *oqp = cmi_malloc(sizeof *oqp);
    cmi_memset(oqp, 0, sizeof *oqp);
    ((struct cmi_resourcebase *)oqp)->cookie = CMI_UNINITIALIZED;

    return oqp;
}

void cmb_objectqueue_initialize(struct cmb_objectqueue *oqp,
                                const char *name,
                                const uint64_t capacity)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_resourcebase_initialize(&(oqp->core), name);

    cmi_resourceguard_initialize(&(oqp->front_guard), &(oqp->core));
    cmi_resourceguard_initialize(&(oqp->rear_guard), &(oqp->core));

    oqp->capacity = capacity;
    oqp->length = 0u;

    oqp->queue_head = NULL;
    oqp->queue_end = NULL;

    oqp->is_recording = false;
    cmb_timeseries_initialize(&(oqp->history));
    cmb_dataset_initialize(&(oqp->wait_times));
}

void cmb_objectqueue_terminate(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);

    while (oqp->queue_head != NULL) {
        struct queue_tag *tag = oqp->queue_head;
        oqp->queue_head = tag->next;
        cmi_mempool_put(&cmi_mempool_32b, tag);
    }

    oqp->length = 0u;
    oqp->queue_head = NULL;
    oqp->queue_end = NULL;

    cmb_dataset_terminate(&(oqp->wait_times));
    cmb_timeseries_terminate(&(oqp->history));

    cmi_resourceguard_terminate(&(oqp->rear_guard));
    cmi_resourceguard_terminate(&(oqp->front_guard));
    cmi_resourcebase_terminate(&(oqp->core));
}

void cmb_objectqueue_destroy(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);

    cmb_objectqueue_terminate(oqp);
    cmi_free(oqp);
}

/*
 * has_content : pre-packaged demand function for a cmb_objectqueue, allowing
 * the getting process to grab some whenever there is something to grab.
 */
static bool has_content(const struct cmi_resourcebase *rbp,
                        const struct cmb_process *pp,
                        const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_objectqueue *oqp = (struct cmb_objectqueue *)rbp;

    return (oqp->queue_head != NULL);
}

/*
 * has_space : pre-packaged demand function for a cmb_objectqueue, allowing
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

    const struct cmb_objectqueue *oqp = (struct cmb_objectqueue *)rbp;

    return (oqp->length < oqp->capacity);
}

/*
 * Record queue length, the waiting times will be updated when objects
 * leave the cmb_objectqueue
 */
static void record_sample(struct cmb_objectqueue *oqp) {
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    if (oqp->is_recording) {
        struct cmb_timeseries *ts = &(oqp->history);
        cmb_timeseries_add(ts, (double)(oqp->length), cmb_time());
    }
}

void cmb_objectqueue_start_recording(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    oqp->is_recording = true;
    record_sample(oqp);
}

void cmb_objectqueue_stop_recording(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    record_sample(oqp);
    oqp->is_recording = false;
}

struct cmb_timeseries *cmb_objectqueue_get_history(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    return &(oqp->history);
}

struct cmb_dataset *cmb_queue_get_wait_times(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    return &(oqp->wait_times);
}

void cmb_objectqueue_print_report(struct cmb_objectqueue *oqp, FILE *fp) {
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    fprintf(fp, "Queue lengths for %s:\n", oqp->core.name);
    const struct cmb_timeseries *ts = &(oqp->history);

    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);

    const unsigned nbin = (oqp->capacity > 20) ? 20 : oqp->capacity + 1;
    cmb_timeseries_print_histogram(ts, fp, nbin, 0.0, (double)(oqp->capacity + 1u));

    fprintf(fp, "Waiting times for %s:\n", oqp->core.name);
    struct cmb_datasummary *ds = cmb_datasummary_create();
    (void)cmb_dataset_summarize(&(oqp->wait_times), ds);
    cmb_datasummary_print(ds, fp, true);
    cmb_wtdsummary_destroy(ws);

    cmb_dataset_print_histogram(&(oqp->wait_times), fp, nbin, 0.0, (double)(oqp->capacity + 1u));
}

int64_t cmb_objectqueue_get(struct cmb_objectqueue *oqp, void **objectloc)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(objectloc != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    while (true) {
        cmb_assert_debug(oqp->length <= oqp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu length now %llu",
                       rbp->name, oqp->capacity, oqp->length);
        cmb_logger_info(stdout, "Gets an object from %s", rbp->name);

        if (oqp->queue_head != NULL) {
            /* There is one ready */
            struct queue_tag *tag = oqp->queue_head;
            cmb_assert_debug(sizeof(*tag) == 32u);
            oqp->queue_head = tag->next;
            oqp->length--;
            if (oqp->queue_head == NULL) {
                oqp->queue_end = NULL;
            }

            *objectloc = tag->object;
            if (oqp->is_recording) {
                record_sample(oqp);
                cmb_dataset_add(&(oqp->wait_times), cmb_time() - tag->timestamp);
            }

            cmb_logger_info(stdout, "Success, got %p", *objectloc);
            tag->next = NULL;
            tag->object = NULL;
            cmi_mempool_put(&cmi_mempool_32b, tag);

            cmi_resourceguard_signal(&(oqp->rear_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(oqp->length == 0u);
        cmb_logger_info(stdout, "Waiting for an object");
        const int64_t sig = cmi_resourceguard_wait(&(oqp->front_guard),
                                                   has_content,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %lld, returns without object",
                            sig);
            *objectloc = NULL;
            cmb_assert_debug(oqp->length <= oqp->capacity);

            return sig;
        }
    }
}

int64_t cmb_objectqueue_put(struct cmb_objectqueue *oqp, void **objectloc)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(objectloc != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    while (true) {
        cmb_assert_debug(oqp->length <= oqp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu length now %llu",
                        rbp->name, oqp->capacity, oqp->length);
        cmb_logger_info(stdout, "Puts object %p into %s", *objectloc, rbp->name);
        if (oqp->length < oqp->capacity) {
            /* There is space */
            struct queue_tag *tag = cmi_mempool_get(&cmi_mempool_32b);
            tag->object = *objectloc;
            tag->timestamp = cmb_time();
            tag->next = NULL;

            if (oqp->queue_head == NULL) {
                oqp->queue_head = tag;
            }
            else {
                oqp->queue_end->next = tag;
            }

            oqp->queue_end = tag;
            oqp->length++;
            cmb_assert_debug(oqp->length <= oqp->capacity);

            record_sample(oqp);
            cmb_logger_info(stdout, "Success, put %p", *objectloc);
            cmi_resourceguard_signal(&(oqp->front_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(oqp->length == oqp->capacity);
        cmb_logger_info(stdout, "Waiting for space");
        const int64_t sig = cmi_resourceguard_wait(&(oqp->rear_guard),
                                                   has_space,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %lld, returns without putting object %p into %s",
                            sig,
                            *objectloc,
                            rbp->name);
            cmb_assert_debug(oqp->length <= oqp->capacity);

            return sig;
        }
    }
}

