/*
 * cmb_queue.c - a two-headed fixed-capacity resource where one or more
 * producer processes can put objects into the one end, and one or more
 * consumer processes can get objects out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
 *
 * The difference from cmb_queue is that cmb_queue only represents amounts,
 * while qmb_queue tracks the individual objects passing throug the queue. An
 * object can be anything, represented by void* here.
 *
 * The queue_tags and their memory pool is defined here, since they are used
 * internally by the cmb_queue class.
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


#include "../include/cmb_queue.h"
#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "cmb_mempool.h"
#include "cmi_memutils.h"

/*
 * struct queue_tag : A tag for the singly linked list that is a queue
 */
struct queue_tag {
    struct queue_tag *next;
    void *object;
    double timestamp;
};

/*
 * queue_tag_pool : Memory pool of resource tags
 */
static CMB_THREAD_LOCAL struct cmb_mempool *queue_tag_pool = NULL;

/*
 * cmb_queue_create : Allocate memory for a queue object.
 */
struct cmb_queue *cmb_queue_create(void)
{
    struct cmb_queue *qp = cmi_malloc(sizeof *qp);
    cmi_memset(qp, 0, sizeof *qp);

    return qp;
}

/*
 * cmb_queue_initialize : Make an allocated queue object ready for use.
 */
void cmb_queue_initialize(struct cmb_queue *qp,
                           const char *name,
                           uint64_t capacity)
{
    cmb_assert_release(qp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_resourcebase_initialize(&(qp->core), name);

    cmi_resourceguard_initialize(&(qp->front_guard), &(qp->core));
    cmi_resourceguard_initialize(&(qp->rear_guard), &(qp->core));

    qp->capacity = capacity;
    qp->length_now = 0u;

    qp->queue_head = NULL;
    qp->queue_end = NULL;

    cmb_dataset_initialize(&(qp->wait_times));

    qp->is_recording = false;
}

/*
 * cmb_queue_terminate : Un-initializes a queue object.
 */
void cmb_queue_terminate(struct cmb_queue *qp)
{
    cmb_assert_release(qp != NULL);

    while (qp->queue_head != NULL) {
        struct queue_tag *tag = qp->queue_head;
        qp->queue_head = tag->next;
        cmb_mempool_put(queue_tag_pool, tag);
    }

    qp->length_now = 0u;
    qp->queue_head = NULL;
    qp->queue_end = NULL;
    qp->is_recording = false;

    cmb_dataset_terminate(&(qp->wait_times));
    cmi_resourceguard_terminate(&(qp->rear_guard));
    cmi_resourceguard_terminate(&(qp->front_guard));
    cmi_resourcebase_terminate(&(qp->core));
}

/*
 * cmb_queue_destroy : Deallocates memory for a queue object.
 */
void cmb_queue_destroy(struct cmb_queue *qp)
{
    cmb_assert_release(qp != NULL);

    cmb_queue_terminate(qp);
    cmi_free(qp);
}

/*
 * queue_has_content : pre-packaged demand function for a cmb_queue, allowing
 * the getting process to grab some whenever there is something to grab.
 */
static bool queue_has_content(const struct cmi_resourcebase *rbp,
                               const struct cmb_process *pp,
                               const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_queue *qp = (struct cmb_queue *)rbp;

    return (qp->queue_head != NULL);
}

/*
 * queue_has_space : pre-packaged demand function for a cmb_queue, allowing
 * the putting process to stuff in some whenever there is space.
 */
static bool queue_has_space(const struct cmi_resourcebase *rbp,
                             const struct cmb_process *pp,
                             const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_queue *qp = (struct cmb_queue *)rbp;

    return (qp->length_now < qp->capacity);
}

static void record_sample(struct cmb_queue *qp) {
    cmb_assert_release(qp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;
    if (rbp->is_recording) {
        struct cmb_timeseries *ts = &(rbp->history);
        cmb_timeseries_add(ts, (double)(qp->length_now), cmb_time());
    }
}

void cmb_queue_start_recording(struct cmb_queue *qp)
{
    cmb_assert_release(qp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;
    rbp->is_recording = true;
    record_sample(qp);
}

void cmb_queue_stop_recording(struct cmb_queue *qp)
{
    cmb_assert_release(qp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;
    record_sample(qp);
    rbp->is_recording = false;
}

struct cmb_timeseries *cmb_queue_get_length_history(struct cmb_queue *qp)
{
    cmb_assert_release(qp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;

    return &(rbp->history);
}

struct cmb_dataset *cmb_queue_get_wait_times(struct cmb_queue *qp)
{
    cmb_assert_release(qp != NULL);

    return &(qp->wait_times);
}

void cmb_queue_print_report(struct cmb_queue *qp, FILE *fp) {
    cmb_assert_release(qp != NULL);

    fprintf(fp, "Queue lengths for %s:\n", qp->core.name);
    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;
    const struct cmb_timeseries *ts = &(rbp->history);

    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);

    const unsigned nbin = (qp->capacity > 20) ? 20 : qp->capacity + 1;
    cmb_timeseries_print_histogram(ts, fp, nbin, 0.0, (double)(qp->capacity + 1u));

    fprintf(fp, "Waiting times for %s:\n", qp->core.name);
    struct cmb_datasummary *ds = cmb_datasummary_create();
    (void)cmb_dataset_summarize(&(qp->wait_times), ds);
    cmb_datasummary_print(ds, fp, true);
    cmb_wtdsummary_destroy(ws);

    cmb_dataset_print_histogram(&(qp->wait_times), fp, nbin, 0.0, (double)(qp->capacity + 1u));
}

/*
 * cmb_queue_get : Request and if necessary wait for an amount of the
 * queue resource.
 *
 * Note that the amount argument is a pointer to where the amount is stored.
 * The return value CMB_PROCESS_SUCCESS (0) indicates that all went well and
 * the value *amount equals the requested amount.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
 * and *amount will be the quantity obtained before interrupted. The return
 * value is the interrupt signal received, some value other than
 * CMB_PROCESS_SUCCESS.
 */
int64_t cmb_queue_get(struct cmb_queue *qp, void **objectloc)
{
    cmb_assert_release(qp != NULL);
    cmb_assert_release(objectloc != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;

    while (true) {
        cmb_assert_debug(qp->length_now <= qp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu length now %llu",
                       rbp->name, qp->capacity, qp->length_now);
        cmb_logger_info(stdout, "Gets an object from %s", rbp->name);

        if (qp->queue_head != NULL) {
            /* There is one ready */
            struct queue_tag *tag = qp->queue_head;
            qp->queue_head = tag->next;
            qp->length_now--;
            if (qp->queue_head == NULL) {
                qp->queue_end = NULL;
            }

            *objectloc = tag->object;
            record_sample(qp);
            cmb_dataset_add(&(qp->wait_times), cmb_time() - tag->timestamp);

            cmb_logger_info(stdout, "Success, got %p", *objectloc);
            tag->next = NULL;
            tag->object = NULL;
            cmb_mempool_put(queue_tag_pool, tag);

            cmi_resourceguard_signal(&(qp->rear_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(qp->length_now == 0u);
        cmb_logger_info(stdout, "Waiting for an object");
        const int64_t sig = cmi_resourceguard_wait(&(qp->front_guard),
                                                   queue_has_content,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %lld, returns without object",
                            sig);
            *objectloc = NULL;
            cmb_assert_debug(qp->length_now <= qp->capacity);

            return sig;
        }
    }
}

/*
 * cmb_queue_put : Put an object into the queue, if necessary waiting for free
 * space.
 *
 * Note that the object argument is a pointer to where the object is stored.
 * The return value CMB_PROCESS_SUCCESS (0) indicates that all went well. The
 * _put() call doe snot change the value at this location.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than CMB_PROCESS_SUCCESS. The
 * object pointer will still be unchanged.
 */
int64_t cmb_queue_put(struct cmb_queue *qp, void **objectloc)
{
    cmb_assert_release(qp != NULL);
    cmb_assert_release(objectloc != NULL);

    /* Lazy initalization of the memory pool for process tags */
    if (queue_tag_pool == NULL) {
        queue_tag_pool = cmb_mempool_create();
        cmb_mempool_initialize(queue_tag_pool,
                              64u,
                              sizeof(struct queue_tag));
    }

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;
    while (true) {
        cmb_assert_debug(qp->length_now <= qp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu length now %llu",
                        rbp->name, qp->capacity, qp->length_now);
        cmb_logger_info(stdout, "Puts object %p into %s", *objectloc, rbp->name);
        if (qp->length_now < qp->capacity) {
            /* There is space */
            struct queue_tag *tag = cmb_mempool_get(queue_tag_pool);
            tag->object = *objectloc;
            tag->timestamp = cmb_time();
            tag->next = NULL;

            if (qp->queue_head == NULL) {
                qp->queue_head = tag;
            }
            else {
                qp->queue_end->next = tag;
            }

            qp->queue_end = tag;
            qp->length_now++;
            cmb_assert_debug(qp->length_now <= qp->capacity);

            record_sample(qp);
            cmb_logger_info(stdout, "Success, put %p", *objectloc);
            cmi_resourceguard_signal(&(qp->front_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(qp->length_now == qp->capacity);
        cmb_logger_info(stdout, "Waiting for space");
        const int64_t sig = cmi_resourceguard_wait(&(qp->rear_guard),
                                                   queue_has_space,
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
            cmb_assert_debug(qp->length_now <= qp->capacity);

            return sig;
        }
    }
}

