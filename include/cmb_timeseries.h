/**
 * @file cmb_timeseries.h
 * @brief An automatically resizing array of ordered sample values,
 *        each sample a (x, t) tuple.
 *
 * The use case is that states change only at the discrete event times in a
 * discrete event simulation. Between events, everything is constant. If sample
 * values only are recorded at event times, the statistics may be biased.
 * For example, collecting length data of a queue that is mostly empty with
 * long time intervals of zero length. Storing data with time stamps allows
 * correct weighting. Use `cmb_timeseries_summarize()` to compute the statistics
 * into a weighted data summary when needed.
 *
 * The data array is allocated from the heap as needed. The internal data array
 * will be created on the heap even if the data series is declared as a local
 * variable (on the stack).
 */

/*
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

#ifndef CIMBA_CMB_TIMESERIES_H
#define CIMBA_CMB_TIMESERIES_H

#include "cmb_dataset.h"
#include "cmb_wtdsummary.h"

/**
 * @brief A time series with a conveniently resizing sample array. The parent
 *        class `cmb_dataset` provides the `xa` array.
 */
struct cmb_timeseries {
    struct cmb_dataset ds;  /**< Parent class providing the `xa` array */
    double *ta;             /**< The array of time stamps for the samples */
    double *wa;             /**< The weighting (duration) of each sample value */
};

/**
 * @brief Allocate memory for a time series.
 *
 * Remember to call a matching `cmb_timeseries_destroy` when done to avoid
 * memory leakage.
 *
 * @return A freshly allocated time series object.
 */
extern struct cmb_timeseries *cmb_timeseries_create(void);

/**
 * @brief Initialize the time series, clearing any data values.
 * @param tsp Pointer to an already allocated time series object.
 */
extern void cmb_timeseries_initialize(struct cmb_timeseries *tsp);

/**
 * @brief Re-initialize it, returning it to a newly initialized state.
 * @param tsp Pointer to an already allocated time series object.
 */
extern void cmb_timeseries_reset(struct cmb_timeseries *tsp);

/**
 * @brief Un-initialize it, returning it to a newly created state.
 * @param tsp Pointer to an already allocated time series object.
 */
extern void cmb_timeseries_terminate(struct cmb_timeseries *tsp);

/**
 * @brief  Free memory allocated by `cmb_timeseries_create` for the time series
 *         and its arrays.
 *
 * Do not call unless the time series was created on the heap by
 * `cmb_timeseries_create`. Otherwise, use `cmb_timeseries_terminate` to free
 * the data array only.
 *
 * @param tsp Pointer to a previously allocated time series object.
 */
extern void cmb_timeseries_destroy(struct cmb_timeseries *tsp);


/**
 * @brief Copy `tgt` into `src`, overwriting whatever was in `tgt`.
 *
 * @param tgt Pointer to the target time series object.
 * @param src Pointer to the source time series object.
 *
 * @return Number of data points copied.
 */
extern uint64_t cmb_timeseries_copy(struct cmb_timeseries *tgt,
                                    const struct cmb_timeseries *src);

/**
 * @brief  Add a single value to a time series, resizing the array as needed.
 *
 * @param tsp Pointer to a time series object.
 * @param x The new sample x-value to add.
 * @param t The t-value (timestamp) for the new sample.
 *
 * @return The new number of data values in the array.
 */
extern uint64_t cmb_timeseries_add(struct cmb_timeseries *tsp,
                                   double x,
                                   double t);

/**
 * @brief  Add a final data point at the given time t with the same x-value as
 *         the last recorded value. Used to ensure that the last value gets
 *         weighted by its correct duration from the event time to the end of
 *         the data collection period.
 *
 * Typically called as `cmb_timeseries_finalize(tsp, cmb_time())` during the
 * simulation closing ceremonies. The closing time is left as an explicit
 * argument for user flexibility in any other use cases and for better
 * separation between Cimba modules.
 *
 * @param tsp Pointer to a time series object.
 * @param t The t-value (timestamp) for the final value, usually `cmb_time()`.
 */
extern uint64_t cmb_timeseries_finalize(struct cmb_timeseries *tsp, double t);

/**
 * @brief  Sort timeseries in ascending order by x-value. The time stamps and
 *         weights in the `ta` and `wa` arrays follow the `xa` values.
 *
 * Caution: Changes the sequence of data points, no longer a timeseries after
 * this call.
 *
 * @param tsp Pointer to a time series object.
 */
extern void cmb_timeseries_sort_x(struct cmb_timeseries *tsp);

/**
 * @brief  An "undo" function for `cmb_timeseries_sort_x()`, sorting the time
 *         series back to an ascending time sequence.
 *
 * @param tsp Pointer to a time series object.
 */
extern void cmb_timeseries_sort_t(struct cmb_timeseries *tsp);

/**
 * @brief  Calculate summary statistics of the time series.
 *
 * @param tsp Pointer to a time series object.
 * @param wsp Pointer to a weighted data summary object.

 * @return The number of data points in the summary.
 */
extern uint64_t cmb_timeseries_summarize(const struct cmb_timeseries *tsp,
                                         struct cmb_wtdsummary *wsp);

/**
 * @brief  Count the number of samples in the time series.
 *
 * @param tsp Pointer to a time series object.
 *
 * @return The number of samples in the time series.
 */
static inline uint64_t cmb_timeseries_count(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    return cmb_dataset_count((struct cmb_dataset *)tsp);
}

/**
 * @brief  Find the smallest sample (x) value in the time series.
 *
 * @param tsp Pointer to a time series object.
 *
 * @return The smallest sample (x) value in the time series.
 */
static inline double cmb_timeseries_min(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    return cmb_dataset_min((struct cmb_dataset *)tsp);
}

/**
 * @brief  Find the largest sample (x) value in the time series.
 *
 * @param tsp Pointer to a time series object.
 *
 * @return The largest sample (x) value in the time series.
 */
static inline double cmb_timeseries_max(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    return cmb_dataset_max((struct cmb_dataset *)tsp);
}

/**
 * @brief  Calculate and return the median of the time series, sample values
 *         weighted by duration. Uses linear interpolation for the median value
 *         at 50 % of the summed weights.
 *
 * Call `cmb_dataset_median((struct cmb_dataset *)tsp, ...)` for unweighted.
 *
 * @param tsp Pointer to a time series object.
 *
 * @return The median value in the time series.
 */
extern double cmb_timeseries_median(const struct cmb_timeseries *tsp);

/**
 * @brief Calculate and print the "five-number" summary of timeseries quantiles,
 *        weighted by duration (the holding time from one sample to the next).
 *
 * Call `cmb_dataset_print_fivenum((struct cmb_dataset *)tsp, ...)` to get
 * unweighted quantiles.
 *
 * @param tsp Pointer to a time series object.
 * @param fp A file pointer for the output, possibly `stdout`.
 * @param lead_ins A flag indicating whether to add explanatory text (if `true`)
 *                 or not (if `false`).
*/
extern void cmb_timeseries_print_fivenum(const struct cmb_timeseries *tsp,
                                         FILE *fp,
                                         bool lead_ins);

/**
 * @brief Print a simple character-based histogram. Like
 *        `cmb_dataset_print_histogram()` but weighted by the time interval
 *        until the next sample.
 *
 *  Call `cmb_dataset_histogram((struct cmb_dataset *)tsp, ...)` for unweighted.
 *
 * @param tsp Pointer to a time series object.
 * @param fp A file pointer for the output, possibly `stdout`.
 * @param num_bins Number of histogram bins to use. Will add one more on either
 *                 side to catch overflow values.
 * @param low_lim The lower limit of the histogram bin values.
 * @param high_lim The upper limit of the histogram bin values. Will autoscale
 *                 to actual sample value range if `low_lim == high_lim`.
 */
extern void cmb_timeseries_print_histogram(const struct cmb_timeseries *tsp,
                                           FILE *fp,
                                           uint16_t num_bins,
                                           double low_lim,
                                           double high_lim);

/**
 * @brief Print the raw data in the timeseries.
 *
 * @param tsp Pointer to a time series object.
 * @param fp A file pointer for the output, possibly `stdout`.
 */
extern void cmb_timeseries_print(const struct cmb_timeseries *tsp, FILE *fp);

/**
 * @brief Calculate the `n` first autocorrelation coefficients between
 *        individual samples, only considering the sequence, disregarding the
 *        time duration between samples.
 *
 * @param tsp Pointer to a time series object.
 * @param n The number of coefficients to calculate.
 * @param acf The array to store the autocorrelation coefficients, size ``n + 1``
 */
static inline void cmb_timeseries_ACF(const struct cmb_timeseries *tsp,
                                      const uint16_t n,
                                      double *acf)
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_ACF((struct cmb_dataset *)tsp, n, acf);
}

/**
 * @brief Calculate the `n` first partial autocorrelation coefficients, again
 *        only considering sequence, not interval durations.
 *
 * The first and most time-consuming step in the algorithm is to calculate the
 * ACFs. If these already have been calculated, they can be given as the last
 * argument `acf[]`. If this argument is `NULL`, they will be calculated
 * directly from the dataset.
 *
 * @param tsp Pointer to a time series object.
 * @param n The number of coefficients to calculate.
 * @param pacf The array to store the partial autocorrelation coefficients, size ``n + 1``.
 * @param acf Array of autocorrelation coefficients, if already available, size ``n + 1``.
 */
static inline void cmb_timeseries_PACF(const struct cmb_timeseries *tsp,
                                       const uint16_t n,
                                       double *pacf,
                                       double *acf)
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_PACF((struct cmb_dataset *)tsp, n, pacf, acf);
}

/**
 * @brief Print a simple correlogram of the autocorrelation coefficients
 *        previously calculated, either ACFs or PACFs.
 *
 * If the data vector `acf[]` is `NULL`, ACFs will be calculated directly from
 * the dataset by calling `cmb_dataset_ACF()`.
 *
 * To print PACFs, give a vector of already calculated PACFs as the `acf`
 * argument.
 *
 * @param tsp Pointer to a time series object.
 * @param fp A file pointer for the output, possibly `stdout`.
 * @param n The number of coefficients to calculate.
 * @param acf Array of (partial) autocorrelation coefficients, if already
 *            available, size ``n + 1``.
 */
static inline void cmb_timeseries_print_correlogram(const struct cmb_timeseries *tsp,
                                             FILE *fp,
                                             const uint16_t n,
                                             double *acf)
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_print_correlogram((struct cmb_dataset *)tsp, fp, n, acf);
}


#endif /* CIMBA_CMB_TIMESERIES_H */
