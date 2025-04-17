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

#include <atomic>

#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES3/gl3.h>

#include "Compositor.h"
#include "gfxstream/host/display_surface_user.h"
#include "TextureDraw.h"

namespace gfxstream {
namespace gl {

class CompositorGl : public Compositor {
   public:
    CompositorGl(TextureDraw* textureDraw);
    virtual ~CompositorGl();

    CompositionFinishedWaitable compose(const CompositionRequest& compositionRequest) override;

  private:
    GLuint m_composeFbo = 0;

    // Owned by FrameBuffer.
    TextureDraw* m_textureDraw = nullptr;
};

}  // namespace gl
}  // namespace gfxstream
