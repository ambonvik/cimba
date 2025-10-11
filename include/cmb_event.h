/*
 * cmb_event.h - simulation manager for discrete event simulation.
 * Provides routines to handle clock sequencing and event scheduling.
 *
 * An event is defined as a function taking two pointers to void as
 * arguments and returning void. The arguments are application defined,
 * but the intention is to provide tuples of (action, subject, object)
 * consisting of pointers to the event function and its two arguments.
 *
 * This is similar to a closure, i.e., an object consisting of a function
 * and its context, for execution at some other point in time and space.
 * In this case, it will be called as *action(subject, object) when it is
 * its turn. Afterwards, control will return to the event dispatcher, which
 * does not know much about the event specifics. Hence, no need to return
 * indications of success or failure (or anything else) from the event
 * function - void (*action)(void *object, void *subject).
 *
 * The first argument void *subject can be understood as the implicit
 * self or this pointer in an object-oriented language. It can be used as
 * an identificator, e.g. what object or process the event belongs to.
 * Understood that way, the meaning becomes subject.action(object), i.e.,
 * a method of the subject class, acting on some other object.
 *
 * The event has an associated activation time and a priority. Just before
 * the event is executed, the simulation time will jump to this time as the
 * event is removed from the queue. The priority is a signed 16-bit integer,
 * where higher numeric value means higher priority. If two events have equal
 * activation time, the one with higher priority will execute first. If
 * you need to ensura that some event executes after all other events at
 * a particular time, use a large negative priority. If two events have the
 * same activation time and the same priority, they will be executed in
 * first in first out (FIFO) order.
 *
 * When scheduled, an event handle will be assigned and returned. This is
 * a unique event identifier and can be used as a reference for later
 * cancelling, rescheduling, or reprioritizing the event. Behind the scene,
 * the event queue is implemented as a hashheap where the event handle is a key
 * in the hash map and the event's current location in the heap is the hash map
 * value. This gives O(1) cancellations and reschedules with no need to search
 * the entire heap to find a future event. The details of the data structure
 * are not exposed in this header file, see cmb_event.c for the implementation.
 *
 * As always, the error handling is draconian. Functions for e.g. rescheduling
 * an event will trip an assertion if the given event is not currently in the
 * event queue. This is a deliberate design choice, see the documentation.
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025.
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

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

/*
 * cmb_time : Get the current simulation time, read-only for the user application
 */
extern double cmb_time(void);

/*
 * cmb_run : Executes the event list until empty or otherwise stopped.
 *
 * Schedule an event calling cmb_event_list_destroy() or calling
 * cmb_event_cancel_all(CMB_ANY_ACTION, CMB_ANY_SUBJECT, CMB_ANY_OBJECT)
 * to zero out the event queue and stop the simulation. This can either be pre-
 * scheduled at some particular time or triggered by some other condition such
 * as reaching a certain number of samples in some data collector.
 */
extern void cmb_run(void);


/*
 * typedef cmb_event_func : The generic event function type
 */
typedef void (cmb_event_func)(void *subject, void *object);

/*
 * cmb_event_eueue_init : Initialize the event queue itself.
 * Must be called before any events can be scheduled or executed.
 */
extern void cmb_event_queue_init(double start_time);

/*
 * cmb_event_queue_destroy : Free memory allocated for event queue and
 * reinitialize pointers. Can be reinitialized by calling cmb_event_queue_init
 * again to start a new simulation run.
 */
extern void cmb_event_queue_destroy(void);

/*
 * cmb_event_schedule: Insert event in event queue as indicated by reactivation
 * time and priority. An event cannot be scheduled at a time before current.
 * Returns the unique handle of the scheduled event.
 */
extern uint64_t cmb_event_schedule(cmb_event_func *action,
                                   void *subject,
                                   void *object,
                                   double time,
                                   int16_t priority);

/*
 *  cmb_event_next : Removes and executes the first event in the event queue.
 *  If both reactivation time and priority equal, first in first out order.
 *
 *  Returns true for success, false for failure (e.g., empty event list), for
 *  use in loops like while(cmb_event_execute_next()) { ... }
 */
extern bool cmb_event_execute_next(void);

/*
 * cmb_event_is_scheduled : Is the given event currently in the event queue?
 */
extern bool cmb_event_is_scheduled(uint64_t handle);

/*
 * cmb_event_time : Get the currently scheduled time for an event
 * Precondition: The event is in the event queue.
 * If in doubt, call cmb_event_is_scheduled(handle) first to verify.
 */
extern double cmb_event_time(uint64_t handle);

/*
 * cmb_event_priority : Get the current priority for an event
 * Precondition: The event is in the event queue.
 */
extern int16_t cmb_event_priority(uint64_t handle);

/*
 * cmb_event_cancel: Remove event from event queue.
 * Precondition: The event is in the event queue.
 */
extern void cmb_event_cancel(uint64_t handle);

/*
 * cmb_event_reschedule: Reschedules event at index to another (absolute) time
 * Precondition: The event is in the event queue.
 */
extern void cmb_event_reschedule(uint64_t handle, double time);

/*
 * cmb_event_reprioritize: Reprioritizes event to another priority level
 * Precondition: The event is in the event queue.
 */
extern void cmb_event_reprioritize(uint64_t handle, int16_t priority);

/*
 * cmb_event_find: Search in event list for an event matching the given pattern
 * and return its handle if one exists in the queue. Return zero if no match.
 * CMB_ANY_* are wildcarda, matching any value in its position.
 *
 * Will start the search from the beginning of the event queue each time,
 * since the queue may have changed in the meantime. There is no guarantee
 * for it returning the event that will execute first, only that it will find
 * some event that matches the search pattern if one exists in the queue. The
 * sequence in which events are found is unspecified.
 */

#define CMB_ANY_ACTION ((cmb_event_func *)0xFFFFFFFFFFFFFFFFull)
#define CMB_ANY_SUBJECT ((void *)0xFFFFFFFFFFFFFFFFull)
#define CMB_ANY_OBJECT ((void *)0xFFFFFFFFFFFFFFFFull)

extern uint64_t cmb_event_find(cmb_event_func *action,
                               const void *subject,
                               const void *object);

/* cmb_event_count : Similarly, count the number of matching events. */
extern uint64_t cmb_event_count(cmb_event_func *action,
                                const void *subject,
                                const void *object);

/*
 * cmb_event_cancel_all : Cancel all matching events, returns the number
 * of events that were cancelled, possibly zero. Use e.g. for cancelling all
 * events related to some subject or object if that thing no longer is alive in
 * the simulation.
 */
extern uint64_t cmb_event_cancel_all(cmb_event_func *action,
                                     const void *subject,
                                     const void *object);

/*
 * cmb_event_heap_print : Print the current content of the event heap.
 * Intended for debugging use, will print hexadecimal pointer values and
 * similar raw data values from the event tag structs.
 */
extern void cmb_event_heap_print(FILE *fp);

/*
 * cmb_event_hash_print : Print the current content of the hash map.
 * Intended for debugging use, will print 64-bit handles and indexes.
 */
extern void cmb_event_hash_print(FILE *fp);

#endif /* CIMBA_CMB_EVENT_H */