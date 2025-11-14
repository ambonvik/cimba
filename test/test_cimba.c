/*
 * Test/demo program for parallel execution in Cimba.
 *
 * The simulation is a simple M/G/1 queuing system for parameterization
 * of utilization (interarrival mean time) and variability (service time
 * standard deviation). Holding mean service time constant at 1.0, inter-
 * arrival times exponentially distributed (c.v. = 1.0)
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

struct simulation {
    struct cmb_process *arrival;
    struct cmb_process *service;
    struct cmb_buffer *queue;
};

struct trial {
    double service_cv;
    double utilization;
    double warmup;
    double duration;
    double cooldown;
    uint64_t seed;
    double avg_queue_length;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_logger_info(stdout, "===> end_sim_evt <===");
    cmb_process_stop(sim->arrival, NULL);
    cmb_process_stop(sim->service, NULL);
    cmb_event_queue_clear();
}

static void start_rec_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_buffer_start_recording(sim->queue);
}

static void stop_rec_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_buffer_stop_recording(sim->queue);
}


void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_logger_user(USERFLAG, stdout, "Started arrival, queue %s", cmb_buffer_get_name(bp));
    const double mean_interarr = 1.0 / ctx->trl->utilization;

    while (true) {
        cmb_logger_user(USERFLAG, stdout, "Holding");
        (void)cmb_process_hold(cmb_random_exponential(mean_interarr));
        cmb_logger_user(USERFLAG, stdout, "Arrival");
        uint64_t n = 1u;
        (void)cmb_buffer_put(bp, &n);
    }
}

void *service_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_logger_user(USERFLAG, stdout, "Started service, queue %s", cmb_buffer_get_name(bp));
    const double cv = ctx->trl->service_cv;
    const double shape = 1.0 / (cv * cv);
    const double scale = cv * cv;

    while (true) {
        cmb_logger_user(USERFLAG, stdout, "Holding shape %f scale %f", shape, scale);
        (void)cmb_process_hold(cmb_random_gamma(shape, scale));
        cmb_logger_user(USERFLAG, stdout, "Getting");
        uint64_t n = 1u;
        (void)cmb_buffer_get(bp, &n);
    }
}

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

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG);
    cmb_event_queue_initialize(0.0);

    double t = trl->warmup;
    (void)cmb_event_schedule(start_rec_evt, sim, NULL, t, 0);
    t += trl->duration;
    (void)cmb_event_schedule(stop_rec_evt, sim, NULL, t, 0);
    t += trl->cooldown;
    (void)cmb_event_schedule(end_sim_evt, sim, NULL, t, 0);

    sim->queue = cmb_buffer_create();
    cmb_buffer_initialize(sim->queue, "Queue", UINT64_MAX);

    sim->arrival = cmb_process_create();
    cmb_process_initialize(sim->arrival, "Arrivals", arrival_proc, ctx, 0);
    cmb_process_start(sim->arrival);

    sim->service = cmb_process_create();
    cmb_process_initialize(sim->service, "Service", service_proc, ctx, 0);
    cmb_process_start(sim->service);

    cmb_event_queue_execute();
//    cmb_buffer_print_report(sim->queue, stdout);

    const struct cmb_timeseries *tsp = cmb_buffer_get_history(sim->queue);
    struct cmb_wtdsummary ws;
    cmb_timeseries_summarize(tsp, &ws);
    trl->avg_queue_length = cmb_wtdsummary_mean(&ws);

    cmb_event_queue_terminate();
    cmb_process_destroy(sim->arrival);
    cmb_process_destroy(sim->service);
    cmb_buffer_destroy(sim->queue);

    free(sim);
    free(ctx);
}

void write_gnuplot_commands(unsigned ncvs, const double *cvs);

int main(void)
{
    const clock_t start_time = clock();

    const unsigned nreps = 10;
    const unsigned ncvs = 4;
    const double cvs[] = { 0.01, 0.5, 2.0, 4.0 };
    const unsigned nrhos = 5;
    const double rhos[] = { 0.4, 0.6, 0.8, 0.9, 0.95 };

    const unsigned ntrials = nrhos * ncvs * nreps;
    printf("Setting up experiment\n");
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

    printf("Finished experiment\n");
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
    printf("It took: %f sec\n", elapsed_time);

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