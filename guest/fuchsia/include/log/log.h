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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __LOG_LOG_H__
#define __LOG_LOG_H__

#include <cutils/log.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef __Fuchsia__
extern "C" {
void gfxstream_fuchsia_log(int8_t severity, const char* tag, const char* file, int line,
                           const char* format, va_list va);
}
#endif

#endif
