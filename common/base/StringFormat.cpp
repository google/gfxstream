// Copyright 2020 The Android Open Source Project
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

#include "aemu/base/StringFormat.h"

#include <stdio.h>

namespace android {
namespace base {

std::string StringFormatRaw(const char* format, ...) {
    va_list args;
    va_start(args, format);
    auto result = StringFormatWithArgs(format, args);
    va_end(args);
    return result;
}

std::string StringFormatWithArgs(const char* format, va_list args) {
    std::string result;
    StringAppendFormatWithArgs(&result, format, args);
    return result;
}

void StringAppendFormatRaw(std::string* string, const char* format, ...) {
    va_list args;
    va_start(args, format);
    StringAppendFormatWithArgs(string, format, args);
    va_end(args);
}

void StringAppendFormatWithArgs(std::string* string,
                                const char* format,
                                va_list args) {
    size_t cur_size = string->size();
    size_t extra = 0;
    for (;;) {
        va_list args2;
        va_copy(args2, args);
        int ret = vsnprintf(&(*string)[cur_size], extra, format, args2);
        va_end(args2);

        if (ret == 0) {
            // Nothing to do here.
            break;
        }

        if (ret > 0) {
            size_t ret_sz = static_cast<size_t>(ret);
            if (extra == 0) {
                // First pass, resize the string and try again.
                extra = ret_sz + 1;
                string->resize(cur_size + extra);
                continue;
            }
            if (ret_sz < extra) {
                // Second pass or later, success!
                string->resize(cur_size + ret_sz);
                return;
            }
        }

        // NOTE: The MSVCRT.DLL implementation of snprintf() is broken and
        // will return -1 in case of truncation. This code path is taken
        // when this happens, or when |ret_sz| is equal or larger than
        // |extra|. Grow the buffer to allow for more room, then try again.
        extra += (extra >> 1) + 32;
        string->resize(cur_size + extra);
    }
}

}  // namespace base
}  // namespace android
