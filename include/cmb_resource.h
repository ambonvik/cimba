/*
 * cmi_resource.h - guarded resources that the processes can queue for.
 *
 * A generic resource consists of two or three parts:
 * - A front end (the guard) that contains the priority queue for processes
 *   that want to use the resource and may have to wait for availability.
 *
 * - A middle part (the core) that is the actual resource, perhaps as simple as
 *   a limited number of available slots (a semaphore). This part also maintains
 *   a list of processes currently using the resource.
 *
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

#include "cmi_hashheap.h"
#include "cmb_process.h"

/******************************************************************************
 * cmi_resource_guard: The hashheap that handles a resource wait list
 *****************************************************************************/
struct cmi_resource_guard {
    struct cmi_hashheap priority_queue;
};

/*
 * typedef cmb_resource_demand_func : function prototype for a resource demand
 */
struct cmi_resource_base;

typedef bool (cmb_resource_demand_func)(const struct cmi_resource_base *res,
                                        const struct cmb_process *pp,
                                        const void *ctx);

/*
 * cmi_resource_guard_initialize : Make an already allocated resource guard
 * object ready for use.
 */
extern void cmi_resource_guard_initialize(struct cmi_resource_guard *rgp);

/*
 * cmi_resource_guard_terminate : Un-initializes a resource guard object.
 */
extern void cmi_resource_guard_terminate(struct cmi_resource_guard *rgp);

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
                                       cmb_resource_demand_func *demand,
                                       void *ctx);

/*
 * cmi_resource_guard_signal : Plings the bell for a resource guard to check if
 * any of the waiting processes should be resumed. Will evaluate the demand
 * function for the first process in the queue, if any, and will resume it if
 * (and only if) its demand function (*demand)(rp, pp, ctx) returns true.
 *
 * Resumes zero or one waiting processes. Call it again if there is a chance
 * that more than one process could be ready, e.g. if some process just returned
 * five units of a resource and there are several processes waiting for one
 * unit each.
 *
 * Returns true if some process was resumed, false otherwise, hence easy to
 * wrap in a loop like while (cmb_resource_guard_signal(rgp)) { ... }
 *
 * In cases where some waiting process needs to bypass another, e.g. if there
 * are three available units of the resource, the first process in the queue
 * demands five, and there are three more behind it that demands one each, it is
 * up to the application to dynamically change process priorities to bring the
 * correct process to the front of the queue and make it eligible to resume.
 * TODO: Ensure that process_set_priority triggers queue reshuffle
 */
extern bool cmi_resource_guard_signal(struct cmi_resource_guard *rgp);

/*
 * cmi_resource_guard_cancel : Remove this process from the priority queue
 * and resume it with a CMB_PROCESS_WAIT_CANCELLED signal.
 * Returns true if the found, false if not.
 */
extern bool cmi_resource_guard_cancel(struct cmi_resource_guard *rgp,
                                      struct cmb_process *pp);

/******************************************************************************
 * cmi_resource_base: The parent "class" of the different resource types
 *****************************************************************************/

/* Maximum length of a resource name, anything longer will be truncated */
#define CMB_RESOURCE_NAMEBUF_SZ 32

struct cmi_resource_base {
    char name[CMB_PROCESS_NAMEBUF_SZ];
    struct cmi_resource_guard front_guard;
};

/*
 * cmi_resource_base_initialize : Make an already allocated resource core
 * object ready for use
 */
extern void cmi_resource_base_initialize(struct cmi_resource_base *rbp,
                                         const char *name);

/*
 * cmi_resource_base_terminate : Un-initializes a resource core object.
 */
extern void cmi_resource_base_terminate(struct cmi_resource_base *rcp);

/*
 * cmb_resource_base_get_name : Return the process name as a const char *,
 * since it is kept in a fixed size buffer and should not be changed directly.
 *
 * If the name for some reason needs to be changed, use cmb_process_set_name to
 * do it safely.
 */
static inline const char *cmi_resource_base_get_name(const struct cmi_resource_base *rbp)
{
    cmb_assert_release(rbp != NULL);

    return rbp->name;
}

/*
 * cmb_resource_set_name : Set a new name for the resource.
 *
 * The name is held in a fixed size buffer of size  CMB_RESOURCE_NAMEBUF_SZ.
 * If the new name is too large for the buffer, it will be truncated at one less
 * than the buffer size, leaving space for the terminating zero char.
 */
extern void cmb_resource_base_set_name(struct cmi_resource_base *rbp,
                                       const char *name);

/******************************************************************************
 * cmb_resource : A simple resource object, formally a binary semaphore
 *****************************************************************************/
struct cmb_resource {
    struct cmi_resource_base core;
    struct cmb_process *holder;
};
/*
 * cmb_resource_create : Allocate memory for a resource object.
 */
extern struct cmb_resource *cmb_resource_create(void);

/*
 * cmb_resource_initialize : Make an allocated resource object ready for use.
 */
extern void cmb_resource_initialize(struct cmb_resource *rp,
                                    const char *name);

/*
 * cmb_resource_terminate : Un-initializes a resource object.
 */
extern void cmb_resource_terminate(struct cmb_resource *rp);

/*
 * cmb_resource_destroy : Deallocates memory for a resource object.
 */
extern void cmb_resource_destroy(struct cmb_resource *rp);

/*
 * cmb_resource_acquire : Request and if necessary wait for the resource.
 * Returns CMB_RESOURCE_ACQUIRE_NORMAL if all is well,
 * CMB_RESOURCE_ACQUIRE_CANCELLED if someone kicked us out of the queue.
 */
#define CMB_RESOURCE_ACQUIRE_NORMAL (0LL)
#define CMB_RESOURCE_ACQUIRE_CANCELLED (5LL)
extern int64_t cmb_resource_acquire(struct cmb_resource *rp);

/*
 * cmb_resource_release : Release the resource.
 */
extern void cmb_resource_release(struct cmb_resource *rp);

/*
 * cmb_resource_preempt : Preempt the current holder and grab the resource, if
 * the calling process has higher priority than the current holder. Otherwise,
 * it politely waits for its turn at the front gate.
 */
#define CMB_RESOURCE_HOLD_PREEMPTED (2LL)
extern int64_t cmb_resource_preempt(struct cmb_resource *rp);

#endif // CIMBA_CMB_RESOURCE_H
