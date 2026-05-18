/*
 * Test script for resources.
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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_resource.h"

#include "test.h"

#define USERFLAG 0x00000001

static void end_sim_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_assert_always(object != NULL);

    struct cmb_process **cpp = subject;
    const uint64_t n = (uint64_t)object;
    cmb_logger_user(stdout, USERFLAG, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < n; ui++) {
        cmb_logger_user(stdout, USERFLAG, "Stopping process %s", cpp[ui]->name);
        const int64_t r = cmb_process_stop(cpp[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(cmb_process_status(cpp[ui]) == CMB_PROCESS_FINISHED);
    }
}

void *preemptable(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_always(ctx != NULL);
    struct cmb_resource *rp = ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        int64_t sig = cmb_resource_acquire(rp);
        if (sig == CMB_PROCESS_SUCCESS) {
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            cmb_assert_always((sig == CMB_PROCESS_SUCCESS) || (sig == CMB_PROCESS_PREEMPTED));
            if (sig == CMB_PROCESS_SUCCESS) {
                cmb_assert_always(cmb_resource_held_by_process(rp, me) == 1u);
                cmb_resource_release(rp);
                cmb_assert_always(cmb_resource_held_by_process(rp, me) == 0u);
            }
            else {
                cmb_assert_always(cmb_resource_held_by_process(rp, me) == 0u);
                cmb_logger_user(stdout, USERFLAG,
                                "Someone stole %s from me, signal %" PRIi64,
                                cmb_resource_name(rp),  sig);
            }
        }

        const double dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        sig = cmb_process_hold(dt);
        /* Not holding the resource; should not be preempted now */
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    }
}

void *preempter(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_always(ctx != NULL);
    struct cmb_resource *rp = ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_assert_always(cmb_resource_held_by_process(rp, me) == 0u);
        int64_t sig = cmb_resource_preempt(rp);
        cmb_assert_always(cmb_resource_held_by_process(rp, me) == 1u);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
        double dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        sig = cmb_process_hold(dt);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
        cmb_resource_release(rp);
        cmb_assert_always(cmb_resource_held_by_process(rp, me) == 0u);
        dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        sig = cmb_process_hold(dt);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    }
}

void test_resource(const uint64_t seed, const double dur)
{
    cmi_test_print_line("*");
    printf("****************************   Testing resources   *****************************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    cmb_random_initialize(seed);
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    printf("Create a resource\n");
    struct cmb_resource *rp = cmb_resource_create();
    cmb_assert_always(rp != NULL);
    cmb_resource_initialize(rp, "Resource_1");
    cmb_assert_always(cmb_resource_in_use(rp) == false);
    cmb_assert_always(cmb_resource_available(rp) == true);

    cmb_resource_start_recording(rp);

    printf("Create three processes to compete for the resource\n");
    struct cmb_process *cpp[4];
    for (unsigned ui = 0; ui < 3; ui++) {
        cpp[ui] = cmb_process_create();
        cmb_assert_always(cpp[ui] != NULL);
        char buf[32];
        snprintf(buf, sizeof(buf), "Target_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_assert_always((pri >= -5) && (pri <= 5));
        cmb_process_initialize(cpp[ui], buf, preemptable, rp, pri);
        cmb_assert_always(cmb_process_status(cpp[ui]) == CMB_PROCESS_CREATED);
        cmb_process_start(cpp[ui]);
        const uint64_t hld = cmb_resource_held_by_process(rp, cpp[ui]);
        cmb_assert_always(hld == 0u);
    }

    printf("Create a fourth process trying to preempt the resource\n");
    cpp[3] = cmb_process_create();
    cmb_process_initialize(cpp[3], "Preempter", preempter, rp, 0);
    cmb_process_start(cpp[3]);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, cpp, (void *)4u, dur, 0);

    printf("Execute simulation\n");
    cmi_test_print_line("-");
    cmb_event_queue_execute();
    cmi_test_print_line("-");

    printf("Report statistics...\n");
    cmb_resource_stop_recording(rp);
    cmb_resource_print_report(rp, stdout);

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 4; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_resource_destroy(rp);
    cmb_event_queue_terminate();
    cmb_random_terminate();

    cmi_test_print_line("*");
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 25.0;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:t")) != -1) {
        switch (opt) {
            case 'd':
                errno = 0;
                dur = strtof(optarg, NULL);
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

    test_resource(seed, dur);

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}