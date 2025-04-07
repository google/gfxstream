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

#include <gtest/gtest.h>

namespace android {
namespace base {

TEST(StringFormat, EmptyString) {
    std::string s = StringFormat("");
    EXPECT_TRUE(s.empty());
    EXPECT_STREQ("", s.c_str());
}

TEST(StringFormat, SimpleString) {
    std::string s = StringFormat("foobar");
    EXPECT_STREQ("foobar", s.c_str());
}

TEST(StringFormat, SimpleDecimal) {
    std::string s = StringFormat("Pi is about %d.%d\n", 3, 1415);
    EXPECT_STREQ("Pi is about 3.1415\n", s.c_str());
}

TEST(StringFormat, VeryLongString) {
    static const char kPiece[] = "A hospital bed is a parked taxi with the meter running - Groucho Marx. ";
    const size_t kPieceLen = sizeof(kPiece) - 1U;
    std::string s = StringFormat("%s%s%s%s%s%s%s", kPiece, kPiece, kPiece,
                                 kPiece, kPiece, kPiece, kPiece);
    EXPECT_EQ(7U * kPieceLen, s.size());
    for (size_t n = 0; n < 7U; ++n) {
        std::string s2 = std::string(s.c_str() + n * kPieceLen, kPieceLen);
        EXPECT_STREQ(kPiece, s2.c_str()) << "Index #" << n;
    }
}

TEST(StringAppendFormat, EmptyString) {
    std::string s = "foo";
    StringAppendFormat(&s, "");
    EXPECT_EQ(3U, s.size());
    EXPECT_STREQ("foo", s.c_str());
}

TEST(StringAppendFormat, SimpleString) {
    std::string s = "foo";
    StringAppendFormat(&s, "bar");
    EXPECT_EQ(6U, s.size());
    EXPECT_STREQ("foobar", s.c_str());
}

TEST(StringAppendFormat, VeryLongString) {
    static const char kPiece[] = "A hospital bed is a parked taxi with the meter running - Groucho Marx. ";
    const size_t kPieceLen = sizeof(kPiece) - 1U;
    const size_t kCount = 12;
    std::string s;
    for (size_t n = 0; n < kCount; ++n) {
        StringAppendFormat(&s, "%s", kPiece);
    }

    EXPECT_EQ(kCount * kPieceLen, s.size());
    for (size_t n = 0; n < kCount; ++n) {
        std::string s2 = std::string(s.c_str() + n * kPieceLen, kPieceLen);
        EXPECT_STREQ(kPiece, s2.c_str()) << "Index #" << n;
    }
}

}  // namespace base
}  // namespace android
