/*
 * cmi_pool.h - internal memory pool for generic small objects.
 *
 * Provides two separate thread local memory pools, one for 256-bit (32 bytes)
 * and one for 512 bit objects (64 bytes). These are used as queueing tags
 * containing prev / next pointers and a payload of two 64-bit values such
 * as the arrival and departure time (4 * 64 bits = 256 bits). The larger
 * 512-bit size is used for enqueuing a simulated process waiting for some
 * resource, where we need additional space for a process pointer, a predicate
 * function pointer, and its argument, adding up to (4 + 3) * 64 = 448 bits.
 * For efficiency reasons, we want this to be a power of two, so it gets one
 * more 64-bit payload field to pad it out to 512 bits = 64 bytes.
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
#ifndef CIMBA_CMI_POOL_H
#define CIMBA_CMI_POOL_H

#endif //CIMBA_CMI_POOL_H