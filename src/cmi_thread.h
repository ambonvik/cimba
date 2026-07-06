/*
 * cmi_threads.h - helper functions to manage multitheading, especially
 *                 the cleanup if not running in a pthread. Defined in cimba.c
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
 * A global flag to ensure that the cleanup function only gets armed once
 */
extern pthread_once_t cmg_atexit_armed;

/*
 * Return true if the currently executing pthread is the main thread,
 * otherwise false if some other pthread.
 */
extern bool cmi_thread_in_main(void);

/*
 * Clean up any thread local allocated memory objects. Not called directly,
 * but scheduled using atexit() from code allocating thread local pools.
 */
extern void cmi_thread_arm_atexit_cleanup(void);

#endif //CIMBA_CMI_THREADS_H
