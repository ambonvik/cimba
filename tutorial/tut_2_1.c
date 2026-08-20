/*
 * tutorial/tut_2_1.c
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
    struct cmb_resourcepool *cheese;
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

#define CHECK_OUTCOME(sig, me, rpoolp, amnt)                                    \
{                                                                               \
    if (sig == CMB_PROCESS_SUCCESS) {                                           \
        /* Acquire returned successfully */                                     \
        cmb_logger_user(stdout, USERFLAG1,                                      \
                        "Success, has %" PRIu64,                                \
                        amnt);                                                  \
    }                                                                           \
    else if (sig == CMB_PROCESS_PREEMPTED) {                                    \
        /* The acquire() call did not end well */                               \
        cmb_logger_user(stdout, USERFLAG1,                                      \
                        "Preempted, all is gone, has %" PRIu64,                 \
                        amnt);                                                  \
    }                                                                           \
    else {                                                                      \
        /* Interrupted, but we still have the same amount as before */          \
        cmb_logger_user(stdout, USERFLAG1,                                      \
                        "Interrupted by signal %" PRIi64 ", still has %" PRIu64,\
                        sig, amnt);                                             \
    }                                                                           \
}

/* The busy life of a mouse */
void *mousefunc(struct cmb_process *me, void *ctx)
{
    cmb_assert_release(me != NULL);
    cmb_assert_release(ctx != NULL);

    const struct simulation *simp = ctx;
    struct cmb_resourcepool *rpoolp = simp->cheese;

    while (true) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Top of loop, has %" PRIu64,
                        cmb_resourcepool_held(rpoolp, me));

        /* Decide on a random amount to get next time and set a random priority */
        const uint64_t amount_req = cmb_random_dice(1, 5);
        const int64_t pri = cmb_random_dice(-10, 10);
        cmb_process_priority_set(me, pri);

        cmb_logger_user(stdout, USERFLAG1, "Acquiring %" PRIu64, amount_req);
        int64_t sig = cmb_resourcepool_acquire(rpoolp, amount_req);
        CHECK_OUTCOME(sig, me, rpoolp, cmb_resourcepool_held(rpoolp, me));

        /* Hold on to it for a while */
        cmb_logger_user(stdout, USERFLAG1,
                        "Holding %" PRIu64,
                        cmb_resourcepool_held(rpoolp, me));
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        CHECK_OUTCOME(sig, me, rpoolp, cmb_resourcepool_held(rpoolp, me));

        /* Drop some amount */
        uint64_t amnt = cmb_resourcepool_held(rpoolp, me);
        if (amnt > 1u) {
            const uint64_t amount_rel = cmb_random_dice(1, amnt);
            cmb_logger_user(stdout, USERFLAG1,
                            "Has %" PRIu64 ", releasing %" PRIu64,
                            amnt, amount_rel);
            cmb_resourcepool_release(rpoolp, amount_rel);
       }

        /* Hang on a moment before trying again */
        cmb_logger_user(stdout, USERFLAG1,
                        "Holding %" PRIu64,
                        cmb_resourcepool_held(rpoolp, me));
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        CHECK_OUTCOME(sig, me, rpoolp, cmb_resourcepool_held(rpoolp, me));
    }
}

/* The rat is very similar to the mouse, but preempts instead of acquiring */
void *ratfunc(struct cmb_process *me, void *ctx)
{
    cmb_assert_release(me != NULL);
    cmb_assert_release(ctx != NULL);

    const struct simulation *simp = ctx;
    struct cmb_resourcepool *rpoolp = simp->cheese;

    while (true) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Top of loop, has: %" PRIu64,
                        cmb_resourcepool_held(rpoolp, me));

        /* Decide on a random amount to get next time and set a random priority */
        const uint64_t amount_req = cmb_random_dice(3, 10);
        const int64_t pri = cmb_random_dice(-5, 15);
        cmb_process_priority_set(me, pri);

        cmb_logger_user(stdout, USERFLAG1, "Preempting %" PRIu64, amount_req);
        int64_t sig = cmb_resourcepool_preempt(rpoolp, amount_req);
        CHECK_OUTCOME(sig, me, rpoolp, cmb_resourcepool_held(rpoolp, me));

        /* Hold on to it for a while */
        cmb_logger_user(stdout, USERFLAG1,
                        "Holding %" PRIu64,
                        cmb_resourcepool_held(rpoolp, me));
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        CHECK_OUTCOME(sig, me, rpoolp, cmb_resourcepool_held(rpoolp, me));

        /* Drop some amount */
        uint64_t amount_held = cmb_resourcepool_held(rpoolp, me);
        if (amount_held > 1u) {
            const uint64_t amount_rel = cmb_random_dice(1, amount_held);
            cmb_logger_user(stdout, USERFLAG1,
                            "Has %" PRIu64 ", releasing %" PRIu64,
                            amount_held, amount_rel);
            cmb_resourcepool_release(rpoolp, amount_rel);
       }

        /* Hang on a moment before trying again */
        cmb_logger_user(stdout, USERFLAG1,
                        "Holding %" PRIu64,
                        cmb_resourcepool_held(rpoolp, me));
        sig = cmb_process_hold(cmb_random_exponential(1.0));
        CHECK_OUTCOME(sig, me, rpoolp, cmb_resourcepool_held(rpoolp, me));
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
            cmb_logger_user(stdout, USERFLAG1,
                            "Awake, looking for rodents");
            struct cmb_process *tgt = cpp[cmb_random_dice(0, num - 1)];
            cmb_logger_user(stdout, USERFLAG1,
                            "Chasing %s", cmb_process_name(tgt));

            /* Send it a random interrupt signal */
            const int64_t sig = (cmb_random_flip()) ?
                                 CMB_PROCESS_INTERRUPTED :
                                 cmb_random_dice(10, 100);
            cmb_process_interrupt(tgt, sig, 0);

            cmb_logger_user(stdout, USERFLAG1, "Pondering");
            (void)cmb_process_hold(cmb_random_exponential(1.0));

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
    simp->cheese = cmb_resourcepool_create();
    cmb_resourcepool_initialize(simp->cheese, "Cheese", CHEESE_AMOUNT);

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

    cmb_resourcepool_terminate(simp->cheese);
    cmb_resourcepool_destroy(simp->cheese);
    cmb_event_queue_terminate();
    cmb_random_terminate();

    free(simp);
}


int main(void)
{
    run_trial(NULL);

    return 0;
}
