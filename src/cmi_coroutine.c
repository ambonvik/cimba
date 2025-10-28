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
#include "cmi_coroutine.h"
#include "cmi_config.h"
#include "cmi_memutils.h"

/*
 * coroutine_main : A dummy coroutine to keep track of the main stack
 * pointer in context switches. No stack allocated, but pointers into the
 * normal system stack to simplify transfers back there.
 */
static CMB_THREAD_LOCAL struct cmi_coroutine *coroutine_main = NULL;

/*
 * coroutine_current : The currently executing coroutine, if any.
 * Initially NULL before any coroutines have been created, then the current
 * coroutine (including main when it has the CPU).
 */
static CMB_THREAD_LOCAL struct cmi_coroutine *coroutine_current = NULL;

/* Assembly functions, see src/arch/cmi_coroutine_context_*.asm */
extern void *cmi_coroutine_context_switch(void **old, void **new, void *ret);
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

/* OS specific C code, see src/arch/cmi_coroutine_context_*.c */
extern bool cmi_coroutine_stack_valid(const struct cmi_coroutine *cp);
extern void cmi_coroutine_context_init(struct cmi_coroutine *cp);

/* Simple getters and putters, not inlined to avoid exposing internals */
struct cmi_coroutine *cmi_coroutine_get_current(void)
{
    return coroutine_current;
}

struct cmi_coroutine *cmi_coroutine_get_main(void)
{
    return coroutine_main;
}

enum cmi_coroutine_state cmi_coroutine_get_status(const struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);

    return cp->status;
}

void *cmi_coroutine_get_context(const struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);

    return cp->context;
}


void *cmi_coroutine_set_context(struct cmi_coroutine *cp, void *context)
{
    cmb_assert_release(cp != NULL);

    void *old_context = cp->context;
    cp->context = context;

    return old_context;
}

void *cmi_coroutine_get_exit_value(const struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);

    return cp->exit_value;
}

/*
 * coroutine_create_main : Helper function to set up the dummy main coroutine
 */
static void coroutine_create_main(void)
{
    cmb_assert_debug(coroutine_main == NULL);

    /* Allocate the coroutine struct, no parent or caller */
    coroutine_main = cmi_malloc(sizeof(*coroutine_main));
    coroutine_main->parent = NULL;
    coroutine_main->caller = NULL;

    /* Using system stack, no separate allocation */
    coroutine_main->stack = NULL;

    /* Get stack top and bottom from assembly. */
    coroutine_main->stack_base = cmi_coroutine_get_stackbase();
    coroutine_main->stack_limit = cmi_coroutine_get_stacklimit();

    /* Sttack pointer will be set first time we transfer out of it */
    coroutine_main->stack_pointer = NULL;

    /* I am running, therefore I am */
    coroutine_main->status = CMI_COROUTINE_RUNNING;
    coroutine_main->exit_value = NULL;
    coroutine_current = coroutine_main;
}

/*
 * cmi_coroutine_create : Create a coroutine object.
 */
struct cmi_coroutine *cmi_coroutine_create(void)
{
    struct cmi_coroutine *cp = cmi_malloc(sizeof(*cp));
    cmi_memset(cp, 0, sizeof(*cp));

    return cp;
}

void cmi_coroutine_initialize(struct cmi_coroutine *cp,
                        cmi_coroutine_func *crfoo,
                        void *context,
                        cmi_coroutine_exit_func *crbar,
                        const size_t stack_size)
{
    /* Create a dummy main coroutine if not already existing */
    if (coroutine_main == NULL) {
        coroutine_create_main();
        cmb_assert_debug(coroutine_main != NULL);
        cmb_assert_debug(coroutine_current == coroutine_main);
    }

    /* Initialize the coroutine struct and allocate stack */
    cp->parent = NULL;
    cp->caller = NULL;
    if (cp->stack == NULL) {
        cp->stack = cmi_malloc(stack_size);
        cp->stack_base = cp->stack + stack_size;
        cp->stack_limit = NULL;
        cp->stack_pointer = NULL;
    }
    else {
        cmb_assert_debug(cp->stack_base == cp->stack + stack_size);
    }

    cp->status = CMI_COROUTINE_CREATED;
    cp->cr_foo = crfoo;
    cp->context = context;
    cp->cr_exit = crbar;
    cp->exit_value = NULL;
}

/*
 * cmi_coroutine_reset : Reset the coroutine to initial state.
 * Can be restarted from the beginning by calling cmi_coroutine_start.
 */
void cmi_coroutine_reset(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp != coroutine_main);
    cmb_assert_debug(cp != coroutine_current);

    cp->status = CMI_COROUTINE_CREATED;
    cp->exit_value = NULL;
}

/*
 * cmi_coroutine_terminate : Reset the coroutine to initial state.
 */
void cmi_coroutine_terminate(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp != coroutine_main);
    cmb_assert_debug(cp != coroutine_current);
    cmb_assert_release(cp->status != CMI_COROUTINE_RUNNING);

    if (cp->stack != NULL) {
        cmi_free(cp->stack);
        cp->stack = NULL;
    }
}

/*
 * cmi_coroutine_destroy : Free memory allocated for coroutine and its stack.
 * The given coroutine cannot be main or the currently executing.
 */
void cmi_coroutine_destroy(struct cmi_coroutine *cp)
{
    cmb_assert_debug(cp != NULL);
    cmb_assert_debug(cp != coroutine_main);
    cmb_assert_debug(cp != coroutine_current);
    cmb_assert_release(cp->status != CMI_COROUTINE_RUNNING);

    cmi_coroutine_terminate(cp);
    cmi_free(cp);
}


/*
 * cmi_coroutine_start : Load the given function and argument into the given
 * coroutine stack, and launch it by transferring control into it.
 * Note that restarting a finished coroutine with the original function and
 * context is allowed, but trying to restart a running coroutine is an error.
 * It will be faster to restart a finished coroutine than to create a new, in
 * cases where this makes sense in the user application.
 */
void *cmi_coroutine_start(struct cmi_coroutine *cp, void *msg)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp->status != CMI_COROUTINE_RUNNING);
    cmb_assert_debug(coroutine_current != NULL);

    /* Prepare the stack for launching the coroutine function */
    cmi_coroutine_context_init(cp);
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    /* The current coroutine now becomes both the parent and caller of cp */
    cp->parent = coroutine_current;
    cp->caller = coroutine_current;

    cp->exit_value = NULL;
    cp->status = CMI_COROUTINE_RUNNING;

    /*
     * Start it by transferring into it for the first time, passing the
     * message msg and returning whatever message it passes back.
     */
    void *ret = cmi_coroutine_transfer(cp, msg);

    return ret;
}

/*
 * cmi_coroutine_exit : End the currently executing coroutine, storing its
 * return value in the coroutine struct. Cannot be the main coroutine.
 *
 * Note that just returning from a coroutine function will be redirected by
 * assembly code as a call to cmi_coroutine_exit with the return value as its
 * argument.
 */
void cmi_coroutine_exit(void *retval)
{
    /* TODO: For now, just assert that it is not main. Later figure out exit from pthread */
    cmb_assert_release(coroutine_current != NULL);
    cmb_assert_release(coroutine_current != coroutine_main);
    cmb_assert_release(coroutine_current->status == CMI_COROUTINE_RUNNING);

    struct cmi_coroutine *cp = coroutine_current;
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    cp->exit_value = retval;
    cp->status = CMI_COROUTINE_FINISHED;
    cmi_coroutine_transfer(cp->parent, retval);
}

/*
 * cmi_coroutine_stop : End some coroutine. Equivalent to
 * cmi_coroutine_exit(NULL) if called on itself.
 */
void cmi_coroutine_stop(struct cmi_coroutine *cp, void *retval)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp->status == CMI_COROUTINE_RUNNING);
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    if (cp == cmi_coroutine_get_current()) {
        cmi_coroutine_exit(retval);
    }
    else {
        cp->exit_value = retval;
        cp->status = CMI_COROUTINE_FINISHED;
    }
}

/*
 * cmi_coroutine_transfer : Symmetric (and general) coroutine pattern,
 * transferring control to whatever coroutine is given, with arg as the
 * value to be returned from the other side of the transfer call.
 *
 * Returns whatever return value the other coroutine passed back as the
 * argument in its transfer call back to this one. Control may have passed
 * through several other coroutines in the meantime, may not be returning
 * from the one we just switched into.
 */
extern void *cmi_coroutine_transfer(struct cmi_coroutine *to, void *msg)
{
    cmb_assert_release(to != NULL);
    cmb_assert_release(to->status == CMI_COROUTINE_RUNNING);
    cmb_assert_debug(cmi_coroutine_stack_valid(to));

    struct cmi_coroutine *from = coroutine_current;
    cmb_assert_debug(from != NULL);
    cmb_assert_debug(cmi_coroutine_stack_valid(from));

    /* May pass through here on its way out from cmi_coroutine_exit */
    cmb_assert_release((from->status == CMI_COROUTINE_RUNNING)
                    || (from->status == CMI_COROUTINE_FINISHED));
    to->caller = from;
    coroutine_current = to;

    /* The actual context switch happens in assembly */
    void **fromstk = (void **)&(from->stack_pointer);
    void **tostk = (void **)&(to->stack_pointer);
    void *ret = cmi_coroutine_context_switch(fromstk, tostk, msg);

    /* Possibly much later, when control has returned here again */
    cmb_assert_debug(cmi_coroutine_stack_valid(to));
    cmb_assert_debug(cmi_coroutine_stack_valid(from));

    return ret;
}

/* Asymmetric coroutine pattern yield/resume, called from within coroutine */
void *cmi_coroutine_yield(void *msg)
{
    const struct cmi_coroutine *from = cmi_coroutine_get_current();
    cmb_assert_release(from != NULL);
    cmb_assert_release(from->status == CMI_COROUTINE_RUNNING);

    struct cmi_coroutine *to = from->caller;
    cmb_assert_release(to != NULL);
    cmb_assert_release(to->status == CMI_COROUTINE_RUNNING);

    void *ret = cmi_coroutine_transfer(to, msg);

    /* Possibly much later */
    return ret;
}

void *cmi_coroutine_resume(struct cmi_coroutine *cp, void *msg)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp != cmi_coroutine_get_current());
    cmb_assert_release(cp->status == CMI_COROUTINE_RUNNING);

    void *ret = cmi_coroutine_transfer(cp, msg);

    /* Possibly much later */
    return ret;
}
