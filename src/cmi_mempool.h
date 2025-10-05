/*
 * cmi_mempool.h - internal memory pool for generic small objects.
 *
 * Provides a memory pool for efficient allocation and reuse of "generic small
 * objects" of fixed size. Each memory pool maintains objects of a certain
 * size, such as 32 or 64 bytes. The object size must be a multiple of 8 bytes.
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
#ifndef CIMBA_CMI_MEMPOOL_H
#define CIMBA_CMI_MEMPOOL_H

#include <stdint.h>

/*
 * struct cmi_mempool : Opaque struct, only handled through API below.
 */
struct cmi_mempool;

/*
 * cmi_mempool_create : Set up a memory pool for objects of size obj_sz bytes.
 * The initial memory allocation is obj_sz * obj_num bytes, later incrementing
 * by the same amount whenever needed. obj_sz must be a multiple of 8 bytes.
 * The memory allocation will be aligned to a page boundary. For efficiency,
 * obj_num * obj_size should be an integer multiple of the page size, such as
 * 128 * 32 = 4096 or 256 * 64 = 16384 for a page size of 4096 bytes.
 */
extern struct cmi_mempool *cmi_mempool_create(uint64_t obj_num, size_t obj_sz);

/*
 * cmi_mempool_expand : Increase the memory pool size by the same amount as
 * originally allocated, obj_sz * obj_num.
 */
extern void cmi_mempool_expand(struct cmi_mempool *mp);

/*
 * cmi_mempool_destroy : Free all memory allocated to the memory pool.
 * All allocated objects from the pool will become invalid.
 */
extern void cmi_mempool_destroy(struct cmi_mempool *mp);

/*
 * cmi_mempool_get : Get an object of size obj_sz from the pool, expanding it
 * if necessary.
 */
extern void *cmi_mempool_get(struct cmi_mempool *mp);

/*
 * cmi_mempool_put : Return an object to the pool. The object must be one
 * previously obtained from this pool.
 */
extern void cmi_mempool_put(struct cmi_mempool *mp, void *op);

#endif //CIMBA_CMI_MEMPOOL_H