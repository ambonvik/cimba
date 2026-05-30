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

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#include "cmb_assert.h"

#include "cmi_coroutine.h"
#include "cmi_memutils.h"

/* Assembly function, see src/arc/cmi_coroutine_context_*.asm */
extern void cmi_coroutine_trampoline(void);

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
                /* Total 9 slots pushed: Trampoline, Flags, MXCSR, RBP, RBX, R12, R13, R14, R15 */
                cmb_assert_debug((((uintptr_t)cp->stack_pointer + 8u) % 16u) == 0u);
            #else
               /* Total 8 slots pushed: MXCSR is gone. */
               cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
            #endif
        }
    }
    else {
        cmb_assert_debug(cp->stack != NULL);
        cmb_assert_debug(cp->stack_pointer != NULL);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer > (uintptr_t *)cp->stack_limit);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer < (uintptr_t *)cp->stack_base);
        #ifndef NMXCSR
            /* Total 9 slots pushed: Trampoline, Flags, MXCSR, RBP, RBX, R12, R13, R14, R15 */
            cmb_assert_debug((((uintptr_t)cp->stack_pointer + 8u) % 16u) == 0u);
        #else
            /* Total 8 slots pushed: MXCSR is gone. */
            cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
        #endif
    }

    return true;
}

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

    /* Clear the flag register, enable interrupts */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0202ull;

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

/* Allocate memory suitable for a stack, including one extra guard page */
unsigned char *cmi_coroutine_stack_alloc(const size_t size, unsigned char **base_p, unsigned char **limit_p)
{
    const size_t pagesz = cmi_pagesize();
    unsigned char *raw = cmi_aligned_alloc(pagesz, size + pagesz);
    cmb_assert_always(raw != NULL);

    const int r = mprotect(raw, pagesz, PROT_NONE);
    cmb_assert_always(r == 0);

    /* Stack grows downwards, base is at the top */
    *base_p = raw + size + pagesz;
    *limit_p = raw + pagesz;

    return raw;
}

/* Free memory previously allocated for a stack */
void cmi_coroutine_stack_free(unsigned char *stack)
{
    cmb_assert_release(stack != NULL);

    /* Unprotect guard page to avoid complaints */
    const size_t pagesz = cmi_pagesize();
    mprotect(stack, pagesz, PROT_READ | PROT_WRITE | PROT_EXEC);

    cmi_aligned_free(stack);
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

    void *stack_end;
    size_t stack_size;
    r = pthread_attr_getstack(&attrs, &stack_end, &stack_size);
    cmb_assert_release(r == 0);

    pthread_attr_destroy(&attrs);

    return (unsigned char *)stack_end + stack_size;
}

unsigned char *cmi_coroutine_stacklimit(void)
{
    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    int r = pthread_getattr_np(pthread_self(), &attrs);
    cmb_assert_debug(r == 0);

    void *stack_end;
    size_t stack_size;
    r = pthread_attr_getstack(&attrs, &stack_end, &stack_size);
    cmb_assert_debug(r == 0);

    pthread_attr_destroy(&attrs);

    return stack_end;
}

unsigned char *cmi_coroutine_stackraw(void)
{
    /* Not relevant for Linux */
    return NULL;
}
