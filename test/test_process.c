/*
 * Test script for processes.
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
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_random.h"

#include "testutils.h"

#define USERFLAG1 0x00000001

static uint64_t cuckoo_clock_handle = 0u;

static void cuckooevtfunc(void *sub, void *obj)
{
    cmb_unused(sub);
    cmb_unused(obj);

    cmb_logger_user(stdout,USERFLAG1, "Cuckoo event occurred");
}

static void cnclevtfunc(void *sub, void *obj)
{
    cmb_unused(sub);
    cmb_unused(obj);

    cmb_assert_release(cuckoo_clock_handle != 0u);
     if (cmb_event_is_scheduled(cuckoo_clock_handle)) {
         cmb_logger_user(stdout, USERFLAG1, "Cancelling cuckoo event");
         const bool found = cmb_event_cancel(cuckoo_clock_handle);
         cmb_assert_always(found);
     }
     else {
         cmb_logger_user(stdout, USERFLAG1, "Cuckoo event already cancelled");
     }
}

static void *preemptable(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_unused(ctx);

    cmb_logger_user(stdout, USERFLAG1, "Running");
    for (;;) {
        const double dur = cmb_random_exponential(5.0);
        cmb_assert_always(dur >= 0.0);
        const int64_t sig = cmb_process_hold(dur);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Hold returned normal signal %" PRIi64, sig);
        }
        else {
            cmb_logger_user(stdout, USERFLAG1,
                            "Hold was interrupted signal %" PRIi64, sig);
        }
    }
}

static void *preempter(struct cmb_process *me, void *ctx)
{
    struct cmb_process *tgt = (struct cmb_process *)ctx;

    cmb_logger_user(stdout, USERFLAG1, "Running, tgt %s", cmb_process_name(tgt));
    const int64_t pri = cmb_process_priority(me);
    for (unsigned ui = 0u; ui < 5u; ui++) {
        const double dur = cmb_random_exponential(10.0);
        cmb_assert_always(dur >= 0.0);
        const int64_t sig = cmb_process_hold(dur);
        cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
        cmb_process_interrupt(tgt, CMB_PROCESS_INTERRUPTED, pri);
    }

    const double dur = cmb_random_exponential(10.0);
    cmb_assert_always(dur >= 0.0);
    const int64_t sig = cmb_process_hold(dur);
    cmb_assert_always(sig == CMB_PROCESS_SUCCESS);
    cmb_process_stop(tgt, (void *)0xABBA);

    cmb_process_exit((void *)0x5EAF00D);

    /* not reached */
    return NULL;
}

static void *procfunc3(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    struct cmb_process *tgt = (struct cmb_process *)ctx;

    cmb_logger_user(stdout, USERFLAG1, "Running, tgt %s", cmb_process_name(tgt));
    int64_t r = cmb_process_wait_event(cuckoo_clock_handle);
    cmb_logger_user(stdout, USERFLAG1, "Got cuckoo clock signal %" PRIi64, r);

    r = cmb_process_hold(cmb_random());
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    cmb_logger_user(stdout, USERFLAG1,
                    "Waiting for process %s", cmb_process_name(tgt));
    r = cmb_process_wait_process(tgt);
    cmb_assert_always(r == CMB_PROCESS_SUCCESS);
    cmb_logger_user(stdout, USERFLAG1,
                    "Tgt %s ended, we received signal %" PRIi64,
                    cmb_process_name(tgt), r);

    cmb_process_exit(NULL);

    /* not reached */
    return NULL;
}

static void test_process(uint64_t seed)
{
    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("****************************   Testing processes   *****************************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    printf("cmb_event_queue_initialize ...\n");
    cmb_event_queue_initialize(0.0);
    cmb_assert_always(cmb_time() == 0.0);
    printf("cmb_process_create ...\n");
    struct cmb_process *cpp1 = cmb_process_create();
    cmb_assert_always(cpp1 != NULL);
    struct cmb_process *cpp2 = cmb_process_create();
    cmb_assert_always(cpp2 != NULL);
    printf("cmb_process_initialize ...\n");
    cmb_process_initialize(cpp1, "Testproc", preemptable, NULL, 0);
    cmb_process_initialize(cpp2,"Nuisance", preempter, cpp1, 1);

    printf("cmb_process_start ...\n");
    cmb_process_start(cpp1);
    cmb_process_start(cpp2);

    printf("Creating an event and a race condition to cancel it...\n");
    cuckoo_clock_handle = cmb_event_schedule(cuckooevtfunc, NULL, NULL,
                                             cmb_random_exponential(25.0), 0);
    cmb_assert_always(cuckoo_clock_handle != 0u);
    uint64_t cnclhdl = cmb_event_schedule(cnclevtfunc, NULL, NULL, cmb_random_exponential(25.0), 0);
    cmb_assert_always(cnclhdl != 0u);

    printf("Creating waiting processes ...\n");
    char buf[32];
    struct cmb_process *waiters[3];
    for (unsigned ui = 0u; ui < 3u; ui++) {
        sprintf(buf, "Waiter_%u", ui);
        waiters[ui] = cmb_process_create();
        cmb_assert_always(waiters[ui] != NULL);
        cmb_process_initialize(waiters[ui], buf, procfunc3, cpp2, cmb_random_dice(-5, 5));
        cmb_process_start(waiters[ui]);
    }

    cmi_test_print_line("-");
    cmb_event_queue_print(stdout, NULL);
    cmi_test_print_line("-");

    printf("cmb_event_queue_execute ...\n");
    cmb_event_queue_execute();

    printf("%s returned %p\n",
           cmb_process_name(cpp1),
           cmb_process_exit_value(cpp1));
    printf("%s returned %p\n",
           cmb_process_name(cpp2),
           cmb_process_exit_value(cpp2));

    printf("cmb_process_terminate ...\n");
    cmb_process_terminate(cpp1);
    cmb_process_terminate(cpp2);
    printf("cmb_process_destroy ...\n");
    cmb_process_destroy(cpp1);
    cmb_process_destroy(cpp2);

    for (unsigned ui = 0u; ui < 3u; ui++) {
        cmb_process_terminate(waiters[ui]);
        cmb_process_destroy(waiters[ui]);
    }

    printf("cmb_event_queue_terminate ...\n");
    cmb_event_queue_terminate();
    cmb_random_terminate();
    cmi_test_print_line("*");
}

/*
 * Wake order among processes waiting for the same process to finish.
 * Regression test for the FIFO fix in 3.1.2: waiters were woken in LIFO
 * order. Silent unless an assert trips.
 */
struct pwait_ctx {
    struct cmb_process *awaited;    /* the process to wait for */
    double arrive;                  /* delay before starting to wait */
    int *counter;                   /* shared running counter of wakeups */
    int order;                      /* counter value when woken, 0 if never */
    double woken;                   /* sim time when woken, -1.0 if never */
    int64_t sig;                    /* signal returned by the wait */
};

static void *pwait_job(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    const double dur = *(const double *)vctx;
    cmb_assert_always(cmb_process_hold(dur) == CMB_PROCESS_SUCCESS);

    return NULL;
}

static void *pwait_waiter(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);
    struct pwait_ctx *c = vctx;

    cmb_assert_always(cmb_process_hold(c->arrive) == CMB_PROCESS_SUCCESS);
    c->sig = cmb_process_wait_process(c->awaited);
    c->order = ++(*c->counter);
    c->woken = cmb_time();

    return NULL;
}

static void *pwait_interrupter(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_always(vctx != NULL);

    cmb_assert_always(cmb_process_hold(6.0) == CMB_PROCESS_SUCCESS);
    cmb_process_interrupt((struct cmb_process *)vctx, CMB_PROCESS_INTERRUPTED, 0);

    return NULL;
}

static void test_waiter_order(void)
{
    cmb_event_queue_initialize(0.0);
    int counter = 0;

    /* Same pattern as in test_event: W1..W5 start waiting for T (finishing
     * at t=10) from t=1..5. W4 has higher priority. W2 is interrupted at t=6.
     * When T finishes, the order must be W4, then W1, W3, W5. */
    static double tdur = 10.0;
    struct cmb_process *tp = cmb_process_create();
    cmb_process_initialize(tp, "T", pwait_job, &tdur, 0);
    cmb_process_start(tp);

    enum { NWAIT = 5 };
    struct pwait_ctx w[NWAIT];
    struct cmb_process *wp[NWAIT];
    for (unsigned ui = 0u; ui < NWAIT; ui++) {
        w[ui] = (struct pwait_ctx){ tp, (double)(ui + 1u), &counter, 0, -1.0, INT64_MIN };
        char name[16];
        snprintf(name, sizeof(name), "W%u", ui + 1u);
        wp[ui] = cmb_process_create();
        cmb_process_initialize(wp[ui], name, pwait_waiter, &w[ui], (ui == 3u) ? 5 : 0);
        cmb_process_start(wp[ui]);
    }

    struct cmb_process *ip = cmb_process_create();
    cmb_process_initialize(ip, "Interrupter", pwait_interrupter, wp[1], 0);
    cmb_process_start(ip);

    cmb_event_queue_execute();

    cmb_assert_always((w[1].sig == CMB_PROCESS_INTERRUPTED) && (w[1].woken == 6.0));
    cmb_assert_always(w[1].order == 1);
    const unsigned expected[] = { 3u, 0u, 2u, 4u };
    for (unsigned ui = 0u; ui < 4u; ui++) {
        const struct pwait_ctx *c = &w[expected[ui]];
        cmb_assert_always(c->sig == CMB_PROCESS_SUCCESS);
        cmb_assert_always(c->woken == 10.0);
        cmb_assert_always(c->order == (int)ui + 2);
    }

    cmb_process_terminate(ip);
    cmb_process_destroy(ip);
    for (unsigned ui = 0u; ui < NWAIT; ui++) {
        cmb_process_terminate(wp[ui]);
        cmb_process_destroy(wp[ui]);
    }
    cmb_process_terminate(tp);
    cmb_process_destroy(tp);

    cmb_event_queue_terminate();
}

/*
 * A stale completion wakeup must not end a later, unrelated wait. Regression
 * test for F6 (Fable 5.2, 2026-11-14). Silent unless an assert trips.
 */
struct stale_ctx {
    struct cmb_process *first;      /* waited for with a timeout */
    struct cmb_process *second;     /* waited for afterwards */
    int64_t sig1, sig2;             /* signals returned by the two waits */
    double t1, t2;                  /* sim times when they returned */
    int st1, st2;                   /* awaited process status at return */
};

static void *stale_waiter(struct cmb_process *me, void *vctx)
{
    cmb_assert_always(vctx != NULL);
    struct stale_ctx *c = vctx;

    (void)cmb_process_timer_add(me, 10.0, CMB_PROCESS_TIMEOUT);
    c->sig1 = cmb_process_wait_process(c->first);
    c->t1 = cmb_time();
    c->st1 = cmb_process_status(c->first);

    c->sig2 = cmb_process_wait_process(c->second);
    c->t2 = cmb_time();
    c->st2 = cmb_process_status(c->second);

    return NULL;
}

static void test_stale_wakeup(void)
{
    cmb_event_queue_initialize(0.0);

    /* A finishes at t=10, exactly when W's timeout on waiting for A fires.
     * A has higher priority, so it finishes first and schedules W's
     * completion wakeup, but W's timeout event is already ahead of that in
     * the queue. W resumes with TIMEOUT and at once waits for B, which runs
     * until t=100. The stale wakeup from A must then be discarded, not end
     * the wait for B at t=10. */
    static double adur = 10.0;
    static double bdur = 100.0;
    struct cmb_process *a = cmb_process_create();
    cmb_process_initialize(a, "A", pwait_job, &adur, 1);
    cmb_process_start(a);
    struct cmb_process *b = cmb_process_create();
    cmb_process_initialize(b, "B", pwait_job, &bdur, 0);
    cmb_process_start(b);

    struct stale_ctx ctx = { a, b, INT64_MIN, INT64_MIN, -1.0, -1.0, -1, -1 };
    struct cmb_process *w = cmb_process_create();
    cmb_process_initialize(w, "W", stale_waiter, &ctx, 0);
    cmb_process_start(w);

    cmb_event_queue_execute();

    /* The first wait must time out, otherwise there is no stale wakeup and
     * the rest of the test proves nothing */
    cmb_assert_always(ctx.sig1 == CMB_PROCESS_TIMEOUT);
    cmb_assert_always(ctx.t1 == 10.0);
    cmb_assert_always(ctx.st1 == CMB_PROCESS_FINISHED);

    cmb_assert_always(ctx.sig2 == CMB_PROCESS_SUCCESS);
    cmb_assert_always(ctx.t2 == 100.0);
    cmb_assert_always(ctx.st2 == CMB_PROCESS_FINISHED);

    cmb_process_terminate(w);
    cmb_process_destroy(w);
    cmb_process_terminate(b);
    cmb_process_destroy(b);
    cmb_process_terminate(a);
    cmb_process_destroy(a);

    cmb_event_queue_terminate();
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();

    int opt;
    while ((opt = getopt(argc, argv, "s:t")) != -1) {
        switch (opt) {
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0u) {
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

    test_process(seed);
    test_waiter_order();
    test_stale_wakeup();

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nIt took %g sec\n", elapsed_time);
    }

    return 0;
}