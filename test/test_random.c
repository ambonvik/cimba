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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmb_dataset.h"
#include "cmb_random.h"

#include "test.h"

/* Test macros */
#define MOMENTS 15
#define ACFS 15
#define LEADINS true

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
    cmb_dataset_summarize(&ds, &dsu); \
    printf("Actual:   "); \
    cmb_datasummary_print(&dsu, stdout, LEADINS); \
    cmb_dataset_histogram_print(&ds, stdout, 20, 0.0, 0.0)

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
    cmb_dataset_terminate(&ds); \
    cmi_test_print_line("=")

static void print_single(const char *lead, const bool has_val, const double val)
{
    printf("  %s ", lead);
    if (has_val) {
        printf("%#8.4g", val);
    }
    else {
        printf("   ---  ");
    }
}

static void print_expected(const uint64_t n,
                           const bool has_mean, const double mean,
                           const bool has_var, const double var,
                           const bool has_skew, const double skew,
                           const bool has_kurt, const double kurt)
{
    printf("\nExpected: N %8" PRIu64, n);
    print_single("Mean", has_mean, mean);
    print_single("StdDev", has_var, sqrt(var));
    print_single("Variance", has_var, var);
    print_single("Skewness", has_skew, skew);
    print_single("Kurtosis", has_kurt, kurt);
    printf("\n");
}

/**** Start of test scripts ****/

static void test_quality_random(uint64_t nsamples)
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

    QTEST_FINISH();
}

static void test_quality_uniform(uint64_t nsamples, const double a, const double b)
{
    printf("\nQuality testing cmb_random_uniform(%g,%g)\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_uniform(a, b), (x >= a) && (x <= b));

    const double var = (b - a) * (b - a) / 12;

    print_expected(nsamples, true, 0.5 * (a + b), true, var, true, 0.0, true, -6.0 / 5.0);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_std_exponential(uint64_t nsamples)
{
    printf("\nQuality testing standard exponential distribution, mean = 1\n");
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_std_exponential(), x >= 0.0);

    print_expected(nsamples, true, 1.0, true, 1.0, true, 2.0, true, 6.0);

    QTEST_REPORT();
    QTEST_REPORT_ACFS();
    QTEST_FINISH();
}

/* Exponential, inverse transform method for comparison */
static double exponential_inv(const double m)
{
    cmb_assert_release(m > 0.0);
    double x;

    /* In the extremely unlikely case of exact zero, reject it and retry */
    while ((x = cmb_random()) == 0.0) { }

    return -log(x) * m;
}

static void test_speed_exponential(uint64_t nsamples, const double m)
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

static void test_quality_exponential(uint64_t nsamples, const double m)
{
    printf("\nQuality testing exponential distribution, mean = %f\n", m);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_exponential(m), x >= 0.0);

    print_expected(nsamples, true, m, true, m * m, true, 2.0, true, 6.0);

    QTEST_REPORT();
    QTEST_FINISH();
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

static void test_quality_std_normal(uint64_t nsamples)
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

    QTEST_FINISH();
}

static void test_quality_normal(uint64_t nsamples, const double m, const double s)
{
    printf("\nQuality testing normal distribution, mean = %f, sigma = %f\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_normal(m, s), true);

    print_expected(nsamples, true, m, true, s * s, true, 0.0, true, 0.0);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_speed_normal(uint64_t nsamples, const double m, const double s)
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

static void test_quality_triangular(uint64_t nsamples, const double a, const double b, const double c)
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
    QTEST_FINISH();
}

static void test_quality_erlang(uint64_t nsamples, const unsigned k, const double m)
{
    printf("\nQuality testing cmb_random_erlang(%u, %g)\n", k, m);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_erlang(k, m), x >= 0.0);

    print_expected(nsamples, true, k * m, true, k * m * m, true, 2.0 / sqrt((double)k), true, 6.0 / (double)k);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_hypoexponential(uint64_t nsamples, const unsigned k, const double m[k])
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

    print_expected(nsamples, true, msum, true, msumsq, true, 2.0 * msumcube / pow(msumsq, 1.5), false, 0.0);

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
    QTEST_FINISH();
}

static void test_quality_lognormal(const uint64_t nsamples,const double m, const double s)
{
    printf("\nQuality testing log-normal distribution, m %g, s %g\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_lognormal(m, s), x >= 0.0);

    const double mean = exp(m + 0.5 * s * s);
    const double var = (exp(s * s) - 1.0) * exp(2 * m + s * s);
    const double skew = (exp(s * s) + 2.0) * sqrt(exp(s * s) - 1);
    const double kurt = exp(4.0 * s * s) + 2.0 * exp(3.0 * s * s) + 3.0 * exp (2.0 * s * s) - 6;

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_logistic(const uint64_t nsamples,const double m, const double s)
{
    printf("\nQuality testing logistic distribution, m %g, s %g\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_logistic(m, s), true);

    const double var = s * s * M_PI * M_PI / 3.0;

    print_expected(nsamples, true, m, true,var,true, 0.0, true, 6.0 / 5.0);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_cauchy(const uint64_t nsamples,const double m, const double s)
{
    printf("\nQuality testing cauchy distribution, m %g, s %g\n", m, s);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_cauchy(m, s), true);

    print_expected(nsamples, false, 0.0, false,0.0,false, 0.0, false, 0.0);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_gamma(const uint64_t nsamples,const double shape, const double scale)
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
    QTEST_FINISH();
}

static void test_quality_pareto(const uint64_t nsamples,const double a, const double b)
{
    printf("\nQuality testing Pareto distribution, shape %g, scale %g\n", a, b);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_pareto(a, b), x >= b);

    const double mean = (a > 1.0) ? (a * b / (a - 1.0)) : HUGE_VAL;
    const double var = (a > 2.0) ? ((a * b * b) / ((a - 1.0) * (a - 1.0) * (a - 2.0))) : HUGE_VAL;
    const double skew = (a > 3.0) ? 2.0 * ((1.0 + a) / (a - 3.0)) * sqrt((a - 2.0) / a) : HUGE_VAL;
    const double kurt = (a > 3.0) ? 6.0 * ( a * a * a + a * a - 6.0 * a - 2.0)
                                        / (a * (a - 3.0) * (a - 4)) : HUGE_VAL;

    print_expected(nsamples, (a > 1.0), mean, (a > 2.0),var,(a > 3.0), skew, (a > 3.0), kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_beta(const uint64_t nsamples,const double a, const double b, const double l, const double r)
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
    QTEST_FINISH();
}

static void test_quality_std_beta(const uint64_t nsamples,const double a, const double b)
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
    QTEST_FINISH();
}

static void test_quality_PERT(const uint64_t nsamples,const double left, const double mode, const double right)
{
    printf("\nQuality testing PERT distribution, left %g, mode %g, right %g\n", left, mode, right);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_PERT(left, mode, right), (x >= left) && (x <= right));

    const double a = left;
    const double b = mode;
    const double c = right;

    const double alpha = (4.0 * b + c - 5.0 * a) / (c - a);
    const double beta = (5.0 * c - a - 4.0 * b) / (c - a);
    const double mu = (a + 4.0 * b + c) / 6.0;

    const double mean = mu;
    const double var = (mu - a) * (c - mu) / 7.0;
    const double skew = 2.0 * ((beta - alpha) * sqrt(alpha + beta + 1.0))
                        / ((alpha + beta + 2.0) * sqrt(alpha * beta));
    const double kurt = 6.0 * ((alpha - beta) * (alpha - beta) * (alpha + beta + 1.0)
                                   - alpha * beta * (alpha + beta + 2.0))
                            / (alpha * beta * (alpha + beta + 2.0) * (alpha + beta + 3.0));

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

    QTEST_REPORT();
    QTEST_FINISH();
}

static void test_quality_chisquare(const uint64_t nsamples, const double v)
{
    printf("\nQuality testing chisquare distribution, v %g\n", v);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_chisquared(v), x >= 0.0);

    print_expected(nsamples, true, v, true, 2.0 * v,
                                 true, sqrt(8.0 / v), true, 12.0 / v);
    QTEST_REPORT();
    QTEST_FINISH();
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
    QTEST_FINISH();
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
    QTEST_FINISH();
}

static void test_quality_t_dist(const uint64_t nsamples, const double m, const double s, const double v)
{
    printf("\nQuality testing t distribution, m %g, s %g, v %g,\n", m, s, v);
    QTEST_PREPARE();
    QTEST_EXECUTE(cmb_random_t_dist(m, s, v), true);

    const double mean = (v > 1.0) ? m : HUGE_VAL;
    const double var = (v > 2.0) ? (s * s * v) / (v - 2.0) : HUGE_VAL;

    print_expected(nsamples, (v > 1.0), mean, (v > 2.0),var,false, 0.0, false, 0.0);

    QTEST_REPORT();
    QTEST_FINISH();
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
    const double skew = (q - p) / sqrt(p * q);
    const double kurt = (1.0 - 6.0 * p * q) / (p * q);

    print_expected(nsamples, true, mean, true,var,true, skew, true, kurt);

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
    printf("\nQuality testing loaded dice distribution, n = %u\n", n);
    QTEST_PREPARE();
    QTEST_EXECUTE((double)cmb_random_loaded_dice(n, pa), (x >= 0) && (x <= (n - 1)));

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
            (void)cmb_random_loaded_dice(n, pa);
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
        printf("%u\t%9.4g\t%9.4g\t%8.4g%%\n", n, ips_simple, ips_alias, 100.0 * speedup);
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

    cmb_random_initialize(seed);

    cmi_test_print_line("*");
    printf("************** Testing random number generators and distributions **************\n");
    cmi_test_print_line("*");
    printf("Using seed: 0x%" PRIx64 "\n", seed);

    test_quality_random(nsamples);
    test_quality_uniform(nsamples, -1.0, 2.0);
    test_quality_triangular(nsamples, -1.0, 2.0, 3.0);

    test_quality_std_normal(nsamples);
    test_quality_normal(nsamples, 2.0, 1.0);
    if (fixed_seed == false) {
        /* Probably trying to compare outputs, and this one is not deterministic */
        test_speed_normal(nsamples, 2.0, 1.0);
    }

    test_quality_std_exponential(nsamples);
    test_quality_exponential(nsamples, 2.0);
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

    test_quality_lognormal(nsamples, 1.0, 0.5);
    test_quality_logistic(nsamples, 1.0, 0.5);
    test_quality_cauchy(nsamples, 1.0, 0.5);

    test_quality_std_beta(nsamples, 2.0, 5.0);
    test_quality_beta(nsamples, 2.0, 5.0, 0.0, 1.0);
    test_quality_beta(nsamples, 0.5, 2.0, 0.0, 1.0);
    test_quality_beta(nsamples, 0.5, 0.5, 2.0, 5.0);
    test_quality_PERT(nsamples, 2.0, 5.0, 10.0);
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
    test_quality_pascal(nsamples, 10, 0.1);
    test_quality_poisson(nsamples, 10.0);

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