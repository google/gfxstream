// Copyright 2025 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "gfxstream/host/features.h"

#ifndef VK_VERSION_1_0
// Define here to avoid importing vulkan headers
#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))
#define VK_API_VERSION_VARIANT(version) ((uint32_t)(version) >> 29U)
#define VK_API_VERSION_MAJOR(version) (((uint32_t)(version) >> 22U) & 0x7FU)
#define VK_API_VERSION_MINOR(version) (((uint32_t)(version) >> 12U) & 0x3FFU)
#define VK_API_VERSION_PATCH(version) ((uint32_t)(version) & 0xFFFU)
#endif

namespace gfxstream {
namespace host {
namespace {

class FeaturesTest : public ::testing::Test {
   protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(FeaturesTest, BoolFeatureInfoTest) {
    FeatureMap map;
    BoolFeatureInfo boolFeature("TestBool", "Description", &map);

    EXPECT_EQ(boolFeature.getName(), "TestBool");
    EXPECT_FALSE(boolFeature.enabled());
    EXPECT_EQ(boolFeature.getValueReadable(), "disabled");

    EXPECT_TRUE(boolFeature.parseValue("enabled"));
    EXPECT_TRUE(boolFeature.enabled());
    EXPECT_EQ(boolFeature.getValueReadable(), "enabled");

    EXPECT_TRUE(boolFeature.parseValue("disabled"));
    EXPECT_FALSE(boolFeature.enabled());
    EXPECT_EQ(boolFeature.getValueReadable(), "disabled");

    EXPECT_FALSE(boolFeature.parseValue("invalid_value"));
    EXPECT_FALSE(boolFeature.enabled());
}

TEST_F(FeaturesTest, StringFeatureInfoTest) {
    FeatureMap map;
    StringFeatureInfo stringFeature("TestString", "Description", &map);

    EXPECT_EQ(stringFeature.getName(), "TestString");
    EXPECT_FALSE(stringFeature.getValue().has_value());
    EXPECT_EQ(stringFeature.getValueReadable(), "(Unset)");

    EXPECT_TRUE(stringFeature.parseValue("some_value"));
    ASSERT_TRUE(stringFeature.getValue().has_value());
    EXPECT_EQ(stringFeature.getValue().value(), "some_value");
    EXPECT_EQ(stringFeature.getValueReadable(), "some_value");

    EXPECT_TRUE(stringFeature.parseValue(""));
    ASSERT_TRUE(stringFeature.getValue().has_value());
    EXPECT_EQ(stringFeature.getValue().value(), "");
    EXPECT_EQ(stringFeature.getValueReadable(), "");
}

TEST_F(FeaturesTest, U32FeatureInfoTest) {
    FeatureMap map;
    U32FeatureInfo u32Feature("TestU32", "Description", &map);

    EXPECT_EQ(u32Feature.getName(), "TestU32");
    EXPECT_FALSE(u32Feature.getValue().has_value());
    EXPECT_EQ(u32Feature.getValueReadable(), "(Unset)");

    EXPECT_TRUE(u32Feature.parseValue("123"));
    ASSERT_TRUE(u32Feature.getValue().has_value());
    EXPECT_EQ(u32Feature.getValue().value(), 123u);
    EXPECT_EQ(u32Feature.getValueReadable(), "123 (0x7b)");

    EXPECT_TRUE(u32Feature.parseValue("0x7B"));
    ASSERT_TRUE(u32Feature.getValue().has_value());
    EXPECT_EQ(u32Feature.getValue().value(), 123u);
    EXPECT_EQ(u32Feature.getValueReadable(), "123 (0x7b)");

    EXPECT_TRUE(u32Feature.parseValue("0"));
    ASSERT_TRUE(u32Feature.getValue().has_value());
    EXPECT_EQ(u32Feature.getValue().value(), 0u);
    EXPECT_EQ(u32Feature.getValueReadable(), "0 (0x0)");

    EXPECT_TRUE(u32Feature.parseValue(""));
    EXPECT_FALSE(u32Feature.getValue().has_value());
    EXPECT_EQ(u32Feature.getValueReadable(), "(Unset)");

    EXPECT_FALSE(u32Feature.parseValue("invalid_number"));
}

TEST_F(FeaturesTest, VulkanVersionFeatureInfoTest) {
    FeatureMap map;
    VulkanVersionFeatureInfo vkVersionFeature("TestVkVersion", "Description", &map);

    EXPECT_EQ(vkVersionFeature.getName(), "TestVkVersion");
    EXPECT_FALSE(vkVersionFeature.getValue().has_value());
    EXPECT_EQ(vkVersionFeature.getValueReadable(), "(Unset)");

    EXPECT_TRUE(vkVersionFeature.parseValue("1.2.3"));
    ASSERT_TRUE(vkVersionFeature.getValue().has_value());
    EXPECT_EQ(vkVersionFeature.getValue().value(), VK_MAKE_API_VERSION(0, 1, 2, 3));
    EXPECT_EQ(vkVersionFeature.getValueReadable(), "1.2.3");

    EXPECT_TRUE(vkVersionFeature.parseValue("1.3"));
    ASSERT_TRUE(vkVersionFeature.getValue().has_value());
    EXPECT_EQ(vkVersionFeature.getValue().value(), VK_MAKE_API_VERSION(0, 1, 3, 0));
    EXPECT_EQ(vkVersionFeature.getValueReadable(), "1.3.0");

    uint32_t intVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
    EXPECT_TRUE(vkVersionFeature.parseValue(std::to_string(intVersion)));
    ASSERT_TRUE(vkVersionFeature.getValue().has_value());
    EXPECT_EQ(vkVersionFeature.getValue().value(), intVersion);
    EXPECT_EQ(vkVersionFeature.getValueReadable(), "1.1.0");

    EXPECT_FALSE(vkVersionFeature.parseValue("invalid_version"));
}

}  // namespace
}  // namespace host
}  // namespace gfxstream
