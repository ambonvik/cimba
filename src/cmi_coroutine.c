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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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
extern bool cmi_coroutine_stack_valid(struct cmi_coroutine *cp);
extern void cmi_coroutine_context_init(struct cmi_coroutine *cp,
                                       cmi_coroutine_func *foo,
                                       void *arg);

/* Helper function to set up the dummy main coroutine */
static void cmi_coroutine_create_main(void) {
    cmb_assert_debug(cmi_coroutine_main == NULL);

    /* Allocate the coroutine struct, no parent or caller */
    cmi_coroutine_main = cmi_malloc(sizeof(*cmi_coroutine_main));
    cmi_coroutine_main->parent = NULL;
    cmi_coroutine_main->caller = NULL;

    /* Using system stack, no separate allocation */
    cmi_coroutine_main->stack = NULL;

    /* Get stack top / bottom from assembly */
    cmi_coroutine_main->stack_base = cmi_coroutine_get_stackbase();
    cmi_coroutine_main->stack_limit = cmi_coroutine_get_stacklimit();

    /* Sttack pointer will be set first time we transfer out of it */
    cmi_coroutine_main->stack_pointer = NULL;

    /* I am running, therefore I am */
    cmi_coroutine_main->status = CMI_CORO_RUNNING;
    cmi_coroutine_main->exit_value = NULL;
    cmi_coroutine_current = cmi_coroutine_main;
    printf("created main coroutine %p\n", cmi_coroutine_main);
}

struct cmi_coroutine *cmi_coroutine_create(const size_t stack_size) {
    /* Create a dummy main coroutine if not already existing */
    if (cmi_coroutine_main == NULL) {
        cmi_coroutine_create_main();
        cmb_assert_debug(cmi_coroutine_main != NULL);
        cmb_assert_debug(cmi_coroutine_current == cmi_coroutine_main);
     }

    /* Create the new coroutine and stack */
    struct cmi_coroutine *cp = cmi_malloc(sizeof(*cp));
    cp->parent = NULL;
    cp->caller = NULL;
    cp->stack = cmi_malloc(stack_size);
    cp->stack_base = cp->stack + stack_size;
    cp->stack_limit = NULL;
    cp->stack_pointer = NULL;
    cp->status = CMI_CORO_CREATED;
    cp->exit_value = NULL;

    return cp;
}

void cmi_coroutine_start(struct cmi_coroutine *cp,
                         cmi_coroutine_func *foo,
                         void *arg) {
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp->status == CMI_CORO_CREATED);
    cmb_assert_debug(cmi_coroutine_current != NULL);

    printf("starting coroutine %p func %p arg %p\n", (void *)cp, (void *)foo, arg);
    /* Prepare the stack for launching the coroutine function */
    cmi_coroutine_context_init(cp, foo, arg);
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    /* The current coroutine now becomes both the parent and caller of cp */
    cp->parent = cmi_coroutine_current;
    cp->caller = cmi_coroutine_current;

    /* Start it by transferring into it for the first time,
     * loading register values from the new stack */
    cp->status = CMI_CORO_RUNNING;
    printf("  transferring, cp %p arg %p\n", (void *)cp, arg);
    cmi_coroutine_transfer(cp, arg);
}

void cmi_coroutine_stop(struct cmi_coroutine *victim) {
    /* Kill it, exiting with return value NULL */
    /* If suicidal (killing the currently executing coroutine),
     * this is equivalent to cmi_coroutine_exit(myself, NULL)
     */
}

void cmi_coroutine_destroy(struct cmi_coroutine *victim) {
    cmb_assert_debug(victim != NULL);
    cmb_assert_debug(victim != cmi_coroutine_main);
    cmb_assert_debug(victim != cmi_coroutine_current);

    if (victim->stack != NULL) {
        cmi_free(victim->stack);
    }

    cmi_free(victim);
}

void cmi_coroutine_exit(void *retval) {
    /* TODO: For now, just assert that it is not main. Later figure out exit from pthread */
    cmb_assert_release(cmi_coroutine_current != NULL);
    cmb_assert_release(cmi_coroutine_current != cmi_coroutine_main);
    cmb_assert_release(*((uint64_t *)cmi_coroutine_current->stack_limit) == CMI_STACK_LIMIT_UNTOUCHED);

    printf("cmi_coroutine_exit, retval %p\n", retval);

    struct cmi_coroutine *cp = cmi_coroutine_current;
    printf("  current coroutine %p\n", (void *)cp);
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));
    cp->exit_value = retval;
    cp->status = CMI_CORO_RETURNED;
    printf("  transferring to parent %p arg %p\n", (void *)cp->parent, retval);
    cmi_coroutine_transfer(cp->parent, retval);
}

void *cmi_coroutine_get_exit_value(const struct cmi_coroutine *cp) {
    return cp->exit_value;
}

extern struct cmi_coroutine *cmi_coroutine_get_current(void) {
    return cmi_coroutine_current;
}

/* The state of the given coroutine */
extern enum cmi_coroutine_state cmi_coroutine_get_status(const struct cmi_coroutine *cp) {
    return cp->status;
}

/* Symmetric coroutine pattern, transferring to wherever */
extern void *cmi_coroutine_transfer(struct cmi_coroutine *to, void *arg) {
    cmb_assert_release(to != NULL);
    cmb_assert_debug(cmi_coroutine_stack_valid(to));
    cmb_assert_release((to == cmi_coroutine_main) || (*((uint64_t *)to->stack_limit) == CMI_STACK_LIMIT_UNTOUCHED));

    cmb_assert_debug(cmi_coroutine_current != NULL);
    struct cmi_coroutine *from = cmi_coroutine_current;
    cmb_assert_debug(cmi_coroutine_stack_valid(from));
    cmb_assert_release((from == cmi_coroutine_main) || (*((uint64_t *)from->stack_limit) == CMI_STACK_LIMIT_UNTOUCHED));
    to->caller = from;
    cmi_coroutine_current = to;

    /* The actual context switch in assembly */
    void **fromstk = (void **)&(from->stack_pointer);
    void **tostk = (void **)&(to->stack_pointer);
    printf("transfer from %p stackptr %p to %p stackptr %p\n", from, *fromstk, to, *tostk);
    void *ret = cmi_coroutine_context_switch(fromstk, tostk, arg);

    /* Possibly much later, when control has returned here again */
    cmb_assert_debug(cmi_coroutine_stack_valid(to));
    cmb_assert_release((to == cmi_coroutine_main) || (*((uint64_t *)to->stack_limit) == CMI_STACK_LIMIT_UNTOUCHED));
    return ret;
}

/* Asymmetric coroutine pattern, always transferring back to caller */
extern void *cmi_coroutine_yield(void *arg) {
    return NULL;
}

extern void *cmi_coroutine_resume(struct cmi_coroutine *cp, void *arg) {
    return NULL;
}