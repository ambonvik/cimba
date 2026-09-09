/*
 * Utility functions for test scripts, including statistical tests for
 * goodness of fit to U(0,1) distribution and two-sample tests.
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
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "cmb_assert.h"
#include "cmb_dataset.h"

#include "testutils.h"

/**** General utilities *****/

void cmi_test_fnprint_line(FILE *fp, const char *str, const unsigned n)
{
    cmb_assert_debug(fp != NULL);
    cmb_assert_debug(str != NULL);

    const unsigned len = strlen(str);
    cmb_assert_debug(len > 0u);

    const unsigned repeats = n / len;
    for (unsigned ui = 0u; ui < repeats; ui++) {
        fprintf(fp, "%s", str);
    }

    if ((len > 1) && (len * repeats < n)) {
        const unsigned rem = n - len * repeats;
        for (unsigned ui = 0u; ui < rem; ui++) {
            fputc(str[ui], fp);
        }
    }

    printf("\n");
}

static unsigned clamp_u(const uint64_t v, const unsigned lo, const unsigned hi)
{
    if (v < (uint64_t)lo) { return lo; }
    if (v > (uint64_t)hi) { return hi; }
    return (unsigned)v;
}

/**** General mathematical and statistical utilities ****/

/* log(1 - exp(x)) for x < 0, accurate at both ends. */
static double log1mexp(const double x)
{
    double ret = 0.0;
    if (x >= 0.0) {
        /* Clamp it instead of asserting, may be a numerical precision issue */
        ret = -INFINITY;
    }
    else if (x > -M_LN2) {
        /* x near 0.0, exp(x) near 1.0. Calculate as log(-(exp(x) - 1)) */
        ret = log(-expm1(x));
    }
    else {
        /* x << 0, exp(x) near 0.0. Calculate as log(1 + (-exp(x))) */
        ret = log1p(-exp(x));
    }

    return ret;
}

/* Orthogonalize basis vectors by a modified Gram-Schmidt process, see
 * https://en.wikipedia.org/wiki/Gram%E2%80%93Schmidt_process#Numerical_stability */
static void mod_gram_schmidt(double *basis, const unsigned k, const unsigned n)
{
    cmb_assert_debug(basis != NULL);
    cmb_assert_debug(k < n);

    /* Initialize */
    for (unsigned i = 0; i < n; i++) {
        const double xj = 2.0 * ((double)i + 0.5) / (double)n - 1.0;
        double p = 1.0;
        for (unsigned j = 0; j <= k; j++) {
            basis[i * (k + 1) + j] = p;
            p *= xj;
        }
    }

    /* Orthogonalize */
    for (unsigned l = 0; l <= k; l++) {
        for (unsigned j = 0; j < l; j++) {
            double dot = 0.0;
            for (unsigned i = 0; i < n; i++) {
                dot += basis[i * (k + 1) + j] * basis[i * (k + 1) + l];
            }

            for (unsigned i = 0; i < n; i++) {
                basis[i * (k + 1) + l] -= dot * basis[i * (k + 1) + j];
            }
        }

        double nrm = 0.0;
        for (unsigned i = 0; i < n; i++) {
            nrm += basis[i * (k + 1) + l] * basis[i * (k + 1) + l];
        }

        nrm = sqrt(nrm);
        for (unsigned i = 0; i < n; i++) {
            basis[i * (k + 1) + l] /= nrm;
        }

        if (l > 0u) {
            /* Normalize, ensuring correct sign */
            double s = 0.0;
            for (unsigned i = 0; i < n; i++) {
                const double xi = 2.0 * ((double)i + 0.5) / (double)n - 1.0;
                double xp = 1.0;
                for (unsigned e = 0; e < l; e++) {
                    xp *= xi;
                }
                s += basis[i * (k + 1) + l] * xp;
            }
            if (s < 0.0) {
                for (unsigned i = 0; i < n; i++) {
                    basis[i * (k + 1) + l] = -basis[i * (k + 1) + l];
                }
            }
        }
    }

    /* Validate correctness */
    for (unsigned a = 1; a <= k; a++) {
        double col = 0.0;
        for (unsigned i = 0; i < n; i++) {
            col += basis[i * (k + 1) + a];
        }

        cmb_assert_debug(fabs(col) < 1e-12);
        for (unsigned b = 1; b <= k; b++) {
            double d = 0.0;
            for (unsigned i = 0; i < n; i++) {
                d += basis[i * (k + 1) + a] *basis[i * (k + 1) + b];
            }

            cmb_assert_debug(fabs(d - (a == b ? 1.0 : 0.0)) < 1e-12);
        }
    }
}

/**** Normal distribution utilities ****/

/* ppnd16 - percentage points of the normal distribution, 16 significant digits.
 * I.e., the inverse CDF of the standard normal distribution, returning the x
 * value that gives probability p starting from the negative tail. For the
 * standard normal distribution, x is also the number of standard deviations, so
 * this function will return the number of standard deviations for any normal
 * distribution, to be scaled appropriately by the user code.
 *
 * Based on Michael J Wichura (1988); "Algorithm AS 241: The Percentage Points
 * of the Normal Distribution", Journal of the Royal Statistical Society, series
 * C (applied Statistics), Vol 31, No 3.
 * https://csg.sph.umich.edu/abecasis/gas_power_calculator/algorithm-as-241-the-percentage-points-of-the-normal-distribution.pdf
 */
static double ppnd16(const double p)
{
    cmb_assert_debug((p >= 0.0) && (p <= 1.0));

    static const double split_1 = 0.425e0;
    static const double split_2 = 5.0e0;
    static const double const_1 = 0.180625e0;
    static const double const_2 = 1.6e0;

    /* Coefficients for p around 0.5 */
    static const double a[8] = {
        3.3871328727963666080e0,
        1.3314166789178437745e2,
        1.9715909503065514427e3,
        1.3731693765509461125e4,
        4.5921953931549871457e4,
        6.7265770927008700853e4,
        3.3430575583588128105e4,
        2.5090809287301226727e3
    };
    static const double b[8] = {
        1.0000000000000000000e0,
        4.2313330701600911252e1,
        6.8718700749205790830e2,
        5.3941960214247511077e3,
        2.1213794301586595867e4,
        3.9307895800092710610e4,
        2.8729085735721942674e4,
        5.2264952788528545610e3
    };

    /* Coefficients for p away from mid- and endpoints */
    static const double c[8] = {
        1.42343711074968357734e0,
        4.63033784615654529590e0,
        5.76949722146069140550e0,
        3.64784832476320460504e0,
        1.27045825245236838258e0,
        2.41780725177450611770e-1,
        2.27238449892691846833e-2,
        7.74545014278341407640e-4
    };
    static const double d[8] = {
        1.00000000000000000000e0,
        2.05319162663775882187e0,
        1.67638483018380384940e0,
        6.89767334985100004550e-1,
        1.48103976427480074590e-1,
        1.51986665636164571966e-2,
        5.47593808499534494600e-4,
        1.05075007164441684324e-9
    };

    /* Coefficients for p close to 0 or 1 */
    static const double e[8] = {
        6.65790464350110377720e0,
        5.46378491116411436990e0,
        1.78482653991729133580e0,
        2.96560571828504891230e-1,
        2.65321895265761230930e-2,
        1.24266094738807843860e-3,
        2.71155556874348757815e-5,
        2.01033439929228813265e-7
    };
    static const double f[8] = {
        1.00000000000000000000e0,
        5.99832206555887937690e-1,
        1.36929880922735805310e-1,
        1.48753612908506148525e-2,
        7.86869131145613259100e-4,
        1.84631831751005468180e-5,
        1.42151175831644588870e-7,
        2.04426310338993978564e-15
    };

    double x = 0.0;
    const double q = p - 0.5;
    if (fabs(q) <= split_1) {
        const double r = const_1 - q * q;
        const double num = q * (((((((a[7] * r + a[6]) * r + a[5]) * r + a[4]) * r + a[3]) * r + a[2]) * r + a[1]) * r + a[0]);
        const double den = ((((((b[7] * r + b[6]) * r + b[5]) * r + b[4]) * r + b[3]) * r + b[2]) * r + b[1]) * r + b[0];
        x =  num / den;
    }
    else {
        double r = (q < 0.0) ? p : 1.0 - p;
        cmb_assert_debug(r > 0.0);
        r = sqrt(-log(r));
        if (r <= split_2) {
            r = r - const_2;
            const double num = ((((((c[7] * r + c[6]) * r + c[5]) * r + c[4]) * r + c[3]) * r + c[2]) * r + c[1]) * r + c[0];
            const double den = ((((((d[7] * r + d[6]) * r + d[5]) * r + d[4]) * r + d[3]) * r + d[2]) * r + d[1]) * r + d[0];
            x = num / den;
        }
        else {
            r = r - split_2;
            const double num = ((((((e[7] * r + e[6]) * r + e[5]) * r + e[4]) * r + e[3]) * r + e[2]) * r + e[1]) * r + e[0];
            const double den = ((((((f[7] * r + f[6]) * r + f[5]) * r + f[4]) * r + f[3]) * r + f[2]) * r + f[1]) * r + f[0];
            x = num / den;
        }

        if (q < 0.0) {
            x = -x;
        }
    }

    return x;
}

/**** Chi squared utilities ****/

/****
 * Log space chi squared CDF. When handling actual deviations, the sample could
 * be extremely unlikely under the null hypothesis ~U(0,1).  We need a way to
 * express these very small probabilities without underflowing to zero, also
 * because we want to state the odds 1/p even for very small p. So we express
 * the chi squared CDF in log(p) to capture the far tails. Several functions.
 ****/

/* Calculate log(P(a,x)) as incomplete gamma series
 * sum_{n>=0} x^n / (a(a+1)...(a+n)), valid for x < a+1. */
static double igamma_log_series(const double a, const double x)
{
    const int nterms = 100000;
    double ap = a;
    double term = 1.0 / a;
    double sum = term;

    for (int i = 0; i < nterms; i++) {
        ap += 1.0;
        term *= x / ap;
        sum += term;
        if (fabs(term) < fabs(sum) * DBL_EPSILON) {
            /* No use in continuing */
            break;
        }
    }

    return log(sum);
}

/* Calculate log(Q(a,x)) as continued fraction, valid for x >= a+1, using the
 * modified Lentz algorithm. */
static double igamma_log_cf(const double a, const double x)
{
    const int nterms = 100000;
    const double tiny = 1.0e-300;

    double b = x + 1.0 - a;
    double c = 1.0 / tiny;
    double d = (fabs(b) < tiny) ? 1.0 / tiny : 1.0 / b;
    double h = d;

    for (int i = 1; i < nterms; i++) {
        const double an = -(double)i * ((double)i - a);
        b += 2.0;
        d = an * d + b;
        if (fabs(d) < tiny) {
            d = tiny;
        }
        c = b + an / c;
        if (fabs(c) < tiny) {
            c = tiny;
        }
        d = 1.0 / d;
        const double delta = d * c;
        h *= delta;
        if (fabs(delta - 1.0) < DBL_EPSILON) {
            /* No use in continuing */
            break;
        }
    }
    return log(h);
}

/* Regularized incomplete gamma in log space. Calculates both log(P(a,x)) and
 * log(Q(a,x)). Either pointer may be NULL but not both (that would be rather
 * useless). */
static void igamma_log(const double a, const double x, double *logp, double *logq)
{
    cmb_assert_debug(a > 0.0);
    cmb_assert_debug((logp != NULL) || (logq != NULL));

    double lp, lq;
    if (x <= 0.0) {
        /* Clamp it instead of asserting, may be a numerical precision issue */
        lp = -INFINITY;
        lq = 0.0;
    }
    else {
        const double pf = -x + a * log(x) - lgamma(a);
        if (x < a + 1.0) {
            /* Calculate P(a,x) as series */
            lp = pf + igamma_log_series(a, x);
            lq = log1mexp(lp);
        }
        else {
            /* Calculate Q(a,x) as continuing fraction */
            lq = pf + igamma_log_cf(a, x);
            lp = log1mexp(lq);
        }
    }

    if (logp != NULL) {
        *logp = lp;
    }

    if (logq != NULL) {
        *logq = lq;
    }
}

/* Regularized incomplete beta function I_x(a,b), evaluated in log space.
 *
 * Uses the continued fraction of Abramowitz & Stegun 26.5.8, evaluated by
 * the modified Lentz method, with the symmetry I_x(a,b) = 1 - I_{1-x}(b,a)
 * to stay in the region where the fraction converges quickly.
 *
 * Working in logs keeps tail probabilities far below DBL_MIN representable:
 * I_0.9(0.5, 50) has a complement of about e^-117, which as a linear double
 * would be indistinguishable from zero. */

/* Continued fraction for the regularized incomplete beta (modified Lentz).
 * Converges quickly for x < (a+1)/(a+b+2); the caller ensures this. */
static double ibeta_cf_log(const double a, const double b, const double x)
{
    const int itmax = 100000;
    const double tiny = 1.0e-300;

    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;

    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (fabs(d) < tiny) {
        d = tiny;
    }
    d = 1.0 / d;
    double h = d;

    for (int m = 1; m < itmax; m++) {
        const double dm = (double)m;
        const double m2 = 2.0 * dm;

        /* Even step */
        double aa = dm * (b - dm) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (fabs(d) < tiny) { d = tiny; }
        c = 1.0 + aa / c;
        if (fabs(c) < tiny) { c = tiny; }
        d = 1.0 / d;
        h *= d * c;

        /* Odd step */
        aa = -(a + dm) * (qab + dm) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (fabs(d) < tiny) { d = tiny; }
        c = 1.0 + aa / c;
        if (fabs(c) < tiny) { c = tiny; }
        d = 1.0 / d;
        const double del = d * c;
        h *= del;

        if (fabs(del - 1.0) < DBL_EPSILON) {
            break;
        }
    }

    return log(h);
}

/*
 * Writes log I_x(a,b) and log(1 - I_x(a,b)); either pointer may be NULL.
 * Requires a > 0, b > 0, 0 <= x <= 1.
 */
void ibeta_log(const double a, const double b, const double x,
               double *logp, double *logq)
{
    cmb_assert_debug((a > 0.0) && (b > 0.0));
    cmb_assert_debug((x >= 0.0) && (x <= 1.0));

    double lp;
    double lq;

    if (x <= 0.0) {
        lp = -INFINITY;
        lq = 0.0;
    }
    else if (x >= 1.0) {
        lp = 0.0;
        lq = -INFINITY;
    }
    else {
        const double lbeta = lgamma(a) + lgamma(b) - lgamma(a + b);
        const double y = 1.0 - x;
        const double pfwd = a * log(x) + b * log1p(-x) - log(a) - lbeta;
        const double prev = b * log(y) + a * log1p(-y) - log(b) - lbeta;

        if (x < (a + 1.0) / (a + b + 2.0)) {
            lp = pfwd + ibeta_cf_log(a, b, x);
            if (lp > -1.0e-8) {
                /* I is indistinguishable from 1; get the small tail directly
                 * rather than by a complement that has nothing left to cancel */
                lq = prev + ibeta_cf_log(b, a, y);
                lp = log1mexp(lq);
            }
            else {
                lq = log1mexp(lp);
            }
        }
        else {
            lq = prev + ibeta_cf_log(b, a, y);
            if (lq > -1.0e-8) {
                lp = pfwd + ibeta_cf_log(a, b, x);
                lq = log1mexp(lp);
            }
            else {
                lp = log1mexp(lq);
            }
        }
    }

    if (logp != NULL) { *logp = lp; }
    if (logq != NULL) { *logq = lq; }
}

/* Chi-square tails in log space; DoF may be odd or even, no special cases needed. */
CMB_MAYBE_UNUSED
static double chisq_logcdf(const double x2, const double dof)
{
    double lp;
    igamma_log(0.5 * dof, 0.5 * x2, &lp, NULL);

    return lp;
}

CMB_MAYBE_UNUSED
static double chisq_logsf(const double x2, const double dof)
{
    double lq;
    igamma_log(0.5 * dof, 0.5 * x2, NULL, &lq);

    return lq;
}

/* Convert log(p) to unsigned sigma magnitude for the smaller tail. The caller
 * must keep track of which tail it is to convert the result to a signed sigma. */
static double logp_to_sigma(const double logp)
{
    /* Argument must be the smaller tail */
    cmb_assert_debug(logp <= -M_LN2);

    double xm;
    if (logp >= 0.0) {
        /* Clamp it */
        xm = 0.0;
    }
    else if (logp > -6.0) {
        /* p is comfortably representable; use the quantile inverse CDF.
         * ppnd16 starts from the lower tail, returning a negative value for
         * small p, and we only want the magnitude, so drop the minus sign. */
        xm = -ppnd16(exp(logp));
    }
    else {
        /* Asymptotic inversion of Q(z) ~ exp(-z^2/2)/(z*sqrt(2*pi)). */
        const double L = -logp;
        double z2 = 2.0 * L;
        for (int i = 0; i < 4; i++) {
            z2 = 2.0 * L - log(2.0 * M_PI * z2);
            if (z2 < DBL_EPSILON) {
                z2 = DBL_EPSILON;
            }
        }

        xm = sqrt(z2);
    }

    return xm;
}

/* Signed sigma for a chi-square statistic: positive for too much deviation,
 * negative for suspiciously little. */
static double chisq_sigma(const double dof, const double x2)
{
    double p, q;
    igamma_log(0.5 * dof, 0.5 * x2, &p, &q);

    return (q < p) ? logp_to_sigma(q) : -logp_to_sigma(p);
}

/* log of the upper tail P(Z > z) of the standard normal. */
static double normal_logsf(const double z)
{
    double lq;
    igamma_log(0.5, 0.5 * z * z, NULL, &lq);
    double r;
    if (z >= 0.0) {
        /* Half of erfc(z/sqrt2) */
        r = lq - M_LN2;
    }
    else {
        /* 1 - upper tail at -z  */
        r = log1mexp(lq - M_LN2);
    }

    return r;
}

/* Fill residuals vector rv with (O_j - E)/sqrt(E), calculate and
 * return Pearson's chi-square statistic in the same pass. */
static double bin_residuals(const struct cmb_dataset *dsp,
                            const unsigned nr, double *rv)
{
    cmb_assert_debug(dsp != NULL);
    cmb_assert_debug(rv != NULL);

    uint64_t *bins = cmi_calloc(nr, sizeof(*bins));

    const uint64_t un = dsp->count;

    for (uint64_t ui = 0; ui < un; ui++) {
        unsigned bin = (unsigned)(dsp->xa[ui] * (double)nr);
        if (bin >= nr) {
            /* x == 1.0 belongs in the top bin here, intentionally different
             * from the cmb_dataset_histogram bins where it would be placed in
             * the [1.0, oo) overflow bin. We are testing against ~U[0,1], not
             * ~U[0,1) also to allow for samples drawn as x = 1 - cmb_random()
             * to avoid the possibility of an exact zero value. Then the
             * possibility of an exact 1.0 comes instead. Allow both. */
            bin = nr - 1u;
        }
        bins[bin]++;
    }

    const double e  = (double)un / (double)nr;
    const double se = sqrt(e);
    double x2 = 0.0;
    for (unsigned i = 0; i < nr; i++) {
        rv[i] = ((double)bins[i] - e) / se;
        x2  += rv[i] * rv[i];
    }

    cmi_free(bins);

    return x2;
}

/*
 * Anderson-Darling limiting distribution, following G Marsaglia and
 * J C W Marsaglia (2004), "Evaluating the Anderson-Darling Distribution",
 * Journal of Statistical Software Vol. 9 (2004), Issue 2.
 * https://www.jstatsoft.org/article/view/v009i02
 */
static double ad_f(const double z, const uint64_t uj)
{
    const double tu = (double)(4u * uj + 1u);
    const double tj = tu * tu * M_PI * M_PI / (8.0 * z);

    const double c0 = M_PI * exp(-tj) / sqrt(2.0 * tj);
    const double c1 = M_PI * sqrt(0.5 * M_PI) * erfc(sqrt(tj));
    const double z_8 = z / 8.0;


    double sum = c0 + c1 * z_8;
    double cn = c1;
    double cnm1 = c0;
    double p = z_8;

    for (uint64_t un = 1u; un < 1000u; un++) {
        const double dn = (double)un;
        const double cnp1 = ((dn - 0.5 - tj) * cn + tj * cnm1) / dn;
        p *= z_8 / (dn + 1.0);
        const double term = cnp1 * p;
        if (fabs(term) < fabs(sum) * DBL_EPSILON) {
            break;
        }

        sum += term;
        cnm1 = cn;
        cn = cnp1;
    }

    return sum;
}

double ad_inf(const double z)
{
    double b = 1.0;
    double sum = ad_f(z, 0u);
    for (uint64_t ui = 1u; ui < 100u; ui++) {
        b *= -(2.0 * (double)ui - 1.0) / (2.0 * (double)ui);
        const double term = b * (4.0 * (double)ui + 1.0) * ad_f(z, ui);
        if (fabs(term) < fabs(sum) * DBL_EPSILON) {
            break;
        }

        sum += term;
    }

    const double ret = sum / z;

    cmb_assert_debug((ret >= 0.0) && (ret <= 1.0));
    return ret;
}

double ad_errfix(const uint64_t un, const double x)
{
    const double dn = (double)un;
    const double cn = 0.01265 + 0.1757 / dn;

    double fix = 0.0;
    if (x < cn) {
        const double r = x / cn;
        const double g1 = sqrt(r) * (1.0 - r) * (49.0 * r - 102.0);
        fix = (0.0037 / (dn * dn * dn) + 0.00078 / (dn * dn) + 0.00006 / dn) * g1;
    }
    else if (x < 0.8) {
        const double r = (x - cn) / (0.8 - cn);
        const double g2 = -0.00022633 + (6.54034 - (14.6538 - (14.458 - (8.259 - 1.91864 * r) * r) * r) * r) * r;
        fix = (0.4213 / dn + 0.01365 / (dn * dn)) * g2;
    }
    else {
        const double g3 = -130.2137 + (745.2337 - (1705.091 - (1950.646 - (1116.360 - 255.7844 * x) * x) * x) * x) * x;
        fix = g3 / dn;
    }

    cmb_assert_debug(!isinf(fix) && !isnan(fix));
    return fix;
}

/**** Hypothesis tests ****/

/*
 * Perform a Pearson chi squared test on the dataset residuals vector, using the
 * specified number of bins. Should have at least 10 samples per bin on average,
 * i.e.,  * dsp->count / num_bins > 10. Returns the log of the p-value to
 * capture values in the far tails without loss of numerical precision.
 */
static void pearson_chisquare_U01(const double *rv,
                                    const double x2,
                                    const unsigned num_bins,
                                    struct cmi_test_outcome *result)
{
    cmb_assert_debug(rv != NULL);
    cmb_assert_debug(x2 > 0.0);
    cmb_assert_debug(num_bins > 0u);
    cmb_assert_debug(result != NULL);

    double lp, lq;
    igamma_log(0.5 * (double)(num_bins - 1u), 0.5 * x2, &lp, &lq);

    /* One-sided tail, always <= log(0.5) since min(P,Q) <= 0.5 */
    double ltail = fmin(lp, lq);
    if (ltail > -M_LN2) {
        ltail = -M_LN2;
    }

    /* Return detailed results */
    cmb_assert_debug(result->nparts < CMI_TEST_U01_PARTS);
    struct cmi_test_partial *rp = &(result->p[result->nparts]);
    rp->name = "Pearson's chi squared test          ";
    rp->level = 0u;
    rp->v = x2;
    rp->e = (double)(num_bins - 1u);
    rp->lp = ltail;
    rp->s = (lq < lp) ?  logp_to_sigma(ltail) : -logp_to_sigma(ltail);
    result->nparts++;
}


/* Orthogonal basis vectors, only depending on the number of bins */
#define NEYMAN_K 4
static CMB_THREAD_LOCAL double *neyman_basis = NULL;
static CMB_THREAD_LOCAL unsigned neyman_m = 0u;

static const char *const nm[NEYMAN_K + 2] = {
    "Neyman V1: mean                 ",
    "Neyman V2: variance             ",
    "Neyman V3: skewness             ",
    "Neyman V4: kurtosis             ",
    "Neyman remainder: fine structure",
    "Neyman's smooth test combined       "
};

/* Perform a Neyman's smooth test on the dataset residuals vector, using the
 * specified number of bins. Returns the log of the Fisher-combined p-value to
 * capture values in the far tails without loss of numerical precision. */
static void neyman_smooth_U01(const double *rv,
                                const double x2,
                                const unsigned num_bins,
                                struct cmi_test_outcome *result)
{
    cmb_assert_debug(rv != NULL);
    cmb_assert_debug(x2 > 0.0);
    cmb_assert_debug(num_bins > 0u);
    cmb_assert_debug(result != NULL);

    if ((neyman_basis != NULL) && (neyman_m != num_bins)) {
        /* Different m, invalidate cache */
        cmi_free(neyman_basis);
        neyman_basis = NULL;
    }

    if (neyman_basis == NULL) {
        /* Lazy allocation and initialization of basis */
        neyman_m = num_bins;
        /* For now, memory will be leaked on thread exit, no corresponding free() call */
        neyman_basis = cmi_calloc(neyman_m * (NEYMAN_K + 1), sizeof(double));
        mod_gram_schmidt(neyman_basis, NEYMAN_K, num_bins);
    }

    /* Reserve a spot for the combined result */
    struct cmi_test_partial *rp_com = &(result->p[result->nparts++]);

    /* Work out the K tests mean, variance, skewness, and kurtosis */
    double sumv2 = 0.0;
    double logp[NEYMAN_K + 1];
    for (unsigned kk = 1u; kk <= NEYMAN_K; kk++) {
        double v = 0.0;
        for (unsigned i = 0; i < num_bins; i++) {
            v += neyman_basis[i * (NEYMAN_K + 1) + kk] * rv[i];
        }

        sumv2 += v * v;
        logp[kk - 1u] = fmin(M_LN2 + normal_logsf(fabs(v)), 0.0);

        cmb_assert_debug(result->nparts < CMI_TEST_U01_PARTS);
        struct cmi_test_partial *rp = &(result->p[result->nparts]);
        rp->name = nm[kk - 1u];
        rp->level = 1u;
        rp->v    = v;
        rp->e    = 0.0;            /* Each V_k is N(0,1) */
        rp->lp   = logp[kk - 1u];
        rp->s    = v;              /* Already on the sigma scale */
        result->nparts++;
    }

    /* Remainder: everything the first K components do not explain */
    const double rem = x2 - sumv2;
    const unsigned rdof = num_bins - 1u - NEYMAN_K;
    double rlp, rlq;
    igamma_log(0.5 * (double)rdof, 0.5 * rem, &rlp, &rlq);

    double rtail = fmin(rlp, rlq);
    if (rtail > -M_LN2) {
        rtail = -M_LN2;
    }

    logp[NEYMAN_K] = fmin(M_LN2 + rtail, 0.0);

    cmb_assert_debug(result->nparts < CMI_TEST_U01_PARTS);
    struct cmi_test_partial *rp = &(result->p[result->nparts]);
    rp->name = nm[NEYMAN_K];
    rp->level = 1u;
    rp->v = rem;
    rp->e = (double)rdof;
    rp->lp = logp[NEYMAN_K];
    rp->s  = (rlq < rlp) ?  logp_to_sigma(rtail) : -logp_to_sigma(rtail);
    result->nparts++;

    /* Combine the K+1 tests into one overall score */
    double fisher = 0.0;
    for (unsigned i = 0; i <= NEYMAN_K; i++) {
        fisher += -2.0 * logp[i];
    }

    double flp, flq;
    const unsigned fisher_dof = 2u * (NEYMAN_K + 1u);
    igamma_log(0.5 * (double)fisher_dof, 0.5 * fisher, &flp, &flq);
    double ftail = fmin(flp, flq);
    if (ftail > -M_LN2) {
        ftail = -M_LN2;
    }

    rp_com->name = nm[NEYMAN_K + 1];
    rp_com->level = 0u;
    rp_com->v = fisher;
    rp_com->e = 2.0 * (double)(NEYMAN_K + 1u);
    rp_com->lp = fmin(M_LN2 + ftail, 0.0);
    rp_com->s  = (flq < flp) ?  logp_to_sigma(ftail)  : -logp_to_sigma(ftail);

    /* Parseval's theorem must hold to a small rounding error */
    cmb_assert_debug(fabs(x2 - (sumv2 + rem)) < 1e-9 * x2);
}

/* Perform Anderson-Darling EDF test on the data set, sorting a copy */
static void anderson_darling_U01(const struct cmb_dataset *dsp,
                                 struct cmi_test_outcome *result)
{
    cmb_assert_debug(dsp != NULL);
    cmb_assert_debug(result != NULL);

    /* Ensure that we have a sorted data array, x1 <= x2 <= ... xn */
    struct cmb_dataset tmp = { 0 };
    cmb_dataset_initialize(&tmp);
    cmb_dataset_copy(&tmp, dsp);
    cmb_dataset_sort(&tmp);

    /* Calculate the Anderson-Darling statistic, clamping exact zeroes and
     * ones to the nearest representable double to avoid infinite log values. */
    static const double AD_EPS = 0x1p-53;
    uint64_t nclamp = 0u;
    double sum = 0.0;
    const uint64_t un = tmp.count;
    for (uint64_t ui = 1u; ui <= un; ui++) {
        double xi = tmp.xa[ui - 1u];
        double xj = tmp.xa[un - ui];
        if (xi < AD_EPS) {
            xi = AD_EPS;
            nclamp++;
        }

        if (xj > 1.0 - AD_EPS) {
            xj = 1.0 - AD_EPS;
            nclamp++;
        }

        sum += (double)(2u * ui - 1u) * (log(xi) + log1p(-xj));
    }

    const double dn = (double)un;
    const double an = -dn - sum / dn;
    cmb_dataset_terminate(&tmp);

    /* Statistical tolerance for clamped full-scale values */
    result->n_clamped = nclamp;
    if ((double)nclamp > sqrt(dn / 3670.0)) {
        result->status = CMI_TEST_SATURATED;

        /* A^2 is meaningless; don't compute a p-value, just bail out */
        return;
    }

    /* Calculate the limiting probability distribution and adjust for finite n */
    const double adinf = ad_inf(an);
    const double p_z = adinf + ad_errfix(un, adinf);
    const double q_z = 1.0 - p_z;
    const double lp = log(p_z);
    const double lq = log(q_z);
    const double ltail = fmin(log(p_z), log(q_z));

    /* Fill in the results */
    cmb_assert_debug(result->nparts < CMI_TEST_U01_PARTS);
    struct cmi_test_partial *rp = &(result->p[result->nparts]);
    rp->name = "Anderson-Darling EDF test           ";
    rp->level = 0u;
    rp->v = an;
    rp->e = 1.0;
    rp->lp = fmin(M_LN2 + ltail, 0.0);
    rp->s = (lq < lp) ? logp_to_sigma(ltail) : -logp_to_sigma(ltail);
    result->nparts++;
}

#define PEARSON_GROUP 16u
static const double test_min_value = 0.0;
static const double test_max_value = 1.0;
static const uint64_t test_min_count = 100u;
static const double test_min_range = 1e-12;

double cmi_test_u01(const struct cmb_dataset *dsp, struct cmi_test_outcome *result)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->count > 0u);
    /* This will also catch any NaNs */
    cmb_assert_release(dsp->max >= dsp->min);
    cmb_assert_release(result != NULL);

    result->type = CMI_TEST_GOF_U01;
    result->n = dsp->count;
    result->min = dsp->min;
    result->max = dsp->max;
    result->combined_lp = 0.0;
    result->combined_sigma = 0.0;
    result->nparts = 0u;

    double sigma;
    if ((dsp->min < test_min_value) || (dsp->max > test_max_value)) {
        /* Can be rejected out of hand. It is surely not ~U(0,1) */
        result->status = CMI_TEST_OUT_OF_RANGE;
        sigma = INFINITY;
    }
    else if (dsp->count < test_min_count) {
        /* Can not make a judgement */
        result->status = CMI_TEST_TOO_FEW;
        sigma = NAN;
    }
    else if (dsp->max - dsp->min <= test_min_range) {
        /* Can not make a judgement */
        result->status = CMI_TEST_DEGENERATE;
        sigma = NAN;
    }
    else {
        /* Good for now, may be changed by one of the test functions */
        result->status = CMI_TEST_OK;

        /* Calculate the residuals vector, ensure a multiple of Pearson, and
         * the corresponding chi squared statistic. */
        const unsigned nb_raw  = clamp_u(dsp->count / 500u, 32u, 256u);
        const unsigned nb_fine = (nb_raw / PEARSON_GROUP) * PEARSON_GROUP;
        double *rv_fine = cmi_calloc(nb_fine, sizeof(*rv_fine));
        const double x2_fine = bin_residuals(dsp, nb_fine, rv_fine);

        /* Calculate a coarse-grained residuals vector for Pearson, grouping
         * PEARSON_GROUP bins into each coarse bin, and its chi square stat. */
        const unsigned nb_coarse = nb_fine / PEARSON_GROUP;
        double *rv_coarse = cmi_calloc(nb_coarse, sizeof(*rv_coarse));
        double x2_coarse = 0.0;
        cmb_assert_debug((nb_fine % nb_coarse) == 0);
        const double inv_sq = 1.0 / sqrt((double)PEARSON_GROUP);
        for (unsigned ui = 0; ui < nb_coarse; ui++) {
            double sum = 0.0;
            for (unsigned uj = 0; uj < PEARSON_GROUP; uj++) {
                sum += rv_fine[ui * PEARSON_GROUP + uj];
            }

            rv_coarse[ui] = sum * inv_sq;
            x2_coarse += (rv_coarse[ui] * rv_coarse[ui]);
        }

        /* Run Pearson chi square and Neyman smooth tests, not to be combined
         * later since they use the exact same data */
        cmb_assert_debug((x2_coarse >= 0.0) && (x2_coarse <= x2_fine + 1e-9));
        pearson_chisquare_U01(rv_coarse, x2_coarse, nb_coarse, result);
        neyman_smooth_U01(rv_fine, x2_fine, nb_fine, result);

        cmi_free(rv_coarse);
        cmi_free(rv_fine);

        /* Anderson-Darling is partly independent, based on the Empirical
         * Distribution Function instead of the residuals vector */
        anderson_darling_U01(dsp, result);

        /* Combine Neyman and Anderson-Darling results into an overall verdict.
         * We know that the combined Neyman is in partial result 1, A-D in 7 */
        struct cmi_test_partial *rp_ns = &(result->p[1]);
        struct cmi_test_partial *rp_ad = &(result->p[7]);
        double lmin = (rp_ad->lp < rp_ns->lp)? rp_ad->lp : rp_ns->lp;

        /* Bonferroni: p_family = min(1, k * p_min).  Valid under arbitrary
         * dependence, which is what we need here. */
        const double lcomb = fmin(log((double)2) + lmin, 0.0);
        sigma = logp_to_sigma(lcomb - M_LN2);
        result->combined_lp = lcomb;
        result->combined_sigma = sigma;
    }

    return sigma;
}

void cmi_test_transform(struct cmb_dataset *tgt,
                        const struct cmb_dataset *src,
                        cmi_test_transform_func *map,
                        void *arg)
{
    cmb_assert_release(src != NULL);
    cmb_assert_release(src->cookie == CMI_INITIALIZED);
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(map != NULL);

    struct cmb_dataset tmp = { 0 };
    cmb_dataset_initialize(&tmp);

    const uint64_t un = src->count;
    for (uint64_t ui = 0; ui < un; ui++) {
        const double x = src->xa[ui];
        const double y = (map)(x, arg);
        cmb_dataset_add(&tmp, y);
    }

    cmb_dataset_copy(tgt, &tmp);
    cmb_dataset_terminate(&tmp);
}

/* Stub for two-sample test */
double cmi_test_ts(struct cmb_dataset *x,
                   struct cmb_dataset *y,
                   struct cmi_test_outcome *r)
{
    cmb_unused(x);
    cmb_unused(y);
    cmb_unused(r);

    return 0;
}

/* Interpretations inspired by PractRand, somewhat simplified */
const char *cmi_test_interpretation(const double sigma)
{
    #define CMI_TEST_BUF_SIZE 120
    static CMB_THREAD_LOCAL char buf[CMI_TEST_BUF_SIZE];

    const char *d, *a;
    const double sigabs = fabs(sigma);
    if (sigabs > 3.0) {
        d = (sigma > 0) ? "High : " : "Low  : ";
        if (sigabs > 9.0) {
            a = "Failed!!!";
        }
        else if (sigabs > 8.0) {
            a = "Failed!!";
        }
        else if (sigabs > 7.0) {
            a = "Failed!";
        }
        else if (sigabs > 6.0) {
            a = "Failed";
        }
        else if (sigabs > 5.0) {
            a = "Malodorous";
        }
        else if (sigabs > 4.0) {
            a = "Suspicious";
        }
        else {
            a = "Unusual";
        }
    }
    else {
        d = "";
        a = "";
    }

    /* Two-sided probabilities */
    const double ltail = normal_logsf(sigabs) + M_LN2;
    int nw = 0;
    if (ltail >= 0.0) {
        nw = snprintf(buf, CMI_TEST_BUF_SIZE,
                    "No evidence against uniformity\n");
    }
    else if (ltail > -700.0) {
        nw = snprintf(buf, CMI_TEST_BUF_SIZE,
                      "Sigma: %#.4g\tOdds: 1 in %.2g\t%s%s",
                      sigma, exp(-ltail), d, a);
    }
    else {
        nw = snprintf(buf, CMI_TEST_BUF_SIZE,
                      "Sigma: %#.4g \tOdds: 1 in 10^%.0f\t%s%s",
                      sigma, -ltail / M_LN10, d, a);
    }

    cmb_assert_debug((nw > 0) && (nw < CMI_TEST_BUF_SIZE));

    return buf;
    #undef CMI_TEST_BUF_SIZE
}

void cmi_test_outcome_print(struct cmi_test_outcome *r, FILE *fp)
{
    cmb_assert_release(r != NULL);
    cmb_assert_release(fp != NULL);

    if (r->type == CMI_TEST_GOF_U01) {
        if (r->status == CMI_TEST_OK) {
            cmi_test_fnprint_line(fp, "-", 120u);
            fprintf(fp, "Test:                                "
                        "\tAct.:   \tExp.:   \tInterpretation:\n");
            cmi_test_fnprint_line(fp, "-", 120u);
            for (unsigned ui = 0; ui < r->nparts; ui++) {
                const struct cmi_test_partial *rp = &(r->p[ui]);
                for (unsigned l = 0u; l < rp->level; l++) {
                    fprintf(fp, "\t");
                }

                 fprintf(fp, "%s\t%8.3g\t%8.3g\t%s\n",
                    rp->name, rp->v, rp->e, cmi_test_interpretation(rp->s));
            }

            if (r->n_clamped > 0u) {
                fprintf(fp, "\tNote: Found %" PRIu64" exact 0.0 or 1.0 values in %" PRIu64 " samples, expected %g\n",
                            r->n_clamped, r->n, ldexp((double)r->n, -53));
            }

            fprintf(fp, "Combined assessment, Bonferroni on Neyman + Anderson-Darling:\t%s\n",
                cmi_test_interpretation(r->combined_sigma));
        }
        else if (r->status == CMI_TEST_TOO_FEW) {
            fprintf(fp, "Too few samples, n = %" PRIu64 ", needs at least %" PRIu64 "\n",
                        r->n, test_min_count);
        }
        else if (r->status == CMI_TEST_OUT_OF_RANGE) {
            fprintf(fp, "Value(s) out of range : Surely failed!\n");
            if (r->min < test_min_value) {
                fprintf(fp, "\tSmallest sample %f, expected at least %f\n",
                            r->min, test_min_value);
            }
            if (r->max > test_max_value) {
                fprintf(fp, "\tLargest sample %f, expected at most %f\n",
                            r->max, test_max_value);
            }
        }
        else if (r->status == CMI_TEST_DEGENERATE) {
            fprintf(fp, "All data values too close, range %f\n",
                        r->max - r->min);
        }
        else if (r->status == CMI_TEST_SATURATED) {
            fprintf(fp, "Too many exact 0.0 and/or 1.0 samples, would bias results.\n");
            fprintf(fp, "\tFound %" PRIu64" in %" PRIu64 " samples, expected %g\n",
                        r->n_clamped, r->n, ldexp((double)r->n, -53));
        }
    }

    cmi_test_fnprint_line(fp, "-", 120u);
}

/** Only self-test functions below here ***/

/* Reference values for log CDF computed with mpmath at 50 digits. */
static const struct { double a, x, logp, logq; } logcdf_ref[] = {
    {     0.5,     0.01,       -2.185131747072374,      -0.1193049737373956 },
    {     1.0,      1.0,      -0.4586751453870819,                     -1.0 },
    {     1.5,      9.0,   -0.0004399464150723104,       -7.729077587537633 },
    {     5.0,      1.0,       -5.610333982897155,    -0.003666560452308509 },
    {     9.5,      9.5,      -0.6103442100890579,       -0.783430539721863 },
    {    10.0,     40.0,    -3.92593223399266e-09,      -19.355662004031213 },
    {    31.5,      6.3,       -27.93006470653563,   -7.415270851149634e-13 },
    {   100.0,    200.0,  -1.8438936497115757e-15,       -33.92689694513168 },
    {   450.0,      9.0,      -1323.3639434120955,                      0.0 },
    {     9.5,     4.54,      -3.5800946303234524,    -0.028268886848755295 },
    {    31.5,   1260.0,                      0.0,      -1118.6112703737606 },
};

/* Deterministic check against reference values */
static void test_chisq_logcdf(void)
{
    cmi_test_print_line("-");
    printf("Testing chi squared CDF in log space\n");
    cmi_test_print_line("-");

    double worst_p = 0.0;
    double worst_q = 0.0;
    const int n = (int)(sizeof logcdf_ref / sizeof logcdf_ref[0]);

    for (int i = 0; i < n; i++) {
        double lp, lq;
        igamma_log(logcdf_ref[i].a, logcdf_ref[i].x, &lp, &lq);
        if (logcdf_ref[i].logp > -1e300) {
            const double e = fabs(lp - logcdf_ref[i].logp) / fmax(fabs(logcdf_ref[i].logp), 1.0);
            if (e > worst_p) {
                worst_p = e;
            }
        }

        if (logcdf_ref[i].logq > -1e300) {
            const double e = fabs(lq - logcdf_ref[i].logq) / fmax(fabs(logcdf_ref[i].logq), 1.0);
            if (e > worst_q) {
                worst_q = e;
            }
        }
    }

    printf("cases = %d   worst rel err: log P = %.2e   log Q = %.2e\n",
           n, worst_p, worst_q);

    const double xs[] = { 19.7933, 10.1652, 76.0, 126.0, 2520.0, 0.30, 2.5 };
    const double dfs[] = { 19.0, 20.0, 19.0, 63.0, 63.0, 20.0, 20.0 };
    cmi_test_print_line("-");

    printf("DoF     \tX2    \tlog(P)  \tlog(Q)  \tOdds          \tSigma\n");
    cmi_test_print_line("-");
    for (int i = 0; i < 7; i++) {
        double lp, lq;
        igamma_log(dfs[i] / 2.0, xs[i] / 2.0, &lp, &lq);
        printf("%4.0f\t%10.4f\t%8.4g\t%8.4g\t", dfs[i], xs[i], lp, lq);
        const double ltail = (lq < lp) ? lq : lp;
        if (ltail > -700.0) {
            printf("1 in %8.4g", exp(-ltail));
        }
        else {
            printf("1 in 10^%.0f  ", -ltail / M_LN10);
        }

        printf("\t%#12.6g\n", chisq_sigma(dfs[i], xs[i]));
    }

    cmi_test_print_line("-");
    for (int i = 0; i < 7; i++) {
        double lp, lq;
        igamma_log(dfs[i] / 2.0, xs[i] / 2.0, &lp, &lq);
        const double sigma = chisq_sigma(dfs[i], xs[i]);
        const char *str = cmi_test_interpretation(sigma);
        printf("%s\n", str);
    }
}

/* Piercing the static declaration for internal functions */
void cmi_test_selftests()
{
    test_chisq_logcdf();
}

void cmi_test_log_incomplete_gamma(const double a, const double x,
                                   double *logp, double *logq)
{
    igamma_log(a, x, logp, logq);
}
void cmi_test_log_incomplete_beta(const double a, const double b, const double x,
                                  double *logp, double *logq)
{
    ibeta_log(a, b, x, logp, logq);
}
