// Copyright (C) 2025 The Android Open Source Project
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

#include <stdlib.h>

#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <cstring>

#if defined(__Fuchsia__)
#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <log/log.h>
#else
#include <libgen.h>
#endif

#include "cutils/log.h"
#include "cutils/properties.h"

extern "C" {

#if !defined(__Fuchsia__)
static void linux_log_prefix(const char *prefix, const char *file, int line, const char *format,
                             va_list ap, ...)
{
  char buf[50];
  char *dup = strdup(file);
  if (!dup)
    return;

  snprintf(buf, sizeof(buf), "[%s(%d)]", basename(dup), line);
  fprintf(stderr, "%s", buf);
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");

  free(dup);
}
#endif

int property_get(const char* key, char* value, const char* default_value) {
  return 0;
}

// According to https://developer.android.com/ndk/reference/group/logging,
// some log levels "Should typically be disabled for a release apk."
static constexpr bool include_debug_logging() {
#if defined(DEBUG)
    return true;
#else
    return false;
#endif
}

int __android_log_print(int priority, const char* tag, const char* file, int line,
                        const char* format, ...) {
    const char* local_tag = tag;
    if (!local_tag) {
        local_tag = "<NO_TAG>";
    }

    va_list ap;
    va_start(ap, format);
#if defined(__Fuchsia__)
    switch (priority) {
        case ANDROID_LOG_VERBOSE:
        case ANDROID_LOG_DEBUG:
            if (include_debug_logging()) {
                gfxstream_fuchsia_log(FUCHSIA_LOG_DEBUG, local_tag, file, line, format, ap);
            }
            break;
        case ANDROID_LOG_INFO:
            if (include_debug_logging()) {
                gfxstream_fuchsia_log(FUCHSIA_LOG_INFO, local_tag, file, line, format, ap);
            }
            break;
        case ANDROID_LOG_WARN:
            gfxstream_fuchsia_log(FUCHSIA_LOG_WARNING, local_tag, file, line, format, ap);
            break;
        case ANDROID_LOG_ERROR:
            gfxstream_fuchsia_log(FUCHSIA_LOG_ERROR, local_tag, file, line, format, ap);
            break;
        case ANDROID_LOG_FATAL:
            gfxstream_fuchsia_log(FUCHSIA_LOG_FATAL, local_tag, file, line, format, ap);
            break;
        default:
            gfxstream_fuchsia_log(FUCHSIA_LOG_INFO, local_tag, file, line, format, ap);
            break;
    }
#else
    // Should we check include_debug_logging() here?
    linux_log_prefix(local_tag, file, line, format, ap);
#endif

    return 1;
}

void __android_log_assert(const char* condition, const char* tag, const char* file, int line,
                          const char* format, ...) {
    const char* local_tag = tag;
    if (!local_tag) {
        local_tag = "<NO_TAG>";
    }
    va_list ap;
    va_start(ap, format);
#if defined(__Fuchsia__)
    gfxstream_fuchsia_log(FUCHSIA_LOG_ERROR, local_tag, file, line, format, ap);
#else
    linux_log_prefix(local_tag, file, line, format, ap);
#endif

    va_end(ap);

    abort();
}

int sync_wait(int fd, int timeout) { return -1; }
}
