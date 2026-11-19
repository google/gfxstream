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
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDescriptorSet>::fromRaw(kBogusPoolId)));

    VkDescriptorPool unboxedPool = unbox_VkDescriptorPool(Boxed<VkDescriptorPool>(boxedPool));
    VkDescriptorSetLayout unboxedSetLayout =
        unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(boxedSetLayout));
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
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDescriptorSet>::fromRaw(kBogusPoolId)));

    mState->on_vkDestroyDescriptorSetLayout(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                            unboxedSetLayout, nullptr);
    delete_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(boxedSetLayout));
    mState->on_vkDestroyDescriptorPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                       unboxedPool, nullptr);
    delete_VkDescriptorPool(Boxed<VkDescriptorPool>(boxedPool));
}

TEST_F(VkDecoderGlobalStateDescriptorSetPoolIdTest, UnallocatedAndAllocatedPoolIdHandling) {
    createInstanceAndDevice();

    VkDescriptorPool boxedPool = createDescriptorPool(/*maxSets=*/1);
    ASSERT_NE(VK_NULL_HANDLE, boxedPool);
    VkDescriptorSetLayout boxedSetLayout = createDescriptorSetLayout();
    ASSERT_NE(VK_NULL_HANDLE, boxedSetLayout);

    VkDescriptorPool unboxedPool = unbox_VkDescriptorPool(Boxed<VkDescriptorPool>(boxedPool));
    VkDescriptorSetLayout unboxedSetLayout =
        unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(boxedSetLayout));

    uint32_t poolIdCount = 1;
    uint64_t poolId = 0;
    mState->on_vkCollectDescriptorPoolIdsGOOGLE(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                                unboxedPool, &poolIdCount, &poolId);
    ASSERT_EQ(1u, poolIdCount);
    const Boxed<VkDescriptorSet> boxedSet = Boxed<VkDescriptorSet>::fromRaw(poolId);
    std::optional<UnboxedHandle> initialUnderlying = sBoxedHandleManager.getUnderlying(boxedSet);
    ASSERT_TRUE(initialUnderlying.has_value());
    EXPECT_EQ(0u, toRaw(*initialUnderlying));

    // Referencing an unallocated preallocated poolId with pendingAlloc=0 must not allocate.
    uint32_t whichPool = 0;
    uint32_t pendingAlloc = 0;
    uint32_t writeStartIndex = 0;
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &poolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/0, nullptr);
    EXPECT_EQ(UnboxedHandle{0}, sBoxedHandleManager.getUnderlying(boxedSet));

    // Committing with pendingAlloc=1 allocates the underlying host VkDescriptorSet.
    pendingAlloc = 1;
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &poolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/0, nullptr);
    std::optional<UnboxedHandle> allocatedUnderlying = sBoxedHandleManager.getUnderlying(boxedSet);
    ASSERT_TRUE(allocatedUnderlying.has_value());
    EXPECT_NE(0u, toRaw(*allocatedUnderlying));

    // Subsequent commits with pendingAlloc=0 reuse the already-allocated underlying handle.
    pendingAlloc = 0;
    mState->on_vkQueueCommitDescriptorSetUpdatesGOOGLE(
        &mPool, kInvalidSnapshotApiCallHandle, mBoxedQueue,
        /*descriptorPoolCount=*/1, &unboxedPool,
        /*descriptorSetCount=*/1, &unboxedSetLayout, &poolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/0, nullptr);
    EXPECT_EQ(allocatedUnderlying, sBoxedHandleManager.getUnderlying(boxedSet));

    mState->on_vkDestroyDescriptorSetLayout(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                            unboxedSetLayout, nullptr);
    delete_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(boxedSetLayout));
    mState->on_vkDestroyDescriptorPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                       unboxedPool, nullptr);
    delete_VkDescriptorPool(Boxed<VkDescriptorPool>(boxedPool));
}

TEST_F(VkDecoderGlobalStateDescriptorSetPoolIdTest, CollectDescriptorPoolIdsClampsCount) {
    createInstanceAndDevice();

    VkDescriptorPool boxedPool = createDescriptorPool(/*maxSets=*/4);
    ASSERT_NE(VK_NULL_HANDLE, boxedPool);
    VkDescriptorPool unboxedPool = unbox_VkDescriptorPool(Boxed<VkDescriptorPool>(boxedPool));

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
    delete_VkDescriptorPool(Boxed<VkDescriptorPool>(boxedPool));
}

TEST(VkDecoderGlobalStateBoxedHandleTest, OrderMaintenanceInfoOutlivesDeletedHandle) {
    Boxed<VkCommandBuffer> boxed =
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
            Boxed<VkCommandBuffer> boxed =
                Boxed<VkCommandBuffer>::fromRaw(currentHandle.load(std::memory_order_acquire));
            std::shared_ptr<OrderMaintenanceInfo> order = ordmaint_VkCommandBuffer(boxed);
            if (order) {
                EXPECT_EQ(0u, __atomic_load_n(&order->sequenceNumber, __ATOMIC_ACQUIRE));
            }
        }
    });

    for (int i = 0; i < kIterations; ++i) {
        Boxed<VkCommandBuffer> boxed =
            new_boxed_VkCommandBuffer(reinterpret_cast<VkCommandBuffer>(0x5555'0000), nullptr);
        currentHandle.store(boxed.toRaw(), std::memory_order_release);
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

    Boxed<VkImage> boxedOld = new_boxed_non_dispatchable_VkImage(underlying);
    ASSERT_NE(Boxed<VkImage>{}, boxedOld);
    EXPECT_EQ(boxedOld, unboxed_to_boxed_non_dispatchable_VkImage(underlying));

    // Host destroys the old image and immediately reuses the handle value for a new image, whose
    // boxed handle gets registered before the old boxed handle is deleted.
    Boxed<VkImage> boxedNew = new_boxed_non_dispatchable_VkImage(underlying);
    ASSERT_NE(Boxed<VkImage>{}, boxedNew);
    ASSERT_NE(boxedOld, boxedNew);
    EXPECT_EQ(boxedNew, unboxed_to_boxed_non_dispatchable_VkImage(underlying));

    delete_VkImage(boxedOld);
    EXPECT_FALSE(sBoxedHandleManager.isLive(boxedOld));
    // The new boxed handle must still be reachable from the (reused) unboxed handle.
    EXPECT_EQ(boxedNew, unboxed_to_boxed_non_dispatchable_VkImage(underlying));

    delete_VkImage(boxedNew);
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(underlying));
}

TEST(BoxedHandleManagerReverseMapTest, UpdateOldUnboxedDoesNotDropReusedMapping) {
    VkImage underlyingA = reinterpret_cast<VkImage>(0x6666'0000);
    VkImage underlyingB = reinterpret_cast<VkImage>(0x7777'0000);

    Boxed<VkImage> boxed1 = new_boxed_non_dispatchable_VkImage(underlyingA);
    // Another boxed handle takes over underlyingA (reuse) before boxed1 is re-pointed.
    Boxed<VkImage> boxed2 = new_boxed_non_dispatchable_VkImage(underlyingA);
    EXPECT_EQ(boxed2, unboxed_to_boxed_non_dispatchable_VkImage(underlyingA));

    set_boxed_non_dispatchable_VkImage(boxed1, underlyingB);
    EXPECT_EQ(boxed1, unboxed_to_boxed_non_dispatchable_VkImage(underlyingB));
    // boxed2's mapping for underlyingA must survive boxed1's update.
    EXPECT_EQ(boxed2, unboxed_to_boxed_non_dispatchable_VkImage(underlyingA));

    delete_VkImage(boxed1);
    delete_VkImage(boxed2);
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(underlyingA));
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(underlyingB));
}

TEST(BoxedHandleManagerReverseMapTest, NullUnboxedHandleIsNeverReverseMapped) {
    // Boxing VK_NULL_HANDLE must not register key 0 in the reverse map: it could never be erased
    // (eraseReverseMappingLocked skips 0) and would make lookups of VK_NULL_HANDLE return a
    // stale boxed handle.
    Boxed<VkImage> boxedNull = new_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE);
    ASSERT_NE(Boxed<VkImage>{}, boxedNull);
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE));

    VkImage underlying = reinterpret_cast<VkImage>(0x9999'0000);
    set_boxed_non_dispatchable_VkImage(boxedNull, underlying);
    EXPECT_EQ(boxedNull, unboxed_to_boxed_non_dispatchable_VkImage(underlying));
    set_boxed_non_dispatchable_VkImage(boxedNull, VK_NULL_HANDLE);
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(underlying));
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE));

    delete_VkImage(boxedNull);
    EXPECT_EQ(Boxed<VkImage>{}, unboxed_to_boxed_non_dispatchable_VkImage(VK_NULL_HANDLE));
}

TEST(BoxedHandleManagerReverseMapTest, DelayedRemoveClearsReverseMapping) {
    VkDevice device = reinterpret_cast<VkDevice>(0x1111'0000);
    VkSampler underlying = reinterpret_cast<VkSampler>(0x8888'0000);

    Boxed<VkSampler> boxed = new_boxed_non_dispatchable_VkSampler(underlying);
    EXPECT_EQ(boxed, unboxed_to_boxed_non_dispatchable_VkSampler(underlying));

    delayed_delete_VkSampler(boxed, device, nullptr);
    // Still alive until the delayed removes for the device are processed.
    EXPECT_EQ(boxed, unboxed_to_boxed_non_dispatchable_VkSampler(underlying));

    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_FALSE(sBoxedHandleManager.isLive(boxed));
    EXPECT_EQ(Boxed<VkSampler>{}, unboxed_to_boxed_non_dispatchable_VkSampler(underlying));
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
    VkCommandPool unboxedPool = unbox_VkCommandPool(Boxed<VkCommandPool>(boxedPool));

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

    ASSERT_NE(nullptr, sBoxedHandleManager.getOrCreateReadStream(
                           Boxed<VkCommandBuffer>(boxedCmdBuf), gfxstream::host::FeatureSet{}));
    std::shared_ptr<OrderMaintenanceInfo> cmdBufOrder =
        ordmaint_VkCommandBuffer(Boxed<VkCommandBuffer>(boxedCmdBuf));
    ASSERT_NE(nullptr, cmdBufOrder);
    EXPECT_EQ(2, cmdBufOrder.use_count());

    mState->on_vkDestroyCommandPool(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                    unboxedPool, nullptr);
    delete_VkCommandPool(Boxed<VkCommandPool>(boxedPool));

    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkCommandBuffer>(boxedCmdBuf)));
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
        .commandPool = unbox_VkCommandPool(Boxed<VkCommandPool>(boxedCmdPool)),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer boxedCmdBuf = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS,
              mState->on_vkAllocateCommandBuffers(&mPool, kInvalidSnapshotApiCallHandle,
                                                  mBoxedDevice, &cmdAllocInfo, &boxedCmdBuf));
    ASSERT_NE(nullptr, sBoxedHandleManager.getOrCreateReadStream(
                           Boxed<VkCommandBuffer>(boxedCmdBuf), gfxstream::host::FeatureSet{}));
    std::shared_ptr<OrderMaintenanceInfo> cmdBufOrder =
        ordmaint_VkCommandBuffer(Boxed<VkCommandBuffer>(boxedCmdBuf));
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
    VkDescriptorSetLayout unboxedSetLayout =
        unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(boxedSetLayout));

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
                .module = unbox_VkShaderModule(Boxed<VkShaderModule>(boxedShader)),
                .pName = "main",
            },
        .layout = unbox_VkPipelineLayout(Boxed<VkPipelineLayout>(boxedLayout)),
    };
    VkPipeline boxedPipeline = VK_NULL_HANDLE;
    ASSERT_EQ(VK_SUCCESS,
              mState->on_vkCreateComputePipelines(
                  &mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                  unbox_VkPipelineCache(Boxed<VkPipelineCache>(boxedCache)), 1, &pipelineCi,
                  nullptr, &boxedPipeline));

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
        .renderPass = unbox_VkRenderPass(Boxed<VkRenderPass>(boxedRp)),
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

    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDevice>(destroyedBoxedDevice)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkCommandPool>(boxedCmdPool)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkCommandBuffer>(boxedCmdBuf)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkBuffer>(boxedBuffer)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDeviceMemory>(boxedMem)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDescriptorPool>(boxedDescPool)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDescriptorSetLayout>(boxedSetLayout)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkDescriptorUpdateTemplate>(boxedTemplate)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkSamplerYcbcrConversion>(boxedYcbcr)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkShaderModule>(boxedShader)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkPipeline>(boxedPipeline)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkPipelineCache>(boxedCache)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkPipelineLayout>(boxedLayout)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkFramebuffer>(boxedFb)));
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkRenderPass>(boxedRp)));
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
        EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkPhysicalDevice>(boxedPhysicalDevice)));
    }
    EXPECT_FALSE(sBoxedHandleManager.isLive(Boxed<VkInstance>(boxedInstance)));
    EXPECT_EQ(baselineHandles, sBoxedHandleManager.getHandlesCount());
}

class VkDecoderGlobalStateSyncFdFenceTest : public VkDecoderGlobalStateTest {
   protected:
    static constexpr uint64_t kWaitTimeoutNs = 1000ULL * 1000ULL * 1000ULL;

    void SetUp() override {
        mBaselineHandles = sBoxedHandleManager.getHandlesCount();
        VkDecoderGlobalStateTest::SetUp();
        createInstanceAndDevice();
        mUnboxedDev = unbox_VkDevice(Boxed<VkDevice>(mBoxedDevice));

        VulkanDispatch* deviceVk = dispatch_VkDevice(Boxed<VkDevice>(mBoxedDevice));
        ASSERT_NE(deviceVk, nullptr);
        {
            std::lock_guard<std::mutex> lock(sFakeMutex);
            sNextFence = 0xf100'0000ULL;
            sSignaledFences.clear();
            sWaitedFences.clear();
            sDestroyedFences.clear();
            sDestroyedDevices.clear();
            sFakeWaitHook = nullptr;
            sRealDestroyDevice =
                deviceVk->vkDestroyDevice ? deviceVk->vkDestroyDevice : mVk->vkDestroyDevice;
        }

        deviceVk->vkDestroyDevice = &fakeDestroyDevice;
        deviceVk->vkCreateFence = &fakeCreateFence;
        deviceVk->vkDestroyFence = &fakeDestroyFence;
        deviceVk->vkResetFences = &fakeResetFences;
        deviceVk->vkGetFenceStatus = &fakeGetFenceStatus;
        deviceVk->vkWaitForFences = &fakeWaitForFences;
        deviceVk->vkQueueSubmit = &fakeQueueSubmit;
    }

    void TearDown() override {
        VkDecoderGlobalStateTest::TearDown();
        {
            std::lock_guard<std::mutex> lock(sFakeMutex);
            sRealDestroyDevice = nullptr;
        }
        EXPECT_EQ(mBaselineHandles, sBoxedHandleManager.getHandlesCount());
    }

    Boxed<VkFence> createFence(bool exportable, bool signaled) {
        VkExportFenceCreateInfo exportInfo = {
            .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
        };
        VkFenceCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = exportable ? &exportInfo : nullptr,
            .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
        };
        VkFence boxedFence = VK_NULL_HANDLE;
        EXPECT_EQ(VK_SUCCESS, mState->on_vkCreateFence(&mPool, kInvalidSnapshotApiCallHandle,
                                                       mBoxedDevice, &createInfo, nullptr,
                                                       &boxedFence));
        return Boxed<VkFence>(boxedFence);
    }

    // Runs only on_vkDestroyFence, leaving the boxed handle live until delete_VkFence() runs,
    // matching the window in OP_vkDestroyFence after mMutex is released.
    void destroyFenceWithoutDeletingBoxedHandle(Boxed<VkFence> boxedFence) {
        mState->on_vkDestroyFence(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice,
                                  try_unbox_VkFence(boxedFence), nullptr);
    }

    // Like the decoder, deletes the boxed handle after the fence is destroyed.
    void destroyFence(Boxed<VkFence> boxedFence) {
        destroyFenceWithoutDeletingBoxedHandle(boxedFence);
        delete_VkFence(boxedFence);
    }

    void destroyDevice() {
        mState->on_vkDestroyDevice(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, nullptr);
        mBoxedDevice = VK_NULL_HANDLE;
    }

    VkResult resetFence(Boxed<VkFence> boxedFence) {
        VkFence fence = unbox_VkFence(boxedFence);
        return mState->on_vkResetFences(&mPool, kInvalidSnapshotApiCallHandle, mBoxedDevice, 1,
                                        &fence);
    }

    void markFenceSubmitted(Boxed<VkFence> boxedFence) {
        VkFence fence = unbox_VkFence(boxedFence);
        signalFence(fence);
        ASSERT_EQ(VK_SUCCESS, mState->on_vkQueueSubmit(&mPool, kInvalidSnapshotApiCallHandle,
                                                       mBoxedQueue, 0, nullptr, fence));
    }

    VkDecoderGlobalState::SyncFdFence acquireSyncFdFence(Boxed<VkFence> boxedFence) {
        return mState->acquireSyncFdFence(boxedFence);
    }

    VkResult waitForSyncFdFence(const VkDecoderGlobalState::SyncFdFence& fence, uint64_t timeout) {
        return mState->waitForSyncFdFence(fence, timeout);
    }

    // Waits on another thread, to fail instead of hanging if the wait does not return.
    VkResult waitWithDeadline(const VkDecoderGlobalState::SyncFdFence& fence, uint64_t timeoutNs) {
        std::promise<VkResult> promise;
        std::future<VkResult> result = promise.get_future();
        std::thread waiter([&] { promise.set_value(waitForSyncFdFence(fence, timeoutNs)); });
        if (result.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
            GFXSTREAM_FATAL("waitForSyncFdFence() did not return.");
        }
        waiter.join();
        return result.get();
    }

    static void signalFence(VkFence fence) {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        sSignaledFences.insert(fence);
    }

    static void setFakeWaitHook(std::function<VkResult(VkFence)> hook) {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        sFakeWaitHook = std::move(hook);
    }

    static std::vector<VkFence> waitedFences() {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        return sWaitedFences;
    }

    static std::vector<VkFence> destroyedFences() {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        return sDestroyedFences;
    }

    static std::vector<VkDevice> destroyedDevices() {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        return sDestroyedDevices;
    }

    static void fakeDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
        PFN_vkDestroyDevice realDestroy = nullptr;
        {
            std::lock_guard<std::mutex> lock(sFakeMutex);
            sDestroyedDevices.push_back(device);
            realDestroy = sRealDestroyDevice;
        }
        if (realDestroy) {
            realDestroy(device, pAllocator);
        }
    }

    static VkResult fakeCreateFence(VkDevice, const VkFenceCreateInfo* pCreateInfo,
                                    const VkAllocationCallbacks*, VkFence* pFence) {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        *pFence = reinterpret_cast<VkFence>(sNextFence++);
        if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) {
            sSignaledFences.insert(*pFence);
        }
        return VK_SUCCESS;
    }

    static void fakeDestroyFence(VkDevice, VkFence fence, const VkAllocationCallbacks*) {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        sSignaledFences.erase(fence);
        sDestroyedFences.push_back(fence);
    }

    static VkResult fakeResetFences(VkDevice, uint32_t fenceCount, const VkFence* pFences) {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        for (uint32_t i = 0; i < fenceCount; ++i) {
            sSignaledFences.erase(pFences[i]);
        }
        return VK_SUCCESS;
    }

    static VkResult fakeGetFenceStatus(VkDevice, VkFence fence) {
        std::lock_guard<std::mutex> lock(sFakeMutex);
        return sSignaledFences.count(fence) ? VK_SUCCESS : VK_NOT_READY;
    }

    static VkResult fakeWaitForFences(VkDevice, uint32_t fenceCount, const VkFence* pFences,
                                      VkBool32, uint64_t) {
        std::function<VkResult(VkFence)> waitHook;
        {
            std::lock_guard<std::mutex> lock(sFakeMutex);
            for (uint32_t i = 0; i < fenceCount; ++i) {
                sWaitedFences.push_back(pFences[i]);
            }
            waitHook = sFakeWaitHook;
        }
        if (waitHook && fenceCount > 0) {
            return waitHook(pFences[0]);
        }
        return VK_SUCCESS;
    }

    static VkResult fakeQueueSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence) {
        return VK_SUCCESS;
    }

    uint64_t mBaselineHandles = 0;
    VkDevice mUnboxedDev = VK_NULL_HANDLE;

    static inline std::mutex sFakeMutex;
    static inline uint64_t sNextFence = 0xf100'0000ULL;
    static inline std::unordered_set<VkFence> sSignaledFences;
    static inline std::vector<VkFence> sWaitedFences;
    static inline std::vector<VkFence> sDestroyedFences;
    static inline std::vector<VkDevice> sDestroyedDevices;
    static inline std::function<VkResult(VkFence)> sFakeWaitHook;
    static inline PFN_vkDestroyDevice sRealDestroyDevice = nullptr;
};

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, ExportedFenceIsNotRecycledUntilItsWaitsComplete) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/true);
    const VkFence exportedFence = unbox_VkFence(boxedFence);

    // Exported twice before the guest destroys it, which puts it into the recycling pool.
    const auto syncFdFence1 = acquireSyncFdFence(boxedFence);
    const auto syncFdFence2 = acquireSyncFdFence(boxedFence);
    EXPECT_EQ(exportedFence, syncFdFence1.fence);
    EXPECT_EQ(exportedFence, syncFdFence2.fence);
    destroyFence(boxedFence);

    Boxed<VkFence> boxedFence2 = createFence(/*exportable=*/true, /*signaled=*/true);
    EXPECT_NE(exportedFence, unbox_VkFence(boxedFence2));

    EXPECT_EQ(VK_SUCCESS, waitWithDeadline(syncFdFence1, kWaitTimeoutNs));
    Boxed<VkFence> boxedFence3 = createFence(/*exportable=*/true, /*signaled=*/true);
    EXPECT_NE(exportedFence, unbox_VkFence(boxedFence3));

    EXPECT_EQ(VK_SUCCESS, waitWithDeadline(syncFdFence2, kWaitTimeoutNs));
    EXPECT_THAT(waitedFences(), ::testing::ElementsAre(exportedFence, exportedFence));

    Boxed<VkFence> boxedFence4 = createFence(/*exportable=*/true, /*signaled=*/true);
    EXPECT_EQ(exportedFence, unbox_VkFence(boxedFence4));

    destroyFence(boxedFence2);
    destroyFence(boxedFence3);
    destroyFence(boxedFence4);
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, WaitUsesExportedFenceWhenGuestResetsIt) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/false);
    const VkFence exportedFence = unbox_VkFence(boxedFence);
    markFenceSubmitted(boxedFence);

    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_EQ(exportedFence, syncFdFence.fence);

    // Resetting an exportable fence points its boxed handle at a replacement fence, which is not
    // waitable until it is submitted.
    ASSERT_EQ(VK_SUCCESS, resetFence(boxedFence));
    ASSERT_NE(exportedFence, unbox_VkFence(boxedFence));
    signalFence(exportedFence);

    EXPECT_EQ(VK_SUCCESS, waitWithDeadline(syncFdFence, kWaitTimeoutNs));
    EXPECT_THAT(waitedFences(), ::testing::ElementsAre(exportedFence));

    destroyFence(boxedFence);
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, WaitForFenceThatIsNeverSubmittedTimesOut) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/false);
    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_NE(VK_NULL_HANDLE, syncFdFence.fence);

    EXPECT_EQ(VK_TIMEOUT, waitWithDeadline(syncFdFence, /*timeoutNs=*/10'000'000));
    EXPECT_THAT(waitedFences(), ::testing::IsEmpty());

    destroyFence(boxedFence);
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, WaitStartsWhenFenceIsSubmittedAfterExport) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/false);
    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_NE(VK_NULL_HANDLE, syncFdFence.fence);

    // The export can be processed before the submission that signals the fence.
    std::promise<VkResult> promise;
    std::future<VkResult> result = promise.get_future();
    std::thread waiter(
        [&] { promise.set_value(waitForSyncFdFence(syncFdFence, 30 * kWaitTimeoutNs)); });
    EXPECT_EQ(std::future_status::timeout, result.wait_for(std::chrono::milliseconds(50)));

    markFenceSubmitted(boxedFence);
    EXPECT_EQ(std::future_status::ready, result.wait_for(std::chrono::seconds(10)));
    waiter.join();
    EXPECT_EQ(VK_SUCCESS, result.get());
    EXPECT_THAT(waitedFences(), ::testing::ElementsAre(syncFdFence.fence));

    destroyFence(boxedFence);
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, FencesThatCannotBeResolvedAreNotWaitedOn) {
    auto syncFdFence = acquireSyncFdFence(Boxed<VkFence>());
    EXPECT_EQ(VK_NULL_HANDLE, syncFdFence.fence);
    EXPECT_EQ(VK_SUCCESS, waitForSyncFdFence(syncFdFence, kWaitTimeoutNs));

    Boxed<VkFence> boxedFence = createFence(/*exportable=*/false, /*signaled=*/true);
    syncFdFence = acquireSyncFdFence(boxedFence);
    EXPECT_EQ(VK_NULL_HANDLE, syncFdFence.fence);
    EXPECT_EQ(VK_SUCCESS, waitForSyncFdFence(syncFdFence, kWaitTimeoutNs));
    destroyFence(boxedFence);

    // Destroyed by the guest before the export was processed.
    boxedFence = createFence(/*exportable=*/true, /*signaled=*/true);
    destroyFence(boxedFence);
    syncFdFence = acquireSyncFdFence(boxedFence);
    EXPECT_EQ(VK_NULL_HANDLE, syncFdFence.fence);
    EXPECT_EQ(VK_SUCCESS, waitForSyncFdFence(syncFdFence, kWaitTimeoutNs));

    EXPECT_THAT(waitedFences(), ::testing::IsEmpty());
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, PendingWaitSkipsFenceOfDestroyedDevice) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/true);
    const VkFence exportedFence = unbox_VkFence(boxedFence);
    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_EQ(exportedFence, syncFdFence.fence);
    destroyFence(boxedFence);

    destroyDevice();
    EXPECT_THAT(destroyedFences(), ::testing::Contains(exportedFence));

    EXPECT_EQ(VK_SUCCESS, waitForSyncFdFence(syncFdFence, kWaitTimeoutNs));
    EXPECT_THAT(waitedFences(), ::testing::IsEmpty());
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, ConcurrentWaitsWhileGuestRecyclesFences) {
    constexpr int kIterations = 200;
    constexpr int kWaiterCount = 2;

    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<VkDecoderGlobalState::SyncFdFence> queue;
    bool exportsDone = false;

    // Exported host fences whose wait has not started yet. Their pins are held for longer than
    // this, so new guest fences must never get one of them.
    std::mutex queuedMutex;
    std::unordered_map<VkFence, int> queuedFences;
    std::atomic<int> recycledWhileQueued{0};
    std::atomic<int> failedWaits{0};

    std::atomic<bool> start{false};
    std::vector<std::thread> waiters;
    for (int i = 0; i < kWaiterCount; ++i) {
        waiters.emplace_back([&] {
            while (!start.load()) {
                std::this_thread::yield();
            }
            while (true) {
                VkDecoderGlobalState::SyncFdFence syncFdFence;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    queueCv.wait(lock, [&] { return exportsDone || !queue.empty(); });
                    if (queue.empty()) {
                        return;
                    }
                    syncFdFence = queue.front();
                    queue.pop_front();
                }
                {
                    std::lock_guard<std::mutex> lock(queuedMutex);
                    if (--queuedFences[syncFdFence.fence] == 0) {
                        queuedFences.erase(syncFdFence.fence);
                    }
                }
                if (waitForSyncFdFence(syncFdFence, kWaitTimeoutNs) != VK_SUCCESS) {
                    ++failedWaits;
                }
            }
        });
    }

    start = true;
    for (int i = 0; i < kIterations; ++i) {
        Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/true);
        {
            std::lock_guard<std::mutex> lock(queuedMutex);
            if (queuedFences.count(unbox_VkFence(boxedFence))) {
                ++recycledWhileQueued;
            }
        }

        const auto syncFdFence = acquireSyncFdFence(boxedFence);
        EXPECT_NE(VK_NULL_HANDLE, syncFdFence.fence);
        {
            std::lock_guard<std::mutex> lock(queuedMutex);
            ++queuedFences[syncFdFence.fence];
        }
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            queue.push_back(syncFdFence);
        }
        queueCv.notify_one();

        if (i % 2) {
            // Reset, then resubmitted and completed before being destroyed.
            EXPECT_EQ(VK_SUCCESS, resetFence(boxedFence));
            {
                std::lock_guard<std::mutex> lock(queuedMutex);
                if (queuedFences.count(unbox_VkFence(boxedFence))) {
                    ++recycledWhileQueued;
                }
            }
            markFenceSubmitted(boxedFence);
            signalFence(unbox_VkFence(boxedFence));
        }
        destroyFence(boxedFence);
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        exportsDone = true;
    }
    queueCv.notify_all();
    for (auto& waiter : waiters) {
        waiter.join();
    }

    EXPECT_EQ(0, recycledWhileQueued.load());
    EXPECT_EQ(0, failedWaits.load());
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, DestroyedFenceCannotBeAcquiredBeforeBoxedHandleIsDeleted) {
    Boxed<VkFence> boxedFence1 = createFence(/*exportable=*/true, /*signaled=*/true);
    const VkFence hostFence = unbox_VkFence(boxedFence1);

    // OP_vkDestroyFence releases mMutex inside on_vkDestroyFence before calling delete_VkFence.
    destroyFenceWithoutDeletingBoxedHandle(boxedFence1);

    EXPECT_EQ(VK_NULL_HANDLE, acquireSyncFdFence(boxedFence1).fence);

    // Another thread can recycle hostFence for a new guest fence before boxedFence1 is deleted.
    Boxed<VkFence> boxedFence2 = createFence(/*exportable=*/true, /*signaled=*/true);
    EXPECT_EQ(hostFence, unbox_VkFence(boxedFence2));
    EXPECT_EQ(VK_NULL_HANDLE, acquireSyncFdFence(boxedFence1).fence);

    delete_VkFence(boxedFence1);
    EXPECT_EQ(boxedFence2, unboxed_to_boxed_non_dispatchable_VkFence(hostFence));

    const auto syncFdFence2 = acquireSyncFdFence(boxedFence2);
    EXPECT_EQ(hostFence, syncFdFence2.fence);
    EXPECT_EQ(VK_SUCCESS, waitWithDeadline(syncFdFence2, kWaitTimeoutNs));

    destroyFence(boxedFence2);
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, DeviceDestroyCancelsInFlightUnsubmittedFenceWait) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/false);
    const VkFence exportedFence = unbox_VkFence(boxedFence);
    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_EQ(exportedFence, syncFdFence.fence);

    std::promise<VkResult> promise;
    std::future<VkResult> result = promise.get_future();
    std::thread waiter(
        [&] { promise.set_value(waitForSyncFdFence(syncFdFence, 30 * kWaitTimeoutNs)); });
    EXPECT_EQ(std::future_status::timeout, result.wait_for(std::chrono::milliseconds(50)));

    destroyDevice();

    EXPECT_EQ(std::future_status::ready, result.wait_for(std::chrono::seconds(5)));
    waiter.join();
    EXPECT_EQ(VK_SUCCESS, result.get());
    EXPECT_THAT(waitedFences(), ::testing::IsEmpty());
    EXPECT_THAT(destroyedFences(), ::testing::Contains(exportedFence));
    EXPECT_THAT(destroyedDevices(), ::testing::ElementsAre(mUnboxedDev));
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, DeviceDestroyDrainsInFlightDriverFenceWait) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/true);
    const VkFence exportedFence = unbox_VkFence(boxedFence);
    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_EQ(exportedFence, syncFdFence.fence);

    std::promise<void> enteredDriverWait;
    std::future<void> enteredDriverWaitFuture = enteredDriverWait.get_future();
    std::promise<void> allowDriverWaitToReturn;
    std::shared_future<void> allowDriverWaitFuture = allowDriverWaitToReturn.get_future().share();
    setFakeWaitHook([&](VkFence) {
        enteredDriverWait.set_value();
        allowDriverWaitFuture.wait();
        return VK_SUCCESS;
    });

    std::promise<VkResult> waitResultPromise;
    std::future<VkResult> waitResult = waitResultPromise.get_future();
    std::thread waiter(
        [&] { waitResultPromise.set_value(waitForSyncFdFence(syncFdFence, kWaitTimeoutNs)); });
    EXPECT_EQ(std::future_status::ready,
              enteredDriverWaitFuture.wait_for(std::chrono::seconds(5)));

    const VkDevice boxedDevice = mBoxedDevice;
    std::promise<void> destroyDonePromise;
    std::future<void> destroyDone = destroyDonePromise.get_future();
    std::thread destroyer([&] {
        destroyDevice();
        destroyDonePromise.set_value();
    });

    // While vkWaitForFences is in flight, neither the fence, device, nor dispatch may be destroyed.
    EXPECT_EQ(std::future_status::timeout, destroyDone.wait_for(std::chrono::milliseconds(50)));
    EXPECT_THAT(destroyedFences(), ::testing::IsEmpty());
    EXPECT_THAT(destroyedDevices(), ::testing::IsEmpty());
    EXPECT_NE(nullptr, dispatch_VkDevice(Boxed<VkDevice>(boxedDevice)));

    allowDriverWaitToReturn.set_value();

    EXPECT_EQ(std::future_status::ready, waitResult.wait_for(std::chrono::seconds(5)));
    EXPECT_EQ(std::future_status::ready, destroyDone.wait_for(std::chrono::seconds(5)));
    waiter.join();
    destroyer.join();

    EXPECT_EQ(VK_SUCCESS, waitResult.get());
    EXPECT_THAT(destroyedFences(), ::testing::Contains(exportedFence));
    EXPECT_THAT(destroyedDevices(), ::testing::ElementsAre(mUnboxedDev));
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, DeviceDestroyCancelsStepPollingDriverFenceWait) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/false);
    const VkFence exportedFence = unbox_VkFence(boxedFence);
    markFenceSubmitted(boxedFence);
    const auto syncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_EQ(exportedFence, syncFdFence.fence);

    std::atomic<bool> enteredDriverWait{false};
    setFakeWaitHook([&](VkFence) {
        enteredDriverWait = true;
        return VK_TIMEOUT;
    });

    std::promise<VkResult> waitResultPromise;
    std::future<VkResult> waitResult = waitResultPromise.get_future();
    std::thread waiter([&] {
        waitResultPromise.set_value(waitForSyncFdFence(syncFdFence, 30 * kWaitTimeoutNs));
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!enteredDriverWait.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_TRUE(enteredDriverWait.load());

    destroyDevice();

    EXPECT_EQ(std::future_status::ready, waitResult.wait_for(std::chrono::seconds(5)));
    waiter.join();
    EXPECT_EQ(VK_SUCCESS, waitResult.get());
    EXPECT_THAT(destroyedFences(), ::testing::Contains(exportedFence));
    EXPECT_THAT(destroyedDevices(), ::testing::ElementsAre(mUnboxedDev));
}

TEST_F(VkDecoderGlobalStateSyncFdFenceTest, AcquireSyncFdFenceRejectsSemaphoreHandle) {
    Boxed<VkFence> boxedFence = createFence(/*exportable=*/true, /*signaled=*/true);
    const VkFence exportedFence = unbox_VkFence(boxedFence);

    // Positive control: the exportable fence is registered in mSyncFdFencePins and resolves.
    const auto validSyncFdFence = acquireSyncFdFence(boxedFence);
    ASSERT_EQ(exportedFence, validSyncFdFence.fence);
    EXPECT_EQ(VK_SUCCESS, waitWithDeadline(validSyncFdFence, kWaitTimeoutNs));

    // Mint a VkSemaphore sharing the exact same numeric host handle value as the still-pinned
    // VkFence (as can happen on index-based Vulkan drivers).
    Boxed<VkSemaphore> boxedSemaphore = new_boxed(reinterpret_cast<VkSemaphore>(exportedFence));
    EXPECT_EQ(boxedFence, unboxed_to_boxed(exportedFence));
    EXPECT_EQ(boxedSemaphore, unboxed_to_boxed(reinterpret_cast<VkSemaphore>(exportedFence)));

    // Passing the semaphore's raw boxed bits down the fence export path must be rejected by its
    // runtime type tag without acquiring a pin or queuing a wait.
    const auto wrongTypeSyncFdFence =
        acquireSyncFdFence(Boxed<VkFence>::fromRaw(boxedSemaphore.toRaw()));
    EXPECT_EQ(VK_NULL_HANDLE, wrongTypeSyncFdFence.fence);

    delete_boxed(boxedSemaphore);
    destroyFence(boxedFence);
}

TEST(BoxedHandleTypeTagValidationTest, RejectsCrossTypeLookupUpdateAndDelete) {
    VkDevice device = reinterpret_cast<VkDevice>(0x1234'0000ULL);
    VkBuffer unboxedBuffer = reinterpret_cast<VkBuffer>(0x4444'1111ULL);
    Boxed<VkBuffer> boxedBuffer = new_boxed(unboxedBuffer);
    ASSERT_TRUE(static_cast<bool>(boxedBuffer));

    // Reinterpret a live VkBuffer's raw handle bits as VkImage (simulating a cross-type handle
    // arriving on the wire via fromRaw).
    Boxed<VkImage> wrongTypeImage = Boxed<VkImage>::fromRaw(boxedBuffer.toRaw());
    EXPECT_FALSE(sBoxedHandleManager.isLive(wrongTypeImage));
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(wrongTypeImage));
    EXPECT_EQ(Boxed<VkImage>(), unboxed_to_boxed(reinterpret_cast<VkImage>(unboxedBuffer)));

    // Cross-type update, delete, and delayed delete must not corrupt or remove the live VkBuffer.
    set_boxed(wrongTypeImage, reinterpret_cast<VkImage>(0x9999'1111ULL));
    EXPECT_EQ(unboxedBuffer, unbox(boxedBuffer));

    delete_boxed(wrongTypeImage);
    EXPECT_EQ(unboxedBuffer, unbox(boxedBuffer));

    bool delayedCbCalled = false;
    delayed_delete_boxed(wrongTypeImage, device, [&]() { delayedCbCalled = true; });
    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_FALSE(delayedCbCalled);
    EXPECT_EQ(unboxedBuffer, unbox(boxedBuffer));

    // Dispatchable cross-type lookup, dispatch, and ordmaint must also be rejected.
    VulkanDispatch cmdVk = {};
    VkCommandBuffer unboxedCmd = reinterpret_cast<VkCommandBuffer>(0x4444'2222ULL);
    Boxed<VkCommandBuffer> boxedCmd = new_boxed(unboxedCmd, &cmdVk);
    Boxed<VkQueue> wrongTypeQueue = Boxed<VkQueue>::fromRaw(boxedCmd.toRaw());
    EXPECT_FALSE(sBoxedHandleManager.isLive(wrongTypeQueue));
    EXPECT_EQ(nullptr, sBoxedHandleManager.getDispatch(wrongTypeQueue));
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(wrongTypeQueue));
    EXPECT_EQ(nullptr, ordmaint(wrongTypeQueue));
    delete_boxed(wrongTypeQueue);
    EXPECT_EQ(unboxedCmd, unbox(boxedCmd));
    EXPECT_EQ(&cmdVk, dispatch(boxedCmd));
    EXPECT_NE(nullptr, ordmaint(boxedCmd));

    delete_boxed(boxedCmd);
    delete_boxed(boxedBuffer);
}

TEST(BoxedHandleTypeTagValidationTest, RejectsForgedTypeTagOnLiveSlot) {
    VkBuffer unboxedBuffer = reinterpret_cast<VkBuffer>(0x4444'3333ULL);
    Boxed<VkBuffer> boxedBuffer = new_boxed(unboxedBuffer);
    ASSERT_TRUE(static_cast<bool>(boxedBuffer));

    // Forge a handle with Tag_VkImage in the upper 16 bits pointing at boxedBuffer's live slot.
    const uint64_t forgedRaw =
        BoxedHandleManager::Store::EM::withType(boxedBuffer.toRaw(), Tag_VkImage);
    Boxed<VkImage> forgedImage = Boxed<VkImage>::fromRaw(forgedRaw);

    EXPECT_FALSE(sBoxedHandleManager.isLive(forgedImage));
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(forgedImage));

    set_boxed(forgedImage, reinterpret_cast<VkImage>(0x9999'3333ULL));
    EXPECT_EQ(unboxedBuffer, unbox(boxedBuffer));

    VkDevice device = reinterpret_cast<VkDevice>(0x1234'3333ULL);
    bool forgedLinearDelayedCalled = false;
    delayed_delete_boxed(forgedImage, device, [&]() { forgedLinearDelayedCalled = true; });
    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_FALSE(forgedLinearDelayedCalled);

    delete_boxed(forgedImage);
    EXPECT_EQ(unboxedBuffer, unbox(boxedBuffer));

    // If a live handle is queued for delayed delete and then deleted directly before
    // processDelayedRemoves runs, processDelayedRemoves must re-check liveness and skip the
    // callback.
    bool alreadyDeletedDelayedCalled = false;
    delayed_delete_boxed(boxedBuffer, device, [&]() { alreadyDeletedDelayedCalled = true; });
    delete_boxed(boxedBuffer);
    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_FALSE(alreadyDeletedDelayedCalled);

    // Dead handle passed to delayed_delete_boxed must also not run the callback.
    bool deadDelayedCalled = false;
    delayed_delete_boxed(boxedBuffer, device, [&]() { deadDelayedCalled = true; });
    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_FALSE(deadDelayedCalled);

    // Overflow-map slot (index >= 16000) where HybridEntityManager::mMap keys only on index_u64,
    // exercising BoxedHandleInfo::tag checks directly.
    const uint64_t overflowBufferRaw =
        BoxedHandleManager::Store::EM::makeHandle(16000, /*generation=*/1, Tag_VkBuffer);
    sBoxedHandleManager.replayHandles({overflowBufferRaw});
    VkBuffer overflowUnboxedBuffer = reinterpret_cast<VkBuffer>(0x4444'3334ULL);
    Boxed<VkBuffer> overflowBoxedBuffer = new_boxed(overflowUnboxedBuffer);
    ASSERT_EQ(overflowBufferRaw, overflowBoxedBuffer.toRaw());

    const uint64_t forgedOverflowRaw =
        BoxedHandleManager::Store::EM::withType(overflowBoxedBuffer.toRaw(), Tag_VkImage);
    Boxed<VkImage> forgedOverflowImage = Boxed<VkImage>::fromRaw(forgedOverflowRaw);
    const Boxed<VkQueue> forgedOverflowQueue = Boxed<VkQueue>::fromRaw(
        BoxedHandleManager::Store::EM::withType(overflowBoxedBuffer.toRaw(), Tag_VkQueue));

    EXPECT_FALSE(sBoxedHandleManager.isLive(forgedOverflowImage));
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(forgedOverflowImage));
    EXPECT_EQ(nullptr, sBoxedHandleManager.getDispatch(forgedOverflowQueue));
    EXPECT_EQ(nullptr, ordmaint(forgedOverflowQueue));

    set_boxed(forgedOverflowImage, reinterpret_cast<VkImage>(0x9999'3334ULL));
    EXPECT_EQ(overflowUnboxedBuffer, unbox(overflowBoxedBuffer));

    bool forgedOverflowDelayedCalled = false;
    delayed_delete_boxed(forgedOverflowImage, device,
                         [&]() { forgedOverflowDelayedCalled = true; });
    sBoxedHandleManager.processDelayedRemoves(device);
    EXPECT_FALSE(forgedOverflowDelayedCalled);

    delete_boxed(forgedOverflowImage);
    EXPECT_EQ(overflowUnboxedBuffer, unbox(overflowBoxedBuffer));

    delete_boxed(overflowBoxedBuffer);
    EXPECT_FALSE(sBoxedHandleManager.isLive(overflowBoxedBuffer));
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(overflowBoxedBuffer));
}

TEST(BoxedHandleTypeTagValidationTest, ConcurrentLookupUpdateAndDeleteLinearAndOverflow) {
    for (bool useOverflowSlot : {false, true}) {
        constexpr int kIterations = 2000;
        constexpr int kReaderCount = 2;
        std::atomic<bool> start{false};
        std::atomic<bool> done{false};
        std::atomic<uint64_t> publishedRaw{0};

        std::vector<std::thread> readers;
        readers.reserve(kReaderCount);
        for (int r = 0; r < kReaderCount; ++r) {
            readers.emplace_back([&]() {
                while (!start.load(std::memory_order_acquire)) {
                }
                while (!done.load(std::memory_order_acquire)) {
                    const uint64_t raw = publishedRaw.load(std::memory_order_acquire);
                    if (!raw) continue;
                    const Boxed<VkFence> candidate = Boxed<VkFence>::fromRaw(raw);
                    const std::optional<UnboxedHandle> underlying =
                        sBoxedHandleManager.getUnderlying(candidate);
                    if (underlying.has_value()) {
                        const uint64_t val = toRaw(*underlying);
                        EXPECT_TRUE(val == 0x5555'1111ULL || val == 0x5555'2222ULL);
                    }
                    const Boxed<VkImage> forgedImage = Boxed<VkImage>::fromRaw(
                        BoxedHandleManager::Store::EM::withType(raw, Tag_VkImage));
                    EXPECT_FALSE(sBoxedHandleManager.isLive(forgedImage));
                    const Boxed<VkQueue> forgedQueue = Boxed<VkQueue>::fromRaw(
                        BoxedHandleManager::Store::EM::withType(raw, Tag_VkQueue));
                    EXPECT_EQ(nullptr, sBoxedHandleManager.getDispatch(forgedQueue));
                    EXPECT_EQ(nullptr, sBoxedHandleManager.getOrderMaintenanceInfo(forgedQueue));
                }
            });
        }

        start.store(true, std::memory_order_release);
        for (int i = 0; i < kIterations; ++i) {
            if (useOverflowSlot) {
                const uint64_t replayRaw = BoxedHandleManager::Store::EM::makeHandle(
                    16000 + (i % 4), /*generation=*/1, Tag_VkFence);
                sBoxedHandleManager.replayHandles({replayRaw});
            }
            Boxed<VkFence> boxed = new_boxed(reinterpret_cast<VkFence>(0x5555'1111ULL));
            publishedRaw.store(boxed.toRaw(), std::memory_order_release);
            set_boxed(boxed, reinterpret_cast<VkFence>(0x5555'2222ULL));
            delete_boxed(boxed);
        }

        done.store(true, std::memory_order_release);
        for (auto& reader : readers) {
            reader.join();
        }
    }
}

TEST(BoxedHandleTypeTagValidationTest, ReverseMapSeparatesSameNumericHostHandlesByTypeTag) {
    constexpr uint64_t kSharedHostHandleA = 0x4444'4444ULL;
    constexpr uint64_t kSharedHostHandleB = 0x4444'4445ULL;

    VkFence fenceA = reinterpret_cast<VkFence>(kSharedHostHandleA);
    VkFence fenceB = reinterpret_cast<VkFence>(kSharedHostHandleB);
    VkSemaphore semaphoreA = reinterpret_cast<VkSemaphore>(kSharedHostHandleA);

    Boxed<VkFence> boxedFence = new_boxed(fenceA);
    Boxed<VkSemaphore> boxedSemaphore = new_boxed(semaphoreA);

    EXPECT_EQ(boxedFence, unboxed_to_boxed(fenceA));
    EXPECT_EQ(boxedSemaphore, unboxed_to_boxed(semaphoreA));

    // Updating or deleting one type must not disturb the other type's reverse mapping.
    set_boxed(boxedFence, fenceB);
    EXPECT_EQ(Boxed<VkFence>(), unboxed_to_boxed(fenceA));
    EXPECT_EQ(boxedFence, unboxed_to_boxed(fenceB));
    EXPECT_EQ(boxedSemaphore, unboxed_to_boxed(semaphoreA));

    set_boxed(boxedFence, fenceA);
    delete_boxed(boxedSemaphore);
    EXPECT_EQ(Boxed<VkSemaphore>(), unboxed_to_boxed(semaphoreA));
    EXPECT_EQ(boxedFence, unboxed_to_boxed(fenceA));

    delete_boxed(boxedFence);
    EXPECT_EQ(Boxed<VkFence>(), unboxed_to_boxed(fenceA));
}

TEST(BoxedHandleTypeTagValidationTest, GenericOverloadedApiRoundTrip) {
    VkDevice unboxedDevice = reinterpret_cast<VkDevice>(0x4444'aaaaULL);
    VulkanDispatch devVk = {};
    Boxed<VkDevice> boxedDevice = new_boxed(unboxedDevice, &devVk);
    EXPECT_EQ(unboxedDevice, unbox(boxedDevice));
    EXPECT_EQ(unboxedDevice, try_unbox(boxedDevice));
    EXPECT_EQ(&devVk, dispatch(boxedDevice));
    EXPECT_NE(nullptr, ordmaint(boxedDevice));
    EXPECT_EQ(boxedDevice, unboxed_to_boxed(unboxedDevice));

    VkBuffer unboxedBufferA = reinterpret_cast<VkBuffer>(0x4444'bbbbULL);
    VkBuffer unboxedBufferB = reinterpret_cast<VkBuffer>(0x4444'ccccULL);
    Boxed<VkBuffer> boxedBuffer = new_boxed(unboxedBufferA);
    EXPECT_EQ(unboxedBufferA, unbox(boxedBuffer));
    EXPECT_EQ(boxedBuffer, unboxed_to_boxed(unboxedBufferA));

    set_boxed(boxedBuffer, unboxedBufferB);
    EXPECT_EQ(unboxedBufferB, unbox(boxedBuffer));
    EXPECT_EQ(Boxed<VkBuffer>(), unboxed_to_boxed(unboxedBufferA));
    EXPECT_EQ(boxedBuffer, unboxed_to_boxed(unboxedBufferB));

    bool delayedCalled = false;
    delayed_delete_boxed(boxedBuffer, unboxedDevice, [&]() { delayedCalled = true; });
    EXPECT_FALSE(delayedCalled);
    sBoxedHandleManager.processDelayedRemoves(unboxedDevice);
    EXPECT_TRUE(delayedCalled);
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(boxedBuffer));

    delete_boxed(boxedDevice);
    EXPECT_EQ(VK_NULL_HANDLE, try_unbox(boxedDevice));
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
