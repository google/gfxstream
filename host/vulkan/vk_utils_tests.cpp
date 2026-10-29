// Copyright 2022 The Android Open Source Project
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

#include "vk_utils.h"

#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include "gfxstream/host/features.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace vk_util {
namespace vk_fn_info {

using gfxstream::host::LogLevel;
using gfxstream::host::SetGfxstreamLogCallback;

// Register a fake Vulkan function for testing.
using PFN_vkGfxstreamTestFunc = PFN_vkCreateDevice;
REGISTER_VK_FN_INFO(GfxstreamTestFunc, ("vkGfxstreamTestFunc", "vkGfxstreamTestFuncGOOGLE",
                                        "vkGfxstreamTestFuncGFXSTREAM"))
constexpr auto vkGfxstreamTestFuncNames = vk_fn_info::GetVkFnInfo<GfxstreamTestFunc>::names;

namespace {
using ::testing::_;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrNe;

TEST(getVkInstanceProcAddrWithFallbackTest, ShouldReturnNullOnFailure) {
    VkInstance instance = reinterpret_cast<VkInstance>(0x1234'0000);
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrAlwaysNULL;

    EXPECT_CALL(vkGetInstanceProcAddrAlwaysNULL, Call(instance, _)).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>({}, instance), nullptr);
    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>({nullptr, nullptr}, instance),
              nullptr);
    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {vkGetInstanceProcAddrAlwaysNULL.AsStdFunction(),
                   vkGetInstanceProcAddrAlwaysNULL.AsStdFunction()},
                  instance),
              nullptr);
}

TEST(getVkInstanceProcAddrWithFallbackTest, ShouldSkipNullVkGetInstanceProcAddr) {
    VkInstance instance = reinterpret_cast<VkInstance>(0x1234'0000);
    PFN_vkGfxstreamTestFunc validFp = reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x4321'0000);
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrMock;

    EXPECT_CALL(vkGetInstanceProcAddrMock, Call(instance, _))
        .WillRepeatedly(Return(reinterpret_cast<PFN_vkVoidFunction>(validFp)));

    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {nullptr, vkGetInstanceProcAddrMock.AsStdFunction()}, instance),
              validFp);
    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {vkGetInstanceProcAddrMock.AsStdFunction(), nullptr}, instance),
              validFp);
}

TEST(getVkInstanceProcAddrWithFallbackTest, ShouldSkipNullFpReturned) {
    VkInstance instance = reinterpret_cast<VkInstance>(0x1234'0000);
    PFN_vkGfxstreamTestFunc validFp = reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x4321'0000);
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrMock;
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrAlwaysNULL;

    // We know that vkGfxstreamTest has different names.
    EXPECT_CALL(vkGetInstanceProcAddrMock,
                Call(instance, StrNe(std::get<1>(vkGfxstreamTestFuncNames))))
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(vkGetInstanceProcAddrMock,
                Call(instance, StrEq(std::get<1>(vkGfxstreamTestFuncNames))))
        .WillRepeatedly(Return(reinterpret_cast<PFN_vkVoidFunction>(validFp)));
    EXPECT_CALL(vkGetInstanceProcAddrAlwaysNULL, Call(instance, _)).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {vkGetInstanceProcAddrMock.AsStdFunction(),
                   vkGetInstanceProcAddrAlwaysNULL.AsStdFunction()},
                  instance),
              validFp);
    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {vkGetInstanceProcAddrAlwaysNULL.AsStdFunction(),
                   vkGetInstanceProcAddrMock.AsStdFunction()},
                  instance),
              validFp);
}

TEST(getVkInstanceProcAddrWithFallbackTest, firstVkInstanceProcAddrShouldTakeThePriority) {
    VkInstance instance = reinterpret_cast<VkInstance>(0x1234'0000);
    PFN_vkGfxstreamTestFunc validFp1 = reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x4321'0000);
    PFN_vkGfxstreamTestFunc validFp2 = reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x3421'0070);
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrMock1;
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrMock2;

    EXPECT_CALL(vkGetInstanceProcAddrMock1, Call(instance, _))
        .WillRepeatedly(Return(reinterpret_cast<PFN_vkVoidFunction>(validFp1)));
    EXPECT_CALL(vkGetInstanceProcAddrMock2, Call(instance, _))
        .WillRepeatedly(Return(reinterpret_cast<PFN_vkVoidFunction>(validFp2)));

    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {vkGetInstanceProcAddrMock1.AsStdFunction(),
                   vkGetInstanceProcAddrMock2.AsStdFunction()},
                  instance),
              validFp1);
}

TEST(getVkInstanceProcAddrWithFallbackTest, firstNameShouldTakeThePriority) {
    VkInstance instance = reinterpret_cast<VkInstance>(0x1234'0000);
    PFN_vkGfxstreamTestFunc validFps[] = {reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x4321'0000),
                                          reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x3421'0070),
                                          reinterpret_cast<PFN_vkGfxstreamTestFunc>(0x2222'4321)};
    MockFunction<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>> vkGetInstanceProcAddrMock;

    EXPECT_CALL(vkGetInstanceProcAddrMock,
                Call(instance, StrEq(std::get<0>(vkGfxstreamTestFuncNames))))
        .WillRepeatedly(Return(reinterpret_cast<PFN_vkVoidFunction>(validFps[0])));
    ON_CALL(vkGetInstanceProcAddrMock, Call(instance, StrEq(std::get<1>(vkGfxstreamTestFuncNames))))
        .WillByDefault(Return(reinterpret_cast<PFN_vkVoidFunction>(validFps[1])));
    ON_CALL(vkGetInstanceProcAddrMock, Call(instance, StrEq(std::get<2>(vkGfxstreamTestFuncNames))))
        .WillByDefault(Return(reinterpret_cast<PFN_vkVoidFunction>(validFps[2])));

    EXPECT_EQ(getVkInstanceProcAddrWithFallback<GfxstreamTestFunc>(
                  {vkGetInstanceProcAddrMock.AsStdFunction()}, instance),
              validFps[0]);
}

TEST(VkCheckCallbacksDeathTest, deviceLostCallbackShouldBeCalled) {
    setVkCheckCallbacks(std::make_unique<VkCheckCallbacks>(VkCheckCallbacks{
        .onVkErrorDeviceLost = [] { exit(43); },
    }));

    EXPECT_EXIT(VK_CHECK(VK_ERROR_DEVICE_LOST), testing::ExitedWithCode(43), "");
}

TEST(VkCheckCallbacksDeathTest, deviceLostCallbackShouldNotBeCalled) {
    // Default death function uses exit code 42
    SetGfxstreamLogCallback([](LogLevel level,
                               const char* /*file*/,
                               int /*line*/,
                               const char* /*function*/,
                               const char* /*message*/) {
        if (level == LogLevel::kFatal) {
            exit(42);
        }
    });

    // Device lost death function uses exit code 43
    setVkCheckCallbacks(std::make_unique<VkCheckCallbacks>(VkCheckCallbacks{
        .onVkErrorDeviceLost = [] { exit(43); },
    }));

    EXPECT_EXIT(VK_CHECK(VK_ERROR_OUT_OF_DEVICE_MEMORY), testing::ExitedWithCode(42), "");
}

TEST(VkCheckCallbacksDeathTest, nullCallbacksShouldntCrash) {
    SetGfxstreamLogCallback([](LogLevel level,
                               const char* /*file*/,
                               int /*line*/,
                               const char* /*function*/,
                               const char* /*message*/) {
        if (level == LogLevel::kFatal) {
            exit(42);
        }
    });

    setVkCheckCallbacks(nullptr);
    EXPECT_EXIT(VK_CHECK(VK_ERROR_DEVICE_LOST), testing::ExitedWithCode(42), "");
}

TEST(VkCheckCallbacksDeathTest, nullVkDeviceLostErrorCallbackShouldntCrash) {
    SetGfxstreamLogCallback([](LogLevel level,
                               const char* /*file*/,
                               int /*line*/,
                               const char* /*function*/,
                               const char* /*message*/) {
        if (level == LogLevel::kFatal) {
            exit(42);
        }
    });

    setVkCheckCallbacks(
        std::make_unique<VkCheckCallbacks>(VkCheckCallbacks{.onVkErrorDeviceLost = nullptr}));
    EXPECT_EXIT(VK_CHECK(VK_ERROR_DEVICE_LOST), testing::ExitedWithCode(42), "");
}

template <typename T, class U = CrtpBase>
class ExampleCrtpClass1 : public U {
   public:
    void doCtrp1() {
        T& self = static_cast<T&>(*this);
        EXPECT_EQ(self.value, 42);
        self.doCtrp1WasCalled = true;
    }
};

template <typename T, class U = CrtpBase>
class ExampleCrtpClass2 : public U {
   public:
    void doCtrp2() {
        T& self = static_cast<T&>(*this);
        EXPECT_EQ(self.value, 42);
        self.doCtrp2WasCalled = true;
    }
};

template <typename T, class U = CrtpBase>
class ExampleCrtpClass3 : public U {
   public:
    void doCtrp3() {
        T& self = static_cast<T&>(*this);
        EXPECT_EQ(self.value, 42);
        self.doCtrp3WasCalled = true;
    }
};

struct MultiCrtpTestStruct
    : MultiCrtp<MultiCrtpTestStruct, ExampleCrtpClass1, ExampleCrtpClass2, ExampleCrtpClass3> {
    void doCtrpMethods() {
        doCtrp1();
        doCtrp2();
        doCtrp3();
    }

    int value = 42;
    bool doCtrp1WasCalled = false;
    bool doCtrp2WasCalled = false;
    bool doCtrp3WasCalled = false;
};

TEST(MultiCrtp, MultiCrtp) {
    MultiCrtpTestStruct object;
    object.doCtrpMethods();
    EXPECT_TRUE(object.doCtrp1WasCalled);
    EXPECT_TRUE(object.doCtrp2WasCalled);
    EXPECT_TRUE(object.doCtrp3WasCalled);
}

TEST(vk_util, vk_insert_struct) {
    VkDeviceCreateInfo deviceCi = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
    };
    VkPhysicalDeviceFeatures2 physicalDeviceFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = nullptr,
    };
    vk_insert_struct(deviceCi, physicalDeviceFeature);
    ASSERT_EQ(deviceCi.pNext, &physicalDeviceFeature);
    ASSERT_EQ(physicalDeviceFeature.pNext, nullptr);

    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
        .pNext = nullptr,
    };
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext = nullptr,
    };
    vk_insert_struct(ycbcrFeature, indexingFeatures);
    ASSERT_EQ(ycbcrFeature.pNext, &indexingFeatures);
    ASSERT_EQ(indexingFeatures.pNext, nullptr);

    vk_insert_struct(deviceCi, ycbcrFeature);
    const VkBaseInStructure* base = reinterpret_cast<VkBaseInStructure*>(&deviceCi);
    ASSERT_EQ(base, reinterpret_cast<VkBaseInStructure*>(&deviceCi));
    base = base->pNext;
    ASSERT_EQ(base, reinterpret_cast<VkBaseInStructure*>(&ycbcrFeature));
    base = base->pNext;
    ASSERT_EQ(base, reinterpret_cast<VkBaseInStructure*>(&indexingFeatures));
    base = base->pNext;
    ASSERT_EQ(base, reinterpret_cast<VkBaseInStructure*>(&physicalDeviceFeature));
    base = base->pNext;
    ASSERT_EQ(base, nullptr);
}

TEST(ShaderFloatControls2WorkaroundTest, FeatureSetRegistersAndTogglesFeature) {
    gfxstream::host::FeatureSet features;
    EXPECT_FALSE(features.VulkanDisableShaderFloatControls2.enabled());
    EXPECT_EQ(features.VulkanDisableShaderFloatControls2.getReason(), "Default reason");
    ASSERT_NE(features.map.find("VulkanDisableShaderFloatControls2"), features.map.end());

    const uint32_t kAffectedVendorId = 0x1010;
    const uint32_t kDummyVendorId = 0x1234;
    uint32_t vendorId = kDummyVendorId;
    bool hasExtension = true;

    GFXSTREAM_SET_BOOL_FEATURE_ON_CONDITION(&features, VulkanDisableShaderFloatControls2,
                                            hasExtension && vendorId == kAffectedVendorId);
    EXPECT_FALSE(features.VulkanDisableShaderFloatControls2.enabled());
    EXPECT_EQ(features.VulkanDisableShaderFloatControls2.getReason(),
              "hasExtension && vendorId == kAffectedVendorId");

    vendorId = kAffectedVendorId;
    GFXSTREAM_SET_BOOL_FEATURE_ON_CONDITION(&features, VulkanDisableShaderFloatControls2,
                                            hasExtension && vendorId == kAffectedVendorId);
    EXPECT_TRUE(features.VulkanDisableShaderFloatControls2.enabled());

    EXPECT_TRUE(
        features.processFeatureString("VulkanDisableShaderFloatControls2:disabled", "CLI override"));
    EXPECT_FALSE(features.VulkanDisableShaderFloatControls2.enabled());
    EXPECT_EQ(features.VulkanDisableShaderFloatControls2.getReason(), "CLI override");

    EXPECT_TRUE(
        features.processFeatureString("VulkanDisableShaderFloatControls2:enabled", "CLI override"));
    EXPECT_TRUE(features.VulkanDisableShaderFloatControls2.enabled());
}

TEST(ShaderFloatControls2WorkaroundTest, ClearsFeatureInExtensionAndVulkan14Structs) {
    clearShaderFloatControls2Features(nullptr);

    VkPhysicalDeviceVulkan14Features vk14Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = nullptr,
        .shaderFloatControls2 = VK_TRUE,
        .maintenance5 = VK_TRUE,
    };
    VkPhysicalDeviceShaderFloatControls2Features fc2Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES,
        .pNext = &vk14Features,
        .shaderFloatControls2 = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &fc2Features,
    };

    clearShaderFloatControls2Features(&features2);

    EXPECT_EQ(fc2Features.shaderFloatControls2, static_cast<VkBool32>(VK_FALSE));
    EXPECT_EQ(vk14Features.shaderFloatControls2, static_cast<VkBool32>(VK_FALSE));
    EXPECT_EQ(vk14Features.maintenance5, static_cast<VkBool32>(VK_TRUE));
}

TEST(ShaderFloatControls2WorkaroundTest, ValidatesDeviceCreateInfoExtensionsAndFeatures) {
    EXPECT_EQ(validateShaderFloatControls2CreateInfo(nullptr), VK_SUCCESS);

    const char* kBlockedExts[] = {
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME,
    };
    VkDeviceCreateInfo createInfoWithBlockedExt = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = kBlockedExts,
    };
    EXPECT_EQ(validateShaderFloatControls2CreateInfo(&createInfoWithBlockedExt),
              VK_ERROR_EXTENSION_NOT_PRESENT);

    const char* kAllowedExts[] = {VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME};
    VkPhysicalDeviceShaderFloatControls2Features fc2Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES,
        .pNext = nullptr,
        .shaderFloatControls2 = VK_TRUE,
    };
    VkDeviceCreateInfo createInfoWithFc2Feature = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &fc2Features,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = kAllowedExts,
    };
    EXPECT_EQ(validateShaderFloatControls2CreateInfo(&createInfoWithFc2Feature),
              VK_ERROR_FEATURE_NOT_PRESENT);

    fc2Features.shaderFloatControls2 = VK_FALSE;
    VkPhysicalDeviceVulkan14Features vk14Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &fc2Features,
        .shaderFloatControls2 = VK_TRUE,
        .maintenance5 = VK_TRUE,
    };
    VkDeviceCreateInfo createInfoWithVk14Feature = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk14Features,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = kAllowedExts,
    };
    EXPECT_EQ(validateShaderFloatControls2CreateInfo(&createInfoWithVk14Feature),
              VK_ERROR_FEATURE_NOT_PRESENT);

    vk14Features.shaderFloatControls2 = VK_FALSE;
    EXPECT_EQ(validateShaderFloatControls2CreateInfo(&createInfoWithVk14Feature), VK_SUCCESS);
}

TEST(AhbFormatMappingTest, MapsVkFormatsAndBytesPerPixelConsistently) {
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_R8G8B8A8_UNORM), 1u);
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_B8G8R8A8_UNORM), 5u);
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_R5G6B5_UNORM_PACK16), 4u);
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_R16G16B16A16_SFLOAT), 0x16u);
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32), 0x2bu);
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_R8_UNORM), 0x38u);
    EXPECT_EQ(vkFormatToAhbFormat(VK_FORMAT_UNDEFINED), 0u);

    EXPECT_EQ(ahbFormatBytesPerPixel(1u), 4u);
    EXPECT_EQ(ahbFormatBytesPerPixel(5u), 4u);
    EXPECT_EQ(ahbFormatBytesPerPixel(3u), 3u);
    EXPECT_EQ(ahbFormatBytesPerPixel(4u), 2u);
    EXPECT_EQ(ahbFormatBytesPerPixel(0x16u), 8u);
    EXPECT_EQ(ahbFormatBytesPerPixel(0x2bu), 4u);
    EXPECT_EQ(ahbFormatBytesPerPixel(0x38u), 1u);
    EXPECT_EQ(ahbFormatBytesPerPixel(0u), 0u);
}

}  // namespace
}  // namespace vk_fn_info
}  // namespace vk_util
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
