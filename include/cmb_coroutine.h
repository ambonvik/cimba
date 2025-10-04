/*
 * cmb_coroutine.h - general stackful coroutines
 *
 * This "base class" covers both symmetric and asymmetric coroutine behavior:
 * - Symmetric coroutines can transfer control to any other coroutine in a
 *   peer-to-peer relationship, using the cmi_transfer(to, arg) function.
 *   A "from" argument is not necessary, since only one coroutine can have the
 *   CPU execution thread at a time, and it will always be the currently
 *   executing coroutine (coroutine_current) that is initiating the transfer.
 *   The argument will reappear as the return value of the cmb_transfer() on the
 *   receiving end.
 *
 * - Asymmetric coroutines only transfer control back to a caller coroutine,
 *   often on the main stack. This coroutine then selects the next one to
 *   be activated. This is done by cmi_yield(arg) / cmi_resume(to, arg)
 *   pairs. Again, the "from" argument is not needed, since it can only be
 *   called by the current coroutine at that point in time. When yielding,
 *   control passes to the coroutine that last _resumed_ the active coroutine,
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
 * Transferring or yielding to a coroutine that is not running is an error.
 *
 * Coroutines can also be nested by creating and starting coroutines from other
 * coroutines. If the coroutine function returns, it will transfer control back
 * to the context it was _started_ from. This will appear to the caller as a
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
 * struct cmb_coroutine : Opaque struct, no user serviceable parts inside.
 * Contains the stack pointers and other housekeeping for the coroutines.
 * Only accessible through defined API functiongs. For implementation details,
 * see src/cmb_coroutine.c
 */
struct cmb_coroutine;

/*
 * enum cmb_coroutine_state : Possible states of a coroutine
 * Running means that it has been started and has not yet ended, not necessarily
 * that it is the coroutine currently executing instructions.
 */
enum cmb_coroutine_state {
    CMB_CORO_CREATED = 0,
    CMB_CORO_RUNNING = 1,
    CMB_CORO_FINISHED = 2
};

/* typedef cmb_coroutine_func : The generic coroutine function type */
typedef void *(cmb_coroutine_func)(struct cmb_coroutine *cp, void *arg);

/* Simple getters */
extern struct cmb_coroutine *cmb_coroutine_get_current(void);
extern struct cmb_coroutine *cmb_coroutine_get_main(void);
extern enum cmb_coroutine_state cmb_coroutine_get_status(const struct cmb_coroutine *cp);
extern void *cmb_coroutine_get_exit_value(const struct cmb_coroutine *cp);

/* Functions to manipulate (other) coroutines */
extern struct cmb_coroutine *cmb_coroutine_create(size_t stack_size);

/* Note that cmb_coroutine_start transfers into the new coroutine and only
 * returns when that (or some other) coroutine yields / transfers back here
 * passing some return value through the yield, resume, or transfer to here.
 */
extern void *cmb_coroutine_start(struct cmb_coroutine *cp,
                                 cmb_coroutine_func foo,
                                 void *arg);

extern void cmb_coroutine_stop(struct cmb_coroutine *victim);
extern void cmb_coroutine_destroy(struct cmb_coroutine *victim);

/* Equivalent to returning retval from the coroutine function. */
extern void cmb_coroutine_exit(void *retval);

/* Symmetric coroutine pattern, transferring to wherever */
extern void *cmb_coroutine_transfer(struct cmb_coroutine *to, void *arg);

/* Asymmetric coroutine pattern, always returning to caller */
extern void *cmb_coroutine_resume(struct cmb_coroutine *cp, void *arg);
extern void *cmb_coroutine_yield(void *arg);

#endif /* CIMBA_CMB_COROUTINE_H */