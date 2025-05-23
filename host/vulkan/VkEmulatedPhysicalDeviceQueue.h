// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

#include "gfxstream/host/Features.h"

namespace gfxstream {
namespace vk {

class EmulatedPhysicalDeviceQueueProperties {
   public:
    EmulatedPhysicalDeviceQueueProperties(const std::vector<VkQueueFamilyProperties>& host,
                                          const gfxstream::host::FeatureSet& features);

    const std::vector<VkQueueFamilyProperties>& getQueueFamilyProperties() const {
        return mQueueFamilyProperties;
    }
    bool hasVirtualGraphicsQueue() const { return mHasVirtualGraphicsQueue; }

   private:
    std::vector<VkQueueFamilyProperties> mQueueFamilyProperties;

    // Indicates that the graphics queue family properties are overridden for
    // this physical device to include a virtual graphics queue.
    bool mHasVirtualGraphicsQueue = false;
};

}  // namespace vk
}  // namespace gfxstream