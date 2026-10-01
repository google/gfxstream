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

VkDescriptorUpdateTemplateEntry makeTemplateEntry(uint32_t binding, uint32_t descriptorCount,
                                                  VkDescriptorType descriptorType) {
    return VkDescriptorUpdateTemplateEntry{
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = descriptorCount,
        .descriptorType = descriptorType,
        .offset = 0,
        .stride = 0,
    };
}

TEST(VkDecoderGlobalStateDescriptorUpdateTemplateTest, linearizedEntriesSkipWholeArrays) {
    // Each array entry is followed by an entry of the same kind, which has to start after
    // all of the array's elements.
    const std::vector<VkDescriptorUpdateTemplateEntry> entries = {
        makeTemplateEntry(0, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        makeTemplateEntry(1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
        makeTemplateEntry(2, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        makeTemplateEntry(3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeTemplateEntry(4, 2, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER),
        makeTemplateEntry(5, 1, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER),
        makeTemplateEntry(6, 16, VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT),
    };
    const VkDescriptorUpdateTemplateCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .descriptorUpdateEntryCount = static_cast<uint32_t>(entries.size()),
        .pDescriptorUpdateEntries = entries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        .descriptorSetLayout = VK_NULL_HANDLE,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = VK_NULL_HANDLE,
        .set = 0,
    };

    const DescriptorUpdateTemplateInfo info =
        calcLinearizedDescriptorUpdateTemplateInfo(&createInfo);

    constexpr size_t kImageInfoSize = sizeof(VkDescriptorImageInfo);
    constexpr size_t kBufferInfoSize = sizeof(VkDescriptorBufferInfo);
    constexpr size_t kBufferViewSize = sizeof(VkBufferView);
    constexpr size_t kBufferInfoStart = 4 * kImageInfoSize;
    constexpr size_t kBufferViewStart = kBufferInfoStart + 3 * kBufferInfoSize;
    constexpr size_t kInlineUniformBlockStart = kBufferViewStart + 3 * kBufferViewSize;

    EXPECT_EQ(info.imageInfoStart, 0u);
    EXPECT_EQ(info.imageInfoCount, 4u);
    EXPECT_EQ(info.bufferInfoStart, kBufferInfoStart);
    EXPECT_EQ(info.bufferInfoCount, 3u);
    EXPECT_EQ(info.bufferViewStart, kBufferViewStart);
    EXPECT_EQ(info.bufferViewCount, 3u);
    EXPECT_EQ(info.inlineUniformBlockStart, kInlineUniformBlockStart);
    EXPECT_EQ(info.inlineUniformBlockCount, 16u);
    EXPECT_EQ(info.data.size(), kInlineUniformBlockStart + 16);

    const size_t expectedOffsets[] = {
        0,
        3 * kImageInfoSize,
        kBufferInfoStart,
        kBufferInfoStart + 2 * kBufferInfoSize,
        kBufferViewStart,
        kBufferViewStart + 2 * kBufferViewSize,
        kInlineUniformBlockStart,
    };
    const size_t expectedStrides[] = {
        kImageInfoSize,
        kImageInfoSize,
        kBufferInfoSize,
        kBufferInfoSize,
        kBufferViewSize,
        kBufferViewSize,
        0,
    };
    ASSERT_EQ(info.linearizedTemplateEntries.size(), entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const VkDescriptorUpdateTemplateEntry& entry = info.linearizedTemplateEntries[i];
        EXPECT_EQ(entry.dstBinding, entries[i].dstBinding);
        EXPECT_EQ(entry.descriptorCount, entries[i].descriptorCount);
        EXPECT_EQ(entry.offset, expectedOffsets[i]) << "entry " << i;
        EXPECT_EQ(entry.stride, expectedStrides[i]) << "entry " << i;
    }
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
