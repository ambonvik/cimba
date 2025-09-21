/*
 * cmi_coroutine.c - general stackful coroutines
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

#include <stdint.h>

#include "cmb_assert.h"
#include "cmi_config.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"

/*
 * cmi_coroutine_main : A dummy coroutine to keep track of the main stack
 * pointer in context switches. No stack allocated, but pointers into the
 * normal system stack to simplify transfers back there.
 * TODO: Figure out how to delete the main coroutine object before exiting pthread.
 */
static CMB_THREAD_LOCAL struct cmi_coroutine *cmi_coroutine_main = NULL;

/*
 * cmi_coroutine_current : The currently executing coroutine, if any.
 * Initially NULL before any coroutines have been created, then the current
 * coroutine (including main when it has the CPU).
 */
static CMB_THREAD_LOCAL struct cmi_coroutine *cmi_coroutine_current = NULL;

/* Assembly functions, see src/arch/cmi_coroutine_context_*.asm */
extern void *cmi_coroutine_context_switch(void **old, void **new, void *ret);
extern void *cmi_coroutine_get_rsp(void);
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

/* OS specific C code, see src/arch/cmi_coroutine_context_*.c */
extern void cmi_coroutine_context_init(struct cmi_coroutine *cp,
                                       cmi_coroutine_func *foo,
                                       void *arg,
                                       size_t stack_size);

/* Helper function to set up the dummy main coroutine */
static void cmi_coroutine_create_main(void) {
    cmb_assert_debug(cmi_coroutine_main == NULL);

    /* Allocate the coroutine struct, no parent or caller */
    cmi_coroutine_main = cmi_malloc(sizeof(*cmi_coroutine_main));
    cmi_coroutine_main->parent_stack_pointer = NULL;
    cmi_coroutine_main->caller_stack_pointer = NULL;

    /* Using system stack, no separate allocation */
    cmi_coroutine_main->stack = NULL;

    /* Get stack pointer and stack top / bottom from assembly */
    cmi_coroutine_main->stack_base = cmi_coroutine_get_stackbase();
    cmi_coroutine_main->stack_limit = cmi_coroutine_get_stacklimit();
    cmi_coroutine_main->stack_pointer = cmi_coroutine_get_rsp();

    /* I am running, therefore I am */
    cmi_coroutine_main->status = CMI_CORO_RUNNING;
    cmi_coroutine_main->exit_value = NULL;
    cmi_coroutine_current = cmi_coroutine_main;
}

struct cmi_coroutine *cmi_coroutine_create(cmi_coroutine_func *foo,
                                           void *arg,
                                           const size_t stack_size) {
    /* Create a dummy main coroutine if not already existing */
    if (cmi_coroutine_main == NULL) {
        cmi_coroutine_create_main();
        cmb_assert_debug(cmi_coroutine_main != NULL);
        cmb_assert_debug(cmi_coroutine_current == cmi_coroutine_main);
     }

    /* Create the new coroutine object and initialize */
    struct cmi_coroutine *cp = cmi_malloc(sizeof(*cp));
    cp->parent_stack_pointer = NULL;
    cp->caller_stack_pointer = NULL;
    cp->stack = NULL;
    cp->stack_base = NULL;
    cp->stack_limit = NULL;
    cp->stack_pointer = NULL;
    cp->status = CMI_CORO_CREATED;
    cp->exit_value = NULL;

    /* Create and initialize a stack for the new coroutine */
    cmi_coroutine_context_init(cp, foo, arg, stack_size);

    return cp;
}

void cmi_coroutine_start(struct cmi_coroutine *cp) {
    /* start the thing by transferring into it for the first time,
     * loading register values from new stack */
}

void cmi_coroutine_stop(struct cmi_coroutine *victim) {
    /* Kill it, exiting with return value NULL */
    /* If suicidal (killing the currently executing coroutine),
     * this is equivalent to cmi_coroutine_exit(myself, NULL)
     */
}

void cmi_coroutine_destroy(struct cmi_coroutine *victim) {

}

void cmi_coroutine_exit(struct cmi_coroutine *myself, void *retval) {
    /* Can do without the myself pointer, use current instead */
    cmb_assert_release(myself != NULL);
    myself->exit_value = retval;
    /* Destroy stack (?) and transfer to parent */
}

void *cmi_coroutine_get_exit_value(struct cmi_coroutine *corp) {
    return corp->exit_value;
}

extern struct cmi_coroutine *cmi_coroutine_get_current(void) {
    return NULL;
}

/* The state of the given coroutine */
extern enum cmi_coroutine_state cmi_coroutine_get_state(struct cmi_coroutine *corp) {
    return corp->status;
}

/* Symmetric coroutine pattern */
extern void *cmi_coroutine_transfer(struct cmi_coroutine *from,
                                    struct cmi_coroutine *to,
                                    void *arg) {
    return NULL;
}

/* Asymmetric coroutine pattern */
extern void *cmi_coroutine_yield(struct cmi_coroutine *from, void *arg) {
    return NULL;
}

extern void *cmi_coroutine_resume(struct cmi_coroutine *to, void *arg) {
    return NULL;
}