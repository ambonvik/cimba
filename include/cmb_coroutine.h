/*
 * cmb_coroutine.h - general symmetric stackful coroutines
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
 *
 * See also:
 *      https://www.inf.puc-rio.br/~roberto/docs/MCC15-04.pdf
 *      https://probablydance.com/2013/02/20/handmade-coroutines-for-windows/
 *      https://github.com/HirbodBehnam/UserContextSwitcher
 *      https://github.com/edubart/minicoro
 *      https://github.com/hnes/libaco
 *      ... and many more.
 *
 * Self-contained implementation here, due to unique requirements:
 *  1. Detailed control of what CPU registers and flags get stored in context switch
 *  2. Passing arbitrary objects from a yield to the corresponding resune, and vice versa
 *  3. Passing arbitrary objects as starting arguments and return values to/from coroutines
 *  4. Multi-platform, Windows and Linux, but restricted to AMD64/x86-64 architecture only.
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

#ifndef CIMBA_CMB_COROUTINE_H
#define CIMBA_CMB_COROUTINE_H

struct cmb_coroutine;
extern int cmb_coroutine_create();
extern int cmm_coroutine_start();
extern void *cmb_coroutine_yield();
extern void *cmb_coroutine_resume();
extern void cmb_coroutine_stop();
extern void cmb_coroutine_destroy();

#endif /* CIMBA_CMB_COROUTINE_H */
