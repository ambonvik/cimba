/*
 * cmi_coroutine_context.c - Windows specific coroutine initialization
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
#include "cmi_memutils.h"

/* Assembly functions, see src/port/x86-64/windows/cmi_coroutine_context_*.asm */
extern void cmi_coroutine_trampoline(void);
extern void *cmi_coroutine_stackbase(void);
extern void *cmi_coroutine_stacklimit(void);

/*
 * Windows-specific code to allocate and initialize stack for a new coroutine.
 *
 * Populates the new stack with register values to be loaded when the
 * new coroutine gets activated for the first time. The context switch into
 * it happens in assembly, function cmi_coroutine_context_switch, see
 * src/arch/cmi_coroutine_context_*.asm
 *
 * Overall structure of the Win64 stack:
 *  - Grows downwards, from high address.
 *  - The top must be 16-byte aligned.
 *  - Before calling a function, "shadow space" is allocated for at least 4
 *    arguments, R9, R8, RDX, and RCX (in that order, from the top downwards),
 *    more if the called function has more than four arguments (we don't).
 *  - The return instruction pointer (RIP) follows next, before the function's
 *    own stack frame for storing registers and local variables.
 *  - When first entering a function, the stack is 8 bytes off the 16-byte
 *    alignment, since the return instruction pointer is pushed to a previously
 *    aligned stack.
 *  - Before pushing 128-bit XMM registers to the stack, it needs to be 16-byte
 *    aligned again.
 *
 * See also:
 *  https://en.wikipedia.org/wiki/X86_calling_conventions#Microsoft_x64_calling_convention
 *  https://probablydance.com/2013/02/20/handmade-coroutines-for-windows/
 *  https://github.com/HirbodBehnam/UserContextSwitcher (a good Linux example)
 *  https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention
 *  https://learn.microsoft.com/en-us/cpp/build/stack-usage
 *  https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog
 *
 * Here, we set up a context with the launcher/trampoline function as the
 * "return" address and register values that prepare for launching the
 * coroutine function cr_foo(coro, arg) on first transfer, and for calling
 * cmb_coroutine_exit to catch its exit value if the coroutine function ever
 * returns.
 *
 * In our coroutines:
 *  - cp->stack points to the bottom of the stack area (low address)
 *  - cp_>stack_base points to the top of the stack area (high address).
 *  - cp->stack_pointer stores the current stack pointer between transfers.
 *
 * We will preload the address of the coroutine function cr_foo(cp, arg) in R12,
 * the coroutine pointer cp in R13, and the void *arg in R14. We will also
 * store the address of cmb_coroutine_exit in R15 before the first transfer into
 * the new coroutine, to be called with the return value from the coroutine
 * function as its argument if that function ever returns.
 */

/* Bit pattern for last 64 bits of valid stack. */
#define CMI_STACK_LIMIT_UNTOUCHED 0xFA151F1AB1Eull

#ifndef NASSERT
 /* Stack sanity check, Win64-specific */
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
            cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
        }
    }
    else {
        cmb_assert_debug(cp->stack != NULL);
        cmb_assert_debug(cp->stack_pointer != NULL);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer > (uintptr_t *)cp->stack_limit);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer < (uintptr_t *)cp->stack_base);
        cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
        cmb_assert_debug(*((uint64_t *)cp->stack_limit) == CMI_STACK_LIMIT_UNTOUCHED);
    }

    return true;
 }

#endif /* NASSERT */

void cmi_coroutine_context_init(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp->stack != NULL),
    cmb_assert_debug(cp->stack_base != NULL);

    /* Make sure we can recognize if something overwrites the end of stack */
    cp->stack_limit = cp->stack;
    while (((uintptr_t)cp->stack_limit % 16u) != 0u) {
        /* Counting up */
        cp->stack_limit++;
    }

    *(uint64_t *)cp->stack_limit = CMI_STACK_LIMIT_UNTOUCHED;

    /* Top end of stack, ensure 16-byte alignment */
    while (((uintptr_t)cp->stack_base % 16u) != 0u) {
        /* Counting down, the way the stack grows */
        cp->stack_base--;
    }

    /* This is our new, aligned stack base */
    unsigned char *stkptr = cp->stack_base;
    cmb_assert_debug(((uintptr_t)stkptr % 16) == 0);

    /* Due to Win64 calling convention, leave 4x8 bytes for storing arguments */
    stkptr -= 32u;

    /* "Push" the "return" address */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_trampoline;

    /* "Push" the stack base and stack limit (to TIB via GS register) */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_base);
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_limit);

    /* Clear the flags register */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Set the XMM status register MXCSR, exception on invalid and div zero */
    stkptr -= 8u;
    // *(uint64_t *)(stkptr + 4) = 0x1f80u;
    *(uint64_t *)(stkptr + 4) = 0x1d00u;
    *(uint32_t *)stkptr = 0u;

    /* Clear RBX */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Point RBP to start of stack frame */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_base - 40u);

    /* Clear RDI */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Clear RSI */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Place address of coroutine function in R12 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->cr_foo);

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

    /* Add space for 10 XMM registers * 16 bytes + 8 bytes for alignment */
    stkptr = (unsigned char *)((uintptr_t)stkptr - 168);
    (void)cmi_memset(stkptr, 0, 168);

    /* Store stack pointer RSP in the coroutine struct to resume from here */
    cp->stack_pointer = stkptr;

    /* That should be it, a valid stack frame ready to transfer into */
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));
}

/*
 * Windows specific code to get top and bottom of current (main) stack,
 * picking the addresses out of the Thread Information Block in assembly.
 */
void cmi_coroutine_stacklimits(unsigned char **top, unsigned char **bottom)
{
     cmb_assert_debug(top != NULL);
     cmb_assert_debug(bottom != NULL);

     *top = cmi_coroutine_stackbase();
     *bottom = cmi_coroutine_stacklimit();

     /* Stack grows downward in address space */
     cmb_assert_debug(*top > *bottom);
}

