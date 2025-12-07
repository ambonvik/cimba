/**
 * @file cmi_resourcebase.h
 * @brief The virtual base class for all resources a process can wait for.
 *
 * This class provides polymorphic functions to be called for members of any
 * derived class and allows lists of miscellaneous resource types together.
 *
 * Most importantly, a `cmi_resourceguard` will need a pointer to a
 * `cmi_resourcebase` object to evaluate the demand function for a particular
 * resource. That function will cast the `cmi_resourcebase` pointer to the
 * appropriate type of resource and determine if the resource is available or
 * not. A common base class is needed for this polymorphism to work. For the
 * same reason, `cmb_condition`is also derived from `cmi_resourcebase`.
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

#ifndef CIMBA_CMI_RESOURCEBASE_H
#define CIMBA_CMI_RESOURCEBASE_H

#include <stdint.h>

#include "../include/cmb_process.h"
#include "../include/cmb_timeseries.h"

#include "cmi_memutils.h"

/**
 * @brief Maximum length of a resource name, anything longer will be truncated
 */
#define CMI_RESOURCEBASE_NAMEBUF_SZ 32

/**
 * @brief Virtual base class for various resources and condition variables.
 */
struct cmi_resourcebase {
    uint64_t cookie;                        /**< Initialization trap */
    char name[CMI_RESOURCEBASE_NAMEBUF_SZ]; /**< Resource name */
};

/**
 * @brief Make an already allocated resource base object ready for use.
 */
extern void cmi_resourcebase_initialize(struct cmi_resourcebase *rbp,
                                        const char *name);

/**
 * @brief  Un-initializes a resource base object.
 */
extern void cmi_resourcebase_terminate(struct cmi_resourcebase *rcp);

/**
 * @brief  Set a new name for the resource.
 *
 * The name is held in a fixed size buffer of size `CMI_RESOURCEBASE_NAMEBUF_SZ`.
 * If the new name is too large for the buffer, it will be truncated at one less
 * than the buffer size, leaving space for the terminating zero char.
 */
extern void cmi_resourcebase_set_name(struct cmi_resourcebase *rbp,
                                      const char *name);

#endif /* CIMBA_CMI_RESOURCEBASE_H */
