/*
 * cmb_wtdsummary.h - a running tally of basic statistics, not keeping the
 * individual sample values, each sample weighted by a double in the summary.
 *
 * It can be used for time series  statistics where each value is held for a
 * certain duration, such as queue lengths or the number of customers in a
 * queueing system.
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

#ifndef CIMBA_CMB_WTDSUMMARY_H
#define CIMBA_CMB_WTDSUMMARY_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_datasummary.h"

struct cmb_wtdsummary {
    struct cmb_datasummary ds;
    double wsum;
};

extern struct cmb_wtdsummary *cmb_wtdsummary_create(void);
extern void cmb_wtdsummary_initialize(struct cmb_wtdsummary *wsp);
extern void cmb_wtdsummary_reset(struct cmb_wtdsummary *wsp);
extern void cmb_wtdsummary_terminate(struct cmb_wtdsummary *wsp);
extern void cmb_wtdsummary_destroy(struct cmb_wtdsummary *wsp);

extern uint64_t cmb_wtdsummary_add(struct cmb_wtdsummary *wsp,
                                   double x,
                                   double w);

extern uint64_t cmb_wtdsummary_merge(struct cmb_wtdsummary *tgt,
                                   const struct cmb_wtdsummary *ws1,
                                   const struct cmb_wtdsummary *ws2);

/*
 * Member functions "inherited" from the base data summary class,
 * but using the already weighted moments.
 */
static inline uint64_t cmb_wtdsummary_count(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_count((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_max(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_max((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_min(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_min((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_mean(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_mean((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_variance(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_variance((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_stddev(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_stddev((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_skewness(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_skewness((struct cmb_datasummary *)wsp);
}

static inline double cmb_wtdsummary_kurtosis(const struct cmb_wtdsummary *wsp)
{
    cmb_assert_release(wsp != NULL);

    return cmb_datasummary_kurtosis((struct cmb_datasummary *)wsp);
}

extern void cmb_wtdsummary_print(const struct cmb_wtdsummary *wsp,
                                 FILE *fp,
                                 bool lead_ins);

#endif /* CIMBA_CMB_WTDSUMMARY_H */
