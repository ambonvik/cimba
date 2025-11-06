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

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_resource.h"

#include "cmi_test.h"
#include "cmi_memutils.h"

#define USERFLAG 0x00000001

static void end_sim_evt(void *subject, void *object)
{
    struct cmb_process **cpp = subject;
    const uint64_t n = (uint64_t)object;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < n; ui++) {
        cmb_process_stop(cpp[ui], NULL);
    }

    /* To be sure that we got everything */
    cmb_event_queue_clear();
}

void *procfunc1(struct cmb_process *me, void *ctx)
{
    cmi_unused(me);
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
                cmb_logger_user(USERFLAG, stdout,
                                "Someone stole %s from me, sig %lld!",
                                cmb_resource_get_name(rp), sig);
            }
            else {
                cmb_logger_user(USERFLAG, stdout,
                                "Interrupted by signal %lld!", sig);
            }
        }

        cmb_process_hold(cmb_random_exponential(1.0));
    }
}

void *procfunc2(struct cmb_process *me, void *ctx)
{
    cmi_unused(me);
    struct cmb_resource *rp = ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        const int64_t sig = cmb_resource_preempt(rp);
        cmb_logger_user(USERFLAG, stdout, "Preempt %s returned signal %lld",
                        cmb_resource_get_name(rp), sig);
        cmb_process_hold(cmb_random_exponential(1.0));
        cmb_resource_release(rp);
        cmb_process_hold(cmb_random_exponential(1.0));
    }
}

void test_resource(void)
{
    cmi_test_print_line("-");
    printf("Testing resources\n");
    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);

    printf("seed: %llu\n", seed);
    cmb_event_queue_initialize(0.0);

    printf("Create a resource\n");
    struct cmb_resource *rp = cmb_resource_create();
    cmb_resource_initialize(rp, "Resource_1");

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

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 4; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_resource_destroy(rp);
    cmb_event_queue_terminate();

    cmi_test_print_line("-");
}

void *procfunc3(struct cmb_process *me, void *ctx)
{
    cmi_unused(me);
    struct cmb_store *sp = ctx;
    uint64_t amount_held = 0u;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        const uint64_t amount_req = cmb_random_dice(1, 10);
        cmb_logger_user(USERFLAG, stdout, "Acquires %llu", amount_req);
        int64_t sig = cmb_store_acquire(sp, amount_req);
        cmb_logger_user(USERFLAG, stdout, "Acquire returned signal %lld", sig);
        if (sig == CMB_PROCESS_SUCCESS) {
            amount_held += amount_req;
            cmb_logger_user(USERFLAG, stdout, "Success, new amount held: %llu", amount_held);
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            cmb_logger_user(USERFLAG, stdout, "Hold returned signal %lld", sig);

            if (sig == CMB_PROCESS_SUCCESS) {
                uint64_t amount_rel = cmb_random_dice(1, 10);
                if (amount_rel > amount_held) {
                    amount_rel = amount_held;
                }
                cmb_logger_user(USERFLAG, stdout, "Holds %llu, releasing %llu", amount_held, amount_rel);
                cmb_store_release(sp, amount_rel);
                amount_held -= amount_rel;
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(USERFLAG, stdout,
                                "Someone stole all my %s from me!",
                                cmb_store_get_name(sp));
                amount_held = 0u;
            }
            else {
                cmb_logger_user(USERFLAG, stdout,
                                "Interrupted by signal %lld", sig);
            }
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(USERFLAG, stdout,
                            "Preempted during acquire, all my %s is gone",
                            cmb_store_get_name(sp));
            amount_held = 0u;
        }
        else {
            cmb_logger_user(USERFLAG, stdout,
                            "Interrupted by signal %lld", sig);
        }

        cmb_logger_user(USERFLAG, stdout, "Holding, amount held: %llu", amount_held);
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        cmb_logger_user(USERFLAG, stdout, "Hold returned signal %lld", sig);
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(USERFLAG, stdout,
                            "Someone stole the rest of my %s from me, sig %lld!",
                            cmb_store_get_name(sp), sig);
            amount_held = 0u;
        }
    }
}

void *procfunc4(struct cmb_process *me, void *ctx)
{
    cmi_unused(me);
    struct cmb_store *sp = ctx;
    uint64_t amount_held = 0u;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        const uint64_t amount_req = cmb_random_dice(1, 10);
        cmb_logger_user(USERFLAG, stdout, "Preempts %llu", amount_req);
        int64_t sig = cmb_store_preempt(sp, amount_req);
        cmb_logger_user(USERFLAG, stdout, "Preempt returned signal %lld", sig);

        if (sig == CMB_PROCESS_SUCCESS) {
            amount_held += amount_req;
            cmb_logger_user(USERFLAG, stdout, "Holding, amount held: %llu", amount_held);
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            cmb_logger_user(USERFLAG, stdout, "Hold returned signal %lld", sig);

            if (sig == CMB_PROCESS_SUCCESS) {
                uint64_t amount_rel = cmb_random_dice(1, 10);
                if (amount_rel > amount_held) {
                    amount_rel = amount_held;
                }

                cmb_logger_user(USERFLAG, stdout, "Holds %llu, releasing %llu", amount_held, amount_rel);
                cmb_store_release(sp, amount_rel);
                amount_held -= amount_rel;
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(USERFLAG, stdout,
                                "Someone stole my %s from me, sig %lld!",
                                cmb_store_get_name(sp), sig);
                amount_held = 0u;
            }
            else {
                cmb_logger_user(USERFLAG, stdout,
                                "Interrupted by signal %lld", sig);
            }
        }

        cmb_logger_user(USERFLAG, stdout, "Holding, amount held: %llu", amount_held);
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        cmb_logger_user(USERFLAG, stdout, "Hold returned signal %lld", sig);
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(USERFLAG, stdout,
                            "Someone stole the rest of my %s from me, sig %lld!",
                            cmb_store_get_name(sp), sig);
            amount_held = 0u;
        }
    }
}

void *procfunc5(struct cmb_process *me, void *ctx)
{
    cmi_unused(me);
    struct cmb_process **cpp = (struct cmb_process **) ctx;
    cmb_assert_release(cpp != NULL);

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Looking for rodents");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        struct cmb_process *tgt = cpp[cmb_random_dice(0, 3)];
        cmb_assert_debug(tgt != NULL);
        cmb_logger_user(USERFLAG, stdout, "Chasing %s", cmb_process_get_name(tgt));
        const int64_t sig = (cmb_random_flip()) ? CMB_PROCESS_INTERRUPTED : cmb_random_dice(10,100);
        cmb_process_interrupt(tgt, sig, 0);
    }
}

void test_store(void)
{
    cmi_test_print_line("-");
    printf("Testing stores\n");
    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);
    printf("seed: %llu\n", seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    printf("Create a store\n");
    struct cmb_store *sp = cmb_store_create();
    cmb_store_initialize(sp, "Cheese", 25u);

    struct cmb_process *cpp[5];
    printf("Create three small mice to compete for the cheese\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        cpp[ui] = cmb_process_create();
        char buf[32];
        snprintf(buf, sizeof(buf), "Mouse_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(cpp[ui], buf, procfunc3, sp, pri);
        cmb_process_start(cpp[ui]);
    }

    printf("Create a rat trying to preempt the cheese from the mice\n");
    cpp[3] = cmb_process_create();
    cmb_process_initialize(cpp[3], "Rat_1", procfunc4, sp, 0);
    cmb_process_start(cpp[3]);

    printf("Create a cat chasing all the rodents\n");
    cpp[4] = cmb_process_create();
    cmb_process_initialize(cpp[4], "Cat_1", procfunc5, cpp, 0);
    cmb_process_start(cpp[4]);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, cpp, (void *)5u, 100.0, 0);

    printf("Execute simulation\n");
    cmb_event_queue_execute();

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 4; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_store_destroy(sp);
    cmb_event_queue_terminate();
    cmi_test_print_line("-");
}

int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing resources   *****************************\n");
    cmi_test_print_line("*");

    // test_resource();
    test_store();

    cmi_test_print_line("*");
    return 0;
}