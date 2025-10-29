/*
 * cmi_resource.h - guarded resources that the processes can queue for
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
#include "cmb_resource.h"

#include "cmi_memutils.h"


/*
 * guard_check : Test if heap_tag *a should go before *b. If so, return true.
 * Ranking higher priority (dkey) before lower, then FIFO based on handle value.
 */
static bool guard_check(const struct cmi_heap_tag *a,
                             const struct cmi_heap_tag *b)
{
    cmb_assert_debug(a != NULL);
    cmb_assert_debug(b != NULL);

    bool ret = false;
    if (a->ikey > b->ikey) {
        ret = true;
    }
    else if (a->ikey == b->ikey) {
        if (a->handle < b->handle) {
            ret = true;
        }
    }

    return ret;
}

struct cmi_resource_guard *cmi_resource_guard_create(void)
{
    struct cmi_resource_guard *rgp = cmi_malloc(sizeof *rgp);

    return rgp;
}

/* Start very small and fast, 2^GUARD_INIT_EXP = 8 slots in the initial queue */
#define GUARD_INIT_EXP 3u

void cmi_resource_guard_initialize(struct cmi_resource_guard *rgp)
{
    cmb_assert_release(rgp != NULL);

    cmi_hashheap_initialize((struct cmi_hashheap *)rgp,
                            GUARD_INIT_EXP,
                            guard_check);
}

void cmi_resource_guard_terminate(struct cmi_resource_guard *rgp)
{
    cmb_assert_release(rgp != NULL);

    cmi_hashheap_terminate((struct cmi_hashheap *)rgp);
}

void cmi_resource_guard_destroy(struct cmi_resource_guard *rgp)
{
    cmb_assert_release(rgp != NULL);

    cmi_resource_guard_terminate(rgp);
    cmi_free(rgp);
}

/*
 * cmi_resource_guard_wait : Enqueue and suspend the calling process until it
 * reaches the front of the priority queue and its demand function returns true.
 * ctx is whatever context the demand function needs to evaluate if it is
 * satisfied or not, such as the number of units needed from the resource or
 * something more complex and user application defined.
 * Returns whatever signal was received when the process was reactivated.
 * Cannot be called from the main process, will fire an assert if attempted.
 */
int64_t cmi_resource_guard_wait(struct cmi_resource_guard *rgp,
                                struct cmb_resource *rp,
                                cmb_resource_demand_func *demand,
                                void *ctx)
{
    cmb_assert_release(rgp != NULL);
    cmb_assert_release(demand != NULL);
    cmb_assert_release(ctx != NULL);

    /* cmb_process_get_current returns NULL if called from the main process */
    struct cmb_process *pp = cmb_process_get_current();
    cmb_assert_release(pp != NULL);

    /* Check if demand is already satisfied */
    if ((*demand)(rp, pp, ctx) == true) {
        return CMB_PROCESS_WAIT_NORMAL;
    }

    /* Contrived cast to suppress compiler warning about pointer conversion */
    void *vdemand = *(void **)&demand;

    const int64_t priority = cmb_process_get_priority(pp);
    const double entry_time = cmb_time();

    cmi_hashheap_enqueue((struct cmi_hashheap *)rgp,
                         (void *)pp,
                         vdemand,
                         ctx,
                         NULL,
                         entry_time,
                         priority);

    /* Yield to the scheduler, collect the return signal value when resumed */
    const int64_t ret = (int64_t)cmi_coroutine_yield(NULL);

    /*
     * Back here again, possibly much later. Return whatever signal was passed
     * through the resume call that took us back here.
     */
    return ret;

}

uint64_t cmi_resource_guard_cancel(struct cmi_resource_guard *rgp,
                                   struct cmb_process *pp)
{
    cmb_assert_release(rgp != NULL);

    return cmi_hashheap_pattern_cancel((struct cmi_hashheap *)rgp,
                                       (void *)pp,
                                       CMI_ANY_ITEM,
                                       CMI_ANY_ITEM,
                                       CMI_ANY_ITEM);
}
