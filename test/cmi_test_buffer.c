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

static void end_sim_evt(void *subject, void *object)
{
    struct cmb_process **cpp = subject;
    const uint64_t n = (uint64_t)object;
    cmb_logger_info(stdout, "===> end_sim: game over <===");
    for (unsigned ui = 0; ui < n; ui++) {
        cmb_process_stop(cpp[ui], NULL);
    }

    /* To be sure that we got everything */
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

        const uint64_t n = cmb_random_dice(1, 5);
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

        const uint64_t n = cmb_random_dice(1, 5);
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

    struct cmb_process **tgt = (struct cmb_process **)ctx;

    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        cmb_logger_user(USERFLAG, stdout, "Holding ...");
        (void)cmb_process_hold(cmb_random_exponential(1.0));
        const uint16_t vic = cmb_random_dice(0, 5);
        const int64_t sig = cmb_random_dice(1, 10);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_logger_user(USERFLAG, stdout, "Interrupting %s with %lld", tgt[vic]->name, sig);
        cmb_process_interrupt(tgt[vic], sig, pri);
    }
}

void test_buffer(void)
{
    const uint64_t seed = cmb_random_get_hwseed();
    cmb_random_initialize(seed);
    printf("seed: %llu\n", seed);

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    printf("Create a buffer\n");
    struct cmb_buffer *bp = cmb_buffer_create();
    cmb_buffer_initialize(bp, "Buf", 10u);

    char buf[32];
    struct cmb_process *cpp[7];
    printf("Create three processes feeding the buffer\n");
    for (unsigned ui = 0; ui < 3; ui++) {
        cpp[ui] = cmb_process_create();
        snprintf(buf, sizeof(buf), "Putter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(cpp[ui], buf, putterfunc, bp, pri);
        cmb_process_start(cpp[ui]);
    }

    printf("Create three processes consuming from the buffer\n");
    for (unsigned ui = 3; ui < 6; ui++) {
        cpp[ui] = cmb_process_create();
        snprintf(buf, sizeof(buf), "Getter_%u", ui + 1u);
        const int64_t pri = cmb_random_dice(-5, 5);
        cmb_process_initialize(cpp[ui], buf, getterfunc, bp, pri);
        cmb_process_start(cpp[ui]);
    }

    printf("Create a bloody nuisance\n");
    cpp[6] = cmb_process_create();
    cmb_process_initialize(cpp[6], "Nuisance", nuisancefunc, cpp, 0);
    cmb_process_start(cpp[6]);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, cpp, (void *)7u, 100.0, 0);

    printf("Execute simulation\n");
    cmb_event_queue_execute();

    printf("Clean up\n");
    for (unsigned ui = 0; ui < 6; ui++) {
        cmb_process_terminate(cpp[ui]);
        cmb_process_destroy(cpp[ui]);
    }

    cmb_buffer_destroy(bp);
    cmb_event_queue_terminate();
}

int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing buffers   *****************************\n");
    cmi_test_print_line("*");

    test_buffer();

    cmi_test_print_line("*");
    return 0;
}

