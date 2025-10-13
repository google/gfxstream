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
#pragma once

#include <GLES2/gl2.h>

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "display_surface_gl.h"
#include "gfxstream/ThreadAnnotations.h"

namespace gfxstream {
namespace host {
namespace gl {

struct PixelReadbackCombination {
    GLenum internalFormat;
    GLenum pixelFormat;
    GLenum pixelType;

    bool operator==(const PixelReadbackCombination& other) const {
        return internalFormat == other.internalFormat && pixelFormat == other.pixelFormat &&
               pixelType == other.pixelType;
    }
};

}  // namespace gl
}  // namespace host
}  // namespace gfxstream

namespace std {
template <>
struct hash<gfxstream::host::gl::PixelReadbackCombination> {
    size_t operator()(const gfxstream::host::gl::PixelReadbackCombination& c) const {
        return hash<GLenum>()(c.internalFormat) ^ hash<GLenum>()(c.pixelFormat) ^
               hash<GLenum>()(c.pixelType);
    }
};
}  // namespace std

namespace gfxstream {
namespace host {
namespace gl {

// Helper class to determine if glReadPixels on a texture image with a given internal format and
// readback format/type is supported by the GL driver.
//
// For example, a driver that implements OpenGL ES is only required to implement glReadPixels for
// format: GL_RGBA, and GL_RGBA_INTEGER
// type: GL_UNSIGNED_BYTE, GL_UNSIGNED_INT, GL_INT, or GL_FLOAT
//
// while a OpenGL driver is required to support a lot more combinations.
class PixelReadFormats {
   public:
    PixelReadFormats() {}

    // Must have a GL context bounded as well as a framebuffer object.
    bool isSupported(GLenum internalFormat, GLenum texFormat, GLenum texType, GLenum format,
                     GLenum type);

   private:
    std::mutex mMutex;
    std::unordered_map<PixelReadbackCombination, bool> mSupportedFormats GUARDED_BY(mMutex);
};

}  // namespace gl
}  // namespace host
}  // namespace gfxstream