/*
 * Test script for pthreads, a simple M/G/1 queuing system for parameterization
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

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_buffer.h"

#include "cmi_memutils.h"

#define USERFLAG 0x00000001

struct simulation {
    struct cmb_process *arrival;
    struct cmb_process *service;
    struct cmb_buffer *queue;
};

struct trial {
    double rho;
    double service_cv;
    double warmup;
    double duration;
    double cooldown;
    uint64_t seed;
    double avg_queuelength;
};

struct context {
    struct simulation *sim;
    struct trial *trial;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct context *ctx = subject;
    cmb_logger_info(stdout, "===> end_sim_evt <===");
    cmb_process_stop(ctx->sim->arrival, NULL);
    cmb_process_stop(ctx->sim->service, NULL);
    cmb_event_queue_clear();
}

static void start_rec_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct context *ctx = subject;
    cmb_buffer_start_recording(ctx->sim->queue);
}

static void stop_rec_evt(void *subject, void *object)
{
    cmb_unused(object);

    const struct context *ctx = subject;
    cmb_buffer_stop_recording(ctx->sim->queue);
}


void *arrivalfunc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_logger_user(USERFLAG, stdout, "Started arrival, queue %s", cmb_buffer_get_name(bp));
    cmb_assert_debug(ctx->trial->rho > 0.0);
    const double mean_interarr = 1.0 / ctx->trial->rho;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Holding");
        (void)cmb_process_hold(cmb_random_exponential(mean_interarr));
        cmb_logger_user(USERFLAG, stdout, "Arrival");
        uint64_t n = 1u;
        (void)cmb_buffer_put(bp, &n);
    }
}

void *servicefunc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    struct cmb_buffer *bp = ctx->sim->queue;
    cmb_logger_user(USERFLAG, stdout, "Started service, queue %s", cmb_buffer_get_name(bp));
    cmb_assert_debug(ctx->trial->service_cv > 0.0);
    const double cv = ctx->trial->service_cv;
    const double shape = 1.0 / (cv * cv);
    const double scale = cv * cv;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Holding shape %f scale %f", shape, scale);
        (void)cmb_process_hold(cmb_random_gamma(shape, scale));
        cmb_logger_user(USERFLAG, stdout, "Getting");
        uint64_t n = 1u;
        (void)cmb_buffer_get(bp, &n);
    }
}

void run_mg1(struct trial *trl)
{
    if (trl->seed == 0u) {
        const uint64_t seed = cmb_random_get_hwseed();
        cmb_random_initialize(seed);
        trl->seed = seed;
    }

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG);
    cmb_event_queue_initialize(0.0);

    struct context *ctx = cmi_malloc(sizeof(*ctx));
    ctx->trial = trl;

    struct simulation *sim = cmi_malloc(sizeof(*sim));
    ctx->sim = sim;

    ctx->sim->queue = cmb_buffer_create();
    cmb_buffer_initialize(ctx->sim->queue, "Queue", UINT64_MAX);

    ctx->sim->arrival = cmb_process_create();
    cmb_process_initialize(ctx->sim->arrival, "Arrivals", arrivalfunc, ctx, 0);
    cmb_process_start(ctx->sim->arrival);

    ctx->sim->service = cmb_process_create();
    cmb_process_initialize(ctx->sim->service, "Service", servicefunc, ctx, 0);
    cmb_process_start(ctx->sim->service);

    double t = trl->warmup;
    (void)cmb_event_schedule(start_rec_evt, ctx, NULL, t, 0);
    t += trl->duration;
    (void)cmb_event_schedule(stop_rec_evt, ctx, NULL, t, 0);
    t += trl->cooldown;
    (void)cmb_event_schedule(end_sim_evt, ctx, NULL, t, 0);

    cmb_event_queue_execute();
    cmb_buffer_print_report(ctx->sim->queue, stdout);

    const struct cmb_timeseries *tsp = cmb_buffer_get_history(ctx->sim->queue);
    struct cmb_wtdsummary ws;
    cmb_timeseries_summarize(tsp, &ws);
    trl->avg_queuelength = cmb_wtdsummary_mean(&ws);

    cmb_process_destroy(ctx->sim->arrival);
    cmb_process_destroy(ctx->sim->service);
    cmb_buffer_destroy(ctx->sim->queue);

    cmi_free(ctx->sim);
    cmi_free(ctx);
}

int main(void)
{
    const clock_t start_time = clock();

    struct trial *trl = cmi_malloc(sizeof(*trl));
    cmi_memset(trl, 0, sizeof(*trl));

    trl->rho = 0.9;
    trl->service_cv = 0.5;
    trl->warmup = 10.0;
    trl->duration = 1e6;
    trl->cooldown = 1.0;

    run_mg1(trl);

    cmi_free(trl);

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("It took: %f sec\n", elapsed_time);
    return 0;
}

