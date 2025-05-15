// Copyright 2019 The Android Open Source Project
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
#pragma once

#include <vulkan/vulkan_core.h>

#include <vector>

#include "gfxstream/common/logging.h"
#include "vulkan/vk_enum_string_helper.h"

namespace gfxstream {
namespace vk {

// Header library that captures common patterns when working with
// Vulkan formats:
// - Macros to iterate over categories of formats
// - Add often-used parameters like the bytes per pixel and ASTC block size

#define LIST_VK_FORMATS_LINEAR(f)                      \
    f(VK_FORMAT_R4G4_UNORM_PACK8, 1)                   \
    f(VK_FORMAT_R4G4B4A4_UNORM_PACK16, 2)              \
    f(VK_FORMAT_B4G4R4A4_UNORM_PACK16, 2)              \
    f(VK_FORMAT_R5G6B5_UNORM_PACK16, 2)                \
    f(VK_FORMAT_B5G6R5_UNORM_PACK16, 2)                \
    f(VK_FORMAT_R5G5B5A1_UNORM_PACK16, 2)              \
    f(VK_FORMAT_B5G5R5A1_UNORM_PACK16, 2)              \
    f(VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2)              \
    f(VK_FORMAT_R8_UNORM, 1)                           \
    f(VK_FORMAT_R8_SNORM, 1)                           \
    f(VK_FORMAT_R8_USCALED, 1)                         \
    f(VK_FORMAT_R8_SSCALED, 1)                         \
    f(VK_FORMAT_R8_UINT, 1)                            \
    f(VK_FORMAT_R8_SINT, 1)                            \
    f(VK_FORMAT_R8_SRGB, 1)                            \
    f(VK_FORMAT_R8G8_UNORM, 2)                         \
    f(VK_FORMAT_R8G8_SNORM, 2)                         \
    f(VK_FORMAT_R8G8_USCALED, 2)                       \
    f(VK_FORMAT_R8G8_SSCALED, 2)                       \
    f(VK_FORMAT_R8G8_UINT, 2)                          \
    f(VK_FORMAT_R8G8_SINT, 2)                          \
    f(VK_FORMAT_R8G8_SRGB, 2)                          \
    f(VK_FORMAT_R8G8B8_UNORM, 3)                       \
    f(VK_FORMAT_R8G8B8_SNORM, 3)                       \
    f(VK_FORMAT_R8G8B8_USCALED, 3)                     \
    f(VK_FORMAT_R8G8B8_SSCALED, 3)                     \
    f(VK_FORMAT_R8G8B8_UINT, 3)                        \
    f(VK_FORMAT_R8G8B8_SINT, 3)                        \
    f(VK_FORMAT_R8G8B8_SRGB, 3)                        \
    f(VK_FORMAT_B8G8R8_UNORM, 3)                       \
    f(VK_FORMAT_B8G8R8_SNORM, 3)                       \
    f(VK_FORMAT_B8G8R8_USCALED, 3)                     \
    f(VK_FORMAT_B8G8R8_SSCALED, 3)                     \
    f(VK_FORMAT_B8G8R8_UINT, 3)                        \
    f(VK_FORMAT_B8G8R8_SINT, 3)                        \
    f(VK_FORMAT_B8G8R8_SRGB, 3)                        \
    f(VK_FORMAT_R8G8B8A8_UNORM, 4)                     \
    f(VK_FORMAT_R8G8B8A8_SNORM, 4)                     \
    f(VK_FORMAT_R8G8B8A8_USCALED, 4)                   \
    f(VK_FORMAT_R8G8B8A8_SSCALED, 4)                   \
    f(VK_FORMAT_R8G8B8A8_UINT, 4)                      \
    f(VK_FORMAT_R8G8B8A8_SINT, 4)                      \
    f(VK_FORMAT_R8G8B8A8_SRGB, 4)                      \
    f(VK_FORMAT_B8G8R8A8_UNORM, 4)                     \
    f(VK_FORMAT_B8G8R8A8_SNORM, 4)                     \
    f(VK_FORMAT_B8G8R8A8_USCALED, 4)                   \
    f(VK_FORMAT_B8G8R8A8_SSCALED, 4)                   \
    f(VK_FORMAT_B8G8R8A8_UINT, 4)                      \
    f(VK_FORMAT_B8G8R8A8_SINT, 4)                      \
    f(VK_FORMAT_B8G8R8A8_SRGB, 4)                      \
    f(VK_FORMAT_A8B8G8R8_UNORM_PACK32, 4)              \
    f(VK_FORMAT_A8B8G8R8_SNORM_PACK32, 4)              \
    f(VK_FORMAT_A8B8G8R8_USCALED_PACK32, 4)            \
    f(VK_FORMAT_A8B8G8R8_SSCALED_PACK32, 4)            \
    f(VK_FORMAT_A8B8G8R8_UINT_PACK32, 4)               \
    f(VK_FORMAT_A8B8G8R8_SINT_PACK32, 4)               \
    f(VK_FORMAT_A8B8G8R8_SRGB_PACK32, 4)               \
    f(VK_FORMAT_A2R10G10B10_UNORM_PACK32, 4)           \
    f(VK_FORMAT_A2R10G10B10_SNORM_PACK32, 4)           \
    f(VK_FORMAT_A2R10G10B10_USCALED_PACK32, 4)         \
    f(VK_FORMAT_A2R10G10B10_SSCALED_PACK32, 4)         \
    f(VK_FORMAT_A2R10G10B10_UINT_PACK32, 4)            \
    f(VK_FORMAT_A2R10G10B10_SINT_PACK32, 4)            \
    f(VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4)           \
    f(VK_FORMAT_A2B10G10R10_SNORM_PACK32, 4)           \
    f(VK_FORMAT_A2B10G10R10_USCALED_PACK32, 4)         \
    f(VK_FORMAT_A2B10G10R10_SSCALED_PACK32, 4)         \
    f(VK_FORMAT_A2B10G10R10_UINT_PACK32, 4)            \
    f(VK_FORMAT_A2B10G10R10_SINT_PACK32, 4)            \
    f(VK_FORMAT_R16_UNORM, 2)                          \
    f(VK_FORMAT_R16_SNORM, 2)                          \
    f(VK_FORMAT_R16_USCALED, 2)                        \
    f(VK_FORMAT_R16_SSCALED, 2)                        \
    f(VK_FORMAT_R16_UINT, 2)                           \
    f(VK_FORMAT_R16_SINT, 2)                           \
    f(VK_FORMAT_R16_SFLOAT, 2)                         \
    f(VK_FORMAT_R16G16_UNORM, 4)                       \
    f(VK_FORMAT_R16G16_SNORM, 4)                       \
    f(VK_FORMAT_R16G16_USCALED, 4)                     \
    f(VK_FORMAT_R16G16_SSCALED, 4)                     \
    f(VK_FORMAT_R16G16_UINT, 4)                        \
    f(VK_FORMAT_R16G16_SINT, 4)                        \
    f(VK_FORMAT_R16G16_SFLOAT, 4)                      \
    f(VK_FORMAT_R16G16B16_UNORM, 6)                    \
    f(VK_FORMAT_R16G16B16_SNORM, 6)                    \
    f(VK_FORMAT_R16G16B16_USCALED, 6)                  \
    f(VK_FORMAT_R16G16B16_SSCALED, 6)                  \
    f(VK_FORMAT_R16G16B16_UINT, 6)                     \
    f(VK_FORMAT_R16G16B16_SINT, 6)                     \
    f(VK_FORMAT_R16G16B16_SFLOAT, 6)                   \
    f(VK_FORMAT_R16G16B16A16_UNORM, 8)                 \
    f(VK_FORMAT_R16G16B16A16_SNORM, 8)                 \
    f(VK_FORMAT_R16G16B16A16_USCALED, 8)               \
    f(VK_FORMAT_R16G16B16A16_SSCALED, 8)               \
    f(VK_FORMAT_R16G16B16A16_UINT, 8)                  \
    f(VK_FORMAT_R16G16B16A16_SINT, 8)                  \
    f(VK_FORMAT_R16G16B16A16_SFLOAT, 8)                \
    f(VK_FORMAT_R32_UINT, 4)                           \
    f(VK_FORMAT_R32_SINT, 4)                           \
    f(VK_FORMAT_R32_SFLOAT, 4)                         \
    f(VK_FORMAT_R32G32_UINT, 8)                        \
    f(VK_FORMAT_R32G32_SINT, 8)                        \
    f(VK_FORMAT_R32G32_SFLOAT, 8)                      \
    f(VK_FORMAT_R32G32B32_UINT, 8)                     \
    f(VK_FORMAT_R32G32B32_SINT, 8)                     \
    f(VK_FORMAT_R32G32B32_SFLOAT, 8)                   \
    f(VK_FORMAT_R32G32B32A32_UINT, 16)                 \
    f(VK_FORMAT_R32G32B32A32_SINT, 16)                 \
    f(VK_FORMAT_R32G32B32A32_SFLOAT, 16)               \
    f(VK_FORMAT_R64_UINT, 8)                           \
    f(VK_FORMAT_R64_SINT, 8)                           \
    f(VK_FORMAT_R64_SFLOAT, 8)                         \
    f(VK_FORMAT_R64G64_UINT, 16)                       \
    f(VK_FORMAT_R64G64_SINT, 16)                       \
    f(VK_FORMAT_R64G64_SFLOAT, 16)                     \
    f(VK_FORMAT_R64G64B64_UINT, 24)                    \
    f(VK_FORMAT_R64G64B64_SINT, 24)                    \
    f(VK_FORMAT_R64G64B64_SFLOAT, 24)                  \
    f(VK_FORMAT_R64G64B64A64_UINT, 32)                 \
    f(VK_FORMAT_R64G64B64A64_SINT, 32)                 \
    f(VK_FORMAT_R64G64B64A64_SFLOAT, 32)               \
    f(VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4)            \
    f(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 4)             \
    f(VK_FORMAT_D16_UNORM, 2)                          \
    f(VK_FORMAT_X8_D24_UNORM_PACK32, 4)                \
    f(VK_FORMAT_D32_SFLOAT, 4)                         \
    f(VK_FORMAT_S8_UINT, 1)                            \
    f(VK_FORMAT_D16_UNORM_S8_UINT, 3)                  \
    f(VK_FORMAT_D24_UNORM_S8_UINT, 4)                  \
    f(VK_FORMAT_D32_SFLOAT_S8_UINT, 5)                 \
    f(VK_FORMAT_R10X6_UNORM_PACK16, 2)                 \
    f(VK_FORMAT_R10X6G10X6_UNORM_2PACK16, 4)           \
    f(VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16, 8) \
    f(VK_FORMAT_R12X4_UNORM_PACK16, 2)                 \
    f(VK_FORMAT_R12X4G12X4_UNORM_2PACK16, 4)           \
    f(VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16, 8)

constexpr bool isEtc2(VkFormat format) {
    switch (format) {
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        case VK_FORMAT_EAC_R11_UNORM_BLOCK:
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
            return true;
        default:
            return false;
    }
}

// Note: this only returns true for ASTC LDR, not HDR.
// TODO(gregschlom): Revisit our ASTC-related names if we decide to support HDR to make sure we
//   clearly distinguish between the two modes.
constexpr bool isAstc(VkFormat format) {
    switch (format) {
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

constexpr bool isBc(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

constexpr bool formatIsUInt(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8B8_UINT:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16B16_UINT:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R64_UINT:
        case VK_FORMAT_R64G64_UINT:
        case VK_FORMAT_R64G64B64_UINT:
        case VK_FORMAT_R64G64B64A64_UINT:
        case VK_FORMAT_B8G8R8_UINT:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:
            return true;
        default:
            return false;
    }
}

constexpr bool formatIsSInt(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8B8_SINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_A8B8G8R8_SINT_PACK32:
        case VK_FORMAT_A2B10G10R10_SINT_PACK32:
        case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16B16_SINT:
        case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32B32_SINT:
        case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_R64_SINT:
        case VK_FORMAT_R64G64_SINT:
        case VK_FORMAT_R64G64B64_SINT:
        case VK_FORMAT_R64G64B64A64_SINT:
        case VK_FORMAT_B8G8R8_SINT:
        case VK_FORMAT_B8G8R8A8_SINT:
        case VK_FORMAT_A2R10G10B10_SINT_PACK32:
            return true;
        default:
            return false;
    }
}

constexpr bool formatIsDepthOrStencil(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return true;
        default:
            return false;
    }
}

constexpr bool formatRequiresSamplerYcbcrConversion(VkFormat format) {
    switch (format) {
        case VK_FORMAT_G8B8G8R8_422_UNORM:
        case VK_FORMAT_B8G8R8G8_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
        case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
        case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
        case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
        case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16B16G16R16_422_UNORM:
        case VK_FORMAT_B16G16R16G16_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
#ifdef VK_EXT_YCBCR_2PLANE_444_FORMATS_SPEC_VERSION
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT:
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT:
#endif
            return true;
        default:
            return false;
    }
}

// Returns the size in bytes needed to copy an image with the given format,
// width, and height to a staging buffer and the VkBufferImageCopy-s needed
// to copy from a staging buffer to destination VkImage.
bool getFormatTransferInfo(VkFormat format, uint32_t width, uint32_t height,
                           VkDeviceSize* outStagingBufferCopySize,
                           std::vector<VkBufferImageCopy>* outBufferImageCopies);

}  // namespace vk
}  // namespace gfxstream

