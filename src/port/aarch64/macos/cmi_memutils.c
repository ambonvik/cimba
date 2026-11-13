/*
 * Aligned allocation helpers for macOS.
 *
 * Copyright (c) FBarrca 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#include <malloc/malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmb_assert.h"
#include "cmi_memutils.h"

size_t cmi_pagesize(void)
{
    return (size_t)sysconf(_SC_PAGESIZE);
}

void *cmi_aligned_alloc(const size_t align, const size_t sz)
{
    void *ptr = NULL;
    const int result = posix_memalign(&ptr, align, sz);
    cmb_assert_always(result == 0);
    cmb_assert_always(ptr != NULL);
    return ptr;
}

void cmi_aligned_free(void *ptr)
{
    cmb_assert_release(ptr != NULL);
    free(ptr);
}

void *cmi_aligned_realloc(void *ptr,
                          const size_t align,
                          const size_t sz)
{
    cmb_assert_release(ptr != NULL);
    void *replacement = cmi_aligned_alloc(align, sz);
    const size_t old_size = malloc_size(ptr);
    memcpy(replacement, ptr, old_size < sz ? old_size : sz);
    free(ptr);
    return replacement;
}
