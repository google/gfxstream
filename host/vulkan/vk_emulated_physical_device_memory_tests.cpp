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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ios>
#include <vector>

#include "gfxstream/host/features.h"
#include "vk_emulated_physical_device_memory.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::Matcher;
using ::testing::Optional;

MATCHER_P(EqsVkMemoryHeap, expected, "") {
    return ExplainMatchResult(AllOf(Field("size", &VkMemoryHeap::size, Eq(expected.size)),
                                    Field("flags", &VkMemoryHeap::flags, Eq(expected.flags))),
                              arg, result_listener);
}

MATCHER_P(EqsVkMemoryType, expected, "") {
    return ExplainMatchResult(
        AllOf(Field("propertyFlags", &VkMemoryType::propertyFlags, Eq(expected.propertyFlags)),
              Field("heapIndex", &VkMemoryType::heapIndex, Eq(expected.heapIndex))),
        arg, result_listener);
}

MATCHER_P(EqsHostMemoryInfo, expected, "") {
    return ExplainMatchResult(
        AllOf(
            Field("index", &EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo::index,
                  Eq(expected.index)),
            Field("memoryType", &EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo::memoryType,
                  EqsVkMemoryType(expected.memoryType))),
        arg, result_listener);
}

MATCHER_P(EqsVkPhysicalDeviceMemoryProperties, expected, "") {
    std::vector<Matcher<VkMemoryHeap>> memoryHeapsMatchers;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++) {
        memoryHeapsMatchers.push_back(EqsVkMemoryHeap(expected.memoryHeaps[i]));
    }

    std::vector<Matcher<VkMemoryType>> memoryTypesMatchers;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
        memoryTypesMatchers.push_back(EqsVkMemoryType(expected.memoryTypes[i]));
    }

    return ExplainMatchResult(
        AllOf(Field("memoryTypeCount", &VkPhysicalDeviceMemoryProperties::memoryTypeCount,
                    Eq(expected.memoryTypeCount)),
              Field("memoryTypes", &VkPhysicalDeviceMemoryProperties::memoryTypes,
                    ElementsAreArray(memoryTypesMatchers)),
              Field("memoryHeapCount", &VkPhysicalDeviceMemoryProperties::memoryHeapCount,
                    Eq(expected.memoryHeapCount)),
              Field("memoryHeaps", &VkPhysicalDeviceMemoryProperties::memoryHeaps,
                    ElementsAreArray(memoryHeapsMatchers))),
        arg, result_listener);
}

TEST(VkGuestMemoryUtilsTest, Passthrough) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 2,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 1, features);

    // Passthrough when no features enabled:
    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(hostMemoryProperties));
}

#if defined(__APPLE__)
TEST(VkGuestMemoryUtilsTest, SystemBlobDeviceLocalTypeWhenEverythingIsHostVisible) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 2,
        .memoryTypes =
            {
                {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
                {.propertyFlags =
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
            },
        .memoryHeapCount = 1,
        .memoryHeaps = {{.size = 0x1000000, .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT}},
    };

    gfxstream::host::FeatureSet features;
    features.SystemBlob.setEnabled(true);
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 1, features);

    // Device local alone is a strict subset of the second host type, so it goes before it.
    VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = hostMemoryProperties;
    expectedGuestMemoryProperties.memoryTypeCount = 3;
    expectedGuestMemoryProperties.memoryTypes[1] = {.propertyFlags =
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
    expectedGuestMemoryProperties.memoryTypes[2] = hostMemoryProperties.memoryTypes[1];
    EXPECT_THAT(helper.getGuestMemoryProperties(),
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));
    EXPECT_THAT(helper.getHostMemoryInfoFromGuestMemoryTypeIndex(1),
                Optional(EqsHostMemoryInfo(EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo{
                    .index = 1,
                    .memoryType = expectedGuestMemoryProperties.memoryTypes[1],
                })));
    EXPECT_EQ(helper.getGuestColorBufferMemoryTypeIndex(), 2u);

    VkMemoryRequirements requirements = {.memoryTypeBits = 0b11};
    helper.transformToGuestImageMemoryRequirements(VK_IMAGE_TILING_LINEAR, &requirements);
    EXPECT_EQ(requirements.memoryTypeBits, 0b111u);

    requirements = {.memoryTypeBits = 0b11};
    helper.transformToGuestImageMemoryRequirements(VK_IMAGE_TILING_OPTIMAL, &requirements);
    EXPECT_EQ(requirements.memoryTypeBits, 0b010u);

    // Nothing reserved is allowed, so what the host allows stays.
    requirements = {.memoryTypeBits = 0b01};
    helper.transformToGuestImageMemoryRequirements(VK_IMAGE_TILING_OPTIMAL, &requirements);
    EXPECT_EQ(requirements.memoryTypeBits, 0b001u);
}
#endif

TEST(VkGuestMemoryUtilsTest, ReserveAHardwareBuffer) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 2,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    features.VulkanUseDedicatedAhbMemoryType.setEnabled(true);

    const uint32_t kHostColorBufferIndex = 1;
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, kHostColorBufferIndex,
                                                  features);

    const VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = {
        .memoryTypeCount = 3,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
                // Note: extra memory type here:
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));

    const auto mappedHostMemoryInfo = helper.getHostMemoryInfoFromGuestMemoryTypeIndex(2);
    EXPECT_THAT(mappedHostMemoryInfo,
                Optional(EqsHostMemoryInfo(EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo{
                    .index = kHostColorBufferIndex,
                    .memoryType = hostMemoryProperties.memoryTypes[kHostColorBufferIndex],
                })));
}

TEST(VkGuestMemoryUtilsTest, VulkanAllocateDeviceMemoryOnly) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 3,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    features.VulkanAllocateDeviceMemoryOnly.setEnabled(true);

    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 1, features);

    const VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = {
        .memoryTypeCount = 3,
        .memoryTypes =
            {
                {
                    .propertyFlags = 0,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));
}

TEST(VkGuestMemoryUtilsTest, VulkanMaxSafeHeapSizeUnset) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 1,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 0,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x100000000ULL,  // 4 GB
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
                {
                    .size = 0x200000000ULL,  // 8 GB
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    // VulkanMaxSafeHeapSize is left unset (default: zero, no limit)

    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

    const VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = {
        .memoryTypeCount = 1,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 0,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x100000000ULL,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
                {
                    .size = 0x200000000ULL,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));
}

TEST(VkGuestMemoryUtilsTest, VulkanMaxSafeHeapSizeSet) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 1,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 0,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x100000000ULL,  // 4 GB
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
                {
                    .size = 0x200000000ULL,  // 8 GB
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    // Set limit to 3 GB (0xC0000000 bytes)
    features.VulkanMaxSafeHeapSize.parseValue("0xC0000000");

    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

    const VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = {
        .memoryTypeCount = 1,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 0,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0xC0000000ULL,  // Limited to 3 GB
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
                {
                    .size = 0xC0000000ULL,  // Limited to 3 GB
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));
}

TEST(VkGuestMemoryUtilsTest, VulkanDisableCoherentMemoryAndEmulate) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 4,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags =
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags =
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    features.VulkanDisableCoherentMemoryAndEmulate.setEnabled(true);

    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 1, features);

    const VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = {
        .memoryTypeCount = 4,
        .memoryTypes =
            {
                {
                    .propertyFlags =
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = 0,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags =
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 0,
                },
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .heapIndex = 1,
                },
            },
        .memoryHeapCount = 2,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
                {
                    .size = 0x200000,
                    .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                },
            },
    };

    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));
}

TEST(VkGuestMemoryUtilsTest, VulkanEnsureCachedCoherentMemoryAvailable) {
    const VkPhysicalDeviceMemoryProperties hostMemoryProperties = {
        .memoryTypeCount = 1,
        .memoryTypes =
            {
                {
                    .propertyFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 0,
                },
            },
        .memoryHeapCount = 1,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
            },
    };

    gfxstream::host::FeatureSet features;
    features.VirtioGpuNext.setEnabled(true);
    features.VulkanEnsureCachedCoherentMemoryAvailable.setEnabled(true);

    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 1, features);

    const VkPhysicalDeviceMemoryProperties expectedGuestMemoryProperties = {
        .memoryTypeCount = 1,
        .memoryTypes =
            {
                {
                    .propertyFlags =
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .heapIndex = 0,
                },
            },
        .memoryHeapCount = 1,
        .memoryHeaps =
            {
                {
                    .size = 0x1000000,
                    .flags = 0,
                },
            },
    };

    const auto actualGuestMemoryProperties = helper.getGuestMemoryProperties();
    EXPECT_THAT(actualGuestMemoryProperties,
                EqsVkPhysicalDeviceMemoryProperties(expectedGuestMemoryProperties));
}

TEST(VkGuestMemoryUtilsTest, VulkanAllocateUncachedHostVisibleAsCached) {
    constexpr VkMemoryPropertyFlags kDeviceLocal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    constexpr VkMemoryPropertyFlags kVisibleCoherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    constexpr VkMemoryPropertyFlags kVisibleCoherentCached =
        kVisibleCoherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    // The memory types of an NVIDIA RTX 4070 SUPER on Windows: heap 0 is VRAM and heap 1 is
    // system memory. Type 2 is write-combined system memory and type 4 is ReBAR.
    const std::vector<VkMemoryType> kNvidiaTypes = {
        {.propertyFlags = 0, .heapIndex = 1},
        {.propertyFlags = kDeviceLocal, .heapIndex = 0},
        {.propertyFlags = kVisibleCoherent, .heapIndex = 1},
        {.propertyFlags = kVisibleCoherentCached, .heapIndex = 1},
        {.propertyFlags = kDeviceLocal | kVisibleCoherent, .heapIndex = 0},
    };
    // The only cached coherent type is on the other heap.
    const std::vector<VkMemoryType> kCachedTypeOnOtherHeap = {
        {.propertyFlags = kVisibleCoherent, .heapIndex = 1},
        {.propertyFlags = kVisibleCoherentCached, .heapIndex = 0},
    };
    // The only cached coherent type is device local.
    const std::vector<VkMemoryType> kCachedTypeIsDeviceLocal = {
        {.propertyFlags = kVisibleCoherent, .heapIndex = 1},
        {.propertyFlags = kDeviceLocal | kVisibleCoherentCached, .heapIndex = 1},
    };
    // The only cached coherent type is AMD device uncached memory.
    const std::vector<VkMemoryType> kCachedTypeIsAmdDeviceUncached = {
        {.propertyFlags = kVisibleCoherent, .heapIndex = 1},
        {.propertyFlags = kVisibleCoherentCached | VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
         .heapIndex = 1},
    };
    // The only cached type is not coherent.
    const std::vector<VkMemoryType> kCachedTypeIsNotCoherent = {
        {.propertyFlags = kVisibleCoherent, .heapIndex = 1},
        {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
         .heapIndex = 1},
    };

    const auto makeHostMemoryProperties = [](const std::vector<VkMemoryType>& memoryTypes) {
        VkPhysicalDeviceMemoryProperties hostMemoryProperties = {};
        hostMemoryProperties.memoryTypeCount = static_cast<uint32_t>(memoryTypes.size());
        for (uint32_t i = 0; i < hostMemoryProperties.memoryTypeCount; i++) {
            hostMemoryProperties.memoryTypes[i] = memoryTypes[i];
        }
        hostMemoryProperties.memoryHeapCount = 2;
        hostMemoryProperties.memoryHeaps[0] = {.size = 0x2edd00000,
                                               .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
        hostMemoryProperties.memoryHeaps[1] = {.size = 0x7e3a7a000, .flags = 0};
        return hostMemoryProperties;
    };

    using Features = gfxstream::host::FeatureSet;
    using Feature = gfxstream::host::BoolFeatureInfo Features::*;
    const Feature kAsCached = &Features::VulkanAllocateUncachedHostVisibleAsCached;

    struct TestCase {
        const char* name;
        std::vector<VkMemoryType> hostMemoryTypes;
        std::vector<Feature> enabledFeatures;
        // No memory types are added in these cases, so guest memory type i has the flags of host
        // memory type i.
        uint32_t guestMemoryTypeIndex;
        uint32_t expectedHostMemoryTypeIndex;
    };
    const std::vector<TestCase> testCases = {
        {.name = "WriteCombinedUsesCachedTypeOnSameHeap",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 2,
         .expectedHostMemoryTypeIndex = 3},
        {.name = "FeatureDisabled",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {},
         .guestMemoryTypeIndex = 2,
         .expectedHostMemoryTypeIndex = 2},
        {.name = "AlreadyCached",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 3,
         .expectedHostMemoryTypeIndex = 3},
        {.name = "DeviceLocal",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 1,
         .expectedHostMemoryTypeIndex = 1},
        {.name = "DeviceLocalHostVisible",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 4,
         .expectedHostMemoryTypeIndex = 4},
        {.name = "NotHostVisible",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 0,
         .expectedHostMemoryTypeIndex = 0},
        {.name = "CachedTypeOnOtherHeap",
         .hostMemoryTypes = kCachedTypeOnOtherHeap,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 0,
         .expectedHostMemoryTypeIndex = 0},
        {.name = "CachedTypeIsDeviceLocal",
         .hostMemoryTypes = kCachedTypeIsDeviceLocal,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 0,
         .expectedHostMemoryTypeIndex = 0},
        {.name = "CachedTypeIsAmdDeviceUncached",
         .hostMemoryTypes = kCachedTypeIsAmdDeviceUncached,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 0,
         .expectedHostMemoryTypeIndex = 0},
        {.name = "CachedTypeIsNotCoherent",
         .hostMemoryTypes = kCachedTypeIsNotCoherent,
         .enabledFeatures = {kAsCached},
         .guestMemoryTypeIndex = 0,
         .expectedHostMemoryTypeIndex = 0},
        // Host visible memory that is exported or imported keeps its type.
        {.name = "ExternalBlob",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached, &Features::ExternalBlob},
         .guestMemoryTypeIndex = 2,
         .expectedHostMemoryTypeIndex = 2},
        {.name = "SystemBlob",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached, &Features::SystemBlob},
         .guestMemoryTypeIndex = 2,
         .expectedHostMemoryTypeIndex = 2},
        {.name = "VulkanAllocateHostVisibleAsUdmabuf",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached, &Features::VulkanAllocateHostVisibleAsUdmabuf},
         .guestMemoryTypeIndex = 2,
         .expectedHostMemoryTypeIndex = 2},
        {.name = "VulkanAllocateHostMemory",
         .hostMemoryTypes = kNvidiaTypes,
         .enabledFeatures = {kAsCached, &Features::VulkanAllocateHostMemory},
         .guestMemoryTypeIndex = 2,
         .expectedHostMemoryTypeIndex = 2},
    };

    for (const TestCase& testCase : testCases) {
        SCOPED_TRACE(testCase.name);

        const VkPhysicalDeviceMemoryProperties hostMemoryProperties =
            makeHostMemoryProperties(testCase.hostMemoryTypes);

        gfxstream::host::FeatureSet features;
        for (const Feature feature : testCase.enabledFeatures) {
            (features.*feature).setEnabled(true);
        }
        EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

        // The feature never changes what the guest sees.
        gfxstream::host::FeatureSet featuresWithoutAsCached = features;
        featuresWithoutAsCached.VulkanAllocateUncachedHostVisibleAsCached.setEnabled(false);
        EmulatedPhysicalDeviceMemoryProperties withoutAsCached(hostMemoryProperties, 0,
                                                               featuresWithoutAsCached);
        EXPECT_THAT(
            helper.getGuestMemoryProperties(),
            EqsVkPhysicalDeviceMemoryProperties(withoutAsCached.getGuestMemoryProperties()));

        EXPECT_THAT(
            helper.getHostMemoryInfoFromGuestMemoryTypeIndex(testCase.guestMemoryTypeIndex),
            Optional(EqsHostMemoryInfo(EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo{
                .index = testCase.expectedHostMemoryTypeIndex,
                .memoryType = testCase.hostMemoryTypes[testCase.expectedHostMemoryTypeIndex],
            })));

        // The guest memory type is offered for a resource exactly when the resource allows the
        // host memory type that it is allocated from.
        const uint32_t allHostMemoryTypeBits = (1u << hostMemoryProperties.memoryTypeCount) - 1;
        const uint32_t expectedHostMemoryTypeBit = 1u << testCase.expectedHostMemoryTypeIndex;
        const uint32_t guestMemoryTypeBit = 1u << testCase.guestMemoryTypeIndex;
        for (const uint32_t hostMemoryTypeBits :
             {allHostMemoryTypeBits, expectedHostMemoryTypeBit,
              allHostMemoryTypeBits & ~expectedHostMemoryTypeBit}) {
            VkMemoryRequirements requirements = {.memoryTypeBits = hostMemoryTypeBits};
            helper.transformToGuestMemoryRequirements(&requirements);
            EXPECT_EQ(requirements.memoryTypeBits & guestMemoryTypeBit,
                      (hostMemoryTypeBits & expectedHostMemoryTypeBit) ? guestMemoryTypeBit : 0u)
                << "host memory type bits 0x" << std::hex << hostMemoryTypeBits;
        }
    }

    // With the NVIDIA memory types, guest memory types 2 and 3 are both allocated from host memory
    // type 3.
    gfxstream::host::FeatureSet features;
    features.VulkanAllocateUncachedHostVisibleAsCached.setEnabled(true);
    EmulatedPhysicalDeviceMemoryProperties helper(makeHostMemoryProperties(kNvidiaTypes), 0,
                                                  features);
    const struct {
        uint32_t hostMemoryTypeBits;
        uint32_t expectedGuestMemoryTypeBits;
    } kRequirements[] = {
        {0b11111, 0b11111},
        {0b01000, 0b01100},
        // Allows the write-combined type but not the cached one.
        {0b00110, 0b00010},
    };
    for (const auto& [hostMemoryTypeBits, expectedGuestMemoryTypeBits] : kRequirements) {
        VkMemoryRequirements requirements = {.memoryTypeBits = hostMemoryTypeBits};
        helper.transformToGuestMemoryRequirements(&requirements);
        EXPECT_EQ(requirements.memoryTypeBits, expectedGuestMemoryTypeBits)
            << "host memory type bits 0x" << std::hex << hostMemoryTypeBits;
    }
}

TEST(VkGuestMemoryUtilsTest, VulkanAMDCoherentFlagsNotLeakedToGuest) {
    VkPhysicalDeviceMemoryProperties hostMemoryProperties = {};
    hostMemoryProperties.memoryHeapCount = 2;
    hostMemoryProperties.memoryHeaps[0] = {.size = 0x400000000, .flags = 0};
    hostMemoryProperties.memoryHeaps[1] = {.size = 0x40000000,
                                           .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};

    hostMemoryProperties.memoryTypeCount = 8;
    // Standard types (types 0-3):
    hostMemoryProperties.memoryTypes[0] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           .heapIndex = 1};
    hostMemoryProperties.memoryTypes[1] = {
        .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .heapIndex = 0};
    hostMemoryProperties.memoryTypes[2] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           .heapIndex = 1};
    hostMemoryProperties.memoryTypes[3] = {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                           .heapIndex = 0};
    // AMD-specific types (types 4-7) with DEVICE_COHERENT and DEVICE_UNCACHED:
    hostMemoryProperties.memoryTypes[4] = {
        .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                         VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
        .heapIndex = 1};
    hostMemoryProperties.memoryTypes[5] = {
        .propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
        .heapIndex = 0};
    hostMemoryProperties.memoryTypes[6] = {
        .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                         VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                         VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
        .heapIndex = 1};
    hostMemoryProperties.memoryTypes[7] = {
        .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                         VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                         VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
        .heapIndex = 0};

    gfxstream::host::FeatureSet features;
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

    const auto guestProps = helper.getGuestMemoryProperties();

    for (uint32_t i = 0; i < guestProps.memoryTypeCount; i++) {
        EXPECT_EQ(
            guestProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD,
            0u)
            << "Guest memory type " << i << " has DEVICE_COHERENT_BIT_AMD (0x40) leaked from host";
        EXPECT_EQ(
            guestProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
            0u)
            << "Guest memory type " << i << " has DEVICE_UNCACHED_BIT_AMD (0x80) leaked from host";
    }

    VkMemoryRequirements reqs = {
        .size = 4096,
        .alignment = 256,
        .memoryTypeBits = 0xFF,
    };

    helper.transformToGuestMemoryRequirements(&reqs);

    for (uint32_t i = 0; i < guestProps.memoryTypeCount; i++) {
        if (!(reqs.memoryTypeBits & (1u << i))) continue;

        VkMemoryPropertyFlags flags = guestProps.memoryTypes[i].propertyFlags;
        EXPECT_EQ(flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD, 0u)
            << "Guest memory type " << i << " (in memoryTypeBits) has "
            << "DEVICE_COHERENT_BIT_AMD, which the guest cannot handle";
        EXPECT_EQ(flags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD, 0u)
            << "Guest memory type " << i << " (in memoryTypeBits) has "
            << "DEVICE_UNCACHED_BIT_AMD, which the guest cannot handle";
    }
}

TEST(VkGuestMemoryUtilsTest, MemoryBudgetClampedToClampedGuestHeapSize) {
    constexpr VkDeviceSize kMaxSafeHeapSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;

    VkPhysicalDeviceMemoryProperties hostMemoryProperties = {};
    hostMemoryProperties.memoryHeapCount = 2;
    hostMemoryProperties.memoryHeaps[0] = {.size = 16ULL * 1024 * 1024 * 1024,
                                           .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
    hostMemoryProperties.memoryHeaps[1] = {.size = 1ULL * 1024 * 1024 * 1024, .flags = 0};
    hostMemoryProperties.memoryTypeCount = 1;
    hostMemoryProperties.memoryTypes[0] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           .heapIndex = 0};

    gfxstream::host::FeatureSet features;
    features.VulkanMaxSafeHeapSize.setValue(kMaxSafeHeapSize);
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

    ASSERT_EQ(helper.getGuestMemoryProperties().memoryHeaps[0].size, kMaxSafeHeapSize);
    ASSERT_EQ(helper.getGuestMemoryProperties().memoryHeaps[1].size, 1ULL * 1024 * 1024 * 1024);

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    budget.heapBudget[0] = 12ULL * 1024 * 1024 * 1024;
    budget.heapUsage[0] = 4ULL * 1024 * 1024 * 1024;
    budget.heapBudget[1] = 768ULL * 1024 * 1024;
    budget.heapUsage[1] = 256ULL * 1024 * 1024;

    helper.clampMemoryBudgetToGuestHeapSizes(&budget);

    EXPECT_EQ(budget.heapBudget[0], kMaxSafeHeapSize);
    EXPECT_EQ(budget.heapUsage[0], kMaxSafeHeapSize);
    EXPECT_EQ(budget.heapBudget[1], 768ULL * 1024 * 1024);
    EXPECT_EQ(budget.heapUsage[1], 256ULL * 1024 * 1024);
}

TEST(VkGuestMemoryUtilsTest, MemoryBudgetWithinHeapSizesUnchanged) {
    VkPhysicalDeviceMemoryProperties hostMemoryProperties = {};
    hostMemoryProperties.memoryHeapCount = 1;
    hostMemoryProperties.memoryHeaps[0] = {.size = 256ULL * 1024 * 1024, .flags = 0};
    hostMemoryProperties.memoryTypeCount = 1;
    hostMemoryProperties.memoryTypes[0] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           .heapIndex = 0};

    gfxstream::host::FeatureSet features;
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    budget.heapBudget[0] = 128ULL * 1024 * 1024;
    budget.heapUsage[0] = 16ULL * 1024 * 1024;
    budget.heapBudget[1] = 0;
    budget.heapUsage[1] = 0;

    helper.clampMemoryBudgetToGuestHeapSizes(&budget);

    EXPECT_EQ(budget.heapBudget[0], 128ULL * 1024 * 1024);
    EXPECT_EQ(budget.heapUsage[0], 16ULL * 1024 * 1024);
}

TEST(VkGuestMemoryUtilsTest, MemoryBudgetNullIsNoOp) {
    VkPhysicalDeviceMemoryProperties hostMemoryProperties = {};
    hostMemoryProperties.memoryHeapCount = 1;
    hostMemoryProperties.memoryHeaps[0] = {.size = 256ULL * 1024 * 1024, .flags = 0};
    hostMemoryProperties.memoryTypeCount = 1;
    hostMemoryProperties.memoryTypes[0] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           .heapIndex = 0};

    gfxstream::host::FeatureSet features;
    EmulatedPhysicalDeviceMemoryProperties helper(hostMemoryProperties, 0, features);

    helper.clampMemoryBudgetToGuestHeapSizes(nullptr);
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream