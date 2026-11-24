/*
 * Test script for event queue and simulation clock.
 * Usage:
 *      test_event [-s <seed>][-t]
 *
 * Uses random number generation from cmb_random as test data.
 *
 * Partly coded by Anthropic Claude Opus 5.5
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
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "testutils.h"

#define USERFLAG 0x00000001

/* Soon to be defined */
static const char *event_formatter(cmb_event_func *a, const void *s, const void *o);

/* An event: Prints a line of info and reschedules itself */
static void test_action(void *subject, void *object)
{
    cmb_logger_user(stdout, USERFLAG, "%s", event_formatter(test_action, subject, object));
    const double t = cmb_time() + cmb_random_exponential(10.0);
    const int64_t p = cmb_random_dice(1, 5);

    const uint64_t hdl = cmb_event_schedule(test_action, subject, object, t, p);
    cmb_assert_always(hdl != 0u);
}

/* Another event: Closes the bar for good */
static void end_sim_evnt(void *subject, void *object)
{
    cmb_logger_user(stdout, USERFLAG, "%s", event_formatter(end_sim_evnt, subject, object));
    cmb_logger_user(stdout, USERFLAG, "===> end_sim_evnt: game over <===");
    cmb_event_queue_clear();
}

/* Lookup table for function names */
struct sym_tab_item {
    const char *name;
    cmb_event_func *func;
};

struct sym_tab_item sym_tab[] = {
    { "test_action", test_action },
    { "end_sim_evnt", end_sim_evnt }
};

static const char *evstrs[] = {"foo", "bar", "yuk"};

static const char *event_formatter(cmb_event_func *a, const void *s, const void *o)
{
    static char buf[128];
    for (unsigned ui = 0; ui < sizeof(sym_tab) / sizeof(sym_tab[0]); ui++) {
        if (a == sym_tab[ui].func) {
            const char *ss = (s == NULL) ? "<NULL>" : (char *)s;
            const char *so = (o == NULL) ? "<NULL>" : (char *)o;
            snprintf(buf, 128, "%s\t%s\t%s", sym_tab[ui].name, ss, so);
            return buf;
        }
    }

    return NULL;
}

static void test_events(const uint64_t seed)
{
    printf("Using seed: 0x%" PRIx64 "\n", seed);
    cmb_random_initialize(seed);
    cmb_logger_flags_off(CMB_LOGGER_INFO);

    cmi_test_print_line("-");
    printf("Testing event queue\n");
    const double start = 3.0;
    printf("Creating queue, start time %g\n", start);
    cmb_event_queue_initialize(start);
    printf("Current simulation time %g\n", cmb_time());

    printf("Scheduling 3x3 events\n");
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const double t = cmb_time() + cmb_random_exponential(10.0);
            cmb_assert_always(t >= cmb_time());
            const int64_t p = cmb_random_dice(1, 5);
            cmb_assert_always((p >= 1) && (p <= 5));
            const uint64_t handle = cmb_event_schedule(test_action,
                                                       (void *)evstrs[i],
                                                       (void *)evstrs[j],
                                                       t, p);
            cmb_assert_always(handle != 0u);
            cmb_assert_always(cmb_event_is_scheduled(handle) == true);
            cmb_assert_always(cmb_event_time(handle) == t);
            cmb_assert_always(cmb_event_priority(handle) == p);
            printf("Scheduled event %" PRIu64 "\n", handle);
        }
    }

    printf("Scheduling end event\n");
    uint64_t handle = cmb_event_schedule(end_sim_evnt, NULL, NULL, 100.0, 0);
    cmb_assert_always(handle != 0u);
    cmb_assert_always(cmb_event_is_scheduled(handle));
    cmb_event_queue_print(stdout, event_formatter);

    printf("\nSearching for an event (%s)...", event_formatter(test_action, (void *)evstrs[1], (void *)evstrs[0]));
    handle = cmb_event_pattern_find(test_action, (void *)evstrs[1], (void *)evstrs[0]);
    cmb_assert_always(handle != 0u);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);
    printf("found event %" PRIu64 "\n", handle);
    printf("It has time %g priority %" PRIi64 ".\n", cmb_event_time(handle), cmb_event_priority(handle));

    printf("Canceling it\n");
    bool found = cmb_event_cancel(handle);
    cmb_assert_always(found);
    cmb_assert_always(cmb_event_is_scheduled(handle) == false);

    printf("\nSearching for it again...  ");
    handle = cmb_event_pattern_find(test_action, (void *)evstrs[1], (void *)evstrs[0]);
    cmb_assert_always(handle == 0u);

    cmb_event_queue_print(stdout, event_formatter);

    printf("\nWildcard search, searching for test action events with subject %s, any object\n", evstrs[2]);
    while ((handle = cmb_event_pattern_find(test_action, (void *)evstrs[2], CMB_ANY_OBJECT))) {
        printf("\tcanceling %" PRIu64 "\n", handle);
        cmb_assert_always(cmb_event_is_scheduled(handle) == true);
        found = cmb_event_cancel(handle);
        cmb_assert_always(found);
        cmb_assert_always(cmb_event_is_scheduled(handle) == false);
    }

    cmb_event_queue_print(stdout, event_formatter);

    printf("\nScheduling new events with subject %s\n", evstrs[2]);
    const double t = 20.0;
    const int64_t p = 20;
    handle = cmb_event_schedule(test_action, (void *)evstrs[2], (void *)evstrs[0], t, p);
    cmb_assert_always(handle != 0u);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);
    handle = cmb_event_schedule(test_action, (void *)evstrs[2], (void *)evstrs[1], t, p);
    cmb_assert_always(handle != 0u);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);
    handle = cmb_event_schedule(test_action, (void *)evstrs[2], (void *)evstrs[2], t, p);
    cmb_assert_always(handle != 0u);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);

    cmb_event_queue_print(stdout, event_formatter);

    printf("\nRescheduling and reprioritizing two events with subject %s\n", evstrs[2]);
    handle = cmb_event_pattern_find(test_action, (void *)evstrs[2], (void *)evstrs[0]);
    cmb_assert_always(handle != 0u);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);
    const double nt = cmb_time() + 25.0;
    found = cmb_event_reschedule(handle, nt);
    cmb_assert_always(found);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);
    cmb_assert_always(cmb_event_time(handle) == nt);
    handle = cmb_event_pattern_find(test_action, (void *)evstrs[2], (void *)evstrs[1]);
    const int64_t np = 3;
    found = cmb_event_reprioritize(handle, np);
    cmb_assert_always(found);
    cmb_assert_always(cmb_event_is_scheduled(handle) == true);
    cmb_assert_always(cmb_event_priority(handle) == np);

    cmb_event_queue_print(stdout, event_formatter);

    printf("\nWildcard search, counting events with subject %s, any object\n", evstrs[1]);
    uint64_t cnt = cmb_event_pattern_count(CMB_ANY_ACTION, evstrs[1], CMB_ANY_OBJECT);
    printf("Found %" PRIu64 " events\n", cnt);

    cmb_event_queue_print(stdout, event_formatter);

    printf("\nWildcard search, cancelling any events with subject %s, any object\n", evstrs[1]);
    cnt = cmb_event_pattern_cancel(CMB_ANY_ACTION, evstrs[1], CMB_ANY_OBJECT);
    printf("Cancelled %" PRIu64 " events\n", cnt);

    cmb_event_queue_print(stdout, event_formatter);

    printf("\nExecuting the simulation, starting time %#g\n", cmb_time());
    cmi_test_print_line("-");
    printf("Time:\t\tType:\tAction: \t\tSubject:\t\tObject:\n");
    cmb_event_queue_execute();
    cmi_test_print_line("-");

    cmb_event_queue_print(stdout, event_formatter);
    cmb_event_queue_terminate();
    cmb_random_terminate();
    cmi_test_print_line("*");
}

/*
 * Wake order among processes waiting for the same event.
 * Silent regression test unless an assert trips.
 */
struct evwait_ctx {
    uint64_t handle;        /* the event to wait for */
    double arrive;          /* delay before starting to wait */
    int *counter;           /* shared running counter of wakeups */
    int order;              /* counter value when woken, 0 if never */
    double woken;           /* sim time when woken, -1.0 if never */
    int64_t sig;            /* signal returned by the wait */
};

static void evwait_noop(void *subject, void *object)
{
    cmb_unused(subject);
    cmb_unused(object);
}

static void *evwait_waiter(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);
    struct evwait_ctx *c = vctx;

    cmb_assert_always(cmb_process_hold(c->arrive) == CMB_PROCESS_SUCCESS);
    c->sig = cmb_process_wait_event(c->handle);
    c->order = ++(*c->counter);
    c->woken = cmb_time();

    return NULL;
}

static void *evwait_interrupter(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    cmb_assert_always(cmb_process_hold(6.0) == CMB_PROCESS_SUCCESS);
    cmb_process_interrupt((struct cmb_process *)vctx, CMB_PROCESS_INTERRUPTED, 0);

    return NULL;
}

static void test_waiter_order(void)
{
    cmb_event_queue_initialize(0.0);
    int counter = 0;

    /* W1..W5 start waiting for an event at t=10 from t=1..5. W4 has higher
     * priority than the others. W2 is interrupted at t=6 and leaves. When the
     * event fires, W4 must be woken first, then W1, W3, W5 in arrival order. */
    const uint64_t handle = cmb_event_schedule(evwait_noop, NULL, NULL, 10.0, 0);
    cmb_assert_always(handle != 0u);

    enum { NWAIT = 5 };
    struct evwait_ctx w[NWAIT];
    struct cmb_process *wp[NWAIT];
    for (unsigned ui = 0u; ui < NWAIT; ui++) {
        w[ui] = (struct evwait_ctx){ handle, (double)(ui + 1u), &counter, 0, -1.0, INT64_MIN };
        char name[16];
        snprintf(name, sizeof(name), "W%u", ui + 1u);
        wp[ui] = cmb_process_create();
        cmb_process_initialize(wp[ui], name, evwait_waiter, &w[ui], (ui == 3u) ? 5 : 0);
        cmb_process_start(wp[ui]);
    }

    struct cmb_process *ip = cmb_process_create();
    cmb_process_initialize(ip, "Interrupter", evwait_interrupter, wp[1], 0);
    cmb_process_start(ip);

    cmb_event_queue_execute();

    cmb_assert_always((w[1].sig == CMB_PROCESS_INTERRUPTED) && (w[1].woken == 6.0));
    cmb_assert_always(w[1].order == 1);
    const unsigned expected[] = { 3u, 0u, 2u, 4u };
    for (unsigned ui = 0u; ui < 4u; ui++) {
        const struct evwait_ctx *c = &w[expected[ui]];
        cmb_assert_always(c->sig == CMB_PROCESS_SUCCESS);
        cmb_assert_always(c->woken == 10.0);
        cmb_assert_always(c->order == (int)ui + 2);
    }

    cmb_process_terminate(ip);
    cmb_process_destroy(ip);
    for (unsigned ui = 0u; ui < NWAIT; ui++) {
        cmb_process_terminate(wp[ui]);
        cmb_process_destroy(wp[ui]);
    }

    cmb_event_queue_terminate();
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();

    int opt;
    while ((opt = getopt(argc, argv, "s:t")) != -1) {
        switch (opt) {
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
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

    test_events(seed);
    test_waiter_order();

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    if (timing_enabled) {
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}