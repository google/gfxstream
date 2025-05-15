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

#ifndef GFXSTREAM_HOST_LOGGING_H
#define GFXSTREAM_HOST_LOGGING_H

#include <cstdarg>
#include <cstdint>
#include <functional>
#include <string>

namespace gfxstream {
namespace host {

enum class LogLevel : uint32_t {
    kFatal = 1,
    kError = 2,
    kWarning = 3,
    kInfo = 4,
    kDebug = 5,
    kVerbose = 6,
};

#ifndef GFXSTREAM_DEFAULT_LOG_LEVEL
#define GFXSTREAM_DEFAULT_LOG_LEVEL gfxstream::host::LogLevel::kInfo
#endif

namespace impl {

void GfxstreamLog(LogLevel type, const char* file, int line, const char* function,
                  const char* format, ...);

}  // namespace impl

std::string GetDefaultFormattedLog(LogLevel /*level*/, const char* /*file*/, int /*line*/,
                                   const char* /*function*/, const char* /*message*/);

using GfxstreamLogCallback =
    std::function<void(LogLevel /*level*/, const char* /*file*/, int /*line*/,
                       const char* /*function*/, const char* /*message*/)>;

void SetGfxstreamLogCallback(GfxstreamLogCallback callback);

void SetGfxstreamLogLevel(LogLevel level);

#define GFXSTREAM_LOG_INNER(level, fmt, ...)                                                 \
    gfxstream::host::impl::GfxstreamLog(level, __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, \
                                        ##__VA_ARGS__)

#define GFXSTREAM_FATAL(fmt, ...) \
    GFXSTREAM_LOG_INNER(gfxstream::host::LogLevel::kFatal, fmt, ##__VA_ARGS__)

#define GFXSTREAM_ERROR(fmt, ...) \
    GFXSTREAM_LOG_INNER(gfxstream::host::LogLevel::kError, fmt, ##__VA_ARGS__)

#define GFXSTREAM_WARNING(fmt, ...) \
    GFXSTREAM_LOG_INNER(gfxstream::host::LogLevel::kWarning, fmt, ##__VA_ARGS__)

#define GFXSTREAM_INFO(fmt, ...) \
    GFXSTREAM_LOG_INNER(gfxstream::host::LogLevel::kInfo, fmt, ##__VA_ARGS__)

#define GFXSTREAM_DEBUG(fmt, ...) \
    GFXSTREAM_LOG_INNER(gfxstream::host::LogLevel::kDebug, fmt, ##__VA_ARGS__)

#define GFXSTREAM_VERBOSE(fmt, ...) \
    GFXSTREAM_LOG_INNER(gfxstream::host::LogLevel::kVerbose, fmt, ##__VA_ARGS__)

// #define ENABLE_DECODER_LOG 1
#if defined(ENABLE_DECODER_LOG)
#define DECODER_DEBUG_LOG(fmt, ...) GFXSTREAM_LOG(fmt, ##__VA_ARGS__)
#else
#define DECODER_DEBUG_LOG(...) ((void)0)
#endif

// #define ENABLE_DISPATCH_LOG 1
#if defined(ENABLE_DISPATCH_LOG)
#define DISPATCH_DEBUG_LOG(fmt, ...) GFXSTREAM_LOG(fmt, ##__VA_ARGS__)
#else
#define DISPATCH_DEBUG_LOG(...) ((void)0)
#endif

}  // namespace host
}  // namespace gfxstream

#endif
