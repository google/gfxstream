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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gfxstream/BumpPool.h"
#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/host/features.h"
#include "gfxstream/host/mem_stream.h"
#include "gfxstream/system/System.h"
#include "render_thread_info_vk.h"
#include "vk_common_operations.h"
#include "vk_decoder_global_state.h"
#include "vk_decoder_internal_structs.h"
#include "vk_decoder_snapshot.h"
#include "vk_decoder_snapshot_utils.h"
#include "vk_emulated_physical_device_memory.h"
#include "vk_format_utils.h"
#include "vk_utils.h"
#include "vulkan_boxed_handles.h"
#include "vulkan_dispatch.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

VkExtent3D GetMipExtent(VkExtent3D baseExtent, uint32_t mipLevel) {
    return VkExtent3D{
        .width = std::max<uint32_t>(baseExtent.width >> mipLevel, 1u),
        .height = std::max<uint32_t>(baseExtent.height >> mipLevel, 1u),
        .depth = std::max<uint32_t>(baseExtent.depth >> mipLevel, 1u),
    };
}

class VkDecoderSnapshotUtilsTest : public ::testing::Test {
   protected:
    static void SetUpTestSuite() {
        ASSERT_TRUE(gfxstream::testing::SetupGraphicsTestEnvironment())
            << "Failed to configure graphics test environment";
        sVk = vkDispatch(!gfxstream::testing::IsGraphicsTestEnvironmentProvidingVulkanDriver());
    }

    void SetUp() override {
        ASSERT_NE(sVk, nullptr);
        ASSERT_NE(sVk->vkCreateInstance, nullptr);

        createInstance();
        pickPhysicalDevice();
        createLogicalDevice();

        const VkCommandPoolCreateInfo commandPoolCi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = mQueueFamilyIndex,
        };
        ASSERT_EQ(
            mDeviceDispatch.vkCreateCommandPool(mDevice, &commandPoolCi, nullptr, &mCommandPool),
            VK_SUCCESS);

        mDeviceDispatch.vkGetDeviceQueue(mDevice, mQueueFamilyIndex, 0, &mQueue);
        ASSERT_NE(mQueue, VK_NULL_HANDLE);

        VkPhysicalDeviceMemoryProperties hostMemProps = {};
        mInstanceDispatch.vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &hostMemProps);
        gfxstream::host::FeatureSet features;
        features.GlDirectMem.setEnabled(true);

        mPhysicalDeviceInfo.instance = mInstance;
        mInstanceDispatch.vkGetPhysicalDeviceProperties(mPhysicalDevice,
                                                        &mPhysicalDeviceInfo.props);
        mPhysicalDeviceInfo.memoryPropertiesHelper =
            std::make_unique<EmulatedPhysicalDeviceMemoryProperties>(hostMemProps, 0, features);

        mStateBlock = StateBlock{
            .physicalDevice = mPhysicalDevice,
            .physicalDeviceInfo = &mPhysicalDeviceInfo,
            .device = mDevice,
            .deviceDispatch = &mDeviceDispatch,
            .queue = mQueue,
            .commandPool = mCommandPool,
        };
    }

    void TearDown() override {
        if (mDevice != VK_NULL_HANDLE) {
            mDeviceDispatch.vkDeviceWaitIdle(mDevice);
            if (mCommandPool != VK_NULL_HANDLE) {
                mDeviceDispatch.vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
                mCommandPool = VK_NULL_HANDLE;
            }
            mDeviceDispatch.vkDestroyDevice(mDevice, nullptr);
            mDevice = VK_NULL_HANDLE;
        }
        if (mInstance != VK_NULL_HANDLE) {
            mInstanceDispatch.vkDestroyInstance(mInstance, nullptr);
            mInstance = VK_NULL_HANDLE;
        }
    }

    struct AllocatedImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        ImageInfo info = {};
    };

    struct AllocatedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        BufferInfo info = {};
        void* mapped = nullptr;
    };

    bool findMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags properties,
                        uint32_t* outTypeIndex) const {
        const auto& props = mPhysicalDeviceInfo.memoryPropertiesHelper->getHostMemoryProperties();
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & properties) == properties) {
                *outTypeIndex = i;
                return true;
            }
        }
        return false;
    }

    bool isImageConfigSupported(VkFormat format, VkImageType imageType, VkImageTiling tiling,
                                VkImageUsageFlags usage, VkImageCreateFlags flags,
                                VkExtent3D extent, uint32_t mipLevels,
                                uint32_t arrayLayers) const {
        VkImageFormatProperties props = {};
        VkResult res = mInstanceDispatch.vkGetPhysicalDeviceImageFormatProperties(
            mPhysicalDevice, format, imageType, tiling, usage, flags, &props);
        if (res != VK_SUCCESS) {
            return false;
        }
        if (extent.width > props.maxExtent.width || extent.height > props.maxExtent.height ||
            extent.depth > props.maxExtent.depth) {
            return false;
        }
        if (mipLevels > props.maxMipLevels || arrayLayers > props.maxArrayLayers) {
            return false;
        }
        return true;
    }

    void createTestImage(const VkImageCreateInfo& imageCi, VkImageLayout layout,
                         AllocatedImage* outImage) {
        ASSERT_EQ(mDeviceDispatch.vkCreateImage(mDevice, &imageCi, nullptr, &outImage->image),
                  VK_SUCCESS);
        ASSERT_NE(outImage->image, VK_NULL_HANDLE);

        VkMemoryRequirements memReqs = {};
        mDeviceDispatch.vkGetImageMemoryRequirements(mDevice, outImage->image, &memReqs);

        uint32_t memTypeIndex = 0;
        ASSERT_TRUE(findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   &memTypeIndex));

        const VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = memTypeIndex,
        };
        ASSERT_EQ(mDeviceDispatch.vkAllocateMemory(mDevice, &allocInfo, nullptr, &outImage->memory),
                  VK_SUCCESS);
        ASSERT_EQ(mDeviceDispatch.vkBindImageMemory(mDevice, outImage->image, outImage->memory, 0),
                  VK_SUCCESS);

        outImage->info.device = mDevice;
        outImage->info.imageCreateInfoShallow = vk_make_orphan_copy(imageCi);
        outImage->info.layout = layout;
        outImage->info.memory = outImage->memory;
    }

    void destroyTestImage(AllocatedImage* img) {
        if (img->image != VK_NULL_HANDLE) {
            mDeviceDispatch.vkDestroyImage(mDevice, img->image, nullptr);
            img->image = VK_NULL_HANDLE;
        }
        if (img->memory != VK_NULL_HANDLE) {
            mDeviceDispatch.vkFreeMemory(mDevice, img->memory, nullptr);
            img->memory = VK_NULL_HANDLE;
        }
    }

    void createStagingBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             AllocatedBuffer* outBuffer) {
        const VkBufferCreateInfo bufferCi = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        ASSERT_EQ(mDeviceDispatch.vkCreateBuffer(mDevice, &bufferCi, nullptr, &outBuffer->buffer),
                  VK_SUCCESS);

        VkMemoryRequirements memReqs = {};
        mDeviceDispatch.vkGetBufferMemoryRequirements(mDevice, outBuffer->buffer, &memReqs);

        uint32_t memTypeIndex = 0;
        ASSERT_TRUE(findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memTypeIndex));

        const VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = memTypeIndex,
        };
        ASSERT_EQ(
            mDeviceDispatch.vkAllocateMemory(mDevice, &allocInfo, nullptr, &outBuffer->memory),
            VK_SUCCESS);
        ASSERT_EQ(
            mDeviceDispatch.vkBindBufferMemory(mDevice, outBuffer->buffer, outBuffer->memory, 0),
            VK_SUCCESS);
        ASSERT_EQ(mDeviceDispatch.vkMapMemory(mDevice, outBuffer->memory, 0, VK_WHOLE_SIZE, 0,
                                              &outBuffer->mapped),
                  VK_SUCCESS);

        outBuffer->info.device = mDevice;
        outBuffer->info.usage = usage;
        outBuffer->info.memory = outBuffer->memory;
        outBuffer->info.size = size;
    }

    void destroyBuffer(AllocatedBuffer* buf) {
        if (buf->mapped != nullptr) {
            mDeviceDispatch.vkUnmapMemory(mDevice, buf->memory);
            buf->mapped = nullptr;
        }
        if (buf->buffer != VK_NULL_HANDLE) {
            mDeviceDispatch.vkDestroyBuffer(mDevice, buf->buffer, nullptr);
            buf->buffer = VK_NULL_HANDLE;
        }
        if (buf->memory != VK_NULL_HANDLE) {
            mDeviceDispatch.vkFreeMemory(mDevice, buf->memory, nullptr);
            buf->memory = VK_NULL_HANDLE;
        }
    }

    // Fills a staging buffer for a given (format, mipExtent, mipLevel, arrayLayer) with
    // deterministic pattern data that round-trips cleanly through the GPU for that format.
    std::vector<uint8_t> generateMipLayerPattern(VkFormat format, VkExtent3D mipExtent,
                                                 const TransferInfo& transferInfo,
                                                 uint32_t mipLevel, uint32_t arrayLayer) {
        std::vector<uint8_t> data(transferInfo.stagingBufferCopySize, 0);
        const uint8_t seed = static_cast<uint8_t>((mipLevel * 53u) + (arrayLayer * 97u) + 19u);

        if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            // Depth plane: exact binary fractions in [0.0, 1.0].
            const size_t numPixels =
                static_cast<size_t>(mipExtent.width) * mipExtent.height * mipExtent.depth;
            float* depthPtr = reinterpret_cast<float*>(data.data());
            for (size_t i = 0; i < numPixels; ++i) {
                const uint32_t step = static_cast<uint32_t>((i * 17u + seed) % 256u);
                depthPtr[i] = static_cast<float>(step) / 256.0f;
            }
            if (format == VK_FORMAT_D32_SFLOAT_S8_UINT &&
                transferInfo.bufferImageCopies.size() > 1) {
                const VkDeviceSize stencilOffset = transferInfo.bufferImageCopies[1].bufferOffset;
                for (size_t i = 0; i < numPixels; ++i) {
                    data[stencilOffset + i] = static_cast<uint8_t>((i * 29u + seed) & 0xFFu);
                }
            }
            return data;
        }

        if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_X8_D24_UNORM_PACK32) {
            // Per Vulkan spec, buffer copies of 24-bit depth use X8_D24_UNORM_PACK32 where the
            // upper 8 bits (byte 3 of each 4-byte texel) are unused.
            const size_t numPixels =
                static_cast<size_t>(mipExtent.width) * mipExtent.height * mipExtent.depth;
            for (size_t i = 0; i < numPixels; ++i) {
                data[i * 4 + 0] = static_cast<uint8_t>((i * 13u + seed) & 0xFFu);
                data[i * 4 + 1] = static_cast<uint8_t>((i * 31u + seed) & 0xFFu);
                data[i * 4 + 2] = static_cast<uint8_t>((i * 47u + seed) & 0xFFu);
                data[i * 4 + 3] = 0;
            }
            if (format == VK_FORMAT_D24_UNORM_S8_UINT &&
                transferInfo.bufferImageCopies.size() > 1) {
                const VkDeviceSize stencilOffset = transferInfo.bufferImageCopies[1].bufferOffset;
                for (size_t i = 0; i < numPixels; ++i) {
                    data[stencilOffset + i] = static_cast<uint8_t>((i * 23u + seed) & 0xFFu);
                }
            }
            return data;
        }

        if (format == VK_FORMAT_D16_UNORM_S8_UINT) {
            const size_t numPixels =
                static_cast<size_t>(mipExtent.width) * mipExtent.height * mipExtent.depth;
            for (size_t i = 0; i < numPixels * 2; ++i) {
                data[i] = static_cast<uint8_t>((i * 19u + seed) & 0xFFu);
            }
            if (transferInfo.bufferImageCopies.size() > 1) {
                const VkDeviceSize stencilOffset = transferInfo.bufferImageCopies[1].bufferOffset;
                for (size_t i = 0; i < numPixels; ++i) {
                    data[stencilOffset + i] = static_cast<uint8_t>((i * 37u + seed) & 0xFFu);
                }
            }
            return data;
        }

        // General color / compressed / single-aspect formats.
        // Mask with 0x7E so float16/float32 formats never hit NaN/Inf exponent patterns.
        for (size_t i = 0; i < data.size(); ++i) {
            uint8_t val = static_cast<uint8_t>((i * 31u + seed) & 0x7Eu);
            if (val == 0) {
                val = 2;
            }
            data[i] = val;
        }
        return data;
    }

    void uploadMipLayer(VkImage image, VkFormat format, VkExtent3D mipExtent, uint32_t mipLevel,
                        uint32_t arrayLayer, VkImageLayout finalLayout,
                        const std::vector<uint8_t>& pattern) {
        TransferInfo transferInfo;
        ASSERT_TRUE(getFormatTransferInfo(format, mipExtent, &transferInfo));
        ASSERT_EQ(pattern.size(), transferInfo.stagingBufferCopySize);

        AllocatedBuffer staging;
        createStagingBuffer(transferInfo.stagingBufferCopySize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            &staging);
        std::memcpy(staging.mapped, pattern.data(), pattern.size());

        VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
        const VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        ASSERT_EQ(mDeviceDispatch.vkAllocateCommandBuffers(mDevice, &allocInfo, &cmdBuf),
                  VK_SUCCESS);

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        ASSERT_EQ(mDeviceDispatch.vkBeginCommandBuffer(cmdBuf, &beginInfo), VK_SUCCESS);

        VkImageAspectFlags aspects = 0;
        for (const auto& copy : transferInfo.bufferImageCopies) {
            aspects |= copy.imageSubresource.aspectMask;
        }

        VkImageMemoryBarrier toTransferDst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = aspects,
                    .baseMipLevel = mipLevel,
                    .levelCount = 1,
                    .baseArrayLayer = arrayLayer,
                    .layerCount = 1,
                },
        };
        mDeviceDispatch.vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                                             nullptr, 1, &toTransferDst);

        for (auto region : transferInfo.bufferImageCopies) {
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.baseArrayLayer = arrayLayer;
            mDeviceDispatch.vkCmdCopyBufferToImage(
                cmdBuf, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }

        VkImageMemoryBarrier toFinal = toTransferDst;
        toFinal.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toFinal.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toFinal.newLayout = finalLayout;
        mDeviceDispatch.vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                             nullptr, 1, &toFinal);

        ASSERT_EQ(mDeviceDispatch.vkEndCommandBuffer(cmdBuf), VK_SUCCESS);

        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdBuf,
        };
        ASSERT_EQ(mDeviceDispatch.vkQueueSubmit(mQueue, 1, &submitInfo, VK_NULL_HANDLE),
                  VK_SUCCESS);
        ASSERT_EQ(mDeviceDispatch.vkQueueWaitIdle(mQueue), VK_SUCCESS);

        mDeviceDispatch.vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmdBuf);
        destroyBuffer(&staging);
    }

    std::vector<uint8_t> readbackMipLayer(VkImage image, VkFormat format, VkExtent3D mipExtent,
                                          uint32_t mipLevel, uint32_t arrayLayer,
                                          VkImageLayout currentLayout) {
        TransferInfo transferInfo;
        EXPECT_TRUE(getFormatTransferInfo(format, mipExtent, &transferInfo));

        AllocatedBuffer readback;
        createStagingBuffer(transferInfo.stagingBufferCopySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            &readback);
        std::memset(readback.mapped, 0, transferInfo.stagingBufferCopySize);

        VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
        const VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        EXPECT_EQ(mDeviceDispatch.vkAllocateCommandBuffers(mDevice, &allocInfo, &cmdBuf),
                  VK_SUCCESS);

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        EXPECT_EQ(mDeviceDispatch.vkBeginCommandBuffer(cmdBuf, &beginInfo), VK_SUCCESS);

        VkImageAspectFlags aspects = 0;
        for (const auto& copy : transferInfo.bufferImageCopies) {
            aspects |= copy.imageSubresource.aspectMask;
        }

        VkImageMemoryBarrier toTransferSrc = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = currentLayout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = aspects,
                    .baseMipLevel = mipLevel,
                    .levelCount = 1,
                    .baseArrayLayer = arrayLayer,
                    .layerCount = 1,
                },
        };
        mDeviceDispatch.vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                                             nullptr, 1, &toTransferSrc);

        for (auto region : transferInfo.bufferImageCopies) {
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.baseArrayLayer = arrayLayer;
            mDeviceDispatch.vkCmdCopyImageToBuffer(cmdBuf, image,
                                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                   readback.buffer, 1, &region);
        }

        VkBufferMemoryBarrier bufBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = readback.buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        VkImageMemoryBarrier toRestored = toTransferSrc;
        toRestored.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toRestored.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toRestored.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toRestored.newLayout = currentLayout;
        mDeviceDispatch.vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             VK_PIPELINE_STAGE_HOST_BIT |
                                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                             0, 0, nullptr, 1, &bufBarrier, 1, &toRestored);

        EXPECT_EQ(mDeviceDispatch.vkEndCommandBuffer(cmdBuf), VK_SUCCESS);

        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdBuf,
        };
        EXPECT_EQ(mDeviceDispatch.vkQueueSubmit(mQueue, 1, &submitInfo, VK_NULL_HANDLE),
                  VK_SUCCESS);
        EXPECT_EQ(mDeviceDispatch.vkQueueWaitIdle(mQueue), VK_SUCCESS);

        std::vector<uint8_t> result(transferInfo.stagingBufferCopySize);
        std::memcpy(result.data(), readback.mapped, result.size());

        mDeviceDispatch.vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmdBuf);
        destroyBuffer(&readback);
        return result;
    }

    void verifySaveAndLoadImage(VkImageType imageType, VkImageCreateFlags flags, VkFormat format,
                                VkExtent3D extent, uint32_t mipLevels, uint32_t arrayLayers,
                                VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkImageUsageFlags usage =
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (formatIsDepthOrStencil(format)) {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        } else {
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        if (!isImageConfigSupported(format, imageType, VK_IMAGE_TILING_OPTIMAL, usage, flags,
                                    extent, mipLevels, arrayLayers)) {
            GTEST_SKIP() << "Image configuration not supported by driver for format "
                         << string_VkFormat(format);
        }

        const VkImageCreateInfo imageCi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = flags,
            .imageType = imageType,
            .format = format,
            .extent = extent,
            .mipLevels = mipLevels,
            .arrayLayers = arrayLayers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        AllocatedImage srcImage;
        AllocatedImage dstImage;
        createTestImage(imageCi, layout, &srcImage);
        createTestImage(imageCi, layout, &dstImage);

        std::vector<std::vector<std::vector<uint8_t>>> expectedPatterns(
            mipLevels, std::vector<std::vector<uint8_t>>(arrayLayers));

        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            const VkExtent3D mipExtent = GetMipExtent(extent, mip);
            TransferInfo transferInfo;
            ASSERT_TRUE(getFormatTransferInfo(format, mipExtent, &transferInfo))
                << "getFormatTransferInfo failed for " << string_VkFormat(format) << " at mip "
                << mip;

            for (uint32_t layer = 0; layer < arrayLayers; ++layer) {
                expectedPatterns[mip][layer] =
                    generateMipLayerPattern(format, mipExtent, transferInfo, mip, layer);
                uploadMipLayer(srcImage.image, format, mipExtent, mip, layer, layout,
                               expectedPatterns[mip][layer]);
            }
        }

        MemStream stream;
        ASSERT_TRUE(saveImageContent(&stream, &mStateBlock, srcImage.image, &srcImage.info));
        ASSERT_GT(stream.writtenSize(), 0u);

        ASSERT_TRUE(loadImageContent(&stream, &mStateBlock, dstImage.image, &dstImage.info));

        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            const VkExtent3D mipExtent = GetMipExtent(extent, mip);
            for (uint32_t layer = 0; layer < arrayLayers; ++layer) {
                SCOPED_TRACE(::testing::Message()
                             << "format=" << string_VkFormat(format) << " mip=" << mip
                             << " layer=" << layer << " mipExtent=(" << mipExtent.width << "x"
                             << mipExtent.height << "x" << mipExtent.depth << ")");
                const std::vector<uint8_t> actual =
                    readbackMipLayer(dstImage.image, format, mipExtent, mip, layer, layout);
                EXPECT_EQ(actual, expectedPatterns[mip][layer]);
            }
        }

        destroyTestImage(&dstImage);
        destroyTestImage(&srcImage);
    }

    static VulkanDispatch* sVk;
    VulkanDispatch mInstanceDispatch = {};
    VulkanDispatch mDeviceDispatch = {};
    VkInstance mInstance = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    PhysicalDeviceInfo mPhysicalDeviceInfo = {};
    uint32_t mQueueFamilyIndex = 0;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    VkQueue mQueue = VK_NULL_HANDLE;
    StateBlock mStateBlock = {};

   private:
    void createInstance() {
        uint32_t extCount = 0;
        ASSERT_EQ(sVk->vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr),
                  VK_SUCCESS);
        std::vector<VkExtensionProperties> exts(extCount);
        ASSERT_EQ(sVk->vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data()),
                  VK_SUCCESS);

        std::vector<const char*> enabledExts;
        VkInstanceCreateFlags flags = 0;
        for (const auto& ext : exts) {
            if (std::strcmp(ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) ==
                0) {
                enabledExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }
        }

        const VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "vk_decoder_snapshot_utils_unittests",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "gfxstream",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1,
        };
        const VkInstanceCreateInfo instanceCi = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = flags,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = static_cast<uint32_t>(enabledExts.size()),
            .ppEnabledExtensionNames = enabledExts.data(),
        };
        ASSERT_EQ(sVk->vkCreateInstance(&instanceCi, nullptr, &mInstance), VK_SUCCESS);
        ASSERT_NE(mInstance, VK_NULL_HANDLE);

        init_vulkan_dispatch_from_instance(sVk, mInstance, &mInstanceDispatch);
    }

    void pickPhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(mInstanceDispatch.vkEnumeratePhysicalDevices(mInstance, &count, nullptr),
                  VK_SUCCESS);
        ASSERT_GT(count, 0u);
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(
            mInstanceDispatch.vkEnumeratePhysicalDevices(mInstance, &count, devices.data()),
            VK_SUCCESS);

        for (VkPhysicalDevice dev : devices) {
            uint32_t qfCount = 0;
            mInstanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
            std::vector<VkQueueFamilyProperties> qfProps(qfCount);
            mInstanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount,
                                                                       qfProps.data());
            for (uint32_t i = 0; i < qfCount; ++i) {
                if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    mPhysicalDevice = dev;
                    mQueueFamilyIndex = i;
                    return;
                }
            }
        }
        FAIL() << "No suitable physical device with a graphics queue found.";
    }

    void createLogicalDevice() {
        uint32_t extCount = 0;
        ASSERT_EQ(mInstanceDispatch.vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr,
                                                                         &extCount, nullptr),
                  VK_SUCCESS);
        std::vector<VkExtensionProperties> exts(extCount);
        ASSERT_EQ(mInstanceDispatch.vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr,
                                                                         &extCount, exts.data()),
                  VK_SUCCESS);

        std::vector<const char*> enabledExts;
        for (const auto& ext : exts) {
            if (std::strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
                enabledExts.push_back("VK_KHR_portability_subset");
            }
        }

        VkPhysicalDeviceFeatures supportedFeatures = {};
        mInstanceDispatch.vkGetPhysicalDeviceFeatures(mPhysicalDevice, &supportedFeatures);

        VkPhysicalDeviceFeatures enabledFeatures = {};
        enabledFeatures.imageCubeArray = supportedFeatures.imageCubeArray;
        enabledFeatures.textureCompressionBC = supportedFeatures.textureCompressionBC;
        enabledFeatures.textureCompressionETC2 = supportedFeatures.textureCompressionETC2;
        enabledFeatures.textureCompressionASTC_LDR = supportedFeatures.textureCompressionASTC_LDR;

        const float priority = 1.0f;
        const VkDeviceQueueCreateInfo queueCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = mQueueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        const VkDeviceCreateInfo deviceCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCi,
            .enabledExtensionCount = static_cast<uint32_t>(enabledExts.size()),
            .ppEnabledExtensionNames = enabledExts.data(),
            .pEnabledFeatures = &enabledFeatures,
        };
        ASSERT_EQ(mInstanceDispatch.vkCreateDevice(mPhysicalDevice, &deviceCi, nullptr, &mDevice),
                  VK_SUCCESS);
        ASSERT_NE(mDevice, VK_NULL_HANDLE);

        init_vulkan_dispatch_from_device(&mInstanceDispatch, mDevice, &mDeviceDispatch);
    }
};

VulkanDispatch* VkDecoderSnapshotUtilsTest::sVk = nullptr;

// --- 2D Image Tests ---

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DSingleMipRGBA8) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, VK_FORMAT_R8G8B8A8_UNORM, {32, 32, 1},
                           /*mipLevels=*/1, /*arrayLayers=*/1);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DWithMipmapsRGBA8) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, VK_FORMAT_R8G8B8A8_UNORM, {32, 32, 1},
                           /*mipLevels=*/6, /*arrayLayers=*/1);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DNonSquareWithMipmaps) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, VK_FORMAT_R8G8B8A8_UNORM, {32, 16, 1},
                           /*mipLevels=*/6, /*arrayLayers=*/1);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DArrayWithMipmaps) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, VK_FORMAT_R8G8B8A8_UNORM, {16, 16, 1},
                           /*mipLevels=*/5, /*arrayLayers=*/4);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DVariousColorFormatsWithMipmaps) {
    const VkFormat kFormats[] = {
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R5G6B5_UNORM_PACK16,
        VK_FORMAT_R4G4B4A4_UNORM_PACK16,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R16_UNORM,
        VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
    };

    for (VkFormat format : kFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                    0, {16, 16, 1}, /*mipLevels=*/5, /*arrayLayers=*/1)) {
            continue;
        }
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, format, {16, 16, 1}, /*mipLevels=*/5,
                               /*arrayLayers=*/1);
    }
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DDepthStencilFormats) {
    const VkFormat kDepthStencilFormats[] = {
        VK_FORMAT_D16_UNORM,          VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_S8_UINT,            VK_FORMAT_D16_UNORM_S8_UINT,   VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };

    for (VkFormat format : kDepthStencilFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                    {16, 16, 1}, /*mipLevels=*/1, /*arrayLayers=*/1)) {
            continue;
        }
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, format, {16, 16, 1}, /*mipLevels=*/1,
                               /*arrayLayers=*/1,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DBCCompressedWithMipmaps) {
    const VkFormat kBcFormats[] = {
        VK_FORMAT_BC1_RGB_UNORM_BLOCK,  VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK,      VK_FORMAT_BC3_UNORM_BLOCK,
        VK_FORMAT_BC4_UNORM_BLOCK,      VK_FORMAT_BC5_UNORM_BLOCK,
        VK_FORMAT_BC6H_UFLOAT_BLOCK,    VK_FORMAT_BC7_UNORM_BLOCK,
    };

    size_t testedCount = 0;
    for (VkFormat format : kBcFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                    {16, 16, 1}, /*mipLevels=*/5, /*arrayLayers=*/1)) {
            continue;
        }
        ++testedCount;
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, format, {16, 16, 1}, /*mipLevels=*/5,
                               /*arrayLayers=*/1);
    }
    if (testedCount == 0) {
        GTEST_SKIP() << "BC compressed formats not supported by driver";
    }
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DETC2CompressedWithMipmaps) {
    const VkFormat kEtc2Formats[] = {
        VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,   VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
        VK_FORMAT_EAC_R11_UNORM_BLOCK,       VK_FORMAT_EAC_R11_SNORM_BLOCK,
        VK_FORMAT_EAC_R11G11_UNORM_BLOCK,    VK_FORMAT_EAC_R11G11_SNORM_BLOCK,
    };

    size_t testedCount = 0;
    for (VkFormat format : kEtc2Formats) {
        SCOPED_TRACE(string_VkFormat(format));
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                    {16, 16, 1}, /*mipLevels=*/5, /*arrayLayers=*/1)) {
            continue;
        }
        ++testedCount;
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, format, {16, 16, 1}, /*mipLevels=*/5,
                               /*arrayLayers=*/1);
    }
    if (testedCount == 0) {
        GTEST_SKIP() << "ETC2/EAC compressed formats not supported by driver";
    }
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DASTCCompressedWithMipmaps) {
    const VkFormat kAstcFormats[] = {
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK,   VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x4_UNORM_BLOCK,   VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_6x5_UNORM_BLOCK,   VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x5_UNORM_BLOCK,   VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x8_UNORM_BLOCK,   VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x5_UNORM_BLOCK,  VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x8_UNORM_BLOCK,  VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
        VK_FORMAT_ASTC_12x10_UNORM_BLOCK, VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
    };

    size_t testedCount = 0;
    for (VkFormat format : kAstcFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                    {24, 24, 1}, /*mipLevels=*/5, /*arrayLayers=*/1)) {
            continue;
        }
        ++testedCount;
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, format, {24, 24, 1}, /*mipLevels=*/5,
                               /*arrayLayers=*/1);
    }
    if (testedCount == 0) {
        GTEST_SKIP() << "ASTC compressed formats not supported by driver";
    }
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage2DMultiPlanarYcbcr) {
    const VkFormat kYcbcrFormats[] = {
        VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
        VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
    };

    size_t testedCount = 0;
    for (VkFormat format : kYcbcrFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                    {32, 32, 1}, /*mipLevels=*/1, /*arrayLayers=*/1)) {
            continue;
        }
        ++testedCount;
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, 0, format, {32, 32, 1}, /*mipLevels=*/1,
                               /*arrayLayers=*/1);
    }
    if (testedCount == 0) {
        GTEST_SKIP() << "Multi-planar YCbCr formats not supported by driver";
    }
}

// --- 3D Image Tests ---

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage3DSingleMip) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_3D, 0, VK_FORMAT_R8G8B8A8_UNORM, {16, 16, 8},
                           /*mipLevels=*/1, /*arrayLayers=*/1);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage3DWithMipmaps) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_3D, 0, VK_FORMAT_R8G8B8A8_UNORM, {16, 8, 4},
                           /*mipLevels=*/5, /*arrayLayers=*/1);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImage3DVariousFormatsWithMipmaps) {
    const VkFormat kFormats[] = {
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
    };

    for (VkFormat format : kFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_3D, VK_IMAGE_TILING_OPTIMAL,
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                    0, {8, 8, 8}, /*mipLevels=*/4, /*arrayLayers=*/1)) {
            continue;
        }
        verifySaveAndLoadImage(VK_IMAGE_TYPE_3D, 0, format, {8, 8, 8}, /*mipLevels=*/4,
                               /*arrayLayers=*/1);
    }
}

// --- Cube Image Tests ---

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageCubeSingleMip) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                           VK_FORMAT_R8G8B8A8_UNORM, {32, 32, 1},
                           /*mipLevels=*/1, /*arrayLayers=*/6);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageCubeWithMipmaps) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                           VK_FORMAT_R8G8B8A8_UNORM, {32, 32, 1},
                           /*mipLevels=*/6, /*arrayLayers=*/6);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageCubeFloat16WithMipmaps) {
    // Environment cubemaps in PBR renderers (such as Vulkan-glTF-PBR irradiance/prefiltered maps)
    // commonly use R16G16B16A16_SFLOAT or R32G32B32A32_SFLOAT with mipmaps and 6 faces.
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                           VK_FORMAT_R16G16B16A16_SFLOAT, {32, 32, 1},
                           /*mipLevels=*/6, /*arrayLayers=*/6);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageCubeArrayWithMipmaps) {
    verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                           VK_FORMAT_R8G8B8A8_UNORM, {16, 16, 1},
                           /*mipLevels=*/5, /*arrayLayers=*/12);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageCubeETC2AndASTCWithMipmaps) {
    const VkFormat kCompressedCubeFormats[] = {
        VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
        VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
    };

    size_t testedCount = 0;
    for (VkFormat format : kCompressedCubeFormats) {
        SCOPED_TRACE(string_VkFormat(format));
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!isImageConfigSupported(format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage,
                                    VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, {24, 24, 1},
                                    /*mipLevels=*/5, /*arrayLayers=*/6)) {
            continue;
        }
        ++testedCount;
        verifySaveAndLoadImage(VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, format,
                               {24, 24, 1}, /*mipLevels=*/5, /*arrayLayers=*/6);
    }
    if (testedCount == 0) {
        GTEST_SKIP() << "ETC2/ASTC compressed cubemap formats not supported by driver";
    }
}

// --- Edge cases & Buffer tests ---

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageUndefinedLayoutSkipsContent) {
    const VkImageCreateInfo imageCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {8, 8, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    AllocatedImage srcImage;
    AllocatedImage dstImage;
    createTestImage(imageCi, VK_IMAGE_LAYOUT_UNDEFINED, &srcImage);
    createTestImage(imageCi, VK_IMAGE_LAYOUT_UNDEFINED, &dstImage);

    MemStream stream;
    EXPECT_TRUE(saveImageContent(&stream, &mStateBlock, srcImage.image, &srcImage.info));
    EXPECT_EQ(stream.writtenSize(), sizeof(uint32_t));
    EXPECT_TRUE(loadImageContent(&stream, &mStateBlock, dstImage.image, &dstImage.info));

    destroyTestImage(&dstImage);
    destroyTestImage(&srcImage);
}

TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadBufferContent) {
    constexpr VkDeviceSize kBufferSize = 1024;
    const VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    AllocatedBuffer srcBuffer;
    AllocatedBuffer dstBuffer;
    createStagingBuffer(kBufferSize, usage, &srcBuffer);
    createStagingBuffer(kBufferSize, usage, &dstBuffer);

    std::vector<uint8_t> expected(kBufferSize);
    for (size_t i = 0; i < kBufferSize; ++i) {
        expected[i] = static_cast<uint8_t>((i * 41u + 7u) & 0xFFu);
    }
    std::memcpy(srcBuffer.mapped, expected.data(), kBufferSize);
    std::memset(dstBuffer.mapped, 0, kBufferSize);

    MemStream stream;
    ASSERT_TRUE(saveBufferContent(&stream, &mStateBlock, srcBuffer.buffer, &srcBuffer.info));
    ASSERT_GT(stream.writtenSize(), kBufferSize);

    ASSERT_TRUE(loadBufferContent(&stream, &mStateBlock, dstBuffer.buffer, &dstBuffer.info));

    std::vector<uint8_t> actual(kBufferSize);
    std::memcpy(actual.data(), dstBuffer.mapped, kBufferSize);
    EXPECT_EQ(actual, expected);

    destroyBuffer(&dstBuffer);
    destroyBuffer(&srcBuffer);
}

TEST_F(VkDecoderSnapshotUtilsTest, LoadMappedMemorySkipsOptimalImageRangesOnUnifiedMemory) {
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    const VkExtent3D extent = {64, 64, 1};
    const uint32_t mipLevels = 4;
    const VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkImageCreateInfo imageCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    AllocatedImage srcImage;
    createTestImage(imageCi, layout, &srcImage);

    std::vector<std::vector<uint8_t>> expectedPatterns(mipLevels);
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        const VkExtent3D mipExtent = GetMipExtent(extent, mip);
        TransferInfo transferInfo;
        ASSERT_TRUE(getFormatTransferInfo(format, mipExtent, &transferInfo));
        expectedPatterns[mip] = generateMipLayerPattern(format, mipExtent, transferInfo, mip, 0);
        uploadMipLayer(srcImage.image, format, mipExtent, mip, 0, layout, expectedPatterns[mip]);
    }

    MemStream imageStream;
    ASSERT_TRUE(saveImageContent(&imageStream, &mStateBlock, srcImage.image, &srcImage.info));

    // Create a destination optimal-tiled image suballocated inside a shared HOST_VISIBLE
    // VkDeviceMemory block alongside prefix and suffix host-mapped regions.
    VkImage dstVkImage = VK_NULL_HANDLE;
    ASSERT_EQ(mDeviceDispatch.vkCreateImage(mDevice, &imageCi, nullptr, &dstVkImage), VK_SUCCESS);

    VkMemoryRequirements imgMemReqs = {};
    mDeviceDispatch.vkGetImageMemoryRequirements(mDevice, dstVkImage, &imgMemReqs);

    const VkDeviceSize prefixSize = std::max<VkDeviceSize>(4096, imgMemReqs.alignment);
    const VkDeviceSize imageOffset = prefixSize;
    const VkDeviceSize suffixOffset = imageOffset + imgMemReqs.size;
    const VkDeviceSize suffixSize = 4096;
    const VkDeviceSize totalMemorySize = suffixOffset + suffixSize;

    VkMemoryRequirements sharedMemReqs = imgMemReqs;
    sharedMemReqs.size = totalMemorySize;
    uint32_t hostVisibleType = UINT32_MAX;
    ASSERT_TRUE(findMemoryType(
        sharedMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &hostVisibleType));

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = totalMemorySize,
        .memoryTypeIndex = hostVisibleType,
    };
    VkDeviceMemory sharedMemory = VK_NULL_HANDLE;
    ASSERT_EQ(mDeviceDispatch.vkAllocateMemory(mDevice, &allocInfo, nullptr, &sharedMemory),
              VK_SUCCESS);
    ASSERT_EQ(
        mDeviceDispatch.vkBindImageMemory(mDevice, dstVkImage, sharedMemory, imageOffset),
        VK_SUCCESS);

    void* mappedPtr = nullptr;
    ASSERT_EQ(mDeviceDispatch.vkMapMemory(mDevice, sharedMemory, 0, totalMemorySize, 0, &mappedPtr),
              VK_SUCCESS);

    ImageInfo dstImageInfo = {};
    dstImageInfo.device = mDevice;
    dstImageInfo.imageCreateInfoShallow = imageCi;
    dstImageInfo.layout = layout;
    dstImageInfo.memory = sharedMemory;
    dstImageInfo.memoryOffset = imageOffset;
    dstImageInfo.memorySize = imgMemReqs.size;

    MemoryInfo memoryInfo = {};
    memoryInfo.ptr = mappedPtr;
    memoryInfo.size = totalMemorySize;
    memoryInfo.device = mDevice;
    memoryInfo.optimalImageMemoryRanges[dstVkImage] = {imageOffset, imgMemReqs.size};

    // Restore optimal-tiled image content first, then restore mapped memory from a stream whose
    // image range is filled with zeros (which would clobber the image on unified memory if not
    // skipped by loadMappedMemoryContent).
    ASSERT_TRUE(loadImageContent(&imageStream, &mStateBlock, dstVkImage, &dstImageInfo));

    std::vector<uint8_t> savedRawMemory(totalMemorySize, 0);
    for (VkDeviceSize i = 0; i < prefixSize; ++i) {
        savedRawMemory[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
    }
    for (VkDeviceSize i = 0; i < suffixSize; ++i) {
        savedRawMemory[suffixOffset + i] = static_cast<uint8_t>((i * 17u + 13u) & 0xFFu);
    }
    MemStream memoryStream;
    memoryStream.write(savedRawMemory.data(), savedRawMemory.size());

    ASSERT_TRUE(loadMappedMemoryContent(&memoryStream, &memoryInfo, totalMemorySize));

    // Verify prefix and suffix host-mapped regions were restored.
    const auto* mappedBytes = static_cast<const uint8_t*>(mappedPtr);
    EXPECT_EQ(std::memcmp(mappedBytes, savedRawMemory.data(), prefixSize), 0);
    EXPECT_EQ(std::memcmp(mappedBytes + suffixOffset, savedRawMemory.data() + suffixOffset,
                          suffixSize),
              0);

    // Verify optimal-tiled image mip levels were not overwritten by the zeroed raw memory range.
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        const VkExtent3D mipExtent = GetMipExtent(extent, mip);
        const std::vector<uint8_t> actual =
            readbackMipLayer(dstVkImage, format, mipExtent, mip, 0, layout);
        EXPECT_EQ(actual, expectedPatterns[mip]) << "Mismatch at mip " << mip;
    }

    mDeviceDispatch.vkUnmapMemory(mDevice, sharedMemory);
    mDeviceDispatch.vkDestroyImage(mDevice, dstVkImage, nullptr);
    mDeviceDispatch.vkFreeMemory(mDevice, sharedMemory, nullptr);
    destroyTestImage(&srcImage);
}

//TODO(b/535587757): Refactor snapshot save/load routines to avoid duplicating
// the save/load logic here.
TEST_F(VkDecoderSnapshotUtilsTest, SaveAndLoadImageGeneratedByRenderPass) {
    // Tests saving and restoring an image that is populated and layout-transitioned solely via a
    // one-off VkRenderPass (e.g., procedural lookup texture / LUT precomputation):
    // 1. Creates a 2D VK_FORMAT_R16G16_SFLOAT optimal-tiled image with
    //    usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    //    (without explicit TRANSFER_SRC_BIT / TRANSFER_DST_BIT) and
    //    initialLayout = VK_IMAGE_LAYOUT_UNDEFINED.
    // 2. Renders into the image via a VkRenderPass whose attachment description has
    //    initialLayout = VK_IMAGE_LAYOUT_UNDEFINED and
    //    finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
    // 3. Submits the command buffer, waits for completion, and destroys the temporary
    //    VkFramebuffer, VkRenderPass, and VkCommandBuffer without ever issuing a
    //    vkCmdPipelineBarrier on the image.
    // 4. Saves and restores the image across a snapshot and verifies the restored image layout
    //    and pixel contents match the rendered image.
    gfxstream::host::FeatureSet features;
    features.GlDirectMem.setEnabled(true);
    features.VulkanSnapshots.setEnabled(true);
    features.GuestVulkanOnly.setEnabled(true);

    gfxstream::base::setEnvironmentVariable("ANDROID_EMU_VK_NO_CLEANUP", "1");
    std::unique_ptr<VkEmulation> vkEmu = VkEmulation::create(sVk, {}, features);
    ASSERT_NE(vkEmu, nullptr);
    VkDecoderGlobalState::initialize(vkEmu.get());
    RenderThreadInfoVk threadInfo;
    threadInfo.ctx_id = 1;

    VkDecoderGlobalState* vkState = VkDecoderGlobalState::get();
    ASSERT_NE(vkState, nullptr);
    gfxstream::base::BumpPool pool;
    VkDecoderContext decoderContext = {};

    const VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "RenderPassImageSnapshotTest",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "gfxstream",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instanceCi = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    };
    VkInstance boxedInstance = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkCreateInstance(&pool, 0, &instanceCi, nullptr, &boxedInstance),
              VK_SUCCESS);

    uint32_t physDevCount = 0;
    ASSERT_EQ(vkState->on_vkEnumeratePhysicalDevices(&pool, 0, boxedInstance, &physDevCount,
                                                     nullptr),
              VK_SUCCESS);
    ASSERT_GT(physDevCount, 0u);
    std::vector<VkPhysicalDevice> boxedPhysDevs(physDevCount);
    ASSERT_EQ(vkState->on_vkEnumeratePhysicalDevices(&pool, 0, boxedInstance, &physDevCount,
                                                     boxedPhysDevs.data()),
              VK_SUCCESS);
    VkPhysicalDevice boxedPhysDev = boxedPhysDevs[0];

    const float queuePriority = 1.0f;
    const VkDeviceQueueCreateInfo queueCi = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = mQueueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    const VkDeviceCreateInfo deviceCi = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCi,
    };
    VkDevice boxedDevice = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkCreateDevice(&pool, 0, boxedPhysDev, &deviceCi, nullptr, &boxedDevice),
              VK_SUCCESS);

    VkQueue boxedQueue = VK_NULL_HANDLE;
    vkState->on_vkGetDeviceQueue(&pool, 0, boxedDevice, mQueueFamilyIndex, 0, &boxedQueue);
    ASSERT_NE(boxedQueue, VK_NULL_HANDLE);

    const VkCommandPoolCreateInfo cmdPoolCi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = mQueueFamilyIndex,
    };
    VkCommandPool boxedCmdPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkCreateCommandPool(&pool, 0, boxedDevice, &cmdPoolCi, nullptr,
                                              &boxedCmdPool),
              VK_SUCCESS);

    constexpr VkFormat kFormat = VK_FORMAT_R16G16_SFLOAT;
    constexpr uint32_t kDim = 64;
    const VkExtent3D extent = {kDim, kDim, 1};
    const VkImageCreateInfo imageCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = kFormat,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage boxedSrcImage = VK_NULL_HANDLE;
    ASSERT_EQ(
        vkState->on_vkCreateImage(&pool, 0, boxedDevice, &imageCi, nullptr, &boxedSrcImage),
        VK_SUCCESS);

    VkMemoryRequirements memReqs = {};
    vkState->on_vkGetImageMemoryRequirements(&pool, 0, boxedDevice, unbox_VkImage(boxedSrcImage),
                                             &memReqs);

    VkPhysicalDeviceMemoryProperties guestMemProps = {};
    vkState->on_vkGetPhysicalDeviceMemoryProperties(&pool, 0, boxedPhysDev, &guestMemProps);
    uint32_t deviceLocalType = UINT32_MAX;
    for (uint32_t i = 0; i < guestMemProps.memoryTypeCount; ++i) {
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (guestMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            deviceLocalType = i;
            break;
        }
    }
    ASSERT_NE(deviceLocalType, UINT32_MAX);

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = deviceLocalType,
    };
    VkDeviceMemory boxedSrcMem = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkAllocateMemory(&pool, 0, boxedDevice, &allocInfo, nullptr, &boxedSrcMem),
              VK_SUCCESS);
    ASSERT_EQ(vkState->on_vkBindImageMemory(&pool, 0, boxedDevice, unbox_VkImage(boxedSrcImage),
                                            unbox_VkDeviceMemory(boxedSrcMem), 0),
              VK_SUCCESS);

    const VkImageViewCreateInfo viewCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = unbox_VkImage(boxedSrcImage),
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = kFormat,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    VkImageView boxedSrcView = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkCreateImageView(&pool, 0, boxedDevice, &viewCi, nullptr, &boxedSrcView),
              VK_SUCCESS);

    const VkAttachmentDescription attDesc = {
        .format = kFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpassDesc = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
    };
    VkSubpassDependency dependencies[2] = {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    const VkRenderPassCreateInfo rpCi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attDesc,
        .subpassCount = 1,
        .pSubpasses = &subpassDesc,
        .dependencyCount = 2,
        .pDependencies = dependencies,
    };
    VkRenderPass boxedRenderPass = VK_NULL_HANDLE;
    ASSERT_EQ(
        vkState->on_vkCreateRenderPass(&pool, 0, boxedDevice, &rpCi, nullptr, &boxedRenderPass),
        VK_SUCCESS);

    VkImageView unboxedAttachmentView = unbox_VkImageView(boxedSrcView);
    const VkFramebufferCreateInfo fbCi = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = unbox_VkRenderPass(boxedRenderPass),
        .attachmentCount = 1,
        .pAttachments = &unboxedAttachmentView,
        .width = kDim,
        .height = kDim,
        .layers = 1,
    };
    VkFramebuffer boxedFramebuffer = VK_NULL_HANDLE;
    ASSERT_EQ(
        vkState->on_vkCreateFramebuffer(&pool, 0, boxedDevice, &fbCi, nullptr, &boxedFramebuffer),
        VK_SUCCESS);

    const VkCommandBufferAllocateInfo cmdBufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = unbox_VkCommandPool(boxedCmdPool),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer boxedCmdBuf = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkAllocateCommandBuffers(&pool, 0, boxedDevice, &cmdBufAllocInfo,
                                                   &boxedCmdBuf),
              VK_SUCCESS);

    const VkCommandBufferBeginInfo cmdBufBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    ASSERT_EQ(vkState->on_vkBeginCommandBuffer(&pool, 0, boxedCmdBuf, &cmdBufBeginInfo,
                                               decoderContext),
              VK_SUCCESS);

    VkClearValue clearValue = {};
    clearValue.color.float32[0] = 0.25f;
    clearValue.color.float32[1] = 0.75f;
    clearValue.color.float32[2] = 0.0f;
    clearValue.color.float32[3] = 1.0f;

    const VkRenderPassBeginInfo rpBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = unbox_VkRenderPass(boxedRenderPass),
        .framebuffer = unbox_VkFramebuffer(boxedFramebuffer),
        .renderArea = {.offset = {0, 0}, .extent = {kDim, kDim}},
        .clearValueCount = 1,
        .pClearValues = &clearValue,
    };
    vkState->on_vkCmdBeginRenderPass(&pool, 0, boxedCmdBuf, &rpBeginInfo,
                                     VK_SUBPASS_CONTENTS_INLINE);

    // Populate sub-regions with additional distinct R16G16_SFLOAT values inside the render pass.
    VulkanDispatch* cmdVk = dispatch_VkCommandBuffer(boxedCmdBuf);
    VkCommandBuffer unboxedCmdBuf = unbox_VkCommandBuffer(boxedCmdBuf);
    VkClearAttachment clearAtt = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
    };
    clearAtt.clearValue.color.float32[0] = 0.5f;
    clearAtt.clearValue.color.float32[1] = 0.125f;
    clearAtt.clearValue.color.float32[2] = 0.0f;
    clearAtt.clearValue.color.float32[3] = 1.0f;
    VkClearRect clearRect = {
        .rect = {.offset = {kDim / 2, 0}, .extent = {kDim / 2, kDim / 2}},
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    cmdVk->vkCmdClearAttachments(unboxedCmdBuf, 1, &clearAtt, 1, &clearRect);

    clearAtt.clearValue.color.float32[0] = 0.875f;
    clearAtt.clearValue.color.float32[1] = 0.375f;
    clearRect.rect.offset = {0, kDim / 2};
    cmdVk->vkCmdClearAttachments(unboxedCmdBuf, 1, &clearAtt, 1, &clearRect);

    cmdVk->vkCmdEndRenderPass(unboxedCmdBuf);
    ASSERT_EQ(vkState->on_vkEndCommandBuffer(&pool, 0, boxedCmdBuf, decoderContext), VK_SUCCESS);

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &unboxedCmdBuf,
    };
    ASSERT_EQ(vkState->on_vkQueueSubmit(&pool, 0, boxedQueue, 1, &submitInfo, VK_NULL_HANDLE),
              VK_SUCCESS);
    ASSERT_EQ(vkState->on_vkQueueWaitIdle(&pool, 0, boxedQueue), VK_SUCCESS);

    // Destroy the temporary framebuffer, render pass, and command buffer immediately after queue
    // wait idle, before taking a snapshot (a common pattern for one-time offscreen/LUT passes).
    vkState->on_vkDestroyFramebuffer(&pool, 0, boxedDevice, unbox_VkFramebuffer(boxedFramebuffer),
                                     nullptr);
    vkState->on_vkDestroyRenderPass(&pool, 0, boxedDevice, unbox_VkRenderPass(boxedRenderPass),
                                    nullptr);
    vkState->on_vkFreeCommandBuffers(&pool, 0, boxedDevice, unbox_VkCommandPool(boxedCmdPool), 1,
                                     &unboxedCmdBuf);

    // Save snapshot via VkDecoderGlobalState::save().
    MemStream snapshotStream;
    ASSERT_TRUE(vkState->save(&snapshotStream));

    // Advance snapshotStream past the header to the saved VkImage section.
    const uint64_t devCount = snapshotStream.getBe64();
    for (uint64_t i = 0; i < devCount; ++i) {
        snapshotStream.getBe64();
        snapshotStream.getBe32();
    }
    const uint64_t instCount = snapshotStream.getBe64();
    for (uint64_t i = 0; i < instCount; ++i) {
        snapshotStream.getBe64();
        snapshotStream.getBe32();
    }
    snapshotStream.getBe64();  // mNextHostBlobId
    std::vector<uint64_t> handleReplayBuffer;
    std::vector<uint8_t> decoderReplayBuffer;
    VkDecoderSnapshot::loadReplayBuffers(&snapshotStream, &handleReplayBuffer,
                                         &decoderReplayBuffer);
    const uint32_t memoryCount = snapshotStream.getBe32();
    for (uint32_t i = 0; i < memoryCount; ++i) {
        snapshotStream.getBe64();
        const bool hasPtr = snapshotStream.getByte() != 0;
        if (hasPtr) {
            const uint64_t size = snapshotStream.getBe64();
            std::vector<uint8_t> discard(size);
            snapshotStream.read(discard.data(), size);
        }
        snapshotStream.getBe64();
        snapshotStream.getBe64();
    }

    // Read the saved image layout and load the image content into a fresh destination image
    // created with the same parameters via VkDecoderGlobalState (mirroring snapshot replay).
    const bool hasContentToSave = (snapshotStream.getBe32() != 0);
    ASSERT_TRUE(hasContentToSave);
    const VkImageLayout savedLayout = static_cast<VkImageLayout>(snapshotStream.getBe32());
    EXPECT_EQ(savedLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkImage boxedDstImage = VK_NULL_HANDLE;
    ASSERT_EQ(
        vkState->on_vkCreateImage(&pool, 0, boxedDevice, &imageCi, nullptr, &boxedDstImage),
        VK_SUCCESS);
    VkDeviceMemory boxedDstMem = VK_NULL_HANDLE;
    ASSERT_EQ(vkState->on_vkAllocateMemory(&pool, 0, boxedDevice, &allocInfo, nullptr, &boxedDstMem),
              VK_SUCCESS);
    ASSERT_EQ(vkState->on_vkBindImageMemory(&pool, 0, boxedDevice, unbox_VkImage(boxedDstImage),
                                            unbox_VkDeviceMemory(boxedDstMem), 0),
              VK_SUCCESS);

    VkDevice unboxedDevice = unbox_VkDevice(boxedDevice);
    VulkanDispatch* devDispatch = dispatch_VkDevice(boxedDevice);
    VkPhysicalDevice unboxedPhysDev = unbox_VkPhysicalDevice(boxedPhysDev);
    VulkanDispatch* instDispatch = dispatch_VkPhysicalDevice(boxedPhysDev);
    VkPhysicalDeviceMemoryProperties hostMemProps = {};
    instDispatch->vkGetPhysicalDeviceMemoryProperties(unboxedPhysDev, &hostMemProps);
    PhysicalDeviceInfo physDevInfo = {};
    physDevInfo.instance = unbox_VkInstance(boxedInstance);
    instDispatch->vkGetPhysicalDeviceProperties(unboxedPhysDev, &physDevInfo.props);
    physDevInfo.memoryPropertiesHelper =
        std::make_unique<EmulatedPhysicalDeviceMemoryProperties>(hostMemProps, 0, features);

    StateBlock decoderStateBlock = {
        .physicalDevice = unboxedPhysDev,
        .physicalDeviceInfo = &physDevInfo,
        .device = unboxedDevice,
        .deviceDispatch = devDispatch,
        .queue = unbox_VkQueue(boxedQueue),
        .commandPool = unbox_VkCommandPool(boxedCmdPool),
    };

    ImageInfo dstImageInfo = {};
    dstImageInfo.device = unboxedDevice;
    dstImageInfo.imageCreateInfoShallow = vk_make_orphan_copy(imageCi);
    dstImageInfo.layout = savedLayout;
    dstImageInfo.memory = unbox_VkDeviceMemory(boxedDstMem);

    ASSERT_TRUE(loadImageContent(&snapshotStream, &decoderStateBlock, unbox_VkImage(boxedDstImage),
                                 &dstImageInfo));

    // Read back both the rendered source image and the restored destination image.
    auto readbackFromDecoderDevice = [&](VkImage img, VkImageLayout currentLayout) {
        TransferInfo transferInfo;
        EXPECT_TRUE(getFormatTransferInfo(kFormat, extent, &transferInfo));

        const VkBufferCreateInfo bufCi = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = transferInfo.stagingBufferCopySize,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VkBuffer readbackBuf = VK_NULL_HANDLE;
        EXPECT_EQ(devDispatch->vkCreateBuffer(unboxedDevice, &bufCi, nullptr, &readbackBuf),
                  VK_SUCCESS);
        VkMemoryRequirements bufReqs = {};
        devDispatch->vkGetBufferMemoryRequirements(unboxedDevice, readbackBuf, &bufReqs);
        uint32_t hostVisibleType = UINT32_MAX;
        for (uint32_t i = 0; i < hostMemProps.memoryTypeCount; ++i) {
            if ((bufReqs.memoryTypeBits & (1u << i)) &&
                (hostMemProps.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                hostVisibleType = i;
                break;
            }
        }
        const VkMemoryAllocateInfo bufAlloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = bufReqs.size,
            .memoryTypeIndex = hostVisibleType,
        };
        VkDeviceMemory readbackMem = VK_NULL_HANDLE;
        EXPECT_EQ(devDispatch->vkAllocateMemory(unboxedDevice, &bufAlloc, nullptr, &readbackMem),
                  VK_SUCCESS);
        EXPECT_EQ(devDispatch->vkBindBufferMemory(unboxedDevice, readbackBuf, readbackMem, 0),
                  VK_SUCCESS);
        void* mapped = nullptr;
        EXPECT_EQ(
            devDispatch->vkMapMemory(unboxedDevice, readbackMem, 0, VK_WHOLE_SIZE, 0, &mapped),
            VK_SUCCESS);
        std::memset(mapped, 0, transferInfo.stagingBufferCopySize);

        VkCommandBuffer cb = VK_NULL_HANDLE;
        EXPECT_EQ(devDispatch->vkAllocateCommandBuffers(unboxedDevice, &cmdBufAllocInfo, &cb),
                  VK_SUCCESS);
        EXPECT_EQ(devDispatch->vkBeginCommandBuffer(cb, &cmdBufBeginInfo), VK_SUCCESS);

        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = currentLayout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = img,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        devDispatch->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                          1, &barrier);
        for (const auto& region : transferInfo.bufferImageCopies) {
            devDispatch->vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                readbackBuf, 1, &region);
        }
        EXPECT_EQ(devDispatch->vkEndCommandBuffer(cb), VK_SUCCESS);
        const VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
        };
        EXPECT_EQ(devDispatch->vkQueueSubmit(unbox_VkQueue(boxedQueue), 1, &si, VK_NULL_HANDLE),
                  VK_SUCCESS);
        EXPECT_EQ(devDispatch->vkQueueWaitIdle(unbox_VkQueue(boxedQueue)), VK_SUCCESS);

        std::vector<uint8_t> bytes(transferInfo.stagingBufferCopySize);
        std::memcpy(bytes.data(), mapped, bytes.size());

        devDispatch->vkFreeCommandBuffers(unboxedDevice, unbox_VkCommandPool(boxedCmdPool), 1, &cb);
        devDispatch->vkUnmapMemory(unboxedDevice, readbackMem);
        devDispatch->vkDestroyBuffer(unboxedDevice, readbackBuf, nullptr);
        devDispatch->vkFreeMemory(unboxedDevice, readbackMem, nullptr);
        return bytes;
    };

    const std::vector<uint8_t> expectedPixels = readbackFromDecoderDevice(
        unbox_VkImage(boxedSrcImage), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const std::vector<uint8_t> actualPixels = readbackFromDecoderDevice(
        unbox_VkImage(boxedDstImage), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Ensure the rendered image content is non-zero and matches after snapshot restore.
    const std::vector<uint8_t> allZeros(expectedPixels.size(), 0);
    ASSERT_NE(expectedPixels, allZeros);
    EXPECT_EQ(actualPixels, expectedPixels);

    vkState->on_vkDestroyImageView(&pool, 0, boxedDevice, unbox_VkImageView(boxedSrcView), nullptr);
    vkState->on_vkDestroyImage(&pool, 0, boxedDevice, unbox_VkImage(boxedDstImage), nullptr);
    vkState->on_vkFreeMemory(&pool, 0, boxedDevice, unbox_VkDeviceMemory(boxedDstMem), nullptr);
    vkState->on_vkDestroyImage(&pool, 0, boxedDevice, unbox_VkImage(boxedSrcImage), nullptr);
    vkState->on_vkFreeMemory(&pool, 0, boxedDevice, unbox_VkDeviceMemory(boxedSrcMem), nullptr);
    vkState->on_vkDestroyCommandPool(&pool, 0, boxedDevice, unbox_VkCommandPool(boxedCmdPool),
                                     nullptr);
    vkState->on_vkDestroyDevice(&pool, 0, boxedDevice, nullptr);
    sVk->vkDestroyInstance(unbox_VkInstance(boxedInstance), nullptr);

    VkDecoderGlobalState::reset();
    gfxstream::base::setEnvironmentVariable("ANDROID_EMU_VK_NO_CLEANUP", "");
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
