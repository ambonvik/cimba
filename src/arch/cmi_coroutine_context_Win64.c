/*
* cmi_coroutine_context_Win64.c - Windows specific coroutine initialization
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

/* Assembly function, see src/arc/cmi_coroutine_context_*.asm */
extern void cmi_coroutine_trampoline(void);

/*
 * Windows-specific code to allocate and initializa stack for a new coroutine.
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
 *
 * See also:
 *  https://en.wikipedia.org/wiki/X86_calling_conventions#Microsoft_x64_calling_convention
 *  https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention
 *  https://learn.microsoft.com/en-us/cpp/build/stack-usage
 *  https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog
 *
 * Here, we set up a context with the launcher/trampoline function as the
 * "return" address and register values that prepare for launching the
 * coroutine function foo(coro, arg) on first transfer, and for calling
 * cmi_coroutine_exit to catch its exit value if the coroutine function ever
 * returns.
 *
 * In our coroutines:
 *  - cp->stack points to the bottom of the stack area (low address)
 *  - cp_>stack_base points to the top of the stack area (high address).
 *  - cp->stack_pointer stores the current stack pointer between transfers.
 *
 * We will pre-load the address of the coroutine function foo(cp, arg) in R12,
 * the coroutine pointer cp in R13, and the void *arg in R14. We will also
 * store the address of cmi_coroutine_exit in R15 before the first transfer into
 * the new coroutine, to be called with the return value from the coroutine
 * function as its argument if that function ever returns.
 */

/* Stack sanity check, Win64-specific */
bool cmi_coroutine_stack_valid(struct cmi_coroutine *cp) {
    cmb_assert_debug(cp != NULL);
    cmb_assert_debug(cp->stack_base != NULL);
    cmb_assert_debug(cp->stack_limit != NULL);
    if (cp->stack != NULL) {
        cmb_assert_debug(cp->stack_pointer != NULL);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer > (uintptr_t *)cp->stack_limit);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer < (uintptr_t *)cp->stack_base);
        cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
    }
    else if (cp->stack_pointer != NULL) {
        /* Main coroutine with a stack pointer recorded already */
        cmb_assert_debug((uintptr_t *)cp->stack_pointer > (uintptr_t *)cp->stack_limit);
        cmb_assert_debug((uintptr_t *)cp->stack_pointer < (uintptr_t *)cp->stack_base);
        cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
    }

    return true;
}

void cmi_coroutine_context_init(struct cmi_coroutine *cp,
                                cmi_coroutine_func *foo,
                                void *arg) {
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp->stack != NULL),
    cmb_assert_debug(cp->stack_base != NULL);

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
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack);

    /* Clear the flags register */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Clear the XMM status register */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x1f80ull;

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

    /* Address of coroutine function in R12 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)foo;

    /* Address of coroutine struct in R13 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)cp;

    /* Coroutine function argument in R14 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)arg;

    /* Address of exit function in R15 */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_exit;

    /* 10 registers * 16 bytes + 8 bytes for 16-byte alignment */
    stkptr = (unsigned char *)((uintptr_t)stkptr - 168);
    (void)cmi_memset(stkptr, 0, 168);

    /* Store the stack pointer RSP */
    cp->stack_pointer = stkptr;
    cmb_assert_debug(((uintptr_t)(cp->stack_pointer) % 16u) == 0u);

    /* Make sure we can recognize if something overwrites the end of stack */
    cp->stack_limit = cp->stack;
    while (((uintptr_t)cp->stack_limit % 16u) != 0u) {
        /* Counting up */
        cp->stack_limit++;
    }

    *(uint64_t *)cp->stack_limit = CMI_STACK_LIMIT_UNTOUCHED;
    cmb_assert_debug(*((uint64_t *)cp->stack_limit) == CMI_STACK_LIMIT_UNTOUCHED);
}