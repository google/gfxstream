// Copyright (C) 2025 The Android Open Source Project
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

#include "gfxstream_end2end_tests.h"

namespace gfxstream {
namespace tests {
namespace {

using ::testing::Eq;

class GfxstreamEnd2EndGrallocTests : public GfxstreamEnd2EndTest {};

TEST_P(GfxstreamEnd2EndGrallocTests, Allocate_RGBA8888) {
    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, 32, 32, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));
}

TEST_P(GfxstreamEnd2EndGrallocTests, Allocate_YV12) {
    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, 32, 32, GFXSTREAM_AHB_FORMAT_YV12));
}

TEST_P(GfxstreamEnd2EndGrallocTests, AllocateTransfer_RGBA8888) {
    constexpr const uint32_t kWidth = 32;
    constexpr const uint32_t kHeight = 32;

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, kWidth, kHeight, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    const PixelR8G8B8A8 color = PixelR8G8B8A8(11, 22, 33, 44);

    constexpr const uint32_t kBpp = 4;
    constexpr const uint32_t kRowStrideBytes = kBpp * kWidth;
    {
        uint8_t* ahbBytes = GFXSTREAM_ASSERT(ahb.Lock());
        for (uint32_t y = 0; y < kHeight; y++) {
            for (uint32_t x = 0; x < kWidth; x++) {
                uint8_t* dst = ahbBytes + (y * kRowStrideBytes) + (x * kBpp);
                dst[0] = color.r;
                dst[1] = color.g;
                dst[2] = color.b;
                dst[3] = color.a;
            }
        }
        ahb.Unlock();
    }
    {
        const uint8_t* ahbBytes = GFXSTREAM_ASSERT(ahb.Lock());
        for (uint32_t y = 0; y < kHeight; y++) {
            for (uint32_t x = 0; x < kWidth; x++) {
                const uint8_t* src = ahbBytes + (y * kRowStrideBytes) + (x * kBpp);
                ASSERT_THAT(src[0], Eq(color.r));
                ASSERT_THAT(src[1], Eq(color.g));
                ASSERT_THAT(src[2], Eq(color.b));
                ASSERT_THAT(src[3], Eq(color.a));
            }
        }
        ahb.Unlock();
    }
}

TEST_P(GfxstreamEnd2EndGrallocTests, AllocateTransfer_YV12) {
    constexpr const uint32_t kWidth = 32;
    constexpr const uint32_t kHeight = 32;

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, kWidth, kHeight, GFXSTREAM_AHB_FORMAT_YV12));

    const PixelR8G8B8A8 color = PixelR8G8B8A8(11, 22, 33, 44);

    uint8_t colorY;
    uint8_t colorU;
    uint8_t colorV;
    RGBToYUV(color.r, color.g, color.b, &colorY, &colorU, &colorV);

    {
        std::vector<Gralloc::LockedPlane> ahbPlanes =
            GFXSTREAM_ASSERT(ahb.LockPlanes());
        const Gralloc::LockedPlane& yPlane = ahbPlanes[0];
        const Gralloc::LockedPlane& uPlane = ahbPlanes[1];
        const Gralloc::LockedPlane& vPlane = ahbPlanes[2];

        for (uint32_t y = 0; y < kHeight; y++) {
            for (uint32_t x = 0; x < kWidth; x++) {
                uint8_t* dstY = yPlane.data +
                                (y * yPlane.rowStrideBytes) +
                                (x * yPlane.pixelStrideBytes);
                *dstY = colorY;
            }
        }
        for (uint32_t y = 0; y < kHeight / 2; y++) {
            for (uint32_t x = 0; x < kWidth / 2; x++) {
                uint8_t* dstU = uPlane.data +
                                (y * uPlane.rowStrideBytes) +
                                (x * uPlane.pixelStrideBytes);
                uint8_t* dstV = vPlane.data +
                                (y * vPlane.rowStrideBytes) +
                                (x * vPlane.pixelStrideBytes);
                *dstU = colorU;
                *dstV = colorV;
            }
        }

        ahb.Unlock();
    }
    {
        std::vector<Gralloc::LockedPlane> ahbPlanes =
            GFXSTREAM_ASSERT(ahb.LockPlanes());
        const Gralloc::LockedPlane& yPlane = ahbPlanes[0];
        const Gralloc::LockedPlane& uPlane = ahbPlanes[1];
        const Gralloc::LockedPlane& vPlane = ahbPlanes[2];

        for (uint32_t y = 0; y < kHeight; y++) {
            for (uint32_t x = 0; x < kWidth; x++) {
                const uint8_t* actualY = yPlane.data +
                                     (y * yPlane.rowStrideBytes) +
                                     (x * yPlane.pixelStrideBytes);
                ASSERT_THAT(*actualY, Eq(colorY));
            }
        }
        for (uint32_t y = 0; y < kHeight / 2; y++) {
            for (uint32_t x = 0; x < kWidth / 2; x++) {
                const uint8_t* actualU = uPlane.data +
                                        (y * uPlane.rowStrideBytes) +
                                        (x * uPlane.pixelStrideBytes);
                const uint8_t* actualV = vPlane.data +
                                        (y * vPlane.rowStrideBytes) +
                                        (x * vPlane.pixelStrideBytes);
                ASSERT_THAT(*actualU, Eq(colorU));
                ASSERT_THAT(*actualV, Eq(colorV));
            }
        }

        ahb.Unlock();
    }
}

INSTANTIATE_TEST_SUITE_P(GfxstreamEnd2EndTests, GfxstreamEnd2EndGrallocTests,
                         ::testing::ValuesIn({
                             TestParams{
                                 .with_gl = true,
                                 .with_vk = false,
                                 .with_features = {"MinimalLogging"},
                             },
                             TestParams{
                                 .with_gl = false,
                                 .with_vk = true,
                                 .with_features = {"MinimalLogging"},
                             },
                             TestParams{
                                 .with_gl = true,
                                 .with_vk = true,
                                 .with_features = {"MinimalLogging"},
                             },
                         }),
                         &GetTestName);

}  // namespace
}  // namespace tests
}  // namespace gfxstream
