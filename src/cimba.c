/*
 * cimba.c - the top level simulation execution.
 *
 * Encapsulates the details of setting up and executing pthreads worker threads
 * to execute the experiments specified by the user. We first create a number of
 * worker threads equal to the number of logical cores on the machine, then let
 * these pull and execute trials from the experiment array.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-2026.
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
#include <setjmp.h>
#include <stdint.h>

#include "cimba.h"

#include "cmi_coroutine.h"
#include "cmi_mempool.h"
#include "cmi_memutils.h"

/* Only used from here, no header file needed */
extern uint32_t cmi_cpu_cores(void);

/*
 * Global control variables shared by all threads but static to this file
 */
static void *cmg_experiment_arr;
static size_t cmg_trial_struct_sz;
static cimba_trial_func *cmg_trial_func = NULL;
static uint64_t cmg_total_trials;
static uint32_t cmg_worker_threads = 0u;
static cimba_thread_init_func *cmg_thread_init_func = NULL;
static void *cmg_thread_init_usrarg = NULL;
static cimba_thread_exit_func *cmg_thread_exit_func = NULL;
static pthread_mutex_t cmg_experiment_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Using GCC/Clang __atomic built-in rather than C11 stdatomic.h due to clangd
 * false positives that have no clean workaround as of early 2026. Hence
 * declaring cmg_next_trial_idx as plain static uint64_t instead of _Atomic */
static uint64_t cmg_next_trial_idx;

/* User-defined context per thread */
CMB_THREAD_LOCAL void *cmi_thread_context = NULL;

/* Index of the current thread, if any */
CMB_THREAD_LOCAL uint64_t cmi_thread_id = UINT64_C(0);

/* For recovering from trial-ending cmb_logger_error calls */
CMB_THREAD_LOCAL jmp_buf cmi_worker_recovery;
CMB_THREAD_LOCAL bool cmi_worker_recovery_armed = false;
static uint64_t cmi_failed_trials;

/*
 * cimba_version - Return the version string as const char *
 */
const char *cimba_version(void)
{
    return CIMBA_VERSION;
}

/* Set the initialization callback function */
void cimba_thread_hooks_set(cimba_thread_init_func *initfunc,
                            void *usrarg,
                            cimba_thread_exit_func *exitfunc)
{
    cmg_thread_init_func = initfunc;
    cmg_thread_init_usrarg = usrarg;
    cmg_thread_exit_func = exitfunc;
}

void *cimba_thread_context(void)
{
    return cmi_thread_context;
}

uint64_t cimba_thread_id(void)
{
    return cmi_thread_id;
}

uint32_t cimba_threads_num(void)
{
    const uint32_t r = (cmg_worker_threads == 0u) ? cmi_cpu_cores()
                                                  : cmg_worker_threads;
    cmb_assert_debug(r >= 1u);

    return r;
}

uint32_t cimba_threads_use(uint32_t n_threads)
{
    cmg_worker_threads = n_threads;

    const uint32_t r = (cmg_worker_threads == 0u) ? cmi_cpu_cores()
                                                  : cmg_worker_threads;
    cmb_assert_debug(r >= 1u);

    return r;
}

uint64_t cimba_trials_total(void)
{
    return cmg_total_trials;
}

uint64_t cimba_trial_index(void)
{
    return cmi_logger_trial_idx;
}

/*
 * Assume that all workers are busy if there is anything to do
 */
uint64_t cimba_trials_remaining(void)
{
    const uint64_t nxt = __atomic_load_n(&cmg_next_trial_idx, __ATOMIC_RELAXED);

    return cmg_total_trials - nxt;
}

/*
 * thread_exit_wrapper - Internal function to simplify conditional pthread_cleanup_push
 * with its strange unbalanced braces and other weirdness. It is cleaner like this.
 */
static void thread_exit_wrapper(void *context)
{
    if (cmg_thread_exit_func != NULL) {
        cmg_thread_exit_func(context);
    }
}

/*
 * worker_thread_func - The function passed to pthread_create. It finds the next
 * available trial from the experiment array, executes it, and repeats. If no
 * more trials are waiting, it exits. An atomic uint64_t is used to track the
 * number of remaining trials.
 */
static void *worker_thread_func(void *arg)
{
    const uint64_t tid = (uint64_t)arg;
    cmi_thread_id = tid;

    /* Any user-defined initialization needed? */
    if (cmg_thread_init_func != NULL) {
        cmi_thread_context = cmg_thread_init_func(tid, cmg_thread_init_usrarg);
    }

    /* Make sure we free any thread local allocations before we exit */
    pthread_cleanup_push(cmi_mempool_cleanup, NULL);
    pthread_cleanup_push(cmi_coroutine_thread_cleanup, NULL);

    /* Any user-defined cleanup needed? */
    pthread_cleanup_push(thread_exit_wrapper, cmi_thread_context);

    while (true) {
        /* Using GCC/Clang __atomic built-in rather than C11 stdatomic.h due to
         * clangd false positives with no clean workaround */
        const uint64_t idx = __atomic_fetch_add(&cmg_next_trial_idx, 1, __ATOMIC_RELAXED);
        if (idx >= cmg_total_trials) {
            break;
        }

        void *trial = ((char *)cmg_experiment_arr) + (idx * cmg_trial_struct_sz);
        cmi_logger_trial_idx = idx;

        cmi_worker_recovery_armed = true;
        if (setjmp(cmi_worker_recovery) == 0) {
            if (cmg_trial_func != NULL) {
                /* Normal usage, a common function, multiple data */
                (*cmg_trial_func)(trial);
            }
            else {
                /* No common function, extracting function to use for this trial */
                cimba_trial_func *trial_func = *(cimba_trial_func **)trial;
                (*trial_func)(trial);
            }
        }
        else {
            /* The trial called cmb_logger_error() and bailed out, increment counter */
            (void)__atomic_fetch_add(&cmi_failed_trials, 1, __ATOMIC_RELAXED);
            /*
             * Note that no attempt at memory cleanup is done here. Anything allocated
             * by the trial is now leaked memory if it did not free it before
             * calling cmb_logger_error()
             */
        }

        cmi_worker_recovery_armed = false;
     }

    /* Made it this far, execute the cleanup functions before exiting */
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);

    return NULL;
}

/*
 * cimba_experiment_run - The main simulation executive function. Initiates the
 * worker threads and waits for them to finish. That's all.
 *
 * The intended use case is to have only one instance of this function running
 * at a time, while the individual trials are multithreaded below it. However,
 * we'll put in a mutex to protect it against hard-to-debug consequences of
 * unintentional misuse.
 */
uint64_t cimba_run(void *your_experiment_array,
                   const uint64_t num_trials,
                   const size_t trial_struct_size,
                   cimba_trial_func *your_trial_func)
{
    cmb_assert_release(your_experiment_array != NULL);
    cmb_assert_release(num_trials > 0u);
    cmb_assert_release(trial_struct_size > 0u);

    /* A mutex to make sure the Cimba globals are protected */
    pthread_mutex_lock(&cmg_experiment_mutex);

    /* Initialize globals for the threads */
    cmg_next_trial_idx = 0u;
    cmg_experiment_arr = your_experiment_array;
    cmg_trial_struct_sz = trial_struct_size;
    cmg_trial_func = your_trial_func;
    cmg_total_trials = num_trials;
    cmi_failed_trials = 0u;

    /* Start the worker threads and let them help themselves to the trials */
    const uint32_t nthreads = (cmg_worker_threads == 0u) ? cmi_cpu_cores() : cmg_worker_threads;
    pthread_t *threads = cmi_calloc(nthreads, sizeof(*threads));
    for (uint64_t ui = 0u; ui < nthreads; ui++) {
        pthread_create(&threads[ui], NULL, worker_thread_func, (void *)ui);
    }

    /* ...worker threads are executing your trials in the background here... */

    /* Wait for all worker threads to finish */
    for (uint64_t ui = 0u; ui < nthreads; ui++) {
        pthread_join(threads[ui], NULL);
    }

    cmi_free(threads);

    /* Only unlock when all is said and done */
    pthread_mutex_unlock(&cmg_experiment_mutex);

    return cmi_failed_trials;
}
