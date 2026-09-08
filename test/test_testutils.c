/*
 * Test harness for the test utilities themselves.
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

/* Make sure we get the math constants we need */
#define _XOPEN_SOURCE 500
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cmb_assert.h"
#include "cmb_dataset.h"
#include "cmb_random.h"
#include "testutils.h"


static double twice(double x, void *arg)
{
    cmb_unused(arg);

    return 2.0 * x;
}


static double half(double x, void *arg)
{
    cmb_unused(arg);

    return 0.5 * x;
}


static void test_transform(const uint64_t nsamples)
{
    struct cmb_dataset ods = { 0 };
    struct cmb_dataset tds = { 0 };
    struct cmb_datasummary dsu = { 0 };

    cmi_test_print_line("-");
    printf("Testing transformations\n");
    cmb_dataset_initialize(&ods);
    cmb_dataset_initialize(&tds);
    cmb_datasummary_initialize(&dsu);

    for (uint64_t ui = 0u; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_dataset_add(&ods, x);
    }

    cmb_dataset_summarize(&ods, &dsu);
    cmb_datasummary_print(&dsu, stdout, true);

    cmi_test_transform(&tds, &ods, twice, NULL);
    cmb_dataset_summarize(&tds, &dsu);
    cmb_datasummary_print(&dsu, stdout, false);

    cmi_test_transform(&tds, &tds, half, NULL);
    cmb_dataset_summarize(&tds, &dsu);
    cmb_datasummary_print(&dsu, stdout, false);

    for (uint64_t ui = 0; ui < ods.count; ui++) {
        cmb_assert_release(tds.xa[ui] == ods.xa[ui]);
    }

    cmb_datasummary_terminate(&dsu);
    cmb_dataset_terminate(&tds);
    cmb_dataset_terminate(&ods);
}

/* Test goodness of fit against a know good and a known bad example */
static void test_gof(const uint64_t nsamples)
{
    struct cmb_dataset ds_good = { 0 };
    struct cmb_dataset ds_bad = { 0 };
    struct cmb_dataset ds_poisoned = { 0 };
    struct cmb_datasummary dsu = { 0 };

    cmi_test_print_line("-");
    printf("Testing goodness-of-fit\n");
    cmb_dataset_initialize(&ds_good);
    cmb_datasummary_initialize(&dsu);

    printf("Generating %" PRIu64 " actual U(0,1) samples\n", nsamples);
    for (uint64_t ui = 0u; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_dataset_add(&ds_good, x);
    }

    cmb_dataset_summarize(&ds_good, &dsu);
    cmb_datasummary_print(&dsu, stdout, true);
    cmb_dataset_histogram_print(&ds_good, stdout, 20, 0.0, 1.0);

    struct cmi_test_outcome *result = cmi_malloc(sizeof(*result));
    cmi_test_u01(&ds_good, result);
    cmi_test_outcome_print(result, stdout);

    cmb_datasummary_terminate(&dsu);
    cmb_dataset_initialize(&ds_bad);
    cmb_datasummary_initialize(&dsu);

    const double alpha = 0.99;
    const double beta = 0.99;
    printf("Generating %" PRIu64 " not quite U(0,1) samples - Beta(%g,%g)\n",
            nsamples, alpha, beta);
    for (uint64_t ui = 0u; ui < nsamples; ui++) {
        const double x = cmb_random_std_beta(alpha, beta);
        cmb_dataset_add(&ds_bad, x);
    }

    cmb_dataset_summarize(&ds_bad, &dsu);
    cmb_datasummary_print(&dsu, stdout, true);
    cmb_dataset_histogram_print(&ds_bad, stdout, 20, 0.0, 1.0);

    cmi_test_u01(&ds_bad, result);
    cmi_test_outcome_print(result, stdout);

    cmb_datasummary_terminate(&dsu);

    printf("Creating a poisoned sample\n");
    cmb_dataset_initialize(&ds_poisoned);
    cmb_dataset_copy(&ds_poisoned, &ds_good);
    cmb_dataset_add(&ds_poisoned, 1.001);
    cmb_dataset_summarize(&ds_poisoned, &dsu);
    cmb_datasummary_print(&dsu, stdout, true);
    cmb_dataset_histogram_print(&ds_poisoned, stdout, 20, 0.0, 1.0);

    cmi_test_u01(&ds_poisoned, result);
    cmi_test_outcome_print(result, stdout);

    cmb_datasummary_terminate(&dsu);
    cmb_dataset_terminate(&ds_poisoned);

    printf("Creating another poisoned sample\n");
    cmb_dataset_initialize(&ds_poisoned);
    cmb_dataset_copy(&ds_poisoned, &ds_good);
    for (unsigned ui = 0; ui < 5; ui++) {
        cmb_dataset_add(&ds_poisoned, 0.0);
        cmb_dataset_add(&ds_poisoned, 1.0);
    }
    cmb_dataset_summarize(&ds_poisoned, &dsu);
    cmb_datasummary_print(&dsu, stdout, true);
    cmb_dataset_histogram_print(&ds_poisoned, stdout, 20, 0.0, 1.0);

    cmi_test_u01(&ds_poisoned, result);
    cmi_test_outcome_print(result, stdout);

    cmb_datasummary_terminate(&dsu);
    cmb_dataset_terminate(&ds_poisoned);
    cmb_dataset_terminate(&ds_bad);
    cmb_dataset_terminate(&ds_good);
    cmi_free(result);
}

/* Deterministic internal tests against reference values for static functions */
extern void cmi_test_selftests();

int main(const int argc, char *argv[])
{
    uint64_t seed = cmb_random_hwseed();
    uint64_t nsamples = 1000000u;

    int opt;
    while ((opt = getopt(argc, argv, "n:s:")) != -1) {
        switch (opt) {
            case 'n':
                errno = 0;
                nsamples = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || nsamples == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-n <nsamples>][-s <seed>]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("***************************** Testing test harness *****************************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    cmi_test_selftests();
    test_transform(nsamples);
    test_gof(nsamples);

    cmb_random_terminate();

    cmi_test_print_line("*");

    return 0;
}

