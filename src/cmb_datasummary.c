/*
* cmb_datasummary - a running tally of basic statistics, not keeping
 *                    individual sample values.
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

#include <float.h>
#include <stdio.h>

#include "cmb_datasummary.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"

struct cmb_datasummary *cmb_datasummary_create(void)
{
    struct cmb_datasummary *dsp = cmi_malloc(sizeof *dsp);
    cmb_datasummary_initialize(dsp);

    return dsp;
}

void cmb_datasummary_initialize(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    dsp->count = 0u;
    dsp->max = -DBL_MAX;
    dsp->min = DBL_MAX;
    dsp->m1 = 0.0;
    dsp->m2 = 0.0;
    dsp->m3 = 0.0;
    dsp->m4 = 0.0;
}

void cmb_datasummary_reset(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    cmb_datasummary_terminate(dsp);
    cmb_datasummary_initialize(dsp);
}

void cmb_datasummary_terminate(struct cmb_datasummary *dsp)
{
    cmb_unused(dsp);
}

void cmb_datasummary_destroy(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    cmb_datasummary_terminate(dsp);
    cmi_free(dsp);
}

/*
 * Merge two data summaries, updating the statistics.
 * Used e.g. for merging across pthreads.
 *
 * See:
 * Philippe Pébay (2008), "Formulas for Robust, One-Pass Parallel Computation of
 *     Covariances and Arbitrary-Order Statistical Moments",
 *     https://www.osti.gov/servlets/purl/1028931
 *     (Sandia report SAND2008-6212, U.S. Government work, hence public domain.)
 *
 * Note that the target address may point to one of the sources, hence all
 * calculations are done  * in a temporary variable and the target overwritten
 * only at the end.
 *
 * Returns tgt->count, the number of data points in the combined summary.
 */
uint64_t cmb_datasummary_merge(struct cmb_datasummary *tgt,
                           const struct cmb_datasummary *dsp1,
                           const struct cmb_datasummary *dsp2)
{
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(dsp1 != NULL);
    cmb_assert_release(dsp2 != NULL);

    struct cmb_datasummary cs = { 0 };
    cs.count = dsp1->count + dsp2->count;
    cs.min = (dsp1->min < dsp2->min) ? dsp1->min : dsp2->min;
    cs.max = (dsp1->max > dsp2->max) ? dsp1->max : dsp2->max;

    const double n1 = (double)dsp1->count;
    const double n2 = (double)dsp2->count;
    const double n = (double)cs.count;
    const double d21 = dsp2->m1 - dsp1->m1;
    const double d21_n = d21 / n;
    const double d21_n_2 = d21_n * d21_n;
    const double d21_n_3 = d21_n * d21_n_2;

    cs.m1 = dsp1->m1 + n2 * d21_n;
    cs.m2 = dsp1->m2 + dsp2->m2
                     + n1 * n2 * d21 * d21_n;
    cs.m3 = dsp1->m3 + dsp2->m3
                     + n1 * n2 * (n1 - n2) * d21 * d21_n_2
                     + 3.0 * (n1 * dsp2->m2 - n2 * dsp1->m2) * d21_n;
    cs.m4 = dsp1->m4 + dsp2->m4
                     + n1 * n2 * (n1 * n1 - n1 * n2 + n2 * n2) * d21 * d21_n_3
                     + 6.0 * (n1 * n1 * dsp2->m2 + n2 * n2 * dsp1->m2) * d21_n_2
                     + 4.0 * (n1 * dsp2->m3 - n2 * dsp1->m3) * d21_n;

    *tgt = cs;
    
    return tgt->count;
}

/*
 * Add a sample value to the data summary, updating the statistics.
 * See Pébay (2008). Adding a single sample is a special case of the merge
 * described there, with n2 = 1.
 *
 * Optimized evaluation sequence as described in Xiangrui Meng (2015),
 *     "Simpler Online Updates for Arbitrary-Order Central Moments",
 *     https://arxiv.org/pdf/1510.04923
 *
 * Returns the updated sample count.
 */
uint64_t cmb_datasummary_add(struct cmb_datasummary *dsp, const double y)
{
    cmb_assert_release(dsp != NULL);

    dsp->max = (y > dsp->max) ? y : dsp->max;
    dsp->min = (y < dsp->min) ? y : dsp->min;

    const double d = y - dsp->m1;
    const double d_2 = d * d;
    const double d_3 = d * d_2;
    const double n = (double)(++dsp->count);
    const double d_n = d / n;
    const double d_n_2 = d_n * d_n;
    const double d_n_3 = d_n_2 * d_n;

    dsp->m1 += d_n;
    dsp->m2 += d * (d - d_n);
    dsp->m3 += d * (d_2 - d_n_2) - 3.0 * d_n * dsp->m2;
    dsp->m4 += d * (d_3 - d_n_3) - 6.0 * d_n_2 * dsp->m2 - 4.0 * d_n * dsp->m3;

    return dsp->count;
}

void cmb_datasummary_print(const struct cmb_datasummary *dsp,
                       FILE *fp,
                       const bool lead_ins)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(fp != NULL);

    int r = fprintf(fp, "%s%8llu", ((lead_ins)? "N ": ""), dsp->count);
    cmb_assert_release(r > 0);
    if (dsp->count > 0u) {
        const double mean = cmb_datasummary_mean(dsp);
        r = fprintf(fp, "%s%#8.4g",
                ((lead_ins) ? "  Mean " : "\t"), mean);
        cmb_assert_release(r > 0);
    }

    if (dsp->count > 1u) {
        const double var = cmb_datasummary_variance(dsp);
        const double std = sqrt(var);
        r = fprintf(fp, "%s%#8.4g",
                ((lead_ins) ? "  StdDev " : "\t"), std);
        cmb_assert_release(r > 0);
        r = fprintf(fp, "%s%#8.4g",
                ((lead_ins) ? "  Variance " : "\t"), var);
        cmb_assert_release(r > 0);
    }

    if (dsp->count > 2u) {
        const double skew = cmb_datasummary_skewness(dsp);
        r = fprintf(fp, "%s%#8.4g",
            ((lead_ins) ? "  Skewness " : "\t"), skew);
        cmb_assert_release(r > 0);
    }

    if (dsp->count > 3u) {
        const double kurt = cmb_datasummary_kurtosis(dsp);
        r = fprintf(fp, "%s%#8.4g",
            ((lead_ins) ? "  Kurtosis " : "\t"), kurt);
        cmb_assert_release(r > 0);
    }

    r = fprintf(fp, "\n");
    cmb_assert_release(r > 0);
}

double cmb_datasummary_skewness(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    double r = 0.0;
    if (dsp->count > 2u) {
        /* Estimate population skewness */
        const double dn = (double)dsp->count;
        const double g = sqrt(dn) * dsp->m3 / pow(dsp->m2, 1.5);

        /* Correction for finite sample */
        r = sqrt(dn * (dn - 1.0)) * g / (dn - 2.0);
    }

    return r;
}

/* Sample excess kurtosis */
double cmb_datasummary_kurtosis(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    double r = 0.0;
    if (dsp->count > 3u) {
        /* Estimate population excess kurtosis */
        const double dn = (double)dsp->count;
        const double g = dn * dsp->m4 / (dsp->m2 * dsp->m2) - 3.0;

        /* Correction for finite sample */
        r = (dn - 1.0) / ((dn - 2.0) * (dn - 3.0)) * ((dn + 1.0) * g + 6.0);
    }

    return r;
}

