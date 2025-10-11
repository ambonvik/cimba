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
#include <stdio.h>

#include "cmb_logger.h"
#include "cmb_process.h"
#include "cmb_random.h"

#include "cmi_test.h"

#define USERFLAG 0x00000001

void *procfunc1(struct cmb_process *me, void *ctx)
{
    cmb_logger_user(USERFLAG, stdout, "procfunc1 running me %p ctx %p", (void *)me, ctx);
    for (unsigned ui = 0u; ui < 10u; ui++) {
        const double dur = cmb_random_exponential(5.0);
        const int64_t sig = cmb_process_hold(dur);
        if (sig == CMB_PROCESS_HOLD_NORMAL) {
            cmb_logger_user(USERFLAG, stdout, "Hold returned normal signal %lld", sig);
        }
        else {
            cmb_logger_user(USERFLAG, stdout, "Hold was interrupted signal %lld", sig);
        }
    }

    cmb_process_exit((void *)0x5EAF00D);
    /* not reached */
    return (void *)0xBADF00D;
}

void *procfunc2(struct cmb_process *me, void *ctx)
{
    cmb_logger_user(USERFLAG, stdout, "procfunc2 running me %p ctx %p", (void *)me, ctx);
    struct cmb_process *tgt = (struct cmb_process *)ctx;
    const int16_t pri = cmb_process_get_priority(me);
    for (unsigned ui = 0u; ui < 3u; ui++) {
        const double dur = cmb_random_exponential(10.0);
        (void)cmb_process_hold(dur);
        cmb_process_interrupt(tgt, CMB_PROCESS_HOLD_INTERRUPTED, pri);
    }

    const double dur = cmb_random_exponential(10.0);
    (void)cmb_process_hold(dur);
    cmb_process_stop(tgt, (void *)0xABBA);

    cmb_process_exit((void *)0x5EAF00D);
    /* not reached */
    return (void *)0xBADF00D;
}


int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing processes   *****************************\n");
    cmi_test_print_line("*");

    printf("cmb_process_create ...\n");
    struct cmb_process *cpp1 = cmb_process_create("Testproc", procfunc1, NULL, 0);
    struct cmb_process *cpp2 = cmb_process_create("Nuisance", procfunc2, cpp1, 1);

    printf("cmb_process_start ...\n");
    cmb_event_queue_init(0.0);
    cmb_process_start(cpp1);
    cmb_process_start(cpp2);

    printf("cmb_run ...\n");
    cmb_run();

    printf("%s returned %p\n", cmb_process_get_name(cpp1), cmb_process_get_exit_value(cpp1));
    printf("%s returned %p\n", cmb_process_get_name(cpp2), cmb_process_get_exit_value(cpp2));

    printf("cmb_process_destroy ...\n");
    cmb_process_destroy(cpp1);
    cmb_process_destroy(cpp2);

    cmb_event_queue_destroy();
    cmi_test_print_line("*");
    return 0;
}