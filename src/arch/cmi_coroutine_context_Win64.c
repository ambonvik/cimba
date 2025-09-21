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

#include <stdint.h>

#include "cmb_assert.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"

/* Assembly, see src/arc/cmi_coroutine_context_*.asm */
extern void cmi_coroutine_trampoline(void);

/*
 * Windows-specific code to allocate and initializa stack for a new coroutine.
 *
 * Populates the new stack with register values to be loaded when the
 * new coroutine gets activated for the first time. The context switch into
 * it happens in assembly, function cmi_coroutine_context_switch, see
 * src/arch/cmi_coroutine_context_*.asm
 *
 * Here, we set up a context with the launcher/trampoline function as the
 * "return" address and register values that prepare for launching the
 * coroutine function foo(coro, arg) on first transfer, and for calling
 * cmi_coroutine_exit to catch its exit value if the coroutine function ever
 * returns.
 *
 * Storing foo, cp, arg in R12, R13, R14, cmi_coroutine_exit in R15
 */
void cmi_coroutine_context_init(struct cmi_coroutine *cp,
                                cmi_coroutine_func *foo,
                                void *arg,
                                const size_t stack_size) {
    cmb_assert_release(cp != NULL);

    cp->stack = cmi_malloc(stack_size);

    /* Bottom end of stack, should never be reached */
    cp->stack_limit = cp->stack;

    /* Get the bottom of the stack aligned to 16 byte boundary */
    while (((uintptr_t)cp->stack_limit % 16) != 0u) {
        /* Counting up */
        cp->stack_limit++;
    }

    /* Make sure we can recognize if something overwrites end of stack */
    uint64_t *stack_trap = (uint64_t *)cp->stack_limit;
    *stack_trap = CMI_STACK_LIMIT_UNTOUCHED;
    cmb_assert_debug((*((uint64_t *)cp->stack_limit)) == CMI_STACK_LIMIT_UNTOUCHED);

    /* Top end of stack, get 16-byte alignment */
    cp->stack_base = cp->stack + stack_size;
    while (((uintptr_t)cp->stack_base % 16) != 0u) {
        /* Counting down, the way the stack grows */
        cp->stack_base--;
    }

    /* "Push" the "return" address */
    unsigned char *stkptr = cp->stack_base;
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_trampoline;

    /* "Push" the stack base and stack limit */
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_base);
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_limit);

    for (unsigned int ui = 0; ui < 6u; ui++) {
        /* Clear the flags register, the XMM status register, and the first four
        *  general purpose registers RBX, RBP, RDI, and RSI */
        stkptr -= 8;
        *(uint64_t *)stkptr = 0x0ull;
    }

    /* Address of coroutine function in R12 */
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)foo;
    /* Address of coroutine struct in R13 */
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)cp;
    /* Coroutine function argument in R14 */
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)arg;
    /* Address of exit function in R15 */
    stkptr -= 8;
    *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_exit;

    /* Alignment padding */
    stkptr -= 8;
    *(uint64_t *)stkptr = 0x0ull;

    for (unsigned int ui = 0; ui < 10u; ui++) {
        /* Zero the XMM registers */
        stkptr -= 8;
        *(uint64_t *)stkptr = 0x0ull;
        stkptr -= 8;
        *(double *)stkptr = 0.0;
    }

}