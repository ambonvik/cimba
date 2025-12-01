/**
 * @file cmb_resource.h
 * @brief A simple binary semaphore supporting acquire, release, and preempt
 *        methods. Can only be held by one process at a time. Assigned to
 *        waiting processes in priority order, then FIFO tie-breaker order.
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

#ifndef CIMBA_CMB_RESOURCE_H
#define CIMBA_CMB_RESOURCE_H

#include <stdint.h>

#include "cmb_process.h"
#include "cmb_holdable.h"
#include "cmb_resourceguard.h"


/**
 * @brief The resource struct, inherits all properties from `cmi_holdable` by
 * composition and adds the resource guard, a single pointer to the process
 * holding the resource (if currently held), and a timeseries for logging its
 * history.
 */
struct cmb_resource {
    struct cmb_holdable core;           /**< The virtual base class */
    struct cmb_resourceguard guard;     /**< The gatekeeper maintaining an orderly queue of waiting processes */
    struct cmb_process *holder;         /**< The current holder, if any */
    bool is_recording;                  /**< Is it currently recording history? */
    struct cmb_timeseries history;      /**< The usage history, 1 for held, 0 for idle */
};

/**
 * @brief Allocate memory for a resource object.
 *
 * @return Pointer to the newly created resource.
 */
extern struct cmb_resource *cmb_resource_create(void);

/**
 * @brief Make an allocated resource object ready for use.
 *
 * @param rp Pointer to an already allocated resource object.
 * @param name A null-terminated string naming the resource.
 */
extern void cmb_resource_initialize(struct cmb_resource *rp,
                                    const char *name);

/**
 * @brief Un-initializes a resource object.
 *
 * @param rp Pointer to an already allocated resource object.
 */
extern void cmb_resource_terminate(struct cmb_resource *rp);

/**
 * @brief  Deallocates memory for a resource object.
 *
 * @param rp Pointer to an already allocated resource object.
 */
extern void cmb_resource_destroy(struct cmb_resource *rp);

/**
 * @brief  Request and if necessary make the current process wait for the
 *         resource. Returns immediately if available.
 *
 * @param rp Pointer to an initialized resource object.
 *
 * @return  `CMB_PROCESS_SUCCESS` if all is well, otherwise the signal value
 *          received when interrupted or preempted.
 */
extern int64_t cmb_resource_acquire(struct cmb_resource *rp);

/**
 * @brief   Release the resource.
 *
 * @param rp Pointer to an initialized resource object.
 */
extern void cmb_resource_release(struct cmb_resource *rp);


/**
 * @brief Preempt the current holder and grab the resource if the calling
 *        process has higher priority than the current holder. Otherwise,
 *        it will politely wait for its turn.
 *
 * @param rp Pointer to an initialized resource object.
 *
 * @return  `CMB_PROCESS_SUCCESS` if all is well, otherwise the signal value
 *          received when interrupted or preempted.
 */
extern int64_t cmb_resource_preempt(struct cmb_resource *rp);

/**
 * @brief Returns name of resource as const char *.
 *
 * @param rp Pointer to an initialized resource object.
 * @return The name of the process as a null-terminated text string.
 */
static inline const char *cmb_resource_get_name(struct cmb_resource *rp)
{
    cmb_assert_debug(rp != NULL);

    const struct cmb_resourcebase *rbp = (struct cmb_resourcebase *)rp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return rbp->name;
}

/**
 * @brief Returns number of resources currently in use
 *
 * @param rp Pointer to resource
 * @return The number of units in use, 0 or 1
 */
static inline uint64_t cmb_resource_in_use(struct cmb_resource *rp)
{
    cmb_assert_debug(rp != NULL);
    cmb_assert_release(((struct cmb_resourcebase *)rp)->cookie == CMI_INITIALIZED);

    return (rp->holder != NULL) ? 1u : 0u;
}

/**
 * @brief Returns number of currently available resources
 *
 * @param rp Pointer to resource
 * @return The number of units not in use, 0 or 1
 */
static inline uint64_t cmb_resource_available(struct cmb_resource *rp)
{
    cmb_assert_debug(rp != NULL);
    cmb_assert_release(((struct cmb_resourcebase *)rp)->cookie == CMI_INITIALIZED);

    return (rp->holder == NULL) ? 1u : 0u;
}

/**
 * @brief Turn on data recording.
 *
 * @param rp Pointer to an initialized resource object.
 */
extern void cmb_resource_start_recording(struct cmb_resource *rp);

/**
 * @brief Turn off data recording.
 *
 * @param rp Pointer to an initialized resource object.
 */
extern void cmb_resource_stop_recording(struct cmb_resource *rp);

/**
 * @brief Get the recorded timeseries of resource usage.
 *
 * @param rp Pointer to an initialized resource object.
 * @return Pointer to a `cmb_timeseries` containing the resource usage history.
 */
extern struct cmb_timeseries *cmb_resource_get_history(struct cmb_resource *rp);

/**
 * @brief Print a simple text mode report of the resource usage, ncluding key
 *        statistical metrics and a histogram. Mostly intended for debugging
 *        purposes, not presentation graphics.
 *
 * @param rp Pointer to an initialized resource object.
 * @param fp File pointer, possibly `stdout`.
 */
extern void cmb_resource_print_report(struct cmb_resource *rp, FILE *fp);


#endif /* CIMBA_CMB_RESOURCE_H */
