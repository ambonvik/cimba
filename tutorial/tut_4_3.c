/*
 * tutorial/tut_4_3.c
 *
 * Multithreaded version of the harbor simulation, with abandoned trials
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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
#include <time.h>

/* Bit masks to distinguish between two types of user-defined logging messages. */
#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

/* Sizes of arrays to be initialized */
#define N_SCENARIOS 3u
#define N_PARAMS    4u
#define N_LEVELS    5u
#define N_SIZES     2u
#define N_REPS      10u

/*
 * Baseline parameters - can be global because const and because only used
 * outside the multithreading when loading the experiment array with trials.
 */
const double mean_wind = 5.0;
const double arrival_rate[N_SCENARIOS] = { 0.5, 0.55, 0.625 };
const double percent_large = 0.25;
const double ref_depth[N_LEVELS] = { 15.0, 15.5, 16.0, 16.5, 17.0 };
const unsigned num_tugs[N_LEVELS] = { 10u, 11u, 12u, 13u, 14u };
const unsigned num_berths[N_SIZES][N_LEVELS] = { { 6, 7, 8, 9, 10 },
                                                 { 3, 4, 5, 6, 7 } };
const double unloading_time_avg[N_SIZES] = { 8.0, 12.0 };

const double warmup_h = 24.0 * 30;
const double duration_h = 24.0 * 365;

/* This implicitly assumes that N_SIZES == 2 */
enum ship_size {
    SMALL = 0,
    LARGE
};

/* Our simulated world consists of these entities. */
struct simulation {
    /* Environmental processes */
    struct cmb_process *weather;
    struct cmb_process *tide;
    /* Comings and goings */
    struct cmb_process *arrivals;
    struct cmb_process *departures;

    /* The fleet of tugboats */
    struct cmb_resourcepool *tugs;
    /* Small and large berths */
    struct cmb_resourcepool *berths[N_SIZES];
    /* The radio channel */
    struct cmb_resource *comms;

    /* A condition variable permitting docking */
    struct cmb_condition *harbormaster;
    /* A condition variable monitoring departures */
    struct cmb_condition *davyjones;

    /* A set of all active ships */
    struct cmi_hashheap active_ships;
    /* A list of departed ships  */
    struct cmi_slist_node departed_ships;

    /* Data collector for local use in this instance */
    struct cmb_dataset *time_in_system[N_SIZES];
};

/* Variables describing the state of the environment around our entities */
struct environment {
    double wind_magnitude;
    double wind_direction;
    double water_depth;
};

/* A single trial is defined by these parameters and generates these results. */
struct trial {
    /* Model parameters */
    double mean_wind;
    double reference_depth;
    double arrival_rate;
    double percent_large;
    unsigned num_tugs;
    unsigned num_berths[N_SIZES];
    double unloading_time_avg[N_SIZES];

    /* Control parameters */
    double warmup_s;
    double duration_h;

    /* Results */
    uint64_t seed_used;
    double avg_time_in_system[N_SIZES];
};

struct context {
    struct simulation *sim;
    struct environment *env;
    struct trial *trl;
};

/* A ship is a derived class from cmb_process */
struct ship {
    struct cmb_process core;       /* <= Note: The real thing, not a pointer */
    enum ship_size size;
    unsigned tugs_needed;
    double max_wind;
    double min_depth;
    struct cmi_slist_node listnode;
};

/* We'll do the object lifecycle properly with constructors and destructors. */
struct ship *ship_create(void)
{
    struct ship *shpp = malloc(sizeof(struct ship));
    memset(shpp, 0, sizeof(*shpp));
    cmb_assert_release(shpp != NULL);

    return shpp;
}

/* Process function to be defined later, for now just declare that it exists */
void *ship_proc(struct cmb_process *me, void *vctx);

void ship_initialize(struct ship *shpp, const enum ship_size sz, uint64_t cnt, void *vctx)
{
    cmb_assert_release(shpp != NULL);
    shpp->size = sz;

    /* We would probably not hard-code parameters except in a demo like this */
    shpp->max_wind = 10.0 + 2.0 * (double)(shpp->size);
    shpp->min_depth = 8.0 + 5.0 * (double)(shpp->size);
    shpp->tugs_needed = 1u + 2u * shpp->size;

    char namebuf[20];
    snprintf(namebuf, sizeof(namebuf),
             "Ship_%06" PRIu64 "%s",
             ++cnt, ((shpp->size == SMALL) ? "_small" : "_large"));

    /* Done initializing the child class properties, pass it on to the parent class */
    cmb_process_initialize((struct cmb_process *)shpp, namebuf, ship_proc, vctx, 0);
}

void ship_terminate(struct ship *shpp)
{
    /* Nothing needed for the ship itself, pass it on to parent class */
    cmb_process_terminate((struct cmb_process *)shpp);
}

void ship_destroy(struct ship *shpp)
{
    free(shpp);
}

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
        envp->wind_direction = cmb_random_PERT(0.0, 225.0, 360.0);

        /* We could request the harbormaster to read the new weather bulletin:
         *       cmb_condition_signal(simp->harbormaster);
         * but it will be signaled by the tide process in a moment anyway,
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

        /* Use wind speed as a proxy for air pressure, assume on a west coast */
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

    if (cmb_resourcepool_available(simp->tugs) < shpp->tugs_needed) {
        cmb_logger_user(stdout, USERFLAG1,
                        "Not enough available tugs for %s",
                        pp->name);
        return false;
    }

    if (cmb_resourcepool_available(simp->berths[shpp->size]) < 1u) {
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
    const uint64_t hndl = cmi_hashheap_enqueue(&(simp->active_ships), shpp,
                                               NULL, NULL, NULL, 0u, t_arr, 0u);

    /* Wait for suitable conditions to dock */
    while (!is_ready_to_dock(NULL, me, ctxp)) {
        /* Loop to catch any spurious wakeups, such as several ships waiting for
         * the tide and one of them grabbing the tugs before we can react. */
        cmb_condition_wait(hbmp, is_ready_to_dock, ctxp);
    }

    /* Resources are ready, grab them for ourselves */
    cmb_logger_user(stdout, USERFLAG1, "%s cleared to dock, acquires berth and tugs", me->name);
    cmb_resourcepool_acquire(simp->berths[shpp->size], 1u);
    cmb_resourcepool_acquire(simp->tugs, shpp->tugs_needed);

    /* Announce our intention to move */
    cmb_resource_acquire(simp->comms);
    cmb_process_hold(cmb_random_gamma(5.0, 0.01));
    cmb_resource_release(simp->comms);

    const double docking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(docking_time);

    /* Simulate a trial-abandoning error condition */
    if (cmb_random_bernoulli(1e-5)) {
        cmb_logger_error(stdout, "Randomly abandoning trial");
    }

    /* Safely at the quay to unload cargo, dismiss the tugs for now */
    cmb_logger_user(stdout, USERFLAG1, "%s docked, releases tugs, unloading", me->name);
    cmb_resourcepool_release(simp->tugs, shpp->tugs_needed);
    const double tua = trlp->unloading_time_avg[shpp->size];
    const double unloading_time = cmb_random_PERT(0.75 * tua, tua, 2 * tua);
    cmb_process_hold(unloading_time);

    /* Need the tugs again to get out of here */
    cmb_logger_user(stdout, USERFLAG1, "%s ready to leave, requests tugs", me->name);
    cmb_resourcepool_acquire(simp->tugs, shpp->tugs_needed);

    /* Announce our intention to move */
    cmb_resource_acquire(simp->comms);
    cmb_process_hold(cmb_random_gamma(5.0, 0.01));
    cmb_resource_release(simp->comms);

    const double undocking_time = cmb_random_PERT(0.4, 0.5, 0.8);
    cmb_process_hold(undocking_time);

    /* Cleared berth, done with the tugs */
    cmb_logger_user(stdout, USERFLAG1, "%s left harbor, releases berth and tugs", me->name);
    cmb_resourcepool_release(simp->berths[shpp->size], 1u);
    cmb_resourcepool_release(simp->tugs, shpp->tugs_needed);

    /* One pass process, remove ourselves from the active set */
    cmi_hashheap_remove(&(simp->active_ships), hndl);
    /* List ourselves as departed instead */
    cmi_slist_push(&(simp->departed_ships), &(shpp->listnode));
    /* Inform Davy Jones that we are coming his way */
    cmb_condition_signal(simp->davyjones);

    /* Store the time we spent as an exit value in a separate heap object.
     * The exit value is a void*, so we could store anything there, but for this
     * demo, we keep it simple. */
    const double t_dep = cmb_time();
    double *t_sys_p = malloc(sizeof(double));
    *t_sys_p = t_dep - t_arr;

    /* Note that returning from a process function has the same effect as calling
     * cmb_process_exit() with the return value as the argument. */
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

        struct ship *shpp = ship_create();
        const enum ship_size sz = cmb_random_bernoulli(p_large);
        ship_initialize(shpp, sz, ++cnt, vctx);

        /* Start our new ship heading into the harbor */
        cmb_process_start((struct cmb_process *)shpp);
        cmb_logger_user(stdout, USERFLAG1, "%s started",
                        ((struct cmb_process *)shpp)->name);
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
    return !(cmi_slist_is_empty(&(simp->departed_ships)));
}

/* The departure process */
void *departure_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctxp = vctx;
    struct simulation *simp = ctxp->sim;
    const struct trial *trlp = ctxp->trl;
    struct cmi_slist_node *dep_head = &(simp->departed_ships);

    while (true) {
        /* We do not need to loop here, since this is the only process waiting */
        cmb_condition_wait(simp->davyjones, is_departed, vctx);

        /* There is one, collect its exit value */
        struct cmi_slist_node *snode = cmi_slist_pop(dep_head);
        struct ship *shp = cmi_slist_entry(snode, struct ship, listnode);
        double *t_sys_p = cmb_process_exit_value((struct cmb_process *)shp);
        cmb_assert_debug(t_sys_p != NULL);
        cmb_logger_user(stdout, USERFLAG1,
                        "Recycling %s, time in system %f",
                        ((struct cmb_process *)shp)->name,
                        *t_sys_p);

        if (cmb_time() > trlp->warmup_s) {
            /* Add it to the statistics */
            cmb_dataset_add(simp->time_in_system[shp->size], *t_sys_p);
        }

        ship_terminate(shp);
        ship_destroy(shp);

        /* The exit value was malloc'ed in the ship process, free it as well */
        free(t_sys_p);
    }
}

/* Event to close down the simulation. */
void end_sim(void *subject, void *object)
{
    cmb_unused(subject);

    struct context *ctxp = object;
    struct simulation *simp = ctxp->sim;
    cmb_logger_user(stdout, USERFLAG1, "Simulation ended");

    cmb_process_stop(simp->weather, NULL);
    cmb_process_stop(simp->tide, NULL);
    cmb_process_stop(simp->arrivals, NULL);
    cmb_process_stop(simp->departures, NULL);

    /* Also stop and recycle any still active ships */
    while (cmi_hashheap_count(&(simp->active_ships)) > 0u) {
        void **item = cmi_hashheap_dequeue(&(simp->active_ships));
        struct ship *shpp = item[0];
        cmb_process_stop((struct cmb_process *)shpp, NULL);
        ship_terminate(shpp);
        ship_destroy(shpp);
    }
}

static void start_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctxp = object;
    const struct simulation *simp = ctxp->sim;

    cmb_resourcepool_start_recording(simp->tugs);
    for (int i = 0; i < 2; i++) {
        cmb_resourcepool_start_recording(simp->berths[i]);
    }
}

static void stop_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctxp = object;
    const struct simulation *simp = ctxp->sim;

    cmb_resourcepool_start_recording(simp->tugs);
    for (int i = 0; i < 2; i++) {
        cmb_resourcepool_stop_recording(simp->berths[i]);
    }
}

/* Our very own cleanup for abandoned trials */
void trial_cleanup(void *vctx)
{
    cmb_assert_always(vctx != NULL);

    struct context *ctxp = (struct context *)vctx;
    struct simulation *simp = ctxp->sim;
    struct environment *envp = ctxp->env;

    /* Stop and recycle any still active ships */
    while (cmi_hashheap_count(&(simp->active_ships)) > 0u) {
        void **item = cmi_hashheap_dequeue(&(simp->active_ships));
        struct ship *shpp = item[0];
        cmb_process_stop((struct cmb_process *)shpp, NULL);
        ship_terminate(shpp);
        ship_destroy(shpp);
    }

    /* Any departed ships waiting? */
    struct cmi_slist_node *dep_head = &(simp->departed_ships);
    while (!cmi_slist_is_empty(dep_head)) {
        struct cmi_slist_node *snode = cmi_slist_pop(dep_head);
        struct ship *shp = cmi_slist_entry(snode, struct ship, listnode);
        double *t_sys_p = cmb_process_exit_value((struct cmb_process *)shp);
        ship_terminate(shp);
        ship_destroy(shp);
        free(t_sys_p);
    }

    cmi_hashheap_terminate(&(simp->active_ships));
    cmi_slist_terminate(&(simp->departed_ships));

    free(envp);
    free(simp);
    free(ctxp);
}


/* The simulation driver function to execute one trial */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trlp = vtrl;

    /* Heap allocated for this tutorial */
    struct environment *envp = malloc(sizeof(*envp));
    cmb_assert_always(envp != NULL);
    memset(envp, 0, sizeof(*envp));
    struct simulation *simp = malloc(sizeof(*simp));
    cmb_assert_always(simp != NULL);
    memset(simp, 0, sizeof(*simp));
    struct context *ctxp = malloc(sizeof(*ctxp));
    cmb_assert_always(ctxp != NULL);
    memset(ctxp, 0, sizeof(*ctxp));
    ctxp->env = envp;
    ctxp->sim = simp;
    ctxp->trl = trlp;

    /* Set up our trial housekeeping */
    cimba_trial_cleanup_set(trial_cleanup, ctxp);
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);
    trlp->seed_used = cmb_random_hwseed();
    cmb_random_initialize(trlp->seed_used);

    cmb_logger_user(stdout, USERFLAG2, "Started, seed 0x%016" PRIx64, trlp->seed_used);

    /* Create and initialize the statistics collectors */
    for (unsigned i = 0; i < N_SIZES; i++) {
        simp->time_in_system[i] = cmb_dataset_create();
        cmb_dataset_initialize(simp->time_in_system[i]);
        trlp->avg_time_in_system[i] = 0.0;
    }

    /* Create weather and tide processes, ensuring that weather goes first */
    simp->weather = cmb_process_create();
    cmb_process_initialize(simp->weather, "Wind", weather_proc, ctxp, 1);
    cmb_process_start(simp->weather);
    simp->tide = cmb_process_create();
    cmb_process_initialize(simp->tide, "Depth", tide_proc, ctxp, 0);
    cmb_process_start(simp->tide);

    /* Create the resources */
    simp->comms = cmb_resource_create();
    cmb_resource_initialize(simp->comms, "Comms");
    simp->tugs = cmb_resourcepool_create();
    cmb_resourcepool_initialize(simp->tugs, "Tugs", trlp->num_tugs);
    for (unsigned i = 0; i < N_SIZES; i++) {
        simp->berths[i] = cmb_resourcepool_create();
        cmb_resourcepool_initialize(simp->berths[i],
            ((i == 0)? "Small berth" : "Large berth"),
            trlp->num_berths[i]);
    }

    /* Create the harbormaster and Davy Jones himself */
    simp->harbormaster = cmb_condition_create();
    cmb_condition_initialize(simp->harbormaster, "Harbormaster");
    cmb_resourceguard_register(&(simp->tugs->guard), &(simp->harbormaster->guard));
    for (unsigned i = 0; i < N_SIZES; i++) {
        cmb_resourceguard_register(&(simp->berths[i]->guard), &(simp->harbormaster->guard));
    }

    simp->davyjones = cmb_condition_create();
    cmb_condition_initialize(simp->davyjones, "Davy Jones");

    /* Create the arrival and departure processes */
    simp->arrivals = cmb_process_create();
    cmb_process_initialize(simp->arrivals, "Arrivals", arrival_proc, ctxp, 0);
    cmb_process_start(simp->arrivals);
    simp->departures = cmb_process_create();
    cmb_process_initialize(simp->departures, "Departures", departure_proc, ctxp, 0);
    cmb_process_start(simp->departures);

    /* Create the collections of active and departed ships */
    cmi_hashheap_initialize(&(simp->active_ships), 3u, NULL);
    cmi_slist_initialize(&(simp->departed_ships));

    /* Schedule the simulation control events */
    double t = trlp->warmup_s;
    cmb_event_schedule(start_rec, NULL, ctxp, t, 0);
    t += trlp->duration_h;
    cmb_event_schedule(stop_rec, NULL, ctxp, t, 0);
    /* Set a large negative priority for the stop event to ensure normal events go first */
    cmb_event_schedule(end_sim, NULL, ctxp, t, -100);

    /* Run this trial */
    cmb_event_queue_execute();

    /* Report statistics, using built-in history statistics for the resources */
    for (unsigned i = 0; i < N_SIZES; i++) {
        struct cmb_datasummary dstmp;
        cmb_datasummary_initialize(&dstmp);
        cmb_dataset_summarize(simp->time_in_system[i], &dstmp);
        trlp->avg_time_in_system[i] = cmb_datasummary_mean(&dstmp);
        cmb_datasummary_terminate(&dstmp);
    }

    /* Clean up */
    cmb_process_terminate(simp->weather);
    cmb_process_destroy(simp->weather);
    cmb_process_terminate(simp->tide);
    cmb_process_destroy(simp->tide);
    cmb_process_terminate(simp->arrivals);
    cmb_process_destroy(simp->arrivals);
    cmb_process_terminate(simp->departures);
    cmb_process_destroy(simp->departures);

    for (unsigned i = 0; i < N_SIZES; i++) {
        cmb_dataset_terminate(simp->time_in_system[i]);
        cmb_dataset_destroy(simp->time_in_system[i]);
        cmb_resourcepool_terminate(simp->berths[i]);
        cmb_resourcepool_destroy(simp->berths[i]);
    }

    cmb_condition_terminate(simp->harbormaster);
    cmb_condition_destroy(simp->harbormaster);
    cmb_condition_terminate(simp->davyjones);
    cmb_condition_destroy(simp->davyjones);
    cmb_resourcepool_terminate(simp->tugs);
    cmb_resourcepool_destroy(simp->tugs);
    cmb_resource_terminate(simp->comms);
    cmb_resource_destroy(simp->comms);

    cmi_hashheap_terminate(&(simp->active_ships));
    cmi_slist_terminate(&(simp->departed_ships));

    free(ctxp->env);
    free(ctxp->sim);
    free(ctxp);

    /* Final housekeeping to leave everything as we found it */
    cmb_event_queue_terminate();
    cmb_random_terminate();

    cmb_logger_user(stdout, USERFLAG2,
                    "Finished normally, seed 0x%016" PRIx64,
                    trlp->seed_used);
}

void write_gnuplot_commands(void);
double t_crit_95(uint32_t n);

int main(void)
{
    printf("Cimba version %s\n", cimba_version());
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    printf("Setting up experiment\n");
    const unsigned n_trials = N_SCENARIOS * N_PARAMS * N_LEVELS * N_REPS;
    struct trial *experiment = calloc(n_trials, sizeof(*experiment));
    cmb_assert_release(experiment != NULL);

    unsigned ui_trl = 0u;
    for (unsigned ui_sc = 0u; ui_sc < N_SCENARIOS; ui_sc++) {
        const double sc_arr_rate = arrival_rate[ui_sc];

        /* Varying the dredging levels */
        for (unsigned ui_dr = 0; ui_dr < N_LEVELS; ui_dr++) {
            const double lvl_dr = ref_depth[ui_dr];
            /* The replications, everything else baseline */
            for (unsigned ui_rp = 0u; ui_rp < N_REPS; ui_rp++) {
                experiment[ui_trl].mean_wind = mean_wind;
                experiment[ui_trl].reference_depth = lvl_dr;
                experiment[ui_trl].arrival_rate = sc_arr_rate;
                experiment[ui_trl].percent_large = percent_large;
                experiment[ui_trl].num_tugs = num_tugs[0];
                experiment[ui_trl].num_berths[SMALL] = num_berths[SMALL][0];
                experiment[ui_trl].num_berths[LARGE] = num_berths[LARGE][0];
                experiment[ui_trl].unloading_time_avg[SMALL] = unloading_time_avg[SMALL];
                experiment[ui_trl].unloading_time_avg[LARGE] = unloading_time_avg[LARGE];

                experiment[ui_trl].warmup_s = warmup_h;
                experiment[ui_trl].duration_h = duration_h;

                experiment[ui_trl].avg_time_in_system[SMALL] = -1.0;
                experiment[ui_trl].avg_time_in_system[LARGE] = -1.0;

                ui_trl++;
            }
        }

        /* Varying the number of tugboats */
        for (unsigned ui_nt = 0; ui_nt < N_LEVELS; ui_nt++) {
            const unsigned lvl_ntugs = num_tugs[ui_nt];
            /* The replications, everything else baseline */
            for (unsigned ui_rp = 0u; ui_rp < N_REPS; ui_rp++) {
                experiment[ui_trl].mean_wind = mean_wind;
                experiment[ui_trl].reference_depth = ref_depth[0];
                experiment[ui_trl].arrival_rate = sc_arr_rate;
                experiment[ui_trl].percent_large = percent_large;
                experiment[ui_trl].num_tugs = lvl_ntugs;
                experiment[ui_trl].num_berths[SMALL] = num_berths[SMALL][0];
                experiment[ui_trl].num_berths[LARGE] = num_berths[LARGE][0];
                experiment[ui_trl].unloading_time_avg[SMALL] = unloading_time_avg[SMALL];
                experiment[ui_trl].unloading_time_avg[LARGE] = unloading_time_avg[LARGE];

                experiment[ui_trl].avg_time_in_system[SMALL] = -1.0;
                experiment[ui_trl].avg_time_in_system[LARGE] = -1.0;

                experiment[ui_trl].warmup_s = warmup_h;
                experiment[ui_trl].duration_h = duration_h;

                ui_trl++;
            }
        }

        /* Varying the number of small berths */
        for (unsigned ui_nsb = 0; ui_nsb < N_LEVELS; ui_nsb++) {
            const unsigned lvl_nsb = num_berths[SMALL][ui_nsb];
            /* The replications, everything else baseline */
            for (unsigned ui_rp = 0u; ui_rp < N_REPS; ui_rp++) {
                experiment[ui_trl].mean_wind = mean_wind;
                experiment[ui_trl].reference_depth = ref_depth[0];
                experiment[ui_trl].arrival_rate = sc_arr_rate;
                experiment[ui_trl].percent_large = percent_large;
                experiment[ui_trl].num_tugs = num_tugs[0];
                experiment[ui_trl].num_berths[SMALL] = lvl_nsb;
                experiment[ui_trl].num_berths[LARGE] = num_berths[LARGE][0];
                experiment[ui_trl].unloading_time_avg[SMALL] = unloading_time_avg[SMALL];
                experiment[ui_trl].unloading_time_avg[LARGE] = unloading_time_avg[LARGE];

                experiment[ui_trl].avg_time_in_system[SMALL] = -1.0;
                experiment[ui_trl].avg_time_in_system[LARGE] = -1.0;

                experiment[ui_trl].warmup_s = warmup_h;
                experiment[ui_trl].duration_h = duration_h;

                ui_trl++;
            }
        }

        /* Varying the number of large berths */
        for (unsigned ui_nlb = 0; ui_nlb < N_LEVELS; ui_nlb++) {
            const unsigned lvl_nlb = num_berths[LARGE][ui_nlb];
            /* The replications, everything else baseline */
            for (unsigned ui_rp = 0u; ui_rp < N_REPS; ui_rp++) {
                experiment[ui_trl].mean_wind = mean_wind;
                experiment[ui_trl].reference_depth = ref_depth[0];
                experiment[ui_trl].arrival_rate = sc_arr_rate;
                experiment[ui_trl].percent_large = percent_large;
                experiment[ui_trl].num_tugs = num_tugs[0];
                experiment[ui_trl].num_berths[SMALL] = num_berths[SMALL][0];
                experiment[ui_trl].num_berths[LARGE] = lvl_nlb;
                experiment[ui_trl].unloading_time_avg[SMALL] = unloading_time_avg[SMALL];
                experiment[ui_trl].unloading_time_avg[LARGE] = unloading_time_avg[LARGE];

                experiment[ui_trl].avg_time_in_system[SMALL] = -1.0;
                experiment[ui_trl].avg_time_in_system[LARGE] = -1.0;

                experiment[ui_trl].warmup_s = warmup_h;
                experiment[ui_trl].duration_h = duration_h;

                ui_trl++;
            }
        }
    }

    printf("Configured %u trials\n", ui_trl);
    printf("Executing experiment\n");
    const uint64_t nfailed = cimba_run(experiment, n_trials, sizeof(*experiment), run_trial);
    printf("Experiment finished, %" PRIu64 " failed trials, %" PRIu64 " successful\n",
            nfailed, ui_trl - nfailed);

    ui_trl = 0u;
    FILE *datafp = fopen("tut_4_2.dat", "w");
    fprintf(datafp, "# arr_rate\tref_depth\tn_tg\tn_bts\tn_btl\tavg_t_small\tci_t_small\tavg_t_large\tci_t_small\n");
    for (unsigned ui_sc = 0u; ui_sc < N_SCENARIOS; ui_sc++) {
        /* Dredging levels */
        for (unsigned ui_dr = 0; ui_dr < N_LEVELS; ui_dr++) {
            double smpl_arr = experiment[ui_trl].arrival_rate;
            double smpl_refdep = experiment[ui_trl].reference_depth;
            unsigned smpl_ntugs = experiment[ui_trl].num_tugs;
            unsigned smpl_nsmallbts = experiment[ui_trl].num_berths[SMALL];
            unsigned smpl_nlargebts = experiment[ui_trl].num_berths[LARGE];

            struct cmb_datasummary ds_small;
            struct cmb_datasummary ds_large;
            cmb_datasummary_initialize(&ds_small);
            cmb_datasummary_initialize(&ds_large);
            for (unsigned ui_rep = 0u; ui_rep < N_REPS; ui_rep++) {
                if (experiment[ui_trl].avg_time_in_system[SMALL] != -1.0) {
                    cmb_datasummary_add(&ds_small, experiment[ui_trl].avg_time_in_system[SMALL]);
                    cmb_datasummary_add(&ds_large, experiment[ui_trl].avg_time_in_system[LARGE]);
                }
                ui_trl++;
            }

            const double smpl_cnt_small = cmb_datasummary_count(&ds_small);
            const double smpl_avg_small = cmb_datasummary_mean(&ds_small);
            const double smpl_sd_small = cmb_datasummary_stddev(&ds_small);
            const double t_crit_small = t_crit_95(smpl_cnt_small);

            const double smpl_cnt_large = cmb_datasummary_count(&ds_large);
            const double smpl_avg_large = cmb_datasummary_mean(&ds_large);
            const double smpl_sd_large = cmb_datasummary_stddev(&ds_large);
            const double t_crit_large = t_crit_95(smpl_cnt_large);

            fprintf(datafp, "%f\t%f\t%u\t%u\t%u\t%f\t%f\t%f\t%f\n",
                    smpl_arr, smpl_refdep, smpl_ntugs,
                    smpl_nsmallbts, smpl_nlargebts,
                    smpl_avg_small, t_crit_small * smpl_sd_small,
                    smpl_avg_large, t_crit_large * smpl_sd_large);
            cmb_datasummary_terminate(&ds_small);
            cmb_datasummary_terminate(&ds_large);
        }

        fprintf(datafp, "\n\n");

        /* Number of tugs */
        for (unsigned ui_nt = 0; ui_nt < N_LEVELS; ui_nt++) {
            double smpl_arr = experiment[ui_trl].arrival_rate;
            double smpl_refdep = experiment[ui_trl].reference_depth;
            unsigned smpl_ntugs = experiment[ui_trl].num_tugs;
            unsigned smpl_nsmallbts = experiment[ui_trl].num_berths[SMALL];
            unsigned smpl_nlargebts = experiment[ui_trl].num_berths[LARGE];

            struct cmb_datasummary ds_small;
            struct cmb_datasummary ds_large;
            cmb_datasummary_initialize(&ds_small);
            cmb_datasummary_initialize(&ds_large);
            for (unsigned ui_rep = 0u; ui_rep < N_REPS; ui_rep++) {
                if (experiment[ui_trl].avg_time_in_system[SMALL] != -1.0) {
                    cmb_datasummary_add(&ds_small, experiment[ui_trl].avg_time_in_system[SMALL]);
                    cmb_datasummary_add(&ds_large, experiment[ui_trl].avg_time_in_system[LARGE]);
                }
                ui_trl++;
            }

            const double smpl_cnt_small = cmb_datasummary_count(&ds_small);
            const double smpl_avg_small = cmb_datasummary_mean(&ds_small);
            const double smpl_sd_small = cmb_datasummary_stddev(&ds_small);
            const double t_crit_small = t_crit_95(smpl_cnt_small);

            const double smpl_cnt_large = cmb_datasummary_count(&ds_large);
            const double smpl_avg_large = cmb_datasummary_mean(&ds_large);
            const double smpl_sd_large = cmb_datasummary_stddev(&ds_large);
            const double t_crit_large = t_crit_95(smpl_cnt_large);

            fprintf(datafp, "%f\t%f\t%u\t%u\t%u\t%f\t%f\t%f\t%f\n",
                    smpl_arr, smpl_refdep, smpl_ntugs,
                    smpl_nsmallbts, smpl_nlargebts,
                    smpl_avg_small, t_crit_small * smpl_sd_small,
                    smpl_avg_large, t_crit_large * smpl_sd_large);
            cmb_datasummary_terminate(&ds_small);
            cmb_datasummary_terminate(&ds_large);
        }

        fprintf(datafp, "\n\n");

        /* Number of small berths */
        for (unsigned ui_nsb = 0; ui_nsb < N_LEVELS; ui_nsb++) {
            double smpl_arr = experiment[ui_trl].arrival_rate;
            double smpl_refdep = experiment[ui_trl].reference_depth;
            unsigned smpl_ntugs = experiment[ui_trl].num_tugs;
            unsigned smpl_nsmallbts = experiment[ui_trl].num_berths[SMALL];
            unsigned smpl_nlargebts = experiment[ui_trl].num_berths[LARGE];

            struct cmb_datasummary ds_small;
            struct cmb_datasummary ds_large;
            cmb_datasummary_initialize(&ds_small);
            cmb_datasummary_initialize(&ds_large);
            for (unsigned ui_rep = 0u; ui_rep < N_REPS; ui_rep++) {
                if (experiment[ui_trl].avg_time_in_system[SMALL] != -1.0) {
                    cmb_datasummary_add(&ds_small, experiment[ui_trl].avg_time_in_system[SMALL]);
                    cmb_datasummary_add(&ds_large, experiment[ui_trl].avg_time_in_system[LARGE]);
                }
                ui_trl++;
            }

            const double smpl_cnt_small = cmb_datasummary_count(&ds_small);
            const double smpl_avg_small = cmb_datasummary_mean(&ds_small);
            const double smpl_sd_small = cmb_datasummary_stddev(&ds_small);
            const double t_crit_small = t_crit_95(smpl_cnt_small);

            const double smpl_cnt_large = cmb_datasummary_count(&ds_large);
            const double smpl_avg_large = cmb_datasummary_mean(&ds_large);
            const double smpl_sd_large = cmb_datasummary_stddev(&ds_large);
            const double t_crit_large = t_crit_95(smpl_cnt_large);

            fprintf(datafp, "%f\t%f\t%u\t%u\t%u\t%f\t%f\t%f\t%f\n",
                    smpl_arr, smpl_refdep, smpl_ntugs,
                    smpl_nsmallbts, smpl_nlargebts,
                    smpl_avg_small, t_crit_small * smpl_sd_small,
                    smpl_avg_large, t_crit_large * smpl_sd_large);
            cmb_datasummary_terminate(&ds_small);
            cmb_datasummary_terminate(&ds_large);
        }

        fprintf(datafp, "\n\n");

        /* Number of large berths */
        for (unsigned ui_nlb = 0; ui_nlb < N_LEVELS; ui_nlb++) {
            double smpl_arr = experiment[ui_trl].arrival_rate;
            double smpl_refdep = experiment[ui_trl].reference_depth;
            unsigned smpl_ntugs = experiment[ui_trl].num_tugs;
            unsigned smpl_nsmallbts = experiment[ui_trl].num_berths[SMALL];
            unsigned smpl_nlargebts = experiment[ui_trl].num_berths[LARGE];

            struct cmb_datasummary ds_small;
            struct cmb_datasummary ds_large;
            cmb_datasummary_initialize(&ds_small);
            cmb_datasummary_initialize(&ds_large);
            for (unsigned ui_rep = 0u; ui_rep < N_REPS; ui_rep++) {
                if (experiment[ui_trl].avg_time_in_system[SMALL] != -1.0) {
                    cmb_datasummary_add(&ds_small, experiment[ui_trl].avg_time_in_system[SMALL]);
                    cmb_datasummary_add(&ds_large, experiment[ui_trl].avg_time_in_system[LARGE]);
                }
                ui_trl++;
            }

            const double smpl_cnt_small = cmb_datasummary_count(&ds_small);
            const double smpl_avg_small = cmb_datasummary_mean(&ds_small);
            const double smpl_sd_small = cmb_datasummary_stddev(&ds_small);
            const double t_crit_small = t_crit_95(smpl_cnt_small);

            const double smpl_cnt_large = cmb_datasummary_count(&ds_large);
            const double smpl_avg_large = cmb_datasummary_mean(&ds_large);
            const double smpl_sd_large = cmb_datasummary_stddev(&ds_large);
            const double t_crit_large = t_crit_95(smpl_cnt_large);

            fprintf(datafp, "%f\t%f\t%u\t%u\t%u\t%f\t%f\t%f\t%f\n",
                    smpl_arr, smpl_refdep, smpl_ntugs,
                    smpl_nsmallbts, smpl_nlargebts,
                    smpl_avg_small, t_crit_small * smpl_sd_small,
                    smpl_avg_large, t_crit_large * smpl_sd_large);
            cmb_datasummary_terminate(&ds_small);
            cmb_datasummary_terminate(&ds_large);
        }

        fprintf(datafp, "\n\n");
    }


    fclose(datafp);
    free(experiment);

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (double)(end_time.tv_sec - start_time.tv_sec);
    elapsed += (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    printf("It took %g sec\n", elapsed);

    write_gnuplot_commands();
    (void)system("gnuplot -persistent tut_4_2.gp");

    return 0;
}

void write_gnuplot_commands(void)
{
    static char labelbuf[32];
    static const double row_label_y[N_SCENARIOS] = { 0.80, 0.50, 0.20 };
    static const double column_label_x[N_PARAMS] = { 0.22, 0.44, 0.67, 0.89 };
    static const char *xlabels[N_PARAMS] = {
        "Dredged depth",
        "Number of tugs",
        "Number of small berths",
        "Number of large berths"
    };
    static const char *xranges[N_PARAMS] = {
        "[14.5:17.5]",
        "[9:15]",
        "[5:11]",
        "[2:8]"
    };
    unsigned scenario;
    unsigned param;
    FILE *cmdfp = fopen("tut_4_2.gp", "w");

    fprintf(cmdfp, "set terminal qt size 1200,1000 enhanced font 'Arial,9'\n");
    fprintf(cmdfp, "set multiplot layout 3,4 rowsfirst \\\n");
    fprintf(cmdfp, "title \"Harbor improvement opportunities\" font 'Helvetica,16'\\\n");
    fprintf(cmdfp, "margins 0.16, 0.95, 0.1, 0.88 spacing 0.1, 0.15\n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set ylabel \"Avg time in system\" font 'Arial,9'\n");
    fprintf(cmdfp, "set yrange [0:30]\n");
    fprintf(cmdfp, "datafile = 'tut_4_2.dat'\n");
    for (param = 0; param < N_PARAMS; ++param) {
        fprintf(cmdfp,
                "set label %u \"%s\" at screen %.2f,0.91 center font 'Helvetica,12'\n",
                param + 10u, xlabels[param], column_label_x[param]);
    }

    for (scenario = 0; scenario < N_SCENARIOS; ++scenario) {
        (void)snprintf(labelbuf, sizeof(labelbuf), "Arrival rate %4.3f",
            arrival_rate[scenario]);
        fprintf(cmdfp,
                "set label 1 \"%s\" at screen 0.05,%.2f center rotate by 90 "
                "font 'Helvetica,12'\n",
                labelbuf, row_label_y[scenario]);

        for (param = 0; param < N_PARAMS; ++param) {
            unsigned index = scenario * N_PARAMS + param;

            fprintf(cmdfp, "set xlabel \"%s\"\n", xlabels[param]);
            fprintf(cmdfp, "set xrange %s\n", xranges[param]);
            if (scenario == 0u && param == 0u) {
                fprintf(cmdfp,
                        "set key at screen 0.95,0.965 top right opaque box spacing 1.0 "
                        "font 'Helvetica,10'\n");
                fprintf(cmdfp,
                        "plot datafile using %u:6:7 index %u with errorbars title 'Small ships' "
                        "lc rgb \"black\",\\\n",
                        param + 2u, index);
                fprintf(cmdfp,
                        "     datafile using %u:8:9 index %u with errorbars title 'Large ships' "
                        "lc rgb \"red\"\n",
                        param + 2u, index);
                fprintf(cmdfp, "unset key\n");
            } else {
                fprintf(cmdfp,
                        "plot datafile using %u:6:7 index %u with errorbars notitle "
                        "lc rgb \"black\",\\\n",
                        param + 2u, index);
                fprintf(cmdfp,
                        "     datafile using %u:8:9 index %u with errorbars notitle "
                        "lc rgb \"red\"\n",
                        param + 2u, index);
            }

            if (param == 0u) {
                fprintf(cmdfp, "unset label 1\n");
            }
        }
    }

    fprintf(cmdfp, "unset multiplot\n");

    fclose(cmdfp);
}

/*
 * Table lookup for the critical values for two-sided 95 % confidence intervals,
 * see https://www.stat.purdue.edu/~lfindsen/stat503/t-Dist.pdf
 */
double t_crit_95(const uint32_t n)
{
    cmb_assert_debug(n > 0u);

#define NUM_TVALS 62u
    static uint32_t n_vals[NUM_TVALS] = {
        1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
       11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
       21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
       31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
       41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
       60, 70, 80, 90,100,120,140,180,200,500,
       1000, UINT32_MAX
   };

    static double t_vals[NUM_TVALS] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
         2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
         2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042,
         2.040, 2.037, 2.035, 2.032, 2.030, 2.028, 2.026, 2.024, 2.023, 2.021,
         2.020, 2.018, 2.017, 2.015, 2.014, 2.013, 2.012, 2.011, 2.010, 2.009,
         2.000, 1.994, 1.990, 1.987, 1.984, 1.980, 1.977, 1.973, 1.972, 1.965,
         1.962, 1.960
    };

    for (uint32_t ui = 0u; ui < NUM_TVALS; ui++) {
        if (n_vals[ui] == n) {
            return t_vals[ui];
        }

        if (n_vals[ui] > n) {
            /* Interpolate between values */
            const uint32_t n_range = n_vals[ui] - n_vals[ui - 1];
            const double t_range = t_vals[ui] - t_vals[ui - 1];
            const double frac = (double)(n - n_vals[ui - 1]) / (double)n_range;
            cmb_assert_debug(frac >= 0.0 && frac <= 1.0);
            const double t_ret = t_vals[ui] - frac * t_range;

            cmb_assert_debug((t_ret >= t_vals[ui -1]) && (t_ret <= t_vals[ui]));
            return t_ret;
        }
    }

    /* Should never get this far */
    cmb_logger_error(stderr, "Critical value lookup failed");
#undef NUM_TVALS
}

