/**
 * @file cmb_condition.h
 * @brief A condition variable class that allows a process to wait for an
 *        arbitrary condition to become true and be reactivated at that point.
 *        It does not assign any resource, just signals that the condition is
 *        fulfilled. The application provides the demand predicate function to
 *        be evaluated.
 *
 * Classes like `cmb_resource` or `cmb_buffer` use pre-packaged demand functions
 * for simple conditions (such as "buffer level greater than zero") and update
 * the resource state accordingly (such as decrementing the buffer level by the
 * correct amount). When using the `cmb_condition` instead, the user application
 * provides the demand predicate function and takes the correct action when
 * a waiting process is reactivated. The demand predicate function can even be
 * different for each waiting process. It will be evaluated for each waiting
 * process separately.
 *
 * When signaled, the condition variable evaluates the predicate function for
 * all waiting processes and reactivates all that evaluate to `true`. The
 * condition variable cannot know what happens next, so it is the calling
 * processes' own responsibility to recheck the condition and wait again if it
 * no longer is satisfied. This is different from classes like `cmb_resource`,
 * where we can assign the resource to the acquiring process and know that no
 * other processes need to be awakened.
 *
 * Recall that in a discrete event simulation, the state can only change at an
 * event. By registering itself as an observer at some other resource guard, the
 * condition variable will receive a signal whenever something has changed, can
 * re-evaluate the demand functions for its waiting processes, and reactivate as
 * justified.
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

#ifndef CIMBA_CMB_CONDITION_H
#define CIMBA_CMB_CONDITION_H

#include "cmi_resourcebase.h"
#include "cmb_resourceguard.h"

/**
 * @brief A condition variable class that allows a process to wait for an
 *        arbitrary condition to become true and be reactivated at that point.
 *        It does not assign any resource, just signals that the condition is
 *        fulfilled. The application provides the demand predicate function to
 *        be evaluated.
 */
struct cmb_condition {
    struct cmi_resourcebase base;           /**< The parent class, providing name and initialization */
    struct cmb_resourceguard guard;         /**< Providing the queueing mechanics */
};

/**
 * @brief Function prototype for the condition predicate function, taking a
 *        pointer to the condition (allowing usage by derived classes), a
 *        pointer to the process, and a `void *` to any context the predicate
 *        function needs to determine a `true` or `false` result.
 *
 * Same as the `cmb_resourceguard_demand_func`, except the first argument type,
 * which only needs a typecast to reach the base class.
 *
 * @param cnd Pointer to a condition object.
 * @param prc Pointer to a process
 * @param ctx Pointer to whatever context is needed to determine the outcome.
 *
 * @return `true` if the demand is considered satisfied, `false` if not.
*/
typedef bool (cmb_condition_demand_func)(const struct cmb_condition *cnd,
                                         const struct cmb_process *prc,
                                         const void *ctx);

/**
 * @brief  Allocate memory for a condition variable.
 *
 * @memberof cmb_condition
 * @return Pointer to an allocated condition variable.
 */
extern struct cmb_condition *cmb_condition_create(void);

/**
 * @brief  Make an allocated condition variable ready for use.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to an allocated condition variable.
 * @param name A null-terminated string naming the condition variable.
 */
extern void cmb_condition_initialize(struct cmb_condition *cvp,
                                     const char *name);

/**
 * @brief  Un-initializes a condition variable.
 *
 * @param cvp Pointer to an allocated condition variable.
 */
extern void cmb_condition_terminate(struct cmb_condition *cvp);

/**
 * @brief Deallocates memory for a condition variable.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to an allocated condition variable.
 */
extern void cmb_condition_destroy(struct cmb_condition *cvp);

/**
 * @brief Make the current process wait for the given demand to be satisfied,
 *        expressed as a predicate function that returns a boolean answer based
 *        on whatever state.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to a condition variable.
 * @param dmnd The demand predicate function.
 * @param ctx The context argument to the demand predicate function.
 *
 * @return `CMB_PROCESS_SUCCESS` if successful, otherwise the signal received
 *         when interrupted.
 */
extern int64_t cmb_condition_wait(struct cmb_condition *cvp,
                                  cmb_condition_demand_func *dmnd,
                                  const void *ctx);

/**
 * @brief Re-evaluate the demand predicate for all waiting processes and
 *        reactivate those that evaluate as `true`.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to a condition variable.
 */
extern bool cmb_condition_signal(struct cmb_condition *cvp);

/**
 * @brief Remove the process from the priority queue and resume it with a
 *        `CMB_PROCESS_CANCELLED` signal.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to a condition variable.
 * @param pp Pointer to a process, presumably waiting for the condition
 *
 * @return `true` if the found, `false` if not.
 */
extern bool cmb_condition_cancel(struct cmb_condition *cvp,
                                 struct cmb_process *pp);

/**
 * @brief Remove the process from the priority queue without resuming it. Used
 *        e.g., when stopping a process and cancelling its appointments.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to a condition variable.
 * @param pp Pointer to a process, presumably waiting for the condition
 *
 * @return `true` if the found, `false` if not.
 */
extern bool cmb_condition_remove(struct cmb_condition *cvp,
                                 const struct cmb_process *pp);

/**
 * @brief Subscribe this condition variable to signals from the other resource
 *        guard.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to a condition variable.
 * @param rgp Pointer to a resource guard.
 */
static inline void cmb_condition_subscribe(struct cmb_condition *cvp,
                                           struct cmb_resourceguard *rgp) {
    cmb_assert_release(cvp != NULL);
    cmb_assert_release(rgp != NULL);

    cmb_resourceguard_register(rgp, (struct cmb_resourceguard *)cvp);
}

/**
 * @brief Unsubscribe this condition variable to signals from the other
 *        resource guard.
 *
 * @memberof cmb_condition
 * @param cvp Pointer to a condition variable.
 * @param rgp Pointer to a resource guard.
 *
 * @return `true` if the found, `false` if not.
 */
static inline bool cmb_condition_unsubscribe(struct cmb_condition *cvp,
                                             struct cmb_resourceguard *rgp) {
    cmb_assert_release(cvp != NULL);
    cmb_assert_release(rgp != NULL);

    return cmb_resourceguard_unregister(rgp, (struct cmb_resourceguard *)cvp);
}

#endif /* CIMBA_CMB_CONDITION_H */
