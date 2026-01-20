/*
 * cmb_slist.h - generic singly linked list, following similar principles as
 *               the Linux kernel doubly linked list.
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

#include <stdbool.h>

#include "cmi_mempool.h"

#define cmi_offset_of(type, member) ((size_t)&(((type *)0)->member))

#define cmi_container_of(ptr, type, member) \
((type *)((char *)(ptr) - cmi_offset_of(type, member)))

struct cmi_slist_head {
    struct cmi_slist_head *next;
};

static inline struct cmi_slist_head *cmi_slist_create(void)
{
    return cmi_malloc(sizeof(struct cmi_slist_head));
}

static inline void cmi_slist_initialize(struct cmi_slist_head *head)
{
    cmb_assert_debug(head != NULL);

    head->next = NULL;
}

static inline void cmi_slist_terminate(struct cmi_slist_head *head)
{
    cmb_assert_debug(head != NULL);
}

static inline void cmi_slist_destroy(struct cmi_slist_head *head)
{
    cmb_assert_release(head != NULL);
    cmi_free(head);
}


static inline bool cmi_slist_is_empty(const struct cmi_slist_head *head)
{
    return (head->next == NULL);
}

static inline void cmi_slist_push(struct cmi_slist_head *head,
                                  struct cmi_slist_head *new)
{
    cmb_assert_debug(head != NULL);
    cmb_assert_debug(new != NULL);

    new->next = head->next;
    head->next = new;
}

static inline struct cmi_slist_head *cmi_slist_pop(struct cmi_slist_head *head)
{
    cmb_assert_debug(head != NULL);

    struct cmi_slist_head *ret = head->next;
    if (ret != NULL) {
        head->next = ret->next;
    }

    return ret;
}

static inline struct cmi_slist_head *cmi_slist_peek(const struct cmi_slist_head *head)
{
    cmb_assert_debug(head != NULL);

    return head->next;
}

#endif /* CIMBA_CMI_SLIST_H */
