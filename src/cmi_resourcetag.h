/*
 * cmi_resourcetag.h - singly linked list of tags pointing to resources
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

#ifndef CIMBA_CMI_RESOURCETAG_H
#define CIMBA_CMI_RESOURCETAG_H

#include <stdio.h>

/*
 * struct cmi_resourcetag : opaque struct, see cmi_resourcetag.c for details.
 */
struct cmi_resourcetag;
struct cmi_resource_base;

/*
 * cmi_resourcetag_list_add : Add a resource to the given list location.
 * Note that this uses pointers to the resource base class, not the various
 * derived classes.
 */
extern void cmi_resourcetag_list_add(struct cmi_resourcetag **rtloc,
                                     struct cmi_resource_base *rbp,
                                     uint64_t handle);

/*
 * cmi_resourcetag_list_remove : Find and remove a resource from the given list
 * location.
 */
extern void cmi_resourcetag_list_remove(struct cmi_resourcetag **rtloc,
                                        const struct cmi_resource_base *rbp);

/*
 * cmi_resourcetag_list_scram_all : Calls the respective scram function for each
 * resource in the given list location, removing them from the list.
 */
extern void cmi_resourcetag_list_scram_all(struct cmi_resourcetag **rtloc);

/*
 * cmi_resourcetag_list_print : Print the list of waiting processes
 */
extern void cmi_resourcetag_list_print(struct cmi_resourcetag **rtloc,
                                       FILE *fp);

#endif // CIMBA_CMI_RESOURCETAG_H
