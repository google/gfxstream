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

extern "C" {
#include "drm/drm_fourcc.h"
}  // extern "C"

#include "gfxstream/host/logging.h"

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

const uint32_t kGlBgra = 0x80e1;
const uint32_t kGlRgba = 0x1908;
const uint32_t kGlRgba16f = 0x881A;
const uint32_t kGlRgb565 = 0x8d62;
const uint32_t kGlRgba1010102 = 0x8059;
const uint32_t kGlR8 = 0x8229;
const uint32_t kGlR16 = 0x822A;
const uint32_t kGlRg8 = 0x822b;
const uint32_t kGlRgb8 = 0x8051;
const uint32_t kGlLuminance = 0x1909;
const uint32_t kGlLuminanceAlpha = 0x190a;
const uint32_t kGlUnsignedByte = 0x1401;
const uint32_t kGlUnsignedShort = 0x1403;
const uint32_t kGlUnsignedShort565 = 0x8363;
const uint32_t kGlDepth16 = 0x81A5;
const uint32_t kGlDepth24 = 0x81A6;
const uint32_t kGlDepth24Stencil8 = 0x88F0;
const uint32_t kGlDepth32f = 0x8CAC;
const uint32_t kGlDepth32fStencil8 = 0x8CAD;

constexpr uint32_t kFwkFormatGlCompat = 0;
constexpr uint32_t kFwkFormatYV12 = 1;
// constexpr uint32_t kFwkFormatYUV420888 = 2;
constexpr uint32_t kFwkFormatNV12 = 3;
constexpr uint32_t kFwkFormatP010 = 4;

static inline bool virgl_format_is_yuv(uint32_t format) {
    switch (format) {
        case VIRGL_FORMAT_B8G8R8X8_UNORM:
        case VIRGL_FORMAT_B5G6R5_UNORM:
        case VIRGL_FORMAT_B8G8R8A8_UNORM:
        case VIRGL_FORMAT_R10G10B10A2_UNORM:
        case VIRGL_FORMAT_R16_UNORM:
        case VIRGL_FORMAT_R16G16B16A16_FLOAT:
        case VIRGL_FORMAT_R8_UNORM:
        case VIRGL_FORMAT_R8G8_UNORM:
        case VIRGL_FORMAT_R8G8B8_UNORM:
        case VIRGL_FORMAT_R8G8B8A8_UNORM:
        case VIRGL_FORMAT_R8G8B8X8_UNORM:
        case VIRGL_FORMAT_Z16_UNORM:
        case VIRGL_FORMAT_Z24_UNORM_S8_UINT:
        case VIRGL_FORMAT_Z24X8_UNORM:
        case VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT:
        case VIRGL_FORMAT_Z32_FLOAT:
            return false;
        case VIRGL_FORMAT_NV12:
        case VIRGL_FORMAT_P010:
        case VIRGL_FORMAT_YV12:
            return true;
        default:
            GFXSTREAM_ERROR("Unknown virgl format 0x%x", format);
            return false;
    }
}

static inline uint32_t virgl_format_to_gl(uint32_t virgl_format) {
    switch (virgl_format) {
        case VIRGL_FORMAT_B8G8R8X8_UNORM:
        case VIRGL_FORMAT_B8G8R8A8_UNORM:
            return kGlBgra;
        case VIRGL_FORMAT_R8G8B8X8_UNORM:
        case VIRGL_FORMAT_R8G8B8A8_UNORM:
            return kGlRgba;
        case VIRGL_FORMAT_B5G6R5_UNORM:
            return kGlRgb565;
        case VIRGL_FORMAT_R16_UNORM:
            return kGlR16;
        case VIRGL_FORMAT_R16G16B16A16_FLOAT:
            return kGlRgba16f;
        case VIRGL_FORMAT_R8_UNORM:
            return kGlR8;
        case VIRGL_FORMAT_R8G8_UNORM:
            return kGlRg8;
        case VIRGL_FORMAT_R8G8B8_UNORM:
            return kGlRgb8;
        case VIRGL_FORMAT_NV12:
        case VIRGL_FORMAT_P010:
        case VIRGL_FORMAT_YV12:
            // emulated as RGBA8888
            return kGlRgba;
        case VIRGL_FORMAT_R10G10B10A2_UNORM:
            return kGlRgba1010102;
        case VIRGL_FORMAT_Z16_UNORM:
            return kGlDepth16;
        case VIRGL_FORMAT_Z24X8_UNORM:
            return kGlDepth24;
        case VIRGL_FORMAT_Z24_UNORM_S8_UINT:
            return kGlDepth24Stencil8;
        case VIRGL_FORMAT_Z32_FLOAT:
            return kGlDepth32f;
        case VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT:
            return kGlDepth32fStencil8;
        default:
            return kGlRgba;
    }
}

static inline uint32_t virgl_format_to_fwk_format(uint32_t virgl_format) {
    switch (virgl_format) {
        case VIRGL_FORMAT_NV12:
            return kFwkFormatNV12;
        case VIRGL_FORMAT_P010:
            return kFwkFormatP010;
        case VIRGL_FORMAT_YV12:
            return kFwkFormatYV12;
        case VIRGL_FORMAT_R8_UNORM:
        case VIRGL_FORMAT_R16_UNORM:
        case VIRGL_FORMAT_R16G16B16A16_FLOAT:
        case VIRGL_FORMAT_R8G8_UNORM:
        case VIRGL_FORMAT_R8G8B8_UNORM:
        case VIRGL_FORMAT_B8G8R8X8_UNORM:
        case VIRGL_FORMAT_B8G8R8A8_UNORM:
        case VIRGL_FORMAT_R8G8B8X8_UNORM:
        case VIRGL_FORMAT_R8G8B8A8_UNORM:
        case VIRGL_FORMAT_B5G6R5_UNORM:
        case VIRGL_FORMAT_R10G10B10A2_UNORM:
        case VIRGL_FORMAT_Z16_UNORM:
        case VIRGL_FORMAT_Z24X8_UNORM:
        case VIRGL_FORMAT_Z24_UNORM_S8_UINT:
        case VIRGL_FORMAT_Z32_FLOAT:
        case VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT:
        default:  // kFwkFormatGlCompat: No extra conversions needed
            return kFwkFormatGlCompat;
    }
}

static inline uint32_t gl_format_to_natural_type(uint32_t format) {
    switch (format) {
        case kGlBgra:
        case kGlRgba:
        case kGlLuminance:
        case kGlLuminanceAlpha:
            return kGlUnsignedByte;
        case kGlRgb565:
            return kGlUnsignedShort565;
        case kGlDepth16:
            return kGlUnsignedShort;
        default:
            return kGlUnsignedByte;
    }
}

#ifndef DRM_FORMAT_DEPTH16
#define DRM_FORMAT_DEPTH16 fourcc_code('D', '1', '6', ' ')
#define DRM_FORMAT_DEPTH24 fourcc_code('D', '2', '4', 'X')
#define DRM_FORMAT_DEPTH24_STENCIL8 fourcc_code('D', '2', '4', 'S')
#define DRM_FORMAT_DEPTH32 fourcc_code('D', '3', '2', 'F')
#define DRM_FORMAT_DEPTH32_STENCIL8 fourcc_code('D', 'F', 'S', '8')
#endif

static inline uint32_t drm_format_to_virgl_format(uint32_t format) {
    switch (format) {
        case DRM_FORMAT_DEPTH16:
            return VIRGL_FORMAT_Z16_UNORM;
        case DRM_FORMAT_DEPTH24:
            return VIRGL_FORMAT_Z24X8_UNORM;
        case DRM_FORMAT_DEPTH24_STENCIL8:
            return VIRGL_FORMAT_Z24_UNORM_S8_UINT;
        case DRM_FORMAT_DEPTH32:
            return VIRGL_FORMAT_Z32_FLOAT;
        case DRM_FORMAT_DEPTH32_STENCIL8:
            return VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT;
        default:
            GFXSTREAM_ERROR("Unknown drm format for virgl conversion 0x%x", format);
            return 0;
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

static inline void set_drm_format_supported(uint32_t* mask, uint32_t drm_format, bool supported) {
    uint32_t virgl_format = drm_format_to_virgl_format(drm_format);
    set_virgl_format_supported(mask, virgl_format, supported);
}

static inline bool is_drm_format_supported(uint32_t* mask, uint32_t drm_format) {
    uint32_t virgl_format = drm_format_to_virgl_format(drm_format);
    uint32_t index = virgl_format / 32;
    uint32_t bit_offset = 1 << (virgl_format & 31);
    return (mask[index] & bit_offset) ? true : false;
}

static inline size_t virgl_format_to_linear_base(uint32_t format, uint32_t totalWidth,
                                                 uint32_t totalHeight, uint32_t x, uint32_t y,
                                                 uint32_t w, uint32_t h) {
    if (virgl_format_is_yuv(format)) {
        return 0;
    } else {
        uint32_t bpp = 4;
        switch (format) {
            case VIRGL_FORMAT_R16G16B16A16_FLOAT:
            case VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT:
                bpp = 8;
                break;
            case VIRGL_FORMAT_B8G8R8X8_UNORM:
            case VIRGL_FORMAT_B8G8R8A8_UNORM:
            case VIRGL_FORMAT_R8G8B8X8_UNORM:
            case VIRGL_FORMAT_R8G8B8A8_UNORM:
            case VIRGL_FORMAT_R10G10B10A2_UNORM:
            case VIRGL_FORMAT_Z24X8_UNORM:
            case VIRGL_FORMAT_Z24_UNORM_S8_UINT:
            case VIRGL_FORMAT_Z32_FLOAT:
                bpp = 4;
                break;
            case VIRGL_FORMAT_R8G8B8_UNORM:
                bpp = 3;
                break;
            case VIRGL_FORMAT_B5G6R5_UNORM:
            case VIRGL_FORMAT_R8G8_UNORM:
            case VIRGL_FORMAT_R16_UNORM:
            case VIRGL_FORMAT_Z16_UNORM:
                bpp = 2;
                break;
            case VIRGL_FORMAT_R8_UNORM:
                bpp = 1;
                break;
            default:
                GFXSTREAM_ERROR("Unknown virgl format: 0x%x", format);
                return 0;
        }

        uint32_t stride = totalWidth * bpp;
        return y * stride + x * bpp;
    }
    return 0;
}

static inline uint32_t align_up_power_of_2(uint32_t n, uint32_t a) {
    return (n + (a - 1)) & ~(a - 1);
}

static inline size_t virgl_format_to_total_xfer_len(uint32_t format, uint32_t totalWidth,
                                                    uint32_t totalHeight, uint32_t x, uint32_t y,
                                                    uint32_t w, uint32_t h) {
    if (virgl_format_is_yuv(format)) {
        uint32_t bpp = format == VIRGL_FORMAT_P010 ? 2 : 1;

        uint32_t yWidth = totalWidth;
        uint32_t yHeight = totalHeight;
        uint32_t yStridePixels;
        if (format == VIRGL_FORMAT_NV12) {
            yStridePixels = yWidth;
        } else if (format == VIRGL_FORMAT_P010) {
            yStridePixels = yWidth;
        } else if (format == VIRGL_FORMAT_YV12) {
            yStridePixels = align_up_power_of_2(yWidth, 32);
        } else {
            GFXSTREAM_ERROR("Unknown virgl format: 0x%x", format);
            return 0;
        }
        uint32_t yStrideBytes = yStridePixels * bpp;
        uint32_t ySize = yStrideBytes * yHeight;

        uint32_t uvStridePixels;
        uint32_t uvPlaneCount;
        if (format == VIRGL_FORMAT_NV12) {
            uvStridePixels = yStridePixels;
            uvPlaneCount = 1;
        } else if (format == VIRGL_FORMAT_P010) {
            uvStridePixels = yStridePixels;
            uvPlaneCount = 1;
        } else if (format == VIRGL_FORMAT_YV12) {
            uvStridePixels = yStridePixels / 2;
            uvPlaneCount = 2;
        } else {
            GFXSTREAM_ERROR("Unknown virgl yuv format: 0x%x", format);
            return 0;
        }
        uint32_t uvStrideBytes = uvStridePixels * bpp;
        uint32_t uvHeight = totalHeight / 2;
        uint32_t uvSize = uvStrideBytes * uvHeight * uvPlaneCount;

        uint32_t dataSize = ySize + uvSize;
        return dataSize;
    } else {
        uint32_t bpp = 4;
        switch (format) {
            case VIRGL_FORMAT_R16G16B16A16_FLOAT:
            case VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT:
                bpp = 8;
                break;
            case VIRGL_FORMAT_B8G8R8X8_UNORM:
            case VIRGL_FORMAT_B8G8R8A8_UNORM:
            case VIRGL_FORMAT_R8G8B8X8_UNORM:
            case VIRGL_FORMAT_R8G8B8A8_UNORM:
            case VIRGL_FORMAT_R10G10B10A2_UNORM:
            case VIRGL_FORMAT_Z24X8_UNORM:
            case VIRGL_FORMAT_Z24_UNORM_S8_UINT:
            case VIRGL_FORMAT_Z32_FLOAT:
                bpp = 4;
                break;
            case VIRGL_FORMAT_R8G8B8_UNORM:
                bpp = 3;
                break;
            case VIRGL_FORMAT_B5G6R5_UNORM:
            case VIRGL_FORMAT_R16_UNORM:
            case VIRGL_FORMAT_R8G8_UNORM:
            case VIRGL_FORMAT_Z16_UNORM:
                bpp = 2;
                break;
            case VIRGL_FORMAT_R8_UNORM:
                bpp = 1;
                break;
            default:
                GFXSTREAM_ERROR("Unknown virgl format: 0x%x", format);
                return 0;
        }

        uint32_t stride = totalWidth * bpp;
        return (h - 1U) * stride + w * bpp;
    }
    return 0;
}

}  // namespace host
}  // namespace gfxstream
