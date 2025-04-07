/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AEMU_UTIL_STATUS_MATCHER_MACROS_H_
#define AEMU_UTIL_STATUS_MATCHER_MACROS_H_

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"

#include "aemu/base/utils/status_macros.h"

#define EXPECT_OK(expression) EXPECT_THAT(expression, ::absl_testing::IsOk())
#define ASSERT_OK(expression) ASSERT_THAT(expression, ::absl_testing::IsOk())

template <typename T>
void AemuFailIfNotOk(const absl::StatusOr<T>& status_or, const char* file, int line, const char* expr) {
    if (!status_or.ok()) {
        GTEST_MESSAGE_AT_(file, line, 
                          ::absl::StrCat(expr, " returned error: ",
                                         status_or.status().ToString(
                                                 absl::StatusToStringMode::kWithEverything))
                                  .c_str(),
                          ::testing::TestPartResult::kFatalFailure);
    }
}

#define ASSERT_OK_AND_ASSIGN(lhs, rhs)                                                          \
    AEMU_INTERNAL_MACRO_CONCAT(auto status_or, __LINE__) = rhs;                                 \
    AemuFailIfNotOk(AEMU_INTERNAL_MACRO_CONCAT(status_or, __LINE__), __FILE__, __LINE__, #rhs); \
    lhs = std::move(AEMU_INTERNAL_MACRO_CONCAT(status_or, __LINE__).value())

#endif  // AEMU_UTIL_STATUS_MATCHER_MACROS_H_
