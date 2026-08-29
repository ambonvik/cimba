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

#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>

#include "cimba.h"

#include "cmi_coroutine.h"
#include "cmi_mempool.h"
#include "cmi_memutils.h"
#include "cmi_thread.h"
#include "cmi_sanitizer.h"

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
static pthread_t cmg_main_thread;

/* Using GCC/Clang __atomic built-in rather than C11 stdatomic.h due to clangd
 * false positives that have no clean workaround as of early 2026. Hence
 * declaring cmg_next_trial_idx as plain static uint64_t instead of _Atomic */
static uint64_t cmg_next_trial_idx;

/* Global control variable to ensure that atexit() gets armed only once.
 * Intentionally external scope, hence `cmg_` namespace. */
pthread_once_t cmg_atexit_armed = PTHREAD_ONCE_INIT;

/* User-defined context per thread */
CMB_THREAD_LOCAL void *cmi_thread_context = NULL;

/* Index of the current thread, if any */
CMB_THREAD_LOCAL uint64_t cmi_thread_id = UINT64_C(0);

/* For recovering from trial-ending cmb_logger_error calls */
CMB_THREAD_LOCAL bool cmi_recovery_armed = false;
static uint64_t failed_trials;
static uint64_t leaked_objects;
static uint64_t leaking_trials;

static CMB_THREAD_LOCAL cimba_trial_cleanup_func *trial_cleanup_func = NULL;
static CMB_THREAD_LOCAL void *trial_cleanup_arg = NULL;

static CMB_THREAD_LOCAL intptr_t recovery_buf[5];
#define recovery_set()   __builtin_setjmp((void **)recovery_buf)
#define recovery_jump()  __builtin_longjmp((void **)recovery_buf, 1)

extern void cmi_coroutine_recovery_prepare(void *stack_marker);
extern void cmi_coroutine_recovery_finalize(void *stack_marker);

extern void cmi_coroutine_thread_cleanup(void);
extern void cmi_event_thread_cleanup(void);
extern void cmi_hashheap_thread_cleanup(void);
extern void cmi_mempool_thread_cleanup(void);

/*
 * This function will run _before_ the start of main(), guaranteed before any
 * other pthread launches. It catches the id of the main thread for later
 * comparison to determine if some code is running in the main thread or
 * in some other pthread.
 */
__attribute__((constructor))
static void thread_capture_main(void)
{
    cmg_main_thread = pthread_self();
}

/* Predicate to distinguish between main thread and worker threads */
bool cmi_thread_in_main(void) {
    return pthread_equal(pthread_self(), cmg_main_thread);
}

/* Call signature as expected by pthread_cleanup_push().
 * Will run on normal exit from a pthread.
 */
static void thread_pthread_cleanup(void *arg)
{
    cmb_unused(arg);

    /* The sequence is important here, mempools last */
    cmi_hashheap_thread_cleanup();
    cmi_event_thread_cleanup();
    cmi_coroutine_thread_cleanup();
    cmi_mempool_thread_cleanup();
}

/* Call signature as expected by atexit(). Will run on program exit.
 * Makes sure it only runs on the main stack, not on some coroutine stack.
 * If it did, we would be free'ing the stack we are running on, heading into
 * highly undefined behaviour. We are exiting anyway, so the OS will reclaim
 * all memory in a moment even with no cleanup done from our side. No damage
 * done by skipping it.
 */
static void thread_main_cleanup(void)
{
    if (cmi_coroutine_current() == cmi_coroutine_main()) {
        cmi_hashheap_thread_cleanup();
        cmi_event_thread_cleanup();
        cmi_coroutine_thread_cleanup();
        cmi_mempool_thread_cleanup();
    }
}

void cmi_thread_arm_atexit_cleanup(void)
{
    const int rc = atexit(thread_main_cleanup);
    cmb_assert_always(rc == 0);
}

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
    const uint32_t n_threads = __atomic_load_n(&cmg_worker_threads, __ATOMIC_RELAXED);
    const uint32_t r = (n_threads == 0u) ? cmi_cpu_cores() : n_threads;
    cmb_assert_debug(r >= 1u);

    return r;
}

uint32_t cimba_threads_use(const uint32_t n_threads)
{
    __atomic_store_n(&cmg_worker_threads, n_threads, __ATOMIC_RELAXED);

    const uint32_t r = (n_threads == 0u) ? cmi_cpu_cores() : n_threads;
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

    return (nxt >= cmg_total_trials) ? 0u : cmg_total_trials - nxt;
}

/*
 * Set cleanup function. Note that both arguments can be NULL.
 */
void cimba_trial_cleanup_set(cimba_trial_cleanup_func *clufunc, void *usrarg)
{
    trial_cleanup_func = clufunc;
    trial_cleanup_arg = usrarg;
}

/*
 * Abandon the current trial via `longjmp` for `worker_thread_func` to recover.
 */
CMB_NORETURN
void cimba_trial_abandon(void)
{
    if (cmi_recovery_armed) {
        cmi_coroutine_recovery_prepare(NULL);
        recovery_jump();
    }
    else {
        /* Not running inside a Cimba worker thread — fall back to exit with
         * error code. Any armed cleanup functions from atexit() know to not
         * delete the stack we are currently running on, so this is safe even
         * from inside a coroutine without triggering undefined behavior.   */
        exit(EXIT_FAILURE);
    }

    /* Not reached */
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

/* Friendly function in cmb_event.c, not part of the public interface. Resets
 * this thread's event queue and clock after a trial abandons itself by longjmp,
 * the event-layer counterpart to cmi_coroutine_reset_to_main(). */
extern void cmi_event_queue_reset(void);

/*
 * thread_worker_func - The function passed to pthread_create. It finds the next
 * available trial from the experiment array, executes it, and repeats. If no
 * more trials are waiting, it exits. An atomic uint64_t is used to track the
 * number of remaining trials.
 */
static void *thread_worker_func(void *arg)
{
    const uint64_t tid = (uint64_t)arg;
    cmi_thread_id = tid;

    /* Any user-defined initialization needed? */
    if (cmg_thread_init_func != NULL) {
        cmi_thread_context = cmg_thread_init_func(tid, cmg_thread_init_usrarg);
    }

    /* Make sure we free any thread local allocations before we exit */
    pthread_cleanup_push(thread_pthread_cleanup, NULL);

    /* Any user-defined thread cleanup needed? */
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

        cmi_recovery_armed = true;
        if (recovery_set() == 0) {
            if (cmg_trial_func != NULL) {
                /* Normal usage, a common function, multiple data */
                (*cmg_trial_func)(trial);
            }
            else {
                /* No common function, extracting function to use for this trial */
                cimba_trial_func *trial_func = *(cimba_trial_func **)trial;
                (*trial_func)(trial);
            }

            /* Continuing after a normal exit from the trial function */
            if (!cmi_dlist_is_empty(&cmi_memregistry)) {
                /* Some cmb_ object was not properly terminated and/or
                 * destroyed during the trial, just flush registry without
                 * calling registered teardown functions - may be intentional
                 * from the user, do not override.    */
                (void)__atomic_fetch_add(&leaking_trials, 1, __ATOMIC_RELAXED);
                while (!cmi_dlist_is_empty(&cmi_memregistry)) {
                    (void)cmi_dlist_remove_first(&cmi_memregistry);
                    (void)__atomic_fetch_add(&leaked_objects, 1, __ATOMIC_RELAXED);
                }
            }
        }
        else {
            /* The trial abandoned via longjmp. That returned us to the main
             * thread stack from whatever coroutine was running. Restore this
             * thread's per-trial stack state bookkeeping, with the address of
             * a local variable in this stack frame as a high-water marker. */
            cmi_coroutine_recovery_finalize(&trial);

            /* Execute the user-defined trial cleanup function, if any */
            if (trial_cleanup_func != NULL) {
                (*trial_cleanup_func)(trial_cleanup_arg);
            }

            /* Execute all registered `cmb_` destructors in LIFO sequence */
            cmi_memregistry_cleanup();

            /* Increment the failure counter across all worker threads */
            (void)__atomic_fetch_add(&failed_trials, 1, __ATOMIC_RELAXED);
        }

        /* Reset the simulation clock and event queue. */
        cmi_event_queue_reset();

        /* Safely out of the trial, disarm the recovery trap */
        cmi_recovery_armed = false;
        trial_cleanup_func = NULL;
        trial_cleanup_arg = NULL;
     }

    /* No more trials, execute the thread cleanup functions before exiting */
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);

    return NULL;
}

/*
 * cimba_run - The main simulation executive function. Initiates the
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
    failed_trials = 0u;
    leaked_objects = 0u;
    leaking_trials = 0u;

    /* Start the worker threads and let them help themselves to the trials */
    const uint32_t nthreads = (cmg_worker_threads == 0u) ? cmi_cpu_cores() : cmg_worker_threads;
    pthread_t *threads = cmi_calloc(nthreads, sizeof(*threads));
    for (uint64_t ui = 0u; ui < nthreads; ui++) {
        /* A failure here will be fatal, so check */
        const int rc = pthread_create(&threads[ui], NULL, thread_worker_func, (void *)ui);
        cmb_assert_always(rc == 0);
    }

    /* ...worker threads are executing your trials in the background here... */

    /* Wait for all worker threads to finish */
    for (uint64_t ui = 0u; ui < nthreads; ui++) {
        /* We are about to exit anyway, so less critical, but could as well check. */
        const int rc = pthread_join(threads[ui], NULL);
        cmb_assert_always(rc == 0);
    }

    cmi_free(threads);


#ifndef NASSERT
    if (leaked_objects > 0u) {
        printf("cimba_run: Warning: Possible memory leaks detected, total of %"
                PRIu64 " objects leaked from %" PRIu64 " trials\n",
                leaked_objects, leaking_trials);
        printf("If not intentionally keeping objects in memory between trials,"
                " check for missing cmb_*_destroy() calls.\n");
    }
#endif

    /* Only unlock when all is said and done */
    pthread_mutex_unlock(&cmg_experiment_mutex);

    return failed_trials;
}
