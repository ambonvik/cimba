/* 
 * cmb_data.h - basic data collector utilities, providing the three "classes":
 *      cmb_summary - a running tally of basic statistics, not keeping indivicual sample values.
 *      cmb_wsummary - as above, but each sample also weighted by a double.
 *      cmb_dataset - an automatically resizing array of possibly unordered sample (x) values.
 *      cmb_data_timeseries - an automatically resizing array of sequential sample (t, x) pairs.
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

#ifndef CIMBA_CMB_DATA_H
#define CIMBA_CMB_DATA_H

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmi_memutils.h"

/******************************************************************************
 * The cmb_summary maintains a running tally of key statistics,
 * using numerically stable methods for updating the first four moments.
 */
struct cmb_summary {
    uint64_t cnt;
    double min;
    double max;
    double m1;
    double m2;
    double m3;
    double m4;
};

/* Initialize a given data summary, not necessarily allocated on the heap */
inline void cmb_summary_init(struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);

    sup->cnt = 0u;
    sup->max = -DBL_MAX;
    sup->min = DBL_MAX;
    sup->m1 = sup->m2 = sup->m3 = sup->m4 = 0.0;
}

inline void cmb_summary_clear(struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    cmb_summary_init(sup);
}

/*
 * Allocate a data summary object on the heap and initialize it.
 * Note that this does not allocate from a thread local memory pool,
 * since it may be passed back outside the current replication.
 */
inline struct cmb_summary *cmb_summary_create(void) {
    struct cmb_summary *sup = cmi_malloc(sizeof *sup);
    cmb_summary_init(sup);
    return sup;
}

/*
 * A matching function to free the heap area again if created there.
 */
inline void cmb_summary_destroy(struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    cmi_free(sup);
}

/*
 * Add a single value to a data summary, updating running statistics.
 * Returns the updated sample count.
 */
extern uint64_t cmb_summary_add(struct cmb_summary *sup, double y);

/*
 * Merge two data summaries s1 and s2 into the given target.
 * The target can be one of the sources, merging the other source into this.
 *
 * Use case: Partition a simulation across several pthreads and CPU cores,
 * assemble the final results by merging the data summaries returned by each.
 *
 * Returns the combined sample count.
 */
extern uint64_t cmb_summary_merge(struct cmb_summary *tgt,
                                        const struct cmb_summary *sup1,
                                        const struct cmb_summary *sup2);

/* Access methods returning the various summary statistics, starting with the sample count */
inline uint64_t cmb_summary_count(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    return sup->cnt;
}

inline double cmb_summary_max(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    return sup->max;
}

inline double cmb_summary_min(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    return sup->min;
}

inline double cmb_summary_mean(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    return sup->m1;
}

/* Sample variance */
inline double cmb_summary_variance(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);

    double r = 0.0;
    if (sup->cnt > 1) {
        r = sup->m2 / (double)(sup->cnt - 1u);
    }

    cmb_assert_debug(r >= 0.0);
    return r;
}

inline double cmb_summary_stddev(const struct cmb_summary *sup) {
    cmb_assert_release(sup != NULL);
    return sqrt(cmb_summary_variance(sup));
}

/* Sample skewness */
extern double cmb_summary_skewness(const struct cmb_summary *sup);

/* Sample excess kurtosis */
extern double cmb_summary_kurtosis(const struct cmb_summary *sup);

/*
 * Print a line of basic statistics for the dataset.
 * The argument lead_ins controls if explanatory text is printed.
 * If false, only prints a tab-separated line of numeric values.
 */
extern void cmb_summary_print(const struct cmb_summary *sup, FILE *fp, bool lead_ins);

/******************************************************************************
 * The cmb_wsummary does the same thing as cmb_summary, but
 * each sample value is weighted by a double precision value. It can be used
 * for time series statistics where each value is held for a certain duration,
 * such as queue lengths or the number of customers in a system.
 */
struct cmb_wsummary {
    struct cmb_summary ds;
    double wsum;
};

inline void cmb_wsummary_init(struct cmb_wsummary *wsup) {
    cmb_assert_release(wsup != NULL);
    cmb_summary_init((struct cmb_summary *)wsup);
    wsup->wsum = 0.0;
}

inline void cmb_wsummary_clear(struct cmb_wsummary *wsup) {
    cmb_assert_release(wsup != NULL);
    cmb_wsummary_init(wsup);
}

inline struct cmb_wsummary *cmb_wsummary_create(void) {
    struct cmb_wsummary *wsup = cmi_malloc(sizeof *wsup);
    cmb_wsummary_init(wsup);
    return wsup;
}

inline void cmb_wsummary_destroy(struct cmb_wsummary *wsup) {
    cmb_assert_release(wsup != NULL);
    cmi_free(wsup);
}

extern uint64_t cmb_wsummary_add(struct cmb_wsummary *wsup, double y, double w);

extern uint64_t cmb_wsummary_merge(struct cmb_wsummary *tgt,
                                        const struct cmb_wsummary *ws1,
                                        const struct cmb_wsummary *ws2);

/*
 * Member functions "inherited" from the base data summary class,
 * but using the already weighted moments.
 */
inline uint64_t cmb_wsummary_count(const struct cmb_wsummary *wsup) {
    return cmb_summary_count((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_max(const struct cmb_wsummary *wsup) {
    return cmb_summary_max((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_min(const struct cmb_wsummary *wsup) {
    return cmb_summary_min((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_mean(const struct cmb_wsummary *wsup) {
    return cmb_summary_mean((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_variance(const struct cmb_wsummary *wsup) {
    return cmb_summary_variance((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_stddev(const struct cmb_wsummary *wsup) {
    return cmb_summary_stddev((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_skewness(const struct cmb_wsummary *wsup) {
    return cmb_summary_skewness((struct cmb_summary *)wsup);
}

inline double cmb_wsummary_kurtosis(const struct cmb_wsummary *wsup) {
    return cmb_summary_kurtosis((struct cmb_summary *)wsup);
}

inline void cmb_wsummary_print(const struct cmb_wsummary *wsup, FILE *fp, const bool lead_ins) {
    cmb_summary_print((struct cmb_summary *)wsup, fp, lead_ins);
}

/******************************************************************************
 * cmb_dataset - a conveniently resizing array for keeping the sample values.
 * It does not keep a running tally, use cmb_dataset_summarize() to compute
 * the statistics when needed. The data array is allocated from the heap as needed
 * and free'd by either cmb_dataset_clear or cmb_dataset_destroy.
 * Use the init/clear pair for data series declared as local variables on the stack,
 * use create/destroy to allocate and free data series on the heap. The internal
 * data array will be created on the heap even if the data series is declared local.
 * The cmb_dataset keeps its own cache of the largest and smallest members,
 * for use in histograms without requiring a complete statistics calculation.
 */

struct cmb_dataset {
    uint64_t cursize;
    uint64_t cnt;
    double min;
    double max;
    double *xa;
};

/* Manage the datasets themselves */
extern struct cmb_dataset *cmb_dataset_create(void);
extern void cmb_dataset_init(struct cmb_dataset *dsp);
extern uint64_t cmb_dataset_copy(struct cmb_dataset *tgt,
                                 const struct cmb_dataset *src);
extern uint64_t cmb_dataset_merge(struct cmb_dataset *tgt,
                                  const struct cmb_dataset *s1,
                                  const struct cmb_dataset *s2);
extern void cmb_dataset_clear(struct cmb_dataset *dsp);
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
                                  struct cmb_summary *dsump);

inline uint64_t cmb_dataset_count(const struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);
    return dsp->cnt;
}

inline double cmb_dataset_min(const struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);
    return dsp->min;
}

inline double cmb_dataset_max(const struct cmb_dataset *dsp) {
    cmb_assert_release(dsp != NULL);
    return dsp->max;
}

/* Calculate and return the median of the data series */
extern double cmb_dataset_median(const struct cmb_dataset *dsp);

/* Calculate and print the "five-number" summary of dataset quantiles */
extern void cmb_dataset_print_fivenum(const struct cmb_dataset *dsp,
                                      FILE *fp, bool lead_ins);

/*
 *  Print a simple character-based histogram.
 *  Will autoscale to the dataset range if LowerLimit == UpperLimit.
 *  Adds overflow bins to the ends of the range to catch anything outside.
 *  Will print symbol '*' for a full "pixel",  *  '+' for one that is more
 *  than half full, and '-' for one that is less than half full.
 */
extern void cmb_dataset_print_histogram(const struct cmb_dataset *dsp, FILE *fp,
                                        unsigned num_bins, double low_lim, double high_lim);

/* Print the raw data in the dataset. */
extern void cmb_dataset_print_data(const struct cmb_dataset *dsp, FILE *fp);

/* Calculate the n first autocorrelation coefficients. */
extern void cmb_dataset_ACF(const struct cmb_dataset *dsp, unsigned max_lag,
                                double acf[max_lag + 1u]);

/*
 * Calculate the n first partial autocorrelation coefficients.
 * The first step in the algorithm is to calculate the ACFs. If these already
 * have been calculated, they can be given as the last argument acf[].
 * If this argument is NULL, they will be calculated directly from the dataset.
 */
extern void cmb_dataset_PACF(const struct cmb_dataset *dsp, unsigned max_lag,
                                 double pacf[max_lag + 1u], double acf[max_lag + 1u]);

/*
 * Print a simple correlogram of the autocorrelation coefficients previously calculated,
 * either ACFs or PACFs. If the data vector acf[] is NULL, ACFs will be calculated
 * directly from the dataset by calling cmb_dataset_ACF. To print PACFs, simply give
 * a vector of PACFs as the acf argument.
 */
extern void cmb_dataset_print_correlogram(const struct cmb_dataset *dsp, FILE *fp,
                                          unsigned max_lag, double acf[max_lag + 1u]);

/******************************************************************************
 * cmb_timeseries - a similarly resizing array for keeping (x, t) value tuples.
 * The use case is that states change only at the discrete event times in a
 * discrete event simulation. Between events, everything is constant. If sample
 * values only are recorded at event times, the statistics may be biased.
 * For example, collecting length data of a queue that is mostly empty with
 * long time intervals of zero length. Storing data with time stamps allows
 * correct weighting. Use cmb_timeseries_summarize() to compute the statistics
 * into a weighted data summary when needed.
 * Implemented by inheritance from cmb_dataset. Wrapper functions given below.
 */

struct cmb_timeseries {
    struct cmb_dataset ds;
    double *ta;
};

/* Manage the timeseries themselves */
extern struct cmb_timeseries *cmb_timeseries_create(void);
extern void cmb_timeseries_init(struct cmb_timeseries *tsp);
extern void cmb_timeseries_clear(struct cmb_timeseries *tsp);
extern void cmb_timeseries_destroy(struct cmb_timeseries *tsp);

/* Add a single value to a timeseries, resizing the array as needed */
extern uint64_t cmb_timeseries_add(struct cmb_timeseries *tsp, double x, double t);

/*
 * Add a final data point at the given time t with the same x-value as
 * the last recorded value. Used to ensure that the last value gets weighted by its
 * correct duration from the event time to the end of the data collection period.
 * Typically called as cmb_timeseries_finalize(tsp, cmb_time()) during the
 * simulation closing ceremonies. The closing time is left as an explicit argument
 * for user flexibility in any other use cases and for better separation between
 * Cimba modules cmb_data and cmb_event.
 */
extern uint64_t cmb_timeseries_finalize(struct cmb_timeseries *tsp, double t);

/*
 * Calculate summary statistics of the data series
 * Returns the number of data points in the summary.
 */
extern uint64_t cmb_timeseries_summarize(const struct cmb_timeseries *tsp,
                                  struct cmb_wsummary *wsump);

inline uint64_t cmb_timeseries_count(const struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);
    return cmb_dataset_count((struct cmb_dataset *)tsp);
}

inline double cmb_timeseries_min(const struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);
    return cmb_dataset_min((struct cmb_dataset *)tsp);
}

inline double cmb_timeseries_max(const struct cmb_timeseries *tsp) {
    cmb_assert_release(tsp != NULL);
    return cmb_dataset_max((struct cmb_dataset *)tsp);
}

/*
 * Calculate and return the median of the data series, weighted by duration.
 * Call cmb_dataset_median((struct cmb_dataset *)tsp, ...) for unweighted.
 */
extern double cmb_timeseries_median(const struct cmb_timeseries *tsp);

/*
 * Calculate and print the "five-number" summary of timeseries quantiles,
 * weighted by duration (the holding time from one sample to the next).
 * Call cmb_dataset_print_fivenum((struct cmb_dataset *)tsp, ...) for unweighted.
 */
extern void cmb_timeseries_print_fivenum(const struct cmb_timeseries *tsp,
                                      FILE *fp, bool lead_ins);

/*
 *  Print a simple character-based histogram.
 *  Like cmb_dataset_histogram but weighted by the interval until next sample.
 *  Call cmb_dataset_histogram((struct cmb_dataset *)tsp, ...) for unweighted.
 */
extern void cmb_timeseries_print_histogram(const struct cmb_timeseries *tsp, FILE *fp,
                                        uint16_t num_bins, double low_lim, double high_lim);

/* Print the raw data in the timeseries. */
extern void cmb_timeseries_print(const struct cmb_timeseries *tsp, FILE *fp);

/*
 * Calculate the n first autocorrelation coefficients between individual samples,
 * only considering the sequence, disregarding the time duration between the samples.
 */
inline void cmb_timeseries_ACF(const struct cmb_timeseries *tsp, const uint16_t max_lag,
                                double acf[max_lag + 1u]) {
    cmb_assert_release(tsp != NULL);
    cmb_dataset_ACF((struct cmb_dataset *)tsp, max_lag, acf);
}

/*
 * Calculate the n first partial autocorrelation coefficients, again only
 * considering sequence, not interval durations.
 */
inline void cmb_timeseries_PACF(const struct cmb_timeseries *tsp, uint16_t max_lag,
                                 double pacf[max_lag + 1u], double acf[max_lag + 1u]) {
    cmb_assert_release(tsp != NULL);
    cmb_dataset_PACF((struct cmb_dataset *)tsp, max_lag, pacf, acf);
}

/*
 * Print a simple correlogram of the autocorrelation coefficients previously calculated.
 */
inline void cmb_timeseries_print_correlogram(const struct cmb_timeseries *tsp, FILE *fp,
                                          uint16_t max_lag, double acf[max_lag + 1u]) {
    cmb_assert_release(tsp != NULL);
    cmb_dataset_print_correlogram((struct cmb_dataset *)tsp, fp, max_lag, acf);
}


#endif /* CIMBA_CMB_DATA_H */
