/*
 * cmb_buffer.h - a two-headed fixed-capacity resource where one or more
 * producer processes can put an amount into the one end, and one or more
 * consumer processes can get amounts out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
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

#ifndef CIMBA_CMB_BUFFER_H
#define CIMBA_CMB_BUFFER_H

#include <stdint.h>

#include "cmb_assert.h"

#include "cmi_resourcebase.h"
#include "cmi_resourceguard.h"

struct cmb_buffer {
    struct cmi_resourcebase core;
    struct cmi_resourceguard front_guard;
    struct cmi_resourceguard rear_guard;
    uint64_t capacity;
    uint64_t contains;
};

/*
 * cmb_buffer_create : Allocate memory for a buffer object.
 */
extern struct cmb_buffer *cmb_buffer_create(void);

/*
 * cmb_buffer_initialize : Make an allocated buffer object ready for use.
 */
extern void cmb_buffer_initialize(struct cmb_buffer *bp,
                                    const char *name,
                                    uint64_t capacity);

/*
 * cmb_buffer_terminate : Un-initializes a buffer object.
 */
extern void cmb_buffer_terminate(struct cmb_buffer *bp);

/*
 * cmb_buffer_destroy : Deallocates memory for a buffer object.
 */
extern void cmb_buffer_destroy(struct cmb_buffer *bp);

/*
 * cmb_buffer_get : Request and if necessary wait for an amount of the
 * buffer resource. The requested amount can be larger than the buffer space.
 * If so, the calling process will just accumulate until satisfied.
 *
 * Note that the amount argument is a pointer to where the amount is stored.
 * The return value CMB_PROCESS_SUCCESS (0) indicates that all went well and
 * the value *amount equals the requested amount.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
 * and *amount will be the quantity obtained before interrupted. The return
 * value is the interrupt signal received, some value other than
 * CMB_PROCESS_SUCCESS.
 */
extern int64_t cmb_buffer_get(struct cmb_buffer *bp, uint64_t *amntp);

/*
 * cmb_buffer_put : Put an amount of the resource into the buffer, if necessary
 * waiting for free space. The amount can be larger than the buffer space.
 *
 * Note that the amount argument is a pointer to where the amount is stored.
 * The return value CMB_PROCESS_SUCCESS (0) indicates that all went well and
 * the value *amount now equals zero.
 *
 * If the call was interrupted for some reason, it will be partially fulfilled,
 * and *amount will be the quantity remaining when interrupted. The return
 * value is the interrupt signal received, some value other than
 * CMB_PROCESS_SUCCESS.
 */
extern int64_t cmb_buffer_put(struct cmb_buffer *sp, uint64_t *amntp);

/*
 * cmb_buffer_get_name : Returns name of buffer as const char *.
 */
static inline const char *cmb_buffer_get_name(struct cmb_buffer *sp)
{
    cmb_assert_debug(sp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)sp;

    return rbp->name;
}

extern void cmb_buffer_start_recording(struct cmb_buffer *bp);
extern void cmb_buffer_stop_recording(struct cmb_buffer *bp);
extern struct cmb_timeseries *cmb_buffer_get_history(struct cmb_buffer *bp);
extern void cmb_buffer_print_report(struct cmb_buffer *bp, FILE *fp);

#endif /* CIMBA_CMB_BUFFER_H */
