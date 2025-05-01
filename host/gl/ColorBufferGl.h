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

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES3/gl3.h>

#include "ContextHelper.h"
#include "FrameworkFormats.h"
#include "Handle.h"
#include "Hwc2.h"
#include "render-utils/stream.h"
#include "gfxstream/ManagedDescriptor.hpp"
#include "gfxstream/host/Features.h"
#include "gfxstream/host/borrowed_image.h"
#include "render-utils/Renderer.h"

// From ANGLE "src/common/angleutils.h"
#define GL_BGR10_A2_ANGLEX 0x6AF9

// A class used to model a guest color buffer, and used to implement several
// related things:
//
//  - Every gralloc native buffer with HW read or write requirements will
//    allocate a host ColorBufferGl instance. When gralloc_lock() is called,
//    the guest will use ColorBufferGl::readPixels() to read the current content
//    of the buffer. When gralloc_unlock() is later called, it will call
//    ColorBufferGl::subUpdate() to send the updated pixels.
//
//  - Every guest window EGLSurface is implemented by a host PBuffer
//    (see WindowSurface.h) that can have a ColorBufferGl instance attached to
//    it (through WindowSurface::attachColorBuffer()). When such an attachment
//    exists, WindowSurface::flushColorBuffer() will copy the PBuffer's
//    pixel data into the ColorBufferGl. The latter can then be displayed
//    in the client's UI sub-window with ColorBufferGl::post().
//
//  - Guest EGLImages are implemented as native gralloc buffers too.
//    The guest glEGLImageTargetTexture2DOES() implementations will end up
//    calling ColorBufferGl::bindToTexture() to bind the current context's
//    GL_TEXTURE_2D to the buffer. Similarly, the guest versions of
//    glEGLImageTargetRenderbufferStorageOES() will end up calling
//    ColorBufferGl::bindToRenderbuffer().
//
// This forces the implementation to use a host EGLImage to implement each
// ColorBufferGl.
//
// As an additional twist.

namespace gfxstream {
namespace gl {

class TextureDraw;
class TextureResize;
class YUVConverter;

class ColorBufferGl {
   public:
    // Create a new ColorBufferGl instance.
    // |display| is the host EGLDisplay handle.
    // |width| and |height| are the buffer's dimensions in pixels.
    // |internalFormat| is the internal OpenGL pixel format to use, valid
    // values
    // are: GL_RGB, GL_RGB565, GL_RGBA, GL_RGB5_A1_OES and GL_RGBA4_OES.
    // Implementation is free to use something else though.
    // |frameworkFormat| specifies the original format of the guest
    // color buffer so that we know how to convert to |internalFormat|,
    // if necessary (otherwise, frameworkFormat ==
    // FRAMEWORK_FORMAT_GL_COMPATIBLE).
    // It is assumed underlying EGL has EGL_KHR_gl_texture_2D_image.
    // Returns NULL on failure.
    // |fastBlitSupported|: whether or not this ColorBufferGl can be
    // blitted and posted to swapchain without context switches.
    static std::unique_ptr<ColorBufferGl> create(EGLDisplay display, int width, int height,
                                                 GLint internalFormat,
                                                 FrameworkFormat frameworkFormat, HandleType handle,
                                                 ContextHelper* helper, TextureDraw* textureDraw,
                                                 bool fastBlitSupported,
                                                 const gfxstream::host::FeatureSet& features);

    // Sometimes things happen and we need to reformat the GL texture
    // used. This function replaces the format of the underlying texture
    // with the internalformat specified.
    void reformat(GLint internalformat, GLenum type);

    // Destructor.
    ~ColorBufferGl();

    // Return ColorBufferGl width and height in pixels
    GLuint getWidth() const { return m_width; }
    GLuint getHeight() const { return m_height; }

    // Read the ColorBufferGl instance's pixel values into host memory.
    bool readPixels(int x,
                    int y,
                    int width,
                    int height,
                    GLenum p_format,
                    GLenum p_type,
                    void* pixels);
    // Read the ColorBuffer instance's pixel values by first scaling
    // to the size of width x height, then clipping a |rect| from the
    // screen defined by width x height.
    bool readPixelsScaled(int width, int height, GLenum p_format, GLenum p_type, int skinRotation,
                          Rect rect, void* pixels);

    // Read cached YUV pixel values into host memory.
    bool readPixelsYUVCached(int x,
                             int y,
                             int width,
                             int height,
                             void* pixels,
                             uint32_t pixels_size);

    void swapYUVTextures(FrameworkFormat texture_type, GLuint* textures, void* metadata = nullptr);

    // Update the ColorBufferGl instance's pixel values from host memory.
    // |p_format / p_type| are the desired OpenGL color buffer format
    // and data type.
    // Otherwise, subUpdate() will explicitly convert |pixels|
    // to be in |p_format|.
    bool subUpdate(int x, int y, int width, int height, GLenum p_format, GLenum p_type,
                   const void* pixels, void* metadata = nullptr);
    bool subUpdateFromFrameworkFormat(int x, int y, int width, int height,
                                      FrameworkFormat fwkFormat, GLenum p_format, GLenum p_type,
                                      const void* pixels, void* metadata = nullptr);

    // Completely replaces contents, assuming that |pixels| is a buffer
    // that is allocated and filled with the same format.
    bool replaceContents(const void* pixels, size_t numBytes);

    // Reads back entire contents, tightly packed rows.
    // If the framework format is YUV, it will read back as raw YUV data.
    bool readContents(size_t* numBytes, void* pixels);

    // Draw a ColorBufferGl instance, i.e. blit it to the current guest
    // framebuffer object / window surface. This doesn't display anything.
    bool draw();

    // Returns the texture name of a texture containing the contents of this
    // ColorBuffer but that is scaled to match the current viewport. This
    // ColorBuffer retains ownership of the returned texture.
    GLuint getViewportScaledTexture();
    // Post this ColorBuffer to the host native sub-window.
    // |rotation| is the rotation angle in degrees, clockwise in the GL
    // coordinate space.
    bool post(GLuint tex, float rotation, float dx, float dy);
    // Post this ColorBufferGl to the host native sub-window and apply
    // the device screen overlay (if there is one).
    // |rotation| is the rotation angle in degrees, clockwise in the GL
    // coordinate space.
    bool postViewportScaledWithOverlay(float rotation, float dx, float dy);

    // Bind the current context's EGL_TEXTURE_2D texture to this ColorBufferGl's
    // EGLImage. This is intended to implement glEGLImageTargetTexture2DOES()
    // for all GLES versions.
    bool bindToTexture();
    bool bindToTexture2();

    // Bind the current context's EGL_RENDERBUFFER_OES render buffer to this
    // ColorBufferGl's EGLImage. This is intended to implement
    // glEGLImageTargetRenderbufferStorageOES() for all GLES versions.
    bool bindToRenderbuffer();

    // Copy the content of the current context's read surface to this
    // ColorBufferGl. This is used from WindowSurface::flushColorBuffer().
    // Return true on success, false on failure (e.g. no current context).
    bool blitFromCurrentReadBuffer();

    // Read the content of the whole ColorBufferGl as 32-bit RGBA pixels.
    // |img| must be a buffer large enough (i.e. width * height * 4).
    void readback(unsigned char* img, bool readbackBgra = false);
    // readback() but async (to the specified |buffer|)
    void readbackAsync(GLuint buffer, bool readbackBgra = false);

    void onSave(gfxstream::Stream* stream);
    static std::unique_ptr<ColorBufferGl> onLoad(gfxstream::Stream* stream,
                                                 EGLDisplay p_display, ContextHelper* helper,
                                                 TextureDraw* textureDraw, bool fastBlitSupported,
                                                 const gfxstream::host::FeatureSet& features);

    HandleType getHndl() const;

    bool isFastBlitSupported() const { return m_fastBlitSupported; }
    void postLayer(const ComposeLayer& l, int frameWidth, int frameHeight);
    GLuint getTexture();

    std::unique_ptr<BorrowedImageInfo> getBorrowedImageInfo();

    // ColorBufferGl backing change methods
    //
    // Change to opaque fd or opaque win32 handle-backed VkDeviceMemory
    // via GL_EXT_memory_objects
    bool importMemory(gfxstream::base::ManagedDescriptor externalDescriptor, uint64_t size,
                      bool dedicated, bool linearTiling);
    // Change to EGL native pixmap
    bool importEglNativePixmap(void* pixmap, bool preserveContent);

    void setSync(bool debug = false);
    void waitSync(bool debug = false);
    void setDisplay(uint32_t displayId) { m_displayId = displayId; }
    uint32_t getDisplay() { return m_displayId; }
    FrameworkFormat getFrameworkFormat() { return m_frameworkFormat; }

 public:
    void restore();

private:
 ColorBufferGl(EGLDisplay display, HandleType hndl, GLuint width, GLuint height,
               ContextHelper* helper, TextureDraw* textureDraw);

private:
    GLuint m_tex = 0;
    GLuint m_blitTex = 0;
    EGLImageKHR m_eglImage = nullptr;
    EGLImageKHR m_blitEGLImage = nullptr;
    const GLuint m_width = 0;
    const GLuint m_height = 0;
    GLuint m_fbo = 0;
    GLint m_internalFormat = 0;
    GLint m_sizedInternalFormat = 0;

    // This is helpful for bindFbo which may skip too many steps after the egl
    // image is replaced.
    bool m_needFboReattach = false;

    // |m_format| and |m_type| are for reformatting purposes only
    // to work around bugs in the guest. No need to snapshot those.
    bool m_needFormatCheck = true;
    GLenum m_format = 0; // TODO: Currently we treat m_internalFormat same as
                         // m_format, but if underlying drivers can take it,
                         // it may be a better idea to distinguish them, with
                         // m_internalFormat as an explicitly sized format; then
                         // guest can specify everything in terms of explicitly
                         // sized internal formats and things will get less
                         // ambiguous.
    GLenum m_type = 0;

    EGLDisplay m_display = nullptr;
    ContextHelper* m_helper = nullptr;
    TextureDraw* m_textureDraw = nullptr;
    TextureResize* m_resizer = nullptr;
    FrameworkFormat m_frameworkFormat;
    bool m_yuv420888ToNv21 = false;
    GLuint m_yuv_conversion_fbo = 0;  // FBO to offscreen-convert YUV to RGB
    GLuint m_scaleRotationFbo = 0;  // FBO to read scaled rotation pixels
    std::unique_ptr<YUVConverter> m_yuv_converter;
    HandleType mHndl;

    GLsync m_sync = nullptr;
    bool m_fastBlitSupported = false;
    bool m_vulkanOnly = false;

    GLenum m_asyncReadbackType = GL_UNSIGNED_BYTE;
    size_t m_numBytes = 0;

    bool m_importedMemory = false;
    GLuint m_memoryObject = 0;
    bool m_inUse = false;
    bool m_isBuffer = false;
    GLuint m_buf = 0;
    uint32_t m_displayId = 0;
    bool m_BRSwizzle = false;
};

typedef std::shared_ptr<ColorBufferGl> ColorBufferGlPtr;

}  // namespace gl
}  // namespace gfxstream
