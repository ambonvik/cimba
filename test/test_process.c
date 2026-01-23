/*
 * Test script for processes.
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

#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_random.h"

#include "test.h"

#define USERFLAG1 0x00000001

uint64_t cuckoo_clock_handle = 0u;

void cuckooevtfunc(void *sub, void *obj)
{
    cmb_unused(sub);
    cmb_unused(obj);

    cmb_logger_user(stdout,USERFLAG1, "Cuckoo event occurred");
}

void cnclevtfunc(void *sub, void *obj)
{
    cmb_unused(sub);
    cmb_unused(obj);

    cmb_assert_release(cuckoo_clock_handle != 0u);
     if (cmb_event_is_scheduled(cuckoo_clock_handle)) {
        cmb_logger_user(stdout, USERFLAG1, "Cancelling cuckoo event");
        cmb_event_cancel(cuckoo_clock_handle);
     }
     else {
        cmb_logger_user(stdout, USERFLAG1, "Cuckoo event already cancelled");
     }
}

void *procfunc1(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_unused(ctx);

    cmb_logger_user(stdout, USERFLAG1, "Running");
    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        const double dur = cmb_random_exponential(5.0);
        const int64_t sig = cmb_process_hold(dur);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Hold returned normal signal %" PRIi64, sig);
        }
        else {
            cmb_logger_user(stdout, USERFLAG1,
                            "Hold was interrupted signal %" PRIi64, sig);
        }
    }
}

void *procfunc2(struct cmb_process *me, void *ctx)
{
    struct cmb_process *tgt = (struct cmb_process *)ctx;
    cmb_logger_user(stdout, USERFLAG1, "Running, tgt %s", cmb_process_name(tgt));
    const int64_t pri = cmb_process_priority(me);
    for (unsigned ui = 0u; ui < 5u; ui++) {
        const double dur = cmb_random_exponential(10.0);
        (void)cmb_process_hold(dur);
        cmb_process_interrupt(tgt, CMB_PROCESS_INTERRUPTED, pri);
    }

    const double dur = cmb_random_exponential(10.0);
    (void)cmb_process_hold(dur);
    cmb_process_stop(tgt, (void *)0xABBA);

    cmb_process_exit((void *)0x5EAF00D);

    /* not reached */
    return NULL;
}

void *procfunc3(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);

    struct cmb_process *tgt = (struct cmb_process *)ctx;
    cmb_logger_user(stdout, USERFLAG1, "Running, tgt %s", cmb_process_name(tgt));
    int64_t r = cmb_process_wait_event(cuckoo_clock_handle);
    cmb_logger_user(stdout, USERFLAG1, "Got cuckoo clock signal %" PRIi64, r);

    cmb_process_hold(cmb_random());
    cmb_logger_user(stdout, USERFLAG1,
                    "Waiting for process %s", cmb_process_name(tgt));
    r = cmb_process_wait_process(tgt);
    cmb_logger_user(stdout, USERFLAG1,
                    "Tgt %s ended, we received signal %" PRIi64,
                    cmb_process_name(tgt), r);

    cmb_process_exit(NULL);

    /* not reached */
    return NULL;
}

int main(void)
{
    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("****************************   Testing processes   *****************************\n");
    cmi_test_print_line("*");
    printf("seed: 0x%" PRIx64 "\n", seed);
    printf("cmb_event_queue_initialize ...\n");
    cmb_event_queue_initialize(0.0);
    printf("cmb_process_create ...\n");
    struct cmb_process *cpp1 = cmb_process_create();
    struct cmb_process *cpp2 = cmb_process_create();
    printf("cmb_process_initialize ...\n");
    cmb_process_initialize(cpp1, "Testproc", procfunc1, NULL, 0);
    cmb_process_initialize(cpp2,"Nuisance", procfunc2, cpp1, 1);

    printf("cmb_process_start ...\n");
    cmb_process_start(cpp1);
    cmb_process_start(cpp2);

    printf("Creating an event and a race condition to cancel it...\n");
    cuckoo_clock_handle = cmb_event_schedule(cuckooevtfunc, NULL, NULL,
                                             cmb_random_exponential(25.0), 0);
    cmb_event_schedule(cnclevtfunc, NULL, NULL, cmb_random_exponential(25.0), 0);

    printf("Creating waiting processes ...\n");
    char buf[32];
    struct cmb_process *cpp3 = NULL;
    for (unsigned ui = 0u; ui < 3u; ui++) {
        sprintf(buf, "Waiter_%u", ui);
        cpp3 = cmb_process_create();
        cmb_process_initialize(cpp3, buf, procfunc3, cpp2, cmb_random_dice(-5, 5));
        cmb_process_start(cpp3);
    }

    cmi_test_print_line("-");
    cmb_event_queue_print(stdout);
    cmi_test_print_line("-");

    printf("cmb_event_queue_execute ...\n");
    cmb_event_queue_execute();

    printf("%s returned %p\n",
           cmb_process_name(cpp1),
           cmb_process_exit_value(cpp1));
    printf("%s returned %p\n",
           cmb_process_name(cpp2),
           cmb_process_exit_value(cpp2));

    printf("cmb_process_destroy ...\n");
    cmb_process_destroy(cpp1);
    cmb_process_destroy(cpp2);

    printf("cmb_event_queue_terminate ...\n");
    cmb_event_queue_terminate();
    cmi_test_print_line("*");
    return 0;
}