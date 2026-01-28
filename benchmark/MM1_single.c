/*
 * Benchmark case: M/M/1 queue, stop after one million objects
 * Single-core version.
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

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#include <cimba.h>

#define NUM_OBJECTS 1000000u
#define ARRIVAL_RATE 0.9
#define SERVICE_RATE 1.0

CMB_THREAD_LOCAL struct cmi_mempool objectpool = {
    CMI_THREAD_STATIC,
    sizeof(void *),
    512u,
    0u, 0u, 0u, NULL, NULL
};

struct simulation {
    struct cmb_process *arrival;
    struct cmb_process *service;
    struct cmb_objectqueue *queue;
};

struct trial {
    double arr_mean;
    double srv_mean;
    uint64_t obj_cnt;
    double sum_wait;
    double avg_wait;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};

void *arrivalfunc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    const struct context *ctx = vctx;
    struct cmb_objectqueue *qp = ctx->sim->queue;
    const double mean_hld = ctx->trl->arr_mean;
    for (uint64_t ui = 0; ui < NUM_OBJECTS; ui++) {
        const double t_hld = cmb_random_exponential(mean_hld);
        cmb_process_hold(t_hld);
        void *object = cmi_mempool_alloc(&objectpool);
        double *dblp = object;
        *dblp = cmb_time();
        cmb_objectqueue_put(qp, object);
    }

    return NULL;
}

void *servicefunc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    const struct context *ctx = vctx;
    struct cmb_objectqueue *qp = ctx->sim->queue;
    const double mean_srv = ctx->trl->srv_mean;
    uint64_t *cnt = &(ctx->trl->obj_cnt);
    double *sum = &(ctx->trl->sum_wait);
    while (true) {
        void *object = NULL;
        cmb_objectqueue_get(qp, &object);
        const double *dblp = object;
        const double t_srv = cmb_random_exponential(mean_srv);
        cmb_process_hold(t_srv);
        *sum += cmb_time() - *dblp;
        *cnt += 1u;
        cmi_mempool_free(&objectpool, object);
    }
}

void run_trial(void *vtrl)
{
    struct trial *trl = vtrl;

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_random_initialize(cmb_random_hwseed());
    cmb_event_queue_initialize(0.0);
    struct context *ctx = malloc(sizeof(*ctx));
    ctx->trl = trl;
    struct simulation *sim = malloc(sizeof(*sim));
    ctx->sim = sim;

    sim->queue = cmb_objectqueue_create();
    cmb_objectqueue_initialize(sim->queue, "Queue", CMB_UNLIMITED);

    sim->arrival = cmb_process_create();
    cmb_process_initialize(sim->arrival, "Arrival", arrivalfunc, ctx, 0);
    cmb_process_start(sim->arrival);
    sim->service = cmb_process_create();
    cmb_process_initialize(sim->service, "Service", servicefunc, ctx, 0);
    cmb_process_start(sim->service);

    cmb_event_queue_execute();

    cmb_process_stop(sim->service, NULL);
    cmb_process_terminate(sim->arrival);
    cmb_process_terminate(sim->service);
    cmb_process_destroy(sim->arrival);
    cmb_process_destroy(sim->service);

    cmb_objectqueue_destroy(sim->queue);
    cmb_event_queue_terminate();
    free(sim);
    free(ctx);
}

int main(void)
{
    struct trial *trl = malloc(sizeof(*trl));
    trl->arr_mean = 1.0 / ARRIVAL_RATE;
    trl->srv_mean = 1.0 / SERVICE_RATE;
    trl->obj_cnt = 0u;
    trl->sum_wait = 0.0;
    run_trial(trl);

    printf("Average system time %f (expected %f)\n",
            trl->sum_wait / (double)trl->obj_cnt,
            1.0 / (SERVICE_RATE - ARRIVAL_RATE));

    return 0;
}
