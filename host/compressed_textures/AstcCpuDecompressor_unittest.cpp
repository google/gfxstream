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

#include <gmock/gmock.h>

#include "gfxstream/host/AstcCpuDecompressor.h"

namespace gfxstream {
namespace vk {
namespace {

using ::testing::ElementsAreArray;
using ::testing::NotNull;

// 16x16 checkerboard pattern, compressed with 8x8 block size.
const uint8_t kCheckerboard[] = {
    0x44, 0x05, 0x00, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
    0x44, 0x05, 0x00, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
    0x44, 0x05, 0x00, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
    0x44, 0x05, 0x00, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa};

struct Rgba {
    uint8_t r, g, b, a;
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

TEST(AstcCpuDecompressor, Decompress) {
    auto& decompressor = AstcCpuDecompressor::get();
    if (!decompressor.available()) GTEST_SKIP() << "ASTC decompressor not available";

    std::vector<Rgba> output(16 * 16);
    int32_t status = decompressor.decompress(16, 16, 8, 8, kCheckerboard, sizeof(kCheckerboard),
                                             (uint8_t*)output.data());
    EXPECT_EQ(status, 0);

    const Rgba W = {0xFF, 0xFF, 0xFF, 0xFF};
    const Rgba B = {0, 0, 0, 0xFF};

    std::vector<Rgba> expected = {
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 0
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 1
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 2
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 3
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 4
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 5
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 6
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 7
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 8
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 9
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 10
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 11
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 12
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 13
        W, B, W, B, W, B, W, B, W, B, W, B, W, B, W, B,  // 14
        B, W, B, W, B, W, B, W, B, W, B, W, B, W, B, W,  // 15
    };

    ASSERT_THAT(output, ElementsAreArray(expected));
}

TEST(AstcCpuDecompressor, getStatusStringAlwaysNonNull) {
    EXPECT_THAT(AstcCpuDecompressor::get().getStatusString(-10000), NotNull());
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream