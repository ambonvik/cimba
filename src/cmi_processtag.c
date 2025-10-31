/*
 * cmi_processtag.c - singly linked lsit of tags pointing to processes
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
#include "cmb_mempool.h"

#include "cmi_config.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"
#include "cmi_processtag.h"

/*
 * struct process_tag : A tag for the singly linked list of processes waiting
 * for some process or event.
 */
struct cmi_processtag {
    struct cmi_processtag *next;
    struct cmb_process *proc;
};

/*
 * tag_pool : Memory pool of process tags
 */
static CMB_THREAD_LOCAL struct cmb_mempool *tag_pool = NULL;

/*
 * pwwuevt : The event handler that actually resumes the process coroutine after
 * being scheduled by cmb_process_wait_*.
 */
static void pwwuevt(void *vp, void *arg)
{
    cmb_assert_debug(vp != NULL);

    struct cmi_coroutine *cp = (struct cmi_coroutine *)vp;
    if (cp->status == CMI_COROUTINE_RUNNING) {
        (void)cmi_coroutine_resume(cp, arg);
    }
    else {
        const struct cmb_process *pp = (struct cmb_process *)vp;
        cmb_logger_warning(stdout,
                          "process wait wakeup call found process %s dead",
                           cmb_process_get_name(pp));
    }
}

/*
 * cmi_processtag_list_wake_all : Schedules a wakeup call for each process in
 * the given list.
 *
 * Unlink the tag chain from the event queue before iteration to avoid any
 * obscure mutating-while-iterating bugs.
 */
void cmi_processtag_list_wake_all(struct cmi_processtag **ptloc, const int64_t signal)
{
    cmb_assert_debug(ptloc != NULL);

    /* Unlink the tag chain */
    struct cmi_processtag *ptag = *ptloc;
    *ptloc = NULL;

    /* Process it, scheduling a wakeup call for each process */
    while (ptag != NULL) {
        struct cmb_process *pp = ptag->proc;
        cmb_assert_debug(pp != NULL);
        const double time = cmb_time();
        const int64_t priority = cmb_process_get_priority(pp);
        (void)cmb_event_schedule(pwwuevt,
                                pp,
                                (void *)signal,
                                 time,
                                 priority);

        struct cmi_processtag *tmp = ptag->next;
        ptag->next = NULL;
        ptag->proc = NULL;
        cmb_mempool_put(tag_pool, ptag);
        ptag = tmp;
    }

    cmb_assert_debug(*ptloc == NULL);
}

/*
 * cmi_processtag_list_add : Add a waiting process to the given list location.
 */
void cmi_processtag_list_add(struct cmi_processtag **ptloc,
                             struct cmb_process *pp)
{
    cmb_assert_debug(ptloc != NULL);
    cmb_assert_debug(pp != NULL);

    /* Lazy initalization of the memory pool for process tags */
    if (tag_pool == NULL) {
        tag_pool = cmb_mempool_create();
        cmb_mempool_initialize(tag_pool,
                              64u,
                              sizeof(struct cmi_processtag));
    }

    /* Get one and add it to the head of the list */
    struct cmi_processtag *ptag = cmb_mempool_get(tag_pool);
    ptag->next = *ptloc;
    ptag->proc = pp;
    *ptloc = ptag;
}

/*
 * cmi_processtag_list_print : Print the list of waiting processes
 */
void cmi_processtag_list_print(struct cmi_processtag **ptloc, FILE *fp)
{
    fprintf(fp, "\t\t\twait list at %p\n", (void *)ptloc);
    struct cmi_processtag *ptag = *ptloc;
    while (ptag != NULL) {
        const struct cmb_process *pp = ptag->proc;
        fprintf(fp, "\t\t\t\tptp %p proc %p", (void *)ptag, (void *)pp);
        cmb_assert_debug(pp != NULL);
        fprintf(fp, " name %s\n", cmb_process_get_name(pp));
        ptag = ptag->next;
    }
}


