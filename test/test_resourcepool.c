/*
* Test script for resource pool (i.e. counting semaphore).
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
#include "cmb_resourcepool.h"

#include "cmi_memutils.h"
#include "test.h"

#define USERFLAG 0x0000000

#define NUM_MICE 3u
#define NUM_RATS 2u
#define NUM_CATS 1u

struct simulation {
    struct cmb_process *mice[NUM_MICE];
    struct cmb_process *rats[NUM_RATS];
    struct cmb_process *cats[NUM_CATS];
    struct cmb_resourcepool *cheese;
};


static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(subject);
    cmb_assert_release(object != NULL);

    struct simulation *tstexp = object;
    cmb_logger_user(stdout, USERFLAG, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_MICE; ui++) {
        const int64_t r = cmb_process_stop(tstexp->mice[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    }

    for (unsigned ui = 0; ui < NUM_RATS; ui++) {
        const int64_t r = cmb_process_stop(tstexp->rats[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    }

    for (unsigned ui = 0; ui < NUM_CATS; ui++) {
        const int64_t r = cmb_process_stop(tstexp->cats[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    }
}

void *mousefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    const struct simulation *tstexp = ctx;
    struct cmb_resourcepool *sp = tstexp->cheese;
    uint64_t amount_held = 0u;

    while (true) {
        cmb_logger_user(stdout, USERFLAG,
                         "Own calc amount %" PRIu64 ", library calc %" PRIu64,
                         amount_held, cmb_resourcepool_held_by_process(sp, me));
        cmb_assert_debug(cmb_resourcepool_held_by_process(sp, me) == amount_held);
        const uint64_t amount_req = cmb_random_dice(1, 10);
        cmb_assert_always((amount_req >= 1) && (amount_req <= 10));
        const int64_t pri = cmb_random_dice(-10, 10);
        cmb_assert_always((pri >= -10) && (pri <= 10));
        cmb_process_priority_set(me, pri);
        cmb_logger_user(stdout, USERFLAG, "Acquiring %" PRIu64, amount_req);
        int64_t sig = cmb_resourcepool_acquire(sp, amount_req);
        cmb_logger_user(stdout, USERFLAG, "Acquire returned signal %" PRIi64, sig);
        if (sig == CMB_PROCESS_SUCCESS) {
            /* Acquire succeeded, got all we wanted */
            amount_held += amount_req;
            cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Success, new amount held: %" PRIu64,
                            amount_held);
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            cmb_logger_user(stdout, USERFLAG, "Hold returned signal %" PRIi64, sig);

            if (sig == CMB_PROCESS_SUCCESS) {
                uint64_t amount_rel = cmb_random_dice(1, 10);
                cmb_assert_always((amount_rel >= 1) && (amount_rel <= 10));
                if (amount_rel > amount_held) {
                    amount_rel = amount_held;
                }
                cmb_logger_user(stdout,
                                USERFLAG,
                                "Holds %" PRIu64 ", releasing %" PRIu64,
                                amount_held,
                                amount_rel);
                cmb_resourcepool_release(sp, amount_rel);
                amount_held -= amount_rel;
                cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(stdout,
                                USERFLAG,
                                "Someone stole all my %s from me!",
                                cmb_resourcepool_get_name(sp));
                amount_held = 0u;
                cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);
            }
            else {
                cmb_logger_user(stdout,
                                USERFLAG,
                                "Interrupted by signal %" PRIi64,
                                sig);
            }
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            /* Acquire preempted, we lost everything */
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Preempted during acquire, all my %s is gone",
                            cmb_resourcepool_get_name(sp));
            amount_held = 0u;
            cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);
        }
        else {
            /* Acquire interrupted */
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Interrupted by signal %" PRIi64,
                            sig);
        }

        cmb_logger_user(stdout,
                        USERFLAG,
                        "Holding, amount held: %" PRIu64,
                        amount_held);
        cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);

        const double dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        sig = cmb_process_hold(dt);
        cmb_logger_user(stdout, USERFLAG, "Hold returned signal %" PRIi64, sig);
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Someone stole the rest of my %s, signal %" PRIi64,
                            cmb_resourcepool_get_name(sp), sig);

            amount_held = 0u;
            cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);
        }
    }
}

void *ratfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    const struct simulation *tstexp = ctx;
    struct cmb_resourcepool *sp = tstexp->cheese;
    uint64_t amount_held = 0u;

    while (true) {
        cmb_logger_user(stdout, USERFLAG,
                        "Own calc amount %" PRIu64 ", library calc %" PRIu64,
                        amount_held, cmb_resourcepool_held_by_process(sp, me));
        uint64_t calc_held = cmb_resourcepool_held_by_process(sp, me);
        cmb_logger_user(stdout, USERFLAG, "Reported %" PRIu64 " own calc %" PRIu64,
                        calc_held, amount_held);
        cmb_assert_always(calc_held == amount_held);

        const uint64_t amount_req = cmb_random_dice(1, 10);
        cmb_assert_always((amount_req >= 1) && (amount_req <= 10));
        cmb_logger_user(stdout, USERFLAG, "Preempting %" PRIu64, amount_req);
        int64_t sig = cmb_resourcepool_preempt(sp, amount_req);
        cmb_logger_user(stdout, USERFLAG, "Preempt returned signal %" PRIi64, sig);

        if (sig == CMB_PROCESS_SUCCESS) {
            amount_held += amount_req;
            calc_held = cmb_resourcepool_held_by_process(sp, me);
            cmb_logger_user(stdout, USERFLAG, "Own calc amount %" PRIu64 " library calc %" PRIu64,
                            amount_held, calc_held);
            cmb_assert_always(calc_held == amount_held);
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Holding, amount held: %" PRIu64,
                            amount_held);
            const double dt = cmb_random_exponential(1.0);
            cmb_assert_always(dt >= 0.0);;
            sig = cmb_process_hold(dt);
            cmb_logger_user(stdout, USERFLAG, "Hold returned signal %" PRIi64, sig);

            if (sig == CMB_PROCESS_SUCCESS) {
                uint64_t amount_rel = cmb_random_dice(1, 10);
                if (amount_rel > amount_held) {
                    amount_rel = amount_held;
                }

                cmb_logger_user(stdout,
                                USERFLAG,
                                "Holds %" PRIu64 ", releasing %" PRIu64,
                                amount_held,
                                amount_rel);
                cmb_resourcepool_release(sp, amount_rel);
                amount_held -= amount_rel;
                cmb_assert_always(cmb_resourcepool_held_by_process(sp, me) == amount_held);
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(stdout, USERFLAG,
                                "Someone stole my %s from me, signal %" PRIi64,
                                cmb_resourcepool_get_name(sp), sig);
                amount_held = 0u;
                cmb_assert_debug(amount_held == cmb_resourcepool_held_by_process(sp, me));
            }
            else {
                cmb_logger_user(stdout, USERFLAG,
                                "Interrupted by signal %" PRIi64, sig);
            }
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout, USERFLAG,
                            "Preempted during own preempt, all my %s is gone",
                            cmb_resourcepool_get_name(sp));
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcepool_held_by_process(sp, me));
        }
        else {
            cmb_logger_user(stdout, USERFLAG,
                            "Interrupted by signal %" PRIi64, sig);
        }

        cmb_logger_user(stdout,
                        USERFLAG,
                        "Holding, amount held: %" PRIu64,
                        amount_held);

        const double dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        sig = cmb_process_hold(dt);
        cmb_logger_user(stdout, USERFLAG, "Hold returned signal %" PRIi64, sig);
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout,
                            USERFLAG,
                            "Someone stole the rest of my %s, signal %" PRIi64,
                            cmb_resourcepool_get_name(sp),
                            sig);
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcepool_held_by_process(sp, me));
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

    while (true) {
        cmb_logger_user(stdout, USERFLAG, "Looking for rodents");
        const double dt = cmb_random_exponential(1.0);
        cmb_assert_always(dt >= 0.0);
        int64_t sig = cmb_process_hold(dt);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
        const unsigned tgt_ind = cmb_random_dice(0, num - 1);
        cmb_assert_always(tgt_ind < num);
        struct cmb_process *tgt = cpp[tgt_ind];
        cmb_assert_always(tgt != NULL);
        cmb_logger_user(stdout,
                        USERFLAG,
                        "Chasing %s",
                        cmb_process_name(tgt));

        const int64_t rsig = cmb_random_dice(10,100);
        cmb_assert_always((rsig >= 10.0) && (rsig <= 100.0));
        sig = (cmb_random_flip()) ? CMB_PROCESS_INTERRUPTED : rsig;
        cmb_process_interrupt(tgt, sig, 0);
    }
}

void test_pool(const uint64_t seed, const double dur)
{
    cmi_test_print_line("*");
    printf("****************************   Testing pools   *****************************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);
    struct simulation *pooltest = cmi_malloc(sizeof(*pooltest));
    cmb_assert_always(pooltest != NULL);
    cmi_memset(pooltest, 0, sizeof(*pooltest));

    cmb_random_initialize(seed);
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    printf("Create a pool\n");
    pooltest->cheese = cmb_resourcepool_create();
    cmb_assert_always(pooltest->cheese != NULL);
    cmb_resourcepool_initialize(pooltest->cheese, "Cheese", 20u);
    cmb_resourcepool_start_recording(pooltest->cheese);

    char scratchpad[32];
    printf("Create three small mice to compete for the cheese\n");
    for (unsigned ui = 0; ui < NUM_MICE; ui++) {
        pooltest->mice[ui] = cmb_process_create();
        cmb_assert_always(pooltest->mice[ui] != NULL);
        snprintf(scratchpad, sizeof(scratchpad), "Mouse_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_assert_always((pri >= -5) && (pri <= 5));
        cmb_process_initialize(pooltest->mice[ui],
                               scratchpad,
                               mousefunc,
                               pooltest,
                               pri);
        cmb_process_start(pooltest->mice[ui]);
    }

    printf("Create a pair of rats trying to preempt the cheese\n");
    for (unsigned ui = 0; ui < NUM_RATS; ui++) {
        pooltest->rats[ui] = cmb_process_create();
        cmb_assert_always(pooltest->rats[ui] != NULL);
        snprintf(scratchpad, sizeof(scratchpad), "Rat_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_assert_always((pri >= -5) && (pri <= 5));
        cmb_process_initialize(pooltest->rats[ui],
                               scratchpad,
                               ratfunc,
                               pooltest,
                               pri);
        cmb_process_start(pooltest->rats[ui]);
    }

    printf("Create a cat chasing all the rodents\n");
    for (unsigned ui = 0; ui < NUM_CATS; ui++) {
        pooltest->cats[ui] = cmb_process_create();
        cmb_assert_always(pooltest->cats[ui] != NULL);
        snprintf(scratchpad, sizeof(scratchpad), "Cat_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_assert_always((pri >= -5) && (pri <= 5));
        cmb_process_initialize(pooltest->cats[ui],
                               scratchpad,
                               catfunc,
                               pooltest,
                               pri);
        cmb_process_start(pooltest->cats[ui]);
    }

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, NULL, pooltest, dur, 0);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Report statistics...\n");
    cmb_resourcepool_stop_recording(pooltest->cheese);
    cmb_resourcepool_print_report(pooltest->cheese, stdout);

    printf("Clean up\n");
    struct cmb_process **cpp = (struct cmb_process **) pooltest;
    for (unsigned ui = 0; ui < NUM_MICE + NUM_RATS + NUM_CATS; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_resourcepool_destroy(pooltest->cheese);
    cmb_event_queue_terminate();
    cmb_random_terminate();
    free(pooltest);

    cmi_test_print_line("*");
}


int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 100.0;

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

    test_pool(seed, dur);

     if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}
