// Copyright (C) 2025 Google Inc.
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

#include "gfxstream/common/logging.h"

#include <stdlib.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <filesystem>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace gfxstream {
namespace host {
namespace impl {
namespace {

GfxstreamLogCallback DefaultLogCallback() {
    return [](LogLevel level, const char* file, int line, const char* function,
              const char* message) {
        const std::string formatted = GetDefaultFormattedLog(level, file, line, function, message);

#ifdef __ANDROID__
        android_LogPriority priority = ANDROID_LOG_ERROR;
        switch (level) {
            case LogLevel::kFatal:
                priority = ANDROID_LOG_FATAL;
                break;
            case LogLevel::kError:
                priority = ANDROID_LOG_ERROR;
                break;
            case LogLevel::kWarning:
                priority = ANDROID_LOG_WARN;
                break;
            case LogLevel::kInfo:
                priority = ANDROID_LOG_INFO;
                break;
            case LogLevel::kDebug:
                priority = ANDROID_LOG_DEBUG;
                break;
            case LogLevel::kVerbose:
                priority = ANDROID_LOG_VERBOSE;
                break;
        }
        __android_log_print(priority, "GFXSTREAM", "%s\n", formatted.c_str());
#else
        FILE* f;
        switch (level) {
            case LogLevel::kFatal:
            case LogLevel::kError:
            case LogLevel::kWarning: {
                f = stderr;
                break;
            }
            case LogLevel::kInfo:
            case LogLevel::kDebug:
            case LogLevel::kVerbose: {
                f = stdout;
                break;
            }
        }
        fprintf(f, "%s\n", formatted.c_str());
        if (level == LogLevel::kFatal) {
            fflush(f);
        }
#endif
    };
}

LogLevel sLogLevel = GFXSTREAM_DEFAULT_LOG_LEVEL;
GfxstreamLogCallback sLogCallback = DefaultLogCallback();

}  // namespace

void GfxstreamLog(LogLevel level, const char* file, int line, const char* function,
                  const char* format, ...) {
    if (level > sLogLevel) {
        return;
    }

    va_list args;
    va_start(args, format);

    // Extra copy for getting length.
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int requestedLength = vsnprintf(nullptr, 0, format, argsCopy) + /* null character */ 1;
    va_end(argsCopy);

    std::vector<char> buffer;
    buffer.resize(requestedLength, 0);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    sLogCallback(level, file, line, function, buffer.data());

    if (level == LogLevel::kFatal) {
        abort();
    }
}

}  // namespace impl

std::string GetDefaultFormattedLog(LogLevel, const char* file, int line, const char*,
                                   const char* message) {
    std::stringstream ss;
    ss << "[" << std::filesystem::path(file).filename().string() << "(" << line << ")] " << message;
    return ss.str();
}

void SetGfxstreamLogCallback(GfxstreamLogCallback callback) { impl::sLogCallback = callback; }

void SetGfxstreamLogLevel(LogLevel level) { impl::sLogLevel = level; }

}  // namespace host
}  // namespace gfxstream
