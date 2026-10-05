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

#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/host/features.h"
#include "gfxstream/host/mem_stream.h"
#include "vk_decoder_internal_structs.h"
#include "vk_decoder_snapshot_utils.h"
#include "vk_emulated_physical_device_memory.h"
#include "vk_format_utils.h"
#include "vk_utils.h"
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

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
