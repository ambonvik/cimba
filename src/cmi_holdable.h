/**
* @file cmb_holdable.h
* @brief A virtual class that extends `cmi_resourcebase` to the derived
 *       subclass of resources that can be held by a process.
 *
 * The `cmb_resource` and `cmb_resourcestore` are derived from here, but not
 * `cmb_buffer` since there is no meaningful way a process can "hold" a buffer
 * in the same way as holding an acquired resource.
 *
 * There is no `cmi_holdable_create()` or `cmi_holdable_destroy()` functions,
 * since this class only will appear as an intermediate derived class between
 * `cmb_resourcebase` and the specific resource types, never on its own.

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

#ifndef CIMBA_CMI_HOLDABLE_H
#define CIMBA_CMI_HOLDABLE_H

#include <stdint.h>

#include "cmb_process.h"
#include "cmi_resourcebase.h"

struct cmi_holdable;

/**
 * @brief Function prototype for a resource drop, to be used when a process is killed
 *        and needs to release all held resources no matter what type these are.
 *
 * The drop function removes a process from the resource's holder list without
 * resuming the process, a different procedure from the process itself releasing
 * the resource.
 *
 * The process pointer argument is needed since the calling (current) process is
 * not the victim process here. The handle arg is for cases where the resource
 * can look it up in its hash map for efficiency, zero if not applicable.
 */
typedef void (cmi_holdable_drop_func)(struct cmi_holdable *hrp,
                                      const struct cmb_process *pp,
                                      uint64_t handle);

/**
 * @brief Function prototype for reshuffling a resource holders' list if a
 *        process changes priority.
 *
 * A pointer to this type of function is stored in the virtual base class for
 * calling the appropriate `reprio` function for each derived class. For some
 * resource classes (e.g. a binary semaphore `cmb_resource`) this is trivial,
 * for others (e.g. a counting semaphore `cmb_resourcestore`) with many
 * holding processes less so. The process that changes its priority can simply
 * call `(*reprio)` for each resource it holds and get the correct handling.
 */
typedef void (cmi_holdable_reprio_func)(struct cmi_holdable *hrp,
                                        uint64_t handle,
                                        int64_t pri);

/**
 * @brief The holdable kind of resources.
 */
struct cmi_holdable {
    struct cmi_resourcebase base;
    cmi_holdable_drop_func *drop;
    cmi_holdable_reprio_func *reprio;
};

/**
 * @brief  Make a holdable resource ready for use.
 *
 * @param hrp Pointer to a holdable resource
 * @param name A null-terminated string naming the holdable resource
 */
extern void cmi_holdable_initialize(struct cmi_holdable *hrp, const char *name);

/**
 * @brief  Un-initialize a holdable resource.
 *
 * @param hrp Pointer to a holdable resource
 */
extern void cmi_holdable_terminate(struct cmi_holdable *hrp);

#endif /* CIMBA_CMI_HOLDABLE_H */
