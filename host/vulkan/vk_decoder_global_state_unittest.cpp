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

TEST(VkDecoderGlobalStateDescriptorUpdateTemplateTest, linearizedEntriesSkipWholeArrays) {
    constexpr size_t kImageInfoSize = sizeof(VkDescriptorImageInfo);
    constexpr size_t kBufferInfoSize = sizeof(VkDescriptorBufferInfo);
    constexpr size_t kBufferViewSize = sizeof(VkBufferView);

    struct TestEntryInfo {
        uint32_t descriptorBinding;
        uint32_t descriptorCount;
        VkDescriptorType descriptorType;

        // Expected offset and stride into the linearized buffer.
        size_t expectedOffset;
        size_t expectedStride;
    };
    // Each array entry is followed by an entry of the same kind, which has to start after all of
    // the array's elements: every entry's offset is the previous entry's offset plus the size of
    // the previous entry's descriptor type times its descriptorCount.
    const std::vector<TestEntryInfo> infos = {
        {
            .descriptorBinding = 0,
            .descriptorCount = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .expectedOffset = 0,
            .expectedStride = kImageInfoSize,
        },
        {
            .descriptorBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .expectedOffset = 3 * kImageInfoSize,
            .expectedStride = kImageInfoSize,
        },
        {
            .descriptorBinding = 2,
            .descriptorCount = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .expectedOffset = 3 * kImageInfoSize +
                              1 * kImageInfoSize,
            .expectedStride = kBufferInfoSize,
        },
        {
            .descriptorBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .expectedOffset = 3 * kImageInfoSize +
                              1 * kImageInfoSize +
                              2 * kBufferInfoSize,
            .expectedStride = kBufferInfoSize,
        },
        {
            .descriptorBinding = 4,
            .descriptorCount = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            .expectedOffset = 3 * kImageInfoSize +
                              1 * kImageInfoSize +
                              2 * kBufferInfoSize +
                              1 * kBufferInfoSize,
            .expectedStride = kBufferViewSize,
        },
        {
            .descriptorBinding = 5,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            .expectedOffset = 3 * kImageInfoSize +
                              1 * kImageInfoSize +
                              2 * kBufferInfoSize +
                              1 * kBufferInfoSize +
                              2 * kBufferViewSize,
            .expectedStride = kBufferViewSize,
        },
        {
            .descriptorBinding = 6,
            .descriptorCount = 16,
            .descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT,
            .expectedOffset = 3 * kImageInfoSize +
                              1 * kImageInfoSize +
                              2 * kBufferInfoSize +
                              1 * kBufferInfoSize +
                              2 * kBufferViewSize +
                              1 * kBufferViewSize,
            .expectedStride = 0,
        },
    };

    std::vector<VkDescriptorUpdateTemplateEntry> entries;
    for (const TestEntryInfo& info : infos) {
        entries.push_back(VkDescriptorUpdateTemplateEntry{
            .dstBinding = info.descriptorBinding,
            .dstArrayElement = 0,
            .descriptorCount = info.descriptorCount,
            .descriptorType = info.descriptorType,
            .offset = 0,
            .stride = 0,
        });
    }
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

    const DescriptorUpdateTemplateInfo templateInfo =
        calcLinearizedDescriptorUpdateTemplateInfo(&createInfo);

    // The image infos, buffer infos, buffer views and inline uniform block bytes each start where
    // the first entry of that kind starts.
    EXPECT_EQ(templateInfo.imageInfoStart, infos[0].expectedOffset);
    EXPECT_EQ(templateInfo.imageInfoCount, 3u + 1u);
    EXPECT_EQ(templateInfo.bufferInfoStart, infos[2].expectedOffset);
    EXPECT_EQ(templateInfo.bufferInfoCount, 2u + 1u);
    EXPECT_EQ(templateInfo.bufferViewStart, infos[4].expectedOffset);
    EXPECT_EQ(templateInfo.bufferViewCount, 2u + 1u);
    EXPECT_EQ(templateInfo.inlineUniformBlockStart, infos[6].expectedOffset);
    EXPECT_EQ(templateInfo.inlineUniformBlockCount, 16u);
    EXPECT_EQ(templateInfo.data.size(), infos[6].expectedOffset + 16);

    ASSERT_EQ(templateInfo.linearizedTemplateEntries.size(), infos.size());
    for (size_t i = 0; i < infos.size(); ++i) {
        const TestEntryInfo& info = infos[i];
        const VkDescriptorUpdateTemplateEntry& entry = templateInfo.linearizedTemplateEntries[i];
        EXPECT_EQ(entry.dstBinding, info.descriptorBinding) << "entry " << i;
        EXPECT_EQ(entry.descriptorCount, info.descriptorCount) << "entry " << i;
        EXPECT_EQ(entry.offset, info.expectedOffset) << "entry " << i;
        EXPECT_EQ(entry.stride, info.expectedStride) << "entry " << i;
    }
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
