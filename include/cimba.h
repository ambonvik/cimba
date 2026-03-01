/**
 * @file cimba.h
 * @brief The top level header file for the Cimba discrete event simulation
 * library, declaring the version, the prototype for a trial function, and the
 * function to execute an experiment in parallel on multiple CPU cores.
 * Includes all other Cimba header files, a user application only needs to
 * `#include "cimba.h"`
 */

/*
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025-26.
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

#ifndef CIMBA_CIMBA_H
#define CIMBA_CIMBA_H

/** \cond */
#define CIMBA_VERSION_MAJOR 3
#define CIMBA_VERSION_MINOR 0
#define CIMBA_VERSION_PATCH 0
#define CIMBA_VERSION_PRE_RELEASE beta

#define CMI_STRINGIFY(x) #x

#define CMI_TOSTRING(A, B, C, D) CMI_STRINGIFY(A) "." CMI_STRINGIFY(B) "." \
                                 CMI_STRINGIFY(C) "-" CMI_STRINGIFY(D)

#define CIMBA_VERSION CMI_TOSTRING(CIMBA_VERSION_MAJOR, \
                                   CIMBA_VERSION_MINOR, \
                                   CIMBA_VERSION_PATCH, \
                                   CIMBA_VERSION_PRE_RELEASE)
/** \endcond */

/**
 * @brief Returns a version string in printable format.
 */
extern const char *cimba_version(void);

/*
 * Declarations of the different "classes" and functions provided by Cimba
 */
#include "cmb_assert.h"
#include "cmb_buffer.h"
#include "cmb_condition.h"
#include "cmb_dataset.h"
#include "cmb_datasummary.h"
#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_objectqueue.h"
#include "cmb_process.h"
#include "cmb_random.h"
#include "cmb_resource.h"
#include "cmb_resourceguard.h"
#include "cmb_resourcepool.h"
#include "cmb_timeseries.h"
#include "cmb_wtdsummary.h"

/**
 * @brief Defines a prototype for the user-implemented function to execute a single
 * trial of the experiment.
 *
 * Your simulated universe lives inside this function, using the tools provided
 * by Cimba. The argument points to a user-defined trial struct containing the
 * parameters to and the results from the trial. It is defined as a `void*` here
 * since we do not know what your struct will contain. The trial function does
 * not return a value but stores the results in the same struct as the
 * parameters.
 *
 * This function will be executed in parallel with other instances of itself in
 * a shared memory space. Do _not_ use writeable global variables to share data
 * between functions inside your simulated world. Do _not_ use static local
 * variables to remember values between calls to some function. If you do, it
 * will easily lead to undefined behavior when different threads execute in
 * parallel in the same memory space, reading and writing the variable values in
 * unpredictable sequences. Using normal (auto) local variables and function
 * arguments is safe.
 *
 * If you absolutely must have a global or static variable to be shared between
 * function calls, declare it `CMB_THREAD_LOCAL` to keep it local to that
 * simulation thread (but it may still be shared across successive trials that
 * happen to share the same worker thread, with possibly unexpected results
 * unless you are careful in initializing and clearing the variable in each
 * trial). Alternatively, put a `mutex` on it, or other methods to make it
 * thread safe.
 */
typedef void (cimba_trial_func)(void *trial_struct);

/**
 * @brief The main simulation function, executing a user-defined experiment
 * consisting of several trials in parallel.
 *
 * The experiment is an array of your trial structs, containing any combination
 * of parameter variations and replications that you need. The trial struct
 * stores the parameters going into each trial and the results coming from it.
 *
 * The run will call your trial function once for each member of your array,
 * executing in parallel on as many CPU cores as the computer has available.
 * It also needs to know the number of trials in your experiment and the size
 * of your trial struct to do the necessary pointer calculations.
 *
 * Your trial function is responsible for setting up the simulation from
 * parameters given in the trial struct, starting it (typically by calling
 * `cmb_event_queue_execute()`, collecting the results, and storing them back to
 * the trial struct. Note that no end time is given as an argument here. You
 * need to determine the appropriate closing time and schedule an event for that
 * inside your simulation. See `test/test_cimba.c` for an example.
 *
 * When `cimba_run_experiment()` returns, the results fields of the trial
 * structs that constitute your experiment array will be filled in.
 *
 * In some cases, different trial functions may be needed for individual trials
 * of the experiment. To run multiple trial functions in parallel, set the
 * `your_trial_func` argument to `NULL` and store the trial function to use as
 * the first member of each trial struct in the experiment. That way, you can
 * run different simulations for each trial in your experiment if required.
 *
 * It is also possible to (ab)use `cimba_run_experiment()` to parallelize other
 * functions than Cimba simulations. The trial function can be any function
 * that matches the pattern `void func(void *arg)`, effectively using
 * `cimba_run_experiment()` as a user-friendly pthreads wrapper to parallelize
 * any CPU-bound function with limited input and output requirements.
 *
 * @param your_experiment_array Your user-defined array of trials to be run.
 * @param num_trials The number of trials in the array.
 * @param trial_struct_size The size of your trial struct in bytes.
 * @param your_trial_func Pointer to the trial function to be executed. If NULL,
 *                        the first member of each trial struct will be assumed
 *                        to be a pointer to the trial function to be executed
 *                        for that particular trial.
 */
extern void cimba_run_experiment(void *your_experiment_array,
                                 uint64_t num_trials,
                                 size_t trial_struct_size,
                                 cimba_trial_func *your_trial_func);

/**
* @brief Defines a prototype for an optional user-provided function to execute
* when initializing a phtread. The first argument is whatever the user code wants
* it to be, the second argument is the thread id assigned by cimba_run_experiment,
* an unsigned 64-bit integer in the range from 0 to the number of logical cores
* on the CPU minus one.
*/
typedef void *(cimba_thread_init_func)(void *usrarg, uint64_t tid);

/**
* @brief Defines a prototype for an optional user-provided function to execute
* when terminating a phtread. The argument is a thread context created by a
* previous thread init function. THis context can also obtained in user code
* by calling cimba_thread_context()
*/
typedef void (cimba_thread_exit_func)(void *thrctx);

/**
 * @brief Set the user-defined thread initialization and termination functions,
 *        with the argument to be given to the former. The init function will be
 *        called at the start of each new Cimba pthread. Used e.g. to determine
 *        CUDA stream parameters for the pthread, using the provided usrarg and
 *        the thread (i.e. core) identifier.
 *
 *        The init function's exit value will be stored as a thread local
 *        variable and can be accessed from inside the pthread by calling
 *        cimba_thread_context().
 *
 *        Before exiting the thread, the given exit function will be called with
 *        the context generated by the init function as its argument.-
 *
 * @param initfunc A function to be called when starting a new pthread
 * @param usrarg An argument to be passed to the initfunc
 * @param exitfunc A function to be called before exiting a pthread
 */
extern void cimba_set_thread_hooks(cimba_thread_init_func *initfunc,
                                   void *usrarg,
                                   cimba_thread_exit_func *exitfunc);

/**
* @brief Access the thread context if one exists. Used e.g. for storing CUDA
*        stream info for this thread.
*
* @return Thread context previously created by a thread init function,
*           possibly NULL if no such function has been called.
*/
extern void *cimba_thread_context(void);


#endif // CIMBA_CIMBA_H
