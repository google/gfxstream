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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gfxstream {
namespace host {

enum class GfxstreamFormat : uint32_t {
    UNKNOWN = 0,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
     *   Vulkan: VK_FORMAT_R8G8B8A8_UNORM
     *   OpenGL ES: GL_RGBA8
     */
    R8G8B8A8_UNORM,

    /**
     * 32 bits per pixel, 8 bits per channel format where alpha values are
     * ignored (always opaque).
     *
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM
     *   Vulkan: VK_FORMAT_R8G8B8A8_UNORM
     *   OpenGL ES: GL_RGB8
     */
    R8G8B8X8_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM
     *   Vulkan: VK_FORMAT_R8G8B8_UNORM
     *   OpenGL ES: GL_RGB8
     */
    R8G8B8_UNORM,

    /**
     * Corresponding formats:
     *   Android: N/A
     *   Vulkan: VK_FORMAT_R8G8_UNORM
     *   OpenGL ES: GL_RG8, GL_RG
     */
    R8G8_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM
     *   Vulkan: VK_FORMAT_R5G6B5_UNORM_PACK16
     *   OpenGL ES: GL_RGB565
     */
    R5G6B5_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM (deprecated)
     *   Vulkan: VK_FORMAT_B8G8R8A8_UNORM
     */
    B8G8R8A8_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_B5G5R5A1_UNORM (deprecated)
     *   Vulkan: VK_FORMAT_B5G5R5A1_UNORM_PACK16
     */
    B5G5R5A1_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_B4G4R4A4_UNORM (deprecated)
     *   Vulkan: VK_FORMAT_B4G4R4A4_UNORM_PACK16
     */
    B4G4R4A4_UNORM,

    /**
     * Corresponding formats:
     *   Android: N/A
     *   Vulkan: VK_FORMAT_R4G4B4A4_UNORM_PACK16
     */
    R4G4B4A4_UNORM,

    /**
     * Corresponding formats:
     *   Android: N/A
     *   Vulkan: VK_FORMAT_R16G16B16_SFLOAT
     *   OpenGL ES: GL_RGB16F
     */
    R16G16B16_FLOAT,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT
     *   Vulkan: VK_FORMAT_R16G16B16A16_SFLOAT
     *   OpenGL ES: GL_RGBA16F
     */
    R16G16B16A16_FLOAT,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM
     *   Vulkan: VK_FORMAT_A2B10G10R10_UNORM_PACK32
     *   OpenGL ES: GL_RGB10_A2
     */
    R10G10B10A2_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM
     *   Vulkan: VK_FORMAT_A2B10G10R10_UNORM_PACK32
     *   OpenGL ES: GL_BGR10_A2_ANGLEX
     */
    B10G10R10A2_UNORM,

    /**
     * An opaque binary blob format that must have height 1, with width equal to
     * the buffer size in bytes.
     *
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_BLOB
     *   Vulkan: N/A
     *   OpenGL ES: N/A
     */
    BLOB,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_D16_UNORM
     *   Vulkan: VK_FORMAT_D16_UNORM
     *   OpenGL ES: GL_DEPTH_COMPONENT16
     */
    D16_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_D24_UNORM
     *   Vulkan: VK_FORMAT_X8_D24_UNORM_PACK32
     *   OpenGL ES: GL_DEPTH_COMPONENT24
     */
    D24_UNORM,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT
     *   Vulkan: VK_FORMAT_D24_UNORM_S8_UINT
     *   OpenGL ES: GL_DEPTH24_STENCIL8
     */
    D24_UNORM_S8_UINT,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_D32_FLOAT
     *   Vulkan: VK_FORMAT_D32_SFLOAT
     *   OpenGL ES: GL_DEPTH_COMPONENT32F
     */
    D32_FLOAT,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT
     *   Vulkan: VK_FORMAT_D32_SFLOAT_S8_UINT
     *   OpenGL ES: GL_DEPTH32F_STENCIL8
     */
    D32_FLOAT_S8_UINT,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT
     *   Vulkan: VK_FORMAT_S8_UINT
     *   OpenGL ES: GL_STENCIL_INDEX8
     */
    S8_UINT,

    /**
     * YV12 is a 4:2:0 YCrCb planar format comprised of a WxH Y plane followed
     * by (W/2) x (H/2) Cr (V) and Cb (U) planes.
     *
     * Corresponding formats:
     *   Android: Potentially used for AHARDWAREBUFFER_FORMAT_YCBCR_420_888
     *   Vulkan: VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM
     *   OpenGL ES: N/A
     */
    YV12,

    /**
     * Same as YV12 but Y, then U, then V.
     *
     * Corresponding formats:
     *   Android: Potentially used for AHARDWAREBUFFER_FORMAT_YCBCR_420_888
     *   Vulkan: VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM
     *   OpenGL ES: N/A
     */
    YV21,

    /**
     * NV12 is a 4:2:0 YCrCb planar format comprised of a WxH Y plane followed
     * by (W/2) x (H/2) Cr and Cb planes.
     *
     * Corresponding formats:
     *   Android: Potentially used for AHARDWAREBUFFER_FORMAT_YCBCR_420_888
     *   Vulkan: VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
     *   OpenGL ES: N/A
     */
    NV12,

    /**
     * NV12 is a 4:2:0 YCrCb planar format comprised of a WxH Y plane followed
     * by (W/2) x (H/2) Cr and Cb planes.
     *
     * Corresponding formats:
     *   Android: Potentially used for AHARDWAREBUFFER_FORMAT_YCBCR_420_888
     *   Vulkan: VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
     *   OpenGL ES: N/A
     */
    NV21,

    /**
     * P010 is a 4:2:0 YCbCr semiplanar format comprised of a WxH Y plane
     * followed by a Wx(H/2) CbCr plane. Each sample is represented by a 16-bit
     * little-endian value, with the lower 6 bits set to zero.
     *
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_YCBCR_P010
     *   Vulkan: VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16
     *   OpenGL ES: N/A
     */
    P010,

    /**
     * Corresponding formats:
     *   Android: AHARDWAREBUFFER_FORMAT_R8_UNORM
     *   Vulkan: VK_FORMAT_R8_UNORM
     *   OpenGL ES: GL_R8
     */
    R8_UNORM,

    /**
     * Corresponding formats:
     *   Android: N/A
     *   Vulkan: VK_FORMAT_R16_UNORM
     *   OpenGL ES: GL_R16_EXT
     */
    R16_UNORM,
};

inline bool IsYuvFormat(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::NV12:
        case GfxstreamFormat::NV21:
        case GfxstreamFormat::P010:
        case GfxstreamFormat::YV21:
        case GfxstreamFormat::YV12:
            return true;
        case GfxstreamFormat::B10G10R10A2_UNORM:
        case GfxstreamFormat::B4G4R4A4_UNORM:
        case GfxstreamFormat::B5G5R5A1_UNORM:
        case GfxstreamFormat::B8G8R8A8_UNORM:
        case GfxstreamFormat::BLOB:
        case GfxstreamFormat::D16_UNORM:
        case GfxstreamFormat::D24_UNORM_S8_UINT:
        case GfxstreamFormat::D24_UNORM:
        case GfxstreamFormat::D32_FLOAT_S8_UINT:
        case GfxstreamFormat::D32_FLOAT:
        case GfxstreamFormat::R4G4B4A4_UNORM:
        case GfxstreamFormat::R10G10B10A2_UNORM:
        case GfxstreamFormat::R16_UNORM:
        case GfxstreamFormat::R16G16B16_FLOAT:
        case GfxstreamFormat::R16G16B16A16_FLOAT:
        case GfxstreamFormat::R5G6B5_UNORM:
        case GfxstreamFormat::R8_UNORM:
        case GfxstreamFormat::R8G8_UNORM:
        case GfxstreamFormat::R8G8B8_UNORM:
        case GfxstreamFormat::R8G8B8A8_UNORM:
        case GfxstreamFormat::R8G8B8X8_UNORM:
        case GfxstreamFormat::S8_UINT:
        case GfxstreamFormat::UNKNOWN:
            return false;
    }
}

inline bool IsInterleavedChromaYuvFormat(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::NV12:
        case GfxstreamFormat::NV21:
        case GfxstreamFormat::P010:
            return true;
        case GfxstreamFormat::B10G10R10A2_UNORM:
        case GfxstreamFormat::B4G4R4A4_UNORM:
        case GfxstreamFormat::B5G5R5A1_UNORM:
        case GfxstreamFormat::B8G8R8A8_UNORM:
        case GfxstreamFormat::BLOB:
        case GfxstreamFormat::D16_UNORM:
        case GfxstreamFormat::D24_UNORM_S8_UINT:
        case GfxstreamFormat::D24_UNORM:
        case GfxstreamFormat::D32_FLOAT_S8_UINT:
        case GfxstreamFormat::D32_FLOAT:
        case GfxstreamFormat::R4G4B4A4_UNORM:
        case GfxstreamFormat::R10G10B10A2_UNORM:
        case GfxstreamFormat::R16_UNORM:
        case GfxstreamFormat::R16G16B16_FLOAT:
        case GfxstreamFormat::R16G16B16A16_FLOAT:
        case GfxstreamFormat::R5G6B5_UNORM:
        case GfxstreamFormat::R8_UNORM:
        case GfxstreamFormat::R8G8_UNORM:
        case GfxstreamFormat::R8G8B8_UNORM:
        case GfxstreamFormat::R8G8B8A8_UNORM:
        case GfxstreamFormat::R8G8B8X8_UNORM:
        case GfxstreamFormat::S8_UINT:
        case GfxstreamFormat::UNKNOWN:
        case GfxstreamFormat::YV21:
        case GfxstreamFormat::YV12:
            return false;
    }
}

enum class YuvChromaOrdering {
    UNKNOWN,
    UV,
    VU,
};

inline std::optional<YuvChromaOrdering> GetYuvChromaOrdering(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::NV12:
        case GfxstreamFormat::YV21:
            return YuvChromaOrdering::UV;
        case GfxstreamFormat::YV12:
        case GfxstreamFormat::NV21:
        case GfxstreamFormat::P010:
            return YuvChromaOrdering::VU;
        case GfxstreamFormat::B10G10R10A2_UNORM:
        case GfxstreamFormat::B4G4R4A4_UNORM:
        case GfxstreamFormat::B5G5R5A1_UNORM:
        case GfxstreamFormat::B8G8R8A8_UNORM:
        case GfxstreamFormat::BLOB:
        case GfxstreamFormat::D16_UNORM:
        case GfxstreamFormat::D24_UNORM_S8_UINT:
        case GfxstreamFormat::D24_UNORM:
        case GfxstreamFormat::D32_FLOAT_S8_UINT:
        case GfxstreamFormat::D32_FLOAT:
        case GfxstreamFormat::R4G4B4A4_UNORM:
        case GfxstreamFormat::R10G10B10A2_UNORM:
        case GfxstreamFormat::R16_UNORM:
        case GfxstreamFormat::R16G16B16_FLOAT:
        case GfxstreamFormat::R16G16B16A16_FLOAT:
        case GfxstreamFormat::R5G6B5_UNORM:
        case GfxstreamFormat::R8_UNORM:
        case GfxstreamFormat::R8G8_UNORM:
        case GfxstreamFormat::R8G8B8_UNORM:
        case GfxstreamFormat::R8G8B8A8_UNORM:
        case GfxstreamFormat::R8G8B8X8_UNORM:
        case GfxstreamFormat::S8_UINT:
        case GfxstreamFormat::UNKNOWN:
            return std::nullopt;
    }
}

enum class YuvPlane {
    UNKNOWN,
    Y,
    U,
    V,
    UV,
};

inline std::string ToString(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::B10G10R10A2_UNORM:
            return "B10G10R10A2_UNORM";
        case GfxstreamFormat::B4G4R4A4_UNORM:
            return "B4G4R4A4_UNORM";
        case GfxstreamFormat::B5G5R5A1_UNORM:
            return "B5G5R5A1_UNORM";
        case GfxstreamFormat::B8G8R8A8_UNORM:
            return "B8G8R8A8_UNORM";
        case GfxstreamFormat::BLOB:
            return "BLOB";
        case GfxstreamFormat::D16_UNORM:
            return "D16_UNORM";
        case GfxstreamFormat::D24_UNORM_S8_UINT:
            return "D24_UNORM_S8_UINT";
        case GfxstreamFormat::D24_UNORM:
            return "D24_UNORM";
        case GfxstreamFormat::D32_FLOAT_S8_UINT:
            return "D32_FLOAT_S8_UINT";
        case GfxstreamFormat::D32_FLOAT:
            return "D32_FLOAT";
        case GfxstreamFormat::NV12:
            return "NV12";
        case GfxstreamFormat::NV21:
            return "NV21";
        case GfxstreamFormat::P010:
            return "P010";
        case GfxstreamFormat::R4G4B4A4_UNORM:
            return "R4G4B4A4_UNORM";
        case GfxstreamFormat::R10G10B10A2_UNORM:
            return "R10G10B10A2_UNORM";
        case GfxstreamFormat::R16_UNORM:
            return "R16_UNORM";
        case GfxstreamFormat::R16G16B16_FLOAT:
            return "R16G16B16_FLOAT";
        case GfxstreamFormat::R16G16B16A16_FLOAT:
            return "R16G16B16A16_FLOAT";
        case GfxstreamFormat::R5G6B5_UNORM:
            return "R5G6B5_UNORM";
        case GfxstreamFormat::R8_UNORM:
            return "R8_UNORM";
        case GfxstreamFormat::R8G8_UNORM:
            return "R8G8_UNORM";
        case GfxstreamFormat::R8G8B8_UNORM:
            return "R8G8B8_UNORM";
        case GfxstreamFormat::R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case GfxstreamFormat::R8G8B8X8_UNORM:
            return "R8G8B8X8_UNORM";
        case GfxstreamFormat::S8_UINT:
            return "S8_UINT";
        case GfxstreamFormat::UNKNOWN:
            return "UNKNOWN";
        case GfxstreamFormat::YV21:
            return "YV21";
        case GfxstreamFormat::YV12:
            return "YV12";
    }
}

inline std::optional<uint32_t> GetBpp(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::B10G10R10A2_UNORM:
            return 4;
        case GfxstreamFormat::B4G4R4A4_UNORM:
            return 2;
        case GfxstreamFormat::B5G5R5A1_UNORM:
            return 2;
        case GfxstreamFormat::B8G8R8A8_UNORM:
            return 4;
        case GfxstreamFormat::BLOB:
            return 1;
        case GfxstreamFormat::D16_UNORM:
            return 2;
        case GfxstreamFormat::D24_UNORM_S8_UINT:
            return 4;
        case GfxstreamFormat::D24_UNORM:
            return 4;
        case GfxstreamFormat::D32_FLOAT_S8_UINT:
            return 8;
        case GfxstreamFormat::D32_FLOAT:
            return 4;
        case GfxstreamFormat::NV12:
            return std::nullopt;
        case GfxstreamFormat::NV21:
            return std::nullopt;
        case GfxstreamFormat::P010:
            return std::nullopt;
        case GfxstreamFormat::R10G10B10A2_UNORM:
            return 4;
        case GfxstreamFormat::R16_UNORM:
            return 2;
        case GfxstreamFormat::R16G16B16_FLOAT:
            return 6;
        case GfxstreamFormat::R16G16B16A16_FLOAT:
            return 8;
        case GfxstreamFormat::R4G4B4A4_UNORM:
            return 2;
        case GfxstreamFormat::R5G6B5_UNORM:
            return 2;
        case GfxstreamFormat::R8_UNORM:
            return 1;
        case GfxstreamFormat::R8G8_UNORM:
            return 2;
        case GfxstreamFormat::R8G8B8_UNORM:
            return 3;
        case GfxstreamFormat::R8G8B8A8_UNORM:
            return 4;
        case GfxstreamFormat::R8G8B8X8_UNORM:
            return 4;
        case GfxstreamFormat::S8_UINT:
            return 1;
        case GfxstreamFormat::UNKNOWN:
            return std::nullopt;
        case GfxstreamFormat::YV21:
            return std::nullopt;
        case GfxstreamFormat::YV12:
            return std::nullopt;
    }
}

}  // host
}  // gfxstream
