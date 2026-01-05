/*
 * tut_3_1.c
 *
 * Demonstrating interrupt and preempt process interactions
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

#include <cimba.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#define USERFLAG1 0x00000001

#define NUM_MICE 5u
#define NUM_RATS 2u
#define NUM_CATS 1u

#define CHEESE_AMOUNT 20u

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

    const struct simulation *simp = object;
    cmb_logger_user(stdout, USERFLAG1, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_MICE; ui++) {
        cmb_process_stop(simp->mice[ui], NULL);
    }
    for (unsigned ui = 0; ui < NUM_RATS; ui++) {
        cmb_process_stop(simp->rats[ui], NULL);
    }
    for (unsigned ui = 0; ui < NUM_CATS; ui++) {
        cmb_process_stop(simp->cats[ui], NULL);
    }
}

/* The busy life of a mouse */
void *mousefunc(struct cmb_process *me, void *ctx)
{
    cmb_assert_release(me != NULL);
    cmb_assert_release(ctx != NULL);

    const struct simulation *simp = ctx;
    struct cmb_resourcestore *sp = simp->cheese;
    uint64_t amount_held = 0u;

    while (true) {
        /* Verify that the amount matches our own calculation */
        cmb_logger_user(stdout, USERFLAG1, "Amount held: %" PRIu64, amount_held);
        cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));

        /* Decide on a random amount to get next time and set a random priority */
        const uint64_t amount_req = cmb_random_dice(1, 5);
        const int64_t pri = cmb_random_dice(-10, 10);
        cmb_process_set_priority(me, pri);
        cmb_logger_user(stdout, USERFLAG1, "Acquiring %" PRIu64, amount_req);
        int64_t sig = cmb_resourcestore_acquire(sp, amount_req);
        if (sig == CMB_PROCESS_SUCCESS) {
            /* Acquire returned successfully */
            amount_held += amount_req;
            cmb_logger_user(stdout, USERFLAG1, "Success, new amount held: %" PRIu64, amount_held);
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            /* The acquire() call did not end well */
            amount_held = 0u;
            cmb_logger_user(stdout, USERFLAG1, "Preempted during acquire, all my %s is gone",
                            cmb_resourcestore_get_name(sp));
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else {
            /* Interrupted, but we still have the same amount as before */
            cmb_logger_user(stdout, USERFLAG1, "Interrupted by signal %" PRIi64, sig);
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }

        /* Hold on to it for a while */
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            /* We still have it */
            cmb_logger_user(stdout, USERFLAG1, "Hold returned normally");
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
       }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            /* Somebody snatched it all away from us */
            amount_held = 0u;
            cmb_logger_user(stdout, USERFLAG1, "Someone stole all my %s from me!",
                            cmb_resourcestore_get_name(sp));
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else {
            /* Interrupted while holding. Still have the cheese, though */
            cmb_logger_user(stdout, USERFLAG1, "Interrupted by signal %" PRIi64, sig);
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
       }

        /* Drop some amount */
        if (amount_held > 1u) {
            const uint64_t amount_rel = cmb_random_dice(1, (long)amount_held);
            cmb_logger_user(stdout, USERFLAG1, "Holds %" PRIu64 ", releasing %" PRIu64,
                            amount_held, amount_rel);
            cmb_resourcestore_release(sp, amount_rel);
            amount_held -= amount_rel;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
       }

        /* Hang on a moment before trying again */
        cmb_logger_user(stdout, USERFLAG1, "Holding, amount held: %" PRIu64, amount_held);
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Someone stole the rest of my %s, signal %" PRIi64,
                            cmb_resourcestore_get_name(sp), sig);
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
       }
    }
}

/* The rat is very similar to the mouse, but preempts instead of acquiring */
void *ratfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    const struct simulation *simp = ctx;
    struct cmb_resourcestore *sp = simp->cheese;
    uint64_t amount_held = 0u;

    while (true) {
        /* Verify that the amount matches our own calculation */
        cmb_logger_user(stdout, USERFLAG1, "Amount held: %" PRIu64, amount_held);
        cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));

        /* Decide on a random amount to get next time and set a random priority */
        const uint64_t amount_req = cmb_random_dice(3, 10);
        const int64_t pri = cmb_random_dice(-5, 15);
        cmb_process_set_priority(me, pri);
        cmb_logger_user(stdout, USERFLAG1, "Preempting %" PRIu64, amount_req);
        int64_t sig = cmb_resourcestore_preempt(sp, amount_req);
        if (sig == CMB_PROCESS_SUCCESS) {
            /* Acquire returned successfully */
            amount_held += amount_req;
            cmb_logger_user(stdout, USERFLAG1, "Success, new amount held: %" PRIu64, amount_held);
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            /* The acquire() call did not end well */
            amount_held = 0u;
            cmb_logger_user(stdout, USERFLAG1, "Preempted during acquire, all my %s is gone",
                            cmb_resourcestore_get_name(sp));
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else {
            /* Interrupted, but we still have the same amount as before */
            cmb_logger_user(stdout, USERFLAG1, "Interrupted by signal %" PRIi64, sig);
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
       }

        /* Hold on to it for a while */
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            /* We still have it */
            cmb_logger_user(stdout, USERFLAG1, "Hold returned normally");
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else if (sig == CMB_PROCESS_PREEMPTED) {
            /* Somebody snatched it all away from us */
            amount_held = 0u;
            cmb_logger_user(stdout, USERFLAG1, "Someone stole all my %s from me!",
                            cmb_resourcestore_get_name(sp));
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }
        else {
            /* Interrupted while holding. Still have the cheese, though */
            cmb_logger_user(stdout, USERFLAG1, "Interrupted by signal %" PRIi64, sig);
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }

        /* Drop some amount */
        if (amount_held > 1u) {
            const uint64_t amount_rel = cmb_random_dice(1, (long)amount_held);
            cmb_logger_user(stdout, USERFLAG1, "Holds %" PRIu64 ", releasing %" PRIu64,
                            amount_held, amount_rel);
            cmb_resourcestore_release(sp, amount_rel);
            amount_held -= amount_rel;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
        }

        /* Hang on a moment before trying again */
        cmb_logger_user(stdout, USERFLAG1, "Holding, amount held: %" PRIu64, amount_held);
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_PREEMPTED) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Someone stole the rest of my %s, signal %" PRIi64,
                            cmb_resourcestore_get_name(sp), sig);
            amount_held = 0u;
            cmb_assert_debug(amount_held == cmb_resourcestore_held_by_process(sp, me));
       }
    }
}

void *catfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    struct simulation *simp = ctx;
    struct cmb_process **cpp = (struct cmb_process **)simp;
    const long num = NUM_MICE + NUM_RATS;

    while (true) {
        /* Nobody interrupts a sleeping cat, disregard return value */
        cmb_logger_user(stdout, USERFLAG1, "Zzzzz...");
        (void)cmb_process_hold(cmb_random_exponential(5.0));
        do {
            cmb_logger_user(stdout, USERFLAG1, "Awake, looking for rodents");
            (void)cmb_process_hold(cmb_random_exponential(1.0));
            struct cmb_process *tgt = cpp[cmb_random_dice(0, num - 1)];
            cmb_logger_user(stdout, USERFLAG1, "Chasing %s", cmb_process_name(tgt));

            /* Send it a random interrupt signal */
            const int64_t sig = (cmb_random_flip()) ?
                                 CMB_PROCESS_INTERRUPTED :
                                 cmb_random_dice(10, 100);
            cmb_process_interrupt(tgt, sig, 0);

            /* Flip a coin to decide whether to go back to sleep */
        } while (cmb_random_flip());
    }
}

void run_trial(void *vtrl)
{
    cmb_unused(vtrl);

    struct simulation *simp = cmi_malloc(sizeof(*simp));
    cmi_memset(simp, 0, sizeof(*simp));

    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    printf("Create a pile of %d cheese cubes\n", CHEESE_AMOUNT);
    simp->cheese = cmb_resourcestore_create();
    cmb_resourcestore_initialize(simp->cheese, "Cheese", CHEESE_AMOUNT);

    char scratchpad[32];
    printf("Create %d mice to compete for the cheese\n", NUM_MICE);
    for (unsigned ui = 0; ui < NUM_MICE; ui++) {
        simp->mice[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Mouse_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(simp->mice[ui], scratchpad, mousefunc, simp, pri);
        cmb_process_start(simp->mice[ui]);
    }

    printf("Create %d rats trying to preempt the cheese\n", NUM_RATS);
    for (unsigned ui = 0; ui < NUM_RATS; ui++) {
        simp->rats[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Rat_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(simp->rats[ui], scratchpad, ratfunc, simp, pri);
        cmb_process_start(simp->rats[ui]);
    }

    printf("Create %d cats chasing all the rodents\n", NUM_CATS);
    for (unsigned ui = 0; ui < NUM_CATS; ui++) {
        simp->cats[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Cat_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(simp->cats[ui], scratchpad, catfunc, simp, pri);
        cmb_process_start(simp->cats[ui]);
    }

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, NULL, simp, 100000.0, 0);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Clean up\n");
    struct cmb_process **cpp = (struct cmb_process **) simp;
    for (unsigned ui = 0; ui < NUM_MICE + NUM_RATS + NUM_CATS; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_resourcestore_terminate(simp->cheese);
    cmb_resourcestore_destroy(simp->cheese);
    cmb_event_queue_terminate();
    cmb_random_terminate();

    free(simp);
}


int main(void)
{
    run_trial(NULL);

    return 0;
}
