/*
 * cmi_waitable.h - the things a process can be waiting for.
 *
 * We make this a separate small struct to be included in the process class by
 * compositon. Alternatively, we could have created a virtual base class for the
 * above types of things-that-can-be-waited-for but would then need to handle
 * multiple inheritance when a process needs to be a coroutine and can be waited
 * for by other processes. That is a few bridges too far for our object-oriented
 * style of C. So, composition it is.
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

#ifndef CIMBA_CMI_WAITABLE_H
#define CIMBA_CMI_WAITABLE_H

#include <stdint.h>

/*
 * enum cmb_waitable_type : Types of things a process may be waiting for
 */
enum cmi_process_waitable_type {
    CMI_WAITABLE_NONE = 0,
    CMI_WAITABLE_CLOCK,
    CMI_WAITABLE_PROCESS,
    CMI_WAITABLE_EVENT,
    CMI_WAITABLE_RESOURCE
};

/*
 * cmi_process_waitable : Things a process can be waiting for.
 */
struct cmi_process_waitable {
    enum cmi_process_waitable_type type;
    void *ptr;
    uint64_t handle;
};

#endif /* CIMBA_CMI_WAITABLE_H */
