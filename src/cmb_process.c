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

struct cmb_process *cmb_process_create(const char *name,
                                              cmb_process_func foo,
                                              void *context,
                                              int16_t priority) {
    return NULL;
}

void cmb_process_destroy(struct cmb_process *cp)
{

}

void cmb_process_start(struct cmb_process *cp)
{

}

char *cmb_process_get_name(struct cmb_process *cp)
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

void *cmb_process_get_context(struct cmb_process *cp)
{
    cmb_assert_release(cp != NULL);

    return cmi_coroutine_get_context((struct cmi_coroutine *)cp);
}

void *cmb_process_set_context(struct cmb_process *cp, void *context)
{
    cmb_assert_release(cp != NULL);

    return cmi_coroutine_set_context((struct cmi_coroutine *)cp, context);
}

int16_t cmb_process_get_priority(struct cmb_process *cp)
{
    return cp->priority;
}

int16_t cmb_process_set_priority(struct cmb_process *cp, int16_t pri)
{
    int16_t oldpri = cp->priority;
    cp->priority = pri;

    return oldpri;
}

struct cmb_process *cmb_process_get_current(void)
{
    return (struct cmb_process *)cmi_coroutine_get_current();
}

