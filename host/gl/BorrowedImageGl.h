// Copyright 2022 The Android Open Source Project
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

#pragma once

#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES3/gl3.h>

#include <functional>

#include "gfxstream/host/borrowed_image.h"

namespace gfxstream {
namespace gl {

struct BorrowedImageInfoGl : public BorrowedImageInfo {
    GLuint texture = 0;

    // Functor called once commands using this Image have been
    // issued (useful for setting a GL sync).
    std::function<void()> onCommandsIssued;
};

}  // namespace gl
}  // namespace gfxstream
