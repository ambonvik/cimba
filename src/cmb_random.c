/*
 * cmb_random.c - pseudo-random number generators and distributions
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
 *
 * The normal and exponential distributions below are based on code at
 *      https://github.com/cd-mcfarland/fast_prng
 * Copyright (c) Chris D McFarland 2025. Used with permission by author.
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

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "cmb_config.h"
#include "cmb_logger.h"
#include "cmb_random.h"
#include "cmi_memutils.h"

/* Declarations of inlined functions from cmb_random.h */
extern double cmb_random(void);
extern double cmb_random_uniform(double a, double b);
extern double cmb_random_std_exponential(void);
extern double cmb_random_exponential(double mean);
extern double cmb_random_erlang(unsigned k, double m);
extern double cmb_random_hypoexponential(unsigned n, const double ma[n]);
extern double cmb_random_std_normal(void);
extern double cmb_random_normal(double mu, double sigma);
extern double cmb_random_gamma(double shape, double scale);
extern double cmb_random_lognormal(double m, double s);
extern double cmb_random_logistic(double m, double s);
extern double cmb_random_cauchy(double mode, double scale);
extern double cmb_random_PERT(double l, double m, double r);
extern double cmb_random_weibull(double shape, double scale);
extern double cmb_random_pareto(double shape, double mode);
extern double cmb_random_chisquare(double k);
extern double cmb_random_std_t_dist(double v);
extern double cmb_random_t_dist(double m, double s, double v);
extern double cmb_random_f_dist(double a, double b);
extern unsigned cmb_random_bernoulli(double p);
extern double cmb_random_std_beta(double a, double b);
extern double cmb_random_beta(double a, double b, double l, double r);
extern unsigned cmb_random_pascal(unsigned m, double p);
extern long cmb_random_dice(long a, long b);
extern unsigned cmb_random_alias_sample(const struct cmb_random_alias *pa);

/* Thread-local pseudo-random generator state, i.e. each thread has its own
 * instance, but all coroutines within the thread share from the same stream
 * of numbers. Hence, multiple replications can run as separate threads in
 * the same program for coarse-grained parallelism on a multicore CPU.
 * Opaque struct, no user-serviceable parts inside except through defined API.
 */
#define DUMMY_SEED 0x0000DEAD5EED0000

static CMB_THREAD_LOCAL struct {
    uint64_t a, b, c, d;
} prng_state = { DUMMY_SEED, DUMMY_SEED, DUMMY_SEED, DUMMY_SEED };

/* Main pseudo-random number generator - 64 bits output, 256 bits state.
 * An implementation of Chris Doty-Humphrey's sfc64. Fast and high quality.
 * Public domain, see https://pracrand.sourceforge.net
 */
uint64_t cmb_random_sfc64(void) {
    const uint64_t tmp = prng_state.a + prng_state.b + prng_state.d++;
    prng_state.a = prng_state.b ^ (prng_state.b >> 11);
    prng_state.b = prng_state.c + (prng_state.c << 3);
    prng_state.c = ((prng_state.c << 24) | (prng_state.c >> 40)) + tmp;

    return tmp;
}

/* Auxiliary pseudo-random number generator - 64 bits output, 64 bits state.
 * Only used internally to bootstrap the sfc64 generator state from a single
 * seed. It is an implementation of Sebastiano Vigna & Guy Steele's splitmix64.
 * Public domain, see
 *    https://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64#C
 */
static CMB_THREAD_LOCAL uint64_t splitmix_state = DUMMY_SEED;

static void splitmix_init(const uint64_t seed) {
    splitmix_state = seed;
}

static uint64_t splitmix64(void) {
	uint64_t z = (splitmix_state += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;

	return z ^ (z >> 31);
}

/* Initializer for pseudo-random number state.
 * Bootstraps one 64-bit seed to 256 bits of state using cmb_random_splitmix()
 * Intentionally randomizes the counter (.d) to start at random place in cycle.
 * Pulls a few samples from the generator to get rid of any initial transient.
 */
void cmb_random_init(uint64_t seed) {
    splitmix_init(seed);
    prng_state.a = splitmix64();
    prng_state.b = splitmix64();
    prng_state.c = splitmix64();
    prng_state.d = splitmix64();

    for (int i = 0; i < 20; i++) {
        (void)cmb_random_sfc64();
    }
}

/* Triangular distribution */
double cmb_random_triangular(const double left,
                             const double mode,
                             const double right) {
    cmb_assert_release(left <= mode);
    cmb_assert_release(mode <= right);

    const double u = cmb_random();

    double x;
    if ((u < (mode - left) / (right - left))) {
        x = (left + sqrt(u * (right - left) * (mode - left)));
    }
    else {
        x = (right - sqrt((1.0 - u) * (right- left) * (right - mode)));
    }

    cmb_assert_debug((x >= left) && (x <= right));
    return x;
}

/* Modified PERT distribution */
 double cmb_random_PERT_mod(const double left,
                            const double mode,
                            const double right,
                            const double lambda) {
    cmb_assert_release(left < mode);
    cmb_assert_release(mode < right);
    cmb_assert_release(lambda > 0.0);

    const double rng = right - left;
    const double a = 1.0 + lambda * (mode - left) / rng;
    const double b = 1.0 + lambda * (right - mode) / rng;
    const double x = left + rng * cmb_random_std_beta(a, b);

    cmb_assert_debug((x >= left) && ( x <= right));
    return x;
}

/*
 * Exponential distribution, fast ziggurat method.
 *
 * Basically, this is a rejection sampling algorithm. We want to generate a
 * random variable X along one dimension. We lay out the probability density
 * function along the axis and sample two random variables with known
 * distributions, one a candidate X and one independent Y.
 * If the point (X, Y) is inside the pdf, we accept and return the candidate X,
 * otherwise we reject it and try again. The result is a set of accepted
 * X-values converging exactly to the pdf.
 *
 * The ziggurat method adds some extra cleverness by pre-computing areas that
 * will always be accepted with less need for sample generation and/or for
 * computing the transcendental functions of the pdf, both likely to be
 * computationally expensive.
 *
 * This code implements a further optimized algorithm by Chris McFarland. It
 * covers the pdf outside the ziggurat in a tight set of right triangles and
 * rejection samples along the edge only, minimizing the need to calculate the
 * exact pdf.
 * This works correctly because it uses the exact probability for selecting this
 * overhang, and we can then isolate the rejection sampling to this overhang
 * only without worrying about the overall distribution of (X, Y) values over
 * the pdf.
 *
 * Our implementation also pre-computes a concavity value for each overhang for
 * an even tighter squeeze on the pdf. We found this to give a modest
 * performance improvement. It also uses the memoryless property of the
 * exponential distribution to generate tail values by iteration instead of
 * recursion, to conserve stack space in our coroutine context. The "hot path"
 * is inlined in cmb_random.h, while cmi_random_exp_not_hot() is called if that
 * does not succeed.
 *
 * Overall, it is 2.5 - 3 times faster than the inversion method, taking the \
 * direct path in about 98.5 % of the samples, on average consuming 1.03 64-bit
 * random numbers per sample. It only needs to calculate the exact pdf in about
 * 0.04 % of the cases. It is the fastest known method, which is why it is here.
 *
 * See also:
 *      https://arxiv.org/pdf/1403.6870
 *      https://github.com/cd-mcfarland/fast_prng
 *      https://en.wikipedia.org/wiki/Ziggurat_algorithm
 *
 * For a clear explanation of the alias sampling method used here, see:
 *      https://www.keithschwarz.com/darts-dice-coins/
 */

/* We do as many calculations as possible in unsigned integer for speed.
 * Helper functions to map uint64_t values to doubles on the correct scale.
 */
static double zig_exp_convert_x(const double *dpx, const uint64_t u) {
    return ldexp((*dpx), 64) + (*(dpx - 1) - *dpx) * (double)u;
}

static inline double zig_exp_convert_y(const double *dpy, const uint64_t u) {
    return ldexp(*(dpy - 1), 64) + (*dpy - *(dpy - 1)) * (double)u;
}

/* #include the lookup tables to avoid cluttering up this code */
#include "cmi_random_exp_zig.inc"

/* Fallback sampling function, called in about 1,5 % of cases */
double cmi_random_exp_not_hot(uint64_t u_cand_x) {
    /* Offset for tail sample generation, implemented as iteration */
    double x_offset = 0.0;
    for (;;) {
        /* We are in one of the leftover pieces, alias sample for which one. */
        uint64_t u_cand_y = cmb_random_sfc64();
        uint8_t jdx = u_cand_y & 0xff;
        const bool aliased = (cmb_random_sfc64() >= exp_zig_u_prob[jdx]);
        jdx = (aliased) ? exp_zig_alias[jdx] : jdx;
        if (jdx > 0) {
            /* Not in tail, rejection sample from within this right triangular
             * overhang only.
             */
            for (;;) {
                /* First time through we still have 56 bits of unused randomness
                 * in u_cand_x, now re-interpreted as an X value along the base
                 * of the sampled overhang triangle. Make sure the current X, Y
                 * pair belongs to the triangle, reflecting if necessary.
                 */
                if (u_cand_y > (UINT64_MAX - u_cand_x)) {
                    u_cand_y = UINT64_MAX - u_cand_y;
                    u_cand_x = UINT64_MAX - u_cand_x;
                }

                /* Are we far enough from the pdf to avoid calculating it? */
                uint64_t u_dist = (UINT64_MAX - u_cand_x) - u_cand_y;
                if (u_dist >= exp_zig_u_concavity[jdx]) {
                    /* Surely inside, scale and return the candidate X value */
                    const double *dpx = &(cmi_random_exp_zig_pdf_x[jdx]);
                    const double x = zig_exp_convert_x(dpx, u_cand_x);
                    return x + x_offset;
                }
                else {
                    /* Maybe inside, do exact pdf calculation to decide */
                    const double *dpx = &(cmi_random_exp_zig_pdf_x[jdx]);
                    const double x = zig_exp_convert_x(dpx, u_cand_x);
                    const double *dpy = &(cmi_random_exp_zig_pdf_y[jdx]);
                    const double y = zig_exp_convert_y(dpy, u_cand_y);
                    if (y <= exp(-x)) {
                        /* Indeed inside */
                        return x + x_offset;
                    }
                }

                /* No joy, try another X, Y pair in this overhang */
                u_cand_y = cmb_random_sfc64();
                u_cand_x = cmb_random_sfc64();
            }
        }
        else {
            /* In the tail, right-shift and try again */
            x_offset += exp_zig_x_tail_start;
        }

        /* Generate a new candidate x-value */
        u_cand_x = cmb_random_sfc64();
        const uint8_t idx = u_cand_x & 0xff;
        /* Re-try the hot path before looping back to the top */
        if (idx <= cmi_random_exp_zig_max) {
            /* Lucky path: Candidate X value is in ziggurat,
             * scale to length of layer idx and return */
            return cmi_random_exp_zig_pdf_x[idx]
                   * (double) u_cand_x + x_offset;
        }
    }

    /* Not reached */
    cmb_assert_debug(0);
}

/*
 * Hyperexponential on [0, oo), choosing and samples one of n exponential
 * distributions. The probability of selecting distribution i is p_arr[i],
 * the mean of that distribution is m_arr[i].
 * The overall mean is the sum of p_arr[i] * m_arr[i], the variance a more
 * complicated sum of terms, see
 * https://en.wikipedia.org/wiki/Hyperexponential_distribution
 *
 * Assumes that p_arr sums to 1.0. Uses a simple O(n) implementation.
 * If n is large and speed is important, consider using O(1) alias sampling to
 * select the distribution instead of using this function.
 */
double cmb_random_hyperexponential(const unsigned n,
                                   const double ma[n],
                                   const double pa[n]) {
    cmb_assert_release(n > 0u);
    cmb_assert_release(ma != NULL);
    cmb_assert_release(pa != NULL);

    const unsigned ui = cmb_random_loaded_dice(n, pa);
    cmb_assert_debug(ui < n);
    const double x = cmb_random_exponential(ma[ui]);

    cmb_assert_debug(x >= 0.0);
    return x;
}

/*
 * Normal distribution, fast Ziggurat method.
 *
 * Optimized algorithm from Chris McFarland, same source and method as
 * described for cmb_random_exponential_zig() above, except that the normal pdf
 * is partly convex and partly concave, giving additional cases for rejection
 * sampling.
 */

/* Helper functions to map int64_t values to doubles on the correct scale */
static double zig_nor_convert_x(const double *dpx, const int64_t ix) {
    return ldexp((*dpx), 63) + (*(dpx - 1) - *dpx) * (double)ix;
}

static double zig_nor_convert_y(const double *dpy, const uint64_t uy) {
    return ldexp(*(dpy - 1), 63) + (*dpy - *(dpy - 1)) * (double)uy;
}

/* Pull 64 bits of randomness, convert to signed and clear the sign bit */
static int64_t zig_sample63(void) {
    uint64_t bits = cmb_random_sfc64();
    return (*(int64_t *)&bits) & INT64_MAX;
}

/* pdf pre-scaled by sqrt (2 * M_PI) to avoid recalculating constant */
static inline double sc_nor_pdf(const double x) {
    return exp(-0.5 * x * x);
}

/* #include the lookup tables to avoid cluttering up this code */
#include "cmi_random_nor_zig.inc"

/* The actual normal distribution sampling function */
double cmi_random_nor_not_hot(int64_t i_cand_x) {
    /* Save the sign bit for later use and clear it */
    double sign = ((i_cand_x >> 63) ? -1.0 : 1.0);
    i_cand_x &= INT64_MAX;

    /* Alias sample to find out which overhang area */
    int64_t i_cand_y = zig_sample63();
    uint8_t jdx = i_cand_y & 0xff;
    jdx = (i_cand_x >= nor_zig_i_prob[jdx]) ? nor_zig_alias[jdx] : jdx;
    if (jdx > nor_zig_inflection) {
        /* Convex overhang */
        for (;;) {
            const double *dpx = &(cmi_random_nor_zig_pdf_x[jdx]);
            const double x = zig_nor_convert_x(dpx, i_cand_x);
            const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
            if (i_dist >= 0) {
                /* Surely inside */
                return sign * x;
            }
            else if (i_dist + nor_zig_i_convexity[jdx] >= 0) {
                /* Maybe inside, calculate pdf for precise rejection sampling */
                const double *dpy = &(cmi_random_nor_zig_pdf_y[jdx]);
                const double y = zig_nor_convert_y(dpy, i_cand_y);
                if (y < sc_nor_pdf(x)) {
                    return sign * x;
                }
            }

            /* Try again, draw another sample from this overhang */
            i_cand_x = zig_sample63();
            i_cand_y = zig_sample63();
        }
    }
    else if (jdx == 0) {
        /* Tail, rejection sample by exponential.
         * See Marsaglia or the wikipedia article. */
        double x, z;
        do {
            x = nor_zig_inv_tail_start * cmb_random_exponential(1.0);
            z = cmb_random_exponential(1.0);
        } while (2 * z <= x * x);
        return sign * (x + nor_zig_x_tail_start);
    }
    else if (jdx < nor_zig_inflection) {
        /* Concave overhang, similar to exponential. */
        for (;;) {
            if (i_cand_y > INT64_MAX - i_cand_x) {
                i_cand_y = INT64_MAX - i_cand_y;
                i_cand_x = INT64_MAX - i_cand_x;
            }

            /* Are we sufficiently far from the pdf to avoid calculating it? */
            const double *dpx = &(cmi_random_nor_zig_pdf_x[jdx]);
            const double x = zig_nor_convert_x(dpx, i_cand_x);
            const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
            if (i_dist >= nor_zig_i_concavity[jdx]) {
                return sign * x;
            }
            else {
                /* Maybe inside, need to do exact pdf calculation to decide */
                const double *dpy = &(cmi_random_nor_zig_pdf_y[jdx]);
                const double y = zig_nor_convert_y(dpy, i_cand_y);
                if (y <= sc_nor_pdf(x)) {
                    return sign * x;
                }
            }

            /* Try again, draw another sample */
            i_cand_x = zig_sample63();
            i_cand_y = zig_sample63();
       }
    }
    else {
        /* At the inflection point */
        cmb_assert(jdx == nor_zig_inflection);
        for (;;) {
            const double *dpx = &(cmi_random_nor_zig_pdf_x[jdx]);
            const double x = zig_nor_convert_x(dpx, i_cand_x);
            const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
            if (i_dist >= nor_zig_i_concavity[jdx]) {
                return sign * x;
            }
            else if (i_dist + nor_zig_i_convexity[jdx] > 0) {
                const double *dpy = &(cmi_random_nor_zig_pdf_y[jdx]);
                const double y = zig_nor_convert_y(dpy, i_cand_y);
                if (y < sc_nor_pdf(x)) {
                    return sign * x;
                }
            }

            /* Try again, draw another sample */
            i_cand_x = zig_sample63();
            i_cand_y = zig_sample63();
        }
    }

    /* Not reached */
    cmb_assert(0);
}

/*
 * Gamma distribution
 *
 * Rejection sampling with an easy-to-check squeeze underneath the pdf.
 * In principle similar to the ziggurat method, except that the covering
 * function is a power of a normal distribution, and that the squeezing
 * function underneath the pdf is a continuous function instead of a ziggurat.
 *
 * See:
 *   Marsaglia & Tsang (2000): "A Simple Method for Generating Gamma Variables",
 *   https://dl.acm.org/doi/10.1145/358407.358414
 */
double cmb_random_std_gamma(const double shape) {
    cmb_assert_release(shape > 0.0);

    static CMB_THREAD_LOCAL double a_prev = 0.0;
    static CMB_THREAD_LOCAL double c = 0.0;
    static CMB_THREAD_LOCAL double d = 0.0;
    if (shape != a_prev) {
        d = shape - 1.0 / 3.0;
        c = 1.0 / sqrt(9.0 * d);
        a_prev = shape;
    }

    double x, v;
    for (;;) {
        do {
            x = cmb_random_std_normal();
            v = 1.0 + c * x;
        } while (v <= 0.0);

        double w = v * v * v;
        double u = cmb_random();
        if ((u < 1.0 - 0.331 * (x * x) * (x * x))
            || (log(u) < (0.5 * x * x) + (d * (1.0 - w + log(w))))) {
            const double ret = d * w;
            cmb_assert_debug(ret >= 0.0);
            return ret;
        }
    }

    /* not reached */
    cmb_assert_debug(0);
}

/* Simple flip of a fair unbiased coin, caching bits for efficiency */
int cmb_random_flip(void) {
    static CMB_THREAD_LOCAL uint64_t bits;
    static CMB_THREAD_LOCAL uint8_t bitpos = 0;

    if (bitpos == 0) {
        bits = cmb_random_sfc64();
        bitpos = 64;
    }

    return ((bits >> --bitpos) & 1) ? 1 : 0;
}

/*
 * Geometric distribution, the number of trials until
 * and including the first success.
 *
 */
unsigned cmb_random_geometric(const double p) {
    cmb_assert((p > 0.0) && (p <= 1.0));

    static CMB_THREAD_LOCAL double prev = 0.0;
    static CMB_THREAD_LOCAL double denom = 0.0;
    if (p != prev) {
        denom = -log(1.0 - p);
    }

    unsigned x = (unsigned)ceil(cmb_random_std_exponential() / denom);

    cmb_assert_debug(x >= 1u);
    return x;
}

/* Binomial distribution, the number of successes in n trials */
unsigned cmb_random_binomial(const unsigned n, const double p) {
    cmb_assert_release(n > 0);
    cmb_assert_release((p > 0.0) && (p <= 1.0));

    unsigned sctr = 0;
    for (unsigned ui = 0u; ui < n; ui++) {
        sctr += cmb_random_bernoulli(p);
    }

    cmb_assert_debug(sctr <= n);
    return sctr;
}

/*
 * Negative binomial distribution, number of failures until m'th success,
 * where p > 0 is the probability of success in each trial.
 */
unsigned cmb_random_negative_binomial(const unsigned m, const double p) {
    cmb_assert_release(m > 0);
    cmb_assert((p > 0.0) && (p <= 1.0));

    unsigned fctr = 0;
    for (unsigned ui = 0u; ui < m; ui++) {
        fctr += cmb_random_geometric(p);
    }

    return fctr;
}

/*
 * Poisson distribution, number of arrivals with rate r in unit time,
 * using our fast exponential distribution to simulate it arrival by arrival.
 */
unsigned cmb_random_poisson(const double r) {
    cmb_assert_release(r > 0.0);

    const double m = 1.0 / r;
    double t = 0.0;
    unsigned ctr = 0;
    for (;;) {
        t += cmb_random_exponential(m);
        if (t <= 1.0) {
            /* Still within time window */
            ctr++;
        }
        else {
            /* Unit time elapsed */
            break;
        }
    }

    return ctr;
}

/*
 * Non-uniform discrete distribution, simple cdf inversion method.
 */
#ifndef NASSERT
static double sum_tolerance = 1.0e-3;
static bool sums_to_one(const unsigned n, const double p[n]) {
    double sum = 0.0;
    for (unsigned ui = 0u; ui < n; ui++) {
        sum += p[ui];
    }

    return (fabs(sum - 1.0) <= sum_tolerance) ? true : false;
}
#endif /* ifndef NASSERT */

unsigned cmb_random_loaded_dice(const unsigned n, const double pa[n]) {
    cmb_assert_release(n > 0);
    cmb_assert_release(pa != NULL);
    cmb_assert_release(sums_to_one(n, pa));

    const double x = cmb_random();
    double q = 0.0;
    unsigned ui;
    for (ui = 0; ui < n; ui++) {
        q += pa[ui];
        if (x < q) {
            break;
        }
    }

    cmb_assert_debug(ui < n);
    return ui;
}

/*
 * Non-uniform discrete distribution, efficient Vose alias sampling method.
 * Three-stage process:
 * 1. Call cmb_random_alias_create once to create lookup table before sampling.
 * 2. Sample as needed with cmb_random_alias_sample from the created table.
 * 3. Call cmb_random_alias_destroy when done to deallocate lookup table.
 */

/* Helper function to make sure the table index never wraps around */
static inline uint64_t cmi_random_alias_secure(const double p) {
    uint64_t ur;
    if (p <= 0.0) {
        ur = 0;
    }
    else if (p >= 1.0) {
        ur = UINT64_MAX;
    }
    else {
        ur = (uint64_t)(p * (double)UINT64_MAX);
    }

    return ur;
}

/* Create alias lookup table before sampling */
struct cmb_random_alias *cmb_random_alias_create(const unsigned n,
                                                 const double pa[n]) {
    cmb_assert_release(n > 0);
    cmb_assert_release(sums_to_one(n, pa));

    struct cmb_random_alias *alp = NULL;
    double *work = cmi_calloc(n, sizeof(double));
    double psum = 0.0;
    for (unsigned ai = 0; ai < n; ai++) {
        psum += pa[ai];
    }
    cmb_assert_debug(fabs(psum - 1.0) <= sum_tolerance);

    unsigned *small = cmi_calloc(n, sizeof(unsigned));
    unsigned *large = cmi_calloc(n, sizeof(unsigned));
    unsigned idxs = 0;
    unsigned idxl = 0;
    for (unsigned ui = 0; ui < n; ui++) {
        work[ui] = pa[ui] * n  / psum;
        if (work[ui] < 1.0) {
            small[idxs++] = ui;
        }
        else {
            large[idxl++] = ui;
        }
    }

    alp = cmi_malloc(sizeof *alp);
    alp->n = n;
    alp->uprob = cmi_calloc(n, sizeof(uint64_t));
    alp->alias = cmi_calloc(n, sizeof(unsigned));

    while ((idxs > 0) && (idxl > 0)) {
        const unsigned l = small[--idxs];
        const unsigned g = large[--idxl];
        alp->uprob[l] = cmi_random_alias_secure(work[l]);
        alp->alias[l] = g;
        work[g] = (work[g] + work[l]) - 1.0;
        if (work[g] < 1.0) {
            small[idxs++] = g;
        }
        else {
            large[idxl++] = g;
        }
    }

    while (idxl > 0) {
        unsigned g = large[--idxl];
        alp->uprob[g] = UINT64_MAX;
    }

    while (idxs > 0) {
        unsigned l = small[--idxs];
        alp->uprob[l] = UINT64_MAX;
    }

    cmi_free(large);
    cmi_free(small);
    cmi_free(work);

    return alp;
}

/* Deallocate the alias lookup table when done sampling */
void cmb_random_alias_destroy(struct cmb_random_alias *pa) {
    cmb_assert_release(pa != NULL);
    cmb_assert_release(pa->uprob != NULL);
    cmb_assert_release(pa->alias != NULL);

    cmi_free(pa->uprob);
    cmi_free(pa->alias);
    cmi_free(pa);
}