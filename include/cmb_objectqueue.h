/*
 * cmi_objectqueue.h - a two-headed fixed-capacity queue where one or more
 * producer processes can put objects into the one end, and one or more
 * consumer processes can get objects out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
 *
 * The difference from cmb_buffer is that it only represents amounts, while
 * qmb_queue tracks the individual objects passing throug the queue. An object
 * can be anything, represented by void* here.
 *
 * First in first out queue order only. No method implemented to cancel random
 * objects from the queue. No record kept of object holders, since the
 * cmb_buffer and cmb_objectqueue essentially deal with assigning the available
 * space in the resource to processes, not lending pieces of a resource to
 * processes. The objects holding a part of a cmb_objectqueue are already in the
 * queue. Hence, no need for forced removal (scram) of holder processes either.
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

#ifndef CIMBA_CMB_OBJECTQUEUE_H
#define CIMBA_CMB_OBJECTQUEUE_H

#include <stdint.h>

#include "cmb_assert.h"

#include "../src/cmi_resourcebase.h"
#include "../src/cmi_resourceguard.h"

struct cmb_objectqueue {
    struct cmi_resourcebase core;
    struct cmi_resourceguard front_guard;
    struct cmi_resourceguard rear_guard;
    uint64_t capacity;
    uint64_t length_now;
    struct queue_tag *queue_head;
    struct queue_tag *queue_end;
    struct cmb_dataset wait_times;
    bool is_recording;
};

/*
 * cmb_objectqueue_create : Allocate memory for a queue object.
 */
extern struct cmb_objectqueue *cmb_objectqueue_create(void);

/*
 * cmb_objectqueue_initialize : Make an allocated queue object ready for use.
 */
extern void cmb_objectqueue_initialize(struct cmb_objectqueue *oqp,
                                       const char *name,
                                       uint64_t capacity);

/*
 * cmb_objectqueue_terminate : Un-initializes an object queue.
 */
extern void cmb_objectqueue_terminate(struct cmb_objectqueue *oqp);

/*
 * cmb_objectqueue_destroy : Deallocates memory for an object queue.
 */
extern void cmb_objectqueue_destroy(struct cmb_objectqueue *oqp);

/*
 * cmb_objectqueue_get : Request and if necessary wait for an object from the
 * queue. Only one object can be requested at a time.
 *
 * Note that the object argument is a pointer to where the object is to be
 * stored. The return value CMB_PROCESS_SUCCESS (0) indicates that all went well
 * and the object pointer location now contains a valid pointer to an object.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than CMB_PROCESS_SUCCESS. The
 * object pointer will be NULL.
 */
extern int64_t cmb_objectqueue_get(struct cmb_objectqueue *oqp,
                                   void **objectloc);

/*
 * cmb_objectqueue_put : Put an object into the queue, if necessary waiting for free
 * space.
 *
 * Note that the object argument is a pointer to where the object is stored.
 * The return value CMB_PROCESS_SUCCESS (0) indicates that all went well. The
 * _put() call doe snot change the value at this location.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than CMB_PROCESS_SUCCESS. The
 * object pointer will still be unchanged.
 */
extern int64_t cmb_objectqueue_put(struct cmb_objectqueue *oqp,
                                   void **objectloc);

/*
 * cmb_objectqueue_get_name : Returns name of queue as const char *.
 */
static inline const char *cmb_objectqueue_get_name(struct cmb_objectqueue *oqp)
{
    cmb_assert_debug(oqp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)oqp;

    return rbp->name;
}

extern void cmb_objectqueue_start_recording(struct cmb_objectqueue *oqp);
extern void cmb_objectqueue_stop_recording(struct cmb_objectqueue *oqp);
extern struct cmb_timeseries *cmb_objectqueue_get_history(struct cmb_objectqueue *oqp);
extern struct cmb_dataset *cmb_objectqueue_get_waiting_times(struct cmb_objectqueue *oqp);
extern void cmb_objectqueue_print_report(struct cmb_objectqueue *oqp, FILE *fp);

#endif /* CIMBA_CMB_OBJECTQUEUE_H */
