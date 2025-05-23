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

#include "VkEmulatedPhysicalDeviceQueue.h"

#include <algorithm>
#include <limits>

namespace gfxstream {
namespace vk {

EmulatedPhysicalDeviceQueueProperties::EmulatedPhysicalDeviceQueueProperties(
    const std::vector<VkQueueFamilyProperties>& host, const gfxstream::host::FeatureSet& features) {
    mQueueFamilyProperties = host;

    // Override queueCount for the virtual queue to be provided with device creations
    mHasVirtualGraphicsQueue = features.VulkanVirtualQueue.enabled;
    if (mHasVirtualGraphicsQueue) {
        // This feature will enforce multiple queues on all graphics capable phsical queues
        // by creating a virtual queue object, which forwards the work streams into the
        // underlying host queue.
        // This feature will override queue properties and handling even if the host device
        // supports multiple graphics queues to reduce divergence.
        for (VkQueueFamilyProperties& qfp : mQueueFamilyProperties) {
            if (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                qfp.queueCount = 2;
            }

            // TODO(b/329845987) Protected memory is not supported yet on emulators.
            qfp.queueFlags &= ~VK_QUEUE_PROTECTED_BIT;
        }
    }
}

}  // namespace vk
}  // namespace gfxstream