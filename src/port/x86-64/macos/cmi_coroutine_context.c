/*
 * Coroutine stack initialization for x86-64 macOS.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>

#include "cmb_assert.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"
#include "cmi_sanitizer.h"

extern void cmi_coroutine_trampoline(void);
extern void *cmi_coroutine_launch(struct cmi_coroutine *cp, void *arg);

bool cmi_coroutine_stack_valid(const struct cmi_coroutine *cp)
{
    cmb_assert_debug(cp != NULL);
    cmb_assert_debug(cp->stack_base != NULL);
    cmb_assert_debug(cp->stack_limit != NULL);

    const struct cmi_coroutine *cp_main = cmi_coroutine_main();
    if (cp == cp_main) {
        cmb_assert_debug(cp->status == CMI_COROUTINE_RUNNING);
        cmb_assert_debug(cp->stack == NULL);
    }
    else {
        cmb_assert_debug(cp->stack != NULL);
        cmb_assert_debug(cp->stack_pointer != NULL);
    }

    if (cp->stack_pointer != NULL) {
        cmb_assert_debug(
            (uintptr_t)cp->stack_pointer > (uintptr_t)cp->stack_limit
        );
        cmb_assert_debug(
            (uintptr_t)cp->stack_pointer < (uintptr_t)cp->stack_base
        );
        #ifndef NMXCSR
            cmb_assert_debug(
                (((uintptr_t)cp->stack_pointer + 8u) % 16u) == 0u
            );
        #else
            cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
        #endif
    }
    return true;
}

void cmi_coroutine_context_init(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp->stack != NULL);

    uintptr_t aligned_base = (uintptr_t)cp->stack_base & ~(uintptr_t)0x0fu;
    unsigned char *stkptr = (unsigned char *)aligned_base;

    stkptr -= 8u;
    *(uintptr_t *)stkptr = (uintptr_t)cmi_coroutine_trampoline;
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0x0202ull;

    #ifndef NMXCSR
        stkptr -= 8u;
        *(uint32_t *)(stkptr + 4u) = 0x1f80u;
        *(uint32_t *)stkptr = 0u;
    #endif

    stkptr -= 8u;
    *(uint64_t *)stkptr = 0u; /* RBP */
    stkptr -= 8u;
    *(uint64_t *)stkptr = 0u; /* RBX */
    stkptr -= 8u;
    *(uintptr_t *)stkptr = (uintptr_t)cmi_coroutine_launch; /* R12 */
    stkptr -= 8u;
    *(uintptr_t *)stkptr = (uintptr_t)cp; /* R13 */
    stkptr -= 8u;
    *(uintptr_t *)stkptr = (uintptr_t)cp->context; /* R14 */
    stkptr -= 8u;
    *(uintptr_t *)stkptr = (uintptr_t)(
        cp->cr_exit == NULL ? cmi_coroutine_exit : cp->cr_exit
    ); /* R15 */

    cp->stack_pointer = stkptr;
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));
}

unsigned char *cmi_coroutine_stack_alloc(const size_t size,
                                         unsigned char **base_p,
                                         unsigned char **limit_p)
{
    const size_t pagesz = cmi_pagesize();
    unsigned char *raw = cmi_aligned_alloc(pagesz, size + pagesz);
    cmb_assert_always(raw != NULL);
    cmb_assert_always(mprotect(raw, pagesz, PROT_NONE) == 0);

    *base_p = raw + size + pagesz;
    *limit_p = raw + pagesz;
    return raw;
}

void cmi_coroutine_stack_free(unsigned char *stack, size_t size)
{
    cmb_assert_release(stack != NULL);
    const size_t pagesz = cmi_pagesize();
    cmi_asan_unpoison(stack + pagesz, size);
    cmb_assert_always(mprotect(stack, pagesz, PROT_READ | PROT_WRITE) == 0);
    cmi_aligned_free(stack);
}

unsigned char *cmi_coroutine_stackbase(void)
{
    return pthread_get_stackaddr_np(pthread_self());
}

unsigned char *cmi_coroutine_stacklimit(void)
{
    unsigned char *base = pthread_get_stackaddr_np(pthread_self());
    return base - pthread_get_stacksize_np(pthread_self());
}

unsigned char *cmi_coroutine_stackraw(void)
{
    return NULL;
}

/* macOS frees stacks immediately, so there is no recycled stack pool. */
void cmi_coroutine_stack_cleanup(void) {}

/* Darwin does not require updating OS stack bounds after longjmp. */
void cmi_coroutine_os_adopt_stack(const struct cmi_coroutine *cp)
{
    cmb_unused(cp);
}

/* Both Darwin ports preserve floating-point control state on every switch. */
bool cmi_coroutine_registers_valid(const struct cmi_coroutine *cp)
{
    cmb_unused(cp);
    return true;
}
