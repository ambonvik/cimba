/*
 * cmb_logger.c - centralized logging functions with simulation timestamps
 *
 * Copyright (c) Asbjørn M. Bonvik 1993-1995, 2025.
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
#include <stdio.h>
#include <stdlib.h>

#include "cmb_event.h"
#include "cmb_process.h"
#include "cmb_logger.h"

#include "cmi_config.h"

/* The current logging level. Initially everything on. */
CMB_THREAD_LOCAL uint32_t cmi_logger_mask = 0xFFFFFFFF;

#define TSTRBUF_SZ 32

static const char *time_to_string(const double t)
{
    static char timestrbuf[TSTRBUF_SZ];

    (void)snprintf(timestrbuf, TSTRBUF_SZ, "%#10.5g", t);

    return timestrbuf;
}

/* Pointer to current time formatting function */
CMB_THREAD_LOCAL static const char *(*timeformatter)(double) = time_to_string;

void cmb_set_timeformatter(cmb_timeformatter_func fp)
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
 * example cmb_logger_flags_off(CMB_LOGGER_INFO), or some user-defined mask.
 */
void cmb_logger_flags_off(uint32_t flags)
{
    cmb_assert_release(flags != 0u);

    cmi_logger_mask &= ~flags;
}


/*
 * cmb_vfprintf : Core logger func, fprintf-style with flags for matching with
 * the mask. Produces a single line of logging output. Overall output format:
 *      time process_name function (line) : [label] formatted_message
 * Returns the number of characters written, in case anyone cares.
 */
int cmb_vfprintf(const uint32_t flags,
                 FILE *fp,
                 const char *func,
                 const int line,
                 const char *fmtstr,
                 const va_list args)
{
    int ret = 0;
    if ((flags & cmi_logger_mask) != 0) {
       int r = fprintf(fp, "%s\t", timeformatter(cmb_time()));
        assert(r > 0);
        ret += r;

        const struct cmb_process *pp = cmb_process_get_current();
        if (pp != NULL) {
            const char *pp_name = cmb_process_get_name(pp);
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

        r += fprintf(fp, "\n");
        assert(r > 0);
        ret += r;

        fflush(fp);
    }

    return ret;
}

void cmi_logger_fatal(FILE *fp, const char *func, const int line, char *fmtstr, ...)
{
    fflush(NULL);
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMB_LOGGER_FATAL, fp, func, line, fmtstr, args);
    va_end(args);
    abort();
}

void cmi_logger_error(FILE *fp, const char *func, const int line, char *fmtstr, ...)
{
    fflush(NULL);
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMB_LOGGER_ERROR, fp, func, line, fmtstr, args);
    va_end(args);
    exit(1);
}

void cmi_logger_warning(FILE *fp, const char *func, const int line, char *fmtstr, ...)
{
    fflush(NULL);
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMB_LOGGER_WARNING, fp, func, line, fmtstr, args);
    va_end(args);
}

void cmi_logger_info(FILE *fp, const char *func, const int line, char *fmtstr, ...)
{
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMB_LOGGER_INFO, fp, func, line, fmtstr, args);
    va_end(args);
}

void cmi_logger_user(const uint32_t flags, FILE *fp,  const char *func, const int line, char *fmtstr, ...)
{
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(flags, fp, func, line, fmtstr, args);
    va_end(args);
}
