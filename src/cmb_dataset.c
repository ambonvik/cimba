/*
 * cmb_dataset.c - an automatically resizing array of possibly unordered
 * sample values, each sample a double.
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
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "cmb_dataset.h"
#include "cmb_logger.h"

#include "cmi_dataset.h"
#include "cmi_memutils.h"

struct cmb_dataset *cmb_dataset_create(void)
{
    struct cmb_dataset *dsp = cmi_malloc(sizeof *dsp);
    cmi_memset(dsp, 0, sizeof *dsp);
    dsp->cookie = CMI_UNINITIALIZED;

    /* Add teardown function to the memregistry in case we need to bail out */
    cmi_dlist_initialize(&(dsp->destroy.node));
    dsp->destroy.teardown = (cmi_teardown_func *)cmb_dataset_destroy;
    dsp->destroy.object = dsp;
    cmi_memregistry_add(&(dsp->destroy));

    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);
    return dsp;
}

void cmb_dataset_initialize(struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    /* Might get raw memory with random content, cannot assert _UNINITIALIZED */

    dsp->cookie = CMI_INITIALIZED;
    dsp->cursize = 0u;
    dsp->count = 0u;
    dsp->min = DBL_MAX;
    dsp->max = -DBL_MAX;
    dsp->xa = NULL;

    /* Add teardown function to the memregistry in case we need to bail out */
    cmi_dlist_initialize(&(dsp->terminate.node));
    dsp->terminate.teardown = (cmi_teardown_func *)cmb_dataset_terminate;
    dsp->terminate.object = dsp;
    cmi_memregistry_add(&(dsp->terminate));

    cmb_assert_debug(dsp->cookie == CMI_INITIALIZED);
}

void cmb_dataset_reset(struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    cmb_dataset_terminate(dsp);
    cmb_dataset_initialize(dsp);

    cmb_assert_debug(dsp->cookie == CMI_INITIALIZED);
}

void cmb_dataset_terminate(struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release((dsp->cookie == CMI_INITIALIZED)
                    || cmi_memregistry_is_demolishing);

    if (dsp->cookie == CMI_INITIALIZED) {
        dsp->cookie = CMI_UNINITIALIZED;
        if (dsp->cursize > 0u) {
            cmb_assert_debug(dsp->xa != NULL);
            cmi_free(dsp->xa);
            dsp->xa = NULL;
            dsp->cursize = 0u;
            dsp->count = 0u;
        }
    }

    if (!cmi_memregistry_is_demolishing) {
       cmi_memregistry_remove(&(dsp->terminate));
    }

    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);
}

void cmb_dataset_destroy(struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    /* Call cmb_dataset_terminate first, please */
    cmb_assert_debug(dsp->cookie == CMI_UNINITIALIZED);

    if (!cmi_memregistry_is_demolishing) {
        cmi_memregistry_remove(&(dsp->destroy));
    }

    cmi_free(dsp);
}

void cmi_dataset_expand(struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(dsp->cursize < UINT64_MAX / 2u);

    if (dsp->cursize == 0) {
        cmb_assert_debug(dsp->xa == NULL);
        dsp->cursize = CMI_DATASET_INIT_SZ;
        dsp->xa = cmi_malloc((size_t)(dsp->cursize * sizeof(*(dsp->xa))));
    }
    else {
        cmb_assert_debug(dsp->xa != NULL);
        dsp->cursize *= 2;
        dsp->xa = cmi_realloc(dsp->xa,
                              (size_t)(dsp->cursize * sizeof(*(dsp->xa))));
    }
}

uint64_t cmb_dataset_add(struct cmb_dataset *dsp, const double x)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    dsp->max = (x > dsp->max) ? x : dsp->max;
    dsp->min = (x < dsp->min) ? x : dsp->min;

    if (dsp->count == dsp->cursize) {
        cmi_dataset_expand(dsp);
    }

    /* May have a null data array on entry, but not by here */
    cmb_assert_release(dsp->xa != NULL);
    cmb_assert_release(dsp->count < dsp->cursize);

    dsp->xa[dsp->count] = x;
    dsp->count++;

    return dsp->count;
}

/*
 * Sorting function for the data array, sorting from smallest to largest value.
 *
 * Since stack space might be at a premium in this coroutine-based simulation
 * library, we use a non-recursive heapsort even if the recursive quicksort
 * may be slightly faster. The dataset sorting function will probably not be
 * in any inner loop, and the performance penalty (if any) is a worthwhile
 * tradeoff for the stack space economy.
 */
void cmi_dataset_swap(double *a, double *b)
{
    const double tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Code only used for checking invariants */
bool cmi_dataset_is_sorted(const uint64_t un, const double arr[un])
{
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
bool cmi_dataset_is_max_heap(const uint64_t un,
                             double const arr[un],
                             const uint64_t uroot)
{
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
                    cmi_free(queue);
                    return false;
                }

                queue[utail++] = ucl;
            }

            if (ucr < un) {
                if (arr[ucur] < arr[ucr]) {
                    cmi_free(queue);
                    return false;
                }

                queue[utail++] = ucr;
            }
        }
        cmi_free(queue);
    }

    /* No evidence to the contrary */
    return true;
}


/* Establish max heap condition in the dataset array starting from uroot */
static void dataset_heapify(const uint64_t un,
                            double arr[un],
                            uint64_t uroot)
{
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
            cmi_dataset_swap(&arr[uroot], &arr[ubig]);
            uroot = ubig;
            ucl = 2u * uroot + 1u;
            ucr = 2u * uroot + 2u;
        }
        else {
            /* Heap property is satisfied */
            break;
        }
    }

    cmb_assert_debug(cmi_dataset_is_max_heap(un, arr, uroot));
}

/* Heapsort from smallest to largest value */
void cmb_dataset_sort(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    if (dsp->count == 0u) {
        cmb_logger_warning(stdout, "No data to sort");
    }
    else {
        cmb_assert_debug(dsp->xa != NULL);
        const uint64_t un = dsp->count;
        double *arr = dsp->xa;
        cmb_assert_debug(INT64_MAX >= UINT64_MAX / 2);
        for (int64_t root = (int64_t)(un / 2u) - 1u; root >= 0u; root--) {
            dataset_heapify(un, arr, root);
        }

        for (uint64_t ui = un - 1u; ui > 0u; ui--) {
            cmb_assert_debug(ui < un);
            cmi_dataset_swap(&arr[0], &arr[ui]);
            dataset_heapify(ui, arr, 0u);
        }
    }

    cmb_assert_debug(cmi_dataset_is_sorted(dsp->count, dsp->xa));
}

uint64_t cmb_dataset_copy(struct cmb_dataset *tgt,
                          const struct cmb_dataset *src)
{
    cmb_assert_release(src != NULL);
    cmb_assert_release(src->cookie == CMI_INITIALIZED);
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(tgt != src);

    if (tgt->xa != NULL) {
        cmb_assert_release(tgt->cookie == CMI_INITIALIZED);
        cmi_free(tgt->xa);
        tgt->xa = NULL;
    }

    if (tgt->cookie != CMI_INITIALIZED) {
        /* Will also add it to the memregistry */
        cmb_dataset_initialize(tgt);
    }

    tgt->count = src->count;
    tgt->cursize = src->cursize;
    tgt->min = src->min;
    tgt->max = src->max;

    if (src->xa != NULL) {
        tgt->xa = cmi_calloc(tgt->cursize, sizeof *(tgt->xa));
        cmi_memcpy(tgt->xa, src->xa, tgt->cursize * sizeof *(tgt->xa));
    }

    return tgt->count;
}

/*
 * cmb_dataset_merge - Merge the samples of `s1` and `s2` into `tgt`, keeping
 * s1's samples first and s2's after them.
 *
 * The target is allowed to alias either source, or both. Rather than special
 * casing that, the result is built in a local dataset and swapped into `tgt`
 * at the end: both sources are fully read before anything belonging to `tgt`
 * is released, so every aliasing combination takes the identical path. It also
 * keeps `cmi_memcpy`'s `restrict` contract, which an in-place append could not
 * do for the tgt == s1 == s2 case.
 */
uint64_t cmb_dataset_merge(struct cmb_dataset *tgt,
                           const struct cmb_dataset *s1,
                           const struct cmb_dataset *s2)
{
    cmb_assert_release(tgt != NULL);
    cmb_assert_release(s1 != NULL);
    cmb_assert_release(s1->cookie == CMI_INITIALIZED);
    cmb_assert_release(s2 != NULL);
    cmb_assert_release(s2->cookie == CMI_INITIALIZED);
    cmb_assert_release(s1->count <= (UINT64_MAX - s2->count));

    const uint64_t total = s1->count + s2->count;
    cmb_assert_release(total < (UINT64_MAX - CMI_DATASET_INIT_SZ));
    const uint64_t mult = total / CMI_DATASET_INIT_SZ;
    const uint64_t newsize = (mult + 1u) * CMI_DATASET_INIT_SZ;

    struct cmb_dataset merged = { 0 };
    cmb_dataset_initialize(&merged);

    if (total > 0u) {
        merged.cursize = newsize;
        merged.xa = cmi_malloc((size_t)(merged.cursize * sizeof *(merged.xa)));

        if (s1->count > 0u) {
            cmb_assert_debug(s1->xa != NULL);
            cmi_memcpy(merged.xa, s1->xa,
                       (size_t)(s1->count * sizeof *(merged.xa)));
        }
        if (s2->count > 0u) {
            cmb_assert_debug(s2->xa != NULL);
            cmi_memcpy(&(merged.xa[s1->count]), s2->xa,
                       (size_t)(s2->count * sizeof *(merged.xa)));
        }
        merged.count = total;

        /* An empty dataset carries min = DBL_MAX and max = -DBL_MAX, which are
         * the identities for these reductions, so empty sources need no case. */
        merged.min = (s1->min < s2->min) ? s1->min : s2->min;
        merged.max = (s1->max > s2->max) ? s1->max : s2->max;
    }

    /* Safe only now: both sources have been read out. */
    if (tgt->xa != NULL) {
        cmb_assert_release(tgt->cookie == CMI_INITIALIZED);
        cmi_free(tgt->xa);
        tgt->xa = NULL;
    }

    cmb_dataset_copy(tgt, &merged);
    cmb_dataset_terminate(&merged);

    return tgt->count;
}

/* Overwrites any existing content of data summary */
uint64_t cmb_dataset_summarize(const struct cmb_dataset *dsrc,
                               struct cmb_datasummary *dstgt)
{
    cmb_assert_release(dsrc != NULL);
    cmb_assert_release(dsrc->cookie == CMI_INITIALIZED);
    cmb_assert_release(dstgt != NULL);

    if (dstgt->cookie == CMI_INITIALIZED) {
        cmb_datasummary_terminate(dstgt);
    }
    cmb_datasummary_initialize(dstgt);

    const uint64_t un = dsrc->count;
    if (un == 0u) {
        cmb_logger_warning(stdout, "Cannot summarize empty data set.");
    }
    else {
        for (uint64_t ui = 0; ui < un; ui++) {
            cmb_datasummary_add(dstgt, dsrc->xa[ui]);
        }
     }

    return dstgt->count;
}

/* Assumes that v is already sorted */
static double data_array_median(const uint64_t n, const double v[n])
{
    cmb_assert_release(n > 0u);
    cmb_assert_debug(cmi_dataset_is_sorted(n, v));

    double r;
    if (n % 2u == 0u) {
        r =  (double)(v[n/2u - 1u] + v[n / 2u]) / 2.0;
    }
    else {
        r = (double)(v[n / 2u]);
    }

    return r;
}

double cmb_dataset_median(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    double r = 0.0;
    if (dsp->count == 0u) {
        cmb_logger_warning(stdout, "Cannot take median of empty data set.");
    }
    else {
        cmb_assert_debug(dsp->xa != NULL);
        struct cmb_dataset dtmp = { 0 };
        cmb_dataset_initialize(&dtmp);
        cmb_dataset_copy(&dtmp, dsp);
        cmb_dataset_sort(&dtmp);
        r = data_array_median(dtmp.count, dtmp.xa);
        cmb_dataset_terminate(&dtmp);
    }

    return r;
}

/*
 * Calculate and print five-number summary (quantiles).
 */
void cmb_dataset_fivenum_print(const struct cmb_dataset *dsp,
                               FILE *fp,
                               const bool lead_ins)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(fp != NULL);

    if (dsp->count == 0u) {
        cmb_logger_warning(fp, "No data to display in five-number summary");
    }
    else if (dsp->count < 4u) {
        cmb_logger_warning(fp, "Not enough data for five-number summary, only have %" PRIu64,
            dsp->count);
    }
    else {
        cmb_assert_debug(dsp->xa != NULL);
        struct cmb_dataset dtmp = { 0 };
        cmb_dataset_initialize(&dtmp);
        cmb_dataset_copy(&dtmp, dsp);
        cmb_dataset_sort(&dtmp);

        const double min = dtmp.min;
        const double max = dtmp.max;
        const double med = data_array_median(dtmp.count, dtmp.xa);

        const uint64_t lhsz = dtmp.count / 2;
        const double q1 = data_array_median(lhsz, dtmp.xa);
        double q3;
        const uint64_t uhsz = dtmp.count - lhsz;
        if ((dtmp.count % 2) == 0) {
            /* Even number of entries */
            q3 = data_array_median(uhsz, &(dtmp.xa[lhsz]));
        } else {
            /* Odd number of entries, exclude the median entry */
            q3 = data_array_median(uhsz - 1, &(dtmp.xa[lhsz + 1]));
        }

        const int r = fprintf(fp, "%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g%s%#8.4g\n",
                ((lead_ins) ? "Min " : ""), min,
                ((lead_ins) ? "  First_Q " : "\t"), q1,
                ((lead_ins) ? "  Median " : "\t"), med,
                ((lead_ins) ? "  Third_Q " : "\t"), q3,
                ((lead_ins) ? "  Max " : "\t"), max);
        cmb_assert_release(r > 0);
        cmb_dataset_terminate(&dtmp);
    }
}

void cmb_dataset_print(const struct cmb_dataset *dsp, FILE *fp)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(fp != NULL);

    if (dsp->count == 0u) {
        cmb_logger_warning(fp, "No data to print");
    }
    else {
        cmb_assert_debug(dsp->xa != NULL);
        for (uint64_t l = 0u; l < dsp->count; l++) {
            fprintf(fp, "%g\n", dsp->xa[l]);
        }
    }
}

/*
 * Print a simple character-based histogram of the data.
 * The only external callable functions are cmb_dataset_histogram_print and
 * cmb_timeseries_print_histogram, the rest (cmi_*) are internal helper functions.
 */
static unsigned char symbol_bar = '|';
static unsigned char symbol_full = '#';
static unsigned char symbol_half = '=';
static unsigned char symbol_thin = '-';
static unsigned char symbol_empty = ' ';
static unsigned char symbol_newline = '\n';

static void data_print_blocks(FILE *fp,
                              const double scale,
                              const double binval)
{
    uint64_t nfilled = (uint64_t)(binval / scale);
    const double rem = binval / scale - (double)nfilled;

    while (nfilled-- > 0) {
        const int r = fputc(symbol_full, fp);
        cmb_assert_release(r == symbol_full);
    }

    const double min_rem_half = 0.5;
    if (rem >= min_rem_half) {
        const int r = fputc(symbol_half, fp);
        cmb_assert_release(r == symbol_half);
    }
    else if (rem > 0.0) {
        const int r = fputc(symbol_thin, fp);
        cmb_assert_release(r == symbol_thin);
    }

    const int r = fputc(symbol_newline, fp);
    cmb_assert_release(r == symbol_newline);
}

static void data_print_chars(FILE *fp,
                             const unsigned char c,
                             const uint16_t repeats)
{
    for (uint16_t ui = 0; ui < repeats; ui++) {
        const int r = fputc(c, fp);
        cmb_assert_release(r == c);
    }
}

static void data_print_line(FILE *fp,
                            const unsigned char c,
                            const uint16_t repeats)
{
    data_print_chars(fp, c, repeats);
    const int r = fputc(symbol_newline, fp);
    cmb_assert_release(r == symbol_newline);
}

struct cmi_dataset_histogram *cmi_dataset_histogram_create(const unsigned num_bins,
                                                           const double low_lim,
                                                           const double high_lim)
{
    cmb_assert_debug(num_bins > 0u);
    const double range = high_lim - low_lim;

    struct cmi_dataset_histogram *hp = cmi_malloc(sizeof(*hp));
    /* Allow for overflow bins on either end */
    hp->num_bins = num_bins + 2u;
    hp->binsize = range / (double)(num_bins);
    hp->low_lim = low_lim;
    hp->high_lim = high_lim;
    hp->binmax = 0.0;
    hp->hbins = cmi_calloc(hp->num_bins, sizeof(*(hp->hbins)));

    return hp;
}

/*
 * Calculate the histogram data for a dataset,
 * weighting each x-value equally with 1.0.
 */
void cmi_dataset_histogram_fill(struct cmi_dataset_histogram *hp,
                                const uint64_t n,
                                const double xa[n])
{
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

/*
 * histogram_nice_unit - Largest of 1, 2 or 5 times a power of ten that does
 * not exceed v. Used to snap the label base to a round number.
 */
static double histogram_nice_unit(const double v)
{
    cmb_assert_debug(v > 0.0);

    const double k = floor(log10(v));
    const double mantissa = v / pow(10.0, k);

    double c = 1.0;
    if (mantissa >= 5.0) {
        c = 5.0;
    }
    else if (mantissa >= 2.0) {
        c = 2.0;
    }

    return c * pow(10.0, k);
}

/* histogram_nice_width - Smallest of 1, 2 or 5 times a power of ten that is
 * at least v. Gives round bucket edges at any magnitude. */
static double histogram_nice_width(const double v)
{
    cmb_assert_debug(v > 0.0);

    const double p = pow(10.0, floor(log10(v)));
    const double m = v / p;

    double c = 10.0;
    if (m <= 1.0) {
        c = 1.0;
    }
    else if (m <= 2.0) {
        c = 2.0;
    }
    else if (m <= 5.0) {
        c = 5.0;
    }

    return c * p;
}

/* Autoscale to dataset range */
void cmi_dataset_histogram_autoscale(const struct cmb_dataset *dsp,
                                     unsigned int *num_bins,
                                     double *low_lim, double *high_lim)
{
    double w = 0.0;
    double nb = (double)(*num_bins);
    const double rng = dsp->max - dsp->min;
    if (rng <= 0.0) {
        /* All values identical; any positive width will do */
        w = 1.0;
    }
    else {
        const double raw = rng / nb;
        w = (raw >= 1.0) ? ceil(raw) : histogram_nice_width(raw);
    }

    const double ll = floor(dsp->min / w) * w;
    const double ncand = ceil((dsp->max - ll) / w) + 1.0;
    nb = (unsigned)(fmin(nb, ncand));
    const double hl = ll + nb * w;

    *num_bins = (unsigned int)nb;
    *low_lim = ll;
    *high_lim = hl;
}

void cmi_dataset_histogram_print(const struct cmi_dataset_histogram *hp,
                                 FILE *fp)
{
    cmb_assert_debug(hp != NULL);
    cmb_assert_debug(fp != NULL);

    /* Length of separator lines */
    static const uint16_t line_length = 80u;

    /* Max width of the histogram bars */
    const uint16_t max_stars = 50u;
    const double scale = hp->binmax / (double)max_stars;

    const double edge_mag = fmax(fabs(hp->low_lim), fabs(hp->high_lim));
    const bool use_offset = (hp->binsize < (edge_mag * 1.0e-3));

    double base = 0.0;
    if (use_offset) {
        const double span = (double)(hp->num_bins - 2u) * hp->binsize;
        const double unit = histogram_nice_unit(span / 10.0);
        base = floor(hp->low_lim / unit) * unit;
    }

    const double origin = hp->low_lim - base;
    data_print_line(fp, symbol_thin, line_length);

    int r;
    if (use_offset) {
        r = fprintf(fp, "Bucket edges shown as offsets from base %.15g, bucket width %#.4g\n",
                    base, hp->binsize);
        cmb_assert_release(r > 0);
        data_print_line(fp, symbol_thin, line_length);
    }

    r = fprintf(fp, "( -Infinity, %#10.4g)   |", origin);
    cmb_assert_release(r > 0);
    data_print_blocks(fp, scale, hp->hbins[0u]);

    for (unsigned ui = 1u; ui < hp->num_bins - 1u; ui++) {
        r = fprintf(fp, "[%#10.4g, %#10.4g)   |",
                    origin + (double)(ui - 1u) * hp->binsize,
                    origin + (double)ui * hp->binsize);
        cmb_assert_release(r > 0);
        data_print_blocks(fp, scale, hp->hbins[ui]);
    }

    r = fprintf(fp, "[%#10.4g,   Infinity)   |",
                origin + (double)(hp->num_bins - 2u) * hp->binsize);
    cmb_assert_release(r > 0);
    data_print_blocks(fp, scale, hp->hbins[hp->num_bins - 1u]);

    data_print_line(fp, symbol_thin, line_length);
}

void cmi_dataset_histogram_destroy(struct cmi_dataset_histogram *hp)
{
    cmb_assert_release(hp != NULL);
    cmi_free(hp->hbins);
    cmi_free(hp);
}

/* The external callable function to print a histogram */
void cmb_dataset_histogram_print(const struct cmb_dataset *dsp,
                                 FILE *fp,
                                 unsigned int num_bins,
                                 double low_lim,
                                 double high_lim)
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);

    cmb_assert_release(num_bins > 0u);
    cmb_assert_release(high_lim >= low_lim);

    if (dsp->xa == NULL) {
         cmb_assert_debug(dsp->count == 0u);
         cmb_logger_warning(fp, "No data to display in histogram");
         return;
    }

    if (low_lim == high_lim) {
        cmi_dataset_histogram_autoscale(dsp, &num_bins, &low_lim, &high_lim);
    }

    struct cmi_dataset_histogram *hp = NULL;
    hp = cmi_dataset_histogram_create(num_bins, low_lim, high_lim);
    cmi_dataset_histogram_fill(hp, dsp->count, dsp->xa);
    cmi_dataset_histogram_print(hp, fp);

    cmi_dataset_histogram_destroy(hp);
}

/*
 * Calculate the first n autocorrelation coefficients.
 */
void cmb_dataset_ACF(const struct cmb_dataset *dsp,
                     const unsigned int n,
                     double acf[])
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(n > 0u);
    cmb_assert_release(acf != NULL);

    if (dsp->count == 0u) {
        cmb_logger_warning(stdout, "No data for calculating ACF");
        return;
    }
    else if (dsp->count == 1u) {
        cmb_logger_warning(stdout, "Cannot calculate ACF of a single point");
        return;
    }
    else if (dsp->count < n + 1) {
        cmb_logger_warning(stdout, "Cannot calculate %u ACFs from %" PRIu64 " data points",
                            n, dsp->count);
        return;
    }

    /* Calculate mean and variance in a single pass,
     * similar to cmb_datasummary() above.  */
    double m1 = 0.0;
    double m2 = 0.0;
    cmb_assert_debug(dsp->xa != NULL);
    for (uint64_t ui = 0; ui < dsp->count; ui++) {
        const double d = dsp->xa[ui] - m1;
        const double d_n = d / ((double)(ui + 1u));
        m1 += d_n;
        m2 += d * (d - d_n);
    }
    const double var = m2 / ((double)(dsp->count - 1u));

    acf[0] = 1.0;
    const double min_acf_variance = 1e-9;
    if (var < min_acf_variance) {
        /* Would be numerically unstable to divide by that */
        cmb_logger_warning(stdout,
                "Dataset nearly constant (variance %g), ACFs rounded to zero",
                 var);
        for (unsigned ui = 1; ui <= n; ui++) {
            acf[ui] = 0.0;
        }
    }
    else {
        for (unsigned ulag = 1; ulag <= n; ulag++) {
            double dk = 0.0;
            const uint64_t ustop = dsp->count - ulag;
            for (uint64_t ui = 0; ui < ustop; ui++) {
                dk += (dsp->xa[ui] - m1) * (dsp->xa[ui + ulag] - m1);
            }
            acf[ulag] = dk / m2;
            cmb_assert_debug((acf[ulag] >= -1.0) && (acf[ulag] <= 1.0));
        }
    }
}

/*
 * Calculate the first n partial autocorrelation coefficients using the
 * Durbin-Levinson algorithm, optionally using previously calculated ACFs
 * to avoid repeating a computationally expensive step if already done once.
 */
static const double pacf_min_variance = 1.0e-12;
static const double pacf_tolerance = 1.0e-9;

void cmb_dataset_PACF(const struct cmb_dataset *dsp,
                      const unsigned int n,
                      double pacf[],
                      double acf[])
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(n > 0u);
    cmb_assert_release(acf != NULL);

    if (dsp->count == 0u) {
        cmb_logger_warning(stdout, "No data for calculating PACF");
        return;
    }
    else if (dsp->count == 1u) {
        cmb_logger_warning(stdout, "Cannot calculate PACF of a single point");
        return;
    }
    else if (dsp->count < n + 1) {
        cmb_logger_warning(stdout, "Cannot calculate %u PACFs from %" PRIu64 " data points",
                            n, dsp->count);
        return;
    }

    bool free_acf = false;
    if (acf == NULL) {
        acf = cmi_malloc((n + 1u) * sizeof(double));
        free_acf = true;
        cmb_dataset_ACF(dsp, n, acf);
    }

    /* Create an intermediary un * un matrix */
    double **phi = cmi_malloc(((size_t)n + 1u) * sizeof(double *));
    phi[0] = cmi_calloc(((size_t)n + 1u) * ((size_t)n + 1u), sizeof(double));
    for (unsigned ui = 1u; ui <= n; ui++) {
        phi[ui] = phi[0] + ui * (n + 1u);
    }

    pacf[0] = 1.0;
    pacf[1] = acf[1];
    phi[1][1] = pacf[1];
    double v = 1.0 - acf[1] * acf[1];

    for (unsigned uk = 2u; uk <= n; ++uk) {
        if (v <= pacf_min_variance) {
            cmb_logger_warning(NULL,
                "PACF: prediction variance exhausted at lag %u, "
                "remaining coefficients set to zero", uk - 1u);
            for (unsigned uj = uk; uj <= n; ++uj) {
                pacf[uj] = 0.0;
            }
            break;
        }

        double numsum = 0.0;
        for (unsigned uj = 1u; uj < uk; ++uj) {
            numsum += phi[uk - 1u][uj] * acf[uk - uj];
        }

        double p = (acf[uk] - numsum) / v;

        cmb_assert_debug((p >= -1.0 - pacf_tolerance)
                         && (p <= 1.0 + pacf_tolerance));
        p = fmin(1.0, fmax(-1.0, p));

        phi[uk][uk] = p;
        pacf[uk] = p;

        for (unsigned uj = 1u; uj < uk; ++uj) {
            phi[uk][uj] = phi[uk - 1u][uj] - p * phi[uk - 1u][uk - uj];
        }

        v *= (1.0 - p * p);
    }

    /* Calculate phi[k][j], the j-th coefficient for a k-th order
     * autoregression model     */
    for (unsigned uk = 2u; uk <= n; ++uk) {
        double numsum = 0.0;
        for (unsigned uj = 1u; uj < uk; ++uj) {
            numsum += phi[uk - 1][uj] * acf[uk - uj];
        }

        double densum = 0.0;
        for (unsigned uj = 1u; uj < uk; ++uj) {
            densum += phi[uk - 1][uj] * acf[uj];
        }

        /* The k-th PACF coefficient is the k-th autoregression
         * coefficient phi[k][k]    */
        cmb_assert_debug(densum < 1.0);
        phi[uk][uk] = (acf[uk] - numsum) / (1.0 - densum);
        pacf[uk] = phi[uk][uk];
        cmb_assert_debug((pacf[uk] >= -1.0) && (pacf[uk] <= 1.0));

        /* Update everything else for the next iteration */
        for (unsigned uj = 1u; uj < uk; ++uj) {
            phi[uk][uj] = phi[uk - 1u][uj]
                          - phi[uk][uk] * phi[uk - 1u][uk - uj];
        }
    }

    cmi_free(phi[0]);
    cmi_free(phi);
    if (free_acf) {
        cmi_free(acf);
    }
}

static void data_bar_print(FILE *fp,
                           const double acfval,
                           const uint16_t max_bar_width)
{
    cmb_assert_debug((acfval >= -1.0) && (acfval <= 1.0));

    const double bar_width = (double)max_bar_width * fabs(acfval);
    const uint16_t num_filled = (uint16_t)floor(bar_width);
    cmb_assert_debug(num_filled <= max_bar_width);
    const double rem = bar_width - num_filled;
    cmb_assert_debug((rem >= 0.0) && (rem < 1.0));

    /* Smallest remainder to qualify for a plus sign */
    const double min_rem_plus = 0.5;

    if (acfval < 0.0) {
        const uint16_t num_spaces = max_bar_width - num_filled - 1;
        data_print_chars(fp, symbol_empty, num_spaces);

        if (rem > min_rem_plus) {
            const int r = fputc(symbol_half, fp);
            cmb_assert_debug(r == symbol_half);
        }
        else if (rem > 0.0) {
            const int r = fputc(symbol_thin, fp);
            cmb_assert_debug(r == symbol_thin);
        }
        else {
            const int r = fputc(symbol_empty, fp);
            cmb_assert_debug(r == symbol_empty);
        }

        data_print_chars(fp, symbol_full, num_filled);
        const int r = fputc(symbol_bar, fp);
        cmb_assert_debug(r == symbol_bar);
    }
    else {
        const uint16_t num_spaces = max_bar_width;
        data_print_chars(fp, symbol_empty, num_spaces);
        int r = fputc(symbol_bar, fp);
        cmb_assert_debug(r == symbol_bar);
        data_print_chars(fp, symbol_full, num_filled);

        if (rem > min_rem_plus) {
            r = fputc(symbol_half, fp);
            cmb_assert_debug(r == symbol_half);
        }
        else if (rem > 0.0) {
            r = fputc(symbol_thin, fp);
            cmb_assert_debug(r == symbol_thin);
        }
    }
}

void cmb_dataset_correlogram_print(const struct cmb_dataset *dsp,
                                   FILE *fp,
                                   const unsigned n,
                                   double acf[])
{
    cmb_assert_release(dsp != NULL);
    cmb_assert_release(dsp->cookie == CMI_INITIALIZED);
    cmb_assert_release(fp != NULL);
    cmb_assert_release(n > 0u);

    if (dsp->count == 0u) {
        cmb_logger_warning(fp, "No data for correlogram");
        return;
    }
    else if (dsp->count == 1u) {
        cmb_logger_warning(fp, "Cannot plot correlogram of a single point");
        return;
    }
    else if (dsp->count < n + 1) {
        cmb_logger_warning(fp, "Cannot plot %u ACFs from %" PRIu64 " data points",
                            n, dsp->count);
        return;
    }

    bool free_acf = false;
    if (acf == NULL) {
        acf = cmi_malloc((n + 1u) * sizeof(double));
        free_acf = true;
        cmb_dataset_ACF(dsp, n, acf);
    }

    /* Length of separator lines / axes */
    static const uint16_t line_length = 80u;

    /* Max width of the bar either side of zero */
    const uint16_t max_bar_width = (line_length - 14u) / 2u;

    data_print_chars(fp, symbol_empty, 11u);
    int r = fprintf(fp, "-1.0");
    cmb_assert_release(r > 0);
    data_print_chars(fp, symbol_empty, max_bar_width - 3u);
    r = fprintf(fp, "0.0");
    cmb_assert_release(r > 0);
    data_print_chars(fp, symbol_empty, max_bar_width - 3u);
    r = fprintf(fp, "1.0\n");
    cmb_assert_release(r > 0);

    data_print_line(fp, symbol_thin, line_length);
    for (unsigned ui = 1u; ui <= n; ui++) {
        r = fprintf(fp, "%4u  %#6.3f ", ui, acf[ui]);
        cmb_assert_release(r > 0);
        data_bar_print(fp, acf[ui], max_bar_width);
        r = fprintf(fp, "\n");
        cmb_assert_release(r > 0);
    }

    data_print_line(fp, symbol_thin, line_length);
    if (free_acf) {
        cmi_free(acf);
    }
}
