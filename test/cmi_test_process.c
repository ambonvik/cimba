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

#include "cmi_test.h"

void *procfunc1(struct cmb_process *me, void *ctx)
{
    cmb_logger_info(stdout, "procfunc1: me %s %p ctx %p", me->name, (void *)me, ctx);
    for (unsigned ui = 0u; ui < 5u; ui++) {
        cmb_process_hold(10.0);
        cmb_logger_info(stdout, "procfunc1: me %s %p ctx %p", me->name, (void *)me, ctx);
    }

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
    struct cmb_process *cpp = cmb_process_create("Testproc", procfunc1, NULL, 0);

    printf("cmb_process_start ...\n");
    cmb_event_queue_init(0.0);
    cmb_process_start(cpp);

    printf("cmb_run\n");
    cmb_run();

    printf("test process returned %p\n", cmb_process_get_exit_value(cpp));

    printf("cmb_process_destroy ...\n");
    cmb_process_destroy(cpp);

    cmb_event_queue_destroy();
    cmi_test_print_line("*");
    return 0;
}