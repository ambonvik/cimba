/*
 * CPU availability for macOS.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#include <stdint.h>
#include <unistd.h>

uint32_t cmi_cpu_cores(void)
{
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (uint32_t)count : 1u;
}
