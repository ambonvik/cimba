/*
 * cmb_dataset.c - an automatically resizing array of possibly unordered
 * sample values, each sample a double.
 *
 * It does not keep a running tally, use cmb_dataset_summarize() to compute
 * the statistics when needed. The data array is allocated from the heap as
 * needed and free'd by either cmb_dataset_reset or cmb_dataset_destroy.
 * The internal data array will be created on the heap even if the data series
 * is declared as a local variable (on the stack).
 *
 * The cmb_dataset keeps its own cache of the largest and smallest members,
 * for use in histograms without requiring a complete statistics calculation.
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

#ifndef CIMBA_CMB_DATASET_H
#define CIMBA_CMB_DATASET_H

#include "cmb_assert.h"
#include "cmb_dataset.h"
#include "cmb_datasummary.h"

struct cmb_dataset {
    uint64_t cursize;
    uint64_t cnt;
    double min;
    double max;
    double *xa;
};

/*
 * Allocate memory for dataset.
 * Remember to call a matching cmb_dataset_destroy to avoid memory leakage.
 */
extern struct cmb_dataset *cmb_dataset_create(void);

/* Initialize the dataset, clearing any data values. */
extern void cmb_dataset_initialize(struct cmb_dataset *dsp);

extern void cmb_dataset_reset(struct cmb_dataset *dsp);

/* Uni-initialize it, returning it to newly created state */
extern void cmb_dataset_terminate(struct cmb_dataset *tgt);

/* Copy tgt into src, overwriting whatever was in tgt */
extern uint64_t cmb_dataset_copy(struct cmb_dataset *tgt,
                                 const struct cmb_dataset *src);

/*
 * Merge s1 and s2 into tgt.
 * tgt may or may not be one of the two sources, but not NULL.
 */
extern uint64_t cmb_dataset_merge(struct cmb_dataset *tgt,
                                  const struct cmb_dataset *s1,
                                  const struct cmb_dataset *s2);

/*
 * Free memory allocated by cmb_dataset_create for the dataset and its arrays.
 * Do not call for a dataset that was just declared on the stack without no
 * cmb_dataset_create. Use cmb_dataset_reset instead to free the arrays only.
 */
extern void cmb_dataset_destroy(struct cmb_dataset *dsp);

/* Sort v[] in ascending order */
extern void cmb_dataset_sort(const struct cmb_dataset *dsp);

/*
 * Add a single value to a dataset, resizing the array as needed.
 * Returns the new number of data values in the array.
 */
extern uint64_t cmb_dataset_add(struct cmb_dataset *dsp, double x);

/*
 * Calculate summary statistics of the data series
 * Returns the number of data points in the summary.
 */
extern uint64_t cmb_dataset_summarize(const struct cmb_dataset *dsp,
                                      struct cmb_datasummary *dsump);

static inline uint64_t cmb_dataset_count(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    return dsp->cnt;
}

static inline double cmb_dataset_min(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    return dsp->min;
}

static inline double cmb_dataset_max(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);
    return dsp->max;
}

/* Calculate and return the median of the data series */
extern double cmb_dataset_median(const struct cmb_dataset *dsp);

/* Calculate and print the "five-number" summary of dataset quantiles */
extern void cmb_dataset_print_fivenum(const struct cmb_dataset *dsp,
                                      FILE *fp,
                                      bool lead_ins);

/*
 *  Print a simple character-based histogram.
 *
 *  Will autoscale to the dataset range if LowerLimit == UpperLimit.
 *
 *  Adds overflow bins to the ends of the range to catch anything outside.
 *  
 *  Will print symbol '*' for a full "pixel",  *  '+' for one that is more
 *  than half full, and '-' for one that is less than half full.
 */
extern void cmb_dataset_print_histogram(const struct cmb_dataset *dsp,
                                        FILE *fp,
                                        unsigned num_bins,
                                        double low_lim,
                                        double high_lim);

/* Print the raw data in the dataset. */
extern void cmb_dataset_print(const struct cmb_dataset *dsp, FILE *fp);

/* Calculate the n first autocorrelation coefficients. */
extern void cmb_dataset_ACF(const struct cmb_dataset *dsp,
                            unsigned max_lag,
                            double acf[max_lag + 1u]);

/*
 * Calculate the n first partial autocorrelation coefficients.
 *
 * The first step in the algorithm is to calculate the ACFs. If these already
 * have been calculated, they can be given as the last argument acf[].
 * If this argument is NULL, they will be calculated directly from the dataset.
 */
extern void cmb_dataset_PACF(const struct cmb_dataset *dsp,
                             unsigned max_lag,
                             double pacf[max_lag + 1u],
                             double acf[max_lag + 1u]);

/*
 * Print a simple correlogram of the autocorrelation coefficients previously
 * calculated, either ACFs or PACFs.
 *
 * If the data vector acf[] is NULL, ACFs will be calculated directly from the
 * dataset by calling cmb_dataset_ACF.
 *
 * To print PACFs, simply give a vector of PACFs as the acf argument.
 */
extern void cmb_dataset_print_correlogram(const struct cmb_dataset *dsp,
                                          FILE *fp,
                                          unsigned max_lag,
                                          double acf[max_lag + 1u]);


#endif /* CIMBA_CMB_DATASET_H */