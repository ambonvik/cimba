/*
 * cmi_process.c - the simulated processes
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
#include <stdio.h>

#include "cmb_assert.h"
#include "cmb_process.h"

#include "cmi_memutils.h"

struct cmb_process *cmb_process_create(const char *name,
                                       cmb_process_func foo,
                                       void *context,
                                       int16_t priority)
{
    /* Allocate memory and initialize the cmi_coroutine parts */
    struct cmb_process *cp = cmi_malloc(sizeof(*cp));
    cmi_coroutine_init((struct cmi_coroutine *)cp,
                       (cmi_coroutine_func *)foo,
                       context,
              CMB_PROCESS_STACK_SIZE);

    /* Initialize the cmi_process parts */
    (void)cmb_process_set_name(cp, name);
    cp->priority = priority;
    cp->wakeup_handle = 0ull;

    return cp;
}

void cmb_process_destroy(struct cmb_process *cp)
{
    cmb_assert_debug(cp != NULL);

    struct cmi_coroutine *crp = (struct cmi_coroutine *)cp;
    cmb_assert_debug(crp != cmi_coroutine_get_main());
    cmb_assert_debug(crp != cmi_coroutine_get_current());

    if (crp->stack != NULL) {
        cmi_free(crp->stack);
    }

    cmi_free(cp);
}

void cmb_process_start(struct cmb_process *cp)
{

}

const char *cmb_process_get_name(const struct cmb_process *cp)
{
    cmb_assert_release(cp != NULL);

    return cp->name;
}

char *cmb_process_set_name(struct cmb_process *cp, const char *name)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(name != NULL);

    const int r = snprintf(cp->name, CMB_PROCESS_NAMEBUF_SZ, "%s", name);
    cmb_assert_release(r >= 0);

    return cp->name;
}

void *cmb_process_get_context(const struct cmb_process *cp)
{
    cmb_assert_release(cp != NULL);

    return cmi_coroutine_get_context((struct cmi_coroutine *)cp);
}

void *cmb_process_set_context(struct cmb_process *cp, void *context)
{
    cmb_assert_release(cp != NULL);

    return cmi_coroutine_set_context((struct cmi_coroutine *)cp, context);
}

int16_t cmb_process_get_priority(const struct cmb_process *cp)
{
    cmb_assert_release(cp != NULL);

    return cp->priority;
}

int16_t cmb_process_set_priority(struct cmb_process *cp, const int16_t pri)
{
    cmb_assert_release(cp != NULL);

    const int16_t oldpri = cp->priority;
    cp->priority = pri;

    return oldpri;
}

struct cmb_process *cmb_process_get_current(void)
{
    return (struct cmb_process *)cmi_coroutine_get_current();
}

static void process_wakeup_event(void *cp, void *arg)
{

}

static void process_interrupt_event(void *cp, void *arg)
{

}

int64_t cmb_process_hold(double dur)
{
    /* Schedule a wakeup call at time + dur and yield */
    return 0;
}

void cmb_process_exit(void *retval)
{

}

void cmb_process_interrupt(struct cmb_process *cp, int64_t sig, int16_t pri)
{
    /* Schedule an interrupt for cp at the current time.   */

}

void cmb_process_stop(struct cmb_process *cp)
{

}