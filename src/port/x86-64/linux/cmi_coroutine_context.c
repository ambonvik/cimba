/*
 * cmi_coroutine_context.c - Linux specific coroutine initialization
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


/* Make sure we get pthread_getattr_np and avoid Clang-Tidy complaints */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier)
#include <pthread.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <xmmintrin.h>

#include "cmb_assert.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"

/* Assembly function, see src/arc/cmi_coroutine_context_*.asm */
extern void cmi_coroutine_trampoline(void);

/* Intrusive singly linked list of recycled stacks */
struct stack_tag {
    size_t size;                /* Size of stacks in this list */
    unsigned char *head;        /* The intrusive list */
    struct stack_tag *next;     /* Next list, a different size stack */
};

CMB_THREAD_LOCAL static struct stack_tag *stack_list = NULL;

/*
 * Linux-specific code to allocate and initialize stack for a new coroutine,
 * see https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
 *
 * Populates the new stack with register values to be loaded when the
 * new coroutine gets activated for the first time. The context switch into
 * it happens in assembly, function cmi_coroutine_context_switch, see
 * src/port/x86-64/Linux//cmi_coroutine_context_*.asm
 *
 * Overall structure of the Linux stack:
 *  - Grows downwards, from high address.
 *  - The top must be 16-byte aligned.
 *  - The first six function arguments are passed in registers RDI, RSI, RDX,
 *    RCX, R8, and R9. Anything more is passed on the stack in reverse order.
 *  - The return instruction pointer (RIP) follows next, before the function's
 *    own stack frame for storing registers and local variables.
 *  - When first entering a function, the stack is 8 bytes off the 16-byte
 *    alignment, since the return instruction pointer is pushed to a previously
 *    aligned stack.
 *
 * Here, we set up a context with the launcher/trampoline function as the
 * "return" address and register values that prepare for launching the
 * coroutine function cr_function(coro, arg) on first transfer, and for calling
 * cmi_coroutine_exit to catch its exit value if the coroutine function ever
 * returns.
 *
 * In our coroutines:
 *  - cp->stack points to the bottom of the stack area (low address)
 *  - cp_>stack_base points to the top of the stack area (high address).
 *  - cp->stack_pointer stores the current stack pointer between transfers.
 *
 * We will preload the address of the coroutine function cr_function(cp, arg) in R12,
 * the coroutine pointer cp in R13, and the void *arg in R14. We will also
 * store the address of cmb_coroutine_exit in R15 before the first transfer into
 * the new coroutine, to be called with the return value from the coroutine
 * function as its argument if that function ever returns.
 */

/*
 * Stack sanity check, Linux SysV-specific, see
 *   https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
 */
bool cmi_coroutine_stack_valid(const struct cmi_coroutine *cp)
{
    cmb_assert_debug(cp != NULL);
    cmb_assert_debug(cp->stack_base != NULL);
    cmb_assert_debug(cp->stack_limit != NULL);
    const struct cmi_coroutine *cp_main = cmi_coroutine_main();
    if (cp == cp_main) {
        cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
        cmb_assert_debug(cp->stack == NULL);
        if (cp->stack_pointer != NULL) {
            cmb_assert_debug((uintptr_t *)cp->stack_pointer > (uintptr_t *)cp->stack_limit);
            cmb_assert_debug((uintptr_t *)cp->stack_pointer < (uintptr_t *)cp->stack_base);
            #ifndef NMXCSR
                /* Even number of slots pushed: Trampoline, MXCSR, RBP, RBX, R12, R13, R14, R15 */
                cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
            #else
                /* Odd number of slots pushed: MXCSR is gone. */
                cmb_assert_debug((((uintptr_t)cp->stack_pointer + 8u) % 16u) == 0u);
            #endif
        }
    }
    else {
        cmb_assert_debug(cp->stack != NULL);
        cmb_assert_debug(cp->stack_pointer != NULL);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer > (uintptr_t *)cp->stack_limit);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer < (uintptr_t *)cp->stack_base);
        #ifndef NMXCSR
            /* Even number of slots pushed: Trampoline, MXCSR, RBP, RBX, R12, R13, R14, R15 */
            cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
         #else
            /* Odd number of slots pushed: MXCSR is gone. */
            cmb_assert_debug((((uintptr_t)cp->stack_pointer + 8u) % 16u) == 0u);
        #endif
    }

    return true;
}

/* Register sanity check, Linux/x86-64-specific: Just check MXCSR */
bool cmi_coroutine_registers_valid(const struct cmi_coroutine *cp)
{
    cmb_unused(cp);

    #ifdef NMXCSR
        /* MXCSR register is not stored in this version of Cimba,
         * assuming that no coroutine changes it. Verify that this holds.    */
        static CMB_THREAD_LOCAL uint64_t mxcsr_cached = 0u;

        /* We only care about the status bits here */
        const uint64_t mxcsr_now = _mm_getcsr() & ~0x3Fu;
        if (mxcsr_cached != 0u) {
            const bool match = (mxcsr_now == mxcsr_cached);
            mxcsr_cached = mxcsr_now;

            return match;
        }
        else {
            mxcsr_cached = mxcsr_now;

            return true;
        }


    #else
        /* Nothing to do */
        return true;
    #endif
}

/* Initialize a new stack with necessary stack frame */
void cmi_coroutine_context_init(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp->stack != NULL);
    cmb_assert_debug(cp->stack_base != NULL);
    cmb_assert_debug(cp->stack_limit != NULL);

    /* Top end of stack, ensure 16-byte alignment */
    while (((uintptr_t)cp->stack_base % 16u) != 0u) {
        /* Counting down, the way the stack grows */
        cp->stack_base--;
    }

    /* This is our new, aligned stack base */
    unsigned char *stkptr = cp->stack_base;
    cmb_assert_debug(((uintptr_t)stkptr % 16) == 0);

    /* "Push" the "return" address */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_trampoline;

#if 0
    /* Clear the flag register, enable interrupts */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0202ull;
#endif

    #ifndef NMXCSR
        /* Default MXCSR value */
        stkptr -= 8u;
        *(uint32_t *)(stkptr + 4) = 0x1f80u;
        *(uint32_t *)stkptr = 0u;
    #endif

    /* Clear RBP to terminate gdb backtrace */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Clear RBX */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Place address of coroutine launch function in R12 */
    stkptr -= 8u;
    *(uintptr_t *)stkptr = (uintptr_t)cmi_coroutine_launch;

    /* Place address of coroutine struct in R13 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)cp;

    /* Place coroutine function context argument in R14 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->context);

    /* Place address of exit function in R15 */
    stkptr -= 8u;
    if (cp->cr_exit == NULL) {
        *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_exit;
    }
    else {
         *(uint64_t *)stkptr = (uintptr_t)(cp->cr_exit);
    }

    /* Store stack pointer RSP in the coroutine struct to resume from here */
    cp->stack_pointer = stkptr;

    /* That should be it, a valid stack frame ready to transfer into */
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));
}

/*
 * Allocate memory suitable for a stack, including one extra guard page.
 * The mprotect call is badly serializing for multithreaded applications,
 * hence managing a pool of recycled stacks of various sizes, assuming that
 * the application will only use a few different stack sizes. (Most likely, just
 * one size, all stacks the same size.)
 */
unsigned char *cmi_coroutine_stack_alloc(const size_t size_req, unsigned char **base_p, unsigned char **limit_p)
{
    cmb_assert_debug(size_req > 0u);
    cmb_assert_debug((base_p != NULL) && (limit_p != NULL));

    const size_t pagesz = cmi_pagesize();
    cmb_assert_debug(size_req <= SIZE_MAX - pagesz);
    const size_t size_rnd = (size_req + pagesz - 1u) & ~(pagesz - 1u);

    /* Do we have one lying around? */
    unsigned char *stack_raw = NULL;
    struct stack_tag *st = stack_list;
    while (st != NULL) {
        if (st->size == size_rnd) {
            if (st->head != NULL) {
                stack_raw = st->head;
                unsigned char **nextloc = (unsigned char **)(stack_raw + pagesz);
                st->head = *nextloc;
            }
            break;
        }
        st = st->next;
    }

    if (stack_raw == NULL) {
        /* None lying around, create one */
        stack_raw = cmi_aligned_alloc(pagesz, size_rnd + pagesz);
        cmb_assert_always(stack_raw != NULL);
        /* Protect the guard page */
        const int r = mprotect(stack_raw, pagesz, PROT_NONE);
        cmb_assert_always(r == 0);
    }

    /* Stack grows downwards, the stack base is at the top */
    *base_p = stack_raw + pagesz + size_rnd;
    /* The usable stack space ends here, growing beyond will trigger segfault */
    *limit_p = stack_raw + pagesz;
    cmb_assert_debug(((uintptr_t)*base_p - (uintptr_t)*limit_p) == size_rnd);

    return stack_raw;
}

/* Free memory previously allocated for a stack, pushing it back on pool */
void cmi_coroutine_stack_free(unsigned char *stack_raw, size_t size_req)
{
    cmb_assert_release(stack_raw != NULL);

    const size_t pagesz = cmi_pagesize();
    cmb_assert_debug(size_req <= SIZE_MAX - pagesz);
    const size_t size_rnd = (size_req + pagesz - 1u) & ~(pagesz - 1u);

    struct stack_tag *st = stack_list;
    while (st != NULL) {
        if (st->size == size_rnd) {
            /* Found its slot, push it */
            unsigned char **nextloc = (unsigned char **)(stack_raw + pagesz);
            *nextloc = st->head;
            st->head = stack_raw;
            return;
        }
        else {
            st = st->next;
        }
    }

    /* Made it here, so first stack this size to be recycled */
    st = cmi_malloc(sizeof(*st));
    *(unsigned char **)(stack_raw + pagesz) = NULL;
    st->size = size_rnd;
    st->head = stack_raw;
    st->next = stack_list;
    stack_list = st;
}

void cmi_coroutine_stack_cleanup(void)
{
    const size_t pagesz = cmi_pagesize();
    struct stack_tag *st = stack_list;
    while (st != NULL) {
        while (st->head != NULL) {
            unsigned char *raw = st->head;
            unsigned char *next = *(unsigned char **)(raw + pagesz);   /* before free */

            /* Unprotect guard page to avoid complaints */
            const int r = mprotect(raw, pagesz, PROT_READ | PROT_WRITE);
            cmb_assert_always(r == 0);
            cmi_aligned_free(raw);

            st->head = next;
        }

        struct stack_tag *next = st->next;
        cmi_free(st);
        st = next;
    }

    stack_list = NULL;
}

/*
 * Linux-specific code to get the top and bottom of the current (main) stack
 */
unsigned char *cmi_coroutine_stackbase(void)
{
    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    int r = pthread_getattr_np(pthread_self(), &attrs);
    cmb_assert_release(r == 0);

    void *stack_lowend;
    size_t stack_size;
    r = pthread_attr_getstack(&attrs, &stack_lowend, &stack_size);
    cmb_assert_release(r == 0);

    pthread_attr_destroy(&attrs);

    return (unsigned char *)stack_lowend + stack_size;
}

unsigned char *cmi_coroutine_stacklimit(void)
{
    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    int r = pthread_getattr_np(pthread_self(), &attrs);
    cmb_assert_release(r == 0);

    void *stack_lowend;
    size_t stack_size;
    r = pthread_attr_getstack(&attrs, &stack_lowend, &stack_size);
    cmb_assert_release(r == 0);

    pthread_attr_destroy(&attrs);

    return stack_lowend;
}

unsigned char *cmi_coroutine_stackraw(void)
{
    /* Not relevant for Linux */
    return NULL;
}

/*
 * cmi_coroutine_os_adopt_stack - Make the OS treat cp's stack as the current
 * one. On Windows this rewrites the TEB stack fields the context switch swaps,
 * needed after a longjmp bypasses the switch. Linux keeps no such per-thread
 * stack bounds that the kernel validates against, so this is a no-op here; the
 * longjmp already left RSP on a valid stack.
 */
void cmi_coroutine_os_adopt_stack(const struct cmi_coroutine *cp)
{
    cmb_unused(cp);
}
