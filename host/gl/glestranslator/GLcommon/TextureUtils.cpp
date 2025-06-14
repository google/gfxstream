/*
* Copyright (C) 2011 The Android Open Source Project
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
#include <GLcommon/TextureUtils.h>
#include <GLcommon/GLESmacros.h>
#include <GLcommon/GLDispatch.h>
#include <GLcommon/GLESvalidate.h>
#include <stdio.h>
#include <cmath>
#include <memory>

#include "gfxstream/AlignedBuf.h"
#include "gfxstream/host/AstcCpuDecompressor.h"

using gfxstream::AlignedBuf;
using gfxstream::vk::AstcCpuDecompressor;

#define GL_R16 0x822A
#define GL_RG16 0x822C
#define GL_R16_SNORM 0x8F98
#define GL_RG16_SNORM 0x8F99

static constexpr size_t kASTCFormatsCount = 28;

#define ASTC_FORMATS_LIST(EXPAND_MACRO) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_4x4_KHR, 4, 4, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_5x4_KHR, 5, 4, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_5x5_KHR, 5, 5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_6x5_KHR, 6, 5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_6x6_KHR, 6, 6, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_8x5_KHR, 8, 5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_8x6_KHR, 8, 6, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_8x8_KHR, 8, 8, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x5_KHR, 10, 5, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x6_KHR, 10, 6, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x8_KHR, 10, 8, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_10x10_KHR, 10, 10, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_12x10_KHR, 12, 10, false) \
    EXPAND_MACRO(GL_COMPRESSED_RGBA_ASTC_12x12_KHR, 12, 12, false) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR, 4, 4, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR, 5, 4, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR, 5, 5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR, 6, 5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR, 6, 6, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR, 8, 5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR, 8, 6, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR, 8, 8, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR, 10, 5, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR, 10, 6, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR, 10, 8, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR, 10, 10, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR, 12, 10, true) \
    EXPAND_MACRO(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR, 12, 12, true) \

int getCompressedFormats(int majorVersion, int* formats) {
    static constexpr size_t kCount = MAX_SUPPORTED_PALETTE + MAX_ETC_SUPPORTED + kASTCFormatsCount;
    int res = kCount;

    if (majorVersion > 1) {
        res -= MAX_SUPPORTED_PALETTE;
    }

    if (formats) {
        size_t i = 0;

        if (1 == majorVersion) {
            // Palette
            formats[i++] = GL_PALETTE4_RGBA8_OES;
            formats[i++] = GL_PALETTE4_RGBA4_OES;
            formats[i++] = GL_PALETTE8_RGBA8_OES;
            formats[i++] = GL_PALETTE8_RGBA4_OES;
            formats[i++] = GL_PALETTE4_RGB8_OES;
            formats[i++] = GL_PALETTE8_RGB8_OES;
            formats[i++] = GL_PALETTE4_RGB5_A1_OES;
            formats[i++] = GL_PALETTE8_RGB5_A1_OES;
            formats[i++] = GL_PALETTE4_R5_G6_B5_OES;
            formats[i++] = GL_PALETTE8_R5_G6_B5_OES;
        }

        // ETC
        formats[i++] = GL_ETC1_RGB8_OES;
        formats[i++] = GL_COMPRESSED_RGB8_ETC2;
        formats[i++] = GL_COMPRESSED_SIGNED_R11_EAC;
        formats[i++] = GL_COMPRESSED_RG11_EAC;
        formats[i++] = GL_COMPRESSED_SIGNED_RG11_EAC;
        formats[i++] = GL_COMPRESSED_RGB8_ETC2;
        formats[i++] = GL_COMPRESSED_SRGB8_ETC2;
        formats[i++] = GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        formats[i++] = GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        formats[i++] = GL_COMPRESSED_RGBA8_ETC2_EAC;
        formats[i++] = GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
        formats[i++] = GL_COMPRESSED_R11_EAC;

        // ASTC
#define ASTC_FORMAT(typeName, blockWidth, blockHeight, srgbValue) \
        formats[i++] = typeName;

        ASTC_FORMATS_LIST(ASTC_FORMAT)
#undef ASTC_FORMAT

    }

    return res;
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

void getAstcFormatInfo(GLenum internalformat, uint32_t* width, uint32_t* height, bool* srgb) {
    switch (internalformat) {
#define ASTC_FORMAT(typeName, blockWidth, blockHeight, srgbValue) \
        case typeName: \
            *width = blockWidth; *height = blockHeight; *srgb = srgbValue; break; \

        ASTC_FORMATS_LIST(ASTC_FORMAT)
#undef ASTC_FORMAT
        default:
            assert(false && "Invalid ASTC format");
            break;
    }
}

// Helper function to decompress an ASTC image.
bool astcDecompress(const uint8_t* astcData, size_t astcDataSize, uint32_t width, uint32_t height,
                    uint32_t blockWidth, uint32_t blockHeight, uint8_t* outBuffer,
                    size_t outBufferSize) {
    if (outBufferSize < width * height * 4) {
        GFXSTREAM_WARNING("ASTC output buffer too small: %d bytes for %d x %d", outBufferSize,
                          width, height);
        return false;
    }
    int32_t status = AstcCpuDecompressor::get().decompress(width, height, blockWidth, blockHeight,
                                                           astcData, astcDataSize, outBuffer);
    if (status != 0) {
        GFXSTREAM_WARNING("astc decompression failed: %s",
                          AstcCpuDecompressor::get().getStatusString(status));
        return false;
    }
    return true;
}

bool isAstcFormat(GLenum internalformat) {
    switch (internalformat) {
#define ASTC_FORMAT(typeName, blockWidth, blockHeight, srgbValue) \
        case typeName:

        ASTC_FORMATS_LIST(ASTC_FORMAT)
#undef ASTC_FORMAT
            return true;
        default:
            return false;
    }
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
    switch (internalformat) {
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

bool isBptcFormat(GLenum internalformat) {
    switch (internalformat) {
    case GL_COMPRESSED_RGBA_BPTC_UNORM_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT:
    case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT:
    case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT:
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

bool isPaletteFormat(GLenum internalformat)  {
    switch (internalformat) {
    case GL_PALETTE4_RGB8_OES:
    case GL_PALETTE4_RGBA8_OES:
    case GL_PALETTE4_R5_G6_B5_OES:
    case GL_PALETTE4_RGBA4_OES:
    case GL_PALETTE4_RGB5_A1_OES:
    case GL_PALETTE8_RGB8_OES:
    case GL_PALETTE8_RGBA8_OES:
    case GL_PALETTE8_R5_G6_B5_OES:
    case GL_PALETTE8_RGBA4_OES:
    case GL_PALETTE8_RGB5_A1_OES:
        return true;
    }
    return false;
}

GLenum decompressedInternalFormat(GLEScontext* ctx, GLenum compressedFormat) {
    bool needSizedInternalFormat =
        isCoreProfile() ||
        (ctx->getMajorVersion() >= 3);

    GLenum glrgb = needSizedInternalFormat ? GL_RGB8 : GL_RGB;
    GLenum glrgba = needSizedInternalFormat ? GL_RGBA8 : GL_RGBA;

    switch (compressedFormat) {
        // ETC2 formats
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_ETC1_RGB8_OES:
            return glrgb;
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            return glrgba;
        case GL_COMPRESSED_SRGB8_ETC2:
            return GL_SRGB8;
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
            return GL_SRGB8_ALPHA8;
        case GL_COMPRESSED_R11_EAC:
        case GL_COMPRESSED_SIGNED_R11_EAC:
            return GL_R32F;
        case GL_COMPRESSED_RG11_EAC:
        case GL_COMPRESSED_SIGNED_RG11_EAC:
            return GL_RG32F;
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            return GL_SRGB8_ALPHA8;
        // ASTC formats
        case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:
        case GL_COMPRESSED_RGBA_ASTC_5x4_KHR:
        case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_6x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:
        case GL_COMPRESSED_RGBA_ASTC_8x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_8x6_KHR:
        case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x6_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x8_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x10_KHR:
        case GL_COMPRESSED_RGBA_ASTC_12x10_KHR:
        case GL_COMPRESSED_RGBA_ASTC_12x12_KHR:
            return glrgba;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
            return GL_SRGB8_ALPHA8;
        // palette formats
        case GL_PALETTE4_RGB8_OES:
        case GL_PALETTE4_R5_G6_B5_OES:
        case GL_PALETTE8_RGB8_OES:
        case GL_PALETTE8_R5_G6_B5_OES:
            return glrgb;
        case GL_PALETTE4_RGBA8_OES:
        case GL_PALETTE4_RGBA4_OES:
        case GL_PALETTE4_RGB5_A1_OES:
        case GL_PALETTE8_RGBA8_OES:
        case GL_PALETTE8_RGBA4_OES:
        case GL_PALETTE8_RGB5_A1_OES:
            return glrgba;
        case GL_COMPRESSED_RED_RGTC1_EXT:               // BC4U
            return GL_R8;
        case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:        // BC4S
            return GL_R8_SNORM;
        case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:         // BC5U
            return GL_RG8;
        case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:  // BC5S
            return GL_RG8_SNORM;
        default:
            return compressedFormat;
    }
}

class ScopedFetchUnpackData {
    public:
        ScopedFetchUnpackData(GLEScontext* ctx, GLintptr offset,
            GLsizei dataSize) : mCtx(ctx) {
            mData = ctx->dispatcher().glMapBufferRange(
                    GL_PIXEL_UNPACK_BUFFER,
                    offset, dataSize, GL_MAP_READ_BIT);
            if (mData) {
                ctx->dispatcher().glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,
                        &mUnpackBuffer);
                ctx->dispatcher().glBindBuffer(GL_PIXEL_UNPACK_BUFFER,
                        0);
            }
        }
        ~ScopedFetchUnpackData() {
            if (mData) {
                mCtx->dispatcher().glBindBuffer(GL_PIXEL_UNPACK_BUFFER,
                        mUnpackBuffer);
                mCtx->dispatcher().glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            }
        }
        void* data() {
            return mData;
        }
    private:
        const GLEScontext* mCtx;
        void* mData = nullptr;
        GLint mUnpackBuffer = 0;
};

void doCompressedTexImage2D(GLEScontext* ctx, GLenum target, GLint level,
                            GLenum internalformat, GLsizei width,
                            GLsizei height, GLint border,
                            GLsizei imageSize, const GLvoid* data,
                            glTexImage2D_t glTexImage2DPtr) {
    /* XXX: This is just a hack to fix the resolve of glTexImage2D problem
       It will be removed when we'll no longer link against ligGL */
    /*typedef void (GLAPIENTRY *glTexImage2DPtr_t ) (
            GLenum target, GLint level, GLint internalformat,
            GLsizei width, GLsizei height, GLint border,
            GLenum format, GLenum type, const GLvoid *pixels);

    glTexImage2DPtr_t glTexImage2DPtr;
    glTexImage2DPtr = (glTexImage2DPtr_t)funcPtr;*/
    bool needUnpackBuffer = false;
    if (ctx->getMajorVersion() >= 3) {
        GLint unpackBuffer = 0;
        ctx->dispatcher().glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,
                &unpackBuffer);
        needUnpackBuffer = unpackBuffer;
    }
    TextureUnpackReset unpack(ctx);
    const int32_t unpackAlignment = TextureUnpackReset::kUnpackAlignment;
    if (isEtcFormat(internalformat)) {
        GLint format = GL_RGB;
        GLint type = GL_UNSIGNED_BYTE;
        GLint convertedInternalFormat = decompressedInternalFormat(ctx, internalformat);
        ETC2ImageFormat etcFormat = EtcRGB8;
        switch (internalformat) {
            case GL_COMPRESSED_RGB8_ETC2:
            case GL_ETC1_RGB8_OES:
                break;
            case GL_COMPRESSED_RGBA8_ETC2_EAC:
                etcFormat = EtcRGBA8;
                format = GL_RGBA;
                break;
            case GL_COMPRESSED_SRGB8_ETC2:
                break;
            case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
                etcFormat = EtcRGBA8;
                format = GL_RGBA;
                break;
            case GL_COMPRESSED_R11_EAC:
                etcFormat = EtcR11;
                format = GL_RED;
                type = GL_FLOAT;
                break;
            case GL_COMPRESSED_SIGNED_R11_EAC:
                etcFormat = EtcSignedR11;
                format = GL_RED;
                type = GL_FLOAT;
                break;
            case GL_COMPRESSED_RG11_EAC:
                etcFormat = EtcRG11;
                format = GL_RG;
                type = GL_FLOAT;
                break;
            case GL_COMPRESSED_SIGNED_RG11_EAC:
                etcFormat = EtcSignedRG11;
                format = GL_RG;
                type = GL_FLOAT;
                break;
            case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
                etcFormat = EtcRGB8A1;
                format = GL_RGBA;
                break;
            case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
                etcFormat = EtcRGB8A1;
                format = GL_RGBA;
                break;
        }
        int pixelSize = etc_get_decoded_pixel_size(etcFormat);
        GLsizei compressedSize =
            etc_get_encoded_data_size(etcFormat, width, height);
        SET_ERROR_IF((compressedSize != imageSize), GL_INVALID_VALUE);
        std::unique_ptr<ScopedFetchUnpackData> unpackData;
        std::unique_ptr<char[]> emulatedData;
        if (needUnpackBuffer) {
            unpackData.reset(new ScopedFetchUnpackData(ctx,
                    reinterpret_cast<GLintptr>(data), compressedSize));
            data = unpackData->data();
            SET_ERROR_IF(!data, GL_INVALID_OPERATION);
        } else {
            if (!data) {
                emulatedData.reset(new char[compressedSize]);
                data = emulatedData.get();
            }
        }

        const int32_t align = unpackAlignment - 1;
        const int32_t bpr = ((width * pixelSize) + align) & ~align;
        const size_t size = bpr * height;
        std::unique_ptr<etc1_byte[]> pOut(new etc1_byte[size]);

        int res =
            etc2_decode_image(
                    (const etc1_byte*)data, etcFormat, pOut.get(),
                    width, height, bpr);
        SET_ERROR_IF(res!=0, GL_INVALID_VALUE);

        glTexImage2DPtr(target, level, convertedInternalFormat,
                        width, height, border, format, type, pOut.get());
    } else if (isAstcFormat(internalformat)) {
        std::unique_ptr<ScopedFetchUnpackData> unpackData;
        std::unique_ptr<char[]> emulatedData;
        if (needUnpackBuffer) {
            unpackData.reset(
                new ScopedFetchUnpackData(ctx, reinterpret_cast<GLintptr>(data), imageSize));
            data = unpackData->data();
            SET_ERROR_IF(!data, GL_INVALID_OPERATION);
        } else {
            if (!data) {
                emulatedData.reset(new char[imageSize]);
                data = emulatedData.get();
            }
        }
        uint32_t blockWidth = 0;
        uint32_t blockHeight = 0;
        bool srgb;
        getAstcFormatInfo(internalformat, &blockWidth, &blockHeight, &srgb);

        const int32_t align = unpackAlignment - 1;
        const int32_t stride = ((width * 4) + align) & ~align;
        const size_t size = stride * height;

        AlignedBuf<uint8_t, 64> alignedUncompressedData(size);

        const bool result = astcDecompress(
                reinterpret_cast<const uint8_t*>(data), imageSize, width,
                height, blockWidth, blockHeight, alignedUncompressedData.data(), size);
        SET_ERROR_IF(!result, GL_INVALID_VALUE);

        glTexImage2DPtr(target, level, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width,
                        height, border, GL_RGBA, GL_UNSIGNED_BYTE,
                        alignedUncompressedData.data());
    } else if (isPaletteFormat(internalformat)) {
        // TODO: fix the case when GL_PIXEL_UNPACK_BUFFER is bound
        SET_ERROR_IF(
            level > log2(ctx->getMaxTexSize()) ||
            border !=0 || level > 0 ||
            !GLESvalidate::texImgDim(
                width, height, ctx->getMaxTexSize() + 2),
            GL_INVALID_VALUE);
        SET_ERROR_IF(!data,GL_INVALID_OPERATION);
        //the decoder fully packed the pixels.
        ctx->dispatcher().glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        int nMipmaps = -level + 1;
        GLsizei tmpWidth  = width;
        GLsizei tmpHeight = height;

        for(int i = 0; i < nMipmaps; i++)
        {
            GLenum uncompressedFrmt;
            unsigned char* uncompressed =
                uncompressTexture(internalformat, uncompressedFrmt,
                                  width, height, imageSize, data, i);
            glTexImage2DPtr(target, i, uncompressedFrmt,
                            tmpWidth, tmpHeight, border,
                            uncompressedFrmt, GL_UNSIGNED_BYTE, uncompressed);
            tmpWidth /= 2;
            tmpHeight /= 2;
            delete [] uncompressed;
        }
    } else if (isRgtcFormat(internalformat)) {
        GLint format = 0, type = 0;
        GLint convertedInternalFormat = decompressedInternalFormat(ctx, internalformat);
        RGTCImageFormat rgtcFormat = BC4_UNORM;
        switch (internalformat) {
            case GL_COMPRESSED_RED_RGTC1_EXT:               // BC4U
                format = GL_RED;
                type = GL_UNSIGNED_BYTE;
                rgtcFormat = BC4_UNORM;
                break;
            case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:        // BC4S
                format = GL_RED;
                type = GL_BYTE;
                rgtcFormat = BC4_SNORM;
                break;
            case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:         // BC5U
                format = GL_RG;
                type = GL_UNSIGNED_BYTE;
                rgtcFormat = BC5_UNORM;
                break;
            case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:  // BC5S
                format = GL_RG;
                type = GL_BYTE;
                rgtcFormat = BC5_SNORM;
                break;
        }
        size_t pixelSize = rgtc_get_decoded_pixel_size(rgtcFormat);
        GLsizei compressedSize = rgtc_get_encoded_image_size(rgtcFormat, width, height);
        SET_ERROR_IF((compressedSize != imageSize), GL_INVALID_VALUE);
        std::unique_ptr<ScopedFetchUnpackData> unpackData;
        std::unique_ptr<char[]> emulatedData;
        if (needUnpackBuffer) {
            unpackData.reset(
                new ScopedFetchUnpackData(ctx, reinterpret_cast<GLintptr>(data), compressedSize));
            data = unpackData->data();
            SET_ERROR_IF(!data, GL_INVALID_OPERATION);
        } else {
            if (!data) {
                emulatedData.reset(new char[compressedSize]);
                data = emulatedData.get();
            }
        }
        const int32_t align = unpackAlignment - 1;
        const int32_t bpr = ((width * pixelSize) + align) & ~align;
        const size_t size = bpr * height;
        std::unique_ptr<uint8_t[]> pOut(new uint8_t[size]);

        int res =
            rgtc_decode_image((const uint8_t*)data, rgtcFormat, pOut.get(), width, height, bpr);
        SET_ERROR_IF(res != 0, GL_INVALID_VALUE);
        glTexImage2DPtr(target, level, convertedInternalFormat, width, height, border, format, type,
                        pOut.get());
    } else {
        SET_ERROR_IF(1, GL_INVALID_ENUM);
    }
}

void deleteRenderbufferGlobal(GLuint rbo) {
    if (rbo) {
        GLEScontext::dispatcher().glDeleteRenderbuffers(1, &rbo);
    }
}

bool isCubeMapFaceTarget(GLenum target) {
    switch (target) {
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            return true;
    }
    return false;
}

bool isCoreProfileEmulatedFormat(GLenum format) {
    switch (format) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
        return true;
    default:
        return false;
    }
}

GLenum getCoreProfileEmulatedFormat(GLenum format) {
    switch (format) {
        case GL_ALPHA:
        case GL_LUMINANCE:
            return GL_RED;
        case GL_LUMINANCE_ALPHA:
            return GL_RG;
    }
    return format;
}

GLint getCoreProfileEmulatedInternalFormat(GLint internalformat, GLenum type) {
    switch (internalformat) {
        case GL_ALPHA:
        case GL_LUMINANCE:
            switch (type) {
                case GL_UNSIGNED_BYTE:
                    return GL_R8;
                case GL_FLOAT:
                    return GL_R32F;
                case GL_HALF_FLOAT:
                    return GL_R16F;
            }
            return GL_R8;
        case GL_LUMINANCE_ALPHA:
            switch (type) {
                case GL_UNSIGNED_BYTE:
                    return GL_RG8;
                case GL_FLOAT:
                    return GL_RG32F;
                case GL_HALF_FLOAT:
                    return GL_RG16F;
            }
            return GL_RG8;
    }
    fprintf(stderr,
            "%s: warning: unsupported alpha/luminance internal format 0x%x type 0x%x\n",
            __func__, internalformat, type);
    return GL_R8;
}

TextureSwizzle getSwizzleForEmulatedFormat(GLenum format) {
    TextureSwizzle res;
    switch (format) {
        case GL_ALPHA:
            res.toRed   = GL_ZERO;
            res.toGreen = GL_ZERO;
            res.toBlue  = GL_ZERO;
            res.toAlpha = GL_RED;
            break;
        case GL_LUMINANCE:
            res.toRed   = GL_RED;
            res.toGreen = GL_RED;
            res.toBlue  = GL_RED;
            res.toAlpha = GL_ONE;
            break;
        case GL_LUMINANCE_ALPHA:
            res.toRed   = GL_RED;
            res.toGreen = GL_RED;
            res.toBlue  = GL_RED;
            res.toAlpha = GL_GREEN;
            break;
        default:
            break;
    }
    return res;
}

// Inverse swizzle: if we were writing fragments back to this texture,
// how should the components be re-arranged?
TextureSwizzle getInverseSwizzleForEmulatedFormat(GLenum format) {
    TextureSwizzle res;
    switch (format) {
        case GL_ALPHA:
            res.toRed   = GL_ALPHA;
            res.toGreen = GL_ZERO;
            res.toBlue  = GL_ZERO;
            res.toAlpha = GL_ZERO;
            break;
        case GL_LUMINANCE:
            res.toRed   = GL_RED;
            res.toGreen = GL_ZERO;
            res.toBlue  = GL_ZERO;
            res.toAlpha = GL_ZERO;
            break;
        case GL_LUMINANCE_ALPHA:
            res.toRed   = GL_RED;
            res.toGreen = GL_ALPHA;
            res.toBlue  = GL_ZERO;
            res.toAlpha = GL_ZERO;
            break;
        default:
            break;
    }
    return res;
}

GLenum swizzleComponentOf(const TextureSwizzle& s, GLenum component) {
    switch (component) {
    case GL_RED: return s.toRed;
    case GL_GREEN: return s.toGreen;
    case GL_BLUE: return s.toBlue;
    case GL_ALPHA: return s.toAlpha;
    }
    // Identity map for GL_ZERO / GL_ONE
    return component;
}

TextureSwizzle concatSwizzles(const TextureSwizzle& first,
                              const TextureSwizzle& next) {

    TextureSwizzle result;
    result.toRed = swizzleComponentOf(first, next.toRed);
    result.toGreen = swizzleComponentOf(first, next.toGreen);
    result.toBlue = swizzleComponentOf(first, next.toBlue);
    result.toAlpha = swizzleComponentOf(first, next.toAlpha);
    return result;
}

bool isSwizzleParam(GLenum pname) {
    switch (pname) {
    case GL_TEXTURE_SWIZZLE_R:
    case GL_TEXTURE_SWIZZLE_G:
    case GL_TEXTURE_SWIZZLE_B:
    case GL_TEXTURE_SWIZZLE_A:
        return true;
    default:
        return false;
    }
}

bool isIntegerInternalFormat(GLint internalformat) {
    switch (internalformat) {
        case GL_R8I:
        case GL_R8UI:
        case GL_R16I:
        case GL_R16UI:
        case GL_R32I:
        case GL_R32UI:
        case GL_RG8I:
        case GL_RG8UI:
        case GL_RG16I:
        case GL_RG16UI:
        case GL_RG32I:
        case GL_RG32UI:
        case GL_RGB8I:
        case GL_RGB8UI:
        case GL_RGB16I:
        case GL_RGB16UI:
        case GL_RGB32I:
        case GL_RGB32UI:
        case GL_RGBA8I:
        case GL_RGBA8UI:
        case GL_RGBA16I:
        case GL_RGBA16UI:
        case GL_RGBA32I:
        case GL_RGBA32UI:
            return true;
        default:
            return false;
    }
}

void doCompressedTexImage2DNative(GLEScontext* ctx, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data) {
    // AlignedBuf<uint8_t, 64> alignedData(imageSize);
    // memcpy(alignedData.data(), data, imageSize);
    // GLint err = ctx->dispatcher().glGetError();
    ctx->dispatcher().glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
    //     fprintf(stderr, "%s: tex %u target 0x%x level 0x%x iformat 0x%x w h b %d %d %d imgSize %d\n", __func__, ctx->getBindedTexture(target), target, level, internalformat, width, height, border, imageSize);
    // err = ctx->dispatcher().glGetError(); if (err) {
    //     fprintf(stderr, "%s:%d err 0x%x\n", __func__, __LINE__, err);
    // }
}

void doCompressedTexSubImage2DNative(GLEScontext* ctx, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid* data) {
    // AlignedBuf<uint8_t, 64> alignedData(imageSize);
    // memcpy(alignedData.data(), data, imageSize);
    // GLint err = ctx->dispatcher().glGetError();
    ctx->dispatcher().glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
    //     fprintf(stderr, "%s: tex %u target 0x%x level 0x%x format 0x%x x y w h %d %d %d %d imgSize %d\n", __func__, ctx->getBindedTexture(target), target, level, format, xoffset, yoffset, width, height, imageSize);
    // err = ctx->dispatcher().glGetError(); if (err) {
    //     fprintf(stderr, "%s:%d err 0x%x\n", __func__, __LINE__, err);
    // }
}

void forEachEtc2Format(std::function<void(GLint format)> f) {
    f(GL_COMPRESSED_RGB8_ETC2);
    f(GL_COMPRESSED_SRGB8_ETC2);
    f(GL_COMPRESSED_RGBA8_ETC2_EAC);
    f(GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC);
    f(GL_COMPRESSED_R11_EAC);
    f(GL_COMPRESSED_SIGNED_R11_EAC);
    f(GL_COMPRESSED_RG11_EAC);
    f(GL_COMPRESSED_SIGNED_RG11_EAC);
    f(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2);
    f(GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2);
}

void forEachAstcFormat(std::function<void(GLint format)> f) {

#define CALL_ON_ASTC_FORMAT(typeName, blockWidth, blockHeight, srgbValue) \
    f(typeName);

    ASTC_FORMATS_LIST(CALL_ON_ASTC_FORMAT)
}

void forEachBptcFormat(std::function<void(GLint format)> f) {
    f(GL_COMPRESSED_RGBA_BPTC_UNORM_EXT);
    f(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT);
    f(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT);
    f(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT);
}

void forEachS3tcFormat(std::function<void(GLint format)> f) {
    f(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
    f(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
    f(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT);
    f(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
    f(GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT);
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT);
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT);
}

bool isRgtcFormat(GLenum format) {
    switch (format) {
        case GL_COMPRESSED_RED_RGTC1_EXT: // BC4U
        case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT: // BC4S
        case GL_COMPRESSED_RED_GREEN_RGTC2_EXT: // BC5U
        case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT: // BC5S
            return true;
        default:
            break;
    }
    return false;
}

bool isEtc2OrAstcFormat(GLenum format) {
    switch (format) {
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
    default:
        break;
    }
    return isAstcFormat(format);
}

bool shouldPassthroughCompressedFormat(GLEScontext* ctx, GLenum internalformat) {
    if (isEtc2Format(internalformat)) {
        return ctx->getCaps()->hasEtc2Support;
    } else if (isAstcFormat(internalformat)) {
        return ctx->getCaps()->hasAstcSupport;
    } else if (isBptcFormat(internalformat)) {
        return ctx->getCaps()->hasBptcSupport;
    } else if (isS3tcFormat(internalformat)) {
        return ctx->getCaps()->hasS3tcSupport;
    } else if (isRgtcFormat(internalformat)) {
        return ctx->getCaps()->hasRgtcSupport;
    }
    return false;
}

static uint32_t s_texAlign(uint32_t v, uint32_t align) {
    uint32_t rem = v % align;
    return rem ? (v + (align - rem)) : v;
}

// s_computePixelSize is both in the host and the guest. Consider moving it to
// android-emugl/shared

static int s_computePixelSize(GLenum format, GLenum type) {
#define FORMAT_ERROR(format, type)                                         \
    fprintf(stderr, "%s:%d unknown format/type 0x%x 0x%x\n", __FUNCTION__, \
            __LINE__, format, type);

    switch (type) {
        case GL_BYTE:
            switch (format) {
                case GL_R8:
                case GL_R8I:
                case GL_R8_SNORM:
                case GL_RED:
                    return 1;
                case GL_RED_INTEGER:
                    return 1;
                case GL_RG8:
                case GL_RG8I:
                case GL_RG8_SNORM:
                case GL_RG:
                    return 1 * 2;
                case GL_RG_INTEGER:
                    return 1 * 2;
                case GL_RGB8:
                case GL_RGB8I:
                case GL_RGB8_SNORM:
                case GL_RGB:
                    return 1 * 3;
                case GL_RGB_INTEGER:
                    return 1 * 3;
                case GL_RGBA8:
                case GL_RGBA8I:
                case GL_RGBA8_SNORM:
                case GL_RGBA:
                    return 1 * 4;
                case GL_RGBA_INTEGER:
                    return 1 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_BYTE:
            switch (format) {
                case GL_R8:
                case GL_R8UI:
                case GL_RED:
                    return 1;
                case GL_RED_INTEGER:
                    return 1;
                case GL_ALPHA8_EXT:
                case GL_ALPHA:
                    return 1;
                case GL_LUMINANCE8_EXT:
                case GL_LUMINANCE:
                    return 1;
                case GL_LUMINANCE8_ALPHA8_EXT:
                case GL_LUMINANCE_ALPHA:
                    return 1 * 2;
                case GL_RG8:
                case GL_RG8UI:
                case GL_RG:
                    return 1 * 2;
                case GL_RG_INTEGER:
                    return 1 * 2;
                case GL_RGB8:
                case GL_RGB8UI:
                case GL_SRGB8:
                case GL_RGB:
                    return 1 * 3;
                case GL_RGB_INTEGER:
                    return 1 * 3;
                case GL_RGBA8:
                case GL_RGBA8UI:
                case GL_SRGB8_ALPHA8:
                case GL_RGBA:
                    return 1 * 4;
                case GL_RGBA_INTEGER:
                    return 1 * 4;
                case GL_BGRA_EXT:
                case GL_BGRA8_EXT:
                    return 1 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_SHORT:
            switch (format) {
                case GL_R16I:
                case GL_RED_INTEGER:
                    return 2;
                case GL_RG16I:
                case GL_RG_INTEGER:
                    return 2 * 2;
                case GL_RGB16I:
                case GL_RGB_INTEGER:
                    return 2 * 3;
                case GL_RGBA16I:
                case GL_RGBA_INTEGER:
                    return 2 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_SHORT:
            switch (format) {
                case GL_DEPTH_COMPONENT16:
                case GL_DEPTH_COMPONENT:
                    return 2;
                case GL_R16UI:
                case GL_RED_INTEGER:
                    return 2;
                case GL_RG16UI:
                case GL_RG_INTEGER:
                    return 2 * 2;
                case GL_RGB16UI:
                case GL_RGB_INTEGER:
                    return 2 * 3;
                case GL_RGBA16UI:
                case GL_RGBA_INTEGER:
                    return 2 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_INT:
            switch (format) {
                case GL_R32I:
                case GL_RED_INTEGER:
                    return 4;
                case GL_RG32I:
                case GL_RG_INTEGER:
                    return 4 * 2;
                case GL_RGB32I:
                case GL_RGB_INTEGER:
                    return 4 * 3;
                case GL_RGBA32I:
                case GL_RGBA_INTEGER:
                    return 4 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_INT:
            switch (format) {
                case GL_DEPTH_COMPONENT16:
                case GL_DEPTH_COMPONENT24:
                case GL_DEPTH_COMPONENT32_OES:
                case GL_DEPTH_COMPONENT:
                    return 4;
                case GL_R32UI:
                case GL_RED_INTEGER:
                    return 4;
                case GL_RG32UI:
                case GL_RG_INTEGER:
                    return 4 * 2;
                case GL_RGB32UI:
                case GL_RGB_INTEGER:
                    return 4 * 3;
                case GL_RGBA32UI:
                case GL_RGBA_INTEGER:
                    return 4 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4_REV_EXT:
        case GL_UNSIGNED_SHORT_1_5_5_5_REV_EXT:
            return 2;
        case GL_UNSIGNED_INT_10F_11F_11F_REV:
        case GL_UNSIGNED_INT_5_9_9_9_REV:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_24_8_OES:
            return 4;
        case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
            return 4 + 4;
        case GL_FLOAT:
            switch (format) {
                case GL_DEPTH_COMPONENT32F:
                case GL_DEPTH_COMPONENT:
                    return 4;
                case GL_ALPHA32F_EXT:
                case GL_ALPHA:
                    return 4;
                case GL_LUMINANCE32F_EXT:
                case GL_LUMINANCE:
                    return 4;
                case GL_LUMINANCE_ALPHA32F_EXT:
                case GL_LUMINANCE_ALPHA:
                    return 4 * 2;
                case GL_RED:
                    return 4;
                case GL_R32F:
                    return 4;
                case GL_RG:
                    return 4 * 2;
                case GL_RG32F:
                    return 4 * 2;
                case GL_RGB:
                    return 4 * 3;
                case GL_RGB32F:
                    return 4 * 3;
                case GL_RGBA:
                    return 4 * 4;
                case GL_RGBA32F:
                    return 4 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_HALF_FLOAT:
        case GL_HALF_FLOAT_OES:
            switch (format) {
                case GL_ALPHA16F_EXT:
                case GL_ALPHA:
                    return 2;
                case GL_LUMINANCE16F_EXT:
                case GL_LUMINANCE:
                    return 2;
                case GL_LUMINANCE_ALPHA16F_EXT:
                case GL_LUMINANCE_ALPHA:
                    return 2 * 2;
                case GL_RED:
                    return 2;
                case GL_R16F:
                    return 2;
                case GL_RG:
                    return 2 * 2;
                case GL_RG16F:
                    return 2 * 2;
                case GL_RGB:
                    return 2 * 3;
                case GL_RGB16F:
                    return 2 * 3;
                case GL_RGBA:
                    return 2 * 4;
                case GL_RGBA16F:
                    return 2 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        default:
            FORMAT_ERROR(format, type);
    }

    return 0;
}

uint32_t texImageSize(GLenum internalformat,
                      GLenum type,
                      int unpackAlignment,
                      GLsizei width,
                      GLsizei height) {

    uint32_t alignedWidth = s_texAlign(width, unpackAlignment);
    uint32_t pixelSize = s_computePixelSize(internalformat, type);
    uint32_t totalSize = pixelSize * alignedWidth * height;

    return totalSize;
}

GLenum getFormatFromInternalFormat(GLint internalFormat) {
    switch (internalFormat) {
        case GL_R8:
            return GL_RED;
        case GL_RG8:
            return GL_RG;
        case GL_RGB8:
        case GL_RGB565:
        case GL_RGB16F:
            return GL_RGB;
        case GL_RGBA8:
        case GL_RGB5_A1_OES:
        case GL_RGBA4_OES:
        case GL_UNSIGNED_INT_10_10_10_2_OES:
        case GL_RGB10_A2:
        case GL_RGBA16F:
            return GL_RGBA;
        case GL_BGRA8_EXT:
            return GL_BGRA_EXT;
        default: // already unsized
            return internalFormat;
    }
}

GLenum getTypeFromInternalFormat(GLint internalFormat) {
    switch (internalFormat) {
        case GL_RGB:
        case GL_RGB8:
            return GL_UNSIGNED_BYTE;
        case GL_RGB565_OES:
            return GL_UNSIGNED_SHORT_5_6_5;
        case GL_RGBA:
        case GL_RGBA8:
        case GL_RGB5_A1_OES:
        case GL_RGBA4_OES:
            return GL_UNSIGNED_BYTE;
        case GL_UNSIGNED_INT_10_10_10_2_OES:
            return GL_UNSIGNED_SHORT;
        case GL_RGB10_A2:
            return GL_UNSIGNED_INT_2_10_10_10_REV;
        case GL_RGB16F:
            return GL_HALF_FLOAT;
        case GL_RGBA16F:
            return GL_HALF_FLOAT;
        case GL_LUMINANCE:
            return GL_UNSIGNED_SHORT;
        case GL_BGRA_EXT:
            return GL_UNSIGNED_BYTE;
        case GL_R8:
        case GL_RED:
            return GL_UNSIGNED_BYTE;
        case GL_RG8:
        case GL_RG:
            return GL_UNSIGNED_BYTE;
        default:
            fprintf(stderr, "%s: Unknown format 0x%x\n", __func__,
                    internalFormat);
            return GL_UNSIGNED_BYTE;
    }
}


GLint TextureUnpackReset::unpackCheckAndUpdate(GLenum name, GLint newValue) {
    GLint curValue;
    glesContext->dispatcher().glGetIntegerv(name, &curValue);
    if (curValue != newValue) {
        glesContext->dispatcher().glPixelStorei(name, newValue);
    }
    return curValue;
}

TextureUnpackReset::TextureUnpackReset(GLEScontext* ctx) : glesContext(ctx) {
    unpackAlignment = unpackCheckAndUpdate(GL_UNPACK_ALIGNMENT, kUnpackAlignment);
    if (glesContext->getMajorVersion() >= 3) {
        unpackRowLength = unpackCheckAndUpdate(GL_UNPACK_ROW_LENGTH, kUnpackRowLength);
        unpackImageHeight = unpackCheckAndUpdate(GL_UNPACK_IMAGE_HEIGHT, kUnpackImageHeight);
        unpackSkipRows = unpackCheckAndUpdate(GL_UNPACK_SKIP_ROWS, kUnpackSkipRows);
        unpackSkipPixels = unpackCheckAndUpdate(GL_UNPACK_SKIP_PIXELS, kUnpackSkipPixels);
        unpackSkipImages = unpackCheckAndUpdate(GL_UNPACK_SKIP_IMAGES, kUnpackSkipImages);
    } else {
        // avoid clang-tidy warnings on uninitialized values
        unpackRowLength = 0;
        unpackImageHeight = 0;
        unpackSkipRows = 0;
        unpackSkipPixels = 0;
        unpackSkipImages = 0;
    }
}
TextureUnpackReset::~TextureUnpackReset() {
    unpackCheckAndUpdate(GL_UNPACK_ALIGNMENT, unpackAlignment);
    if (glesContext->getMajorVersion() >= 3) {
        unpackCheckAndUpdate(GL_UNPACK_ROW_LENGTH, unpackRowLength);
        unpackCheckAndUpdate(GL_UNPACK_IMAGE_HEIGHT, unpackImageHeight);
        unpackCheckAndUpdate(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
        unpackCheckAndUpdate(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
        unpackCheckAndUpdate(GL_UNPACK_SKIP_IMAGES, unpackSkipImages);
    }
}

