/*
 * cmi_memregistry.h - registry for ensuring deallocation of Cimba internal
 *  objects on abandoning a trial to avoid uncontrolled memory leaks. The user
 *  code is still responsible for deallocating its own objects before calling
 *  cmb_logger_error to abandon a trial without exiting the program.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
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

#ifndef CIMBA_CMI_MEMREGISTRY_H
#define CIMBA_CMI_MEMREGISTRY_H

#include <stdbool.h>

#include "cmi_config.h"
#include "cmi_dlist.h"

/* Teardown function for a particular object class. In the Cimba object
 * lifecycle (create, initialize, terminate, destroy) for some struct cmb_X,
 * these are the destructor cmb_X_terminate(struct cmb_X *ptr) and
 * the deallocator cmb_X_destroy(struct cmb_X *ptr), both with the same call
 * signature. We state the argument as `void *` here as a generic function. */
typedef void (cmi_teardown_func)(void *obj);

/* The registry tag, embedded in the managed objects */
struct cmi_memregistry_item {
    cmi_teardown_func  *teardown;
    cmi_dlist_node node;
};

/* The actual registry of memory objects */
extern CMB_THREAD_LOCAL struct cmi_dlist_node cmi_memregistry;
extern CMB_THREAD_LOCAL bool cmi_memregistry_armed;

extern void cmi_memregistry_add(void *obj, cmi_teardown_func *fn);
extern void cmi_memregistry_remove(void *obj);
extern void cmi_memregistry_teardown(void);

#endif /* CIMBA_CMI_MEMREGISTRY_H */
