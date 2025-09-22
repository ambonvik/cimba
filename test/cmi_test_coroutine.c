/*
 * Test script for coroutines.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
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

#include "cmi_coroutine.h"
#include "cmi_test.h"

/* Assembly functions, see arch/cmi_coroutine_context_*.asm */
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

static void test_asm_calls() {
    printf("Testing assembly get functions\n");
    void *sbp = cmi_coroutine_get_stackbase();
    printf("Current stack base: %p\n", sbp);
    void *slp = cmi_coroutine_get_stacklimit();
    printf("Current stack limit: %p\n", slp);
    printf("Stack size: %llu\n", (uintptr_t)sbp - (uintptr_t)slp);

    cmi_test_print_line("=");
}

static void *corofunc_1(struct cmi_coroutine *myself, void *arg) {
    printf("corofunc_1(%p, %p) running\n", (void *)myself, arg);
    printf("corofunc_1 returning %p\n", arg);
    return arg;
}

int main(void) {
    cmi_test_print_line("*");
    printf("**********************         Testing coroutines         **********************\n");
    cmi_test_print_line("*");

    test_asm_calls();

    struct cmi_coroutine *cp = cmi_coroutine_create(24 * 1024);
    cmi_coroutine_start(cp, corofunc_1, 0x5EAF00Dull);

    return 0;
}