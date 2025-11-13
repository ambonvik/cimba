/*
 * cimba.c - the top level simulation execution.
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

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cimba.h"

#include "cmi_memutils.h"

/* Only used from here, no header file needed */
extern uint32_t cmi_cpu_cores(void);

/* Global control variables shared by all threads */
static uint64_t cmg_next_trial_idx;
static void *cmg_experiment_arr;
static size_t cmg_trial_struct_sz;
static cimba_trial_func *cmg_trial_func;
static sem_t cmg_completed_sem;
static uint64_t cmg_total_trials;
static pthread_mutex_t cmg_terminal;

static void *worker_thread_func(void *arg)
{
    cmb_unused(arg);

    while (true) {
        /* stdatomic.h broken on Windows, using gcc/clang intrinsic instead for now */
        uint64_t idx = __atomic_fetch_add(&cmg_next_trial_idx, 1, __ATOMIC_SEQ_CST);
        if (idx >= cmg_total_trials) {
            break;
        }

        void *trial = ((char *)cmg_experiment_arr) + (idx * cmg_trial_struct_sz);

        if (cmg_trial_func != NULL) {
            /* Normal usage, a common function, multiple data */
            (*cmg_trial_func)(trial);
        }
        else {
            /* No common function, extracting the function to use for this trial */
            cimba_trial_func *trial_func = (cimba_trial_func *)(((char *)trial) + cmg_trial_struct_sz);
            (*trial_func)(trial);
        }

        pthread_mutex_lock(&cmg_terminal);
        printf("\rCompleted %llu/%llu", idx, cmg_total_trials);
        sem_post(&cmg_completed_sem);
        pthread_mutex_unlock(&cmg_terminal);
    }

    return NULL;
}

void cimba_run_experiment(void *your_experiment_array,
                          uint64_t num_trials,
                          size_t trial_struct_size,
                          cimba_trial_func *your_trial_func)
{
    cmb_assert_release(your_experiment_array != NULL);
    cmb_assert_release(num_trials > 0u);
    cmb_assert_release(trial_struct_size > 0u);
    cmb_assert_release(your_trial_func != NULL);

    /* Initialize globals for the threads */
    cmg_next_trial_idx = 0u;
    cmg_experiment_arr = your_experiment_array;
    cmg_trial_struct_sz = trial_struct_size;
    cmg_trial_func = your_trial_func;
    cmg_total_trials = num_trials;
    sem_init(&cmg_completed_sem, 0, 0);
    pthread_mutex_init(&cmg_terminal, NULL);

    /* Start the worker threads and let them help themselves to the trials */
    const uint32_t ncores = cmi_cpu_cores();
    pthread_t *threads = cmi_calloc(ncores, sizeof(*threads));
    for (uint64_t ui = 0u; ui < ncores; ui++) {
        pthread_create(&threads[ui], NULL, worker_thread_func, (void *)ui);
    }

    /* A lot of stuff happens in parallel here, wait for it all to finish */
    for (uint64_t ui = 0u; ui < ncores; ui++) {
        pthread_join(threads[ui], NULL);
    }

    cmi_free(threads);
}



