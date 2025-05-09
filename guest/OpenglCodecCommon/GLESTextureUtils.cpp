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

#include "GLESTextureUtils.h"

#include <algorithm>

#include "glUtils.h"
#include "gfxstream/etc.h"
#include "astc-codec.h"

#include <cutils/log.h>

#define ASTC_FORMATS_LIST(EXPAND_MACRO) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_4x4_KHR, astc_codec::FootprintType::k4x4, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_5x4_KHR, astc_codec::FootprintType::k5x4, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_5x5_KHR, astc_codec::FootprintType::k5x5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_6x5_KHR, astc_codec::FootprintType::k6x5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_6x6_KHR, astc_codec::FootprintType::k6x6, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_8x5_KHR, astc_codec::FootprintType::k8x5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_8x6_KHR, astc_codec::FootprintType::k8x6, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_8x8_KHR, astc_codec::FootprintType::k8x8, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x5_KHR, astc_codec::FootprintType::k10x5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x6_KHR, astc_codec::FootprintType::k10x6, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x8_KHR, astc_codec::FootprintType::k10x8, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x10_KHR, astc_codec::FootprintType::k10x10, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_12x10_KHR, astc_codec::FootprintType::k12x10, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_12x12_KHR, astc_codec::FootprintType::k12x12, false) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR, astc_codec::FootprintType::k4x4, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR, astc_codec::FootprintType::k5x4, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR, astc_codec::FootprintType::k5x5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR, astc_codec::FootprintType::k6x5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR, astc_codec::FootprintType::k6x6, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR, astc_codec::FootprintType::k8x5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR, astc_codec::FootprintType::k8x6, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR, astc_codec::FootprintType::k8x8, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR, astc_codec::FootprintType::k10x5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR, astc_codec::FootprintType::k10x6, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR, astc_codec::FootprintType::k10x8, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR, astc_codec::FootprintType::k10x10, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR, astc_codec::FootprintType::k12x10, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR, astc_codec::FootprintType::k12x12, true) \

namespace GLESTextureUtils {

// Based on computations in
// https://swiftshader.googlesource.com/SwiftShader/+/master/src/OpenGL/common/Image.cpp
// such as Image::loadImageData,
// ComputePitch/ComputePackingOffset

#define HIGHEST_MULTIPLE_OF(align, x) \
    (( ( x ) + ( align ) - 1) & ~( ( align ) - 1)) \

static int computePixelSize(GLenum format, GLenum type) {

#define FORMAT_ERROR(format, type) \
    ALOGE("%s:%d unknown format/type 0x%x 0x%x", __FUNCTION__, __LINE__, format, type) \

    switch(type) {
    case GL_BYTE:
        switch(format) {
        case GL_R8:
        case GL_R8I:
        case GL_R8_SNORM:
        case GL_RED:             return sizeof(char);
        case GL_RED_INTEGER:     return sizeof(char);
        case GL_RG8:
        case GL_RG8I:
        case GL_RG8_SNORM:
        case GL_RG:              return sizeof(char) * 2;
        case GL_RG_INTEGER:      return sizeof(char) * 2;
        case GL_RGB8:
        case GL_RGB8I:
        case GL_RGB8_SNORM:
        case GL_RGB:             return sizeof(char) * 3;
        case GL_RGB_INTEGER:     return sizeof(char) * 3;
        case GL_RGBA8:
        case GL_RGBA8I:
        case GL_RGBA8_SNORM:
        case GL_RGBA:            return sizeof(char) * 4;
        case GL_RGBA_INTEGER:    return sizeof(char) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_UNSIGNED_BYTE:
        switch(format) {
        case GL_R8:
        case GL_R8UI:
        case GL_RED:             return sizeof(unsigned char);
        case GL_RED_INTEGER:     return sizeof(unsigned char);
        case GL_ALPHA8_EXT:
        case GL_ALPHA:           return sizeof(unsigned char);
        case GL_LUMINANCE8_EXT:
        case GL_LUMINANCE:       return sizeof(unsigned char);
        case GL_LUMINANCE8_ALPHA8_EXT:
        case GL_LUMINANCE_ALPHA: return sizeof(unsigned char) * 2;
        case GL_RG8:
        case GL_RG8UI:
        case GL_RG:              return sizeof(unsigned char) * 2;
        case GL_RG_INTEGER:      return sizeof(unsigned char) * 2;
        case GL_RGB8:
        case GL_RGB8UI:
        case GL_SRGB8:
        case GL_RGB:             return sizeof(unsigned char) * 3;
        case GL_RGB_INTEGER:     return sizeof(unsigned char) * 3;
        case GL_RGBA8:
        case GL_RGBA8UI:
        case GL_SRGB8_ALPHA8:
        case GL_RGBA:            return sizeof(unsigned char) * 4;
        case GL_RGBA_INTEGER:    return sizeof(unsigned char) * 4;
        case GL_BGRA_EXT:
        case GL_BGRA8_EXT:       return sizeof(unsigned char)* 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_SHORT:
        switch(format) {
        case GL_R16I:
        case GL_RED_INTEGER:     return sizeof(short);
        case GL_RG16I:
        case GL_RG_INTEGER:      return sizeof(short) * 2;
        case GL_RGB16I:
        case GL_RGB_INTEGER:     return sizeof(short) * 3;
        case GL_RGBA16I:
        case GL_RGBA_INTEGER:    return sizeof(short) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_UNSIGNED_SHORT:
        switch(format) {
        case GL_DEPTH_COMPONENT16:
        case GL_DEPTH_COMPONENT: return sizeof(unsigned short);
        case GL_R16UI:
        case GL_RED_INTEGER:     return sizeof(unsigned short);
        case GL_RG16UI:
        case GL_RG_INTEGER:      return sizeof(unsigned short) * 2;
        case GL_RGB16UI:
        case GL_RGB_INTEGER:     return sizeof(unsigned short) * 3;
        case GL_RGBA16UI:
        case GL_RGBA_INTEGER:    return sizeof(unsigned short) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_INT:
        switch(format) {
        case GL_R32I:
        case GL_RED_INTEGER:     return sizeof(int);
        case GL_RG32I:
        case GL_RG_INTEGER:      return sizeof(int) * 2;
        case GL_RGB32I:
        case GL_RGB_INTEGER:     return sizeof(int) * 3;
        case GL_RGBA32I:
        case GL_RGBA_INTEGER:    return sizeof(int) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_UNSIGNED_INT:
        switch(format) {
        case GL_DEPTH_COMPONENT16:
        case GL_DEPTH_COMPONENT24:
        case GL_DEPTH_COMPONENT32_OES:
        case GL_DEPTH_COMPONENT: return sizeof(unsigned int);
        case GL_R32UI:
        case GL_RED_INTEGER:     return sizeof(unsigned int);
        case GL_RG32UI:
        case GL_RG_INTEGER:      return sizeof(unsigned int) * 2;
        case GL_RGB32UI:
        case GL_RGB_INTEGER:     return sizeof(unsigned int) * 3;
        case GL_RGBA32UI:
        case GL_RGBA_INTEGER:    return sizeof(unsigned int) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV_EXT:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV_EXT:
        return sizeof(unsigned short);
    case GL_UNSIGNED_INT_10F_11F_11F_REV:
    case GL_UNSIGNED_INT_5_9_9_9_REV:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_24_8_OES:
        return sizeof(unsigned int);
    case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
        return sizeof(float) + sizeof(unsigned int);
    case GL_FLOAT:
        switch(format) {
        case GL_DEPTH_COMPONENT32F:
        case GL_DEPTH_COMPONENT: return sizeof(float);
        case GL_ALPHA32F_EXT:
        case GL_ALPHA:           return sizeof(float);
        case GL_LUMINANCE32F_EXT:
        case GL_LUMINANCE:       return sizeof(float);
        case GL_LUMINANCE_ALPHA32F_EXT:
        case GL_LUMINANCE_ALPHA: return sizeof(float) * 2;
        case GL_RED:             return sizeof(float);
        case GL_R32F:            return sizeof(float);
        case GL_RG:              return sizeof(float) * 2;
        case GL_RG32F:           return sizeof(float) * 2;
        case GL_RGB:             return sizeof(float) * 3;
        case GL_RGB32F:          return sizeof(float) * 3;
        case GL_RGBA:            return sizeof(float) * 4;
        case GL_RGBA32F:         return sizeof(float) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    case GL_HALF_FLOAT:
    case GL_HALF_FLOAT_OES:
        switch(format) {
        case GL_ALPHA16F_EXT:
        case GL_ALPHA:           return sizeof(unsigned short);
        case GL_LUMINANCE16F_EXT:
        case GL_LUMINANCE:       return sizeof(unsigned short);
        case GL_LUMINANCE_ALPHA16F_EXT:
        case GL_LUMINANCE_ALPHA: return sizeof(unsigned short) * 2;
        case GL_RED:             return sizeof(unsigned short);
        case GL_R16F:            return sizeof(unsigned short);
        case GL_RG:              return sizeof(unsigned short) * 2;
        case GL_RG16F:           return sizeof(unsigned short) * 2;
        case GL_RGB:             return sizeof(unsigned short) * 3;
        case GL_RGB16F:          return sizeof(unsigned short) * 3;
        case GL_RGBA:            return sizeof(unsigned short) * 4;
        case GL_RGBA16F:         return sizeof(unsigned short) * 4;
        default: FORMAT_ERROR(format, type);
        }
        break;
    default: FORMAT_ERROR(format, type);
    }

    return 0;
}

static int computePitch(GLsizei inputWidth, GLenum format, GLenum type, int align) {
    GLsizei unaligned_width = computePixelSize(format, type) * inputWidth;
    return HIGHEST_MULTIPLE_OF(align, unaligned_width);
}

static int computePackingOffset(GLenum format, GLenum type, GLsizei width, GLsizei height, int align, int skipPixels, int skipRows, int skipImages) {
    GLsizei alignedPitch = computePitch(width, format, type, align);
    int packingOffsetRows =
        (skipImages * height + skipRows);
    int res = packingOffsetRows * alignedPitch + skipPixels * computePixelSize(format, type);

    return res;
}

void computeTextureStartEnd(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int unpackAlignment,
        int unpackRowLength,
        int unpackImageHeight,
        int unpackSkipPixels,
        int unpackSkipRows,
        int unpackSkipImages,
        int* start,
        int* end,
        int ignoreTrailing) {

    GLsizei inputWidth = (unpackRowLength == 0) ? width : unpackRowLength;
    GLsizei inputPitch = computePitch(inputWidth, format, type, unpackAlignment);
    GLsizei inputHeight = (unpackImageHeight == 0) ? height : unpackImageHeight;

    ALOGV("%s: input idim %d %d %d w p h %d %d %d:", __FUNCTION__, width, height, depth, inputWidth, inputPitch, inputHeight);

    int startVal = computePackingOffset(format, type, inputWidth, inputHeight, unpackAlignment, unpackSkipPixels, unpackSkipRows, unpackSkipImages);
    int endVal;
    if (ignoreTrailing) {
        // The last row needs to have just enough data per spec, and could
        // ignore alignment.
        // b/223402256
        endVal = startVal + inputPitch * inputHeight * (depth - 1);
        endVal += inputPitch * (std::min(height, inputHeight) - 1);
        endVal += computePitch(std::min(width, inputWidth), format, type, 1);
    } else {
        endVal = startVal + inputPitch * inputHeight * depth;
    }

    if (start) *start = startVal;
    if (end) *end = endVal;

    ALOGV("%s: start/end: %d %d", __FUNCTION__, *start, *end);

}

int computeTotalImageSize(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int unpackAlignment,
        int unpackRowLength,
        int unpackImageHeight,
        int unpackSkipPixels,
        int unpackSkipRows,
        int unpackSkipImages) {

    int start, end;
    computeTextureStartEnd(
            width, height, depth,
            format, type,
            unpackAlignment,
            unpackRowLength,
            unpackImageHeight,
            unpackSkipPixels,
            unpackSkipRows,
            unpackSkipImages,
            &start,
            &end,
            0);
    return end;
}

int computeNeededBufferSize(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int unpackAlignment,
        int unpackRowLength,
        int unpackImageHeight,
        int unpackSkipPixels,
        int unpackSkipRows,
        int unpackSkipImages,
        int ignoreTrailing) {

    int start, end;
    computeTextureStartEnd(
            width, height, depth,
            format, type,
            unpackAlignment,
            unpackRowLength,
            unpackImageHeight,
            unpackSkipPixels,
            unpackSkipRows,
            unpackSkipImages,
            &start,
            &end,
            ignoreTrailing);
    return end - start;
}

void computePackingOffsets2D(
        GLsizei width, GLsizei height,
        GLenum format, GLenum type,
        int packAlignment,
        int packRowLength,
        int packSkipPixels,
        int packSkipRows,
        int* bpp,
        int* startOffset,
        int* packingPixelRowSize,
        int* packingTotalRowSize) {

    int widthTotal = (packRowLength == 0) ? width : packRowLength;
    int totalRowSize = computePitch(widthTotal, format, type, packAlignment);
    int pixelsOnlyRowSize = computePitch(width, format, type, packAlignment);

    int packingOffsetStart =
        computePackingOffset(
                format, type, widthTotal, height, packAlignment, packSkipPixels, packSkipRows, 0 /* skip images = 0 */);

    if (bpp) *bpp = computePixelSize(format, type);
    if (startOffset) *startOffset = packingOffsetStart;
    if (packingPixelRowSize) *packingPixelRowSize = pixelsOnlyRowSize;
    if (packingTotalRowSize) *packingTotalRowSize = totalRowSize;
}

void computePackingOffsets3D(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int packAlignment,
        int packRowLength,
        int packImageHeight,
        int packSkipPixels,
        int packSkipRows,
        int packSkipImages,
        int* bpp,
        int* startOffset,
        int* packingPixelRowSize,
        int* packingTotalRowSize,
        int* packingPixelImageSize,
        int* packingTotalImageSize) {

    (void)depth;

    int widthTotal = (packRowLength == 0) ? width : packRowLength;
    int totalRowSize = computePitch(widthTotal, format, type, packAlignment);
    int pixelsOnlyRowSize = computePitch(width, format, type, packAlignment);

    int heightTotal = packImageHeight == 0 ? height : packImageHeight;
    int totalImageSize = totalRowSize * heightTotal;
    int pixelsOnlyImageSize = totalRowSize * height;

    int packingOffsetStart =
        computePackingOffset(
                format, type, widthTotal, heightTotal, packAlignment, packSkipPixels, packSkipRows, packSkipImages);

    if (bpp) *bpp = computePixelSize(format, type);
    if (startOffset) *startOffset = packingOffsetStart;
    if (packingPixelRowSize) *packingPixelRowSize = pixelsOnlyRowSize;
    if (packingTotalRowSize) *packingTotalRowSize = totalRowSize;
    if (packingPixelImageSize) *packingPixelImageSize = pixelsOnlyImageSize;
    if (packingTotalImageSize) *packingTotalImageSize = totalImageSize;
}

bool isEtcFormat(GLenum internalformat) {
    switch (internalformat) {
    case GL_ETC1_RGB8_OES:
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_SRGB8_ETC2:
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
    case GL_COMPRESSED_R11_EAC:
    case GL_COMPRESSED_SIGNED_R11_EAC:
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC:
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        return true;
    }
    return false;
}

bool isEtc2Format(GLenum internalformat) {
    return internalformat != GL_ETC1_RGB8_OES &&
        isEtcFormat(internalformat);
}

ETC2ImageFormat getEtcFormat(GLenum internalformat) {
    ETC2ImageFormat etcFormat = EtcRGB8;
    switch (internalformat) {
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_ETC1_RGB8_OES:
            break;
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
            etcFormat = EtcRGBA8;
            break;
        case GL_COMPRESSED_SRGB8_ETC2:
            break;
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
            etcFormat = EtcRGBA8;
            break;
        case GL_COMPRESSED_R11_EAC:
            etcFormat = EtcR11;
            break;
        case GL_COMPRESSED_SIGNED_R11_EAC:
            etcFormat = EtcSignedR11;
            break;
        case GL_COMPRESSED_RG11_EAC:
            etcFormat = EtcRG11;
            break;
        case GL_COMPRESSED_SIGNED_RG11_EAC:
            etcFormat = EtcSignedRG11;
            break;
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            etcFormat = EtcRGB8A1;
            break;
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            etcFormat = EtcRGB8A1;
            break;
    }
    return etcFormat;
}

bool isAstcFormat(GLenum internalformat) {
    switch (internalformat) {
#define ASTC_FORMAT(typeName, footprintType, srgbValue) \
        case typeName:

        ASTC_FORMATS_LIST(ASTC_FORMAT)
#undef ASTC_FORMAT
            return true;
        default:
            return false;
    }
}

bool isBptcFormat(GLenum internalformat) {
    switch (internalformat) {
        case GL_COMPRESSED_RGBA_BPTC_UNORM_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT:
        case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT:
        case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT:
            return true;
        default:
            return false;
    }
}

bool isRgtcFormat(GLenum internalformat) {
    switch(internalformat)
    {
        case GL_COMPRESSED_RED_RGTC1_EXT:
        case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:
        case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
        case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:
            return true;
    }
    return false;
}

bool isS3tcFormat(GLenum internalformat) {
    switch (internalformat) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            return true;
    }

    return false;
}

void getAstcFormatInfo(GLenum internalformat,
                       astc_codec::FootprintType* footprint,
                       bool* srgb) {
    switch (internalformat) {
#define ASTC_FORMAT(typeName, footprintType, srgbValue) \
        case typeName: \
            *footprint = footprintType; *srgb = srgbValue; break; \

        ASTC_FORMATS_LIST(ASTC_FORMAT)
#undef ASTC_FORMAT
        default:
            ALOGE("%s: invalid astc format: 0x%x\n", __func__, internalformat);
            abort();
    }
}

int getAstcFootprintWidth(astc_codec::FootprintType footprint) {
    switch (footprint) {
        case astc_codec::FootprintType::k4x4: return 4;
        case astc_codec::FootprintType::k5x4: return 5;
        case astc_codec::FootprintType::k5x5: return 5;
        case astc_codec::FootprintType::k6x5: return 6;
        case astc_codec::FootprintType::k6x6: return 6;
        case astc_codec::FootprintType::k8x5: return 8;
        case astc_codec::FootprintType::k8x6: return 8;
        case astc_codec::FootprintType::k10x5: return 10;
        case astc_codec::FootprintType::k10x6: return 10;
        case astc_codec::FootprintType::k8x8: return 8;
        case astc_codec::FootprintType::k10x8: return 10;
        case astc_codec::FootprintType::k10x10: return 10;
        case astc_codec::FootprintType::k12x10: return 12;
        case astc_codec::FootprintType::k12x12: return 12;
        default:
            ALOGE("%s: invalid astc footprint: 0x%x\n", __func__, footprint);
            abort();
    }
}

int getAstcFootprintHeight(astc_codec::FootprintType footprint) {
    switch (footprint) {
        case astc_codec::FootprintType::k4x4: return 4;
        case astc_codec::FootprintType::k5x4: return 4;
        case astc_codec::FootprintType::k5x5: return 5;
        case astc_codec::FootprintType::k6x5: return 5;
        case astc_codec::FootprintType::k6x6: return 6;
        case astc_codec::FootprintType::k8x5: return 5;
        case astc_codec::FootprintType::k8x6: return 6;
        case astc_codec::FootprintType::k10x5: return 5;
        case astc_codec::FootprintType::k10x6: return 6;
        case astc_codec::FootprintType::k8x8: return 8;
        case astc_codec::FootprintType::k10x8: return 8;
        case astc_codec::FootprintType::k10x10: return 10;
        case astc_codec::FootprintType::k12x10: return 10;
        case astc_codec::FootprintType::k12x12: return 12;
        default:
            ALOGE("%s: invalid astc footprint: 0x%x\n", __func__, footprint);
            abort();
    }
}

GLsizei getAstcCompressedSize(GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, bool* error) {
    bool srgb;
    astc_codec::FootprintType footprintType;
    getAstcFormatInfo(internalformat, &footprintType, &srgb);

    int fpWidth = getAstcFootprintWidth(footprintType);
    int fpHeight = getAstcFootprintHeight(footprintType);

    if (width == 0 || height == 0 || depth == 0) {
        *error = true;
        return 0;
    }

    const size_t blocks_wide = (width + fpWidth - 1) / fpWidth;
    if (blocks_wide == 0) {
        *error = true;
        return 0;
    }

    const size_t expected_block_count =
        ((width + fpWidth - 1) / fpWidth) *
        ((height + fpHeight - 1) / fpHeight);

    const size_t kPhysBlockSizeBytes = 16;

    GLsizei res = kPhysBlockSizeBytes * expected_block_count * depth;

    return res;
}

GLsizei getCompressedImageBlocksize(GLenum internalformat) {
    if (isBptcFormat(internalformat)) {
        return 16;
    }

    switch (internalformat) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RED_RGTC1_EXT:
        case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:
            return 8;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
        case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:
            return 16;
    }

    ALOGE("%s: Unknown blocksize for internal format: 0x%x\n", __func__, internalformat);
    abort();
}

GLsizei get4x4CompressedSize(GLsizei width, GLsizei height, GLsizei depth, GLsizei blocksize, bool* error) {
    *error = false;
    return blocksize * ((width + 3) / 4) * ((height + 3) / 4) * depth;
}

GLsizei getCompressedImageSize(GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, bool* error) {
    if (isEtcFormat(internalformat)) {
        GLsizei total = 0;
        GLsizei one = etc_get_encoded_data_size(getEtcFormat(internalformat), width, height);
        for (GLsizei i = 0; i < depth; ++i) {
            total += one;
        }
        return total;
    }

    if (isAstcFormat(internalformat)) {
        return getAstcCompressedSize(internalformat, width, height, depth, error);
    }

    if (isBptcFormat(internalformat) || isS3tcFormat(internalformat) || isRgtcFormat(internalformat)) {
        GLsizei blocksize = getCompressedImageBlocksize(internalformat);
        return get4x4CompressedSize(width, height, depth, blocksize, error);
    }

    ALOGE("%s: Unknown compressed internal format: 0x%x\n", __func__, internalformat);
    abort();
}

} // namespace GLESTextureUtils
