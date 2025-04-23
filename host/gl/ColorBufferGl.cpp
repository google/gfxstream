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
#include "ColorBufferGl.h"

#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <string.h>

#include "BorrowedImageGl.h"
#include "DebugGl.h"
#include "GLcommon/GLutils.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "RenderThreadInfoGl.h"
#include "TextureDraw.h"
#include "TextureResize.h"
#include "gl/YUVConverter.h"
#include "gfxstream/host/renderer_operations.h"

#define DEBUG_CB_FBO 0

using android::base::ManagedDescriptor;

namespace gfxstream {
namespace gl {
namespace {

// Lazily create and bind a framebuffer object to the current host context.
// |fbo| is the address of the framebuffer object name.
// |tex| is the name of a texture that is attached to the framebuffer object
// on creation only. I.e. all rendering operations will target it.
// returns true in case of success, false on failure.
bool bindFbo(GLuint* fbo, GLuint tex, bool ensureTextureAttached) {
    if (*fbo) {
        // fbo already exist - just bind
        s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
        if (ensureTextureAttached) {
            s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_OES,
                                           GL_TEXTURE_2D, tex, 0);
        }
        return true;
    }

    s_gles2.glGenFramebuffers(1, fbo);
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_OES,
                                   GL_TEXTURE_2D, tex, 0);

#if DEBUG_CB_FBO
    GLenum status = s_gles2.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
        GFXSTREAM_ERROR("ColorBufferGl::bindFbo: FBO not complete: %#x\n", status);
        s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        s_gles2.glDeleteFramebuffers(1, fbo);
        *fbo = 0;
        return false;
    }
#endif

    return true;
}

void unbindFbo() {
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}

static GLenum sGetUnsizedColorBufferFormat(GLenum format) {
    switch (format) {
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
        case GL_BGR10_A2_ANGLEX:
            return GL_BGRA_EXT;
        default: // already unsized
            return format;
    }
}

static bool sGetFormatParameters(GLint* internalFormat,
                                 GLenum* texFormat,
                                 GLenum* pixelType,
                                 int* bytesPerPixel,
                                 GLint* sizedInternalFormat,
                                 bool* isBlob) {
    if (!internalFormat) {
        fprintf(stderr, "%s: error: internal format not provided\n", __func__);
        return false;
    }

    *isBlob = false;

    switch (*internalFormat) {
        case GL_RGB:
        case GL_RGB8:
            *texFormat = GL_RGB;
            *pixelType = GL_UNSIGNED_BYTE;
            *bytesPerPixel = 3;
            *sizedInternalFormat = GL_RGB8;
            return true;
        case GL_RGB565_OES:
            *texFormat = GL_RGB;
            *pixelType = GL_UNSIGNED_SHORT_5_6_5;
            *bytesPerPixel = 2;
            *sizedInternalFormat = GL_RGB565;
            return true;
        case GL_RGBA:
        case GL_RGBA8:
        case GL_RGB5_A1_OES:
        case GL_RGBA4_OES:
            *texFormat = GL_RGBA;
            *pixelType = GL_UNSIGNED_BYTE;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_RGBA8;
            return true;
        case GL_UNSIGNED_INT_10_10_10_2_OES:
            *texFormat = GL_RGBA;
            *pixelType = GL_UNSIGNED_SHORT;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_UNSIGNED_INT_10_10_10_2_OES;
            return true;
        case GL_RGB10_A2:
            *texFormat = GL_RGBA;
            *pixelType = GL_UNSIGNED_INT_2_10_10_10_REV;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_RGB10_A2;
            return true;
        case GL_RGB16F:
            *texFormat = GL_RGB;
            *pixelType = GL_HALF_FLOAT;
            *bytesPerPixel = 6;
            *sizedInternalFormat = GL_RGB16F;
            return true;
        case GL_RGBA16F:
            *texFormat = GL_RGBA;
            *pixelType = GL_HALF_FLOAT;
            *bytesPerPixel = 8;
            *sizedInternalFormat = GL_RGBA16F;
            return true;
        case GL_LUMINANCE:
            *texFormat = GL_LUMINANCE;
            *pixelType = GL_UNSIGNED_BYTE;
            *bytesPerPixel = 1;
            *sizedInternalFormat = GL_R8;
            *isBlob = true;
            return true;
        case GL_BGRA_EXT:
            *texFormat = GL_BGRA_EXT;
            *pixelType = GL_UNSIGNED_BYTE;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_BGRA8_EXT;
            return true;
        case GL_BGR10_A2_ANGLEX:
            *texFormat = GL_RGBA;
            *pixelType = GL_UNSIGNED_INT_2_10_10_10_REV;
            *bytesPerPixel = 4;
            *internalFormat = GL_RGB10_A2_EXT;
            // GL_BGR10_A2_ANGLEX is actually not a valid GL format. We should
            // replace it with a normal GL internal format instead.
            *sizedInternalFormat = GL_BGR10_A2_ANGLEX;
            return true;
        case GL_R8:
        case GL_RED:
            *texFormat = GL_RED;
            *pixelType = GL_UNSIGNED_BYTE;
            *bytesPerPixel = 1;
            *sizedInternalFormat = GL_R8;
            return true;
        case GL_RG8:
        case GL_RG:
            *texFormat = GL_RG;
            *pixelType = GL_UNSIGNED_BYTE;
            *bytesPerPixel = 2;
            *sizedInternalFormat = GL_RG8;
            return true;
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_COMPONENT16:
            *texFormat = GL_DEPTH_COMPONENT;
            *pixelType = GL_UNSIGNED_SHORT;
            *bytesPerPixel = 2;
            *sizedInternalFormat = GL_DEPTH_COMPONENT16;
            return true;
        case GL_DEPTH_COMPONENT24:
            *texFormat = GL_DEPTH_COMPONENT;
            *pixelType = GL_UNSIGNED_INT;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_DEPTH_COMPONENT24;
            return true;
        case GL_DEPTH_COMPONENT32F:
            *texFormat = GL_DEPTH_COMPONENT;
            *pixelType = GL_FLOAT;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_DEPTH_COMPONENT32F;
            return true;
        case GL_DEPTH_STENCIL:
        case GL_DEPTH24_STENCIL8:
            *texFormat = GL_DEPTH_STENCIL;
            *pixelType = GL_UNSIGNED_INT_24_8;
            *bytesPerPixel = 4;
            *sizedInternalFormat = GL_DEPTH24_STENCIL8;
            return true;
        case GL_DEPTH32F_STENCIL8:
            *texFormat = GL_DEPTH_STENCIL;
            *pixelType = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
            *bytesPerPixel = 8;
            *sizedInternalFormat = GL_DEPTH32F_STENCIL8;
            return true;
        default:
            fprintf(stderr, "%s: Unknown format 0x%x\n", __func__,
                    *internalFormat);
            return false;
    }
}

// static
std::unique_ptr<ColorBufferGl> ColorBufferGl::create(EGLDisplay p_display, int p_width,
                                                     int p_height, GLint p_internalFormat,
                                                     FrameworkFormat p_frameworkFormat,
                                                     HandleType hndl, ContextHelper* helper,
                                                     TextureDraw* textureDraw,
                                                     bool fastBlitSupported,
                                                     const gfxstream::host::FeatureSet& features) {
    GLenum texFormat = 0;
    GLenum pixelType = GL_UNSIGNED_BYTE;
    int bytesPerPixel = 4;
    GLint p_sizedInternalFormat = GL_RGBA8;
    bool isBlob = false;;

    if (!sGetFormatParameters(&p_internalFormat, &texFormat, &pixelType,
                              &bytesPerPixel, &p_sizedInternalFormat,
                              &isBlob)) {
        GFXSTREAM_ERROR("ColorBufferGl::create invalid format 0x%x", p_internalFormat);
        return nullptr;
    }
    const unsigned long bufsize = ((unsigned long)bytesPerPixel) * p_width
            * p_height;

    // This constructor is private, so std::make_unique can't be used.
    std::unique_ptr<ColorBufferGl> cb{
        new ColorBufferGl(p_display, hndl, p_width, p_height, helper, textureDraw)};
    cb->m_internalFormat = p_internalFormat;
    cb->m_sizedInternalFormat = p_sizedInternalFormat;
    cb->m_format = texFormat;
    cb->m_type = pixelType;
    cb->m_frameworkFormat = p_frameworkFormat;
    cb->m_yuv420888ToNv21 = features.Yuv420888ToNv21.enabled;
    cb->m_fastBlitSupported = fastBlitSupported;
    cb->m_numBytes = (size_t)bufsize;

    RecursiveScopedContextBind context(helper);
    if (!context.isOk()) {
        return nullptr;
    }

    GL_SCOPED_DEBUG_GROUP("ColorBufferGl::create(handle:%d)", hndl);

    GLint prevUnpackAlignment;
    s_gles2.glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    s_gles2.glGenTextures(1, &cb->m_tex);
    s_gles2.glBindTexture(GL_TEXTURE_2D, cb->m_tex);

    s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, p_internalFormat, p_width, p_height,
                         0, texFormat, pixelType, nullptr);

    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Swizzle B/R channel for BGR10_A2 images.
    if (p_sizedInternalFormat == GL_BGR10_A2_ANGLEX) {
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        cb->m_BRSwizzle = true;
    }

    //
    // create another texture for that colorbuffer for blit
    //
    s_gles2.glGenTextures(1, &cb->m_blitTex);
    s_gles2.glBindTexture(GL_TEXTURE_2D, cb->m_blitTex);
    s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, p_internalFormat, p_width, p_height,
                         0, texFormat, pixelType, NULL);

    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Swizzle B/R channel for BGR10_A2 images.
    if (p_sizedInternalFormat == GL_BGR10_A2_ANGLEX) {
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        cb->m_BRSwizzle = true;
    }

    cb->m_blitEGLImage = s_egl.eglCreateImageKHR(
            p_display, s_egl.eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer)SafePointerFromUInt(cb->m_blitTex), NULL);

    cb->m_resizer = new TextureResize(p_width, p_height);

    switch (cb->m_frameworkFormat) {
        case FRAMEWORK_FORMAT_GL_COMPATIBLE:
            break;
        default: // Any YUV format
            cb->m_yuv_converter.reset(
                    new YUVConverter(p_width, p_height, cb->m_frameworkFormat, cb->m_yuv420888ToNv21));
            break;
    }

    // desktop GL only: use GL_UNSIGNED_INT_8_8_8_8_REV for faster readback.
    if (get_gfxstream_renderer() == SELECTED_RENDERER_HOST) {
#define GL_UNSIGNED_INT_8_8_8_8           0x8035
#define GL_UNSIGNED_INT_8_8_8_8_REV       0x8367
        cb->m_asyncReadbackType = GL_UNSIGNED_INT_8_8_8_8_REV;
    }

    // Check the ExternalObjectManager for an external memory handle provided for import
    auto extResourceHandleInfo =
        ExternalObjectManager::get()->removeResourceExternalHandleInfo(hndl);
    if (extResourceHandleInfo) {
        switch (extResourceHandleInfo->streamHandleType) {
            case STREAM_HANDLE_TYPE_PLATFORM_EGL_NATIVE_PIXMAP: {
                void* nativePixmap = reinterpret_cast<void*>(extResourceHandleInfo->handle);
                cb->m_eglImage =
                    s_egl.eglCreateImageKHR(p_display, s_egl.eglGetCurrentContext(),
                                            EGL_NATIVE_PIXMAP_KHR, nativePixmap, nullptr);
                if (cb->m_eglImage == EGL_NO_IMAGE_KHR) {
                    GFXSTREAM_ERROR(
                        "ColorBufferGl::create(): EGL_NATIVE_PIXMAP handle provided as external "
                        "resource info, but failed to import pixmap (nativePixmap=0x%x)",
                        nativePixmap);
                    return nullptr;
                }

                // Assume nativePixmap is compatible with ColorBufferGl's current dimensions and
                // internal format.
                EGLBoolean setInfoRes = s_egl.eglSetImageInfoANDROID(
                    p_display, cb->m_eglImage, cb->m_width, cb->m_height, cb->m_internalFormat);
                if (EGL_TRUE != setInfoRes) {
                    GFXSTREAM_ERROR("ColorBufferGl::create(): Failed to set image info");
                    return nullptr;
                }

                s_gles2.glBindTexture(GL_TEXTURE_2D, cb->m_tex);
                s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)cb->m_eglImage);
            } break;
            default:
                GFXSTREAM_ERROR("ColorBufferGl::create -- external memory info was provided, but ",
                                p_internalFormat);
                return nullptr;
        }
    } else {
        cb->m_eglImage =
            s_egl.eglCreateImageKHR(p_display, s_egl.eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
                                    (EGLClientBuffer)SafePointerFromUInt(cb->m_tex), NULL);
    }

    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);

    s_gles2.glFinish();

    return cb;
}

ColorBufferGl::ColorBufferGl(EGLDisplay display, HandleType hndl, GLuint width, GLuint height,
                             ContextHelper* helper, TextureDraw* textureDraw)
    : m_width(width),
      m_height(height),
      m_display(display),
      m_helper(helper),
      m_textureDraw(textureDraw),
      mHndl(hndl) {}

ColorBufferGl::~ColorBufferGl() {
    RecursiveScopedContextBind context(m_helper);

    // b/284523053
    // Swiftshader logspam on exit. But it doesn't happen with SwANGLE.
    if (!context.isOk()) {
        GFXSTREAM_DEBUG("Failed to bind context when releasing color buffers\n");
        return;
    }

    if (m_blitEGLImage) {
        s_egl.eglDestroyImageKHR(m_display, m_blitEGLImage);
    }
    if (m_eglImage) {
        s_egl.eglDestroyImageKHR(m_display, m_eglImage);
    }

    if (m_fbo) {
        s_gles2.glDeleteFramebuffers(1, &m_fbo);
    }

    if (m_yuv_conversion_fbo) {
        s_gles2.glDeleteFramebuffers(1, &m_yuv_conversion_fbo);
    }

    if (m_scaleRotationFbo) {
        s_gles2.glDeleteFramebuffers(1, &m_scaleRotationFbo);
    }

    m_yuv_converter.reset();

    GLuint tex[2] = {m_tex, m_blitTex};
    s_gles2.glDeleteTextures(2, tex);

    if (m_memoryObject) {
        s_gles2.glDeleteMemoryObjectsEXT(1, &m_memoryObject);
    }

    delete m_resizer;
}

static void convertRgbaToRgbPixels(void* dst, const void* src, uint32_t w, uint32_t h) {
    const size_t pixelCount = w * h;
    const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(src);
    uint8_t* dstBytes = reinterpret_cast<uint8_t*>(dst);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint32_t pixel = *(srcPixels++);
        *(dstBytes++) = (pixel & 0xff);
        *(dstBytes++) = ((pixel >> 8) & 0xff);
        *(dstBytes++) = ((pixel >> 16) & 0xff);
    }
}

bool ColorBufferGl::readPixels(int x, int y, int width, int height, GLenum p_format, GLenum p_type,
                               void* pixels) {
    RecursiveScopedContextBind context(m_helper);
    if (!context.isOk()) {
        return false;
    }

    GL_SCOPED_DEBUG_GROUP("ColorBufferGl::readPixels(handle:%d fbo:%d tex:%d)", mHndl, m_fbo,
                          m_tex);

    p_format = sGetUnsizedColorBufferFormat(p_format);

    waitSync();

    if (bindFbo(&m_fbo, m_tex, m_needFboReattach)) {
        m_needFboReattach = false;
        GLint prevAlignment = 0;
        s_gles2.glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlignment);
        s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if ((p_format == GL_RGB || p_format == GL_RGB8) && p_type == GL_UNSIGNED_BYTE) {
            // GL_RGB reads fail with SwiftShader.
            uint8_t* tmpPixels = new uint8_t[width * height * 4];
            s_gles2.glReadPixels(x, y, width, height, GL_RGBA, p_type, tmpPixels);
            convertRgbaToRgbPixels(pixels, tmpPixels, width, height);
        } else {
            s_gles2.glReadPixels(x, y, width, height, p_format, p_type, pixels);
        }
        s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, prevAlignment);
        unbindFbo();
        return true;
    }

    return false;
}

bool ColorBufferGl::readPixelsScaled(int width, int height, GLenum p_format, GLenum p_type,
                                     int rotation, Rect rect, void* pixels) {
    RecursiveScopedContextBind context(m_helper);
    if (!context.isOk()) {
        return false;
    }
    bool useSnipping = rect.size.w != 0 && rect.size.h != 0;
    // Boundary check
    if (useSnipping &&
        (rect.pos.x < 0 || rect.pos.y < 0 || rect.pos.x + rect.size.w > width ||
         rect.pos.y + rect.size.h > height)) {
        GFXSTREAM_ERROR(
            "readPixelsScaled failed. Out-of-bound rectangle: (%d, %d) [%d x %d]"
            " with screen [%d x %d]",
            rect.pos.x, rect.pos.y, rect.size.w, rect.size.h);
        return false;
    }
    p_format = sGetUnsizedColorBufferFormat(p_format);

    waitSync();
    GLuint tex = m_resizer->update(m_tex, width, height, rotation);
    if (bindFbo(&m_scaleRotationFbo, tex, m_needFboReattach)) {
        m_needFboReattach = false;
        GLint prevAlignment = 0;
        s_gles2.glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlignment);
        s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // SwANGLE does not suppot glReadPixels with 3 channels.
        // In fact, the spec only require RGBA8888 format support. Supports for
        // other formats are optional.
        bool needConvert4To3Channel =
                p_format == GL_RGB && p_type == GL_UNSIGNED_BYTE &&
                (get_gfxstream_renderer() == SELECTED_RENDERER_SWIFTSHADER_INDIRECT ||
                    get_gfxstream_renderer() == SELECTED_RENDERER_ANGLE_INDIRECT);
        std::vector<uint8_t> tmpPixels;
        void* readPixelsDst = pixels;
        if (needConvert4To3Channel) {
            tmpPixels.resize(width * height * 4);
            p_format = GL_RGBA;
            readPixelsDst = tmpPixels.data();
        }
        if (useSnipping) {
            s_gles2.glReadPixels(rect.pos.x, rect.pos.y, rect.size.w,
                                 rect.size.h, p_format, p_type, readPixelsDst);
            width = rect.size.w;
            height = rect.size.h;
        } else {
            s_gles2.glReadPixels(0, 0, width, height, p_format, p_type,
                                 readPixelsDst);
        }
        if (needConvert4To3Channel) {
            uint8_t* src = tmpPixels.data();
            uint8_t* dst = static_cast<uint8_t*>(pixels);
            for (int h = 0; h < height; h++) {
                for (int w = 0; w < width; w++) {
                    memcpy(dst, src, 3);
                    dst += 3;
                    src += 4;
                }
            }
        }
        s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, prevAlignment);
        unbindFbo();
        return true;
    }

    return false;
}

bool ColorBufferGl::readPixelsYUVCached(int x, int y, int width, int height, void* pixels,
                                        uint32_t pixels_size) {
    RecursiveScopedContextBind context(m_helper);
    if (!context.isOk()) {
        return false;
    }

    waitSync();

#if DEBUG_CB_FBO
    fprintf(stderr, "%s %d request width %d height %d\n", __func__, __LINE__,
            width, height);
    memset(pixels, 0x00, pixels_size);
    assert(m_yuv_converter.get());
#endif

    m_yuv_converter->readPixels((uint8_t*)pixels, pixels_size);

    return true;
}

void ColorBufferGl::reformat(GLint internalformat, GLenum type) {
    GLenum texFormat = internalformat;
    GLenum pixelType = GL_UNSIGNED_BYTE;
    GLint sizedInternalFormat = GL_RGBA8;
    int bpp = 4;
    bool isBlob = false;
    if (!sGetFormatParameters(&internalformat, &texFormat, &pixelType, &bpp,
                              &sizedInternalFormat, &isBlob)) {
        fprintf(stderr, "%s: WARNING: reformat failed. internal format: 0x%x\n",
                __func__, internalformat);
    }

    // BUG: 143607546
    //
    // During reformatting, sGetFormatParameters can be too
    // opinionated and override the guest's intended choice for the
    // pixel type.  If the guest wanted GL_UNSIGNED_SHORT_5_6_5 as
    // the pixel type, and the incoming internal format is not
    // explicitly sized, sGetFormatParameters will pick a default of
    // GL_UNSIGNED BYTE, which goes against guest expectations.
    //
    // This happens only on older API levels where gralloc.cpp in
    // goldfish-opengl communicated HAL_PIXEL_FORMAT_RGB_565 as GL
    // format GL_RGB, pixel type GL_UNSIGNED_SHORT_5_6_5.  Newer
    // system images communicate HAL_PIXEL_FORMAT_RGB_565 as GL
    // format GL_RGB565, which allows sGetFormatParameters to work
    // correctly.
    if (pixelType != type) {
        pixelType = type;
    }

    s_gles2.glBindTexture(GL_TEXTURE_2D, m_tex);
    s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, internalformat, m_width, m_height,
                         0, texFormat, pixelType, nullptr);

    s_gles2.glBindTexture(GL_TEXTURE_2D, m_blitTex);
    s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, internalformat, m_width, m_height,
                         0, texFormat, pixelType, nullptr);

    // EGL images need to be recreated because the EGL_KHR_image_base spec
    // states that respecifying an image (i.e. glTexImage2D) will generally
    // result in orphaning of the EGL image.
    s_egl.eglDestroyImageKHR(m_display, m_eglImage);
    m_eglImage = s_egl.eglCreateImageKHR(
            m_display, s_egl.eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer)SafePointerFromUInt(m_tex), NULL);

    s_egl.eglDestroyImageKHR(m_display, m_blitEGLImage);
    m_blitEGLImage = s_egl.eglCreateImageKHR(
            m_display, s_egl.eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer)SafePointerFromUInt(m_blitTex), NULL);

    s_gles2.glBindTexture(GL_TEXTURE_2D, 0);

    m_internalFormat = internalformat;
    m_format = texFormat;
    m_type = pixelType;
    m_sizedInternalFormat = sizedInternalFormat;

    m_numBytes = bpp * m_width * m_height;
}

void ColorBufferGl::swapYUVTextures(FrameworkFormat type, uint32_t* textures, void* metadata) {
    if (type == FrameworkFormat::FRAMEWORK_FORMAT_NV12) {
        m_yuv_converter->swapTextures(type, textures, metadata);
    } else {
        fprintf(stderr,
                "%s: ERROR: format other than NV12 is not supported: 0x%x\n",
                __func__, type);
    }
}

bool ColorBufferGl::subUpdate(int x, int y, int width, int height, GLenum p_format, GLenum p_type,
                              const void* pixels, void* metadata) {
    return subUpdateFromFrameworkFormat(x, y, width, height, m_frameworkFormat, p_format, p_type,
                                        pixels, metadata);
}

bool ColorBufferGl::subUpdateFromFrameworkFormat(int x, int y, int width, int height,
                                                 FrameworkFormat fwkFormat, GLenum p_format,
                                                 GLenum p_type, const void* pixels,
                                                 void* metadata) {
    const GLenum p_unsizedFormat = sGetUnsizedColorBufferFormat(p_format);
    RecursiveScopedContextBind context(m_helper);
    if (!context.isOk()) {
        return false;
    }

    GL_SCOPED_DEBUG_GROUP("ColorBufferGl::subUpdate(handle:%d fbo:%d tex:%d)", mHndl, m_fbo, m_tex);

    if (m_needFormatCheck) {
        if (p_type != m_type || p_format != m_format) {
            reformat((GLint)p_format, p_type);
        }
        m_needFormatCheck = false;
    }

    if (m_frameworkFormat != FRAMEWORK_FORMAT_GL_COMPATIBLE || fwkFormat != m_frameworkFormat) {
        assert(m_yuv_converter.get());

        // This FBO will convert the YUV frame to RGB
        // and render it to |m_tex|.
        bindFbo(&m_yuv_conversion_fbo, m_tex, m_needFboReattach);
        m_yuv_converter->drawConvertFromFormat(fwkFormat, x, y, width, height, (char*)pixels,
                                               metadata);
        unbindFbo();

        // |m_tex| still needs to be bound afterwards
        s_gles2.glBindTexture(GL_TEXTURE_2D, m_tex);

    } else {
        s_gles2.glBindTexture(GL_TEXTURE_2D, m_tex);
        s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        s_gles2.glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, p_unsizedFormat,
                                p_type, pixels);
    }

    if (m_fastBlitSupported) {
        s_gles2.glFlush();
        m_sync = (GLsync)s_egl.eglSetImageFenceANDROID(m_display, m_eglImage);
    }

    return true;
}

bool ColorBufferGl::replaceContents(const void* newContents, size_t numBytes) {
    return subUpdate(0, 0, m_width, m_height, m_format, m_type, newContents);
}

bool ColorBufferGl::readContents(size_t* numBytes, void* pixels) {
    if (m_yuv_converter) {
        // common code path for vk & gles
        *numBytes = m_yuv_converter->getDataSize();
        if (!pixels) {
            return true;
        }
        return readPixelsYUVCached(0, 0, 0, 0, pixels, *numBytes);
    } else {
        *numBytes = m_numBytes;
        if (!pixels) {
            return true;
        }
        return readPixels(0, 0, m_width, m_height, m_format, m_type, pixels);
    }
}

bool ColorBufferGl::blitFromCurrentReadBuffer() {
    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();
    if (!tInfo) {
        GFXSTREAM_FATAL("Render thread GL not available.");
    }

    if (!tInfo->currContext.get()) {
        // no Current context
        return false;
    }

    if (m_fastBlitSupported) {
        s_egl.eglBlitFromCurrentReadBufferANDROID(m_display, m_eglImage);
        m_sync = (GLsync)s_egl.eglSetImageFenceANDROID(m_display, m_eglImage);
    } else {
        // Copy the content of the current read surface into m_blitEGLImage.
        // This is done by creating a temporary texture, bind it to the EGLImage
        // then call glCopyTexSubImage2D().
        GLuint tmpTex;
        GLint currTexBind;
        if (tInfo->currContext->clientVersion() > GLESApi_CM) {
            s_gles2.glGetIntegerv(GL_TEXTURE_BINDING_2D, &currTexBind);
            s_gles2.glGenTextures(1, &tmpTex);
            s_gles2.glBindTexture(GL_TEXTURE_2D, tmpTex);
            s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_blitEGLImage);

            const bool isGles3 = tInfo->currContext->clientVersion() > GLESApi_2;

            GLint prev_read_fbo = 0;
            if (isGles3) {
                // Make sure that we unbind any existing GL_READ_FRAMEBUFFER
                // before calling glCopyTexSubImage2D, otherwise we may blit
                // from the guest's current read framebuffer instead of the EGL
                // read buffer.
                s_gles2.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
                if (prev_read_fbo != 0) {
                    s_gles2.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                }
            } else {
                // On GLES 2, there are not separate read/draw framebuffers,
                // only GL_FRAMEBUFFER.  Per the EGL 1.4 spec section 3.9.3,
                // the draw surface must be bound to the calling thread's
                // current context, so GL_FRAMEBUFFER should be 0.  However, the
                // error case is not strongly defined and generating a new error
                // may break existing apps.
                //
                // Instead of the obviously wrong behavior of posting whatever
                // GL_FRAMEBUFFER is currently bound to, fix up the
                // GL_FRAMEBUFFER if it is non-zero.
                s_gles2.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_read_fbo);
                if (prev_read_fbo != 0) {
                    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
                }
            }

            // If the read buffer is multisampled, we need to resolve.
            GLint samples;
            s_gles2.glGetIntegerv(GL_SAMPLE_BUFFERS, &samples);
            if (isGles3 && samples > 0) {
                s_gles2.glBindTexture(GL_TEXTURE_2D, 0);

                GLuint resolve_fbo;
                GLint prev_draw_fbo;
                s_gles2.glGenFramebuffers(1, &resolve_fbo);
                s_gles2.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

                s_gles2.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
                s_gles2.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                        GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                        tmpTex, 0);
                s_gles2.glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width,
                        m_height, GL_COLOR_BUFFER_BIT,
                        GL_NEAREST);
                s_gles2.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                        (GLuint)prev_draw_fbo);

                s_gles2.glDeleteFramebuffers(1, &resolve_fbo);
                s_gles2.glBindTexture(GL_TEXTURE_2D, tmpTex);
            } else {
                // If the buffer is not multisampled, perform a normal texture copy.
                s_gles2.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_width,
                        m_height);
            }

            if (prev_read_fbo != 0) {
                if (isGles3) {
                    s_gles2.glBindFramebuffer(GL_READ_FRAMEBUFFER,
                                              (GLuint)prev_read_fbo);
                } else {
                    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER,
                                              (GLuint)prev_read_fbo);
                }
            }

            s_gles2.glDeleteTextures(1, &tmpTex);
            s_gles2.glBindTexture(GL_TEXTURE_2D, currTexBind);

            // clear GL errors, because its possible that the fbo format does not
            // match
            // the format of the read buffer, in the case of OpenGL ES 3.1 and
            // integer
            // RGBA formats.
            s_gles2.glGetError();
            // This is currently for dEQP purposes only; if we actually want these
            // integer FBO formats to actually serve to display something for human
            // consumption,
            // we need to change the egl image to be of the same format,
            // or we get some really psychedelic patterns.
        } else {
            // Like in the GLES 2 path above, correct the case where
            // GL_FRAMEBUFFER_OES is not bound to zero so that we don't blit
            // from arbitrary framebuffers.
            // Use GLES 2 because it internally has the same value as the GLES 1
            // API and it doesn't require GL_OES_framebuffer_object.
            GLint prev_fbo = 0;
            s_gles2.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
            if (prev_fbo != 0) {
                s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            s_gles1.glGetIntegerv(GL_TEXTURE_BINDING_2D, &currTexBind);
            s_gles1.glGenTextures(1, &tmpTex);
            s_gles1.glBindTexture(GL_TEXTURE_2D, tmpTex);
            s_gles1.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_blitEGLImage);
            s_gles1.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_width,
                    m_height);
            s_gles1.glDeleteTextures(1, &tmpTex);
            s_gles1.glBindTexture(GL_TEXTURE_2D, currTexBind);

            if (prev_fbo != 0) {
                s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
            }
        }

        RecursiveScopedContextBind context(m_helper);
        if (!context.isOk()) {
            return false;
        }

        if (!bindFbo(&m_fbo, m_tex, m_needFboReattach)) {
            return false;
        }

        // Save current viewport and match it to the current colorbuffer size.
        GLint vport[4] = {
            0,
        };
        s_gles2.glGetIntegerv(GL_VIEWPORT, vport);
        s_gles2.glViewport(0, 0, m_width, m_height);

        // render m_blitTex
        m_textureDraw->draw(m_blitTex, 0., 0, 0);

        // Restore previous viewport.
        s_gles2.glViewport(vport[0], vport[1], vport[2], vport[3]);
        unbindFbo();
    }

    return true;
}

bool ColorBufferGl::bindToTexture() {
    if (!m_eglImage) {
        return false;
    }

    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();
    if (!tInfo) {
        GFXSTREAM_FATAL("Render thread GL not available.");
    }

    if (!tInfo->currContext.get()) {
        return false;
    }

    if (tInfo->currContext->clientVersion() > GLESApi_CM) {
        s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
    } else {
        s_gles1.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
    }
    return true;
}

bool ColorBufferGl::bindToTexture2() {
    if (!m_eglImage) {
        return false;
    }

    s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
    return true;
}

bool ColorBufferGl::bindToRenderbuffer() {
    if (!m_eglImage) {
        return false;
    }

    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();
    if (!tInfo) {
        GFXSTREAM_FATAL("Render thread GL not available.");
    }

    if (!tInfo->currContext.get()) {
        return false;
    }

    if (tInfo->currContext->clientVersion() > GLESApi_CM) {
        s_gles2.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER_OES,
                                                       m_eglImage);
    } else {
        s_gles1.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER_OES,
                                                       m_eglImage);
    }
    return true;
}

GLuint ColorBufferGl::getViewportScaledTexture() { return m_resizer->update(m_tex); }

void ColorBufferGl::setSync(bool debug) {
    m_sync = (GLsync)s_egl.eglSetImageFenceANDROID(m_display, m_eglImage);
    if (debug) fprintf(stderr, "%s: %u to %p\n", __func__, getHndl(), m_sync);
}

void ColorBufferGl::waitSync(bool debug) {
    if (debug) fprintf(stderr, "%s: %u sync %p\n", __func__, getHndl(), m_sync);
    if (m_sync) {
        s_egl.eglWaitImageFenceANDROID(m_display, m_sync);
    }
}

bool ColorBufferGl::post(GLuint tex, float rotation, float dx, float dy) {
    // NOTE: Do not call m_helper->setupContext() here!
    waitSync();
    return m_textureDraw->draw(tex, rotation, dx, dy);
}

bool ColorBufferGl::postViewportScaledWithOverlay(float rotation, float dx, float dy) {
    // NOTE: Do not call m_helper->setupContext() here!
    waitSync();
    return m_textureDraw->drawWithOverlay(getViewportScaledTexture(), rotation, dx, dy);
}

void ColorBufferGl::readback(unsigned char* img, bool readbackBgra) {
    RecursiveScopedContextBind context(m_helper);
    if (!context.isOk()) {
        return;
    }

    waitSync();

    if (bindFbo(&m_fbo, m_tex, m_needFboReattach)) {
        m_needFboReattach = false;
        // Flip the readback format if RED/BLUE components are swizzled.
        bool shouldReadbackBgra = m_BRSwizzle ? !readbackBgra : readbackBgra;
        GLenum format = shouldReadbackBgra ? GL_BGRA_EXT : GL_RGBA;

        s_gles2.glReadPixels(0, 0, m_width, m_height, format, GL_UNSIGNED_BYTE, img);
        unbindFbo();
    }
}

void ColorBufferGl::readbackAsync(GLuint buffer, bool readbackBgra) {
    RecursiveScopedContextBind context(m_helper);
    if (!context.isOk()) {
        return;
    }

    waitSync();

    if (bindFbo(&m_fbo, m_tex, m_needFboReattach)) {
        m_needFboReattach = false;
        s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        bool shouldReadbackBgra = m_BRSwizzle ? !readbackBgra : readbackBgra;
        GLenum format = shouldReadbackBgra ? GL_BGRA_EXT : GL_RGBA;
        s_gles2.glReadPixels(0, 0, m_width, m_height, format, m_asyncReadbackType, 0);
        s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        unbindFbo();
    }
}

HandleType ColorBufferGl::getHndl() const { return mHndl; }

void ColorBufferGl::onSave(android::base::Stream* stream) {
    stream->putBe32(getHndl());
    stream->putBe32(static_cast<uint32_t>(m_width));
    stream->putBe32(static_cast<uint32_t>(m_height));
    stream->putBe32(static_cast<uint32_t>(m_internalFormat));
    stream->putBe32(static_cast<uint32_t>(m_frameworkFormat));
    // for debug
    assert(m_eglImage && m_blitEGLImage);
    stream->putBe32(reinterpret_cast<uintptr_t>(m_eglImage));
    stream->putBe32(reinterpret_cast<uintptr_t>(m_blitEGLImage));
    stream->putBe32(m_needFormatCheck);
}

std::unique_ptr<ColorBufferGl> ColorBufferGl::onLoad(android::base::Stream* stream,
                                                     EGLDisplay p_display, ContextHelper* helper,
                                                     TextureDraw* textureDraw,
                                                     bool fastBlitSupported,
                                                     const gfxstream::host::FeatureSet& features) {
    HandleType hndl = static_cast<HandleType>(stream->getBe32());
    GLuint width = static_cast<GLuint>(stream->getBe32());
    GLuint height = static_cast<GLuint>(stream->getBe32());
    GLenum internalFormat = static_cast<GLenum>(stream->getBe32());
    FrameworkFormat frameworkFormat =
            static_cast<FrameworkFormat>(stream->getBe32());
    EGLImageKHR eglImage = reinterpret_cast<EGLImageKHR>(stream->getBe32());
    EGLImageKHR blitEGLImage = reinterpret_cast<EGLImageKHR>(stream->getBe32());
    uint32_t needFormatCheck = stream->getBe32();

    if (!eglImage) {
        return create(p_display, width, height, internalFormat, frameworkFormat,
                      hndl, helper, textureDraw, fastBlitSupported, features);
    }
    std::unique_ptr<ColorBufferGl> cb(
        new ColorBufferGl(p_display, hndl, width, height, helper, textureDraw));
    cb->m_eglImage = eglImage;
    cb->m_blitEGLImage = blitEGLImage;
    assert(eglImage && blitEGLImage);
    cb->m_internalFormat = internalFormat;
    cb->m_frameworkFormat = frameworkFormat;
    cb->m_fastBlitSupported = fastBlitSupported;
    cb->m_needFormatCheck = needFormatCheck;

    GLenum texFormat;
    GLenum pixelType;
    int bytesPerPixel = 1;
    GLint sizedInternalFormat;
    bool isBlob;
    sGetFormatParameters(&cb->m_internalFormat, &texFormat, &pixelType, &bytesPerPixel,
                         &sizedInternalFormat, &isBlob);
    cb->m_type = pixelType;
    cb->m_format = texFormat;
    cb->m_sizedInternalFormat = sizedInternalFormat;
    // TODO: set m_BRSwizzle properly
    cb->m_numBytes = ((unsigned long)bytesPerPixel) * width * height;
    return cb;
}

void ColorBufferGl::restore() {
    RecursiveScopedContextBind context(m_helper);
    s_gles2.glGenTextures(1, &m_tex);
    s_gles2.glBindTexture(GL_TEXTURE_2D, m_tex);
    s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);

    s_gles2.glGenTextures(1, &m_blitTex);
    s_gles2.glBindTexture(GL_TEXTURE_2D, m_blitTex);
    s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_blitEGLImage);

    m_resizer = new TextureResize(m_width, m_height);
    switch (m_frameworkFormat) {
        case FRAMEWORK_FORMAT_GL_COMPATIBLE:
            break;
        default: // any YUV format
            m_yuv_converter.reset(
                    new YUVConverter(m_width, m_height, m_frameworkFormat, m_yuv420888ToNv21));
            break;
    }
}

GLuint ColorBufferGl::getTexture() { return m_tex; }

void ColorBufferGl::postLayer(const ComposeLayer& l, int frameWidth, int frameHeight) {
    waitSync();
    m_textureDraw->drawLayer(l, frameWidth, frameHeight, m_width, m_height,
                             getViewportScaledTexture());
}

bool ColorBufferGl::importMemory(ManagedDescriptor externalDescriptor, uint64_t size,
                                 bool dedicated, bool linearTiling) {
    RecursiveScopedContextBind context(m_helper);
    s_gles2.glCreateMemoryObjectsEXT(1, &m_memoryObject);
    if (dedicated) {
        static const GLint DEDICATED_FLAG = GL_TRUE;
        s_gles2.glMemoryObjectParameterivEXT(m_memoryObject,
                                             GL_DEDICATED_MEMORY_OBJECT_EXT,
                                             &DEDICATED_FLAG);
    }
    std::optional<ManagedDescriptor::DescriptorType> maybeRawDescriptor = externalDescriptor.get();
    if (!maybeRawDescriptor.has_value()) {
        GFXSTREAM_FATAL("Uninitialized external descriptor.");
    }
    ManagedDescriptor::DescriptorType rawDescriptor = *maybeRawDescriptor;

#ifdef _WIN32
    s_gles2.glImportMemoryWin32HandleEXT(m_memoryObject, size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                         rawDescriptor);
#else
    s_gles2.glImportMemoryFdEXT(m_memoryObject, size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, rawDescriptor);
#endif
    GLenum error = s_gles2.glGetError();
    if (error == GL_NO_ERROR) {
#ifdef _WIN32
        // Let the external descriptor close when going out of scope. From the
        // EXT_external_objects_win32 spec: importing a Windows handle does not transfer ownership
        // of the handle to the GL implementation.  For handle types defined as NT handles, the
        // application must release the handle using an appropriate system call when it is no longer
        // needed.
#else
        // Inform ManagedDescriptor not to close the fd, since the owner of the fd is transferred to
        // the GL driver. From the EXT_external_objects_fd spec: a successful import operation
        // transfers ownership of <fd> to the GL implementation, and performing any operation on
        // <fd> in the application after an import results in undefined behavior.
        externalDescriptor.release();
#endif
    } else {
        GFXSTREAM_ERROR("Failed to import external memory object with error: %d",
                        static_cast<int>(error));
        return false;
    }

    GLuint glTiling = linearTiling ? GL_LINEAR_TILING_EXT : GL_OPTIMAL_TILING_EXT;

    std::vector<uint8_t> prevContents;

    size_t bytes;
    readContents(&bytes, nullptr);
    prevContents.resize(bytes, 0);
    readContents(&bytes, prevContents.data());

    s_gles2.glDeleteTextures(1, &m_tex);
    s_gles2.glDeleteFramebuffers(1, &m_fbo);
    m_fbo = 0;
    s_gles2.glDeleteFramebuffers(1, &m_scaleRotationFbo);
    m_scaleRotationFbo = 0;
    s_gles2.glDeleteFramebuffers(1, &m_yuv_conversion_fbo);
    m_yuv_conversion_fbo = 0;
    s_egl.eglDestroyImageKHR(m_display, m_eglImage);

    s_gles2.glGenTextures(1, &m_tex);
    s_gles2.glBindTexture(GL_TEXTURE_2D, m_tex);

    // HOST needed because we do not expose this to guest
    s_gles2.glTexParameteriHOST(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, glTiling);

    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (m_sizedInternalFormat == GL_BGRA8_EXT ||
        m_sizedInternalFormat == GL_BGR10_A2_ANGLEX) {
        GLint internalFormat = m_sizedInternalFormat == GL_BGRA8_EXT
                                       ? GL_RGBA8
                                       : GL_RGB10_A2_EXT;
        s_gles2.glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, internalFormat, m_width,
                                     m_height, m_memoryObject, 0);
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        m_BRSwizzle = true;
    } else {
        s_gles2.glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, m_sizedInternalFormat, m_width, m_height, m_memoryObject, 0);
        m_BRSwizzle = false;
    }

    m_eglImage = s_egl.eglCreateImageKHR(
            m_display, s_egl.eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer)SafePointerFromUInt(m_tex), NULL);

    replaceContents(prevContents.data(), m_numBytes);

    return true;
}

bool ColorBufferGl::importEglNativePixmap(void* pixmap, bool preserveContent) {
    EGLImageKHR image = s_egl.eglCreateImageKHR(m_display, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, pixmap, nullptr);

    if (image == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "%s: error: failed to import pixmap\n", __func__);
        return false;
    }

    // Assume pixmap is compatible with ColorBufferGl's current dimensions and internal format.
    EGLBoolean setInfoRes = s_egl.eglSetImageInfoANDROID(m_display, image, m_width, m_height, m_internalFormat);

    if (EGL_TRUE != setInfoRes) {
        fprintf(stderr, "%s: error: failed to set image info\n", __func__);
        s_egl.eglDestroyImageKHR(m_display, image);
        return false;
    }

    RecursiveScopedContextBind context(m_helper);

    std::vector<uint8_t> contents;
    if (preserveContent) {
        size_t bytes;
        readContents(&bytes, nullptr);
        contents.resize(bytes);
        readContents(&bytes, contents.data());
    }

    s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)NULL);
    s_egl.eglDestroyImageKHR(m_display, m_eglImage);

    m_eglImage = image;
    s_gles2.glBindTexture(GL_TEXTURE_2D, m_tex);
    s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)m_eglImage);

    if (preserveContent) {
        replaceContents(contents.data(), m_numBytes);
    }

    return true;
}

std::unique_ptr<BorrowedImageInfo> ColorBufferGl::getBorrowedImageInfo() {
    auto info = std::make_unique<BorrowedImageInfoGl>();
    info->id = mHndl;
    info->width = m_width;
    info->height = m_height;
    info->texture = m_tex;
    info->onCommandsIssued = [this]() { setSync(); };
    return info;
}

}  // namespace gl
}  // namespace gfxstream
