/*
* Test script for resource store (i.e. counting semaphore).
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
#include "cmb_resourcestore.h"

#include "cmi_memutils.h"
#include "test.h"

#define USERFLAG1 0x00000001

#define NUM_MICE 3u
#define NUM_RATS 2u
#define NUM_CATS 1u

struct simulation {
    struct cmb_process *mice[NUM_MICE];
    struct cmb_process *rats[NUM_RATS];
    struct cmb_process *cats[NUM_CATS];
    struct cmb_resourcestore *cheese;
};


static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(subject);
    cmb_assert_release(object != NULL);

    struct simulation *tstexp = object;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_MICE; ui++) {
        cmb_process_stop(tstexp->mice[ui], NULL);
    }
    for (unsigned ui = 0; ui < NUM_RATS; ui++) {
        cmb_process_stop(tstexp->rats[ui], NULL);
    }
    for (unsigned ui = 0; ui < NUM_CATS; ui++) {
        cmb_process_stop(tstexp->cats[ui], NULL);
    }

    /* To be sure that we got everything */
    cmb_event_queue_clear();
}

void *mousefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    const struct simulation *tstexp = ctx;
    struct cmb_resourcestore *sp = tstexp->cheese;
    uint64_t amount_held = 0u;

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        cmb_logger_user(stdout, USERFLAG1,
                         "Own calc amount %" PRIu64 ", library calc %" PRIu64,
                         amount_held, cmb_resourcestore_held_by_process(sp, me));
        cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        const uint64_t amount_req = cmb_random_dice(1, 10);
        (void)cmb_process_set_priority(me, cmb_random_dice(-10, 10));
        cmb_logger_user(stdout, USERFLAG1, "Acquiring %" PRIu64, amount_req);
        int64_t sig = cmb_resourcestore_acquire(sp, amount_req);
        cmb_logger_user(stdout, USERFLAG1, "Acquire returned signal %" PRIi64, sig);
        if (sig == CMB_PROCESS_SUCCESS) {
            amount_held += amount_req;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
            cmb_logger_user(stdout,
                            USERFLAG1,
                            "Success, new amount held: %" PRIu64,
                            amount_held);
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);

            if (sig == CMB_PROCESS_SUCCESS) {
                uint64_t amount_rel = cmb_random_dice(1, 10);
                if (amount_rel > amount_held) {
                    amount_rel = amount_held;
                }
                cmb_logger_user(stdout,
                                USERFLAG1,
                                "Holds %" PRIu64 ", releasing %" PRIu64,
                                amount_held,
                                amount_rel);
                cmb_resourcestore_release(sp, amount_rel);
                amount_held -= amount_rel;
                cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(stdout,
                                USERFLAG1,
                                "Someone stole all my %s from me!",
                                cmb_resourcestore_get_name(sp));
                amount_held = 0u;
                cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
            }
            else {
                cmb_logger_user(stdout,
                                USERFLAG1,
                                "Interrupted by signal %" PRIi64,
                                sig);
            }
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout,
                            USERFLAG1,
                            "Preempted during acquire, all my %s is gone",
                            cmb_resourcestore_get_name(sp));
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else {
            cmb_logger_user(stdout,
                            USERFLAG1,
                            "Interrupted by signal %" PRIi64,
                            sig);
        }

        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Holding, amount held: %" PRIu64,
                        amount_held);

        sig = cmb_process_hold(cmb_random_exponential(1.0));
        cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout,
                            USERFLAG1,
                            "Someone stole the rest of my %s, signal %" PRIi64,
                            cmb_resourcestore_get_name(sp), sig);

            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
    }
}

void *ratfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    const struct simulation *tstexp = ctx;
    struct cmb_resourcestore *sp = tstexp->cheese;
    uint64_t amount_held = 0u;

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Own calc amount %" PRIu64 ", library calc %" PRIu64,
                        amount_held, cmb_resourcestore_held_by_process(sp, me));
        cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        const uint64_t amount_req = cmb_random_dice(1, 10);
        cmb_logger_user(stdout, USERFLAG1, "Preempting %" PRIu64, amount_req);
        int64_t sig = cmb_resourcestore_preempt(sp, amount_req);
        cmb_logger_user(stdout, USERFLAG1, "Preempt returned signal %" PRIi64, sig);

        if (sig == CMB_PROCESS_SUCCESS) {
            amount_held += amount_req;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
            cmb_logger_user(stdout,
                            USERFLAG1,
                            "Holding, amount held: %" PRIu64,
                            amount_held);
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);

            if (sig == CMB_PROCESS_SUCCESS) {
                uint64_t amount_rel = cmb_random_dice(1, 10);
                if (amount_rel > amount_held) {
                    amount_rel = amount_held;
                }

                cmb_logger_user(stdout,
                                USERFLAG1,
                                "Holds %" PRIu64 ", releasing %" PRIu64,
                                amount_held,
                                amount_rel);
                cmb_resourcestore_release(sp, amount_rel);
                amount_held -= amount_rel;
                cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(stdout, USERFLAG1,
                                "Someone stole my %s from me, signal %" PRIi64,
                                cmb_resourcestore_get_name(sp), sig);
                amount_held = 0u;
                cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
            }
            else {
                cmb_logger_user(stdout, USERFLAG1,
                                "Interrupted by signal %" PRIi64, sig);
            }
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Preempted during own preempt, all my %s is gone",
                            cmb_resourcestore_get_name(sp));
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else {
            cmb_logger_user(stdout, USERFLAG1,
                            "Interrupted by signal %" PRIi64, sig);
        }

        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Holding, amount held: %" PRIu64,
                        amount_held);
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        cmb_logger_user(stdout, USERFLAG1, "Hold returned signal %" PRIi64, sig);
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout,
                            USERFLAG1,
                            "Someone stole the rest of my %s, signal %" PRIi64,
                            cmb_resourcestore_get_name(sp),
                            sig);
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
    }
}

void *catfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    struct simulation *tstexp = ctx;
    struct cmb_process **cpp = (struct cmb_process **) tstexp;
    const long num = NUM_MICE + NUM_RATS;

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        cmb_logger_user(stdout, USERFLAG1, "Looking for rodents");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        struct cmb_process *tgt = cpp[cmb_random_dice(0, num - 1)];
        cmb_assert_debug(tgt != NULL);
        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Chasing %s",
                        cmb_process_get_name(tgt));

        const int64_t sig = (cmb_random_flip()) ?
                             CMB_PROCESS_INTERRUPTED :
                             cmb_random_dice(10,100);
        cmb_process_interrupt(tgt, sig, 0);
    }
}

void test_store(void)
{
    struct simulation *storetest = cmi_malloc(sizeof(*storetest));
    cmi_memset(storetest, 0, sizeof(*storetest));

    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);
    printf("seed: 0x%" PRIx64 "\n", seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    printf("Create a store\n");
    storetest->cheese = cmb_resourcestore_create();
    cmb_resourcestore_initialize(storetest->cheese, "Cheese", 20u);
    cmb_resourcestore_start_recording(storetest->cheese);

    char scratchpad[32];
    printf("Create three small mice to compete for the cheese\n");
    for (unsigned ui = 0; ui < NUM_MICE; ui++) {
        storetest->mice[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Mouse_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(storetest->mice[ui],
                               scratchpad,
                               mousefunc,
                               storetest,
                               pri);
        cmb_process_start(storetest->mice[ui]);
    }

    printf("Create a pair of rats trying to preempt the cheese\n");
    for (unsigned ui = 0; ui < NUM_RATS; ui++) {
        storetest->rats[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Rat_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(storetest->rats[ui],
                               scratchpad,
                               ratfunc,
                               storetest,
                               pri);
        cmb_process_start(storetest->rats[ui]);
    }

    printf("Create a cat chasing all the rodents\n");
    for (unsigned ui = 0; ui < NUM_CATS; ui++) {
        storetest->cats[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Cat_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(storetest->cats[ui],
                               scratchpad,
                               catfunc,
                               storetest,
                               pri);
        cmb_process_start(storetest->cats[ui]);
    }

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, NULL, storetest, 100.0, 0);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Report statistics...\n");
    cmb_resourcestore_stop_recording(storetest->cheese);
    cmb_resourcestore_print_report(storetest->cheese, stdout);

    printf("Clean up\n");
    struct cmb_process **cpp = (struct cmb_process **) storetest;
    for (unsigned ui = 0; ui < NUM_MICE + NUM_RATS + NUM_CATS; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_resourcestore_destroy(storetest->cheese);
    cmb_event_queue_terminate();
    free(storetest);
}


int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing stores   *****************************\n");
    cmi_test_print_line("*");

    test_store();

    cmi_test_print_line("*");
    return 0;
}
