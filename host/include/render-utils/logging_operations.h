// Copyright 2025 The Android Open Source Project
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

#include <cstdint>

typedef enum {
    GFXSTREAM_LOGGING_LEVEL_FATAL = 1,
    GFXSTREAM_LOGGING_LEVEL_ERROR = 2,
    GFXSTREAM_LOGGING_LEVEL_WARNING = 3,
    GFXSTREAM_LOGGING_LEVEL_INFO = 4,
    GFXSTREAM_LOGGING_LEVEL_DEBUG = 5,
    GFXSTREAM_LOGGING_LEVEL_VERBOSE = 6,
} gfxstream_logging_level;

typedef void (*gfxstream_log_callback_t)(gfxstream_logging_level level,
                                         const char* file,
                                         int line,
                                         const char* function,
                                         const char* message);
