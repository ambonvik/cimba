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
