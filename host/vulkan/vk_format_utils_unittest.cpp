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

#include "vk_format_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace gfxstream {
namespace host {
namespace vk {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

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
    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, {width, height, 1}, &transferInfo), IsFalse());
}

TEST(VkFormatUtilsTest, GetTransferInfoRGBA) {
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    const uint32_t width = 16;
    const uint32_t height = 16;

    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, {width, height, 1}, &transferInfo), IsTrue());
    EXPECT_THAT(transferInfo.stagingBufferCopySize, Eq(1024));
    ASSERT_THAT(transferInfo.bufferImageCopies, ElementsAre(EqsVkBufferImageCopy(VkBufferImageCopy{
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

    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, {width, height, 1}, &transferInfo), IsTrue());
    EXPECT_THAT(transferInfo.stagingBufferCopySize, Eq(384));
    ASSERT_THAT(transferInfo.bufferImageCopies,
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

    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, {width, height, 1}, &transferInfo), IsTrue());
    EXPECT_THAT(transferInfo.stagingBufferCopySize, Eq(1536));
    ASSERT_THAT(transferInfo.bufferImageCopies,
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

TEST(VkFormatUtilsTest, GetTransferInfoD24S8) {
    const VkFormat format = VK_FORMAT_D24_UNORM_S8_UINT;
    const uint32_t width = 16;
    const uint32_t height = 16;

    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, {width, height, 1}, &transferInfo), IsTrue());
    // Staging stores a 4-byte-per-texel depth plane plus a 1-byte-per-texel stencil
    // plane.
    EXPECT_THAT(transferInfo.stagingBufferCopySize, Eq(1280));
    ASSERT_THAT(transferInfo.bufferImageCopies,
                ElementsAre(EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 0,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
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
                                .bufferOffset = 1024,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
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

TEST(VkFormatUtilsTest, PackUnpackD24S8RoundTrip) {
    const VkFormat format = VK_FORMAT_D24_UNORM_S8_UINT;
    const VkExtent3D extent = {4, 4, 1};

    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, extent, &transferInfo), IsTrue());
    ASSERT_THAT(transferInfo.packFunction, NotNull());
    ASSERT_THAT(transferInfo.unpackFunction, NotNull());

    // Interleaved D24S8 pixels are 4 bytes each.
    std::vector<uint8_t> natural(extent.width * extent.height * extent.depth * 4);
    for (size_t i = 0; i < natural.size(); ++i) {
        natural[i] = static_cast<uint8_t>(i * 7 + 1);
    }

    std::vector<uint8_t> staging(transferInfo.stagingBufferCopySize, 0xaa);
    transferInfo.packFunction(extent, natural.data(), staging.data());

    // The upper byte of each 32-bit depth word must be written as zero.
    const size_t pixelCount = extent.width * extent.height;
    for (size_t i = 0; i < pixelCount; ++i) {
        EXPECT_THAT(staging[i * 4 + 3], Eq(0)) << "depth word " << i;
    }

    std::vector<uint8_t> roundTripped(natural.size(), 0);
    transferInfo.unpackFunction(extent, staging.data(), roundTripped.data());
    EXPECT_THAT(roundTripped, Eq(natural));
}

TEST(VkFormatUtilsTest, GetTransferInfoDepthStencilWithDepth) {
    const VkFormat format = VK_FORMAT_D16_UNORM_S8_UINT;
    const uint32_t width = 16;
    const uint32_t height = 16;
    const uint32_t depth = 2;

    TransferInfo transferInfo;
    ASSERT_THAT(getFormatTransferInfo(format, {width, height, depth}, &transferInfo), IsTrue());
    EXPECT_THAT(transferInfo.stagingBufferCopySize, Eq(1536));
    ASSERT_THAT(transferInfo.bufferImageCopies,
                ElementsAre(EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 0,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
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
                                        .depth = 2,
                                    },
                            }),
                            EqsVkBufferImageCopy(VkBufferImageCopy{
                                .bufferOffset = 1024,
                                .bufferRowLength = 16,
                                .bufferImageHeight = 0,
                                .imageSubresource =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
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
                                        .depth = 2,
                                    },
                            })));
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream

