/*
 * Test script for event queue and simulation clock.
 * Usage:
 *      test_event [-s <seed>][-t]
 *
 * Uses random number generation from cmb_random as test data.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-26.
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

#include "test.h"

/* Soon to be defined */
static char *event_formatter(cmb_event_func *a, const void *s, const void *o);

/* An event: Prints a line of info and reschedules itself */
static void test_action(void *subject, void *object)
{
    cmb_logger_info(stdout, "%s", event_formatter(test_action, subject, object));
    const uint64_t hdl = cmb_event_schedule(test_action, subject, object,
                                            cmb_time() + cmb_random_exponential(10),
                                            (int16_t)cmb_random_dice(1, 5));
    cmb_assert_always(hdl != 0u);
}

/* Another event: Closes the bar for good */
static void end_sim(void *subject, void *object)
{
    cmb_logger_info(stdout, "%s", event_formatter(end_sim, subject, object));
    cmb_logger_warning(stdout, "===> end_sim: game over <===");
    cmb_event_queue_clear();
}

/* Lookup table for function names */
struct sym_tab_item {
    const char *name;
    cmb_event_func *func;
};

struct sym_tab_item sym_tab[] = {
    { "test_action", test_action },
    { "end_sim", end_sim }
};

static const char *evstrs[] = {"foo", "bar", "yuk"};

static char *event_formatter(cmb_event_func *a, const void *s, const void *o)
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

void test_events(const uint64_t seed)
{
    printf("Using seed: 0x%" PRIx64 "\n", seed);
    cmb_random_initialize(seed);

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
    uint64_t handle = cmb_event_schedule(end_sim, NULL, NULL, 100.0, 0);
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
    cmi_test_print_line("*");

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

    test_events(seed);

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    if (timing_enabled) {
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}