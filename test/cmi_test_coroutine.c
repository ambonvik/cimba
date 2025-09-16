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

/* Assembly functions, see arch/cmi_coroutine_context_*.asm */
extern int asm_test(int i, int j, int k);
extern void *cmi_coroutine_get_rsp(void);
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

int main(void) {
    printf("Calls asm_test(17, 18, 19) ... ");
    int r = asm_test(17, 18, 19);
    printf("returned %d\n", r);

    const int i = 42;
    const int j = 43;
    const int k = 44;
    printf("Calls asm_test(%d, %d, %d) ... ", i, j, k);
    r = asm_test(i, j, k);
    printf("returned %d\n", r);

    void *sbp = cmi_coroutine_get_stackbase();
    printf("Current stack base: %p\n", sbp);
    void *rsp = cmi_coroutine_get_rsp();
    printf("Current stack pointer: %p\n", rsp);
    void *slp = cmi_coroutine_get_stacklimit();
    printf("Current stack limit: %p\n", slp);
    printf("Stack size: %llu\n", (uintptr_t)sbp - (uintptr_t)slp);

    return 0;
}