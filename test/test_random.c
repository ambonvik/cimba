/*
 * Test script for random number distributions.
 *
 * Plot a histogram and compare sample average and standard deviation to
 * expected values for each distribution provided.
 *
 * Some alternative implementations of certain distributions in this
 * file for performance comparison purposes, e.g. Box Muller normal.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-26.
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
#include <time.h>
#include <unistd.h>

#include "cmb_dataset.h"
#include "cmb_random.h"

#include "testutils.h"

/* Test macros */
#define MOMENTS 15
#define ACFS 15

#define QTEST_PREPARE() \
    struct cmb_dataset ds = { 0 }; \
    cmb_dataset_initialize(&ds)

#define QTEST_EXECUTE(DUT, ASRT) \
    printf("Drawing %" PRIu64 " samples...\n", nsamples); \
    for (uint32_t ui = 0; ui < nsamples; ui++) { \
        const double x = (DUT); \
        cmb_assert_always((ASRT)); \
        cmb_dataset_add(&ds, x); \
    }

#define QTEST_REPORT() \
    struct cmb_datasummary dsu = { 0 }; \
    cmb_datasummary_initialize(&dsu); \
    cmb_dataset_summarize(&ds, &dsu); \
    cmb_datasummary_print(&dsu, stdout, false); \
    cmb_dataset_histogram_print(&ds, stdout, 20, 0.0, 0.0); \
    cmb_datasummary_terminate(&dsu)

#define QTEST_GOF_CONT(cdf, ctx) \
    struct cmi_test_outcome result = { 0 }; \
    cmi_test_gof_cont(cdf, ctx, &ds, &result); \
    cmi_test_outcome_print(&result, stdout); \
    cmb_assert_always((result.status == CMI_TEST_OK) \
                  && (fabs(result.combined_sigma) < 6.0))

#define QTEST_REPORT_ACFS() \
    printf("\nAutocorrelation factors (expected 0.0):\n"); \
    double acf[ACFS + 1] = { 0.0 }; \
    cmb_dataset_ACF(&ds, ACFS, acf); \
    cmb_dataset_correlogram_print(&ds, stdout, ACFS, acf); \
    printf("\nPartial autocorrelation factors (expected 0.0):\n"); \
    double pacf[ACFS + 1] = { 0.0 }; \
    cmb_dataset_PACF(&ds, ACFS, pacf, acf); \
    cmb_dataset_correlogram_print(&ds, stdout, ACFS, pacf)

#define QTEST_FINISH() \
    cmb_dataset_terminate(&ds)

static void print_single(const bool has_val, const double val)
{
    if (has_val) {
        printf("\t%#8.4g", val);
    }
    else {
        printf("\t   ---  ");
    }
}

static void print_expected(const uint64_t n,
                           const bool has_mean, const double mean,
                           const bool has_var, const double var,
                           const bool has_skew, const double skew,
                           const bool has_kurt, const double kurt)
{
    printf("Expected vs actual:\n");
    printf("Count   \tMean    \tStdDev  \tVariance\tSkewness\tExcess kurtosis\n");
    if (n < 100000u) {
        printf("%8" PRIu64, n);
    }
    else {
        printf("%#8.4g", (double)n);
    }

    print_single(has_mean, mean);
    print_single(has_var, sqrt(var));
    print_single(has_var, var);
    print_single(has_skew, skew);
    print_single(has_kurt, kurt);
    printf("\n");
}

/**** Start of test scripts ****/

static double cdf_u01(const double x, void *ctx)
{
    cmb_unused(ctx);

    double r;
    if (x <= 0.0) {
        r = 0;
    }
    if (x >= 1.0) {
        r = 1.0;
    }
    else {
        r = x;
    }

    return r;
}

static void test_quality_random(const uint64_t nsamples)
{
    printf("\nQuality testing basic random number generator cmb_random(), uniform on [0,1)\n");

    /* Verify the range stated above, using same formula as in cmb_random() */
    const uint64_t all_zeros = UINT64_C(0x0000000000000000);
    const double rmin = ldexp((double)(all_zeros >> 11), -53);
    cmb_assert_always(rmin == 0.0);
    const uint64_t all_ones = UINT64_C(0xFFFFFFFFFFFFFFFF);
    const double rmax = ldexp((double)(all_ones >> 11), -53);
    cmb_assert_always((rmax < 1.0) && (1.0 - rmax) > 0.0);

    QTEST_PREPARE();

    /* Handle test execution outside macro to capture moments as well */
    printf("Drawing %" PRIu64 " samples...\n", nsamples);
    double moment_r[MOMENTS] = { 0.0 };
    for (uint64_t ui = 0; ui < nsamples; ui++) {
        const double xi = cmb_random();
        cmb_assert_always(((xi >= 0.0) && (xi <= 1.0)));
        cmb_dataset_add(&ds, xi);

        double xij = xi;
        for (int j = 0; j < MOMENTS; j++) {
            moment_r[j] += xij;
            xij *= xi;
        }
    }

    print_expected(nsamples, true, 0.5, true, 1.0 / 12.0, true, 0.0, true, -6.0 / 5.0);

    QTEST_REPORT();
    QTEST_REPORT_ACFS();

    /* Report moments */
    printf("\nRaw moment:   Expected:   Actual:   Error:\n");
    cmi_test_print_line("-");
    for (uint16_t ui = 0; ui < MOMENTS; ui++) {
        const double expmom = 1.0 / (double)(ui + 2u);
        const double avgmom = moment_r[ui] / (double)nsamples;
        printf("%5d        %8.5g    %8.5g   %6.3f %%\n", ui + 1u,
               expmom, avgmom, 100.0 * (avgmom - expmom) / expmom);
    }
    cmi_test_print_line("-");

    /* Run goodness-of-fit test battery, no CDF transform needed */
    printf("Testing Goodness of Fit vs the U(0,1) distribution\n");
    struct cmi_test_outcome result = { 0 };
    cmi_test_gof_cont(cdf_u01, NULL, &ds, &result);
    cmi_test_outcome_print(&result, stdout);
    cmb_assert_always((result.status == CMI_TEST_OK)
                      && (fabs(result.combined_sigma) < 6.0));

    QTEST_FINISH();
}

struct cdf_uniform_params {
    double a;
    double b;
};

static double cdf_uniform(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_uniform_params *pp = ctx;
    cmb_assert_debug(pp->a <= pp->b);

    double r;
    if (x <= pp->a) {
        r = 0.0;
    }
    else if (x < pp->b) {
        r = (x - pp->a) / (pp->b - pp->a);
    }
    else {
        r = 1.0;
    }

    return r;
}

static void test_quality_uniform(const uint64_t nsamples, const double a, const double b)
{
    printf("\nQuality testing cmb_random_uniform(%g,%g)\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_uniform(a, b), (x >= a) && (x <= b));

    const double var = (b - a) * (b - a) / 12;

    print_expected(nsamples, true, 0.5 * (a + b), true, var, true, 0.0, true, -6.0 / 5.0);

    QTEST_REPORT();

    struct cdf_uniform_params cdfpar = { .a = a, .b = b };
    QTEST_GOF_CONT(cdf_uniform, &cdfpar);
    QTEST_FINISH();
}

static double cdf_std_exp(const double x, void *ctx)
{
    cmb_unused(ctx);

    const double r = 1.0 - exp(-x);

    return r;
}

static void test_quality_std_exponential(const uint64_t nsamples)
{
    printf("\nQuality testing standard exponential distribution, mean = 1\n");
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_std_exponential(), x >= 0.0);

    print_expected(nsamples, true, 1.0, true, 1.0, true, 2.0, true, 6.0);

    QTEST_REPORT();
    QTEST_REPORT_ACFS();
    QTEST_GOF_CONT(cdf_std_exp, NULL);
    QTEST_FINISH();
}

/* Exponential, inverse transform method for comparison */
static double exponential_inv(const double m)
{
    cmb_assert_release(m > 0.0);

    return -log(1.0 - cmb_random()) * m;
}

static void test_speed_exponential(const uint64_t nsamples, const double m)
{
    printf("\nSpeed testing standard exponential distribution\n");
    printf("\nInversion method, drawing %" PRIu64 " samples...", nsamples);

    const clock_t csi = clock();
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        (void)exponential_inv(m);
    }
    const clock_t cei = clock();
    const double ti = (double)(cei - csi) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", (double)nsamples / ti);

    printf("Ziggurat method, drawing %" PRIu64 " samples...", nsamples);
    const clock_t csz = clock();
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        (void)cmb_random_exponential(m);
    }
    const clock_t cez = clock();
    const double tz = (double)(cez - csz) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", (double)nsamples / tz);

    printf("\nSpeedup for ziggurat vs inversion method %.1fx, %4.1f %% less time per sample.\n",
           ti / tz, 100.0 * (ti - tz) / ti);

    cmi_test_print_line("=");
}

struct cdf_exp_params {
    double m;
};

static double cdf_exp(const double x, void *ctx)
{
    cmb_unused(ctx);
    cmb_assert_debug(ctx != NULL);
    const struct cdf_exp_params *pp = ctx;

    const double r = 1.0 - exp(-x / pp->m);

    return r;
}

static void test_quality_exponential(const uint64_t nsamples, const double m)
{
    printf("\nQuality testing exponential distribution, mean = %f\n", m);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_exponential(m), x >= 0.0);

    print_expected(nsamples, true, m, true, m * m, true, 2.0, true, 6.0);

    QTEST_REPORT();

    struct cdf_exp_params pp = { .m = m };
    QTEST_GOF_CONT(cdf_exp, &pp);
    QTEST_FINISH();
}

static void test_tail_std_exponential(const uint64_t nsamples)
{
    const double t[] = {0.0005, 0.001, 0.005, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.1 };
    const unsigned n_t = sizeof t / sizeof t[0];

    struct cmb_dataset ds_z = { 0 };
    struct cmb_dataset ds_i = { 0 };
    cmb_dataset_initialize(&ds_z);
    cmb_dataset_initialize(&ds_i);

    for (uint64_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random_std_exponential();
        cmb_dataset_add(&ds_z, x);
        const double y = exponential_inv(1.0);
        cmb_dataset_add(&ds_i, y);
    }

    printf("\nBinomial tail test, standard exponential, n = %" PRIu64 "\n", nsamples);
    printf("t        expected      obs (zig)      deficit      sigma      obs (inv)      deficit      sigma\n");

    double sigma_max = 0.0;
    for (unsigned ut = 0; ut < n_t; ut++) {
        uint64_t cnt_z = 0u;
        uint64_t cnt_i = 0u;
        for (uint64_t ui = 0; ui < ds_z.count; ui++) {
            const double x = ds_z.xa[ui];
            if (x <= t[ut]) {
                cnt_z++;
            }

            const double y = ds_i.xa[ui];
            if (y <= t[ut]) {
                cnt_i++;
            }
        }

        const double p  = 1.0 - exp(-t[ut]);
        const double e  = (double)nsamples * p;
        const double sd = sqrt((double)nsamples * p * (1.0 - p));
        const double zz  = ((double)cnt_z - e) / sd;
        const double zi  = ((double)cnt_i - e) / sd;

        printf("%-8.3f %12.1f %12" PRIu64 " %10.4f%% %10.2f %12" PRIu64 " %10.4f%% %10.2f\n",
               t[ut], e,
               cnt_z, 100.0 * (e - (double)cnt_z) / e, zz,
               cnt_i, 100.0 * (e - (double)cnt_i) / e, zi);

        const double sig_abs = fabs(zz);
        if (sig_abs > sigma_max) {
            sigma_max = sig_abs;
        }
    }

    cmb_dataset_terminate(&ds_i);
    cmb_dataset_terminate(&ds_z);

    cmb_assert_always(sigma_max < 5.0);
}


/* Normal distribution using Box-Muller approach for comparison purposes */
static double normal_bm(const double m, const double s)
{
    static double zu, zv;
    static bool even = false;

    double z;
    if (even) {
        z = zu * cos(zv);
    }
    else {
        zu = sqrt(-2.0 * log(cmb_random()));
        zv = 2.0 * M_PI * cmb_random();
        z = zu * sin(zv);
    }

    even = !even;
    return s * z + m;
}

/*
 * Recursive function to calculate raw moments of normal distribution,
 * recursion is OK here since it will not be called from the coroutine context.
 */
static double normal_raw_moment(const uint16_t n, const double mu, const double sigma)
{
    if (n == 0) {
        return 1.0;
    }
    else if (n == 1) {
        return mu;
    }
    else {
        /* divide & conquer */
        return mu * normal_raw_moment(n - 1, mu, sigma)
            + (n - 1) * sigma * sigma * normal_raw_moment(n - 2, mu, sigma);
    }
}

static double cdf_std_normal(const double x, void *ctx)
{
    cmb_unused(ctx);

    const double r = 0.5 * (1.0 - erf(x / sqrt(2.0)));

    return r;
}

static void test_quality_std_normal(const uint64_t nsamples)
{
    printf("\nQuality testing standard normal distribution, mean = 0, sigma = 1\n");
    QTEST_PREPARE();

    double moment_r[MOMENTS] = { 0.0 };
    double moment_bm[MOMENTS] = { 0.0 };
    printf("Drawing %" PRIu64 " samples...\n", nsamples);
    for (uint64_t ui = 0; ui < nsamples; ui++) {
        const double xi = cmb_random_std_normal();
        cmb_dataset_add(&ds, xi);

        double xij = xi;
        for (uint16_t j = 0; j < MOMENTS; j++) {
            moment_r[j] += xij;
            xij *= xi;
        }

        const double xbmi = normal_bm(0.0, 1.0);
        double xbmij = xbmi;
        for (uint16_t j = 0; j < MOMENTS; j++) {
            moment_bm[j] += xbmij;
            xbmij *= xbmi;
        }
    }

    print_expected(nsamples, true, 0.0, true, 1.0, true, 0.0, true, 0.0);

    QTEST_REPORT();
    QTEST_REPORT_ACFS();

    printf("\n                              Cimba ziggurat method:    Box Muller method:\n");
    printf("Raw moment:     Expected:     Actual:     Error:        Actual:     Error:\n");
    cmi_test_print_line("-");
    for (uint16_t ui = 0; ui < MOMENTS; ui++) {
        const double expmom = normal_raw_moment(ui + 1u, 0.0, 1.0);
        const double avgmom = moment_r[ui] / (double)nsamples;
        const double bmmom = moment_bm[ui] / (double)nsamples;
        printf("%5d        %10.4g    %10.4g", ui + 1u,
               expmom, avgmom);
        if (expmom != 0.0) {
            printf("   %6.3f %%", 100.0 * (avgmom - expmom) / expmom);
        }
        else {
            printf("      ---  ");
        }
        printf("     %10.4g", bmmom);
        if (expmom != 0.0) {
            printf("   %6.3f %%\n", 100.0 * (bmmom - expmom) / expmom);
        }
        else {
            printf("      ---\n");
        }

    }

    QTEST_GOF_CONT(cdf_std_normal, NULL);
    QTEST_FINISH();
}

struct cdf_normal_params {
    double m;
    double s;
};

static double cdf_normal(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_normal_params *pp = ctx;

    const double r = 0.5 * (1.0 - erf((x - pp->m) / (pp->s * sqrt(2.0))));

    return r;
}

static void test_quality_normal(const uint64_t nsamples, const double m, const double s)
{
    printf("\nQuality testing normal distribution, mean = %f, sigma = %f\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_normal(m, s), true);

    print_expected(nsamples, true, m, true, s * s, true, 0.0, true, 0.0);

    QTEST_REPORT();
    struct cdf_normal_params pp = { .m = m, .s = s };
    QTEST_GOF_CONT(cdf_normal, &pp);
    QTEST_FINISH();
}

static void test_speed_normal(const uint64_t nsamples, const double m, const double s)
{
    printf("\nSpeed testing normal distribution\n");
    printf("\nBox Muller method, drawing %" PRIu64 " samples...", nsamples);

    const clock_t csi = clock();
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        (void)normal_bm(m, s);
    }
    const clock_t cei = clock();
    const double ti = (double)(cei - csi) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", (double)nsamples / ti);

    printf("Ziggurat method, drawing %" PRIu64 " samples...", nsamples);
    const clock_t csz = clock();
    for (uint32_t ui = 0; ui < nsamples; ui++) {
        (void)cmb_random_normal(m, s);
    }
    clock_t cez = clock();
    double tz = (double)(cez - csz) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", (double)nsamples / tz);

    printf("\nSpeedup for ziggurat vs Box Muller method %.1fx, %4.1f %% less time per sample\n",
           ti / tz, 100.0 * (ti - tz) / ti);

    cmi_test_print_line("=");
}

static void test_tail_std_normal(const uint64_t nsamples)
{
    const double t[] = {0.0005, 0.001, 0.005, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.1 };
    const unsigned n_t = sizeof t / sizeof t[0];

    struct cmb_dataset ds_z = { 0 };
    struct cmb_dataset ds_i = { 0 };
    cmb_dataset_initialize(&ds_z);
    cmb_dataset_initialize(&ds_i);

    for (uint64_t ui = 0; ui < nsamples; ui++) {
        const double x = cmb_random_std_normal();
        cmb_dataset_add(&ds_z, x);
        const double y = normal_bm(0.0, 1.0);
        cmb_dataset_add(&ds_i, y);
    }

    printf("\nBinomial tail test, standard normal, n = %" PRIu64 "\n", nsamples);
    printf("t        expected      obs (zig)      deficit      sigma      obs (B-M)      deficit      sigma\n");

    double sigma_max = 0.0;
    for (unsigned ut = 0; ut < n_t; ut++) {
        uint64_t cnt_z = 0u;
        uint64_t cnt_i = 0u;
        for (uint64_t ui = 0; ui < ds_z.count; ui++) {
            const double x = ds_z.xa[ui];
            if (fabs(x) <= t[ut]) {
                cnt_z++;
            }

            const double y = ds_i.xa[ui];
            if (fabs(y) <= t[ut]) {
                cnt_i++;
            }
        }

        const double p  = erf(t[ut] / sqrt(2.0));
        const double e  = (double)nsamples * p;
        const double sd = sqrt((double)nsamples * p * (1.0 - p));
        const double zz  = ((double)cnt_z - e) / sd;
        const double zi  = ((double)cnt_i - e) / sd;

        printf("%-8.4f %12.1f %12" PRIu64 " %10.4f%% %10.2f %12" PRIu64 " %10.4f%% %10.2f\n",
               t[ut], e,
               cnt_z, 100.0 * (e - (double)cnt_z) / e, zz,
               cnt_i, 100.0 * (e - (double)cnt_i) / e, zi);

        const double sig_abs = fabs(zz);
        if (sig_abs > sigma_max) {
            sigma_max = sig_abs;
        }
    }

    cmb_dataset_terminate(&ds_i);
    cmb_dataset_terminate(&ds_z);

    cmb_assert_always(sigma_max < 5.0);
}

struct cdf_triang_params {
    double a;
    double b;
    double c;
};

static double cdf_triang(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_triang_params *pp = ctx;
    cmb_assert_debug((pp->a <= pp->b) && (pp->b <= pp->c) && (pp->a < pp->c));

    double r;
    if (x <= pp->a) {
        r = 0.0;
    }
    else if (x < pp->b) {
        r = ((x - pp->a) * (x - pp->a)) / ((pp->b - pp->a) * (pp->c - pp->a));
    }
    else if (x < pp->c) {
        r = 1.0 - ((pp->c - x) * (pp->c - x)) / ((pp->c - pp->b) * (pp->c - pp->a));
    }
    else {
        r = 1.0;
    }

    return r;
}

static void test_quality_triangular(const uint64_t nsamples, const double a, const double b, const double c)
{
    printf("\nQuality testing cmb_random_triangular(%g, %g, %g)\n", a, b, c);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_triangular(a, b, c), (x >= a) && (x <= c));

    const double mean = (a + b + c) / 3.0;
    const double g = (a * a) + (b * b) + (c * c) - (a * b) - (a * c) - (b * c);
    const double var = g / 18.0;
    const double snum = ((sqrt(2.0) * (a + b - 2.0 * c) * (2.0 * a - b - c) * (a - 2.0 * b + c)));
    const double sden = 5.0 * pow(g, 1.5);

    print_expected(nsamples, true, mean, true, var, true, snum / sden, true, -3.0 / 5.0);

    QTEST_REPORT();

    struct cdf_triang_params cdfpar = { .a = a, .b = b, .c = c };
    QTEST_GOF_CONT(cdf_triang, &cdfpar);

    QTEST_FINISH();
}

struct cdf_erlang_params {
    unsigned k;
    double m;
};

static double cdf_erlang(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_erlang_params *pp = ctx;
    cmb_assert_debug(pp->m > 0.0);

    double r;
    if (x <= 0.0) {
        r = 0.0;
    }
    else {
        /* CDF expressed in terms of y = lambda x = x / m */
        const double y = x / pp->m;
        const unsigned k = pp->k;
        /* Start with zeroth term to avoid dividing by zero, y^0 / 0! = 1.0 */
        double t = 1.0;
        double sum = 1.0;
        for (unsigned ui = 1u; ui < k; ui++) {
            t *= y / (double)ui;
            sum += t;
        }

        r = 1.0 - exp(-y) * sum;

        /* Cross-check: Erlang(k, m) is Gamma(k, m) at integer shape, so the
         * direct series and the incomplete gamma must agree. */
        double lp;
        cmi_test_log_incomplete_gamma((double)pp->k, y, &lp, NULL);
        cmb_assert_debug(fabs(r - exp(lp)) < 1e-12);
    }

    return r;
}

static void test_quality_erlang(const uint64_t nsamples, const unsigned k, const double m)
{
    printf("\nQuality testing cmb_random_erlang(%u, %g)\n", k, m);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_erlang(k, m), x >= 0.0);

    print_expected(nsamples, true, k * m, true, k * m * m,
                             true, 2.0 / sqrt((double)k), true, 6.0 / (double)k);

    QTEST_REPORT();

    struct cdf_erlang_params cdfpar = { .k = k, .m = m };
    QTEST_GOF_CONT(cdf_erlang, &cdfpar);

    QTEST_FINISH();
}

static void test_quality_hypoexponential(const uint64_t nsamples, const unsigned k, const double m[k])
{
    printf("\nQuality testing cmb_random_hypoexponential, k = %u, m = [", k);
    for (unsigned ui = 0; ui < k-1; ui++) {
        printf("%g, ", m[ui]);
    }
    printf("%g]\n", m[k-1]);

    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_hypoexponential(k, m), x >= 0.0);

    double msum = 0.0;
    double msumsq = 0.0;
    double msumcube = 0.0;
    for (unsigned i = 0; i < k; i++) {
        msum += m[i];
        msumsq += m[i] * m[i];
        msumcube += m[i] * m[i] * m[i];
    }

    print_expected(nsamples, true, msum, true, msumsq,
                             true, 2.0 * msumcube / pow(msumsq, 1.5), false, 0.0);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_hyperexponential(const uint64_t nsamples,
                                          const unsigned k,
                                          const double m[k],
                                          const double p[k])
{
    printf("\nQuality testing cmb_random_hyperexponential, k = %u, m = [", k);
    for (unsigned ui = 0; ui < k-1; ui++) {
        printf("%g, ", m[ui]);
    }
    printf("%g], p = [", m[k-1]);
    for (unsigned ui = 0; ui < k-1; ui++) {
        printf("%g, ", p[ui]);
    }
    printf("%g]\n", p[k-1]);

    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_hyperexponential(k, m, p), x >= 0.0);

    double msum = 0.0;
    double msumsq = 0.0;
    for (unsigned i = 0; i < k; i++) {
        msum += p[i] * m[i];
        for (unsigned j = 0; j < k; j++) {
            msumsq += p[i] * p[j] * (m[i] - m[j]) * (m[i] - m[j]);
        }
    }

    print_expected(nsamples, true, msum, true, msum * msum + msumsq, false,  0.0, false, 0.0);

    QTEST_REPORT();
    QTEST_FINISH();
}

struct cdf_weibull_params {
    double shape;
    double scale;
};

static double cdf_weibull(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_weibull_params *pp = ctx;

    double r;
    if (x < 0.0) {
        r = 0.0;
    }
    else {
        const double sh = pp->shape;
        const double sc = pp->scale;
        r = (1.0 - exp(-pow(x / sc, sh)));
    }

    return r;
}

static void test_quality_weibull(const uint64_t nsamples,
                                 const double shape,
                                 const double scale)
{
    printf("\nQuality testing cmb_random_weibull(%g, %g)\n", shape, scale);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_weibull(shape, scale), x >= 0.0);

    const double z = tgamma(1.0 + 1.0 / shape);
    const double mean = scale * z;
    const double var = scale * scale * (tgamma(1.0 + 2.0 / shape) - z * z);

    /* Skewness exists in closed form but is complicated, left out for now */
    /* No closed form expression for kurtosis */
    print_expected(nsamples, true, mean, true, var, false, 0.0, false, 0.0);

    QTEST_REPORT();

    struct cdf_weibull_params cdfpar = { .shape = shape, .scale = scale };
    QTEST_GOF_CONT(cdf_weibull, &cdfpar);

    QTEST_FINISH();
}

struct cdf_lognorm_params {
    double m;
    double s;
};

static double cdf_lognorm(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_lognorm_params *pp = ctx;

    const double m = pp->m;
    const double s = pp->s;

    return 0.5 * erfc(-(log(x) - m) / (s * sqrt(2.0)));
}

static void test_quality_lognormal(const uint64_t nsamples, const double m, const double s)
{
    printf("\nQuality testing log-normal distribution, m %g, s %g\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_lognormal(m, s), x >= 0.0);

    const double mean = exp(m + 0.5 * s * s);
    const double var = (exp(s * s) - 1.0) * exp(2 * m + s * s);
    const double skew = (exp(s * s) + 2.0) * sqrt(exp(s * s) - 1);
    const double kurt = exp(4.0 * s * s) + 2.0 * exp(3.0 * s * s) + 3.0 * exp (2.0 * s * s) - 6;

    print_expected(nsamples, true, mean, true, var, true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_lognorm_params cdfpar = { .m = m, .s = s };
    QTEST_GOF_CONT(cdf_lognorm, &cdfpar);

    QTEST_FINISH();
}

struct cdf_logistic_params {
    double m;
    double s;
};

static double cdf_logistic(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    struct cdf_lognorm_params *pp = ctx;

    const double m = pp->m;
    const double s = pp->s;

    return 1.0 / (1.0 + exp(-(x - m) / s));
}

static void test_quality_logistic(const uint64_t nsamples, const double m, const double s)
{
    printf("\nQuality testing logistic distribution, m %g, s %g\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_logistic(m, s), true);

    const double var = s * s * M_PI * M_PI / 3.0;

    print_expected(nsamples, true, m, true, var, true, 0.0, true, 6.0 / 5.0);

    QTEST_REPORT();

    struct cdf_logistic_params cdfpar = { .m = m, .s = s };
    QTEST_GOF_CONT(cdf_logistic, &cdfpar);

    QTEST_FINISH();
}

struct cdf_cauchy_params {
    double m;
    double s;
};

static double cdf_cauchy(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    struct cdf_cauchy_params *pp = ctx;

    const double m = pp->m;
    const double s = pp->s;

    return atan((x - m) / s) / M_PI + 0.5;
}

static void test_quality_cauchy(const uint64_t nsamples, const double m, const double s)
{
    printf("\nQuality testing cauchy distribution, m %g, s %g\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_cauchy(m, s), true);

    print_expected(nsamples, false, 0.0, false,0.0,false, 0.0, false, 0.0);

    QTEST_REPORT();

    struct cdf_cauchy_params cdfpar = { .m = m, .s = s };
    QTEST_GOF_CONT(cdf_cauchy, &cdfpar);

    QTEST_FINISH();
}

struct cdf_gamma_params {
    double shape;
    double scale;
};

static double cdf_gamma(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    struct cdf_gamma_params *pp = ctx;

    const double sh = pp->shape;
    const double sc = pp->scale;
    const double y = x / sc;
    double lp;
    cmi_test_log_incomplete_gamma(sh, y, &lp, NULL);

    return exp(lp);
}

static void test_quality_gamma(const uint64_t nsamples, const double shape, const double scale)
{
    printf("\nQuality testing gamma distribution, shape %g, scale %g\n", shape, scale);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_gamma(shape, scale), x >= 0.0);

    const double mean = shape * scale;
    const double var = shape * scale * scale;
    const double skew = 2.0 / sqrt(shape);
    const double kurt = 6.0 / shape;

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_gamma_params cdfpar = { .shape = shape, .scale = scale };
    QTEST_GOF_CONT(cdf_gamma, &cdfpar);

    QTEST_FINISH();
}

struct cdf_pareto_params {
    double a;
    double b;
};

static double cdf_pareto(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_pareto_params *pp = ctx;

    const double a = pp->a;
    const double b = pp->b;

    return 1.0 - pow(b / x, a);
}

static void test_quality_pareto(const uint64_t nsamples, const double a, const double b)
{
    printf("\nQuality testing Pareto distribution, shape %g, scale %g\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_pareto(a, b), x >= b);

    const double mean = (a > 1.0) ? (a * b / (a - 1.0)) : HUGE_VAL;
    const double var = (a > 2.0) ? ((a * b * b) / ((a - 1.0) * (a - 1.0) * (a - 2.0))) : HUGE_VAL;
    const double skew = (a > 3.0) ? 2.0 * ((1.0 + a) / (a - 3.0)) * sqrt((a - 2.0) / a) : HUGE_VAL;
    const double kurt = (a > 4.0) ? 6.0 * ( a * a * a + a * a - 6.0 * a - 2.0)
                                        / (a * (a - 3.0) * (a - 4)) : HUGE_VAL;

    print_expected(nsamples, (a > 1.0), mean, (a > 2.0),var,(a > 3.0), skew, (a > 3.0), kurt);

    QTEST_REPORT();

    struct cdf_pareto_params cdfpar = { .a = a, .b = b };
    QTEST_GOF_CONT(cdf_pareto, &cdfpar);

    QTEST_FINISH();
}

struct cdf_beta_params {
    double a;
    double b;
    double l;
    double r;
};

static double cdf_beta(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_beta_params *pp = ctx;
    cmb_assert_debug(pp->l < pp->r);

    double r;
    if (x <= pp->l) {
        r = 0.0;
    }
    else if (x >= pp->r) {
        r = 1.0;
    }
    else {
        const double y = (x - pp->l) / (pp->r - pp->l);
        double lp;
        cmi_test_log_incomplete_beta(pp->a, pp->b, y, &lp, NULL);
        r = exp(lp);
    }

    return r;
}

static void test_quality_beta(const uint64_t nsamples,
                              const double a, const double b,
                              const double l, const double r)
{
    printf("\nQuality testing beta distribution, shape %g, scale %g, left %g, right %g\n", a, b, l, r);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_beta(a, b, l, r), (x >= l) && (x <= r));

    const double mean = l + (r - l) * (a / (a + b));
    const double var = ((r - l) * (r - l) * (a * b)) / ((a + b) * (a + b) * (a + b + 1));
    const double skew = 2.0 * ((b - a) * sqrt(a + b + 1.0)) / ((a + b + 2.0) * sqrt(a * b));
    const double kurt = 6.0 * ((a - b) * (a - b) * (a + b + 1.0) - a * b * (a + b + 2.0))
                            / (a * b * (a + b + 2.0) * (a + b + 3.0));

    print_expected(nsamples, true, mean, true, var,true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_beta_params cdfpar = { .a = a, .b = b, .l = l, .r = r };
    QTEST_GOF_CONT(cdf_beta, &cdfpar);

    QTEST_FINISH();
}

struct cdf_std_beta_params {
    double a;
    double b;
};

static double cdf_std_beta(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_std_beta_params *pp = ctx;

    double lp;
    cmi_test_log_incomplete_beta(pp->a, pp->b, x, &lp, NULL);

    return exp(lp);
}

static void test_quality_std_beta(const uint64_t nsamples, const double a, const double b)
{
    printf("\nQuality testing beta distribution, shape %g, scale %g\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_std_beta(a, b), (x >= 0.0) && (x <= 1.0));

    const double mean = a / (a + b);
    const double var = (a * b) / ((a + b) * (a + b) * (a + b + 1));
    const double skew = 2.0 * ((b - a) * sqrt(a + b + 1.0)) / ((a + b + 2.0) * sqrt(a * b));
    const double kurt = 6.0 * ((a - b) * (a - b) * (a + b + 1.0) - a * b * (a + b + 2.0))
                            / (a * b * (a + b + 2.0) * (a + b + 3.0));

    print_expected(nsamples, true, mean, true, var,true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_std_beta_params cdfpar = { .a = a, .b = b };
    QTEST_GOF_CONT(cdf_std_beta, &cdfpar);

    QTEST_FINISH();
}

/* PERT is Beta(alpha, beta) scaled to [left, right].  Lambda weights the
 * mode: lambda = 4 gives the classic PERT, larger values concentrate the
 * distribution more tightly around the mode. */
static void pert_shapes(const double left, const double mode, const double right,
                        const double lambda, double *alpha, double *beta)
{
    cmb_assert_debug(left < right);
    cmb_assert_debug((mode >= left) && (mode <= right));
    cmb_assert_debug(lambda > 0.0);

    const double w = right - left;
    *alpha = 1.0 + lambda * (mode - left) / w;
    *beta  = 1.0 + lambda * (right - mode) / w;
}

static void test_quality_PERT(const uint64_t nsamples,
                              const double left, const double mode, const double right)
{
    printf("\nQuality testing PERT distribution, left %g, mode %g, right %g\n", left, mode, right);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_PERT(left, mode, right), (x >= left) && (x <= right));

    double alpha;
    double beta;
    pert_shapes(left, mode, right, 4.0, &alpha, &beta);

    const double s = alpha + beta;
    const double mean = left + (right - left) * alpha / s;
    const double var  = (right - left) * (right - left) * alpha * beta
                      / (s * s * (s + 1.0));
    const double skew = 2.0 * ((beta - alpha) * sqrt(alpha + beta + 1.0))
                        / ((alpha + beta + 2.0) * sqrt(alpha * beta));
    const double kurt = 6.0 * ((alpha - beta) * (alpha - beta) * (alpha + beta + 1.0)
                                   - alpha * beta * (alpha + beta + 2.0))
                            / (alpha * beta * (alpha + beta + 2.0) * (alpha + beta + 3.0));

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_beta_params cdfpar = { .a = alpha, .b = beta, .l = left, .r = right };
    QTEST_GOF_CONT(cdf_beta, &cdfpar);

    QTEST_FINISH();
}

static void test_quality_PERT_mod(const uint64_t nsamples,
                              const double left, const double mode, const double right,
                              const double lambda)
{
    printf("\nQuality testing modified PERT distribution, left %g, mode %g, right %g, lambda %g\n",
            left, mode, right, lambda);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_PERT_mod(left, mode, right, lambda), (x >= left) && (x <= right));

    double alpha;
    double beta;
    pert_shapes(left, mode, right, lambda, &alpha, &beta);

    const double s = alpha + beta;
    const double mean = left + (right - left) * alpha / s;
    const double var  = (right - left) * (right - left) * alpha * beta
                      / (s * s * (s + 1.0));
    const double skew = 2.0 * ((beta - alpha) * sqrt(alpha + beta + 1.0))
                        / ((alpha + beta + 2.0) * sqrt(alpha * beta));
    const double kurt = 6.0 * ((alpha - beta) * (alpha - beta) * (alpha + beta + 1.0)
                                   - alpha * beta * (alpha + beta + 2.0))
                            / (alpha * beta * (alpha + beta + 2.0) * (alpha + beta + 3.0));

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_beta_params cdfpar = { .a = alpha, .b = beta, .l = left, .r = right };
    QTEST_GOF_CONT(cdf_beta, &cdfpar);

    QTEST_FINISH();
}


struct cdf_chisq_params {
    double v;
};

static double cdf_chisq(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    struct cdf_chisq_params *pp = ctx;

    const double v = pp->v;
    const double a = v / 2.0;
    const double b = x / 2.0;
    double lp;
    cmi_test_log_incomplete_gamma(a, b, &lp, NULL);

    return exp(lp);
}

static void test_quality_chisquare(const uint64_t nsamples, const double v)
{
    printf("\nQuality testing chisquare distribution, v %g\n", v);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_chisquared(v), x >= 0.0);

    print_expected(nsamples, true, v, true, 2.0 * v,
                                 true, sqrt(8.0 / v), true, 12.0 / v);
    QTEST_REPORT();

    struct cdf_chisq_params cdfpar = { .v = v };
    QTEST_GOF_CONT(cdf_chisq, &cdfpar);

    QTEST_FINISH();
}

struct cdf_f_params {
    double a;
    double b;
};

static double cdf_f(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_f_params *pp = ctx;
    const double a = 0.5 * pp->a;
    const double b = 0.5 * pp->b;
    const double y = a * x / (a * x + b);

    double lp;
    cmi_test_log_incomplete_beta(a, b, y, &lp, NULL);

    return exp(lp);
}

static void test_quality_f_dist(const uint64_t nsamples, const double a, const double b) {
    printf("\nQuality testing f distribution, a %g, b %g\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_F_dist(a, b), x >= 0.0);

    const double mean = (b > 2.0) ? b / (b - 2.0) : HUGE_VAL;
    const double var = (b > 4.0) ? (2.0 * (b * b) * (a + b - 2.0)) / (a * (b - 2) * (b - 2) * (b - 4.0)) : HUGE_VAL;
    const double skew = (b > 6.0) ? ((2.0 * a + b - 2.0) * sqrt(8.0 * (b - 4.0)))
                                    / ((b - 6.0) * sqrt(a * (a + b - 2.0))) : HUGE_VAL;

    print_expected(nsamples, (b > 2.0), mean, (b > 4.0),var,(b > 6.0), skew, false, 0.0);

    QTEST_REPORT();

    struct cdf_f_params cdfpar = { .a = a, .b = b };
    QTEST_GOF_CONT(cdf_f, &cdfpar);

    QTEST_FINISH();
}

struct cdf_stdt_params {
    double v;
};

static double cdf_stdt(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_stdt_params *pp = ctx;
    const double v = pp->v;

    const double y = v / (v + x * x);
    double lp;
    cmi_test_log_incomplete_beta(0.5 * v, 0.5, y, &lp, NULL);

    const double half = 0.5 * exp(lp);
    return (x > 0.0) ? (1.0 - half) : half;
}

static void test_quality_std_t_dist(const uint64_t nsamples, const double v)
{
    printf("\nQuality testing Student's t distribution, v %g\n", v);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_std_t_dist(v), true);

    const double mean = (v > 1.0) ? 0.0 : HUGE_VAL;
    const double var = (v > 2.0) ? (v / (v - 2)) : HUGE_VAL;
    const double skew = (v > 3.0) ? 0.0 : HUGE_VAL;
    const double kurt = (v > 4.0) ? 6.0 / (v - 4.0) : HUGE_VAL;

    print_expected(nsamples, (v > 1.0), mean, (v > 2.0),var,(v > 3.0), skew, (v > 4.0), kurt);

    QTEST_REPORT();

    struct cdf_stdt_params cdfpar = { .v = v };
    QTEST_GOF_CONT(cdf_stdt, &cdfpar);

    QTEST_FINISH();
}

struct cdf_t_params {
    double m;
    double s;
    double v;
};

static double cdf_t(double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    const struct cdf_t_params *pp = ctx;
    const double m = pp->m;
    const double s = pp->s;
    const double v = pp->v;

    const double a = 0.5 * v;
    const double b = 0.5;
    const double z = (x - m) / s;
    const double y = v / (v + z * z);
    double lp;
    cmi_test_log_incomplete_beta(a, b, y, &lp, NULL);

    const double half = 0.5 * exp(lp);
    return (z > 0.0) ? (1.0 - half) : half;
}

static void test_quality_t_dist(const uint64_t nsamples,
                                const double m, const double s, const double v)
{
    printf("\nQuality testing t distribution, m %g, s %g, v %g,\n", m, s, v);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_t_dist(m, s, v), true);

    const double mean = (v > 1.0) ? m : HUGE_VAL;
    const double var = (v > 2.0) ? (s * s * v) / (v - 2.0) : HUGE_VAL;

    print_expected(nsamples, (v > 1.0), mean, (v > 2.0),var,false, 0.0, false, 0.0);

    QTEST_REPORT();

    struct cdf_t_params cdfpar = { .m = m, .s = s, .v = v };
    QTEST_GOF_CONT(cdf_t, &cdfpar);

    QTEST_FINISH();
}

struct cdf_rayleigh_params {
    double s;
};

static double cdf_rayleigh(const double x, void *ctx)
{
    cmb_assert_debug(ctx != NULL);
    struct cdf_rayleigh_params *pp = ctx;
    const double s = pp->s;

    double r;
    if (x <= 0.0) {
        r = 0.0;
    }
    else {
        r = 1.0 - exp(-x * x / (2.0 * s * s));
    }

    return r;
}

static void test_quality_rayleigh(const uint64_t nsamples, const double s)
{
    printf("\nQuality testing Rayleigh distribution, s %g\n", s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_rayleigh(s), x >= 0.0);

    const double mean = s * sqrt(0.5 * M_PI);
    const double var = 0.5 * (4.0 - M_PI) * s * s;
    const double skew = 2.0 * sqrt(M_PI) * (M_PI - 3.0) / pow((4.0 - M_PI), 1.5);
    const double kurt = -(6.0 * M_PI * M_PI - 24.0 * M_PI + 16.0) / ((4.0 - M_PI) * (4.0 - M_PI));

    print_expected(nsamples, true, mean, true, var, true, skew, true, kurt);

    QTEST_REPORT();

    struct cdf_rayleigh_params cdfpar = { .s = s };
    QTEST_GOF_CONT(cdf_rayleigh, &cdfpar);

    QTEST_FINISH();
}

static void test_quality_flip(const uint64_t nsamples)
{
    printf("\nQuality testing unbiased coin flip, p = 0.5\n");
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_flip(), (x == 0) || (x == 1));

    const double mean = 0.5;
    const double var = 0.5 * 0.5;
    const double skew = 0.0;
    const double kurt = (1.0 - 6.0 * 0.5 * 0.5) / (0.5 * 0.5);

    print_expected(nsamples, true, mean, true, var, true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_bernoulli(const uint64_t nsamples, const double p)
{
    printf("\nQuality testing biased Bernoulli trials, p = %g\n", p);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_bernoulli(p), (x == 0) || (x == 1));

    const double mean = p;
    const double q = 1.0 - p;
    const double var = p * q;
    const double skew = (q - p) / sqrt(p * q);
    const double kurt = (1.0 - 6.0 * p * q) / (p * q);

    print_expected(nsamples, true, mean, true, var,true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_geometric(const uint64_t nsamples, const double p)
{
    printf("\nQuality testing geometric distribution, p = %g\n", p);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_geometric(p), x >= 1);

    const double mean = 1.0 / p;
    const double q = 1.0 - p;
    const double var = q / (p * p);
    const double skew = (2 - p) / sqrt(q);
    const double kurt = 6.0 + p * p / q;

    print_expected(nsamples, true, mean, true, var, true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_binomial(const uint64_t nsamples, const unsigned n, const double p)
{
    printf("\nQuality testing binomial distribution, n = %d, p = %g\n", n, p);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_binomial(n, p), x >= 0);

    const double mean = n * p;
    const double q = 1.0 - p;
    const double var = n * p * q;
    const double skew = (q - p) / sqrt(n * p * q);
    const double kurt = (1.0 - 6.0 * p * q) / (n * p * q);

    print_expected(nsamples, true, mean, true, var,true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_pascal(const uint64_t nsamples, const unsigned m, const double p)
{
    printf("\nQuality testing negative binomial (Pascal) distribution, m = %d, p = %g\n", m, p);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_pascal(m, p), x >= 0);

    const double q = 1.0 - p;
    const double mean = (double)m * q / p;
    const double var = (double)m * q / (p * p);
    const double skew = (2.0 - p) / sqrt(q * (double)m);
    const double kurt = 6.0 / (double)m + (p * p) / (q * (double)m);

    print_expected(nsamples, true, mean, true, var, true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}


static void test_quality_poisson(const uint64_t nsamples, const double r)
{
    printf("\nQuality testing Poisson distribution, r = %g\n", r);

    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_poisson(r), x >= 0);

    print_expected(nsamples, true, r, true, r, true, 1.0 / sqrt(r), true, 1.0 / r);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_dice(const uint64_t nsamples, const long a, const long b)
{
    printf("\nQuality testing dice (discrete uniform) distribution, a = %ld, b = %ld\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_dice(a, b), (x >= a) && (x <= b));

    const double mean = (double)(a + b) / 2.0;
    const double var = ((double)(b - a + 1) * (double)(b - a + 1) - 1.0) / 12.0;
    const double skew = 0.0;
    const double n = (double)(b - a + 1);
    const double kurt = - (6.0 *(n * n  + 1.0)) / (5.0 * ((n * n - 1.0)));

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void print_discrete_expects(const uint64_t nsamples, const unsigned n, const double pa[n])
{
    double m1 = 0.0;
    double m2 = 0.0;
    double m3 = 0.0;
    double m4 = 0.0;

    for (unsigned ui = 0; ui < n; ui++) {
        const double iv = (double)ui;
        const double p = pa[ui];
        m1 += iv * p;
        const double iv2 = iv * iv;
        m2 += iv2 * p;
        m3 += iv * iv2 * p;
        m4 += iv2 * iv2 * p;
    }

    const double mean = m1;
    const double var = m2 - m1 * m1;
    const bool has_skew = (var > 1e-12);
    const bool has_kurt = has_skew;
    double skew = 0.0;
    double kurt = 0.0;
    if (has_skew) {
        const double mu3 = m3 - 3.0 * m1 * m2 + 2.0 * m1 * m1 * m1;
        skew = mu3 / pow(var, 3.0 / 2.0);
        const double mu4 = m4 - 4.0 * m1 * m3 + 6.0 * m1 * m1 * m2 - 3.0 * m1 * m1 * m1 * m1;
        kurt = mu4 / (var * var) - 3.0;
    }

    print_expected(nsamples, true, mean, true, var, has_skew, skew, has_kurt, kurt);
}

static void test_quality_loaded_dice(const uint64_t nsamples, const unsigned n, double pa[n])
{
    printf("\nQuality testing discrete non-uniform distribution, n = %u\n", n);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_discrete_nonuniform(n, pa), (x >= 0) && (x <= (n - 1)));

    print_discrete_expects(nsamples, n, pa);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_vose_alias(const uint64_t nsamples, const unsigned n, const double pa[n])
{
    printf("\nQuality testing vose alias sampling, n = %u\n", n);
    QTEST_PREPARE();
    struct cmb_random_alias *alp = cmb_random_alias_create(n, pa);
    QTEST_EXECUTE((double)cmb_random_alias_sample(alp), (x >= 0) && (x <= (n - 1)));

    print_discrete_expects(nsamples, n, pa);

    QTEST_REPORT();
    cmb_random_alias_destroy(alp);
    QTEST_FINISH();
}

static void test_speed_vose_alias(const uint64_t nsamples, const unsigned init, const unsigned end, const unsigned step)
{
    printf("\nSpeed testing vose alias sampling, %" PRIu64 " samples\n", nsamples);
    printf("Iterations per second (ips)\n");
    printf("n\tips simple\tips alias\tspeedup\n");
    for (unsigned n = init; n <= end; n += step) {
        double *pa = calloc(n, sizeof *pa);
        cmb_assert(pa != NULL);
        double sum = 0.0;
        for (unsigned ui = 0; ui < n; ui++) {
            pa[ui] = cmb_random();
            sum += pa[ui];
        }

        for (unsigned ui = 0; ui < n; ui++) {
            pa[ui] /= sum;
        }

        const clock_t cs_simple = clock();
        for (unsigned ui = 0; ui < nsamples; ui++) {
            (void)cmb_random_discrete_nonuniform(n, pa);
        }

        const clock_t ce_simple = clock();

        const clock_t cs_alias = clock();
        struct cmb_random_alias *alp = cmb_random_alias_create(n, pa);
        for (unsigned i = 0; i < nsamples; i++) {
            (void)cmb_random_alias_sample(alp);
        }

        cmb_random_alias_destroy(alp);
        const clock_t ce_alias = clock();
        free(pa);

        const double t_simple = (double)(ce_simple - cs_simple) / CLOCKS_PER_SEC;
        const double ips_simple = (double)nsamples / t_simple;
        const double t_alias = (double)(ce_alias - cs_alias) / CLOCKS_PER_SEC;
        const double ips_alias = (double)nsamples / t_alias;
        const double speedup = (ips_alias - ips_simple) / ips_simple;
        printf("%u\t%9.4g\t%9.4g\t%8.4g %%\n", n, ips_simple, ips_alias, 100.0 * speedup);
    }

    cmi_test_print_line("=");
}

int main(const int argc, char *argv[])
{
    bool timing_enabled = false;
    bool fixed_seed = false;
    uint64_t seed = cmb_random_hwseed();
    uint64_t nsamples = 1000000u;

    int opt;
    while ((opt = getopt(argc, argv, "n:s:t")) != -1) {
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
                fixed_seed = true;
                if (errno != 0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 't':
                timing_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-n <nsamples>][-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    struct timespec start_time;
    if (timing_enabled) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }

    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("************** Testing random number generators and distributions **************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    test_quality_random(nsamples);
    test_quality_uniform(nsamples, -1.0, 2.0);
    test_quality_triangular(nsamples, -1.0, 2.0, 3.0);

    test_quality_std_normal(nsamples);
    test_tail_std_normal(10u * nsamples);

    test_quality_normal(nsamples, 2.0, 1.0);
    if (fixed_seed == false) {
        /* Probably trying to compare outputs, and this one is not deterministic */
        test_speed_normal(nsamples, 2.0, 1.0);
    }

    test_quality_std_exponential(nsamples);
    test_tail_std_exponential(10u * nsamples);

    test_quality_exponential(nsamples, 2.0);
    test_quality_exponential(nsamples, 0.01);
    test_quality_exponential(nsamples, 1.0e6);

    if (fixed_seed == false) {
         test_speed_exponential(nsamples, 2.0);
    }

    test_quality_erlang(nsamples, 5, 1.0);

    const double m[4] = { 1.0, 2.0, 4.0, 8.0 };
    test_quality_hypoexponential(nsamples, 4, m);

    const double p[4] = { 0.1, 0.2, 0.3, 0.4 };
    test_quality_hyperexponential(nsamples, 4, m, p);

    test_quality_weibull(nsamples, 2.0, 3.0);

    test_quality_gamma(nsamples, 3.0, 0.5);
    test_quality_gamma(nsamples, 1.0, 1.0);
    test_quality_gamma(nsamples, 0.5, 2.0);
    test_quality_gamma(nsamples, 0.1, 2.0);

    test_quality_lognormal(nsamples, 1.0, 0.5);
    test_quality_logistic(nsamples, 1.0, 0.5);
    test_quality_cauchy(nsamples, 1.0, 0.5);

    test_quality_std_beta(nsamples, 2.0, 5.0);
    test_quality_beta(nsamples, 2.0, 5.0, 0.0, 1.0);
    test_quality_beta(nsamples, 0.5, 2.0, 0.0, 1.0);
    test_quality_beta(nsamples, 0.5, 0.5, 2.0, 5.0);
    test_quality_beta(nsamples, 0.5, 0.5, -2.0, 2.0);
    test_quality_PERT(nsamples, 2.0, 5.0, 10.0);
    test_quality_PERT_mod(nsamples, 2.0, 5.0, 10.0, 5.0);
    test_quality_PERT_mod(nsamples, 2.0, 3.0, 5.0, 2.0);
    test_quality_pareto(nsamples, 3.0, 2.0);

    test_quality_chisquare(nsamples, 4);
    test_quality_f_dist(nsamples, 3.0, 5.0);
    test_quality_std_t_dist(nsamples, 3.0);
    test_quality_t_dist(nsamples, 1.0, 2.0, 3.0);
    test_quality_rayleigh(nsamples, 1.5);

    printf("************************* Integer-valued distributions *************************\n");

    test_quality_flip(nsamples);
    test_quality_bernoulli(nsamples, 0.6);
    test_quality_geometric(nsamples, 0.1);
    test_quality_binomial(nsamples, 10, 0.1);
    test_quality_binomial(nsamples, 100, 0.5);
    test_quality_pascal(nsamples, 10, 0.1);

    test_quality_poisson(nsamples, 5.0);
    test_quality_poisson(nsamples, 50.0);
    test_quality_poisson(nsamples, 1.0e13);

    test_quality_dice(nsamples, 1, 6);

    double q[7] = { 0.05, 0.05, 0.1, 0.1, 0.2, 0.2, 0.3 };
    test_quality_loaded_dice(nsamples, 7, q);
    test_quality_vose_alias(nsamples, 7, q);
    if (fixed_seed == false) {
        test_speed_vose_alias(nsamples, 5, 50, 5);
    }

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