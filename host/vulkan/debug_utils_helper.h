// Copyright 2022 The Android Open Source Project
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

#pragma once

#include <vulkan/vulkan.h>

#include "goldfish_vk_dispatch.h"

namespace gfxstream {
namespace vk {

class DebugUtilsHelper {
   public:
    static DebugUtilsHelper withUtilsEnabled(VkDevice device, const VulkanDispatch* dispatch);

    static DebugUtilsHelper withUtilsDisabled();

    ~DebugUtilsHelper() = default;

    template <typename VkObjectT, typename... T>
    void addDebugLabel(VkObjectT object, const char* format, T&&... formatArgs) const {
        if (!m_debugUtilsEnabled) {
            return;
        }

        VkObjectType objectType = VK_OBJECT_TYPE_UNKNOWN;
        if constexpr (std::is_same_v<VkObjectT, VkBuffer>) {
            objectType = VK_OBJECT_TYPE_BUFFER;
        } else if constexpr (std::is_same_v<VkObjectT, VkCommandBuffer>) {
            objectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
        } else if constexpr (std::is_same_v<VkObjectT, VkCommandPool>) {
            objectType = VK_OBJECT_TYPE_COMMAND_POOL;
        } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorSet>) {
            objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;
        } else if constexpr (std::is_same_v<VkObjectT, VkDevice>) {
            objectType = VK_OBJECT_TYPE_DEVICE;
        } else if constexpr (std::is_same_v<VkObjectT, VkDeviceMemory>) {
            objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
        } else if constexpr (std::is_same_v<VkObjectT, VkFence>) {
            objectType = VK_OBJECT_TYPE_FENCE;
        } else if constexpr (std::is_same_v<VkObjectT, VkFramebuffer>) {
            objectType = VK_OBJECT_TYPE_FRAMEBUFFER;
        } else if constexpr (std::is_same_v<VkObjectT, VkImage>) {
            objectType = VK_OBJECT_TYPE_IMAGE;
        } else if constexpr (std::is_same_v<VkObjectT, VkImageView>) {
            objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
        } else if constexpr (std::is_same_v<VkObjectT, VkInstance>) {
            objectType = VK_OBJECT_TYPE_INSTANCE;
        } else if constexpr (std::is_same_v<VkObjectT, VkPipeline>) {
            objectType = VK_OBJECT_TYPE_PIPELINE;
        } else if constexpr (std::is_same_v<VkObjectT, VkSampler>) {
            objectType = VK_OBJECT_TYPE_SAMPLER;
        } else {
            static_assert(sizeof(VkObjectT) == 0,
                          "Unhandled VkObjectT. Please update DebugUtilsHelper.h.");
        }

        addDebugLabelToHandle((uint64_t)object, objectType, format, std::forward<T>(formatArgs)...);
    }

    void cmdBeginDebugLabel(VkCommandBuffer commandBuffer, const char* format, ...) const;
    void cmdEndDebugLabel(VkCommandBuffer commandBuffer) const;

    bool isEnabled() const { return m_debugUtilsEnabled; }

   private:
    DebugUtilsHelper(bool enabled, VkDevice device, const VulkanDispatch* dispatch);

    void addDebugLabelToHandle(uint64_t object, VkObjectType objectType, const char* format,
                               ...) const;

    bool m_debugUtilsEnabled = false;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    const VulkanDispatch* m_vk = nullptr;
};

}  // namespace vk
}  // namespace gfxstream
