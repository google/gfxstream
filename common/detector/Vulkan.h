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

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#define VULKAN_HPP_NAMESPACE vkhpp
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>

#include "Expected.h"

namespace gfxstream {

#define VK_EXPECT(x)                                        \
    ({                                                      \
        auto expected = (x);                                \
        if (!expected.ok()) {                               \
            return gfxstream::unexpected(expected.error()); \
        };                                                  \
        std::move(expected.value());                        \
    })

#define VK_EXPECT_RESULT(x)                                 \
    do {                                                    \
        vkhpp::Result result = (x);                         \
        if (result != vkhpp::Result::eSuccess) {            \
            return gfxstream::unexpected(result);           \
        }                                                   \
    } while (0);

#define VK_EXPECT_RV(x)                                     \
    ({                                                      \
        auto vkhpp_rv = (x);                                \
        if (vkhpp_rv.result != vkhpp::Result::eSuccess) {   \
            return gfxstream::unexpected(vkhpp_rv.result);  \
        };                                                  \
        std::move(vkhpp_rv.value);                          \
    })

#define VK_EXPECT_RV_OR_STRING(x)                           \
    ({                                                      \
        auto vkhpp_rv = (x);                                \
        if (vkhpp_rv.result != vkhpp::Result::eSuccess) {   \
            return gfxstream::unexpected(                   \
                std::string("Failed to " #x ": ") +         \
                vkhpp::to_string(vkhpp_rv.result));         \
        };                                                  \
        std::move(vkhpp_rv.value);                          \
    })

#define VK_TRY(x)                                           \
    do {                                                    \
        vkhpp::Result result = (x);                         \
        if (result != vkhpp::Result::eSuccess) {            \
            return result;                                  \
        }                                                   \
    } while (0);

#define VK_TRY_RV(x)                                        \
    ({                                                      \
        auto vkhpp_rv = (x);                                \
        if (vkhpp_rv.result != vkhpp::Result::eSuccess) {   \
            return vkhpp_rv.result;                         \
        };                                                  \
        std::move(vkhpp_rv.value);                          \
    })

class Vk {
  public:
    static gfxstream::expected<Vk, vkhpp::Result> Load(
        const std::vector<std::string>& instance_extensions = {},
        const std::vector<std::string>& instance_layers = {},
        const std::vector<std::string>& device_extensions = {});

    Vk(const Vk&) = delete;
    Vk& operator=(const Vk&) = delete;

    Vk(Vk&&) = default;
    Vk& operator=(Vk&&) = default;

    struct BufferWithMemory {
        vkhpp::UniqueBuffer buffer;
        vkhpp::UniqueDeviceMemory bufferMemory;
    };
    gfxstream::expected<BufferWithMemory, vkhpp::Result> CreateBuffer(
        vkhpp::DeviceSize buffer_size,
        vkhpp::BufferUsageFlags buffer_usages,
        vkhpp::MemoryPropertyFlags buffer_memory_properties);
    gfxstream::expected<BufferWithMemory, vkhpp::Result> CreateBufferWithData(
        vkhpp::DeviceSize buffer_size,
        vkhpp::BufferUsageFlags buffer_usages,
        vkhpp::MemoryPropertyFlags buffer_memory_properties,
        const uint8_t* buffer_data);

    vkhpp::Result DoCommandsImmediate(
        const std::function<vkhpp::Result(vkhpp::UniqueCommandBuffer&)>& func,
        const std::vector<vkhpp::UniqueSemaphore>& semaphores_wait = {},
        const std::vector<vkhpp::UniqueSemaphore>& semaphores_signal = {});

    struct ImageWithMemory {
        vkhpp::UniqueImage image;
        vkhpp::UniqueDeviceMemory imageMemory;
        vkhpp::UniqueImageView imageView;
    };
    gfxstream::expected<ImageWithMemory, vkhpp::Result> CreateImage(
        uint32_t width,
        uint32_t height,
        vkhpp::Format format,
        vkhpp::ImageUsageFlags usages,
        vkhpp::MemoryPropertyFlags memory_properties,
        vkhpp::ImageLayout returned_layout);

    gfxstream::expected<std::vector<uint8_t>, vkhpp::Result> DownloadImage(
        uint32_t width,
        uint32_t height,
        const vkhpp::UniqueImage& image,
        vkhpp::ImageLayout current_layout,
        vkhpp::ImageLayout returned_layout);

    struct YuvImageWithMemory {
        vkhpp::UniqueSamplerYcbcrConversion imageSamplerConversion;
        vkhpp::UniqueSampler imageSampler;
        vkhpp::UniqueDeviceMemory imageMemory;
        vkhpp::UniqueImage image;
        vkhpp::UniqueImageView imageView;
    };
    gfxstream::expected<YuvImageWithMemory, vkhpp::Result> CreateYuvImage(
        uint32_t width,
        uint32_t height,
        vkhpp::ImageUsageFlags usages,
        vkhpp::MemoryPropertyFlags memory_properties,
        vkhpp::ImageLayout returned_layout);

    vkhpp::Result LoadYuvImage(const vkhpp::UniqueImage& image,
                               uint32_t width,
                               uint32_t height,
                               const std::vector<uint8_t>& image_data_y,
                               const std::vector<uint8_t>& image_data_u,
                               const std::vector<uint8_t>& image_data_v,
                               vkhpp::ImageLayout current_layout,
                               vkhpp::ImageLayout returned_layout);

    struct FramebufferWithAttachments {
        std::optional<ImageWithMemory> colorAttachment;
        std::optional<ImageWithMemory> depthAttachment;
        vkhpp::UniqueRenderPass renderpass;
        vkhpp::UniqueFramebuffer framebuffer;
    };
    gfxstream::expected<FramebufferWithAttachments, vkhpp::Result> CreateFramebuffer(
        uint32_t width,
        uint32_t height,
        vkhpp::Format colorAttachmentFormat = vkhpp::Format::eUndefined,
        vkhpp::Format depthAttachmentFormat = vkhpp::Format::eUndefined);

    vkhpp::Instance& instance() { return *mInstance; }

    vkhpp::Device& device() { return *mDevice; }

  private:
    Vk(vkhpp::detail::DynamicLoader loader,
       vkhpp::UniqueInstance instance,
       std::optional<vkhpp::UniqueDebugUtilsMessengerEXT> debug,
       vkhpp::PhysicalDevice physical_device,
       vkhpp::UniqueDevice device,
       vkhpp::Queue queue,
       uint32_t queue_family_index,
       vkhpp::UniqueCommandPool command_pool,
       vkhpp::UniqueBuffer stagingBuffer,
       vkhpp::UniqueDeviceMemory stagingBufferMemory)
        : mLoader(std::move(loader)),
          mInstance(std::move(instance)),
          mDebugMessenger(std::move(debug)),
          mPhysicalDevice(std::move(physical_device)),
          mDevice(std::move(device)),
          mQueue(std::move(queue)),
          mQueueFamilyIndex(queue_family_index),
          mCommandPool(std::move(command_pool)),
          mStagingBuffer(std::move(stagingBuffer)),
          mStagingBufferMemory(std::move(stagingBufferMemory)) {}

    // Note: order is important for destruction.
    vkhpp::detail::DynamicLoader mLoader;
    vkhpp::UniqueInstance mInstance;
    std::optional<vkhpp::UniqueDebugUtilsMessengerEXT> mDebugMessenger;
    vkhpp::PhysicalDevice mPhysicalDevice;
    vkhpp::UniqueDevice mDevice;
    vkhpp::Queue mQueue;
    uint32_t mQueueFamilyIndex;
    vkhpp::UniqueCommandPool mCommandPool;
    static constexpr const VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;
    vkhpp::UniqueBuffer mStagingBuffer;
    vkhpp::UniqueDeviceMemory mStagingBufferMemory;
};

}  // namespace cuttlefish
