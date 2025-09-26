/*
 * cmi_config.c - Configuration dependent utility functions
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

#include "cmi_config.h"

#if CMB_OS == Windows
#include <windows.h>
size_t cmi_get_pagesize(void) {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return sys_info.dwPageSize;
}
#elif CMB_OS == Linux
#iinclude <unistd.h>
size_t cmi_get_pagesize(void) {
    return (size_t)getpagesize();
}
#else
#error "Platform operating system not yet supported."
#endif