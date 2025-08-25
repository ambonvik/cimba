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
#include <stdio.h>
#include <time.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"

/* An event, prints a line of info and reschedules itself */
static void test_action(void *subject, void *object) {
    cmb_info(stdout, "%p\t%p\t%p", (void *)test_action, subject, object);
    cmb_event_schedule(test_action, subject, object,
                      cmb_random_exponential(10),
                      (int16_t)cmb_random_dice(1, 5));
}

/* Another event, closes the bar for good */
static void end_sim(void *subject, void *object) {
    cmb_info(stdout, "%p\t%p\t%p", (void *)end_sim, subject, object);
    cmb_warning(stdout, "===> end_sim: game over <===");
    cmb_event_queue_destroy();
}

static uint64_t create_seed(void) {
    struct timespec ts;
    (void) clock_gettime(CLOCK_REALTIME, &ts);

    return (uint64_t)(ts.tv_nsec ^ ts.tv_sec);
}

static const char *subjects[] = {"this", "self", "me"};
static const char *objects[] = {"that thing", "some thing", "the other thing"};

int main() {
    cmb_random_init(create_seed());

    printf("Testing event queue\n");
    double start_time = 3.0;
    printf("Creating queue, start time %g\n", start_time);
    cmb_event_queue_init(start_time);
    printf("Current simulation time %g\n", cmb_time());

    printf("Scheduling 3x3 events\n");
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            cmb_event_schedule(test_action, (void *)subjects[i], (void *)objects[j],
                              cmb_random_exponential(10.0),
                              (int16_t)cmb_random_dice(1, 5));
        }
    }

    printf("Scheduling end event\n");
    cmb_event_schedule(end_sim, NULL, NULL, 100.0, 0);

    printf("Queue now:\n");
    cmb_event_queue_print(stdout);

    printf("\nSearching for an event (%p, %p, %p)...", (void *)test_action, subjects[1], objects[0]);
    unsigned idx = cmb_event_find(test_action, (void *)subjects[1], (void *)objects[0]);
    printf("found index %d\n", idx);
    printf("It has time %g priority %d.\n", cmb_event_time(idx), cmb_event_priority(idx));

    printf("Canceling it, queue now:\n");
    cmb_event_cancel(idx);
    cmb_event_queue_print(stdout);

    printf("\nSearching for it again...  ");
    idx = cmb_event_find(test_action, (void *)subjects[1], (void *)objects[0]);
    printf("returned index %d %s\n", idx, ((idx == 0)? "not found" : "huh?"));

    printf("\nWildcard search, cancelling test action events with subject %p, any object\n", subjects[2]);
    while ((idx = cmb_event_find(test_action, (void *)subjects[2], CMB_ANY_OBJECT))) {
        printf("\tcancelling %d\n", idx);
        cmb_event_cancel(idx);
    }

    cmb_event_queue_print(stdout);

    printf("\nScheduling new events with subject %p\n", subjects[2]);
    cmb_event_schedule(test_action, (void *)subjects[2], (void *)objects[0], 20.0, 1);
    cmb_event_schedule(test_action, (void *)subjects[2], (void *)objects[1], 20.0, 1);
    cmb_event_schedule(test_action, (void *)subjects[2], (void *)objects[2], 20.0, 1);
    cmb_event_queue_print(stdout);

    printf("\nRescheduling and reprioritizing events with subject %p\n", subjects[2]);
    idx = cmb_event_find(test_action, (void *)subjects[2], (void *)objects[0]);
    cmb_event_reschedule(idx, 25.0);
    idx = cmb_event_find(test_action, (void *)subjects[2], (void *)objects[1]);
    cmb_event_reprioritize(idx, 3);
    cmb_event_queue_print(stdout);

    printf("\nWildcard search, cancelling any events with subject %p, any object\n", subjects[1]);
    while ((idx = cmb_event_find(CMB_ANY_ACTION, (void *)subjects[1], CMB_ANY_OBJECT))) {
        printf("\tcancelling %d\n", idx);
        cmb_event_cancel(idx);
    }

    printf("\nExecuting the simulation, starting time %#g\n", cmb_time());
    printf("Time:\t\tType:\tAction: \t\tSubject:\t\tObject:\n");
    while (cmb_event_execute_next()) { }

    printf("\nDone\n");
}