/*
 * cmi_threads.h - helper functions to manage multitheading, especially
 *                 the cleanup if not running in a pthread.
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

#ifndef CIMBA_CMI_THREADS_H
#define CIMBA_CMI_THREADS_H

#include <stdbool.h>

/*
 * Return true if the currently executing pthread is the main thread,
 * otherwise false if some other pthread. Defined in cimba.c
 */
extern bool cmi_thread_in_main(void);

/*
 * Clean up any thread local allocated memory objects. Not called directly,
 * but scheduled using atexit() or pthread_cleanup_push(). The call signature
 * is different, since atexit(foo) expects a void foo(void), while
 * pthread_cleanup_push(foo, arg) expects a void foo(void *arg). The argument
 * arg will not be used, could be NULL.
 */
extern void cmi_thread_main_cleanup(void);
extern void cmi_thread_pthread_cleanup(void *arg);

#endif //CIMBA_CMI_THREADS_H
