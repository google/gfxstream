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
#include <memory>
#include <vector>

#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/host/features.h"
#include "vulkan/vulkan_dispatch.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

// Brings up a real VkEmulation against the bundled software Vulkan driver and exercises the
// guest-controlled offset/size validation in readBufferToBytes()/updateBufferFromBytes(). The
// guest controls the offset and size of buffer transfers, so an out-of-range request must be
// rejected rather than reading or writing past the buffer.
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
    }

    void TearDown() override { mVkEmu.reset(); }

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

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
