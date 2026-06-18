/*
 * Test/demo program for parallel execution in Cimba.
 * Usage:
 *      test_cimba [-s <seed>][-g][-t]
 *
 * The simulation is a simple M/G/1 queuing system for parameterization
 * of utilization (interarrival mean time) and variability (service time
 * standard deviation). Holding mean service time constant at 1.0, inter-
 * arrival times exponentially distributed (c.v. = 1.0)
 *
 * Terminology:
 *  - Simulation   The simulated universe with the processes and objects in it.
 *  - Trial        A set of parameters and results for a simulation.
 *  - Replication  A trial with the same parameters as another.
 *  - Experiment   A set of trials according to some experimental design.
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
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cimba.h"
#include "test.h"

#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

/*
 * Define the entities that make up our simulated world.
 */
struct simulation {
    struct cmb_process *arrival;
    struct cmb_process *service;
    struct cmb_buffer *queue;
};

/*
 * Define the parameters that we would like to vary and the results that
 * interest us as an outcome of a single trial. Use several trials with
 * identical parameters (but different seeds) to perform replications.
 */
struct trial {
    /* Parameters */
    double service_cv;
    double utilization;
    double start_time;
    double warmup_s;
    double duration_s;
    double cooldown_s;
    uint64_t seed;
    /* Outcome */
    double avg_queue_length;
};

/*
 * The complete context for running a trial in this simulation.
 */
struct context {
    struct simulation *sim;
    struct trial *trl;
};

/*
 * Define the event to stop the simulation at the end of a trial.
 */
static void end_sim_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_unused(object);

    cmb_logger_info(stdout, "===> end_sim_evt <===");

    const struct simulation *sim = subject;
    cmb_assert_always(sim->arrival != NULL);
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_RUNNING);
    int r = cmb_process_stop(sim->arrival, NULL);
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_FINISHED);


    cmb_assert_always(sim->service != NULL);
    cmb_assert_always(cmb_process_status(sim->service) == CMB_PROCESS_RUNNING);
    r = cmb_process_stop(sim->service, NULL);
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_FINISHED);

    cmb_assert_always(cmb_event_queue_is_empty());
}

/*
 * Define the event to start recording statistics after the warm-up period (if any).
 */
static void start_rec_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_buffer_recording_start(sim->queue);
    cmb_assert_always(sim->queue->is_recording);
}

/*
 * Define the event to stop recording statistics after the trial is complete.
 */
static void stop_rec_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_buffer_recording_stop(sim->queue);
    cmb_assert_always(!(sim->queue->is_recording));
}

/*
 * Define the simulated arrival process putting new items into the queue at
 * random intervals.
 */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_assert_always(bp != NULL);
    cmb_logger_user(stdout,
                    USERFLAG1,
                    "Started arrival, queue %s",
                    cmb_buffer_get_name(bp));
    cmb_assert_always(ctx->trl->utilization > 0.0);
    const double mean_interarr = 1.0 / ctx->trl->utilization;

    while (true) {
        cmb_logger_user(stdout, USERFLAG1, "Holding");
        const double ht = cmb_random_exponential(mean_interarr);
        cmb_assert_always(ht >= 0.0);
        (void)cmb_process_hold(ht);
        cmb_logger_user(stdout, USERFLAG1, "Arrival");
        uint64_t n = 1u;
        const int64_t r = cmb_buffer_put(bp, &n);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(n == 0u);
    }
}

/*
 * Define the simulated service process getting items from the queue and
 * servicing them for a random duration.
 */
void *service_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_assert_always(bp != NULL);
    cmb_logger_user(stdout,
                    USERFLAG1,
                    "Started service, queue %s",
                    cmb_buffer_get_name(bp));

    const double cv = ctx->trl->service_cv;
    cmb_assert_always(cv > 0.0);
    const double shape = 1.0 / (cv * cv);
    const double scale = cv * cv;

    while (true) {
        cmb_logger_user(stdout,
                        USERFLAG1,
                        "Holding gamma shape %f scale %f",
                        shape,
                        scale);
        const double ht = cmb_random_gamma(shape, scale);
        cmb_assert_always(ht >= 0.0);
        int64_t r = cmb_process_hold(ht);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);

        /* Test occasional trial failures from process coroutine level */
        const double pfail = 1e-7;
        if (cmb_random_bernoulli(pfail)) {
            /* Pretend that this trial failed for some reason, bail out.
             * Note that memory will leak here, abandoning the coroutine.    */
            const uint64_t tid = cimba_thread_id();
            const uint64_t tix = cimba_trial_index();
            cmb_logger_error(stdout,
                             "Trial %" PRIu64 " failed in worker %" PRIu64,
                             tix, tid);
            /* Not reached */
        }

        cmb_logger_user(stdout, USERFLAG1, "Getting");
        uint64_t n = 1u;
        r = cmb_buffer_get(bp, &n);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(n == 1u);
   }
}

/*
 * Our trial function, setting up the simulation, obtaining trial parameters
 */
void run_mg1_trial(void *vtrl)
{
    cmb_assert_always(vtrl != NULL);

    struct trial *trl = vtrl;

    /* Start from an empty event queue. The simulation clock will not be
     * initialized to its correct starting value before this call. Any error
     * or warning messages may show the wrong timestamp until properly
     * initialized. We do that first, and we start from non-zero here
     * because we can. */
    cmb_assert_always(cmb_process_current() == NULL);
    cmb_assert_always(cmb_time() == 0.0);
    cmb_event_queue_initialize(trl->start_time);
    cmb_assert_always(cmb_time() == trl->start_time);

    /* Any error or warning messages will contain the pseudo-random number seed,
     * again not initialized before this call. So we do that too. */
    cmb_random_initialize(trl->seed);

    struct context *ctx = malloc(sizeof(*ctx));
    cmb_assert_always(ctx != NULL);
    ctx->trl = trl;

    struct simulation *sim = malloc(sizeof(*sim));
    cmb_assert_always(sim != NULL);
    ctx->sim = sim;

    cmb_logger_user(stdout, USERFLAG2, "Started, seed 0x%" PRIx64, trl->seed);

    /* Test occasional trial failures from trial function level */
    const double pfail = 0.05;
    if (cmb_random_bernoulli(pfail)) {
        /* Pretend that this trial failed for some reason, bail out.
         * Note that it is the caller's responsibility to free any allocated
         * memory before calling cmb_logger_error()    */
        free(sim);
        free(ctx);
        const uint64_t tid = cimba_thread_id();
        const uint64_t tix = cimba_trial_index();
        cmb_logger_error(stdout,
                         "Trial %" PRIu64 " failed in worker %" PRIu64,
                         tix, tid);
        /* Not reached */
    }

    /* Set the data collection period */
    double t = trl->start_time + trl->warmup_s;
    uint64_t ev_hdle = cmb_event_schedule(start_rec_evt, sim, NULL, t, 0);
    cmb_assert_always(ev_hdle != 0u);
    t += trl->duration_s;
    ev_hdle = cmb_event_schedule(stop_rec_evt, sim, NULL, t, 0);
    cmb_assert_always(ev_hdle != 0u);
    t += trl->cooldown_s;
    ev_hdle = cmb_event_schedule(end_sim_evt, sim, NULL, t, 0);
    cmb_assert_always(ev_hdle != 0u);

    /* Create the simulation entities */
    sim->queue = cmb_buffer_create();
    cmb_assert_always(sim->queue != NULL);
    cmb_buffer_initialize(sim->queue, "Queue", UINT64_MAX);
    cmb_assert_always(cmb_buffer_level(sim->queue) == 0u);

    sim->arrival = cmb_process_create();
    cmb_assert_always(sim->arrival != NULL);
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim->arrival, "Arrivals", arrival_proc, ctx, 0);
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_CREATED);
    /* Non-blocking, just schedules the start event to run when we yield from here */
    cmb_process_start(sim->arrival);
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_CREATED);

    sim->service = cmb_process_create();
    cmb_assert_always(sim->service != NULL);
    cmb_assert_always(cmb_process_status(sim->service) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim->service, "Service", service_proc, ctx, 0);
    cmb_assert_always(cmb_process_status(sim->service) == CMB_PROCESS_CREATED);
    cmb_process_start(sim->service);
    cmb_assert_always(cmb_process_status(sim->service) == CMB_PROCESS_CREATED);

    /* Execute the trial */
    cmb_event_queue_execute();

    /* Collect and save statistics into the trial struct */
    const struct cmb_timeseries *tsp = cmb_buffer_history(sim->queue);
    cmb_assert_always(tsp != NULL);
    struct cmb_wtdsummary ws;
    cmb_wtdsummary_initialize(&ws);
    cmb_timeseries_summarize(tsp, &ws);
    trl->avg_queue_length = cmb_wtdsummary_mean(&ws);

    /* Clean up */

    cmb_logger_user(stdout, USERFLAG2, "Finished");
    cmb_assert_always(cmb_process_status(sim->arrival) == CMB_PROCESS_FINISHED);
    cmb_process_terminate(sim->arrival);
    cmb_process_destroy(sim->arrival);
    cmb_assert_always(cmb_process_status(sim->service) == CMB_PROCESS_FINISHED);
    cmb_process_terminate(sim->service);
    cmb_process_destroy(sim->service);
    cmb_buffer_destroy(sim->queue);
    free(sim);
    free(ctx);

    cmb_event_queue_terminate();
    cmb_random_terminate();
}

/* Declare for later use, do not want to digress with that here */
void write_gnuplot_commands(unsigned ncvs, const double *cvs);

/* Testing the cimba_set_init_func and _exit_func */
struct thread_context {
    pthread_t thread_id;
    void *usrarg;
    uint64_t tid;
};

void *thread_init_func(const uint64_t tid, void *usrarg)
{
    /* Create a context object for the Cimba worker thread */
    struct thread_context *ctx = malloc(sizeof *ctx);
    cmb_assert_always(ctx != NULL);
    ctx->thread_id = pthread_self();
    ctx->usrarg = usrarg;
    ctx->tid = tid;

    /* Use the thread hook mechanism to set logger flags */
    const uint32_t logflagsoff = *((uint32_t *)usrarg);
    cmb_logger_flags_off(logflagsoff);

    return ctx;
}

void thread_exit_func(void *vctx)
{
    cmb_assert_always(vctx != NULL);

    /* Delete the context object */
    struct thread_context *ctx = vctx;
    free(ctx);
}

/*
 * Our main() function, loading the experiment and reporting the outcome.
 */
int main(const int argc, char **argv)
{
    bool plot_graphics = false;
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 1.0e6;
    double wup = 1.0e-3;
    uint32_t nthr = 0u;

    int opt;
    while ((opt = getopt(argc, argv, "d:gr:s:tw:")) != -1) {
        switch (opt) {
            case 'd': {
                errno = 0;
                dur = strtod(optarg, NULL);
                if (errno != 0 || dur <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            }
            case 'g': {
                plot_graphics = true;
                break;
            }
            case 'r': {
                errno = 0;
                nthr = (uint32_t)strtoul(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                (void)cimba_threads_use(nthr);
                break;
            }
            case 's': {
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            }
            case 't': {
                timing_enabled = true;
                break;
            }
            case 'w': {
                wup = strtod(optarg, NULL);
                break;
            }
            default: {
                fprintf(stderr, "Usage: %s [-d <duration>][-g][-r <runner_threads>][-s <seed>][-t][-w <warmup_period>]\n", argv[0]);

                return EXIT_FAILURE;
            }
        }
    }

    cmi_test_print_line("*");
    printf("*************************   Testing trial execution   **************************\n");
    cmi_test_print_line("*");
    printf("Cimba version %s\n", cimba_version());
    printf("Using seed: 0x%" PRIx64 "\n", seed);
    struct timespec start_time;
    if (timing_enabled) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }

    /* Experiment design parameters */
    const unsigned nreps = 10;
    const unsigned ncvs = 4;
    const double cvs[] = { 0.01, 0.5, 2.0, 4.0 };
    const unsigned nrhos = 5;
    const double rhos[] = { 0.4, 0.6, 0.8, 0.9, 0.95 };

    printf("Setting up experiment\n");
    const unsigned ntrials = nrhos * ncvs * nreps;
    struct trial *experiment = calloc(ntrials, sizeof(*experiment));
    uint64_t ui_exp = 0u;
    for (unsigned ui_cv = 0u; ui_cv < ncvs; ui_cv++) {
        for (unsigned ui_rho = 0u; ui_rho < nrhos; ui_rho++) {
            for (unsigned ui_rep = 0u; ui_rep < nreps; ui_rep++) {
                experiment[ui_exp].service_cv = cvs[ui_cv];
                experiment[ui_exp].utilization = rhos[ui_rho];
                experiment[ui_exp].start_time = 1.0e6;
                experiment[ui_exp].warmup_s = wup;
                experiment[ui_exp].duration_s = dur;
                experiment[ui_exp].cooldown_s = 1.0;
                experiment[ui_exp].seed = cmb_random_fmix64(seed, ui_exp);
                /* Sentinel initial value to catch any failed trials */
                experiment[ui_exp].avg_queue_length = -1.0;
                ui_exp++;
            }
        }
    }

    /* Stash a copy for possible later use */
    struct trial *experiment_single = calloc(ntrials, sizeof(*experiment_single));
    cmb_assert_always(sizeof(*experiment) == sizeof(*experiment_single));
    cmi_memcpy(experiment_single, experiment, ntrials * sizeof(*experiment));

    printf("Baiting thread hooks\n");
    uint32_t logflagsoff = CMB_LOGGER_INFO | USERFLAG1;
    cimba_thread_hooks_set(thread_init_func, &logflagsoff, thread_exit_func);

    printf("Running experiment\n");
    cmi_test_print_line("-");
    const uint64_t nfail = cimba_run(experiment,
                                     ntrials,
                                     sizeof(*experiment),
                                     run_mg1_trial);
    cmi_test_print_line("-");

    if (cimba_threads_num() != 1u) {
        /* We were running multithreaded, check that we get the exact same outcome single-threaded */
        printf("Validating experiment ...");
        fflush(stdout);
        cimba_threads_use(1);
        /* Turn off all logging, including error messages. cimba_run() has joined
         * all threads that read the previous value, will soon start new worker
         * threads, tell them to turn off logging by storing a new value here. */
        logflagsoff = 0xFFFFFFFF;
        const uint64_t rs = cimba_run(experiment_single, ntrials, sizeof(*experiment_single), run_mg1_trial);
        cmb_assert_always(rs == nfail);
        for (uint64_t i = 0; i < ntrials; i++) {
            /* Bitwise comparison of the per-trial outcome, independent of execution sequence */
            cmb_assert_always(memcmp(&experiment[i], &experiment_single[i], sizeof(*experiment)) == 0);
        }

        cimba_threads_use(nthr);
        printf("done\n");
    }

    free(experiment_single);

    printf("Experiment finished, %" PRIu64 " failed trials\n", nfail);
    if (plot_graphics) {
        printf("Writing results to file\n");
        ui_exp = 0u;
        FILE *datafp = fopen("test_cimba.dat", "w");
        fprintf(datafp, "# CV utilization avg_queue_length\n");
        for (unsigned ui_cv = 0u; ui_cv < ncvs; ui_cv++) {
            for (unsigned ui_rho = 0u; ui_rho < nrhos; ui_rho++) {
                for (unsigned ui_rep = 0u; ui_rep < nreps; ui_rep++) {
                    if (experiment[ui_exp].avg_queue_length != -1.0) {
                        /* Trial did not fail, valid result */
                        fprintf(datafp, "%f %f %f\n",
                           experiment[ui_exp].service_cv,
                           experiment[ui_exp].utilization,
                           experiment[ui_exp].avg_queue_length);
                    }
                    ui_exp++;
                }
                fprintf(datafp, "\n");
            }
            fprintf(datafp, "\n");
        }

        fclose(datafp);
    }
    else {
        printf("Results:\n");
        ui_exp = 0u;
        printf("cv: \trho:\tn_avg:\n");
        for (unsigned ui_cv = 0u; ui_cv < ncvs; ui_cv++) {
            const double cv = experiment[ui_exp].service_cv;
            for (unsigned ui_rho = 0u; ui_rho < nrhos; ui_rho++) {
                const double rho = experiment[ui_exp].utilization;
                double sum = 0.0;
                unsigned nval = 0u;
                for (unsigned ui_rep = 0u; ui_rep < nreps; ui_rep++) {
                    if (experiment[ui_exp].avg_queue_length != -1.0) {
                        /* Trial did not fail, valid result */
                        nval++;
                        sum += experiment[ui_exp].avg_queue_length;
                    }
                    ui_exp++;
                }
                if (nval > 0u) {
                    const double avg = sum / (double)nval;
                    printf("%5.3f\t%5.3f\t%5.3f\n", cv, rho, avg);
                }
                else {
                    printf("%5.3f\t%5.3f\t-\n", cv, rho);
                }
            }
        }
    }

    free(experiment);

    struct timespec end_time;
    if (timing_enabled) {
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed = (double)(end_time.tv_sec - start_time.tv_sec);
        elapsed += (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
        printf("It took %g sec\n", elapsed);
    }

    if (plot_graphics) {
        /* Pop up the Gnuplot graphics window before exiting */
        write_gnuplot_commands(ncvs, cvs);
        if (system("gnuplot -persistent test_cimba.gp") != 0) {
            cmb_logger_warning(stderr, "gnuplot launch failed");
        }
    }

    cmi_test_print_line("*");
    return 0;
}

void write_gnuplot_commands(const unsigned ncvs, const double *cvs)
{
    cmb_assert_release(ncvs == 4u);
    cmb_assert_release(cvs != NULL);

    FILE *cmdfp = fopen("test_cimba.gp", "w");
    fprintf(cmdfp, "set terminal qt size 1200,1000 enhanced font 'Arial,12'\n");
    fprintf(cmdfp, "set multiplot layout 2,2 rowsfirst \\\n");
    fprintf(cmdfp, "title \"Impact of service time variability in M/G/1 queue\" \\\n");
    fprintf(cmdfp, "margins 0.1, 0.95, 0.1, 0.9 spacing 0.1, 0.15\n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set xlabel \"System utilization (rho)\"\n");
    fprintf(cmdfp, "set ylabel \"Avg queue length\"\n");
    fprintf(cmdfp, "set xrange [0.0:1.0]\n");
    fprintf(cmdfp, "set yrange [0:100]\n");
    fprintf(cmdfp, "f(x) = x**2 / (1.0 - x)\n");
    fprintf(cmdfp, "datafile = 'test_cimba.dat'\n");
    fprintf(cmdfp, "plot datafile using 2:3 index 0 with points title \"cv = %g\" lc rgb \"black\", \\\n", cvs[0]);
    fprintf(cmdfp, "        f(x) title \"M/M/1\" with lines lw 2 lc rgb \"gray\"\n");
    fprintf(cmdfp, "plot datafile using 2:3 index 1 with points title \"cv = %g\" lc rgb \"black\", \\\n", cvs[1]);
    fprintf(cmdfp, "        f(x) title \"M/M/1\" with lines lw 2 lc rgb \"gray\"\n");
    fprintf(cmdfp, "plot datafile using 2:3 index 2 with points title \"cv = %g\" lc rgb \"black\", \\\n", cvs[2]);
    fprintf(cmdfp, "        f(x) title \"M/M/1\" with lines lw 2 lc rgb \"gray\"\n");
    fprintf(cmdfp, "plot datafile using 2:3 index 3 with points title \"cv = %g\" lc rgb \"black\", \\\n", cvs[3]);
    fprintf(cmdfp, "        f(x) title \"M/M/1\" with lines lw 2 lc rgb \"gray\"\n");
    fprintf(cmdfp, "unset multiplot\n");
    fclose(cmdfp);
}