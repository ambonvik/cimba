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

/* todo: add functions to set and clear mask */

#define TSTRBUF_SZ 16

static char *time_to_string(const double t, char *buf, const size_t bufsz)
{
    (void)snprintf(buf, bufsz, "%#10.5g", t);

    return buf;
}

/* Pointer to current time formatting function */
CMB_THREAD_LOCAL static char *(*timeformatter)(double, char*, size_t) = time_to_string;

void cmb_set_timeformatter(cmb_timeformatter_func fp)
{
    assert(fp != NULL);

    timeformatter = fp;
}

/*
 * Core logger function, fprintf-style with flags for matching with the mask.
 * Overall format: <time> <process> <label> <message> \n
 * Returns the number of characters written, in case anyone cares.
 */
int cmb_vfprintf(const uint32_t flags,
                 FILE *fp,
                 const char *fmtstr,
                 const va_list args)
{
    int ret = 0;
    if ((flags & cmi_logger_mask) != 0) {
        char timestrbuf[TSTRBUF_SZ];
        int r = fprintf(fp, "%s\t", timeformatter(cmb_time(), timestrbuf, TSTRBUF_SZ));
        assert(r > 0);
        ret += r;

        struct cmb_process *pp = cmb_process_get_current();
        if (pp != NULL) {
            r = fprintf(fp, "%s\t", pp->name);
            assert(r > 0);
            ret += r;
        }
        else {
            r = fprintf(fp, "main_process\t");
            assert(r > 0);
            ret += r;
        }

        char *label;
        if (flags >= CMI_LOGGER_FATAL)
            label = "Fatal";
        else if (flags >= CMI_LOGGER_ERROR)
            label = "Error";
        else if (flags >= CMI_LOGGER_WARNING)
            label = "Warning";
        else if (flags >= CMI_LOGGER_INFO)
            label = "Info";
        else
            label = "";

        r = fprintf(fp, "%s: ", label);
        assert(r > 0);
        ret += r;

        r = vfprintf (fp, fmtstr, args);
        assert(r > 0);
        ret += r;

        r += fprintf(fp, "\n");
        assert(r > 0);
        ret += r;
    }

    return ret;
}

void cmb_logger_fatal(FILE *fp, char *fmtstr, ...)
{
    fflush(NULL);
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMI_LOGGER_FATAL, fp, fmtstr, args);
    va_end(args);
    abort();
}

void cmb_logger_error(FILE *fp, char *fmtstr, ...)
{
    fflush(NULL);
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMI_LOGGER_ERROR, fp, fmtstr, args);
    va_end(args);
    exit(1);
}

void cmb_logger_warning(FILE *fp, char *fmtstr, ...)
{
    fflush(NULL);
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMI_LOGGER_WARNING, fp, fmtstr, args);
    va_end(args);
}

void cmb_logger_info(FILE *fp, char *fmtstr, ...)
{
    va_list args;
    va_start(args, fmtstr);
    (void)cmb_vfprintf(CMI_LOGGER_INFO, fp, fmtstr, args);
    va_end(args);
}