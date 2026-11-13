/*
 * Random seed generation for arm64 Linux.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pthread.h>
#include <stdint.h>
#include <sys/random.h>
#include <time.h>

#include "cmb_assert.h"
#include "cmb_random.h"

uint64_t cmb_random_hwseed(void)
{
    uint64_t seed = 0u;
    if (getrandom(&seed, sizeof(seed), 0) != (ssize_t)sizeof(seed)) {
        seed = 0u;
    }

    if (seed == 0u) {
        /* No kernel entropy, mash the thread id, clock and virtual counter */
        seed = (uint64_t)pthread_self() << 32;
        struct timespec ts;
        const int r = clock_gettime(CLOCK_REALTIME, &ts);
        cmb_assert_release(r == 0);
        seed += (uint64_t)(ts.tv_nsec ^ ts.tv_sec);

        uint64_t ticks;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks));
        seed ^= ticks;
    }

    cmb_assert_debug(seed != 0u);
    return seed;
}
