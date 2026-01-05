/*
 * Test script for event queue and simulation clock.
 *
 * Uses random number generation from cmb_random as test data.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
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

#include "test.h"

/* An event: Prints a line of info and reschedules itself */
static void test_action(void *subject, void *object)
{
    cmb_logger_info(stdout, "%p\t%p\t%p", (void *)test_action, subject, object);
    cmb_event_schedule(test_action, subject, object,
                      cmb_time() + cmb_random_exponential(10),
                      (int16_t)cmb_random_dice(1, 5));
}

/* Another event: Closes the bar for good */
static void end_sim(void *subject, void *object)
{
    cmb_logger_info(stdout, "%p\t%p\t%p", (void *)end_sim, subject, object);
    cmb_logger_warning(stdout, "===> end_sim: game over <===");
    cmb_event_queue_clear();
}

static const char *subjects[] = {"this", "self", "me"};
static const char *objects[] = {"that thing", "some thing", "the other thing"};

int main(void)
{
    cmi_test_print_line("-");
    printf("Testing event queue\n");
    const double start_time = 3.0;
    printf("Creating queue, start time %g\n", start_time);
    cmb_event_queue_initialize(start_time);
    printf("Current simulation time %g\n", cmb_time());

    cmb_random_initialize(cmb_random_hwseed());
    printf("Scheduling 3x3 events\n");
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const uint64_t handle = cmb_event_schedule(test_action,
                              (void *)subjects[i],
                              (void *)objects[j],
                              cmb_time() + cmb_random_exponential(10.0),
                              cmb_random_dice(1, 5));
            printf("Scheduled event %" PRIu64 "\n", handle);
        }
    }

    printf("Scheduling end event\n");
    (void)cmb_event_schedule(end_sim, NULL, NULL, 100.0, 0);
    cmi_test_print_line("-");
    cmb_event_queue_print(stdout);
    cmi_test_print_line("-");

    printf("\nSearching for an event (%p, %p, %p)...", (void *)test_action, subjects[1], objects[0]);
    uint64_t handle = cmb_event_pattern_find(test_action, (void *)subjects[1], (void *)objects[0]);
    if (handle != 0u) {
        printf("found event %" PRIu64 "\n", handle);
        printf("It has time %g priority %" PRIi64 ".\n", cmb_event_time(handle), cmb_event_priority(handle));

        printf("Canceling it\n");
        cmb_event_cancel(handle);

        printf("\nSearching for it again...  ");
        handle = cmb_event_pattern_find(test_action, (void *)subjects[1], (void *)objects[0]);
        printf("returned handle %" PRIu64 " %s\n", handle, ((handle == 0)? "not found" : "huh?"));
    }
    else {
        printf("not found???\n");
    }

    printf("\nWildcard search, searching for test action events with subject %p, any object\n", subjects[2]);
    while ((handle = cmb_event_pattern_find(test_action, (void *)subjects[2], CMB_ANY_OBJECT))) {
        printf("\tcanceling %" PRIu64 "\n", handle);
        cmb_event_cancel(handle);
    }

    printf("\nScheduling new events with subject %p\n", subjects[2]);
    cmb_event_schedule(test_action, (void *)subjects[2], (void *)objects[0], 20.0, 1);
    cmb_event_schedule(test_action, (void *)subjects[2], (void *)objects[1], 20.0, 1);
    cmb_event_schedule(test_action, (void *)subjects[2], (void *)objects[2], 20.0, 1);

    printf("\nRescheduling and reprioritizing two events with subject %p\n", subjects[2]);
    handle = cmb_event_pattern_find(test_action, (void *)subjects[2], (void *)objects[0]);
    cmb_event_reschedule(handle, cmb_time() + 25.0);
    handle = cmb_event_pattern_find(test_action, (void *)subjects[2], (void *)objects[1]);
    cmb_event_reprioritize(handle, 3);

    printf("\nWildcard search, counting events with subject %p, any object\n", subjects[1]);
    uint64_t cnt = cmb_event_pattern_count(CMB_ANY_ACTION, subjects[1], CMB_ANY_OBJECT);
    printf("Found %" PRIu64 " events\n", cnt);

    printf("\nWildcard search, cancelling any events with subject %p, any object\n", subjects[1]);
    cnt = cmb_event_pattern_cancel(CMB_ANY_ACTION, subjects[1], CMB_ANY_OBJECT);
    printf("Cancelled %" PRIu64 " events\n", cnt);
    cmi_test_print_line("-");

    printf("\nExecuting the simulation, starting time %#g\n", cmb_time());
    printf("Time:\t\tType:\tAction: \t\tSubject:\t\tObject:\n");
    cmb_event_queue_execute();

    cmb_event_queue_terminate();
    cmi_test_print_line("=");

    return 0;
}