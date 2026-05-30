/*
 * cmi_sanitizer.h - thin wrappers around the AddressSanitizer fiber-switch and
 * ThreadSanitizer fiber APIs so the coroutine layer can be sanitized correctly.
 * Every wrapper compiles to nothing in a non-instrumented build.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */
#ifndef CIMBA_CMI_SANITIZER_H
#define CIMBA_CMI_SANITIZER_H

#include <stddef.h>

/* Detect AddressSanitizer (GCC: __SANITIZE_ADDRESS__, Clang: __has_feature) */
#if defined(__SANITIZE_ADDRESS__)
#  define CMI_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define CMI_ASAN 1
#  endif
#endif

/* Detect ThreadSanitizer (GCC: __SANITIZE_THREAD__, Clang: __has_feature) */
#if defined(__SANITIZE_THREAD__)
#  define CMI_TSAN 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define CMI_TSAN 1
#  endif
#endif

#if defined(CMI_ASAN)
#  include <sanitizer/common_interface_defs.h>
#endif
#if defined(CMI_TSAN)
#  include <sanitizer/tsan_interface.h>
#endif

/*
 * ASan: announce the upcoming switch. `save` receives the outgoing fiber's fake
 * stack (pass NULL when the outgoing fiber will never resume, so ASan discards
 * it). `bottom`/`size` describe the destination stack.
 */
static inline void cmi_asan_start_switch(void **save, const void *bottom, size_t size)
{
#if defined(CMI_ASAN)
    __sanitizer_start_switch_fiber(save, bottom, size);
#else
    (void)save; (void)bottom; (void)size;
#endif
}

/* ASan: finalize the switch on the fiber we just landed on. */
static inline void cmi_asan_finish_switch(void *saved)
{
#if defined(CMI_ASAN)
    __sanitizer_finish_switch_fiber(saved, NULL, NULL);
#else
    (void)saved;
#endif
}

/* TSan: fiber handle for the currently running stack (e.g. the thread itself). */
static inline void *cmi_tsan_get_current_fiber(void)
{
#if defined(CMI_TSAN)
    return __tsan_get_current_fiber();
#else
    return NULL;
#endif
}

/* TSan: create / switch-to / destroy a fiber handle. */
static inline void *cmi_tsan_create_fiber(void)
{
#if defined(CMI_TSAN)
    return __tsan_create_fiber(0);
#else
    return NULL;
#endif
}

static inline void cmi_tsan_switch_fiber(void *fiber)
{
#if defined(CMI_TSAN)
    __tsan_switch_to_fiber(fiber, 0);   /* flags 0 => establish happens-before */
#else
    (void)fiber;
#endif
}

static inline void cmi_tsan_destroy_fiber(void *fiber)
{
#if defined(CMI_TSAN)
    __tsan_destroy_fiber(fiber);
#else
    (void)fiber;
#endif
}

#endif /* CIMBA_CMI_SANITIZER_H */
