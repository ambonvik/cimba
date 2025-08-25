/* 
 * cmb_random.h - pseudo-random number generators and distributions.
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

#ifndef CIMBA_CMB_RANDOM_H
#define CIMBA_CMB_RANDOM_H
#include <inttypes.h>
#include <math.h>

#include "cmb_config.h"
#include "cmb_logger.h"

/*
 * Initiate pseudo-random number distribution using a 64-bit seed. Will be
 * bootstrapped internally to a 256-bit state by an auxiliary pseudo-random
 * number generator only used for this purpose. Call before any sampling.
 * Can be called again later to reset seed to initial (or some other) state.
 */
extern void cmb_random_init(uint64_t seed);

/*
 * Main pseudo-random number generator - 64 bits output, 256 bits state.
 * An implementation of Chris Doty-Humphrey's sfc64. Fast and really good.
 * Public domain, see https://pracrand.sourceforge.net
 */
extern uint64_t cmb_random_sfc64(void);


/*
 * Get pseudo-random number uniformly distributed on interval [0, 1).
 */
inline double cmb_random(void) {
    return ldexp((double)(cmb_random_sfc64() >> 11), -53);
}

/*
 * Continuous uniform distribution on the open interval [l, r).
 * Often used in lack of any other information about a distribution than
 * the endpoints. Assuming uniform in between may then be reasonable.
 */
inline double cmb_random_uniform(const double l, const double r) {
    return l + (r - l) * cmb_random();
}

/*
 * Triangular distribution on the interval [l, r) with mode m,
 * where l < m < r. The probability density function is zero at l and r,
 * and reaches a maximum of 2/(r-l) at m. The mean is (l + r + m)/3.
 * Another "I don't know much about the shape of this thing" distribution.
 * (See also the PERT distribution cmb_random_PERT()
 */
extern double cmb_random_triangular(double l, double m, double r);

/*
 * Normal distribution on (-oo, oo) with mean m and standard deviation s
 * where s > 0. cmb_random_normal_std() is standard normal distribution N(0,1).
 * Often used to model measurement errors or process variation.
 * It tends to appear whenever the variation is caused by a sum (or average)
 * of many small effects, according to the Central Limit Theorem.
 * Uses an implementation of McFarland's improved ziggurat method,
 * with the hot path inlined here.
 */
extern const uint8_t cmi_random_nor_zig_max;
extern const double cmi_random_nor_zig_pdf_x[];
extern double cmi_random_nor_not_hot(int64_t icx);

inline double cmb_random_std_normal(void) {
    uint64_t bits = cmb_random_sfc64();
    const int64_t i_cand_x = *(int64_t *) &bits;
    const uint8_t idx = i_cand_x & 0xFF;
    return (idx <= cmi_random_nor_zig_max) ?
        cmi_random_nor_zig_pdf_x[idx] * (double) i_cand_x :
        cmi_random_nor_not_hot(i_cand_x);
}

inline double cmb_random_normal(const double mu, const double sigma) {
    return mu + sigma * cmb_random_std_normal();
}

/*
 * Lognormal distribution on [0, oo) with parameters m and s, where m > 0
 * and s > 0. Mean exp(m + 0.5 s^2), median exp(m). 
 * Occurs naturally for effects that are the product of many small non-negative
 * sources of variation, including multiplicative measurement errors.
 */
inline double cmb_random_lognormal(const double m, const double s) {
    return exp(cmb_random_normal(m, s));
}

/*
 * Logistic distribution with location m and scale s. Similar to normal
 * distribution, but with fatter tails. Mean = median = mode = m.
 */
inline double cmb_random_logistic(const double m, const double s) {
    const double x = cmb_random();
    return m + s * log(x / (1.0 - x));
}

/*
 * Cauchy distribution - the canonical fat-tailed distribution.abort
 * The mean, variance and all higher moments are undefined.
 * Occurs e.g. as the incidence of rays from a point source onto a line.
 * Substituting it for a normal distribution in some financial model gives
 * black swan events galore. Also known as the Lorentz distribution, or the 
 * Witch of Agnesi. It is evil. Mostly useful as a practical joke, or as a
 * patological test case to break assumptions.
 */
 inline double cmb_random_cauchy(const double mode, const double scale) {
     double y;
     const double x = cmb_random_std_normal();
     while ((y = cmb_random_std_normal()) == 0.0) {}
     return mode + scale * x / y;
 }

/*
 * Exponential distribution on [0, oo) with mean m, where m > 0.
 * Corresponds to a rate r = 1/m (but avoids a division in each call).
 * Used for modeling time intervals between successive events, such as
 * customer inter-arrival times, service times, times to fail or repair, 
 * state transition times in Markov chains. Uses an implementation of
 * McFarland's improved ziggurat method, with the hot path inlined here.
 */
extern const uint8_t cmi_random_exp_zig_max;
extern const double cmi_random_exp_zig_pdf_x[];
extern double cmi_random_exp_not_hot(uint64_t ucx);

inline double cmb_random_std_exponential(void) {
    const uint64_t u_cand_x = cmb_random_sfc64();
    const uint8_t idx = u_cand_x & 0xff;
    return (idx <= cmi_random_exp_zig_max) ?
        cmi_random_exp_zig_pdf_x[idx] * (double) u_cand_x :
        cmi_random_exp_not_hot(u_cand_x);
}

inline double cmb_random_exponential(const double mean) {
    return mean * cmb_random_std_exponential();
}

/*
 * Erlang distribution on [0, oo), a sum of k exponentially distributed
 * random variables each with mean m, where k > 0 and m > 0.
 * The mean is k m, the variance k m^2.
 * Used for modeling more complex service times than the simple exponential.
 */
inline double cmb_random_erlang(const unsigned k, const double m) {
    cmb_assert (m > 0.0);
    double x = 0.0;
    for (unsigned i = 0; i < k; i++)
        x += cmb_random_exponential(m);
    return x;
}

/*
 * Hypoexponential on [0, oo), a sum of n exponentially distributed
 * random variables each with mean m[i], where n > 0 and m[i] > 0.
 * Slight generalization of the Erlang distribution by allowing each
 * summed item its own exponential parameter. Will give lower coefficent of
 * variation than a single exponential, hence the name.
 * The mean equals the sum of m[i], the variance the sum of m[i]^2.
 */
inline double cmb_random_hypoexponential(const unsigned n, const double m_arr[n]) {
    double x = 0.0;
    for (unsigned i = 0; i < n; i++)
        x += cmb_random_exponential(m_arr[i]);
    return x;
}

/*
 * Hyperexponential on [0, oo), choosing and samples one of n exponential
 * distributions. The probability of selecting distribution i is p_arr[i],
 * the mean of that distribution is m_arr[i].
 * The overall mean is the sum of p_arr[i] * m_arr[i], the variance a more
 * complicated sum of terms, see
 *  https://en.wikipedia.org/wiki/Hyperexponential_distribution
 *
 * Assumes that p_arr sums to 1.0. Uses a simple O(n) implementation.
 * If n is large and speed is important, consider using O(1) alias sampling to
 * select the distribution instead of using this function.
 */
extern double cmb_random_hyperexponential(unsigned n, const double m_arr[n], const double p_arr[n]);

/*
 * Gamma distribution on (0, oo) with shape parameter alpha and scale parameter
 * theta, both > 0. Generalizes the Erlang distribution to non-integer n (shape).
 * Used for various servicing, waiting, and repair times in queuing systems,
 * along the exponential and Erlang distributions.
 * The mean is shape * scale, variance shape * scale^2.
 */
extern double cmb_random_std_gamma(double shape);

inline double cmb_random_gamma(const double shape, const double scale) {
    cmb_assert(shape > 0);
    return (shape >= 1.0) ?
        scale * cmb_random_std_gamma(shape) :
        scale * (cmb_random_std_gamma(shape + 1.0) * pow(cmb_random(), 1.0 / shape));
}

/*
 * Beta distribution on the interval [0, 1) with real-valued shape
 * parameters a and b, where a > 0 and b > 0. The mean is a/(a + b).
 * Used to model various proportions and percentages of something.
 */
inline double cmb_random_std_beta(const double a, const double b) {
    cmb_assert(a > 0.0 && b > 0.0);
    double x = cmb_random_std_gamma(a);
    double y = cmb_random_std_gamma(b);
    return x / (x + y);
}

/* Shifted and scaled beta to arbitrary interval [l, r) */
inline double cmb_random_beta(double a, double b, double l, double r) {
    return l + (r - l) * cmb_random_std_beta(a, b);
}

/*
 * PERT and modified PERT distributions
 * Scaled and shifted beta distributions to get a mean at m.
 * Can be used as an heuristically determined distribution where the parameters
 * are "at least l", "most likely m", and "not more than r". The additional
 * parameter lambda determines the peakiness around m, with lambda = 4.0 the default.
 */
extern double cmb_random_PERT_mod(double l, double m, double r, double lambda);

inline double cmb_random_PERT(const double l, const double m, const double r) {
    return cmb_random_PERT_mod(l, m, r, 4.0);
}

/*
 * Weibull distribution on (0, oo) with parameters shape and scale, where shape > 0 and
 * scale > 0. Generalizes the exponential distribution, typically used for component
 * lifetimes and similar durations. Equal to exponential with mean scale when shape = 1.0.
 * Failure rates increase with time for shape < 1, decrease with time for shape > 1.
 * Looks like a normal distribution for shape around 4.
  */
inline double cmb_random_weibull(const double shape, const double scale) {
    return scale * pow(cmb_random_exponential(1.0), 1.0 / shape);
}

/*
 * Pareto distribution (power law) on (1, oo) with parameter a > 0.
 * Used to model e.g the size of human settlements (hamlets to cities),
 * size of (extreme) weather events, human income and wealth, etc.
 * a = log4(5) = ln(5)/ln(4) = 1.16 gives the 80:20 rule. 
 * Higher values of the parameter give steeper distributions.
 */
inline double cmb_random_pareto(const double shape, const double scale) {
    return scale / pow(cmb_random(), 1.0 / shape);
}

/*
 * Chi-square distribution on [0, oo), modeling the sum k squared standard
 * normal distributions N(0, 1). Mean k, variance 2 k. Used to model sample
 * variances for normally distributed samples.
 */
inline double cmb_random_chisquare(const double v) {
    return cmb_random_gamma(v / 2.0, 2.0);
}

/*
 * f distribution for ratios of sample variances, parameters a and b for
 * numerator and denominator degrees of freedom, respectively. Probably not very
 * useful in a discrete event simulation context, included for completeness.
 */
inline double cmb_random_f_dist(const double a, const double b) {
    cmb_assert((a > 0.0) && (b > 0.0));
    double y;
    const double x = cmb_random_chisquare(a) / a;
    while ((y = cmb_random_chisquare(b) / b) == 0.0) {}
    return x / y;
}

/*
 * Student's t-distribution for confidence intervals and t-tests.
 * Can also be used as a generic fat-tailed alternative to the standard
 * normal distribution, where the degree of fat-tailedness depends on v,
 * converging to a normal distribution as v -> oo.
 * Mean 0.0 for v > 1.0, variance v / (v - 2.0) for v > 2.0,
 * otherwise undefined.
 */
inline double cmb_random_std_t_dist(const double v) {
    cmb_assert(v > 0.0);
    double y;
    const double x = cmb_random_std_normal();
    while ((y = cmb_random_chisquare(v)) == 0.0) {}
    return x / sqrt( y / v);
}

/*
 * Location - scale generalization of t distribution.
 * Mean m for v > 1.0, variance s * s * v /(v - 2.0) for v > 2.0,
 * otherwise undefined. Can be used as a drop-in replacement for
 * normal distributions if fatter tails are needed. Converges to
 * a normal distribution N(m, s) for v -> oo.
 */
inline double cmb_random_t_dist(const double m, const double s, const double v) {
    return m + s * cmb_random_std_t_dist(v);
}

/*
 * A single flip of an unbiased coin. Returns 1 with p = 0.5, 0 with same.
 * Equivalent to cmb_random_bernoulli(0.5), but optimized for speed, only consuming
 * one bit of randomness for each trial by caching random bits every 64 calls.
 */
extern int cmb_random_flip();

/*
 * A single Bernoulli trial. Returns 1 with probability p, otherwise 0.
 * 0 < p < 1. Used for any binary yes/no outcome of independent and
 * identically distributed trials. A fair coin flip if p = 0.5.
 */
inline unsigned cmb_random_bernoulli(const double p) {
    cmb_assert ((p > 0.0) && (p <= 1.0));
    return (cmb_random() <= p) ? 1 : 0;
}

/*
 * Geometric distribution, a discrete parallel to the exponential distribution.
 * Models the number of trials up to and including the first success in a
 * series of consecutive Bernoulli trials with probability p of success.
 * I.e., returns an integer value in [1, oo)
 */
extern unsigned cmb_random_geometric(double p);

/*
 * Binomial distribution, number of successes in n independent Bernoulli trials
 * each with probability p. Models a drawing process with replacement (or from
 * an infinite pool). Mean np, variance np(1-p).
 */
extern unsigned cmb_random_binomial(unsigned n, double p);

/*
 * Negative binomial distribution, number of failures before the m'th success
 * in independent Bernoulli trials each with probability p, sampled with
 * replacement. Equal to geometric distribution if m = 1. Used to model e.g the
 * number of bits (or packets) that need to be sent to successfully transmit a
 * r-bit (or -packet) message. Also known as the Pascal distribution, aliased here.
 * Mean m(1-p)/p, variance m(1-p)/p^2.
 */
extern unsigned cmb_random_negative_binomial(unsigned m, double p);

inline unsigned cmb_random_pascal(const unsigned m, const double p) {
    return cmb_random_negative_binomial(m, p);
}

/*
 * Poisson distribution, number of arrivals per unit time in a Poisson process
 * with arrival rate r, where r > 0. Mean r, variance r, interarrival times
 * exponentially distributed with mean 1/r. Models shot noise, customer
 * arrivals, incoming calls, Geiger counter clicks, etc.
 */
extern unsigned cmb_random_poisson(double r);

/*
 * A discrete uniform distribution on [a, a+1, a+2, ..., b] for a < b.
 * The function name reflects what happens for a = 1, b = 6.
 */
static inline long cmb_random_dice(const long a, const long b) {
    cmb_assert (a < b);
    return (long)(floor((double)(a + (b - a + 1) * cmb_random())));
}

/*
 * A non-uniform discrete distribution among n alternatives. It returns the
 * selected array index i on [0, n-1] with probability p[i].
 *
 * Very simple O(n) implementation. For anything larger than ~15 values,
 * use the alias sampling method below instead.
 */
extern unsigned cmb_random_loaded_dice(unsigned n, const double p[n]);

/*
 * Alias sampling, more efficient way of sampling a non-uniform discrete
 * distribution of n alternatives. Does the same thing as cmb_random_loaded_dice,
 * but at O(1) in each draw, at the cost of an initial O(n) initialization.
 * See, e.g., https://pbr-book.org/4ed/Sampling_Algorithms/The_Alias_Method
 * or (especially)  https://www.keithschwarz.com/darts-dice-coins/
 *
 * cmb_random_alias_create() allocates and returns an array of (prob, alias) pairs,
 * cmb_random_alias_draw() samples it efficiently as many times as needed,
 * cmb_random_alias_destroy() frees the memory when finished.
 */

/* Alias table using integer encoding of the probabilities for fast lookup */
struct cmb_random_alias {
    unsigned n;
    uint64_t *uprob;
    unsigned *alias;
};

/*
 * Create alias lookup table, where n is the number of entries.
 * Allocates memory, remember to use a matching cmb_random_alias_destroy to free it
 * when the distribution is no longer needed.
 */
extern struct cmb_random_alias *cmb_random_alias_create(unsigned n, const double p_arr[n]);

/*
 * Alias sampling, combining one fair die throw and one biased coin toss per sample.
 * Returns values on [0, (pa->n) - 1], typically used for array indices and the like.
 */
inline unsigned cmb_random_alias_sample(const struct cmb_random_alias *pa) {
    cmb_assert (pa != NULL);
    unsigned idx = (unsigned) (floor(pa->n * cmb_random()));
    unsigned ret = (cmb_random_sfc64() >= pa->uprob[idx]) ? pa->alias[idx] : idx;
    return ret;
}

/* Destroy alias lookup table */
extern void cmb_random_alias_destroy(struct cmb_random_alias *pa);

#endif /* CIMBA_CMB_RANDOM_H */
