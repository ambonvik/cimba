/**
 * @file cimba.h
 * @brief The top level header file for the Cimba discrete event simulation
 * library.
 *
 * Defines the data types and functions for executing a simulation in
 * parallel on the available CPU cores. Includes all other Cimba header files,
 * a user application only needs to `#include "cimba.h"`
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

#ifndef CIMBA_CIMBA_H
#define CIMBA_CIMBA_H

#define CIMBA_VERSION_MAJOR 3
#define CIMBA_VERSION_MINOR 0
#define CIMBA_VERSION_PATCH 0
#define CIMBA_VERSION_PRE_RELEASE alpha

/** \cond */
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
 * Definitions of the different "classes" and functions provided by Cimba
 */
#include "cmb_assert.h"
#include "cmb_buffer.h"
#include "cmb_dataset.h"
#include "cmb_datasummary.h"
#include "cmb_event.h"
#include "cmb_logger.h"
#include "cmb_mempool.h"
#include "cmb_objectqueue.h"
#include "cmb_process.h"
#include "cmb_random.h"
#include "cmb_resource.h"
#include "cmb_store.h"
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
 * not return a value, but stores the results in the same struct as the
 * parameters.
 *
 * This function will be executed in parallel with other instances of itself in
 * a shared memory space. Do not use writeable global variables to share data
 * between functions inside the simulated world, or static local variables to
 * remember values between calls to some function without being very careful in
 * handling concurrency. It will easily lead to undefined behavior when
 * different threads execute in parallel in the same memory space. Using normal
 * local variables and function arguments is safe. If you absolutely must have a
 * global or static variable to be shared between function calls, consider
 * declaring it `CMB_THREAD_LOCAL` to keep it local to that simulation thread
 * (but it may still be shared across successive trials that happen to share the
 * same worker thread, with possibly unexpected results).
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
 * parameters given in the trial struct, start it (typically by calling
 * `cmb_event_queue_execute()`, collecting the results, and storing them back to
 * the trial struct. Note that no end time is given as an argument here. You
 * need to determine the appropriate closing time and schedule an event for that
 * inside your simulation. See `test/test_cimba.c` for an example.
 *
 * When cimba_run_experiment returns, the results fields of the trial structs
 * that constitute your experiment array will be filled in.
 *
 * In some cases, different trial functions may be needed for individual trials
 * of the experiment. To run multiple trial functions in parallel, set the
 * `your_trial_func` argument to `NULL` and store the trial function to use as
 * the first member of each trial struct in the experiment. That way, you can
 * run different simulations for each trial in your experiment if required.
 *
 * It is also possible to (ab)use cimba_run_experiment to parallelize other
 * functions than Cimba simulations. The trial function can be any function
 * that matches the pattern `void func(void *arg)`, effectively using
 * cimba_run_experiment as a user-friendly wrapper to parallelize any CPU-bound
 * function with limited input and output requirements.
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

#endif // CIMBA_CIMBA_H
