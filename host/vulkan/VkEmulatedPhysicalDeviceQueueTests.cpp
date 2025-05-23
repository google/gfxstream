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

#include "VkEmulatedPhysicalDeviceQueue.h"
#include "gfxstream/host/Features.h"
#include "vulkan/vulkan_core.h"

namespace gfxstream {
namespace vk {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::Matcher;
using ::testing::Optional;

MATCHER_P(EqsVkExtent3D, expected, "") {
    return ExplainMatchResult(AllOf(Field("width", &VkExtent3D::width, Eq(expected.width)),
                                    Field("height", &VkExtent3D::height, Eq(expected.height)),
                                    Field("depth", &VkExtent3D::depth, Eq(expected.depth))),
                              arg, result_listener);
}

MATCHER_P(EqsVkQueueFamilyProperties, expected, "") {
    return ExplainMatchResult(
        AllOf(Field("queueFlags", &VkQueueFamilyProperties::queueFlags, Eq(expected.queueFlags)),
              Field("queueCount", &VkQueueFamilyProperties::queueCount, Eq(expected.queueCount)),
              Field("timestampValidBits", &VkQueueFamilyProperties::timestampValidBits,
                    Eq(expected.timestampValidBits)),
              Field("minImageTransferGranularity",
                    &VkQueueFamilyProperties::minImageTransferGranularity,
                    EqsVkExtent3D(expected.minImageTransferGranularity))),
        arg, result_listener);
}

// Use VulkanVirtualQueue feature to multiplex physical queues.
TEST(VkGuestQueueUtilsTest, Passthrough) {
    const std::vector<VkQueueFamilyProperties> hostQueueFamilyProperties = {
        {.queueFlags = VK_QUEUE_GRAPHICS_BIT,
         .queueCount = 1,
         .timestampValidBits = 16,
         .minImageTransferGranularity = {1, 1, 1}}};

    gfxstream::host::FeatureSet features;
    EmulatedPhysicalDeviceQueueProperties helper(hostQueueFamilyProperties, features);

    // Passthrough when no features enabled:
    const auto actualQueueProperties = helper.getQueueFamilyProperties();
    EXPECT_THAT(actualQueueProperties.size(), 1);
    EXPECT_THAT(actualQueueProperties[0], EqsVkQueueFamilyProperties(hostQueueFamilyProperties[0]));
}

// Use VulkanVirtualQueue feature to multiplex physical queues.
TEST(VkGuestQueueUtilsTest, VulkanVirtualQueue) {
    const std::vector<VkQueueFamilyProperties> hostQueueFamilyProperties = {
        {.queueFlags = VK_QUEUE_GRAPHICS_BIT,
         .queueCount = 1,
         .timestampValidBits = 16,
         .minImageTransferGranularity = {1, 1, 1}}};

    const std::vector<VkQueueFamilyProperties> expectedQueueFamilyProperties = {
        {.queueFlags = VK_QUEUE_GRAPHICS_BIT,
         .queueCount = 2,
         .timestampValidBits = 16,
         .minImageTransferGranularity = {1, 1, 1}}};

    // Enable VulkanVirtualQueue, expect 2 graphics queues
    gfxstream::host::FeatureSet features;
    features.Vulkan.enabled = true;
    features.VulkanVirtualQueue.enabled = true;

    EmulatedPhysicalDeviceQueueProperties helper(hostQueueFamilyProperties, features);

    const auto actualQueueProperties = helper.getQueueFamilyProperties();
    EXPECT_THAT(actualQueueProperties.size(), 1);
    EXPECT_THAT(actualQueueProperties[0], EqsVkQueueFamilyProperties(expectedQueueFamilyProperties[0]));
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream