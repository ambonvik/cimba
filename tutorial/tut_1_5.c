#include <cimba.h>
#include <stdio.h>

#define USERFLAG1 0x00000001

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

    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);

    cmb_event_queue_initialize(0.0);

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

void load_params(struct trial *trlp)
{
    cmb_assert_release(trlp != NULL);

    trlp->arr_rate = 0.75;
    trlp->srv_rate = 1.0;
    trlp->warmup_time = 1000.0;
    trlp->duration = 1e6;
}

int main(void)
{
    struct trial trl = {};
    load_params(&trl);

    run_MM1_trial(&trl);

    printf("Avg %f\n", trl.avg_queue_length);

    return 0;
}
