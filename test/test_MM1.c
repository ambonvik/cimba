/*
 * M/M/1 queue test case for goodness-of-fit of queue length and time-in-system
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
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <cimba.h>

#include "cmi_mempool.h"
#include "testutils.h"

#define ARRIVAL_RATE 0.9
#define SERVICE_RATE 1.0
#define DURATION_RTS 100
#define NUM_TRIALS 1000
#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

/* Fast memory pool of recycling objects */
CMB_THREAD_LOCAL struct cmi_mempool objectpool = CMI_MEMPOOL_STATIC_INIT(16u, 512u);

struct simulation {
    struct cmb_objectqueue *queue;
    struct cmb_process *arrival;
    struct cmb_process *service;
};

struct trial {
    /* Parameters */
    double arr_mean_s;
    double srv_mean_s;
    double duration_s;
    uint64_t target_cust;
    uint64_t trial_seed;
    /* Status */
    uint64_t customers_in_service;
    bool has_custinsys;
    bool has_timeinsys;
    /* Results, queue length and time in system of last customer when ending */
    uint64_t customers_in_system;
    double time_in_system;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};

struct customer {
    uint64_t ssn;
    double arrival_time;
};

static void end_sim_evnt(void *subject, void *object)
{
    cmb_unused(subject);

    struct context *ctx = object;
    struct simulation *sim = ctx->sim;
    cmb_logger_user(stdout, USERFLAG1, "Ending");

    cmb_process_stop(sim->arrival, NULL);
    cmb_process_stop(sim->service, NULL);
}

static void log_qlen_evnt(void *subject, void *object)
{
    cmb_unused(subject);

    struct context *ctx = object;
    struct simulation *sim = ctx->sim;
    cmb_logger_user(stdout, USERFLAG1, "Logging");

    struct trial *trl = ctx->trl;
    trl->customers_in_system = cmb_objectqueue_length(sim->queue)
                               + trl->customers_in_service;

    trl->has_custinsys = true;
    if (trl->has_timeinsys == true) {
        cmb_event_schedule(end_sim_evnt, NULL, ctx, cmb_time(), 0);
    }
}


static void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    const struct context *ctx = vctx;
    struct cmb_objectqueue *qp = ctx->sim->queue;
    const double mean_hld = ctx->trl->arr_mean_s;
    uint64_t cust_count = 0u;
    while (true) {
        const double t_hld = cmb_random_exponential(mean_hld);
        cmb_logger_user(stdout, USERFLAG1, "Holds for %f time units", t_hld);
        cmb_process_hold(t_hld);
        struct customer *cp = cmi_mempool_alloc(&objectpool);
        cp->ssn = ++cust_count;
        cp->arrival_time= cmb_time();
        cmb_logger_user(stdout, USERFLAG1, "Puts one into the queue");
        cmb_objectqueue_put(qp, cp);
    }

    return NULL;
}

static void *service_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    struct context *ctx = vctx;
    struct trial *trl = ctx->trl;
    struct cmb_objectqueue *qp = ctx->sim->queue;
    const double mean_srv = trl->srv_mean_s;
    while (true) {
        trl->customers_in_service = 0u;
        cmb_logger_user(stdout, USERFLAG1, "Gets one from the queue");
        void *vp = NULL;
        cmb_objectqueue_get(qp, &vp);
        trl->customers_in_service = 1u;
        struct customer *cp = vp;
        const double t_srv = cmb_random_exponential(mean_srv);
        cmb_logger_user(stdout, USERFLAG1,
                        "Holds customer %" PRIu64 " for %f time units",
                        cp->ssn, t_srv);
        cmb_process_hold(t_srv);
        if (cp->ssn == trl->target_cust) {
            trl->time_in_system = cmb_time() - cp->arrival_time;
            trl->has_timeinsys = true;
            if (trl->has_custinsys == true) {
                cmb_event_schedule(end_sim_evnt, NULL, ctx, cmb_time(), 0);
            }
        }

        cmi_mempool_free(&objectpool, vp);
    }

    return NULL;
}

static void run_trial(void *vtrl)
{
    struct trial *trl = vtrl;

    cmb_logger_flags_off(CMB_LOGGER_INFO | USERFLAG1 | USERFLAG2);
    cmb_event_queue_initialize(0.0);
    cmb_random_initialize(trl->trial_seed);
    cmb_logger_user(stdout, USERFLAG2, "seed: 0x%016" PRIx64 " rho: %f",
                    trl->trial_seed, trl->srv_mean_s / trl->arr_mean_s);

    struct context *ctx = malloc(sizeof(*ctx));
    ctx->trl = trl;
    struct simulation *sim = malloc(sizeof(*sim));
    ctx->sim = sim;

    sim->queue = cmb_objectqueue_create();
    cmb_objectqueue_initialize(sim->queue, "Queue", CMB_UNLIMITED);

    sim->arrival = cmb_process_create();
    cmb_process_initialize(sim->arrival, "Arrival", arrival_proc, ctx, 0);
    cmb_process_start(sim->arrival);

    sim->service = cmb_process_create();
    cmb_process_initialize(sim->service, "Service", service_proc, ctx, 0);
    cmb_process_start(sim->service);

    cmb_event_schedule(log_qlen_evnt, NULL, ctx, cmb_time() + trl->duration_s, 0);
    cmb_event_queue_execute();

    cmb_process_terminate(sim->arrival);
    cmb_process_destroy(sim->arrival);
    cmb_process_terminate(sim->service);
    cmb_process_destroy(sim->service);

    cmb_objectqueue_terminate(sim->queue);
    cmb_objectqueue_destroy(sim->queue);

    cmb_event_queue_terminate();
    cmb_random_terminate();
    free(sim);
    free(ctx);
}

static double plus_one(const double x, void *ctx)
{
    cmb_unused(ctx);

    return x + 1.0;
}

static void test_geometric(const struct cmb_dataset *dsp, const double p)
{
    struct cmb_dataset ds = { 0 };
    cmb_dataset_initialize(&ds);
    cmi_test_transform(&ds, dsp, plus_one, NULL);

    double pg_vec[22] = { 0 };
    double vg_vec[22] = { 0 };

    pg_vec[0] = 0.0;
    vg_vec[0] = 0.0;

    double q = 1.0 - p;
    double pg_tmp = p;
    for (unsigned ui = 1u; ui <= 20u; ui++) {
        pg_vec[ui] = pg_tmp;
        vg_vec[ui] = (double)ui;
        pg_tmp *= q;
    }

    pg_vec[21] = pow(1.0 - p, 20.0);
    vg_vec[21] = 20.0 + 1.0 / p;

    struct cmi_test_outcome result = { 0 };
    cmi_test_gof_disc(20u, pg_vec, vg_vec, &ds, &result);
    cmi_test_outcome_print(&result, stdout);
    // cmb_assert_always((result.status == CMI_TEST_OK) && (fabs(result.combined_sigma) < 6.0));

    cmb_dataset_terminate(&ds);
}

struct cdf_exp_params {
    double m;
};

static double cdf_exp(const double x, void *ctx)
{
    cmb_unused(ctx);
    cmb_assert_debug(ctx != NULL);
    const struct cdf_exp_params *pp = ctx;

    const double r = 1.0 - exp(-x / pp->m);

    return r;
}

static void test_exponential(const struct cmb_dataset *dsp, const double m)
{
    struct cdf_exp_params pp = { .m = m };
    struct cmi_test_outcome result = { 0 };
    cmi_test_gof_cont(cdf_exp, &pp, dsp, &result);
    cmi_test_outcome_print(&result, stdout);
    // cmb_assert_always((result.status == CMI_TEST_OK) && (fabs(result.combined_sigma) < 6.0));
}


int main(const int argc, char *argv[])
{
    /* Can be set on command line */
    uint64_t master_seed = cmb_random_hwseed();
    uint64_t n_reps = NUM_TRIALS;
    /* Multiple of relaxation time, can be set on command line */
    double duration_rts = DURATION_RTS;
    double arrival_mean = 1.0 / ARRIVAL_RATE;
    double service_mean = 1.0 / SERVICE_RATE;

    /* Parse command line options, if any */
    int opt;
    while ((opt = getopt(argc, argv, "d:n:s:r:")) != -1) {
        switch (opt) {
            case 'd': {
                errno = 0;
                duration_rts = strtod(optarg, NULL);
                if (errno != 0 || duration_rts <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }
            case 'n': {
                errno = 0;
                n_reps = (uint32_t)strtoul(optarg, NULL, 0);
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
            case 'r': {
                errno = 0;
                const double rhoarg = (double)strtod(optarg, NULL);
                if (errno != 0 || rhoarg <= 0.0 || rhoarg >= 1.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    return EXIT_FAILURE;
                }
                arrival_mean = 1.0 / rhoarg;
                break;
            }
            default: {
                fprintf(stderr, "Usage: %s [-d <duration>][-n <num_replications>][-s <seed>][-r <utilization>]\n", argv[0]);
                return EXIT_FAILURE;
            }
        }
    }

    printf("Cimba version %s\n", cimba_version());
    printf("Master seed: 0x%" PRIx64 "\n", master_seed);
    printf("Average inter-arrival time: %f, average service time: %f\n", arrival_mean, service_mean);
    const double rho = service_mean / arrival_mean;
    printf("Utilization: %f\n", rho);
    const double tmp = 1.0 - sqrt(rho);
    const double relaxation_s =  1.0  / (tmp * tmp);
    const double duration_s = duration_rts * relaxation_s;
    printf("Relaxation time: %g\n", relaxation_s);
    printf("Duration until collecting number of customers in system: %f\n", duration_s);
    const uint64_t tgt_cust = (uint64_t)(duration_s / arrival_mean);
    printf("Target customer for collecting time in system ssn %" PRIu64 "\n", tgt_cust);
    printf("Running %" PRIu64 " trials...\n", n_reps);

    struct trial *experiment = calloc(n_reps, sizeof(*experiment));
    for (unsigned ui = 0; ui < n_reps; ui++) {
        struct trial *trl = &experiment[ui];
        trl->trial_seed = cmb_random_fmix64(master_seed, ui);
        trl->arr_mean_s = arrival_mean;
        trl->srv_mean_s = service_mean;
        trl->duration_s = duration_s;
        trl->target_cust = tgt_cust;
        trl->customers_in_service = 0u;
        trl->customers_in_system = 0u;
        trl->time_in_system = 0.0;
        trl->has_custinsys = false;
        trl->has_timeinsys = false;
    }

    cimba_run(experiment, n_reps, sizeof(*experiment), run_trial);

    struct cmb_dataset ds_quelen = { 0 };
    cmb_dataset_initialize(&ds_quelen);
    struct cmb_dataset ds_sojourns = { 0 };
    cmb_dataset_initialize(&ds_sojourns);

    for (unsigned ui = 0; ui < n_reps; ui++) {
        /* Just grab the latest values */
        const double tsys = experiment[ui].time_in_system;
        cmb_dataset_add(&ds_sojourns, tsys);
        const uint64_t qlen = experiment[ui].customers_in_system;
        cmb_dataset_add(&ds_quelen, (double)qlen);
    }

    free(experiment);

    const double p = 1.0 - rho;
    struct cmb_datasummary dsu = { 0 };
    cmb_datasummary_initialize(&dsu);
    cmb_dataset_summarize(&ds_quelen, &dsu);
    cmi_test_print_line("-");
    printf("Queue lengths:\n");
    cmb_datasummary_print(&dsu, stdout, true);
    cmb_dataset_histogram_print(&ds_quelen, stdout, 20, 0.0, 0.0);
    printf("Assumed shifted geometric distribution, p = %f, testing...\n", p);
    test_geometric(&ds_quelen, p);

    const double m = 1.0 / (1.0 / service_mean - 1.0 / arrival_mean);
    cmb_datasummary_reset(&dsu);
    cmb_dataset_summarize(&ds_sojourns, &dsu);
    cmi_test_print_line("-");
    printf("Time in system:\n");
    cmb_datasummary_print(&dsu, stdout, true);
    cmb_dataset_histogram_print(&ds_sojourns, stdout, 20, 0.0, 0.0);
    printf("Assumed exponential distribution, m = %f,  testing...\n", m);
    test_exponential(&ds_sojourns, m);

    cmb_datasummary_terminate(&dsu);
    cmb_dataset_terminate(&ds_quelen);
    cmb_dataset_terminate(&ds_sojourns);

    return 0;
}
