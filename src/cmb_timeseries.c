/*
 * cmb_timeseries.c - an automatically resizing array of ordered sample values,
 * each sample a (x, t) tuple.
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

#include "cmb_logger.h"
#include "cmb_timeseries.h"

#include "cmi_dataset.h"
#include "cmi_memutils.h"

struct cmb_timeseries *cmb_timeseries_create(void)
{
    struct cmb_timeseries *tsp = cmi_malloc(sizeof *tsp);
    cmb_timeseries_initialize(tsp);

    return tsp;
}

void cmb_timeseries_initialize(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_initialize((struct cmb_dataset *)tsp);

    tsp->ta = NULL;
    tsp->wa = NULL;
}

void cmb_timeseries_reset(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);
    cmb_timeseries_terminate(tsp);
    cmb_timeseries_initialize(tsp);
}

void cmb_timeseries_terminate(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    if (tsp->ta != NULL) {
        cmi_free(tsp->ta);
        tsp->ta = NULL;
    }

    if (tsp->wa != NULL) {
        cmi_free(tsp->wa);
        tsp->wa = NULL;
    }

    cmb_dataset_terminate((struct cmb_dataset *)tsp);
}

void cmb_timeseries_destroy(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    cmb_timeseries_terminate(tsp);
    cmi_free(tsp);
}

static void timeseries_expand(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    /* First expand x-vector and increment cursize */
    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    cmi_dataset_expand(dsp);

    if (tsp->ta == NULL) {
        /* Just allocated the first chunk of xa array, do same for ta and wa */
        cmb_assert_debug(dsp->cursize == CMI_DATASET_INIT_SZ);
        cmb_assert_debug(tsp->wa == NULL);
        tsp->ta = cmi_malloc(CMI_DATASET_INIT_SZ * sizeof(*(tsp->ta)));
        tsp->wa = cmi_malloc(CMI_DATASET_INIT_SZ * sizeof(*(tsp->wa)));
    }
    else {
        /* Already exist, expand the xa and ta arrays */
        cmb_assert_debug((tsp->ta != NULL) && (tsp->wa != NULL));
        tsp->ta = cmi_realloc(tsp->ta, dsp->cursize * sizeof(*(tsp->ta)));
        tsp->wa = cmi_realloc(tsp->wa, dsp->cursize * sizeof(*(tsp->wa)));
    }
}

uint64_t cmb_timeseries_add(struct cmb_timeseries *tsp,
                            const double x,
                            const double t)
{
    cmb_assert_release(tsp != NULL);

    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    cmb_assert_debug((dsp->cnt == 0u) || ((tsp->ta != NULL)
                                      && (tsp->ta[dsp->cnt - 1u] <= t)));
    if (dsp->cnt == dsp->cursize) {
        /* Full (or not created) data arrays, resize arrays xa, ta, and wa */
        timeseries_expand(tsp);
    }

    const uint64_t ui_new = dsp->cnt;
    const uint64_t n = cmb_dataset_add(dsp, x);
    cmb_assert_debug(n == ui_new + 1u);

    cmb_assert_debug(tsp->ta != NULL);
    tsp->ta[ui_new] = t;
    /* Duration still unknown, weight at zero for now */
    tsp->wa[ui_new] = 0.0;

    if (ui_new > 0u) {
        /* Update previous duration */
        const uint64_t ui_prev = ui_new - 1u;
        const double t_prev = tsp->ta[ui_prev];
        cmb_assert_debug(tsp->wa[ui_prev] == 0.0);
        const double dt = t - t_prev;
        cmb_assert_debug(dt >= 0.0);
        tsp->wa[ui_prev] = dt;
    }

    return dsp->cnt;
}

uint64_t cmb_timeseries_finalize(struct cmb_timeseries *tsp, const double t)
{
    cmb_assert_release(tsp != NULL);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    const uint64_t n = dsp->cnt;
    cmb_assert_release((n == 0u) || ((tsp->ta != NULL)
                                 && (tsp->ta[n - 1u] <= t)));
    const double x = dsp->xa[n - 1u];
    const uint64_t r = cmb_timeseries_add(tsp, x, t);

    cmb_assert_debug((r == n + 1) && (dsp->xa[n] == x) && (tsp->ta[n] == t));
    return r;
}

/*
 * Summarize the timeseries into a weighted data set, using the time intervals
 * between x-values as the weighing. The last x-value in the timeseries has no
 * duration and is not included in the summary.
 *
 * Call cmb_timeseries_finalize(cmb_time()) first to include the last x-value
 * with a non-zero duration.
 */
uint64_t cmb_timeseries_summarize(const struct cmb_timeseries *tsp,
                                  struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(tsp->ta != NULL);
    cmb_assert_release(wsp != NULL);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    cmb_assert_debug(dsp->xa != NULL);

    const uint64_t old_n = cmb_wtdsummary_count(wsp);
    const uint64_t un = cmb_timeseries_count(tsp);
    cmb_assert_debug(un > 0u);
    for (uint64_t ui = 0u; ui < un - 1u; ui++) {
        const double x = dsp->xa[ui];
        const double w = tsp->wa[ui];
        (void)cmb_wtdsummary_add(wsp, x, w);
    }

    return old_n + un - 1u;
}

void cmb_timeseries_print(const struct cmb_timeseries *tsp, FILE *fp)
{
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(fp != NULL);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    const uint64_t n = dsp->cnt;
    if (dsp->xa != NULL) {
        cmb_assert_debug(tsp->ta != NULL);
        cmb_assert_debug(tsp->wa != NULL);
        for (uint64_t ui = 0; ui < n; ui++) {
            fprintf(fp, "%g\t%g\t%g\n", tsp->ta[ui], dsp->xa[ui], tsp->wa[ui]);
        }
    }
    else {
        cmb_logger_warning(fp, "No data to print");
    }
}

/*
 * Calculate a histogram with time-weighed values, each x-value counted with
 * the t-value interval to the next x-value, i.e. the holding time.
 */
static void timeseries_fill_histogram(struct cmi_dataset_histogram *hp,
                                      const uint64_t n,
                                      const double xa[n],
                                      const double wa[n])
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(n > 0u);
    cmb_assert_debug(xa != NULL);

    /* Distribute x-values to bins */
    for (uint64_t ui = 0u; ui < n - 1u; ui++) {
        /* In what bin does this x-value belong? */
        uint16_t bin;
        const double x = xa[ui];
        if (x < hp->low_lim) {
            bin = 0u;
        }
        else if (x > hp->high_lim) {
            bin = hp->num_bins - 1u;
        }
        else {
            bin = 1u + (uint16_t)((x - hp->low_lim) / hp->binsize);
        }

        /* Add it to that bin and note the high-water mark */
        hp->hbins[bin] += wa[ui];
        if (hp->hbins[bin] > hp->binmax) {
            hp->binmax = hp->hbins[bin];
        }
    }
}

void cmb_timeseries_print_histogram(const struct cmb_timeseries *tsp,
                                    FILE *fp,
                                    const uint16_t num_bins,
                                    double low_lim,
                                    double high_lim)
{
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(fp != NULL);
    cmb_assert_release(num_bins > 0u);
    cmb_assert_release(high_lim >= low_lim);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    if (dsp->xa == NULL) {
        cmb_assert_debug(dsp->cnt == 0u);
        cmb_assert_debug(tsp->ta == NULL);
        cmb_logger_warning(fp, "No data to display in histogram");
        return;
    }

    if (low_lim == high_lim) {
        /* Autoscale to dataset range */
        low_lim = dsp->min;
        high_lim = dsp->max;
    }

    struct cmi_dataset_histogram *hp = NULL;
    hp = cmi_dataset_create_histogram(num_bins, low_lim, high_lim);
    timeseries_fill_histogram(hp, dsp->cnt, dsp->xa, tsp->wa);
    cmi_dataset_print_histogram(hp, fp);

    cmi_dataset_destroy_histogram(hp);
}

uint64_t cmb_timeseries_copy(struct cmb_timeseries *tgt,
                          const struct cmb_timeseries *src)
{
    cmb_assert_release(src != NULL);
    cmb_assert_release(tgt != NULL);

    struct cmb_dataset *dsp_tgt = (struct cmb_dataset *) tgt;
    const struct cmb_dataset *dsp_src = (struct cmb_dataset *)src;
    (void)cmb_dataset_copy(dsp_tgt, dsp_src);

    if (tgt->ta != NULL) {
        cmi_free(tgt->ta);
        tgt->ta = NULL;
    }

    const uint64_t csz = dsp_src->cnt;
    if (src->ta != NULL) {
        cmb_assert_debug(csz > 0u);
        tgt->ta = cmi_calloc(csz, sizeof *(tgt->ta));
        cmi_memcpy(tgt->ta, src->ta, csz * sizeof *(tgt->ta));
    }

    if (tgt->wa != NULL) {
        cmi_free(tgt->wa);
        tgt->wa = NULL;
    }

    if (src->wa != NULL) {
        cmb_assert_debug(csz > 0u);
        tgt->wa = cmi_calloc(csz, sizeof *(tgt->wa));
        cmi_memcpy(tgt->wa, src->wa, csz * sizeof *(tgt->wa));
    }

    return dsp_tgt->cnt;
}

/*
 * Establish max heap condition in time series arrays starting from uroot.
 * Uses keya as the sorting key, da1 and da2 just follow along for the ride.
 * Normally called with the timeseries xa, ta, and wa as arguments.
 * Caution: Changes the sequence of data points in the time series.
 * To restablish time sequence, resort with ta as the key array.
 */
static void timeseries_heapify(const uint64_t un,
                               double keya[un],
                               double da1[un],
                               double da2[un],
                               uint64_t uroot)
{
    cmb_assert_release(keya != NULL);
    cmb_assert_release(da1 != NULL);
    cmb_assert_release(da2 != NULL);
    cmb_assert_release(uroot < un);

    uint64_t ucl = 2u * uroot + 1u;
    uint64_t ucr = 2u * uroot + 2u;

    for (;;) {
        uint64_t ubig = uroot;
        cmb_assert_debug(ubig < un);
        if ((ucl < un) && (keya[ucl] > keya[ubig])) {
            ubig = ucl;
        }

        if ((ucr < un) && (keya[ucr] > keya[ubig])) {
            ubig = ucr;
        }

        if (ubig != uroot) {
            cmb_assert_debug((uroot < un) && (ubig < un));
            cmi_dataset_swap(&keya[uroot], &keya[ubig]);
            cmi_dataset_swap(&da1[uroot], &da1[ubig]);
            cmi_dataset_swap(&da2[uroot], &da2[ubig]);
            uroot = ubig;
            ucl = 2u * uroot + 1u;
            ucr = 2u * uroot + 2u;
        }
        else {
            /* Heap property is satisfied */
            break;
        }
    }

    cmb_assert_debug(cmi_dataset_is_max_heap(un, keya, uroot));
}

/* Heapsort from smallest to largest x-value */
void cmb_timeseries_sort_x(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    if (dsp->xa != NULL) {
        const uint64_t un = dsp->cnt;
        cmb_assert_debug(INT64_MAX >= UINT64_MAX / 2u);
        for (int64_t root = (int64_t)(un / 2u) - 1u; root >= 0u; root--) {
            timeseries_heapify(un, dsp->xa, tsp->ta, tsp->wa,  root);
        }

        for (uint64_t ui = un - 1u; ui > 0u; ui--) {
            cmb_assert_debug(ui < un);
            cmi_dataset_swap(&(dsp->xa[0]), &(dsp->xa[ui]));
            cmi_dataset_swap(&(tsp->ta[0]), &(tsp->ta[ui]));
            cmi_dataset_swap(&(tsp->wa[0]), &(tsp->wa[ui]));
            timeseries_heapify(ui, dsp->xa, tsp->ta, tsp->wa, 0u);
        }
    }

    cmb_assert_debug(cmi_dataset_is_sorted(dsp->cnt, dsp->xa));
}

/* Sort back in ascending order of time */
void cmb_timeseries_sort_t(struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    if (tsp->ta != NULL) {
        const uint64_t un = dsp->cnt;
        cmb_assert_debug(INT64_MAX >= UINT64_MAX / 2u);
        for (int64_t root = (int64_t)(un / 2u) - 1u; root >= 0u; root--) {
            timeseries_heapify(un, tsp->ta, dsp->xa, tsp->wa,  root);
        }

        for (uint64_t ui = un - 1u; ui > 0u; ui--) {
            cmb_assert_debug(ui < un);
            cmi_dataset_swap(&(dsp->xa[0]), &(dsp->xa[ui]));
            cmi_dataset_swap(&(tsp->ta[0]), &(tsp->ta[ui]));
            cmi_dataset_swap(&(tsp->wa[0]), &(tsp->wa[ui]));
            timeseries_heapify(ui, tsp->ta, dsp->xa, tsp->wa, 0u);
        }
    }

    cmb_assert_debug(cmi_dataset_is_sorted(dsp->cnt, tsp->ta));
}

/*
 * Takes a copy before sorting, leaving tsp unchanged.
 */
double cmb_timeseries_median(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(tsp->wa != NULL);

    struct cmb_timeseries tmp_ts = { 0 };
    const uint64_t un = cmb_timeseries_copy(&tmp_ts, tsp);
    cmb_timeseries_sort_x(&tmp_ts);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)(&tmp_ts);
    cmb_assert_debug(dsp->xa != NULL);
    cmb_assert_debug(dsp->cnt == un);

    double wsum = 0.0;
    double *wcum = cmi_calloc(un, sizeof(*wcum));
    for (uint64_t ui = 0u; ui < un; ui++) {
        wsum += tmp_ts.wa[ui];
        wcum[ui] = wsum;
    }

    const double wmid = 0.5 * wsum;
    double r = 0.0;
     for (uint64_t ui = 0u; ui < un - 1; ui++) {
        if ((wcum[ui] <= wmid) && (wcum[ui + 1] > wmid)) {
            cmb_assert_debug(wcum[ui + 1] > wcum[ui]);
            r = dsp->xa[ui] + (dsp->xa[ui + 1]
                            - dsp->xa[ui]) * (wmid - wcum[ui])
                               / (wcum[ui + 1] - wcum[ui]);
            break;
        }
    }

    cmi_free(wcum);
    cmb_timeseries_reset(&tmp_ts);
    return r;
}

void cmb_timeseries_print_fivenum(const struct cmb_timeseries *tsp,
                                  FILE *fp,
                                  const bool lead_ins)
{
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(tsp->wa != NULL);
    cmb_assert_release(fp != NULL);

    struct cmb_timeseries tmp_ts = { 0 };
    const uint64_t un = cmb_timeseries_copy(&tmp_ts, tsp);
    cmb_timeseries_sort_x(&tmp_ts);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)(&tmp_ts);
    cmb_assert_debug(dsp->xa != NULL);
    cmb_assert_debug(dsp->cnt == un);
    const double xmin = dsp->min;
    const double xmax = dsp->max;

    double wsum = 0.0;
    double *wcum = cmi_calloc(un, sizeof(*wcum));
    for (uint64_t ui = 0u; ui < un; ui++) {
        wsum += tmp_ts.wa[ui];
        wcum[ui] = wsum;
    }

    const double w025 = 0.25 * wsum;
    const double w050 = 0.50 * wsum;
    const double w075 = 0.75 * wsum;

    double x025 = 0.0;
    double x050 = 0.0;
    double x075 = 0.0;
    for (uint64_t ui = 0u; ui < un - 1; ui++) {
        if ((wcum[ui] <= w025) && (wcum[ui + 1] > w025)) {
            cmb_assert_debug(wcum[ui + 1] > wcum[ui]);
            x025 = dsp->xa[ui] + (dsp->xa[ui + 1]
                               - dsp->xa[ui]) * (w025 - wcum[ui])
                                  / (wcum[ui + 1] - wcum[ui]);
        }

        if ((wcum[ui] <= w050) && (wcum[ui + 1] > w050)) {
            cmb_assert_debug(wcum[ui + 1] > wcum[ui]);
            x050 = dsp->xa[ui] + (dsp->xa[ui + 1]
                               - dsp->xa[ui]) * (w050 - wcum[ui])
                                  / (wcum[ui + 1] - wcum[ui]);
        }

        if ((wcum[ui] <= w075) && (wcum[ui + 1] > w075)) {
            cmb_assert_debug(wcum[ui + 1] > wcum[ui]);
            x075 = dsp->xa[ui] + (dsp->xa[ui + 1]
                               - dsp->xa[ui]) * (w075 - wcum[ui])
                                  / (wcum[ui + 1] - wcum[ui]);
        }
    }

    cmb_assert_debug((xmin <= x025) && (x025 <= x050)
                  && (x050 <= x075) && (x075 <= xmax));
    const int r = fprintf(fp, "%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g\n",
            ((lead_ins) ? "Min " : ""), xmin,
            ((lead_ins) ? "  Quartile_1 " : "\t"), x025,
            ((lead_ins) ? "  Median " : "\t"), x050,
            ((lead_ins) ? "  Quartile_3 " : "\t"), x075,
            ((lead_ins) ? "  Max " : "\t"), xmax);
    cmb_assert_release(r > 0);

    cmi_free(wcum);
    cmb_timeseries_reset(&tmp_ts);
}
