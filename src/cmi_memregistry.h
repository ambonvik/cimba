/*
 * cmi_memregistry.h - registry for ensuring deallocation of Cimba internal
 * objects on abandoning a trial to avoid uncontrolled memory leaks. The user
 * code is still responsible for deallocating its own objects before calling
 * cmb_logger_error to abandon a trial without exiting the program.
 *
 * Note that this is not a general garbage collection, but more like a partial
 * RAII for the specific case where objects in a trial are abandoned due to
 * error handling. So we run their _terminate and _destroy functions from here
 * as part of the error unwind.
 *
 * See also functions thread_pthread_cleanup, thread_main_cleanup, and
 * worker_thread_func in cimba.c
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

#include "cmi_config.h"
#include "cmi_dlist.h"

/*
 * Teardown function for a particular object class. In the Cimba object
 * lifecycle (create, initialize, terminate, destroy) for some struct cmb_X,
 * these are cmb_X_terminate(struct cmb_X *ptr) and
 * cmb_X_destroy(struct cmb_X *ptr), both with the same call signature.
 * We state the argument as `void *` here as a generic function and typecast
 * the actual functions when registering.
 */
typedef void (cmi_teardown_func)(void *obj);

/* The registry tag, embedded in the managed objects */
struct cmi_memregistry_item {
    cmi_teardown_func *teardown;    /* Teardown function */
    void *object;                   /* Address of the object to be torn down */
    struct cmi_dlist_node node;     /* Registry list links */
};

/* The actual registry of memory objects */
extern CMB_THREAD_LOCAL struct cmi_dlist_node cmi_memregistry;

/* Flag to identify if a teardown is in progress or not. */
extern CMB_THREAD_LOCAL bool cmi_memregistry_is_demolishing;

/*
 * Add an object to the registry, pushing it from the head of the list.
 */
extern void cmi_memregistry_add(struct cmi_memregistry_item *item);

/*
 * Remove a given object from the registry.
 */
extern void cmi_memregistry_remove(struct cmi_memregistry_item *item);

/*
 * Execute the teardown in LIFO order for proper create, initialize,
 * terminate, destroy sequence.
 */
extern void cmi_memregistry_cleanup(void);

#endif /* CIMBA_CMI_MEMREGISTRY_H */
