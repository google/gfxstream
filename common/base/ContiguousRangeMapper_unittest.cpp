// Copyright 2018 The Android Open Source Project
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

#include "android/base/ContiguousRangeMapper.h"

#include <gtest/gtest.h>

#include <vector>

using android::base::ContiguousRangeMapper;

namespace android {
namespace base {

TEST(ContiguousRangeMapper, Basic) {
    std::vector<uintptr_t> elements = {
        1, 2, 3, 5, 6, 7,
    };

    int numTotalRanges = 0;

    ContiguousRangeMapper rm(
        [&numTotalRanges](uintptr_t start, uintptr_t size) {
            ++numTotalRanges;
        });

    EXPECT_EQ(0, numTotalRanges);

    rm.finish();
    EXPECT_EQ(0, numTotalRanges);

    for (auto elt : elements) {
        rm.add(elt, 1);
    }

    EXPECT_EQ(1, numTotalRanges);

    rm.finish();

    EXPECT_EQ(2, numTotalRanges);

    rm.finish();

    EXPECT_EQ(2, numTotalRanges);
}

TEST(ContiguousRangeMapper, Pages) {
    std::vector<uintptr_t> elements = {
        0x1000,
        0x2000,
        0x3000,
        0x5000,
        0x6000,
        0x7000,
        0xa000,
        0xc000,
    };

    int numTotalRanges = 0;

    ContiguousRangeMapper rm(
        [&numTotalRanges](uintptr_t start, uintptr_t size) {
            ++numTotalRanges;
        });

    for (auto elt : elements) {
        rm.add(elt, 0x1000);
    }

    rm.finish();

    EXPECT_EQ(4, numTotalRanges);
}

TEST(ContiguousRangeMapper, PagesBatched) {
    std::vector<uintptr_t> elements = {
        0x1000,
        0x2000,

        0x3000,

        0x5000,
        0x6000,

        0x7000,

        0xa000,

        0xc000,
    };

    int numTotalRanges = 0;

    ContiguousRangeMapper rm(
        [&numTotalRanges](uintptr_t start, uintptr_t size) {
            ++numTotalRanges;
        }, 0x2000); // 2 page batch

    for (auto elt : elements) {
        rm.add(elt, 0x1000);
    }

    rm.finish();

    EXPECT_EQ(6, numTotalRanges);
}

}  // namespace base
}  // namespace android
