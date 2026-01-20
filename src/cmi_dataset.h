/*
* cmi_dataset.h - internal header file for declaring functions shared between
* cmb_dataset and cmb_timeseries.
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

#ifndef CIMBA_CMI_DATASET_H
#define CIMBA_CMI_DATASET_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CMI_DATASET_INIT_SZ (1024u)

extern void cmi_dataset_swap(double *a, double *b);
extern void cmi_dataset_expand(struct cmb_dataset *dsp);

extern bool cmi_dataset_is_sorted(uint64_t un, const double arr[un]);
extern bool cmi_dataset_is_max_heap(uint64_t un,
                                    double const arr[un],
                                    uint64_t uroot);


/*
 * Histogram data structure. Note that the bins are real-valued (not integer) to
 * work both with traditional histograms for cmb_dataset and for time-weighted
 * ones for cmb_timeseries where each value is counted proportional to the time
 * interval between it and the next value. Internal use only, hence declared
 * here and not in the header file.
 */
struct cmi_dataset_histogram {
    unsigned num_bins;
    double binsize;
    double low_lim;
    double high_lim;
    double binmax;
    double *hbins;
};

extern struct cmi_dataset_histogram *cmi_dataset_create_histogram(unsigned num_bins,
                                                       double low_lim,
                                                       double high_lim);

extern void cmi_dataset_fill_histogram(struct cmi_dataset_histogram *hp,
                                       uint64_t n,
                                       const double xa[n]);

extern void cmi_dataset_print_histogram(const struct cmi_dataset_histogram *hp,
                                        FILE *fp);

extern void cmi_dataset_destroy_histogram(struct cmi_dataset_histogram *hp);

#endif /* CIMBA_CMI_DATASET_H */

