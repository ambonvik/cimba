/**
 * @file cmb_priorityqueue.h
 * @brief A fixed-capacity priority queue where one or more producer processes (putters)
 *        can put arbitrary objects into the one end, and one or more consumer
 *        processes (getters) can get objects out of the other end. If space is not
 *        available, the producers wait, and if there is no content, the
 *        consumers wait. Objects will be retrieved in priority order.
 */

/*
 * Copyright (c) Asbjørn M. Bonvik 2026.
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

#ifndef CIMBA_CMB_PRIORITYQUEUE_H
#define CIMBA_CMB_PRIORITYQUEUE_H

#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_resourceguard.h"
#include "cmb_timeseries.h"

#include "cmi_hashheap.h"
#include "cmi_resourcebase.h"

/**
 * @brief Unlimited queue size
 */
#ifndef CMB_UNLIMITED
  #define CMB_UNLIMITED UINT64_MAX
#endif

/**
 * @brief A fixed-capacity priority queue where one or more producer processes (putters)
 *        can put arbitrary objects into the one end, and one or more consumer
 *        processes (getters) can get objects out of the other end. If space is not
 *        available, the producers wait, and if there is no content, the
 *        consumers wait. Objects will be retrieved in priority order.
 */
struct cmb_priorityqueue {
    struct cmi_resourcebase core;           /**< The virtual base class */
    struct cmb_resourceguard front_guard;   /**< Front waiting room for getters */
    struct cmb_resourceguard rear_guard;    /**< Rear waiting room for putters */
    struct cmi_hashheap queue;              /**< The actual priority queue */
    uint64_t capacity;                      /**< The maximum size, possibly `CMB_UNLIMITED` */
    bool is_recording;                      /**< Is it recording its history? */
    struct cmb_timeseries history;          /**< History of queue lengths */
};

/**
 * @brief Allocate memory for a `cmb_priorityqueue` object.
 *
 * @relates cmb_priorityqueue
 */
extern struct cmb_priorityqueue *cmb_priorityqueue_create(void);

/**
 * @brief Make an allocated `cmb_priorityqueue` ready for use.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to a `cmb_priorityqueue`
 * @param name The identifying name string
 * @param capacity Its maximum size, possibly `CMB_UNLIMITED`
 */
extern void cmb_priorityqueue_initialize(struct cmb_priorityqueue *pqp,
                                         const char *name,
                                         uint64_t capacity);

/**
 * @brief  Un-initializes an object queue.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to a `cmb_priorityqueue`
 */
extern void cmb_priorityqueue_terminate(struct cmb_priorityqueue *pqp);

/**
 * @brief  Deallocate memory for an object queue.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to a `cmb_priorityqueue`
 */
extern void cmb_priorityqueue_destroy(struct cmb_priorityqueue *pqp);

/**
 * @brief   Request and, if necessary, wait for an object from the queue.
 *          Only one object can be requested at a time.
 *
 * Note that the object argument is a pointer to where the object is to be
 * stored. The return value `CMB_PROCESS_SUCCESS` (0) indicates that all went
 * well and that the object pointer location now contains a valid pointer to
 * an object.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than `CMB_PROCESS_SUCCESS`. The
 * object pointer will be `NULL`.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @param objectloc Pointer to the location for storing the obtained object.
 * @return `CMB_PROCESS_SUCCESS` (0) for success, some other value otherwise.
 */
extern int64_t cmb_priorityqueue_get(struct cmb_priorityqueue *pqp,
                                     void **objectloc);

/**
 * @brief Put an object into the queue, if necessary, waiting for free
 * space.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than `CMB_PROCESS_SUCCESS`. The
 * object pointer will still be unchanged.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @param object Pointer to the object
 * @param priority The object priority, higher goes before lower
 * @param handleloc Pointer to where to store the object queue handle. If NULL,
 *                  the handle is not saved.
 * @return `CMB_PROCESS_SUCCESS` (0) for success, some other value otherwise.
 */
extern int64_t cmb_priorityqueue_put(struct cmb_priorityqueue *pqp,
                                     void *object,
                                     int64_t priority,
                                     uint64_t *handleloc);

/**
 * @brief Return the 1-based position of an object in the queue by priority order.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @param handle The handle of the object to locate
 * @return 1-based position in the queue, or 0 if not found
 */
extern uint64_t cmb_priorityqueue_position(const struct cmb_priorityqueue *pqp,
                                           uint64_t handle);


/**
 * @brief Cancel an object in the queue
 *
 * @memberof cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @param handle Handle of the object to reprioritize
 * @param priority New priority for the object
 */
static inline bool cmb_priorityqueue_cancel(struct cmb_priorityqueue *pqp,
                                            const uint64_t handle)
{
    cmb_assert_release(pqp != NULL);

    struct cmi_hashheap *hp = &(pqp->queue);
    const bool found = cmi_hashheap_remove(hp, handle);

    return found;
}

/**
 * @brief Change the priority of an object in the queue, reshuffling as needed.
 *
 * @memberof cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @param handle Handle of the object to reprioritize
 * @param priority New priority for the object
 */
static inline void cmb_priorityqueue_reprioritize(const struct cmb_priorityqueue *pqp,
                                                  const uint64_t handle,
                                                  const int64_t priority)
{
    cmb_assert_release(pqp != NULL);

    const struct cmi_hashheap *hp = &(pqp->queue);
    cmi_hashheap_reprioritize(hp, handle, 0.0, priority);
}

/**
 * @brief Returns name of queue as `const char *`.
 *
 * @memberof cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @return A null-terminated string containing the name of the object queue.
 */
static inline const char *cmb_priorityqueue_name(struct cmb_priorityqueue *pqp)
{
    cmb_assert_debug(pqp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)pqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return rbp->name;
}

/**
 * @brief Returns current object queue length
 *
 * @memberof cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @return The current queue length
 */
static inline uint64_t cmb_priorityqueue_length(struct cmb_priorityqueue *pqp)
{
    cmb_assert_debug(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);

    return pqp->queue.heap_count;
}

/**
 * @brief Returns current free space in object queue
 *
 * @memberof cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @return The available space in the queue
 */
static inline uint64_t cmb_priorityqueue_space(struct cmb_priorityqueue *pqp)
{
    cmb_assert_release(pqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)pqp)->cookie == CMI_INITIALIZED);
    cmb_assert_debug(pqp->queue.heap_count <= pqp->capacity);

    return (pqp->capacity - pqp->queue.heap_count);
}

/**
 * @brief Turn on data recording.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to a object queue
 */
extern void cmb_priorityqueue_recording_start(struct cmb_priorityqueue *pqp);

/**
 * @brief Turn off data recording.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to an object queue
 */
extern void cmb_priorityqueue_recording_stop(struct cmb_priorityqueue *pqp);

/**
 * @brief Get the recorded timeseries of queue lengths.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @return Pointer to a `cmb_timeseries` containing the queue length history.
 */
extern struct cmb_timeseries *cmb_priorityqueue_history(struct cmb_priorityqueue *pqp);

/**
 * @brief Print a simple text mode report of the queue lengths, including key
 *        statistical metrics and histograms. Mostly intended for debugging
 *        purposes, not presentation graphics.
 *
 * @relates cmb_priorityqueue
 * @param pqp Pointer to an object queue
 * @param fp File pointer, possibly `stdout`.
 */
extern void cmb_priorityqueue_report_print(struct cmb_priorityqueue *pqp, FILE *fp);

#endif /* CIMBA_CMB_PRIORITYQUEUE_H */
