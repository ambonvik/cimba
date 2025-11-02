/*
 * cmi_resourcetag.c - singly linked list of tags pointing to resources
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
#include "cmb_resource.h"
#include "cmb_mempool.h"

#include "cmi_config.h"
#include "cmi_memutils.h"
#include "cmi_resourcetag.h"

/*
 * struct resource_tag : A tag for the singly linked list of resources held by
 * some process.
 */
struct cmi_resourcetag {
    struct cmi_resourcetag *next;
    struct cmi_resource_base *res;
    uint64_t handle;
};

/*
 * tag_pool : Memory pool of resource tags
 */
static CMB_THREAD_LOCAL struct cmb_mempool *tag_pool = NULL;

/*
 * cmi_resourcetag_list_scram_all : Calls the scram function for each resource
 * in the list
 */
void cmi_resourcetag_list_scram_all(struct cmi_resourcetag **rtloc)
{
    cmb_assert_debug(rtloc != NULL);

    /* Unlink the tag chain */
    struct cmi_resourcetag *rtag = *rtloc;
    *rtloc = NULL;

    const struct cmb_process *pp = cmi_container_of(rtloc,
                                              struct cmb_process,
                                              resource_listhead);
    cmb_assert_debug(pp != NULL);

    /* Process it, calling scram() for each resource */
    while (rtag != NULL) {
        struct cmi_resource_base *rbp = rtag->res;
        const uint64_t handle = rtag->handle;
        (*(rbp->scram))(rbp, pp, handle);

        struct cmi_resourcetag *tmp = rtag->next;
        rtag->next = NULL;
        rtag->res = NULL;
        rtag->handle = 0u;
        cmb_mempool_put(tag_pool, rtag);
        rtag = tmp;
    }

    cmb_assert_debug(*rtloc == NULL);
}

/*
 * cmi_resourcetag_list_add : Add a resource to the given list location.
 */
void cmi_resourcetag_list_add(struct cmi_resourcetag **rtloc,
                              struct cmi_resource_base *rbp,
                              const uint64_t handle)
{
    cmb_assert_debug(rtloc != NULL);
    cmb_assert_debug(rbp != NULL);

    /* Lazy initalization of the memory pool for process tags */
    if (tag_pool == NULL) {
        tag_pool = cmb_mempool_create();
        cmb_mempool_initialize(tag_pool,
                              64u,
                              sizeof(struct cmi_resourcetag));
    }

    /* Get one and add it to the head of the list */
    struct cmi_resourcetag *rtag = cmb_mempool_get(tag_pool);
    rtag->next = *rtloc;
    rtag->res = rbp;
    rtag->handle = handle;
    *rtloc = rtag;
}

/*
 * cmi_resourcetag_list_remove : Find and remove a resource from the given list
 * location.
 */
void cmi_resourcetag_list_remove(struct cmi_resourcetag **rtloc,
                                 const struct cmi_resource_base *rbp)
{
    cmb_assert_debug(rtloc != NULL);
    cmb_assert_debug(rbp != NULL);

    struct cmi_resourcetag **rtpp = rtloc;
    while (*rtpp != NULL) {
        struct cmi_resourcetag *rtag = *rtpp;
        if (rtag->res == rbp) {
            *rtpp = rtag->next;
            rtag->next = NULL;
            rtag->res = NULL;
            rtag->handle = 0u;
            cmb_mempool_put(tag_pool, rtag);
            return;
        }
        else {
            rtpp = &(rtag->next);
        }
    }

    #ifndef NDEBUG
        const struct cmb_process *pp = cmi_container_of(rtloc,
                                              struct cmb_process,
                                              resource_listhead);
        cmb_assert_debug(pp != NULL);
        cmb_logger_error(stderr,
                         "Resource %s not found in resource list of process %s",
                         rbp->name,
                         pp->name);
    #endif
}

/*
 * cmi_resourcetag_list_print : Print the list of resources
 */
void cmi_resourcetag_list_print(struct cmi_resourcetag **rtloc, FILE *fp)
{
    fprintf(fp, "\t\t\tresource list at %p\n", (void *)rtloc);
    struct cmi_resourcetag *rtag = *rtloc;
    while (rtag != NULL) {
        const struct cmi_resource_base *rbp = rtag->res;
        fprintf(fp, "\t\t\t\trbp %p res %p handle %llu",
                    (void *)rtag, (void *)rbp, rtag->handle);
        cmb_assert_debug(rbp != NULL);
        fprintf(fp, " name %s\n", rbp->name);
        rtag = rtag->next;
    }
}


