/*
 * cmi_resource.h - guarded resources that the processes can queue for.
 *
 * A generic resource consists of two or three parts:
 * - A front end (the guard) that contains the priority queue for processes
 *   that want to use the resource and may have to wait for availability
 * - A middle part (the core) that is the actual resource, perhaps as simple as
 *   a limited number of available slots (a semaphore). This part also maintains
 *   a list of processes currently using the resource.
 * - Optionally, a back end symmetrical to the front end for processes waiting
 *   to refill the resource core. For example, the core could be a fixed size
 *   buffer between two machines in a workshop, where the upstream machine may
 *   have to wait for space in the buffer and the downstream machine may have to
 *   wait for parts in the buffer.
 *
 * A process will register itself and a predicate demand function when first
 * joining the priority queue. The demand function evaluates whether the
 * necessary condition to grab the resource is in place, such as at least one
 * part being available in a buffer or semaphore slot being available. If true
 * initially, the wait returns immediately. If not, the process waits in line.
 * When some other process signals the resource, it evaluates the demand
 * function for the first process in the priority queue. If true, the process is
 * resumed and can grab the resource. When done, it puts it back and signals the
 * quard to evaluate waiting demand again.
 *
 * A process holding a resource may in some cases be preempted by a higher
 * priority process. It is for this purpose that the resource core maintains a
 * list of processes currently holding (parts of) the resource, to enable use
 * cases like machine breakdowns or priority interrupts.
 *
 * Below, we describe the cmb_resourcequard and cmb_resourcecore "classes", and
 * combine these to generic models of semaphore-type resources, finite-capacity
 * buffers, and finite-sized object queues. A user application can extend this
 * further using inheritance by composition, also used in the code below.
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

#ifndef CIMBA_CMB_RESOURCE_H
#define CIMBA_CMB_RESOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmi_hashheap.h"
#include "cmb_process.h"
#include "cmi_processtag.h"

struct cmb_resource;

struct cmi_resource_guard {
    struct cmi_hashheap priority_queue;
};

typedef bool (cmb_resource_demand_func)(struct cmb_resource *res,
                                        struct cmb_process *pp,
                                        void *ctx);

/*
 * We have up to four 64-bit payload fields in the hash_heap entries.
 * Mapping:
 * item[0] - pointer to the process itself
 * item[1] - pointer to its demand function
 * item[2] - its context pointer
 * item[3] - not used for now
 */

/*
 * cmi_resource_guard_create : Allocate memory for a resource guard object
 */
struct cmi_resource_guard *cmi_resource_guard_create(void);

/*
 * cmi_resource_guard_initialize : Make an already allocated resource guard
 * object ready for use. Separated from the _create function to allow
 * inheritance by composition.
 */
extern void cmi_resource_guard_initialize(struct cmi_resource_guard *rgp);

/*
 * cmi_resource_guard_terminate : Un-initializes a resource guard object.
 * Separated from the _destroy function to allow inheritance by composition.
 */
extern void cmi_resource_guard_terminate(struct cmi_resource_guard *rgp);

/*
 * cmi_resource_guard_destroy : Deallocates (frees) memory allocated to a
 * resource guard object by _create. Separated from the _destroy function to
 * allow inheritance by composition, i.e. avoid freeing the same memory twice.
 */
extern void cmi_resource_guard_destroy(struct cmi_resource_guard *rgp);

/*
 * cmi_resource_guard_wait : Enqueue and suspend the calling process until it
 * reaches the front of the priority queue and its demand function returns true.
 * ctx is whatever context the demand function needs to evaluate if it is
 * satisfied or not, such as the number of units needed from the resource or
 * something more complex and user application defined.
 * Returns whatever signal was received when the process was reactivated.
 * Cannot be called from the main process.
 */
extern int64_t cmi_resource_guard_wait(struct cmi_resource_guard *rgp,
                                       struct cmb_resource *rp,
                                       cmb_resource_demand_func *demand,
                                       void *ctx);

/*
 * cmi_resource_guard_signal : Plings the bell for a resource guard to check if
 * any of the waiting processes should be resumed. Will evaluate the demand
 * function for the first process in the queue, if any, and will resume it if
 * (and only if) its demand function (*demand)(pp, rp, ctx) returns true.
 *
 * Resumes zero or one waiting processes. Call it again if there is a chance
 * that more than one process could be ready, e.g. if some process just returned
 * five units of a resource and there are several processes waiting for one
 * unit each.
 *
 * In cases where some waiting process needs to bypass another, e.g. if there
 * are three available units of the resource, the first process in the queue
 * demands five, and there are three more behind it that demands one each, it is
 * up to the application to dynamically change process priorities to bring the
 * correct process to the front of the queue. Use
 */
extern void cmi_resource_guard_signal(struct cmi_resource_guard *rgp,
                                      int64_t sig);

/*
 * cmi_resource_guard_cancel : Remove this process from the priority queue
 * without resuming it.
 */
extern uint64_t cmi_resource_guard_cancel(struct cmi_resource_guard *rgp,
                                          struct cmb_process *pp);

#endif // CIMBA_CMB_RESOURCE_H
