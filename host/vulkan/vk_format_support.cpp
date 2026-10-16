// Copyright 2025 The Android Open Source Project
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

#include "vk_format_support.h"

#include "vk_format_utils.h"

namespace gfxstream {
namespace host {
namespace vk {

/*static*/ ImageSupport ImageSupport::GetDefaultUnpopulatedImageSupport() {
    struct ImageFeatureCombo {
        VkFormat format;
        VkImageCreateFlags createFlags = 0;
    };
    // Set the mutable flag for RGB UNORM formats so that the created image can also be sampled in
    // the sRGB Colorspace. See
    // https://chromium-review.googlesource.com/c/chromiumos/platform/minigbm/+/3827672/comments/77db9cb3_60663a6a
    // for details. Keep each unflagged fallback entry after its flagged entry because
    // GetSupportedInfo() returns the first supported match.
    std::vector<ImageFeatureCombo> combos = {
        // Cover all the gralloc formats
        {VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R8G8B8A8_UNORM},
        {VK_FORMAT_R8G8B8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R8G8B8_UNORM},

        {VK_FORMAT_R5G6B5_UNORM_PACK16},
        {VK_FORMAT_A1R5G5B5_UNORM_PACK16},

        {VK_FORMAT_R16G16B16A16_SFLOAT},
        {VK_FORMAT_R16G16B16_SFLOAT},

        {VK_FORMAT_B8G8R8A8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_B8G8R8A8_UNORM},

        {VK_FORMAT_B4G4R4A4_UNORM_PACK16,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_B4G4R4A4_UNORM_PACK16},
        {VK_FORMAT_R4G4B4A4_UNORM_PACK16,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R4G4B4A4_UNORM_PACK16},

        {VK_FORMAT_R8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R8_UNORM},
        {VK_FORMAT_R16_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R16_UNORM},

        {VK_FORMAT_A2R10G10B10_UINT_PACK32},
        {VK_FORMAT_A2R10G10B10_UNORM_PACK32},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32},

        // Compressed texture formats
        {VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK},
        {VK_FORMAT_ASTC_4x4_UNORM_BLOCK},

        // YUV formats used in Android
        {VK_FORMAT_G8_B8R8_2PLANE_420_UNORM},
        {VK_FORMAT_G8_B8R8_2PLANE_422_UNORM},
        {VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM},
        {VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM},
        {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16},
        {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16},
    };

    std::vector<VkImageType> types = {
        VK_IMAGE_TYPE_2D,
    };

    std::vector<VkImageTiling> tilings = {
        VK_IMAGE_TILING_LINEAR,
        VK_IMAGE_TILING_OPTIMAL,
    };

    ImageSupport imageSupport = {};

    for (auto combo : combos) {
        for (auto t : types) {
            for (auto ti : tilings) {
                imageSupport.mSupportInfos.emplace_back(ImageSupportInfo{
                    .format = combo.format,
                    .type = t,
                    .tiling = ti,
                    .createFlags = combo.createFlags,
                });
            }
        }
    }

    // Add depth attachment cases
    std::vector<ImageFeatureCombo> depthCombos = {
        // Depth formats
        {VK_FORMAT_D16_UNORM},
        {VK_FORMAT_X8_D24_UNORM_PACK32},
        {VK_FORMAT_D24_UNORM_S8_UINT},
        {VK_FORMAT_D32_SFLOAT},
        {VK_FORMAT_D32_SFLOAT_S8_UINT},
    };

    for (auto combo : depthCombos) {
        for (auto t : types) {
            imageSupport.mSupportInfos.emplace_back(ImageSupportInfo{
                .format = combo.format,
                .type = t,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .createFlags = combo.createFlags,
            });
        }
    }

    return imageSupport;
}

bool ImageSupport::IsFormatSupported(VkFormat format) const {
    bool supported = !formatIsDepthOrStencil(format);
    // TODO(b/356603558): add proper Vulkan querying, for now preserve existing assumption
    if (!supported) {
        for (const ImageSupportInfo& supportInfo : mSupportInfos) {
            // Only enable depth/stencil if it is usable as an attachment
            if (supportInfo.format == format &&
                formatIsDepthOrStencil(supportInfo.format) &&
                supportInfo.supported &&
                supportInfo.formatProps2.formatProperties.optimalTilingFeatures &
                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                supported = true;
            }
        }
    }
    return supported;
}

const ImageSupportInfo* ImageSupport::GetSupportedInfo(VkFormat format,
                                                       VkImageTiling tiling) const {
    for (const auto& supportInfo : mSupportInfos) {
        if (supportInfo.format == format && supportInfo.tiling == tiling && supportInfo.supported) {
            return &supportInfo;
        }
    }
    GFXSTREAM_WARNING("Could not find support info for format: %s [%d] and tiling: %d",
                      string_VkFormat(format), (int)format, (int)tiling);
    return nullptr;
}

std::optional<uint32_t> ImageSupport::GetNumberOfNeededCombinedImageSamplerDescriptors(VkFormat format) const {
    if (!formatRequiresSamplerYcbcrConversion(format)) {
        return 1;
    }

    uint32_t needed = 0;
    for (const ImageSupportInfo& info : mSupportInfos) {
        if (info.format == format && info.supported) {
            needed = std::max(needed, info.combinedImageSamplerDescriptorCount);
        }
    }

    return needed > 0 ? std::optional<uint32_t>(needed) : std::nullopt;
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
