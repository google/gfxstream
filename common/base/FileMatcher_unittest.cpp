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
#include "aemu/base/testing/FileMatchers.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>

namespace testing {
namespace {

using ::testing::Not;

TEST(PathEqTest, SamePaths) {
    std::filesystem::path path1 = "C:\\MyFolder\\file.txt";
    std::filesystem::path path2 = "C:\\MyFolder\\file.txt";
    EXPECT_THAT(path1, PathEq(path2));
}

TEST(PathEqTest, SamePathsForwardSlash) {
    std::filesystem::path path1 = "C:/MyFolder/file.txt";
    std::filesystem::path path2 = "C:/MyFolder/file.txt";
    EXPECT_THAT(path1, PathEq(path2));
}

TEST(PathEqTest, WindowsBackslashForwardSlash) {
    std::filesystem::path path1 = "C:\\MyFolder\\file.txt";
    std::filesystem::path path2 = "C:/MyFolder/file.txt";
    EXPECT_THAT(path1, PathEq(path2));
}


TEST(PathEqTest, PosixPaths) {
    std::filesystem::path path1 = "/MyFolder/file.txt";
    std::filesystem::path path2 = "/MyFolder/file.txt";
    EXPECT_THAT(path1, PathEq(path2));
}

TEST(PathEqTest, DifferentPaths) {
    std::filesystem::path path1 = "C:\\MyFolder\\file.txt";
    std::filesystem::path path2 = "C:\\MyFolder\\other.txt";
    EXPECT_THAT(path1, Not(PathEq(path2)));
}

TEST(PathEqTest, WindowsPosixDifferentPaths) {
    std::filesystem::path path1 = "C:\\MyFolder\\file.txt";
    std::filesystem::path path2 = "/MyFolder/file.txt";
    EXPECT_THAT(path1, Not(PathEq(path2)));
}

TEST(PathEqTest, UnicodePaths) {
    std::filesystem::path path1 = L"C:\\MyFolder\\你好.txt";
    std::filesystem::path path2 = L"C:/MyFolder/你好.txt";
    EXPECT_THAT(path1, PathEq(path2));
}


TEST(PathEqTest, UnicodePathsDifferent) {
    std::filesystem::path path1 = L"C:\\MyFolder\\你好.txt";
    std::filesystem::path path2 = L"C:/MyFolder/再见.txt";
    EXPECT_THAT(path1, Not(PathEq(path2)));
}

TEST(PathEqTest, EmptyPaths) {
    std::filesystem::path path1 = "";
    std::filesystem::path path2 = "";
    EXPECT_THAT(path1, PathEq(path2));
}

TEST(PathEqTest, EmptyPathAndNonEmptyPath) {
    std::filesystem::path path1 = "";
    std::filesystem::path path2 = "C:\\MyFolder\\file.txt";
    EXPECT_THAT(path1, Not(PathEq(path2)));
}

TEST(PathEqTest, RelativePaths) {
    std::filesystem::path path1 = "MyFolder/file.txt";
    std::filesystem::path path2 = "MyFolder/file.txt";
    EXPECT_THAT(path1, PathEq(path2));
}

TEST(PathEqTest, RelativePathsDifferent) {
    std::filesystem::path path1 = "MyFolder/file.txt";
    std::filesystem::path path2 = "MyFolder/other.txt";
    EXPECT_THAT(path1, Not(PathEq(path2)));
}

TEST(PathEqTest, RelativePathsWindows) {
    std::filesystem::path path1 = "MyFolder\\file.txt";
    std::filesystem::path path2 = "MyFolder/file.txt";
    EXPECT_THAT(path1, PathEq(path2));
}

TEST(PathEqTest, CanUseStrings) {
    std::filesystem::path path1 = L"C:\\MyFolder\\你好.txt";
    EXPECT_THAT(path1, Not(PathEq(L"C:/MyFolder/再见.txt")));
}


}  // namespace
}  // namespace testing
