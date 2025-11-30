/*
 * cmb_holdable.h - extends the base cmb_resourcebase class to the derived
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

#include "cmb_holdable.h"

void cmb_holdable_initialize(struct cmb_holdable *hrp, const char *name)
{
    hrp->drop = NULL;
    hrp->reprio = NULL;

    cmb_resourcebase_initialize((struct cmb_resourcebase *)hrp, name);
}

void cmb_holdable_terminate(struct cmb_holdable *hrp)
{
    cmb_resourcebase_terminate((struct cmb_resourcebase *)hrp);
}
