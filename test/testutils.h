/*
 * Utility functions for test scripts
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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

#ifndef CIMBA_TESTUTILS_H
#define CIMBA_TESTUTILS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cmb_assert.h"
#include "cmb_dataset.h"

enum cmi_test_type {
    CMI_TEST_GOF_U01,
    CMI_TEST_TWO_SAMPLE
};

enum cmi_test_status {
    CMI_TEST_OK = 0,
    CMI_TEST_TOO_FEW,        /* Not enough samples per bin */
    CMI_TEST_OUT_OF_RANGE,   /* Sample outside [0,1] */
    CMI_TEST_DEGENERATE,     /* All samples near identical, NaN present */
    CMI_TEST_SATURATED       /* Too many exact 0.0 and 1.0 samples */
};

struct cmi_test_partial {
    const char *name;
    unsigned level;
    double v;       /* Value of statistic */
    double e;       /* Its expected value */
    double lp;      /* log probability */
    double s;       /* Sigmas from expected */
};

#define CMI_TEST_U01_PARTS 10
struct cmi_test_outcome {
    enum cmi_test_type type;
    enum cmi_test_status status;
    uint64_t n_clamped;
    uint64_t n;
    double min;
    double max;
    double combined_lp;
    double combined_sigma;
    unsigned nparts;
    struct cmi_test_partial p[CMI_TEST_U01_PARTS];
};

typedef double (cmi_test_transform_func)(double, void*);

/*
 * Transform a data set according to a mapping function double -> double.
 * The target can be the same as the source, overwriting previous values.
 * Used for converting other distributions to U(0,1) for testing.
 */
extern void cmi_test_transform(struct cmb_dataset *tgt,
                               const struct cmb_dataset *src,
                               cmi_test_transform_func *map,
                               void *arg);

/*
 * Goodness-of-fit test for continuous-valued distributions: Is the dataset
 * distributed according to the CDF? Returns a sigma value (standard deviations
 * of the standard normal distribution) where a high absolute value of sigma
 * indicates improbability and the sign the direction from the expected value.
 */
extern double cmi_test_gof_cont(cmi_test_transform_func *cdf,
                                void *cdf_arg,
                                const struct cmb_dataset *dsp,
                                struct cmi_test_outcome *result);

/*
 * Goodness-of-fit test for discrete-valued distributions: is the dataset
* distributed according to the PMF? Returns a sigma value (standard deviations
 * of the standard normal distribution) where a high absolute value of sigma
 * indicates improbability and the sign the direction from the expected value.
 */
extern double cmi_test_gof_disc(uint64_t n,
                                double p_vec[n + 2],
                                double v_vec[n + 2],
                                const struct cmb_dataset *dsp,
                                struct cmi_test_outcome *result);

/* Return a text string with an interpretation of a sigma value. */
extern const char *cmi_test_interpretation(double sigma);

/* Print a short report of a test outcome */
extern void cmi_test_outcome_print(struct cmi_test_outcome *r, FILE *fp);

/* Utility: Log of the incomplete gamma function */
extern void cmi_test_log_incomplete_gamma(double a, double x,
                                          double *logp, double *logq);
/* Utility: Log of the incomplete beta function */
extern void cmi_test_log_incomplete_beta(double a, double b, double x,
                                          double *logp, double *logq);

/* Utility: Print n characters by repeating the string str, ending with a newline */
extern void cmi_test_fnprint_line(FILE *fp, const char *str, const unsigned n);

CMB_MAYBE_UNUSED
static inline void cmi_test_print_line(const char *str)
{
    cmb_assert_release(str != NULL);

    cmi_test_fnprint_line(stdout, str, 80u);
}



#endif /* CIMBA_TESTUTILS_H */