/*
 * cmb_coroutine.c - general stackful coroutines
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
#include "cmb_coroutine.h"
#include "cmi_config.h"
#include "cmi_memutils.h"

/*
 * coroutine_main : A dummy coroutine to keep track of the main stack
 * pointer in context switches. No stack allocated, but pointers into the
 * normal system stack to simplify transfers back there.
 */
static CMB_THREAD_LOCAL struct cmb_coroutine *coroutine_main = NULL;

/*
 * coroutine_current : The currently executing coroutine, if any.
 * Initially NULL before any coroutines have been created, then the current
 * coroutine (including main when it has the CPU).
 */
static CMB_THREAD_LOCAL struct cmb_coroutine *coroutine_current = NULL;

/* Assembly functions, see src/arch/cmi_coroutine_context_*.asm */
extern void *cmi_coroutine_context_switch(void **old, void **new, void *ret);
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

/* OS specific C code, see src/arch/cmi_coroutine_context_*.c */
extern bool cmi_coroutine_stack_valid(const struct cmb_coroutine *cp);
extern void cmi_coroutine_context_init(struct cmb_coroutine *cp,
                                       cmb_coroutine_func *foo,
                                       void *arg);

/*
 * The coroutine struct contains data about current stack and where to return.
 * Execution context (such as registers) are pushed to and popped from the
 * coroutine's stack, pointed to from here. The *stack is the raw address of the
 * allocated stack, *stack_base the top (growing down), *stack_limit the end as
 * seen by the OS. Alignment requirements may cause minor differences, hence
 * maintaining several pointers here for different purposes.
 *
 * Parent is the coroutine that first activated (started) this coroutine, and
 * where control is passed when and if the coroutine function returns or exits.
 * Hence, cmb_coroutine_exit(arg) => cmb_coroutine_transfer(this, parent, arg).
 *
 * Caller is the coroutine that last (re)activated this coroutine, and where
 * control is passed when and if the coroutine yields.
 * cmb_coroutine_yield(arg) => cmb_coroutine_transfer(this, caller, arg).
 *
 * Initially, caller and parent will be the same, only differing if the
 * coroutine later gets reactivated by some other coroutine.
 *
 * Invariant: stack_base > stack_pointer > stack_limit >= stack.
 * Using unsigned char * as raw byte addresses to have same offset calculation
 * here as in the assembly code.
 */
struct cmb_coroutine {
    struct cmb_coroutine *parent;
    struct cmb_coroutine *caller;
    unsigned char *stack;
    unsigned char *stack_base;
    unsigned char *stack_limit;
    unsigned char *stack_pointer;
    enum cmb_coroutine_state status;
    void *exit_value;
};

struct cmb_coroutine *cmb_coroutine_get_current(void)
{
    return coroutine_current;
}

struct cmb_coroutine *cmb_coroutine_get_main(void)
{
    return coroutine_main;
}

enum cmb_coroutine_state cmb_coroutine_get_status(const struct cmb_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    return cp->status;
}

void *cmb_coroutine_get_exit_value(const struct cmb_coroutine *cp)
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
    coroutine_main->status = CMB_CORO_RUNNING;
    coroutine_main->exit_value = NULL;
    coroutine_current = coroutine_main;
}

/*
 * cmb_coroutine_create : Create a coroutine with the given stack size,
 * creating a main coroutine object if not done already.
 */
struct cmb_coroutine *cmb_coroutine_create(const size_t stack_size)
{
    /* Create a dummy main coroutine if not already existing */
    if (coroutine_main == NULL) {
        coroutine_create_main();
        cmb_assert_debug(coroutine_main != NULL);
        cmb_assert_debug(coroutine_current == coroutine_main);
     }

    /* Create the new coroutine and stack */
    struct cmb_coroutine *cp = cmi_malloc(sizeof(*cp));
    cp->parent = NULL;
    cp->caller = NULL;
    cp->stack = cmi_malloc(stack_size);
    cp->stack_base = cp->stack + stack_size;
    cp->stack_limit = NULL;
    cp->stack_pointer = NULL;
    cp->status = CMB_CORO_CREATED;
    cp->exit_value = NULL;

    return cp;
}

/*
 * cmb_coroutine_start : Load the given function and argument into the given
 * coroutine stack, and launch it by transferring control into it.
 */
void *cmb_coroutine_start(struct cmb_coroutine *cp,
                         cmb_coroutine_func *foo,
                         void *arg)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp->status == CMB_CORO_CREATED);
    cmb_assert_debug(coroutine_current != NULL);

    /* Prepare the stack for launching the coroutine function */
    cmi_coroutine_context_init(cp, foo, arg);
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    /* The current coroutine now becomes both the parent and caller of cp */
    cp->parent = coroutine_current;
    cp->caller = coroutine_current;

    /* Start it by transferring into it for the first time */
    cp->status = CMB_CORO_RUNNING;
    void *ret = cmb_coroutine_transfer(cp, arg);

    return ret;
}

/*
 * cmb_coroutine_destroy : Free memory allocated for coroutine stack.
 * The given coroutine cannot be main or the currently executing.
 */
void cmb_coroutine_destroy(struct cmb_coroutine *victim)
{
    cmb_assert_debug(victim != NULL);
    cmb_assert_debug(victim != coroutine_main);
    cmb_assert_debug(victim != coroutine_current);

    if (victim->stack != NULL) {
        cmi_free(victim->stack);
    }

    cmi_free(victim);
}

/*
 * cmb_coroutine_exit : End the currently executing coroutine, storing its
 * return value in the coroutine struct. Cannot be the main coroutine.
 *
 * Note that just returning from a coroutine function will be redirected by
 * assembly code as a call to cmb_coroutine_exit with the return value as its
 * argument.
 */
void cmb_coroutine_exit(void *retval)
{
    /* TODO: For now, just assert that it is not main. Later figure out exit from pthread */
    cmb_assert_release(coroutine_current != NULL);
    cmb_assert_release(coroutine_current != coroutine_main);
    cmb_assert_release(coroutine_current->status == CMB_CORO_RUNNING);

    struct cmb_coroutine *cp = coroutine_current;
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));

    cp->exit_value = retval;
    cp->status = CMB_CORO_FINISHED;
    cmb_coroutine_transfer(cp->parent, retval);
}

/*
 * cmb_coroutine_stop : End some coroutine. Equivalent to
 * cmb_coroutine_exit(NULL) if called on itself.
 */
void cmb_coroutine_stop(struct cmb_coroutine *victim)
{
    cmb_assert_release(victim != NULL);
    cmb_assert_release(victim->status == CMB_CORO_RUNNING);
    cmb_assert_debug(cmi_coroutine_stack_valid(victim));

    if (victim == cmb_coroutine_get_current()) {
        cmb_coroutine_exit(NULL);
    }
    else {
        victim->status = CMB_CORO_FINISHED;
    }
}

/*
 * cmb_coroutine_transfer : Symmetric (and general) coroutine pattern,
 * transferring control to whatever coroutine is given, with arg as the
 * value to be returned from the other side of the transfer call.
 *
 * Returns whatever return value the other coroutine passed back as the
 * argument in its transfer call back to this one. Control may have passed
 * through several other coroutines in the meantime, may not be returning
 * from the one we just switched into.
 */
extern void *cmb_coroutine_transfer(struct cmb_coroutine *to, void *arg)
{
    cmb_assert_release(to != NULL);
    cmb_assert_release(to->status == CMB_CORO_RUNNING);
    cmb_assert_debug(cmi_coroutine_stack_valid(to));

    struct cmb_coroutine *from = coroutine_current;
    cmb_assert_debug(from != NULL);
    cmb_assert_debug(cmi_coroutine_stack_valid(from));

    /* May pass through here on its way out from cmb_coroutine_exit */
    cmb_assert_release((from->status == CMB_CORO_RUNNING)
                    || (from->status == CMB_CORO_FINISHED));
    to->caller = from;
    coroutine_current = to;

    /* The actual context switch happens in assembly */
    void **fromstk = (void **)&(from->stack_pointer);
    void **tostk = (void **)&(to->stack_pointer);
    void *ret = cmi_coroutine_context_switch(fromstk, tostk, arg);

    /* Possibly much later, when control has returned here again */
    cmb_assert_debug(cmi_coroutine_stack_valid(to));
    cmb_assert_debug(cmi_coroutine_stack_valid(from));

    return ret;
}

/* Asymmetric coroutine pattern yield/resume, called from within coroutine */
void *cmb_coroutine_yield(void *arg)
{
    const struct cmb_coroutine *from = cmb_coroutine_get_current();
    cmb_assert_release(from != NULL);
    cmb_assert_release(from->status == CMB_CORO_RUNNING);

    struct cmb_coroutine *to = from->caller;
    cmb_assert_release(to != NULL);
    cmb_assert_release(to->status == CMB_CORO_RUNNING);

    void *ret = cmb_coroutine_transfer(to, arg);

    /* Possibly much later */
    return ret;
}

void *cmb_coroutine_resume(struct cmb_coroutine *cp, void *arg)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_release(cp != cmb_coroutine_get_current());
    cmb_assert_release(cp->status == CMB_CORO_RUNNING);

    void *ret = cmb_coroutine_transfer(cp, arg);

    /* Possibly much later */
    return ret;
}
