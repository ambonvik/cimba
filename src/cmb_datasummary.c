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
#include <math.h>
#include <inttypes.h>
#include <stdio.h>

#include "cmb_datasummary.h"

#include "cmi_memutils.h"

struct cmb_datasummary *cmb_datasummary_create(void)
{
    struct cmb_datasummary *dsp = cmi_malloc(sizeof *dsp);
    cmi_memset(dsp, 0, sizeof *dsp);
    dsp->cookie = CMI_UNINITIALIZED;

    /* Add teardown function to the memregistry in case we need to bail out */
    cmi_dlist_initialize(&(dsp->destroy.node));
    dsp->destroy.teardown = (cmi_teardown_func *)cmb_datasummary_destroy;
    dsp->destroy.object = dsp;
    cmi_memregistry_add(&(dsp->destroy));

    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);
    return dsp;
}

void cmb_datasummary_initialize(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);
    /* Might get raw memory with random content, cannot assert _UNINITIALIZED */

    dsp->cookie = CMI_INITIALIZED;
    dsp->count = 0u;
    dsp->max = -DBL_MAX;
    dsp->min = DBL_MAX;
    dsp->m1 = 0.0;
    dsp->m2 = 0.0;
    dsp->m3 = 0.0;
    dsp->m4 = 0.0;

    /* Add teardown function to the memregistry in case we need to bail out */
    cmi_dlist_initialize(&(dsp->terminate.node));
    dsp->terminate.teardown = (cmi_teardown_func *)cmb_datasummary_terminate;
    dsp->terminate.object = dsp;
    cmi_memregistry_add(&(dsp->terminate));

    cmb_assert_debug(dsp->cookie == CMI_INITIALIZED);
}

void cmb_datasummary_reset(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    cmb_datasummary_terminate(dsp);
    cmb_datasummary_initialize(dsp);

    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
}

void cmb_datasummary_terminate(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release((dsp->cookie == CMI_INITIALIZED)
                    || cmi_memregistry_is_demolishing);

    if (dsp->cookie == CMI_INITIALIZED) {
        dsp->cookie = CMI_UNINITIALIZED;
        dsp->count = 0u;
    }

    if (!cmi_memregistry_is_demolishing) {
        cmi_memregistry_remove(&(dsp->terminate));
    }

    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);
}

void cmb_datasummary_destroy(struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);
    /* Call cmb_datasummary_terminate first, please */
    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);

    if (!cmi_memregistry_is_demolishing) {
        /* Destroying normally, remove from register */
        cmi_memregistry_remove(&(dsp->destroy));
    }

    cmi_free(dsp);
}

/*
 * Merge two data summaries, updating the statistics.
 * Used e.g., for merging across pthreads.
 *
 * See:
 * Philippe Pébay (2008), "Formulas for Robust, One-Pass Parallel Computation of
 *     Covariances and Arbitrary-Order Statistical Moments",
 *     https://www.osti.gov/servlets/purl/1028931
 *     (Sandia report SAND2008-6212, U.S. Government work, hence public domain.)
 *
 * Note that the target address may point to one of the sources, hence all
 * calculations are done in a temporary variable and the target overwritten
 * only at the end.
 *
 * Returns tgt->count, the number of data points in the combined summary.
 */
uint64_t cmb_datasummary_merge(struct cmb_datasummary *tgt,
                               const struct cmb_datasummary *dsrc1,
                               const struct cmb_datasummary *dsrc2)
{
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(dsrc1 != NULL);
    cmb_assert_release(dsrc1->cookie == CMI_INITIALIZED);
    cmb_assert_release(dsrc2 != NULL);
    cmb_assert_release(dsrc2->cookie == CMI_INITIALIZED);

    struct cmb_datasummary dstmp = { 0 };
    cmb_datasummary_initialize(&dstmp);

    dstmp.count = dsrc1->count + dsrc2->count;
    dstmp.min = (dsrc1->min < dsrc2->min) ? dsrc1->min : dsrc2->min;
    dstmp.max = (dsrc1->max > dsrc2->max) ? dsrc1->max : dsrc2->max;
    if (dstmp.count > 0u) {
        const double n1 = (double)dsrc1->count;
        const double n2 = (double)dsrc2->count;
        const double n = (double)dstmp.count;
        const double d21 = dsrc2->m1 - dsrc1->m1;
        const double d21_n = d21 / n;
        const double d21_n_2 = d21_n * d21_n;
        const double d21_n_3 = d21_n * d21_n_2;

        dstmp.m1 = dsrc1->m1 + n2 * d21_n;
        dstmp.m2 = dsrc1->m2 + dsrc2->m2
                         + n1 * n2 * d21 * d21_n;
        dstmp.m3 = dsrc1->m3 + dsrc2->m3
                         + n1 * n2 * (n1 - n2) * d21 * d21_n_2
                         + 3.0 * (n1 * dsrc2->m2 - n2 * dsrc1->m2) * d21_n;
        dstmp.m4 = dsrc1->m4 + dsrc2->m4
                         + n1 * n2 * (n1 * n1 - n1 * n2 + n2 * n2) * d21 * d21_n_3
                         + 6.0 * (n1 * n1 * dsrc2->m2 + n2 * n2 * dsrc1->m2) * d21_n_2
                         + 4.0 * (n1 * dsrc2->m3 - n2 * dsrc1->m3) * d21_n;
    }
    else {
        dstmp.m1 = 0.0;
        dstmp.m2 = 0.0;
        dstmp.m3 = 0.0;
        dstmp.m4 = 0.0;
    }

    if (tgt->cookie == CMI_INITIALIZED) {
        cmb_datasummary_terminate(tgt);
    }

    cmb_datasummary_initialize(tgt);
    tgt->count = dstmp.count;
    tgt->max = dstmp.max;
    tgt->min = dstmp.min;
    tgt->m1 = dstmp.m1;
    tgt->m2 = dstmp.m2;
    tgt->m3 = dstmp.m3;
    tgt->m4 = dstmp.m4;
    cmb_datasummary_terminate(&dstmp);

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
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

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
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(fp != NULL);

    int r = fprintf(fp, "%s%8" PRIu64, ((lead_ins)? "N ": ""), dsp->count);
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
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

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
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

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

