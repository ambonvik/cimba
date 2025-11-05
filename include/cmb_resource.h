/*
 * cmi_resource.h - guarded resources that the processes can queue for, enabling
 * complex process interactions modelling real-world patterns.
 *
 * Implements four externally available "classes".
 * - cmb_resource - a simple binary semaphore supporting acquire, release, and
 *   preempt methods. Can only be held by one process at a time. Assigned to
 *   waiting processes in priority order, then FIFO tie-breaker order.
 *
 * - cmb_store - a counting semaphore that supports acquire, release, and
 *   preempt in specific amounts against a fixed resource capacity, where a
 *   process also can acquire more of a resource it already holds some amount
 *   of, or release parts of its holding. Several processes can be holding parts
 *   of the resource capacity at the same time, possibly also different amounts.
 *   Assigns in a greedy fashion, where the acquiring process will first grab
 *   whatever amount is available, then wait for some more to become available,
 *   and repeat until the requested amount is acquired.
 *
 * - cmb_buffer - a two-headed fixed-capacity resource where one or more
 *   producer processes can put an amount into the one end, and one or more
 *   consumer processes can get amounts out of the other end. If enough space is
 *   not available, the producers wait, and if there is not enough content, the
 *   consumers wait. Twice the complexity of cmb_store.
 *
 * - cmb_queue - as the cmb_buffer, but the content is represented by a linked
 *   list of pointers to void, allowing arbitrary objects to be passed between
 *   the producer and consumer.
 *
 * These classes are built on a virtual base class cmi_resource_base, allowing
 * processes to maintain lists of held resources without necessarily caring
 * which exact resource class this is. The priority queues for waiting processes
 * are instances of the class cmi_resource_guard, derived from cmi_hashheap.
 *
 * A process will register itself and a predicate demand function when first
 * joining the priority queue. The demand function evaluates whether the
 * necessary condition to grab the resource is in place, such as at least one
 * part being available in a buffer or store slot being available. If true
 * initially, the wait returns immediately. If not, the process waits in line.
 *
 * When some other process signals the resource, it evaluates the demand
 * function for the first process in the priority queue. If true, the process is
 * resumed and can grab the resource. When done, it puts it back and signals the
 * quard to evaluate waiting demand again.
 *
 * A process holding a resource may in some cases be preempted by a higher
 * priority process. For this purpose, the resources maintain a list of
 * processes currently holding (parts of) the resource, to enable use cases like
 * machine breakdowns, priority interrupts, or holding processes getting killed
 * in more violent use cases.
 *
 * Below, we implement the cmb_resource_guard and cmb_resource_base "classes",
 * then combining these to generic semaphore-type resources, finite-capacity
 * buffers, and finite-sized object queues. A user application can extend this
 * further using inheritance by composition, as used in the code below, and/or
 * by replacing the precedence-checking predicate functions guard_queue_check
 * and holder_queue_check with priority ranking more suitable for the specific
 * purpose.
 *
 * We have tried to build simple and clear resource assignment algorithms. It is
 * not a goal to build "optimal" or guaranteed deadlock-free resource allocation,
 * since this is a simulation library, not an operating system. If the details
 * of resource allocation strategies are important in your use case, kindly make
 * the necessary extensions or modifications. It might be your research goal to
 * investigate the effects of different algorithms here. (Your humble author has
 * written both a Master's and a PhD thesis doing exactly that.)
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

/*******************************************************************************
 * cmi_resource_guard: The hashheap that handles a resource wait list
 ******************************************************************************/

struct cmi_resource_base;

struct cmi_resource_guard {
    struct cmi_hashheap priority_queue;
    struct cmi_resource_base *guarded_resource;
};

/*
 * typedef cmb_resource_demand_func : function prototype for a resource demand
 */
typedef bool (cmb_resource_demand_func)(const struct cmi_resource_base *res,
                                        const struct cmb_process *pp,
                                        const void *ctx);

/*
 * cmi_resource_guard_initialize : Make an already allocated resource guard
 * object ready for use.
 */
extern void cmi_resource_guard_initialize(struct cmi_resource_guard *rgp,
                                          struct cmi_resource_base *rbp);

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
 * and resume it with a CMB_PROCESS_CANCELLED signal.
 * Returns true if the found, false if not.
 */
extern bool cmi_resource_guard_cancel(struct cmi_resource_guard *rgp,
                                      struct cmb_process *pp);

/*******************************************************************************
 * cmi_resource_base: The virtual parent "class" of the different resource types
 ******************************************************************************/

/*
 * typedef cmi_resource_scram_func : function prototype for a resource scram,
 * to be used e.g. when a process is killed and needs to release all held
 * resources no matter what type these are. We let the resource base class
 * contain a pointer to a scram function and each derived class populate it with
 * a pointer to a function that does appropriate handling for the derived class.
 *
 * The process pointer argument is needed since the calling (current) process is
 * not the victim process here. The handle arg is for cases where the resource
 * can look it up in its hash map for efficiency.
 */
typedef void (cmi_resource_scram_func)(struct cmi_resource_base *res,
                                       const struct cmb_process *pp,
                                       uint64_t handle);

/* Maximum length of a resource name, anything longer will be truncated */
#define CMB_RESOURCE_NAMEBUF_SZ 32

struct cmi_resource_base {
    char name[CMB_PROCESS_NAMEBUF_SZ];
    cmi_resource_scram_func *scram;
};

/*
 * cmi_resource_base_initialize : Make an already allocated resource core
 * object ready for use.
 */
extern void cmi_resource_base_initialize(struct cmi_resource_base *rbp,
                                         const char *name);

/*
 * cmi_resource_base_terminate : Un-initializes a resource core object.
 */
extern void cmi_resource_base_terminate(struct cmi_resource_base *rcp);

/*
 * cmb_resource_set_name : Set a new name for the resource.
 *
 * The name is held in a fixed size buffer of size CMB_RESOURCE_NAMEBUF_SZ.
 * If the new name is too large for the buffer, it will be truncated at one less
 * than the buffer size, leaving space for the terminating zero char.
 */
extern void cmb_resource_base_set_name(struct cmi_resource_base *rbp,
                                       const char *name);

/*******************************************************************************
 * cmb_resource : A simple resource object, formally a binary semaphore
 ******************************************************************************/

struct cmb_resource {
    struct cmi_resource_base core;
    struct cmi_resource_guard front_guard;
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
 * Returns CMB_PROCESS_SUCCESS if all is well.
 */
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
extern int64_t cmb_resource_preempt(struct cmb_resource *rp);

/*
 * cmb_resource_get_name : Returns name of resource as const char *.
 */
static inline const char *cmb_resource_get_name(struct cmb_resource *rp)
{
    cmb_assert_debug(rp != NULL);

    const struct cmi_resource_base *rbp = (struct cmi_resource_base *)rp;

    return rbp->name;
}

/*******************************************************************************
 * cmb_store : Resource with integer-valued capacity, a counting semaphore
 ******************************************************************************/

/*
 * This class adds numeric values for capacity and usage to the basic resource.
 * These values are unsigned integers to avoid any rounding issues from
 * floating-point calculations, both faster and higher resolution (if scaled
 * properly to 64-bit range).
 *
 * The holders list is now a hashheap, since we may need to handle many separate
 * processes acquiring, holding, releasing, and preempting various amounts of
 * the resource capacity. The hashheap is sorted to keep the holder most likely
 * to be preempted at the front, i.e. lowest priority and last in.
 */
struct cmb_store {
    struct cmi_resource_base core;
    struct cmi_resource_guard front_guard;
    struct cmi_hashheap holders;
    uint64_t capacity;
    uint64_t in_use;
};

/*
 * cmb_store_create : Allocate memory for a store object.
 */
extern struct cmb_store *cmb_store_create(void);

/*
 * cmb_store_initialize : Make an allocated store object ready for use.
 */
extern void cmb_store_initialize(struct cmb_store *sp,
                                    const char *name,
                                    uint64_t capacity);

/*
 * cmb_store_terminate : Un-initializes a store object.
 */
extern void cmb_store_terminate(struct cmb_store *sp);

/*
 * cmb_store_destroy : Deallocates memory for a store object.
 */
extern void cmb_store_destroy(struct cmb_store *sp);

/*
 * cmb_store_acquire : Request and if necessary wait for an claim_amount of the
 * store resource. The calling process may already hold some and try to
 * increase its holding with this call, or to obtain its first helping.
 *
 * Will either get the required amount and return CMB_PROCESS_SUCCESS,
 * or fail and return some other value. If failed, the calling process has the
 * same amount of the resource as before the call. There is no partial
 * fulfillment, just all or nothing.
 */
extern int64_t cmb_store_acquire(struct cmb_store *sp, uint64_t amount);

/*
 * cmb_store_preempt : Preempt the current holders and grab the resource
 * amount, starting from the lowest priority holder. If there is not enough to
 * cover the amount before it runs into holders with equal or higher priority
 * than the caller, it will politely wait in line for the remainder. It only
 * preempts processes with strictly lower priority than itself, otherwise acts
 * like cmb_store_acquire.
 *
 * As for cmb_store_acquire, it is full amount or nothing.
 */
extern int64_t cmb_store_preempt(struct cmb_store *sp, uint64_t amount);

/*
 * cmb_store_release : Release an amount of the resource, not necessarily
 * everything that the calling process holds.
 */
extern void cmb_store_release(struct cmb_store *sp, uint64_t amount);

/*
 * cmb_store_get_name : Returns name of store as const char *.
 */
static inline const char *cmb_store_get_name(struct cmb_store *sp)
{
    cmb_assert_debug(sp != NULL);

    const struct cmi_resource_base *rbp = (struct cmi_resource_base *)sp;

    return rbp->name;
}

#endif // CIMBA_CMB_RESOURCE_H
