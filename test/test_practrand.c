/*
 * Script for feeding random numbers to PractRand for testing
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
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cimba.h"
#include "cmb_random.h"

int main(const int argc, char *argv[])
{
    uint64_t master_seed = cmb_random_hwseed();
    size_t words_per_seed = 64;

    int opt;
    while ((opt = getopt(argc, argv, "n:s:")) != -1) {
        switch (opt) {
            case 'n':
                errno = 0;
                words_per_seed = (size_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || words_per_seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 's':
                errno = 0;
                master_seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <master_seed>][-n <words_per_seed>]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    fprintf(stderr, "test_practrand, Cimba version %s\n", cimba_version());
    fprintf(stderr, "master_seed = 0x%016" PRIx64 ", words_per_seed = %zu\n\n",
            master_seed, words_per_seed);

    static uint64_t buf[16384];          /* 128 KiB per write */
    size_t   n = 0;                       /* words currently in buf */
    uint64_t i = 0;                       /* trial index */

    for (;;) {
        /* A new batch */
        cmb_random_initialize(cmb_random_fmix64(master_seed, i++));

        for (size_t j = 0; j < words_per_seed; j++) {
            /* Generate sample to buffer */
            buf[n++] = cmb_random_sfc64();
            if (n == sizeof buf / sizeof buf[0]) {
                /* Write the buffered samples */
                if (fwrite(buf, sizeof buf, 1, stdout) != 1) {
                    /* PractRand closed the pipe */
                    cmb_random_terminate();
                    return 0;
                }

                /* Restart buffer */
                n = 0;
            }
        }
    }

    /* Not reached */
}
