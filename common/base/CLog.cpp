// Copyright 2022 The Android Open Source Project
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

#include "aemu/base/logging/CLog.h"


#include <stdio.h>                              // for size_t, fflush, vsnpr...

extern "C" void __emu_log_print(LogSeverity prio,
                                const char* file,
                                int line,
                                const char* fmt,
                                ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, fmt, args);
    va_end(args);
}
