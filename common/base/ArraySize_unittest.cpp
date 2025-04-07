// Copyright 2016 The Android Open Source Project
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
#include "aemu/base/ArraySize.h"

#include <gtest/gtest.h>

#include <vector>

namespace android {
namespace base {

TEST(ArraySize, Sizes) {
    int array1[100];
    EXPECT_EQ(100U, arraySize(array1));
    EXPECT_EQ(100U, ARRAY_SIZE(array1));

    char array2[200];
    EXPECT_EQ(200U, arraySize(array2));
    EXPECT_EQ(200U, ARRAY_SIZE(array2));

    std::array<std::vector<bool>, 15> array3;
    EXPECT_EQ(15U, arraySize(array3));
    EXPECT_EQ(15U, ARRAY_SIZE(array3));
}

TEST(ArraySize, CompileTime) {
    static constexpr int arr[20] = {};
    static_assert(ARRAY_SIZE(arr) == 20U,
                  "Bad ARRAY_SIZE() result in compile time");
    static_assert(arraySize(arr) == 20U,
                  "Bad arraySize() result in compile time");

    static constexpr bool arr2[arraySize(arr)] = {};
    static_assert(arraySize(arr2) == 20U,
                  "Bad size of a new array declared with a result of "
                  "arraySize() call");
    static_assert(arraySize(arr) == ARRAY_SIZE(arr2),
                  "Macro and function are compatible");
}

}  // namespace base
}  // namespace android
