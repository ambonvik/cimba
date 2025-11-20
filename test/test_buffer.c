/*
* Test script for buffers
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

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_buffer.h"

#include "cmi_memutils.h"
#include "test.h"

#define USERFLAG 0x00000001
#define NUM_PUTTERS 3u
#define NUM_GETTERS 3u

struct simulation {
    struct cmb_process *putters[NUM_PUTTERS];
    struct cmb_process *getters[NUM_PUTTERS];
    struct cmb_process *nuisance;
    struct cmb_buffer *buf;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *thesim = subject;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_PUTTERS; ui++) {
        cmb_process_stop(thesim->putters[ui], NULL);
    }

    for (unsigned ui = 0; ui < NUM_GETTERS; ui++) {
        cmb_process_stop(thesim->getters[ui], NULL);
    }

    cmb_process_stop(thesim->nuisance, NULL);

    /* Make sure that we got everything */
    cmb_event_queue_clear();
}

void *putterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);
    struct cmb_buffer *bp = (struct cmb_buffer *) ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(stdout, USERFLAG, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG, "Hold returned normally");
        }
        else {
            cmb_logger_user(stdout, USERFLAG, "Hold returned signal %lld", sig);
        }

        const uint64_t n = cmb_random_dice(1, 15);
        uint64_t m = n;
        cmb_logger_user(stdout,
                        USERFLAG,
                        "Putting %llu into %s...",
                        n,
                        cmb_buffer_get_name(bp));

        sig = cmb_buffer_put(bp, &m);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_assert_debug(m == 0u);
            cmb_logger_user(stdout, USERFLAG, "Put %llu succeeded", n);
        }
        else {
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Put returned signal %lld, got %llu instead of %llu",
                            sig,
                            m,
                            n);
        }
    }
}

void *getterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    struct cmb_buffer *bp = (struct cmb_buffer *) ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(stdout, USERFLAG, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG, "Hold returned normally");
        }
        else {
            cmb_logger_user(stdout, USERFLAG, "Hold returned signal %lld", sig);
        }

        const uint64_t n = cmb_random_dice(1, 15);
        cmb_logger_user(stdout,
                        USERFLAG,
                        "Getting %llu from %s...",
                        n,
                        cmb_buffer_get_name(bp));

        uint64_t m = n;
        sig = cmb_buffer_get(bp, &m);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_assert_debug(m == n);
            cmb_logger_user(stdout, USERFLAG, "Get %llu succeeded", n);
        }
        else {
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Get returned signal %lld, got %llu instead of %llu",
                            sig,
                            m,
                            n);
        }
    }
}

void *nuisancefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    /* Abuse internal knowledge of the content of the simulation struct */
    struct cmb_process **tgt = (struct cmb_process **)ctx;
    const long nproc = NUM_PUTTERS + NUM_GETTERS;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(stdout, USERFLAG, "Holding ...");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        const uint16_t vic = cmb_random_dice(0, nproc - 1);
        const int64_t sig = cmb_random_dice(1, 10);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_logger_user(stdout, USERFLAG, "Interrupting %s with %lld", tgt[vic]->name, sig);
        cmb_process_interrupt(tgt[vic], sig, pri);
    }
}

void test_queue(const double duration)
{
    struct simulation *thesim = cmi_malloc(sizeof(*thesim));
    cmi_memset(thesim, 0, sizeof(*thesim));

    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);
    printf("seed: %llx\n", seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG);
    cmb_event_queue_initialize(0.0);

    printf("Create a buffer\n");
    thesim->buf = cmb_buffer_create();
    cmb_buffer_initialize(thesim->buf, "Buf", 10u);
    cmb_buffer_start_recording(thesim->buf);

    char scratchpad[32];
    printf("Create three processes feeding into the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        thesim->putters[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Putter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(thesim->putters[ui], scratchpad, putterfunc, thesim->buf, pri);
        cmb_process_start(thesim->putters[ui]);
    }

    printf("Create three processes consuming from the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        thesim->getters[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Getter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(thesim->getters[ui], scratchpad, getterfunc, thesim->buf, pri);
        cmb_process_start(thesim->getters[ui]);
    }

    printf("Create a bloody nuisance\n");
    thesim->nuisance = cmb_process_create();
    cmb_process_initialize(thesim->nuisance, "Nuisance", nuisancefunc, thesim, 0);
    cmb_process_start(thesim->nuisance);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, thesim, NULL, duration, 0);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Report statistics...\n");
    cmb_buffer_stop_recording(thesim->buf);
    cmb_buffer_print_report(thesim->buf, stdout);

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        cmb_process_terminate(thesim->getters[ui]);
        cmb_process_destroy(thesim->putters[ui]);
    }

    cmb_process_terminate(thesim->nuisance);
    cmb_process_destroy(thesim->nuisance);
    cmb_buffer_destroy(thesim->buf);
    cmb_event_queue_terminate();
    cmi_free(thesim);
}

int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing buffers   *****************************\n");
    cmi_test_print_line("*");

    test_queue(100000);

    cmi_test_print_line("*");
    return 0;
}

