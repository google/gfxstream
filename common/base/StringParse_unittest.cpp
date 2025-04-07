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

#include "android/base/StringParse.h"

#include "android/base/threads/FunctorThread.h"
#include "aemu/base/testing/Utils.h"
#include <gtest/gtest.h>

namespace android {
namespace base {

static void testScanf() {
    static const char comma[] = "1,3";
    static const char dot[] = "1.3";
    static const char format[] = "%f%n";
    float val = 0;
    int n = 0;

    // Now make sure we parse floating point strings as expected.
    EXPECT_EQ(1, sscanf(dot, format, &val, &n));
    EXPECT_FLOAT_EQ(1, val);
    EXPECT_EQ(1, n);
    EXPECT_EQ(1, sscanf(comma, format, &val, &n));
    EXPECT_FLOAT_EQ(1.3, val);
    EXPECT_EQ(3, n);

    // C-Locale parsing should be able to parse strings with C locale
    EXPECT_EQ(1, SscanfWithCLocale(dot, format, &val, &n));
    EXPECT_FLOAT_EQ(1.3, val);
    EXPECT_EQ(3, n);

    // And the regular parsing still works as it used to.
    EXPECT_EQ(1, sscanf(dot, format, &val, &n));
    EXPECT_FLOAT_EQ(1, val);
    EXPECT_EQ(1, n);
    EXPECT_EQ(1, sscanf(comma, format, &val, &n));
    EXPECT_FLOAT_EQ(1.3, val);
    EXPECT_EQ(3, n);
}

// These are flaky as they depend on the build env.
TEST(StringParse, DISABLED_SscanfWithCLocale) {
    auto scopedCommaLocale = setScopedCommaLocale();
    testScanf();
}

TEST(StringParse, DISABLED_SscanfWithCLocaleThreads) {
    auto scopedCommaLocale = setScopedCommaLocale();

    std::vector<std::unique_ptr<FunctorThread>> threads;
    for (int i = 0; i < 20; ++i) {
        threads.emplace_back(new FunctorThread(&testScanf));
    }
    for (auto& t : threads) {
        ASSERT_TRUE(t->start());
    }
    for (auto& t : threads) {
        ASSERT_TRUE(t->wait());
    }
}

}  // namespace base
}  // namespace android
