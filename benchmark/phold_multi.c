/*
 * PHOLD performance test on event queue - parallel run as default
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

#define N_THREADS 64u
#define N_CORES 32u
#define N_REPLICATIONS 64u
#define N_LP 160u
#define N_HOLDING 100u
#define P_REMOTE 0.25
#define T_DURATION 20000.0
#define T_LOOKAHEAD 1.0
#define T_HOLD_MEAN 1.0
#define N_RATIOS 11u
#define T_FACTOR sqrt(2.0)

#define USERLEVEL 0x0001

static uint64_t total_events = 0u;
static double running_time = 0.0;
extern uint32_t cmi_cpu_cores();

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
    double t_duration;
    /* Controls */
    uint64_t index;
    uint64_t seed;
    bool dry_run;
    /* Results */
    double trial_sec;
    uint64_t n_events;
    uint32_t n_threads1;
    uint32_t n_threads2;
    double n_ev_s_core;
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
    const double t = cmb_time() + cmb_random_exponential(m) + lh;

    /* Where to route it next? */
    if (cmb_random_bernoulli(trl->p_remote) == 1u) {
        subject = (void *)cmb_random_discrete_uniform(sim->n_lp);
    }

    cmb_event_schedule(event, subject, object, t, 0);
}

static void end_event(void *subject, void *object)
{
    cmb_unused(subject);
    cmb_unused(object);

    cmb_event_queue_clear();
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

    if (trl->dry_run) {
        return;
    }

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
    const uint64_t me = trl->index;
    printf ("Trial %" PRIu64 " started\n", me);

    const uint64_t n_queue = trl->n_lp * n_holding;
    for (uint64_t ui = 0u; ui < n_queue; ui++) {
        const double t = cmb_time() + cmb_random_exponential(mean) + lh;
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
    cmb_event_schedule(end_event, NULL, NULL, cmb_time() + trl->t_duration, 0);

    trl->n_threads1 = cimba_threads_inuse();
    const double t0 = monotonic_seconds();
    cmb_event_queue_execute();
    const double t1 = monotonic_seconds();
    const double elapsed = t1 - t0;
    trl->n_threads2 = cimba_threads_inuse();

    uint64_t count = 0u;
    for (uint64_t ui = 0u; ui < trl->n_lp; ui++) {
        count += sim.lp_arr[ui];
    }

    trl->trial_sec = elapsed;
    trl->n_events = count;
    const double epsec = (double)count / elapsed;
    const double ntr = ((double)trl->n_threads1 + (double)trl->n_threads2) / 2.0;
    trl->n_ev_s_core = epsec * ntr / N_CORES;

    pthread_mutex_lock(&tty);
    printf ("Trial %" PRIu64 " finished in %f sec\n", me, elapsed);
    pthread_mutex_unlock(&tty);

    cmi_free(sim.lp_arr);
    cmb_random_terminate();
    cmb_event_queue_terminate();
}


void write_gnuplot_commands_lh(void);
void write_gnuplot_commands_tp(void);
double t_crit_95(uint32_t n);

int main(const int argc, char *argv[])
{
    uint64_t master_seed = cmb_random_hwseed();
    uint64_t n_holding = N_HOLDING;
    uint64_t n_reps = N_REPLICATIONS;
    uint64_t n_ratios = N_RATIOS;
    double t_factor = T_FACTOR;
    uint64_t n_lp = N_LP;
    double p_remote = P_REMOTE;
    double t_duration = T_DURATION;
    double t_hold_mean = T_HOLD_MEAN;
    double t_lookahead = T_LOOKAHEAD;
    uint32_t n_threads = N_THREADS;
    bool dry_run = false;

    int opt;
    while ((opt = getopt(argc, argv, "s:z")) != -1) {
        switch (opt) {
            case 's': {
                errno = 0;
                master_seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || master_seed == 0u) {
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
    printf("Number of logical processes: %" PRIu64 "\n", n_lp);
    printf("Number of holding queue entries per LP: %" PRIu64 "\n", n_holding);
    printf("Number of threads: %" PRIu32 "\n", n_threads);
    const uint64_t n_trials = 2u * n_ratios * n_reps;
    printf("Number of trials: %" PRIu64 "\n", n_trials);
    printf("Master seed: 0x%" PRIx64 "\n", master_seed);

    printf("\nConfiguring lookahead ratio sweep:\n");
    printf("Fixed probability of re-routing: %g\n", p_remote);
    printf("Fixed lookahead: %g\n", t_lookahead);
    printf("Varying mean hold time in %" PRIu64 " steps by factors of %g...\n\n", n_ratios, t_factor);

    struct trial *experiment = cmi_calloc(n_trials, sizeof(*experiment));
    uint64_t ui_exp = 0u;
    /* Interleave replications to avoid any run-sequence bias */
    for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
        double tm = t_hold_mean;
        for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
            experiment[ui_exp].n_holding = n_holding;
            experiment[ui_exp].n_lp = n_lp;
            experiment[ui_exp].hold_mean = tm;
            experiment[ui_exp].lookahead = t_lookahead;
            experiment[ui_exp].p_remote = p_remote;
            experiment[ui_exp].t_duration = t_duration * (tm + t_lookahead);

            experiment[ui_exp].seed = cmb_random_fmix64(master_seed, ui_exp);
            experiment[ui_exp].index = ui_exp;
            experiment[ui_exp].dry_run = dry_run;

            experiment[ui_exp].trial_sec = 0.0;
            experiment[ui_exp].n_events = 0u;
            experiment[ui_exp].n_ev_s_core = 0.0;

            tm *= t_factor;
            ui_exp++;
        }
    }

    printf("\nSetting up transfer probability sweep\n");
    printf("Fixed lookahead: %g\n", t_lookahead);
    printf("Fixed hold mean: %g\n", t_hold_mean);
    const double p_step = 1.0 / (n_ratios - 1u);
    printf("Varying transfer probability in %" PRIu64 " steps of %g...\n\n", n_ratios, p_step);

    /* Interleave replications to avoid any run-sequence bias */
    for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
        double pt = 0.0;
        for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
            experiment[ui_exp].n_holding = n_holding;
            experiment[ui_exp].n_lp = n_lp;
            experiment[ui_exp].hold_mean = t_hold_mean;
            experiment[ui_exp].lookahead = t_lookahead;
            experiment[ui_exp].p_remote = pt;
            experiment[ui_exp].t_duration = t_duration;

            experiment[ui_exp].seed = cmb_random_fmix64(master_seed, ui_exp);
            experiment[ui_exp].index = ui_exp;
            experiment[ui_exp].dry_run = dry_run;

            experiment[ui_exp].trial_sec = 0.0;
            experiment[ui_exp].n_events = 0u;
            experiment[ui_exp].n_ev_s_core = 0.0;

            pt += p_step;
            ui_exp++;
        }
    }

    cimba_threads_set(n_threads);
    double start_time = monotonic_seconds();
    cimba_run(experiment, n_trials, sizeof(*experiment), run_trial);
    double end_time = monotonic_seconds();
    running_time += end_time - start_time;

    FILE *datafp = fopen("phold_cimba_lh.dat", "w");
    fprintf(datafp, "# lookahead_ratio\tevents_sec_core\tconf_interval\n");

    struct cmb_datasummary *dstpcarr = cmi_calloc((size_t)n_ratios, sizeof(*dstpcarr));

    for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
        cmb_datasummary_initialize(&dstpcarr[ui_rat]);
    }

    ui_exp = 0u;
    for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
        for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
            const double ne = (double)experiment[ui_exp].n_events;
            total_events += ne;
            const double nepspc = experiment[ui_exp].n_ev_s_core;
            cmb_datasummary_add(&dstpcarr[ui_rat], nepspc);
            ui_exp++;
        }
    }

    double tm = t_hold_mean;
    for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
        const double rat = t_lookahead / tm;
        const double sample_avg = cmb_datasummary_mean(&dstpcarr[ui_rat]);
        const double sample_sd = cmb_datasummary_stddev(&dstpcarr[ui_rat]);
        const double sample_n = (double)cmb_datasummary_count(&dstpcarr[ui_rat]);
        const double t_crit = t_crit_95(n_reps - 1u);
        fprintf(datafp, "%f\t%f\t%f\n", rat, sample_avg, t_crit * sample_sd / sqrt(sample_n));

        printf("Mean %f lookahead %f ratio %f:\n", tm, t_lookahead, t_lookahead / tm);
        cmb_datasummary_print(&dstpcarr[ui_rat], stdout, false);
        cmb_datasummary_terminate(&dstpcarr[ui_rat]);
        tm *= t_factor;
    }

    fclose(datafp);

    write_gnuplot_commands_lh();
    if (system("gnuplot -persistent phold_cimba_lh.gp") != 0) {
        cmb_logger_warning(stderr, "gnuplot launch failed");
    }

    datafp = fopen("phold_cimba_tp.dat", "w");
    fprintf(datafp, "# transfer_probability\tevents_sec_core\tconf_interval\n");

    for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
        cmb_datasummary_initialize(&dstpcarr[ui_rat]);
     }

    for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
        for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
            const double ne = (double)experiment[ui_exp].n_events;
            total_events += ne;
            const double nepspc = experiment[ui_exp].n_ev_s_core;
            cmb_datasummary_add(&dstpcarr[ui_rat], nepspc);
            ui_exp++;
        }
    }

    for (unsigned ui_rat = 0u; ui_rat < n_ratios; ui_rat++) {
        const double pt = p_step * (double)ui_rat;
        const double sample_avg = cmb_datasummary_mean(&dstpcarr[ui_rat]);
        const double sample_sd = cmb_datasummary_stddev(&dstpcarr[ui_rat]);
        const double sample_n = (double)cmb_datasummary_count(&dstpcarr[ui_rat]);
        const double t_crit = t_crit_95(n_reps - 1u);
        fprintf(datafp, "%f\t%f\t%f\n", pt, sample_avg, t_crit * sample_sd / sqrt(sample_n));

        printf("Transfer prob %f:\n", pt);
        cmb_datasummary_print(&dstpcarr[ui_rat], stdout, false);
        cmb_datasummary_terminate(&dstpcarr[ui_rat]);
    }

    fclose(datafp);
    write_gnuplot_commands_tp();
    if (system("gnuplot -persistent phold_cimba_tp.gp") != 0) {
        cmb_logger_warning(stderr, "gnuplot launch failed");
    }

    cmi_free(dstpcarr);
    cmi_free(experiment);

    printf("Total events processed: %" PRIu64 "\n", total_events);
    printf("Running time: %g seconds\n", running_time);
    const double eps = (double)total_events / running_time;
    const double nspe = 1e9 / eps;
    printf("%g events per second, %g ns per event\n", eps, nspe);
    const unsigned ncores = cmi_cpu_cores() / 2u;
    printf("%g events per second per core\n", eps / (double)ncores);

    return 0;
}


void write_gnuplot_commands_lh(void)
{
    FILE *cmdfp = fopen("phold_cimba_lh.gp", "w");

    fprintf(cmdfp, "set terminal qt size 1200,700 enhanced font 'Arial,12'\n");
    fprintf(cmdfp, "set title \"Cimba PHold: Effect of lookahead/mean ratio\" font \"Times Bold, 18\" \n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set xlabel \"Lookahead ratio\"\n");
    fprintf(cmdfp, "set ylabel \"Events per second per core\"\n");
    fprintf(cmdfp, "set xrange [-0.1:1.1]\n");
    fprintf(cmdfp, "set yrange [0:*]\n");
    fprintf(cmdfp, "datafile = 'phold_cimba_lh.dat'\n");
    fprintf(cmdfp, "plot datafile with yerrorbars lc rgb \"black\"\n");

    fclose(cmdfp);
}

void write_gnuplot_commands_tp(void)
{
    FILE *cmdfp = fopen("phold_cimba_tp.gp", "w");

    fprintf(cmdfp, "set terminal qt size 1200,700 enhanced font 'Arial,12'\n");
    fprintf(cmdfp, "set title \"Cimba PHold: Effect of transfer probability\" font \"Times Bold, 18\" \n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set xlabel \"Transfer probability\"\n");
    fprintf(cmdfp, "set ylabel \"Events per second per core\"\n");
    fprintf(cmdfp, "set xrange [-0.1:1.1]\n");
    fprintf(cmdfp, "set yrange [0:*]\n");
    fprintf(cmdfp, "datafile = 'phold_cimba_tp.dat'\n");
    fprintf(cmdfp, "plot datafile with yerrorbars lc rgb \"black\"\n");

    fclose(cmdfp);
}

/*
 * Table lookup for the critical values for two-sided 95 % confidence intervals,
 * see https://www.stat.purdue.edu/~lfindsen/stat503/t-Dist.pdf
 */
double t_crit_95(const uint32_t n)
{
    cmb_assert_debug(n > 0u);

#define NUM_TVALS 62u
    static uint32_t n_vals[NUM_TVALS] = {
        1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
       11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
       21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
       31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
       41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
       60, 70, 80, 90,100,120,140,180,200,500,
       1000, UINT32_MAX
   };

    static double t_vals[NUM_TVALS] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
         2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
         2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042,
         2.040, 2.037, 2.035, 2.032, 2.030, 2.028, 2.026, 2.024, 2.023, 2.021,
         2.020, 2.018, 2.017, 2.015, 2.014, 2.013, 2.012, 2.011, 2.010, 2.009,
         2.000, 1.994, 1.990, 1.987, 1.984, 1.980, 1.977, 1.973, 1.972, 1.965,
         1.962, 1.960
    };

    for (uint32_t ui = 0u; ui < NUM_TVALS; ui++) {
        if (n_vals[ui] == n) {
            return t_vals[ui];
        }

        if (n_vals[ui] > n) {
            /* Interpolate between values */
            const uint32_t n_range = n_vals[ui] - n_vals[ui - 1];
            const double t_range = t_vals[ui] - t_vals[ui - 1];
            const double frac = (double)(n - n_vals[ui - 1]) / (double)n_range;
            cmb_assert_debug(frac >= 0.0 && frac <= 1.0);
            const double t_ret = t_vals[ui - 1u] + frac * t_range;

            cmb_assert_debug((t_ret <= t_vals[ui - 1u]) && (t_ret >= t_vals[ui]));
            return t_ret;
        }
    }

    /* Should never get this far */
    cmb_logger_error(stderr, "Critical value lookup failed");
#undef NUM_TVALS
}

