/*
 * tutorial/tut_2_1.c
 *
 * Singlethreaded development version of the harbor simulation.
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

#include <cimba.h>
#include <stdio.h>

/* Bit masks to distinguish between two types of user-defined logging messages. */
#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

/* Our simulateed world consists of these entities. */
struct simulation {
    /* Environmental processes */
    struct cmb_process *weather;
    struct cmb_process *tide;
    /* Comings and goings */
    struct cmb_process *arrivals;
    struct cmb_process *departures;

    /* The fleet of tugboats */
    struct cmb_resourcestore *tugs;
    /* Small and large berths */
    struct cmb_resourcestore *berths[2];
    /* The radio channel */
    struct cmb_resource *comms;

    /* A condition variable permitting docking */
    struct cmb_condition *harbormaster;
    /* A condition variable monitoring departures */
    struct cmb_condition *davyjones;

    /* A set of all active ships */
    struct cmi_hashheap *active_ships;
    /* A list of departed ships  */
    struct cmi_list_tag *departed_ships;

    /* Data collector for local use in this instane */
    struct cmb_dataset *time_in_system[2];

};

/* Variables describing the state of the environment around our entities */
struct environment {
    double wind_magnitude;
    double wind_direction;
    double water_depth;
};

/* A single trial is defined by these parameters, and generates these results. */
struct trial {
    /* Model parameters */
    double mean_wind;
    double reference_depth;
    double arrival_rate;
    double percent_large;
    unsigned num_tugs;
    unsigned num_berths[2];
    double unloading_time_avg[2];

    /* Control parameters */
    double warmup_time;
    double duration;

    /* Results */
    uint64_t seed_used;
    double avg_time_in_system[2];
};

struct context {
    struct simulation *sim;
    struct environment *env;
    struct trial *trl;
};

enum ship_size {
    SMALL = 0,
    LARGE
};

/* A ship is a derived class from cmb_process */
struct ship {
    struct cmb_process core;       /* <= Note: The real thing, not a pointer */
    enum ship_size size;
    unsigned tugs_needed;
    double max_wind;
    double min_depth;
};

/* A process that updates the weather once per hour */
void *weather_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctxp = vctx;
    struct environment *envp = ctxp->env;
    const struct trial *trlp = ctxp->trl;

    while (true) {
        /* Wind magnitude in meters per second */
        const double wmag = cmb_random_rayleigh(trlp->mean_wind);
        const double wold = envp->wind_magnitude;
        envp->wind_magnitude = 0.5 * wmag + 0.5 * wold;

        /* Wind direction in compass degrees, dominant from the southwest */
        const double wdir1 = cmb_random_PERT(0.0, 225.0, 360.0);
        const double wdir2 = cmb_random_PERT(0.0,  45.0, 360.0);
        envp->wind_direction = 0.75 * wdir1 + 0.25 * wdir2;

        /* We could request the harbormaster to read the new weather bulletin:
         *       cmb_condition_signal(simp->harbormaster);
         * but it will be signalled by the tide process in a moment anyway,
         * so we do not need to do it from here. */

        /* Wait until the top of the next hour */
        cmb_process_hold(1.0);
    }
}

/* A process that updates the water depth once per hour */
void *tide_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctxp = vctx;
    struct environment *envp = ctxp->env;
    const struct simulation *simp = ctxp->sim;
    const struct trial *trlp = ctxp->trl;

    while (true) {
        /* A simple tide model with astronomical and weather-driven tides */
        const double t = cmb_time();
        const double da0 = trlp->reference_depth;
        const double da1 = 1.0 * sin(2.0 * M_PI * t / 12.4);
        const double da2 = 0.5 * sin(2.0 * M_PI * t / 24.0);
        const double da3 = 0.25 * sin(2.0 * M_PI * t / (0.5 * 29.5 * 24));
        const double da = da0 + da1 + da2 + da3;

        /* Use wind speed as proxy for air pressure, assume on a west coast */
        const double dw1 = 0.5 * envp->wind_magnitude;
        const double dw2 = 0.5 * envp->wind_magnitude
                         * sin(envp->wind_direction * M_PI / 180.0);
        const double dw = dw1 - dw2;

        envp->water_depth = da + dw;

        /* Requesting the harbormaster to read the tide dial */
        cmb_condition_signal(simp->harbormaster);

        /* ... and wait until the next hour */
        cmb_process_hold(1.0);
    }
}

/* The demand predicate function for a ship wanting to dock */
bool is_ready_to_dock(const struct cmb_condition *cvp,
                      const struct cmb_process *pp,
                      const void *vctx) {
    cmb_unused(cvp);
    cmb_assert_debug(pp != NULL);
    cmb_assert_debug(vctx != NULL);

    const struct ship *shpp = (struct ship *)pp;
    const struct context *ctxp = vctx;
    const struct environment *envp = ctxp->env;
    const struct simulation *simp = ctxp->sim;

    if (envp->water_depth < shpp->min_depth) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Water %f m too shallow for %s, needs %f",
                        envp->water_depth, pp->name, shpp->min_depth);
        return false;
    }

    if (envp->wind_magnitude > shpp->max_wind){
        cmb_logger_user(stdout, USERFLAG1,
                        "Wind %f m/s too strong for %s, max %f",
                        envp->wind_magnitude, pp->name, shpp->max_wind);
        return false;
    }

    if (cmb_resourcestore_available(simp->tugs) < shpp->tugs_needed) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Not enough available tugs for %s",
                        pp->name);
        return false;
    }

    if (cmb_resourcestore_available(simp->berths[shpp->size]) < 1u) {
        cmb_logger_user(stdout, USERFLAG1,
                        "No available berth for %s",
                        pp->name);
        return false;
    }

    cmb_logger_user(stdout, USERFLAG1, "All good for %s", pp->name);
    return true;
}

/* The ship process function */
void *ship_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_debug(me != NULL);
    cmb_assert_debug(vctx != NULL);

    /* Unpack some convenient shortcut names */
    struct ship *shpp = (struct ship *)me;
    const struct context *ctxp = vctx;
    struct simulation *simp = ctxp->sim;
    struct cmb_condition *hbmp = simp->harbormaster;
    const struct trial *trlp = ctxp->trl;

    /* Note ourselves as active */
    cmb_logger_user(stdout, USERFLAG1, "%s arrives", me->name);
    const double t_arr = cmb_time();
    const uint64_t hndl = cmi_hashheap_enqueue(simp->active_ships, shpp,
                                               NULL, NULL, NULL, t_arr, 0u);

    /* Wait for suitable conditions to dock */
    while (!is_ready_to_dock(NULL, me, ctxp)) {
        /* Loop to catch any spurious wakeups, such as several ships waiting for
         * the tide and one of them grabbing the tugs before we can react. */
        cmb_condition_wait(hbmp, is_ready_to_dock, ctxp);
    }

    /* Resources are ready, grab them for ourselves */
    cmb_logger_user(stdout, USERFLAG1, "%s cleared to dock, acquires berth and tugs", me->name);
    cmb_resourcestore_acquire(simp->berths[shpp->size], 1u);
    cmb_resourcestore_acquire(simp->tugs, shpp->tugs_needed);

    /* Announce our intention to move */
    cmb_resource_acquire(simp->comms);
    cmb_process_hold(cmb_random_gamma(5.0, 0.01));
    cmb_resource_release(simp->comms);

    const double docking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(docking_time);

    /* Safely at the quay to unload cargo, dismiss the tugs for now */
    cmb_logger_user(stdout, USERFLAG1, "%s docked, releases tugs, unloading", me->name);
    cmb_resourcestore_release(simp->tugs, shpp->tugs_needed);
    const double tua = trlp->unloading_time_avg[shpp->size];
    const double unloading_time = cmb_random_PERT(0.75 * tua, tua, 2 * tua);
    cmb_process_hold(unloading_time);

    /* Need the tugs again to get out of here */
    cmb_logger_user(stdout, USERFLAG1, "%s ready to leave, requests tugs", me->name);
    cmb_resourcestore_acquire(simp->tugs, shpp->tugs_needed);

    /* Announce our intention to move */
    cmb_resource_acquire(simp->comms);
    cmb_process_hold(cmb_random_gamma(5.0, 0.01));
    cmb_resource_release(simp->comms);

    const double undocking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(undocking_time);

    /* Cleared berth, done with the tugs */
    cmb_logger_user(stdout, USERFLAG1, "%s left harbor, releases berth and tugs", me->name);
    cmb_resourcestore_release(simp->berths[shpp->size], 1u);
    cmb_resourcestore_release(simp->tugs, shpp->tugs_needed);

    /* One pass process, remove ourselves from the active set */
    cmi_hashheap_remove(simp->active_ships, hndl);
    /* List ourselves as departed instead */
    cmi_list_push(&(simp->departed_ships), shpp);
    /* Inform Davy Jones that we are coming his way */
    cmb_condition_signal(simp->davyjones);

    /* Store the time we spent as an exit value in a separate heap object.
     * The exit value is a void*, so we could store anything there, but for this
     * demo, we keep it simple. */
    const double t_dep = cmb_time();
    double *t_sys_p = malloc(sizeof(double));
    *t_sys_p = t_dep - t_arr;

    /* Note that returning from a process function has the same effect as calling
     * cmb_process_exit() with the return value as argument. */
    return t_sys_p;
}

/* The arrival process generating new ships */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctxp = vctx;
    const struct trial *trlp = ctxp->trl;
    const double mean = 1.0 / trlp->arrival_rate;
    const double p_large = trlp->percent_large;

    uint64_t cnt = 0u;
    while (true) {
        cmb_process_hold(cmb_random_exponential(mean));

        /* The ship class is a derived sub-class of cmb_process, we malloc it
         * directly instead of calling cmb_process_create() */
        struct ship *shpp = malloc(sizeof(struct ship));

        /* Remember to zero-initialize it if malloc'ing on your own! */
        memset(shpp, 0, sizeof(struct ship));

        /* We started the ship size enum from 0 to match array indexes. If we
         * had more size classes, we could use cmb_random_dice(0, n) instead. */
        shpp->size = cmb_random_bernoulli(p_large);

        /* We would probably not hard-code parameters except in a demo like this */
        shpp->max_wind = 10.0 + 2.0 * (double)(shpp->size);
        shpp->min_depth = 8.0 + 5.0 * (double)(shpp->size);
        shpp->tugs_needed = 1u + 2u * shpp->size;

        /* A ship needs a name */
        char namebuf[20];
        snprintf(namebuf, sizeof(namebuf),
                 "Ship_%06" PRIu64 "%s",
                 ++cnt, ((shpp->size == SMALL) ? "_small" : "_large"));
        cmb_process_initialize((struct cmb_process *)shpp, namebuf, ship_proc, vctx, 0);

        /* Start our brand new ship heading into the harbor */
        cmb_process_start((struct cmb_process *)shpp);
        cmb_logger_user(stdout, USERFLAG1, "%s started", namebuf);
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

    const struct context *ctxp = vctx;
    const struct simulation *simp = ctxp->sim;

    /* Simple: One or more ships in the list of departed ships */
    return (simp->departed_ships != NULL);
}

/* The departure process */
void *departure_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctxp = vctx;
    struct simulation *simp = ctxp->sim;
    const struct trial *trlp = ctxp->trl;
    struct cmi_list_tag **dep_head = &(simp->departed_ships);

    while (true) {
        /* We do not need to loop here, this is the only process waiting */
        cmb_condition_wait(simp->davyjones, is_departed, vctx);

        /* There is one, collect its exit value */
        struct ship *shpp = cmi_list_pop(dep_head);
        double *t_sys_p = cmb_process_get_exit_value((struct cmb_process *)shpp);
        cmb_assert_debug(t_sys_p != NULL);
        cmb_logger_user(stdout, USERFLAG1,
                        "Recycling %s, time in system %f",
                        ((struct cmb_process *)shpp)->name,
                        *t_sys_p);

        if (cmb_time() > trlp->warmup_time) {
            /* Add it to the statistics */
            cmb_dataset_add(simp->time_in_system[shpp->size], *t_sys_p);
        }

        /* Frees internally allocated memory, but not the object itself */
        cmb_process_terminate((struct cmb_process *)shpp);
        /* We malloc'ed it, call free() directly instead of cmb_process_destroy() */
        free(shpp);
        /* The exit value was malloc'ed in the ship process, free it as well */
        free(t_sys_p);
    }
}

/* Event to close down the simulation. */
void end_sim(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctxp = object;
    const struct simulation *simp = ctxp->sim;
    cmb_logger_user(stdout, USERFLAG1, "Simulation ended");

    cmb_process_stop(simp->weather, NULL);
    cmb_process_stop(simp->tide, NULL);
    cmb_process_stop(simp->arrivals, NULL);
    cmb_process_stop(simp->departures, NULL);

    /* Also stop and recycle any still active ships */
    while (cmi_hashheap_count(simp->active_ships) > 0u) {
        void **item = cmi_hashheap_dequeue(simp->active_ships);
        struct ship *shpp = item[0];
        cmb_process_stop((struct cmb_process *)shpp, NULL);
        cmb_process_terminate((struct cmb_process *)shpp);
        free(shpp);
    }

    cmb_event_queue_clear();
}

static void start_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctxp = object;
    const struct simulation *simp = ctxp->sim;

    cmb_resourcestore_start_recording(simp->tugs);
    for (int i = 0; i < 2; i++) {
        cmb_resourcestore_start_recording(simp->berths[i]);
    }
}

static void stop_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctxp = object;
    const struct simulation *simp = ctxp->sim;

    cmb_resourcestore_start_recording(simp->tugs);
    for (int i = 0; i < 2; i++) {
        cmb_resourcestore_stop_recording(simp->berths[i]);
    }
}

/* The simulation driver function to execute one trial */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trlp = vtrl;

    /* Using local variables, will only be used before this function exits */
    struct environment env = {0};
    struct simulation sim = {0};
    struct context ctx = { &sim, &env, trlp };

    /* Set up our trial housekeeping */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);
    trlp->seed_used = cmb_random_get_hwseed();
    cmb_random_initialize(trlp->seed_used);

    /* Create and initialize the statistics collectors */
    for (int i = 0; i < 2; i++) {
        sim.time_in_system[i] = cmb_dataset_create();
        cmb_dataset_initialize(sim.time_in_system[i]);
        trlp->avg_time_in_system[i] = 0.0;
    }

    /* Create weather and tide processes, ensuring that weather goes first */
    sim.weather = cmb_process_create();
    cmb_process_initialize(sim.weather, "Wind", weather_proc, &ctx, 1);
    cmb_process_start(sim.weather);
    sim.tide = cmb_process_create();
    cmb_process_initialize(sim.tide, "Depth", tide_proc, &ctx, 0);
    cmb_process_start(sim.tide);

    /* Create the resources */
    sim.comms = cmb_resource_create();
    cmb_resource_initialize(sim.comms, "Comms");
    sim.tugs = cmb_resourcestore_create();
    cmb_resourcestore_initialize(sim.tugs, "Tugs", trlp->num_tugs);
    for (int i = 0; i < 2; i++) {
        sim.berths[i] = cmb_resourcestore_create();
        cmb_resourcestore_initialize(sim.berths[i],
            ((i == 0)? "Small berth" : "Large berth"),
            trlp->num_berths[i]);
    }

    /* Create the harbormaster and Davy Jones himself */
    sim.harbormaster = cmb_condition_create();
    cmb_condition_initialize(sim.harbormaster, "Harbormaster");
    cmb_resourceguard_register(&(sim.tugs->guard), &(sim.harbormaster->guard));
    for (int i = 0; i < 2; i++) {
        cmb_resourceguard_register(&(sim.berths[i]->guard), &(sim.harbormaster->guard));
    }

    sim.davyjones = cmb_condition_create();
    cmb_condition_initialize(sim.davyjones, "Davy Jones");

    /* Create the arrival and departure processes */
    sim.arrivals = cmb_process_create();
    cmb_process_initialize(sim.arrivals, "Arrivals", arrival_proc, &ctx, 0);
    cmb_process_start(sim.arrivals);
    sim.departures = cmb_process_create();
    cmb_process_initialize(sim.departures, "Departures", departure_proc, &ctx, 0);
    cmb_process_start(sim.departures);

    /* Create the collections of active and departed ships */
    sim.active_ships = cmi_hashheap_create();
    cmi_hashheap_initialize(sim.active_ships, 3u, NULL);
    sim.departed_ships = NULL;

    /* Schedule the simulation control events */
    double t = trlp->warmup_time;
    cmb_event_schedule(start_rec, NULL, &ctx, t, 0);
    t += trlp->duration;
    cmb_event_schedule(stop_rec, NULL, &ctx, t, 0);
    /* Set a large negative priority for the stop event to ensure normal events go first */
    cmb_event_schedule(end_sim, NULL, &ctx, t, -100);

    /* Run this trial */
    cmb_event_queue_execute();

    /* Report statistics, using built-in history statistics for the resources */
    for (int i = 0; i < 2; i++) {
        printf("\nSystem times for %s ships:\n", ((i == 0) ? "small" : "large"));
        const unsigned n = cmb_dataset_count(sim.time_in_system[i]);
        if (n > 0) {
            struct cmb_datasummary dsumm;
            cmb_dataset_summarize(sim.time_in_system[i], &dsumm);
            cmb_datasummary_print(&dsumm, stdout, true);
            cmb_dataset_print_histogram(sim.time_in_system[i], stdout, 20, 0.0, 0.0);
            trlp->avg_time_in_system[i] = cmb_datasummary_mean(&dsumm);
        }
    }

    for (int i = 0; i < 2; i++) {
        printf("\nUtilization of %s berths:\n", ((i == 0) ? "small" : "large"));
        const struct cmb_timeseries *hist = cmb_resourcestore_get_history(sim.berths[i]);
        const unsigned n = cmb_timeseries_count(hist);
        if (n > 0) {
            struct cmb_wtdsummary wsumm;
            cmb_timeseries_summarize(hist, &wsumm);
            cmb_wtdsummary_print(&wsumm, stdout, true);
            const unsigned nvals = (unsigned)cmb_wtdsummary_max(&wsumm);
            cmb_timeseries_print_histogram(hist, stdout, nvals, 0.0, (double)nvals);
        }
    }

    printf("\nUtilization of tugs:\n");
    const struct cmb_timeseries *hist = cmb_resourcestore_get_history(sim.tugs);
    const unsigned n = cmb_timeseries_count(hist);
    if (n > 0) {
        struct cmb_wtdsummary wsumm;
        cmb_timeseries_summarize(hist, &wsumm);
        cmb_wtdsummary_print(&wsumm, stdout, true);
        const unsigned nvals = (unsigned)cmb_wtdsummary_max(&wsumm);
        cmb_timeseries_print_histogram(hist, stdout, nvals, 0.0, (double)nvals);
    }

    /* Clean up */
    for (int i = 0; i < 2; i++) {
        cmb_dataset_destroy(sim.time_in_system[i]);
        cmb_resourcestore_destroy(sim.berths[i]);
    }

    cmb_condition_destroy(sim.harbormaster);
    cmb_condition_destroy(sim.davyjones);
    cmb_resourcestore_destroy(sim.tugs);
    cmb_process_destroy(sim.weather);
    cmb_process_destroy(sim.tide);

    /* Final housekeeping to leave everything as we found it */
    cmb_event_queue_terminate();
    cmb_random_terminate();
}

/* Temporary function to load trial test data for the single-threaded development version. */
void load_params(struct trial *trlp)
{
    cmb_assert_release(trlp != NULL);

    trlp->mean_wind = 5.0;
    trlp->reference_depth = 15.0;
    trlp->arrival_rate = 0.5;
    trlp->percent_large = 0.25;
    trlp->num_tugs = 10;
    trlp->num_berths[SMALL] = 6;
    trlp->num_berths[LARGE] = 3;
    trlp->unloading_time_avg[SMALL] = 8.0;
    trlp->unloading_time_avg[LARGE] = 12.0;

    trlp->warmup_time = 24.0;
    trlp->duration = 24.0 * 7 * 52;
}

/* The minimal single-threaded main function */
int main(void)
{
    struct trial trl = {};
    load_params(&trl);

    run_trial(&trl);

    printf("Avg time in system, small ships: %f\n", trl.avg_time_in_system[SMALL]);
    printf("Avg time in system, large ships: %f\n", trl.avg_time_in_system[LARGE]);

    return 0;
}
