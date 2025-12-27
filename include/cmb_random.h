/**
 * @file cmb_random.h
 * @brief Fast, high-quality pseudo-random number generators and distributions
 *        built from the ground up for multithreaded use.
 *
 * The main generator gives 64-bit pseudo-random numbers with 256 bits of state
 * and a cycle of at least 2^64 samples. It is seeded by a 64-bit value but
 * amplifies that to a 265-bit state by using an auxiliary generator with a 64-bit
 * state to bootstrap the initial 256-bit state for the main generator. All
 * later pseudo-random numbers come from the same stream.
 *
 * The state is thread local, i.e., providing a separate random number stream
 * for each thread. Setting a new seed will determine the random number stream
 * for that thread until it is reset to some other seed. This makes it possible
 * to run independent identically distributed trials in separate threads without
 * interactions with each other.
 *
 * Suitable 64-bit seeds can be obtained from hardware entropy by calling
 * `cmb_random_get_hwseed`.
 *
 * The various random number distributions are built on this generator. They use
 * the fastest available algorithms without making any compromises on accuracy.
 * A wide range of random number distributions are provided, both academically
 * important ones like beta and gamma, and more empirical ones like triangular
 * and PERT. For arbitrary non-uniform discrete distributions, efficient Vose
 * alias sampling is provided.
 *
 * Most other pseudo-random number generators and distributions are not thread-
 * safe. The internal generator state is often kept as static variables between
 * calls, making it non-reentrant. Some common distributions, such as the
 * typical Box-Muller method for normal variates, also depend on static
 * variables to maintain state between calls, making it unsuitable for our
 * purpose. Luckily, the algorithms used here are not only thread-safe, but also
 * faster and statistically equally good or better.
 *
 * For mathematical details about the various distributions, the respective
 * Wikipedia pages are highly recommended.
 */

/*
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
 *
 * The normal and exponential distributions below are based on code at
 *      https://github.com/cd-mcfarland/fast_prng
 *      Copyright (c) Chris D McFarland 2025.
 *      Used with permission by author.
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

#ifndef CIMBA_CMB_RANDOM_H
#define CIMBA_CMB_RANDOM_H

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>

#include "cmb_assert.h"

/**
 * @brief Initiate pseudo-random number distribution using a 64-bit seed. Call
 *        this function before drawing samples from any random number
 *        distributions. Can be called again later to reset seed to initial (or
 *        some other) state.
 *
 * The given seed will be bootstrapped internally to a 256-bit state by an
 * auxiliary pseudo-random number generator only used for this purpose. The
 * state is thread local, i.e., the call is only effective for the currently
 * executing thread.
 *
 * @param seed Initial seed value to be used, preferably a random 64-bit value.
 */
extern void cmb_random_initialize(uint64_t seed);

/**
 * @brief Resets the random number generator to the newly created, pre-initialized
 *        state.
 */
extern void cmb_random_terminate(void);

/**
 * @brief Get a suitable 64-bit seed from a hardware entropy source.
 *
 * It will use the best available entropy source on the current hardware, such
 * as the `RDSEED` or `RDRAND` CPU instruction. If no suitable hardware
 * entropy source  is available, this function will do a mashup of clock time
 * and CPU cycle count to get a suitably random seed value.
 *
 * @return A random 64-bit value from the best available entropy source of the
 *         current CPU hardware.
 */
extern uint64_t cmb_random_get_hwseed(void);

/**
 * @brief Get the seed that was used for the ongoing run in this thread. Used as
 *        a debugging tool, e.g., to enable repeating whatever sequence of events
 *        led to some unexpected result, possibly in a debugger or with more
 *        `printf()` statements enabled next time.
 *
 * @return The 64-bit seed that was used to initialize the generator. If it
 *         returns `0x0000DEAD5EED0000`, the generator was never initialized.
 */
extern uint64_t cmb_random_get_curseed(void);

/**
 * @brief The main pseudo-random number generator - 64-bit output, 256-bit
 *        thread local state. An implementation of Chris Doty-Humphrey's sfc64.
 *
 * Public domain, see https://pracrand.sourceforge.net
 *
 * @return A uniformly distributed pseudo-random 64-bit unsigned bit pattern.
 */
extern uint64_t cmb_random_sfc64(void);

/**
 * @brief Continuous uniform distribution on the interval [0, 1].
 *
 * A 64-bit double has a 53-bit significand. We discard the bottom 11 bits and
 * scale the result by 2^(-53) to get a number in [0.0, 1.0].
 *
 * See also https://en.wikipedia.org/wiki/Continuous_uniform_distribution
 */
static inline double cmb_random(void)
{
    return ldexp((double)(cmb_random_sfc64() >> 11), -53);
}

/**
 * @brief Continuous uniform distribution on the interval `[min, max]`.
 *
 * Often used in lack of any other information about a distribution than
 * the endpoints. Assuming a uniform distribution between may then be
 * reasonable, but see also `cmb_random_triangular`, `cmb_random_PERT`, and
 * `cmb_random_PERT_mod` as other possible empirical distributions.
 *
 * See also https://en.wikipedia.org/wiki/Continuous_uniform_distribution
 */
static inline double cmb_random_uniform(const double min, const double max)
{
    cmb_assert_release(min < max);

    const double r = min + (max - min) * cmb_random();
    cmb_assert_debug((r >= min) && (r <= max));

    return r;
}

/**
 * @brief Triangular distribution on the interval `[min, max]` with peak at
 *        `mode`, where `min < mode < max`.
 *
 * The probability density function is zero at `min` and `max`, and reaches a
 * maximum of `2 / (max - min)` at `mode`. The mean is `(min + mode + max) / 3`.
 *
 * Used as an empirical "I don't know much about the shape of this thing"
 * distribution. Also consider the PERT distributions `cmb_random_PERT()` and
 * `cmb_random_PERT_mod()` or the scaled beta distribution `cmb_random_beta()`
 * for this purpose.
 *
 * See also https://en.wikipedia.org/wiki/Triangular_distribution
 */
extern double cmb_random_triangular(double min, double mode, double max);

/** @cond */
extern const uint8_t cmi_random_nor_zig_max;
extern const double cmi_random_nor_zig_pdf_x[];
extern double cmi_random_nor_not_hot(int64_t i_cand_x);
/** @endcond */

/**
 * @brief Standard normal distribution on (-oo, oo) with mean 0 and standard
 *        deviation 0.
 *
 * Uses an implementation of McFarland's improved ziggurat method.
 *
 * See also https://en.wikipedia.org/wiki/Normal_distribution
 */
static inline double cmb_random_std_normal(void)
{
    uint64_t bits = cmb_random_sfc64();
    const int64_t i_cand_x = *(int64_t *) &bits;
    const uint8_t idx = i_cand_x & 0xFF;

    return (idx <= cmi_random_nor_zig_max) ?
        cmi_random_nor_zig_pdf_x[idx] * (double) i_cand_x :
        cmi_random_nor_not_hot(i_cand_x);
}

/**
 * @brief Normal distribution on `(-oo, oo)` with mean `mu` and standard
 *        deviation `sigma` where `sigma > 0`.
 *
 * Often used to model measurement errors or process variation.
 * It tends to appear whenever the variation is caused by a sum (or average)
 * of many small effects, according to the Central Limit Theorem.
 *
 * Uses an implementation of McFarland's improved ziggurat method.
 *
 * See also https://en.wikipedia.org/wiki/Normal_distribution
 */
static inline double cmb_random_normal(const double mu, const double sigma)
{
    cmb_assert_release(sigma > 0.0);

    return mu + sigma * cmb_random_std_normal();
}

/**
 * @brief Lognormal distribution on `[0, oo)` with parameters `m` and `s`,
 *        where `m > 0` and `s > 0`.
 *
 * The mean is `exp(m + 0.5 s^2)`, the median `exp(m)`.
 *
 * Occurs naturally for effects that are the product of many small non-negative
 * sources of variation, including multiplicative measurement errors.
 *
 * See also https://en.wikipedia.org/wiki/Log-normal_distribution
 */
static inline double cmb_random_lognormal(const double m, const double s)
{
    cmb_assert_release(s > 0.0);
    const double r = exp(cmb_random_normal(m, s));

    cmb_assert_debug(r >= 0.0);
    return r;
}

/**
 * @brief Logistic distribution with location `m` and scale `s`.
 *
 * Similar to normal distribution, but with fatter tails.
 * Mean = median = mode = `m`.
 *
 * See also https://en.wikipedia.org/wiki/Logistic_distribution
 */
static inline double cmb_random_logistic(const double m, const double s)
{
    cmb_assert_release(s > 0.0);
    const double x = cmb_random();

    return m + s * log(x / (1.0 - x));
}

/**
 * @brief Cauchy distribution - the canonical fat-tailed distribution.
 *
 * The mean, variance, and all higher moments are undefined.
 *
 * Occurs e.g., as the incidence of rays from a point source onto a line.
 * Substituting it for a normal distribution in some financial model gives
 * black swan events galore. Also known as the Lorentz distribution or the
 * Witch of Agnesi. It is evil. Mostly useful as a practical joke, or as a
 * pathological test case to break assumptions.
 *
 * See also https://en.wikipedia.org/wiki/Cauchy_distribution
 */
static inline double cmb_random_cauchy(const double mode, const double scale)
{
    cmb_assert_release(scale > 0.0);

    const double x = cmb_random_std_normal();
    double y;
    while ((y = cmb_random_std_normal()) == 0.0) {}

    return mode + scale * x / y;
 }

/** @cond */
extern const uint8_t cmi_random_exp_zig_max;
extern const double cmi_random_exp_zig_pdf_x[];
extern double cmi_random_exp_not_hot(uint64_t u_cand_x);
/** @endcond */

/**
 * @brief Exponential distribution on `[0, oo)` with rate 1.
 *
 * Used for modeling time intervals between successive events, such as
 * customer inter-arrival times, service times, times to fail or repair,
 * state transition times in Markov chains.
 *
 * Uses an implementation of McFarland's improved ziggurat method.
 *
 * See also https://en.wikipedia.org/wiki/Exponential_distribution
 */
static inline double cmb_random_std_exponential(void)
{
    const uint64_t u_cand_x = cmb_random_sfc64();
    const uint8_t idx = u_cand_x & 0xff;
    double r = (idx <= cmi_random_exp_zig_max) ?
        cmi_random_exp_zig_pdf_x[idx] * (double) u_cand_x :
        cmi_random_exp_not_hot(u_cand_x);

    cmb_assert_debug(r >= 0.0);
    return r;
}

/**
 * @brief Exponential distribution on `[0, oo)` with mean `m`, where `m > 0`.
 *        Corresponds to a rate `r = 1/m` (but avoids a division in each call).
 *
 * Used for modeling time intervals between successive events, such as
 * customer inter-arrival times, service times, times to fail or repair,
 * state transition times in Markov chains.
 *
 * Uses an implementation of McFarland's improved ziggurat method.
 *
 * See also https://en.wikipedia.org/wiki/Exponential_distribution
 */
static inline double cmb_random_exponential(const double mean)
{
    cmb_assert_release(mean > 0.0);

    double r = mean * cmb_random_std_exponential();

    cmb_assert_debug(r >= 0.0);
    return r;
}

/**
 * @brief Erlang distribution on `[0, oo)`, a sum of `k` exponentially
 *        distributed random variables each with mean `m`, where `k > 0` and
 *        `m > 0`.
 *
 * The mean is `k m`, the variance `k m^2`.
 *
 * Used for modeling more complex service times than the simple exponential.
 *
 * See also https://en.wikipedia.org/wiki/Erlang_distribution
 */
static inline double cmb_random_erlang(const unsigned k, const double m)
{
    cmb_assert_release(k > 0u);
    cmb_assert_release(m > 0.0);

    double x = 0.0;
    for (unsigned i = 0u; i < k; i++) {
        x += cmb_random_exponential(m);
    }

    cmb_assert_debug(x >= 0.0);
    return x;
}

/**
 * @brief Hypoexponential on `[0, oo)`, a sum of `n` exponentially distributed
 *        random variables, each with mean `ma[i]`, where `n > 0` and
 *        `ma[i] > 0`.
 *
 * The mean equals the sum of `ma[i]`, the variance the sum of `ma[i]^2`.
 *
 * A slight generalization of the Erlang distribution by allowing each
 * summed item its own exponential parameter. This will give a lower coefficient
 * of variation than a single exponential, hence the name.
 *
 * See also https://en.wikipedia.org/wiki/Hypoexponential_distribution
 */
static inline double cmb_random_hypoexponential(const unsigned n, const double *ma)
{
    cmb_assert_release(n > 0);
    cmb_assert_release(ma != NULL);

    double x = 0.0;
    for (unsigned i = 0; i < n; i++) {
        cmb_assert_release(ma[i] > 0.0);
        x += cmb_random_exponential(ma[i]);
    }

    cmb_assert_debug(x >= 0.0);
    return x;
}

/**
 * @brief Hyperexponential on `[0, oo)`, choosing and samples one of `n`
 *        exponential distributions. Assumes that `pa` sums to 1.0.
 *
 * The probability of selecting distribution `i` is `p_arr[i]`,
 * the mean of that distribution is `ma[i]`.
 * The overall mean is the sum of `pa[i] * ma[i]`.
 *
 * Uses a simple O(n) implementation. If `n` is large and speed is important,
 * consider using O(1) Vose alias sampling to select the distribution instead of
 * this function.
 *
 * See also https://en.wikipedia.org/wiki/Hyperexponential_distribution
 */
extern double cmb_random_hyperexponential(unsigned n,
                                          const double *ma,
                                          const double *pa);

/**
 * @brief Gamma distribution on `[0, oo)` with shape parameter `shape`, where
 *        `shape > 0`. Equal to `cmb_random_std_gamma(shape, 1.0)`.
 *
 * Generalizes the Erlang distribution to noninteger `n` (shape).
 * The mean and variance equal `shape`.
 *
 * See also https://en.wikipedia.org/wiki/Gamma_distribution
 */
extern double cmb_random_std_gamma(double shape);

/**
 * @brief Gamma distribution on `[0, oo)` with shape parameter `shape` and
 *        scale parameter `scale`, both `> 0`.
 *
 * Generalizes the Erlang distribution to noninteger `n` (here `shape`).
 * The mean is `shape * scale`, the variance `shape * scale^2`.
 *
 * Used for various servicing, waiting, and repair times in queuing systems,
 * along the exponential and Erlang distributions.
 *
 * See also https://en.wikipedia.org/wiki/Gamma_distribution
 */
static inline double cmb_random_gamma(const double shape, const double scale)
{
    cmb_assert_release(shape > 0.0);
    cmb_assert_release(scale > 0.0);

    const double r = (shape >= 1.0) ?
        scale * cmb_random_std_gamma(shape) :
        scale * (cmb_random_std_gamma(shape + 1.0)
                 * pow(cmb_random(), 1.0 / shape));

    cmb_assert_debug(r >= 0.0);
    return r;
}

/**
 * @brief Beta distribution on the interval `[0, 1]` with real-valued shape
 *        parameters `a` and `b`, where `a > 0` and `b > 0`.
 *
 * The mean is `a/(a + b)`.
 *
 * Used to model various proportions and percentages of something.
 *
 * See also  https://en.wikipedia.org/wiki/Beta_distribution
 */
static inline double cmb_random_std_beta(const double a, const double b)
{
    cmb_assert_release(a > 0.0);
    cmb_assert_release(b > 0.0);

    const double x = cmb_random_std_gamma(a);
    const double y = cmb_random_std_gamma(b);
    const double r = x / (x + y);

    cmb_assert_debug((r >= 0.0) && (r <= 1.0));
    return r;
}

/**
 * @brief Shifted and scaled beta distribution on arbitrary interval
 *        `[min, max]`  with real-valued shape parameters `a` and `b`,
 *        where `a > 0` and `b > 0`.
 *
 * Used to model task completion times within a certain interval, as an
 * alternative to the triangular and PERT distributions.
 *
 * See also https://en.wikipedia.org/wiki/Beta_distribution
 */
static inline double cmb_random_beta(const double a, const double b,
                              const double min, const double max)
{
    cmb_assert_release(a > 0.0);
    cmb_assert_release(b > 0.0);
    cmb_assert_release(min < max);

    const double x = min + (max - min) * cmb_random_std_beta(a, b);

    cmb_assert_debug((x >= min) && (x <= max));
    return x;
}

/**
 * @brief Modified PERT distribution, a scaled and shifted beta distribution
 *        to get the mean at `m`.
 *
 * Can be used as a heuristically determined distribution where the parameters
 * are "at least min", "most likely around mode", and "not more than max".
 *
 * The additional parameter `lambda` determines the peakiness around `mode`, with
 * `lambda = 4.0` default in the standard PERT distribution `cmb_random_PERT()`.
 *
 * See also https://en.wikipedia.org/wiki/PERT_distribution
 */
extern double cmb_random_PERT_mod(double min,
                                  double mode,
                                  double max,
                                  double lambda);

/**
 * @brief PERT distribution, a scaled and shifted beta distribution
 *        to get the mean at `m`.
 *
 * Can be used as a heuristically determined distribution where the parameters
 * are "at least min", "most likely around mode", and "not more than max".
 *
 * See also https://en.wikipedia.org/wiki/PERT_distribution
 */
static inline double cmb_random_PERT(const double min,
                              const double mode,
                              const double max)
{
    cmb_assert_release(min < mode);
    cmb_assert_release(mode < max);

    const double x = cmb_random_PERT_mod(min, mode, max, 4.0);


    cmb_assert_debug((x >= min) && (x <= max));
    return x;
}

/**
 * @brief Weibull distribution on `[0, oo)` with parameters `shape` and `scale`,
 * where `shape > 0` and `scale > 0`.
 *
 * Generalizes the exponential distribution, typically used for component
 * lifetimes and similar durations. Failure rates increase with time for
 * `shape < 1`, decrease with time for `shape > 1`. Equal to exponential
 * (memoryless, constant failure rate) with mean `scale` when `shape = 1.0`.
 * Looks similar to a normal distribution for `shape` around 4.
 *
 * Also used for wind speed simulation, often with `shape` around 2 and `scale`
 * somewhere around 5 to 15.
 *
 * See also https://en.wikipedia.org/wiki/Weibull_distribution
 */
static inline double cmb_random_weibull(const double shape, const double scale)
{
    cmb_assert_release(shape > 0.0);
    cmb_assert_release(scale > 0.0);

    const double u = cmb_random_exponential(1.0);
    const double x = scale * pow(u, 1.0 / shape);

    cmb_assert_debug(x >= 0.0);
    return x;
}

/**
 * @brief Pareto distribution (power law) on `[mode, oo)` with parameters
 * `shape > 0` and `mode > 0`.
 *
 * Used to model e.g., the size of human settlements (hamlets to cities),
 * size of (extreme) weather events, human income and wealth, etc.
 * Setting `shape = log4(5) = ln(5)/ln(4) = 1.16` gives the 80:20 rule.
 * Higher values of the `shape` parameter give steeper distributions.
 *
 * See also https://en.wikipedia.org/wiki/Pareto_distribution
 */
static inline double cmb_random_pareto(const double shape, const double mode)
{
    cmb_assert_release(shape > 0.0);
    cmb_assert_release(mode > 0.0);

    const double x = mode / pow(cmb_random(), 1.0 / shape);

    cmb_assert_debug(x >= mode);
    return x;
}

/**
 * @brief Chi-squared distribution on `[0, oo)`, modeling the sum of `k` squared
 *        standard normal distributions N(0, 1).
 *
 * Used to model sample variances for normally distributed samples.
 *
 * The parameter `k` is known as the 'degrees of freedom' when it is an integer
 * value. Here generalized by permitting real-valued `k`, not just integers.
 *
 * See also https://en.wikipedia.org/wiki/Chi-squared_distribution
 */
static inline double cmb_random_chisquared(const double k)
{
    cmb_assert_release(k > 0.0);

    const double x = cmb_random_gamma(k / 2.0, 2.0);

    cmb_assert_debug(x >= 0.0);
    return x;
}

/**
 * @brief F distribution for ratios of sample variances, parameters `a` and `b`
 *        for numerator and denominator degrees of freedom, respectively.
 *
 * Probably not very useful in a discrete event simulation context, included for
 * completeness. Here generalized by allowing real-valued a and b, not just
 * integer values.
 *
 * See also https://en.wikipedia.org/wiki/F-distribution
 */
static inline double cmb_random_F_dist(const double a, const double b)
{
    cmb_assert_release(a > 0.0);
    cmb_assert_release(b > 0.0);

    const double x = cmb_random_chisquared(a) / a;
    double y;
    while ((y = cmb_random_chisquared(b) / b) == 0.0) {}

    const double r = x / y;

    cmb_assert_debug(r >= 0.0);
    return r;
}

/**
 * @brief Student's t-distribution for confidence intervals and t-tests.
 *
 * Mean 0.0 for `v > 1`, variance `v / (v - 2)` for `v > 2`, otherwise
 * undefined.
 *
 * Can be used as a generic fat-tailed alternative to the standard
 * normal distribution, where the degree of fat-tailedness depends on `v`.
 * It is equal to a Cauchy distribution for `v = 1`, converging to a normal
 * distribution as `v -> oo`.
 *
 * See also https://en.wikipedia.org/wiki/Student%27s_t-distribution
 */
static inline double cmb_random_std_t_dist(const double v)
{
    cmb_assert_release(v > 0.0);

    const double x = cmb_random_std_normal();
    double y;
    while ((y = cmb_random_chisquared(v)) == 0.0) {}

    const double r = x / sqrt( y / v);
    return r;
}

/**
 * @brief A location - scale generalization of Student's t distribution.
 *
 * Mean `m` for `v > 1`, variance `s * s * v /(v - 2)` for `v > 2`,
 * otherwise undefined.
 *
 * Can be used as a drop-in replacement for normal distributions if fatter tails
 * are needed. It is equal to a Cauchy distribution for `v = 1`, converges to a
 * normal distribution N(m, s) for `v -> oo`.
 *
 * See also https://en.wikipedia.org/wiki/Student%27s_t-distribution
 */
static inline double cmb_random_t_dist(const double m,
                                const double s,
                                const double v)
{
    cmb_assert_release(s > 0.0);
    cmb_assert_release(v > 0.0);

    const double x = m + s * cmb_random_std_t_dist(v);
    return x;
}

/**
 * @brief Rayleigh distribution, equivalent to a scaled chi distribution with
 *        `k = 2`.
 *
 * Occurs in natural phenomena like the amplitude of wind or waves summing from
 * several directions.
 *
 * See also https://en.wikipedia.org/wiki/Rayleigh_distribution
 */
static inline double cmb_random_rayleigh(const double s)
{
    cmb_assert_release(s > 0.0);

    const double x = cmb_random_normal(0.0, s);
    const double y = cmb_random_normal(0.0, s);
    const double r = sqrt(x * x + y * y);

    cmb_assert_debug(r >= 0.0);
    return r;
}

/**
 * @brief A single flip of an unbiased coin. Returns 1 with `p = 0.5`, 0 with
 *        the same probability.
 *
 * Equivalent to `cmb_random_bernoulli(0.5)`, but optimized for speed, only
 * consuming one bit of randomness for each trial by caching random bits
 * every 64 calls.
 *
 * See also https://en.wikipedia.org/wiki/Bernoulli_distribution
 * (or https://sites.stat.columbia.edu/gelman/research/published/diceRev2.pdf)
 */
extern int cmb_random_flip(void);

/**
 * @brief A single Bernoulli trial. Returns 1 with probability `p`, otherwise 0.
 * `0 <= p <= 1`.
 *
 * Used for any binary yes/no outcome of independent and
 * identically distributed trials. A fair coin flip if `p = 0.5`.
 *
 * See also https://en.wikipedia.org/wiki/Bernoulli_distribution
 */
static inline unsigned cmb_random_bernoulli(const double p)
{
    cmb_assert_release((p >= 0.0) && (p <= 1.0));

    return (cmb_random() <= p) ? 1 : 0;
}

/**
 * @brief Geometric distribution, a discrete parallel to the exponential
 *        distribution, returns an integer value in `[1, oo)`.
 *
 * Models the number of trials up to and including the first success in a
 * series of consecutive Bernoulli trials each with probability `p` of success.
 *
 * Mean `1/p`, variance `(1-p)/p^2`.
 *
 * Performs the calculation by simulating the experiment.
 *
 * See also https://en.wikipedia.org/wiki/Geometric_distribution
 */
extern unsigned cmb_random_geometric(double p);

/**
 * @brief Binomial distribution, number of successes in n independent Bernoulli
 *        trials each with probability `p`.
 *
 * Models a drawing process with replacement (or from an infinite pool).
 *
 * Mean `np`, variance `np(1-p)`.
 *
 * Performs the calculation by simulating the experiment.
 *
 * See also https://en.wikipedia.org/wiki/Geometric_distribution
 */
extern unsigned cmb_random_binomial(unsigned n, double p);

/**
 * @brief Negative binomial distribution, the number of failures before the
 *        `m`th success in independent Bernoulli trials each with probability
 *        `p`, sampled with replacement (or equivalently from an infinite pool).
 *
 * Mean `m(1-p)/p`, variance `m(1-p)/p^2`. Equal to a geometric distribution
 * for `m = 1`.
 *
 * Used to model e.g., the number of bits (or packets) that need to be sent to
 * successfully transmit an m-bit (or -packet) message. Also known as the Pascal
 * distribution.
 *
 * Performs the calculation by simulating the experiment.
 *
 * See also https://en.wikipedia.org/wiki/Negative_binomial_distribution
 */
extern unsigned cmb_random_negative_binomial(unsigned m, double p);

/**
 * @brief Pascal distribution an alias for the negative binomial distribution,
 *        `cmb_random_negative_binomial()`, the number of failures before the
 *        `m`th success in independent Bernoulli trials each with probability
 *        `p`, sampled with replacement (or equivalently from an infinite pool).
 *
 * See also https://en.wikipedia.org/wiki/Negative_binomial_distribution
 */
static inline unsigned cmb_random_pascal(const unsigned m, const double p)
{
    return cmb_random_negative_binomial(m, p);
}

/**
 * @brief Poisson distribution, number of arrivals per unit time in a Poisson
 *        process with arrival rate `r`, where `r > 0`.
 *
 * Mean `r`, variance `r`, interarrival times exponentially distributed with
 * mean `1/r`.
 *
 * Models the number of shot noise pulses, customer arrivals, incoming calls,
 * Geiger counter clicks, etc., per unit of time.
 *
 * Performs the calculation by simulating the experiment.
 *
 * See also https://en.wikipedia.org/wiki/Poisson_distribution
 */
extern unsigned cmb_random_poisson(double r);

/**
 * @brief A discrete uniform distribution on `[a, a+1, a+2, ..., b]` for
 *        `a < b`. The function name reflects what happens for `a = 1`, `b = 6`.
 *
 * See also https://en.wikipedia.org/wiki/Discrete_uniform_distribution
 */
static inline long cmb_random_dice(const long a, const long b)
{
    cmb_assert (a < b);

    const double x = (double)(b - a + 1) * cmb_random();
    return (long)(floor((double)a + x));
}

/**
 * @brief A non-uniform discrete distribution among `n` alternatives. It returns
 *        the selected array index `i` on `[0, n-1]` with a probability `pa[i]`.
 *
 * The probabilities in `pa[]` should sum to 1.0, i.e., mutually exclusive,
 * collectively exhaustive.
 *
 * This function uses a very simple O(n) implementation. For anything larger
 * than ~15 values, use the alias sampling method below instead.
 *
 * Both can easily be extended to arbitrary discrete values by letting the
 * result be an index into an array of whatever values need to be selected.
 *
 * See the implementations of the normal and exponential distributions in
 * `cmb_random.c` for an example of alias sampling from a table of values.
 *
 * @param n Number of entries
 * @param pa The probabilities for each of the entries, array size ``n``
 *
 */
extern unsigned cmb_random_loaded_dice(unsigned n, const double *pa);

/**
 * @brief Alias table using integer encoding of the probabilities for fast
 *        look-up
 */
struct cmb_random_alias {
    unsigned n;         /**< The number of entries */
    uint64_t *uprob;    /**< Probabilities encoded as unsigned 64-bit integers */
    unsigned *alias;    /**< Alias indexes */
};

/**
 * @brief Create a look-up table for alias sampling, where `n` is the
 *        number of entries.
 *
 * `cmb_random_alias_create()` allocates and returns a look-up table of
 * `(prob, alias)` pairs, `cmb_random_alias_draw()` samples it efficiently as
 * many times as needed, `cmb_random_alias_destroy()` frees the memory when
 * finished.
 *
 * @param n Number of entries
 * @param pa The probabilities for each of the entries, array size ``n``
 *
 * @return Pointer to an allocated and initialized `cmb_random_alias` look-up
 *         table.
 */

extern struct cmb_random_alias *cmb_random_alias_create(unsigned n,
                                                        const double *pa);

/**
 * @brief Perform  alias sampling, a more efficient way of sampling a non-uniform
*        discrete distribution of `n` alternatives. Returns values on
*        `[0, (pa->n) - 1]`, typically used for array indices and the like.
 *
 * Does the same as `cmb_random_loaded_dice()`, but at O(1) in each draw, at the
 * cost of an initial O(n) initialization by `cmb_random_alias_create()`.
 *
 * See also  https://en.wikipedia.org/wiki/Alias_method \n
 * https://pbr-book.org/4ed/Sampling_Algorithms/The_Alias_Method \n
 * or (especially)  https://www.keithschwarz.com/darts-dice-coins/
 *
 * Call `cmb_random_alias_create()` first to allocate and return a look-up
 * table of `(prob, alias)` pairs, `cmb_random_alias_destroy()` to free the
 * memory when finished.
 *
 * @param ap Pointer to an allocated and initialized `cmb_random_alias` look-up
 *           table.
 * @return A randomly chosen index into the table, selected with a probability
 *        `pa[i]`

 */
static inline unsigned cmb_random_alias_sample(const struct cmb_random_alias *ap)
{
    cmb_assert_release(ap != NULL);

    const unsigned idx = (unsigned) (floor(ap->n * cmb_random()));
    const bool c = (cmb_random_sfc64() >= ap->uprob[idx]);
    const unsigned r = (c) ? ap->alias[idx] : idx;

    cmb_assert_debug(r < ap->n);
    return r;
}

/**
 * @brief Destroy alias lookup table created by `cmb_random_alias_create()`
 *        after `cmb_random_alias_draw()` is finished using it.
 *
 * @param ap Pointer to an allocated `cmb_random_alias` look-up table.
 */
extern void cmb_random_alias_destroy(struct cmb_random_alias *ap);

#endif /* CIMBA_CMB_RANDOM_H */