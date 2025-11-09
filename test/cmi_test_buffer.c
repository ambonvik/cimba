/*
* Test script for buffers
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

#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_buffer.h"

#include "cmi_test.h"
#include "cmi_memutils.h"

#define USERFLAG 0x00000001
#define NUM_PUTTERS 3u
#define NUM_GETTERS 3u

struct experiment {
    struct cmb_process *putters[NUM_PUTTERS];
    struct cmb_process *getters[NUM_PUTTERS];
    struct cmb_process *nuisance;
    struct cmb_buffer *buf;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_unused(object);

    struct experiment *texp = subject;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < NUM_PUTTERS; ui++) {
        cmb_process_stop(texp->putters[ui], NULL);
    }

    for (unsigned ui = 0; ui < NUM_GETTERS; ui++) {
        cmb_process_stop(texp->getters[ui], NULL);
    }

    cmb_process_stop(texp->nuisance, NULL);

    /* Make sure that we got everything */
    cmb_event_queue_clear();
}

void *putterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);
    struct cmb_buffer *bp = (struct cmb_buffer *) ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(USERFLAG, stdout, "Hold returned normally");
        }
        else {
            cmb_logger_user(USERFLAG, stdout, "Hold returned signal %lld", sig);
        }

        const uint64_t n = cmb_random_dice(1, 15);
        uint64_t m = n;
        cmb_logger_user(USERFLAG,
                        stdout,
                        "Putting %llu into %s...",
                        n,
                        cmb_buffer_get_name(bp));

        sig = cmb_buffer_put(bp, &m);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_assert_debug(m == 0u);
            cmb_logger_user(USERFLAG, stdout, "Put %llu succeeded", n);
        }
        else {
            cmb_logger_user(USERFLAG,
                            stdout,
                            "Put returned signal %lld, got %llu instead of %llu",
                            sig,
                            m,
                            n);
        }
    }
}

void *getterfunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    struct cmb_buffer *bp = (struct cmb_buffer *) ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Holding ...");
        int64_t sig = cmb_process_hold(cmb_random_exponential(1.0));
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_logger_user(USERFLAG, stdout, "Hold returned normally");
        }
        else {
            cmb_logger_user(USERFLAG, stdout, "Hold returned signal %lld", sig);
        }

        const uint64_t n = cmb_random_dice(1, 15);
        cmb_logger_user(USERFLAG,
                        stdout,
                        "Getting %llu from %s...",
                        n,
                        cmb_buffer_get_name(bp));

        uint64_t m = n;
        sig = cmb_buffer_get(bp, &m);
        if (sig == CMB_PROCESS_SUCCESS) {
            cmb_assert_debug(m == n);
            cmb_logger_user(USERFLAG, stdout, "Get %llu succeeded", n);
        }
        else {
            cmb_logger_user(USERFLAG,
                            stdout,
                            "Get returned signal %lld, got %llu instead of %llu",
                            sig,
                            m,
                            n);
        }
    }
}

void *nuisancefunc(struct cmb_process *me, void *ctx)
{
    cmb_unused(me);
    cmb_assert_release(ctx != NULL);

    /* Abuse internal knowledge of the content of the experiment struct */
    struct cmb_process **tgt = (struct cmb_process **)ctx;
    unsigned nproc = NUM_PUTTERS + NUM_GETTERS;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Holding ...");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        const uint16_t vic = cmb_random_dice(0, nproc - 1u);
        const int64_t sig = cmb_random_dice(1, 10);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_logger_user(USERFLAG, stdout, "Interrupting %s with %lld", tgt[vic]->name, sig);
        cmb_process_interrupt(tgt[vic], sig, pri);
    }
}

void test_buffer(double duration)
{
    struct experiment *buftst = cmi_malloc(sizeof(*buftst));
    cmi_memset(buftst, 0, sizeof(*buftst));

    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);
    printf("seed: %llx\n", seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG);
    cmb_event_queue_initialize(0.0);

    printf("Create a buffer\n");
    buftst->buf = cmb_buffer_create();
    cmb_buffer_initialize(buftst->buf, "Buf", 10u);
    cmb_buffer_start_recording(buftst->buf);

    char scratchpad[32];
    printf("Create three processes feeding into the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        buftst->putters[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Putter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(buftst->putters[ui], scratchpad, putterfunc, buftst->buf, pri);
        cmb_process_start(buftst->putters[ui]);
    }

    printf("Create three processes consuming from the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        buftst->getters[ui] = cmb_process_create();
        snprintf(scratchpad, sizeof(scratchpad), "Getter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(buftst->getters[ui], scratchpad, getterfunc, buftst->buf, pri);
        cmb_process_start(buftst->getters[ui]);
    }

    printf("Create a bloody nuisance\n");
    buftst->nuisance = cmb_process_create();
    cmb_process_initialize(buftst->nuisance, "Nuisance", nuisancefunc, buftst, 0);
    cmb_process_start(buftst->nuisance);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, buftst, NULL, duration, 0);

    printf("Execute simulation...\n");
    cmb_event_queue_execute();

    printf("Report statistics...\n");
    cmb_buffer_stop_recording(buftst->buf);
    cmb_buffer_print_report(buftst->buf, stdout);

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        cmb_process_terminate(buftst->getters[ui]);
        cmb_process_destroy(buftst->putters[ui]);
    }

    cmb_process_terminate(buftst->nuisance);
    cmb_process_destroy(buftst->nuisance);
    cmb_buffer_destroy(buftst->buf);
    cmb_event_queue_terminate();
    cmi_free(buftst);
}

int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing buffers   *****************************\n");
    cmi_test_print_line("*");

    test_buffer(100000);

    cmi_test_print_line("*");
    return 0;
}

