/*
 * tutorial/tut_3_1.c
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

#include "cmb_priorityqueue.h"

/*
 * Bit masks to distinguish between types of user-defined logging messages.
 */
#define LOGFLAG_ARRIVAL     0x00000001
#define LOGFLAG_VISITOR     0x00000002
#define LOGFLAG_SERVICE     0x00000004
#define LOGFLAG_DEPARTURE   0x00000008
#define LOGFLAG_SIMULATION  0x00000010
#define LOGFLAG_ALL         0xFFFFFFFF

/*
 * Simulation parameters - can be global because const and because only used
 * outside the multithreading when loading the experiment array with trials.
 */
const double arrival_rate = 0.5;
const double percent_goldcards = 0.25;
const double duration = 16 * 60.0;

/* All multiplied by each visitors patience */
const unsigned balking_threshold = 10;
const double jockeying_threshold = 5.0;
const double reneging_threshold = 10.0;

/*******************************************************************************
 * Hard-coding the park structure for this tutorial only.
 * Should be an input file.
 */
#define NUM_ATTRACTIONS 9
#define IDX_ENTRANCE 0
#define IDX_EXIT (NUM_ATTRACTIONS + 1)

/* Adding extra entries at the beginning and end for entrance and exit */
/* Transition probabilities i => j */
const double transition_probs[NUM_ATTRACTIONS + 2][NUM_ATTRACTIONS + 2] = {
    { 0.00, 0.30, 0.20, 0.20, 0.10, 0.05, 0.05, 0.00, 0.00, 0.00, 0.10 },
    { 0.00, 0.00, 0.30, 0.20, 0.10, 0.10, 0.05, 0.05, 0.00, 0.00, 0.20 },
    { 0.00, 0.10, 0.05, 0.20, 0.10, 0.15, 0.05, 0.05, 0.05, 0.05, 0.20 },
    { 0.00, 0.05, 0.10, 0.05, 0.20, 0.10, 0.10, 0.05, 0.05, 0.05, 0.25 },
    { 0.00, 0.05, 0.00, 0.10, 0.05, 0.20, 0.15, 0.10, 0.05, 0.05, 0.25 },
    { 0.00, 0.00, 0.00, 0.05, 0.05, 0.00, 0.20, 0.20, 0.10, 0.10, 0.30 },
    { 0.00, 0.00, 0.00, 0.05, 0.10, 0.05, 0.00, 0.30, 0.10, 0.10, 0.30 },
    { 0.00, 0.00, 0.00, 0.05, 0.05, 0.05, 0.05, 0.05, 0.20, 0.20, 0.35 },
    { 0.00, 0.00, 0.00, 0.00, 0.00, 0.05, 0.05, 0.10, 0.00, 0.30, 0.50 },
    { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.05, 0.10, 0.20, 0.00, 0.65 },
    { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 1.00 }
};

/* Average walking times i => j */
const double transition_times[NUM_ATTRACTIONS + 2][NUM_ATTRACTIONS + 2] = {
    { 0.00, 3.00, 7.00, 8.00, 9.00, 12.0, 13.0, 15.0, 20.0, 25.0, 30.0 },
    { 3.00, 1.00, 3.00, 7.00, 8.00, 9.00, 12.0, 13.0, 15.0, 20.0, 25.0 },
    { 7.00, 3.00, 1.00, 3.00, 7.00, 8.00, 9.00, 12.0, 13.0, 15.0, 20.0 },
    { 8.00, 7.00, 3.00, 1.00, 3.00, 7.00, 8.00, 9.00, 12.0, 13.0, 15.0 },
    { 9.00, 8.00, 7.00, 3.00, 1.00, 3.00, 7.00, 8.00, 9.00, 12.0, 13.0 },
    { 12.0, 9.00, 8.00, 7.00, 3.00, 1.00, 3.00, 7.00, 8.00, 9.00, 12.0 },
    { 13.0, 12.0, 9.00, 8.00, 7.00, 3.00, 1.00, 3.00, 7.00, 8.00, 9.00 },
    { 15.0, 13.0, 12.0, 9.00, 8.00, 7.00, 3.00, 1.00, 3.00, 7.00, 8.00 },
    { 20.0, 15.0, 13.0, 12.0, 9.00, 8.00, 7.00, 3.00, 1.00, 3.00, 7.00 },
    { 25.0, 20.0, 15.0, 13.0, 12.0, 9.00, 8.00, 7.00, 3.00, 1.00, 3.00 },
    { 30.0, 25.0, 20.0, 15.0, 13.0, 12.0, 9.00, 8.00, 7.00, 3.00, 0.00 }
};

/* Aligned indexing with transition_probs, but index 0 and n+1 not used */
const unsigned num_queues[NUM_ATTRACTIONS + 2] =
    { 0, 1, 1, 1, 3, 1, 1, 1, 1, 1, 0 };

const unsigned num_servers_per_q[NUM_ATTRACTIONS + 2] =
    { 0, 1, 3, 2, 1, 1, 1, 1, 1, 1, 0 };

const unsigned batch_sizes[NUM_ATTRACTIONS + 2] =
    { 0, 1, 5, 5, 1, 10, 5, 8, 1, 1, 0 };

const double min_durations[NUM_ATTRACTIONS + 2] =
    { 0.0, 3.0, 5.0, 4.0, 15.0,  8.0, 5.0, 5.0, 6.0, 3.0, 0.0 };

const double mode_durations[NUM_ATTRACTIONS + 2 ] =
    { 0.0, 4.0, 6.0, 5.0, 20.0,  9.0, 6.0, 5.5, 7.0, 4.0, 0.0 };

const double max_durations[NUM_ATTRACTIONS + 2] =
    { 0.0, 5.0, 7.0, 6.0, 24.0, 12.0, 8.0, 6.0, 8.0, 5.0, 0.0 };

/* End of hard-coded parameters
 ******************************************************************************/

#define TIMER_JOCKEYING 17
#define TIMER_RENEGING  42

struct visitor {
    struct cmb_process core;       /* <= Note: The real thing, not a pointer */
    double patience;
    bool goldcard;
    double entry_time;
    unsigned current_attraction;
    double riding_time;
    double waiting_time;
    double walking_time;
    unsigned num_attractions_visited;
    double last_event_time;
};

struct server {
    struct cmb_process core;       /* <= Note: The real thing, not a pointer */
    struct cmb_priorityqueue *pq;
    unsigned batch_size;
    double dur_min, dur_mode, dur_max;
};

struct attraction {
    unsigned num_queues;
    struct cmb_priorityqueue **queues;
    unsigned num_servers;
    struct server **servers;
    struct cmb_random_alias *quo_vadis;
};

/* Our simulated world consists of these entities.  */
struct simulation {
    struct attraction park_attractions[NUM_ATTRACTIONS + 1];
    struct cmb_process *arrivals;
    struct cmb_process *departures;
    struct cmb_objectqueue *departeds;
    struct cmb_datasummary time_in_park;
    struct cmb_datasummary riding_times;
    struct cmb_datasummary waiting_times;
    struct cmb_datasummary walking_times;
    struct cmb_datasummary num_rides;
};

/* A single trial is defined by these parameters and generates these results. */
struct trial {
    double duration;
    uint64_t seed_used;
    double avg_time_in_park;
    double avg_time_riding;
    double avg_time_waiting;
    double avg_time_walking;
    double avg_num_rides;
};

struct context {
    struct simulation *sim;
    struct trial *trl;
};

/*
 * Server methods
 */
void *serverfunc(struct cmb_process *me, void *vctx)
{
    cmb_assert_debug(me != NULL);
    cmb_unused(vctx);
    const struct server *sp = (struct server *)me;

    while (true) {
        /* Prepare for the next batch of visitors */
        unsigned cnt = 0u;
        struct visitor *batch[sp->batch_size];

        /* Wait for the first visitor, then fill the ride as best possible */
        cmb_logger_user(stdout, LOGFLAG_SERVICE, "Open for next batch");
        do {
            struct visitor *vip;
            (void)cmb_priorityqueue_get(sp->pq, (void**)&(vip));
            struct cmb_process *pp = (struct cmb_process *)vip;
            cmb_process_timers_clear(pp);
            cmb_logger_user(stdout, LOGFLAG_SERVICE, "Got visitor %s", cmb_process_name(pp));
            batch[cnt++] = vip;
        } while ((cmb_priorityqueue_length(sp->pq) > 0) && (cnt < sp->batch_size)) ;

        /* Log the waiting times for this batch of visitors */
        cmb_logger_user(stdout, LOGFLAG_SERVICE, "Has %u for %u slots", cnt, sp->batch_size);
        for (unsigned ui = 0; ui < cnt; ui++) {
            struct visitor *vip = batch[ui];
            const double qt = cmb_time() - vip->last_event_time;
            vip->waiting_time += qt;
            vip->last_event_time = cmb_time();
        }

        /* Run the ride with these visitors on board */
        cmb_logger_user(stdout, LOGFLAG_SERVICE, "Starting ride");
        const double dur = cmb_random_PERT(sp->dur_min, sp->dur_mode, sp->dur_max);
        cmb_process_hold(dur);
        cmb_logger_user(stdout, LOGFLAG_SERVICE, "Ride finished");

        /* Unload and send the visitors on their merry way */
        for (unsigned ui = 0u; ui < cnt; ui++) {
            struct visitor *vip = batch[ui];
            const double rt = cmb_time() - vip->last_event_time;
            vip->riding_time += rt;
            vip->last_event_time = cmb_time();
            struct cmb_process *pp = (struct cmb_process *)vip;
            cmb_logger_user(stdout, LOGFLAG_SERVICE, "Resuming visitor %s", cmb_process_name(pp));
            cmb_process_resume(pp, CMB_PROCESS_SUCCESS);
        }
    }
}

struct server *server_create(void)
{
    struct server *sp = malloc(sizeof(struct server));
    cmb_assert_release(sp != NULL);
    return sp;
}

void server_initialize(struct server *sp,
                       const char *name,
                       struct cmb_priorityqueue *qp,
                       const unsigned bs,
                       const double dmin, const double dmod, const double dmax)
{
    sp->pq = qp;
    sp->batch_size = bs;
    sp->dur_min = dmin;
    sp->dur_mode = dmod;
    sp->dur_max = dmax;

    cmb_process_initialize(&sp->core, name, serverfunc, NULL, 0u);
}

void server_start(struct server *sp)
{
    cmb_process_start(&sp->core);
}

void server_terminate(struct server *sp)
{
    cmb_process_terminate(&sp->core);
}

void server_destroy(struct server *sp)
{
    free(sp);
}

/*
 * Attraction methods
 */
struct attraction *attraction_create(void)
{
    struct attraction *ap = malloc(sizeof(struct attraction));
    cmb_assert_release(ap != NULL);
    return ap;
}

void attraction_initialize(struct attraction *ap, const unsigned ui)
{
    ap->num_queues = num_queues[ui];
    ap->queues = malloc(ap->num_queues * sizeof(struct cmb_priorityqueue *));
    cmb_assert_release(ap->queues != NULL);

    ap->num_servers = num_queues[ui] * num_servers_per_q[ui];
    ap->servers = malloc(ap->num_servers * sizeof(struct server *));
    cmb_assert_release(ap->servers != NULL);


    for (unsigned qi = 0; qi < ap->num_queues; qi++) {
        char namebuf[32];
        struct cmb_priorityqueue *pq = cmb_priorityqueue_create();
        snprintf(namebuf, sizeof(namebuf), "Queue_%02u_%02u", ui, qi);
        cmb_priorityqueue_initialize(pq, namebuf, CMB_UNLIMITED);
        cmb_priorityqueue_recording_start(pq);
        ap->queues[qi] = pq;

        for (unsigned si = 0; si < num_servers_per_q[ui]; si++) {
            snprintf(namebuf, sizeof(namebuf), "Server_%02u_%02u_%02u", ui, qi, si);
            struct server *sp = server_create();
            server_initialize(sp, namebuf, pq,
                              batch_sizes[ui],
                              min_durations[ui], mode_durations[ui], max_durations[ui]);
            ap->servers[qi * num_servers_per_q[ui] + si] = sp;
            server_start(sp);
        }
    }

    ap->quo_vadis = cmb_random_alias_create(NUM_ATTRACTIONS + 2, transition_probs[ui]);
}

void attraction_terminate(struct attraction *ap)
{
    for (unsigned qi = 0; qi < ap->num_queues; qi++) {
        cmb_priorityqueue_terminate(ap->queues[qi]);
    }

    for (unsigned si = 0; si < ap->num_servers; si++) {
        server_terminate(ap->servers[si]);
    }

    free(ap->queues);
    free(ap->servers);
    cmb_random_alias_destroy(ap->quo_vadis);
}

void attraction_destroy(struct attraction *ap)
{
    free(ap);
}

/*
 * Visitor methods
 */
void *visitor_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_debug(me != NULL);
    cmb_assert_debug(vctx != NULL);

    struct visitor *vip = (struct visitor *)me;
    const struct context *ctx = vctx;
    const struct simulation *sim = ctx->sim;

    cmb_logger_user(stdout, LOGFLAG_VISITOR, "Started");
    vip->current_attraction = IDX_ENTRANCE;
    while (vip->current_attraction != IDX_EXIT) {
        /* Select where to go next */
        const unsigned ua = vip->current_attraction;
        const struct attraction *ap_from = &sim->park_attractions[ua];
        const struct cmb_random_alias *qv = ap_from->quo_vadis;
        const unsigned nxt = cmb_random_alias_sample(qv);
        cmb_logger_user(stdout, LOGFLAG_VISITOR, "At %u, next %u", ua, nxt);

        /* Walk there */
        const double mwt = transition_times[ua][nxt];
        const double wt = cmb_random_PERT(0.5 * mwt, mwt, 2.0 * mwt);
        cmb_process_hold(wt);
        vip->walking_time += wt;
        vip->current_attraction = nxt;

        /* Join the shortest queue if several */
        if (nxt != IDX_EXIT) {
            const struct attraction *ap_to = &sim->park_attractions[nxt];
            uint64_t shrtlen = UINT64_MAX;
            unsigned shrtqi = 0u;
            for (unsigned qi = 0u; qi < ap_to->num_queues; qi++) {
                struct cmb_priorityqueue *pq = ap_to->queues[qi];
                const unsigned len = cmb_priorityqueue_length(pq);
                if (len < shrtlen) {
                    shrtlen = len;
                    shrtqi = qi;
                }
            }

            cmb_logger_user(stdout, LOGFLAG_VISITOR,
                            "At attraction %u, queue %u looks shortest (%" PRIu64 ")",
                            ua, shrtqi, shrtlen);

            /* Balking? */
            if (shrtlen > (uint64_t)(vip->patience * balking_threshold)) {
                /* Too long queue, go to next instead */
                cmb_logger_user(stdout, LOGFLAG_VISITOR,
                                "Balked at attraction %u, queue %u", ua, shrtqi);
                continue;
            }

            /* Set two timeouts */
            const double jt = vip->patience * jockeying_threshold;
            cmb_process_timer_set(me, jt, TIMER_JOCKEYING);
            const double rt = vip->patience * reneging_threshold;
            cmb_process_timer_add(me, rt, TIMER_RENEGING);

            struct cmb_priorityqueue *q = sim->park_attractions[nxt].queues[shrtqi];
            uint64_t pq_hndl;
            cmb_logger_user(stdout, LOGFLAG_VISITOR,
                            "Joining queue %s", cmb_priorityqueue_name(q));
            vip->last_event_time = cmb_time();
            /* Not blocking, since the queue has unlimited size */
            cmb_priorityqueue_put(q, (void *)vip, cmb_process_priority(me), &pq_hndl);

            /* Suspend ourselves until we have finished both queue and ride,
             * trusting the server to update our waiting and riding times */
            while (true) {
                const int64_t sig = cmb_process_yield();
                if (sig == TIMER_JOCKEYING) {
                    cmb_logger_user(stdout, LOGFLAG_VISITOR, "Jockeying at attraction %u", ua);
                    const unsigned in_q = shrtqi;
                    const unsigned mypos = cmb_priorityqueue_position(q, pq_hndl);
                    shrtlen = UINT64_MAX;
                    shrtqi = 0u;
                    for (unsigned qi = 0u; qi < ap_to->num_queues; qi++) {
                        struct cmb_priorityqueue *pq = ap_to->queues[qi];
                        const unsigned len = cmb_priorityqueue_length(pq);
                        if (len < shrtlen) {
                            shrtlen = len;
                            shrtqi = qi;
                        }
                    }

                    if (shrtlen < mypos) {
                        cmb_logger_user(stdout, LOGFLAG_VISITOR,
                                        "Moving from queue %u to queue %u (len %" PRIu64 ")",
                                        in_q, shrtqi, shrtlen);
                        const bool found = cmb_priorityqueue_cancel(q, pq_hndl);
                        cmb_assert_debug(found == true);
                        q = ap_to->queues[shrtqi];
                        const int64_t pri = cmb_process_priority(me);
                        cmb_priorityqueue_put(q, (void *)vip, pri + 1, &pq_hndl);
                        continue;
                    }
                }
                else if (sig == TIMER_RENEGING) {
                    cmb_logger_user(stdout, LOGFLAG_VISITOR, "Reneging at attraction %u", ua);
                    const bool found = cmb_priorityqueue_cancel(q, pq_hndl);
                    cmb_assert_debug(found == true);
                    cmb_process_timers_clear(me);
                    break;
                }
                else {
                    cmb_assert_release(sig == CMB_PROCESS_SUCCESS);
                    vip->num_attractions_visited++;
                    cmb_logger_user(stdout, LOGFLAG_VISITOR,
                                    "Yay! Leaving attraction %u", vip->current_attraction);
                    break;
                }
            }

            /* Out of the attraction, slightly dizzy. Do it again? */
        }
    }

    /* No, enough for today */
    cmb_logger_user(stdout, LOGFLAG_VISITOR, "Leaving");
    cmb_objectqueue_put(sim->departeds, (void *)vip);
    cmb_process_exit(NULL);

    /* Not reached */
    return NULL;
}

struct visitor *visitor_create(void)
{
    struct visitor *vip = malloc(sizeof(struct visitor));
    cmb_assert_release(vip != NULL);

    return vip;
}

void visitor_initialize(struct visitor *vip,
                        const uint64_t cnt,
                        const double patience,
                        const bool goldcard,
                        const int64_t priority,
                        void *vctx)
{
    cmb_assert_release(vip != NULL);

    vip->patience = patience;
    vip->goldcard = goldcard;
    vip->current_attraction = 0u;
    vip->num_attractions_visited = 0u;
    vip->riding_time = 0.0;
    vip->waiting_time = 0.0;
    vip->walking_time = 0.0;

    char namebuf[32];
    snprintf(namebuf, sizeof(namebuf), "Visitor_%06" PRIu64, cnt);
    struct cmb_process *pp = (struct cmb_process *)vip;
    cmb_process_initialize(pp, namebuf, visitor_proc, vctx, priority);
}

void visitor_start(struct visitor *vip)
{
    cmb_assert_release(vip != NULL);

    vip->entry_time = cmb_time();
    cmb_process_start((struct cmb_process *)vip);
}

const char *visitor_name(struct visitor *vip)
{
    cmb_assert_release(vip != NULL);

    return ((struct cmb_process *)vip)->name;
}

void visitor_terminate(struct visitor *vip)
{
    /* Nothing needed for the visitor itself, pass it on to parent class */
    cmb_process_terminate((struct cmb_process *)vip);
}

void visitor_destroy(struct visitor *vip)
{
    free(vip);
}

/* Arrival process - generate new visitors */
void *arrival_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_unused(vctx);

    uint64_t cnt = 0u;
    const double mean = 1.0 / arrival_rate;
    while (true) {
        cmb_process_hold(cmb_random_exponential(mean));

        struct visitor *vip = visitor_create();
        const double patience = cmb_random_triangular(0.5, 1.0, 1.5);
        const bool goldcard = cmb_random_bernoulli(percent_goldcards);
        const int64_t priority = (goldcard)? 5 : 0;
        visitor_initialize(vip, ++cnt, patience, goldcard, priority, vctx);

        /* Start our new visitor passing the turnstile */
        visitor_start(vip);
        cmb_logger_user(stdout, LOGFLAG_ARRIVAL, "%s arriving", visitor_name(vip));
    }
}

/* Departure process, wait for leaving visitors, then recycle it */
void *departure_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    const struct context *ctx = vctx;
    struct simulation *sim = ctx->sim;

    while (true) {
        struct visitor *vip = NULL;
        (void)cmb_objectqueue_get(sim->departeds, (void **)(&vip));
        cmb_assert_debug(vip != NULL);
        cmb_assert_debug(cmb_process_status((struct cmb_process *)vip) == CMB_PROCESS_FINISHED);
        cmb_logger_user(stdout, LOGFLAG_DEPARTURE, "%s departed",
                        ((struct cmb_process *)vip)->name);

        /* Collect its statistics */
        const double tsys = cmb_time() - vip->entry_time;
        cmb_datasummary_add(&sim->time_in_park, tsys);
        cmb_datasummary_add(&sim->riding_times, vip->riding_time);
        cmb_datasummary_add(&sim->waiting_times, vip->waiting_time);
        cmb_datasummary_add(&sim->num_rides, vip->num_attractions_visited);
        cmb_datasummary_add(&sim->walking_times, vip->walking_time);

        visitor_terminate(vip);
        visitor_destroy(vip);
    }
}

/* Event to close down the simulation. */
void end_sim(void *subject, void *object)
{
    cmb_unused(subject);

    const struct context *ctx = object;
    const struct simulation *sim = ctx->sim;
    cmb_logger_user(stdout, LOGFLAG_SIMULATION, "Closing entrance for today");

    cmb_process_stop(sim->arrivals, NULL);
}

/* The simulation driver function to execute one trial */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trl = vtrl;

    cmb_logger_flags_off(CMB_LOGGER_INFO);

    /* Using local variables, since it will only be used before this function exits */
    struct context ctx = {};
    struct simulation sim = {};
    ctx.sim = &sim;
    ctx.trl = trl;

    /* Set up our trial housekeeping */
    cmb_event_queue_initialize(0.0);

    trl->seed_used = cmb_random_hwseed();
    cmb_random_initialize(trl->seed_used);

    cmb_datasummary_initialize(&(sim.time_in_park));
    cmb_datasummary_initialize(&(sim.riding_times));
    cmb_datasummary_initialize(&(sim.waiting_times));
    cmb_datasummary_initialize(&(sim.num_rides));
    cmb_datasummary_initialize(&(sim.walking_times));


    /* Create the attractions */
    for (unsigned ui = 0; ui < NUM_ATTRACTIONS + 1; ui++) {
        attraction_initialize(&sim.park_attractions[ui], ui);
    }

    /* Create the arrival and departure processes */
    sim.arrivals = cmb_process_create();
    cmb_process_initialize(sim.arrivals, "Arrivals", arrival_proc, &ctx, 0);
    cmb_process_start(sim.arrivals);
    sim.departeds = cmb_objectqueue_create();
    cmb_objectqueue_initialize(sim.departeds, "Departed visitors", CMB_UNLIMITED);
    sim.departures = cmb_process_create();
    cmb_process_initialize(sim.departures, "Departures", departure_proc, &ctx, 0);
    cmb_process_start(sim.departures);

    cmb_event_schedule(end_sim, NULL, &ctx, duration, 0);

    /* Run this trial */
    cmb_event_queue_execute();

    /* Capture and print the statistics for this trial */
    trl->avg_num_rides = cmb_datasummary_mean(&(sim.num_rides));
    printf("Number of rides taken:\n");
    cmb_datasummary_print(&(sim.num_rides), stdout, true);
    trl->avg_time_in_park = cmb_datasummary_mean(&(sim.time_in_park));
    printf("Time spent in park:\n");
    cmb_datasummary_print(&(sim.time_in_park), stdout, true);
    trl->avg_time_riding = cmb_datasummary_mean(&(sim.riding_times));
    printf("Riding times:\n");
    cmb_datasummary_print(&(sim.riding_times), stdout, true);
    trl->avg_time_waiting = cmb_datasummary_mean(&(sim.waiting_times));
    printf("Waiting times:\n");
    cmb_datasummary_print(&(sim.waiting_times), stdout, true);
    trl->avg_time_walking = cmb_datasummary_mean(&(sim.walking_times));
    printf("Walking times:\n");
    cmb_datasummary_print(&(sim.walking_times), stdout, true);

    printf("\nDetailed queue reports:\n");
    for (unsigned ui = 1; ui <= NUM_ATTRACTIONS; ui++) {
        const unsigned nq = sim.park_attractions[ui].num_queues;
        for (unsigned qi = 0u; qi < nq; qi++) {
            struct cmb_priorityqueue *pq = sim.park_attractions[ui].queues[qi];
            cmb_timeseries_finalize(&(pq->history), cmb_time());
            cmb_priorityqueue_report_print(pq, stdout);
        }
    }

    cmb_process_stop(sim.departures, NULL);
    cmb_process_terminate(sim.departures);
    cmb_process_destroy(sim.departures);
    cmb_objectqueue_terminate(sim.departeds);
    cmb_objectqueue_destroy(sim.departeds);
    cmb_process_terminate(sim.arrivals);
    cmb_process_destroy(sim.arrivals);

    for (unsigned ui = 0; ui < NUM_ATTRACTIONS + 1; ui++) {
        attraction_terminate(&sim.park_attractions[ui]);
    }

    cmb_event_queue_terminate();
    cmb_random_terminate();
}

/*
 * Placeholder function to load trial test data for the single-threaded development version.
 */
void load_params(struct trial *trlp)
{
    cmb_assert_release(trlp != NULL);

    trlp->duration = duration;
}

/*
 * The minimal single-threaded main function
 */
int main(void)
{
    struct trial trl = {};
    load_params(&trl);

    run_trial(&trl);

    printf("\nTrial outcomes:\n");
    printf("---------------\n");
    printf("Average number of rides: %.2f\n", trl.avg_num_rides);
    printf("Average time in park: %.2f\n", trl.avg_time_in_park);
    printf("Average time in rides: %.2f\n", trl.avg_time_riding);
    printf("Average time in queues: %.2f\n", trl.avg_time_waiting);
    printf("Average time walking: %.2f\n", trl.avg_time_walking);

    return 0;
}
