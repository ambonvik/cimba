/*
 * cmi_resource.h - guarded resources that the processes can queue for.
 *
 * A generic resource consists of two or three parts:
 * - A front end (the guard) that contains the priority queue for processes
 *   that want to use the resource and may have to wait for availability
 * - A middle part (the core) that is the actual resource, perhaps as simple as
 *   a limited number of available slots (a semaphore). This part also maintains
 *   a list of processes currently using the resource.
 * - Optionally, a back end symmetrical to the front end for processes waiting
 *   to refill the resource core. For example, the core could be a fixed size
 *   buffer between two machines in a workshop, where the upstream machine may
 *   have to wait for space in the buffer and the downstream machine may have to
 *   wait for parts in the buffer.
 *
 * A process will register itself and a predicate demand function when first
 * joining the priority queue. The demand function evaluates whether the
 * necessary condition to grab the resource is in place, such as at least one
 * part being available in a buffer or semaphore slot being available. If true
 * initially, the wait returns immediately. If not, the process waits in line.
 * When some other process signals the resource, it evaluates the demand
 * function for the first process in the priority queue. If true, the process is
 * resumed and can grab the resource. When done, it puts it back and signals the
 * quard to evaluate waiting demand again.
 *
 * A process holding a resource may in some cases be preempted by a higher
 * priority process. It is for this purpose that the resource core maintains a
 * list of processes currently holding (parts of) the resource, to enable use
 * cases like machine breakdowns or priority interrupts.
 *
 * Below, we describe the cmb_resourcequard and cmb_resourcecore "classes", and
 * combine these to generic models of semaphore-type resources, finite-capacity
 * buffers, and finite-sized object queues. A user application can extend this
 * further by inheritance, using C's object oriented features. (Seriously, see
 * examples in the code below.)
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

#ifndef CIMBA_CMB_RESOURCE_H
#define CIMBA_CMB_RESOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "cmb_assert.h"
#include "cmi_hashheap.h"
#include "cmb_process.h"
#include "cmi_processtag.h"

#endif // CIMBA_CMB_RESOURCE_H
