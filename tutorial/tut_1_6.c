#include <cimba.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

struct simulation {
    struct cmb_process *arr;
    struct cmb_buffer *que;
    struct cmb_process *srv;
};

struct trial {
    /* Parameters */
    double arr_rate;
    double srv_rate;
    double warmup_time;
    double duration;
    /* Results */
    uint64_t seed_used;
    double avg_queue_length;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};

void end_sim(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;
    cmb_logger_user(stdout, USERFLAG1, "--- Game Over ---");
    cmb_process_stop(sim->arr, NULL);
    cmb_process_stop(sim->srv, NULL);
}

static void start_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;
    cmb_buffer_start_recording(sim->que);
}

static void stop_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;
    cmb_buffer_stop_recording(sim->que);
}


void *arrivals(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;
    const struct trial *trl = ctx->trl;
    struct cmb_buffer *que = sim->que;

    cmb_assert_debug(trl->arr_rate > 0.0);
    const double t_ia_mean = 1.0 / trl->arr_rate;

    while (true) {
        const double t_ia = cmb_random_exponential(t_ia_mean);
        cmb_logger_user(stdout, USERFLAG1, "Holds for %f time units", t_ia);
        cmb_process_hold(t_ia);
        uint64_t n = 1;
        cmb_logger_user(stdout, USERFLAG1, "Puts one into the queue");
        cmb_buffer_put(que, &n);
    }
}

void *service(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);

    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;
    const struct trial *trl = ctx->trl;
    struct cmb_buffer *que = sim->que;

    cmb_assert_debug(trl->srv_rate > 0.0);
    const double t_srv_mean = 1.0 / trl->srv_rate;

    while (true) {
        uint64_t m = 1;
        cmb_logger_user(stdout, USERFLAG1, "Gets one from the queue");
        cmb_buffer_get(que, &m);
        const double t_srv = cmb_random_exponential(t_srv_mean);
        cmb_logger_user(stdout, USERFLAG1, "Got one, services it for %f time units", t_srv);
        cmb_process_hold(t_srv);
    }
}

void run_MM1_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trl = vtrl;

    struct context ctx = {};
    struct simulation sim = {};
    ctx.sim = &sim;
    ctx.trl = trl;

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);
    trl->seed_used = cmb_random_get_hwseed();
    cmb_random_initialize(trl->seed_used);
    cmb_logger_user(stdout, USERFLAG2,
                    "seed: 0x%016" PRIx64 " rho: %f",
                    trl->seed_used, trl->arr_rate / trl->srv_rate);

    ctx.sim->que = cmb_buffer_create();
    cmb_buffer_initialize(ctx.sim->que, "Queue", CMB_BUFFER_UNLIMITED);

    ctx.sim->arr = cmb_process_create();
    cmb_process_initialize(ctx.sim->arr, "Arrivals", arrivals, &ctx, 0);
    cmb_process_start(ctx.sim->arr);

    ctx.sim->srv = cmb_process_create();
    cmb_process_initialize(ctx.sim->srv, "Service", service, &ctx, 0);
    cmb_process_start(ctx.sim->srv);

    double t = trl->warmup_time;
    cmb_event_schedule(start_rec, NULL, &ctx, t, 0);
    t += trl->duration;
    cmb_event_schedule(stop_rec, NULL, &ctx, t, 0);
    cmb_event_schedule(end_sim, NULL, &ctx, t, -100);

    cmb_event_queue_execute();

    struct cmb_wtdsummary wtdsum;
    const struct cmb_timeseries *ts = cmb_buffer_get_history(ctx.sim->que);
    cmb_timeseries_summarize(ts, &wtdsum);
    ctx.trl->avg_queue_length = cmb_wtdsummary_mean(&wtdsum);

    cmb_process_terminate(ctx.sim->srv);
    cmb_process_destroy(ctx.sim->srv);

    cmb_process_terminate(ctx.sim->arr);
    cmb_process_destroy(ctx.sim->arr);

    cmb_buffer_terminate(ctx.sim->que);
    cmb_buffer_destroy(ctx.sim->que);

    cmb_event_queue_terminate();
    cmb_random_terminate();
}

void write_gnuplot_commands(void);

int main(void)
{
    printf("Cimba version %s\n", cimba_version());
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    const unsigned n_rhos = 39;
    const double rho_start = 0.025;
    const double rho_step = 0.025;
    const unsigned n_reps = 10;

    const double srv_rate = 1.0;
    const double warmup_time = 1000.0;
    const double duration = 1.0e6;

    printf("Setting up experiment\n");
    const unsigned n_trials = n_rhos * n_reps;
    struct trial *experiment = calloc(n_trials, sizeof(*experiment));

    uint64_t ui_exp = 0u;
    double rho = rho_start;
    for (unsigned ui_rho = 0u; ui_rho < n_rhos; ui_rho++) {
        for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
            experiment[ui_exp].arr_rate = rho * srv_rate;
            experiment[ui_exp].srv_rate = srv_rate;
            experiment[ui_exp].warmup_time = warmup_time;
            experiment[ui_exp].duration = duration;
            experiment[ui_exp].seed_used = 0u;
            experiment[ui_exp].avg_queue_length = 0.0;

            ui_exp++;
        }

        rho += rho_step;
    }

    printf("Executing experiment\n");
    cimba_run_experiment(experiment, n_trials, sizeof(*experiment), run_MM1_trial);

    printf("Finished experiment, writing results to file\n");
    ui_exp = 0u;
    FILE *datafp = fopen("tut_1_6.dat", "w");
    fprintf(datafp, "# utilization\tavg_queue_length\tconf_interval\n");
    for (unsigned ui_rho = 0u; ui_rho < n_rhos; ui_rho++) {
        const double ar = experiment[ui_exp].arr_rate;
        const double sr = experiment[ui_exp].srv_rate;
        const double rho_used = ar / sr;

        struct cmb_datasummary cds;
        cmb_datasummary_initialize(&cds);
        for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
            cmb_datasummary_add(&cds, experiment[ui_exp].avg_queue_length);
            ui_exp++;
        }

        cmb_assert_debug(cmb_datasummary_count(&cds) == n_reps);
        const double sample_avg = cmb_datasummary_mean(&cds);
        const double sample_sd = cmb_datasummary_stddev(&cds);
        const double t_crit = 2.228;
        fprintf(datafp, "%f\t%f\t%f\n", rho_used, sample_avg, t_crit * sample_sd);
        cmb_datasummary_terminate(&cds);
    }

    fclose(datafp);
    free(experiment);

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (double)(end_time.tv_sec - start_time.tv_sec);
    elapsed += (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    printf("It took %g sec\n", elapsed);

    write_gnuplot_commands();
    system("gnuplot -persistent tut_1_6.gp");

    return 0;
}

void write_gnuplot_commands(void)
{
    FILE *cmdfp = fopen("tut_1_6.gp", "w");

    fprintf(cmdfp, "set terminal qt size 1200,700 enhanced font 'Arial,12'\n");
    fprintf(cmdfp, "set title \"Impact of utilization in M/M/1 queue\" font \"Times Bold, 18\" \n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set xlabel \"System utilization (rho)\"\n");
    fprintf(cmdfp, "set ylabel \"Avg queue length\"\n");
    fprintf(cmdfp, "set xrange [0.0:1.0]\n");
    fprintf(cmdfp, "set yrange [0:50]\n");
    fprintf(cmdfp, "f(x) = x**2 / (1.0 - x)\n");
    fprintf(cmdfp, "datafile = 'tut_1_6.dat'\n");
    fprintf(cmdfp, "plot datafile with yerrorbars lc rgb \"black\", \\\n");
    fprintf(cmdfp, "        f(x) title \"M/M/1\" with lines lw 2 lc rgb \"gray\"\n");

    fclose(cmdfp);
}