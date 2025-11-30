/**
 * @file cmb_buffer.h
 * @brief A two-headed fixed-capacity resource where one or more
 * producer processes can put an amount into the one end, and one or more
 * consumer processes can get amounts out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
 *
 * The buffer will go through level changes that may not be visible outside
 * its own code, e.g., when some process is trying to put or get more amount
 * than currently possible. The buffer level will then hit full or empty before
 * the get or put call returns. Trying to track the level from user code will
 * be inaccurate. Use the built-in history recording instead, and retrieve the
 * buffer level history as a timeseries once the trial is complete.
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

#ifndef CIMBA_CMB_BUFFER_H
#define CIMBA_CMB_BUFFER_H

#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_resourcebase.h"
#include "cmb_resourceguard.h"

/**
 * A `cmb_buffer` has two resource guards, one for get (front) and one for put
 * (rear) operations. It has a fixed capacity, of which some amount may be in
 * use, leaving some free space (the difference between `capacity` and `level`).
 *
 * Note the object oriented structure here: The `cmb_buffer` class inherits the
 * methods and properties from its (virtual) base class `cmb_resourcebase`.
 * In incorporates (by composition) its two `cmb_resourceguard` members. These
 * are full members of the buffer object, not pointers to some other objects.
 * Allocating memory for a `cmb_buffer` object simultaneously allocates memory
 * for the `cmb_resourcebase` and the two `cmb_resourceguard`s. The details of
 * these are encapsulated in the respective classes.
 *
 * If you need a derived class from the `cmb_buffer`, you can declare a struct,
 * say `my_special_buffer`, with a `cmb_buffer` as its first member followed by
 * whatever additions you need. You can then freely cast pointers between
 * `struct my_special_buffer` and `struct cmb_buffer` to refer to the same
 * object as needed, as we do here.
 */

/**
 * @brief A two-sided fixed capacity buffer between one or more producer
 *        (putter) and one or more consumer (getter) processes.
 */
struct cmb_buffer {
    struct cmb_resourcebase core;           /**< The virtual base class */
    struct cmb_resourceguard front_guard;   /**< Front waiting room for getters */
    struct cmb_resourceguard rear_guard;    /**< Rear waiting room for putters */
    uint64_t capacity;                      /**< The buffer size, possibly UINT64_MAX for unlimited */
    uint64_t level;                         /**< The current level in the buffer */
    bool is_recording;                      /**< Is the buffer recording its history? */
    struct cmb_timeseries history;         /**< The bufer level history */
};

/**
 * @brief Allocate memory for a buffer object.
 */
extern struct cmb_buffer *cmb_buffer_create(void);

/**
 * @brief Make an allocated buffer object ready for use.
 * @param bp Pointer to the already allocated buffer object.
 * @param name A null-terminated string naming the buffer resource.
 * @param capacity The capacity of the buffer. Use `UINT64_MAX`
 *                 for buffers of unlimited capacity.
 */
extern void cmb_buffer_initialize(struct cmb_buffer *bp,
                                  const char *name,
                                  uint64_t capacity);

/**
 * @brief Un-initializes a buffer object.
 * @param bp Pointer to the buffer object.
 */
extern void cmb_buffer_terminate(struct cmb_buffer *bp);

/**
 * @brief Deallocates memory for a buffer object.
 * @param bp Pointer to the buffer object.
 */
extern void cmb_buffer_destroy(struct cmb_buffer *bp);

/**
 * @brief Request and if necessary wait for an amount of the
 * buffer resource. The requested amount can be larger than the buffer space.
 * If so, the calling process will accumulate until satisfied.
 *
 * Note that the argument is a pointer to where the amount is stored.
 * The return value `CMB_PROCESS_SUCCESS` (0) indicates that all went well and
 * the value `*amount` equals the requested amount.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
 * and `*amntp` will be the quantity remaining when interrupted. The return
 * value is then the interrupt signal received, some other value than
 * `CMB_PROCESS_SUCCESS`, possibly an application-defined reason code.
 *
 * @param bp Pointer to the buffer object.
 * @param amntp Pointer to a variable containing the amount to be obtained. Will
 *              contain the amount actually obtained after the call.
 * @return `CMB_PROCESS_SUCCESS` (0) for success, some other value otherwise.
 */
extern int64_t cmb_buffer_get(struct cmb_buffer *bp, uint64_t *amntp);

/**
 * @brief Put an amount of the resource into the buffer, if necessary
 * waiting for free space. The amount can be larger than the buffer space.
 *
 * Note that the argument is a pointer to where the amount is stored.
 * The return value `CMB_PROCESS_SUCCESS` (0) indicates that all went well and
 * the value `*amntp` now equals zero.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
* and `*amntp` will be the quantity obtained before it was interrupted. The return
 * value is then the interrupt signal received, some other value than
 * `CMB_PROCESS_SUCCESS`, possibly an application-defined reason code.
 *
 * @param bp Pointer to the buffer object.
 * @param amntp Pointer to a variable containing the amount to be obtained. Will
 *              contain the amount actually obtained after the call.
 * @return `CMB_PROCESS_SUCCESS` (0) for success, some other value otherwise.
 */
extern int64_t cmb_buffer_put(struct cmb_buffer *bp, uint64_t *amntp);

/**
 * @brief Returns name of buffer as `const char *`.
 *
 * @param bp Pointer to the buffer object.
 * @return A null-terminated string containing the name of the buffer.
 */
static inline const char *cmb_buffer_get_name(struct cmb_buffer *bp)
{
    cmb_assert_debug(bp != NULL);

    const struct cmb_resourcebase *rbp = (struct cmb_resourcebase *)bp;

    return rbp->name;
}

/**
 * @brief Returns current level in buffer
 *
 * @param bp Pointer to a buffer
 * @return The current buffer level
 */
static inline uint64_t cmb_buffer_level(struct cmb_buffer *bp)
{
    cmb_assert_debug(bp != NULL);
    cmb_assert_release(((struct cmb_resourcebase *)bp)->cookie == CMI_INITIALIZED);

    return bp->level;
}

/**
 * @brief Returns current free space in buffer
 *
 * @param bp Pointer to a buffer
 * @return The available space in the buffer
 */
static inline uint64_t cmb_buffer_space(struct cmb_buffer *bp)
{
    cmb_assert_release(bp != NULL);
    cmb_assert_release(((struct cmb_resourcebase *)bp)->cookie == CMI_INITIALIZED);
    cmb_assert_debug(bp->level <= bp->capacity);

    return (bp->capacity - bp->level);
}

/**
 * @brief Turn on data recording.
 *
 * @param bp Pointer to the buffer object.
 */
extern void cmb_buffer_start_recording(struct cmb_buffer *bp);

/**
 * @brief Turn off data recording
 *
 * @param bp Pointer to the buffer object.
 */
extern void cmb_buffer_stop_recording(struct cmb_buffer *bp);

/**
 * @brief Get the recorded timeseries of buffer levels.
 *
 * @param bp Pointer to the buffer object.
 * @return Pointer to a `cmb_timeseries`containing the buffer level history.
 */
extern struct cmb_timeseries *cmb_buffer_get_history(struct cmb_buffer *bp);

/**
 * @brief Print a simple text mode report of the buffer levels, including key
 * statical metrics and a histogram. Mostly intended for debugging purposes,
 * not presentation graphics.
 *
 * @param bp Pointer to the buffer object.
 * @param fp File pointer, possibly `stdout`.
 */
extern void cmb_buffer_print_report(struct cmb_buffer *bp, FILE *fp);

#endif /* CIMBA_CMB_BUFFER_H */
