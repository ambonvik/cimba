/*
 * Test script for dataset collection and reporting.
 *
 * Uses the uniform random number distribution from cmb_random as test object.
 * Usage:
 *      test_data [-n <samples>][-s <seed>][-t]
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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
#include <float.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "cmb_dataset.h"
#include "cmb_datasummary.h"
#include "cmb_random.h"
#include "cmb_timeseries.h"
#include "cmb_wtdsummary.h"

#include "test.h"

/* Test macros */
#define MAX_SAMPLES 1000000u
#define SORT_SAMPLES 25u
#define MAX_LAG 20u
#define NUM_BINS 20u

static void test_summary(const uint64_t nsamples)
{
    printf("\nTesting data summaries\n");
    printf("Declaring local variable data summary on stack and initializing it: cmb_datasummary_initialize\n");
    struct cmb_datasummary ds;
    cmb_datasummary_initialize(&ds);
    cmb_assert_always(cmb_datasummary_count(&ds) == 0);
    cmb_assert_always(cmb_datasummary_min(&ds) == DBL_MAX);
    cmb_assert_always(cmb_datasummary_max(&ds) == -DBL_MAX);
    cmb_assert_always(cmb_datasummary_mean(&ds) == 0.0);
    cmb_assert_always(cmb_datasummary_variance(&ds) == 0.0);
    cmb_assert_always(cmb_datasummary_stddev(&ds) == 0.0);
    cmb_assert_always(cmb_datasummary_skewness(&ds) == 0.0);
    cmb_assert_always(cmb_datasummary_kurtosis(&ds) == 0.0);

    printf("Drawing %" PRIu64 " U(0,1) samples and adding to data summary: cmb_datasummary_add\n", nsamples);
        for (uint32_t ui = 0; ui < nsamples; ui++) {
            cmb_assert_always(cmb_datasummary_count(&ds) == ui);
            const double x = cmb_random();
            cmb_assert_always((x >= 0.0) && (x <= 1.0));
            const uint64_t un = cmb_datasummary_add(&ds, x);
            cmb_assert_always(un == cmb_datasummary_count(&ds));
            cmb_assert_always(un == ui + 1);
            cmb_assert_always(cmb_datasummary_max(&ds) >= x);
            cmb_assert_always(cmb_datasummary_min(&ds) <= x);
        }

    printf("\nBasic summary reporting functions:\n");
    cmi_test_print_line("-");
    printf("cmb_datasummary_count:\t%" PRIu64 "\n", cmb_datasummary_count(&ds));
    printf("cmb_datasummary_min:\t%#8.4g\n", cmb_datasummary_min(&ds));
    printf("cmb_datasummary_max:\t%#8.4g\n", cmb_datasummary_max(&ds));
    printf("cmb_datasummary_mean:\t%#8.4g\t(expected %#8.4g)\n", cmb_datasummary_mean(&ds), 0.5);
    printf("cmb_datasummary_variance:\t%#8.4g\t(expected %#8.4g)\n", cmb_datasummary_variance(&ds), 1.0/12.0);
    printf("cmb_datasummary_stddev:\t%#8.4g\t(expected %#8.4g)\n", cmb_datasummary_stddev(&ds), sqrt(1.0/12.0));
    printf("cmb_datasummary_skewness:\t%#8.4g\t(expected %#8.4g)\n", cmb_datasummary_skewness(&ds), 0.0);
    printf("cmb_datasummary_kurtosis:\t%#8.4g\t(expected %#8.4g)\n", cmb_datasummary_kurtosis(&ds), - 6.0 / 5.0);
    cmi_test_print_line("-");

    printf("\nSummary: cmb_datasummary_print\n");
    cmb_datasummary_print(&ds, stdout, true);
    printf("Summary without lead-ins:\n");
    cmb_datasummary_print(&ds, stdout, false);

    cmi_test_print_line("-");
    printf("\nOnce more, now on the heap: cmb_datasummary_create()\n");
    struct cmb_datasummary *dsp = cmb_datasummary_create();
    cmb_assert_always(dsp != NULL);
    cmb_datasummary_initialize(dsp);
    cmb_assert_always(cmb_datasummary_count(dsp) == 0);

    printf("Drawing %" PRIu64 " U(0,1) samples and adding to data summary: cmb_datasummary_add\n", nsamples);
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random_uniform(1.0, 2.0);
        cmb_assert_always((x >= 1.0) && (x <= 2.0));
        cmb_assert_always(cmb_datasummary_count(dsp) == ui);
        const uint64_t un = cmb_datasummary_add(dsp, x);
        cmb_assert_always(un == cmb_datasummary_count(dsp));
        cmb_assert_always(un == ui + 1);
        cmb_assert_always(cmb_datasummary_max(dsp) >= x);
        cmb_assert_always(cmb_datasummary_min(dsp) <= x);
    }

    printf("\nSummary: cmb_datasummary_print\n");
    cmb_datasummary_print(dsp, stdout, true);
    printf("\nMerging the two data summaries: cmb_datasummary_merge ... ");
    const uint64_t nn = cmb_datasummary_merge(dsp, dsp, &ds);
    printf("Returned %" PRIu64 " samples\n", nn);
    printf("Merged summary: cmb_datasummary_print\n");
    cmb_datasummary_print(dsp, stdout, true);

    printf("\nCleaning up: cmb_datasummary_terminate, cmb_datasummary_destroy\n");
    cmb_datasummary_terminate(&ds);
    cmb_datasummary_terminate(dsp);
    cmb_datasummary_destroy(dsp);

    cmi_test_print_line("=");
}

static void test_wsummary(const uint64_t nsamples)
{
    printf("\nTesting weighted data summaries\n");
    printf("Weighted and unweighted in parallel, all weights set to 1.0\n");
    struct cmb_datasummary ds;
    cmb_datasummary_initialize(&ds);
    cmb_assert_always(cmb_datasummary_count(&ds) == 0);
    struct cmb_wtdsummary dws;
    cmb_wtdsummary_initialize(&dws);
    cmb_assert_always(cmb_wtdsummary_count(&dws) == 0);
    cmb_assert_always(cmb_wtdsummary_max(&dws) == -DBL_MAX);
    cmb_assert_always(cmb_wtdsummary_min(&dws) == DBL_MAX);
    cmb_assert_always(cmb_wtdsummary_mean(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_variance(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_stddev(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_skewness(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_kurtosis(&dws) == 0.0);

    printf("Drawing %" PRIu64 " U(0,1) samples...\n", nsamples);
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_assert_always((x >= 0.0) && (x <= 1.0));
        cmb_assert_always(cmb_datasummary_count(&ds) == ui);
        cmb_assert_always(cmb_wtdsummary_count(&dws) == ui);
        const uint64_t un = cmb_datasummary_add(&ds, x);
        cmb_assert_always(un == cmb_datasummary_count(&ds));
        cmb_assert_always(un == ui + 1);
        const uint64_t um = cmb_wtdsummary_add(&dws, x, 1.0);
        cmb_assert_always(um == cmb_wtdsummary_count(&dws));
        cmb_assert_always(um == un);
        cmb_assert_always(cmb_wtdsummary_max(&dws) >= x);
        cmb_assert_always(cmb_wtdsummary_min(&dws) <= x);
    }

    printf("\n\t\tUnweighted\tWeighted\tExpected:\n");
    cmi_test_print_line("-");
    printf("Count:   \t%" PRIu64 " \t%" PRIu64 " \t%" PRIu64 "\n", cmb_datasummary_count(&ds), cmb_wtdsummary_count(&dws), nsamples);
    printf("Minimum: \t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_min(&ds), cmb_wtdsummary_min(&dws), 0.0);
    printf("Maximum: \t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_max(&ds), cmb_wtdsummary_max(&dws), 1.0);
    printf("Mean:    \t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_mean(&ds), cmb_wtdsummary_mean(&dws), 0.5);
    printf("Variance:\t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_variance(&ds), cmb_wtdsummary_variance(&dws), 1.0/12.0);
    printf("StdDev:  \t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_stddev(&ds), cmb_wtdsummary_stddev(&dws), sqrt(1.0/12.0));
    printf("Skewness:\t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_skewness(&ds), cmb_wtdsummary_skewness(&dws), 0.0);
    printf("Kurtosis:\t%#8.4g\t%#8.4g\t%#8.4g\n", cmb_datasummary_kurtosis(&ds), cmb_wtdsummary_kurtosis(&dws), - 6.0 / 5.0);
    cmi_test_print_line("-");

    printf("\nSummary: cmb_wtdsummary_print\n");
    cmb_wtdsummary_print(&dws, stdout, true);
    printf("Summary without lead-ins, tab separated:\n");
    cmb_wtdsummary_print(&dws, stdout, false);

    printf("\nCleaning up: cmb_datasummary_reset, cmb_wtdsummary_reset\n");
    cmb_datasummary_reset(&ds);
    cmb_assert_always(cmb_datasummary_count(&ds) == 0);
    cmb_wtdsummary_reset(&dws);
    cmb_assert_always(cmb_wtdsummary_count(&dws) == 0);
    cmi_test_print_line("-");

    printf("\nDrawing %" PRIu64 " new x ~ U(0,1) samples weighted by 1.5 - x\n", nsamples);
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_assert_always((x >= 0.0) && (x <= 1.0));
        const double w = 1.5 - x;
        cmb_assert_always(cmb_datasummary_count(&ds) == ui);
        cmb_assert_always(cmb_wtdsummary_count(&dws) == ui);
        const uint64_t un = cmb_datasummary_add(&ds, x);
        cmb_assert_always(un == cmb_datasummary_count(&ds));
        cmb_assert_always(un == ui + 1);
        const uint64_t um = cmb_wtdsummary_add(&dws, x, w);
        cmb_assert_always(um == cmb_wtdsummary_count(&dws));
        cmb_assert_always(um == un);
        cmb_assert_always(cmb_wtdsummary_count(&dws) == ui + 1);
        cmb_assert_always(cmb_wtdsummary_max(&dws) >= x);
        cmb_assert_always(cmb_wtdsummary_min(&dws) <= x);
    }

    printf("Sum of weights: %#8.4g\n", dws.wsum);
    printf("Weighted:   ");;
    cmb_wtdsummary_print(&dws, stdout, true);
    printf("Unweighted: ");;
    cmb_datasummary_print(&ds, stdout, true);
    cmb_datasummary_reset(&ds);
    cmi_test_print_line("-");


    printf("\nCreating another weighted data summary on the heap: cmb_wtdsummary_create\n");
    struct cmb_wtdsummary *dwp = cmb_wtdsummary_create();
    cmb_assert_always(dwp != NULL);
    cmb_wtdsummary_initialize(dwp);
    cmb_assert_always(cmb_wtdsummary_count(dwp) == 0);
    cmb_assert_always(cmb_wtdsummary_max(dwp) == -DBL_MAX);
    cmb_assert_always(cmb_wtdsummary_min(dwp) == DBL_MAX);
    cmb_assert_always(cmb_wtdsummary_mean(dwp) == 0.0);
    cmb_assert_always(cmb_wtdsummary_variance(dwp) == 0.0);
    cmb_assert_always(cmb_wtdsummary_stddev(dwp) == 0.0);
    cmb_assert_always(cmb_wtdsummary_skewness(dwp) == 0.0);
    cmb_assert_always(cmb_wtdsummary_kurtosis(dwp) == 0.0);

    printf("Drawing %" PRIu64 " new x ~ U(0,1) samples randomly weighted on U(1,5)\n", nsamples);
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_assert_always((x >= 0.0) && (x <= 1.0));
        const double w = cmb_random_uniform(1.0, 5.0);
        cmb_assert_always((w >= 1.0) && (w <= 5.0));
        cmb_assert_always(cmb_wtdsummary_count(dwp) == ui);
        const uint64_t un = cmb_wtdsummary_add(dwp, x, w);
        cmb_assert_always(un == cmb_wtdsummary_count(dwp));
        cmb_assert_always(un == ui + 1);
        cmb_assert_always(cmb_wtdsummary_max(dwp) >= x);
        cmb_assert_always(cmb_wtdsummary_min(dwp) <= x);
    }

    printf("Summary: cmb_wtdsummary_print\n");
    printf("Old: ");
    cmb_wtdsummary_print(&dws, stdout, true);
    printf("New: ");
    cmb_wtdsummary_print(dwp, stdout, true);

    printf("\nMerging the two: cmb_wtdsummary_merge ... ");
    const uint64_t nm = cmb_wtdsummary_merge(dwp, dwp, &dws);
    printf("Returned %" PRIu64 "\n", nm);
    printf("Merged summary: cmb_wtdsummary_print\n");
    cmb_wtdsummary_print(dwp, stdout, true);
    printf("Cleaning up: cmb_wtdsummary_terminate, cmb_wtdsummary_destroy\n");
    cmb_wtdsummary_terminate(&dws);
    cmb_wtdsummary_destroy(dwp);

    cmi_test_print_line("=");
}

static inline bool is_sorted(const struct cmb_dataset *dsp)
{
    cmb_assert_always(dsp != NULL);

    const uint64_t un = dsp->count;
    const double *arr = dsp->xa;
    if (arr == NULL) {
        cmb_assert_always(un == 0u);
        return true;
    }

    if (un < 2u) {
        return true;
    }

    for (uint64_t ui = 0u; ui < un - 1u; ui++) {
        if (arr[ui] > arr[ui + 1u]) {
            return false;
        }
    }

    /* No evidence to the contrary */
    return true;
}

void test_dataset(const uint64_t nsamples)
{
    printf("\nTesting datasets\n");
    printf("Local variable dataset on stack: cmb_dataset_initialize\n");

    struct cmb_dataset ds = { 0 };
    cmb_dataset_initialize(&ds);
    cmb_assert_always(cmb_dataset_count(&ds) == 0);
    cmb_assert_always(cmb_dataset_min(&ds) == DBL_MAX);
    cmb_assert_always(cmb_dataset_max(&ds) == -DBL_MAX);

    printf("Drawing %u U(0,1) samples: cmb_dataset_add\n", SORT_SAMPLES);
    for (uint32_t ui = 0; ui < SORT_SAMPLES; ui++) {
        const double x = cmb_random();
        cmb_assert_always((x >= 0.0) && (x <= 1.0));
        cmb_assert_always(cmb_dataset_count(&ds) == ui);
        const uint64_t un = cmb_dataset_add(&ds, x);
        cmb_assert_always(un == cmb_dataset_count(&ds));
        cmb_assert_always(un == ui + 1);
        cmb_assert_always(cmb_dataset_max(&ds) >= x);
        cmb_assert_always(cmb_dataset_min(&ds) <= x);
    }

    printf("Content of dataset: cmb_dataset_print:\n");
    cmb_dataset_print(&ds, stdout);
    printf("\nMaking a copy: cmb_dataset_copy ... ");
    struct cmb_dataset dsc = { 0 };
    cmb_assert_always(cmb_dataset_count(&dsc) == 0);
    uint64_t un = cmb_dataset_copy(&dsc, &ds);
    cmb_assert_always(cmb_dataset_count(&dsc) == cmb_dataset_count(&ds));
    printf("Returned %" PRIu64 "\n", un);
    printf("\nContent of copy: cmb_dataset_print:\n");
    cmb_dataset_print(&dsc, stdout);
    printf("\nSorting the copy: cmb_dataset_sort ...\n");
    cmb_dataset_sort(&dsc);
    cmb_assert_always(is_sorted(&dsc));
    printf("Content of copy: cmb_dataset_print:\n");
    cmb_dataset_print(&dsc, stdout);
    printf("\nClearing the copy: cmb_dataset_reset\n");
    cmb_dataset_reset(&dsc);
    cmb_assert_always(cmb_dataset_count(&dsc) == 0);

    printf("\nBasic dataset reporting functions:\n");
    cmi_test_print_line("-");
    printf("cmb_dataset_count:\t%" PRIu64 "\n", cmb_dataset_count(&ds));
    printf("cmb_dataset_min:\t%#8.4g\n", cmb_dataset_min(&ds));
    printf("cmb_dataset_max:\t%#8.4g\n", cmb_dataset_max(&ds));
    printf("cmb_dataset_median:\t%#8.4g\n", cmb_dataset_median(&ds));
    cmi_test_print_line("-");

    printf("Five number summary of dataset: cmb_dataset_fivenum_print ...\n");
    cmb_dataset_fivenum_print(&ds, stdout, true);

    printf("\nClearing the dataset; cmb_dataset_reset\n");
    cmb_dataset_reset(&ds);
    cmb_assert_always(cmb_dataset_count(&ds) == 0);

    printf("\nDrawing %" PRIu64 " U(0,1) samples: cmb_dataset_add\n", nsamples);
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_assert_always((x >= 0.0) && (x <= 1.0));
        cmb_assert_always(cmb_dataset_count(&ds) == ui);
        un = cmb_dataset_add(&ds, x);
        cmb_assert_always(un == cmb_dataset_count(&ds));
        cmb_assert_always(un == ui + 1);
        cmb_assert_always(cmb_dataset_max(&ds) >= x);
        cmb_assert_always(cmb_dataset_min(&ds) <= x);
    }

    struct cmb_datasummary dsum = { 0 };
    printf("\nSummarizing the dataset: cmb_dataset_summarize ...");
    un = cmb_dataset_summarize(&ds, &dsum);
    cmb_assert_always(un == cmb_datasummary_count(&dsum));
    cmb_assert_always(un == cmb_dataset_count(&ds));
    cmb_assert_always(cmb_datasummary_max(&dsum) == cmb_dataset_max(&ds));
    cmb_assert_always(cmb_datasummary_min(&dsum) == cmb_dataset_min(&ds));
    printf("returned %" PRIu64 "\n", un);

    printf("Summary generated from the dataset:\n");
    cmb_datasummary_print(&dsum, stdout, true);
    printf("\nUnweighted histogram: cmb_dataset_histogram_print\n");
    cmb_dataset_histogram_print(&ds, stdout, 20u, 0.0, 0.0);

    printf("\nAutocorrelation coefficients: cmb_dataset_ACF\n");
    double acf[MAX_LAG + 1];
    cmb_dataset_ACF(&ds, MAX_LAG, acf);
    printf("\nACF correlogram: cmb_dataset_correlogram_print\n");
    cmb_dataset_correlogram_print(&ds, stdout, MAX_LAG, acf);

    printf("\nPartial autocorrelation coefficients:cmb_dataset_PACF\n");
    double pacf[MAX_LAG + 1];
    cmb_dataset_PACF(&ds, MAX_LAG, pacf, acf);
    printf("\nPACF correlogram: cmb_dataset_correlogram_print\n");
    cmb_dataset_correlogram_print(&ds, stdout, MAX_LAG, pacf);
    cmi_test_print_line("-");

    printf("\nCreating a new dataset on the heap: cmb_dataset_create\n");
    struct cmb_dataset *dsp = cmb_dataset_create();
    cmb_assert_always(dsp != NULL);
    cmb_dataset_initialize(dsp);
    cmb_assert_always(cmb_dataset_count(dsp) == 0);
    printf("Filling it with noisy sine curves ...\n");
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double period = 10.0;
        const double amp_signal = 2.0;
        const double amp_noise = 0.5;
        const double x = amp_signal * sin(2.0 * M_PI * (double)ui / period)
                       + cmb_random_normal(0.0, amp_noise);
        cmb_assert_always(cmb_dataset_count(dsp) == ui);
        un = cmb_dataset_add(dsp, x);
        cmb_assert_always(un == cmb_dataset_count(dsp));
        cmb_assert_always(un == ui + 1);
    }

    cmb_datasummary_reset(&dsum);
    cmb_assert_always(cmb_datasummary_count(&dsum) == 0);
    un = cmb_dataset_summarize(dsp, &dsum);
    cmb_assert_always(un == cmb_datasummary_count(&dsum));
    cmb_assert_always(un == cmb_dataset_count(dsp));
    cmb_datasummary_print(&dsum, stdout, true);
    cmb_dataset_histogram_print(dsp, stdout, 20u, 0, 0);

    printf("\nAutocorrelation coefficients:\n");
    cmb_dataset_ACF(dsp, MAX_LAG, acf);
    cmb_dataset_correlogram_print(dsp, stdout, MAX_LAG, acf);

    printf("\nPartial autocorrelation coefficients:\n");
    cmb_dataset_PACF(dsp, MAX_LAG, pacf, acf);
    cmb_dataset_correlogram_print(dsp, stdout, MAX_LAG, pacf);

    printf("\nCleaning up: cmb_datasummary_terminate, cmb_dataset_destroy\n");
    cmb_datasummary_terminate(&dsum);
    cmb_dataset_destroy(dsp);

    cmi_test_print_line("=");
}

static inline bool is_sorted_x(const struct cmb_timeseries *tsp)
{
    cmb_assert_always(tsp != NULL);

    const struct cmb_dataset *dsp = &(tsp->ds);
    const uint64_t un = dsp->count;
    const double *arr = dsp->xa;
    if (arr == NULL) {
        cmb_assert_always(un == 0u);
        return true;
    }

    if (un < 2u) {
        return true;
    }

    for (uint64_t ui = 0u; ui < un - 1u; ui++) {
        if (arr[ui] > arr[ui + 1u]) {
            return false;
        }
    }

    /* No evidence to the contrary */
    return true;
}

static inline bool is_sorted_t(const struct cmb_timeseries *tsp)
{
    cmb_assert_always(tsp != NULL);

    const struct cmb_dataset *dsp = &(tsp->ds);
    const uint64_t un = dsp->count;
    const double *arr = tsp->ta;
    if (arr == NULL) {
        cmb_assert_always(un == 0u);
        return true;
    }

    if (un < 2u) {
        return true;
    }

    for (uint64_t ui = 0u; ui < un - 1u; ui++) {
        if (arr[ui] > arr[ui + 1u]) {
            return false;
        }
    }

    /* No evidence to the contrary */
    return true;
}


void test_timeseries(const uint64_t nsamples)
{
    printf("\nTesting timeseries\n");
    printf("Creating timeseries: cmb_timeseries_create\n");

    struct cmb_timeseries *tsp = cmb_timeseries_create();
    cmb_assert_always(tsp != NULL);
    cmb_timeseries_initialize(tsp);
    cmb_assert_always(cmb_timeseries_count(tsp) == 0);
    cmb_assert_always(cmb_timeseries_min(tsp) == DBL_MAX);
    cmb_assert_always(cmb_timeseries_max(tsp) == -DBL_MAX);

    printf("Drawing %" PRIu64 " x = U(0,1) samples at intervals Exp(2 - x): cmb_timeseries_add\n", nsamples);
    double t = 0.0;
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random();
        cmb_assert_always((x >= 0.0) && (x <= 1.0));
        cmb_assert_always(cmb_timeseries_count(tsp) == ui);
        const uint64_t un = cmb_timeseries_add(tsp, x, t);
        cmb_assert_always(un == cmb_timeseries_count(tsp));
        cmb_assert_always(un == ui + 1);

        /* Make holding time until the next sample correlated with this sample value */
        const double dt = cmb_random_exponential(2.0 - x);
        cmb_assert_always(dt >= 0.0);
        t += dt;
    }

    printf("Finalizing at time %g: cmb_timeseries_finalize\n", t);
    uint64_t uo = cmb_timeseries_count(tsp);
    uint64_t un = cmb_timeseries_finalize(tsp, t);
    cmb_assert_always(un == uo + 1u);

    printf("\nBasic timeseries reporting functions:\n");
    cmi_test_print_line("-");
    printf("cmb_timeseries_count:\t%" PRIu64 "\n", cmb_timeseries_count(tsp));
    printf("cmb_timeseries_min:\t%#8.4g\n", cmb_timeseries_min(tsp));
    printf("cmb_timeseries_max:\t%#8.4g\n", cmb_timeseries_max(tsp));
    cmi_test_print_line("-");

    printf("\nSummarizing: cmb_timeseries_summarize, cmb_wtdsummary_print, cmb_timeseries_fivenum_print ...\n");
    struct cmb_wtdsummary ws = { 0 };
    un = cmb_timeseries_summarize(tsp, &ws);
    cmb_assert_always(un == cmb_wtdsummary_count(&ws));
    cmb_assert_always(un == cmb_timeseries_count(tsp) - 1u);
    cmb_wtdsummary_print(&ws, stdout, true);
    cmb_timeseries_fivenum_print(tsp, stdout, true);

    printf("\nWeighted histogram:\n");
    cmb_timeseries_histogram_print(tsp, stdout, NUM_BINS, 0.0, 0.0);
    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    printf("Unweighted histogram of same data:\n");
    cmb_dataset_histogram_print(dsp, stdout, NUM_BINS, 0.0, 0.0);
    cmi_test_print_line("=");

    printf("\nDeclaring another timeseries on the stack: cmb_timeseries_initialize\n");
    struct cmb_timeseries ts = { 0 };
    cmb_timeseries_initialize(&ts);
    cmb_assert_always(cmb_timeseries_count(&ts) == 0);
    printf("Drawing %" PRIu64 " x = U(1,2) samples at intervals Exp(1): cmb_timeseries_add\n", nsamples);
    t = 0.0;
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random_uniform(1.0, 2.0);
        cmb_assert_always((x >= 1.0) && (x <= 2.0));
        cmb_assert_always(cmb_timeseries_count(&ts) == ui);
        un = cmb_timeseries_add(&ts, x, t);
        cmb_assert_always(un == cmb_timeseries_count(&ts));
        cmb_assert_always(un == ui + 1);
        const double dt = cmb_random_std_exponential();
        cmb_assert_always(dt >= 0.0);
        t += dt;
    }

    printf("Finalizing at time %g: cmb_timeseries_finalize\n", t);
    un = cmb_timeseries_count(&ts);
    cmb_timeseries_finalize(&ts, t);
    cmb_assert_always(cmb_timeseries_count(&ts) == un + 1u);

    printf("Src: ");
    un = cmb_timeseries_summarize(&ts, &ws);
    cmb_assert_always(un == cmb_wtdsummary_count(&ws));
    cmb_assert_always(un == cmb_timeseries_count(&ts) - 1u);
    cmb_wtdsummary_print(&ws, stdout, true);
    printf("Tgt: ");
    un = cmb_timeseries_summarize(tsp, &ws);
    cmb_assert_always(un == cmb_wtdsummary_count(&ws));
    cmb_assert_always(un == cmb_timeseries_count(tsp) - 1u);
    cmb_wtdsummary_print(&ws, stdout, true);

    printf("Copying src into tgt: cmb_timeseries_copy ... ");
    un = cmb_timeseries_copy(tsp, &ts);
    cmb_assert_always(un == cmb_timeseries_count(&ts));
    cmb_assert_always(un == cmb_timeseries_count(tsp));
    printf("returned %" PRIu64 "\n", un);
    printf("Tgt: ");
    un = cmb_timeseries_summarize(tsp, &ws);
    cmb_wtdsummary_print(&ws, stdout, true);
    printf("Src: ");
    un = cmb_timeseries_summarize(&ts, &ws);
    cmb_assert_always(un == cmb_wtdsummary_count(&ws));
    cmb_assert_always(un == cmb_timeseries_count(&ts) - 1u);
    cmb_wtdsummary_print(&ws, stdout, true);

    printf("\nCleaning up: cmb_timeseries_reset, cmb_timeseries_destroy\n");
    cmb_timeseries_reset(&ts);
    cmb_assert_always(cmb_timeseries_count(&ts) == 0);
    cmb_timeseries_destroy(tsp);
    cmi_test_print_line("-");

    printf("\nTesting sorting functions\n");
    cmb_timeseries_initialize(&ts);
    cmb_assert_always(cmb_timeseries_count(&ts) == 0);
    printf("Drawing %u x = U(1,2) samples at intervals Exp(1): cmb_timeseries_add\n", SORT_SAMPLES);
    t = 0.0;
    for (uint32_t ui = 0; ui < SORT_SAMPLES; ui++) {
        const double x = cmb_random_uniform(1.0, 2.0);
        cmb_assert_always((x >= 1.0) && (x <= 2.0));
        cmb_assert_always(cmb_timeseries_count(&ts) == ui);
        un = cmb_timeseries_add(&ts, x, t);
        cmb_assert_always(un == cmb_timeseries_count(&ts));
        cmb_assert_always(un == ui + 1u);
        const double dt = cmb_random_std_exponential();
        cmb_assert_always(dt >= 0.0);
        t += dt;
    }

    printf("Finalizing at time %g: cmb_timeseries_finalize\n", t);
    uo = cmb_timeseries_count(&ts);
    un = cmb_timeseries_finalize(&ts, t);
    cmb_assert_always(un == uo + 1u);
    printf("Content of timeseries: cmb_timeseries_print\n");
    cmi_test_print_line(".");
    cmb_timeseries_print(&ts, stdout);
    cmi_test_print_line(".");

    printf("\nSorting: cmb_timeseries_sort_x\n");
    cmb_assert_always(is_sorted_t(&ts));
    cmb_timeseries_sort_x(&ts);
    cmb_assert_always(is_sorted_x(&ts));
    printf("Content of timeseries: cmb_timeseries_print\n");
    cmi_test_print_line(".");
    cmb_timeseries_print(&ts, stdout);
    cmi_test_print_line(".");
    printf("\nUnsorting: cmb_timeseries_sort_t\n");
    cmb_assert_always(is_sorted_x(&ts));
    cmb_timeseries_sort_t(&ts);
    cmb_assert_always(is_sorted_t(&ts));
    printf("Content of timeseries: cmb_timeseries_print\n");
    cmi_test_print_line(".");
    cmb_timeseries_print(&ts, stdout);
    cmi_test_print_line(".");

    printf("\ncmb_dataset_median:\t%#8.4g\n", cmb_timeseries_median(&ts));
    printf("cmb_timeseries_fivenum_print:\n");
    cmi_test_print_line("-");
    cmb_timeseries_fivenum_print(&ts, stdout, true);
    cmi_test_print_line("-");

    printf("\nCleaning up: cmb_timeseries_terminate\n");
    cmb_timeseries_terminate(&ts);

    cmi_test_print_line("=");
}

int main(const int argc, char *argv[])
{
    uint64_t nsamples = MAX_SAMPLES;
    uint64_t seed = cmb_random_hwseed();
    bool timing_enabled = false;

    int opt;
    while ((opt = getopt(argc, argv, "s:t")) != -1) {
        switch (opt) {
            case 'n':
                errno = 0;
                nsamples = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 't':
                timing_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    struct timespec start_time;
    if (timing_enabled) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }

    cmi_test_print_line("*");
    printf("**********************      Testing data collectors       **********************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);
    cmb_random_initialize(seed);

    test_summary(nsamples);
    test_wsummary(nsamples);
    test_dataset(nsamples);
    test_timeseries(nsamples);

    cmb_random_terminate();
    cmi_test_print_line("*");

    if (timing_enabled) {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed = (double)(end_time.tv_sec - start_time.tv_sec);
        elapsed += (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
        printf("It took %g sec\n", elapsed);
    }

    return 0;
}