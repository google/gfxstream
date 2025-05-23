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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VkDecoderGlobalState.cpp"

#include "gfxstream/testing/TestUtils.h"

namespace gfxstream {
namespace vk {
namespace {
using ::testing::_;
using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::Test;
using ::testing::UnorderedElementsAre;

class VkDecoderGlobalStateExternalFenceTest : public Test {
protected:
    class MockDispatch {
       public:
        MOCK_METHOD(VkResult, vkGetFenceStatus, (VkDevice device, VkFence fence), ());
        MOCK_METHOD(VkResult,
                    vkResetFences,
                    (VkDevice device, uint32_t numFences, const VkFence* fence),
                    ());
    };

    VkDecoderGlobalStateExternalFenceTest()
        : mDevice(reinterpret_cast<VkDevice>(0x2222'0000)), mPool(&mMockDispatch, mDevice) {}

    ~VkDecoderGlobalStateExternalFenceTest() {
        mPool.popAll();
    }

    MockDispatch mMockDispatch;
    VkDevice mDevice;
    ExternalFencePool<MockDispatch> mPool;
};

using VkDecoderGlobalStateExternalFenceDeathTest = VkDecoderGlobalStateExternalFenceTest;

TEST_F(VkDecoderGlobalStateExternalFenceTest, poolNoDeviceFences) {
    VkFenceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
    };
    ASSERT_EQ(VK_NULL_HANDLE, mPool.pop(&createInfo));
}

TEST_F(VkDecoderGlobalStateExternalFenceTest, poolReuseSignalledFence) {
    {
        InSequence s;
        EXPECT_CALL(mMockDispatch, vkGetFenceStatus(_, _)).WillOnce(Return(VK_SUCCESS));
        EXPECT_CALL(mMockDispatch, vkResetFences(_, _, _)).WillOnce(Return(VK_SUCCESS));
    }

    VkFence fence = reinterpret_cast<VkFence>(0x1234'0000);
    VkFenceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
    };
    mPool.add(fence);
    VkFence reusedFence = mPool.pop(&createInfo);

    ASSERT_EQ(fence, reusedFence);
}

TEST_F(VkDecoderGlobalStateExternalFenceTest, poolReuseSignalledFenceAsSignaled) {
    {
        InSequence s;
        EXPECT_CALL(mMockDispatch, vkGetFenceStatus(_, _)).WillOnce(Return(VK_SUCCESS));
        EXPECT_CALL(mMockDispatch, vkResetFences(_, _, _)).Times(0);
    }

    VkFence fence = reinterpret_cast<VkFence>(0x1234'0000);
    VkFenceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = 0,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    mPool.add(fence);
    VkFence reusedFence = mPool.pop(&createInfo);

    ASSERT_EQ(fence, reusedFence);
}

TEST_F(VkDecoderGlobalStateExternalFenceTest, poolUnsignalledFence) {
    {
        InSequence s;
        EXPECT_CALL(mMockDispatch, vkGetFenceStatus(_, _)).WillOnce(Return(VK_NOT_READY));
        EXPECT_CALL(mMockDispatch, vkResetFences(_, _, _)).Times(0);
    }

    VkFence fence = reinterpret_cast<VkFence>(0x1234'0000);
    VkFenceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
    };
    mPool.add(fence);

    ASSERT_EQ(VK_NULL_HANDLE, mPool.pop(&createInfo));
}

TEST_F(VkDecoderGlobalStateExternalFenceTest, poolPopAll) {
    VkFence fence1 = reinterpret_cast<VkFence>(0x1234'0000);
    VkFence fence2 = reinterpret_cast<VkFence>(0x2234'0000);
    VkFence fence3 = reinterpret_cast<VkFence>(0x3234'0000);
    mPool.add(fence1);
    mPool.add(fence2);
    mPool.add(fence3);

    std::vector<VkFence> result = mPool.popAll();
    ASSERT_THAT(result, UnorderedElementsAre(fence1, fence2, fence3));
}

TEST_F(VkDecoderGlobalStateExternalFenceDeathTest, undestroyedFences) {
    ASSERT_DEATH(
        {
            ExternalFencePool<MockDispatch> pool(&mMockDispatch, mDevice);
            VkFence fence = reinterpret_cast<VkFence>(0x1234'0000);
            pool.add(fence);
        },
        MatchesStdRegex(
            "External fence pool for VkDevice:0000000022220000|0x22220000 destroyed but 1 "
            "fences still not destroyed."));
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream
