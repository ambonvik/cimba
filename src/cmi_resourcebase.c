/*
 * cmi_resourcebase.c - the virtual base class for resources a process can wait
 * for, enabling polymorphic functions to be called for members of any derived
 * class and allowing lists of miscellaneous resource types together.
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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

#include "cmb_assert.h"
#include "cmb_process.h"

#include "cmi_resourcebase.h"

/*
 * cmi_resourcebase_initialize - Make an already allocated resource core
 * object ready for use. Do nothing if already initialized.
 */
void cmi_resourcebase_initialize(struct cmi_resourcebase *rbp,
                                 const char *name)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(name != NULL);
    /* Might get raw memory with random content, cannot assert _UNINITIALIZED */

    rbp->cookie = CMI_INITIALIZED;
    cmi_resourcebase_set_name(rbp, name);

    cmi_dlist_initialize(&(rbp->terminate.node));
    rbp->terminate.teardown = (cmi_teardown_func *)cmi_resourcebase_terminate;
    rbp->terminate.object = rbp;
    cmi_memregistry_add(&(rbp->terminate));

    cmb_assert_debug(rbp->cookie == CMI_INITIALIZED);
}

/*
 * cmi_resourcebase_terminate - Un-initializes a resource base object.
 */
void cmi_resourcebase_terminate(struct cmi_resourcebase *rbp)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release((rbp->cookie == CMI_INITIALIZED)
                    || cmi_memregistry_is_demolishing);

    if (rbp->cookie == CMI_INITIALIZED) {
        rbp->cookie = CMI_UNINITIALIZED;
        if (!cmi_memregistry_is_demolishing) {
            /* Terminating normally, remove from register */
            cmi_memregistry_remove(&(rbp->terminate));
        }
    }

    cmb_assert_debug(rbp->cookie == CMI_UNINITIALIZED);
}

/*
 * cmb_resource_set_name - Change the resource name.
 *
 * The name is contained in a fixed size buffer and will be truncated if it is
 * too long to fit into the buffer, leaving one char for the \0 at the end.
 */
void cmi_resourcebase_set_name(struct cmi_resourcebase *rbp, const char *name)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
    cmb_assert_release(name != NULL);

    const int r = snprintf(rbp->name, CMI_RESOURCEBASE_NAMEBUF_SZ, "%s", name);
    cmb_assert_release((r >= 0) && (r < CMI_RESOURCEBASE_NAMEBUF_SZ));
}

