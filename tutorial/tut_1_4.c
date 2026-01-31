#include <cimba.h>
#include <stdio.h>

#define USERFLAG1 0x00000001

struct simulation {
    struct cmb_process *arr;
    struct cmb_buffer *que;
    struct cmb_process *srv;
};

void end_sim(void *subject, void *object)
{
    struct simulation *sim = object;
    cmb_logger_user(stdout, USERFLAG1, "--- Game Over ---");
    cmb_process_stop(sim->arr, NULL);
    cmb_process_stop(sim->srv, NULL);
}

void *arrival(struct cmb_process *me, void *ctx)
{
    struct cmb_buffer *bp = ctx;
    while (true) {
        const double rate = 0.75;
        const double mean = 1.0 / rate;
        const double t_ia = cmb_random_exponential(mean);
        cmb_logger_user(stdout, USERFLAG1, "Holds for %f time units", t_ia);
        cmb_process_hold(t_ia);
        uint64_t n = 1;
        cmb_logger_user(stdout, USERFLAG1, "Puts one into the queue");
        cmb_buffer_put(bp, &n);
    }
}

void *service(struct cmb_process *me, void *ctx)
{
    struct cmb_buffer *bp = ctx;
    while (true) {
        const double rate = 1.0;
        const double mean = 1.0 / rate;
        uint64_t m = 1;
        cmb_logger_user(stdout, USERFLAG1, "Gets one from the queue");
        cmb_buffer_get(bp, &m);
        double t_srv = cmb_random_exponential(mean);
        cmb_logger_user(stdout, USERFLAG1, "Got one, services it for %f time units", t_srv);
        cmb_process_hold(t_srv);
    }
}

int main(void)
{
    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);

    cmb_event_queue_initialize(0.0);

    struct simulation sim = {};
    sim.que = cmb_buffer_create();
    cmb_buffer_initialize(sim.que, "Queue", CMB_UNLIMITED);
    cmb_buffer_recording_start(sim.que);

    sim.arr = cmb_process_create();
    cmb_process_initialize(sim.arr, "Arrival", arrival, sim.que, 0);
    cmb_process_start(sim.arr);

    sim.srv = cmb_process_create();
    cmb_process_initialize(sim.srv, "Service", service, sim.que, 0);
    cmb_process_start(sim.srv);

    cmb_event_schedule(end_sim, NULL, &sim, 1e6, 0);
    cmb_event_queue_execute();

    cmb_buffer_recording_stop(sim.que);
    cmb_buffer_print_report(sim.que, stdout);

    struct cmb_timeseries *ts = cmb_buffer_history(sim.que);
    double pacf_arr[21];
    cmb_timeseries_PACF(ts, 20, pacf_arr, NULL);
    cmb_timeseries_correlogram_print(ts, stdout, 20, pacf_arr);

    cmb_process_terminate(sim.srv);
    cmb_process_destroy(sim.srv);

    cmb_process_terminate(sim.arr);
    cmb_process_destroy(sim.arr);

    cmb_buffer_terminate(sim.que);
    cmb_buffer_destroy(sim.que);

    cmb_event_queue_terminate();
    cmb_random_terminate();

    return 0;
}
