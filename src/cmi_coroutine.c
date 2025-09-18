/*
 * cmi_coroutine.c - general stackful coroutines
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
#include <stddef.h>

#include "cmb_assert.h"
#include "cmi_config.h"
#include "cmi_coroutine.h"

/*
 * The coroutine struct contains data about current stack and where to return.
 * Execution context (such as registers) are pushed to and popped from the
 * coroutine's stack, not stored here. The *stack is the raw address of the
 * allocated stack, *stack_base the top (growing down), *stack_limit the end as
 * seen by the OS. Alignment requirements may cause minor differences, hence
 * maintaining several pointers here for different purposes.
 * Parent is the coroutine that first activated this coroutine, and where
 * control is passed when and if the coroutine function returns or exits.
 * Caller is the coroutine that last (re)activated this coroutine, and where
 * control is passed when and if the coroutine yields = transfer(this, caller).
 * Invariant: stack_base > stack_pointer > stack_limit >= stack.
 */
struct cmi_coroutine {
    void *parent_stack_pointer;
    void *caller_stack_pointer;
    void *stack_pointer;
    void *stack_base;
    void *stack_limit;
    void *stack;
    enum cmi_coroutine_state status;
    void *exit_value;
};

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
extern void cmi_coroutine_launcher(void);
extern void *cmi_coroutine_get_rsp(void);
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

void *cmi_coroutine_stack_init(void *raw_stack, size_t stack_size) {
    void *aligned_stack_base = NULL;

    /* Ensure that the top of the stack is correctly aligned */
    /* Write in the trampoline as return address post transfer */
    /* Load the stack record with correct register values */
    /* Parameters for call to trampoline */
    /* parameters for call to exit (if returning from coroutine function) */
    /* Stack pointers for this stack */

    return aligned_stack_base;
}

struct cmi_coroutine *cmi_coroutine_create(void) {
    /* Create and set up stack */
    /* Create a dummy main coroutine if not already existing */
    /* ??? How to ensure that the main coroutine gets deleted when this pthread exits ??? */
    /* Set coroutine stack pointers */
    return NULL;
}

void *cmi_coroutine_start(struct cmi_coroutine *cp, cmi_coroutine_func *foo, void *arg) {
    /* start the thing by transferring into it for the first time, loading register values from new stack */
    return NULL;
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