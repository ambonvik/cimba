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
    CMI_TEST_GOF_CONTINUOUS,
    CMI_TEST_GOF_DISCRETE
};

enum cmi_test_status {
    CMI_TEST_OK = 0,
    CMI_TEST_TOO_FEW,        /* Not enough samples per bin */
    CMI_TEST_OUT_OF_RANGE,   /* Sample value(s) outside support */
    CMI_TEST_INVALID_VALUE,  /* Invalid sample values(s) found, e.g, fractional values in an integer distribution */
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

#define CMI_TEST_PARTS 10
struct cmi_test_outcome {
    enum cmi_test_type type;
    enum cmi_test_status status;
    uint64_t n;
    double min;
    double max;
    double combined_lp;
    double combined_sigma;
    uint64_t n_clamped;     /* A-D: Number of samples on exact edge values */
    unsigned n_bins;        /* Neyman: Number of bins after lumping */
    unsigned k_eff;         /* Neyman: Effective K after binning and lumping */
    unsigned nparts;
    struct cmi_test_partial p[CMI_TEST_PARTS];
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
 * Goodness-of-fit test for discrete-valued distributions: Is the dataset
 * distributed according to the PMF? Returns a sigma value (standard deviations
 * of the standard normal distribution) where a high absolute value of sigma
 * indicates improbability and the sign the direction from the expected value.
 */
extern double cmi_test_gof_disc(uint64_t m,
                                const double pmf_vec[m + 2],
                                const double val_vec[m + 2],
                                const struct cmb_dataset *dsp,
                                struct cmi_test_outcome *result);

/* Return a text string with an interpretation of a sigma value.
 * If the arg `comment` is `true`, also includes a "Unremarkable" string for
 * insignificant results.  */
extern const char *cmi_test_interpretation(double sigma, bool comment);

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

    cmi_test_fnprint_line(stdout, str, 120u);
}



#endif /* CIMBA_TESTUTILS_H */