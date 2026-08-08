/*
* Test script for singly and doubly linked lists.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
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

#include "cmi_slist.h"
#include "cmi_dlist.h"
#include "test.h"

struct slist_item {
    int payload;
    struct cmi_slist_node node;
};

void test_slist_inner(struct cmi_slist_node *listptr)
{
    printf("Initializing it\n");
    cmi_slist_initialize(listptr);
    cmb_assert_always(cmi_slist_is_empty(listptr));
    cmb_assert_always(cmi_slist_peek(listptr) == NULL);
    cmb_assert_always(cmi_slist_pop(listptr) == NULL);

    printf("Pushing entries into it\n");
    for (int i = 0; i < 10; i++) {
        struct slist_item *item = cmi_malloc(sizeof(struct slist_item));
        item->payload = i;
        printf("%d ", item->payload);
        cmi_slist_push(listptr, &(item->node));
    }

    printf("\nPeeking at the first item: ");
    cmb_assert_always(!cmi_slist_is_empty(listptr));
    struct cmi_slist_node *first = cmi_slist_peek(listptr);
    cmb_assert_always(first != NULL);
    struct slist_item *first_item = cmi_slist_entry(first, struct slist_item, node);
    printf("%d\n", first_item->payload);
    cmb_assert_always(first_item != NULL);
    cmb_assert_always(first_item->payload == 9);

    printf("Popping entries from it\n");
    for (int i = 9; i >= 0; i--) {
        struct cmi_slist_node *popped = cmi_slist_pop(listptr);
        cmb_assert_always(popped != NULL);
        struct slist_item *item = cmi_slist_entry(popped, struct slist_item, node);
        printf("%d ", item->payload);
        cmb_assert_always(item != NULL);
        cmb_assert_always(item->payload == i);
        cmi_free(item);
    }
    printf("\n");

    cmb_assert_always(cmi_slist_is_empty(listptr));
    cmb_assert_always(cmi_slist_peek(listptr) == NULL);
    cmb_assert_always(cmi_slist_pop(listptr) == NULL);

    printf("Terminating it\n");
    cmi_slist_terminate(listptr);
}

void test_slist(void)
{
    printf("\nTesting a single-linked list on the stack\n");
    struct cmi_slist_node list;
    test_slist_inner(&list);

    printf("\nTesting a single-linked list on the heap\n");
    struct cmi_slist_node *listhead = cmi_slist_create();
    test_slist_inner(listhead);
    cmi_slist_destroy(listhead);
}

struct dlist_item {
    int payload;
    struct cmi_dlist_node node;
};

void dlist_print(struct cmi_dlist_node *head)
{
    struct cmi_dlist_node *node = cmi_dlist_first(head);
    while (node != head) {
        struct dlist_item *item = cmi_dlist_entry(node, struct dlist_item, node);
        printf("%d ", item->payload);
        node = node->next;
    }
    printf("\n");
}

void test_dlist_inner(struct cmi_dlist_node *listptr)
{
    printf("Initializing it\n");
    cmi_dlist_initialize(listptr);
    cmb_assert_always(cmi_dlist_is_empty(listptr));
    cmb_assert_always(cmi_dlist_first(listptr) == NULL);
    cmb_assert_always(cmi_dlist_remove_first(listptr) == NULL);
    cmb_assert_always(cmi_dlist_remove_last(listptr) == NULL);
    cmb_assert_always(cmi_dlist_unlink(listptr) == false);

    printf("Adding entries from the head\n");
    struct dlist_item *item = NULL;
    for (int i = 0; i < 5; i++) {
        item = cmi_malloc(sizeof(struct dlist_item));
        item->payload = i;
        printf("%d ", item->payload);
        cmi_dlist_insert_first(listptr, &(item->node));
    }

    cmb_assert_always(!cmi_dlist_is_empty(listptr));
    struct cmi_dlist_node *first = cmi_dlist_first(listptr);
    cmb_assert_always(first != NULL);
    struct dlist_item *first_item = cmi_dlist_entry(first, struct dlist_item, node);
    cmb_assert_always(first_item != NULL);
    printf("\nFirst item %d\n", first_item->payload);
    cmb_assert_always(first_item->payload == 4);

    struct cmi_dlist_node *last = cmi_dlist_last(listptr);
    cmb_assert_always(last != NULL);
    struct dlist_item *last_item = cmi_dlist_entry(last, struct dlist_item, node);
    printf("Last item %d\n", last_item->payload);
    cmb_assert_always(last_item->payload == 0);

    printf("Adding entries from the head\n");
    struct cmi_dlist_node *middle = last;
    for (int i = 5; i < 10; i++) {
        item = cmi_malloc(sizeof(struct dlist_item));
        item->payload = i;
        printf("%d ", item->payload);
        cmi_dlist_insert_last(listptr, &(item->node));
    }

    cmb_assert_always(!cmi_dlist_is_empty(listptr));
    printf("List now: ");
    dlist_print(listptr);
    first = cmi_dlist_first(listptr);
    cmb_assert_always(first != NULL);
    first_item = cmi_dlist_entry(first, struct dlist_item, node);
    cmb_assert_always(first_item != NULL);
    printf("\nFirst item: %d\n", first_item->payload);
    cmb_assert_always(first_item->payload == 4);

    last = cmi_dlist_last(listptr);
    cmb_assert_always(last != NULL);
    last_item = cmi_dlist_entry(last, struct dlist_item, node);
    printf("Last item: %d\n", last_item->payload);
    cmb_assert_always(last_item->payload == 9);

    printf("\nInserting in the middle\n");
    item = cmi_malloc(sizeof(struct dlist_item));
    item->payload = 10;
    printf("%d ", item->payload);
    cmi_dlist_insert_before(middle, &(item->node));
    item = cmi_malloc(sizeof(struct dlist_item));
    item->payload = 11;
    printf("%d ", item->payload);
    cmi_dlist_insert_after(middle, &(item->node));
    printf("\nList now: ");
    dlist_print(listptr);

    printf("Deleting the middle\n");
    cmb_assert_always(cmi_dlist_unlink(middle) == true);
    printf("List now: ");
    dlist_print(listptr);

    printf("Removing from the head\n");
    struct cmi_dlist_node *node = cmi_dlist_remove_first(listptr);
    cmb_assert_always(node != NULL);
    item = cmi_dlist_entry(node, struct dlist_item, node);
    cmb_assert_always(item != NULL);
    cmb_assert_always(item->payload == 4);
    cmi_free(item);

    cmb_assert_always(!cmi_dlist_is_empty(listptr));
    printf("List now: ");
    dlist_print(listptr);
    first = cmi_dlist_first(listptr);
    cmb_assert_always(first != NULL);
    first_item = cmi_dlist_entry(first, struct dlist_item, node);
    cmb_assert_always(first_item != NULL);
    printf("First item: %d\n", first_item->payload);
    cmb_assert_always(first_item->payload == 3);

    printf("Removing from the tail\n");
    node = cmi_dlist_remove_last(listptr);
    cmb_assert_always(node != NULL);
    item = cmi_dlist_entry(node, struct dlist_item, node);
    cmb_assert_always(item != NULL);
    cmb_assert_always(item->payload == 9);
    cmi_free(item);

    cmb_assert_always(!cmi_dlist_is_empty(listptr));
    printf("List now: ");
    dlist_print(listptr);
    last = cmi_dlist_last(listptr);
    cmb_assert_always(last != NULL);
    last_item = cmi_dlist_entry(last, struct dlist_item, node);
    cmb_assert_always(last_item != NULL);
    printf("Last item: %d\n", last_item->payload);
    cmb_assert_always(last_item->payload == 8);

    printf("Clearing it from the tail:");
    while (!cmi_dlist_is_empty(listptr)) {
        node = cmi_dlist_remove_last(listptr);
        cmb_assert_always(node != NULL);
        item = cmi_dlist_entry(node, struct dlist_item, node);
        cmb_assert_always(item != NULL);
        printf(" %d", item->payload);
        cmi_free(item);
    }
    printf("\n");

    cmb_assert_always(cmi_dlist_is_empty(listptr));
    cmb_assert_always(cmi_dlist_first(listptr) == NULL);
    cmb_assert_always(cmi_dlist_last(listptr) == NULL);

    printf("Terminating it\n");
    cmi_dlist_terminate(listptr);
}


void test_dlist(void)
{
    printf("\nTesting a double-linked list on the stack\n");
    struct cmi_dlist_node list;
    test_dlist_inner(&list);

    printf("\nTesting a double-linked list on the heap\n");
    struct cmi_dlist_node *listhead = cmi_dlist_create();
    test_dlist_inner(listhead);
    cmi_dlist_destroy(listhead);
}

int main(void) {

    cmi_test_print_line("*");
    printf("******************************   Testing lists   *******************************\n");
    cmi_test_print_line("*");

    test_slist();
    test_dlist();

    cmi_test_print_line("*");
    return 0;
}