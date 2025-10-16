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

#include "external_memory.h"

#include "vk_utils.h"

namespace gfxstream {
namespace host {
namespace vk {

const char* ExternalMemory::to_string(const ExternalMemory::Mode mode) {
    switch (mode) {
        case Mode::Unknown:
            return "Unknown";
        case Mode::NotSupported:
            return "NotSupported";
        case Mode::OpaqueFd:
            return "OpaqueFd";
        case Mode::OpaqueWin32:
            return "OpaqueWin32";
        case Mode::Metal:
            return "Metal";
        case Mode::AndroidAHB:
            return "AndroidAHB";
        case Mode::QnxScreenBuffer:
            return "QnxScreenBuffer";
    }
    return "Unhandled";
}

ExternalMemory::Mode ExternalMemory::calculateMode(std::vector<VkExtensionProperties>& deviceExts) {
#if defined(_WIN32)
    std::array<Mode, 1> supportedModes = {Mode::OpaqueWin32};
#elif defined(__ANDROID__)
    std::array<Mode, 1> supportedModes = {
        Mode::AndroidAHB,
    };
#elif defined(__QNX__)
    // TODO(aruby@blackberry.com): Use (DMABUF|OPAQUE_FD) on QNX, when screen_buffer not supported?
    std::array<Mode, 1> supportedModes = {
        Mode::QnxScreenBuffer,
    };
#elif defined(__APPLE__)
    std::array<Mode, 2> supportedModes = {
        Mode::Metal,
        Mode::OpaqueFd,
    };
#else
    std::array<Mode, 1> supportedModes = {
        Mode::OpaqueFd,
    };
#endif

    for (auto mode : supportedModes) {
        std::vector<const char*> extRequired;
        getDeviceExtensionsForMode(mode, extRequired);
        if (vk_util::extensionsSupported(deviceExts, extRequired)) {
            // Supported modes are in-order of preference, return the first one supported
            return mode;
        }
    }

    GFXSTREAM_ERROR("%s: Vulkan driver doesn't support any external memory modes!", __func__);
    return Mode::NotSupported;
}

VkExternalMemoryHandleTypeFlagBits ExternalMemory::getHandleType(const ExternalMemory::Mode mode) {
    switch (mode) {
        case Mode::OpaqueFd:
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        case Mode::OpaqueWin32:
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        case Mode::Metal:
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
        case Mode::AndroidAHB:
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
        case Mode::QnxScreenBuffer:
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_SCREEN_BUFFER_BIT_QNX;
        default:
            // Should not call this function with Unknown. Not a fatal error as the
            // value retrieved might be used with external memory support check
            GFXSTREAM_ERROR("%s: Unhandled external memory mode '%s'", __func__, to_string(mode));
    }

    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
}

void ExternalMemory::getDeviceExtensionsForMode(const ExternalMemory::Mode mode,
                                                std::vector<const char*>& outDeviceExtensions) {
    // These will always be necessary
    outDeviceExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
    outDeviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    outDeviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);

    switch (mode) {
        case Mode::OpaqueFd:
            outDeviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
            break;
#ifdef VK_USE_PLATFORM_WIN32_KHR
        case Mode::OpaqueWin32:
            outDeviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
            break;
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
        case Mode::Metal:
            outDeviceExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_METAL_EXTENSION_NAME);
            break;
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        case Mode::AndroidAHB:
            outDeviceExtensions.push_back(
                VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
            break;
#endif
#ifdef VK_USE_PLATFORM_SCREEN_QNX
        case Mode::QnxScreenBuffer:
            outDeviceExtensions.push_back(VK_QNX_EXTERNAL_MEMORY_SCREEN_BUFFER_EXTENSION_NAME);
            // EXT_queue_family_foreign is an extension dependency of
            // VK_QNX_external_memory_screen_buffer
            outDeviceExtensions.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
            break;
#endif
        default:
            GFXSTREAM_FATAL("%s: Invalid external memory mode %s!", __func__, to_string(mode));
    }
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream