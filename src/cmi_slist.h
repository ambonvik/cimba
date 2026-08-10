/*
 * cmb_slist.h - a generic singly linked list with no `last` pointer.
 *               Only useful for implementing the stack abstract data type with
 *               its LIFO ordering, implemented as push, pop, and peek ops
 *               here. If more is needed, see cmi_dlist.h.
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

#ifndef CIMBA_CMI_SLIST_H
#define CIMBA_CMI_SLIST_H

#include "cmi_memutils.h"

struct cmi_slist_node {
    struct cmi_slist_node *next;
};

CMB_MAYBE_UNUSED
static inline struct cmi_slist_node *cmi_slist_create(void)
{
    return cmi_malloc(sizeof(struct cmi_slist_node));
}

CMB_MAYBE_UNUSED
static inline void cmi_slist_initialize(struct cmi_slist_node *head)
{
    cmb_assert_debug(head != NULL);

    head->next = NULL;
}

CMB_MAYBE_UNUSED
static inline void cmi_slist_terminate(struct cmi_slist_node *head)
{
    cmb_assert_debug(head != NULL);

    head->next = NULL;
}

CMB_MAYBE_UNUSED
static inline void cmi_slist_destroy(struct cmi_slist_node *head)
{
    cmb_assert_release(head != NULL);

    cmi_free(head);
}

CMB_MAYBE_UNUSED
static inline bool cmi_slist_is_empty(const struct cmi_slist_node *head)
{
    cmb_assert_debug(head != NULL);

    return (head->next == NULL);
}

CMB_MAYBE_UNUSED
static inline void cmi_slist_push(struct cmi_slist_node *head,
                                  struct cmi_slist_node *new)
{
    cmb_assert_debug(head != NULL);
    cmb_assert_debug(new != NULL);

    new->next = head->next;
    head->next = new;
}

CMB_MAYBE_UNUSED
static inline struct cmi_slist_node *cmi_slist_pop(struct cmi_slist_node *head)
{
    cmb_assert_debug(head != NULL);

    struct cmi_slist_node *ret = head->next;
    if (ret != NULL) {
        head->next = ret->next;
    }

    return ret;
}

CMB_MAYBE_UNUSED
static inline struct cmi_slist_node *cmi_slist_peek(const struct cmi_slist_node *head)
{
    cmb_assert_debug(head != NULL);

    return head->next;
}

#define cmi_slist_entry(node, type, member) cmi_container_of(node, type, member)

#endif /* CIMBA_CMI_SLIST_H */
