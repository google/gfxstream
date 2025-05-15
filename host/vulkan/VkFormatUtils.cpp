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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "VkFormatUtils.h"

#include <unordered_map>

namespace gfxstream {
namespace vk {
#include "gfxstream/common/logging.h"
#include "vulkan/vk_enum_string_helper.h"

namespace {

struct FormatPlaneLayout {
    uint32_t horizontalSubsampling = 1;
    uint32_t verticalSubsampling = 1;
    uint32_t sampleIncrementBytes = 0;
    VkImageAspectFlags aspectMask = 0;
};

struct FormatPlaneLayouts {
    uint32_t horizontalAlignmentPixels = 1;
    std::vector<FormatPlaneLayout> planeLayouts;
};

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
        });

#define ADD_SINGLE_PLANE_FORMAT_INFO(format, bpp)            \
    (*map)[format] = FormatPlaneLayouts{                     \
        .horizontalAlignmentPixels = 1,                      \
        .planeLayouts =                                      \
            {                                                \
                {                                            \
                    .horizontalSubsampling = 1,              \
                    .verticalSubsampling = 1,                \
                    .sampleIncrementBytes = bpp,             \
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, \
                },                                           \
            },                                               \
    };
        LIST_VK_FORMATS_LINEAR(ADD_SINGLE_PLANE_FORMAT_INFO)
#undef ADD_SINGLE_PLANE_FORMAT_INFO

        return map;
    }();
    return *kPlaneLayoutsMap;
}

inline uint32_t alignToPower2(uint32_t val, uint32_t align) {
    return (val + (align - 1)) & ~(align - 1);
}

}  // namespace

const FormatPlaneLayouts* getFormatPlaneLayouts(VkFormat format) {
    const auto& formatPlaneLayoutsMap = getFormatPlaneLayoutsMap();

    auto it = formatPlaneLayoutsMap.find(format);
    if (it == formatPlaneLayoutsMap.end()) {
        return nullptr;
    }
    return &it->second;
}

bool getFormatTransferInfo(VkFormat format, uint32_t width, uint32_t height,
                           VkDeviceSize* outStagingBufferCopySize,
                           std::vector<VkBufferImageCopy>* outBufferImageCopies) {
    const FormatPlaneLayouts* formatInfo = getFormatPlaneLayouts(format);
    if (formatInfo == nullptr) {
        GFXSTREAM_ERROR("Unhandled format: %s [%d]", string_VkFormat(format), format);
        return false;
    }

    const uint32_t alignedWidth = alignToPower2(width, formatInfo->horizontalAlignmentPixels);
    const uint32_t alignedHeight = height;
    uint32_t cumulativeOffset = 0;
    uint32_t cumulativeSize = 0;
    for (const FormatPlaneLayout& planeInfo : formatInfo->planeLayouts) {
        const uint32_t planeOffset = cumulativeOffset;
        const uint32_t planeWidth = alignedWidth / planeInfo.horizontalSubsampling;
        const uint32_t planeHeight = alignedHeight / planeInfo.verticalSubsampling;
        const uint32_t planeBpp = planeInfo.sampleIncrementBytes;
        const uint32_t planeStrideTexels = planeWidth;
        const uint32_t planeStrideBytes = planeStrideTexels * planeBpp;
        const uint32_t planeSize = planeHeight * planeStrideBytes;
        if (outBufferImageCopies) {
            outBufferImageCopies->emplace_back(VkBufferImageCopy{
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
                        .width = planeWidth,
                        .height = planeHeight,
                        .depth = 1,
                    },
            });
        }
        cumulativeOffset += planeSize;
        cumulativeSize += planeSize;
    }
    if (outStagingBufferCopySize) {
        *outStagingBufferCopySize = cumulativeSize;
    }

    return true;
}

}  // namespace vk
}  // namespace gfxstream
