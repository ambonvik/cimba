#include <cimba.h>

void *arrivals(struct cmb_process *me, void *ctx)
{
    struct cmb_buffer *bp = ctx;
    while (true) {
        double t_ia = cmb_random_exponential(1.0 / 0.75);
        cmb_process_hold(t_ia);
        uint64_t n = 1;
        cmb_buffer_put(bp, &n);
    }
}

void *service(struct cmb_process *me, void *ctx)
{
    struct cmb_buffer *bp = ctx;
    while (true) {
        uint64_t m = 1;
        cmb_buffer_get(bp, &m);
        double t_srv = cmb_random_exponential(1.0);
        cmb_process_hold(t_srv);
    }
}

int main(void)
{
    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);

    cmb_event_queue_initialize(0.0);

    struct cmb_buffer *que = cmb_buffer_create();
    cmb_buffer_initialize(que, "Queue", CMB_BUFFER_UNLIMITED);

    struct cmb_process *arr_proc = cmb_process_create();
    cmb_process_initialize(arr_proc, "Arrivals", arrivals, que, 0);
    cmb_process_start(arr_proc);

    struct cmb_process *serv_proc = cmb_process_create();
    cmb_process_initialize(serv_proc, "Service", service, que, 0);
    cmb_process_start(serv_proc);

    cmb_event_queue_execute();

    cmb_process_terminate(serv_proc);
    cmb_process_destroy(serv_proc);

    cmb_process_terminate(arr_proc);
    cmb_process_destroy(arr_proc);

    cmb_buffer_terminate(que);
    cmb_buffer_destroy(que);

    cmb_event_queue_terminate();
    cmb_random_terminate();

    return 0;
}
