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
 * cmb_objectqueue_create : Allocate memory for a queue object.
 */
struct cmb_objectqueue *cmb_objectqueue_create(void)
{
    struct cmb_objectqueue *oqp = cmi_malloc(sizeof *oqp);
    cmi_memset(oqp, 0, sizeof *oqp);
    ((struct cmi_resourcebase *)oqp)->cookie = CMI_UNINITIALIZED;

    return oqp;
}

/*
 * cmb_objectqueue_initialize : Make an allocated queue object ready for use.
 */
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
    oqp->length_now = 0u;

    oqp->queue_head = NULL;
    oqp->queue_end = NULL;

    oqp->is_recording = false;
    cmb_timeseries_initialize(&(oqp->history));
    cmb_dataset_initialize(&(oqp->wait_times));
}

/*
 * cmb_objectqueue_terminate : Un-initializes a queue object.
 */
void cmb_objectqueue_terminate(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);

    while (oqp->queue_head != NULL) {
        struct queue_tag *tag = oqp->queue_head;
        oqp->queue_head = tag->next;
        cmb_mempool_put(queue_tag_pool, tag);
    }

    oqp->length_now = 0u;
    oqp->queue_head = NULL;
    oqp->queue_end = NULL;

    cmb_dataset_terminate(&(oqp->wait_times));
    cmb_timeseries_terminate(&(oqp->history));

    cmi_resourceguard_terminate(&(oqp->rear_guard));
    cmi_resourceguard_terminate(&(oqp->front_guard));
    cmi_resourcebase_terminate(&(oqp->core));
}

/*
 * cmb_objectqueue_destroy : Deallocates memory for a queue object.
 */
void cmb_objectqueue_destroy(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);

    cmb_objectqueue_terminate(oqp);
    cmi_free(oqp);
}

/*
 * queue_has_content : pre-packaged demand function for a cmb_objectqueue, allowing
 * the getting process to grab some whenever there is something to grab.
 */
static bool queue_has_content(const struct cmi_resourcebase *rbp,
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
 * queue_has_space : pre-packaged demand function for a cmb_objectqueue, allowing
 * the putting process to stuff in some whenever there is space.
 */
static bool queue_has_space(const struct cmi_resourcebase *rbp,
                             const struct cmb_process *pp,
                             const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_objectqueue *oqp = (struct cmb_objectqueue *)rbp;

    return (oqp->length_now < oqp->capacity);
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
        cmb_timeseries_add(ts, (double)(oqp->length_now), cmb_time());
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

/*
 * cmb_objectqueue_get : Request and if necessary wait for an amount of the
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
int64_t cmb_objectqueue_get(struct cmb_objectqueue *oqp, void **objectloc)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(objectloc != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    while (true) {
        cmb_assert_debug(oqp->length_now <= oqp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu length now %llu",
                       rbp->name, oqp->capacity, oqp->length_now);
        cmb_logger_info(stdout, "Gets an object from %s", rbp->name);

        if (oqp->queue_head != NULL) {
            /* There is one ready */
            struct queue_tag *tag = oqp->queue_head;
            oqp->queue_head = tag->next;
            oqp->length_now--;
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
            cmb_mempool_put(queue_tag_pool, tag);

            cmi_resourceguard_signal(&(oqp->rear_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(oqp->length_now == 0u);
        cmb_logger_info(stdout, "Waiting for an object");
        const int64_t sig = cmi_resourceguard_wait(&(oqp->front_guard),
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
            cmb_assert_debug(oqp->length_now <= oqp->capacity);

            return sig;
        }
    }
}

/*
 * cmb_objectqueue_put : Put an object into the queue, if necessary waiting for free
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
int64_t cmb_objectqueue_put(struct cmb_objectqueue *oqp, void **objectloc)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(objectloc != NULL);

    /* Lazy initalization of the memory pool for process tags */
    if (queue_tag_pool == NULL) {
        queue_tag_pool = cmb_mempool_create();
        cmb_mempool_initialize(queue_tag_pool,
                              64u,
                              sizeof(struct queue_tag));
    }

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    while (true) {
        cmb_assert_debug(oqp->length_now <= oqp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu length now %llu",
                        rbp->name, oqp->capacity, oqp->length_now);
        cmb_logger_info(stdout, "Puts object %p into %s", *objectloc, rbp->name);
        if (oqp->length_now < oqp->capacity) {
            /* There is space */
            struct queue_tag *tag = cmb_mempool_get(queue_tag_pool);
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
            oqp->length_now++;
            cmb_assert_debug(oqp->length_now <= oqp->capacity);

            record_sample(oqp);
            cmb_logger_info(stdout, "Success, put %p", *objectloc);
            cmi_resourceguard_signal(&(oqp->front_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(oqp->length_now == oqp->capacity);
        cmb_logger_info(stdout, "Waiting for space");
        const int64_t sig = cmi_resourceguard_wait(&(oqp->rear_guard),
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
            cmb_assert_debug(oqp->length_now <= oqp->capacity);

            return sig;
        }
    }
}

