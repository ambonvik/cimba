/*
 * Test/demo program for parallel execution in Cimba.
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
 * Copyright (c) Asbjørn M. Bonvik 2025.
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

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cimba.h"

#define USERFLAG 0x00000001

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
    double service_cv;
    double utilization;
    double warmup;
    double duration;
    double cooldown;
    uint64_t seed;
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
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_logger_info(stdout, "===> end_sim_evt <===");
    cmb_process_stop(sim->arrival, NULL);
    cmb_process_stop(sim->service, NULL);
    cmb_event_queue_clear();
}

/*
 * Define the event to start recording statistics after warm-up period (if any).
 */
static void start_rec_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_buffer_start_recording(sim->queue);
}

/*
 * Define the event to stop recording statistics after the trial is complete.
 */
static void stop_rec_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_buffer_stop_recording(sim->queue);
}

/*
 * Define the simulated arrival process putting new items into the queue at
 * random intervals.
 */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_logger_user(USERFLAG, stdout, "Started arrival, queue %s",
                    cmb_buffer_get_name(bp));
    const double mean_interarr = 1.0 / ctx->trl->utilization;

    while (true) {
        cmb_logger_user(USERFLAG, stdout, "Holding");
        (void)cmb_process_hold(cmb_random_exponential(mean_interarr));
        cmb_logger_user(USERFLAG, stdout, "Arrival");
        uint64_t n = 1u;
        (void)cmb_buffer_put(bp, &n);
    }
}

/*
 * Define the simulated service process getting items from the queue and
 * servicing them for a random duration.
 */
void *service_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_logger_user(USERFLAG, stdout, "Started service, queue %s",
                    cmb_buffer_get_name(bp));

    const double cv = ctx->trl->service_cv;
    const double shape = 1.0 / (cv * cv);
    const double scale = cv * cv;

    while (true) {
        cmb_logger_user(USERFLAG, stdout, "Holding shape %f scale %f",
                        shape, scale);
        (void)cmb_process_hold(cmb_random_gamma(shape, scale));
        cmb_logger_user(USERFLAG, stdout, "Getting");
        uint64_t n = 1u;
        (void)cmb_buffer_get(bp, &n);
    }
}

/*
 * Our trial function, setting up the simulation, obtaining trial parameters,
 */
void run_mg1_trial(void *vtrl)
{
    struct trial *trl = vtrl;
    if (trl->seed == 0u) {
        const uint64_t seed = cmb_random_get_hwseed();
        cmb_random_initialize(seed);
        trl->seed = seed;
    }

    struct context *ctx = malloc(sizeof(*ctx));
    ctx->trl = trl;

    struct simulation *sim = malloc(sizeof(*sim));
    ctx->sim = sim;

    /* Do not disturb, except for significant warnings and errors */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG);

    /* Start from an empty event queue */
    cmb_event_queue_initialize(0.0);

    /* Set the data collection period */
    double t = trl->warmup;
    (void)cmb_event_schedule(start_rec_evt, sim, NULL, t, 0);
    t += trl->duration;
    (void)cmb_event_schedule(stop_rec_evt, sim, NULL, t, 0);
    t += trl->cooldown;
    (void)cmb_event_schedule(end_sim_evt, sim, NULL, t, 0);

    /* Create the simulation entities */
    sim->queue = cmb_buffer_create();
    cmb_buffer_initialize(sim->queue, "Queue", UINT64_MAX);

    sim->arrival = cmb_process_create();
    cmb_process_initialize(sim->arrival, "Arrivals", arrival_proc, ctx, 0);
    cmb_process_start(sim->arrival);

    sim->service = cmb_process_create();
    cmb_process_initialize(sim->service, "Service", service_proc, ctx, 0);
    cmb_process_start(sim->service);

    /* Execute the trial */
    cmb_event_queue_execute();

    /* Collect and save statistics into the trial struct */
    const struct cmb_timeseries *tsp = cmb_buffer_get_history(sim->queue);
    struct cmb_wtdsummary ws;
    cmb_timeseries_summarize(tsp, &ws);
    trl->avg_queue_length = cmb_wtdsummary_mean(&ws);

    /* Clean up */
    cmb_event_queue_terminate();

    cmb_process_destroy(sim->arrival);
    cmb_process_destroy(sim->service);
    cmb_buffer_destroy(sim->queue);

    free(sim);
    free(ctx);
}

/* Declare for later use, do not want to digress with that here */
void write_gnuplot_commands(unsigned ncvs, const double *cvs);

/*
 * Our main() function, loading the experiment and reporting the outcome.
 */
int main(void)
{
    printf("Cimba version %s\n", cimba_version());
    const clock_t start_time = clock();

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
                experiment[ui_exp].warmup = 1000.0;
                experiment[ui_exp].duration = 1.0e6;
                experiment[ui_exp].cooldown = 1.0;
                experiment[ui_exp].seed = 0u;
                experiment[ui_exp].avg_queue_length = 0.0;
                ui_exp++;
            }
        }
    }

    printf("Executing experiment\n");
    cimba_run_experiment(experiment, ntrials, sizeof(*experiment), run_mg1_trial);

    printf("Finished experiment, writing results to file\n");
    ui_exp = 0u;
    FILE *datafp = fopen("test_cimba.dat", "w");
    fprintf(datafp, "# CV utilization avg_queue_length\n");
    for (unsigned ui_cv = 0u; ui_cv < ncvs; ui_cv++) {
        for (unsigned ui_rho = 0u; ui_rho < nrhos; ui_rho++) {
             for (unsigned ui_rep = 0u; ui_rep < nreps; ui_rep++) {
                 fprintf(datafp, "%f %f %f\n",
                    experiment[ui_exp].service_cv,
                    experiment[ui_exp].utilization,
                    experiment[ui_exp].avg_queue_length);
                 ui_exp++;
             }
             fprintf(datafp, "\n");
        }
        fprintf(datafp, "\n");
    }

    fclose(datafp);
    free(experiment);

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("It took %g sec\n", elapsed_time);

    /* ...and pop up the graphics window before exiting */
    write_gnuplot_commands(ncvs, cvs);
    system("gnuplot -persistent test_cimba.gp");

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
    fprintf(cmdfp, "f(x) = x / (1.0 - x)\n");
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