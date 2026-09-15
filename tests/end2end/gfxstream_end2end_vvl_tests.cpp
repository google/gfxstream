// Copyright (C) 2026 The Android Open Source Project
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

#include <atomic>
#include <thread>

#include "gfxstream_end2end_test_utils.h"
#include "gfxstream_end2end_tests.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/Expected.h"

namespace gfxstream {
namespace tests {
namespace {

using testing::Eq;
using testing::NotNull;

class GfxstreamEnd2EndVvlTest : public GfxstreamEnd2EndTest {
   protected:
    void SetUp() override {
        testing::internal::CaptureStdout();
        testing::internal::CaptureStderr();
        mOutputCaptured = true;
        GfxstreamEnd2EndTest::SetUp();
    }

    void TearDown() override {
        GfxstreamEnd2EndTest::TearDown();
        if (mOutputCaptured) {
            (void)testing::internal::GetCapturedStdout();
            (void)testing::internal::GetCapturedStderr();
            mOutputCaptured = false;
        }
    }

    std::string GetCapturedLogs() {
        if (mOutputCaptured) {
            mOutputCaptured = false;
            return testing::internal::GetCapturedStdout() + testing::internal::GetCapturedStderr();
        }
        return "";
    }

    void DoValidVkOperations() {
        auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
            GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

        const uint32_t width = 32;
        const uint32_t height = 32;
        auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
            *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

        const VkNativeBufferANDROID imageNativeBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID,
            .handle = mGralloc->getNativeHandle(ahb),
        };

        const vkhpp::ImageCreateInfo imageCreateInfo = {
            .pNext = &imageNativeBufferInfo,
            .imageType = vkhpp::ImageType::e2D,
            .format = vkhpp::Format::eR8G8B8A8Unorm,
            .extent =
                {
                    .width = width,
                    .height = height,
                    .depth = 1,
                },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vkhpp::SampleCountFlagBits::e1,
            .tiling = vkhpp::ImageTiling::eOptimal,
            .usage = vkhpp::ImageUsageFlagBits::eSampled | vkhpp::ImageUsageFlagBits::eTransferDst |
                     vkhpp::ImageUsageFlagBits::eTransferSrc,
            .sharingMode = vkhpp::SharingMode::eExclusive,
            .initialLayout = vkhpp::ImageLayout::eUndefined,
        };
        auto image = device->createImageUnique(imageCreateInfo).value;
        ASSERT_TRUE(image.get() != VK_NULL_HANDLE);
    }

   private:
    bool mOutputCaptured = false;
};

TEST_P(GfxstreamEnd2EndVvlTest, BasicValidOperations) {
    DoValidVkOperations();
}

TEST_P(GfxstreamEnd2EndVvlTest, InvalidVkOperationTriggersVvlLog) {
    bool vvlEnabled = false;
    for (const auto& f : GetParam().with_features) {
        if (f.find("VulkanValidation") != std::string::npos) {
            vvlEnabled = true;
            break;
        }
    }

    {
        auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
            GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

        // Perform invalid operations that trigger VVL validation without hitting Mesa assertions:
        // 1. createBuffer with usage = 0 (VUID-VkBufferCreateInfo-usage-requiredbitmask)
        const vkhpp::BufferCreateInfo invalidBufferInfo = {
            .size = 64,
            .usage = {},
        };
        auto bufferRes = device->createBufferUnique(invalidBufferInfo);
        (void)bufferRes;

        // 2. createSampler with maxLod < minLod (VUID-VkSamplerCreateInfo-maxLod-01973)
        const vkhpp::SamplerCreateInfo invalidSamplerInfo = {
            .minLod = 2.0f,
            .maxLod = 1.0f,
        };
        auto samplerRes = device->createSamplerUnique(invalidSamplerInfo);
        (void)samplerRes;
    }

    // Tear down guest connection so kumquat_virtio processes and flushes all pending commands/logs
    TearDownGuest();

    const std::string logs = GetCapturedLogs();

    if (vvlEnabled) {
        EXPECT_NE(logs.find("VVL"), std::string::npos)
            << "Expected VVL prefix in logs when VulkanValidation feature is enabled. Logs:\n"
            << logs;
        EXPECT_NE(logs.find("VUID-"), std::string::npos)
            << "Expected VVL VUID validation error in logs when VulkanValidation feature is enabled. Logs:\n"
            << logs;
    } else {
        EXPECT_EQ(logs.find("VVL"), std::string::npos)
            << "Expected no VVL validation output in logs when VulkanValidation feature is disabled. Logs:\n"
            << logs;
        EXPECT_EQ(logs.find("VUID-"), std::string::npos)
            << "Expected no VVL VUID validation error in logs when VulkanValidation feature is disabled. Logs:\n"
            << logs;
    }
}

INSTANTIATE_TEST_SUITE_P(GfxstreamEnd2EndTests, GfxstreamEnd2EndVvlTest, ::testing::ValuesIn([]() {
                             std::vector<TestParams> cases = {
                                 TestParams{
                                     .with_gl = false,
                                     .with_vk = true,
                                     .with_features = {"MinimalLogging"},
                                     .with_transport = GfxstreamTransport::kVirtioGpuAsg,
                                 },
                                 TestParams{
                                     .with_gl = false,
                                     .with_vk = true,
                                     .with_features = {"MinimalLogging", "VulkanValidation:print"},
                                     .with_transport = GfxstreamTransport::kVirtioGpuAsg,
                                 },
                             };
                             return cases;
                         }()),
                         &GetTestName);

}  // namespace
}  // namespace tests
}  // namespace gfxstream
