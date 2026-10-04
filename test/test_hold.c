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

#define N_SAMPLES 1000000u          /* Override with -x command line option */
#define N_TRIALS 10u                /* Override with -n command line option */
#define N_LP 10u                    /* Override with -p command line option */
#define N_START 0u                  /* Override with -a command line option */
#define N_QLENGTHS 5u               /* Override with -q command line option */
#define STEP_MULT 1.0               /* Override with -m command line option */
#define LOOKAHEAD 0.0               /* Override with -h command line option */
#define MEAN 1.0                    /* Override with -u command line option */
#define P_REMOTE 0.25               /* Override with -r command line option */

#define USERLEVEL 0x0001

struct simulation {
    uint64_t n_lp;
    uint64_t *lp_arr;
};

struct trial {
    /* Parameters */
    uint64_t n_holding;
    uint64_t n_samples;
    uint64_t n_lp;
    double hold_mean;
    double lookahead;
    double p_remote;
    uint64_t seed;
    /* Results */
    double events_sec;
    double ns_event;
    uint64_t n_events;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};


static pthread_mutex_t tty = PTHREAD_MUTEX_INITIALIZER;

static void event(void *subject, void *object)
{
    cmb_assert_debug(object != NULL);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;

    /* Update "our" logical process counter */
    const uint64_t my_lp = (uint64_t)subject;
    cmb_assert_debug(my_lp < sim->n_lp);
    cmb_assert_debug(sim->lp_arr != NULL);
    sim->lp_arr[my_lp]++;

    /* Hold for a while */
    const struct trial *trl = ctx->trl;
    const double_t m = trl->hold_mean;
    const double lh = trl->lookahead;
    double t = cmb_time();
    const double diff = m - lh;
    if (diff > 0.0) {
        /* Random */
        t += cmb_random_exponential(m - lh) + lh;
    }
    else {
        /* Deterministic */
        t += lh;
    }

    /* Where to route it next? */
    if (cmb_random_bernoulli(trl->p_remote) == 1u) {
        subject = (void *)cmb_random_discrete_uniform(sim->n_lp);
    }

    /* Next, please */
    cmb_event_schedule(event, subject, object, t, 0);
}

static double monotonic_seconds(void)
{
    struct timespec tnow;
    clock_gettime(CLOCK_MONOTONIC, &tnow);

    return tnow.tv_sec + tnow.tv_nsec / 1e9;
}

static void run_trial(void *vtrl)
{
    cmb_assert_debug(vtrl != NULL);
    struct trial *trl = vtrl;
    const uint64_t n_holding = trl->n_holding;
    const uint64_t n_samples = trl->n_samples;
    const double mean = trl->hold_mean;
    const double lh = trl->lookahead;

    struct simulation sim = { 0 };
    sim.n_lp = trl->n_lp;
    sim.lp_arr = cmi_calloc((size_t)sim.n_lp, sizeof(*(sim.lp_arr)));
    struct context ctx = { 0 };
    ctx.sim = &sim;
    ctx.trl = trl;

    /* Initial population */
    cmb_event_queue_initialize(0.0);
    cmb_random_initialize(trl->seed);
    for (uint64_t ui = 0u; ui < n_holding; ui++) {
        const double t = (mean > lh) ? cmb_random_exponential(mean - lh) + lh : lh;
        const uint64_t lp = cmb_random_discrete_uniform(sim.n_lp);
        cmb_event_schedule(event, (void *)lp, &ctx, t, 0);
    }

    /* Warmup */
    const uint64_t n_warm = (n_holding > 100000u) ? n_holding : 100000u;
    for (uint64_t ui = 0u; ui < n_warm; ui++) {
        cmb_event_execute_next();
    }

    /* Measure over at least t_min of wall clock, in chunks so the clock read
     * stays out of the inner loop.  n_samples is an upper bound, not a target. */
    const double t_min = 1.0;
    const uint64_t chunk = (n_samples < 65536u) ? n_samples : 65536u;
    uint64_t count = 0u;
    double elapsed;
    const double t0 = monotonic_seconds();
    do {
        for (uint64_t ui = 0u; ui < chunk; ui++) {
            cmb_event_execute_next();
        }
        count += chunk;
        elapsed = monotonic_seconds() - t0;
    } while ((elapsed < t_min) && (count < n_samples));

    const double eps = (double)count / elapsed;
    trl->events_sec = eps;
    trl->ns_event = 1e9 / eps;
    trl->n_events = count;

    cmi_free(sim.lp_arr);
    cmb_random_terminate();
    cmb_event_queue_terminate();
}

static uint64_t run_level(const uint64_t n_holding, const uint64_t n_lp,
                      const uint64_t n_trials, const uint64_t n_samples,
                      const double mean, const double lookahead,
                      const double p_remote,
                      const uint64_t master_seed)
{
    struct trial *experiment = calloc(n_trials, sizeof(*experiment));
    for (unsigned ui = 0; ui < n_trials; ui++) {
        struct trial *trl = &experiment[ui];
        trl->n_holding = n_holding;
        trl->n_samples = n_samples;
        trl->n_lp = n_lp;
        trl->hold_mean = mean;
        trl->p_remote = p_remote;
        trl->lookahead = lookahead;
        trl->seed = cmb_random_fmix64(master_seed, ui);
    }

    cimba_run(experiment, n_trials, sizeof(*experiment), run_trial);

    uint64_t events = 0u;
    struct cmb_datasummary dsu_esec;
    cmb_datasummary_initialize(&dsu_esec);
    struct cmb_datasummary dsu_nse;
    cmb_datasummary_initialize(&dsu_nse);
    for (unsigned ui = 0; ui < n_trials; ui++) {
        struct trial *trl = &experiment[ui];
        cmb_datasummary_add(&dsu_esec, trl->events_sec);
        cmb_datasummary_add(&dsu_nse, trl->ns_event);
        events += trl->n_events;
    }

    pthread_mutex_lock(&tty);
    printf ("%10" PRIu64 "                %8.3g                %8.5g\n",
             n_holding, cmb_datasummary_mean(&dsu_esec), cmb_datasummary_mean(&dsu_nse));
    pthread_mutex_unlock(&tty);

    cmb_datasummary_terminate(&dsu_esec);
    cmb_datasummary_terminate(&dsu_nse);
    free(experiment);

    return events;
}

int main(const int argc, char *argv[])
{
    uint64_t master_seed = cmb_random_hwseed();
    uint64_t n_samples = N_SAMPLES;
    uint64_t n_trials = N_TRIALS;
    uint64_t n_start = N_START;
    uint64_t n_lp = N_LP;
    uint64_t n_qlengths = N_QLENGTHS;
    double step_mult = STEP_MULT;
    double lookahead = LOOKAHEAD;
    double mean = MEAN;
    double p_remote = P_REMOTE;
    uint32_t n_threads = cmi_cpu_cores();

    int opt;
    while ((opt = getopt(argc, argv, "a:h:m:n:p:q:r:s:t:u:x:")) != -1) {
        switch (opt) {
            case 'a': {
                errno = 0;
                n_start = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'h': {
                errno = 0;
                lookahead = strtod(optarg, NULL);
                if (errno != 0 || lookahead < 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'm': {
                errno = 0;
                step_mult = strtod(optarg, NULL);
                if (errno != 0 || step_mult <= 0.0) {
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

            case 'p': {
                errno = 0;
                n_lp = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'q': {
                errno = 0;
                n_qlengths = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'r': {
                errno = 0;
                p_remote = strtod(optarg, NULL);
                if (errno != 0 || p_remote < 0.0 || p_remote > 1.0) {
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

            case 'u': {
                errno = 0;
                mean = strtod(optarg, NULL);
                if (errno != 0 || mean < 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'x': {
                errno = 0;
                n_samples = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || n_samples == 0u) {
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

    cmi_test_print_line("*");
    printf("***************                Cimba PHOLD test                ***************\n");
    cmi_test_print_line("*");
    printf("\nModel parameters:\n");
    printf("-----------------\n");
    printf("Number of logical processes: %" PRIu64 "\n", n_lp);
    printf("Probability of re-routing: %g\n", p_remote);
    printf("Holding time mean: %g lookahead: %g\n", mean, lookahead);
    printf("\nTest parameters:\n");
    printf("----------------\n");
    printf("Number of event queue levels: %" PRIu64 ", from %g to %g\n", n_qlengths,
            pow(10.0, (double)n_start), pow(10.0, (n_start + n_qlengths - 1) * step_mult));
    printf("Number of trials per level: %" PRIu64 "\n", n_trials);
    printf("Max samples per trial: %" PRIu64 "\n", n_samples);
    printf("Step multiplier: %4.2f\n", pow(10.0, step_mult));
    printf("Number of threads: %" PRIu32 "\n", n_threads);
    printf("Master seed: 0x%" PRIx64 "\n", master_seed);

    cmb_assert_release(mean - lookahead >= 0.0);

    cimba_threads_use(n_threads);

    printf("\nResults:\n");
    printf("--------\n");
    printf("Event queue size:     "
           "Events per second:    "
           "Nanoseconds per event:\n");

    uint64_t total_events = 0u;
    for (uint64_t ui = 0u; ui < n_qlengths; ui++) {
        const uint64_t holding = (uint64_t)pow(10.0, (double)((n_start + ui) * step_mult));
        total_events += run_level(holding, n_lp, n_trials, n_samples,
                            mean, lookahead, p_remote, master_seed);
    }

    printf("\nTotal number of events processed: %" PRIu64 "\n", total_events);

    return 0;
}
