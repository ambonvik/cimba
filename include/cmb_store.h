/*
 * cmb_store.h - a counting semaphore that supports acquire, release, and
 * preempt in specific amounts against a fixed resource capacity, where a
 * process also can acquire more of a resource it already holds some amount
 * of, or release parts of its holding. Several processes can be holding parts
 * of the resource capacity at the same time, possibly also different amounts.
 *
 * the cmb_store adds numeric values for capacity and usage to the base resource.
 * These values are unsigned integers to avoid any rounding issues from
 * floating-point calculations, both faster and higher resolution (if scaled
 * properly to 64-bit range).
 *
 * It assigns amounts to processes in a greedy fashion, where the acquiring
 * process will first grab whatever amount is available, then wait for some more
 * to become available, and repeat until the requested amount is acquired.
 *
 * Preempt is similar to acquire, except that the prempting process also will
 * grab resources from any lower-priority processes holding some.
 *
 * The holders list is now a hashheap, since we may need to handle many separate
 * processes acquiring, holding, releasing, and preempting various amounts of
 * the resource capacity. The hashheap is sorted to keep the holder most likely
 * to be preempted at the front, i.e. lowest priority and last in.
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

#ifndef CIMBA_CMB_STORE_H
#define CIMBA_CMB_STORE_H

#include <stdint.h>

#include "cmi_resourcebase.h"
#include "cmi_resourceguard.h"

struct cmb_store {
    struct cmi_resourcebase core;
    struct cmi_resourceguard front_guard;
    struct cmi_hashheap holders;
    uint64_t capacity;
    uint64_t in_use;
};

/*
 * cmb_store_create : Allocate memory for a store object.
 */
extern struct cmb_store *cmb_store_create(void);

/*
 * cmb_store_initialize : Make an allocated store object ready for use.
 */
extern void cmb_store_initialize(struct cmb_store *sp,
                                    const char *name,
                                    uint64_t capacity);

/*
 * cmb_store_terminate : Un-initializes a store object.
 */
extern void cmb_store_terminate(struct cmb_store *sp);

/*
 * cmb_store_destroy : Deallocates memory for a store object.
 */
extern void cmb_store_destroy(struct cmb_store *sp);

/*
 * cmb_store_acquire : Request and if necessary wait for an amount of the
 * store resource. The calling process may already hold some and try to
 * increase its holding with this call, or to obtain its first helping.
 *
 * Will either get the required amount and return CMB_PROCESS_SUCCESS,
 * be preempted and return CMB_PROCESS_PREEMPTED, or be interrupted and return
 * some other value. If it is preempted, the process lost everything it had and
 * returns empty-handed. If interrupted by any other signal, it returns with the
 * same amount as it had at the beginning of the call.
 */
extern int64_t cmb_store_acquire(struct cmb_store *sp, uint64_t amount);

/*
 * cmb_store_preempt : Preempt the current holders and grab the resource
 * amount, starting from the lowest priority holder. If there is not enough to
 * cover the amount before it runs into holders with equal or higher priority
 * than the caller, it will politely wait in line for the remainder. It only
 * preempts processes with strictly lower priority than itself, otherwise acts
 * like cmb_store_acquire.
 *
 * As for cmb_store_acquire, it can either return with the requested amount,
 * an unchanged amount (interrupted), or nothing at all (preempted).
 */
extern int64_t cmb_store_preempt(struct cmb_store *sp, uint64_t amount);

/*
 * cmb_store_release : Release an amount of the resource back to the store, not
 * necessarily everything that the calling process holds, but not more than it
 * is currently holding. Always returns immediately.
 */
extern void cmb_store_release(struct cmb_store *sp, uint64_t amount);

/*
 * cmb_store_get_name : Returns name of store as const char *.
 */
static inline const char *cmb_store_get_name(struct cmb_store *sp)
{
    cmb_assert_debug(sp != NULL);

    const struct cmi_resourcebase *rbp = (struct cmi_resourcebase *)sp;

    return rbp->name;
}

extern void cmb_store_start_recording(struct cmb_store *sp);
extern void cmb_store_stop_recording(struct cmb_store *sp);
extern struct cmb_timeseries *cmb_store_get_history(struct cmb_store *sp);
extern void cmb_store_print_report(struct cmb_store *sp, FILE *fp);

/*
 * cmb_held_by_process : Return the amount of this store that is currently
 * held by the given process, possibly zero.
 */
extern uint64_t cmb_store_held_by_process(struct cmb_store *sp,
                                          struct cmb_process *pp);

#endif /* CIMBA_CMB_STORE_H */
