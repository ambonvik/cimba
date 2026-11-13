/*
 * Random seed generation for macOS.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#include <stdlib.h>

#include "cmb_assert.h"
#include "cmb_random.h"

uint64_t cmb_random_hwseed(void)
{
    uint64_t seed = 0u;
    arc4random_buf(&seed, sizeof(seed));
    cmb_assert_debug(seed != 0u);
    return seed;
}
