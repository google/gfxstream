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

#include <stdarg.h>  // for va_list
#include <stddef.h>  // for size_t

#include <memory>
#include <mutex>   // for mutex
#include <string>  // for string

#include "aemu/base/logging/Log.h"  // for LogParams

namespace android {
namespace base {

// A LogFormatter formats a log line.
class LogFormatter {
   public:
    virtual ~LogFormatter() = default;

    // Formats the given line, returning the string that should be printed, or
    // empty in case nothing should be printed.
    //
    // The last line should not be terminated by a newline.
    virtual std::string format(const LogParams& params, const std::string& line) = 0;
};

// This simply logs the level, and message according to the following regex:
// ^(VERBOSE|DEBUG|INFO|WARNING|ERROR|FATAL|UNKWOWN)\s+\| (.*)
class SimpleLogFormatter : public LogFormatter {
   public:
    std::string format(const LogParams& params, const std::string& line) override;
};

// This simply logs the time, level and message according to the following
// regex:
// ^(\d+:\d+:\d+\.\d+)\s+(VERBOSE|DEBUG|INFO|WARNING|ERROR|FATAL|UNKWOWN)\s+\|
// (.*)
class SimpleLogWithTimeFormatter : public LogFormatter {
   public:
    std::string format(const LogParams& params, const std::string& line) override;
};

// This is a more verbose log line, which includes all we know:
//
// According to the regex below:
// ^(\d+:\d+:\d+\.\d+)
// (\d+)\s+(VERBOSE|DEBUG|INFO|WARNING|ERROR|FATAL|UNKWOWN)\s+([\w-]+\.[A-Za-z]+:\d+)\s+\|
// (.*)
//
// Where:
// group 1: Time stamp
// group 2: Thread Id
// group 3: Log level
// group 4: File:Line
// group 5: Log message
class VerboseLogFormatter : public LogFormatter {
   public:
    std::string format(const LogParams& params, const std::string& line) override;
};


// Uses Google's standard logging prefix (go/logging#prefix)
// so that the logs are correctly parsed by standard tools (e.g. Analog)
class GoogleLogFormatter : public LogFormatter {
    public:
    std::string format(const LogParams& params, const std::string& line) override;
};

// This formatter removes all duplicate lines, replacing them with an occurrence
// count.
//
// WARNING: This logger does not neccessarily produces output, and buffers the
// last line if it was a duplicate.
class NoDuplicateLinesFormatter : public LogFormatter {
   public:
    NoDuplicateLinesFormatter(std::shared_ptr<LogFormatter> logger);

    // Will return "" when the last line was a duplicate.
    std::string format(const LogParams& params, const std::string& line) override;

   private:
    std::shared_ptr<LogFormatter> mInner;
    std::string kEmpty{};
    std::string mPrevLogLine;
    LogParams mPrevParams;
    int mDuplicates{0};
    std::mutex mFormatMutex;
};

}  // namespace base
}  // namespace android
