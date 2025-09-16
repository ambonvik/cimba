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
 * If exploiting this fully, the control flow can get mightily confusing rather
 * fast. It should be considered low-level code not to be called directly by
 * user applications.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
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
 * The coroutine data structure.
 * Opaque struct, no user serviceable parts inside.
 */
struct cmi_coroutine;

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

/* General functions to create, start, stop, and destroy coroutines */
extern struct cmi_coroutine *cmi_coroutine_create(void);
extern void *cmi_coroutine_start(struct cmi_coroutine *new);
extern void cmi_coroutine_stop(struct cmi_coroutine *victim);
extern void cmi_coroutine_destroy(struct cmi_coroutine *victim);

/*
 * The currently executing coroutine, if any.
 * Returns NULL if currently executing on the main stack.
 */
extern struct cmi_coroutine *cmi_coroutine_current(void);

/* The state of the given coroutine */
extern enum cmi_coroutine_state cmi_coroutine_state(struct cmi_coroutine *corp);

/* The exit value, if any. Will return NULL if the state is not _RETURNED */
extern void *cmi_coroutine_exit_value(struct cmi_coroutine *corp);

/* Symmetric coroutine pattern */
extern void *cmi_coroutine_transfer(struct cmi_coroutine *from,
                                    struct cmi_coroutine *to,
                                    void *arg);

/* Asymmetric coroutine pattern */
extern void *cmi_coroutine_yield(struct cmi_coroutine from, void *arg);
extern void *cmi_coroutine_resume(struct cmi_coroutine to, void *arg);

#endif /* CIMBA_CMB_COROUTINE_H */