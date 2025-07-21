/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "PixelReadFormats.h"

#include <GLES2/gl2ext.h>

#include <cstring>

#include "ContextHelper.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace gl {

bool PixelReadFormats::isSupported(GLenum internalFormat, GLenum texFormat, GLenum texType,
                                   GLenum readFormat, GLenum readType) {
    static const size_t MAX_PIXEL_SIZE_BYTES = 16;

    std::lock_guard<std::mutex> lock(mMutex);
    uint8_t data[MAX_PIXEL_SIZE_BYTES];
    PixelReadbackCombination fmtCombo{
        .internalFormat = internalFormat, .pixelFormat = texFormat, .pixelType = texType};

    // Check our cached results
    auto it = mSupportedFormats.find(fmtCombo);
    if (it != mSupportedFormats.end()) {
        return it->second;
    }

    // Not in our cache. Test glReadPixels to see if it works.
    GLint prevUnpackAlignment, prevAlignment;
    GLuint tex = 0;
    GLint oldTex = 0;

    // Get the currently bound texture to GL_TEXTURE_2D
    s_gles2.glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);

    s_gles2.glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    s_gles2.glGenTextures(1, &tex);

    s_gles2.glBindTexture(GL_TEXTURE_2D, tex);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 1, 1, 0, texFormat, texType, nullptr);

    GLint prevFbo = 0;
    GLuint fbo = 0;
    s_gles2.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);  // Save current FBO
    s_gles2.glGenFramebuffers(1, &fbo);
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, fbo);  // Bind new FBO
    s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex,
                                   0);  // Attach texture

    GLint prevViewport[4];
    s_gles2.glGetIntegerv(GL_VIEWPORT, prevViewport);  // Save viewport state

    // Some GL drivers may report GL_NO_ERROR on glReadPixels, but silently fails (e.g. swiftshader
    // GL glReadPixels w/ GL_RGB10_A2). Instead of checking for correctness of the pixel, which can
    // get overly complicated, let's just see if glReadPixels does something. We zero-initialize
    // our readback buffer, and set the clear color to a white color. If glReadPixels makes anything
    // in our readback buffer non-zero, we will assume that it works.
    memset(data, 0, MAX_PIXEL_SIZE_BYTES);
    s_gles2.glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    s_gles2.glClear(GL_COLOR_BUFFER_BIT);
    s_gles2.glViewport(0, 0, 1, 1);

    s_gles2.glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlignment);
    s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, 1);

    s_gles2.glGetError();
    s_gles2.glReadPixels(0, 0, 1, 1, readFormat, readType, data);
    GLenum error = s_gles2.glGetError();

    bool res = false;
    if (error == GL_NO_ERROR) {
        // Next, check if the pixel is non-zero
        for (size_t i = 0; i < MAX_PIXEL_SIZE_BYTES; i++) {
            if (data[i] != 0) {
                res = true;
                break;
            }
        }
        if (!res) {
            GFXSTREAM_ERROR(
                "glReadPixels returned GL_NO_ERROR for tex(internal:0x%x fmt:0x%x type:0x%x) "
                "read(fmt:0x%x type:0x%x), but data is zero.",
                internalFormat, texFormat, texType, readFormat, readType);
        } else {
            GFXSTREAM_VERBOSE(
                "glReadPixels seems OK for tex(internal:0x%x fmt:0x%x type:0x%x) read(fmt:0x%x "
                "type:0x%x).",
                internalFormat, texFormat, texType, readFormat, readType);
        }
    } else {
        GFXSTREAM_ERROR(
            "ColorBufferGl::readPixels NOT supported for tex(internal:0x%x fmt:0x%x type:0x%x) "
            "read(fmt:0x%x type:0x%x) error: 0x%x",
            internalFormat, texFormat, texType, readFormat, readType, error);
    }

    // Restore the GL context back to the previous state.
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);  // Restore previous FBO
    s_gles2.glDeleteFramebuffers(1, &fbo);               // Delete temporary FBO
    s_gles2.glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
                       prevViewport[3]);  // Restore viewport

    s_gles2.glPixelStorei(GL_PACK_ALIGNMENT, prevAlignment);
    s_gles2.glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    s_gles2.glBindTexture(GL_TEXTURE_2D, oldTex);
    s_gles2.glDeleteTextures(1, &tex);

    // Add to the cache
    mSupportedFormats[fmtCombo] = {res};

    return res;
}

}  // namespace gl
}  // namespace gfxstream
