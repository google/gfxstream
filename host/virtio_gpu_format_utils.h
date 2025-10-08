// Copyright (C) 2024 The Android Open Source Project
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

#pragma once

#include "gfxstream/common/logging.h"
#include "gfxstream/host/gfxstream_format.h"

namespace gfxstream {
namespace host {

// clang-format off
#define VIRGL_BIND_RENDER_TARGET (1u << 1)
#define VIRGL_BIND_SAMPLER_VIEW  (1u << 3)
#define VIRGL_BIND_CURSOR        (1u << 16)
#define VIRGL_BIND_CUSTOM        (1u << 17)
#define VIRGL_BIND_SCANOUT       (1u << 18)
#define VIRGL_BIND_SHARED        (1u << 20)
#define VIRGL_BIND_LINEAR        (1u << 22)

#define VIRGL_FORMAT_B8G8R8A8_UNORM          1
#define VIRGL_FORMAT_B8G8R8X8_UNORM          2
#define VIRGL_FORMAT_B5G6R5_UNORM            7
#define VIRGL_FORMAT_R10G10B10A2_UNORM       8
#define VIRGL_FORMAT_Z16_UNORM               16
#define VIRGL_FORMAT_Z32_FLOAT               18
#define VIRGL_FORMAT_Z24_UNORM_S8_UINT       19
#define VIRGL_FORMAT_Z24X8_UNORM             21
#define VIRGL_FORMAT_R16_UNORM               48
#define VIRGL_FORMAT_R8_UNORM                64
#define VIRGL_FORMAT_R8G8_UNORM              65
#define VIRGL_FORMAT_R8G8B8_UNORM            66
#define VIRGL_FORMAT_R8G8B8A8_UNORM          67
#define VIRGL_FORMAT_R16G16B16A16_FLOAT      94
#define VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT    126
#define VIRGL_FORMAT_R8G8B8X8_UNORM          134
#define VIRGL_FORMAT_YV12                    163
#define VIRGL_FORMAT_NV12                    166
#define VIRGL_FORMAT_P010                    314
// clang-format on

inline std::optional<GfxstreamFormat> ToGfxstreamFormat(uint32_t virglFormat) {
    switch (virglFormat) {
        case VIRGL_FORMAT_B8G8R8X8_UNORM:
        case VIRGL_FORMAT_B8G8R8A8_UNORM:
            return GfxstreamFormat::B8G8R8A8_UNORM;
        case VIRGL_FORMAT_R8G8B8X8_UNORM:
            return GfxstreamFormat::R8G8B8X8_UNORM;
        case VIRGL_FORMAT_R8G8B8A8_UNORM:
            return GfxstreamFormat::R8G8B8A8_UNORM;
        case VIRGL_FORMAT_B5G6R5_UNORM:
            return GfxstreamFormat::R5G6B5_UNORM;
        case VIRGL_FORMAT_R16_UNORM:
            return GfxstreamFormat::R16_UNORM;
        case VIRGL_FORMAT_R16G16B16A16_FLOAT:
            return GfxstreamFormat::R16G16B16A16_FLOAT;
        case VIRGL_FORMAT_R8_UNORM:
            return GfxstreamFormat::R8_UNORM;
        case VIRGL_FORMAT_R8G8_UNORM:
            return GfxstreamFormat::R8G8_UNORM;
        case VIRGL_FORMAT_R8G8B8_UNORM:
            return GfxstreamFormat::R8G8B8_UNORM;
        case VIRGL_FORMAT_NV12:
            return GfxstreamFormat::NV12;
        case VIRGL_FORMAT_P010:
            return GfxstreamFormat::P010;
        case VIRGL_FORMAT_YV12:
            return GfxstreamFormat::YV12;
        case VIRGL_FORMAT_R10G10B10A2_UNORM:
            return GfxstreamFormat::R10G10B10A2_UNORM;
        case VIRGL_FORMAT_Z16_UNORM:
            return GfxstreamFormat::D16_UNORM;
        case VIRGL_FORMAT_Z24X8_UNORM:
            return GfxstreamFormat::D24_UNORM;
        case VIRGL_FORMAT_Z24_UNORM_S8_UINT:
            return GfxstreamFormat::D24_UNORM_S8_UINT;
        case VIRGL_FORMAT_Z32_FLOAT:
            return GfxstreamFormat::D32_FLOAT;
        case VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT:
            return GfxstreamFormat::D32_FLOAT_S8_UINT;
        default: {
            return std::nullopt;
        }
    }
}

static inline void set_virgl_format_supported(uint32_t* mask, uint32_t virgl_format,
                                              bool supported) {
    uint32_t index = virgl_format / 32;
    uint32_t bit_offset = 1 << (virgl_format & 31);
    if (supported) {
        mask[index] |= bit_offset;
    } else {
        mask[index] &= ~bit_offset;
    }
}

static inline size_t GetTransferOffset(uint32_t virglFormat,
                                       uint32_t totalWidth,
                                       uint32_t totalHeight,
                                       uint32_t x,
                                       uint32_t y,
                                       uint32_t w,
                                       uint32_t h) {
    auto formatOpt = ToGfxstreamFormat(virglFormat);
    if (!formatOpt) {
        GFXSTREAM_ERROR("Unhandled format %d", virglFormat);
        return 0;
    }
    auto format = *formatOpt;

    if (IsYuvFormat(format)) {
        return 0;
    }

    auto bppOpt = GetBpp(format);
    if (!bppOpt) {
        const std::string formatString = ToString(format);
        GFXSTREAM_ERROR("Unhandled format %s", formatString.c_str());
        return 0;
    }
    const uint32_t bpp = *bppOpt;
    const uint32_t stride = totalWidth * bpp;
    return y * stride + x * bpp;
}

static inline uint32_t align_up_power_of_2(uint32_t n, uint32_t a) {
    return (n + (a - 1)) & ~(a - 1);
}

static inline size_t GetTransferSize(uint32_t virglFormat,
                                     uint32_t totalWidth,
                                     uint32_t totalHeight,
                                     uint32_t x,
                                     uint32_t y,
                                     uint32_t w,
                                     uint32_t h) {
    auto formatOpt = ToGfxstreamFormat(virglFormat);
    if (!formatOpt) {
        GFXSTREAM_ERROR("Unhandled format %d", virglFormat);
        return 0;
    }
    auto format = *formatOpt;

    if (IsYuvFormat(format)) {
        uint32_t bpp = format == GfxstreamFormat::P010 ? 2 : 1;

        uint32_t yWidth = totalWidth;
        uint32_t yHeight = totalHeight;
        uint32_t yStridePixels;
        switch (format) {
            case GfxstreamFormat::NV12:
            case GfxstreamFormat::NV21:
            case GfxstreamFormat::P010: {
                yStridePixels = yWidth;
                break;
            }
            case GfxstreamFormat::YV12:
            case GfxstreamFormat::YV21: {
                yStridePixels = align_up_power_of_2(yWidth, 32);
                break;
            }
            default: {
                const std::string formatString = ToString(format);
                GFXSTREAM_ERROR("Unhandled format %s", formatString.c_str());
                return 0;
            }
        }

        uint32_t yStrideBytes = yStridePixels * bpp;
        uint32_t ySize = yStrideBytes * yHeight;

        uint32_t uvStridePixels;
        uint32_t uvPlaneCount;
        switch (format) {
            case GfxstreamFormat::NV12:
            case GfxstreamFormat::NV21:
            case GfxstreamFormat::P010: {
                uvStridePixels = yStridePixels;
                uvPlaneCount = 1;
                break;
            }
            case GfxstreamFormat::YV12:
            case GfxstreamFormat::YV21: {
                uvStridePixels = yStridePixels / 2;
                uvPlaneCount = 2;
                break;
            }
            default: {
                const std::string formatString = ToString(format);
                GFXSTREAM_ERROR("Unhandled format %s", formatString.c_str());
                return 0;
            }
        }

        uint32_t uvStrideBytes = uvStridePixels * bpp;
        uint32_t uvHeight = totalHeight / 2;
        uint32_t uvSize = uvStrideBytes * uvHeight * uvPlaneCount;

        uint32_t dataSize = ySize + uvSize;
        return dataSize;
    } else {
        auto bppOpt = GetBpp(format);
        if (!bppOpt) {
            const std::string formatString = ToString(format);
            GFXSTREAM_ERROR("Unhandled format %s", formatString.c_str());
            return 0;
        }
        const uint32_t bpp = *bppOpt;
        const uint32_t stride = totalWidth * bpp;

        // height - 1 in order to treat the (w * bpp) row specially
        // (i.e., the last row does not occupy the full stride)
        return (h - 1U) * stride + w * bpp;
    }
}

}  // namespace host
}  // namespace gfxstream
