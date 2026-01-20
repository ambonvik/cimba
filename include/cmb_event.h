/**
 * @file cmb_event.h
 * @brief Event queue manager for discrete event simulation.
 * Provides routines to key clock sequencing and event scheduling.
 *
 * An event is defined as a function taking two pointers to void as
 * arguments and returning void. The arguments are application defined,
 * but the intention is to provide tuples of (action, subject, object)
 * consisting of pointers to the event function and its two arguments.
 * It will be called as `*action(subject, object)` when it is its turn.
 *
 * Afterward, control will return to the event dispatcher, which does not
 * know the event specifics. Hence, no need to return indications of
 * success or failure (or anything else) from the event function.
 *
 * The first argument `void *subject` can be understood as the implicit
 * self or this pointer in an object-oriented language. It can be used as
 * an identification, e.g., what object or process the event belongs to.
 * Understood that way, the meaning becomes `subject.action(object)`, i.e.,
 * a method of the subject class, acting on some other object.
 *
 * The event has an associated activation time and a priority. Just before
 * the event is executed, the simulation time will jump to this time as the
 * event is removed from the queue. The priority is a signed 16-bit integer,
 * where a higher numeric value means a higher priority. If two events have equal
 * activation time, the one with higher priority will execute first. If
 * you need to ensure that some event executes after all other events at
 * a particular time, use a large negative priority. If two events have the
 * same activation time and the same priority, they will be executed according
 * to first in first out (FIFO) order.
 *
 * When scheduled, an event key will be assigned and returned. This is
 * a unique event identifier and can be used as a reference for later
 * cancelling, rescheduling, or reprioritizing the event. Behind the scene,
 * the event queue is implemented as a hashheap where the event key is a key
 * in the hash map and the event's current location in the heap is the hash map
 * value. This gives O(1) cancellations and reschedules with no need to search
 * the entire heap to find a future event. The details of the data structure
 * are not exposed in this header file, see `cmb_event.c` for implementation.
 *
 * As always, the error handling is draconian. Functions for e.g., rescheduling
 * an event will trip an assertion if the given event is not currently in the
 * event queue. This is a deliberate design choice to ensure that bugs get fixed
 * rather than "handled".
 */

/*
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025-26.
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

#ifndef CIMBA_CMB_EVENT_H
#define CIMBA_CMB_EVENT_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

/**
 * @brief Get the current simulation time, read-only for user application.
 * @return Current simulation time.
 */
extern double cmb_time(void);

/**
 * @brief Defines a prototype for the generic event function type
 *        `void action(void *subject, void *object)`
 */
typedef void (cmb_event_func)(void *subject, void *object);

/**
 * @brief Initialize the event queue itself. Must be called before any events
 * can be scheduled or executed. Expects to find an empty event queue.
 *
 * Call at the beginning of your simulation trial to start from a fresh state.
 * Make sure to call `cmb_event_queue_terminate` at the end of your trial
 * to free up space.
 *
 * Note that there is no `cmb_event_queue_create`, `_reset`, or `_destroy`.
 * There ìs only one (thread local) event queue per thread, no need to create
 * another. Hence, no need for the usual self-pointer as the first argument to
 * this function either, it acts on the one and only event queue in this thread.
 *
 * Calling `cmb_event_queue_initialize` again on the next trial in the same
 * worker thread without calling `cmb_event_queue_terminate` at the end of the
 * previous trial will fire an assert about the event queue not being `NULL` in
 * `cmb_event_queue_initialize`.
 *
 * @param start_time The starting value for the simulation clock, usually 0.
 */
extern void cmb_event_queue_initialize(double start_time);

/**
 * @brief Reset event queue to fresh state. Free's memory allocated for internal
 *        workings of the event queue.
 *
 * No argument needed, acts on the current thread's event queue.
 *
 * Call at the end of every simulation trial to clean up allocated space for the
 * event queue. Then call `cmb_event_queue_initialize()` again at the start of
 * the next trial.
 */
extern void cmb_event_queue_terminate(void);

/**
 * @brief Clears out all scheduled events from the queue.
 *
 * Does not deallocate any memory or reset any counters, just cancels all events
 * in the queue. Calling this function from an event will stop the simulation
 * running as `cmb_event_queue_execute()`- no more events to execute after that.
 */
extern void cmb_event_queue_clear(void);

/**
 * @brief Is the event queue empty?
 * @returns `true` if empty, `false` if not.
 */
extern bool cmb_event_queue_is_empty(void);

/**
 * @brief Returns the current number of events in the queue.
 * @return The number of events in the event queue.
 */
extern uint64_t cmb_event_queue_count(void);

/**
 * @brief Insert an event in the event queue as indicated by the activation time
 *        and priority. An event cannot be scheduled at a time before the
 *        current simulation time.
 *
 * @param action  Pointer to the event function to execute.
 * @param subject Pointer to something user-defined, intended as a self-pointer
 *                for whatever entity (e.g., a `cmb_process`) is acting here.
 * @param object  Pointer to something user-defined, intended as a pointer to
 *                whatever object the `subject` is acting on.
 * @param time    The simulation time when this event will occur. The `time`
 *                argument must be greater than or equal to the current
 *                simulation time when making this call.
 * @param priority The priority of this event at the scheduled time, an integer
 *                between `INT64_MIN`and `INT64_MAX`. Events with numerically
 *                higher priority will happen before events with lower if
 *                scheduled at the same time. If both are equal, they will occur
 *                in FIFO sequence.
 * @return        The unique key of the scheduled event, to be used as a
 *                reference for any rescheduling or cancellation of this event.
 */
extern uint64_t cmb_event_schedule(cmb_event_func *action,
                                   void *subject,
                                   void *object,
                                   double time,
                                   int64_t priority);

/**
 * @brief Removes and executes the first event in the event queue.
 *
 * @return `true` for success, `false` for failure (e.g., empty event list),
 *         for use in loops like `while(cmb_event_execute_next()) { ... }`.
 */
extern bool cmb_event_execute_next(void);

/**
 * @brief Executes events from the event queue until empty.
 *
 * Schedule an event calling `cmb_event_queue_clear()` to zero out the event
 * queue and stop the simulation. This can either be pre-scheduled for some
 * particular time or triggered by some other condition such as reaching a
 * certain number of samples in some data collector, a confidence interval being
 * narrow enough, or anything else.
 */
extern void cmb_event_queue_execute(void);

/**
 * @brief Is the given event currently in the event queue?
 * @param key The key of some event.
 * @return `true` if the event is scheduled in the event queue, `false` if not.
 */
extern bool cmb_event_is_scheduled(uint64_t key);

/**
 * @brief Get the currently scheduled time for an event. The event is assumed
 *        to be in the event queue, error if not. Call `cmb_event_is_scheduled`
 *        first if not sure.
 * @param key The `key` of some event in the event queue.
 * @return The scheduled activation time for the event.
 */
extern double cmb_event_time(uint64_t key);

/**
 * @brief Get the current priority for an event. The event is assumed
*         to be in the event queue, error if not. Call `cmb_event_is_scheduled`
 *        first if not sure.
 * @param key The `key` of some event in the event queue.
 * @return The priority for the event.
 */
extern int64_t cmb_event_priority(uint64_t key);

/**
 * @brief  Remove event from event queue.
 * @param key The `key` of some event in the event queue.
 * @return `true` if the event was found, `false` if not.
 */
extern bool cmb_event_cancel(uint64_t key);

/**
 * @brief  Reschedules event at index to another (absolute) time. The event is
*          assumed to be in the event queue, error if not. Call
*         `cmb_event_is_scheduled` first if not sure.
 * @param key The `key` of some event in the event queue.
 * @param time The new scheduled time of the event.
 */
extern void cmb_event_reschedule(uint64_t key, double time);

/**
* @brief  Reprioritizes event to another priority level. The event is
*         assumed to be in the event queue, error if not. Call
*         `cmb_event_is_scheduled` first if not sure.
 * @param key The `key` of some event in the event queue.
 * @param priority The new priority of the event.
 */
extern void cmb_event_reprioritize(uint64_t key, int64_t priority);

/**
 * @brief Wildcard pattern that matches any `cmb_event_func` (action) when searching
 *        the event list.
 */
#define CMB_ANY_ACTION ((cmb_event_func *)UINT64_C(0xFFFFFFFFFFFFFFFF))

/**
 * @brief Wildcard pattern that matches any subject when searching the event list.
 */
#define CMB_ANY_SUBJECT ((void *)UINT64_C(0xFFFFFFFFFFFFFFFF))

/**
 * @brief Wildcard pattern that matches any object when searching the event list.
 */
#define CMB_ANY_OBJECT ((void *)UINT64_C(0xFFFFFFFFFFFFFFFF))

/**
 * @brief Search in the event list for an event matching the given pattern
 *        and return its key if one exists in the queue. Return zero if no
 *        match. `CMB_ANY_*` are wildcards, matching any value in its position.
 *
 * Will start the search from the beginning of the event queue each time,
 * since the queue may have changed in the meantime. There is no guarantee
 * for it returning the event that will execute first, only that it will find
 * some event that matches the search pattern if one exists in the queue. The
 * sequence in which events are found is unspecified.
 *
 * @param action  Pointer to the event function to find, or `CMB_ANY_ACTION`
 * @param subject Pointer to the subject to find, or `CMB_ANY_SUBJECT`
 * @param object  Pointer to the object to find, or `CMB_ANY_OBJECT`
 *
 * @return The key of an event matching the search pattern, zero if none.
 */
extern uint64_t cmb_event_pattern_find(cmb_event_func *action,
                               const void *subject,
                               const void *object);

/**
 * @brief Count the number of scheduled events matching the search pattern.
 *
 * @param action  Pointer to the event function to find, or `CMB_ANY_ACTION`
 * @param subject Pointer to the subject to find, or `CMB_ANY_SUBJECT`
 * @param object  Pointer to the object to find, or `CMB_ANY_OBJECT`
 *
 * @return The number of events matching the search pattern, possibly zero.
*/
extern uint64_t cmb_event_pattern_count(cmb_event_func *action,
                                        const void *subject,
                                        const void *object);

/**
 * @brief Cancel all matching events, returns the number
 * of events that were canceled, possibly zero.
 *
 * Use e.g., for cancelling all events related to some subject or object if that
 * thing no longer is alive in the simulation.
 *
 * @param action  Pointer to the event function to find, or `CMB_ANY_ACTION`
 * @param subject Pointer to the subject to find, or `CMB_ANY_SUBJECT`
 * @param object  Pointer to the object to find, or `CMB_ANY_OBJECT`
 *
 * @return The number of events that matched and were canceled, possibly zero.

 */
extern uint64_t cmb_event_pattern_cancel(cmb_event_func *action,
                                         const void *subject,
                                         const void *object);

/**
 * @brief Print the current content of the event queue.
 *
 * Intended for debugging use only, will print hexadecimal pointer values and
 * similar raw data values from the event tag structs.
 *
 * @param fp A file pointer to print to, perhaps `stdout`.
 */
extern void cmb_event_queue_print(FILE *fp);

#endif /* CIMBA_CMB_EVENT_H */