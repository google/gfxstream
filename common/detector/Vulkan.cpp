/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Vulkan.h"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace gfxstream {
namespace {

constexpr const bool kEnableValidationLayers = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
        vkhpp::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vkhpp::DebugUtilsMessageTypeFlagsEXT,
        const vkhpp::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*) {
    if (severity == vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
        std::cout << pCallbackData->pMessage << std::endl;
    } else if (severity == vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
        std::cout << pCallbackData->pMessage << std::endl;
    } else if (severity == vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        std::cout << pCallbackData->pMessage << std::endl;
    } else if (severity == vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        std::cout << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

uint32_t GetMemoryType(const vkhpp::PhysicalDevice& physical_device,
                       uint32_t memory_type_mask,
                       vkhpp::MemoryPropertyFlags memoryProperties) {
    const auto props = physical_device.getMemoryProperties();
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (!(memory_type_mask & (1 << i))) {
            continue;
        }
        if ((props.memoryTypes[i].propertyFlags & memoryProperties) != memoryProperties) {
            continue;
        }
        return i;
    }
    return -1;
}

}  // namespace

gfxstream::expected<Vk::BufferWithMemory, vkhpp::Result> DoCreateBuffer(
        const vkhpp::PhysicalDevice& physical_device,
        const vkhpp::UniqueDevice& device, vkhpp::DeviceSize buffer_size,
        vkhpp::BufferUsageFlags buffer_usages,
        vkhpp::MemoryPropertyFlags bufferMemoryProperties) {
    const vkhpp::BufferCreateInfo bufferCreateInfo = {
        .size = static_cast<VkDeviceSize>(buffer_size),
        .usage = buffer_usages,
        .sharingMode = vkhpp::SharingMode::eExclusive,
    };
    auto buffer = VK_EXPECT_RV(device->createBufferUnique(bufferCreateInfo));

    vkhpp::MemoryRequirements bufferMemoryRequirements{};
    device->getBufferMemoryRequirements(*buffer, &bufferMemoryRequirements);

    const auto bufferMemoryType =
        GetMemoryType(physical_device,
                    bufferMemoryRequirements.memoryTypeBits,
                    bufferMemoryProperties);

    const vkhpp::MemoryAllocateInfo bufferMemoryAllocateInfo = {
        .allocationSize = bufferMemoryRequirements.size,
        .memoryTypeIndex = bufferMemoryType,
    };
    auto bufferMemory = VK_EXPECT_RV(device->allocateMemoryUnique(bufferMemoryAllocateInfo));

    VK_EXPECT_RESULT(device->bindBufferMemory(*buffer, *bufferMemory, 0));

    return Vk::BufferWithMemory{
        .buffer = std::move(buffer),
        .bufferMemory = std::move(bufferMemory),
    };
}

/*static*/
gfxstream::expected<Vk, vkhpp::Result> Vk::Load(
        const std::vector<std::string>& requestedInstanceExtensions,
        const std::vector<std::string>& requestedInstanceLayers,
        const std::vector<std::string>& requestedDeviceExtensions) {
    vkhpp::detail::DynamicLoader loader;

    VULKAN_HPP_DEFAULT_DISPATCHER.init(
        loader.getProcAddress<PFN_vkGetInstanceProcAddr>(
            "vkGetInstanceProcAddr"));

    std::vector<const char*> requestedInstanceExtensionsChars;
    requestedInstanceExtensionsChars.reserve(requestedInstanceExtensions.size());
    for (const auto& e : requestedInstanceExtensions) {
        requestedInstanceExtensionsChars.push_back(e.c_str());
    }
    if (kEnableValidationLayers) {
        requestedInstanceExtensionsChars.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> requestedInstanceLayersChars;
    requestedInstanceLayersChars.reserve(requestedInstanceLayers.size());
    for (const auto& l : requestedInstanceLayers) {
        requestedInstanceLayersChars.push_back(l.c_str());
    }

    const vkhpp::ApplicationInfo applicationInfo = {
        .pApplicationName = "Cuttlefish Graphics Detector",
        .applicationVersion = 1,
        .pEngineName = "Cuttlefish Graphics Detector",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_2,
    };
    const vkhpp::InstanceCreateInfo instanceCreateInfo = {
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<uint32_t>(requestedInstanceLayersChars.size()),
        .ppEnabledLayerNames = requestedInstanceLayersChars.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requestedInstanceExtensionsChars.size()),
        .ppEnabledExtensionNames = requestedInstanceExtensionsChars.data(),
    };

    auto instance = VK_EXPECT_RV(vkhpp::createInstanceUnique(instanceCreateInfo));

    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    std::optional<vkhpp::UniqueDebugUtilsMessengerEXT> debugMessenger;
    if (kEnableValidationLayers) {
        const vkhpp::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
            .messageSeverity = vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                            vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                            vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType = vkhpp::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                        vkhpp::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                        vkhpp::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            .pfnUserCallback = VulkanDebugCallback,
            .pUserData = nullptr,
        };
        debugMessenger = VK_EXPECT_RV(instance->createDebugUtilsMessengerEXTUnique(debugCreateInfo));
    }

    const auto physicalDevices = VK_EXPECT_RV(instance->enumeratePhysicalDevices());
    vkhpp::PhysicalDevice physicalDevice = std::move(physicalDevices[0]);


    std::unordered_set<std::string> availableDeviceExtensions;
    {
        const auto exts = VK_EXPECT_RV(physicalDevice.enumerateDeviceExtensionProperties());
        for (const auto& ext : exts) {
            availableDeviceExtensions.emplace(ext.extensionName);
        }
    }

    const auto features2 =
        physicalDevice
            .getFeatures2<vkhpp::PhysicalDeviceFeatures2,  //
                            vkhpp::PhysicalDeviceSamplerYcbcrConversionFeatures>();

    bool ycbcr_conversion_needed = false;

    std::vector<const char*> requestedDeviceExtensionsChars;
    requestedDeviceExtensionsChars.reserve(requestedDeviceExtensions.size());
    for (const auto& e : requestedDeviceExtensions) {
        if (e == std::string(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME)) {
            // The interface of VK_KHR_sampler_ycbcr_conversion was promoted to core
            // in Vulkan 1.1 but the feature/functionality is still optional. Check
            // here:
            const auto& sampler_features =
                features2.get<vkhpp::PhysicalDeviceSamplerYcbcrConversionFeatures>();

            if (sampler_features.samplerYcbcrConversion == VK_FALSE) {
                return gfxstream::unexpected(vkhpp::Result::eErrorExtensionNotPresent);
            }
            ycbcr_conversion_needed = true;
        } else {
            if (availableDeviceExtensions.find(e) == availableDeviceExtensions.end()) {
                return gfxstream::unexpected(vkhpp::Result::eErrorExtensionNotPresent);
            }
            requestedDeviceExtensionsChars.push_back(e.c_str());
        }
    }

    uint32_t queueFamilyIndex = -1;
    {
        const auto props = physicalDevice.getQueueFamilyProperties();
        for (uint32_t i = 0; i < props.size(); i++) {
            const auto& prop = props[i];
            if (prop.queueFlags & vkhpp::QueueFlagBits::eGraphics) {
                queueFamilyIndex = i;
                break;
            }
        }
    }

    const float queue_priority = 1.0f;
    const vkhpp::DeviceQueueCreateInfo device_queue_create_info = {
        .queueFamilyIndex = queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };
    const vkhpp::PhysicalDeviceVulkan11Features device_enable_features = {
        .samplerYcbcrConversion = ycbcr_conversion_needed,
    };
    const vkhpp::DeviceCreateInfo deviceCreateInfo = {
        .pNext = &device_enable_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_create_info,
        .enabledLayerCount = static_cast<uint32_t>(requestedInstanceLayersChars.size()),
        .ppEnabledLayerNames = requestedInstanceLayersChars.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requestedDeviceExtensionsChars.size()),
        .ppEnabledExtensionNames = requestedDeviceExtensionsChars.data(),
    };
    auto device = VK_EXPECT_RV(physicalDevice.createDeviceUnique(deviceCreateInfo));
    auto queue = device->getQueue(queueFamilyIndex, 0);

    const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
        .queueFamilyIndex = queueFamilyIndex,
    };
    auto commandPool = VK_EXPECT_RV(device->createCommandPoolUnique(commandPoolCreateInfo));

    auto stagingBuffer =
        VK_EXPECT(DoCreateBuffer(physicalDevice, device, kStagingBufferSize,
                                 vkhpp::BufferUsageFlagBits::eTransferDst |
                                    vkhpp::BufferUsageFlagBits::eTransferSrc,
                                 vkhpp::MemoryPropertyFlagBits::eHostVisible |
                                    vkhpp::MemoryPropertyFlagBits::eHostCoherent));

    return Vk(std::move(loader),
              std::move(instance),
              std::move(debugMessenger),
              std::move(physicalDevice),
              std::move(device),
              std::move(queue),
              queueFamilyIndex,
              std::move(commandPool),
              std::move(stagingBuffer.buffer),
              std::move(stagingBuffer.bufferMemory));
}

gfxstream::expected<Vk::BufferWithMemory, vkhpp::Result> Vk::CreateBuffer(
    vkhpp::DeviceSize bufferSize,
    vkhpp::BufferUsageFlags bufferUsages,
    vkhpp::MemoryPropertyFlags bufferMemoryProperties) {
  return DoCreateBuffer(mPhysicalDevice,
                        mDevice,
                        bufferSize,
                        bufferUsages,
                        bufferMemoryProperties);
}

gfxstream::expected<Vk::BufferWithMemory, vkhpp::Result> Vk::CreateBufferWithData(
        vkhpp::DeviceSize bufferSize,
        vkhpp::BufferUsageFlags bufferUsages,
        vkhpp::MemoryPropertyFlags bufferMemoryProperties,
        const uint8_t* buffer_data) {
    auto buffer = VK_EXPECT(CreateBuffer(
        bufferSize,
        bufferUsages | vkhpp::BufferUsageFlagBits::eTransferDst,
        bufferMemoryProperties));

    void* mapped = VK_EXPECT_RV(mDevice->mapMemory(*mStagingBufferMemory, 0, kStagingBufferSize));

    std::memcpy(mapped, buffer_data, bufferSize);

    mDevice->unmapMemory(*mStagingBufferMemory);

    DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
        const std::vector<vkhpp::BufferCopy> regions = {
            vkhpp::BufferCopy{
                .srcOffset = 0,
                .dstOffset = 0,
                .size = bufferSize,
            },
        };
        cmd->copyBuffer(*mStagingBuffer, *buffer.buffer, regions);
        return vkhpp::Result::eSuccess;
    });

    return std::move(buffer);
}

gfxstream::expected<Vk::ImageWithMemory, vkhpp::Result> Vk::CreateImage(
        uint32_t width,
        uint32_t height,
        vkhpp::Format format,
        vkhpp::ImageUsageFlags usages,
        vkhpp::MemoryPropertyFlags memoryProperties,
        vkhpp::ImageLayout returnedLayout) {
    const vkhpp::ImageCreateInfo imageCreateInfo = {
        .imageType = vkhpp::ImageType::e2D,
        .format = format,
        .extent = {
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
    auto image = VK_EXPECT_RV(mDevice->createImageUnique(imageCreateInfo));

    const auto memoryRequirements = mDevice->getImageMemoryRequirements(*image);
    const uint32_t memoryIndex =
        GetMemoryType(mPhysicalDevice,
                      memoryRequirements.memoryTypeBits,
                      memoryProperties);

    const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = memoryIndex,
    };
    auto imageMemory = VK_EXPECT_RV(mDevice->allocateMemoryUnique(imageMemoryAllocateInfo));

    mDevice->bindImageMemory(*image, *imageMemory, 0);

    const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
        .image = *image,
        .viewType = vkhpp::ImageViewType::e2D,
        .format = format,
        .components = {
            .r = vkhpp::ComponentSwizzle::eIdentity,
            .g = vkhpp::ComponentSwizzle::eIdentity,
            .b = vkhpp::ComponentSwizzle::eIdentity,
            .a = vkhpp::ComponentSwizzle::eIdentity,
        },
        .subresourceRange = {
            .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    auto imageView = VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));

    VK_EXPECT_RESULT(DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
        const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
            vkhpp::ImageMemoryBarrier{
                .srcAccessMask = {},
                .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
                .oldLayout = vkhpp::ImageLayout::eUndefined,
                .newLayout = returnedLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *image,
                .subresourceRange = {
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

        return vkhpp::Result::eSuccess;
    }));

    return ImageWithMemory{
        .image = std::move(image),
        .imageMemory = std::move(imageMemory),
        .imageView = std::move(imageView),
    };
}

gfxstream::expected<std::vector<uint8_t>, vkhpp::Result> Vk::DownloadImage(
        uint32_t width,
        uint32_t height,
        const vkhpp::UniqueImage& image,
        vkhpp::ImageLayout currentLayout,
        vkhpp::ImageLayout returnedLayout) {
    VK_EXPECT_RESULT(
        DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
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
                    .subresourceRange = {
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
            cmd->copyImageToBuffer(*image,
                                   vkhpp::ImageLayout::eTransferSrcOptimal,
                                   *mStagingBuffer, regions);

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
                        .subresourceRange = {
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

            return vkhpp::Result::eSuccess;
        }));

    auto* mapped = reinterpret_cast<uint8_t*>(
        VK_EXPECT_RV(mDevice->mapMemory(*mStagingBufferMemory, 0, kStagingBufferSize)));

    std::vector<uint8_t> outPixels;
    outPixels.resize(width * height * 4);

    std::memcpy(outPixels.data(), mapped, outPixels.size());

    mDevice->unmapMemory(*mStagingBufferMemory);

    return outPixels;
}

gfxstream::expected<Vk::YuvImageWithMemory, vkhpp::Result> Vk::CreateYuvImage(
        uint32_t width,
        uint32_t height,
        vkhpp::ImageUsageFlags usages,
        vkhpp::MemoryPropertyFlags memoryProperties,
        vkhpp::ImageLayout layout) {
    const vkhpp::SamplerYcbcrConversionCreateInfo conversionCreateInfo = {
        .format = vkhpp::Format::eG8B8R83Plane420Unorm,
        .ycbcrModel = vkhpp::SamplerYcbcrModelConversion::eYcbcr601,
        .ycbcrRange = vkhpp::SamplerYcbcrRange::eItuNarrow,
        .components = {
            .r = vkhpp::ComponentSwizzle::eIdentity,
            .g = vkhpp::ComponentSwizzle::eIdentity,
            .b = vkhpp::ComponentSwizzle::eIdentity,
            .a = vkhpp::ComponentSwizzle::eIdentity,
        },
        .xChromaOffset = vkhpp::ChromaLocation::eMidpoint,
        .yChromaOffset = vkhpp::ChromaLocation::eMidpoint,
        .chromaFilter = vkhpp::Filter::eLinear,
        .forceExplicitReconstruction = VK_FALSE,
    };
    auto imageSamplerConversion = VK_EXPECT_RV(mDevice->createSamplerYcbcrConversionUnique(conversionCreateInfo));

    const vkhpp::SamplerYcbcrConversionInfo samplerConversionInfo = {
        .conversion = *imageSamplerConversion,
    };
    const vkhpp::SamplerCreateInfo samplerCreateInfo = {
        .pNext = &samplerConversionInfo,
        .magFilter = vkhpp::Filter::eLinear,
        .minFilter = vkhpp::Filter::eLinear,
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
        .maxLod = 0.25f,
        .borderColor = vkhpp::BorderColor::eIntTransparentBlack,
        .unnormalizedCoordinates = VK_FALSE,
    };
    auto imageSampler = VK_EXPECT_RV(mDevice->createSamplerUnique(samplerCreateInfo));

    const vkhpp::ImageCreateInfo imageCreateInfo = {
        .imageType = vkhpp::ImageType::e2D,
        .format = vkhpp::Format::eG8B8R83Plane420Unorm,
        .extent = {
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
    auto image = VK_EXPECT_RV(mDevice->createImageUnique(imageCreateInfo));

    const auto memoryRequirements = mDevice->getImageMemoryRequirements(*image);

    const uint32_t memoryIndex =
        GetMemoryType(mPhysicalDevice,
                      memoryRequirements.memoryTypeBits,
                      memoryProperties);

    const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = memoryIndex,
    };
    auto imageMemory = VK_EXPECT_RV(mDevice->allocateMemoryUnique(imageMemoryAllocateInfo));

    mDevice->bindImageMemory(*image, *imageMemory, 0);

    const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
        .pNext = &samplerConversionInfo,
        .image = *image,
        .viewType = vkhpp::ImageViewType::e2D,
        .format = vkhpp::Format::eG8B8R83Plane420Unorm,
        .components = {
            .r = vkhpp::ComponentSwizzle::eIdentity,
            .g = vkhpp::ComponentSwizzle::eIdentity,
            .b = vkhpp::ComponentSwizzle::eIdentity,
            .a = vkhpp::ComponentSwizzle::eIdentity,
        },
        .subresourceRange = {
            .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    auto imageView = VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));

    VK_EXPECT_RESULT(DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
        const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
            vkhpp::ImageMemoryBarrier{
                .srcAccessMask = {},
                .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
                .oldLayout = vkhpp::ImageLayout::eUndefined,
                .newLayout = layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *image,
                .subresourceRange = {
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
        return vkhpp::Result::eSuccess;
    }));

    return YuvImageWithMemory{
        .imageSamplerConversion = std::move(imageSamplerConversion),
        .imageSampler = std::move(imageSampler),
        .imageMemory = std::move(imageMemory),
        .image = std::move(image),
        .imageView = std::move(imageView),
    };
}

vkhpp::Result Vk::LoadYuvImage(
        const vkhpp::UniqueImage& image,
        uint32_t width,
        uint32_t height,
        const std::vector<uint8_t>& imageDataY,
        const std::vector<uint8_t>& imageDataU,
        const std::vector<uint8_t>& imageDataV,
        vkhpp::ImageLayout currentLayout,
        vkhpp::ImageLayout returnedLayout) {
    auto* mapped = reinterpret_cast<uint8_t*>(VK_TRY_RV(mDevice->mapMemory(*mStagingBufferMemory, 0, kStagingBufferSize)));

    const VkDeviceSize yOffset = 0;
    const VkDeviceSize uOffset = imageDataY.size();
    const VkDeviceSize vOffset = imageDataY.size() + imageDataU.size();
    std::memcpy(mapped + yOffset, imageDataY.data(), imageDataY.size());
    std::memcpy(mapped + uOffset, imageDataU.data(), imageDataU.size());
    std::memcpy(mapped + vOffset, imageDataV.data(), imageDataV.size());
    mDevice->unmapMemory(*mStagingBufferMemory);

    return DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
        if (currentLayout != vkhpp::ImageLayout::eTransferDstOptimal) {
        const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
            vkhpp::ImageMemoryBarrier{
                .srcAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                                vkhpp::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
                .oldLayout = currentLayout,
                .newLayout = vkhpp::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *image,
                .subresourceRange = {
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

        const std::vector<vkhpp::BufferImageCopy> imageCopyRegions = {
            vkhpp::BufferImageCopy{
                .bufferOffset = yOffset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = vkhpp::ImageAspectFlagBits::ePlane0,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageOffset = {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                .imageExtent = {
                    .width = width,
                    .height = height,
                    .depth = 1,
                },
            },
            vkhpp::BufferImageCopy{
                .bufferOffset = uOffset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = vkhpp::ImageAspectFlagBits::ePlane1,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageOffset = {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                .imageExtent = {
                    .width = width / 2,
                    .height = height / 2,
                    .depth = 1,
                },
            },
            vkhpp::BufferImageCopy{
                .bufferOffset = vOffset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = vkhpp::ImageAspectFlagBits::ePlane2,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageOffset = {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                .imageExtent = {
                    .width = width / 2,
                    .height = height / 2,
                    .depth = 1,
                },
            },
        };
        cmd->copyBufferToImage(*mStagingBuffer,
                               *image,
                               vkhpp::ImageLayout::eTransferDstOptimal,
                               imageCopyRegions);

        if (returnedLayout != vkhpp::ImageLayout::eTransferDstOptimal) {
            const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
                vkhpp::ImageMemoryBarrier{
                    .srcAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
                    .dstAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                                    vkhpp::AccessFlagBits::eMemoryWrite,
                    .oldLayout = vkhpp::ImageLayout::eTransferDstOptimal,
                    .newLayout = returnedLayout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = *image,
                    .subresourceRange = {
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
        return vkhpp::Result::eSuccess;
    });
}

gfxstream::expected<Vk::FramebufferWithAttachments, vkhpp::Result>
Vk::CreateFramebuffer(
        uint32_t width,
        uint32_t height,
        vkhpp::Format color_format,
        vkhpp::Format depth_format) {
    std::optional<Vk::ImageWithMemory> colorAttachment;
    if (color_format != vkhpp::Format::eUndefined) {
        colorAttachment =
            GFXSTREAM_EXPECT(CreateImage(width, height, color_format,
                                vkhpp::ImageUsageFlagBits::eColorAttachment |
                                    vkhpp::ImageUsageFlagBits::eTransferSrc,
                                vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                                vkhpp::ImageLayout::eColorAttachmentOptimal));
    }

    std::optional<Vk::ImageWithMemory> depthAttachment;
    if (depth_format != vkhpp::Format::eUndefined) {
        depthAttachment =
            GFXSTREAM_EXPECT(CreateImage(width, height, depth_format,
                                vkhpp::ImageUsageFlagBits::eDepthStencilAttachment |
                                    vkhpp::ImageUsageFlagBits::eTransferSrc,
                                vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                                vkhpp::ImageLayout::eDepthStencilAttachmentOptimal));
    }

    std::vector<vkhpp::AttachmentDescription> attachments;

    std::optional<vkhpp::AttachmentReference> colorAttachment_reference;
    if (color_format != vkhpp::Format::eUndefined) {
        attachments.push_back(vkhpp::AttachmentDescription{
            .format = color_format,
            .samples = vkhpp::SampleCountFlagBits::e1,
            .loadOp = vkhpp::AttachmentLoadOp::eClear,
            .storeOp = vkhpp::AttachmentStoreOp::eStore,
            .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
            .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
            .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
            .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
        });

        colorAttachment_reference = vkhpp::AttachmentReference{
            .attachment = static_cast<uint32_t>(attachments.size() - 1),
            .layout = vkhpp::ImageLayout::eColorAttachmentOptimal,
        };
    }

    std::optional<vkhpp::AttachmentReference> depthAttachment_reference;
    if (depth_format != vkhpp::Format::eUndefined) {
        attachments.push_back(vkhpp::AttachmentDescription{
            .format = depth_format,
            .samples = vkhpp::SampleCountFlagBits::e1,
            .loadOp = vkhpp::AttachmentLoadOp::eClear,
            .storeOp = vkhpp::AttachmentStoreOp::eStore,
            .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
            .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
            .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
            .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
        });

        depthAttachment_reference = vkhpp::AttachmentReference{
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
    if (color_format != vkhpp::Format::eUndefined) {
        dependency.srcStageMask |=
            vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask |=
            vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask |= vkhpp::AccessFlagBits::eColorAttachmentWrite;
    }
    if (depth_format != vkhpp::Format::eUndefined) {
        dependency.srcStageMask |=
            vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask |=
            vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
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
    if (color_format != vkhpp::Format::eUndefined) {
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &*colorAttachment_reference;
    }
    if (depth_format != vkhpp::Format::eUndefined) {
        subpass.pDepthStencilAttachment = &*depthAttachment_reference;
    }

    const vkhpp::RenderPassCreateInfo renderpassCreateInfo = {
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    auto renderpass = VK_EXPECT_RV(mDevice->createRenderPassUnique(renderpassCreateInfo));

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
    auto framebuffer = VK_EXPECT_RV(mDevice->createFramebufferUnique(framebufferCreateInfo));

    return Vk::FramebufferWithAttachments{
        .colorAttachment = std::move(colorAttachment),
        .depthAttachment = std::move(depthAttachment),
        .renderpass = std::move(renderpass),
        .framebuffer = std::move(framebuffer),
    };
}

vkhpp::Result Vk::DoCommandsImmediate(
        const std::function<vkhpp::Result(vkhpp::UniqueCommandBuffer&)>& func,
        const std::vector<vkhpp::UniqueSemaphore>& semaphores_wait,
        const std::vector<vkhpp::UniqueSemaphore>& semaphores_signal) {
    const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
        .commandPool = *mCommandPool,
        .level = vkhpp::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    auto commandBuffers = VK_TRY_RV(mDevice->allocateCommandBuffersUnique(commandBufferAllocateInfo));
    auto commandBuffer = std::move(commandBuffers[0]);

    const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
        .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer->begin(commandBufferBeginInfo);
    VK_TRY(func(commandBuffer));
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
    mQueue.submit(submitInfo);
    mQueue.waitIdle();

    return vkhpp::Result::eSuccess;
}

}  // namespace gfxstream
