/*
* cmb_wtdsummary.c - a running tally of basic statistics, not keeping the
 * individual sample values, each sample weighted by a double in the summary.
 *
 * It can be used for time series  statistics where each value is held for a
 * certain duration, such as queue lengths or the number of customers in a
 * queueing system.
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

#include <inttypes.h>
#include <stdio.h>

#include "cmb_logger.h"
#include "cmb_wtdsummary.h"

#include "cmi_memutils.h"

struct cmb_wtdsummary *cmb_wtdsummary_create(void)
{
    struct cmb_wtdsummary *wsp = cmi_malloc(sizeof *wsp);
    cmi_memset(wsp, 0, sizeof *wsp);
    struct cmb_datasummary *dsp = &(wsp->ds);
    dsp->cookie = CMI_UNINITIALIZED;

    /* We store the memregistry item in the parent class node */
    cmi_dlist_initialize(&(dsp->destroy.node));
    dsp->destroy.teardown = (cmi_teardown_func *)cmb_wtdsummary_destroy;
    dsp->destroy.object = wsp;
    cmi_memregistry_add(&(dsp->destroy));

    cmb_assert_debug(wsp->ds.cookie == CMI_UNINITIALIZED);
    return wsp;
}

void cmb_wtdsummary_initialize(struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);
    /* Might get raw memory with random content, cannot assert _UNINITIALIZED */

    /* Initializing base class creates a memregistry item. */
    struct cmb_datasummary *dsp = &(wsp->ds);
    cmb_datasummary_initialize(dsp);

    /* The memregistry item is now pointing to cmi_dataset_terminate.
     * Redirect it to ours (do not call cmb_logger_error in the meantime!) */
    cmb_assert_debug(dsp->terminate.object == wsp);
    dsp->terminate.teardown = (cmi_teardown_func *)cmb_wtdsummary_terminate;
    /* OK, consistent now, continue own initialization */

    wsp->wsum = 0.0;

    cmb_assert_debug(dsp->cookie == CMI_INITIALIZED);
}

void cmb_wtdsummary_reset(struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);
    const struct cmb_datasummary *dsp = &(wsp->ds);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    cmb_wtdsummary_terminate(wsp);
    cmb_wtdsummary_initialize(wsp);

    cmb_assert_debug(dsp->cookie == CMI_INITIALIZED);
}

void cmb_wtdsummary_terminate(struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);
    struct cmb_datasummary *dsp = &(wsp->ds);
    cmb_assert_release((dsp->cookie == CMI_INITIALIZED)
                    || cmi_memregistry_is_demolishing);

    if (dsp->cookie == CMI_INITIALIZED) {
        /* Terminating the parent class will also clear the memregistry item */
        cmb_datasummary_terminate(&(wsp->ds));
    }

    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);
}

void cmb_wtdsummary_destroy(struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);
    struct cmb_datasummary *dsp = &(wsp->ds);
    /* Call cmb_datasummary_terminate first, please */
    cmb_assert_release(dsp->cookie == CMI_UNINITIALIZED);

    if (!cmi_memregistry_is_demolishing) {
        /* Destroying normally, remove from parent class register */
        cmi_memregistry_remove(&(dsp->destroy));
    }

    cmi_free(wsp);
}

/*
 * Add a weighted sample value to the data summary, updating the statistics.
 * See: Pébay & al, "Numerically stable, scalable formulas for parallel and
 *      online computation of higher-order multivariate central moments with
 *      arbitrary weights", Computational Statistics (2016) 31:1305–1325
 *
 * Adding a single sample is a special case with n2 = 1.
 *
 * Returns the updated sample count.
 */
uint64_t cmb_wtdsummary_add(struct cmb_wtdsummary *wsp,
                            const double x,
                            const double w)
{
    cmb_assert_release(wsp != NULL);
    cmb_assert_release(wsp->ds.cookie == CMI_INITIALIZED);
    cmb_assert_release(w >= 0.0);

    struct cmb_datasummary *dsp = &(wsp->ds);
    if (w == 0.0) {
        return dsp->count;
    }

    if (dsp->count == 0u) {
        dsp->count = 1u;
        dsp->max = x;
        dsp->min = x;
        dsp->m1 = x;
        dsp->m2 = 0.0;
        dsp->m3 = 0.0;
        dsp->m4 = 0.0;
        wsp->wsum = w;

        return dsp->count;
    }

    dsp->max = (x > dsp->max) ? x : dsp->max;
    dsp->min = (x < dsp->min) ? x : dsp->min;
    dsp->count++;

    const double w1 = wsp->wsum;
    const double w2 = w;
    const double ws = w1 + w2;
    const double d21 = x - dsp->m1;
    const double d21_w = d21 / ws;
    const double d21_w_2 = d21_w * d21_w;
    const double d21_w_3 = d21_w * d21_w_2;

    const double tmp_m1 = dsp->m1 + w2 * d21_w;
    const double tmp_m2 = dsp->m2 + w1 * w2 * d21 * d21_w;
    const double tmp_m3 = dsp->m3
                         + w1 * w2 * (w1 - w2) * d21 * d21_w_2
                         - 3.0 * w2 * dsp->m2 * d21_w;
    const double tmp_m4 = dsp->m4
                         + w1 * w2 * (w1 * w1 - w1 * w2 + w2 * w2) * d21 * d21_w_3
                         + 6.0 * w2 * w2 * dsp->m2 * d21_w_2
                         - 4.0 * w2 * dsp->m3 * d21_w;

    dsp->m1 = tmp_m1;
    dsp->m2 = tmp_m2;
    dsp->m3 = tmp_m3;
    dsp->m4 = tmp_m4;
    wsp->wsum = ws;

    return dsp->count;
}

/*
 * Merge two weighted data summaries, updating the statistics.
 * Used e.g., for merging across pthreads.
 * See: Pébay & al, "Numerically stable, scalable formulas for parallel and
 *      online computation of higher-order multivariate central moments with
 *      arbitrary weights", Computational Statistics (2016) 31:1305–1325
 *
 * Note that the target address may point to one of the sources, hence all
 * calculations are done in a temporary variable and the target overwritten
 * only at the end.
 *
 * Returns tgt->count, the number of data points in the combined summary.
 */
uint64_t cmb_wtdsummary_merge(struct cmb_wtdsummary *tgt,
                              const struct cmb_wtdsummary *ws1,
                              const struct cmb_wtdsummary *ws2)
{
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(ws1 != NULL);
    cmb_assert_release(ws1->ds.cookie == CMI_INITIALIZED);
    cmb_assert_release(ws2 != NULL);
    cmb_assert_release(ws2->ds.cookie == CMI_INITIALIZED);

    struct cmb_wtdsummary tws = { 0 };
    cmb_wtdsummary_initialize(&tws);
    struct cmb_datasummary *ts = &(tws.ds);
    const struct cmb_datasummary *dsp1 = &(ws1->ds);
    const struct cmb_datasummary *dsp2 = &(ws2->ds);

    ts->count = dsp1->count + dsp2->count;
    ts->min = (dsp1->min < dsp2->min) ? dsp1->min : dsp2->min;
    ts->max = (dsp1->max > dsp2->max) ? dsp1->max : dsp2->max;
    if (ts->count > 0u) {
        const double w1 = ws1->wsum;
        const double w2 = ws2->wsum;
        const double ws = w1 + w2;
        const double d21 = dsp2->m1 - dsp1->m1;
        const double d21_w = d21 / ws;
        const double d21_w_2 = d21_w * d21_w;
        const double d21_w_3 = d21_w * d21_w_2;

        tws.wsum = ws;
        ts->m1 = dsp1->m1 + w2 * d21_w;
        ts->m2 = dsp1->m2 + dsp2->m2
                          + w1 * w2 * d21 * d21_w;
        ts->m3 = dsp1->m3 + dsp2->m3
                          + w1 * w2 * (w1 - w2) * d21 * d21_w_2
                          + 3.0 * (w1 * dsp2->m2 - w2 * dsp1->m2) * d21_w;
        ts->m4 = dsp1->m4 + dsp2->m4
                          + w1 * w2 * (w1 * w1 - w1 * w2 + w2 * w2) * d21 * d21_w_3
                          + 6.0 * (w1 * w1 * dsp2->m2 + w2 * w2 * dsp1->m2) * d21_w_2
                          + 4.0 * (w1 * dsp2->m3 - w2 * dsp1->m3) * d21_w;
    }
    else {
        tws.wsum = 0.0;
        ts->m1 = 0.0;
        ts->m2 = 0.0;
        ts->m3 = 0.0;
        ts->m4 = 0.0;
    }

    if (tgt->ds.cookie == CMI_INITIALIZED) {
        cmb_wtdsummary_terminate(tgt);
    }

    cmb_wtdsummary_initialize(tgt);
    tgt->wsum = tws.wsum;
    tgt->ds.count = ts->count;
    tgt->ds.min = ts->min;
    tgt->ds.max = ts->max;
    tgt->ds.m1 = ts->m1;
    tgt->ds.m2 = ts->m2;
    tgt->ds.m3 = ts->m3;
    tgt->ds.m4 = ts->m4;
    cmb_wtdsummary_terminate(&tws);

    return tgt->ds.count;
}

void cmb_wtdsummary_print(const struct cmb_wtdsummary *wsp,
                          FILE *fp,
                          const bool lead_ins)
{
    cmb_assert_release(wsp != NULL);
    cmb_assert_release(wsp->ds.cookie == CMI_INITIALIZED);
    cmb_assert_release(fp != NULL);

    const uint64_t count = cmb_wtdsummary_count(wsp);

    int r = fprintf(fp, "%s%8" PRIu64, ((lead_ins) ? "N " : ""), count);
    cmb_assert_release(r > 0);

    if (count > 0u) {
        r = fprintf(fp, "%s%#8.4g",
                    ((lead_ins) ? "  Mean " : "\t"), cmb_wtdsummary_mean(wsp));
        cmb_assert_release(r > 0);
    }
    if (count > 1u) {
        const double var = cmb_wtdsummary_variance(wsp);
        r = fprintf(fp, "%s%#8.4g",
                    ((lead_ins) ? "  StdDev " : "\t"), cmb_wtdsummary_stddev(wsp));
        cmb_assert_release(r > 0);
        r = fprintf(fp, "%s%#8.4g",
                    ((lead_ins) ? "  Variance " : "\t"), var);
        cmb_assert_release(r > 0);
    }
    if (count > 2u) {
        r = fprintf(fp, "%s%#8.4g",
                    ((lead_ins) ? "  Skewness " : "\t"), cmb_wtdsummary_skewness(wsp));
        cmb_assert_release(r > 0);
    }
    if (count > 3u) {
        r = fprintf(fp, "%s%#8.4g",
                    ((lead_ins) ? "  Kurtosis " : "\t"), cmb_wtdsummary_kurtosis(wsp));
        cmb_assert_release(r > 0);
    }

    r = fprintf(fp, "\n");
    cmb_assert_release(r > 0);
}
