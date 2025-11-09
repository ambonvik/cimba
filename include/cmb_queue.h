/*
* cmi_queue.h - a two-headed fixed-capacity resource where one or more
 * producer processes can put objects into the one end, and one or more
 * consumer processes can get objects out of the other end. If enough space is
 * not available, the producers wait, and if there is not enough content, the
 * consumers wait.
 *
 * The difference from cmb_queue is that cmb_queue only represents amounts,
 * while qmb_queue tracks the individual objects passing throug the queue. An
 * object can be anything, represented by void* here.
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

#ifndef CIMBA_CMB_QUEUE_H
#define CIMBA_CMB_QUEUE_H

#include <stdint.h>

#include "cmb_assert.h"

#include "../src/cmi_resourcebase.h"
#include "../src/cmi_resourceguard.h"

struct cmb_queue {
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
 * cmb_queue_create : Allocate memory for a queue object.
 */
extern struct cmb_queue *cmb_queue_create(void);

/*
 * cmb_queue_initialize : Make an allocated queue object ready for use.
 */
extern void cmb_queue_initialize(struct cmb_queue *qp,
                                 const char *name,
                                 uint64_t capacity);

/*
 * cmb_queue_terminate : Un-initializes a queue object.
 */
extern void cmb_queue_terminate(struct cmb_queue *qp);

/*
 * cmb_queue_destroy : Deallocates memory for a queue object.
 */
extern void cmb_queue_destroy(struct cmb_queue *qp);

/*
 * cmb_queue_get : Request and if necessary wait for an object from the queue.
 * Only one object can be requested at a time.
 *
 * Note that the object argument is a pointer to where the object is to be
 * stored. The return value CMB_PROCESS_SUCCESS (0) indicates that all went well
 * and the object pointer location now contains a valid pointer to an object.
 *
 * If the call was interrupted for some reason, the return value is the
 * interrupt signal received, some value other than CMB_PROCESS_SUCCESS. The
 * object pointer will be NULL.
 */
extern int64_t cmb_queue_get(struct cmb_queue *qp, void **objectloc);

/*
 * cmb_queue_put : Put an object into the queue, if necessary waiting for free
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
extern int64_t cmb_queue_put(struct cmb_queue *qp, void **objectloc);

/*
 * cmb_queue_get_name : Returns name of queue as const char *.
 */
static inline const char *cmb_queue_get_name(struct cmb_queue *qp)
{
    cmb_assert_debug(qp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)qp;

    return rbp->name;
}

extern void cmb_queue_start_recording(struct cmb_queue *qp);
extern void cmb_queue_stop_recording(struct cmb_queue *qp);
extern struct cmb_timeseries *cmb_queue_get_length_history(struct cmb_queue *qp);
extern struct cmb_dataset *cmb_queue_get_waiting_times(struct cmb_queue *qp);
extern void cmb_queue_print_report(struct cmb_queue *qp, FILE *fp);

#endif /* CIMBA_CMB_QUEUE_H */
