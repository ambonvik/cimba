/*
 * Test/demo program for parallel execution in Cimba.
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

extern uint32_t cmi_cpu_cores(void);

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

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG);
    cmb_event_queue_initialize(0.0);

    struct context *ctx = malloc(sizeof(*ctx));
    ctx->trl = trl;

    struct simulation *sim = malloc(sizeof(*sim));
    ctx->sim = sim;

    sim->queue = cmb_buffer_create();
    cmb_buffer_initialize(sim->queue, "Queue", UINT64_MAX);

    sim->arrival = cmb_process_create();
    cmb_process_initialize(sim->arrival, "Arrivals", arrival_proc, ctx, 0);
    cmb_process_start(sim->arrival);

    sim->service = cmb_process_create();
    cmb_process_initialize(sim->service, "Service", service_proc, ctx, 0);
    cmb_process_start(sim->service);

    double t = trl->warmup;
    (void)cmb_event_schedule(start_rec_evt, sim, NULL, t, 0);
    t += trl->duration;
    (void)cmb_event_schedule(stop_rec_evt, sim, NULL, t, 0);
    t += trl->cooldown;
    (void)cmb_event_schedule(end_sim_evt, sim, NULL, t, 0);

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

int main(void)
{
    const clock_t start_time = clock();

    const unsigned nreps = 10;
    const unsigned ncvs = 4;
    const double cvs[] = { 0.125, 0.25, 0.5, 1.0 };
    const unsigned nrhos = 5;
    const double rhos[] = { 0.4, 0.6, 0.8, 0.9, 0.95 };
    const double warmup = 10.0;
    const double duration = 1e6;
    const double cooldown = 1.0;

    const unsigned ntrials = nrhos * ncvs * nreps;
    printf("We have %u trials\n", ntrials);

    const uint32_t ncores = cmi_cpu_cores();
    printf("We have %u cores to play with\n", ncores);

    printf("Setting up experiment\n");
    struct trial *experiment = calloc(ntrials, sizeof(*experiment));
    uint64_t ui_exp = 0u;
    for (unsigned ui_cv = 0u; ui_cv < ncvs; ui_cv++) {
        for (unsigned ui_rho = 0u; ui_rho < nrhos; ui_rho++) {
            for (unsigned ui_rep = 0u; ui_rep < nreps; ui_rep++) {
                experiment[ui_exp].service_cv = cvs[ui_cv];
                experiment[ui_exp].utilization = rhos[ui_rho];
                experiment[ui_exp].warmup = warmup;
                experiment[ui_exp].duration = duration;
                experiment[ui_exp].cooldown = cooldown;
                experiment[ui_exp].seed = 0u;
                experiment[ui_exp].avg_queue_length = 0.0;
                ui_exp++;
            }
        }
    }

    printf("Executing experiment\n");
    cimba_run_experiment(experiment, ntrials, sizeof(*experiment), run_mg1_trial);

    printf("Done with experiment\n");
    ui_exp = 0u;
    for (unsigned ui_cv = 0u; ui_cv < ncvs; ui_cv++) {
         for (unsigned ui_rho = 0u; ui_rho < nrhos; ui_rho++) {
            for (unsigned ui_rep = 0u; ui_rep < nreps; ui_rep++) {
                printf("Trial %llu: seed 0x%llx CV %f rho %f L %f\n",
                    ui_exp,
                    experiment[ui_exp].seed,
                    experiment[ui_exp].service_cv,
                    experiment[ui_exp].utilization,
                    experiment[ui_exp].avg_queue_length);
                ui_exp++;
            }
        }
    }


    free(experiment);

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("It took: %f sec\n", elapsed_time);

    return 0;
}

