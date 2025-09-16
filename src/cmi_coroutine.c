/*
 * cmi_coroutine.c - general stackful coroutines
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
#include <stddef.h>

#include "cmi_config.h"
#include "cmi_coroutine.h"

/*
 * cmi_coroutine_current : The currently executing coroutine, if any.
 * NULL means that the main execution thread has the CPU, which always will be
 * the case before any coroutines are created and started.
 */
static CMB_THREAD_LOCAL struct cmi_coroutine *cmi_coroutine_current = NULL;

struct cmi_coroutine {
    void *parent_stack_pointer;
    void *caller_stack_pointer;
    void *stack;
    size_t stack_size;
    enum cmi_coroutine_state status;
    void *exit_value;
};

struct cmi_coroutine *cmi_coroutine_create(void) {
    return NULL;
}
void *cmi_coroutine_start(struct cmi_coroutine *new) {
    return NULL;
}

void cmi_coroutine_stop(struct cmi_coroutine *victim) {

}

void cmi_coroutine_destroy(struct cmi_coroutine *victim) {

}

void *cmi_coroutine_get_exit_value(struct cmi_coroutine *corp) {
    return corp->exit_value;
}

extern struct cmi_coroutine *cmi_coroutine_get_current(void) {
    return NULL;
}

/* The state of the given coroutine */
extern enum cmi_coroutine_state cmi_coroutine_get_state(struct cmi_coroutine *corp) {
    return corp->status;
}

/* Symmetric coroutine pattern */
extern void *cmi_coroutine_transfer(struct cmi_coroutine *from,
                                    struct cmi_coroutine *to,
                                    void *arg) {
    return NULL;
}

/* Asymmetric coroutine pattern */
extern void *cmi_coroutine_yield(struct cmi_coroutine from, void *arg) {
    return NULL;
}

extern void *cmi_coroutine_resume(struct cmi_coroutine to, void *arg) {
    return NULL;
}