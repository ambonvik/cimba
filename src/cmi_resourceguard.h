/*
 * cmi_resourceguard.h - the gatekeeper class for resources a process can wait
 * for. It is derived from cmi_hashheap by composition and inherits its methods,
 * adding a pointer to the resource it guards.
 *
 *  * Copyright (c) Asbjørn M. Bonvik 2025.
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

#ifndef CIMBA_CMI_RESOURCEGUARD_H
#define CIMBA_CMI_RESOURCEGUARD_H

#include <stdbool.h>
#include <stdint.h>

#include "cmb_process.h"

#include "cmi_hashheap.h"

struct cmi_resourceguard {
    struct cmi_hashheap priority_queue;
    struct cmi_resourcebase *guarded_resource;
};

/*
 * typedef cmb_resourceguard_demand_func : function prototype for a resource demand
 */
typedef bool (cmb_resourceguard_demand_func)(const struct cmi_resourcebase *res,
                                        const struct cmb_process *pp,
                                        const void *ctx);

/*
 * cmi_resourceguard_initialize : Make an already allocated resource guard
 * object ready for use.
 */
extern void cmi_resourceguard_initialize(struct cmi_resourceguard *rgp,
                                          struct cmi_resourcebase *rbp);

/*
 * cmi_resourceguard_terminate : Un-initializes a resource guard object.
 */
extern void cmi_resourceguard_terminate(struct cmi_resourceguard *rgp);

/*
 * cmi_resourceguard_wait : Enqueue and suspend the calling process until it
 * reaches the front of the priority queue and its demand function returns true.
 * ctx is whatever context the demand function needs to evaluate if it is
 * satisfied or not, such as the number of units needed from the resource or
 * something more complex and user application defined.
 * Returns whatever signal was received when the process was reactivated.
 * Cannot be called from the main process.
 */
extern int64_t cmi_resourceguard_wait(struct cmi_resourceguard *rgp,
                                       cmb_resourceguard_demand_func *demand,
                                       void *ctx);

/*
 * cmi_resourceguard_signal : Plings the bell for a resource guard to check if
 * any of the waiting processes should be resumed. Will evaluate the demand
 * function for the first process in the queue, if any, and will resume it if
 * (and only if) its demand function (*demand)(rp, pp, ctx) returns true.
 *
 * Resumes zero or one waiting processes. Call it again if there is a chance
 * that more than one process could be ready, e.g. if some process just returned
 * five units of a resource and there are several processes waiting for one
 * unit each.
 *
 * Returns true if some process was resumed, false otherwise, hence easy to
 * wrap in a loop like while (cmb_resource_guard_signal(rgp)) { ... }
 *
 * In cases where some waiting process needs to bypass another, e.g. if there
 * are three available units of the resource, the first process in the queue
 * demands five, and there are three more behind it that demands one each, it is
 * up to the application to dynamically change process priorities to bring the
 * correct process to the front of the queue and make it eligible to resume.
  */
extern bool cmi_resourceguard_signal(struct cmi_resourceguard *rgp);

/*
 * cmi_resourceguard_cancel : Remove this process from the priority queue
 * and resume it with a CMB_PROCESS_CANCELLED signal.
 * Returns true if the found, false if not.
 */
extern bool cmi_resourceguard_cancel(struct cmi_resourceguard *rgp,
                                      struct cmb_process *pp);

/*
 * cmi_resourceguard_remove : Remove this process from the priority queue
 * without resuming it. Returns true if the found, false if not.
 */
extern bool cmi_resourceguard_remove(struct cmi_resourceguard *rgp,
                                      const struct cmb_process *pp);


#endif /* CIMBA_CMI_RESOURCEGUARD_H */
