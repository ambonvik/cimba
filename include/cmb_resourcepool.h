/**
 * @file cmb_resourcepool.h
 * @brief A counting semaphore that supports acquire(), release(), and preempt()
 *        in specific amounts against a fixed resource capacity, where a process
 *        also can acquire more of a resource it already holds some amount of,
 *        or release parts of its holding. Several processes can be holding
 *        parts of the resource capacity at the same time, possibly also
 *        different amounts.
 *
 * The resource pool adds numeric values for capacity and usage to the
 * simple `cmb_resource`. These values are unsigned integers to avoid any
 * rounding issues from floating-point calculations, both faster and higher
 * resolution (if scaled properly to 64-bit range).
 *
 * It assigns requested amounts to processes in a greedy fashion. The acquiring
 * process will first grab whatever amount is available, then wait for some more
 * to become available, repeat until the requested amount is acquired, and
 * it eventually returns from the call.
 *
 * Preemption is similar to acquisition, except that the preempting process also
 * will grab resources from any lower-priority processes holding some.
 *
 * The holders list is now a hashheap, since we may need to handle many separate
 * processes acquiring, holding, releasing, and preempting various amounts of
 * the resource capacity. The hashheap is sorted to keep the holder most likely
 * to be preempted at the front, i.e., the lowest priority and last in.
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

#ifndef CIMBA_CMB_RESOURCEPOOL_H
#define CIMBA_CMB_RESOURCEPOOL_H

#include <stdint.h>

#include "cmb_resourceguard.h"
#include "cmb_timeseries.h"

#include "cmi_holdable.h"

/**
 * @brief A counting semaphore that supports acquire(), release(), and preempt()
 *        in specific amounts against a fixed resource capacity, where a process
 *        also can acquire more of a resource it already holds some amount of,
 *        or release parts of its holding. Several processes can be holding
 *        parts of the resource capacity at the same time, possibly also
 *        different amounts.
 */
struct cmb_resourcepool {
    struct cmi_holdable core;           /**< The virtual base class */
    struct cmb_resourceguard guard;     /**< The gatekeeper maintaining an orderly queue of waiting processes */
    struct cmi_hashheap holders;        /**< The processes currently holding some, if any */
    uint64_t capacity;                  /**< The maximum amount that can be assigned to processes */
    uint64_t in_use;                    /**< The amount currently in use, less than or equal to the capacity */
    bool is_recording;                  /**< Is it currently recording history? */
    struct cmb_timeseries history;      /**< The usage history */
};

/**
 * @brief  Allocate memory for a resource pool.
 *
 * @relates cmb_resourcepool
 * @return Pointer to an allocated resource pool.
 */
extern struct cmb_resourcepool *cmb_resourcepool_create(void);

/**
 * @brief  Make an allocated resource pool ready for use.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to a resource pool.
 * @param name A null-terminated string naming the resource pool.
 * @param capacity The maximum amount that can be assigned at the same time.
 */
extern void cmb_resourcepool_initialize(struct cmb_resourcepool *rpp,
                                         const char *name,
                                         uint64_t capacity);

/**
 * @brief  Un-initializes a resource pool.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to a resource pool.
 */
extern void cmb_resourcepool_terminate(struct cmb_resourcepool *rpp);

/**
 * @brief Deallocates memory for a resource pool.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to an allocated resource pool object.
 */
extern void cmb_resourcepool_destroy(struct cmb_resourcepool *rpp);

/**
 * @brief Return the amount of this pool that is currently held by the given
 *        process, possibly zero.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to a resource pool.
 * @param pp Pointer to a `cmb_process`
 * @return The amount from this resource pool that is held by the process.
 */
extern uint64_t cmb_resourcepool_held_by_process(const struct cmb_resourcepool *rpp,
                                                 const struct cmb_process *pp);

/**
 * @brief Request and, if necessary, wait for an amount of the resource pool.
 *        The calling process may already hold some and try to increase its
 *        holding with this call, or to acquire its first helping.
 *
 * It will either get the required req_amount and return `CMB_PROCESS_SUCCESS`,
 * be preempted and return `CMB_PROCESS_PREEMPTED`, or be interrupted and return
 * some other value. If it is preempted, the process lost everything it had and
 * returns empty-handed. If interrupted by any other signal, it returns with the
 * same req_amount as it had at the beginning of the call.
 *
 * Only the signal is returned, not the req_amount obtained or held. The calling
 * process needs to keep track of this itself based on the return signal values.
 * In particular, do not assume that the process has received the requested
 * req_amount when it returns.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to a resource pool.
 * @param req_amount The requested amount.
 * @return `CMB_PROCESS_SUCCESS` if successful, otherwise the signal received
 *         when preempted or interrupted.
 */
extern int64_t cmb_resourcepool_acquire(struct cmb_resourcepool *rpp,
                                        uint64_t req_amount);

/**
 * @brief Preempt the current holders and grab the amount, starting from the
 *        lowest priority holder. If there is not enough to cover the amount
 *        before it runs into holders with equal or higher priority than the
 *        caller, it will politely wait in line for the remainder. It only
 *        preempts processes with strictly lower priority than itself, otherwise
 *        acts like `cmb_resourcepool_acquire()`.
 *
 * As for `cmb_resourcepool_acquire()`, it can either return with the requested
 * req_amount, an unchanged req_amount (interrupted), or nothing at all (preempted). This
 * function does not return the req_amount received or held, only the signal value.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to a resource pool.
 * @param req_amount The requested amount.
 * @return `CMB_PROCESS_SUCCESS` if successful, otherwise the signal received
 *         when preempted or interrupted.
 */
extern int64_t cmb_resourcepool_preempt(struct cmb_resourcepool *rpp,
                                         uint64_t req_amount);

/**
 * @brief Release an amount of the resource back to the pool, not necessarily
 *        everything that the calling process holds, but not more than it is
 *        currently holding. Always returns immediately.
 *
 * @relates cmb_resourcepool
 * @param rpp Pointer to a resource pool.
 * @param rel_amount The requested amount.
 */
extern void cmb_resourcepool_release(struct cmb_resourcepool *rpp,
                                      uint64_t rel_amount);

/**
 * @brief Returns name of pool as const char *.
 *
 * @memberof cmb_resourcepool
 * @param rsp Pointer to a resource pool.
 * @return A null-terminated string with the name of the resource pool.
 */
static inline const char *cmb_resourcepool_get_name(struct cmb_resourcepool *rsp)
{
    cmb_assert_debug(rsp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return rbp->name;
}

/**
 * @brief Returns the number of resources currently in use
 *
 * @memberof cmb_resourcepool
 * @param rsp Pointer to a resource pool
 * @return The number of units in use
 */
static inline uint64_t cmb_resourcepool_in_use(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)rsp)->cookie == CMI_INITIALIZED);
    cmb_assert_debug(rsp->in_use <= rsp->capacity);

    return rsp->in_use;
}

/**
 * @brief Returns the number of currently available resources
 *
 * @memberof cmb_resourcepool
 * @param rsp Pointer to a resource pool
 * @return The number of units not in use
 */
static inline uint64_t cmb_resourcepool_available(struct cmb_resourcepool *rsp)
{
    cmb_assert_release(rsp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)rsp)->cookie == CMI_INITIALIZED);
    cmb_assert_debug(rsp->in_use <= rsp->capacity);

    return (rsp->capacity - rsp->in_use);
}

/**
 * @brief Turn on data recording.
 *
 * @param rsp Pointer to a resource pool.
 */
extern void cmb_resourcepool_start_recording(struct cmb_resourcepool *rsp);

/**
 * @brief Turn off data recording.
 *
 * @relates cmb_resourcepool
 * @param rsp Pointer to a resource pool.
 */
extern void cmb_resourcepool_stop_recording(struct cmb_resourcepool *rsp);

/**
 * @brief Get the recorded timeseries of resource usage.
 *
 * @relates cmb_resourcepool
 * @param rsp Pointer to a resource pool.
 * @return Pointer to a `cmb_timeseries` containing the usage history.
 */
extern struct cmb_timeseries *cmb_resourcepool_get_history(struct cmb_resourcepool *rsp);

/**
 * @brief Print a simple text mode report of the resource usage, including key
 *        statistical metrics and a histogram. Mostly intended for debugging
 *        purposes, not presentation graphics.
 *
 * @relates cmb_resourcepool
 * @param rsp Pointer to a resource pool.
 * @param fp File pointer, possibly `stdout`.
 */
extern void cmb_resourcepool_print_report(struct cmb_resourcepool *rsp, FILE *fp);


#endif /* CIMBA_CMB_RESOURCEPOOL_H */
