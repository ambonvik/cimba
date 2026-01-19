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

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_resource.h"

#include "test.h"

#define USERFLAG1 0x00000001

static void end_sim_evt(void *subject, void *object)
{
    struct cmb_process **cpp = subject;
    const uint64_t n = (uint64_t)object;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < n; ui++) {
        cmb_logger_info(stdout, "Stopping process %s", cpp[ui]->name);
        cmb_process_stop(cpp[ui], NULL);
    }
}

void *procfunc1(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    struct cmb_resource *rp = ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        int64_t sig = cmb_resource_acquire(rp);
        if (sig == CMB_PROCESS_SUCCESS) {
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            if (sig == CMB_PROCESS_SUCCESS) {
                cmb_resource_release(rp);
            }
            else if (sig == CMB_PROCESS_PREEMPTED){
                cmb_logger_user(stdout, USERFLAG1,
                                "Someone stole %s from me, signal %" PRIi64,
                                cmb_resource_name(rp),  sig);
            }
            else {
                cmb_logger_user(stdout, USERFLAG1,
                                "Interrupted by signal %" PRIi64,
                                sig);
            }
        }

        cmb_process_hold(cmb_random_exponential(1.0));
    }
}

void *procfunc2(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    struct cmb_resource *rp = ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        const int64_t sig = cmb_resource_preempt(rp);
        cmb_logger_user(stdout, USERFLAG1,
                        "Preempt %s returned signal %" PRIi64,
                        cmb_resource_name(rp), sig);
        cmb_process_hold(cmb_random_exponential(1.0));
        cmb_resource_release(rp);
        cmb_process_hold(cmb_random_exponential(1.0));
    }
}

void test_resource(void)
{
    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);

    printf("seed: %" PRIu64 "\n", seed);
    cmb_event_queue_initialize(0.0);

    printf("Create a resource\n");
    struct cmb_resource *rp = cmb_resource_create();
    cmb_resource_initialize(rp, "Resource_1");
    cmb_resource_start_recording(rp);

    printf("Create three processes to compete for the resource\n");
    struct cmb_process *cpp[4];
    for (unsigned ui = 0; ui < 3; ui++) {
        cpp[ui] = cmb_process_create();
        char buf[32];
        snprintf(buf, sizeof(buf), "Process_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(cpp[ui], buf, procfunc1, rp, pri);
        cmb_process_start(cpp[ui]);
    }

    printf("Create a fourth process trying to preempt the resource\n");
    cpp[3] = cmb_process_create();
    cmb_process_initialize(cpp[3], "Process_4", procfunc2, rp, 0);
    cmb_process_start(cpp[3]);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, cpp, (void *)4u, 25.0, 0);

    printf("Execute simulation\n");
    cmb_event_queue_execute();

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
}

int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing resources   *****************************\n");
    cmi_test_print_line("*");

    test_resource();

    cmi_test_print_line("*");
    return 0;
}