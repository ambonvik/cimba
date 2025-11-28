/*
 * cmb_list.c - a generic singly linked list of 64-byte objects, reserving
 *              two 64-bit fields for metadata.
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
    double dstamp;
    uint64_t ustamp;
    void *payload;
};

extern void cmi_list_add(struct cmi_list_tag **lloc,
                         double dstamp,
                         uint64_t ustamp,
                         void *payload);

extern bool cmi_list_remove(struct cmi_list_tag **lloc,
                            const void *payload);

extern double cmi_list_get_dstamp(struct cmi_list_tag **lloc,
                                  const void *payload);

extern uint64_t cmi_list_get_ustamp(struct cmi_list_tag **lloc,
                                    const void *payload);


#endif /* CIMBA_CMI_LIST_H */
