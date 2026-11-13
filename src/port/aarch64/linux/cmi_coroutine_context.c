/*
 * Coroutine stack initialization for arm64 Linux.
 *
 * The initial frame matches cmi_coroutine_context.S (shared with the arm64
 * macOS port), while stack allocation and main stack discovery follow the
 * x86-64 Linux port: a per-thread pool of recycled guarded stacks, and
 * pthread_getattr_np for the main stack bounds.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

/* Make sure we get pthread_getattr_np */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier)
#include <pthread.h>

#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>

#include "cmb_assert.h"
#include "cmi_coroutine.h"
#include "cmi_memutils.h"
#include "cmi_sanitizer.h"

#define CMI_ARM64_CONTEXT_SIZE 176u

extern void cmi_coroutine_trampoline(void);
extern void *cmi_coroutine_launch(struct cmi_coroutine *cp, void *arg);

/*
 * Intrusive singly linked list of recycled stacks, as in the x86-64 Linux
 * port: mprotect is badly serializing for multithreaded applications, so
 * stacks are recycled within the current thread and freed at thread exit.
 */
struct stack_tag {
    size_t size;                /* Size of stacks in this list */
    unsigned char *head;        /* The intrusive list */
    struct stack_tag *next;     /* Next list, a different size stack */
};

CMB_THREAD_LOCAL static struct stack_tag *stack_list = NULL;

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
        cmb_assert_debug(((uintptr_t)cp->stack_pointer % 16u) == 0u);
    }
    return true;
}

void cmi_coroutine_context_init(struct cmi_coroutine *cp)
{
    cmb_assert_release(cp != NULL);
    cmb_assert_debug(cp->stack != NULL);

    uintptr_t aligned_base = (uintptr_t)cp->stack_base & ~(uintptr_t)0x0fu;
    unsigned char *stkptr = (
        (unsigned char *)aligned_base - CMI_ARM64_CONTEXT_SIZE
    );
    (void)cmi_memset(stkptr, 0, CMI_ARM64_CONTEXT_SIZE);

    uintptr_t *registers = (uintptr_t *)stkptr;
    registers[0] = (uintptr_t)cmi_coroutine_launch; /* X19 */
    registers[1] = (uintptr_t)cp; /* X20 */
    registers[2] = (uintptr_t)cp->context; /* X21 */
    registers[3] = (uintptr_t)(
        cp->cr_exit == NULL ? cmi_coroutine_exit : cp->cr_exit
    ); /* X22 */
    registers[11] = (uintptr_t)cmi_coroutine_trampoline; /* X30 */

    /* Inherit the parent's FPCR (rounding mode etc.), start with clear FPSR */
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    registers[20] = (uintptr_t)fpcr;

    cp->stack_pointer = stkptr;
    cmb_assert_debug(cmi_coroutine_stack_valid(cp));
}

unsigned char *cmi_coroutine_stack_alloc(const size_t size_req,
                                         unsigned char **base_p,
                                         unsigned char **limit_p)
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
                cmi_asan_unpoison(stack_raw + pagesz, size_rnd);
                unsigned char **nextloc = (unsigned char **)(stack_raw + pagesz);
                st->head = *nextloc;
            }
            break;
        }
        st = st->next;
    }

    if (stack_raw == NULL) {
        stack_raw = cmi_aligned_alloc(pagesz, size_rnd + pagesz);
        cmb_assert_always(stack_raw != NULL);
        const int r = mprotect(stack_raw, pagesz, PROT_NONE);
        cmb_assert_always(r == 0);
    }

    *base_p = stack_raw + pagesz + size_rnd;
    *limit_p = stack_raw + pagesz;
    cmb_assert_debug(!cmi_asan_region_poisoned(stack_raw + pagesz, size_rnd));

    return stack_raw;
}

/* Push a stack back on the pool rather than freeing it */
void cmi_coroutine_stack_free(unsigned char *stack_raw, size_t size_req)
{
    cmb_assert_release(stack_raw != NULL);

    const size_t pagesz = cmi_pagesize();
    cmb_assert_debug(size_req <= SIZE_MAX - pagesz);
    const size_t size_rnd = (size_req + pagesz - 1u) & ~(pagesz - 1u);
    cmi_asan_unpoison(stack_raw + pagesz, size_rnd);

    struct stack_tag *st = stack_list;
    while (st != NULL) {
        if (st->size == size_rnd) {
            unsigned char **nextloc = (unsigned char **)(stack_raw + pagesz);
            *nextloc = st->head;
            st->head = stack_raw;
            return;
        }
        st = st->next;
    }

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
            unsigned char *next = *(unsigned char **)(raw + pagesz);
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

static void main_stack_bounds(unsigned char **low_p, size_t *size_p)
{
    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    int r = pthread_getattr_np(pthread_self(), &attrs);
    cmb_assert_release(r == 0);

    void *low;
    r = pthread_attr_getstack(&attrs, &low, size_p);
    cmb_assert_release(r == 0);
    pthread_attr_destroy(&attrs);

    *low_p = low;
}

unsigned char *cmi_coroutine_stackbase(void)
{
    unsigned char *low;
    size_t size;
    main_stack_bounds(&low, &size);
    return low + size;
}

unsigned char *cmi_coroutine_stacklimit(void)
{
    unsigned char *low;
    size_t size;
    main_stack_bounds(&low, &size);
    return low;
}

unsigned char *cmi_coroutine_stackraw(void)
{
    return NULL;
}

/* Linux does not require updating OS stack bounds after longjmp. */
void cmi_coroutine_os_adopt_stack(const struct cmi_coroutine *cp)
{
    cmb_unused(cp);
}

/* The context switch preserves FPCR and FPSR on every switch. */
bool cmi_coroutine_registers_valid(const struct cmi_coroutine *cp)
{
    cmb_unused(cp);
    return true;
}
