/*
 * cmb_list.c - a generic singly linked list of 16- or 32-byte objects. Using
 *              two different memory pools, need to distinguish between these
 *              cases, hence the apparent code duplication below.
 *              Only basic add (push) and remove methods here, others need to
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

/* 16-byte version, just the linked list of pointers to something */
struct cmi_list_tag16 {
    struct cmi_list_tag16 *next;
    void *ptr;
};

static inline void cmi_list_add16(struct cmi_list_tag16 **lloc, void *payload)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_list_tag16 *ltag = NULL;
    cmb_assert_debug(sizeof(*ltag) == 16u);
    ltag = cmi_mempool_get(&cmi_mempool_16b);
    cmb_assert_debug(ltag != NULL);

    ltag->next = *lloc;
    ltag->ptr = payload;

    *lloc = ltag;
}

static inline bool cmi_list_remove16(struct cmi_list_tag16 **lloc,
                                     const void *target)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(target != NULL);

    struct cmi_list_tag16 **ltpp = lloc;
    while (*ltpp != NULL) {
        struct cmi_list_tag16 *ltag = *ltpp;
        if (ltag->ptr == target) {
            *ltpp = ltag->next;
            cmi_mempool_put(&cmi_mempool_16b, ltag);

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

static inline void cmi_list_add32(struct cmi_list_tag32 **lloc,
                                const double dstamp,
                                const uint64_t ustamp,
                                void *payload)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_list_tag32 *ltag = NULL;
    cmb_assert_debug(sizeof(*ltag) == 32u);
    ltag = cmi_mempool_get(&cmi_mempool_32b);
    cmb_assert_debug(ltag != NULL);

    ltag->next = *lloc;
    ltag->dbl = dstamp;
    ltag->uint = ustamp;
    ltag->ptr = payload;

    *lloc = ltag;
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
            cmi_mempool_put(&cmi_mempool_32b, ltag);

            return true;
        }
        else {
            ltpp = &(ltag->next);
        }
    }

    return false;
}

#endif /* CIMBA_CMI_LIST_H */
