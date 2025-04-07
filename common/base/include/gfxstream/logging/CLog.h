/* Copyright (C) 2007-2008 The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
** http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "aemu/base/logging/LogSeverity.h"

#ifdef _MSC_VER
#ifdef LOGGING_API_SHARED
#define LOGGING_API __declspec(dllexport)
#else
#define LOGGING_API __declspec(dllimport)
#endif
#else
#define LOGGING_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

LOGGING_API void __emu_log_print(LogSeverity prio, const char* file, int line, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

#ifndef EMULOG
#define EMULOG(priority, fmt, ...) \
    __emu_log_print(priority, __FILE__, __LINE__, fmt, ##__VA_ARGS__);
#endif

#ifndef dprint
// Logging support.
#define dprint(fmt, ...)                               \
    if (EMULATOR_LOG_DEBUG >= getMinLogLevel()) {      \
        EMULOG(EMULATOR_LOG_DEBUG, fmt, ##__VA_ARGS__) \
    }
#endif
#ifndef dinfo
#define dinfo(fmt, ...)                               \
    if (EMULATOR_LOG_INFO >= getMinLogLevel()) {      \
        EMULOG(EMULATOR_LOG_INFO, fmt, ##__VA_ARGS__) \
    }
#endif
#ifndef dwarning
#define dwarning(fmt, ...)                               \
    if (EMULATOR_LOG_WARNING >= getMinLogLevel()) {      \
        EMULOG(EMULATOR_LOG_WARNING, fmt, ##__VA_ARGS__) \
    }
#endif
#ifndef derror
#define derror(fmt, ...)                               \
    if (EMULATOR_LOG_ERROR >= getMinLogLevel()) {      \
        EMULOG(EMULATOR_LOG_ERROR, fmt, ##__VA_ARGS__) \
    }
#endif
#ifndef dfatal
#define dfatal(fmt, ...) EMULOG(EMULATOR_LOG_FATAL, fmt, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif
