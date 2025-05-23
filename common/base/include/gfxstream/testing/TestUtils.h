// Copyright 2023 The Android Open Source Project
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

#ifndef BASE_TESTING_TESTUTILS_H_
#define BASE_TESTING_TESTUTILS_H_

#include <gmock/gmock.h>

#include <regex>
#include <string>

// The original gtest MatchesRegex will use different regex implementation on different platforms,
// e.g. on Windows gtest's limited regex engine is used while on Linux Posix ERE is used, which
// could result incompatible syntaxes. See
// https://github.com/google/googletest/blob/main/docs/advanced.md#regular-expression-syntax for
// details. std::regex will by default use the ECMAScript syntax for all platforms, which could fix
// this issue.
MATCHER_P(MatchesStdRegex, regStr, std::string("contains regular expression: ") + regStr) {
    std::regex reg(regStr);
    return std::regex_search(arg, reg);
}

#endif  // BASE_TESTING_TESTUTILS_H_
