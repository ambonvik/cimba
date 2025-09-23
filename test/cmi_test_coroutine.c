/*
 * Test script for coroutines.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
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
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cmi_coroutine.h"
#include "cmi_memutils.h"
#include "cmi_test.h"

/* Assembly functions, see arch/cmi_coroutine_context_*.asm */
extern void *cmi_coroutine_get_stackbase(void);
extern void *cmi_coroutine_get_stacklimit(void);

static void test_asm_calls() {
    printf("Testing assembly get functions\n");
    void *sbp = cmi_coroutine_get_stackbase();
    printf("Current stack base: %p\n", sbp);
    void *slp = cmi_coroutine_get_stacklimit();
    printf("Current stack limit: %p\n", slp);
    printf("Stack size: %llu\n", (uintptr_t)sbp - (uintptr_t)slp);
    cmi_test_print_line("=");
}

/* Simple test function, just an event that returns */
static void *corofunc_1(struct cmi_coroutine *myself, void *arg) {
    printf("corofunc_1(%p, %p) running\n", (void *)myself, arg);
    void *sbp = cmi_coroutine_get_stackbase();
    printf("\tCurrent stack base: %p\n", sbp);
    void *slp = cmi_coroutine_get_stacklimit();
    printf("\tCurrent stack limit: %p\n", slp);
    printf("\tStack size: %llu\n", (uintptr_t)sbp - (uintptr_t)slp);
    printf("corofunc_1 returning %p\n", arg);

    /* Return is caught and redirected to cmi_coroutine_exit(ret) */
    return arg;
}

static void test_simple_event(void) {
    /* This function may look simple, but it exercises a lot of stuff.
     * First create a coroutine, which is straightforward memory allocation.
     */
    printf("Test simple coroutine call\n");
    const size_t stksz = 24 * 1024;
    printf("Create a coroutine, stack size %llu\n", stksz);
    struct cmi_coroutine *cp = cmi_coroutine_create(stksz);
    printf("Got %p, now start it\n", (void *)cp);

    /* cmi_coroutine_start(cp) transfers control into the coroutine stack,
     * saving the registers and stack pointer of the main continuation,
     * loading the prepared register values for the new coroutine,
     * starts executing the coroutine function, and, since it
     * does not yield or resume, continues until the end where
     * the return is caught by the trampoline and control is
     * transferred back to its parent, i.e., here. It tests almost
     * everything in the coroutine class right here.
     */
    cmi_coroutine_start(cp, corofunc_1, (void *)0x5EAF00Dull);
    printf("Survived, now back in main coroutine\n");

    /* Destroy the coroutine to free its memory allocation*/
    printf("Delete coroutine %p\n", (void *)cp);
    cmi_coroutine_destroy(cp);

    cmi_test_print_line("=");
}

/* A coroutine that transfers control to a partner coroutine and back */
static void *corofunc_3(struct cmi_coroutine *myself, void *arg) {
    /* The arg is not used here, using the caller pointer instead */
    struct cmi_coroutine *boss = myself->caller;
    printf("corofunc_3(%p, %p), boss %p\n", (void *)myself, arg, (void *)boss);

    for (unsigned ui = 0; ui < 5; ui++) {
        printf("corofunc_3: Iteration %u\n", ui);
        /* Wrap the index number in a fortune cookie and pass it back */
        uint64_t *cookie = cmi_malloc(sizeof(*cookie));
        *cookie = ui;
        printf("corofunc_3: yields cookie %llu back to boss\n", *cookie);
        uint64_t *ticket = cmi_coroutine_yield(cookie);
        printf("corofunc_3: received ticket %llu in return\n", *ticket);
        /* Toss it and try again */
        cmi_free(ticket);
    }

    printf("corofunc_3 done, exit value NULL\n");
    /* Will transfer control back to parent */
    cmi_coroutine_exit(NULL);

    /* Never gets here */
    return (void *)0xBADF00Dull;
}

/* A coroutine that transfers control to a partner coroutine and back */
static void *corofunc_2(struct cmi_coroutine *myself, void *arg) {
    /* The arg is a disguised pointer to the other coroutine */
    struct cmi_coroutine *buddy = (struct cmi_coroutine *)arg;
    printf("corofunc_2(%p, %p) running\n", (void *)myself, (void *)buddy);

    /* We are evidently running, start the buddy as well. */
    void *ret = cmi_coroutine_start(buddy, corofunc_3, myself);
    printf("corofunc_2: Back, now trade tickets for cookies\n");

    int cntr = 100;
    while (ret != NULL) {
        uint64_t *cookie = ret;
        printf("corofunc_2: Got cookie %llu\n", *cookie);
        /* Inedible, toss it */
        cmi_free(cookie);
        uint64_t *ticket = cmi_malloc(sizeof(*ticket));
        *ticket = cntr++;
        printf("corofunc_2: Passes ticket %llu\n", *ticket);
        ret = cmi_coroutine_resume(buddy, ticket);
    }

    /* Return is caught and redirected to cmi_coroutine_exit(ret) */
    return (void *)0x5EAF00Dull;
}

static void test_asymmetric(void) {
    printf("Test asymmetric coroutines\n");
    const size_t stksz = 16 * 1024;
    printf("Create two coroutines, stack size %llu\n", stksz);
    struct cmi_coroutine *cp1 = cmi_coroutine_create(stksz);
    struct cmi_coroutine *cp2 = cmi_coroutine_create(stksz);
    printf("Start %p\n", (void *)cp1);
    cmi_coroutine_start(cp1, corofunc_2, (void *)cp2);
    printf("Survived, now back in main coroutine\n");

    /* Destroy the coroutine to free its memory allocation*/
    printf("Delete coroutine %p\n", (void *)cp1);
    cmi_coroutine_destroy(cp1);
    printf("Delete coroutine %p\n", (void *)cp2);
    cmi_coroutine_destroy(cp2);

    cmi_test_print_line("=");
}



int main(void) {
    cmi_test_print_line("*");
    printf("**********************         Testing coroutines         **********************\n");
    cmi_test_print_line("*");

    test_asm_calls();
    test_simple_event();
    test_asymmetric();

    return 0;
}