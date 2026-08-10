/*
 * cmi_memregistry.c - registry for ensuring deallocation of Cimba internal
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

#include "cmi_memregistry.h"

CMB_THREAD_LOCAL bool cmi_memregistry_is_demolishing = false;
CMB_THREAD_LOCAL struct cmi_dlist_node cmi_memregistry = { NULL, NULL };

void cmi_memregistry_add(struct cmi_memregistry_item *item)
{
    cmb_assert_debug(item != NULL);
    cmb_assert_debug(item->teardown != NULL);
    cmb_assert_debug(item->object != NULL);
    cmb_assert_debug(cmi_memregistry_is_demolishing == false);

    if (cmi_memregistry.next == NULL) {
        cmi_dlist_initialize(&cmi_memregistry);
    }

    cmi_dlist_insert_first(&cmi_memregistry, &(item->node));
}

void cmi_memregistry_remove(struct cmi_memregistry_item *item)
{
    cmb_assert_debug(item != NULL);
    cmb_assert_debug(cmi_memregistry_is_demolishing == false);

    cmi_dlist_unlink(&item->node);
}

/*
 * cmi_memregistry_teardown - execute the registered destructors.
 *
 * This should only happen on the way out of an abandoned trial, following
 * a call to cmb_logger_error(). We should already be back on the main
 * stack, since the cmb_processes have registered themselves here.
 * Calling cmb_process_terminate and cmb_process_destroy from inside that
 * process will delete the current call stack, heading deep into undefined
 * behavior. Hence, assert that we are on main here to nip it in the bud.
 */
void cmi_memregistry_teardown(void)
{
    cmb_assert_debug(cmi_memregistry.next != NULL);
    cmb_assert_debug(cmi_memregistry_is_demolishing == false);

    cmi_memregistry_is_demolishing = true;
    while (!cmi_dlist_is_empty(&cmi_memregistry)) {
        struct cmi_dlist_node *node = cmi_dlist_remove_first(&cmi_memregistry);
        struct cmi_memregistry_item *item = cmi_dlist_entry(node, struct cmi_memregistry_item, node);

        (*item->teardown)(item->object);
    }

    cmi_memregistry_is_demolishing = false;
}