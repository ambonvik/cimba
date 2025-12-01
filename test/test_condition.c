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

#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

/* Our simulated world */
struct simulation {
    struct cmb_process *weather;
    struct cmb_process *tide;
    struct cmb_process *arrivals;
    struct cmb_process *departures;
    struct cmb_process *entertainment;
    struct cmb_resourcestore *tugs;
    struct cmb_resourcestore *berths[2];
    struct cmb_condition *harbormaster;
    struct cmb_condition *davyjones;
    struct cmi_list_tag *departed_ships;
 };

/* The current sea and weather state */
struct env_state {
    double wind_magnitude;
    double wind_direction;
    double water_depth;
};

enum ship_size {
    SMALL = 0,
    LARGE
};

/* The trial we are performing */
struct trial {
    /* Parameters */
    double arrival_rate;
    double percent_large;
    unsigned num_tugs;
    unsigned num_berths[2];
    double unloading_time_avg[2];
    /* Outcomes */
    struct cmb_dataset *system_time[2];
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

/* A process that updates the weather once per hour */
void *weather_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    struct context *ctx = vctx;
    struct env_state *env = ctx->state;
    struct simulation *sim = ctx->sim;

    while (true) {
        const double wmag = cmb_random_rayleigh(5.0);
        double wold = env->wind_magnitude;
        env->wind_magnitude = 0.5 * wmag + 0.5 * wold;

        const double wdir1 = cmb_random_PERT(0.0, 225.0, 360.0);
        const double wdir2 = cmb_random_PERT(0.0,  45.0, 360.0);
        env->wind_direction = 0.75 * wdir1 + 0.25 * wdir2;

        cmb_logger_user(stdout, USERFLAG2,
                        "Wind: %5.1f m/s %03.0f deg",
                        env->wind_magnitude,
                        env->wind_direction);

        cmb_condition_signal(sim->harbormaster);
        cmb_process_hold(1.0);
    }
}

/* A process that updates the water depth once per hour */
void *tide_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    struct context *ctx = vctx;
    struct env_state *env = ctx->state;
    struct simulation *sim = ctx->sim;

    while (true) {
        const double t = cmb_time();
        const double da0 = 15.0;
        const double da1 = 1.0 * sin(2.0 * M_PI * t / 12.4);
        const double da2 = 0.5 * sin(2.0 * M_PI * t / 24.0);
        const double da3 = 0.25 * sin(2.0 * M_PI * t / (0.5 * 29.5 * 24));
        const double da = da0 + da1 + da2 + da3;

        const double dw = 0.5 * env->wind_magnitude
                         * sin(env->wind_direction * M_PI / 180.0);

        env->water_depth = da - dw;
        cmb_logger_user(stdout, USERFLAG2,
                        "Water: %5.1f m",
                        env->water_depth);

        cmb_condition_signal(sim->harbormaster);
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

    if (env->water_depth < shp->min_depth) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Water %f m too shallow for ship %s",
                        env->water_depth, pp->name);
        return false;
    }

    if (env->wind_magnitude > shp->max_wind){
        cmb_logger_user(stdout, USERFLAG1,
                        "Wind %f m/s too strong for ship %s",
                        env->wind_magnitude, pp->name);
        return false;
    }

    if (cmb_resourcestore_available(sim->tugs) < shp->tugs) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Not enough available tugs for ship %s",
                        pp->name);
        return false;
    }

    if (cmb_resourcestore_available(sim->berths[shp->size]) < 1u) {
        cmb_logger_user(stdout, USERFLAG1,
                        "No available berth for ship %s",
                        pp->name);
        return false;
    }

    cmb_logger_user(stdout, USERFLAG1, "All good for ship %s", pp->name);
    return true;
}

/* The ship process function */
void *ship_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_debug(me != NULL);
    cmb_assert_debug(vctx != NULL);

    struct ship *shp = (struct ship *)me;
    struct context *ctx = vctx;
    struct simulation *sim = ctx->sim;
    struct cmb_condition *hbm = sim->harbormaster;
    struct trial *trl = ctx->trial;

    cmb_logger_user(stdout, USERFLAG1, "Ship %s arrives", me->name);
    const double t_arr = cmb_time();

    while (!is_ready_to_dock(NULL, me, ctx)) {
        cmb_condition_wait(hbm, is_ready_to_dock, ctx);
    }

    cmb_logger_user(stdout, USERFLAG1, "Ship %s cleared to dock", me->name);
    cmb_resourcestore_acquire(sim->berths[shp->size], 1u);
    cmb_resourcestore_acquire(sim->tugs, shp->tugs);
    const double docking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(docking_time);

    cmb_logger_user(stdout, USERFLAG1, "Ship %s docked, unloading", me->name);
    cmb_resourcestore_release(sim->tugs, shp->tugs);
    const double tua = trl->unloading_time_avg[shp->size];
    const double unloading_time = cmb_random_PERT(0.75 * tua, tua, 2 * tua);
    cmb_process_hold(unloading_time);

    cmb_logger_user(stdout, USERFLAG1, "Ship %s ready to leave", me->name);
    cmb_resourcestore_acquire(sim->tugs, shp->tugs);
    const double undocking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(undocking_time);

    cmb_logger_user(stdout, USERFLAG1, "Ship %s left harbor", me->name);
    cmb_resourcestore_release(sim->berths[shp->size], 1u);
    cmb_resourcestore_release(sim->tugs, shp->tugs);
    cmi_list_push(&(sim->departed_ships), shp);
    cmb_condition_signal(sim->davyjones);

    const double t_dep = cmb_time();
    double *t_sys_p = malloc(sizeof(double));;
    *t_sys_p = t_dep - t_arr;

    return t_sys_p;
}

/* The arrival process */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctx = vctx;
    const struct trial *trl = ctx->trial;
    const double mean = 1.0 / trl->arrival_rate;
    const double p_large = trl->percent_large;

    uint64_t cnt = 0u;
    while (true) {
        cmb_process_hold(cmb_random_exponential(mean));

        struct ship *shp = malloc(sizeof(struct ship));
        memset(shp, 0, sizeof(struct ship));
        shp->size = cmb_random_bernoulli(p_large);
        if (shp->size == SMALL) {
            shp->max_wind = 10.0;
            shp->min_depth = 8.0;
            shp->tugs = 1u;
        }
        else {
            shp->max_wind = 12.0;
            shp->min_depth = 13.0;
            shp->tugs = 3u;
        }

        char namebuf[20];
        snprintf(namebuf, sizeof(namebuf),
                 "Ship_%04llu%s",
                 ++cnt, ((shp->size == SMALL) ? "_small" : "_large"));
        cmb_process_initialize((struct cmb_process *)shp, namebuf, ship_proc, vctx, 0);

        cmb_process_start((struct cmb_process *)shp);
        cmb_logger_user(stdout, USERFLAG1, "Ship %s started", namebuf);
    }
}

/* The demand predicate function for ships leaving */
bool is_departed(const struct cmb_condition *cvp,
                 const struct cmb_process *pp,
                 const void *vctx)
{
    cmb_unused(cvp);
    cmb_unused(pp);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;

    return (sim->departed_ships != NULL);
}

/* The departure process */
void *departure_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctx = vctx;
    struct simulation *sim = ctx->sim;
    const struct trial *trl = ctx->trial;
    struct cmi_list_tag **dep_head = &(sim->departed_ships);

    while (true) {
        cmb_condition_wait(sim->davyjones, is_departed, vctx);
        struct ship *shp = cmi_list_pop(dep_head);
        double *t_sys = cmb_process_get_exit_value((struct cmb_process *)shp);
        cmb_logger_user(stdout, USERFLAG1,
                        "Recycling ship %s, time in system %f",
                        ((struct cmb_process *)shp)->name,
                        *t_sys);

        cmb_dataset_add(trl->system_time[shp->size], *t_sys);
        cmb_process_terminate((struct cmb_process *)shp);
        free(t_sys);
        free(shp);
    }
}

void *entertainment_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_unused(vctx);

    while (true) {
        cmb_process_hold(24*7*52);
        printf(".");
        fflush(stdout);
    }
}

/* An event to shut down the simulation */
void end_sim_evt(void *subject, void *object)
{
    cmb_assert_debug(subject != NULL);
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_process_stop(sim->weather, NULL);
    cmb_process_stop(sim->tide, NULL);
    cmb_process_stop(sim->arrivals, NULL);
    cmb_process_stop(sim->departures, NULL);
    cmb_process_stop(sim->entertainment, NULL);
}

void test_condition(void)
{
    const uint64_t seed = cmb_random_get_hwseed();
    printf("seed: 0x%llx\n", seed);
    cmb_random_initialize(seed);
    cmb_event_queue_initialize(0.0);

    /* Turn off/on selected logging levels */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_logger_flags_off(USERFLAG2);

    /* Our simulated world exists on the main stack */
    struct simulation sim;
    cmi_memset(&sim, 0, sizeof(sim));
    struct env_state state = { 0.0, 0.0, 0.0 };
    struct trial trial = { 0.25, 0.25, 10, { 6, 3 }, { 8.0, 12.0 }, { NULL, NULL } };
    struct context ctx = { &sim, &state, &trial };

    /* Create the statistics collectors */
    for (int i = 0; i < 2; i++) {
        trial.system_time[i] = cmb_dataset_create();
        cmb_dataset_initialize(trial.system_time[i]);
    }

    /* Create weather and tide processes */
    sim.weather = cmb_process_create();
    cmb_process_initialize(sim.weather, "Weather", weather_proc, &ctx, 0);
    cmb_process_start(sim.weather);

    sim.tide = cmb_process_create();
    cmb_process_initialize(sim.tide, "Depth", tide_proc, &ctx, 0);
    cmb_process_start(sim.tide);

    /* Create the resources, turn on recording with no warmup period */
    sim.tugs = cmb_resourcestore_create();
    cmb_resourcestore_initialize(sim.tugs, "Tugs", trial.num_tugs);
    cmb_resourcestore_start_recording(sim.tugs);
    for (int i = 0; i < 2; i++) {
        sim.berths[i] = cmb_resourcestore_create();
        cmb_resourcestore_initialize(sim.berths[i],
            ((i == 0)? "Small berth" : "Large berth"),
            trial.num_berths[i]);
        cmb_resourcestore_start_recording(sim.berths[i]);
    }

    /* Create the harbormaster and Davy Jones himself */
    sim.harbormaster = cmb_condition_create();
    cmb_condition_initialize(sim.harbormaster, "Harbormaster");
    sim.davyjones = cmb_condition_create();
    cmb_condition_initialize(sim.davyjones, "Davy Jones");

    /* Create the arrival and departure processes */
    sim.arrivals = cmb_process_create();
    cmb_process_initialize(sim.arrivals, "Arrivals", arrival_proc, &ctx, 0);
    cmb_process_start(sim.arrivals);
    sim.departures = cmb_process_create();
    cmb_process_initialize(sim.departures, "Departures", departure_proc, &ctx, 0);
    cmb_process_start(sim.departures);

    /* Schedule the end event at a fixed time */
    (void)cmb_event_schedule(end_sim_evt, &sim, NULL, 24.0 * 7 * 52 * 100, 0);

    /* Just to keep ourselves amused in the meantime */
    sim.entertainment = cmb_process_create();
    cmb_process_initialize(sim.entertainment, "Dot", entertainment_proc, NULL, 0);
    cmb_process_start(sim.entertainment);

    /* Execute the simulation */
    cmb_event_queue_execute();

    /* Report statistics */
    for (int i = 0; i < 2; i++) {
        printf("\nSystem times for %s ships:\n", ((i == 0) ? "small" : "large"));
        const unsigned n = cmb_dataset_count(trial.system_time[i]);
        if (n > 0) {
            struct cmb_datasummary dsumm;
            cmb_dataset_summarize(trial.system_time[i], &dsumm);
            cmb_datasummary_print(&dsumm, stdout, true);
            cmb_dataset_print_histogram(trial.system_time[i], stdout, 20, 0.0, 0.0);
        }
    }

    for (int i = 0; i < 2; i++) {
        printf("\nUtilization of %s berths:\n", ((i == 0) ? "small" : "large"));
        const struct cmb_timeseries *hist = cmb_resourcestore_get_history(sim.berths[0]);
        const unsigned n = cmb_timeseries_count(hist);
        if (n > 0) {
            struct cmb_wtdsummary wsumm;
            cmb_timeseries_summarize(hist, &wsumm);
            cmb_wtdsummary_print(&wsumm, stdout, true);
            cmb_timeseries_print_histogram(hist, stdout, 20, 0.0, 0.0);
        }
    }

    printf("\nUtilization of tugs:\n");
    const struct cmb_timeseries *hist = cmb_resourcestore_get_history(sim.tugs);
    const unsigned n = cmb_timeseries_count(hist);
    if (n > 0) {
        struct cmb_wtdsummary wsumm;
        cmb_timeseries_summarize(hist, &wsumm);
        cmb_wtdsummary_print(&wsumm, stdout, true);
        cmb_timeseries_print_histogram(hist, stdout, 20, 0.0, 0.0);
    }

    /* Clean up */
    for (int i = 0; i < 2; i++) {
        cmb_dataset_destroy(trial.system_time[i]);
        cmb_resourcestore_destroy(sim.berths[i]);
     }

    cmb_condition_destroy(sim.harbormaster);
    cmb_condition_destroy(sim.davyjones);
    cmb_resourcestore_destroy(sim.tugs);
    cmb_process_destroy(sim.weather);
    cmb_process_destroy(sim.tide);

    cmb_event_queue_terminate();
}


int main(void)
{
    cmi_test_print_line("*");
    printf("***********************   Testing condition variables  *************************\n");
    cmi_test_print_line("*");

    printf("Cimba version %s\n", cimba_version());
    const clock_t start_time = clock();

    test_condition();

    const clock_t end_time = clock();
    const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    cmi_test_print_line("*");
    printf("\nIt took %g sec\n", elapsed_time);

    return 0;
}