/*
 * cmb_timeseries.h - an automatically resizing array of ordered sample values,
 * each sample a (x, t) tuple.
 *
 * It does not keep a running tally, use cmb_timeseries_summarize() to compute
 * the statistics when needed. The data array is allocated from the heap as
 * needed and free'd by either cmb_dataset_reset or cmb_dataset_destroy.
 * The internal data array will be created on the heap even if the data series
 * is declared as a local variable (on the stack).
 *
 * The use case is that states change only at the discrete event times in a
 * discrete event simulation. Between events, everything is constant. If sample
 * values only are recorded at event times, the statistics may be biased.
 * For example, collecting length data of a queue that is mostly empty with
 * long time intervals of zero length. Storing data with time stamps allows
 * correct weighting. Use cmb_timeseries_summarize() to compute the statistics
 * into a weighted data summary when needed.
 *
 * Implemented by inheritance from cmb_dataset. Wrapper functions given below.
 * The ta array are the time stamps, wa is for calculated weights (intervals).
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

#ifndef CIMBA_CMB_TIMESERIES_H
#define CIMBA_CMB_TIMESERIES_H

#include "cmb_dataset.h"
#include "cmb_wtdsummary.h"

struct cmb_timeseries {
    struct cmb_dataset ds;
    double *ta;
    double *wa;
};

/* Manage the timeseries themselves */
extern struct cmb_timeseries *cmb_timeseries_create(void);
extern void cmb_timeseries_initialize(struct cmb_timeseries *tsp);
extern void cmb_timeseries_reset(struct cmb_timeseries *tsp);
extern void cmb_timeseries_terminate(struct cmb_timeseries *tsp);
extern void cmb_timeseries_destroy(struct cmb_timeseries *tsp);

/* Copy tgt into src, overwriting whatever was in tgt */
extern uint64_t cmb_timeseries_copy(struct cmb_timeseries *tgt,
                                    const struct cmb_timeseries *src);

/* Add a single value to a timeseries, resizing the array as needed */
extern uint64_t cmb_timeseries_add(struct cmb_timeseries *tsp,
                                   double x,
                                   double t);

/*
 * Add a final data point at the given time t with the same x-value as
 * the last recorded value. Used to ensure that the last value gets weighted
 * by its correct duration from the event time to the end of the data
 * collection period.
 *
 * Typically called as cmb_timeseries_finalize(tsp, cmb_time()) during the
 * simulation closing ceremonies. The closing time is left as an explicit
 * argument for user flexibility in any other use cases and for better
 * separation between Cimba modules cmb_data and cmb_event.
 */
extern uint64_t cmb_timeseries_finalize(struct cmb_timeseries *tsp, double t);

/*
 * Sort timeseries in ascending order by x-value. The time stamps and weights
 * in the ta and wa arrays follow the xa values.
 * Caution: Changes the sequence of data points, no longer a timeseries.
 */
extern void cmb_timeseries_sort_x(struct cmb_timeseries *tsp);

/* An "undo" function, sorting it back to ascending time sequence */
extern void cmb_timeseries_sort_t(struct cmb_timeseries *tsp);

/*
 * Calculate summary statistics of the data series
 * Returns the number of data points in the summary.
 */
extern uint64_t cmb_timeseries_summarize(const struct cmb_timeseries *tsp,
                                         struct cmb_wtdsummary *wsp);

static inline uint64_t cmb_timeseries_count(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    return cmb_dataset_count((struct cmb_dataset *)tsp);
}

static inline double cmb_timeseries_min(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    return cmb_dataset_min((struct cmb_dataset *)tsp);
}

static inline double cmb_timeseries_max(const struct cmb_timeseries *tsp)
{
    cmb_assert_release(tsp != NULL);

    return cmb_dataset_max((struct cmb_dataset *)tsp);
}

/*
 * Calculate and return the median of the data series, weighted by duration.
 * Uses linear interpolation for the median value at 50 % of the summed weights.
 * Call cmb_dataset_median((struct cmb_dataset *)tsp, ...) for unweighted.
 */
extern double cmb_timeseries_median(const struct cmb_timeseries *tsp);

/*
 * Calculate and print the "five-number" summary of timeseries quantiles,
 * weighted by duration (the holding time from one sample to the next).
 * Call cmb_dataset_print_fivenum((struct cmb_dataset *)tsp, ...) to get
 * unweighted quantiles.
 */
extern void cmb_timeseries_print_fivenum(const struct cmb_timeseries *tsp,
                                         FILE *fp,
                                         bool lead_ins);

/*
 *  Print a simple character-based histogram.
 *  Like cmb_dataset_histogram but weighted by the interval until next sample.
 *  Call cmb_dataset_histogram((struct cmb_dataset *)tsp, ...) for unweighted.
 */
extern void cmb_timeseries_print_histogram(const struct cmb_timeseries *tsp,
                                           FILE *fp,
                                           uint16_t num_bins,
                                           double low_lim,
                                           double high_lim);

/* Print the raw data in the timeseries. */
extern void cmb_timeseries_print(const struct cmb_timeseries *tsp, FILE *fp);

/*
 * Calculate the n first autocorrelation coefficients between individual
 * samples, only considering the sequence, disregarding the time duration
 * between samples.
 */
static inline void cmb_timeseries_ACF(const struct cmb_timeseries *tsp,
                                      const uint16_t max_lag,
                                      double acf[max_lag + 1u])
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_ACF((struct cmb_dataset *)tsp, max_lag, acf);
}

/*
 * Calculate the n first partial autocorrelation coefficients, again only
 * considering sequence, not interval durations.
 *
 * The first step in the algorithm is to calculate the ACFs. If these already
 * have been calculated, they can be given as the last argument acf[].
 * If this argument is NULL, they will be calculated directly from the dataset.
 */
static inline void cmb_timeseries_PACF(const struct cmb_timeseries *tsp,
                                       const uint16_t max_lag,
                                       double pacf[max_lag + 1u],
                                       double acf[max_lag + 1u])
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_PACF((struct cmb_dataset *)tsp, max_lag, pacf, acf);
}

/*
 * Print a simple correlogram of the autocorrelation coefficients previously
 * calculated, either ACFs or PACFs.
 *
 * If the data vector acf[] is NULL, ACFs will be calculated directly from the
 * dataset by calling cmb_dataset_ACF.
 *
 * To print PACFs, simply give a vector of PACFs as the acf argument.
 */
static inline void cmb_timeseries_print_correlogram(const struct cmb_timeseries *tsp,
                                             FILE *fp,
                                             const uint16_t max_lag,
                                             double acf[max_lag + 1u])
{
    cmb_assert_release(tsp != NULL);

    cmb_dataset_print_correlogram((struct cmb_dataset *)tsp, fp, max_lag, acf);
}


#endif /* CIMBA_CMB_TIMESERIES_H */
