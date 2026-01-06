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

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

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

struct simulation {
    struct cmb_process *putters[NUM_PUTTERS];
    struct cmb_process *getters[NUM_PUTTERS];
    struct cmb_process *nuisance;
    struct cmb_priorityqueue *queue;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(object);

    struct simulation *texp = subject;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_PUTTERS; ui++) {
        cmb_process_stop(texp->putters[ui], NULL);
    }

    for (unsigned ui = 0; ui < NUM_GETTERS; ui++) {
        cmb_process_stop(texp->getters[ui], NULL);
    }

    cmb_process_stop(texp->nuisance, NULL);

    /* Make sure that we got everything */
    cmb_event_queue_clear();
}

void *putterfunc(struct cmb_process *me, void *vctx)
{
    cmb_assert_release(vctx != NULL);
    struct cmb_priorityqueue *qp = (struct cmb_priorityqueue *)vctx;
    const int64_t pri = cmb_process_priority(me);

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(stdout, USERFLAG1, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Hold returned normally");
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);
        }

        void *object = cmi_mempool_get(&cmi_mempool_8b);
        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Putting object %p priority %" PRIi64 " into %s...",
                        object, pri,
                        cmb_priorityqueue_name(qp));

        sig = cmb_priorityqueue_put(qp, &object, pri);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Put succeeded");
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Put returned signal %" PRIi64, sig);
            cmi_mempool_put(&cmi_mempool_8b, object);
        }
    }
}

void *getterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    struct cmb_priorityqueue *qp = (struct cmb_priorityqueue *) ctx;

    // ReSharper disable once CppDFAEndlessLoop
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
        void *object = NULL;
        sig = cmb_priorityqueue_get(qp, &object);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1, "Get succeeded");
            cmi_mempool_put(&cmi_mempool_8b, object);
        }
        else {
            cmb_logger_user(stdout, USERFLAG1, "Get returned signal %" PRIi64, sig);
        }
    }
}

void *nuisancefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    /* Abuse internal knowledge of the content of the simulation struct */
    struct cmb_process **tgt = (struct cmb_process **)ctx;
    unsigned nproc = NUM_PUTTERS + NUM_GETTERS;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(stdout, USERFLAG1, "Holding ...");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        const uint16_t vic = cmb_random_dice(0, (long)(nproc - 1u));
        const int64_t sig = cmb_random_dice(1, 10);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Interrupting %s with signal %" PRIi64,
                        tgt[vic]->name,
                        sig);
        cmb_process_interrupt(tgt[vic], sig, pri);
    }
}

void test_priorityqueue(const double duration)
{
    struct simulation *quetst = cmi_malloc(sizeof(*quetst));
    cmi_memset(quetst, 0, sizeof(*quetst));

    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);
    printf("seed: %" PRIx64 "\n", seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);

    printf("Create a priority queue\n");
    quetst->queue = cmb_priorityqueue_create();
    cmb_priorityqueue_initialize(quetst->queue, "Queue", 10u);
    cmb_priorityqueue_start_recording(quetst->queue);

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
    cmb_priorityqueue_stop_recording(quetst->queue);
    cmb_priorityqueue_print_report(quetst->queue, stdout);

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
}

int main(void)
{
    cmi_test_print_line("*");
    printf("**************************   Testing priority queues   ***************************\n");
    cmi_test_print_line("*");

    test_priorityqueue(1000000);

    cmi_test_print_line("*");
    return 0;
}

