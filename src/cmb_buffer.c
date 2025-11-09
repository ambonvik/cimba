/*
 * cmb_buffer.c - a two-headed fixed-capacity resource where one or more
 * producer processes can put an amount into the one end, and one or more
 * consumer processes can get amounts out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
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
#include "cmb_buffer.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "cmi_memutils.h"

/*
 * cmb_buffer_create : Allocate memory for a buffer object.
 */
struct cmb_buffer *cmb_buffer_create(void)
{
    struct cmb_buffer *bp = cmi_malloc(sizeof *bp);
    cmi_memset(bp, 0, sizeof *bp);

    return bp;
}

/*
 * cmb_buffer_initialize : Make an allocated buffer object ready for use.
 */
void cmb_buffer_initialize(struct cmb_buffer *bp,
                           const char *name,
                           uint64_t capacity)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_resourcebase_initialize(&(bp->core), name);

    cmi_resourceguard_initialize(&(bp->front_guard), &(bp->core));
    cmi_resourceguard_initialize(&(bp->rear_guard), &(bp->core));

    bp->capacity = capacity;
    bp->contains = 0u;
}

/*
 * cmb_buffer_terminate : Un-initializes a buffer object.
 */
void cmb_buffer_terminate(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    cmi_resourceguard_terminate(&(bp->rear_guard));
    cmi_resourceguard_terminate(&(bp->front_guard));
    cmi_resourcebase_terminate(&(bp->core));
}

/*
 * cmb_buffer_destroy : Deallocates memory for a buffer object.
 */
void cmb_buffer_destroy(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    cmb_buffer_terminate(bp);
    cmi_free(bp);
}

/*
 * buffer_has_content : pre-packaged demand function for a cmb_buffer, allowing
 * the getting process to grab some whenever there is something to grab,
 */
static bool buffer_has_content(const struct cmi_resourcebase *rbp,
                               const struct cmb_process *pp,
                               const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_buffer *bp = (struct cmb_buffer *)rbp;

    return (bp->contains > 0u);
}

/*
 * buffer_has_space : pre-packaged demand function for a cmb_buffer, allowing
 * the putting process to stuff in some whenever there is space.
 */
static bool buffer_has_space(const struct cmi_resourcebase *rbp,
                             const struct cmb_process *pp,
                             const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_buffer *bp = (struct cmb_buffer *)rbp;

    return (bp->contains < bp->capacity);
}

static void record_sample(struct cmb_buffer *bp) {
    cmb_assert_release(bp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;
    if (rbp->is_recording) {
        struct cmb_timeseries *ts = &(rbp->history);
        cmb_timeseries_add(ts, (double)(bp->contains), cmb_time());
    }
}

void cmb_buffer_start_recording(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;
    rbp->is_recording = true;
    record_sample(bp);
}

void cmb_buffer_stop_recording(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;
    record_sample(bp);
    rbp->is_recording = false;
}

struct cmb_timeseries *cmb_buffer_get_history(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;

    return &(rbp->history);
}

void cmb_buffer_print_report(struct cmb_buffer *bp, FILE *fp) {
    cmb_assert_release(bp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;
    const struct cmb_timeseries *ts = &(rbp->history);

    fprintf(fp, "Buffer levels for %s\n", bp->core.name);
    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);

    const unsigned nbin = (bp->capacity > 20) ? 20 : bp->capacity + 1;
    cmb_timeseries_print_histogram(ts, fp, nbin, 0.0, (double)(bp->capacity + 1u));
}

/*
 * cmb_buffer_get : Request and if necessary wait for an amount of the
 * buffer resource.
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
int64_t cmb_buffer_get(struct cmb_buffer *bp, uint64_t *amntp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(amntp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;

    const uint64_t init_claim = *amntp;
    uint64_t rem_claim = *amntp;
    *amntp = 0u;
    while (true) {
        cmb_assert_debug(bp->contains <= bp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu contains %llu",
                       rbp->name, bp->capacity, bp->contains);
        cmb_logger_info(stdout, "Gets %llu from %s", rem_claim, rbp->name);
        if (bp->contains >= rem_claim) {
            /* Grab what we need */
            bp->contains -= rem_claim;
            record_sample(bp);
            *amntp += rem_claim;
            cmb_logger_info(stdout,
                            "Success, %llu was available, got %llu",
                            rem_claim,
                            *amntp);
            rem_claim = 0u;

            cmb_assert_debug(bp->contains <= bp->capacity);
            cmi_resourceguard_signal(&(bp->rear_guard));
            if (bp->contains > 0u) {
                /* In case someone else can use any leftovers */
                cmi_resourceguard_signal(&(bp->front_guard));
            }

            return CMB_PROCESS_SUCCESS;
        }
        else if (bp->contains > 0u) {
            /* Grab what is there */
            const uint64_t grab = bp->contains;
            bp->contains = 0u;
            record_sample(bp);
            *amntp += grab;
            rem_claim -= grab;
            cmb_logger_info(stdout,
                            "Grabbed %llu, has %llu",
                            grab,
                            *amntp);

            cmb_assert_debug(bp->contains <= bp->capacity);
            cmi_resourceguard_signal(&(bp->rear_guard));
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        cmb_logger_info(stdout, "Waiting for content");
        cmb_logger_info(stdout, "%s capacity %llu contains %llu",
                       rbp->name, bp->capacity, bp->contains);
        cmi_resourceguard_signal(&(bp->rear_guard));
        const int64_t sig = cmi_resourceguard_wait(&(bp->front_guard),
                                                   buffer_has_content,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %lld, wanted %lld, returns with %llu",
                            sig,
                            init_claim,
                            *amntp);

            cmb_assert_debug(bp->contains <= bp->capacity);
            cmb_assert_debug(*amntp <= init_claim);

            return sig;
        }
    }
}

/*
 * cmb_buffer_put : Put an amount of the resource into the buffer, if necessary
 * waiting for free space.
 *
 * Note that the amount argument is a pointer to where the amount is stored.
 * The return value CMB_PROCESS_NORMAL (0) indicates that all went well and
 * the value *amount now equals zero.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
 * and *amount will be the quantity remaining when interrupted. The return
 * value is the interrupt signal received, some value other than
 * CMB_PROCESS_NORMAL.
 */
int64_t cmb_buffer_put(struct cmb_buffer *bp, uint64_t *amntp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(amntp != NULL);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;
    const uint64_t init_claim = *amntp;
    uint64_t rem_claim = *amntp;
    while (true) {
        cmb_assert_debug(bp->contains <= bp->capacity);
        cmb_logger_info(stdout, "%s capacity %llu contains %llu",
                        rbp->name, bp->capacity, bp->contains);
        cmb_logger_info(stdout, "Puts %lld into %s", rem_claim, rbp->name);
        if ((bp->capacity - bp->contains) >= rem_claim) {
            /* Push the remainder into the buffer */
            bp->contains += rem_claim;
            record_sample(bp);
            *amntp -= rem_claim;
            cmb_logger_info(stdout,
                            "Success, found room for %llu, has %llu remaining",
                            rem_claim,
                            *amntp);
            rem_claim = 0u;

            cmb_assert_debug(bp->contains <= bp->capacity);
            cmi_resourceguard_signal(&(bp->front_guard));
            if (bp->contains < bp->capacity) {
                /* In case someone else can use any leftover space */
                cmi_resourceguard_signal(&(bp->rear_guard));
            }

            return CMB_PROCESS_SUCCESS;
        }
        else if (bp->contains < bp->capacity) {
            /* Fill 'er up */
            const uint64_t grab = bp->capacity - bp->contains;
            bp->contains = bp->capacity;
            record_sample(bp);
            *amntp -= grab;
            rem_claim -= grab;
            cmb_logger_info(stdout,
                            "Pushed in %llu, still has %llu remaining",
                            grab,
                            *amntp);

            cmi_resourceguard_signal(&(bp->front_guard));
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        cmb_logger_info(stdout, "Waiting for space");
        cmb_logger_info(stdout, "%s capacity %llu contains %llu",
                       rbp->name, bp->capacity, bp->contains);
        cmi_resourceguard_signal(&(bp->front_guard));
        const int64_t sig = cmi_resourceguard_wait(&(bp->rear_guard),
                                                   buffer_has_space,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Trying again");
        }
        else {
            cmb_logger_info(stdout,
                            "Interrupted by signal %lld, had %lld, returns with %llu left",
                            sig,
                            init_claim,
                            *amntp);

            cmb_assert_debug(bp->contains <= bp->capacity);
            cmb_assert_debug(*amntp <= init_claim);

            return sig;
        }
    }
}


