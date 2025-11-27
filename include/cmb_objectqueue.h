/**
 * @file cmb_objectqueue.h
 * @brief A two-headed fixed-capacity queue where one or more producer processes
 * can put arbitrary objects into the one end, and one or more consumer
 * processes can get objects out of the other end. If enough space is not
 * available, the producers wait, and if there is not enough content, the
 * consumers wait.
 *
 * The difference from `cmb_buffer` is that it only represents amounts, while
 * `cmb_objectqueue` tracks the individual objects passing throug the queue.
 * An object can be anything, represented by `void*` here.
 *
 * First in first out queue order only. No method implemented to cancel random
 * objects from the queue. No record kept of object holders, since the
 * `cmb_buffer` and `cmb_objectqueue` essentially deal with assigning the
 * available space in the resource to processes, not lending pieces of a
 * resource to processes. The objects holding a part of a `cmb_objectqueue` are
 * already in the queue. Hence, no need for forced removal (drop) of holder
 * processes either.
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

#ifndef CIMBA_CMB_OBJECTQUEUE_H
#define CIMBA_CMB_OBJECTQUEUE_H

#include <stdint.h>

#include "cmb_assert.h"

#include "cmi_resourcebase.h"
#include "cmi_resourceguard.h"

/**
 * @brief A fixed capacity queue for passing arbitrary objects from one or more
 *        producer (putter) processes to one or more consumer (getter) processes.
 */
struct cmb_objectqueue {
    struct cmi_resourcebase core;           /**< The virtual base class */
    struct cmi_resourceguard front_guard;   /**< Front waiting room for getters */
    struct cmi_resourceguard rear_guard;    /**< Rear waiting room for putters */
    uint64_t capacity;                      /**< The maximum size, possibly `UINT64_MAX` for unlimited */
    uint64_t length;                        /**< The current queue length */
    struct queue_tag *queue_head;           /**< The head of the queue, `NULL` if empty */
    struct queue_tag *queue_end;            /**< The tail of the queue, `NULL` if empty */
    bool is_recording;                      /**< Is it recording its history? */
    struct cmb_timeseries history;          /**< History of queue lengths */
    struct cmb_dataset wait_times;          /**< Additional data set for history of waiting times */
};

/**
 * @brief Allocate memory for a `cmb_objectqueue` object.
 */
extern struct cmb_objectqueue *cmb_objectqueue_create(void);

/**
 * @brief Make an allocated `cmb_objectqueue` ready for use.
 *
 * @param oqp Pointer to a `cmb_objectqueue`
 * @param name Its identifying name string
 * @param capacity Its maximum size, possibly `UINT64_MAX` for unlimited.
 */
extern void cmb_objectqueue_initialize(struct cmb_objectqueue *oqp,
                                       const char *name,
                                       uint64_t capacity);

/**
 * @brief  Un-initializes an object queue.
 *
 * @param oqp Pointer to a `cmb_objectqueue`
 */
extern void cmb_objectqueue_terminate(struct cmb_objectqueue *oqp);

/**
 * @brief  Deallocate memory for an object queue.
 *
 * @param oqp Pointer to a `cmb_objectqueue`
 */
extern void cmb_objectqueue_destroy(struct cmb_objectqueue *oqp);

/**
 * @brief   Request and if necessary wait for an object from the queue.
 *          Only one object can be requested at a time.
 *
 * Note that the object argument is a pointer to where the object is to be
 * stored. The return value `CMB_PROCESS_SUCCESS` (0) indicates that all went
 * well and the object pointer location now level a valid pointer to an object.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than `CMB_PROCESS_SUCCESS`. The
 * object pointer will be `NULL`.
 *
 * @param oqp Pointer to an object queue
 * @param objectloc Pointer to the location for storing the obtained object.
 *
 * @return `CMB_PROCESS_SUCCESS` (0) for success, some other value otherwise.
 */
extern int64_t cmb_objectqueue_get(struct cmb_objectqueue *oqp,
                                   void **objectloc);

/**
 * @brief Put an object into the queue, if necessary waiting for free
 * space.
 *
 * Note that the object argument is a pointer to where the object is stored.
 * The return value `CMB_PROCESS_SUCCESS` (0) indicates that all went well. The
 * `_put()` call does not change the value at this location. (It is passed as a
 * `void**` for symmetry with `cmb_objectqueue_get`)
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than `CMB_PROCESS_SUCCESS`. The
 * object pointer will still be unchanged.
 *
 * @param oqp Pointer to an object queue
 * @param objectloc Pointer to the location where the object is stored.
 *
 * @return `CMB_PROCESS_SUCCESS` (0) for success, some other value otherwise.
 */
extern int64_t cmb_objectqueue_put(struct cmb_objectqueue *oqp,
                                   void **objectloc);

/**
 * @brief Returns name of queue as `const char *`.
 *
 * @param oqp Pointer to an object queue
 * @return A null-terminated string containing the name of the object queue.
 */
static inline const char *cmb_objectqueue_get_name(struct cmb_objectqueue *oqp)
{
    cmb_assert_debug(oqp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);

    return rbp->name;
}

/**
 * @brief Returns current object queue length
 *
 * @param oqp Pointer to an object queue
 * @return The current queue length
 */
static inline uint64_t cmb_objectqueue_length(struct cmb_objectqueue *oqp)
{
    cmb_assert_debug(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);

    return oqp->length;
}

/**
 * @brief Returns current free space in object queue
 *
 * @param oqp Pointer to an object queue
 * @return The available space in the queue
 */
static inline uint64_t cmb_objectqueue_space(struct cmb_objectqueue *oqp)
{
    cmb_assert_release(oqp != NULL);
    cmb_assert_release(((struct cmi_resourcebase *)oqp)->cookie == CMI_INITIALIZED);
    cmb_assert_debug(oqp->level <= oqp->capacity);

    return (oqp->capacity - oqp->length);
}

/**
 * @brief Turn on data recording.
 *
 * @param oqp Pointer to a object queue
 */
extern void cmb_objectqueue_start_recording(struct cmb_objectqueue *oqp);

/**
 * @brief Turn off data recording.
 *
 * @param oqp Pointer to an object queue
 */
extern void cmb_objectqueue_stop_recording(struct cmb_objectqueue *oqp);

/**
 * @brief Get the recorded timeseries of queue lengths.
 *
 * @param oqp Pointer to an object queue
 * @return Pointer to a `cmb_timeseries` containing the queue length history.
 */
extern struct cmb_timeseries *cmb_objectqueue_get_history(struct cmb_objectqueue *oqp);

/**
 * @brief Get the recorded data set of waiting times. A dataset, not a time
 * series, since each sample value is associated with an object, not a point in
 * time.
 *
 * @param oqp Pointer to an object queue
 * @return Pointer to a `cmb_dataset` containing the waiting times.
 */
extern struct cmb_dataset *cmb_objectqueue_get_waiting_times(struct cmb_objectqueue *oqp);

/**
 * @brief Print a simple text mode report of the queue lengths and waiting times,
 *        including key statisticcal metrics and histograms. Mostly intended for
 *        debugging purposes, not presentation graphics.
 *
 * @param oqp Pointer to an object queue
 * @param fp File pointer, possibly `stdout`.
 */
extern void cmb_objectqueue_print_report(struct cmb_objectqueue *oqp, FILE *fp);

#endif /* CIMBA_CMB_OBJECTQUEUE_H */
