/*
 * cmb_coroutine.h - general stackful coroutines
 *
 * This "base class" covers both symmetric and asymmetric coroutine behavior:
 * - Symmetric coroutines can transfer control to any other coroutine in a
 *   peer-to-peer relationship, using the cmi_transfer(to, arg) function.
 *   A "from" argument is not necessary, since only one coroutine can have the
 *   CPU execution thread at a time, and it will always be the currently
 *   executing coroutine (coroutine_current) that is initiating the transfer.
 *   The arg argument will reappear as the return value of the cmb_transfer()
 *   on the receiving end.
 *
 * - Asymmetric coroutines only transfer control back to a caller coroutine,
 *   often on the main stack. This coroutine then selects the next one to
 *   be activated. This is done by cmi_yield(arg) / cmi_resume(to, arg)
 *   pairs. Again, the "from" argument is not needed, since it can only be
 *   called by the current coroutine at that point in time. When yielding,
 *   control passes to the coroutine that last resumed the active coroutine,
 *   or that otherwise last transferred control into it.
 *   The argument passed through yield() appears as the return value of
 *   resume(), and vice versa, the argument given to resume() appears as the
 *   return value from yield() on the other end of the implicit transfer.
 *
 * The cmb_coroutines can do both patterns, and can mix freely between them.
 * E.g., the main execution thread can transfer control into some coroutine,
 * which can act in a yield/resume asymmetric producer/consumer relationship
 * with some other coroutine for a while, before transferring control to yet
 * another coroutine, which could abruptly decide to return to main.
 *
 * Coroutines can also be nested by creating and starting coroutines from other
 * coroutines. If the coroutine function returns, it will transfer control back
 * to the context it was started from. This will appear to the caller as a
 * return from where it last transferred control out, not necessarily from the
 * call to start the daughther coroutine.
 *
 * If exploiting this fully, the control flow can get mightily confusing fast.
 * It should be considered low-level code not to be called directly by
 * user applications, but can on the other hand be used as such independent of
 * the rest of the Cimba library.
 *
 * See also:
 *      https://en.wikipedia.org/wiki/Coroutine
 *      https://dl.acm.org/doi/pdf/10.1145/1462166.1462167
 *      https://github.com/HirbodBehnam/UserContextSwitcher
 *      https://probablydance.com/2013/02/20/handmade-coroutines-for-windows/
 *      https://github.com/edubart/minicoro
 *      https://github.com/tidwall/neco
 *      https://github.com/hnes/libaco
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

#ifndef CIMBA_CMB_COROUTINE_H
#define CIMBA_CMB_COROUTINE_H

#include <stddef.h>

/*
 * enum cmb_coroutine_state : Possible states of a coroutine
 * Running means that it has been started and has not yet ended, not necessarily
 * that it is the coroutine currently executing instructions. Control can only
 * be passed to coroutines in the running state.
 */
enum cmb_coroutine_state {
    CMB_COROUTINE_CREATED = 0,
    CMB_COROUTINE_RUNNING = 1,
    CMB_COROUTINE_FINISHED = 2
};

/*
 * struct cmb_coroutine : Contains pointers to the coroutine's own stack, its
 * current state and exit value (if finished), and where to return from here.
 *
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

/*
 * typedef cmb_coroutine_func : The generic coroutine function type
 */
typedef void *(cmb_coroutine_func)(struct cmb_coroutine *cp, void *arg);

/*
 * cmb_coroutine_get_current : Return a pointer to the currently executing
 * coroutine, i.e. a self pointer for where the function is called from.
 * Will return NULL if no coroutines have yet been initiated.
 */
extern struct cmb_coroutine *cmb_coroutine_get_current(void);

/*
 * cmb_coroutine_get_main : Return a pointer to the main coroutine, possibly
 * NULL if it has not yet been created.
 */
extern struct cmb_coroutine *cmb_coroutine_get_main(void);

/*****************************************************************************/
/*         Functions acting on some (other) coroutine                        */
/*****************************************************************************/

/*
 * cmb_coroutine_create : Create a new coroutine with the given stack size.
 * The stack size should be large enough for the functions running in the
 * coroutine. For a simple case without deeply nested function calls and
 * many local variables, 10 kB could be sufficient, 24 kB probably on the
 * safe side. The program will either trigger an assert or segfault if the
 * stack was too small.
 */
extern struct cmb_coroutine *cmb_coroutine_create(size_t stack_size);

/*
 * cmb_coroutine_start : Launch the given coroutine, executing foo(arg) on
 * its own stack. This will transfer control into the new coroutine and only
 * return when that (or some other) coroutine yields / transfers back here.
 * The value returned from cmb_coroutine_start is whatever was returned by
 * the transfer here again.
 */
extern void *cmb_coroutine_start(struct cmb_coroutine *cp,
                                 cmb_coroutine_func foo,
                                 void *arg);

/*
 * cmb_coroutine_stop : Kill the given coroutine, setting its status to
 * CMB_COROUTINE_FINISHED and its exit value to NULL. If cp points to the current
 * coroutine, this has the same effect as it returning NULL or calling
 * cmb_coroutine_exit(NULL).
 */
extern void cmb_coroutine_stop(struct cmb_coroutine *cp);

/*
 * cmb_coroutine_get_status : Return the current state of the given coroutine.
 */
extern enum cmb_coroutine_state cmb_coroutine_get_status(const struct cmb_coroutine *cp);

/*
 * cmb_coroutine_get_exit_value : Return the exit value of the given coroutine,
 * NULL if it has not yet returned (or if it returned NULL).
 */
extern void *cmb_coroutine_get_exit_value(const struct cmb_coroutine *cp);

/*
 * cmb_coroutine_destroy : Free memory allocated to coroutine.
 */
extern void cmb_coroutine_destroy(struct cmb_coroutine *cp);

/*****************************************************************************/
/*         Functions called from within the coroutine                        */
/*****************************************************************************/

/*
 * cmb_coroutine_transfer : Symmetric coroutine pattern, transferring control
 * to given coroutine. The second argument arg will appear as the return value
 * on the receiving end of the transfer.
 */
extern void *cmb_coroutine_transfer(struct cmb_coroutine *to, void *arg);

/*
 * cmb_coroutine_yield : Asymmetric coroutine pattern, transfer back to latest
 * caller, i.e. the coroutine that last resumed this one or transferred to it.
 */
extern void *cmb_coroutine_yield(void *arg);

/*
 * cmb_coroutine_resume : Asymmetric coroutine pattern, transfer control to the
 * given coroutine. Equivalent to cmb_coroutine_transfer(cp, arg).
 */
extern void *cmb_coroutine_resume(struct cmb_coroutine *cp, void *arg);

/*
 * cmb_coroutine_exit : End the currently executing coroutine and store the
 * given argument as its exit value. Same as returning from the coroutine
 * function with the return value retval.
 */
extern void cmb_coroutine_exit(void *retval);

#endif /* CIMBA_CMB_COROUTINE_H */