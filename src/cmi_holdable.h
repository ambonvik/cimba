/*
 * cmi_holdable.h - extends the base cmb_resourcebase class to the derived
 * subclass of resources that can be held by a process. The cmb_resource and
 * cmb_resourcestore will be derived from here, but not cmb_buffer since there
 * is no way the process can "hold" a buffer in the same way as holding an
 * acquired resource.
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

#ifndef CIMBA_CMI_HOLDABLE_H
#define CIMBA_CMI_HOLDABLE_H

#include <stdint.h>

#include "cmb_process.h"

#include "cmi_resourcebase.h"

struct cmi_holdable;

/*
 * typedef cmi_holdable_drop_func : function prototype for a resource scram,
 * to be used when a process is killed and needs to release all held resources
 * no matter what type these are. The drop function removes a process from the
 * resource's holder list without resuming the process, a different procedure
 * from the process itself releasing the resource.
 *
 * The process pointer argument is needed since the calling (current) process is
 * not the victim process here. The handle arg is for cases where the resource
 * can look it up in its hash map for efficiency, zero if not applicable.
 */
typedef void (cmi_holdable_drop_func)(struct cmi_holdable *hrp,
                                      const struct cmb_process *pp,
                                      uint64_t handle);

/*
 * typedef cmi_resourcebase_reprio_func : function prototype for reshuffling a
 * resource holders' list if a process changes priority. A pointer to this type
 * function is stored in the virtual base class for calling the appropriate
 * reprio function for each derived class. For some resource classes (e.g. a
 * binary semaphore cmb_resource) this is trivial, for others (e.g. a counting
 * semaphore cmb_resourcestore) with many simultaneous holding processes it is decidedly
 * less trivial to do. The process that changes its priority can simply call
 * (*reprio) and get the correct handling for each resource it holds.
 */
typedef void (cmi_holdable_reprio_func)(struct cmi_holdable *hrp,
                                        uint64_t handle,
                                        int64_t pri);


/*
 * struct cmi_holdable : includes the timeseries head by composition, but
 * its data array will only be allocated as needed.
 */
struct cmi_holdable {
    struct cmi_resourcebase base;
    cmi_holdable_drop_func *drop;
    cmi_holdable_reprio_func *reprio;
};

extern void cmi_holdable_initialize(struct cmi_holdable *hrp, const char *name);
extern void cmi_holdable_terminate(struct cmi_holdable *hrp);

#endif /* CIMBA_CMI_HOLDABLE_H */
