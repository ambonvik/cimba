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
#include <time.h>

#include "cmb_coroutine.h"
extern int asm_test(int i, int j, int k);

int main(void) {
    printf("Calls asm_test(17, 18, 19) ... ");
    int r = asm_test(17, 18, 19);
    printf("returned %d\n", r);

    int i = 42;
    int j = 43;
    int k = 44;
    printf("Calls asm_test(%d, %d, %d) ... ", i, j, k);
    r = asm_test(i, j, k);
    printf("returned %d\n", r);

    return 0;
}