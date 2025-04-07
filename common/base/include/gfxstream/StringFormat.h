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

#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include <stdarg.h>

namespace android {
namespace base {

// Create a new string instance that contains the printf-style formatted
// output from |format| and potentially any following arguments.
std::string StringFormatRaw(const char* format, ...);

// A variant of StringFormat() which uses a va_list to list formatting
// parameters instead.
std::string StringFormatWithArgs(const char* format, va_list args);

// Appends a formatted string at the end of an existing string.
// |string| is the target string instance, |format| the format string,
// followed by any formatting parameters. This is more efficient than
// appending the result of StringFormat(format,...) to |*string| directly.
void StringAppendFormatRaw(std::string* string, const char* format, ...);

// A variant of StringAppendFormat() that takes a va_list to list
// formatting parameters.
void StringAppendFormatWithArgs(std::string* string,
                                const char* format,
                                va_list args);

// unpackFormatArg() is a set of overloaded functions needed to unpack
// an argument of the formatting list to a POD value which can be passed
// into the sprintf()-like C function

// Anything which can be used to construct a string goes here and unpacks into
// a const char*
inline const char* unpackFormatArg(const std::string& str) {
    return str.c_str();
}

// Forward all PODs as-is
template <class T>
constexpr T&& unpackFormatArg(T&& t,
        typename std::enable_if<
                    std::is_pod<typename std::decay<T>::type>::value
                 >::type* = nullptr) {
    return std::forward<T>(t);
}

// These templated versions of StringFormat*() allow one to pass all kinds of
// string objects into the argument list
template <class... Args>
std::string StringFormat(const char* format, Args&&... args) {
    return StringFormatRaw(format, unpackFormatArg(std::forward<Args>(args))...);
}

template <class... Args>
void StringAppendFormat(std::string* string,
                        const char* format,
                        Args&&... args) {
    StringAppendFormatRaw(string, format,
                          unpackFormatArg(std::forward<Args>(args))...);
}

}  // namespace base
}  // namespace android
