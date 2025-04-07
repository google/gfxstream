// Copyright 2019 The Android Open Source Project
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

// This file defines macroes ARRAY_SIZE() and STRING_LITERAL_LENGTH for static
// array size and string literal length detection
// If used from C, it's just a standard division of sizes. In C++ though, it
// would give you a compile-time error if used with something other than
// a built-in array or std::array<>
// Also, C++ defines corresponding constexpr functions for the same purpose

#ifndef __cplusplus
#   define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#   define STRING_LITERAL_LENGTH(str) (ARRAY_SIZE(str) - 1))
#else

#include <array>
#include <stddef.h>

namespace android {
namespace base {

template <class T, size_t N>
static constexpr size_t arraySize(const T (&arr)[N]) {
    return N;
}

template <class T, size_t N>
static constexpr size_t arraySize(const std::array<T, N>& arr) {
    return N;
}

template <size_t N>
static constexpr size_t stringLiteralLength(const char (&str)[N]) {
    return N - 1;
}

}  // namespace base
}  // namespace android

// for those who like macros, define it to be a simple function call
#define ARRAY_SIZE(arr) (::android::base::arraySize(arr))
#define STRING_LITERAL_LENGTH(str) (::android::base::stringLiteralLength(str))

#endif  // __cplusplus
