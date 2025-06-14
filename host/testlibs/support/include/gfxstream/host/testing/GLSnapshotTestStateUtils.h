// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "gfxstream/host/testing/GLSnapshotTesting.h"

#include <GLES2/gl2.h>
#include <GLES3/gl31.h>

namespace gfxstream {
namespace gl {

GLuint createBuffer(const GLESv2Dispatch* gl, GlBufferData data);

GLuint loadAndCompileShader(const GLESv2Dispatch* gl,
                            GLenum shaderType,
                            const char* src);

// Binds the active texture in target to a temporary framebuffer object
// and retrieves its texel data using glReadPixels.
std::vector<GLubyte> getTextureImageData(const GLESv2Dispatch* gl,
                                         GLuint texture,
                                         GLenum target,
                                         GLint level,
                                         GLsizei width,
                                         GLsizei height,
                                         GLenum format,
                                         GLenum type);

}  // namespace gl
}  // namespace gfxstream
