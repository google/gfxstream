// Copyright (C) 2026 The Android Open Source Project
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

#include "vk_decoder_snapshot.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

#include "vulkan_boxed_handles.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

using ::testing::Not;

MATCHER_P(HasApiCall, apiCallHandle, "") { return arg.hasApiCall(apiCallHandle); }

template <typename HandleT>
HandleT MakeHandle(BoxedHandleTypeTag tag, uint64_t index) {
    return reinterpret_cast<HandleT>(
        BoxedHandleManager::Store::EM::makeHandle(index, /*generation=*/1, tag));
}

class VkDecoderSnapshotTest : public ::testing::Test {
   protected:
    uint8_t mUnusedPacket = 1;
    size_t mUnusedPacketSize = 1;
};

TEST_F(VkDecoderSnapshotTest, EnumerateTwice) {
    VkDecoderSnapshot snapshot;

    const VkInstanceCreateInfo instanceCreateInfo = {};
    VkInstance instance = MakeHandle<VkInstance>(Tag_VkInstance, 1);

    const auto createInstanceApiCall = snapshot.createApiCallInfo();
    snapshot.vkCreateInstance(nullptr, createInstanceApiCall, &mUnusedPacket, mUnusedPacketSize,
                              VK_SUCCESS, &instanceCreateInfo, nullptr, &instance);
    snapshot.destroyApiCallInfoIfUnused(createInstanceApiCall);

    uint32_t physicalDeviceCount = 1;
    VkPhysicalDevice physicalDevice = MakeHandle<VkPhysicalDevice>(Tag_VkPhysicalDevice, 1);

    const auto enumerateCall1 = snapshot.createApiCallInfo();
    snapshot.vkEnumeratePhysicalDevices(nullptr, enumerateCall1, &mUnusedPacket, mUnusedPacketSize,
                                        VK_SUCCESS, instance, &physicalDeviceCount,
                                        &physicalDevice);
    snapshot.destroyApiCallInfoIfUnused(enumerateCall1);

    const auto enumerateCall2 = snapshot.createApiCallInfo();
    snapshot.vkEnumeratePhysicalDevices(nullptr, enumerateCall2, &mUnusedPacket, mUnusedPacketSize,
                                        VK_SUCCESS, instance, &physicalDeviceCount,
                                        &physicalDevice);
    snapshot.destroyApiCallInfoIfUnused(enumerateCall2);

    EXPECT_THAT(snapshot, HasApiCall(createInstanceApiCall));
    EXPECT_THAT(snapshot, Not(HasApiCall(enumerateCall1)));
    EXPECT_THAT(snapshot, HasApiCall(enumerateCall2));
}

TEST_F(VkDecoderSnapshotTest, ModifyOnlyCallsAreReclaimedWhenUnused) {
    VkDecoderSnapshot snapshot;

    VkCommandBuffer commandBuffer = MakeHandle<VkCommandBuffer>(Tag_VkCommandBuffer, 1);
    const VkCommandBufferBeginInfo beginInfo = {};

    const auto beginCall = snapshot.createApiCallInfo();
    snapshot.vkBeginCommandBuffer(nullptr, beginCall, &mUnusedPacket, mUnusedPacketSize, VK_SUCCESS,
                                  commandBuffer, &beginInfo);
    snapshot.destroyApiCallInfoIfUnused(beginCall);

    const auto endCall = snapshot.createApiCallInfo();
    snapshot.vkEndCommandBuffer(nullptr, endCall, &mUnusedPacket, mUnusedPacketSize, VK_SUCCESS,
                                commandBuffer);
    snapshot.destroyApiCallInfoIfUnused(endCall);

    EXPECT_THAT(snapshot, Not(HasApiCall(beginCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(endCall)));
}

TEST_F(VkDecoderSnapshotTest, DestroyBufferReclaimsCreateAndDestroyCalls) {
    VkDecoderSnapshot snapshot;

    const VkInstanceCreateInfo instanceCreateInfo = {};
    VkInstance instance = MakeHandle<VkInstance>(Tag_VkInstance, 1);
    const auto createInstanceCall = snapshot.createApiCallInfo();
    snapshot.vkCreateInstance(nullptr, createInstanceCall, &mUnusedPacket, mUnusedPacketSize,
                              VK_SUCCESS, &instanceCreateInfo, nullptr, &instance);
    snapshot.destroyApiCallInfoIfUnused(createInstanceCall);

    uint32_t physicalDeviceCount = 1;
    VkPhysicalDevice physicalDevice = MakeHandle<VkPhysicalDevice>(Tag_VkPhysicalDevice, 1);
    const auto enumerateCall = snapshot.createApiCallInfo();
    snapshot.vkEnumeratePhysicalDevices(nullptr, enumerateCall, &mUnusedPacket, mUnusedPacketSize,
                                        VK_SUCCESS, instance, &physicalDeviceCount,
                                        &physicalDevice);
    snapshot.destroyApiCallInfoIfUnused(enumerateCall);

    const VkDeviceCreateInfo deviceCreateInfo = {};
    VkDevice device = MakeHandle<VkDevice>(Tag_VkDevice, 1);
    const auto createDeviceCall = snapshot.createApiCallInfo();
    snapshot.vkCreateDevice(nullptr, createDeviceCall, &mUnusedPacket, mUnusedPacketSize,
                            VK_SUCCESS, physicalDevice, &deviceCreateInfo, nullptr, &device);
    snapshot.destroyApiCallInfoIfUnused(createDeviceCall);

    const VkBufferCreateInfo bufferCreateInfo = {};
    VkBuffer buffer = MakeHandle<VkBuffer>(Tag_VkBuffer, 1);
    const auto createBufferCall = snapshot.createApiCallInfo();
    snapshot.vkCreateBuffer(nullptr, createBufferCall, &mUnusedPacket, mUnusedPacketSize,
                            VK_SUCCESS, device, &bufferCreateInfo, nullptr, &buffer);
    snapshot.destroyApiCallInfoIfUnused(createBufferCall);
    EXPECT_THAT(snapshot, HasApiCall(createBufferCall));

    const auto destroyBufferCall = snapshot.createApiCallInfo();
    snapshot.vkDestroyBuffer(nullptr, destroyBufferCall, &mUnusedPacket, mUnusedPacketSize, device,
                             buffer, nullptr);
    snapshot.destroyApiCallInfoIfUnused(destroyBufferCall);

    EXPECT_THAT(snapshot, Not(HasApiCall(createBufferCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(destroyBufferCall)));
    EXPECT_THAT(snapshot, HasApiCall(createDeviceCall));
}

TEST_F(VkDecoderSnapshotTest, DelayedDeleteTypesSurviveUntilAncestorIsDestroyed) {
    VkDecoderSnapshot snapshot;

    const VkInstanceCreateInfo instanceCreateInfo = {};
    VkInstance instance = MakeHandle<VkInstance>(Tag_VkInstance, 1);
    const auto createInstanceCall = snapshot.createApiCallInfo();
    snapshot.vkCreateInstance(nullptr, createInstanceCall, &mUnusedPacket, mUnusedPacketSize,
                              VK_SUCCESS, &instanceCreateInfo, nullptr, &instance);
    snapshot.destroyApiCallInfoIfUnused(createInstanceCall);

    uint32_t physicalDeviceCount = 1;
    VkPhysicalDevice physicalDevice = MakeHandle<VkPhysicalDevice>(Tag_VkPhysicalDevice, 1);
    const auto enumerateCall = snapshot.createApiCallInfo();
    snapshot.vkEnumeratePhysicalDevices(nullptr, enumerateCall, &mUnusedPacket, mUnusedPacketSize,
                                        VK_SUCCESS, instance, &physicalDeviceCount,
                                        &physicalDevice);
    snapshot.destroyApiCallInfoIfUnused(enumerateCall);

    const VkDeviceCreateInfo deviceCreateInfo = {};
    VkDevice device = MakeHandle<VkDevice>(Tag_VkDevice, 1);
    const auto createDeviceCall = snapshot.createApiCallInfo();
    snapshot.vkCreateDevice(nullptr, createDeviceCall, &mUnusedPacket, mUnusedPacketSize,
                            VK_SUCCESS, physicalDevice, &deviceCreateInfo, nullptr, &device);
    snapshot.destroyApiCallInfoIfUnused(createDeviceCall);

    const VkShaderModuleCreateInfo shaderCreateInfo = {};
    VkShaderModule shader = MakeHandle<VkShaderModule>(Tag_VkShaderModule, 1);
    const auto createShaderCall = snapshot.createApiCallInfo();
    snapshot.vkCreateShaderModule(nullptr, createShaderCall, &mUnusedPacket, mUnusedPacketSize,
                                  VK_SUCCESS, device, &shaderCreateInfo, nullptr, &shader);
    snapshot.destroyApiCallInfoIfUnused(createShaderCall);

    const VkSamplerCreateInfo samplerCreateInfo = {};
    VkSampler sampler = MakeHandle<VkSampler>(Tag_VkSampler, 1);
    const auto createSamplerCall = snapshot.createApiCallInfo();
    snapshot.vkCreateSampler(nullptr, createSamplerCall, &mUnusedPacket, mUnusedPacketSize,
                             VK_SUCCESS, device, &samplerCreateInfo, nullptr, &sampler);
    snapshot.destroyApiCallInfoIfUnused(createSamplerCall);

    // Delayed-delete types stay alive while their VkDevice is still running.
    const auto destroyShaderCall = snapshot.createApiCallInfo();
    snapshot.vkDestroyShaderModule(nullptr, destroyShaderCall, &mUnusedPacket, mUnusedPacketSize,
                                   device, shader, nullptr);
    snapshot.destroyApiCallInfoIfUnused(destroyShaderCall);

    const auto destroySamplerCall = snapshot.createApiCallInfo();
    snapshot.vkDestroySampler(nullptr, destroySamplerCall, &mUnusedPacket, mUnusedPacketSize,
                              device, sampler, nullptr);
    snapshot.destroyApiCallInfoIfUnused(destroySamplerCall);

    EXPECT_THAT(snapshot, HasApiCall(createShaderCall));
    EXPECT_THAT(snapshot, HasApiCall(createSamplerCall));
    EXPECT_THAT(snapshot, Not(HasApiCall(destroyShaderCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(destroySamplerCall)));

    // Destroying the VkInstance reclaims the device and all delayed-delete children.
    const auto destroyInstanceCall = snapshot.createApiCallInfo();
    snapshot.vkDestroyInstance(nullptr, destroyInstanceCall, &mUnusedPacket, mUnusedPacketSize,
                               instance, nullptr);
    snapshot.destroyApiCallInfoIfUnused(destroyInstanceCall);

    EXPECT_THAT(snapshot, Not(HasApiCall(createInstanceCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(enumerateCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(createDeviceCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(createShaderCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(createSamplerCall)));
    EXPECT_THAT(snapshot, Not(HasApiCall(destroyInstanceCall)));
}

TEST_F(VkDecoderSnapshotTest, BatchCreateKeepsApiCallUntilAllCreatedHandlesAreDestroyed) {
    VkDecoderSnapshot snapshot;

    VkDevice device = MakeHandle<VkDevice>(Tag_VkDevice, 1);
    const VkDeviceCreateInfo deviceCreateInfo = {};
    VkPhysicalDevice physicalDevice = MakeHandle<VkPhysicalDevice>(Tag_VkPhysicalDevice, 1);
    const auto createDeviceCall = snapshot.createApiCallInfo();
    snapshot.vkCreateDevice(nullptr, createDeviceCall, &mUnusedPacket, mUnusedPacketSize,
                            VK_SUCCESS, physicalDevice, &deviceCreateInfo, nullptr, &device);
    snapshot.destroyApiCallInfoIfUnused(createDeviceCall);

    const VkComputePipelineCreateInfo pipelineCreateInfos[2] = {{}, {}};
    VkPipeline pipelines[2] = {
        MakeHandle<VkPipeline>(Tag_VkPipeline, 1),
        MakeHandle<VkPipeline>(Tag_VkPipeline, 2),
    };
    const auto createPipelinesCall = snapshot.createApiCallInfo();
    snapshot.vkCreateComputePipelines(nullptr, createPipelinesCall, &mUnusedPacket,
                                      mUnusedPacketSize, VK_SUCCESS, device, VK_NULL_HANDLE, 2,
                                      pipelineCreateInfos, nullptr, pipelines);
    snapshot.destroyApiCallInfoIfUnused(createPipelinesCall);
    EXPECT_THAT(snapshot, HasApiCall(createPipelinesCall));

    // Destroying one of the two batch-created pipelines keeps the batch call alive.
    const auto destroyPipeline0Call = snapshot.createApiCallInfo();
    snapshot.vkDestroyPipeline(nullptr, destroyPipeline0Call, &mUnusedPacket, mUnusedPacketSize,
                               device, pipelines[0], nullptr);
    snapshot.destroyApiCallInfoIfUnused(destroyPipeline0Call);
    EXPECT_THAT(snapshot, HasApiCall(createPipelinesCall));

    // Destroying the remaining pipeline reclaims the batch call.
    const auto destroyPipeline1Call = snapshot.createApiCallInfo();
    snapshot.vkDestroyPipeline(nullptr, destroyPipeline1Call, &mUnusedPacket, mUnusedPacketSize,
                               device, pipelines[1], nullptr);
    snapshot.destroyApiCallInfoIfUnused(destroyPipeline1Call);
    EXPECT_THAT(snapshot, Not(HasApiCall(createPipelinesCall)));
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
