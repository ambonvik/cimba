/*
 * cmb_process.h - the simulated processes
 *
 * The cmb_process is a named coroutine that works in the simulated time and
 * interacts with events off the simulation event queue. It has a name and a
 * priority, which can be changed later.
 *
 * The cmb_process can hold (deactivate itself for a certain interval of
 * simulated time) and wait for resources to become available. In those states,
 * it can also be interrupted by other processes. The interrupt passes a non-
 * zero value that appears as the return value from hold, to be used for
 * distinguishing between normal return vs various (possibly user-defined)
 * interrupt types.
 *
 * In the same way as the interrupt call, the functions for starting and
 * stopping a process are non-blocking. The calling process will continue
 * immediately, until it explicitly transfers control to another process by
 * yield or resume. The actual transfer of control happens from a scheduled
 * event, defined in cmb_process.c.
 *
 * Copyright (c) Asbjørn M. Bonvik 2025.
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

/* Maximum length of a process name, anything longer will be truncated */
#define CMB_PROCESS_NAMEBUF_SZ 32

/*
 * struct cmb_process : Inherits all properties from struct cmi_coroutine by
 * composition and adds the name, priority, and the handle of wakeup event (if
 * the process is holding, i.e. scheduled for a wakeup event, otherwise zero).
 */
struct cmb_process {
    struct cmi_coroutine cr;
    char name[CMB_PROCESS_NAMEBUF_SZ];
    int64_t priority;
    uint64_t wakeup_handle;
    struct cmi_processtag *waiter_tag;
};

/*
 * typedef cmb_process_func : The generic process function type is a function
 * taking two arguments, a pointer to a cmb_process (its own) and a pointer to
 * some application-defined context, returning a pointer to void. This is the
 * same as the coroutine function, except for the type of the first argument.
 */
typedef void *(cmb_process_func)(struct cmb_process *cp, void *context);

/*
 * cmb_process_create : Allocates memory for the process object. Separated from
 * initialization to enable object-oriented inheritance by composition, where
 * derived "classes" from cmb_process can repeat the same pattern as is done
 * here with parent class cmi_coroutine and derived class cmb_process.
 */
extern struct cmb_process *cmb_process_create(void);

/*
 * cmb_process_initialize : Initializes process parameters and allocates memory
 * for the underlying coroutine stack. Uses a default 64 kB stack size per
 * process, #defined here. Does not start the process yet.
 */
#define CMB_PROCESS_STACK_SIZE (64u * 1024u)
extern struct cmb_process *cmb_process_initialize(struct cmb_process *pp,
                                                  const char *name,
                                                  cmb_process_func foo,
                                                  void *context,
                                                  int64_t priority);

/*
 * cmb_process_terminate : Deallocates memory for the underlying coroutine stack
 * but not for the process object itself. The process exit value is still there.
 * The process must be finished (exited, stopped, returned) before getting here.
 * Do not confuse this object destructor function with cmb_process_stop to force
 * a running process to exit non-voluntarily. Call that first.
 */
extern void cmb_process_terminate(struct cmb_process *pp);

/*
 * cmb_process_destroy : Deallocates memory for the process struct and its
 * underlying coroutine object.
 */
extern void cmb_process_destroy(struct cmb_process *pp);


/*
 * cmb_process_start : Schedules the process to start execution at the current
 * simulation time. This is a non-blocking call, allowing the calling process
 * to continue execution until it explicitly yields to some other process.
 */
extern void cmb_process_start(struct cmb_process *pp);

/*
 * cmb_process_hold : Wait for a specified duration. Returns 0 (NORMAL) when
 * returning normally after the specified duration, something else if not. We
 * provide a default value, but the user application can use whatever
 * interrupt values it needs, including defining an enum with any number of
 * values. We do not do that here, since it is unpredictable what and how many
 * types of interrupts an application may need, or what the different values
 * will mean in the application. Called from within the process.
 */
#define CMB_PROCESS_HOLD_NORMAL (0LL)
#define CMB_PROCESS_HOLD_INTERRUPTED (1LL)
extern int64_t cmb_process_hold(double dur);

/*
 * cmb_process_wait_process : Wait for some other proceess to finish.
 * Returns immediately if the awaited process already is finished.
 * Returns CMB_PROCESS_WAIT_NORMAL if awaited exited normally,
 * CMB_PROCESS_WAIT_STOPPED if it was stopped by some other process.
 * Similar to pthreads join.
 */
#define CMB_PROCESS_WAIT_NORMAL (0LL)
#define CMB_PROCESS_WAIT_STOPPED (1LL)
extern int64_t cmb_process_wait_process(struct cmb_process *awaited);

/*
 * cmb_process_wait_event : Wait for an event to occur.
 * Returns CMB_PROCESS_WAIT_NORMAL when the event executes normally,
 * CMB_PROCESS_WAIT_CANCELLED if the event was cancelled for some reason.
 */
#define CMB_PROCESS_WAIT_CANCELLED (2LL)
extern int64_t cmb_process_wait_event(uint64_t ev_handle);

/*
 * cmb_process_exit : Terminate the process with the given return value.
 * Called from within that process.
 */
extern void cmb_process_exit(void *retval);

/*
 * cmb_process_interrupt : Interrupt a holding process, passing the non-zero
 * signal value sig, which will appear as return value from cmb_process_hold.
 * The signal cannot be CMB_PROCESS_HOLD_NORMAL, since that would appear as a
 * normal, non-interrupted return from cmb_process_hold.
 *
 * Enters an interrupt event with priority pri at the current simulation time,
 * rather than calling the process interrupt handler directly. This lets the
 * calling process complete whatever else it is doing at the current time before
 * the interrupt is executed and control is transferred to the interrupted
 * process.
 *
 * Since this allows multiple interrupts on the same process, the interrupt
 * event function will check if the process still is holding in the event queue
 * before executing the transfer.
 */
extern void cmb_process_interrupt(struct cmb_process *pp,
                                  int64_t sig,
                                  int64_t pri);

/*
 * cmb_process_stop : Terminate the target process by scheduling a stop event.
 * Sets the target process exit value to the argument value retval. The meaning
 * of return values for an externally terminated process is application defined.
 *
 * Does not transfer control to the target process. Does not destroy its memory
 * allocation. The target process can be restarted from the beginning by calling
 * cmb_process_start(pp) again.
 */
extern void cmb_process_stop(struct cmb_process *pp, void *retval);

/*
 * cmb_process_get_name : Return the process name as a const char *,
 * since it is kept in a fixed size buffer and should not be changed directly.
 *
 * If the name for some reason needs to be changed, use cmb_process_set_name to
 * do it safely.
 */
static inline const char *cmb_process_get_name(const struct cmb_process *pp)
{
    cmb_assert_release(pp != NULL);

    return pp->name;
}

/*
 * cmb_process_set_name : Set a new name for the process, returning a const
 * char * to the new name. The name is held in a fixed size buffer of size
 * CMB_PROCESS_NAMEBUF_SZ. If the new name is too large for the buffer, it will
 * be truncated at one less than the buffer size, leaving space for the
 * terminating zero char.
 */
extern const char *cmb_process_set_name(struct cmb_process *cp,
                                        const char *name);

/*
 * cmb_process_get_context : Return a pointer to the context. Not const, the
 * caller may change the content of the context data through this pointer.
 */
extern void *cmb_process_get_context(const struct cmb_process *pp);

/*
 * cmb_process_set_context : Replace the process context with something else.
 * Returns the old context pointer. The intended use is for cases where the
 * context is not ready when the process is created, e.g. because it will
 * contain a pointer to some obejct that has not been created yet.
 *
 * Use with extreme care if changing the context of a running process. An
 * optimizing compiler may not expect it to be changed and cause unexpected
 * behavior.
 */
extern void *cmb_process_set_context(struct cmb_process *pp, void *context);

/*
 * cmb_process_get_priority : Returns the current priority for the process.
 */
extern int64_t cmb_process_get_priority(const struct cmb_process *pp);

/*
 * cmb_process_set_priority : Changes the priority for the process, returning the
 * old priority value.
 */
extern int64_t cmb_process_set_priority(struct cmb_process *pp, int64_t pri);

/*
 * cmb_process_get_exit_value : Returns the stored exit value from the process,
 * as set by cmb_process_exit, cmb_process_stop, or simply returned by the
 * process function. Will issue a warning and return NULL if the process has not
 * yet finished.
 */
extern void *cmb_process_get_exit_value(const struct cmb_process *pp);

/*
 * cmb_process_get_current : Returns a pointer to the currently executing
 * process, i.e. the calling process itself. Returns NULL if called from outside
 * a named process, such as the main process that executes the event scheduler.
 */
extern struct cmb_process *cmb_process_get_current(void);


#endif // CIMBA_CMB_PROCESS_H
