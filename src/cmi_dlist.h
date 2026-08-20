/*
 * cmb_dlist.h - generic intrusive doubly linked list. Used wherever fast
 *               insertion and deletion of specific objects is a main
 *               requirement, while sorting, iteration, and search over
 *               elements is not. Used in the memory object registry,
 *               see cmi_memregistry.[hc].
 *
 *               Similar usage pattern and design principles as the well-known
 *               Linux kernel linked list implementation (see, e.g.,
 *               https://docs.kernel.org/core-api/list.html).
 *
 *               Note that our list head is a struct cmi_dlist_node, same as
 *               the embedded nodes in each list member item, since we find
 *               this slightly less confusing than having a "list head" in
 *               every member item (as in the Linux kernel). Still, the list
 *               head node is purely a sentinel with no associated item.
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

#ifndef CIMBA_CMI_DLIST_H
#define CIMBA_CMI_DLIST_H

#include "cmi_memutils.h"

struct cmi_dlist_node {
    struct cmi_dlist_node *next;
    struct cmi_dlist_node *prev;
};

CMB_MAYBE_UNUSED
static inline struct cmi_dlist_node *cmi_dlist_create(void)
{
    struct cmi_dlist_node *tmp = cmi_malloc(sizeof(struct cmi_dlist_node));
    tmp->next = NULL;
    tmp->prev = NULL;

    return tmp;
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_initialize(struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    head->next = head;
    head->prev = head;
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_terminate(struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    head->next = NULL;
    head->prev = NULL;
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_destroy(struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    cmi_free(head);
}

CMB_MAYBE_UNUSED
static inline bool cmi_dlist_is_empty(const struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);
    cmb_assert_debug((head->next != NULL) && (head->prev != NULL));;

    return (head->next == head);
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_insert_after(struct cmi_dlist_node *old_node,
                                           struct cmi_dlist_node *new_node)
{
    cmb_assert_debug(old_node != NULL);
    cmb_assert_debug(new_node != NULL);

    new_node->next = old_node->next;
    new_node->prev = old_node;
    old_node->next = new_node;
    new_node->next->prev = new_node;
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_insert_before(struct cmi_dlist_node *old_node,
                                           struct cmi_dlist_node *new_node)
{
    cmb_assert_debug(old_node != NULL);
    cmb_assert_debug(new_node != NULL);

    new_node->prev = old_node->prev;
    new_node->next = old_node;
    old_node->prev = new_node;
    new_node->prev->next = new_node;
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_insert_first(struct cmi_dlist_node *head, struct cmi_dlist_node *new_node) {
    cmi_dlist_insert_after(head, new_node);
}

CMB_MAYBE_UNUSED
static inline void cmi_dlist_insert_last(struct cmi_dlist_node *head,
                                          struct cmi_dlist_node *new_node)
{
    cmi_dlist_insert_before(head, new_node);
}

CMB_MAYBE_UNUSED
static inline bool cmi_dlist_unlink(struct cmi_dlist_node *node)
{
    cmb_assert_debug(node != NULL);

    if ((node->next == NULL) || (node->next == node)) {
        cmb_assert_debug(node->prev == node->next);
        return false;
    }
    else {
        node->next->prev = node->prev;
        node->prev->next = node->next;
        node->prev = node;
        node->next = node;
        return true;
    }
}

CMB_MAYBE_UNUSED
static inline struct cmi_dlist_node *cmi_dlist_remove_first(struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    struct cmi_dlist_node *node = head->next;
    if (node != head) {
        const bool ret = cmi_dlist_unlink(node);
        cmb_assert_debug(ret == true);
        return node;
    }
    else {
        return NULL;
    }
}

CMB_MAYBE_UNUSED
static inline struct cmi_dlist_node *cmi_dlist_remove_last(struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    struct cmi_dlist_node *node = head->prev;
    if (node != head) {
        const bool ret = cmi_dlist_unlink(node);
        cmb_assert_debug(ret == true);
        return node;
    }
    else {
        return NULL;
    }
}

CMB_MAYBE_UNUSED
static inline struct cmi_dlist_node *cmi_dlist_first(const struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    struct cmi_dlist_node *node = head->next;

    return (node != head) ? node : NULL;
}

CMB_MAYBE_UNUSED
static inline struct cmi_dlist_node *cmi_dlist_last(const struct cmi_dlist_node *head)
{
    cmb_assert_debug(head != NULL);

    struct cmi_dlist_node *node = head->prev;

    return (node != head) ? node : NULL;
}

#define cmi_dlist_entry(node, type, member) cmi_container_of(node, type, member)

#endif /* CIMBA_CMI_DLIST_H */
