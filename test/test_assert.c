/*
 * Test script for custom asserts
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cmb_assert.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(const int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif

    if (argc < 2) {
        printf("Usage: test_assert <release|debug>\n");
        exit(2);
    }

#ifdef NASSERT
    printf("NASSERT is defined\n");
#else
    printf("NASSERT is not defined\n");
#endif
#ifdef NDEBUG
    printf("NDEBUG is defined\n");
#else
    printf("NDEBUG is not defined\n");
#endif

    if (strcmp(argv[1], "release") == 0)  {
        /* Should trip unless NASSERT is defined */
        cmb_assert_release(false);
    }
    else if (strcmp(argv[1], "debug") == 0) {
        /* Should trip unless NASSERT or NDEBUG are defined */
        cmb_assert_debug(false);
    }
    else {
        printf("Usage: test_assert <release|debug>\n");
        exit(2);
    }

    /* Made it through, no asserts tripped */
    exit(0);
}
