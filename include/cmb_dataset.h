/**
 * @file cmb_dataset.h
 * @brief An automatically resizing array of (possibly unordered) sample values,
 *        each sample a double.
 *
 * The internal data array will be created on the heap even if the data series
 * is declared as a local variable on the stack.
 */

/*
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
#include "cmb_datasummary.h"

/**
 * @brief A data set with a conveniently resizing sample array.
 */
struct cmb_dataset {
    uint64_t cookie;    /**< A "magic cookie" to catch uninitialized objects */
    uint64_t cursize;   /**< The currently allocated space as number of samples */
    uint64_t count;     /**< The current number of samples in array */
    double min;         /**< Smallest sample, initially `DBL_MAX` */
    double max;         /**< Largest sample, initially `-DBL_MAX` */
    double *xa;         /**< Pointer to the actual data array, initially `NULL` */
};

/**
 * @brief Allocate memory for a dataset.
 *
 * Remember to call a matching `cmb_dataset_destroy` when done to avoid memory
 * leakage.
 *
 * @return A freshly allocated dataset object.
 */
extern struct cmb_dataset *cmb_dataset_create(void);

/**
 * @brief Initialize the dataset, clearing any data values.
 * @param dsp Pointer to an already allocated dataset object.
 */
extern void cmb_dataset_initialize(struct cmb_dataset *dsp);

/**
 * @brief Re-initialize it, returning it to newly initialized state.
 * @param dsp Pointer to an already allocated dataset object.
 */
extern void cmb_dataset_reset(struct cmb_dataset *dsp);

/**
 * @brief Un-initialize it, returning it to newly created state.
 * @param dsp Pointer to an already allocated dataset object.
 */
extern void cmb_dataset_terminate(struct cmb_dataset *dsp);

/**
 * @brief Copy `tgt` into `src`, overwriting whatever was in `tgt`.
 * @param tgt Pointer to the target dataset object.
 * @param src Pointer to the source dataset object.
 * @return Number of data points copied.
 */
extern uint64_t cmb_dataset_copy(struct cmb_dataset *tgt,
                                 const struct cmb_dataset *src);

/**
 * @brief  Merge `datasets `s1` and `s2` into dataset `tgt.
 *         The target may or may not be one of the two sources, but not NULL.
 * @param tgt Pointer to the target dataset object.
 * @param s1 Pointer to the first source dataset object.
 * @param s2 Pointer to the second source dataset object.
 * @return Number of data points in the merged data set.
 */
extern uint64_t cmb_dataset_merge(struct cmb_dataset *tgt,
                                  const struct cmb_dataset *s1,
                                  const struct cmb_dataset *s2);

/**
 * @brief Free memory allocated by `cmb_dataset_create` for the dataset and its
 *        arrays.
 *
 * Do not call unless the dataset was created on the heap by
 * `cmb_dataset_create`. Use `cmb_dataset_terminate` instead to free the data
 * array only if not.
 *
 * @param dsp Pointer to previously allocated dataset object.
 */
extern void cmb_dataset_destroy(struct cmb_dataset *dsp);

/**
 * @brief  Sort the data array in ascending order
 * @param dsp Pointer to a dataset object.
 */
extern void cmb_dataset_sort(const struct cmb_dataset *dsp);

/**
 * @brief  Add a single value to a dataset, resizing the array as needed.
 * @param dsp Pointer to a dataset object.
 * @param x The new sample value to add.
 * @return The new number of data values in the array.
 */
extern uint64_t cmb_dataset_add(struct cmb_dataset *dsp, double x);

/**
 * @brief  Calculate summary statistics of the data series
 * @param dsp Pointer to a dataset object.
 * @param dsump Pointer to a data summary object to store the results.
 * @return The number of data values included in the summary.
 */
extern uint64_t cmb_dataset_summarize(const struct cmb_dataset *dsp,
                                      struct cmb_datasummary *dsump);

/**
 * @brief Count the number of data values.
 * @param dsp Pointer to a dataset object.
 * @return The number of data values in the data set.
 */
static inline uint64_t cmb_dataset_count(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->count;
}

/**
 * @brief The minimum sample value in the dataset.
 * @param dsp Pointer to a dataset object.
 * @return The minimum data value in the data set, `DBL_MAX` if no data yet.
 */
static inline double cmb_dataset_min(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->min;
}

/**
 * @brief The maximum sample value in the dataset.
 * @param dsp Pointer to a dataset object.
 * @return The maximum data value in the data set, `-DBL_MAX` if no data yet.
 */
static inline double cmb_dataset_max(const struct cmb_dataset *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->max;
}

/**
 * @brief Calculate and return the median of the dataset.
 *
 * May be somewhat time-consuming, since it first needs to sort the data array.
 * Calling it on an empty dataset will generate a warning and return zero.
 *
 * @param dsp Pointer to a dataset object.
 * @return The maximum data value in the data set, zero if no data yet.
 */
extern double cmb_dataset_median(const struct cmb_dataset *dsp);

/**
 * @brief Calculate and print the "five-number" summary of dataset quantiles,
 *        i.e., minimum, first quartile, median, third quartile, and maximum.
 *
 * @param dsp Pointer to a dataset object.
 * @param fp A valid file pointer, possibly `stdout`
 * @param lead_ins Flag for whether to add lead-in texts or just print the
 *                 numeric values.
 */
extern void cmb_dataset_print_fivenum(const struct cmb_dataset *dsp,
                                      FILE *fp,
                                      bool lead_ins);

/**
 * @brief Print a simple character-based histogram. Will autoscale to the
 * dataset range if `LowerLimit == UpperLimit`.
 *
 *  Will print symbol '#' for a full bar "pixel", '=' for one that is more
 *  than half full, and '-' for one that is less than half full.
*
 *  Adds overflow bins to the ends of the range to catch anything outside.
 *
 * @param dsp Pointer to a dataset object.
 * @param fp A valid file pointer, possibly `stdout`
 * @param num_bins The number of bins, not including the two overflow bins
 * @param low_lim The lower limit for the bin range.
 * @param high_lim The upper limit for the bin range.
 */
extern void cmb_dataset_print_histogram(const struct cmb_dataset *dsp,
                                        FILE *fp,
                                        unsigned num_bins,
                                        double low_lim,
                                        double high_lim);

/**
 * @brief Print the raw data values in a single column.
 *
 * @param dsp Pointer to a dataset object.
 * @param fp A valid file pointer, possibly `stdout`
 */
extern void cmb_dataset_print(const struct cmb_dataset *dsp, FILE *fp);

/**
 * @brief Calculate autocorrelation coefficients.
 *
 * @param dsp Pointer to a dataset object.
 * @param max_lag The highest lag value to calculate.
 * @param acf The array where the acf's will be stored.
 */
extern void cmb_dataset_ACF(const struct cmb_dataset *dsp,
                            unsigned max_lag,
                            double acf[max_lag + 1u]);

/**
 * @brief Calculate partial autocorrelation coefficients.
 *
 * The first and most time-consuming step in the algorithm is to calculate the
 * ACFs. If these already have been calculated, they can be given as the last
 * argument `acf[]`. If this argument is `NULL`, they will be calculated
 * directly from the dataset during the call.
 *
 * @param dsp Pointer to a dataset object.
 * @param max_lag The highest lag value to calculate.
 * @param pacf The array where the pacf's will be stored.
 * @param acf Array of ACF's if already calculated, otherwise `NULL`.
 */
extern void cmb_dataset_PACF(const struct cmb_dataset *dsp,
                             unsigned max_lag,
                             double pacf[max_lag + 1u],
                             double acf[max_lag + 1u]);

/**
 * @brief Print a simple correlogram of the autocorrelation coefficients
 * previously calculated, either ACFs or PACFs.
 *
 * If the data vector `acf[]` is `NULL`, ACFs will be calculated directly from
 * the dataset by calling `cmb_dataset_ACF.
 *
 * To print PACFs, give a vector of PACFs as the `acf` argument.
 *
 * @param dsp Pointer to a dataset object.
 * @param fp A valid file pointer, possibly `stdout`
 * @param max_lag The highest lag value to calculate.
 * @param acf The array where the acf's will be stored.
 */
extern void cmb_dataset_print_correlogram(const struct cmb_dataset *dsp,
                                          FILE *fp,
                                          unsigned max_lag,
                                          double acf[max_lag + 1u]);


#endif /* CIMBA_CMB_DATASET_H */