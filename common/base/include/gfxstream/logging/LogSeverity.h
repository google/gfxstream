// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <stdint.h>

#ifndef LOGGING_API
#ifdef _MSC_VER
#ifdef LOGGING_API_SHARED
#define LOGGING_API __declspec(dllexport)
#else
#define LOGGING_API __declspec(dllimport)
#endif
#else
#define LOGGING_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_SEVERITY
// Defines the available log severities.
typedef enum LogSeverity {
    EMULATOR_LOG_VERBOSE = -2,
    EMULATOR_LOG_DEBUG = -1,
    EMULATOR_LOG_INFO = 0,
    EMULATOR_LOG_WARNING = 1,
    EMULATOR_LOG_ERROR = 2,
    EMULATOR_LOG_FATAL = 3,
    EMULATOR_LOG_NUM_SEVERITIES,

// DFATAL will be ERROR in release builds, and FATAL in debug ones.
#ifdef NDEBUG
    EMULATOR_LOG_DFATAL = EMULATOR_LOG_ERROR,
#else
    EMULATOR_LOG_DFATAL = EMULATOR_LOG_FATAL,
#endif
} LogSeverity;

// Returns the minimal log level.
LOGGING_API LogSeverity getMinLogLevel();
LOGGING_API void setMinLogLevel(LogSeverity level);


typedef enum {
    kLogDefaultOptions = 0,
    kLogEnableDuplicateFilter = 1,
    kLogEnableTime = 1 << 2,
    kLogEnableVerbose = 1 << 3,
} LoggingFlags;

// Enable/disable verbose logs from the base/* family.
LOGGING_API void base_enable_verbose_logs();
LOGGING_API void base_disable_verbose_logs();

LOGGING_API void verbose_enable(uint64_t tag);
LOGGING_API void verbose_disable(uint64_t tag);
LOGGING_API bool verbose_check(uint64_t tag);
LOGGING_API bool verbose_check_any();
LOGGING_API void set_verbosity_mask(uint64_t mask);
LOGGING_API uint64_t get_verbosity_mask();

// Configure the logging framework.
LOGGING_API void base_configure_logs(LoggingFlags flags);

#ifdef __cplusplus
}
#endif

