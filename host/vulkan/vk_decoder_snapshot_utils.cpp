// Copyright 2024 The Android Open Source Project
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

#include "vulkan/vk_decoder_snapshot_utils.h"

#include "vk_common_operations.h"
#include "vk_utils.h"
#include "vulkan_boxed_handles.h"
#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace host {
namespace vk {

namespace {

uint32_t GetMemoryType(const PhysicalDeviceInfo& physicalDevice,
                       const VkMemoryRequirements& memoryRequirements,
                       VkMemoryPropertyFlags memoryProperties) {
    const auto& props = physicalDevice.memoryPropertiesHelper->getHostMemoryProperties();
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (!(memoryRequirements.memoryTypeBits & (1 << i))) {
            continue;
        }
        if ((props.memoryTypes[i].propertyFlags & memoryProperties) != memoryProperties) {
            continue;
        }
        return i;
    }
    GFXSTREAM_FATAL("Cannot find memory type for snapshot save.");
    return -1;
}

VkExtent3D getMipmapExtent(VkExtent3D baseExtent, uint32_t mipLevel) {
    return VkExtent3D{
        .width = baseExtent.width >> mipLevel,
        .height = baseExtent.height >> mipLevel,
        .depth = baseExtent.depth,
    };
}

constexpr uint32_t kBadImageSnapshot = 0xbaadbeef;
constexpr uint32_t kGoodImageSnapshot = 0x900df00d;
}  // namespace

void saveImageContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkImage image,
                      const ImageInfo* imageInfo) {
    if (imageInfo->layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        stream->putBe32(kBadImageSnapshot);
        return;
    }
    // TODO(b/333936705): snapshot multi-sample images
    if (imageInfo->imageCreateInfoShallow.samples != VK_SAMPLE_COUNT_1_BIT) {
        stream->putBe32(kBadImageSnapshot);
        return;
    }

    VulkanDispatch* dispatch = stateBlock->deviceDispatch;
    const VkImageCreateInfo& imageCreateInfo = imageInfo->imageCreateInfoShallow;

    TransferInfo transferInfo;
    if (!getFormatTransferInfo(imageCreateInfo.format, imageCreateInfo.extent, &transferInfo) ||
        !transferInfo.stagingBufferCopySize) {
        stream->putBe32(kBadImageSnapshot);
        return;
    }
    VkDeviceSize stagingBufferSize = transferInfo.stagingBufferCopySize;

    stream->putBe32(kGoodImageSnapshot);
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = stateBlock->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    VK_CHECK(dispatch->vkAllocateCommandBuffers(stateBlock->device, &allocInfo,
                                                      &commandBuffer));
    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(dispatch->vkCreateFence(stateBlock->device, &fenceCreateInfo, nullptr, &fence));
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = stagingBufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer readbackBuffer;
    VK_CHECK(
        dispatch->vkCreateBuffer(stateBlock->device, &bufferCreateInfo, nullptr, &readbackBuffer));

    VkMemoryRequirements readbackBufferMemoryRequirements{};
    dispatch->vkGetBufferMemoryRequirements(stateBlock->device, readbackBuffer,
                                            &readbackBufferMemoryRequirements);

    const auto readbackBufferMemoryType =
        GetMemoryType(*stateBlock->physicalDeviceInfo, readbackBufferMemoryRequirements,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // Staging memory
    // TODO(b/323064243): reuse staging memory
    VkMemoryAllocateInfo readbackBufferMemoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = readbackBufferMemoryRequirements.size,
        .memoryTypeIndex = readbackBufferMemoryType,
    };
    VkDeviceMemory readbackMemory;
    VK_CHECK(dispatch->vkAllocateMemory(stateBlock->device, &readbackBufferMemoryAllocateInfo,
                                              nullptr, &readbackMemory));
    VK_CHECK(
        dispatch->vkBindBufferMemory(stateBlock->device, readbackBuffer, readbackMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(dispatch->vkMapMemory(stateBlock->device, readbackMemory, 0, VK_WHOLE_SIZE,
                                         VkMemoryMapFlags{}, &mapped));

    for (uint32_t mipLevel = 0; mipLevel < imageInfo->imageCreateInfoShallow.mipLevels;
         mipLevel++) {
        for (uint32_t arrayLayer = 0; arrayLayer < imageInfo->imageCreateInfoShallow.arrayLayers;
             arrayLayer++) {
            // TODO(b/323064243): reuse command buffers
            VkCommandBufferBeginInfo beginInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            if (dispatch->vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
                GFXSTREAM_FATAL("Failed to start command buffer on snapshot save");
            }

            VkExtent3D mipmapExtent = getMipmapExtent(imageCreateInfo.extent, mipLevel);
            if (!getFormatTransferInfo(imageCreateInfo.format, mipmapExtent, &transferInfo)) {
                GFXSTREAM_FATAL("Failed to get transfer info for snapshot save");
            }
            VkDeviceSize mipmapStagingBufferSize = transferInfo.stagingBufferCopySize;
            std::vector<VkBufferImageCopy>& bufferImageCopies = transferInfo.bufferImageCopies;
            VkImageAspectFlags aspects = 0;
            for (const auto& copy : bufferImageCopies) {
                aspects |= copy.imageSubresource.aspectMask;
            }
            VkImageLayout layoutBeforeSave = imageInfo->layout;
            VkImageMemoryBarrier imgMemoryBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = static_cast<VkAccessFlags>(~VK_ACCESS_NONE_KHR),
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = layoutBeforeSave,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = VkImageSubresourceRange{.aspectMask = aspects,
                                                            .baseMipLevel = mipLevel,
                                                            .levelCount = 1,
                                                            .baseArrayLayer = arrayLayer,
                                                            .layerCount = 1}};

            dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                           nullptr, 1, &imgMemoryBarrier);

            for (auto& region : bufferImageCopies) {
                region.imageSubresource.mipLevel = mipLevel;
                region.imageSubresource.baseArrayLayer = arrayLayer;
                dispatch->vkCmdCopyImageToBuffer(commandBuffer, image,
                                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                 readbackBuffer, 1, &region);
            }

            // Cannot really translate it back to VK_IMAGE_LAYOUT_PREINITIALIZED
            if (layoutBeforeSave != VK_IMAGE_LAYOUT_PREINITIALIZED) {
                imgMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                imgMemoryBarrier.newLayout = layoutBeforeSave;
                imgMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                imgMemoryBarrier.dstAccessMask = ~VK_ACCESS_NONE_KHR;
                dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                               nullptr, 1, &imgMemoryBarrier);
            }
            VK_CHECK(dispatch->vkEndCommandBuffer(commandBuffer));

            // Execute the command to copy image
            VkSubmitInfo submitInfo = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &commandBuffer,
            };
            VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
            VK_CHECK(
                dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
            VK_CHECK(dispatch->vkResetFences(stateBlock->device, 1, &fence));
            auto bytes = mipmapStagingBufferSize;
            stream->putBe64(bytes);
            stream->write(mapped, bytes);
        }
    }
    dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
    dispatch->vkUnmapMemory(stateBlock->device, readbackMemory);
    dispatch->vkDestroyBuffer(stateBlock->device, readbackBuffer, nullptr);
    dispatch->vkFreeMemory(stateBlock->device, readbackMemory, nullptr);
    dispatch->vkFreeCommandBuffers(stateBlock->device, stateBlock->commandPool, 1, &commandBuffer);
}

void loadImageContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkImage image,
                      const ImageInfo* imageInfo) {
    const bool validImage = (stream->getBe32() == kGoodImageSnapshot);
    if (!validImage) {
        return;
    }

    VulkanDispatch* dispatch = stateBlock->deviceDispatch;
    const VkImageCreateInfo& imageCreateInfo = imageInfo->imageCreateInfoShallow;

    TransferInfo transferInfo;
    if (!getFormatTransferInfo(imageCreateInfo.format, imageCreateInfo.extent, &transferInfo)) {
        return;
    }
    VkDeviceSize stagingBufferSize = transferInfo.stagingBufferCopySize;

    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = stateBlock->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    VK_CHECK(dispatch->vkAllocateCommandBuffers(stateBlock->device, &allocInfo,
                                                      &commandBuffer));
    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(dispatch->vkCreateFence(stateBlock->device, &fenceCreateInfo, nullptr, &fence));
    if (imageInfo->imageCreateInfoShallow.samples != VK_SAMPLE_COUNT_1_BIT) {
        // Set the layout and quit
        // TODO: resolve and save image content
        getFormatTransferInfo(imageCreateInfo.format, imageCreateInfo.extent, &transferInfo);
        VkImageAspectFlags aspects = 0;
        for (const auto& copy : transferInfo.bufferImageCopies) {
            aspects |= copy.imageSubresource.aspectMask;
        }
        VkImageMemoryBarrier imgMemoryBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = static_cast<VkAccessFlags>(~VK_ACCESS_NONE_KHR),
            .dstAccessMask = static_cast<VkAccessFlags>(~VK_ACCESS_NONE_KHR),
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = imageInfo->layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = VkImageSubresourceRange{.aspectMask = aspects,
                                                        .baseMipLevel = 0,
                                                        .levelCount = VK_REMAINING_MIP_LEVELS,
                                                        .baseArrayLayer = 0,
                                                        .layerCount = VK_REMAINING_ARRAY_LAYERS}};
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        VK_CHECK(dispatch->vkBeginCommandBuffer(commandBuffer, &beginInfo));

        dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                       nullptr, 1, &imgMemoryBarrier);

        VK_CHECK(dispatch->vkEndCommandBuffer(commandBuffer));

        // Execute the command to copy image
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
        };
        VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
        VK_CHECK(
            dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
        dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
        dispatch->vkFreeCommandBuffers(stateBlock->device, stateBlock->commandPool, 1,
                                       &commandBuffer);
        return;
    }
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = stagingBufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer stagingBuffer;
    VK_CHECK(
        dispatch->vkCreateBuffer(stateBlock->device, &bufferCreateInfo, nullptr, &stagingBuffer));

    VkMemoryRequirements stagingBufferMemoryRequirements{};
    dispatch->vkGetBufferMemoryRequirements(stateBlock->device, stagingBuffer,
                                            &stagingBufferMemoryRequirements);

    const auto stagingBufferMemoryType =
        GetMemoryType(*stateBlock->physicalDeviceInfo, stagingBufferMemoryRequirements,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Staging memory
    // TODO(b/323064243): reuse staging memory
    VkMemoryAllocateInfo stagingBufferMemoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingBufferMemoryRequirements.size,
        .memoryTypeIndex = stagingBufferMemoryType,
    };
    VkDeviceMemory stagingMemory;
    VK_CHECK(dispatch->vkAllocateMemory(stateBlock->device, &stagingBufferMemoryAllocateInfo,
                                              nullptr, &stagingMemory));
    VK_CHECK(
        dispatch->vkBindBufferMemory(stateBlock->device, stagingBuffer, stagingMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(dispatch->vkMapMemory(stateBlock->device, stagingMemory, 0, VK_WHOLE_SIZE,
                                         VkMemoryMapFlags{}, &mapped));

    for (uint32_t mipLevel = 0; mipLevel < imageInfo->imageCreateInfoShallow.mipLevels;
         mipLevel++) {
        for (uint32_t arrayLayer = 0; arrayLayer < imageInfo->imageCreateInfoShallow.arrayLayers;
             arrayLayer++) {
            // TODO(b/323064243): reuse command buffers
            VkCommandBufferBeginInfo beginInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            if (dispatch->vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
                GFXSTREAM_FATAL("Failed to start command buffer on snapshot save");
            }

            VkExtent3D mipmapExtent = getMipmapExtent(imageCreateInfo.extent, mipLevel);
            size_t bytes = stream->getBe64();
            stream->read(mapped, bytes);

            if (!getFormatTransferInfo(imageCreateInfo.format, mipmapExtent, &transferInfo)) {
                GFXSTREAM_FATAL("Failed to get transfer info for snapshot load");
            }
            std::vector<VkBufferImageCopy>& bufferImageCopies = transferInfo.bufferImageCopies;
            VkImageAspectFlags aspects = 0;
            for (const auto& copy : bufferImageCopies) {
                aspects |= copy.imageSubresource.aspectMask;
            }
            VkImageMemoryBarrier imgMemoryBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = static_cast<VkAccessFlags>(~VK_ACCESS_NONE_KHR),
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = VkImageSubresourceRange{.aspectMask = aspects,
                                                            .baseMipLevel = mipLevel,
                                                            .levelCount = 1,
                                                            .baseArrayLayer = arrayLayer,
                                                            .layerCount = 1}};

            dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                           nullptr, 1, &imgMemoryBarrier);

            for (auto& region : bufferImageCopies) {
                region.imageSubresource.mipLevel = mipLevel;
                region.imageSubresource.baseArrayLayer = arrayLayer;
                dispatch->vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }

            // Cannot really translate it back to VK_IMAGE_LAYOUT_PREINITIALIZED
            if (imageInfo->layout != VK_IMAGE_LAYOUT_PREINITIALIZED) {
                imgMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imgMemoryBarrier.newLayout = imageInfo->layout;
                imgMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                imgMemoryBarrier.dstAccessMask = ~VK_ACCESS_NONE_KHR;
                dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                               nullptr, 1, &imgMemoryBarrier);
            }
            VK_CHECK(dispatch->vkEndCommandBuffer(commandBuffer));

            // Execute the command to copy image
            VkSubmitInfo submitInfo = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &commandBuffer,
            };
            VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
            VK_CHECK(
                dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
            VK_CHECK(dispatch->vkResetFences(stateBlock->device, 1, &fence));
        }
    }
    dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
    dispatch->vkUnmapMemory(stateBlock->device, stagingMemory);
    dispatch->vkDestroyBuffer(stateBlock->device, stagingBuffer, nullptr);
    dispatch->vkFreeMemory(stateBlock->device, stagingMemory, nullptr);
    dispatch->vkFreeCommandBuffers(stateBlock->device, stateBlock->commandPool, 1, &commandBuffer);
}

void saveBufferContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkBuffer buffer,
                       const BufferInfo* bufferInfo) {
    VkBufferUsageFlags requiredUsages =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if ((bufferInfo->usage & requiredUsages) != requiredUsages) {
        return;
    }
    VulkanDispatch* dispatch = stateBlock->deviceDispatch;
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = stateBlock->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    VK_CHECK(dispatch->vkAllocateCommandBuffers(stateBlock->device, &allocInfo,
                                                      &commandBuffer));
    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(dispatch->vkCreateFence(stateBlock->device, &fenceCreateInfo, nullptr, &fence));
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(bufferInfo->size),
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer readbackBuffer;
    VK_CHECK(
        dispatch->vkCreateBuffer(stateBlock->device, &bufferCreateInfo, nullptr, &readbackBuffer));

    VkMemoryRequirements readbackBufferMemoryRequirements{};
    dispatch->vkGetBufferMemoryRequirements(stateBlock->device, readbackBuffer,
                                            &readbackBufferMemoryRequirements);

    const auto readbackBufferMemoryType =
        GetMemoryType(*stateBlock->physicalDeviceInfo, readbackBufferMemoryRequirements,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // Staging memory
    // TODO(b/323064243): reuse staging memory
    VkMemoryAllocateInfo readbackBufferMemoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = readbackBufferMemoryRequirements.size,
        .memoryTypeIndex = readbackBufferMemoryType,
    };
    VkDeviceMemory readbackMemory;
    VK_CHECK(dispatch->vkAllocateMemory(stateBlock->device, &readbackBufferMemoryAllocateInfo,
                                              nullptr, &readbackMemory));
    VK_CHECK(
        dispatch->vkBindBufferMemory(stateBlock->device, readbackBuffer, readbackMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(dispatch->vkMapMemory(stateBlock->device, readbackMemory, 0, VK_WHOLE_SIZE,
                                         VkMemoryMapFlags{}, &mapped));

    VkBufferCopy bufferCopy = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = bufferInfo->size,
    };

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    if (dispatch->vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        GFXSTREAM_FATAL("Failed to start command buffer on snapshot save");
    }
    dispatch->vkCmdCopyBuffer(commandBuffer, buffer, readbackBuffer, 1, &bufferCopy);
    VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                  .srcQueueFamilyIndex = 0xFFFFFFFF,
                                  .dstQueueFamilyIndex = 0xFFFFFFFF,
                                  .buffer = readbackBuffer,
                                  .offset = 0,
                                  .size = bufferInfo->size};
    dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0,
                                   nullptr);

    // Execute the command to copy buffer
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    VK_CHECK(dispatch->vkEndCommandBuffer(commandBuffer));
    VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
    VK_CHECK(dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
    VK_CHECK(dispatch->vkResetFences(stateBlock->device, 1, &fence));
    stream->putBe64(bufferInfo->size);
    stream->write(mapped, bufferInfo->size);

    dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
    dispatch->vkUnmapMemory(stateBlock->device, readbackMemory);
    dispatch->vkDestroyBuffer(stateBlock->device, readbackBuffer, nullptr);
    dispatch->vkFreeMemory(stateBlock->device, readbackMemory, nullptr);
    dispatch->vkFreeCommandBuffers(stateBlock->device, stateBlock->commandPool, 1, &commandBuffer);
}

void setEventInQueue(StateBlock* stateBlock, VkEvent event, uint64_t eventflags) {
    VulkanDispatch* dispatch = stateBlock->deviceDispatch;
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = stateBlock->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    VK_CHECK(dispatch->vkAllocateCommandBuffers(stateBlock->device, &allocInfo, &commandBuffer));
    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(dispatch->vkCreateFence(stateBlock->device, &fenceCreateInfo, nullptr, &fence));
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    if (dispatch->vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        GFXSTREAM_FATAL("Failed to start command buffer on snapshot load");
    }
    dispatch->vkCmdSetEvent(commandBuffer, event, eventflags);
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    VK_CHECK(dispatch->vkEndCommandBuffer(commandBuffer));
    VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
    VK_CHECK(dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
    VK_CHECK(dispatch->vkResetFences(stateBlock->device, 1, &fence));
    GFXSTREAM_DEBUG("load in queue 0x%llx: event 0x%llx", (unsigned long long)(stateBlock->queue),
                    (unsigned long long)event);

    dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
    dispatch->vkFreeCommandBuffers(stateBlock->device, stateBlock->commandPool, 1, &commandBuffer);
}

void signalSemaphore(StateBlock* stateBlock, VkSemaphore unboxed_semaphore) {
    VulkanDispatch* dispatch = stateBlock->deviceDispatch;
    VkFence fence;
    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(dispatch->vkCreateFence(stateBlock->device, &fenceCreateInfo, nullptr, &fence));
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 0,
        .pCommandBuffers = nullptr,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &unboxed_semaphore,
    };
    VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
    VK_CHECK(dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
    VK_CHECK(dispatch->vkResetFences(stateBlock->device, 1, &fence));
    dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
}

void loadBufferContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkBuffer buffer,
                       const BufferInfo* bufferInfo) {
    VkBufferUsageFlags requiredUsages =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if ((bufferInfo->usage & requiredUsages) != requiredUsages) {
        return;
    }
    VulkanDispatch* dispatch = stateBlock->deviceDispatch;
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = stateBlock->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    VK_CHECK(dispatch->vkAllocateCommandBuffers(stateBlock->device, &allocInfo,
                                                      &commandBuffer));
    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(dispatch->vkCreateFence(stateBlock->device, &fenceCreateInfo, nullptr, &fence));
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(bufferInfo->size),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer stagingBuffer;
    VK_CHECK(
        dispatch->vkCreateBuffer(stateBlock->device, &bufferCreateInfo, nullptr, &stagingBuffer));

    VkMemoryRequirements stagingBufferMemoryRequirements{};
    dispatch->vkGetBufferMemoryRequirements(stateBlock->device, stagingBuffer,
                                            &stagingBufferMemoryRequirements);

    const auto stagingBufferMemoryType =
        GetMemoryType(*stateBlock->physicalDeviceInfo, stagingBufferMemoryRequirements,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // Staging memory
    // TODO(b/323064243): reuse staging memory
    VkMemoryAllocateInfo stagingBufferMemoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingBufferMemoryRequirements.size,
        .memoryTypeIndex = stagingBufferMemoryType,
    };
    VkDeviceMemory stagingMemory;
    VK_CHECK(dispatch->vkAllocateMemory(stateBlock->device, &stagingBufferMemoryAllocateInfo,
                                              nullptr, &stagingMemory));
    VK_CHECK(
        dispatch->vkBindBufferMemory(stateBlock->device, stagingBuffer, stagingMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(dispatch->vkMapMemory(stateBlock->device, stagingMemory, 0, VK_WHOLE_SIZE,
                                         VkMemoryMapFlags{}, &mapped));
    size_t bufferSize = stream->getBe64();
    if (bufferSize != bufferInfo->size) {
        GFXSTREAM_FATAL("Failed to read buffer on snapshot load");
    }
    stream->read(mapped, bufferInfo->size);

    VkBufferCopy bufferCopy = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = bufferInfo->size,
    };

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    if (dispatch->vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        GFXSTREAM_FATAL("Failed to start command buffer on snapshot load");
    }
    dispatch->vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &bufferCopy);
    VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = static_cast<VkAccessFlags>(~VK_ACCESS_NONE_KHR),
                                  .srcQueueFamilyIndex = 0xFFFFFFFF,
                                  .dstQueueFamilyIndex = 0xFFFFFFFF,
                                  .buffer = buffer,
                                  .offset = 0,
                                  .size = bufferInfo->size};
    dispatch->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &barrier,
                                   0, nullptr);

    // Execute the command to copy buffer
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    VK_CHECK(dispatch->vkEndCommandBuffer(commandBuffer));
    VK_CHECK(dispatch->vkQueueSubmit(stateBlock->queue, 1, &submitInfo, fence));
    VK_CHECK(dispatch->vkWaitForFences(stateBlock->device, 1, &fence, VK_TRUE, 3000000000L));
    VK_CHECK(dispatch->vkResetFences(stateBlock->device, 1, &fence));

    dispatch->vkDestroyFence(stateBlock->device, fence, nullptr);
    dispatch->vkUnmapMemory(stateBlock->device, stagingMemory);
    dispatch->vkDestroyBuffer(stateBlock->device, stagingBuffer, nullptr);
    dispatch->vkFreeMemory(stateBlock->device, stagingMemory, nullptr);
    dispatch->vkFreeCommandBuffers(stateBlock->device, stateBlock->commandPool, 1, &commandBuffer);
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
