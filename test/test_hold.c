/*
 * HOLD performance test on event queue
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
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
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "cimba.h"
#include "testutils.h"
extern uint32_t cmi_cpu_cores(void);

#define N_SAMPLES 10000000u
#define N_TRIALS 10u
#define N_START 0u
#define N_STEPS 8u
#define STEP_SZ 1u
#define LOOKAHEAD 0.0
#define MEAN 1.0

#define USERLEVEL 0x0001

struct simulation {
    struct cmb_objectqueue *queue;
    struct cmb_process *arrival;
    struct cmb_process *service;
};

struct trial {
    /* Parameters */
    uint64_t n_holding;
    uint64_t n_samples;
    double hold_mean;
    double lookahead;
    uint64_t seed;
    double events_sec;
    double ns_event;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};


static pthread_mutex_t tty = PTHREAD_MUTEX_INITIALIZER;

static void event(void *subject, void *object)
{
    cmb_assert_debug(subject != NULL);
    cmb_unused(object);
    const struct trial *trl = subject;
    const double_t m = trl->hold_mean;
    const double lh = trl->lookahead;
    const double t = cmb_time() + cmb_random_exponential(m) + lh;
    cmb_event_schedule(event, subject, object, t, 0);
}

static void run_trial(void *vtrl)
{
    cmb_assert_debug(vtrl != NULL);
    struct trial *trl = vtrl;
    const uint64_t n_holding = trl->n_holding;
    const uint64_t n_samples = trl->n_samples;

    cmb_event_queue_initialize(0.0);
    cmb_random_initialize(trl->seed);
    for (uint64_t ui = 0u; ui < n_holding; ui++) {
        const double t = cmb_random();
        cmb_event_schedule(event, vtrl, NULL, t, 0);
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (uint64_t ui = 0u; ui < n_samples; ui++) {
        cmb_event_execute_next();
    }

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (double)(end_time.tv_sec - start_time.tv_sec);
    elapsed += (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

    const double eps = n_samples / elapsed;
    trl->events_sec = eps;
    trl->ns_event = 1e9 / eps;

    cmb_random_terminate();
    cmb_event_queue_terminate();
}

static void run_level(const uint64_t n_holding,
                      const uint64_t n_trials, const uint64_t n_samples,
                      const double mean, const double lookahead,
                      const uint64_t master_seed)
{
    struct trial *experiment = calloc(n_trials, sizeof(*experiment));
    for (unsigned ui = 0; ui < n_trials; ui++) {
        struct trial *trl = &experiment[ui];
        trl->n_holding = n_holding;
        trl->n_samples = n_samples;
        trl->hold_mean = mean;
        trl->lookahead = lookahead;
        trl->seed = cmb_random_fmix64(master_seed, ui);
    }

    cimba_run(experiment, n_trials, sizeof(*experiment), run_trial);

    struct cmb_datasummary dsu_esec;
    cmb_datasummary_initialize(&dsu_esec);
    struct cmb_datasummary dsu_nse;
    cmb_datasummary_initialize(&dsu_nse);
    for (unsigned ui = 0; ui < n_trials; ui++) {
        struct trial *trl = &experiment[ui];
        cmb_datasummary_add(&dsu_esec, trl->events_sec);
        cmb_datasummary_add(&dsu_nse, trl->ns_event);
    }

    pthread_mutex_lock(&tty);
    printf ("%10" PRIu64 "                %8.3g                %8.5g\n",
             n_holding, cmb_datasummary_mean(&dsu_esec), cmb_datasummary_mean(&dsu_nse));
    pthread_mutex_unlock(&tty);

    cmb_datasummary_terminate(&dsu_esec);
    cmb_datasummary_terminate(&dsu_nse);
    free(experiment);
}

int main(const int argc, char *argv[])
{
    uint64_t master_seed = cmb_random_hwseed();
    uint64_t n_samples = N_SAMPLES;
    uint64_t n_trials = N_TRIALS;
    uint64_t n_start = N_START;
    uint64_t n_levels = N_STEPS;
    uint64_t step_sz = STEP_SZ;
    double lookahead = LOOKAHEAD;
    double mean = MEAN;
    uint32_t n_threads = cmi_cpu_cores();

    int opt;
    while ((opt = getopt(argc, argv, "l:m:n:s:t:")) != -1) {
        switch (opt) {
            case 'l': {
                errno = 0;
                n_levels = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }
            case 'm': {
                errno = 0;
                step_sz = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || step_sz <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }
            case 'n': {
                errno = 0;
                n_trials = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }
            case 's': {
                errno = 0;
                master_seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || master_seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }
            case 't': {
                errno = 0;
                n_threads = (uint32_t)strtoul(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }
            default: {
                return EXIT_FAILURE;
            }
        }
    }

    printf("Master seed: 0x%" PRIx64 "\n", master_seed);
    printf("Number of threads: %" PRIu32 "\n", n_threads);
    printf("Number of levels: %" PRIu64 ", from %f to %f\n", n_levels,
            pow(10.0, (double)n_start), pow(10.0, (double)(n_start + n_levels)));
    printf("Number of trials per level: %" PRIu64 "\n", n_trials);
    printf("Number of samples per trial: %" PRIu64 "\n", n_samples);
    printf("Step multiplier: %4.2f\n", pow(10.0, step_sz));

    cimba_threads_use(n_threads);

    printf("\nEvent queue size:   Events per second:     Nanoseconds per event:\n");
    for (uint64_t ui = n_start; ui < n_levels; ui++) {
        const uint64_t holding = (uint64_t)pow(10.0, (double)ui * step_sz);
        run_level(holding, n_trials, n_samples, mean, lookahead, master_seed);
    }

    return 0;
}
