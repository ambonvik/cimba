/*
 * Test script for condition variables.
 *
 * Creates a complex mixed state simulation of a harbor, where tides and wind
 * conditions are state variables. Tugs and berths are modelled as resource
 * stores. Arriving ships come in various sizes, with different resource needs
 * and different requirements to max wind and min water depth. The entire
 * package of states and resources is modelled as a condition variable that the
 * ship processes can wait for before docking. The time unit is one hour.
 *
 * Somewhat inspired by
 * https://dl.acm.org/doi/pdf/10.1145/1103225.1103226
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

#include "cimba.h"

#include "test.h"

#define USERFLAG 0x00000001

/* Our simulated world */
struct simulation {
    struct cmb_process *weather;
    struct cmb_process *tide;
    struct cmb_process *arrivals;
    struct cmb_process *departures;
    struct cmb_resourcestore *tugs;
    struct cmb_resourcestore *small_berths;
    struct cmb_resourcestore *large_berths;
    struct cmb_condition *harbormaster;
    struct cmb_condition *davyjones;
    struct cmi_dlist_tag *ship_list_head;
    struct cmi_dlist_tag *ship_list_tail;
 };

/* The current sea and weather state */
struct env_state {
    double wind_magnitude;
    double wind_direction;
    double water_depth;
};

/* The trial we are performing */
struct trial {
    /* Parameters */
    unsigned num_tugs;
    unsigned num_large_berths;
    unsigned num_small_berths;
    double arrival_rate;
    double perc_sml[3];
    /* Outcomes */
    struct cmb_dataset *system_time[3];
};

enum ship_size {
    SMALL = 0,
    MEDIUM,
    LARGE
};

/* A ship is a derived class from cmb_process */
struct ship {
    struct cmb_process core;       /* <= The real thing, not a pointer */
    double max_wind;
    double min_depth;
    unsigned tugs;
    enum ship_size size;
};

/* The context a ship is encountering on arrival */
struct context {
    struct simulation *sim;
    struct env_state *state;
    struct trial *trial;
};

/* The event to shut down the simulation */
static void end_sim_evt(void *subject, void *object)
{
    cmb_assert_debug(subject != NULL);
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_process_stop(sim->weather, NULL);
    cmb_process_stop(sim->tide, NULL);
}

/* A process that updates the weather once per hour */
void *weather_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    while (true) {
        struct env_state *env = vctx;
        const double wmag = cmb_random_rayleigh(5.0);
        double wold = env->wind_magnitude;
        env->wind_magnitude = 0.5 * wmag + 0.5 * wold;

        const double wdir1 = cmb_random_PERT(0.0, 225.0, 360.0);
        const double wdir2 = cmb_random_PERT(0.0,  45.0, 360.0);
        env->wind_direction = 0.75 * wdir1 + 0.25 * wdir2;

        cmb_logger_user(stdout, USERFLAG,
                        "Wind: %5.1f m/s %03.0f deg",
                        env->wind_magnitude,
                        env->wind_direction);

        cmb_process_hold(1.0);
    }
}

/* A process that updates the water depth once per hour */
void *tide_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    while (true) {
        struct env_state *env = vctx;
        const double t = cmb_time();
        const double da0 = 15.0;
        const double da1 = 1.0 * sin(2.0 * M_PI * t / 12.4);
        const double da2 = 0.5 * sin(2.0 * M_PI * t / 24.0);
        const double da3 = 0.25 * sin(2.0 * M_PI * t / (0.5 * 29.5 * 24));
        const double da = da0 + da1 + da2 + da3;

        const double dw = 0.5 * env->wind_magnitude
                         * sin(env->wind_direction * M_PI / 180.0);

        env->water_depth = da - dw;
        cmb_logger_user(stdout, USERFLAG,
                        "Water: %5.1f m",
                        env->water_depth);

        cmb_process_hold(1.0);
    }
}

/* The demand predicate function for a ship wanting to dock */
bool is_ready_to_dock(const struct cmb_condition *cvp,
                      const struct cmb_process *pp,
                      const void *vctx) {
    cmb_unused(cvp);
    cmb_unused(pp);
    cmb_assert_debug(vctx != NULL);

    const struct ship *shp = (struct ship *)pp;
    const struct context *ctx = vctx;
    const struct env_state *env = ctx->state;
    const struct simulation *sim = ctx->sim;

    bool ret = true;
    if ((env->water_depth < shp->min_depth)
        || (env->wind_magnitude > shp->max_wind) ) {
        ret = false;
    }

    if (cmb_resourcestore_available(sim->tugs) < shp->tugs) {
        ret = false;
    }

    if (shp->size == SMALL) {
        if (cmb_resourcestore_available(sim->small_berths) < 1u) {
            ret = false;
        }
    }
    else {
        if (cmb_resourcestore_available(sim->large_berths) < 1u) {
            ret = false;
        }
    }

    return ret;
}

/* The ship process function */
void *ship_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_debug(me != NULL);
    cmb_assert_debug(vctx != NULL);

    const struct ship *shp = (struct ship *)me;
    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;
    struct cmb_condition *hbm = sim->harbormaster;

    cmb_logger_user(stdout, USERFLAG, "Ship %s arrives", me->name);
    const double t_arr = cmb_time();

    while (!is_ready_to_dock(NULL, me, ctx)) {
        cmb_condition_wait(hbm, is_ready_to_dock, ctx);
    }

    cmb_logger_user(stdout, USERFLAG, "Ship %s cleared to dock", me->name);
    cmb_resourcestore_acquire(sim->tugs, shp->tugs);
    if (shp->size == SMALL) {
        cmb_resourcestore_acquire(sim->small_berths, 1u);
    }
    else {
        cmb_resourcestore_acquire(sim->large_berths, 1u);
    }

    const double docking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(docking_time);
    cmb_resourcestore_release(sim->tugs, shp->tugs);

    cmb_logger_user(stdout, USERFLAG, "Ship %s docked, unloading", me->name);
    double unloading_time;
    if (shp->size == SMALL) {
        unloading_time = cmb_random_PERT(5.0, 6.0, 12.0);
    }
    else if (shp->size == MEDIUM) {
        unloading_time = cmb_random_PERT(10.0, 12.0, 24.0);
    }
    else {
        cmb_assert_debug(shp->size == LARGE);
        unloading_time = cmb_random_PERT(15.0, 20.0, 30.0);
    }
    cmb_process_hold(unloading_time);

    cmb_logger_user(stdout, USERFLAG, "Ship %s ready to leave", me->name);
    cmb_resourcestore_acquire(sim->tugs, shp->tugs);
    const double undocking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(undocking_time);
    cmb_resourcestore_release(sim->tugs, shp->tugs);
    if (shp->size == SMALL) {
        cmb_resourcestore_release(sim->small_berths, 1u);
    }
    else {
        cmb_resourcestore_release(sim->large_berths, 1u);
    }

    cmb_logger_user(stdout, USERFLAG, "Ship %s leaving", me->name);

    /* Todo: move statistics to arrival and departure process */
    const double t_dep = cmb_time();
    const double t_sys = t_dep - t_arr;
    struct cmb_dataset *systimes = ctx->trial->system_time[shp->size];
    cmb_dataset_add(systimes, t_sys);

    cmb_condition_signal(sim->davyjones);
    return NULL;
}

/* The arrival process */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    return NULL;
}

/* The demand predicate function for ships leaving */
bool is_departing(const struct cmb_condition *cvp,
                  const struct cmb_process *pp,
                  const void *vctx)
{
    cmb_unused(cvp);
    cmb_unused(pp);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;

    /* Searching from the oldest, is there at least one ship that has left? */
    if (sim->ship_list_tail != NULL) {
        struct cmi_dlist_tag *tag = sim->ship_list_tail;
        while (tag != NULL) {
            struct ship *shp = (struct ship *)tag->ptr;
            const struct cmb_process *prp = (struct cmb_process *)shp;
            if (cmb_process_get_state(prp) == CMB_PROCESS_FINISHED) {
                return true;
            }
            tag = tag->prev;
        }
    }

    return false;
}

/* The departure process */
void *departure_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    return NULL;
}

void test_condition(void)
{
    const uint64_t seed = cmb_random_get_hwseed();
    printf("seed: %llu\n", seed);
    cmb_random_initialize(seed);
    cmb_event_queue_initialize(0.0);

    /* Our simulated world exists on the main stack */
    struct simulation sim;
    cmi_memset(&sim, 0, sizeof(sim));
    struct env_state state = { 0.0, 0.0, 0.0 };
    struct trial trial = { 10, 6, 3, 0.1, {0.5, 0.3, 0.2}, {NULL, NULL, NULL} };
    struct context ctx = { &sim, &state, &trial };

    /* Create the statistics collectors */
    for (int i = 0; i < 3; i++) {
        trial.system_time[i] = cmb_dataset_create();
        cmb_dataset_initialize(trial.system_time[i]);
    }

    /* Create weather and tide processes */
    sim.weather = cmb_process_create();
    cmb_process_initialize(sim.weather, "Weather", weather_proc, &state, 0);
    cmb_process_start(sim.weather);

    sim.tide = cmb_process_create();
    cmb_process_initialize(sim.tide, "Depth", tide_proc, &state, 0);
    cmb_process_start(sim.tide);

    /* Create the resources */
    sim.tugs = cmb_resourcestore_create();
    cmb_resourcestore_initialize(sim.tugs, "Tugs", trial.num_tugs);
    sim.small_berths = cmb_resourcestore_create();
    cmb_resourcestore_initialize(sim.small_berths, "Small", trial.num_small_berths);
    sim.large_berths = cmb_resourcestore_create();
    cmb_resourcestore_initialize(sim.large_berths, "Large", trial.num_large_berths);

    /* Create the harbormaster and Davy Jones himself */
    sim.harbormaster = cmb_condition_create();
    cmb_condition_initialize(sim.harbormaster, "Harbormaster");
    sim.davyjones = cmb_condition_create();
    cmb_condition_initialize(sim.davyjones, "Davy Jones");

    /* Create the arrival and departure processes */

    /* Schedule the end event at a fixed time */
    (void)cmb_event_schedule(end_sim_evt, &sim, NULL, 100.0, 0);

    /* Execute the simulation */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_execute();

    /* Report statistics */
    printf("\nSystem times for small ships:\n");
    struct cmb_datasummary dsumm;
    cmb_datasummary_initialize(&dsumm);
    cmb_dataset_summarize(trial.system_time[0], &dsumm);
    cmb_datasummary_print(&dsumm, stdout, true);
    cmb_dataset_print_histogram(trial.system_time[0], stdout, 20, 0.0, 0.0);

    printf("\nSystem times for medium ships:\n");
    cmb_dataset_summarize(trial.system_time[0], &dsumm);
    cmb_datasummary_print(&dsumm, stdout, true);
    cmb_dataset_print_histogram(trial.system_time[1], stdout, 20, 0.0, 0.0);

    printf("\nSystem times for large ships:\n");
    cmb_dataset_summarize(trial.system_time[0], &dsumm);
    cmb_datasummary_print(&dsumm, stdout, true);
    cmb_dataset_print_histogram(trial.system_time[2], stdout, 20, 0.0, 0.0);

    printf("\nUtilization of small berths:\n");
    struct cmb_wtdsummary wsumm;
    cmb_wtdsummary_initialize(&wsumm);
    struct cmb_timeseries *hist = cmb_resourcestore_get_history(sim.small_berths);
    cmb_timeseries_summarize(hist, &wsumm);
    cmb_wtdsummary_print(&wsumm, stdout, true);
    cmb_timeseries_print_histogram(hist, stdout, 20, 0.0, 0.0);

    printf("\nUtilization of large berths:\n");
    hist = cmb_resourcestore_get_history(sim.large_berths);
    cmb_timeseries_summarize(hist, &wsumm);
    cmb_wtdsummary_print(&wsumm, stdout, true);
    cmb_timeseries_print_histogram(hist, stdout, 20, 0.0, 0.0);

    printf("\nUtilization of tugs:\n");
    hist = cmb_resourcestore_get_history(sim.tugs);
    cmb_timeseries_summarize(hist, &wsumm);
    cmb_wtdsummary_print(&wsumm, stdout, true);
    cmb_timeseries_print_histogram(hist, stdout, 20, 0.0, 0.0);


    /* Clean up */
    for (int i = 0; i < 3; i++) {
        cmb_dataset_destroy(trial.system_time[i]);
    }

    cmb_condition_destroy(sim.harbormaster);
    cmb_condition_destroy(sim.davyjones);
    cmb_resourcestore_destroy(sim.tugs);
    cmb_resourcestore_destroy(sim.small_berths);
    cmb_resourcestore_destroy(sim.large_berths);
    cmb_process_destroy(sim.weather);
    cmb_process_destroy(sim.tide);

    cmb_event_queue_terminate();
}


int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing condition   *****************************\n");
    cmi_test_print_line("*");

    test_condition();

    cmi_test_print_line("*");
    return 0;
}