/**
 * @file cmb_resourceguard.h
 * @brief The gatekeeper class for resources a process can wait for. It is
 *        derived from `cmi_hashheap` and inherits its methods,
 *        adding a pointer to the resource it guards and a list of any observer
 *        resource guards that get signals forwarded from this one.
 *
 * Note that there is no `cmb_resourceguard_create()` or
 * `cmb_resourceguard_destroy()`. Always embedded in some other object, such as
 * a resource or condition, never on its own.
 */

/*
 *Copyright (c) Asbjørn M. Bonvik 2025-26.
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

#ifndef CIMBA_CMB_RESOURCEGUARD_H
#define CIMBA_CMB_RESOURCEGUARD_H

#include <stdbool.h>
#include <stdint.h>

#include "cmb_process.h"

#include "cmi_hashheap.h"
#include "cmi_slist.h"

/**
 * @brief The gatekeeper class for resources a process can wait for. It is
 *        derived from `cmi_hashheap` and inherits its methods,
 *        adding a pointer to the resource it guards and a list of any observer
 *        resource guards that get signals forwarded from this one.
 */
struct cmb_resourceguard {
    struct cmi_hashheap priority_queue;         /**< The base hashheap class */
    struct cmi_resourcebase *guarded_resource;  /**< The resource it guards */
    struct cmi_slist_head observers;            /**< Any other resource guards observing this one */
};

/**
 * @brief Function prototype for a resource demand predicate.
 *
 * @relates cmb_resourceguard
 * @param rbp Pointer to a resource base object.
 * @param pp Pointer to a process
 * @param ctx Pointer to whatever context is needed to determine the outcome.
 * @return `true` if the demand is considered satisfied (e.g., a resource is
 *         available), `false` if not.
 */
typedef bool (cmb_resourceguard_demand_func)(const struct cmi_resourcebase *rbp,
                                             const struct cmb_process *pp,
                                             const void *ctx);

/**
 * @brief Make a resource guard ready for use.
 *
 * @relates cmb_resourceguard
 * @param rgp Pointer to a resource guard.
 * @param rbp Pointer to the thing it will be guarding.
 */
extern void cmb_resourceguard_initialize(struct cmb_resourceguard *rgp,
                                         struct cmi_resourcebase *rbp);

/**
 * @brief  Un-initializes a resource guard.
 *
 * @relates cmb_resourceguard
 * @param rgp Pointer to a resource guard.
 */
extern void cmb_resourceguard_terminate(struct cmb_resourceguard *rgp);

/**
 * @brief  Enqueue and suspend the calling process until it reaches the front of
 *         the priority queue and its demand function returns true.
 *
 * `ctx` is whatever context the demand function needs to evaluate if it is
 * satisfied or not, such as the number of units needed from the resource or
 * something more complex and user application defined.
 * Returns whatever signal was received when the process was reactivated.
 * Cannot be called from the main process.
 *
 * @relates cmb_resourceguard
 * @param rgp Pointer to a resource guard.
 * @param demand Pointer to the demand predicate function
 * @param ctx The context argument to the demand predicate function.
 */
extern int64_t cmb_resourceguard_wait(struct cmb_resourceguard *rgp,
                                      cmb_resourceguard_demand_func *demand,
                                      const void *ctx);

/**
 * @brief  Ring the bell for a resource guard to check if any of the waiting
 *         processes should be resumed. Will evaluate the demand function for
 *         the first process in the queue, if any, and will resume it if
 *         (and only if) its demand function `(*demand)(rp, pp, ctx)` returns
 *         `true`.
 *
 * Resumes zero or one waiting processes. Call it again if there is a chance
 * that more than one process could be ready, e.g., if some process just returned
 * five units of a resource and there are several processes waiting for one
 * unit each.
 *
 * Returns `true` if some process was resumed, `false` otherwise, hence easy to
 * wrap in a loop like `while (cmb_resource_guard_signal(rgp)) { ... }`
 *
 * By default, Cimba does not allow potential priority inversion where a
 * sequence of lower-priority processes could starve a higher-priority process
 * indefinitely. In cases where some waiting process needs to bypass another,
 * e.g., if there are three available units of the resource, the first process in
 * the queue demands five, and there are three more behind it that demands one
 * each, it is up to the application to dynamically change process priorities to
 * bring the correct process to the front of the queue and make it eligible to
 * resume. If this sort of thing is important in your use case, you probably
 * want to write that code yourself.
 *
 * @relates cmb_resourceguard
 * @param rgp Pointer to a resource guard.
 */
extern bool cmb_resourceguard_signal(struct cmb_resourceguard *rgp);

/**
 * @brief Remove this process from the priority queue and resume it with a
 *        `CMB_PROCESS_CANCELLED` signal.
 *
 * @param rgp Pointer to a resource guard.
 * @param pp Pointer to a process
 *
 * @relates cmb_resourceguard
 * @return `true` if the process was in the queue, `false` if not.
 */
extern bool cmb_resourceguard_cancel(struct cmb_resourceguard *rgp,
                                     struct cmb_process *pp);

/**
 * @brief Remove this process from the priority queue without resuming it.
 *
 * @param rgp Pointer to a resource guard.
 * @param pp Pointer to a process
 *
 * @relates cmb_resourceguard
 * @return `true` if the process was in the queue, `false` if not.
 */
extern bool cmb_resourceguard_remove(struct cmb_resourceguard *rgp,
                                     const struct cmb_process *pp);

/**
 * @brief Register another resource guard as an observer of this one, forwarding
 * signals and causing the observer to evaluate its demand predicates as well.
 *
 * @relates cmb_resourceguard
 * @param rgp Pointer to the subject resource guard.
 * @param obs Pointer to an observer resource guard.
 */
extern void cmb_resourceguard_register(struct cmb_resourceguard *rgp,
                                       struct cmb_resourceguard *obs);

/**
 * @brief Unregister another resource guard as an observer of this one.
 *
 * @relates cmb_resourceguard
 * @param rgp Pointer to the subject resource guard.
 * @param obs Pointer to an observer resource guard.
 * @return `true` if the observer was registered, `false` if not.
 */
extern bool cmb_resourceguard_unregister(struct cmb_resourceguard *rgp,
                                         const struct cmb_resourceguard *obs);

#endif /* CIMBA_CMB_RESOURCEGUARD_H */
