/*
 * cmb_random.c - pseudo-random number generators and distributions
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-26.
 *
 * The normal and exponential distributions below are based on code at
 *      https://github.com/cd-mcfarland/fast_prng
 *      Copyright (c) Chris D McFarland 2025.
 *      Used with permission by author.
 *
* A good general reference text is
*       Devroye, L. (1986), Non-Uniform Random Variate Generation, Springer
 *      available online at https://luc.devroye.org/handbooksimulation1.pdf
 *      https://luc.devroye.org/LucDevroye-NonUniformRandomVariateGeneration-10.1007_978-1-4613-8643-8-1986.pdf
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
#include <stdio.h>

#include "cmb_logger.h"
#include "cmb_random.h"

#include "cmi_config.h"
#include "cmi_memutils.h"

/*
 * Thread-local pseudo-random generator state, i.e., each thread has its own
 * instance, but all coroutines within the thread share from the same stream
 * of numbers. Hence, multiple replications can run as separate threads in
 * the same program for coarse-grained parallelism on a multicore CPU.
 * Opaque struct, no user-serviceable parts inside except through defined API.
 */
#define DUMMY_SEED 0x0000DEAD5EED0000

static CMB_THREAD_LOCAL struct {
    uint64_t a, b, c, d;
} prng_state = { DUMMY_SEED, DUMMY_SEED, DUMMY_SEED, DUMMY_SEED };

/* Storage for the seed used in this thread */
static CMB_THREAD_LOCAL uint64_t initial_seed = DUMMY_SEED;

/* Bit-buffer for cmb_random_flip */
static CMB_THREAD_LOCAL uint64_t flip_bits = DUMMY_SEED;
static CMB_THREAD_LOCAL uint8_t flip_bitpos = 0;

/*******************************************************************************
 * Main pseudo-random number generator - 64-bit output, 256-bit state.
 * An implementation of Chris Doty-Humphrey's sfc64. Fast and high-quality.
 * Public domain, see https://pracrand.sourceforge.net
 */
uint64_t cmb_random_sfc64(void)
{
    const uint64_t tmp = prng_state.a + prng_state.b + prng_state.d++;
    prng_state.a = prng_state.b ^ (prng_state.b >> 11);
    prng_state.b = prng_state.c + (prng_state.c << 3);
    prng_state.c = ((prng_state.c << 24) | (prng_state.c >> 40)) + tmp;

    return tmp;
}

/*
 * The MurmurHash3 finalizer function, used e.g. for bootstrapping thread seeds
 * from a common master seed, e.g. from command line input.
 *
 * See: https://github.com/aappleby/smhasher/wiki/MurmurHash3
 */
uint64_t cmb_random_fmix64(const uint64_t seed, const uint64_t nonce)
{
    uint64_t h = seed + nonce;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;

    return h;
}

/*
 * Auxiliary pseudo-random number generator - 64-bit output, 64-bit state.
 * Only used internally to bootstrap the sfc64 generator state from a single
 * seed. It is an implementation of Sebastiano Vigna & Guy Steele's splitmix64.
 * Public domain, see
 *    https://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64#C
 */
static CMB_THREAD_LOCAL uint64_t splitmix_state = DUMMY_SEED;

static void splitmix_initialize(const uint64_t seed)
{
    splitmix_state = seed;
}

static uint64_t splitmix64(void)
{
    uint64_t z = (splitmix_state += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;

    return z ^ (z >> 31);
}

/*
 * Initializer for sfc64 pseudo-random number state.
 * Bootstraps one 64-bit seed to 256 bits of state using cmb_random_splitmix().
 *
 * Intentionally randomizes the counter (.d) to start at a random place in the
 * cycle. This starting point is one that is also reachable starting from
 * zero, just with an offset. That implies that all published sfc64 results in
 * PractRand and BigCrush tests carry over unchanged, and the cycle length stays
 * the same "at least 2^64". However, if each trial consumes L sfc64 samples,
 * where L << 2^64, this reduces the probability of getting overlapping sample
 * sequences between trials or threads by about 2^64 / L compared to starting
 * all counters from zero.
 *
 * Finally pulls a few samples from the generator to get rid of any initial
 * transient, and pre-loads the cmb_random_flip() buffer.
 */
void cmb_random_initialize(const uint64_t seed)
{
    initial_seed = seed;
    splitmix_initialize(seed);
    prng_state.a = splitmix64();
    prng_state.b = splitmix64();
    prng_state.c = splitmix64();
    prng_state.d = splitmix64();

    for (int i = 0; i < 20; i++) {
        (void)cmb_random_sfc64();
    }

    flip_bits = cmb_random_sfc64();
    flip_bitpos = UINT64_C(64);
}

/*
 * De-initializer, return to the newly created state. Not very useful, mostly
 * provided for syntactical symmetry with other initialize/terminate pairs.
 */
void cmb_random_terminate(void) {
    prng_state.a = DUMMY_SEED;
    prng_state.b = DUMMY_SEED;
    prng_state.c = DUMMY_SEED;
    prng_state.d = DUMMY_SEED;

    splitmix_state = DUMMY_SEED;

    flip_bits = DUMMY_SEED;
    flip_bitpos = 0u;
}

/*
 * Return the 64-bit seed that was used to initialize the generator.
 * If it returns ´0x0000DEAD5EED0000`, the generator was never initialized.
 */
uint64_t cmb_random_curseed(void)
{
    return initial_seed;
}

/*******************************************************************************
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
 * exact pdf. This works correctly because it uses the exact probability for
 * selecting this overhang, and we can then isolate the rejection sampling to
 * this overhang only without worrying about the overall distribution of (X, Y)
 * values over the pdf.
 *
 * Our implementation also pre-computes a concavity value for each overhang for
 * an even tighter squeeze on the pdf. We found this to give a modest
 * performance improvement. It also uses the memoryless property of the
 * exponential distribution to generate tail values by iteration instead of
 * recursion, to conserve stack space in our coroutine context. The "hot path"
 * is inlined in cmb_random.h, while cmi_random_exp_not_hot() is called if that
 * does not succeed.
 *
 * Overall, it is 2.5 - 3 times faster than the inversion method, taking the
 * direct path in about 98.5 % of the samples, consuming 1.03 64-bit random
 * numbers on average per sample. It only needs to calculate the exact pdf in
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

/* Helper functions to map int64_t values to doubles on the correct scale */
static double zig_convert_x(const double *dpx, const int64_t ix)
{
    /* Multiply by 2^63, equal to ldexp((*dpx), 63) but faster */
    const double x1 = (*dpx) * 9223372036854775808.0;
    const double x2 = (*(dpx - 1) - *dpx) * (double)ix;

    return x1 + x2;
}

static double zig_convert_y(const double *dpy, const int64_t iy)
{
    const double y1 = (*(dpy - 1)) * 9223372036854775808.0;
    const double y2 = (*dpy - *(dpy - 1)) * (double)iy;

    return y1 + y2;
}

/* Pull 64 bits of randomness, convert to signed and clear the sign bit
 * for 63 bits net, always positive sign */
static int64_t zig_sample63(void)
{
    const uint64_t bits = cmb_random_sfc64();
    const int64_t r = (*(int64_t *)&bits) & INT64_MAX;

    return r;
}

/* We #include the lookup tables to avoid cluttering up this code.
 * The file is built on the fly in the build script by executing programs
 * found in src/codegen */
#include "cmi_random_exp_zig.inc"

/* Fallback sampling function, called in about 1,5 % of cases.
 * Note that it uses signed integers even if exponentials are non-negative,
 * since casts between signed int and double are faster than between
 * unsigned and double. The doubles have a 53-bit mantissa anyway, so the
 * difference between 64 and 63 bit integers does not lose any precision. */
double cmi_random_exp_not_hot(int64_t i_cand_x)
{
    /* Offset for tail sample generation, implemented as iteration */
    double x_offset = 0.0;
    for (;;) {
        /* We are in one of the leftover pieces, alias sample for which one. */
        int64_t i_cand_y = zig_sample63();
        uint8_t jdx = i_cand_y & 0xff;
        jdx = (i_cand_x >= exp_zig_i_prob[jdx]) ? exp_zig_alias[jdx] : jdx;
        if (jdx > 0) {
            /* Not in tail, rejection sample from within this right triangular
             * overhang only.
             */
            for (;;) {
                /* First time through we still have 56 bits of unused randomness
                 * in i_cand_x, now re-interpreted as an X value along the base
                 * of the sampled overhang triangle. Make sure the current X, Y
                 * pair belongs to the triangle, reflecting if necessary.
                 */
                if (i_cand_y > (INT64_MAX - i_cand_x)) {
                    i_cand_y = INT64_MAX -i_cand_y;
                    i_cand_x = INT64_MAX - i_cand_x;
                }

                /* Are we far enough from the pdf to avoid calculating it? */
                const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
                if (i_dist >= exp_zig_i_concavity[jdx]) {
                    /* Surely inside, scale and return the candidate X value */
                    const double *dpx = &(cmi_random_exp_zig_pdf_x[jdx]);
                    const double x = zig_convert_x(dpx, i_cand_x);
                    return x + x_offset;
                }
                else {
                    /* Maybe inside, do exact pdf calculation to decide */
                    const double *dpx = &(cmi_random_exp_zig_pdf_x[jdx]);
                    const double x = zig_convert_x(dpx, i_cand_x);
                    const double *dpy = &(cmi_random_exp_zig_pdf_y[jdx]);
                    const double y = zig_convert_y(dpy, i_cand_y);
                    if (y <= exp(-x)) {
                        /* Safely inside */
                        return x + x_offset;
                    }
                }

                /* No joy, try another X, Y pair in this overhang */
                i_cand_y = zig_sample63();
                i_cand_x = zig_sample63();
            }
        }
        else {
            /* In the tail, right-shift and try again */
            x_offset += exp_zig_x_tail_start;
        }

        /* Generate a new candidate x-value */
        i_cand_x = zig_sample63();
        const uint8_t idx = i_cand_x & 0xff;
        /* Re-try the hot path before looping back to the top */
        if (idx <= cmi_random_exp_zig_max) {
            /* Lucky path: Candidate X value is in ziggurat,
             * scale to length of layer idx and return */
            return cmi_random_exp_zig_pdf_x[idx]
                   * (double) i_cand_x + x_offset;
        }
    }

    /* Not reached */
    cmb_assert_debug(0);
}

/*******************************************************************************
 * Erlang distribution, adds up a few exponentials for small k, calls gamma
 * for large k.
 */

double cmb_random_erlang(const unsigned k, const double m)
{
    cmb_assert_release(k > 0u);
    cmb_assert_release(m > 0.0);

    double x = 0.0;
    if (k < 10) {
        for (unsigned i = 0u; i < k; i++) {
            x += cmb_random_exponential(m);
        }
    }
    else {
        x = cmb_random_gamma((double)k, m);
    }

    cmb_assert_debug(x >= 0.0);
    return x;
}

/*******************************************************************************
 * Hyperexponential on [0, oo), choosing and samples one of n exponential
 * distributions. The probability of selecting distribution i is p_arr[i],
 * the mean of that distribution is m_arr[i].
 * The overall mean is the sum of p_arr[i] * m_arr[i].
 * The variance is a more complicated sum of terms, see
 * https://en.wikipedia.org/wiki/Hyperexponential_distribution
 *
 * Assumes that p_arr sums to 1.0. Uses a simple O(n) implementation.
 * If n is large and speed is important, consider using O(1) alias sampling to
 * select the distribution instead of using this function.
 */
double cmb_random_hyperexponential(const uint64_t n,
                                   const double ma[],
                                   const double pa[])
{
    cmb_assert_release(n > 0u);
    cmb_assert_release(ma != NULL);
    cmb_assert_release(pa != NULL);

    const uint64_t ui = cmb_random_discrete_nonuniform(n, pa);
    cmb_assert_debug(ui < n);
    const double x = cmb_random_exponential(ma[ui]);

    cmb_assert_debug(x >= 0.0);
    return x;
}

/*******************************************************************************
 * Normal distribution, fast Ziggurat method.
 *
 * Optimized algorithm from Chris McFarland, the same source and method as
 * described for cmb_random_exponential_zig() above, except that the normal pdf
 * is partly convex and partly concave, giving additional cases for rejection
 * sampling.
 */

/* pdf pre-scaled by sqrt (2 * M_PI) to avoid recalculating constant */
static inline double sc_nor_pdf(const double x)
{
    return exp(-0.5 * x * x);
}

/* #include the lookup tables to avoid cluttering up this code */
#include "cmi_random_nor_zig.inc"

/* The fallback normal distribution sampling function */
double cmi_random_nor_not_hot(int64_t i_cand_x)
{
    /* Save the sign bit for later use and clear it */
    const double sign = ((i_cand_x >> 63) ? -1.0 : 1.0);
    i_cand_x &= INT64_MAX;

    /* Alias sample to find out which overhang area */
    int64_t i_cand_y = zig_sample63();
    uint8_t jdx = i_cand_y & 0xff;
    jdx = (i_cand_x >= nor_zig_i_prob[jdx]) ? nor_zig_alias[jdx] : jdx;
    if (jdx > nor_zig_inflection) {
        /* Convex overhang */
        for (;;) {
            const double *dpx = &(cmi_random_nor_zig_pdf_x[jdx]);
            const double x = zig_convert_x(dpx, i_cand_x);
            const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
            if (i_dist >= 0) {
                /* Surely inside */
                return sign * x;
            }
            else if (i_dist + nor_zig_i_convexity[jdx] >= 0) {
                /* Maybe inside, calculate pdf for precise rejection sampling */
                const double *dpy = &(cmi_random_nor_zig_pdf_y[jdx]);
                const double y = zig_convert_y(dpy, i_cand_y);
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

            /* Are we far enough from the pdf to avoid calculating it? */
            const double *dpx = &(cmi_random_nor_zig_pdf_x[jdx]);
            const double x = zig_convert_x(dpx, i_cand_x);
            const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
            if (i_dist >= nor_zig_i_concavity[jdx]) {
                return sign * x;
            }
            else {
                /* Maybe inside, need to do exact pdf calculation to decide */
                const double *dpy = &(cmi_random_nor_zig_pdf_y[jdx]);
                const double y = zig_convert_y(dpy, i_cand_y);
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
        cmb_assert_debug(jdx == nor_zig_inflection);
        for (;;) {
            const double *dpx = &(cmi_random_nor_zig_pdf_x[jdx]);
            const double x = zig_convert_x(dpx, i_cand_x);
            const int64_t i_dist = (INT64_MAX - i_cand_x) - i_cand_y;
            if (i_dist >= nor_zig_i_concavity[jdx]) {
                return sign * x;
            }
            else if (i_dist + nor_zig_i_convexity[jdx] > 0) {
                const double *dpy = &(cmi_random_nor_zig_pdf_y[jdx]);
                const double y = zig_convert_y(dpy, i_cand_y);
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
    cmb_assert_debug(false);
}

/*******************************************************************************
 * Gamma distribution
 *
 * Rejection sampling with an easy-to-check squeeze underneath the pdf.
 * In principle similar to the ziggurat method, except that the covering
 * function is a power of a normal distribution, and that the squeezing
 * function underneath the pdf is a continuous function instead of a ziggurat.
 * Leverages our fast normal distribution for its efficiency and simplicity.
 *
 * See:
 *   Marsaglia & Tsang (2000): "A Simple Method for Generating Gamma Variables",
 *   https://dl.acm.org/doi/10.1145/358407.358414
 */
double cmb_random_std_gamma(double shape)
{
    cmb_assert_release(shape > 0.0);

    /* Cache to avoid recalculating needlessly */
    static CMB_THREAD_LOCAL double a_prev = 0.0;
    static CMB_THREAD_LOCAL double c = 0.0;
    static CMB_THREAD_LOCAL double d = 0.0;

    double mult = 1.0;
    if (shape < 1.0) {
        /* Make sure we do not accidentally produce a zero */
        const double f = 1.0 - cmb_random();
        cmb_assert_debug(f > 0.0);
        mult = pow(f, 1.0 / shape);
        shape += 1.0;
    }

    if (shape != a_prev) {
        /* Update cache */
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

        const double w = v * v * v;
        const double u = cmb_random();
        if ((u < 1.0 - 0.331 * (x * x) * (x * x))
            || (log(u) < (0.5 * x * x) + (d * (1.0 - w + log(w))))) {
            const double ret = mult * d * w;

            cmb_assert_debug(ret >= 0.0);
            return ret;
        }
    }

    /* Not reached */
}

/*******************************************************************************
 * Triangular distribution
 */
double cmb_random_triangular(const double min,
                             const double mode,
                             const double max)
{
    cmb_assert_release(min <= mode);
    cmb_assert_release(mode <= max);
    cmb_assert_release(min < max);

    const double u = cmb_random();

    double x;
    if ((u < (mode - min) / (max - min))) {
        x = (min + sqrt(u * (max - min) * (mode - min)));
    }
    else {
        x = (max - sqrt((1.0 - u) * (max- min) * (max - mode)));
    }

    cmb_assert_debug((x >= min) && (x <= max));
    return x;
}

/*******************************************************************************
 * Modified PERT distribution
 */
double cmb_random_PERT_mod(const double min,
                           const double mode,
                           const double max,
                           const double lambda)
{
    cmb_assert_release(min < mode);
    cmb_assert_release(mode < max);
    cmb_assert_release(lambda > 0.0);

    const double rng = max - min;
    const double a = 1.0 + lambda * (mode - min) / rng;
    const double b = 1.0 + lambda * (max - mode) / rng;
    const double x = min + rng * cmb_random_std_beta(a, b);

    cmb_assert_debug((x >= min) && ( x <= max));
    return x;
}

/*******************************************************************************
 * Simple flip of a fair unbiased coin, caching bits for efficiency
 */
int cmb_random_flip(void)
{
    if (flip_bitpos == 0) {
        flip_bits = cmb_random_sfc64();
        flip_bitpos = 64;
    }

    return ((flip_bits >> --flip_bitpos) & 1) ? 1 : 0;
}

/*******************************************************************************
 * Geometric distribution, the number of trials until and including the
 * first success.
 */
uint64_t cmb_random_geometric(const double p)
{
    cmb_assert_release((p > 0.0) && (p <= 1.0));

    if (p == 1.0) {
        /* Special case, the log below would overflow */
        return UINT64_C(1);
    }

    /* Caching for cases where many samples are required with same p,
     * avoids redoing the same expensive log operation over and over. */
    static CMB_THREAD_LOCAL double prev = 0.0;
    static CMB_THREAD_LOCAL double denom = 0.0;
    if (p != prev) {
        denom = -log1p(-p);
        prev = p;
    }

    const double d = ceil(cmb_random_std_exponential() / denom);
    const uint64_t x = (d >= (double)UINT64_MAX) ? UINT64_MAX : (uint64_t)d;
    if (x == 0) {
        /* Special case with probability ~2^(-64) */
        return UINT64_C(1);
    }

    cmb_assert_debug(x >= 1u);
    return x;
}

/******************************************************************************
 * Binomial distribution. Uses Hörmann's fast BTRD algorithm for large rates
 * and a simple inversion search for small.
 */

/*
 * cmi_random_binomial_chopdown - Simple nversion chop-down search.
 * Expects already folded p, so p <= 0.5 here. Limit n range to 2^53, size of a
 * double mantissa.
 */
static const uint64_t binomial_n_max = UINT64_C(9007199254740992);

static uint64_t cmi_random_binomial_chopdown(const uint64_t n, const double p)
{
    cmb_assert_debug(n <= binomial_n_max);
    cmb_assert_debug((p > 0.0) && (p <= 0.5));

    /* Thread local cache */
    static CMB_THREAD_LOCAL uint64_t n_prev = 0u;
    static CMB_THREAD_LOCAL double p_prev = -1.0;
    static CMB_THREAD_LOCAL double p0 = 0.0;
    static CMB_THREAD_LOCAL double r = 0.0;

    if ((n != n_prev) || (p != p_prev)) {
        /* Update cache */
        n_prev = n;
        p_prev = p;
        p0 = exp((double)n * log1p(-p));
        r = p / (1.0 - p);
    }

    double u = cmb_random();
    double pk = p0;
    uint64_t k = 0u;

    while (u > pk) {
        u -= pk;
        k++;
        if (k > n) {
            /* Clamp to fix accumulated rounding errors */
            k = n;
            break;
        }

        pk *= ((double)(n - k + 1u) / (double)k) * r;
        if (pk == 0.0) {
            /* Tail underflow, bail out of infinite cycle */
            break;
        }
    }

    return k;
}

/*
 * stirling_tail, correction term fc(k) for the Stirling approximation.
 *   fc(k) = 1/(12 * (k+1)) - 1/(360 * (k+1)^3) + 1/(1260 * (k+1)^5)
 *
 * Additional terms needed for small k, using tabulated exact values
 * calculated in Python `mpmath` as
 *   ex = log(factorial(k)) - (mpf(1)/2*log(2*pi) + (k+mpf(1)/2)*log(k+1) - (k+1))
 * which matches the values stated in Hörmann's paper (with one more significant
 * digit given here for full double precision resolution).
 */
static const double stirling_tab[10] = {
    8.1061466795327261e-02,   /* fc(0) */
    4.1340695955409297e-02,   /* fc(1) */
    2.7677925684998338e-02,   /* fc(2) */
    2.0790672103765093e-02,   /* fc(3) */
    1.6644691189821193e-02,   /* fc(4) */
    1.3876128823070748e-02,   /* fc(5) */
    1.1896709945891770e-02,   /* fc(6) */
    1.0411265261972096e-02,   /* fc(7) */
    9.2554621827127329e-03,   /* fc(8) */
    8.3305634333628708e-03,   /* fc(9) */
};

static double stirling_tail(const double k)
{
    cmb_assert_debug(k >= 0.0);

    double fc;
    if (k < 10.0) {
        const int i = (int)k;
        fc =  stirling_tab[i];
    }
    else {
        const double kp1 = k + 1.0;
        const double kp1s = kp1 * kp1;
        fc = (1.0 / 12.0 - (1.0 / 360.0 - 1.0 / (1260.0 * kp1s)) / kp1s) / kp1;
    }

    return fc;
}

static double signof(const double x)
{
    if (x > 0.0) {
        return 1.0;
    }
    else if (x < 0.0) {
        return -1.0;
    }
    else {
        cmb_assert_debug(x == 0.0);
        return 0.0;
    }
}

/*
 * cmi_random_binomial_btrd - Transformed rejection with squeeze.
 *      Expects the folded p, so p <= 0.5 here, and n*p at or above validity
 *      threshold 10.
 *
 * See:
 *     Hörmann, W. (1993), "The generation of binomial random variates",
 *     Journal of Statistical Computation and Simulation 46(1-2):101-110.
 *
 *     Hörmann, W. (1992). The generation of binomial random variates. (April
 *     1992 ed.) Institut für Statistik und Mathematik, Abt. f. Angewandte
 *     Statistik u. Datenverarbeitung, WU Vienna University of Economics and
 *     Business. Preprint Series / Department of Applied Statistics and Data
 *     Processing No. 1
 *     https://doi.org/10.57938/79ec156b-9c3d-4b5d-a939-07a1846443fd
 *     https://research.wu.ac.at/ws/files/18967500/document.pdf
 */
static uint64_t cmi_random_binomial_btrd(const uint64_t n, const double p)
{
    cmb_assert_debug(n <= binomial_n_max);
    cmb_assert_debug((p > 0.0) && (p <= 0.5));
    cmb_assert_debug(((double)n * p) >= 10.0);

    static CMB_THREAD_LOCAL uint64_t n_prev = 0u;
    static CMB_THREAD_LOCAL double p_prev = -1.0;
    static CMB_THREAD_LOCAL double dn = 0.0;
    static CMB_THREAD_LOCAL double npq = 0.0;
    static CMB_THREAD_LOCAL double a = 0.0;
    static CMB_THREAD_LOCAL double b = 0.0;
    static CMB_THREAD_LOCAL double c = 0.0;
    static CMB_THREAD_LOCAL double vr = 0.0;
    static CMB_THREAD_LOCAL double urvr = 0.0;
    static CMB_THREAD_LOCAL double alpha = 0.0;
    static CMB_THREAD_LOCAL double r = 0.0;
    static CMB_THREAD_LOCAL double nr = 0.0;
    static CMB_THREAD_LOCAL double dm = 0.0;
    static CMB_THREAD_LOCAL int64_t im = 0;

    if ((n != n_prev) || (p != p_prev)) {
        /* 0. Update cache */
        n_prev = n;
        p_prev = p;
        dn = (double)n;
        dm = floor((dn + 1.0) * p);
        im = (int64_t)dm;
        const double q = 1.0 - p;
        r = p / q;
        nr = (dn + 1.0) * r;
        npq = dn * p * q;
        const double spq = sqrt(npq);
        b = 1.15 + 2.53 * spq;
        a = -0.0873 + 0.0248 * b + 0.01 * p;
        c = dn * p + 0.5;
        alpha = (2.83 + 5.1 / b) * spq;
        vr = 0.92 - 4.2 / b;
        urvr = 0.86 * vr;
    }

    for (;;) {
        /* 1. Decomposition step */
        double u = 0.0;
        double v = cmb_random();
        if (v <= urvr) {
            /* Lucky, fast return */
            u = (v / vr) - 0.43;
            const double g = (2.0 * a) / (0.5 - fabs(u));
            const double ret = floor(u * (g + b) + c);

            cmb_assert_debug(ret >= 0.0);
            return (uint64_t)ret;
        }

        /* 2. Generate candidate pair (u, v) */
        if (v >= vr) {
            /* Range (0.0, 1.0) */
            u = (1.0 - cmb_random()) - 0.5;
        }
        else {
            u = v/vr - 0.93;
            u = signof(u) * 0.5 - u;
            v = (1.0 - cmb_random()) * vr;
        }

        /* 3.0 Can it be rejected? */
        const double us = 0.5 - fabs(u);
        if (us <= 0.0) {
            /* Numerically intractable, will be rejected anyway */
            continue;
        }

        const double g = ((2.0 * a) / us) + b;
        const double dk = floor(g * u + c);
        if (dk < 0.0 || dk > dn) {
            /* Reject */
            continue;
        }

        const int64_t k = (int64_t)dk;
        v = v * alpha / (a / (us * us) + b);
        const double km = fabs(dk - dm);

        if (km <= 15.0) {
            /* 3.1 Evaluate f(k) iteratively */
            double f = 1.0;
            if (im < k) {
                int64_t i = im;
                while (i != k) {
                    i++;
                    f *= nr / i - r;
                }
            }
            else if (im > k) {
                int64_t i = k;
                while (i != im) {
                    i++;
                    v *= nr / i - r;
                }
            }

            if (v <= f) {
                return k;
            }
            else {
                continue;
            }
        }

        /* 3.2 Squeeze accept or reject */
        v = log(v);
        const double rho = (km / npq) * (((km / 3.0 + 0.625) * km + 1.0 / 6.0)
                            / npq + 0.5);
        const double t = -km * km / (2.0 * npq);
        if (v < t - rho) {
            return k;
        }
        if (v > t + rho) {
            continue;
        }

        /* 3.3 Setup for final check */
        const double nm = n - dm + 1.0;
        const double h = (dm + 0.5) * log((dm + 1.0) / (r * nm))
                    + stirling_tail(dm) + stirling_tail(dn - dm);

        /* 3.4 Final acceptance-rejection test */
        const double nk = dn - k + 1.0;
        const double ugh = h + (dn + 1.0) * log(nm / nk)
                          + (k + 0.5) * log(nk * r / (k + 1.0))
                          - stirling_tail(k) - stirling_tail(dn - (double)k);
        if (v <= ugh) {
            return k;
        }

        /* No joy, try again */
    }
}


/*
 * cmb_random_binomial - Number of successes in n independent trials, each
 * succeeding with probability p. Switching from chop-down inversion search
 * to Hörmann's BTRD at np = 30 for performance, BTRD valid for np > 10.
 */

static const double binomial_np_switch = 30.0;

uint64_t cmb_random_binomial(const uint64_t n, const double p)
{
    cmb_assert_release(n <= binomial_n_max);
    cmb_assert_release((p > 0.0) && (p <= 1.0));

    if (p == 1.0) {
        /* Special case, guaranteed to succeed on every trial */
        return n;
    }

    /* Fold to p <= 0.5: halves the inversion work  */
    const bool flip = (p > 0.5);
    const double pp = flip ? (1.0 - p) : p;
    cmb_assert_debug(pp > 0.0);

    uint64_t k;
    if (((double)n * pp) < binomial_np_switch) {
        k = cmi_random_binomial_chopdown(n, pp);
    }
    else {
        k = cmi_random_binomial_btrd(n, pp);
    }

    cmb_assert_debug(k <= n);

    return flip ? (n - k) : k;
}

/*******************************************************************************
 * Poisson distribution, number of arrivals per unit time in a Poisson
 *        process with arrival rate `r`, where `r > 0`.
 */

/*
 * cmi_random_poisson_chopdown - Simple inversion search
 */
uint64_t cmi_random_poisson_chopdown(const double r)
{
    cmb_assert_debug(r > 0.0);

    /* Thread local cache to avoid unnecessarily repeated exp() */
    static CMB_THREAD_LOCAL double r_prev = 0.0;
    static CMB_THREAD_LOCAL double p0 = 0.0;

    if (r != r_prev) {
        /* Update cache */
        r_prev = r;
        p0 = exp(-r);
    }

    double u = cmb_random();
    double p = p0;
    uint64_t k = 0;
    while (u > p) {
        u -= p;
        k++;
        p *= r / (double)k;
        if (p == 0.0) {
            /* Tail underflow, bail out from what would be an infinite cycle */
            break;
        }
    }

    return k;
}

/* Constant, 0.5 * log(2 * pi) = log(sqrt(2 * pi)) */
static const double half_log_2pi = 0.91893853320467278;

/* log(k!) for k < 10, where the Stirling series is not accurate enough.
 * Calculated in Python mpmath at 50 digits precision and rounded to double
 * precision resolution. */
static const double log_fact_table[10] = {
    0.0,
    0.0,
    0.69314718055994531,
    1.7917594692280550,
    3.1780538303479456,
    4.7874917427820460,
    6.5792512120101010,
    8.5251613610654143,
   10.6046029027452502,
   12.8018274800814696
};

/*
 * cmi_random_poisson_ptrd - Transformed rejection with decomposition. Constant expected
 * cost in r. Roughly 86 percent of draws leave by the fast path having consumed
 * a single uniform and evaluated no transcendental. Only valid for r >= 10.
 *
 * See
 *  Hörmann, W. (1993), "The transformed rejection method for generating
 *  Poisson random variables", Insurance: Mathematics and Economics 12(1):39-45.
 *
 *  Hörmann, W. (1992). The transformed rejection method for generating Poisson random
 *  variables. (April 1992 ed.) Institut für Statistik und Mathematik, Abt. f. Angewandte
 *  Statistik u. Datenverarbeitung, WU Vienna University of Economics and Business.
 *  Preprint Series / Department of Applied Statistics and Data Processing No. 2
 *  https://doi.org/10.57938/feb80d49-2db4-4305-bf0f-09b35b3f45f1
 *  https://research.wu.ac.at/ws/portalfiles/portal/18953249/document.pdf
 *
 * Implementation adapted from Roy Ward's C++ implementation (BSD 3-clause, see NOTICE),
 *      https://github.com/royward/random-variate-poisson
 * See also his blog post:
 *      https://www.orange-kiwi.com/posts/fast-integer-poisson-random-variates-for-procedural-generation/
 */
uint64_t cmi_random_poisson_ptrd(const double r)
{
    cmb_assert_debug(r >= 10.0);

    /* Thread local cache */
    static CMB_THREAD_LOCAL double r_prev = 0.0;
    static CMB_THREAD_LOCAL double smu = 0.0;
    static CMB_THREAD_LOCAL double a = 0.0;
    static CMB_THREAD_LOCAL double b = 0.0;
    static CMB_THREAD_LOCAL double vr = 0.0;
    static CMB_THREAD_LOCAL double vr_fast = 0.0;
    static CMB_THREAD_LOCAL double inv_alpha = 0.0;

    if (r != r_prev) {
        /* 0. Update cache */
        r_prev = r;
        smu = sqrt(r);
        b = 0.931 + 2.53 * smu;
        a = -0.059 + 0.02483 * b;
        inv_alpha = 1.1239 + 1.1328 / (b - 3.4);
        vr = 0.9277 - 3.6224 / (b - 2.0);
        vr_fast = 0.86 * vr;
    }

    for (;;) {
        /* 1. Generate a uniform random number V */
        double v = cmb_random();
        double u;

        if (v < vr_fast) {
            /* Decomposition step succeeded */
            u = v / vr - 0.43;
            const double us = 0.5 - fabs(u);
            const double k = floor((2.0 * a / us + b) * u + r + 0.445);

            cmb_assert_debug(k >= 0.0);
            return (uint64_t)k;
        }

        /* 2. Generate a uniform random number U */
        const double t = cmb_random();
        if (v >= vr) {
            /* Generate U in  in (-0.5, 0.5)  */
            u = t - 0.5;
        }
        else {
            /* Generate V in (0, vr) */
            u = v / vr - 0.93;
            u = signof(u) * 0.5 - u;
            v = t * vr;
        }

        /* 3.0 Can it be rejected out of hand? */
        const double us = 0.5 - fabs(u);
        if ((us <= 0.0) || ((us < 0.013) && (v > us))) {
            /* Try another */
            continue;
        }

        /* 3.1 Transform */
        const double k = floor((2.0 * a / us + b) * u + r + 0.445);
        v = v * inv_alpha / (a / (us * us) + b);

        if (k >= 10.0) {
            const double rhs = (k + 0.5) * log(r / k) - r - half_log_2pi
                               + k - (1.0 / 12.0 - 1.0 / (360.0 * k * k)) / k;
            if (log(v * smu) <= rhs) {
                cmb_assert_debug(k >= 0.0);
                return (uint64_t)k;
            }
        }
        else if (k >= 0.0) {
            /* 3.2 k in [0, 10): check exact log factorial. */
            const uint64_t ki = (uint64_t)k;
            if (log(v) < ((k * log(r)) - r - log_fact_table[ki])) {
                return ki;
            }
        }

        /* No joy, try another */
    }
}

uint64_t cmb_random_poisson(double r)
{
    cmb_assert_release(r > 0.0);
    cmb_assert_release(r <= 1.0e18);

    uint64_t x;
    if (r < 17.0) {
        /* The simple inversion search is faster */
        x = cmi_random_poisson_chopdown(r);
    }
    else if (r < 1.0e10) {
        /* Use Hörmann's PTRD */
        x = cmi_random_poisson_ptrd(r);
    }
    else {
        /* The normal approximation is more accurate for very large r */
        const double z = cmb_random_std_normal();
        const double d = floor(r + sqrt(r) * z + 0.5);
        if (d <= 0.0) {
            x = UINT64_C(0);
        }
        else if (d >= (double)UINT64_MAX) {
            x = UINT64_MAX;
        }
        else {
            x = (uint64_t) d;
        }
    }

    return x;
}

/*******************************************************************************
 * Negative binomial distribution, number of failures until m'th success,
 * where p > 0 is the probability of success in each trial.
 */
uint64_t cmb_random_negative_binomial(const uint64_t m, const double p)
{
    cmb_assert_release(m > 0);
    cmb_assert_release((p > 0.0) && (p <= 1.0));

    if (p == 1.0) {
        /* Special case, guaranteed to succeed on first trial */
        return 0u;
    }

    const double lambda = cmb_random_gamma(m, (1.0 - p) / p);

    return cmb_random_poisson(lambda);
}

/*******************************************************************************
 * Discrete uniform, generate a uniform integer on [0, s) by using Daniel
 * Lemire's "nearly divisionless" algorithm. See:
 *  https://arxiv.org/pdf/1805.10941
 *  https://lemire.me/blog/2019/06/06/nearly-divisionless-random-integer-generation-on-various-systems/
 */
uint64_t cmb_random_discrete_uniform (const uint64_t s)
{
    cmb_assert_release(s > 0u);

    uint64_t x = cmb_random_sfc64();
    __uint128_t m = (__uint128_t) x * (__uint128_t) s;
    uint64_t l = (uint64_t) m;
    if (l < s) {
        const uint64_t t = -s % s;
        while (l < t) {
            x = cmb_random_sfc64();
            m = ( __uint128_t ) x * ( __uint128_t ) s;
            l = ( uint64_t ) m;
        }
    }

    return m >> 64u;
}

static const double sum_tolerance = 1.0e-3;
static bool sums_to_one(const uint64_t n, const double p[n])
{
    double sum = 0.0;
    for (uint64_t ui = 0u; ui < n; ui++) {
        sum += p[ui];
    }

    return (fabs(sum - 1.0) <= sum_tolerance) ? true : false;
}

/*******************************************************************************
 * Non-uniform discrete distribution on [0, n-1], typically used for selecting
 * in an array.
 */
uint64_t cmb_random_discrete_nonuniform(const uint64_t n, const double *pa)
{
    cmb_assert_release(n > 0);
    cmb_assert_release(pa != NULL);
    cmb_assert_release(sums_to_one(n, pa));

    const double x = cmb_random();

    double q = 0.0;
    uint64_t ui;
    for (ui = 0; ui < n - 1; ui++) {
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
 * 1. Call cmb_random_alias_create once to create a lookup table before sampling.
 * 2. Sample as needed with cmb_random_alias_sample from the created table.
 * 3. Call cmb_random_alias_destroy when done to deallocate lookup table.
 */

/* Helper function to make sure the table index never wraps around */
static inline uint64_t alias_secure(const double p)
{
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

/* Create an alias lookup table before sampling */
struct cmb_random_alias *cmb_random_alias_create(const uint64_t n,
                                                 const double *pa) {
    cmb_assert_release(n > 0u);
    cmb_assert_release(sums_to_one(n, pa));

    struct cmb_random_alias *ap = NULL;
    double *work = cmi_calloc(n, sizeof(double));
    double psum = 0.0;
    for (uint64_t ai = 0; ai < n; ai++) {
        psum += pa[ai];
    }
    cmb_assert_debug(fabs(psum - 1.0) <= sum_tolerance);

    uint64_t *small = cmi_calloc(n, sizeof(uint64_t));
    uint64_t *large = cmi_calloc(n, sizeof(uint64_t));
    uint64_t idxs = 0;
    uint64_t idxl = 0;
    for (uint64_t ui = 0; ui < n; ui++) {
        work[ui] = pa[ui] * n  / psum;
        if (work[ui] < 1.0) {
            small[idxs++] = ui;
        }
        else {
            large[idxl++] = ui;
        }
    }

    ap = cmi_malloc(sizeof *ap);
    ap->n = n;
    ap->uprob = cmi_calloc(n, sizeof(uint64_t));
    ap->alias = cmi_calloc(n, sizeof(uint64_t));

    while ((idxs > 0) && (idxl > 0)) {
        const uint64_t l = small[--idxs];
        const uint64_t g = large[--idxl];
        ap->uprob[l] = alias_secure(work[l]);
        ap->alias[l] = g;
        work[g] = (work[g] + work[l]) - 1.0;
        if (work[g] < 1.0) {
            small[idxs++] = g;
        }
        else {
            large[idxl++] = g;
        }
    }

    while (idxl > 0) {
        const uint64_t g = large[--idxl];
        ap->uprob[g] = UINT64_MAX;
        ap->alias[g] = g;
    }

    while (idxs > 0) {
        const uint64_t l = small[--idxs];
        ap->uprob[l] = UINT64_MAX;
        ap->alias[l] = l;
    }

    cmi_free(large);
    cmi_free(small);
    cmi_free(work);

    cmi_dlist_initialize(&(ap->destroy.node));
    ap->destroy.teardown = (cmi_teardown_func *)cmb_random_alias_destroy;
    ap->destroy.object = ap;
    cmi_memregistry_add(&(ap->destroy));

    return ap;
}

uint64_t cmb_random_alias_sample(const struct cmb_random_alias *ap)
{
    cmb_assert_release(ap != NULL);
    cmb_assert_debug(ap->uprob != NULL);
    cmb_assert_debug(ap->alias != NULL);

    const uint64_t idx = cmb_random_discrete_uniform(ap->n);
    const bool c = (cmb_random_sfc64() >= ap->uprob[idx]);
    const uint64_t r = (c) ? ap->alias[idx] : idx;

    cmb_assert_debug(r < ap->n);
    return r;
}


/* Deallocate the alias lookup table when done sampling */
void cmb_random_alias_destroy(struct cmb_random_alias *ap)
{
    cmb_assert_release(ap != NULL);
    cmb_assert_debug(ap->uprob != NULL);
    cmb_assert_debug(ap->alias != NULL);

    if (!cmi_memregistry_is_demolishing) {
        cmi_memregistry_remove(&(ap->destroy));
    }

    cmi_free(ap->uprob);
    cmi_free(ap->alias);
    cmi_free(ap);
}