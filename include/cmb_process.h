/*
 * cmb_process.h - the simulated processes
 *
 * Basically, the cmb_process is a coroutine that works in the simulated time
 * and interacts with events off the simulation event queue. It has a name and
 * a priority, which can be changed. When a cmb_process is created, a start
 * event is scheduled at the current time. The cmb_process can hold (deactivate
 * itself for a certain interval of simulated time) and wait for resources to
 * become available. In those states, it can also be interrupted by other
 * processes. The interrupt passes a non-zero value that appears as the return
 * value from hold, to be used for distinguishing between normal return vs
 * various (possibly user-defined) interrupt types.
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

#include "cmb_event.h"
#include "cmi_coroutine.h"

/*
 * struct cmb_process : Inherits all properties from a coroutine and adds name,
 * priority, and the next scheduled wake-up event (when applicable).
 */
#define CMB_PROCESS_NAMEBUF_SZ 32
struct cmb_process {
    struct cmi_coroutine cr;
    char name[CMB_PROCESS_NAMEBUF_SZ];
    int16_t priority;
    uint64_t wakeup_handle;
};

/*
 * typedef cmb_process_func : The generic process function type is a function
 * taking two arguments, a pointer to a cmb_process (its own) and a pointer to
 * some application-defined context, returning a pointer to void. This is the
 * same as the coroutine function, except for the type of the first argument.
 */
typedef void *(cmb_process_func)(struct cmb_process *cp, void *context);

/*
 * cmb_process_create : Allocates memory for the process and its underlying
 * coroutine. Uses a default 64 kB stack size per process, #defined here.
 */
#define CMB_PROCESS_STACK_SIZE (64u * 1024u)
extern struct cmb_process *cmb_process_create(const char *name,
                                              cmb_process_func foo,
                                              void *context,
                                              int16_t priority);

extern void cmb_process_destroy(struct cmb_process *cp);

extern void cmb_process_start(struct cmb_process *cp);

extern const char *cmb_process_get_name(const struct cmb_process *cp);
extern char *cmb_process_set_name(struct cmb_process *cp, const char *name);
extern void *cmb_process_get_context(const struct cmb_process *cp);
extern void *cmb_process_set_context(struct cmb_process *cp, void *context);
extern int16_t cmb_process_get_priority(const struct cmb_process *cp);
extern int16_t cmb_process_set_priority(struct cmb_process *cp, int16_t pri);

extern struct cmb_process *cmb_process_get_current(void);

/*
 * cmb_process_hold : Wait for a specified duration. Returns 0 (NORMAL) when
 * returning normally after the specified duration, something else if not. We
 * provide a default value, but the user application can use whatever
 * interrupt values it needs, including defining an enum with any number of
 * values. We do not do that here, since it is unpredictable what and how many
 * types of interrupts an application may need, or what the different values
 * will mean in the application. Called from within the process.
 */
#define CMB_PROCESS_HOLD_NORMAL 0LL
#define CMB_PROCESS_HOLD_INTERRUPTED (1LL)
extern int64_t cmb_process_hold(double dur);

/*
 * cmb_process_exit : Terminate the process with the given return value.
 * Called from within that process.
 */
extern void cmb_process_exit(void *retval);

/*
 * cmb_process_interrupt : Preempt a holding process, passing the non-zero
 * signal value sig, which will appear as return value from cmb_process_hold.
 * The signal cannot be zero, since that would appear as a normal, non-
 * interrupted return from cmb_process_hold. Called on some other process.
 *
 * This will enter an interrupt event with priority pri at the current
 * simulation time, rather than calling the process interrupt handler directly.
 * This lets the calling process complete whatever it is doing at the current
 * time before the interrupt is executed and control is transferred to the
 * interrupted process.
 *
 * Since this allows multiple interrupts on the same process, the interrupt
 * event function will check if the process still is holding in the event queue.
 */
extern void cmb_process_interrupt(struct cmb_process *cp,
                                  int64_t sig,
                                  int16_t pri);

/*
 * cmb_process_stop : Unceremoniously terminate the calling process. Does not
 * transfer control to the victim process. Does not destroy its memory
 * allocation. The process can be restarted from the beginning by calling
 * cmb_process_start(cp) again.
 */
extern void cmb_process_stop(struct cmb_process *cp);


#endif // CIMBA_CMB_PROCESS_H
