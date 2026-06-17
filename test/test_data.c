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
#define MAX_SAMPLES 10000u
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

/* Relative-or-absolute float comparison for statistic checks. */
static bool stat_close(const double got, const double expected)
{
    const double diff = fabs(got - expected);
    if (diff <= 1e-9) {
        return true;                       /* absolute floor, for ~0 values */
    }
    const double scale = fmax(fabs(got), fabs(expected));
    return diff <= 1e-9 * scale;           /* 1e-9 relative otherwise */
}

/*
 * Independent two-pass reference for the population-weighted moments, written
 * straight from the definitions
 *     mu   = (sum w_i x_i) / W,           W = sum w_i
 *     mu_k = (sum w_i (x_i - mu)^k) / W
 *     variance = mu_2,  skewness = mu_3 / mu_2^{3/2},
 *     excess kurtosis = mu_4 / mu_2^2 - 3
 * It deliberately does NOT reuse the library's streaming algebra, so it is a
 * genuine oracle for both the accumulation and the normalization.
 */
static void wtd_reference(const double *x, const double *w, const uint64_t n,
                          double *mean, double *var, double *skew, double *exkurt)
{
    double bigw = 0.0;
    double sx = 0.0;
    for (uint64_t i = 0; i < n; i++) { bigw += w[i]; sx += w[i] * x[i]; }
    const double mu = sx / bigw;

    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    for (uint64_t i = 0; i < n; i++) {
        const double d = x[i] - mu;
        const double d2 = d * d;
        m2 += w[i] * d2;
        m3 += w[i] * d2 * d;
        m4 += w[i] * d2 * d2;
    }
    const double mu2 = m2 / bigw;
    const double mu3 = m3 / bigw;
    const double mu4 = m4 / bigw;

    *mean   = mu;
    *var    = mu2;
    *skew   = (mu2 > 0.0) ? mu3 / pow(mu2, 1.5)     : 0.0;
    *exkurt = (mu2 > 0.0) ? mu4 / (mu2 * mu2) - 3.0 : 0.0;
}

/* Assert a summary's four moments match a reference tuple. */
static void check_against(const struct cmb_wtdsummary *s,
                          const double mean, const double var,
                          const double skew, const double exkurt)
{
    cmb_assert_always(stat_close(cmb_wtdsummary_mean(s),     mean));
    cmb_assert_always(stat_close(cmb_wtdsummary_variance(s), var));
    cmb_assert_always(stat_close(cmb_wtdsummary_stddev(s),   sqrt(var)));
    cmb_assert_always(stat_close(cmb_wtdsummary_skewness(s), skew));
    cmb_assert_always(stat_close(cmb_wtdsummary_kurtosis(s), exkurt));
}

static void test_wsummary(const uint64_t nsamples)
{
    printf("\nTesting weighted data summaries\n");

    /* ---- 1. Empty summary: every statistic is the neutral value ---------- */
    struct cmb_wtdsummary dws;
    cmb_wtdsummary_initialize(&dws);
    cmb_assert_always(cmb_wtdsummary_count(&dws)    == 0);
    cmb_assert_always(cmb_wtdsummary_max(&dws)      == -DBL_MAX);
    cmb_assert_always(cmb_wtdsummary_min(&dws)      == DBL_MAX);
    cmb_assert_always(cmb_wtdsummary_mean(&dws)     == 0.0);
    cmb_assert_always(cmb_wtdsummary_variance(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_stddev(&dws)   == 0.0);
    cmb_assert_always(cmb_wtdsummary_skewness(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_kurtosis(&dws) == 0.0);

    /* ---- 2. Single sample: mean = x, all higher moments zero ------------- */
    cmb_wtdsummary_add(&dws, 4.2, 7.0);
    cmb_assert_always(cmb_wtdsummary_count(&dws) == 1);
    cmb_assert_always(stat_close(cmb_wtdsummary_mean(&dws), 4.2));
    cmb_assert_always(cmb_wtdsummary_variance(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_skewness(&dws) == 0.0);
    cmb_assert_always(cmb_wtdsummary_kurtosis(&dws) == 0.0);
    cmb_wtdsummary_reset(&dws);

    /* ---- 3. Fixed dataset with UNEQUAL weights, pinned to constants ------ */
    /* Reference computed independently (NumPy, 17 digits). The old count-based
       code gives variance 9.916..., grossly different from the correct
       7.933..., so this block alone is enough to catch the regression. */
    printf("Fixed unequal-weight dataset, checked against exact constants\n");
    static const double fx[] = { 2.0, 5.0, 5.0, 9.0, 1.0, 7.0, 3.0 };
    static const double fw[] = { 0.7, 1.3, 0.4, 2.1, 0.9, 1.6, 0.5 };
    const uint64_t fn = sizeof fx / sizeof fx[0];

    const double EXP_MEAN =  5.6533333333333342;
    const double EXP_VAR  =  7.9331555555555555;
    const double EXP_SKEW = -0.31034707315744514;
    const double EXP_KURT = -1.2193888806914575;

    struct cmb_wtdsummary *a = cmb_wtdsummary_create();
    for (uint64_t i = 0; i < fn; i++) { cmb_wtdsummary_add(a, fx[i], fw[i]); }
    cmb_assert_always(stat_close(a->wsum, 7.5));
    check_against(a, EXP_MEAN, EXP_VAR, EXP_SKEW, EXP_KURT);

    /* the two-pass reference helper must agree with those constants too */
    double rm, rv, rs, rk;
    wtd_reference(fx, fw, fn, &rm, &rv, &rs, &rk);
    cmb_assert_always(stat_close(rm, EXP_MEAN) && stat_close(rv, EXP_VAR)
                   && stat_close(rs, EXP_SKEW) && stat_close(rk, EXP_KURT));

    /* ---- 4. Segmentation invariance: split every weight into 3 unequal
              pieces. Same weighted distribution => identical moments. This is
              exactly the failure mode of the count-based bug. -------------- */
    printf("Segmentation invariance (weights split 3 ways)\n");
    struct cmb_wtdsummary *b = cmb_wtdsummary_create();
    for (uint64_t i = 0; i < fn; i++) {
        cmb_wtdsummary_add(b, fx[i], fw[i] * 0.2);
        cmb_wtdsummary_add(b, fx[i], fw[i] * 0.3);
        cmb_wtdsummary_add(b, fx[i], fw[i] * 0.5);
    }
    cmb_assert_always(cmb_wtdsummary_count(b) == 3 * fn);   /* more segments */
    cmb_assert_always(stat_close(b->wsum, a->wsum));        /* same total weight */
    check_against(b, EXP_MEAN, EXP_VAR, EXP_SKEW, EXP_KURT);

    /* ---- 5. Weight-scale invariance: multiply all weights by 1000 -------- */
    printf("Weight-scale invariance (all weights x1000)\n");
    struct cmb_wtdsummary *c = cmb_wtdsummary_create();
    for (uint64_t i = 0; i < fn; i++) { cmb_wtdsummary_add(c, fx[i], fw[i] * 1000.0); }
    check_against(c, EXP_MEAN, EXP_VAR, EXP_SKEW, EXP_KURT);

    /* ---- 6. Zero-weight samples are ignored entirely --------------------- */
    printf("Zero-weight samples are ignored\n");
    const uint64_t before  = cmb_wtdsummary_count(c);
    const double   wbefore = c->wsum;
    cmb_wtdsummary_add(c, 999.0, 0.0);
    cmb_assert_always(cmb_wtdsummary_count(c) == before);
    cmb_assert_always(c->wsum == wbefore);
    check_against(c, EXP_MEAN, EXP_VAR, EXP_SKEW, EXP_KURT);

    /* ---- 7. Merge matches a single summary of the combined data ---------- */
    printf("Merge of two partial summaries matches the whole\n");
    struct cmb_wtdsummary *p1 = cmb_wtdsummary_create();
    struct cmb_wtdsummary *p2 = cmb_wtdsummary_create();
    for (uint64_t i = 0; i < 3;  i++) { cmb_wtdsummary_add(p1, fx[i], fw[i]); }
    for (uint64_t i = 3; i < fn; i++) { cmb_wtdsummary_add(p2, fx[i], fw[i]); }
    struct cmb_wtdsummary *m = cmb_wtdsummary_create();
    cmb_wtdsummary_merge(m, p1, p2);
    cmb_assert_always(cmb_wtdsummary_count(m) == fn);
    cmb_assert_always(stat_close(m->wsum, a->wsum));
    check_against(m, EXP_MEAN, EXP_VAR, EXP_SKEW, EXP_KURT);
    /* merge target aliasing one source must also work */
    struct cmb_wtdsummary *q1 = cmb_wtdsummary_create();
    struct cmb_wtdsummary *q2 = cmb_wtdsummary_create();
    for (uint64_t i = 0; i < 3;  i++) { cmb_wtdsummary_add(q1, fx[i], fw[i]); }
    for (uint64_t i = 3; i < fn; i++) { cmb_wtdsummary_add(q2, fx[i], fw[i]); }
    cmb_wtdsummary_merge(q1, q1, q2);
    check_against(q1, EXP_MEAN, EXP_VAR, EXP_SKEW, EXP_KURT);

    /* ---- 8. Randomized property test against the two-pass reference ------ */
    /* Stores the raw (x,w) so the oracle sees identical data. With weights on
       [0.25,4] the total weight differs from the count by ~2x, so a count-vs-
       weight normalization error fails grossly here as well. -------------- */
    printf("Randomized agreement with two-pass reference, %" PRIu64 " samples\n", nsamples);
    if (nsamples >= 4u && nsamples <= 2000000u) {
        double *xs = malloc(nsamples * sizeof *xs);
        double *ws = malloc(nsamples * sizeof *ws);
        cmb_assert_always(xs != NULL && ws != NULL);
        struct cmb_wtdsummary *r = cmb_wtdsummary_create();
        for (uint64_t i = 0; i < nsamples; i++) {
            xs[i] = cmb_random_normal(10.0, 3.0);
            ws[i] = cmb_random_uniform(0.25, 4.0);
            cmb_wtdsummary_add(r, xs[i], ws[i]);
        }
        double em, ev, es, ek;
        wtd_reference(xs, ws, nsamples, &em, &ev, &es, &ek);
        cmb_assert_always(fabs(cmb_wtdsummary_mean(r)     - em) <= 1e-7 * fmax(1.0, fabs(em)));
        cmb_assert_always(fabs(cmb_wtdsummary_variance(r) - ev) <= 1e-7 * fmax(1.0, fabs(ev)));
        cmb_assert_always(fabs(cmb_wtdsummary_skewness(r) - es) <= 1e-5 * fmax(1.0, fabs(es)));
        cmb_assert_always(fabs(cmb_wtdsummary_kurtosis(r) - ek) <= 1e-5 * fmax(1.0, fabs(ek)));
        free(xs); free(ws);
        cmb_wtdsummary_destroy(r);
    }

    /* ---- 9. Relationship to the unweighted summary (documents the
              population-vs-sample distinction). With equal weights the
              weighted POPULATION variance equals the unweighted SAMPLE
              variance scaled by (n-1)/n. ----------------------------------- */
    printf("Equal-weight case: weighted(pop) vs unweighted(sample) relationship\n");
    {
        struct cmb_datasummary ds;
        struct cmb_wtdsummary  ws;
        cmb_datasummary_initialize(&ds);
        cmb_wtdsummary_initialize(&ws);
        const uint64_t k = 500u;
        for (uint64_t i = 0; i < k; i++) {
            const double x = cmb_random();
            cmb_datasummary_add(&ds, x);
            cmb_wtdsummary_add(&ws, x, 1.0);
        }
        const double dn = (double)k;
        cmb_assert_always(stat_close(cmb_wtdsummary_mean(&ws),
                                     cmb_datasummary_mean(&ds)));
        cmb_assert_always(stat_close(cmb_wtdsummary_variance(&ws),
                                     cmb_datasummary_variance(&ds) * (dn - 1.0) / dn));
        cmb_datasummary_terminate(&ds);
        cmb_wtdsummary_terminate(&ws);
    }

    /* ---- 10. Smoke-test that the printed report uses the weighted figures - */
    printf("\nReport (Variance must be ~%.4g, not the count-based ~9.916):\n", EXP_VAR);
    cmb_wtdsummary_print(a, stdout, true);

    cmb_wtdsummary_destroy(a);
    cmb_wtdsummary_destroy(b);
    cmb_wtdsummary_destroy(c);
    cmb_wtdsummary_destroy(p1);
    cmb_wtdsummary_destroy(p2);
    cmb_wtdsummary_destroy(m);
    cmb_wtdsummary_destroy(q1);
    cmb_wtdsummary_destroy(q2);

    printf("\nAll weighted-summary assertions passed.\n");
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