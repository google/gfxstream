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

#include "VkEmulatedPhysicalDeviceMemory.h"

#include <algorithm>
#include <limits>

#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace vk {
namespace {

static constexpr const uint32_t kInvalidMemoryTypeIndex = std::numeric_limits<uint32_t>::max();

}  // namespace

EmulatedPhysicalDeviceMemoryProperties::EmulatedPhysicalDeviceMemoryProperties(
    const VkPhysicalDeviceMemoryProperties& hostMemoryProperties,
    const uint32_t hostColorBufferMemoryTypeIndex, const gfxstream::host::FeatureSet& features) {
    // Start with the original host memory properties:
    mHostMemoryProperties = hostMemoryProperties;
    mGuestMemoryProperties = hostMemoryProperties;
    std::fill_n(mGuestToHostMemoryTypeIndexMap, VK_MAX_MEMORY_TYPES, kInvalidMemoryTypeIndex);
    std::fill_n(mHostToGuestMemoryTypeIndexMap, VK_MAX_MEMORY_TYPES, kInvalidMemoryTypeIndex);
    for (uint32_t i = 0; i < mHostMemoryProperties.memoryTypeCount; i++) {
        mGuestToHostMemoryTypeIndexMap[i] = i;
        mHostToGuestMemoryTypeIndexMap[i] = i;
    }
    mGuestColorBufferMemoryTypeIndex = hostColorBufferMemoryTypeIndex;

    // Hide any bogus heap sizes from bad drivers with a reasonable default that will not
    // break the bank on 32-bit userspaces.
    static constexpr VkDeviceSize kMaxSafeHeapSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    for (uint32_t i = 0; i < mHostMemoryProperties.memoryHeapCount; i++) {
        if (mGuestMemoryProperties.memoryHeaps[i].size > kMaxSafeHeapSize) {
            mGuestMemoryProperties.memoryHeaps[i].size = kMaxSafeHeapSize;
        }
    }

    // If enabled, hide non device memory types from the guest.
    // (useful to work around a bug where KVM can't map TTM memory).
    if (features.VulkanAllocateDeviceMemoryOnly.enabled) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            auto guestMemoryProperties = mGuestMemoryProperties.memoryTypes[i].propertyFlags;
            if (!(guestMemoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags = 0;
            }
        }
    }

    // Coherent memory in the guest requires one of these features:
    if (!features.GlDirectMem.enabled && !features.VirtioGpuNext.enabled) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            mGuestMemoryProperties.memoryTypes[i].propertyFlags =
                mGuestMemoryProperties.memoryTypes[i].propertyFlags &
                ~(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
    }

    // If enabled, reserve an additional memory type for AHB backed buffers and images
    // so that the host can control its memory properties. This ensures that the guest
    // only sees `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` and will not try to map the
    // memory.
    if (features.VulkanUseDedicatedAhbMemoryType.enabled) {
        if (mGuestMemoryProperties.memoryTypeCount == VK_MAX_MEMORY_TYPES) {
            GFXSTREAM_FATAL("Unable to create emulated AHB memory type because VK_MAX_MEMORY_TYPES "
                            "already in use.");
        }

        uint32_t ahbMemoryTypeIndex = mGuestMemoryProperties.memoryTypeCount;
        ++mGuestMemoryProperties.memoryTypeCount;

        VkMemoryType& ahbMemoryType = mGuestMemoryProperties.memoryTypes[ahbMemoryTypeIndex];
        ahbMemoryType.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        ahbMemoryType.heapIndex =
            mHostMemoryProperties.memoryTypes[hostColorBufferMemoryTypeIndex].heapIndex;

        mGuestToHostMemoryTypeIndexMap[ahbMemoryTypeIndex] = hostColorBufferMemoryTypeIndex;

        mGuestColorBufferMemoryTypeIndex = ahbMemoryTypeIndex;
    }
}

std::optional<EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo>
EmulatedPhysicalDeviceMemoryProperties::getHostMemoryInfoFromHostMemoryTypeIndex(
    uint32_t hostMemoryTypeIndex) const {
    if (hostMemoryTypeIndex >= mHostMemoryProperties.memoryTypeCount) {
        return std::nullopt;
    }

    return HostMemoryInfo{
        .index = hostMemoryTypeIndex,
        .memoryType = mHostMemoryProperties.memoryTypes[hostMemoryTypeIndex],
    };
}

std::optional<EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo>
EmulatedPhysicalDeviceMemoryProperties::getHostMemoryInfoFromGuestMemoryTypeIndex(
    uint32_t guestMemoryTypeIndex) const {
    if (guestMemoryTypeIndex >= mGuestMemoryProperties.memoryTypeCount) {
        return std::nullopt;
    }

    uint32_t hostMemoryTypeIndex = mGuestToHostMemoryTypeIndexMap[guestMemoryTypeIndex];
    if (hostMemoryTypeIndex == kInvalidMemoryTypeIndex) {
        return std::nullopt;
    }

    return getHostMemoryInfoFromHostMemoryTypeIndex(hostMemoryTypeIndex);
}

void EmulatedPhysicalDeviceMemoryProperties::transformToGuestMemoryRequirements(
    VkMemoryRequirements* memoryRequirements) const {
    uint32_t guestMemoryTypeBits = 0;

    const uint32_t hostMemoryTypeBits = memoryRequirements->memoryTypeBits;
    for (uint32_t hostMemoryTypeIndex = 0;
         hostMemoryTypeIndex < mHostMemoryProperties.memoryTypeCount; hostMemoryTypeIndex++) {
        if (!(hostMemoryTypeBits & (1u << hostMemoryTypeIndex))) {
            continue;
        }

        uint32_t guestMemoryTypeIndex = mHostToGuestMemoryTypeIndexMap[hostMemoryTypeIndex];
        if (guestMemoryTypeIndex == kInvalidMemoryTypeIndex) {
            continue;
        }

        guestMemoryTypeBits |= (1u << guestMemoryTypeIndex);
    }

    memoryRequirements->memoryTypeBits = guestMemoryTypeBits;
}

}  // namespace vk
}  // namespace gfxstream