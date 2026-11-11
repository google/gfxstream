// copyright (c) 2022 the android open source project
//
// licensed under the apache license, version 2.0 (the "license");
// you may not use this file except in compliance with the license.
// you may obtain a copy of the license at
//
// http://www.apache.org/licenses/license-2.0
//
// unless required by applicable law or agreed to in writing, software
// distributed under the license is distributed on an "as is" basis,
// without warranties or conditions of any kind, either express or implied.
// see the license for the specific language governing permissions and
// limitations under the license.

#include "vk_decoder_global_state.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "gfxstream/BumpPool.h"
#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/host/features.h"
#include "gfxstream/system/System.h"
#include "gfxstream/testing/TestUtils.h"
#include "render_thread_info_vk.h"
#include "vk_common_operations.h"
#include "vk_snapshot_handles.h"
#include "vulkan_boxed_handles.h"
#include "vulkan_dispatch.h"
#include "vulkan_stream.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

// Minimal SPIR-V compute shader: `#version 450 void main() {}`
constexpr uint32_t kEmptyComputeShader[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x00000006, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0005000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x00060010, 0x00000004, 0x00000011,
    0x00000001, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47,
    0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365, 0x00006576,
    0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
    0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x000100fd, 0x00010038,
};

class VkDecoderGlobalStateTest : public ::testing::Test {
   protected:
    static void SetUpTestSuite() {
        ASSERT_TRUE(gfxstream::testing::SetupGraphicsTestEnvironment())
            << "Failed to configure graphics test environment";
    }

    void SetUp() override {
        mVk = vkDispatch(!gfxstream::testing::IsGraphicsTestEnvironmentProvidingVulkanDriver());
        ASSERT_NE(mVk, nullptr);

        gfxstream::host::FeatureSet features;
        features.GlDirectMem.setEnabled(true);
        features.VulkanBatchedDescriptorSetUpdate.setEnabled(true);

        gfxstream::base::setEnvironmentVariable("ANDROID_EMU_VK_NO_CLEANUP", "1");
        mVkEmu = VkEmulation::create(mVk, {}, features);
        ASSERT_NE(mVkEmu, nullptr);

        VkDecoderGlobalState::initialize(mVkEmu.get());
        mState = VkDecoderGlobalState::get();
        ASSERT_NE(mState, nullptr);

        mThreadInfo = std::make_unique<RenderThreadInfoVk>();
        mThreadInfo->ctx_id = 1;
    }

    void TearDown() override {
        if (mBoxedDevice != VK_NULL_HANDLE) {
            mState->on_vkDestroyDevice(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, nullptr);
            mBoxedDevice = VK_NULL_HANDLE;
        }
        if (mBoxedInstance != VK_NULL_HANDLE) {
            mState->on_vkDestroyInstance(&mPool, kInvalidSnapshotApiCallHandle, mBoxedInstance,
                                         nullptr);
            mBoxedInstance = VK_NULL_HANDLE;
        }
        mThreadInfo.reset();
        VkDecoderGlobalState::reset();
        mVkEmu.reset();
    }

    void createInstanceAndDevice() {
        const VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "VkDecoderGlobalStateTest",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "gfxstream",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1,
        };
        const VkInstanceCreateInfo instanceCi = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
        };
        ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateInstance(&mPool, kInvalidSnapshotApiCallHandle,
                                                          &instanceCi, nullptr, &mBoxedInstance));
        ASSERT_NE(VK_NULL_HANDLE, mBoxedInstance);

        uint32_t physDevCount = 0;
        ASSERT_EQ(VK_SUCCESS,
                  mState->on_vkEnumeratePhysicalDevices(&mPool, kInvalidSnapshotApiCallHandle,
                                                        mBoxedInstance, &physDevCount, nullptr));
        ASSERT_GT(physDevCount, 0u);
        std::vector<VkPhysicalDevice> physDevs(physDevCount, VK_NULL_HANDLE);
        ASSERT_EQ(VK_SUCCESS, mState->on_vkEnumeratePhysicalDevices(
                                  &mPool, kInvalidSnapshotApiCallHandle, mBoxedInstance,
                                  &physDevCount, physDevs.data()));
        mBoxedPhysDev = physDevs[0];
        ASSERT_NE(VK_NULL_HANDLE, mBoxedPhysDev);

        VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
            .samplerYcbcrConversion = VK_TRUE,
        };
        const float queuePriority = 1.0f;
        const VkDeviceQueueCreateInfo queueCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
        const VkDeviceCreateInfo deviceCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &ycbcrFeatures,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCi,
        };
        ASSERT_EQ(VK_SUCCESS,
                  mState->on_vkCreateDevice(&mPool, kInvalidSnapshotApiCallHandle, mBoxedPhysDev,
                                            &deviceCi, nullptr, &mBoxedDevice));
        ASSERT_NE(VK_NULL_HANDLE, mBoxedDevice);

        mState->on_vkGetDeviceQueue(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, 0, 0,
                                    &mBoxedQueue);
        ASSERT_NE(VK_NULL_HANDLE, mBoxedQueue);
    }

    VkDescriptorPool createDescriptorPool(uint32_t maxSets) {
        const VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = maxSets,
        };
        const VkDescriptorPoolCreateInfo poolCi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = maxSets,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        VkDescriptorPool boxedPool = VK_NULL_HANDLE;
        EXPECT_EQ(VK_SUCCESS,
                  mState->on_vkCreateDescriptorPool(&mPool, kInvalidSnapshotApiCallHandle,
                                                    mBoxedDevice, &poolCi, nullptr, &boxedPool));
        return boxedPool;
    }

    VkDescriptorSetLayout createDescriptorSetLayout() {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
        const VkDescriptorSetLayoutCreateInfo layoutCi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding,
        };
        VkDescriptorSetLayout boxedLayout = VK_NULL_HANDLE;
        EXPECT_EQ(VK_SUCCESS, mState->on_vkCreateDescriptorSetLayout(
                                  &mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, &layoutCi,
                                  nullptr, &boxedLayout));
        return boxedLayout;
    }

    VulkanDispatch* mVk = nullptr;
    std::unique_ptr<VkEmulation> mVkEmu;
    VkDecoderGlobalState* mState = nullptr;
    std::unique_ptr<RenderThreadInfoVk> mThreadInfo;
    gfxstream::base::BumpPool mPool;
    VkInstance mBoxedInstance = VK_NULL_HANDLE;
    VkPhysicalDevice mBoxedPhysDev = VK_NULL_HANDLE;
    VkDevice mBoxedDevice = VK_NULL_HANDLE;
    VkQueue mBoxedQueue = VK_NULL_HANDLE;
};

using VkDecoderGlobalStateDescriptorSetPoolIdTest = VkDecoderGlobalStateTest;
using VkDecoderGlobalStateProcessCleanupLeakTest = VkDecoderGlobalStateTest;

TEST_F(VkDecoderGlobalStateDescriptorSetPoolIdTest, BogusPoolIdReturnsNullWithoutCrash) {
    createInstanceAndDevice();

    VkDescriptorPool boxedPool = createDescriptorPool(/*maxSets=*/1);
    ASSERT_NE(VK_NULL_HANDLE, boxedPool);
    VkDescriptorSetLayout boxedSetLayout = createDescriptorSetLayout();
    ASSERT_NE(VK_NULL_HANDLE, boxedSetLayout);

    constexpr uint64_t kBogusPoolId = 0xdeadbeef'cafebabeULL;
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(kBogusPoolId));

    VkDescriptorPool unboxedPool = unbox_VkDescriptorPool(boxedPool);
    VkDescriptorSetLayout unboxedSetLayout = unbox_VkDescriptorSetLayout(boxedSetLayout);
    uint32_t whichPool = 0;
    uint32_t pendingAlloc = 1;
    uint32_t writeStartIndex = 0;
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    };
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &kBogusPoolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/1, &write);
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(kBogusPoolId));

    mState->on_vkDestroyDescriptorSetLayout(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                            unboxedSetLayout, nullptr);
    delete_VkDescriptorSetLayout(boxedSetLayout);
    mState->on_vkDestroyDescriptorPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                       unboxedPool, nullptr);
    delete_VkDescriptorPool(boxedPool);
}

TEST_F(VkDecoderGlobalStateDescriptorSetPoolIdTest, UnallocatedAndAllocatedPoolIdHandling) {
    createInstanceAndDevice();

    VkDescriptorPool boxedPool = createDescriptorPool(/*maxSets=*/1);
    ASSERT_NE(VK_NULL_HANDLE, boxedPool);
    VkDescriptorSetLayout boxedSetLayout = createDescriptorSetLayout();
    ASSERT_NE(VK_NULL_HANDLE, boxedSetLayout);

    VkDescriptorPool unboxedPool = unbox_VkDescriptorPool(boxedPool);
    VkDescriptorSetLayout unboxedSetLayout = unbox_VkDescriptorSetLayout(boxedSetLayout);

    uint32_t poolIdCount = 1;
    uint64_t poolId = 0;
    mState->on_vkCollectDescriptorPoolIdsGOOGLE(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                                unboxedPool, &poolIdCount, &poolId);
    ASSERT_EQ(1u, poolIdCount);
    BoxedHandleInfo* setHandleInfo = sBoxedHandleManager.get(poolId);
    ASSERT_NE(nullptr, setHandleInfo);
    EXPECT_EQ(0u, setHandleInfo->underlying);

    // Referencing an unallocated preallocated poolId with pendingAlloc=0 must not allocate.
    uint32_t whichPool = 0;
    uint32_t pendingAlloc = 0;
    uint32_t writeStartIndex = 0;
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &poolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/0, nullptr);
    EXPECT_EQ(0u, setHandleInfo->underlying);

    // Committing with pendingAlloc=1 allocates the underlying host VkDescriptorSet.
    pendingAlloc = 1;
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &poolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/0, nullptr);
    const uint64_t allocatedUnderlying = setHandleInfo->underlying;
    EXPECT_NE(0u, allocatedUnderlying);

    // Subsequent commits with pendingAlloc=0 reuse the already-allocated underlying handle.
    pendingAlloc = 0;
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &poolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/0, nullptr);
    EXPECT_EQ(allocatedUnderlying, setHandleInfo->underlying);

    mState->on_vkDestroyDescriptorSetLayout(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                            unboxedSetLayout, nullptr);
    delete_VkDescriptorSetLayout(boxedSetLayout);
    mState->on_vkDestroyDescriptorPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                       unboxedPool, nullptr);
    delete_VkDescriptorPool(boxedPool);
}

TEST_F(VkDecoderGlobalStateDescriptorSetPoolIdTest, CollectDescriptorPoolIdsClampsCount) {
    createInstanceAndDevice();

    VkDescriptorPool boxedPool = createDescriptorPool(/*maxSets=*/4);
    ASSERT_NE(VK_NULL_HANDLE, boxedPool);
    VkDescriptorPool unboxedPool = unbox_VkDescriptorPool(boxedPool);

    uint32_t count = 0;
    mState->on_vkCollectDescriptorPoolIdsGOOGLE(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                                unboxedPool, &count, nullptr);
    EXPECT_EQ(4u, count);

    uint64_t expectedIds[4] = {0, 0, 0, 0};
    count = 4;
    mState->on_vkCollectDescriptorPoolIdsGOOGLE(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                                unboxedPool, &count, expectedIds);
    ASSERT_EQ(4u, count);
    for (uint64_t id : expectedIds) {
        EXPECT_NE(0u, id);
    }

    uint64_t outIds[4] = {0, 0, 0, 0};
    count = 1;
    mState->on_vkCollectDescriptorPoolIdsGOOGLE(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                                unboxedPool, &count, outIds);
    EXPECT_EQ(1u, count);
    EXPECT_EQ(expectedIds[0], outIds[0]);
    EXPECT_EQ(0u, outIds[1]);

    count = 10;
    mState->on_vkCollectDescriptorPoolIdsGOOGLE(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                                unboxedPool, &count, outIds);
    EXPECT_EQ(4u, count);
    EXPECT_EQ(expectedIds[0], outIds[0]);
    EXPECT_EQ(expectedIds[1], outIds[1]);
    EXPECT_EQ(expectedIds[2], outIds[2]);
    EXPECT_EQ(expectedIds[3], outIds[3]);

    mState->on_vkDestroyDescriptorPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                       unboxedPool, nullptr);
    delete_VkDescriptorPool(boxedPool);
}

TEST(VkDecoderGlobalStateBoxedHandleTest, OrderMaintenanceInfoOutlivesDeletedHandle) {
    VkCommandBuffer boxed =
        new_boxed_VkCommandBuffer(reinterpret_cast<VkCommandBuffer>(0x5555'0000), nullptr);
    std::shared_ptr<OrderMaintenanceInfo> order = ordmaint_VkCommandBuffer(boxed);
    ASSERT_NE(nullptr, order);
    EXPECT_EQ(order, ordmaint_VkCommandBuffer(boxed));

    delete_VkCommandBuffer(boxed);
    EXPECT_EQ(nullptr, ordmaint_VkCommandBuffer(boxed));
    EXPECT_EQ(1, order.use_count());
}

TEST(VkDecoderGlobalStateBoxedHandleTest, ConcurrentOrderMaintenanceInfoLookupAndDelete) {
    constexpr int kIterations = 20000;
    std::atomic<uint64_t> currentHandle{0};
    std::atomic<bool> done{false};

    std::thread reader([&]() {
        while (!done.load(std::memory_order_acquire)) {
            VkCommandBuffer boxed =
                reinterpret_cast<VkCommandBuffer>(currentHandle.load(std::memory_order_acquire));
            std::shared_ptr<OrderMaintenanceInfo> order = ordmaint_VkCommandBuffer(boxed);
            if (order) {
                EXPECT_EQ(0u, __atomic_load_n(&order->sequenceNumber, __ATOMIC_ACQUIRE));
            }
        }
    });

    for (int i = 0; i < kIterations; ++i) {
        VkCommandBuffer boxed =
            new_boxed_VkCommandBuffer(reinterpret_cast<VkCommandBuffer>(0x5555'0000), nullptr);
        currentHandle.store(reinterpret_cast<uint64_t>(boxed), std::memory_order_release);
        delete_VkCommandBuffer(boxed);
    }

    done.store(true, std::memory_order_release);
    reader.join();
}

// Regression test for b/573373414: the host driver can hand out the same unboxed handle value
// for a new object right after the previous object was destroyed, and the new boxed handle may be
// registered before the old boxed handle is removed. Removing the old boxed handle must not drop
// the reverse mapping owned by the new boxed handle, otherwise unboxed_to_boxed_* lookups fail
// and (among other things) snapshot dependency tracking for dedicated allocations breaks.
TEST(BoxedHandleManagerReverseMapTest, RemoveOldBoxedHandleKeepsReusedUnboxedMapping) {
    VkImage underlying = reinterpret_cast<VkImage>(0x5555'0000);

    VkImage boxedOld = new_boxed_non_dispatchable_VkImage(underlying);
    ASSERT_NE(VK_NULL_HANDLE, boxedOld);
    EXPECT_EQ(boxedOld, unboxed_to_boxed_non_dispatchable_VkImage(underlying));

    // Host destroys the old image and immediately reuses the handle value for a new image, whose
    // boxed handle gets registered before the old boxed handle is deleted.
    VkImage boxedNew = new_boxed_non_dispatchable_VkImage(underlying);
    ASSERT_NE(VK_NULL_HANDLE, boxedNew);
    ASSERT_NE(boxedOld, boxedNew);
    EXPECT_EQ(boxedNew, unboxed_to_boxed_non_dispatchable_VkImage(underlying));

    delete_VkImage(boxedOld);
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedOld)));
    // The new boxed handle must still be reachable from the (reused) unboxed handle.
    EXPECT_EQ(boxedNew, unboxed_to_boxed_non_dispatchable_VkImage(underlying));

    delete_VkImage(boxedNew);
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(underlying));
}

TEST(BoxedHandleManagerReverseMapTest, UpdateOldUnboxedDoesNotDropReusedMapping) {
    VkImage underlyingA = reinterpret_cast<VkImage>(0x6666'0000);
    VkImage underlyingB = reinterpret_cast<VkImage>(0x7777'0000);

    VkImage boxed1 = new_boxed_non_dispatchable_VkImage(underlyingA);
    // Another boxed handle takes over underlyingA (reuse) before boxed1 is re-pointed.
    VkImage boxed2 = new_boxed_non_dispatchable_VkImage(underlyingA);
    EXPECT_EQ(boxed2, unboxed_to_boxed_non_dispatchable_VkImage(underlyingA));

    set_boxed_non_dispatchable_VkImage(boxed1, underlyingB);
    EXPECT_EQ(boxed1, unboxed_to_boxed_non_dispatchable_VkImage(underlyingB));
    // boxed2's mapping for underlyingA must survive boxed1's update.
    EXPECT_EQ(boxed2, unboxed_to_boxed_non_dispatchable_VkImage(underlyingA));

    delete_VkImage(boxed1);
    delete_VkImage(boxed2);
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(underlyingA));
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(underlyingB));
}

TEST(BoxedHandleManagerReverseMapTest, NullUnboxedHandleIsNeverReverseMapped) {
    // Boxing VK_NULL_HANDLE must not register key 0 in the reverse map: it could never be erased
    // (eraseReverseMappingLocked skips 0) and would make lookups of VK_NULL_HANDLE return a
    // stale boxed handle.
    VkImage boxedNull = new_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE);
    ASSERT_NE(VK_NULL_HANDLE, boxedNull);
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE));

    VkImage underlying = reinterpret_cast<VkImage>(0x9999'0000);
    set_boxed_non_dispatchable_VkImage(boxedNull, underlying);
    EXPECT_EQ(boxedNull, unboxed_to_boxed_non_dispatchable_VkImage(underlying));
    set_boxed_non_dispatchable_VkImage(boxedNull, VK_NULL_HANDLE);
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(underlying));
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE));

    delete_VkImage(boxedNull);
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE));
}

TEST(BoxedHandleManagerReverseMapTest, DelayedRemoveClearsReverseMapping) {
    VkDevice device = reinterpret_cast<VkDevice>(0x1111'0000);
    VkSampler underlying = reinterpret_cast<VkSampler>(0x8888'0000);

    VkSampler boxed = new_boxed_non_dispatchable_VkSampler(underlying);
    EXPECT_EQ(boxed, unboxed_to_boxed_non_dispatchable_VkSampler(underlying));

    delayed_delete_VkSampler(boxed, device, nullptr);
    // Still alive until the delayed removes for the device are processed.
    EXPECT_EQ(boxed, unboxed_to_boxed_non_dispatchable_VkSampler(underlying));

    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxed)));
    EXPECT_EQ(VK_NULL_HANDLE, unboxed_to_boxed_non_dispatchable_VkSampler(underlying));
}

TEST_F(VkDecoderGlobalStateProcessCleanupLeakTest, DestroyCommandPoolDeletesBoxedCommandBuffers) {
    createInstanceAndDevice();
    const uint64_t baselineHandles = sBoxedHandleManager.getHandlesCount();

    const VkCommandPoolCreateInfo poolCi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0,
    };
    VkCommandPool boxedPool = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateCommandPool(&mPool, kInvalidSnapshotApiCallHandle,
                                                         mBoxedDevice, &poolCi, nullptr,
                                                         &boxedPool));
    ASSERT_NE(VK_NULL_HANDLE, boxedPool);
    VkCommandPool unboxedPool = unbox_VkCommandPool(boxedPool);

    const VkCommandBufferAllocateInfo cmdAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = unboxedPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer boxedCmdBuf = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS,
              mState->on_vkAllocateCommandBuffers(&mPool, kInvalidSnapshotApiCallHandle,
                                                  mBoxedDevice, &cmdAllocInfo, &boxedCmdBuf));
    ASSERT_NE(VK_NULL_HANDLE, boxedCmdBuf);

    BoxedHandleInfo* cmdBufBoxedInfo =
        sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedCmdBuf));
    ASSERT_NE(nullptr, cmdBufBoxedInfo);
    cmdBufBoxedInfo->readStream =
        std::make_unique<VulkanMemReadingStream>(nullptr, gfxstream::host::FeatureSet{});
    std::shared_ptr<OrderMaintenanceInfo> cmdBufOrder = ordmaint_VkCommandBuffer(boxedCmdBuf);
    ASSERT_NE(nullptr, cmdBufOrder);
    EXPECT_EQ(2, cmdBufOrder.use_count());

    mState->on_vkDestroyCommandPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                    unboxedPool, nullptr);
    delete_VkCommandPool(boxedPool);

    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedCmdBuf)));
    EXPECT_EQ(1, cmdBufOrder.use_count());
    EXPECT_EQ(baselineHandles, sBoxedHandleManager.getHandlesCount());
}

TEST_F(VkDecoderGlobalStateProcessCleanupLeakTest, DeviceCleanupDeletesAllExtractedBoxedHandles) {
    createInstanceAndDevice();

    // Baseline is after instance + physical device, before mBoxedDevice was created.
    // mBoxedDevice + mBoxedQueue are 2 handles owned by the device.
    const uint64_t baselineBeforeDevice = sBoxedHandleManager.getHandlesCount() - 2;

    const VkCommandPoolCreateInfo cmdPoolCi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0,
    };
    VkCommandPool boxedCmdPool = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateCommandPool(&mPool, kInvalidSnapshotApiCallHandle,
                                                         mBoxedDevice, &cmdPoolCi, nullptr,
                                                         &boxedCmdPool));
    const VkCommandBufferAllocateInfo cmdAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = unbox_VkCommandPool(boxedCmdPool),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer boxedCmdBuf = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS,
              mState->on_vkAllocateCommandBuffers(&mPool, kInvalidSnapshotApiCallHandle,
                                                  mBoxedDevice, &cmdAllocInfo, &boxedCmdBuf));
    BoxedHandleInfo* cmdBufBoxedInfo =
        sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedCmdBuf));
    ASSERT_NE(nullptr, cmdBufBoxedInfo);
    cmdBufBoxedInfo->readStream =
        std::make_unique<VulkanMemReadingStream>(nullptr, gfxstream::host::FeatureSet{});
    std::shared_ptr<OrderMaintenanceInfo> cmdBufOrder = ordmaint_VkCommandBuffer(boxedCmdBuf);
    ASSERT_NE(nullptr, cmdBufOrder);

    const VkBufferCreateInfo bufCi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 256,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer boxedBuffer = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateBuffer(&mPool, kInvalidSnapshotApiCallHandle,
                                                    mBoxedDevice, &bufCi, nullptr, &boxedBuffer));

    const VkMemoryAllocateInfo memAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = 256,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory boxedMem = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkAllocateMemory(&mPool, kInvalidSnapshotApiCallHandle,
                                                      mBoxedDevice, &memAllocInfo, nullptr,
                                                      &boxedMem));

    VkDescriptorPool boxedDescPool = createDescriptorPool(/*maxSets=*/1);
    ASSERT_NE(VK_NULL_HANDLE, boxedDescPool);
    VkDescriptorSetLayout boxedSetLayout = createDescriptorSetLayout();
    ASSERT_NE(VK_NULL_HANDLE, boxedSetLayout);
    VkDescriptorSetLayout unboxedSetLayout = unbox_VkDescriptorSetLayout(boxedSetLayout);

    const VkPipelineLayoutCreateInfo layoutCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &unboxedSetLayout,
    };
    VkPipelineLayout boxedLayout = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreatePipelineLayout(&mPool, kInvalidSnapshotApiCallHandle,
                                                            mBoxedDevice, &layoutCi, nullptr,
                                                            &boxedLayout));

    const VkDescriptorUpdateTemplateEntry templateEntry = {
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .offset = 0,
        .stride = sizeof(VkDescriptorBufferInfo),
    };
    const VkDescriptorUpdateTemplateCreateInfo templateCi = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = 1,
        .pDescriptorUpdateEntries = &templateEntry,
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        .descriptorSetLayout = unboxedSetLayout,
    };
    VkDescriptorUpdateTemplate boxedTemplate = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateDescriptorUpdateTemplate(
                              &mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, &templateCi,
                              nullptr, &boxedTemplate));

    const VkSamplerYcbcrConversionCreateInfo ycbcrCi = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
        .ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601,
        .ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
        .yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
        .chromaFilter = VK_FILTER_NEAREST,
        .forceExplicitReconstruction = VK_FALSE,
    };
    VkSamplerYcbcrConversion boxedYcbcr = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateSamplerYcbcrConversion(
                              &mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, &ycbcrCi,
                              nullptr, &boxedYcbcr));

    const VkShaderModuleCreateInfo shaderCi = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(kEmptyComputeShader),
        .pCode = kEmptyComputeShader,
    };
    VkShaderModule boxedShader = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateShaderModule(&mPool, kInvalidSnapshotApiCallHandle,
                                                          mBoxedDevice, &shaderCi, nullptr,
                                                          &boxedShader));

    const VkPipelineCacheCreateInfo cacheCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    VkPipelineCache boxedCache = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreatePipelineCache(&mPool, kInvalidSnapshotApiCallHandle,
                                                           mBoxedDevice, &cacheCi, nullptr,
                                                           &boxedCache));

    const VkComputePipelineCreateInfo pipelineCi = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = unbox_VkShaderModule(boxedShader),
                .pName = "main",
            },
        .layout = unbox_VkPipelineLayout(boxedLayout),
    };
    VkPipeline boxedPipeline = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS,
              mState->on_vkCreateComputePipelines(&mPool, kInvalidSnapshotApiCallHandle,
                                                  mBoxedDevice, unbox_VkPipelineCache(boxedCache),
                                                  1, &pipelineCi, nullptr, &boxedPipeline));

    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };
    const VkRenderPassCreateInfo rpCi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass boxedRp = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateRenderPass(&mPool, kInvalidSnapshotApiCallHandle,
                                                        mBoxedDevice, &rpCi, nullptr, &boxedRp));

    const VkFramebufferCreateInfo fbCi = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = unbox_VkRenderPass(boxedRp),
        .width = 1,
        .height = 1,
        .layers = 1,
    };
    VkFramebuffer boxedFb = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateFramebuffer(&mPool, kInvalidSnapshotApiCallHandle,
                                                         mBoxedDevice, &fbCi, nullptr, &boxedFb));

    // Destroy the device directly without destroying its child objects first;
    // extractDeviceAndDependenciesLocked + destroyDeviceObjects must release
    // every extracted child object and its boxed handle.
    VkDevice destroyedBoxedDevice = mBoxedDevice;
    mState->on_vkDestroyDevice(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, nullptr);
    mBoxedDevice = VK_NULL_HANDLE;

    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(destroyedBoxedDevice)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedCmdPool)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedCmdBuf)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedBuffer)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedMem)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedDescPool)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedSetLayout)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedTemplate)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedYcbcr)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedShader)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedPipeline)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedCache)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedLayout)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedFb)));
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedRp)));
    EXPECT_EQ(1, cmdBufOrder.use_count());
    EXPECT_EQ(baselineBeforeDevice, sBoxedHandleManager.getHandlesCount());
}

TEST_F(VkDecoderGlobalStateProcessCleanupLeakTest,
       EnumeratePhysicalDevicesReturnsInstanceOwnedHandlesUntilInstanceDestroyed) {
    const uint64_t baselineHandles = sBoxedHandleManager.getHandlesCount();

    const VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "EnumeratePhysicalDevicesTest",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "gfxstream",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instanceCi = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    };
    VkInstance boxedInstance = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS, mState->on_vkCreateInstance(&mPool, kInvalidSnapshotApiCallHandle,
                                                      &instanceCi, nullptr, &boxedInstance));
    ASSERT_NE(VK_NULL_HANDLE, boxedInstance);

    // Count-only query reports the physical devices without writing handles.
    uint32_t totalCount = 0;
    EXPECT_EQ(VK_SUCCESS,
              mState->on_vkEnumeratePhysicalDevices(&mPool, kInvalidSnapshotApiCallHandle,
                                                    boxedInstance, &totalCount, nullptr));
    ASSERT_GE(totalCount, 1u);

    // An array that is too small gets only as many handles as it can hold,
    // the count is updated to the number written and VK_INCOMPLETE returned.
    VkPhysicalDevice sentinel = reinterpret_cast<VkPhysicalDevice>(0xdeadbeefULL);
    uint32_t count = 0;
    EXPECT_EQ(VK_INCOMPLETE,
              mState->on_vkEnumeratePhysicalDevices(&mPool, kInvalidSnapshotApiCallHandle,
                                                    boxedInstance, &count, &sentinel));
    EXPECT_EQ(0u, count);
    EXPECT_EQ(reinterpret_cast<VkPhysicalDevice>(0xdeadbeefULL), sentinel);

    std::vector<VkPhysicalDevice> firstPass(totalCount, VK_NULL_HANDLE);
    count = totalCount;
    EXPECT_EQ(VK_SUCCESS,
              mState->on_vkEnumeratePhysicalDevices(&mPool, kInvalidSnapshotApiCallHandle,
                                                    boxedInstance, &count, firstPass.data()));
    EXPECT_EQ(totalCount, count);

    // Repeated enumeration on the same instance returns the same boxed
    // handles and does not box new ones.
    for (int i = 0; i < 2; ++i) {
        std::vector<VkPhysicalDevice> out(totalCount, VK_NULL_HANDLE);
        count = totalCount;
        EXPECT_EQ(VK_SUCCESS,
                  mState->on_vkEnumeratePhysicalDevices(&mPool, kInvalidSnapshotApiCallHandle,
                                                        boxedInstance, &count, out.data()));
        EXPECT_EQ(totalCount, count);
        EXPECT_EQ(firstPass, out);
    }
    EXPECT_EQ(baselineHandles + 1 + totalCount, sBoxedHandleManager.getHandlesCount());

    // Destroying the instance releases its physical devices with it.
    mState->on_vkDestroyInstance(&mPool, kInvalidSnapshotApiCallHandle, boxedInstance, nullptr);

    for (VkPhysicalDevice boxedPhysicalDevice : firstPass) {
        EXPECT_EQ(nullptr,
                  sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedPhysicalDevice)));
    }
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(reinterpret_cast<uint64_t>(boxedInstance)));
    EXPECT_EQ(baselineHandles, sBoxedHandleManager.getHandlesCount());
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
