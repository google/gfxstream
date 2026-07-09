// Copyright 2026 The Android Open Source Project
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

#include "vulkan/vk_common_operations.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/host/features.h"
#include "vulkan/vk_decoder_global_state.h"
#include "vulkan/vulkan_dispatch.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

// Brings up a real VkEmulation against the bundled software Vulkan driver and exercises the
// guest-controlled offset/size validation in the buffer and color-buffer transfer paths. The guest
// controls the offset and size of transfers, so an out-of-range request must be rejected rather
// than reading or writing past the allocation.
class VkEmulationBufferTransferTest : public ::testing::Test {
   protected:
    static void SetUpTestSuite() {
        ASSERT_TRUE(gfxstream::testing::SetupGraphicsTestEnvironment())
            << "Failed to configure graphics test environment";
    }

    void SetUp() override {
        VulkanDispatch* vk =
            vkDispatch(!gfxstream::testing::IsGraphicsTestEnvironmentProvidingVulkanDriver());
        ASSERT_NE(vk, nullptr);

        gfxstream::host::FeatureSet features;
        // Needed so that host-visible coherent memory is available for the staging buffer.
        features.GlDirectMem.setEnabled(true);

        mVkEmu = VkEmulation::create(vk, {}, features);
        ASSERT_NE(mVkEmu, nullptr);

        // The color buffer update path consults VkDecoderGlobalState; initialize it as
        // FrameBuffer does.
        VkDecoderGlobalState::initialize(mVkEmu.get());
    }

    void TearDown() override {
        VkDecoderGlobalState::reset();
        mVkEmu.reset();
    }

    std::unique_ptr<VkEmulation> mVkEmu;
};

TEST_F(VkEmulationBufferTransferTest, RejectsOutOfRangeTransfers) {
    constexpr uint64_t kBufferSize = 4096;
    constexpr uint32_t kBufferHandle = 1;
    ASSERT_TRUE(mVkEmu->setupVkBuffer(kBufferSize, kBufferHandle, /*vulkanOnly=*/true));

    std::vector<uint8_t> scratch(kBufferSize, 0xab);

    // Offset entirely past the end of the buffer.
    EXPECT_FALSE(mVkEmu->readBufferToBytes(kBufferHandle, kBufferSize + 1, 1, scratch.data()));
    EXPECT_FALSE(mVkEmu->updateBufferFromBytes(kBufferHandle, kBufferSize + 1, 1, scratch.data()));

    // Offset exactly at the end, with a non-zero size.
    EXPECT_FALSE(mVkEmu->readBufferToBytes(kBufferHandle, kBufferSize, 1, scratch.data()));

    // In-bounds offset, but size runs off the end.
    EXPECT_FALSE(mVkEmu->readBufferToBytes(kBufferHandle, 1, kBufferSize, scratch.data()));
    EXPECT_FALSE(mVkEmu->updateBufferFromBytes(kBufferHandle, 0, kBufferSize + 1, scratch.data()));

    // offset + size would overflow if added naively; the subtraction-based check must still reject.
    EXPECT_FALSE(mVkEmu->readBufferToBytes(kBufferHandle, kBufferSize,
                                           ~static_cast<uint64_t>(0), scratch.data()));

    mVkEmu->teardownVkBuffer(kBufferHandle);
}

TEST_F(VkEmulationBufferTransferTest, AllowsInRangeTransfers) {
    constexpr uint64_t kBufferSize = 4096;
    constexpr uint32_t kBufferHandle = 2;
    ASSERT_TRUE(mVkEmu->setupVkBuffer(kBufferSize, kBufferHandle, /*vulkanOnly=*/true));

    std::vector<uint8_t> scratch(kBufferSize, 0xcd);

    // Whole-buffer and sub-range transfers within bounds must be accepted.
    EXPECT_TRUE(mVkEmu->updateBufferFromBytes(kBufferHandle, 0, kBufferSize, scratch.data()));
    EXPECT_TRUE(mVkEmu->readBufferToBytes(kBufferHandle, 0, kBufferSize, scratch.data()));
    EXPECT_TRUE(mVkEmu->readBufferToBytes(kBufferHandle, kBufferSize / 2, kBufferSize / 2,
                                          scratch.data()));

    mVkEmu->teardownVkBuffer(kBufferHandle);
}

TEST_F(VkEmulationBufferTransferTest, RejectsUndersizedDepthStencilColorBufferTransfers) {
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 8;
    constexpr uint32_t kColorBufferHandle = 3;
    ASSERT_TRUE(mVkEmu->createVkColorBuffer(kWidth, kHeight, GfxstreamFormat::D32_FLOAT_S8_UINT,
                                            kColorBufferHandle, /*vulkanOnly=*/true,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, /*mipLevels=*/1));

    std::vector<uint8_t> full;
    ASSERT_TRUE(mVkEmu->readColorBufferToBytes(kColorBufferHandle, &full));
    ASSERT_GT(full.size(), 1u);

    std::vector<uint8_t> tooSmall(full.size() - 1, 0);

    EXPECT_FALSE(mVkEmu->readColorBufferToBytes(kColorBufferHandle, 0, 0, kWidth, kHeight,
                                                tooSmall.data(), tooSmall.size()));

    EXPECT_FALSE(mVkEmu->updateColorBufferFromBytes(kColorBufferHandle, tooSmall));

    mVkEmu->teardownVkColorBuffer(kColorBufferHandle);
}

// The packed depth/stencil formats share the plane pack/unpack machinery; iterate over
// the subset the driver supports.
constexpr GfxstreamFormat kPackedDepthStencilFormats[] = {
    GfxstreamFormat::D24_UNORM_S8_UINT,
    GfxstreamFormat::D32_FLOAT_S8_UINT,
};

TEST_F(VkEmulationBufferTransferTest, RoundTripsDepthStencilColorBufferContents) {
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 8;

    uint32_t nextColorBufferHandle = 9;
    size_t testedFormats = 0;
    for (const GfxstreamFormat format : kPackedDepthStencilFormats) {
        SCOPED_TRACE(ToString(format));
        if (!mVkEmu->isFormatSupported(format)) {
            continue;
        }
        ++testedFormats;

        const uint32_t colorBufferHandle = nextColorBufferHandle++;
        ASSERT_TRUE(mVkEmu->createVkColorBuffer(kWidth, kHeight, format, colorBufferHandle,
                                                /*vulkanOnly=*/true,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                /*mipLevels=*/1));

        std::vector<uint8_t> full;
        ASSERT_TRUE(mVkEmu->readColorBufferToBytes(colorBufferHandle, &full));

        // Update packs interleaved pixels into per-aspect planes and read unpacks them
        // back; they must be exact inverses. Float depth must hold in-range [0.0, 1.0]
        // values (out-of-range is undefined; observed zeroed on RADV) that are exact
        // binary fractions so readback is bit-identical; 24-bit UNORM depth accepts any
        // bit pattern. The i * 17 + 3 byte sequence keeps adjacent bytes distinct and
        // repeats only every 256 bytes, so shifted or dropped bytes fail the comparison.
        std::vector<uint8_t> pattern(full.size());
        if (format == GfxstreamFormat::D32_FLOAT_S8_UINT) {
            // Interleaved D32S8 pixel: 4-byte float depth, then 1 stencil byte.
            ASSERT_EQ(pattern.size() % 5, 0u);
            const size_t pixelCount = pattern.size() / 5;
            for (size_t i = 0; i < pixelCount; ++i) {
                const float depth = static_cast<float>(i) / static_cast<float>(pixelCount);
                std::memcpy(&pattern[i * 5], &depth, sizeof(depth));
                pattern[i * 5 + 4] = static_cast<uint8_t>(i * 17 + 3);  // stencil byte
            }
        } else {
            // Interleaved D24S8 pixels are 4 bytes each. Whole-buffer reads return the
            // larger staging-sized buffer, whose bytes past the interleaved pixels do
            // not round-trip (they read back as zero), so leave them zero.
            const size_t pixelBytes = kWidth * kHeight * 4;
            ASSERT_LE(pixelBytes, pattern.size());
            for (size_t i = 0; i < pixelBytes; ++i) {
                pattern[i] = static_cast<uint8_t>(i * 17 + 3);
            }
        }
        ASSERT_TRUE(mVkEmu->updateColorBufferFromBytes(colorBufferHandle, pattern));

        std::vector<uint8_t> readback;
        ASSERT_TRUE(mVkEmu->readColorBufferToBytes(colorBufferHandle, &readback));
        EXPECT_EQ(readback, pattern);

        mVkEmu->teardownVkColorBuffer(colorBufferHandle);
    }

    if (testedFormats == 0) {
        GTEST_SKIP() << "No packed depth/stencil format supported by this driver";
    }
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
