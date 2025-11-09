/*
 * cmi_resourcebase.h - the virtual base class for resources a process can wait
 * for, providing polymorphic functions to be called for members of any derived
 * class and allowing lists of miscellaneous resource types together.
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

#ifndef CIMBA_CMI_RESOURCEBASE_H
#define CIMBA_CMI_RESOURCEBASE_H

#include <stdint.h>

#include "cmb_process.h"
#include "cmb_timeseries.h"

/*
 * typedef cmi_resourcebase_scram_func : function prototype for a resource scram,
 * to be used when a process is killed and needs to release all held resources
 * no matter what type these are. The scram function removes a process from the
 * resource's holder list without resuming the process, a different procedure
 * from the process itself releasing the resource.
 *
 * We let the resource base class contain a pointer to a scram function and each
 * derived class populate it with a pointer to a function that does appropriate
 * handling for the derived class. The process class can then maintain a list of
 * all held resources and drop them using this function when needed.
 *
 * The process pointer argument is needed since the calling (current) process is
 * not the victim process here. The handle arg is for cases where the resource
 * can look it up in its hash map for efficiency, zero if not applicable.
 */
typedef void (cmi_resourcebase_scram_func)(struct cmi_resourcebase *res,
                                       const struct cmb_process *pp,
                                       uint64_t handle);

/*
 * typedef cmi_resourcebase_reprio_func : function prototype for reshuffling a
 * resource holders' list if a process changes priority. A pointer to this type
 * function is stored in the virtual base class for calling the appropriate
 * reprio function for each derived class. For some resource classes (e.g. a
 * binary semaphore cmb_resource) this is trivial, for others (e.g. a counting
 * semaphore cmb_store) with many simultaneous holding processes it is decidedly
 * less trivial to do. The process that changes its priority can simply call
 * (*reprio) and get the correct handling for each resource it holds.
 */
typedef void (cmi_resourcebase_reprio_func)(struct cmi_resourcebase *rbp,
                                        uint64_t handle,
                                        int64_t pri);

/* Maximum length of a resource name, anything longer will be truncated */
#define CMB_RESOURCEBASE_NAMEBUF_SZ 32

/*
 * struct cmi_resourcebase : includes the timeseries head by composition, but
 * its data array will only be allocated as needed.
 */
struct cmi_resourcebase {
    char name[CMB_RESOURCEBASE_NAMEBUF_SZ];
    cmi_resourcebase_scram_func *scram;
    cmi_resourcebase_reprio_func *reprio;
    bool is_recording;
    struct cmb_timeseries history;
};

/*
 * cmi_resourcebase_initialize : Make an already allocated resource core
 * object ready for use.
 */
extern void cmi_resourcebase_initialize(struct cmi_resourcebase *rbp,
                                         const char *name);

/*
 * cmi_resourcebase_terminate : Un-initializes a resource core object.
 */
extern void cmi_resourcebase_terminate(struct cmi_resourcebase *rcp);

/*
 * cmi_resourcebase_set_name : Set a new name for the resource.
 *
 * The name is held in a fixed size buffer of size CMB_RESOURCEBASE_NAMEBUF_SZ.
 * If the new name is too large for the buffer, it will be truncated at one less
 * than the buffer size, leaving space for the terminating zero char.
 */
extern void cmi_resourcebase_set_name(struct cmi_resourcebase *rbp,
                                       const char *name);

static inline void cmi_resourcebase_start_recording(struct cmi_resourcebase *rbp)
{
    cmb_assert_release(rbp != NULL);

    rbp->is_recording = true;
}

static inline void cmi_resourcebase_stop_recording(struct cmi_resourcebase *rbp)
{
    cmb_assert_release(rbp != NULL);

    rbp->is_recording = false;
}


#endif /* CIMBA_CMI_RESOURCEBASE_H */
