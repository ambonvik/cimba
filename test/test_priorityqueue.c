/*
* Test script for priority queues
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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmb_priorityqueue.h"
#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_random.h"

#include "cmi_memutils.h"
#include "test.h"

#define USERFLAG1 0x00000001
#define NUM_PUTTERS 3u
#define NUM_GETTERS 3u

CMB_THREAD_LOCAL struct cmi_mempool objectpool
    = CMI_MEMPOOL_STATIC_INIT(sizeof(void *), 512u);

struct simulation {
    struct cmb_process *putters[NUM_PUTTERS];
    struct cmb_process *getters[NUM_GETTERS];
    struct cmb_process *nuisance;
    struct cmb_priorityqueue *queue;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_assert_release(subject != NULL);
    cmb_unused(object);

    const struct simulation *texp = subject;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_PUTTERS; ui++) {
        const int64_t r = cmb_process_stop(texp->putters[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    }

    for (unsigned ui = 0; ui < NUM_GETTERS; ui++) {
        const int64_t r = cmb_process_stop(texp->getters[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    }

    const int64_t r = cmb_process_stop(texp->nuisance, NULL);
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
}

void *putterfunc(struct cmb_process *me, void *vctx)
{
    cmb_assert_release(vctx != NULL);

    struct cmb_priorityqueue *qp = (struct cmb_priorityqueue *)vctx;
    const int64_t pri = cmb_process_priority(me);

    for (;;) {
        cmb_logger_user(stdout, USERFLAG1, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Hold returned normally");
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);
        }

        void *object = cmi_mempool_alloc(&objectpool);
        cmb_assert_always(object != NULL);
        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Putting object %p priority %" PRIi64 " into %s...",
                        object, pri,
                        cmb_priorityqueue_name(qp));
        cmb_assert_always(cmb_priorityqueue_length(qp) <= qp->capacity);
        cmb_assert_always(cmb_priorityqueue_space(qp) <= qp->capacity);
        uint64_t handle = 0u;
        sig = cmb_priorityqueue_put(qp, object, pri, &handle);
        cmb_assert_always(cmb_priorityqueue_length(qp) <= qp->capacity);
        cmb_assert_always(cmb_priorityqueue_space(qp) <= qp->capacity);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Put succeeded");
            cmb_assert_always(handle != 0u);
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Put returned signal %" PRIi64, sig);
            cmb_assert_always(handle == 0u);
            cmb_assert_always(object != NULL);
            cmi_mempool_free(&objectpool, object);
        }
    }
}

void *getterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    struct cmb_priorityqueue *qp = (struct cmb_priorityqueue *)ctx;

    for (;;) {
        cmb_logger_user(stdout, USERFLAG1, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Hold returned normally");
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);
        }

        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Getting object from %s...",
                        cmb_priorityqueue_name(qp));
        cmb_assert_always(cmb_priorityqueue_length(qp) <= qp->capacity);
        cmb_assert_always(cmb_priorityqueue_space(qp) <= qp->capacity);
        void *object = NULL;
        sig = cmb_priorityqueue_get(qp, &object);
        cmb_assert_always(cmb_priorityqueue_length(qp) <= qp->capacity);
        cmb_assert_always(cmb_priorityqueue_space(qp) <= qp->capacity);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Get succeeded");
            cmb_assert_always(object != NULL);
            cmi_mempool_free(&objectpool, object);
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Get returned signal %" PRIi64, sig);
            cmb_assert_always(object == NULL);
        }
    }
}

void *nuisancefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    /* Abuse internal knowledge of the content of the simulation struct,
     * treating it as one-dimensional array of all processes */
    struct cmb_process **tgt = (struct cmb_process **)ctx;
    const unsigned nproc = NUM_PUTTERS + NUM_GETTERS;

    for (;;) {
        cmb_logger_user(stdout, USERFLAG1, "Holding ...");
        double dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        int64_t sig = cmb_process_hold(dt);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
        const uint16_t vic = cmb_random_dice(0, (long)(nproc - 1u));
        cmb_assert_always(vic < (uint16_t)nproc);
        sig = cmb_random_dice(1, 10);
        cmb_assert_always((sig >= 1) && (sig <= 10));
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_assert_always((pri >= -5) && (pri <= 5));
        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Interrupting %s with signal %" PRIi64,
                        tgt[vic]->name,
                        sig);
        cmb_process_interrupt(tgt[vic], sig, pri);
    }
}

void test_priorityqueue(const uint64_t seed, const double duration)
{
    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("*************************   Testing priority queues   **************************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    struct simulation *quetst = cmi_malloc(sizeof(*quetst));
    cmi_memset(quetst, 0, sizeof(*quetst));

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);

    printf("Create a priority queue\n");
    quetst->queue = cmb_priorityqueue_create();
    cmb_priorityqueue_initialize(quetst->queue, "Queue", 10u);
    cmb_priorityqueue_recording_start(quetst->queue);

    char scratchpad[32];
    printf("Create three processes feeding into the queue\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        quetst->putters[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Putter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(quetst->putters[ui],
                               scratchpad,
                               putterfunc,
                               quetst->queue,
                               pri);
        cmb_process_start(quetst->putters[ui]);
    }

    printf("Create three processes consuming from the queue\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        quetst->getters[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Getter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(quetst->getters[ui],
                               scratchpad,
                               getterfunc,
                               quetst->queue,
                               pri);
        cmb_process_start(quetst->getters[ui]);
    }

    printf("Create a nuisance\n");
    quetst->nuisance = cmb_process_create();
    cmb_process_initialize(quetst->nuisance, "Nuisance", nuisancefunc, quetst, 0);
    cmb_process_start(quetst->nuisance);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, quetst, NULL, duration, 0);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Report statistics...\n");
    cmb_priorityqueue_recording_stop(quetst->queue);
    cmb_priorityqueue_report_print(quetst->queue, stdout);

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        cmb_process_terminate(quetst->putters[ui]);
        cmb_process_terminate(quetst->getters[ui]);
        cmb_process_destroy(quetst->putters[ui]);
        cmb_process_destroy(quetst->getters[ui]);
    }

    cmb_process_terminate(quetst->nuisance);
    cmb_process_destroy(quetst->nuisance);
    cmb_priorityqueue_destroy(quetst->queue);
    cmb_event_queue_terminate();
    cmi_free(quetst);
    cmb_random_terminate();
    cmi_test_print_line("*");
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 100000.0;

    int opt;
    while ((opt = getopt(argc, argv, "d: s:t")) != -1) {
        switch (opt) {
            case 'd':
                errno = 0;
                dur = strtod(optarg, NULL);
                if (errno != 0 || dur <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 't':
                timing_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    const clock_t start_time = clock();

    test_priorityqueue(seed, dur);

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}

