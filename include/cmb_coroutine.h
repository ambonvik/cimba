/*
 * cmi_coroutine.h - general symmetric stackful coroutines
 *
 * Considered an internal (cmi_*) class since the semantics of symmetric
 * coroutines are a bit too general for simulation purposes, but we will
 * build the cmb_process class of asymmetric coroutines for direct use in
 * the simulation on top of these. The difference:
 * - Symmetric coroutines can transfer control to any other coroutine in a
 *   peer-to-peer relationship.
 * - Asymmetric coroutines only transfer control (yield) back to a main
 *   coroutine, the scheduler, which then selects the next coroutine to resume.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
 *
 * See also:
 *      https://dl.acm.org/doi/pdf/10.1145/1462166.1462167
 *      https://github.com/HirbodBehnam/UserContextSwitcher
 *      https://probablydance.com/2013/02/20/handmade-coroutines-for-windows/
 *      https://github.com/edubart/minicoro
 *      https://github.com/tidwall/neco
 *      https://github.com/hnes/libaco
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

struct cmi_coroutine;
extern struct cmi_coroutine *cmi_coroutine_create(void);
extern int cmi_coroutine_start(struct cmi_coroutine *new);
extern void *cmi_coroutine_transfer(struct cmi_coroutine *from,
                                    struct cmi_coroutine *to,
                                    void *arg);
extern int cmi_coroutine_stop(struct cmi_coroutine *victim);
extern int cmi_coroutine_destroy(struct cmi_coroutine *victim);
extern struct cmi_coroutine *cmi_coroutine_current(void);

#endif /* CIMBA_CMB_COROUTINE_H */