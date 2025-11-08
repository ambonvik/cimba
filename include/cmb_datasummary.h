/*
 * cmb_datasummary.h - a running tally of basic statistics, not keeping
 *                    individual sample values.
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

struct cmb_datasummary {
    uint64_t cnt;
    double min;
    double max;
    double m1;
    double m2;
    double m3;
    double m4;
};

/*
 * Allocate a data summary object on the heap and initialize it.
 * Note that this does not allocate from a thread local memory pool,
 * since it may be passed back outside the current replication.
 */
extern struct cmb_datasummary *cmb_datasummary_create(void);

/* A matching function to free the heap area again if created there. */
extern void cmb_datasummary_destroy(struct cmb_datasummary *dsp);

/* Initialize a given data summary, not necessarily allocated on the heap */
extern void cmb_datasummary_initialize(struct cmb_datasummary *dsp);

/* Reset a previously used data summary to newly initialized state */
extern void cmb_datasummary_reset(struct cmb_datasummary *dsp);

/* Un-initialize the data summary, returning it to newly created state */
extern void cmb_datasummary_terminate(struct cmb_datasummary *dsp);

/*
 * Add a single value to a data summary, updating running statistics.
 * Returns the updated sample count.
 */
extern uint64_t cmb_datasummary_add(struct cmb_datasummary *dsp, double y);

/*
 * Merge two data summaries s1 and s2 into the given target.
 * The target can be one of the sources, merging the other source into this.
 *
 * Use case: Partition a simulation across several pthreads and CPU cores,
 * assemble the final results by merging the data summaries returned by each.
 *
 * Returns the combined sample count.
 */
extern uint64_t cmb_datasummary_merge(struct cmb_datasummary *tgt,
                                      const struct cmb_datasummary *dsp1,
                                      const struct cmb_datasummary *dsp2);

/* The various summary statistics, starting with the sample count */
static inline uint64_t cmb_datasummary_count(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->cnt;
}

static inline double cmb_datasummary_max(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->max;
}

static inline double cmb_datasummary_min(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->min;
}

static inline double cmb_datasummary_mean(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return dsp->m1;
}

/* Sample variance */
static inline double cmb_datasummary_variance(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    double r = 0.0;
    if (dsp->cnt > 1) {
        r = dsp->m2 / (double)(dsp->cnt - 1u);
    }

    cmb_assert_debug(r >= 0.0);

    return r;
}

static inline double cmb_datasummary_stddev(const struct cmb_datasummary *dsp)
{
    cmb_assert_release(dsp != NULL);

    return sqrt(cmb_datasummary_variance(dsp));
}

/* Sample skewness */
extern double cmb_datasummary_skewness(const struct cmb_datasummary *dsp);

/* Sample excess kurtosis */
extern double cmb_datasummary_kurtosis(const struct cmb_datasummary *dsp);

/*
 * Print a line of basic statistics for the dataset.
 * The argument lead_ins controls if explanatory text is printed.
 * If false, only prints a tab-separated line of numeric values.
 */
extern void cmb_datasummary_print(const struct cmb_datasummary *dsp,
                              FILE *fp,
                              bool lead_ins);

#endif /* CIMBA_CMB_DATASUMMARY_H */
