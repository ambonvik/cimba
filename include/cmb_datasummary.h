/**
 * @file cmb_datasummary.h
 * @brief  A running tally of basic statistics. The `cmb_datasummary` does not
 * keep individual data values, just the summary statistics. Use `cmb_dataset`
 * instead if youneed individual values, and use `cmb_dataset_summarize` to
 * extract the summary statistics from a collected data set.
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


#ifndef CIMBA_CMB_DATASUMMARY_H
#define CIMBA_CMB_DATASUMMARY_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "cmb_assert.h"

/**
 * @brief A datasummary object with its tallies.
 */
struct cmb_datasummary {
    uint64_t count;     /**< The number of samples seen */
    double min;         /**< The smallest sample seen, initially `DBL_MAX` */
    double max;         /**< The largest sample seen, initially `-DBL_MAX` */
    double m1;          /**< First raw moment */
    double m2;          /**< Second raw moment */
    double m3;          /**< Third raw moment */
    double m4;          /**< Fourth raw moment */
};

/**
 * @brief Allocate a data summary object on the heap and initialize it.
 *
 * Note that this does not allocate from a thread local memory pool,
 * since it may be passed back outside the current replication.
 */
extern struct cmb_datasummary *cmb_datasummary_create(void);

/**
 * @brief Deallocate (free) the allocated memory for a `cmb_datasummary` object.
 * @param dsp Pointer to a `cmb_datasummary` object previously created by
 *              `cmb_datasummary_create`.
 */
extern void cmb_datasummary_destroy(struct cmb_datasummary *dsp);

/**
 *  @brief Initialize a given data summary, not necessarily allocated on the heap.
 *  @param dsp Pointer to a valid `cmb_datasummary` object.
 */
extern void cmb_datasummary_initialize(struct cmb_datasummary *dsp);

/**
 * @brief Reset a previously used data summary to newly initialized state.
*  @param dsp Pointer to a valid `cmb_datasummary` object.
 */
extern void cmb_datasummary_reset(struct cmb_datasummary *dsp);

/**
 * @brief Un-initialize the data summary, returning it to newly created state.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 */
extern void cmb_datasummary_terminate(struct cmb_datasummary *dsp);

/**
 * @brief Add a single value to a data summary, updating running statistics.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @param y Sample value to be added.
 * @return The updated sample count.
 */
extern uint64_t cmb_datasummary_add(struct cmb_datasummary *dsp, double y);

/**
 * @brief Merge two data summaries s1 and s2 into the given target. The target
 * can be one of the sources, merging the other source into this.
 *
 * Use case: Partition a simulation across several pthreads and CPU cores,
 * assemble the final results by merging the data summaries returned by each.
 *
 * @param tgt Pointer to an initialized `cmb_datasummary` object to receive the
 *            merge. Any previous content will be overwritten.
 * @param dsp1 Pointer to an initialized `cmb_datasummary` object.
 * @param dsp2 Pointer to an initialized `cmb_datasummary` object.
 * @return The combined sample count.
 */
extern uint64_t cmb_datasummary_merge(struct cmb_datasummary *tgt,
                                      const struct cmb_datasummary *dsp1,
                                      const struct cmb_datasummary *dsp2);

/**
 * @brief The number of samples included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The number of samples included in the data summary.
 */
static inline uint64_t cmb_datasummary_count(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->count;
}

/**
 * @brief The largest sample included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The largest sample included in the data summary.
 */
static inline double cmb_datasummary_max(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->max;
}

/**
 * @brief The smallest sample included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The smallest sample included in the data summary.
 */
static inline double cmb_datasummary_min(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->min;
}

/**
 * @brief The mean of the samples included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The mean of the samples included in the data summary.
 */
static inline double cmb_datasummary_mean(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->m1;
}

/**
 * @brief The sample variance of the samples included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The sample variance of the samples included in the data summary.
 */
static inline double cmb_datasummary_variance(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    double r = 0.0;
    if (dsp->count > 1) {
        r = dsp->m2 / (double)(dsp->count - 1u);
    }

    cmb_assert_debug(r >= 0.0);

    return r;
}

/**
 * @brief The sample standard deviation of the samples included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The sample standard deviation of the samples included in the data summary.
 */
static inline double cmb_datasummary_stddev(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return sqrt(cmb_datasummary_variance(dsp));
}

/**
 * @brief The sample skewness of the samples included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The sample skewness of the samples included in the data summary.
 */
extern double cmb_datasummary_skewness(const struct cmb_datasummary *dsp);

/**
 * @brief The sample excess kurtosis of the samples included in the data summary.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @return The sample excess kurtosis of the samples included in the data summary.
 */
extern double cmb_datasummary_kurtosis(const struct cmb_datasummary *dsp);

/**
 * @brief Print a line of basic statistics for the dataset.
 * @param dsp Pointer to a valid `cmb_datasummary` object.
 * @param fp A file pointer for where to print, possibly `stdout`
 * @param lead_ins Flag to control if explanatory text is printed.
 * If false, only prints a tab-separated line of numeric values.
 */
extern void cmb_datasummary_print(const struct cmb_datasummary *dsp,
                              FILE *fp,
                              bool lead_ins);

#endif /* CIMBA_CMB_DATASUMMARY_H */
