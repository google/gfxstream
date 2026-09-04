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

#include "vk_emulated_physical_device_memory.h"

#include <algorithm>
#include <limits>

#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

#if defined(__APPLE__)
// The first device local type, if every type is host visible.
std::optional<uint32_t> FindDeviceLocalMemoryTypeIfAllAreHostVisible(
    const VkPhysicalDeviceMemoryProperties& memoryProperties) {
    std::optional<uint32_t> deviceLocalIndex;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
        if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            return std::nullopt;
        }
        if (!deviceLocalIndex && (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            deviceLocalIndex = i;
        }
    }
    return deviceLocalIndex;
}
#endif

}  // namespace

EmulatedPhysicalDeviceMemoryProperties::EmulatedPhysicalDeviceMemoryProperties(
    const VkPhysicalDeviceMemoryProperties& hostMemoryProperties,
    const uint32_t hostColorBufferMemoryTypeIndex, const gfxstream::host::FeatureSet& features) {
    // Start with the original host memory properties:
    mHostMemoryProperties = hostMemoryProperties;
    mGuestMemoryProperties = hostMemoryProperties;
    mGuestColorBufferMemoryTypeIndex = hostColorBufferMemoryTypeIndex;

    // Limit max safe memory heap size if the VulkanMaxSafeHeapSize feature is set to a non-zero
    // value.
    const uint64_t maxSafeHeapSizeLimit = features.VulkanMaxSafeHeapSize.getValue().value_or(0);
    if (maxSafeHeapSizeLimit > 0) {
        for (uint32_t i = 0; i < mHostMemoryProperties.memoryHeapCount; i++) {
            if (mGuestMemoryProperties.memoryHeaps[i].size > maxSafeHeapSizeLimit) {
                mGuestMemoryProperties.memoryHeaps[i].size = maxSafeHeapSizeLimit;
            }
        }
    }

    // Strip VK_AMD_device_coherent_memory flags that gfxstream does not translate.
    for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
        mGuestMemoryProperties.memoryTypes[i].propertyFlags &=
            ~(VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
              VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD);
    }

    // If enabled, hide non device memory types from the guest.
    // (useful to work around a bug where KVM can't map TTM memory).
    if (features.VulkanAllocateDeviceMemoryOnly.enabled()) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            auto guestMemoryProperties = mGuestMemoryProperties.memoryTypes[i].propertyFlags;
            if (!(guestMemoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags = 0;
            }
        }
    }

    // Coherent memory in the guest requires one of these features:
    if (!features.GlDirectMem.enabled() && !features.VirtioGpuNext.enabled()) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            mGuestMemoryProperties.memoryTypes[i].propertyFlags =
                mGuestMemoryProperties.memoryTypes[i].propertyFlags &
                ~(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
    }

    // Let cached memory pretend as coherent on the guest side.
    if (features.VulkanDisableCoherentMemoryAndEmulate.enabled()) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            if (mGuestMemoryProperties.memoryTypes[i].propertyFlags &
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags |=
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            } else {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags &=
                    ~(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            }
        }
    }

    if (features.VulkanEnsureCachedCoherentMemoryAvailable.enabled()) {
        /* Some app layers (i.e. Angle) require *some* coherent-cached memory to be
         *  available. To ensure compatiblity these guest layers, when coherent-cached
         *  memory type is unavailable, append the cached bit to the first coherent
         *  memory type available. Note that in this scenario, there is no potential
         *  functional downside to marking one of the host-coherent as cached, aside
         *  from the guest layer believing there will be some performance benefit to
         *  using this particular memory.
         */
        bool hasCoherentCached = false;
        uint32_t firstCoherent = VK_MAX_MEMORY_TYPES;
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            const VkMemoryPropertyFlags flags = mGuestMemoryProperties.memoryTypes[i].propertyFlags;
            const bool coherent = flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            const bool cached = flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            if (coherent) {
                if (firstCoherent == VK_MAX_MEMORY_TYPES) {
                    firstCoherent = i;
                }
                if (cached) {
                    hasCoherentCached = true;
                }
            }
        }

        if (!hasCoherentCached) {
            if (firstCoherent == VK_MAX_MEMORY_TYPES) {
                GFXSTREAM_FATAL(
                    "Unexpected memoryTypes error -- no available host-coherent memory.");
            }
            mGuestMemoryProperties.memoryTypes[firstCoherent].propertyFlags |=
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }
    }

    for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
        mGuestMemoryTypes.push_back(EmulatedGuestMemoryType{
            .hostMemoryTypeIndex = i,
            .memoryType = mGuestMemoryProperties.memoryTypes[i],
        });
    }

#if defined(__APPLE__)
    const std::optional<uint32_t> hostDeviceLocalIndex =
        features.SystemBlob.enabled()
            ? FindDeviceLocalMemoryTypeIfAllAreHostVisible(hostMemoryProperties)
            : std::nullopt;
    if (hostDeviceLocalIndex && mGuestMemoryTypes.size() < VK_MAX_MEMORY_TYPES) {
        const VkMemoryType memoryType = {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            .heapIndex = hostMemoryProperties.memoryTypes[*hostDeviceLocalIndex].heapIndex,
        };
        const uint32_t index = findIndexForNewMemoryType(memoryType.propertyFlags);
        mGuestMemoryTypes.insert(mGuestMemoryTypes.begin() + index,
                                 EmulatedGuestMemoryType{
                                     .hostMemoryTypeIndex = *hostDeviceLocalIndex,
                                     .isReservedForAppleSystemBlobAllocations = true,
                                     .memoryType = memoryType,
                                 });
        if (index <= mGuestColorBufferMemoryTypeIndex) {
            mGuestColorBufferMemoryTypeIndex++;
        }
    }
#endif

    mGuestMemoryProperties.memoryTypeCount = static_cast<uint32_t>(mGuestMemoryTypes.size());
    for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
        mGuestMemoryProperties.memoryTypes[i] = mGuestMemoryTypes[i].memoryType;
    }

    // If enabled, reserve an additional memory type for AHB backed buffers and images
    // so that the host can control its memory properties. This ensures that the guest
    // only sees `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` and will not try to map the
    // memory.
    if (features.VulkanUseDedicatedAhbMemoryType.enabled()) {
        if (mGuestMemoryProperties.memoryTypeCount == VK_MAX_MEMORY_TYPES) {
            GFXSTREAM_FATAL(
                "Unable to create emulated AHB memory type because VK_MAX_MEMORY_TYPES "
                "already in use.");
        }

        uint32_t ahbMemoryTypeIndex = mGuestMemoryProperties.memoryTypeCount;
        ++mGuestMemoryProperties.memoryTypeCount;

        VkMemoryType& ahbMemoryType = mGuestMemoryProperties.memoryTypes[ahbMemoryTypeIndex];
        ahbMemoryType.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        ahbMemoryType.heapIndex =
            mHostMemoryProperties.memoryTypes[hostColorBufferMemoryTypeIndex].heapIndex;

        mGuestMemoryTypes.push_back(EmulatedGuestMemoryType{
            .hostMemoryTypeIndex = hostColorBufferMemoryTypeIndex,
            .isReservedForAhbAllocations = true,
            .memoryType = ahbMemoryType,
        });

        mGuestColorBufferMemoryTypeIndex = ahbMemoryTypeIndex;
    }
}

uint32_t EmulatedPhysicalDeviceMemoryProperties::findIndexForNewMemoryType(
    VkMemoryPropertyFlags propertyFlags) const {
    // A memory type whose flags are a strict subset of another's must come before it, see
    // https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryProperties.html
    for (uint32_t i = 0; i < mGuestMemoryTypes.size(); i++) {
        const VkMemoryPropertyFlags existingFlags = mGuestMemoryTypes[i].memoryType.propertyFlags;
        if (existingFlags != propertyFlags && (existingFlags & propertyFlags) == propertyFlags) {
            return i;
        }
    }
    return static_cast<uint32_t>(mGuestMemoryTypes.size());
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

    uint32_t hostMemoryTypeIndex = mGuestMemoryTypes[guestMemoryTypeIndex].hostMemoryTypeIndex;

    // The host type with its host visibility withheld, so no host visible emulation.
    if (mGuestMemoryTypes[guestMemoryTypeIndex].isReservedForAppleSystemBlobAllocations) {
        return HostMemoryInfo{
            .index = hostMemoryTypeIndex,
            .memoryType = mGuestMemoryProperties.memoryTypes[guestMemoryTypeIndex],
        };
    }

    return getHostMemoryInfoFromHostMemoryTypeIndex(hostMemoryTypeIndex);
}

void EmulatedPhysicalDeviceMemoryProperties::transformToGuestMemoryRequirements(
    VkMemoryRequirements* memoryRequirements) const {
    uint32_t guestMemoryTypeBits = 0;

    const uint32_t hostMemoryTypeBits = memoryRequirements->memoryTypeBits;
    for (uint32_t guestMemoryTypeIndex = 0; guestMemoryTypeIndex < mGuestMemoryTypes.size();
         guestMemoryTypeIndex++) {
        uint32_t hostMemoryTypeIndex = mGuestMemoryTypes[guestMemoryTypeIndex].hostMemoryTypeIndex;
        if (!(hostMemoryTypeBits & (1u << hostMemoryTypeIndex))) {
            continue;
        }

        if (mGuestMemoryTypes[guestMemoryTypeIndex].isReservedForAhbAllocations) {
            continue;
        }

        guestMemoryTypeBits |= (1u << guestMemoryTypeIndex);
    }

    memoryRequirements->memoryTypeBits = guestMemoryTypeBits;
}

void EmulatedPhysicalDeviceMemoryProperties::transformToGuestImageMemoryRequirements(
    VkImageTiling tiling, VkMemoryRequirements* memoryRequirements) const {
    transformToGuestMemoryRequirements(memoryRequirements);
    if (tiling == VK_IMAGE_TILING_LINEAR) {
        return;
    }

    uint32_t tiledMemoryTypeBits = 0;
    for (uint32_t i = 0; i < mGuestMemoryTypes.size(); i++) {
        if (mGuestMemoryTypes[i].isReservedForAppleSystemBlobAllocations) {
            tiledMemoryTypeBits |= (1u << i);
        }
    }

    // Leaving nothing at all would be worse than leaving what the host allows.
    if (memoryRequirements->memoryTypeBits & tiledMemoryTypeBits) {
        memoryRequirements->memoryTypeBits &= tiledMemoryTypeBits;
    }
}

void EmulatedPhysicalDeviceMemoryProperties::clampMemoryBudgetToGuestHeapSizes(
    VkPhysicalDeviceMemoryBudgetPropertiesEXT* budgetProps) const {
    if (budgetProps == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < mGuestMemoryProperties.memoryHeapCount; i++) {
        const VkDeviceSize heapSize = mGuestMemoryProperties.memoryHeaps[i].size;
        if (budgetProps->heapBudget[i] > heapSize) {
            budgetProps->heapBudget[i] = heapSize;
        }
        if (budgetProps->heapUsage[i] > heapSize) {
            budgetProps->heapUsage[i] = heapSize;
        }
    }
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream