/**
 * @file cmb_resourcestore.h
 * @brief A counting semaphore that supports acquire, release, and preempt in
 *        specific amounts against a fixed resource capacity, where a process
 *        also can acquire more of a resource it already holds some amount of,
 *        or release parts of its holding. Several processes can be holding
 *        parts of the resource capacity at the same time, possibly also
 *        different amounts.
 *
 * The `cmb_resourcestore` adds numeric values for capacity and usage to the
 * simple `cmb_resource`. These values are unsigned integers to avoid any
 * rounding issues from floating-point calculations, both faster and higher
 * resolution (if scaled properly to 64-bit range).
 *
 * It assigns amounts to processes in a greedy fashion, where the acquiring
 * process will first grab whatever amount is available, then wait for some more
 * to become available, and repeat until the requested amount is acquired and
 * it returns from the call.
 *
 * Preempt is similar to acquire, except that the prempting process also will
 * grab resources from any lower-priority processes holding some.
 *
 * The holders list is now a hashheap, since we may need to handle many separate
 * processes acquiring, holding, releasing, and preempting various amounts of
 * the resource capacity. The hashheap is sorted to keep the holder most likely
 * to be preempted at the front, i.e. lowest priority and last in.
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

#ifndef CIMBA_CMB_STORE_H
#define CIMBA_CMB_STORE_H

#include <stdint.h>

#include "cmi_holdable.h"
#include "cmi_resourceguard.h"

/**
 * @brief The resourcestore struct, inherits all properties from `cmi_holdable`
 * by composition and adds the resource guard, a hashheap of processes holding
 * some amount of the resource, and a timeseries for logging its history.
 */
struct cmb_resourcestore {
    struct cmi_holdable core;           /**< The virtual base class */
    struct cmi_resourceguard guard;     /**< The gatekeeper maintaining an orderly queue of waiting processes */
    struct cmi_hashheap holders;        /**< The processes currently holding some, if any */
    uint64_t capacity;                  /**< The maximum amount that can be assigned to processes */
    uint64_t in_use;                    /**< The amount currently in use, less than or equal to the capacity */
    bool is_recording;                  /**< Is it currently recording history? */
    struct cmb_timeseries history;      /**< The usage history */
};

/**
 * @brief  Allocate memory for a resource store object.
 *
 * @return Pointer to an allocated resource store object.
 */
extern struct cmb_resourcestore *cmb_resourcestore_create(void);

/**
 * @brief  Make an allocated resource store object ready for use.
 *
 * @param rsp Pointer to an allocated resource store object.
 * @param name A null-terminated string naming the resource store.
 * @param capacity The maximum amount that can be assigned at the same time.
 */
extern void cmb_resourcestore_initialize(struct cmb_resourcestore *rsp,
                                         const char *name,
                                         uint64_t capacity);

/**
 * @brief  Un-initializes a store object.
 *
 * @param rsp Pointer to an allocated resource store object.
 */
extern void cmb_resourcestore_terminate(struct cmb_resourcestore *rsp);

/**
 * @brief Deallocates memory for a store object.
 *
 * @param rsp Pointer to an allocated resource store object.
 */
extern void cmb_resourcestore_destroy(struct cmb_resourcestore *rsp);

/**
 * @brief Return the amount of this store that is currently held by the given
 *        process, possibly zero.
 *
 * @param rsp Pointer to an initialized resource store object.
 * @param pp Pointer to a `cmb_process`
 *
 * @return The amount from this `cmb_resourcestore`that is held by the process.
 */
extern uint64_t cmb_resourcestore_held_by_process(struct cmb_resourcestore *rsp,
                                                  struct cmb_process *pp);

/**
 * @brief Request and if necessary wait for an amount of the reource store.
 *        The calling process may already hold some and try to increase its
 *        holding with this call, or to obtain its first helping.
 *
 * It will either get the required amount and return `CMB_PROCESS_SUCCESS`,
 * be preempted and return `CMB_PROCESS_PREEMPTED`, or be interrupted and return
 * some other value. If it is preempted, the process lost everything it had and
 * returns empty-handed. If interrupted by any other signal, it returns with the
 * same amount as it had at the beginning of the call.
 *
 * Only the signal is returned, not the amount obtained or held. The calling
 * process needs to keep track of this itself based on the return signal values.
 * In particular, do not assume that the process has received the requested
 * amount when it returns.
 *
 * @param rsp Pointer to an allocated resource store object.
 * @param amount The requested amount.
 */
extern int64_t cmb_resourcestore_acquire(struct cmb_resourcestore *rsp,
                                         uint64_t amount);

/**
 * @brief Preempt the current holders and grab the amount, starting from the
 *        lowest priority holder. If there is not enough to cover the amount
 *        before it runs into holders with equal or higher priority than the
 *        caller, it will politely wait in line for the remainder. It only
 *        preempts processes with strictly lower priority than itself, otherwise
 *        acts like `cmb_resourcestore_acquire()`.
 *
 * As for `cmb_resourcestore_acquire()`, it can either return with the requested
 * amount, an unchanged amount (interrupted), or nothing at all (preempted). The
 * amount received or held is not returned by this function, only the signal
 * value.
 *
 * @param rsp Pointer to an allocated resource store object.
 * @param amount The requested amount.
 */
extern int64_t cmb_resourcestore_preempt(struct cmb_resourcestore *rsp,
                                         uint64_t amount);

/**
 * @brief Release an amount of the resource back to the store, not necessarily
 *        everything that the calling process holds, but not more than it is
 *        currently holding. Always returns immediately.
 *
 * @param rsp Pointer to an allocated resource store object.
 * @param amount The requested amount.
 */
extern void cmb_resourcestore_release(struct cmb_resourcestore *rsp,
                                      uint64_t amount);

/**
 * @brief Returns name of store as const char *.
 *
 * @param rsp Pointer to an allocated resource store object.
 * @return A null-terminated string with the name of the resource store.
 */
static inline const char *cmb_resourcestore_get_name(struct cmb_resourcestore *rsp)
{
    cmb_assert_debug(rsp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rsp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return rbp->name;
}

/**
 * @brief Turn on data recording.
 *
 * @param rsp Pointer to an initialized resource store object.
 */
extern void cmb_resourcestore_start_recording(struct cmb_resourcestore *rsp);

/**
 * @brief Turn off data recording.
 *
 * @param rsp Pointer to an initialized resource object.
 */
extern void cmb_resourcestore_stop_recording(struct cmb_resourcestore *rsp);

/**
 * @brief Get the recorded timeseries of resource usage.
 *
 * @param rsp Pointer to an initialized resource object.
 * @return Pointer to a `cmb_timeseries` containing the resource usage history.
 */
extern struct cmb_timeseries *cmb_resourcestore_get_history(struct cmb_resourcestore *rsp);

/**
 * @brief Print a simple text mode report of the resource usage, ncluding key
 *        statistical metrics and a histogram. Mostly intended for debugging
 *        purposes, not presentation graphics.
 *
 * @param rsp Pointer to an initialized resource object.
 * @param fp File pointer, possibly `stdout`.
 */
extern void cmb_resourcestore_print_report(struct cmb_resourcestore *rsp, FILE *fp);


#endif /* CIMBA_CMB_STORE_H */
