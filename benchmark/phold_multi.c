/*
 * PHOLD performance test on event queue - sequential run as default
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

#define N_THREADS 1u                /* Override with -t command line option */
#define N_TRIALS 1u                 /* Override with -n command line option */
#define N_LP 32u                    /* Override with -p command line option */
#define N_START 1u                  /* Override with -a command line option */
#define N_QLENGTHS 49u              /* Override with -q command line option */
#define STEP_MULT 0.125             /* Override with -m command line option */
#define LOOKAHEAD 1.0               /* Override with -h command line option */
#define MEAN 2.0                    /* Override with -u command line option */
#define P_REMOTE 0.25               /* Override with -r command line option */
#define N_MEASURE 10000000          /* Override with -x command line option */

#define USERLEVEL 0x0001

struct simulation {
    uint64_t n_lp;
    uint64_t *lp_arr;
};

struct trial {
    /* Parameters */
    uint64_t n_holding;
    uint64_t n_lp;
    double hold_mean;
    double lookahead;
    double p_remote;
    uint64_t n_measure;
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
    cmb_assert_debug(subject != NULL);

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

    struct simulation sim = { 0 };
    sim.n_lp = trl->n_lp;
    sim.lp_arr = cmi_calloc((size_t)sim.n_lp, sizeof(*(sim.lp_arr)));
    struct context ctx = { 0 };
    ctx.sim = &sim;
    ctx.trl = trl;

    cmb_event_queue_initialize(0.0);
    cmb_random_initialize(trl->seed);

    /* Initial population */
    const uint64_t n_holding = trl->n_holding;
    const double mean = trl->hold_mean;
    const double lh = trl->lookahead;
    const uint64_t n_queue = trl->n_lp * n_holding;
    for (uint64_t ui = 0u; ui < n_queue; ui++) {
        const double t = (mean > lh) ? cmb_random_exponential(mean - lh) + lh : lh;
        const uint64_t lp = cmb_random_discrete_uniform(sim.n_lp);
        cmb_event_schedule(event, (void *)lp, &ctx, t, 0);
    }

    /* Warmup */
    const uint64_t n_warm = (n_queue > 1000u) ? n_queue : 1000u;
    for (uint64_t ui = 0u; ui < n_warm; ui++) {
        cmb_event_execute_next();
    }

    /* Zero all event counters before the real run */
    cmi_memset(sim.lp_arr, 0, (size_t)sim.n_lp * sizeof(*sim.lp_arr));

    const uint64_t n_measure = trl->n_measure;
    const double t0 = monotonic_seconds();
    for (uint64_t ui = 0u; ui < n_measure; ui++) {
        cmb_event_execute_next();
    }
    const double t1 = monotonic_seconds();

    uint64_t count = 0u;
    for (uint64_t ui = 0u; ui < trl->n_lp; ui++) {
        count += sim.lp_arr[ui];
    }

    const double elapsed = t1 - t0;
    const double eps = (double)count / elapsed;
    trl->events_sec = eps;
    trl->ns_event = 1e9 / eps;
    trl->n_events = count;

    cmi_free(sim.lp_arr);
    cmb_random_terminate();
    cmb_event_queue_terminate();
}

static uint64_t run_level(const uint64_t n_lp,
                          const uint64_t n_holding,
                          const uint64_t n_trials,
                          const uint64_t n_measure,
                          const double mean,
                          const double lookahead,
                          const double p_remote,
                          const uint64_t master_seed,
                          const bool dry_run)
{
    struct trial *experiment = cmi_calloc(n_trials, sizeof(*experiment));

    for (unsigned ui = 0; ui < n_trials; ui++) {
        struct trial *trl = &experiment[ui];
        trl->n_holding = n_holding;
        trl->n_measure = n_measure;
        trl->n_lp = n_lp;
        trl->hold_mean = mean;
        trl->p_remote = p_remote;
        trl->lookahead = lookahead;
        trl->seed = cmb_random_fmix64(master_seed, ui);
    }

    pthread_mutex_lock(&tty);
    printf ("%9" PRIu64 "\t"
            "%9" PRIu64 "\t"
            "%9" PRIu64 "\t",
             n_lp, n_holding, n_lp * n_holding);
    fflush(stdout);
    pthread_mutex_unlock(&tty);

    if (!dry_run) {
        cimba_run(experiment, n_trials, sizeof(*experiment), run_trial);
    }

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
    printf ("%10.3g\t%10.5g\n",
             cmb_datasummary_mean(&dsu_esec),
             cmb_datasummary_mean(&dsu_nse));
    pthread_mutex_unlock(&tty);

    cmb_datasummary_terminate(&dsu_esec);
    cmb_datasummary_terminate(&dsu_nse);
    free(experiment);

    return events;
}

int main(const int argc, char *argv[])
{
    uint64_t master_seed = cmb_random_hwseed();
    uint64_t n_trials = N_TRIALS;
    uint64_t n_measure = N_MEASURE;
    uint64_t n_start = N_START;
    uint64_t n_lp = N_LP;
    uint64_t n_qlengths = N_QLENGTHS;
    double step_mult = STEP_MULT;
    double lookahead = LOOKAHEAD;
    double mean = MEAN;
    double p_remote = P_REMOTE;
    uint32_t n_threads = N_THREADS;
    bool dry_run = false;

    int opt;
    while ((opt = getopt(argc, argv, "a:h:m:n:p:q:r:s:t:u:x:z")) != -1) {
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
                n_measure = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'z': {
                dry_run = true;
                break;
            }

            default: {
                return EXIT_FAILURE;
            }
        }
    }

    printf("******************************************************************************\n");
    printf("***************                Cimba PHOLD test                ***************\n");
    printf("******************************************************************************\n");

    printf("\nModel parameters:\n");
    printf("-----------------\n");
    printf("Probability of re-routing: %g\n", p_remote);
    printf("Holding time mean: %g lookahead: %g\n", mean, lookahead);

    printf("\nTest parameters, varying queue length per logical process:\n");
    printf("----------------------------------------------------------\n");
    printf("Number of logical processes: %" PRIu64 "\n", n_lp);
    printf("Number of event queue levels: %" PRIu64 ", from %" PRIu64 " to %" PRIu64 "\n",
           n_qlengths,   (uint64_t)n_start, (uint64_t)(n_start * pow(10.0, (n_qlengths - 1u) * step_mult)));
    printf("Number of trials per level: %" PRIu64 "\n", n_trials);
    printf("Number of measured events: %" PRIu64 "\n", n_measure);
    printf("Step multiplier: %4.2f\n", pow(10.0, step_mult));
    printf("Number of threads: %" PRIu32 "\n", n_threads);
    printf("Master seed: 0x%" PRIx64 "\n", master_seed);

    cmb_assert_release(mean - lookahead >= 0.0);

    cimba_threads_use(n_threads);

    printf("\nResults:\n");
    printf("--------\n");
    printf("LPs:\t"
           "initevs/LP:\t"
           "totevs:\t"
           "evs/s:\t"
           "ns/ev:\n");

    uint64_t total_events = 0u;
    uint64_t prev = 0u;
    for (uint64_t ui = 0u; ui < n_qlengths; ui++) {
        const uint64_t qevs = (uint64_t)((double)n_start * pow(10.0, (double)(ui) * step_mult));
        if ((qevs == 0u) || (qevs == prev)) {
            continue;
        }

        total_events += run_level(n_lp, qevs, n_trials, n_measure,
                            mean, lookahead, p_remote, master_seed, dry_run);
        prev = qevs;
    }

    printf("\nTest parameters, varying number of logical processes:\n");
    printf("-----------------------------------------------------\n");
    const uint64_t qevs = 1000u;
    printf("Number of events in queue per logical process: %" PRIu64 "\n", qevs);
    uint64_t start_lp = (uint64_t)((double)n_start * pow(10.0, (double)(0u) * step_mult)) / qevs;;
    if (start_lp == 0u) {
        start_lp = 1u;
    }

    uint64_t end_lp = (uint64_t)((double)n_lp * pow(10.0, (double)(n_qlengths - 1u) * step_mult)) / qevs;;
    printf("Number of logical process counts: %" PRIu64 ", from %" PRIu64 " to %" PRIu64 "\n",
            n_qlengths, start_lp, end_lp);
    printf("Number of trials per level: %" PRIu64 "\n", n_trials);
    printf("Number of measured events: %" PRIu64 "\n", n_measure);
    printf("Number of threads: %" PRIu32 "\n", n_threads);
    printf("Master seed: 0x%" PRIx64 "\n", master_seed);

    printf("\nResults:\n");
    printf("--------\n");
    printf("LPs:\t"
           "initevs/LP:\t"
           "totevs:\t"
           "evs/s:\t"
           "ns/ev:\n");

    total_events = 0u;
    prev = 0u;
    for (uint64_t ui = 0u; ui < n_qlengths; ui++) {
        /* Same values as first sweep */
        const uint64_t tevs = n_lp * (uint64_t)((double)n_start * pow(10.0, (double)(ui) * step_mult));
        /* ... but by varying the number of lp's instead */
        const uint64_t lps = tevs / qevs;
        if ((lps == 0u) || (lps == prev)) {
            continue;
        }

        total_events += run_level(lps, qevs, n_trials, n_measure,
                            mean, lookahead, p_remote, master_seed, dry_run);
        prev = lps;
    }

    printf("\nTotal number of events processed: %" PRIu64 "\n", total_events);


    return 0;
}
