/*
 * Test script for dataset collection and reporting.
 *
 * Uses the uniform random number distribution from cmb_random as test object.
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
#include <stdio.h>
#include <time.h>

#include "cmb_data.h"
#include "cmb_random.h"

static const uint32_t max_iter = 1000000u;
static const uint32_t max_lag = 25;

static uint64_t create_seed(void) {
   struct timespec ts;
   (void) clock_gettime(CLOCK_REALTIME, &ts);

   return (uint64_t)(ts.tv_nsec ^ ts.tv_sec);
}

static void test_summary(void)
{
   const uint64_t seed = create_seed();
   cmb_random_init(seed);

   printf("Testing data summaries\n");
   printf("Local variable data summary on stack\n");
   struct cmb_summary ds;
   cmb_summary_init(&ds);

   printf("Drawing a million U(0,1) samples...\n");
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double x = cmb_random();
      cmb_summary_add(&ds, x);
   }

   printf("Count:\t%llu\n", cmb_summary_count(&ds));
   printf("Min:\t%g\n", cmb_summary_min(&ds));
   printf("Max:\t%g\n", cmb_summary_max(&ds));
   printf("Mean:\t%g\t(expected %g)\n", cmb_summary_mean(&ds), 0.5);
   printf("Var:\t%g\t(expected %g)\n", cmb_summary_variance(&ds), 1.0/12.0);
   printf("StdDev:\t%g\t(expected %g)\n", cmb_summary_stddev(&ds), sqrt(1.0/12.0));
   printf("Skewness:\t%g\t(expected %g)\n", cmb_summary_skewness(&ds), 0.0);
   printf("Kurtosis:\t%g\t(expected %g)\n", cmb_summary_kurtosis(&ds), - 6.0 / 5.0);

   printf("\nSummary:\n");
   cmb_summary_print(&ds, stdout, true);
   printf("Summary without lead-ins:\n");
   cmb_summary_print(&ds, stdout, false);

   printf("Once more, now on the heap\n");
   struct cmb_summary *dsp = cmb_summary_create();

   printf("Drawing a million U(1,2) samples...\n");
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double x = cmb_random_uniform(1.0, 2.0);
      cmb_summary_add(dsp, x);
   }

   cmb_summary_print(dsp, stdout, true);
   printf("Merging the data summaries... ");
   const uint64_t nn = cmb_summary_merge(dsp, dsp, &ds);
   printf("got %llu samples\n", nn);
   cmb_summary_print(dsp, stdout, true);

   cmb_summary_clear(dsp);
   cmb_summary_destroy(dsp);
}

static void test_wsummary(void)
{
   const uint64_t seed = create_seed();
   cmb_random_init(seed);

   printf("Testing weighted data summaries\n");
   printf("Weighted and unweighted in parallel, all weights set to 1.0\n");
   struct cmb_summary ds;
   cmb_summary_init(&ds);
   struct cmb_wsummary dws;
   cmb_wsummary_init(&dws);

   printf("Drawing a million U(0,1) samples...\n");
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double x = cmb_random();
      cmb_summary_add(&ds, x);
      cmb_wsummary_add(&dws, x, 1.0);
   }

   printf("\t\tUnweighted\tWeighted\tExpected:\n");
   printf("Count:   \t%llu \t%llu \t%u\n", cmb_summary_count(&ds), cmb_wsummary_count(&dws), max_iter);
   printf("Minimum: \t%g\t%g\t%g\n", cmb_summary_min(&ds), cmb_wsummary_min(&dws), 0.0);
   printf("Maximum: \t%g\t%g\t%g\n", cmb_summary_max(&ds), cmb_wsummary_max(&dws), 1.0);
   printf("Mean:    \t%g\t%g\t%g\n", cmb_summary_mean(&ds), cmb_wsummary_mean(&dws), 0.5);
   printf("Variance:\t%g\t%g\t%g\n", cmb_summary_variance(&ds), cmb_wsummary_variance(&dws), 1.0/12.0);
   printf("StdDev:  \t%g\t%g\t%g\n", cmb_summary_stddev(&ds), cmb_wsummary_stddev(&dws), sqrt(1.0/12.0));
   printf("Skewness:\t%g\t%g\t%g\n", cmb_summary_skewness(&ds), cmb_wsummary_skewness(&dws), 0.0);
   printf("Kurtosis:\t%g\t%g\t%g\n", cmb_summary_kurtosis(&ds), cmb_wsummary_kurtosis(&dws), - 6.0 / 5.0);

   printf("\nSummary:\n");
   cmb_wsummary_print(&dws, stdout, true);
   printf("Summary without lead-ins:\n");
   cmb_wsummary_print(&dws, stdout, false);

   cmb_summary_clear(&ds);
   cmb_wsummary_clear(&dws);

   printf("Drawing a million U(0,1) samples, randomly weighted on U(1,5)...\n");
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double x = cmb_random();
      const double w = cmb_random_uniform(1.0, 5.0);
      cmb_wsummary_add(&dws, x, w);
   }

   printf("Sum of weights: %g\n", dws.wsum);
   cmb_wsummary_print(&dws, stdout, true);

   printf("Creating another weighted data summary...\n");
   struct cmb_wsummary *dwp = cmb_wsummary_create();
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double x = cmb_random();
      const double w = cmb_random_uniform(1.0, 5.0);
      cmb_wsummary_add(dwp, x, w);
   }

   cmb_wsummary_print(dwp, stdout, true);

   printf("Merging the two...\n");
   const uint64_t nm = cmb_wsummary_merge(dwp, dwp, &dws);
   printf("nm = %llu\n", nm);
   cmb_wsummary_print(dwp, stdout, true);
   cmb_wsummary_print(&dws, stdout, true);
}

void test_dataset() {
   uint64_t seed = create_seed();
   cmb_random_init(seed);

   printf("\nTesting datasets\n");
   printf("Local variable dataset on stack\n");

   struct cmb_dataset ds = { 0 };
   cmb_dataset_init(&ds);

   printf("Drawing a million U(0,1) samples...\n");
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double x = cmb_random();
      cmb_dataset_add(&ds, x);
   }

   printf("Making a copy...\n");
   struct cmb_dataset dsc = { 0 };
   const uint64_t un = cmb_dataset_copy(&dsc, &ds);
   printf("Returned %llu\n", un);
   printf("Sorting the copy...\n");
   cmb_dataset_sort(&dsc);
   printf("Clearing it...\n");
   cmb_dataset_clear(&dsc);

   printf("Count:\t%llu\n", cmb_dataset_count(&ds));
   printf("Min:\t%g\n", cmb_dataset_min(&ds));
   printf("Max:\t%g\n", cmb_dataset_max(&ds));
   printf("Median:\t%g\n", cmb_dataset_median(&ds));

   printf("Five number summary:\n");
   cmb_dataset_print_fivenum(&ds, stdout, true);
   struct cmb_summary dsum = { 0 };
   const uint64_t um = cmb_dataset_summarize(&ds, &dsum);
   printf("cmb_dataset_summarize returned %llu\n", um);
   cmb_summary_print(&dsum, stdout, true);
   cmb_dataset_print_histogram(&ds, stdout, 20u, 0.0, 0.0);

   printf("\nAutocorrelation coefficients:\n");
   double acf[max_lag + 1];
   cmb_dataset_ACF(&ds, max_lag, acf);
   cmb_dataset_print_correlogram(&ds, stdout, max_lag, acf);

   printf("\nPartial autocorrelation coefficients:\n");
   double pacf[max_lag + 1];
   cmb_dataset_PACF(&ds, max_lag, pacf, acf);
   cmb_dataset_print_correlogram(&ds, stdout, max_lag, pacf);

   printf("\nCreating a new dataset, filling it with noisy sine curves...\n");
   struct cmb_dataset *dsp = cmb_dataset_create();
   for (uint32_t ui = 0; ui < max_iter; ui++) {
      const double amp_noise = 0.5;
      const double period = 10.0;
      const double amp_signal = 2.0;
      const double x = amp_signal * sin(2.0 * M_PI * (double)ui / period)
                       + cmb_random_normal(0.0, amp_noise);
      cmb_dataset_add(dsp, x);
   }

   cmb_summary_clear(&dsum);
   (void)cmb_dataset_summarize(dsp, &dsum);
   cmb_summary_print(&dsum, stdout, true);
   cmb_dataset_print_histogram(dsp, stdout, 20u, 0, 0);

   printf("\nAutocorrelation coefficients:\n");
   cmb_dataset_ACF(dsp, max_lag, acf);
   cmb_dataset_print_correlogram(dsp, stdout, max_lag, acf);

   printf("\nPartial autocorrelation coefficients:\n");
   cmb_dataset_PACF(dsp, max_lag, pacf, acf);
   cmb_dataset_print_correlogram(dsp, stdout, max_lag, pacf);

   cmb_summary_clear(&dsum);
   cmb_dataset_destroy(dsp);

   /* TODO: dataset_create/destroy, dataset_merge, dataset_correlogram */
}

int main(void) {
   test_summary();
   test_wsummary();
   test_dataset();

   /* TODO: timeseries */

   return 0;
}