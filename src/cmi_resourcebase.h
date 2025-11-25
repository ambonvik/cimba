/*
 * cmi_resourcebase.h - the virtual base class for resources a process can wait
 * for, providing polymorphic functions to be called for members of any derived
 * class and allowing lists of miscellaneous resource types together.
 *
 * Most importantly, a cmb_resource guard will need a pointer to a
 * cmb_resourcebase object to evaluate the demand function for a particular
 * resource. That function will cast the cmb_resourcebase pointer to the
 * appropriate type of resource and determine if the resource is available or
 * not. A common base class is needed for the polymorphism to work.
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

#include "cmi_memutils.h"

/* Maximum length of a resource name, anything longer will be truncated */
#define CMB_RESOURCEBASE_NAMEBUF_SZ 32

/*
 * struct cmi_resourcebase : includes the timeseries head by composition, but
 * its data array will only be allocated as needed.
 */
struct cmi_resourcebase {
    uint64_t cookie;
    char name[CMB_RESOURCEBASE_NAMEBUF_SZ];
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

#endif /* CIMBA_CMI_RESOURCEBASE_H */
