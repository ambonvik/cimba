/**
 * @file cmb_process.h
 * @brief The simulated processes, the active entities in the simulation,
 *        interacting with each other and with passive resources.
 *
 * The `cmb_process` is a named coroutine that works in the simulated time and
 * interacts with events off the simulation event queue. It has a name and a
 * priority, which can be changed later.
 *
 * The `cmb_process` can hold (deactivate itself for a certain interval of
 * simulated time) and wait for resources to become available. In those states,
 * it can also be interrupted by other processes. The interrupt passes a nonzero
 * value that appears as the return value from hold, to be used for
 * distinguishing between normal return vs. various (possibly user-defined)
 * interrupt types.
 *
 * In the same way as the interrupt call, the functions for starting and
 * stopping a process are non-blocking. The calling process will continue
 * immediately, until it explicitly transfers control to another process by
 * yield or resume. The actual transfer of control happens from a scheduled
 * event, defined in `cmb_process.c`.
 */

/*
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
#ifndef CIMBA_CMB_PROCESS_H
#define CIMBA_CMB_PROCESS_H

#include "cmb_assert.h"
#include "cmb_event.h"

#include "cmi_coroutine.h"
#include "cmi_list.h"
#include "cmi_waitable.h"

/**
 * @brief Maximum length of a process name, anything longer will be truncated
 */
#define CMB_PROCESS_NAMEBUF_SZ 32

/**
 * @brief Size of a process (coroutine) stack in bytes
 */
#define CMB_PROCESS_STACK_SIZE (64u * 1024u)

/**
 * @brief Return code from various process context switching calls, indicating
 *        a successful return from whatever it was calling.
 */
#define CMB_PROCESS_SUCCESS INT64_C(0)

/**
 * @brief Return code from various process context switching calls, indicating
 *        that the process was preempted by a higher priority process.
 */
#define CMB_PROCESS_PREEMPTED INT64_C(-1)

/**
 * @brief Return code from various process context switching calls, indicating
 *        that the process was interrupted with this signal. (It may also bw
 *        interrupted with some other application defined signal, any 64-bit
 *        signed integer value except these predefined values.)
 */
#define CMB_PROCESS_INTERRUPTED INT64_C(-2)
/**
 * @brief Return code from various process context switching calls, indicating
 *        that the process it was waiting for was stopped (killed) by some other
 *        process.
 */
#define CMB_PROCESS_STOPPED INT64_C(-3)

/**
 * @brief Return code from various process context switching calls, indicating
 *        that the process request for some type of resource was canceled.
 */

#define CMB_PROCESS_CANCELLED INT64_C(-4)

/**
 * @brief The states a process can be in (direct from the underlying coroutine)
 */
enum cmb_process_state {
    CMB_PROCESS_CREATED = 0,
    CMB_PROCESS_RUNNING,
    CMB_PROCESS_FINISHED
};

/**
 * @brief The process struct, inheriting all properties from `cmi_coroutine` by
 * composition, adding the name, priority, and lists of resources it may be
 * holding and things it may be waiting for.
 *
 * The `waiters_listhead` contains any processes that are waiting for this
 * process to finish. The `resources_listhead` contains any resources held by
 * this process, to be released if the process is stopped by someone else.
*/
struct cmb_process {
    struct cmi_coroutine core;              /**< The parent coroutine */
    char name[CMB_PROCESS_NAMEBUF_SZ];      /**< The process name string */
    int64_t priority;                       /**< The current process priority */
    struct cmi_process_waitable waitsfor;   /**< What the process is waiting for, if anything */
    struct cmi_list_tag *waiters_listhead;  /**< Other processes waiting for this process to finish */
    struct cmi_list_tag32 *resources_listhead; /**< Any resources held by this process */
};

/**
 * @brief The generic process function prototype, a function taking two
 * arguments, a pointer to a `cmb_process` (itself) and a pointer to some
 * application-defined context, returning a pointer to `void`. This is the same
 * as the coroutine function, except for the type of the first argument.
 */
typedef void *(cmb_process_func)(struct cmb_process *cp, void *context);

/**
 * @brief Allocate memory for the process object.
 *
 * Separated from initialization to enable object-oriented inheritance by
 * composition, where any derived "classes" from `cmb_process` can repeat the
 * same pattern as is done here with parent class `cmi_coroutine` and derived
 * class `cmb_process`.
 *
 * @memberof cmb_process
 * @return Pointer to the newly created process.
 */
extern struct cmb_process *cmb_process_create(void);

/**
 * @brief Initialize process parameters and allocates memory
 * for the underlying coroutine stack. Uses a default stack size per
 * process, `#defined` here. Does not start the process yet.
 *
 * @memberof cmb_process
 * @param pp Pointer to an already created process.
 * @param name Null terminated string for the process name.
 * @param foo The process function that will be executed when the process starts.
 * @param context The second argument to the process function, after the pointer
 *                 to the process itself.
 * @param priority The initial priority for the process, used in various
 *                 priority queues the process may find itself in.
 */
extern void cmb_process_initialize(struct cmb_process *pp,
                                   const char *name,
                                   cmb_process_func foo,
                                   void *context,
                                   int64_t priority);

/**
 * @brief Deallocate memory for the underlying coroutine stack but not for the
 * process object itself. The process exit value is still there.
 *
 * The process must be finished (exited, stopped, returned) before getting here.
 * Do not confuse this object destructor function with cmb_process_stop to force
 * a running process to exit non-voluntarily. Call that first.
 *
 * @memberof cmb_process
 * @param pp Pointer to an already created process.
 */
extern void cmb_process_terminate(struct cmb_process *pp);

/**
 * @brief Deallocate memory for the process struct and its underlying coroutine.
 *
 * @memberof cmb_process
 * @param pp Pointer to an already created process.
 */
extern void cmb_process_destroy(struct cmb_process *pp);

/**
 * @brief Schedule the process to start execution at the current simulation time.
 *
 * This is a non-blocking call, allowing the calling process to continue
 * execution until it explicitly yields to some other process.
 *
 * @memberof cmb_process
 * @param pp Pointer to an already created process.
 */
extern void cmb_process_start(struct cmb_process *pp);

/**
 * @brief  Hold (sleep) for a specified duration of simulated time. Called from
 *         within a process.
 *
 * @memberof cmb_process
 * @param dur The duration to hold for, relative to the current simulation time.
 * @return `CMB_PROCESS_SUCCESS` if returning normally at the scheduled time,
 *        otherwise some other signal value indicating the type of interruption.
 */
extern int64_t cmb_process_hold(double dur);

/**
 * @brief  Wait for some other process to finish. Called from within a process.
 *
 * Returns immediately if the awaited process already is finished.
 *
 * @memberof cmb_process
 * @param awaited The process we will be waiting for.
 * @return `CMB_PROCESS_SUCCESS` if the awaited process exited normally,
 *         `CMB_PROCESS_STOPPED` if it was stopped by some other process,
 *         something else if we were interrupted with some other signal.
 */
extern int64_t cmb_process_wait_process(struct cmb_process *awaited);

/**
 * @brief  Wait for an event to occur. Called from within a process.
 *
 * @memberof cmb_process
 * @param ev_handle The handle of the event we will be waiting for. Note that
 *                  this is not a pointer, see `cmb_event.h`for details.
 * @return `CMB_PROCESS_SUCCESS` if the awaited event occurred,
 *         `CMB_PROCESS_CANCELLED` if the event was canceled for some reason,
 *         something else if we were interrupted with some other signal.
 */
extern int64_t cmb_process_wait_event(uint64_t ev_handle);

/**
 * @brief  Terminate the process with the given exit value. Called from within
 *         the process.
 *
 * @memberof cmb_process
 * @param retval The return value from the process, user defined meaning.
 *               Will be stored as the `cmb_coroutine` `exit_value`.
 */
extern void cmb_process_exit(void *retval);

/**
 * @brief  Interrupt a holding process, passing the non-zero signal value `sig`,
 *         which will appear as return value from whatever the target process
 *         was doing when it was interrupted.
 *
 * The signal cannot be `CMB_PROCESS_SUCCESS`, since that would appear as a
 * normal, non-interrupted return.
 *
 * Does not directly transfer control to the target, but enters an interrupt
 * event with priority `pri` at the current simulation time. This lets the
 * calling process complete whatever else it is doing at the current time before
 * the interrupt is executed and control is transferred to the target.
 *
 * @memberof cmb_process
 * @param pp Pointer to the target process.
 * @param sig The signal to be passed to the victim process, e.g.,
 *            `CMB_PROCESS_INTERRUPTED`, or something user-application defined.
 * @param pri The priority for the interrupt event that will be scheduled.
 */
extern void cmb_process_interrupt(struct cmb_process *pp,
                                  int64_t sig,
                                  int64_t pri);

/**
 * @brief  Kill the target process by scheduling a stop event.
 *
 * Sets the target process exit value to the argument value `retval`. The
 * meaning of return values for an externally terminated process is application
 * defined.
 *
 * Does not transfer control to the target process, but schedules a stop event
 * at the current simulation time with minimum priority (`INT64_MIN`) to ensure
 * that any other events can execute first. Does not destroy the target's memory
 * allocation. The target process can be restarted from the beginning by calling
 * `cmb_process_start(pp)` again.
 *
 * @memberof cmb_process
 * @param pp Pointer to the target process.
 * @param retval The return value from the process, user defined meaning, often
 *               `NULL` for an externally killed process. Will be stored as the
 *               `cmb_coroutine` `exit_value`.
 */
extern void cmb_process_stop(struct cmb_process *pp, void *retval);

/**
 * @brief  Return the process name as a `const char *`, since it is kept in a
 * fixed size buffer and should not be changed directly.
 *
 * If the name for some reason needs to be changed, use `cmb_process_set_name`
 * to do it safely, not by modifying through this pointer.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 */
static inline const char *cmb_process_name(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->name;
}

/**
 * @brief  Set a new name for the process.
 *
 * The name is held in a fixed size buffer of size `CMB_PROCESS_NAMEBUF_SZ`.
 * If the new name is too large for the buffer, it will be truncated at one less
 * than the buffer size, leaving space for the terminating zero char.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 * @param name Null terminated string for the process name.
 */
extern void cmb_process_set_name(struct cmb_process *pp,
                                 const char *name);

/**
 * @brief  Return a pointer to the context. Not const, the caller may change the
 *         content of the context data through this pointer.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 * @return Pointer to the process context.
 */
extern void *cmb_process_context(const struct cmb_process *pp);

/**
 * @brief  Replace the process context with something else.
 *
 * The intended use is for cases where the context is not ready when the process
 * is initialized, e.g., because it will contain a pointer to some object that
 * has not been created yet.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 * @param context The second argument to the process function, after the pointer
 *                 to the process itself. The content and meaning are user
 *                 application defined.
 */
extern void cmb_process_set_context(struct cmb_process *pp, void *context);

/**
 * @brief  Get the current priority for the process.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 * @return The current priority value.
 */
extern int64_t cmb_process_priority(const struct cmb_process *pp);

/**
 * @brief  Change the priority for the process.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 * @param pri The new priority value.
 */
extern void cmb_process_set_priority(struct cmb_process *pp, int64_t pri);

/**
 * @brief  Get the current status of the process
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 * @return The current state of the process and its underlying coroutine.
 */
static inline enum cmb_process_state cmb_process_status(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    const struct cmi_coroutine *cp = (struct cmi_coroutine *)pp;

    return (enum cmb_process_state)(cp->status);
}

/**
 * @brief  Get the stored exit value from the process, as set by
 * `cmb_process_exit`, `cmb_process_stop`, or simply returned by the
 * process function. Will issue a warning and return `NULL` if the process has
 * not yet finished.
 *
 * @memberof cmb_process
 * @param pp Pointer to a process.
 */
extern void *cmb_process_exit_value(const struct cmb_process *pp);

/**
 * @brief  Return a pointer to the currently executing process, i.e., the calling
 * process itself.
 *
 * @memberof cmb_process
 * @return Pointer to the currently executing process, `NULL` if called from
 * outside a named process, such as the main process that executes the event
 * dispatcher.
 */
extern struct cmb_process *cmb_process_current(void);


#endif /* CIMBA_CMB_PROCESS_H */
