/* 
 * cmb_dataset.c - basic data collector utilities mainly for debugging purposes
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

#include <stdio.h>

#include "cmb_data.h"
#include "cmb_logger.h"

/* If the compiler decides to issue code for the inlines, do it here */
extern void cmb_summary_init(struct cmb_summary *sup);
extern void cmb_summary_clear(struct cmb_summary *sup);
extern struct cmb_summary *cmb_summary_create(void);
extern void cmb_summary_destroy(struct cmb_summary *sup);
extern uint64_t cmb_summary_count(const struct cmb_summary *sup);
extern double cmb_summary_max(const struct cmb_summary *sup);
extern double cmb_summary_min(const struct cmb_summary *sup);
extern double cmb_summary_mean(const struct cmb_summary *sup);
extern double cmb_summary_variance(const struct cmb_summary *sup);
extern double cmb_summary_stddev(const struct cmb_summary *sup);

extern void cmb_wsummary_init(struct cmb_wsummary *wsup);
extern void cmb_wsummary_clear(struct cmb_wsummary *wsup);
extern struct cmb_wsummary *cmb_wsummary_create(void);
extern void cmb_wsummary_destroy(struct cmb_wsummary *wsup);
extern uint64_t cmb_wsummary_count(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_max(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_min(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_mean(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_variance(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_stddev(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_skewness(const struct cmb_wsummary *wsup);
extern double cmb_wsummary_kurtosis(const struct cmb_wsummary *wsup);
extern void cmb_wsummary_print(const struct cmb_wsummary *wsup, FILE *fp, bool lead_ins);

extern void cmb_dataset_clear(struct cmb_dataset *dsp);
extern uint64_t cmb_dataset_count(const struct cmb_dataset *dsp);
extern double cmb_dataset_min(const struct cmb_dataset *dsp);
extern double cmb_dataset_max(const struct cmb_dataset *dsp);

extern void cmb_timeseries_clear(struct cmb_timeseries *tsp);
extern uint64_t cmb_timeseries_count(const struct cmb_timeseries *tsp);
extern double cmb_timeseries_min(const struct cmb_timeseries *tsp);
extern double cmb_timeseries_max(const struct cmb_timeseries *tsp);

extern void *cmi_malloc(size_t sz);
extern void *cmi_calloc(unsigned int n, size_t sz);
extern void *cmi_realloc(void *p, size_t sz);
extern void cmi_free(void *p);

static const uint64_t cmb_dataset_init_size = 1024;

/**************************** Data summary methods ****************************/
/*
 * Merge two data summaries, updating the statistics. Used e.g. for merging across pthreads.
 *
 * See:
 * Philippe Pébay (2008), "Formulas for Robust, One-Pass Parallel Computation of Covariances and
 *     Arbitrary-Order Statistical Moments" , https://www.osti.gov/servlets/purl/1028931
 *     (Sandia report SAND2008-6212, U.S. Government work, hence public domain.)
 *
 * Note that the target address may point to one of the sources, hence all calculations are done
 * in a temporary variable and the target overwritten only at the end.
 *
 * Returns tgt->cnt, the number of data points in the combined summary.
 */
uint64_t cmb_summary_merge(struct cmb_summary *tgt,
                           const struct cmb_summary *sup1,
                           const struct cmb_summary *sup2) {
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(sup1 != NULL);
    cmb_assert_release(sup2 != NULL);

    struct cmb_summary cs = { 0 };
    cs.cnt = sup1->cnt + sup2->cnt;
    cs.min = (sup1->min < sup2->min) ? sup1->min : sup2->min;
    cs.max = (sup1->max > sup2->max) ? sup1->max : sup2->max;

    const double n1 = (double)sup1->cnt;
    const double n2 = (double)sup2->cnt;
    const double n = (double)cs.cnt;
    const double d21 = sup2->m1 - sup1->m1;
    const double d21_n = d21 / n;
    const double d21_n_2 = d21_n * d21_n;
    const double d21_n_3 = d21_n * d21_n_2;

    cs.m1 = sup1->m1 + n2 * d21_n;
    cs.m2 = sup1->m2 + sup2->m2 + n1 * n2 * d21 * d21_n;
    cs.m3 = sup1->m3 + sup2->m3 + n1 * n2 * (n1 - n2) * d21 * d21_n_2
                    + 3.0 * (n1 * sup2->m2 - n2 * sup1->m2) * d21_n;
    cs.m4 = sup1->m4 + sup2->m4 + n1 * n2 * (n1 * n1 - n1 * n2 + n2 * n2) * d21 * d21_n_3
                    + 6.0 * (n1 * n1 * sup2->m2 + n2 * n2 * sup1->m2) * d21_n_2
                    + 4.0 * (n1 * sup2->m3 - n2 * sup1->m3) * d21_n;

    *tgt = cs;
    return tgt->cnt;
}

/*
 * Add a sample value to the data summary, updating the statistics. See Petay (2008).
 * Adding a single sample is a special case of the merge described there, with n2 = 1.
 *
 * Optimized evaluation sequence as described in:
 * Xiangrui Meng (2015), "Simpler Online Updates for Arbitrary-Order Central Moments"
 *     https://arxiv.org/pdf/1510.04923
 *
 * Returns the updated sample count.
 */
uint64_t cmb_summary_add(struct cmb_summary *sup, const double y) {
    cmb_assert_release(sup != NULL);

    sup->max = (y > sup->max) ? y : sup->max;
    sup->min = (y < sup->min) ? y : sup->min;

    const double d = y - sup->m1;
    const double d_2 = d * d;
    const double d_3 = d * d_2;
    const double n = (double)(++sup->cnt);
    const double d_n = d / n;
    const double d_n_2 = d_n * d_n;
    const double d_n_3 = d_n_2 * d_n;

    sup->m1 += d_n;
    sup->m2 += d * (d - d_n);
    sup->m3 += d * (d_2 - d_n_2) - 3.0 * d_n * sup->m2;
    sup->m4 += d * (d_3 - d_n_3) - 6.0 * d_n_2 * sup->m2 - 4.0 * d_n * sup->m3;

    return sup->cnt;
}

void cmb_summary_print(const struct cmb_summary *sup, FILE *fp, const bool lead_ins) {
    cmb_assert_release(sup != NULL);
    cmb_assert_release(fp != NULL);

    int r = fprintf(fp, "%s%8llu", ((lead_ins)? "N ": ""), sup->cnt);
    cmb_assert_release(r > 0);
    if (sup->cnt > 0u) {
        const double mean = cmb_summary_mean(sup);
        r = fprintf(fp, "%s%#8.4g",
                ((lead_ins) ? "  Mean " : "\t"), mean);
        cmb_assert_release(r > 0);
    }

    if (sup->cnt > 1u) {
        const double var = cmb_summary_variance(sup);
        const double std = sqrt(var);
        r = fprintf(fp, "%s%#8.4g",
                ((lead_ins) ? "  StdDev " : "\t"), std);
        cmb_assert_release(r > 0);
        r = fprintf(fp, "%s%#8.4g",
                ((lead_ins) ? "  Variance " : "\t"), var);
        cmb_assert_release(r > 0);
    }

    if (sup->cnt > 2u) {
        const double skew = cmb_summary_skewness(sup);
        r = fprintf(fp, "%s%#8.4g",
            ((lead_ins) ? "  Skewness " : "\t"), skew);
        cmb_assert_release(r > 0);
    }

    if (sup->cnt > 3u) {
        const double kurt = cmb_summary_kurtosis(sup);
        r = fprintf(fp, "%s%#8.4g",
            ((lead_ins) ? "  Kurtosis " : "\t"), kurt);
        cmb_assert_release(r > 0);
    }

    r = fprintf(fp, "\n");
    cmb_assert_release(r > 0);
}

double cmb_summary_skewness(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);

    double r = 0.0;
    if (sup->cnt > 2u) {
        /* Estimate population skewness */
        const double dn = (double)sup->cnt;
        const double g = sqrt(dn) * sup->m3 / pow(sup->m2, 1.5);

        /* Correction for finite sample */
        r = sqrt(dn * (dn - 1.0)) * g / (dn - 2.0);
    }

    return r;
}

/* Sample excess kurtosis */
double cmb_summary_kurtosis(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);

    double r = 0.0;
    if (sup->cnt > 3u) {
        /* Estimate population excess kurtosis */
        const double dn = (double)sup->cnt;
        const double g = dn * sup->m4 / (sup->m2 * sup->m2) - 3.0;

        /* Correction for finite sample */
        r = (dn - 1.0) / ((dn - 2.0) * (dn - 3.0)) * ((dn + 1.0) * g + 6.0);
    }

    return r;
}

/************************** Weighted summary methods **************************/
/*
 * Add a weighted sample value to the data summary, updating the statistics.
 * See: Pébay & al, "Numerically stable, scalable formulas for parallel and
 *      online computation of higher-order multivariate central moments with arbitrary weights",
 *      Computational Statistics (2016) 31:1305–1325
 * Adding a single sample is a special case of the merge described there, with n2 = 1.
 *
 * Returns the updated sample count.
 */
uint64_t cmb_wsummary_add(struct cmb_wsummary *wsup, const double y, const double w) {
    cmb_assert_release(wsup != NULL);

    struct cmb_summary *sup = (struct cmb_summary *)wsup;
    sup->max = (y > sup->max) ? y : sup->max;
    sup->min = (y < sup->min) ? y : sup->min;

    const double d = y - sup->m1;
    const double d_2 = d * d;
    const double d_3 = d * d_2;
    const double ws = wsup->wsum + w;
    const double d_w = d / ws;
    const double d_w_2 = d_w * d_w;
    const double d_w_3 = d_w_2 * d_w;

    sup->cnt++;
    wsup->wsum = ws;
    sup->m1 += w * d_w;
    sup->m2 += d * (d - d_w);
    sup->m3 += d * (d_2 - d_w_2) - 3.0 * d_w * sup->m2;
    sup->m4 += d * (d_3 - d_w_3) - 6.0 * d_w_2 * sup->m2 - 4.0 * d_w * sup->m3;

    return sup->cnt;
}

/*
 * Merge two weighted data summaries, updating the statistics. Used e.g. for merging across pthreads.
 * See: Pébay & al, "Numerically stable, scalable formulas for parallel and
 *      online computation of higher-order multivariate central moments with arbitrary weights",
 *      Computational Statistics (2016) 31:1305–1325
 *
 * Note that the target address may point to one of the sources, hence all calculations are done
 * in a temporary variable and the target overwritten only at the end.
 *
 * Returns tgt->cnt, the number of data points in the combined summary.
 */
uint64_t cmb_wsummary_merge(struct cmb_wsummary *tgt,
                            const struct cmb_wsummary *ws1,
                            const struct cmb_wsummary *ws2) {
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(ws1 != NULL);
    cmb_assert_release(ws2 != NULL);

    struct cmb_wsummary tws = { 0 };
    struct cmb_summary *ts = (struct cmb_summary *)(&tws);
    const struct cmb_summary *sup1 = (struct cmb_summary *)ws1;
    const struct cmb_summary *sup2 = (struct cmb_summary *)ws2;

    ts->cnt = sup1->cnt + sup2->cnt;
    ts->min = (sup1->min < sup2->min) ? sup1->min : sup2->min;
    ts->max = (sup1->max > sup2->max) ? sup1->max : sup2->max;

    const double w1 = ws1->wsum;
    const double w2 = ws2->wsum;
    const double ws = tws.wsum = w1 + w2;
    const double d21 = sup2->m1 - sup1->m1;
    const double d21_w = d21 / ws;
    const double d21_w_2 = d21_w * d21_w;
    const double d21_w_3 = d21_w * d21_w_2;

    ts->m1 = sup1->m1 + w2 * d21_w;
    ts->m2 = sup1->m2 + sup2->m2 + w1 * w2 * d21 * d21_w;
    ts->m3 = sup1->m3 + sup2->m3 + w1 * w2 * (w1 - w2) * d21 * d21_w_2
                    + 3.0 * (w1 * sup2->m2 - w2 * sup1->m2) * d21_w;
    ts->m4 = sup1->m4 + sup2->m4 + w1 * w2 * (w1 * w1 - w1 * w2 + w2 * w2) * d21 * d21_w_3
                    + 6.0 * (w1 * w1 * sup2->m2 + w2 * w2 * sup1->m2) * d21_w_2
                    + 4.0 * (w1 * sup2->m3 - w2 * sup1->m3) * d21_w;

    *tgt = tws;
    return ts->cnt;
}

/****************************** Data set methods ******************************/

struct cmb_dataset *cmb_dataset_create(void) {
    struct cmb_dataset *dsp = cmi_malloc(sizeof *dsp);
    dsp->xa = NULL;
    cmb_dataset_init(dsp);

    return dsp;
}

void cmb_dataset_init(struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);

    dsp->cursize = 0u;
    dsp->cnt = 0u;
    dsp->min = DBL_MAX;
    dsp->max = -DBL_MAX;
    if (dsp->xa != NULL) {
        cmi_free(dsp->xa);
        dsp->xa = NULL;
    }
}

void cmb_dataset_destroy(struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);

    cmb_dataset_clear(dsp);
    cmi_free(dsp);
}

static void cmi_dataset_expand(struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cursize < UINT64_MAX / 2u);

    if (dsp->cursize == 0) {
        cmb_assert_release(dsp->xa == NULL);
        dsp->cursize = cmb_dataset_init_size;
        dsp->xa = cmi_malloc((size_t)(dsp->cursize * sizeof(*(dsp->xa))));
    }
    else {
        cmb_assert_release(dsp->xa != NULL);
        dsp->cursize *= 2;
        dsp->xa = cmi_realloc(dsp->xa, (size_t)(dsp->cursize * sizeof(*(dsp->xa))));
    }
}

uint64_t cmb_dataset_add(struct cmb_dataset *dsp, const double x) {
    cmb_assert_release(dsp != NULL);

    dsp->max = (x > dsp->max) ? x : dsp->max;
    dsp->min = (x < dsp->min) ? x : dsp->min;

    if (dsp->cnt == dsp->cursize) {
        cmi_dataset_expand(dsp);
    }

    /* May have null data array on entry, but not by here */
    cmb_assert_release(dsp->xa != NULL);
    cmb_assert_release(dsp->cnt < dsp->cursize);

    dsp->xa[dsp->cnt] = x;
    dsp->cnt++;

    return dsp->cnt;
}

/*
 * Sorting function for data array, sorting from smallest to largest value.
 *
 * Since stack space might be at a premium in this coroutine-based simulation
 * library, we use a non-recursive heapsort even if the recursive quicksort
 * may be slightly faster. The dataset sorting function will probably not be
 * in any inner loop, and the performance penalty (if any) is a worthwhile
 * tradeoff for the stack space economy.
 */

static void cmi_data_swap(double *a, double *b) {
    const double tmp = *a;
    *a = *b;
    *b = tmp;
}

#ifndef NASSERT
/* Code only used for checking invariants */
static bool cmi_data_is_sorted(const uint64_t un, const double arr[un]) {
    if ((arr == NULL) || (un < 2u)) {
        return true;
    }

    for (uint64_t ui = 0u; ui < un - 1u; ui++) {
        if (arr[ui] > arr[ui + 1u]) {
            return false;
        }
    }

    /* No evidence to the contrary */
    return true;
}

/* Check for heap condition starting from root ui, testing this subtree only */
static bool cmi_data_is_max_heap(const uint64_t un, double const arr[un], const uint64_t uroot) {
    cmb_assert_release(arr != NULL);
    if ((un > 1u) && (uroot <= un)) {
        uint64_t *queue = cmi_malloc(un * sizeof(uint64_t));
        uint64_t uhead = 0u;
        uint64_t utail = 0u;
        queue[utail++] = uroot;
        while (uhead < utail) {
            const uint64_t ucur = queue[uhead++];
            const uint64_t ucl = 2u * ucur + 1u;
            const uint64_t ucr = 2u * ucur + 2u;

            if (ucl < un) {
                if (arr[ucur] < arr[ucl]) {
                    free(queue);
                    return false;
                }

                queue[utail++] = ucl;
            }

            if (ucr < un) {
                if (arr[ucur] < arr[ucr]) {
                    free(queue);
                    return false;
                }

                queue[utail++] = ucr;
            }
        }
        free(queue);
    }

    /* No evidence to the contrary */
    return true;
}

#endif /* ifndef NASSERT */

/* Establish max heap condition in dataset array starting from uroot */
static void cmi_dataset_heapify(const uint64_t un, double arr[un], uint64_t uroot) {
    cmb_assert_release(arr != NULL);
    uint64_t ucl = 2u * uroot + 1u;
    uint64_t ucr = 2u * uroot + 2u;

    for (;;) {
        uint64_t ubig = uroot;
        cmb_assert_debug(ubig < un);
        if ((ucl < un) && (arr[ucl] > arr[ubig])) {
            ubig = ucl;
        }

        if ((ucr < un) && (arr[ucr] > arr[ubig])) {
            ubig = ucr;
        }

        if (ubig != uroot) {
            cmb_assert_debug((uroot < un) && (ubig < un));
            cmi_data_swap(&arr[uroot], &arr[ubig]);
            uroot = ubig;
            ucl = 2u * uroot + 1u;
            ucr = 2u * uroot + 2u;
        }
        else {
            /* Heap property is satisfied */
            break;
        }
    }

    cmb_assert_debug(cmi_data_is_max_heap(un, arr, uroot));
}

/* Heapsort from smallest to largest value */
void cmb_dataset_sort(struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);

    if (dsp->xa != NULL) {
        const uint64_t un = dsp->cnt;
        double *arr = dsp->xa;
        cmb_assert_debug(INT64_MAX >= UINT64_MAX / 2);
        for (int64_t root = (int64_t)(un / 2u) - 1u; root >= 0u; root--) {
            cmi_dataset_heapify(un, arr, root);
        }

        for (uint64_t ui = un - 1u; ui > 0u; ui--) {
            cmb_assert_debug(ui < un);
            cmi_data_swap(&arr[0], &arr[ui]);
            cmi_dataset_heapify(ui, arr, 0u);
        }
    }

    cmb_assert_debug(cmi_data_is_sorted(dsp->cnt, dsp->xa));
}

uint64_t cmb_dataset_copy(struct cmb_dataset *tgt,
                          const struct cmb_dataset *src) {
    cmb_assert_release(src != NULL);
    cmb_assert_release(tgt != NULL);

    tgt->cnt = src->cnt;
    tgt->cursize = src->cursize;
    tgt->min = src->min;
    tgt->max = src->max;

    if (tgt->xa != NULL) {
        cmi_free(tgt->xa);
        tgt->xa = NULL;
    }

    if (src->xa != NULL) {
        tgt->xa = cmi_calloc(tgt->cursize, sizeof *(tgt->xa));
        cmi_memcpy(tgt->xa, src->xa, tgt->cursize * sizeof *(tgt->xa));
    }

    return tgt->cnt;
}

/* Overwrites any existing content of data summary */
uint64_t cmb_dataset_summarize(const struct cmb_dataset *dsp, struct cmb_summary *dsump) {
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsump != NULL);

    cmb_summary_init(dsump);

    for (uint64_t ui = 0; ui < dsp->cnt; ui++) {
        cmb_summary_add(dsump, dsp->xa[ui]);
    }

    return dsump->cnt;
}

/* Assumes that v is already sorted */
static double cmi_data_array_median(const unsigned n, const double v[n]) {
    cmb_assert_debug(cmi_data_is_sorted(n, v));
    double r;
    if (n % 2u == 0u) {
        r =  (double)(v[n/2u - 1u] + v[n / 2u]) / 2.0;
    }
    else {
        r = (double)(v[n / 2u]);
    }

    return r;
}

double cmb_dataset_median(const struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);

    double r = 0.0;
    if (dsp->xa != NULL) {
        struct cmb_dataset dup = { 0 };
        cmb_dataset_copy(&dup, dsp);
        cmb_dataset_sort(&dup);
        r = cmi_data_array_median(dup.cnt, dup.xa);
        cmb_dataset_clear(&dup);
    }
    else {
        cmb_warning(stderr, "Cannot take median without any data.");
    }

    return r;
}

/*
 * Calculate and print five-number summary (quantiles).
 */
void cmb_dataset_print_fivenum(const struct cmb_dataset *dsp, FILE *fp, const bool lead_ins) {
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(fp != NULL);

    if (dsp->xa != NULL) {
        struct cmb_dataset dsc = { 0 };
        cmb_dataset_copy(&dsc, dsp);
        cmb_dataset_sort(&dsc);

        const double min = dsc.min;
        const double max = dsc.max;
        const double med = cmi_data_array_median(dsc.cnt, dsc.xa);

        const unsigned lhsz = dsc.cnt / 2;
        const double q1 = cmi_data_array_median(lhsz, dsc.xa);
        double q3;
        const unsigned uhsz = dsc.cnt - lhsz;
        if ((dsc.cnt % 2) == 0) {
            /* Even number of entries */
            q3 = cmi_data_array_median(uhsz, &(dsc.xa[lhsz]));
        } else {
            /* Odd number of entries, exclude the median entry */
            q3 = cmi_data_array_median(uhsz - 1, &(dsc.xa[lhsz + 1]));
        }

        const int r = fprintf(fp, "%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g\n",
                ((lead_ins) ? "Min " : ""), min,
                ((lead_ins) ? "  Quartile_1 " : "\t"), q1,
                ((lead_ins) ? "  Median " : "\t"), med,
                ((lead_ins) ? "  Quartile_3 " : "\t"), q3,
                ((lead_ins) ? "  Max " : "\t"), max);
        cmb_assert_release(r > 0);
        cmb_dataset_clear(&dsc);
    }
    else {
        cmb_warning(fp, "No data to display in five-number summary");
    }
}

void cmb_dataset_print(const struct cmb_dataset *dsp, FILE *fp) {
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(fp != NULL);

    if (dsp->xa != NULL) {
        for (uint64_t l = 0; l < dsp->cnt; l++) {
            fprintf(fp, "%g\n", dsp->xa[l]);
        }
    }
    else {
        cmb_warning(fp, "No data to print");
    }
}

/*
 * Print a simple character-based histogram of the data.
 * The only external callable functions are cmb_dataset_print_histogram and
 * cmb_timeseries_print_histogram, the rest (cmi_*) are internal helper functions.
 */

static void cmi_data_print_stars(FILE *fp, const double scale, const double binval) {
    uint64_t nstars = (uint64_t)(binval / scale);
    const double rem = binval / scale - (double)nstars;

    while (nstars-- > 0) {
        const int r = fprintf(fp, "*");
        cmb_assert_release(r > 0);
    }

    const double min_rem_plus = 0.5;
    if (rem >= min_rem_plus) {
        const int r = fprintf(fp, "+");
        cmb_assert_release(r > 0);
    }
    else if (rem > 0.0) {
        const int r = fprintf(fp, "-");
        cmb_assert_release(r > 0);
    }

    const int r = fprintf(fp, "\n");
    cmb_assert_release(r > 0);
}

static void cmi_data_print_chars(FILE *fp, const char *str, const uint16_t repeats) {
    cmb_assert_release(str != NULL);
    for (uint16_t ui = 0; ui < repeats; ui++) {
        const int r = fprintf(fp, "%s", str);
        cmb_assert_release(r > 0);
    }
}

static void cmi_data_print_line(FILE *fp, const char *str, const uint16_t repeats) {
    cmi_data_print_chars(fp, str, repeats);
    int r = fprintf(fp, "\n");
    cmb_assert_release(r > 0);
}

/*
 * Histogram data structure. Note that the bins are real-valued (not integer) to
 * work both with traditional histograms for cmb_dataset and for time-weighted
 * ones for cmb_timeseries where each value is counted proportional to the time
 * interval between it and the next value.
 */
struct cmi_data_histogram {
    unsigned num_bins;
    double binsize;
    double low_lim;
    double high_lim;
    double binmax;
    double *hbins;
};

static struct cmi_data_histogram *cmi_data_create_histogram(const unsigned num_bins, const double low_lim, const double high_lim) {
    cmb_assert_debug(num_bins > 0u);
    const double range = high_lim - low_lim;
    cmb_assert_debug(range > 0.0);

    struct cmi_data_histogram *hp = cmi_malloc(sizeof(*hp));
    hp->num_bins = num_bins + 2u;
    hp->binsize = range / (double)(num_bins);
    hp->low_lim = low_lim;
    hp->high_lim = high_lim;
    hp->binmax = 0.0;
    hp->hbins = cmi_calloc(hp->num_bins, sizeof(*(hp->hbins)));

    return hp;
}

/* Calculate the histogram data for a dataset, weighting each x-value equally as 1.0. */
static void cmi_dataset_fill_histogram(struct cmi_data_histogram *hp, const uint64_t n, const double xa[n]) {
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(n > 0u);
    cmb_assert_debug(xa != NULL);

    /* Distribute x-values to bins */
    for (uint64_t ui = 0u; ui < n; ui++) {
        /* In what bin does this x-value belong? */
        uint16_t bin;
        if (xa[ui] < hp->low_lim) {
            bin = 0u;
        }
        else if (xa[ui] > hp->high_lim) {
            bin = hp->num_bins - 1u;
        }
        else {
            bin = 1u + (uint16_t)((xa[ui] - hp->low_lim) / hp->binsize);
        }

        /* Add it to that bin and note the high-water mark */
        hp->hbins[bin] += 1.0;
        if (hp->hbins[bin] > hp->binmax) {
            hp->binmax = hp->hbins[bin];
        }
    }
}

static void cmi_data_print_histogram(const struct cmi_data_histogram *hp, FILE *fp) {
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(fp != NULL);

    /* Length of separator lines */
    static const uint16_t line_length = 80u;

    /* Max width of the histogram bars */
    const uint16_t max_stars = 50u;
    const double scale = hp->binmax / (double)max_stars;

    /* Print the histogram */
    cmi_data_print_line(fp, "-", line_length);
    int r = fprintf(fp, "( -Infinity, %#10.4g)   |", hp->low_lim);
    cmb_assert_release(r > 0);
    cmi_data_print_stars(fp, scale, hp->hbins[0u]);
    for (unsigned ui = 1u; ui < hp->num_bins - 1u; ui++) {
        r = fprintf(fp, "[%#10.4g, %#10.4g)   |",
                hp->low_lim + (ui - 1u) * hp->binsize,
                hp->low_lim + ui * hp->binsize);
        cmb_assert_release(r > 0);
        cmi_data_print_stars(fp, scale, hp->hbins[ui]);
    }

    r = fprintf(fp, "[%#10.4g,  Infinity )   |", hp->high_lim);
    cmb_assert_release(r > 0);
    cmi_data_print_stars(fp, scale, hp->hbins[hp->num_bins - 1u]);
    cmi_data_print_line(fp, "-", line_length);
}

static void cmi_data_destroy_histogram(struct cmi_data_histogram *hp) {
    cmb_assert_release(hp != NULL);
    cmi_free(hp->hbins);
    cmi_free(hp);
}

/* The external callable function to print a histogram */
void cmb_dataset_print_histogram(const struct cmb_dataset *dsp, FILE *fp,
                                 const unsigned num_bins, double low_lim, double high_lim) {
     cmb_assert_release(dsp != NULL);
     cmb_assert_release(num_bins > 0u);
     cmb_assert_release(high_lim >= low_lim);

     if (dsp->xa == NULL) {
         cmb_assert_debug(dsp->cnt == 0u);
         cmb_warning(fp, "No data to display in histogram");
         return;
    }

    if (low_lim == high_lim) {
        /* Autoscale to dataset range */
        low_lim = dsp->min;
        high_lim = dsp->max;
    }

    struct cmi_data_histogram *hp = NULL;
    hp = cmi_data_create_histogram(num_bins, low_lim, high_lim);
    cmi_dataset_fill_histogram(hp, dsp->cnt, dsp->xa);
    cmi_data_print_histogram(hp, fp);

    cmi_data_destroy_histogram(hp);
}

/*
 * Calculate the first max_lag autocorrelation coefficients.
 */
void cmb_dataset_ACF(const struct cmb_dataset *dsp, const unsigned max_lag, double acf[max_lag + 1u])
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->xa != NULL);
    cmb_assert_release(dsp->cnt > 1);
    cmb_assert_release((max_lag > 0u) && (max_lag < dsp->cnt));

    /* Calculate mean and variance in a single pass, similar to cmb_summary() above */
    double m1 = 0.0;
    double m2 = 0.0;
    for (uint64_t ui = 0; ui < dsp->cnt; ui++) {
        const double d = dsp->xa[ui] - m1;
        const double d_n = d / ((double)(ui + 1u));
        m1 += d_n;
        m2 += d * (d - d_n);
    }
    const double var = m2 / ((double)(dsp->cnt - 1u));

    acf[0] = 1.0;
    const double min_acf_variance = 1e-9;
    if (var < min_acf_variance) {
        /* Would be numerically unstable to divide by that */
        cmb_warning(stderr, "Dataset nearly constant (variance %g), ACFs rounded to zero", var);
        for (unsigned ui = 1; ui <= max_lag; ui++) {
            acf[ui] = 0.0;
        }
    }
    else {
        for (unsigned ulag = 1; ulag <= max_lag; ulag++) {
            double dk = 0.0;
            const uint64_t ustop = dsp->cnt - ulag;
            for (uint64_t ui = 0; ui < ustop; ui++) {
                dk += (dsp->xa[ui] - m1) * (dsp->xa[ui + ulag] - m1);
            }
            const double acov = dk / ((double)(ustop));
            acf[ulag] = acov / var;
            cmb_assert_debug((acf[ulag] >= -1.0) && (acf[ulag] <= 1.0));
        }
    }
}

/*
 * Calculate the first n partial autocorrelation coefficients using the Durbin-Levinson
 * algorithm, optionally using previously calculated ACFs to avoid repeating a
 * computationally expensive step.
 */
void cmb_dataset_PACF(const struct cmb_dataset *dsp, const unsigned max_lag,
                                 double pacf[max_lag + 1u], double acf[max_lag + 1u]) {
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->xa != NULL);
    cmb_assert_release(dsp->cnt > 1u);
    cmb_assert_release((max_lag > 0u) && (max_lag < dsp->cnt - 1u));
    cmb_assert_release(pacf != NULL);

    bool free_acf = false;
    if (acf == NULL) {
        acf = cmi_malloc((max_lag + 1u) * sizeof(double));
        free_acf = true;
        cmb_dataset_ACF(dsp, max_lag, acf);
    }

    /* Create an intermediary un * un matrix */
    double **phi = cmi_malloc((max_lag + 1u) * sizeof(double *));
    phi[0] = cmi_calloc((max_lag + 1) * (max_lag + 1), sizeof(double));
    for (unsigned ui = 1u; ui <= max_lag; ui++) {
        phi[ui] = phi[0] + ui * max_lag;
    }

    /* Per definition */
    pacf[0] = 1.0;
    pacf[1] = acf[1];
    phi[1][1] = pacf[1];

    /* Calculate phi[k][j], the j-th coefficient for a k-th order autoregression model */
    for (unsigned uk = 2u; uk <= max_lag; ++uk) {
        double numsum = 0.0;
        for (unsigned uj = 1u; uj < uk; ++uj) {
            numsum += phi[uk - 1][uj] * acf[uk - uj];
        }

        double densum = 0.0;
        for (unsigned uj = 1u; uj < uk; ++uj) {
            densum += phi[uk - 1][uj] * acf[uj];
        }

        /* The k-th PACF coefficient is the k-th autoregression coefficient phi[k][k] */
        phi[uk][uk] = (acf[uk] - numsum) / (1.0 - densum);
        pacf[uk] = phi[uk][uk];
        cmb_assert_debug((pacf[uk] >= -1.0) && (pacf[uk] <= 1.0));

        /* Update everything else for the next iteration */
        for (unsigned uj = 1u; uj < uk; ++uj) {
            phi[uk][uj] = phi[uk - 1u][uj] - phi[uk][uk] * phi[uk - 1u][uk - uj];
        }
    }

    cmi_free(phi[0]);
    cmi_free(phi);
    if (free_acf) {
        cmi_free(acf);
    }
}

static void cmi_data_print_bar(FILE *fp, const double acfval, const uint16_t max_bar_width) {
    cmb_assert_release((acfval >= -1.0) && (acfval <= 1.0));

    const double bar_width = (double)max_bar_width * fabs(acfval);
    const uint16_t num_stars = (uint16_t)floor(bar_width);
    cmb_assert_debug(num_stars <= max_bar_width);
    const double rem = bar_width - num_stars;
    cmb_assert_debug((rem >= 0.0) && (rem < 1.0));

    /* Smallest remainder to qualify for a plus sign */
    const double min_rem_plus = 0.5;

    if (acfval < 0.0) {
        const uint16_t num_spaces = max_bar_width - num_stars - 1;
        cmi_data_print_chars(fp, " ", num_spaces);

        if (rem > min_rem_plus) {
            const int r = fprintf(fp, "+");
            cmb_assert_release(r > 0);
        }
        else if (rem > 0.0) {
            const int r = fprintf(fp, "-");
            cmb_assert_release(r > 0);
        }
        else {
            const int r = fprintf(fp, " ");
            cmb_assert_release(r > 0);
        }

        cmi_data_print_chars(fp, "*", num_stars);
        const int r = fprintf(fp, "|");
        cmb_assert_release(r > 0);
    }
    else {
        const uint16_t num_spaces = max_bar_width;
        cmi_data_print_chars(fp, " ", num_spaces);
        int r = fprintf(fp, "|");
        cmb_assert_release(r > 0);
        cmi_data_print_chars(fp, "*", num_stars);

        if (rem > min_rem_plus) {
            r = fprintf(fp, "+");
            cmb_assert_release(r > 0);
        }
        else if (rem > 0.0) {
            r = fprintf(fp, "-");
            cmb_assert_release(r > 0);
        }
    }
}

void cmb_dataset_print_correlogram(const struct cmb_dataset *dsp, FILE *fp,
                                          const unsigned max_lag, double acf[max_lag + 1u]) {
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->xa != NULL);
    cmb_assert_release(dsp->cnt > 1u);
    cmb_assert_release((max_lag > 0u) && (max_lag <= dsp->cnt) && (max_lag < UINT16_MAX));

    bool free_acf = false;
    if (acf == NULL) {
        acf = cmi_malloc((max_lag + 1u) * sizeof(double));
        free_acf = true;
        cmb_dataset_ACF(dsp, max_lag, acf);
    }

    /* Length of separator lines / axes */
    static const uint16_t line_length = 80u;

    /* Max width of the bar either side of zero */
    const uint16_t max_bar_width = (line_length - 14u) / 2u;

    cmi_data_print_chars(fp, " ", 11u);
    int r = fprintf(fp, "-1.0");
    cmb_assert_release(r > 0);
    cmi_data_print_chars(fp, " ", max_bar_width - 3u);
    r = fprintf(fp, "0.0");
    cmb_assert_release(r > 0);
    cmi_data_print_chars(fp, " ", max_bar_width - 3u);
    r = fprintf(fp, "1.0\n");
    cmb_assert_release(r > 0);

    cmi_data_print_line(fp, "-", line_length);
    for (unsigned ui = 1u; ui <= max_lag; ui++) {
        r = fprintf(fp, "%4u  %#6.3f ", ui, acf[ui]);
        cmb_assert_release(r > 0);
        cmi_data_print_bar(fp, acf[ui], max_bar_width);
        r = fprintf(fp, "\n");
        cmb_assert_release(r > 0);
    }

    cmi_data_print_line(fp, "-", line_length);
    if (free_acf) {
        cmi_free(acf);
    }
}

/**************************** Time series methods ****************************/

struct cmb_timeseries *cmb_timeseries_create(void) {
    struct cmb_timeseries *tsp = cmi_malloc(sizeof *tsp);
    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    dsp->xa = NULL;
    tsp->ta = NULL;
    tsp->wa = NULL;
    cmb_timeseries_init(tsp);

    return tsp;
}

void cmb_timeseries_init(struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);

    cmb_dataset_init((struct cmb_dataset *)tsp);
    if (tsp->ta != NULL) {
        cmi_free(tsp->ta);
        tsp->ta = NULL;
    }

    if (tsp->wa != NULL) {
        cmi_free(tsp->wa);
        tsp->wa = NULL;
    }
}

void cmb_timeseries_destroy(struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);

    cmb_timeseries_clear(tsp);
    cmi_free(tsp);
}

static void cmi_timeseries_expand(struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);

    /* First expand x-vector and increment cursize */
    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    cmi_dataset_expand(dsp);

    if (tsp->ta == NULL) {
        /* Just allocated the first chunk of xa array, do same for ta and wa */
        cmb_assert_debug(dsp->cursize == cmb_dataset_init_size);
        cmb_assert_debug(tsp->wa == NULL);
        tsp->ta = cmi_malloc((size_t)(cmb_dataset_init_size * sizeof(*(tsp->ta))));
        tsp->wa = cmi_malloc((size_t)(cmb_dataset_init_size * sizeof(*(tsp->wa))));
    }
    else {
        /* Already exist, expand the xa and ta arrays */
        cmb_assert_debug((tsp->ta != NULL) && (tsp->wa != NULL));
        tsp->ta = cmi_realloc(tsp->ta, (size_t)(dsp->cursize * sizeof(*(tsp->ta))));
        tsp->wa = cmi_realloc(tsp->wa, (size_t)(dsp->cursize * sizeof(*(tsp->wa))));
    }
}

uint64_t cmb_timeseries_add(struct cmb_timeseries *tsp, const double x, const double t) {
    cmb_assert_release(tsp != NULL);

    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    cmb_assert_debug((dsp->cnt == 0u) || ((tsp->ta != NULL) && (tsp->ta[dsp->cnt - 1u] <= t)));
    if (dsp->cnt == dsp->cursize) {
        /* Full (or not even created) data arrays, resize arrays xa, ta, and wa */
        cmi_timeseries_expand(tsp);
    }

    const uint64_t ui_new = dsp->cnt;
    (void)cmb_dataset_add(dsp, x);
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

uint64_t cmb_timeseries_finalize(struct cmb_timeseries *tsp, const double t) {
    cmb_assert_release(tsp != NULL);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    const uint64_t n = dsp->cnt;
    cmb_assert_release((n == 0u) || ((tsp->ta != NULL) && (tsp->ta[n - 1u] <= t)));
    const double x = dsp->xa[n - 1u];
    const uint64_t r = cmb_timeseries_add(tsp, x, t);

    cmb_assert_debug((r == n + 1) && (dsp->xa[n] == x) && (tsp->ta[n] == t));
    return r;
}

/*
 * Summarize the timeseries into a weighted data set, using the time intervals between x-values as the weighing.
 * The last x-value in the timeseries has no duration and is not included in the summary.
 * Call cmb_timeseries_finalize() first to include the last x-value wit a non-zero duration.
 */
uint64_t cmb_timeseries_summarize(const struct cmb_timeseries *tsp, struct cmb_wsummary *wsp) {
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(tsp->ta != NULL);
    cmb_assert_release(wsp != NULL);

    cmb_wsummary_clear(wsp);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    cmb_assert_debug(dsp->xa != NULL);

    const uint64_t un = cmb_timeseries_count(tsp);
    cmb_assert_debug(un > 0u);
    for (uint64_t ui = 0u; ui < un - 1u; ui++) {
        const double x = dsp->xa[ui];
        const double w = tsp->wa[ui];
        cmb_wsummary_add(wsp, x, w);
    }

    cmb_assert_debug(cmb_wsummary_count(wsp) == un - 1u);
    return un - 1u;
}

void cmb_timeseries_print(const struct cmb_timeseries *tsp, FILE *fp) {
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
        cmb_warning(fp, "No data to print");
    }
}

/*
 * Calculate a histogram with time-weighed values, each x-value counted with
 * the t-value interval to the next x-value, i.e. the holding time.
 */
static void cmi_timeseries_fill_histogram(struct cmi_data_histogram *hp, const uint64_t n,
                                            const double xa[n], const double wa[n]) {
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

void cmb_timeseries_print_histogram(const struct cmb_timeseries *tsp, FILE *fp,
                                    const uint16_t num_bins, double low_lim, double high_lim) {
    cmb_assert_release(tsp != NULL);
    cmb_assert_release(fp != NULL);
    cmb_assert_release(num_bins > 0u);
    cmb_assert_release(high_lim >= low_lim);

    const struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    if (dsp->xa == NULL) {
        cmb_assert_debug(dsp->cnt == 0u);
        cmb_assert_debug(tsp->ta == NULL);
        cmb_warning(fp, "No data to display in histogram");
        return;
    }

    if (low_lim == high_lim) {
        /* Autoscale to dataset range */
        low_lim = dsp->min;
        high_lim = dsp->max;
    }

    struct cmi_data_histogram *hp = NULL;
    hp = cmi_data_create_histogram(num_bins, low_lim, high_lim);
    cmi_timeseries_fill_histogram(hp, dsp->cnt, dsp->xa, tsp->wa);
    cmi_data_print_histogram(hp, fp);

    cmi_data_destroy_histogram(hp);
}

uint64_t cmb_timeseries_copy(struct cmb_timeseries *tgt,
                          const struct cmb_timeseries *src) {
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
static void cmi_timeseries_heapify(const uint64_t un, double keya[un], double da1[un], double da2[un], uint64_t uroot) {
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
            cmi_data_swap(&keya[uroot], &keya[ubig]);
            cmi_data_swap(&da1[uroot], &da1[ubig]);
            cmi_data_swap(&da2[uroot], &da2[ubig]);
            uroot = ubig;
            ucl = 2u * uroot + 1u;
            ucr = 2u * uroot + 2u;
        }
        else {
            /* Heap property is satisfied */
            break;
        }
    }

    cmb_assert_debug(cmi_data_is_max_heap(un, keya, uroot));
}

/* Heapsort from smallest to largest x-value */
void cmb_timeseries_sort_x(struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);

    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    if (dsp->xa != NULL) {
        const uint64_t un = dsp->cnt;
        cmb_assert_debug(INT64_MAX >= UINT64_MAX / 2u);
        for (int64_t root = (int64_t)(un / 2u) - 1u; root >= 0u; root--) {
            cmi_timeseries_heapify(un, dsp->xa, tsp->ta, tsp->wa,  root);
        }

        for (uint64_t ui = un - 1u; ui > 0u; ui--) {
            cmb_assert_debug(ui < un);
            cmi_data_swap(&(dsp->xa[0]), &(dsp->xa[ui]));
            cmi_data_swap(&(tsp->ta[0]), &(tsp->ta[ui]));
            cmi_data_swap(&(tsp->wa[0]), &(tsp->wa[ui]));
            cmi_timeseries_heapify(ui, dsp->xa, tsp->ta, tsp->wa, 0u);
        }
    }

    cmb_assert_debug(cmi_data_is_sorted(dsp->cnt, dsp->xa));
}

/* Sort back in ascending order of time */
void cmb_timeseries_sort_t(struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);

    struct cmb_dataset *dsp = (struct cmb_dataset *)tsp;
    if (tsp->ta != NULL) {
        const uint64_t un = dsp->cnt;
        cmb_assert_debug(INT64_MAX >= UINT64_MAX / 2u);
        for (int64_t root = (int64_t)(un / 2u) - 1u; root >= 0u; root--) {
            cmi_timeseries_heapify(un, tsp->ta, dsp->xa, tsp->wa,  root);
        }

        for (uint64_t ui = un - 1u; ui > 0u; ui--) {
            cmb_assert_debug(ui < un);
            cmi_data_swap(&(dsp->xa[0]), &(dsp->xa[ui]));
            cmi_data_swap(&(tsp->ta[0]), &(tsp->ta[ui]));
            cmi_data_swap(&(tsp->wa[0]), &(tsp->wa[ui]));
            cmi_timeseries_heapify(ui, tsp->ta, dsp->xa, tsp->wa, 0u);
        }
    }

    cmb_assert_debug(cmi_data_is_sorted(dsp->cnt, tsp->ta));
}

/*
 * Takes a copy before sorting, leaving tsp unchanged.
 */
double cmb_timeseries_median(const struct cmb_timeseries *tsp) {
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
            r = dsp->xa[ui] + (dsp->xa[ui + 1] - dsp->xa[ui]) * (wmid - wcum[ui]) / (wcum[ui + 1] - wcum[ui]);
            break;
        }
    }

    cmi_free(wcum);
    cmb_timeseries_clear(&tmp_ts);
    return r;
}

void cmb_timeseries_print_fivenum(const struct cmb_timeseries *tsp, FILE *fp, bool lead_ins) {
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
            x025 = dsp->xa[ui] + (dsp->xa[ui + 1] - dsp->xa[ui]) * (w025 - wcum[ui]) / (wcum[ui + 1] - wcum[ui]);
        }

        if ((wcum[ui] <= w050) && (wcum[ui + 1] > w050)) {
            cmb_assert_debug(wcum[ui + 1] > wcum[ui]);
            x050 = dsp->xa[ui] + (dsp->xa[ui + 1] - dsp->xa[ui]) * (w050 - wcum[ui]) / (wcum[ui + 1] - wcum[ui]);
        }

        if ((wcum[ui] <= w075) && (wcum[ui + 1] > w075)) {
            cmb_assert_debug(wcum[ui + 1] > wcum[ui]);
            x075 = dsp->xa[ui] + (dsp->xa[ui + 1] - dsp->xa[ui]) * (w075 - wcum[ui]) / (wcum[ui + 1] - wcum[ui]);
        }
    }

    cmb_assert_debug((xmin <= x025) && (x025 <= x050) && (x050 <= x075) && (x075 <= xmax));
    const int r = fprintf(fp, "%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g\n",
            ((lead_ins) ? "Min " : ""), xmin,
            ((lead_ins) ? "  Quartile_1 " : "\t"), x025,
            ((lead_ins) ? "  Median " : "\t"), x050,
            ((lead_ins) ? "  Quartile_3 " : "\t"), x075,
            ((lead_ins) ? "  Max " : "\t"), xmax);
    cmb_assert_release(r > 0);

    cmi_free(wcum);
    cmb_timeseries_clear(&tmp_ts);
}
