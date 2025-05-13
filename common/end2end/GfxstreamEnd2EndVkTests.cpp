// Copyright (C) 2023 The Android Open Source Project
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

#include "GfxstreamEnd2EndTestUtils.h"
#include "GfxstreamEnd2EndTests.h"
#include "gfxstream/Expected.h"
#include "shaders/blit_sampler2d_frag.h"
#include "shaders/fullscreen_triangle_with_uv_vert.h"

namespace gfxstream {
namespace tests {
namespace {

using namespace std::chrono_literals;
using testing::Eq;
using testing::Ge;
using testing::IsEmpty;
using testing::IsNull;
using testing::IsTrue;
using testing::Ne;
using testing::Not;
using testing::NotNull;

template <typename DurationType>
constexpr uint64_t AsVkTimeout(DurationType duration) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

class GfxstreamEnd2EndVkTest : public GfxstreamEnd2EndTest {
   protected:
    // Gfxstream uses a vkQueueSubmit() to signal the VkFence and VkSemaphore used
    // in vkAcquireImageANDROID() calls. The guest is not aware of this and may try
    // to vkDestroyFence() and vkDestroySemaphore() (because the VkImage, VkFence,
    // and VkSemaphore may have been unused from the guest point of view) while the
    // host's command buffer is running. Gfxstream needs to ensure that it performs
    // the necessary tracking to not delete the VkFence and VkSemaphore while they
    // are in use on the host.
    void DoAcquireImageAndroidWithSync(bool withFence, bool withSemaphore) {
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

        auto vkAcquireImageANDROID =
            PFN_vkAcquireImageANDROID(device->getProcAddr("vkAcquireImageANDROID"));
        ASSERT_THAT(vkAcquireImageANDROID, NotNull());

        const vkhpp::ImageCreateInfo imageCreateInfo = {
            .pNext = &imageNativeBufferInfo,
            .imageType = vkhpp::ImageType::e2D,
            .extent.width = width,
            .extent.height = height,
            .extent.depth = 1,
            .mipLevels = 1,
            .arrayLayers = 1,
            .format = vkhpp::Format::eR8G8B8A8Unorm,
            .tiling = vkhpp::ImageTiling::eOptimal,
            .initialLayout = vkhpp::ImageLayout::eUndefined,
            .usage = vkhpp::ImageUsageFlagBits::eSampled | vkhpp::ImageUsageFlagBits::eTransferDst |
                     vkhpp::ImageUsageFlagBits::eTransferSrc,
            .sharingMode = vkhpp::SharingMode::eExclusive,
            .samples = vkhpp::SampleCountFlagBits::e1,
        };
        auto image = device->createImageUnique(imageCreateInfo).value;

        vkhpp::MemoryRequirements imageMemoryRequirements{};
        device->getImageMemoryRequirements(*image, &imageMemoryRequirements);

        const uint32_t imageMemoryIndex = utils::getMemoryType(
            physicalDevice, imageMemoryRequirements, vkhpp::MemoryPropertyFlagBits::eDeviceLocal);
        ASSERT_THAT(imageMemoryIndex, Not(Eq(-1)));

        const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
            .allocationSize = imageMemoryRequirements.size,
            .memoryTypeIndex = imageMemoryIndex,
        };

        auto imageMemory = device->allocateMemoryUnique(imageMemoryAllocateInfo).value;
        ASSERT_THAT(imageMemory, IsValidHandle());
        ASSERT_THAT(device->bindImageMemory(*image, *imageMemory, 0), IsVkSuccess());

        vkhpp::UniqueFence fence;
        if (withFence) {
            fence = device->createFenceUnique(vkhpp::FenceCreateInfo()).value;
        }

        vkhpp::UniqueSemaphore semaphore;
        if (withSemaphore) {
            semaphore = device->createSemaphoreUnique(vkhpp::SemaphoreCreateInfo()).value;
        }

        auto result = vkAcquireImageANDROID(*device, *image, -1, *semaphore, *fence);
        ASSERT_THAT(result, Eq(VK_SUCCESS));

        if (withFence) {
            fence.reset();
        }
        if (withSemaphore) {
            semaphore.reset();
        }
    }

    Result<Ok> DoCommandsImmediate(
        TypicalVkTestEnvironment& vk,
        const std::function<Result<Ok>(vkhpp::UniqueCommandBuffer&)>& func,
        const std::vector<vkhpp::UniqueSemaphore>& semaphores_wait = {},
        const std::vector<vkhpp::UniqueSemaphore>& semaphores_signal = {}) {
        const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
            .queueFamilyIndex = vk.queueFamilyIndex,
        };
        auto commandPool =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createCommandPoolUnique(commandPoolCreateInfo));

        const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
            .commandPool = *commandPool,
            .level = vkhpp::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };
        auto commandBuffers = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->allocateCommandBuffersUnique(commandBufferAllocateInfo));
        auto commandBuffer = std::move(commandBuffers[0]);

        const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
            .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
        };
        commandBuffer->begin(commandBufferBeginInfo);
        GFXSTREAM_EXPECT(func(commandBuffer));
        commandBuffer->end();

        std::vector<vkhpp::CommandBuffer> commandBufferHandles;
        commandBufferHandles.push_back(*commandBuffer);

        std::vector<vkhpp::Semaphore> semaphoreHandlesWait;
        semaphoreHandlesWait.reserve(semaphores_wait.size());
        for (const auto& s : semaphores_wait) {
            semaphoreHandlesWait.emplace_back(*s);
        }

        std::vector<vkhpp::Semaphore> semaphoreHandlesSignal;
        semaphoreHandlesSignal.reserve(semaphores_signal.size());
        for (const auto& s : semaphores_signal) {
            semaphoreHandlesSignal.emplace_back(*s);
        }

        vkhpp::SubmitInfo submitInfo = {
            .commandBufferCount = static_cast<uint32_t>(commandBufferHandles.size()),
            .pCommandBuffers = commandBufferHandles.data(),
        };
        if (!semaphoreHandlesWait.empty()) {
            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(semaphoreHandlesWait.size());
            submitInfo.pWaitSemaphores = semaphoreHandlesWait.data();
        }
        if (!semaphoreHandlesSignal.empty()) {
            submitInfo.signalSemaphoreCount = static_cast<uint32_t>(semaphoreHandlesSignal.size());
            submitInfo.pSignalSemaphores = semaphoreHandlesSignal.data();
        }
        vk.queue.submit(submitInfo);
        vk.queue.waitIdle();
        return Ok{};
    }

    struct BufferWithMemory {
        vkhpp::UniqueBuffer buffer;
        vkhpp::UniqueDeviceMemory bufferMemory;
    };
    Result<BufferWithMemory> CreateBuffer(TypicalVkTestEnvironment& vk,
                                          vkhpp::DeviceSize bufferSize,
                                          vkhpp::BufferUsageFlags bufferUsages,
                                          vkhpp::MemoryPropertyFlags bufferMemoryProperties,
                                          const uint8_t* data = nullptr,
                                          vkhpp::DeviceSize dataSize = 0) {
        const vkhpp::BufferCreateInfo bufferCreateInfo = {
            .size = static_cast<VkDeviceSize>(bufferSize),
            .usage = bufferUsages,
            .sharingMode = vkhpp::SharingMode::eExclusive,
        };
        auto buffer = GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createBufferUnique(bufferCreateInfo));

        vkhpp::MemoryRequirements bufferMemoryRequirements{};
        vk.device->getBufferMemoryRequirements(*buffer, &bufferMemoryRequirements);

        const auto bufferMemoryTypeIndex = utils::getMemoryType(
            vk.physicalDevice, bufferMemoryRequirements, bufferMemoryProperties);

        const vkhpp::MemoryAllocateInfo bufferMemoryAllocateInfo = {
            .allocationSize = bufferMemoryRequirements.size,
            .memoryTypeIndex = bufferMemoryTypeIndex,
        };
        auto bufferMemory =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->allocateMemoryUnique(bufferMemoryAllocateInfo));

        GFXSTREAM_EXPECT_VKHPP_RESULT(vk.device->bindBufferMemory(*buffer, *bufferMemory, 0));

        if (data != nullptr) {
            if (!(bufferUsages & vkhpp::BufferUsageFlagBits::eTransferDst)) {
                return gfxstream::unexpected(
                    "Must request transfer dst usage when creating buffer with data");
            }
            if (!(bufferMemoryProperties & vkhpp::MemoryPropertyFlagBits::eHostVisible)) {
                return gfxstream::unexpected(
                    "Must request host visible mem property when creating buffer with data");
            }

            void* mapped =
                GFXSTREAM_EXPECT_VKHPP_RV(vk.device->mapMemory(*bufferMemory, 0, bufferSize));

            std::memcpy(mapped, data, dataSize);

            if (!(bufferMemoryProperties & vkhpp::MemoryPropertyFlagBits::eHostVisible)) {
                vk.device->flushMappedMemoryRanges(vkhpp::MappedMemoryRange{
                    .memory = *bufferMemory,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                });
            }

            vk.device->unmapMemory(*bufferMemory);
        }

        return BufferWithMemory{
            .buffer = std::move(buffer),
            .bufferMemory = std::move(bufferMemory),
        };
    }

    struct ImageWithMemory {
        std::optional<vkhpp::UniqueSamplerYcbcrConversion> imageSamplerConversion;
        vkhpp::UniqueSampler imageSampler;
        vkhpp::UniqueDeviceMemory imageMemory;
        vkhpp::UniqueImage image;
        vkhpp::UniqueImageView imageView;
    };
    Result<ImageWithMemory> CreateImageWithAhb(TypicalVkTestEnvironment& vk,
                                               const ScopedAHardwareBuffer& ahb,
                                               const vkhpp::ImageUsageFlags usages,
                                               const vkhpp::ImageLayout layout) {
        const auto ahbHandle = mGralloc->getNativeHandle(ahb);
        if (ahbHandle == nullptr) {
            return gfxstream::unexpected("Failed to query native handle.");
        }
        const auto ahbFormat = mGralloc->getFormat(ahb);
        const bool ahbIsYuv = ahbFormat == GFXSTREAM_AHB_FORMAT_YV12 ||
                              ahbFormat == GFXSTREAM_AHB_FORMAT_Y8Cb8Cr8_420;

        auto vkGetAndroidHardwareBufferPropertiesANDROID =
            reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
                vk.device->getProcAddr("vkGetAndroidHardwareBufferPropertiesANDROID"));
        if (vkGetAndroidHardwareBufferPropertiesANDROID == nullptr) {
            return gfxstream::unexpected(
                "Failed to query vkGetAndroidHardwareBufferPropertiesANDROID().");
        }
        VkAndroidHardwareBufferFormatPropertiesANDROID ahbFormatProperties = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
            .pNext = nullptr,
        };
        VkAndroidHardwareBufferPropertiesANDROID ahbProperties = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
            .pNext = &ahbFormatProperties,
        };
        if (vkGetAndroidHardwareBufferPropertiesANDROID(*vk.device, ahb, &ahbProperties) !=
            VK_SUCCESS) {
            return gfxstream::unexpected("Failed to query ahb properties.");
        }

        const VkExternalFormatANDROID externalFormat = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
            .externalFormat = ahbFormatProperties.externalFormat,
        };

        std::optional<vkhpp::UniqueSamplerYcbcrConversion> imageSamplerConversion;
        std::optional<vkhpp::SamplerYcbcrConversionInfo> samplerConversionInfo;
        if (ahbIsYuv) {
            const vkhpp::SamplerYcbcrConversionCreateInfo conversionCreateInfo = {
                .pNext = &externalFormat,
                .format = static_cast<vkhpp::Format>(ahbFormatProperties.format),
                .ycbcrModel = static_cast<vkhpp::SamplerYcbcrModelConversion>(
                    ahbFormatProperties.suggestedYcbcrModel),
                .ycbcrRange =
                    static_cast<vkhpp::SamplerYcbcrRange>(ahbFormatProperties.suggestedYcbcrRange),
                .components =
                    {
                        .r = static_cast<vkhpp::ComponentSwizzle>(
                            ahbFormatProperties.samplerYcbcrConversionComponents.r),
                        .g = static_cast<vkhpp::ComponentSwizzle>(
                            ahbFormatProperties.samplerYcbcrConversionComponents.g),
                        .b = static_cast<vkhpp::ComponentSwizzle>(
                            ahbFormatProperties.samplerYcbcrConversionComponents.b),
                        .a = static_cast<vkhpp::ComponentSwizzle>(
                            ahbFormatProperties.samplerYcbcrConversionComponents.a),
                    },
                .xChromaOffset =
                    static_cast<vkhpp::ChromaLocation>(ahbFormatProperties.suggestedXChromaOffset),
                .yChromaOffset =
                    static_cast<vkhpp::ChromaLocation>(ahbFormatProperties.suggestedYChromaOffset),
                .chromaFilter = vkhpp::Filter::eNearest,
                .forceExplicitReconstruction = VK_FALSE,
            };
            imageSamplerConversion = GFXSTREAM_EXPECT_VKHPP_RV(
                vk.device->createSamplerYcbcrConversionUnique(conversionCreateInfo));

            samplerConversionInfo = vkhpp::SamplerYcbcrConversionInfo{
                .conversion = **imageSamplerConversion,
            };
        }
        const vkhpp::SamplerCreateInfo samplerCreateInfo = {
            .pNext = ahbIsYuv ? &samplerConversionInfo : nullptr,
            .magFilter = vkhpp::Filter::eNearest,
            .minFilter = vkhpp::Filter::eNearest,
            .mipmapMode = vkhpp::SamplerMipmapMode::eNearest,
            .addressModeU = vkhpp::SamplerAddressMode::eClampToEdge,
            .addressModeV = vkhpp::SamplerAddressMode::eClampToEdge,
            .addressModeW = vkhpp::SamplerAddressMode::eClampToEdge,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = vkhpp::CompareOp::eLessOrEqual,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = vkhpp::BorderColor::eIntTransparentBlack,
            .unnormalizedCoordinates = VK_FALSE,
        };
        auto imageSampler =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createSamplerUnique(samplerCreateInfo));

        const VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext = &externalFormat,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
        };
        const vkhpp::ImageCreateInfo imageCreateInfo = {
            .pNext = &externalMemoryImageCreateInfo,
            .imageType = vkhpp::ImageType::e2D,
            .format = static_cast<vkhpp::Format>(ahbFormatProperties.format),
            .extent =
                {
                    .width = mGralloc->getWidth(ahb),
                    .height = mGralloc->getHeight(ahb),
                    .depth = 1,
                },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vkhpp::SampleCountFlagBits::e1,
            .tiling = vkhpp::ImageTiling::eOptimal,
            .usage = usages,
            .sharingMode = vkhpp::SharingMode::eExclusive,
            .initialLayout = vkhpp::ImageLayout::eUndefined,
        };
        auto image = GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createImageUnique(imageCreateInfo));

        const vkhpp::MemoryRequirements imageMemoryRequirements = {
            .size = ahbProperties.allocationSize,
            .alignment = 0,
            .memoryTypeBits = ahbProperties.memoryTypeBits,
        };
        const uint32_t imageMemoryIndex =
            utils::getMemoryType(vk.physicalDevice, imageMemoryRequirements,
                                 vkhpp::MemoryPropertyFlagBits::eDeviceLocal);

        const vkhpp::ImportAndroidHardwareBufferInfoANDROID importAhbInfo = {
            .buffer = ahb,
        };
        const vkhpp::MemoryDedicatedAllocateInfo importMemoryDedicatedInfo = {
            .pNext = &importAhbInfo,
            .image = *image,
        };
        const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
            .pNext = &importMemoryDedicatedInfo,
            .allocationSize = imageMemoryRequirements.size,
            .memoryTypeIndex = imageMemoryIndex,
        };
        auto imageMemory =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->allocateMemoryUnique(imageMemoryAllocateInfo));
        vk.device->bindImageMemory(*image, *imageMemory, 0);

        const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
            .pNext = &samplerConversionInfo,
            .image = *image,
            .viewType = vkhpp::ImageViewType::e2D,
            .format = static_cast<vkhpp::Format>(ahbFormatProperties.format),
            .components =
                {
                    .r = vkhpp::ComponentSwizzle::eIdentity,
                    .g = vkhpp::ComponentSwizzle::eIdentity,
                    .b = vkhpp::ComponentSwizzle::eIdentity,
                    .a = vkhpp::ComponentSwizzle::eIdentity,
                },
            .subresourceRange =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        auto imageView =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createImageViewUnique(imageViewCreateInfo));

        GFXSTREAM_EXPECT(DoCommandsImmediate(vk, [&](vkhpp::UniqueCommandBuffer& cmd) {
            const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
                vkhpp::ImageMemoryBarrier{
                    .srcAccessMask = {},
                    .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
                    .oldLayout = vkhpp::ImageLayout::eUndefined,
                    .newLayout = layout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = *image,
                    .subresourceRange =
                        {
                            .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },

                },
            };
            cmd->pipelineBarrier(
                /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                /*dependencyFlags=*/{},
                /*memoryBarriers=*/{},
                /*bufferMemoryBarriers=*/{},
                /*imageMemoryBarriers=*/imageMemoryBarriers);
            return Ok{};
        }));

        return ImageWithMemory{
            .imageSamplerConversion = std::move(imageSamplerConversion),
            .imageSampler = std::move(imageSampler),
            .imageMemory = std::move(imageMemory),
            .image = std::move(image),
            .imageView = std::move(imageView),
        };
    }

    Result<ImageWithMemory> CreateImage(TypicalVkTestEnvironment& vk, uint32_t width,
                                        uint32_t height, vkhpp::Format format,
                                        vkhpp::ImageUsageFlags usages,
                                        vkhpp::MemoryPropertyFlags memoryProperties,
                                        vkhpp::ImageLayout returnedLayout) {
        const vkhpp::ImageCreateInfo imageCreateInfo = {
            .imageType = vkhpp::ImageType::e2D,
            .format = format,
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
            .usage = usages,
            .sharingMode = vkhpp::SharingMode::eExclusive,
            .initialLayout = vkhpp::ImageLayout::eUndefined,
        };
        auto image = GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createImageUnique(imageCreateInfo));

        const auto memoryRequirements = vk.device->getImageMemoryRequirements(*image);
        const uint32_t memoryIndex =
            utils::getMemoryType(vk.physicalDevice, memoryRequirements, memoryProperties);

        const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = memoryIndex,
        };
        auto imageMemory =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->allocateMemoryUnique(imageMemoryAllocateInfo));

        vk.device->bindImageMemory(*image, *imageMemory, 0);

        const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
            .image = *image,
            .viewType = vkhpp::ImageViewType::e2D,
            .format = format,
            .components =
                {
                    .r = vkhpp::ComponentSwizzle::eIdentity,
                    .g = vkhpp::ComponentSwizzle::eIdentity,
                    .b = vkhpp::ComponentSwizzle::eIdentity,
                    .a = vkhpp::ComponentSwizzle::eIdentity,
                },
            .subresourceRange =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        auto imageView =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createImageViewUnique(imageViewCreateInfo));

        GFXSTREAM_EXPECT(DoCommandsImmediate(vk, [&](vkhpp::UniqueCommandBuffer& cmd) {
            const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
                vkhpp::ImageMemoryBarrier{
                    .srcAccessMask = {},
                    .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
                    .oldLayout = vkhpp::ImageLayout::eUndefined,
                    .newLayout = returnedLayout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = *image,
                    .subresourceRange =
                        {
                            .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                },
            };
            cmd->pipelineBarrier(
                /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                /*dependencyFlags=*/{},
                /*memoryBarriers=*/{},
                /*bufferMemoryBarriers=*/{},
                /*imageMemoryBarriers=*/imageMemoryBarriers);

            return Ok{};
        }));

        return ImageWithMemory{
            .image = std::move(image),
            .imageMemory = std::move(imageMemory),
            .imageView = std::move(imageView),
        };
    }

    struct FramebufferWithAttachments {
        std::optional<ImageWithMemory> colorAttachment;
        std::optional<ImageWithMemory> depthAttachment;
        vkhpp::UniqueRenderPass renderpass;
        vkhpp::UniqueFramebuffer framebuffer;
    };
    Result<FramebufferWithAttachments> CreateFramebuffer(
        TypicalVkTestEnvironment& vk, uint32_t width, uint32_t height,
        vkhpp::Format colorAttachmentFormat = vkhpp::Format::eUndefined,
        vkhpp::Format depthAttachmentFormat = vkhpp::Format::eUndefined) {
        std::optional<ImageWithMemory> colorAttachment;
        if (colorAttachmentFormat != vkhpp::Format::eUndefined) {
            colorAttachment =
                GFXSTREAM_EXPECT(CreateImage(vk, width, height, colorAttachmentFormat,
                                             vkhpp::ImageUsageFlagBits::eColorAttachment |
                                                 vkhpp::ImageUsageFlagBits::eTransferSrc,
                                             vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                                             vkhpp::ImageLayout::eColorAttachmentOptimal));
        }

        std::optional<ImageWithMemory> depthAttachment;
        if (depthAttachmentFormat != vkhpp::Format::eUndefined) {
            depthAttachment =
                GFXSTREAM_EXPECT(CreateImage(vk, width, height, depthAttachmentFormat,
                                             vkhpp::ImageUsageFlagBits::eDepthStencilAttachment |
                                                 vkhpp::ImageUsageFlagBits::eTransferSrc,
                                             vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                                             vkhpp::ImageLayout::eDepthStencilAttachmentOptimal));
        }

        std::vector<vkhpp::AttachmentDescription> attachments;

        std::optional<vkhpp::AttachmentReference> colorAttachmentReference;
        if (colorAttachmentFormat != vkhpp::Format::eUndefined) {
            attachments.push_back(vkhpp::AttachmentDescription{
                .format = colorAttachmentFormat,
                .samples = vkhpp::SampleCountFlagBits::e1,
                .loadOp = vkhpp::AttachmentLoadOp::eClear,
                .storeOp = vkhpp::AttachmentStoreOp::eStore,
                .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
                .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
                .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
                .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
            });

            colorAttachmentReference = vkhpp::AttachmentReference{
                .attachment = static_cast<uint32_t>(attachments.size() - 1),
                .layout = vkhpp::ImageLayout::eColorAttachmentOptimal,
            };
        }

        std::optional<vkhpp::AttachmentReference> depthAttachmentReference;
        if (depthAttachmentFormat != vkhpp::Format::eUndefined) {
            attachments.push_back(vkhpp::AttachmentDescription{
                .format = depthAttachmentFormat,
                .samples = vkhpp::SampleCountFlagBits::e1,
                .loadOp = vkhpp::AttachmentLoadOp::eClear,
                .storeOp = vkhpp::AttachmentStoreOp::eStore,
                .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
                .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
                .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
                .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
            });

            depthAttachmentReference = vkhpp::AttachmentReference{
                .attachment = static_cast<uint32_t>(attachments.size() - 1),
                .layout = vkhpp::ImageLayout::eDepthStencilAttachmentOptimal,
            };
        }

        vkhpp::SubpassDependency dependency = {
            .srcSubpass = 0,
            .dstSubpass = 0,
            .srcStageMask = {},
            .dstStageMask = vkhpp::PipelineStageFlagBits::eFragmentShader,
            .srcAccessMask = {},
            .dstAccessMask = vkhpp::AccessFlagBits::eInputAttachmentRead,
            .dependencyFlags = vkhpp::DependencyFlagBits::eByRegion,
        };
        if (colorAttachmentFormat != vkhpp::Format::eUndefined) {
            dependency.srcStageMask |= vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.dstStageMask |= vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask |= vkhpp::AccessFlagBits::eColorAttachmentWrite;
        }
        if (depthAttachmentFormat != vkhpp::Format::eUndefined) {
            dependency.srcStageMask |= vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.dstStageMask |= vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask |= vkhpp::AccessFlagBits::eColorAttachmentWrite;
        }

        vkhpp::SubpassDescription subpass = {
            .pipelineBindPoint = vkhpp::PipelineBindPoint::eGraphics,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 0,
            .pColorAttachments = nullptr,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
            .pPreserveAttachments = nullptr,
        };
        if (colorAttachmentFormat != vkhpp::Format::eUndefined) {
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &*colorAttachmentReference;
        }
        if (depthAttachmentFormat != vkhpp::Format::eUndefined) {
            subpass.pDepthStencilAttachment = &*depthAttachmentReference;
        }

        const vkhpp::RenderPassCreateInfo renderpassCreateInfo = {
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency,
        };
        auto renderpass =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createRenderPassUnique(renderpassCreateInfo));

        std::vector<vkhpp::ImageView> framebufferAttachments;
        if (colorAttachment) {
            framebufferAttachments.push_back(*colorAttachment->imageView);
        }
        if (depthAttachment) {
            framebufferAttachments.push_back(*depthAttachment->imageView);
        }
        const vkhpp::FramebufferCreateInfo framebufferCreateInfo = {
            .renderPass = *renderpass,
            .attachmentCount = static_cast<uint32_t>(framebufferAttachments.size()),
            .pAttachments = framebufferAttachments.data(),
            .width = width,
            .height = height,
            .layers = 1,
        };
        auto framebuffer =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createFramebufferUnique(framebufferCreateInfo));

        return FramebufferWithAttachments{
            .colorAttachment = std::move(colorAttachment),
            .depthAttachment = std::move(depthAttachment),
            .renderpass = std::move(renderpass),
            .framebuffer = std::move(framebuffer),
        };
    }

    struct DescriptorContents {
        uint32_t binding = 0;
        struct Image {
            vkhpp::ImageView imageView;
            vkhpp::ImageLayout imageLayout;
            vkhpp::Sampler imageSampler;
        };
        std::optional<Image> image;
    };
    struct DescriptorSetBundle {
        vkhpp::UniqueDescriptorPool pool;
        vkhpp::UniqueDescriptorSetLayout layout;
        vkhpp::UniqueDescriptorSet ds;
    };
    Result<DescriptorSetBundle> CreateDescriptorSet(
        TypicalVkTestEnvironment& vk,
        const std::vector<vkhpp::DescriptorSetLayoutBinding>& bindings,
        const std::vector<DescriptorContents> contents) {
        std::unordered_map<vkhpp::DescriptorType, uint32_t> descriptorTypeToSizes;
        for (const auto& binding : bindings) {
            descriptorTypeToSizes[binding.descriptorType] += binding.descriptorCount;
        }
        std::vector<vkhpp::DescriptorPoolSize> descriptorPoolSizes;
        for (const auto& [descriptorType, descriptorCount] : descriptorTypeToSizes) {
            descriptorPoolSizes.push_back(vkhpp::DescriptorPoolSize{
                .type = descriptorType,
                .descriptorCount = descriptorCount,
            });
        }
        const vkhpp::DescriptorPoolCreateInfo descriptorPoolCreateInfo = {
            .flags = vkhpp::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
            .pPoolSizes = descriptorPoolSizes.data(),
        };
        auto descriptorSetPool = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->createDescriptorPoolUnique(descriptorPoolCreateInfo));

        const vkhpp::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        auto descriptorSetLayout = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->createDescriptorSetLayoutUnique(descriptorSetLayoutCreateInfo));

        const vkhpp::DescriptorSetLayout descriptorSetLayoutHandle = *descriptorSetLayout;
        const vkhpp::DescriptorSetAllocateInfo descriptorSetAllocateInfo = {
            .descriptorPool = *descriptorSetPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayoutHandle,
        };
        auto descriptorSets = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->allocateDescriptorSetsUnique(descriptorSetAllocateInfo));
        auto descriptorSet(std::move(descriptorSets[0]));

        std::vector<std::unique_ptr<vkhpp::DescriptorImageInfo>> descriptorImageInfos;
        std::vector<vkhpp::WriteDescriptorSet> descriptorSetWrites;
        for (const auto& content : contents) {
            if (content.image) {
                descriptorImageInfos.emplace_back(new vkhpp::DescriptorImageInfo{
                    .sampler = content.image->imageSampler,
                    .imageView = content.image->imageView,
                    .imageLayout = content.image->imageLayout,
                });
                descriptorSetWrites.emplace_back(vkhpp::WriteDescriptorSet{
                    .dstSet = *descriptorSet,
                    .dstBinding = content.binding,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = descriptorImageInfos.back().get(),
                });
            } else {
                return gfxstream::unexpected("Unhandled descriptor type");
                ;
            }
        }
        vk.device->updateDescriptorSets(descriptorSetWrites, {});

        return DescriptorSetBundle{
            .pool = std::move(descriptorSetPool),
            .layout = std::move(descriptorSetLayout),
            .ds = std::move(descriptorSet),
        };
    }

    struct PipelineParams {
        std::vector<uint32_t> vert;
        std::vector<uint32_t> frag;
        std::vector<DescriptorSetBundle*> descriptorSets;
        const FramebufferWithAttachments* framebuffer = nullptr;
    };
    struct PipelineBundle {
        vkhpp::UniqueShaderModule vert;
        vkhpp::UniqueShaderModule frag;
        vkhpp::UniquePipelineLayout pipelineLayout;
        vkhpp::UniquePipeline pipeline;
    };
    Result<PipelineBundle> CreatePipeline(TypicalVkTestEnvironment& vk,
                                          const PipelineParams& params) {
        const vkhpp::ShaderModuleCreateInfo vertShaderCreateInfo = {
            .codeSize = params.vert.size() * sizeof(uint32_t),
            .pCode = params.vert.data(),
        };
        auto vertShaderModule =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createShaderModuleUnique(vertShaderCreateInfo));

        const vkhpp::ShaderModuleCreateInfo fragShaderCreateInfo = {
            .codeSize = params.frag.size() * sizeof(uint32_t),
            .pCode = params.frag.data(),
        };
        auto fragShaderModule =
            GFXSTREAM_EXPECT_VKHPP_RV(vk.device->createShaderModuleUnique(fragShaderCreateInfo));

        const std::vector<vkhpp::PipelineShaderStageCreateInfo> pipelineStages = {
            vkhpp::PipelineShaderStageCreateInfo{
                .stage = vkhpp::ShaderStageFlagBits::eVertex,
                .module = *vertShaderModule,
                .pName = "main",
            },
            vkhpp::PipelineShaderStageCreateInfo{
                .stage = vkhpp::ShaderStageFlagBits::eFragment,
                .module = *fragShaderModule,
                .pName = "main",
            },
        };

        std::vector<vkhpp::DescriptorSetLayout> descriptorSetLayoutHandles;
        for (const auto* descriptorSet : params.descriptorSets) {
            descriptorSetLayoutHandles.push_back(*descriptorSet->layout);
        }
        const vkhpp::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
            .setLayoutCount = static_cast<uint32_t>(descriptorSetLayoutHandles.size()),
            .pSetLayouts = descriptorSetLayoutHandles.data(),
        };
        auto pipelineLayout = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->createPipelineLayoutUnique(pipelineLayoutCreateInfo));

        const vkhpp::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo = {};
        const vkhpp::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo = {
            .topology = vkhpp::PrimitiveTopology::eTriangleList,
        };
        const vkhpp::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo = {
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
        };
        const vkhpp::PipelineRasterizationStateCreateInfo pipelineRasterStateCreateInfo = {
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = vkhpp::PolygonMode::eFill,
            .cullMode = {},
            .frontFace = vkhpp::FrontFace::eCounterClockwise,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        const vkhpp::SampleMask pipelineSampleMask = 65535;
        const vkhpp::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo = {
            .rasterizationSamples = vkhpp::SampleCountFlagBits::e1,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = &pipelineSampleMask,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        const vkhpp::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo = {
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = vkhpp::CompareOp::eLess,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front =
                {
                    .failOp = vkhpp::StencilOp::eKeep,
                    .passOp = vkhpp::StencilOp::eKeep,
                    .depthFailOp = vkhpp::StencilOp::eKeep,
                    .compareOp = vkhpp::CompareOp::eAlways,
                    .compareMask = 0,
                    .writeMask = 0,
                    .reference = 0,
                },
            .back =
                {
                    .failOp = vkhpp::StencilOp::eKeep,
                    .passOp = vkhpp::StencilOp::eKeep,
                    .depthFailOp = vkhpp::StencilOp::eKeep,
                    .compareOp = vkhpp::CompareOp::eAlways,
                    .compareMask = 0,
                    .writeMask = 0,
                    .reference = 0,
                },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 0.0f,
        };
        const std::vector<vkhpp::PipelineColorBlendAttachmentState> pipelineColorBlendAttachments =
            {
                vkhpp::PipelineColorBlendAttachmentState{
                    .blendEnable = VK_FALSE,
                    .srcColorBlendFactor = vkhpp::BlendFactor::eOne,
                    .dstColorBlendFactor = vkhpp::BlendFactor::eOneMinusSrcAlpha,
                    .colorBlendOp = vkhpp::BlendOp::eAdd,
                    .srcAlphaBlendFactor = vkhpp::BlendFactor::eOne,
                    .dstAlphaBlendFactor = vkhpp::BlendFactor::eOneMinusSrcAlpha,
                    .alphaBlendOp = vkhpp::BlendOp::eAdd,
                    .colorWriteMask =
                        vkhpp::ColorComponentFlagBits::eR | vkhpp::ColorComponentFlagBits::eG |
                        vkhpp::ColorComponentFlagBits::eB | vkhpp::ColorComponentFlagBits::eA,
                },
            };
        const vkhpp::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo = {
            .logicOpEnable = VK_FALSE,
            .logicOp = vkhpp::LogicOp::eCopy,
            .attachmentCount = static_cast<uint32_t>(pipelineColorBlendAttachments.size()),
            .pAttachments = pipelineColorBlendAttachments.data(),
            .blendConstants = {{
                0.0f,
                0.0f,
                0.0f,
                0.0f,
            }},
        };
        const std::vector<vkhpp::DynamicState> pipelineDynamicStates = {
            vkhpp::DynamicState::eViewport,
            vkhpp::DynamicState::eScissor,
        };
        const vkhpp::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {
            .dynamicStateCount = static_cast<uint32_t>(pipelineDynamicStates.size()),
            .pDynamicStates = pipelineDynamicStates.data(),
        };
        const vkhpp::GraphicsPipelineCreateInfo pipelineCreateInfo = {
            .stageCount = static_cast<uint32_t>(pipelineStages.size()),
            .pStages = pipelineStages.data(),
            .pVertexInputState = &pipelineVertexInputStateCreateInfo,
            .pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo,
            .pTessellationState = nullptr,
            .pViewportState = &pipelineViewportStateCreateInfo,
            .pRasterizationState = &pipelineRasterStateCreateInfo,
            .pMultisampleState = &pipelineMultisampleStateCreateInfo,
            .pDepthStencilState = &pipelineDepthStencilStateCreateInfo,
            .pColorBlendState = &pipelineColorBlendStateCreateInfo,
            .pDynamicState = &pipelineDynamicStateCreateInfo,
            .layout = *pipelineLayout,
            .renderPass = *params.framebuffer->renderpass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        auto pipeline = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->createGraphicsPipelineUnique({}, pipelineCreateInfo));

        return PipelineBundle{
            .vert = std::move(vertShaderModule),
            .frag = std::move(fragShaderModule),
            .pipelineLayout = std::move(pipelineLayout),
            .pipeline = std::move(pipeline),
        };
    }

    Result<Image> DownloadImage(TypicalVkTestEnvironment& vk, uint32_t width, uint32_t height,
                                const vkhpp::UniqueImage& image, vkhpp::ImageLayout currentLayout,
                                vkhpp::ImageLayout returnedLayout) {
        static constexpr const VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;
        auto stagingBuffer = GFXSTREAM_EXPECT(CreateBuffer(
            vk, kStagingBufferSize,
            vkhpp::BufferUsageFlagBits::eTransferDst | vkhpp::BufferUsageFlagBits::eTransferSrc,
            vkhpp::MemoryPropertyFlagBits::eHostVisible |
                vkhpp::MemoryPropertyFlagBits::eHostCoherent));

        GFXSTREAM_EXPECT(DoCommandsImmediate(vk, [&](vkhpp::UniqueCommandBuffer& cmd) {
            if (currentLayout != vkhpp::ImageLayout::eTransferSrcOptimal) {
                const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
                    vkhpp::ImageMemoryBarrier{
                        .srcAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                                         vkhpp::AccessFlagBits::eMemoryWrite,
                        .dstAccessMask = vkhpp::AccessFlagBits::eTransferRead,
                        .oldLayout = currentLayout,
                        .newLayout = vkhpp::ImageLayout::eTransferSrcOptimal,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = *image,
                        .subresourceRange =
                            {
                                .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                    },
                };
                cmd->pipelineBarrier(
                    /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                    /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                    /*dependencyFlags=*/{},
                    /*memoryBarriers=*/{},
                    /*bufferMemoryBarriers=*/{},
                    /*imageMemoryBarriers=*/imageMemoryBarriers);
            }

            const std::vector<vkhpp::BufferImageCopy> regions = {
                vkhpp::BufferImageCopy{
                    .bufferOffset = 0,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource =
                        {
                            .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                            .mipLevel = 0,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    .imageOffset =
                        {
                            .x = 0,
                            .y = 0,
                            .z = 0,
                        },
                    .imageExtent =
                        {
                            .width = width,
                            .height = height,
                            .depth = 1,
                        },
                },
            };
            cmd->copyImageToBuffer(*image, vkhpp::ImageLayout::eTransferSrcOptimal,
                                   *stagingBuffer.buffer, regions);

            if (returnedLayout != vkhpp::ImageLayout::eTransferSrcOptimal) {
                const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
                    vkhpp::ImageMemoryBarrier{
                        .srcAccessMask = vkhpp::AccessFlagBits::eTransferRead,
                        .dstAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                                         vkhpp::AccessFlagBits::eMemoryWrite,
                        .oldLayout = vkhpp::ImageLayout::eTransferSrcOptimal,
                        .newLayout = returnedLayout,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = *image,
                        .subresourceRange =
                            {
                                .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                    },
                };
                cmd->pipelineBarrier(
                    /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                    /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
                    /*dependencyFlags=*/{},
                    /*memoryBarriers=*/{},
                    /*bufferMemoryBarriers=*/{},
                    /*imageMemoryBarriers=*/imageMemoryBarriers);
            }
            return Ok{};
        }));

        std::vector<uint32_t> outPixels;
        outPixels.resize(width * height);

        auto* mapped = GFXSTREAM_EXPECT_VKHPP_RV(
            vk.device->mapMemory(*stagingBuffer.bufferMemory, 0, VK_WHOLE_SIZE));
        std::memcpy(outPixels.data(), mapped, sizeof(uint32_t) * outPixels.size());
        vk.device->unmapMemory(*stagingBuffer.bufferMemory);

        return Image{
            .width = width,
            .height = height,
            .pixels = outPixels,
        };
    }

    void DoFillAndRenderFromAhb(uint32_t ahbFormat) {
        const uint32_t width = 1920;
        const uint32_t height = 1080;
        const auto goldenPixel = PixelR8G8B8A8(0, 255, 255, 255);
        const auto badPixel = PixelR8G8B8A8(0, 0, 0, 255);

        // Bind to a placeholder ahb before rebinding to the real one.
        // This is to test the behavior of descriptors and make sure
        // it removes the references to the old one when overwritten.
        auto deletedAhb =
            GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(*mGralloc, width, height, ahbFormat));

        GFXSTREAM_ASSERT(FillAhb(deletedAhb, badPixel));

        auto ahb =
            GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(*mGralloc, width, height, ahbFormat));

        GFXSTREAM_ASSERT(FillAhb(ahb, goldenPixel));

        const vkhpp::PhysicalDeviceVulkan11Features deviceFeatures = {
            .samplerYcbcrConversion = VK_TRUE,
        };
        auto vk = GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment({
            .deviceExtensions = {{VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME}},
            .deviceCreateInfoPNext = &deviceFeatures,
        }));

        auto deletedAhbImage =
            GFXSTREAM_ASSERT(CreateImageWithAhb(vk, deletedAhb, vkhpp::ImageUsageFlagBits::eSampled,
                                                vkhpp::ImageLayout::eShaderReadOnlyOptimal));

        auto ahbImage =
            GFXSTREAM_ASSERT(CreateImageWithAhb(vk, ahb, vkhpp::ImageUsageFlagBits::eSampled,
                                                vkhpp::ImageLayout::eShaderReadOnlyOptimal));

        auto framebuffer = GFXSTREAM_ASSERT(CreateFramebuffer(
            vk, width, height, /*colorAttachmentFormat=*/vkhpp::Format::eR8G8B8A8Unorm));

        const vkhpp::Sampler ahbSamplerHandle = *ahbImage.imageSampler;
        auto descriptorSet0 = GFXSTREAM_ASSERT(
            CreateDescriptorSet(vk,
                                /*bindings=*/
                                {{
                                    .binding = 0,
                                    .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
                                    .descriptorCount = 1,
                                    .stageFlags = vkhpp::ShaderStageFlagBits::eFragment,
                                    .pImmutableSamplers = &ahbSamplerHandle,
                                }},
                                /*writes=*/
                                {{
                                    .binding = 0,
                                    .image = {{
                                        .imageView = *deletedAhbImage.imageView,
                                        .imageLayout = vkhpp::ImageLayout::eShaderReadOnlyOptimal,
                                        .imageSampler = *deletedAhbImage.imageSampler,
                                    }},
                                }}));

        auto pipeline =
            GFXSTREAM_ASSERT(CreatePipeline(vk, {
                                                    .vert = kFullscreenTriangleWithUVVert,
                                                    .frag = kBlitSampler2dFrag,
                                                    .descriptorSets = {&descriptorSet0},
                                                    .framebuffer = &framebuffer,
                                                }));

        std::vector<vkhpp::WriteDescriptorSet> descriptorSetWrites;
        vkhpp::DescriptorImageInfo descriptorImageInfo = {
            .imageView = *ahbImage.imageView,
            .imageLayout = vkhpp::ImageLayout::eShaderReadOnlyOptimal,
            .sampler = *ahbImage.imageSampler,
        };
        descriptorSetWrites.emplace_back(vkhpp::WriteDescriptorSet{
            .dstSet = *descriptorSet0.ds,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &descriptorImageInfo,
        });
        vk.device->updateDescriptorSets(descriptorSetWrites, {});
        deletedAhbImage = {};
        deletedAhb = {};

        GFXSTREAM_ASSERT(DoCommandsImmediate(vk, [&](vkhpp::UniqueCommandBuffer& cmd) {
            const std::vector<vkhpp::ClearValue> renderPassBeginClearValues = {
                vkhpp::ClearValue{
                    .color =
                        {
                            .float32 = {{
                                1.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                            }},
                        },
                },
            };
            const vkhpp::RenderPassBeginInfo renderPassBeginInfo = {
                .renderPass = *framebuffer.renderpass,
                .framebuffer = *framebuffer.framebuffer,
                .renderArea =
                    {
                        .offset =
                            {
                                .x = 0,
                                .y = 0,
                            },
                        .extent =
                            {
                                .width = width,
                                .height = height,
                            },
                    },
                .clearValueCount = static_cast<uint32_t>(renderPassBeginClearValues.size()),
                .pClearValues = renderPassBeginClearValues.data(),
            };
            cmd->beginRenderPass(renderPassBeginInfo, vkhpp::SubpassContents::eInline);
            cmd->bindPipeline(vkhpp::PipelineBindPoint::eGraphics, *pipeline.pipeline);
            cmd->bindDescriptorSets(vkhpp::PipelineBindPoint::eGraphics, *pipeline.pipelineLayout,
                                    /*firstSet=*/0, {*descriptorSet0.ds},
                                    /*dynamicOffsets=*/{});
            const vkhpp::Viewport viewport = {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            cmd->setViewport(0, {viewport});
            const vkhpp::Rect2D scissor = {
                .offset =
                    {
                        .x = 0,
                        .y = 0,
                    },
                .extent =
                    {
                        .width = width,
                        .height = height,
                    },
            };
            cmd->setScissor(0, {scissor});
            cmd->draw(3, 1, 0, 0);
            cmd->endRenderPass();
            return Ok{};
        }));

        const auto actualImage = GFXSTREAM_ASSERT(
            DownloadImage(vk, width, height, framebuffer.colorAttachment->image,
                          /*currentLayout=*/vkhpp::ImageLayout::eColorAttachmentOptimal,
                          /*returnedLayout=*/vkhpp::ImageLayout::eColorAttachmentOptimal));

        const auto expectedImage = ImageFromColor(width, height, goldenPixel);
        EXPECT_THAT(AreImagesSimilar(expectedImage, actualImage), IsTrue());
    }
};

TEST_P(GfxstreamEnd2EndVkTest, Basic) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
}

TEST_P(GfxstreamEnd2EndVkTest, ImportAHB) {
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

    auto vkQueueSignalReleaseImageANDROID = PFN_vkQueueSignalReleaseImageANDROID(
        device->getProcAddr("vkQueueSignalReleaseImageANDROID"));
    ASSERT_THAT(vkQueueSignalReleaseImageANDROID, NotNull());

    const vkhpp::ImageCreateInfo imageCreateInfo = {
        .pNext = &imageNativeBufferInfo,
        .imageType = vkhpp::ImageType::e2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = vkhpp::Format::eR8G8B8A8Unorm,
        .tiling = vkhpp::ImageTiling::eOptimal,
        .initialLayout = vkhpp::ImageLayout::eUndefined,
        .usage = vkhpp::ImageUsageFlagBits::eSampled |
                 vkhpp::ImageUsageFlagBits::eTransferDst |
                 vkhpp::ImageUsageFlagBits::eTransferSrc,
        .sharingMode = vkhpp::SharingMode::eExclusive,
        .samples = vkhpp::SampleCountFlagBits::e1,
    };
    auto image = device->createImageUnique(imageCreateInfo).value;

    vkhpp::MemoryRequirements imageMemoryRequirements{};
    device->getImageMemoryRequirements(*image, &imageMemoryRequirements);

    const uint32_t imageMemoryIndex = utils::getMemoryType(
        physicalDevice, imageMemoryRequirements, vkhpp::MemoryPropertyFlagBits::eDeviceLocal);
    ASSERT_THAT(imageMemoryIndex, Not(Eq(-1)));

    const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
        .allocationSize = imageMemoryRequirements.size,
        .memoryTypeIndex = imageMemoryIndex,
    };

    auto imageMemory = device->allocateMemoryUnique(imageMemoryAllocateInfo).value;
    ASSERT_THAT(imageMemory, IsValidHandle());
    ASSERT_THAT(device->bindImageMemory(*image, *imageMemory, 0), IsVkSuccess());

    const vkhpp::BufferCreateInfo bufferCreateInfo = {
        .size = static_cast<VkDeviceSize>(12 * 1024 * 1024),
        .usage = vkhpp::BufferUsageFlagBits::eTransferDst |
                 vkhpp::BufferUsageFlagBits::eTransferSrc,
        .sharingMode = vkhpp::SharingMode::eExclusive,
    };
    auto stagingBuffer = device->createBufferUnique(bufferCreateInfo).value;
    ASSERT_THAT(stagingBuffer, IsValidHandle());

    vkhpp::MemoryRequirements stagingBufferMemoryRequirements{};
    device->getBufferMemoryRequirements(*stagingBuffer, &stagingBufferMemoryRequirements);

    const auto stagingBufferMemoryType = utils::getMemoryType(
        physicalDevice, stagingBufferMemoryRequirements,
        vkhpp::MemoryPropertyFlagBits::eHostVisible | vkhpp::MemoryPropertyFlagBits::eHostCoherent);

    const vkhpp::MemoryAllocateInfo stagingBufferMemoryAllocateInfo = {
        .allocationSize = stagingBufferMemoryRequirements.size,
        .memoryTypeIndex = stagingBufferMemoryType,
    };
    auto stagingBufferMemory = device->allocateMemoryUnique(stagingBufferMemoryAllocateInfo).value;
    ASSERT_THAT(stagingBufferMemory, IsValidHandle());
    ASSERT_THAT(device->bindBufferMemory(*stagingBuffer, *stagingBufferMemory, 0), IsVkSuccess());

    const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
        .queueFamilyIndex = queueFamilyIndex,
    };

    auto commandPool = device->createCommandPoolUnique(commandPoolCreateInfo).value;
    ASSERT_THAT(stagingBufferMemory, IsValidHandle());

    const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
        .level = vkhpp::CommandBufferLevel::ePrimary,
        .commandPool = *commandPool,
        .commandBufferCount = 1,
    };
    auto commandBuffers = device->allocateCommandBuffersUnique(commandBufferAllocateInfo).value;
    ASSERT_THAT(commandBuffers, Not(IsEmpty()));
    auto commandBuffer = std::move(commandBuffers[0]);
    ASSERT_THAT(commandBuffer, IsValidHandle());

    const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
        .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer->begin(commandBufferBeginInfo);
    commandBuffer->end();

    std::vector<vkhpp::CommandBuffer> commandBufferHandles;
    commandBufferHandles.push_back(*commandBuffer);

    auto transferFence = device->createFenceUnique(vkhpp::FenceCreateInfo()).value;
    ASSERT_THAT(commandBuffer, IsValidHandle());

    const vkhpp::SubmitInfo submitInfo = {
        .commandBufferCount = static_cast<uint32_t>(commandBufferHandles.size()),
        .pCommandBuffers = commandBufferHandles.data(),
    };
    queue.submit(submitInfo, *transferFence);

    auto waitResult = device->waitForFences(*transferFence, VK_TRUE, AsVkTimeout(3s));
    ASSERT_THAT(waitResult, IsVkSuccess());

    int fence;

    auto result = vkQueueSignalReleaseImageANDROID(queue, 0, nullptr, *image, &fence);
    ASSERT_THAT(result, Eq(VK_SUCCESS));
    ASSERT_THAT(fence, Not(Eq(-1)));

    ASSERT_THAT(mSync->wait(fence, 3000), Eq(0));
}

TEST_P(GfxstreamEnd2EndVkTest, DeferredImportAHB) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    const uint32_t width = 32;
    const uint32_t height = 32;
    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    auto vkQueueSignalReleaseImageANDROID = PFN_vkQueueSignalReleaseImageANDROID(
        device->getProcAddr("vkQueueSignalReleaseImageANDROID"));
    ASSERT_THAT(vkQueueSignalReleaseImageANDROID, NotNull());

    const vkhpp::ImageCreateInfo imageCreateInfo = {
        .pNext = nullptr,
        .imageType = vkhpp::ImageType::e2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = vkhpp::Format::eR8G8B8A8Unorm,
        .tiling = vkhpp::ImageTiling::eOptimal,
        .initialLayout = vkhpp::ImageLayout::eUndefined,
        .usage = vkhpp::ImageUsageFlagBits::eSampled |
                 vkhpp::ImageUsageFlagBits::eTransferDst |
                 vkhpp::ImageUsageFlagBits::eTransferSrc,
        .sharingMode = vkhpp::SharingMode::eExclusive,
        .samples = vkhpp::SampleCountFlagBits::e1,
    };
    auto image = device->createImageUnique(imageCreateInfo).value;

    // NOTE: Binding the VkImage to the AHB happens after the VkImage is created.
    const VkNativeBufferANDROID imageNativeBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID,
        .handle = mGralloc->getNativeHandle(ahb),
    };

    const vkhpp::BindImageMemoryInfo imageBindMemoryInfo = {
        .pNext = &imageNativeBufferInfo,
        .image = *image,
        .memory = VK_NULL_HANDLE,
        .memoryOffset = 0,
    };
    ASSERT_THAT(device->bindImageMemory2({imageBindMemoryInfo}), IsVkSuccess());

    std::vector<vkhpp::Semaphore> semaphores;
    int fence;

    auto result = vkQueueSignalReleaseImageANDROID(queue, 0, nullptr, *image, &fence);
    ASSERT_THAT(result, Eq(VK_SUCCESS));
    ASSERT_THAT(fence, Not(Eq(-1)));

    ASSERT_THAT(mSync->wait(fence, 3000), Eq(0));
}

TEST_P(GfxstreamEnd2EndVkTest, BlobAHBIsNotMapable) {
    if (GetParam().with_gl) {
        GTEST_SKIP()
            << "Skipping test, data buffers are currently only supported in Vulkan only mode.";
    }
    if (GetParam().with_features.count("VulkanUseDedicatedAhbMemoryType") == 0) {
        GTEST_SKIP()
            << "Skipping test, AHB test only makes sense with VulkanUseDedicatedAhbMemoryType.";
    }

    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    const uint32_t width = 32;
    const uint32_t height = 1;
    auto ahb = GFXSTREAM_ASSERT(
        ScopedAHardwareBuffer::Allocate(*mGralloc, width, height, GFXSTREAM_AHB_FORMAT_BLOB));

    const vkhpp::ExternalMemoryBufferCreateInfo externalMemoryBufferCreateInfo = {
        .handleTypes = vkhpp::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID,
    };
    const vkhpp::BufferCreateInfo bufferCreateInfo = {
        .pNext = &externalMemoryBufferCreateInfo,
        .size = width,
        .usage = vkhpp::BufferUsageFlagBits::eTransferDst |
                 vkhpp::BufferUsageFlagBits::eTransferSrc |
                 vkhpp::BufferUsageFlagBits::eVertexBuffer,
        .sharingMode = vkhpp::SharingMode::eExclusive,
    };
    auto buffer = device->createBufferUnique(bufferCreateInfo).value;
    ASSERT_THAT(buffer, IsValidHandle());

    auto vkGetAndroidHardwareBufferPropertiesANDROID =
        reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            device->getProcAddr("vkGetAndroidHardwareBufferPropertiesANDROID"));
    ASSERT_THAT(vkGetAndroidHardwareBufferPropertiesANDROID, NotNull());

    VkAndroidHardwareBufferPropertiesANDROID bufferProperties = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
        .pNext = nullptr,
    };
    ASSERT_THAT(vkGetAndroidHardwareBufferPropertiesANDROID(*device, ahb, &bufferProperties),
                Eq(VK_SUCCESS));

    const vkhpp::MemoryRequirements bufferMemoryRequirements{
        .size = bufferProperties.allocationSize,
        .alignment = 0,
        .memoryTypeBits = bufferProperties.memoryTypeBits,
    };

    const auto memoryProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if (!(bufferMemoryRequirements.memoryTypeBits & (1 << i))) {
            continue;
        }

        const auto memoryPropertyFlags = memoryProperties.memoryTypes[i].propertyFlags;
        EXPECT_THAT(memoryPropertyFlags & vkhpp::MemoryPropertyFlagBits::eHostVisible,
                    Ne(vkhpp::MemoryPropertyFlagBits::eHostVisible));
    }

    const auto bufferMemoryType = utils::getMemoryType(physicalDevice, bufferMemoryRequirements,
                                                       vkhpp::MemoryPropertyFlagBits::eDeviceLocal);
    ASSERT_THAT(bufferMemoryType, Ne(-1));

    const vkhpp::ImportAndroidHardwareBufferInfoANDROID importHardwareBufferInfo = {
        .buffer = ahb,
    };
    const vkhpp::MemoryAllocateInfo bufferMemoryAllocateInfo = {
        .pNext = &importHardwareBufferInfo,
        .allocationSize = bufferMemoryRequirements.size,
        .memoryTypeIndex = bufferMemoryType,
    };
    auto bufferMemory = device->allocateMemoryUnique(bufferMemoryAllocateInfo).value;
    ASSERT_THAT(bufferMemory, IsValidHandle());

    ASSERT_THAT(device->bindBufferMemory(*buffer, *bufferMemory, 0), IsVkSuccess());
}

TEST_P(GfxstreamEnd2EndVkTest, HostMemory) {
    static constexpr const vkhpp::DeviceSize kSize = 16 * 1024;

    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    uint32_t hostMemoryTypeIndex = -1;
    const auto memoryProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        const vkhpp::MemoryType& memoryType = memoryProperties.memoryTypes[i];
        if (memoryType.propertyFlags & vkhpp::MemoryPropertyFlagBits::eHostVisible) {
            hostMemoryTypeIndex = i;
        }
    }
    if (hostMemoryTypeIndex == -1) {
        GTEST_SKIP() << "Skipping test due to no host visible memory type.";
        return;
    }

    const vkhpp::MemoryAllocateInfo memoryAllocateInfo = {
        .allocationSize = kSize,
        .memoryTypeIndex = hostMemoryTypeIndex,
    };
    auto memory = device->allocateMemoryUnique(memoryAllocateInfo).value;
    ASSERT_THAT(memory, IsValidHandle());

    void* mapped = nullptr;

    auto mapResult = device->mapMemory(*memory, 0, VK_WHOLE_SIZE, vkhpp::MemoryMapFlags{}, &mapped);
    ASSERT_THAT(mapResult, IsVkSuccess());
    ASSERT_THAT(mapped, NotNull());

    auto* bytes = reinterpret_cast<uint8_t*>(mapped);
    std::memset(bytes, 0xFF, kSize);

    const vkhpp::MappedMemoryRange range = {
        .memory = *memory,
        .offset = 0,
        .size = kSize,
    };
    device->flushMappedMemoryRanges({range});
    device->invalidateMappedMemoryRanges({range});

    for (uint32_t i = 0; i < kSize; ++i) {
        EXPECT_THAT(bytes[i], Eq(0xFF));
    }
}

TEST_P(GfxstreamEnd2EndVkTest, GetPhysicalDeviceProperties2) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    auto props1 = physicalDevice.getProperties();
    auto props2 = physicalDevice.getProperties2();

    EXPECT_THAT(props1.vendorID, Eq(props2.properties.vendorID));
    EXPECT_THAT(props1.deviceID, Eq(props2.properties.deviceID));
}

TEST_P(GfxstreamEnd2EndVkTest, GetPhysicalDeviceFeatures2KHR) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    auto features1 = physicalDevice.getFeatures();
    auto features2 = physicalDevice.getFeatures2();
    EXPECT_THAT(features1.robustBufferAccess, Eq(features2.features.robustBufferAccess));
}

TEST_P(GfxstreamEnd2EndVkTest, GetPhysicalDeviceImageFormatProperties2KHR) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    const vkhpp::PhysicalDeviceImageFormatInfo2 imageFormatInfo = {
        .format = vkhpp::Format::eR8G8B8A8Unorm,
        .type = vkhpp::ImageType::e2D,
        .tiling = vkhpp::ImageTiling::eOptimal,
        .usage = vkhpp::ImageUsageFlagBits::eSampled,
    };
    const auto properties =
        GFXSTREAM_ASSERT_VKHPP_RV(physicalDevice.getImageFormatProperties2(imageFormatInfo));
    EXPECT_THAT(properties.imageFormatProperties.maxExtent.width, Ge(1));
    EXPECT_THAT(properties.imageFormatProperties.maxExtent.height, Ge(1));
    EXPECT_THAT(properties.imageFormatProperties.maxExtent.depth, Ge(1));
}

template <typename VkhppUniqueHandleType,
          typename VkhppHandleType = typename VkhppUniqueHandleType::element_type>
std::vector<VkhppHandleType> AsHandles(const std::vector<VkhppUniqueHandleType>& elements) {
    std::vector<VkhppHandleType> ret;
    ret.reserve(elements.size());
    for (const auto& e : elements) {
        ret.push_back(*e);
    }
    return ret;
}

struct DescriptorBundle {
    vkhpp::UniqueDescriptorPool descriptorPool;
    vkhpp::UniqueDescriptorSetLayout descriptorSetLayout;
    std::vector<vkhpp::UniqueDescriptorSet> descriptorSets;
};

Result<Ok> ReallocateDescriptorBundleSets(vkhpp::Device device, uint32_t count,
                                          DescriptorBundle* bundle) {
    if (!bundle->descriptorSetLayout) {
        return gfxstream::unexpected("Invalid descriptor set layout");
    }

    const std::vector<vkhpp::DescriptorSetLayout> descriptorSetLayouts(count, *bundle->descriptorSetLayout);
    const vkhpp::DescriptorSetAllocateInfo descriptorSetAllocateInfo = {
        .descriptorPool = *bundle->descriptorPool,
        .descriptorSetCount = count,
        .pSetLayouts = descriptorSetLayouts.data(),
    };
    auto descriptorSets =
        GFXSTREAM_EXPECT_VKHPP_RV(device.allocateDescriptorSetsUnique(descriptorSetAllocateInfo));
    bundle->descriptorSets = std::move(descriptorSets);
    return Ok{};
}

Result<DescriptorBundle> AllocateDescriptorBundle(vkhpp::Device device, uint32_t count) {
    const vkhpp::DescriptorPoolSize descriptorPoolSize = {
        .type = vkhpp::DescriptorType::eUniformBuffer,
        .descriptorCount = 1 * count,
    };
    const vkhpp::DescriptorPoolCreateInfo descriptorPoolCreateInfo = {
        .flags = vkhpp::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = count,
        .poolSizeCount = 1,
        .pPoolSizes = &descriptorPoolSize,
    };
    auto descriptorPool =
        GFXSTREAM_EXPECT_VKHPP_RV(device.createDescriptorPoolUnique(descriptorPoolCreateInfo));

    const vkhpp::DescriptorSetLayoutBinding descriptorSetBinding = {
        .binding = 0,
        .descriptorType = vkhpp::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vkhpp::ShaderStageFlagBits::eVertex,
    };
    const vkhpp::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
        .bindingCount = 1,
        .pBindings = &descriptorSetBinding,
    };
    auto descriptorSetLayout =
        GFXSTREAM_EXPECT_VKHPP_RV(device.createDescriptorSetLayoutUnique(descriptorSetLayoutInfo));

    DescriptorBundle bundle = {
        .descriptorPool = std::move(descriptorPool),
        .descriptorSetLayout = std::move(descriptorSetLayout),
    };
    GFXSTREAM_EXPECT(ReallocateDescriptorBundleSets(device, count, &bundle));
    return std::move(bundle);
}

// Tests creating a bunch of descriptor sets and freeing them via vkFreeDescriptorSet.
// 1. Via vkFreeDescriptorSet directly
// 2. Via vkResetDescriptorPool
// 3. Via vkDestroyDescriptorPool
// 4. Via vkResetDescriptorPool and double frees in vkFreeDescriptorSet
// 5. Via vkResetDescriptorPool and double frees in vkFreeDescriptorSet
// 4. Via vkResetDescriptorPool, creating more, and freeing vai vkFreeDescriptorSet
// (because vkFree* APIs are expected to never fail)
// https://github.com/KhronosGroup/Vulkan-Docs/issues/1070
TEST_P(GfxstreamEnd2EndVkTest, DescriptorSetAllocFree) {
    constexpr const uint32_t kNumSets = 4;

    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    auto bundle = GFXSTREAM_ASSERT(AllocateDescriptorBundle(*device, kNumSets));

    auto descriptorSetHandles = AsHandles(bundle.descriptorSets);
    EXPECT_THAT(device->freeDescriptorSets(*bundle.descriptorPool, kNumSets, descriptorSetHandles.data()), IsVkSuccess());

    // The double free should also work
    EXPECT_THAT(device->freeDescriptorSets(*bundle.descriptorPool, kNumSets, descriptorSetHandles.data()), IsVkSuccess());

    // Alloc/free again should also work
    GFXSTREAM_ASSERT(ReallocateDescriptorBundleSets(*device, kNumSets, &bundle));

    descriptorSetHandles = AsHandles(bundle.descriptorSets);
    EXPECT_THAT(device->freeDescriptorSets(*bundle.descriptorPool, kNumSets, descriptorSetHandles.data()), IsVkSuccess());
}

TEST_P(GfxstreamEnd2EndVkTest, DescriptorSetAllocFreeReset) {
    constexpr const uint32_t kNumSets = 4;

    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    auto bundle = GFXSTREAM_ASSERT(AllocateDescriptorBundle(*device, kNumSets));

    device->resetDescriptorPool(*bundle.descriptorPool);

    // The double free should also work
    auto descriptorSetHandles = AsHandles(bundle.descriptorSets);
    EXPECT_THAT(device->freeDescriptorSets(*bundle.descriptorPool, kNumSets, descriptorSetHandles.data()), IsVkSuccess());

    // Alloc/reset/free again should also work
    GFXSTREAM_ASSERT(ReallocateDescriptorBundleSets(*device, kNumSets, &bundle));

    device->resetDescriptorPool(*bundle.descriptorPool);

    descriptorSetHandles = AsHandles(bundle.descriptorSets);
    EXPECT_THAT(device->freeDescriptorSets(*bundle.descriptorPool, kNumSets, descriptorSetHandles.data()), IsVkSuccess());
}

TEST_P(GfxstreamEnd2EndVkTest, DISABLED_DescriptorSetAllocFreeDestroy) {
    constexpr const uint32_t kNumSets = 4;

    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    auto bundle = GFXSTREAM_ASSERT(AllocateDescriptorBundle(*device, kNumSets));

    device->destroyDescriptorPool(*bundle.descriptorPool);

    // The double free should also work
    auto descriptorSetHandles = AsHandles(bundle.descriptorSets);
    EXPECT_THAT(device->freeDescriptorSets(*bundle.descriptorPool, kNumSets, descriptorSetHandles.data()), IsVkSuccess());
}

TEST_P(GfxstreamEnd2EndVkTest, MultiThreadedShutdown) {
    constexpr const int kNumIterations = 20;
    for (int i = 0; i < kNumIterations; i++) {
        auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
            GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

        const vkhpp::BufferCreateInfo bufferCreateInfo = {
            .size = 1024,
            .usage = vkhpp::BufferUsageFlagBits::eTransferSrc,
        };

        // TODO: switch to std::barrier with arrive_and_wait().
        std::atomic_int threadsReady{0};
        std::vector<std::thread> threads;

        constexpr const int kNumThreads = 5;
        for (int t = 0; t < kNumThreads; t++) {
            threads.emplace_back([&, this]() {
                // Perform some work to ensure host RenderThread started.
                auto buffer1 = device->createBufferUnique(bufferCreateInfo).value;

                ++threadsReady;
                while (threadsReady.load() != kNumThreads) {
                }

                // Sleep a little which is hopefully enough time to potentially get
                // the corresponding host ASG RenderThreads to go sleep waiting for
                // a WAKEUP via a GFXSTREAM_CONTEXT_PING.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                auto buffer2 = device->createBufferUnique(bufferCreateInfo).value;

                // 2 vkDestroyBuffer() calls happen here with the destruction of `buffer1`
                // and `buffer2`. vkDestroy*() calls are async (return `void`) and the
                // guest thread continues execution without waiting for the command to
                // complete on the host.
                //
                // The guest ASG and corresponding virtio gpu resource will also be
                // destructed here as a part of the thread_local HostConnection being
                // destructed.
                //
                // Note: Vulkan commands are given a sequence number in order to ensure that
                // commands from multi-threaded guest Vulkan apps are executed in order on the
                // host. Gfxstream's host Vulkan decoders will spin loop waiting for their turn to
                // process their next command.
                //
                // With all of the above, a deadlock would previouly occur with the following
                // sequence:
                //
                // T1: Host-RenderThread-1: <sleeping waiting for wakeup>
                //
                // T2: Host-RenderThread-2: <sleeping waiting for wakeup>
                //
                // T3: Guest-Thread-1: vkDestroyBuffer() called,
                //                     VkEncoder grabs sequence-number-10,
                //                     writes sequence-number-10 into ASG-1 via resource-1
                //
                // T4: Guest-Thread-2: vkDestroyBuffer() called,
                //                     VkEncoder grabs sequence-number-11,
                //                     writes into ASG-2 via resource-2
                //
                // T5: Guest-Thread-2: ASG-2 sends a VIRTIO_GPU_CMD_SUBMIT_3D with
                //                     GFXSTREAM_CONTEXT_PING on ASG-resource-2
                //
                // T6: Guest-Thread-2: guest thread finishes,
                //                     ASG-2 destructor destroys the virtio-gpu resource used,
                //                     destruction sends VIRTIO_GPU_CMD_RESOURCE_UNREF on
                //                     resource-2
                //
                // T7: Guest-Thread-1: ASG-1 sends VIRTIO_GPU_CMD_SUBMIT_3D with
                //                     GFXSTREAM_CONTEXT_PING on ASG-resource-1
                //
                // T8: Host-Virtio-Gpu-Thread: performs VIRTIO_GPU_CMD_SUBMIT_3D from T5,
                //                             pings ASG-2 which wakes up Host-RenderThread-2
                //
                // T9: Host-RenderThread-2: woken from T8,
                //                          reads sequence-number-11 from ASG-2,
                //                          spin looping waiting for sequence-number-10 to execute
                //
                // T10: Host-Virtio-Gpu-Thread: performs VIRTIO_GPU_CMD_RESOURCE_UNREF for
                //                              resource-2 from T6,
                //                              resource-2 is used by ASG-2 / Host-RenderThread-2
                //                              waits for Host-RenderThread-2 to finish
                //
                // DEADLOCKED HERE:
                //
                //   *  Host-Virtio-GpuThread is waiting for Host-RenderThread-2 to finish before
                //      it can finish destroying resource-2
                //
                //   *  Host-RenderThread-2 is waiting for Host-RenderThread-1 to execute
                //      sequence-number-10
                //
                //   *  Host-RenderThread-1 is asleep waiting for a GFXSTREAM_CONTEXT_PING
                //      from Host-Virtio-GpuThread
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }
}

TEST_P(GfxstreamEnd2EndVkTest, DeviceCreateWithDeviceGroup) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    const vkhpp::DeviceGroupDeviceCreateInfo deviceGroupDeviceCreateInfo = {
        .physicalDeviceCount = 1,
        .pPhysicalDevices = &physicalDevice,
    };

    const float queuePriority = 1.0f;
    const vkhpp::DeviceQueueCreateInfo deviceQueueCreateInfo = {
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    const vkhpp::DeviceCreateInfo deviceCreateInfo = {
        .pNext = &deviceGroupDeviceCreateInfo,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .queueCreateInfoCount = 1,
    };
    auto device2 = GFXSTREAM_ASSERT_VKHPP_RV(physicalDevice.createDeviceUnique(deviceCreateInfo));
    ASSERT_THAT(device2, IsValidHandle());
}

TEST_P(GfxstreamEnd2EndVkTest, AcquireImageAndroidWithFence) {
    DoAcquireImageAndroidWithSync(/*withFence=*/true, /*withSemaphore=*/false);
}

TEST_P(GfxstreamEnd2EndVkTest, AcquireImageAndroidWithSemaphore) {
    DoAcquireImageAndroidWithSync(/*withFence=*/false, /*withSemaphore=*/true);
}

TEST_P(GfxstreamEnd2EndVkTest, AcquireImageAndroidWithFenceAndSemaphore) {
    DoAcquireImageAndroidWithSync(/*withFence=*/true, /*withSemaphore=*/true);
}

VKAPI_ATTR void VKAPI_CALL MemoryReportCallback(const vkhpp::DeviceMemoryReportCallbackDataEXT*, void*) {
    // Unused
}

TEST_P(GfxstreamEnd2EndVkTest, DeviceMemoryReport) {
    int userdata = 1;
    vkhpp::DeviceDeviceMemoryReportCreateInfoEXT deviceDeviceMemoryReportInfo = {
        .pfnUserCallback = &MemoryReportCallback,
        .pUserData = &userdata,
    };

    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment({
            .deviceExtensions = {{
                VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME,
            }},
            .deviceCreateInfoPNext = &deviceDeviceMemoryReportInfo,
        }));

    const vkhpp::MemoryAllocateInfo memoryAllocateInfo = {
        .allocationSize = 1024,
        .memoryTypeIndex = 0,
    };
    auto memory = device->allocateMemoryUnique(memoryAllocateInfo).value;
    ASSERT_THAT(memory, IsValidHandle());
}

TEST_P(GfxstreamEnd2EndVkTest, DescriptorUpdateTemplateWithWrapping) {
    auto vk = GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
    auto& [instance, physicalDevice, device, queue, queueFamilyIndex] = vk;

    const VkDeviceSize kBufferSize = 1024;
    auto buffer = GFXSTREAM_ASSERT(CreateBuffer(
            vk, kBufferSize,
            vkhpp::BufferUsageFlagBits::eTransferDst |
                vkhpp::BufferUsageFlagBits::eTransferSrc |
                vkhpp::BufferUsageFlagBits::eUniformBuffer,
            vkhpp::MemoryPropertyFlagBits::eHostVisible |
                vkhpp::MemoryPropertyFlagBits::eHostCoherent));

    const std::vector<VkDescriptorBufferInfo> descriptorInfo = {
        VkDescriptorBufferInfo{
            .buffer = *buffer.buffer,
            .offset = 0,
            .range = kBufferSize,
        },
        VkDescriptorBufferInfo{
            .buffer = *buffer.buffer,
            .offset = 0,
            .range = kBufferSize,
        },
        VkDescriptorBufferInfo{
            .buffer = *buffer.buffer,
            .offset = 0,
            .range = kBufferSize,
        },
        VkDescriptorBufferInfo{
            .buffer = *buffer.buffer,
            .offset = 0,
            .range = kBufferSize,
        },
    };

    const std::vector<vkhpp::DescriptorPoolSize> descriptorPoolSizes = {
        {
            .type = vkhpp::DescriptorType::eUniformBuffer,
            .descriptorCount = 4,
        },
    };
    const vkhpp::DescriptorPoolCreateInfo descriptorPoolCreateInfo = {
        .flags = vkhpp::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
        .pPoolSizes = descriptorPoolSizes.data(),
    };
    auto descriptorPool =
        GFXSTREAM_ASSERT_VKHPP_RV(device->createDescriptorPoolUnique(descriptorPoolCreateInfo));

    const std::vector<vkhpp::DescriptorSetLayoutBinding> descriptorSetBindings = {
        {
            .binding = 0,
            .descriptorType = vkhpp::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vkhpp::ShaderStageFlagBits::eVertex,
        },
        {
            .binding = 1,
            .descriptorType = vkhpp::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vkhpp::ShaderStageFlagBits::eVertex,
        },
        {
            .binding = 2,
            .descriptorType = vkhpp::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vkhpp::ShaderStageFlagBits::eVertex,
        },
        {
            .binding = 3,
            .descriptorType = vkhpp::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vkhpp::ShaderStageFlagBits::eVertex,
        },
    };
    const vkhpp::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
        .bindingCount = static_cast<uint32_t>(descriptorSetBindings.size()),
        .pBindings = descriptorSetBindings.data(),
    };
    auto descriptorSetLayout =
        GFXSTREAM_ASSERT_VKHPP_RV(device->createDescriptorSetLayoutUnique(descriptorSetLayoutInfo));

    const std::vector<vkhpp::DescriptorSetLayout> descriptorSetLayouts = {*descriptorSetLayout};
    const vkhpp::DescriptorSetAllocateInfo descriptorSetAllocateInfo = {
        .descriptorPool = *descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
    };
    auto descriptorSets =
        GFXSTREAM_ASSERT_VKHPP_RV(device->allocateDescriptorSetsUnique(descriptorSetAllocateInfo));
    auto descriptorSet = std::move(descriptorSets[0]);

    const vkhpp::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
    };
    auto pipelineLayout =
        GFXSTREAM_ASSERT_VKHPP_RV(device->createPipelineLayoutUnique(pipelineLayoutCreateInfo));

    const std::vector<vkhpp::DescriptorUpdateTemplateEntry> descriptorUpdateEntries = {
        {
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 4,
            .descriptorType = vkhpp::DescriptorType::eUniformBuffer,
            .offset = 0,
            .stride = sizeof(VkDescriptorBufferInfo),
        },
    };
    const vkhpp::DescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateCreateInfo = {
        .descriptorUpdateEntryCount = static_cast<uint32_t>(descriptorUpdateEntries.size()),
        .pDescriptorUpdateEntries = descriptorUpdateEntries.data(),
        .descriptorSetLayout = *descriptorSetLayout,
        .pipelineBindPoint = vkhpp::PipelineBindPoint::eGraphics,
        .pipelineLayout = *pipelineLayout,
        .set = 0,
    };
    auto descriptorUpdateTemplate = GFXSTREAM_ASSERT_VKHPP_RV(
        device->createDescriptorUpdateTemplateUnique(descriptorUpdateTemplateCreateInfo));

    device->updateDescriptorSetWithTemplate(*descriptorSet, *descriptorUpdateTemplate,
                                            (const void*)descriptorInfo.data());

    // Gfxstream optimizes descriptor set updates by batching updates until there is an
    // actual use in a command buffer. Try to force that flush by binding the descriptor
    // set here:
    GFXSTREAM_ASSERT(DoCommandsImmediate(vk,
        [&](vkhpp::UniqueCommandBuffer& cmd) {
            cmd->bindDescriptorSets(vkhpp::PipelineBindPoint::eGraphics, *pipelineLayout,
                                    /*firstSet=*/0, {*descriptorSet},
                                    /*dynamicOffsets=*/{});
            return Ok{};
        }));
}

TEST_P(GfxstreamEnd2EndVkTest, MultiThreadedVkMapMemory) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    static constexpr const vkhpp::DeviceSize kSize = 1024;
    const vkhpp::BufferCreateInfo bufferCreateInfo = {
        .size = kSize,
        .usage = vkhpp::BufferUsageFlagBits::eTransferSrc,
    };
    auto buffer = device->createBufferUnique(bufferCreateInfo).value;

    vkhpp::MemoryRequirements bufferMemoryRequirements{};
    device->getBufferMemoryRequirements(*buffer, &bufferMemoryRequirements);

    const uint32_t bufferMemoryIndex = utils::getMemoryType(
        physicalDevice, bufferMemoryRequirements,
        vkhpp::MemoryPropertyFlagBits::eHostVisible | vkhpp::MemoryPropertyFlagBits::eHostCoherent);
    if (bufferMemoryIndex == -1) {
        GTEST_SKIP() << "Skipping test due to no memory type with HOST_VISIBLE | HOST_COHERENT.";
    }

    std::vector<std::thread> threads;
    std::atomic_int threadsReady{0};

    constexpr const int kNumThreads = 2;
    for (int t = 0; t < kNumThreads; t++) {
        threads.emplace_back([&, this]() {
            // Perform some work to ensure host RenderThread started.
            auto buffer2 = device->createBufferUnique(bufferCreateInfo).value;
            ASSERT_THAT(buffer2, IsValidHandle());

            ++threadsReady;
            while (threadsReady.load() != kNumThreads) {
            }

            constexpr const int kNumIterations = 100;
            for (int i = 0; i < kNumIterations; i++) {
                auto buffer3 = device->createBufferUnique(bufferCreateInfo).value;
                ASSERT_THAT(buffer3, IsValidHandle());

                const vkhpp::MemoryAllocateInfo buffer3MemoryAllocateInfo = {
                    .allocationSize = bufferMemoryRequirements.size,
                    .memoryTypeIndex = bufferMemoryIndex,
                };
                auto buffer3Memory = device->allocateMemoryUnique(buffer3MemoryAllocateInfo).value;
                ASSERT_THAT(buffer3Memory, IsValidHandle());

                ASSERT_THAT(device->bindBufferMemory(*buffer3, *buffer3Memory, 0), IsVkSuccess());

                void* mapped = nullptr;
                ASSERT_THAT(device->mapMemory(*buffer3Memory, 0, VK_WHOLE_SIZE,
                                              vkhpp::MemoryMapFlags{}, &mapped),
                            IsVkSuccess());
                ASSERT_THAT(mapped, NotNull());

                device->unmapMemory(*buffer3Memory);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

TEST_P(GfxstreamEnd2EndVkTest, MultiThreadedResetCommandBuffer) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());

    static constexpr const vkhpp::DeviceSize kSize = 1024;
    const vkhpp::BufferCreateInfo bufferCreateInfo = {
        .size = kSize,
        .usage = vkhpp::BufferUsageFlagBits::eTransferSrc,
    };

    static std::mutex queue_mutex;
    std::vector<std::thread> threads;
    std::atomic_int threadsReady{0};

    constexpr const int kNumThreads = 10;
    for (int t = 0; t < kNumThreads; t++) {
        threads.emplace_back([&, this]() {
            // Perform some work to ensure host RenderThread started.
            auto buffer2 = device->createBufferUnique(bufferCreateInfo).value;
            ASSERT_THAT(buffer2, IsValidHandle());

            ++threadsReady;
            while (threadsReady.load() != kNumThreads) {
            }

            const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
                .queueFamilyIndex = queueFamilyIndex,
            };
            auto commandPool = device->createCommandPoolUnique(commandPoolCreateInfo).value;

            const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
                .level = vkhpp::CommandBufferLevel::ePrimary,
                .commandPool = *commandPool,
                .commandBufferCount = 1,
            };
            auto commandBuffers = device->allocateCommandBuffersUnique(commandBufferAllocateInfo).value;
            ASSERT_THAT(commandBuffers, Not(IsEmpty()));
            auto commandBuffer = std::move(commandBuffers[0]);
            ASSERT_THAT(commandBuffer, IsValidHandle());

            auto transferFence = device->createFenceUnique(vkhpp::FenceCreateInfo()).value;
            ASSERT_THAT(commandBuffer, IsValidHandle());

            constexpr const int kNumIterations = 1000;
            for (int i = 0; i < kNumIterations; i++) {
                commandBuffer->reset();
                const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
                    .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
                };
                commandBuffer->begin(commandBufferBeginInfo);

                commandBuffer->end();

                std::vector<vkhpp::CommandBuffer> commandBufferHandles;
                commandBufferHandles.push_back(*commandBuffer);

                const vkhpp::SubmitInfo submitInfo = {
                    .commandBufferCount = static_cast<uint32_t>(commandBufferHandles.size()),
                    .pCommandBuffers = commandBufferHandles.data(),
                };
                {
                    std::lock_guard<std::mutex> qm(queue_mutex);
                    queue.submit(submitInfo, *transferFence);
                }
                auto waitResult = device->waitForFences(*transferFence, VK_TRUE, AsVkTimeout(3s));
                ASSERT_THAT(waitResult, IsVkSuccess());
                device->resetFences(*transferFence);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

TEST_P(GfxstreamEnd2EndVkTest, ImportAndBlitFromR8G8B8A8Ahb) {
    DoFillAndRenderFromAhb(GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM);
}

TEST_P(GfxstreamEnd2EndVkTest, ImportAndBlitFromYCbCr888420Ahb) {
    DoFillAndRenderFromAhb(GFXSTREAM_AHB_FORMAT_Y8Cb8Cr8_420);
}

TEST_P(GfxstreamEnd2EndVkTest, ImportAndBlitFromYv12Ahb) {
    DoFillAndRenderFromAhb(GFXSTREAM_AHB_FORMAT_YV12);
}

std::vector<TestParams> GenerateTestCases() {
    std::vector<TestParams> cases = {TestParams{
                                         .with_gl = false,
                                         .with_vk = true,
                                         .with_transport = GfxstreamTransport::kVirtioGpuAsg,
                                     },
                                     TestParams{
                                         .with_gl = true,
                                         .with_vk = true,
                                         .with_transport = GfxstreamTransport::kVirtioGpuAsg,
                                     },
                                     TestParams{
                                         .with_gl = false,
                                         .with_vk = true,
                                         .with_transport = GfxstreamTransport::kVirtioGpuPipe,
                                     },
                                     TestParams{
                                         .with_gl = true,
                                         .with_vk = true,
                                         .with_transport = GfxstreamTransport::kVirtioGpuPipe,
                                     }};
    cases = WithAndWithoutFeatures(cases, {"VulkanSnapshots"});
    cases = WithAndWithoutFeatures(cases, {"VulkanUseDedicatedAhbMemoryType"});
    return cases;
}

TEST_P(GfxstreamEnd2EndVkTest, GetFenceStatusDefault) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
    vkhpp::UniqueFence fence = device->createFenceUnique(vkhpp::FenceCreateInfo()).value;
    auto status = device->getFenceStatus(*fence);
    ASSERT_THAT(status, Eq(vkhpp::Result::eNotReady));
}

TEST_P(GfxstreamEnd2EndVkTest, GetFenceStatusCreateSignaled) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
    vkhpp::FenceCreateInfo fenceCreateInfo = {
        .flags = vkhpp::FenceCreateFlagBits::eSignaled,
    };
    vkhpp::UniqueFence fence = device->createFenceUnique(fenceCreateInfo).value;
    auto status = device->getFenceStatus(*fence);
    ASSERT_THAT(status, Eq(vkhpp::Result::eSuccess));
}

TEST_P(GfxstreamEnd2EndVkTest, WaitForFenceCreateSignaled) {
    auto [instance, physicalDevice, device, queue, queueFamilyIndex] =
        GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
    vkhpp::FenceCreateInfo fenceCreateInfo = {
        .flags = vkhpp::FenceCreateFlagBits::eSignaled,
    };
    vkhpp::UniqueFence fence = device->createFenceUnique(fenceCreateInfo).value;
    auto status = device->waitForFences(*fence, VK_TRUE, AsVkTimeout(3s));
    ASSERT_THAT(status, Eq(vkhpp::Result::eSuccess));
}

TEST_P(GfxstreamEnd2EndVkTest, UseDoubleQueuesAndSynchronizeCorrectly) {
    auto vk = GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
    auto& [instance, physicalDevice, device, queue_dont_use, graphicsQueueFamilyIndex] = vk;

    const uint32_t graphicsQueueCount =
        physicalDevice.getQueueFamilyProperties()[graphicsQueueFamilyIndex].queueCount;
    if (graphicsQueueCount < 2) {
        GTEST_SKIP() << "Device does not support at least 2 graphic_bit queues. Skipping test...";
        return;
    }

    // Recreate logical device to get 2 queues
    const float queuePriority = 1.0f;
    const vkhpp::DeviceQueueCreateInfo deviceQueueCreateInfo = {
        .queueFamilyIndex = graphicsQueueFamilyIndex,
        .queueCount = 2,
        .pQueuePriorities = &queuePriority,
    };
    std::vector<const char*> deviceExtensions = {
        VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
    };
    const vkhpp::DeviceCreateInfo deviceCreateInfo = {
        .pNext = nullptr,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .queueCreateInfoCount = 1,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
    };

    device = GFXSTREAM_ASSERT_VKHPP_RV(physicalDevice.createDeviceUnique(deviceCreateInfo));
    auto queue1 = device->getQueue(graphicsQueueFamilyIndex, 0);
    auto queue2 = device->getQueue(graphicsQueueFamilyIndex, 1);

    const VkDeviceSize kBufferSize = 1024;
    auto sourceBufferWithMemoryResult = CreateBuffer(
        vk, kBufferSize, vkhpp::BufferUsageFlagBits::eTransferSrc,
        vkhpp::MemoryPropertyFlagBits::eHostVisible | vkhpp::MemoryPropertyFlagBits::eHostCoherent);
    if (!sourceBufferWithMemoryResult.ok()) {
        GTEST_SKIP()
            << "Couldn't create hostVisible, hostCoherent, TransferSrc buffer. Skipping test...";
        return;
    }
    auto sourceBufferWithMemory = std::move(sourceBufferWithMemoryResult.value());
    auto destBufferWithMemoryResult = CreateBuffer(
        vk, kBufferSize, vkhpp::BufferUsageFlagBits::eTransferDst,
        vkhpp::MemoryPropertyFlagBits::eHostVisible | vkhpp::MemoryPropertyFlagBits::eHostCoherent);
    if (!destBufferWithMemoryResult.ok()) {
        GTEST_SKIP()
            << "Couldn't create hostVisible, hostCoherent, TransferDst buffer. Skipping test...";
        return;
    }
    auto destBufferWithMemory = std::move(destBufferWithMemoryResult.value());
    const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
        .queueFamilyIndex = graphicsQueueFamilyIndex,
    };

    auto commandPool =
        GFXSTREAM_ASSERT_VKHPP_RV(device->createCommandPoolUnique(commandPoolCreateInfo));

    const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
        .commandPool = *commandPool,
        .level = vkhpp::CommandBufferLevel::ePrimary,
        .commandBufferCount = 2,
    };
    auto commandBuffers =
        GFXSTREAM_ASSERT_VKHPP_RV(device->allocateCommandBuffersUnique(commandBufferAllocateInfo));
    auto commandBufferWriteInto = std::move(commandBuffers[0]);
    auto commandBufferCopy = std::move(commandBuffers[1]);

    vkhpp::UniqueSemaphore semaphore =
        device->createSemaphoreUnique(vkhpp::SemaphoreCreateInfo()).value;

    vkhpp::SubmitInfo submitInfoWrite = {.commandBufferCount = 1,
                                         .pCommandBuffers = &*commandBufferWriteInto,
                                         .signalSemaphoreCount = 1,
                                         .pSignalSemaphores = &*semaphore};

    const vkhpp::PipelineStageFlags waitStage = vkhpp::PipelineStageFlagBits::eTransfer;
    vkhpp::SubmitInfo submitInfoCopy = {.commandBufferCount = 1,
                                        .pCommandBuffers = &*commandBufferCopy,
                                        .waitSemaphoreCount = 1,
                                        .pWaitSemaphores = &*semaphore,
                                        .pWaitDstStageMask = &waitStage};
    const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
        .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBufferWriteInto->begin(commandBufferBeginInfo);
    std::vector<uint8_t> dataToWrite = utils::getRandomNByteData(100);
    commandBufferWriteInto->updateBuffer(sourceBufferWithMemory.buffer.get(), 0, dataToWrite.size(),
                                         dataToWrite.data());
    auto bufferBarrier = vkhpp::BufferMemoryBarrier()
                             .setSrcAccessMask(vkhpp::AccessFlagBits::eTransferWrite)
                             .setDstAccessMask(vkhpp::AccessFlagBits::eTransferRead)
                             .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                             .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                             .setBuffer(sourceBufferWithMemory.buffer.get())
                             .setOffset(0)
                             .setSize(kBufferSize);
    commandBufferWriteInto->pipelineBarrier(vkhpp::PipelineStageFlagBits::eTransfer,
                                            vkhpp::PipelineStageFlagBits::eTransfer,
                                            vkhpp::DependencyFlags(), {}, bufferBarrier, {});
    commandBufferWriteInto->end();

    commandBufferCopy->begin(commandBufferBeginInfo);
    vkhpp::BufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = kBufferSize;
    vkhpp::ArrayProxy<const vkhpp::BufferCopy> copyRegions = copyRegion;
    commandBufferCopy->copyBuffer(sourceBufferWithMemory.buffer.get(),
                                  destBufferWithMemory.buffer.get(), copyRegions);
    commandBufferCopy->end();
    queue1.submit(submitInfoWrite, nullptr);
    queue2.submit(submitInfoCopy, nullptr);
    queue2.waitIdle();
    void* mappedMemory = GFXSTREAM_ASSERT_VKHPP_RV(
        device->mapMemory(*destBufferWithMemory.bufferMemory, 0, kBufferSize));
    uint8_t* readData = static_cast<uint8_t*>(mappedMemory);
    device->unmapMemory(*destBufferWithMemory.bufferMemory);
    ASSERT_TRUE(std::memcmp(readData, dataToWrite.data(), dataToWrite.size()) == 0);
}

TEST_P(GfxstreamEnd2EndVkTest, GetFenceStatusOnExternalFence) {
    auto vk = GFXSTREAM_ASSERT(SetUpTypicalVkTestEnvironment());
    auto& [instance, physicalDevice, device, queue, queueFamilyIndex] = vk;

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
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = vkhpp::Format::eR8G8B8A8Unorm,
        .tiling = vkhpp::ImageTiling::eOptimal,
        .initialLayout = vkhpp::ImageLayout::eUndefined,
        .usage = vkhpp::ImageUsageFlagBits::eSampled | vkhpp::ImageUsageFlagBits::eTransferDst |
                 vkhpp::ImageUsageFlagBits::eTransferSrc,
        .sharingMode = vkhpp::SharingMode::eExclusive,
        .samples = vkhpp::SampleCountFlagBits::e1,
    };
    auto image = device->createImageUnique(imageCreateInfo).value;

    vkhpp::MemoryRequirements imageMemoryRequirements{};
    device->getImageMemoryRequirements(*image, &imageMemoryRequirements);

    const uint32_t imageMemoryIndex = utils::getMemoryType(
        physicalDevice, imageMemoryRequirements, vkhpp::MemoryPropertyFlagBits::eDeviceLocal);
    ASSERT_THAT(imageMemoryIndex, Not(Eq(-1)));

    const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
        .allocationSize = imageMemoryRequirements.size,
        .memoryTypeIndex = imageMemoryIndex,
    };

    auto imageMemory = device->allocateMemoryUnique(imageMemoryAllocateInfo).value;
    ASSERT_THAT(imageMemory, IsValidHandle());
    ASSERT_THAT(device->bindImageMemory(*image, *imageMemory, 0), IsVkSuccess());

    auto vkQueueSignalReleaseImageANDROID = PFN_vkQueueSignalReleaseImageANDROID(
        device->getProcAddr("vkQueueSignalReleaseImageANDROID"));
    ASSERT_THAT(vkQueueSignalReleaseImageANDROID, NotNull());

    int qsriSyncFd = -1;
    auto qsriResult = vkQueueSignalReleaseImageANDROID(queue, 0, nullptr, *image, &qsriSyncFd);
    ASSERT_THAT(qsriResult, Eq(VK_SUCCESS));
    ASSERT_THAT(qsriSyncFd, Not(Eq(-1)));

    // Initially unsignaled.
    vkhpp::UniqueFence fence = device->createFenceUnique(vkhpp::FenceCreateInfo()).value;

    const vkhpp::ImportFenceFdInfoKHR importFenceInfo = {
        .fence = *fence,
        .flags = vkhpp::FenceImportFlagBits::eTemporary,
        .handleType = vkhpp::ExternalFenceHandleTypeFlagBits::eSyncFd,
        .fd = qsriSyncFd,
    };
    auto importResult = device->importFenceFdKHR(&importFenceInfo);
    ASSERT_THAT(qsriResult, Eq(VK_SUCCESS));

    const auto kMaxTimeout = std::chrono::seconds(10);

    auto begin = std::chrono::steady_clock::now();
    while (true) {
        vkhpp::Result fenceStatus = device->getFenceStatus(*fence);
        if (fenceStatus == vkhpp::Result::eSuccess) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        if ((now - begin) > kMaxTimeout) {
            ASSERT_THAT(fenceStatus, Eq(vkhpp::Result::eSuccess));
        }
    }
}

INSTANTIATE_TEST_CASE_P(GfxstreamEnd2EndTests, GfxstreamEnd2EndVkTest,
                        ::testing::ValuesIn(GenerateTestCases()), &GetTestName);

}  // namespace
}  // namespace tests
}  // namespace gfxstream
