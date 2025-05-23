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

#include "DebugUtilsHelper.h"

#include <inttypes.h>

#include <cstdarg>
#include <cstdio>

#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace vk {

/*static*/ DebugUtilsHelper DebugUtilsHelper::withUtilsDisabled() {
    return DebugUtilsHelper(false, VK_NULL_HANDLE, nullptr);
}

/*static*/ DebugUtilsHelper DebugUtilsHelper::withUtilsEnabled(VkDevice device,
                                                               const VulkanDispatch* dispatch) {
    return DebugUtilsHelper(true, device, dispatch);
}

DebugUtilsHelper::DebugUtilsHelper(bool enabled, VkDevice device, const VulkanDispatch* dispatch)
    : m_debugUtilsEnabled(enabled), m_vkDevice(device), m_vk(dispatch) {}

void DebugUtilsHelper::addDebugLabelToHandle(uint64_t object, VkObjectType objectType,
                                             const char* format, ...) const {
    if (!m_debugUtilsEnabled) {
        return;
    }

    char objectLabelBuffer[256];

    va_list vararg;
    va_start(vararg, format);
    vsnprintf(objectLabelBuffer, sizeof(objectLabelBuffer), format, vararg);
    va_end(vararg);

    const VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext = nullptr,
        .objectType = objectType,
        .objectHandle = object,
        .pObjectName = objectLabelBuffer,
    };

    VkResult result = m_vk->vkSetDebugUtilsObjectNameEXT(m_vkDevice, &objectNameInfo);
    if (result != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to add debug label to %" PRIu64, object);
    }
}

void DebugUtilsHelper::cmdBeginDebugLabel(VkCommandBuffer commandBuffer, const char* format,
                                          ...) const {
    if (!m_debugUtilsEnabled) {
        return;
    }

    va_list vararg;
    va_start(vararg, format);
    char labelBuffer[256];
    vsnprintf(labelBuffer, sizeof(labelBuffer), format, vararg);
    va_end(vararg);

    const VkDebugUtilsLabelEXT labelInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext = nullptr,
        .pLabelName = labelBuffer,
        .color = { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    m_vk->vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &labelInfo);
}

void DebugUtilsHelper::cmdEndDebugLabel(VkCommandBuffer commandBuffer) const {
    if (!m_debugUtilsEnabled) {
        return;
    }

    m_vk->vkCmdEndDebugUtilsLabelEXT(commandBuffer);
}

}  // namespace vk
}  // namespace gfxstream
