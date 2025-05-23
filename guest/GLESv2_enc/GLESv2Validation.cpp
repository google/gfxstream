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

#include "GLESv2Validation.h"

#include <sstream>

#include "gfxstream/common/logging.h"

#define LIST_VALID_TEX_INTERNALFORMATS(f) \
    f(GL_BGRA8_EXT) \
    f(GL_R8) \
    f(GL_R8_SNORM) \
    f(GL_R16F) \
    f(GL_R32F) \
    f(GL_R8UI) \
    f(GL_R8I) \
    f(GL_R16UI) \
    f(GL_R16I) \
    f(GL_R32UI) \
    f(GL_R32I) \
    f(GL_RG8) \
    f(GL_RG8_SNORM) \
    f(GL_RG16F) \
    f(GL_RG32F) \
    f(GL_RG8UI) \
    f(GL_RG8I) \
    f(GL_RG16UI) \
    f(GL_RG16I) \
    f(GL_RG32UI) \
    f(GL_RG32I) \
    f(GL_RGB8) \
    f(GL_SRGB8) \
    f(GL_RGB565) \
    f(GL_RGB8_SNORM) \
    f(GL_R11F_G11F_B10F) \
    f(GL_RGB9_E5) \
    f(GL_RGB16F) \
    f(GL_RGB32F) \
    f(GL_RGB8UI) \
    f(GL_RGB8I) \
    f(GL_RGB16UI) \
    f(GL_RGB16I) \
    f(GL_RGB32UI) \
    f(GL_RGB32I) \
    f(GL_RGBA8) \
    f(GL_SRGB8_ALPHA8) \
    f(GL_RGBA8_SNORM) \
    f(GL_RGB5_A1) \
    f(GL_RGBA4) \
    f(GL_RGB10_A2) \
    f(GL_RGBA16F) \
    f(GL_RGBA32F) \
    f(GL_RGBA8UI) \
    f(GL_RGBA8I) \
    f(GL_RGB10_A2UI) \
    f(GL_RGBA16UI) \
    f(GL_RGBA16I) \
    f(GL_RGBA32I) \
    f(GL_RGBA32UI) \
    f(GL_DEPTH_COMPONENT16) \
    f(GL_DEPTH_COMPONENT24) \
    f(GL_DEPTH_COMPONENT32F) \
    f(GL_DEPTH24_STENCIL8) \
    f(GL_DEPTH32F_STENCIL8) \
    f(GL_ETC1_RGB8_OES) \
    f(GL_COMPRESSED_R11_EAC) \
    f(GL_COMPRESSED_SIGNED_R11_EAC) \
    f(GL_COMPRESSED_RG11_EAC) \
    f(GL_COMPRESSED_SIGNED_RG11_EAC) \
    f(GL_COMPRESSED_RGB8_ETC2) \
    f(GL_COMPRESSED_SRGB8_ETC2) \
    f(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2) \
    f(GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2) \
    f(GL_COMPRESSED_RGBA8_ETC2_EAC) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC) \
    f(GL_COMPRESSED_RGBA_ASTC_4x4_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_5x4_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_5x5_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_6x5_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_6x6_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_8x5_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_8x6_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_8x8_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_10x5_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_10x6_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_10x8_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_10x10_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_12x10_KHR) \
    f(GL_COMPRESSED_RGBA_ASTC_12x12_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR) \
    f(GL_COMPRESSED_RGBA_BPTC_UNORM_EXT) \
    f(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT) \
    f(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT) \
    f(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT) \
    f(GL_COMPRESSED_RGB_S3TC_DXT1_EXT) \
    f(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) \
    f(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) \
    f(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) \
    f(GL_COMPRESSED_SRGB_S3TC_DXT1_EXT) \
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT) \
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT) \
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT) \
    f(GL_COMPRESSED_RED_RGTC1_EXT) \
    f(GL_COMPRESSED_SIGNED_RED_RGTC1_EXT) \
    f(GL_COMPRESSED_RED_GREEN_RGTC2_EXT) \
    f(GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT)

#define LIST_VALID_TEX_BUFFER_FORMATS(f) \
    f(GL_R8) \
    f(GL_R16F) \
    f(GL_R32F) \
    f(GL_R8I) \
    f(GL_R16I) \
    f(GL_R32I) \
    f(GL_R8UI) \
    f(GL_R16UI) \
    f(GL_R32UI) \
    f(GL_RG8) \
    f(GL_RG16F) \
    f(GL_RG32F) \
    f(GL_RG8I) \
    f(GL_RG16I) \
    f(GL_RG32I) \
    f(GL_RG8UI) \
    f(GL_RG16UI) \
    f(GL_RG32UI) \
    f(GL_RGB32F) \
    f(GL_RGB32I) \
    f(GL_RGB32UI) \
    f(GL_RGBA8) \
    f(GL_RGBA16F) \
    f(GL_RGBA32F) \
    f(GL_RGBA8I) \
    f(GL_RGBA16I) \
    f(GL_RGBA32I) \
    f(GL_RGBA8UI) \
    f(GL_RGBA16UI) \
    f(GL_RGBA32UI)


#define LIST_INTEGER_TEX_FORMATS(f) \
    f(GL_RED_INTEGER) \
    f(GL_RG_INTEGER) \
    f(GL_RGB_INTEGER) \
    f(GL_RGBA_INTEGER) \
    f(GL_R8UI) \
    f(GL_R8I) \
    f(GL_R16UI) \
    f(GL_R16I) \
    f(GL_R32UI) \
    f(GL_R32I) \
    f(GL_RG8UI) \
    f(GL_RG8I) \
    f(GL_RG16UI) \
    f(GL_RG16I) \
    f(GL_RG32UI) \
    f(GL_RG32I) \
    f(GL_RGB8UI) \
    f(GL_RGB8I) \
    f(GL_RGB16UI) \
    f(GL_RGB16I) \
    f(GL_RGB32UI) \
    f(GL_RGB32I) \
    f(GL_RGBA8UI) \
    f(GL_RGBA8I) \
    f(GL_RGB10_A2UI) \
    f(GL_RGBA16UI) \
    f(GL_RGBA16I) \
    f(GL_RGBA32I) \
    f(GL_RGBA32UI) \

#define LIST_VALID_TEXFORMAT_COMBINATIONS(f) \
    f(GL_BGRA8_EXT, GL_BGRA_EXT, GL_UNSIGNED_BYTE) \
    f(GL_R8, GL_RED, GL_UNSIGNED_BYTE) \
    f(GL_R8_SNORM, GL_RED, GL_BYTE) \
    f(GL_R16F, GL_RED, GL_FLOAT) \
    f(GL_R16F, GL_RED, GL_HALF_FLOAT) \
    f(GL_R32F, GL_RED, GL_FLOAT) \
    f(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE) \
    f(GL_R8I, GL_RED_INTEGER, GL_BYTE) \
    f(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT) \
    f(GL_R16I, GL_RED_INTEGER, GL_SHORT) \
    f(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT) \
    f(GL_R32I, GL_RED_INTEGER, GL_INT) \
    f(GL_RG8, GL_RG, GL_UNSIGNED_BYTE) \
    f(GL_RG8_SNORM, GL_RG, GL_BYTE) \
    f(GL_RG16F, GL_RG, GL_HALF_FLOAT) \
    f(GL_RG16F, GL_RG, GL_FLOAT) \
    f(GL_RG32F, GL_RG, GL_FLOAT) \
    f(GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE) \
    f(GL_RG8I, GL_RG_INTEGER, GL_BYTE) \
    f(GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT) \
    f(GL_RG16I, GL_RG_INTEGER, GL_SHORT) \
    f(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT) \
    f(GL_RG32I, GL_RG_INTEGER, GL_INT) \
    f(GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_RGB565, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5) \
    f(GL_RGB8_SNORM, GL_RGB, GL_BYTE) \
    f(GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV) \
    f(GL_R11F_G11F_B10F, GL_RGB, GL_HALF_FLOAT) \
    f(GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT) \
    f(GL_RGB9_E5, GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV) \
    f(GL_RGB9_E5, GL_RGB, GL_HALF_FLOAT) \
    f(GL_RGB9_E5, GL_RGB, GL_FLOAT) \
    f(GL_RGB16F, GL_RGB, GL_HALF_FLOAT) \
    f(GL_RGB16F, GL_RGB, GL_FLOAT) \
    f(GL_RGB32F, GL_RGB, GL_FLOAT) \
    f(GL_RGB8UI, GL_RGB_INTEGER, GL_UNSIGNED_BYTE) \
    f(GL_RGB8I, GL_RGB_INTEGER, GL_BYTE) \
    f(GL_RGB16UI, GL_RGB_INTEGER, GL_UNSIGNED_SHORT) \
    f(GL_RGB16I, GL_RGB_INTEGER, GL_SHORT) \
    f(GL_RGB32UI, GL_RGB_INTEGER, GL_UNSIGNED_INT) \
    f(GL_RGB32I, GL_RGB_INTEGER, GL_INT) \
    f(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_RGBA8_SNORM, GL_RGBA, GL_BYTE) \
    f(GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1) \
    f(GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV) \
    f(GL_RGBA4, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4) \
    f(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV) \
    f(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) \
    f(GL_RGBA16F, GL_RGBA, GL_FLOAT) \
    f(GL_RGBA32F, GL_RGBA, GL_FLOAT) \
    f(GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE) \
    f(GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE) \
    f(GL_RGB10_A2UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV) \
    f(GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT) \
    f(GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT) \
    f(GL_RGBA32I, GL_RGBA_INTEGER, GL_INT) \
    f(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT) \
    f(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT) \
    f(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT) \
    f(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT) \
    f(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT) \
    f(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8) \
    f(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV) \
    f(GL_COMPRESSED_R11_EAC, GL_RED, GL_FLOAT) \
    f(GL_COMPRESSED_SIGNED_R11_EAC, GL_RED, GL_FLOAT) \
    f(GL_COMPRESSED_RG11_EAC, GL_RG, GL_FLOAT) \
    f(GL_COMPRESSED_SIGNED_RG11_EAC, GL_RG, GL_FLOAT) \
    f(GL_COMPRESSED_RGB8_ETC2, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ETC2, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA8_ETC2_EAC, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_4x4_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_5x4_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_5x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_6x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_6x6_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_8x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_8x6_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_8x8_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_10x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_10x6_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_10x8_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_10x10_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_12x10_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_ASTC_12x12_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_BPTC_UNORM_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_RGB, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_RED_RGTC1_EXT, GL_RED, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SIGNED_RED_RGTC1_EXT, GL_RED, GL_BYTE) \
    f(GL_COMPRESSED_RED_GREEN_RGTC2_EXT, GL_RG, GL_UNSIGNED_BYTE) \
    f(GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, GL_RG, GL_BYTE)



namespace GLESv2Validation {

GLbitfield allBufferMapAccessFlags =
    GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
    GL_MAP_INVALIDATE_RANGE_BIT |
    GL_MAP_INVALIDATE_BUFFER_BIT |
    GL_MAP_FLUSH_EXPLICIT_BIT |
    GL_MAP_UNSYNCHRONIZED_BIT;

bool bufferTarget(GL2Encoder* ctx, GLenum target) {
    int glesMajorVersion = ctx->majorVersion();
    int glesMinorVersion = ctx->minorVersion();
    switch (target) {
    case GL_ARRAY_BUFFER: // Vertex attributes
    case GL_ELEMENT_ARRAY_BUFFER: // Vertex array indices
        return true;
        // GLES 3.0 buffers
    case GL_COPY_READ_BUFFER: // Buffer copy source
    case GL_COPY_WRITE_BUFFER: // Buffer copy destination
    case GL_PIXEL_PACK_BUFFER: // Pixel read target
    case GL_PIXEL_UNPACK_BUFFER: // Texture data source
    case GL_TRANSFORM_FEEDBACK_BUFFER: // Transform feedback buffer
    case GL_UNIFORM_BUFFER: // Uniform block storage
        return glesMajorVersion >= 3;
        // GLES 3.1 buffers
    case GL_ATOMIC_COUNTER_BUFFER: // Atomic counter storage
    case GL_DISPATCH_INDIRECT_BUFFER: // Indirect compute dispatch commands
    case GL_DRAW_INDIRECT_BUFFER: // Indirect command arguments
    case GL_SHADER_STORAGE_BUFFER: // Read-write storage for shader
        return glesMajorVersion >= 3 && glesMinorVersion >= 1;
    case GL_TEXTURE_BUFFER_OES:
        return ctx->es32Plus() || ctx->getExtensions().textureBufferAny();
    default:
        return false;
    }
}

bool bufferParam(GL2Encoder* ctx, GLenum pname) {
    int glesMajorVersion = ctx->majorVersion();
    switch (pname) {
    case GL_BUFFER_SIZE:
    case GL_BUFFER_USAGE:
        return true;
    case GL_BUFFER_ACCESS_FLAGS:
    case GL_BUFFER_MAPPED:
    case GL_BUFFER_MAP_LENGTH:
    case GL_BUFFER_MAP_OFFSET:
        return glesMajorVersion >= 3;
    default:
        return false;
    }
}

bool bufferUsage(GL2Encoder* ctx, GLenum usage) {
    int glesMajorVersion = ctx->majorVersion();
    switch(usage) {
        case GL_STREAM_DRAW:
        case GL_STATIC_DRAW:
        case GL_DYNAMIC_DRAW:
            return true;
        case GL_STREAM_READ:
        case GL_STATIC_READ:
        case GL_DYNAMIC_READ:
        case GL_STREAM_COPY:
        case GL_STATIC_COPY:
        case GL_DYNAMIC_COPY:
            return glesMajorVersion >= 3;
    }
    return false;

}
bool pixelStoreParam(GL2Encoder* ctx, GLenum param) {
    int glesMajorVersion = ctx->majorVersion();
    switch(param) {
    case GL_UNPACK_ALIGNMENT:
    case GL_PACK_ALIGNMENT:
        return true;
    case GL_UNPACK_ROW_LENGTH:
    case GL_UNPACK_IMAGE_HEIGHT:
    case GL_UNPACK_SKIP_PIXELS:
    case GL_UNPACK_SKIP_ROWS:
    case GL_UNPACK_SKIP_IMAGES:
    case GL_PACK_ROW_LENGTH:
    case GL_PACK_SKIP_PIXELS:
    case GL_PACK_SKIP_ROWS:
        return glesMajorVersion >= 3;
    default:
        return false;
    }
}

bool pixelStoreValue(GLenum param, GLint value) {
    switch(param) {
    case GL_UNPACK_ALIGNMENT:
    case GL_PACK_ALIGNMENT:
        return (value == 1) || (value == 2) || (value == 4) || (value == 8);
    case GL_UNPACK_ROW_LENGTH:
    case GL_UNPACK_IMAGE_HEIGHT:
    case GL_UNPACK_SKIP_PIXELS:
    case GL_UNPACK_SKIP_ROWS:
    case GL_UNPACK_SKIP_IMAGES:
    case GL_PACK_ROW_LENGTH:
    case GL_PACK_SKIP_PIXELS:
    case GL_PACK_SKIP_ROWS:
        return value >= 0;
    default:
        return false;
    }
}

bool rboFormat(GL2Encoder* ctx, GLenum internalformat) {
    int glesMajorVersion = ctx->majorVersion();

    switch (internalformat) {
    // Funny internal formats
    // that will cause an incomplete framebuffer
    // attachment error pre-gles3. For dEQP,
    // we can also just abort early here in
    // RenderbufferStorage with a GL_INVALID_ENUM.
    case GL_DEPTH_COMPONENT32F:
    case GL_R8:
    case GL_R8UI:
    case GL_R8I:
    case GL_R16UI:
    case GL_R16I:
    case GL_R32UI:
    case GL_R32I:
    case GL_RG8:
    case GL_RG8UI:
    case GL_RG8I:
    case GL_RG16UI:
    case GL_RG16I:
    case GL_RG32UI:
    case GL_RG32I:
    case GL_SRGB8_ALPHA8:
    case GL_RGBA8UI:
    case GL_RGBA8I:
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
    case GL_RGBA16UI:
    case GL_RGBA16I:
    case GL_RGBA32I:
    case GL_RGBA32UI:
    case GL_RGB32F:
        return glesMajorVersion >= 3;
        // These 4 formats are still not OK,
        // but dEQP expects GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT or
        // GL_FRAMEBUFFER_UNSUPPORTED if the extension is not present,
        // not a GL_INVALID_ENUM from earlier on.
        // So let's forward these to the rest of
        // FBO initialization
        // In GLES 3, these are rejected immediately if not
        // supported.
    case GL_R16F:
    case GL_RG16F:
    case GL_RGBA16F:
    case GL_R32F:
    case GL_RG32F:
    case GL_RGBA32F:
    case GL_R11F_G11F_B10F:
        return glesMajorVersion >= 3 && ctx->hasExtension("GL_EXT_color_buffer_float");
    case GL_RGB16F:
        return glesMajorVersion >= 3 && ctx->hasExtension("GL_EXT_color_buffer_half_float");
        // dEQP expects GL_FRAMEBUFFER_UNSUPPORTED or GL_FRAMEBUFFER_COMPLETE
        // for this format
        // These formats are OK
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32_OES:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGB565:
    case GL_RGB8_OES:
    case GL_RGBA8_OES:
    case GL_STENCIL_INDEX8:
    case GL_DEPTH32F_STENCIL8:
    case GL_DEPTH24_STENCIL8_OES:
        return true;
        break;
        // Everything else: still not OK,
        // and they need the GL_INVALID_ENUM
    }
    return false;
}

bool framebufferTarget(GL2Encoder* ctx, GLenum target) {
    int glesMajorVersion = ctx->majorVersion();
    switch (target) {
    case GL_FRAMEBUFFER:
        return true;
    case GL_DRAW_FRAMEBUFFER:
    case GL_READ_FRAMEBUFFER:
        return glesMajorVersion >= 3;
    }
    return false;
}

bool framebufferAttachment(GL2Encoder* ctx, GLenum attachment) {
    int glesMajorVersion = ctx->majorVersion();
    switch (attachment) {
    case GL_COLOR_ATTACHMENT0:
    case GL_DEPTH_ATTACHMENT:
    case GL_STENCIL_ATTACHMENT:
        return true;
    case GL_COLOR_ATTACHMENT1:
    case GL_COLOR_ATTACHMENT2:
    case GL_COLOR_ATTACHMENT3:
    case GL_COLOR_ATTACHMENT4:
    case GL_COLOR_ATTACHMENT5:
    case GL_COLOR_ATTACHMENT6:
    case GL_COLOR_ATTACHMENT7:
    case GL_COLOR_ATTACHMENT8:
    case GL_COLOR_ATTACHMENT9:
    case GL_COLOR_ATTACHMENT10:
    case GL_COLOR_ATTACHMENT11:
    case GL_COLOR_ATTACHMENT12:
    case GL_COLOR_ATTACHMENT13:
    case GL_COLOR_ATTACHMENT14:
    case GL_COLOR_ATTACHMENT15:
    case GL_DEPTH_STENCIL_ATTACHMENT:
        return glesMajorVersion >= 3;
    }
    return false;
}

bool readPixelsFormat(GLenum format) {
    switch (format) {
    case GL_RED:
    case GL_RED_INTEGER:
    case GL_RG:
    case GL_RG_INTEGER:
    case GL_RGB:
    case GL_RGB_INTEGER:
    case GL_RGBA:
    case GL_RGBA_INTEGER:
    case GL_LUMINANCE_ALPHA:
    case GL_LUMINANCE:
    case GL_ALPHA:
        return true;
    }
    return false;
}

bool readPixelsType(GLenum format) {
    switch (format) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
    case GL_HALF_FLOAT:
    case GL_FLOAT:
    case GL_INT:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_INT:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_10F_11F_11F_REV:
    case GL_UNSIGNED_INT_5_9_9_9_REV:
        return true;
    }
    return false;
}

bool pixelOp(GLenum format,GLenum type) {
     switch(type) {
     case GL_UNSIGNED_SHORT_4_4_4_4:
     case GL_UNSIGNED_SHORT_5_5_5_1:
         return format == GL_RGBA;
     case GL_UNSIGNED_SHORT_5_6_5:
         return format == GL_RGB;
     }
     return true;
}

bool vertexAttribType(GL2Encoder* ctx, GLenum type)
{
    int glesMajorVersion = ctx->majorVersion();
    bool retval = false;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_FIXED:
    case GL_FLOAT:
    // The following are technically only available if certain GLES2 extensions are.
    // However, they are supported by desktop GL3, which is a reasonable requirement
    // for the desktop GL version. Therefore, consider them valid.
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_HALF_FLOAT_OES:
        retval = true;
        break;
    case GL_HALF_FLOAT:
    case GL_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        retval = glesMajorVersion >= 3;
        break;
    }
    return retval;
}

bool readPixelsFboFormatMatch(GLenum, GLenum type, GLenum fboTexType) {
#define INVALID_TYPE_MATCH(x, y) \
    if (type == x && fboTexType == y) return false; \
    if (type == y && fboTexType == x) return false; \

    // These are meant to reject additional format/type mismatches
    // not caught by underlying system.
    INVALID_TYPE_MATCH(GL_FLOAT, GL_BYTE)
    INVALID_TYPE_MATCH(GL_FLOAT, GL_UNSIGNED_BYTE)
    INVALID_TYPE_MATCH(GL_FLOAT, GL_UNSIGNED_INT)
    INVALID_TYPE_MATCH(GL_FLOAT, GL_INT)

    return true;
}

bool blitFramebufferFormat(GLenum readFormat, GLenum drawFormat) {
#define INVALID_MATCH(x, y) \
    if (readFormat == x && drawFormat == y) return false; \
    if (readFormat == y && drawFormat == x) return false; \

    INVALID_MATCH(GL_FLOAT, GL_BYTE)
    INVALID_MATCH(GL_FLOAT, GL_UNSIGNED_BYTE)
    INVALID_MATCH(GL_FLOAT, GL_UNSIGNED_INT)
    INVALID_MATCH(GL_FLOAT, GL_INT)
    INVALID_MATCH(GL_INT, GL_UNSIGNED_BYTE);
    INVALID_MATCH(GL_UNSIGNED_INT, GL_UNSIGNED_BYTE);
    INVALID_MATCH(GL_INT, GL_BYTE);
    INVALID_MATCH(GL_UNSIGNED_INT, GL_BYTE);
    INVALID_MATCH(GL_DEPTH32F_STENCIL8, GL_DEPTH24_STENCIL8);

    return true;
}

bool textureTarget(GL2Encoder* ctx, GLenum target) {
    int glesMajorVersion = ctx->majorVersion();
    int glesMinorVersion = ctx->minorVersion();
    switch (target) {
    case GL_TEXTURE_EXTERNAL_OES:
    case GL_TEXTURE_2D:
    case GL_TEXTURE_CUBE_MAP:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return true;
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_3D:
        return glesMajorVersion >= 3;
    case GL_TEXTURE_2D_MULTISAMPLE:
        return glesMajorVersion >= 3 && glesMinorVersion >= 1;
    case GL_TEXTURE_BUFFER_OES:
        return  ctx->es32Plus() || ctx->getExtensions().textureBufferAny();
    default:
        break;
    }
    return false;
}

bool textureParams(GL2Encoder* ctx, GLenum param) {
    int glesMajorVersion = ctx->majorVersion();
    int glesMinorVersion = ctx->minorVersion();
    switch(param) {
    case GL_TEXTURE_MIN_FILTER:
    case GL_TEXTURE_MAG_FILTER:
    case GL_TEXTURE_WRAP_S:
    case GL_TEXTURE_WRAP_T:
    case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        return true;
    case GL_TEXTURE_SWIZZLE_R:
    case GL_TEXTURE_SWIZZLE_G:
    case GL_TEXTURE_SWIZZLE_B:
    case GL_TEXTURE_SWIZZLE_A:
    case GL_TEXTURE_MIN_LOD:
    case GL_TEXTURE_MAX_LOD:
    case GL_TEXTURE_BASE_LEVEL:
    case GL_TEXTURE_MAX_LEVEL:
    case GL_TEXTURE_COMPARE_MODE:
    case GL_TEXTURE_COMPARE_FUNC:
    case GL_TEXTURE_WRAP_R:
    case GL_TEXTURE_IMMUTABLE_FORMAT:
    case GL_TEXTURE_IMMUTABLE_LEVELS:
        return glesMajorVersion >= 3;
    case GL_DEPTH_STENCIL_TEXTURE_MODE:
        return glesMajorVersion >= 3 && glesMinorVersion >= 1;
    default:
        return false;
    }
}

bool samplerParams(GL2Encoder* ctx, GLenum param) {
    (void)ctx;
    switch(param) {
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_MAG_FILTER:
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
        case GL_TEXTURE_MIN_LOD:
        case GL_TEXTURE_MAX_LOD:
        case GL_TEXTURE_COMPARE_MODE:
        case GL_TEXTURE_COMPARE_FUNC:
            return true;
        default:
            return false;
    }
}

bool textureParamValue(GL2Encoder* ctx, GLenum pname, GLint intval, GLfloat floatval, GLenum enumval) {
    (void)ctx;
    (void)floatval;
    switch (pname) {
    case GL_TEXTURE_BASE_LEVEL:
        return intval >= 0;
    case GL_TEXTURE_COMPARE_MODE:
        return
            (enumval == GL_NONE) ||
            (enumval == GL_COMPARE_REF_TO_TEXTURE);
    case GL_TEXTURE_COMPARE_FUNC:
        return
            (enumval == GL_LEQUAL) ||
            (enumval == GL_GEQUAL) ||
            (enumval == GL_LESS) ||
            (enumval == GL_GREATER) ||
            (enumval == GL_EQUAL) ||
            (enumval == GL_NOTEQUAL) ||
            (enumval == GL_ALWAYS) ||
            (enumval == GL_NEVER);
    case GL_TEXTURE_MAG_FILTER:
        return
            (enumval == GL_NEAREST) ||
            (enumval == GL_LINEAR);
    case GL_TEXTURE_MAX_LEVEL:
        return intval >= 0;
    case GL_TEXTURE_MAX_LOD:
        return true;
    case GL_TEXTURE_MIN_FILTER:
        return
            (enumval == GL_NEAREST) ||
            (enumval == GL_LINEAR) ||
            (enumval == GL_NEAREST_MIPMAP_NEAREST) ||
            (enumval == GL_NEAREST_MIPMAP_LINEAR) ||
            (enumval == GL_LINEAR_MIPMAP_NEAREST) ||
            (enumval == GL_LINEAR_MIPMAP_LINEAR);
    case GL_TEXTURE_MIN_LOD:
        return true;
    case GL_TEXTURE_SWIZZLE_R:
    case GL_TEXTURE_SWIZZLE_G:
    case GL_TEXTURE_SWIZZLE_B:
    case GL_TEXTURE_SWIZZLE_A:
        return
            (enumval == GL_RED) ||
            (enumval == GL_GREEN) ||
            (enumval == GL_BLUE) ||
            (enumval == GL_ALPHA) ||
            (enumval == GL_ZERO) ||
            (enumval == GL_ONE);
    case GL_TEXTURE_WRAP_S:
    case GL_TEXTURE_WRAP_T:
    case GL_TEXTURE_WRAP_R:
        return
            (enumval == GL_CLAMP_TO_EDGE) ||
            (enumval == GL_REPEAT) ||
            (enumval == GL_MIRRORED_REPEAT);
    case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        return true;
    case GL_TEXTURE_IMMUTABLE_FORMAT:
    case GL_TEXTURE_IMMUTABLE_LEVELS:
    case GL_DEPTH_STENCIL_TEXTURE_MODE:
        return true;
    default:
        return true;
    }
}

bool isIntegerFormat(GLenum format) {

#define CHECK_EQUAL(x) case x: return true;

    switch (format) {
        LIST_INTEGER_TEX_FORMATS(CHECK_EQUAL)

    default:
        return false;
    }
}

bool isCompressedFormat(GLenum internalformat) {
#define COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(internal) \
    case internal: \
        return true; \

    switch (internalformat) {
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_R11_EAC)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SIGNED_R11_EAC)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RG11_EAC)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SIGNED_RG11_EAC)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGB8_ETC2)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ETC2)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA8_ETC2_EAC)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_4x4_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_5x4_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_5x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_6x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_6x6_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_8x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_8x6_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_8x8_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_10x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_10x6_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_10x8_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_10x10_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_12x10_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_RGBA_ASTC_12x12_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR)
    COMPRESSED_TEX_IMAGE_IS_COMPRESSED_FORMAT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR)
    default:
        break;
    }
    return false;
}

bool supportedCompressedFormat(GL2Encoder* ctx, GLenum internalformat) {
    int glesMajorVersion = ctx->majorVersion();
    int glesMinorVersion = ctx->minorVersion();

#define COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(internal) \
    case internal: \
        return glesMajorVersion > 1 && ctx->hasExtension("GL_KHR_texture_compression_astc_ldr"); \

#define COMPRESSED_TEX_IMAGE_SUPPORT_CASE_BPTC(internal) \
    case internal: \
        return glesMajorVersion > 1 && ctx->hasExtension("GL_EXT_texture_compression_bptc"); \

#define COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC(internal) \
    case internal: \
      return glesMajorVersion > 1 && ctx->hasExtension("GL_EXT_texture_compression_s3tc"); \

#define COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC_SRGB(internal)       \
    case internal: \
      return glesMajorVersion > 1 && ctx->hasExtension("GL_EXT_texture_compression_s3tc") && ctx->hasExtension("GL_EXT_texture_sRGB"); \

#define COMPRESSED_TEX_IMAGE_SUPPORT_CASE_RGTC(internal) \
    case internal: \
      return glesMajorVersion > 1 && ctx->hasExtension("GL_EXT_texture_compression_rgtc"); \

#define COMPRESSED_TEX_IMAGE_SUPPORT_CASE(internal, maj, min) \
    case internal: \
        if (maj < 3) return true; \
        if (glesMajorVersion < maj) return false; \
        if (glesMinorVersion < min) return false; \
        break; \

#define COMPRESSED_TEX_IMAGE_NOTSUPPORTED(internal) \
    case internal: \
        return false ; \

    switch (internalformat) {
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_ETC1_RGB8_OES, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_R11_EAC, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_SIGNED_R11_EAC, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_RG11_EAC, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_SIGNED_RG11_EAC, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_RGB8_ETC2, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_SRGB8_ETC2, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_RGBA8_ETC2_EAC, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE(GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC, 2, 0)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_4x4_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_5x4_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_5x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_6x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_6x6_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_8x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_8x6_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_8x8_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_10x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_10x6_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_10x8_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_10x10_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_12x10_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_RGBA_ASTC_12x12_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_ASTC(GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_BPTC(GL_COMPRESSED_RGBA_BPTC_UNORM_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_BPTC(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_BPTC(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_BPTC(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC(GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC(GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC_SRGB(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC_SRGB(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_S3TC_SRGB(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_RGTC(GL_COMPRESSED_RED_RGTC1_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_RGTC(GL_COMPRESSED_SIGNED_RED_RGTC1_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_RGTC(GL_COMPRESSED_RED_GREEN_RGTC2_EXT)
    COMPRESSED_TEX_IMAGE_SUPPORT_CASE_RGTC(GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT)
    default:
        break;
    }
    return false;
}

bool unsizedFormat(GLenum format) {
    switch (format) {
    case GL_RED:
    case GL_RED_INTEGER:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_STENCIL:
    case GL_RG:
    case GL_RG_INTEGER:
    case GL_RGB:
    case GL_RGB_INTEGER:
    case GL_RGBA:
    case GL_RGBA_INTEGER:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
        return true;
    }
    return false;
}

// TODO: fix this
bool filterableTexFormat(GL2Encoder* ctx, GLenum internalformat) {
    switch (internalformat) {
    case GL_R32F:
    case GL_RG32F:
    case GL_RGB32F:
    case GL_RGBA32F:
        return ctx->hasExtension("GL_OES_texture_float");
    case GL_R8UI:
    case GL_R8I:
    case GL_R16UI:
    case GL_R16I:
    case GL_R32UI:
    case GL_R32I:
    case GL_RG8UI:
    case GL_RG8I:
    case GL_RG16UI:
    case GL_RG16I:
    case GL_RG32UI:
    case GL_RG32I:
    case GL_RGBA8UI:
    case GL_RGBA8I:
    case GL_RGB10_A2UI:
    case GL_RGBA16UI:
    case GL_RGBA16I:
    case GL_RGBA32I:
    case GL_RGBA32UI:
        return false;
    }
    return true;
}


bool colorRenderableFormat(GL2Encoder* ctx, GLenum internalformat) {
    int glesMajorVersion = ctx->majorVersion();
    switch (internalformat) {
    case GL_R8:
    case GL_RG8:
    case GL_RGB8:
    case GL_RGB565:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGBA8:
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
    case GL_SRGB8_ALPHA8:
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
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
        return true;
    case GL_R16F:
    case GL_RG16F:
    case GL_RGBA16F:
    case GL_R32F:
    case GL_RG32F:
    case GL_RGBA32F:
    case GL_R11F_G11F_B10F:
        return glesMajorVersion >= 3 && ctx->hasExtension("GL_EXT_color_buffer_float");
        break;
    case GL_RGB16F:
        return glesMajorVersion >= 3 && ctx->hasExtension("GL_EXT_color_buffer_half_float");
        break;
    }
    return false;
}

bool depthRenderableFormat(GL2Encoder* ctx, GLenum internalformat) {
    switch (internalformat) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_STENCIL:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
        return true;
    case GL_DEPTH_COMPONENT32_OES:
        return ctx->hasExtension("GL_OES_depth32");
    }
    return false;
}

bool stencilRenderableFormat(GL2Encoder*, GLenum internalformat) {
    switch (internalformat) {
    case GL_DEPTH_STENCIL:
    case GL_STENCIL_INDEX8:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
        return true;
    }
    return false;
}

bool isCubeMapTarget(GLenum target) {
    switch (target) {
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return true;
    default:
        break;
    }
    return false;
}

#define LIST_VALID_TEXFORMATS(f) \
    f(GL_DEPTH_COMPONENT) \
    f(GL_DEPTH_STENCIL) \
    f(GL_RED) \
    f(GL_RED_INTEGER) \
    f(GL_RG) \
    f(GL_RGB) \
    f(GL_RGBA) \
    f(GL_RGBA_INTEGER) \
    f(GL_RGB_INTEGER) \
    f(GL_RG_INTEGER) \
    f(GL_BGRA_EXT) \
    f(GL_ALPHA) \
    f(GL_LUMINANCE) \
    f(GL_LUMINANCE_ALPHA) \

#define LIST_VALID_TEXTYPES(f) \
    f(GL_BYTE) \
    f(GL_FLOAT) \
    f(GL_FLOAT_32_UNSIGNED_INT_24_8_REV) \
    f(GL_HALF_FLOAT) \
    f(GL_HALF_FLOAT_OES) \
    f(GL_INT) \
    f(GL_SHORT) \
    f(GL_UNSIGNED_BYTE) \
    f(GL_UNSIGNED_INT) \
    f(GL_UNSIGNED_INT_10F_11F_11F_REV) \
    f(GL_UNSIGNED_INT_2_10_10_10_REV) \
    f(GL_UNSIGNED_INT_24_8) \
    f(GL_UNSIGNED_INT_5_9_9_9_REV) \
    f(GL_UNSIGNED_SHORT) \
    f(GL_UNSIGNED_SHORT_4_4_4_4) \
    f(GL_UNSIGNED_SHORT_5_5_5_1) \
    f(GL_UNSIGNED_SHORT_5_6_5) \

bool pixelType(GL2Encoder* ctx, GLenum type) {
    int glesMajorVersion = ctx->majorVersion();
    if (glesMajorVersion < 3) {
        switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_SHORT:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_INT:
        case GL_UNSIGNED_INT_10F_11F_11F_REV:
        case GL_UNSIGNED_INT_24_8:
        case GL_HALF_FLOAT:
        case GL_HALF_FLOAT_OES:
        case GL_FLOAT:
            return true;
        }
        return false;
    }

#define GLES3_TYPE_CASE(type) \
    case type: \

    switch (type) {
        LIST_VALID_TEXTYPES(GLES3_TYPE_CASE)
            return glesMajorVersion >= 3;
        default:
            break;
    }

    return false;
}

bool pixelFormat(GL2Encoder* ctx, GLenum format) {
    int glesMajorVersion = ctx->majorVersion();
    if (glesMajorVersion < 3) {
        switch (format) {
            case GL_DEPTH_COMPONENT:
                // GLES3 compatible
                // Required in dEQP
            case GL_RED:
            case GL_RG:
            case GL_DEPTH_STENCIL_OES:
            case GL_ALPHA:
            case GL_RGB:
            case GL_RGBA:
            case GL_BGRA_EXT:
            case GL_LUMINANCE:
            case GL_LUMINANCE_ALPHA:
                return true;
        }
        return false;
    }

#define GLES3_FORMAT_CASE(format) \
    case format:

    switch (format) {
        LIST_VALID_TEXFORMATS(GLES3_FORMAT_CASE)
            return glesMajorVersion >= 3;
        default:
            break;
    }
    return false;
}

bool pixelInternalFormat(GLenum internalformat) {
#define VALID_INTERNAL_FORMAT(format) \
    case format: \
        return true; \

    switch (internalformat) {
    LIST_VALID_TEX_INTERNALFORMATS(VALID_INTERNAL_FORMAT)
    default:
        break;
    }

    GFXSTREAM_WARNING("Error internal format: 0x%x is invalid.", internalformat);
    return false;
}


bool textureBufferFormat(__attribute__((unused)) GL2Encoder* ctx, GLenum internalFormat) {
    switch(internalFormat) {
    LIST_VALID_TEX_BUFFER_FORMATS(VALID_INTERNAL_FORMAT)
    default:
        break;
    }

    GFXSTREAM_WARNING("Error internal format: 0x%x is invalid\n", internalFormat);
    return false;
}

bool pixelSizedFormat(GL2Encoder* ctx, GLenum internalformat, GLenum format, GLenum type) {
    int glesMajorVersion = ctx->majorVersion();
    if (internalformat == format) {
        return true;
    }

    if (glesMajorVersion < 3) {
        switch (format) {
            case GL_RED:
                switch (type) {
                    case GL_UNSIGNED_BYTE:
                        return internalformat == GL_R8;
                    case GL_HALF_FLOAT:
                    case GL_FLOAT:
                        return internalformat == GL_R16F;
                    case GL_BYTE:
                        return internalformat == GL_R8_SNORM;
                    default:
                        return false;
                }
                break;
            case GL_RG:
                switch (type) {
                    case GL_UNSIGNED_BYTE:
                        return internalformat == GL_RG8;
                    case GL_HALF_FLOAT:
                    case GL_FLOAT:
                        return internalformat == GL_RG16F;
                    default:
                        return false;
                }
                break;
            case GL_RGB:
                switch (type) {
                    case GL_HALF_FLOAT:
                    case GL_FLOAT:
                        return internalformat == GL_RGB16F
                            || internalformat == GL_R11F_G11F_B10F;
                    case GL_UNSIGNED_INT_10F_11F_11F_REV:
                        return internalformat == GL_R11F_G11F_B10F;
                    default:
                        return internalformat == GL_RGB8 ||
                               internalformat == GL_RGB;
                }
                break;
            case GL_RGBA:
                switch (type) {
                    case GL_HALF_FLOAT:
                    case GL_FLOAT:
                        return internalformat == GL_RGBA16F;
                    default:
                        return internalformat == GL_RGBA8 ||
                               internalformat == GL_RGBA;
                }
                break;
        }
    }

#define VALIDATE_FORMAT_COMBINATION(x, y, z) \
    if (internalformat == x && format == y && type == z) return true; \

    LIST_VALID_TEXFORMAT_COMBINATIONS(VALIDATE_FORMAT_COMBINATION)

    return false;
}

void getCompatibleFormatTypeForInternalFormat(GLenum internalformat, GLenum* format_out, GLenum* type_out) {
#define RETURN_COMPATIBLE_FORMAT(x, y, z) \
    if (internalformat == x) { \
        *format_out = y; \
        *type_out = z; \
        return; \
    } \

    LIST_VALID_TEXFORMAT_COMBINATIONS(RETURN_COMPATIBLE_FORMAT)
}

bool shaderType(GL2Encoder* ctx, GLenum type) {
    int glesMajorVersion = ctx->majorVersion();
    int glesMinorVersion = ctx->minorVersion();
    switch (type) {
    case GL_VERTEX_SHADER:
    case GL_FRAGMENT_SHADER:
        return true;
    case GL_COMPUTE_SHADER:
        return glesMajorVersion >= 3 && glesMinorVersion >= 1;
    }
    return false;
}

bool internalFormatTarget(GL2Encoder* ctx, GLenum target) {
    int glesMajorVersion = ctx->majorVersion();
    int glesMinorVersion = ctx->minorVersion();
    switch (target) {
    case GL_RENDERBUFFER:
        return true;
    case GL_TEXTURE_2D_MULTISAMPLE:
        return glesMajorVersion >= 3 && glesMinorVersion >= 1;
    }
    return false;
}

std::string vertexAttribIndexRangeErrorMsg(GL2Encoder* ctx, GLuint index) {
    std::stringstream ss;
    GLint maxIndex;
    ctx->glGetIntegerv(ctx, GL_MAX_VERTEX_ATTRIBS, &maxIndex);
    ss << "Invalid vertex attribute index. Wanted index: " << index << ". Max index: " << maxIndex;
    return ss.str();
}

bool allowedFace(GLenum face) {
    switch (face) {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            return true;
        default:
            return false;
    }
}

bool allowedFunc(GLenum func) {
    switch (func) {
        case GL_NEVER:
        case GL_ALWAYS:
        case GL_LESS:
        case GL_LEQUAL:
        case GL_EQUAL:
        case GL_GREATER:
        case GL_GEQUAL:
        case GL_NOTEQUAL:
            return true;
        default:
            return false;
    }
}

bool allowedStencilOp(GLenum op) {
    switch (op) {
        case GL_KEEP:
        case GL_ZERO:
        case GL_REPLACE:
        case GL_INCR:
        case GL_DECR:
        case GL_INVERT:
        case GL_INCR_WRAP:
        case GL_DECR_WRAP:
            return true;
        default:
            return false;
    }
}

bool allowedBlendEquation(GLenum eq) {
    switch (eq) {
        case GL_FUNC_ADD:
        case GL_FUNC_SUBTRACT:
        case GL_FUNC_REVERSE_SUBTRACT:
        case GL_MIN:
        case GL_MAX:
            return true;
        default:
            return false;
    }
}

bool allowedBlendFunc(GLenum func) {
    switch (func) {
        case GL_ZERO:
        case GL_ONE:
        case GL_SRC_COLOR:
        case GL_ONE_MINUS_SRC_COLOR:
        case GL_DST_COLOR:
        case GL_ONE_MINUS_DST_COLOR:
        case GL_SRC_ALPHA:
        case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA:
        case GL_ONE_MINUS_DST_ALPHA:
        case GL_CONSTANT_COLOR:
        case GL_ONE_MINUS_CONSTANT_COLOR:
        case GL_CONSTANT_ALPHA:
        case GL_ONE_MINUS_CONSTANT_ALPHA:
        case GL_SRC_ALPHA_SATURATE:
            return true;
        default:
            return false;
    }
}

bool allowedCullFace(GLenum mode) {
    switch (mode) {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            return true;
        default:
            return false;
    }
}

bool allowedFrontFace(GLenum mode) {
    switch (mode) {
        case GL_CCW:
        case GL_CW:
            return true;
        default:
            return false;
    }
}

bool allowedEnable(int majorVersion, int minorVersion, GLenum cap) {
    switch (cap) {
        case GL_CULL_FACE:
        case GL_POLYGON_OFFSET_FILL:
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
        case GL_SAMPLE_COVERAGE:
        case GL_SCISSOR_TEST:
        case GL_STENCIL_TEST:
        case GL_DEPTH_TEST:
        case GL_BLEND:
        case GL_DITHER:
            return true;
        case GL_PRIMITIVE_RESTART_FIXED_INDEX:
        case GL_RASTERIZER_DISCARD:
            return majorVersion >= 3;
        case GL_SAMPLE_MASK:
            return majorVersion >= 3 && minorVersion >= 1;
        default:
            GFXSTREAM_WARNING("error cap: 0x%x is invalid.", cap);
	    return false;
    }
}

bool allowedGetShader(GLenum pname) {
	switch (pname) {
		case GL_SHADER_TYPE:
		case GL_DELETE_STATUS:
		case GL_COMPILE_STATUS:
		case GL_INFO_LOG_LENGTH:
		case GL_SHADER_SOURCE_LENGTH:
			return true;
		default:
			return false;
	}
}

bool allowedShaderType(GLenum shadertype) {
	switch (shadertype) {
		case GL_VERTEX_SHADER:
		case GL_FRAGMENT_SHADER:
            return true;
		default:
			return false;
	}
}

bool allowedPrecisionType(GLenum precisiontype) {
	switch (precisiontype) {
		case GL_LOW_FLOAT:
		case GL_MEDIUM_FLOAT:
		case GL_HIGH_FLOAT:
		case GL_LOW_INT:
		case GL_MEDIUM_INT:
		case GL_HIGH_INT:
            return true;
		default:
			return false;
	}
}

bool allowedGetProgram(int majorVersion, int minorVersion, GLenum pname) {
    switch (pname) {
        case GL_DELETE_STATUS:
        case GL_LINK_STATUS:
        case GL_VALIDATE_STATUS:
        case GL_INFO_LOG_LENGTH:
        case GL_ATTACHED_SHADERS:
        case GL_ACTIVE_ATTRIBUTES:
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
        case GL_ACTIVE_UNIFORMS:
        case GL_ACTIVE_UNIFORM_MAX_LENGTH:
            return true;
        case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
        case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
        case GL_PROGRAM_BINARY_LENGTH:
        case GL_TRANSFORM_FEEDBACK_VARYINGS:
        case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH:
        case GL_ACTIVE_UNIFORM_BLOCKS:
        case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH:
            return majorVersion > 2;
        case GL_COMPUTE_WORK_GROUP_SIZE:
        case GL_PROGRAM_SEPARABLE:
        case GL_ACTIVE_ATOMIC_COUNTER_BUFFERS:
            return majorVersion > 2 && minorVersion > 0;
        default:
            return false;
    }
}

bool allowedGetActiveUniforms(GLenum pname) {
    switch (pname) {
        case GL_UNIFORM_TYPE:
        case GL_UNIFORM_SIZE:
        case GL_UNIFORM_NAME_LENGTH:
        case GL_UNIFORM_BLOCK_INDEX:
        case GL_UNIFORM_OFFSET:
        case GL_UNIFORM_ARRAY_STRIDE:
        case GL_UNIFORM_MATRIX_STRIDE:
        case GL_UNIFORM_IS_ROW_MAJOR:
            return true;
        default:
            return false;
    }
}

bool allowedGetActiveUniformBlock(GLenum pname) {
    switch (pname) {
        case GL_UNIFORM_BLOCK_BINDING:
        case GL_UNIFORM_BLOCK_DATA_SIZE:
        case GL_UNIFORM_BLOCK_NAME_LENGTH:
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES:
        case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
        case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
            return true;
        default:
            return false;
    }
}

bool allowedGetVertexAttrib(GLenum pname) {
    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
        case GL_VERTEX_ATTRIB_BINDING:
        case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:
        case GL_CURRENT_VERTEX_ATTRIB:
            return true;
        default:
            return false;
    }
}

bool allowedGetRenderbufferParameter(GLenum pname) {
    switch (pname) {
        case GL_RENDERBUFFER_WIDTH:
        case GL_RENDERBUFFER_HEIGHT:
        case GL_RENDERBUFFER_INTERNAL_FORMAT:
        case GL_RENDERBUFFER_RED_SIZE:
        case GL_RENDERBUFFER_GREEN_SIZE:
        case GL_RENDERBUFFER_BLUE_SIZE:
        case GL_RENDERBUFFER_ALPHA_SIZE:
        case GL_RENDERBUFFER_DEPTH_SIZE:
        case GL_RENDERBUFFER_STENCIL_SIZE:
        case GL_RENDERBUFFER_SAMPLES:
            return true;
        default:
            return false;
    }
}

bool allowedQueryTarget(GLenum target) {
    switch (target) {
        case GL_ANY_SAMPLES_PASSED:
        case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
            return true;
        default:
            return false;
    }
}

bool allowedQueryParam(GLenum pname) {
    switch (pname) {
        case GL_CURRENT_QUERY:
            return true;
        default:
            return false;
    }
}

bool allowedQueryObjectParam(GLenum pname) {
    switch (pname) {
        case GL_QUERY_RESULT:
        case GL_QUERY_RESULT_AVAILABLE:
            return true;
        default:
            return false;
    }
}

bool allowedGetSyncParam(GLenum pname) {
    switch (pname) {
        case GL_OBJECT_TYPE:
        case GL_SYNC_STATUS:
        case GL_SYNC_CONDITION:
        case GL_SYNC_FLAGS:
            return true;
        default:
            return false;
    }
}

bool allowedHintTarget(GLenum target) {
    switch (target) {
        case GL_GENERATE_MIPMAP_HINT:
        case GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES:
            return true;
        default:
            return false;
    }
}

bool allowedHintMode(GLenum mode) {
    switch (mode) {
        case GL_DONT_CARE:
        case GL_NICEST:
        case GL_FASTEST:
            return true;
        default:
            return false;
    }
}

} // namespace GLESv2Validation
