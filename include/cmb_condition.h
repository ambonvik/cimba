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
 * When signalled, the condition variable evaluates the predicate function for
 * all waiting processes and reactivates all that evaluate to `true`. The
 * condition variable cannot know what happens next, so it is the calling
 * processes' own responsibility to recheck the condition and wait again if it
 * no longer is satisfied. This is different from classes like `cmb_resource`,
 * where we can assign the resource to the acquiring process and know that no
 * other processes need to be awakened.
 *
 * The `cmb_condition` also provides methods to subscribe to signals from other
 * resource guard objects and to unsubscribe. Recall that in a discrete event
 * simulation, the state can only change at an event. By registering itself at
 * some resource guard, the condition variable receives a signal whenever
 * something has changed, can re-evaluate the demand functions for its waiting
 * processes, and reactivate as justified. Strictly speaking, this is an
 * "observer" design pattern, not "subscriber" (since the observed subject is
 * aware that the observers exist).
 *
 * When registering observers, do not create any cycles where e.g. condition A
 * gets signalled from B, B gets signalled from C, and C gets signalled from A.
 * That will not end well.
 */

/*
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

#ifndef CIMBA_CMB_CONDITION_H
#define CIMBA_CMB_CONDITION_H

#include "cmi_resourceguard.h"

/* Maximum length of a resource name, anything longer will be truncated */
#define CMB_CONDIITON_NAMEBUF_SZ 32


/**
 * @brief The condition struct, basically a named resource guard. It inherits
 *        the properties of the resource guard, including a hash heap of waiting
 *        processes, each with a pointer to its demand function and the
 *        corresponding context argument.
 */
struct cmb_condition {
    struct cmi_resourceguard guard;      /**< The parent class */
    char name[CMB_CONDIITON_NAMEBUF_SZ]; /**< The process name string */
};

/**
 * @brief Function prototype for the condition predicate function, taking a
 *        pointer to the condition (allowing usage by derived classes), a
 *        pointer to the process, and a `void *` to basically any context the
 *        predicate function needs to determine a `true` or `false` result.
 */
typedef bool (cmb_condition_demand_func)(const struct cmb_condition *cnd,
                                         const struct cmb_process *prc,
                                         const void *ctx);


/**
 * @brief  Allocate memory for a condition variable.
 *
 * @return Pointer to an allocated condition variable.
 */
extern struct cmb_condition *cmb_condition_create(void);

/**
 * @brief  Make an allocated condition variable ready for use.
 *
 * @param cvp Pointer to an allocated condition variable.
 * @param name A null-terminated string naming the condition variable.
 */
extern void cmb_condition_initialize(struct cmb_condition *cvp,
                                         const char *name,
                                         uint64_t capacity);

/**
 * @brief  Un-initializes a condition variable.
 *
 * @param cvp Pointer to an allocated condition variable.
 */
extern void cmb_condition_terminate(struct cmb_condition *cvp);

/**
 * @brief Deallocates memory for a condition variable.
 *
 * @param cvp Pointer to an allocated condition variable.
 */
extern void cmb_condition_destroy(struct cmb_condition *cvp);

/**
 * @brief Wait for the given demand to be satisfied, expressed as a predicate
 *        function that returns a boolean answer based on whatever state.
 *
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
 * @brief Cause the condition variable to re-evaluate the demand predicate for
 *        all waiting processes and reacivate those that evaluate as `true`.
 *
 * @param cvp Pointer to a condition variable.
 */
extern bool cmb_condition_signal(struct cmb_condition *cvp);

/**
 * @brief Register for signals from some other resource guard.
 *
 * @param cvp Pointer to a condition variable.
 * @param rgp Pointer to a resource guard.
 */
extern bool cmb_condition_register(struct cmb_condition *cvp,
                                   struct cmi_resourceguard *rgp);

/**
 * @brief Un-register for signals from some other resource guard.
 *
 * @param cvp Pointer to a condition variable.
 * @param rgp Pointer to a resource guard.
 */
extern bool cmb_condition_unregister(struct cmb_condition *cvp,
                                     struct cmi_resourceguard *rgp);


#endif /* CIMBA_CMB_CONDITION_H */
