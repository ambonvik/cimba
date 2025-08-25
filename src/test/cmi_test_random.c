/*
 * Test script for random number distributions.
 *
 * Plot a histogram and compare sample average and standard deviation to
 * expected values for each distribution provided. For the most critical
 * distributions (uniform, exponential, normal - building blocs for the
 * others) also compare the first few raw moments to expected values.
 * The number of moments to check is #defined below as MOMENTS, the
 * number of max_iter to use for each test as max_iter.
 *
 * Some alternative implementations of certain distributions in this
 * file for performance comparison purposes, e.g. Box Muller normal.
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

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "cmb_data.h"
#include "cmb_random.h"

#define MOMENTS 15
#define ACFS 15
static const uint64_t max_iter =  10000000ull;
static const bool with_leadins = true;

static uint64_t create_seed(void) {
    struct timespec ts;
    (void) clock_gettime(CLOCK_REALTIME, &ts);

    return (uint64_t)(ts.tv_nsec ^ ts.tv_sec);
}

static double uniform_raw_moment(const int k, double a, double b) {
    if (a == b) {
        return pow(a, k);
    }

    const double num = pow(b, k + 1) - pow(a, k + 1);
    const double den = (b - a) * (k + 1);

    return num / den;
}

static const uint16_t line_length = 80u;

static void print_line(char *str) {
    uint16_t len = strlen(str);
    uint16_t repeats = line_length / len;
    for (uint16_t ui = 0; ui < repeats; ui++) {
        printf("%s", str);
    }

    printf("\n");
}

static void test_quality_random(void) {
    printf("\nQuality testing basic random number generator cmb_random(), uniform on [0,1]\n");
    const uint64_t seed = create_seed();
    cmb_random_init(seed);

    printf("Seed = %#llx, drawing %llu samples...\n", seed, max_iter);
    struct cmb_dataset ds;
    cmb_dataset_init(&ds);

    double moment_r[MOMENTS] = { 0.0 };

    for (uint64_t ui = 0; ui < max_iter; ui++) {
        const double xi = cmb_random();
        cmb_dataset_add(&ds, xi);

        double xij = xi;
        for (int j = 0; j < MOMENTS; j++) {
            moment_r[j] += xij;
            xij *= xi;
        }
    }

    printf("\nExpected values: avg %g, std dev %g, variance %g, skewness %g, kurtosis %g\n",
                            0.5, 1.0 / sqrt(12), 1.0 / 12.0, 0.0, -6.0 / 5.0);
    printf("Actual outcome:\n");;

    struct cmb_summary dsu = { 0 };
    cmb_dataset_summarize(&ds, &dsu);
    cmb_summary_print(&dsu, stdout, with_leadins);
    cmb_dataset_print_histogram(&ds, stdout, 20, 0.0, 0.0);
    cmb_dataset_print_fivenum(&ds, stdout, with_leadins);

    printf("\nAutocorrelation factors (expected 0.0):\n");
    double acf[ACFS + 1] = { 0.0 };
    cmb_dataset_ACF(&ds, ACFS, acf);
    cmb_dataset_print_correlogram(&ds, stdout, ACFS, acf);

    printf("\nPartial autocorrelation factors (expected 0.0):\n");
    double pacf[ACFS + 1] = { 0.0 };
    cmb_dataset_PACF(&ds, ACFS, pacf, acf);
    cmb_dataset_print_correlogram(&ds, stdout, ACFS, pacf);

    cmb_dataset_clear(&ds);

    printf("\nRaw moment:   Expected:   Actual:   Error:\n");
    print_line("-");
    for (uint16_t ui = 0; ui < MOMENTS; ui++) {
        const double expmom = 1.0 / (double)(ui + 2u);
        const double avgmom = moment_r[ui] / (double)max_iter;
        printf("%5d        %8.5f    %8.5f   % 6.3f %%\n", ui + 1u,
               expmom, avgmom, 100.0 * (avgmom - expmom) / expmom);
    }
    print_line("-");
}

#if 0
static void test_quality_uniform(double a, double b) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing uniform distribution, l = %g, r = %g, seed %#llx\n", a, b, seed);
    cmb_dataset_clear(&ds);
    printf("Drawing %llu samples...\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        const double x = cmb_random_uniform(a, b);
        cmb_dataset_add(&ds, x);
    }

    printf("Expected values: avg %g, std dev %g\n", 0.5 * (a + b), sqrt((b - a) * (b - a) / 12));
    cmb_dataset_summarize(&ds, &dsu);
    cmb_summary_print(&dsu, stdout, with_leadins);
    cmb_dataset_print_histogram(&ds, stdout, 20, 0.0, 0.0);

    printf("Autocorrelation factors (expected 0.0)\n");
    double acf[ACFS + 1] = { 0.0 };
    cmb_dataset_ACF(&ds, ACFS, acf);
    cmb_dataset_print_correlogram(&ds, stdout, ACFS, acf);

    printf("Partial autocorrelation factors (expected 0.0)\n");
    double pacf[ACFS + 1] = { 0.0 };
    cmb_dataset_PACF(&ds, ACFS, pacf, acf);
    cmb_dataset_print_correlogram(&ds, stdout, ACFS, pacf);

    cmb_dataset_clear(&ds);

    printf("\nTesting first %d raw moments, drawing another %llu samples...\n", MOMENTS, max_iter);
    cmb_random_init(seed);
    double moment_u[MOMENTS] = { 0.0 };
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        const double xi = cmb_random_uniform(a, b);
        double xij = xi;
        for (uint16_t uj = 0; uj < MOMENTS; uj++) {
            moment_u[uj] += xij;
            xij *= xi;
        }
    }

    printf("Moment\tExpected\tActual  \tError\n");
    for (int i = 0; i < MOMENTS; i++) {
        const double expmom = uniform_raw_moment(i + 1, a, b);
        const double avgmom = moment_u[i] / max_iter;
        printf("%d\t%8g\t%8g\t%8.4g %%\n", i + 1,
               expmom, avgmom, 100.0 * (avgmom - expmom)/expmom);
    }
}


static void test_triangular(double a, double b, double c) {
    uint64_t seed = create_seed();
    printf("\nQuality testing triangular distribution, left a = %g, mode b = %g, right c = %g, seed = %#llx.\n", a, b, c, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples...\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_triangular(-1.0, 2.0, 3.0);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected value: %g, std dev %g\n",(a + b + c)/3.0, sqrt((a*a + b*b + c*c - a*b - a*c - b*c)/18.0));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_bernoulli(double p) {
    uint64_t seed = create_seed();
    printf("\nQuality testing unbiased coin flip, seed = %#llx.\n", seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_flip();
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n",0.5, 0.5);
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 10, 0, 0);
    cmb_dataset_clear(dsp);


    seed = create_seed();
    printf("\nQuality testing biased Bernoulli trials, p = %g, seed = %#llx.\n", p, seed);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_bernoulli(p);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n",p, sqrt(p * ( 1.0 - p)));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 10, 0, 0);
    cmb_dataset_destroy(dsp);
}

unsigned long factorial(unsigned int n) {
    unsigned long f = 1;
    for (int i = 1; i <= n; i++)
        f *= i;

    return f;
}

/* Exponential, inverse transform method */
double smi_exponential_inv(double m) {
    assert (m > 0.0);
    double x;
    /* In the extremely unlikely case of exact zero, reject it and retry */
    while ((x = cmb_random()) == 0.0) { }
    return -log(x) * m;
}

static void test_quality_exponential(double m) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);

    printf("\nQuality testing exponential distribution, m = %g, seed = %#llx.\n", m, seed);
    printf("Drawing %d samples....\n", max_iter);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_exponential(m);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n",m, m);
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);

    printf("\nTesting exponential distribution raw moments, seed = %#llx.\n", seed);
    printf("Inversion method, drawing %d samples...\n", max_iter);
    double moment_inv[MOMENTS] = { 0 };
    cmb_random_init(seed);
     for (uint32_t ui = 0; ui < max_iter; ui++) {
        double xi = smi_exponential_inv(m);
        double xij = xi;
        for (int j = 0; j < MOMENTS; j++) {
            moment_inv[j] += xij;
            xij *= xi;
        }
    }

    printf("Ziggurat method, drawing %d samples...\n", max_iter);
    double moment_zig[MOMENTS] = { 0 };
    cmb_random_init(seed);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double xz = cmb_random_exponential(m);
        double xzj = xz;
        for (int j = 0; j < MOMENTS; j++) {
            moment_zig[j] += xzj;
            xzj *= xz;
        }
    }

    printf("Moment\tExpected\tinversion\tinv err\t\tziggurat\tzig err\n");
    for (int i = 0; i < MOMENTS; i++) {
        double expmom = (double)factorial(i + 1) * pow(m,(double)(i + 1));
        double avgmom_zig = moment_zig[i] / max_iter;
        double avgmom_inv = moment_inv[i] / max_iter;
        printf("%d\t%8g\t%8g\t%8.4g %%\t%8g\t%8.4g %%\n", i + 1,
               expmom, avgmom_inv, 100.0 * (avgmom_inv - expmom)/expmom,
               avgmom_zig, 100.0 * (avgmom_zig - expmom)/expmom);
    }
}

static void test_speed_exponential(double m) {
    uint64_t seed = create_seed();
    printf("\nSpeed testing exponential distribution, seed = %#llx\n", seed);
    cmb_random_init(seed);
    printf("Inversion method, drawing %d samples...", max_iter);

    clock_t csi = clock();
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        (void)smi_exponential_inv(m);
    }
    clock_t cei = clock();
    double ti = (double)(cei - csi) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", max_iter / ti);

    cmb_random_init(seed);
    printf("Ziggurat method, drawing %d samples...", max_iter);
    clock_t csz = clock();
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        (void)cmb_random_exponential(m);
    }
    clock_t cez = clock();
    double tz = (double)(cez - csz) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", max_iter / tz);

    printf("Speedup for ziggurat vs inversion method %.1fx, %4.1f %% less time per sample.\n",
           ti / tz, 100.0 * (ti - tz) / ti);
}

static void test_quality_erlang(unsigned k, double m) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing Erlang distribution, k = %d, m = %g, seed = %#llx.\n", k, m, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_erlang(k, m);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", k * m, sqrt(k * m * m));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_hypoexponential(unsigned k, double m[k]) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing hypoexponential distribution, k = %d, seed = %#llx.\n", k, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_hypoexponential(k, m);
        cmb_dataset_add(dsp, x);
    }

    double msum = 0.0;
    double msumsq = 0.0;
    for (unsigned i = 0; i < k; i++) {
        msum += m[i];
        msumsq += m[i] * m[i];
    }
    printf("Expected values: avg %g, std dev %g\n", msum, sqrt(msumsq));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_hyperexponential(unsigned k, double m[k], double p[k]) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing hyperexponential distribution, k = %d, seed = %#llx.\n", k, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_hyperexponential(k, m, p);
        cmb_dataset_add(dsp, x);
    }

    double msum = 0.0;
    double msumsq = 0.0;
    for (unsigned i = 0; i < k; i++) {
        msum += p[i] * m[i];
        for (unsigned j = 0; j < k; j++) {
            msumsq += p[i] * p[j] * (m[i] - m[j]) * (m[i] - m[j]);
        }
    }
    printf("Expected values: avg %g, std dev %g\n", msum, sqrt(msum * msum + msumsq));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_weibull(double shape, double scale) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing Weibull distribution, shape %g, scale %g, seed = %#llx.\n", shape, scale, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_weibull(shape, scale);
        cmb_dataset_add(dsp, x);
    }

    double z = tgamma(1.0 + 1.0 / shape);
    printf("Expected values: avg %g, std dev %g\n", scale * z,
           sqrt(scale * scale * (tgamma(1.0 + 2.0 / shape) - z * z)));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

/*
 * Normal distribution using Box-Muller approach, generating two and saving one
 * for next time.
 */
double smi_normal_bm(double m, double s) {
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
double smi_normal_raw_moment(int n, double mu, double sigma) {
    if (n == 0) {
        return 1.0;
    }
    else if (n == 1) {
        return mu;
    }
    else {
        /* divide & conquer */
        return mu * smi_normal_raw_moment(n - 1, mu, sigma)
            + (n - 1) * sigma * sigma * smi_normal_raw_moment(n - 2, mu, sigma);
    }
}

static void test_quality_normal(double m, double s) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing normal distribution, seed = %#llx.\n", seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_normal(m, s);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n",m, s);
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);

    printf("\nTesting normal distribution raw moments, seed = %#llx\n", seed);
    double moment_zig[MOMENTS] = { 0 };
    double moment_bm[MOMENTS] = { 0 };

    printf("Box Muller method, drawing %d samples...\n", max_iter);
    cmb_random_init(seed);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double xi = smi_normal_bm(m, s);
        double xij = xi;
        for (int j = 0; j < MOMENTS; j++) {
            moment_bm[j] += xij;
            xij *= xi;
        }
    }

    printf("Ziggurat method, drawing %d samples...\n", max_iter);
    cmb_random_init(seed);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double xz = cmb_random_normal(m, s);
        double xzj = xz;
        for (int j = 0; j < MOMENTS; j++) {
            moment_zig[j] += xzj;
            xzj *= xz;
        }
    }

    printf("\nMoment\tExpected\tBox-Muller\tb-m err\t\tziggurat\tzig err\n");
    for (int i = 0; i < MOMENTS; i++) {
        double expmom = smi_normal_raw_moment(i + 1, m, s);
        double avgmom_zig = moment_zig[i] / max_iter;
        double avgmom_bm = moment_bm[i] / max_iter;
        printf("%d\t%8g\t%8g\t%8.4g %%\t%8g\t%8.4g %%\n", i + 1,
               expmom, avgmom_bm, 100.0 * (avgmom_bm - expmom)/expmom,
               avgmom_zig, 100.0 * (avgmom_zig - expmom)/expmom);
    }
}

static void test_speed_normal(double m, double s) {
    uint64_t seed = create_seed();
    printf("\nTesting normal distribution speed, seed = %#llx\n", seed);
    cmb_random_init(seed);
    printf("Box Muller method, drawing %d samples...", max_iter);

    clock_t csi = clock();
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        (void)smi_normal_bm(m, s);
    }
    clock_t cei = clock();
    double ti = (double)(cei - csi) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", max_iter / ti);

    cmb_random_init(seed);
    printf("Ziggurat method, drawing %d samples...", max_iter);
    clock_t csz = clock();
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        (void)cmb_random_normal(m, s);
    }
    clock_t cez = clock();
    double tz = (double)(cez - csz) / CLOCKS_PER_SEC;
    printf("\t%.3e samples per second\n", max_iter / tz);

    printf("Speedup for ziggurat vs Box Muller method %.1fx, %4.1f %% less time per sample\n",
           ti / tz, 100.0 * (ti - tz) / ti);
}

static void test_quality_lognormal(double m, double s) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing log-normal distribution, m %g, s %g, seed = %#llx.\n", m, s, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_lognormal(m, s);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n",
           exp(m + 0.5 * s * s), sqrt((exp(s*s) - 1.0) * exp(2 * m + s * s)));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_logistic(double m, double s) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing logistic distribution, m %g, s %g, seed = %#llx.\n", m, s, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_logistic(m, s);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", m, sqrt(s * s * M_PI * M_PI / 3.0));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_cauchy(double m, double s) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing cauchy distribution, m %g, s %g, seed = %#llx.\n", m, s, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_cauchy(m, s);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg N/A, std dev N/A\n");
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_gamma(double shape, double scale) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing gamma distribution, shape %g, scale %g, seed = %#llx.\n", shape, scale, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_gamma(shape, scale);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", shape * scale, sqrt(shape * scale * scale));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_pareto(double a, double b) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing pareto distribution, shape %g, scale %g, seed = %#llx.\n", a, b, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_pareto(a, b);
        cmb_dataset_add(dsp, x);
    }

    double mean = (a > 1) ? (a * b / (a - 1.0)) : HUGE_VAL;
    double variance = (a > 2) ? ((a * b * b) / ((a - 1.0) * (a - 1.0) * (a - 2.0))) : HUGE_VAL;
    printf("Expected values: avg %g, std dev %g\n", mean, sqrt(variance));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_beta(double a, double b, double l, double r) {
    uint64_t seed = create_seed();
    printf("\nQuality testing beta distribution, shape %g, scale %g, left %g, right %g, seed = %#llx.\n",
           a, b, l, r, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_beta(a, b, l, r);
        cmb_dataset_add(dsp, x);
    }

    double m = l + (r - l) * (a / (a + b));
    double v = ((r - l) * (r - l) * (a * b)) / ((a + b) * (a + b) * (a + b + 1));
    printf("Expected values: avg %g, std dev %g\n", m, sqrt(v));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_PERT(double left, double mode, double right) {
    uint64_t seed = create_seed();
    printf("\nQuality testing PERT distribution, left %g, mode %g, right %g, seed = %#llx.\n",
           left, mode, right, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_PERT(left, mode, right);
        cmb_dataset_add(dsp, x);
    }

    double m = (left + 4.0 * mode + right) / 6.0;
    double v = (m - left) * (right - m) / 7.0;
    printf("Expected values: avg %g, std dev %g\n", m, sqrt(v));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_chisquare(double v) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing chisquare distribution, v %g, seed = %#llx.\n", v, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_chisquare(v);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", v, sqrt(2.0 * v));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_f_dist(double a, double b) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing f distribution, a %g, b %g, seed = %#llx.\n", a, b, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_f_dist(a, b);
        cmb_dataset_add(dsp, x);
    }

    double mean = (b > 2.0) ? b / (b - 2.0) : HUGE_VAL;
    double variance = (b > 4.0) ? (2.0 * (b * b) * (a + b - 2.0)) / (a * (b - 2) * (b - 2) * (b - 4.0)) : HUGE_VAL;
    printf("Expected values: avg %g, std dev %g\n", mean, sqrt(variance));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_std_t_dist(double v) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing Student's t distribution, v %g, seed = %#llx.\n", v, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_std_t_dist(v);
        cmb_dataset_add(dsp, x);
    }

    double mean = (v > 1.0) ? 0.0 : HUGE_VAL;
    double variance = (v > 2.0) ? (v / (v - 2)) : HUGE_VAL;
    printf("Expected values: avg %g, std dev %g\n", mean, sqrt(variance));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_t_dist(double m, double s, double v) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing t distribution, m %g, s %g, v %g, seed = %#llx.\n", m, s, v, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = cmb_random_t_dist(m, s, v);
        cmb_dataset_add(dsp, x);
    }

    double mean = (v > 1.0) ? m : HUGE_VAL;
    double variance = (v > 2.0) ? (s * s * v) / (v - 2.0) : HUGE_VAL;
    printf("Expected values: avg %g, std dev %g\n", mean, sqrt(variance));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_geometric(double p) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing geometric distribution, p = %g, seed = %#llx.\n", p, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_geometric(p);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", 1.0/p, sqrt((1.0 - p) / (p * p)));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_binomial(unsigned n, double p) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing binomial distribution, n = %d, p = %g, seed = %#llx.\n", n, p, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_binomial(n, p);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", n * p, sqrt(n * p * (1.0 - p)));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_pascal(unsigned m, double p) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing negative binomial (pascal) distribution, m = %d, p = %g, seed = %#llx.\n", m, p, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_pascal(m, p);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", m * (1.0 - p) / p, sqrt(m * (1.0 - p) / (p * p)));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_poisson(double r) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing poisson distribution, r = %g, seed = %#llx.\n", r, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_poisson(r);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", r, sqrt(r));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_dice(long a, long b) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing dice (discrete uniform) distribution, a = %ld, b = %ld, seed = %#llx.\n", a, b, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_dice(a, b);
        cmb_dataset_add(dsp, x);
    }

    printf("Expected values: avg %g, std dev %g\n", (double)(a + b) / 2.0,
           sqrt(((b - a + 1) * (b - a + 1) - 1.0) / 12.0));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_loaded_dice(long n, double p_arr[n]) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing loaded dice distribution, n = %ld, seed = %#llx.\n", n, seed);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        double x = (double)cmb_random_loaded_dice(n, p_arr);
        cmb_dataset_add(dsp, x);
    }

    double mean = 0.0;
    double variance = 0.0;
    for (unsigned i = 0; i < n; i++) {
        mean += i * p_arr[i];
    }
    for (unsigned i = 0; i < n; i++) {
        variance += (i - mean) * (i - mean) * p_arr[i];
    }

    printf("Expected values: avg %g, std dev %g\n", mean, sqrt(variance));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);
    cmb_dataset_destroy(dsp);
}

static void test_quality_vose_alias(long n, double p_arr[n]) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nQuality testing vose alias sampling, n = %ld, seed = %#llx.\n", n, seed);
    struct cmb_random_alias *alp = cmb_random_alias_create(n, p_arr);
    struct cmb_dataset *dsp = cmb_dataset_create(2048);
    printf("Drawing %d samples....\n", max_iter);
    for (uint32_t ui = 0; ui < max_iter; ui++) {
        unsigned u = cmb_random_alias_sample(alp);
        double x = (double)u;
        cmb_dataset_add(dsp, x);
    }

    double mean = 0.0;
    double variance = 0.0;
    for (unsigned i = 0; i < n; i++) {
        mean += i * p_arr[i];
    }
    for (unsigned i = 0; i < n; i++) {
        variance += (i - mean) * (i - mean) * p_arr[i];
    }

    printf("Expected values: avg %g, std dev %g\n", mean, sqrt(variance));
    cmb_dataset_print_summary(stdout, dsp, true);
    cmb_dataset_print_histogram(stdout, dsp, 25, 0.0, 0.0);

    cmb_random_alias_destroy(alp);
    cmb_dataset_destroy(dsp);
}

static void test_speed_vose_alias(unsigned init, unsigned end, unsigned step) {
    uint64_t seed = create_seed();
    cmb_random_init(seed);
    printf("\nSpeed testing vose alias sampling, %d samples, seed = %#llx.\n", max_iter, seed);
    printf("n\tips simple\tips alias\tspeedup\n");
    unsigned last_neg = 0;
    double last_neg_val = 0.0;
    unsigned first_pos = 0;
    double first_pos_val = 0.0;
    for (unsigned n = init; n <= end; n += step) {
        double *p_arr = cmi_calloc(n, sizeof *p_arr);
        double sum = 0.0;
        for (unsigned i = 0; i < n; i++) {
            p_arr[i] = cmb_random();
            sum += p_arr[i];
        }

        for (unsigned i = 0; i < n; i++)
            p_arr[i] /= sum;

        clock_t cs_simple = clock();
        for (unsigned i = 0; i < max_iter; i++)
            (void)cmb_random_loaded_dice(n, p_arr);
        clock_t ce_simple = clock();

        clock_t cs_alias = clock();
        struct cmb_random_alias *alp = cmb_random_alias_create(n, p_arr);
        for (unsigned i = 0; i < max_iter; i++)
            (void)cmb_random_alias_sample(alp);
        cmb_random_alias_destroy(alp);
        clock_t ce_alias = clock();

        double t_simple = (double)(ce_simple - cs_simple) / CLOCKS_PER_SEC;
        double ips_simple = max_iter / t_simple;
        double t_alias = (double)(ce_alias - cs_alias) / CLOCKS_PER_SEC;
        double ips_alias = max_iter / t_alias;
        double speedup = (ips_alias - ips_simple) / ips_simple;
        if (speedup < 0.0) {
            last_neg = n;
            last_neg_val = speedup;
        }
        else if (first_pos == 0) {
            first_pos = n;
            first_pos_val = speedup;
        }
        printf("%d\t%9.4g\t%9.4g\t%+5.2g%%\n", n, ips_simple, ips_alias, 100.0 * speedup);
    }
}
#endif

int main(void) {
    printf("************** Testing random number generators and distributions **************\n");
    test_quality_random();
#if 0
    test_quality_uniform(-1.0, 2.0);

    test_triangular(-1.0, 2.0, 3.0);

    test_quality_exponential(2.0);
    test_speed_exponential(2.0);
    test_quality_weibull(2.0, 3.0);
    test_quality_erlang(5, 1.0);
    test_quality_gamma(3.0, 0.5);
    test_quality_gamma(0.5, 2.0);

    double m[4] = { 1.0, 2.0, 4.0, 8.0 };
    test_quality_hypoexponential(4, m);
    double p[4] = { 0.1, 0.2, 0.3, 0.4 };
    test_quality_hyperexponential(4, m, p);

    test_quality_normal(2.0, 2.0);
    test_speed_normal(1.5, 2.0);
    test_quality_lognormal(1.0, 0.5);
    test_quality_logistic(1.0, 0.5);
    test_quality_cauchy(1.0, 0.5);

    test_quality_beta(2.0, 5.0, 0.0, 1.0);
    test_quality_beta(0.5, 2.0, 0.0, 1.0);
    test_quality_beta(0.5, 0.5, 2.0, 5.0);
    test_quality_PERT(2.0, 5.0, 10.0);
    test_quality_pareto(3.0, 2.0);

    test_quality_chisquare(4);
    test_quality_f_dist(3.0, 5.0);
    test_quality_std_t_dist(3.0);
    test_quality_t_dist(1.0, 2.0, 3.0);

    test_quality_bernoulli(0.6);
    test_quality_geometric(0.1);
    test_quality_binomial(100, 0.1);
    test_quality_pascal(10, 0.1);
    test_quality_poisson(100.0);

    test_quality_dice(1, 6);

    double q[7] = { 0.05, 0.05, 0.1, 0.1, 0.2, 0.2, 0.3 };
    test_quality_loaded_dice(7, q);
    test_quality_vose_alias(7, q);
    test_speed_vose_alias(5, 50, 5);
#endif
    return 0;
}
