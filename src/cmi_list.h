/*
 * cmb_list.c - a generic singly linked list of 32-byte objects, inlined for
 *              efficiency and unified across multiple use cases for code size.
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

struct cmi_list_tag {
    struct cmi_list_tag *next;
    double dbl;
    uint64_t uint;
    void *ptr;
};

static inline void cmi_list_add(struct cmi_list_tag **lloc,
                                const double dstamp,
                                const uint64_t ustamp,
                                void *payload)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(payload != NULL);

    struct cmi_list_tag *ltag = NULL;
    cmb_assert_debug(sizeof(*ltag) == 32u);
    ltag = cmi_mempool_get(&cmi_mempool_32b);
    cmb_assert_debug(ltag != NULL);

    ltag->next = *lloc;
    ltag->dbl = dstamp;
    ltag->uint = ustamp;
    ltag->ptr = payload;

    *lloc = ltag;
}


static inline bool cmi_list_remove(struct cmi_list_tag **lloc,
                                   const void *target)
{
    cmb_assert_debug(lloc != NULL);
    cmb_assert_debug(target != NULL);

    struct cmi_list_tag **ltpp = lloc;
    while (*ltpp != NULL) {
        struct cmi_list_tag *ltag = *ltpp;
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
