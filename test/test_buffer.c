/*
 * Test script for buffers
 * Usage:
 *      test_buffer [-s <seed>]
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
#include <time.h>
#include <unistd.h>

#include "cimba.h"
#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_buffer.h"

#include "cmi_memutils.h"
#include "test.h"

#define USERFLAG1 0x00000001
#define NUM_PUTTERS 3u
#define NUM_GETTERS 3u

struct simulation {
    struct cmb_process *putters[NUM_PUTTERS];
    struct cmb_process *getters[NUM_GETTERS];
    struct cmb_process *nuisance;
    struct cmb_buffer *buf;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_unused(object);

    const struct simulation *thesim = subject;
    cmb_logger_info(stdout, "===> end_sim: game over <===");

    for (unsigned ui = 0; ui < NUM_PUTTERS; ui++) {
        struct cmb_process *putter = thesim->putters[ui];
        cmb_assert_always(putter != NULL);
        cmb_assert_always(cmb_process_status(putter) == CMB_PROCESS_RUNNING);
        const int r = cmb_process_stop(putter, NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(cmb_process_status(putter) == CMB_PROCESS_FINISHED);
    }

    for (unsigned ui = 0; ui < NUM_GETTERS; ui++) {
        struct cmb_process *getter = thesim->getters[ui];
        cmb_assert_always(getter != NULL);
        cmb_assert_always(cmb_process_status(getter) == CMB_PROCESS_RUNNING);
        const int r = cmb_process_stop(getter, NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(cmb_process_status(getter) == CMB_PROCESS_FINISHED);
    }

    cmb_assert_always(cmb_process_status(thesim->nuisance) == CMB_PROCESS_RUNNING);
    const int r = cmb_process_stop(thesim->nuisance, NULL);
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    cmb_assert_always(cmb_process_status(thesim->nuisance) == CMB_PROCESS_FINISHED);
}

void *putterfunc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(cmb_process_status(me) == CMB_PROCESS_RUNNING);
    cmb_assert_always(vctx != NULL);
    struct cmb_buffer *bp = (struct cmb_buffer *) vctx;

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

        const uint64_t n = cmb_random_dice(1, 15);
        cmb_assert_always((n >= (uint64_t)1) && (n <= (uint64_t)15));
        uint64_t m = n;
        cmb_logger_user(stdout, USERFLAG1, "Putting %" PRIu64 " into %s...",
                        n, cmb_buffer_get_name(bp));

        sig = cmb_buffer_put(bp, &m);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_assert_always(m == 0u);
            /* Cannot assert much about the new level here, since multiple gets also may have happened */
            cmb_logger_user(stdout, USERFLAG1, "Put %" PRIu64 " success, level %" PRIu64, n, cmb_buffer_level(bp));
        }
        else {
            cmb_assert_always(m != 0u);
            cmb_logger_user(stdout, USERFLAG1,
                         "Put returned signal %" PRIi64 ", placed %" PRIu64 " instead of %" PRIu64 ", level %" PRIu64,
                         sig, m, n, cmb_buffer_level(bp));
        }
    }
}

void *getterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_always(cmb_process_status(me) == CMB_PROCESS_RUNNING);
    cmb_assert_always(ctx != NULL);

    struct cmb_buffer *bp = (struct cmb_buffer *) ctx;

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

        const uint64_t n = cmb_random_dice(1, 15);
        cmb_assert_always((n >= (uint64_t)1) && (n <= (uint64_t)15));
        cmb_logger_user(stdout, USERFLAG1, "Getting %" PRIu64 " from %s...",
                        n, cmb_buffer_get_name(bp));

        uint64_t m = n;
        sig = cmb_buffer_get(bp, &m);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_assert_always(m == n);
            cmb_logger_user(stdout, USERFLAG1, "Get %" PRIu64 " succeeded, level %" PRIu64, n, cmb_buffer_level(bp));
        }
        else {
            cmb_assert_always(m != n);
            cmb_logger_user(stdout, USERFLAG1,
                            "Get returned signal %" PRIi64 ", got %" PRIi64 " instead of %" PRIu64 ", level %" PRIu64,
                            sig, m, n, cmb_buffer_level(bp));
        }
    }
}

void *nuisancefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_always(cmb_process_status(me) == CMB_PROCESS_RUNNING);
    cmb_assert_always(ctx != NULL);

    /* Abuse knowledge of the content of the simulation struct
     * to handle the putters and getters as one contiguous array
     * of target processes. */
    struct cmb_process **tgt = (struct cmb_process **)ctx;
    const long nproc = NUM_PUTTERS + NUM_GETTERS;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(stdout, USERFLAG1, "Holding ...");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        const uint16_t vic = cmb_random_dice(0, nproc - 1);
        cmb_assert_always(vic < (uint16_t)nproc);
        const int64_t sig = cmb_random_dice(1, 10);
        cmb_assert_always(sig >= 1 && sig <= 10);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_assert_always(pri >= -5 && pri <= 5);
        cmb_logger_user(stdout, USERFLAG1, "Interrupting %s with %" PRIi64, tgt[vic]->name, sig);
        cmb_process_interrupt(tgt[vic], sig, pri);
    }
}

void test_buffer(const double duration, const uint64_t seed)
{
    struct simulation *thesim = cmi_malloc(sizeof(*thesim));
    cmb_assert_always(thesim != NULL);
    cmi_memset(thesim, 0, sizeof(*thesim));

    cmb_random_initialize(seed);
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);

    printf("Create a buffer\n");
    thesim->buf = cmb_buffer_create();
    cmb_assert_always(thesim->buf != NULL);
    cmb_buffer_initialize(thesim->buf, "Buf", 10u);
    cmb_assert_always(cmb_buffer_level(thesim->buf) == 0u);
    cmb_buffer_recording_start(thesim->buf);

    char scratchpad[32];
    printf("Create three processes feeding into the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        struct cmb_process *putter = cmb_process_create();
        cmb_assert_always(putter != NULL);
        cmb_assert_always(cmb_process_status(putter) == CMB_PROCESS_CREATED);
        snprintf(scratchpad, sizeof(scratchpad), "Putter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(putter,
                               scratchpad,
                               putterfunc,
                               thesim->buf,
                               pri);
        cmb_assert_always(cmb_process_status(putter) == CMB_PROCESS_CREATED);
        /* Non-blocking call, just schedules the start event until we yield */
        cmb_process_start(putter);
        cmb_assert_always(cmb_process_status(putter) == CMB_PROCESS_CREATED);
        thesim->putters[ui] = putter;
    }

    printf("Create three processes consuming from the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        struct cmb_process *getter = cmb_process_create();
        cmb_assert_always(getter != NULL);
        cmb_assert_always(cmb_process_status(getter) == CMB_PROCESS_CREATED);
        snprintf(scratchpad, sizeof(scratchpad), "Getter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(getter,
                               scratchpad,
                               getterfunc,
                               thesim->buf,
                               pri);
        cmb_assert_always(cmb_process_status(getter) == CMB_PROCESS_CREATED);
        cmb_process_start(getter);
        cmb_assert_always(cmb_process_status(getter) == CMB_PROCESS_CREATED);
        thesim->getters[ui] = getter;
    }

    printf("Create a nuisance interrupting others at random times\n");
    thesim->nuisance = cmb_process_create();
    cmb_assert_always(thesim->nuisance != NULL);
    cmb_assert_always(cmb_process_status(thesim->nuisance) == CMB_PROCESS_CREATED);
    cmb_process_initialize(thesim->nuisance, "Nuisance", nuisancefunc, thesim, 0);
    cmb_assert_always(cmb_process_status(thesim->nuisance) == CMB_PROCESS_CREATED);
    cmb_process_start(thesim->nuisance);
    cmb_assert_always(cmb_process_status(thesim->nuisance) == CMB_PROCESS_CREATED);

    printf("Schedule end event\n");
    const uint64_t end_evt_hdle = cmb_event_schedule(end_sim_evt, thesim, NULL, duration, 0);
    cmb_assert_always(end_evt_hdle != 0u);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Report statistics...\n");
    cmb_buffer_recording_stop(thesim->buf);
    cmb_buffer_print_report(thesim->buf, stdout);

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        cmb_assert_always(cmb_process_status(thesim->putters[ui]) == CMB_PROCESS_FINISHED);
        cmb_process_terminate(thesim->putters[ui]);
        cmb_process_destroy(thesim->putters[ui]);
        cmb_assert_always(cmb_process_status(thesim->getters[ui]) == CMB_PROCESS_FINISHED);
        cmb_process_terminate(thesim->getters[ui]);
        cmb_process_destroy(thesim->getters[ui]);
    }

    cmb_assert_always(cmb_process_status(thesim->nuisance) == CMB_PROCESS_FINISHED);
    cmb_process_terminate(thesim->nuisance);
    cmb_process_destroy(thesim->nuisance);
    cmb_buffer_destroy(thesim->buf);
    cmb_event_queue_terminate();
    cmb_random_terminate();
    cmi_free(thesim);
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 10000.0;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:t")) != -1) {
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
                seed = (uint64_t)strtoul(optarg, NULL, 0);
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

    cmi_test_print_line("*");
    printf("*****************************   Testing buffers   ******************************\n");
    cmi_test_print_line("*");
    printf("Cimba version %s\n", cimba_version());
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    test_buffer(dur, seed);

    cmi_test_print_line("*");

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    if (timing_enabled) {
        printf("\nIt took %g sec\n", elapsed_time);
    }
    return 0;
}

