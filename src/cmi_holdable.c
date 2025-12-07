/*
 * cmi_holdable.h - extends the base cmi_resourcebase class to the derived
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

#include "cmi_holdable.h"

void cmi_holdable_initialize(struct cmi_holdable *hrp, const char *name)
{
    hrp->drop = NULL;
    hrp->reprio = NULL;

    cmi_resourcebase_initialize((struct cmi_resourcebase *)hrp, name);
}

void cmi_holdable_terminate(struct cmi_holdable *hrp)
{
    cmi_resourcebase_terminate((struct cmi_resourcebase *)hrp);
}
