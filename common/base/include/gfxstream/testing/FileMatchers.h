// Copyright 2025 The Android Open Source Project
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

#include <gmock/gmock.h>

#include <codecvt>
#include <filesystem>
#include <locale>
#include <string>

namespace testing {
namespace internal {

/**
 * @brief Converts a std::filesystem::path to a std::string.
 *
 * This function handles the difference between Windows and POSIX path
 * representations, ensuring that the resulting string is UTF-8 encoded.
 *
 * @param path The std::filesystem::path to convert.
 * @return The path as a UTF-8 encoded std::string.
 */
static std::string pathToString(const std::filesystem::path& path) {
#ifdef _WIN32
    std::wstring widePath = path.wstring();
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(widePath);
#else
    return path.string();
#endif
}

/**
 * @brief Normalizes a std::filesystem::path for comparison.
 *
 * This function converts the path to a std::string and replaces backslashes
 * with forward slashes.
 *
 * @note This function will normalize all backslashes to forward slashes.
 *
 * @param path The std::filesystem::path to normalize.
 * @return The normalized path as a std::string.
 */
std::string normalizePath(const std::filesystem::path& path) {
    std::string normalized = pathToString(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

}  // namespace internal

/**
 * @brief A Google Test matcher for comparing std::filesystem::path objects.
 *
 * This matcher normalizes paths before comparison, making it suitable for
 * cross-platform testing.
 *
 * @note This matcher normalizes all backslashes to forward slashes.
 * @note This matcher does not handle case-insensitivity.
 *
 * @param expected The expected std::filesystem::path.
 *
 * @code
 * #include <gtest/gtest.h>
 * #include <gmock/gmock.h>
 * #include <filesystem>
 * #include "aemu/base/testing/FileMatchers.h"
 *
 * TEST(PathMatcherTest, PathEquality) {
 *   std::filesystem::path path1 = "C:\\MyFolder\\file.txt"; // Windows path
 *   std::filesystem::path path2 = "C:/MyFolder/file.txt";   // Windows path (forward slashes)
 *   std::filesystem::path path3 = "/MyFolder/file.txt";     // Posix path
 *   std::filesystem::path path4 = "/MyFolder/other.txt";   // Posix path
 *
 *   EXPECT_THAT(path1, testing::PathEq(path2)); // Should pass (normalized)
 *   EXPECT_THAT(path3, testing::PathEq(path3)); // Should pass (same path)
 *   EXPECT_THAT(path1, testing::Not(testing::PathEq(path3))); // Should pass (different paths)
 *   EXPECT_THAT(path3, testing::Not(testing::PathEq(path4))); // Should pass (different paths)
 * }
 * @endcode
 */
MATCHER_P(PathEq, expected,
          std::string(negation ? "is not equal to " : "is equal to ") +
              PrintToString(internal::normalizePath(expected))) {
    return internal::normalizePath(std::filesystem::path(arg)) ==
           internal::normalizePath(std::filesystem::path(expected));
}

}  // namespace testing
