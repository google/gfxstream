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

#pragma once

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "FrameworkFormats.h"

#include <stdint.h>
#include <cstring>
#include <vector>

namespace gfxstream {
namespace gl {

enum class YUVPlane : int {
    Y = 0,
    U = 1,
    V = 2,
    UV = 3,
};

// The purpose of YUVConverter is to use
// OpenGL shaders to convert YUV images to RGB
// images that can be displayed on screen.
// Doing this on the GPU can be much faster than
// on the CPU.

// Usage:
// 0. Have a current OpenGL context working.
// 1. Constructing the YUVConverter object will allocate
//    OpenGL resources needed to convert, given the desired
//    |width| and |height| of the buffer.
// 2. To convert a given YUV buffer of |pixels|, call
//    the drawConvert method (with x, y, width, and height
//    arguments depicting the region to convert).
//    The RGB version of the YUV buffer will be drawn
//    to the current framebuffer. To retrieve
//    the result, if the user of the result is an OpenGL texture,
//    it suffices to have that texture be the color attachment
//    of the framebuffer object. Or, if you want the results
//    on the CPU, call glReadPixels() after the call to drawConvert().
class YUVConverter {
public:
    // call ctor when creating a gralloc buffer
    // with YUV format
    YUVConverter(int width, int height, FrameworkFormat format,
                 bool yuv420888ToNv21);
    // destroy when ColorBuffer is destroyed
    ~YUVConverter();
    // call when gralloc_unlock updates
    // the host color buffer
    // (rcUpdateColorBuffer)
    void drawConvert(int x, int y, int width, int height, const char* pixels);
    void drawConvertFromFormat(FrameworkFormat format, int x, int y, int width, int height,
                               const char* pixels, void* metadata = nullptr);

    uint32_t getDataSize();
    // read YUV data into pixels, exactly pixels_size bytes;
    // if size mismatches, will read nothing.
    void readPixels(uint8_t* pixels, uint32_t pixels_size);

    void swapTextures(FrameworkFormat type, GLuint* textures, void* metadata = nullptr);

    // public so other classes can call
    static void createYUVGLTex(GLenum textureUnit,
                               GLsizei width,
                               GLsizei height,
                               FrameworkFormat format,
                               bool yuv420888ToNv21,
                               YUVPlane plane,
                               GLuint* outTextureName);
private:
    void init(int w, int h, FrameworkFormat format);
    void reset();

    void createYUVGLShader();
    void createYUVGLFullscreenQuad();

    // For dealing with n-pixel-aligned buffers
    void updateCutoffs(float yWidth, float yStridePixels,
                       float uvWidth, float uvStridePixels);

    int mWidth = 0;
    int mHeight = 0;
    FrameworkFormat mFormat;
    // colorbuffer w/h/format, could be different
    FrameworkFormat mColorBufferFormat;
    // We need the following GL objects:
    GLuint mProgram = 0;
    GLuint mQuadVertexBuffer = 0;
    GLuint mQuadIndexBuffer = 0;
    GLuint mTextureY = 0;
    GLuint mTextureU = 0;
    GLuint mTextureV = 0;
    bool mTexturesSwapped = false;
    GLint mUniformLocYWidthCutoff = -1;
    GLint mUniformLocUVWidthCutoff = -1;
    GLint mUniformLocSamplerY = -1;
    GLint mUniformLocSamplerU = -1;
    GLint mUniformLocSamplerV = -1;
    GLint mAttributeLocPos = -1;
    GLint mAttributeLocTexCoord = -1;

    float mYWidthCutoff = 1.0;
    float mUVWidthCutoff = 1.0;
    bool mHasGlsl3Support = false;
    bool mYuv420888ToNv21 = false;

    // YUVConverter can end up being used
    // in a TextureDraw / subwindow context, and subsequently
    // overwrite the previous state.
    // This section is so YUVConverter can be used in the middle
    // of any GL context without impacting what's
    // already going on there, by saving/restoring the state
    // that it is impacting.
    void saveGLState();
    void restoreGLState();
    // Impacted state
    GLfloat mCurrViewport[4] = {};
    GLint mCurrTexUnit = 0;
    GLint mCurrProgram = 0;
    GLint mCurrTexBind = 0;
    GLint mCurrVbo = 0;
    GLint mCurrIbo = 0;

    // color aspects related information
    uint64_t mColorPrimaries = 4;  // this is 601, the default
    uint64_t mColorRange = 2;      // this is limited range, the default
    uint64_t mColorTransfer = 3;   // this is the default

    bool checkAndUpdateColorAspectsChanged(void* metadata);
};

}  // namespace gl
}  // namespace gfxstream
