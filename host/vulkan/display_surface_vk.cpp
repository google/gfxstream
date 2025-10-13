// Copyright (C) 2022 The Android Open Source Project
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

#include "display_surface_vk.h"

#include "gfxstream/common/logging.h"
#if defined(VK_USE_PLATFORM_XCB_KHR)
#include "gfxstream/host/X11Support.h"
#endif
#include "native_sub_window.h"
#include "vk_utils.h"

namespace gfxstream {
namespace vk {

std::unique_ptr<DisplaySurfaceVk> DisplaySurfaceVk::create(const VulkanDispatch& vk,
                                                           VkInstance instance,
                                                           FBNativeWindowType window) {
    GFXSTREAM_VERBOSE("Creating display surface");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef VK_USE_PLATFORM_WIN32_KHR
    const VkWin32SurfaceCreateInfoKHR surfaceCi = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = window,
    };
    if (vk.vkCreateWin32SurfaceKHR == nullptr) {
        GFXSTREAM_FATAL("Vulkan driver does not support display surfaces!");
    }
    VK_CHECK(vk.vkCreateWin32SurfaceKHR(instance, &surfaceCi, nullptr, &surface));
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    // TODO(b/389646068): add precondition for enabling vulkan composition path, and handle this
    // case more gracefully without crashing the emulator
    if (vk.vkCreateMacOSSurfaceMVK != nullptr) {
        // Legacy path for MoltenVK, to be removed
        const VkMacOSSurfaceCreateInfoMVK surfaceCi = {
            .sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK,
            .pNext = nullptr,
            .flags = 0,
            .pView = window,
        };
        VK_CHECK(vk.vkCreateMacOSSurfaceMVK(instance, &surfaceCi, nullptr, &surface));
    } else if (vk.vkCreateMetalSurfaceEXT != nullptr) {
        const CAMetalLayer* layer = getMetalLayerFromView(window);
        if (!layer) {
            GFXSTREAM_ERROR("Could not get a compatible metal view layer!");
            return nullptr;
        }

        const VkMetalSurfaceCreateInfoEXT surfaceCi = {
            .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .pLayer = layer,
        };
        VK_CHECK(vk.vkCreateMetalSurfaceEXT(instance, &surfaceCi, nullptr, &surface));
    } else {
        GFXSTREAM_FATAL("Vulkan driver does not support display surfaces!");
    }
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    auto x11 = getX11Api();

    Display* display = static_cast<Display*>(getNativeDisplay());
    xcb_connection_t* connection = x11 ? x11->XGetXCBConnection(display) : nullptr;
    if (connection == NULL) {
        GFXSTREAM_ERROR("Could not get a compatible window connection!");
        return nullptr;  // return, as a call with nullptr can cause emulator crashes
    }
    const VkXcbSurfaceCreateInfoKHR surfaceCi = {
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .connection = connection,
        .window = window,
    };
    if (vk.vkCreateXcbSurfaceKHR == nullptr) {
        GFXSTREAM_FATAL("Vulkan driver do not support display surfaces!");
    }
    VK_CHECK(vk.vkCreateXcbSurfaceKHR(instance, &surfaceCi, nullptr, &surface));
#else
    GFXSTREAM_FATAL("Unimplemented.");
#endif
    if (surface == VK_NULL_HANDLE) {
        GFXSTREAM_FATAL("No VkSurfaceKHR created?");
    }

    GFXSTREAM_VERBOSE("Created native vulkan surface");
    return std::unique_ptr<DisplaySurfaceVk>(new DisplaySurfaceVk(vk, instance, surface));
}

DisplaySurfaceVk::DisplaySurfaceVk(const VulkanDispatch& vk, VkInstance instance,
                                   VkSurfaceKHR surface)
    : mVk(vk), mInstance(instance), mSurface(surface) {}

DisplaySurfaceVk::~DisplaySurfaceVk() {
    if (mSurface != VK_NULL_HANDLE) {
        mVk.vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    }
}

}  // namespace vk
}  // namespace gfxstream
