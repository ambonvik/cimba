/*
* tutorial/tut_4_1.c
 *
 * M/G/n model with balking, reneging, and jockeying customer behaviors.
 * Single-threaded development version.
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

/*
 * Bit masks to distinguish between types of user-defined logging messages.
 */
#define LOGFLAG_ARRIVAL     0x00000001
#define LOGFLAG_CUSTOMER    0x00000002
#define LOGFLAG_SERVICE     0x00000004
#define LOGFLAG_SIMULATION  0x00000008

/*
 * Our simulated world consists of these entities.
 */
struct simulation {
    /* TODO: Pointers to entities in your simulated world go here */
};

/* Variables describing the state of the environment around our entities */
struct environment {
    /* TODO: Place your environment state variables here */
};


/*
 * A single trial is defined by these parameters and generates these results.
 */
struct trial {
    /* TODO: Add your parameters here */
    double warmup_time;
    double duration;
    /* TODO: Place your results here */
    uint64_t seed_used;
};

/*
 * The context for our simulation consists of the simulation entities, the
 * trial parameters, and the requested trial results.
 */
struct context {
    struct simulation *sim;
    struct environment *env;
    struct trial *trl;
};

/*
 * Event to close down the simulation.
 */
void end_sim(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;
    cmb_logger_user(stdout, USERFLAG1, "--- Game Over ---");

    /* TODO: Stop all your simulated processes here */
}

/*
 * Event to turn on data recording
 */
static void start_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;

    /* TODO: Turn on data recording for relevant entities here */
}

/*
 * Event to turn off data recording
 */
static void stop_rec(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;

    /* TODO: Turn off data recording for relevant entities here */
}


/*
 * TODO: Define functions for your other events and processes here
 */

/*
 * The simulation driver function to execute one trial
 */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trl = vtrl;

    /* Using local variables, since it will only be used before this function exits */
    struct context ctx = {};
    struct simulation sim = {};
    ctx.sim = &sim;
    ctx.trl = trl;

    /* Set up our trial housekeeping */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    // cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);
    trl->seed_used = cmb_random_get_hwseed();
    cmb_random_initialize(trl->seed_used);

    /*
     * TODO: Create, initialize, and start your simulated entities here
     */

    /* Schedule the simulation control events */
    double t = trl->warmup_time;
    cmb_event_schedule(start_rec, NULL, &ctx, t, 0);
    t += trl->duration;
    cmb_event_schedule(stop_rec, NULL, &ctx, t, 0);
    /* Set a large negative priority for the stop event to ensure normal events go first */
    cmb_event_schedule(end_sim, NULL, &ctx, t, -100);

    /* Run this trial */
    cmb_event_queue_execute();

    /*
     * TODO: Collect your statistics here
     */

    /*
     * TODO: Terminate and destroy your simulated entities here,
     * one _terminate for each _initialize, one _destroy for each _create
     */

    /* Final housekeeping to leave everything as we found it */
    cmb_event_queue_terminate();
    cmb_random_terminate();

}

/*
 * Temporary function to load trial test data for the single-threaded development version.
 */
void load_params(struct trial *trlp)
{
    cmb_assert_release(trlp != NULL);

    /*
     * TODO: Fill in your trial test data here
     */
}

/*
 * The minimal single-threaded main function
 */
int main(void)
{
    struct trial trl = {};
    load_params(&trl);

    run_trial(&trl);

    return 0;
}
