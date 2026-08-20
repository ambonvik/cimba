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
#include "cmb_objectqueue.h"
#include "cmb_process.h"

#include "cmi_mempool.h"
#include "cmi_memutils.h"

/*
 * struct queue_tag - A tag for the singly linked list that is a queue.
 * We need a circular list here and do not use the cmi_slist implementation.
 */
struct queue_tag {
    struct queue_tag *next;
    void *object;
};

/* Thread local mempool of queue tags */
static CMB_THREAD_LOCAL struct cmi_mempool objectqueue_tags
    = CMI_MEMPOOL_STATIC_INIT(sizeof(struct queue_tag), 16u);

struct cmb_objectqueue *cmb_objectqueue_create(void)
{
    struct cmb_objectqueue *oqp = cmi_malloc(sizeof *oqp);
    cmi_memset(oqp, 0, sizeof *oqp);
    struct cmi_resourcebase *rbp = &(oqp->base);
    rbp->cookie = CMI_UNINITIALIZED;

    /* Add teardown function to the memregistry in case we need to bail out */
    cmi_dlist_initialize(&(oqp->destroy.node));
    oqp->destroy.teardown = (cmi_teardown_func *)cmb_objectqueue_destroy;
    oqp->destroy.object = oqp;
    cmi_memregistry_add(&(oqp->destroy));

    cmb_assert_debug(oqp->base.cookie == CMI_UNINITIALIZED);
    return oqp;
}

void cmb_objectqueue_initialize(struct cmb_objectqueue *oqp,
                                const char *name,
                                const uint64_t capacity)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);
    /* Might get raw memory with random content, cannot assert _UNINITIALIZED */

    /* Initializing base class creates a memregistry item. */
    struct cmi_resourcebase *rbp = &(oqp->base);
    cmi_resourcebase_initialize(rbp, name);

    /* The memregistry item is now pointing to cmi_resourcebase_terminate.
     * Redirect it to ours (do not call cmb_logger_error in the meantime!) */
    cmb_assert_debug(rbp->terminate.object == oqp);
    rbp->terminate.teardown = (cmi_teardown_func *)cmb_objectqueue_terminate;
    /* OK, consistent now, continue own initialization */

    /* Connect the resource guards to the actual resource */
    cmb_resourceguard_initialize(&(oqp->front_guard), rbp);
    cmb_resourceguard_initialize(&(oqp->rear_guard), rbp);

    oqp->capacity = capacity;
    oqp->length = 0u;
    oqp->queue_head = NULL;
    oqp->queue_end = NULL;

    /* Initialize data collector */
    oqp->is_recording = false;
    cmb_timeseries_initialize(&(oqp->history));

    cmb_assert_debug(rbp->cookie == CMI_INITIALIZED);
}

void cmb_objectqueue_terminate(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    struct cmi_resourcebase *rbp = &(oqp->base);
    cmb_assert_release((rbp->cookie == CMI_INITIALIZED)
                    || cmi_memregistry_is_demolishing);

    if (rbp->cookie == CMI_INITIALIZED) {
        while (oqp->queue_head != NULL) {
            struct queue_tag *tag = oqp->queue_head;
            oqp->queue_head = tag->next;
            cmi_mempool_free(&objectqueue_tags, tag);
        }

        oqp->length = 0u;
        oqp->queue_head = NULL;
        oqp->queue_end = NULL;

        cmb_timeseries_terminate(&(oqp->history));
        cmb_resourceguard_terminate(&(oqp->rear_guard));
        cmb_resourceguard_terminate(&(oqp->front_guard));

        /* Terminating the parent class will also clear the memregistry item */
        cmi_resourcebase_terminate(&(oqp->base));
    }

    cmb_assert_debug(rbp->cookie == CMI_UNINITIALIZED);
}

void cmb_objectqueue_destroy(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    struct cmi_resourcebase *rbp = &(oqp->base);
    /* Call cmb_objectqueue_terminate first, please */
    cmb_assert_debug(rbp->cookie == CMI_UNINITIALIZED);

    if (!cmi_memregistry_is_demolishing) {
        /* Destroying normally, remove from register */
        cmi_memregistry_remove(&(oqp->destroy));
    }

    cmi_free(oqp);
}

/*
 * has_content - pre-packaged demand function for a cmb_objectqueue, allowing
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

    const struct cmb_objectqueue *oqp = (struct cmb_objectqueue *)rbp;

    return (oqp->queue_head != NULL);
}

/*
 * has_space - pre-packaged demand function for a cmb_objectqueue, allowing
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

static void record_sample(struct cmb_objectqueue *oqp) {
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(oqp->base.cookie == CMI_INITIALIZED);

    if (oqp->is_recording) {
        struct cmb_timeseries *ts = &(oqp->history);
        cmb_timeseries_add(ts, (double)(oqp->length), cmb_time());
    }
}

void cmb_objectqueue_recording_start(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(oqp->base.cookie == CMI_INITIALIZED);

    oqp->is_recording = true;
    record_sample(oqp);
}

void cmb_objectqueue_recording_stop(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(oqp->base.cookie == CMI_INITIALIZED);

    record_sample(oqp);
    oqp->is_recording = false;
}

struct cmb_timeseries *cmb_objectqueue_history(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(oqp->base.cookie == CMI_INITIALIZED);

    return &(oqp->history);
}

void cmb_objectqueue_report_print(struct cmb_objectqueue *oqp, FILE *fp) {
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(oqp->base.cookie == CMI_INITIALIZED);

    fprintf(fp, "Queue lengths for %s:\n", oqp->base.name);
    const struct cmb_timeseries *ts = &(oqp->history);
    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    cmb_wtdsummary_initialize(ws);
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_terminate(ws);
    cmb_wtdsummary_destroy(ws);

    const unsigned nbin = (oqp->capacity > 20) ? 20 : oqp->capacity + 1;
    cmb_timeseries_histogram_print(ts, fp, nbin, 0.0, (double)(oqp->capacity + 1u));
}

int64_t cmb_objectqueue_get(struct cmb_objectqueue *oqp, void **objectloc)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(objectloc != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    cmb_logger_info(stdout, "Gets an object from %s, length now %" PRIu64,
                    rbp->name, oqp->length);
    while (true) {
        cmb_assert_debug(oqp->length <= oqp->capacity);

        if (oqp->queue_head != NULL) {
            /* There is one ready */
            struct queue_tag *tag = oqp->queue_head;
            cmb_assert_debug(sizeof(*tag) == 16u);
            oqp->queue_head = tag->next;
            oqp->length--;
            if (oqp->queue_head == NULL) {
                oqp->queue_end = NULL;
            }

            *objectloc = tag->object;
            if (oqp->is_recording) {
                record_sample(oqp);
            }

            cmb_logger_info(stdout, "Success, got %p", *objectloc);
            tag->next = NULL;
            tag->object = NULL;
            cmi_mempool_free(&objectqueue_tags, tag);

            cmb_resourceguard_signal(&(oqp->rear_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(oqp->length == 0u);
        cmb_logger_info(stdout, "Waiting for an object");
        const int64_t sig = cmb_resourceguard_wait(&(oqp->front_guard),
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
            cmb_assert_debug(oqp->length <= oqp->capacity);

            return sig;
        }
    }
}

int64_t cmb_objectqueue_put(struct cmb_objectqueue *oqp, void *object)
{
    cmb_assert_release(oqp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_logger_info(stdout, "Puts object %p into %s, length %" PRIu64,
                    object, rbp->name, oqp->length);
    while (true) {
        cmb_assert_debug(oqp->length <= oqp->capacity);
        if (oqp->length < oqp->capacity) {
            /* There is space */
            struct queue_tag *tag = cmi_mempool_alloc(&objectqueue_tags);
            tag->object = object;
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
            cmb_logger_info(stdout, "Success, put %p", object);
            cmb_resourceguard_signal(&(oqp->front_guard));

            return CMB_PROCESS_SUCCESS;
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(oqp->length == oqp->capacity);
        cmb_logger_info(stdout, "Waiting for space");
        const int64_t sig = cmb_resourceguard_wait(&(oqp->rear_guard),
                                                   has_space,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %" PRIi64 ", could not put object %p into %s",
                            sig, object, rbp->name);
            cmb_assert_debug(oqp->length <= oqp->capacity);

            return sig;
        }
    }
}

/* Note that NULL may be a valid object value */
uint64_t cmb_objectqueue_position(struct cmb_objectqueue *oqp, void *object)
{
    cmb_assert_release(oqp != NULL);

    if (oqp->queue_head == NULL || oqp->length == 0u) {
        return 0u;
    }

    uint64_t pos = 0u;
    const struct queue_tag *tag = oqp->queue_head;
    while (tag != NULL) {
        pos++;
        if (tag->object == object) {
            return pos;
        }
        tag = tag->next;
    }

    return 0u;
}
