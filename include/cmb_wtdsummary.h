/**
 * @file cmb_wtdsummary.h
 * @brief A running tally of basic statistics, not keeping the individual sample
 *        values, each sample weighted by a double in the summary.
 *
 * It can be used for time series statistics where each value is held for a
 * certain duration, such as queue lengths or the number of customers in a
 * queueing system.
 */

/*
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-26.
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

#ifndef CIMBA_CMB_WTDSUMMARY_H
#define CIMBA_CMB_WTDSUMMARY_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_datasummary.h"

/**
 * @brief A weighted data summary maintaining running tally statistics, derived
 *        from the `cmb_datasummary` parent class.
 *
 * @extends cmb_datasummary
 */
struct cmb_wtdsummary {
    struct cmb_datasummary ds;  /**< Parent class, inheriting all properties from there */
    double wsum;                /**< Summed weights */
};

/**
 * @brief Allocate a weighted data summary object on the heap.
 *
 * Note that this does not allocate from a thread local memory pool,
 * since it may be passed back outside the current replication.
 *
 * @relates cmb_wtdsummary
 * @return A pointer to a newly allocated weighted data summary object.
 */
extern struct cmb_wtdsummary *cmb_wtdsummary_create(void);

/**
 *  @brief Initialize a weighted data summary, not necessarily allocated on the
 *         heap.
 *
 * @relates cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 */
extern void cmb_wtdsummary_initialize(struct cmb_wtdsummary *wsp);

/**
 * @brief Reset a previously used weighted data summary to a newly initialized
 *        state.
 *
 * @relates cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 */
extern void cmb_wtdsummary_reset(struct cmb_wtdsummary *wsp);

/**
 * @brief Un-initialize the weighted data summary, returning it to a newly
 *        created state.
 *
 * @relates cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 */
extern void cmb_wtdsummary_terminate(struct cmb_wtdsummary *wsp);

/**
 * @brief Deallocate (free) the allocated memory for a weighted data summary.
 *
 * @relates cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary previously created by
 *              `cmb_datasummary_create`.
 */
extern void cmb_wtdsummary_destroy(struct cmb_wtdsummary *wsp);

/**
 * @brief Add a sample `(x, w)` to the weighted summary. Any zero-weight samples
 *        are ignored, not even counted.
 *
 * @relates cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @param x Sample value to be added.
 * @param w Sample weight to be added.
 * @return The updated sample count.
*/
extern uint64_t cmb_wtdsummary_add(struct cmb_wtdsummary *wsp,
                                   double x,
                                   double w);

/**
 * @brief Merge two weighted data summaries. The target may be one of the
 *        sources.
 *
 * See: Pébay & al, "Numerically stable, scalable formulas for parallel and
 *      online computation of higher-order multivariate central moments with
 *      arbitrary weights", Computational Statistics (2016) 31:1305–1325
 *
 * @relates cmb_wtdsummary
 * @param tgt Pointer to a weighted summary to receive the merge.
 *            Any previous content will be overwritten. Possibly equal to
 *            `ws1`or `ws2`, or a separate third weighted data summary.
 * @param ws1 Pointer to a weighted data summary to be merged.
 * @param ws2 Pointer to a weighted data summary to be merged.
 * @return The combined sample count.
 */
extern uint64_t cmb_wtdsummary_merge(struct cmb_wtdsummary *tgt,
                                     const struct cmb_wtdsummary *ws1,
                                     const struct cmb_wtdsummary *ws2);

/**
* @brief   The number of samples in the data summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @return The number of samples in the weighted data summary.
 */
static inline uint64_t cmb_wtdsummary_count(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_count((struct cmb_datasummary *)wsp);
}

/**
 * @brief The largest sample value (x) in the weighted data summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @return The largest sample value in the data summary.
 */
static inline double cmb_wtdsummary_max(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_max((struct cmb_datasummary *)wsp);
}

/**
 * @brief The smallest sample value (x) in the weighted data summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @return The smallest sample value in the data summary.
 */
static inline double cmb_wtdsummary_min(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_min((struct cmb_datasummary *)wsp);
}

/**
 * @brief The weighted mean of the samples in the weighted data summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @return The weighted mean of the samples in the weighted data summary.
 */
static inline double cmb_wtdsummary_mean(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_mean((struct cmb_datasummary *)wsp);
}

/**
 * @brief The weighted sample variance of the samples in the weighted data
 *        summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @return The weighted sample variance of the samples in the weighted data
 * summary.
 */
static inline double cmb_wtdsummary_variance(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_variance((struct cmb_datasummary *)wsp);
}

/**
 * @brief The weighted sample standard deviation of the samples in the weighted
 *        data summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @return The weighted sample standard deviation of the samples in the weighted
 *         data summary.
 */
static inline double cmb_wtdsummary_stddev(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_stddev((struct cmb_datasummary *)wsp);
}

/**
 * @brief The weighted sample skewness of the samples in the weighted data
 *        summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a data summary.
 * @return The weighted sample skewness of the samples in the weighted data
 *         summary.
 */
static inline double cmb_wtdsummary_skewness(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_skewness((struct cmb_datasummary *)wsp);
}

/**
 * @brief The weighted sample excess kurtosis of the samples in the weighted
 *        data summary.
 *
 * @memberof cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary
 * @return The weighted sample excess kurtosis of the samples in the weighted
 *         data summary.
 */
static inline double cmb_wtdsummary_kurtosis(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_kurtosis((struct cmb_datasummary *)wsp);
}

/**
 * @brief Print a line of basic statistics for the weighted data summary.
 *
 * @relates cmb_wtdsummary
 * @param wsp Pointer to a weighted data summary.
 * @param fp A file pointer for where to print, possibly `stdout`
 * @param lead_ins Flag to control if explanatory text is printed. If false,
 *                 only prints a tab-separated line of numeric values.
 */
extern void cmb_wtdsummary_print(const struct cmb_wtdsummary *wsp,
                                 FILE *fp,
                                 bool lead_ins);

#endif /* CIMBA_CMB_WTDSUMMARY_H */
