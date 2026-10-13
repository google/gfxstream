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

#include "vk_decoder_global_state.cpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gfxstream/testing/TestUtils.h"

namespace gfxstream {
namespace host {
namespace vk {
struct VkDecoderGlobalStateTestPeer {
    VkDecoderGlobalState::Impl impl;

    void registerDescriptorPool(VkDescriptorPool pool) {
        std::lock_guard<std::mutex> lock(impl.mMutex);
        impl.mDescriptorPoolInfo[pool] = DescriptorPoolInfo{};
    }

    VkDescriptorSet getOrAllocateDescriptorSetFromPoolAndId(
        VulkanDispatch* vk, VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout setLayout,
        uint64_t poolId, uint32_t pendingAlloc, bool* didAlloc) {
        std::lock_guard<std::mutex> lock(impl.mMutex);
        return impl.getOrAllocateDescriptorSetFromPoolAndIdLocked(
            vk, device, pool, setLayout, poolId, pendingAlloc, didAlloc);
    }

    void commitDescriptorSetUpdates(
        gfxstream::base::BumpPool* pool, VulkanDispatch* vk, VkDevice device,
        uint32_t descriptorPoolCount, const VkDescriptorPool* pDescriptorPools,
        uint32_t descriptorSetCount, const VkDescriptorSetLayout* pDescriptorSetLayouts,
        const uint64_t* pDescriptorSetPoolIds, const uint32_t* pDescriptorSetWhichPool,
        const uint32_t* pDescriptorSetPendingAllocation,
        const uint32_t* pDescriptorWriteStartingIndices, uint32_t pendingDescriptorWriteCount,
        const VkWriteDescriptorSet* pPendingDescriptorWrites) {
        std::lock_guard<std::mutex> lock(impl.mMutex);
        impl.on_vkQueueCommitDescriptorSetUpdatesGOOGLELocked(
            pool, kInvalidSnapshotApiCallHandle, vk, device, descriptorPoolCount, pDescriptorPools,
            descriptorSetCount, pDescriptorSetLayouts, pDescriptorSetPoolIds,
            pDescriptorSetWhichPool, pDescriptorSetPendingAllocation,
            pDescriptorWriteStartingIndices, pendingDescriptorWriteCount, pPendingDescriptorWrites);
    }
};

namespace {

TEST(VkDecoderGlobalStateDescriptorSetPoolIdTest, BogusPoolIdReturnsNullWithoutCrash) {
    VkDecoderGlobalStateTestPeer peer;
    VulkanDispatch vk = {};
    VkDevice device = reinterpret_cast<VkDevice>(0x1111'0000);
    VkDescriptorPool pool = reinterpret_cast<VkDescriptorPool>(0x2222'0000);
    VkDescriptorSetLayout setLayout = reinterpret_cast<VkDescriptorSetLayout>(0x3333'0000);

    peer.registerDescriptorPool(pool);

    bool didAlloc = true;
    constexpr uint64_t kBogusPoolId = 0xdeadbeef'cafebabeULL;
    EXPECT_EQ(nullptr, sBoxedHandleManager.get(kBogusPoolId));

    VkDescriptorSet result = peer.getOrAllocateDescriptorSetFromPoolAndId(
        &vk, device, pool, setLayout, kBogusPoolId, /*pendingAlloc=*/1, &didAlloc);
    EXPECT_EQ(VK_NULL_HANDLE, result);
    EXPECT_FALSE(didAlloc);

    gfxstream::base::BumpPool bumpPool;
    uint32_t whichPool = 0;
    uint32_t pendingAlloc = 1;
    uint32_t writeStartIndex = 0;
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    };
    peer.commitDescriptorSetUpdates(
        &bumpPool, &vk, device,
        /*descriptorPoolCount=*/1, &pool,
        /*descriptorSetCount=*/1, &setLayout, &kBogusPoolId, &whichPool, &pendingAlloc,
        &writeStartIndex, /*pendingDescriptorWriteCount=*/1, &write);
}

TEST(VkDecoderGlobalStateDescriptorSetPoolIdTest, UnallocatedAndAllocatedPoolIdHandling) {
    VkDecoderGlobalStateTestPeer peer;
    VulkanDispatch vk = {};
    VkDevice device = reinterpret_cast<VkDevice>(0x1111'0000);
    VkDescriptorPool pool = reinterpret_cast<VkDescriptorPool>(0x2222'0000);
    VkDescriptorSetLayout setLayout = reinterpret_cast<VkDescriptorSetLayout>(0x3333'0000);
    VkDescriptorSet expectedSet = reinterpret_cast<VkDescriptorSet>(0x4444'0000);

    peer.registerDescriptorPool(pool);

    VkDescriptorSet boxedNullSet = new_boxed_non_dispatchable_VkDescriptorSet(VK_NULL_HANDLE);
    uint64_t nullPoolId = reinterpret_cast<uint64_t>(boxedNullSet);
    bool didAlloc = true;
    EXPECT_EQ(VK_NULL_HANDLE,
              peer.getOrAllocateDescriptorSetFromPoolAndId(
                  &vk, device, pool, setLayout, nullPoolId, /*pendingAlloc=*/0, &didAlloc));
    EXPECT_FALSE(didAlloc);
    delete_VkDescriptorSet(boxedNullSet);

    VkDescriptorSet boxedValidSet = new_boxed_non_dispatchable_VkDescriptorSet(expectedSet);
    uint64_t validPoolId = reinterpret_cast<uint64_t>(boxedValidSet);
    didAlloc = true;
    EXPECT_EQ(expectedSet,
              peer.getOrAllocateDescriptorSetFromPoolAndId(
                  &vk, device, pool, setLayout, validPoolId, /*pendingAlloc=*/0, &didAlloc));
    EXPECT_FALSE(didAlloc);
    delete_VkDescriptorSet(boxedValidSet);
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
