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

#include <assert.h>
#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <log/log.h>
#include <stdio.h>
#include <stdlib.h>

#include <cstdarg>

static void log_vararg(int8_t severity, const char* tag, const char* file, int line,
                                 const char* format, ...) {
    va_list args;
    va_start(args, format);
    gfxstream_fuchsia_log(severity, tag, file, line, format, args);
    va_end(args);
}

void __assert_fail(const char* expr, const char* file, int line, const char* func) {
    log_vararg(FUCHSIA_LOG_ERROR, "gfxstream", file, line,
                                "Assertion failed: %s (%s: %s: %d)", expr, file, func, line);
    abort();
}

int puts(const char *s)
{
  return fputs(s, stdout);
}

int printf(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);
  return 0;
}

int vprintf(const char *format, va_list ap)
{
  return vfprintf(stdout, format, ap);
}

int fprintf(FILE *stream, const char *format, ...)
{
  assert(stream == stdout || stream == stderr);
  if (stream == stdout || stream == stderr)
  {
    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);
  }
  return 0;
}

static inline FuchsiaLogSeverity severity(FILE* stream) {
    return stream == stdout ? FUCHSIA_LOG_INFO : FUCHSIA_LOG_ERROR;
}

int fputs(const char* s, FILE* stream) {
    assert(stream == stdout || stream == stderr);
    if (stream == stdout || stream == stderr) {
        // File is set to nullptr as that information isn't available here.
        log_vararg(severity(stream), "gfxstream", nullptr, 0, s);
    }
    return 0;
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    assert(stream == stdout || stream == stderr);
    if (stream == stdout || stream == stderr) {
        gfxstream_fuchsia_log(severity(stream), "gfxstream", __FILE__, __LINE__, format, ap);
    }
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
  assert(stream == stdout || stream == stderr);
  char buffer[512];
  size_t offset = 0;
  size_t count = 0;
  for (; count < nitems; count++)
  {
    snprintf(buffer + offset, sizeof(buffer) - offset, reinterpret_cast<const char *>(ptr) + offset, size);
    offset += size;
    if (offset > sizeof(buffer))
      break;
  }
  buffer[sizeof(buffer) - 1] = 0;
  fputs(buffer, stream);
  return count;
}
