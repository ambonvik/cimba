/*
 * Test script for resource guards.
 *
 * The resource guard is the shared waiting-line mechanism underneath resources,
 * resource pools, buffers, object queues and conditions. These tests exercise
 * it directly through a minimal counting resource ("token_pool") built on the
 * guard's intended extension point: a cmi_resourcebase, an embedded guard, and
 * a demand predicate driven by the wait/signal protocol.
 *
 * The deterministic scenarios check the guard's contract:
 *   - a waiter blocks until its demand is met, then is resumed (basic),
 *   - waiters of equal priority are served in arrival order (FIFO),
 *   - higher priority is served first regardless of arrival order (priority),
 *   - a queued waiter can be reprioritized in place (reprioritize),
 *   - a queued waiter that times out frees its slot without stranding the
 *     waiter behind it (timeout-cancel),
 *   - a queued waiter that is stopped is removed without stranding the waiter
 *     behind it (stop),
 *   - signals are forwarded to registered observer guards (observers).
 * A final randomized soak then stresses the queue under mixed priorities with
 * interleaved reprioritizations and a terminal stop of every process.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_resourceguard.h"

#include "cmi_resourcebase.h"

#include "test.h"

#define USERFLAG 0x00000001

/* ───────────────────────────────────────────────────────────────────────────
 * A minimal counting resource built directly on cmb_resourceguard.
 *
 * The cmi_resourcebase must be the first member so the guard can recover the
 * concrete type from the base pointer it passes to the demand function.
 * ───────────────────────────────────────────────────────────────────────── */

struct token_pool {
    struct cmi_resourcebase base;       /* must be first: guard casts to this */
    struct cmb_resourceguard guard;     /* the waiting line under test */
    uint64_t capacity;                  /* total tokens that exist */
    uint64_t available;                 /* tokens currently free */
};

/* Demand predicate: are there at least ctx tokens free right now? */
static bool tokens_available(const struct cmi_resourcebase *rbp,
                             const struct cmb_process *pp,
                             const void *ctx)
{
    cmb_assert_always(rbp != NULL);
    cmb_unused(pp);

    const struct token_pool *tp = (const struct token_pool *)rbp;
    const uint64_t need = (uint64_t)ctx;
    return tp->available >= need;
}

static void token_pool_initialize(struct token_pool *tp,
                                   const char *name,
                                   const uint64_t capacity)
{
    cmb_assert_always(tp != NULL);
    cmb_assert_always(capacity > 0u);

    /* The library's create() functions hand initialize() a zeroed object; the
     * hashheap and resourcebase initializers rely on that (e.g. heap == NULL is
     * their double-init trap). These pools are stack allocated, so zero here. */
    memset(tp, 0, sizeof(*tp));

    cmi_resourcebase_initialize(&(tp->base), name);
    cmb_resourceguard_initialize(&(tp->guard), &(tp->base));
    tp->capacity = capacity;
    tp->available = capacity;
}

static void token_pool_terminate(struct token_pool *tp)
{
    cmb_assert_always(tp != NULL);

    cmb_resourceguard_terminate(&(tp->guard));
    cmi_resourcebase_terminate(&(tp->base));
}

/*
 * token_acquire - Get some tokens, blocking until they are available.
 *
 * This is the canonical guard client: a recheck loop around the wait. On a
 * SUCCESS resume the demand may already have been consumed by another resumed
 * process, so availability is re-tested rather than trusted. A non-SUCCESS
 * resume (canceled, interrupted, timed out) is returned to the caller.
 */
static int64_t token_acquire(struct token_pool *tp, const uint64_t need)
{
    cmb_assert_always(tp != NULL);
    cmb_assert_always(need <= tp->capacity);

    for (;;) {
        if (tp->available >= need) {
            tp->available -= need;
            return CMB_PROCESS_SUCCESS;
        }

        const int64_t sig = cmb_resourceguard_wait(&(tp->guard),
                                                   tokens_available,
                                                   (const void *)need);
        if (sig != CMB_PROCESS_SUCCESS) {
            return sig;
        }
    }
}

static void token_release(struct token_pool *tp, const uint64_t n)
{
    cmb_assert_always(tp != NULL);

    tp->available += n;
    cmb_assert_always(tp->available <= tp->capacity);
    cmb_resourceguard_signal(&(tp->guard));
}

/* ───────────────────────────────────────────────────────────────────────────
 * Shared process bodies and timed control events.
 * ───────────────────────────────────────────────────────────────────────── */

/* Per-waiter scenario state, passed as the process context. */
struct waiter_ctx {
    struct token_pool *pool;
    uint64_t need;          /* tokens demanded */
    double arrive_after;    /* delay before the first acquire attempt */
    double hold_after;      /* time to hold the tokens once acquired */
    double timeout_after;   /* self-timeout while waiting, 0 for none */
    int *order_counter;     /* shared running counter, may be NULL */
    int acquired_order;     /* counter value at acquire, 0 if never */
    double acquired_time;   /* sim time at acquire, -1.0 if never */
    int64_t result;         /* signal returned by the acquire attempt */
};

static void waiter_ctx_reset(struct waiter_ctx *c)
{
    c->acquired_order = 0;
    c->acquired_time = -1.0;
    c->result = INT64_MIN;
}

static void *waiter(struct cmb_process *me, void *vctx)
{
    cmb_assert_always(vctx != NULL);
    struct waiter_ctx *c = vctx;

    if (c->arrive_after > 0.0) {
        cmb_assert_always(cmb_process_hold(c->arrive_after) == CMB_PROCESS_SUCCESS);
    }

    if (c->timeout_after > 0.0) {
        (void)cmb_process_timer_add(me, c->timeout_after, CMB_PROCESS_TIMEOUT);
    }

    c->result = token_acquire(c->pool, c->need);
    if (c->result == CMB_PROCESS_SUCCESS) {
        if (c->order_counter != NULL) {
            c->acquired_order = ++(*c->order_counter);
        }
        c->acquired_time = cmb_time();
        cmb_logger_user(stdout, USERFLAG,
                        "%s acquired %" PRIu64 " (order %d) at t=%.1f",
                        me->name, c->need, c->acquired_order, c->acquired_time);
        if (c->hold_after > 0.0) {
            (void)cmb_process_hold(c->hold_after);
        }
        token_release(c->pool, c->need);
    }
    else {
        cmb_logger_user(stdout, USERFLAG, "%s leaves the queue, signal %" PRIi64,
                        me->name, c->result);
    }

    return NULL;
}

/* Reprioritize the subject process; object carries the new priority. */
static void reprioritize_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);

    struct cmb_process *p = subject;
    const int64_t pri = (int64_t)object;
    cmb_logger_user(stdout, USERFLAG, "Reprioritizing %s to %" PRIi64,
                    p->name, pri);
    cmb_process_priority_set(p, pri);
}

/* Stop the subject process wherever it happens to be. */
static void stop_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_unused(object);

    struct cmb_process *p = subject;
    cmb_logger_user(stdout, USERFLAG, "Stopping %s", p->name);
    const int64_t r = cmb_process_stop(p, NULL);
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    cmb_assert_always(cmb_process_status(p) == CMB_PROCESS_FINISHED);
}

/* Convenience: create, name, initialize and start a process in one call. */
static struct cmb_process *spawn(const char *name,
                                 cmb_process_func *fn,
                                 void *ctx,
                                 const int64_t pri)
{
    struct cmb_process *p = cmb_process_create();
    cmb_assert_always(p != NULL);
    cmb_process_initialize(p, name, fn, ctx, pri);
    cmb_assert_always(cmb_process_status(p) == CMB_PROCESS_CREATED);
    cmb_process_start(p);
    return p;
}

static void reap(struct cmb_process *p)
{
    cmb_assert_always(p != NULL);
    cmb_assert_always(cmb_process_status(p) == CMB_PROCESS_FINISHED);
    cmb_process_terminate(p);
    cmb_process_destroy(p);
}

/* ───────────────────────────────────────────────────────────────────────────
 * Deterministic scenarios. Each runs in its own freshly initialized event
 * queue so it is fully isolated, and asserts the guard's behaviour directly.
 * None of them draw from the RNG, so their output is seed independent.
 * ───────────────────────────────────────────────────────────────────────── */

/* A holder that grabs the whole pool, holds for `dur`, then releases. */
static void *sole_holder(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    struct waiter_ctx *c = vctx;
    cmb_assert_always(token_acquire(c->pool, c->need) == CMB_PROCESS_SUCCESS);
    (void)cmb_process_hold(c->hold_after);
    token_release(c->pool, c->need);
    return NULL;
}

static void scenario_basic(void)
{
    printf("Scenario 1: a waiter blocks until the resource is released\n");
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "Pool", 1u);

    struct waiter_ctx hctx = { &pool, 1u, 0.0, 10.0, 0.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx wctx = { &pool, 1u, 1.0, 0.0, 0.0, NULL, 0, -1.0, 0 };
    waiter_ctx_reset(&hctx);
    waiter_ctx_reset(&wctx);

    struct cmb_process *h = spawn("Holder", sole_holder, &hctx, 0);
    struct cmb_process *w = spawn("Waiter", waiter, &wctx, 0);

    cmb_event_queue_execute();

    cmb_assert_always(wctx.result == CMB_PROCESS_SUCCESS);
    cmb_assert_always(wctx.acquired_time == 10.0);  /* resumed exactly on release */
    cmb_assert_always(pool.available == 1u);        /* all tokens returned */

    reap(h);
    reap(w);
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

static void scenario_fifo(void)
{
    printf("Scenario 2: equal-priority waiters are served in arrival order\n");
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "Pool", 1u);
    int counter = 0;

    struct waiter_ctx hctx = { &pool, 1u, 0.0, 10.0, 0.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx a = { &pool, 1u, 1.0, 1.0, 0.0, &counter, 0, -1.0, 0 };
    struct waiter_ctx b = { &pool, 1u, 2.0, 1.0, 0.0, &counter, 0, -1.0, 0 };
    struct waiter_ctx d = { &pool, 1u, 3.0, 1.0, 0.0, &counter, 0, -1.0, 0 };

    struct cmb_process *h  = spawn("Holder", sole_holder, &hctx, 0);
    struct cmb_process *pa = spawn("First",  waiter, &a, 0);
    struct cmb_process *pb = spawn("Second", waiter, &b, 0);
    struct cmb_process *pd = spawn("Third",  waiter, &d, 0);

    cmb_event_queue_execute();

    /* Arrived 1, 2, 3 → served 1, 2, 3 */
    cmb_assert_always(a.acquired_order == 1);
    cmb_assert_always(b.acquired_order == 2);
    cmb_assert_always(d.acquired_order == 3);

    reap(h); reap(pa); reap(pb); reap(pd);
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

static void scenario_priority(void)
{
    printf("Scenario 3: higher priority is served before earlier lower priority\n");
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "Pool", 1u);
    int counter = 0;

    struct waiter_ctx hctx = { &pool, 1u, 0.0, 10.0, 0.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx lo = { &pool, 1u, 1.0, 1.0, 0.0, &counter, 0, -1.0, 0 };
    struct waiter_ctx hi = { &pool, 1u, 2.0, 1.0, 0.0, &counter, 0, -1.0, 0 };

    struct cmb_process *h  = spawn("Holder", sole_holder, &hctx, 0);
    struct cmb_process *pl = spawn("LowPri",  waiter, &lo, 0);   /* arrives first */
    struct cmb_process *ph = spawn("HighPri", waiter, &hi, 10);  /* arrives later */

    cmb_event_queue_execute();

    cmb_assert_always(hi.acquired_order == 1);  /* high priority first */
    cmb_assert_always(lo.acquired_order == 2);

    reap(h); reap(pl); reap(ph);
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

static void scenario_reprioritize(void)
{
    printf("Scenario 4: a queued waiter is reprioritized in place\n");
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "Pool", 1u);
    int counter = 0;

    struct waiter_ctx hctx = { &pool, 1u, 0.0, 20.0, 0.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx a = { &pool, 1u, 1.0, 1.0, 0.0, &counter, 0, -1.0, 0 };
    struct waiter_ctx b = { &pool, 1u, 2.0, 1.0, 0.0, &counter, 0, -1.0, 0 };

    struct cmb_process *h  = spawn("Holder", sole_holder, &hctx, 0);
    struct cmb_process *pa = spawn("Early",  waiter, &a, 0);  /* arrives first */
    struct cmb_process *pb = spawn("Late",   waiter, &b, 0);  /* arrives second */

    /* While both are queued (t=5), promote the later arrival above the earlier. */
    (void)cmb_event_schedule(reprioritize_evt, pb, (void *)INT64_C(100), 5.0, 0);

    cmb_event_queue_execute();

    cmb_assert_always(b.acquired_order == 1);  /* promotion jumped the queue */
    cmb_assert_always(a.acquired_order == 2);

    reap(h); reap(pa); reap(pb);
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

static void scenario_timeout(void)
{
    printf("Scenario 5: a queued waiter times out without stranding the next\n");
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "Pool", 1u);

    struct waiter_ctx hctx = { &pool, 1u, 0.0, 10.0, 0.0, NULL, 0, -1.0, 0 };
    /* First waiter gives up at t=1+5=6; second keeps waiting. */
    struct waiter_ctx a = { &pool, 1u, 1.0, 1.0, 5.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx b = { &pool, 1u, 2.0, 1.0, 0.0, NULL, 0, -1.0, 0 };

    struct cmb_process *h  = spawn("Holder",  sole_holder, &hctx, 0);
    struct cmb_process *pa = spawn("Impatient", waiter, &a, 0);
    struct cmb_process *pb = spawn("Patient",   waiter, &b, 0);

    cmb_event_queue_execute();

    cmb_assert_always(a.result == CMB_PROCESS_TIMEOUT);   /* gave up */
    cmb_assert_always(b.result == CMB_PROCESS_SUCCESS);   /* not stranded */
    cmb_assert_always(b.acquired_time == 10.0);           /* woken on release */
    cmb_assert_always(pool.available == 1u);

    reap(h); reap(pa); reap(pb);
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

static void scenario_stop(void)
{
    printf("Scenario 6: a queued waiter is stopped without stranding the next\n");
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "Pool", 1u);

    struct waiter_ctx hctx = { &pool, 1u, 0.0, 10.0, 0.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx a = { &pool, 1u, 1.0, 1.0, 0.0, NULL, 0, -1.0, 0 };
    struct waiter_ctx b = { &pool, 1u, 2.0, 1.0, 0.0, NULL, 0, -1.0, 0 };
    waiter_ctx_reset(&a);

    struct cmb_process *h  = spawn("Holder",  sole_holder, &hctx, 0);
    struct cmb_process *pa = spawn("Doomed",  waiter, &a, 0);   /* queued first */
    struct cmb_process *pb = spawn("Survivor", waiter, &b, 0);  /* queued behind */

    /* While both are queued (t=5), stop the one in front. */
    (void)cmb_event_schedule(stop_evt, pa, NULL, 5.0, 0);

    cmb_event_queue_execute();

    cmb_assert_always(a.result == INT64_MIN);            /* never returned from acquire */
    cmb_assert_always(b.result == CMB_PROCESS_SUCCESS);  /* not stranded */
    cmb_assert_always(b.acquired_time == 10.0);          /* woken on release */
    cmb_assert_always(pool.available == 1u);

    /* pa was stopped: it is FINISHED but never ran its release path. */
    reap(h); reap(pa); reap(pb);
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

/* Top up pool `subject` by one token, then ring the *observed* guard `object`
 * (not the pool's own guard) so the wakeup can only reach a waiter on the pool
 * via the observer-forwarding link. */
static void topup_and_signal_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);
    cmb_assert_always(object != NULL);

    struct token_pool *bp = subject;
    struct cmb_resourceguard *observed = object;
    bp->available += 1u;
    cmb_logger_user(stdout, USERFLAG, "Topped up %s, ringing the observed guard",
                    bp->base.name);
    (void)cmb_resourceguard_signal(observed);
}

static void scenario_observers(void)
{
    printf("Scenario 7: a signal is forwarded to a registered observer guard\n");
    cmb_event_queue_initialize(0.0);

    /* Two independent pools; B's guard is registered as an observer of A's,
     * so signalling A also re-evaluates B. */
    struct token_pool a;
    struct token_pool b;
    token_pool_initialize(&a, "PoolA", 1u);
    token_pool_initialize(&b, "PoolB", 1u);
    cmb_resourceguard_register(&(a.guard), &(b.guard));

    b.available = 0u;   /* B starts empty, so its waiter blocks */

    struct waiter_ctx bctx = { &b, 1u, 1.0, 0.0, 0.0, NULL, 0, -1.0, 0 };
    struct cmb_process *pb = spawn("UserB", waiter, &bctx, 0);

    /* At t=3 top up B but ring only A's bell; the observer link must carry the
     * signal across so B re-evaluates its front waiter and wakes UserB. */
    (void)cmb_event_schedule(topup_and_signal_evt, &b, &(a.guard), 3.0, 0);

    cmb_event_queue_execute();

    cmb_assert_always(bctx.result == CMB_PROCESS_SUCCESS);
    cmb_assert_always(bctx.acquired_time == 3.0);   /* woken via the observer */

    cmb_assert_always(cmb_resourceguard_unregister(&(a.guard), &(b.guard)));
    reap(pb);
    token_pool_terminate(&a);
    token_pool_terminate(&b);
    cmb_event_queue_terminate();
}

/* ───────────────────────────────────────────────────────────────────────────
 * Randomized soak: many processes contend for a small pool under mixed
 * priorities, with periodic reprioritizations, then every process is stopped.
 * This drives the same enqueue / reprioritize / remove paths repeatedly with
 * RNG-chosen timing, locking seed-dependent behavior into the reference file.
 * ───────────────────────────────────────────────────────────────────────── */

#define SOAK_PROCS 6u

void *soak_worker(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_always(ctx != NULL);
    struct token_pool *tp = ctx;

    for (;;) {
        const uint64_t need = (uint64_t)cmb_random_dice(1, 2);
        const int64_t sig = token_acquire(tp, need);
        if (sig == CMB_PROCESS_SUCCESS) {
            const double dt = cmb_random_exponential(1.0);
            cmb_assert_always(dt >= 0.0);
            (void)cmb_process_hold(dt);
            token_release(tp, need);
        }

        const double idle = cmb_random_exponential(1.0);
        cmb_assert_always(idle >= 0.0);
        (void)cmb_process_hold(idle);
    }
}

/* Periodically shuffle the priority of a randomly chosen worker. */
struct shuffler_ctx {
    struct cmb_process **procs;
    uint64_t nprocs;
};

void *soak_shuffler(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_always(ctx != NULL);
    struct shuffler_ctx *s = ctx;

    for (;;) {
        const double dt = cmb_random_exponential(0.5);
        cmb_assert_always(dt >= 0.0);
        (void)cmb_process_hold(dt);

        const int64_t which = cmb_random_dice(0, (int64_t)s->nprocs - 1);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_priority_set(s->procs[which], pri);
    }
}

static void stop_all_evt(void *subject, void *object)
{
    cmb_assert_always(subject != NULL);

    struct cmb_process **procs = subject;
    const uint64_t n = (uint64_t)object;
    cmb_logger_user(stdout, USERFLAG, "===> soak over, stopping all <===");
    for (uint64_t ui = 0; ui < n; ui++) {
        const int64_t r = cmb_process_stop(procs[ui], NULL);
        cmb_assert_always(r == CMB_PROCESS_SUCCESS);
        cmb_assert_always(cmb_process_status(procs[ui]) == CMB_PROCESS_FINISHED);
    }
}

static void scenario_soak(const double dur)
{
    printf("Scenario 8: randomized contention with reprioritization (dur=%g)\n", dur);
    cmb_event_queue_initialize(0.0);

    struct token_pool pool;
    token_pool_initialize(&pool, "SoakPool", 2u);

    struct cmb_process *procs[SOAK_PROCS + 1u];   /* + shuffler */
    for (unsigned ui = 0; ui < SOAK_PROCS; ui++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Worker_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-2, 2);
        procs[ui] = spawn(buf, soak_worker, &pool, pri);
    }

    struct shuffler_ctx sctx = { procs, SOAK_PROCS };
    procs[SOAK_PROCS] = spawn("Shuffler", soak_shuffler, &sctx, 0);

    (void)cmb_event_schedule(stop_all_evt, procs,
                             (void *)(uint64_t)(SOAK_PROCS + 1u), dur, 0);

    cmi_test_print_line("-");
    cmb_event_queue_execute();
    cmi_test_print_line("-");

    /* A worker stopped mid-hold never runs its release, so some tokens may be
     * stranded. The meaningful invariant is that the count stays sane and that
     * tearing down the guard finds no stranded queue entries (which would trip
     * an assert inside terminate if a stopped waiter had been left enqueued). */
    cmb_assert_always(pool.available <= pool.capacity);

    for (unsigned ui = 0; ui < SOAK_PROCS + 1u; ui++) {
        reap(procs[ui]);
    }
    token_pool_terminate(&pool);
    cmb_event_queue_terminate();
}

void test_resourceguard(const uint64_t seed, const double dur)
{
    cmi_test_print_line("*");
    printf("**************************   Testing resourceguards   *************************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    cmb_random_initialize(seed);
    cmb_logger_flags_off(CMB_LOGGER_INFO);

    scenario_basic();
    scenario_fifo();
    scenario_priority();
    scenario_reprioritize();
    scenario_timeout();
    scenario_stop();
    scenario_observers();
    scenario_soak(dur);

    printf("All resourceguard scenarios passed\n");

    cmb_random_terminate();

    cmi_test_print_line("*");
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur = 50.0;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:t")) != -1) {
        switch (opt) {
            case 'd': {
                errno = 0;
                dur = strtof(optarg, NULL);
                if (errno != 0 || dur <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            }

            case 's': {
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            }

            case 't': {
                timing_enabled = true;
                break;
            }

            default: {
                fprintf(stderr, "Usage: %s [-d <dur>][-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
            }
        }
    }

    const clock_t start_time = clock();

    test_resourceguard(seed, dur);

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}
