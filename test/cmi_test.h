/*
 * Utility functions for test scripts
 *
 *
 * Copyright (c) Asbjørn M. Bonvik 1994 - 2025.
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

#ifndef CIMBA_CMI_TEST_H
#define CIMBA_CMI_TEST_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "cmb_assert.h"

static inline uint64_t cmi_test_create_seed(void) {
    struct timespec ts;
    (void) clock_gettime(CLOCK_REALTIME, &ts);

    return (uint64_t)(ts.tv_nsec ^ ts.tv_sec);
}

static inline void cmi_test_print_chars(const char *str, const uint16_t repeats) {
    cmb_assert_release(str != NULL);

    for (uint16_t ui = 0; ui < repeats; ui++) {
        printf("%s", str);
    }
}

static inline void cmi_test_print_line(const char *str) {
    cmb_assert_release(str != NULL);

    const uint16_t len = strlen(str);
    const uint16_t line_length = 80u;
    const uint16_t repeats = line_length / len;
    cmi_test_print_chars(str, repeats);
    printf("\n");
}

#endif /* CIMBA_CMI_TEST_H */