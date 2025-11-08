/*
 * cmi_resourcebase.c - the virtual base class for resources a process can wait
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

#include "cmb_assert.h"
#include "cmb_logger.h"
#include "cmb_process.h"

#include "cmi_resourcebase.h"

/*
 * base_scram : dummy scram function, to be replaced by appropriate scram in
 * derived classes. Does nothing.
 */
void base_scram(struct cmi_resourcebase *rbp,
                const struct cmb_process *pp,
                const uint64_t handle)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(pp != NULL);
    cmb_unused(handle);
}

/*
 * base_reprio : dummy holders reprioritization function. Does nothing.
 */
void base_reprio(struct cmi_resourcebase *rbp, const uint64_t handle, const int64_t pri)
{
    cmb_assert_release(rbp != NULL);
    cmb_unused(handle);
    cmb_unused(pri);
}

/*
 * cmi_resourcebase_initialize : Make an already allocated resource core
 * object ready for use with a given capacity.
 */
void cmi_resourcebase_initialize(struct cmi_resourcebase *rbp,
                                  const char *name)
{
    cmb_assert_release(rbp != NULL);

    cmi_resourcebase_set_name(rbp, name);
    rbp->scram = base_scram;
    rbp->reprio = base_reprio;
    rbp->is_recording = false;

    cmb_timeseries_initialize(&(rbp->history));
}

/*
 * cmi_resourcebase_terminate : Un-initializes a resource base object.
 */
void cmi_resourcebase_terminate(struct cmi_resourcebase *rbp)
{
    cmb_assert_release(rbp != NULL);

    cmb_timeseries_terminate(&(rbp->history));
}

/*
 * cmb_resource_set_name : Change the resource name.
 *
 * The name is contained in a fixed size buffer and will be truncated if it is
 * too long to fit into the buffer, leaving one char for the \0 at the end.
 */
void cmi_resourcebase_set_name(struct cmi_resourcebase *rbp, const char *name)
{
    cmb_assert_release(rbp != NULL);
    cmb_assert_release(name != NULL);

    const int r = snprintf(rbp->name, CMB_RESOURCEBASE_NAMEBUF_SZ, "%s", name);
    cmb_assert_release(r >= 0);
}

