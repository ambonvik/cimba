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
 * event is removed from the queue. The priority is an integer value, where
 * higher numeric value means higher priority. If two events have equal
 * activation time, the one with higher priority will execute first. If
 * you need to ensura that some event executes after all other events at
 * a particular time, use a large negative priority. If two events have the
 * same activation time and the same priority, the activation order is
 * unspecified. (Guaranteeing FIFO could have performance implications.)
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

#include <stdio.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmi_config.h"

/* The generic event type */
typedef void (cmb_event_func)(void*, void*);

/*
 * The tag to store an event with its context.
 * These tags only exist as members of the event queue, never seen alone.
 */
struct cmb_event_tag {
    cmb_event_func *action;
    void *subject;
    void *object;
    double time;
    int16_t priority;
};

/* The event queue */
extern CMB_THREAD_LOCAL struct cmb_event_tag *cmi_event_queue;

/* Manage the event queue itself */
extern void cmb_event_queue_init(double start_time);
extern void cmb_event_queue_destroy(void);

/* Current simulation time. */
extern CMB_THREAD_LOCAL double cmi_event_sim_time;

/* Get the current simulation time */
inline double cmb_time(void) {
    return cmi_event_sim_time;
}

/*
 * cmb_event_schedule: Inserts event in event queue as indicated by reactivation
 * time and priority. An avent cannot be scheduled at a time before current. The
 * reactivation time rel_time is relative to the current simulation time and is
 * non-negative.
 */
extern void cmb_event_schedule(cmb_event_func *action,
                               void *subject,
                               void *object,
                               double rel_time,
                               int16_t priority);

/*
 *  cmb_event_next; Executes and removes the first event in the event queue.
 *  If both reactivation time and priority equal, FCFS order.
 *  Returns 1 for success, 0 for failure (e.g., empty event list)
 */
extern int cmb_event_execute_next(void);

/* TODO: Heavy rewrite to use hash table instead of direct index */

/* cmb_event_cancel: Remove event from event list. */
extern void cmb_event_cancel(uint64_t index);

/* cmb_event_reschedule: Reschedules event at index to another time */
extern void cmb_event_reschedule(uint64_t index, double time);

/* The currently scheduled time for an event */
inline double cmb_event_time(const uint64_t index) {
    return cmi_event_queue[index].time;
}

/* cmb_event_reprioritize: Reprioritizes event to another priority level */
extern void cmb_event_reprioritize(uint64_t index, int16_t priority);

/* The current priority for an event */
inline int16_t cmb_event_priority(const uint64_t index) {
    return cmi_event_queue[index].priority;
}

/*
 * cmb_event_find: Find event in event list and return index, zero if not found.
 * CMB_ANY_* are wildcarda, matching any value in its position.
 */
static_assert(sizeof(cmb_event_func *) == sizeof(void*));
#define CMB_ANY_ACTION ((cmb_event_func *)0xFFFFFFFFFFFFFFFF)
#define CMB_ANY_SUBJECT ((void *)0xFFFFFFFFFFFFFFFF)
#define CMB_ANY_OBJECT ((void *)0xFFFFFFFFFFFFFFFF)

extern uint64_t cmb_event_find(cmb_event_func *action,
                               const void *subject,
                               const void *object);

/* Print the current content of the event queue, mostly for debugging use. */
extern void cmb_event_queue_print(FILE *fp);

#endif /* CIMBA_CMB_EVENT_H */