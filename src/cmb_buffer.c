/*
 * cmb_buffer.c - a two-headed fixed-capacity resource where one or more
 * producer processes can put an amount into the one end, and one or more
 * consumer processes can get amounts out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
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

#include "cmb_buffer.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"

/*
 * cmb_buffer_create - Allocate memory for a buffer object.
 */
struct cmb_buffer *cmb_buffer_create(void)
{
    struct cmb_buffer *bp = cmi_malloc(sizeof *bp);
    cmi_memset(bp, 0, sizeof *bp);
    ((struct cmi_resourcebase *)bp)->cookie = CMI_UNINITIALIZED;

    return bp;
}

/*
 * cmb_buffer_initialize - Make an allocated buffer object ready for use.
 */
void cmb_buffer_initialize(struct cmb_buffer *bp,
                           const char *name,
                           uint64_t capacity)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(capacity > 0u);

    cmi_resourcebase_initialize(&(bp->core), name);

    cmb_resourceguard_initialize(&(bp->front_guard), &(bp->core));
    cmb_resourceguard_initialize(&(bp->rear_guard), &(bp->core));

    bp->capacity = capacity;
    bp->level = 0u;

    bp->is_recording = false;
    cmb_timeseries_initialize(&(bp->history));
}

/*
 * cmb_buffer_terminate - Un-initializes a buffer object.
 */
void cmb_buffer_terminate(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    cmb_resourceguard_terminate(&(bp->rear_guard));
    cmb_resourceguard_terminate(&(bp->front_guard));

    bp->is_recording = false;
    cmb_timeseries_terminate(&(bp->history));

    cmi_resourcebase_terminate(&(bp->core));
}

/*
 * cmb_buffer_destroy - Deallocates memory for a buffer object.
 */
void cmb_buffer_destroy(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);

    cmb_buffer_terminate(bp);
    cmi_free(bp);
}

/*
 * buffer_has_content - pre-packaged demand function for a cmb_buffer, allowing
 * the getting process to grab some whenever there is something to grab,
 */
static bool buffer_has_content(const struct cmi_resourcebase *rbp,
                               const struct cmb_process *pp,
                               const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_buffer *bp = (struct cmb_buffer *)rbp;

    return (bp->level > 0u);
}

/*
 * buffer_has_space - pre-packaged demand function for a cmb_buffer, allowing
 * the putting process to stuff in some whenever there is space.
 */
static bool buffer_has_space(const struct cmi_resourcebase *rbp,
                             const struct cmb_process *pp,
                             const void *ctx)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_unused(pp);
    cmb_unused(ctx);

    const struct cmb_buffer *bp = (struct cmb_buffer *)rbp;

    return (bp->level < bp->capacity);
}

static void record_sample(struct cmb_buffer *bp) {
    cmb_assert_release(bp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)bp)->cookie == CMI_INITIALIZED);

    if (bp->is_recording) {
        struct cmb_timeseries *ts = &(bp->history);
        cmb_timeseries_add(ts, (double)(bp->level), cmb_time());
    }
}

void cmb_buffer_start_recording(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)bp)->cookie == CMI_INITIALIZED);

    bp->is_recording = true;
    record_sample(bp);
}

void cmb_buffer_stop_recording(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)bp)->cookie == CMI_INITIALIZED);

    record_sample(bp);
    bp->is_recording = false;
}

struct cmb_timeseries *cmb_buffer_history(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)bp)->cookie == CMI_INITIALIZED);

    return &(bp->history);
}

void cmb_buffer_print_report(struct cmb_buffer *bp, FILE *fp) {
    cmb_assert_release(bp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)bp)->cookie == CMI_INITIALIZED);

    const struct cmb_timeseries *ts = &(bp->history);

    fprintf(fp, "Buffer levels for %s\n", bp->core.name);
    struct cmb_wtdsummary *ws = cmb_wtdsummary_create();
    (void)cmb_timeseries_summarize(ts, ws);
    cmb_wtdsummary_print(ws, fp, true);
    cmb_wtdsummary_destroy(ws);

    const unsigned nbin = (bp->capacity > 20) ? 20 : bp->capacity + 1;
    cmb_timeseries_histogram_print(ts, fp, nbin, 0.0, (double)(bp->capacity + 1u));
}

/*
 * cmb_buffer_get - Request and, if necessary, wait for an amount of the
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
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    const uint64_t init_claim = *amntp;
    uint64_t rem_claim = *amntp;
    *amntp = 0u;
    while (true) {
        cmb_assert_debug(bp->level <= bp->capacity);
        cmb_logger_info(stdout, "Gets %" PRIu64 " from %s, level %" PRIu64,
                        rem_claim, rbp->name, bp->level);
        if (bp->level >= rem_claim) {
            /* Grab what we need */
            bp->level -= rem_claim;
            record_sample(bp);
            *amntp += rem_claim;
            cmb_logger_info(stdout,
                            "Success, %" PRIu64 " was available, got %" PRIu64,
                            rem_claim, *amntp);

            cmb_assert_debug(bp->level <= bp->capacity);
            cmb_resourceguard_signal(&(bp->rear_guard));
            if (bp->level > 0u) {
                /* In case someone else can use any leftovers */
                cmb_resourceguard_signal(&(bp->front_guard));
            }

            return CMB_PROCESS_SUCCESS;
        }
        else if (bp->level > 0u) {
            /* Grab what is there */
            const uint64_t grab = bp->level;
            bp->level = 0u;
            record_sample(bp);
            *amntp += grab;
            rem_claim -= grab;
            cmb_logger_info(stdout,
                            "Grabbed %" PRIu64 ", has %" PRIu64,
                            grab, *amntp);

            cmb_assert_debug(bp->level <= bp->capacity);
            cmb_resourceguard_signal(&(bp->rear_guard));
        }

        /* Wait at the front door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        cmb_logger_info(stdout, "Waiting for more, level now %" PRIu64,
                        bp->level);
        cmb_resourceguard_signal(&(bp->rear_guard));
        const int64_t sig = cmb_resourceguard_wait(&(bp->front_guard),
                                                   buffer_has_content,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Returned successfully from wait");
        }
        else {
            cmb_logger_info(stdout,
                            "Wait interrupted by signal %" PRIi64 " wanted %" PRIu64 ", returns with %" PRIu64,
                            sig, init_claim, *amntp);

            cmb_assert_debug(bp->level <= bp->capacity);
            cmb_assert_debug(*amntp <= init_claim);

            return sig;
        }
    }
}

/*
 * cmb_buffer_put - Put a non-zero amount of the resource into the buffer,
 * waiting for free space if necessary.
 *
 * Note that the amount argument is a pointer to where the amount is stored.
 * The return value CMB_PROCESS_NORMAL (0) indicates that all went well and
 * the value *amntp now equals zero.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
 * and *amntp will be the quantity remaining when interrupted. The return
 * value is the interrupt signal received, some value other than
 * CMB_PROCESS_NORMAL.
 */
int64_t cmb_buffer_put(struct cmb_buffer *bp, uint64_t *amntp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(amntp != NULL);
    cmb_assert_release(*amntp > 0u);

    struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)bp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    const uint64_t init_claim = *amntp;
    uint64_t rem_claim = *amntp;
    while (true) {
        cmb_assert_debug(bp->level <= bp->capacity);
        cmb_logger_info(stdout, "Puts %" PRIu64 " into %s, level %" PRIu64,
                        rem_claim, rbp->name, bp->level);
        if ((bp->capacity - bp->level) >= rem_claim) {
            /* Push the remainder into the buffer */
            bp->level += rem_claim;
            record_sample(bp);
            *amntp -= rem_claim;
            cmb_logger_info(stdout,
                            "Success, found room for %" PRIu64 ", has %" PRIu64 " remaining",
                            rem_claim, *amntp);

            cmb_assert_debug(bp->level <= bp->capacity);
            cmb_resourceguard_signal(&(bp->front_guard));
            if (bp->level < bp->capacity) {
                /* In case someone else can use any leftover space */
                cmb_resourceguard_signal(&(bp->rear_guard));
            }

            return CMB_PROCESS_SUCCESS;
        }
        else if (bp->level < bp->capacity) {
            /* Fill 'er up */
            const uint64_t grab = bp->capacity - bp->level;
            bp->level = bp->capacity;
            record_sample(bp);
            *amntp -= grab;
            rem_claim -= grab;
            cmb_logger_info(stdout,
                            "Pushed in %" PRIu64 ", still has %" PRIu64 " remaining",
                            grab,  *amntp);

            cmb_resourceguard_signal(&(bp->front_guard));
        }

        /* Wait at the back door until some more becomes available  */
        cmb_assert_debug(rem_claim > 0u);
        cmb_logger_info(stdout, "Waiting for space, level %" PRIu64, bp->level);
        cmb_resourceguard_signal(&(bp->front_guard));
        const int64_t sig = cmb_resourceguard_wait(&(bp->rear_guard),
                                                   buffer_has_space,
                                                   NULL);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_info(stdout,"Returned successfully from wait");
        }
        else {
            cmb_logger_info(stdout,
                            "Wait interrupted by signal %" PRIi64 ", had %" PRIu64 ", returns with %" PRIu64 " left",
                            sig, init_claim, *amntp);

            cmb_assert_debug(bp->level <= bp->capacity);
            cmb_assert_debug(*amntp <= init_claim);

            return sig;
        }
    }
}


