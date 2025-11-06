/*
 * cmi_processtag.h - singly linked list of tags pointing to processes
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

#ifndef CIMBA_CMI_PROCESSTAG_H
#define CIMBA_CMI_PROCESSTAG_H

#include <stdio.h>

/*
 * struct process_tag : A tag for the singly linked list of processes waiting
 * for some process or event.
 */
struct cmi_processtag {
    struct cmi_processtag *next;
    struct cmb_process *proc;
};

/*
 * cmi_processtag_list_add : Add a waiting process to the given list location.
 */
extern void cmi_processtag_list_add(struct cmi_processtag **ptloc,
                                    struct cmb_process *pp);

/*
 * cmi_processtag_list_remove : Remove a waiting process from the given list
 * location. Returns true if found, false if not.
 */
extern bool cmi_processtag_list_remove(struct cmi_processtag **ptloc,
                                       const struct cmb_process *pp);
/*
 * cmi_processtag_list_wake_all : Schedules a wakeup call for each process in
 * the given list location.
 */
extern void cmi_processtag_list_wake_all(struct cmi_processtag **ptloc,
                                         int64_t signal);

/*
 * cmi_processtag_list_print : Print the list of waiting processes
 */
extern void cmi_processtag_list_print(struct cmi_processtag **ptloc, FILE *fp);

#endif // CIMBA_CMI_PROCESSTAG_H
