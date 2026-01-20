/*
 * cmi_process.h - internal process mechanics, not part of public API
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26
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

#ifndef CIMBA_CMI_PROCESS_H
#define CIMBA_CMI_PROCESS_H

#include <stdint.h>

#include "cmi_slist.h"
#include "cmi_mempool.h"

/*
 * enum cmi_process_awaitable_type - Types of things a process may be waiting for
 */
enum cmi_process_awaitable_type {
    CMI_PROCESS_AWAITABLE_TIME,
    CMI_PROCESS_AWAITABLE_RESOURCE,
    CMI_PROCESS_AWAITABLE_PROCESS,
    CMI_PROCESS_AWAITABLE_EVENT
};

/*
 * cmi_process_awaitable - Things a process can be waiting for.
 */
struct cmi_process_awaitable {
    enum cmi_process_awaitable_type type;
    union { void *ptr; uint64_t handle; };
    void *padding;
    struct cmi_slist_head listhead;
};

extern CMB_THREAD_LOCAL struct cmi_mempool cmi_process_awaitabletags;

extern void cmi_process_add_awaitable(struct cmb_process *pp,
                                      enum cmi_process_awaitable_type type,
                                      void *awaitable);

extern bool cmi_process_remove_awaitable(struct cmb_process *pp,
                                         enum cmi_process_awaitable_type type,
                                         const void *awaitable);

extern void cmi_process_cancel_awaiteds(struct cmb_process *pp);

/*
 * cmi_process_holdable - Things that can be held by a process.
 */
struct cmi_process_holdable {
    struct cmi_holdable *res;
    struct cmi_slist_head listhead;
};

extern CMB_THREAD_LOCAL struct cmi_mempool cmi_process_holdabletags;

extern bool cmi_process_remove_holdable(struct cmb_process *pp,
                                        const struct cmi_holdable *holdable);

/*
 * cmi_process_waiter - Things that can wait for a process, i.e., other processes.
 */
struct cmi_process_waiter {
    struct cmb_process *proc;
    struct cmi_slist_head listhead;
};

extern CMB_THREAD_LOCAL struct cmi_mempool cmi_process_waitertags;

extern bool cmi_process_remove_waiter(struct cmb_process *pp,
                                      const struct cmb_process *waiter);

#endif /* CIMBA_CMI_PROCESS_H */
