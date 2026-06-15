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

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <windows.h>
#include <xmmintrin.h>

#include "cmb_assert.h"

#include "cmi_coroutine.h"
#include "cmi_memutils.h"

/* Assembly functions, see src/port/x86-64/windows/cmi_coroutine_context_*.asm */
extern void cmi_coroutine_trampoline(void);
extern void cmi_coroutine_set_stack_teb(void *base, void *limit, void *dealloc);

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
 * cm_coroutine_exit to catch its exit value if the coroutine function ever
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

/* Stack sanity check, Win64-specific */
bool cmi_coroutine_stack_valid(const struct cmi_coroutine *cp)
{
    cmb_assert_debug(cp != NULL);

    /* We do not worry about the main stack here., only our own. */
    const struct cmi_coroutine *cp_main = cmi_coroutine_main();
    if (cp != cp_main) {
        cmb_assert_debug(cp->stack != NULL);
        cmb_assert_debug(cp->stack_base != NULL);
        cmb_assert_debug(cp->stack_limit != NULL);
        cmb_assert_debug(cp->stack_pointer != NULL);

        cmb_assert_debug((uintptr_t)cp->stack_limit >= (uintptr_t)cp->stack);
        cmb_assert_debug((uintptr_t)cp->stack_pointer > (uintptr_t)cp->stack_limit);
        cmb_assert_debug((uintptr_t)cp->stack_pointer < (uintptr_t)cp->stack_base);

        cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 8u);
    }

    return true;
}

/* Register sanity check, Win64/x86-64-specific: Just check MXCSR */
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

void cmi_coroutine_context_init(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp->stack != NULL);
    cmb_assert_debug(cp->stack_base != NULL);

    /* Inform Windows that stack shenanigans are OK in this thread.
     * 0x1e00 means "not a fiber"  */
    if (GetCurrentFiber() == (void*)0x1e00) {
        ConvertThreadToFiber(NULL);
    }

    /* The new stack for this coroutine starts here */
    unsigned char *stkptr = cp->stack_base;
    cmb_assert_debug(((uintptr_t)stkptr % 16) == 0);

    /* Due to Win64 calling convention, leave 4x8 bytes for storing arguments */
    stkptr -= 32u;

    /* "Push" the "return" address */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)cmi_coroutine_trampoline;

    #ifndef NMXCSR
        /* Set the XMM status register MXCSR, default value (masked fp exceptions) */
        stkptr -= 8u;
        *(uint32_t *)(stkptr + 4) = 0x1f80u;
        *(uint32_t *)stkptr = 0u;
    #endif

    /* Clear RBX */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Set RBP to 0 to cleanly terminate stack backtraces in debuggers */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Clear RDI */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0ull;

    /* Clear RSI */
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

    #ifndef NMXCSR
        /* Add space for 10 XMM registers * 16 bytes */
        stkptr = (unsigned char *)((uintptr_t)stkptr - 160);
        (void)cmi_memset(stkptr, 0, 160);
    #else
        /* Also add 8 extra bytes for alignment */
        stkptr = (unsigned char *)((uintptr_t)stkptr - 168);
        (void)cmi_memset(stkptr, 0, 168);
    #endif

    /* "Push" the stack deallocation ptr, stack limit, stack base (to TIB via GS register) */
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack);
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_limit);
    stkptr -= 8u;
    *(uint64_t *)stkptr = (uintptr_t)(cp->stack_base);

    /* Store stack pointer RSP in the coroutine struct to resume from here */
    cp->stack_pointer = stkptr;

    /* That should be it, a valid stack frame ready to transfer into */
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));
}

/* Allocate memory suitable for a stack */
unsigned char *cmi_coroutine_stack_alloc(const size_t size,
                                         unsigned char **base_p,
                                         unsigned char **limit_p)
{
    const size_t pagesz = cmi_pagesize();
    void *raw = VirtualAlloc(NULL, size + pagesz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    cmb_assert_always(raw != NULL);

    DWORD old_protect;
    const int ok = VirtualProtect(raw, pagesz, PAGE_READWRITE | PAGE_NOACCESS, &old_protect);
    cmb_assert_always(ok != 0);

    /* The stack grows downwards; the base is at the top */
    *base_p = raw + size + pagesz;

    /* The bottom includes the guard page */
    *limit_p = raw + pagesz;

    return raw;
}

/* Free memory previously allocated for a stack */
void cmi_coroutine_stack_free(unsigned char *stack)
{
    cmb_assert_release(stack != NULL);

    int r = VirtualFree(stack, 0, MEM_RELEASE);
    cmb_assert_always(r != 0);
}

/*
 * cmi_coroutine_os_adopt_stack - Reinstall cp's stack as the thread's current
 * stack in the Windows TEB. The context switch (cmi_coroutine_context_switch)
 * swaps StackBase, StackLimit, and DeallocationStack on every transfer, but a
 * longjmp out of a running coroutine returns to the main stack without going
 * through the switch, leaving the TEB describing the abandoned coroutine. The
 * kernel then rejects the main stack pointer with STATUS_BAD_STACK (0xC0000028)
 * the next time it validates the stack (a __chkstk probe, SEH unwind, or a
 * checked return). cmi_coroutine_reset_to_main calls this with the main
 * coroutine to put the TEB back in agreement with the stack we are really on.
 */
void cmi_coroutine_os_adopt_stack(const struct cmi_coroutine *cp)
{
    cmb_assert_debug(cp != NULL);
    cmb_assert_debug(cp->stack_base != NULL);
    cmb_assert_debug(cp->stack_limit != NULL);

    cmi_coroutine_set_stack_teb(cp->stack_base, cp->stack_limit, cp->stack);
}
