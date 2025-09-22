/*
 * cmi_coroutine.h - general stackful coroutines
 *
 * Considered an internal (cmi_*) class since the complete semantics of
 * coroutines are a bit too general for simulation purposes, but we will
 * build the cmb_process class of asymmetric coroutines for direct use in
 * the simulation on top of these.
 *
 * This "base class" covers both symmetric and asymmetric coroutine behavior:
 * - Symmetric coroutines can transfer control to any other coroutine in a
 *   peer-to-peer relationship, using the cmi_transfer(from, to, arg) function.
 *   The argument will reappear as the return value of the cmi_transfer() on the
 *   receiving end.
 *
 * - Asymmetric coroutines only transfer control back to a caller coroutine,
 *   often on the main stack. This coroutine then selects the next one to
 *   be activated. This is done by cmi_yield(from, arg) / cmi_resume(to, arg)
 *   pairs. When yielding, control passes to the coroutine that last resumed the
 *   active coroutine. The argument passed through yield() appears as the return
 *   value of resume(), and vice versa, the argument given to resume() appears
 *   as the return value from yield().
 *
 * These coroutines can do both patterns, and can mix freely between them.
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

/*
 * Possible states of a coroutine
 * Running means that it has been started and has not yet ended, not necessarily
 * that it is the coroutine currently executing instructions.
 */
enum cmi_coroutine_state {
    CMI_CORO_CREATED = 0,
    CMI_CORO_RUNNING = 1,
    CMI_CORO_KILLED = 2,
    CMI_CORO_RETURNED = 3
};

/* Bit pattern for last 64 bits of valid stack. */
#define CMI_STACK_LIMIT_UNTOUCHED 0xACE0FBA5Eull

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
struct cmi_coroutine {
    struct cmi_coroutine *parent;
    struct cmi_coroutine *caller;
    unsigned char *stack;
    unsigned char *stack_base;
    unsigned char *stack_limit;
    unsigned char *stack_pointer;
    enum cmi_coroutine_state status;
    void *exit_value;
};

/* The generic coroutine function type */
typedef void *(cmi_coroutine_func)(struct cmi_coroutine *cp, void *arg);

/*
 * General functions to create, start, stop, and destroy coroutines
 */
extern struct cmi_coroutine *cmi_coroutine_create(size_t stack_size);
extern void cmi_coroutine_start(struct cmi_coroutine *cp,
                                cmi_coroutine_func foo,
                                void *arg);
extern void cmi_coroutine_stop(struct cmi_coroutine *victim);
extern void cmi_coroutine_exit(void *retval);
extern void cmi_coroutine_reset(struct cmi_coroutine *victim);
extern void cmi_coroutine_destroy(struct cmi_coroutine *victim);

/*
 * The currently executing coroutine, if any.
 */
extern struct cmi_coroutine *cmi_coroutine_get_current(void);

/* The state of the given coroutine */
extern enum cmi_coroutine_state cmi_coroutine_get_status(const struct cmi_coroutine *cp);

/* The exit value, if any. Will return NULL if the state is not _RETURNED */
extern void *cmi_coroutine_get_exit_value(const struct cmi_coroutine *corp);

/* Symmetric coroutine pattern */
extern void *cmi_coroutine_transfer(struct cmi_coroutine *to, void *arg);

/* Asymmetric coroutine pattern */
extern void *cmi_coroutine_yield(void *arg);
extern void *cmi_coroutine_resume(struct cmi_coroutine *cp, void *arg);

#endif /* CIMBA_CMB_COROUTINE_H */