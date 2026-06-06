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

#include <vector>

#include "vk_emulated_physical_device_memory.h"
#include "gfxstream/host/features.h"

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
    hostMemoryProperties.memoryTypes[1] = {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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
    hostMemoryProperties.memoryTypes[4] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                             VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                                                             VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
                                           .heapIndex = 1};
    hostMemoryProperties.memoryTypes[5] = {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                             VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                                                             VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
                                           .heapIndex = 0};
    hostMemoryProperties.memoryTypes[6] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                             VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                                                             VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
                                           .heapIndex = 1};
    hostMemoryProperties.memoryTypes[7] = {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
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
    constexpr VkDeviceSize kMaxSafeHeapSize =
        EmulatedPhysicalDeviceMemoryProperties::kDefaultMaxSafeHeapSize;

    VkPhysicalDeviceMemoryProperties hostMemoryProperties = {};
    hostMemoryProperties.memoryHeapCount = 2;
    hostMemoryProperties.memoryHeaps[0] = {.size = 16ULL * 1024 * 1024 * 1024,
                                           .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
    hostMemoryProperties.memoryHeaps[1] = {.size = 1ULL * 1024 * 1024 * 1024, .flags = 0};
    hostMemoryProperties.memoryTypeCount = 1;
    hostMemoryProperties.memoryTypes[0] = {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           .heapIndex = 0};

    gfxstream::host::FeatureSet features;
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