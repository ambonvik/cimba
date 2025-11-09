/*
 * cmi_resource.h - a simple binary semaphore supporting acquire, release, and
 * preempt methods. Can only be held by one process at a time. Assigned to
 * waiting processes in priority order, then FIFO tie-breaker order.
 *
 * A process holding a resource may be preempted by a higher priority process.
 * For this purpose, the resources maintain a list of processes currently
 * holding (parts of) the resource, to enable use cases like machine breakdowns,
 * priority interrupts, or holding processes getting killed in more violent use
 * cases.
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

#ifndef CIMBA_CMB_RESOURCE_H
#define CIMBA_CMB_RESOURCE_H

#include <stdint.h>

#include "cmb_process.h"

#include "cmi_resourcebase.h"
#include "cmi_resourceguard.h"

struct cmb_resource {
    struct cmi_resourcebase core;
    struct cmi_resourceguard front_guard;
    struct cmb_process *holder;
};

/*
 * cmb_resource_create : Allocate memory for a resource object.
 */
extern struct cmb_resource *cmb_resource_create(void);

/*
 * cmb_resource_initialize : Make an allocated resource object ready for use.
 */
extern void cmb_resource_initialize(struct cmb_resource *rp,
                                    const char *name);

/*
 * cmb_resource_terminate : Un-initializes a resource object.
 */
extern void cmb_resource_terminate(struct cmb_resource *rp);

/*
 * cmb_resource_destroy : Deallocates memory for a resource object.
 */
extern void cmb_resource_destroy(struct cmb_resource *rp);

/*
 * cmb_resource_acquire : Request and if necessary wait for the resource.
 * Returns CMB_PROCESS_SUCCESS if all is well.
 */
extern int64_t cmb_resource_acquire(struct cmb_resource *rp);

/*
 * cmb_resource_release : Release the resource.
 */
extern void cmb_resource_release(struct cmb_resource *rp);


/*
 * cmb_resource_preempt : Preempt the current holder and grab the resource, if
 * the calling process has higher priority than the current holder. Otherwise,
 * it politely waits for its turn at the front gate.
 */
extern int64_t cmb_resource_preempt(struct cmb_resource *rp);

/*
 * cmb_resource_get_name : Returns name of resource as const char *.
 */
static inline const char *cmb_resource_get_name(struct cmb_resource *rp)
{
    cmb_assert_debug(rp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)rp;

    return rbp->name;
}

extern void cmb_resource_start_recording(struct cmb_resource *rp);
extern void cmb_resource_stop_recording(struct cmb_resource *rp);
extern struct cmb_timeseries *cmb_resource_get_history(struct cmb_resource *rp);
extern void cmb_resource_print_report(struct cmb_resource *rp, FILE *fp);

#endif /* CIMBA_CMB_RESOURCE_H */
