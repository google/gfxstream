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

#include "DisplaySurfaceVk.h"

#include "gfxstream/common/logging.h"
#include "VkUtils.h"

namespace gfxstream {
namespace vk {

std::unique_ptr<DisplaySurfaceVk> DisplaySurfaceVk::create(const VulkanDispatch& vk,
                                                           VkInstance instance,
                                                           FBNativeWindowType window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef _WIN32
    const VkWin32SurfaceCreateInfoKHR surfaceCi = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = window,
    };
    VK_CHECK(vk.vkCreateWin32SurfaceKHR(instance, &surfaceCi, nullptr, &surface));
#else
    GFXSTREAM_FATAL("Unimplemented.");
#endif
    if (surface == VK_NULL_HANDLE) {
        GFXSTREAM_FATAL("No VkSurfaceKHR created?");
    }

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
