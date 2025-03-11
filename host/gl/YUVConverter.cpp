/*
* Copyright (C) 2016 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "YUVConverter.h"

#include <assert.h>
#include <stdio.h>

#include <string>

#include "OpenGLESDispatch/DispatchTables.h"
#include "gfxstream/host/logging.h"
#include "host-common/feature_control.h"
#include "host-common/opengl/misc.h"

namespace gfxstream {
namespace gl {

#define YUV_CONVERTER_DEBUG 0

#if YUV_CONVERTER_DEBUG
#define YUV_DEBUG_LOG(fmt, ...)                                                        \
    fprintf(stderr, "yuv-converter: %s %s:%d " fmt "\n", __FILE__, __func__, __LINE__, \
            ##__VA_ARGS__);
#else
#define YUV_DEBUG_LOG(fmt, ...)
#endif

bool isInterleaved(FrameworkFormat format, bool yuv420888ToNv21) {
    switch (format) {
    case FRAMEWORK_FORMAT_NV12:
    case FRAMEWORK_FORMAT_P010:
        return true;
    case FRAMEWORK_FORMAT_YUV_420_888:
        return yuv420888ToNv21;
    case FRAMEWORK_FORMAT_YV12:
        return false;
    default:
        GFXSTREAM_FATAL("Invalid for format:%d", format);
        return false;
    }
}

enum class YUVInterleaveDirection {
    VU = 0,
    UV = 1,
};

YUVInterleaveDirection getInterleaveDirection(FrameworkFormat format, bool yuv420888ToNv21) {
    if (!isInterleaved(format, yuv420888ToNv21)) {
        GFXSTREAM_FATAL("Format:%d not interleaved", format);
    }

    switch (format) {
    case FRAMEWORK_FORMAT_NV12:
    case FRAMEWORK_FORMAT_P010:
        return YUVInterleaveDirection::UV;
    case FRAMEWORK_FORMAT_YUV_420_888:
        if (yuv420888ToNv21) {
            return YUVInterleaveDirection::VU;
        }
        GFXSTREAM_FATAL("Format:%d not interleaved", format);
        return YUVInterleaveDirection::UV;
    case FRAMEWORK_FORMAT_YV12:
    default:
        GFXSTREAM_FATAL("Format:%d not interleaved", format);
        return YUVInterleaveDirection::UV;
    }
}

GLint getGlTextureFormat(FrameworkFormat format, bool yuv420888ToNv21, YUVPlane plane) {
    switch (format) {
    case FRAMEWORK_FORMAT_YV12:
        switch (plane) {
        case YUVPlane::Y:
        case YUVPlane::U:
        case YUVPlane::V:
            return GL_R8;
        case YUVPlane::UV:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    case FRAMEWORK_FORMAT_YUV_420_888:
        if (yuv420888ToNv21) {
            switch (plane) {
            case YUVPlane::Y:
                return GL_R8;
            case YUVPlane::UV:
                return GL_RG8;
            case YUVPlane::U:
            case YUVPlane::V:
                GFXSTREAM_FATAL("Invalid plane for format:%d", format);
                return 0;
            }
        } else {
            switch (plane) {
            case YUVPlane::Y:
            case YUVPlane::U:
            case YUVPlane::V:
                return GL_R8;
            case YUVPlane::UV:
                GFXSTREAM_FATAL("Invalid plane for format:%d", format);
                return 0;
            }
        }
    case FRAMEWORK_FORMAT_NV12:
        switch (plane) {
        case YUVPlane::Y:
            return GL_R8;
        case YUVPlane::UV:
            return GL_RG8;
        case YUVPlane::U:
        case YUVPlane::V:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    case FRAMEWORK_FORMAT_P010:
        switch (plane) {
        case YUVPlane::Y:
            return GL_R16UI;
        case YUVPlane::UV:
            return GL_RG16UI;
        case YUVPlane::U:
        case YUVPlane::V:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    default:
        GFXSTREAM_FATAL("Invalid format:%d", format);
        return 0;
    }
}

GLenum getGlPixelFormat(FrameworkFormat format, bool yuv420888ToNv21, YUVPlane plane) {
    switch (format) {
    case FRAMEWORK_FORMAT_YV12:
        switch (plane) {
        case YUVPlane::Y:
        case YUVPlane::U:
        case YUVPlane::V:
            return GL_RED;
        case YUVPlane::UV:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    case FRAMEWORK_FORMAT_YUV_420_888:
        if (yuv420888ToNv21) {
            switch (plane) {
            case YUVPlane::Y:
                return GL_RED;
            case YUVPlane::UV:
                return GL_RG;
            case YUVPlane::U:
            case YUVPlane::V:
                GFXSTREAM_FATAL("Invalid plane for format:%d", format);
                return 0;
            }
        } else {
            switch (plane) {
            case YUVPlane::Y:
            case YUVPlane::U:
            case YUVPlane::V:
                return GL_RED;
            case YUVPlane::UV:
                GFXSTREAM_FATAL("Invalid plane for format:%d", format);
                return 0;
            }
        }
    case FRAMEWORK_FORMAT_NV12:
        switch (plane) {
        case YUVPlane::Y:
            return GL_RED;
        case YUVPlane::UV:
            return GL_RG;
        case YUVPlane::U:
        case YUVPlane::V:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    case FRAMEWORK_FORMAT_P010:
        switch (plane) {
        case YUVPlane::Y:
            return GL_RED_INTEGER;
        case YUVPlane::UV:
            return GL_RG_INTEGER;
        case YUVPlane::U:
        case YUVPlane::V:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    default:
        GFXSTREAM_FATAL("Invalid format:%d", format);
        return 0;
    }
}

GLsizei getGlPixelType(FrameworkFormat format, bool yuv420888ToNv21, YUVPlane plane) {
    switch (format) {
    case FRAMEWORK_FORMAT_YV12:
        switch (plane) {
        case YUVPlane::Y:
        case YUVPlane::U:
        case YUVPlane::V:
            return GL_UNSIGNED_BYTE;
        case YUVPlane::UV:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    case FRAMEWORK_FORMAT_YUV_420_888:
        if (yuv420888ToNv21) {
            switch (plane) {
            case YUVPlane::Y:
            case YUVPlane::UV:
                return GL_UNSIGNED_BYTE;
            case YUVPlane::U:
            case YUVPlane::V:
                GFXSTREAM_FATAL("Invalid plane for format:%d", format);
                return 0;
            }
        } else {
            switch (plane) {
            case YUVPlane::Y:
            case YUVPlane::U:
            case YUVPlane::V:
                return GL_UNSIGNED_BYTE;
            case YUVPlane::UV:
                GFXSTREAM_FATAL("Invalid plane for format:%d", format);
                return 0;
            }
        }
    case FRAMEWORK_FORMAT_NV12:
        switch (plane) {
        case YUVPlane::Y:
        case YUVPlane::UV:
            return GL_UNSIGNED_BYTE;
        case YUVPlane::U:
        case YUVPlane::V:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    case FRAMEWORK_FORMAT_P010:
        switch (plane) {
        case YUVPlane::Y:
        case YUVPlane::UV:
            return GL_UNSIGNED_SHORT;
        case YUVPlane::U:
        case YUVPlane::V:
            GFXSTREAM_FATAL("Invalid plane for format:%d", format);
            return 0;
        }
    default:
        GFXSTREAM_FATAL("Invalid format:%d", format);
        return 0;
    }
}

// NV12 and YUV420 are all packed
static void NV12ToYUV420PlanarInPlaceConvert(int nWidth,
                                             int nHeight,
                                             uint8_t* pFrame,
                                             uint8_t* pQuad) {
    std::vector<uint8_t> tmp;
    if (pQuad == nullptr) {
        tmp.resize(nWidth * nHeight / 4);
        pQuad = tmp.data();
    }
    int nPitch = nWidth;
    uint8_t *puv = pFrame + nPitch * nHeight, *pu = puv,
            *pv = puv + nPitch * nHeight / 4;
    for (int y = 0; y < nHeight / 2; y++) {
        for (int x = 0; x < nWidth / 2; x++) {
            pu[y * nPitch / 2 + x] = puv[y * nPitch + x * 2];
            pQuad[y * nWidth / 2 + x] = puv[y * nPitch + x * 2 + 1];
        }
    }
    memcpy(pv, pQuad, nWidth * nHeight / 4);
}

inline uint32_t alignToPower2(uint32_t val, uint32_t align) {
    return (val + (align - 1)) & ~(align - 1);
}

// getYUVOffsets(), given a YUV-formatted buffer that is arranged
// according to the spec
// https://developer.android.com/reference/android/graphics/ImageFormat.html#YUV
// In particular, Android YUV widths are aligned to 16 pixels.
// Inputs:
// |yv12|: the YUV-formatted buffer
// Outputs:
// |yOffsetBytes|: offset into |yv12| of the start of the Y component
// |uOffsetBytes|: offset into |yv12| of the start of the U component
// |vOffsetBytes|: offset into |yv12| of the start of the V component
static void getYUVOffsets(int width,
                          int height,
                          FrameworkFormat format,
                          bool yuv420888ToNv21,
                          uint32_t* yWidth,
                          uint32_t* yHeight,
                          uint32_t* yOffsetBytes,
                          uint32_t* yStridePixels,
                          uint32_t* yStrideBytes,
                          uint32_t* uWidth,
                          uint32_t* uHeight,
                          uint32_t* uOffsetBytes,
                          uint32_t* uStridePixels,
                          uint32_t* uStrideBytes,
                          uint32_t* vWidth,
                          uint32_t* vHeight,
                          uint32_t* vOffsetBytes,
                          uint32_t* vStridePixels,
                          uint32_t* vStrideBytes) {
    switch (format) {
        case FRAMEWORK_FORMAT_YV12: {
            *yWidth = width;
            *yHeight = height;
            *yOffsetBytes = 0;
            // Luma stride is 32 bytes aligned in minigbm, 16 in goldfish
            // gralloc.
            *yStridePixels = alignToPower2(width, emugl::getGrallocImplementation() == MINIGBM
                    ? 32 : 16);
            *yStrideBytes = *yStridePixels;

            // Chroma stride is 16 bytes aligned.
            *vWidth = width / 2;
            *vHeight = height / 2;
            *vOffsetBytes = (*yStrideBytes) * (*yHeight);
            *vStridePixels = alignToPower2((*yStridePixels) / 2, 16);
            *vStrideBytes = (*vStridePixels);

            *uWidth = width / 2;
            *uHeight = height / 2;
            *uOffsetBytes = (*vOffsetBytes) + ((*vStrideBytes) * (*vHeight));
            *uStridePixels = alignToPower2((*yStridePixels) / 2, 16);
            *uStrideBytes = *uStridePixels;
            break;
        }
        case FRAMEWORK_FORMAT_YUV_420_888: {
            if (yuv420888ToNv21) {
                *yWidth = width;
                *yHeight = height;
                *yOffsetBytes = 0;
                *yStridePixels = width;
                *yStrideBytes = *yStridePixels;

                *vWidth = width / 2;
                *vHeight = height / 2;
                *vOffsetBytes = (*yStrideBytes) * (*yHeight);
                *vStridePixels = (*yStridePixels) / 2;
                *vStrideBytes = (*vStridePixels);

                *uWidth = width / 2;
                *uHeight = height / 2;
                *uOffsetBytes = (*vOffsetBytes) + 1;
                *uStridePixels = (*yStridePixels) / 2;
                *uStrideBytes = *uStridePixels;
            } else {
                *yWidth = width;
                *yHeight = height;
                *yOffsetBytes = 0;
                *yStridePixels = width;
                *yStrideBytes = *yStridePixels;

                *uWidth = width / 2;
                *uHeight = height / 2;
                *uOffsetBytes = (*yStrideBytes) * (*yHeight);
                *uStridePixels = (*yStridePixels) / 2;
                *uStrideBytes = *uStridePixels;

                *vWidth = width / 2;
                *vHeight = height / 2;
                *vOffsetBytes = (*uOffsetBytes) + ((*uStrideBytes) * (*uHeight));
                *vStridePixels = (*yStridePixels) / 2;
                *vStrideBytes = (*vStridePixels);
            }
            break;
        }
        case FRAMEWORK_FORMAT_NV12: {
            *yWidth = width;
            *yHeight = height;
            *yOffsetBytes = 0;
            *yStridePixels = width;
            *yStrideBytes = *yStridePixels;

            *uWidth = width / 2;
            *uHeight = height / 2;
            *uOffsetBytes = (*yStrideBytes) * (*yHeight);
            *uStridePixels = (*yStridePixels) / 2;
            *uStrideBytes = *uStridePixels;

            *vWidth = width / 2;
            *vHeight = height / 2;
            *vOffsetBytes = (*uOffsetBytes) + 1;
            *vStridePixels = (*yStridePixels) / 2;
            *vStrideBytes = (*vStridePixels);
            break;
        }
        case FRAMEWORK_FORMAT_P010: {
            *yWidth = width;
            *yHeight = height;
            *yOffsetBytes = 0;
            *yStridePixels = width;
            *yStrideBytes = (*yStridePixels) * /*bytes per pixel=*/2;

            *uWidth = width / 2;
            *uHeight = height / 2;
            *uOffsetBytes = (*yStrideBytes) * (*yHeight);
            *uStridePixels = (*uWidth);
            *uStrideBytes = *uStridePixels  * /*bytes per pixel=*/2;

            *vWidth = width / 2;
            *vHeight = height / 2;
            *vOffsetBytes = (*uOffsetBytes) + 2;
            *vStridePixels = (*vWidth);
            *vStrideBytes = (*vStridePixels)  * /*bytes per pixel=*/2;
            break;
        }
        case FRAMEWORK_FORMAT_GL_COMPATIBLE: {
            GFXSTREAM_FATAL("Input not a YUV format! (FRAMEWORK_FORMAT_GL_COMPATIBLE)");
            break;
        }
        default: {
            GFXSTREAM_FATAL("Unknown format: 0x%x", format);
            break;
        }
    }
}

// Allocates an OpenGL texture that is large enough for a single plane of
// a YUV buffer of the given format and returns the texture name in the
// `outTextureName` argument.
void YUVConverter::createYUVGLTex(GLenum textureUnit,
                                  GLsizei width,
                                  GLsizei height,
                                  FrameworkFormat format,
                                  bool yuv420888ToNv21,
                                  YUVPlane plane,
                                  GLuint* outTextureName) {
    YUV_DEBUG_LOG("w:%d h:%d format:%d plane:%d", width, height, format, plane);

    s_gles2.glActiveTexture(textureUnit);
    s_gles2.glGenTextures(1, outTextureName);
    s_gles2.glBindTexture(GL_TEXTURE_2D, *outTextureName);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GLint unprevAlignment = 0;
    s_gles2.glGetIntegerv(GL_UNPACK_ALIGNMENT, &unprevAlignment);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLint textureFormat = getGlTextureFormat(format, yuv420888ToNv21, plane);
    const GLenum pixelFormat = getGlPixelFormat(format, yuv420888ToNv21, plane);
    const GLenum pixelType = getGlPixelType(format, yuv420888ToNv21, plane);
    s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, textureFormat, width, height, 0, pixelFormat, pixelType, NULL);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, unprevAlignment);
    s_gles2.glActiveTexture(GL_TEXTURE0);
}

static void readYUVTex(GLuint tex, FrameworkFormat format, bool yuv420888ToNv21,
                       YUVPlane plane, void* pixels, uint32_t pixelsStride) {
    YUV_DEBUG_LOG("format%d plane:%d pixels:%p", format, plane, pixels);

    GLuint prevTexture = 0;
    s_gles2.glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint*)&prevTexture);
    s_gles2.glBindTexture(GL_TEXTURE_2D, tex);
    GLint prevAlignment = 0;
    s_gles2.glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlignment);
    s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    GLint prevStride = 0;
    s_gles2.glGetIntegerv(GL_PACK_ROW_LENGTH, &prevStride);
    s_gles2.glPixelStorei(GL_PACK_ROW_LENGTH, pixelsStride);

    const GLenum pixelFormat = getGlPixelFormat(format, yuv420888ToNv21, plane);
    const GLenum pixelType = getGlPixelType(format, yuv420888ToNv21,plane);
    if (s_gles2.glGetTexImage) {
        s_gles2.glGetTexImage(GL_TEXTURE_2D, 0, pixelFormat, pixelType, pixels);
    } else {
        YUV_DEBUG_LOG("empty glGetTexImage");
    }

    s_gles2.glPixelStorei(GL_PACK_ROW_LENGTH, prevStride);
    s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, prevAlignment);
    s_gles2.glBindTexture(GL_TEXTURE_2D, prevTexture);
}

// Updates a given YUV buffer's plane texture at the coordinates
// (x, y, width, height), with the raw YUV data in |pixels|.  We
// cannot view the result properly until after conversion; this is
// to be used only as input to the conversion shader.
static void subUpdateYUVGLTex(GLenum texture_unit,
                              GLuint tex,
                              int x,
                              int y,
                              int width,
                              int height,
                              FrameworkFormat format,
                              bool yuv420888ToNv21,
                              YUVPlane plane,
                              const void* pixels) {
    YUV_DEBUG_LOG("x:%d y:%d w:%d h:%d format:%d plane:%d", x, y, width, height, format, plane);

    const GLenum pixelFormat = getGlPixelFormat(format, yuv420888ToNv21, plane);
    const GLenum pixelType = getGlPixelType(format, yuv420888ToNv21, plane);

    s_gles2.glActiveTexture(texture_unit);
    s_gles2.glBindTexture(GL_TEXTURE_2D, tex);
    GLint unprevAlignment = 0;
    s_gles2.glGetIntegerv(GL_UNPACK_ALIGNMENT, &unprevAlignment);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    s_gles2.glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, pixelFormat, pixelType, pixels);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, unprevAlignment);
    s_gles2.glActiveTexture(GL_TEXTURE0);
}

bool YUVConverter::checkAndUpdateColorAspectsChanged(void* metadata) {
    bool needToUpdateConversionShader = false;
    if (metadata) {
        uint64_t type = *(uint64_t*)(metadata);
        uint8_t* pmetadata = (uint8_t*)(metadata);
        if (type == 1) {
            uint64_t primaries = *(uint64_t*)(pmetadata + 8);
            uint64_t range = *(uint64_t*)(pmetadata + 16);
            uint64_t transfer = *(uint64_t*)(pmetadata + 24);
            if (primaries != mColorPrimaries || range != mColorRange ||
                transfer != mColorTransfer) {
                mColorPrimaries = primaries;
                mColorRange = range;
                mColorTransfer = transfer;
                needToUpdateConversionShader = true;
            }
        }
    }

    return needToUpdateConversionShader;
}

void YUVConverter::createYUVGLShader() {
    YUV_DEBUG_LOG("format:%d", mFormat);

    // P010 needs uint samplers.
    if (mFormat == FRAMEWORK_FORMAT_P010 && !mHasGlsl3Support) {
        return;
    }

    static const char kVertShader[] = R"(
precision highp float;
attribute mediump vec4 aPosition;
attribute highp vec2 aTexCoord;
varying highp vec2 vTexCoord;
void main(void) {
  gl_Position = aPosition;
  vTexCoord = aTexCoord;
}
    )";

    static const char kFragShaderVersion3[] = R"(#version 300 es)";

    static const char kFragShaderBegin[] = R"(
precision highp float;

varying highp vec2 vTexCoord;

uniform highp float uYWidthCutoff;
uniform highp float uUVWidthCutoff;
    )";
    static const char kFragShaderBeginVersion3[] = R"(
precision highp float;

layout (location = 0) out vec4 FragColor;
in highp vec2 vTexCoord;

uniform highp float uYWidthCutoff;
uniform highp float uUVWidthCutoff;
    )";

    static const char kSamplerUniforms[] = R"(
uniform sampler2D uSamplerY;
uniform sampler2D uSamplerU;
uniform sampler2D uSamplerV;
    )";
    static const char kSamplerUniformsUint[] = R"(
uniform highp usampler2D uSamplerY;
uniform highp usampler2D uSamplerU;
uniform highp usampler2D uSamplerV;
    )";

    static const char kFragShaderMainBegin[] = R"(
void main(void) {
    highp vec2 yTexCoords = vTexCoord;
    highp vec2 uvTexCoords = vTexCoord;

    // For textures with extra padding for alignment (e.g. YV12 pads to 16),
    // scale the coordinates to only sample from the non-padded area.
    yTexCoords.x *= uYWidthCutoff;
    uvTexCoords.x *= uUVWidthCutoff;

    highp vec3 yuv;
)";

    static const char kSampleY[] = R"(
    yuv[0] = texture2D(uSamplerY, yTexCoords).r;
    )";
    static const char kSampleUV[] = R"(
    yuv[1] = texture2D(uSamplerU, uvTexCoords).r;
    yuv[2] = texture2D(uSamplerV, uvTexCoords).r;
    )";
    static const char kSampleInterleavedUV[] = R"(
    // Note: uSamplerU and vSamplerV refer to the same texture.
    yuv[1] = texture2D(uSamplerU, uvTexCoords).r;
    yuv[2] = texture2D(uSamplerV, uvTexCoords).g;
    )";
    static const char kSampleInterleavedVU[] = R"(
    // Note: uSamplerU and vSamplerV refer to the same texture.
    yuv[1] = texture2D(uSamplerU, uvTexCoords).g;
    yuv[2] = texture2D(uSamplerV, uvTexCoords).r;
    )";

    static const char kSampleP010[] = R"(
        uint yRaw = texture(uSamplerY, yTexCoords).r;
        uint uRaw = texture(uSamplerU, uvTexCoords).r;
        uint vRaw = texture(uSamplerV, uvTexCoords).g;

        // P010 values are stored in the upper 10-bits of 16-bit unsigned shorts.
        yuv[0] = float(yRaw >> 6) / 1023.0;
        yuv[1] = float(uRaw >> 6) / 1023.0;
        yuv[2] = float(vRaw >> 6) / 1023.0;
    )";

    // default
    // limited range (2) 601 (4) sRGB transfer (3)
    static const char kFragShaderMain_2_4_3[] = R"(
    yuv[0] = yuv[0] - 0.0625;
    yuv[1] = (yuv[1] - 0.5);
    yuv[2] = (yuv[2] - 0.5);

    highp float yscale = 1.1643835616438356;
    highp vec3 rgb = mat3(            yscale,               yscale,            yscale,
                                           0, -0.39176229009491365, 2.017232142857143,
                          1.5960267857142856,  -0.8129676472377708,                 0) * yuv;

    )";

    // full range (1) 601 (4) sRGB transfer (3)
    static const char kFragShaderMain_1_4_3[] = R"(
    yuv[0] = yuv[0];
    yuv[1] = (yuv[1] - 0.5);
    yuv[2] = (yuv[2] - 0.5);

    highp float yscale = 1.0;
    highp vec3 rgb = mat3(            yscale,               yscale,            yscale,
                                           0, -0.344136* yscale, 1.772* yscale,
                          yscale*1.402,  -0.714136* yscale,                 0) * yuv;

    )";

    // limited range (2) 709 (1) sRGB transfer (3)
    static const char kFragShaderMain_2_1_3[] = R"(
    highp float xscale = 219.0/ 224.0;
    yuv[0] = yuv[0] - 0.0625;
    yuv[1] = xscale* (yuv[1] - 0.5);
    yuv[2] = xscale* (yuv[2] - 0.5);

    highp float yscale = 255.0/219.0;
    highp vec3 rgb = mat3(            yscale,               yscale,            yscale,
                                           0, -0.1873* yscale, 1.8556* yscale,
                          yscale*1.5748,  -0.4681* yscale,                 0) * yuv;

    )";

    static const char kFragShaderMainEnd[] = R"(
    gl_FragColor = vec4(rgb, 1.0);
}
    )";

    static const char kFragShaderMainEndVersion3[] = R"(
    FragColor = vec4(rgb, 1.0);
}
    )";
    std::string vertShaderSource(kVertShader);
    std::string fragShaderSource;

    if (mFormat == FRAMEWORK_FORMAT_P010) {
        fragShaderSource += kFragShaderVersion3;
        fragShaderSource += kFragShaderBeginVersion3;
    } else {
        fragShaderSource += kFragShaderBegin;
    }

    if (mFormat == FRAMEWORK_FORMAT_P010) {
        fragShaderSource += kSamplerUniformsUint;
    } else {
        fragShaderSource += kSamplerUniforms;
    }

    fragShaderSource += kFragShaderMainBegin;

    switch (mFormat) {
    case FRAMEWORK_FORMAT_NV12:
    case FRAMEWORK_FORMAT_YUV_420_888:
    case FRAMEWORK_FORMAT_YV12:
        fragShaderSource += kSampleY;
        if (isInterleaved(mFormat, mYuv420888ToNv21)) {
            if (getInterleaveDirection(mFormat, mYuv420888ToNv21) == YUVInterleaveDirection::UV) {
                fragShaderSource += kSampleInterleavedUV;
            } else {
                fragShaderSource += kSampleInterleavedVU;
            }
        } else {
            fragShaderSource += kSampleUV;
        }
        break;
    case FRAMEWORK_FORMAT_P010:
        fragShaderSource += kSampleP010;
        break;
    default:
        GFXSTREAM_FATAL("%s: invalid format:%d", __FUNCTION__, mFormat);
        return;
    }

    if (mColorRange == 1 && mColorPrimaries == 4) {
        fragShaderSource += kFragShaderMain_1_4_3;
    } else if (mColorRange == 2 && mColorPrimaries == 1) {
        fragShaderSource += kFragShaderMain_2_1_3;
    } else {
        fragShaderSource += kFragShaderMain_2_4_3;
    }

    if (mFormat == FRAMEWORK_FORMAT_P010) {
        fragShaderSource += kFragShaderMainEndVersion3;
    } else {
        fragShaderSource += kFragShaderMainEnd;
    }

    YUV_DEBUG_LOG("format:%d vert-source:%s frag-source:%s", mFormat, vertShaderSource.c_str(), fragShaderSource.c_str());

    const GLchar* const vertShaderSourceChars = vertShaderSource.c_str();
    const GLchar* const fragShaderSourceChars = fragShaderSource.c_str();
    const GLint vertShaderSourceLen = vertShaderSource.length();
    const GLint fragShaderSourceLen = fragShaderSource.length();

    GLuint vertShader = s_gles2.glCreateShader(GL_VERTEX_SHADER);
    GLuint fragShader = s_gles2.glCreateShader(GL_FRAGMENT_SHADER);
    s_gles2.glShaderSource(vertShader, 1, &vertShaderSourceChars, &vertShaderSourceLen);
    s_gles2.glShaderSource(fragShader, 1, &fragShaderSourceChars, &fragShaderSourceLen);
    s_gles2.glCompileShader(vertShader);
    s_gles2.glCompileShader(fragShader);

    for (GLuint shader : {vertShader, fragShader}) {
        GLint status = GL_FALSE;
        s_gles2.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == GL_FALSE) {
            GLchar error[1024];
            s_gles2.glGetShaderInfoLog(shader, sizeof(error), nullptr, &error[0]);
            GFXSTREAM_FATAL("Failed to compile YUV conversion shader: %s", error);
            s_gles2.glDeleteShader(shader);
            return;
        }
    }

    mProgram = s_gles2.glCreateProgram();
    s_gles2.glAttachShader(mProgram, vertShader);
    s_gles2.glAttachShader(mProgram, fragShader);
    s_gles2.glLinkProgram(mProgram);

    GLint status = GL_FALSE;
    s_gles2.glGetProgramiv(mProgram, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar error[1024];
        s_gles2.glGetProgramInfoLog(mProgram, sizeof(error), 0, &error[0]);
        GFXSTREAM_FATAL("Failed to link YUV conversion program: %s", error);
        s_gles2.glDeleteProgram(mProgram);
        mProgram = 0;
        return;
    }

    mUniformLocYWidthCutoff = s_gles2.glGetUniformLocation(mProgram, "uYWidthCutoff");
    mUniformLocUVWidthCutoff = s_gles2.glGetUniformLocation(mProgram, "uUVWidthCutoff");
    mUniformLocSamplerY = s_gles2.glGetUniformLocation(mProgram, "uSamplerY");
    mUniformLocSamplerU = s_gles2.glGetUniformLocation(mProgram, "uSamplerU");
    mUniformLocSamplerV = s_gles2.glGetUniformLocation(mProgram, "uSamplerV");
    mAttributeLocPos = s_gles2.glGetAttribLocation(mProgram, "aPosition");
    mAttributeLocTexCoord = s_gles2.glGetAttribLocation(mProgram, "aTexCoord");

    s_gles2.glDeleteShader(vertShader);
    s_gles2.glDeleteShader(fragShader);
}

void YUVConverter::createYUVGLFullscreenQuad() {
    s_gles2.glGenBuffers(1, &mQuadVertexBuffer);
    s_gles2.glGenBuffers(1, &mQuadIndexBuffer);

    static const float kVertices[] = {
        +1, -1, +0, +1, +0,
        +1, +1, +0, +1, +1,
        -1, +1, +0, +0, +1,
        -1, -1, +0, +0, +0,
    };

    static const GLubyte kIndices[] = { 0, 1, 2, 2, 3, 0 };

    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, mQuadVertexBuffer);
    s_gles2.glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
    s_gles2.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mQuadIndexBuffer);
    s_gles2.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);
}

static void doYUVConversionDraw(GLuint program,
                                GLint uniformLocYWidthCutoff,
                                GLint uniformLocUVWidthCutoff,
                                GLint uniformLocYSampler,
                                GLint uniformLocUSampler,
                                GLint uniformLocVSampler,
                                GLint attributeLocTexCoord,
                                GLint attributeLocPos,
                                GLuint quadVertexBuffer,
                                GLuint quadIndexBuffer,
                                float uYWidthCutoff,
                                float uUVWidthCutoff) {
    const GLsizei kVertexAttribStride = 5 * sizeof(GL_FLOAT);
    const GLvoid* kVertexAttribPosOffset = (GLvoid*)0;
    const GLvoid* kVertexAttribCoordOffset = (GLvoid*)(3 * sizeof(GL_FLOAT));

    s_gles2.glUseProgram(program);

    s_gles2.glUniform1f(uniformLocYWidthCutoff, uYWidthCutoff);
    s_gles2.glUniform1f(uniformLocUVWidthCutoff, uUVWidthCutoff);

    s_gles2.glUniform1i(uniformLocYSampler, 0);
    s_gles2.glUniform1i(uniformLocUSampler, 1);
    s_gles2.glUniform1i(uniformLocVSampler, 2);

    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, quadVertexBuffer);
    s_gles2.glEnableVertexAttribArray(attributeLocPos);
    s_gles2.glEnableVertexAttribArray(attributeLocTexCoord);

    s_gles2.glVertexAttribPointer(attributeLocPos, 3, GL_FLOAT, false,
                                  kVertexAttribStride,
                                  kVertexAttribPosOffset);
    s_gles2.glVertexAttribPointer(attributeLocTexCoord, 2, GL_FLOAT, false,
                                  kVertexAttribStride,
                                  kVertexAttribCoordOffset);

    s_gles2.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadIndexBuffer);
    s_gles2.glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);

    s_gles2.glDisableVertexAttribArray(attributeLocPos);
    s_gles2.glDisableVertexAttribArray(attributeLocTexCoord);
}

// initialize(): allocate GPU memory for YUV components,
// and create shaders and vertex data.
YUVConverter::YUVConverter(int width, int height, FrameworkFormat format, bool yuv420888ToNv21)
    : mWidth(width),
      mHeight(height),
      mFormat(format),
      mColorBufferFormat(format),
      mYuv420888ToNv21(yuv420888ToNv21) {}

void YUVConverter::init(int width, int height, FrameworkFormat format) {
    YUV_DEBUG_LOG("w:%d h:%d format:%d", width, height, format);

    uint32_t yWidth, yHeight = 0, yOffsetBytes, yStridePixels = 0, yStrideBytes;
    uint32_t uWidth, uHeight = 0, uOffsetBytes, uStridePixels = 0, uStrideBytes;
    uint32_t vWidth, vHeight = 0, vOffsetBytes, vStridePixels = 0, vStrideBytes;
    getYUVOffsets(width, height, mFormat, mYuv420888ToNv21,
                  &yWidth, &yHeight, &yOffsetBytes, &yStridePixels, &yStrideBytes,
                  &uWidth, &uHeight, &uOffsetBytes, &uStridePixels, &uStrideBytes,
                  &vWidth, &vHeight, &vOffsetBytes, &vStridePixels, &vStrideBytes);
    mWidth = width;
    mHeight = height;
    if (!mTextureY) {
        createYUVGLTex(GL_TEXTURE0, yStridePixels, yHeight, mFormat, mYuv420888ToNv21, YUVPlane::Y, &mTextureY);
    }
    if (isInterleaved(mFormat, mYuv420888ToNv21)) {
        if (!mTextureU) {
            createYUVGLTex(GL_TEXTURE1, uStridePixels, uHeight, mFormat, mYuv420888ToNv21, YUVPlane::UV, &mTextureU);
            mTextureV = mTextureU;
        }
    } else {
        if (!mTextureU) {
            createYUVGLTex(GL_TEXTURE1, uStridePixels, uHeight, mFormat, mYuv420888ToNv21, YUVPlane::U, &mTextureU);
        }
        if (!mTextureV) {
            createYUVGLTex(GL_TEXTURE2, vStridePixels, vHeight, mFormat, mYuv420888ToNv21, YUVPlane::V, &mTextureV);
        }
    }

    int glesMajor;
    int glesMinor;
    emugl::getGlesVersion(&glesMajor, &glesMinor);
    mHasGlsl3Support = glesMajor >= 3;
    YUV_DEBUG_LOG("YUVConverter has GLSL ES 3 support:%s (major:%d minor:%d", (mHasGlsl3Support ? "yes" : "no"), glesMajor, glesMinor);

    createYUVGLShader();
    createYUVGLFullscreenQuad();
}

void YUVConverter::saveGLState() {
    s_gles2.glGetFloatv(GL_VIEWPORT, mCurrViewport);
    s_gles2.glGetIntegerv(GL_ACTIVE_TEXTURE, &mCurrTexUnit);
    s_gles2.glGetIntegerv(GL_TEXTURE_BINDING_2D, &mCurrTexBind);
    s_gles2.glGetIntegerv(GL_CURRENT_PROGRAM, &mCurrProgram);
    s_gles2.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &mCurrVbo);
    s_gles2.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &mCurrIbo);
}

void YUVConverter::restoreGLState() {
    s_gles2.glViewport(mCurrViewport[0], mCurrViewport[1],
                       mCurrViewport[2], mCurrViewport[3]);
    s_gles2.glActiveTexture(mCurrTexUnit);
    s_gles2.glUseProgram(mCurrProgram);
    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, mCurrVbo);
    s_gles2.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mCurrIbo);
}

uint32_t YUVConverter::getDataSize() {
    uint32_t align = (mFormat == FRAMEWORK_FORMAT_YV12) ? 16 : 1;
    uint32_t yStrideBytes = (mWidth + (align - 1)) & ~(align - 1);
    uint32_t uvStride = (yStrideBytes / 2 + (align - 1)) & ~(align - 1);
    uint32_t uvHeight = mHeight / 2;
    uint32_t dataSize = yStrideBytes * mHeight + 2 * (uvHeight * uvStride);
    YUV_DEBUG_LOG("w:%d h:%d format:%d has data size:%d", mWidth, mHeight, mFormat, dataSize);
    return dataSize;
}

void YUVConverter::readPixels(uint8_t* pixels, uint32_t pixels_size) {
    YUV_DEBUG_LOG("w:%d h:%d format:%d pixels:%p pixels-size:%d", mWidth, mHeight, mFormat, pixels, pixels_size);

    uint32_t yWidth, yHeight, yOffsetBytes, yStridePixels, yStrideBytes;
    uint32_t uWidth, uHeight, uOffsetBytes, uStridePixels, uStrideBytes;
    uint32_t vWidth, vHeight, vOffsetBytes, vStridePixels, vStrideBytes;
    getYUVOffsets(mWidth, mHeight, mFormat, mYuv420888ToNv21,
                  &yWidth, &yHeight, &yOffsetBytes, &yStridePixels, &yStrideBytes,
                  &uWidth, &uHeight, &uOffsetBytes, &uStridePixels, &uStrideBytes,
                  &vWidth, &vHeight, &vOffsetBytes, &vStridePixels, &vStrideBytes);

    if (isInterleaved(mFormat, mYuv420888ToNv21)) {
        readYUVTex(mTextureV, mFormat, mYuv420888ToNv21, YUVPlane::UV, pixels + std::min(uOffsetBytes, vOffsetBytes),
                   uStridePixels);
    } else {
        readYUVTex(mTextureU, mFormat, mYuv420888ToNv21, YUVPlane::U, pixels + uOffsetBytes, uStridePixels);
        readYUVTex(mTextureV, mFormat, mYuv420888ToNv21, YUVPlane::V, pixels + vOffsetBytes, vStridePixels);
    }

    if (mFormat == FRAMEWORK_FORMAT_NV12 && mColorBufferFormat == FRAMEWORK_FORMAT_YUV_420_888) {
        NV12ToYUV420PlanarInPlaceConvert(mWidth, mHeight, pixels, pixels);
    }

    // Read the Y plane last because so that we can use it as a scratch space.
    readYUVTex(mTextureY, mFormat, mYuv420888ToNv21, YUVPlane::Y, pixels + yOffsetBytes, yStridePixels);
}

void YUVConverter::swapTextures(FrameworkFormat format, GLuint* textures, void* metadata) {
    if (isInterleaved(format, mYuv420888ToNv21)) {
        std::swap(textures[0], mTextureY);
        std::swap(textures[1], mTextureU);
        mTextureV = mTextureU;
    } else {
        std::swap(textures[0], mTextureY);
        std::swap(textures[1], mTextureU);
        std::swap(textures[2], mTextureV);
    }

    mFormat = format;

    const bool needToUpdateConversionShader = checkAndUpdateColorAspectsChanged(metadata);
    if (needToUpdateConversionShader) {
        saveGLState();
        reset();
        init(mWidth, mHeight, mFormat);
    }

    mTexturesSwapped = true;
}

// drawConvert: per-frame updates.
// Update YUV textures, then draw the fullscreen
// quad set up above, which results in a framebuffer
// with the RGB colors.
void YUVConverter::drawConvert(int x, int y, int width, int height, const char* pixels) {
    drawConvertFromFormat(mFormat, x, y, width, height, pixels);
}

void YUVConverter::drawConvertFromFormat(FrameworkFormat format, int x, int y, int width,
                                         int height, const char* pixels, void* metadata) {
    saveGLState();
    const bool needToUpdateConversionShader = checkAndUpdateColorAspectsChanged(metadata);

    if (pixels && (width != mWidth || height != mHeight)) {
        reset();
    }

    bool uploadFormatChanged = !mTexturesSwapped && pixels && (format != mFormat);
    bool initNeeded = (mProgram == 0) || uploadFormatChanged || needToUpdateConversionShader;

    if (initNeeded) {
        if (uploadFormatChanged) {
            mFormat = format;
            // TODO: missing cherry-picks, put it back
            // b/264928117
            //mCbFormat = format;
            reset();
        }
        init(width, height, mFormat);
    }

    if (mFormat == FRAMEWORK_FORMAT_P010 && !mHasGlsl3Support) {
        // TODO: perhaps fallback to just software conversion.
        return;
    }

    uint32_t yWidth = 0, yHeight = 0, yOffsetBytes, yStridePixels = 0, yStrideBytes;
    uint32_t uWidth = 0, uHeight = 0, uOffsetBytes, uStridePixels = 0, uStrideBytes;
    uint32_t vWidth = 0, vHeight = 0, vOffsetBytes, vStridePixels = 0, vStrideBytes;
    getYUVOffsets(width, height, mFormat, mYuv420888ToNv21,
                  &yWidth, &yHeight, &yOffsetBytes, &yStridePixels, &yStrideBytes,
                  &uWidth, &uHeight, &uOffsetBytes, &uStridePixels, &uStrideBytes,
                  &vWidth, &vHeight, &vOffsetBytes, &vStridePixels, &vStrideBytes);

    YUV_DEBUG_LOG("Updating YUV textures for drawConvert() "
                  "x:%d y:%d width:%d height:%d "
                  "yWidth:%d yHeight:%d yOffsetBytes:%d yStridePixels:%d yStrideBytes:%d "
                  "uWidth:%d uHeight:%d uOffsetBytes:%d uStridePixels:%d uStrideBytes:%d "
                  "vWidth:%d vHeight:%d vOffsetBytes:%d vStridePixels:%d vStrideBytes:%d ",
                  x, y, width, height,
                  yWidth, yHeight, yOffsetBytes, yStridePixels, yStrideBytes,
                  uWidth, uHeight, uOffsetBytes, uStridePixels, uStrideBytes,
                  vWidth, vHeight, vOffsetBytes, vStridePixels, vStrideBytes);

    s_gles2.glViewport(x, y, width, height);

    updateCutoffs(static_cast<float>(yWidth),
                  static_cast<float>(yStridePixels),
                  static_cast<float>(uWidth),
                  static_cast<float>(uStridePixels));

    if (pixels) {
        subUpdateYUVGLTex(GL_TEXTURE0, mTextureY, x, y, yStridePixels, yHeight, mFormat, mYuv420888ToNv21, YUVPlane::Y, pixels + yOffsetBytes);
        if (isInterleaved(mFormat, mYuv420888ToNv21)) {
            subUpdateYUVGLTex(GL_TEXTURE1, mTextureU, x, y, uStridePixels, uHeight, mFormat, mYuv420888ToNv21, YUVPlane::UV, pixels + std::min(uOffsetBytes, vOffsetBytes));
        } else {
            subUpdateYUVGLTex(GL_TEXTURE1, mTextureU, x, y, uStridePixels, uHeight, mFormat, mYuv420888ToNv21, YUVPlane::U, pixels + uOffsetBytes);
            subUpdateYUVGLTex(GL_TEXTURE2, mTextureV, x, y, vStridePixels, vHeight, mFormat, mYuv420888ToNv21, YUVPlane::V, pixels + vOffsetBytes);
        }
    } else {
        // special case: draw from texture, only support NV12 for now
        // as cuvid's native format is NV12.
        // TODO: add more formats if there are such needs in the future.
        assert(mFormat == FRAMEWORK_FORMAT_NV12);
    }

    s_gles2.glActiveTexture(GL_TEXTURE0);
    s_gles2.glBindTexture(GL_TEXTURE_2D, mTextureY);
    s_gles2.glActiveTexture(GL_TEXTURE1);
    s_gles2.glBindTexture(GL_TEXTURE_2D, mTextureU);
    s_gles2.glActiveTexture(GL_TEXTURE2);
    s_gles2.glBindTexture(GL_TEXTURE_2D, mTextureV);

    doYUVConversionDraw(mProgram,
                        mUniformLocYWidthCutoff,
                        mUniformLocUVWidthCutoff,
                        mUniformLocSamplerY,
                        mUniformLocSamplerU,
                        mUniformLocSamplerV,
                        mAttributeLocTexCoord,
                        mAttributeLocPos,
                        mQuadVertexBuffer,
                        mQuadIndexBuffer,
                        mYWidthCutoff,
                        mUVWidthCutoff);

    restoreGLState();
}

void YUVConverter::updateCutoffs(float yWidth, float yStridePixels,
                                 float uvWidth, float uvStridePixels) {
    switch (mFormat) {
    case FRAMEWORK_FORMAT_YV12:
        mYWidthCutoff = yWidth / yStridePixels;
        mUVWidthCutoff = uvWidth / uvStridePixels;
        break;
    case FRAMEWORK_FORMAT_NV12:
    case FRAMEWORK_FORMAT_P010:
    case FRAMEWORK_FORMAT_YUV_420_888:
        mYWidthCutoff = 1.0f;
        mUVWidthCutoff = 1.0f;
        break;
    case FRAMEWORK_FORMAT_GL_COMPATIBLE:
        GFXSTREAM_FATAL("Input not a YUV format!");
    }
}

void YUVConverter::reset() {
    if (mQuadIndexBuffer) s_gles2.glDeleteBuffers(1, &mQuadIndexBuffer);
    if (mQuadVertexBuffer) s_gles2.glDeleteBuffers(1, &mQuadVertexBuffer);
    if (mProgram) s_gles2.glDeleteProgram(mProgram);
    if (mTextureY) s_gles2.glDeleteTextures(1, &mTextureY);
    if (isInterleaved(mFormat, mYuv420888ToNv21)) {
        if (mTextureU) s_gles2.glDeleteTextures(1, &mTextureU);
    } else {
        if (mTextureU) s_gles2.glDeleteTextures(1, &mTextureU);
        if (mTextureV) s_gles2.glDeleteTextures(1, &mTextureV);
    }
    mQuadIndexBuffer = 0;
    mQuadVertexBuffer = 0;
    mProgram = 0;
    mTextureY = 0;
    mTextureU = 0;
    mTextureV = 0;
}

YUVConverter::~YUVConverter() {
    reset();
}

}  // namespace gl
}  // namespace gfxstream
