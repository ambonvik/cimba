/*
* cmb_random_hwseed_Win64.c - Windows specific hardware seed
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

#include <time.h>

#include "cmb_assert.h"
#include "cmb_random.h"

/* Assembly functions, see cmi_random_hwseed_Win64.asm */
extern int cmi_cpu_has_rdseed(void);
extern int cmi_cpu_has_rdrand(void);
extern uint64_t cmi_rdseed(void);
extern uint64_t cmi_rdrand(void);
extern uint32_t cmi_threadid(void);
extern uint32_t cmi_rdtsc(void);

/* Windows-specific code to get a suitable 64-bit seed from hardware */
uint64_t cmb_random_get_hwseed(void)
{
    uint64_t seed = 0u;
    if (cmi_cpu_has_rdseed()) {
        /* Should be valid since Intel Broadwell (2014) and AMD Zen (2016) */
        seed = cmi_rdseed();
    }
    else if (cmi_cpu_has_rdrand()) {
        /* True for Intel since Ivy Bridge (2012) */
        seed = cmi_rdrand();
    }
    else {
        /* Some other (older?) CPU, build a decent seed ourselves */
        /* Start with thread id in top 32 bits */
        const uint64_t tid = cmi_threadid();
        seed = (tid << 32);

        /* Add a mashup of clock values into low 32 bits */
        struct timespec ts;
        const int r = clock_gettime(CLOCK_REALTIME, &ts);
        cmb_assert_release(r == 0);
        const uint64_t mash = (uint64_t)(ts.tv_nsec ^ ts.tv_sec);
        seed += mash;

        /* XOR the whole thing with the 64-bit CPU cycle count since reset */
        seed ^= cmi_rdtsc();
    }

    cmb_assert_debug(seed != 0u);
    return seed;
}