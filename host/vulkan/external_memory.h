// Copyright 2025 The Android Open Source Project
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

#include <string>
#include <vector>

namespace gfxstream {
namespace host {
namespace vk {

// Class to encapsulate external memory related functionalities
class ExternalMemory {
   public:
    enum class Mode {
        Unknown = 0,
        OpaqueFd,         // VK_KHR_external_memory_fd
        OpaqueWin32,      // VK_KHR_external_memory_win32
        Metal,            // VK_EXT_external_memory_metal
        AndroidAHB,       // VK_ANDROID_external_memory_android_hardware_buffer
        QnxScreenBuffer,  // VK_QNX_external_memory_screen_buffer
    };

    static const char* to_string(const Mode mode);
    static Mode calculateMode(std::vector<VkExtensionProperties>& deviceExts);
    static VkExternalMemoryHandleTypeFlagBits getHandleType(const Mode mode);
    static void getDeviceExtensionsForMode(const Mode mode,
                                           std::vector<const char*>& outDeviceExtensions);
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
