/*
 * Test script for coroutines.
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
#include <stdio.h>
#include <stdint.h>

#include "cmi_coroutine.h"
#include "cmi_memutils.h"
#include "test.h"

/* Simple test function, just a single event that returns */
static void *corofunc(struct cmi_coroutine *myself, void *context)
{
    printf("corofunc(%p, %p) running\n", (void *)myself, context);
    printf("corofunc returning %p\n", context);
    return context;
}

static void test_simple_event(void)
{
    /* First create a coroutine, which is straightforward memory allocation. */
    printf("Test simple coroutine call\n");
    const size_t stksz = 24 * 1024;
    printf("Create a coroutine\n");
    struct cmi_coroutine *cp = cmi_coroutine_create();
    printf("Got %p, initialize it, stack size %llu\n", (void *)cp, stksz);
    cmi_coroutine_initialize(cp, corofunc, (void *)0x5EAF00D, NULL, stksz);

    /* The next call may look simple, but it exercises a lot of stuff.
     * cmi_coroutine_start() transfers control into the new coroutine,
     * saving the registers and stack pointer of the main continuation,
     * loading the prepared register values for the new coroutine,
     * starts executing the coroutine function, and, since this one
     * does not yield or resume, continues until the end where
     * the return is caught by the trampoline and control is
     * transferred back to its parent, i.e., here. It tests almost
     * everything in the coroutine class in just this call.
     */
    void *ret = cmi_coroutine_start(cp, NULL);

    printf("Survived, now back in main coroutine, received %p\n", ret);

    /* Destroy the coroutine to free its memory allocation*/
    printf("Delete coroutine %p\n", (void *)cp);
    cmi_coroutine_destroy(cp);

    cmi_test_print_line("=");
}

/* A coroutine that transfers control to a partner coroutine and back */
static void *corofunc_2(struct cmi_coroutine *myself, void *context)
{
    /* The context is not used here, indirectly using the caller pointer instead */
    printf("corofunc_2(%p, %p) running\n", (void *)myself, context);

    for (unsigned ui = 0; ui < 5; ui++) {
        /* Wrap the index number in a fortune cookie and pass it back */
        uint64_t *cookie = cmi_malloc(sizeof(*cookie));
        *cookie = ui;
        printf("corofunc_2: Yields cookie %llu back to boss\n", *cookie);
        uint64_t *ticket = cmi_coroutine_yield(cookie);
        printf("corofunc_2: Received ticket %llu in return\n", *ticket);
        /* Toss it and try again */
        cmi_free(ticket);
    }

    printf("corofunc_2: Done, exit value NULL\n");
    /* Will transfer control back to parent */
    cmi_coroutine_exit(NULL);

    /* Never gets here */
    return (void *)0xBADF00D;
}

/* A coroutine that transfers control to a partner coroutine and back */
static void *corofunc_1(struct cmi_coroutine *myself, void *context)
{
    /* The context is a disguised pointer to the other coroutine */
    struct cmi_coroutine *buddy = context;
    printf("corofunc_1(%p, %p) running\n", (void *)myself, (void *)buddy);

    /* We are evidently running, start the buddy as well. */
    void *ret = cmi_coroutine_start(buddy, (void *)0x5EAF00D);
    printf("corofunc_1: Back, return value %p, now trade tickets for cookies\n", ret);

    int cntr = 100;
    while (ret != NULL) {
        uint64_t *cookie = ret;
        printf("corofunc_1: Got cookie %llu\n", *cookie);
        /* Inedible, toss it */
        cmi_free(cookie);
        uint64_t *ticket = cmi_malloc(sizeof(*ticket));
        *ticket = cntr++;
        printf("corofunc_1: Returns ticket %llu\n", *ticket);
        ret = cmi_coroutine_resume(buddy, ticket);
    }

    /* Return is caught and redirected to cmi_coroutine_exit(ret) */
    printf("corofunc_1: Wut, no more cookies?\n");
    return (void *)0x5EAF00D;
}

static void test_asymmetric(void)
{
    printf("Test asymmetric coroutines\n");
    const size_t stksz = 16 * 1024;
    printf("Create two coroutines, stack size %llu\n", stksz);
    struct cmi_coroutine *cp1 = cmi_coroutine_create();
    struct cmi_coroutine *cp2 = cmi_coroutine_create();
    cmi_coroutine_initialize(cp2, corofunc_2, NULL, NULL, stksz);
    cmi_coroutine_initialize(cp1, corofunc_1, cp2, NULL, stksz);

    /* Start cp1 and hence the entire circus */
    printf("Start %p\n", (void *)cp1);
    void *ret = cmi_coroutine_start(cp1, (void *)0x5EAF00D);
    printf("Survived, now back in main coroutine, received %p\n", ret);

    /* Destroy the coroutine to free its memory allocation*/
    printf("Delete coroutine %p\n", (void *)cp1);
    cmi_coroutine_destroy(cp1);
    printf("Delete coroutine %p\n", (void *)cp2);
    cmi_coroutine_destroy(cp2);

    cmi_test_print_line("=");
}



int main(void)
{
    cmi_test_print_line("*");
    printf("**********************         Testing coroutines         **********************\n");
    cmi_test_print_line("*");

    test_simple_event();
    test_asymmetric();

    return 0;
}