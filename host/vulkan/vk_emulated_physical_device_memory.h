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

#include "gfxstream/host/features.h"

namespace gfxstream {
namespace host {
namespace vk {

// A physical device may have memory types that are not desirable or are not
// supportable by the host renderer. This class helps to track the original
// host memory types, helps to track the emulated memory types shared with the
// guest, and helps to convert between both.
class EmulatedPhysicalDeviceMemoryProperties {
   public:
    EmulatedPhysicalDeviceMemoryProperties(const VkPhysicalDeviceMemoryProperties& host,
                                           uint32_t hostColorBufferMemoryTypeIndex,
                                           const gfxstream::host::FeatureSet& features);

    struct HostMemoryInfo {
        uint32_t index;
        VkMemoryType memoryType;
    };
    std::optional<HostMemoryInfo> getHostMemoryInfoFromHostMemoryTypeIndex(
        uint32_t hostMemoryTypeIndex) const;
    std::optional<HostMemoryInfo> getHostMemoryInfoFromGuestMemoryTypeIndex(
        uint32_t guestMemoryTypeIndex) const;

    const VkPhysicalDeviceMemoryProperties& getGuestMemoryProperties() const {
        return mGuestMemoryProperties;
    }
    const VkPhysicalDeviceMemoryProperties& getHostMemoryProperties() const {
        return mHostMemoryProperties;
    }

    uint32_t getGuestColorBufferMemoryTypeIndex() const { return mGuestColorBufferMemoryTypeIndex; }

    void transformToGuestMemoryRequirements(VkMemoryRequirements* hostMemoryRequirements) const;

    // As above, but a tiled image is left only the types reserved for it.
    void transformToGuestImageMemoryRequirements(
        VkImageTiling tiling, VkMemoryRequirements* hostMemoryRequirements) const;

    // Clamp heapBudget/heapUsage to the guest-visible heap sizes. No-op if budgetProps is null.
    void clampMemoryBudgetToGuestHeapSizes(
        VkPhysicalDeviceMemoryBudgetPropertiesEXT* budgetProps) const;

   private:
    uint32_t findIndexForNewMemoryType(VkMemoryPropertyFlags propertyFlags) const;

    VkPhysicalDeviceMemoryProperties mGuestMemoryProperties;
    VkPhysicalDeviceMemoryProperties mHostMemoryProperties;

    struct EmulatedGuestMemoryType {
        uint32_t hostMemoryTypeIndex;
        // Reserved for AHBs with the VulkanUseDedicatedAhbMemoryType feature.
        bool isReservedForAhbAllocations = false;
        // Reserved for tiled images on Apple with system blobs.
        bool isReservedForAppleSystemBlobAllocations = false;
        // The memory properties reported to the guest.
        VkMemoryType memoryType;
    };
    std::vector<EmulatedGuestMemoryType> mGuestMemoryTypes;

    // The memory type index reported to the guest for VkDeviceMemory requirements which would
    // try to import host ColorBuffer allocations
    // (e.g. vkGetAndroidHardwareBufferPropertiesANDROID()).
    uint32_t mGuestColorBufferMemoryTypeIndex;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
