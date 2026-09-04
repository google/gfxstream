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

    // With system blobs, host visible memory is shared memory imported as a host pointer, and
    // a tiled image cannot be bound to that. A device whose every memory type is host visible
    // leaves images nowhere else to go, so the guest gets a device local only type that
    // allocates from the same host type without the sharing. It goes first: a strict subset
    // of flags has to precede its superset, which is also what makes it the type an image
    // is given.
    if (features.SystemBlob.enabled() && mHostMemoryProperties.memoryTypeCount > 0 &&
        mHostMemoryProperties.memoryTypeCount < VK_MAX_MEMORY_TYPES) {
        bool allHostVisible = true;
        uint32_t hostDeviceLocalIndex = 0;
        for (uint32_t i = 0; i < mHostMemoryProperties.memoryTypeCount; i++) {
            const VkMemoryPropertyFlags flags = mHostMemoryProperties.memoryTypes[i].propertyFlags;
            if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                allHostVisible = false;
            }
            if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && hostDeviceLocalIndex == 0) {
                hostDeviceLocalIndex = i;
            }
        }
        if (allHostVisible) {
            for (uint32_t i = mGuestMemoryProperties.memoryTypeCount; i > 0; i--) {
                mGuestMemoryProperties.memoryTypes[i] = mGuestMemoryProperties.memoryTypes[i - 1];
                mGuestToHostMemoryTypeIndexMap[i] = i - 1;
                mHostToGuestMemoryTypeIndexMap[i - 1] = i;
            }
            mGuestMemoryProperties.memoryTypes[0] = VkMemoryType{
                .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                .heapIndex = mHostMemoryProperties.memoryTypes[hostDeviceLocalIndex].heapIndex,
            };
            mGuestMemoryProperties.memoryTypeCount++;
            mGuestToHostMemoryTypeIndexMap[0] = hostDeviceLocalIndex;
            mGuestDeviceOnlyMemoryTypeIndex = 0;
            mGuestColorBufferMemoryTypeIndex = hostColorBufferMemoryTypeIndex + 1;
        }
    }

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

    // The device only type is the host type with its host visibility withheld, so that an
    // allocation from it is not given host visible emulation.
    if (mGuestDeviceOnlyMemoryTypeIndex == guestMemoryTypeIndex) {
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

    if (mGuestDeviceOnlyMemoryTypeIndex) {
        const uint32_t hostMemoryTypeIndex =
            mGuestToHostMemoryTypeIndexMap[*mGuestDeviceOnlyMemoryTypeIndex];
        if (hostMemoryTypeBits & (1u << hostMemoryTypeIndex)) {
            guestMemoryTypeBits |= (1u << *mGuestDeviceOnlyMemoryTypeIndex);
        }
    }

    memoryRequirements->memoryTypeBits = guestMemoryTypeBits;
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