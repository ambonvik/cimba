/*
 * cmb_logger.c - centralized logging functions with simulation timestamps
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025-26.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied .
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Using standard asserts here to avoid recursive calls */
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "cmb_event.h"
#include "cmb_process.h"
#include "cmb_logger.h"
#include "cmb_random.h"

#include "cmi_config.h"

/* Maximum length of a formatted time string before it gets truncated */
#define TSTRBUF_SZ 32

/* A trial array index guaranteed not to be used any time soon */
#define CMI_NO_TRIAL_IDX UINT32_C(0xFFFFFFFF)

/* The current logging level. Initially everything on. */
static CMB_THREAD_LOCAL uint32_t cmi_logger_mask = UINT32_C(0xFFFFFFFF);

/* A mutex to ensure that only one thread can be writing at the same time */
static pthread_mutex_t cmi_logger_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * The index of the currently executing trial in this worker thread,
 * see worker_thread_func() in cimba.c
 */
CMB_THREAD_LOCAL uint64_t cmi_logger_trial_idx = CMI_NO_TRIAL_IDX;

/*
 * Default time formatting function.
 *
 * The buffer has to be thread local to avoid overwriting by other threads
 * potentially calling the same function at the same time. If replacing by your
 * own version, make sure it is reentrant and threadsafe.
 */
static const char *time_to_string(const double t)
{
    static CMB_THREAD_LOCAL char timestrbuf[TSTRBUF_SZ];

    (void)snprintf(timestrbuf, TSTRBUF_SZ, "%#10.5g", t);

    return timestrbuf;
}

/* Pointer to current time formatting function */
static CMB_THREAD_LOCAL const char *(*timeformatter)(double) = time_to_string;

void cmb_set_timeformatter(cmb_timeformatter_func *fp)
{
    assert(fp != NULL);

    timeformatter = fp;
}

/*
 * cmb_logger_flags_on : turn on logging flags according to the bitmask, for
 * example cmb_logger_flags_on(CMB_LOGGER_INFO), or some user-defined mask.
 */
void cmb_logger_flags_on(const uint32_t flags)
{
    cmb_assert_release(flags != 0u);

    cmi_logger_mask |= flags;
}

/*
 * cmb_logger_flags_off : turn off logging flags according to the bitmask, for
 * example, cmb_logger_flags_off(CMB_LOGGER_INFO), or some user-defined mask.
 */
void cmb_logger_flags_off(uint32_t flags)
{
    cmb_assert_release(flags != 0u);

    cmi_logger_mask &= ~flags;
}

/*
 * cmb_vfprintf : Core logger func, fprintf-style with flags for matching with
 * the mask. Produces a single line of logging output. Will print the trial
 * number as the first field if part of a multi-trial experiment. Will print the
 * random number seed for message levels warning and above to enable reproducing
 * the suspect condition in a debugger or with additional logging turned on.
 *
 * Uses standard assert calls to avoid infinite recursion, since our custom
 * cmb_assert_debug and cmb_assert_release will end up here if failed.
 *
 * Overall output format:
 * [trial_index] [seed] time process_name function (line) : [label] formatted_message
 *
 * Returns the number of characters written, in case anyone cares.
 */
int cmb_vfprintf(FILE *fp,
                 const uint32_t flags,
                 const char *func,
                 const int line,
                 const char *fmtstr,
                 va_list args)
{
    int ret = 0;
    if ((flags & cmi_logger_mask) != 0) {
        pthread_mutex_lock(&cmi_logger_mutex);
        int r = 0;
        if (cmi_logger_trial_idx != CMI_NO_TRIAL_IDX) {
            r = fprintf(fp, "%" PRIu64 "\t", cmi_logger_trial_idx);
            assert(r > 0);
            ret += r;
        }

        r = fprintf(fp, "%s\t", timeformatter(cmb_time()));
        assert(r > 0);
        ret += r;

        const struct cmb_process *pp = cmb_process_current();
        if (pp != NULL) {
            const char *pp_name = cmb_process_name(pp);
            r = fprintf(fp, "%s\t", pp_name);
            assert(r > 0);
            ret += r;
        }
        else {
            r = fprintf(fp, "dispatcher\t");
            assert(r > 0);
            ret += r;
        }

        r = fprintf(fp, "%s (%d):  ", func, line);
        assert(r > 0);
        ret += r;

        if (flags >= CMB_LOGGER_WARNING) {
            char *label;
            if (flags >= CMB_LOGGER_FATAL)
                label = "Fatal";
            else if (flags >= CMB_LOGGER_ERROR)
                label = "Error";
            else
                label = "Warning";

            r = fprintf(fp, "%s: ", label);
            assert(r > 0);
            ret += r;
        }

        r = vfprintf (fp, fmtstr, args);
        assert(r > 0);
        ret += r;

        if (flags >= CMB_LOGGER_WARNING) {
            r = fprintf(fp, ", seed 0x%" PRIx64 "\t", cmb_random_curseed());
            assert(r > 0);
            ret += r;
        }

        r += fprintf(fp, "\n");
        assert(r > 0);
        ret += r;

        fflush(fp);
        pthread_mutex_unlock(&cmi_logger_mutex);
    }

    return ret;
}

void cmi_logger_fatal(FILE *fp,
                      const char *func,
                      const int line,
                      char *fmtstr,
                      ...)
{
    if ((CMB_LOGGER_FATAL & cmi_logger_mask) != 0) {
        fflush(NULL);
        va_list args;
        va_start(args, fmtstr);
        (void)cmb_vfprintf(fp, CMB_LOGGER_FATAL, func, line, fmtstr, args);
        va_end(args);
    }

    abort();
}

void cmi_logger_error(FILE *fp,
                      const char *func,
                      const int line,
                      char *fmtstr,
                      ...)
{
    if ((CMB_LOGGER_ERROR & cmi_logger_mask) != 0) {
        fflush(NULL);
        va_list args;
        va_start(args, fmtstr);
        (void)cmb_vfprintf(fp, CMB_LOGGER_ERROR, func, line, fmtstr, args);
        va_end(args);
    }

    pthread_exit(NULL);
    /* Not reached */
    abort();
}

void cmi_logger_warning(FILE *fp,
                        const char *func,
                        const int line,
                        char *fmtstr,
                        ...)
{
    if ((CMB_LOGGER_WARNING & cmi_logger_mask) != 0) {
        fflush(NULL);
        va_list args;
        va_start(args, fmtstr);
        (void)cmb_vfprintf(fp, CMB_LOGGER_WARNING, func, line, fmtstr, args);
        va_end(args);
    }
}

void cmi_logger_info(FILE *fp,
                     const char *func,
                     const int line,
                     char *fmtstr,
                     ...)
{
    if ((CMB_LOGGER_INFO & cmi_logger_mask) != 0) {
        va_list args;
        va_start(args, fmtstr);
        (void)cmb_vfprintf(fp, CMB_LOGGER_INFO, func, line, fmtstr, args);
        va_end(args);
    }
}

void cmi_logger_user(FILE *fp,
                     const uint32_t flags,
                     const char *func,
                     const int line,
                     char *fmtstr,
                     ...)
{
    if ((flags & cmi_logger_mask) != 0) {
        va_list args;
        va_start(args, fmtstr);
        (void)cmb_vfprintf(fp, flags, func, line, fmtstr, args);
        va_end(args);
    }
}
