/*
 * cmb_list.c - a generic singly linked list of 16- or 32-byte objects. Using
 *              two different memory pools, need to distinguish between these
 *              cases, hence the apparent code duplication below.
 *              Only basic push, pop, and remove methods here, others need to
 *              be implemented at point of usage from exposed struct content.
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

#ifndef CIMBA_CMI_LIST_H
#define CIMBA_CMI_LIST_H

#include <stdbool.h>
#include <stdint.h>

#include "cmi_mempool.h"

/* Implicit 16-byte version, just the linked list of pointers to something */
struct cmi_list_tag {
    struct cmi_list_tag *next;
    void *ptr;
};

static inline void cmi_list_push(struct cmi_list_tag **lloc, void *payload)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_list_tag *ltag = NULL;
    cmb_assert_debug(sizeof(*ltag) == 16u);
    ltag = cmi_mempool_alloc(&cmi_mempool_16b);
    cmb_assert_debug(ltag != NULL);

    ltag->next = *lloc;
    ltag->ptr = payload;

    *lloc = ltag;
}

static inline void *cmi_list_pop(struct cmi_list_tag **lloc)
{
    cmb_assert_debug(lloc != NULL);

    void *ret = NULL;
    if (*lloc != NULL) {
        struct cmi_list_tag *ltag = *lloc;
        ret = ltag->ptr;
        *lloc = ltag->next;
        cmi_mempool_free(&cmi_mempool_16b, ltag);
    }

    return ret;
}

static inline bool cmi_list_remove(struct cmi_list_tag **lloc, const void *tgt)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(tgt != NULL);

    struct cmi_list_tag **ltpp = lloc;
    while (*ltpp != NULL) {
        struct cmi_list_tag *ltag = *ltpp;
        if (ltag->ptr == tgt) {
            *ltpp = ltag->next;
            cmi_mempool_free(&cmi_mempool_16b, ltag);

            return true;
        }
        else {
            ltpp = &(ltag->next);
        }
    }

    return false;
}

/* 32-byte version, adding two 64-bit fields for object metadata */
struct cmi_list_tag32 {
    struct cmi_list_tag32 *next;
    double dbl;
    uint64_t uint;
    void *ptr;
};

static inline void cmi_list_push32(struct cmi_list_tag32 **lloc,
                                   const double dstamp,
                                   const uint64_t ustamp,
                                   void *payload)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_list_tag32 *ltag = NULL;
    cmb_assert_debug(sizeof(*ltag) == 32u);
    ltag = cmi_mempool_alloc(&cmi_mempool_32b);
    cmb_assert_debug(ltag != NULL);

    ltag->next = *lloc;
    ltag->dbl = dstamp;
    ltag->uint = ustamp;
    ltag->ptr = payload;

    *lloc = ltag;
}

static inline void *cmi_list_pop32(struct cmi_list_tag32 **lloc)
{
    cmb_assert_debug(lloc != NULL);

    void *ret = NULL;
    if (*lloc != NULL) {
        struct cmi_list_tag32 *ltag = *lloc;
        ret = ltag->ptr;
        *lloc = ltag->next;
        cmi_mempool_free(&cmi_mempool_32b, ltag);
    }

    return ret;
}

static inline bool cmi_list_remove32(struct cmi_list_tag32 **lloc,
                                     const void *target)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(target != NULL);

    struct cmi_list_tag32 **ltpp = lloc;
    while (*ltpp != NULL) {
        struct cmi_list_tag32 *ltag = *ltpp;
        if (ltag->ptr == target) {
            *ltpp = ltag->next;
            cmi_mempool_free(&cmi_mempool_32b, ltag);

            return true;
        }
        else {
            ltpp = &(ltag->next);
        }
    }

    return false;
}

/* A doubly linked version, again 32 bytes per tag */
struct cmi_dlist_tag {
    struct cmi_dlist_tag *next;
    struct cmi_dlist_tag *prev;
    void *meta;
    void *ptr;
};

/* Push item to the front */
static inline void cmi_dlist_push(struct cmi_dlist_tag **dlhloc,
                                  struct cmi_dlist_tag **dltloc,
                                  void *meta,
                                  void *payload)
{
    cmb_assert_debug(dlhloc != NULL);
    cmb_assert_debug(dltloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_dlist_tag *dtag = NULL;
    cmb_assert_debug(sizeof(*dtag) == 32u);
    dtag = cmi_mempool_alloc(&cmi_mempool_32b);
    cmb_assert_debug(dtag != NULL);

    dtag->ptr = payload;
    dtag->meta = meta;

    dtag->next = *dlhloc;
    dtag->prev = NULL;
    *dlhloc = dtag;
    if (*dltloc == NULL) {
        *dltloc = dtag;
    }
}

/* Pop item from the front */
static inline void *cmi_dlist_pop(struct cmi_dlist_tag **dlhloc,
                                  struct cmi_dlist_tag **dltloc)
{
    cmb_assert_debug(dlhloc != NULL);
    cmb_assert_debug(dltloc != NULL);

    if (*dlhloc == NULL) {
        return NULL;
    }

    struct cmi_dlist_tag *dtag = *dlhloc;
    void *tmp = dtag->ptr;
    *dlhloc = dtag->next;
    if (*dlhloc != NULL) {
        (*dlhloc)->prev = NULL;
    }
    else {
        *dltloc = NULL;
    }

    cmi_mempool_free(&cmi_mempool_32b, dtag);

    return tmp;
}

/* Add item to the back */
static inline void cmi_dlist_add(struct cmi_dlist_tag **dlhloc,
                                 struct cmi_dlist_tag **dltloc,
                                 void *meta,
                                 void *payload)
{
    cmb_assert_debug(dlhloc != NULL);
    cmb_assert_debug(dltloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_dlist_tag *dtag = NULL;
    cmb_assert_debug(sizeof(*dtag) == 32u);
    dtag = cmi_mempool_alloc(&cmi_mempool_32b);
    cmb_assert_debug(dtag != NULL);

    dtag->ptr = payload;
    dtag->meta = meta;

    dtag->next = NULL;
    dtag->prev = *dltloc;
    *dltloc = dtag;
    if (*dlhloc == NULL) {
        *dlhloc = dtag;
    }
}

/* Pull item from the back */
static inline void *cmi_dlist_pull(struct cmi_dlist_tag **dlhloc,
                                   struct cmi_dlist_tag **dltloc)
{
    cmb_assert_debug(dlhloc != NULL);
    cmb_assert_debug(dltloc != NULL);

    if (*dltloc == NULL) {
        return NULL;
    }

    struct cmi_dlist_tag *dtag = *dltloc;
    void *tmp = dtag->ptr;
    *dltloc = dtag->prev;
    if (*dltloc != NULL) {
        (*dltloc)->next = NULL;
    }
    else {
        *dlhloc = NULL;
    }

    cmi_mempool_free(&cmi_mempool_32b, dtag);

    return tmp;
}


#endif /* CIMBA_CMI_LIST_H */
