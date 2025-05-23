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

#include "VkFormatUtils.h"

namespace gfxstream {
namespace vk {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsTrue;

MATCHER_P(EqsVkExtent3D, expected, "") {
    return ExplainMatchResult(AllOf(Field("width", &VkExtent3D::width, Eq(expected.width)),
                                    Field("height", &VkExtent3D::height, Eq(expected.height)),
                                    Field("depth", &VkExtent3D::depth, Eq(expected.depth))),
                              arg, result_listener);
}

MATCHER_P(EqsVkImageSubresourceLayers, expected, "") {
    return ExplainMatchResult(
        AllOf(Field("aspectMask", &VkImageSubresourceLayers::aspectMask, Eq(expected.aspectMask)),
              Field("mipLevel", &VkImageSubresourceLayers::mipLevel, Eq(expected.mipLevel)),
              Field("baseArrayLayer", &VkImageSubresourceLayers::baseArrayLayer,
                    Eq(expected.baseArrayLayer)),
              Field("layerCount", &VkImageSubresourceLayers::layerCount, Eq(expected.layerCount))),
        arg, result_listener);
}

MATCHER_P(EqsVkOffset3D, expected, "") {
    return ExplainMatchResult(AllOf(Field("x", &VkOffset3D::x, Eq(expected.x)),
                                    Field("y", &VkOffset3D::y, Eq(expected.y)),
                                    Field("z", &VkOffset3D::z, Eq(expected.z))),
                              arg, result_listener);
}

MATCHER_P(EqsVkBufferImageCopy, expected, "") {
    return ExplainMatchResult(
        AllOf(Field("bufferOffset", &VkBufferImageCopy::bufferOffset, Eq(expected.bufferOffset)),
              Field("bufferRowLength", &VkBufferImageCopy::bufferRowLength,
                    Eq(expected.bufferRowLength)),
              Field("bufferImageHeight", &VkBufferImageCopy::bufferImageHeight,
                    Eq(expected.bufferImageHeight)),
              Field("imageSubresource", &VkBufferImageCopy::imageSubresource,
                    EqsVkImageSubresourceLayers(expected.imageSubresource)),
              Field("imageOffset", &VkBufferImageCopy::imageOffset,
                    EqsVkOffset3D(expected.imageOffset)),
              Field("imageExtent", &VkBufferImageCopy::imageExtent,
                    EqsVkExtent3D(expected.imageExtent))),
        arg, result_listener);
}

TEST(VkFormatUtilsTest, GetTransferInfoInvalidFormat) {
    const VkFormat format = VK_FORMAT_UNDEFINED;
    const uint32_t width = 16;
    const uint32_t height = 16;
    ASSERT_THAT(getFormatTransferInfo(format, width, height, nullptr, nullptr), IsFalse());
}

TEST(VkFormatUtilsTest, GetTransferInfoRGBA) {
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    const uint32_t width = 16;
    const uint32_t height = 16;

    VkDeviceSize bufferCopySize;
    std::vector<VkBufferImageCopy> bufferImageCopies;
    ASSERT_THAT(getFormatTransferInfo(format, width, height, &bufferCopySize, &bufferImageCopies),
                IsTrue());
    EXPECT_THAT(bufferCopySize, Eq(1024));
    ASSERT_THAT(bufferImageCopies, ElementsAre(EqsVkBufferImageCopy(VkBufferImageCopy{
                                       .bufferOffset = 0,
                                       .bufferRowLength = 16,
                                       .bufferImageHeight = 0,
                                       .imageSubresource =
                                           {
                                               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                               .mipLevel = 0,
                                               .baseArrayLayer = 0,
                                               .layerCount = 1,
                                           },
                                       .imageOffset =
                                           {
                                               .x = 0,
                                               .y = 0,
                                               .z = 0,
                                           },
                                       .imageExtent =
                                           {
                                               .width = 16,
                                               .height = 16,
                                               .depth = 1,
                                           },
                                   })));
}

TEST(VkFormatUtilsTest, GetTransferInfoNV12OrNV21) {
    const VkFormat format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    const uint32_t width = 16;
    const uint32_t height = 16;

    VkDeviceSize bufferCopySize;
    std::vector<VkBufferImageCopy> bufferImageCopies;
    ASSERT_THAT(getFormatTransferInfo(format, width, height, &bufferCopySize, &bufferImageCopies),
                IsTrue());
    EXPECT_THAT(bufferCopySize, Eq(384));
    ASSERT_THAT(bufferImageCopies,
                ElementsAre(EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 0,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                                        .mipLevel = 0,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                .imageOffset =
                                    {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                    },
                                .imageExtent =
                                    {
                                        .width = 16,
                                        .height = 16,
                                        .depth = 1,
                                    },
                            }),
                            EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 256,
                                .bufferRowLength = 8,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                                        .mipLevel = 0,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                .imageOffset =
                                    {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                    },
                                .imageExtent =
                                    {
                                        .width = 8,
                                        .height = 8,
                                        .depth = 1,
                                    },
                            })));
}

TEST(VkFormatUtilsTest, GetTransferInfoYV12OrYV21) {
    const VkFormat format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    const uint32_t width = 32;
    const uint32_t height = 32;

    VkDeviceSize bufferCopySize;
    std::vector<VkBufferImageCopy> bufferImageCopies;
    ASSERT_THAT(getFormatTransferInfo(format, width, height, &bufferCopySize, &bufferImageCopies),
                IsTrue());
    EXPECT_THAT(bufferCopySize, Eq(1536));
    ASSERT_THAT(bufferImageCopies,
                ElementsAre(EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 0,
                                .bufferRowLength = 32,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                                        .mipLevel = 0,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                .imageOffset =
                                    {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                    },
                                .imageExtent =
                                    {
                                        .width = 32,
                                        .height = 32,
                                        .depth = 1,
                                    },
                            }),
                            EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 1024,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                                        .mipLevel = 0,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                .imageOffset =
                                    {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                    },
                                .imageExtent =
                                    {
                                        .width = 16,
                                        .height = 16,
                                        .depth = 1,
                                    },
                            }),
                            EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 1280,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_PLANE_2_BIT,
                                        .mipLevel = 0,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                .imageOffset =
                                    {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                    },
                                .imageExtent =
                                    {
                                        .width = 16,
                                        .height = 16,
                                        .depth = 1,
                                    },
                            })));
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream

