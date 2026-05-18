/*
 * Test script for condition variables.
 * Usage:
 *      test_condition [-s <seed>][-t]
 *
 * Creates a complex mixed state simulation of a harbor, where tides and wind
 * conditions are state variables. Tugs and berths are modelled as resource
 * stores. Arriving ships come in various sizes, with different resource needs
 * and different requirements to max wind and min water depth. The entire
 * package of states and resources is modelled as a condition variable that the
 * ship processes can wait for before docking. The time unit is one hour.
 *
 * Somewhat inspired by
 *      https://dl.acm.org/doi/pdf/10.1145/1103225.1103226
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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

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
    struct cmb_resourcepool *tugs;
    struct cmb_resourcepool *berths[2];
    struct cmb_condition *harbormaster;
    struct cmb_condition *davyjones;
    struct cmi_hashheap *active_ships;
    struct cmi_slist_head *departed_ships;
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
    double duration;
    /* Outcomes */
    struct cmb_dataset *system_time[2];
};

/* A ship is a derived class from cmb_process */
struct ship {
    struct cmb_process core;       /* <= Note: The real thing, not a pointer */
    uint64_t id;
    double max_wind;
    double min_depth;
    unsigned tugs;
    enum ship_size size;
    struct cmi_slist_head listhead;
};

/* The entire context for our simulation run */
struct context {
    struct simulation *sim;
    struct env_state *state;
    struct trial *trial;
};

/* A process that updates the weather once per hour */
void *weather_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    struct env_state *env = ctx->state;
    const struct simulation *sim = ctx->sim;

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        /* Wind magnitude in meters per second */
        const double wmag = cmb_random_rayleigh(5.0);
        cmb_assert_always(wmag >= 0.0);
        const double wold = env->wind_magnitude;
        env->wind_magnitude = 0.5 * wmag + 0.5 * wold;

        /* Wind direction in compass degrees, dominant from the southwest */
        const double wdir1 = cmb_random_PERT(0.0, 225.0, 360.0);
        cmb_assert_always((wdir1 >= 0.0) && (wdir1 <= 360.0));
        const double wdir2 = cmb_random_PERT(0.0,  45.0, 360.0);
        cmb_assert_always((wdir2 >= 0.0) && (wdir2 <= 360.0));
        env->wind_direction = 0.75 * wdir1 + 0.25 * wdir2;

        cmb_logger_user(stdout, USERFLAG2,
                        "Wind: %5.1f m/s %03.0f deg",
                        env->wind_magnitude,
                        env->wind_direction);

        /* Requesting the harbormaster to read the new weather bulletin */
        const uint64_t r = cmb_condition_signal(sim->harbormaster);
        cmb_logger_user(stdout, USERFLAG2, "Reactivated %" PRIu64 " ships", r);

        /* ... and wait until the next hour */
        const int64_t sig = cmb_process_hold(1.0);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    }
}

/* A process that updates the water depth once per hour */
void *tide_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    struct env_state *env = ctx->state;
    cmb_assert_always(ctx->state != NULL);
    const struct simulation *sim = ctx->sim;
    cmb_assert_always(sim != NULL);

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        /* A simple tide model with astronomical and weather-driven tides */
        const double t = cmb_time();
        const double da0 = 15.0;
        const double da1 = 1.0 * sin(2.0 * M_PI * t / 12.4);
        const double da2 = 0.5 * sin(2.0 * M_PI * t / 24.0);
        const double da3 = 0.25 * sin(2.0 * M_PI * t / (0.5 * 29.5 * 24));
        const double da = da0 + da1 + da2 + da3;

        /* Use wind speed as a proxy for air pressure, assume a west coast */
        const double dw1 = 0.5 * env->wind_magnitude;
        const double dw2 = 0.5 * env->wind_magnitude
                         * sin(env->wind_direction * M_PI / 180.0);
        const double dw = dw1 - dw2;

        env->water_depth = da + dw;
        cmb_logger_user(stdout, USERFLAG2,
                        "Water: %5.1f m",
                        env->water_depth);

        /* Requesting the harbormaster to read the tide dial as well */
        const uint64_t r = cmb_condition_signal(sim->harbormaster);
        cmb_logger_user(stdout, USERFLAG2, "Reactivated %" PRIu64 " ships", r);

        /* ... and wait until the next hour */
        const int64_t sig = cmb_process_hold(1.0);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    }
}

/* The demand predicate function for a ship wanting to dock */
bool is_ready_to_dock(const struct cmb_condition *cvp,
                      const struct cmb_process *pp,
                      const void *vctx) {
    cmb_unused(cvp);
    cmb_assert_always(pp != NULL);
    cmb_assert_always(vctx != NULL);

    const struct ship *shp = (struct ship *)pp;
    const struct context *ctx = vctx;
    const struct env_state *env = ctx->state;
    cmb_assert_always(env != NULL);
    const struct simulation *sim = ctx->sim;
    cmb_assert_always(sim != NULL);

    if (env->water_depth < shp->min_depth) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Water %f m too shallow for ship %s, needs %f",
                        env->water_depth, pp->name, shp->min_depth);
        return false;
    }

    if (env->wind_magnitude > shp->max_wind){
        cmb_logger_user(stdout, USERFLAG1,
                        "Wind %f m/s too strong for ship %s, max %f",
                        env->wind_magnitude, pp->name, shp->max_wind);
        return false;
    }

    if (cmb_resourcepool_available(sim->tugs) < shp->tugs) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Not enough available tugs for ship %s",
                        pp->name);
        return false;
    }

    if (cmb_resourcepool_available(sim->berths[shp->size]) < 1u) {
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
    cmb_assert_always(me != NULL);
    cmb_assert_always(vctx != NULL);

    /* Unpack some convenient shortcut names */
    struct ship *shp = (struct ship *)me;
    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;
    cmb_assert_always(sim != NULL);
    struct cmb_condition *hbm = sim->harbormaster;
    cmb_assert_always(hbm != NULL);
    const struct trial *trl = ctx->trial;
    cmb_assert_always(trl != NULL);

    /* Note ourselves as active */
    cmb_logger_user(stdout, USERFLAG1, "Ship %s arrives", me->name);
    const double t_arr = cmb_time();
    cmi_hashheap_enqueue(sim->active_ships, shp,
                         NULL, NULL, NULL, shp->id, t_arr, 0u);

    /* Wait for suitable conditions to dock */
    int64_t sig = 0;
    while (!is_ready_to_dock(NULL, me, ctx)) {
        /* Loop to catch any spurious wakeups, such as several ships waiting for
         * the tide and one of them grabbing the tugs before we can react. */
        sig = cmb_condition_wait(hbm, is_ready_to_dock, ctx);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    }

    /* Resources are ready, grab them for ourselves */
    cmb_logger_user(stdout, USERFLAG1, "Ship %s cleared to dock", me->name);
    sig = cmb_resourcepool_acquire(sim->berths[shp->size], 1u);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    sig = cmb_resourcepool_acquire(sim->tugs, shp->tugs);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    const double docking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_assert_always((docking_time >= 0.4) && (docking_time <= 0.8));
    sig = cmb_process_hold(docking_time);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);

    /* Safely at the quay to unload cargo, dismiss the tugs for now */
    cmb_logger_user(stdout, USERFLAG1, "Ship %s docked, unloading", me->name);
    cmb_resourcepool_release(sim->tugs, shp->tugs);
    const double tua = trl->unloading_time_avg[shp->size];
    const double unloading_time = cmb_random_PERT(0.75 * tua, tua, 2 * tua);
    cmb_assert_always((unloading_time >= 0.75 * tua) && (unloading_time <= 2 * tua));
    sig = cmb_process_hold(unloading_time);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);

    /* Need the tugs again to get out of here */
    cmb_logger_user(stdout, USERFLAG1, "Ship %s ready to leave", me->name);
    sig = cmb_resourcepool_acquire(sim->tugs, shp->tugs);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    const double undocking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    sig = cmb_process_hold(undocking_time);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);

    /* Cleared berth, done with the tugs */
    cmb_logger_user(stdout, USERFLAG1, "Ship %s left harbor", me->name);
    cmb_resourcepool_release(sim->berths[shp->size], 1u);
    cmb_resourcepool_release(sim->tugs, shp->tugs);

    /* One pass process, remove ourselves from the active set */
    const bool found = cmi_hashheap_remove(sim->active_ships, shp->id);
    cmb_assert_always(found);
    /* List ourselves as departed instead */
    cmi_slist_push(sim->departed_ships, &(shp->listhead));
    /* Inform Davy Jones that we are coming his way */
    uint64_t r = cmb_condition_signal(sim->davyjones);
    cmb_assert_always(r == 1u);

    /* Store the time we spent working as an exit value in a separate heap object.
     * The exit value is a void*, so we could store anything there, but for this
     * demo, we keep it simple. */
    const double t_dep = cmb_time();
    double *t_sys_p = malloc(sizeof(double));
    cmb_assert_always(t_sys_p != NULL);
    *t_sys_p = t_dep - t_arr;

    cmb_logger_user(stdout, USERFLAG1, "Ship %s arr %g dep %f time in system %f",
        me->name, t_arr, t_dep, *t_sys_p);

    /* Note that returning from a process function has the same effect as calling
     * cmb_process_exit() with the return value as the argument. */
    return t_sys_p;
}

/* The arrival process generating new ships */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    const struct trial *trl = ctx->trial;
    cmb_assert_always(trl != NULL);
    cmb_assert_always(trl->arrival_rate > 0.0);
    const double mean = 1.0 / trl->arrival_rate;
    const double p_large = trl->percent_large;

    uint64_t cnt = 0u;
    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        cmb_process_hold(cmb_random_exponential(mean));

        /* The ship class is a derived subclass of cmb_process, we malloc it
         * directly instead of calling cmb_process_create() */
        struct ship *shp = malloc(sizeof(struct ship));
        cmb_assert_always(shp != NULL);

        /* Remember to zero-initialize it if malloc'ing on your own! */
        memset(shp, 0, sizeof(struct ship));

        shp->id = ++cnt;
        /* We started the ship size enum from 0 to match array indexes. If we
         * had more size classes, we could use cmb_random_dice(0, n) instead. */
        shp->size = cmb_random_bernoulli(p_large);

        /* We would probably not hard-code parameters except in a demo like this */
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

        /* A ship needs a name */
        char namebuf[20];
        const int r = snprintf(namebuf, sizeof(namebuf),
                         "Ship_%04" PRIu64 "%s",
                         cnt, ((shp->size == SMALL) ? "_small" : "_large"));
        cmb_assert_always((r >= 0) && (r < (int)sizeof(namebuf)) && (namebuf[r] == '\0'));
        cmb_process_initialize((struct cmb_process *)shp, namebuf, ship_proc, vctx, 0);
        cmb_assert_always(cmb_process_status((struct cmb_process *)shp) == CMB_PROCESS_CREATED);

        /* Start our new ship heading into the harbor */
        cmb_process_start((struct cmb_process *)shp);
        cmb_assert_always(cmb_process_status((struct cmb_process *)shp) == CMB_PROCESS_CREATED);
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
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;
    cmb_assert_always(sim != NULL);

    /* Simple: One or more ships in the list of departed ships */
    return (cmi_slist_is_empty(sim->departed_ships) == false);
}

/* The departure process */
void *departure_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;
    cmb_assert_always(sim != NULL);
    const struct trial *trl = ctx->trial;
    cmb_assert_always(trl != NULL);
    struct cmi_slist_head *dep_head = sim->departed_ships;

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        /* We do not need to loop here, since this is the only process waiting */
        const int64_t sig = cmb_condition_wait(sim->davyjones, is_departed, vctx);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);

        /* Got one, collect its exit value */
        struct cmi_slist_head *shead = cmi_slist_pop(dep_head);
        cmb_assert_always(shead != NULL);
        struct ship *shp = cmi_container_of(shead, struct ship, listhead);
        cmb_assert_always(shp != NULL);
        double *t_sys_p = cmb_process_exit_value((struct cmb_process *)shp);
        cmb_assert_always(t_sys_p != NULL);
        cmb_logger_user(stdout, USERFLAG1,
                        "Recycling ship %s, time in system %f",
                        ((struct cmb_process *)shp)->name,
                        *t_sys_p);

        /* Add it to the statistics and clean up */
        cmb_dataset_add(trl->system_time[shp->size], *t_sys_p);
        /* Frees internally allocated memory, but not the object itself */
        cmb_process_terminate((struct cmb_process *)shp);
        /* We malloc'ed it, call free() directly instead of cmb_process_destroy() */
        free(shp);
        /* The exit value was malloc'ed in the ship process, free it as well */
        free(t_sys_p);
    }
}

/* Just to keep ourselves amused while the simulation is running */
void *entertainment_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_unused(vctx);

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        /* Print one dot per simulated year */
        const int64_t sig = cmb_process_hold(24.0 * 7 * 52);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
        printf(".");
        fflush(stdout);
    }
}

/* An event to shut down the simulation */
void end_sim_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_process_stop(sim->weather, NULL);
    cmb_process_stop(sim->tide, NULL);
    cmb_process_stop(sim->arrivals, NULL);
    cmb_process_stop(sim->departures, NULL);
    cmb_process_stop(sim->entertainment, NULL);

    /* Also stop and recycle any still active ships */
    while (cmi_hashheap_count(sim->active_ships) > 0u) {
        void **item = cmi_hashheap_dequeue(sim->active_ships);
        cmb_assert_always(item != NULL);
        struct ship *shp = item[0];
        cmb_assert_always(shp != NULL);
        cmb_assert_always(cmb_process_status((struct cmb_process *)shp) == CMB_PROCESS_RUNNING);
        const int64_t r = cmb_process_stop((struct cmb_process *)shp, NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(cmb_process_status((struct cmb_process *)shp) == CMB_PROCESS_FINISHED);
        cmb_process_terminate((struct cmb_process *)shp);
        free(shp);
    }
}

/* For now, set params here instead of in an external experiment array */
void set_test_parameters(struct trial *trl, double dur)
{
    trl->arrival_rate = 0.5;
    trl->percent_large = 0.25;
    trl->num_tugs = 10;
    trl->num_berths[SMALL] = 6;
    trl->num_berths[LARGE] = 3;
    trl->unloading_time_avg[SMALL] = 8.0;
    trl->unloading_time_avg[LARGE] = 12.0;

    trl->duration = dur;
}

/* The test function running the simulation */
void test_condition(uint64_t seed, double dur)
{
    cmi_test_print_line("*");
    printf("***********************   Testing condition variables  *************************\n");
    cmi_test_print_line("*");

    printf("Cimba version %s\n", cimba_version());
    printf("Using seed: 0x%" PRIx64 "\n", seed);
    cmb_random_initialize(seed);
    cmb_event_queue_initialize(0.0);

    /* Turn off/on selected logging levels */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_logger_flags_off(USERFLAG2);

    /* Our simulated world exists on the main stack, initialize memory */
    struct simulation sim;
    cmi_memset(&sim, 0, sizeof(sim));
    struct env_state state = { 0.0, 0.0, 0.0 };
    struct trial trl;
    set_test_parameters(&trl, dur);
    struct context ctx = { &sim, &state, &trl };

    /* Create the statistics collectors */
    for (int i = 0; i < 2; i++) {
        trl.system_time[i] = cmb_dataset_create();
        cmb_assert_always(trl.system_time[i] != NULL);
        cmb_dataset_initialize(trl.system_time[i]);
    }

    /* Create weather and tide processes */
    sim.weather = cmb_process_create();
    cmb_assert_always(cmb_process_status(sim.weather) == CMB_PROCESS_CREATED);
    cmb_assert_always(sim.weather != NULL);
    cmb_assert_always(cmb_process_status(sim.weather) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim.weather, "Wind", weather_proc, &ctx, 0);
    cmb_process_start(sim.weather);

    sim.tide = cmb_process_create();
    cmb_assert_always(sim.tide != NULL);
    cmb_assert_always(cmb_process_status(sim.weather) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim.tide, "Depth", tide_proc, &ctx, 0);
    cmb_assert_always(cmb_process_status(sim.tide) == CMB_PROCESS_CREATED);
    cmb_process_start(sim.tide);

    /* Create the resources, turn on history recording with no warmup period */
    sim.tugs = cmb_resourcepool_create();
    cmb_assert_always(sim.tugs != NULL);
    cmb_resourcepool_initialize(sim.tugs, "Tugs", trl.num_tugs);
    cmb_resourcepool_start_recording(sim.tugs);
    for (int i = 0; i < 2; i++) {
        sim.berths[i] = cmb_resourcepool_create();
        cmb_assert_always(sim.berths[i] != NULL);
        cmb_resourcepool_initialize(sim.berths[i],
            ((i == 0)? "Small berth" : "Large berth"),
            trl.num_berths[i]);
        cmb_resourcepool_start_recording(sim.berths[i]);
    }

    /* Create the harbormaster and Davy Jones himself */
    sim.harbormaster = cmb_condition_create();
    cmb_assert_always(sim.harbormaster != NULL);
    cmb_condition_initialize(sim.harbormaster, "Harbormaster");
    sim.davyjones = cmb_condition_create();
    cmb_assert_always(sim.davyjones != NULL);
    cmb_condition_initialize(sim.davyjones, "Davy Jones");

    /* Create the arrival and departure processes */
    sim.arrivals = cmb_process_create();
    cmb_assert_always(sim.arrivals != NULL);
    cmb_assert_always(cmb_process_status(sim.arrivals) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim.arrivals, "Arrivals", arrival_proc, &ctx, 0);
    cmb_process_start(sim.arrivals);
    sim.departures = cmb_process_create();
    cmb_assert_always(sim.departures != NULL);
    cmb_assert_always(cmb_process_status(sim.departures) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim.departures, "Departures", departure_proc, &ctx, 0);
    cmb_process_start(sim.departures);

    /* Create the collections of active and departed ships */
    sim.active_ships = cmi_hashheap_create();
    cmb_assert_always(sim.active_ships != NULL);
    cmi_hashheap_initialize(sim.active_ships, 3u, NULL);
    sim.departed_ships = cmi_slist_create();
    cmb_assert_always(sim.departed_ships != NULL);
    cmi_slist_initialize(sim.departed_ships);

    uint64_t evt_hdle = cmb_event_schedule(end_sim_evt, &sim, NULL, dur, 0);
    cmb_assert_always(evt_hdle != 0u);

    sim.entertainment = cmb_process_create();
    cmb_assert_always(sim.entertainment != NULL);
    cmb_assert_always(cmb_process_status(sim.entertainment) == CMB_PROCESS_CREATED);
    cmb_process_initialize(sim.entertainment, "Dot", entertainment_proc, NULL, 0);
    cmb_process_start(sim.entertainment);

    /* Execute the simulation */
    cmb_event_queue_execute();

    /* Report statistics, using built-in history statistics for the resources */
    for (int i = 0; i < 2; i++) {
        printf("\nSystem times for %s ships:\n", ((i == 0) ? "small" : "large"));
        const unsigned n = cmb_dataset_count(trl.system_time[i]);
        if (n > 0) {
            struct cmb_datasummary dsumm;
            cmb_dataset_summarize(trl.system_time[i], &dsumm);
            cmb_datasummary_print(&dsumm, stdout, true);
            cmb_dataset_histogram_print(trl.system_time[i], stdout, 20, 0.0, 0.0);
        }
    }

    for (int i = 0; i < 2; i++) {
        printf("\nUtilization of %s berths:\n", ((i == 0) ? "small" : "large"));
        const struct cmb_timeseries *hist = cmb_resourcepool_get_history(sim.berths[i]);
        const unsigned n = cmb_timeseries_count(hist);
        if (n > 0) {
            struct cmb_wtdsummary wsumm;
            cmb_timeseries_summarize(hist, &wsumm);
            cmb_wtdsummary_print(&wsumm, stdout, true);
            cmb_timeseries_histogram_print(hist, stdout, 20, 0.0, 0.0);
        }
    }

    printf("\nUtilization of tugs:\n");
    const struct cmb_timeseries *hist = cmb_resourcepool_get_history(sim.tugs);
    const unsigned n = cmb_timeseries_count(hist);
    if (n > 0) {
        struct cmb_wtdsummary wsumm;
        cmb_timeseries_summarize(hist, &wsumm);
        cmb_wtdsummary_print(&wsumm, stdout, true);
        cmb_timeseries_histogram_print(hist, stdout, 20, 0.0, 0.0);
    }

    /* Clean up */
    for (int i = 0; i < 2; i++) {
        cmb_dataset_destroy(trl.system_time[i]);
        cmb_resourcepool_destroy(sim.berths[i]);
     }

    cmb_condition_destroy(sim.harbormaster);
    cmb_condition_destroy(sim.davyjones);
    cmb_resourcepool_destroy(sim.tugs);
    cmb_process_destroy(sim.weather);
    cmb_process_destroy(sim.tide);

    cmb_event_queue_terminate();
    cmb_random_terminate();
    cmi_test_print_line("*");
}


int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 24.0 * 7 * 52 * 100;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:t")) != -1) {
        switch (opt) {
            case 'd':
                errno = 0;
                dur = strtod(optarg, NULL);
                if (errno != 0 || dur <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 's':
                errno = 0;
                seed = (uint64_t)strtoul(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 't':
                timing_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    const clock_t start_time = clock();

    test_condition(seed, dur);

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}