/*
 * Test script for logger functions.
 * Usage:
 *      test_logger [-s <seed>]
 *
 * Will normally return a non-zero exit code, since we end by testing the
 * error functions. An "abnormal" exit with abort() is actually successful.
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

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"

/* An event: Prints a line of info and reschedules itself */
static void test_action(void *subject, void *object)
{
    cmb_logger_info(stdout, "%p\t%s\t%s", (void *)test_action,  (char *)subject, (char *)object);
    const double dt = cmb_random_exponential(3600);
    cmb_assert_always(dt >= 0.0);
    const int64_t pri = cmb_random_dice(1, 5);
    cmb_assert_always((pri >= 1) && (pri <= 5));
    const uint64_t hndl = cmb_event_schedule(test_action, subject, object,
                                             cmb_time() + dt, pri);
    cmb_assert_always(hndl != 0u);
}

/* Another event: Closes the bar for good */
static void end_sim(void *subject, void *object)
{
    cmb_logger_info(stdout, "%p\t%s\t%s", (void *)end_sim, (char *)subject, (char *)object);
    cmb_logger_warning(stdout, "===> end_sim: game over <===");
    cmb_event_queue_clear();
}

/*
 * Format time values as if they are decimal minutes,
 * print in DD HH:MM:SS.sss format
 */
#define FMTBUFLEN 20
static char fmtbuf[FMTBUFLEN];

static const char *hhhmmss_formatter(const double t)
{
    double tmp = t;
    const unsigned days = (unsigned)(tmp / (24.0 * 60.0));
    tmp -= (double)(days * 24 * 60);
    const unsigned hours = (unsigned)(tmp / 60.0);
    tmp -= (double)(hours * 60);
    const unsigned minutes = (unsigned)floor(tmp);
    tmp -= (double)minutes;
    const double seconds = tmp * 60.0;

    const unsigned r = snprintf(fmtbuf,
                               FMTBUFLEN,
                               "%02d-%02d:%02d:%06.3f",
                               days + 1u, hours, minutes, seconds);
    assert((r > 0) && (r < FMTBUFLEN));

    return fmtbuf;
}

void test_logger(uint64_t seed)
{
    cmb_random_initialize(seed);
    cmb_event_queue_initialize(0.0);
    cmb_logger_timeformatter_set(hhhmmss_formatter);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const char *objects[] = {"that thing", "some thing", "other thing"};
            const char *subjects[] = {"this", "self", "me  "};
            cmb_event_schedule(test_action,
                               (void *)subjects[i],
                               (void *)objects[j],
                               cmb_random_exponential(60.0),
                               (int16_t)cmb_random_dice(1, 5));
        }
    }

    const double two_days = 2.0 * 24.0 * 60.0;
    cmb_event_schedule(end_sim, NULL, NULL, two_days, 0);
    while (cmb_event_execute_next()) { }

    /* Note that cmb_logger_error exits the current thread, exit code 0 */
    cmb_logger_error(stdout, "Hard stop (intentional)");
}

int main(int argc, char *argv[])
{
     uint64_t seed = cmb_random_hwseed();

    int opt;
    while ((opt = getopt(argc, argv, "s:")) != -1) {
        switch (opt) {
            case 's':
                errno = 0;
                seed = (uint64_t)strtoul(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <seed>]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    test_logger(seed);

    /* If we got here, something failed */
    cmb_logger_fatal(stderr, "Returned from test_logger, cmb_logger_error() ignored!");

    /* not reached */
}