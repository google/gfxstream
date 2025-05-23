// Copyright (C) 2024 The Android Open Source Project
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

#include "GfxstreamEnd2EndTestUtils.h"

#include <random>
#include <vector>
namespace gfxstream {
namespace tests {
namespace utils {

using testing::Eq;
using testing::Ge;
using testing::IsEmpty;
using testing::IsNull;
using testing::Not;
using testing::NotNull;

uint32_t getMemoryType(const vkhpp::PhysicalDevice& physicalDevice,
                       const vkhpp::MemoryRequirements& memoryRequirements,
                       vkhpp::MemoryPropertyFlags memoryProperties) {
    const auto props = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (!(memoryRequirements.memoryTypeBits & (1 << i))) {
            continue;
        }
        if ((props.memoryTypes[i].propertyFlags & memoryProperties) != memoryProperties) {
            continue;
        }
        return i;
    }
    return -1;
}

void readImageData(vkhpp::Image image, uint32_t width, uint32_t height,
                   vkhpp::ImageLayout currentLayout, void* dst, uint64_t dstSize,
                   const GfxstreamEnd2EndTest::TypicalVkTestEnvironment& testEnvironment) {
    auto& instance = testEnvironment.instance;
    auto& physicalDevice = testEnvironment.physicalDevice;
    auto& device = testEnvironment.device;
    auto& queue = testEnvironment.queue;
    auto queueFamilyIndex = testEnvironment.queueFamilyIndex;

    // Read-back buffer
    const vkhpp::BufferCreateInfo readbackBufferCreateInfo = {
        .size = static_cast<VkDeviceSize>(dstSize),
        .usage = vkhpp::BufferUsageFlagBits::eTransferDst,
        .sharingMode = vkhpp::SharingMode::eExclusive,
    };
    auto readbackBuffer = device->createBufferUnique(readbackBufferCreateInfo).value;
    ASSERT_THAT(readbackBuffer, IsValidHandle());

    vkhpp::MemoryRequirements readbackBufferMemoryRequirements{};
    device->getBufferMemoryRequirements(*readbackBuffer, &readbackBufferMemoryRequirements);

    const auto readbackBufferMemoryType = getMemoryType(
        physicalDevice, readbackBufferMemoryRequirements,
        vkhpp::MemoryPropertyFlagBits::eHostVisible | vkhpp::MemoryPropertyFlagBits::eHostCoherent);

    // Read-back memory
    const vkhpp::MemoryAllocateInfo readbackBufferMemoryAllocateInfo = {
        .allocationSize = readbackBufferMemoryRequirements.size,
        .memoryTypeIndex = readbackBufferMemoryType,
    };
    auto readbackBufferMemory =
        device->allocateMemoryUnique(readbackBufferMemoryAllocateInfo).value;
    ASSERT_THAT(readbackBufferMemory, IsValidHandle());
    ASSERT_THAT(device->bindBufferMemory(*readbackBuffer, *readbackBufferMemory, 0), IsVkSuccess());

    // Command buffer
    const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
        .queueFamilyIndex = queueFamilyIndex,
    };

    auto commandPool = device->createCommandPoolUnique(commandPoolCreateInfo).value;
    ASSERT_THAT(commandPool, IsValidHandle());
    const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
        .level = vkhpp::CommandBufferLevel::ePrimary,
        .commandPool = *commandPool,
        .commandBufferCount = 1,
    };
    auto readbackCommandBuffers =
        device->allocateCommandBuffersUnique(commandBufferAllocateInfo).value;
    ASSERT_THAT(readbackCommandBuffers, Not(IsEmpty()));
    auto readbackCommandBuffer = std::move(readbackCommandBuffers[0]);
    ASSERT_THAT(readbackCommandBuffer, IsValidHandle());

    const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
        .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    readbackCommandBuffer->begin(commandBufferBeginInfo);
    const vkhpp::ImageMemoryBarrier readbackBarrier{
        .oldLayout = currentLayout,
        .newLayout = vkhpp::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
    };

    readbackCommandBuffer->pipelineBarrier(
        vkhpp::PipelineStageFlagBits::eAllCommands, vkhpp::PipelineStageFlagBits::eAllCommands,
        vkhpp::DependencyFlags(), nullptr, nullptr, readbackBarrier);

    const vkhpp::BufferImageCopy bufferImageCopy = {
        .imageSubresource =
            {
                .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                .layerCount = 1,
            },
        .imageExtent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
    };
    readbackCommandBuffer->copyImageToBuffer(image, vkhpp::ImageLayout::eTransferSrcOptimal,
                                             *readbackBuffer, 1, &bufferImageCopy);

    const vkhpp::ImageMemoryBarrier restoreBarrier{
        .oldLayout = vkhpp::ImageLayout::eTransferSrcOptimal,
        .newLayout = currentLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            },
    };

    readbackCommandBuffer->pipelineBarrier(
        vkhpp::PipelineStageFlagBits::eAllCommands, vkhpp::PipelineStageFlagBits::eAllCommands,
        vkhpp::DependencyFlags(), nullptr, nullptr, restoreBarrier);

    readbackCommandBuffer->end();

    auto readbackFence = device->createFenceUnique(vkhpp::FenceCreateInfo()).value;
    ASSERT_THAT(readbackCommandBuffer, IsValidHandle());

    // Execute the command to copy image back to buffer
    const vkhpp::SubmitInfo readbackSubmitInfo = {
        .commandBufferCount = 1,
        .pCommandBuffers = &readbackCommandBuffer.get(),
    };
    queue.submit(readbackSubmitInfo, *readbackFence);

    auto readbackWaitResult = device->waitForFences(*readbackFence, VK_TRUE, 3000000000L);
    ASSERT_THAT(readbackWaitResult, IsVkSuccess());

    // Verify content
    void* mapped;
    auto mapResult = device->mapMemory(*readbackBufferMemory, 0, VK_WHOLE_SIZE,
                                       vkhpp::MemoryMapFlags{}, &mapped);
    ASSERT_THAT(mapResult, IsVkSuccess());
    ASSERT_THAT(mapped, NotNull());
    memcpy(dst, mapped, dstSize);
    device->unmapMemory(*readbackBufferMemory);
}

std::vector<uint8_t> getRandomNByteData(size_t len) {
    std::vector<uint8_t> randomData(len);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);  // Generate values from 0 to 255
    for (auto& byte : randomData) {
        byte = static_cast<uint8_t>(distrib(gen));
    }
    // std::vector is move capable so optimal to return by value.
    return randomData;
}

}  // namespace utils
}  // namespace tests
}  // namespace gfxstream