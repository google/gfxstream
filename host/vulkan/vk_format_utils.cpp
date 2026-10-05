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

#include "vk_format_utils.h"

#include <limits>
#include <unordered_map>

#include "gfxstream/common/logging.h"
#include "gfxstream/host/gfxstream_format.h"
#include "gfxstream/host/guest_operations.h"
#include "vulkan/vk_enum_string_helper.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

struct FormatPlaneLayout {
    uint32_t horizontalSubsampling = 1;
    uint32_t verticalSubsampling = 1;
    uint32_t texelBlockWidth = 1;
    uint32_t texelBlockHeight = 1;
    uint32_t sampleIncrementBytes = 0;
    VkImageAspectFlags aspectMask = 0;
};

template <typename T>
inline T alignToPower2(T val, T align) {
    return (val + (align - 1)) & ~(align - 1);
}

template <typename T>
inline T alignUp(T val, T align) {
    return ((val + (align - 1)) / align) * align;
}

struct FormatPlaneLayouts {
    uint32_t horizontalAlignmentPixels = 1;
    uint32_t verticalAlignmentPixels = 1;
    std::vector<FormatPlaneLayout> planeLayouts;
    TransferInfo::PackFunction packFunction = nullptr;
    TransferInfo::UnpackFunction unpackFunction = nullptr;
};

void PackD16S8(const VkExtent3D& extent, const uint8_t* src, uint8_t* dst) {
    const size_t numPixels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
    const size_t plane0Size = numPixels * 2;
    const size_t plane1Offset = alignToPower2<size_t>(plane0Size, 4);
    const uint8_t* srcPixels = src;
    uint8_t* dstDepth = dst;
    uint8_t* dstStencil = dst + plane1Offset;
    for (size_t i = 0; i < numPixels; ++i) {
        dstDepth[i * 2 + 0] = srcPixels[i * 3 + 0];
        dstDepth[i * 2 + 1] = srcPixels[i * 3 + 1];
        dstStencil[i] = srcPixels[i * 3 + 2];
    }
}

void UnpackD16S8(const VkExtent3D& extent, const uint8_t* src, uint8_t* dst) {
    const size_t numPixels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
    const size_t plane0Size = numPixels * 2;
    const size_t plane1Offset = alignToPower2<size_t>(plane0Size, 4);
    const uint8_t* srcDepth = src;
    const uint8_t* srcStencil = src + plane1Offset;
    uint8_t* dstPixels = dst;
    for (size_t i = 0; i < numPixels; ++i) {
        dstPixels[i * 3 + 0] = srcDepth[i * 2 + 0];
        dstPixels[i * 3 + 1] = srcDepth[i * 2 + 1];
        dstPixels[i * 3 + 2] = srcStencil[i];
    }
}

// Per the Vulkan spec, buffer copies of the depth aspect of D24 formats use
// VK_FORMAT_X8_D24_UNORM_PACK32: one 32-bit word per texel, upper 8 bits unused.
void PackD24S8(const VkExtent3D& extent, const uint8_t* src, uint8_t* dst) {
    const size_t numPixels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
    const size_t plane0Size = numPixels * 4;
    const size_t plane1Offset = alignToPower2<size_t>(plane0Size, 4);
    const uint8_t* srcPixels = src;
    uint8_t* dstDepth = dst;
    uint8_t* dstStencil = dst + plane1Offset;
    for (size_t i = 0; i < numPixels; ++i) {
        dstDepth[i * 4 + 0] = srcPixels[i * 4 + 0];
        dstDepth[i * 4 + 1] = srcPixels[i * 4 + 1];
        dstDepth[i * 4 + 2] = srcPixels[i * 4 + 2];
        // Unused X8 byte; zero it so the staging contents are deterministic.
        dstDepth[i * 4 + 3] = 0;
        dstStencil[i] = srcPixels[i * 4 + 3];
    }
}

void UnpackD24S8(const VkExtent3D& extent, const uint8_t* src, uint8_t* dst) {
    const size_t numPixels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
    const size_t plane0Size = numPixels * 4;
    const size_t plane1Offset = alignToPower2<size_t>(plane0Size, 4);
    const uint8_t* srcDepth = src;
    const uint8_t* srcStencil = src + plane1Offset;
    uint8_t* dstPixels = dst;
    for (size_t i = 0; i < numPixels; ++i) {
        dstPixels[i * 4 + 0] = srcDepth[i * 4 + 0];
        dstPixels[i * 4 + 1] = srcDepth[i * 4 + 1];
        dstPixels[i * 4 + 2] = srcDepth[i * 4 + 2];
        dstPixels[i * 4 + 3] = srcStencil[i];
    }
}

void PackD32S8(const VkExtent3D& extent, const uint8_t* src, uint8_t* dst) {
    const size_t numPixels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
    const size_t plane0Size = numPixels * 4;
    const size_t plane1Offset = alignToPower2<size_t>(plane0Size, 4);
    const uint8_t* srcPixels = src;
    uint8_t* dstDepth = dst;
    uint8_t* dstStencil = dst + plane1Offset;
    for (size_t i = 0; i < numPixels; ++i) {
        dstDepth[i * 4 + 0] = srcPixels[i * 5 + 0];
        dstDepth[i * 4 + 1] = srcPixels[i * 5 + 1];
        dstDepth[i * 4 + 2] = srcPixels[i * 5 + 2];
        dstDepth[i * 4 + 3] = srcPixels[i * 5 + 3];
        dstStencil[i] = srcPixels[i * 5 + 4];
    }
}

void UnpackD32S8(const VkExtent3D& extent, const uint8_t* src, uint8_t* dst) {
    const size_t numPixels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
    const size_t plane0Size = numPixels * 4;
    const size_t plane1Offset = alignToPower2<size_t>(plane0Size, 4);
    const uint8_t* srcDepth = src;
    const uint8_t* srcStencil = src + plane1Offset;
    uint8_t* dstPixels = dst;
    for (size_t i = 0; i < numPixels; ++i) {
        dstPixels[i * 5 + 0] = srcDepth[i * 4 + 0];
        dstPixels[i * 5 + 1] = srcDepth[i * 4 + 1];
        dstPixels[i * 5 + 2] = srcDepth[i * 4 + 2];
        dstPixels[i * 5 + 3] = srcDepth[i * 4 + 3];
        dstPixels[i * 5 + 4] = srcStencil[i];
    }
}

const std::unordered_map<VkFormat, FormatPlaneLayouts>& getFormatPlaneLayoutsMap() {
    static const auto* kPlaneLayoutsMap = []() {
        auto* map = new std::unordered_map<VkFormat, FormatPlaneLayouts>({
            {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
             {
                 .horizontalAlignmentPixels = 2,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 2,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                         },
                         {
                             .horizontalSubsampling = 2,
                             .verticalSubsampling = 2,
                             .sampleIncrementBytes = 4,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                         },
                     },
             }},
            {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,
             {
                 .horizontalAlignmentPixels = 2,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 2,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                         },
                         {
                             .horizontalSubsampling = 2,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 4,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                         },
                     },
             }},
            {VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
             {
                 .horizontalAlignmentPixels = 2,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                         },
                         {
                             .horizontalSubsampling = 2,
                             .verticalSubsampling = 2,
                             .sampleIncrementBytes = 2,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                         },
                     },
             }},
            {VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
             {
                 .horizontalAlignmentPixels = 1,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                         },
                         {
                             .horizontalSubsampling = 2,
                             .verticalSubsampling = 2,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                         },
                         {
                             .horizontalSubsampling = 2,
                             .verticalSubsampling = 2,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_PLANE_2_BIT,
                         },
                     },
             }},
            {VK_FORMAT_D16_UNORM_S8_UINT,
             {
                 .horizontalAlignmentPixels = 1,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 2,
                             .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                         },
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                         },
                     },
                 .packFunction = PackD16S8,
                 .unpackFunction = UnpackD16S8,
             }},
            {VK_FORMAT_D24_UNORM_S8_UINT,
             {
                 .horizontalAlignmentPixels = 1,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 4,
                             .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                         },
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                         },
                     },
                 .packFunction = PackD24S8,
                 .unpackFunction = UnpackD24S8,
             }},
            {VK_FORMAT_D32_SFLOAT_S8_UINT,
             {
                 .horizontalAlignmentPixels = 1,
                 .planeLayouts =
                     {
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 4,
                             .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                         },
                         {
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                             .sampleIncrementBytes = 1,
                             .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                         },
                     },
                 .packFunction = PackD32S8,
                 .unpackFunction = UnpackD32S8,
             }},
        });

#define ADD_SINGLE_PLANE_FORMAT_INFO(format, bpp)           \
    (*map)[format] = FormatPlaneLayouts{                    \
        .horizontalAlignmentPixels = 1,                     \
        .planeLayouts =                                     \
            {                                               \
                {                                           \
                    .horizontalSubsampling = 1,             \
                    .verticalSubsampling = 1,               \
                    .sampleIncrementBytes = bpp,            \
                    .aspectMask = getFormatAspects(format), \
                },                                          \
            },                                              \
    };
        LIST_VK_FORMATS_LINEAR(ADD_SINGLE_PLANE_FORMAT_INFO)
#undef ADD_SINGLE_PLANE_FORMAT_INFO

#define ADD_COMPRESSED_FORMAT_INFO(format, blockWidth, blockHeight, bytesPerBlock) \
    (*map)[format] = FormatPlaneLayouts{                                           \
        .horizontalAlignmentPixels = blockWidth,                                   \
        .verticalAlignmentPixels = blockHeight,                                    \
        .planeLayouts =                                                            \
            {                                                                      \
                {                                                                  \
                    .horizontalSubsampling = 1,                                    \
                    .verticalSubsampling = 1,                                      \
                    .texelBlockWidth = blockWidth,                                 \
                    .texelBlockHeight = blockHeight,                               \
                    .sampleIncrementBytes = bytesPerBlock,                         \
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,                       \
                },                                                                 \
            },                                                                     \
    };
        LIST_VK_FORMATS_BC(ADD_COMPRESSED_FORMAT_INFO)
        LIST_VK_FORMATS_ETC2(ADD_COMPRESSED_FORMAT_INFO)
        LIST_VK_FORMATS_ASTC(ADD_COMPRESSED_FORMAT_INFO)
#undef ADD_COMPRESSED_FORMAT_INFO

        return map;
    }();
    return *kPlaneLayoutsMap;
}

}  // namespace

std::optional<VkFormat> ToVkFormat(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::B4G4R4A4_UNORM:
            return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
        case GfxstreamFormat::B5G5R5A1_UNORM:
            return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
        case GfxstreamFormat::B8G8R8A8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case GfxstreamFormat::BLOB:
            return VK_FORMAT_R8_UNORM;
        case GfxstreamFormat::D16_UNORM:
            return VK_FORMAT_D16_UNORM;
        case GfxstreamFormat::D24_UNORM_S8_UINT:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case GfxstreamFormat::D24_UNORM:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
        case GfxstreamFormat::D32_FLOAT_S8_UINT:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case GfxstreamFormat::D32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case GfxstreamFormat::NV12:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case GfxstreamFormat::NV21:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case GfxstreamFormat::P010:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        case GfxstreamFormat::P210:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16;
        case GfxstreamFormat::R10G10B10A2_UNORM:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case GfxstreamFormat::R4G4B4A4_UNORM:
            return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case GfxstreamFormat::R16_UNORM:
            return VK_FORMAT_R16_UNORM;
        case GfxstreamFormat::R16G16B16_FLOAT:
            return VK_FORMAT_R16G16B16_SFLOAT;
        case GfxstreamFormat::R16G16B16A16_FLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GfxstreamFormat::R5G6B5_UNORM:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case GfxstreamFormat::R8_UNORM:
            return VK_FORMAT_R8_UNORM;
        case GfxstreamFormat::R8G8_UNORM:
            return VK_FORMAT_R8G8_UNORM;
        case GfxstreamFormat::R8G8B8_UNORM:
            return VK_FORMAT_R8G8B8_UNORM;
        case GfxstreamFormat::R8G8B8A8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case GfxstreamFormat::R8G8B8X8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case GfxstreamFormat::S8_UINT:
            return VK_FORMAT_S8_UINT;
        case GfxstreamFormat::UNKNOWN:
            return std::nullopt;
        case GfxstreamFormat::YV21:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        case GfxstreamFormat::YV12:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        case GfxstreamFormat::A1B5G5R5_UNORM:
            return VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR;
        case GfxstreamFormat::A8_UNORM:
            return VK_FORMAT_A8_UNORM_KHR;
        default:
            return std::nullopt;
    }
}

std::optional<GfxstreamFormat> ToGfxstreamFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
            return GfxstreamFormat::B4G4R4A4_UNORM;
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
            return GfxstreamFormat::R4G4B4A4_UNORM;
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
            return GfxstreamFormat::B5G5R5A1_UNORM;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return GfxstreamFormat::B8G8R8A8_UNORM;
        case VK_FORMAT_D16_UNORM:
            return GfxstreamFormat::D16_UNORM;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return GfxstreamFormat::D24_UNORM_S8_UINT;
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return GfxstreamFormat::D24_UNORM;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return GfxstreamFormat::D32_FLOAT_S8_UINT;
        case VK_FORMAT_D32_SFLOAT:
            return GfxstreamFormat::D32_FLOAT;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return GfxstreamFormat::R10G10B10A2_UNORM;
        case VK_FORMAT_R16_UNORM:
            return GfxstreamFormat::R16_UNORM;
        case VK_FORMAT_R16G16B16_SFLOAT:
            return GfxstreamFormat::R16G16B16_FLOAT;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return GfxstreamFormat::R16G16B16A16_FLOAT;
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return GfxstreamFormat::R5G6B5_UNORM;
        case VK_FORMAT_R8_UNORM:
            return GfxstreamFormat::R8_UNORM;
        case VK_FORMAT_R8G8_UNORM:
            return GfxstreamFormat::R8G8_UNORM;
        case VK_FORMAT_R8G8B8_UNORM:
            return GfxstreamFormat::R8G8B8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return GfxstreamFormat::R8G8B8A8_UNORM;
        case VK_FORMAT_S8_UINT:
            return GfxstreamFormat::S8_UINT;
        case VK_FORMAT_UNDEFINED:
            return GfxstreamFormat::UNKNOWN;
        case VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR:
            return GfxstreamFormat::A1B5G5R5_UNORM;
        case VK_FORMAT_A8_UNORM_KHR:
            return GfxstreamFormat::A8_UNORM;
        default:
            return std::nullopt;
    }
}

const FormatPlaneLayouts* getFormatPlaneLayouts(VkFormat format) {
    const auto& formatPlaneLayoutsMap = getFormatPlaneLayoutsMap();

    auto it = formatPlaneLayoutsMap.find(format);
    if (it == formatPlaneLayoutsMap.end()) {
        return nullptr;
    }
    return &it->second;
}

bool getFormatTransferInfo(VkFormat format, VkExtent3D extent, TransferInfo* outTransferInfo) {
    const FormatPlaneLayouts* formatInfo = getFormatPlaneLayouts(format);
    if (formatInfo == nullptr) {
        GFXSTREAM_ERROR("Unhandled format: %s [%d]", string_VkFormat(format), format);
        return false;
    }

    uint32_t alignmentPixels = formatInfo->horizontalAlignmentPixels;
    if (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM) {
        if (gfxstream::host::get_gfxstream_guest_android_gralloc() == MINIGBM) {
            alignmentPixels = 32;
        } else {
            alignmentPixels = 1;
        }
    }

    const VkDeviceSize alignedWidth64 =
        alignUp<VkDeviceSize>(extent.width, alignmentPixels);
    if (alignedWidth64 > std::numeric_limits<uint32_t>::max()) {
        GFXSTREAM_ERROR("Overflow aligning width %u with alignment %u", extent.width,
                        alignmentPixels);
        return false;
    }
    const VkDeviceSize alignedHeight64 =
        alignUp<VkDeviceSize>(extent.height, formatInfo->verticalAlignmentPixels);
    if (alignedHeight64 > std::numeric_limits<uint32_t>::max()) {
        GFXSTREAM_ERROR("Overflow aligning height %u with alignment %u", extent.height,
                        formatInfo->verticalAlignmentPixels);
        return false;
    }
    const uint32_t alignedWidth = static_cast<uint32_t>(alignedWidth64);
    const uint32_t alignedHeight = static_cast<uint32_t>(alignedHeight64);
    VkDeviceSize cumulativeOffset = 0;

    outTransferInfo->bufferImageCopies.clear();
    for (const FormatPlaneLayout& planeInfo : formatInfo->planeLayouts) {
        // Align to 4 for VUID-vkCmdCopyBufferToImage-dstImage-07978
        const VkDeviceSize planeOffset = (formatInfo->packFunction || formatInfo->unpackFunction)
                                             ? alignToPower2<VkDeviceSize>(cumulativeOffset, 4)
                                             : cumulativeOffset;
        const uint32_t planeWidth = alignedWidth / planeInfo.horizontalSubsampling;
        const uint32_t planeHeight = alignedHeight / planeInfo.verticalSubsampling;
        const uint32_t planeBlocksWide = planeWidth / planeInfo.texelBlockWidth;
        const uint32_t planeBlocksHigh = planeHeight / planeInfo.texelBlockHeight;
        const VkDeviceSize planeBpp = planeInfo.sampleIncrementBytes;
        const uint32_t planeStrideTexels = planeWidth;
        const VkDeviceSize planeStrideBytes =
            static_cast<VkDeviceSize>(planeBlocksWide) * planeBpp;
        const VkDeviceSize planeSize =
            static_cast<VkDeviceSize>(planeBlocksHigh) * planeStrideBytes * extent.depth;

        const uint32_t imageExtentWidth =
            (planeInfo.texelBlockWidth > 1)
                ? (extent.width / planeInfo.horizontalSubsampling)
                : planeWidth;
        const uint32_t imageExtentHeight =
            (planeInfo.texelBlockHeight > 1)
                ? (extent.height / planeInfo.verticalSubsampling)
                : planeHeight;

        outTransferInfo->bufferImageCopies.emplace_back(VkBufferImageCopy{
            .bufferOffset = planeOffset,
            .bufferRowLength = planeStrideTexels,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = planeInfo.aspectMask,
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
                    .width = imageExtentWidth,
                    .height = imageExtentHeight,
                    .depth = extent.depth,
                },
        });
        cumulativeOffset = planeOffset + planeSize;
    }
    outTransferInfo->stagingBufferCopySize = cumulativeOffset;
    outTransferInfo->packFunction = formatInfo->packFunction;
    outTransferInfo->unpackFunction = formatInfo->unpackFunction;

    return true;
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
