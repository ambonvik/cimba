/*
 * cmi_coroutine.c - general stackful coroutines
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
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
#include "cmi_coroutine.h"
#include "cmi_config.h"
#include "cmi_memutils.h"
#include "cmi_sanitizer.h"

/* The main and current coroutine pointers */
CMB_THREAD_LOCAL struct cmi_coroutine *coroutine_main = NULL;
CMB_THREAD_LOCAL struct cmi_coroutine *coroutine_current = NULL;

/* Backing storage for the per-thread main coroutine. In TLS so it is
 * reclaimed when the worker thread exits. */
static CMB_THREAD_LOCAL struct cmi_coroutine coroutine_main_storage;

/* Registry of allocated stacks to ensure that all get freed even on error */
static CMB_THREAD_LOCAL struct cmi_coroutine *coroutine_registry = NULL;

/* Assembly function, see src/port/x86-64/Linux/cmi_coroutine_context_*.asm */
extern void *cmi_coroutine_context_switch(void **old, void **new, void *ret);

/* OS-specific C code, see src/arch/cmi_coroutine_context_*.c */
extern bool cmi_coroutine_stack_valid(const struct cmi_coroutine *cp);
extern void cmi_coroutine_context_init(struct cmi_coroutine *cp);

/*
 * System dependent functions in port/x86-64/.../cmi_coroutine_context.c
 */

/* Get the stack base pointer (top of stack, grows downwards) */
extern unsigned char *cmi_coroutine_stackbase(void);
/* Get the stack limit pointer (bottom of stack) */
extern unsigned char *cmi_coroutine_stacklimit(void);
/* Get the raw memory address of the stack bottom */
extern unsigned char *cmi_coroutine_stackraw(void);
/* Allocate memory suitable for a stack */
extern unsigned char *cmi_coroutine_stack_alloc(size_t size,
                                                unsigned char **base,
                                                unsigned char **limit);
/* Free memory previously allocated for a stack */
extern void cmi_coroutine_stack_free(unsigned char *stack);

/* Helper functions for maintaining the stack registry */
static void registry_add(struct cmi_coroutine *cp)
{
    cp->reg_prev = NULL;
    cp->reg_next = coroutine_registry;
    if (coroutine_registry) {
        coroutine_registry->reg_prev = cp;
    }

    coroutine_registry = cp;
}

static void registry_remove(struct cmi_coroutine *cp)
{
    if (cp->reg_prev) {
        cp->reg_prev->reg_next = cp->reg_next;
    }
    else {
        coroutine_registry = cp->reg_next;
    }

    if (cp->reg_next) {
        cp->reg_next->reg_prev = cp->reg_prev;
    }
}

/*
 * create_main - Helper function to set up the dummy main coroutine
 */
static void create_main(void)
{
    cmb_assert_debug(coroutine_main == NULL);

    /* Store the coroutine struct in TLS, no parent or caller */
    coroutine_main = &coroutine_main_storage;
    coroutine_main->parent = NULL;
    coroutine_main->caller = NULL;

    /* Using system stack, no separate allocation */
    coroutine_main->stack = cmi_coroutine_stackraw();
    coroutine_main->stack_base = cmi_coroutine_stackbase();
    coroutine_main->stack_limit = cmi_coroutine_stacklimit();

    /* Stack pointer will be set the first time we transfer out of it */
    coroutine_main->stack_pointer = NULL;

    /* I am running, therefore, I am */
    coroutine_main->status = CMI_COROUTINE_RUNNING;
    coroutine_main->exit_value = NULL;
    coroutine_main->tsan_fiber = cmi_tsan_get_current_fiber();
    coroutine_current = coroutine_main;
}

/*
 * cmi_coroutine_create - Create a coroutine object.
 */
struct cmi_coroutine *cmi_coroutine_create(void)
{
    struct cmi_coroutine *cp = cmi_malloc(sizeof(*cp));
    cmi_memset(cp, 0, sizeof(*cp));

    return cp;
}

/*
 * cmi_coroutine_initialize - Create a stack context for the coroutine
 */
void cmi_coroutine_initialize(struct cmi_coroutine *cp,
                        cmi_coroutine_func *crfunction,
                        void *context,
                        cmi_coroutine_exit_func *crexit,
                        const size_t stack_size)
{
    /* Create a dummy main coroutine if not already existing */
    if (coroutine_main == NULL) {
        create_main();
        cmb_assert_debug(coroutine_main != NULL);
        cmb_assert_debug(coroutine_current == coroutine_main);
    }

    /* Initialize the coroutine struct and allocate the stack */
    cp->parent = NULL;
    cp->caller = NULL;
    cp->stack = cmi_coroutine_stack_alloc(stack_size, &(cp->stack_base), &(cp->stack_limit));
    /* Will be set on first transfer */
    cp->stack_pointer = NULL;

    cp->status = CMI_COROUTINE_CREATED;
    cp->cr_function = crfunction;
    cp->context = context;
    cp->cr_exit = crexit;
    cp->exit_value = NULL;
    cp->tsan_fiber = cmi_tsan_create_fiber();

    registry_add(cp);
}

/*
 * cmi_coroutine_reset - Reset the coroutine to the initial state.
 * Can be restarted from the beginning by calling cmi_coroutine_start.
 */
void cmi_coroutine_reset(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp != coroutine_main);
    cmb_assert_debug(cp != coroutine_current);

    /* Force TSan to reset its shadow stack, if used */
    cmi_tsan_destroy_fiber(cp->tsan_fiber);
    cp->tsan_fiber = cmi_tsan_create_fiber();

    cp->status = CMI_COROUTINE_CREATED;
    cp->exit_value = NULL;
}

/*
 * cmi_coroutine_terminate - Reset the coroutine to a newly created state.
 */
void cmi_coroutine_terminate(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp != coroutine_main);
    cmb_assert_debug(cp != coroutine_current);

    cmb_assert_debug(cp->stack != NULL);
    registry_remove(cp);
    cmi_tsan_destroy_fiber(cp->tsan_fiber);
    cmi_coroutine_stack_free(cp->stack);
    cmi_memset(cp, 0, sizeof(*cp));
}

/*
 * cmi_coroutine_destroy - Free memory allocated for a coroutine and its stack
 * if not already free'd by cmi_coroutine_terminate, which should properly be
 * called first by the user code.
 *
 * The given coroutine cannot be main or the currently executing coroutine.
 */
void cmi_coroutine_destroy(struct cmi_coroutine *cp)
{
    cmb_assert_debug(cp != NULL);
    cmb_assert_debug(cp != coroutine_main);
    cmb_assert_debug(cp != coroutine_current);

    if (cp->stack != NULL) {
        registry_remove(cp);
        cmi_tsan_destroy_fiber(cp->tsan_fiber);
        cmi_coroutine_stack_free(cp->stack);
    }

    cmi_free(cp);
}

/*
 * cmi_coroutine_start - Load the given function and argument into the given
 * coroutine stack and launch it by transferring control into it.
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
 * cmi_coroutine_exit - End the currently executing coroutine, storing its
 * return value in the coroutine struct. Cannot be the main coroutine.
 *
 * Note that just returning from a coroutine function will be redirected by
 * assembly code as a call to cmi_coroutine_exit with the return value as its
 * argument.
 */
void cmi_coroutine_exit(void *retval)
{
    cmb_assert_release(coroutine_current != NULL);
    cmb_assert_release(coroutine_current != coroutine_main);
    cmb_assert_release(coroutine_current->status == CMI_COROUTINE_RUNNING);

    struct cmi_coroutine *cp = coroutine_current;
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    cp->exit_value = retval;
    cp->status = CMI_COROUTINE_FINISHED;
    /* End of the current execution fiber, transfer safely somewhere else */
    cmi_coroutine_transfer(cp->parent, retval);
}

/*
 * cmi_coroutine_stop - End some coroutine. Equivalent to
 * cmi_coroutine_exit(NULL) if called on itself.
 */
void cmi_coroutine_stop(struct cmi_coroutine *cp, void *retval)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp->status == CMI_COROUTINE_RUNNING);
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    if (cp == cmi_coroutine_current()) {
        cmi_coroutine_exit(retval);
    }
    else {
        /* Not the current coroutine, control continues in the caller */
        cp->exit_value = retval;
        cp->status = CMI_COROUTINE_FINISHED;
    }
}

/*
 * cmi_coroutine_transfer - Symmetric (and general) coroutine pattern,
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

    /* Announce the fiber switch. ASan wants the destination stack bounds; if
     * `from` has finished it will never resume, so pass NULL and ASan discards
     * its fake stack instead of saving it. */
    void *asan_fake = NULL;
    cmi_asan_start_switch((from->status == CMI_COROUTINE_FINISHED) ? NULL : &asan_fake,
                          to->stack_limit,
                          (size_t)(to->stack_base - to->stack_limit));
    cmi_tsan_switch_fiber(to->tsan_fiber);

    /* The actual context switch happens in assembly */
    void **fromstk = (void **)&(from->stack_pointer);
    void **tostk = (void **)&(to->stack_pointer);
    void *ret = cmi_coroutine_context_switch(fromstk, tostk, msg);

    /* Possibly much later, when control has returned here again */
    cmi_asan_finish_switch(asan_fake);
    cmb_assert_debug(cmi_coroutine_stack_valid(to));
    cmb_assert_debug(cmi_coroutine_stack_valid(from));

    return ret;
}

/* Asymmetric coroutine pattern yield/resume, called from within coroutine */
void *cmi_coroutine_yield(void *msg)
{
    const struct cmi_coroutine *from = cmi_coroutine_current();
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
    cmb_assert_release(cp != cmi_coroutine_current());
    cmb_assert_release(cp->status == CMI_COROUTINE_RUNNING);

    void *ret = cmi_coroutine_transfer(cp, msg);

    /* Possibly much later */
    return ret;
}

struct cmi_coroutine *cmi_coroutine_current(void)
{
    return coroutine_current;
}

struct cmi_coroutine *cmi_coroutine_main(void)
{
    return coroutine_main;
}

/*
 * cmi_coroutine_launch - first-entry shim. The assembly trampoline calls this
 * as the new coroutine's R12 target. It finalizes the sanitizer fiber switch on
 * the fresh stack (the matching half of the start-switch done by whoever
 * transferred in) and then runs the real coroutine function. A fresh fiber has
 * no prior fake stack to restore, hence NULL. Routed through unconditionally;
 * the call vanishes in non-instrumented builds.
 */
void *cmi_coroutine_launch(struct cmi_coroutine *cp, void *arg)
{
    cmi_asan_finish_switch(NULL);
    return cp->cr_function(cp, arg);
}

/*
 * Cleanup handler to be called on thread termination,
 * will free() all still allocated stacks in this thread
 */
void cmi_coroutine_thread_cleanup(void *arg)
{
    cmb_unused(arg);

    while (coroutine_registry != NULL) {
        struct cmi_coroutine *cp = coroutine_registry;
        registry_remove(cp);
        cmi_coroutine_stack_free(cp->stack);
        cmi_tsan_destroy_fiber(cp->tsan_fiber);
        cmi_free(cp);
    }

    coroutine_current = coroutine_main;
}