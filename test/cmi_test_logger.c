/*
 * Test script for logger functions.
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
#include <stdbool.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"

/* An event, prints a line of info and reschedules itself */
static void test_action(void *subject, void *object)
{
    cmb_logger_info(stdout, "%p\t%p\t%p", (void *)test_action, subject, object);
    cmb_event_schedule(test_action, subject, object,
                       cmb_time() + cmb_random_exponential(3600),
                       (int16_t)cmb_random_dice(1, 5));
}

/* Another event, closes the bar for good */
static void end_sim(void *subject, void *object)
{
    cmb_logger_info(stdout, "%p\t%p\t%p", (void *)end_sim, subject, object);
    cmb_logger_warning(stdout, "===> end_sim: game over <===");
    cmb_event_queue_terminate();
}

/* Format time values as if they are decimal minutes, print in DD HH:MM:SS.sss format */
#define MYBUFLEN 20
static char mybuf[MYBUFLEN];

static char *myformatter(const double t)
{
    double tmp = t;
    const unsigned days = (unsigned)(tmp / (24.0 * 60.0));
    tmp -= (double)(days * 24 * 60);
    const unsigned hours = (unsigned)(tmp / 60.0);
    tmp -= (double)(hours * 60);
    const unsigned minutes = (unsigned)floor(tmp);
    tmp -= (double)minutes;
    const double seconds = tmp * 60.0;

    const int r = snprintf(mybuf, MYBUFLEN, "%02d %02d:%02d:%06.3f", days, hours, minutes, seconds);
    assert((r > 0) && (r < MYBUFLEN));

    return mybuf;
}

int main(void)
{
    cmb_random_initialize(cmb_random_get_hwseed());
    cmb_event_queue_initialize(0.0);
    cmb_set_timeformatter(myformatter);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const char *objects[] = {"that thing", "some thing", "the other thing"};
            const char *subjects[] = {"this", "self", "me"};
            cmb_event_schedule(test_action, (void *)subjects[i], (void *)objects[j],
                               cmb_random_exponential(60.0),
                               (int16_t)cmb_random_dice(1, 5));
        }
    }

    const double two_days = 2.0 * 24.0 * 60.0;
    cmb_event_schedule(end_sim, NULL, NULL, two_days, 0);
    while (cmb_event_execute_next()) { }
    cmb_logger_error(stdout, "We seemed to run out of time here. (This was a test.)");
    /* Not reached */
    cmb_logger_fatal(stdout, "How did this happen?");
    cmb_assert_release(false);
}